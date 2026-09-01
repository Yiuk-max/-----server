#include "message_repo.h"
#include "mysql_conn_pool.h"
#include <memory>
#include <iostream>
#include <algorithm>
// JDBC 类型定义（PreparedStatement / ResultSet / Statement）由 mysql 驱动递送不保证完整，
// 显式引入以确保类型完整可用。
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"
#include "cppconn/exception.h"

namespace {
// 连接 RAII：取池连接，作用域结束后归还，异常安全。
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

// 从 ResultSet 当前行构造 message 对象（列名与 sql/create_table.sql 的 message 表严格一致）
message make_message(sql::ResultSet* rs) {
    message m;
    m.message_id   = rs->getInt("id");
    m.sender_UID   = rs->getInt("sender_UID");
    m.receiver_UID = rs->getInt("receiver_UID");
    m.content      = rs->getString("content");
    m.is_group     = (rs->getInt("type") == 2); // type: 1=私聊 2=群聊
    m.timestamp    = rs->getString("send_time");
    return m;
}
} // namespace

// 存储消息：type=2 群聊 / 1 私聊；私聊 receiver_UID=对方UID，群聊 receiver_UID=group_UID
// 成功返回数据库分配的 message_id，失败返回 -1
int message_repo::store_message(int sender_UID, int receiver_UID,
                                const std::string& content, bool is_group) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] store_message: no DB connection." << std::endl;
        return -1;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO message (type, sender_UID, receiver_UID, content) VALUES (?, ?, ?, ?)"));
        pstmt->setInt(1, is_group ? 2 : 1);
        pstmt->setInt(2, sender_UID);
        pstmt->setInt(3, receiver_UID);
        pstmt->setString(4, content);
        pstmt->executeUpdate();

        // 取回 AUTO_INCREMENT 分配的 message_id
        int message_id = -1;
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery("SELECT LAST_INSERT_ID()"));
        if (rs->next()) {
            message_id = rs->getInt(1);
        }
        return message_id > 0 ? message_id : -1;
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] store_message failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

// 按 id 查询单条消息；不存在返回 false
bool message_repo::get_message(int message_id, message& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] get_message: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, type, sender_UID, receiver_UID, content, send_time FROM message WHERE id = ?"));
        pstmt->setInt(1, message_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return false;
        }
        out = make_message(rs.get());
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] get_message failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 删除单条消息：DELETE WHERE id=?
bool message_repo::delete_message(int message_id) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] delete_message: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("DELETE FROM message WHERE id = ?"));
        pstmt->setInt(1, message_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] delete_message failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 定时清理：删除发送时间超过 7 天的消息（无过期消息也视为成功）
bool message_repo::delete_expired_messages() {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] delete_expired_messages: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM message WHERE send_time < (NOW() - INTERVAL 7 DAY)"));
        pstmt->executeUpdate();
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] delete_expired_messages failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 查询自 since_time 之后发给该用户的离线消息：
//   - 私聊：receiver_UID = 本人 且 非本人发送
//   - 群聊：receiver_UID 是本人已加入的群 且 非本人发送
// since_time 为空时按 1970-01-01 00:00:00 处理（即返回全部）；按时间正序返回。
std::vector<message> message_repo::get_offline_messages(int receiver_UID, const std::string& since_time) {
    std::vector<message> result;
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] get_offline_messages: no DB connection." << std::endl;
        return result;
    }
    std::string since = since_time.empty() ? "1970-01-01 00:00:00" : since_time;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, type, sender_UID, receiver_UID, content, send_time FROM message "
                "WHERE ((type = 1 AND receiver_UID = ? AND sender_UID != ?) "
                "   OR (type = 2 AND receiver_UID IN "
                "         (SELECT group_UID FROM Groupmember WHERE member_UID = ?) "
                "       AND sender_UID != ?)) "
                "  AND send_time > ? "
                "ORDER BY id ASC"));
        pstmt->setInt(1, receiver_UID);
        pstmt->setInt(2, receiver_UID);
        pstmt->setInt(3, receiver_UID);
        pstmt->setInt(4, receiver_UID);
        pstmt->setString(5, since);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result.push_back(make_message(rs.get()));
        }
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] get_offline_messages failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
    return result;
}

// 查询两个用户之间的私聊历史：取最近 limit 条，按时间正序（旧→新）返回
std::vector<message> message_repo::get_chat_history(int user1_UID, int user2_UID, int limit) {
    std::vector<message> result;
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] get_chat_history: no DB connection." << std::endl;
        return result;
    }
    if (limit <= 0) {
        limit = 20; // 默认最近 20 条
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, type, sender_UID, receiver_UID, content, send_time FROM message "
                "WHERE type = 1 "
                "  AND ((sender_UID = ? AND receiver_UID = ?) "
                "    OR (sender_UID = ? AND receiver_UID = ?)) "
                "ORDER BY id DESC LIMIT ?"));
        pstmt->setInt(1, user1_UID);
        pstmt->setInt(2, user2_UID);
        pstmt->setInt(3, user2_UID);
        pstmt->setInt(4, user1_UID);
        pstmt->setInt(5, limit);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result.push_back(make_message(rs.get()));
        }
        // 上面按 id 倒序取回最近 limit 条，这里反转为时间正序（旧→新）
        std::reverse(result.begin(), result.end());
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] get_chat_history failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
    return result;
}
