# Redis 缓存接入方案（缓存类，不含架构演进）

> 范围：**仅做缓存**，把 MySQL 热路径卸载到 Redis。不涉及在线状态路由、跨节点、离线队列、分布式 ID 等多节点架构演进。
>
> 原则：**MySQL 仍是唯一数据真源**，Redis 只是加速层，可随时整体失效/降级回 DB。

---

## 0. 实现状态

| 缓存点 | 状态 | key | 说明 |
|---|---|---|---|
| C1 群成员列表 | ✅ 已实现 | `chat:g:members:{gid}` SET | `group_repo` 读回填 / 写 DEL |
| C2 账号信息 | ✅ 已实现 | `chat:a:{uid}` HASH + 1h TTL | `account_repo` 读回填 / 写 DEL |
| C3 token→uid | ⬜ 未实现 | `chat:t:{token}` STRING + 7d | 待做（见 §3 C3） |
| C4 邮箱→uid | ✅ 已实现 | `chat:e:{email}` STRING + 24h TTL | `account_email_repo` 读回填 / 换绑 DEL 旧 |
| C5 历史索引 | ⬜ 未实现 | `chat:h:*` ZSET | 待做（较重，双写） |
| C6 好友列表 | ⬜ 未实现 | `chat:f:{uid}` SET | 待做 |
| C7 社区成员列表 | ✅ 已实现 | `chat:c:members:{cid}` SET | `community_repo` 读回填 / 写 DEL |
| C8 频道信息 | ✅ 已实现 | `chat:c:channel:{chid}` HASH + 24h | `community_repo` 读回填 / 写 DEL |
| C9 社区频道列表 | ✅ 已实现 | `chat:c:channels:{cid}` ZSET + 24h | `community_repo` 读回填 / 写 DEL |
| C10 用户社区/频道列表 | ✅ 已实现 | `chat:c:ucommunities:{uid}` / `chat:c:uchannels:{uid}` SET + 24h | `community_repo` 读回填 / 写 DEL |
| C11 社区信息 | ✅ 已实现 | `chat:c:info:{cid}` HASH + 24h | `community_repo` 读回填 / 写 DEL |

---

## 1. 背景与目标

### 1.1 现状

- 单进程、MySQL 唯一数据源；业务层 `client_session` / `social_module` 通过 `RepositoryHub` 门面访问 `I_*_repo` 接口。
- 内存已有两层缓存：`session_manager`（在线表）、`group_manager`（群对象 + 引用计数）。
- 所有 `*_repo` 实现都直接走 `MySQL_Conn_Pool`，每个方法一个 SQL，无缓存。

### 1.2 要解决的 DB 热路径

| 热路径 | 触发场景 | 频率 | DB 压力 |
|---|---|---|---|
| 群成员查询 | 每条群消息广播前 | **极高** | 每条群消息 1 次 `SELECT ... FROM Groupmember` |
| 账号查询 `load_account` | 登录、私聊校验、好友申请、离线消息、show_friends | 高 | 存在 N+1（show_friends 每个好友一次） |
| token 查询 | 自动登录 `verify_token` | 中 | 每次 1 次 `WHERE token=?` |
| email 查询 | 注册预检查、邮箱登录、加好友 | 中 | 每次 1 次 `WHERE email=?` |
| 历史分页 | 拉聊天记录 | 中高 | 4 JOIN + 子查询 |
| 好友列表 | 登录加载 | 中 | 每次 1 次 `WHERE UID=?` |

### 1.3 目标

1. 把「每条群消息查成员」这条最高频 DB 读消掉。
2. 把 `load_account` 的 N+1 和重复读消掉。
3. 所有缓存点**可开关、可降级**，Redis 挂了业务不受影响。
4. 业务层（`client_session` / `social_module`）**零改动**。

---

## 2. 总体设计

### 2.1 缓存策略：Cache-Aside（旁路缓存）

- **读**：先查 Redis，命中返回；未命中查 MySQL，回填 Redis 后返回。
- **写**：先写 MySQL 成功，再**失效或刷新** Redis。**绝不**先写 Redis 再写 MySQL。
- **降级**：Redis 任何异常/超时一律吞掉打日志，回退 MySQL 路径（现有代码已普遍有 `nullptr/空` 兜底）。

