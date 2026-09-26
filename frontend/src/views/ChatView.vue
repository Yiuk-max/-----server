<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useChat } from '@/chatStore.js';
import { emojiGroups } from '@/emoji.js';

const chat = useChat();
const { state, activeConv } = chat;

const draft = ref('');
const filterText = ref('');
const messagesRef = ref(null);
const ctxMenu = ref(null);
const draftRef = ref(null);
const emojiOpen = ref(false);
const activeEmojiGroup = ref(emojiGroups[0].name);

const activeGroupItems = computed(() => {
  const g = emojiGroups.find((x) => x.name === activeEmojiGroup.value);
  return g ? g.items : [];
});

// 加载更旧一页时保持滚动位置
let preserve = false;
let prevHeight = 0;
let prevTop = 0;

const groups = computed(() => state.contacts.filter((c) => c.isGroup));
const dms = computed(() => state.contacts.filter((c) => !c.isGroup));

function matches(c) {
  const q = filterText.value.trim().toLowerCase();
  if (!q) return true;
  return c.name.toLowerCase().includes(q) || String(c.uid).includes(q);
}
const visibleGroups = computed(() => groups.value.filter(matches));
const visibleDms = computed(() => dms.value.filter(matches));

function previewOf(c) {
  const conv = state.conversations[c.uid];
  const last = conv && conv.messages.length ? conv.messages[conv.messages.length - 1] : null;
  return last ? last.content : '暂无消息';
}
function timeOf(c) {
  const conv = state.conversations[c.uid];
  const last = conv && conv.messages.length ? conv.messages[conv.messages.length - 1] : null;
  return last ? fmtTime(last.timestamp) : '';
}

// "YYYY-MM-DD HH:MM:SS" -> "HH:MM"
function fmtTime(ts) {
  if (!ts) return '';
  return String(ts).slice(11, 16);
}
function avatarLetter(name) {
  return name ? name.trim().charAt(0).toUpperCase() : '?';
}

function onSend() {
  const text = draft.value.trim();
  if (!text) return;
  chat.sendChat(text);
  draft.value = '';
  emojiOpen.value = false;
}

// 回车发送，Shift+Enter 换行；输入法合成中不发送
function onEnter(e) {
  if (e.isComposing || e.shiftKey) return;
  e.preventDefault();
  onSend();
}

// 把 emoji 追加到输入框文字末尾（不发送）
function insertEmoji(emoji) {
  draft.value = draft.value + emoji;
  nextTick(() => {
    const el = draftRef.value;
    if (el) {
      el.focus();
      el.selectionStart = el.selectionEnd = draft.value.length;
    }
  });
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

// 消息不满一屏时自动往前加载，直到能滚动（或没有更早）
async function ensureFilled() {
  const conv = activeConv.value;
  const el = messagesRef.value;
  if (!conv || !el) return;
  if (!conv.hasMore || conv.loading || !conv.oldestId) return;
  if (el.scrollHeight <= el.clientHeight + 40) {
    await chat.loadOlder(conv.peer);
    await nextTick();
    const el2 = messagesRef.value;
    if (el2) el2.scrollTop = el2.scrollHeight; // 滚到底部看最新
    ensureFilled();
  }
}

// 手动点击“加载更早的消息”
async function loadMoreManual() {
  const conv = activeConv.value;
  if (!conv || conv.loading || !conv.hasMore || !conv.oldestId) return;
  const el = messagesRef.value;
  const prevH = el ? el.scrollHeight : 0;
  const prevT = el ? el.scrollTop : 0;
  await chat.loadOlder(conv.peer);
  await nextTick();
  const el2 = messagesRef.value;
  if (el2) el2.scrollTop = el2.scrollHeight - prevH + prevT;
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
  ensureFilled();
});

function openMsgProfile(m) {
  if (chat.isMine(m, activeConv.value)) {
    chat.openSelfProfile();
  } else {
    chat.openUserProfile(m.sender_UID, m.sender_name || activeConv.value?.name);
  }
}

