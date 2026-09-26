#include "message_handler.h"

#include "client_session.h"
#include "logic_common.h"
#include "notice_service.h"
#include "repository_hub.h"
#include "session_manager.h"

#include <tuple>

namespace {

// ==================== 社区 ====================

void create_community(client_session& s, const std::string& name, const std::string& description) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to create a community.\n", "system");
        return;
    }
    if (name.empty()) {
        s.package_message("Community name cannot be empty.\n", "system");
        return;
    }
    int community_id = s.repo_hub()->communities()->create_community(acc->getUID(), name, description);
    if (community_id < 0) {
        s.package_message("Failed to create community (database unavailable).\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        social->add_community_to_list(community_id);
    }
    s.package_message("Community created successfully. Community ID: " + std::to_string(community_id) + ".\n", "system");
}

void delete_community(client_session& s, int community_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to delete a community.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->delete_community(community_id, acc->getUID())) {
        s.package_message("Failed to delete community (only owner can delete).\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        social->remove_community_from_list(community_id);
        social->reload_community_state(); // 该社区下的频道已级联删除，重新拉取频道列表
    }
    s.package_message("Community deleted successfully.\n", "system");
}

void modify_community(client_session& s, int community_id, const std::string& name,
                      const std::string& description, int avatar_id, int banner_id) {//banner是横幅，就是社区背景的图片的数据库id
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to modify a community.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->modify_community(community_id, acc->getUID(),
                                                       name, description, avatar_id, banner_id)) {
        s.package_message("Failed to modify community (only owner can modify).\n", "system");
        return;
    }
    s.package_message("Community updated successfully.\n", "system");
}

void show_communities(client_session& s) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view communities.\n", "system");
        return;
    }
    std::vector<community_info> list;
    if (!s.repo_hub()->communities()->get_user_communities_info(acc->getUID(), list)) {
        s.package_message("Failed to load communities.\n", "system");
        return;
    }
    chat_proto::Envelope resp;
    resp.set_type("community_list_response");
    for (const auto& c : list) {
        auto* item = resp.add_communities();
        item->set_community_id(c.community_id);
        item->set_name(c.name);
        item->set_description(c.description);
        item->set_avatar(c.avatar);
        item->set_category(c.category);
        item->set_my_role(c.my_role);
    }
    s.package_envelope(resp);
}

// ==================== 频道 ====================

void show_community_channels(client_session& s, int community_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view channels.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->is_community_member(community_id, acc->getUID())) {
        s.package_message("You are not a member of this community.\n", "system");
        return;
    }
    std::vector<channel_info> list;
    if (!s.repo_hub()->communities()->get_community_channels(community_id, list)) {
        s.package_message("Failed to load channels.\n", "system");
        return;
    }
    chat_proto::Envelope resp;
    resp.set_type("channel_list_response");
    for (const auto& c : list) {
        auto* item = resp.add_channels();
        item->set_channel_id(c.channel_id);
        item->set_community_id(c.community_id);
        item->set_name(c.name);
        item->set_category(c.category);
    }
    s.package_envelope(resp);
}

void show_community_members(client_session& s, int community_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view members.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->is_community_member(community_id, acc->getUID())) {
        s.package_message("You are not a member of this community.\n", "system");
        return;
    }
    std::vector<community_member_info> list;
    if (!s.repo_hub()->communities()->get_community_members(community_id, list)) {
        s.package_message("Failed to load members.\n", "system");
        return;
    }
    chat_proto::Envelope resp;
    resp.set_type("community_member_list_response");
    for (const auto& m : list) {
        auto* item = resp.add_members();
        item->set_user_uid(m.user_uid);
        item->set_nickname(m.nickname);
        item->set_role(m.role);
        auto macc = s.repo_hub()->accounts()->load_account(m.user_uid);
        item->set_avatar_id(macc ? macc->get_avatar_id() : -1);// 头像 file.id（-1 未设置）
    }
    s.package_envelope(resp);
}

void create_channel(client_session& s, int community_id, const std::string& name,
                    const std::string& category) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to create a channel.\n", "system");
        return;
    }
    if (name.empty()) {
        s.package_message("Channel name cannot be empty.\n", "system");
        return;
    }
    int channel_id = s.repo_hub()->communities()->create_channel(community_id, acc->getUID(),
                                                                 name, category);
    if (channel_id < 0) {
        s.package_message("Failed to create channel (not a member / community not exists).\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        social->add_channel_to_list(channel_id);
    }
    s.package_message("Channel created successfully. Channel ID: " + std::to_string(channel_id) + ".\n", "system");
}

void delete_channel(client_session& s, int community_id, int channel_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to delete a channel.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->delete_channel(community_id, channel_id, acc->getUID())) {
        s.package_message("Failed to delete channel (only owner can delete).\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        social->remove_channel_from_list(channel_id);
    }
    s.package_message("Channel deleted successfully.\n", "system");
}

