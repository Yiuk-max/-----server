#include "group.h"
#include "client_session.h"
std::shared_ptr<group> find_group_by_name(std::string group_name){
    auto it = group_list.find(group_name);
    if (it == group_list.end()) {
        // std::string fail = "Group [" + group_name + "] does not exist.\n";
        // send_message(fail);
        return nullptr;
    }
    return it->second;
}
void group::group_spk(std::string message){
    std::lock_guard<std::mutex> lock(group_mutex_);
    for(int fd:group_clients){
        if(fd == -1) continue;
        auto it = handle_msg_list.find(std::to_string(fd));
        if (it == handle_msg_list.end()) continue; // 连接可能已经被关闭
        it->second->package_message("[group chat]:"+message,group_name_);
    }
}
bool group::add_client(int UID){
    std::lock_guard<std::mutex> lock(group_mutex_);
    if(client_name_group.size() >= 100){
        //发送群成员已满提示
        return false;
    }
    member_UID_list.push_back(UID);
    return true;
}
bool group::delete_client(int UID){
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
    }
    return false;
}
