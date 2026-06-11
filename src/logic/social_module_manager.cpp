#include "social_module_manager.h"
#include "social_module.h"

void social_manager::add_social_module(int UID){
    std::lock_guard<std::mutex>lock (social_mutex);
    auto it = std::make_shared<social_module>(UID);
    social_module_manager_[UID] = it;
}
void social_manager::remove_social_module(int UID){
    std::lock_guard<std::mutex>lock (social_mutex);
    auto it = social_module_manager_.find(UID);
    
    if(it!=social_module_manager_.end()){
        social_module_manager_.erase(it);
    }
}
std::shared_ptr<social_module> social_manager::find_relation_manager(int UID){
    std::lock_guard<std::mutex> lock(social_mutex);
    auto it = social_module_manager_.find(UID);
    if (it != social_module_manager_.end()) {
        return it->second;
    }
    return nullptr;
}