// ---- 群管理 ----
const groupRename = ref('');
const addMemberUid = ref('');
function renameGroup() {
  if (state.ui.groupManage != null) chat.modifyGroupName(state.ui.groupManage, groupRename.value);
  groupRename.value = '';
}
function addMember() {
  if (state.ui.groupManage != null) chat.groupAddClient(state.ui.groupManage, addMemberUid.value);
  addMemberUid.value = '';
}
function disbandGroup() {
  const gid = state.ui.groupManage;
  if (gid != null && window.confirm('确定解散该群？')) {
    chat.deleteGroup(gid);
  }
}

// ---- 消息右键菜单 ----
function openMenu(e, m) {
  ctxMenu.value = { x: e.clientX, y: e.clientY, message: m };
}
function closeMenu() {
  ctxMenu.value = null;
}
function replyFromMenu() {
  if (!ctxMenu.value) return;
  chat.setReply(ctxMenu.value.message);
  closeMenu();
}
function deleteFromMenu() {
  if (!ctxMenu.value) return;
  chat.deleteMessage(ctxMenu.value.message.message_id);
  closeMenu();
}
function fallbackCopy(text) {
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.style.position = 'fixed';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.select();
  try { document.execCommand('copy'); } catch {}
  document.body.removeChild(ta);
}
function copyFromMenu() {
  if (!ctxMenu.value) return;
  const text = ctxMenu.value.message.content || '';
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).catch(() => fallbackCopy(text));
  } else {
    fallbackCopy(text);
  }
  closeMenu();
}
onMounted(() => document.addEventListener('click', closeMenu));
onBeforeUnmount(() => document.removeEventListener('click', closeMenu));
</script>

