#include "redis_cache.h"

#include "redis_client.h"

#include <sw/redis++/redis.h>

#include <chrono>
#include <unordered_map>

RedisCache& RedisCache::get_instance() {
    static RedisCache instance;
    return instance;
}

std::optional<std::vector<int>> RedisCache::group_members_get(int group_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;  // 未启用/不可用：走 DB
    }
    try {
        std::vector<std::string> members;
        redis->smembers(key_group_members(group_uid), std::back_inserter(members));//back_inserter(members) 直接将结果插入到 members 中
        if (members.empty()) {
            // 空集合 = key 不存在（群必有成员，不可能为空）
            return std::nullopt;
        }
        std::vector<int> out;
        out.reserve(members.size());
        for (const auto& m : members) {
            try {
                out.push_back(std::stoi(m));//std::stoi 将字符串转换为整数
            } catch (const std::exception&) {
                // 脏数据：忽略该条
            }
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;  // Redis 异常：降级
    }
}

void RedisCache::group_members_set(int group_uid, const std::vector<int>& members) {
    if (members.empty()) {
        return;
    }
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        std::vector<std::string> vals;
        vals.reserve(members.size());
        for (int m : members) {
            vals.push_back(std::to_string(m));
        }
        redis->sadd(key_group_members(group_uid), vals.begin(), vals.end());
    } catch (const std::exception&) {
        // 忽略：失败下次读未命中回源即可
    }
}

void RedisCache::group_members_invalidate(int group_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_group_members(group_uid));
    } catch (const std::exception&) {
        // 忽略：兜底 TTL/下次读回源可自愈
    }
}

// ---------------- C2 账号信息 ----------------

std::optional<AccountCache> RedisCache::account_get(int uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;
    }
    try {
        std::unordered_map<std::string, std::string> m;
        redis->hgetall(key_account(uid), std::inserter(m, m.begin()));
        if (m.empty()) {
            return std::nullopt;  // key 不存在
        }
        AccountCache a;
        auto it = m.find("nickname"); if (it != m.end()) a.nickname = it->second;
        it = m.find("password");       if (it != m.end()) a.password = it->second;
        it = m.find("settings");       if (it != m.end()) a.settings = it->second;
        it = m.find("language");       if (it != m.end()) a.language = it->second;
        it = m.find("token");          if (it != m.end()) a.token = it->second;
        return a;
    } catch (const std::exception&) {
        return std::nullopt;  // Redis 异常：降级
    }
}

void RedisCache::account_set(int uid, const AccountCache& a) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        std::vector<std::pair<std::string, std::string>> fields = {
            {"nickname", a.nickname},
            {"password", a.password},
            {"settings", a.settings},
            {"language", a.language},
            {"token", a.token},
        };
        redis->hset(key_account(uid), fields.begin(), fields.end());
        redis->expire(key_account(uid), std::chrono::seconds(3600));  // 兜底 TTL
    } catch (const std::exception&) {
        // 忽略：失败下次读未命中回源即可
    }
}

void RedisCache::account_invalidate(int uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_account(uid));
    } catch (const std::exception&) {
        // 忽略：兜底 TTL 可自愈
    }
}

// ---------------- C4 邮箱 -> uid ----------------

std::optional<int> RedisCache::email_get_uid(const std::string& email) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;
    }
    try {
        auto v = redis->get(key_email(email));  // OptionalString
        if (!v) {
            return std::nullopt;  // key 不存在
        }
        try {
            return std::stoi(*v);
        } catch (const std::exception&) {
            return std::nullopt;  // 脏数据
        }
    } catch (const std::exception&) {
        return std::nullopt;  // Redis 异常：降级
    }
}

void RedisCache::email_set(const std::string& email, int uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->set(key_email(email), std::to_string(uid));
        redis->expire(key_email(email), std::chrono::seconds(86400));  // 24h 兜底 TTL
    } catch (const std::exception&) {
        // 忽略：失败下次读未命中回源即可
    }
}

void RedisCache::email_invalidate(const std::string& email) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_email(email));
    } catch (const std::exception&) {
        // 忽略：兜底 TTL 可自愈
    }
}
