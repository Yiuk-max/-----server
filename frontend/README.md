# Baka Community 前端

界面以 `/home/ubuntu/project/discord` 为视觉和交互基线，保留原有 Discord 风格 DOM、CSS、菜单、弹窗、设置页和功能工作台；业务数据通过本仓库 C++ WebSocket 服务端提供，不使用模拟联系人或模拟聊天记录。

## 开发与构建

```bash
cd frontend
npm install
npm run dev       # http://localhost:5173，/ws 代理到 127.0.0.1:8080
npm run build     # 输出到 ../web
```

后端需配置 `net_layer=websocket`，从 `build/` 运行时设置 `websocket.web_root=../web`。

## 接入模块

- `src/protobufProtocol.js`：共享 `src/proto/message.proto` 的 Envelope 编解码，以及 `4B protobuf长度 + protobuf + file_data` 帧。
- `src/backendClient.js`：WebSocket、token、账号、联系人、群聊、社区、频道、消息历史和文件分片传输。
- `src/App.vue`：原版 Discord 界面；只替换数据源和事件处理，不另建简化页面。
- `styles.css`、`reference-theme.css`：直接来自 `/home/ubuntu/project/discord`，不做视觉精简。

## 已接入后端能力

- 注册、UID/邮箱登录、token 自动登录、登出
- 好友、申请、备注、删除好友
- 群聊创建、加入、成员、角色、改名与解散
- 私聊、群聊、社区频道聊天、历史分页、回复和删除消息
- 社区、频道、成员及社区设置
- 文件列表、删除、分片上传/下载、暂停、继续、取消和上传断线续传
- 聊天附件上传完成后发送文件消息，点击文件消息进行分片下载
- 消息图片查看器（lightbox）：点击消息图片打开大图，支持滚轮缩放、拖动平移、左右切换当前会话已加载图片、X / Esc 关闭，兼容 gif / png / jpg
- 输入区 Discord 风格发送按钮（蓝底圆角，图标 + 文字，输入为空时自动隐藏）
- 私信列表与通讯录已移除在线/离线状态显示，仅保留未读红点
- 左侧竖栏已移除「测试」菜单入口

后端没有接口的功能保留原入口，点击后提示“暂未开发”，例如语音/视频、反应、转发、投票、活动、邀请链接和服务器指南编辑。

跨页面刷新恢复上传时，浏览器安全策略要求用户重新选择名称和大小一致的原文件；服务端通过 `transfer_id` 和 `next_chunk_index` 从断点继续。
