#include "community_repo.h"

#include "mysql_conn_pool.h"
#include "redis_cache.h"

#include <iostream>
#include <memory>
#include <tuple>

#include "cppconn/exception.h"
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"

namespace {

// 连接 RAII：取池连接，作用域结束后归还。
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

// 查询成员在某社区的 role；非成员返回空串。异常交给调用方 try/catch。
std::string member_role(sql::Connection* conn, int community_id, int user_uid) {
    std::unique_ptr<sql::PreparedStatement> pstmt(
        conn->prepareStatement(
            "SELECT role FROM community_member WHERE community_id = ? AND user_UID = ?"));
    pstmt->setInt(1, community_id);
    pstmt->setInt(2, user_uid);
    std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
    if (rs->next()) {
        return rs->getString("role");
    }
    return "";
}

} // namespace

int community_repo::create_community(int owner_uid, const std::string& name,
                                     const std::string& description) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] create_community: no DB connection." << std::endl;
        return -1;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO community (name, is_core, description, owner_UID) "
                "VALUES (?, 1, ?, ?)"));
        pstmt->setString(1, name);
        pstmt->setString(2, description);
        pstmt->setInt(3, owner_uid);
        pstmt->execute();

        int community_id = -1;
        {
            std::unique_ptr<sql::ResultSet> rs(
                guard.get()->createStatement()->executeQuery("SELECT LAST_INSERT_ID()"));
            if (rs->next()) {
                community_id = rs->getInt(1);
            }
        }
        if (community_id <= 0) {
            std::cerr << "[community_repo] create_community: LAST_INSERT_ID invalid." << std::endl;
            return -1;
        }

        // 创建者成为社区 owner 成员
        std::unique_ptr<sql::PreparedStatement> member_pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO community_member (community_id, user_UID, nickname, role) "
                "VALUES (?, ?, NULL, 'owner')"));
        member_pstmt->setInt(1, community_id);
        member_pstmt->setInt(2, owner_uid);
        member_pstmt->execute();
        RedisCache::get_instance().user_communities_invalidate(owner_uid);
        return community_id;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] create_community failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

bool community_repo::delete_community(int community_id, int requester_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] delete_community: no DB connection." << std::endl;
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] delete_community: requester is not owner." << std::endl;
            return false;
        }
        // 收集频道 id 与成员 uid，用于删除后失效缓存
        std::vector<int> channel_ids;
        std::vector<int> member_uids;
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "SELECT id FROM community WHERE core_channel_id = ? AND is_core = 0"));
            pstmt->setInt(1, community_id);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            while (rs->next()) channel_ids.push_back(rs->getInt(1));
        }
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "SELECT user_UID FROM community_member WHERE community_id = ?"));
            pstmt->setInt(1, community_id);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            while (rs->next()) member_uids.push_back(rs->getInt(1));
        }
        // 先删该社区所有普通频道的消息（message 无外键级联）
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "DELETE FROM message WHERE type = 'channel' AND receiver_UID IN "
                    "(SELECT id FROM community WHERE core_channel_id = ? AND is_core = 0)"));
            pstmt->setInt(1, community_id);
            pstmt->executeUpdate();
        }
        // 删核心频道：普通频道（fk_community_core）与成员（fk_cm_community）级联删
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("DELETE FROM community WHERE id = ?"));
        pstmt->setInt(1, community_id);
        pstmt->executeUpdate();

        // 失效缓存
        RedisCache::get_instance().community_members_invalidate(community_id);
        RedisCache::get_instance().community_channels_invalidate(community_id);
        RedisCache::get_instance().community_info_invalidate(community_id);
        for (int chid : channel_ids) {
            RedisCache::get_instance().channel_invalidate(chid);
        }
        for (int uid : member_uids) {
            RedisCache::get_instance().user_communities_invalidate(uid);
            RedisCache::get_instance().user_channels_invalidate(uid);
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] delete_community failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::modify_community(int community_id, int requester_uid,
                                      const std::string& name,
                                      const std::string& description,
                                      const std::string& avatar) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] modify_community: no DB connection." << std::endl;
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] modify_community: requester is not owner." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE community SET name = ?, description = ?, avatar = ? "
                "WHERE id = ? AND is_core = 1"));
        pstmt->setString(1, name);
        pstmt->setString(2, description);
        pstmt->setString(3, avatar);
        pstmt->setInt(4, community_id);
        const bool ok = pstmt->executeUpdate() > 0;
        if (ok) {
            RedisCache::get_instance().community_info_invalidate(community_id);
        }
        return ok;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] modify_community failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::is_community_member(int community_id, int user_uid) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        return !member_role(guard.get(), community_id, user_uid).empty();
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] is_community_member failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> community_repo::get_user_communities(int user_uid) {
    if (auto cached = RedisCache::get_instance().user_communities_get(user_uid)) {
        return *cached;
    }
    ConnGuard guard;
    std::vector<int> out;
    if (!guard) {
        return out;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT c.id FROM community_member cm "
                "JOIN community c ON c.id = cm.community_id "
                "WHERE cm.user_UID = ? AND c.is_core = 1 ORDER BY c.id"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(rs->getInt(1));
        }
        RedisCache::get_instance().user_communities_set(user_uid, out);
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_user_communities failed: " << e.what() << std::endl;
    }
    return out;
}

