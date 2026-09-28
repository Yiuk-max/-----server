import { h } from 'vue'

// —— 图标库：把重复出现的描边 SVG 集中到 ICONS，模板里用 <Icon name="..."/> 引用 ——
export const ICONS = {
  edit: '<path d="m4 16.5-.8 4.3 4.3-.8L19.8 7.7l-3.5-3.5L4 16.5Z"/><path d="m14.8 5.7 3.5 3.5"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="m19 13 2 1-2 4-2-1-2 1-.5 2h-4L10 18l-2-1-2 1-2-4 2-1v-2l-2-1 2-4 2 1 2-1 .5-2h4L15 6l2 1 2-1 2 4-2 1z"/>',
  logout: '<path d="M10 17l5-5-5-5m5 5H3m9-9h7a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-7"/>',
  caret: '<path d="m6 9.5 6 6 6-6"/>',
  hash: '<path d="M10 3.5 8 20.5M16 3.5l-2 17M4 9h16M3 15h16"/>',
  userPlus: '<path d="M15 20v-1a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v1m7-9a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm10-2v8m-4-4h8"/>',
  group: '<circle cx="9.2" cy="8.6" r="3.2"/><circle cx="16.8" cy="10.2" r="2.6"/><path d="M3.6 19.4a5.6 5.6 0 0 1 11.2 0"/><path d="M14.8 19.4a4.8 4.8 0 0 1 6.6-3.6"/>',
  smile: '<circle cx="12" cy="12" r="9"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><path d="M9 9h.01M15 9h.01"/>',
  gift: '<path d="M4 11h16v9a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1z"/><path d="M3 7.5h18V11H3zM12 7.5V21"/><path d="M12 7.5S11 3 8.6 3a2.3 2.3 0 0 0 0 4.5zM12 7.5S13 3 15.4 3a2.3 2.3 0 0 1 0 4.5z"/>',
  sticker: '<path d="M21 12a9 9 0 1 0-9 9c4.5 0 9-4.5 9-9Z"/><path d="M21 12h-5a4 4 0 0 0-4 4v5"/>',
  upload: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M12 18v-7m-3 3 3-3 3 3"/>',
  search: '<circle cx="10.8" cy="10.8" r="6.5"/><path d="m16 16 4.5 4.5"/>',
  doc: '<path d="M5 4h11l3 3v13H5z"/><path d="M8 9h8M8 13h8M8 17h5"/>',
  list: '<path d="M4 5h16M4 12h16M4 19h16"/>'
}

export const Icon = (props) => h('svg', {
  viewBox: '0 0 24 24',
  class: props.class || null,
  'aria-hidden': 'true',
  innerHTML: ICONS[props.name] || ''
})
