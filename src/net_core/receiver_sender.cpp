#include "receiver_sender.h"
#include "epoller.h"
#include <cstring>
#include <arpa/inet.h>
void sender::add_to_out_buffer(const std::string &message)
{
    std::lock_guard<std::mutex> lock(out_mtx);
    bool was_empty = out_buffer.empty();
    out_buffer += message;
    // 仅当缓冲区由空变为非空时注册写事件，减少 epoll_ctl 调用
    if (was_empty)
    {
        struct epoll_event event;
        event.data.fd = client_fd_;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET; // 同时监听读和写事件
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd_, &event);
    }
}
bool sender::send_msg()
{
    std::lock_guard<std::mutex> lock(out_mtx);
    while (!out_buffer.empty())
    {
        ssize_t bytes_sent = send(client_fd_, out_buffer.c_str(), out_buffer.size(), MSG_NOSIGNAL);
        if (bytes_sent > 0)
        {
            out_buffer.erase(0, bytes_sent); // 移除已发送的部分
        }
        else if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // Socket缓冲区满了，稍后再试
            break;
        }
        else
        {
            // 连接错误由 reactor 的后续事件/关闭流程统一清理。
            break;
        }
    }
    if (out_buffer.empty())
    {
        // 发送完毕，取消写事件的注册
        struct epoll_event event{};
        event.data.fd = client_fd_;
        event.events = EPOLLIN | EPOLLET; // 只保留读事件
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd_, &event);
    }
    return out_buffer.empty();
}

bool sender::empty()
{
    std::lock_guard<std::mutex> lock(out_mtx);
    return out_buffer.empty();
}
Standard_Message receiver::process_recv_data(std::string raw_message)
{
    Standard_Message result;
    constexpr uint32_t MAX_FRAME_SIZE = 16 * 1024 * 1024;

    // total_len 表示整个帧长度：8 字节包头 + JSON + 可选文件数据。
    while (in_buffer.size() >= 8)
    {
        uint32_t net_total_len;
        std::memcpy(&net_total_len, in_buffer.data(), sizeof(net_total_len));
        uint32_t total_len = ntohl(net_total_len);

        // 非法长度无法可靠寻找下一帧边界，清空缓冲并等待连接关闭/新数据。
        if (total_len < 8 || total_len > MAX_FRAME_SIZE)
        {
            in_buffer.clear();
            return result;
        }
        if (in_buffer.size() < total_len)
        {
            return result; // 半包，等待更多数据
        }

        uint32_t net_payload_len;
        std::memcpy(&net_payload_len, in_buffer.data() + 4, sizeof(net_payload_len));
        uint32_t payload_len = ntohl(net_payload_len);
        if (payload_len > total_len - 8)
        {
            in_buffer.erase(0, total_len);
            return result;
        }

        result.payload = in_buffer.substr(8, payload_len);
        uint32_t file_len = total_len - 8 - payload_len;
        if (file_len > 0)
        {
            result.file_part = in_buffer.substr(8 + payload_len, file_len);
        }

        result.is_valid = true;
        in_buffer.erase(0, total_len);
        return result;
    }

    return result;
}
void receiver::append_data(const std::string &data)
{
    std::lock_guard<std::mutex> lock(in_mtx);
    in_buffer += data;
}