bool community_repo::get_user_communities_info(int user_uid,
                                               std::vector<community_info>& out) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT c.id, c.name, c.description, c.avatar, c.category, c.owner_UID, cm.role "
                "FROM community_member cm "
                "JOIN community c ON c.id = cm.community_id "
                "WHERE cm.user_UID = ? AND c.is_core = 1 ORDER BY c.id"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            community_info info;
            info.community_id = rs->getInt("id");
            info.name         = rs->getString("name");
            info.description  = rs->isNull("description") ? "" : rs->getString("description");
            info.avatar       = rs->isNull("avatar") ? "" : rs->getString("avatar");
            info.category     = rs->isNull("category") ? "" : rs->getString("category");
            info.owner_uid    = rs->isNull("owner_UID") ? -1 : rs->getInt("owner_UID");
            info.my_role      = rs->getString("role");
            out.push_back(std::move(info));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_user_communities_info failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::get_community_info(int community_id, community_info& out) {
    if (auto cached = RedisCache::get_instance().community_info_get(community_id)) {
        out.community_id = community_id;
        out.name         = cached->name;
        out.description  = cached->description;
        out.avatar       = cached->avatar;
        out.category     = cached->category;
        out.owner_uid    = cached->owner_uid;
        return true;
    }
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, name, description, avatar, category, owner_UID FROM community "
                "WHERE id = ? AND is_core = 1"));
        pstmt->setInt(1, community_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return false;
        }
        out.community_id = rs->getInt("id");
        out.name         = rs->getString("name");
        out.description  = rs->isNull("description") ? "" : rs->getString("description");
        out.avatar       = rs->isNull("avatar") ? "" : rs->getString("avatar");
        out.category     = rs->isNull("category") ? "" : rs->getString("category");
        out.owner_uid    = rs->isNull("owner_UID") ? -1 : rs->getInt("owner_UID");

        CommunityCache cc;
        cc.name        = out.name;
        cc.description = out.description;
        cc.avatar      = out.avatar;
        cc.category    = out.category;
        cc.owner_uid   = out.owner_uid;
        RedisCache::get_instance().community_info_set(community_id, cc);
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_community_info failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::get_community_channels(int community_id,
                                            std::vector<channel_info>& out) {
    // C9 频道列表缓存：命中后逐个取 C8 频道信息
    if (auto ids = RedisCache::get_instance().community_channels_get(community_id)) {
        bool all_hit = true;
        for (int id : *ids) {
            auto ch = RedisCache::get_instance().channel_get(id);
            if (!ch) {
                all_hit = false;
                break;
            }
            channel_info info;
            info.channel_id   = id;
            info.community_id = ch->community_id;
            info.name         = ch->name;
            info.category     = ch->category;
            out.push_back(std::move(info));
        }
        if (all_hit) {
            return true;
        }
        out.clear();
    }

    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, core_channel_id, name, category FROM community "
                "WHERE core_channel_id = ? AND is_core = 0 ORDER BY category, id"));
        pstmt->setInt(1, community_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        std::vector<int> id_list;
        while (rs->next()) {
            channel_info ch;
            ch.channel_id   = rs->getInt("id");
            ch.community_id = rs->getInt("core_channel_id");
            ch.name         = rs->getString("name");
            ch.category     = rs->isNull("category") ? "" : rs->getString("category");
            out.push_back(ch);

            ChannelCache cc;
            cc.community_id = ch.community_id;
            cc.name         = ch.name;
            cc.category     = ch.category;
            RedisCache::get_instance().channel_set(ch.channel_id, cc);
            id_list.push_back(ch.channel_id);
        }
        RedisCache::get_instance().community_channels_set(community_id, id_list);
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_community_channels failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> community_repo::get_user_channels(int user_uid) {
    if (auto cached = RedisCache::get_instance().user_channels_get(user_uid)) {
        return *cached;
    }
    ConnGuard guard;
    std::vector<int> out;
    if (!guard) {
        return out;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT ch.id FROM community ch "
                "JOIN community_member cm ON cm.community_id = ch.core_channel_id "
                "WHERE cm.user_UID = ? AND ch.is_core = 0 ORDER BY ch.id"));
        pstmt->setInt(1, user_uid);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(rs->getInt(1));
        }
        RedisCache::get_instance().user_channels_set(user_uid, out);
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_user_channels failed: " << e.what() << std::endl;
    }
    return out;
}

