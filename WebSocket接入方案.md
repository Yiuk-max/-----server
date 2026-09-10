# WebSocket 接入方案 v4（定案：双网络层平行，配置二选一，WS 层用 Boost）

> 适用工程：`-----server`（第一套） + 本工程（第二套孵化与验证）
> 定案：
>   1. **第二套 = 全新的 Boost.Asio + Beast 网络层**，自带 io_context 事件循环，
>      与第一套 epoll 主从 Reactor **完全平行、互不嵌套**。
>   2. 启动时读 `configure.json` 的 `net_layer`，本次只启用其中一套（二选一）。
>   3. 第一套代码不动；WS 层写进新文件；业务代码侧新增一套 WS 打包 API。
>   4. 端口维持 8080（运行时只有一套在跑，无需第二端口）。

---

## 1. 定案架构

```
configure.json
   └─ "net_layer": "binary" | "websocket"        ← 二选一（运行时读，改完重启）
                        │
                 main() 读配置，只启动一套
        ┌───────────────┴────────────────┐
        ▼                                ▼
  分支 A：binary                    分支 B：websocket
  网络：epoll 主从 Reactor            网络：Boost.Asio + Beast（全新）
  事件循环：main/sub_reactor 线程      事件循环：io_context（可多线程 run）
  协议：4B总长|4BJSON长|JSON|文件      协议：101握手 + RFC6455 帧
  连接对象：connection（现有代码不动）  连接对象：ws_session（新文件，Beast stream）
        │                                │
        └──────────┬─────────────────────┘
                   ▼
        共享业务：client_session / message_handler /
        logic / NoticeService / session_manager / repo(MySQL)
```

**为什么这样设计而不是“epoll 里塞 WS”：**
- Beast/Asio 自带完整事件循环（底层其实也用 epoll），硬塞进你的
  sub_reactor 等于两套事件驱动打架，还会让你重写 Beast 的握手/帧处理——白费。
- 二选一运行，意味着**两套网络层永远不同时活在同一进程的事件循环里**，
  因此“epoll 和 asio 各跑各的循环”的冲突根本不存在：选 B 时根本不启动 A。

---

## 2. 运行时流程（读配置二选一）

```cpp
// main() 伪代码
int main() {
    ServerConfig::get_instance().load("configure.json");   // 读 net_layer
    MySQL_Conn_Pool::get_instance().init();                // 数据库初始化（两套共用）

    if (ServerConfig::get_instance().net_layer() == "websocket") {
        ws::run_server(8080);          // 新网络层：Boost.Asio+Beast，阻塞在 io_context.run()
    } else {
        // 原有流程原样：socket/bind/listen → main_reactor.loop();  // epoll
    }
}
```

- `configure.json` 新增一个字段：
  ```jsonc
  { "net_layer": "websocket" /* 或 "binary"，默认 binary */ }
  ```
- `ServerConfig` 只加一个读取接口 `net_layer()`。
- 端口都是 8080：同一时刻只有一套协议在跑，客户端按服务端当前层选择连接方式即可。

---

## 3. 需要改的“缝”（保持最小）

| # | 改动 | 性质 |
|---|---|---|
| 1 | `configure.json` + `server_config.h/.cpp`：加 `net_layer` | 新增/小改 |
| 2 | `main.cpp`：按 `net_layer` 分流启动 | 小改 |
| 3 | 业务侧“发送打包”新做一套 WS API（见 §5），并把 `client_session`
      持有的连接弱引用类型换成极薄接口（好让 binary 连接 / ws 会话都满足） | 小改（允许范围内） |
| 4 | 新增整套 `net_core_ws/` 目录（Beast 网络层 + ws 会话） | 全新增 |
| 5 | CMake：引入 Boost（`find_package(Boost)`，ws 模式才需要；链接 ws2_32/mswsock 或 -pthread） | 构建改动 |

不动的：`net_core/`（epoll、connection、receiver/sender、session_manager、
NoticeService）、`logic/*`、`db/*`、`message_handler.*` 的内部实现。

---

## 4. 新增文件清单（全在 `src/net_core_ws/`，与老 `net_core/` 平行）

```
src/net_core_ws/
├── ws_server.h/.cpp       # 入口：net::io_context + acceptor，async_accept 循环
├── ws_session.h/.cpp      # 一个 WebSocket 连接 = 一个会话（Beast websocket::stream）
│                          #   async_read 收帧 → 业务 on_json；业务发 → async_write
├── ws_link.h/.cpp         # 给 client_session / NoticeService 用的“连接出口”
│                          #   （实现§3缝里的薄接口；把 JSON 打成文本/二进制帧发出）
└── ws_types.h             # 少量公共定义（UID→ws_session 在线表等，对照 session_manager）
```

Beast 自动处理：101 握手与 `Sec-WebSocket-Accept` 计算、帧边界、掩码解码、
分片重组、ping/pong、close 流程——**这些都不再需要手写**（对比 v3，删掉了
ws_sha1、ws_http_upgrade、手写状态机这些文件）。

