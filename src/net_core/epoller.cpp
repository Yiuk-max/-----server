#include "epoller.h"
#include "client_session.h"
#include "server_config.h"
epoller::~epoller()
{
    if (epoller_fd_ >= 0)
    {
        close(epoller_fd_);
    }
}

void main_reactor::add_connect()
{
    struct sockaddr_in6 client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &len);
    if (client_fd != -1)
    {
        int choose = (select_num) % 3 + 1;
        select_num++;
        switch (choose)
        {
        case 1:
            sub1_->add_connect(client_fd);
            break;
        case 2:
            sub2_->add_connect(client_fd);
            break;
        case 3:
            sub3_->add_connect(client_fd);
            break;
        default:
            // 需要错误处理
            break;
        }
    }
}
void sub_reactor::add_connect(int new_client_fd)
{
    struct epoll_event event;
    fcntl(new_client_fd, F_SETFL, O_NONBLOCK);
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = new_client_fd;
    epoll_ctl(epoller_fd_, EPOLL_CTL_ADD, new_client_fd, &event);

    // 方案 3b 重拆：创建独立的 connection（纯网络收发），并为其创建并绑定业务会话。
    auto conn = std::make_shared<connection>(epoller_fd_, new_client_fd);
    auto session = std::make_shared<client_session>();
    session->set_connection(conn);         // 会话用弱引用回指 connection
    conn->attach_session(session);         // connection 持有会话（决定其生命周期）
    {
        std::lock_guard<std::mutex> lock(client_mutex);
        set_connection(new_client_fd, conn);
        session_manager::get_instance().add_session(new_client_fd, session);
    }
}
void main_reactor::loop()
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_fd_;
    epoll_ctl(epoller_fd_, EPOLL_CTL_ADD, server_fd_, &event);
    while (running)
    {
        int new_accept = epoll_wait(epoller_fd_, &event, 1, -1);
        if (new_accept <= 0)
        {
            continue;
        }
        if (event.data.fd == server_fd_)
        {
            add_connect();
        }
    }
}
void sub_reactor::pool_add_task(std::string received_data, int fd)
{
    auto pool = pool_.lock();
    if(!pool) {
        std::cerr << "ThreadPool is no longer available." << std::endl;
        return;
    }
    auto conn = get_connection(fd);
    if (!conn) {
        return; // 连接可能已经关闭
    }
    // 先把原始字节追加进 connection 的接收缓冲，再把切帧+业务处理提交到线程池
    conn->append_raw_data(received_data);
    pool->submit_task([conn]()
                   {
                        conn->process_incoming();
                   });
}
void sub_reactor::remove_client(int fd)
{
    epoll_ctl(epoller_fd_, EPOLL_CTL_DEL, fd, nullptr);
    std::lock_guard<std::mutex> lock(client_mutex);
    erase_connection(fd);
    session_manager::get_instance().remove_session(fd);
}
std::string sub_reactor::read_data(bool &disconnected, int &fd)
{
    std::string received_data;
    char buf[1024];
    while (true)
    {
        ssize_t read_bytes = recv(fd, buf, sizeof(buf), 0);
        if (read_bytes > 0)
        {
            received_data.append(buf, read_bytes);
        }
        else if (read_bytes == 0)
        {
            disconnected = true;
            break;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return received_data; // 数据读完了
            }
            else
            {
                disconnected = true; // 发生错误
                break;
            }
        }
    }
    return "";
}
void sub_reactor::loop()
{
    auto last_check = std::chrono::steady_clock::now();
    while (running)
    {
        // 设 1 秒超时，以便周期性做心跳/空闲检查
        int num_events = epoll_wait(epoller_fd_, events.data(), events.size(), 1000);
        for (int i = 0; i < num_events; ++i)
        {
            int fd = events[i].data.fd;

            auto conn = get_connection(fd);
            if (!conn)
                continue; // 连接可能已经被关闭了
            // ===================处理EPOLLOUT事件====================
            if (events[i].events & EPOLLOUT)
            {
                conn->handle_write();
                continue; // 处理完写事件后继续下一轮循环
            }
            //===============================================
            if (!(events[i].events & EPOLLIN))
                continue; // 不是读事件，继续下一轮循环
            //=======================处理read事件===================
            bool disconnected = false;
            std::string received_data = read_data(disconnected, fd);

            if (disconnected)
            {
                remove_client(fd);
            }
            else if (!received_data.empty())
            {
                pool_add_task(received_data, fd);
            }
        }

        // 心跳检测：约每 1 秒检查一次所有连接，断开空间闲超时的连接
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_check).count() >= 1) {
            last_check = now;
            check_idle_connections();
        }
    }
}

// 心跳/空闲检测：遍历本 reactor 的所有连接，超过配置的超时秒数无活动则断开
void sub_reactor::check_idle_connections()
{
    if (!ServerConfig::get_instance().use_heartbeat()) {
        return; // 未启用心跳则不检测
    }
    int timeout_s = ServerConfig::get_instance().heartbeat_interval();

    // 先收集空间闲连接的 fd（避免在遍历时删除导致迭代器失效），再加锁逐个移除
    std::vector<int> to_remove;
    {
        std::lock_guard<std::mutex> lock(client_mutex);
        for (auto& kv : connections_by_fd) {
            if (kv.second && kv.second->is_idle(timeout_s)) {
                to_remove.push_back(kv.first);
            }
        }
    }
    for (int fd : to_remove) {
        if (running) {
            std::cout << "[Heartbeat] closing idle connection fd=" << fd << std::endl;
            remove_client(fd);
        }
    }
}

std::shared_ptr<connection> sub_reactor::get_connection(int fd)
{
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = connections_by_fd.find(fd);
    if (it != connections_by_fd.end()) {
        return it->second;
    }
    return nullptr;
}
void sub_reactor::set_connection(int fd, std::shared_ptr<connection> conn)
{
    connections_by_fd[fd] = std::move(conn);
}
void sub_reactor::erase_connection(int fd)
{
    connections_by_fd.erase(fd);
}
