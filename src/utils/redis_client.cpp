#include "redis_client.h"

#include <chrono>
#include <iostream>

#include "server_config.h"

RedisPool& RedisPool::get_instance() {
    static RedisPool instance;
    return instance;
}

void RedisPool::init() {
    if (inited_.exchange(true)) {// 已经初始化过了
        return;
    }

    const auto& cfg = ServerConfig::get_instance();
    enabled_ = cfg.redis_enabled();
    if (!enabled_) {
        std::cout << "[RedisPool] disabled" << std::endl;
        return;
    }

    sw::redis::ConnectionOptions opts;
    opts.host            = cfg.redis_host();
    opts.port            = cfg.redis_port();
    opts.password        = cfg.redis_password();
    opts.db              = cfg.redis_db();
    opts.connect_timeout = std::chrono::milliseconds(cfg.redis_timeout_ms());
    opts.socket_timeout  = std::chrono::milliseconds(cfg.redis_timeout_ms());

    sw::redis::ConnectionPoolOptions pool_opts;
    pool_opts.size         = static_cast<std::size_t>(cfg.redis_pool_size());
    pool_opts.wait_timeout = std::chrono::milliseconds(50);  // 池满快速失败，缓存层降级走 DB

    redis_ = std::make_shared<sw::redis::Redis>(opts, pool_opts);

    try {
        redis_->ping();
        std::cout << "[RedisPool] connected to " << opts.host << ":" << opts.port
                  << ", pool_size " << pool_opts.size << std::endl;
    } catch (const std::exception& e) {
        // 连不上时仍保留 redis_（后续命令会继续尝试并抛异常 → 缓存层降级），这里只提示。
        std::cout << "[RedisPool] ping failed: " << e.what()
                  << " (cache degrades to MySQL)" << std::endl;
    }
}

void RedisPool::shutdown() {
    redis_.reset();
    enabled_ = false;
}
