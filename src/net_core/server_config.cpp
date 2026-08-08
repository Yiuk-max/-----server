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
                  << ", fallback to defaults (use_heartbeat=false)." << std::endl;
        return;
    }
    try {
        json cfg = json::parse(ifs);
        if (cfg.contains("use_heartbeat"))       use_heartbeat_  = cfg["use_heartbeat"].get<bool>();
        if (cfg.contains("heartbeat_interval"))  heartbeat_interval_ = cfg["heartbeat_interval"].get<int>();
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
    std::cout << "[ServerConfig] use_heartbeat=" << (use_heartbeat_ ? "true" : "false")
              << ", heartbeat_interval=" << heartbeat_interval_ << "s"
              << ", db_conn_count=" << db_conn_count_ << std::endl;
}

bool ServerConfig::use_heartbeat() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return use_heartbeat_;
}

int ServerConfig::heartbeat_interval() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return heartbeat_interval_;
}

const std::string& ServerConfig::db_host() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_host_;
}
int ServerConfig::db_port() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_port_;
}
const std::string& ServerConfig::db_user() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_user_;
}
const std::string& ServerConfig::db_password() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_password_;
}
const std::string& ServerConfig::db_name() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_name_;
}
int ServerConfig::db_conn_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return db_conn_count_;
}
