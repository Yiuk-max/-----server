import { reactive, computed } from 'vue';
import { decodeEnvelope, encodeEnvelope } from '@/protobufProtocol.js';

// ============================================================
// 与 UI 解耦的聊天状态/客户端。
// 只依赖后端 WebSocket 协议（客户端接口文档.txt）：
//   - 登录后 show -> 每个会话 history_request 预加载最近 10 条
//   - 上滑用 message_id 作 before_id 拉更旧的一页
//   - private_chat 按 sender_UID、Group_Chat 按 group_UID 路由到会话
//   - 按 message_id 去重；本地回显标 local 且固定 orderKey
// ============================================================

const state = reactive({
  connected: false,
  connecting: false,
  myUid: null,
  myName: '',
  contacts: [],          // [{ uid, name, isGroup }]
  conversations: {},     // peerId -> { peer,name,isGroup,messages[],oldestId,hasMore,loaded,loading,pendingBefore }
  activeId: null,
  replyTarget: null,     // { peer, message_id, sender_name, content } 当前正在回复的消息
  friendRequests: [],    // [{ sender_UID, sender_name, apply_message }]
  groupMembers: [],      // [{ uid, name }] 当前查看的群成员
  groupRequests: [],     // [{ requester_UID, message }] 当前查看的入群申请（单群）
  groupRequestsAll: [],  // [{ group_UID, requester_UID, message }] 所有群的入群申请
  profile: null,         // { uid, name, isSelf } 当前查看的个人主页
  ui: { addFriend: false, friendRequests: false, groupManage: null },
  logs: [],
  conn: { host: '', port: '', path: '/ws' },
  form: { username: 'webuser', password: '123456', email: '', account: '' },
});

let ws = null;
let heartbeatTimer = null;
let receiveChain = Promise.resolve();
let tempSeq = 0;
const pendingQueue = [];   // 待解析的 system 响应类型队列（按发送顺序消费）
const historyWaiters = {};   // peer -> resolve(history)

function log(text) {
  state.logs.push(`${new Date().toLocaleTimeString()}  ${text}`);
  if (state.logs.length > 300) state.logs.splice(0, state.logs.length - 300);
}

function initConn() {
  if (!state.conn.host) state.conn.host = location.hostname || '127.0.0.1';
  if (!state.conn.port) state.conn.port = location.port || '8080';
  if (!state.conn.path) state.conn.path = '/ws';
}
initConn();

// ---------- 连接 ----------

function connect() {
  if (ws && ws.readyState <= WebSocket.OPEN) return;
  const { host, port, path } = state.conn;
  const url = `ws://${host}:${port}${path}`;
  log(`连接 ${url}`);
  state.connecting = true;
  ws = new WebSocket(url);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { state.connected = true; state.connecting = false; startHeartbeat(); log('已连接'); tryTokenLogin(); };
  ws.onclose = (e) => {
    state.connected = false; state.connecting = false; stopHeartbeat();
    log(`连接已关闭 (code=${e.code})`);
    ws = null;
  };
  ws.onerror = () => log('WebSocket 错误：确认服务端 net_layer=websocket');
  ws.onmessage = (ev) => {
    receiveChain = receiveChain
      .then(() => onMessage(ev))
      .catch((error) => log(`协议解析失败：${error.message}`));
  };
}

function disconnect() { if (ws) ws.close(); }

// token 自动登录：登录成功后保存 token，页面启动时用它验证登录
const TOKEN_KEY = 'chat.token';

function tryTokenLogin() {
  const token = localStorage.getItem(TOKEN_KEY);
  if (token) send({ type: 'verify_token', token });
}

function startHeartbeat() {
  stopHeartbeat();
  heartbeatTimer = setInterval(() => send({ type: 'heartbeat' }), 30000);
}
function stopHeartbeat() {
  if (heartbeatTimer) clearInterval(heartbeatTimer);
  heartbeatTimer = null;
}

