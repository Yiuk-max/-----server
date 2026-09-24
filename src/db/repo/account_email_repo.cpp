#include "account_email_repo.h"
#include "mysql_conn_pool.h"
#include "redis_cache.h"
#include <iostream>
#include <memory>

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
} // namespace

// 绑定 / 换绑邮箱：事务内先校验邮箱归属，再删旧绑定、插新绑定。
// 邮箱已被其他 UID 占用时回滚返回 false。
bool account_email_repo::set_email(int uid, const std::string& email) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_email_repo] set_email: no DB connection." << std::endl;
        return false;
    }
    sql::Connection* conn = guard.get();
    try {
        conn->setAutoCommit(false);

        // 1. 邮箱若已被别的 UID 绑定，直接失败
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement("SELECT UID FROM account_email WHERE email = ?"));
            pstmt->setString(1, email);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            if (rs->next() && rs->getInt("UID") != uid) {
                conn->rollback();
                conn->setAutoCommit(true);
                return false;
            }
        }

        // 1.5 取出该 UID 当前的旧邮箱（换绑时用于失效旧映射缓存）
        std::string old_email;
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement("SELECT email FROM account_email WHERE UID = ?"));
            pstmt->setInt(1, uid);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            if (rs->next()) {
                old_email = rs->getString("email");
            }
        }

        // 2. 删掉该 UID 的旧绑定（换绑），再插入新绑定
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement("DELETE FROM account_email WHERE UID = ?"));
            pstmt->setInt(1, uid);
            pstmt->executeUpdate();
        }
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement("INSERT INTO account_email (email, UID) VALUES (?, ?)"));
            pstmt->setString(1, email);
            pstmt->setInt(2, uid);
            pstmt->execute();
        }

        conn->commit();
        conn->setAutoCommit(true);

        // 缓存：SET 新映射；换绑时 DEL 旧映射（禁用/异常时内部 no-op）
        RedisCache::get_instance().email_set(email, uid);
        if (!old_email.empty() && old_email != email) {
            RedisCache::get_instance().email_invalidate(old_email);
        }
        return true;
    } catch (const sql::SQLException& e) {
        try { conn->rollback(); } catch (...) {}
        try { conn->setAutoCommit(true); } catch (...) {}
        std::cerr << "[account_email_repo] set_email failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 按邮箱查 UID；不存在返回 -1
int account_email_repo::find_uid_by_email(const std::string& email) {
    // 缓存优先：命中直接返回 uid，避免注册预检查/邮箱登录/加好友反复查 DB
    if (auto uid = RedisCache::get_instance().email_get_uid(email)) {
        return *uid;
    }

    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_email_repo] find_uid_by_email: no DB connection." << std::endl;
        return -1;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("SELECT UID FROM account_email WHERE email = ?"));
        pstmt->setString(1, email);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (rs->next()) {
            const int uid = rs->getInt("UID");
            RedisCache::get_instance().email_set(email, uid);  // 只回填正映射，负结果不缓存
            return uid;
        }
        return -1;
    } catch (const sql::SQLException& e) {
        std::cerr << "[account_email_repo] find_uid_by_email failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

// 查 UID 的绑定邮箱；未绑定返回空串
std::string account_email_repo::get_email(int uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[account_email_repo] get_email: no DB connection." << std::endl;
        return "";
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("SELECT email FROM account_email WHERE UID = ?"));
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (rs->next()) {
            return rs->getString("email");
        }
        return "";
    } catch (const sql::SQLException& e) {
        std::cerr << "[account_email_repo] get_email failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return "";
    }
}
