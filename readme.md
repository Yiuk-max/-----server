# 聊天服务器（C++17 / epoll 主从 Reactor / WebSocket）

基于 epoll + 主从 Reactor 多线程模型的 TCP 聊天服务器，支持注册/登录、私聊、群聊、好友系统、文件传输。
另提供平行的 **Boost.Asio + Beast WebSocket 网络层**，由 `configure.json` 的 `net_layer` 在启动时二选一，两种模式复用同一套业务层。
两种网络模式的业务消息统一使用 `src/proto/message.proto` 定义的 protobuf `chat_proto.Envelope`；WebSocket 使用 binary 帧 `4B payload_len + protobuf`。WebSocket 模式已支持注册/登录、私聊、群聊、好友、离线消息、心跳等即时通讯业务，**文件传输暂未支持**。

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

服务器启动时读取**相对路径** `configure.json`（即当前工作目录下的文件）。程序通常从 `build/` 目录运行，因此请先执行 `cp configure.json build/`（或在 `build/` 下创建该文件）再启动，否则只读到默认值。配置文件包含：

1. 网络层：`net_layer`（`"binary"` 或 `"websocket"`，默认 `binary`，启动时二选一，改完重启）；
   `websocket` 子对象可配置 `path`（默认 `/ws`）、`io_threads`、`max_message_bytes`、`max_pending_bytes`、
   `web_root`（静态前端资源目录，默认 `./web`）。
2. 心跳：`use_heartbeat`（开关）、`heartbeat_interval`（超时秒数）。
3. 连接上限：`max_connections`（最大并发连接数，默认 10000，达到后拒绝新连接）。
4. 数据库连接池：`database` 下的 `host / port / user / password / dbname / db_conn_count`
   （`db_conn_count` 为连接池上限，默认 4）。
5. Redis 缓存：`redis` 下的 `enabled / host / port / password / db / pool_size / timeout_ms`
   （`enabled=false` 时关闭缓存，回退纯 MySQL；已实现 C1 群成员 / C2 账号 / C4 邮箱缓存，详见《Redis缓存方案.md》）。

> 后端构建依赖 Boost 与 protobuf（`libboost-dev`、`libboost-system-dev`、`libprotobuf-dev`、`protobuf-compiler`）。
> Redis 缓存依赖 `redis-plus-plus`（基于 hiredis），构建时经 CMake `FetchContent` 从 GitHub 自动拉取源码并静态链接，
> 因此**首次 configure 需要网络**。`net_layer` 是运行时选择，因此同一个二进制总是包含两套网络代码；
> 前端依赖 `protobufjs`，由 `npm install` 安装。

未读到配置文件时使用默认值（连接信息见《数据库设计.txt》），可能连不上你的数据库。

## 网络层切换（binary / websocket）

`net_layer` 在启动时读取，改完需要重启；**两种模式只能同时跑一个，都占用 8080 端口**。

编辑运行目录下的 `configure.json`（通常是 `build/configure.json`）：

- **用 WebSocket**：设置 `"net_layer": "websocket"`，浏览器 / WebSocket 客户端连 `ws://<host>:8080/ws`。
  同一端口还会以 HTTP 提供前端页面：浏览器直接打开 `http://<host>:8080/` 即可（静态资源目录由 `websocket.web_root` 指定）。
- **使用 TCP binary 模式**：把 `net_layer` 改成 `"binary"`（或直接删掉该字段，默认就是 `binary`）再启动，TCP 客户端需遵循当前 protobuf 帧协议连接 `:8080`。
  此时不再提供 HTTP 页面（binary 模式没有 HTTP 处理）。

> 切错协议会立刻连不上，这是预期的：websocket 模式只接受 WS 握手（错误路径返回 404，非升级请求返回 426）；
> binary 模式只认自定义帧。`use_heartbeat` / `heartbeat_interval` 对两种模式都生效。

启动后日志会明确打印当前网络层，例如：

```
[ServerConfig] net_layer=websocket, use_heartbeat=false, heartbeat_interval=60s, ws_path=/ws, ws_io_threads=4, web_root=../web, max_connections=10000, db_conn_count=4
[MySQL_Conn_Pool] initialized 4/4 connections.
[RedisPool] connected to 127.0.0.1:6379, pool_size 4
[ws] WebSocket server listening on port 8080, path /ws, io_threads 4
```

### 浏览器快速测试

**方式一（推荐）**：直接打开服务端提供的聊天页 `http://<host>:8080/`（仅 websocket 模式）。
前端是 **Vite + Vue 3** 项目，源码在 `frontend/`，构建产物输出到 `web/`（服务端静态提供）：

```bash
cd frontend
npm install
npm run build      # 产出到 ../web（index.html + assets/*.js|css）
# 开发模式：npm run dev（5173，已代理 /ws 到后端 8080）
```

