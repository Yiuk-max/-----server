# WebSocket 接入代码改动文档

> 文档状态：评审稿，仅描述改动，不修改业务代码。  
> 适用代码版本：当前 `master`（`fa32983`）。  
> 关联草稿：`WebSocket接入方案.md`。本文在该草稿“Boost.Asio + Beast 独立网络层、运行时二选一”的方向上，补齐真实代码改动点和并发/生命周期细节。

## 1. 结论

推荐方案如下：

1. 保留现有 `epoll + 自定义二进制协议` 网络层。
2. 新增平行的 `Boost.Asio + Boost.Beast` WebSocket 网络层。
3. 启动时读取 `configure.json` 的 `net_layer`，只启动 `binary` 或 `websocket` 其中一个监听器，两者继续使用端口 `8080`。
4. 在业务会话和具体连接之间增加 `IClientTransport`，让 `client_session` 不再直接依赖 `connection`。
5. 普通业务消息使用 WebSocket 文本消息，文件块使用 WebSocket 二进制消息。
6. WebSocket IO 只在 Asio 线程执行；数据库和业务处理放到业务线程池；同一连接一次只处理一条业务消息，保证消息顺序。
7. WebSocket 写入必须经过单连接发送队列，任意业务线程只能投递发送任务，不能直接调用 Beast 的 `async_write`。

不建议把 Beast 的 socket 塞进现有 `sub_reactor`。Asio 自己管理 epoll 和异步操作，把两套事件循环嵌套后既无法复用现有 `connection`，也更容易产生重复读写和生命周期问题。

## 2. 当前项目的真实边界

当前消息链路是：

```text
main_reactor
  -> sub_reactor
  -> connection（TCP 收发、自定义帧、文件传输）
  -> client_session（登录状态和业务入口）
  -> message_handler
  -> logic / RepositoryHub / MySQL
```

业务主动推送链路是：

```text
logic / client_session
  -> NoticeService
  -> session_manager
  -> client_session::package_message/package_chat_message
  -> connection
```

适合复用的部分：

- `client_session` 的注册、登录、私聊、群聊、好友等业务。
- `message_handler` 的 JSON `type` 分发。
- `NoticeService` 和 `session_manager` 的按 UID 推送语义。
- `logic/*`、`db/*` 和仓储接口。

不能直接复用的部分：

- `connection` 内含 fd、epoll fd、TCP 帧编解码和 `sender/receiver`，不能作为 WebSocket 连接基类。
- `client_session` 的连接字段和文件处理目前直接使用具体 `connection`。
- `kick_offline/logout/exit_self` 直接操作 `sender_obj()` 和 `close()`，不适用于异步 WebSocket。

因此，接入点不是在 `connection` 里增加 WebSocket 判断，而是在 `client_session` 与连接之间抽象一个传输接口。

## 3. 接入前必须确认的现有问题

这些问题不是 WebSocket 本身造成的，但如果不先处理，会在双网络层下放大。

### 3.1 TCP 帧总长度实现与文档不一致（P0）

当前发送端写入：

```cpp
total_len = 8 + json_size + file_size;
packet_size = 8 + json_size + file_size;
```

但 `receiver::process_recv_data()` 按下面方式读取：

```cpp
if (in_buffer.size() < 4 + total_len) wait;
file_len = total_len - 4 - json_len;
erase(4 + total_len);
```

这会比实际包长多等待和多消费 4 字节。按照《客户端接口文档》的定义，`total_len` 是整个帧长度，建议统一为：

```cpp
if (total_len < 8 || in_buffer.size() < total_len) wait;
if (json_len > total_len - 8) reject;
file_len = total_len - 8 - json_len;
erase(total_len);
```

WebSocket 文本消息不复用该 TCP 总长度字段。WebSocket 文件二进制负载只保留一个 4 字节 JSON 长度，见第 6 节。

### 3.2 登录会话在异常断线时可能残留（P0）

当前未登录会话用 fd 存进 `session_manager`，登录后 key 改成 UID；但 `sub_reactor::remove_client(fd)` 仍只删除 fd。客户端异常断线时，UID 对应的会话可能继续留在在线表。

另一个问题是 `logout()` 删除 UID 后没有把会话重新加入 fd key，后续再次登录时 `find_session(session_key_)` 可能取不到自身，导致新 UID 映射没有建立。

建议借本次改动把 `session_manager` 收敛为“只保存已登录 UID”：

- 未登录会话由连接对象持有，不进入全局在线表。
- 登录时通过 `shared_from_this()` 注册 UID。
- 登出、断线时按“UID + 会话对象身份”条件删除。
- 新登录替换旧登录时，先原子替换映射，再在锁外通知旧会话下线。

### 3.3 文件上传参数使用错误（P0）

`File_handler::handle_message()` 当前重新声明了局部 `file_data`，并从 `message["file_data"]` 读取：

```cpp
std::string file_data = message["file_data"];
```

实际二进制文件数据已经由帧解析器通过函数参数传入。这里应直接使用参数 `file_data`，否则按现有客户端文档发送的文件块无法正常处理。

### 3.4 当前关闭连接不具备“发送完成后关闭”语义（P1）

`exit_self()` 先把 Goodbye 放入发送缓冲，随后立即关闭 fd，消息可能来不及发出。WebSocket 的 close handshake 更不能用“直接关 fd”代替。

