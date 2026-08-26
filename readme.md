# 聊天服务器（C++17 / epoll / 主从 Reactor）

基于 epoll + 主从 Reactor 多线程模型的 TCP 聊天服务器，支持注册/登录、私聊、群聊、好友系统、文件传输。

## 快速开始

```bash
# 第一次：把配置文件复制到运行目录（见下方"配置文件"）
cp configure.json build/

# 首次配置
cd build
cmake ..

# 编译（之后每次改动只需这一条）
cd build
cmake --build . -j8

# 运行（监听 :8080，兼容 IPv4/IPv6）
cd build
./server
```

## 配置文件

服务器启动时读取**相对路径** `configure.json`（即当前工作目录下的文件）。程序通常从 `build/` 目录运行，因此请先执行 `cp configure.json build/`（或在 `build/` 下创建该文件）再启动，否则只读到默认值。配置文件包含两部分：

1. 心跳：`use_heartbeat`（开关）、`heartbeat_interval`（超时秒数）。
2. 数据库连接池：`database` 下的 `host / port / user / password / dbname / db_conn_count`
   （`db_conn_count` 为连接池上限，默认 4）。

未读到配置文件时使用默认值（连接信息见《数据库设计.txt》），可能连不上你的数据库。

## 文档

- **[项目说明文档.md](项目说明文档.md)** —— 技术栈、目录结构、架构与工作流程、核心模块说明、常见问题。
- **[客户端接口文档.txt](客户端接口文档.txt)** —— 前后端 JSON / 帧协议接口规范（建议客户端开发者先读此文档）。
- **[数据库设计.txt](数据库设计.txt)** —— 数据库表结构、UID 分配与连接池接入说明。
- **[开发日志.txt](开发日志.txt)** —— 待办清单与开发日志（含历次架构重构与问题修复记录）。

## 项目结构（概要）

```
server/
├── src/
│   ├── main.cpp            # 入口：socket、主从 Reactor、线程池、初始化 MySQL 连接池
│   ├── net_core/           # 网络核心层：epoller / client_session / message_handler / notice_service / receiver_sender / session_manager / server_config
│   ├── logic/              # 业务逻辑层：account / group / social_module / UID 分配器等
│   ├── db/                 # 数据库分层：mysql(连接池) / repo_interface(接口契约) / repo(MySQL 实现)
│   └── utils/              # 线程池
├── include/total.h         # 基础设施公共头（不再是"万能头"）
├── configure.json          # 运行配置（心跳 / 数据库连接池）
├── sql/create_table.sql    # 数据库建表脚本
└── build/                  # 构建输出
```

> 说明：本仓库仅包含**服务器端**源代码。
