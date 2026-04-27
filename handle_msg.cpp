#include "handle_msg.h"
#include "group.h"

namespace {
std::string trim_crlf(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}
}

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
void handle_msg::exit_self(){
    std::lock_guard<std::mutex> lock(client_mutex);
    auto user_it = users.find(client_fd);
    if (user_it != users.end()) {
        username_to_fd.erase(user_it->second.getName());
    }
    auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
    if (it != activate_clients.end()) {
        activate_clients.erase(it);
    }
    users.erase(client_fd);
    close(client_fd);
}
void handle_msg::show_chatlist(){
    std::lock_guard<std::mutex>lock (client_mutex);
    std::string name_list="group\n";
    for(auto &pair : group_list){
        name_list+=pair.first+"\n";
    }
    name_list+="person:\n";
    for(auto it=username_to_fd.begin();it!=username_to_fd.end();++it){
        name_list+=it->first+"\n";
        
    }
    write(client_fd,name_list.c_str(),name_list.size());
}
//控制=====================
void handle_msg::handle(std::string message){
    std::string zhiling1="/spk_to:";
    message = trim_crlf(message);

        if(message.find(zhiling1,0)==0){
            std::string name = message.substr(zhiling1.length());
            spk_to(name,message);
        }else if(message.find("/show",0)==0){
            show_chatlist();
        }
        else if(message.find("/create_group",0)==0){
            create_group(message);
        }else if(message.find("/group_add_client",0)==0){
            group_add_client(message);
        }else if(message.find("/group_delete_client",0)==0){
            group_delete_client(message);
        }else if(message.rfind("/exit", 0) == 0){
            exit_self();
        }else{

        }

}
void handle_msg::spk_to(std::string name,std::string message){
    //第二个：后面的真正信息
    std::size_t first_colon = message.find(':');
    std::size_t second_colon = message.find(':', first_colon + 1);
    if (first_colon == std::string::npos || second_colon == std::string::npos) {
        std::string fail = "Invalid format. Use /spk_to:group_or_user:content\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    name = message.substr(first_colon + 1, second_colon - first_colon - 1);
    std::string actual_message = message.substr(second_colon + 1);
    if (name.empty() || actual_message.empty()) {
        std::string fail = "Invalid format. Use /spk_to:group_or_user:content\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }

    auto it = group_list.find(name);
    if (it != group_list.end()) {
        spk_group(it->second,actual_message);
        return;
    }
    for(auto &pair:username_to_fd){
        if(pair.first == name){
            int target_fd=username_to_fd[name];
            spk_personally(target_fd,actual_message);
            break;
        }
    }
}
//===============
void handle_msg::login(){
   
    std::string name_prompt = "Please enter your name : ";
    write(client_fd, name_prompt.c_str(), name_prompt.size());
   
    std::vector<char> buffer(1024);
    ssize_t read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (read_bytes == -1) {
        std::cerr << "Failed to read from socket" << std::endl;
        auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
        if (it != activate_clients.end()) {
            activate_clients.erase(it);
        }
        users.erase(client_fd);
        return;
    } else if (read_bytes == 0) {
        std::cout << "Client disconnected" << std::endl;
        return;
    }
    std::string name(buffer.data(), static_cast<std::size_t>(read_bytes));
    name = trim_crlf(name);

    std::string pw_prompt = "Please enter your password : ";
    write(client_fd, pw_prompt.c_str(), pw_prompt.size());

    read_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (read_bytes == -1) {
        std::cerr << "Failed to read from socket" << std::endl;
        return;
    } else if (read_bytes == 0) {
        std::cout << "Client disconnected" << std::endl;
        return;
    }
    std::string password(buffer.data(), static_cast<std::size_t>(read_bytes));
    password = trim_crlf(password);

    user new_user(client_fd, name, password);
    
    if(new_user.passwd_check()){
        std::lock_guard<std::mutex>lock(client_mutex);
        users[client_fd] = new_user;
        username_to_fd[name]=client_fd;
       
        activate_clients.push_back(client_fd);
        std::string success = "Login successful\n";
        write(client_fd, success.c_str(), success.size());
        
    }else {
        std::string fail = "Login failed\n";
        write(client_fd, fail.c_str(), fail.size());
        close(client_fd);
    }
}

void handle_msg::spk_group(std::shared_ptr<group> chat_group,std::string message){

    write(client_fd,"[group chat]:",13);



    chat_group->group_spk(message);
        


}

void handle_msg::spk_personally(int target_fd,std::string message){

      
        
        std::string msg="["+users[client_fd].getName()+"]:"+message;
        write(target_fd,msg.c_str(),msg.length());

    
}
void handle_msg::create_group(std::string message){
    

    std::string tishi2="Group name already exists, please choose another one.\n";
    std::unique_lock<std::mutex>lock (client_mutex);
    //第一个：/create_group:后面的真正群名称
    if (message.find("/create_group:", 0) != 0) {
        std::string fail = "Invalid format. Use /create_group:group_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    std::string name = message.substr(std::string("/create_group:").length());
    name = trim_crlf(name);
    if (name.empty()) {
        std::string fail = "Group name cannot be empty.\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    for(auto &pair:group_list){
        if(pair.first == name){
            write(client_fd,tishi2.c_str(),tishi2.length());
            return;
        }
    }
    for(auto &pair:username_to_fd){
        if(pair.first == name){
            write(client_fd,tishi2.c_str(),tishi2.length());
            return;
        }
    }
    
    //group new_group (client_fd);
    group_list[name]=std::make_shared<group>(client_fd);
    //  修复：创建成功给客户端提示
    std::string success = "create group [" + name + "] success!\n";
    write(client_fd, success.c_str(), success.size());
    return;
}
void handle_msg::group_add_client(std::string message){
    //第一个：和第二个：之间的是真正的群名称
    //第二个：后面的是真正的信息（要添加的用户名）
    std::size_t first_colon = message.find(':');
    std::size_t second_colon = message.find(':', first_colon + 1);
    if (first_colon == std::string::npos || second_colon == std::string::npos) {
        std::string fail = "Invalid format. Use /group_add_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    std::string group_name = message.substr(first_colon + 1, second_colon - first_colon - 1);
    std::string username = message.substr(second_colon + 1);
    group_name = trim_crlf(group_name);
    username = trim_crlf(username);
    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_add_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    auto it = group_list.find(group_name);
    if (it != group_list.end()) {
        //找到username对应的fd
        auto user_it = username_to_fd.find(username);
        if(user_it == username_to_fd.end()){
            std::string fail = "User [" + username + "] does not exist.\n";
            write(client_fd, fail.c_str(), fail.size());
            return;
        }
        it->second->add_client(username);
         std::string success = "User [" + username + "] added to group [" + group_name + "] successfully.\n";
        write(client_fd, success.c_str(), success.size());
    } else {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        write(client_fd, fail.c_str(), fail.size());
    }
}
void handle_msg::group_delete_client(std::string message){
    //第一个：和第二个：之间的是真正的群名称
    //第二个：后面的是真正的信息（要删除的用户名）
    std::size_t first_colon = message.find(':');
    std::size_t second_colon = message.find(':', first_colon + 1);
    if (first_colon == std::string::npos || second_colon == std::string::npos) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    std::string group_name = message.substr(first_colon + 1, second_colon - first_colon - 1);
    std::string username = message.substr(second_colon + 1);
    group_name = trim_crlf(group_name);
    username = trim_crlf(username);
    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    auto it = group_list.find(group_name);
    if (it != group_list.end()) {
        if(it->second->delete_client(username)){
            std::string success = "User [" + username + "] removed from group [" + group_name + "] successfully.\n";
            write(client_fd, success.c_str(), success.size());
        }else{
            std::string fail = "User [" + username + "] is not in group [" + group_name + "].\n";
            write(client_fd, fail.c_str(), fail.size());
        }
        
    } else {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        write(client_fd, fail.c_str(), fail.size());
    }
}
handle_msg::~handle_msg(){
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
    if (it != activate_clients.end()) {
        activate_clients.erase(it);
    }
    users.erase(client_fd);
    close(client_fd);
}