#include "account.h"
int account::getClientFd(){
    return client_fd_;
}
std::string account::getName(){
    return  base_info.name;
}
bool account::passwd_check(std::string password){
    return base_info.password == stored_password;
}
void account::load_account_info(){
    // 从数据库或文件加载用户信息，填充base_info和settings
}
int account::getUID()const{
    return base_info.UID;
}

void account::setName(const std::string& name){
    base_info.name = name;
}