### 2.2 分层与装配

实际实现采用**「repo 内部直连 RedisCache」**（比装饰器改动更小，业务层零改动）：

```
业务层（client_session / social_module）
        ↓ RepositoryHub::accounts()/groups()/emails()（接口不变）
repo 实现（account_repo / group_repo / account_email_repo / ...）
        ↓ 内部先查 RedisCache，未命中查 MySQL 后回填；写路径调用失效函数
RedisCache（src/utils/redis_cache.*：key 规范 + 命令封装 + 失效函数）
        ↓
RedisPool（src/utils/redis_client.*：持有 sw::redis::Redis，自带连接池）
```

- `RedisCache` 单例集中封装「key 规范 + 命令 + 失效函数」，跨表失效由各 repo 调用其失效函数完成。
- 开关放 `configure.json` 的 `redis.enabled`；`false` 时 `RedisPool` 不建连接，所有缓存方法退化为 no-op/未命中，业务自动回退 MySQL（等价原现状，无需换装配）。

### 2.3 Key 规范

统一前缀 `chat:`，类型约定见各缓存点。所有 uid/gid 用十进制字符串。

| Key | 类型 | 内容 | TTL |
|---|---|---|---|
| `chat:g:members:{gid}` | SET | 群成员 uid | 无 TTL（靠主动失效）+ 可选 24h 兜底 |
| `chat:a:{uid}` | HASH | 账号字段 | 1h 兜底 + 主动失效 |
| `chat:t:{token}` | STRING | uid | 7d（滑动刷新） |
| `chat:e:{email}` | STRING | uid | 24h 兜底 + 主动失效 |
| `chat:h:1:{gid}` | ZSET | 群聊会话消息 id 索引 | 7d（对齐消息保留期） |
| `chat:h:0:{min}:{max}` | ZSET | 私聊会话消息 id 索引 | 7d |
| `chat:m:{id}` | HASH | 单条消息（可选） | 7d |
| `chat:f:{uid}` | SET | 好友 uid | 无 TTL + 主动失效（可选 24h） |

### 2.4 序列化约定

- **account**：HASH，field 与 `Account` 列一一对应：`nickname / password / settings / language / token`。读取时按字段重建 `account` 对象。
- **message**：HASH，field：`message_id / sender_UID / receiver_UID / content / is_group / timestamp / reply_to_message_id`。
- **uid/gid/email/token**：STRING 直接存。

### 2.5 基础设施：RedisPool + ServerConfig

- `src/utils/redis_client.{h,cpp}`：`RedisPool` 单例，持有 `sw::redis::Redis`
  （`ConnectionOptions` + `ConnectionPoolOptions`），池满 `wait_timeout=50ms` 快速失败
  （缓存层捕获后降级，不阻塞业务线程）。
- C++17 客户端：`redis-plus-plus`（基于 hiredis），经 CMake `FetchContent` 拉取
  `hiredis v1.4.1` + `redis-plus-plus 1.3.15` 静态链接；`server` 与 `test_session_manager` 均链接
  `redis++::redis++_static`。
- `configure.json` 增加 `redis` 段：

```json
"redis": {
  "enabled": true,
  "host": "127.0.0.1",
  "port": 6379,
  "password": "",
  "db": 0,
  "pool_size": 4,
  "timeout_ms": 200
}
```

- `ServerConfig` 增加对应读取 + 数值下限校验（对齐现有 `db_conn_count` 的写法）。

---

## 3. 缓存点详述

> 每项含：现状代码位置 / Redis 结构 / 读写流程 / 失效点 / 降级。

### C1 群成员列表（最高优先级 ⭐）✅ 已实现

**现状**
- `client_session::group_chat()` → `repo_hub_->groups()->get_group_members(group_uid)` → `group_repo::get_group_members()`：
  `SELECT member_UID FROM Groupmember WHERE group_UID = ?`
- 每条群消息一次，群聊广播放大时 DB 压力最大。

