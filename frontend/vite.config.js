import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath, URL } from 'node:url'

export default defineConfig({
  plugins: [vue()],
  base: './',
  resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
  build: { outDir: '../web', emptyOutDir: true, sourcemap: false, assetsInlineLimit: 4096 },
  server: {
    fs: { allow: [fileURLToPath(new URL('..', import.meta.url))] },
    proxy: { '/ws': { target: 'ws://127.0.0.1:8080', ws: true } },
  },
})
