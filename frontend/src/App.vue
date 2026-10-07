<script setup>
import { ref, nextTick, computed, h, reactive, watch, onMounted, onUnmounted, withDirectives, vModelText } from 'vue'
import { DEFAULT_AVATAR, DEFAULT_GROUP_AVATAR, DEFAULT_COMM_AVATAR, DEFAULT_COMM_BACKGROUND, QUICK_EMOJIS, PICKER_EMOJIS, AVATAR_TONES } from './data/ui.js'
import { EMOJI_CATEGORIES } from './data/emoji.js'
import { lang, setLang, t } from './data/i18n.js'
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
const createGroupDialogOpen = ref(false)
const groupManageOpen = ref(false)
const groupManageTarget = ref(null)
const newCommunityName = ref('')
const selectedDm = ref('')
const friendFilter = ref('全部')
const friendSearchQuery = ref('')
const friendMoreMenu = ref('')
const dmQuickMenu = ref(false)
const friendRequestModal = ref(false)
const demoLoginOpen = ref(true)
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
// 全局传输进度弹窗：静默图片/头像预览不显示，其余上传和下载都显示。
const transferTasks = computed(() => backend.transferOrder.map((id) => backend.transfers[id]).filter((task) => task && !task.silent && Number(task.ownerUid) === Number(backend.uid)))
const transferAutoDismissTimers = new Map()
const profileMenuOpen = ref(false)
const profileAccountMenu = ref(false)
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
const unreadBanner = ref(false)
// —— Baka 音效（内置 Baka.mp3）——
const bakaOnSend = ref(true)
const bakaOnReceive = ref(true)
const bakaOnLogo = ref(true)
const bakaVolume = ref(0.65)
let bakaAudio = null
const playBaka = (reason = 'logo', force = false) => {
  const allowed = force || (reason === 'send' ? bakaOnSend.value : reason === 'receive' ? bakaOnReceive.value : bakaOnLogo.value)
  if (!allowed || typeof Audio === 'undefined') return
  try {
    if (!bakaAudio) bakaAudio = new Audio(bakaAudioUrl)
    bakaAudio.volume = Number(bakaVolume.value) || 0
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
    if (typeof saved.volume === 'number') bakaVolume.value = saved.volume
  }
} catch {}
watch([bakaOnSend, bakaOnReceive, bakaOnLogo, bakaVolume], () => {
  try { localStorage.setItem('discord-baka-v1', JSON.stringify({ send: bakaOnSend.value, receive: bakaOnReceive.value, logo: bakaOnLogo.value, volume: bakaVolume.value })) } catch {}
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
const nicknameSaving = ref(false)
const closeAfterNicknameSave = ref(false)
const selectedSetting = ref('账户')
const themeChoice = ref('dark')
const friendSearch = ref('')
const inviteUrl = ref('功能暂未开发')
const joinCode = ref('')
const joinNote = ref('')
const joinGroupOpen = ref(false)
const joinGroupUid = ref('')
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
const MessageActions = () => h('div', { class: 'message-actions' }, [
  ...QUICK_EMOJIS.map((emoji) => h('button', { 'data-action': 'react', 'data-emoji': emoji, title: t('添加 {0}', emoji) }, emoji)),
  h('button', { 'data-action': 'picker', title: t('添加反应') }, '☻'),
  h('button', { 'data-action': 'reply', title: t('回复') }, '↶'),
  h('button', { 'data-action': 'forward', title: t('转发') }, '↱'),
  h('button', { 'data-action': 'more', title: t('更多') }, h('svg', { viewBox: '0 0 24 24', class: 'more-dots' }, [h('circle', { cx: '5', cy: '12', r: '1.7' }), h('circle', { cx: '12', cy: '12', r: '1.7' }), h('circle', { cx: '19', cy: '12', r: '1.7' })]))
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
    'data-action': 'picker', title: t('添加反应'), 'aria-label': '添加反应'
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
  const parts = []
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
        h('button', { type: 'button', class: 'upload-remove', 'aria-label': t('移除 {0}', item.name), onClick: () => removeUpload(item.id) }, '×')
      ])
    })))
  }
  parts.push(h('div', { class: 'composer-line' }, [
    h('button', { type: 'button', class: 'add-attachment', 'aria-label': t('更多附件选项'), title: t('更多附件选项'), onClick: (e) => { e.stopPropagation(); toggleAttachmentMenu(e) } }, '＋'),
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
      (message.value.trim() || uploads.value.length)
        ? h('button', { type: 'button', class: 'send-button', 'aria-label': t('发送'), title: t('发送'), onClick: () => submit() }, [h(Icon, { name: 'send' }), h('span', null, t('发送'))])
        : null,
      h('button', { type: 'button', class: 'tool-button', 'aria-label': t('表情'), title: t('表情'), onClick: (e) => toggleEmojiPanel(e) }, [h(Icon, { name: 'smile' })])
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
          title: t(item.name),
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
      h('button', { onClick: () => handleAttachmentAction('upload') }, [h(Icon, { name: 'upload', class: 'upload-menu-icon' }), h('b', null, t('上传文件'))])
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
const IMAGE_FILE_PATTERN = /\.(?:avif|bmp|gif|ico|jpe?g|png|svg|webp)$/i
const VIDEO_FILE_PATTERN = /\.(?:avi|flv|m4v|mkv|mov|mp4|ogv|webm|wmv)$/i
const isImageAttachment = (file) => file?.type === 'image' || String(file?.mime || '').startsWith('image/') || IMAGE_FILE_PATTERN.test(String(file?.name || ''))
const isVideoAttachment = (file) => file?.type === 'video' || String(file?.mime || '').startsWith('video/') || VIDEO_FILE_PATTERN.test(String(file?.name || ''))
const MessageRow = (props) => {
  const record = props.record
  const recall = recalled.value[record.id]
  if (recall) {
    return h('article', { class: 'message system-message recalled-message', 'data-message-id': record.id, 'data-recalled': '1' }, [
      h('div', { class: 'system-row' }, [h('span', { class: 'system-glyph' }, '⟲'), h('b', {}, recall.mine ? t('你撤回了一条消息') : t('{0} 撤回了一条消息', recall.author))])
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
      title: record.reply.id ? t('跳到原消息') : null
    }, [
      h('span', { class: 'reply-label' }, t('回复')),
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
    record.mutual ? h('span', { class: 'mutual-badge', title: t('共同服务器') }, 'M') : null,
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
  const files = props.files?.length ? props.files : (record.files || [])
  const fileImages = files.filter(isImageAttachment).map((file) => {
    const previewTask = Object.values(backend.transfers).find((task) => task.silent && String(task.fileId) === String(file.fileId))
    return { ...file, url: backend.fileUrls[String(file.fileId)] || file.url || '', previewFailed: ['失败', '已取消', '已中断'].includes(previewTask?.status) }
  })
  const images = [...(props.images?.length ? props.images : (record.images || [])), ...fileImages]
  if (images.length) {
    content.push(h('div', { class: 'message-images' }, images.map((img) => img.url
      ? h('button', { type: 'button', class: 'message-image-open', title: img.name, key: img.fileId || img.url, 'data-url': img.url, 'data-name': img.name, 'data-file-id': img.fileId || '', 'data-size': img.size || 0, onClick: () => openLightbox(img) }, h('img', { src: img.url, alt: img.name }))
      : h('div', { class: ['message-image-loading', { failed: img.previewFailed }], key: img.fileId || img.name }, [
          h('span', null, img.previewFailed ? '!' : '▧'),
          h('small', null, img.previewFailed ? t('图片加载失败') : t('图片加载中…')),
          img.previewFailed ? h('button', { type: 'button', onClick: () => api.ensureFileUrl(img.fileId, img) }, t('重试')) : null
        ]))))
  }
  const fileVideos = files.filter(isVideoAttachment)
  if (fileVideos.length) {
    content.push(h('div', { class: 'message-videos' }, fileVideos.map((video) => h('div', {
      class: 'message-video-cover', key: video.fileId || video.name, 'data-name': video.name, 'data-file-id': video.fileId || '', 'data-size': video.size || 0
    }, [
      h('div', { class: 'video-cover-art', 'aria-hidden': 'true' }, [h('span', { class: 'video-cover-play' }, '▶'), h('b', null, 'VIDEO')]),
      h('div', { class: 'video-cover-info' }, [h('b', { title: video.name }, video.name), h('small', null, formatFileSize(video.size || 0))]),
      h('button', { type: 'button', class: 'video-cover-download', 'data-action': 'download', 'data-name': video.name, 'data-file-id': video.fileId || '', 'data-size': video.size || 0, title: t('下载'), 'aria-label': t('下载 {0}', video.name) },
        h('svg', { viewBox: '0 0 24 24', 'aria-hidden': 'true' }, h('path', { d: 'M12 4v11m-5-5 5 5 5-5M5 20h14' })))
    ]))))
  }
  const downloadableFiles = files.filter((file) => !isImageAttachment(file) && !isVideoAttachment(file))
  if (downloadableFiles.length) {
    content.push(h('div', { class: 'message-files' }, downloadableFiles.map((file) => h('div', {
      class: 'message-file', key: file.fileId || file.url || file.name, 'data-url': file.url || '', 'data-name': file.name, 'data-file-id': file.fileId || ''
    }, [
      h(FileCover, { name: file.name }),
      h('span', { class: 'message-file-body' }, [
        h('a', { href: file.url || '#', download: file.name, title: file.name }, file.name),
        h('small', {}, formatFileSize(file.size || 0))
      ]),
      h('button', { type: 'button', class: 'message-file-download', 'data-action': 'download', 'data-url': file.url || '', 'data-name': file.name, 'data-file-id': file.fileId || '', 'data-size': file.size || 0, title: '下载', 'aria-label': t('下载 {0}', file.name) },
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
// 统一的重命名弹窗：取消返回 null，否则返回去除首尾空白后的输入（是否允许空值由调用方决定）
const promptRename = (title, current = '') => {
  const next = window.prompt(title, current)
  return next === null ? null : next.trim()
}
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
  const height = 180
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
const testResetDemo = () => notDeveloped(t('重置服务端数据'))
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
  window.removeEventListener('keydown', onLightboxKey)
  window.removeEventListener('mousemove', lightboxDragMove)
  window.removeEventListener('mouseup', lightboxDragEnd)
  for (const upload of uploads.value) if (upload.preview) URL.revokeObjectURL(upload.preview)
  for (const timer of transferAutoDismissTimers.values()) clearTimeout(timer)
  transferAutoDismissTimers.clear()
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
  if (action === 'clear') { notDeveloped(t('清除服务端聊天记录')); return }
  if (action === 'remove') {
    const friend = demoFriends.value.find((item) => item.name === target.name || item.remark === target.name)
    if (friend) removeDemoFriend(friend)
    else if (contact) closeDm(contact)
    return
  }
  if (action === 'members' || action === 'groupSettings') { const group=demoGroups.value.find(item=>item.name===target.name);if(group){api.showGroupMembers(group.uid);groupManageTarget.value=group;groupManageOpen.value=true};return }
  if (action === 'rename') {
    const group = demoGroups.value.find((item) => item.name === target.name)
    if (group) renameDemoGroup(group)
    return
  }
  if (action === 'leave' && contact) { if(window.confirm(t('确定退出群聊“{0}”？', contact.name)))api.groupRemove(contact.uid,backend.uid) }
}
const openCommunityInvite = (name = selectedCommunity.value) => {
  const community = communities.value.find((item) => item.name === name)
  if (!community) return demoNotice(t('请先选择社区'))
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
  if (action === 'invite') { openCommunityInvite(name); return }
  if (action === 'settings') { openCommunitySettings(name); return }
  if (action === 'leave') { const community=communities.value.find(item=>item.name===name);if(community&&window.confirm(t('确定离开“{0}”？', name)))api.leaveCommunity(community.id) }
}
const openCommunitySettings = (name) => {
  selectedCommunity.value=name
  communitySettingsName.value=name
  communitySettingsDescription.value=currentCommunity.value?.description||'欢迎来到我们的社区，一起交流分享吧。'
  newCommunityChannel.value=''
  communityChannelDeleted.value={}
  channelDeleteTarget.value=null
  communitySettingsOpen.value=true
}
const saveCommunitySettings = () => {
  const community = currentCommunity.value
  if (!community) return
  const name = communitySettingsName.value.trim()
  if (!name) { demoNotice(t('社区名称不能为空')); return }
  api.modifyCommunity(community.id, name, communitySettingsDescription.value.trim(), Number(community.avatarId || 0), Number(community.bannerId || 0))
  communitySettingsOpen.value = false
}
const scrollToCommunitySection = (id) => document.getElementById(id)?.scrollIntoView({behavior:'smooth',block:'start'})
const closeCommunitySettings = () => { communitySettingsOpen.value = false; dissolveConfirm.value = false; channelDeleteTarget.value = null }
const openGuideEditor = async () => {
  openCommunitySettings(currentCommunity.value?.name || selectedCommunity.value)
  await nextTick()
  scrollToCommunitySection('community-overview')
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
  if (row.querySelector('.message-images img')) return '🖼 图片'
  return row.querySelector('.message-video-cover') ? '视频' : ''
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
  if (!el) { demoNotice(t('原消息不在当前视图')); return }
  el.scrollIntoView({ block: 'center', behavior: 'smooth' })
  el.classList.add('message-flash')
  setTimeout(() => el.classList.remove('message-flash'), 1400)
}
const openContextMenu = (event) => {
  const row = event.target.closest('.message[data-message-id]')
  if (!row || row.dataset.recalled) { contextMenu.value = null; return }
  const id = row.dataset.messageId
  // 右键落在文件卡片 / 图片上时，菜单里多一项「下载」
  const attachment = event.target.closest?.('.message-file, .message-images a, .message-video-cover')
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
  if (!attachment?.url) { demoNotice(t('该附件没有可下载的文件')); return }
  const link = document.createElement('a')
  link.href = attachment.url
  link.download = attachment.name || 'download'
  link.rel = 'noreferrer'
  document.body.appendChild(link)
  link.click()
  link.remove()
  demoNotice(t('开始下载 {0}', attachment.name))
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
    if (contextMenu.value.mine) api.deleteMessage(contextMenu.value.id); else demoNotice(t('只能撤回自己的消息'))
    contextMenu.value = null
  }
  if (action === 'forward') { notDeveloped('消息转发'); contextMenu.value = null }
  if (action === 'copy' && contextMenu.value) {
    const text = contextMenu.value.text
    if (text) {
      const ok = await copyToClipboard(text)
      demoNotice(ok ? '消息已复制' : '复制失败')
    } else {
      demoNotice(t('该消息没有可复制的文字'))
    }
    contextMenu.value = null
  }
  if (action === 'unread') { notDeveloped(t('标记未读')); contextMenu.value = null }
  if (action === 'delete' && contextMenu.value?.mine) { api.deleteMessage(contextMenu.value.id); contextMenu.value = null }
  if (action === 'link' && contextMenu.value) {
    const ok = await copyToClipboard(`${location.origin}${location.pathname}#${contextMenu.value.id}`)
    demoNotice(ok ? '消息链接已复制' : '复制失败')
    contextMenu.value = null
  }
  if (action === 'report') { notDeveloped(t('举报消息')); contextMenu.value = null }
}
const submit = async () => {
  const text = message.value.trim()
  if (!text && !uploads.value.length) return
  const conversationKey = activePage.value === 'dm-chat' ? `dm:${selectedDm.value}` : active.value
  const reply = replyTarget.value?.conversationKey === conversationKey ? replyTarget.value : null
  const replyId = reply?.id || 0
  const textSent = text ? api.sendChat(text, replyId, reply) : false
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
// —— 图片查看器（lightbox）：滚轮缩放、拖动平移、左右切换已加载图片、X/Esc 关闭 ——
const lightbox = ref(null)  // null 或 { images:[{url,name,fileId}], index, scale, dx, dy, dragging }
const collectLoadedImages = () => {
  const records = api.activeConversation.value?.messages || []
  const seen = new Set()
  const images = []
  for (const record of records) {
    for (const file of (record.files || [])) {
      if (!isImageAttachment(file)) continue
      const url = backend.fileUrls[String(file.fileId)] || file.url || ''
      if (!url || seen.has(url)) continue
      seen.add(url)
      images.push({ url, name: file.name || '', fileId: file.fileId || '' })
    }
    for (const img of (record.images || [])) {
      if (!img?.url || seen.has(img.url)) continue
      seen.add(img.url)
      images.push({ url: img.url, name: img.name || '', fileId: img.fileId || '' })
    }
  }
  return images
}
const resetLightbox = () => {
  const lb = lightbox.value
  if (!lb) return
  lb.scale = 1; lb.dx = 0; lb.dy = 0; lb.dragging = false
}
const lightboxPrev = () => { const lb = lightbox.value; if (lb && lb.images.length > 1) { lb.index = (lb.index - 1 + lb.images.length) % lb.images.length; resetLightbox() } }
const lightboxNext = () => { const lb = lightbox.value; if (lb && lb.images.length > 1) { lb.index = (lb.index + 1) % lb.images.length; resetLightbox() } }
const onLightboxKey = (event) => {
  if (!lightbox.value) return
  if (event.key === 'Escape') closeLightbox()
  else if (event.key === 'ArrowLeft') lightboxPrev()
  else if (event.key === 'ArrowRight') lightboxNext()
}
const openLightbox = (img) => {
  const images = collectLoadedImages()
  let index = images.findIndex((item) => item.url === img.url)
  if (index < 0) { images.unshift({ url: img.url, name: img.name || '', fileId: img.fileId || '' }); index = 0 }
  lightbox.value = { images, index, scale: 1, dx: 0, dy: 0, dragging: false }
  window.addEventListener('keydown', onLightboxKey)
}
const closeLightbox = () => {
  lightbox.value = null
  window.removeEventListener('keydown', onLightboxKey)
  window.removeEventListener('mousemove', lightboxDragMove)
  window.removeEventListener('mouseup', lightboxDragEnd)
}
const lightboxWheel = (event) => {
  const lb = lightbox.value
  if (!lb) return
  const factor = event.deltaY < 0 ? 1.2 : 1 / 1.2
  lb.scale = Math.min(8, Math.max(1, lb.scale * factor))
  if (lb.scale <= 1) { lb.dx = 0; lb.dy = 0 }
}
const lightboxDragStart = (event) => {
  const lb = lightbox.value
  if (!lb) return
  event.preventDefault()
  lb.dragging = true
  lb.startX = event.clientX; lb.startY = event.clientY
  lb.startDx = lb.dx; lb.startDy = lb.dy
  window.addEventListener('mousemove', lightboxDragMove)
  window.addEventListener('mouseup', lightboxDragEnd)
}
const lightboxDragMove = (event) => {
  const lb = lightbox.value
  if (!lb || !lb.dragging) return
  lb.dx = lb.startDx + (event.clientX - lb.startX)
  lb.dy = lb.startDy + (event.clientY - lb.startY)
}
const lightboxDragEnd = () => {
  const lb = lightbox.value
  if (lb) lb.dragging = false
  window.removeEventListener('mousemove', lightboxDragMove)
  window.removeEventListener('mouseup', lightboxDragEnd)
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
  const labels = { thread: '子区', poll: '投票', emoji: '表情', app: 'APP' }
  notDeveloped(labels[action] || '该功能')
}
const copyInvite = async () => notDeveloped(t('社区邀请链接'))
const joinServer = () => {
  const id = Number(String(joinCode.value).trim())
  if (!id || id <= 0) { demoNotice(t('请输入社区 UID')); return }
  api.joinCommunity(id, joinNote.value.trim())
  joinModal.value = false
  joinCode.value = ''
  joinNote.value = ''
}
const submitJoinGroup = () => {
  const id = Number(String(joinGroupUid.value).trim())
  if (!id || id <= 0) { demoNotice(t('请输入群 UID')); return }
  api.joinGroup(id)
  joinGroupOpen.value = false
  joinGroupUid.value = ''
}
const selectSetting = (name) => { selectedSetting.value = name; if (!['账户','Baka','文件','外观','语言'].includes(name)) notDeveloped(name) }
const handleProfileBackground = (event) => { event.target.value = ''; notDeveloped(t('个人资料背景')) }
const closeSettings = () => { settingsOpen.value = false; profileEditorOpen.value = false; profileBackgroundMenu.value = false }
const closeProfileEditor = () => { profileEditorOpen.value = false; profileBackgroundMenu.value = false }
const cancelProfileEditing = () => {
  demoNicknameDraft.value = backend.name || ''
  nicknameSaving.value = false
  closeAfterNicknameSave.value = false
  closeProfileEditor()
}
const demoNotice = (text) => { toast.value = text; setTimeout(() => { if (toast.value === text) toast.value = '' }, 1800) }
const notDeveloped = (name = '该功能') => demoNotice(t('{0}暂未开发', t(name)))

let lastSyncedBackendName = ''
const syncBackendState = () => {
  demoSignedIn.value = !!backend.authenticated
  demoUid.value = backend.uid ? String(backend.uid) : ''
  if (backend.name !== lastSyncedBackendName) { demoNickname.value = backend.name || '未登录'; demoNicknameDraft.value = backend.name || ''; lastSyncedBackendName = backend.name }
  if (backend.email) demoEmail.value = backend.email
  demoFriends.value = backend.contacts.filter((item) => !item.isGroup).map((item) => ({ uid: String(item.uid), name: item.name, remark: item.name, email: '', online: false }))
  demoGroups.value = backend.contacts.filter((item) => item.isGroup).map((item) => ({ uid: String(item.uid), name: item.name, members: backend.groupMembers[item.uid] || [] }))
  demoFriendRequests.value = backend.friendRequests.map((item) => ({ ...item, email: '' }))
  const allGroupRequests = []
  for (const [groupId, requests] of Object.entries(backend.groupRequests)) for (const request of requests) allGroupRequests.push({ ...request, group: backend.contacts.find((item) => item.uid === Number(groupId))?.name || groupId, groupUid: Number(groupId) })
  demoGroupRequests.value = allGroupRequests
  communities.value = backend.communities.map((item) => ({ ...item, mark: item.name.slice(0, 1), color: '#5865f2', icon: '', members: (backend.members[item.id] || []).length, online: 0 }))
  demoFiles.value = [
    ...backend.transferOrder.map((id) => backend.transfers[id]).filter((task) => task && !task.silent && (!task.ownerUid || Number(task.ownerUid)===Number(backend.uid)) && !(task.direction==='upload'&&task.status==='已完成')).map((task) => ({ ...task, id: task.localId, size: task.totalSize, type: task.direction, format: task.format || 'FILE' })),
    ...backend.files.filter((file) => file.type !== 'avatar'),
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
  uid: backend.uid, name: backend.name, email: backend.email,
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
  const notice = backend.lastNotice
  demoNotice(notice)
  if (notice.includes('发来新消息')) playBaka('receive')
  const renamed = /Name updated successfully\. Your new name is \[(.*?)\]\./.exec(notice)
  if (renamed) {
    nicknameSaving.value = false
    demoNicknameDraft.value = renamed[1]
    const shouldClose = closeAfterNicknameSave.value
    closeAfterNicknameSave.value = false
    if (shouldClose) closeProfileEditor()
  } else if (nicknameSaving.value && /Name cannot be empty|Failed to update name|logged in to change your name/i.test(notice)) {
    nicknameSaving.value = false
    closeAfterNicknameSave.value = false
  }
  const registered=/注册成功，UID：(\d+)/.exec(notice)
  if (registered) { demoAuth.identity=registered[1];demoAuthMode.value='登录' }
})
watch(profileEditorOpen, (open) => {
  if (!open) return
  demoNicknameDraft.value = backend.name || ''
  nicknameSaving.value = false
  closeAfterNicknameSave.value = false
})
watch(themeChoice, (theme) => { if (backend.uid && theme !== 'device') api.changeTheme(theme) })
watch(() => [backend.authenticated, backend.connected, backend.authenticating, backend.reconnecting], ([authenticated, connected, authenticating, reconnecting]) => {
  demoLoginOpen.value = !authenticated && (!connected || !authenticating || reconnecting)
}, { immediate: true })
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
// 上传和下载进入终态后自动关闭进度条，同时保留几秒让用户看清结果。
watch(() => backend.transferOrder.map((id) => { const task = backend.transfers[id]; return task ? { id, status: task.status, direction: task.direction, silent: !!task.silent } : null }), () => {
  for (const task of transferTasks.value) {
    const key = task.localId
    if (!['已完成', '已取消', '失败'].includes(task.status) || transferAutoDismissTimers.has(key)) continue
    const timer = setTimeout(() => {
      transferAutoDismissTimers.delete(key)
      const current = backend.transfers[key]
      if (current && ['已完成', '已取消', '失败'].includes(current.status)) api.dismissTransfer(current)
    }, 3000)
    transferAutoDismissTimers.set(key, timer)
  }
}, { deep: true, immediate: true })
watch(() => ({
  sessionSeq: backend.sessionSeq,
  authenticated: backend.authenticated,
  activeKey: backend.activeKey,
  files: (api.activeConversation.value?.messages || []).flatMap((record) => (record.files || []).map((file) => ({ fileId: file.fileId, name: file.name, size: file.size, type: file.type, mime: file.mime, priority: true })))
}), ({ authenticated, files }) => {
  if (!authenticated) return
  for (const file of files) {
    if (file.fileId && isImageAttachment(file)) api.ensureFileUrl(file.fileId, file)
  }
}, { deep: true, immediate: true })
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
    if (!demoAuth.username.trim() || !demoAuth.email.trim() || !demoAuth.password.trim()) return demoNotice(t('请填写昵称、邮箱和密码'))
    api.register(demoAuth.username.trim(), demoAuth.email.trim(), demoAuth.password)
  } else {
    if (!demoAuth.identity.trim() || !demoAuth.password.trim()) return demoNotice(t('请输入账号和密码'))
    api.login(demoAuth.identity.trim(), demoAuth.password)
  }
  demoAuth.password = ''
}
const demoLogout = () => { api.logout(); demoAuthMode.value = '登录'; demoAuth.password = ''; demoLoginOpen.value = true }
const requestDemoFriend = () => {
  const email = demoForm.email.trim().toLowerCase()
  if (!email || !email.includes('@')) return demoNotice(t('请输入有效的邮箱地址'))
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
const renameDemoFriend = (friend) => { const next=promptRename(t('设置好友备注名'),friend.remark||friend.name);if(next)api.setFriendRemark(friend.uid,next) }
const createDemoGroup = () => { const name=demoForm.groupName.trim();if(!name)return demoNotice(t('请输入群聊名称'));api.createGroup(name);demoForm.groupName='';createGroupDialogOpen.value=false }
const renameDemoGroup = (group) => {const next=promptRename(t('修改群名称'),group.name);if(next)api.groupRename(group.uid,next)}
const addDemoGroupMember = (group) => {const uid=demoForm.memberUid.trim();if(!uid)return demoNotice(t('请输入成员 UID'));api.groupAdd(group.uid,uid);demoForm.memberUid='';setTimeout(()=>api.showGroupMembers(group.uid),200)}
const removeDemoGroupMember = (group, member) => api.groupRemove(group.uid,member.uid)
const toggleDemoGroupRole = (group, member) => api.groupRole(group.uid,member.uid,true)
const handleDemoGroupRequest = (request, accept) => {if(api.handleGroupRequest(request.groupUid,request.uid,accept))setTimeout(()=>api.showGroupRequests(request.groupUid),200)}
const openDemoFriendChat = (friend) => {const contact=dmContacts.find(item=>Number(item.uid)===Number(friend.uid));if(contact)openDm(contact)}
const openGroupChat = (group) => { const contact=dmContacts.find(item=>Number(item.uid)===Number(group.uid));if(contact)openDm(contact);api.showGroupMembers(group.uid);api.showGroupRequests(group.uid) }
const deleteDemoGroup = (group) => {if(window.confirm(t('解散群聊“{0}”？', group.name)))api.groupDelete(group.uid)}
const selectDemoFiles = () => demoFilesInput.value?.click()
const addDemoFiles = (event) => {for(const file of Array.from(event.target.files||[]))api.uploadFile(file,false);event.target.value=''}
const setDemoFileStatus = (file,status) => {const task=backend.transfers[file.localId];if(!task)return;if(status==='已暂停')api.pauseTransfer(task);else if(task.direction==='upload'&&!task.file&&['已中断','等待选择原文件'].includes(task.status)){resumeTransferTarget.value=task;resumeFileInput.value?.click()}else api.resumeTransfer(task)}
const bindResumeUpload = (event) => {const file=event.target.files?.[0];if(file&&resumeTransferTarget.value)api.bindResumeFile(resumeTransferTarget.value,file);event.target.value='';resumeTransferTarget.value=null}
const downloadDemoFile = (file) => {const task=backend.transfers[file.localId];if(task?.url){const link=document.createElement('a');link.href=task.url;link.download=task.name||'download';link.rel='noreferrer';document.body.appendChild(link);link.click();link.remove()}else api.downloadFile(file)}
const toggleFileTransfer = (file) => {if(!file)return;if(['已暂停','已中断','等待选择原文件'].includes(file.status))setDemoFileStatus(file,'继续');else setDemoFileStatus(file,'已暂停')}
const toggleActiveTransfer = () => toggleFileTransfer(activeTransfer.value)
const closeTransferProgress = (task) => { if (['已完成','已取消','失败'].includes(task.status)) api.dismissTransfer(task); else api.cancelTransfer(task) }
const deleteDemoFile = (file) => {const task=backend.transfers[file.localId];if(task){if(['已完成','已取消','失败'].includes(task.status))api.dismissTransfer(task);else api.cancelTransfer(task)}else api.deleteFile(file.fileId||file.id)}
const handleDemoCommunityRequest = (request,accept) => {const community=currentCommunity.value;if(!community)return;if(api.handleCommunityRequest(community.id,request.uid,accept))setTimeout(()=>api.showCommunityRequests(community.id),200)}
const inviteFriendToCommunity = (friend) => {
  const community = currentCommunity.value
  if (!community) return demoNotice(t('请先选择社区'))
  if (community.role !== 'owner') return demoNotice('只有社区所有者可以直接添加成员')
  api.communityAdd(community.id, friend.uid)
}
const createDemoCommunity = () => {newCommunityName.value=demoForm.communityName;createCommunity();demoForm.communityName=''}
const toggleDemoMemberRole = (member) => {const community=currentCommunity.value;if(!community||member.role==='owner')return;api.communityRole(community.id,member.uid,member.role!=='admin')}
const removeDemoCommunityMember = (member) => {const community=currentCommunity.value;if(community&&member.role!=='owner')api.communityRemove(community.id,member.uid)}
const addDemoCommunityMember = () => {const community=currentCommunity.value;const uid=Number(demoForm.memberUid);if(!community||!uid)return demoNotice(t('请输入成员 UID'));api.communityAdd(community.id,uid);demoForm.memberUid=''}
const saveNickname = (closeWhenSaved = false) => {
  if (nicknameSaving.value) return false
  if (!backend.uid) { demoNotice(t('请先登录')); return false }
  const name = demoNicknameDraft.value.trim()
  if (!name) { demoNotice(t('昵称不能为空')); return false }
  if (name.length > 30) { demoNotice(t('昵称最多 30 个字符')); return false }
  demoNicknameDraft.value = name
  if (name === backend.name) {
    demoNotice(t('昵称没有变化'))
    if (closeWhenSaved) closeProfileEditor()
    return true
  }
  if (!api.changeName(name)) { demoNotice(t('昵称保存失败')); return false }
  nicknameSaving.value = true
  closeAfterNicknameSave.value = closeWhenSaved
  return true
}
const setDemoAvatar = () => avatarUploadInput.value?.click()
const handleAvatarUpload = (event) => {const file=event.target.files?.[0];if(file)openAvatarCrop(file);event.target.value=''}

// —— 聊天消息搜索：只检索前端已加载的消息，命中文字用 Discord 蓝底圆角高亮 ——
const searchQuery = ref('')
const searchOpen = ref(false)
const closeSearch = () => { searchQuery.value = ''; searchOpen.value = false }
const searchResults = computed(() => {
  const q = searchQuery.value.trim().toLowerCase()
  if (!q) return []
  const results = []
  for (const conv of Object.values(backend.conversations)) {
    for (const record of conv.messages) {
      if (record.system || isReactionMessage(record)) continue
      const text = String(record.text || '')
      const files = (record.files || []).map((f) => f.name).join(' ')
      const hay = `${text} ${files}`.toLowerCase()
      if (hay.includes(q)) results.push({ id: record.id, author: record.author, time: record.time, text: text || files, conversation: conv.name })
    }
  }
  return results.slice(0, 60)
})
const highlightMatch = (text, query) => {
  const q = String(query || '').trim()
  if (!q || !text) return text
  const lower = text.toLowerCase()
  const ql = q.toLowerCase()
  const nodes = []
  let i = 0
  for (;;) {
    const idx = lower.indexOf(ql, i)
    if (idx === -1) { nodes.push(text.slice(i)); break }
    if (idx > i) nodes.push(text.slice(i, idx))
    nodes.push(h('mark', { class: 'search-highlight' }, text.slice(idx, idx + q.length)))
    i = idx + q.length
  }
  return nodes
}
const SearchPanel = () => {
  const results = searchResults.value
  return h('div', { class: 'search-panel', onClick: (e) => e.stopPropagation() }, [
    h('div', { class: 'search-panel-head' }, [
      h('b', null, t('搜索消息 — {0} 条', results.length)),
      h('span', null, t('仅搜索当前已加载的消息'))
    ]),
    results.length
      ? h('div', { class: 'search-panel-list' }, results.map((result) => h('div', { key: `${result.conversation}-${result.id}`, class: 'search-result-row' }, [
          h('div', { class: 'search-result-meta' }, [
            h('b', null, result.conversation),
            h('span', null, result.author),
            h('time', null, result.time)
          ]),
          h('p', null, highlightMatch(result.text, searchQuery.value))
        ])))
      : h('div', { class: 'search-panel-empty' }, t('没有找到匹配的消息'))
  ])
}
// —— 账号设置：邮箱与修改密码 ——
const emailEditOpen = ref(false)
const passwordEditOpen = ref(false)
const oldPassword = ref('')
const newPassword = ref('')
const saveEmail = () => {
  const email = demoEmail.value.trim()
  if (!email || !email.includes('@')) return demoNotice(t('请输入有效的邮箱地址'))
  api.setEmail(email)
  emailEditOpen.value = false
}
const savePassword = () => {
  if (!oldPassword.value || !newPassword.value) return demoNotice(t('请填写当前密码和新密码'))
  notDeveloped('修改密码（后端暂未提供接口）')
}
const resetAvatar = () => {
  if (!backend.avatarId) return demoNotice(t('当前已是默认头像'))
  api.setAvatar(-1)
  backend.avatarId = 0
  demoNotice(t('已恢复默认头像'))
}

// —— 头像上传：正方形裁切（拖动图片移动位置，滚轮/滑块缩放）——
const avatarCrop = ref(null)
const CROP_STAGE = 320
const CROP_MIN_ZOOM = 1
const CROP_MAX_ZOOM = 4
const crop = reactive({ zoom: 1, ox: 0, oy: 0, naturalW: 0, naturalH: 0, baseScale: 1 })
const cropDrag = ref(null)
const cropDisplayW = computed(() => Math.round(crop.naturalW * crop.baseScale * crop.zoom))
const cropDisplayH = computed(() => Math.round(crop.naturalH * crop.baseScale * crop.zoom))
const clampCrop = () => {
  const dw = crop.naturalW * crop.baseScale * crop.zoom
  const dh = crop.naturalH * crop.baseScale * crop.zoom
  crop.ox = Math.min(0, Math.max(crop.ox, CROP_STAGE - dw))
  crop.oy = Math.min(0, Math.max(crop.oy, CROP_STAGE - dh))
}
const centerCrop = () => {
  crop.ox = (CROP_STAGE - crop.naturalW * crop.baseScale * crop.zoom) / 2
  crop.oy = (CROP_STAGE - crop.naturalH * crop.baseScale * crop.zoom) / 2
}
const openAvatarCrop = (file) => {
  const url = URL.createObjectURL(file)
  const img = new Image()
  img.onload = () => {
    const baseScale = CROP_STAGE / Math.min(img.naturalWidth, img.naturalHeight)
    Object.assign(crop, { zoom: 1, ox: 0, oy: 0, naturalW: img.naturalWidth, naturalH: img.naturalHeight, baseScale })
    avatarCrop.value = { url, file, img }
    centerCrop()
  }
  img.src = url
}
const cancelAvatarCrop = () => {
  if (avatarCrop.value?.url) URL.revokeObjectURL(avatarCrop.value.url)
  avatarCrop.value = null
  cropDrag.value = null
}
const startCropDrag = (e) => {
  cropDrag.value = { sx: e.clientX, sy: e.clientY, ox: crop.ox, oy: crop.oy }
  const move = (ev) => {
    const d = cropDrag.value
    if (!d) return
    crop.ox = d.ox + (ev.clientX - d.sx)
    crop.oy = d.oy + (ev.clientY - d.sy)
    clampCrop()
  }
  const up = () => { cropDrag.value = null; window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up) }
  window.addEventListener('mousemove', move)
  window.addEventListener('mouseup', up)
}
const onCropWheel = (e) => {
  const oldZoom = crop.zoom
  const next = Math.max(CROP_MIN_ZOOM, Math.min(CROP_MAX_ZOOM, oldZoom + (e.deltaY < 0 ? 0.1 : -0.1)))
  if (next === oldZoom) return
  const k = next / oldZoom
  const cx = CROP_STAGE / 2
  const cy = CROP_STAGE / 2
  crop.ox = cx - (cx - crop.ox) * k
  crop.oy = cy - (cy - crop.oy) * k
  crop.zoom = next
  clampCrop()
}
const onCropZoomInput = (e) => {
  crop.zoom = Number(e.target.value)
  clampCrop()
}
const confirmAvatarCrop = () => {
  const data = avatarCrop.value
  if (!data) return
  const inv = 1 / (crop.baseScale * crop.zoom)
  const size = Math.min(Math.round(CROP_STAGE * inv), crop.naturalW, crop.naturalH)
  const sx = Math.max(0, Math.min(Math.round(-crop.ox * inv), crop.naturalW - size))
  const sy = Math.max(0, Math.min(Math.round(-crop.oy * inv), crop.naturalH - size))
  const canvas = document.createElement('canvas')
  canvas.width = size
  canvas.height = size
  canvas.getContext('2d').drawImage(data.img, sx, sy, size, size, 0, 0, size, size)
  canvas.toBlob((blob) => {
    if (!blob) { cancelAvatarCrop(); demoNotice(t('图片处理失败')); return }
    const out = new File([blob], data.file.name, { type: data.file.type || 'image/png' })
    api.uploadFile(out, { type: 'avatar' })
    cancelAvatarCrop()
  }, data.file.type || 'image/png')
}
</script>

<template>
  <div class="window-bar"><div class="window-title"><span class="tiny-server">🌸</span> {{ ['dm','dm-chat'].includes(activePage) ? (activePage==='dm'?'好友':selectedDm) : activePage==='discover' ? '发现' : selectedCommunity }}</div></div>
  <input ref="avatarUploadInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleAvatarUpload">
  <div v-if="avatarCrop" class="dialog-backdrop crop-backdrop" @click.self="cancelAvatarCrop">
    <section class="crop-dialog">
      <button class="dialog-close" @click="cancelAvatarCrop">×</button>
      <h2>{{ t("裁切头像") }}</h2>
      <p>{{ t("拖动图片调整位置，滚轮或下方滑块缩放。") }}</p>
      <div class="crop-stage" :style="{width: `${CROP_STAGE}px`, height: `${CROP_STAGE}px`}" @mousedown.prevent="startCropDrag" @wheel.prevent="onCropWheel">
        <img :src="avatarCrop.url" :style="{width: `${cropDisplayW}px`, height: `${cropDisplayH}px`, transform: `translate(${crop.ox}px, ${crop.oy}px)`}" draggable="false">
        <div class="crop-grid"></div>
      </div>
      <div class="crop-zoom-row"><span>{{ t("缩放") }}</span><input type="range" :min="CROP_MIN_ZOOM" :max="CROP_MAX_ZOOM" :step="0.01" :value="crop.zoom" @input="onCropZoomInput"></div>
      <footer><button @click="cancelAvatarCrop">{{ t("取消") }}</button><button class="crop-confirm" @click="confirmAvatarCrop">{{ t("上传头像") }}</button></footer>
    </section>
  </div>
  <div class="discord-app" :style="appGridStyle">
    <nav class="server-rail">
      <button class="rail-home" :class="{'rail-selected':['dm','dm-chat'].includes(activePage)}" :aria-label="t('私聊和群聊')" :data-tooltip="t('私聊和群聊')" :title="t('私聊和群聊')" @click="activePage='dm'" @dblclick="playBaka('logo')"><img src="/logo.png" alt="Discord"></button>
      <div class="rail-divider"></div>
      <button v-for="community in communities" :key="community.name" class="server community-server" :class="{'active-server':activePage==='community'&&selectedCommunity===community.name}" :aria-label="community.name" :data-tooltip="community.name" :title="community.name" @click="selectCommunity(community)" @contextmenu.prevent.stop="openCommunityContext($event,community)"><span class="community-mark"><img :src="community.icon || DEFAULT_COMM_AVATAR" :alt="community.name"></span><i></i></button>
      <div class="rail-action-wrap"><button class="round-add" :class="{'rail-selected':communityMenu}" :aria-label="t('创建社区')" :data-tooltip="t('创建社区')" :title="t('创建或加入社区')" @click="communityMenu=!communityMenu;communityContextMenu=null"><svg viewBox="0 0 24 24"><path d="M12 4v16M4 12h16"/></svg></button><div v-if="communityMenu" class="community-menu"><button @click="communityMenu=false;createCommunityOpen=true"><Icon name="community"/>{{ t("创建社区") }}</button><button @click="communityMenu=false;createGroupDialogOpen=true"><Icon name="group"/>{{ t("创建群聊") }}</button><button @click="communityMenu=false;joinModal=true"><Icon name="enter"/>{{ t("申请加入社区") }}</button><button @click="communityMenu=false;joinGroupOpen=true"><Icon name="enter"/>{{ t("申请加入群聊") }}</button><button @click="communityMenu=false;friendRequestModal=true"><Icon name="userPlus"/>{{ t("添加好友") }}</button></div></div>
      <button class="round-discover" :class="{'rail-selected':activePage==='discover'}" :aria-label="t('发现')" :data-tooltip="t('发现')" :title="t('发现')" @click="notDeveloped(t('发现社区'))"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="m15.5 8.5-2.2 5-4.8 2 2-4.8 5-2.2Z"/></svg></button>
      <div class="rail-divider rail-test-divider"></div>
      <button class="round-test" :class="{'rail-selected':testMenu}" :aria-label="t('测试')" :data-tooltip="t('测试菜单')" :title="t('测试菜单')" @click.stop="openTestMenu"><svg viewBox="0 0 24 24"><path d="M9 3h6M10 3v6.2L5.2 17A2.4 2.4 0 0 0 7.3 20.6h9.4A2.4 2.4 0 0 0 18.8 17L14 9.2V3"/><path d="M7.2 14h9.6"/></svg></button>
    </nav>
    <div v-if="testMenu" class="community-context-dismiss" @click="testMenu=null" @contextmenu.prevent="testMenu=null"></div>
    <div v-if="testMenu" class="community-context-menu test-menu" :style="{left:`${testMenu.x}px`,bottom:`${testMenu.bottom}px`}" @click.stop>
      <div class="test-menu-label">{{ t("测试 · 好友与群聊") }}</div>
      <button @click="addTestFriendRequest()"><span class="test-menu-icon">♧</span>{{ t("收到一条好友申请") }}<b v-if="demoFriendRequests.length" class="test-menu-count">{{demoFriendRequests.length}}</b></button>
      <button @click="addTestGroupRequest()"><span class="test-menu-icon">◌</span>{{ t("收到一条入群申请") }}</button>
      <button @click="addTestCommunityRequest()"><span class="test-menu-icon">◆</span>{{ t("收到一条入社区申请") }}</button>
      <button @click="activePage='dm';friendFilter='待定'"><span class="test-menu-icon">▤</span>{{ t("打开待处理列表") }}</button>
      <div></div>
      <div class="test-menu-label">{{ t("测试 · 消息与音效") }}</div>
      <button @click="pushTestIncoming()"><span class="test-menu-icon">✉</span>{{ t("推送一条私信") }}</button>
      <button @click="pushTestIncoming()"><span class="test-menu-icon">#</span>{{ t("推送一条频道消息") }}</button>
      <button @click="playBaka('logo')"><span class="test-menu-icon">♪</span>{{ t("试听 Baka 音效") }}</button>
      <div></div>
      <div class="test-menu-label">{{ t("测试 · 窗口与流程") }}</div>
      <button @click="testMenu=null;friendRequestModal=true"><span class="test-menu-icon">＋</span>{{ t("添加好友窗口") }}</button>
      <button @click="testMenu=null;createCommunityOpen=true"><span class="test-menu-icon">＋</span>{{ t("创建社区窗口") }}</button>
      <button @click="testMenu=null;joinModal=true"><span class="test-menu-icon">↗</span>{{ t("申请加入社区窗口") }}</button>
      <button @click="testMenu=null;createGroupDialogOpen=true"><span class="test-menu-icon">◌</span>{{ t("创建群聊表单") }}</button>
      <button @click="testMenu=null;openDemoLoginWindow()"><span class="test-menu-icon">⇥</span>{{ t("登录窗口") }}</button>
      <button @click="testMenu=null;settingsOpen=true"><span class="test-menu-icon">⚙</span>{{ t("用户设置") }}</button>
      <div></div>
      <button @click="testSignedOut()"><span class="test-menu-icon">⇤</span>{{ t("切换为未登录态") }}</button>
      <button @click="testClearLogs()"><span class="test-menu-icon">⌫</span>{{ t("清空本机聊天记录") }}</button>
      <button class="leave-community" @click="testResetDemo()"><span class="test-menu-icon">↺</span>{{ t("重置服务端数据") }}</button>
    </div>
    <div v-if="communityContextMenu" class="community-context-dismiss" @click="communityContextMenu=null" @contextmenu.prevent="communityContextMenu=null"></div>
    <div v-if="communityContextMenu" class="community-context-menu" :style="{left:`${communityContextMenu.x}px`,top:`${communityContextMenu.y}px`}" @click.stop>
      <button @click="communityMenuAction('invite')"><Icon name="userPlus"/>{{ t("邀请至社区") }}</button>
      <button @click="communityMenuAction('settings')"><Icon name="gear"/>{{ t("社区设置") }}</button>
      <div></div>
      <button @click="communityContextMenu=null;communitySettingsOpen=true"><svg viewBox="0 0 24 24"><path d="M4 7h16M4 12h16M4 17h10"/></svg>{{ t("创建频道") }}</button>
      <div></div>
      <button class="leave-community" @click="communityMenuAction('leave')"><Icon name="logout"/>{{ t("离开社区") }}</button>
    </div>
    <div v-if="dmContextMenu" class="community-context-dismiss" @click="dmContextMenu=null" @contextmenu.prevent="dmContextMenu=null"></div>
    <div v-if="dmContextMenu" class="community-context-menu dm-context-menu" :style="{left:`${dmContextMenu.x}px`,top:`${dmContextMenu.y}px`}" @click.stop>
      <template v-if="dmContextMenu.kind==='群聊'">
        <button @click.stop="openDmEditMenu"><Icon name="edit"/>{{ t("编辑群聊") }}<b class="menu-arrow">›</b></button>
        <div></div>
        <button class="leave-community" @click="dmContextAction('leave')"><Icon name="logout"/>{{ t("退出群聊") }}</button>
      </template>
      <template v-else>
        <button @click="dmContextAction('profile')"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.4"/><path d="M5 20v-1.2a7 7 0 0 1 14 0V20z"/></svg>{{ t("打开他人主页") }}</button>
        <button @click="dmContextAction('clear')"><svg viewBox="0 0 24 24"><path d="M4 7h16M9 7V5h6v2M6.5 7l1 13h9l1-13"/></svg>{{ t("清除聊天记录") }}</button>
        <div></div>
        <button class="leave-community" @click="dmContextAction('remove')"><svg viewBox="0 0 24 24"><circle cx="10" cy="8" r="3.4"/><path d="M4 20v-1.2A6 6 0 0 1 16 18v2M17 9l5 5m0-5-5 5"/></svg>{{ t("删除好友") }}</button>
      </template>
    </div>
    <div v-if="dmEditMenu" class="community-context-menu dm-context-menu dm-edit-menu" :style="{left:`${dmEditMenu.x}px`,top:`${dmEditMenu.y}px`}" @click.stop>
      <button @click="dmContextAction('rename')"><Icon name="edit"/>{{ t("修改群聊名称") }}</button>
      <button @click="dmContextAction('members')"><Icon name="group"/>{{ t("管理群成员") }}</button>
      <div></div>
      <button @click="dmContextAction('groupSettings')"><Icon name="gear"/>{{ t("群聊设置") }}</button>
    </div>
    <aside class="channel-sidebar">
      <div v-if="['dm','dm-chat'].includes(activePage)" class="dm-sidebar"><label class="dm-search"><input :placeholder="t('寻找或开始新的对话')" @focus="notDeveloped(t('全局会话搜索'))"></label><div class="dm-list-heading dm-message-heading"><span>{{ t("私信") }}</span><button class="dm-add-cta" :aria-label="t('私信操作')" :aria-expanded="dmQuickMenu" @click.stop="dmQuickMenu=!dmQuickMenu">＋</button><div v-if="dmQuickMenu" class="dm-quick-menu" @click.stop><button @click="dmQuickMenu=false;createGroupDialogOpen=true"><Icon name="group"/><span>{{ t("创建群聊") }}</span></button></div></div><button v-for="contact in dmContacts" :key="contact.name" class="dm-contact" :class="{selected:activePage==='dm-chat'&&selectedDm===contact.name}" @click="openDm(contact)" @contextmenu.prevent.stop="openDmContext($event,contact)"><span class="dm-avatar"><img :src="dmAvatarSrc(contact.name)" :alt="contact.name"><i v-if="isDmUnread(contact)" class="dm-presence" :aria-label="t('有未读消息')" :title="t('有未读消息')" style="border:0;background:#ed4245;box-shadow:0 0 0 2.5px var(--sidebar-bg)"></i><i v-else class="dm-presence" :class="contact.online?'online':contact.idle?'idle':'offline'" :data-tooltip="contact.online?'在线':contact.idle?t('闲置'):t('离线')"></i></span><b class="dm-name">{{contact.name}}</b><span class="dm-close" role="button" :aria-label="t('关闭私信')" :title="t('关闭私信')" @click.stop="closeDm(contact)">×</span></button></div>
      <div v-else-if="activePage==='community'" class="community-sidebar-content">
      <div class="community-sidebar-banner"></div>
      <div class="guild-header"><button class="guild-header-title" :title="t('服务器菜单')" @click.stop="openCommunityContext($event, currentCommunity)"><b>{{selectedCommunity}}</b><Icon name="caret" class="guild-caret"/></button><div class="guild-header-actions"><button class="invite" :title="t('邀请至服务器')" :aria-label="t('邀请至服务器')" @click="openCommunityInvite()"><Icon name="userPlus"/></button></div></div>
      <div class="side-shortcuts"><button :class="{selected:active==='服务器指南'}" @click="active='服务器指南'"><Icon name="doc"/>{{ t("服务器指南") }}</button></div>
      <div class="channel-list" @wheel.prevent="scrollChat">
        <template v-for="group in groups" :key="group.title">
          <div class="category" :class="{collapsed:isCategoryCollapsed(group)}">
            <button class="category-toggle" :aria-expanded="!isCategoryCollapsed(group)" :title="isCategoryCollapsed(group)?t('展开分区'):t('收起分区')" @click="toggleCategory(group)"><span class="category-name">{{ group.title }}</span><Icon name="caret" class="category-caret"/></button>
            <button class="category-add" :title="t('新建频道')" @click="communitySettingsOpen=true">＋</button>
          </div>
          <template v-if="!isCategoryCollapsed(group)">
          <button v-for="channel in group.channels" :key="channel" class="channel-row" :class="{selected: active === channel}" :title="channel" @click="switchChannel(channel)"><Icon name="hash" class="channel-hash"/><span class="channel-label">{{ channel }}</span></button>
          </template>
        </template>
        <div v-if="communityChannels[selectedCommunity]?.length" class="category custom-channel-category" :class="{collapsed:customChannelsCollapsed}"><button class="category-toggle" :aria-expanded="!customChannelsCollapsed" @click="customChannelsCollapsed=!customChannelsCollapsed"><span class="category-name">{{ t("新建频道") }}</span><Icon name="caret" class="category-caret"/></button></div>
        <template v-if="!customChannelsCollapsed">
        <button v-for="channel in communityChannels[selectedCommunity]||[]" :key="channel" class="channel-row" :class="{selected:active===channel}" :title="channel" @click="switchChannel(channel)"><Icon name="hash" class="channel-hash"/><span class="channel-label">{{channel}}</span></button>
        </template>
      </div>
      </div>
      <div class="account-bar"><button class="profile-menu-trigger" :aria-label="t('打开个人资料菜单')" :title="t('个人资料')" @click.stop="profileMenuOpen=!profileMenuOpen;profileAccountMenu=false"><span class="profile-pic"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span></button><div class="profile-label"><b>{{demoNickname}}</b><small>{{backend.connected?(backend.authenticated?t('在线'):t('未登录')):t('离线')}}</small></div><Icon name="caret" class="account-caret"/><button aria-label="設定" :title="t('设置')" @click="profileMenuOpen=false;profileEditorOpen=false;settingsOpen = true"><Icon name="gear"/></button></div>
    </aside>
    <div class="sidebar-resizer" role="separator" tabindex="0" aria-orientation="vertical" :aria-label="t('拖动调整频道栏宽度')" :title="t('拖动调整频道栏宽度 · 双击复位')" :style="{ left: resizerX }" @mousedown.prevent="startSidebarResize" @dblclick="resetSidebarWidth" @keydown="onSidebarResizerKeydown"></div>
    <div v-if="profileMenuOpen" class="profile-menu-dismiss" @click="profileMenuOpen=false;profileAccountMenu=false"></div>
    <section v-if="profileMenuOpen" class="user-profile-popover" @click.stop>
      <div class="user-profile-banner"><div class="user-profile-avatar settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></div><div class="user-profile-status-bubble">UID {{demoUid||'—'}}</div></div>
      <div class="user-profile-details"><h2>{{demoNickname}}</h2><p>UID {{demoUid||'—'}}</p>
        <button class="profile-popover-action edit-profile-action" @click="profileMenuOpen=false;selectedSetting='账户';settingsOpen=true;profileEditorOpen=true"><Icon name="edit"/><span>{{ t("编辑个人资料") }}</span></button>
      </div>
      <button class="profile-popover-action switch-account-action" @click="profileAccountMenu=!profileAccountMenu"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.3"/><path d="M5 20v-1a7 7 0 0 1 14 0v1"/></svg><span>{{ t("切换账户") }}</span><b>›</b></button>
      <div v-if="profileAccountMenu" class="profile-popover-submenu account-switch-menu"><button @click="profileAccountMenu=false">{{demoNickname}}　当前账户</button><button @click="profileAccountMenu=false;openDemoLoginWindow()">＋　切换账户</button></div>
    </section>
    <main v-if="activePage==='community'" class="chat" :class="{'members-open':memberPanelOpen}" @click="closeComposerPopovers">
      <aside v-if="memberPanelOpen" class="member-panel" :aria-label="t('社区成员')">
        <header><h2>{{ t("社区成员") }}</h2><span>{{demoCommunityMemberList.length}}</span></header>
        <div class="member-panel-scroll">
          <section v-for="group in communityMemberGroups" :key="group.label">
            <h3>{{group.label}} — {{group.members.length}}</h3>
            <article v-for="member in group.members" :key="member.uid" class="member-panel-row" @click="memberProfile={name:member.name,uid:member.uid,avatar:'',avatarText:member.name[0]}">
              <span class="member-panel-avatar">{{member.name[0]}}<i :class="{offline:!member.online}"></i></span>
              <div><b>{{member.name}}</b><small>{{member.online?t('在线'):t('离线')}}</small></div>
              <span v-if="member.role!=='member'" class="member-panel-role">{{member.role==='owner'?t('所有者'):t('管理员')}}</span>
            </article>
          </section>
        </div>
      </aside>
      <header class="chat-top"><div class="channel-heading"><span class="hash-big"><Icon name="hash" class="channel-hash"/></span><b>{{ active==='服务器指南'?'服务器指南':active }}</b><span v-if="active!=='服务器指南'" class="separator"></span><span v-if="active!=='服务器指南'" class="topic">{{currentCommunity?.description||'本频道用于吹水，吹水和吹水。'}}</span></div><div class="chat-tools"><div class="chat-search"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.6-3.6"/></svg><input v-model="searchQuery" type="text" :placeholder="t('搜索')" :aria-label="t('搜索')" @focus="searchOpen=true"><button v-if="searchQuery" type="button" class="search-clear" :aria-label="t('关闭搜索')" :title="t('关闭搜索')" @click.stop="closeSearch">×</button><SearchPanel v-if="searchOpen && searchQuery" /></div><button v-if="active==='服务器指南'" class="guide-edit-button" :title="t('编辑服务器描述')" :aria-label="t('编辑服务器描述')" @click="openGuideEditor"><Icon name="edit"/></button><button v-else class="members-toggle" :class="{active:memberPanelOpen}" :title="t('社区成员')" :aria-label="t('社区成员')" :aria-expanded="memberPanelOpen" @click.stop="memberPanelOpen=!memberPanelOpen"><Icon name="group"/></button></div></header>
      <div v-if="unreadBanner&&active!=='服务器指南'" class="channel-unread-bar"><span>自从 {{unreadBannerInfo.time}} 以来有 {{unreadBannerInfo.count}} 条以上的新消息</span><button @click="notDeveloped(t('标记已读'))">{{ t("标记为已读") }}</button></div>
      <section v-if="active==='服务器指南'" class="server-guide-page">
        <div class="guide-content-wrap">
          <div class="guide-banner" :style="{backgroundImage:`linear-gradient(180deg, rgba(17,18,22,.35), rgba(17,18,22,.78)), url(${currentCommunity?.background || DEFAULT_COMM_BACKGROUND})`, backgroundSize:'cover', backgroundPosition:'center'}"><div class="guide-banner-art"><i></i><b></b><em>✦</em></div></div>
          <div class="guide-server-intro"><span class="guide-server-avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><div><h1>{{selectedCommunity}} <span>✿</span></h1><p>{{currentCommunity?.description||'欢迎来到我们的社区，一起交流分享吧。'}}</p></div><button @click="openCommunityInvite()">{{ t("邀请") }}</button></div>
          <div class="guide-main-grid">
            <aside class="guide-community-card"><div class="guide-community-banner"></div><div class="guide-community-body"><span class="guide-community-avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><h3>{{selectedCommunity}} <span>✿</span></h3><p><i></i>{{currentCommunity?.online||0}} 人在线　•　{{currentCommunity?.members||0}} 位成员</p><small>{{ t("创建日期：暂未提供") }}</small><div class="guide-highlight"><b>{{ t("热门活动") }}</b><span>{{ t("社区活动暂未开发") }}</span></div><div class="guide-tags"><span>{{ t("社区标签暂未开发") }}</span></div></div></aside>
          </div>
        </div>
      </section>
        <section v-else class="message-scroll" :style="{paddingBottom: uploads.length ? `${84 + Math.ceil(uploads.length / 3) * 230}px` : '84px'}" @wheel.prevent="scrollChat" @click="handleMessageAction" @contextmenu.prevent="openContextMenu">
        <div class="messages-inner">
          <MessageRow v-for="record in channelHistory" :key="record.id" :record="record" :reactions="messageReactions[record.id]" />
          <article v-if="!channelHistory.length" class="message system-message"><div class="system-row"><span class="system-glyph">#</span><b>{{ t("这里还没有消息，发送第一条消息吧。") }}</b></div></article>
        </div>
      </section>
      <div v-if="toast" class="toast community-toast">{{ toast }}</div>
      <div v-if="typingUsers.length && active!=='服务器指南'" class="typing-row" aria-live="polite">
        <span class="typing-dots" aria-hidden="true"><i></i><i></i><i></i></span>
        <b>{{typingUsers[0].author}}</b>
        <span v-if="typingUsers[0].tag" class="tag"><span class="tag-icon">{{typingUsers[0].tag.icon}}</span>{{typingUsers[0].tag.label}}</span>
        <span class="typing-text">{{ t("正在输入…") }}</span>
      </div>
      <div v-if="replyTarget && replyTarget.conversationKey===active" class="reply-composer-preview" role="status">
        <span class="reply-composer-accent"></span>
        <span class="reply-composer-avatar"><img v-if="replyTarget.avatarImage" :src="replyTarget.avatarImage" :alt="replyTarget.author"><template v-else>{{replyTarget.avatarText || replyTarget.author?.slice(0,1)}}</template></span>
        <span class="reply-composer-copy"><small>{{ t("正在回复") }}</small><b>@{{replyTarget.author}}</b><span :title="replyTarget.text">{{replyTarget.text || t('附件')}}</span></span>
        <button type="button" :aria-label="t('取消回复')" :title="t('取消回复')" @click="replyTarget=null">×</button>
      </div>
      <Composer v-if="active!=='服务器指南'" :conversation-key="active" :placeholder="`给 ${active} 发消息`" />
    </main>
    <main v-else-if="activePage==='dm'" class="friends-main" @click="friendMoreMenu='';dmQuickMenu=false">
      <section class="friends-column">
      <header class="friends-page-header"><div class="friends-page-title"><span><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="8.2" r="3.5"/><path d="M5.8 19.6a6.2 6.2 0 0 1 12.4 0"/></svg></span><b>{{ t("好友") }}</b></div><div class="friends-filter-tabs"><button v-for="tab in ['全部','待定']" :key="tab" :class="{active:friendFilter===tab}" @click="friendFilter=tab;friendMoreMenu=''">{{tab}}<span v-if="tab==='待定'&&demoFriendRequests.length+demoGroupRequests.length+demoCommunityRequests.length" class="friends-tab-dot" aria-hidden="true"></span></button></div></header>
      <section class="friends-page-scroll">
        <template v-if="friendFilter==='待定'">
          <div class="friends-list-heading"><div><h2>{{ t("待处理") }}</h2><p>好友申请、群聊邀请与社区申请都集中在这里。</p></div><span>{{demoFriendRequests.length+demoGroupRequests.length+demoCommunityRequests.length}} 项</span></div>
          <section class="friends-request-section"><h3>好友申请 <small>{{demoFriendRequests.length}}</small></h3><article v-for="request in demoFriendRequests" :key="`friend-${request.uid}`" class="friends-request-row"><span class="friends-avatar"><img :src="avatarByUid(request.uid)" :alt="request.name"></span><div class="friends-row-copy"><b>{{request.name}} <small>UID {{request.uid}}</small></b><p>{{request.outgoing?'等待对方回应':request.note}}</p></div><span v-if="request.outgoing" class="friends-pending-label">{{ t("等待回应") }}</span><template v-else><button class="friends-soft-button" @click="handleDemoFriendRequest(request,false)">{{ t("忽略") }}</button><button class="friends-primary-button" @click="handleDemoFriendRequest(request,true)">{{ t("接受") }}</button></template></article><div v-if="!demoFriendRequests.length" class="friends-empty">{{ t("没有待处理的好友申请") }}</div></section>
          <section class="friends-request-section"><h3>群聊邀请 <small>{{demoGroupRequests.length}}</small></h3><article v-for="request in demoGroupRequests" :key="`group-${request.uid}-${request.group}`" class="friends-request-row"><span class="friends-avatar group"><img :src="DEFAULT_GROUP_AVATAR" alt="群聊"></span><div class="friends-row-copy"><b>{{request.group}} <small>群 UID {{request.groupUid}}</small></b><p>{{request.outgoing?'你已申请加入，等待群主回应':t('{0} 申请加入群聊', request.name)}}</p></div><span v-if="request.outgoing" class="friends-pending-label">{{ t("等待回应") }}</span><template v-else><button class="friends-soft-button" @click="handleDemoGroupRequest(request,false)">{{ t("拒绝") }}</button><button class="friends-primary-button" @click="handleDemoGroupRequest(request,true)">{{ t("接受") }}</button></template></article><div v-if="!demoGroupRequests.length" class="friends-empty">{{ t("没有待处理的群聊邀请") }}</div></section>
          <section class="friends-request-section"><h3>社区申请 <small>{{demoCommunityRequests.length}}</small></h3><article v-for="request in demoCommunityRequests" :key="`community-${request.uid}-${request.community}`" class="friends-request-row"><span class="friends-avatar community"><img :src="DEFAULT_COMM_AVATAR" alt="社区"></span><div class="friends-row-copy"><b>{{request.community}} <small>{{ t("社区") }}</small></b><p>{{request.outgoing?'你已申请加入，等待社区管理员回应':t('{0} 申请加入社区', request.name)}}<template v-if="request.note"> · {{request.note}}</template></p></div><span v-if="request.outgoing" class="friends-pending-label">{{ t("等待回应") }}</span><template v-else><button class="friends-soft-button" @click="handleDemoCommunityRequest(request,false)">{{ t("拒绝") }}</button><button class="friends-primary-button" @click="handleDemoCommunityRequest(request,true)">{{ t("接受") }}</button></template></article><div v-if="!demoCommunityRequests.length" class="friends-empty">{{ t("没有待处理的社区申请") }}</div></section>
        </template>
        <template v-else>
          <label class="friends-directory-search"><svg class="search-glyph" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.4"/><path d="m15.4 15.4 4.6 4.6"/></svg><input v-model="friendSearchQuery" :placeholder="t('搜索好友')"></label>
          <div class="friends-list-heading"><div><h2>{{friendFilter==='在线'?t('在线'):t('全部')}} — {{filteredFriendDirectory.length}}</h2></div><button v-if="friendFilter==='全部'" class="friends-create-group-link" @click="createGroupDialogOpen=true">＋ 新建群聊</button></div>
          <article v-for="friend in filteredFriendDirectory" :key="friend.uid" class="friend-directory-row" @contextmenu.prevent="friendMoreMenu=friend.uid" @click="openDemoFriendChat(friend)"><span class="friends-avatar friend-face"><img :src="avatarByUid(friend.uid)" :alt="friend.name"><i :class="{offline:!friend.online,idle:friend.idle}"></i></span><div class="friends-row-copy"><div class="friends-name-line"><b>{{friend.remark||friend.name}}</b></div><p>{{friend.idle?'闲置':friend.online?t('在线'):t('离线')}}</p></div><button class="friends-round-action" :title="t('发送消息')" :aria-label="t('发送消息')" @click="openDemoFriendChat(friend)"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 11.6c0 3.6-3.6 6.5-8 6.5a9.8 9.8 0 0 1-2.6-.3L5 20l1.2-3.4A6.2 6.2 0 0 1 4 11.6C4 8 7.6 5.1 12 5.1s8 2.9 8 6.5Z"/></svg></button><div class="friend-more-wrap"><button class="friends-round-action" :title="t('更多')" :aria-label="t('更多')" @click.stop="friendMoreMenu=friendMoreMenu===friend.uid?'':friend.uid"><svg class="dots" viewBox="0 0 24 24" aria-hidden="true"><circle cx="5" cy="12" r="1.7"/><circle cx="12" cy="12" r="1.7"/><circle cx="19" cy="12" r="1.7"/></svg></button><div v-if="friendMoreMenu===friend.uid" class="friend-more-menu" @click.stop><button @click="openDemoFriendChat(friend);friendMoreMenu=''">{{ t("发送消息") }}</button><button @click="renameDemoFriend(friend);friendMoreMenu=''">{{ t("设置备注") }}</button><button class="danger" @click="removeDemoFriend(friend);friendMoreMenu=''">{{ t("删除好友") }}</button></div></div></article>
          <div v-if="!filteredFriendDirectory.length" class="friends-empty">{{ t("没有找到好友") }}</div>
          <section v-if="friendFilter==='全部'" class="friends-group-preview"><div class="friends-list-heading"><div><h2>{{ t("群聊") }}</h2><p>{{ t("你的群聊通讯录") }}</p></div></div><button v-for="group in demoGroups" :key="group.uid" class="friends-group-row" @click="openGroupChat(group)"><span class="friends-avatar group"><img :src="DEFAULT_GROUP_AVATAR" alt="群聊"></span><span><b>{{group.name}}</b><small>{{group.members.length}} 位成员</small></span><span>›</span></button></section>
          <section v-if="friendFilter==='全部'" class="friends-group-preview friends-community-preview"><div class="friends-list-heading"><div><h2>{{ t("社区") }}</h2><p>{{ t("你加入的社区") }}</p></div><button class="friends-create-group-link" @click="notDeveloped(t('发现社区'))">发现社区　›</button></div><button v-for="community in communities" :key="community.name" class="friends-group-row" @click="selectCommunity(community);active='服务器指南'"><span class="friends-avatar community"><img :src="community.icon || DEFAULT_COMM_AVATAR" :alt="community.name"></span><span><b>{{community.name}}</b><small>{{community.description||'社区成员'}}</small></span><span>›</span></button><div v-if="!communities.length" class="friends-empty">{{ t("还没有加入社区。") }}</div></section>
        </template>
      </section><div v-if="toast" class="toast friends-toast">{{toast}}</div>
      </section>
    </main>
    <main v-else-if="activePage==='dm-chat'" class="dm-main" @click="closeComposerPopovers">
      <header class="dm-top"><div><span class="dm-avatar"><img :src="dmAvatarSrc(selectedDm)" :alt="selectedDm"></span><b>{{selectedDm}}</b><small>{{dmContacts.find(c=>c.name===selectedDm)?.kind}}</small></div><div class="chat-search"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.6-3.6"/></svg><input v-model="searchQuery" type="text" :placeholder="t('搜索')" :aria-label="t('搜索')" @focus="searchOpen=true"><button v-if="searchQuery" type="button" class="search-clear" :aria-label="t('关闭搜索')" :title="t('关闭搜索')" @click.stop="closeSearch">×</button><SearchPanel v-if="searchOpen && searchQuery" /></div></header>
      <section class="dm-thread" @wheel.prevent="scrollChat" @click="handleMessageAction" @contextmenu.prevent="openContextMenu"><div class="dm-thread-intro"><span class="dm-avatar large"><img :src="dmAvatarSrc(selectedDm)" :alt="selectedDm"></span><h2>{{selectedDm}}</h2><p>这是你和 {{selectedDm}} 的私信开头。</p></div><div class="dm-thread-messages"><MessageRow v-for="entry in dmThread" :key="entry.id" :record="entry" :reactions="messageReactions[entry.id]" :images="entry.images" /></div></section>
      <div v-if="replyTarget && replyTarget.conversationKey===`dm:${selectedDm}`" class="reply-composer-preview" role="status">
        <span class="reply-composer-accent"></span>
        <span class="reply-composer-avatar"><img v-if="replyTarget.avatarImage" :src="replyTarget.avatarImage" :alt="replyTarget.author"><template v-else>{{replyTarget.avatarText || replyTarget.author?.slice(0,1)}}</template></span>
        <span class="reply-composer-copy"><small>{{ t("正在回复") }}</small><b>@{{replyTarget.author}}</b><span :title="replyTarget.text">{{replyTarget.text || t('附件')}}</span></span>
        <button type="button" :aria-label="t('取消回复')" :title="t('取消回复')" @click="replyTarget=null">×</button>
      </div>
      <Composer :conversation-key="`dm:${selectedDm}`" :placeholder="`给 ${selectedDm} 发消息`" />
      <div v-if="toast" class="toast">{{toast}}</div>
    </main>
    <main v-else class="discover-page" aria-label="发现页面"></main>
  </div>
  <!-- 消息右键菜单 / emoji 选择器 / 查看反应：fixed 定位，社区频道与私信、群聊共用 -->
  <div v-if="contextMenu" class="context-menu" :style="{left: `${contextMenu.x}px`, top: `${contextMenu.y}px`}" @click.stop="handleMessageAction">
    <div class="context-reactions"><button v-for="emoji in QUICK_EMOJIS" :key="emoji" data-action="react" :data-emoji="emoji">{{ emoji }}</button></div>
    <button class="context-item" data-action="picker"><span>☻</span>添加反应 <b>›</b></button>
    <button v-if="messageReactions[contextMenu.id]?.length" class="context-item" data-action="view-reactions"><span><Icon name="smile"/></span>{{ t("查看反应") }}</button>
    <div class="context-separator"></div>
    <button class="context-item" data-action="reply"><span><svg viewBox="0 0 24 24"><polyline points="9 14 4 9 9 4"/><path d="M20 20v-7a4 4 0 0 0-4-4H4"/></svg></span>{{ t("回复") }}</button>
    <button class="context-item" data-action="forward"><span><svg viewBox="0 0 24 24"><polyline points="15 14 20 9 15 4"/><path d="M4 20v-7a4 4 0 0 1 4-4h12"/></svg></span>{{ t("转发") }}</button>
    <div class="context-separator"></div>
    <button v-if="contextMenu.attachment" class="context-item" data-action="download"><span><svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg></span>{{ t("下载") }}</button>
    <button class="context-item" data-action="copy"><span><svg viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg></span>{{ t("复制文字") }}</button>
    <button class="context-item" data-action="unread"><span><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3" fill="currentColor" stroke="none"/></svg></span>{{ t("标记未读") }}</button>
    <button class="context-item" data-action="recall"><span><svg viewBox="0 0 24 24"><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/></svg></span>{{ t("撤回") }}</button>
    <button v-if="contextMenu.mine" class="context-item delete-message-action" data-action="delete"><span><svg viewBox="0 0 24 24"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg></span>{{ t("删除消息") }}</button>
    <button class="context-item" data-action="link"><span><svg viewBox="0 0 24 24"><path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/></svg></span>{{ t("复制消息链接") }}</button>
    <div class="context-separator"></div>
    <button class="context-item report" data-action="report"><span><svg viewBox="0 0 24 24"><path d="M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z"/><line x1="4" y1="22" x2="4" y2="15"/></svg></span>{{ t("举报消息") }}</button>
  </div>
  <div v-if="emojiPicker" class="emoji-picker" :data-message-id="emojiPicker.id" :style="{left: `${emojiPicker.x}px`, top: `${emojiPicker.y}px`, width: 'auto', minWidth: '96px', gridTemplateColumns: 'repeat(2, 1fr)', gap: '5px', padding: '8px'}" @click.stop="handleMessageAction">
    <button v-for="emoji in PICKER_EMOJIS" :key="emoji" data-action="react" :data-emoji="emoji" style="height:42px;min-width:44px;font-size:22px;border-radius:6px;background:transparent">{{ emoji }}</button>
    <button type="button" @click.stop="openEmojiPanelFromReaction" style="grid-column:1 / -1;height:34px;font-size:12px;display:flex;align-items:center;justify-content:center;background:#303137;color:#d0d1d6;border-radius:6px;cursor:pointer">更多反应</button>
  </div>
  <div v-if="reactionDetails" class="reaction-backdrop" @click.self="reactionDetails = null">
    <div class="reaction-modal">
      <div class="reaction-modal-head"><b>{{ t("查看反应") }}</b><button @click="reactionDetails = null" :aria-label="t('关闭')">×</button></div>
      <div v-for="reaction in reactionDetails.items" :key="reaction.emoji" class="reaction-group"><div class="reaction-group-title">{{ reaction.emoji }} <span>{{ reaction.count }}</span></div><div v-for="person in reaction.users" :key="person" class="reaction-person"><span class="person-avatar">{{ person[0] }}</span><b>{{ person }}</b><small>已回应</small></div></div>
      <div v-if="!reactionDetails.items.length" class="empty-reactions">{{ t("还没有可显示的反应") }}</div>
    </div>
  </div>
  <div v-if="friendRequestModal" class="friend-request-backdrop" @click.self="friendRequestModal=false">
    <section class="friend-request-window" role="dialog" aria-modal="true" aria-labelledby="friend-request-title">
      <button class="friend-request-close" :aria-label="t('关闭')" @click="friendRequestModal=false">×</button>
      <div class="friend-request-emblem">♧</div>
      <h1 id="friend-request-title">{{ t("添加好友") }}</h1>
      <p>{{ t("通过邮箱向朋友发送好友申请。") }}</p>
      <label>{{ t("好友邮箱") }}<input v-model="demoForm.email" type="email" placeholder="name@example.com" @keydown.enter.prevent="sendFriendRequestFromModal"></label>
      <label>申请留言 <span>{{ t("可选") }}</span><textarea v-model="demoForm.note" rows="4" :placeholder="t('写一句话介绍自己')"></textarea></label>
      <footer><button class="friend-request-cancel" @click="friendRequestModal=false">{{ t("取消") }}</button><button class="friend-request-submit" @click="sendFriendRequestFromModal">{{ t("发送好友申请") }}</button></footer>
    </section>
  </div>
  <div v-if="backend.authenticating || demoLoginOpen" class="demo-login-backdrop" @click.self="backend.authenticated&&(demoLoginOpen=false)">
    <section class="demo-login-window" role="dialog" aria-modal="true" aria-labelledby="demo-login-title">
      <button v-if="backend.authenticated" class="friend-request-close" :aria-label="t('关闭')" @click="demoLoginOpen=false">×</button>
      <div class="demo-login-logo"><img src="/logo.png" alt="LOGO"></div>
      <h1 id="demo-login-title">{{backend.reconnecting?t('正在重新连接'):backend.authenticating?t('正在恢复登录'):t('欢迎回来')}}</h1><p>{{backend.reconnecting?t('连接中断，正在自动重连…'):backend.authenticating?t('正在验证登录状态…'):backend.connecting?t('正在连接服务器…'):backend.connected?t('登录账号，继续使用 Baka Community。'):t('服务器连接失败，请稍后重试。')}}</p>
      <template v-if="!backend.authenticating && !backend.reconnecting">
        <div class="demo-login-tabs"><button :class="{active:demoAuthMode==='登录'}" @click="demoAuthMode='登录'">{{ t("登录") }}</button><button :class="{active:demoAuthMode==='注册'}" @click="demoAuthMode='注册'">{{ t("注册") }}</button></div>
        <label v-if="demoAuthMode==='注册'">昵称<input v-model="demoAuth.username" :placeholder="t('例如：小明')"></label>
        <label v-if="demoAuthMode==='注册'">{{ t("邮箱") }}<input v-model="demoAuth.email" placeholder="name@example.com"></label>
        <label v-else>{{ t("UID 或邮箱") }}<input v-model="demoAuth.identity" placeholder="100001 / name@example.com"></label>
        <label>{{ t("密码") }}<input v-model="demoAuth.password" type="password" :placeholder="t('输入密码')" @keydown.enter.prevent="submitDemoLoginWindow"></label>
        <button class="demo-login-submit" @click="backend.connected?submitDemoLoginWindow():api.connect()">{{backend.connecting?'连接中…':backend.connected?demoAuthMode:'重新连接'}}</button>
      </template><div v-else class="login-restoring-indicator"><i></i><span>{{t(backend.reconnecting?'正在重新连接…':'自动登录中…')}}</span></div><small>{{ t("数据通过 WebSocket + protobuf 与服务器同步。") }}</small>
    </section>
  </div>
  <div v-if="memberProfile" class="member-profile-backdrop" @click.self="memberProfile=null">
    <section class="member-profile-card" role="dialog" aria-modal="true" aria-label="成员个人资料">
      <button class="member-profile-close" :aria-label="t('关闭')" @click="memberProfile=null">×</button>
      <div class="member-profile-banner"></div>
      <div class="member-profile-content"><div class="member-profile-avatar" :class="memberProfile.avatar">{{memberProfile.avatarText}}</div><span class="member-online-dot"></span><h2>{{memberProfile.name}}</h2><p class="member-profile-handle">UID <span>{{memberProfile.uid||'暂未提供'}}</span></p><div class="member-profile-actions"><button @click="message=`@${memberProfile.name} `;memberProfile=null;nextTick(()=>composerInput?.focus())">{{ t("消息") }}</button><button :title="t('更多')" @click="notDeveloped('成员更多操作')">•••</button></div><div class="member-profile-about"><label>{{ t("关于我") }}</label><p>{{ t("暂未提供") }}</p><label>{{ t("服务器成员") }}</label><p>{{selectedCommunity||'暂未提供'}}</p><label>{{ t("成员加入时间") }}</label><p>{{ t("暂未提供") }}</p></div></div>
    </section>
  </div>
  <div v-if="communitySettingsOpen" class="community-settings-backdrop" @click.self="closeCommunitySettings()">
    <section class="community-settings-window" role="dialog" aria-modal="true" :aria-label="t('社区设置')">
      <aside class="community-settings-sidebar"><header><span class="community-settings-icon"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="communitySettingsName"></span><div><b>{{communitySettingsName}}</b><small>{{ t("社区设置") }}</small></div><button :aria-label="t('关闭社区设置')" @click="closeCommunitySettings()">×</button></header>
        <nav><button class="selected" @click="scrollToCommunitySection('community-overview')"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v5m0-8h.01"/></svg>{{ t("社区概览") }}</button><button @click="scrollToCommunitySection('community-channels')"><Icon name="list"/>{{ t("频道管理") }}</button><button class="dissolve-nav" @click="dissolveConfirm=true"><svg viewBox="0 0 24 24"><path d="M4 7h16M10 11v6m4-6v6M6 7l1 14h10l1-14M9 7V4h6v3"/></svg>{{ t("解散社区") }}</button></nav>
      </aside>
      <main class="community-settings-main"><header class="community-settings-header"><b>{{communitySettingsName}} 设置</b><button :aria-label="t('关闭社区设置')" @click="closeCommunitySettings()">×</button></header>
        <div class="community-settings-scroll">
          <section id="community-overview" class="community-setting-section"><h1>{{ t("社区概览") }}</h1><p class="community-settings-hint">{{ t("管理社区的名称、介绍和外观。") }}</p>
            <div class="community-setting-row"><div class="community-setting-label"><b>社区头像</b><small>{{ t("选择一张图片作为社区标识。") }}</small></div><div class="community-avatar-editor"><span class="community-settings-icon large"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="communitySettingsName"></span><label class="settings-upload-button">{{ t("更换头像") }}<input type="file" accept="image/*" @change="handleCommunityAsset('avatar',$event)"></label></div></div>
            <label class="community-field"><span>{{ t("社区名称") }}</span><input v-model="communitySettingsName" maxlength="60" :placeholder="t('输入社区名称')"></label>
            <label class="community-field"><span>{{ t("介绍文字") }}</span><textarea v-model="communitySettingsDescription" maxlength="300" rows="3" placeholder="介绍一下你的社区"></textarea><small>{{communitySettingsDescription.length}} / 300</small></label>
            <div class="community-setting-row background-upload-row"><div class="community-setting-label"><b>背景图片</b><small>显示在社区侧栏顶部。</small></div><label class="settings-upload-button">{{ t("上传背景图片") }}<input type="file" accept="image/*" @change="handleCommunityAsset('banner',$event)"></label></div>
            <div class="community-banner-preview" :style="currentCommunity?.background?{backgroundImage:`url(${currentCommunity.background})`}:{}"><span>{{communitySettingsName}}</span></div>
          </section>
        <section class="community-setting-section"><h2>{{ t("服务器资料") }}</h2><p class="community-settings-hint">头像、背景图片与名称会同步显示在竖栏和顶部横幅。</p>
          <div class="community-asset-row"><span class="community-asset-preview avatar"><img :src="currentCommunity?.icon || DEFAULT_COMM_AVATAR" :alt="selectedCommunity"></span><div><b>{{ t("服务器头像") }}</b><small>建议 512×512 的方形图片，显示在左侧竖栏</small></div><button @click="setCommunityAsset('avatar')">{{ t("上传头像") }}</button><input ref="communityAvatarInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleCommunityAsset('avatar',$event)"></div>
          <div class="community-asset-row"><span class="community-asset-preview banner" :style="currentCommunity?.background?{backgroundImage:`url(${currentCommunity.background})`}:{}"></span><div><b>{{ t("服务器背景图片") }}</b><small>显示在频道栏顶部的横幅与服务器指南</small></div><button @click="setCommunityAsset('banner')">上传背景</button><input ref="communityBannerInput" class="demo-hidden-input" type="file" accept="image/*" @change="handleCommunityAsset('banner',$event)"></div>
        </section>
        <section id="community-channels" class="community-setting-section"><h2>{{ t("频道管理") }}</h2><p class="community-settings-hint">下面是这个社区当前的频道列表，与左侧频道栏一致。鼠标移到频道上点右侧 × 删除，删除会在「保存更改」后生效。</p><div class="new-channel-form"><span>#</span><input v-model="newCommunityChannel" :placeholder="t('新频道名称')" @keydown.enter.prevent="addCommunityChannel"><button @click="addCommunityChannel">新增频道</button></div>
          <div class="settings-channel-groups"><template v-for="group in channelListOf(selectedCommunity)" :key="group.title"><div class="settings-channel-category" :class="{collapsed:isCategoryCollapsed(group)}"><button class="category-toggle" :aria-expanded="!isCategoryCollapsed(group)" :title="isCategoryCollapsed(group)?t('展开分区'):t('收起分区')" @click="toggleCategory(group)"><span class="category-name">{{group.title}}</span><Icon name="caret" class="category-caret"/></button><span class="settings-channel-count">{{group.channels.length}}</span></div><template v-if="!isCategoryCollapsed(group)"><div v-for="channel in group.channels" :key="`${group.title}::${channel}`" class="settings-channel-row"><span class="channel-hash">#</span><span class="channel-label">{{channel}}</span><button class="settings-channel-remove" :aria-label="t('删除频道 {0}', channel)" :title="t('删除频道 {0}', channel)" @click.stop="openChannelDelete(selectedCommunity,group.title,channel)">×</button></div></template></template>
          <div v-if="!channelListOf(selectedCommunity).length" class="guide-draft-empty">{{ t("这个社区还没有频道，用上面的输入框新增一个。") }}</div>
        </div></section>
          <section class="community-setting-section danger-zone"><h2>{{ t("解散社区") }}</h2><p class="community-settings-hint">{{ t("解散后，社区将从你的列表中移除。") }}</p><button @click="dissolveConfirm=true">{{ t("解散社区") }}</button></section>
        </div>
        <footer class="community-settings-footer"><span>{{ t("更改尚未保存") }}</span><button @click="closeCommunitySettings()">{{ t("取消") }}</button><button class="save-community-settings" @click="saveCommunitySettings">{{ t("保存更改") }}</button></footer>
        <div v-if="channelDeleteTarget" class="server-dissolve-overlay"><section><h2>删除“{{channelDeleteTarget.channel}}”？</h2><p>{{ t("该频道会从这个社区移除，点「保存更改」后生效。") }}</p><footer><button @click="channelDeleteTarget=null">{{ t("取消") }}</button><button @click="confirmChannelDelete">{{ t("删除频道") }}</button></footer></section></div>
        <div v-if="dissolveConfirm" class="server-dissolve-overlay"><section><h2>解散“{{communitySettingsName}}”？</h2><p>此操作会从你的社区列表中移除该社区。</p><footer><button @click="dissolveConfirm=false">{{ t("取消") }}</button><button @click="confirmDissolveCommunity">{{ t("解散社区") }}</button></footer></section></div>
      </main>
    </section>
  </div>
  <div v-if="createCommunityOpen" class="dialog-backdrop" @click.self="createCommunityOpen=false"><section class="create-community-dialog"><button class="dialog-close" @click="createCommunityOpen=false">×</button><h2>{{ t("创建社区") }}</h2><p>{{ t("为你的社区取个名字。") }}</p><input v-model="newCommunityName" :placeholder="t('社区名称')" @keydown.enter.prevent="createCommunity"><footer><button @click="createCommunityOpen=false">{{ t("取消") }}</button><button class="create-community-submit" @click="createCommunity">{{ t("创建社区") }}</button></footer></section></div>
  <div v-if="createGroupDialogOpen" class="dialog-backdrop" @click.self="createGroupDialogOpen=false"><section class="create-community-dialog"><button class="dialog-close" @click="createGroupDialogOpen=false">×</button><h2>{{ t("创建群聊") }}</h2><p>{{ t("为你的群聊取个名字。") }}</p><input v-model="demoForm.groupName" placeholder="群聊名称" @keydown.enter.prevent="createDemoGroup"><footer><button @click="createGroupDialogOpen=false">{{ t("取消") }}</button><button class="create-community-submit" @click="createDemoGroup">{{ t("创建群聊") }}</button></footer></section></div>
  <div v-if="groupManageOpen" class="dialog-backdrop" @click.self="groupManageOpen=false"><section class="create-community-dialog group-manage-dialog"><button class="dialog-close" @click="groupManageOpen=false">×</button><h2>{{ t("管理群成员") }}</h2><p v-if="groupManageTarget">{{groupManageTarget.name}} · 群 UID {{groupManageTarget.uid}} · {{groupManageTarget.members.length}} 位成员</p><div class="group-manage-members"><div v-for="member in groupManageTarget?.members || []" :key="member.uid" class="friend-group-member"><span class="friends-avatar tiny"><img :src="avatarByUid(member.uid)" :alt="member.name"></span><span class="friend-member-name"><b>{{member.name}}</b><small>UID {{member.uid}}</small></span><span class="friend-role-tag">{{member.role}}</span><button v-if="Number(member.uid)!==Number(backend.uid)" class="friends-soft-button compact-button" @click="toggleDemoGroupRole(groupManageTarget,member)">{{ t("设为群主") }}</button><button v-if="Number(member.uid)!==Number(backend.uid)" class="friends-icon-button danger" :title="t('移除成员')" @click="removeDemoGroupMember(groupManageTarget,member)">−</button></div><div v-if="!(groupManageTarget?.members||[]).length" class="friends-empty">{{ t("还没有群成员。") }}</div></div><div class="group-manage-invite"><input v-model="demoForm.memberUid" :placeholder="t('输入好友 UID 邀请加入')"><button class="friends-soft-button" @click="addDemoGroupMember(groupManageTarget)">{{ t("邀请好友") }}</button></div><footer><button @click="renameDemoGroup(groupManageTarget)">改名</button><button class="danger" @click="deleteDemoGroup(groupManageTarget)">{{ t("解散群聊") }}</button><button class="create-community-submit" @click="groupManageOpen=false">完成</button></footer></section></div>
  <div v-if="inviteModal" class="dialog-backdrop invite-backdrop" @click.self="inviteModal = false">
    <section class="invite-dialog" role="dialog" aria-modal="true" aria-labelledby="invite-title">
      <button class="dialog-close" :aria-label="t('关闭')" @click="inviteModal = false">×</button>
      <div class="invite-content">
        <h2 id="invite-title">添加朋友到 {{selectedCommunity}}</h2>
        <p class="dialog-subtitle">当前服务端没有待接受的社区邀请；社区所有者会将好友直接加入。</p>
        <label class="friend-search"><Icon name="search"/><input v-model="friendSearch" :placeholder="t('搜索好友')"></label>
        <div class="friend-list">
          <div v-for="friend in filteredFriends" :key="friend.handle" class="friend-row"><span class="friend-avatar" :class="friend.color">{{ friend.avatar }}</span><span class="friend-names"><b>{{ friend.name }}</b><small>{{ friend.handle }}</small></span><button @click="inviteFriendToCommunity(friend)">直接添加</button></div>
          <div v-if="!filteredFriends.length" class="no-friends">{{ t("没有找到好友") }}</div>
        </div>
      </div>
      <div class="invite-link-panel"><label>或者，向好友发送服务器邀请链接</label><div class="invite-url"><input v-model="inviteUrl" readonly><button @click="copyInvite">{{ t("复制") }}</button></div><p>您的邀请链接将在 30 天后过期。 <button @click="notDeveloped(t('社区邀请链接'))">{{ t("编辑邀请链接") }}</button></p></div>
    </section>
  </div>
  <div v-if="joinModal" class="friend-request-backdrop" @click.self="joinModal=false">
    <section class="friend-request-window" role="dialog" aria-modal="true" aria-labelledby="join-community-title">
      <button class="friend-request-close" :aria-label="t('关闭')" @click="joinModal=false">×</button>
      <div class="friend-request-emblem">◆</div>
      <h1 id="join-community-title">{{ t("申请加入社区") }}</h1>
      <p>{{ t("输入社区 UID，提交加入申请。") }}</p>
      <label>{{ t("社区 UID") }}<input v-model="joinCode" type="number" :placeholder="t('例如：10001')" @keydown.enter.prevent="joinServer"></label>
      <label>申请留言 <span>{{ t("可选") }}</span><textarea v-model="joinNote" rows="4" :placeholder="t('写一句话介绍自己')"></textarea></label>
      <footer><button class="friend-request-cancel" @click="joinModal=false">{{ t("取消") }}</button><button class="friend-request-submit" @click="joinServer">{{ t("发送申请") }}</button></footer>
    </section>
  </div>
  <div v-if="joinGroupOpen" class="friend-request-backdrop" @click.self="joinGroupOpen=false">
    <section class="friend-request-window" role="dialog" aria-modal="true" aria-labelledby="join-group-title">
      <button class="friend-request-close" :aria-label="t('关闭')" @click="joinGroupOpen=false">×</button>
      <div class="friend-request-emblem">◌</div>
      <h1 id="join-group-title">{{ t("申请加入群聊") }}</h1>
      <p>{{ t("输入群 UID，提交加入申请。") }}</p>
      <label>{{ t("群 UID") }}<input v-model="joinGroupUid" type="number" :placeholder="t('例如：10001')" @keydown.enter.prevent="submitJoinGroup"></label>
      <footer><button class="friend-request-cancel" @click="joinGroupOpen=false">{{ t("取消") }}</button><button class="friend-request-submit" @click="submitJoinGroup">{{ t("发送申请") }}</button></footer>
    </section>
  </div>
  <div v-if="settingsOpen" class="settings-backdrop" @click.self="closeSettings">
    <section class="settings-window" :class="{'settings-suspended':profileEditorOpen}" role="dialog" aria-modal="true" :aria-label="t('用户设置')">
      <aside class="settings-sidebar">
        <div class="settings-profile"><span class="settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span><span><b>{{demoNickname}}</b><button @click="profileEditorOpen=true">编辑个人资料　✎</button></span></div>
        <label class="settings-search"><Icon name="search"/><input :placeholder="t('搜索')" @focus="notDeveloped(t('设置搜索'))"></label>
        <nav class="settings-nav">
          <button :class="{active:selectedSetting==='账户'}" @click="selectSetting('账户')"><svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3.2"/><path d="M5 20v-1.2a7 7 0 0 1 14 0V20z"/></svg>{{ t("账户") }}</button>
          <button :class="{active:selectedSetting==='Baka'}" @click="selectSetting('Baka')"><img class="settings-nav-logo" src="/logo.png" alt="">Baka</button>
          <button :class="{active:selectedSetting==='文件'}" @click="selectSetting('文件')"><svg viewBox="0 0 24 24"><path d="M6 2h8l6 6v14H6z"/><path d="M14 2v6h6"/></svg>{{ t("文件") }}</button>
          <div class="settings-divider"></div>
          <div class="settings-group-label">体验</div>
          <button :class="{active:selectedSetting==='外观'}" @click="selectSetting('外观')"><svg viewBox="0 0 24 24"><path d="M12 3a9 9 0 1 0 0 18h1.3a2 2 0 0 0 1.5-3.3 1.8 1.8 0 0 1 1.4-3h1.1A3.7 3.7 0 0 0 21 11c0-4.4-4-8-9-8Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7.5" r="1"/><circle cx="15" cy="8" r="1"/></svg>{{ t("外观") }}</button>
          <button :class="{active:selectedSetting==='系统'}" @click="selectSetting('系统')"><svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="12" rx="2"/><path d="M8 21h8m-4-4v4M7 9h.01M10 9h.01M13 9h.01M16 9h.01M7 13h10"/></svg>{{ t("系统") }}</button>
          <button :class="{active:selectedSetting==='语言'}" @click="selectSetting('语言')"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/></svg>{{ t("语言") }}</button>
          <div class="settings-divider"></div>
          <button class="logout-setting" @click="closeSettings();demoLogout()"><Icon name="logout"/>{{ t("登出") }}</button>
        </nav>
      </aside>
      <main class="settings-main">
        <header class="settings-header"><b>{{ selectedSetting === '外观' ? '外观' : selectedSetting }}</b><button aria-label="关闭设置" @click="closeSettings">×</button></header>
        <div class="settings-scroll">
          <div v-if="selectedSetting==='账户'" class="account-settings-content">
            <section class="account-section"><h1>{{ t("账号信息") }}</h1><div class="account-info-row"><span>{{ t("用户名") }}</span><b>{{demoNickname}}</b><button @click="profileEditorOpen=true">{{ t("编辑") }}</button></div><div class="account-info-row"><span>{{ t("账号 UID") }}</span><b>{{demoUid||'—'}}</b></div><div class="account-info-row"><span>{{ t("邮箱") }}</span><b>{{demoEmail||'未绑定'}}</b><button @click="emailEditOpen=!emailEditOpen">{{emailEditOpen?t('取消'):t('编辑')}}</button></div><div v-if="emailEditOpen" class="account-inline-form"><input v-model="demoEmail" type="email" placeholder="name@example.com"><button class="settings-row-button" @click="saveEmail">保存邮箱</button></div></section>
            <section class="account-section"><h2>密码和安全中心</h2><div class="account-info-row"><span>{{ t("密码") }}</span><b>••••••••</b><button @click="passwordEditOpen=!passwordEditOpen">{{passwordEditOpen?t('取消'):t('修改')}}</button></div><div v-if="passwordEditOpen" class="account-inline-form password-form"><input v-model="oldPassword" type="password" :placeholder="t('当前密码')"><input v-model="newPassword" type="password" :placeholder="t('新密码')"><button class="settings-row-button" @click="savePassword">保存密码</button></div></section>
          </div>
          <div v-else-if="selectedSetting==='外观'" class="appearance-content">
            <h1>{{ t("主题") }}</h1>
            <h3>{{ t("默认主题") }}</h3>
            <div class="theme-options"><button v-for="theme in ['light','gray','dark','black','device']" :key="theme" class="theme-swatch" :class="[theme,{chosen:themeChoice===theme}]" :aria-label="theme" @click="themeChoice=theme"><i v-if="themeChoice===theme">✓</i></button></div>
          </div>
          <div v-else-if="selectedSetting==='Baka'" class="baka-settings-content">
            <section class="settings-section">
              <h1>{{ t("Baka 音效") }}</h1>
              <div class="settings-row"><div class="settings-row-copy"><b>{{ t("音效文件") }}</b><small>内置 Baka.mp3，本地播放，不联网。</small></div><img class="settings-row-logo" src="/logo.png" alt="Baka"></div>
              <div class="settings-row"><div class="settings-row-copy"><b>{{ t("音量") }}</b><small>{{ Math.round(bakaVolume * 100) }}%</small></div><input class="volume-slider" type="range" min="0" max="1" step="0.01" v-model.number="bakaVolume" @input="playBaka('logo', true)" aria-label="Baka 音量"></div>
              <div class="settings-row"><div class="settings-row-copy"><b>{{ t("发送消息时播放") }}</b><small>每次发出消息后播放一次。</small></div><button class="toggle" :class="{on:bakaOnSend}" @click="bakaOnSend=!bakaOnSend"><i></i></button></div>
              <div class="settings-row"><div class="settings-row-copy"><b>{{ t("收到消息时播放") }}</b><small>收到服务器实时推送的消息时播放一次。</small></div><button class="toggle" :class="{on:bakaOnReceive}" @click="bakaOnReceive=!bakaOnReceive"><i></i></button></div>
              <div class="settings-row"><div class="settings-row-copy"><b>{{ t("双击 LOGO 播放") }}</b><small>双击左侧竖栏顶部的 LOGO 播放一次。</small></div><button class="toggle" :class="{on:bakaOnLogo}" @click="bakaOnLogo=!bakaOnLogo"><i></i></button></div>
            </section>
          </div>
          <div v-else-if="selectedSetting==='文件'" class="account-settings-content">
            <section class="account-section"><h1>{{ t("我的文件") }}</h1><p>文件保存在服务器，上传与下载均使用分片传输。</p></section>
            <input ref="demoFilesInput" class="demo-hidden-input" type="file" multiple @change="addDemoFiles"><input ref="resumeFileInput" class="demo-hidden-input" type="file" @change="bindResumeUpload">
            <div class="demo-panel-heading"><div><h2>传输与文件列表</h2><p>{{demoFiles.length}} 个文件</p></div><button class="demo-primary-button" @click="selectDemoFiles">＋ 上传文件</button></div>
            <div class="demo-transfer-card"><div class="demo-transfer-icon">⇅</div><div><b>{{activeTransfer?.name||t('文件传输状态')}}</b><small>{{activeTransfer?`${activeTransfer.status} · ${activeTransfer.progress||0}%`:t('当前没有进行中的传输')}}</small></div><span class="demo-transfer-state"><i :class="{paused:!activeTransfer||activeTransfer.status==='已暂停'}"></i>{{activeTransfer?.status||'空闲'}}</span><button v-if="activeTransfer?.transferId" class="demo-soft-button" @click="toggleActiveTransfer">{{['已暂停','已中断','等待选择原文件'].includes(activeTransfer.status)?t('继续'):t('暂停')}}</button><button v-if="activeTransfer?.transferId" class="demo-icon-action danger" :title="t('取消传输')" @click="api.cancelTransfer(activeTransfer)">×</button></div>
            <div class="demo-file-list"><article v-for="file in demoFiles" :key="file.id" class="demo-file-row"><div class="demo-file-thumb"><img v-if="file.url&&file.type==='image'" :src="file.url" :alt="file.name"><span v-else class="demo-file-ext" :class="file.format.toLowerCase()">{{file.format}}</span></div><div class="demo-row-copy"><b>{{file.name}}</b><p>{{file.format}} · {{formatFileSize(file.size)}} <span>·</span> {{ t(file.status) }}</p><div class="demo-file-progress"><i :class="{paused:file.status==='已暂停'}" :style="{width:`${file.progress??(file.status==='已完成'?100:0)}%`}"></i></div></div><div class="demo-file-actions"><button v-if="file.status==='已完成'" class="demo-soft-button" @click="downloadDemoFile(file)">{{ t("下载") }}</button><button v-if="file.localId&&file.transferId&&!['已完成','已取消','失败','正在取消'].includes(file.status)" class="demo-soft-button" @click="toggleFileTransfer(file)">{{['已暂停','已中断','等待选择原文件'].includes(file.status)?(file.status==='等待选择原文件'?t('选择原文件续传'):t('继续')):t('暂停')}}</button><button class="demo-icon-action danger" title="删除文件" @click="deleteDemoFile(file)">×</button></div></article><div v-if="!demoFiles.length" class="demo-empty-state">{{ t("还没有文件，上传一个文件开始使用。") }}</div></div>
            <div class="demo-doc-note"><b>传输操作</b><span>上传初始化 · 文件分片 · 完成校验 · 断点续传 · 下载 · 暂停 / 继续 / 取消 · 文件列表 / 删除</span><small>文件通过 WebSocket 分片传输，并由服务器持久化。</small></div>
          </div>

          <div v-else-if="selectedSetting==='语言'" class="account-settings-content">
            <section class="account-section"><h1>{{ t("语言") }}</h1><p>{{ t("选择界面显示语言。") }}</p><div class="language-options"><button :class="{active:lang==='zh'}" @click="setLang('zh')">{{ t("中文") }}</button><button :class="{active:lang==='en'}" @click="setLang('en')">English</button></div></section>
          </div>
          <div v-else class="settings-placeholder"><h1>{{ selectedSetting }}</h1><p>在这里调整 {{ selectedSetting }} 相关设置。</p></div>
        </div>
      </main>
      <section v-if="profileEditorOpen" class="profile-editor" role="dialog" aria-modal="true" :aria-label="t('编辑个人资料')">
        <aside class="profile-editor-tools">
          <header><button class="profile-back" @click="cancelProfileEditing">‹</button><b>{{ t("编辑个人资料") }}</b><button class="profile-editor-close" :aria-label="t('关闭')" @click="cancelProfileEditing">×</button></header>
          <div class="profile-edit-scroll">
            <h3>头像</h3>
            <div class="profile-avatar-control">
              <span class="settings-avatar large-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{demoNickname.slice(0,1)}}</template><i></i></span>
              <div class="profile-avatar-buttons"><button class="profile-avatar-upload-btn" @click="setDemoAvatar">{{ t("上传新头像") }}</button><button class="profile-avatar-upload-btn reset" @click="resetAvatar">{{ t("恢复默认") }}</button></div>
            </div>
            <section class="profile-name-field">
              <div class="profile-field-heading"><label for="profile-display-name">{{ t("显示名称") }}</label><span>{{demoNicknameDraft.length}} / 30</span></div>
              <input id="profile-display-name" v-model="demoNicknameDraft" maxlength="30" autocomplete="nickname" :placeholder="t('输入昵称')" :disabled="nicknameSaving" @keydown.enter.prevent="saveNickname(true)">
              <p>{{ t("这是其他成员在聊天和个人资料中看到的名称。") }}</p>
            </section>
          </div>
        </aside>
        <div class="profile-editor-preview">
          <div class="profile-preview-card">
            <div class="profile-preview-banner" :style="profileBackgroundImage ? {backgroundImage:`url(${profileBackgroundImage})`} : {background:profileBackground}">
              <div class="profile-background-control"><button :aria-label="t('编辑背景')" @click="notDeveloped(t('个人资料背景'))">✎</button><div v-if="profileBackgroundMenu" class="profile-background-menu"><button @click="profileBackgroundImage='';profileBackground='#454347';profileBackgroundMenu=false">纯色</button><button @click="$refs.profileBannerUpload.click();profileBackgroundMenu=false">{{ t("上传背景图片") }}</button></div></div>
              <input ref="profileBannerUpload" class="profile-file-input" type="file" accept="image/*" @change="handleProfileBackground">
            </div>
            <div class="profile-preview-body"><div class="profile-preview-avatar settings-avatar"><img v-if="myAvatarSrc" :src="myAvatarSrc" :alt="demoNicknameDraft || demoNickname" style="width:100%;height:100%;border-radius:inherit;object-fit:cover"><template v-else>{{(demoNicknameDraft || demoNickname).slice(0,1)}}</template><i></i></div><div class="profile-status-bubble">UID {{demoUid||'—'}}</div><h1>{{demoNicknameDraft || demoNickname}}</h1><p>UID {{demoUid||'—'}} <span>•</span> <i>{{ t("添加个性签名") }}</i> <button @click="notDeveloped(t('服务器标签'))">{{ t("服务器标签⌄") }}</button></p><div class="profile-preview-actions"><button @click="notDeveloped('个人资料消息')">▰　消息</button><button @click="notDeveloped('个人资料操作')">▣</button><button @click="notDeveloped('个人资料更多操作')">•••</button></div><section><label>{{ t("自我介绍") }}</label><p>{{ t("写一段简短的介绍") }}</p></section><section><label>{{ t("成员加入时间") }}</label><p>{{ t("暂未提供") }}</p></section><section><label>{{ t("连接") }}</label><p>{{ t("＋ 添加关联") }}</p></section><section><label>{{ t("备注（仅对您可见）") }}</label><p>{{ t("点击添加备注") }}</p></section></div>
          </div>
        </div>
        <footer class="profile-editor-savebar"><span>{{ nicknameSaving ? t("正在保存昵称…") : (demoNicknameDraft.trim()!==demoNickname ? t("更改尚未保存") : '') }}</span><button :disabled="nicknameSaving" @click="cancelProfileEditing">{{ t("取消") }}</button><button class="save-profile" :disabled="nicknameSaving || !demoNicknameDraft.trim() || demoNicknameDraft.trim()===demoNickname" @click="saveNickname(true)">{{ nicknameSaving ? t("保存中…") : t("保存更改") }}</button></footer>
      </section>
    </section>
  </div>

  <!-- 全局传输进度弹窗（静默图片预览不会出现在这里） -->
  <div v-if="transferTasks.length" class="transfer-progress-panel">
    <article v-for="task in transferTasks" :key="task.localId || task.transferId || task.fileId" class="transfer-progress-card" :class="{upload:task.direction==='upload',done:task.status==='已完成',failed:task.status==='失败'||task.status==='已取消'}">
      <div class="transfer-progress-head">
        <span class="transfer-progress-icon">{{ task.status==='已完成' ? '✓' : task.status==='失败'||task.status==='已取消' ? '✕' : task.direction==='upload' ? '↑' : '↓' }}</span>
        <div class="transfer-progress-copy">
          <b :title="task.name">{{task.name || (task.direction==='upload'?'upload':'download')}}</b>
          <small v-if="task.status==='已完成'">{{t(task.direction==='upload'?'上传完成':'下载完成')}}</small>
          <small v-else-if="task.status==='失败'">{{t(task.direction==='upload'?'上传失败':'下载失败')}}</small>
          <small v-else-if="task.status==='已取消'">{{t('已取消')}}</small>
          <small v-else>{{t(task.status)}} · {{formatFileSize(task.transferred||0)}} / {{formatFileSize(task.totalSize||0)}} · {{Math.round(task.progress||0)}}%</small>
        </div>
        <button class="transfer-progress-close" :aria-label="t('关闭')" @click="closeTransferProgress(task)">×</button>
      </div>
      <div class="transfer-progress-bar"><i :style="{width:`${task.status==='已完成'?100:(task.progress||0)}%`}"></i></div>
    </article>
  </div>

  <!-- 图片查看器（lightbox）：点击消息图片打开，滚轮缩放、拖动平移、左右切换、X/Esc 关闭 -->
  <div v-if="lightbox" class="lightbox-backdrop" @click.self="closeLightbox" @wheel.prevent="lightboxWheel">
    <button class="lightbox-close" :aria-label="t('关闭')" :title="t('关闭')" @click="closeLightbox">×</button>
    <button v-if="lightbox.images.length > 1" class="lightbox-nav lightbox-prev" :aria-label="t('上一张')" :title="t('上一张')" @click.stop="lightboxPrev">‹</button>
    <button v-if="lightbox.images.length > 1" class="lightbox-nav lightbox-next" :aria-label="t('下一张')" :title="t('下一张')" @click.stop="lightboxNext">›</button>
    <div class="lightbox-stage" :class="{ dragging: lightbox.dragging }" @mousedown="lightboxDragStart">
      <img v-if="lightbox.images[lightbox.index]" :src="lightbox.images[lightbox.index].url" :alt="lightbox.images[lightbox.index].name" class="lightbox-image" :style="{ transform: `translate(${lightbox.dx}px, ${lightbox.dy}px) scale(${lightbox.scale})` }" draggable="false" />
    </div>
    <div class="lightbox-meta">
      <span v-if="lightbox.images.length > 1" class="lightbox-count">{{ lightbox.index + 1 }} / {{ lightbox.images.length }}</span>
      <span v-if="lightbox.images[lightbox.index]?.name" class="lightbox-name">{{ lightbox.images[lightbox.index].name }}</span>
      <span class="lightbox-hint">{{ t('滚轮缩放 · 拖动平移 · ←→ 切换 · Esc 关闭') }}</span>
    </div>
  </div>
</template>

<style>
.emoji-panel-scroll{scrollbar-width:thin;scrollbar-color:#606168 #1b1c20}
.emoji-panel-scroll::-webkit-scrollbar{width:12px}
.emoji-panel-scroll::-webkit-scrollbar-track{background:#1b1c20}
.emoji-panel-scroll::-webkit-scrollbar-thumb{background:#606168;border:2px solid #1b1c20;border-radius:8px;min-height:56px}
.emoji-panel-scroll::-webkit-scrollbar-thumb:hover{background:#73757e}
.login-restoring-indicator{height:112px;display:flex;align-items:center;justify-content:center;gap:10px;color:#b9bbc3;font-size:13px}
.login-restoring-indicator i{width:18px;height:18px;border:2px solid #555965;border-top-color:#7d88ff;border-radius:50%;animation:login-restoring-spin .8s linear infinite}
@keyframes login-restoring-spin{to{transform:rotate(360deg)}}
.profile-avatar-buttons{margin-left:auto;margin-right:12px;display:flex;gap:6px}
.profile-avatar-upload-btn{width:auto;height:30px;padding:0 11px;border-radius:5px;background:#5865f2;color:#fff;font-size:12px;font-weight:600;border:0;white-space:nowrap;cursor:pointer}
.profile-avatar-upload-btn:hover{background:#6975ff}
.profile-avatar-upload-btn:active{background:#4752c4}
.profile-avatar-upload-btn:disabled,.profile-editor-savebar button:disabled{cursor:not-allowed;opacity:.55}
.profile-avatar-upload-btn.reset{background:#4e5058}
.profile-avatar-upload-btn.reset:hover{background:#5d6069}
.profile-name-field{margin-top:18px;padding-top:18px;border-top:1px solid #35363d}
.profile-field-heading{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.profile-field-heading label{color:#f0f1f4;font-size:13px;font-weight:600}
.profile-field-heading span{color:#858791;font-size:11px}
.profile-name-field input{width:100%;height:40px;padding:0 11px;border:1px solid #41434b;border-radius:6px;outline:0;background:#17181d;color:#f0f1f4;font-size:14px}
.profile-name-field input:focus{border-color:#6d78f6;box-shadow:0 0 0 1px #6d78f6}
.profile-name-field input:disabled{cursor:not-allowed;opacity:.7}
.profile-name-field p{margin:7px 0 0;color:#8f919a;font-size:11px;line-height:1.45}
.reply-composer-preview{position:relative;min-width:0;min-height:54px;display:flex;align-items:center;gap:10px;flex:none;margin:0 16px;padding:7px 10px 7px 14px;border:1px solid #3a3c44;border-bottom:0;border-radius:8px 8px 0 0;background:#292a30;color:#dfe1e6}
.reply-composer-preview+.composer{margin-top:0;border-radius:0 0 8px 8px}
.reply-composer-accent{position:absolute;left:0;top:0;bottom:0;width:3px;background:#5865f2}
.reply-composer-avatar{width:30px;height:30px;flex:none;display:grid;place-items:center;overflow:hidden;border-radius:50%;background:#454750;color:#fff;font-size:12px;font-weight:600}
.reply-composer-avatar img{width:100%;height:100%;object-fit:cover}
.reply-composer-copy{min-width:0;flex:1;display:grid;grid-template-columns:auto minmax(0,1fr);align-items:center;column-gap:5px;line-height:1.25}
.reply-composer-copy small{color:#92949e;font-size:10px;grid-column:1/-1}
.reply-composer-copy b{font-size:12px;font-weight:600;white-space:nowrap}
.reply-composer-copy>span{overflow:hidden;color:#b5b7bf;font-size:12px;text-overflow:ellipsis;white-space:nowrap}
.reply-composer-preview>button{width:26px;height:26px;flex:none;display:grid;place-items:center;padding:0;border-radius:5px;background:transparent;color:#aeb0b8;font-size:20px;line-height:1}
.reply-composer-preview>button:hover{background:#3b3d45;color:#fff}
.message-reply .reply-label{flex:none;color:#8b8d96;font-size:12px;font-weight:600}
@media(max-width:720px){.reply-composer-preview{margin-right:8px;margin-left:8px}.reply-composer-copy>span{display:none}}
.community-sidebar-content{position:relative}
.community-sidebar-banner{position:absolute;top:0;left:0;right:0;height:135px;z-index:0;background-image:linear-gradient(180deg, rgba(14,15,19,.2) 0%, #121214 100%), url('/default_background.png');background-size:cover;background-position:center 28%;pointer-events:none}
.community-sidebar-content .guild-header{position:relative;z-index:1;background:transparent;box-shadow:none;border-bottom:1px solid #00000040}
.community-sidebar-content .guild-header-title:hover{background:transparent}
.community-sidebar-content .side-shortcuts{position:relative;z-index:1;background:transparent;margin-top:87px}
.message-image-loading{width:min(420px,100%);height:220px;display:grid;place-items:center;align-content:center;gap:8px;border:1px solid #34363e;border-radius:8px;background:#18191e;color:#777a85}
.message-image-loading span{font-size:30px;line-height:1}
.message-image-loading small{font-size:11px}
.message-image-loading.failed{color:#c7787d}
.message-image-loading button{height:28px;padding:0 10px;border-radius:5px;background:#303137;color:#dfe0e5;font-size:11px}
.message-image-loading button:hover{background:#3b3d45;color:#fff}
.message-videos{display:flex;flex-direction:column;align-items:flex-start;gap:8px;margin-top:8px}
.message-video-cover{width:min(440px,100%);height:126px;display:grid;grid-template-columns:176px minmax(0,1fr) 34px;align-items:center;gap:12px;padding:10px;border:1px solid #34363e;border-radius:8px;background:#202126}
.video-cover-art{position:relative;width:176px;height:104px;display:grid;place-items:center;overflow:hidden;border-radius:6px;background:linear-gradient(145deg,#191b21,#313541);color:#fff}
.video-cover-art::after{content:"";position:absolute;inset:0;background:linear-gradient(180deg,transparent,#090a0d99)}
.video-cover-play{position:relative;z-index:1;width:40px;height:40px;display:grid;place-items:center;padding-left:3px;border-radius:50%;background:#111216cc;color:#fff;font-size:17px}
.video-cover-art>b{position:absolute;z-index:1;right:7px;bottom:6px;padding:2px 5px;border-radius:3px;background:#111216cc;color:#cfd1d8;font-size:9px}
.video-cover-info{min-width:0;display:grid;gap:5px}
.video-cover-info>b{overflow:hidden;color:#e6e7eb;font-size:13px;text-overflow:ellipsis;white-space:nowrap}
.video-cover-info>small{color:#92949e;font-size:11px}
.video-cover-download{width:30px;height:30px;display:grid;place-items:center;padding:0;border-radius:5px;background:transparent;color:#b5b7bf}
.video-cover-download:hover{background:#303137;color:#fff}
.video-cover-download svg{width:19px;height:19px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
@media(max-width:560px){.message-video-cover{height:auto;grid-template-columns:112px minmax(0,1fr) 30px}.video-cover-art{width:112px;height:72px}}
/* —— 全局上传 / 下载进度弹窗（右下角）—— */
.transfer-progress-panel{position:fixed;right:20px;bottom:96px;z-index:4200;display:flex;flex-direction:column;justify-content:flex-end;gap:10px;width:min(340px,calc(100vw - 24px));max-height:calc(100vh - 120px);overflow:auto;pointer-events:none}
.transfer-progress-card{width:100%;min-width:280px;padding:12px 14px;border:1px solid #383a43;border-radius:8px;background:#1e1f24;box-shadow:0 10px 32px #000b;color:#dcdee3;pointer-events:auto}
.transfer-progress-card.done{border-color:#3a5c4b}
.transfer-progress-card.failed{border-color:#5c3535}
.transfer-progress-head{display:flex;align-items:center;gap:10px}
.transfer-progress-icon{width:34px;height:34px;flex:none;display:grid;place-items:center;border-radius:7px;background:#263b48;color:#66b7dd;font-size:18px;font-weight:700}
.transfer-progress-card.upload .transfer-progress-icon{background:#33354d;color:#9ba4ff}
.transfer-progress-card.done .transfer-progress-icon{background:#26382f;color:#43b581}
.transfer-progress-card.failed .transfer-progress-icon{background:#462b2b;color:#f08686}
.transfer-progress-copy{min-width:0;flex:1}
.transfer-progress-copy b{display:block;overflow:hidden;color:#e8e9ed;font-size:12.5px;font-weight:600;text-overflow:ellipsis;white-space:nowrap}
.transfer-progress-copy small{display:block;margin-top:3px;overflow:hidden;color:#92949e;font-size:11px;text-overflow:ellipsis;white-space:nowrap}
.transfer-progress-close{width:24px;height:24px;flex:none;border-radius:5px;background:transparent;color:#a5a7ae;font-size:18px;line-height:24px}
.transfer-progress-close:hover{background:#35363e;color:#fff}
.transfer-progress-bar{height:4px;margin-top:10px;overflow:hidden;border-radius:3px;background:#383a43}
.transfer-progress-bar i{height:100%;display:block;border-radius:3px;background:#45a8d5;transition:width .2s ease}
.transfer-progress-card.upload .transfer-progress-bar i{background:#6875f5}
.transfer-progress-card.done .transfer-progress-bar i{background:#43b581}
.transfer-progress-card.failed .transfer-progress-bar i{background:#ed4245}
@media(max-width:520px){.transfer-progress-panel{right:12px;bottom:82px}.transfer-progress-card{min-width:0}}
.message-images{display:flex;flex-wrap:wrap;gap:8px;margin-top:8px}
.message-image-open{display:block;padding:0;border:0;background:transparent;cursor:zoom-in;overflow:hidden;border-radius:8px;max-width:100%}
.message-image-open img{display:block;max-width:min(420px,100%);max-height:320px;object-fit:cover;border-radius:8px}
.message-image-open:hover img{filter:brightness(.9)}
.composer .composer-tools button.send-button{width:auto;min-width:60px;height:32px;flex:none;display:inline-flex;align-items:center;justify-content:center;gap:4px;padding:0 10px;border-radius:4px;background:#5865f2;color:#fff;font-size:13px;font-weight:600;line-height:1;transition:background .15s ease,transform .05s ease}
.composer .composer-tools button.send-button svg{width:16px;height:16px;fill:currentColor;flex:none}
.composer .composer-tools button.send-button:hover{background:#4752c4;color:#fff}
.composer .composer-tools button.send-button:active{background:#3c45a5;transform:translateY(1px)}
.lightbox-backdrop{position:fixed;inset:0;z-index:6000;background:rgba(0,0,0,.92);display:flex;align-items:center;justify-content:center;user-select:none}
.lightbox-stage{position:relative;max-width:calc(100vw - 120px);max-height:calc(100vh - 120px);display:flex;align-items:center;justify-content:center;overflow:hidden}
.lightbox-image{display:block;max-width:calc(100vw - 120px);max-height:calc(100vh - 120px);object-fit:contain;cursor:grab}
.lightbox-stage.dragging .lightbox-image{cursor:grabbing}
.lightbox-close{position:absolute;right:20px;top:20px;width:44px;height:44px;border-radius:50%;background:#ffffff14;color:#fff;font-size:28px;line-height:1;z-index:2}
.lightbox-close:hover{background:#ffffff2b}
.lightbox-nav{position:absolute;top:50%;transform:translateY(-50%);width:48px;height:48px;border-radius:50%;background:#ffffff14;color:#fff;font-size:30px;line-height:1;z-index:2}
.lightbox-nav:hover{background:#ffffff2b}
.lightbox-prev{left:20px}
.lightbox-next{right:20px}
.lightbox-meta{position:absolute;left:0;right:0;bottom:20px;display:flex;flex-direction:column;align-items:center;gap:4px;color:#cfd1d6;font-size:12px;pointer-events:none}
.lightbox-count{color:#8b8d96;font-size:11px}
.lightbox-name{max-width:70vw;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#e8e9ed;font-size:13px}
.lightbox-hint{color:#8b8d96;font-size:11px}
</style>
