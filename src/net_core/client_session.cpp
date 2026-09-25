#include "client_session.h"
#include "session_manager.h"

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

bool client_session::activate_session(const std::shared_ptr<account>& account,
                                      std::shared_ptr<social_module> social,
                                      std::shared_ptr<client_session>* old_session){
    if (!old_session) {
        return false;
    }
    old_session->reset();
    auto self = shared_from_this();
    std::shared_ptr<social_module> previous_social;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mtx_);
        // 同一连接直接切换账号时，先按对象身份移除旧 UID，避免残留两个在线映射。
        if (logged_in_uid_ >= 0 && logged_in_uid_ != account->getUID()) {
            session_manager::get_instance().remove_online_if_same(logged_in_uid_, this);
        }
        if (!online) {
            return false; // DB 查询期间连接已经断开
        }
        previous_social = std::move(social_manager_);
        current_account_ = account;
        social_manager_ = std::move(social);
        logged_in_uid_ = account->getUID();
        *old_session = session_manager::get_instance().replace_online(account->getUID(), self);
    }
    previous_social.reset();
    return true;
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
    //社区/频道相关
    handlers_["create_community"]             = std::make_unique<Community_handler>();
    handlers_["delete_community"]             = std::make_unique<Community_handler>();
    handlers_["modify_community"]             = std::make_unique<Community_handler>();
    handlers_["show_communities"]             = std::make_unique<Community_handler>();
    handlers_["show_community_channels"]      = std::make_unique<Community_handler>();
    handlers_["show_community_members"]       = std::make_unique<Community_handler>();
    handlers_["create_channel"]               = std::make_unique<Community_handler>();
    handlers_["delete_channel"]               = std::make_unique<Community_handler>();
    handlers_["modify_channel"]               = std::make_unique<Community_handler>();
    handlers_["channel_chat"]                 = std::make_unique<Community_handler>();
    handlers_["join_community"]               = std::make_unique<Community_handler>();
    handlers_["handle_community_join_request"] = std::make_unique<Community_handler>();
    handlers_["community_add_member"]         = std::make_unique<Community_handler>();
    handlers_["community_remove_member"]      = std::make_unique<Community_handler>();
    handlers_["leave_community"]              = std::make_unique<Community_handler>();
    handlers_["show_community_requests"]      = std::make_unique<Community_handler>();
    handlers_["modify_community_member_role"] = std::make_unique<Community_handler>();
    //文件相关
    handlers_["download_file"]          = std::make_unique<File_handler>();
    handlers_["upload_file"]            = std::make_unique<File_handler>();
}

//===============消息处理===============
// 接收缓冲/切帧已在 connection::process_incoming 完成，
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

//===============生命周期===============
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

//===============跨会话内存列表同步===============
void client_session::add_friend_to_list(int friend_UID){
    if (social_manager_) {
        social_manager_->add_friend_to_list(friend_UID);
    }
}

void client_session::remove_friend_from_list(int friend_UID){
    if (social_manager_) {
        social_manager_->remove_friend_from_list(friend_UID);
    }
}

void client_session::add_group_to_list(int group_UID){
    if (social_manager_) {
        social_manager_->add_group_to_list(group_UID);
    }
}

void client_session::remove_group_from_list(int group_UID){
    if (social_manager_) {
        social_manager_->remove_group_from_list(group_UID);
    }
}

void client_session::add_community_to_list(int community_id){
    if (social_manager_) {
        social_manager_->add_community_to_list(community_id);
    }
}

void client_session::remove_community_from_list(int community_id){
    if (social_manager_) {
        social_manager_->remove_community_from_list(community_id);
    }
}

void client_session::add_channel_to_list(int channel_id){
    if (social_manager_) {
        social_manager_->add_channel_to_list(channel_id);
    }
}

void client_session::remove_channel_from_list(int channel_id){
    if (social_manager_) {
        social_manager_->remove_channel_from_list(channel_id);
    }
}

void client_session::reload_community_state(){
    if (social_manager_) {
        social_manager_->reload_community_state();
    }
}

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

//===============================发送端口================================
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
