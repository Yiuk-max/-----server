#include "server.h"
#include "session.h"
#include <iostream>

Server::Server(asio::io_context& io)
    : io_(io),
      acceptor_(io, tcp::endpoint(tcp::v6(), 8080)) {
    // 允许端口快速复用，避免服务重启时因为 TIME_WAIT 绑定失败。
    acceptor_.set_option(asio::socket_base::reuse_address(true));
}

void Server::start_accept() {
    do_accept();
}

bool Server::register_user(const std::string& name, const std::shared_ptr<Session>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 用户名为空或已存在，注册失败（保持用户名唯一）。
    if (name.empty() || users_.count(name) != 0) {
        return false;
    }
    users_[name] = session;
    sessions_.insert(session);
    return true;
}

void Server::unregister_user(const std::string& name, const std::shared_ptr<Session>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(name);
    if (it != users_.end() && it->second.lock() == session) {
        users_.erase(it);
    }
    // 用户离线时，从所有群成员列表里移除，避免保留脏成员。
    for (auto& group_item : groups_) {
        group_item.second.erase(name);
    }
    sessions_.erase(session);
}

std::string Server::build_chat_list() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 沿用你原来协议格式：先 group，再 person。
    std::string out = "group\n";
    for (const auto& item : groups_) {
        out += item.first + "\n";
    }
    out += "person:\n";
    for (const auto& item : users_) {
        out += item.first + "\n";
    }
    return out;
}

bool Server::create_group(const std::string& group_name, const std::string& creator_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 群名不能空，不能与已有群名冲突，也不能与用户名冲突。
    if (group_name.empty() || groups_.count(group_name) != 0 || users_.count(group_name) != 0) {
        return false;
    }
    groups_[group_name].insert(creator_name);
    return true;
}

bool Server::add_user_to_group(const std::string& group_name, const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto git = groups_.find(group_name);
    if (git == groups_.end()) {
        return false;
    }
    if (users_.count(username) == 0) {
        return false;
    }
    git->second.insert(username);
    return true;
}

bool Server::remove_user_from_group(const std::string& group_name, const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto git = groups_.find(group_name);
    if (git == groups_.end()) {
        return false;
    }
    auto erased = git->second.erase(username);
    return erased > 0;
}

bool Server::user_exists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.count(username) != 0;
}

bool Server::group_exists(const std::string& group_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return groups_.count(group_name) != 0;
}

bool Server::send_private(const std::string& from, const std::string& to, const std::string& msg) {
    std::shared_ptr<Session> target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(to);
        if (it == users_.end()) {
            return false;
        }
        target = it->second.lock();
    }
    // 弱引用失效，说明目标会话已断开。
    if (!target) {
        return false;
    }
    target->deliver("[" + from + "]:" + msg + "\n");
    return true;
}

bool Server::send_group(const std::string& group_name, const std::string& from, const std::string& msg) {
    std::vector<std::shared_ptr<Session>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto git = groups_.find(group_name);
        if (git == groups_.end()) {
            return false;
        }
        for (const auto& member : git->second) {
            auto uit = users_.find(member);
            if (uit != users_.end()) {
                auto sp = uit->second.lock();
                if (sp) {
                    targets.push_back(sp);
                }
            }
        }
    }
    // 注意这里先复制目标列表，再在锁外发送，避免“持锁期间执行网络操作”。
    const std::string payload = "[group " + group_name + "][" + from + "]:" + msg + "\n";
    for (const auto& target : targets) {
        target->deliver(payload);
    }
    return true;
}

void Server::shutdown_all() {
    std::vector<std::shared_ptr<Session>> all_sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutting_down_) {
            return;
        }
        // 只允许第一次进入的人执行关服，其它线程直接返回。
        shutting_down_ = true;
        all_sessions.assign(sessions_.begin(), sessions_.end());
    }
    for (const auto& session : all_sessions) {
        session->deliver("SERVER POWEROFF\n");
        session->close();
    }
    boost::system::error_code ec;
    // 关闭 acceptor，阻止新连接进入。
    acceptor_.close(ec);
    // 停止 io_context，使 run() 逐步退出。
    io_.stop();
}

void Server::do_accept() {
    // 核心 I/O 复用入口：非阻塞挂起一个 accept 操作，完成后由回调处理。
    acceptor_.async_accept([this](const boost::system::error_code& ec, tcp::socket socket) {
        if (!ec) {
            // 每来一个连接就创建一个会话对象，并启动该会话的异步读链。
            std::make_shared<Session>(std::move(socket), *this)->start();
        } else if (ec != asio::error::operation_aborted) {
            std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        // 关键：继续挂下一次 accept，形成持续接入能力。
        if (!shutting_down_) {
            do_accept();
        }
    });
}
