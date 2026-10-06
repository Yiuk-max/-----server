import { computed, reactive } from 'vue'
import { decodePacket, encodePacket } from './protobufProtocol.js'

const TOKEN_KEY = 'chat.token'
const RESUME_KEY = 'chat.upload.resume.v1'
const EMAIL_KEY = 'chat.email.'

export const backend = reactive({
  connected: false,
  connecting: false,
  uid: null,
  name: '',
  email: '',
  avatarId: 0,
  contacts: [],
  communities: [],
  activeCommunityId: null,
  channels: {},
  members: {},
  conversations: {},
  activeKey: '',
  unreadConversations: {},
  friendRequests: [],
  groupMembers: {},
  groupRequests: {},
  communityRequests: {},
  files: [],
  fileUrls: {},
  transfers: {},
  transferOrder: [],
  lastNotice: '',
  noticeSeq: 0,
})

let ws = null
let receiveChain = Promise.resolve()
let heartbeat = null
let localSeq = 0
let transferSeq = 0
let pendingAvatarId = 0
let friendRefreshTimer = null
let persistTimer = null
const objectUrls = new Set()
const pendingText = []
const historyQueue = []
const channelQueue = []
const memberQueue = []
const communityAddQueue = []
const uploadQueue = []
const downloadQueue = []
const messageAcks = []

const notice = (text) => {
  backend.lastNotice = String(text || '').trim()
  backend.noticeSeq += 1
}

const keyOf = (kind, id) => `${kind}:${Number(id)}`
const activeConversation = computed(() => backend.activeKey ? backend.conversations[backend.activeKey] : null)
const ownsTask = (task) => !!task && Number(task.ownerUid) > 0 && Number(task.ownerUid) === Number(backend.uid)

function createObjectUrl(value) {
  const url = URL.createObjectURL(value)
  objectUrls.add(url)
  return url
}
function revokeObjectUrl(url) {
  if (!url || !objectUrls.has(url)) return
  objectUrls.delete(url)
  URL.revokeObjectURL(url)
}
function replaceFileUrl(fileId, url) {
  const id = String(fileId || '')
  const previous = backend.fileUrls[id]
  if (previous && previous !== url) revokeObjectUrl(previous)
  if (id) backend.fileUrls[id] = url
}
function releaseRuntimeUrls() {
  for (const task of Object.values(backend.transfers)) {
    revokeObjectUrl(task?.url)
    if (task) task.url = ''
  }
  for (const url of Object.values(backend.fileUrls)) revokeObjectUrl(url)
  backend.fileUrls = {}
}

function ensureConversation(kind, id, name = '') {
  const key = keyOf(kind, id)
  if (!backend.conversations[key]) {
    backend.conversations[key] = { key, kind, id: Number(id), name: name || String(id), messages: [], loaded: false, loading: false, hasMore: true, oldestId: 0 }
  }
  if (name) backend.conversations[key].name = name
  return backend.conversations[key]
}

function send(message, fileData = null) {
  if (!ws || ws.readyState !== WebSocket.OPEN) { notice('未连接服务器'); return false }
  try { ws.send(encodePacket(message, fileData)); return true }
  catch (error) { notice(`协议编码失败：${error.message}`); return false }
}

export function connect() {
  if (ws && ws.readyState <= WebSocket.OPEN) return
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws'
  const host = location.host || '127.0.0.1:8080'
  const socket = new WebSocket(`${scheme}://${host}/ws`)
  ws = socket
  backend.connecting = true
  socket.binaryType = 'arraybuffer'
  socket.onopen = () => {
    if (ws !== socket) return
    backend.connected = true
    backend.connecting = false
    if (heartbeat) clearInterval(heartbeat)
    heartbeat = setInterval(() => { if (ws === socket) send({ type: 'heartbeat' }) }, 30000)
    const token = localStorage.getItem(TOKEN_KEY)
    if (token) send({ type: 'verify_token', token })
    else notice('已连接，请登录')
  }
  socket.onclose = () => {
    if (ws !== socket) return
    ws = null
    backend.connected = false
    backend.connecting = false
    if (heartbeat) clearInterval(heartbeat)
    heartbeat = null
    interruptTransfers()
    clearTransportQueues()
    notice('与服务器的连接已断开')
  }
  socket.onerror = () => { if (ws === socket) notice('连接服务器失败') }
  socket.onmessage = (event) => {
    receiveChain = receiveChain.then(() => receive(event, socket)).catch((error) => {
      if (ws === socket) notice(`协议解析失败：${error.message}`)
    })
  }
}

export function disconnect() {
  if (heartbeat) clearInterval(heartbeat)
  heartbeat = null
  const socket = ws
  ws = null
  if (socket) {
    socket.onopen = null; socket.onclose = null; socket.onerror = null; socket.onmessage = null
    socket.close()
  }
  backend.connected = false
  backend.connecting = false
  interruptTransfers()
  flushPersistUploads()
  clearTransportQueues()
  releaseRuntimeUrls()
}

function clearTransportQueues() {
  for (const ack of messageAcks) {
    const conv = backend.conversations[ack.key]
    if (conv && ack.localId) conv.messages = conv.messages.filter((item) => item.id !== ack.localId)
  }
  for (const conv of Object.values(backend.conversations)) conv.loading = false
  pendingText.splice(0); historyQueue.splice(0); channelQueue.splice(0); memberQueue.splice(0); communityAddQueue.splice(0)
  uploadQueue.splice(0); downloadQueue.splice(0); messageAcks.splice(0)
}

