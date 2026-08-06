#pragma once
#include "total.h"
#include <mutex>

// ============================================================
// 服务器配置（单例）
// 从 configure.json 读取配置，供各网络/连接模块查询。
//
// 目前支持的配置项：
//   - use_heartbeat       : bool   是否启用心跳包检测
//   - heartbeat_interval  : int    活动超时阈值（秒）；连接超过该秒数无数据即断开
//
// 用法：ServerConfig::get_instance().use_heartbeat()
// ============================================================
class ServerConfig {
public:
    static ServerConfig& get_instance();

    // 从指定路径加载配置（服务器启动时调用一次）
    void load(const std::string& path);

    bool use_heartbeat() const;
    int  heartbeat_interval() const;   // 活动超时秒数

private:
    ServerConfig() = default;

    mutable std::mutex mtx_;
    bool use_heartbeat_          = false;
    int  heartbeat_interval_     = 60;
};
