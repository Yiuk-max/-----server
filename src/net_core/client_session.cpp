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
#include <random>


extern bool running;

// client_session 只依赖传输端口；TCP connection（以及后续 WsSession）负责具体协议。
void client_session::set_transport(const std::shared_ptr<IClientTransport>& transport){
    transport_ = transport;
}

void client_session::on_disconnected(){
    int uid = -1;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        if (!online && logged_in_uid_ < 0) {
            return;
        }
        online = false;
        uid = logged_in_uid_;
        logged_in_uid_ = -1;
    }
    session_manager::get_instance().remove_online_if_same(uid, this);
    // account/social 保留到正在执行的业务任务结束并随会话析构，避免断线清理
    // 与该任务并发 reset 同一 shared_ptr；在线表已经在上面及时移除。
}

void client_session::upload_file(const chat_proto::FileChunkMeta& meta, const std::string& file_data){
    if (auto transport = transport_.lock()) {
        transport->accept_file_chunk(meta, file_data);
    }
}

void client_session::download_file(const std::string& file_name){
    if (auto transport = transport_.lock()) {
        transport->send_file(file_name);
    }
}

void client_session::init_(){
    //初始化消息处理器，后续可以根据需要添加更多类型的消息处理器
    handlers_["private_chat"]           = std::make_unique<Chat_handler>();// 聊天消息.私聊
    handlers_["group_chat"]             = std::make_unique<Chat_handler>();// 聊天消息.群聊
    handlers_["delete_message"]         = std::make_unique<Chat_handler>();// 删除消息
    handlers_["history_request"]        = std::make_unique<Chat_handler>();// 聊天历史（游标分页）
    handlers_["add_friend"]             = std::make_unique<Chat_handler>();// 好友申请
    handlers_["set_friend_remark"]      = std::make_unique<Chat_handler>();// 给好友设置备注名
    handlers_["accept_friend"]          = std::make_unique<Chat_handler>();// 同意好友申请
    handlers_["reject_friend"]          = std::make_unique<Chat_handler>();// 拒绝好友申请
    handlers_["remove_friend"]          = std::make_unique<Chat_handler>();// 删除好友
    handlers_["show_friend_requests"]   = std::make_unique<Chat_handler>();// 查看待处理好友申请
    
    //基本功能
    handlers_["show"]                   = std::make_unique<Base_handler>();// 展示聊天对象
    handlers_["exit"]                   = std::make_unique<Base_handler>();
    handlers_["logout"]                 = std::make_unique<Base_handler>();// 登出账号（保留连接）
    handlers_["login"]                  = std::make_unique<Base_handler>();
    handlers_["register"]               = std::make_unique<Base_handler>();
    handlers_["change_name"]            = std::make_unique<Base_handler>();// 修改自己的昵称
    handlers_["set_email"]              = std::make_unique<Base_handler>();// 绑定/换绑邮箱
    handlers_["verify_token"]           = std::make_unique<Base_handler>();// 用 token 自动登录
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
// 这里只负责解析这一条完整 protobuf 消息并按 type 策略分发到对应 handler。
void client_session::on_message(const std::string& payload, std::string file_data){
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        if (!online) {
            return; // 连接已退出/被顶号，不再处理接收缓冲中的后续消息
        }
    }

    chat_proto::Envelope msg;
    if(!msg.ParseFromString(payload)){
        std::string fail = "Invalid protobuf message.\n";
        package_message(fail,"system");
        return;
    }
    if(msg.type().empty()){
        std::string fail = "Invalid message format: missing 'type' field.\n";
        package_message(fail,"system");
        return;
    }
    //策略分发到对应的处理者
    const std::string type = msg.type();
    // 心跳包：不进入业务，立即回复 HeartbeatAck，确认连接仍有效
    if (type == "heartbeat") {
        package_message("pong", "heartbeat_ack");
        return;
    }

    auto handler_it = handlers_.find(type);
    if (handler_it != handlers_.end()) {
        handler_it->second->handle_message(msg, *this, file_data);
    } else {
        std::string fail = "Unknown command type.\n";
        package_message(fail,"system");
    }
}