async function receive(event, socket) {
  const { message, fileData } = await decodePacket(event.data)
  if (ws !== socket) return
  switch (message.type) {
    case 'heartbeat_ack': return
    case 'login_success': return loginSuccess(message)
    case 'contact_list_response': return contactsResponse(message)
    case 'community_list_response': return communitiesResponse(message)
    case 'channel_list_response': return channelsResponse(message)
    case 'community_member_list_response': return membersResponse(message)
    case 'history_response': return historyResponse(message)
    case 'private_chat': return liveMessage(message, 'private', message.sender_UID)
    case 'Group_Chat': return liveMessage(message, 'group', message.group_UID)
    case 'Channel_Chat': return liveMessage(message, 'channel', message.group_UID)
    case 'delete_message': return deleteMessageLocal(message.message_id)
    case 'file_list_response': return fileListResponse(message)
    case 'file_upload_ready': return uploadReady(message)
    case 'file_upload_complete': return uploadComplete(message)
    case 'file_download_ready': return downloadReady(message)
    case 'file_chunk': return downloadChunk(message, fileData)
    case 'file_transfer_paused': return transferUpdate(message)
    case 'file_transfer_progress': return transferUpdate(message)
    case 'system': return systemMessage(message.content || '')
    default: notice(`功能响应：${message.type || 'unknown'}`)
  }
}

function loginSuccess(message) {
  const nextUid = Number(message.user?.id || 0)
  if (backend.uid && Number(backend.uid) !== nextUid) { interruptTransfers(); clearAccount() }
  backend.uid = nextUid
  backend.name = message.user?.username || ''
  backend.avatarId = Number(message.avatar_id || 0)
  try {
    if (message.email) { backend.email = message.email; localStorage.setItem(EMAIL_KEY + nextUid, message.email) }
    else { backend.email = localStorage.getItem(EMAIL_KEY + nextUid) || '' }
  } catch {}
  if (backend.avatarId > 0) ensureFileUrl(backend.avatarId)
  if (message.token) localStorage.setItem(TOKEN_KEY, message.token)
  notice(`欢迎回来，${backend.name}`)
  setTimeout(() => {
    showContacts()
    showFriendRequests()
    showCommunities()
    requestFileList()
  }, 120)
}

function contactsResponse(message) {
  backend.contacts = (message.contacts || []).map((item) => ({ uid: Number(item.uid), name: item.name || String(item.uid), isGroup: !!item.is_group, avatarId: Number(item.avatar_id || 0) }))
  for (const item of backend.contacts) {
    ensureConversation(item.isGroup ? 'group' : 'private', item.uid, item.name)
    if (!item.isGroup && item.avatarId > 0) ensureFileUrl(item.avatarId)
    if (item.isGroup) showGroupRequests(item.uid)
  }
}

function communitiesResponse(message) {
  backend.communities = (message.communities || []).map((item) => ({
    id: Number(item.community_id), name: item.name || String(item.community_id), description: item.description || '',
    role: item.my_role || 'member', avatarId: Number(item.avatar_id || 0), bannerId: Number(item.banner_id || 0),
  }))
  for (const community of backend.communities) if (community.role === 'owner') showCommunityRequests(community.id)
}

function channelsResponse(message) {
  const requestedCommunityId = channelQueue.shift()
  const communityId = Number(message.channels?.[0]?.community_id || requestedCommunityId || 0)
  const list = (message.channels || []).map((item) => ({ id: Number(item.channel_id), communityId: Number(item.community_id), name: item.name || String(item.channel_id), category: item.category || '一般' }))
  if (communityId) backend.channels[communityId] = list
  for (const channel of list) ensureConversation('channel', channel.id, channel.name)
}

function membersResponse(message) {
  const communityId = Number(memberQueue.shift() || 0)
  if (!communityId) return
  backend.members[communityId] = (message.members || []).map((item) => ({ uid: Number(item.user_uid), name: item.nickname || String(item.user_uid), role: item.role || 'member', avatarId: Number(item.avatar_id || 0) }))
  for (const item of backend.members[communityId]) if (item.avatarId > 0) ensureFileUrl(item.avatarId)
}

function avatarIdForUid(uid) {
  if (Number(uid) === Number(backend.uid)) return backend.avatarId
  const contact = backend.contacts.find((item) => !item.isGroup && Number(item.uid) === Number(uid))
  if (contact?.avatarId) return contact.avatarId
  return Object.values(backend.members).flat().find((item) => Number(item.uid) === Number(uid))?.avatarId || 0
}
function recordOf(item, conv) {
  const mine = Number(item.sender_UID) === Number(backend.uid)
  const reply = item.reply_to ? { id: String(item.reply_to.message_id), author: item.reply_to.sender_name || '成员', text: item.reply_to.content || '' } : null
  const avatarId = avatarIdForUid(item.sender_UID)
  return {
    id: String(item.message_id || `local-${++localSeq}`), messageId: Number(item.message_id || 0),
    mine, author: mine ? backend.name : (item.sender_name || conv.name), authorKey: mine ? 'self' : `member:${item.sender_UID}`,
    avatarImage: backend.fileUrls[avatarId] || '/default_avatar.png', text: item.content || '', time: String(item.timestamp || '').slice(11, 16), reply,
    files: item.is_file ? [{ fileId: String(item.file_id || ''), name: item.file_name || '文件', size: Number(item.file_size || 0), url: '' }] : null,
    senderUid: Number(item.sender_UID || 0),
  }
}

function regroup(conv) {
  conv.messages.sort((a, b) => (a.messageId || Number.MAX_SAFE_INTEGER) - (b.messageId || Number.MAX_SAFE_INTEGER))
  conv.messages = conv.messages.map((item, index, list) => ({ ...item, grouped: index > 0 && list[index - 1].authorKey === item.authorKey && !item.reply }))
  const ids = conv.messages.map((item) => item.messageId).filter((id) => id > 0)
  conv.oldestId = ids.length ? Math.min(...ids) : 0
}

function historyResponse(message) {
  const key = historyQueue.shift()
  const conv = backend.conversations[key] || ensureConversation('private', message.peer_id)
  const ids = new Set(conv.messages.map((item) => item.messageId))
  for (const item of message.messages || []) if (!ids.has(Number(item.message_id))) conv.messages.push(recordOf(item, conv))
  conv.loaded = true
  conv.loading = false
  conv.hasMore = !!message.has_more
  regroup(conv)
}

