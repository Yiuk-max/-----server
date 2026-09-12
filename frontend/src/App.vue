<script setup>
import { useChat } from '@/chatStore.js';
const { state } = useChat();
</script>

<template>
  <header class="topbar">
    <h1>Chat Server</h1>
    <span class="status" :class="state.connected ? 'on' : 'off'">
      {{ state.connected ? '已连接' : '未连接' }}
    </span>
    <nav class="nav">
      <RouterLink to="/">聊天</RouterLink>
      <RouterLink to="/settings">设置</RouterLink>
    </nav>
  </header>
  <RouterView />
</template>
