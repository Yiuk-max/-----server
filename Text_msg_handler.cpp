#include "Text_msg_handler.h"
#include "group.h" 


extern bool running;

void Text_msg_handler::exit_self(){
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
void Text_msg_handler::show_chatlist(){
    std::lock_guard<std::mutex>lock (client_mutex);
    std::string name_list="group\n";
    for(auto &pair : group_list){
        name_list+=pair.first+"\n";
    }
    name_list+="person:\n";
    for(auto it=username_to_fd.begin();it!=username_to_fd.end();++it){
        name_list+=it->first+"\n";
        
    }
    //write(client_fd,name_list.c_str(),name_list.size());
    package_message(name_list,"system");
}
//控制=====================
void Text_msg_handler::handle(std::string message){
    json msg_json;
    try{
        msg_json = json::parse(message);
    }catch(const std::exception& e){
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
    if(!msg_json.contains("type")){
        std::string fail = "Invalid message format: missing 'type' field.\n";
        //write(client_fd, fail.c_str(), fail.size());
        package_message(fail,"system");
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

    }else if(type == "modify_group_name"){
        modify_group_name(msg_json["target_name"].get<std::string>(),msg_json["content"].get<std::string>());
    }else if(type == "login"){
        login(msg_json["username"].get<std::string>(),msg_json["password"].get<std::string>());

    }else{
        std::string fail = "Unknown command type.\n";
        package_message(fail,"system");
    }

}
void Text_msg_handler::spk_to(std::string name,std::string message){

    if (name.empty() || message.empty()) {
        std::string fail = "Invalid format. Use /spk_to:group_or_user:content\n";
        package_message(fail,"system");
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
void Text_msg_handler::login(std::string username,std::string password){

    user new_user(client_fd, username, password);
    
    if(new_user.passwd_check()){
        std::lock_guard<std::mutex>lock(client_mutex);
        users[client_fd] = new_user;
        username_to_fd[username]=client_fd;
       
        activate_clients.push_back(client_fd);
        std::string success = "Login successful\n";
        package_message(success,"system");
    }else {
        std::string fail = "Login failed\n";
        package_message(fail,"system");
        close(client_fd);
    }
}

void Text_msg_handler::spk_group(std::shared_ptr<group> chat_group,std::string message){
    //package_message("[group chat]:"+message);
    chat_group->group_spk(message);
}

void Text_msg_handler::spk_personally(int target_fd,std::string message){    
    std::string msg="["+users[client_fd].getName()+"]:"+message;
    if(target_fd != -1){
        package_message(msg,users[client_fd].getName());
    }

}
//============================群聊
void Text_msg_handler::create_group(std::string group_name){
    

    std::string tishi2="Group name already exists, please choose another one.\n";
    std::unique_lock<std::mutex>lock (client_mutex);

    if (group_name.empty()) {
        std::string fail = "Group name cannot be empty.\n";
        package_message(fail,"system");
        return;
    }
    for(auto &pair:group_list){
        if(pair.first == group_name){
            package_message(tishi2,"system");
            return;
        }
    }
    for(auto &pair:username_to_fd){
        if(pair.first == group_name){
            package_message(tishi2,"system");
            return;
        }
    }
    
    //group new_group (client_fd);
    group_list[group_name]=std::make_shared<group>(client_fd,group_name);
    lock.unlock();
    //  修复：创建成功给客户端提示
    std::string success = "create group [" + group_name + "] success!\n";
    package_message(success,"system");
    return;
}
void Text_msg_handler::group_add_client(std::string group_name,std::string username){

    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_add_client:group_name:user_name\n";
        package_message(fail,"system");
        return;
    }
    auto it = group_list.find(group_name);

    if (it != group_list.end()) {
        if(!it->second->is_manager_fd(client_fd)){
            std::string fail = "You are not the manager of group [" + group_name + "].\n";
            package_message(fail,"system");
            return;
        }
        //找到username对应的fd
        auto user_it = username_to_fd.find(username);
        if(user_it == username_to_fd.end()){
            std::string fail = "User [" + username + "] does not exist.\n";
            package_message(fail,"system");
            return;
        }
        it->second->add_client(username);
         std::string success = "User [" + username + "] added to group [" + group_name + "] successfully.\n";
        package_message(success,"system");
    } else {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        package_message(fail,"system");
    }
}
void Text_msg_handler::group_delete_client(std::string group_name,std::string username){
    if (group_name.empty() || username.empty()) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        package_message(fail,"system");
        return;
    }
    auto it = group_list.find(group_name);
    if (it != group_list.end()) {
        if(!it->second->is_manager_fd(client_fd)){
            std::string fail = "You are not the manager of group [" + group_name + "].\n";
            package_message(fail,"system");
            return;
        }
        if(it->second->delete_client(username)){
            std::string success = "User [" + username + "] removed from group [" + group_name + "] successfully.\n";
            package_message(success,"system");
        }else{
            std::string fail = "User [" + username + "] is not in group [" + group_name + "].\n";
            package_message(fail,"system");
        }
        
    } else {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        package_message(fail,"system");
    }
}
void Text_msg_handler::delete_group(std::string group_name){
    if (group_name.empty()) {
        std::string fail = "Invalid format. Use /group_delete_client:group_name:user_name\n";
        package_message(fail,"system");
        return;
    }
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = group_list.find(group_name);
    if (it == group_list.end()) {
        std::string fail = "Group [" + group_name + "] does not exist.\n";
        package_message(fail,"system");
        return;
    }
    if(!it->second->is_manager_fd(client_fd)){
        std::string fail = "You are not the manager of group [" + group_name + "].\n";
        package_message(fail,"system");
        return;
    }
    it->second->group_spk("Group [" + group_name + "] is being deleted by the manager.\n");
    group_list.erase(it);

    std::string success = "Group [" + group_name + "] deleted successfully.\n";
    package_message(success,"system");

}
void Text_msg_handler::modify_group_name(std::string old_name,std::string new_name){
    auto it = find_group_by_name(old_name);
    if(it == nullptr){
        //发送未找到群聊提示
        return;
    }
    it->modify_group_name(client_fd,new_name);
}
//================================
Text_msg_handler::~Text_msg_handler(){
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
void Text_msg_handler::package_message(const std::string& message,std::string type){
    // 构造包（4字节长度头 + 消息体），并直接交给 sender
    json msg_json;
    msg_json["type"] = type;
    msg_json["content"] = message;
    std::string msg_str = msg_json.dump();

    uint32_t msg_length = msg_str.size();
    uint32_t net_len = htonl(msg_length);
    std::string packet;
    // 在 packet.append 之前加这一行来预分配内存，避免多次扩容导致性能问题
    packet.reserve(4 + msg_str.size());
    packet.append(reinterpret_cast<const char*>(&net_len), sizeof(net_len)); // 包头（网络字节序）
    packet += msg_str; // 包体
    // 直接把本次构造的包交给 sender 处理并注册写事件，避免保留两份缓冲区
    sender->add_to_out_buffer(packet);

}

void Text_msg_handler::send_msg(){
    sender->send_msg();
}
void Text_msg_handler::preprocess_recv_data(std::string raw_message){
    std::string message = recver->process_recv_data(raw_message);
    handle(message);
}