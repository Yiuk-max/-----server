#include "message_handler.h"

#include "client_session.h"
#include "group_manager.h"
#include "logic_common.h"
#include "repository_hub.h"
#include "session_manager.h"

#include <tuple>

namespace {

void create_group(client_session& s, std::string group_name) {
    if (group_name.empty()) {
        s.package_message("The group name can't be empty", "system");
        return;
    }
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to create a group.\n", "system");
        return;
    }
    auto social = s.social_manager();
    if (!social) return;

    int group_uid = -1;
    social->create_friend_group(group_name, group_uid);
    if (group_uid >= 0) {
        s.package_message("Group created successfully. Group name: [" + group_name + "], Group UID: " + std::to_string(group_uid) + ".\n", "system");
    } else {
        s.package_message("Failed to create group (group name may already exist or DB unavailable).\n", "system");
    }
}

void group_add_client(client_session& s, int target_group_uid, int target_user_uid) {
    if (!logic::target_uid_exists(s, target_group_uid) || !logic::target_uid_exists(s, target_user_uid)) {
        return;
    }
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to add a group member.\n", "system");
        return;
    }

    if (!s.repo_hub()->groups()->member_add_group(target_group_uid, acc->getUID(), target_user_uid)) {
        s.package_message("Failed to add member (you are not in the group or member already exists).\n", "system");
        return;
    }
    s.package_message("Member [" + std::to_string(target_user_uid) + "] added to group [" + std::to_string(target_group_uid) + "] successfully.\n", "system");

    // 被拉入的成员若在线，同步其内存群列表并通知已入群
    auto new_member_session = session_manager::get_instance().find_session(target_user_uid);
    if (new_member_session) {
        new_member_session->add_group_to_list(target_group_uid);
        new_member_session->package_message("You have been added to group [" + std::to_string(target_group_uid) + "].\n", "system");
    }
}

void group_delete_client(client_session& s, int target_group_uid, int target_user_uid) {
    if (!logic::target_uid_exists(s, target_group_uid) || !logic::target_uid_exists(s, target_user_uid)) {
        return;
    }
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to remove a group member.\n", "system");
        return;
    }

    if (!s.repo_hub()->groups()->remove_group_member(target_group_uid, acc->getUID(), target_user_uid)) {
        s.package_message("Failed to remove member (only owner can kick, or member not in group).\n", "system");
        return;
    }
    s.package_message("Member [" + std::to_string(target_user_uid) + "] removed from group [" + std::to_string(target_group_uid) + "] successfully.\n", "system");

    // 被踢成员若在线，同步其内存群列表并通知
    auto target_session = session_manager::get_instance().find_session(target_user_uid);
    if (target_session) {
        target_session->remove_group_from_list(target_group_uid);
        target_session->package_message("You have been removed from group [" + std::to_string(target_group_uid) + "].\n", "system");
    }
}

void delete_group(client_session& s, int group_uid) {
    if (!logic::target_uid_exists(s, group_uid)) {
        return;
    }
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to delete a group.\n", "system");
        return;
    }

    if (!s.repo_hub()->groups()->delete_group(group_uid, acc->getUID())) {
        s.package_message("Failed to delete group (only owner can delete).\n", "system");
        return;
    }
    // 同步清理本会话群列表
    if (auto social = s.social_manager()) {
        social->exit_friend_group(group_uid);
    }
    // 群已被删除，从内存管理器中强制移除（不论是否还有人持有）
    group_manager::get_instance().remove_group(group_uid);
    s.package_message("Group deleted successfully.\n", "system");
}

void modify_group_name(client_session& s, int group_uid, std::string new_name) {
    if (!logic::target_uid_exists(s, group_uid)) {
        return;
    }
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to modify a group.\n", "system");
        return;
    }

    if (!s.repo_hub()->groups()->modify_group_name(group_uid, acc->getUID(), new_name)) {
        s.package_message("Failed to modify group name (only owner can modify).\n", "system");
        return;
    }
    s.package_message("Group name updated successfully. New name: [" + new_name + "].\n", "system");
}

