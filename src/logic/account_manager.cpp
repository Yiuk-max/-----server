#include "account_manager.h"
#include "social_module_manager.h"

std::string account_manager::register_account(const std::string& name, const std::string& password) {
    int new_uid;
    {
        std::lock_guard<std::mutex> lock(accounts_mutex);
        auto new_account = std::make_shared<account>(UID_allocator::get_instance().request_uid(), name, password);
        accounts_[new_account->getUID()] = new_account;
        new_uid = new_account->getUID();
    }
    // 注意：必须先释放 accounts_mutex 再创建社交模块。
    // 因为 social_module 构造函数内部会调用 find_account() 重新获取 accounts_mutex，
    // 而 std::mutex 不可重入，若在持有锁时调用 add_social_module 会造成死锁（注册无响应）。
    social_manager::get_instance().add_social_module(new_uid);
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