传输接口需要区分：

- `immediate`：协议错误、IO 错误，立即中止。
- `after_pending_writes`：顶号、主动退出，发送队列清空后执行 TCP close 或 WebSocket close handshake。

### 3.5 `logout` 未注册到消息处理器（P0，实施时发现）

`client_session::init_()` 从未注册 `handlers_["logout"]`，而 `Base_handler` 已实现 `logout` 分支，导致客户端发送 `{"type":"logout"}` 时服务端返回 `Unknown command type.`。登出是 M0 会话生命周期的关键路径，已随 M0 修复（补上 `handlers_["logout"]`）。

### 3.6 `sub_reactor::add_connect` 的 ET 首包竞态（P0，实施时发现）

原实现先 `epoll_ctl(EPOLL_CTL_ADD)`，再创建 `connection` 并插入 `connections_by_fd`。在边沿触发模式下，若数据在两步之间到达，事件先触发而 `get_connection(fd)` 仍为空，该次边沿被丢弃，表现为客户端连接后首个请求偶发无响应（直到下一次数据到达）。已改为“先入 map、再注册 epoll”，并用 40 次连发首包回归验证。

## 4. 目标架构

```text
                              +-------------------------+
                              | client_session          |
                              | message_handler         |
                              | NoticeService / repos   |
                              +------------+------------+
                                           |
                                  weak_ptr<IClientTransport>
                                           |
                 +-------------------------+-------------------------+
                 |                                                   |
       +---------v----------+                              +---------v----------+
       | connection         |                              | WsSession          |
       | epoll + TCP        |                              | Asio + Beast       |
       | 8B header framing  |                              | RFC6455 framing    |
       +---------+----------+                              +---------+----------+
                 |                                                   |
       main/sub reactor                                    io_context + strand
```

新增目录建议：

```text
src/
├── net_common/
│   └── client_transport.h
└── net_core_ws/
    ├── ws_protocol.h/.cpp
    ├── ws_session.h/.cpp
    └── ws_server.h/.cpp
```

不建议新增独立 `ws_link`：`WsSession` 本身实现 `IClientTransport` 即可，额外转发对象只会增加一次生命周期跳转。

## 5. 传输接口设计

新增 `src/net_common/client_transport.h`：

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include <string>

enum class CloseMode {
    immediate,
    after_pending_writes,
};

class IClientTransport {
public:
    virtual ~IClientTransport() = default;

    // 可从任意业务线程调用；实现必须自行切回所属 IO 线程并复制参数。
    virtual void send_packet(nlohmann::json message,
                             std::string file_data = {}) = 0;

    // 第一阶段保留现有文件收发职责，减少改动面。
    virtual void send_file(std::string file_name) = 0;
    virtual void accept_file_chunk(nlohmann::json meta,
                                   std::string file_data) = 0;

    virtual void close(CloseMode mode) = 0;
};
```

接口约束：

- `send_packet()` 的 JSON 是完整服务器响应对象，不再传 `message + type` 给网络层拼 JSON。
- 参数按值传递是有意设计。WebSocket 实现需要跨线程异步使用，必须拥有数据副本。
- `connection` 和 `WsSession` 都实现该接口。
- `client_session` 只持有 `std::weak_ptr<IClientTransport>`，连接对象继续强持有 `client_session`，避免引用环。
- `send_file/accept_file_chunk` 暂时保留在接口中是迁移折中。后续可把文件读写提取为独立 `FileTransferService`，但不建议和首次 WS 接入同时扩大改动。

## 6. WebSocket 应用协议

### 6.1 地址

```text
ws://<host>:8080/ws
```

握手时只接受：

- HTTP 方法为 `GET`。
- Upgrade 为 WebSocket。
- target 为 `/ws`（如需参数，只允许 `/ws?...`）。

其他 HTTP 请求返回 `404` 或 `426 Upgrade Required`，不要静默升级任意路径。

### 6.2 普通消息

客户端到服务端、服务端到客户端都使用 WebSocket text message，payload 是完整 UTF-8 JSON：

```json
{"type":"login","UID":1001,"password":"123456"}
```

业务 JSON 字段保持《客户端接口文档》现状，不增加 WebSocket 专属业务字段。

### 6.3 文件消息

文件块使用 WebSocket binary message。WebSocket 已提供完整消息边界，不需要 TCP 的 `total_len`：

```text
+----------------------+----------------------+-------------------+
| 4 字节 json_len      | json_len 字节 JSON   | 剩余文件数据      |
| uint32 网络字节序    | UTF-8                | binary            |
+----------------------+----------------------+-------------------+
```

解析规则：

```text
payload_size >= 4
json_len <= payload_size - 4
JSON 必须可解析
JSON type 必须是 upload_file/file_chunk 对应方向允许的类型
file_size 必须等于 meta.chunk_size
file_size <= SEND_CHUNK_SIZE
```

服务端下发下载文件块同样使用该二进制格式。

### 6.4 大小限制

建议初始限制：

```text
单条 text message       <= 1 MiB
单条 binary message     <= 1 MiB
单个文件块              <= 256 KiB
单连接待发送队列        <= 8 MiB
```

`WsSession` 调用 Beast 的 `read_message_max()`。超过读限制使用 close code `too_big`；发送队列超过限制使用 `policy_error` 并关闭连接，防止慢客户端无限占用内存。

### 6.5 心跳

第一阶段继续使用现有应用层消息：

```json
{"type":"heartbeat"}
```

服务端回复 `heartbeat_ack/pong`，并在收到任意完整消息时刷新活动时间。Beast 会处理 RFC6455 ping/pong 控制帧，但第一阶段不再额外启动一套主动 ping 定时器，避免两套超时策略互相影响。

## 7. 逐文件改动

### 7.1 `src/net_core/client_session.h`

改动：

1. 删除 `#include "connection.h"`，改为包含 `client_transport.h`。
2. `client_session` 继承 `std::enable_shared_from_this<client_session>`。
3. 把 `std::weak_ptr<connection> conn_` 改为私有 `std::weak_ptr<IClientTransport> transport_`。
4. `set_connection()` 改名为 `set_transport()`。
5. 删除返回具体连接的 `conn()`。
6. 新增 `upload_file()`、`download_file()` 和 `on_disconnected()`。
7. `session_key_` 改成明确的 `logged_in_uid_`，未登录值为 `-1`。

