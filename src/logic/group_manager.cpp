#include "group_manager.h"

void Group_manager::create_group(int UID,std::shared_ptr<group> new_group){
    std::lock_guard<std::mutex> lock(group_manager_mutex);
    group_list_[UID] = new_group;
}
void Group_manager::delete_group(int UID,int sender_fd){
    std::lock_guard<std::mutex> lock(group_manager_mutex);
    auto it = group_list_.find(UID);
    if(it != group_list_.end() && it->second->is_manager_fd(sender_fd)){

        group_list_.erase(UID);
    
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