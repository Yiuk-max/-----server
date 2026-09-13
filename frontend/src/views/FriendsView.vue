<script setup>
import { computed, onMounted, ref } from 'vue';
import { useChat } from '@/chatStore.js';

const chat = useChat();
const { state } = chat;

const email = ref('');
const message = ref('');
const groupName = ref('');
const joinGroupUid = ref('');
const tab = ref('friend');

const friends = computed(() => state.contacts.filter((c) => !c.isGroup));

const allRequests = computed(() => {
  const friend = state.friendRequests.map((r) => ({
    kind: 'friend',
    key: 'f' + r.sender_UID,
    uid: r.sender_UID,
    name: r.sender_name,
    message: r.apply_message,
  }));
  const group = state.groupRequestsAll.map((r) => ({
    kind: 'group',
    key: 'g' + r.group_UID + '-' + r.requester_UID,
    groupUid: r.group_UID,
    uid: r.requester_UID,
    name: '#群 ' + r.group_UID,
    message: '申请人 #' + r.requester_UID + (r.message ? ' · ' + r.message : ''),
  }));
  return [...friend, ...group];
});

onMounted(() => {
  if (state.connected) {
    chat.showFriendRequests();
    chat.loadAllGroupRequests();
  }
});

function submit() {
  chat.addFriend(email.value, message.value);
  email.value = '';
  message.value = '';
}
function submitGroup() {
  chat.createGroup(groupName.value);
  groupName.value = '';
}
function submitJoinGroup() {
  chat.sendJoinGroup(joinGroupUid.value);
  joinGroupUid.value = '';
}
function refreshRequests() {
  chat.showFriendRequests();
  chat.loadAllGroupRequests();
}
function acceptRequest(r) {
  if (r.kind === 'friend') chat.acceptFriend(r.uid);
  else chat.handleJoinRequest(r.groupUid, r.uid, true);
}
function rejectRequest(r) {
  if (r.kind === 'friend') chat.rejectFriend(r.uid);
  else chat.handleJoinRequest(r.groupUid, r.uid, false);
}
function avatarLetter(name) {
  return name ? name.trim().charAt(0).toUpperCase() : '?';
}
</script>

<template>
  <div class="friends">
    <!-- 左：通讯录 -->
    <section class="friends-col">
      <div class="friends-head">
        <h2>通讯录</h2>
        <span class="rooms-count">{{ friends.length }} 位好友</span>
      </div>
      <div class="friend-list">
        <div v-if="!friends.length" class="rooms-empty">暂无好友</div>
        <div
          v-for="c in friends" :key="c.uid"
          class="friend-item"
          @click="chat.openUserProfile(c.uid, c.name)"
        >
          <div class="room-icon dm">{{ avatarLetter(c.name) }}<span class="dot"></span></div>
          <div class="room-body">
            <div class="room-name">{{ c.name }}</div>
            <div class="room-preview">UID {{ c.uid }}</div>
          </div>
          <button class="btn ghost small" @click.stop="chat.removeFriend(c.uid)">删除</button>
        </div>
      </div>
    </section>

    <!-- 中：添加好友 / 创建群聊 -->
    <section class="friends-col">
      <div class="friends-head"><h2>添加</h2></div>
      <div class="tab-switch">
        <button :class="{ active: tab === 'friend' }" @click="tab = 'friend'">加好友</button>
        <button :class="{ active: tab === 'group' }" @click="tab = 'group'">加群聊</button>
      </div>

      <template v-if="tab === 'friend'">
        <label>对方邮箱
          <input v-model="email" placeholder="friend@example.com" />
        </label>
        <label>申请留言
          <input v-model="message" placeholder="我是 xxx（可留空）" />
        </label>
        <button class="btn" style="width:100%;" @click="submit">发送申请</button>
        <p class="sub" style="margin-top:12px;">对方通过后，会出现在你的通讯录与会话列表里。</p>
      </template>

      <template v-else>
        <label>创建群聊（群名称）
          <input v-model="groupName" placeholder="输入群名称" />
        </label>
        <button class="btn" style="width:100%;" @click="submitGroup">创建群聊</button>
        <div class="sep-line"></div>
        <label>按 UID 申请入群
          <input v-model="joinGroupUid" placeholder="输入群 UID" />
        </label>
        <button class="btn ghost" style="width:100%;" @click="submitJoinGroup">申请加入</button>
        <p class="sub" style="margin-top:12px;">创建后你成为群主；按 UID 入群需群主同意。</p>
      </template>
    </section>

    <!-- 右：好友申请 + 入群申请 -->
    <section class="friends-col">
      <div class="friends-head">
        <h2>申请列表</h2>
        <button class="btn ghost small" @click="refreshRequests">刷新</button>
      </div>
      <div class="friend-list">
        <div v-if="!allRequests.length" class="rooms-empty">暂无待处理申请</div>
        <div v-for="r in allRequests" :key="r.key" class="friend-item">
          <div class="room-icon dm">{{ r.kind === 'friend' ? avatarLetter(r.name) : '#' }}</div>
          <div class="room-body">
            <div class="room-name">
              <span class="req-tag" :class="r.kind">{{ r.kind === 'friend' ? '好友' : '入群' }}</span>
              {{ r.name }}
            </div>
            <div class="room-preview">{{ r.message || '（无留言）' }}</div>
          </div>
          <button class="btn small" @click="acceptRequest(r)">同意</button>
          <button class="btn ghost small" @click="rejectRequest(r)">拒绝</button>
        </div>
      </div>
    </section>
  </div>
</template>
