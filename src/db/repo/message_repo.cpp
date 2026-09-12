#include "message_repo.h"
#include "mysql_conn_pool.h"
#include <memory>
#include <iostream>
#include <algorithm>
#include <limits>
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

// 历史查询专用：额外读取 JOIN 出来的发送者昵称与“回复的原消息摘要”。
message make_history_message(sql::ResultSet* rs) {
    message m = make_message(rs);
    m.sender_name = rs->isNull("sender_name") ? "" : rs->getString("sender_name");
    m.reply_to_message_id = rs->isNull("reply_to_message_id") ? 0 : rs->getInt("reply_to_message_id");
    m.reply_sender_name = rs->isNull("reply_sender_name") ? "" : rs->getString("reply_sender_name");
    m.reply_content = rs->isNull("reply_content") ? "" : rs->getString("reply_content");
    return m;
}
} // namespace

// 存储消息：type=2 群聊 / 1 私聊；私聊 receiver_UID=对方UID，群聊 receiver_UID=group_UID
// 成功返回数据库分配的 message_id，失败返回 -1
int message_repo::store_message(int sender_UID, int receiver_UID,
                                const std::string& content, bool is_group,
                                int reply_to_message_id) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] store_message: no DB connection." << std::endl;
        return -1;
    }
    try {
        const bool is_reply = reply_to_message_id > 0;
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                is_reply
                    ? "INSERT INTO message (type, sender_UID, receiver_UID, content, reply_to_message_id) VALUES (?, ?, ?, ?, ?)"
                    : "INSERT INTO message (type, sender_UID, receiver_UID, content) VALUES (?, ?, ?, ?)"));
        pstmt->setInt(1, is_group ? 2 : 1);
        pstmt->setInt(2, sender_UID);
        pstmt->setInt(3, receiver_UID);
        pstmt->setString(4, content);
        if (is_reply) {
            pstmt->setInt(5, reply_to_message_id);
        }
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
                "SELECT m.id, m.type, m.sender_UID, m.receiver_UID, m.content, m.send_time, "
                "       a.nickname AS sender_name, "
                "       m.reply_to_message_id, ra.nickname AS reply_sender_name, r.content AS reply_content "
                "FROM message m "
                "LEFT JOIN Account a  ON a.UID = m.sender_UID "
                "LEFT JOIN message r  ON r.id = m.reply_to_message_id "
                "LEFT JOIN Account ra ON ra.UID = r.sender_UID "
                "WHERE ((m.type = 1 AND m.receiver_UID = ? AND m.sender_UID != ?) "
                "   OR (m.type = 2 AND m.receiver_UID IN "
                "         (SELECT group_UID FROM Groupmember WHERE member_UID = ?) "
                "       AND m.sender_UID != ?)) "
                "  AND m.send_time > ? "
                "ORDER BY m.id ASC"));
        pstmt->setInt(1, receiver_UID);
        pstmt->setInt(2, receiver_UID);
        pstmt->setInt(3, receiver_UID);
        pstmt->setInt(4, receiver_UID);
        pstmt->setString(5, since);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result.push_back(make_history_message(rs.get()));
        }
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] get_offline_messages failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
    }
    return result;
}

// 游标分页查聊天历史：按 id 倒序取 limit+1 条，多出的一条用于判断 has_more，
// 最后反转为时间正序（旧→新）返回。id 倒序即时间倒序（id 自增保持发送顺序）。
bool message_repo::get_history_page(int self_uid, int peer_uid, bool is_group,
                                    int before_id, int limit,
                                    std::vector<message>& out, bool& has_more) {
    out.clear();
    has_more = false;
    if (limit <= 0) {
        limit = 10;
    }
    // before_id<=0（首屏）时用一个足够大的上界，等价于"取最新一页"
    const long long cursor = (before_id > 0)
        ? static_cast<long long>(before_id)
        : std::numeric_limits<long long>::max();

    ConnGuard guard;
    if (!guard) {
        std::cerr << "[message_repo] get_history_page: no DB connection." << std::endl;
        return false;
    }

    const char* sql_group =
        "SELECT m.id, m.type, m.sender_UID, m.receiver_UID, m.content, m.send_time, "
        "       a.nickname AS sender_name, "
        "       m.reply_to_message_id, ra.nickname AS reply_sender_name, r.content AS reply_content "
        "FROM message m "
        "LEFT JOIN Account a  ON a.UID = m.sender_UID "
        "LEFT JOIN message r  ON r.id = m.reply_to_message_id "
        "LEFT JOIN Account ra ON ra.UID = r.sender_UID "
        "WHERE m.type = 2 AND m.receiver_UID = ? AND m.id < ? "
        "ORDER BY m.id DESC LIMIT ?";
    const char* sql_private =
        "SELECT m.id, m.type, m.sender_UID, m.receiver_UID, m.content, m.send_time, "
        "       a.nickname AS sender_name, "
        "       m.reply_to_message_id, ra.nickname AS reply_sender_name, r.content AS reply_content "
        "FROM message m "
        "LEFT JOIN Account a  ON a.UID = m.sender_UID "
        "LEFT JOIN message r  ON r.id = m.reply_to_message_id "
        "LEFT JOIN Account ra ON ra.UID = r.sender_UID "
        "WHERE m.type = 1 "
        "  AND ((m.sender_UID = ? AND m.receiver_UID = ?) "
        "    OR (m.sender_UID = ? AND m.receiver_UID = ?)) "
        "  AND m.id < ? "
        "ORDER BY m.id DESC LIMIT ?";

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(is_group ? sql_group : sql_private));
        int idx = 1;
        if (is_group) {
            pstmt->setInt(idx++, peer_uid);
            pstmt->setInt64(idx++, cursor);
        } else {
            pstmt->setInt(idx++, self_uid);
            pstmt->setInt(idx++, peer_uid);
            pstmt->setInt(idx++, peer_uid);
            pstmt->setInt(idx++, self_uid);
            pstmt->setInt64(idx++, cursor);
        }
        pstmt->setInt(idx++, limit + 1); // 多取一条判断 has_more

        std::vector<message> desc;
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            desc.push_back(make_history_message(rs.get()));
        }
        if (static_cast<int>(desc.size()) > limit) {
            has_more = true;
            desc.pop_back(); // 丢弃多取的那条（最旧）
        }
        out.assign(desc.rbegin(), desc.rend()); // 反转为时间正序
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[message_repo] get_history_page failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}