---

## 5. 业务代码：新做一套 WS 打包 API

业务现在的“发消息”形态（binary 模式）：
```cpp
session.package_message(json_str, type);   // → connection::package_message（4B头打包）
```
新增一组**语义化发送 API**，内部按本进程启用的网络层走对应打包器：

```cpp
// client_session（或一个小门面类）
void send_json_msg(const std::string& json_text);          // 业务：发一条 JSON
void send_json_file_msg(const std::string& json_text,
                        const std::string& file_bytes);    // 业务：发 JSON+文件
```
- `binary` 模式 → 走老打包（4B 帧头），行为与今天完全一致；
- `websocket` 模式 → `ws_link` 把 JSON 打成 **文本帧**、JSON+文件打成 **二进制帧**
  （二进制帧 payload 内沿用 `4B JSON长|JSON|文件`，文件收发逻辑可对照第一套移植）。

传输映射（业务 JSON 零改动）：

| 场景 | WebSocket 帧 | 业务看到 |
|---|---|---|
| 登录/注册/聊天/群/好友 | 文本帧 = 完整 JSON | `on_json(json)`，字段与第一套一致 |
| 文件上传/下载 | 二进制帧 = `4BJSON长\|JSON\|文件` | `on_json(json, file)` |
| 系统推送/顶号 | 服务器主动文本帧 | 客户端按 type 处理 |

---

## 6. WebSocket 网络层工作流（Boost 版，全部异步）

```
ws::run_server(8080)
  ├─ io_context + acceptor → async_accept
  │     每来一个连接：构造 ws_session（beast::websocket::stream<tcp::socket>）
  │     async_accept → 完成 101 握手（Beast 内部算 Accept、写响应）
  │           │
  │           ▼ 握手完成，进入读循环
  │     ws_session::run():
  │        async_read(frame) ──（Beast：帧边界/掩码/分片/自动 pong 都处理好了）
  │              │ 文本帧 → json_text → 找/建 client_session → on_message(json,"")
  │              │ 二进制帧 → 拆 [4BJSON长|JSON|file] → on_message(json,file)
  │              │ close 帧 → Beast 收尾 → 走现有断线清理
  │              └ 回到 async_read
  │     业务要回复/推送：
  │        send_json_msg(json) → ws_link → async_write(文本帧)
  └─ io_context.run()（多核可开 N 个线程各跑一个 run）
```

线程模型（与第一套对等）：
- 一个 session 的所有读写都在**同一条 strand / 同一 io_context 线程**上串行，
  避免你手写锁（对应第一套 sub_reactor + 线程池的“串行化”职责）。

---

## 7. Qt 客户端：协议切换

```cpp
class ITransport {
public:
    virtual void connect_to(const QString& host, quint16 port /*8080*/) = 0;
    virtual void send_json(const QString& json) = 0;
signals:
    void messageArrived(QString json);
    void disconnected();
};
class TcpTransport : public ITransport { /* QTcpSocket + 老帧打包（现有） */ };
class WsTransport   : public ITransport { /* QWebSocket */ };
```
设置里选 `tcp | websocket`（都对 8080）：服务端当前 binary → 选 TCP；
websocket → 选 WS；切错立刻失败可感知。业务 UI 只依赖 `ITransport`。

---

## 8. 分阶段计划

| 阶段 | 内容 | 验收 | 能否本机验证 |
|---|---|---|---|
| M0 | 配置 `net_layer` + main 分流（binary 分支原样跑） | binary 老客户端回归不变 | 需 Linux |
| M1 | 建 `net_core_ws/` Beast 层骨架：握手 + JSON 回显 + 心跳/close | 浏览器/Python：101、收发、close 全过 | ✅ 本机可测（已装 Boost） |
| M2 | 接业务：ws_session → client_session；登录/注册/私聊/群聊 | 两个 WS 客户端走通完整业务链 | 需 Linux（含 MySQL） |
| M3 | 文件传输（二进制帧内复用 JSON+file 打包） | 上传/下载过 | 需 Linux |
| M4 | binary / websocket 两模式各完整回归对照 | 功能对照表打勾 | 需 Linux |

## 9. 风险与注意点

1. 同一端口 8080、同一套 JSON、按 `net_layer` 选层——**这就是服务端给两端客户端立的契约**。
2. 二选一切换=改配置重启；别在 binary 模式连 WS 客户端（反之亦然），断连属预期。
3. Beast 异步回调里别做 DB 长操作：入库放到业务线程池/队列（和第一套同一纪律）。
4. 慢客户端/背压：Beast 有写队列，注意单会话积压上限。
5. 心跳：应用层 heartbeat 与 Beast ping/pong 二选一，避免重复踢人。

## 10. 现在不碰的东西

Redis、token 鉴权、wss/TLS、分布式——M4 之后单独立项。

---

## 下一步

M1 的 Beast 网络层（`net_core_ws/` 骨架）**可以在本机直接编译验证**（Boost 已装），
不依赖你的 Linux 环境。要不要我现在就写 M1？