**Redis**
- `chat:g:members:{gid}` = SET of uid
- 读：`SMEMBERS`；未命中 → 查 MySQL → `SADD` 回填。

**失效点（写路径全部要 DEL）**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 建群 | `group_repo::create_group` | `SADD` 群主 uid（或直接 DEL 让下次回源） |
| 删群 | `group_repo::delete_group` | `DEL` |
| 拉人入群 | `group_repo::member_add_group` | `DEL` |
| 踢人/退群 | `group_repo::remove_group_member` | `DEL` |
| 同意入群申请 | `group_repo::handle_join_request`（accept） | `DEL` |

> 选择「写时 DEL」而非「增量 SADD/SREM」，实现最简单、不会漏；下次读回源重建。群成员变更低频，代价可接受。

**降级**：Redis 失败 → 直接查 MySQL，行为与现状一致。

---

### C2 账号信息缓存（高优先级 ⭐）✅ 已实现

**现状**
- `account_repo::load_account(uid)`：`SELECT UID,password,nickname,settings,language,token FROM Account WHERE UID = ?`
- 调用点（都是热/重复读）：
  - `client_session::login` / `finish_login`
  - `client_session::verify_token`（间接经 C3）
  - `client_session::private_chat` → `target_UID_is_exit`
  - `social_module::send_friend_request`（查接收方 + 发送方）
  - `social_module::show_friends`（**每个好友一次**，N+1）
  - `social_module::show_friend_requests`（每个申请者一次）
  - `client_session::send_offline_messages` / `load_reply_summary`

**Redis**
- `chat:a:{uid}` = HASH：`nickname/password/settings/language/token`
- 读：`HGETALL`；未命中 → `load_account` → `HSET` 回填 + `EXPIRE 3600`。

**失效点**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 改名/改设置/改密码 | `account_repo::update_account` | `DEL`（或直接 `HSET` 刷新） |
| 刷新/清空 token | `account_repo::update_token` | `HSET token` 或 `DEL` |
| 注销 | `account_repo::remove_account` | `DEL` |

> 兜底 TTL 1h，防止漏失效。密码字段沿用现有设计（DB 明文，Redis 同样明文，不额外加解密）。

**降级**：Redis 失败 → 查 MySQL（现有 `nullptr` 兜底不变）。

---

### C3 token → uid（自动登录）

**现状**
- `account_repo::load_account_by_token(token)`：`SELECT ... WHERE token = ?`
- 调用点：`client_session::verify_token`。

**Redis**
- `chat:t:{token}` = STRING uid
- 登录成功 `update_token` 时：`SET chat:t:{token} uid EX 7d`（滑动过期，每次登录刷新）。
- 验证时：`GET chat:t:{token}` 拿到 uid → 再走 `load_account(uid)`（命中 C2），跳过 `WHERE token=?` 这条 SQL。

**失效点**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 登录刷新 token | `account_repo::update_token` | 先 `DEL` 旧 token（若已知），`SET` 新 token |
| 过期清空 | `account_repo::update_token(uid, "")` | `DEL` 对应 token |

> 需要能拿到「旧 token」才能 DEL：`update_token` 目前只写新值。改造时建议 `update_token` 签名增加旧 token 参数，或在 `load_account` 后由业务层先 DEL 旧 token。**实现时要处理旧 token 泄漏问题**（旧 token 在 TTL 7 天内仍能命中，但 uid 指向同一账号，且登录会刷新 token 使 DB 旧 token 失效——若只信缓存会导致「被顶号后的旧 token 仍可登录」）。

**安全注意**：token 校验必须**以 DB 为准**。C3 只能当「加速命中 uid」用，命中后仍需 `load_account(uid)` 核对 token 一致 + 过期判断；或者 `update_token` 刷新时同步 `DEL` 旧 token，避免双 token 窗口。**推荐**：`verify_token` 逻辑保持「查 DB 校验」，C3 仅用于先拿到 uid 减少一条按 token 查的 SQL，最终 token 是否有效仍比对 DB 里的 token。

---

### C4 email → uid ✅ 已实现

**现状**
- `account_email_repo::find_uid_by_email(email)`：`SELECT UID FROM account_email WHERE email = ?`
- 调用点：注册预检查、`login_by_email`、`send_friend_request`。

