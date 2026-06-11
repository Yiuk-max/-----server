#include "social_module.h"
#include "session_manager.h"
#include "group.h"
void social_module::add_friend(int friend_UID){
    friend_relations.emplace_back(friend_UID);
}
void social_module::remove_friend(int friend_UID){
    friend_relations.erase(std::remove(friend_relations.begin(), friend_relations.end(), friend_UID), friend_relations.end());
}
void social_module::create_friend_group(std::string group_name){
    Group_manager::get_instance().create_group(user_UID_,group_name);
}
void social_module::exit_friend_group(int group_UID){
    Group_manager::get_instance().remove_group_member(group_UID,user_UID_,user_UID_);
}
void social_module::send_friend_request(int receiver_UID, std::string &apply_message){

    auto receiver = account_manager::get_instance().find_account(receiver_UID);//效验被添加者是否存在
    if(receiver){
        std::string message;
        auto sender = account_manager::get_instance().find_account(user_UID_);
        if (!sender) {
            return;
        }
        std::string sender_name = sender->getName();
        message +="[" + sender_name + "]" + "want to be friend with you!\n" + apply_message;
        auto receiver_session = session_manager::get_instance().find_session(receiver_UID);
        if (receiver_session) {
            receiver_session->package_message(message,"system");
        }
    }else{
        std::string fail = "The target user not exit!";
        auto self_session = session_manager::get_instance().find_session(user_UID_);
        if (self_session) {
            self_session->package_message(fail,"system");
        }
    }
}
void social_module::handle_friend_request(int sender_UID, bool accept){
    std::string notify;
    auto self_account = account_manager::get_instance().find_account(user_UID_);
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
    auto sender_session = session_manager::get_instance().find_session(sender_UID);
    if (sender_session) {
        sender_session->package_message(notify,"system");
    }
}
std::string social_module::show_friends(){
    std::string chat_list;
    for(int UID:friend_relations){
        auto acc = account_manager::get_instance().find_account(UID);
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

