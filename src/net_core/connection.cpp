#include "connection.h"

connection::connection(int epoll_fd, int fd)
    : client_fd_(fd), epoll_fd_(epoll_fd) {
    receiver_ = std::make_unique<receiver>(epoll_fd_, fd);
    sender_   = std::make_unique<sender>(epoll_fd_, fd);
}

connection::~connection() {
    close();
}

// ---------- 供 epoller（sub_reactor）调用 ----------
void connection::handle_write() {
    sender_->send_msg();
}

void connection::append_raw_data(const std::string& data) {
    receiver_->append_data(data);
}

Standard_Message connection::next_frame() {
    // process_recv_data 的入参在实现里只读 in_buffer，传空串即可
    return receiver_->process_recv_data("");
}

// ---------- 供 client_session / 业务层调用 ----------
void connection::package_message(const std::string& message, const std::string& type) {
    // 统一协议: |4字节总长度|4字节JSON长度|JSON|file|
    json msg_json;
    msg_json["type"]    = type;
    msg_json["content"] = message;
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

// ---------- 连接生命周期 ----------
void connection::close() {
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
}