建议签名：

```cpp
class client_session : public std::enable_shared_from_this<client_session> {
public:
    void set_transport(const std::shared_ptr<IClientTransport>& transport);
    void on_disconnected();

    void upload_file(const json& meta, const std::string& file_data);
    void download_file(const std::string& file_name);

private:
    int logged_in_uid_ = -1;
    std::weak_ptr<IClientTransport> transport_;
};
```

### 7.2 `src/net_core/client_session.cpp`

消息发送统一在业务层构造 JSON：

```cpp
void client_session::package_message(const std::string& content,
                                     std::string type) {
    json message{{"type", std::move(type)}, {"content", content}};
    if (auto transport = transport_.lock()) {
        transport->send_packet(std::move(message));
    }
}
```

`package_chat_message()` 同样在这里增加 `message_id/group_UID`，然后调用 `send_packet()`。这样 TCP 与 WS 看到完全相同的业务 JSON。

关闭行为改为：

```text
kick_offline:
  send_packet(顶号通知)
  close(after_pending_writes)

logout:
  send_packet(登出成功)
  从在线 UID 表解绑
  清空账户状态
  不关闭传输

exit_self:
  send_packet(Goodbye)
  从在线 UID 表解绑
  close(after_pending_writes)

on_disconnected:
  从在线 UID 表按对象身份解绑
  online = false
  清理 current_account_/social_manager_
```

登录改为通过 `shared_from_this()` 注册：

```cpp
auto self = shared_from_this();
auto old = session_manager::get_instance().replace_online(UID, self);
logged_in_uid_ = UID;
if (old && old.get() != this) {
    old->kick_offline();
}
```

注意 `replace_online()` 只做映射替换，不能在持有 `sessions_mutex` 时调用 `kick_offline()`。

### 7.3 `src/net_core/session_manager.h/.cpp`

在线表只按 UID 保存已登录会话，建议接口改为：

```cpp
std::shared_ptr<client_session> replace_online(
    int uid, std::shared_ptr<client_session> session);

void remove_online_if_same(
    int uid, const client_session* expected);

std::shared_ptr<client_session> find_session(int uid);
```

`remove_online_if_same()` 必须校验指针身份，防止旧连接晚到的断线回调把刚登录的新会话删除：

```cpp
void session_manager::remove_online_if_same(
    int uid, const client_session* expected) {
    std::unique_lock lock(sessions_mutex);
    auto it = sessions_.find(uid);
    if (it != sessions_.end() && it->second.get() == expected) {
        sessions_.erase(it);
    }
}
```

删除“未登录 fd 也放入 `session_manager`”的逻辑。

### 7.4 `src/net_core/message_handler.cpp`

文件处理不再获取具体 `connection`：

```cpp
if (type == "upload_file") {
    session.upload_file(message.at("meta"), file_data);
    return;
}
if (type == "download_file") {
    session.download_file(message.at("file_name").get<std::string>());
    return;
}
```

这里同时修复当前忽略函数参数 `file_data` 的问题。

建议给 `handle_message()` 最外层补字段类型异常处理。当前大量 `message["field"]` 在字段缺失或类型错误时会抛异常，WebSocket 公网接入后不能让异常越过业务线程入口。

### 7.5 `src/net_core/connection.h/.cpp`

让 `connection` 实现 `IClientTransport`：

```cpp
class connection : public IClientTransport {
public:
    void send_packet(json message, std::string file_data = {}) override;
    void send_file(std::string file_name) override;
    void accept_file_chunk(json meta, std::string file_data) override;
    void close(CloseMode mode) override;
};
```

主要调整：

- 原 `send_json_packet()` 扩展为可以附带文件数据。
- 原 `package_message/package_chat_message` 移到 `client_session`，避免 TCP/WS 重复构造 JSON。
- `accept_file_chunk()` 转发给当前 `receiver::upload_file()`。
- `close(after_pending_writes)` 设置关闭标记；发送缓冲清空后再关闭 fd。
- 关闭只执行一次，并调用 `session_->on_disconnected()`。
- `sub_reactor` 提供连接移除回调，确保主动关闭时也从 `connections_by_fd` 和 epoll 中清理，而不是等待一个不一定再出现的 fd 事件。

### 7.6 `src/net_core/receiver_sender.h/.cpp`

