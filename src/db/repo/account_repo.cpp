#include "account_repo.h"
#include "mysql_conn_pool.h"
#include <memory>
#include <iostream>
#include <nlohmann/json.hpp>
// JDBC 类型定义（PreparedStatement / ResultSet / Statement）由 mysql_connection/mysql_connection 递送不保证完整，
// 显式引入以确保类型完整可用。
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"
#include "cppconn/exception.h"

// 连接 RAII：取池连接，作用域结束后归还，异常安全。
namespace {
class ConnGuard {
public:
    ConnGuard() : conn_(MySQL_Conn_Pool::get_instance().get_connection()) {}
    ~ConnGuard() {
        if (conn_) {
            MySQL_Conn_Pool::get_instance().return_connection(conn_);
        }
    }
    sql::Connection* get() { return conn_; }
    explicit operator bool() const { return conn_ != nullptr; }
private:
    sql::Connection* conn_;
};

// 构造 Account.settings 列的 JSON 字符串（theme + language + last_login_time）
std::string make_settings_json(const std::string& theme, const std::string& language,
                               const std::string& last_login_time) {
    nlohmann::json j;
    j["theme"]           = theme;
    j["language"]        = language;
    j["last_login_time"] = last_login_time;
    return j.dump();
}

// 从 Account.settings 的 JSON 字符串解析出 last_login_time（解析失败/缺键回退空串）
std::string parse_last_login_time(const std::string& settings_json) {
    if (settings_json.empty()) {
        return "";
    }
    try {
        nlohmann::json j = nlohmann::json::parse(settings_json);
        if (j.contains("last_login_time") && j["last_login_time"].is_string()) {
            return j["last_login_time"].get<std::string>();
        }
    } catch (const std::exception&) {
        // JSON 解析失败，回退空串
    }
    return "";
}

// 从 Account.settings 的 JSON 字符串解析出 theme（解析失败/缺键回退默认 white）
std::string parse_theme(const std::string& settings_json) {
    if (settings_json.empty()) {
        return "white";
    }
    try {
        nlohmann::json j = nlohmann::json::parse(settings_json);
        if (j.contains("theme") && j["theme"].is_string()) {
            return j["theme"].get<std::string>();
        }
    } catch (const std::exception&) {
        // JSON 解析失败，回退默认 white
    }
    return "white";
}
} // namespace

// 注册：INSERT Account，取数据库自动分配的 UID
std::shared_ptr<account> account_repo::register_account(const std::string& name,const std::string& password) 
{
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_repo] register: no DB connection." << std::endl;
        return nullptr;
    }
    try {
        // 默认设置：theme=white, language=Chinese；写入 settings JSON 与 language 列
        std::string settings = make_settings_json("white", "Chinese", "");
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "INSERT INTO Account (password, nickname, settings, language) VALUES (?, ?, ?, ?)"));
            pstmt->setString(1, password);
            pstmt->setString(2, name);
            pstmt->setString(3, settings);
            pstmt->setString(4, "Chinese");
            pstmt->execute();
        }
        // 方案B：让数据库 AUTO_INCREMENT 分配 UID，用 LAST_INSERT_ID() 取回
        int uid = -1;
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery("SELECT LAST_INSERT_ID()"));
        if (rs->next()) {
            uid = rs->getInt(1);
        }
        if (uid <= 0) {
            std::cerr << "[account_repo] register: LAST_INSERT_ID invalid." << std::endl;
            return nullptr;
        }
        auto acc = std::make_shared<account>(uid, name, password);
        // 填充设置项，与库中一致
        acc->set_theme("white");
        acc->set_language("Chinese");
        acc->set_last_login_time("");
        acc->set_settings_json(settings);
        return acc;
    } catch (const sql::SQLException& e) {
        // 常见失败原因：nickname 违反唯一约束（uk_nickname），已存在同名账户
        std::cerr << "[account_repo] register failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return nullptr;
    }
}

// 注销：DELETE Account WHERE UID=?
void account_repo::remove_account(int uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_repo] remove: no DB connection." << std::endl;
        return;
    }
    try {
        // 依赖外键 ON DELETE CASCADE 清理好友关系/群成员等关联
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("DELETE FROM Account WHERE UID = ?"));
        pstmt->setInt(1, uid);
        pstmt->executeUpdate();
    } catch (const sql::SQLException& e) {
        std::cerr << "[account_repo] remove failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
}

// 加载：按 UID 查询账户（含设置项 settings/language）
std::shared_ptr<account> account_repo::load_account(int uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_repo] load: no DB connection." << std::endl;
        return nullptr;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT UID, password, nickname, settings, language FROM Account WHERE UID = ?"));
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return nullptr; // 账户不存在
        }
        auto acc = std::make_shared<account>(rs->getInt("UID"),
                                             rs->getString("nickname"),
                                             rs->getString("password"));
        // 填充设置项：theme 从 settings JSON 解析，language 取独立列
        const std::string settings_json = rs->getString("settings");
        acc->set_settings_json(settings_json);
        acc->set_theme(parse_theme(settings_json));
        acc->set_language(rs->getString("language"));
        acc->set_last_login_time(parse_last_login_time(settings_json));
        return acc;
    } catch (const sql::SQLException& e) {
        std::cerr << "[account_repo] load failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return nullptr;
    }
}

// 更新：修改账户个人资料（昵称/密码/设置项）；成功返回 true
bool account_repo::update_account(const std::shared_ptr<account>& acc) {
    if (!acc) {
        return false;
    }
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_repo] update: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE Account SET nickname = ?, password = ?, settings = ?, language = ? WHERE UID = ?"));
        pstmt->setString(1, acc->getName());
        pstmt->setString(2, acc->passwd_raw());
        pstmt->setString(3, make_settings_json(acc->get_theme(), acc->get_language(),
                                             acc->get_last_login_time()));
        pstmt->setString(4, acc->get_language());
        pstmt->setInt(5, acc->getUID());
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        // 常见失败：nickname 违反唯一约束（uk_nickname），已存在同名账户
        std::cerr << "[account_repo] update failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}
