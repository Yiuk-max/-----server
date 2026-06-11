#include "account.h"

account::account() {
    load_account_info();
}

account::account(int uid, const std::string& name, const std::string& password) {
    base_info.UID = uid;
    base_info.name = name;
    base_info.password = password;
}

std::string account::getName(){
    return base_info.name;
}
bool account::passwd_check(std::string password){
    return base_info.password == password;
}
void account::load_account_info(){
    // 从数据库或文件加载用户信息，填充base_info和settings
}
int account::getUID() const {
    return base_info.UID;
}

void account::setName(const std::string& name){
    base_info.name = name;
}
std::string account::get_string_UID(){
    std::string str_uid = std::to_string(base_info.UID);
    // 补零到uid_length位
    const size_t uid_length = 6;
    while (str_uid.length() < uid_length) {
        str_uid = "0" + str_uid;
    }
    return str_uid;
}
std::string account::get_info(){
    std::string info;
    info += getName() + ":" + get_string_UID();
    return info;
}