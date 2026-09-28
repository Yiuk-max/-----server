<script setup>
import { ref, nextTick, computed, h, reactive, watch, onMounted, onUnmounted, withDirectives, vModelText } from 'vue'
import { DEFAULT_AVATAR, DEFAULT_GROUP_AVATAR, DEFAULT_COMM_AVATAR, QUICK_EMOJIS, PICKER_EMOJIS, AVATAR_TONES } from './data/ui.js'
import { EMOJI_CATEGORIES } from './data/emoji.js'
import { Icon } from './components/icons.js'
import { FileCover } from './components/file-cover.js'
import { backend, api } from './backendClient.js'
import bakaAudioUrl from './assets/baka.mp3?inline'
const active = ref('💬 チャット環状線')
const activePage = ref('dm')
const communities = ref([])
const selectedCommunity = ref('')
const currentCommunity = computed(() => communities.value.find(item=>item.name===selectedCommunity.value))
const communityMenu = ref(false)
const testMenu = ref(null)
const communityContextMenu = ref(null)
const communitySettingsOpen = ref(false)
const communitySettingsName = ref('')
const communitySettingsDescription = ref('')
const communityChannels = ref({})
const communityChannelCategories = ref({})
const newCommunityChannel = ref('')
const dissolveConfirm = ref(false)
const createCommunityOpen = ref(false)
const newCommunityName = ref('')
const selectedDm = ref('')
const friendFilter = ref('全部')
const friendSearchQuery = ref('')
const friendMoreMenu = ref('')
const createGroupFormOpen = ref(false)
const dmQuickMenu = ref(false)
const friendRequestModal = ref(false)
const demoLoginOpen = ref(true)
const demoHubOpen = ref(false)
const demoTab = ref('账号')
const demoAuthMode = ref('登录')
const demoSignedIn = ref(false)
const demoAuth = reactive({ identity: '', password: '', username: '', email: '' })
const demoUid = ref('')
const demoNickname = ref('未登录')
const demoNicknameDraft = ref('')
const demoEmail = ref('')
const avatarUploadInput = ref(null)
const demoFriends = ref([])
const demoFriendRequests = ref([])
const demoGroups = ref([])
const demoGroupRequests = ref([])
const demoFiles = ref([])
const demoCommunityRequests = ref([])
const demoForm = reactive({ name: '', email: '', note: '', groupName: '', memberUid: '', communityName: '', joinCode: '' })
const demoSearch = ref('')
const demoFilesInput = ref(null)
const resumeFileInput = ref(null)
const resumeTransferTarget = ref(null)
const activeTransfer = computed(() => backend.transferOrder.map((id) => backend.transfers[id]).find((task) => task && !task.silent && (!task.ownerUid || Number(task.ownerUid)===Number(backend.uid)) && !['已完成','已取消','失败'].includes(task.status)) || null)
const profileMenuOpen = ref(false)
const profileStatusMenu = ref(false)
const profileAccountMenu = ref(false)
const currentStatus = ref('在线')
const memberProfile = ref(null)
const dmContacts = reactive([])
const message = ref('')
const closeDm = (contact) => { if (selectedDm.value===contact.name) { selectedDm.value=''; activePage.value='dm' } }
const conversationKindOf = (contact) => contact.kind === '群聊' ? 'group' : 'private'
const isDmUnread = (contact) => !!backend.unreadConversations[`${conversationKindOf(contact)}:${Number(contact.uid)}`]
const openDm = (contact) => {
  selectedDm.value = contact.name
  activePage.value = 'dm-chat'
  const kind = conversationKindOf(contact)
  api.selectConversation(kind, contact.uid, contact.name)
  api.markConversationRead(kind, contact.uid)
}
const dmAvatarSrc = (name) => { const contact=dmContacts.find((item)=>item.name===name);return contact?.kind==='群聊'?DEFAULT_GROUP_AVATAR:avatarByUid(contact?.uid) }
const myAvatarSrc = computed(() => backend.fileUrls[backend.avatarId] || DEFAULT_AVATAR)
const avatarByUid = (uid) => { const contact=backend.contacts.find(item=>Number(item.uid)===Number(uid));return backend.fileUrls[contact?.avatarId] || DEFAULT_AVATAR }
// 正在回复的目标消息：发送时会作为引用行挂到新消息上（Discord 的回复行为）
const replyTarget = ref(null)
// 私信 / 群聊保持原 MessageRow 和像素布局，数据改为后端会话。
const dmThread = computed(() => (api.activeConversation.value?.messages || []).filter((record) => !isReactionMessage(record)))
const backendChannelThread = computed(() => api.activeConversation.value?.kind === 'channel' ? api.activeConversation.value.messages : [])
const messageReactions = computed(() => {
  const map = {}
  for (const record of (api.activeConversation.value?.messages || [])) {
    if (!isReactionMessage(record)) continue
    const targetId = String(record.reply.id)
    const emoji = String(record.text).trim()
    if (!map[targetId]) map[targetId] = []
    let reaction = map[targetId].find((item) => item.emoji === emoji)
    if (!reaction) { reaction = { emoji, count: 0, mine: false, users: [] }; map[targetId].push(reaction) }
    reaction.count += 1
    reaction.users.push(record.author)
    if (record.mine) reaction.mine = true
  }
  return map
})
const contextMenu = ref(null)
const emojiPicker = ref(null)
const reactionDetails = ref(null)
const toast = ref('')
const micMuted = ref(true)
const headphoneOn = ref(false)
const unreadBanner = ref(false)
// —— Baka 音效（内置 Baka.mp3）——
const bakaOnSend = ref(true)
const bakaOnReceive = ref(true)
const bakaOnLogo = ref(true)
let bakaAudio = null
const playBaka = (reason = 'logo') => {
  const allowed = reason === 'send' ? bakaOnSend.value : reason === 'receive' ? bakaOnReceive.value : bakaOnLogo.value
  if (!allowed || typeof Audio === 'undefined') return
  try {
    if (!bakaAudio) { bakaAudio = new Audio(bakaAudioUrl); bakaAudio.volume = 0.65 }
    bakaAudio.currentTime = 0
    const played = bakaAudio.play()
    if (played?.catch) played.catch(() => {})
  } catch {}
}
// 只有真正收到消息时播放（切频道 / 切社区不播放）
try {
  const saved = JSON.parse(localStorage.getItem('discord-baka-v1') || 'null')
  if (saved) {
    bakaOnSend.value = saved.send !== false
    bakaOnReceive.value = saved.receive !== false
    bakaOnLogo.value = saved.logo !== false
  }
} catch {}
watch([bakaOnSend, bakaOnReceive, bakaOnLogo], () => {
  try { localStorage.setItem('discord-baka-v1', JSON.stringify({ send: bakaOnSend.value, receive: bakaOnReceive.value, logo: bakaOnLogo.value })) } catch {}
})
const composerInput = ref(null)
const fileInput = ref(null)
const uploads = ref([])
const attachmentMenu = ref(null)
const emojiPanelOpen = ref(false)
const emojiPanelPos = ref(null)
const emojiPanelTarget = ref(null)
const emojiCategory = ref(EMOJI_CATEGORIES[0]?.name || '')
const activeEmojiCategory = computed(() => EMOJI_CATEGORIES.find((item) => item.name === emojiCategory.value) || EMOJI_CATEGORIES[0])
const inviteModal = ref(false)
const joinModal = ref(false)
const settingsOpen = ref(false)
const profileEditorOpen = ref(false)
const profileBackgroundMenu = ref(false)
const profileBackground = ref('#454347')
const profileBackgroundImage = ref('')
const selectedSetting = ref('账户')
const selectedAppearance = ref('主题')
const themeChoice = ref('dark')
const syncTheme = ref(true)
const shareTheme = ref(false)
const serverTheme = ref('使用服务器主题')
const friendSearch = ref('')
const inviteUrl = ref('功能暂未开发')
const joinCode = ref('')
const invitedFriends = ref([])
const filteredFriends = computed(() => {
  const memberUids = new Set((backend.members[currentCommunity.value?.id] || []).map((member) => Number(member.uid)))
  return demoFriends.value
    .filter((friend) => !memberUids.has(Number(friend.uid)) && `${friend.name} ${friend.uid}`.toLowerCase().includes(friendSearch.value.toLowerCase()))
    .map((friend) => ({ ...friend, handle: String(friend.uid), color: '', avatar: friend.name?.[0] || '?' }))
})
const filteredDemoFriends = computed(() => demoFriends.value.filter(friend => `${friend.name} ${friend.remark} ${friend.uid} ${friend.email}`.toLowerCase().includes(demoSearch.value.toLowerCase())))
const filteredFriendDirectory = computed(() => demoFriends.value.filter(friend => `${friend.name} ${friend.remark} ${friend.uid}`.toLowerCase().includes(friendSearchQuery.value.toLowerCase()) && (friendFilter.value !== '在线' || friend.online)))
const demoCommunityMemberList = ref([])
const demoSelectedGroup = computed(() => demoGroups.value.find(group => group.name === selectedDm.value) || demoGroups.value[0])
// 社区成员右侧栏
const memberPanelOpen = ref(false)
const communityMemberGroups = computed(() => {
  const list = (demoCommunityMemberList.value || []).map((member) => ({ ...member, online: false }))
  return [['owner', '所有者'], ['admin', '管理员'], ['member', '成员']]
    .map(([role, label]) => ({ label, members: list.filter(member => member.role === role) }))
    .filter(group => group.members.length)
})
const demoTabIcon = (item) => ({账号:'◉',联系人:'♧',群聊:'◌',社区:'◆',文件:'▤'}[item] || '•')
const MessageActions = () => h('div', { class: 'message-actions' }, [
  ...QUICK_EMOJIS.map((emoji) => h('button', { 'data-action': 'react', 'data-emoji': emoji, title: `添加 ${emoji}` }, emoji)),
  h('button', { 'data-action': 'picker', title: '添加反应' }, '☻'),
  h('button', { 'data-action': 'reply', title: '回复' }, '↶'),
  h('button', { 'data-action': 'forward', title: '转发' }, '↱'),
  h('button', { 'data-action': 'more', title: '更多' }, h('svg', { viewBox: '0 0 24 24', class: 'more-dots' }, [h('circle', { cx: '5', cy: '12', r: '1.7' }), h('circle', { cx: '12', cy: '12', r: '1.7' }), h('circle', { cx: '19', cy: '12', r: '1.7' })]))
])
// 消息行：把参考图里的模块（回复引用行 / 服务器标签胶囊 / 共同服务器 M 标 / 链接卡片）
// 统一收进一个函数式组件，mainHistory 与 channelHistory 共用，避免两处模板各写一遍。
const tagPill = (tag, key) => h('span', { class: 'tag', key: key || tag.label }, [
  tag.icon ? h('span', { class: 'tag-icon' }, tag.icon) : null,
  tag.label
])
// 反应行：芯片（emoji + 计数）+ 末尾的「添加反应」按钮（对齐参考图：按钮一直显示，点击开 emoji 选择器）
const ReactionRow = (props) => h('div', { class: 'reaction-row' }, [
  ...(props.reactions || []).map((reaction) => h('button', {
    class: ['reaction-chip', { mine: reaction.mine }], key: reaction.emoji,
    'data-action': 'react', 'data-emoji': reaction.emoji, title: `${reaction.emoji} ${reaction.count}`
  }, [h('span', { class: 'reaction-emoji' }, reaction.emoji), h('span', {}, reaction.count)])),
  h('button', {
    class: 'reaction-add', key: 'reaction-add', type: 'button',
    'data-action': 'picker', title: '添加反应', 'aria-label': '添加反应'
  }, h('svg', { viewBox: '0 0 24 24', 'aria-hidden': 'true' }, [
    h('circle', { cx: '11', cy: '12', r: '8' }),
    h('path', { d: 'M7.6 13.8s1.4 2 3.4 2 3.4-2 3.4-2' }),
    h('path', { d: 'M8.7 9.6h.01M13.3 9.6h.01' }),
    h('path', { d: 'M18.6 3.4v5M21.1 5.9h-5' })
  ]))
])
// —— 输入框：社区频道与私信两个 <main> 各写了一份几乎相同的 composer，收敛成一个函数式组件 ——
// 闭包直接读写 message / uploads / replyTarget / attachmentMenu，并复用 fileInput / composerInput 两个模板 ref；
// 消息输入沿用 vModelText 指令，保留中文输入法（IME）的组合输入行为。
const Composer = (props) => {
  const reply = replyTarget.value && replyTarget.value.conversationKey === props.conversationKey ? replyTarget.value : null
  const parts = []
  if (reply) {
    parts.push(h('div', { class: 'composer-reply' }, [
      h('span', { class: 'composer-reply-text' }, ['正在回复 ', h('b', { class: reply.tone }, `@${reply.author}`)]),
      h('span', { class: 'composer-reply-preview' }, reply.text),
      h('button', { type: 'button', class: 'composer-reply-cancel', 'aria-label': '取消回复', title: '取消回复', onClick: () => { replyTarget.value = null } }, '×')
    ]))
  }
  if (uploads.value.length) {
    parts.push(h('div', { class: 'attachment-tray' }, uploads.value.map((item) => {
      const task = item.transferLocalId ? backend.transfers[item.transferLocalId] : null
      const progress = task ? Math.max(0, Math.min(100, Math.round(task.progress || 0))) : null
      const statusText = task ? `${item.format} · ${formatFileSize(item.size)} · ${task.status}${progress == null ? '' : ` ${progress}%`}` : `${item.format} · ${formatFileSize(item.size)}`
      return h('div', { key: item.id, class: 'upload-card' }, [
        item.preview
          ? h('img', { class: 'upload-image', src: item.preview, alt: item.name })
          : h('div', { class: 'upload-file-preview' }, [h(FileCover, { name: item.name }), h('b', null, item.format), h('small', null, formatFileSize(item.size))]),
        h('div', { class: 'upload-caption' }, [
          h('b', null, item.name),
          h('small', null, statusText),
          progress != null ? h('div', { style: 'height:3px;margin-top:4px;border-radius:2px;background:#33353d;overflow:hidden' }, [
            h('i', { style: `display:block;height:100%;border-radius:2px;background:#5865f2;width:${progress}%` })
          ]) : null
        ]),
        h('button', { type: 'button', class: 'upload-remove', 'aria-label': `移除 ${item.name}`, onClick: () => removeUpload(item.id) }, '×')
      ])
    })))
  }
  parts.push(h('div', { class: 'composer-line' }, [
    h('button', { type: 'button', class: 'add-attachment', 'aria-label': '更多附件选项', title: '更多附件选项', onClick: (e) => { e.stopPropagation(); toggleAttachmentMenu(e) } }, '＋'),
    h('input', { ref: fileInput, class: 'file-input', type: 'file', multiple: true, onChange: handleFiles }),
    withDirectives(h('textarea', {
      ref: composerInput,
      rows: 1,
      placeholder: props.placeholder,
      'onUpdate:modelValue': (value) => { message.value = value },
      onKeydown: (event) => {
        if (event.key === 'Enter' && !event.shiftKey && !event.isComposing && event.keyCode !== 229) {
          event.preventDefault()
          submit()
        }
      },
      onInput: (event) => resizeComposer(event.target),
      style: 'flex:1;min-width:30px;border:0;outline:0;background:transparent;color:var(--text-1);font-size:15px;font-family:inherit;resize:none;height:24px;line-height:24px;padding:0;margin:0'
    }), [[vModelText, message.value]]),
    h('div', { class: 'composer-tools' }, [
      h('button', { type: 'button', class: 'tool-button', 'aria-label': '礼物', title: '礼物', onClick: () => handleAttachmentAction('gift') }, [h(Icon, { name: 'gift' })]),
      h('button', { type: 'button', class: 'tool-button tool-gif', 'aria-label': 'GIF', title: 'GIF', onClick: () => handleAttachmentAction('gif') }, 'GIF'),
      h('button', { type: 'button', class: 'tool-button', 'aria-label': '贴纸', title: '贴纸', onClick: () => handleAttachmentAction('sticker') }, [h(Icon, { name: 'sticker' })]),
      h('button', { type: 'button', class: 'tool-button', 'aria-label': '表情', title: '表情', onClick: (e) => toggleEmojiPanel(e) }, [h(Icon, { name: 'smile' })])
    ])
  ]))
  if (emojiPanelOpen.value) {
    const cat = activeEmojiCategory.value
    parts.push(h('div', {
      class: 'emoji-panel',
      onClick: (e) => e.stopPropagation(),
      style: `position:fixed;left:${emojiPanelPos.value?.left ?? 8}px;top:${emojiPanelPos.value?.top ?? 8}px;width:420px;height:360px;z-index:90;display:flex;flex-direction:column;background:#2b2d31;border:1px solid #1e1f22;border-radius:8px;box-shadow:0 8px 16px rgba(0,0,0,.35);padding:8px`
    }, [
      h('div', { style: 'display:flex;gap:2px;flex-wrap:wrap;border-bottom:1px solid #1e1f22;padding-bottom:6px;margin-bottom:6px' },
        EMOJI_CATEGORIES.map((item) => h('button', {
          key: item.name,
          type: 'button',
          title: item.name,
          onClick: () => { emojiCategory.value = item.name },
          style: `width:32px;height:32px;display:grid;place-items:center;border-radius:6px;border:0;cursor:pointer;font-size:18px;line-height:1;background:${emojiCategory.value === item.name ? '#5865f2' : 'transparent'}`
        }, item.icon))
      ),
      h('div', { class: 'emoji-panel-scroll', style: 'display:grid;grid-template-columns:repeat(8,1fr);gap:2px;overflow-y:auto;flex:1;align-content:start' },
        cat.emojis.map((emoji) => h('button', {
          key: emoji,
          type: 'button',
          title: emoji,
          onClick: () => { if (emojiPanelTarget.value) sendReaction(emoji, emojiPanelTarget.value); else appendEmoji(emoji) },
          style: 'background:transparent;border:0;cursor:pointer;font-size:24px;line-height:1;height:38px;border-radius:4px;padding:0'
        }, emoji))
      )
    ]))
  }
  if (attachmentMenu.value) {
    parts.push(h('div', { class: 'attachment-menu', style: { left: `${attachmentMenu.value.x}px`, top: `${attachmentMenu.value.y}px` }, onClick: (e) => e.stopPropagation() }, [
      h('button', { onClick: () => handleAttachmentAction('upload') }, [h(Icon, { name: 'upload', class: 'upload-menu-icon' }), h('b', null, '上传文件')])
    ]))
  }
  return h('form', { class: ['composer', { 'has-uploads': uploads.value.length }], onSubmit: (e) => { e.preventDefault(); submit() } }, parts)
}
// 已撤回的消息：按消息 id 记录（作者 / 是否自己），渲染成一行系统提示，静态与动态消息通用
const recalled = ref({})
const isEmojiOnlyMessage = (text) => {
  const trimmed = String(text || '').trim()
  if (!trimmed) return false
  const emojis = trimmed.match(/\p{Extended_Pictographic}/gu) || []
  if (emojis.length < 1 || emojis.length > 6) return false
  const rest = trimmed.replace(/\p{Extended_Pictographic}/gu, '').replace(/\s+/g, '').replace(/[\uFE0F\u200D\u20E3]/g, '')
  return rest.length === 0
}
const isReactionMessage = (record) => !!record?.reply?.id && isEmojiOnlyMessage(record.text)
const MessageRow = (props) => {
  const record = props.record
  const recall = recalled.value[record.id]
  if (recall) {
    return h('article', { class: 'message system-message recalled-message', 'data-message-id': record.id, 'data-recalled': '1' }, [
      h('div', { class: 'system-row' }, [h('span', { class: 'system-glyph' }, '⟲'), h('b', {}, recall.mine ? '你撤回了一条消息' : `${recall.author} 撤回了一条消息`)])
    ])
  }
  if (record.system) {
    return h('article', { class: 'message system-message', 'data-message-id': record.id }, [
      h('div', { class: 'system-row' }, [h('span', { class: 'system-glyph' }, '↰'), h('b', {}, record.system)])
    ])
  }
  const content = []
  if (record.reply) {
    content.push(h('div', {
      class: ['message-reply', { jumpable: !!record.reply.id }],
      'data-action': record.reply.id ? 'jump' : null,
      'data-target': record.reply.id || null,
      title: record.reply.id ? '跳到原消息' : null
    }, [
      h('span', { class: ['reply-avatar', record.reply.avatar] }, record.reply.avatarImage
        ? h('img', { src: record.reply.avatarImage, alt: record.reply.author })
        : (record.reply.avatarText || record.reply.author[0])),
      h('span', { class: ['reply-mention', record.reply.tone] }, `@${record.reply.author}`),
      record.reply.tag ? tagPill(record.reply.tag, 'reply-tag') : null,
      h('span', { class: 'reply-text' }, record.reply.text)
    ]))
  }
  content.push(h('div', { class: 'meta' }, [
    h('b', { class: record.tone || '' }, record.author),
    ...(record.tags || []).map((tag) => tagPill(tag)),
    record.mutual ? h('span', { class: 'mutual-badge', title: '共同服务器' }, 'M') : null,
    h('time', {}, record.time)
  ]))
  if (record.text) {
    const jumbo = isEmojiOnlyMessage(record.text)
    content.push(h('div', { class: 'message-content' }, h('p', { style: jumbo ? 'font-size:44px;line-height:1.15;letter-spacing:2px' : null }, record.text)))
  }
  if (record.embed) {
    content.push(h('a', { class: 'message-embed', href: record.embed.url || '#', target: '_blank', rel: 'noreferrer', style: `--embed-accent:${record.embed.accent}` }, [
      h('span', { class: 'embed-body' }, [
        h('b', { class: 'embed-title' }, record.embed.title),
        h('span', { class: 'embed-desc' }, record.embed.desc),
        h('span', { class: 'embed-url' }, record.embed.url)
      ]),
      record.embed.thumb ? h('span', { class: 'embed-thumb' }, record.embed.thumb) : null
    ]))
  }
  if (props.reactions?.length) content.push(h(ReactionRow, { reactions: props.reactions }))
  const images = props.images?.length ? props.images : record.images
  if (images?.length) {
    content.push(h('div', { class: 'message-images' }, images.map((img) => h('a', {
      href: img.url, target: '_blank', rel: 'noreferrer', title: img.name, key: img.url
    }, h('img', { src: img.url, alt: img.name })))))
  }
  const files = props.files?.length ? props.files : record.files
  if (files?.length) {
    content.push(h('div', { class: 'message-files' }, files.map((file) => h('div', {
      class: 'message-file', key: file.fileId || file.url || file.name, 'data-url': file.url || '', 'data-name': file.name, 'data-file-id': file.fileId || ''
    }, [
      h(FileCover, { name: file.name }),
      h('span', { class: 'message-file-body' }, [
        h('a', { href: file.url || '#', download: file.name, title: file.name }, file.name),
        h('small', {}, formatFileSize(file.size || 0))
      ]),
      h('button', { type: 'button', class: 'message-file-download', 'data-action': 'download', 'data-url': file.url || '', 'data-name': file.name, 'data-file-id': file.fileId || '', 'data-size': file.size || 0, title: '下载', 'aria-label': `下载 ${file.name}` },
        h('svg', { viewBox: '0 0 24 24', 'aria-hidden': 'true' }, h('path', { d: 'M12 4v11m-5-5 5 5 5-5M5 20h14' })))
    ]))))
  }
  return h('article', { class: ['message', { grouped: record.grouped, 'has-reply': !!record.reply }], 'data-message-id': record.id }, [
    h('div', { class: ['avatar', record.avatar, { 'has-image': !!record.avatarImage }] }, record.avatarImage
      ? h('img', { src: record.avatarImage, alt: record.author })
      : record.author[0]),
    h('div', { class: 'message-body' }, content),
    h(MessageActions)
  ])
}
const groups = computed(() => {
  const community = currentCommunity.value
  const list = community ? (backend.channels[community.id] || []) : []
  const categories = new Map()
  for (const channel of list) {
    const title = channel.category || '一般'
    if (!categories.has(title)) categories.set(title, [])
    categories.get(title).push(channel.name)
  }
  return [...categories].map(([title, channels]) => ({ title, channels }))
})
// 分区可以点击收起 / 展开
const collapsedCategories = ref({})
const customChannelsCollapsed = ref(false)
const categoryKey = (group) => `${selectedCommunity.value}::${group.title}`
const isCategoryCollapsed = (group) => !!collapsedCategories.value[categoryKey(group)]
const toggleCategory = (group) => { const key = categoryKey(group); collapsedCategories.value = { ...collapsedCategories.value, [key]: !collapsedCategories.value[key] } }
// 服务器设置：头像 / 背景图 / 名字 / 个人在服务器内的展示名
const communityNickname = ref('')
const communityAvatarInput = ref(null)
const communityBannerInput = ref(null)
const hideMutedChannels = ref(false)
// 统一的重命名弹窗：取消返回 null，否则返回去除首尾空白后的输入（是否允许空值由调用方决定）
const promptRename = (title, current = '') => {
  const next = window.prompt(title, current)
  return next === null ? null : next.trim()
}
const editCommunityNickname = () => { communityContextMenu.value = null; notDeveloped('社区内个人展示名') }
const setCommunityAsset = (kind) => (kind === 'avatar' ? communityAvatarInput.value : communityBannerInput.value)?.click()
// 读取图片文件为 objectURL（用完记得 revoke）；三个上传入口（社区头像/背景、账号头像）共用
const readImageFile = (event) => { const file = event.target.files?.[0]; event.target.value=''; return file || null }
const handleCommunityAsset = (kind, event) => {
  readImageFile(event)
  notDeveloped(kind === 'banner' ? '社区背景图（服务端列表暂未返回现有图片 ID）' : '社区头像（服务端列表暂未返回现有图片 ID）')
}
const selectCommunity = (community) => {
  communityContextMenu.value = null
  communityMenu.value = false
  testMenu.value = null
  selectedCommunity.value = community.name
  activePage.value = 'community'
  backend.activeCommunityId = community.id
  backend.activeKey = ''
  active.value = '暂无频道'
  api.showChannels(community.id)
  api.showMembers(community.id)
  api.showCommunityRequests(community.id)
  const first = (backend.channels[community.id] || [])[0]
  if (first) switchChannel(first.name)
}
const channelHistory = computed(() => backendChannelThread.value.filter((record) => !isReactionMessage(record)))
const typingUsers = computed(() => [])
// 未读横幅：进入页面时按当前时间倒推 52 分钟，不再是写死的 15:26 / 12 条
const unreadBannerInfo = computed(() => {
  const since = new Date(Date.now() - 52 * 60000)
  return { time: `${String(since.getHours()).padStart(2, '0')}:${String(since.getMinutes()).padStart(2, '0')}`, count: 12 }
})
const switchChannel = async (channel) => {
  active.value = channel
  const community = currentCommunity.value
  const target = (backend.channels[community?.id] || []).find((item) => item.name === channel)
  if (target) api.selectConversation('channel', target.id, target.name)
  await nextTick()
  const log = document.querySelector('.message-scroll')
  if (log) log.scrollTop = log.scrollHeight
}
const createCommunity = () => {
  const name = newCommunityName.value.trim()
  if (!name) return
  api.createCommunity(name)
  newCommunityName.value = ''
  createCommunityOpen.value = false
}
const openCommunityContext = (event, community) => {
  const width = 220
  const height = 190
  communityMenu.value = false
  communityContextMenu.value = {name:community.name,x:Math.min(event.clientX,window.innerWidth-width-8),y:Math.min(event.clientY,window.innerHeight-height-8)}
}
// 竖栏底部「测试」按钮：菜单贴按钮显示在竖栏右侧（fixed 定位，与其它浮层同一套关闭逻辑）
// 用 bottom 定位而非按固定高度估算 top；下沿抬到底部账号栏之上，否则菜单最后一项会被账号栏（fixed z-index:60）压住。
const openTestMenu = (event) => {
  if (testMenu.value) { testMenu.value = null; return }
  communityMenu.value = false
  communityContextMenu.value = null
  const rect = event.currentTarget.getBoundingClientRect()
  const accountBar = document.querySelector('.account-bar')?.getBoundingClientRect()
  const minBottom = accountBar ? Math.min(window.innerHeight - accountBar.top, window.innerHeight - accountBar.bottom) : 0
  testMenu.value = {
    x: Math.min(rect.right + 10, window.innerWidth - 276),
    bottom: Math.max(8, window.innerHeight - rect.bottom, minBottom)
  }
}
// 任意空白处点击 / Esc：关闭竖栏的两个菜单（点击菜单、右键菜单）与私信 ＋ 菜单
const MENU_KEEP_SELECTOR = '.rail-action-wrap,.community-menu,.community-context-menu,.dm-context-menu,.dm-edit-menu,.dm-message-heading,.dm-quick-menu,.test-menu'
const closeFloatingMenus = (event) => {
  if (event?.target?.closest?.(MENU_KEEP_SELECTOR)) return
  communityMenu.value = false
  communityContextMenu.value = null
  dmContextMenu.value = null
  dmEditMenu.value = null
  dmQuickMenu.value = false
  testMenu.value = null
  emojiPanelOpen.value = false
  emojiPanelPos.value = null
  emojiPanelTarget.value = null
  attachmentMenu.value = null
}
// 原版测试菜单保留其像素位置；生产接入后不再伪造服务端数据。
const pushTestIncoming = () => notDeveloped('测试推送')
const addTestFriendRequest = () => notDeveloped('测试好友申请')
const addTestGroupRequest = () => notDeveloped('测试入群申请')
const addTestCommunityRequest = () => notDeveloped('测试社区申请')
const testSignedOut = () => api.logout()
const testClearLogs = () => notDeveloped('本地清空服务端聊天记录')
const testResetDemo = () => notDeveloped('重置服务端数据')
const onMenuKeydown = (event) => {
  if (event.key !== 'Escape') return
  if (replyTarget.value) { replyTarget.value = null; return }
  emojiPanelOpen.value = false
  emojiPanelPos.value = null
  emojiPanelTarget.value = null
  attachmentMenu.value = null
  closeFloatingMenus()
}
onMounted(() => {
  api.connect()
  window.addEventListener('click', closeFloatingMenus)
  window.addEventListener('contextmenu', closeFloatingMenus, true)
  window.addEventListener('keydown', onMenuKeydown)
  window.addEventListener('resize', syncSidebarWidthFromDom)
})
onUnmounted(() => {
  window.removeEventListener('click', closeFloatingMenus)
  window.removeEventListener('contextmenu', closeFloatingMenus, true)
  window.removeEventListener('keydown', onMenuKeydown)
  window.removeEventListener('resize', syncSidebarWidthFromDom)
  for (const upload of uploads.value) if (upload.preview) URL.revokeObjectURL(upload.preview)
  api.disconnect()
})
// 频道栏宽度可拖动（Discord 式拉轴）：宽度用响应式 ref 驱动栅格，拖动结果写入 localStorage
const SIDEBAR_MIN = 200
const SIDEBAR_MAX = 480
const SIDEBAR_STORAGE_KEY = 'discord-sidebar-w-v1'
const clampSidebarWidth = (value) => Math.min(SIDEBAR_MAX, Math.max(SIDEBAR_MIN, Math.round(value)))
const readStoredWidth = () => {
  try {
    const saved = Number(localStorage.getItem(SIDEBAR_STORAGE_KEY))
    return saved ? clampSidebarWidth(saved) : null
  } catch { return null }
}
const sidebarWidth = ref(readStoredWidth() || 240)
// rail 实际宽度用 ResizeObserver 追踪成响应式 ref（旧写法在 computed 里直接读 getBoundingClientRect，断点变化不会触发重算）
const railWidth = ref(60)
const measureRailWidth = () => {
  const rail = document.querySelector('.server-rail')
  const width = rail ? Math.round(rail.getBoundingClientRect().width) : 0
  if (width) railWidth.value = width
}
let railObserver = null
onMounted(() => {
  measureRailWidth()
  const rail = document.querySelector('.server-rail')
  if (rail && typeof ResizeObserver !== 'undefined') {
    railObserver = new ResizeObserver(measureRailWidth)
    railObserver.observe(rail)
  }
})
onUnmounted(() => { railObserver?.disconnect() })
// 拉轴贴在频道栏右边缘，位置必须跟着 rail 的实际宽度（断点下会变）
const resizerX = computed(() => `${railWidth.value + sidebarWidth.value}px`)
const appGridStyle = computed(() => ({ gridTemplateColumns: `${railWidth.value}px ${sidebarWidth.value}px minmax(0, 1fr)` }))
const persistSidebarWidth = () => { try { localStorage.setItem(SIDEBAR_STORAGE_KEY, String(sidebarWidth.value)) } catch {} }
// 窄屏下侧栏被 @media 隐藏：此时读取实际渲染宽度回填，保持状态与画面一致
const syncSidebarWidthFromDom = () => {
  const rect = document.querySelector('.channel-sidebar')?.getBoundingClientRect()
  if (!rect || !rect.width) return
  sidebarWidth.value = clampSidebarWidth(rect.width)
}
const startSidebarResize = (event) => {
  const originX = event.clientX
  const originWidth = document.querySelector('.channel-sidebar')?.getBoundingClientRect().width || sidebarWidth.value
  document.documentElement.classList.add('sidebar-resizing')
  const onMove = (moveEvent) => { sidebarWidth.value = clampSidebarWidth(originWidth + (moveEvent.clientX - originX)) }
  const finish = () => {
    document.documentElement.classList.remove('sidebar-resizing')
    window.removeEventListener('mousemove', onMove)
    window.removeEventListener('mouseup', finish)
    persistSidebarWidth()
  }
  window.addEventListener('mousemove', onMove)
  window.addEventListener('mouseup', finish)
}
const resetSidebarWidth = () => {
  sidebarWidth.value = 240
  try { localStorage.removeItem(SIDEBAR_STORAGE_KEY) } catch {}
  demoNotice('频道栏宽度已复位')
}
const onSidebarResizerKeydown = (event) => {
  if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return
  event.preventDefault()
  sidebarWidth.value = clampSidebarWidth(sidebarWidth.value + (event.key === 'ArrowRight' ? 16 : -16))
  persistSidebarWidth()
}
// 私信 / 群聊 右键菜单
const dmContextMenu = ref(null)
const openDmContext = (event, contact) => {
  dmContextMenu.value = {
    name: contact.name,
    kind: contact.kind,
    x: Math.min(event.clientX, window.innerWidth - 216),
    y: Math.min(event.clientY, window.innerHeight - 180)
  }
}
const dmEditMenu = ref(null)
const openDmEditMenu = () => {
  if (!dmContextMenu.value) return
  dmEditMenu.value = { x: Math.min(dmContextMenu.value.x + 216, window.innerWidth - 224), y: dmContextMenu.value.y }
}
const dmContextAction = (action) => {
  const target = dmContextMenu.value
  dmContextMenu.value = null
  dmEditMenu.value = null
  if (!target) return
  const contact = dmContacts.find((item) => item.name === target.name)
  const conversationKey = `dm:${target.name}`
  if (action === 'profile') {
    memberProfile.value = { name: target.name, avatar: '', avatarText: contact?.avatar || target.name[0] }
    return
  }
  if (action === 'clear') { notDeveloped('清除服务端聊天记录'); return }
  if (action === 'remove') {
    const friend = demoFriends.value.find((item) => item.name === target.name || item.remark === target.name)
    if (friend) removeDemoFriend(friend)
    else if (contact) closeDm(contact)
    return
  }
  if (action === 'members' || action === 'groupSettings') { const group=demoGroups.value.find(item=>item.name===target.name);if(group)api.showGroupMembers(group.uid);activePage.value='dm';friendFilter.value='群聊';createGroupFormOpen.value=false;return }
  if (action === 'rename') {
    const group = demoGroups.value.find((item) => item.name === target.name)
    if (group) renameDemoGroup(group)
    return
  }
  if (action === 'leave' && contact) { if(window.confirm(`确定退出群聊“${contact.name}”？`))api.groupRemove(contact.uid,backend.uid) }
}
const openCommunityInvite = (name = selectedCommunity.value) => {
  const community = communities.value.find((item) => item.name === name)
  if (!community) return demoNotice('请先选择社区')
  if (community.role !== 'owner') return demoNotice('只有社区所有者可以直接添加成员')
  selectedCommunity.value = community.name
  backend.activeCommunityId = community.id
  friendSearch.value = ''
  api.showMembers(community.id)
  inviteModal.value = true
}
const communityMenuAction = (action) => {
  const name = communityContextMenu.value?.name
  communityContextMenu.value = null
  if (!name) return
  if (action === 'read') { notDeveloped('标记社区已读'); return }
  if (action === 'invite') { openCommunityInvite(name); return }
  if (action === 'settings') { openCommunitySettings(name); return }
  if (action === 'leave') { const community=communities.value.find(item=>item.name===name);if(community&&window.confirm(`确定离开“${name}”？`))api.leaveCommunity(community.id) }
}
const openCommunitySettings = (name) => {
  selectedCommunity.value=name
  communitySettingsName.value=name
  communitySettingsDescription.value=currentCommunity.value?.description||'欢迎来到我们的社区，一起交流分享吧。'
  newCommunityChannel.value=''
  communityChannelDeleted.value={}
  channelDeleteTarget.value=null
  syncGuideDraft()
  communitySettingsOpen.value=true
}
const saveCommunitySettings = () => notDeveloped('社区资料保存（服务端未返回现有头像/背景 ID，已阻止覆盖）')
const scrollToCommunitySection = (id) => document.getElementById(id)?.scrollIntoView({behavior:'smooth',block:'start'})
const closeCommunitySettings = () => { communitySettingsOpen.value = false; dissolveConfirm.value = false; channelDeleteTarget.value = null }
const communityGuides = ref({})
const guideTitleOf = (name) => communityGuides.value[name]?.title || '服务器指南'
const guideItemsOf = (name) => communityGuides.value[name]?.items || []
const guideDraftTitle = ref('')
const guideDraftItems = ref([])
const newGuideItem = ref({ title: '', body: '' })
const syncGuideDraft = () => {
  const name = selectedCommunity.value
  guideDraftTitle.value = guideTitleOf(name)
  guideDraftItems.value = guideItemsOf(name).map((item) => ({ ...item }))
  newGuideItem.value = { title: '', body: '' }
}
const addGuideItem = () => notDeveloped('服务器指南编辑')
const removeGuideItem = () => notDeveloped('服务器指南编辑')
const saveCommunityGuide = () => notDeveloped('服务器指南编辑')
// 顶部横幅标题跟随社区里的指南标题
const guideHeadingOf = (name) => guideTitleOf(name)
const openGuideEditor = async () => {
  openCommunitySettings(currentCommunity.value?.name || selectedCommunity.value)
  await nextTick()
  scrollToCommunitySection('community-guide')
}
const communityChannelDeleted = ref({})
const channelDeleteTarget = ref(null)
const addCommunityChannel = () => { const name=newCommunityChannel.value.trim().replace(/^#+/,'');const community=currentCommunity.value;if(!name||!community)return;api.createChannel(community.id,name,'一般');newCommunityChannel.value='';setTimeout(()=>api.showChannels(community.id),200) }
// —— 频道管理：列表照搬左侧频道栏（同一份「分区 + 频道」数据），新增频道进入「新建频道」分区 ——
// 删除不立刻改动社区结构，先记入待删除集合，点「保存更改」时统一提交（点「取消」即放弃）。
const CUSTOM_CHANNEL_CATEGORY = '新建频道'
const channelListOf = (name) => { const community=communities.value.find(item=>item.name===name);if(!community)return[];const map=new Map();for(const channel of backend.channels[community.id]||[]){const title=channel.category||'一般';if(!map.has(title))map.set(title,[]);map.get(title).push(channel.name)}return[...map].map(([title,channels])=>({title,channels})) }
const openChannelDelete = (name, category, channel) => { channelDeleteTarget.value = { name, category, channel } }
const confirmChannelDelete = () => { const target=channelDeleteTarget.value;channelDeleteTarget.value=null;if(!target)return;const community=communities.value.find(item=>item.name===target.name);const channel=(backend.channels[community?.id]||[]).find(item=>item.name===target.channel);if(community&&channel)api.deleteChannel(community.id,channel.id) }
const commitChannelDeletions = () => 0
const confirmDissolveCommunity = () => {const community=currentCommunity.value;if(!community)return;api.deleteCommunity(community.id);dissolveConfirm.value=false;communitySettingsOpen.value=false;activePage.value='dm'}
const scrollChat = (event) => {
  const log = event.currentTarget
  log.scrollTop += event.deltaY
  const conv = api.activeConversation.value
  if (log.scrollTop < 12 && conv?.hasMore && !conv.loading && conv.oldestId) api.requestHistory(conv, conv.oldestId)
}
const sendReaction = (emoji, messageId) => {
  const conv = api.activeConversation.value
  if (!conv || !emoji) return
  const record = conv.messages.find((item) => String(item.id) === String(messageId))
  const snapshot = record ? { author: record.author, text: record.text } : { author: '成员', text: '' }
  api.sendChat(emoji, messageId, snapshot)
}
const showEmojiPicker = (id, x, y) => {
  emojiPicker.value = { id, x: Math.max(8, Math.min(x, window.innerWidth - 110)), y: Math.max(8, Math.min(y, window.innerHeight - 340)) }
  contextMenu.value = null
}
const openEmojiPanelFromReaction = () => {
  const picker = emojiPicker.value
  emojiPicker.value = null
  attachmentMenu.value = null
  const width = 420
  const height = 360
  let left = Math.max(8, Math.min(picker?.x || 8, window.innerWidth - width - 8))
  let top = Math.max(8, Math.min(picker?.y || 8, window.innerHeight - height - 8))
  emojiPanelTarget.value = picker ? picker.id : null
  emojiPanelPos.value = { left, top }
  emojiPanelOpen.value = true
}
// 从消息行读出「被回复消息」的快照，用来生成引用行（作者 / 角色色 / 头像 / 正文）
// 按 id 找行：id 里含空格/emoji，属性选择器不可靠，统一用 dataset 比对。
// 用 Map 缓存 id→DOM 行，避免每次跳转/回复都全量 querySelectorAll；命中前校验 isConnected，防止拿到已卸载的旧节点
const messageRowCache = new Map()
const findMessageRow = (id) => {
  if (!id) return null
  const cached = messageRowCache.get(id)
  if (cached && cached.isConnected && cached.dataset.messageId === id) return cached
  const el = [...document.querySelectorAll('[data-message-id]')].find((row) => row.dataset.messageId === id) || null
  if (el) messageRowCache.set(id, el)
  return el
}
// 从消息行提取完整正文：所有 <p> 段落用换行拼接；没有文字时退回文件名 / 图片占位
const messageTextFromRow = (row) => {
  if (!row) return ''
  const paragraphs = [...row.querySelectorAll('p')].map((p) => p.textContent.trim()).filter(Boolean)
  if (paragraphs.length) return paragraphs.join('\n')
  const files = [...row.querySelectorAll('.message-file a')].map((a) => `📎 ${a.textContent.trim()}`)
  if (files.length) return files.join('\n')
  return row.querySelector('.message-images img') ? '🖼 图片' : ''
}
// 复制到剪贴板：优先 navigator.clipboard，失败退回 execCommand（兼容非安全上下文 / 权限受限）
const copyToClipboard = async (text) => {
  try {
    if (navigator.clipboard?.writeText) { await navigator.clipboard.writeText(text); return true }
    throw new Error('no clipboard api')
  } catch {
    try {
      const ta = document.createElement('textarea')
      ta.value = text
      ta.setAttribute('readonly', '')
      ta.style.position = 'fixed'
      ta.style.top = '-9999px'
      document.body.appendChild(ta)
      ta.select()
      const ok = document.execCommand('copy')
      ta.remove()
      return ok
    } catch { return false }
  }
}
const replyInfoFromRow = (row) => {
  const nameEl = row?.querySelector('.meta b')
  const author = nameEl?.textContent?.trim() || '成员'
  const avatarEl = row?.querySelector('.avatar')
  const img = avatarEl?.querySelector('img')
  return {
    author,
    tone: [...(nameEl?.classList || [])].find((cls) => ['purple', 'blue', 'red'].includes(cls)) || '',
    text: messageTextFromRow(row),
    avatar: [...(avatarEl?.classList || [])].find((cls) => AVATAR_TONES.includes(cls)) || '',
    avatarImage: img?.getAttribute('src') || '',
    avatarText: (avatarEl?.textContent || author[0] || '?').trim().slice(0, 1)
  }
}
// 点引用行 → 滚到原消息并闪一下（Discord 的跳转反馈）
const jumpToMessage = (targetId) => {
  if (!targetId) return
  const el = findMessageRow(targetId)
  if (!el) { demoNotice('原消息不在当前视图'); return }
  el.scrollIntoView({ block: 'center', behavior: 'smooth' })
  el.classList.add('message-flash')
  setTimeout(() => el.classList.remove('message-flash'), 1400)
}
const openContextMenu = (event) => {
  const row = event.target.closest('.message[data-message-id]')
  if (!row || row.dataset.recalled) { contextMenu.value = null; return }
  const id = row.dataset.messageId
  // 右键落在文件卡片 / 图片上时，菜单里多一项「下载」
  const attachment = event.target.closest?.('.message-file, .message-images a')
  const record = api.activeConversation.value?.messages.find((item) => String(item.id) === String(id))
  contextMenu.value = {
    id,
    attachment: attachment ? { url: attachment.dataset?.url || attachment.getAttribute('href') || '', fileId: attachment.dataset?.fileId || '', size: Number(attachment.dataset?.size || 0), name: attachment.dataset?.name || attachment.getAttribute('title') || '附件' } : null,
    x: Math.max(8, Math.min(event.clientX, window.innerWidth - 282)),
    y: Math.max(8, Math.min(event.clientY, window.innerHeight - 490)),
    author: row.querySelector('.meta b')?.textContent || '成员',
    text: messageTextFromRow(row),
    mine: !!record?.mine,
    conversationKey: activePage.value === 'dm-chat' ? `dm:${selectedDm.value}` : active.value
  }
  emojiPicker.value = null
}
// 下载附件：blob 地址直接走 <a download>；将来接后端换成文件接口的下载地址即可
const downloadAttachment = (attachment) => {
  if (attachment?.fileId) { api.downloadFile({ fileId: attachment.fileId, name: attachment.name, size: attachment.size, format: (attachment.name.split('.').pop()||'FILE').toUpperCase() }); return }
  if (!attachment?.url) { demoNotice('该附件没有可下载的文件'); return }
  const link = document.createElement('a')
  link.href = attachment.url
  link.download = attachment.name || 'download'
  link.rel = 'noreferrer'
  document.body.appendChild(link)
  link.click()
  link.remove()
  demoNotice(`开始下载 ${attachment.name}`)
}
const handleMessageAction = async (event) => {
  const avatar = event.target.closest('.avatar')
  if (avatar) {
    const row=avatar.closest('.message')
    const name=row?.querySelector('.meta b')?.textContent?.trim()
    if (name) { const id=row?.dataset.messageId;const record=api.activeConversation.value?.messages.find(item=>String(item.id)===String(id));memberProfile.value={name,uid:record?.senderUid||'',avatar:avatar.className.split(/\s+/).find(cls=>['aka','ragnar','snow','bot','zucha'].includes(cls))||'',avatarText:avatar.textContent.trim()};contextMenu.value=null;emojiPicker.value=null;return }
  }
  const button = event.target.closest('[data-action]')
  if (!button) { contextMenu.value = null; emojiPicker.value = null; return }
  const id = button.closest('.message')?.dataset.messageId || button.closest('[data-message-id]')?.dataset.messageId || contextMenu.value?.id
  const action = button.dataset.action
  const emoji = button.dataset.emoji
  if (action === 'react' && id && emoji) { sendReaction(emoji, id); contextMenu.value = null; emojiPicker.value = null }
  if (action === 'view-reactions' && id) { reactionDetails.value = { items: messageReactions.value[id] || [] }; contextMenu.value = null }
  if (action === 'picker' && id) { showEmojiPicker(id, event.clientX, event.clientY) }
  if (action === 'more' && id) openContextMenu({ target: button.closest('.message'), clientX: event.clientX, clientY: event.clientY })
  if (action === 'reply' && id) {
    // Discord 的回复：输入框上方出现「正在回复 @某人」的条，发出去的消息自带引用行。
    // 右键菜单渲染在消息行之外，closest('.message') 取不到，必须按 id 回查。
    const row = button.closest('.message') || findMessageRow(id)
    replyTarget.value = {
      ...replyInfoFromRow(row),
      id,
      conversationKey: activePage.value === 'dm-chat' ? `dm:${selectedDm.value}` : active.value
    }
    contextMenu.value = null
    emojiPicker.value = null
    await nextTick()
    composerInput.value?.focus()
  }
  if (action === 'jump') { jumpToMessage(button.dataset.target); contextMenu.value = null; return }
  if (action === 'download') {
    downloadAttachment(button.dataset.fileId ? { fileId: button.dataset.fileId, name: button.dataset.name, size: Number(button.dataset.size||0) } : button.dataset.url ? { url: button.dataset.url, name: button.dataset.name } : contextMenu.value?.attachment)
    contextMenu.value = null
  }
  if (action === 'recall' && contextMenu.value) {
    if (contextMenu.value.mine) api.deleteMessage(contextMenu.value.id); else demoNotice('只能撤回自己的消息')
    contextMenu.value = null
  }
  if (action === 'forward') { notDeveloped('消息转发'); contextMenu.value = null }
  if (action === 'copy' && contextMenu.value) {
    const text = contextMenu.value.text
    if (text) {
      const ok = await copyToClipboard(text)
      demoNotice(ok ? '消息已复制' : '复制失败')
    } else {
      demoNotice('该消息没有可复制的文字')
    }
    contextMenu.value = null
  }
  if (action === 'unread') { notDeveloped('标记未读'); contextMenu.value = null }
  if (action === 'delete' && contextMenu.value?.mine) { api.deleteMessage(contextMenu.value.id); contextMenu.value = null }
  if (action === 'link' && contextMenu.value) {
    const ok = await copyToClipboard(`${location.origin}${location.pathname}#${contextMenu.value.id}`)
    demoNotice(ok ? '消息链接已复制' : '复制失败')
    contextMenu.value = null
  }
  if (action === 'report') { notDeveloped('举报消息'); contextMenu.value = null }
}
const submit = async () => {
  const text = message.value.trim()
  if (!text && !uploads.value.length) return
  const replyId = replyTarget.value?.id || 0
  const textSent = text ? api.sendChat(text, replyId, replyTarget.value) : false
  let startedAny = false
  const failedUploads = []
  for (const upload of uploads.value) {
    if (upload.transferLocalId) { startedAny = true; continue }
    const transferId = api.uploadFile(upload.file, true)
    if (transferId) { upload.transferLocalId = transferId; startedAny = true }
    else { failedUploads.push(upload); if (upload.preview) URL.revokeObjectURL(upload.preview) }
  }
  if (failedUploads.length) uploads.value = uploads.value.filter((item) => !failedUploads.includes(item))
  if (!textSent && !startedAny) return
  playBaka('send')
  replyTarget.value = null
  if (textSent) message.value = ''
  await nextTick()
  resizeComposer(composerInput.value)
  const log = document.querySelector('.message-scroll, .dm-thread')
  if (log) log.scrollTop = log.scrollHeight
}
const handleFiles = async (event) => {
  const selected = Array.from(event.target.files || [])
  for (const file of selected) {
    const ext = file.name.includes('.') ? file.name.split('.').pop().toUpperCase() : 'FILE'
    uploads.value.push({
      id: `${file.name}-${file.lastModified}-${Math.random().toString(36).slice(2, 8)}`,
      file,
      name: file.name,
      format: ext,
      size: file.size,
      preview: file.type.startsWith('image/') ? URL.createObjectURL(file) : ''
    })
  }
  event.target.value = ''
  // 选完文件直接回到输入框，接着打字或回车发送
  await nextTick()
  composerInput.value?.focus()
}
const removeUpload = (id) => {
  const index = uploads.value.findIndex((item) => item.id === id)
  if (index < 0) return
  const item = uploads.value[index]
  const task = item.transferLocalId ? backend.transfers[item.transferLocalId] : null
  if (task && !['已完成','失败','已取消'].includes(task.status)) api.cancelTransfer(task)
  if (item.preview) URL.revokeObjectURL(item.preview)
  uploads.value.splice(index, 1)
}
const formatFileSize = (size) => size < 1024 * 1024 ? `${Math.max(1, Math.round(size / 1024))} KB` : `${(size / 1024 / 1024).toFixed(1)} MB`
const closeComposerPopovers = () => { attachmentMenu.value = null; emojiPanelOpen.value = false; emojiPanelPos.value = null; emojiPanelTarget.value = null }
const resizeComposer = (el) => {
  if (!el) return
  el.style.height = 'auto'
  el.style.height = `${Math.min(Math.max(el.scrollHeight, 24), 160)}px`
}
const appendEmoji = (emoji) => {
  message.value += emoji
  nextTick(() => {
    const input = composerInput.value
    if (input) {
      resizeComposer(input)
      input.focus()
      const end = input.value.length
      try { input.setSelectionRange(end, end) } catch {}
    }
  })
}
const toggleEmojiPanel = (event) => {
  event.stopPropagation()
  attachmentMenu.value = null
  if (emojiPanelOpen.value) { emojiPanelOpen.value = false; emojiPanelPos.value = null; emojiPanelTarget.value = null; return }
  emojiPanelTarget.value = null
  const rect = event.currentTarget.getBoundingClientRect()
  const width = 420
  const height = 360
  let left = Math.max(8, Math.min(rect.right - width, window.innerWidth - width - 8))
  let top = rect.top - height - 8
  if (top < 8) top = Math.min(rect.bottom + 8, Math.max(8, window.innerHeight - height - 8))
  emojiPanelPos.value = { left, top }
  emojiPanelOpen.value = true
}
const toggleAttachmentMenu = (event) => {
  emojiPanelOpen.value = false
  emojiPanelTarget.value = null
  if (attachmentMenu.value) { attachmentMenu.value = null; return }
  const rect = event.currentTarget.getBoundingClientRect()
  attachmentMenu.value = { x: Math.max(8, Math.min(rect.left - 4, window.innerWidth - 216)), y: Math.max(8, rect.top - 58) }
}
const handleAttachmentAction = async (action) => {
  attachmentMenu.value = null
  emojiPanelOpen.value = false
  if (action === 'upload') { await nextTick(); fileInput.value?.click(); return }
  const labels = { thread: '子区', poll: '投票', gift: '礼物', gif: 'GIF', sticker: '贴纸', emoji: '表情', app: 'APP' }
  notDeveloped(labels[action] || '该功能')
}
const copyInvite = async () => notDeveloped('社区邀请链接')
const joinServer = () => {
  const id = Number(String(joinCode.value).match(/\d+/)?.[0] || 0)
  if (!id) { demoNotice('请输入社区 ID'); return }
  api.joinCommunity(id)
  joinModal.value = false
  joinCode.value = ''
}
const selectSetting = (name) => { selectedSetting.value = name; if (!['账户','Baka','外观'].includes(name)) notDeveloped(name) }
const selectAppearance = (name) => { selectedAppearance.value = name; if (name !== '主题') notDeveloped(name) }
const handleProfileBackground = (event) => { event.target.value = ''; notDeveloped('个人资料背景') }
const closeSettings = () => { settingsOpen.value = false; profileEditorOpen.value = false; profileBackgroundMenu.value = false }
const closeProfileEditor = () => { profileEditorOpen.value = false; settingsOpen.value = false; profileBackgroundMenu.value = false }
const demoNotice = (text) => { toast.value = text; setTimeout(() => { if (toast.value === text) toast.value = '' }, 1800) }
const notDeveloped = (name = '该功能') => demoNotice(`${name}暂未开发`)

let lastSyncedBackendName = ''
const syncBackendState = () => {
  demoSignedIn.value = !!backend.uid
  demoUid.value = backend.uid ? String(backend.uid) : ''
  if (backend.name !== lastSyncedBackendName) { demoNickname.value = backend.name || '未登录'; demoNicknameDraft.value = backend.name || ''; lastSyncedBackendName = backend.name }
  demoFriends.value = backend.contacts.filter((item) => !item.isGroup).map((item) => ({ uid: String(item.uid), name: item.name, remark: item.name, email: '', online: false }))
  demoGroups.value = backend.contacts.filter((item) => item.isGroup).map((item) => ({ uid: String(item.uid), name: item.name, members: backend.groupMembers[item.uid] || [] }))
  demoFriendRequests.value = backend.friendRequests.map((item) => ({ ...item, email: '' }))
  const allGroupRequests = []
  for (const [groupId, requests] of Object.entries(backend.groupRequests)) for (const request of requests) allGroupRequests.push({ ...request, group: backend.contacts.find((item) => item.uid === Number(groupId))?.name || groupId, groupUid: Number(groupId) })
  demoGroupRequests.value = allGroupRequests
  communities.value = backend.communities.map((item) => ({ ...item, mark: item.name.slice(0, 1), color: '#5865f2', icon: '', members: (backend.members[item.id] || []).length, online: 0 }))
  demoFiles.value = [
    ...backend.transferOrder.map((id) => backend.transfers[id]).filter((task) => task && !task.silent && (!task.ownerUid || Number(task.ownerUid)===Number(backend.uid)) && !(task.direction==='upload'&&task.status==='已完成')).map((task) => ({ ...task, id: task.localId, size: task.totalSize, type: task.direction, format: task.format || 'FILE' })),
    ...backend.files,
  ]
  if (!selectedCommunity.value && communities.value.length) selectedCommunity.value = communities.value[0].name
  if (selectedCommunity.value && !communities.value.some((item) => item.name === selectedCommunity.value)) selectedCommunity.value = communities.value[0]?.name || ''
  const contacts = backend.contacts.map((item) => ({ uid: item.uid, name: item.name, avatar: item.name[0] || '?', kind: item.isGroup ? '群聊' : '好友', online: false, messages: [] }))
  dmContacts.splice(0, dmContacts.length, ...contacts)
  if (!selectedDm.value && contacts.length) selectedDm.value = contacts[0].name
  if (selectedDm.value && !contacts.some((item) => item.name === selectedDm.value)) selectedDm.value = contacts[0]?.name || ''
  const community = communities.value.find((item) => item.name === selectedCommunity.value)
  demoCommunityMemberList.value = community ? (backend.members[community.id] || []) : []
  demoCommunityRequests.value = community ? (backend.communityRequests[community.id] || []).map((item) => ({ ...item, community: community.name })) : []
}
watch(() => ({
  uid: backend.uid, name: backend.name,
  contacts: backend.contacts.map((item) => ({...item})),
  communities: backend.communities.map((item) => ({...item})),
  friendRequests: backend.friendRequests.map((item) => ({...item})),
  groupMembers: Object.fromEntries(Object.entries(backend.groupMembers).map(([id,list]) => [id,list.map((item) => ({...item}))])),
  groupRequests: Object.fromEntries(Object.entries(backend.groupRequests).map(([id,list]) => [id,list.map((item) => ({...item}))])),
  communityRequests: Object.fromEntries(Object.entries(backend.communityRequests).map(([id,list]) => [id,list.map((item) => ({...item}))])),
  members: Object.fromEntries(Object.entries(backend.members).map(([id,list]) => [id,list.map((item) => ({...item}))])),
  files: backend.files.map((item) => ({...item})),
  transfers: backend.transferOrder.map((id) => { const task=backend.transfers[id];return task?{id,status:task.status,progress:task.progress,transferred:task.transferred,fileId:task.fileId,url:task.url,error:task.error}:null }),
}), syncBackendState, { deep: true, immediate: true })
watch(() => backend.noticeSeq, () => {
  if (!backend.lastNotice) return
  demoNotice(backend.lastNotice)
  if (backend.lastNotice.includes('发来新消息')) playBaka('receive')
  const registered=/注册成功，UID：(\d+)/.exec(backend.lastNotice)
  if (registered) { demoAuth.identity=registered[1];demoAuthMode.value='登录' }
})
watch(themeChoice, (theme) => { if (backend.uid && theme !== 'device') api.changeTheme(theme) })
watch(() => backend.uid, (uid) => { demoLoginOpen.value = !uid })
watch(() => backend.connected, (connected) => { if (!connected) demoLoginOpen.value = true })
watch(() => backend.transferOrder.map((id) => { const task = backend.transfers[id]; return task ? { id, status: task.status } : null }), () => {
  const remaining = []
  for (const upload of uploads.value) {
    const task = upload.transferLocalId ? backend.transfers[upload.transferLocalId] : null
    if (task && ['已完成','失败','已取消'].includes(task.status)) {
      if (upload.preview) URL.revokeObjectURL(upload.preview)
    } else {
      remaining.push(upload)
    }
  }
  if (remaining.length !== uploads.value.length) uploads.value = remaining
}, { deep: true })
watch(() => api.activeConversation.value?.messages.length, async () => { await nextTick(); const log=document.querySelector('.message-scroll, .dm-thread');if(log&&log.scrollHeight-log.scrollTop-log.clientHeight<180)log.scrollTop=log.scrollHeight })
watch(() => ({ page: activePage.value, key: backend.activeKey, unread: { ...backend.unreadConversations } }), ({ page, key, unread }) => {
  if (page !== 'dm-chat' || !key || !unread[key]) return
  const [kind, id] = key.split(':')
  if (kind === 'private' || kind === 'group') api.markConversationRead(kind, Number(id))
}, { deep: true })
watch(() => backend.activeKey, async () => { await nextTick();const log=document.querySelector('.message-scroll, .dm-thread');if(log)log.scrollTop=log.scrollHeight })
watch(() => backend.channels, () => {
  const community = currentCommunity.value
  if (!community || activePage.value !== 'community') return
  const channels = backend.channels[community.id] || []
  const current = api.activeConversation.value
  if (channels.length && (!current || current.kind !== 'channel' || !channels.some((item) => item.id === current.id))) switchChannel(channels[0].name)
}, { deep: true })
watch(selectedCommunity, () => { const community=currentCommunity.value;demoCommunityRequests.value=community?(backend.communityRequests[community.id]||[]).map(item=>({...item,community:community.name})):[] })
const submitDemoAuth = () => {
  if (demoAuthMode.value === '注册') {
    if (!demoAuth.username.trim() || !demoAuth.email.trim() || !demoAuth.password.trim()) return demoNotice('请填写昵称、邮箱和密码')
    api.register(demoAuth.username.trim(), demoAuth.email.trim(), demoAuth.password)
  } else {
    if (!demoAuth.identity.trim() || !demoAuth.password.trim()) return demoNotice('请输入账号和密码')
    api.login(demoAuth.identity.trim(), demoAuth.password)
  }
  demoAuth.password = ''
}
const demoLogout = () => { api.logout(); demoAuthMode.value = '登录'; demoAuth.password = ''; demoTab.value = '账号'; demoLoginOpen.value = true }
const requestDemoFriend = () => {
  const email = demoForm.email.trim().toLowerCase()
  if (!email || !email.includes('@')) return demoNotice('请输入有效的邮箱地址')
  api.addFriend(email, demoForm.note.trim())
  demoForm.email=''; demoForm.note=''
}
const sendFriendRequestFromModal = () => {
  const valid = demoForm.email.trim().includes('@')
  requestDemoFriend()
  if (valid) friendRequestModal.value = false
}
const openDemoLoginWindow = () => {
  demoAuthMode.value = '登录'
  demoAuth.password = ''
  demoLoginOpen.value = true
}
const submitDemoLoginWindow = () => {
  submitDemoAuth()
  if (demoSignedIn.value) demoLoginOpen.value = false
}
const handleDemoFriendRequest = (request, accept) => { if (api.handleFriend(request.uid, accept)) setTimeout(() => { api.showFriendRequests();api.showContacts() }, 200) }
const removeDemoFriend = (friend) => api.removeFriend(friend.uid)
const renameDemoFriend = (friend) => { const next=promptRename('设置好友备注名',friend.remark||friend.name);if(next)api.setFriendRemark(friend.uid,next) }
const createDemoGroup = () => { const name=demoForm.groupName.trim();if(!name)return demoNotice('请输入群聊名称');api.createGroup(name);demoForm.groupName='' }
const renameDemoGroup = (group) => {const next=promptRename('修改群名称',group.name);if(next)api.groupRename(group.uid,next)}
const addDemoGroupMember = (group) => {const uid=demoForm.memberUid.trim();if(!uid)return demoNotice('请输入成员 UID');api.groupAdd(group.uid,uid);demoForm.memberUid='';setTimeout(()=>api.showGroupMembers(group.uid),200)}
const removeDemoGroupMember = (group, member) => api.groupRemove(group.uid,member.uid)
const toggleDemoGroupRole = (group, member) => api.groupRole(group.uid,member.uid,true)
const handleDemoGroupRequest = (request, accept) => {if(api.handleGroupRequest(request.groupUid,request.uid,accept))setTimeout(()=>api.showGroupRequests(request.groupUid),200)}
const requestDemoJoinGroup = () => {const uid=promptRename('输入要申请加入的群 UID','');if(uid&&Number(uid)>0)api.joinGroup(uid)}
const openDemoFriendChat = (friend) => {const contact=dmContacts.find(item=>Number(item.uid)===Number(friend.uid));if(contact)openDm(contact);demoHubOpen.value=false}
const openGroupChat = (group) => { const contact=dmContacts.find(item=>Number(item.uid)===Number(group.uid));if(contact)openDm(contact);api.showGroupMembers(group.uid);api.showGroupRequests(group.uid) }
const deleteDemoGroup = (group) => {if(window.confirm(`解散群聊“${group.name}”？`))api.groupDelete(group.uid)}
const selectDemoFiles = () => demoFilesInput.value?.click()
const addDemoFiles = (event) => {for(const file of Array.from(event.target.files||[]))api.uploadFile(file,false);event.target.value=''}
const setDemoFileStatus = (file,status) => {const task=backend.transfers[file.localId];if(!task)return;if(status==='已暂停')api.pauseTransfer(task);else if(task.direction==='upload'&&!task.file&&['已中断','等待选择原文件'].includes(task.status)){resumeTransferTarget.value=task;resumeFileInput.value?.click()}else api.resumeTransfer(task)}
const bindResumeUpload = (event) => {const file=event.target.files?.[0];if(file&&resumeTransferTarget.value)api.bindResumeFile(resumeTransferTarget.value,file);event.target.value='';resumeTransferTarget.value=null}
const downloadDemoFile = (file) => {const task=backend.transfers[file.localId];if(task?.url){const link=document.createElement('a');link.href=task.url;link.download=task.name;link.click()}else api.downloadFile(file)}
const toggleFileTransfer = (file) => {if(!file)return;if(['已暂停','已中断','等待选择原文件'].includes(file.status))setDemoFileStatus(file,'继续');else setDemoFileStatus(file,'已暂停')}
const toggleActiveTransfer = () => toggleFileTransfer(activeTransfer.value)
const deleteDemoFile = (file) => {const task=backend.transfers[file.localId];if(task){if(['已完成','已取消','失败'].includes(task.status))api.dismissTransfer(task);else api.cancelTransfer(task)}else api.deleteFile(file.fileId||file.id)}
const joinDemoCommunity = () => {const id=Number(String(demoForm.joinCode).match(/\d+/)?.[0]||0);if(!id)return demoNotice('请输入社区 ID');api.joinCommunity(id);demoForm.joinCode=''}
const handleDemoCommunityRequest = (request,accept) => {const community=currentCommunity.value;if(!community)return;if(api.handleCommunityRequest(community.id,request.uid,accept))setTimeout(()=>api.showCommunityRequests(community.id),200)}
const inviteFriendToCommunity = (friend) => {
  const community = currentCommunity.value
  if (!community) return demoNotice('请先选择社区')
  if (community.role !== 'owner') return demoNotice('只有社区所有者可以直接添加成员')
  api.communityAdd(community.id, friend.uid)
}
const createDemoCommunity = () => {newCommunityName.value=demoForm.communityName;createCommunity();demoForm.communityName=''}
const toggleDemoMemberRole = (member) => {const community=currentCommunity.value;if(!community||member.role==='owner')return;api.communityRole(community.id,member.uid,member.role!=='admin')}
const removeDemoCommunityMember = (member) => {const community=currentCommunity.value;if(community&&member.role!=='owner')api.communityRemove(community.id,member.uid)}
const addDemoCommunityMember = () => {const community=currentCommunity.value;const uid=Number(demoForm.memberUid);if(!community||!uid)return demoNotice('请输入成员 UID');api.communityAdd(community.id,uid);demoForm.memberUid=''}
const saveDemoProfile = () => {if(!demoNicknameDraft.value.trim())return demoNotice('昵称不能为空');api.changeName(demoNicknameDraft.value.trim());if(demoEmail.value.trim())api.setEmail(demoEmail.value.trim())}
const setDemoAvatar = () => avatarUploadInput.value?.click()
const handleAvatarUpload = (event) => {const file=event.target.files?.[0];if(file)api.uploadFile(file,{type:'avatar'});event.target.value=''}
</script>

<template>
  <div class="window-bar"><div class="window-title"><span class="tiny-server">🌸</span> {{ ['dm','dm-chat'].includes(activePage) ? (activePage==='dm'?'好友':selectedDm) : activePage==='discover' ? '发现' : selectedCommunity }}</div></div>
  <input ref="avatarUploadInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleAvatarUpload">
  <div class="discord-app" :style="appGridStyle">
    <nav class="server-rail">
      <button class="rail-home" :class="{'rail-selected':['dm','dm-chat'].includes(activePage)}" aria-label="私聊和群聊" data-tooltip="私聊和群聊" title="私聊和群聊" @click="activePage='dm'" @dblclick="playBaka('logo')"><img src="/logo.png" alt="Discord"></button>
      <div class="rail-divider"></div>
      <button v-for="community in communities" :key="community.name" class="server community-server" :class="{'active-server':activePage==='community'&&selectedCommunity===community.name}" :aria-label="community.name" :data-tooltip="community.name" :title="community.name" @click="selectCommunity(community)" @contextmenu.prevent.stop="openCommunityContext($event,community)"><span class="community-mark"><img :src="community.icon || DEFAULT_COMM_AVATAR" :alt="community.name"></span><i></i></button>
      <div class="rail-action-wrap"><button class="round-add" :class="{'rail-selected':communityMenu}" aria-label="创建社区" data-tooltip="创建社区" title="创建或加入社区" @click="communityMenu=!communityMenu;communityContextMenu=null"><svg viewBox="0 0 24 24"><path d="M12 4v16M4 12h16"/></svg></button><div v-if="communityMenu" class="community-menu"><button @click="communityMenu=false;createCommunityOpen=true">＋　创建社区</button><button @click="communityMenu=false;joinModal=true">↗　加入社区</button><button @click="communityMenu=false;friendRequestModal=true">♧　添加好友</button></div></div>
      <button class="round-discover" :class="{'rail-selected':activePage==='discover'}" aria-label="发现" data-tooltip="发现" title="发现" @click="notDeveloped('发现社区')"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="m15.5 8.5-2.2 5-4.8 2 2-4.8 5-2.2Z"/></svg></button>
      <div class="rail-divider rail-test-divider"></div>
      <button class="round-test" :class="{'rail-selected':testMenu}" aria-label="测试" data-tooltip="测试菜单" title="测试菜单" @click.stop="openTestMenu"><svg viewBox="0 0 24 24"><path d="M9 3h6M10 3v6.2L5.2 17A2.4 2.4 0 0 0 7.3 20.6h9.4A2.4 2.4 0 0 0 18.8 17L14 9.2V3"/><path d="M7.2 14h9.6"/></svg></button>
    </nav>
    <div v-if="testMenu" class="community-context-dismiss" @click="testMenu=null" @contextmenu.prevent="testMenu=null"></div>
    <div v-if="testMenu" class="community-context-menu test-menu" :style="{left:`${testMenu.x}px`,bottom:`${testMenu.bottom}px`}" @click.stop>
      <div class="test-menu-label">测试 · 好友与群聊</div>
      <button @click="addTestFriendRequest()"><span class="test-menu-icon">♧</span>收到一条好友申请<b v-if="demoFriendRequests.length" class="test-menu-count">{{demoFriendRequests.length}}</b></button>
      <button @click="addTestGroupRequest()"><span class="test-menu-icon">◌</span>收到一条入群申请</button>
      <button @click="addTestCommunityRequest()"><span class="test-menu-icon">◆</span>收到一条入社区申请</button>
      <button @click="activePage='dm';friendFilter='待定'"><span class="test-menu-icon">▤</span>打开待处理列表</button>
      <div></div>
      <div class="test-menu-label">测试 · 消息与音效</div>
      <button @click="pushTestIncoming()"><span class="test-menu-icon">✉</span>推送一条私信</button>
      <button @click="pushTestIncoming()"><span class="test-menu-icon">#</span>推送一条频道消息</button>
      <button @click="playBaka('logo')"><span class="test-menu-icon">♪</span>试听 Baka 音效</button>
      <div></div>
      <div class="test-menu-label">测试 · 窗口与流程</div>
      <button @click="testMenu=null;friendRequestModal=true"><span class="test-menu-icon">＋</span>添加好友窗口</button>
      <button @click="testMenu=null;createCommunityOpen=true"><span class="test-menu-icon">＋</span>创建社区窗口</button>
      <button @click="testMenu=null;joinModal=true"><span class="test-menu-icon">↗</span>加入服务器窗口</button>
      <button @click="testMenu=null;activePage='dm';friendFilter='群聊';createGroupFormOpen=true"><span class="test-menu-icon">◌</span>创建群聊表单</button>
      <button @click="testMenu=null;openDemoLoginWindow()"><span class="test-menu-icon">⇥</span>登录窗口</button>
      <button @click="testMenu=null;demoTab='账号';demoHubOpen=true"><span class="test-menu-icon">◈</span>功能工作台</button>
      <button @click="testMenu=null;settingsOpen=true"><span class="test-menu-icon">⚙</span>用户设置</button>
      <div></div>
      <button @click="testSignedOut()"><span class="test-menu-icon">⇤</span>切换为未登录态</button>
      <button @click="testClearLogs()"><span class="test-menu-icon">⌫</span>清空本机聊天记录</button>
      <button class="leave-community" @click="testResetDemo()"><span class="test-menu-icon">↺</span>重置服务端数据</button>
    </div>
    <div v-if="communityContextMenu" class="community-context-dismiss" @click="communityContextMenu=null" @contextmenu.prevent="communityContextMenu=null"></div>
    <div v-if="communityContextMenu" class="community-context-menu" :style="{left:`${communityContextMenu.x}px`,top:`${communityContextMenu.y}px`}" @click.stop>
      <button @click="communityMenuAction('read')"><svg viewBox="0 0 24 24"><path d="m5 12 4 4L19 6"/></svg>标记为已读</button>
      <button @click="communityMenuAction('invite')"><Icon name="userPlus"/>邀请至服务器</button>
      <div></div>
      <button @click="communityContextMenu=null;notDeveloped('社区静音')"><svg viewBox="0 0 24 24"><path d="M5 9v6h4l5 4V5L9 9H5Z"/><path d="m17 9 4 6m0-6-4 6"/></svg>静音服务器<b class="menu-arrow">›</b></button>
      <button @click="communityContextMenu=null;notDeveloped('社区通知设定')"><svg viewBox="0 0 24 24"><path d="M18 15v-4a6 6 0 0 0-12 0v4l-2 3h16l-2-3Z"/><path d="M10 21h4"/></svg>通知设定<small>所有消息</small><b class="menu-arrow">›</b></button>
      <label class="menu-check" @click.stop="notDeveloped('隐藏已静音频道')"><span>隐藏已静音的频道</span><i :class="{on:hideMutedChannels}"></i></label>
      <div></div>
      <button @click="communityMenuAction('settings')"><Icon name="gear"/>服务器设置<b class="menu-arrow">›</b></button>
      <button @click="communityContextMenu=null;notDeveloped('社区隐私设置')"><svg viewBox="0 0 24 24"><path d="M12 3 5 6v6c0 4 3 7 7 8 4-1 7-4 7-8V6l-7-3Z"/><path d="m9 12 2 2 4-4"/></svg>隐私设置</button>
      <button @click="editCommunityNickname()"><Icon name="edit"/>编辑各服务器个人资料</button>
      <div></div>
      <button @click="communityContextMenu=null;communitySettingsOpen=true"><svg viewBox="0 0 24 24"><path d="M4 7h16M4 12h16M4 17h10"/></svg>创建频道</button>
      <button @click="communityContextMenu=null;notDeveloped('频道类别')"><svg viewBox="0 0 24 24"><path d="M4 7h16M4 12h16M4 17h16"/></svg>创建类别</button>
      <button @click="communityContextMenu=null;notDeveloped('社区活动')"><svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="16" rx="2"/><path d="M8 3v4M16 3v4M3 11h18"/></svg>创建活动</button>
      <div></div>
      <button class="leave-community" @click="communityMenuAction('leave')"><Icon name="logout"/>离开社区</button>
    </div>
    <div v-if="dmContextMenu" class="community-context-dismiss" @click="dmContextMenu=null" @contextmenu.prevent="dmContextMenu=null"></div>
    <div v-if="dmContextMenu" class="community-context-menu dm-context-menu" :style="{left:`${dmContextMenu.x}px`,top:`${dmContextMenu.y}px`}" @click.stop>
      <template v-if="dmContextMenu.kind==='群聊'">
        <button @click.stop="openDmEditMenu"><Icon name="edit"/>编辑群聊<b class="menu-arrow">›</b></button>
        <div></div>
        <button class="leave-community" @click="dmContextAction('leave')"><Icon name="logout"/>退出群聊</button>
      </template>
      <template v-else>
        <button @click="dmContextAction('profile')"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.4"/><path d="M5 20v-1.2a7 7 0 0 1 14 0V20z"/></svg>打开他人主页</button>
        <button @click="dmContextAction('clear')"><svg viewBox="0 0 24 24"><path d="M4 7h16M9 7V5h6v2M6.5 7l1 13h9l1-13"/></svg>清除聊天记录</button>
        <div></div>
        <button class="leave-community" @click="dmContextAction('remove')"><svg viewBox="0 0 24 24"><circle cx="10" cy="8" r="3.4"/><path d="M4 20v-1.2A6 6 0 0 1 16 18v2M17 9l5 5m0-5-5 5"/></svg>删除好友</button>
      </template>
    </div>
    <div v-if="dmEditMenu" class="community-context-menu dm-context-menu dm-edit-menu" :style="{left:`${dmEditMenu.x}px`,top:`${dmEditMenu.y}px`}" @click.stop>
      <button @click="dmContextAction('rename')"><Icon name="edit"/>修改群聊名称</button>
      <button @click="dmContextAction('members')"><Icon name="group"/>管理群成员</button>
      <div></div>
      <button @click="dmContextAction('groupSettings')"><Icon name="gear"/>群聊设置</button>
    </div>
    <aside class="channel-sidebar">
      <div v-if="['dm','dm-chat'].includes(activePage)" class="dm-sidebar"><label class="dm-search"><input placeholder="寻找或开始新的对话" @focus="notDeveloped('全局会话搜索')"></label><div class="dm-list-heading dm-message-heading"><span>私信</span><button class="dm-add-cta" aria-label="私信操作" :aria-expanded="dmQuickMenu" @click.stop="dmQuickMenu=!dmQuickMenu">＋</button><div v-if="dmQuickMenu" class="dm-quick-menu" @click.stop><button @click="dmQuickMenu=false;activePage='dm';friendFilter='群聊';createGroupFormOpen=true"><Icon name="group"/><span>创建群聊</span></button></div></div><button v-for="contact in dmContacts" :key="contact.name" class="dm-contact" :class="{selected:activePage==='dm-chat'&&selectedDm===contact.name}" @click="openDm(contact)" @contextmenu.prevent.stop="openDmContext($event,contact)"><span class="dm-avatar"><img :src="dmAvatarSrc(contact.name)" :alt="contact.name"><i v-if="isDmUnread(contact)" class="dm-presence" aria-label="有未读消息" title="有未读消息" style="border:0;background:#ed4245;box-shadow:0 0 0 2.5px var(--sidebar-bg)"></i><i v-else class="dm-presence" :class="contact.online?'online':contact.idle?'idle':'offline'" :data-tooltip="contact.online?'在线':contact.idle?'闲置':'离线'"></i></span><b class="dm-name">{{contact.name}}</b><span class="dm-close" role="button" aria-label="关闭私信" title="关闭私信" @click.stop="closeDm(contact)">×</span></button></div>
      <div v-else-if="activePage==='community'" class="community-sidebar-content">
      <div class="guild-header"><button class="guild-header-title" title="服务器菜单" @click.stop="openCommunityContext($event, currentCommunity)"><b>{{selectedCommunity}}</b><Icon name="caret" class="guild-caret"/></button><div class="guild-header-actions"><button class="invite" title="邀请至服务器" aria-label="邀请至服务器" @click="openCommunityInvite()"><Icon name="userPlus"/></button></div></div>
      <div class="side-shortcuts"><button :class="{selected:active==='服务器指南'}" @click="notDeveloped('服务器指南')"><Icon name="doc"/>{{guideHeadingOf(selectedCommunity)}}</button><button @click="demoTab='社区';demoHubOpen=true"><Icon name="list"/>频道和身份组</button></div>
      <div class="channel-list" @wheel.prevent="scrollChat">
        <template v-for="group in groups" :key="group.title">
          <div class="category" :class="{collapsed:isCategoryCollapsed(group)}">
            <button class="category-toggle" :aria-expanded="!isCategoryCollapsed(group)" :title="isCategoryCollapsed(group)?'展开分区':'收起分区'" @click="toggleCategory(group)"><span class="category-name">{{ group.title }}</span><Icon name="caret" class="category-caret"/></button>
            <button class="category-add" title="新建频道" @click="communitySettingsOpen=true">＋</button>
          </div>
          <template v-if="!isCategoryCollapsed(group)">
          <button v-for="channel in group.channels" :key="channel" class="channel-row" :class="{selected: active === channel}" :title="channel" @click="switchChannel(channel)"><Icon name="hash" class="channel-hash"/><span class="channel-label">{{ channel }}</span></button>
          </template>
        </template>
        <div v-if="communityChannels[selectedCommunity]?.length" class="category custom-channel-category" :class="{collapsed:customChannelsCollapsed}"><button class="category-toggle" :aria-expanded="!customChannelsCollapsed" @click="customChannelsCollapsed=!customChannelsCollapsed"><span class="category-name">新建频道</span><Icon name="caret" class="category-caret"/></button></div>
        <template v-if="!customChannelsCollapsed">
        <button v-for="channel in communityChannels[selectedCommunity]||[]" :key="channel" class="channel-row" :class="{selected:active===channel}" :title="channel" @click="switchChannel(channel)"><Icon name="hash" class="channel-hash"/><span class="channel-label">{{channel}}</span></button>
        </template>
      </div>
      </div>
      <div class="account-bar"><button class="profile-menu-trigger" aria-label="打开个人资料菜单" title="个人资料" @click.stop="profileMenuOpen=!profileMenuOpen;profileStatusMenu=false;profileAccountMenu=false"><span class="profile-pic"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span></button><div class="profile-label"><b>{{demoNickname}}</b><small>{{backend.connected?(backend.uid?'在线':'未登录'):'离线'}}</small></div><Icon name="caret" class="account-caret"/><button class="mic" :class="{off:micMuted}" aria-label="静音" title="静音" @click="notDeveloped('语音静音')"><svg viewBox="0 0 24 24"><rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5.5 11.5a6.5 6.5 0 0 0 13 0M12 18v3M8.5 21h7"/></svg></button><button aria-label="耳机" title="耳机" @click="notDeveloped('耳机模式')"><svg viewBox="0 0 24 24"><path d="M4 14v-2a8 8 0 0 1 16 0v2"/><rect x="3" y="13" width="4" height="7" rx="2"/><rect x="17" y="13" width="4" height="7" rx="2"/></svg></button><button aria-label="設定" title="设置" @click="profileMenuOpen=false;profileEditorOpen=false;settingsOpen = true"><Icon name="gear"/></button></div>
    </aside>
    <div class="sidebar-resizer" role="separator" tabindex="0" aria-orientation="vertical" aria-label="拖动调整频道栏宽度" title="拖动调整频道栏宽度 · 双击复位" :style="{ left: resizerX }" @mousedown.prevent="startSidebarResize" @dblclick="resetSidebarWidth" @keydown="onSidebarResizerKeydown"></div>
    <div v-if="profileMenuOpen" class="profile-menu-dismiss" @click="profileMenuOpen=false;profileStatusMenu=false;profileAccountMenu=false"></div>
    <section v-if="profileMenuOpen" class="user-profile-popover" @click.stop>
      <div class="user-profile-banner"><div class="user-profile-avatar settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></div><div class="user-profile-status-bubble">UID {{demoUid||'—'}}</div></div>
      <div class="user-profile-details"><h2>{{demoNickname}}</h2><p>UID {{demoUid||'—'}}</p>
        <button class="profile-popover-action edit-profile-action" @click="profileMenuOpen=false;selectedSetting='账户';settingsOpen=true;profileEditorOpen=true"><Icon name="edit"/><span>编辑个人资料</span><b>新的</b></button>
        <button class="profile-popover-action" @click="setDemoAvatar"><Icon name="upload"/><span>更换头像</span></button>
        <button class="profile-popover-action status-action" @click="profileStatusMenu=!profileStatusMenu;profileAccountMenu=false"><i :class="'status-'+currentStatus"></i><span>{{currentStatus}}</span><b>›</b></button>
        <div v-if="profileStatusMenu" class="profile-popover-submenu"><button v-for="status in ['在线','闲置','请勿打扰','隐身']" :key="status" @click="notDeveloped('在线状态设置');profileStatusMenu=false"><i :class="'status-'+status"></i>{{status}}</button></div>
      </div>
      <button class="profile-popover-action switch-account-action" @click="profileAccountMenu=!profileAccountMenu;profileStatusMenu=false"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.3"/><path d="M5 20v-1a7 7 0 0 1 14 0v1"/></svg><span>切换账户</span><b>›</b></button>
      <div v-if="profileAccountMenu" class="profile-popover-submenu account-switch-menu"><button @click="profileAccountMenu=false">{{demoNickname}}　当前账户</button><button @click="profileAccountMenu=false;openDemoLoginWindow()">＋　切换账户</button></div>
    </section>
    <main v-if="activePage==='community'" class="chat" :class="{'members-open':memberPanelOpen}" @click="closeComposerPopovers">
      <aside v-if="memberPanelOpen" class="member-panel" aria-label="社区成员">
        <header><h2>社区成员</h2><span>{{demoCommunityMemberList.length}}</span></header>
        <div class="member-panel-scroll">
          <section v-for="group in communityMemberGroups" :key="group.label">
            <h3>{{group.label}} — {{group.members.length}}</h3>
            <article v-for="member in group.members" :key="member.uid" class="member-panel-row" @click="memberProfile={name:member.name,uid:member.uid,avatar:'',avatarText:member.name[0]}">
              <span class="member-panel-avatar">{{member.name[0]}}<i :class="{offline:!member.online}"></i></span>
              <div><b>{{member.name}}</b><small>{{member.online?'在线':'离线'}}</small></div>
              <span v-if="member.role!=='member'" class="member-panel-role">{{member.role==='owner'?'所有者':'管理员'}}</span>
            </article>
          </section>
        </div>
      </aside>
      <header class="chat-top"><div class="channel-heading"><span class="hash-big"><Icon name="hash" class="channel-hash"/></span><b>{{ active==='服务器指南'?guideHeadingOf(selectedCommunity):active }}</b><span v-if="active!=='服务器指南'" class="separator"></span><span v-if="active!=='服务器指南'" class="topic">{{currentCommunity?.description||'本频道用于吹水，吹水和吹水。'}}</span></div><div class="chat-tools"><template v-if="active!=='服务器指南'"><button type="button" title="话题" aria-label="话题" @click="notDeveloped('话题')"><svg viewBox="0 0 24 24"><path d="M21 11.5a8.4 8.4 0 0 1-9 8.4 9 9 0 0 1-3.8-.8L3 21l1.9-5A8.4 8.4 0 0 1 12 3.1a8.4 8.4 0 0 1 9 8.4Z"/></svg></button><button type="button" title="通知设置" aria-label="通知设置" @click="notDeveloped('通知设置')"><svg viewBox="0 0 24 24"><path d="M18 8.6a6 6 0 1 0-12 0c0 5.6-2 7.4-2 7.4h16s-2-1.8-2-7.4"/><path d="M13.7 20a2 2 0 0 1-3.4 0"/></svg></button><button type="button" title="置顶消息" aria-label="置顶消息" @click="notDeveloped('置顶消息')"><svg viewBox="0 0 24 24"><path d="M9 4h6l-1 6 3 3v2H7v-2l3-3z"/><path d="M12 15v5"/></svg></button></template><label class="chat-search"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.6-3.6"/></svg><input type="search" placeholder="搜索" aria-label="搜索" @focus="notDeveloped('消息搜索')"></label><button v-if="active==='服务器指南'" class="guide-edit-button" title="编辑服务器指南" aria-label="编辑服务器指南" @click="openGuideEditor"><Icon name="edit"/></button><button v-else class="members-toggle" :class="{active:memberPanelOpen}" title="社区成员" aria-label="社区成员" :aria-expanded="memberPanelOpen" @click.stop="memberPanelOpen=!memberPanelOpen"><Icon name="group"/></button></div></header>
      <div v-if="unreadBanner&&active!=='服务器指南'" class="channel-unread-bar"><span>自从 {{unreadBannerInfo.time}} 以来有 {{unreadBannerInfo.count}} 条以上的新消息</span><button @click="notDeveloped('标记已读')">标记为已读</button></div>
      <section v-if="active==='服务器指南'" class="server-guide-page">
        <div class="guide-content-wrap">
          <div class="guide-banner" :style="currentCommunity?.background?{backgroundImage:`url(${currentCommunity.background})`}:{}"><div class="guide-banner-art"><i></i><b></b><em>✦</em></div></div>
          <div class="guide-server-intro"><span class="guide-server-avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><div><h1>{{selectedCommunity}} <span>✿</span></h1><p>{{currentCommunity?.description||'欢迎来到我们的社区，一起交流分享吧。'}}</p></div><button @click="openCommunityInvite()">邀请</button></div>
          <div class="guide-main-grid"><section class="guide-resources"><h2>资源</h2><template v-if="guideItemsOf(selectedCommunity).length"><article v-for="item in guideItemsOf(selectedCommunity)" :key="item.id" class="guide-resource-card"><b>{{item.title}}</b><p v-if="item.body">{{item.body}}</p></article></template><article v-else class="guide-resource-card guide-resource-empty"><b>还没有指南内容</b><p>在「服务器设置 → 服务器指南」里添加资源卡片。</p></article></section>
            <aside class="guide-community-card"><div class="guide-community-banner"></div><div class="guide-community-body"><span class="guide-community-avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><h3>{{selectedCommunity}} <span>✿</span></h3><p><i></i>{{currentCommunity?.online||0}} 人在线　•　{{currentCommunity?.members||0}} 位成员</p><small>创建日期：暂未提供</small><div class="guide-highlight"><b>热门活动</b><span>社区活动暂未开发</span></div><div class="guide-tags"><span>社区标签暂未开发</span></div></div></aside>
          </div>
        </div>
      </section>
        <section v-else class="message-scroll" :style="{paddingBottom: uploads.length ? `${84 + Math.ceil(uploads.length / 3) * 230}px` : '84px'}" @wheel.prevent="scrollChat" @click="handleMessageAction" @contextmenu.prevent="openContextMenu">
        <div class="messages-inner">
          <MessageRow v-for="record in channelHistory" :key="record.id" :record="record" :reactions="messageReactions[record.id]" />
          <article v-if="!channelHistory.length" class="message system-message"><div class="system-row"><span class="system-glyph">#</span><b>这里还没有消息，发送第一条消息吧。</b></div></article>
        </div>
      </section>
      <div v-if="toast" class="toast community-toast">{{ toast }}</div>
      <div v-if="typingUsers.length && active!=='服务器指南'" class="typing-row" aria-live="polite">
        <span class="typing-dots" aria-hidden="true"><i></i><i></i><i></i></span>
        <b>{{typingUsers[0].author}}</b>
        <span v-if="typingUsers[0].tag" class="tag"><span class="tag-icon">{{typingUsers[0].tag.icon}}</span>{{typingUsers[0].tag.label}}</span>
        <span class="typing-text">正在输入…</span>
      </div>
      <Composer v-if="active!=='服务器指南'" :conversation-key="active" :placeholder="`给 ${active} 发消息`" />
    </main>
    <main v-else-if="activePage==='dm'" class="friends-main" @click="friendMoreMenu='';dmQuickMenu=false">
      <section class="friends-column">
      <header class="friends-page-header"><div class="friends-page-title"><span><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="8.2" r="3.5"/><path d="M5.8 19.6a6.2 6.2 0 0 1 12.4 0"/></svg></span><b>好友</b></div><div class="friends-filter-tabs"><button v-for="tab in ['全部','待定','群聊']" :key="tab" :class="{active:friendFilter===tab}" @click="friendFilter=tab;friendMoreMenu=''">{{tab}}<span v-if="tab==='待定'&&demoFriendRequests.length+demoGroupRequests.length+demoCommunityRequests.length" class="friends-tab-dot" aria-hidden="true"></span></button><button class="friends-add-button header-add-friend" @click="friendRequestModal=true">添加好友</button></div></header>
      <section class="friends-page-scroll">
        <template v-if="friendFilter==='待定'">
          <div class="friends-list-heading"><div><h2>待处理</h2><p>好友申请、群聊邀请与社区申请都集中在这里。</p></div><span>{{demoFriendRequests.length+demoGroupRequests.length+demoCommunityRequests.length}} 项</span></div>
          <section class="friends-request-section"><h3>好友申请 <small>{{demoFriendRequests.length}}</small></h3><article v-for="request in demoFriendRequests" :key="`friend-${request.uid}`" class="friends-request-row"><span class="friends-avatar"><img :src="avatarByUid(request.uid)" :alt="request.name"></span><div class="friends-row-copy"><b>{{request.name}} <small>UID {{request.uid}}</small></b><p>{{request.outgoing?'等待对方回应':request.note}}</p></div><span v-if="request.outgoing" class="friends-pending-label">等待回应</span><template v-else><button class="friends-soft-button" @click="handleDemoFriendRequest(request,false)">忽略</button><button class="friends-primary-button" @click="handleDemoFriendRequest(request,true)">接受</button></template></article><div v-if="!demoFriendRequests.length" class="friends-empty">没有待处理的好友申请</div></section>
          <section class="friends-request-section"><h3>群聊邀请 <small>{{demoGroupRequests.length}}</small></h3><article v-for="request in demoGroupRequests" :key="`group-${request.uid}-${request.group}`" class="friends-request-row"><span class="friends-avatar group"><img :src="DEFAULT_GROUP_AVATAR" alt="群聊"></span><div class="friends-row-copy"><b>{{request.group}} <small>群 UID {{request.groupUid}}</small></b><p>{{request.outgoing?'你已申请加入，等待群主回应':`${request.name} 申请加入群聊`}}</p></div><span v-if="request.outgoing" class="friends-pending-label">等待回应</span><template v-else><button class="friends-soft-button" @click="handleDemoGroupRequest(request,false)">拒绝</button><button class="friends-primary-button" @click="handleDemoGroupRequest(request,true)">接受</button></template></article><div v-if="!demoGroupRequests.length" class="friends-empty">没有待处理的群聊邀请</div></section>
          <section class="friends-request-section"><h3>社区申请 <small>{{demoCommunityRequests.length}}</small></h3><article v-for="request in demoCommunityRequests" :key="`community-${request.uid}-${request.community}`" class="friends-request-row"><span class="friends-avatar community"><img :src="DEFAULT_COMM_AVATAR" alt="社区"></span><div class="friends-row-copy"><b>{{request.community}} <small>社区</small></b><p>{{request.outgoing?'你已申请加入，等待社区管理员回应':`${request.name} 申请加入社区`}}<template v-if="request.note"> · {{request.note}}</template></p></div><span v-if="request.outgoing" class="friends-pending-label">等待回应</span><template v-else><button class="friends-soft-button" @click="handleDemoCommunityRequest(request,false)">拒绝</button><button class="friends-primary-button" @click="handleDemoCommunityRequest(request,true)">接受</button></template></article><div v-if="!demoCommunityRequests.length" class="friends-empty">没有待处理的社区申请</div></section>
        </template>
        <template v-else-if="friendFilter==='群聊'">
          <div class="friends-list-heading"><div><h2>群聊通讯录</h2><p>创建群聊、打开对话并管理群成员。</p></div><button class="friends-add-button" @click="createGroupFormOpen=!createGroupFormOpen">＋ 创建群聊</button></div>
          <div v-if="createGroupFormOpen" class="friends-add-panel compact"><label>群聊名称<div><input v-model="demoForm.groupName" placeholder="例如：周末旅行计划" @keydown.enter.prevent="createDemoGroup"><button class="friends-add-button" @click="createDemoGroup();createGroupFormOpen=false">创建群聊</button></div></label></div>
          <article v-for="group in demoGroups" :key="group.uid" class="friend-group-card"><header><span class="friends-avatar group"><img :src="DEFAULT_GROUP_AVATAR" alt="群聊"></span><div class="friends-row-copy"><b>{{group.name}}</b><p>群 UID {{group.uid}} · {{group.members.length}} 位成员</p></div><button class="friends-soft-button" @click="openGroupChat(group)">打开聊天</button><button class="friends-soft-button" @click="renameDemoGroup(group)">改名</button><button class="friends-icon-button danger" title="解散群聊" @click="deleteDemoGroup(group)">×</button></header><div class="friend-group-members"><div v-for="member in group.members" :key="member.uid" class="friend-group-member"><span class="friends-avatar tiny"><img :src="avatarByUid(member.uid)" :alt="member.name"></span><span class="friend-member-name"><b>{{member.name}}</b><small>UID {{member.uid}}</small></span><span class="friend-role-tag">{{member.role}}</span><button v-if="Number(member.uid)!==Number(backend.uid)" class="friends-soft-button compact-button" @click="toggleDemoGroupRole(group,member)">设为群主</button><button v-if="Number(member.uid)!==Number(backend.uid)" class="friends-icon-button danger" title="移除成员" @click="removeDemoGroupMember(group,member)">−</button></div></div><footer><input v-model="demoForm.memberUid" placeholder="输入好友 UID 邀请加入"><button class="friends-soft-button" @click="addDemoGroupMember(group)">邀请好友</button></footer></article>
          <div v-if="!demoGroups.length" class="friends-empty">还没有群聊，创建一个开始聊天吧。</div>
        </template>
        <template v-else>
          <label class="friends-directory-search"><svg class="search-glyph" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.4"/><path d="m15.4 15.4 4.6 4.6"/></svg><input v-model="friendSearchQuery" placeholder="搜索好友"></label>
          <div class="friends-list-heading"><div><h2>{{friendFilter==='在线'?'在线':'全部'}} — {{filteredFriendDirectory.length}}</h2></div><button v-if="friendFilter==='全部'" class="friends-create-group-link" @click="friendFilter='群聊';createGroupFormOpen=true">＋ 新建群聊</button></div>
          <article v-for="friend in filteredFriendDirectory" :key="friend.uid" class="friend-directory-row" @contextmenu.prevent="friendMoreMenu=friend.uid"><span class="friends-avatar friend-face"><img :src="avatarByUid(friend.uid)" :alt="friend.name"><i :class="{offline:!friend.online,idle:friend.idle}"></i></span><div class="friends-row-copy"><div class="friends-name-line"><b>{{friend.remark||friend.name}}</b></div><p>{{friend.idle?'闲置':friend.online?'在线':'离线'}}</p></div><button class="friends-round-action" title="发送消息" aria-label="发送消息" @click="openDemoFriendChat(friend)"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 11.6c0 3.6-3.6 6.5-8 6.5a9.8 9.8 0 0 1-2.6-.3L5 20l1.2-3.4A6.2 6.2 0 0 1 4 11.6C4 8 7.6 5.1 12 5.1s8 2.9 8 6.5Z"/></svg></button><div class="friend-more-wrap"><button class="friends-round-action" title="更多" aria-label="更多" @click.stop="friendMoreMenu=friendMoreMenu===friend.uid?'':friend.uid"><svg class="dots" viewBox="0 0 24 24" aria-hidden="true"><circle cx="5" cy="12" r="1.7"/><circle cx="12" cy="12" r="1.7"/><circle cx="19" cy="12" r="1.7"/></svg></button><div v-if="friendMoreMenu===friend.uid" class="friend-more-menu"><button @click="openDemoFriendChat(friend);friendMoreMenu=''">发送消息</button><button @click="renameDemoFriend(friend);friendMoreMenu=''">设置备注</button><button class="danger" @click="removeDemoFriend(friend);friendMoreMenu=''">删除好友</button></div></div></article>
          <div v-if="!filteredFriendDirectory.length" class="friends-empty">没有找到好友</div>
          <section v-if="friendFilter==='全部'" class="friends-group-preview"><div class="friends-list-heading"><div><h2>群聊</h2><p>你的群聊通讯录</p></div><button class="friends-create-group-link" @click="friendFilter='群聊'">管理群聊　›</button></div><button v-for="group in demoGroups" :key="group.uid" class="friends-group-row" @click="openGroupChat(group)"><span class="friends-avatar group"><img :src="DEFAULT_GROUP_AVATAR" alt="群聊"></span><span><b>{{group.name}}</b><small>{{group.members.length}} 位成员</small></span><span>›</span></button></section>
          <section v-if="friendFilter==='全部'" class="friends-group-preview friends-community-preview"><div class="friends-list-heading"><div><h2>社区</h2><p>你加入的社区</p></div><button class="friends-create-group-link" @click="notDeveloped('发现社区')">发现社区　›</button></div><button v-for="community in communities" :key="community.name" class="friends-group-row" @click="selectCommunity(community);active='服务器指南'"><span class="friends-avatar community"><img :src="community.icon || DEFAULT_COMM_AVATAR" :alt="community.name"></span><span><b>{{community.name}}</b><small>{{community.description||'社区成员'}}</small></span><span>›</span></button><div v-if="!communities.length" class="friends-empty">还没有加入社区。</div></section>
        </template>
      </section><div v-if="toast" class="toast friends-toast">{{toast}}</div>
      </section>
    </main>
    <main v-else-if="activePage==='dm-chat'" class="dm-main" @click="closeComposerPopovers">
      <header class="dm-top"><div><span class="dm-avatar"><img :src="dmAvatarSrc(selectedDm)" :alt="selectedDm"></span><b>{{selectedDm}}</b><small>{{dmContacts.find(c=>c.name===selectedDm)?.kind}}</small></div><nav><button title="语音通话" @click="notDeveloped('语音通话')">⌕</button><button title="置顶" @click="notDeveloped('置顶消息')">♧</button><button title="搜索" @click="notDeveloped('消息搜索')">⌕</button></nav></header>
      <section class="dm-thread" @wheel.prevent="scrollChat" @click="handleMessageAction" @contextmenu.prevent="openContextMenu"><div class="dm-thread-intro"><span class="dm-avatar large"><img :src="dmAvatarSrc(selectedDm)" :alt="selectedDm"></span><h2>{{selectedDm}}</h2><p>这是你和 {{selectedDm}} 的私信开头。</p></div><div class="dm-thread-messages"><MessageRow v-for="entry in dmThread" :key="entry.id" :record="entry" :reactions="messageReactions[entry.id]" :images="entry.images" /></div></section>
      <Composer :conversation-key="`dm:${selectedDm}`" :placeholder="`给 ${selectedDm} 发消息`" />
      <div v-if="toast" class="toast">{{toast}}</div>
    </main>
    <main v-else class="discover-page" aria-label="发现页面"></main>
  </div>
  <!-- 消息右键菜单 / emoji 选择器 / 查看反应：fixed 定位，社区频道与私信、群聊共用 -->
  <div v-if="contextMenu" class="context-menu" :style="{left: `${contextMenu.x}px`, top: `${contextMenu.y}px`}" @click.stop="handleMessageAction">
    <div class="context-reactions"><button v-for="emoji in QUICK_EMOJIS" :key="emoji" data-action="react" :data-emoji="emoji">{{ emoji }}</button></div>
    <button class="context-item" data-action="picker"><span>☻</span>添加反应 <b>›</b></button>
    <button v-if="messageReactions[contextMenu.id]?.length" class="context-item" data-action="view-reactions"><span><Icon name="smile"/></span>查看反应</button>
    <div class="context-separator"></div>
    <button class="context-item" data-action="reply"><span><svg viewBox="0 0 24 24"><polyline points="9 14 4 9 9 4"/><path d="M20 20v-7a4 4 0 0 0-4-4H4"/></svg></span>回复</button>
    <button class="context-item" data-action="forward"><span><svg viewBox="0 0 24 24"><polyline points="15 14 20 9 15 4"/><path d="M4 20v-7a4 4 0 0 1 4-4h12"/></svg></span>转发</button>
    <div class="context-separator"></div>
    <button v-if="contextMenu.attachment" class="context-item" data-action="download"><span><svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg></span>下载</button>
    <button class="context-item" data-action="copy"><span><svg viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg></span>复制文字</button>
    <button class="context-item" data-action="unread"><span><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3" fill="currentColor" stroke="none"/></svg></span>标记未读</button>
    <button class="context-item" data-action="recall"><span><svg viewBox="0 0 24 24"><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/></svg></span>撤回</button>
    <button v-if="contextMenu.mine" class="context-item delete-message-action" data-action="delete"><span><svg viewBox="0 0 24 24"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg></span>删除消息</button>
    <button class="context-item" data-action="link"><span><svg viewBox="0 0 24 24"><path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/></svg></span>复制消息链接</button>
    <div class="context-separator"></div>
    <button class="context-item report" data-action="report"><span><svg viewBox="0 0 24 24"><path d="M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z"/><line x1="4" y1="22" x2="4" y2="15"/></svg></span>举报消息</button>
  </div>
  <div v-if="emojiPicker" class="emoji-picker" :data-message-id="emojiPicker.id" :style="{left: `${emojiPicker.x}px`, top: `${emojiPicker.y}px`, width: 'auto', minWidth: '96px', gridTemplateColumns: 'repeat(2, 1fr)', gap: '5px', padding: '8px'}" @click.stop="handleMessageAction">
    <button v-for="emoji in PICKER_EMOJIS" :key="emoji" data-action="react" :data-emoji="emoji" style="height:42px;min-width:44px;font-size:22px;border-radius:6px;background:transparent">{{ emoji }}</button>
    <button type="button" @click.stop="openEmojiPanelFromReaction" style="grid-column:1 / -1;height:34px;font-size:12px;display:flex;align-items:center;justify-content:center;background:#303137;color:#d0d1d6;border-radius:6px;cursor:pointer">更多反应</button>
  </div>
  <div v-if="reactionDetails" class="reaction-backdrop" @click.self="reactionDetails = null">
    <div class="reaction-modal">
      <div class="reaction-modal-head"><b>查看反应</b><button @click="reactionDetails = null" aria-label="关闭">×</button></div>
      <div v-for="reaction in reactionDetails.items" :key="reaction.emoji" class="reaction-group"><div class="reaction-group-title">{{ reaction.emoji }} <span>{{ reaction.count }}</span></div><div v-for="person in reaction.users" :key="person" class="reaction-person"><span class="person-avatar">{{ person[0] }}</span><b>{{ person }}</b><small>已回应</small></div></div>
      <div v-if="!reactionDetails.items.length" class="empty-reactions">还没有可显示的反应</div>
    </div>
  </div>
  <div v-if="friendRequestModal" class="friend-request-backdrop" @click.self="friendRequestModal=false">
    <section class="friend-request-window" role="dialog" aria-modal="true" aria-labelledby="friend-request-title">
      <button class="friend-request-close" aria-label="关闭" @click="friendRequestModal=false">×</button>
      <div class="friend-request-emblem">♧</div>
      <h1 id="friend-request-title">添加好友</h1>
      <p>通过邮箱向朋友发送好友申请。</p>
      <label>好友邮箱<input v-model="demoForm.email" type="email" placeholder="name@example.com" @keydown.enter.prevent="sendFriendRequestFromModal"></label>
      <label>申请留言 <span>可选</span><textarea v-model="demoForm.note" rows="4" placeholder="写一句话介绍自己"></textarea></label>
      <footer><button class="friend-request-cancel" @click="friendRequestModal=false">取消</button><button class="friend-request-submit" @click="sendFriendRequestFromModal">发送好友申请</button></footer>
    </section>
  </div>
  <div v-if="demoLoginOpen" class="demo-login-backdrop" @click.self="backend.uid&&(demoLoginOpen=false)">
    <section class="demo-login-window" role="dialog" aria-modal="true" aria-labelledby="demo-login-title">
      <button v-if="backend.uid" class="friend-request-close" aria-label="关闭" @click="demoLoginOpen=false">×</button>
      <div class="demo-login-logo"><img src="/logo.png" alt="LOGO"></div>
      <h1 id="demo-login-title">欢迎回来</h1><p>{{backend.connecting?'正在连接服务器…':backend.connected?'登录账号，继续使用 Baka Community。':'服务器连接失败，请稍后重试。'}}</p>
      <div class="demo-login-tabs"><button :class="{active:demoAuthMode==='登录'}" @click="demoAuthMode='登录'">登录</button><button :class="{active:demoAuthMode==='注册'}" @click="demoAuthMode='注册'">注册</button></div>
      <label v-if="demoAuthMode==='注册'">昵称<input v-model="demoAuth.username" placeholder="例如：小明"></label>
      <label v-if="demoAuthMode==='注册'">邮箱<input v-model="demoAuth.email" placeholder="name@example.com"></label>
      <label v-else>UID 或邮箱<input v-model="demoAuth.identity" placeholder="100001 / name@example.com"></label>
      <label>密码<input v-model="demoAuth.password" type="password" placeholder="输入密码" @keydown.enter.prevent="submitDemoLoginWindow"></label>
      <button class="demo-login-submit" @click="backend.connected?submitDemoLoginWindow():api.connect()">{{backend.connecting?'连接中…':backend.connected?demoAuthMode:'重新连接'}}</button><small>数据通过 WebSocket + protobuf 与服务器同步。</small>
    </section>
  </div>
  <div v-if="demoHubOpen" class="demo-hub-backdrop" @click.self="demoHubOpen=false">
    <section class="demo-hub" role="dialog" aria-modal="true" aria-label="客户端功能工作台">
      <aside class="demo-hub-nav"><div class="demo-brand"><span>◈</span><div><b>客户端功能</b><small>服务器管理工作台</small></div><button aria-label="关闭" @click="demoHubOpen=false">×</button></div><div class="demo-nav-label">功能目录</div><button v-for="item in ['账号','联系人','群聊','社区','文件']" :key="item" :class="{active:demoTab===item}" @click="demoTab=item"><span>{{demoTabIcon(item)}}</span>{{item}}</button><button class="demo-nav-login" @click="openDemoLoginWindow"><span>⇥</span>登录窗口</button><div class="demo-nav-foot"><span class="demo-live-dot"></span>{{backend.connected?'已连接服务器':'服务器离线'}}</div></aside>
      <main class="demo-hub-main"><header><div><div class="demo-eyebrow">BAKA COMMUNITY</div><h1>{{demoTab}}管理</h1><p>数据与当前服务器实时同步。</p></div><div class="demo-user-pill"><span>{{demoNickname.slice(0,1)}}</span><div><b>{{demoNickname}}</b><small>UID {{demoUid}}</small></div></div></header>
        <div class="demo-hub-scroll">
          <section v-if="demoTab==='账号'" class="demo-section">
            <div v-if="demoSignedIn" class="demo-account-card"><div class="demo-account-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" alt="账号头像"><span v-else>{{demoNickname.slice(0,1)}}</span><i></i></div><div class="demo-account-copy"><span class="demo-status-tag"><i></i> 已登录</span><h2>{{demoNickname}}</h2><p>UID {{demoUid}} <span>·</span> {{demoEmail}}</p><small>当前账号数据来自聊天服务器。</small></div></div>
            <div v-if="demoSignedIn" class="demo-form-card demo-profile-form"><div class="demo-form-title"><b>个人资料</b><small>昵称、邮箱和头像更新会提交到服务器</small></div><div class="demo-profile-fields"><input v-model="demoNicknameDraft" maxlength="32" placeholder="昵称"><input v-model="demoEmail" placeholder="邮箱"><button class="demo-soft-button" @click="setDemoAvatar">更换头像</button><button class="demo-primary-button" @click="saveDemoProfile">保存资料</button></div></div>
            <div v-if="demoSignedIn" class="demo-info-grid"><article><span>账户标识</span><b>{{demoUid}}</b><small>私聊和群聊使用 UID</small></article><article><span>联系人</span><b>{{demoFriends.length}}</b><small>服务器好友列表</small></article><article><span>文件</span><b>{{demoFiles.length}}</b><small>服务器文件与传输任务</small></article></div>
            <div v-if="demoSignedIn" class="demo-account-actions"><div><b>账号会话</b><small>可打开独立登录窗口，体验 UID / 邮箱登录和注册流程。</small></div><div class="demo-account-action-buttons"><button class="demo-soft-button" @click="openDemoLoginWindow">登录窗口</button><button class="demo-danger-button" @click="demoLogout">退出当前账号</button></div></div>
            <div v-else class="demo-auth-card"><div class="demo-auth-tabs"><button :class="{active:demoAuthMode==='登录'}" @click="demoAuthMode='登录'">登录</button><button :class="{active:demoAuthMode==='注册'}" @click="demoAuthMode='注册'">注册</button></div><h2>{{demoAuthMode}}账号</h2><p>{{demoAuthMode==='登录'?'请输入 UID 或邮箱及密码。':'填写昵称、邮箱和密码创建服务器账号。'}}</p><label v-if="demoAuthMode==='注册'">昵称<input v-model="demoAuth.username" placeholder="例如：小明"></label><label v-if="demoAuthMode==='注册'">邮箱<input v-model="demoAuth.email" placeholder="name@example.com"></label><label v-else>UID 或邮箱<input v-model="demoAuth.identity" placeholder="100001 / name@example.com"></label><label>密码<input v-model="demoAuth.password" type="password" placeholder="输入密码" @keydown.enter.prevent="submitDemoAuth"></label><button class="demo-primary-button wide" @click="submitDemoAuth">{{demoAuthMode}}</button><small class="demo-disclaimer">账号数据将提交到当前服务器。</small></div>
          </section>
          <section v-else-if="demoTab==='联系人'" class="demo-section">
            <div class="demo-panel-heading"><div><h2>好友与申请</h2><p>按 UID 展示联系人，通过邮箱发送好友申请。</p></div><span class="demo-count">{{demoFriends.length}} 位好友</span></div>
            <div class="demo-form-card"><div class="demo-form-title"><b>添加好友</b><small>通过邮箱发送好友申请</small></div><div class="demo-inline-fields"><input v-model="demoForm.email" placeholder="好友邮箱"><input v-model="demoForm.note" placeholder="申请留言（可选）"><button class="demo-primary-button" @click="requestDemoFriend">发送申请</button></div></div>
            <div class="demo-panel-heading compact"><div><h3>待处理申请</h3><p>接受后会加入好友列表。</p></div><span class="demo-count">{{demoFriendRequests.length}}</span></div>
            <div v-if="demoFriendRequests.length" class="demo-request-list"><article v-for="request in demoFriendRequests" :key="request.uid" class="demo-list-row"><span class="demo-person-avatar">{{request.name[0]}}</span><div class="demo-row-copy"><b>{{request.name}} <small>UID {{request.uid}}</small></b><p>{{request.outgoing?'等待对方回应':request.note}}</p></div><div v-if="request.outgoing" class="demo-pending-tag">等待回应</div><template v-else><button class="demo-soft-button" @click="handleDemoFriendRequest(request,false)">拒绝</button><button class="demo-primary-button" @click="handleDemoFriendRequest(request,true)">接受</button></template></article></div><div v-else class="demo-empty-state">目前没有待处理的好友申请。</div>
            <div class="demo-panel-heading compact"><div><h3>我的好友</h3><p>备注名保存到服务器。</p></div><label class="demo-search"><svg class="search-glyph" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.4"/><path d="m15.4 15.4 4.6 4.6"/></svg><input v-model="demoSearch" placeholder="搜索好友或 UID"></label></div>
            <div class="demo-contact-list"><article v-for="friend in filteredDemoFriends" :key="friend.uid" class="demo-list-row"><span class="demo-person-avatar friend">{{friend.avatar}}</span><div class="demo-row-copy"><b>{{friend.remark||friend.name}}</b><p>{{friend.email}} <span>·</span> UID {{friend.uid}}</p></div><button class="demo-soft-button" @click="openDemoFriendChat(friend)">发消息</button><button class="demo-icon-action" title="设置备注" @click="renameDemoFriend(friend)">✎</button><button class="demo-icon-action danger" title="删除好友" @click="removeDemoFriend(friend)">×</button></article><div v-if="!filteredDemoFriends.length" class="demo-empty-state">没有匹配的好友。</div></div>
          </section>
          <section v-else-if="demoTab==='群聊'" class="demo-section">
            <div class="demo-panel-heading"><div><h2>群聊管理</h2><p>创建群聊、邀请好友、管理群成员与入群申请。</p></div><span class="demo-count">{{demoGroups.length}} 个群聊</span></div>
            <div class="demo-form-card"><div class="demo-form-title"><b>创建群聊</b><small>群聊创建后可直接在左侧私信列表打开</small></div><div class="demo-inline-fields"><input v-model="demoForm.groupName" placeholder="输入群聊名称" @keydown.enter.prevent="createDemoGroup"><button class="demo-primary-button" @click="createDemoGroup">创建群聊</button></div></div>
            <div v-if="demoGroupRequests.length" class="demo-request-list"><article v-for="request in demoGroupRequests" :key="request.uid" class="demo-list-row"><span class="demo-person-avatar">{{request.name[0]}}</span><div class="demo-row-copy"><b>{{request.name}} <small>UID {{request.uid}}</small></b><p>{{request.outgoing?'等待「'+request.group+'」群主回应':'申请加入「'+request.group+'」'}}</p></div><button v-if="request.outgoing" class="demo-soft-button" @click="handleDemoGroupRequest(request,false)">撤回</button><template v-else><button class="demo-soft-button" @click="handleDemoGroupRequest(request,false)">拒绝</button><button class="demo-primary-button" @click="handleDemoGroupRequest(request,true)">接受</button></template></article></div><div v-else class="demo-empty-state">没有待处理的入群申请。</div>
            <article v-for="group in demoGroups" :key="group.uid" class="demo-group-card"><header><div class="demo-group-avatar">👥</div><div class="demo-row-copy"><h3>{{group.name}}</h3><p>群 UID {{group.uid}} · {{group.members.length}} 位成员</p></div><button class="demo-soft-button" @click="openGroupChat(group);demoHubOpen=false">打开聊天</button><button class="demo-soft-button" @click="requestDemoJoinGroup(group)">申请加入</button><button class="demo-soft-button" @click="renameDemoGroup(group)">改名</button><button class="demo-icon-action danger" title="解散群聊" @click="deleteDemoGroup(group)">×</button></header><div class="demo-member-list"><div v-for="member in group.members" :key="member.uid" class="demo-member-row"><span class="demo-person-avatar tiny">{{member.name[0]}}</span><span><b>{{member.name}}</b><small>UID {{member.uid}}</small></span><span class="demo-role">{{member.role}}</span><button v-if="Number(member.uid)!==Number(backend.uid)" class="demo-soft-button" @click="toggleDemoGroupRole(group,member)">设为群主</button><button v-if="Number(member.uid)!==Number(backend.uid)" class="demo-icon-action danger" title="移除成员" @click="removeDemoGroupMember(group,member)">−</button></div></div><div class="demo-add-member"><input v-model="demoForm.memberUid" placeholder="输入好友 UID"><button class="demo-soft-button" @click="addDemoGroupMember(group)">邀请好友</button><button class="demo-soft-button" @click="demoHubOpen=false;activePage='dm';friendFilter='全部'">查找好友 UID</button></div></article>
          </section>
          <section v-else-if="demoTab==='社区'" class="demo-section">
            <div class="demo-panel-heading"><div><h2>社区与成员</h2><p>管理加入申请、角色和社区成员。</p></div><button class="demo-primary-button" @click="demoHubOpen=false;createCommunityOpen=true">＋ 创建社区</button></div>
            <div class="demo-form-card"><div class="demo-form-title"><b>加入社区</b><small>输入社区 ID 提交加入申请</small></div><div class="demo-inline-fields"><input v-model="demoForm.joinCode" placeholder="邀请链接 / 社区 ID"><button class="demo-primary-button" @click="joinDemoCommunity">提交申请</button></div></div>
            <div class="demo-panel-heading compact"><div><h3>加入申请</h3><p>处理发往你所管理社区的申请。</p></div><span class="demo-count">{{demoCommunityRequests.length}}</span></div><div v-if="demoCommunityRequests.length" class="demo-request-list"><article v-for="request in demoCommunityRequests" :key="`${request.uid}-${request.community}`" class="demo-list-row"><span class="demo-person-avatar">{{request.name[0]}}</span><div class="demo-row-copy"><b>{{request.name}} <small>UID {{request.uid}}</small></b><p>{{request.outgoing?'等待社区管理员回应':`申请加入「${request.community}」`}} · {{request.note}}</p></div><button v-if="request.outgoing" class="demo-soft-button" @click="notDeveloped('撤回社区申请')">撤回</button><template v-else><button class="demo-soft-button" @click="handleDemoCommunityRequest(request,false)">拒绝</button><button class="demo-primary-button" @click="handleDemoCommunityRequest(request,true)">接受</button></template></article></div><div v-else class="demo-empty-state">没有待处理的社区申请。</div>
            <div class="demo-panel-heading compact"><div><h3>社区成员 · {{selectedCommunity}}</h3><p>查看成员并切换管理员身份。</p></div><span class="demo-count">{{demoCommunityMemberList.length}} 位成员</span></div><div class="demo-form-card"><div class="demo-inline-fields"><input v-model="demoForm.memberUid" placeholder="输入好友 UID 邀请加入社区"><button class="demo-primary-button" @click="addDemoCommunityMember">邀请好友</button><button class="demo-soft-button" @click="demoHubOpen=false;activePage='dm';friendFilter='全部'">查看好友</button></div></div><div class="demo-contact-list"><article v-for="member in demoCommunityMemberList" :key="member.uid" class="demo-list-row"><span class="demo-person-avatar">{{member.name[0]}}</span><div class="demo-row-copy"><b>{{member.name}}</b><p>UID {{member.uid}}</p></div><span class="demo-role" :class="member.role">{{member.role==='owner'?'所有者':member.role==='admin'?'管理员':'成员'}}</span><button v-if="member.role!=='owner'" class="demo-soft-button" @click="toggleDemoMemberRole(member)">{{member.role==='admin'?'降为成员':'设为管理员'}}</button><button v-if="member.role!=='owner'" class="demo-icon-action danger" title="移除成员" @click="removeDemoCommunityMember(member)">−</button></article></div><div class="demo-community-switcher"><b>切换社区</b><button v-for="community in communities" :key="community.name" :class="{selected:selectedCommunity===community.name}" @click="selectedCommunity=community.name;activePage='community'"><span class="community-mark"><img :src="community.icon || DEFAULT_COMM_AVATAR" :alt="community.name"></span>{{community.name}}</button></div>
          </section>
          <section v-else class="demo-section">
            <div class="demo-panel-heading"><div><h2>我的文件</h2><p>文件保存在服务器，上传与下载均使用分片传输。</p></div><button class="demo-primary-button" @click="selectDemoFiles">＋ 上传文件</button></div><input ref="demoFilesInput" class="demo-hidden-input" type="file" multiple @change="addDemoFiles"><input ref="resumeFileInput" class="demo-hidden-input" type="file" @change="bindResumeUpload">
            <div class="demo-transfer-card"><div class="demo-transfer-icon">⇅</div><div><b>{{activeTransfer?.name||'文件传输状态'}}</b><small>{{activeTransfer?`${activeTransfer.status} · ${activeTransfer.progress||0}%`:'当前没有进行中的传输'}}</small></div><span class="demo-transfer-state"><i :class="{paused:!activeTransfer||activeTransfer.status==='已暂停'}"></i>{{activeTransfer?.status||'空闲'}}</span><button v-if="activeTransfer?.transferId" class="demo-soft-button" @click="toggleActiveTransfer">{{['已暂停','已中断','等待选择原文件'].includes(activeTransfer.status)?'继续':'暂停'}}</button><button v-if="activeTransfer?.transferId" class="demo-icon-action danger" title="取消传输" @click="api.cancelTransfer(activeTransfer)">×</button></div>
            <div class="demo-file-list"><article v-for="file in demoFiles" :key="file.id" class="demo-file-row"><div class="demo-file-thumb"><img v-if="file.url&&file.type==='image'" :src="file.url" :alt="file.name"><span v-else class="demo-file-ext" :class="file.format.toLowerCase()">{{file.format}}</span></div><div class="demo-row-copy"><b>{{file.name}}</b><p>{{file.format}} · {{formatFileSize(file.size)}} <span>·</span> {{file.status}}</p><div class="demo-file-progress"><i :class="{paused:file.status==='已暂停'}" :style="{width:`${file.progress??(file.status==='已完成'?100:0)}%`}"></i></div></div><div class="demo-file-actions"><button v-if="file.status==='已完成'" class="demo-soft-button" @click="downloadDemoFile(file)">下载</button><button v-if="file.localId&&file.transferId&&!['已完成','已取消','失败','正在取消'].includes(file.status)" class="demo-soft-button" @click="toggleFileTransfer(file)">{{['已暂停','已中断','等待选择原文件'].includes(file.status)?(file.status==='等待选择原文件'?'选择原文件续传':'继续'):'暂停'}}</button><button class="demo-icon-action danger" title="删除文件" @click="deleteDemoFile(file)">×</button></div></article><div v-if="!demoFiles.length" class="demo-empty-state">还没有文件，上传一个文件开始使用。</div></div>
            <div class="demo-doc-note"><b>传输操作</b><span>上传初始化 · 文件分片 · 完成校验 · 断点续传 · 下载 · 暂停 / 继续 / 取消 · 文件列表 / 删除</span><small>文件通过 WebSocket 分片传输，并由服务器持久化。</small></div>
          </section>
        </div>
      </main>
    </section>
  </div>
  <div v-if="memberProfile" class="member-profile-backdrop" @click.self="memberProfile=null">
    <section class="member-profile-card" role="dialog" aria-modal="true" aria-label="成员个人资料">
      <button class="member-profile-close" aria-label="关闭" @click="memberProfile=null">×</button>
      <div class="member-profile-banner"></div>
      <div class="member-profile-content"><div class="member-profile-avatar" :class="memberProfile.avatar">{{memberProfile.avatarText}}</div><span class="member-online-dot"></span><h2>{{memberProfile.name}}</h2><p class="member-profile-handle">UID <span>{{memberProfile.uid||'暂未提供'}}</span></p><div class="member-profile-actions"><button @click="message=`@${memberProfile.name} `;memberProfile=null;nextTick(()=>composerInput?.focus())">消息</button><button title="更多" @click="notDeveloped('成员更多操作')">•••</button></div><div class="member-profile-about"><label>关于我</label><p>暂未提供</p><label>服务器成员</label><p>{{selectedCommunity||'暂未提供'}}</p><label>成员加入时间</label><p>暂未提供</p></div></div>
    </section>
  </div>
  <div v-if="communitySettingsOpen" class="community-settings-backdrop" @click.self="closeCommunitySettings()">
    <section class="community-settings-window" role="dialog" aria-modal="true" aria-label="社区设置">
      <aside class="community-settings-sidebar"><header><span class="community-settings-icon"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="communitySettingsName"></span><div><b>{{communitySettingsName}}</b><small>社区设置</small></div><button aria-label="关闭社区设置" @click="closeCommunitySettings()">×</button></header>
        <nav><button class="selected" @click="scrollToCommunitySection('community-overview')"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v5m0-8h.01"/></svg>社区概览</button><button @click="scrollToCommunitySection('community-guide')"><Icon name="doc"/>服务器指南</button><button @click="scrollToCommunitySection('community-channels')"><Icon name="list"/>频道管理</button><button class="dissolve-nav" @click="dissolveConfirm=true"><svg viewBox="0 0 24 24"><path d="M4 7h16M10 11v6m4-6v6M6 7l1 14h10l1-14M9 7V4h6v3"/></svg>解散社区</button></nav>
      </aside>
      <main class="community-settings-main"><header class="community-settings-header"><b>{{communitySettingsName}} 设置</b><button aria-label="关闭社区设置" @click="closeCommunitySettings()">×</button></header>
        <div class="community-settings-scroll">
          <section id="community-overview" class="community-setting-section"><h1>社区概览</h1><p class="community-settings-hint">管理社区的名称、介绍和外观。</p>
            <div class="community-setting-row"><div class="community-setting-label"><b>社区头像</b><small>选择一张图片作为社区标识。</small></div><div class="community-avatar-editor"><span class="community-settings-icon large"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="communitySettingsName"></span><label class="settings-upload-button">更换头像<input type="file" accept="image/*" @change="handleCommunityAsset('avatar',$event)"></label></div></div>
            <label class="community-field"><span>社区名称</span><input v-model="communitySettingsName" maxlength="60" placeholder="输入社区名称"></label>
            <label class="community-field"><span>介绍文字</span><textarea v-model="communitySettingsDescription" maxlength="300" rows="3" placeholder="介绍一下你的社区"></textarea><small>{{communitySettingsDescription.length}} / 300</small></label>
            <div class="community-setting-row background-upload-row"><div class="community-setting-label"><b>背景图片</b><small>显示在社区侧栏顶部。</small></div><label class="settings-upload-button">上传背景图片<input type="file" accept="image/*" @change="handleCommunityAsset('banner',$event)"></label></div>
            <div class="community-banner-preview" :style="currentCommunity?.background?{backgroundImage:`url(${currentCommunity.background})`}:{}"><span>{{communitySettingsName}}</span></div>
          </section>
        <section class="community-setting-section"><h2>服务器资料</h2><p class="community-settings-hint">头像、背景图片与名称会同步显示在竖栏和顶部横幅。</p>
          <div class="community-asset-row"><span class="community-asset-preview avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><div><b>服务器头像</b><small>建议 512×512 的方形图片，显示在左侧竖栏</small></div><button @click="setCommunityAsset('avatar')">上传头像</button><input ref="communityAvatarInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleCommunityAsset('avatar',$event)"></div>
          <div class="community-asset-row"><span class="community-asset-preview banner" :style="currentCommunity?.background?{backgroundImage:`url(${currentCommunity.background})`}:{}"></span><div><b>服务器背景图片</b><small>显示在频道栏顶部的横幅与服务器指南</small></div><button @click="setCommunityAsset('banner')">上传背景</button><input ref="communityBannerInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleCommunityAsset('banner',$event)"></div>
        </section>
        <section id="community-guide" class="community-setting-section"><h2>服务器指南</h2><p class="community-settings-hint">左侧保留「服务器指南」入口；当前服务端尚未提供指南编辑与保存接口。</p>
          <label class="community-field"><span>指南名称</span><input v-model="guideDraftTitle" maxlength="30" placeholder="服务器指南"></label>
          <div v-if="guideDraftItems.length" class="managed-channel-list guide-item-list"><div v-for="(item,index) in guideDraftItems" :key="item.id"><span>▤</span><b>{{item.title}}</b><small>{{item.body.length}} 字</small><span class="managed-channel-actions"><button @click="removeGuideItem(index)">删除</button></span></div></div>
          <div v-else class="guide-draft-empty">还没有指南内容，用下面的表单添加第一条。</div>
          <div class="new-channel-form guide-item-form"><input v-model="newGuideItem.title" placeholder="指南标题（例如：频道规则）" @keydown.enter.prevent="addGuideItem"><input v-model="newGuideItem.body" placeholder="指南内容" @keydown.enter.prevent="addGuideItem"><button @click="addGuideItem">添加</button></div>
          <div class="community-setting-row guide-save-row"><div class="community-setting-label"><b>保存指南</b><small>把名称与卡片写入这个社区。</small></div><button class="guide-save" @click="saveCommunityGuide">保存服务器指南</button></div>
        </section>
        <section id="community-channels" class="community-setting-section"><h2>频道管理</h2><p class="community-settings-hint">下面是这个社区当前的频道列表，与左侧频道栏一致。鼠标移到频道上点右侧 × 删除，删除会在「保存更改」后生效。</p><div class="new-channel-form"><span>#</span><input v-model="newCommunityChannel" placeholder="新频道名称" @keydown.enter.prevent="addCommunityChannel"><button @click="addCommunityChannel">新增频道</button></div>
          <div class="settings-channel-groups"><template v-for="group in channelListOf(selectedCommunity)" :key="group.title"><div class="settings-channel-category" :class="{collapsed:isCategoryCollapsed(group)}"><button class="category-toggle" :aria-expanded="!isCategoryCollapsed(group)" :title="isCategoryCollapsed(group)?'展开分区':'收起分区'" @click="toggleCategory(group)"><span class="category-name">{{group.title}}</span><Icon name="caret" class="category-caret"/></button><span class="settings-channel-count">{{group.channels.length}}</span></div><template v-if="!isCategoryCollapsed(group)"><div v-for="channel in group.channels" :key="`${group.title}::${channel}`" class="settings-channel-row"><span class="channel-hash">#</span><span class="channel-label">{{channel}}</span><button class="settings-channel-remove" :aria-label="`删除频道 ${channel}`" :title="`删除频道 ${channel}`" @click.stop="openChannelDelete(selectedCommunity,group.title,channel)">×</button></div></template></template>
          <div v-if="!channelListOf(selectedCommunity).length" class="guide-draft-empty">这个社区还没有频道，用上面的输入框新增一个。</div>
        </div></section>
          <section class="community-setting-section danger-zone"><h2>解散社区</h2><p class="community-settings-hint">解散后，社区将从你的列表中移除。</p><button @click="dissolveConfirm=true">解散社区</button></section>
        </div>
        <footer class="community-settings-footer"><span>更改尚未保存</span><button @click="closeCommunitySettings()">取消</button><button class="save-community-settings" @click="saveCommunitySettings">保存更改</button></footer>
        <div v-if="channelDeleteTarget" class="server-dissolve-overlay"><section><h2>删除“{{channelDeleteTarget.channel}}”？</h2><p>该频道会从这个社区移除，点「保存更改」后生效。</p><footer><button @click="channelDeleteTarget=null">取消</button><button @click="confirmChannelDelete">删除频道</button></footer></section></div>
        <div v-if="dissolveConfirm" class="server-dissolve-overlay"><section><h2>解散“{{communitySettingsName}}”？</h2><p>此操作会从你的社区列表中移除该社区。</p><footer><button @click="dissolveConfirm=false">取消</button><button @click="confirmDissolveCommunity">解散社区</button></footer></section></div>
      </main>
    </section>
  </div>
  <div v-if="createCommunityOpen" class="dialog-backdrop" @click.self="createCommunityOpen=false"><section class="create-community-dialog"><button class="dialog-close" @click="createCommunityOpen=false">×</button><h2>创建社区</h2><p>为你的社区取个名字。</p><input v-model="newCommunityName" placeholder="社区名称" @keydown.enter.prevent="createCommunity"><footer><button @click="createCommunityOpen=false">取消</button><button class="create-community-submit" @click="createCommunity">创建社区</button></footer></section></div>
  <div v-if="inviteModal" class="dialog-backdrop invite-backdrop" @click.self="inviteModal = false">
    <section class="invite-dialog" role="dialog" aria-modal="true" aria-labelledby="invite-title">
      <button class="dialog-close" aria-label="关闭" @click="inviteModal = false">×</button>
      <div class="invite-content">
        <h2 id="invite-title">添加朋友到 {{selectedCommunity}}</h2>
        <p class="dialog-subtitle">当前服务端没有待接受的社区邀请；社区所有者会将好友直接加入。</p>
        <label class="friend-search"><Icon name="search"/><input v-model="friendSearch" placeholder="搜索好友"></label>
        <div class="friend-list">
          <div v-for="friend in filteredFriends" :key="friend.handle" class="friend-row"><span class="friend-avatar" :class="friend.color">{{ friend.avatar }}</span><span class="friend-names"><b>{{ friend.name }}</b><small>{{ friend.handle }}</small></span><button @click="inviteFriendToCommunity(friend)">直接添加</button></div>
          <div v-if="!filteredFriends.length" class="no-friends">没有找到好友</div>
        </div>
      </div>
      <div class="invite-link-panel"><label>或者，向好友发送服务器邀请链接</label><div class="invite-url"><input v-model="inviteUrl" readonly><button @click="copyInvite">复制</button></div><p>您的邀请链接将在 30 天后过期。 <button @click="notDeveloped('社区邀请链接')">编辑邀请链接</button></p></div>
    </section>
  </div>
  <div v-if="joinModal" class="dialog-backdrop join-backdrop" @click.self="joinModal = false">
    <section class="join-dialog" role="dialog" aria-modal="true" aria-labelledby="join-title">
      <button class="dialog-close" aria-label="关闭" @click="joinModal = false">×</button>
      <header><h2 id="join-title">加入服务器</h2><p>在下方输入邀请以加入现有的服务器</p></header>
      <label class="invite-code-label">邀请链接 <span>*</span></label>
      <input v-model="joinCode" class="invite-code-input" placeholder="https://discord.gg/hTKzmak" @keydown.enter.prevent="joinServer">
      <div class="format-label">邀请的格式应为：</div><div class="invite-examples"><button @click="joinCode = 'hTKzmak'">hTKzmak</button><button @click="joinCode = 'https://discord.gg/hTKzmak'">https://discord.gg/hTKzmak</button><button @click="joinCode = 'https://discord.gg/wumpus-friends'">https://discord.gg/wumpus-friends</button></div>
      <button class="discover-card" @click="joinModal = false; notDeveloped('发现社区') "><span class="discover-emblem">◈</span><span><b>您没有邀请？</b><small>前往“发现服务器”查看可发现的社区。</small></span><strong>›</strong></button>
      <footer><button class="back-button" @click="joinModal = false">后退</button><button class="join-submit" @click="joinServer">加入服务器</button></footer>
    </section>
  </div>
  <div v-if="settingsOpen" class="settings-backdrop" @click.self="closeSettings">
    <section class="settings-window" :class="{'settings-suspended':profileEditorOpen}" role="dialog" aria-modal="true" aria-label="用户设置">
      <aside class="settings-sidebar">
        <div class="settings-profile"><span class="settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span><span><b>{{demoNickname}}</b><button @click="profileEditorOpen=true">编辑个人资料　✎</button></span></div>
        <label class="settings-search"><Icon name="search"/><input placeholder="搜索" @focus="notDeveloped('设置搜索')"></label>
        <nav class="settings-nav">
          <button :class="{active:selectedSetting==='账户'}" @click="selectSetting('账户')"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.2"/><path d="M5 20v-1.2a7 7 0 0 1 14 0V20z"/></svg>账户</button>
          <button :class="{active:selectedSetting==='Baka'}" @click="selectSetting('Baka')"><img class="settings-nav-logo" src="/logo.png" alt="">Baka</button>
          <button :class="{active:selectedSetting==='数据与隐私'}" @click="selectSetting('数据与隐私')"><svg viewBox="0 0 24 24"><path d="M12 3 20 6v5c0 5-3.4 8.4-8 10-4.6-1.6-8-5-8-10V6z"/><path d="m9 12 2 2 4-4"/></svg>数据与隐私</button>
          <button :class="{active:selectedSetting==='消息权限'}" @click="selectSetting('消息权限')"><svg viewBox="0 0 24 24"><path d="M4 5h16v12H9l-5 4z"/><path d="M8 9h8m-8 4h5"/></svg>消息权限</button>
          <button :class="{active:selectedSetting==='通知'}" @click="selectSetting('通知')"><svg viewBox="0 0 24 24"><path d="M18 9a6 6 0 0 0-12 0c0 7-3 7-3 9h18c0-2-3-2-3-9M10 21h4"/></svg>通知</button>
          <div class="settings-divider"></div>
          <div class="settings-group-label">体验</div>
          <button :class="{active:selectedSetting==='外观'}" @click="selectSetting('外观')"><svg viewBox="0 0 24 24"><path d="M12 3a9 9 0 1 0 0 18h1.3a2 2 0 0 0 1.5-3.3 1.8 1.8 0 0 1 1.4-3h1.1A3.7 3.7 0 0 0 21 11c0-4.4-4-8-9-8Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7.5" r="1"/><circle cx="15" cy="8" r="1"/></svg>外观</button>
          <div v-if="selectedSetting==='外观'" class="settings-subnav">
            <button v-for="item in ['主题','应用图标','消息','聊天框','搜索','主播模式']" :key="item" :class="{selected:selectedAppearance===item}" @click="selectAppearance(item)">{{ item }}</button>
          </div>
          <button :class="{active:selectedSetting==='系统'}" @click="selectSetting('系统')"><svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="12" rx="2"/><path d="M8 21h8m-4-4v4M7 9h.01M10 9h.01M13 9h.01M16 9h.01M7 13h10"/></svg>系统</button>
          <button :class="{active:selectedSetting==='语言和时间'}" @click="selectSetting('语言和时间')"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/></svg>语言和时间</button>
          <button :class="{active:selectedSetting==='当前状态隐私'}" @click="selectSetting('当前状态隐私')"><svg viewBox="0 0 24 24"><circle cx="9" cy="8" r="3"/><path d="M3 20v-1a6 6 0 0 1 12 0v1m2-9 2 2 4-4"/></svg>当前状态隐私</button>
          <button :class="{active:selectedSetting==='已关联 APP'}" @click="selectSetting('已关联 APP')"><svg viewBox="0 0 24 24"><path d="M9 15 15 9m-8 8-2 2a4 4 0 0 1-6-6l5-5a4 4 0 0 1 6 0m3 0 2-2a4 4 0 0 1 6 6l-5 5a4 4 0 0 1-6 0" transform="translate(2 -1)"/></svg>已关联 APP</button>
          <div class="settings-divider"></div>
          <button class="logout-setting" @click="closeSettings();demoLogout();demoHubOpen=true;demoTab='账号'"><Icon name="logout"/>登出</button>
        </nav>
      </aside>
      <main class="settings-main">
        <header class="settings-header"><b>{{ selectedSetting === '外观' ? '外观' : selectedSetting }}</b><button aria-label="关闭设置" @click="closeSettings">×</button></header>
        <div class="settings-scroll">
          <div v-if="selectedSetting==='账户'" class="account-settings-content">
            <section class="account-section"><h1>账号信息</h1><div class="account-info-row"><span>用户名</span><b>{{demoNickname}}</b><button @click="profileEditorOpen=true">编辑</button></div><div class="account-info-row"><span>账号 UID</span><b>{{demoUid||'—'}} <button class="show-email" @click="notDeveloped('显示绑定邮箱')">查看邮箱</button></b><button @click="notDeveloped('编辑账号邮箱')">编辑</button></div><div class="account-info-row"><span>手机号码</span><b class="secondary-value">暂未支持手机号码。</b><button @click="notDeveloped('手机号码')">添加</button></div></section>
            <section class="account-section"><h2>密码和安全中心</h2><div class="account-info-row"><span>密码</span><b></b><button @click="notDeveloped('修改密码')">编辑</button></div><div class="account-info-row"><span>多重认证</span><b></b><button class="plain-action" @click="notDeveloped('多重认证')">设置　›</button></div><div class="account-info-row"><span>已登录的设备</span><b></b><button class="plain-action" @click="notDeveloped('登录设备管理')">暂未提供　›</button></div></section>
            <section class="account-section"><h2>账号信誉</h2><div class="standing-row"><span>✓</span><div><b>账号信誉</b><small>感谢您遵守 Discord 的<a>服务条款</a>和<a>社区守则</a>。如果您有违规行为，将会于此处显示。</small></div><button @click="notDeveloped('账号信誉')">暂未提供　›</button></div></section>
            <section class="account-section"><h2>家庭中心</h2><p>通过家庭中心了解并管理您的社交活动。</p></section>
          </div>
          <div v-else-if="selectedSetting==='外观' && selectedAppearance==='主题'" class="appearance-content">
            <h1>主题</h1>
            <div class="appearance-setting device-theme"><div><b>与设备主题相同</b><small>匹配您设备的浅色或深色模式。</small></div><button class="toggle" :class="{on:themeChoice==='device'}" @click="themeChoice=themeChoice==='device'?'dark':'device'"><i></i></button></div>
            <h3>默认主题</h3>
            <div class="theme-options"><button v-for="theme in ['light','gray','dark','black','device']" :key="theme" class="theme-swatch" :class="[theme,{chosen:themeChoice===theme}]" :aria-label="theme" @click="themeChoice=theme"><i v-if="themeChoice===theme">✓</i></button></div>
            <div class="appearance-setting"><div><b>在我的设备间同步主题</b><small>在此帐户登录的设备上使用相同的主题。</small></div><button class="toggle" :class="{on:syncTheme}" @click="notDeveloped('跨设备同步主题')"><i></i></button></div>
            <div class="appearance-setting"><div><b>将主题应用至其他用户的个人资料</b><small>在个人资料卡中显示您的主题风格。</small></div><button class="toggle" :class="{on:shareTheme}" @click="notDeveloped('资料主题共享')"><i></i></button></div>
            <div class="appearance-setting server-theme"><div><b>服务器默认主题</b><small>为此服务器中的频道使用的主题。</small></div><select v-model="serverTheme" @change="notDeveloped('服务器默认主题')"><option>使用服务器主题</option><option>浅色</option><option>深色</option><option>同步设备</option></select></div>
            <h3 class="related-heading">相关设置</h3>
          </div>
          <div v-else-if="selectedSetting==='Baka'" class="baka-settings-content">
            <section class="settings-section">
              <h1>Baka 音效</h1>
              <div class="settings-row"><div class="settings-row-copy"><b>音效文件</b><small>内置 Baka.mp3，本地播放，不联网。</small></div><img class="settings-row-logo" src="/logo.png" alt="Baka"></div>
              <div class="settings-row"><div class="settings-row-copy"><b>发送消息时播放</b><small>每次发出消息后播放一次。</small></div><button class="toggle" :class="{on:bakaOnSend}" @click="bakaOnSend=!bakaOnSend"><i></i></button></div>
              <div class="settings-row"><div class="settings-row-copy"><b>收到消息时播放</b><small>收到服务器实时推送的消息时播放一次。</small></div><button class="toggle" :class="{on:bakaOnReceive}" @click="bakaOnReceive=!bakaOnReceive"><i></i></button></div>
              <div class="settings-row"><div class="settings-row-copy"><b>双击 LOGO 播放</b><small>双击左侧竖栏顶部的 LOGO 播放一次。</small></div><button class="toggle" :class="{on:bakaOnLogo}" @click="bakaOnLogo=!bakaOnLogo"><i></i></button></div>
            </section>
            <section class="settings-section">
              <h1>试听</h1>
              <div class="settings-row"><div class="settings-row-copy"><b>播放一次 Baka.mp3</b><small>用来确认音量和音效是否合适。</small></div><button class="settings-row-button" @click="playBaka('logo')">试听</button></div>
            </section>
          </div>
          <div v-else class="settings-placeholder"><h1>{{ selectedSetting==='外观'?selectedAppearance:selectedSetting }}</h1><p>在这里调整 {{ selectedSetting==='外观'?selectedAppearance:selectedSetting }} 相关设置。</p><button @click="selectedSetting='外观';selectedAppearance='主题'">返回外观设置</button></div>
        </div>
      </main>
      <section v-if="profileEditorOpen" class="profile-editor" role="dialog" aria-modal="true" aria-label="编辑个人资料">
        <aside class="profile-editor-tools">
          <header><button class="profile-back" @click="closeProfileEditor">‹</button><b>主要个人资料</b><span>⌄</span><button class="profile-editor-close" aria-label="关闭" @click="closeProfileEditor">×</button></header>
          <div class="profile-edit-scroll">
            <h3>头像</h3><div class="profile-avatar-control"><span class="settings-avatar large-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span><button @click="setDemoAvatar">＋</button></div>
            <h3>头像和装饰</h3><div class="profile-avatar-grid"><button class="avatar-preview" @click="setDemoAvatar"><span class="settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span></button><button class="avatar-add" @click="notDeveloped('头像装饰')">＋</button></div>
            <h3>横幅颜色</h3><button class="profile-color-swatch" :style="{background:profileBackground}" @click="notDeveloped('个人资料横幅')"></button>
            <h3>个人资料特效和边框</h3><div class="profile-decoration-grid"><button @click="notDeveloped('个人资料特效')">＋</button><button @click="notDeveloped('个人资料边框')">＋</button></div>
            <h3>昵称样式</h3><button class="profile-name-style" @click="notDeveloped('昵称样式')">{{demoNickname}}</button>
            <h3>个人资料主题</h3><button class="profile-theme-swatch" :style="{background:profileBackground}" @click="notDeveloped('个人资料主题')"></button>
          </div>
        </aside>
        <div class="profile-editor-preview">
          <div class="profile-preview-card">
            <div class="profile-preview-banner" :style="profileBackgroundImage ? {backgroundImage:`url(${profileBackgroundImage})`} : {background:profileBackground}">
              <div class="profile-background-control"><button aria-label="编辑背景" @click="notDeveloped('个人资料背景')">✎</button><div v-if="profileBackgroundMenu" class="profile-background-menu"><button @click="profileBackgroundImage='';profileBackground='#454347';profileBackgroundMenu=false">纯色</button><button @click="$refs.profileBannerUpload.click();profileBackgroundMenu=false">上传背景图片</button></div></div>
              <input ref="profileBannerUpload" class="profile-file-input" type="file" accept="image/*" @change="handleProfileBackground">
            </div>
            <div class="profile-preview-body"><div class="profile-preview-avatar settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></div><div class="profile-status-bubble">UID {{demoUid||'—'}}</div><h1>{{demoNickname}}</h1><p>UID {{demoUid||'—'}} <span>•</span> <i>添加个性签名</i> <button @click="notDeveloped('服务器标签')">服务器标签⌄</button></p><div class="profile-preview-actions"><button @click="notDeveloped('个人资料消息')">▰　消息</button><button @click="notDeveloped('个人资料操作')">▣</button><button @click="notDeveloped('个人资料更多操作')">•••</button></div><section><label>自我介绍</label><p>写一段简短的介绍</p></section><section><label>成员加入时间</label><p>暂未提供</p></section><section><label>连接</label><p>＋ 添加关联</p></section><section><label>备注（仅对您可见）</label><p>点击添加备注</p></section></div>
          </div>
        </div>
        <footer class="profile-editor-savebar"><span>别忘了保存您的更改</span><button @click="profileBackgroundImage='';profileBackground='#454347'">重置</button><button class="save-profile" @click="closeProfileEditor();notDeveloped('个人资料装饰')">保存</button></footer>
      </section>
    </section>
  </div>
</template>

<style>
.emoji-panel-scroll{scrollbar-width:thin;scrollbar-color:#606168 #1b1c20}
.emoji-panel-scroll::-webkit-scrollbar{width:12px}
.emoji-panel-scroll::-webkit-scrollbar-track{background:#1b1c20}
.emoji-panel-scroll::-webkit-scrollbar-thumb{background:#606168;border:2px solid #1b1c20;border-radius:8px;min-height:56px}
.emoji-panel-scroll::-webkit-scrollbar-thumb:hover{background:#73757e}
</style>
