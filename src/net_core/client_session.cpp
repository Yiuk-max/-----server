#include "client_session.h"
#include "receiver_sender.h"
#include "message_handler.h"
#include "group.h" 
#include "session_manager.h"
#include "social_module.h"
#include "notice_service.h"
#include "group_manager.h"
#include <ctime>
#include <cstdio>


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
    handlers_["delete_message"]         = std::make_unique<Chat_handler>();// 删除消息
    handlers_["refresh_offline_messages"] = std::make_unique<Chat_handler>();// 手动刷新离线消息
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
    handlers_["send_join_group"]        = std::make_unique<Group_handler>();// 申请加入群聊
    handlers_["handle_join_request"]    = std::make_unique<Group_handler>();// 处理入群申请
    handlers_["modify_member_role"]     = std::make_unique<Group_handler>();// 修改成员身份
    handlers_["show_group_requests"]    = std::make_unique<Group_handler>();// 查看待处理入群申请
    handlers_["show_group_members"]     = std::make_unique<Group_handler>();// 查看群成员
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
        // 失败：DB 不可用等（昵称/密码允许重复，不再因重名失败）
        std::string fail = "Registration failed (database unavailable).\n";
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

    // 记录上一次登录时间（用于查询离线消息：自上次登录之后未收到的消息）
    std::string last_login_time = account->get_last_login_time();

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
    show_friend_requests(); // 登录后自动查看待处理的好友申请
    // 登录后自动查看待处理的群聊入群申请（若有权限）todo

    // 登录后自动获取一次离线消息（自上次登录时间之后）
    send_offline_messages(last_login_time);

    // 更新"上次登录时间"（存入 Account.settings JSON），供下次登录查询离线消息
    {
        char time_buf[32];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&now, &tmv);
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tmv);
        account->set_last_login_time(time_buf);
        repo_hub_->accounts()->update_account(account);
    }
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
    if (!current_account_) {
        package_message("You must be logged in to send group messages.\n", "system");
        return;
    }
    // 校验群存在
    auto grp = group_manager::get_instance().get(target_UID);
    if (!grp) {
        std::string fail = "Group [" + std::to_string(target_UID) + "] does not exist.\n";
        package_message(fail, "system");
        return;
    }
    // 先存储消息，取回数据库分配的 message_id
    int msg_id = repo_hub_->messages()->store_message(current_account_->getUID(), target_UID, message, true);
    // 从数据库拉取群成员列表，经全局通知服务广播（自动忽略不在线成员），并携带 message_id
    auto members = repo_hub_->groups()->get_group_members(target_UID);
    std::string formatted_msg = "[" + current_account_->getName() + "]: " + message;
    if (msg_id > 0) {
        NoticeService::get_instance().send_to_users_with_id(members, formatted_msg, "Group_Chat", msg_id);
        // 告知发送者消息 id，便于 3 分钟内删除
        package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        NoticeService::get_instance().send_to_users(members, formatted_msg, "Group_Chat");
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

    // 3. 检查目标用户是否是自己的好友
    if (!repo_hub_->friends()->is_friend(current_account_->getUID(), target_UID)) {
        std::string fail = "UID [" + std::to_string(target_UID) + "] is not your friend. You can only send private messages to friends.\n";
        package_message(fail, "system");
        return;
    }

    // 4. 构造带发送者名字的消息并发送给目标用户
    auto target_session = session_manager::get_instance().find_session(target_UID);
    if (!target_session) {
        package_message("Target user session not found.\n", "system");
        return;
    }
    // 先存储消息，取回数据库分配的 message_id
    int msg_id = repo_hub_->messages()->store_message(current_account_->getUID(), target_UID, message, false);
    std::string formatted_msg = "[" + current_account_->getName() + "]: " + message;
    if (msg_id > 0) {
        target_session->package_chat_message(formatted_msg, "private_chat", msg_id);
        // 告知发送者消息 id，便于 3 分钟内删除
        package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        target_session->package_message(formatted_msg, "private_chat");
    }
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
        std::string fail = "Failed to update name (database unavailable).\n";
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
        bool handled = social_manager_->handle_friend_request(sender_UID, accept);
        // 同意后双方都应在好友列表看到对方：同步更新申请人（若在线）的内存好友列表
        if (handled && accept) {
            auto sender_session = session_manager::get_instance().find_session(sender_UID);
            if (sender_session) {
                sender_session->add_friend_to_list(current_account_->getUID());
            }
        }
    }
}
// 删除好友
void client_session::remove_friend(int friend_UID){
    if (!current_account_) {
        package_message("You must be logged in to remove a friend.\n", "system");
        return;
    }
    if (social_manager_) {
        bool removed = social_manager_->remove_friend(friend_UID);
        // 被删除方（若在线）也应同步更新内存好友列表，避免其仍看到已删除的好友
        if (removed) {
            auto target_session = session_manager::get_instance().find_session(friend_UID);
            if (target_session) {
                target_session->remove_friend_from_list(current_account_->getUID());
            }
        }
    }
}
// 同步更新内存好友列表（由对方同意加好友后调用）
void client_session::add_friend_to_list(int friend_UID){
    if (social_manager_) {
        social_manager_->add_friend_to_list(friend_UID);
    }
}
// 同步更新内存好友列表（被对方删除后调用）
void client_session::remove_friend_from_list(int friend_UID){
    if (social_manager_) {
        social_manager_->remove_friend_from_list(friend_UID);
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
    if (!current_account_) {
        package_message("You must be logged in to create a group.\n", "system");
        return;
    }
    int group_uid = -1;
    social_manager_->create_friend_group(group_name, group_uid);
    if (group_uid >= 0) {
        std::string success = "Group created successfully. Group name: [" + group_name + "], Group UID: " + std::to_string(group_uid) + ".\n";
        package_message(success,"system");
    } else {
        std::string fail = "Failed to create group (group name may already exist or DB unavailable).\n";
        package_message(fail,"system");
    }
    return;
}
void client_session::group_add_client(int target_group_UID,int target_user_UID){
    if(!target_UID_is_exit(target_group_UID) || !target_UID_is_exit(target_user_UID)){
        return;
    }
    if (!repo_hub_->groups()->member_add_group(target_group_UID, current_account_->getUID(), target_user_UID)) {
        package_message("Failed to add member (you are not in the group or member already exists).\n", "system");
        return;
    }
    std::string success = "Member [" + std::to_string(target_user_UID) + "] added to group [" + std::to_string(target_group_UID) + "] successfully.\n";
    package_message(success,"system");
    // 被拉入的成员若在线，同步其内存群列表并通知已入群
    auto new_member_session = session_manager::get_instance().find_session(target_user_UID);
    if (new_member_session) {
        new_member_session->add_group_to_list(target_group_UID);
        new_member_session->package_message(
            "You have been added to group [" + std::to_string(target_group_UID) + "].\n", "system");
    }
}
void client_session::group_delete_client(int target_group_UID,int target_user_UID){
    if(!target_UID_is_exit(target_group_UID) || !target_UID_is_exit(target_user_UID)){
        return;
    }
    if (!repo_hub_->groups()->remove_group_member(target_group_UID, current_account_->getUID(), target_user_UID)) {
        package_message("Failed to remove member (only owner can kick, or member not in group).\n", "system");
        return;
    }
    std::string success = "Member [" + std::to_string(target_user_UID) + "] removed from group [" + std::to_string(target_group_UID) + "] successfully.\n";
    package_message(success,"system");
    // 被踢成员若在线，同步其内存群列表并通知
    auto target_session = session_manager::get_instance().find_session(target_user_UID);
    if (target_session) {
        target_session->remove_group_from_list(target_group_UID);
        target_session->package_message(
            "You have been removed from group [" + std::to_string(target_group_UID) + "].\n", "system");
    }
}
void client_session::delete_group(int group_UID){
    if(!target_UID_is_exit(group_UID)){
        return;
    }
    if (!repo_hub_->groups()->delete_group(group_UID, current_account_->getUID())) {
        package_message("Failed to delete group (only owner can delete).\n", "system");
        return;
    }
    // 同步清理本会话群列表
    if (social_manager_) {
        social_manager_->exit_friend_group(group_UID);
    }
    // 群已被删除，从内存管理器中强制移除（不论是否还有人持有）
    group_manager::get_instance().remove_group(group_UID);
    package_message("Group deleted successfully.\n", "system");
}
void client_session::modify_group_name(int group_UID,std::string new_name){
    if(!target_UID_is_exit(group_UID)){
        return;
    }
    if (!repo_hub_->groups()->modify_group_name(group_UID, current_account_->getUID(), new_name)) {
        package_message("Failed to modify group name (only owner can modify).\n", "system");
        return;
    }
    std::string success = "Group name updated successfully. New name: [" + new_name + "].\n";
    package_message(success,"system");
}
// 申请加入群聊：落库申请（relation_apply, apply_type=2），等待群主处理
void client_session::send_join_group(int group_UID){
    if (!current_account_) {
        package_message("You must be logged in to join a group.\n", "system");
        return;
    }
    if (!repo_hub_->groups()->send_join_group(group_UID, current_account_->getUID())) {
        package_message("Failed to send join request (group not exists / already a member / duplicate request).\n", "system");
        return;
    }
    std::string success = "Join request sent to group [" + std::to_string(group_UID) + "] successfully.\n";
    package_message(success,"system");
    // 通知群主/管理员（若在线）提醒处理入群申请（get 内部已做内存优先 + DB 兜底）
    auto grp = group_manager::get_instance().get(group_UID);
    if (grp) {
        int owner_UID = grp->get_manager_UID();
        auto owner_session = session_manager::get_instance().find_session(owner_UID);
        if (owner_session) {
            std::string notice = "User [" + std::to_string(current_account_->getUID()) + "] has requested to join your group [" + std::to_string(group_UID) + "]. Please handle the request.\n";
            owner_session->package_message(notice, "system");
        }
    }
}
// 处理入群申请：requester_UID 为申请人，本会话为群主；同意则拉人入群
void client_session::handle_join_request(int group_UID,int requester_UID,bool accept){
    if (!current_account_) {
        package_message("You must be logged in to handle join requests.\n", "system");
        return;
    }
    if (!repo_hub_->groups()->handle_join_request(group_UID, current_account_->getUID(), requester_UID, accept)) {
        package_message("Failed to handle join request (you are not owner or no pending request).\n", "system");
        return;
    }
    std::string result = accept ? "Join request accepted.\n" : "Join request rejected.\n";
    package_message(result, "system");
    // 同步申请人（若在线）：同意则更新其内存群列表并通知已入群；拒绝则通知被拒
    auto requester_session = session_manager::get_instance().find_session(requester_UID);
    if (requester_session) {
        if (accept) {
            requester_session->add_group_to_list(group_UID);
            requester_session->package_message(
                "You have been added to group [" + std::to_string(group_UID) + "].\n", "system");
        } else {
            requester_session->package_message(
                "Your join request for group [" + std::to_string(group_UID) + "] was rejected.\n", "system");
        }
    }
}
// 同步更新内存群列表（被拉入群/申请通过后调用）
void client_session::add_group_to_list(int group_UID){
    if (social_manager_) {
        social_manager_->add_group_to_list(group_UID);
    }
}
// 同步更新内存群列表（被踢出群后调用）
void client_session::remove_group_from_list(int group_UID){
    if (social_manager_) {
        social_manager_->remove_group_from_list(group_UID);
    }
}
// 修改群成员身份：promote=true 提升为群主 / false 降回普通成员
void client_session::modify_member_role(int group_UID,int target_UID,bool promote){
    if (!current_account_) {
        package_message("You must be logged in to modify member role.\n", "system");
        return;
    }
    if (!repo_hub_->groups()->modify_member_role(group_UID, current_account_->getUID(), target_UID, promote)) {
        package_message("Failed to modify member role (only owner can promote/demote).\n", "system");
        return;
    }
    std::string success = "Role of UID [" + std::to_string(target_UID) + "] updated successfully.\n";
    package_message(success,"system");
}
// 查看群聊待处理的入群申请：仅群主可查看
void client_session::show_group_requests(int group_UID){
    if (!current_account_) {
        package_message("You must be logged in to view group join requests.\n", "system");
        return;
    }
    std::vector<std::tuple<int, std::string>> requests;
    if (!repo_hub_->groups()->show_group_requests(group_UID, requests)) {
        package_message("Failed to retrieve group join requests.\n", "system");
        return;
    }
    if (requests.empty()) {
        package_message("No pending join requests for group [" + std::to_string(group_UID) + "].\n", "system");
        return;
    }
    std::string result = "Pending join requests for group [" + std::to_string(group_UID) + "]:\n";
    for (const auto& req : requests) {
        int requester_UID;
        std::string apply_message;
        std::tie(requester_UID, apply_message) = req;
        result += "Requester UID: " + std::to_string(requester_UID) + ", Message: " + apply_message + "\n";
    }
    package_message(result, "system");
}
// 查看群成员（含群内名字）
void client_session::show_group_members(int group_UID){
    if(!target_UID_is_exit(group_UID)){
        return;
    }
    package_message(repo_hub_->groups()->show_group_members(group_UID), "system");
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

void client_session::package_chat_message(const std::string& message, std::string type, int message_id){
    if (auto c = conn_.lock()) {
        c->package_chat_message(message, type, message_id);
    }
}

// 解析 MySQL DATETIME "YYYY-MM-DD HH:MM:SS" 为 time_t；失败返回 0
static std::time_t parse_datetime(const std::string& s) {
    std::tm tmv{};
    if (s.size() < 19) return 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d",
                    &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday,
                    &tmv.tm_hour, &tmv.tm_min, &tmv.tm_sec) != 6) {
        return 0;
    }
    tmv.tm_year -= 1900;
    tmv.tm_mon  -= 1;
    return std::mktime(&tmv);
}

