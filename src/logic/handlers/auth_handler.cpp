#include "message_handler.h"

#include "client_session.h"
#include "logic_common.h"
#include "repository_hub.h"
#include "social_module.h"
#include "group_manager.h"

#include <ctime>

namespace {

// 登录成功后的通用收尾：建立社交模块、上线映射、返回 token/user JSON、推送申请与离线消息
void finish_login(client_session& s, const std::shared_ptr<account>& acc, const std::string& token) {
    const int UID = acc->getUID();
    // 记录上一次登录时间（用于查询离线消息：自上次登录之后未收到的消息）
    std::string last_login_time = acc->get_last_login_time();
    // 确保自己是自己的好友：新账号在注册时已写入，这里覆盖历史账号，并在被删除后自愈。
    s.repo_hub()->friends()->ensure_self_friend(UID);
    auto new_social = std::make_shared<social_module>(UID, s.repo_hub());

    std::shared_ptr<client_session> old_session;
    if (!s.activate_session(acc, new_social, &old_session)) {
        return; // DB 查询期间连接已经断开
    }
    if (old_session && old_session.get() != &s) {
        old_session->kick_offline();
    }

    // 返回登录成功消息：token + user 信息（前端保存 token 用于自动登录）
    chat_proto::Envelope resp;
    resp.set_type("login_success");
    resp.set_token(token);
    auto* user = resp.mutable_user();
    user->set_id(UID);
    user->set_username(acc->getName());
    resp.set_avatar_id(acc->get_avatar_id());
    s.package_envelope(resp);

    logic::show_friend_requests(s); // 登录后自动查看待处理的好友申请
    logic::send_offline_messages(s, last_login_time); // 登录后自动获取一次离线消息

    // 更新"上次登录时间"
    char time_buf[32];
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&now, &tmv);
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tmv);
    acc->set_last_login_time(time_buf);
    s.repo_hub()->accounts()->update_account(acc);
}

void register_user(client_session& s, std::string username, std::string password, std::string email) {
    if (username.empty() || password.empty()) {
        s.package_message("Username and password cannot be empty.\n", "system");
        return;
    }
    if (email.empty()) {
        // 新用户注册必须填邮箱
        s.package_message("Email is required for registration.\n", "system");
        return;
    }
    // 邮箱唯一性预检查（并发下最终由 account_email.email 主键兜底）
    if (s.repo_hub()->emails()->find_uid_by_email(email) != -1) {
        s.package_message("Email [" + email + "] is already registered.\n", "system");
        return;
    }
    auto new_account = s.repo_hub()->accounts()->register_account(username, password);
    if (!new_account) {
        s.package_message("Registration failed (database unavailable).\n", "system");
        return;
    }
    const int uid = new_account->getUID();
    if (!s.repo_hub()->emails()->set_email(uid, email)) {
        // 并发下邮箱刚被占用：回滚刚创建的账户，避免产生无邮箱的孤儿账号
        s.repo_hub()->accounts()->remove_account(uid);
        s.package_message("Email [" + email + "] is already registered.\n", "system");
        return;
    }
    // 注册后把自己加为自己的好友，允许给自己发消息（自聊）。
    s.repo_hub()->friends()->ensure_self_friend(uid);
    // 注意：注册只创建账号，不建立登录态；客户端仍需 login。
    std::string UID = new_account->get_string_UID();
    std::string success = "Registration successful. You can now log in with UID " + UID + " or " + email + ".\n";
    s.package_message(success, "system");
}

void login(client_session& s, int UID, std::string password) {
    auto acc = s.repo_hub()->accounts()->load_account(UID);
    if (!acc) {
        s.package_message("Account with UID [" + std::to_string(UID) + "] does not exist.\n", "system");
        return;
    }
    if (!acc->passwd_check(password)) {
        s.package_message("Incorrect password for UID [" + std::to_string(UID) + "].\n", "system");
        return;
    }
    // 生成并持久化 token，随后返回给前端保存
    std::string token = logic::generate_token();
    s.repo_hub()->accounts()->update_token(UID, token);
    finish_login(s, acc, token);
}

// 用邮箱登录：先查邮箱绑定的 UID，再复用 UID 登录流程
void login_by_email(client_session& s, std::string email, std::string password) {
    int uid = s.repo_hub()->emails()->find_uid_by_email(email);
    if (uid < 0) {
        s.package_message("No account is bound to email [" + email + "].\n", "system");
        return;
    }
    login(s, uid, password);
}

// 用 token 自动登录（跳过密码）：token 解析 → 验证 → 找到 user_id → 登录成功
void verify_token(client_session& s, std::string token) {
    if (token.empty()) {
        s.package_message("Token is required.\n", "system");
        return;
    }
    auto acc = s.repo_hub()->accounts()->load_account_by_token(token);
    if (!acc) {
        s.package_message("Invalid or expired token.\n", "system");
        return;
    }
    // 7 天过期：用 Account.settings 里的 last_login_time 判断。
    // 每次登录都会刷新 last_login_time，因此是“7 天不登录则过期”的滑动过期。
    const std::time_t last = logic::parse_datetime(acc->get_last_login_time());
    const std::time_t now = std::time(nullptr);
    constexpr std::time_t SEVEN_DAYS = 7 * 24 * 60 * 60;
    if (last == 0 || now - last > SEVEN_DAYS) {
        s.repo_hub()->accounts()->update_token(acc->getUID(), "");
        s.package_message("Token expired, please login again.\n", "system");
        return;
    }
    finish_login(s, acc, token);
}

