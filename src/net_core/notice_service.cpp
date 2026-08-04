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

bool NoticeService::is_online(int uid) {
    return session_manager::get_instance().find_session(uid) != nullptr;
}
