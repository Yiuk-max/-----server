#include "session.h"
#include <utility>

static std::string trim_line(const std::string& text) {
    std::string out = text;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

Session::Session(tcp::socket socket, Server& server)
    : socket_(std::move(socket)), server_(server) {}

void Session::start() {
    // 会话启动时先进入登录流程的第一步：输入用户名。
    deliver("Please enter your name : \n");
    // 启动异步读取链。之后每收到一行都会再次 read_loop()。
    read_loop();
}

void Session::read_loop() {
    auto self = shared_from_this();
    // 按“行协议”读取：收到 '\n' 才触发一次 on_line。
    // 这要求客户端发送的每条命令以换行结尾。
    asio::async_read_until(
        socket_,
        asio::dynamic_buffer(read_buf_),
        '\n',
        [self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                // 对端关闭/网络错误：关闭本会话。
                self->close();
                return;
            }
            // 从累计缓冲取出这一次完整的一行。
            std::string line = self->read_buf_.substr(0, bytes_transferred);
            self->read_buf_.erase(0, bytes_transferred);
            // 状态机分发：不同状态下同一行文本含义不同。
            self->on_line(trim_line(line));
            // 继续挂下一次读取，形成“常驻读取链”。
            self->read_loop();
        });
}

void Session::on_line(const std::string& line) {
    switch (state_) {
        case State::WaitName:
            // 第一步拿到用户名，进入密码阶段。
            pending_name_ = line;
            deliver("Please enter your password : \n");
            state_ = State::WaitPassword;
            break;
        case State::WaitPassword:
            // 简化版登录校验：密码固定 123456，同时用户名必须可注册成功。
            if (line == "123456" && server_.register_user(pending_name_, shared_from_this())) {
                username_ = pending_name_;
                state_ = State::Command;
                deliver("Login successful\n");
                deliver(server_.build_chat_list());
            } else {
                deliver("Login failed\n");
                close();
            }
            break;
        case State::Command:
            handle_command(line);
            break;
        case State::PrivateChat:
            handle_private_chat(line);
            break;
        case State::GroupChat:
            handle_group_chat(line);
            break;
    }
}

void Session::handle_command(const std::string& cmd) {
    if (cmd.rfind("/spk_to:", 0) == 0) {
        // /spk_to:xxx 先判断 xxx 是群还是人，再切换到对应子状态。
        std::string target = trim_line(cmd.substr(std::string("/spk_to:").size()));
        if (server_.group_exists(target)) {
            group_target_ = target;
            state_ = State::GroupChat;
            deliver("[group chat]\n");
            return;
        }
        if (server_.user_exists(target)) {
            private_target_ = target;
            state_ = State::PrivateChat;
            deliver("[private chat]\n");
            return;
        }
        deliver("target not found\n");
        return;
    }
    if (cmd == "/show") {
        deliver(server_.build_chat_list());
        return;
    }
    if (cmd == "/create_group") {
        // 复用 GroupChat 状态做“输入新群名”的一步式流程：
        // group_target_ 为空表示接下来第一行不是聊天内容，而是新群名。
        deliver("plz enter group's name:\n");
        state_ = State::GroupChat;
        group_target_.clear();
        return;
    }
    if (cmd == "/exit") {
        server_.shutdown_all();
        return;
    }
}

void Session::handle_private_chat(const std::string& line) {
    if (line == "/exit") {
        // 退出私聊子状态，回到主命令状态。
        deliver("\n[exit from spk_personally]\n");
        state_ = State::Command;
        private_target_.clear();
        return;
    }
    if (!server_.send_private(username_, private_target_, line)) {
        deliver("user not found!\n");
        state_ = State::Command;
        private_target_.clear();
    }
}

void Session::handle_group_chat(const std::string& line) {
    if (group_target_.empty()) {
        // group_target_ 为空时，说明这是“创建群聊流程”里的群名输入。
        std::string new_group_name = trim_line(line);
        if (server_.create_group(new_group_name, username_)) {
            deliver("create group [" + new_group_name + "] success!\n");
        } else {
            deliver("can be same with group/person\n");
        }
        state_ = State::Command;
        return;
    }

    if (line == "/exit") {
        state_ = State::Command;
        return;
    }
    if (line.rfind("/add_client:", 0) == 0) {
        // 恢复旧功能：群聊态支持动态拉人。
        deliver(server_.build_chat_list());
        const std::string target = trim_line(line.substr(std::string("/add_client:").size()));
        if (!server_.add_user_to_group(group_target_, target)) {
            deliver("user not found!\n");
        }
        return;
    }
    if (line.rfind("/delete_client:", 0) == 0) {
        // 恢复旧功能：群聊态支持动态踢人。
        deliver(server_.build_chat_list());
        const std::string target = trim_line(line.substr(std::string("/delete_client:").size()));
        if (!server_.remove_user_from_group(group_target_, target)) {
            deliver("user not found!\n");
        }
        return;
    }
    if (!server_.send_group(group_target_, username_, line)) {
        deliver("group not found\n");
        state_ = State::Command;
        group_target_.clear();
    }
}

void Session::deliver(const std::string& msg) {
    bool need_start_write = false;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push_back(msg);
        if (!write_in_progress_) {
            // 当前没有写操作在飞，当前消息将触发第一段 async_write。
            write_in_progress_ = true;
            need_start_write = true;
        }
    }
    if (need_start_write) {
        do_write();
    }
}

void Session::do_write() {
    auto self = shared_from_this();
    std::string current;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.empty()) {
            // 队列已清空，写链结束，等待下一次 deliver 重新拉起。
            write_in_progress_ = false;
            return;
        }
        current = write_queue_.front();
    }
    // 串行异步写：每次只写队头一个消息，完成后再写下一个。
    asio::async_write(
        socket_,
        asio::buffer(current),
        [self, current](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                self->close();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(self->write_mutex_);
                if (!self->write_queue_.empty()) {
                    // 当前包写完，弹出队头。
                    self->write_queue_.pop_front();
                }
            }
            // 递归推进写链，直到队列清空。
            self->do_write();
        });
}

void Session::close() {
    auto self = shared_from_this();
    // 把关闭动作 post 回 socket 所在线程执行器，避免并发 close 的竞态。
    asio::post(socket_.get_executor(), [self]() {
        // 从 Server 的全局映射中摘除当前会话。
        self->server_.unregister_user(self->username_, self);
        boost::system::error_code ignored;
        self->socket_.shutdown(tcp::socket::shutdown_both, ignored);
        self->socket_.close(ignored);
    });
}