function send(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) { log('未连接，无法发送'); return false; }
  try {
    ws.send(encodeEnvelope(obj));
    return true;
  } catch (error) {
    log(`消息编码失败：${error.message}`);
    return false;
  }
}

// 发送一个会返回 system 文本的请求，并把解析类型入队（按发送顺序消费）
function sendWithPending(obj, action, ctx) {
  if (send(obj)) pendingQueue.push({ action, ctx });
}

// ---------- 接收 ----------

async function onMessage(ev) {
  const msg = await decodeEnvelope(ev.data);
  switch (msg.type) {
    case 'system':           return onSystem(msg.content ?? '');
    case 'heartbeat_ack':    return;
    case 'history_response': return onHistory(msg);
    case 'private_chat':     return onLive(msg, msg.sender_UID, false);
    case 'Group_Chat':       return onLive(msg, msg.group_UID, true);
    case 'delete_message':   return onDelete(msg);
    case 'login_success':    return onLoginSuccess(msg);
    default:                 log(`[${msg.type}] ${msg.content ?? ''}`);
  }
}

function onLoginSuccess(msg) {
  state.myUid = msg.user && msg.user.id != null ? msg.user.id : null;
  state.myName = (msg.user && msg.user.username) || '';
  if (msg.token) localStorage.setItem(TOKEN_KEY, msg.token);
  state.form.account = state.myUid != null ? String(state.myUid) : '';
  state.conversations = {};
  state.activeId = null;
  state.contacts = [];
  log('登录成功');
  setTimeout(() => { show(); showFriendRequests(); }, 300);
}

function onSystem(content) {
  const reg = /Registration successful.*UID (\d+)/.exec(content);
  if (reg) state.form.account = String(parseInt(reg[1], 10));

  if (pendingQueue.length) {
    const { action, ctx } = pendingQueue.shift();
    if (action === 'show') { applyContacts(parseContacts(content)); return; }
    if (action === 'friendRequests') { state.friendRequests = parseFriendRequests(content); return; }
    if (action === 'groupMembers') { state.groupMembers = parseGroupMembers(content); return; }
    if (action === 'groupRequests') {
      const list = parseGroupRequests(content).map((r) => ({ ...r, group_UID: ctx ? ctx.groupUid : null }));
      if (ctx && ctx.all) state.groupRequestsAll = state.groupRequestsAll.concat(list);
      else state.groupRequests = list;
      return;
    }
  }

  log(content.trimEnd());

  // token 过期/无效：清除本地 token，回到登录界面
  if (/Token expired|Invalid or expired token/.test(content)) {
    try { localStorage.removeItem(TOKEN_KEY); } catch {}
  }

  // 有人申请加好友：自动刷新申请列表
  if (/wants to be friend with you/.test(content)) {
    setTimeout(() => showFriendRequests(), 300);
  }

  // 建群成功：刷新会话列表（新群出现）
  if (/Group created successfully/.test(content)) {
    setTimeout(() => show(), 300);
  }

  // 被拉入群 / 被踢出群：刷新会话列表
  if (/You have been added to group|You have been removed from group/.test(content)) {
    setTimeout(() => show(), 300);
  }

  // 有人申请入群：群管理面板开着时刷新申请列表
  if (/has requested to join your group/.test(content) && state.ui.groupManage != null) {
    setTimeout(() => showGroupRequests(state.ui.groupManage), 300);
  }

  const nameReg = /Name updated successfully\. Your new name is \[(.*?)\]\./.exec(content);
  if (nameReg) {
    state.myName = nameReg[1];
    // 刷新页面后用 token 自动重登，让会话列表/历史里的旧昵称一起刷新
    setTimeout(() => location.reload(), 400);
  }

  const delReg = /Message \[(\d+)\] deleted\./.exec(content);
  if (delReg) removeLocalMessage(parseInt(delReg[1], 10));

  // 私聊发送回执：把当前会话最后一条本地回显消息升级为真实 message_id（便于删除）
  const sentReg = /Message sent\. Message ID: (\d+)\./.exec(content);
  if (sentReg) {
    const realId = parseInt(sentReg[1], 10);
    const conv = activeConv.value;
    if (conv) {
      for (let i = conv.messages.length - 1; i >= 0; i--) {
        const m = conv.messages[i];
        if (m.local && typeof m.message_id === 'number' && m.message_id < 0) {
          m.message_id = realId;
          m.local = false;
          delete m.orderKey;
          sortConv(conv);
          break;
        }
      }
    }
  }

}

