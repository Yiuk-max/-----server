#include "message_handler.h"

#include "client_session.h"
#include "group_manager.h"
#include "logic_common.h"
#include "notice_service.h"
#include "repository_hub.h"
#include "session_manager.h"

#include <ctime>

namespace {

void group_chat(client_session& s, int target_uid, const std::string& message, int reply_to_message_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to send group messages.\n", "system");
        return;
    }
    // 校验群存在
    auto grp = group_manager::get_instance().get(target_uid);
    if (!grp) {
        s.package_message("Group [" + std::to_string(target_uid) + "] does not exist.\n", "system");
        return;
    }
    // 先存储消息（含 reply_to_message_id），取回数据库分配的 message_id 与发送时间
    std::string msg_timestamp;
    int msg_id = s.repo_hub()->messages()->store_message(acc->getUID(), target_uid, message, "group", reply_to_message_id, &msg_timestamp);
    // 回复的原消息摘要（可能不在当前分页里，单独按 id 查）
    std::string reply_name, reply_content;
    if (reply_to_message_id > 0) logic::load_reply_summary(s, reply_to_message_id, reply_name, reply_content);
    // 从数据库拉取群成员列表，经全局通知服务广播（自动忽略不在线成员），并携带 message_id 与发送时间
    auto members = s.repo_hub()->groups()->get_group_members(target_uid);
    if (msg_id > 0) {
        NoticeService::get_instance().send_to_users_with_id(members, message, "Group_Chat", msg_id, target_uid,
                                                            acc->getUID(), acc->getName(),
                                                            reply_to_message_id, reply_name, reply_content, msg_timestamp);
        // 告知发送者消息 id，便于 3 分钟内删除
        s.package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        NoticeService::get_instance().send_to_users(members, message, "Group_Chat");
    }
}

void private_chat(client_session& s, int target_uid, const std::string& message, int reply_to_message_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to send private messages.\n", "system");
        return;
    }
    if (message.empty()) {
        s.package_message("Message cannot be empty.\n", "system");
        return;
    }

    // 1. 检查目标账号是否存在（不要求在线，离线也允许发送并落库）
    if (!logic::target_uid_exists(s, target_uid)) return;

    // 2. 检查目标用户是否是自己的好友
    if (!s.repo_hub()->friends()->is_friend(acc->getUID(), target_uid)) {
        s.package_message("UID [" + std::to_string(target_uid) + "] is not your friend. You can only send private messages to friends.\n", "system");
        return;
    }

    // 3. 先落库（无论对方是否在线；离线消息由对方上线时离线拉取）
    std::string msg_timestamp;
    int msg_id = s.repo_hub()->messages()->store_message(acc->getUID(), target_uid, message, "private", reply_to_message_id, &msg_timestamp);
    // 回复的原消息摘要（可能不在当前分页里，单独按 id 查）
    std::string reply_name, reply_content;
    if (reply_to_message_id > 0) logic::load_reply_summary(s, reply_to_message_id, reply_name, reply_content);

    // 4. 对方在线则实时转发，否则留在 DB 等其上线离线拉取
    auto target_session = session_manager::get_instance().find_session(target_uid);
    if (target_session) {
        if (msg_id > 0) {
            target_session->package_chat_message(message, "private_chat", msg_id, 0,
                                                 acc->getUID(), acc->getName(),
                                                 reply_to_message_id, reply_name, reply_content, msg_timestamp);
        } else {
            target_session->package_message(message, "private_chat");
        }
    }

    // 5. 回执给发送者（含 message_id，便于 3 分钟内删除）
    if (msg_id > 0) {
        s.package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        s.package_message("Failed to store message (database unavailable).\n", "system");
    }
}

void delete_message(client_session& s, int message_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to delete messages.\n", "system");
        return;
    }
    message msg;
    if (!s.repo_hub()->messages()->get_message(message_id, msg)) {
        s.package_message("Message [" + std::to_string(message_id) + "] does not exist.\n", "system");
        return;
    }
    if (msg.sender_UID != acc->getUID()) {
        s.package_message("You can only delete your own messages.\n", "system");
        return;
    }
    std::time_t send_t = logic::parse_datetime(msg.timestamp);
    std::time_t now = std::time(nullptr);
    if (send_t == 0 || now - send_t > 180) {
        s.package_message("Messages can only be deleted within 3 minutes of sending.\n", "system");
        return;
    }
    if (!s.repo_hub()->messages()->delete_message(message_id)) {
        s.package_message("Failed to delete message [" + std::to_string(message_id) + "].\n", "system");
        return;
    }
    s.package_message("Message [" + std::to_string(message_id) + "] deleted.\n", "system");

    // 通知在线且会收到该消息的人（群聊/频道删除时携带 group_UID/channel_id）
    if (msg.type == "group") {
        auto members = s.repo_hub()->groups()->get_group_members(msg.receiver_UID);
        for (int uid : members) {
            if (uid == acc->getUID()) continue; // 跳过删除者自己
            NoticeService::get_instance().send_to_user_with_id(uid, "", "delete_message", message_id, msg.receiver_UID);
        }
    } else if (msg.type == "channel") {
        auto members = s.repo_hub()->communities()->get_channel_members(msg.receiver_UID);
        for (int uid : members) {
            if (uid == acc->getUID()) continue;
            NoticeService::get_instance().send_to_user_with_id(uid, "", "delete_message", message_id, msg.receiver_UID);
        }
    } else {
        NoticeService::get_instance().send_to_user_with_id(msg.receiver_UID, "", "delete_message", message_id);
    }
}

