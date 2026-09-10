#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "client_transport.h"
#include "thread_pool.h"

class client_session;

// ============================================================
// WsSession：一个 WebSocket 连接 = 一个会话 + 一个 IClientTransport 实现
//
// 线程模型：
//   - 所有 socket 读写、握手、close 都在该连接所属的 strand 上串行执行。
//   - 业务线程调用 send_packet()/close() 时，只把任务 post 回 strand，
//     绝不从业务线程直接触碰 Beast stream。
//   - 收到一条完整消息后提交给业务线程池；该业务任务完成后才恢复下一次读，
//     从而保证同一连接的消息按序处理（同时也形成单连接读背压）。
// ============================================================
class WsSession final : public IClientTransport,
                        public std::enable_shared_from_this<WsSession> {
public:
    WsSession(boost::asio::ip::tcp::socket&& socket,
              std::shared_ptr<ThreadPool> business_pool,
              std::string path,
              std::size_t max_message_bytes,
              std::size_t max_pending_bytes);

    // 开始握手与读循环（构造后由 WsServer 调用一次）。
    void run();

    // ---------- IClientTransport ----------
    void send_packet(nlohmann::json message, std::string file_data = {}) override;
    void send_file(std::string file_name) override;
    void accept_file_chunk(nlohmann::json meta, std::string file_data) override;
    void close(CloseMode mode) override;

private:
    std::shared_ptr<WsSession> self();
    void on_http_read(boost::beast::error_code ec, std::size_t bytes);
    void send_http_response_and_close(boost::beast::http::status status, std::string body);
    void on_accept(boost::beast::error_code ec);
    void do_read();
    void on_read(boost::beast::error_code ec, std::size_t bytes);
    void dispatch_to_business(std::string json_text, std::string file_data);
    void on_business_done();

    // 空闲超时：与 binary 层 use_heartbeat / heartbeat_interval 行为对齐。
    void touch();
    void schedule_idle_check();

    void enqueue(std::string payload, bool is_text);
    void do_write();
    void on_write(boost::beast::error_code ec, std::size_t bytes);

    void do_close();
    void on_close(boost::beast::error_code ec);
    void teardown();
    void fail(boost::beast::error_code ec, const char* what);

    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;  // WebSocket stream，绑定到独立 strand 上
    boost::asio::steady_timer idle_timer_;                          // 空闲检测计时器，绑定到独立 strand 上
    std::chrono::steady_clock::time_point last_active_;             // 最近一次读/写/心跳的时间点，绑定到独立 strand 上
    boost::beast::flat_buffer buffer_;                              // WebSocket 读缓冲区，绑定到独立 strand 上
    boost::beast::http::request<boost::beast::http::string_body> request_; // 握手阶段的 HTTP 请求，绑定到独立 strand 上

    std::shared_ptr<ThreadPool> business_pool_;// 业务线程池由外部持有并共享，保证生命周期覆盖所有会话
    std::shared_ptr<client_session> session_;

    std::deque<std::pair<std::string, bool>> write_queue_;  // {payload, is_text}
    std::size_t pending_bytes_ = 0;
    bool write_in_progress_    = false;
    bool closing_after_write_  = false;
    bool closed_               = false;
    bool disconnected_         = false;

    std::string path_;
    std::size_t max_message_bytes_;
    std::size_t max_pending_bytes_;
};
