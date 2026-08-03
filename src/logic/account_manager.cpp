#include "account_manager.h"

std::string account_manager::register_account(const std::string& name, const std::string& password) {
    std::lock_guard<std::mutex> lock(accounts_mutex);
    auto new_account = std::make_shared<account>(UID_allocator::get_instance().request_uid(), name, password);
    accounts_[new_account->getUID()] = new_account;
    std::string UID = UID_allocator::get_instance().get_string_UID(new_account->getUID());
    social_manager::get_instance().add_social_module(new_account->getUID()); // 为新注册的用户创建社交模块

    return UID;
}
void account_manager::remove_account(int UID) {
    std::lock_guard<std::mutex> lock(accounts_mutex);
    accounts_.erase(UID);
}
std::shared_ptr<account> account_manager::find_account(int UID) {
    std::lock_guard<std::mutex> lock(accounts_mutex);
    auto it = accounts_.find(UID);
    if (it != accounts_.end()) {
        return it->second;
    }
    return nullptr;
}
std::string account_manager::get_account_info(int UID){
    std::lock_guard<std::mutex> lock(accounts_mutex);
    auto it = accounts_.find(UID);
    if (it != accounts_.end()) {
        return it->second->get_info();
    }
    return "";
}