# 前端（Vite + Vue 3）

聊天服务器的 Web 前端。**后端与 `configure.json` 零改动**：构建产物输出到仓库根目录的 `web/`，
由 WebSocket 服务端（`net_layer=websocket`）自带的静态文件服务提供。

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

1. **没有 SPA fallback**：非文件路径一律 404，所以用 **hash 路由**（`#/settings`），
   页面永远只在 `/` 加载。
2. **后缀白名单**：仅 `html/htm/css/js/json/svg/png/jpg/jpeg/gif/ico/woff/woff2/map`。
   不要引入 `wasm / webp / avif / ttf / otf / mp4`，否则 404。字体请用 woff2 或系统字体。

## 目录

```
frontend/
├── index.html
├── vite.config.js
├── package.json
└── src/
    ├── main.js
    ├── router.js          # hash 路由
    ├── style.css
    ├── App.vue
    ├── chatStore.js       # WebSocket 客户端 + 会话/历史/离线状态（与 UI 解耦）
    └── views/
        ├── ChatView.vue    # 主界面：连接/账号/会话列表/消息/输入
        └── SettingsView.vue# 系统日志与说明
```
