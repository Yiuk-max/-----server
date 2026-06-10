#include "epoller.h"
inline epoller::~epoller()
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

    auto session = std::make_shared<client_session>(new_client_fd, epoller_fd_);
    {
        std::lock_guard<std::mutex> lock(client_mutex);
        session_manager::get_instance().add_session(new_client_fd, session);

    }
    session->show_chatlist();
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
    pool->add_task([pool, fd, received_data]()
                   {
                        auto manager = session_manager::get_instance().find_session(fd);
                        if (manager) {
                            manager->preprocess_recv_data(received_data);
                        } });
}
void sub_reactor::remove_client(int fd)
{
    epoll_ctl(epoller_fd_, EPOLL_CTL_DEL, fd, nullptr);
    {
        auto session = session_manager::get_instance().find_session(fd);
        if (session) {
            session_manager::get_instance().remove_session(fd);
        }
    }
    // 注意：handle_msg的析构函数里已经写了close(fd)和清理users的代码
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
    while (running)
    {
        int num_events = epoll_wait(epoller_fd_, events.data(), events.size(), -1);
        for (int i = 0; i < num_events; ++i)
        {
            int fd = events[i].data.fd;
            std::shared_ptr<client_session> manager;
            {
                std::lock_guard<std::mutex> lock(client_mutex);
                auto it = handle_msg_list.find(std::to_string(fd));
                if (it != handle_msg_list.end())
                    manager = it->second;
            }
            if (!manager)
                continue; // 连接可能已经被关闭了
            // ===================处理EPOLLOUT事件====================
            if (events[i].events & EPOLLOUT)
            {
                manager->send_msg();
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
                // 【关键修改2】清理资源：从epoll移除并从全局map删除
                remove_client(fd);
            }
            else if (!received_data.empty())
            {
                pool_add_task(received_data, fd);
            }
        }
    }
}