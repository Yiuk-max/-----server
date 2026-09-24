#pragma once
#include <atomic>
#include <memory>
#include <string>

#include <sw/redis++/redis.h>

// ============================================================
// Redis 客户端封装：基于 redis-plus-plus（现代 C++ 封装，
// 内部自带连接池与线程安全）。单例持有共享 sw::redis::Redis。
//
// 策略：
//   - enabled=false 时 get() 返回 nullptr，缓存层直接降级走 MySQL。
//   - 连接池满/命令异常由 redis-plus-plus 抛 sw::redis::Error，
//     缓存层统一捕获并降级，绝不阻塞业务线程。
// ============================================================
class RedisPool {
public:
    static RedisPool& get_instance();

    void init();     // 读 ServerConfig 的 redis 段；enabled=false 时为空操作
    bool enabled() const { return enabled_; }

    // 返回共享 Redis 客户端；未启用返回 nullptr
    std::shared_ptr<sw::redis::Redis> get() { return enabled_ ? redis_ : nullptr; }

    void shutdown();

private:
    RedisPool() = default;
    RedisPool(const RedisPool&) = delete;
    RedisPool& operator=(const RedisPool&) = delete;

    std::atomic<bool> inited_{false};
    bool enabled_ = false;
    std::shared_ptr<sw::redis::Redis> redis_;
};
