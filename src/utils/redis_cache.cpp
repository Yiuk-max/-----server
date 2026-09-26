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
        it = m.find("avatar_id");      if (it != m.end()) a.avatar_id = std::stoi(it->second);
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
            {"avatar_id", std::to_string(a.avatar_id)},
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

// ============================================================
// C7 社区成员列表（SET，无 TTL + 主动失效；空集合=不存在）
// ============================================================

std::optional<std::vector<int>> RedisCache::community_members_get(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;
    }
    try {
        std::vector<std::string> members;
        redis->smembers(key_community_members(community_id), std::back_inserter(members));
        if (members.empty()) {
            return std::nullopt;
        }
        std::vector<int> out;
        out.reserve(members.size());
        for (const auto& m : members) {
            try {
                out.push_back(std::stoi(m));
            } catch (const std::exception&) {
                // 脏数据：忽略该条
            }
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::community_members_set(int community_id, const std::vector<int>& members) {
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
        redis->sadd(key_community_members(community_id), vals.begin(), vals.end());
    } catch (const std::exception&) {
        // 忽略：失败下次读未命中回源即可
    }
}

void RedisCache::community_members_invalidate(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_community_members(community_id));
    } catch (const std::exception&) {
        // 忽略
    }
}

// ============================================================
// C8 频道信息（HASH + 24h 兜底 TTL）
// ============================================================

std::optional<ChannelCache> RedisCache::channel_get(int channel_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;
    }
    try {
        std::unordered_map<std::string, std::string> m;
        redis->hgetall(key_channel(channel_id), std::inserter(m, m.begin()));
        if (m.empty()) {
            return std::nullopt;
        }
        ChannelCache c;
        auto it = m.find("community_id");
        if (it != m.end()) {
            try { c.community_id = std::stoi(it->second); } catch (const std::exception&) {}
        }
        it = m.find("name");
        if (it != m.end()) c.name = it->second;
        it = m.find("category");
        if (it != m.end()) c.category = it->second;
        return c;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::channel_set(int channel_id, const ChannelCache& c) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        std::vector<std::pair<std::string, std::string>> fields = {
            {"community_id", std::to_string(c.community_id)},
            {"name", c.name},
            {"category", c.category},
        };
        redis->hset(key_channel(channel_id), fields.begin(), fields.end());
        redis->expire(key_channel(channel_id), std::chrono::seconds(86400));
    } catch (const std::exception&) {
        // 忽略
    }
}

void RedisCache::channel_invalidate(int channel_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_channel(channel_id));
    } catch (const std::exception&) {
        // 忽略
    }
}

// ============================================================
// C9 社区频道列表（ZSET，score=channel_id，24h 兜底）
// ============================================================

std::optional<std::vector<int>> RedisCache::community_channels_get(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return std::nullopt;
    }
    try {
        std::vector<std::string> ids;
        redis->zrange(key_community_channels(community_id), 0, -1, std::back_inserter(ids));
        if (ids.empty()) {
            return std::nullopt;
        }
        std::vector<int> out;
        out.reserve(ids.size());
        for (const auto& id : ids) {
            try {
                out.push_back(std::stoi(id));
            } catch (const std::exception&) {
            }
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::community_channels_set(int community_id, const std::vector<int>& channel_ids) {
    if (channel_ids.empty()) {
        return;
    }
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        for (int id : channel_ids) {
            redis->zadd(key_community_channels(community_id), std::to_string(id),
                        static_cast<double>(id));
        }
        redis->expire(key_community_channels(community_id), std::chrono::seconds(86400));
    } catch (const std::exception&) {
        // 忽略
    }
}

void RedisCache::community_channels_invalidate(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) {
        return;
    }
    try {
        redis->del(key_community_channels(community_id));
    } catch (const std::exception&) {
        // 忽略
    }
}

// ============================================================
// C10 用户社区/频道列表（SET + 24h 兜底）
// ============================================================

