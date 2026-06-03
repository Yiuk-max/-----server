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
RecvMessage Text_msg_recver::process_recv_data(std::string raw_message)
{
    in_buffer += raw_message;
    RecvMessage result;
    
    // 需要包头：4字节包头总长度 + 4字节JSON长度
    while (in_buffer.size() >= 8)
    {
        // 解析1：包头总长度
        uint32_t net_total_len;
        std::memcpy(&net_total_len, in_buffer.c_str(), sizeof(net_total_len));
        uint32_t total_len = ntohl(net_total_len);
        
        if (in_buffer.size() < 4 + total_len)
        {
            // 包体未完全接收，等待更多数据
            break;
        }
        
        // 解析2：JSON长度
        uint32_t net_json_len;
        std::memcpy(&net_json_len, in_buffer.c_str() + 4, sizeof(net_json_len));
        uint32_t json_len = ntohl(net_json_len);
        
        // 验证长度合法性
        if (json_len > total_len - 4)
        {
            // 数据错误，跳过这个包
            in_buffer.erase(0, 4 + total_len);
            break;
        }
        
        // 提取JSON部分
        result.json_part = in_buffer.substr(8, json_len);
        
        // 提取文件部分 (如果有)
        uint32_t file_len = total_len - 4 - json_len;
        if (file_len > 0)
        {
            result.file_part = in_buffer.substr(8 + json_len, file_len);
        }
        
        result.is_valid = true;
        
        // 移除已处理的部分
        in_buffer.erase(0, 4 + total_len);
        
        return result;
    }
    
    return result; // is_valid = false
}