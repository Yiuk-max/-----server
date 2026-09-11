#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/error.hpp>

#include "thread_pool.h"

// ============================================================
// WsServer：WebSocket 监听器
//   - 持有 acceptor（复用端口 8080，IPv6 双栈）
//   - 每个新连接接受后创建一个 WsSession（绑定到独立 strand）
//   - 业务线程池由外部持有并共享，保证生命周期覆盖所有会话
// ============================================================
class WsServer : public std::enable_shared_from_this<WsServer> {
public:
    WsServer(boost::asio::io_context& ioc,// 由外部持有，保证生命周期覆盖所有会话
             boost::asio::ip::tcp::endpoint endpoint,//类似socket的sockaddr_in结构，包含ip和端口和协议族信息
             std::shared_ptr<ThreadPool> pool,
             std::string path,// WebSocket 连接路径（如 /ws）
             std::size_t max_message_bytes,
             std::size_t max_pending_bytes,
             std::string web_root);// 静态前端资源目录（非升级请求时服务）

    void run();

private:
    void do_accept();// 异步接受新连接，成功后创建 WsSession 并调用 run()，失败则打印错误并继续接受。
    void on_accept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket);// 接受回调：成功则创建 WsSession 并调用 run()，失败则打印错误并继续接受。

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;   // 监听器必须在独立 strand 上创建，保证所有 IO 串行。
    std::shared_ptr<ThreadPool> pool_;          //所有session共享同一个业务线程池，保证生命周期覆盖所有会话。
    std::string path_;                          // WebSocket 连接路径（如 /ws）
    std::size_t max_message_bytes_;             // 每个连接的最大消息字节数    接收
    std::size_t max_pending_bytes_;             // 每个连接的最大待发送字节数  发送
    std::string web_root_;                       // 静态前端资源目录
};

// 阻塞运行 WebSocket 服务器（读取 ServerConfig），直到收到 SIGINT/SIGTERM,优雅退出
int run_websocket_server(unsigned short port);
