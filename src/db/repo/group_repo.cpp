#include "group_repo.h"
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

namespace{
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

}

std::shared_ptr<group> group_repo::create_group(int owner_uid, const std::string& group_name) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] create_group: no DB connection." << std::endl;
        return nullptr;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO `Group` (owner_UID, name) VALUES (?, ?)"));
        pstmt->setInt(1, owner_uid);
        pstmt->setString(2, group_name);
        pstmt->execute();
        
        // 获取新创建的群聊的 ID
        int group_id = -1;
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery("SELECT LAST_INSERT_ID()"));
        if (rs->next()) {
            group_id = rs->getInt(1);
        }
        
        if (group_id <= 0) {
            std::cerr << "[group_repo] create_group: LAST_INSERT_ID invalid." << std::endl;
            return nullptr;
        }
        
        // 将群主添加到群成员列表中，name 默认取用户昵称(Account.nickname)，role=owner
        std::unique_ptr<sql::PreparedStatement> member_pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO Groupmember (group_UID, member_UID, name, role) "
                "SELECT ?, UID, nickname, 'owner' FROM Account WHERE UID = ?"));
        member_pstmt->setInt(1, group_id);
        member_pstmt->setInt(2, owner_uid);
        member_pstmt->execute();
        
        auto grp = std::make_shared<group>(owner_uid, group_name, group_id);
        return grp;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] create_group failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return nullptr;
    }
}

bool group_repo::delete_group(int group_uid, int requester_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] delete_group: no DB connection." << std::endl;
        return false;
    }
    try {
        // 检查请求者是否为群主
        std::unique_ptr<sql::PreparedStatement> check_pstmt(
            guard.get()->prepareStatement(
                "SELECT owner_UID FROM `Group` WHERE group_UID = ?"));
        check_pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
        if (!rs->next() || rs->getInt("owner_UID") != requester_uid) {
            std::cerr << "[group_repo] delete_group: requester is not the owner." << std::endl;
            return false; // 请求者不是群主
        }
        
        // 删除群聊及其成员关系（依赖外键 ON DELETE CASCADE）
        std::unique_ptr<sql::PreparedStatement> delete_pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM `Group` WHERE group_UID = ?"));
        delete_pstmt->setInt(1, group_uid);
        delete_pstmt->execute();
        
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] delete_group failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool group_repo::modify_group_name(int group_uid, int requester_uid, const std::string& new_group_name) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] modify_group_name: no DB connection." << std::endl;
        return false;
    }
    try {
        // 检查请求者是否为群主
        std::unique_ptr<sql::PreparedStatement> check_pstmt(
            guard.get()->prepareStatement(
                "SELECT owner_UID FROM `Group` WHERE group_UID = ?"));
        check_pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
        if (!rs->next() || rs->getInt("owner_UID") != requester_uid) {
            std::cerr << "[group_repo] modify_group_name: requester is not the owner." << std::endl;
            return false; // 请求者不是群主
        }
        
        // 修改群聊名称
        std::unique_ptr<sql::PreparedStatement> update_pstmt(
            guard.get()->prepareStatement(
                "UPDATE `Group` SET name = ? WHERE group_UID = ?"));
        update_pstmt->setString(1, new_group_name);
        update_pstmt->setInt(2, group_uid);
        update_pstmt->execute();
        
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] modify_group_name failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

std::shared_ptr<group> group_repo::load_group(int group_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] load_group: no DB connection." << std::endl;
        return nullptr;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT group_UID, owner_UID, name FROM `Group` WHERE group_UID = ?"));
        pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return nullptr; // 群聊不存在
        }
        auto grp = std::make_shared<group>(rs->getInt("owner_UID"),
                                           rs->getString("name"),
                                           rs->getInt("group_UID"));
        return grp;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] load_group failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return nullptr;
    }
}

std::vector<int> group_repo::get_group_members(int group_uid) {
    ConnGuard guard;
    std::vector<int> members;
    if (!guard) {
        std::cerr << "[group_repo] get_group_members: no DB connection." << std::endl;
        return members;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT member_UID FROM Groupmember WHERE group_UID = ?"));
        pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            members.push_back(rs->getInt("member_UID"));
        }
        return members;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] get_group_members failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return members;
    }
}

std::string group_repo::show_group_members(int group_uid) {
    ConnGuard guard;
    std::string result;
    if (!guard) {
        std::cerr << "[group_repo] show_group_members: no DB connection." << std::endl;
        return "DB unavailable.\n";
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT member_UID, name FROM Groupmember WHERE group_UID = ?"));
        pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result += std::to_string(rs->getInt("member_UID")) + ": "
                    + rs->getString("name") + "\n";
        }
        return result.empty() ? "No members found.\n" : result;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] show_group_members failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return result.empty() ? "No members found.\n" : result;
    }
}

