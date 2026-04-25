#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class Session;

// Server 管“全局共享状态”和“接入层”：
// 1) 监听端口 + 异步接收新连接
// 2) 管理在线会话、用户名映射、群组成员关系
// 3) 提供私聊/群聊路由能力
// 4) 统一执行关服流程
class Server {
public:
    explicit Server(asio::io_context& io);

    void start_accept();
    bool register_user(const std::string& name, const std::shared_ptr<Session>& session);
    void unregister_user(const std::string& name, const std::shared_ptr<Session>& session);
    std::string build_chat_list();
    bool create_group(const std::string& group_name, const std::string& creator_name);
    bool user_exists(const std::string& username);
    bool group_exists(const std::string& group_name);
    bool send_private(const std::string& from, const std::string& to, const std::string& msg);
    bool send_group(const std::string& group_name, const std::string& from, const std::string& msg);
    void shutdown_all();

private:
    // do_accept() 会形成“自我续订”的异步 accept 链。
    // 每次收到一个连接，都创建 Session，然后继续挂下一次 async_accept。
    void do_accept();

    // io_context 引用：所有异步网络事件都在这个事件循环里调度。
    asio::io_context& io_;
    // acceptor：监听端口并异步接收新连接。
    tcp::acceptor acceptor_;
    // 保护下面这些共享容器，避免多线程 io.run() 并发访问时数据竞争。
    std::mutex mutex_;
    // 当前存活的会话对象（强引用），用于关服广播等全量操作。
    std::unordered_set<std::shared_ptr<Session>> sessions_;
    // 用户名 -> 会话弱引用。弱引用避免 Server 与 Session 互相强持有导致泄漏。
    std::unordered_map<std::string, std::weak_ptr<Session>> users_;
    // 群名 -> 群成员用户名集合。
    std::unordered_map<std::string, std::unordered_set<std::string>> groups_;
    // 关服标记，防止重复执行 shutdown_all。
    bool shutting_down_ = false;
};
