<script setup>
import { useChat } from '@/chatStore.js';
const { state } = useChat();
</script>

<template>
  <main class="layout" style="grid-template-columns: 1fr;">
    <section class="card" style="max-width: 900px; width: 100%; margin: 0 auto;">
      <h2>当前会话</h2>
      <p style="font-size: 13px; color: var(--muted);">
        服务器：<code>{{ state.conn.host }}:{{ state.conn.port }}{{ state.conn.path }}</code> ·
        状态：{{ state.connected ? '已连接' : '未连接' }} ·
        我的 UID：{{ state.myUid ?? '-' }} · 昵称：{{ state.myName || '-' }}
      </p>
      <p style="font-size: 13px; color: var(--muted);">
        提示：本前端只用后端既有 WebSocket 协议，未改动后端。历史记录用 message_id 游标分页（每页 10 条）。
      </p>

      <h2 style="margin-top: 16px;">系统日志</h2>
      <div class="log-box">
        <div v-for="(line, i) in state.logs" :key="i">{{ line }}</div>
      </div>
    </section>
  </main>
</template>
