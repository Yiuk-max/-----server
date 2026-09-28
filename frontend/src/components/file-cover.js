import { h } from 'vue'

// 文件封面：按扩展名归到常见类型，各自一套配色 + 标签（PDF / DOC / ZIP / AUDIO…）；发出去的文件卡片与输入框附件预览共用
export const FILE_KINDS = [
  { kind: 'pdf', label: 'PDF', exts: ['pdf'] },
  { kind: 'doc', label: 'DOC', exts: ['doc', 'docx', 'odt', 'rtf', 'pages', 'wps'] },
  { kind: 'xls', label: 'XLS', exts: ['xls', 'xlsx', 'csv', 'ods', 'numbers'] },
  { kind: 'ppt', label: 'PPT', exts: ['ppt', 'pptx', 'odp', 'key'] },
  { kind: 'zip', label: 'ZIP', exts: ['zip', 'rar', '7z', 'tar', 'gz', 'bz2', 'xz', 'iso'] },
  { kind: 'audio', label: 'AUDIO', exts: ['mp3', 'wav', 'flac', 'ogg', 'm4a', 'aac', 'wma', 'opus'] },
  { kind: 'video', label: 'VIDEO', exts: ['mp4', 'mkv', 'mov', 'avi', 'webm', 'flv', 'wmv', 'm4v'] },
  { kind: 'image', label: 'IMG', exts: ['psd', 'ai', 'tif', 'tiff', 'heic', 'raw', 'bmp'] },
  { kind: 'text', label: 'TXT', exts: ['txt', 'md', 'log', 'ini', 'cfg', 'conf'] },
  { kind: 'code', label: 'CODE', exts: ['js', 'ts', 'jsx', 'tsx', 'vue', 'py', 'java', 'c', 'cpp', 'h', 'hpp', 'cs', 'go', 'rs', 'rb', 'php', 'swift', 'kt', 'html', 'css', 'scss', 'json', 'xml', 'yml', 'yaml', 'sh', 'bat', 'ps1', 'sql', 'proto'] },
  { kind: 'exe', label: 'EXE', exts: ['exe', 'msi', 'apk', 'dmg', 'pkg', 'deb', 'rpm', 'appx'] },
  { kind: 'font', label: 'FONT', exts: ['ttf', 'otf', 'woff', 'woff2'] }
]
export const fileKindOf = (name = '') => {
  const ext = name.includes('.') ? name.split('.').pop().toLowerCase() : ''
  const hit = FILE_KINDS.find((item) => item.exts.includes(ext))
  return hit ? { kind: hit.kind, label: hit.label } : { kind: 'file', label: ext ? ext.toUpperCase().slice(0, 4) : 'FILE' }
}
// 封面中间的小图形（24 视口，落在页面形状 6..18 × 9..19 的范围里）
export const FILE_COVER_GLYPHS = {
  pdf: 'M8.5 11.5h7M8.5 14.5h7M8.5 17.5h4',
  doc: 'M8.5 11.5h7M8.5 14.5h7M8.5 17.5h4',
  text: 'M8.5 11.5h7M8.5 14.5h7M8.5 17.5h4',
  xls: 'M7.5 10.5h9v8h-9zM7.5 14.5h9M11.5 10.5v8',
  ppt: 'M8 18.5v-3M12 18.5v-7M16 18.5v-5',
  zip: 'M11 10h2M11 12.5h2M11 15h2M10.5 17h3',
  audio: 'M10 18.2a1.6 1.6 0 1 1-3.2 0 1.6 1.6 0 0 1 3.2 0zM10 18.2V10l6.5-1.6v8.2M16.5 16.6a1.6 1.6 0 1 1-3.2 0 1.6 1.6 0 0 1 3.2 0z',
  video: 'M9.5 10.5l6.5 3.75-6.5 3.75z',
  image: 'M7.5 18.5l3.2-4 2.3 2.6 1.9-2.4 2.6 3.8zM9.4 11.6a1 1 0 1 1 0 .01',
  code: 'M9.6 11.5 7 14l2.6 2.5M14.4 11.5 17 14l-2.6 2.5',
  exe: 'M12 12.2a1.9 1.9 0 1 1 0 3.8 1.9 1.9 0 0 1 0-3.8zM12 9.8v1.2M12 17.2v1.2M7.8 14.1H9M15 14.1h1.2',
  font: 'M8.3 18.5 12 9.5l3.7 9M9.6 15.5h4.8',
  file: ''
}
export const FileCover = (props) => {
  const info = fileKindOf(props.name)
  return h('span', { class: ['file-cover', `file-cover-${info.kind}`], 'aria-hidden': 'true' }, [
    h('svg', { viewBox: '0 0 24 24' }, [
      h('path', { class: 'cover-page', d: 'M6 2h8l6 6v14H6z' }),
      h('path', { class: 'cover-fold', d: 'M14 2v6h6' }),
      FILE_COVER_GLYPHS[info.kind] ? h('path', { class: 'cover-glyph', d: FILE_COVER_GLYPHS[info.kind] }) : null
    ]),
    h('i', {}, info.label)
  ])
}
