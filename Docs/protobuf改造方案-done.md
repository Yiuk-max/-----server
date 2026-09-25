# 网络消息体 JSON → Protobuf 改造方案（方案 A）

> **实施状态：已完成（2026-09-15）。** 后端两种网络模式、Vue 前端、TCP/WS 协议单测、WebSocket 端到端冒烟和低阶浏览器测试页均已切换到 protobuf；当前线上字段与帧格式以 `src/proto/message.proto` 和《客户端接口文档.txt》为准。

## 0. 目标与范围

- **目标**：把 `binary`（epoll/TCP）与 `websocket`（Boost.Asio/Beast）两种网络模式中，作为消息体使用的 JSON 全部替换为 protobuf。
- **接口形态**：`IClientTransport` 的发送接口直接**传 protobuf 对象**，由各传输实现负责序列化。
- **不兼容旧 JSON 网络协议**：不做新旧协议共存，不保留 JSON 网络层兼容。
- **保留 JSON 的场合**（与网络协议无关，不在此次改造范围）：
  - `configure.json` 配置读取（`src/utils/server_config.cpp`）
  - 数据库 `Account.settings` JSON 列（`src/db/repo/account_repo.cpp`）
  - 因此 `nlohmann/json` 继续保留在构建中，仅不再用于网络消息。

---

## 1. 总体设计

采用**大信封 Envelope**方案：一个 protobuf `Envelope` 承载所有请求/响应字段，`type` 仍为字符串，与当前 `handlers_.find(type)` 的分发方式完全兼容。

```
客户端 ──protobuf 字节──▶ 传输层切帧 ──bytes──▶ client_session::on_message
                                                    │ ParseFromString
                                                    ▼
                                              chat_proto::Envelope
                                                    │ type 分发
                                                    ▼
                                          message_handler（读字段）
                                                    │ 调用业务
                                                    ▼
                                          client_session 构造 Envelope
                                                    │ SerializeToString
                                                    ▼
                                          transport->send_packet(env)
```

---

## 2. 新增 proto 定义

新增文件：`src/proto/message.proto`

```proto
syntax = "proto3";

package chat_proto;

// 被回复消息摘要（对应原 JSON reply_to）
message ReplyTo {
  int32  message_id  = 1;
  string sender_name = 2;
  string content     = 3;
}

// 登录成功返回的用户信息（对应原 JSON user）
message UserInfo {
  int32  id       = 1;
  string username = 2;
}

// 聊天历史单条记录（对应 history_response.messages[]）
message ChatItem {
  int32  message_id          = 1;
  int32  sender_UID          = 2;
  string sender_name         = 3;
  bool   is_group            = 4;
  string content             = 5;
  string timestamp           = 6;
  int32  reply_to_message_id = 7;
  ReplyTo reply_to           = 8;
}

// 文件分块元信息（对应 upload_file.meta / 下载 file_chunk 头部）
message FileChunkMeta {
  string type        = 1;  // 恒为 "file_chunk"
  string file_id     = 2;
  string filename    = 3;
  uint64 total_size  = 4;
  uint32 chunk_index = 5;
  uint32 chunk_count = 6;
  uint32 chunk_size  = 7;
  uint64 offset      = 8;
}

// 顶层信封（方案 A：一个 message 承载所有请求/响应字段）
message Envelope {
  string type    = 1;  // 对应原 JSON type，保留字符串便于 handlers_ 分发
  string content = 2;  // 对应原 JSON content

  // 私聊/群聊
  int32  target_UID = 3;
  string message    = 4;

  // 消息 ID / 历史分页
  int32  message_id = 5;
  int32  peer_id    = 6;
  int32  before_id  = 7;
  bool   has_more   = 8;

  // 群相关
  int32  group_UID       = 9;
  int32  target_user_UID = 10;
  int32  requester_UID   = 11;
  string group_name      = 12;
  string new_name        = 13;
  bool   promote         = 14;

  // 好友相关
  int32  friend_UID    = 15;
  int32  sender_UID    = 16;
  string email         = 17;  // add_friend/set_email/register；login 时非空表示邮箱登录
  string apply_message = 18;
  string remark        = 19;
  bool   accept        = 20;

  // 账号/登录
  int32    UID      = 21;  // login(UID)
  string   username = 22;
  string   password = 23;
  string   token    = 24;
  UserInfo user     = 25;  // login_success

  // 聊天消息附加字段
  string  sender_name         = 26;
  string  timestamp           = 27;
  int32   reply_to_message_id = 28;
  ReplyTo reply_to            = 29;

  // 文件
  string        file_name = 30;
  FileChunkMeta meta      = 31;

  // 历史响应
  repeated ChatItem messages = 32;
}
```