// 绑定/换绑邮箱（老账号补绑邮箱用；不校验格式，仅要求不重复）
void set_email(client_session& s, std::string email) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to set an email.\n", "system");
        return;
    }
    if (email.empty()) {
        s.package_message("Email cannot be empty.\n", "system");
        return;
    }
    if (s.repo_hub()->emails()->set_email(acc->getUID(), email)) {
        s.package_message("Email updated successfully. Your email is [" + email + "].\n", "system");
    } else {
        s.package_message("Failed to set email (email may already be used by another account).\n", "system");
    }
}

// 修改自己的昵称：先改内存，再落库；失败时回滚内存中的昵称，保证与数据库一致
void change_my_name(client_session& s, std::string new_name) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to change your name.\n", "system");
        return;
    }
    if (new_name.empty()) {
        s.package_message("Name cannot be empty.\n", "system");
        return;
    }
    std::string old_name = acc->getName();
    acc->setName(new_name);
    if (s.repo_hub()->accounts()->update_account(acc)) {
        s.package_message("Name updated successfully. Your new name is [" + new_name + "].\n", "system");
    } else {
        acc->setName(old_name); // 失败回滚内存中的昵称，保持与库一致
        s.package_message("Failed to update name (database unavailable).\n", "system");
    }
}

// 修改自己的主题：先改内存，再落库；失败时回滚内存主题，保证与数据库一致
void change_theme(client_session& s, const std::string& theme) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to change your theme.\n", "system");
        return;
    }
    if (theme.empty()) {
        s.package_message("Theme cannot be empty.\n", "system");
        return;
    }
    std::string old_theme = acc->get_theme();
    acc->set_theme(theme);
    if (s.repo_hub()->accounts()->update_account(acc)) {
        s.package_message("Theme updated successfully. Your new theme is [" + theme + "].\n", "system");
    } else {
        acc->set_theme(old_theme); // 失败回滚内存中的主题，保持与库一致
        s.package_message("Failed to update theme (database unavailable).\n", "system");
    }
}

void show_chatlist(client_session& s) {
    auto acc = s.current_account();
    auto social = s.social_manager();
    if (!acc || !social) {
        s.package_message("You must be logged in to view your chat list.\n", "system");
        return;
    }
    s.package_message(social->show_friends(), "system");
}

// 结构化联系人列表（好友 + 群），携带每个好友的 avatar_id 供客户端加载头像。
void show_contacts(client_session& s) {
    auto acc = s.current_account();
    auto social = s.social_manager();
    if (!acc || !social) {
        s.package_message("You must be logged in to view contacts.\n", "system");
        return;
    }

    chat_proto::Envelope resp;
    resp.set_type("contact_list_response");

    for (int uid : social->friend_list()) {
        auto f = s.repo_hub()->accounts()->load_account(uid);
        if (!f) continue;
        auto* c = resp.add_contacts();
        c->set_uid(uid);
        const std::string remark = s.repo_hub()->friends()->get_remark(acc->getUID(), uid);
        c->set_name(remark.empty() ? f->getName() : remark);
        c->set_is_group(false);
        c->set_avatar_id(f->get_avatar_id());
    }

    for (int gid : social->group_list()) {
        auto grp = group_manager::get_instance().get(gid);
        if (!grp) continue;
        auto* c = resp.add_contacts();
        c->set_uid(gid);
        c->set_name(grp->get_group_name());
        c->set_is_group(true);
        c->set_avatar_id(0);
    }

    s.package_envelope(resp);
}

// 设置头像：把当前账号的 avatar_id 指向一个已上传的文件（file_id=-1 表示清除）。
void set_avatar(client_session& s, const std::string& file_id_str) {
    auto acc = s.current_account();
    if (!acc) {
        s.package_message("You must be logged in to set an avatar.\n", "system");
        return;
    }
    int file_id = -1;
    if (!file_id_str.empty()) {
        try {
            file_id = std::stoi(file_id_str);
        } catch (...) {
            s.package_message("Invalid file_id.\n", "system");
            return;
        }
    }
    if (file_id > 0) {
        file_meta_info f;
        if (!s.repo_hub()->files()->get_file(file_id, f)) {//上传头像
            s.package_message("File not found.\n", "system");//检查文件是否存在
            return;
        }
    }
    if (s.repo_hub()->accounts()->update_avatar(acc->getUID(), file_id)) {//更新数据库中的头像id
        acc->set_avatar_id(file_id);
        s.package_message("Avatar updated successfully.\n", "system");
    } else {
        s.package_message("Failed to update avatar (database unavailable).\n", "system");
    }
}

} // namespace

void Base_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string& file_data) {
    const std::string type = message.type();
    if (type == "show") {
        show_chatlist(session);
        return;
    } else if (type == "logout") {
        session.logout();
        return;
    } else if (type == "exit") {
        session.exit_self();
        return;
    } else if (type == "login") {
        std::string password = message.password();
        // 登录支持两种方式：带 email 则用邮箱，否则用 UID（老客户端兼容）
        if (!message.email().empty()) {
            login_by_email(session, message.email(), password);
        } else {
            login(session, message.uid(), password);
        }
        return;
    } else if (type == "verify_token") {
        verify_token(session, message.token());
        return;
    } else if (type == "register") {
        register_user(session, message.username(), message.password(), message.email());
        return;
    } else if (type == "set_email") {
        set_email(session, message.email());
        return;
    } else if (type == "change_name") {
        change_my_name(session, message.new_name());
        return;
    } else if (type == "change_theme") {
        change_theme(session, message.theme());
        return;
    } else if (type == "set_avatar") {
        set_avatar(session, message.file_id());
        return;
    } else if (type == "show_contacts") {
        show_contacts(session);
        return;
    }
}