改动：

- 修正第 3.1 节的 TCP 长度计算。
- `sender::send_msg()` 返回发送缓冲是否已经清空，供 `close(after_pending_writes)` 判断。
- 增加最大 TCP 帧长度校验，不能仅按客户端给出的长度持续扩容等待。
- `send_file()` 打开文件失败、空文件、非法路径时返回明确错误。

文件名必须经过路径约束。至少拒绝绝对路径和 `..`，否则 `download_file` 可读取 `SERVER_SAVING_PATH` 之外的文件。

### 7.7 `src/net_core/epoller.cpp`

改动：

- 创建连接后调用 `session->set_transport(conn)`。
- 不再把未登录 session 以 fd 加入 `session_manager`。
- `remove_client()` 获取连接后先触发一次统一关闭/断线清理，再删除 epoll 和 fd 映射。
- 主动关闭通过回调进入 `remove_client()`，保证 map 不残留。

需要避免回调重入：关闭状态使用 `std::atomic<bool>` 或在连接内部加锁保证只执行一次。

### 7.8 `src/net_core_ws/ws_protocol.h/.cpp`（新增）

只负责可单元测试的纯协议函数：

```cpp
std::string encode_binary(const json& meta, const std::string& bytes);
Standard_Message decode_binary(beast::string_view payload);
```

要求：

- 长度字段使用网络字节序。
- 使用 `memcpy` 读取整数，避免未对齐指针转换。
- 校验长度后再切片。
- 不在该层做业务 `type` 分发。

### 7.9 `src/net_core_ws/ws_session.h/.cpp`（新增）

`WsSession` 同时负责一个 WebSocket 连接和 `IClientTransport` 实现：

```cpp
class WsSession final
    : public IClientTransport,
      public std::enable_shared_from_this<WsSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer read_buffer_;
    std::deque<OutgoingMessage> write_queue_;
    std::shared_ptr<ThreadPool> business_pool_;
    std::shared_ptr<client_session> session_;
};
```

核心规则：

1. 握手、读、写、定时器和关闭全部在 `ws_.get_executor()` 上执行。
2. `send_packet()` 可由任意线程调用，但内部第一步必须 `net::post(ws_.get_executor(), ...)`。
3. 同一连接同时只能有一个 `async_write`，其余消息进入 FIFO 队列。
4. 队列元素拥有完整 payload，直到对应写回调结束。
5. 收到一条完整 WS 消息后提交到 `business_pool_` 执行 `client_session::on_message()`。
6. 该业务任务完成后才投递下一次 `async_read`，保证同一连接的登录、登出、聊天等操作有序，也形成自然的单连接读背压。
7. 业务任务外围捕获异常，返回协议错误，不能让线程池 worker 退出。
8. 所有失败路径最终只调用一次 `session_->on_disconnected()`。

写队列伪代码：

```cpp
void WsSession::send_packet(json message, std::string file_data) {
    auto self = shared_from_this();
    net::post(ws_.get_executor(),
        [self, message = std::move(message),
         file_data = std::move(file_data)]() mutable {
            self->enqueue(encode(message, file_data));
        });
}

void WsSession::do_write() {
    if (write_queue_.empty() || write_in_progress_) return;
    write_in_progress_ = true;
    ws_.text(write_queue_.front().is_text);
    ws_.async_write(net::buffer(write_queue_.front().payload),
                    beast::bind_front_handler(
                        &WsSession::on_write, shared_from_this()));
}
```

不要从业务线程直接调用 `ws_.write()`，也不要为每条消息启动一个并行 `async_write()`。

握手建议先读取 HTTP request，再验证 path 和 Upgrade，最后 `ws_.async_accept(request)`。建议设置：

```cpp
ws_.set_option(websocket::stream_base::timeout::suggested(
    beast::role_type::server));
ws_.read_message_max(max_message_bytes);
```

### 7.10 `src/net_core_ws/ws_server.h/.cpp`（新增）

职责：

- 创建 IPv6 acceptor，并设置兼容 IPv4（与当前服务行为一致）。
- `async_accept()` 新 socket。
- 每个连接创建一个 `WsSession`。
- 创建共享业务线程池。
- 启动配置数量的 `io_context.run()` 线程。
- 处理 `SIGINT/SIGTERM`，停止 accept 并关闭现有连接。

推荐默认线程数：

```text
io_threads = max(2, hardware_concurrency)
business_threads = 8
```

如果继续使用当前 `ThreadPool`，`WsServer` 必须用 `shared_ptr` 持有它直到所有 `WsSession` 停止，不能让 session 留下悬空线程池引用。

### 7.11 `src/net_core/server_config.h/.cpp`

新增配置：

```cpp
const std::string& net_layer() const;
int ws_io_threads() const;
std::size_t ws_max_message_bytes() const;
std::size_t ws_max_pending_bytes() const;
```

配置示例：

```json
{
    "net_layer": "websocket",
    "use_heartbeat": false,
    "heartbeat_interval": 60,
    "websocket": {
        "path": "/ws",
        "io_threads": 4,
        "max_message_bytes": 1048576,
        "max_pending_bytes": 8388608
    },
    "database": {
        "host": "localhost",
        "port": 3306,
        "user": "chat_server",
        "password": "Chat_123!",
        "dbname": "chat_server",
        "db_conn_count": 4
    }
}
```

