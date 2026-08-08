#include "account_manager.h"

std::string account_manager::register_account(const std::string& name, const std::string& password) {
    int new_uid;
    {
        std::lock_guard<std::mutex> lock(accounts_mutex);
        auto new_account = std::make_shared<account>(UID_allocator::get_instance().request_uid(), name, password);
        accounts_[new_account->getUID()] = new_account;
        new_uid = new_account->getUID();
    }
    // 社交模块不再在这里创建：social_module 属于"登录用户会话"的状态，
    // 由 client_session 在 login 时随会话创建并持有（见 client_session::login）。
    return UID_allocator::get_instance().get_string_UID(new_uid);
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