// 聊天历史：message.id 作游标分页。登录后首屏不传 before_id（<=0 取最新一页），
// 前端上滑加载更多时把当前最旧一条的 message_id 作为 before_id 传回。
void chat_history(client_session& s, int peer_id, int before_id, bool is_channel) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view chat history.\n", "system");
        return;
    }
    const int self_uid = acc->getUID();
    // is_channel 由协议明确标记；否则靠内存群列表判断是群还是用户
    auto social = s.social_manager();
    const std::string type = is_channel ? "channel" : (social && social->has_group(peer_id) ? "group" : "private");

    constexpr int kPageSize = 10;   // 每页 10 条
    std::vector<message> page;
    bool has_more = false;
    if (!s.repo_hub()->messages()->get_history_page(self_uid, peer_id, type,
                                                    before_id, kPageSize, page, has_more)) {
        s.package_message("Failed to load chat history.\n", "system");
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
        item->set_is_group(m.is_group());
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
    s.package_envelope(resp);
}

void send_friend_request(client_session& s, const std::string& email, std::string apply_message) {
    if (auto social = s.social_manager()) {
        social->send_friend_request(email, apply_message);
    }
}

// 给好友设置备注名：先校验目标确实是自己的好友，再落库（friend_relation.remark_name）
void set_friend_remark(client_session& s, int friend_uid, std::string remark) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to set a friend remark.\n", "system");
        return;
    }
    if (remark.empty()) {
        s.package_message("Remark cannot be empty.\n", "system");
        return;
    }
    int my_uid = acc->getUID();
    if (!s.repo_hub()->friends()->is_friend(my_uid, friend_uid)) {
        s.package_message("UID [" + std::to_string(friend_uid) + "] is not your friend.\n", "system");
        return;
    }
    if (s.repo_hub()->friends()->set_remark(my_uid, friend_uid, remark)) {
        s.package_message("Remark for UID [" + std::to_string(friend_uid) + "] updated successfully.\n", "system");
    } else {
        s.package_message("Failed to update remark for UID [" + std::to_string(friend_uid) + "].\n", "system");
    }
}

// 处理好友申请（同意/拒绝）
void handle_friend_request(client_session& s, int sender_uid, bool accept) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to handle friend requests.\n", "system");
        return;
    }
    auto social = s.social_manager();
    if (!social) return;

    bool handled = social->handle_friend_request(sender_uid, accept);
    // 同意后双方都应在好友列表看到对方：同步更新申请人（若在线）的内存好友列表
    if (handled && accept) {
        auto sender_session = session_manager::get_instance().find_session(sender_uid);
        if (sender_session) {
            sender_session->add_friend_to_list(acc->getUID());
        }
    }
}

// 删除好友
void remove_friend(client_session& s, int friend_uid) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to remove a friend.\n", "system");
        return;
    }
    auto social = s.social_manager();
    if (!social) return;

    bool removed = social->remove_friend(friend_uid);
    // 被删除方（若在线）也应同步更新内存好友列表，避免其仍看到已删除的好友
    if (removed) {
        auto target_session = session_manager::get_instance().find_session(friend_uid);
        if (target_session) {
            target_session->remove_friend_from_list(acc->getUID());
        }
    }
}

} // namespace

void Chat_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string& file_data) {
    const std::string type = message.type();
    if (type == "private_chat") {
        private_chat(session, message.target_uid(), message.message(), message.reply_to_message_id());
        return;
    } else if (type == "group_chat") {
        group_chat(session, message.target_uid(), message.message(), message.reply_to_message_id());
        return;
    } else if (type == "delete_message") {
        delete_message(session, message.message_id());
        return;
    } else if (type == "history_request") {
        // 游标分页拉聊天历史：peer_id = 对方/群/频道 ID；is_channel=true 表示频道
        chat_history(session, message.peer_id(), message.before_id(), message.is_channel());
        return;
    } else if (type == "add_friend") {
        // 按邮箱定位要添加的好友：email 缺失/未绑定时由 social_module 给出系统提示
        send_friend_request(session, message.email(), message.apply_message());
        return;
    } else if (type == "set_friend_remark") {
        set_friend_remark(session, message.friend_uid(), message.remark());
        return;
    } else if (type == "accept_friend") {
        handle_friend_request(session, message.sender_uid(), true);
        return;
    } else if (type == "reject_friend") {
        handle_friend_request(session, message.sender_uid(), false);
        return;
    } else if (type == "remove_friend") {
        remove_friend(session, message.friend_uid());
        return;
    } else if (type == "show_friend_requests") {
        logic::show_friend_requests(session);
        return;
    }
}

