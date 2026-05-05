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
    json msg_json;
    try{
        msg_json = json::parse(message);
    }catch(const std::exception& e){
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
    if(!msg_json.contains("type")){
        std::string fail = "Invalid message format: missing 'type' field.\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    std::string type = msg_json["type"];
    

    if(type == "spk"){
        std::string name = msg_json["target_name"].get<std::string>();
        spk_to(name,msg_json["content"].get<std::string>());
    }else if(type == "show"){
        show_chatlist();
    }
    else if(type == "create_group"){
        create_group(msg_json["target_name"].get<std::string>());
    }else if(type == "group_add_client"){
        group_add_client(msg_json["group_name"].get<std::string>(),msg_json["target_name"].get<std::string>());
    }else if(type == "group_delete_client"){
        group_delete_client(msg_json["group_name"].get<std::string>(),msg_json["target_name"].get<std::string>());
    }else if(type == "exit"){
        exit_self();
    }else if(type == "delete_group"){
        delete_group(msg_json["target_name"].get<std::string>());

    }else if(type == "login"){
        login(msg_json["username"].get<std::string>(),msg_json["password"].get<std::string>());

    }else{
        std::string fail = "Unknown command type.\n";
        write(client_fd, fail.c_str(), fail.size());
    }

}
void handle_msg::spk_to(std::string name,std::string message){

    if (name.empty() || message.empty()) {
        std::string fail = "Invalid format. Use /spk_to:group_or_user:content\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }

    auto it = group_list.find(name);
    if (it != group_list.end()) {
        spk_group(it->second,message);
        return;
    }
    for(auto &pair:username_to_fd){
        if(pair.first == name){
            int target_fd=username_to_fd[name];
            spk_personally(target_fd,message);
            break;
        }
    }
}
//===============
void handle_msg::login(std::string username,std::string password){

    user new_user(client_fd, username, password);
    
    if(new_user.passwd_check()){
        std::lock_guard<std::mutex>lock(client_mutex);
        users[client_fd] = new_user;
        username_to_fd[username]=client_fd;
       
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
    if(target_fd != -1){
        write(target_fd,msg.c_str(),msg.length());
    }

}
void handle_msg::create_group(std::string group_name){
    

    std::string tishi2="Group name already exists, please choose another one.\n";
    std::unique_lock<std::mutex>lock (client_mutex);

    if (group_name.empty()) {
        std::string fail = "Group name cannot be empty.\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    for(auto &pair:group_list){
        if(pair.first == group_name){
            write(client_fd,tishi2.c_str(),tishi2.length());
            return;
        }
    }
    for(auto &pair:username_to_fd){
        if(pair.first == group_name){
            write(client_fd,tishi2.c_str(),tishi2.length());
            return;
        }
    }
    
    //group new_group (client_fd);
    group_list[group_name]=std::make_shared<group>(client_fd);
    lock.unlock();
    //  修复：创建成功给客户端提示
    std::string success = "create group [" + group_name + "] success!\n";
    write(client_fd, success.c_str(), success.size());
    return;
}
void handle_msg::group_add_client(std::string group_name,std::string username){

    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_add_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    auto it = group_list.find(group_name);

    if (it != group_list.end()) {
        if(!it->second->is_manager_fd(client_fd)){
            std::string fail = "You are not the manager of group [" + group_name + "].\n";
            write(client_fd, fail.c_str(), fail.size());
            return;
        }
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
void handle_msg::group_delete_client(std::string group_name,std::string username){
    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    auto it = group_list.find(group_name);
    if (it != group_list.end()) {
        if(!it->second->is_manager_fd(client_fd)){
            std::string fail = "You are not the manager of group [" + group_name + "].\n";
            write(client_fd, fail.c_str(), fail.size());
            return;
        }
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
void handle_msg::delete_group(std::string group_name){
    if (group_name.empty()) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = group_list.find(group_name);
    if (it == group_list.end()) {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    if(!it->second->is_manager_fd(client_fd)){
        std::string fail = "You are not the manager of group [" + group_name + "].\n";
        write(client_fd, fail.c_str(), fail.size());
        return;
    }
    it->second->group_spk("Group [" + group_name + "] is being deleted by the manager.\n");
    group_list.erase(it);

    std::string success = "Group [" + group_name + "] deleted successfully.\n";
    write(client_fd, success.c_str(), success.size());

}
handle_msg::~handle_msg(){
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