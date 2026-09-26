#pragma once
#include "total.h"
#include <mutex>

// ============================================================
// 服务器配置（单例）
// 从 configure.json 读取配置，供各网络/连接模块查询。
//
// 目前支持的配置项：
//   - net_layer           : string "binary" | "websocket"，启动时二选一（默认 binary）
//   - use_heartbeat       : bool   是否启用心跳包检测
//   - heartbeat_interval  : int    活动超时阈值（秒）；连接超过该秒数无数据即断开
//   - websocket           : object WebSocket 网络层配置
//       path / io_threads / max_message_bytes / max_pending_bytes
//   - database            : object MySQL 连接池配置
//       host / port / user / password / dbname / db_conn_count(默认 4)
//   - file_storage        : object 文件存储配置
//       root / chunk_size / max_chunk_size / max_file_size /
//       max_storage_size / max_avatar_size / transfer_timeout_seconds
//
// 用法：ServerConfig::get_instance().net_layer()
//       ServerConfig::get_instance().db_conn_count()
// ============================================================
class ServerConfig {
public:
    static ServerConfig& get_instance();

    // 从指定路径加载配置（服务器启动时调用一次）
    void load(const std::string& path);

    // ---- 网络层选择 ----
    std::string net_layer() const;     // "binary" 或 "websocket"

    bool use_heartbeat() const;
    int  heartbeat_interval() const;   // 活动超时秒数
    int  max_connections() const;      // 最大并发连接数（binary 与 websocket 共用）

    // ---- WebSocket 配置 ----
    std::string ws_path() const;                 // 握手路径，默认 "/ws"
    int         ws_io_threads() const;           // Asio 事件循环线程数（>=1）
    std::size_t ws_max_message_bytes() const;    // 单条 WS 消息上限
    std::size_t ws_max_pending_bytes() const;    // 单连接待发送字节上限
    std::string ws_web_root() const;             // 静态前端资源目录，默认 "./web"

    // ---- MySQL 连接池配置（按值返回，避免锁外引用内部字符串） ----
    std::string db_host() const;
    int  db_port() const;
    std::string db_user() const;
    std::string db_password() const;
    std::string db_name() const;
    int  db_conn_count() const;        // 连接池内连接数量（默认 4）

    // ---- Redis 缓存配置 ----
    bool        redis_enabled() const;    // 是否启用缓存（默认 false）
    std::string redis_host() const;
    int         redis_port() const;
    std::string redis_password() const;
    int         redis_db() const;
    int         redis_pool_size() const; // 连接池大小（默认 4）
    int         redis_timeout_ms() const;// 单次命令超时（默认 200ms）

    // ---- 文件存储配置 ----
    std::string file_storage_root() const;      // 存储根目录（默认 /home/ubuntu/chat_server_files）
    uint32_t    file_chunk_size() const;        // 文件分片大小（默认 4 MiB）
    uint32_t    file_max_chunk_size() const;    // 单个分片上限（默认 16 MiB）
    uint64_t    file_max_file_size() const;     // 单文件上限（默认 512 MiB）
    uint64_t    file_max_storage_size() const;  // 存储总量上限（默认 10 GiB，超限淘汰最老文件）
    uint64_t    file_max_avatar_size() const;   // 头像文件上限（默认 10 MiB）
    int         file_transfer_timeout_seconds() const; // 传输超时（watchdog，默认 1800s）

private:
    ServerConfig() = default;

    mutable std::mutex mtx_;
    std::string net_layer_ = "binary";
    bool use_heartbeat_          = false;
    int  heartbeat_interval_     = 60;
    int  max_connections_        = 10000;   // 最大并发连接数，达到后拒绝新连接

    // WebSocket 配置
    std::string ws_path_             = "/ws";
    int         ws_io_threads_       = 4;
    std::size_t ws_max_message_bytes_ = 8 * 1024 * 1024;  // 8 MiB（文件分片默认 4 MiB，需大于单块大小）
    std::size_t ws_max_pending_bytes_ = 8 * 1024 * 1024;  // 8 MiB
    std::string ws_web_root_         = "./web";           // 静态前端资源目录

    // MySQL 连接池配置
    std::string db_host_     = "localhost";
    int         db_port_     = 3306;
    std::string db_user_     = "chat_server";
    std::string db_password_ = "Chat_123!";
    std::string db_name_     = "chat_server";
    int         db_conn_count_ = 4;

    // Redis 缓存配置
    bool        redis_enabled_    = false;
    std::string redis_host_       = "127.0.0.1";
    int         redis_port_       = 6379;
    std::string redis_password_   = "";
    int         redis_db_         = 0;
    int         redis_pool_size_  = 4;
    int         redis_timeout_ms_ = 200;

    // 文件存储配置
    std::string file_storage_root_ = "/home/ubuntu/chat_server_files";
    uint32_t    file_chunk_size_   = 4 * 1024 * 1024;
    uint32_t    file_max_chunk_size_   = 16 * 1024 * 1024;
    uint64_t    file_max_file_size_    = 512ULL * 1024 * 1024;
    uint64_t    file_max_storage_size_ = 10ULL * 1024 * 1024 * 1024;
    uint64_t    file_max_avatar_size_  = 10ULL * 1024 * 1024;
    int         file_transfer_timeout_seconds_ = 1800;
};