**Redis**
- `chat:e:{email}` = STRING uid + 24h 兜底 TTL
- 绑定/换绑 `set_email` 成功 → `SET` 新映射 + `DEL` 旧映射；注销暂靠 24h TTL 自愈（见难点）。

**失效点**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 绑定/换绑 | `account_email_repo::set_email` | 先 SELECT 旧邮箱，成功后 `SET` 新 + `DEL` 旧 |
| 注销 | `account_repo::remove_account` | 暂不处理，靠 24h TTL 自愈（见下方难点） |

**难点（实际采用方案 2）**：`remove_account` 只 `DELETE FROM Account`，邮箱由外键 CASCADE 删除，repo 层拿不到 email。
- `remove_account` 当前仅在注册回滚路径调用（此时 `set_email` 已失败，邮箱缓存本就不存在），无实际脏数据窗口。
- 未来若加「注销账号」业务，再在 `remove_account` 补 `email_invalidate`（先查 email 再 DEL）。

**负结果**：邮箱不存在（-1）不缓存，避免污染；仅缓存正映射。

---

### C5 聊天历史索引（收益高，最需谨慎）

**现状**
- `client_session::chat_history()` → `message_repo::get_history_page()`：
  4 个 LEFT JOIN（Account、message reply、Account reply）+ 私聊双向 OR + 群聊 IN 子查询，`ORDER BY id DESC LIMIT ?`。

**设计（推荐：只存 id 索引，内容仍以 DB/消息缓存为准）**

- `chat:h:1:{gid}` = ZSET，member=message_id 字符串，score=message_id（群聊会话）。
- `chat:h:0:{min}:{max}` = ZSET（私聊会话，`min/max` 是双方 uid 排序后归一化，消除方向）。
- **写**：`store_message` 成功拿到 `message_id` 后 `ZADD`。
- **读历史**：
  1. `ZRANGEBYSCORE` 拿一页 message_id（`before_id` 游标即 score 上界，天然支持）。
  2. 逐个 `get_message(id)`（走 `chat:m:{id}` 缓存或 MySQL）+ `load_account(sender)` 补昵称 + 补 reply 摘要。
- **TTL**：`EXPIRE 7d`，与 `delete_expired_messages`（7 天）对齐。

**失效点**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 发消息 | `message_repo::store_message` | `ZADD` |
| 删消息 | `message_repo::delete_message` | `ZREM` |
| 定时清理 | `message_repo::delete_expired_messages` | 依赖 TTL 自愈（不必逐条 ZREM） |

**为什么只存 id 索引**：
- 历史查询需要 `sender_name` 与 `reply_sender_name / reply_content`，这些是 JOIN 出来的，跨表易不一致。
- 只存 id 索引，内容复用 `get_message` + C2 账号缓存拼装，一致性风险最低。
- 代价是历史一页（10 条）会触发多次 `get_message`，但这些走缓存（见 C5 可选增强）或 DB 单行主键查询，很快。

**可选增强 C5+（`chat:m:{id}` 消息缓存）**
- `chat:m:{id}` = HASH（message 字段），`get_message` 命中即回，历史拼装全走缓存。
- 写：`store_message` 成功后 `HSET`；删：`DEL`。
- TTL 7d。此增强把「历史 + 单条消息查询」也全部脱库，收益更大，但多一层缓存，建议 C5 跑稳后再加。

**注意**：私聊会话 key 归一化（`min:max`）必须在写和读两侧用同一规则，否则读不到。

---

### C6 好友列表缓存（中优先级）

**现状**
- `friend_repo::get_friend_list(uid)`：`SELECT friend_UID FROM friend_relation WHERE UID = ?`
- 调用点：登录时 `social_module` 构造加载。

**Redis**
- `chat:f:{uid}` = SET of friend_uid
- 读：`SMEMBERS`；未命中 → MySQL → `SADD` 回填。