兼容策略：

- 缺少 `net_layer` 时默认 `binary`，保证旧配置可启动。
- 值不是 `binary/websocket` 时启动失败并输出明确错误，不要静默选择另一个协议。
- 数值配置必须做上下限校验。

当前字符串 getter 在解锁后返回内部 `std::string` 引用。从严格线程安全角度，建议新旧字符串 getter 都改为按值返回；配置只在启动时加载虽然通常不会触发竞争，但接口本身不应暴露锁外可变对象引用。

### 7.12 `src/main.cpp`

把现有 TCP 启动代码提取成 `run_binary_server()`，然后按配置分流：

```cpp
int main() {
    auto& config = ServerConfig::get_instance();
    config.load("configure.json");

    MySQL_Conn_Pool::get_instance().init();

    if (config.net_layer() == "websocket") {
        return run_websocket_server(8080, config);
    }
    return run_binary_server(8080);
}
```

两套监听器不同时启动，因此可继续共用 8080。切换配置后需要重启服务。

### 7.13 `CMakeLists.txt`

新增：

```cmake
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws WS_NET_SRC)

find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_executable(server
    src/main.cpp
    ${NET_SRC}
    ${WS_NET_SRC}
    ...
)

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws
    ...
)

target_link_libraries(server PRIVATE
    nlohmann_json::nlohmann_json
    mysqlcppconn
    Boost::system
    Threads::Threads
)
```

说明：`net_layer` 是运行时选择，因此只要同一个二进制包含 WS 代码，Boost 就是编译期依赖，即使当前配置选择 `binary` 也需要安装。若后续确实需要构建不依赖 Boost 的纯 TCP 版本，再增加 CMake 选项 `ENABLE_WEBSOCKET`。

当前机器已安装 Boost 1.83 的 `libboost-dev` 和 `libboost-system-dev`，满足该方案。

## 8. 关键时序

### 8.1 收消息

```text
WsSession::async_read
  -> Beast 完成 WS 消息重组
  -> text: 直接取 JSON
     binary: ws_protocol 拆 JSON + 文件数据
  -> 提交 business_pool
  -> client_session::on_message
  -> handler / repo / NoticeService
  -> 业务任务结束
  -> post 到 WS executor
  -> 发起下一次 async_read
```

Asio IO 线程不执行 MySQL 查询。一个连接的消息不会并行业务处理，不会出现 login 尚未完成、下一条 private_chat 已开始执行的情况。

### 8.2 主动推送

```text
任意业务线程
  -> NoticeService
  -> session_manager.find_session(uid)
  -> client_session.package_message
  -> IClientTransport.send_packet
  -> post 到对应 WsSession executor
  -> 入单连接写队列
  -> async_write（严格一次一个）
```

### 8.3 顶号

```text
新连接 login(uid)
  -> session_manager.replace_online(uid, new_session)
  -> 返回 old_session
  -> 锁外 old_session.kick_offline()
  -> 旧 transport 入队顶号通知
  -> close(after_pending_writes)
  -> 旧 on_disconnected 尝试 remove_online_if_same
  -> 指针不匹配新 session，不会误删新登录
```

## 9. 分阶段实施

### M0：修复共同基础

改动：

- 修复 TCP 帧长度解析。
- 修复文件上传参数。
- 重构 `session_manager` 登录/登出/断线生命周期。
- 引入 `IClientTransport`，让现有 `connection` 实现它。

验收：现有 TCP 客户端全部功能保持可用，异常断线后 UID 不再显示在线，logout 后可以在同一连接重新登录。

> **状态：已完成（2026-06）。** 文件传输按需求暂缓，仅保持原有 TCP 文件能力可编译，未扩展 WebSocket 文件协议。详见第 13 节。

### M1：WebSocket 骨架

改动：

- 新增 `ws_protocol`、`WsSession`、`WsServer`。
- 完成 `/ws` 握手、text JSON 收发、close、消息大小限制、发送队列。
- 配置和 main 分流。

验收：注册、登录、heartbeat、show 可以通过 WS 完成。

> **状态：已完成（2026-06）。** 文件传输仍按需求暂缓，`WsSession` 对文件请求返回“暂不支持”提示；详见第 14 节。

### M2：聊天和通知

改动：

- 私聊、群聊、离线消息、好友/群通知走通。
- 验证 TCP 用户与 WS 用户使用同一业务层时的行为一致。当前二选一运行，主要做两种模式的对照回归。

验收：两个 WS 客户端并发登录和互发消息；顶号通知可靠到达；慢客户端不导致无限队列。

> **状态：已完成（2026-06）。** 聊天与通知链路全部复用同一套 `client_session`，无需改业务逻辑；本阶段补齐了 WS 层缺失的 `use_heartbeat` 空闲超时，并新增端到端冒烟脚本。详见第 15 节。

### M3：文件传输

改动：

- 接入 WS binary 文件块。
- 增加文件名安全校验、块大小校验和错误响应。
- 下载避免一次性把整个大文件塞入发送队列，按写完成回调继续读取下一块。

验收：空文件、单块文件、多块文件、同名文件、非法路径、超限块均有确定行为。

### M4：回归与文档

改动：

- 更新 `readme.md`、`项目说明文档.md`、`客户端接口文档.txt`。
- 增加 TCP/WS 功能对照表和部署配置说明。

