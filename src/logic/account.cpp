#include "account.h"

account::account(int uid, const std::string& name, const std::string& password)
    : UID_(uid), name_(name), password_(password) {}

std::string account::getName() const {
    return name_;
}

void account::setName(const std::string& name) {
    name_ = name;
}

bool account::passwd_check(const std::string& password) const {
    return password_ == password;
}

std::string account::passwd_raw() const {
    return password_;
}

int account::getUID() const {
    return UID_;
}

std::string account::get_string_UID() const {
    std::string str_uid = std::to_string(UID_);
    // 补零到 uid_length 位
    const size_t uid_length = 6;
    while (str_uid.length() < uid_length) {
        str_uid = "0" + str_uid;
    }
    return str_uid;
}

// ---- 设置项存取 ----
std::string account::get_theme()    const { return theme_; }
void        account::set_theme(const std::string& theme) { theme_ = theme; }
std::string account::get_language() const { return language_; }
void        account::set_language(const std::string& language) { language_ = language; }
std::string account::get_last_login_time() const { return last_login_time_; }
void        account::set_last_login_time(const std::string& t) { last_login_time_ = t; }
std::string account::get_settings_json()  const { return settings_json_; }
void        account::set_settings_json(const std::string& json) { settings_json_ = json; }

std::string account::get_token() const {
    return token_;
}
void account::set_token(const std::string& token) {
    token_ = token;
}

int account::get_avatar_id() const {
    return avatar_id_;
}
void account::set_avatar_id(int avatar_id) {
    avatar_id_ = avatar_id;
}
