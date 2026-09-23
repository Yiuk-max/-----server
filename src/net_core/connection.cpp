#include "connection.h"
#include "client_session.h"

std::atomic<int> g_connection_count{0};

connection::connection(int epoll_fd, int fd)
    : client_fd_(fd), epoll_fd_(epoll_fd) {
    receiver_ = std::make_unique<receiver>(epoll_fd_, fd);
    sender_   = std::make_unique<sender>(epoll_fd_, fd);
    last_active_ = std::chrono::steady_clock::now();
}

connection::~connection() {
    close(CloseMode::immediate);
}

void connection::attach_session(std::shared_ptr<client_session> session) {
    session_ = std::move(session);
}

void connection::set_close_callback(std::function<void(int)> callback) {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    close_callback_ = std::move(callback);
}

void connection::finish_close(int fd) {
    if (session_) {
        session_->on_disconnected();
    }

    std::function<void(int)> callback;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        callback = close_callback_;
    }
    if (callback) {
        callback(fd);
    }
}

void connection::close(CloseMode mode) {
    int fd_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        if (closed_) {
            return;
        }

        if (mode == CloseMode::after_pending_writes && !sender_->empty()) {
            closing_after_write_ = true;
            return;
        }

        closed_ = true;
        closing_after_write_ = false;
        fd_to_close = client_fd_.exchange(-1);
    }

    if (fd_to_close >= 0) {
        ::shutdown(fd_to_close, SHUT_RDWR);
        ::close(fd_to_close);
        g_connection_count.fetch_sub(1);   // 真正关闭 fd 后释放连接名额（对应 accept 时的 fetch_add）
        finish_close(fd_to_close);
    }
}

void connection::handle_write() {
    bool empty = sender_->send_msg();
    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        should_close = closing_after_write_ && empty && !closed_;
    }
    if (should_close) {
        close(CloseMode::immediate);
    }
}

void connection::append_raw_data(const std::string& data) {
    std::lock_guard<std::mutex> lock(recv_mtx_);
    receiver_->append_data(data);
    update_active();
}

void connection::update_active() {
    std::lock_guard<std::mutex> lock(active_mtx_);
    last_active_ = std::chrono::steady_clock::now();
}

long long connection::idle_seconds() const {
    std::lock_guard<std::mutex> lock(active_mtx_);
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - last_active_).count();
}

bool connection::is_idle(int timeout_s) const {
    return idle_seconds() >= timeout_s;
}

void connection::process_incoming() {
    std::lock_guard<std::mutex> lock(recv_mtx_);
    while (true) {
        Standard_Message recv_result = next_frame();
        if (!recv_result.is_valid) {
            break;
        }
        if (session_) {
            session_->on_message(recv_result.payload, recv_result.file_part);
        }
    }
}

Standard_Message connection::next_frame() {
    return receiver_->process_recv_data("");
}

void connection::send_packet(const chat_proto::Envelope& message, std::string file_data) {
    std::string payload;
    if (!message.SerializeToString(&payload)) {
        return;
    }
    send_framed(payload, file_data);
}

void connection::send_serialized(const std::string& payload) {
    send_framed(payload, {});
}

void connection::send_framed(const std::string& payload, const std::string& file_data) {
    const std::size_t frame_size = 8 + payload.size() + file_data.size();
    if (frame_size > UINT32_MAX) {
        return;
    }

    uint32_t total_len    = htonl(static_cast<uint32_t>(frame_size));
    uint32_t payload_len  = htonl(static_cast<uint32_t>(payload.size()));

    std::string packet;
    packet.reserve(frame_size);
    packet.append(reinterpret_cast<const char*>(&total_len), sizeof(total_len));
    packet.append(reinterpret_cast<const char*>(&payload_len), sizeof(payload_len));
    packet += payload;
    packet += file_data;

    // 与 close() 共用生命周期锁，保证"检查连接状态 + 入发送队列"不可被关闭穿插。
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (closed_ || closing_after_write_) {
        return;
    }
    sender_->add_to_out_buffer(packet);
}

void connection::send_file(std::string file_name) {
    sender_->send_file(file_name);
}

void connection::accept_file_chunk(const chat_proto::FileChunkMeta& meta, std::string file_data) {
    receiver_->upload_file(meta, file_data);
}