验收：`binary` 和 `websocket` 配置分别启动并通过完整回归。

## 10. 测试清单

建议至少覆盖：

| 类别 | 用例 |
|---|---|
| 构建 | 干净目录 CMake configure/build 成功 |
| 配置 | 缺少 `net_layer` 默认 binary；非法值启动失败 |
| 握手 | `/ws` 成功；错误 path/普通 HTTP 被拒绝 |
| 文本协议 | 合法 JSON、非法 JSON、缺 type、字段类型错误 |
| 二进制协议 | 小于 4 字节、json_len 越界、非法 JSON、块大小不符 |
| 顺序 | 同连接连续发送 login + show，show 必须在 login 后执行 |
| 并发 | 多连接并发登录、私聊、群聊，IO 线程不阻塞 DB |
| 推送 | 业务线程向 WS 连接推送，多次写严格有序且无并行 write |
| 生命周期 | 主动 exit、浏览器直接关闭、网络中断、服务端停机 |
| 顶号 | 旧连接收到通知，新连接不会被旧断线回调移出在线表 |
| logout | 同一连接 logout 后再次 login，可被其他用户正常找到 |
| 心跳 | 开启/关闭配置、正常保活、空闲超时 |
| 背压 | 超过单连接写队列上限后按策略关闭 |
| 文件 | 上传/下载、多块重组、文件名穿越、超限、空文件 |
| TCP 回归 | 旧协议全部功能不因传输抽象而改变 |

建议新增纯协议单元测试覆盖 `ws_protocol`，再用 Beast 客户端或 Python `websockets` 做集成测试。测试客户端必须校验服务器主动推送，不能只做请求后同步等响应。

## 11. 风险和边界

### 11.1 数据库阻塞

现有 repo 是同步 MySQL 调用。WS 接入后必须继续放业务线程池，不能放 Asio IO 线程。当前连接池上限默认 4，而业务线程建议 8；高并发下部分业务线程等待 DB 连接属于现有容量约束，需要通过压测再调整。

### 11.2 发送背压

群聊广播可能同时向大量连接排队。每连接队列上限只能限制单个慢客户端；后续还可增加全局待发送字节指标和群广播限流，但不属于首次接入范围。

### 11.3 浏览器安全

`ws://` 是明文协议，登录密码也会明文传输。若跨机器或公网部署，应使用反向代理终止 TLS，通过 `wss://` 暴露服务，并限制允许的 `Origin`。首次接入可以先在可信网络使用 `ws://`，但不能把它当成公网最终形态。

### 11.4 暂不纳入

首次改动不包含：

- 同一端口自动识别原始 TCP 与 WebSocket。
- binary 和 websocket 两个监听器同时运行。
- JWT/token 鉴权。
- TLS 证书管理。
- Redis、跨进程在线状态和横向扩容。
- 全量重写现有文件传输模块。

如果以后要求旧 TCP 客户端和浏览器客户端同时在线，建议两个端口分别监听，而不是在同一端口猜协议；共享 `client_session/session_manager` 即可。

## 12. 预计改动面

| 类型 | 文件 |
|---|---|
| 新增 | `src/net_common/client_transport.h` |
| 新增 | `src/net_core_ws/ws_protocol.h/.cpp` |
| 新增 | `src/net_core_ws/ws_session.h/.cpp` |
| 新增 | `src/net_core_ws/ws_server.h/.cpp` |
| 修改 | `src/net_core/client_session.h/.cpp` |
| 修改 | `src/net_core/session_manager.h/.cpp` |
| 修改 | `src/net_core/message_handler.cpp` |
| 修改 | `src/net_core/connection.h/.cpp` |
| 修改 | `src/net_core/receiver_sender.h/.cpp` |
| 修改 | `src/net_core/epoller.cpp` |
| 修改 | `src/net_core/server_config.h/.cpp` |
| 修改 | `src/main.cpp`、`CMakeLists.txt`、`configure.json` |
| 测试/文档 | 协议单测、WS 集成测试、现有三份使用文档 |

推荐按 M0、M1、M2、M3 分开提交。M0 虽然没有新增 WebSocket 功能，但它决定后续两套传输是否能共享业务层并正确清理会话，不建议跳过。

## 13. M0 实施记录（已完成）

### 13.1 实际改动文件

| 文件 | 改动 |
|---|---|
| `src/net_common/client_transport.h` | 新增 `IClientTransport` 与 `CloseMode`，业务层只依赖抽象传输 |
| `src/net_core/client_session.h/.cpp` | 继承 `enable_shared_from_this`；`weak_ptr<IClientTransport>`；登录/登出/断线/顶号生命周期重构；`package_message` 在业务层组 JSON；新增 `on_disconnected/upload_file/download_file` |
| `src/net_core/session_manager.h/.cpp` | 在线表改为只存已登录 UID；`replace_online` + `remove_online_if_same` |
| `src/net_core/connection.h/.cpp` | 实现 `IClientTransport`；统一 `send_packet`；`close(after_pending_writes)`；断线回调 |
| `src/net_core/receiver_sender.h/.cpp` | `total_len` 改为整帧长度；`send_msg()` 返回是否清空；非法长度丢弃；`MSG_NOSIGNAL` |
| `src/net_core/epoller.h/.cpp` | 先入 map 再注册 epoll；EPOLLRDHUP；EPOLLOUT 后复核连接；关闭回调去重入 |
| `src/net_core/message_handler.cpp` | 文件上传改用传入的 `file_data` 参数 |
| `CMakeLists.txt` | 新增 `net_common` include；可选 `SERVER_BUILD_TESTS` 测试目标 |
| `tests/test_tcp_protocol.cpp` | 新增：整帧/半包/粘包/文件帧/非法长度/收发闭环 |
| `tests/test_session_manager.cpp` | 新增：顶号替换与“旧连接迟到断线不误删新登录” |

