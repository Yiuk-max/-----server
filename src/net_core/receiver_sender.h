#pragma once
#include <string>
#include <mutex>

// 接收消息解析结果结构体
struct Standard_Message {
    std::string payload;    // protobuf 字节
    std::string file_part;
    bool is_valid = false;
};

class sender
{
private:
    int client_fd_;
    int epoll_fd_;
    std::string out_buffer;      // 发送缓冲区
    std::mutex out_mtx;          // 保护缓冲区的锁
public:
    sender(int epoll_fd,int fd):epoll_fd_(epoll_fd),client_fd_(fd){}
    void add_to_out_buffer(const std::string& message);
    bool send_msg();                    // 返回发送缓冲是否已清空
    bool empty();
};


class receiver
{
private:
    int client_fd_;
    int epoll_fd_;
    std::string in_buffer;       // 接收缓冲区，存储未处理的原始数据
    std::mutex in_mtx;           // 保护缓冲区的锁
public:
    receiver(int epoll_fd,int fd):epoll_fd_(epoll_fd),client_fd_(fd){}
    Standard_Message process_recv_data(std::string raw_message);// 处理原始数据，返回解析结果
    void append_data (const std::string& data); // 将新接收的数据追加到缓冲区
};