//===============注册、登录、退出、展示===============
void client_session::register_user(std::string username,std::string password,std::string email){
    if(username.empty() || password.empty()){
        std::string fail = "Username and password cannot be empty.\n";
        package_message(fail,"system");
        return;
    }
    if(email.empty()){
        // 新用户注册必须填邮箱
        package_message("Email is required for registration.\n","system");
        return;
    }
    // 邮箱唯一性预检查（并发下最终由 account_email.email 主键兜底）
    if(repo_hub_->emails()->find_uid_by_email(email) != -1){
        package_message("Email [" + email + "] is already registered.\n","system");
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
    const int uid = new_account->getUID();
    if(!repo_hub_->emails()->set_email(uid, email)){
        // 并发下邮箱刚被占用：回滚刚创建的账户，避免产生无邮箱的孤儿账号
        repo_hub_->accounts()->remove_account(uid);
        package_message("Email [" + email + "] is already registered.\n","system");
        return;
    }
    current_account_ = new_account;
    // 注册后把自己加为自己的好友，允许给自己发消息（自聊）。
    repo_hub_->friends()->ensure_self_friend(uid);
    std::string UID = new_account->get_string_UID();
    std::string success = "Registration successful. You can now log in with UID " + UID + " or " + email + ".\n";
    package_message(success,"system");
}

// 用邮箱登录：先查邮箱绑定的 UID，再复用 UID 登录流程
void client_session::login_by_email(std::string email,std::string password){
    int uid = repo_hub_->emails()->find_uid_by_email(email);
    if (uid < 0) {
        std::string fail = "No account is bound to email [" + email + "].\n";
        package_message(fail,"system");
        return;
    }
    login(uid, password);
}

// 绑定/换绑邮箱（老账号补绑邮箱用；不校验格式，仅要求不重复）
void client_session::set_email(std::string email){
    if(!current_account_){
        package_message("You must be logged in to set an email.\n","system");
        return;
    }
    if(email.empty()){
        package_message("Email cannot be empty.\n","system");
        return;
    }
    if(repo_hub_->emails()->set_email(current_account_->getUID(), email)){
        std::string success = "Email updated successfully. Your email is [" + email + "].\n";
        package_message(success,"system");
    } else {
        package_message("Failed to set email (email may already be used by another account).\n","system");
    }
}
// 前置声明：解析 MySQL DATETIME 为 time_t（定义在文件下方，delete_message 附近）
static std::time_t parse_datetime(const std::string& s);

// 生成随机登录令牌（64 位十六进制）
static std::string generate_token() {
    const char* hex = "0123456789abcdef";
    std::string t;
    t.reserve(64);
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dis;
    for (int i = 0; i < 4; i++) {
        unsigned long long v = dis(gen);
        for (int j = 0; j < 16; j++) {
            t += hex[v & 0xF];
            v >>= 4;
        }
    }
    return t;
}

// 登录成功后的通用收尾：建立社交模块、上线映射、返回 token/user JSON、推送申请与离线消息
void client_session::finish_login(const std::shared_ptr<account>& account, const std::string& token) {
    const int UID = account->getUID();
    // 记录上一次登录时间（用于查询离线消息：自上次登录之后未收到的消息）
    std::string last_login_time = account->get_last_login_time();
    // 确保自己是自己的好友：新账号在注册时已写入，这里覆盖历史账号，并在被删除后自愈。
    repo_hub_->friends()->ensure_self_friend(UID);
    auto new_social = std::make_shared<social_module>(UID, repo_hub_);
    auto self = shared_from_this();

    std::shared_ptr<social_module> previous_social;
    std::shared_ptr<client_session> old_session;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        // 同一连接直接切换账号时，先按对象身份移除旧 UID，避免残留两个在线映射。
        if (logged_in_uid_ >= 0 && logged_in_uid_ != UID) {
            session_manager::get_instance().remove_online_if_same(logged_in_uid_, this);
        }
        if (!online) {
            return; // DB 查询期间连接已经断开
        }
        previous_social = std::move(social_manager_);
        current_account_ = account;
        social_manager_ = std::move(new_social);
        logged_in_uid_ = UID;
        old_session = session_manager::get_instance().replace_online(UID, self);
    }
    previous_social.reset();

    if (old_session && old_session.get() != this) {
        old_session->kick_offline();
    }

    // 返回登录成功消息：token + user 信息（前端保存 token 用于自动登录）
    chat_proto::Envelope resp;
    resp.set_type("login_success");
    resp.set_token(token);
    auto* user = resp.mutable_user();
    user->set_id(UID);
    user->set_username(account->getName());
    package_envelope(resp);

    show_friend_requests(); // 登录后自动查看待处理的好友申请
    // 登录后自动获取一次离线消息
    send_offline_messages(last_login_time);

    // 更新"上次登录时间"
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

void client_session::login(int UID,std::string password){
    // 经仓储门面调账号仓储接口加载账户
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
    // 生成并持久化 token，随后返回给前端保存
    std::string token = generate_token();
    repo_hub_->accounts()->update_token(UID, token);
    finish_login(account, token);
}

// 用 token 自动登录（跳过密码）：token 解析 → 验证 → 找到 user_id → 登录成功
void client_session::verify_token(std::string token){
    if (token.empty()) {
        package_message("Token is required.\n", "system");
        return;
    }
    auto account = repo_hub_->accounts()->load_account_by_token(token);
    if (!account) {
        package_message("Invalid or expired token.\n", "system");
        return;
    }
    // 7 天过期：用 Account.settings 里的 last_login_time 判断。
    // 每次登录都会刷新 last_login_time，因此是“7 天不登录则过期”的滑动过期。
    const std::time_t last = parse_datetime(account->get_last_login_time());
    const std::time_t now = std::time(nullptr);
    constexpr std::time_t SEVEN_DAYS = 7 * 24 * 60 * 60;
    if (last == 0 || now - last > SEVEN_DAYS) {
        // 过期：清除 token，要求重新密码登录
        repo_hub_->accounts()->update_token(account->getUID(), "");
        package_message("Token expired, please login again.\n", "system");
        return;
    }
    finish_login(account, token);
}
// 被顶下线：通知进入发送队列，待队列清空后关闭连接。
void client_session::kick_offline(){
    package_message("Your account is logged in elsewhere, you have been kicked offline.\n", "system");
    on_disconnected();
    if (auto transport = transport_.lock()) {
        transport->close(CloseMode::after_pending_writes);
    }
}

// 登出当前账号但保留连接；在线表只保存 UID，因此无需恢复 fd 映射。
void client_session::logout(){
    package_message("Logout successful.\n", "system");

    int uid = -1;
    std::shared_ptr<social_module> old_social;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        if (!online) {
            return;
        }
        uid = logged_in_uid_;
        logged_in_uid_ = -1;
        current_account_.reset();
        old_social = std::move(social_manager_);
    }
    session_manager::get_instance().remove_online_if_same(uid, this);
    old_social.reset();
}

