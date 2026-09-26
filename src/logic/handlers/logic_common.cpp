#include "logic_common.h"

#include "client_session.h"
#include "group_manager.h"
#include "session_manager.h"

#include <cstdio>
#include <random>

namespace logic {

std::time_t parse_datetime(const std::string& s) {
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

std::string generate_token() {
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

bool target_uid_exists(client_session& s, int target_uid) {
    // 校验个人 UID 是否存在
    if (s.repo_hub()->accounts()->load_account(target_uid)) {
        return true;
    }
    // 校验群聊 UID 是否存在（get 内部已做内存优先 + DB 兜底）
    if (group_manager::get_instance().get(target_uid)) {
        return true;
    }
    std::string fail = "UID [" + std::to_string(target_uid) + "] does not exist.\n";
    s.package_message(fail, "system");
    return false;
}

bool target_uid_online(client_session& s, int target_uid) {
    if (session_manager::get_instance().find_session(target_uid)) {
        return true;
    }
    std::string fail = "User [" + std::to_string(target_uid) + "] is not online.\n";
    s.package_message(fail, "system");
    return false;
}

void show_friend_requests(client_session& s) {
    if (!s.current_account()) {
        s.package_message("You must be logged in to view friend requests.\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        s.package_message(social->show_friend_requests(), "system");
    }
}

void send_offline_messages(client_session& s, const std::string& since_time) {
    auto acc = s.current_account();
    if (!acc) return;
    int uid = acc->getUID();
    auto msgs = s.repo_hub()->messages()->get_offline_messages(uid, since_time);
    for (const auto& m : msgs) {
        std::string sender_name = std::to_string(m.sender_UID);
        auto sender = s.repo_hub()->accounts()->load_account(m.sender_UID);
        if (sender) {
            sender_name = sender->getName();
        }
        std::string type = (m.type == "group") ? "Group_Chat" : (m.type == "channel" ? "Channel_Chat" : "private_chat");
        int group_uid = (m.type == "private") ? 0 : m.receiver_UID;

        ChatMessageData data;
        data.type        = type;
        data.message     = m.content;
        data.message_id  = m.message_id;
        data.group_uid   = group_uid;
        data.sender_uid  = m.sender_UID;
        data.sender_name = sender_name;
        data.timestamp   = m.timestamp;
        if (m.reply_to_message_id > 0) {
            data.reply = ReplyInfo{m.reply_to_message_id, m.reply_sender_name, m.reply_content};
        }
        if (m.is_file && m.file_id > 0) {
            data.file = FileInfo{m.file_id, m.file_name, m.file_size};
        }
        s.package_chat_message(data);
    }
}

bool load_reply_summary(client_session& s, int reply_to_message_id,
                        std::string& sender_name, std::string& content) {
    sender_name.clear();
    content.clear();
    if (reply_to_message_id <= 0) return false;
    message original;
    if (!s.repo_hub()->messages()->get_message(reply_to_message_id, original)) return false;
    auto acc = s.repo_hub()->accounts()->load_account(original.sender_UID);
    sender_name = acc ? acc->getName() : std::to_string(original.sender_UID);
    content = original.content;
    return true;
}

} // namespace logic