**失效点**
| 写操作 | 函数 | 动作 |
|---|---|---|
| 建好友（双向） | `friend_repo::add_friend` | `SADD` 双方（或 `DEL` 双方） |
| 删好友（双向） | `friend_repo::remove_friend` | `SREM` 双方（或 `DEL` 双方） |
| 自好友 | `friend_repo::ensure_self_friend` | `SADD self` |

> 推荐写时 `DEL` 双方集合，下次回源，避免 SADD/SREM 增量遗漏。

**可选关联优化**：`is_friend(a,b)` 可从 `chat:f:{a}` 用 `SISMEMBER b` 判断，省一次 COUNT SQL。但要注意双向一致性，建议先不做，列为后续优化。

### C7-C11 社区缓存（已实现 ✅）

社区功能（见《社区后端改造方案.md》§5.8）同样采用 Cache-Aside，全部接在 `community_repo` 内：

- **C7 社区成员** `chat:c:members:{cid}` SET：频道发言广播前取成员（频道成员=社区成员，一份共享）。
- **C8 频道信息** `chat:c:channel:{chid}` HASH：`get_channel_community` 解析频道→社区。
- **C9 社区频道列表** `chat:c:channels:{cid}` ZSET：进社区拉频道列表；命中后逐个取 C8。
- **C10 用户社区/频道列表** `chat:c:ucommunities:{uid}` / `chat:c:uchannels:{uid}` SET：登录加载。
- **C11 社区信息** `chat:c:info:{cid}` HASH：低频读（join_community 通知 owner 等）。

**失效点**（写路径全部主动失效，见 `community_repo.cpp`）：

| 写操作 | 失效 |
|---|---|
| 建社区 | C10(owner) |
| 删社区 | C7/C9/C11 + C8(各频道) + C10(各成员) |
| 改社区 | C11 |
| 建/删/改频道 | C8(channel) + C9 |
| 加/踢/退成员、同意入社区 | C7 + C10(目标用户) |

> 频道成员与社区成员是同一份数据，所以广播只需要 C7（按 community_id），无需为每个频道各存一份成员列表。

---

## 4. 失效矩阵汇总

| 写操作（repo 函数） | 影响缓存 | 动作 |
|---|---|---|
| `account_repo::update_account` | a:{uid} | DEL |
| `account_repo::update_token` | a:{uid}.token, t:{old}, t:{new} | HSET / DEL 旧 / SET 新 |
| `account_repo::remove_account` | a:{uid} | DEL（email 靠 24h TTL 自愈，见 C4） |
| `account_email_repo::set_email` | e:{old}, e:{new} | DEL 旧 / SET 新 |
| `friend_repo::add_friend` | f:{a}, f:{b} | DEL 双方 |
| `friend_repo::remove_friend` | f:{a}, f:{b} | DEL 双方 |
| `friend_repo::ensure_self_friend` | f:{uid} | DEL |
| `group_repo::create_group` | g:members:{gid} | DEL（下次回源） |
| `group_repo::delete_group` | g:members:{gid}, h:1:{gid} | DEL |
| `group_repo::member_add_group` | g:members:{gid} | DEL |
| `group_repo::remove_group_member` | g:members:{gid} | DEL |
| `group_repo::handle_join_request(accept)` | g:members:{gid} | DEL |
| `message_repo::store_message` | h:{...}, m:{id} | ZADD / HSET |
| `message_repo::delete_message` | h:{...}, m:{id} | ZREM / DEL |

> 兜底 TTL 是第二道保险：主动失效漏了，TTL 到期也能自愈。因此**所有无 TTL 的 key 建议加一个 24h 兜底 TTL**，写路径统一 `DEL` 即可。

---

## 5. 降级与容灾

1. **Redis 完全不可用**：`CacheClient` 捕获所有异常，统一返回「未命中/操作失败」，上层回退 MySQL，业务语义与现状完全一致。
2. **Redis 抖动**：单次命令超时（`timeout_ms=200`）即降级该次请求，不阻塞业务线程（业务线程池只有 8 个，绝不能因 Redis 阻塞）。
3. **DB 写成功、Redis 失效失败**：打印 error 日志，靠兜底 TTL 自愈。TTL 设短（如 1h/24h）避免脏数据窗口过长。
4. **缓存穿透**：`load_account` / `find_uid_by_email` 对不存在的值不缓存负结果，或只对「确实存在」的数据缓存，避免打穿。
5. **缓存雪崩**：兜底 TTL 加随机抖动（如 `EXPIRE 3600 + rand(0,300)`），避免大量 key 同时过期。