void client_session::exit_self(){
    package_message("Goodbye!\n", "system");
    on_disconnected();
    if (auto transport = transport_.lock()) {
        transport->close(CloseMode::after_pending_writes);
    }
}
void client_session::show_chatlist(){
    if (!current_account_ || !social_manager_) {
        package_message("You must be logged in to view your chat list.\n", "system");
        return;
    }
    std::string chat_list = social_manager_->show_friends();//调用社交模块的查询
    package_message(chat_list,"system");
}
//==========================================================================================
//============================================业务逻辑=======================================
//==========================================================================================

void client_session::group_chat(int target_UID,std::string message,int reply_to_message_id){
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
    // 先存储消息（含 reply_to_message_id），取回数据库分配的 message_id 与发送时间
    std::string msg_timestamp;
    int msg_id = repo_hub_->messages()->store_message(current_account_->getUID(), target_UID, message, true, reply_to_message_id, &msg_timestamp);
    // 回复的原消息摘要（可能不在当前分页里，单独按 id 查）
    std::string reply_name, reply_content;
    if (reply_to_message_id > 0) load_reply_summary(reply_to_message_id, reply_name, reply_content);
    // 从数据库拉取群成员列表，经全局通知服务广播（自动忽略不在线成员），并携带 message_id 与发送时间
    auto members = repo_hub_->groups()->get_group_members(target_UID);
    if (msg_id > 0) {
        NoticeService::get_instance().send_to_users_with_id(members, message, "Group_Chat", msg_id, target_UID,
                                                            current_account_->getUID(), current_account_->getName(),
                                                            reply_to_message_id, reply_name, reply_content, msg_timestamp);
        // 告知发送者消息 id，便于 3 分钟内删除
        package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        NoticeService::get_instance().send_to_users(members, message, "Group_Chat");
    }
}