// 删除消息：仅发送者本人可在发送后 3 分钟内删除；删除后通知在线接收方
void client_session::delete_message(int message_id){
    if (!current_account_) {
        package_message("You must be logged in to delete messages.\n", "system");
        return;
    }
    message msg;
    if (!repo_hub_->messages()->get_message(message_id, msg)) {
        std::string fail = "Message [" + std::to_string(message_id) + "] does not exist.\n";
        package_message(fail, "system");
        return;
    }
    if (msg.sender_UID != current_account_->getUID()) {
        package_message("You can only delete your own messages.\n", "system");
        return;
    }
    std::time_t send_t = parse_datetime(msg.timestamp);
    std::time_t now = std::time(nullptr);
    if (send_t == 0 || now - send_t > 180) {
        package_message("Messages can only be deleted within 3 minutes of sending.\n", "system");
        return;
    }
    if (!repo_hub_->messages()->delete_message(message_id)) {
        std::string fail = "Failed to delete message [" + std::to_string(message_id) + "].\n";
        package_message(fail, "system");
        return;
    }
    std::string success = "Message [" + std::to_string(message_id) + "] deleted.\n";
    package_message(success, "system");

    // 通知在线且会收到该消息的人
    if (msg.is_group) {
        auto members = repo_hub_->groups()->get_group_members(msg.receiver_UID);
        for (int uid : members) {
            if (uid == current_account_->getUID()) continue; // 跳过删除者自己
            NoticeService::get_instance().send_to_user_with_id(uid, "", "delete_message", message_id);
        }
    } else {
        NoticeService::get_instance().send_to_user_with_id(msg.receiver_UID, "", "delete_message", message_id);
    }
}

