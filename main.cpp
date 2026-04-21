#include "total.h"
#include "user.h"
#include "handle_msg.h"
void client_thread(const std::shared_ptr<handle_msg>&manager){
    manager->login();
    manager->handle();
}
/*
void accept_connect(int client_fd, int server_fd){
    std::vector<char> buffer(1024);
    while (true)
    {
        ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);// Read data from the client
        if (read_bytes == -1) {
            std::cerr << "Failed to read from socket" << std::endl;
            break;
        } else if (read_bytes == 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }
        std::string message(buffer.data(), static_cast<std::size_t>(read_bytes));
        if (message.rfind("exit", 0) == 0) {
            std::cout << "Client requested to close the connection" << std::endl;
            std::cout << "Server is shutting down..." << std::endl;
            running = false;
            // Notify all active clients about server shutdown
            std::lock_guard<std::mutex> lock(client_mutex);
            for(int fd : activate_clients){
                write(fd, "SERVER POWEROFF\n", 17);
                close(fd);
            }
            auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
            if(it != activate_clients.end()){
                activate_clients.erase(it);
            }
            close(client_fd);
            exit(0);
            break;
        }else if(message.rfind("spk:", 0) == 0){
            std::string response = "SPK: " + message.substr(4) + "\n";
            std::lock_guard<std::mutex> lock(client_mutex);
            for(int fd : activate_clients){
                write(fd, response.data(), response.size());
            }
            write(client_fd, "send successfully", 17);
        
        }

        write(client_fd, message.data(), message.size());
    }
    close(client_fd);

}*/
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