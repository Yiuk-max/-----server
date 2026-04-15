#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

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
    //accept a connection
    for(;;){
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd,(struct sockaddr *)&client_addr,&client_addr_len);
        if(client_fd == -1){
            std::cerr << "Failed to accept connection" << std::endl;
            close(server_fd);
            return 1;
        }
        std::cout << "Accepted a connection" << std::endl;

        // Handle the connection
        std::vector<char> buffer(1024);
        while (true)
        {
            ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
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
                write(client_fd, "SERVER POWEROFF\n", 17);
                break;
            }
            write(client_fd, message.data(), message.size());
        }
        close(client_fd);
    }
    // Close the socket

    close(server_fd);

}