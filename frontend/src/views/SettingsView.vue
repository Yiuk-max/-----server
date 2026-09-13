<script setup>
import { ref } from 'vue';
import { useChat } from '@/chatStore.js';
const chat = useChat();
const { state } = chat;
const newName = ref('');
</script>

<template>
  <div class="settings">
      <h2>连接</h2>
      <div class="grid2">
        <label>Host<input v-model="state.conn.host" /></label>
        <label>Port<input v-model="state.conn.port" /></label>
        <label>Path<input v-model="state.conn.path" /></label>
      </div>
      <div class="row" style="margin-top: 10px; max-width: 320px;">
        <button class="btn" :disabled="state.connected" @click="chat.connect()">连接</button>
        <button class="btn ghost" :disabled="!state.connected" @click="chat.disconnect()">断开</button>
      </div>

      <h2>账号</h2>
      <p>
        服务器：<code>{{ state.conn.host }}:{{ state.conn.port }}{{ state.conn.path }}</code> ·
        状态：<code>{{ state.connected ? '已连接' : '未连接' }}</code><br />
        我的 UID：<code>{{ state.myUid ?? '-' }}</code> · 昵称：<code>{{ state.myName || '-' }}</code>
      </p>
      <div class="row" style="max-width: 420px; margin-bottom: 10px;">
        <label style="flex:1;">新昵称
          <input v-model="newName" placeholder="输入新昵称" />
        </label>
        <button class="btn ghost" style="align-self: flex-end;" @click="chat.changeName(newName)">修改昵称</button>
      </div>
      <div class="row" style="max-width: 420px;">
        <label style="flex:1;">邮箱（换绑）
          <input v-model="state.form.email" placeholder="you@example.com" />
        </label>
        <button class="btn ghost" style="align-self: flex-end;" @click="chat.setEmail()">设置邮箱</button>
      </div>
      <div class="row" style="margin-top: 10px; max-width: 200px;">
        <button class="btn ghost" :disabled="!state.connected" @click="chat.logout()">登出</button>
      </div>

      <h2>系统日志</h2>
      <div class="log-box">
        <div v-for="(line, i) in state.logs" :key="i">{{ line }}</div>
      </div>
  </div>
</template>