### 13.2 新增的可选测试

```bash
cmake -S . -B build -DSERVER_BUILD_TESTS=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

两个测试均不依赖 MySQL。

### 13.3 端到端冒烟结果

在真实 MySQL 上启动服务器后验证：

- 连接后立即发送首包（40/40 成功），确认 ET 竞态已修复。
- 注册、登录、心跳、`Unknown command type`、缺少 `type` 提示均正常。
- 顶号：A 登录后 B 用同一 UID 登录，A 收到顶号通知且连接关闭。
- B 在 A 断线后仍在线（旧连接断线未误删新会话）。
- `logout` 后同一连接可再次登录（修复了未注册 `logout` 处理器的问题）。
- `exit` 返回 Goodbye 后连接关闭。

### 13.4 本阶段未做

- WebSocket 网络层、握手、帧协议（M1 起）。
- WebSocket 文件二进制协议；文件部分仅保持原 TCP 能力可编译。
- 群缓存引用计数在“断线”路径的释放时机调整（当前随会话最终析构释放，不再与此前并发处理任务竞争）。

## 14. M1 实施记录（已完成）

### 14.1 新增/改动文件

| 文件 | 改动 |
|---|---|
| `src/net_core_ws/ws_protocol.h/.cpp` | 新增：WebSocket binary 应用层编解码（4B json_len + JSON + file） |
| `src/net_core_ws/ws_session.h/.cpp` | 新增：单连接会话，实现 `IClientTransport`；HTTP 握手 + 路径校验 + text/binary 读 + 单连接发送队列 + close |
| `src/net_core_ws/ws_server.h/.cpp` | 新增：Asio acceptor（IPv6 双栈）、`run_websocket_server()` 入口 |
| `src/net_core/server_config.h/.cpp` | 新增 `net_layer / ws_path / ws_io_threads / ws_max_message_bytes / ws_max_pending_bytes`；字符串 getter 改为按值返回 |
| `src/main.cpp` | 拆出 `run_binary_server()`，按 `net_layer` 分流 |
| `CMakeLists.txt` | 新增 `net_core_ws` 源目录/include；`find_package(Boost COMPONENTS system)`；链接 `Boost::system`、`Threads`；新增 `ws_protocol` 测试 |
| `configure.json` | 新增 `net_layer`（默认 `binary`）与 `websocket` 配置块 |
| `tests/test_ws_protocol.cpp` | 新增：binary 编解码、长度越界、短 payload |

### 14.2 运行时配置

```jsonc
{
  "net_layer": "binary",          // 或 "websocket"
  "websocket": {
    "path": "/ws",
    "io_threads": 4,
    "max_message_bytes": 1048576,
    "max_pending_bytes": 8388608
  }
}
```

- `net_layer` 缺省为 `binary`，旧配置可直接启动。
- 非法值（非 `binary/websocket`）启动失败并输出明确错误。
- 两种模式都监听 `8080`，但同一时刻只启动一个。

### 14.3 关键实现点

- **握手**：`http::async_read` 读取升级请求 → 校验 `Upgrade` 与 `path`（允许 query）→ `ws_.async_accept()`。非升级请求返回 `426`，错误路径返回 `404`。
- **线程**：每个连接接受时绑定独立 `strand`；所有 socket 操作 post 回该 strand。业务提交到共享 `ThreadPool`。
- **顺序**：收到一条完整消息 → 提交业务线程 → 业务完成后回 strand 再发起下一次读，保证单连接消息按序处理并形成读背压。
- **发送**：任意业务线程 `send_packet()` 只 post 到 strand 入队；单连接同时只有一个 `async_write`。发送队列超过 `max_pending_bytes` 则关闭连接。
- **大小限制**：`read_message_max(max_message_bytes)`；超过时 Beast 以 `message_too_big` 关闭。
- **关闭**：`immediate` 直接 teardown；`after_pending_writes` 先排空写队列再 `async_close`。`teardown()` 幂等，且只调用一次 `session_->on_disconnected()`。
- **文件**：M1 阶段 `send_file/accept_file_chunk` 返回“暂不支持”系统消息，不实现二进制文件收发（M3）。

### 14.4 端到端冒烟结果

用最小 Python WebSocket 客户端（标准库手写握手/帧）在真实 MySQL 上验证：

- 错误路径 `/nope` → `404`；`/ws` 握手返回合法 `Sec-WebSocket-Accept`。
- `heartbeat` → `heartbeat_ack/pong`；未知命令与缺少 `type` 正常提示。
- binary 非法帧（json_len 越界）→ 返回 `Invalid binary frame` 且连接保持。
- 注册、登录、`show` 均可通过 WS 完成。
- 顶号：第二个 WS 登录后第一个收到顶号通知；第二个连接不受第一个断线影响。
- `close` 握手正常；`SIGTERM` 可干净退出。
- 同一二进制切回 `net_layer=binary` 后，原 TCP 冒烟测试全部通过（无回归）。

### 14.5 M1 未做（留给后续）

- WebSocket 文件二进制收发（M3）。
- `wss/TLS`、`Origin` 校验、token 鉴权。
- 群广播级别的全局发送限流（当前只有单连接队列上限）。


## 15. M2 实施记录（已完成）

### 15.1 结论

M2 的“聊天和通知”**不需要改动 `client_session` 及任何业务逻辑**：私聊、群聊、离线消息、好友/群通知在 M0 引入 `IClientTransport` 后已经是传输无关的，WS 会话与 TCP 连接共用同一条业务链路。因此本阶段的实质工作是：

1. 补齐 WS 层唯一的行为缺口——服务端空闲超时。
2. 用真实 MySQL + 真实服务端跑端到端聊天/通知回归。
3. 增加可复用的冒烟脚本，作为后续 M3/M4 的回归基线。

### 15.2 新增/改动文件

| 文件 | 改动 |
|---|---|
| `src/net_core_ws/ws_session.h/.cpp` | 新增 `idle_timer_` / `last_active_` 与 `touch()` / `schedule_idle_check()`；`teardown()` 取消计时器 |
| `tests/ws_chat_smoke.py` | 新增：标准库 WebSocket 客户端 + 注册/登录/好友/私聊/群聊/离线/顶号端到端脚本 |

### 15.3 空闲超时实现

与 binary 层 `sub_reactor::check_idle_connections` 对齐：

- 仅当 `ServerConfig::use_heartbeat()` 为真时启动 `steady_timer`，每秒检查一次。
- 每收到一条完整消息调用 `touch()` 刷新 `last_active_`（与 binary 在 `append_raw_data` 刷新一致）。
- 空闲秒数达到 `heartbeat_interval` 时走 `close(after_pending_writes)`，先排空写队列再做 WebSocket close。
- `teardown()` 取消计时器；计时器回调对 `operation_aborted`/`closed_` 直接返回，保证幂等。
- 未启用 `use_heartbeat` 时不启动计时器，零额外开销。

> 说明：应用层 `heartbeat`/`heartbeat_ack` 消息在 M1 已可用；本节补的是**服务端主动判空闲并断开**，此前 WS 层完全没有，属于两套网络层的行为不一致点。

### 15.4 端到端冒烟结果

环境：真实 MySQL（`chat_server`），`net_layer=websocket`，`tests/ws_chat_smoke.py`。

| 步骤 | 结果 |
|---|---|
| 两账号注册 / WS 登录 | ✅ |
| 好友申请通知、接受后通知申请人 | ✅ |
| 私聊实时投递并携带 `message_id` | ✅ |
| 建群 / 拉人通知 / 群聊广播携带 `group_UID` | ✅ |
| 对方离线时落库，重连登录后补发离线消息 | ✅ |
| 顶号：旧连接收到通知，新连接保持可用 | ✅ |
| 空闲超时（`use_heartbeat=true, interval=3`） | ✅ 服务端日志 `[WsSession] idle timeout` |
| binary 模式回归（heartbeat / login） | ✅ 无回归 |

### 15.5 浏览器实测修复：tcp_stream 30s 硬超时

浏览器实测时连接会在建连后约 30 秒被服务端关闭（页面收到 `code=1006`，服务端日志
`[WsSession] read: The socket was closed due to a timeout`）。

根因：`run()` 里为 HTTP 升级请求读写的超时用了 `ws_.next_layer().expires_after(30s)`。
`tcp_stream` 的 `expires_after` 是**整条连接的硬超时**，与 WebSocket 自身的
`stream_base::timeout`（独立 `stream_impl::timer`，`suggested(server)` 的 idle=300s）
是两套机制；它不会因为后续读写被刷新，所以 30 秒后底层读被 `beast::error::timeout` 取消。

修复（`ws_session.cpp`）：

- HTTP 请求读仍设 30s `expires_after`，但在 `on_http_read` 入口立即 `expires_never()` 清除，
  避免它变成整条连接的硬超时。
- 不再用 `timeout::suggested(server)`（idle=300s，且会额外发 ping，语义与 binary 层不一致），
  改为显式设置：`handshake_timeout=30s`、`idle_timeout=none()`、`keep_alive_pings=false`。
  空闲断开统一交给 M2 的 `use_heartbeat` 检测，保持两套网络层行为一致。

复现与验证：修复前两条连接（一条不发、一条每 10s 心跳）都在 `t=30.0s` 被关闭；
修复后同样条件观测 46s 均保持连接；随后完整 M2 冒烟（含空闲超时）与 ctest 全部通过。

> 附带改动：`tests/ws_browser_test.html` 在通过 HTTP 提供时自动把 Host 填成 `location.hostname`，
> 方便宿主机直接打开测试页。

### 15.6 M2 未做（留给后续）

- WebSocket 文件二进制收发（M3）。
- 群广播级别的全局发送限流；当前只有单连接 `max_pending_bytes` 上限。
- 慢客户端的自动化压测（脚本未覆盖写队列溢出路径，靠单连接队列上限兜底）。
- `wss/TLS`、`Origin` 校验、token 鉴权。