void modify_channel(client_session& s, int community_id, int channel_id,
                    const std::string& name, const std::string& category) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to modify a channel.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->modify_channel(community_id, channel_id, acc->getUID(),
                                                     name, category)) {
        s.package_message("Failed to modify channel (only owner can modify).\n", "system");
        return;
    }
    s.package_message("Channel updated successfully.\n", "system");
}

void channel_chat(client_session& s, int channel_id, const std::string& message,
                  int reply_to_message_id, bool is_file, int file_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to send channel messages.\n", "system");
        return;
    }
    if (!is_file && message.empty()) {
        s.package_message("Message cannot be empty.\n", "system");
        return;
    }
    // 1. 解析频道所属社区（同时校验频道存在）
    int community_id = -1;
    if (!s.repo_hub()->communities()->get_channel_community(channel_id, community_id)) {
        s.package_message("Channel [" + std::to_string(channel_id) + "] does not exist.\n", "system");
        return;
    }
    // 2. 校验发送者是频道所属社区的成员（而不是频道成员）
    if (!s.repo_hub()->communities()->is_community_member(community_id, acc->getUID())) {
        s.package_message("You are not a member of this community.\n", "system");
        return;
    }
    // 3. 落库（type='channel'，receiver_UID=channel_id）
    std::string msg_timestamp;
    int msg_id = s.repo_hub()->messages()->store_message(acc->getUID(), channel_id, message,
                                                         "channel", reply_to_message_id, &msg_timestamp,
                                                         is_file, file_id);
    std::string reply_name, reply_content;
    if (reply_to_message_id > 0) {
        logic::load_reply_summary(s, reply_to_message_id, reply_name, reply_content);
    }
    std::string file_name; uint64_t file_size = 0;
    if (is_file && file_id > 0) {
        file_meta_info f;
        if (s.repo_hub()->files()->get_file(file_id, f)) { file_name = f.original_name; file_size = f.size; }
    }
    // 4. 取社区成员（频道共享社区成员）广播
    auto members = s.repo_hub()->communities()->get_channel_members(channel_id);
    if (msg_id > 0) {
        ChatMessageData data;
        data.type        = "Channel_Chat";
        data.message     = message;
        data.message_id  = msg_id;
        data.group_uid   = channel_id;
        data.sender_uid  = acc->getUID();
        data.sender_name = acc->getName();
        data.timestamp   = msg_timestamp;
        if (reply_to_message_id > 0) data.reply = ReplyInfo{reply_to_message_id, reply_name, reply_content};
        if (is_file && file_id > 0)  data.file = FileInfo{file_id, file_name, file_size};
        NoticeService::get_instance().send_to_users_with_id(members, data);
        s.package_message("Message sent. Message ID: " + std::to_string(msg_id) + ".\n", "system");
    } else {
        NoticeService::get_instance().send_to_users(members, message, "Channel_Chat");
    }
}

// ==================== 成员 ====================

void join_community(client_session& s, int community_id, const std::string& apply_message) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to join a community.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->send_join_request(community_id, acc->getUID(), apply_message)) {
        s.package_message("Failed to send join request (community not exists / already a member / duplicate request).\n", "system");
        return;
    }
    s.package_message("Join request sent to community [" + std::to_string(community_id) + "].\n", "system");

    community_info info;
    if (s.repo_hub()->communities()->get_community_info(community_id, info)) {
        auto owner_session = session_manager::get_instance().find_session(info.owner_uid);
        if (owner_session) {
            owner_session->package_message(
                "User [" + std::to_string(acc->getUID()) + "] has requested to join your community [" +
                std::to_string(community_id) + "]. Please handle the request.\n", "system");
        }
    }
}

void handle_community_join_request(client_session& s, int community_id, int requester_uid, bool accept) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to handle join requests.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->handle_join_request(community_id, acc->getUID(), requester_uid, accept)) {
        s.package_message("Failed to handle join request (you are not owner or no pending request).\n", "system");
        return;
    }
    s.package_message(accept ? "Join request accepted.\n" : "Join request rejected.\n", "system");

    auto requester_session = session_manager::get_instance().find_session(requester_uid);
    if (requester_session) {
        if (accept) {
            requester_session->reload_community_state();
            requester_session->package_message(
                "You have been added to community [" + std::to_string(community_id) + "].\n", "system");
        } else {
            requester_session->package_message(
                "Your join request for community [" + std::to_string(community_id) + "] was rejected.\n", "system");
        }
    }
}

