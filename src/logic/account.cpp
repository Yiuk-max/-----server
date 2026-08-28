#include "account.h"

account::account() {
    // 占位/默认账户：写入安全默认值，保证对象任何时候都处于有效状态。
    // 真正的用户数据由仓库层(load_account)构造带参 account 后填充。
    load_account_info();
}

account::account(int uid, const std::string& name, const std::string& password) {
    base_info.UID = uid;
    base_info.name = name;
    base_info.password = password;
    load_account_info();   // 让 settings 等进入安全的默认值（theme=white / language=Chinese）
}

std::string account::getName(){
    return base_info.name;
}

void account::load_account_info(){
    // 【默认值兜底】Account 表其余列（settings/language）若仓库尚未填充，
    // 则这里保证其处于合理默认值，避免读到未初始化内存。
    // 真正的数据库加载由 repo 层完成：
    //   account_repo::load_account() SELECT UID,password,nickname,settings,language ...
    //   之后依次调用 set_settings_json() / set_theme() / set_language() 填充。
    if (base_info.settings_json.empty()) {
        base_info.settings_json = "{}";
    }
    if (settings.theme.empty()) {
        settings.theme = "white";
    }
    if (settings.language.empty()) {
        settings.language = "Chinese";
    }
    // last_login_time 无默认值语义，空串表示"从未登录"，由 repo / 外层在登录时填充
}

// ---- 设置项存取 ----
std::string account::get_theme()    const { return settings.theme; }
void        account::set_theme(const std::string& theme) { settings.theme = theme; }
std::string account::get_language() const { return settings.language; }
void        account::set_language(const std::string& language) { settings.language = language; }
std::string account::get_last_login_time() const { return settings.last_login_time; }
void        account::set_last_login_time(const std::string& t) { settings.last_login_time = t; }
std::string account::get_settings_json()  const { return base_info.settings_json; }
void        account::set_settings_json(const std::string& json) { base_info.settings_json = json; }

bool account::passwd_check(std::string password){
    return base_info.password == password;
}
std::string account::passwd_raw(){
    return base_info.password;
}
void account::set_password(const std::string& password){
    base_info.password = password;
}
int account::getUID() const {
    return base_info.UID;
}

void account::setName(const std::string& name){//已失效，需要由仓储层在 update_account() 时直接修改数据库，成功后再回写本对象
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