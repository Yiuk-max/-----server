#pragma once
#include "total.h"
#include "session_manager.h"
#include "thread_pool.h"
#include "connection.h"
#include "server_config.h"
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

class sub_reactor;
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
    // 心跳检测：每约 1 秒调用一次，断开 idle 超时的连接（由 loop 的定时点触发）
    void check_idle_connections();
private:
    std::weak_ptr<ThreadPool> pool_;
    std::vector<struct epoll_event> events;
    std::mutex client_mutex;

    // 方案 3b 重拆：本 reactor 维护 fd -> connection 映射，事件循环直接面向 connection。
    // connection 内部再绑定自己的 client_session（业务会话），从而把网络收发与业务彻底分离。
    std::unordered_map<int, std::shared_ptr<connection>> connections_by_fd;
    std::shared_ptr<connection> get_connection(int fd);   // 按 fd 查找
    void set_connection(int fd, std::shared_ptr<connection> conn);
    void erase_connection(int fd);
};