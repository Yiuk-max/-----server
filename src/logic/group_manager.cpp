#include "group_manager.h"
#include "group.h"

void Group_manager::create_group(int manager_UID,std::string group_name){
    std::lock_guard<std::mutex> lock(group_manager_mutex);
    int group_UID = UID_allocator::get_instance().request_group_id();
    auto new_group = std::make_shared<group>(manager_UID,group_name,group_UID);
    group_list_[group_UID] = new_group;
}
void Group_manager::delete_group(int group_UID,int requester_UID){
    std::lock_guard<std::mutex> lock(group_manager_mutex);
    auto it = group_list_.find(group_UID);
    if(it != group_list_.end() && it->second->is_manager_(requester_UID)){
        group_list_.erase(group_UID);
    }
}
std::shared_ptr<group> Group_manager::find_group(int group_id){
    std::lock_guard<std::mutex> lock(group_manager_mutex);
    auto it = group_list_.find(group_id);
    if(it != group_list_.end()){
        return it->second;
    }
    return nullptr;
}
void Group_manager::add_group_member(int group_UID,int newmember_UID,int sender_UID){
    //由group内部加锁，无需manager上锁
    auto grp = find_group(group_UID);
    if (grp) {
        grp->add_client(newmember_UID,sender_UID);
    }
}
void Group_manager::remove_group_member(int group_UID,int member_UID,int sender_UID){
    //由group内部加锁，无需manager上锁
    auto grp = find_group(group_UID);
    if (grp) {
        grp->delete_client(member_UID,sender_UID);
    }
}
void Group_manager::show_group_member(int group_UID){
    auto grp = find_group(group_UID);
    if (grp) {
        grp->show_member();
    }
}