> 说明：protobuf3 标量字段缺省为 `0`/`""`/`false`，与现有代码「缺失字段按空值处理」的语义一致，因此大部分字段不需要 `optional`。
> 唯一需要区分「存在性」的是登录的邮箱/UID 判断，用 `!msg.email().empty()` 即可（空邮箱登录本就不合法）。

---

## 3. 线格式变化

| 模式 | 现在 | 改成 |
|---|---|---|
| `binary` TCP 业务帧 | `4B total_len \| 4B json_len \| JSON \| file` | `4B total_len \| 4B payload_len \| protobuf bytes \| file` |
| `websocket` 业务消息 | **text 帧**，payload = JSON 字符串 | **binary 帧**，payload = `4B payload_len \| protobuf` |
| `websocket` 文件消息 | binary 帧 `4B json_len \| JSON \| file` | binary 帧 `4B payload_len \| protobuf \| file` |

> 长度字段语义从 `json_len` 更名为 `payload_len`（仍是 uint32 网络字节序，值 = protobuf 字节数）。
> WebSocket 业务消息也统一加 `4B payload_len` 头，与文件消息共用 `ws_protocol::encode_binary/decode_binary`，避免在 on_read 中区分两种 binary 帧。

---

## 4. 逐文件改动明细

### 4.1 新增 + CMake

**新增 `src/proto/message.proto`**，内容见第 2 节。

**修改 `CMakeLists.txt`**：

```cmake
# 必须用大写 Protobuf（走 FindProtobuf 模块，提供 protobuf::libprotobuf 与 protobuf_generate_cpp）
find_package(Protobuf REQUIRED)

# 新增：生成 protobuf C++ 源文件
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/message.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# add_executable 中追加 ${PROTO_SRCS}
add_executable(server src/main.cpp ${NET_SRC} ${WS_NET_SRC} ${LOGIC_SRC}
               ${UTILS} ${REPO_SRC} ${REPO_INTERFACE_SRC} ${MYSQL_SRC}
               ${PROTO_SRCS})

# include 生成目录（message.pb.h 输出位置）
target_include_directories(server PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ...)
```

> `protobuf::libprotobuf` 已链接，无需新增链接。

---

### 4.2 `src/net_common/client_transport.h`

```cpp
// 改前
virtual void send_packet(nlohmann::json message, std::string file_data = {}) = 0;
virtual void accept_file_chunk(nlohmann::json meta, std::string file_data) = 0;

// 改后
virtual void send_packet(const chat_proto::Envelope& message, std::string file_data = {}) = 0;
virtual void accept_file_chunk(const chat_proto::FileChunkMeta& meta, std::string file_data) = 0;
```

- 去掉 `#include <nlohmann/json.hpp>`，改为 `#include "message.pb.h"`。
- `send_file(std::string file_name)`、`close(CloseMode)` 保持不变。

---

### 4.3 `src/net_core/connection.h/.cpp`

`connection.h`：

```cpp
// 改前
void send_packet(json message, std::string file_data = {}) override;
void accept_file_chunk(json meta, std::string file_data) override;

// 改后
void send_packet(const chat_proto::Envelope& message, std::string file_data = {}) override;
void accept_file_chunk(const chat_proto::FileChunkMeta& meta, std::string file_data) override;
```

`connection.cpp`：

