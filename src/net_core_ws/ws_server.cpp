#include "ws_server.h"

#include <algorithm>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>

#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/v6_only.hpp>

#include "server_config.h"
#include "ws_session.h"

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = boost::asio::ip::tcp;

WsServer::WsServer(net::io_context& ioc,
                   tcp::endpoint endpoint,
                   std::shared_ptr<ThreadPool> pool,
                   std::string path,
                   std::size_t max_message_bytes,
                   std::size_t max_pending_bytes)
    : ioc_(ioc),
      acceptor_(net::make_strand(ioc)),
      pool_(std::move(pool)),
      path_(std::move(path)),
      max_message_bytes_(max_message_bytes),
      max_pending_bytes_(max_pending_bytes) {
    beast::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);// 监听器必须在独立 strand 上创建，保证所有 IO 串行。
    if (ec) {
        throw std::runtime_error("ws acceptor open failed: " + ec.message());
    }

    // IPv6 监听同时接受 IPv4（与现有 TCP 网络层行为一致）。
    if (endpoint.protocol() == tcp::v6()) {
        acceptor_.set_option(net::ip::v6_only(false), ec);
        ec = {};  // 某些平台不支持双栈时忽略，继续按 v6-only 运行
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("ws acceptor reuse_address failed: " + ec.message());
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        throw std::runtime_error("ws acceptor bind failed: " + ec.message());
    }

    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        throw std::runtime_error("ws acceptor listen failed: " + ec.message());
    }
}

void WsServer::run() {
    do_accept();
}

void WsServer::do_accept() {
    // 每个连接使用独立 strand，保证单连接所有 IO 串行。
    auto socket = std::make_shared<tcp::socket>(net::make_strand(ioc_));
    auto self = shared_from_this();
    acceptor_.async_accept(*socket, [self, socket](beast::error_code ec) {//asyn_accept 回调在 acceptor strand 上执行，创建 WsSession 后再切换到其独立 strand。
        self->on_accept(ec, std::move(*socket));// 成功则创建 WsSession 并调用 run()，失败则打印错误并继续接受。
    });
}

void WsServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        if (ec != net::error::operation_aborted) {
            std::cerr << "[ws] accept: " << ec.message() << std::endl;
        }
    } else {
        std::make_shared<WsSession>(std::move(socket), pool_, path_,
                                    max_message_bytes_, max_pending_bytes_)
            ->run();
    }
    do_accept();
}

int run_websocket_server(unsigned short port) {
    auto& cfg = ServerConfig::get_instance();
    const int io_threads = std::max(1, cfg.ws_io_threads());

    try {
        net::io_context ioc{io_threads};
        auto pool = std::make_shared<ThreadPool>(8);

        tcp::endpoint endpoint{tcp::v6(), port};
        auto server = std::make_shared<WsServer>(ioc, endpoint, pool,
                                                 cfg.ws_path(),
                                                 cfg.ws_max_message_bytes(),
                                                 cfg.ws_max_pending_bytes());
        server->run();

        std::cout << "[ws] WebSocket server listening on port " << port
                  << ", path " << cfg.ws_path()
                  << ", io_threads " << io_threads << std::endl;

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const beast::error_code&, int) { ioc.stop(); });

        std::vector<std::thread> threads;
        for (int i = 1; i < io_threads; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }
        ioc.run();
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        // 先停业务线程，确保之后不再有任务向即将销毁的 io_context 投递。
        pool->stop_pool();
    } catch (const std::exception& e) {
        std::cerr << "[ws] fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
