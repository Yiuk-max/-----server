#include "client_session.h"
#include "receiver_sender.h"
#include "message_handler.h"
#include "group.h" 
#include "session_manager.h"
#include "social_module.h"
#include <ctime>


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
    handlers_["add_friend"]             = std::make_unique<Chat_handler>();// 好友申请
    handlers_["set_friend_remark"]      = std::make_unique<Chat_handler>();// 给好友设置备注名
    handlers_["accept_friend"]          = std::make_unique<Chat_handler>();// 同意好友申请
    handlers_["reject_friend"]          = std::make_unique<Chat_handler>();// 拒绝好友申请
    handlers_["remove_friend"]          = std::make_unique<Chat_handler>();// 删除好友
    handlers_["show_friend_requests"]   = std::make_unique<Chat_handler>();// 查看待处理好友申请
    
    //基本功能
    handlers_["show"]                   = std::make_unique<Base_handler>();// 展示聊天对象
    handlers_["exit"]                   = std::make_unique<Base_handler>();
    handlers_["login"]                  = std::make_unique<Base_handler>();
    handlers_["register"]               = std::make_unique<Base_handler>();
    handlers_["change_name"]            = std::make_unique<Base_handler>();// 修改自己的昵称
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
    // 心跳包：不进入业务，立即回复 HeartbeatAck，确认连接仍有效
    if (type == "heartbeat") {
        package_message("pong", "heartbeat_ack");
        return;
    }

    auto handler_it = handlers_.find(type);
    if (handler_it != handlers_.end()) {
        handler_it->second->handle_message(msg_json, *this, file_data);
    } else {
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
    // 经仓储门面调账号仓储接口注册：
    //   client_session -> repo_hub_->accounts() -> I_account_repo 契约 -> account_repo (repo 层 MySQL 实现)
    auto new_account = repo_hub_->accounts()->register_account(username, password);
    if (!new_account) {
        // 失败：nickname 重名 / DB 不可用等
        std::string fail = "Registration failed (invalid username/password or account already exists).\n";
        package_message(fail,"system");
        return;
    }
    current_account_ = new_account;
    std::string UID = new_account->get_string_UID();
    std::string success = "Registration successful. You can now log in with UID " + UID + ".\n";
    package_message(success,"system");
}
void client_session::login(int UID,std::string password){
    // 经仓储门面调账号仓储接口加载账户（契约 I_account_repo，由 repo 层 MySQL 实现 SELECT Account WHERE UID=?）
    auto account = repo_hub_->accounts()->load_account(UID);
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

    // 更新"上次登录时间"（存入 Account.settings JSON），登录时更新一次并落库
    {
        char time_buf[32];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&now, &tmv);
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tmv);
        account->set_last_login_time(time_buf);
        repo_hub_->accounts()->update_account(account);
    }

    social_manager_ = std::make_shared<social_module>(UID, repo_hub_);//init 社交模块（登录时创建，随会话生命周期持有）

    // 顶掉旧登录：同一 UID 若已有在线会话，先移除其映射并关闭其连接，避免同账号多会话/残留
    auto old_session = session_manager::get_instance().find_session(UID);
    if (old_session && old_session.get() != this) {
        old_session->kick_offline();                       // 通知旧连接并关闭其 fd
        session_manager::get_instance().remove_session(UID); // 移除旧会话的 UID 映射
    }

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
// 被顶下线：通知 + 立即发送后关闭本连接；sub_reactor 会收到断开事件后回收 connection 与会话
void client_session::kick_offline(){
    if (auto c = conn_.lock()) {
        // 先把提示放入发送缓冲（由 connection::package_message 打包）
        c->package_message("Your account is logged in elsewhere, you have been kicked offline.\n", "system");
        // 立即把缓冲真正 send 出去（非阻塞；单条小消息通常一次可发完），再关闭
        // 否则依赖 EPOLLOUT 异步 flush，可能 close 前还没发出，导致旧客户端收不到提示
        c->sender_obj().send_msg();
        c->close();   // 关闭底层 fd，让 sub_reactor 通过断开事件移除本连接
    }
    online = false;
}
// 登出：退出当前账号登录，但保留连接（区别于 exit_self 的彻底断开）
// 之后该连接仍可继续登录其他账户或接收系统消息。
void client_session::logout(){
    // 1. 先通知客户端退出成功（同步 flush，确保真正送达）
    if (auto c = conn_.lock()) {
        c->package_message("Logout successful.\n", "system");
        c->sender_obj().send_msg();
    }
    // 2. 从 session_manager 移除本次登录记录（key 为 UID）
    if (current_account_) {
        session_manager::get_instance().remove_session(session_key_);
    }
    // 3. 登出后该连接回到"未登录"态：把 key 还原为 fd（以便下次登录时以 fd 作为查找键）
    int self_fd = -1;
    if (auto c = conn_.lock()) {
        self_fd = c->fd();
    }
    if (session_key_ != self_fd) {
        session_key_ = self_fd;
    }
    // 4. 清空账号状态与社交模块，连接保持在线可用
    current_account_.reset();
    social_manager_.reset();
    online = true;
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
}