<template>
  <div class="main">
    <!-- ============ rooms 会话列表 ============ -->
    <aside class="rooms">
      <!-- 未登录：登录卡片 -->
      <template v-if="!state.myUid">
        <div class="rooms-header">
          <div>
            <div class="rooms-title">消息</div>
            <div class="rooms-count">请先登录</div>
          </div>
        </div>
        <div class="login-card">
          <h3>连接 · 账号</h3>
          <div class="login-row" style="align-items:center;">
            <span class="rooms-count" style="flex:1;">{{ state.connecting ? '连接中…' : (state.connected ? '已连接' : '未连接') }}</span>
            <button class="btn ghost small" :disabled="state.connected || state.connecting" @click="chat.connect()">连接</button>
            <button class="btn ghost small" :disabled="!state.connected" @click="chat.disconnect()">断开</button>
          </div>
          <label>昵称（注册用）
            <input v-model="state.form.username" placeholder="昵称" />
          </label>
          <label>密码
            <input v-model="state.form.password" type="password" placeholder="密码" />
          </label>
          <label>邮箱（注册必填）
            <input v-model="state.form.email" placeholder="you@example.com" />
          </label>
          <label>UID 或邮箱（登录用）
            <input v-model="state.form.account" placeholder="UID 或邮箱" />
          </label>
          <div class="login-row">
            <button class="btn" @click="chat.register()">注册</button>
            <button class="btn ghost" @click="chat.login()">登录</button>
          </div>
          <div class="login-row">
            <button class="btn ghost small" :disabled="!state.connected" @click="chat.show()">刷新列表</button>
          </div>
        </div>
      </template>

      <!-- 已登录：会话列表 -->
      <template v-else>
        <div class="rooms-header">
          <div>
            <div class="rooms-title">消息</div>
            <div class="rooms-count">{{ state.contacts.length }} 会话</div>
          </div>
          <div class="rooms-tools">
            <button class="tool-chip" title="刷新（重新登录）" @click="chat.refresh()">⟳</button>
          </div>
        </div>
        <input class="search" v-model="filterText" placeholder="搜索会话" />

        <div class="room-scroll">
          <template v-if="!state.contacts.length">
            <div class="rooms-empty">
              暂无会话。<br />登录成功后会自动加载好友与群组；<br />加好友 / 建群功能即将接入。
            </div>
          </template>

          <template v-if="visibleGroups.length">
            <div class="room-group-label">群组</div>
            <div
              v-for="c in visibleGroups" :key="'g' + c.uid"
              class="room" :class="{ active: state.activeId === c.uid }"
              @click="chat.selectContact(c.uid)"
            >
              <div class="room-icon">#</div>
              <div class="room-body">
                <div class="room-name">{{ c.name }}</div>
                <div class="room-preview">{{ previewOf(c) }}</div>
              </div>
              <div class="room-meta"><div class="room-time">{{ timeOf(c) }}</div></div>
            </div>
          </template>

          <template v-if="visibleDms.length">
            <div class="room-group-label">私聊</div>
            <div
              v-for="c in visibleDms" :key="'d' + c.uid"
              class="room" :class="{ active: state.activeId === c.uid }"
              @click="chat.selectContact(c.uid)"
            >
              <div class="room-icon dm">{{ avatarLetter(c.name) }}<span class="dot"></span></div>
              <div class="room-body">
                <div class="room-name">{{ c.name }}</div>
                <div class="room-preview">{{ previewOf(c) }}</div>
              </div>
              <div class="room-meta"><div class="room-time">{{ timeOf(c) }}</div></div>
            </div>
          </template>
        </div>
      </template>
    </aside>

    <!-- ============ chat 聊天区 ============ -->
    <section class="chat">
      <header class="chat-header">
        <div class="chat-title-wrap" v-if="activeConv">
          <div class="chat-hash">{{ activeConv.isGroup ? '#' : '@' }}</div>
          <div>
            <div class="chat-title">{{ activeConv.name }}</div>
            <div class="chat-sub">{{ activeConv.isGroup ? '群聊 · ' + activeConv.peer : '私聊 · ' + activeConv.peer }}</div>
          </div>
        </div>
        <div class="chat-title-wrap" v-else>
          <div class="chat-hash">#</div>
          <div>
            <div class="chat-title">未选择会话</div>
            <div class="chat-sub">从左侧选择一个会话开始聊天</div>
          </div>
        </div>
        <div class="chat-header-right">
          <div class="online-pill" v-if="state.connected"><div class="dot2"></div>在线</div>
          <div class="online-pill" v-else><div class="dot2" style="background:var(--text-tertiary)"></div>离线</div>
          <button v-if="activeConv && activeConv.isGroup" class="icon-btn" title="群管理" @click="chat.openGroupManage(activeConv.peer)">⋯</button>
        </div>
      </header>

      <!-- 消息区 -->
      <div ref="messagesRef" class="messages" @scroll="onScroll" v-if="activeConv">
        <div v-if="activeConv.hasMore || activeConv.loading" class="history-hint" @click="loadMoreManual">
          {{ activeConv.loading ? '加载中…' : '上滑 / 点击加载更早的消息' }}
        </div>

        <div class="day-divider"><span>今天</span></div>

        <div
          v-for="m in activeConv.messages" :key="m.message_id"
          class="msg" :class="{ mine: chat.isMine(m, activeConv), 'ctx-target': ctxMenu && ctxMenu.message === m }"
          @contextmenu.prevent="openMenu($event, m)"
        >
          <div class="avatar" @click="openMsgProfile(m)">{{ avatarLetter(m.sender_name || (chat.isMine(m, activeConv) ? state.myName : activeConv.name)) }}</div>
          <div class="msg-col">
            <div class="msg-meta">
              <span class="msg-name">{{ chat.isMine(m, activeConv) ? (state.myName || '我') : (m.sender_name || activeConv.name) }}</span>
              <span class="msg-time">{{ fmtTime(m.timestamp) }}</span>
            </div>
            <div class="bubble">
              <blockquote v-if="m.reply_to" class="quote">
                <span class="quote-who">{{ m.reply_to.sender_name || ('#' + m.reply_to.message_id) }}</span>
                <span class="quote-text">{{ m.reply_to.content }}</span>
              </blockquote>
              <span>{{ m.content }}</span>
            </div>

          </div>
        </div>
      </div>

      <!-- 空态 -->
      <div class="chat-empty" v-else>
        <b>{{ state.myUid ? '选择一个会话' : '欢迎使用 Baka Community' }}</b>
        <span>{{ state.myUid ? '点击左侧会话开始聊天' : '请在左侧登录后开始使用' }}</span>
      </div>

      <!-- 输入区 -->
      <form class="composer" @submit.prevent="onSend" v-if="activeConv">
        <div v-if="state.replyTarget" class="reply-bar">
          <div class="reply-bar-text">
            <b>回复 {{ state.replyTarget.sender_name }}</b>
            <span>{{ state.replyTarget.content }}</span>
          </div>
          <button type="button" class="reply-cancel" @click="chat.clearReply()">×</button>
        </div>
        <div class="composer-box">
          <textarea
            ref="draftRef"
            v-model="draft" class="composer-input" rows="1"
            :placeholder="'在 ' + activeConv.name + ' 发消息…'"
            @keydown.enter="onEnter"
          ></textarea>
          <div class="composer-row">
            <div class="composer-tools">
              <button type="button" class="tool-btn" :class="{ on: emojiOpen }" title="表情" @click="emojiOpen = !emojiOpen">🙂</button>
              <button type="button" class="tool-btn" title="文件（WS 暂不支持）">📎</button>
            </div>
            <button type="submit" class="send-btn">发送</button>
          </div>
        </div>

        <!-- emoji 面板 -->
        <div v-if="emojiOpen" class="emoji-panel">
          <div class="emoji-tabs">
            <button
              v-for="g in emojiGroups" :key="g.name" type="button"
              :class="{ active: activeEmojiGroup === g.name }"
              :title="g.name"
              @click="activeEmojiGroup = g.name"
            >{{ g.icon }}</button>
          </div>
          <div class="emoji-grid">
            <button v-for="e in activeGroupItems" :key="e" type="button" @click="insertEmoji(e)">{{ e }}</button>
          </div>
        </div>
      </form>

      <!-- 右键菜单 -->
      <div
        v-if="ctxMenu" class="ctx-menu"
        :style="{ left: ctxMenu.x + 'px', top: ctxMenu.y + 'px' }"
        @click.stop
      >
        <button @click="replyFromMenu">回复</button>
        <button @click="copyFromMenu">复制</button>
        <button
          v-if="chat.isMine(ctxMenu.message, activeConv)"
          class="danger"
          @click="deleteFromMenu"
        >删除</button>
      </div>
    </section>

    <!-- 群管理弹窗 -->
    <div v-if="state.ui.groupManage != null" class="overlay" @click.self="chat.closeGroupManage()">
      <div class="modal">
        <h3>群管理 · #{{ state.ui.groupManage }}</h3>

        <div class="row" style="margin-bottom:10px;">
          <input v-model="groupRename" placeholder="新群名称" />
          <button class="btn ghost" style="flex:0 0 60px;" @click="renameGroup">改名</button>
        </div>

        <div class="row" style="margin-bottom:12px;">
          <input v-model="addMemberUid" placeholder="按 UID 拉人入群" />
          <button class="btn ghost" style="flex:0 0 60px;" @click="addMember">拉人</button>
        </div>

        <div class="sub" style="margin-bottom:6px;">群成员（{{ state.groupMembers.length }}）</div>
        <div class="list">
          <div v-for="m in state.groupMembers" :key="m.uid" class="list-item">
            <div class="grow">{{ m.name }} <span class="sub">#{{ m.uid }}</span></div>
            <button v-if="m.uid !== state.myUid" class="btn ghost small" @click="chat.groupDeleteClient(state.ui.groupManage, m.uid)">踢出</button>
          </div>
        </div>

        <div class="sub" style="margin:12px 0 6px;">入群申请（{{ state.groupRequests.length }}）</div>
        <div class="list">
          <div v-if="!state.groupRequests.length" class="sub">暂无待处理申请</div>
          <div v-for="r in state.groupRequests" :key="r.requester_UID" class="list-item">
            <div class="grow">#{{ r.requester_UID }} <span class="sub">{{ r.message }}</span></div>
            <button class="btn small" @click="chat.handleJoinRequest(state.ui.groupManage, r.requester_UID, true)">同意</button>
            <button class="btn ghost small" @click="chat.handleJoinRequest(state.ui.groupManage, r.requester_UID, false)">拒绝</button>
          </div>
        </div>

        <div class="modal-actions">
          <button class="btn ghost" style="color:var(--danger); border-color:var(--danger);" @click="disbandGroup">解散群</button>
          <button class="btn" @click="chat.closeGroupManage()">关闭</button>
        </div>
      </div>
    </div>
  </div>
</template>