// 直播/离线消息：私聊按 sender_UID、群聊按 group_UID 路由
function onLive(msg, peer, isGroup) {
  if (peer == null) { log(`收到无法定位会话的消息: ${JSON.stringify(msg)}`); return; }
  const conv = ensureConv(peer, isGroup);
  addMessage(conv, {
    message_id: msg.message_id,
    sender_UID: msg.sender_UID ?? null,
    sender_name: msg.sender_name || '',
    is_group: isGroup,
    content: msg.content ?? '',
    timestamp: msg.timestamp || '',
    reply_to: msg.reply_to || null,
  });
}

function onHistory(msg) {
  const peer = msg.peer_id;
  const conv = ensureConv(peer, false);
  conv.loading = false;
  conv.loaded = true;
  conv.pendingBefore = 0;

  const existing = new Set(conv.messages.map((m) => m.message_id));
  for (const m of (msg.messages || [])) {
    if (m.message_id && existing.has(m.message_id)) continue;
    conv.messages.push({
      message_id: m.message_id,
      sender_UID: m.sender_UID ?? null,
      sender_name: m.sender_name || '',
      is_group: !!m.is_group,
      content: m.content ?? '',
      timestamp: m.timestamp || '',
      reply_to: m.reply_to || null,
    });
  }
  sortConv(conv);
  conv.hasMore = !!msg.has_more;

  if (historyWaiters[peer]) { historyWaiters[peer](true); delete historyWaiters[peer]; }
}

function removeLocalMessage(id) {
  if (!id) return;
  for (const key of Object.keys(state.conversations)) {
    const conv = state.conversations[key];
    const before = conv.messages.length;
    conv.messages = conv.messages.filter((m) => m.message_id !== id);
    if (conv.messages.length !== before) sortConv(conv);
  }
}

function onDelete(msg) {
  removeLocalMessage(msg.message_id);
}

// ---------- 会话与消息存储 ----------

function ensureConv(peer, isGroup, name) {
  let conv = state.conversations[peer];
  if (!conv) {
    conv = {
      peer, name: name || String(peer), isGroup: !!isGroup, messages: [],
      oldestId: null, hasMore: true, loaded: false, loading: false, pendingBefore: 0,
    };
    state.conversations[peer] = conv;
  }
  if (isGroup) conv.isGroup = true;
  if (name) conv.name = name;
  return conv;
}

function msgKey(m) {
  if (typeof m.orderKey === 'number') return m.orderKey;
  return (typeof m.message_id === 'number' && m.message_id > 0)
    ? m.message_id : Number.MAX_SAFE_INTEGER;
}

function nextLocalKey(conv) {
  const maxId = conv.messages.reduce((mx, m) => {
    const k = (typeof m.message_id === 'number' && m.message_id > 0) ? m.message_id : 0;
    return Math.max(mx, k);
  }, 0);
  return maxId + 0.5;
}

function sortConv(conv) {
  conv.messages.sort((a, b) => msgKey(a) - msgKey(b));
  const ids = conv.messages.map((m) => m.message_id)
    .filter((id) => typeof id === 'number' && id > 0);
  conv.oldestId = ids.length ? Math.min(...ids) : null;
}

function addMessage(conv, item) {
  if (item.message_id && conv.messages.some((m) => m.message_id === item.message_id)) return;
  conv.messages.push(item);
  sortConv(conv);
}

// ---------- 会话列表 ----------

function parseContacts(text) {
  const list = [];
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    const idx = line.lastIndexOf(':');
    if (idx < 0) continue;
    const name = line.slice(0, idx);
    const uidPart = line.slice(idx + 1).trim();
    if (!/^\d+$/.test(uidPart)) continue;
    list.push({ name, uid: parseInt(uidPart, 10), isGroup: /\(\d+\)\s*$/.test(name) });
  }
  return list;
}

