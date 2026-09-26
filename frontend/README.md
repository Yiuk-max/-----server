# 前端（Vite + Vue 3）

Baka Community 的 Web 前端。构建产物输出到仓库根目录的 `web/`，
由 WebSocket 服务端（`net_layer=websocket`）自带的静态文件服务提供。

前后端业务消息统一使用 `src/proto/message.proto` 定义的 `chat_proto.Envelope`。前端通过 `protobufjs`
直接读取该共享 schema，WebSocket 使用 binary 帧：`4B payload_len（大端） + protobuf Envelope`。

## 开发

```bash
cd frontend
npm install
npm run dev          # http://localhost:5173，/ws 已代理到后端 127.0.0.1:8080
```

## 构建（部署到后端）

```bash
cd frontend
npm run build        # 产出到 ../web（index.html + assets/*.js|css）
```

然后启动后端：

```bash
cd ../build && ./server      # configure.json: net_layer=websocket, web_root=../web
```

浏览器打开 `http://<host>:8080/`。

## 为什么这样配置

后端 Boost.Beast 静态服务有两条限制，`vite.config.js` 已对应处理：

1. **没有 SPA fallback**：非文件路径一律 404，所以用 **hash 路由**（`#/friends`、`#/settings`），
   页面永远只在 `/` 加载。
2. **后缀白名单**：仅 `html/htm/css/js/json/svg/png/jpg/jpeg/gif/ico/woff/woff2/map`。
   不要引入 `wasm / webp / avif / ttf / otf / mp4`，否则 404。字体请用 woff2 或系统字体。

## 协议实现

- `src/proto/message.proto` 是前后端共享的唯一 schema，前端不维护字段副本。
- `src/protobufProtocol.js` 使用 `protobufjs` 的 `keepCase` 解析选项，保留现有 `UID/group_UID/sender_UID` 字段名。
- 发送：普通对象 → `Envelope` → protobuf bytes → 4 字节大端长度头 → WebSocket binary 帧。
- 接收：`ArrayBuffer/Blob` → 校验 4 字节长度头 → `Envelope` → 普通对象；嵌套 `user/reply_to/messages` 会完整恢复。
- `chatStore.js` 串行处理异步解码结果，保证服务器推送顺序不因 Blob/ArrayBuffer 转换而变化。

## 已接入功能

- 连接 / 心跳 / 自动重登；注册、登录（UID 或邮箱）、登出、改昵称、设置邮箱
- 会话列表（好友 / 群组分组、搜索、未读时间）、私聊、群聊、离线消息
- 历史游标分页（上滑加载更早）、回复某条消息、删除自己的消息（右键菜单）
- 好友闭环：按邮箱加好友、好友申请列表（同意/拒绝）、通讯录、删除好友（`#/friends`）
- 群聊：创建群、按 UID 申请入群、群管理（改群名/拉人/踢人/解散/成员/入群申请处理）
- 个人主页：点击左下角头像或消息头像查看（自己可改昵称/换邮箱/登出；好友可删除）
- 多主题：左侧栏底部上衣图标切换，只改配色（两色/三色），见 `theme.js`

## 目录

```
frontend/
├── index.html
├── vite.config.js
├── package.json
└── src/
    ├── main.js
    ├── router.js          # hash 路由：/（聊天）、/friends（好友）、/settings（设置）
    ├── style.css          # 全局样式 + 多套主题 CSS 变量（按 data-theme 切换）
    ├── theme.js           # 主题定义（两色/三色）与切换
    ├── App.vue            # 左侧 rail 导航 + 主题切换 + 个人主页弹窗
    ├── chatStore.js       # WebSocket 客户端 + 会话/历史/好友/群聊状态（与 UI 解耦）
    ├── protobufProtocol.js# Envelope 编解码 + WebSocket 4B payload_len 应用帧
    └── views/
        ├── ChatView.vue    # 主聊天界面：会话列表 + 消息 + 输入 + 群管理
        ├── FriendsView.vue # 好友页三栏：通讯录 / 添加好友·群聊 / 申请列表
        └── SettingsView.vue# 连接配置 / 账号 / 系统日志
```