function liveMessage(message, kind, id) {
  const contact = backend.contacts.find((item) => Number(item.uid) === Number(id))
  const channel = Object.values(backend.channels).flat().find((item) => Number(item.id) === Number(id))
  const conv = ensureConversation(kind, id, contact?.name || channel?.name || message.sender_name || String(id))
  if (!conv.messages.some((item) => item.messageId === Number(message.message_id))) conv.messages.push(recordOf(message, conv))
  regroup(conv)
  if (Number(message.sender_UID) !== Number(backend.uid)) {
    if (kind === 'private' || kind === 'group') backend.unreadConversations[conv.key] = true
    notice(`${message.sender_name || conv.name} 发来新消息`)
  }
}

function deleteMessageLocal(id) {
  for (const conv of Object.values(backend.conversations)) conv.messages = conv.messages.filter((item) => item.messageId !== Number(id))
}

function parseFriendRequests(text) {
  const result = []
  for (const line of text.split('\n')) {
    const match = /^\[(.*?)\] \((\d+)\)(?::\s*(.*))?$/.exec(line.trim())
    if (match) result.push({ uid: match[2], name: match[1], note: match[3] || '' })
  }
  return result
}
function parseGroupMembers(text) {
  const result = []
  for (const line of text.split('\n')) {
    const match = /^(\d+):\s*(.*)$/.exec(line.trim())
    if (match) result.push({ uid: match[1], name: match[2], role: Number(match[1]) === Number(backend.uid) ? '成员' : '成员' })
  }
  return result
}
function parseGroupRequests(text) {
  const result = []
  for (const line of text.split('\n')) {
    const match = /^Requester UID: (\d+), Message:\s*(.*)$/.exec(line.trim())
    if (match) result.push({ uid: match[1], name: `用户 ${match[1]}`, note: match[2] || '' })
  }
  return result
}

function takePending(type, id = null) {
  const index = pendingText.findIndex((item) => item.type === type && (id == null || Number(item.id) === Number(id)))
  return index >= 0 ? pendingText.splice(index, 1)[0] : null
}
function scheduleFriendRefresh() {
  if (friendRefreshTimer) clearTimeout(friendRefreshTimer)
  friendRefreshTimer = setTimeout(() => {
    friendRefreshTimer = null
    if (!backend.uid) return
    showFriendRequests()
    showContacts()
  }, 80)
}
function systemMessage(content) {
  const text = content.trim()
  if (/Failed to load chat history|must be logged in to view chat history/i.test(text)) {
    const key = historyQueue.shift()
    const conv = backend.conversations[key]
    if (conv) conv.loading = false
  }
  if (/Failed to load channels|must be logged in to view channels/i.test(text)) {
    const communityId = channelQueue.shift()
    if (communityId) backend.channels[communityId] = []
  }
  if (/Failed to load members|must be logged in to view members/i.test(text)) {
    const communityId = memberQueue.shift()
    if (communityId) backend.members[communityId] = []
  }
  if (/You are not a member of this community/i.test(text)) {
    const channelCommunityId = channelQueue.shift()
    const memberCommunityId = channelCommunityId ? 0 : memberQueue.shift()
    if (channelCommunityId) backend.channels[channelCommunityId] = []
    if (memberCommunityId) backend.members[memberCommunityId] = []
  }
  if (/^\[.*?\] \(\d+\):|No pending friend requests|Failed to load friend requests/.test(text)) {
    takePending('friendRequests'); backend.friendRequests = parseFriendRequests(text); return
  }
  const groupRequestId = Number(/(?:Pending join requests for group|No pending join requests for group|Failed to retrieve group join requests).*?\[(\d+)\]/.exec(text)?.[1] || 0)
  if (groupRequestId) { takePending('groupRequests', groupRequestId); backend.groupRequests[groupRequestId] = parseGroupRequests(text); return }
  const communityRequestId = Number(/(?:Pending join requests for community|No pending join requests for community|Failed to retrieve community join requests).*?\[(\d+)\]/.exec(text)?.[1] || 0)
  if (communityRequestId) { takePending('communityRequests', communityRequestId); backend.communityRequests[communityRequestId] = parseGroupRequests(text); return }
  if (/Failed to retrieve group join requests/.test(text)) { const pending=takePending('groupRequests');if(pending)backend.groupRequests[pending.id]=[];return }
  if (/Failed to retrieve community join requests/.test(text)) { const pending=takePending('communityRequests');if(pending)backend.communityRequests[pending.id]=[];return }
  if (/^(\d+):/m.test(text) || /No members found|DB unavailable/.test(text)) {
    const pendingMembers = takePending('groupMembers')
    if (pendingMembers) { backend.groupMembers[pendingMembers.id] = parseGroupMembers(text); return }
  }
  const addedMember = /Member \[(\d+)\] added to community \[(\d+)\]/i.exec(text)
  const addedToCommunity = /You have been added to community \[(\d+)\]/i.exec(text)
  const failedCommunityAdd = /Failed to add member \((.*?)\)/i.exec(text)
  const registration = /Registration successful.*UID (\d+).*?or ([^\s.]+)\./.exec(text)
  if (addedMember) {
    const targetUid = Number(addedMember[1]); const communityId = Number(addedMember[2])
    const index = communityAddQueue.findIndex((item) => item.communityId === communityId && item.uid === targetUid)
    if (index >= 0) communityAddQueue.splice(index, 1)
    notice(`已将 UID ${targetUid} 添加到社区`)
    showMembers(communityId)
  }
  else if (addedToCommunity) {
    const communityId = Number(addedToCommunity[1])
    notice(`你已被添加到社区（ID：${communityId}）`)
    showCommunities()
  }
  else if (failedCommunityAdd) {
    communityAddQueue.shift()
    notice('添加社区成员失败：仅社区所有者可添加，或该用户已在社区中')
  }
  else if (registration) { try { localStorage.setItem(EMAIL_KEY + registration[1], registration[2]) } catch {} notice(`注册成功，UID：${registration[1]}，请登录`) }
  else if (/Avatar updated successfully/.test(text)) { if (pendingAvatarId > 0) backend.avatarId = pendingAvatarId; pendingAvatarId = 0; notice(text) }
  else if (/Failed to update avatar|Invalid file_id|Avatar file not found|not allowed for avatar/i.test(text)) { pendingAvatarId = 0; notice(text) }
  else if (/Logout successful/.test(text)) clearAccount()
  else if (/Token expired|Invalid or expired token/.test(text)) { localStorage.removeItem(TOKEN_KEY); clearAccount() }
  else if (/Message \[(\d+)\] deleted/.test(text)) deleteMessageLocal(Number(/Message \[(\d+)\]/.exec(text)?.[1]))
  else if (/Message sent\. Message ID: (\d+)/.test(text)) {
    const ack = messageAcks.shift()
    const id = Number(/Message ID: (\d+)/.exec(text)?.[1])
    const record = ack && backend.conversations[ack.key]?.messages.find((item) => item.id === ack.localId)
    if (record) {
      const duplicate = backend.conversations[ack.key].messages.find((item) => item !== record && item.messageId === id)
      if (duplicate) backend.conversations[ack.key].messages = backend.conversations[ack.key].messages.filter((item) => item !== record)
      else { record.messageId = id; record.id = String(id) }
      regroup(backend.conversations[ack.key])
    }
  } else notice(text)
  if (/Message cannot be empty|(?:UID|Group|Channel) \[\d+\] does not exist|not your friend|Failed to store message|must be logged in to send|not a member of this community/i.test(text) && messageAcks.length) {
    const ack = messageAcks.shift()
    const conv = ack && backend.conversations[ack.key]
    if (conv && ack.localId) { conv.messages = conv.messages.filter((item) => item.id !== ack.localId); regroup(conv) }
  }
  const renamed = /Name updated successfully\. Your new name is \[(.*?)\]\./.exec(text)
  if (renamed) backend.name = renamed[1]
  const emailUpdated = /Email updated successfully\. Your email is \[(.*?)\]\./.exec(text)
  if (emailUpdated) { backend.email = emailUpdated[1]; try { localStorage.setItem(EMAIL_KEY + backend.uid, backend.email) } catch {} }
  if (/wants to be friend with you|You are now friends with|rejected your friend request|removed from .*friend list|Failed to handle friend request/i.test(text)) scheduleFriendRefresh()
  if (/Group created|Group deleted|Group name updated|added to group|removed from group/.test(text)) setTimeout(showContacts, 200)
  if (/Join request accepted|Join request rejected/.test(text)) setTimeout(() => {
    for (const contact of backend.contacts) if (contact.isGroup) showGroupRequests(contact.uid)
    for (const community of backend.communities) if (community.role === 'owner') showCommunityRequests(community.id)
  }, 150)
  if (/Community created|Community deleted|Community updated|added to community|left community/.test(text)) setTimeout(showCommunities, 200)
  const changedCommunityId = Number(/(?:added to|removed from) community \[(\d+)\]/i.exec(text)?.[1] || 0)
  if (changedCommunityId && Number(backend.activeCommunityId) === changedCommunityId) setTimeout(() => showMembers(changedCommunityId), 200)
  if (/Channel created|Channel deleted|Channel updated/.test(text) && backend.activeCommunityId) setTimeout(() => showChannels(backend.activeCommunityId), 200)
  if (/File deleted/.test(text)) setTimeout(requestFileList, 150)
  handleTransferError(text)
}

