#include "ws_session.h"

#include <chrono>
#include <iostream>

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>

#include "client_session.h"
#include "ws_protocol.h"

namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
using tcp           = boost::asio::ip::tcp;

WsSession::WsSession(tcp::socket&& socket,
                     std::shared_ptr<ThreadPool> business_pool,
                     std::string path,
                     std::size_t max_message_bytes,
                     std::size_t max_pending_bytes)
    : ws_(std::move(socket)),
      business_pool_(std::move(business_pool)),
      path_(std::move(path)),
      max_message_bytes_(max_message_bytes),
      max_pending_bytes_(max_pending_bytes) {
    // client_session 只依赖 IClientTransport，实际协议由本类负责。
    session_ = std::make_shared<client_session>();
}

std::shared_ptr<WsSession> WsSession::self() {
    try {
        return shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return nullptr;
    }
}

void WsSession::run() {
    session_->set_transport(shared_from_this());

    // 握手阶段的 HTTP 读取加超时；升级后由下面的 suggested 超时接管。
    ws_.next_layer().expires_after(std::chrono::seconds(30));
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.read_message_max(max_message_bytes_);

    auto self = shared_from_this();
    http::async_read(ws_.next_layer(), buffer_, request_,
                     beast::bind_front_handler(&WsSession::on_http_read, self));
}

void WsSession::on_http_read(beast::error_code ec, std::size_t /*bytes*/) {
    if (ec) {
        fail(ec, "http_read");
        return;
    }

    if (!websocket::is_upgrade(request_)) {
        send_http_response_and_close(
            http::status::upgrade_required,
            "WebSocket upgrade required. Connect to ws://<host>:<port>" + path_ + "\n");
        return;
    }

    // 只接受配置的路径（允许带 query）。
    std::string target = std::string(request_.target());
    auto query = target.find('?');
    std::string path = (query == std::string::npos) ? target : target.substr(0, query);
    if (path != path_) {
        send_http_response_and_close(http::status::not_found, "Not found: " + path + "\n");
        return;
    }

    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server, "chat-server-ws");
        }));

    auto self = shared_from_this();
    ws_.async_accept(request_, beast::bind_front_handler(&WsSession::on_accept, self));
}

void WsSession::send_http_response_and_close(http::status status, std::string body) {
    auto response = std::make_shared<http::response<http::string_body>>(status, request_.version());
    response->set(http::field::content_type, "text/plain");
    response->body() = std::move(body);
    response->prepare_payload();

    auto self = shared_from_this();
    http::async_write(ws_.next_layer(), *response,
                      [self, response](beast::error_code ec, std::size_t) {
                          self->teardown();  // 纯 HTTP 响应，直接关底层 socket
                      });
}

void WsSession::on_accept(beast::error_code ec) {
    if (ec) {
        fail(ec, "accept");
        return;
    }
    do_read();
}

void WsSession::do_read() {
    if (closed_) {
        return;
    }
    auto self = shared_from_this();
    ws_.async_read(buffer_, beast::bind_front_handler(&WsSession::on_read, self));
}

void WsSession::on_read(beast::error_code ec, std::size_t /*bytes*/) {
    if (ec) {
        fail(ec, "read");
        return;
    }

    const bool is_text = ws_.got_text();
    std::string payload = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());

    std::string json_text;
    std::string file_data;

    if (is_text) {
        json_text = std::move(payload);
    } else {
        std::string error;
        if (!ws_protocol::decode_binary(payload, json_text, file_data, error)) {
            // 协议错误：提示后恢复读取，不关闭连接。
            send_packet({{"type", "system"},
                         {"content", "Invalid binary frame: " + error + ".\n"}},
                        {});
            do_read();
            return;
        }
    }

    dispatch_to_business(std::move(json_text), std::move(file_data));
}

void WsSession::dispatch_to_business(std::string json_text, std::string file_data) {
    auto self = shared_from_this();
    try {
        business_pool_->submit_task(
            [self, json_text = std::move(json_text), file_data = std::move(file_data)]() mutable {
                if (self->session_) {
                    self->session_->on_message(json_text, file_data);
                }
                // 业务处理完成后回到本连接的 strand，再发起下一次读。
                net::post(self->ws_.get_executor(), [self]() { self->on_business_done(); });
            });
    } catch (const std::exception& e) {
        std::cerr << "[WsSession] business pool submit failed: " << e.what() << std::endl;
        fail(net::error::operation_aborted, "business_pool");
    }
}

