#include "message_repo.h"
#include "mysql_conn_pool.h"
#include <memory>
#include <iostream>
#include <nlohmann/json.hpp>
// JDBC 类型定义（PreparedStatement / ResultSet / Statement）由 mysql_connection/mysql_connection 递送不保证完整，
// 显式引入以确保类型完整可用。
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"
#include "cppconn/exception.h"