function clearAccount() {
  interruptTransfers()
  releaseRuntimeUrls()
  if (friendRefreshTimer) clearTimeout(friendRefreshTimer)
  friendRefreshTimer = null
  pendingAvatarId = 0
  backend.uid = null; backend.name = ''; backend.email = ''; backend.avatarId = 0; backend.contacts = []; backend.communities = []
  backend.activeCommunityId = null; backend.channels = {}; backend.members = {}; backend.conversations = {}; backend.activeKey = ''; backend.unreadConversations = {}
  backend.friendRequests = []; backend.groupMembers = {}; backend.groupRequests = {}; backend.communityRequests = {}; backend.files = []
  clearTransportQueues()
  notice('已登出')
}

export function login(identity, password) {
  const value = String(identity || '').trim()
  return value.includes('@') ? send({ type: 'login', email: value, password }) : send({ type: 'login', UID: Number(value), password })
}
export function register(username, email, password) { return send({ type: 'register', username, email, password }) }
export function logout() {
  localStorage.removeItem(TOKEN_KEY)
  for (const task of Object.values(backend.transfers)) if (ownsTask(task) && task.transferId && ['上传中','下载中','校验中','正在续传'].includes(task.status)) send({ type: 'file_transfer_pause', transfer_id: Number(task.transferId) })
  interruptTransfers()
  send({ type: 'logout' })
  clearAccount()
}
export function showContacts() { return send({ type: 'show_contacts' }) }
export function showCommunities() { return send({ type: 'show_communities' }) }
export function showChannels(id) { if (send({ type: 'show_community_channels', community_id: Number(id) })) channelQueue.push(Number(id)) }
export function showMembers(id) { if (send({ type: 'show_community_members', community_id: Number(id) })) memberQueue.push(Number(id)) }
export function showFriendRequests() { if (send({ type: 'show_friend_requests' })) pendingText.push({ type: 'friendRequests' }) }
export function showGroupMembers(id) { if (send({ type: 'show_group_members', group_UID: Number(id) })) pendingText.push({ type: 'groupMembers', id: Number(id) }) }
export function showGroupRequests(id) { if (send({ type: 'show_group_requests', group_UID: Number(id) })) pendingText.push({ type: 'groupRequests', id: Number(id) }) }
export function showCommunityRequests(id) { if (send({ type: 'show_community_requests', community_id: Number(id) })) pendingText.push({ type: 'communityRequests', id: Number(id) }) }

export function selectConversation(kind, id, name) {
  const conv = ensureConversation(kind, id, name)
  backend.activeKey = conv.key
  if (!conv.loaded && !conv.loading) requestHistory(conv)
  return conv
}
export function markConversationRead(kind, id) { delete backend.unreadConversations[keyOf(kind, id)] }
export function requestHistory(conv = activeConversation.value, beforeId = 0) {
  if (!conv || conv.loading) return
  conv.loading = true
  const request = { type: 'history_request', peer_id: conv.id }
  if (beforeId) request.before_id = beforeId
  if (conv.kind === 'channel') request.is_channel = true
  if (send(request)) historyQueue.push(conv.key)
  else conv.loading = false
}

