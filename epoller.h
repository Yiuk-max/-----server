#pragma once
#include "total.h"
#include "Text_msg_handler.h"
#include "thread_pool.h"
extern bool running;

class epoller{//epoll基类，实现main和sub reactor
public:
    epoller(){
        epoller_fd_ = epoll_create(1);
        if (epoller_fd_ == -1) { 
            std::cerr << "Failed to create epoll instance" << std::endl;
        }
    }
    virtual void loop() = 0;
    virtual ~epoller();
protected:
    int epoller_fd_;
};


class main_reactor : public epoller{
public:
    main_reactor(
        int fd,
        std::shared_ptr<sub_reactor> sub1,
        std::shared_ptr<sub_reactor> sub2,
        std::shared_ptr<sub_reactor> sub3
    ) : server_fd_(fd),
    sub1_(std::move(sub1)),
    sub2_(std::move(sub2)),
    sub3_(std::move(sub3)) {}
    
    void loop()override;
    void add_connect();
private:
    int select_num = 0;
    int server_fd_;
    std::shared_ptr<sub_reactor> sub1_;
    std::shared_ptr<sub_reactor> sub2_;
    std::shared_ptr<sub_reactor> sub3_;
};
class sub_reactor : public epoller{
public:
    sub_reactor(std::shared_ptr<ThreadPool> pool):pool_(std::move(pool)){
        events = std::vector<struct epoll_event>(1024);
    }
    void add_connect(int new_client_fd);
    void loop() override;
    std::string read_data(bool &disconnected,int &fd);
    void pool_add_task(std::string received_data,int fd);
    void remove_client(int fd);
private:
    std::weak_ptr<ThreadPool> pool_;
    std::vector<struct epoll_event> events;
    //解析有问题，误认为函数，用=解决
};


void epoll_event_loop(int server_fd, ThreadPool& pool);//旧单reactor模式，代码复用...
