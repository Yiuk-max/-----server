<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue';
import { useRoute } from 'vue-router';
import { useChat } from '@/chatStore.js';
import { themes, loadTheme, applyTheme, cycleTheme } from '@/theme.js';

const route = useRoute();
const chat = useChat();
const { state } = chat;

const themeId = ref(loadTheme());
const themeOpen = ref(false);

const avatarLetter = (name) => (name ? name.trim().charAt(0).toUpperCase() : '?');

const isFriend = computed(() => {
  const p = state.profile;
  if (!p || p.isSelf) return false;
  return state.contacts.some((c) => !c.isGroup && c.uid === p.uid);
});

onMounted(() => {
  applyTheme(themeId.value);
  document.addEventListener('click', closeTheme);
  // 页面加载后自动连接 WebSocket（host/port 已由 chatStore 从 location 初始化）
  if (!state.connected && !state.connecting) chat.connect();
});
onBeforeUnmount(() => document.removeEventListener('click', closeTheme));

function closeTheme(e) {
  if (themeOpen.value && e && e.target && !e.target.closest('.theme-wrap')) {
    themeOpen.value = false;
  }
}
function toggleThemeMenu() {
  themeOpen.value = !themeOpen.value;
}
function pickTheme(id) {
  themeId.value = applyTheme(id);
  themeOpen.value = false;
}
function quickCycle() {
  themeId.value = cycleTheme(themeId.value).id;
}
function swatchStyle(colors) {
  if (colors.length >= 3) {
    return `linear-gradient(135deg, ${colors[0]} 34%, ${colors[1]} 34% 67%, ${colors[2]} 67%)`;
  }
  return `linear-gradient(135deg, ${colors[0]} 50%, ${colors[1]} 50%)`;
}
const profileName = ref('');
function submitChangeName() {
  chat.changeName(profileName.value);
  profileName.value = '';
}
function confirmRemoveFriend() {
  const p = state.profile;
  if (!p) return;
  if (window.confirm(`确定删除好友「${p.name}」？`)) {
    chat.removeFriend(p.uid);
  }
}
</script>

<template>
  <div class="app">
    <aside class="rail">
      <div class="rail-logo">CS</div>

      <RouterLink to="/" class="rail-item" :class="{ active: route.path === '/' }" title="聊天">#</RouterLink>
      <RouterLink to="/friends" class="rail-item" :class="{ active: route.path === '/friends' }" title="好友">@</RouterLink>

      <div class="rail-spacer"></div>

      <div class="theme-wrap">
        <button class="rail-item" :class="{ active: themeOpen }" title="切换主题" @click="toggleThemeMenu">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor"
               stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
            <path d="M20.38 3.46 16 2a4 4 0 0 1-8 0L3.62 3.46a2 2 0 0 0-1.34 2.23l.58 3.47a1 1 0 0 0 .99.84H6v10c0 1.1.9 2 2 2h8a2 2 0 0 0 2-2V10h2.15a1 1 0 0 0 .99-.84l.58-3.47a2 2 0 0 0-1.34-2.23z"/>
          </svg>
        </button>
        <div v-if="themeOpen" class="dropdown theme-menu" @click.stop>
          <div class="hint">主题配色 · 点击切换</div>
          <div class="sep"></div>
          <button
            v-for="t in themes"
            :key="t.id"
            :class="{ active: t.id === themeId }"
            @click="pickTheme(t.id)"
          >
            <span class="swatch" :style="{ background: swatchStyle(t.colors) }"></span>
            <span class="grow">{{ t.name }}<em>{{ t.kind === 'three' ? '三色' : '两色' }}</em></span>
            <span v-if="t.id === themeId" class="check">✓</span>
          </button>
          <div class="sep"></div>
          <button @click="quickCycle">换一套（循环）</button>
        </div>
      </div>

      <RouterLink to="/settings" class="rail-item" :class="{ active: route.path === '/settings' }" title="设置">⚙</RouterLink>

      <div class="rail-avatar" :class="{ off: !state.myUid }" title="个人主页" @click="chat.openSelfProfile()">{{ avatarLetter(state.myName) }}</div>
    </aside>

    <RouterView />

    <!-- 个人主页弹窗 -->
    <div v-if="state.profile" class="overlay" @click.self="chat.closeProfile()">
      <div class="modal profile-modal">
        <div class="profile-head">
          <div class="profile-avatar">{{ avatarLetter(state.profile.name) }}</div>
          <div>
            <div class="profile-name">{{ state.profile.name }}</div>
            <div class="sub">UID {{ state.profile.uid }}</div>
          </div>
        </div>

        <template v-if="state.profile.isSelf">
          <label>昵称
            <input v-model="profileName" :placeholder="state.myName || '输入新昵称'" />
          </label>
          <button class="btn" style="width:100%;" @click="submitChangeName">修改昵称</button>
          <label style="margin-top:10px;">邮箱
            <input v-model="state.form.email" placeholder="you@example.com" />
          </label>
          <button class="btn ghost" style="width:100%;" @click="chat.setEmail()">设置邮箱</button>
          <button class="btn ghost" style="width:100%; margin-top:10px; color:var(--danger); border-color:var(--danger);" @click="chat.logout()">登出</button>
        </template>

        <template v-else>
          <p class="sub">{{ isFriend ? '你们是好友' : '对方不在你的好友列表（可能来自群聊）' }}</p>
          <button v-if="isFriend" class="btn" style="width:100%; background:var(--danger); border-color:var(--danger);" @click="confirmRemoveFriend">删除好友</button>
        </template>

        <div class="modal-actions">
          <button class="btn" @click="chat.closeProfile()">关闭</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.theme-wrap { position: relative; }
.theme-menu { bottom: 8px; left: 48px; width: 230px; }
.theme-menu button { display: flex; align-items: center; gap: 8px; }
.theme-menu button.active { color: var(--accent); }
.theme-menu .grow { flex: 1; }
.theme-menu em { font-style: normal; font-size: 10px; color: var(--text-tertiary); margin-left: 6px; }
.theme-menu .check { color: var(--accent); font-size: 12px; }
.swatch {
  width: 14px; height: 14px; border-radius: 4px; flex-shrink: 0;
  border: 1px solid var(--border-strong);
}
</style>