std::optional<std::vector<int>> RedisCache::user_communities_get(int user_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return std::nullopt;
    try {
        std::vector<std::string> vals;
        redis->smembers(key_user_communities(user_uid), std::back_inserter(vals));
        if (vals.empty()) return std::nullopt;
        std::vector<int> out;
        out.reserve(vals.size());
        for (const auto& v : vals) {
            try { out.push_back(std::stoi(v)); } catch (const std::exception&) {}
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::user_communities_set(int user_uid, const std::vector<int>& community_ids) {
    if (community_ids.empty()) return;
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        std::vector<std::string> vals;
        vals.reserve(community_ids.size());
        for (int id : community_ids) vals.push_back(std::to_string(id));
        redis->sadd(key_user_communities(user_uid), vals.begin(), vals.end());
        redis->expire(key_user_communities(user_uid), std::chrono::seconds(86400));
    } catch (const std::exception&) {
    }
}

void RedisCache::user_communities_invalidate(int user_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        redis->del(key_user_communities(user_uid));
    } catch (const std::exception&) {
    }
}

std::optional<std::vector<int>> RedisCache::user_channels_get(int user_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return std::nullopt;
    try {
        std::vector<std::string> vals;
        redis->smembers(key_user_channels(user_uid), std::back_inserter(vals));
        if (vals.empty()) return std::nullopt;
        std::vector<int> out;
        out.reserve(vals.size());
        for (const auto& v : vals) {
            try { out.push_back(std::stoi(v)); } catch (const std::exception&) {}
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::user_channels_set(int user_uid, const std::vector<int>& channel_ids) {
    if (channel_ids.empty()) return;
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        std::vector<std::string> vals;
        vals.reserve(channel_ids.size());
        for (int id : channel_ids) vals.push_back(std::to_string(id));
        redis->sadd(key_user_channels(user_uid), vals.begin(), vals.end());
        redis->expire(key_user_channels(user_uid), std::chrono::seconds(86400));
    } catch (const std::exception&) {
    }
}

void RedisCache::user_channels_invalidate(int user_uid) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        redis->del(key_user_channels(user_uid));
    } catch (const std::exception&) {
    }
}

// ============================================================
// C11 社区信息（HASH + 24h 兜底）
// ============================================================

std::optional<CommunityCache> RedisCache::community_info_get(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return std::nullopt;
    try {
        std::unordered_map<std::string, std::string> m;
        redis->hgetall(key_community_info(community_id), std::inserter(m, m.begin()));
        if (m.empty()) return std::nullopt;
        CommunityCache c;
        auto it = m.find("name"); if (it != m.end()) c.name = it->second;
        it = m.find("description"); if (it != m.end()) c.description = it->second;
        it = m.find("avatar"); if (it != m.end()) c.avatar = it->second;
        it = m.find("category"); if (it != m.end()) c.category = it->second;
        it = m.find("owner_uid");
        if (it != m.end()) { try { c.owner_uid = std::stoi(it->second); } catch (const std::exception&) {} }
        it = m.find("avatar_id");
        if (it != m.end()) { try { c.avatar_id = std::stoi(it->second); } catch (const std::exception&) {} }
        it = m.find("banner_id");
        if (it != m.end()) { try { c.banner_id = std::stoi(it->second); } catch (const std::exception&) {} }
        return c;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RedisCache::community_info_set(int community_id, const CommunityCache& c) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        std::vector<std::pair<std::string, std::string>> fields = {
            {"name", c.name},
            {"description", c.description},
            {"avatar", c.avatar},
            {"category", c.category},
            {"owner_uid", std::to_string(c.owner_uid)},
            {"avatar_id", std::to_string(c.avatar_id)},
            {"banner_id", std::to_string(c.banner_id)},
        };
        redis->hset(key_community_info(community_id), fields.begin(), fields.end());
        redis->expire(key_community_info(community_id), std::chrono::seconds(86400));
    } catch (const std::exception&) {
    }
}

void RedisCache::community_info_invalidate(int community_id) {
    auto redis = RedisPool::get_instance().get();
    if (!redis) return;
    try {
        redis->del(key_community_info(community_id));
    } catch (const std::exception&) {
    }
}