void WsSession::on_business_done() {
    if (closed_) {
        return;
    }
    do_read();
}

// ---------------- IClientTransport ----------------

void WsSession::send_packet(nlohmann::json message, std::string file_data) {
    const bool is_text = file_data.empty();
    std::string payload;
    try {
        payload = is_text ? message.dump() : ws_protocol::encode_binary(message, file_data);
    } catch (const std::exception& e) {
        std::cerr << "[WsSession] encode failed: " << e.what() << std::endl;
        return;
    }

    auto s = self();
    if (!s) {
        return;
    }
    net::post(ws_.get_executor(),
              [s, payload = std::move(payload), is_text]() mutable {
                  s->enqueue(std::move(payload), is_text);
              });
}

void WsSession::send_file(std::string /*file_name*/) {
    // M1 暂不支持 WebSocket 文件下载，M3 再实现二进制块协议。
    send_packet({{"type", "system"},
                 {"content", "File transfer is not supported over WebSocket yet.\n"}},
                {});
}

void WsSession::accept_file_chunk(nlohmann::json /*meta*/, std::string /*file_data*/) {
    // M1 暂不支持 WebSocket 文件上传，M3 再实现二进制块协议。
    send_packet({{"type", "system"},
                 {"content", "File transfer is not supported over WebSocket yet.\n"}},
                {});
}

void WsSession::close(CloseMode mode) {
    auto s = self();
    if (!s) {
        return;
    }
    net::post(ws_.get_executor(), [s, mode]() {
        if (s->closed_) {
            return;
        }
        if (mode == CloseMode::immediate) {
            s->teardown();
            return;
        }
        // 等待已入队的消息写完再发起 WebSocket close handshake。
        if (s->write_in_progress_ || !s->write_queue_.empty()) {
            s->closing_after_write_ = true;
            return;
        }
        s->do_close();
    });
}

// ---------------- 发送队列（仅在 strand 上执行） ----------------

void WsSession::enqueue(std::string payload, bool is_text) {
    if (closed_) {
        return;
    }
    if (payload.size() > max_pending_bytes_ ||
        pending_bytes_ + payload.size() > max_pending_bytes_) {
        std::cerr << "[WsSession] send queue overflow, closing connection" << std::endl;
        teardown();
        return;
    }

    pending_bytes_ += payload.size();
    write_queue_.emplace_back(std::move(payload), is_text);
    if (!write_in_progress_) {
        do_write();
    }
}

void WsSession::do_write() {
    if (write_queue_.empty()) {
        write_in_progress_ = false;
        if (closing_after_write_) {
            do_close();
        }
        return;
    }

    write_in_progress_ = true;
    ws_.text(write_queue_.front().second);
    auto self = shared_from_this();
    ws_.async_write(net::buffer(write_queue_.front().first),// 发送完成后回到 strand 再处理队列 
                    beast::bind_front_handler(&WsSession::on_write, self));
}

void WsSession::on_write(beast::error_code ec, std::size_t /*bytes*/) {//on_write 只在 strand 上执行，因此无需额外锁。
    if (ec) {
        fail(ec, "write");
        return;
    }
    if (!write_queue_.empty()) {//如果队列不为空，说明还有待发送的消息，继续发送下一条
        pending_bytes_ -= write_queue_.front().first.size();
        write_queue_.pop_front();
    }
    do_write();
}

// ---------------- 关闭 ----------------

void WsSession::do_close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    write_queue_.clear();
    pending_bytes_ = 0;

    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal,
                    beast::bind_front_handler(&WsSession::on_close, self));
}

void WsSession::on_close(beast::error_code /*ec*/) {
    teardown();
}

void WsSession::teardown() {
    if (disconnected_) {
        return;
    }
    disconnected_ = true;
    closed_ = true;
    write_queue_.clear();
    pending_bytes_ = 0;

    if (session_) {
        session_->on_disconnected();
    }

    beast::error_code ec;
    beast::get_lowest_layer(ws_).socket().close(ec);
}

void WsSession::fail(beast::error_code ec, const char* what) {
    if (ec == net::error::operation_aborted || ec == websocket::error::closed) {
        teardown();
        return;
    }
    std::cerr << "[WsSession] " << what << ": " << ec.message() << std::endl;
    teardown();
}
