#include "Text_recver_sender.h"
#include "epoller.h"
#include <arpa/inet.h>
inline sender::~sender()
{
}
inline recver::~recver()
{
}
void Text_msg_sender::add_to_out_buffer(const std::string &message)
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
void Text_msg_sender::send_msg()
{
    std::lock_guard<std::mutex> lock(out_mtx);
    while (!out_buffer.empty())
    {
        ssize_t bytes_sent = send(client_fd_, out_buffer.c_str(), out_buffer.size(), 0);
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
            // 发生错误，可能需要关闭连接
            // close_connection();
            break;
        }
    }
    if (out_buffer.empty())
    {
        // 发送完毕，取消写事件的注册
        struct epoll_event event;
        event.data.fd = client_fd_;
        event.events = EPOLLIN | EPOLLET; // 只保留读事件
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd_, &event);
    }
}
std::string Text_msg_recver::process_recv_data(std::string raw_message)
{
    in_buffer += raw_message;
    while (in_buffer.size() >= 4)
    {
        // 解析包头，获取消息长度
        uint32_t net_len;
        std::memcpy(&net_len, in_buffer.c_str(), sizeof(net_len));
        uint32_t msg_length = ntohl(net_len);
        if (in_buffer.size() < 4 + msg_length)
        {
            // 包体未完全接收，等待更多数据
            break;
        }
        std::string message = in_buffer.substr(4, msg_length); // 提取消息
        in_buffer.erase(0, 4 + msg_length);                    // 移除已处理的部分
        if (!message.empty())
        {
            return message; // 返回处理后消息
        }
    }
    return "";
}