bool community_repo::get_community_members(int community_id,
                                           std::vector<community_member_info>& out) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT cm.user_UID, COALESCE(cm.nickname, a.nickname) AS nickname, cm.role "
                "FROM community_member cm "
                "LEFT JOIN Account a ON a.UID = cm.user_UID "
                "WHERE cm.community_id = ? ORDER BY cm.join_time, cm.user_UID"));
        pstmt->setInt(1, community_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            community_member_info m;
            m.user_uid = rs->getInt("user_UID");
            m.nickname = rs->isNull("nickname") ? "" : rs->getString("nickname");
            m.role     = rs->getString("role");
            out.push_back(std::move(m));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_community_members failed: " << e.what() << std::endl;
        return false;
    }
}

int community_repo::create_channel(int community_id, int requester_uid,
                                   const std::string& name, const std::string& category) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] create_channel: no DB connection." << std::endl;
        return -1;
    }
    try {
        // 校验请求者是社区成员 + 取社区 owner
        int owner_uid = -1;
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "SELECT owner_UID FROM community WHERE id = ? AND is_core = 1"));
            pstmt->setInt(1, community_id);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            if (!rs->next()) {
                std::cerr << "[community_repo] create_channel: community not exists." << std::endl;
                return -1;
            }
            owner_uid = rs->getInt("owner_UID");
        }
        if (member_role(guard.get(), community_id, requester_uid).empty()) {
            std::cerr << "[community_repo] create_channel: requester is not a member." << std::endl;
            return -1;
        }
        // 插入普通频道（不写成员，成员继承社区）
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO community (name, is_core, core_channel_id, owner_UID, category) "
                "VALUES (?, 0, ?, ?, ?)"));
        pstmt->setString(1, name);
        pstmt->setInt(2, community_id);
        pstmt->setInt(3, owner_uid);
        pstmt->setString(4, category);
        pstmt->execute();

        int channel_id = -1;
        {
            std::unique_ptr<sql::ResultSet> rs(
                guard.get()->createStatement()->executeQuery("SELECT LAST_INSERT_ID()"));
            if (rs->next()) {
                channel_id = rs->getInt(1);
            }
        }
        if (channel_id > 0) {
            RedisCache::get_instance().community_channels_invalidate(community_id);
        }
        return channel_id > 0 ? channel_id : -1;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] create_channel failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

