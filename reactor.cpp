#include "reactor.h"

void epoll_event_loop(int server_fd, ThreadPool& pool) {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { std::cerr << "Failed to create epoll instance" << std::endl; return; }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    std::vector<struct epoll_event> events(1024);

    while (running) {
        int num_events = epoll_wait(epoll_fd, events.data(), events.size(), -1);
        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                struct sockaddr_in6 client_addr;
                socklen_t len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
                if (client_fd != -1) {
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);
                    event.events = EPOLLIN | EPOLLET; 
                    event.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
                    
                    auto manager = std::make_shared<handle_msg>(client_fd);
                    {
                        std::lock_guard<std::mutex> lock(client_mutex);
                        handle_msg_list[std::to_string(client_fd)] = manager;
                    }
                    manager->show_chatlist();
                }
            } else {
                std::shared_ptr<handle_msg> manager;
                {
                    std::lock_guard<std::mutex> lock(client_mutex);
                    auto it = handle_msg_list.find(std::to_string(fd));
                    if (it != handle_msg_list.end()) manager = it->second;
                }
                if (!manager) continue; // 连接可能已经被关闭了
                // 处理EPOLLOUT事件
                if (events[i].events & EPOLLOUT) {
                    manager->on_write();
                    continue; // 处理完写事件后继续下一轮循环
                }
                if(!(events[i].events & EPOLLIN)) continue; // 不是读事件，继续下一轮循环
                // 处理read事件
                std::string received_data;
                char buf[1024];
                bool disconnected = false;

                // 【关键修改1】EPOLLET模式下，必须循环读取直到返回EAGAIN
                while (true) {
                    ssize_t read_bytes = recv(fd, buf, sizeof(buf), 0);
                    if (read_bytes > 0) {
                        received_data.append(buf, read_bytes);
                    } else if (read_bytes == 0) {
                        disconnected = true;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 数据读完了
                        } else {
                            disconnected = true; // 发生错误
                            break;
                        }
                    }
                }

                if (disconnected) {
                    // 【关键修改2】清理资源：从epoll移除并从全局map删除
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    {
                        std::lock_guard<std::mutex> lock(client_mutex);
                        auto it = handle_msg_list.find(std::to_string(fd));
                        if (it != handle_msg_list.end()) {
                            handle_msg_list.erase(it);
                        }
                        
                        
                    }
                    // 注意：handle_msg的析构函数里已经写了close(fd)和清理users的代码
                } else if (!received_data.empty()) {
                    pool.add_task([fd, received_data]() {
                        std::shared_ptr<handle_msg> manager;
                        {
                            std::lock_guard<std::mutex> lock(client_mutex);
                            auto it = handle_msg_list.find(std::to_string(fd));
                            if (it != handle_msg_list.end()) manager = it->second;
                        }
                        if (manager) {
                            manager->process_input(received_data);
                            // 这里不直接调用handle，而是调用process_input，让handle_msg自己解析消息
                            // manager->handle(received_data);
                        }
                    });
                }
            }
        }
    }
    close(epoll_fd);
}