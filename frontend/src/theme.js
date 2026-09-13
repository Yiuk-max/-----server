// ============================================================
// 主题系统：只改配色，不改布局。
// 每套主题只允许「两色」或「三色」主色（kind: 'two' | 'three'），
// 中性色（背景/面板/边框/文字灰阶）随主题一起定义，保证深浅可读。
// 切换 = 给 <html> 设 data-theme，CSS 里按 [data-theme=...] 覆盖变量。
// ============================================================

export const themes = [
  {
    id: 'dark-amber',
    name: '暗夜琥珀',
    kind: 'two',
    desc: '琥珀 · 青绿',
    colors: ['#c98a3e', '#4fa88f'],
  },
  {
    id: 'aurora-night',
    name: '极光极夜',
    kind: 'three',
    desc: '极光绿 · 夜海蓝 · 冰雾白',
    colors: ['#37cca3', '#0d405b', '#d7e3e5'],
  },
  {
    id: 'xuanzhi',
    name: '宣纸白 × 鸢尾蓝',
    kind: 'two',
    desc: '宣纸白 · 鸢尾蓝',
    colors: ['#f9f2e0', '#1660ab'],
  },
  {
    id: 'choco',
    name: '巧克力棕 × 浅驼杏色',
    kind: 'two',
    desc: '巧克力棕 · 浅驼杏色',
    colors: ['#50230e', '#ecc395'],
  },
];

const KEY = 'chat.theme';

export function loadTheme() {
  const saved = localStorage.getItem(KEY);
  return themes.some((t) => t.id === saved) ? saved : themes[0].id;
}

export function applyTheme(id) {
  const exists = themes.some((t) => t.id === id);
  const next = exists ? id : themes[0].id;
  document.documentElement.setAttribute('data-theme', next);
  localStorage.setItem(KEY, next);
  return next;
}

export function cycleTheme(currentId) {
  const i = themes.findIndex((t) => t.id === currentId);
  const next = themes[(i + 1) % themes.length];
  applyTheme(next.id);
  return next;
}