---

## 6. 实施计划

| 阶段 | 内容 | 交付 |
|---|---|---|
| **P0** | ✅ 已完成：RedisPool + ServerConfig redis 段 + RedisCache 骨架 + C1 群成员缓存 | 群聊场景 DB 读下降，冒烟通过 |
| **P1** | 🔶 部分完成：C2 账号缓存 + C4 email 已完成；C3 token 待做 | 登录/私聊/加好友 DB 读下降 |
| **P2** | ⬜ C5 历史索引（+ 可选 C5+ 消息缓存）+ C6 好友列表 | 历史/登录读下降 |
| **P3** | ⬜ 压测对比（开/关缓存）+ 失效矩阵回归 + 文档基线 | 压测报告 |

每个阶段都用 `redis.enabled=false` 回归一次，保证关掉缓存后行为与现状一致。

---

## 7. 测试与验证

**已执行（C1/C2/C4）**：
- ✅ `tests/ws_chat_smoke.py` 在 `redis.enabled=true/false` 下全量通过。
- ✅ C1：群聊广播回填成员、拉人 DEL、下次群聊回填新成员（`test_group_broadcast.py` + redis-cli 验证）。
- ✅ C2：加好友触发 `load_account` 回填；`change_name` 后 key 被 DEL；`show` 再回填新昵称。
- ✅ C4：注册回填、换绑 DEL 旧/SET 新、负结果不缓存、新邮箱登录。
- ✅ 降级：`redis.enabled=false` 时 `[RedisPool] disabled`，冒烟全通过。

**待做（P3）**：
- 性能对比：用 `Docs/压力测试方案.md` 的群聊/私聊场景，对比开/关缓存的 DB 连接占用与 P95。
- 失效矩阵回归：C5/C6 落地后补齐。
- 单元测试：`RedisCache` 各 key 操作（命中/未命中/写后失效）。

---

## 8. 风险与注意点

1. **脏数据**：失效点漏一个就脏。对策：失效矩阵逐项 review + 兜底 TTL + 一致性测试。
2. **remove_account 的 email 失效**：CASCADE 删除拿不到 email，需删除前先查（见 C4 难点）。
3. **旧 token 泄漏**：`update_token` 刷新时旧 token 未 DEL，可能造成「被顶号后旧 token 短时仍可登录」。C3 只做加速，最终校验仍以 DB token 为准。
4. **私聊会话 key 归一化**：`min:max` 规则读写两侧必须一致。
5. **历史消息双写**：先 MySQL 成功再 ZADD，Redis 失败只影响缓存命中率，不影响正确性。
6. **密码/隐私**：account 缓存含明文密码（与现有 DB 一致），Redis 需内网部署、不暴露公网。
7. **内存容量**：历史 ZSET（7 天消息）与账号缓存会占 Redis 内存，需预估容量并设 `maxmemory` 策略（建议 `allkeys-lru`，Redis 满了丢缓存回源 DB，不影响正确性）。
8. **线程模型**：`RedisPool` 的调用发生在 8 个业务线程，连接池要够用（`pool_size` 建议 ≥ 业务线程数，但连接数可控）；所有 Redis 调用设超时，禁止阻塞等待。

---

## 9. 交付物

1. `src/utils/redis_client.{h,cpp}`（RedisPool：redis-plus-plus 封装 + 连接池）
2. `src/utils/redis_cache.{h,cpp}`（RedisCache：key 规范 + 命令封装 + 失效函数，已含 C1/C2/C4）
3. `src/db/repo/group_repo.cpp`（C1 接入）、`account_repo.cpp`（C2 接入）、`account_email_repo.cpp`（C4 接入）
4. `configure.json` + `ServerConfig` 增加 `redis` 段
5. CMakeLists：FetchContent(hiredis + redis-plus-plus) + 链接 `redis++::redis++_static`
6. 一致性验证（C1/C2/C4）+ `enabled=false` 降级回归；压测对比报告待 P3

