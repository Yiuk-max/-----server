#include "social_module.h"
#include "notice_service.h"
#include "group.h"
#include <algorithm>
social_module::social_module(int UID, std::shared_ptr<I_account_repo> repo)
    :user_UID_(UID), account_repo_(repo) {
    auto acc = account_repo_->load_account(UID);
        if (acc) {
        name_ = acc->getName();
        }
}
void social_module::add_friend(int friend_UID){
    std::string target_name = account_repo_->load_account(friend_UID)->getName();
    std::string message = "You received a new friend request from [" + target_name + "]";


    if (NoticeService::get_instance().is_online(friend_UID)) {
        // 发送friend_request好友请求通知给目标用户
        std::string notify = "You have a new friend request from [" + name_ + "]: ";

        NoticeService::get_instance().send_to_user(friend_UID, notify, "system");
    }else{
        std::string fail = "The target user is not online!";




        NoticeService::get_instance().send_to_user(user_UID_, fail, "system");
    }   
}
void social_module::remove_friend(int friend_UID){
    friend_relations.erase(std::remove(friend_relations.begin(), friend_relations.end(), friend_UID), friend_relations.end());
    //找到好友的session，发送通知，从对方的好友列表中删除自己


    if (NoticeService::get_instance().is_online(friend_UID)) {
        std::string notify = "You have been removed from [" + name_ + "]'s friend list.";

        NoticeService::get_instance().send_to_user(friend_UID, notify, "system");
    }else{
        std::string fail = "The target user is not online!";



        NoticeService::get_instance().send_to_user(user_UID_, fail, "system");
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
void social_module::send_friend_request(int receiver_UID, std::string &apply_message){

    auto receiver = account_repo_->load_account(receiver_UID);//效验被添加者是否存在
    if(receiver){
        std::string message;
        auto sender = account_repo_->load_account(user_UID_);
        if (!sender) {
            return;
        }
        std::string sender_name = sender->getName();
        message +="[" + sender_name + "]" + "want to be friend with you!\n" + apply_message;
        NoticeService::get_instance().send_to_user(receiver_UID, message, "system");
    }else{
        std::string fail = "The target user not exit!";
        NoticeService::get_instance().send_to_user(user_UID_, fail, "system");
    }
}
void social_module::handle_friend_request(int sender_UID, bool accept){
    std::string notify;
    auto self_account = account_repo_->load_account(user_UID_);
    if (!self_account) {
        return;
    }
    std::string name = self_account->getName();
    if(accept){
        friend_relations.push_back(sender_UID);

        notify = name + "accepted your apply !";
    }else{
        notify = name + "rejected your apply !";
    }
    NoticeService::get_instance().send_to_user(sender_UID, notify, "system");
}
std::string social_module::show_friends(){
    std::string chat_list;
    for(int UID:friend_relations){
        auto acc = account_repo_->load_account(UID);
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

void social_module::accept_friend_request(int sender_UID){
    friend_relations.push_back(sender_UID);
    std::string notify = "Your friend request has been accepted!";
    NoticeService::get_instance().send_to_user(sender_UID, notify, "system");
}

