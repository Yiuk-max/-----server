#include "notice_service.h"
#include "session_manager.h"

NoticeService& NoticeService::get_instance() {
    static NoticeService instance;
    return instance;
}

void NoticeService::send_to_user(int uid, const std::string& message, const std::string& type) {
    auto target = session_manager::get_instance().find_session(uid);
    if (target) {
        target->package_message(message, type);
    }
}

void NoticeService::send_to_users(const std::vector<int>& uid_list, const std::string& message, const std::string& type) {
    // 广播：Envelope 构造 + 序列化各做一次，再批量查在线会话逐个发送，
    // 避免 N 次 protobuf 序列化与 N 次 find_session 加锁。
    chat_proto::Envelope env;
    env.set_type(type);
    env.set_content(message);
    std::string payload;
    if (!env.SerializeToString(&payload)) {
        return;
    }
    auto sessions = session_manager::get_instance().find_sessions(uid_list);
    for (const auto& s : sessions) {
        if (s) {
            s->send_serialized_packet(payload);
        }
    }
}

void NoticeService::send_to_user_with_id(int uid, const std::string& message, const std::string& type, int message_id, int group_uid, int sender_uid, const std::string& sender_name, int reply_to_message_id, const std::string& reply_sender_name, const std::string& reply_content, const std::string& timestamp) {
    auto target = session_manager::get_instance().find_session(uid);
    if (target) {
        target->package_chat_message(message, type, message_id, group_uid, sender_uid, sender_name,
                                     reply_to_message_id, reply_sender_name, reply_content, timestamp);
    }
}

void NoticeService::send_to_users_with_id(const std::vector<int>& uid_list, const std::string& message, const std::string& type, int message_id, int group_uid, int sender_uid, const std::string& sender_name, int reply_to_message_id, const std::string& reply_sender_name, const std::string& reply_content, const std::string& timestamp) {
    // 广播：Envelope 构造 + 序列化各做一次，再批量查在线会话逐个发送，
    // 避免 N 次 protobuf 序列化与 N 次 find_session 加锁。
    chat_proto::Envelope env;
    build_chat_envelope(env, type, message, message_id, group_uid, sender_uid, sender_name,
                        reply_to_message_id, reply_sender_name, reply_content, timestamp);
    std::string payload;
    if (!env.SerializeToString(&payload)) {
        return;
    }
    auto sessions = session_manager::get_instance().find_sessions(uid_list);
    for (const auto& s : sessions) {
        if (s) {
            s->send_serialized_packet(payload);
        }
    }
}

bool NoticeService::is_online(int uid) {
    return session_manager::get_instance().find_session(uid) != nullptr;
}
