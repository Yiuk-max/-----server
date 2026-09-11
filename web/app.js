// 前端聊天页：与后端 WebSocket 协议（客户端接口文档.txt）对接。
// 历史记录用 message_id 作游标分页：登录后每个会话预加载最近 10 条，
// 滚动到顶部再拉更旧的 10 条。仅使用原生 WebSocket + DOM，无框架依赖。
(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const PAGE_SIZE = 10;   // 与后端一致（后端固定每页 10 条）

  const els = {
    status: $('status'),
    host: $('host'), port: $('port'), path: $('path'),
    username: $('username'), password: $('password'), email: $('email'), account: $('account'),
    contacts: $('contacts'),
    messages: $('messages'),
    peer: $('peer'), peerKind: $('peer-kind'),
    message: $('message'),
  };

  let ws = null;
  let heartbeatTimer = null;
  let pendingShow = false;   // 已发送 show，等待下一条 system 作为会话列表
  let myUid = null;          // 当前登录 UID（登录成功后从提示解析）
  let myName = '';
  let active = null;         // 当前会话的 peer_id
  let tempSeq = 0;           // 本地回显用的临时（负数）消息 id

  // peer_id -> { name, isGroup, messages[], oldestId, hasMore, loaded, loading, pendingBefore }
  const store = new Map();

  // ---------- 连接 ----------

  function connect() {
    if (ws && ws.readyState <= WebSocket.OPEN) return;
    const url = `ws://${els.host.value.trim()}:${els.port.value.trim()}${els.path.value.trim()}`;
    addSystem(`连接 ${url}`);
    ws = new WebSocket(url);

    ws.onopen = () => { setStatus(true); startHeartbeat(); };
    ws.onclose = (e) => { setStatus(false); stopHeartbeat(); addSystem(`连接已关闭 (code=${e.code})`); };
    ws.onerror = () => addSystem('WebSocket 错误：请确认服务端 net_layer=websocket');
    ws.onmessage = (e) => {
      if (typeof e.data !== 'string') return;   // 文件二进制帧暂不处理
      let msg;
      try { msg = JSON.parse(e.data); } catch { addSystem(e.data); return; }
      handleMessage(msg);
    };
  }

  function disconnect() { if (ws) ws.close(); }

  function setStatus(on) {
    els.status.textContent = on ? '已连接' : '未连接';
    els.status.className = 'status ' + (on ? 'on' : 'off');
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
    if (!ws || ws.readyState !== WebSocket.OPEN) { addSystem('未连接，无法发送'); return false; }
    ws.send(JSON.stringify(obj));
    return true;
  }

  // ---------- 接收处理 ----------

  function handleMessage(msg) {
    switch (msg.type) {
      case 'system':            return handleSystem(msg.content ?? '');
      case 'heartbeat_ack':     return;
      case 'history_response':  return handleHistory(msg);
      case 'private_chat':      return handleLive(msg, msg.sender_UID, false);
      case 'Group_Chat':        return handleLive(msg, msg.group_UID, true);
      case 'delete_message':    return handleDelete(msg);
      default:                  addSystem(`[${msg.type}] ${msg.content ?? ''}`);
    }
  }

  function handleSystem(content) {
    // 注册成功：解析 UID 自动填入登录框
    const reg = /Registration successful.*UID (\d+)/.exec(content);
    if (reg) { els.account.value = String(parseInt(reg[1], 10)); }

    // show 的响应就是下一条 system（列表每行 名称:UID）
    if (pendingShow) {
      pendingShow = false;
      const list = parseContacts(content);
      renderContacts(list);
      preloadHistory(list);
      return;
    }

    addSystem(content.trimEnd());

    // 登录成功：解析自己的 UID / 昵称，然后拉会话列表
    const login = /Login successful.*\(UID (\d+)\)/.exec(content);
    if (login) {
      myUid = parseInt(login[1], 10);
      const nm = /Welcome, (.*?)!/.exec(content);
      myName = nm ? nm[1] : '';
      store.clear();
      active = null;
      els.contacts.innerHTML = '';
      setTimeout(() => requestShow(), 300);
    }
  }

  // 直播/离线消息：按 sender_UID（私聊）或 group_UID（群聊）路由到对应会话
  function handleLive(msg, peer, isGroup) {
    if (peer == null) { addSystem(`收到无法定位会话的消息: ${JSON.stringify(msg)}`); return; }
    const st = ensureStore(peer, isGroup);
    addMessageToStore(st, {
      message_id: msg.message_id,
      sender_UID: msg.sender_UID ?? null,
      sender_name: msg.sender_name || '',
      is_group: isGroup,
      content: msg.content ?? '',
      timestamp: msg.timestamp || '',
    });
    if (active === peer) renderMessages();
  }

  // 历史响应：message.id 游标分页
  function handleHistory(msg) {
    const peer = msg.peer_id;
    const st = ensureStore(peer, false);
    const older = st.pendingBefore > 0;
    st.loading = false;
    st.loaded = true;
    st.pendingBefore = 0;

    const existing = new Set(st.messages.map((m) => m.message_id));
    for (const m of (msg.messages || [])) {
      if (m.message_id && existing.has(m.message_id)) continue;
      st.messages.push({
        message_id: m.message_id,
        sender_UID: m.sender_UID ?? null,
        sender_name: m.sender_name || '',
        is_group: !!m.is_group,
        content: m.content ?? '',
        timestamp: m.timestamp || '',
      });
    }
    sortStore(st);
    st.hasMore = !!msg.has_more;
    if (active === peer) renderMessages(older);
  }

  function handleDelete(msg) {
    const id = msg.message_id;
    if (!id) return;
    for (const [peer, st] of store) {
      const before = st.messages.length;
      st.messages = st.messages.filter((m) => m.message_id !== id);
      if (st.messages.length !== before && active === peer) renderMessages(true);
    }
  }

  // ---------- 会话存储 ----------

  function ensureStore(peer, isGroup, name) {
    let st = store.get(peer);
    if (!st) {
      st = { name: name || String(peer), isGroup: !!isGroup, messages: [],
             oldestId: null, hasMore: true, loaded: false, loading: false, pendingBefore: 0 };
      store.set(peer, st);
    }
    if (isGroup) st.isGroup = true;
    if (name) st.name = name;
    return st;
  }

  // 排序键：本地回显用创建时固定的 orderKey（当前最大真实 id + 0.5），
  // 这样它排在已有消息之后、但排在随后到达的同一条回显（真实 id 更大）之前。
  function msgKey(m) {
    if (typeof m.orderKey === 'number') return m.orderKey;
    return (typeof m.message_id === 'number' && m.message_id > 0)
      ? m.message_id : Number.MAX_SAFE_INTEGER;
  }

  // 本地回显的排序键：当前会话最大真实 message_id + 0.5
  function nextLocalKey(st) {
    const maxId = st.messages.reduce((mx, m) => {
      const k = (typeof m.message_id === 'number' && m.message_id > 0) ? m.message_id : 0;
      return Math.max(mx, k);
    }, 0);
    return maxId + 0.5;
  }

  function sortStore(st) {
    st.messages.sort((a, b) => msgKey(a) - msgKey(b));
    const ids = st.messages.map((m) => m.message_id).filter((id) => typeof id === 'number' && id > 0);
    st.oldestId = ids.length ? Math.min(...ids) : null;
  }

  function addMessageToStore(st, item) {
    if (item.message_id && st.messages.some((m) => m.message_id === item.message_id)) return;
    st.messages.push(item);
    sortStore(st);
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
      const isGroup = /\(\d+\)\s*$/.test(name);
      list.push({ name, uid: parseInt(uidPart, 10), isGroup });
    }
    return list;
  }

  function renderContacts(list) {
    els.contacts.innerHTML = '';
    if (!list.length) {
      const li = document.createElement('li');
      li.className = 'empty';
      li.textContent = '暂无会话（加好友或建群后刷新）';
      els.contacts.appendChild(li);
      return;
    }
    for (const c of list) {
      ensureStore(c.uid, c.isGroup, c.name);
      const li = document.createElement('li');
      li.dataset.peer = c.uid;
      if (active === c.uid) li.classList.add('active');
      const name = document.createElement('span');
      name.textContent = c.name;
      const kind = document.createElement('span');
      kind.className = 'kind';
      kind.textContent = c.isGroup ? '群' : '好友';
      li.append(name, kind);
      li.onclick = () => selectContact(c.uid);
      els.contacts.appendChild(li);
    }
  }

  function preloadHistory(list) {
    // 登录后为每个会话预加载最近一页（已有缓存的不重复拉）
    for (const c of list) {
      const st = ensureStore(c.uid, c.isGroup, c.name);
      if (!st.loaded && !st.loading) requestHistory(c.uid, 0);
    }
  }

  function selectContact(peer) {
    active = peer;
    const st = ensureStore(peer);
    for (const li of els.contacts.querySelectorAll('li')) {
      li.classList.toggle('active', Number(li.dataset.peer) === peer);
    }
    els.peer.textContent = `${st.name} (${peer})`;
    els.peerKind.textContent = st.isGroup ? '群聊' : '私聊';
    els.peerKind.classList.remove('hidden');
    renderMessages();
    if (!st.loaded && !st.loading) requestHistory(peer, 0);
  }

  function requestShow() {
    if (send({ type: 'show' })) pendingShow = true;
  }

  function requestHistory(peer, beforeId) {
    const st = ensureStore(peer);
    st.loading = true;
    st.pendingBefore = beforeId || 0;
    send(beforeId > 0
      ? { type: 'history_request', peer_id: peer, before_id: beforeId }
      : { type: 'history_request', peer_id: peer });
  }

  // ---------- 消息渲染 ----------

  // 判断一条消息是否应显示在右侧（自己发的）。
  // - 本地回显一定是我发的
  // - 群聊：发送者是我
  // - 私聊：发送者不是对方（peer）就是自己——自聊时 peer==自己，收到的回显会落在左侧
  function isMine(m, peer, isGroup) {
    if (m.local) return true;
    if (m.sender_UID == null) return false;
    if (isGroup) return myUid != null && m.sender_UID === myUid;
    return m.sender_UID !== peer;
  }

  function renderMessages(keepScroll) {
    const box = els.messages;
    const prevHeight = box.scrollHeight;
    const prevTop = box.scrollTop;
    box.innerHTML = '';
    if (active == null) return;
    const st = store.get(active);
    if (!st) return;

    if (st.messages.length && (st.hasMore || st.loading)) {
      const hint = document.createElement('div');
      hint.className = 'history-hint';
      hint.textContent = st.loading ? '加载中…' : '上滑加载更早的消息';
      box.appendChild(hint);
    }

    for (const m of st.messages) {
      const mine = isMine(m, active, st.isGroup);
      const div = document.createElement('div');
      div.className = `bubble ${mine ? 'out' : 'in'}`;
      if (!mine && m.sender_name) {
        const who = document.createElement('span');
        who.className = 'who';
        who.textContent = m.sender_name;
        div.appendChild(who);
      }
      const text = document.createElement('span');
      text.textContent = m.content;
      div.appendChild(text);
      if (m.message_id) div.dataset.messageId = m.message_id;
      box.appendChild(div);
    }

    box.scrollTop = keepScroll ? (box.scrollHeight - prevHeight + prevTop) : box.scrollHeight;
  }

  function addSystem(text) {
    const div = document.createElement('div');
    div.className = 'system';
    div.textContent = text;
    els.messages.appendChild(div);
    els.messages.scrollTop = els.messages.scrollHeight;
  }

  // 上滑（滚到顶部）加载更旧的一页
  els.messages.addEventListener('scroll', () => {
    if (active == null || els.messages.scrollTop > 4) return;
    const st = store.get(active);
    if (!st || st.loading || !st.hasMore || !st.oldestId) return;
    requestHistory(active, st.oldestId);
    renderMessages(true);
  });

  // ---------- 发送业务消息 ----------

  function sendChat(text) {
    if (active == null) { addSystem('请先在左侧会话列表选择一个目标'); return; }
    const st = ensureStore(active);
    const payload = st.isGroup
      ? { type: 'group_chat', target_UID: active, message: text }
      : { type: 'private_chat', target_UID: active, message: text };
    if (!send(payload)) return;
    if (!st.isGroup) {
      // 私聊不回显给发送方，这里本地回显（群聊会收到服务端广播）
      addMessageToStore(st, {
        message_id: --tempSeq,
        orderKey: nextLocalKey(st),
        sender_UID: myUid,
        sender_name: myName,
        is_group: false,
        content: text,
        timestamp: '',
        local: true,
      });
      renderMessages();
    }
  }

  // ---------- 事件绑定 ----------

  function validEmail(email) {
    // 按需求只做最基本判断：含 @ 与 .com 即放行，不做真实校验
    return email.includes('@') && email.includes('.com');
  }

  $('btn-connect').onclick = connect;
  $('btn-disconnect').onclick = disconnect;
  $('btn-register').onclick = () => {
    const email = els.email.value.trim();
    if (!validEmail(email)) { addSystem('请输入邮箱（需包含 @ 和 .com）'); return; }
    send({
      type: 'register',
      username: els.username.value.trim(),
      password: els.password.value,
      email,
    });
  };
  $('btn-login').onclick = () => {
    const acc = els.account.value.trim();
    if (!acc) { addSystem('请填写 UID 或邮箱（注册后自动填入 UID）'); return; }
    send(acc.includes('@')
      ? { type: 'login', email: acc, password: els.password.value }
      : { type: 'login', UID: parseInt(acc, 10), password: els.password.value });
  };
  $('btn-set-email').onclick = () => {
    const email = els.email.value.trim();
    if (!validEmail(email)) { addSystem('请输入邮箱（需包含 @ 和 .com）'); return; }
    send({ type: 'set_email', email });
  };
  $('btn-logout').onclick = () => send({ type: 'logout' });
  $('btn-show').onclick = requestShow;

  $('composer').onsubmit = (e) => {
    e.preventDefault();
    const text = els.message.value.trim();
    if (!text) return;
    sendChat(text);
    els.message.value = '';
  };

  window.addEventListener('DOMContentLoaded', () => {
    els.host.value = location.hostname || '127.0.0.1';
    els.port.value = location.port || '8080';
    addSystem('填入账号后点「连接」→「注册」→「登录」；登录后会自动加载各会话最近 10 条，上滑加载更早');
  });
})();
