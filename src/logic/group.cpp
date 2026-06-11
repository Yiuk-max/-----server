#include "group.h"
#include "client_session.h"


void group::group_spk(std::string message){
    std::lock_guard<std::mutex> lock(group_mutex_);
    for(int UID:member_UID_list){
        auto it = session_manager::get_instance().find_session(UID);
        if(it){
            it->package_message(message,"Group_Chat");
        }
    }

}
bool group::add_client(int UID,int sender_UID){
    if(!is_manager_(sender_UID)){
        return false;
    }
    std::lock_guard<std::mutex> lock(group_mutex_);
    if(member_UID_list.size() >= 100){
        //发送群成员已满提示
        return false;
    }
    auto it = std::find(member_UID_list.begin(), member_UID_list.end(), UID);
    if(it != member_UID_list.end()){
        return false;
    }
    member_UID_list.push_back(UID);
    return true;
}
bool group::delete_client(int UID,int sender_UID){
    if(!is_manager_(sender_UID)){
        return false;
    }
    std::lock_guard<std::mutex> lock(group_mutex_);
    auto it = std::find(member_UID_list.begin(), member_UID_list.end(), UID);
    if (it != member_UID_list.end()) {
        member_UID_list.erase(it);
        return true;
    }
    return false;
}
bool group::is_manager_(int UID){
    return UID == manager_UID_;
}
bool group::modify_group_name(int renmaer_UID,std::string new_group_name){
    if(is_manager_(renmaer_UID)){
        group_name_ = new_group_name;
        return true;
    }else{
        //发送失败提示
        auto session = session_manager::get_instance().find_session(renmaer_UID);
        if (session) {
            session->package_message("Permission denied: only manager can modify group name.", "system");
        }
    }
    return false;
}
std::string group::get_group_info(){
    std::string info;
    info += group_name_ + ":" + str_UID;
    return info;
}

std::string group::show_member(){
    std::string members;
    for(int uid : member_UID_list){
        members += std::to_string(uid) + " ";
    }
    return members;
}