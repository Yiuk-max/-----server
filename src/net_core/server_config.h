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
//   - database            : object MySQL 连接池配置
//       host / port / user / password / dbname / db_conn_count(默认 4)
//
// 用法：ServerConfig::get_instance().use_heartbeat()
//       ServerConfig::get_instance().db_conn_count()
// ============================================================
class ServerConfig {
public:
    static ServerConfig& get_instance();

    // 从指定路径加载配置（服务器启动时调用一次）
    void load(const std::string& path);

    bool use_heartbeat() const;
    int  heartbeat_interval() const;   // 活动超时秒数

    // ---- MySQL 连接池配置 ----
    const std::string& db_host() const;
    int  db_port() const;
    const std::string& db_user() const;
    const std::string& db_password() const;
    const std::string& db_name() const;
    int  db_conn_count() const;        // 连接池内连接数量（默认 4）

private:
    ServerConfig() = default;

    mutable std::mutex mtx_;
    bool use_heartbeat_          = false;
    int  heartbeat_interval_     = 60;

    // MySQL 连接池配置
    std::string db_host_     = "localhost";
    int         db_port_     = 3306;
    std::string db_user_     = "chat_server";
    std::string db_password_ = "Chat_123!";
    std::string db_name_     = "chat_server";
    int         db_conn_count_ = 4;
};