// 查找某用户已加入的所有群
std::vector<int> group_repo::get_user_groups(int user_uid) {
    ConnGuard guard;
    std::vector<int> groups;
    if (!guard) {
        std::cerr << "[group_repo] get_user_groups: no DB connection." << std::endl;
        return groups;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT group_UID FROM Groupmember WHERE member_UID = ?"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            groups.push_back(rs->getInt("group_UID"));
        }
        return groups;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] get_user_groups failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return groups;
    }
}

// 展示某用户已加入的所有群（群ID: 群名）
std::string group_repo::show_user_groups(int user_uid) {
    ConnGuard guard;
    std::string result;
    if (!guard) {
        std::cerr << "[group_repo] show_user_groups: no DB connection." << std::endl;
        return "DB unavailable.\n";
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT gm.group_UID, g.name FROM Groupmember gm "
                "JOIN `Group` g ON gm.group_UID = g.group_UID "
                "WHERE gm.member_UID = ?"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            result += std::to_string(rs->getInt("group_UID")) + ": "
                    + rs->getString("name") + "\n";
        }
        return result.empty() ? "No groups found.\n" : result;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] show_user_groups failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return result.empty() ? "No groups found.\n" : result;
    }
}

// 群内成员(或群主)邀请外人加入群聊；新成员 name 默认取用户昵称(Account.nickname)
bool group_repo::member_add_group(int group_uid, int requester_uid, int new_member_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] member_add_group: no DB connection." << std::endl;
        return false;
    }
    try {
        // 校验：请求者必须是该群已有成员
        {
            std::unique_ptr<sql::PreparedStatement> check_pstmt(
                guard.get()->prepareStatement(
                    "SELECT 1 FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            check_pstmt->setInt(1, group_uid);
            check_pstmt->setInt(2, requester_uid);
            std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
            if (!rs->next()) {
                std::cerr << "[group_repo] member_add_group: requester is not a member." << std::endl;
                return false;
            }
        }
        // 防止重复入群（联合主键会拦截，这里先友好检查）
        {
            std::unique_ptr<sql::PreparedStatement> dup_pstmt(
                guard.get()->prepareStatement(
                    "SELECT 1 FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            dup_pstmt->setInt(1, group_uid);
            dup_pstmt->setInt(2, new_member_uid);
            std::unique_ptr<sql::ResultSet> rs(dup_pstmt->executeQuery());
            if (rs->next()) {
                std::cerr << "[group_repo] member_add_group: already a member." << std::endl;
                return false;
            }
        }
        // 插入新成员，name 默认取 Account.nickname
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO Groupmember (group_UID, member_UID, name, role) "
                "SELECT ?, UID, nickname, 'member' FROM Account WHERE UID = ?"));
        pstmt->setInt(1, group_uid);
        pstmt->setInt(2, new_member_uid);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] member_add_group failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 移出群成员，两种场景：
//   1. 自己退群：requester_uid == target_uid，任何成员均可（本人主动退出）
//   2. 群主踢人：requester_uid 必须是群主，把 target_uid 移出
bool group_repo::remove_group_member(int group_uid, int requester_uid, int target_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] remove_group_member: no DB connection." << std::endl;
        return false;
    }
    try {
        if (requester_uid != target_uid) {
            // 踢人场景：校验请求者必须是群主
            std::unique_ptr<sql::PreparedStatement> check_pstmt(
                guard.get()->prepareStatement(
                    "SELECT role FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            check_pstmt->setInt(1, group_uid);
            check_pstmt->setInt(2, requester_uid);
            std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
            if (!rs->next() || rs->getString("role") != "owner") {
                std::cerr << "[group_repo] remove_group_member: requester is not owner." << std::endl;
                return false;
            }
        }
        // 删除成员（若本就不在群中，executeUpdate=0 也视为成功）
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
        pstmt->setInt(1, group_uid);
        pstmt->setInt(2, target_uid);
        pstmt->executeUpdate();
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] remove_group_member failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 申请加入群聊：落库 relation_apply（apply_type=2, status=0 等待），
// 由群主/管理员调用 handle_join_request 处理。
bool group_repo::send_join_group(int group_uid, int requester_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] send_join_group: no DB connection." << std::endl;
        return false;
    }
    try {
        // 群存在性检查，并取出群主 UID（作为申请的接收方）
        int owner_uid = -1;
        {
            std::unique_ptr<sql::PreparedStatement> check_pstmt(
                guard.get()->prepareStatement(
                    "SELECT owner_UID FROM `Group` WHERE group_UID = ?"));
            check_pstmt->setInt(1, group_uid);
            std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
            if (!rs->next()) {
                std::cerr << "[group_repo] send_join_group: group not exists." << std::endl;
                return false;
            }
            owner_uid = rs->getInt("owner_UID");
        }
        // 已入群则拒绝
        {
            std::unique_ptr<sql::PreparedStatement> dup_pstmt(
                guard.get()->prepareStatement(
                    "SELECT 1 FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            dup_pstmt->setInt(1, group_uid);
            dup_pstmt->setInt(2, requester_uid);
            std::unique_ptr<sql::ResultSet> rs(dup_pstmt->executeQuery());
            if (rs->next()) {
                std::cerr << "[group_repo] send_join_group: already a member." << std::endl;
                return false;
            }
        }
        // 落库申请：receiver_UID=群主，group_UID=目标群，status=0 等待
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO relation_apply (apply_type, sender_UID, receiver_UID, group_UID, message, status) "
                "VALUES (2, ?, ?, ?, '', 0)"));
        pstmt->setInt(1, requester_uid);
        pstmt->setInt(2, owner_uid);
        pstmt->setInt(3, group_uid);
        pstmt->execute();
        return true;
    } catch (const sql::SQLException& e) {
        // 唯一约束 uk_apply：同一人同一群已有"等待中"申请时失败
        std::cerr << "[group_repo] send_join_group failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 处理入群申请（与 send_join_group 对应）：requester_uid 为群主/管理员，
// 处理 target_uid 对 group_uid 的申请；accept=true 同意（更新状态+拉人入群）/ false 拒绝（仅更新状态）
bool group_repo::handle_join_request(int group_uid, int requester_uid, int target_uid, bool accept) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] handle_join_request: no DB connection." << std::endl;
        return false;
    }
    try {
        // 校验：请求者必须是群主
        {
            std::unique_ptr<sql::PreparedStatement> check_pstmt(
                guard.get()->prepareStatement(
                    "SELECT role FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            check_pstmt->setInt(1, group_uid);
            check_pstmt->setInt(2, requester_uid);
            std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
            if (!rs->next() || rs->getString("role") != "owner") {
                std::cerr << "[group_repo] handle_join_request: requester is not owner." << std::endl;
                return false;
            }
        }
        // 更新申请状态：1=同意 2=拒绝（仅处理 status=0 的等待中申请）
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "UPDATE relation_apply SET status = ? "
                    "WHERE apply_type = 2 AND group_UID = ? AND sender_UID = ? AND status = 0"));
            pstmt->setInt(1, accept ? 1 : 2);
            pstmt->setInt(2, group_uid);
            pstmt->setInt(3, target_uid);
            pstmt->executeUpdate();
        }
        // 同意则拉人入群（name 默认取用户昵称）
        if (accept) {
            return member_add_group(group_uid, requester_uid, target_uid);
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] handle_join_request failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

// 修改群成员身份：promote=true 提升为 owner（群主），false 降回 member
bool group_repo::modify_member_role(int group_uid, int requester_uid, int target_uid, bool promote) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] modify_member_role: no DB connection." << std::endl;
        return false;
    }
    try {
        // 校验：请求者必须是群主
        {
            std::unique_ptr<sql::PreparedStatement> check_pstmt(
                guard.get()->prepareStatement(
                    "SELECT role FROM Groupmember WHERE group_UID = ? AND member_UID = ?"));
            check_pstmt->setInt(1, group_uid);
            check_pstmt->setInt(2, requester_uid);
            std::unique_ptr<sql::ResultSet> rs(check_pstmt->executeQuery());
            if (!rs->next() || rs->getString("role") != "owner") {
                std::cerr << "[group_repo] modify_member_role: requester is not owner." << std::endl;
                return false;
            }
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE Groupmember SET role = ? WHERE group_UID = ? AND member_UID = ?"));
        pstmt->setString(1, promote ? "owner" : "member");
        pstmt->setInt(2, group_uid);
        pstmt->setInt(3, target_uid);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] modify_member_role failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool group_repo::show_group_requests(int group_uid, std::vector<std::tuple<int, std::string>>& out_requests) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[group_repo] show_group_requests: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT sender_UID, message FROM relation_apply "
                "WHERE apply_type = 2 AND group_UID = ? AND status = 0"));
        pstmt->setInt(1, group_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            int sender_uid = rs->getInt("sender_UID");
            std::string message = rs->getString("message");
            out_requests.emplace_back(sender_uid, message);
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[group_repo] show_group_requests failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}