void client_session::private_chat(int target_UID, std::string message, int reply_to_message_id) {
    if (!current_account_) {
        package_message("You must be logged in to send private messages.\n", "system");
        return;
    }
    if (message.empty()) {
        package_message("Message cannot be empty.\n", "system");
        return;
    }

    // 1. 检查目标账号是否存在（不要求在线，离线也允许发送并落库）
    if(!target_UID_is_exit(target_UID)){return;}

    // 2. 检查目标用户是否是自己的好友
    if (!repo_hub_->friends()->is_friend(current_account_->getUID(), target_UID)) {
        std::string fail = "UID [" + std::to_string(target_UID) + "] is not your friend. You can only send private messages to friends.\n";
        package_message(fail, "system");
        return;
    }

    // 3. 先落库（无论对方是否在线；离线消息由对方上线时离线拉取）
    std::string msg_timestamp;
    int msg_id = repo_hub_->messages()->store_message(current_account_->getUID(), target_UID, message, false, reply_to_message_id, &msg_timestamp);
    // 回复的原消息摘要（可能不在当前分页里，单独按 id 查）
    std::string reply_name, reply_content;
    if (reply_to_message_id > 0) load_reply_summary(reply_to_message_id, reply_name, reply_content);

    // 4. 对方在线则实时转发，否则留在 DB 等其上线离线拉取
    auto target_session = session_manager::get_instance().find_session(target_UID);
    if (target_session) {
        if (msg_id > 0) {
            target_session->package_chat_message(message, "private_chat", msg_id, 0,
                                                 current_account_->getUID(), current_account_->getName(),
                                                 reply_to_message_id, reply_name, reply_content, msg_timestamp);
        } else {
            target_session->package_message(message, "private_chat");
        }
    }

    // 5. 回执给发送者（含 message_id，便于 3 分钟内删除）
    if (msg_id > 0) {
        package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        package_message("Failed to store message (database unavailable).\n", "system");
    }
}