// 手动刷新离线消息（自本次登录时间之后）
void client_session::refresh_offline_messages(){
    if (!current_account_) {
        package_message("You must be logged in to refresh offline messages.\n", "system");
        return;
    }
    send_offline_messages(current_account_->get_last_login_time());
    package_message("Offline messages refreshed.\n", "system");
}

// 查询并推送自 since_time 之后的离线消息（私聊/群聊）
void client_session::send_offline_messages(const std::string& since_time){
    if (!current_account_) return;
    int uid = current_account_->getUID();
    auto msgs = repo_hub_->messages()->get_offline_messages(uid, since_time);
    for (const auto& m : msgs) {
        std::string sender_name = std::to_string(m.sender_UID);
        auto sender = repo_hub_->accounts()->load_account(m.sender_UID);
        if (sender) {
            sender_name = sender->getName();
        }
        std::string formatted = "[" + sender_name + "]: " + m.content;
        std::string type = m.is_group ? "Group_Chat" : "private_chat";
        package_chat_message(formatted, type, m.message_id);
    }
}

//=======================效验target_UID_is_exit=====================
bool client_session::target_UID_is_exit(int target_UID){
    // 校验个人UID是否存在（账号仓储接口查询）
    if (repo_hub_->accounts()->load_account(target_UID)) {
        return true;
    }
    // 校验群聊UID是否存在（get 内部已做内存优先 + DB 兜底）
    if (group_manager::get_instance().get(target_UID)) {
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
