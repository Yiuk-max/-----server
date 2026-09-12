<script setup>
import { nextTick, ref, watch } from 'vue';
import { useChat } from '@/chatStore.js';

const chat = useChat();
const { state, activeConv } = chat;

const draft = ref('');
const messagesRef = ref(null);

// 加载更旧一页时保持滚动位置
let preserve = false;
let prevHeight = 0;
let prevTop = 0;

function onSend() {
  const text = draft.value.trim();
  if (!text) return;
  chat.sendChat(text);
  draft.value = '';
}

async function onScroll() {
  const el = messagesRef.value;
  if (!el || el.scrollTop > 4) return;
  const conv = activeConv.value;
  if (!conv || conv.loading || !conv.hasMore || !conv.oldestId) return;
  preserve = true;
  prevHeight = el.scrollHeight;
  prevTop = el.scrollTop;
  await chat.loadOlder(conv.peer);
  await nextTick();
  const el2 = messagesRef.value;
  if (el2) el2.scrollTop = el2.scrollHeight - prevHeight + prevTop;
  preserve = false;
}

// 切换会话滚到底部
watch(() => activeConv.value?.peer, async () => {
  await nextTick();
  const el = messagesRef.value;
  if (el) el.scrollTop = el.scrollHeight;
});

// 新消息：接近底部时自动滚到底
watch(() => activeConv.value?.messages.length, async () => {
  if (preserve) return;
  await nextTick();
  const el = messagesRef.value;
  if (!el) return;
  if (el.scrollHeight - el.scrollTop - el.clientHeight < 120) el.scrollTop = el.scrollHeight;
});
</script>

<template>
  <main class="layout">
    <aside class="sidebar">
      <section class="card">
        <h2>连接</h2>
        <div class="grid">
          <label>Host<input v-model="state.conn.host" /></label>
          <label>Port<input v-model="state.conn.port" /></label>
          <label class="span2">Path<input v-model="state.conn.path" /></label>
        </div>
        <div class="row">
          <button :disabled="state.connected" @click="chat.connect()">连接</button>
          <button class="ghost" :disabled="!state.connected" @click="chat.disconnect()">断开</button>
        </div>
      </section>

      <section class="card">
        <h2>账号</h2>
        <label>昵称<input v-model="state.form.username" /></label>
        <label>密码<input v-model="state.form.password" type="password" /></label>
        <label>邮箱（注册必填 / 可换绑）<input v-model="state.form.email" placeholder="you@example.com" /></label>
        <div class="row">
          <button @click="chat.register()">注册</button>
          <button @click="chat.login()">登录</button>
        </div>
        <div class="row">
          <button class="ghost" @click="chat.setEmail()">设置邮箱</button>
          <button class="ghost" @click="chat.logout()">登出</button>
        </div>
        <label class="span2">UID 或邮箱（登录用）<input v-model="state.form.account" placeholder="UID 或邮箱" /></label>
      </section>

      <section class="card grow">
        <div class="card-head">
          <h2>会话</h2>
          <button class="mini" :disabled="!state.connected" @click="chat.show()">刷新</button>
        </div>
        <ul class="contacts">
          <li v-if="!state.contacts.length" class="empty">暂无会话（加好友或建群后刷新）</li>
          <li v-for="c in state.contacts" :key="c.uid"
              :class="{ active: state.activeId === c.uid }"
              @click="chat.selectContact(c.uid)">
            <span>{{ c.name }}</span>
            <span class="kind">{{ c.isGroup ? '群' : '好友' }}</span>
          </li>
        </ul>
      </section>
    </aside>

    <section class="chat">
      <div class="chat-head">
        <span v-if="activeConv">{{ activeConv.name }} ({{ activeConv.peer }})</span>
        <span v-else>未选择会话</span>
        <span v-if="activeConv" class="tag">{{ activeConv.isGroup ? '群聊' : '私聊' }}</span>
      </div>

      <div ref="messagesRef" class="messages" @scroll="onScroll">
        <div v-if="activeConv && (activeConv.hasMore || activeConv.loading) && activeConv.messages.length"
             class="history-hint">
          {{ activeConv.loading ? '加载中…' : '上滑加载更早的消息' }}
        </div>
        <div v-for="m in activeConv?.messages || []" :key="m.message_id"
             class="bubble" :class="chat.isMine(m, activeConv) ? 'out' : 'in'">
          <span v-if="!chat.isMine(m, activeConv) && m.sender_name" class="who">{{ m.sender_name }}</span>
          <blockquote v-if="m.reply_to" class="quote">
            <span class="quote-who">{{ m.reply_to.sender_name || ('#' + m.reply_to.message_id) }}</span>
            <span class="quote-text">{{ m.reply_to.content }}</span>
          </blockquote>
          <span>{{ m.content }}</span>
          <button class="reply-btn" title="回复" @click="chat.setReply(m)">↩</button>
        </div>
      </div>

      <form class="composer" @submit.prevent="onSend">
        <div v-if="state.replyTarget" class="reply-bar">
          <div class="reply-bar-text">
            <b>回复 {{ state.replyTarget.sender_name }}</b>
            <span>{{ state.replyTarget.content }}</span>
          </div>
          <button type="button" class="reply-cancel" @click="chat.clearReply()">×</button>
        </div>
        <div class="composer-row">
          <input v-model="draft" placeholder="输入消息，回车发送" />
          <button type="submit">发送</button>
        </div>
      </form>
    </section>
  </main>
</template>