function applyContacts(list) {
  state.contacts = list;
  for (const c of list) {
    const conv = ensureConv(c.uid, c.isGroup, c.name);
    conv.name = c.name;
    conv.isGroup = c.isGroup;
    if (!conv.loaded && !conv.loading) requestHistory(c.uid, 0);
  }
}

function selectContact(peer) {
  state.activeId = Number(peer);
  state.replyTarget = null;
  const conv = ensureConv(state.activeId);
  if (!conv.loaded && !conv.loading) requestHistory(state.activeId, 0);
}

function requestHistory(peer, beforeId) {
  const conv = ensureConv(peer);
  conv.loading = true;
  conv.pendingBefore = beforeId || 0;
  return new Promise((resolve) => {
    historyWaiters[peer] = resolve;
    const ok = send(beforeId > 0
      ? { type: 'history_request', peer_id: Number(peer), before_id: beforeId }
      : { type: 'history_request', peer_id: Number(peer) });
    if (!ok) { conv.loading = false; delete historyWaiters[peer]; resolve(false); }
  });
}

function loadOlder(peer) {
  const conv = ensureConv(peer);
  if (conv.loading || !conv.hasMore || !conv.oldestId) return Promise.resolve(false);
  return requestHistory(peer, conv.oldestId);
}

// ---------- 账号操作 ----------

function show() { sendWithPending({ type: 'show' }, 'show'); }

function register() {
  const { username, password, email } = state.form;
  if (!email.includes('@') || !email.includes('.com')) { log('请输入邮箱（需包含 @ 和 .com）'); return; }
  send({ type: 'register', username, password, email });
}

function login() {
  const acc = (state.form.account || '').trim();
  if (!acc) { log('请填写 UID 或邮箱'); return; }
  if (acc.includes('@')) send({ type: 'login', email: acc, password: state.form.password });
  else send({ type: 'login', UID: parseInt(acc, 10), password: state.form.password });
}

function logout() {
  try { localStorage.removeItem(TOKEN_KEY); } catch {}
  send({ type: 'logout' });
}

function setEmail() {
  const email = state.form.email;
  if (!email.includes('@') || !email.includes('.com')) { log('请输入邮箱（需包含 @ 和 .com）'); return; }
  send({ type: 'set_email', email });
}

// ---------- 好友申请 ----------

function parseFriendRequests(text) {
  const list = [];
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    // 兼容空留言：`[名字] (UID):` 或 `[名字] (UID): 留言`
    const m = /^\[(.*?)\] \((\d+)\)(?::\s*(.*))?$/.exec(line);
    if (m) list.push({ sender_name: m[1], sender_UID: parseInt(m[2], 10), apply_message: m[3] || '' });
  }
  return list;
}

function showFriendRequests() { sendWithPending({ type: 'show_friend_requests' }, 'friendRequests'); }

function acceptFriend(senderUid) {
  if (send({ type: 'accept_friend', sender_UID: senderUid })) {
    // 本地移除该申请，并只刷新会话列表（新好友出现）；避免 show/showFriendRequests 并发抢 pending 标志
    state.friendRequests = state.friendRequests.filter((r) => r.sender_UID !== senderUid);
    setTimeout(() => show(), 300);
  }
}

function rejectFriend(senderUid) {
  if (send({ type: 'reject_friend', sender_UID: senderUid })) {
    state.friendRequests = state.friendRequests.filter((r) => r.sender_UID !== senderUid);
  }
}

function addFriend(email, message) {
  const e = (email || '').trim();
  if (!e) { log('请输入对方邮箱'); return; }
  send({ type: 'add_friend', email: e, apply_message: (message || '').trim() });
}

function createGroup(name) {
  const n = (name || '').trim();
  if (!n) { log('请输入群名称'); return; }
  send({ type: 'create_group', group_name: n });
}

