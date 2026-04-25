#include <algorithm>
#include <iostream>
#include "server.h"
#include <thread>
#include <vector>

int main() {
    try {
        // io_context 是 Asio 的“事件循环核心”：
        // - 所有 async_* 异步操作都把完成事件投递到这里
        // - run() 会不断取出事件并执行对应回调
        asio::io_context io;

        // Server 负责监听端口、接收连接、维护在线用户/群聊等全局状态。
        Server server(io);

        // 启动异步 accept 链：
        // 第一次发起 async_accept，后续每次 accept 完成都会再次发起下一次。
        server.start_accept();
        std::cout << "Server is listening on port 8080..." << std::endl;

        // 这里就是“线程池”：
        // 多个线程同时调用 io.run()，共同消费同一个 io_context 事件队列。
        // 这样多个连接回调可以并发执行，但不需要“每个连接一个线程”。
        const std::size_t thread_count = std::max<std::size_t>(2, std::thread::hardware_concurrency());
        std::vector<std::thread> workers;
        workers.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            // 每个 worker 线程都进入事件循环，等待并处理异步事件。
            workers.emplace_back([&io]() { io.run(); });
        }

        // 主线程等待所有 worker 退出（通常是服务器关闭时）。
        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const std::exception& ex) {
        std::cerr << "Server error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}