# client_session 拆分方案（方案 B）

## 一、背景与目标

`src/net_core/client_session.cpp` 已接近 1000 行，业务逻辑与会话生命周期、网络发送、消息分发混在一起，后续新增命令会继续加剧膨胀。

本次拆分的核心目标：

1. `client_session` 瘦身为「会话上下文 + 生命周期 + 发送端口 + 分发入口」。
2. 把业务逻辑按 domain 分散到各个 `Message_handler` 实现中，文件放入 `src/logic/handlers/`。
3. 保持对外协议、错误文案、发送字段、数据库行为不变（除一处显式修正，见 §五）。

## 二、目标目录结构（方案 B：handlers 子目录）

```
src/net_core/
  client_session.h/.cpp        # 瘦身后的会话类
  notice_service.*             # 不变
  session_manager.*            # 不变
  group_manager.*              # 不变

src/logic/
  account.*
  group.*
  social_module.*

src/logic/handlers/
  message_handler.h            # 从 net_core 移入：抽象接口 + 4 个具体 handler 声明
  logic_common.h/.cpp          # 跨 handler 共享的辅助函数
  auth_handler.cpp             # 原 Base_handler
  chat_handler.cpp             # 原 Chat_handler
  group_handler.cpp            # 原 Group_handler
  file_handler.cpp             # 原 File_handler
```

删除文件：

- `src/net_core/message_handler.h`（移动到 `src/logic/handlers/`）
- `src/net_core/message_handler.cpp`（拆分到各 handler）
- `client_session.h/.cpp` 中的业务方法声明与实现（最后清理）

## 三、拆分边界

### 3.1 `client_session` 保留

- 传输端口：`set_transport` / `on_disconnected` / `upload_file` / `download_file`
- 会话上下文 getter：`is_online` / `logged_in_uid` / `current_account` / `social_manager` / `repo_hub`
- 登录状态切换：`activate_session`（锁内切账号 + 替换在线表，返回旧会话）
- 生命周期：`logout` / `exit_self` / `kick_offline`
- 跨会话内存列表同步：`add_friend_to_list` / `remove_friend_from_list` / `add_group_to_list` / `remove_group_from_list`
- 消息分发：`on_message` / `init_`
- 发送端口：`package_message` / `package_chat_message` / `package_envelope` / `send_serialized_packet`
- 内存池：`operator new` / `operator delete`

### 3.2 移到 handler 的业务

| Handler | 业务 |
|---|---|
| `Auth_handler`（原 Base_handler） | `register_user` / `login` / `login_by_email` / `verify_token` / `finish_login` / `set_email` / `change_my_name` / `show_chatlist` |
| `Chat_handler` | `private_chat` / `group_chat` / `delete_message` / `chat_history` / `send_friend_request` / `set_friend_remark` / `handle_friend_request` / `remove_friend` / `show_friend_requests` |
| `Group_handler` | `create_group` / `delete_group` / `group_add_client` / `group_delete_client` / `modify_group_name` / `send_join_group` / `handle_join_request` / `modify_member_role` / `show_group_requests` / `show_group_members` |
| `File_handler` | `upload_file` / `download_file`（直接透传 session） |

### 3.3 `logic_common` 共享辅助

```cpp
namespace logic {
    std::time_t parse_datetime(const std::string& s);
    std::string generate_token();
    bool target_uid_exists(client_session& s, int uid);
    bool target_uid_online(client_session& s, int uid);
    void show_friend_requests(client_session& s);
    void send_offline_messages(client_session& s, const std::string& since_time);
    bool load_reply_summary(client_session& s, int reply_id, std::string& name, std::string& content);
}
```

## 四、CMake 改动

```cmake
# 新增 handlers 子目录源文件
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/logic/handlers HANDLER_SRC)
```

1. `add_executable(server ...)` 增加 `${HANDLER_SRC}`。
2. `target_include_directories(server ...)` 增加 `${CMAKE_CURRENT_SOURCE_DIR}/src/logic/handlers`。
3. 单元测试 `SERVER_TEST_INCLUDES` 与 `add_executable(test_session_manager ...)` 同样增加对应路径与 `${HANDLER_SRC}`。

## 五、行为变化说明（唯一显式修正）

原 `register_user` 在注册成功后执行了 `current_account_ = new_account`，但没有建立 `social_manager_` / `logged_in_uid_`，会导致注册后未登录时 `set_email` 误判为已登录。

拆分后 `register_user` **不再设置 `current_account_`**，注册成功后客户端仍需正常登录。这是对历史疑点的显式修正。

另：`group_add_client` / `group_delete_client` / `delete_group` / `modify_group_name` 原实现未校验 `current_account_`，未登录时会空指针崩溃；拆分后统一补上登录校验并返回系统提示（不再崩溃）。

## 六、实施顺序（增改 → 测试 → 清理）

1. 写入本方案文档。
2. 移动 `message_handler.h` 到 `src/logic/handlers/`，更新 CMake。
3. 给 `client_session` **增量增加**上下文 getter 与 `activate_session`，暂不删除旧业务代码。
4. 新增 `logic_common.*` 与 4 个 handler `.cpp`，删除旧 `src/net_core/message_handler.cpp`。
5. 编译 + 测试（单元测试 + WS 冒烟）。
6. 最后清理 `client_session.h/.cpp` 中已不再被调用的业务方法与静态辅助函数，保留瘦身后的会话类。
7. 再次编译 + 测试。

## 七、测试

```bash
cd build
cmake ..
cmake --build . -j8

# 可选单元测试（不依赖 MySQL）
cmake -S . -B build -DSERVER_BUILD_TESTS=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure

# 需要 MySQL 且服务端以 websocket 启动时
python3 tests/ws_chat_smoke.py 127.0.0.1 8080 /ws
python3 tests/test_group_broadcast.py 127.0.0.1 8080 /ws
```

## 八、风险与注意点

- `message_handler.h` 对 `client_session` 保持前置声明，避免 include 环。
- `lifecycle_mtx_` 仍由 `client_session::activate_session` 独占管理，handler 不得直接访问。
- `old_session->kick_offline()` 必须在 `activate_session` 返回后、锁外调用。
- 迁移是结构重构，除 §五 外不改变业务行为。
