#pragma once
#include "total.h"
#include "session_manager.h"
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
private:
    std::weak_ptr<ThreadPool> pool_;
    std::vector<struct epoll_event> events;
    std::mutex client_mutex;

    // 本 reactor 维护的 fd -> client_session 映射。
    // 注意：session 登录后会在 session_manager 中把 key 从 fd 换成 UID，
    // 因此这里必须用独立的 fd 映射，否则 epoll 事件循环将无法按 fd 找到对应连接。
    std::unordered_map<int, std::shared_ptr<class client_session>> sessions_by_fd;
    std::shared_ptr<class client_session> get_session(int fd);   // 按 fd 查找
    void set_session(int fd, std::shared_ptr<class client_session> session);
    void erase_session(int fd);
};


void epoll_event_loop(int server_fd, ThreadPool& pool);//旧单reactor模式，代码复用...

