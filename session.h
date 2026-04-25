#pragma once

#include "server.h"
#include <deque>

// Session 代表“一个客户端连接”。
// 它有自己的状态机、读缓冲、写队列，负责把该连接上的文本命令翻译成业务动作。
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, Server& server);
    void start();
    void deliver(const std::string& msg);
    void close();

private:
    // 连接状态机：
    // - WaitName/WaitPassword：登录阶段
    // - Command：主命令阶段
    // - PrivateChat/GroupChat：子会话阶段（进入某个聊天上下文）
    enum class State {
        WaitName,
        WaitPassword,
        Command,
        PrivateChat,
        GroupChat
    };

    void read_loop();
    void on_line(const std::string& line);
    void handle_command(const std::string& cmd);
    void handle_private_chat(const std::string& line);
    void handle_group_chat(const std::string& line);
    void do_write();

    // 与该客户端对应的 socket（异步读写都发生在这个 socket 上）。
    tcp::socket socket_;
    // 指向全局 Server，用于查用户/群、发消息、关服等操作。
    Server& server_;
    // async_read_until 的累计接收缓冲。
    std::string read_buf_;
    // 发送队列：保证 async_write 串行，防止并发 write 导致消息交错。
    std::deque<std::string> write_queue_;
    // 保护 write_queue_ 和 write_in_progress_。
    std::mutex write_mutex_;
    // 是否已有一个 async_write 在飞行中。
    bool write_in_progress_ = false;

    // 当前状态机状态。
    State state_ = State::WaitName;
    // 已登录用户名。
    std::string username_;
    // 登录流程里暂存的用户名（等待输入密码后再正式注册）。
    std::string pending_name_;
    // 私聊目标用户名。
    std::string private_target_;
    // 群聊目标群名；为空时表示当前 GroupChat 状态其实是在“等待输入新群名”。
    std::string group_target_;
};
