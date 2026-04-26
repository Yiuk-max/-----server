# 🚀 C++ Chat Server | C++ 聊天服务器

<div align="center">

![C++](https://img.shields.io/badge/C++-17-blue?style=for-the-badge&logo=cplusplus)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-Active-success?style=for-the-badge)

*A lightweight multi-client chat server built with raw TCP sockets* | 一个基于原生TCP套接字的轻量级多客户端聊天服务器

</div>

---

## 📑 Table of Contents | 目录

- [Features | 功能特性](#features--功能特性)
- [Architecture | 系统架构](#architecture--系统架构)
- [Quick Start | 快速开始](#quick-start--快速开始)
- [Usage | 使用说明](#usage--使用说明)
- [Protocol | 通信协议](#protocol--通信协议)
- [Contributing | 贡献指南](#contributing--贡献指南)
- [License | 许可证](#license--许可证)

---

## ✨ Features | 功能特性

| Feature | 功能 | Description | 描述 |
|---------|------|-------------|------|
| 🔐 User Authentication | 用户认证 | Login with username and password | 用户名密码登录 |
| 💬 Private Messaging | 私聊消息 | Send messages to specific users | 向指定用户发送消息 |
| 📢 Public Broadcasting | 群广播 | Broadcast messages to all online users | 向所有在线用户广播消息 |
| 👥 Group Chat | 群组聊天 | Create and manage group conversations | 创建和管理群组对话 |
| 🌐 IPv6 Support | IPv6支持 | Full IPv6 compatibility | 完整IPv6兼容性 |
| 🔄 Multi-threaded | 多线程 | Handle multiple clients concurrently | 并发处理多个客户端 |

---

## 🏗 Architecture | 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                        Client Clients                        │
│                    (Telnet / Netcat)                        │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Main Server Loop                          │
│                  (IPv6 TCP Socket :8080)                     │
└─────────────────────────┬───────────────────────────────────┘
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  User Module │  │ Group Module │  │  Msg Module  │
│   user.cpp   │  │  group.cpp   │  │ handle_msg.cpp│
└──────────────┘  └──────────────┘  └──────────────┘
         │                │                │
         └────────────────┼────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Global Data Store                         │
│              (total.h - Shared State)                       │
│  • activate_clients    • username_to_fd                     │
│  • users               • group_list                         │
└─────────────────────────────────────────────────────────────┘
```

### File Structure | 文件结构

```
server/
├── main.cpp          # Server entry point | 服务器入口
├── total.h           # Global variables and state | 全局变量和状态
├── user.cpp/h        # User management | 用户管理
├── group.cpp/h       # Group chat functionality | 群组聊天功能
├── handle_msg.cpp/h  # Message handling | 消息处理
├── CMakeLists.txt    # Build configuration | 构建配置
└── readme.md         # This file | 说明文档
```

---

## 🚦 Quick Start | 快速开始

### Prerequisites | 环境要求

- Linux / macOS
- GCC or Clang with C++17 support
- CMake 3.10+

### Build | 编译

```bash
# Clone and enter the project
cd server

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build the project
make
```

### Run | 运行

```bash
# Start the server
./server

# Expected output:
# Server is listening on port 8080...
```

### Connect | 连接

```bash
# Using telnet
telnet localhost 8080

# Using netcat
nc -6 localhost 8080
```

---

## 📖 Usage | 使用说明

### Login | 登录

```
Username: your_username
Password: 123456 (default)
```

### Commands | 命令

| Command | 命令 | Description | 描述 |
|---------|------|-------------|------|
| `send <username> <message>` | 发送私聊 | Send private message | 发送私聊消息 |
| `broadcast <message>` | 广播 | Broadcast to all users | 向所有用户广播 |
| `create_group <group_name>` | 创建群 | Create a new group | 创建新群组 |
| `join_group <group_name>` | 加群 | Join an existing group | 加入已有群组 |
| `group_msg <group_name> <message>` | 群消息 | Send message to group | 向群发送消息 |
| `list` | 列表 | Show online users | 显示在线用户 |
| `quit` | 退出 | Disconnect from server | 断开服务器连接 |

---

## 🔌 Protocol | 通信协议

### Message Format | 消息格式

```
[Command] [Parameters] \n
```

### Response Codes | 响应码

| Code | 含义 | Description |
|------|------|-------------|
| `200` | 成功 | Success |
| `400` | 错误 | Error |
| `401` | 未认证 | Not authenticated |
| `500` | 服务器错误 | Server error |

---

## 🤝 Contributing | 贡献指南

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## 📄 License | 许可证

This project is licensed under the MIT License.

---

<div align="center">

Made with ❤️ by [-Yiuk-]

</div>