bool community_repo::delete_channel(int community_id, int channel_id, int requester_uid) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] delete_channel: no DB connection." << std::endl;
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] delete_channel: requester is not owner." << std::endl;
            return false;
        }
        // 删频道消息
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "DELETE FROM message WHERE type = 'channel' AND receiver_UID = ?"));
            pstmt->setInt(1, channel_id);
            pstmt->executeUpdate();
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM community WHERE id = ? AND is_core = 0 AND core_channel_id = ?"));
        pstmt->setInt(1, channel_id);
        pstmt->setInt(2, community_id);
        const bool ok = pstmt->executeUpdate() > 0;
        if (ok) {
            RedisCache::get_instance().channel_invalidate(channel_id);
            RedisCache::get_instance().community_channels_invalidate(community_id);
        }
        return ok;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] delete_channel failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::modify_channel(int community_id, int channel_id, int requester_uid,
                                    const std::string& name, const std::string& category) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] modify_channel: no DB connection." << std::endl;
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] modify_channel: requester is not owner." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE community SET name = ?, category = ? "
                "WHERE id = ? AND is_core = 0 AND core_channel_id = ?"));
        pstmt->setString(1, name);
        pstmt->setString(2, category);
        pstmt->setInt(3, channel_id);
        pstmt->setInt(4, community_id);
        const bool ok = pstmt->executeUpdate() > 0;
        if (ok) {
            RedisCache::get_instance().channel_invalidate(channel_id);
            RedisCache::get_instance().community_channels_invalidate(community_id);
        }
        return ok;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] modify_channel failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::get_channel_community(int channel_id, int& out_community_id) {
    // C8 频道信息缓存
    if (auto cached = RedisCache::get_instance().channel_get(channel_id)) {
        out_community_id = cached->community_id;
        return out_community_id > 0;
    }

    ConnGuard guard;
    if (!guard) {
        std::cerr << "[community_repo] get_channel_community: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT core_channel_id, name, category FROM community WHERE id = ? AND is_core = 0"));
        pstmt->setInt(1, channel_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next() || rs->isNull("core_channel_id")) {
            return false;
        }
        out_community_id = rs->getInt("core_channel_id");
        ChannelCache c;
        c.community_id = out_community_id;
        c.name         = rs->getString("name");
        c.category     = rs->isNull("category") ? "" : rs->getString("category");
        RedisCache::get_instance().channel_set(channel_id, c);
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_channel_community failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::add_member(int community_id, int requester_uid, int target_uid) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] add_member: requester is not owner." << std::endl;
            return false;
        }
        if (!member_role(guard.get(), community_id, target_uid).empty()) {
            std::cerr << "[community_repo] add_member: already a member." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO community_member (community_id, user_UID, nickname, role) "
                "VALUES (?, ?, NULL, 'member')"));
        pstmt->setInt(1, community_id);
        pstmt->setInt(2, target_uid);
        const bool ok = pstmt->executeUpdate() > 0;
        if (ok) {
            RedisCache::get_instance().community_members_invalidate(community_id);
            RedisCache::get_instance().user_communities_invalidate(target_uid);
            RedisCache::get_instance().user_channels_invalidate(target_uid);
        }
        return ok;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] add_member failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::remove_member(int community_id, int requester_uid, int target_uid) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (requester_uid != target_uid &&
            member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] remove_member: requester is not owner." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM community_member WHERE community_id = ? AND user_UID = ?"));
        pstmt->setInt(1, community_id);
        pstmt->setInt(2, target_uid);
        pstmt->executeUpdate();
        RedisCache::get_instance().community_members_invalidate(community_id);
        RedisCache::get_instance().user_communities_invalidate(target_uid);
        RedisCache::get_instance().user_channels_invalidate(target_uid);
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] remove_member failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::leave(int community_id, int user_uid) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, user_uid) == "owner") {
            std::cerr << "[community_repo] leave: owner cannot leave." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "DELETE FROM community_member WHERE community_id = ? AND user_UID = ?"));
        pstmt->setInt(1, community_id);
        pstmt->setInt(2, user_uid);
        pstmt->executeUpdate();
        RedisCache::get_instance().community_members_invalidate(community_id);
        RedisCache::get_instance().user_communities_invalidate(user_uid);
        RedisCache::get_instance().user_channels_invalidate(user_uid);
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] leave failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::send_join_request(int community_id, int requester_uid,
                                       const std::string& message) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        // 取社区 owner 作为接收方
        int owner_uid = -1;
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "SELECT owner_UID FROM community WHERE id = ? AND is_core = 1"));
            pstmt->setInt(1, community_id);
            std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
            if (!rs->next()) {
                std::cerr << "[community_repo] send_join_request: community not exists." << std::endl;
                return false;
            }
            owner_uid = rs->getInt("owner_UID");
        }
        if (!member_role(guard.get(), community_id, requester_uid).empty()) {
            std::cerr << "[community_repo] send_join_request: already a member." << std::endl;
            return false;
        }
        // 落库申请：apply_type=3，group_UID 列存 community_id，receiver_UID=owner
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO relation_apply (apply_type, sender_UID, receiver_UID, group_UID, message, status) "
                "VALUES (3, ?, ?, ?, ?, 0)"));
        pstmt->setInt(1, requester_uid);
        pstmt->setInt(2, owner_uid);
        pstmt->setInt(3, community_id);
        pstmt->setString(4, message);
        pstmt->execute();
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] send_join_request failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::handle_join_request(int community_id, int requester_uid,
                                         int target_uid, bool accept) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] handle_join_request: requester is not owner." << std::endl;
            return false;
        }
        {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "DELETE FROM relation_apply "
                    "WHERE apply_type = 3 AND group_UID = ? AND sender_UID = ? AND status = 0"));
            pstmt->setInt(1, community_id);
            pstmt->setInt(2, target_uid);
            if (pstmt->executeUpdate() == 0) {
                return false; // 无等待中的申请
            }
        }
        if (accept) {
            std::unique_ptr<sql::PreparedStatement> pstmt(
                guard.get()->prepareStatement(
                    "INSERT INTO community_member (community_id, user_UID, nickname, role) "
                    "VALUES (?, ?, NULL, 'member')"));
            pstmt->setInt(1, community_id);
            pstmt->setInt(2, target_uid);
            const bool ok = pstmt->executeUpdate() > 0;
            if (ok) {
                RedisCache::get_instance().community_members_invalidate(community_id);
                RedisCache::get_instance().user_communities_invalidate(target_uid);
                RedisCache::get_instance().user_channels_invalidate(target_uid);
            }
            return ok;
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] handle_join_request failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool community_repo::modify_member_role(int community_id, int requester_uid,
                                        int target_uid, bool promote) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] modify_member_role: requester is not owner." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE community_member SET role = ? WHERE community_id = ? AND user_UID = ?"));
        pstmt->setString(1, promote ? "admin" : "member");
        pstmt->setInt(2, community_id);
        pstmt->setInt(3, target_uid);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] modify_member_role failed: " << e.what() << std::endl;
        return false;
    }
}

