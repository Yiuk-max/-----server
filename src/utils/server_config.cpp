#include "server_config.h"

ServerConfig& ServerConfig::get_instance() {
    static ServerConfig instance;
    return instance;
}

void ServerConfig::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "[ServerConfig] cannot open " << path
                  << ", fallback to defaults (net_layer=binary, use_heartbeat=false)." << std::endl;
        return;
    }
    try {
        json cfg = json::parse(ifs);
        if (cfg.contains("net_layer"))           net_layer_ = cfg["net_layer"].get<std::string>();
        if (cfg.contains("use_heartbeat"))       use_heartbeat_  = cfg["use_heartbeat"].get<bool>();
        if (cfg.contains("heartbeat_interval"))  heartbeat_interval_ = cfg["heartbeat_interval"].get<int>();
        if (cfg.contains("max_connections"))     max_connections_  = cfg["max_connections"].get<int>();
        // WebSocket 配置（嵌套于 "websocket" 对象）
        if (cfg.contains("websocket") && cfg["websocket"].is_object()) {
            auto& w = cfg["websocket"];
            if (w.contains("path"))              ws_path_              = w["path"].get<std::string>();
            if (w.contains("io_threads"))        ws_io_threads_        = w["io_threads"].get<int>();
            if (w.contains("max_message_bytes")) ws_max_message_bytes_ = w["max_message_bytes"].get<std::size_t>();
            if (w.contains("max_pending_bytes")) ws_max_pending_bytes_ = w["max_pending_bytes"].get<std::size_t>();
            if (w.contains("web_root"))          ws_web_root_          = w["web_root"].get<std::string>();
        }
        // MySQL 连接池配置（嵌套于 "database" 对象）
        if (cfg.contains("database") && cfg["database"].is_object()) {
            auto& d = cfg["database"];
            if (d.contains("host"))          db_host_       = d["host"].get<std::string>();
            if (d.contains("port"))          db_port_       = d["port"].get<int>();
            if (d.contains("user"))          db_user_       = d["user"].get<std::string>();
            if (d.contains("password"))      db_password_   = d["password"].get<std::string>();
            if (d.contains("dbname"))        db_name_       = d["dbname"].get<std::string>();
            if (d.contains("db_conn_count")) db_conn_count_ = d["db_conn_count"].get<int>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ServerConfig] parse error: " << e.what()
                  << ", fallback to defaults." << std::endl;
    }

    // 数值下限校验，避免 0/负数导致未定义行为。
    if (ws_io_threads_ < 1)         ws_io_threads_ = 1;
    if (ws_max_message_bytes_ == 0) ws_max_message_bytes_ = 1024 * 1024;
    if (ws_max_pending_bytes_ == 0) ws_max_pending_bytes_ = 8 * 1024 * 1024;
    if (db_conn_count_ < 1)         db_conn_count_ = 1;
    if (heartbeat_interval_ < 1)    heartbeat_interval_ = 1;
    if (max_connections_ < 1)       max_connections_ = 1;

    std::cout << "[ServerConfig] net_layer=" << net_layer_
              << ", use_heartbeat=" << (use_heartbeat_ ? "true" : "false")
              << ", heartbeat_interval=" << heartbeat_interval_ << "s"
              << ", ws_path=" << ws_path_
              << ", ws_io_threads=" << ws_io_threads_
              << ", ws_web_root=" << ws_web_root_
              << ", max_connections=" << max_connections_
              << ", db_conn_count=" << db_conn_count_ << std::endl;
}

std::string ServerConfig::net_layer() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return net_layer_;
}

bool ServerConfig::use_heartbeat() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return use_heartbeat_;
}

int ServerConfig::heartbeat_interval() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return heartbeat_interval_;
}

int ServerConfig::max_connections() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return max_connections_;
}

std::string ServerConfig::ws_path() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return ws_path_;
}

int ServerConfig::ws_io_threads() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return ws_io_threads_;
}

std::size_t ServerConfig::ws_max_message_bytes() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return ws_max_message_bytes_;
}

std::size_t ServerConfig::ws_max_pending_bytes() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return ws_max_pending_bytes_;
}

std::string ServerConfig::ws_web_root() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return ws_web_root_;
}

std::string ServerConfig::db_host() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_host_;
}
int ServerConfig::db_port() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_port_;
}
std::string ServerConfig::db_user() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_user_;
}
std::string ServerConfig::db_password() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_password_;
}
std::string ServerConfig::db_name() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_name_;
}
int ServerConfig::db_conn_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_conn_count_;
}