> `web_root` 是相对**运行目录**的路径。从仓库 `build/` 目录运行时指向构建产物 `web/`，
> 即 `"web_root": "../web"`（已在 `build/configure.json` 配好）；若在仓库根目录运行则用 `"./web"`。

> 后端静态服务后缀白名单不含 `wasm/webp/avif/ttf`，且无 SPA fallback，因此前端用 hash 路由、字体用 woff2 或系统字体。

**方式二**：低阶 protobuf 协议测试页 `tests/ws_browser_test.html`。从仓库根目录启动静态服务：

```bash
python3 -m http.server 8000
# 然后打开 http://<host>:8000/tests/ws_browser_test.html
```

页面里 Host 填服务端地址、Port `8080`、Path `/ws`，点「连接」即可注册/登录/私聊。

## 测试

可选单元测试（不需要 MySQL）：

```bash
cmake -S . -B build -DSERVER_BUILD_TESTS=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

端到端冒烟（需要 MySQL，且服务端已以 `net_layer=websocket` 启动）：

```bash
python3 tests/ws_chat_smoke.py 127.0.0.1 8080 /ws
# 群聊广播多接收方一致性（验证广播构造/序列化一次后多端字段一致）
python3 tests/test_group_broadcast.py 127.0.0.1 8080 /ws
# 额外验证空闲超时（服务端需 use_heartbeat=true 且 heartbeat_interval<=N）
python3 tests/ws_chat_smoke.py 127.0.0.1 8080 /ws --idle-seconds=3
```

冒烟脚本覆盖：注册/登录、好友申请与接受、私聊、建群/拉人/群聊、离线消息、顶号、空闲超时。

## 文档

- **[项目说明文档.md](Docs/项目说明文档.md)** —— 技术栈、目录结构、架构与工作流程、核心模块说明、常见问题。
- **[客户端接口文档.txt](Docs/客户端接口文档.txt)** —— 前后端 protobuf / 帧协议接口规范（建议客户端开发者先读此文档）。
- **[frontend/README.md](frontend/README.md)** —— 前端（Vite + Vue 3）开发与构建说明。
- **[protobuf改造方案.md](Docs/protobuf改造方案.md)** —— JSON → protobuf 的设计、落地文件与验证矩阵。
- **[WebSocket接入代码改动文档.md](Docs/WebSocket接入代码改动文档.md)** —— WebSocket 接入历史记录（正文包含旧 JSON/text 阶段，当前协议以接口文档为准）。
- **[数据库设计.txt](Docs/数据库设计.txt)** —— 数据库表结构、UID 分配与连接池接入说明。
- **[Redis缓存方案.md](Docs/Redis缓存方案.md)** —— Redis 缓存接入方案与实现状态（C1/C2/C4 已落地，C3/C5/C6 待做），附并发/稳定性瓶颈分析。
- **[压力测试方案.md](Docs/压力测试方案.md)** —— WebSocket 压测方案与用例清单。
- **[开发日志.txt](build/开发日志.txt)** —— 待办清单与开发日志（当前位于 build/ 目录，含历次架构重构与问题修复记录）。

## 项目结构（概要）

```
server/
├── src/
│   ├── main.cpp            # 入口：socket、主从 Reactor、线程池、初始化 MySQL/Redis 连接池
│   ├── net_core/           # 网络核心层：epoller / client_session / notice_service / group_manager / receiver_sender / session_manager
│   ├── net_common/         # 传输抽象：IClientTransport（让业务层不依赖具体 TCP/WS）
│   ├── net_core_ws/        # WebSocket 网络层：ws_server / ws_session / ws_protocol（Boost.Asio + Beast，含静态前端服务）
│   ├── proto/              # protobuf 协议唯一来源：message.proto
│   ├── logic/              # 业务逻辑层：account / group / social_module / handlers（各消息处理器）
│   ├── db/                 # 数据库分层：mysql(连接池) / repo_interface(接口契约) / repo(MySQL 实现)
│   └── utils/              # 线程池 + server_config + redis_client/redis_cache（Redis 缓存）
├── include/total.h         # 基础设施公共头（不再是"万能头"）
├── configure.json          # 运行配置（网络层 / 心跳 / 连接上限 / WebSocket / 数据库连接池 / Redis 缓存）
├── sql/create_table.sql    # 数据库建表脚本
├── sql/add_self_friend.sql # 给已有账号补齐"自己是自己的好友"（自聊）
├── frontend/               # 前端源码（Vite + Vue 3）：npm run build 输出到 web/
├── web/                    # 前端构建产物（由 WS 服务端静态提供，勿手改）
├── tests/                  # 可选单元测试 + 端到端冒烟脚本 + 低阶浏览器测试页
└── build/                  # 构建输出
```

> 说明：仓库同时包含后端、Vue 前端、protobuf schema 与测试客户端。