// ---------- 群聊 ----------

function parseGroupMembers(text) {
  const list = [];
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    const idx = line.indexOf(':');
    if (idx < 0) continue;
    const uidPart = line.slice(0, idx).trim();
    const name = line.slice(idx + 1).trim();
    if (/^\d+$/.test(uidPart)) list.push({ uid: parseInt(uidPart, 10), name });
  }
  return list;
}

function parseGroupRequests(text) {
  const list = [];
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    // 兼容空留言：`Requester UID: 43, Message:` 或 `Requester UID: 43, Message: hi`
    const m = /^Requester UID: (\d+), Message:\s*(.*)$/.exec(line);
    if (m) list.push({ requester_UID: parseInt(m[1], 10), message: m[2] || '' });
  }
  return list;
}

function showGroupMembers(groupUid) { sendWithPending({ type: 'show_group_members', group_UID: groupUid }, 'groupMembers'); }
function showGroupRequests(groupUid, all = false) { sendWithPending({ type: 'show_group_requests', group_UID: groupUid }, 'groupRequests', { groupUid, all }); }

// 遍历我加入的所有群，拉取发给我的入群申请（用于好友页右侧汇总）
function loadAllGroupRequests() {
  state.groupRequestsAll = [];
  const groups = state.contacts.filter((c) => c.isGroup);
  for (const g of groups) showGroupRequests(g.uid, true);
}

function sendJoinGroup(groupUid) {
  const uid = Number(groupUid);
  if (!uid || uid <= 0) { log('请输入有效的群 UID'); return; }
  send({ type: 'send_join_group', group_UID: uid });
}

function handleJoinRequest(groupUid, requesterUid, accept) {
  send({ type: 'handle_join_request', group_UID: groupUid, requester_UID: requesterUid, accept });
  // 乐观移除本地申请，避免按钮没刷新导致重复点击
  state.groupRequests = state.groupRequests.filter((r) => r.requester_UID !== requesterUid);
  state.groupRequestsAll = state.groupRequestsAll.filter((r) => !(r.group_UID === groupUid && r.requester_UID === requesterUid));
  // 延迟刷新成员列表 + 单群申请 + 全群汇总
  setTimeout(() => {
    showGroupMembers(groupUid);
    showGroupRequests(groupUid);
    loadAllGroupRequests();
  }, 300);
}

function groupAddClient(groupUid, userUid) {
  const uid = Number(userUid);
  if (!uid || uid <= 0) { log('请输入有效的用户 UID'); return; }
  send({ type: 'group_add_client', group_UID: groupUid, target_user_UID: uid });
  setTimeout(() => showGroupMembers(groupUid), 300);
}

function groupDeleteClient(groupUid, userUid) {
  send({ type: 'group_delete_client', group_UID: groupUid, target_user_UID: userUid });
  setTimeout(() => showGroupMembers(groupUid), 300);
}

function modifyGroupName(groupUid, name) {
  const n = (name || '').trim();
  if (!n) { log('请输入群名称'); return; }
  send({ type: 'modify_group_name', group_UID: groupUid, new_name: n });
  setTimeout(() => show(), 300);
}

function deleteGroup(groupUid) {
  send({ type: 'delete_group', group_UID: groupUid });
  closeGroupManage();
  setTimeout(() => show(), 300);
}

function openGroupManage(groupUid) {
  state.ui.groupManage = groupUid;
  state.groupMembers = [];
  state.groupRequests = [];
  showGroupMembers(groupUid);
  showGroupRequests(groupUid);
}

function closeGroupManage() {
  state.ui.groupManage = null;
  state.groupMembers = [];
  state.groupRequests = [];
}

function openAddFriend() { state.ui.addFriend = true; }
function closeAddFriend() { state.ui.addFriend = false; }
function openFriendRequests() { state.ui.friendRequests = true; showFriendRequests(); }
function closeFriendRequests() { state.ui.friendRequests = false; }

// ---------- 个人主页 / 删除好友 ----------

