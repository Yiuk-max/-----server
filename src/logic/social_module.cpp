#include "social_module.h"
#include "notice_service.h"
#include "group.h"
#include <algorithm>
#include <tuple>

social_module::social_module(int UID, std::shared_ptr<RepositoryHub> hub)
    :user_UID_(UID), repo_hub_(hub) {
    auto acc = repo_hub_->accounts()->load_account(UID);
    if (acc) {
        name_ = acc->getName();
    }
    // 登录时从数据库加载好友列表，之后 show_friends() 直接读内存
    friend_relations = repo_hub_->friends()->get_friend_list(UID);
}

// 建立双向好友关系：事务内双写落库 + 更新本地列表 + 通知对方
void social_module::add_friend(int friend_UID){
    if (!repo_hub_->friends()->add_friend(user_UID_, friend_UID)) {
        NoticeService::get_instance().send_to_user(user_UID_, "Failed to add friend (may already be friends).\n", "system");
        return;
    }
    if (std::find(friend_relations.begin(), friend_relations.end(), friend_UID) == friend_relations.end()) {
        friend_relations.push_back(friend_UID);
    }
    std::string notify = "You are now friends with [" + name_ + "].\n";
    NoticeService::get_instance().send_to_user(friend_UID, notify, "system");
}

// 删除好友：事务内双删落库 + 更新本地列表 + 通知对方（若在线）
void social_module::remove_friend(int friend_UID){
    if (!repo_hub_->friends()->remove_friend(user_UID_, friend_UID)) {
        NoticeService::get_instance().send_to_user(user_UID_, "Failed to remove friend.\n", "system");
        return;
    }
    friend_relations.erase(std::remove(friend_relations.begin(), friend_relations.end(), friend_UID), friend_relations.end());
    if (NoticeService::get_instance().is_online(friend_UID)) {
        std::string notify = "You have been removed from [" + name_ + "]'s friend list.\n";
        NoticeService::get_instance().send_to_user(friend_UID, notify, "system");
    }
}

void social_module::create_friend_group(std::string group_name, int& out_group_uid){
    Group_manager::get_instance().create_group(user_UID_, group_name, out_group_uid);
    // 把新建群加入当前用户的群组列表，这样 /show 才能展示出自己的群
    if (out_group_uid >= 0) {
        friend_groups.push_back(out_group_uid);
    }
}
void social_module::exit_friend_group(int group_UID){
    Group_manager::get_instance().remove_group_member(group_UID,user_UID_,user_UID_);
}

// 发送好友申请：落库（friend_request, status=0）+ 通知接收方
void social_module::send_friend_request(int receiver_UID, std::string &apply_message){
    auto receiver = repo_hub_->accounts()->load_account(receiver_UID);//校验被添加者是否存在
    if (!receiver) {
        NoticeService::get_instance().send_to_user(user_UID_, "The target user does not exist!\n", "system");
        return;
    }
    if (!repo_hub_->friends()->send_friend_request(user_UID_, receiver_UID, apply_message)) {
        NoticeService::get_instance().send_to_user(user_UID_, "Failed to send friend request (request may already be pending).\n", "system");
        return;
    }
    auto sender = repo_hub_->accounts()->load_account(user_UID_);
    std::string sender_name = sender ? sender->getName() : std::to_string(user_UID_);
    std::string notify = "[" + sender_name + "] wants to be friend with you!\n" + apply_message;
    NoticeService::get_instance().send_to_user(receiver_UID, notify, "system");
}

// 处理好友申请：更新申请状态（1同意/2拒绝）；同意则建立双向好友关系
void social_module::handle_friend_request(int sender_UID, bool accept){
    if (!repo_hub_->friends()->handle_friend_request(sender_UID, user_UID_, accept)) {
        NoticeService::get_instance().send_to_user(user_UID_, "Failed to handle friend request (no pending request found).\n", "system");
        return;
    }
    if (accept) {
        add_friend(sender_UID); // 建立双向好友关系并通知对方
    } else {
        std::string notify = name_ + " rejected your friend request.\n";
        NoticeService::get_instance().send_to_user(sender_UID, notify, "system");
    }
}

// 查看发给自己的、等待处理的好友申请
std::string social_module::show_friend_requests(){
    std::vector<std::tuple<int, std::string>> requests;
    if (!repo_hub_->friends()->get_friend_requests(user_UID_, requests)) {
        return "Failed to load friend requests.\n";
    }
    if (requests.empty()) {
        return "No pending friend requests.\n";
    }
    std::string result;
    for (auto& req : requests) {
        int sender_uid = std::get<0>(req);
        const std::string& msg = std::get<1>(req);
        auto sender = repo_hub_->accounts()->load_account(sender_uid);
        std::string sender_name = sender ? sender->getName() : std::to_string(sender_uid);
        result += "[" + sender_name + "] (" + std::to_string(sender_uid) + "): " + msg + "\n";
    }
    return result;
}

std::string social_module::show_friends(){
    std::string chat_list;
    for(int UID:friend_relations){
        auto acc = repo_hub_->accounts()->load_account(UID);
        if (acc) {
            chat_list += acc->get_info() + "\n";
        }
    }
    for(int UID:friend_groups){
        auto grp = Group_manager::get_instance().find_group(UID);
        if (grp) {
            chat_list += grp->get_group_info() + "\n";
        }
    }
    return chat_list;
}

// 同意好友申请（兼容旧接口，转发到 handle_friend_request）
void social_module::accept_friend_request(int sender_UID){
    handle_friend_request(sender_UID, true);
}
