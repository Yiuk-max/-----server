#pragma once
#include "total.h"
class sender
{
protected:
    int client_fd_;
    int epoll_fd_;
    std::string out_buffer;      // 发送缓冲区
    std::mutex out_mtx;          // 保护缓冲区的锁
public:
    sender()=delete;
    sender(int epoll_fd,int new_client):epoll_fd_(epoll_fd),client_fd_(new_client){}
    virtual void send_msg() =0;
    virtual ~sender();
};
class Text_msg_sender : public sender
{
private:

public:
    Text_msg_sender(int epoll_fd,int fd):sender(epoll_fd,fd){}
    void add_to_out_buffer(const std::string& message);
    void send_msg()override;
};

class recver
{
protected:
    int client_fd_;
    int epoll_fd_;
    std::string in_buffer;       // 未处理原始数据
public:
    recver()=delete;
    recver(int epoll_fd,int new_client):epoll_fd_(epoll_fd),client_fd_(new_client){}
    virtual ~recver();
};

class Text_msg_recver : public recver
{
private:

public:
    Text_msg_recver(int epoll_fd,int fd):recver(epoll_fd,fd){}
    std::string process_recv_data(std::string raw_message);
    void recv_msg(int fd);
};

//Text_msg_sender
//Text_msg_recever

