#include "client_session.h"
#include "group.h" 


extern bool running;

//===============消息处理===============
void client_session::handle(std::string message){
    json msg_json;
    try{
        msg_json = json::parse(message);
    }catch(const std::exception& e){
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
    if(!msg_json.contains("cmd")){
        std::string fail = "Invalid message format: missing 'cmd' field.\n";
        //write(client_fd, fail.c_str(), fail.size());
        package_message(fail,"system");
        return;
    }
    std::string type = msg_json["cmd"];
    auto handler_it = handlers_.find(type);
    if(handler_it != handlers_.end()){
        handler_it->second->handle_message(msg_json, *this);
        return;
    }else{
        std::string fail = "Unknown command type.\n";
        //write(client_fd, fail.c_str(), fail.size());
        package_message(fail,"system");
        return;
    }

    if(type == "spk"){
        std::string UID = msg_json["target_UID"].get<std::string>();
        spk_to(UID,msg_json["content"].get<std::string>());
        return;
    }
    if(type == "show"){
        show_chatlist();
        return;
    }
    if(type == "create_group"){
        create_group(msg_json["target_name"].get<std::string>());
        return;
    }
    if(type == "group_add_client"){
        group_add_client(msg_json["group_name"].get<std::string>(),msg_json["target_name"].get<std::string>());
        return;
    }
    if(type == "group_delete_client"){
        group_delete_client(msg_json["group_name"].get<std::string>(),msg_json["target_name"].get<std::string>());
        return;
    }
    if(type == "exit"){
        exit_self();
        return;
    }
    if(type == "download_file"){
        // 这里的content是文件路径，实际应用中可能还需要文件ID等信息
        sender_->send_file(msg_json["content"].get<std::string>());
        return;
    }
    if(type == "delete_group"){
        delete_group(msg_json["target_name"].get<std::string>());
        return;
    }
    if(type == "modify_group_name"){
        modify_group_name(msg_json["target_name"].get<std::string>(),msg_json["content"].get<std::string>());
        return;
    } 
    if(type == "login"){
        login(msg_json["username"].get<std::string>(),msg_json["password"].get<std::string>());
        return;
    }
    
        std::string fail = "Unknown command type.\n";
        package_message(fail,"system");
    

}

//===============注册、登录、退出、展示===============
void client_session::register_user(std::string username,std::string password){

}
void client_session::login(std::string username,std::string password){

    // 构造临时用户用于验证
    user tmp_user(client_fd, username, password);
    
    if(tmp_user.passwd_check()){
        std::lock_guard<std::mutex>lock(client_mutex);
        // 创建shared_ptr并存入users
        auto tmp = std::make_shared<user>(client_fd, username, password);
        users[client_fd] = tmp;
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
void client_session::exit_self(){
    std::lock_guard<std::mutex> lock(client_mutex);
    auto user_it = users.find(client_fd);
    if (user_it != users.end()) {
        username_to_fd.erase(user_it->second->getName());
    }
    auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
    if (it != activate_clients.end()) {
        activate_clients.erase(it);
    }
    users.erase(client_fd);
    close(client_fd);
}
void client_session::show_chatlist(){
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
//==========================================================================================
//============================================业务逻辑=======================================
//==========================================================================================
void client_session::spk_to(std::string name,std::string message){

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
void client_session::spk_group(std::shared_ptr<group> chat_group,std::string message){
    //package_message("[group chat]:"+message);
    chat_group->group_spk(message);
}

void client_session::spk_personally(int target_fd,std::string message){    
    std::string msg="["+users[client_fd]->getName()+"]:"+message;
    auto user_it = users.find(target_fd);
    if (user_it == users.end()) {
        std::string fail = "User [" + std::to_string(target_fd) + "] does not exist.\n";
        package_message(fail,"system");
        return;
    }
    if(target_fd != -1){
        // package_message(msg,users[target_fd].getName());
        auto it = handle_msg_list.find(std::to_string(target_fd));
        if (it != handle_msg_list.end()) {
                it->second->package_message(msg, users[client_fd]->getName());
            }
    }

}

void client_session::create_group(std::string group_name){
    

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
void client_session::group_add_client(std::string group_name,std::string username){

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
void client_session::group_delete_client(std::string group_name,std::string username){
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
void client_session::delete_group(std::string group_name){
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
void client_session::modify_group_name(std::string old_name,std::string new_name){
    auto it = find_group_by_name(old_name);
    if(it == nullptr){
        //发送未找到群聊提示
        return;
    }
    it->modify_group_name(client_fd,new_name);
}
//====================================================================================
//====================================================================================

//===============析构函数===============
client_session::~client_session(){
    auto user_it = users.find(client_fd);
    if (user_it != users.end()) {
        username_to_fd.erase(user_it->second->getName());
    }

    auto it = std::find(activate_clients.begin(), activate_clients.end(), client_fd);
    if (it != activate_clients.end()) {
        activate_clients.erase(it);
    }
    users.erase(client_fd);
    close(client_fd);
}
//===============================数据处理================================
void client_session::package_message(const std::string& message,std::string type){
    // 统一协议: |4字节总长度|4字节JSON长度|JSON|file|
    json msg_json;
    msg_json["type"]= "chat"; // 这里的type是消息类型，和之前的cmd区分开
    msg_json["cmd"] = type;
    msg_json["content"] = message;
    std::string json_str = msg_json.dump();

    uint32_t json_len = htonl(json_str.size());
    uint32_t total_len = 8+msg_json.dump().size(); // 包头长度 + JSON长度 + file长度（0）

    std::string packet;
    packet.reserve(8 + json_str.size());
    packet.append(reinterpret_cast<const char*>(&total_len), 4);
    packet.append(reinterpret_cast<const char*>(&json_len), 4);
    // packet.append(reinterpret_cast<const char*>(&file_len), 4);
    packet += json_str;
    // file部分为空，不追加

    sender_->add_to_out_buffer(packet);

}

void client_session::send_msg(){
    sender_->send_msg();
}
void client_session::preprocess_recv_data(std::string raw_message){
    while (true) {
        Standard_Message recv_result = receiver_->process_recv_data(raw_message);
        if (!recv_result.is_valid) {
            break;
        }
        
        // 解析JSON判断类型
        json msg_json;
        try {
            msg_json = json::parse(recv_result.json_part);
        } catch(const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            raw_message.clear();
            continue;
        }
        
        if (!msg_json.contains("type")) {
            std::cerr << "Missing 'type' field in message" << std::endl;
            raw_message.clear();
            continue;
        }
        
        std::string type = msg_json["type"];
        
        if (type == "chat") {
            // chat类型：调用handle处理JSON内容
            handle(recv_result.json_part);
        } else if (type == "file") {
            // file类型：交给文件类处理（暂未实现，留空）
            // TODO: 文件处理逻辑
            // FileHandler::process_file(recv_result.json_part, recv_result.file_part);
            receiver_->handle_file(msg_json, recv_result.file_part);
        }
        
        raw_message.clear();
    }
}
