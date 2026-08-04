#include "client_session.h"
#include "receiver_sender.h"
#include "message_handler.h"
#include "group.h" 
#include "session_manager.h"
#include "social_module.h"


extern bool running;

// 方案 3b 重拆：client_session 不再构造/持有 connection（由 sub_reactor 创建并绑定）。
// 客户端连接在 sub_reactor::add_connect 中创建 connection 与 client_session，
// 再通过 set_connection() 把会话和 connection 绑定起来。

void client_session::set_connection(const std::shared_ptr<connection>& c){
    conn_ = c;   // 存弱引用，避免与 connection 持有的 shared_ptr<client_session> 形成环
    // 未登录时 session_key_ 以连接 fd 作为 session_manager 的 key（登录后切换为 UID）
    if (session_key_ < 0) {
        session_key_ = c->fd();
    }
}

connection& client_session::conn(){
    // 仅在本连接的消息处理（process_incoming 任务）期间调用：此时任务持有 connection，
    // 弱引用锁定的共享所有权必成功。
    auto c = conn_.lock();
    return *c;
}

void client_session::init_(){
    //初始化消息处理器，后续可以根据需要添加更多类型的消息处理器
    handlers_["private_chat"]           = std::make_unique<Chat_handler>();// 聊天消息.私聊
    handlers_["group_chat"]             = std::make_unique<Chat_handler>();// 聊天消息.群聊
    handlers_["add_friend"]             = std::make_unique<Chat_handler>();// 好友相关 -----------未实现
    
    //基本功能
    handlers_["show"]                   = std::make_unique<Base_handler>();// 展示聊天对象
    handlers_["exit"]                   = std::make_unique<Base_handler>();
    handlers_["login"]                  = std::make_unique<Base_handler>();
    handlers_["register"]               = std::make_unique<Base_handler>();
    //群聊相关
    handlers_["create_group"]           = std::make_unique<Group_handler>();
    handlers_["group_add_client"]       = std::make_unique<Group_handler>();
    handlers_["group_delete_client"]    = std::make_unique<Group_handler>();
    handlers_["delete_group"]           = std::make_unique<Group_handler>();
    handlers_["modify_group_name"]      = std::make_unique<Group_handler>();
    //文件相关
    handlers_["download_file"]          = std::make_unique<File_handler>();
    handlers_["upload_file"]            = std::make_unique<File_handler>();
}

//===============消息处理===============
// 方案 3b：接收缓冲/切帧已在 connection::process_incoming 完成，
// 这里只负责解析这一条完整 JSON 消息并按 type 策略分发到对应 handler。
void client_session::on_message(const std::string& json_data, std::string file_data){
    json msg_json;
    try{
        msg_json = json::parse(json_data);
    }catch(const std::exception& e){
        std::cerr << "Error handling message: " << e.what() << std::endl;
        return;
    }
    if(!msg_json.contains("type")){
        std::string fail = "Invalid message format: missing 'type' field.\n";
        package_message(fail,"system");
        return;
    }
    //策略分发到对应的处理者
    std::string type = msg_json["type"];
    auto handler_it = handlers_.find(type);
    if(handler_it != handlers_.end()){
        handler_it->second->handle_message(msg_json, *this, file_data);
    }else{
        std::string fail = "Unknown command type.\n";
        package_message(fail,"system");
    }
}

//===============注册、登录、退出、展示===============
void client_session::register_user(std::string username,std::string password){
    if(username.empty() || password.empty()){
        std::string fail = "Username and password cannot be empty.\n";
        package_message(fail,"system");
        return;
    }
    std::string UID = account_manager::get_instance().register_account(username, password);
    std::string success = "Registration successful. You can now log in with UID " + UID + ".\n";
    package_message(success,"system");
}
void client_session::login(int UID,std::string password){
    auto account = account_manager::get_instance().find_account(UID);
    if (!account) {
        std::string fail = "Account with UID [" + std::to_string(UID) + "] does not exist.\n";
        package_message(fail,"system");
        return;
    }
    if (!account->passwd_check(password)) {
        std::string fail = "Incorrect password for UID [" + std::to_string(UID) + "].\n";
        package_message(fail,"system");
        return;
    }
    current_account_ = account;//init 账户
    social_manager_ = social_manager::get_instance().find_relation_manager(UID);//init 社交模块
    // 切换 session_manager 的 key：从 fd 切换到 UID
    // 先把当前 session 从 session_manager 里取出来（用旧 key）
    auto self_session = session_manager::get_instance().find_session(session_key_);
    session_manager::get_instance().remove_session(session_key_);
    session_key_ = UID;
    if (self_session) {
        session_manager::get_instance().add_session(UID, self_session);
    }
    std::string success = "Login successful. Welcome, " + account->getName() + "!\n";
    package_message(success,"system");
}
void client_session::logout(){
    //清除account信息，清除社交关系信息，清除当前用户的聊天对象列表
    if (current_account_) {
        session_manager::get_instance().remove_session(session_key_);
    }
    current_account_.reset();
    social_manager_.reset();

    //重新加载用户数据，清理当前用户的登录状态
    //重新加载social_realations数据，清理当前用户的好友关系
    package_message("Logout successful.\n","system");
    exit_self();
}
void client_session::exit_self(){
    //清理资源，关闭连接
    std::string success = "Goodbye!\n";
    package_message(success,"system");
    online = false;
    // 从 session_manager 移除（如果还在的话）
    if (current_account_) {
        session_manager::get_instance().remove_session(session_key_);
    }
    // 通知 connection 关闭底层 fd；sub_reactor 会在收到关闭事件后回收该 connection
    if (auto c = conn_.lock()) {
        c->close();
    }
}
void client_session::show_chatlist(){
    std::string chat_list = social_manager_->show_friends();//调用社交模块的查询
    package_message(chat_list,"system");
}
//==========================================================================================
//============================================业务逻辑=======================================
//==========================================================================================