---

# 附录：并发与稳定性瓶颈分析

> 本文档在「缓存方案」之外，补充对当前服务器的**并发**与**稳定性**瓶颈梳理，作为缓存落地与压测的对照清单。每条标注代码位置与影响面，最后给出优化优先级。
>
> 已修复（2026-09-23，见开发日志）：B1 无界队列、B2 异常 terminate、A4 群聊广播放大、B6 连接数/fd 上限。

## A. 并发瓶颈

### A1. 业务线程池：硬编码 8 线程 + 单队列单锁 + 无界

**位置**：`ws_server.cpp` 的 `run_websocket_server()` 中 `std::make_shared<ThreadPool>(8)`；binary 模式 `epoller.cpp` 每个 `sub_reactor` 一个 `ThreadPool(8)`（默认 3 个 sub_reactor = 24 线程）。

**问题**：
- 线程数**硬编码**，无法按 CPU/负载调优；WS 模式全服只有一个 8 线程池。
- `ThreadPool::submit_task` 与 `ThreadPool::run` 共用**一把锁 + 一个条件变量 + 一条队列**（`th_pool_mtx_`/`cv_`/`tasks`）。高并发下所有连接的消息入队/出队都争同一把锁。

### A2. MySQL 连接池：默认 4 + 阻塞等待 + 每消息写库

**位置**：`mysql_conn_pool.cpp` 的 `get_connection()`；`db_conn_count` 默认 4。

**问题**：
- 达上限后 `cv_.wait()` **阻塞业务线程**。8 个业务线程里一旦 4 个卡在等连接，只剩 4 个能处理消息。
- 每条聊天消息（私聊/群聊）都 `store_message` 写库，DB 写是最先触顶的吞吐瓶颈。
- `ConnGuard` 只在方法作用域持有连接，高频短查询反复取还，连接池锁 `mtx_` 也是争用点。

> 这是 Redis 缓存能**直接缓解**的点：C1/C2/C5 把「每消息查成员、查账号、查历史」从 DB 摘走，DB 只承担真正的写。

### A3. session_manager：全局单 map + shared_mutex

**位置**：`session_manager.cpp`。

**问题**：
- 所有在线 UID 放一个 `unordered_map`，无分片。
- `find_session` 每次 `shared_lock`；**群聊广播 N 个成员 = N 次 find_session = N 次锁获取/释放**。
- `replace_online` / `remove_online_if_same` 用 `unique_lock`，登录/顶号/断线会短暂阻塞所有 `find_session`。

### A4. 群聊广播 CPU 放大：同一消息 N 次序列化 + N 次查表 ✅ 已修复

**位置**：`client_session::group_chat()` → `NoticeService::send_to_users_with_id()` → 循环 `package_chat_message()`。

**问题**：
- 每条群消息对每个成员：构造一次 `Envelope` + `SerializeToString` + `find_session` + `send_packet`。
- 内容对所有成员**完全相同**（同 content/message_id/group_uid/sender），却被序列化 N 次。100 人群 = 100 次序列化 + 100 次锁，全在发送者所在的那一个业务线程上同步执行。
- 优化方向：序列化一次复用 payload；`find_session` 批量或按成员分片。

### A5. ClassMemoryPool：全局单锁 + 归属线性扫描

**位置**：`utils/memory_pool.h`。

**问题**：
- `allocate`/`deallocate` 都 `lock_guard<std::mutex>` 一把全局锁。
- `deallocate` 的 `belongs_to_pool` 遍历 `chunks_` 判断归属（O(chunk 数)）。
- 高连接 churn（大量建连/断连）时这把锁是全局争用点。

### A6. 每连接锁与 strand

- **binary**：`connection::process_incoming` 持 `recv_mtx_`；`send_packet` 持 `lifecycle_mtx_` 追加发送缓冲；`sender::add_to_out_buffer/send_msg` 再锁 `out_mtx`。同一连接内多把锁顺序使用，开销可控，但要注意锁顺序（见 A8）。
- **WS**：每连接独立 strand，IO 串行，设计合理；业务完成后 `net::post` 回 strand 恢复读，形成单连接读背压。

