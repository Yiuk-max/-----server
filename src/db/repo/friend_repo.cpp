#include "friend_repo.h"
#include "mysql_conn_pool.h"
#include <iostream>
#include <memory>
#include <tuple>

// JDBC 类型定义（PreparedStatement / ResultSet / Statement）由 mysql 驱动递送不保证完整，
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
} // namespace

// 建立好友：事务内双写 (uid_a, uid_b) 与 (uid_b, uid_a)，任一行失败则回滚。
bool friend_repo::add_friend(int uid_a, int uid_b) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] add_friend: no DB connection." << std::endl;
        return false;
    }
    sql::Connection* conn = guard.get();
    try {
        conn->setAutoCommit(false);
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement(
                    "INSERT INTO friend_relation (UID, friend_UID, remark_name) VALUES (?, ?, NULL)"));
            pstmt->setInt(1, uid_a);
            pstmt->setInt(2, uid_b);
            pstmt->execute();
        }
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement(
                    "INSERT INTO friend_relation (UID, friend_UID, remark_name) VALUES (?, ?, NULL)"));
            pstmt->setInt(1, uid_b);
            pstmt->setInt(2, uid_a);
            pstmt->execute();
        }
        conn->commit();
        conn->setAutoCommit(true);
        return true;
    } catch (const sql::SQLException& e) {
        try { conn->rollback(); } catch (...) {}
        try { conn->setAutoCommit(true); } catch (...) {}
        std::cerr << "[friend_repo] add_friend failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 删除好友：事务内同时删除两行；若本就不存在也视为成功。
bool friend_repo::remove_friend(int uid_a, int uid_b) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] remove_friend: no DB connection." << std::endl;
        return false;
    }
    sql::Connection* conn = guard.get();
    try {
        conn->setAutoCommit(false);
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement(
                    "DELETE FROM friend_relation WHERE UID = ? AND friend_UID = ?"));
            pstmt->setInt(1, uid_a);
            pstmt->setInt(2, uid_b);
            pstmt->executeUpdate();
        }
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                conn->prepareStatement(
                    "DELETE FROM friend_relation WHERE UID = ? AND friend_UID = ?"));
            pstmt->setInt(1, uid_b);
            pstmt->setInt(2, uid_a);
            pstmt->executeUpdate();
        }
        conn->commit();
        conn->setAutoCommit(true);
        return true;
    } catch (const sql::SQLException& e) {
        try { conn->rollback(); } catch (...) {}
        try { conn->setAutoCommit(true); } catch (...) {}
        std::cerr << "[friend_repo] remove_friend failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 查询某用户的所有好友 UID
std::vector<int> friend_repo::get_friend_list(int uid) {
    std::vector<int> result;
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] get_friend_list: no DB connection." << std::endl;
        return result;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT friend_UID FROM friend_relation WHERE UID = ?"));
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result.push_back(rs->getInt("friend_UID"));
        }
    } catch (const sql::SQLException& e) {
        std::cerr << "[friend_repo] get_friend_list failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
    return result;
}

// 判断是否已是好友：存在 (A,B) 或 (B,A) 任一行即视为好友
bool friend_repo::is_friend(int uid_a, int uid_b) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] is_friend: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT COUNT(*) FROM friend_relation "
                "WHERE (UID = ? AND friend_UID = ?) OR (UID = ? AND friend_UID = ?)"));
        pstmt->setInt(1, uid_a);
        pstmt->setInt(2, uid_b);
        pstmt->setInt(3, uid_b);
        pstmt->setInt(4, uid_a);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (rs->next()) {
            return rs->getInt(1) > 0;
        }
    } catch (const sql::SQLException& e) {
        std::cerr << "[friend_repo] is_friend failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
    return false;
}

// 设置备注名：UPDATE remark_name WHERE UID=? AND friend_UID=?
bool friend_repo::set_remark(int uid, int friend_uid, const std::string& remark) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] set_remark: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE friend_relation SET remark_name = ? WHERE UID = ? AND friend_UID = ?"));
        pstmt->setString(1, remark);
        pstmt->setInt(2, uid);
        pstmt->setInt(3, friend_uid);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[friend_repo] set_remark failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 发送好友申请：INSERT friend_request，status 默认 0（等待）
bool friend_repo::send_friend_request(int sender_uid, int receiver_uid,
                                      const std::string& message) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] send_friend_request: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO friend_request (sender_UID, receiver_UID, message, status) "
                "VALUES (?, ?, ?, 0)"));
        pstmt->setInt(1, sender_uid);
        pstmt->setInt(2, receiver_uid);
        pstmt->setString(3, message);
        pstmt->execute();
        return true;
    } catch (const sql::SQLException& e) {
        // 唯一约束 uk_sender_receiver_status：同对用户已存在"等待中"申请时失败
        std::cerr << "[friend_repo] send_friend_request failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 查询发给 user_uid 且 status=0 的等待中申请，输出 (sender_UID, message) 列表
bool friend_repo::get_friend_requests(int user_uid,
                                      std::vector<std::tuple<int, std::string>>& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] get_friend_requests: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT sender_UID, message FROM friend_request "
                "WHERE receiver_UID = ? AND status = 0 ORDER BY create_time ASC"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            int sender_uid = rs->getInt("sender_UID");
            // message 可空，NULL 时回退为空串
            std::string msg = rs->isNull("message") ? "" : rs->getString("message");
            out.emplace_back(sender_uid, msg);
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[friend_repo] get_friend_requests failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 处理申请：accept=true 置 status=1（同意）/ false 置 status=2（拒绝）
bool friend_repo::handle_friend_request(int sender_uid, int receiver_uid, bool accept) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[friend_repo] handle_friend_request: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE friend_request SET status = ? "
                "WHERE sender_UID = ? AND receiver_UID = ? AND status = 0"));
        pstmt->setInt(1, accept ? 1 : 2);
        pstmt->setInt(2, sender_uid);
        pstmt->setInt(3, receiver_uid);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[friend_repo] handle_friend_request failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}