void client_session::group_chat(int target_UID,std::string message){
    auto grp = Group_manager::get_instance().find_group(target_UID);
    if (grp) {
        grp->group_spk(message);
    }
}

void client_session::private_chat(int target_UID, std::string message) {
    if (!current_account_) {
        package_message("You must be logged in to send private messages.\n", "system");
        return;
    }
    if (message.empty()) {
        package_message("Message cannot be empty.\n", "system");
        return;
    }

    // 1. 检查目标账号是否存在
    if(!target_UID_is_exit(target_UID)){return;}

    // 2. 检查目标用户是否在线
    if(!target_UID_is_online(target_UID)){return;}

    // 3. 构造带发送者名字的消息并发送给目标用户
    auto target_session = session_manager::get_instance().find_session(target_UID);
    if (!target_session) {
        package_message("Target user session not found.\n", "system");
        return;
    }
    std::string formatted_msg = "[" + current_account_->getName() + "]: " + message;
    target_session->package_message(formatted_msg, "private_chat");

    // 可选：给发送者一个回显（已发送提示）
    // package_message("[to " + target_account->getName() + "]: " + message, "private_chat");
}

void client_session::send_friend_request(int target_UID, std::string apply_message){
    if (social_manager_) {
        social_manager_->send_friend_request(target_UID, apply_message);
    }
}
void client_session::create_group(std::string group_name){
    if(group_name.empty()){
        std::string fail = "The group name can't be empty";
        package_message(fail,"system");
        return;
    }
    int group_uid = -1;
    social_manager_->create_friend_group(group_name, group_uid);
    if (group_uid >= 0) {
        std::string success = "Group created successfully. Group name: [" + group_name + "], Group UID: " + std::to_string(group_uid) + ".\n";
        package_message(success,"system");
    }
    return;
}
void client_session::group_add_client(int target_group_UID,int target_user_UID){
    if(!target_UID_is_exit(target_group_UID) || !target_UID_is_exit(target_user_UID)){
        return;
    }
    Group_manager::get_instance().add_group_member(target_group_UID,target_user_UID,current_account_->getUID());
}
void client_session::group_delete_client(int target_group_UID,int target_user_UID){
    if(!target_UID_is_exit(target_group_UID) || !target_UID_is_exit(target_user_UID)){
        return;
    }
    Group_manager::get_instance().remove_group_member(target_group_UID,target_user_UID,current_account_->getUID());
}
void client_session::delete_group(int group_UID){
    if(!target_UID_is_exit(group_UID)){
        return;
    }
    Group_manager::get_instance().delete_group(group_UID,current_account_->getUID());
}
void client_session::modify_group_name(int group_UID,std::string new_name){
    if(!target_UID_is_exit(group_UID)){
        return;
    }
    auto it = Group_manager::get_instance().find_group(group_UID);
    it->modify_group_name(current_account_->getUID(),new_name);
}
//====================================================================================
//====================================================================================

//===============析构函数===============
client_session::~client_session(){
    // 确保从 session_manager 移除
    session_manager::get_instance().remove_session(session_key_);
    // connection 由 sub_reactor 独立管理其生命周期，无关本会话析构；弱引用自动失效
}
//===============================数据处理================================
void client_session::package_message(const std::string& message,std::string type){
    // 方案 3b 重拆：打包序列化在 connection，会话只做转发（conn_ 为弱引用，连接存活则发送成功）
    if (auto c = conn_.lock()) {
        c->package_message(message, type);
    }
}


bool client_session::target_UID_is_exit(int target_UID){
    // 校验个人UID是否存在
    if (account_manager::get_instance().find_account(target_UID)) {
        return true;
    }
    // 校验群聊UID是否存在
    if (Group_manager::get_instance().find_group(target_UID)) {
        return true;
    }
    std::string fail = "UID [" + std::to_string(target_UID) + "] does not exist.\n";
    package_message(fail, "system");
    return false;
}
bool client_session::target_UID_is_online(int target_UID){
    if(session_manager::get_instance().find_session(target_UID)){
        return true;
    }
    std::string fail = "User [" + std::to_string(target_UID) + "] is not online.\n";
    package_message(fail, "system");
    return false;
}






/*
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
    */

/*
void client_session::send_msg(){
    sender_->send_msg();
}
void client_session::preprocess_recv_data(std::string raw_message){
    while (!raw_message.empty() && online) {
        Standard_Message recv_result = receiver_->process_recv_data(raw_message);
        if (!recv_result.is_valid) {
            return;
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
        handle(recv_result.json_part,recv_result.file_part,type);
        // if (type == "chat") {
        //     // chat类型：调用handle处理JSON内容
        //     handle(recv_result.json_part);
        // } else if (type == "file") {
        //     // file类型：交给文件类处理（暂未实现，留空）
        //     // TODO: 文件处理逻辑
        //     // FileHandler::process_file(recv_result.json_part, recv_result.file_part);
        //     receiver_->upload_file(msg_json, recv_result.file_part);
        // }
    }
}*/