bool community_repo::show_requests(int community_id, int requester_uid,
                                   std::vector<std::tuple<int, std::string>>& out) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        if (member_role(guard.get(), community_id, requester_uid) != "owner") {
            std::cerr << "[community_repo] show_requests: requester is not owner." << std::endl;
            return false;
        }
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT sender_UID, message FROM relation_apply "
                "WHERE apply_type = 3 AND group_UID = ? AND status = 0 ORDER BY create_time"));
        pstmt->setInt(1, community_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.emplace_back(rs->getInt("sender_UID"),
                             rs->isNull("message") ? "" : rs->getString("message"));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] show_requests failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> community_repo::get_channel_members(int channel_id) {
    int community_id = -1;
    if (!get_channel_community(channel_id, community_id)) {
        return {};
    }
    // C7 社区成员缓存（频道成员=社区成员，一份共享）
    if (auto cached = RedisCache::get_instance().community_members_get(community_id)) {
        return *cached;
    }

    ConnGuard guard;
    std::vector<int> out;
    if (!guard) {
        return out;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT user_UID FROM community_member WHERE community_id = ?"));
        pstmt->setInt(1, community_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(rs->getInt(1));
        }
        RedisCache::get_instance().community_members_set(community_id, out);
    } catch (const sql::SQLException& e) {
        std::cerr << "[community_repo] get_channel_members failed: " << e.what() << std::endl;
    }
    return out;
}