// 申请加入群聊：落库申请（relation_apply, apply_type=2），等待群主处理
void send_join_group(client_session& s, int group_uid) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to join a group.\n", "system");
        return;
    }
    if (!s.repo_hub()->groups()->send_join_group(group_uid, acc->getUID())) {
        s.package_message("Failed to send join request (group not exists / already a member / duplicate request).\n", "system");
        return;
    }
    s.package_message("Join request sent to group [" + std::to_string(group_uid) + "] successfully.\n", "system");

    // 通知群主/管理员（若在线）提醒处理入群申请（get 内部已做内存优先 + DB 兜底）
    auto grp = group_manager::get_instance().get(group_uid);
    if (grp) {
        int owner_uid = grp->get_manager_UID();
        auto owner_session = session_manager::get_instance().find_session(owner_uid);
        if (owner_session) {
            std::string notice = "User [" + std::to_string(acc->getUID()) + "] has requested to join your group [" + std::to_string(group_uid) + "]. Please handle the request.\n";
            owner_session->package_message(notice, "system");
        }
    }
}

// 处理入群申请：requester_UID 为申请人，本会话为群主；同意则拉人入群
void handle_join_request(client_session& s, int group_uid, int requester_uid, bool accept) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to handle join requests.\n", "system");
        return;
    }
    if (!s.repo_hub()->groups()->handle_join_request(group_uid, acc->getUID(), requester_uid, accept)) {
        s.package_message("Failed to handle join request (you are not owner or no pending request).\n", "system");
        return;
    }
    s.package_message(accept ? "Join request accepted.\n" : "Join request rejected.\n", "system");

    // 同步申请人（若在线）：同意则更新其内存群列表并通知已入群；拒绝则通知被拒
    auto requester_session = session_manager::get_instance().find_session(requester_uid);
    if (requester_session) {
        if (accept) {
            requester_session->add_group_to_list(group_uid);
            requester_session->package_message("You have been added to group [" + std::to_string(group_uid) + "].\n", "system");
        } else {
            requester_session->package_message("Your join request for group [" + std::to_string(group_uid) + "] was rejected.\n", "system");
        }
    }
}

// 修改群成员身份：promote=true 提升为群主 / false 降回普通成员
void modify_member_role(client_session& s, int group_uid, int target_uid, bool promote) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to modify member role.\n", "system");
        return;
    }
    if (!s.repo_hub()->groups()->modify_member_role(group_uid, acc->getUID(), target_uid, promote)) {
        s.package_message("Failed to modify member role (only owner can promote/demote).\n", "system");
        return;
    }
    s.package_message("Role of UID [" + std::to_string(target_uid) + "] updated successfully.\n", "system");
}

// 查看群聊待处理的入群申请：仅群主可查看
void show_group_requests(client_session& s, int group_uid) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to view group join requests.\n", "system");
        return;
    }
    std::vector<std::tuple<int, std::string>> requests;
    if (!s.repo_hub()->groups()->show_group_requests(group_uid, acc->getUID(), requests)) {
        s.package_message("Failed to retrieve group join requests.\n", "system");
        return;
    }
    if (requests.empty()) {
        s.package_message("No pending join requests for group [" + std::to_string(group_uid) + "].\n", "system");
        return;
    }
    std::string result = "Pending join requests for group [" + std::to_string(group_uid) + "]:\n";
    for (const auto& req : requests) {
        int requester_uid;
        std::string apply_message;
        std::tie(requester_uid, apply_message) = req;
        result += "Requester UID: " + std::to_string(requester_uid) + ", Message: " + apply_message + "\n";
    }
    s.package_message(result, "system");
}

// 查看群成员（含群内名字）
void show_group_members(client_session& s, int group_uid) {
    if (!logic::target_uid_exists(s, group_uid)) {
        return;
    }
    s.package_message(s.repo_hub()->groups()->show_group_members(group_uid), "system");
}

} // namespace

void Group_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string& file_data) {
    const std::string type = message.type();
    if (type == "create_group") {
        create_group(session, message.group_name());
        return;
    } else if (type == "delete_group") {
        delete_group(session, message.group_uid());
        return;
    } else if (type == "group_add_client") {
        group_add_client(session, message.group_uid(), message.target_user_uid());
        return;
    } else if (type == "group_delete_client") {
        group_delete_client(session, message.group_uid(), message.target_user_uid());
        return;
    } else if (type == "modify_group_name") {
        modify_group_name(session, message.group_uid(), message.new_name());
        return;
    } else if (type == "send_join_group") {
        send_join_group(session, message.group_uid());
        return;
    } else if (type == "handle_join_request") {
        handle_join_request(session, message.group_uid(), message.requester_uid(), message.accept());
        return;
    } else if (type == "modify_member_role") {
        modify_member_role(session, message.group_uid(), message.target_uid(), message.promote());
        return;
    } else if (type == "show_group_requests") {
        show_group_requests(session, message.group_uid());
        return;
    } else if (type == "show_group_members") {
        show_group_members(session, message.group_uid());
        return;
    }
}

