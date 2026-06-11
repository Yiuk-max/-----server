#include "account_manager.h"

void account_manager::register_account(const std::string& name, const std::string& password) {
    std::lock_guard<std::mutex> lock(accounts_mutex);
    auto new_account = std::make_shared<account>(UID_allocator::get_instance().request_uid(), name, password);
    accounts_[new_account->getUID()] = new_account;
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