void client_session::send_friend_request(int target_UID, std::string apply_message){
    if (social_manager_) {
        social_manager_->send_friend_request(target_UID, apply_message);
    }
}
// 给好友设置备注名：先校验目标确实是自己的好友，再落库（friend_relation.remark_name）
void client_session::set_friend_remark(int friend_UID, std::string remark){
    if (!current_account_) {
        package_message("You must be logged in to set a friend remark.\n", "system");
        return;
    }
    if (remark.empty()) {
        package_message("Remark cannot be empty.\n", "system");
        return;
    }
    int my_uid = current_account_->getUID();
    if (!repo_hub_->friends()->is_friend(my_uid, friend_UID)) {
        std::string fail = "UID [" + std::to_string(friend_UID) + "] is not your friend.\n";
        package_message(fail, "system");
        return;
    }
    if (repo_hub_->friends()->set_remark(my_uid, friend_UID, remark)) {
        std::string success = "Remark for UID [" + std::to_string(friend_UID) + "] updated successfully.\n";
        package_message(success, "system");
    } else {
        std::string fail = "Failed to update remark for UID [" + std::to_string(friend_UID) + "].\n";
        package_message(fail, "system");
    }
}
// 修改自己的昵称：先改内存，再落库；失败时回滚内存中的昵称，保证与数据库一致
void client_session::change_my_name(std::string new_name){
    if (!current_account_) {
        package_message("You must be logged in to change your name.\n", "system");
        return;
    }
    if (new_name.empty()) {
        package_message("Name cannot be empty.\n", "system");
        return;
    }
    std::string old_name = current_account_->getName();
    current_account_->setName(new_name);
    if (repo_hub_->accounts()->update_account(current_account_)) {
        std::string success = "Name updated successfully. Your new name is [" + new_name + "].\n";
        package_message(success, "system");
    } else {
        current_account_->setName(old_name); // 失败回滚内存中的昵称，保持与库一致
        std::string fail = "Failed to update name (nickname may already exist).\n";
        package_message(fail, "system");
    }
}
// 处理好友申请（同意/拒绝）
void client_session::handle_friend_request(int sender_UID, bool accept){
    if (!current_account_) {
        package_message("You must be logged in to handle friend requests.\n", "system");
        return;
    }
    if (social_manager_) {
        social_manager_->handle_friend_request(sender_UID, accept);
    }
}
// 删除好友
void client_session::remove_friend(int friend_UID){
    if (!current_account_) {
        package_message("You must be logged in to remove a friend.\n", "system");
        return;
    }
    if (social_manager_) {
        social_manager_->remove_friend(friend_UID);
    }
}
// 查看待处理的好友申请
void client_session::show_friend_requests(){
    if (!current_account_) {
        package_message("You must be logged in to view friend requests.\n", "system");
        return;
    }
    if (social_manager_) {
        package_message(social_manager_->show_friend_requests(), "system");
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
    // 注意：这里不能再调用 session_manager::remove_session()。
    // 触发本析构的常见路径是 session_manager::remove_session() 已在持有 sessions_mutex
    // 排他锁时 erase() 使本对象引用归零、就地析构；若析构里再次 remove_session()，
    // 会对同一把 sessions_mutex 二次排他加锁，导致 std::system_error("Resource deadlock avoided") 崩溃。
    // 实际移除已在 logout()/exit_self()/remove_client() 等入口显式完成。
    // connection 由 sub_reactor 独立管理其生命周期，无关本会话析构；弱引用自动失效。
}
//===============================数据处理================================
void client_session::package_message(const std::string& message,std::string type){
    // 方案 3b 重拆：打包序列化在 connection，会话只做转发（conn_ 为弱引用，连接存活则发送成功）
    if (auto c = conn_.lock()) {
        c->package_message(message, type);
    }
}

//=======================效验target_UID_is_exit=====================
bool client_session::target_UID_is_exit(int target_UID){
    // 校验个人UID是否存在（改为账号仓储接口查询）
    if (repo_hub_->accounts()->load_account(target_UID)) {
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
