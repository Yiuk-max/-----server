#include "handle_msg.h"
#include "total.h"
#include "user.h"
#include "group.h"
#include "thread_pool.h"
#include <cerrno>

bool running = true;

void accept_connections_thread(int server_fd){
    while (running){
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);//accept a connection
    int client_fd = accept(server_fd,(struct sockaddr *)&client_addr,&client_addr_len);
    std::cout << "Accepted a connection" << std::endl;        
        if(client_fd == -1){
            std::cerr << "Failed to accept connection" << std::endl;
            close(server_fd);
            continue;
        }else {
            // Handle the connection in a separate thread

            auto manager=std::make_shared<handle_msg>(client_fd);
            handle_msg_list[std::to_string(client_fd)] = manager;
            manager->show_chatlist();
            std::thread th([manager]() { manager->login(); });// 先登录，登录成功后才处理消息

            th.detach();
        }
    }
}
void recv_msg_thread(ThreadPool& pool){
    std::vector<char>buffer(1024);
     while (running){
        std::vector<int> disconnected_clients;
        {
            std::lock_guard<std::mutex> lock(client_mutex);
            for(int client_fd : activate_clients){
                ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), MSG_DONTWAIT); // 非阻塞读取
                if (read_bytes > 0) {
                    std::string message(buffer.data(), static_cast<std::size_t>(read_bytes));
                    pool.add_task([client_fd, message](){
                        auto it = handle_msg_list.find(std::to_string(client_fd));
                        if (it != handle_msg_list.end()) {
                            it->second->handle(message);
                        }
                    });
                }else if(read_bytes == 0){
                    std::cout << "Client disconnected" << std::endl;
                    disconnected_clients.push_back(client_fd);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Failed to read from socket, client_fd=" << client_fd << std::endl;
                    disconnected_clients.push_back(client_fd);
                }
            }
        }

        for (int client_fd : disconnected_clients) {
            auto it = handle_msg_list.find(std::to_string(client_fd));
            if(it != handle_msg_list.end()){
                it->second->exit_self();
                handle_msg_list.erase(it);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
int main(){
    // Create a socket
    int server_fd = socket(AF_INET6,SOCK_STREAM,0);
    if(server_fd == -1){
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }
    //bind the socket to an address and port
    struct sockaddr_in6 server_addr{};
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;    //listen on all interfaces
    server_addr.sin6_port = htons(8080);
    int opt = 1;
    if(setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt)) == -1){
        std::cerr << "Failed to set socket options" << std::endl;
        close(server_fd);
        return 1;
    }
    if(bind(server_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) == -1){
        std::cerr << "Failed to bind socket" << std::endl;
        close(server_fd);
        return 1;
    }
    // Listen for incoming connections
    if(listen(server_fd,128) == -1){
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_fd);
        return 1;
    }
    std::cout << "Server is listening on port 8080..." << std::endl;
    
    ThreadPool pool;
    std::thread accept_thread(accept_connections_thread, server_fd);
    std::thread recv_thread(recv_msg_thread, std::ref(pool));
    
    // 保持服务器运行
    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    pool.stop_pool();
    
    // 等待后台线程结束
    if (accept_thread.joinable()) accept_thread.join();
    if (recv_thread.joinable()) recv_thread.join();
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}