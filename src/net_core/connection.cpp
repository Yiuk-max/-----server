#include "connection.h"
#include "client_session.h"

connection::connection(int epoll_fd, int fd)
    : client_fd_(fd), epoll_fd_(epoll_fd) {
    receiver_ = std::make_unique<receiver>(epoll_fd_, fd);
    sender_   = std::make_unique<sender>(epoll_fd_, fd);
    last_active_ = std::chrono::steady_clock::now();
}

connection::~connection() {
    close();
}

// ---------- 生命周期 ----------
void connection::attach_session(std::shared_ptr<client_session> session) {
    session_ = std::move(session);
}

void connection::close() {
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
}

// ---------- 供 epoller（sub_reactor）调用 ----------
void connection::handle_write() {
    sender_->send_msg();
}

void connection::append_raw_data(const std::string& data) {
    std::lock_guard<std::mutex> lock(recv_mtx_);
    receiver_->append_data(data);
    // 收到任何数据都视为一次有效活动，刷新心跳时间
    update_active();
}

// ---------- 心跳 / 活动检测 ----------
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
    // 循环切出所有完整帧（支持粘包/半包），逐帧交给业务会话分发。
    // 加锁保护接收缓冲的切换，避免与 append_raw_data（IO 线程）竞争。
    std::lock_guard<std::mutex> lock(recv_mtx_);
    while (true) {
        Standard_Message recv_result = next_frame();
        if (!recv_result.is_valid) {
            break;  // 没有更多完整消息
        }
        if (session_) {
            session_->on_message(recv_result.json_part, recv_result.file_part);
        }
    }
}

// 私有：从接收缓冲切出一个完整帧（调用方需已持 recv_mtx_）
Standard_Message connection::next_frame() {
    return receiver_->process_recv_data("");
}

// ---------- 供 client_session / 业务层调用 ----------
void connection::send_json_packet(const json& msg_json) {
    // 统一协议: |4字节总长度|4字节JSON长度|JSON|file|
    std::string json_str = msg_json.dump();

    uint32_t json_len  = htonl(json_str.size());
    uint32_t total_len = htonl(8 + json_str.size()); // 包头长度 + JSON长度 + file长度（0）

    std::string packet;
    packet.reserve(8 + json_str.size());
    packet.append(reinterpret_cast<const char*>(&total_len), 4);
    packet.append(reinterpret_cast<const char*>(&json_len), 4);
    packet += json_str;

    sender_->add_to_out_buffer(packet);
}

void connection::package_message(const std::string& message, const std::string& type) {
    json msg_json;
    msg_json["type"]    = type;
    msg_json["content"] = message;
    send_json_packet(msg_json);
}

void connection::package_chat_message(const std::string& message, const std::string& type, int message_id) {
    json msg_json;
    msg_json["type"]        = type;
    msg_json["content"]     = message;
    msg_json["message_id"]  = message_id;
    send_json_packet(msg_json);
}

void connection::send_file(const std::string& file_name) {
    sender_->send_file(file_name);
}

void connection::upload_file(const json& meta, const std::string& data) {
    receiver_->upload_file(meta, data);
}

// ---------- 访问器 ----------
sender& connection::sender_obj() {
    return *sender_;
}

receiver& connection::receiver_obj() {
    return *receiver_;
}