- `send_packet`：
  ```cpp
  void connection::send_packet(const chat_proto::Envelope& message, std::string file_data) {
      std::string payload;
      message.SerializeToString(&payload);
      const std::size_t frame_size = 8 + payload.size() + file_data.size();
      // ... total_len / payload_len 照旧，body 由 json_str 改为 payload
  }
  ```
- `process_incoming()`：`recv_result.json_part` → `recv_result.payload`，语义不变，仍把原始字节传给 `session_->on_message(...)`。
- `accept_file_chunk`：`receiver_->upload_file(meta, file_data)`，meta 类型已变。

---

### 4.4 `src/net_core/receiver_sender.h/.cpp`

`receiver_sender.h`：

```cpp
// Standard_Message：json_part → payload
struct Standard_Message {
    std::string payload;    // protobuf 字节
    std::string file_part;
    bool is_valid = false;
};

// 改前
void process_file_data(json &msg_json, std::string &data);
void upload_file(const json& meta, const std::string& data);

// 改后
void process_file_data(const chat_proto::FileChunkMeta& meta, std::string &data);
void upload_file(const chat_proto::FileChunkMeta& meta, const std::string& data);
```

`receiver_sender.cpp`：

- `receiver::process_recv_data`：只切字节，不关心内容是 JSON 还是 protobuf。仅把 `json_len` / `json_part` 命名改为 `payload_len` / `payload`，逻辑不变。
- `sender::send_file`：构造 meta 从 `nlohmann::json` 改为填充 `chat_proto::FileChunkMeta`（`set_type/set_file_id/set_filename/...`）。
- `sender::process_file_data`：
  ```cpp
  void sender::process_file_data(const chat_proto::FileChunkMeta& meta, std::string &data) {
      std::string payload;
      meta.SerializeToString(&payload);
      // 8 + payload.size() + data.size() 组帧
  }
  ```
- `receiver::upload_file`：读字段从 `meta["file_id"]` 改为 `meta.file_id()` 等访问器。

---

### 4.5 `src/net_core/client_session.h/.cpp`（核心改动）

`client_session.h`：

```cpp
// 改前
void upload_file(const json& meta, const std::string& file_data);
void on_message(const std::string& json_data, std::string file_data);
void package_json(const json& message);

// 改后
void upload_file(const chat_proto::FileChunkMeta& meta, const std::string& file_data);
void on_message(const std::string& payload, std::string file_data);
void package_envelope(const chat_proto::Envelope& message);  // 原 package_json
```

`client_session.cpp`：

**接收侧 `on_message`**：

```cpp
void client_session::on_message(const std::string& payload, std::string file_data) {
    // online 检查不变

    chat_proto::Envelope msg;
    if (!msg.ParseFromString(payload)) {
        package_message("Invalid protobuf message.\n", "system");
        return;
    }
    if (msg.type().empty()) {
        package_message("Invalid message format: missing 'type' field.\n", "system");
        return;
    }

    const std::string type = msg.type();
    if (type == "heartbeat") {
        package_message("pong", "heartbeat_ack");
        return;
    }

    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        it->second->handle_message(msg, *this, file_data);
    } else {
        package_message("Unknown command type.\n", "system");
    }
}
```

**发送侧三个函数**：

- `package_message(message, type)`：构造 `Envelope`，`set_type` / `set_content`，序列化后 `transport->send_packet(env)`。
- `package_chat_message(...)`：**参数列表保持不变**，实现改为填充 `Envelope` 字段：
  ```cpp
  chat_proto::Envelope env;
  env.set_type(type);
  env.set_content(message);
  env.set_message_id(message_id);
  if (group_uid > 0) env.set_group_uid(group_uid);
  if (sender_uid > 0) env.set_sender_uid(sender_uid);
  if (!sender_name.empty()) env.set_sender_name(sender_name);
  if (!timestamp.empty()) env.set_timestamp(timestamp);
  if (reply_to_message_id > 0) {
      env.set_reply_to_message_id(reply_to_message_id);
      auto* reply = env.mutable_reply_to();
      reply->set_message_id(reply_to_message_id);
      reply->set_sender_name(reply_sender_name);
      reply->set_content(reply_content);
  }
  transport->send_packet(env);
  ```
