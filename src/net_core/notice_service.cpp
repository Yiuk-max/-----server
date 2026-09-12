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
    for (int uid : uid_list) {
        send_to_user(uid, message, type);
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
    for (int uid : uid_list) {
        send_to_user_with_id(uid, message, type, message_id, group_uid, sender_uid, sender_name,
                             reply_to_message_id, reply_sender_name, reply_content, timestamp);
    }
}

bool NoticeService::is_online(int uid) {
    return session_manager::get_instance().find_session(uid) != nullptr;
}
