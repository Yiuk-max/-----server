#pragma once
// ============================================================
// MySQL 数据库连接池（单例）
// 定位：供 db/repo 层获取数据库连接。所有 SQL 只出现在 db/repo 内，
//       而连接的获取/归还统一通过本连接池。
// 行为：
//   - init() 在服务器启动时调用，按 ServerConfig 中的 database 配置
//     （host/port/user/password/dbname/db_conn_count）预建连接。
//   - db_conn_count 为连接池上限（默认 4，见 configure.json）。
//   - get_connection() 优先复用空闲连接；不足且未达上限时临时新建；
//     达上限则阻塞等待空闲连接归还。
//   - 使用完后必须 return_connection() 归还，避免连接泄漏。
// ============================================================

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// MySQL Connector/C++（JDBC 风格）API
#include "mysql_driver.h"
#include "mysql_connection.h"
#include "cppconn/connection.h"
#include "cppconn/exception.h"

class MySQL_Conn_Pool {
public:
    static MySQL_Conn_Pool& get_instance();

    // 根据 ServerConfig 配置初始化连接池（服务器启动时调用一次）
    void init();

    // 取一个空闲连接（供 repo 执行 SQL 用）；用完须 return_connection
    sql::Connection* get_connection();

    // 归还连接
    void return_connection(sql::Connection* conn);

    // 关闭并清空连接池
    void shutdown();

private:
    MySQL_Conn_Pool() = default;
    MySQL_Conn_Pool(const MySQL_Conn_Pool&) = delete;
    MySQL_Conn_Pool& operator=(const MySQL_Conn_Pool&) = delete;

    sql::Connection* create_connection();   // 建一个连接；失败返回 nullptr

    std::queue<sql::Connection*> idle_conns_;
    std::mutex mtx_;
    std::condition_variable cv_;
    int max_conns_          = 4;          // 连接池上限（来自 db_conn_count）
    int total_created_      = 0;          // 已创建且未销毁的连接数（含借出中的）
    std::atomic<bool> inited_{false};
};