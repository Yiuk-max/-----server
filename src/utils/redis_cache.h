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
};
