#include "handle_msg.h"
#include "total.h"
#include "user.h"
#include "group.h"

void client_thread(const std::shared_ptr<handle_msg>&manager){
    manager->login();
    manager->show_chatlist();
    manager->handle();
}

bool running=true;
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
    //
    //std::thread(close_server,std::ref(server_fd)).detach();
    //accept a connection
    while(running){
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_len = sizeof(client_addr);//accept a connection
        int client_fd = accept(server_fd,(struct sockaddr *)&client_addr,&client_addr_len);
        std::cout << "Accepted a connection" << std::endl;        
        if(client_fd == -1){
            std::cerr << "Failed to accept connection" << std::endl;
            close(server_fd);
            return 1;
        }else {
            // Handle the connection in a separate thread

            auto manager=std::make_shared<handle_msg>(client_fd);
            std::thread t(client_thread,manager);
            t.detach();
        }

    }
    // Close the socket

    close(server_fd);

}