- `package_json(const json&)` → 删除，替换为 `package_envelope(const chat_proto::Envelope&)`，内部直接 `transport->send_packet(message)`。

**大 JSON 构造点重写**：

- `finish_login()`：
  ```cpp
  chat_proto::Envelope resp;
  resp.set_type("login_success");
  resp.set_token(token);
  auto* user = resp.mutable_user();
  user->set_id(UID);
  user->set_username(account->getName());
  package_envelope(resp);
  ```
- `chat_history()`：
  ```cpp
  chat_proto::Envelope resp;
  resp.set_type("history_response");
  resp.set_peer_id(peer_id);
  resp.set_has_more(has_more);
  for (const auto& m : page) {
      auto* item = resp.add_messages();
      item->set_message_id(m.message_id);
      item->set_sender_uid(m.sender_UID);
      item->set_sender_name(m.sender_name);
      item->set_is_group(m.is_group);
      item->set_content(m.content);
      item->set_timestamp(m.timestamp);
      if (m.reply_to_message_id > 0) {
          item->set_reply_to_message_id(m.reply_to_message_id);
          auto* reply = item->mutable_reply_to();
          reply->set_message_id(m.reply_to_message_id);
          reply->set_sender_name(m.reply_sender_name);
          reply->set_content(m.reply_content);
      }
  }
  package_envelope(resp);
  ```

- `upload_file(const chat_proto::FileChunkMeta& meta, ...)`：`transport->accept_file_chunk(meta, file_data)`。

---

### 4.6 `src/net_core/message_handler.h/.cpp`（机械但量大）

`message_handler.h`：

```cpp
#include "message.pb.h"

class Message_handler {
public:
    virtual void handle_message(const chat_proto::Envelope& message,
                                client_session& session,
                                std::string &file_data) = 0;
    // ...
};
```

`message_handler.cpp`：删除 `reply_id()` helper，所有 `const json&` 改 `const chat_proto::Envelope&`，字段读取对照替换：

| 现在 | 改成 |
|---|---|
| `std::string type = message["type"]` | `std::string type = message.type()` |
| `message["target_UID"]` | `message.target_uid()` |
| `message["message"]` | `message.message()` |
| `message["message_id"]` | `message.message_id()` |
| `message.contains("peer_id") && is_number_integer()` | `message.peer_id()`（默认 0） |
| `message.contains("before_id") && ...` | `message.before_id()` |
| `message.value("email", std::string())` | `message.email()` |
| `message.value("apply_message", std::string())` | `message.apply_message()` |
| `message["friend_UID"]` | `message.friend_uid()` |
| `message["remark"]` | `message.remark()` |
| `message["sender_UID"]` | `message.sender_uid()` |
| `message["group_UID"]` | `message.group_uid()` |
| `message["target_user_UID"]` | `message.target_user_uid()` |
| `message["new_name"]` | `message.new_name()` |
| `message["requester_UID"]` | `message.requester_uid()` |
| `message["accept"]` | `message.accept()` |
| `message["promote"]` | `message.promote()` |
| `message["group_name"]` | `message.group_name()` |
| `message.contains("email")`（login） | `!message.email().empty()` |
| `message["UID"]` | `message.uid()` |
| `message["password"]` | `message.password()` |
| `message.value("token", std::string())` | `message.token()` |
| `message.value("username", std::string())` | `message.username()` |
| `message["file_name"]` | `message.file_name()` |
| `json meta = message["meta"]` | `const auto& meta = message.meta()` |

`File_handler::upload_file` 分支改为：

```cpp
session.upload_file(message.meta(), file_data);
```

---

### 4.7 `src/net_core_ws/ws_protocol.h/.cpp`

为了让该编解码函数保持通用，改为操作**已序列化字节**，不再依赖具体 proto 类型：