export function sendChat(text, replyId = 0, replySnapshot = null) {
  const conv = activeConversation.value
  if (!conv || !String(text).trim()) return false
  const payload = conv.kind === 'private' ? { type: 'private_chat', target_UID: conv.id, message: text }
    : conv.kind === 'group' ? { type: 'group_chat', target_UID: conv.id, message: text }
      : { type: 'channel_chat', target_UID: conv.id, message: text }
  if (Number(replyId) > 0) payload.reply_to_message_id = Number(replyId)
  if (!send(payload)) return false
  if (conv.kind === 'private') {
    const localId = `local-${++localSeq}`
    conv.messages.push({ id: localId, messageId: 0, mine: true, author: backend.name, authorKey: 'self', avatarImage: backend.fileUrls[backend.avatarId] || '/default_avatar.png', text, time: nowClock(), reply: replySnapshot && Number(replyId)>0 ? { id: String(replyId), author: replySnapshot.author || '成员', text: replySnapshot.text || '' } : null })
    messageAcks.push({ key: conv.key, localId })
    regroup(conv)
  } else messageAcks.push({ key: conv.key, localId: '' })
  return true
}
export function deleteMessage(id) { return send({ type: 'delete_message', message_id: Number(id) }) }
export function addFriend(email, note) { return send({ type: 'add_friend', email, apply_message: note || '' }) }
export function handleFriend(uid, accept) {
  const ok = send({ type: accept ? 'accept_friend' : 'reject_friend', sender_UID: Number(uid) })
  if (ok) scheduleFriendRefresh()
  return ok
}
export function removeFriend(uid) { const ok = send({ type: 'remove_friend', friend_UID: Number(uid) }); setTimeout(showContacts, 180); return ok }
export function setFriendRemark(uid, remark) { return send({ type: 'set_friend_remark', friend_UID: Number(uid), remark }) }
export function createGroup(name) { return send({ type: 'create_group', group_name: name }) }
export function joinGroup(id) { return send({ type: 'send_join_group', group_UID: Number(id) }) }
export function groupAdd(id, uid) { const ok=send({ type: 'group_add_client', group_UID: Number(id), target_user_UID: Number(uid) });if(ok)setTimeout(()=>showGroupMembers(id),200);return ok }
export function groupRemove(id, uid) { const ok=send({ type: 'group_delete_client', group_UID: Number(id), target_user_UID: Number(uid) });if(ok)setTimeout(()=>{showGroupMembers(id);showContacts()},200);return ok }
export function groupRename(id, name) { const ok=send({ type: 'modify_group_name', group_UID: Number(id), new_name: name });if(ok)setTimeout(showContacts,200);return ok }
export function groupDelete(id) { const ok=send({ type: 'delete_group', group_UID: Number(id) });if(ok)setTimeout(showContacts,200);return ok }
export function groupRole(id, uid, promote) { const ok=send({ type: 'modify_member_role', group_UID: Number(id), target_UID: Number(uid), promote: !!promote });if(ok)setTimeout(()=>showGroupMembers(id),200);return ok }
export function handleGroupRequest(id, uid, accept) { return send({ type: 'handle_join_request', group_UID: Number(id), requester_UID: Number(uid), accept }) }
export function createCommunity(name, description = '') { return send({ type: 'create_community', group_name: name, description }) }
export function joinCommunity(id, note = '') { return send({ type: 'join_community', community_id: Number(id), apply_message: note }) }
export function leaveCommunity(id) { return send({ type: 'leave_community', community_id: Number(id) }) }
export function deleteCommunity(id) { return send({ type: 'delete_community', community_id: Number(id) }) }
export function modifyCommunity(id, name, description, avatarId = 0, bannerId = 0) { return send({ type: 'modify_community', community_id: Number(id), group_name: name, description, community_avatar_id: Number(avatarId), community_banner_id: Number(bannerId) }) }
export function createChannel(communityId, name, category) { return send({ type: 'create_channel', community_id: Number(communityId), group_name: name, category }) }
export function deleteChannel(communityId, channelId) { return send({ type: 'delete_channel', community_id: Number(communityId), channel_id: Number(channelId) }) }
export function handleCommunityRequest(id, uid, accept) { return send({ type: 'handle_community_join_request', community_id: Number(id), requester_UID: Number(uid), accept }) }
export function communityAdd(id, uid) {
  const request = { communityId: Number(id), uid: Number(uid) }
  const ok = send({ type: 'community_add_member', community_id: request.communityId, target_user_UID: request.uid })
  if (ok) communityAddQueue.push(request)
  return ok
}
export function communityRemove(id, uid) { const ok=send({ type: 'community_remove_member', community_id: Number(id), target_user_UID: Number(uid) });if(ok)setTimeout(()=>showMembers(id),200);return ok }
export function communityRole(id, uid, promote) { const ok=send({ type: 'modify_community_member_role', community_id: Number(id), target_user_UID: Number(uid), promote });if(ok)setTimeout(()=>showMembers(id),200);return ok }
export function changeName(name) { return send({ type: 'change_name', new_name: name }) }
export function setEmail(email) { return send({ type: 'set_email', email }) }
export function setAvatar(fileId) { return send({ type: 'set_avatar', file_id: String(fileId) }) }
export function changeTheme(theme) { return send({ type: 'change_theme', theme }) }

const nowClock = () => { const now = new Date(); return `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}` }

// 文件协议：保持原文件工作台 DOM，只把数据与按钮切换为真实任务。
function normalizeFile(item = {}) {
  return { id: String(item.file_id || ''), fileId: String(item.file_id || ''), name: item.original_name || '文件', format: (item.original_name?.split('.').pop() || 'FILE').toUpperCase(), size: Number(item.size || 0), type: item.type || 'file', status: '已完成', mime: item.mime_type || 'application/octet-stream', createdAt: item.created_at || '', url: '' }
}
function fileListResponse(message) { backend.files = (message.files || []).map(normalizeFile) }
export function requestFileList() { return send({ type: 'file_list_request' }) }
export function deleteFile(fileId) { return send({ type: 'file_delete', file_id: String(fileId) }) }
export function ensureFileUrl(fileId) {
  const id = String(fileId || '')
  if (!id || id === '-1' || backend.fileUrls[id]) return backend.fileUrls[id] || ''
  const existing = Object.values(backend.transfers).find((task) => ownsTask(task) && task.silent && task.fileId === id)
  if (existing?.url) { replaceFileUrl(id, existing.url); return existing.url }
  if (existing && !['已完成','失败','已取消'].includes(existing.status)) return ''
  downloadFile({ fileId: id, id, name: `file-${id}`, size: 0, mime: 'application/octet-stream', format: 'FILE' }, { silent: true })
  return ''
}

