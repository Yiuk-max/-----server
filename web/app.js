// 前端聊天页：与后端 WebSocket 协议（客户端接口文档.txt）对接。
// 仅使用原生 WebSocket + DOM，无框架依赖。
(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);

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
  let pendingShow = false;   // 已发送 show，等待下一条 system 消息作为会话列表
  let active = null;         // { name, uid, isGroup }

  // ---------- 连接 ----------

  function connect() {
    if (ws && ws.readyState <= WebSocket.OPEN) return;
    const url = `ws://${els.host.value.trim()}:${els.port.value.trim()}${els.path.value.trim()}`;
    addSystem(`连接 ${url}`);
    ws = new WebSocket(url);

    ws.onopen = () => {
      setStatus(true);
      startHeartbeat();
    };
    ws.onclose = (e) => {
      setStatus(false);
      stopHeartbeat();
      addSystem(`连接已关闭 (code=${e.code})`);
    };
    ws.onerror = () => addSystem('WebSocket 错误：请确认服务端 net_layer=websocket');
    ws.onmessage = (e) => {
      if (typeof e.data !== 'string') return;   // 文件二进制帧暂不处理
      let msg;
      try { msg = JSON.parse(e.data); } catch { addSystem(e.data); return; }
      handleMessage(msg);
    };
  }

  function disconnect() {
    if (ws) ws.close();
  }

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
    const type = msg.type;
    const content = msg.content ?? '';

    if (type === 'system')                       return handleSystem(content);
    if (type === 'heartbeat_ack')                return;
    if (type === 'private_chat')                 return addBubble('in', content, '私聊');
    if (type === 'Group_Chat')                   return addBubble('in', content, `群 ${msg.group_UID ?? ''}`);
    if (type === 'delete_message')               return removeMessage(msg.message_id);
    addSystem(`[${type}] ${content}`);
  }

  function handleSystem(content) {
    // 1) 注册成功：解析 UID 自动填入登录框
    const reg = /Registration successful.*UID (\d+)/.exec(content);
    if (reg) { els.account.value = String(parseInt(reg[1], 10)); }

    // 2) show 的响应就是下一条 system（列表每行 名称:UID）
    if (pendingShow) {
      pendingShow = false;
      renderContacts(parseContacts(content));
      return;
    }

    addSystem(content.trimEnd());

    // 登录成功后自动拉取一次会话列表（稍等，避开登录时的自动 system 消息）
    if (/Login successful/.test(content)) setTimeout(() => requestShow(), 300);
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
      const li = document.createElement('li');
      const name = document.createElement('span');
      name.textContent = c.name;
      const kind = document.createElement('span');
      kind.className = 'kind';
      kind.textContent = c.isGroup ? '群' : '好友';
      li.append(name, kind);
      li.onclick = () => selectContact(c, li);
      els.contacts.appendChild(li);
    }
  }

  function selectContact(c, li) {
    active = c;
    for (const el of els.contacts.querySelectorAll('li')) el.classList.remove('active');
    if (li) li.classList.add('active');
    els.peer.textContent = `${c.name} (${c.uid})`;
    els.peerKind.textContent = c.isGroup ? '群聊' : '私聊';
    els.peerKind.classList.remove('hidden');
  }

  function requestShow() {
    if (send({ type: 'show' })) pendingShow = true;
  }

  // ---------- 消息渲染 ----------

  function addBubble(dir, text, kind) {
    const div = document.createElement('div');
    div.className = `bubble ${dir}`;
    div.textContent = text;
    if (kind) {
      const meta = document.createElement('span');
      meta.className = 'meta';
      meta.textContent = kind;
      div.appendChild(meta);
    }
    els.messages.appendChild(div);
    scrollBottom();
  }

  function addSystem(text) {
    const div = document.createElement('div');
    div.className = 'system';
    div.textContent = text;
    els.messages.appendChild(div);
    scrollBottom();
  }

  function removeMessage(messageId) {
    if (messageId == null) return;
    const el = els.messages.querySelector(`[data-message-id="${messageId}"]`);
    if (el) el.remove();
  }

  function scrollBottom() {
    els.messages.scrollTop = els.messages.scrollHeight;
  }

  // ---------- 发送业务消息 ----------

  function sendChat(text) {
    if (!active) { addSystem('请先在左侧会话列表选择一个目标'); return; }
    const payload = active.isGroup
      ? { type: 'group_chat', target_UID: active.uid, message: text }
      : { type: 'private_chat', target_UID: active.uid, message: text };
    if (!send(payload)) return;
    // 群聊会收到服务端广播回显；私聊只回 system 回执，这里做本地回显
    if (!active.isGroup) addBubble('out', text, '私聊');
  }

  // ---------- 事件绑定 ----------

  $('btn-connect').onclick = connect;
  $('btn-disconnect').onclick = disconnect;
  function validEmail(email) {
    // 按需求只做最基本判断：含 @ 与 .com 即放行，不做真实校验
    return email.includes('@') && email.includes('.com');
  }

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

  // 默认连当前页面所在主机（页面由服务端提供时即同源）；file:// 打开则回退本机
  window.addEventListener('DOMContentLoaded', () => {
    els.host.value = location.hostname || '127.0.0.1';
    els.port.value = location.port || '8080';
    addSystem('填入账号后点「连接」→「注册」→「登录」→「刷新」会话列表');
  });
})();
