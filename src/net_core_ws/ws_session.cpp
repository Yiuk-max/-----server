#include "ws_session.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <tuple>

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>

#include "client_session.h"
#include "server_config.h"
#include "ws_protocol.h"

namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
using tcp           = boost::asio::ip::tcp;

namespace {
// 仅允许白名单扩展名；返回空串表示不服务该类型。
std::string static_content_type(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".woff")  return "font/woff";
    if (ext == ".map")   return "application/json; charset=utf-8";
    return {};
}
}  // namespace

WsSession::WsSession(tcp::socket&& socket,
                     std::shared_ptr<ThreadPool> business_pool,
                     std::string path,
                     std::size_t max_message_bytes,
                     std::size_t max_pending_bytes,
                     std::string web_root)
    : ws_(std::move(socket)),
      idle_timer_(ws_.get_executor()),
      last_active_(std::chrono::steady_clock::now()),
      business_pool_(std::move(business_pool)),
      path_(std::move(path)),
      max_message_bytes_(max_message_bytes),
      max_pending_bytes_(max_pending_bytes),
      web_root_(std::move(web_root)) {
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

    // HTTP 升级请求读取单独设 30s 超时（tcp_stream 定时器）。
    // 注意：tcp_stream 的 expires_after 是整条连接的硬超时，必须在读完请求后
    // 用 expires_never() 清除，否则连接会在 30s 后被 beast::error::timeout 掉线。
    ws_.next_layer().expires_after(std::chrono::seconds(30));

    // WebSocket 自身超时：握手/关闭握手 30s；空闲超时关闭（idle_timeout=none），
    // 交给 M2 的 use_heartbeat 空闲检测负责，与 binary 层行为一致；不额外发 ping。
    websocket::stream_base::timeout opt{
        std::chrono::seconds(30),
        websocket::stream_base::none(),
        false};
    ws_.set_option(opt);
    ws_.read_message_max(max_message_bytes_);

    auto self = shared_from_this();
    http::async_read(ws_.next_layer(), buffer_, request_,
                     beast::bind_front_handler(&WsSession::on_http_read, self));
}

void WsSession::on_http_read(beast::error_code ec, std::size_t /*bytes*/) {
    // HTTP 请求已读完（或失败），清除 tcp_stream 的 30s 硬超时。
    ws_.next_layer().expires_never();
    if (ec) {
        fail(ec, "http_read");
        return;
    }

    if (!websocket::is_upgrade(request_)) {
        // 普通 HTTP 请求：作为静态前端资源响应（/ -> /index.html）。
        handle_static_request();
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

void WsSession::handle_static_request() {
    // 静态资源只允许 GET / HEAD。
    if (request_.method() != http::verb::get && request_.method() != http::verb::head) {
        send_http_response_and_close(http::status::method_not_allowed, "Method Not Allowed\n");
        return;
    }

    std::string target(request_.target());
    auto query = target.find('?');
    if (query != std::string::npos) {
        target = target.substr(0, query);
    }
    if (target.empty() || target == "/") {
        target = "/index.html";
    }

    // 安全：拒绝目录穿越与空字节（不额外做 URL 解码，编码后的 .. 只会被当作普通文件名）。
    if (target.find("..") != std::string::npos || target.find('\0') != std::string::npos) {
        send_http_response_and_close(http::status::bad_request, "Bad Request\n");
        return;
    }

    const std::string content_type = static_content_type(target);
    if (content_type.empty()) {
        send_http_response_and_close(http::status::not_found, "Not found: " + target + "\n");
        return;
    }

    std::string file_path = web_root_;
    if (!file_path.empty() && file_path.back() == '/') {
        file_path.pop_back();
    }
    file_path += target;

    beast::error_code ec;
    http::file_body::value_type body;
    body.open(file_path.c_str(), beast::file_mode::scan, ec);
    if (ec) {
        send_http_response_and_close(http::status::not_found, "Not found: " + target + "\n");
        return;
    }

    const auto size = body.size();

    if (request_.method() == http::verb::head) {
        auto res = std::make_shared<http::response<http::empty_body>>(
            http::status::ok, request_.version());
        res->set(http::field::server, "chat-server-ws");
        res->set(http::field::content_type, content_type);
        res->content_length(size);
        res->keep_alive(false);
        auto self = shared_from_this();
        http::async_write(ws_.next_layer(), *res,
                          [self, res](beast::error_code, std::size_t) { self->teardown(); });
        return;
    }

    auto res = std::make_shared<http::response<http::file_body>>(
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, request_.version()));
    res->set(http::field::server, "chat-server-ws");
    res->set(http::field::content_type, content_type);
    res->content_length(size);
    res->keep_alive(false);

    auto self = shared_from_this();
    http::async_write(ws_.next_layer(), *res,
                      [self, res](beast::error_code, std::size_t) { self->teardown(); });
}

void WsSession::on_accept(beast::error_code ec) {
    if (ec) {
        fail(ec, "accept");
        return;
    }
    do_read();
    schedule_idle_check();
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
    touch();

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

// ---------------- 空闲超时 ----------------

void WsSession::touch() {
    last_active_ = std::chrono::steady_clock::now();
}

// 每秒检查一次，与 binary 层 sub_reactor::check_idle_connections 的节奏一致。
// 未启用 use_heartbeat 时不启动计时器。
void WsSession::schedule_idle_check() {
    if (closed_ || !ServerConfig::get_instance().use_heartbeat()) {
        return;
    }
    idle_timer_.expires_after(std::chrono::seconds(1));
    auto self = shared_from_this();
    idle_timer_.async_wait([self](beast::error_code ec) {
        if (ec || self->closed_) {
            return;
        }
        const int timeout_s = ServerConfig::get_instance().heartbeat_interval();
        const auto idle_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - self->last_active_).count();
        if (idle_s >= timeout_s) {
            std::cout << "[WsSession] idle timeout, closing connection" << std::endl;
            self->close(CloseMode::after_pending_writes);
            return;
        }
        self->schedule_idle_check();
    });
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

    // 取消空闲检测；其回调会因 operation_aborted 直接返回。
    idle_timer_.cancel();

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