void client_session::send_friend_request(const std::string& email, std::string apply_message){
    if (social_manager_) {
        social_manager_->send_friend_request(email, apply_message);
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
    if (!repo_hub_->groups()->show_group_requests(group_UID, current_account_->getUID(), requests)) {
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

//===============内存池===============
// 池子参数：每 chunk 256 个块、最多 32 个 chunk（即最多 8192 个会话对象）。
// chunk 按需扩容；达到上限且无空闲块时 allocate() 返回 nullptr，这里回退到全局 new。
void* client_session::operator new(std::size_t size) {
    // 防御：只接管本类型大小的单个对象分配，其它（如潜在派生类）走全局。
    if (size != sizeof(client_session)) {
        return ::operator new(size);
    }
    void* p = ClassMemoryPool<client_session, 256, 32>::allocate();
    if (!p) {
        p = ::operator new(size);  // 池耗尽，回退全局 new
    }
    return p;
}

void client_session::operator delete(void* p) noexcept {
    if (!p) {
        return;
    }
    // 先尝试归还池子；不是池子分配的（回退出去的）再走全局 delete。
    if (!ClassMemoryPool<client_session, 256, 32>::deallocate(p)) {
        ::operator delete(p);
    }
}

//===============析构函数===============
client_session::~client_session(){
    // 在线映射由 logout()/on_disconnected()/顶号路径显式按对象身份移除。
}
//===============================数据处理================================
void client_session::package_message(const std::string& message,std::string type){
    chat_proto::Envelope msg;
    msg.set_type(std::move(type));
    msg.set_content(message);
    if (auto transport = transport_.lock()) {
        transport->send_packet(msg);
    }
}

// 构造聊天消息 Envelope（单发与广播共用），把字段填充逻辑收敛到一处。
void build_chat_envelope(chat_proto::Envelope& msg, const std::string& type, const std::string& message,
                         int message_id, int group_uid, int sender_uid, const std::string& sender_name,
                         int reply_to_message_id, const std::string& reply_sender_name,
                         const std::string& reply_content, const std::string& timestamp) {
    msg.set_type(type);
    msg.set_content(message);
    msg.set_message_id(message_id);
    if (group_uid > 0) {
        msg.set_group_uid(group_uid);   // 群聊消息携带群 UID，便于客户端归类
    }
    if (sender_uid > 0) {
        msg.set_sender_uid(sender_uid); // 发送者 UID，便于客户端将消息路由到对应会话
    }
    if (!sender_name.empty()) {
        msg.set_sender_name(sender_name); // 发送者昵称，便于客户端展示
    }
    if (!timestamp.empty()) {
        msg.set_timestamp(timestamp);      // 发送时间，便于客户端展示
    }
    if (reply_to_message_id > 0) {
        // 只带一层引用：直接给出被回复消息的 id + 摘要
        msg.set_reply_to_message_id(reply_to_message_id);
        auto* reply = msg.mutable_reply_to();
        reply->set_message_id(reply_to_message_id);
        reply->set_sender_name(reply_sender_name);
        reply->set_content(reply_content);
    }
}

void client_session::package_chat_message(const std::string& message, std::string type, int message_id, int group_uid, int sender_uid, const std::string& sender_name, int reply_to_message_id, const std::string& reply_sender_name, const std::string& reply_content, const std::string& timestamp){
    chat_proto::Envelope msg;
    build_chat_envelope(msg, type, message, message_id, group_uid, sender_uid, sender_name,
                        reply_to_message_id, reply_sender_name, reply_content, timestamp);
    if (auto transport = transport_.lock()) {
        transport->send_packet(msg);
    }
}

// 直接发送一个完整 Envelope（用于 history_response 这类非 {type,content} 结构的消息）
void client_session::package_envelope(const chat_proto::Envelope& message){
    if (auto transport = transport_.lock()) {
        transport->send_packet(message);
    }
}

// 发送已序列化的 protobuf（供 NoticeService 广播复用同一份 payload，跳过重复序列化）
void client_session::send_serialized_packet(const std::string& payload){
    if (auto transport = transport_.lock()) {
        transport->send_serialized(payload);
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

    // 通知在线且会收到该消息的人（群聊删除时携带 group_UID）
    if (msg.is_group) {
        auto members = repo_hub_->groups()->get_group_members(msg.receiver_UID);
        for (int uid : members) {
            if (uid == current_account_->getUID()) continue; // 跳过删除者自己
            NoticeService::get_instance().send_to_user_with_id(uid, "", "delete_message", message_id, msg.receiver_UID);
        }
    } else {
        NoticeService::get_instance().send_to_user_with_id(msg.receiver_UID, "", "delete_message", message_id);
    }
}

// 聊天历史：message.id 作游标分页。登录后首屏不传 before_id（<=0 取最新一页），
// 前端上滑加载更多时把当前最旧一条的 message_id 作为 before_id 传回。
void client_session::chat_history(int peer_id, int before_id){
    if (!current_account_) {
        package_message("You must be logged in to view chat history.\n", "system");
        return;
    }
    const int self_uid = current_account_->getUID();
    // 内存群列表判断 peer 是群还是用户（UID 与群 UID 共用编号空间，靠 is_group 区分 type）
    const bool is_group = social_manager_ && social_manager_->has_group(peer_id);

    constexpr int kPageSize = 10;   // 每页 10 条
    std::vector<message> page;
    bool has_more = false;
    if (!repo_hub_->messages()->get_history_page(self_uid, peer_id, is_group,
                                                 before_id, kPageSize, page, has_more)) {
        package_message("Failed to load chat history.\n", "system");
        return;
    }

    chat_proto::Envelope resp;
    resp.set_type("history_response");
    resp.set_peer_id(peer_id);
    resp.set_has_more(has_more);
    for (const auto& m : page) {
        auto* item = resp.add_messages();
        item->set_message_id(m.message_id);
        item->set_sender_uid(m.sender_UID);
        item->set_sender_name(m.sender_name);
        item->set_is_group(m.is_group);
        item->set_content(m.content);
        item->set_timestamp(m.timestamp);
        if (m.reply_to_message_id > 0) {
            item->set_reply_to_message_id(m.reply_to_message_id);
            auto* reply = item->mutable_reply_to();
            reply->set_message_id(m.reply_to_message_id);
            reply->set_sender_name(m.reply_sender_name);
            reply->set_content(m.reply_content);
        }
    }
    package_envelope(resp);
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
        std::string type = m.is_group ? "Group_Chat" : "private_chat";
        // 群聊离线消息携带 group_UID，便于客户端归类
        int group_uid = m.is_group ? m.receiver_UID : 0;
        package_chat_message(m.content, type, m.message_id, group_uid, m.sender_UID, sender_name,
                             m.reply_to_message_id, m.reply_sender_name, m.reply_content, m.timestamp);
    }
}

// 查“被回复的原消息”摘要（原消息可能不在当前分页里，必须单独按 id 查）。
// 成功填充 sender_name/content 并返回 true。
bool client_session::load_reply_summary(int reply_to_message_id, std::string& sender_name, std::string& content){
    sender_name.clear();
    content.clear();
    if (reply_to_message_id <= 0) return false;
    message original;
    if (!repo_hub_->messages()->get_message(reply_to_message_id, original)) return false;
    auto acc = repo_hub_->accounts()->load_account(original.sender_UID);
    sender_name = acc ? acc->getName() : std::to_string(original.sender_UID);
    content = original.content;
    return true;
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