function openSelfProfile() {
  state.profile = { uid: state.myUid, name: state.myName || '我', isSelf: true };
}

function openUserProfile(uid, name) {
  if (uid == null) return;
  state.profile = {
    uid,
    name: name || String(uid),
    isSelf: state.myUid != null && uid === state.myUid,
  };
}

function closeProfile() { state.profile = null; }

function removeFriend(friendUid) {
  if (!friendUid) return;
  send({ type: 'remove_friend', friend_UID: friendUid });
  closeProfile();
  setTimeout(() => {
    show(); // 刷新会话列表（移除该好友）
    if (state.conversations[friendUid]) delete state.conversations[friendUid];
    if (state.activeId === friendUid) state.activeId = null;
  }, 300);
}

// 带 token 重新登录刷新
function refresh() {
  location.reload();
}

function changeName(newName) {
  const name = (newName || '').trim();
  if (!name) { log('请输入新昵称'); return; }
  send({ type: 'change_name', new_name: name });
}

function deleteMessage(messageId) {
  if (!messageId) return;
  // 删除成功由服务端 system "Message [x] deleted." 回执驱动本地移除；
  // 接收方则由 delete_message 广播驱动移除。
  send({ type: 'delete_message', message_id: messageId });
}

function sendChat(text) {
  const conv = activeConv.value;
  if (!conv) { log('请先选择会话'); return; }
  const reply = (state.replyTarget && state.replyTarget.peer === conv.peer) ? state.replyTarget : null;
  const payload = conv.isGroup
    ? { type: 'group_chat', target_UID: conv.peer, message: text }
    : { type: 'private_chat', target_UID: conv.peer, message: text };
  if (reply) payload.reply_to_message_id = reply.message_id;
  if (!send(payload)) return;
  if (!conv.isGroup) {
    // 私聊不给自己回显 → 本地回显（orderKey 固定在已有消息之后、服务端回显之前）
    const item = {
      message_id: --tempSeq,
      orderKey: nextLocalKey(conv),
      sender_UID: state.myUid,
      sender_name: state.myName,
      is_group: false,
      content: text,
      timestamp: '',
      local: true,
    };
    if (reply) {
      item.reply_to = { message_id: reply.message_id, sender_name: reply.sender_name, content: reply.content };
    }
    addMessage(conv, item);
  }
  state.replyTarget = null;
}

// 标记“正在回复”某条消息（只针对当前会话）
function setReply(message) {
  const conv = activeConv.value;
  if (!conv) return;
  state.replyTarget = {
    peer: conv.peer,
    message_id: message.message_id,
    sender_name: message.sender_name || (isMine(message, conv) ? state.myName : conv.name),
    content: message.content,
  };
}

function clearReply() { state.replyTarget = null; }

// ---------- 展示辅助 ----------

// 右侧=自己发的。私聊按“发送者是不是对方”判断（自聊时收到的回显落在左侧）；
// 群聊按发送者是否为自己；本地回显恒为自己发的。
function isMine(m, conv) {
  if (m.local) return true;
  if (m.sender_UID == null) return false;
  if (conv && conv.isGroup) return state.myUid != null && m.sender_UID === state.myUid;
  return conv ? m.sender_UID !== conv.peer : false;
}

const activeConv = computed(() => (
  state.activeId != null ? state.conversations[state.activeId] : null
));

const chat = {
  state, activeConv,
  connect, disconnect,
  register, login, logout, show, setEmail, changeName, deleteMessage,
  addFriend, createGroup, showFriendRequests, acceptFriend, rejectFriend,
  sendJoinGroup, showGroupMembers, showGroupRequests, loadAllGroupRequests, handleJoinRequest,
  groupAddClient, groupDeleteClient, modifyGroupName, deleteGroup,
  openGroupManage, closeGroupManage,
  openAddFriend, closeAddFriend, openFriendRequests, closeFriendRequests,
  openSelfProfile, openUserProfile, closeProfile, removeFriend, refresh,
  selectContact, sendChat, loadOlder, isMine, setReply, clearReply,
};

export function useChat() { return chat; }
