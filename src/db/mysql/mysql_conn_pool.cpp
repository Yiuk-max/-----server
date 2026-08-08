#include "mysql_conn_pool.h"
#include "server_config.h"
#include <iostream>

MySQL_Conn_Pool& MySQL_Conn_Pool::get_instance() {
    static MySQL_Conn_Pool instance;
    return instance;
}

// 建一个连接；读 ServerConfig 的 database 配置。失败返回 nullptr。
sql::Connection* MySQL_Conn_Pool::create_connection() {
    const auto& cfg = ServerConfig::get_instance();
    try {
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
        std::string url = "tcp://" + cfg.db_host() + ":" + std::to_string(cfg.db_port());
        sql::Connection* conn = driver->connect(url, cfg.db_user(), cfg.db_password());
        if (!conn) {
            return nullptr;
        }
        conn->setSchema(cfg.db_name());
        return conn;
    } catch (const sql::SQLException& e) {
        std::cerr << "[MySQL_Conn_Pool] create connection failed: "
                  << e.what() << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return nullptr;
    }
}

void MySQL_Conn_Pool::init() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (inited_.exchange(true)) {
        return; // 已初始化，避免重复预建
    }
    max_conns_ = ServerConfig::get_instance().db_conn_count();
    if (max_conns_ < 1) {
        max_conns_ = 1;
    }
    for (int i = 0; i < max_conns_; ++i) {
        if (auto* c = create_connection()) {
            idle_conns_.push(c);
            ++total_created_;
        }
    }
    std::cout << "[MySQL_Conn_Pool] initialized " << idle_conns_.size()
              << "/" << max_conns_ << " connections." << std::endl;
}

sql::Connection* MySQL_Conn_Pool::get_connection() {
    std::unique_lock<std::mutex> lock(mtx_);
    // 1. 有现成空闲连接，直接取
    if (!idle_conns_.empty()) {
        auto* c = idle_conns_.front();
        idle_conns_.pop();
        return c;
    }
    // 2. 未达上限，临时新建
    if (total_created_ < max_conns_) {
        if (auto* c = create_connection()) {
            ++total_created_;
            return c;
        }
        // 创建失败（如 DB 未就绪）直接返回 nullptr，由上层决定是否重试
        return nullptr;
    }
    // 3. 已达上限，阻塞等待空闲连接归还
    cv_.wait(lock, [this] { return !idle_conns_.empty(); });
    auto* c = idle_conns_.front();
    idle_conns_.pop();
    return c;
}

void MySQL_Conn_Pool::return_connection(sql::Connection* conn) {
    if (!conn) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        idle_conns_.push(conn);
    }
    cv_.notify_one();
}

void MySQL_Conn_Pool::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!idle_conns_.empty()) {
        delete idle_conns_.front();
        idle_conns_.pop();
        --total_created_;
    }
    inited_ = false;
}