### A7. 心跳/空闲检测 O(N) 扫描

- **binary**：`sub_reactor::check_idle_connections` 每秒持 `client_mutex` 遍历全部连接。
- **WS**：`use_heartbeat=true` 时，每个连接一个 `steady_timer` 每秒触发一次。N 连接 = N 个定时器每秒唤醒。

### A8. 锁顺序（需保持的约定）

**现状**：`finish_login` / `on_disconnected` 都是「先 `lifecycle_mtx_` 再 `session_manager` 的 `sessions_mutex`」顺序。目前没有发现反向加锁，但这是隐性约定，**新增代码必须保持同一顺序**，否则引入死锁。

---

## B. 稳定性瓶颈

### B3. 发送缓冲上限不一致（中）

- **WS**：`max_pending_bytes` 8 MiB 超限 `teardown`，有保护。
- **binary**：`sender::out_buffer` 只 `+=` **无任何上限**，慢消费者可把内存撑爆。

**建议**：给 binary `sender` 同样加 `max_pending_bytes` 阈值，超限断开。

### B4. 无消息频率/限流（中）

单连接可高速刷 `private_chat`/`group_chat`，每条都写 DB + 广播，故障/恶意客户端可拖垮 DB。当前无 per-connection / per-user 限流。

### B5. DB 调用无超时（中高）

JDBC `PreparedStatement::execute*` 无 statement 超时。DB 慢/锁等待时，业务线程被长时间占用；8 线程全卡 = 全服无响应。

### B6. 连接数 / fd 无上限（中）✅ 已修复

accept 无限，无 max connections。fd 耗尽后 accept 仅打日志；异常连接可能耗光资源。

### B7. 日志/输出阻塞（低）

大量 `std::cout/std::cerr`（如 `upload_file` 每次打印 `DebugString`、心跳日志、异常日志）。高并发下 stdout 阻塞且无缓冲控制，拖慢业务线程。

### B8. 优雅退出不完整（低）

- WS：`signal_set` 停 ioc + `pool->stop_pool()`，基本可用；但 `stop_pool` 先置 `is_exit` 后 notify，随后 `submit_task` 会 throw，正在执行的任务不受影响。
- binary：`running` 是普通 `bool`（非原子），无信号处理，退出靠外力。
- `MySQL_Conn_Pool::shutdown` 只删 idle 连接，借出中的连接未回收；且 main 未调用 shutdown。

### B9. 内存池超限退化（低）

`ClassMemoryPool` 达 8192 上限后 `allocate` 返回 nullptr → `operator new` 回退全局 new，功能正确但池化失效；属于设计行为，压测时观测即可（见压测方案 S3）。这是计划的一部分🤫

---

## C. 优先级建议

| 优先级 | 问题 | 动作 | 与缓存的关系 |
|---|---|---|---|
| ✅ 已修 | B1/B2 无界队列/异常 terminate | 队列上限 + run() try/catch | — |
| ✅ 已修 | A4 群聊广播放大 | 序列化一次 + find_sessions 批量 | 广播路径优化 |
| ✅ 已修 | B6 连接数/fd 上限 | 各网络层加连接数上限 | — |
| **P1** | A2 MySQL 连接池/DB 写 | 接入 Redis 缓存（C1/C2/C5）卸载读 | 本方案核心 |
| **P1** | B3 binary 发送缓冲无上限 | 加 max_pending_bytes | — |
| **P1** | B5 DB 无超时 | statement 超时 + 连接池等待超时 | 与缓存降级配合 |
| **P2** | A1 线程池硬编码 | 线程数可配置（进 configure.json） | 压测调参依据 |
| **P2** | A3 session_manager 锁 | 分片 map / 批量查询 | — |
| **P3** | B4 限流 / B7 日志 | 按需加固 | — |

> 结论：截止 2026-09-23，B1/B2/A4/B6 已修复；剩余 P1（A2/B3/B5）与 P2/P3 待按表推进。
> 缓存（C1/C2/C4 已落地，C5 待做）是缓解 A2 及 DB 压力的主力手段，两者互补。