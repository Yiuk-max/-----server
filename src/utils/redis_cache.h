#pragma once
#include <optional>
#include <string>
#include <vector>

// ============================================================
// 缓存语义封装（Cache-Aside）：只面向业务语义，屏蔽 Redis 命令细节。
// enabled=false / 连接不可用时所有方法退化为「未命中 / no-op」，
// 调用方自然回退到 MySQL，缓存层不抛异常、不阻塞。
//
// 当前实现：
//   C1 群成员列表  key: chat:g:members:{group_uid}  (SET of uid)
//   C2 账号信息    key: chat:a:{uid}                (HASH: nickname/password/settings/language/token)
//   C4 邮箱→uid    key: chat:e:{email}              (STRING: uid，24h 兜底 TTL)
// ============================================================

// 账号缓存字段（与 Account 表主要列对齐）
struct AccountCache {
    std::string nickname;
    std::string password;
    std::string settings;   // settings JSON 原始字符串
    std::string language;
    std::string token;
    int         avatar_id = -1;
};

// 频道缓存字段（与 community 表普通频道行对齐）
struct ChannelCache {
    int         community_id = -1;  // core_channel_id
    std::string name;
    std::string category;
};

// 社区信息缓存字段（与 community 表核心频道行对齐）
struct CommunityCache {
    std::string name;
    std::string description;
    std::string avatar;
    std::string category;
    int         owner_uid = -1;
    int         avatar_id = -1;
    int         banner_id = -1;
};

class RedisCache {
public:
    static RedisCache& get_instance();

    // ---- C1 群成员列表 ----
    // 命中返回成员列表；未命中/不可用返回 nullopt（调用方查 DB 并回填）
    std::optional<std::vector<int>> group_members_get(int group_uid);//optional是C++17引入的一个模板类，用于表示一个可能存在也可能不存在的值。它可以用来表示函数的返回值可能为空的情况。
    // DB 查到后回填缓存
    void group_members_set(int group_uid, const std::vector<int>& members);
    // 成员变更时失效（写路径调用）
    void group_members_invalidate(int group_uid);

    // ---- C2 账号信息 ----
    // 命中返回账号字段；未命中/不可用返回 nullopt（调用方查 DB 并回填）
    std::optional<AccountCache> account_get(int uid);
    // DB 查到后回填缓存（HASH + 1h 兜底 TTL）
    void account_set(int uid, const AccountCache& a);
    // 账号变更/注销时失效（写路径调用）
    void account_invalidate(int uid);

    // ---- C4 邮箱 -> uid ----
    // 命中返回 uid；未命中/不可用返回 nullopt（调用方查 DB 并回填正映射）
    std::optional<int> email_get_uid(const std::string& email);
    // DB 查到正映射后回填（STRING + 24h 兜底 TTL）
    void email_set(const std::string& email, int uid);
    // 换绑时失效旧邮箱映射
    void email_invalidate(const std::string& email);

    // ---- C7 社区成员列表（chat:c:members:{community_id} SET） ----
    std::optional<std::vector<int>> community_members_get(int community_id);
    void community_members_set(int community_id, const std::vector<int>& members);
    void community_members_invalidate(int community_id);

    // ---- C8 频道信息（chat:c:channel:{channel_id} HASH） ----
    std::optional<ChannelCache> channel_get(int channel_id);
    void channel_set(int channel_id, const ChannelCache& c);
    void channel_invalidate(int channel_id);

    // ---- C9 社区频道列表（chat:c:channels:{community_id} ZSET，member=channel_id） ----
    std::optional<std::vector<int>> community_channels_get(int community_id);
    void community_channels_set(int community_id, const std::vector<int>& channel_ids);
    void community_channels_invalidate(int community_id);

    // ---- C10 用户社区/频道列表 ----
    std::optional<std::vector<int>> user_communities_get(int user_uid);
    void user_communities_set(int user_uid, const std::vector<int>& community_ids);
    void user_communities_invalidate(int user_uid);
    std::optional<std::vector<int>> user_channels_get(int user_uid);
    void user_channels_set(int user_uid, const std::vector<int>& channel_ids);
    void user_channels_invalidate(int user_uid);

    // ---- C11 社区信息（chat:c:info:{community_id} HASH） ----
    std::optional<CommunityCache> community_info_get(int community_id);
    void community_info_set(int community_id, const CommunityCache& c);
    void community_info_invalidate(int community_id);

private:
    RedisCache() = default;
    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;

    std::string key_group_members(int group_uid) const {
        return "chat:g:members:" + std::to_string(group_uid);
    }
    std::string key_account(int uid) const {
        return "chat:a:" + std::to_string(uid);
    }
    std::string key_email(const std::string& email) const {
        return "chat:e:" + email;
    }
    std::string key_community_members(int cid) const {
        return "chat:c:members:" + std::to_string(cid);
    }
    std::string key_channel(int chid) const {
        return "chat:c:channel:" + std::to_string(chid);
    }
    std::string key_community_channels(int cid) const {
        return "chat:c:channels:" + std::to_string(cid);
    }
    std::string key_user_communities(int uid) const {
        return "chat:c:ucommunities:" + std::to_string(uid);
    }
    std::string key_user_channels(int uid) const {
        return "chat:c:uchannels:" + std::to_string(uid);
    }
    std::string key_community_info(int cid) const {
        return "chat:c:info:" + std::to_string(cid);
    }
};