`ws_protocol.h`：

```cpp
// 把 payload + 文件数据编码为 binary 消息（4B payload_len + payload + file）
std::string encode_binary(const std::string& payload, const std::string& file_data);

// 解析 binary 消息：payload / file_part / error
bool decode_binary(const std::string& frame,
                   std::string& payload,
                   std::string& file_part,
                   std::string& error);
```

`ws_protocol.cpp`：

- `encode_binary` 中 `meta.dump()` 改为直接用 `payload`。
- `decode_binary` 中 `json_len` / `json_part` 改为 `payload_len` / `payload`。

> 说明：`ws_protocol` 不再 include `nlohmann/json.hpp`，只 include `<string>` / `<cstdint>`。

---

### 4.8 `src/net_core_ws/ws_session.h/.cpp`

`ws_session.h`：

```cpp
void send_packet(const chat_proto::Envelope& message, std::string file_data = {}) override;
void accept_file_chunk(const chat_proto::FileChunkMeta& meta, std::string file_data) override;
void dispatch_to_business(std::string payload, std::string file_data);  // 原名 json_text
```

`ws_session.cpp`：

- `on_read()`：
  - 业务消息与文件消息统一走 `ws_protocol::decode_binary`（两者都是 `4B payload_len | protobuf | file` 的 binary 帧）。
  - text 帧视为旧 JSON 客户端，回一条 `system` 提示后恢复读取，不关闭连接。
  - 伪代码：
    ```cpp
    if (ws_.got_text()) {
        // 回 system：Text frames are not supported...
        do_read();
        return;
    }
    std::string body, file_data, error;
    if (!ws_protocol::decode_binary(payload, body, file_data, error)) {
        // 回 system：Invalid binary frame...
        do_read();
        return;
    }
    dispatch_to_business(std::move(body), std::move(file_data));
    ```
- `send_packet(const chat_proto::Envelope& message, std::string file_data)`：
  ```cpp
  std::string body;
  message.SerializeToString(&body);
  // 统一 binary 帧：| 4B payload_len | protobuf | file_data |
  std::string payload = ws_protocol::encode_binary(body, file_data);
  net::post(ws_.get_executor(),
            [s, payload = std::move(payload)]() mutable {
                s->enqueue(std::move(payload), /*is_text=*/false);
            });
  ```
  - `enqueue` 传入 `is_text=false`，`do_write()` 中 `ws_.text(false)` 即表示 binary 帧，无需改字段名。
- `send_file` / `accept_file_chunk`：当前是「WS 暂不支持文件」，改用 protobuf `Envelope` 构造系统提示：
  ```cpp
  chat_proto::Envelope env;
  env.set_type("system");
  env.set_content("File transfer is not supported over WebSocket yet.\n");
  send_packet(env, {});
  ```
- `ws_session.h` 中 `#include <nlohmann/json.hpp>` 相关删除，改为 `#include "message.pb.h"`。

---

### 4.9 `src/net_core/notice_service.h/.cpp`

**不改**。

只要 `client_session::package_message` / `package_chat_message` 的参数列表保持 `(string message, string type, ...)` 不变，`NoticeService` 只是透传，内部实现无需变动。

---

## 5. 不改动的模块

| 文件/模块 | 原因 |
|---|---|
| `src/logic/social_module.cpp`、`group.cpp`、`account.cpp` | 只拼字符串、经 `NoticeService` 发送，不直接接触网络 JSON |
| `src/db/**` | `Account.settings` 是 DB JSON 列，与网络协议无关 |
| `src/utils/server_config.cpp` | `configure.json` 是本地配置 JSON |
| `src/net_core/session_manager.*`、`epoller.*`、`group_manager.*` | 会话/IO/缓存，不解析消息体 |
| `src/net_core_ws/ws_server.cpp` | 只做 accept/静态资源，不碰消息体 |
| `include/total.h` | 保留 `nlohmann/json` 与 `using json`，供 config/DB 继续使用 |

---

## 6. 实施步骤（已按此顺序完成）