void community_add_member(client_session& s, int community_id, int target_uid) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to add a member.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->add_member(community_id, acc->getUID(), target_uid)) {
        s.package_message("Failed to add member (only owner can add, or already a member).\n", "system");
        return;
    }
    s.package_message("Member [" + std::to_string(target_uid) + "] added to community [" +
                      std::to_string(community_id) + "].\n", "system");

    auto target_session = session_manager::get_instance().find_session(target_uid);
    if (target_session) {
        target_session->reload_community_state();
        target_session->package_message(
            "You have been added to community [" + std::to_string(community_id) + "].\n", "system");
    }
}

void community_remove_member(client_session& s, int community_id, int target_uid) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to remove a member.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->remove_member(community_id, acc->getUID(), target_uid)) {
        s.package_message("Failed to remove member (only owner can remove, or member not in community).\n", "system");
        return;
    }
    s.package_message("Member [" + std::to_string(target_uid) + "] removed from community [" +
                      std::to_string(community_id) + "].\n", "system");

    auto target_session = session_manager::get_instance().find_session(target_uid);
    if (target_session) {
        target_session->reload_community_state();
        target_session->package_message(
            "You have been removed from community [" + std::to_string(community_id) + "].\n", "system");
    }
}

void leave_community(client_session& s, int community_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to leave a community.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->leave(community_id, acc->getUID())) {
        s.package_message("Failed to leave community (owner cannot leave).\n", "system");
        return;
    }
    if (auto social = s.social_manager()) {
        social->reload_community_state();
    }
    s.package_message("You have left community [" + std::to_string(community_id) + "].\n", "system");
}

void show_community_requests(client_session& s, int community_id) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view community join requests.\n", "system");
        return;
    }
    std::vector<std::tuple<int, std::string>> requests;
    if (!s.repo_hub()->communities()->show_requests(community_id, acc->getUID(), requests)) {
        s.package_message("Failed to retrieve community join requests.\n", "system");
        return;
    }
    if (requests.empty()) {
        s.package_message("No pending join requests for community [" + std::to_string(community_id) + "].\n", "system");
        return;
    }
    std::string result = "Pending join requests for community [" + std::to_string(community_id) + "]:\n";
    for (const auto& req : requests) {
        int requester_uid;
        std::string apply_message;
        std::tie(requester_uid, apply_message) = req;
        result += "Requester UID: " + std::to_string(requester_uid) + ", Message: " + apply_message + "\n";
    }
    s.package_message(result, "system");
}

void modify_community_member_role(client_session& s, int community_id, int target_uid, bool promote) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to modify member role.\n", "system");
        return;
    }
    if (!s.repo_hub()->communities()->modify_member_role(community_id, acc->getUID(), target_uid, promote)) {
        s.package_message("Failed to modify member role (only owner can promote/demote).\n", "system");
        return;
    }
    s.package_message("Role of UID [" + std::to_string(target_uid) + "] updated successfully.\n", "system");
}

} // namespace

void Community_handler::handle_message(const chat_proto::Envelope& message, client_session& session,
                                       std::string& file_data) {
    const std::string type = message.type();
    if (type == "create_community") {
        create_community(session, message.group_name(), message.description());
    } else if (type == "delete_community") {
        delete_community(session, message.community_id());
    } else if (type == "modify_community") {
        modify_community(session, message.community_id(), message.group_name(),
                         message.description(), message.community_avatar_id(),
                         message.community_banner_id());
    } else if (type == "show_communities") {
        show_communities(session);
    } else if (type == "show_community_channels") {
        show_community_channels(session, message.community_id());
    } else if (type == "show_community_members") {
        show_community_members(session, message.community_id());
    } else if (type == "create_channel") {
        create_channel(session, message.community_id(), message.group_name(), message.category());
    } else if (type == "delete_channel") {
        delete_channel(session, message.community_id(), message.channel_id());
    } else if (type == "modify_channel") {
        modify_channel(session, message.community_id(), message.channel_id(),
                       message.group_name(), message.category());
    } else if (type == "channel_chat") {
        int file_id = 0;
        if (message.is_file() && !message.file_id().empty()) {
            try { file_id = std::stoi(message.file_id()); } catch (...) {}
        }
        channel_chat(session, message.target_uid(), message.message(), message.reply_to_message_id(),
                     message.is_file(), file_id);
    } else if (type == "join_community") {
        join_community(session, message.community_id(), message.apply_message());
    } else if (type == "handle_community_join_request") {
        handle_community_join_request(session, message.community_id(), message.requester_uid(), message.accept());
    } else if (type == "community_add_member") {
        community_add_member(session, message.community_id(), message.target_user_uid());
    } else if (type == "community_remove_member") {
        community_remove_member(session, message.community_id(), message.target_user_uid());
    } else if (type == "leave_community") {
        leave_community(session, message.community_id());
    } else if (type == "show_community_requests") {
        show_community_requests(session, message.community_id());
    } else if (type == "modify_community_member_role") {
        modify_community_member_role(session, message.community_id(), message.target_user_uid(), message.promote());
    }
}
