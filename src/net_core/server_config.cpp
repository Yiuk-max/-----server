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
    } catch (const std::exception& e) {
        std::cerr << "[ServerConfig] parse error: " << e.what()
                  << ", fallback to defaults." << std::endl;
    }
    std::cout << "[ServerConfig] use_heartbeat=" << (use_heartbeat_ ? "true" : "false")
              << ", heartbeat_interval=" << heartbeat_interval_ << "s" << std::endl;
}

bool ServerConfig::use_heartbeat() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return use_heartbeat_;
}

int ServerConfig::heartbeat_interval() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return heartbeat_interval_;
}
