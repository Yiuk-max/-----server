#include "Text_msg_handler.h"
#include "total.h"
#include "user.h"
#include "group.h"
#include "thread_pool.h"
#include "epoller.h"
#include <cerrno>


bool running = true;
int epoll_fd;


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
    
    epoll_event_loop(server_fd, pool);
    // 保持服务器运行
    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    pool.stop_pool();
    
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}
