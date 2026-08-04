# 聊天服务器（C++17 / epoll / 主从 Reactor）

基于 epoll + 主从 Reactor 多线程模型的 TCP 聊天服务器，支持注册/登录、私聊、群聊、好友系统、文件传输。

## 快速开始

```bash
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

## 文档

- **[项目说明文档.md](项目说明文档.md)** —— 技术栈、目录结构、架构与工作流程、核心模块说明、常见问题。
- **[客户端接口文档.txt](客户端接口文档.txt)** —— 前后端 JSON / 帧协议接口规范（建议客户端开发者先读此文档）。
- **[to_do.txt](to_do.txt)** —— 待办清单与开发日志（含历次架构重构与问题修复记录）。

## 项目结构（概要）

```
server/
├── src/
│   ├── main.cpp            # 入口：socket、主从 Reactor、线程池
│   ├── net_core/           # 网络核心层：epoller / client_session / message_handler / notice_service / receiver_sender / session_manager
│   ├── logic/              # 业务逻辑层：account / group / social_module / UID 分配器等
│   └── utils/              # 线程池
├── include/total.h         # 基础设施公共头（不再是"万能头"）
└── build/                  # 构建输出
```

> 说明：本仓库仅包含**服务器端**源代码。