1. **proto + CMake**：新增 `src/proto/message.proto`，改 `CMakeLists.txt` 生成 `${PROTO_SRCS}`，确认 `protoc` 能生成 `message.pb.h/.pb.cc` 且链接通过。
2. **传输接口与两个传输实现**：改 `client_transport.h` → `connection.*` → `receiver_sender.*` → `ws_protocol.*` → `ws_session.*`，先把「JSON 序列化」换成「protobuf 序列化」。
3. **业务接收/发送**：改 `client_session.*` 的 `on_message` / `package_message` / `package_chat_message` / `package_envelope` / `finish_login` / `chat_history`。
4. **消息分发**：改 `message_handler.*` 全部字段访问。
5. **文件路径**：改 `upload_file` / `download_file` / `sender::send_file` / `receiver::upload_file` 的 meta 类型。
6. **清理**：删除网络相关文件里不再使用的 `nlohmann/json` include 与 `using json`。
7. **编译 + 单测 + 冒烟联调**。

---

## 7. 测试与验证

已同步更新的测试：

- `tests/test_tcp_protocol.cpp`：帧体从 JSON 文本改为 protobuf 字节；`make_frame` 的 `json_text` 改为 `payload`，断言相应调整。
- `tests/test_ws_protocol.cpp`：`ws_protocol::encode_binary/decode_binary` 入参从 `nlohmann::json` 改为 `std::string payload`。
- `tests/test_session_manager.cpp`：继续验证会话替换/断线语义，并链接生成的 protobuf 源文件。
- `tests/ws_chat_smoke.py`：用纯标准库 `tests/proto_codec.py` 发送/接收 protobuf binary 帧，覆盖注册、登录、好友、私聊、群聊、离线消息与顶号。
- `tests/ws_browser_test.html`：低阶浏览器测试页改为 protobuf binary 帧，不再发送 text JSON。
- `frontend/src/protobufProtocol.js`：Vue 前端通过 `protobufjs` 读取共享 schema，完整处理 `user/reply_to/messages` 等嵌套消息。

实际验证：后端与前端生产构建成功；TCP/WS/session_manager 单测通过；WebSocket 端到端冒烟全流程通过。

验证矩阵：

| 场景 | binary TCP | websocket |
|---|---|---|
| 注册/登录/登出/心跳 | ✅ | ✅ |
| 私聊/群聊/历史/删除 | ✅ | ✅ |
| 好友/群管理 | ✅ | ✅ |
| 文件上传/下载 | ✅（meta 改 proto） | 维持「不支持」提示 |
| 粘包/半包 | ✅ 单测 | WS 自带边界 |

---

## 8. 风险与注意点

1. **`type` 必须保留字符串**：否则 `handlers_` map 分发、心跳判断全部要重写。
2. **protobuf3 默认值语义**：缺失字段返回默认值，不再像 JSON 那样可严格区分「缺失/非法」。现有业务基本把缺失当空值处理，影响很小；唯一注意 `login` 的邮箱判断改为 `!email().empty()`。
3. **WebSocket 帧类型变化**：业务消息从 text 帧改为 binary 帧，`ws_.text()` → `ws_.binary(true)`，否则浏览器/客户端按 text 解析 protobuf 会失败。
4. **旧客户端不兼容**：这是破坏性变更；测试客户端、`web/` 前端与当前文档已同步更新。旧 JSON 客户端不会兼容，如需灰度需另做版本协商。
5. **字段命名风格**：proto 使用 snake_case 访问器（`target_uid()`），原 JSON key 是 `target_UID`，需要逐处对照，避免漏改。
6. **`send_packet` 的 `file_data` 参数**：当前业务路径实际从未传非空 `file_data`（文件走 `send_file`/`accept_file_chunk`），可保留参数但需在 `connection`/`ws_session` 中正确处理序列化边界。
7. **生成头文件 include 路径**：`message.pb.h` 默认生成在 `CMAKE_CURRENT_BINARY_DIR`，需把该目录加入 `target_include_directories`。
