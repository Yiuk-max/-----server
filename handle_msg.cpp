#include "handle_msg.h"
    static std::mutex client_mutex;
    static std::vector<int> activate_clients;
    static std::unordered_map<std::string, int> username_to_fd;
    static std::unordered_map<int, user> users;
    extern bool running;
void handle_msg::close_server(){
    std::cout << "Client requested to close the connection" << std::endl;
    std::cout << "Server is shutting down..." << std::endl;
    running = false;
    // Notify all active clients about server shutdown
    std::lock_guard<std::mutex> lock(client_mutex);
    for(int fd : activate_clients){
        
        write(fd, "SERVER POWEROFF\n", 17);

        close(fd);
    }
    exit(0);
}
void handle_msg::show_userlist(){
    std::lock_guard<std::mutex>lock (client_mutex);
    std::string name_list="";
    for(auto it=username_to_fd.begin();it!=username_to_fd.end();++it){
        name_list+=it->first+"\n";
        
    }
    write(client_fd,name_list.c_str(),name_list.size());
}
void handle_msg::handle(){
    std::vector<char> buffer(1024);
    while(true){
        ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);// Read data from the client
        if (read_bytes == -1) {
            std::cerr << "Failed to read from socket" << std::endl;
            break;
        } else if (read_bytes == 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }
        std::string message(buffer.data(), static_cast<std::size_t>(read_bytes));
        if(message.rfind("spk_public:", 0) == 0){
            spk_public(message);
        }else if(message.rfind("spk_private:", 0) == 0){
            spk_private(message);
        }else if(message.rfind("show", 0) == 0){
            show_userlist();
        }else if(message.rfind("exit", 0) == 0){
            close_server();
        }

    }
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
    if (it != activate_clients.end()) {
        activate_clients.erase(it);
    }
    users.erase(client_fd);
    close(client_fd);
}
void handle_msg::login(){
   
    write(client_fd, "Please enter your name : ", 35);
   
    std::vector<char> buffer(1024);
    ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (read_bytes == -1) {
        std::cerr << "Failed to read from socket" << std::endl;
        return;
    } else if (read_bytes == 0) {
        std::cout << "Client disconnected" << std::endl;
        return;
    }
    std::string name(buffer.data(), static_cast<std::size_t>(read_bytes));

    write(client_fd, "Please enter your password : ", 35);

    read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (read_bytes == -1) {
        std::cerr << "Failed to read from socket" << std::endl;
        return;
    } else if (read_bytes == 0) {
        std::cout << "Client disconnected" << std::endl;
        return;
    }
    std::string password(buffer.data(), static_cast<std::size_t>(read_bytes));

    user new_user(client_fd, name, password);
    
    if(new_user.passwd_check()){
        std::lock_guard<std::mutex>lock(client_mutex);
        users[client_fd] = new_user;
        username_to_fd[name]=client_fd;
       
        activate_clients.push_back(client_fd);
        write(client_fd, "Login successful\n", 17);
    }else {
        write(client_fd, "Login failed\n", 14);
        close(client_fd);
    }

}
void handle_msg::spk_public(std::string message){
    std::vector<char> buffer(1024);
        std::string msg="[public] "+users[client_fd].getName()+":"+message.substr(11);
        
        std::lock_guard<std::mutex> lock(client_mutex);
        for(int fd : activate_clients){

            write(fd, msg.data(), msg.size());
        }
        write(client_fd, "send successfully", 17);
    
}
void handle_msg::spk_private(std::string message){
    std::vector<char> buffer(1024);
    size_t first_colon =message.find(":");
    size_t second_colon =message.find(":",first_colon+1);
    std::string target_id=message.substr(first_colon+1,second_colon-first_colon-1);
    std::string msg = message.substr(second_colon+1);
    int target_client=-1;
    std::unique_lock<std::mutex> lock (client_mutex);
    auto it=username_to_fd.find(target_id);
    
    if(it!=username_to_fd.end()){
        target_client=it->second;
    }else{
        write(client_fd,"user not found!",15);
        return;
    }
    lock.unlock();
    std::string send_msg="[private message ] from ["+users[client_fd].getName()+"] "+msg;
    
    write(target_client,send_msg.c_str(),send_msg.size());
    write(client_fd,"msg sent",8);
}
