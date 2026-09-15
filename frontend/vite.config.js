import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { fileURLToPath, URL } from 'node:url';

// 后端（Boost.Beast）静态服务有两条硬约束：
//   1. 没有 SPA fallback：未知路径直接 404 → 前端用 hash 路由（#/xxx）。
//   2. 后缀白名单：html/css/js/json/svg/png/jpg/jpeg/gif/ico/woff/woff2/map
//      → 不引入 wasm/webp/avif/ttf 等资源。
export default defineConfig({
  plugins: [vue()],
  base: './',
  resolve: {
    alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) },
  },
  build: {
    outDir: '../web',      // 直接产出到后端 web_root 指向的目录（configure.json: websocket.web_root=../web）
    emptyOutDir: true,     // 每次构建清空旧产物
    sourcemap: false,
    assetsInlineLimit: 4096,
  },
  server: {
    // 允许开发模式读取仓库根目录的 src/proto/message.proto（生产构建不受此项影响）。
    fs: {
      allow: [fileURLToPath(new URL('..', import.meta.url))],
    },
    // 开发模式：Vite 起在 5173，把 /ws 代理到后端 8080，前端仍按 location.host 连接即可
    proxy: {
      '/ws': { target: 'ws://127.0.0.1:8080', ws: true },
    },
  },
});