function persistUploadsNow() {
  try {
    const tasks = backend.transferOrder.map((id) => backend.transfers[id]).filter((task) => task?.direction === 'upload' && task.transferId && !['已完成','已取消','失败'].includes(task.status)).map((task) => ({
      localId: task.localId, transferId: task.transferId, name: task.name, format: task.format,
      totalSize: task.totalSize, mime: task.mime, type: task.type, nextChunk: task.nextChunk,
      chunkSize: task.chunkSize, chunkCount: task.chunkCount, attachKey: task.attachKey,
      ownerUid: task.ownerUid,
    }))
    localStorage.setItem(RESUME_KEY, JSON.stringify(tasks))
  } catch {}
}
function persistUploads() {
  if (persistTimer) return
  persistTimer = setTimeout(() => { persistTimer = null; persistUploadsNow() }, 250)
}
function flushPersistUploads() {
  if (persistTimer) { clearTimeout(persistTimer); persistTimer = null }
  persistUploadsNow()
}
function restoreUploads() {
  try {
    const tasks = JSON.parse(localStorage.getItem(RESUME_KEY) || '[]')
    if (!Array.isArray(tasks)) return
    for (const task of tasks) {
      if (!task?.localId || !task.transferId || Number(task.ownerUid) <= 0 || backend.transfers[task.localId]) continue
      backend.transfers[task.localId] = { ...task, ownerUid: Number(task.ownerUid), direction: 'upload', size: Number(task.totalSize || 0), transferred: Math.min(Number(task.totalSize || 0), Number(task.nextChunk || 0) * Number(task.chunkSize || 0)), progress: task.totalSize ? Math.min(100, Math.round(Number(task.nextChunk || 0) * Number(task.chunkSize || 0) * 100 / Number(task.totalSize))) : 0, status: '等待选择原文件', paused: true, file: null }
      backend.transferOrder.push(task.localId)
    }
  } catch {}
}
function addTransfer(task) {
  backend.transfers[task.localId] = task
  backend.transferOrder = [task.localId, ...backend.transferOrder.filter((id) => id !== task.localId)]
  persistUploads()
  return task
}
function byTransferId(id) { return Object.values(backend.transfers).find((task) => ownsTask(task) && String(task.transferId || '') === String(id || '')) }
function applyTransfer(task, info = {}) {
  if (!task) return
  task.transferId = String(info.transfer_id || task.transferId || '')
  task.totalSize = Number(info.total_size || task.totalSize || 0)
  task.transferred = Number(info.transferred_size ?? task.transferred ?? 0)
  task.chunkSize = Number(info.chunk_size || task.chunkSize || 0)
  task.chunkCount = Number(info.chunk_count || task.chunkCount || 0)
  task.nextChunk = Number(info.next_chunk_index ?? task.nextChunk ?? 0)
  task.progress = task.totalSize ? Math.min(100, Math.round(task.transferred * 100 / task.totalSize)) : 0
  persistUploads()
}
export function uploadFile(file, options = false) {
  if (!file) return false
  if (!backend.uid) { notice('请先登录'); return false }
  const config = typeof options === 'object' ? options : { attach: !!options }
  const conv = config.attach ? activeConversation.value : null
  const localId = `upload-${Date.now()}-${++transferSeq}`
  addTransfer({ localId, direction: 'upload', name: file.name, format: (file.name.split('.').pop() || 'FILE').toUpperCase(), totalSize: file.size, size: file.size, mime: file.type || 'application/octet-stream', type: config.type || (file.type.startsWith('image/') ? 'image' : 'file'), status: '初始化', transferred: 0, progress: 0, nextChunk: 0, file, attachKey: conv?.key || '', paused: false, ownerUid: backend.uid, onComplete: config.onComplete || null })
  uploadQueue.push(localId)
  const ok = send({ type: 'file_upload_init', target_UID: conv?.kind === 'private' ? conv.id : 0, file_meta: { original_name: file.name, mime_type: file.type || 'application/octet-stream', size: file.size, type: config.type || (file.type.startsWith('image/') ? 'image' : 'file') } })
  if (!ok) { uploadQueue.pop(); backend.transfers[localId].status = '失败'; return false }
  return localId
}
function uploadReady(message) {
  let task = byTransferId(message.transfer_info?.transfer_id)
  if (!task) task = backend.transfers[uploadQueue.shift()]
  if (!task) return
  applyTransfer(task, message.transfer_info)
  if(task.cancelRequested){task.paused=true;task.status=send({type:'file_transfer_cancel',transfer_id:Number(task.transferId)})?'正在取消':'已中断';return}
  task.status = '上传中'; task.paused = false
  setTimeout(() => pumpUpload(task), 0)
}
async function pumpUpload(task) {
  if (!task?.file || task.pumping || task.paused) return
  task.pumping = true
  const token = Number(task.pumpToken || 0) + 1
  task.pumpToken = token
  try {
    while (task.pumpToken === token && !task.paused && task.nextChunk < task.chunkCount && backend.connected && ownsTask(task)) {
      while (ws?.readyState === WebSocket.OPEN && ws.bufferedAmount > 2 * 1024 * 1024 && task.pumpToken === token && !task.paused) {
        await new Promise((resolve) => setTimeout(resolve, 10))
      }
      if (task.pumpToken !== token || task.paused) break
      const index = task.nextChunk
      const offset = index * task.chunkSize
      const end = Math.min(task.totalSize, offset + task.chunkSize)
      const bytes = new Uint8Array(await task.file.slice(offset, end).arrayBuffer())
      if (task.pumpToken !== token || task.paused) break
      const sent = send({ type: 'file_chunk', meta: { type: 'file_chunk', transfer_id: task.transferId, filename: task.name, total_size: task.totalSize, chunk_index: index, chunk_count: task.chunkCount, chunk_size: bytes.byteLength, offset } }, bytes)
      if (!sent) { task.status = '已中断'; task.paused = true; break }
      task.nextChunk += 1; task.transferred = end; task.progress = Math.round(end * 100 / task.totalSize)
      persistUploads()
      await new Promise((resolve) => setTimeout(resolve, 0))
    }
    if (!task.paused && task.nextChunk >= task.chunkCount) {
      task.status = '校验中'
      if (!send({ type: 'file_upload_done', transfer_id: Number(task.transferId) })) { task.status = '已中断'; task.paused = true }
    }
  } catch (error) { task.status = '失败'; task.error = error.message }
  if (task.pumpToken === token) task.pumping = false
}
function uploadComplete(message) {
  const task = byTransferId(message.transfer_info?.transfer_id)
  const file = normalizeFile(message.file_meta)
  if (task) {
    applyTransfer(task, message.transfer_info); task.status = '已完成'; task.progress = 100; task.fileId = file.fileId
    if (task.file?.type?.startsWith('image/')) replaceFileUrl(file.fileId, createObjectUrl(task.file))
    if (task.attachKey) sendFileMessage(task.attachKey, file)
    if (task.type === 'avatar' && setAvatar(file.fileId)) pendingAvatarId = Number(file.fileId)
    if (task.onComplete) task.onComplete(file)
    task.file = null; task.onComplete = null
  }
  backend.files = [file, ...backend.files.filter((item) => item.fileId !== file.fileId)]
  persistUploads()
}
function sendFileMessage(key, file) {
  const conv = backend.conversations[key] || (() => { const [kind,id]=key.split(':');return ensureConversation(kind,Number(id),String(id)) })()
  if (!conv) return
  const payload = conv.kind === 'private' ? { type: 'private_chat', target_UID: conv.id } : conv.kind === 'group' ? { type: 'group_chat', target_UID: conv.id } : { type: 'channel_chat', target_UID: conv.id }
  Object.assign(payload, { message: '', is_file: true, file_id: file.fileId, file_name: file.name, file_size: file.size })
  if (!send(payload)) return
  if (conv.kind === 'private') {
    const localId = `local-${++localSeq}`
    conv.messages.push({ id: localId, messageId: 0, mine: true, author: backend.name, authorKey: 'self', avatarImage: backend.fileUrls[backend.avatarId] || '/default_avatar.png', text: '', time: nowClock(), reply: null, files: [{ fileId: file.fileId, name: file.name, size: file.size, url: '' }] })
    regroup(conv); messageAcks.push({ key: conv.key, localId })
  } else messageAcks.push({ key: conv.key, localId: '' })
}
export function downloadFile(file, options = {}) {
  if (!backend.uid) { notice('请先登录'); return false }
  const localId = `download-${Date.now()}-${++transferSeq}`
  addTransfer({ localId, direction: 'download', fileId: String(file.fileId || file.id || ''), name: file.name, format: file.format || 'FILE', totalSize: Number(file.size || 0), size: Number(file.size || 0), mime: file.mime || 'application/octet-stream', status: '初始化', transferred: 0, progress: 0, nextChunk: 0, chunks: [], paused: false, ownerUid: backend.uid, silent: !!options.silent })
  downloadQueue.push(localId)
  const ok=send({ type: 'file_download_init', file_id: String(file.fileId || file.id) })
  if(!ok){downloadQueue.pop();backend.transfers[localId].status='失败'}
  return ok
}
function downloadReady(message) {
  const task = backend.transfers[downloadQueue.shift()]
  if (!ownsTask(task)) return
  if(task.cancelRequested){applyTransfer(task,message.transfer_info);task.paused=true;task.status=send({type:'file_transfer_cancel',transfer_id:Number(task.transferId)})?'正在取消':'已中断';return}
  const meta = normalizeFile(message.file_meta || {})
  if (meta.name && meta.name !== '文件') task.name = meta.name
  if (meta.mime) task.mime = meta.mime
  if (meta.size) { task.totalSize = meta.size; task.size = meta.size }
  const oldChunks = task.restarting ? task.chunks : null
  const oldNext = task.restarting ? task.nextChunk : 0
  const oldTransferred = task.restarting ? task.transferred : 0
  applyTransfer(task, message.transfer_info); task.status = '下载中'; task.paused = false; task.chunkPending = false
  task.chunks = oldChunks || new Array(task.chunkCount); task.nextChunk = oldNext; task.transferred = oldTransferred; task.restarting = false
  pullChunk(task)
}
function pullChunk(task) {
  if (task.paused || task.chunkPending || task.nextChunk >= task.chunkCount) return
  task.chunkPending = true
  if (!send({ type: 'file_download_chunk', transfer_id: Number(task.transferId), chunk_index: task.nextChunk })) { task.chunkPending = false; task.status = '已中断'; task.paused = true }
}
function downloadChunk(message, data) {
  const task = byTransferId(message.meta?.transfer_id)
  if (!task || task.cancelRequested) return
  task.chunkPending = false
  const index = Number(message.meta?.chunk_index || 0)
  task.chunks[index] = new Uint8Array(data); task.nextChunk = index + 1
  task.transferred = Math.min(task.totalSize, Number(message.meta?.offset || 0) + data.byteLength); task.progress = Math.round(task.transferred * 100 / task.totalSize)
  if (task.nextChunk >= task.chunkCount) {
    const blob = new Blob(task.chunks, { type: task.mime }); task.url = createObjectUrl(blob); task.chunks = []; task.status = '已完成'; task.progress = 100
    if (task.silent) {
      replaceFileUrl(task.fileId, task.url)
      for (const conv of Object.values(backend.conversations)) {
        for (const record of conv.messages) {
          if (String(avatarIdForUid(record.senderUid)) === String(task.fileId)) record.avatarImage = task.url
        }
      }
      delete backend.transfers[task.localId]
      backend.transferOrder = backend.transferOrder.filter((id) => id !== task.localId)
    } else { const link = document.createElement('a'); link.href = task.url; link.download = task.name; link.click() }
  } else pullChunk(task)
}
export function pauseTransfer(task) { if (!ownsTask(task) || !task.transferId) return false; task.paused = true; task.status = '已暂停'; task.pumpToken = Number(task.pumpToken || 0) + 1; task.pumping = false; persistUploads(); const ok=send({ type: 'file_transfer_pause', transfer_id: Number(task.transferId) });if(!ok)task.status='已中断';return ok }
export function bindResumeFile(task, file) {
  if (!ownsTask(task) || !file) return false
  if (file.name !== task.name || file.size !== Number(task.totalSize)) { notice('请选择名称和大小与原文件一致的文件'); return false }
  task.file = file; task.paused = false; task.status = '正在续传'
  const ok = send({ type: 'file_upload_resume', transfer_id: Number(task.transferId) })
  if (!ok) { task.paused = true; task.status = '已中断' }
  return ok
}
export function resumeTransfer(task) {
  if (!ownsTask(task)) return false
  if (task.direction === 'download' && task.status === '已中断') {
    task.status = '初始化'; task.paused = false; task.restarting = true; downloadQueue.push(task.localId)
    const ok = send({ type: 'file_download_init', file_id: String(task.fileId) })
    if (!ok) { downloadQueue.splice(downloadQueue.lastIndexOf(task.localId), 1); task.status = '已中断'; task.paused = true; task.restarting = false }
    return ok
  }
  if (task.direction === 'upload' && ['已中断','等待选择原文件'].includes(task.status)) {
    if (!task.file) return false
    task.paused = false; task.status = '正在续传'
    const ok = send({ type: 'file_upload_resume', transfer_id: Number(task.transferId) })
    if (!ok) { task.paused = true; task.status = '已中断' }
    return ok
  }
  task.paused = false
  const ok = send({ type: 'file_transfer_resume', transfer_id: Number(task.transferId) })
  if (!ok) { task.paused = true; task.status = '已中断' }
  return ok
}
export function cancelTransfer(task) { if (!ownsTask(task)) return false; task.paused = true; task.cancelRequested = true; task.pumpToken = Number(task.pumpToken || 0) + 1; task.pumping = false;if(!task.transferId){task.status='正在取消';return true}task.status = '正在取消';persistUploads();const ok=send({ type: 'file_transfer_cancel', transfer_id: Number(task.transferId) });if(!ok)task.status='已中断';return ok }
export function dismissTransfer(task) {
  if (!ownsTask(task)) return
  if (task.url && !Object.values(backend.fileUrls).includes(task.url)) revokeObjectUrl(task.url)
  delete backend.transfers[task.localId]
  backend.transferOrder = backend.transferOrder.filter((id) => id !== task.localId)
  persistUploads()
}
function transferUpdate(message, forced = '') {
  const task = byTransferId(message.transfer_info?.transfer_id); if (!task) return
  applyTransfer(task, message.transfer_info)
  const serverStatus = forced || message.transfer_info?.status || ''
  if (serverStatus === 'paused') task.status = '已暂停'
  else if (serverStatus === 'cancelled') task.status = '已取消'
  else if (serverStatus === 'failed') task.status = '失败'
  else if (serverStatus === 'completed') task.status = '已完成'
  else task.status = task.direction === 'upload' ? '上传中' : '下载中'
  task.paused = !['上传中','下载中'].includes(task.status)
  if (['已完成','已取消','失败'].includes(task.status)) task.cancelRequested = false
  if (!task.paused) task.direction === 'upload' ? setTimeout(() => pumpUpload(task), 0) : pullChunk(task)
  persistUploads()
}
function interruptTransfers() {
  for (const task of Object.values(backend.transfers)) if (ownsTask(task) && !['已完成','已取消','失败'].includes(task.status)) {
    if (task.cancelRequested && !task.transferId) task.status = '已取消'
    else if (!task.transferId) { task.status='失败';task.error='传输初始化时连接中断，请重新开始' }
    else task.status = task.direction === 'upload' && !task.file ? '等待选择原文件' : '已中断'
    task.paused = true; task.chunkPending = false; task.pumpToken = Number(task.pumpToken || 0) + 1; task.pumping = false
  }
  persistUploads()
}
function handleTransferError(text) {
  if (!/Upload|upload|Transfer|transfer|File not found|file/.test(text)) return
  let task = null
  if (/Invalid upload request|Upload rejected|Failed to create upload/.test(text)) task = backend.transfers[uploadQueue.shift()]
  else if (/Invalid file_id|File not found|Failed to create download/.test(text)) task = backend.transfers[downloadQueue.shift()]
  else task = Object.values(backend.transfers).find((item) => ownsTask(item) && !['已完成','已取消','失败'].includes(item.status))
  if (task && /failed|rejected|not found|incomplete|Unknown|Invalid/i.test(text)) { task.status = '失败'; task.error = text; task.paused = true; task.pumpToken=Number(task.pumpToken||0)+1;task.pumping=false;persistUploads() }
}

export const api = {
  connect, disconnect, login, register, logout, showContacts, showCommunities, showChannels, showMembers,
  showFriendRequests, showGroupMembers, showGroupRequests, showCommunityRequests,
  selectConversation, markConversationRead, requestHistory, sendChat, deleteMessage,
  addFriend, handleFriend, removeFriend, setFriendRemark, createGroup, joinGroup, groupAdd, groupRemove, groupRename, groupDelete, groupRole, handleGroupRequest,
  createCommunity, joinCommunity, leaveCommunity, deleteCommunity, modifyCommunity, createChannel, deleteChannel, handleCommunityRequest, communityAdd, communityRemove, communityRole,
  changeName, setEmail, setAvatar, changeTheme, ensureFileUrl,
  uploadFile, downloadFile, pauseTransfer, bindResumeFile, resumeTransfer, cancelTransfer, dismissTransfer, requestFileList, deleteFile,
  activeConversation,
}

restoreUploads()
