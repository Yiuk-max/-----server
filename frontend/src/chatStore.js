import { reactive, computed } from 'vue';

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
  logs: [],
  conn: { host: '', port: '', path: '/ws' },
  form: { username: 'webuser', password: '123456', email: '', account: '' },
});

let ws = null;
let heartbeatTimer = null;
let pendingShow = false;
let tempSeq = 0;
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
  ws.onopen = () => { state.connected = true; state.connecting = false; startHeartbeat(); log('已连接'); };
  ws.onclose = (e) => {
    state.connected = false; state.connecting = false; stopHeartbeat();
    log(`连接已关闭 (code=${e.code})`);
    ws = null;
  };
  ws.onerror = () => log('WebSocket 错误：确认服务端 net_layer=websocket');
  ws.onmessage = onMessage;
}

function disconnect() { if (ws) ws.close(); }

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
  ws.send(JSON.stringify(obj));
  return true;
}

// ---------- 接收 ----------

function onMessage(ev) {
  if (typeof ev.data !== 'string') return;   // 文件二进制帧暂不处理
  let msg;
  try { msg = JSON.parse(ev.data); } catch { log(ev.data); return; }
  switch (msg.type) {
    case 'system':           return onSystem(msg.content ?? '');
    case 'heartbeat_ack':    return;
    case 'history_response': return onHistory(msg);
    case 'private_chat':     return onLive(msg, msg.sender_UID, false);
    case 'Group_Chat':       return onLive(msg, msg.group_UID, true);
    case 'delete_message':   return onDelete(msg);
    default:                 log(`[${msg.type}] ${msg.content ?? ''}`);
  }
}

function onSystem(content) {
  const reg = /Registration successful.*UID (\d+)/.exec(content);
  if (reg) state.form.account = String(parseInt(reg[1], 10));

  if (pendingShow) {
    pendingShow = false;
    applyContacts(parseContacts(content));
    return;
  }

  log(content.trimEnd());

  const login = /Login successful.*\(UID (\d+)\)/.exec(content);
  if (login) {
    state.myUid = parseInt(login[1], 10);
    const nm = /Welcome, (.*?)!/.exec(content);
    state.myName = nm ? nm[1] : '';
    state.conversations = {};
    state.activeId = null;
    state.contacts = [];
    setTimeout(() => show(), 300);
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

function onDelete(msg) {
  const id = msg.message_id;
  if (!id) return;
  for (const key of Object.keys(state.conversations)) {
    const conv = state.conversations[key];
    const before = conv.messages.length;
    conv.messages = conv.messages.filter((m) => m.message_id !== id);
    if (conv.messages.length !== before) sortConv(conv);
  }
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

function show() { if (send({ type: 'show' })) pendingShow = true; }

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

function logout() { send({ type: 'logout' }); }

function setEmail() {
  const email = state.form.email;
  if (!email.includes('@') || !email.includes('.com')) { log('请输入邮箱（需包含 @ 和 .com）'); return; }
  send({ type: 'set_email', email });
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
  register, login, logout, show, setEmail,
  selectContact, sendChat, loadOlder, isMine, setReply, clearReply,
};

export function useChat() { return chat; }
