import { createRouter, createWebHashHistory } from 'vue-router';
import ChatView from './views/ChatView.vue';
import SettingsView from './views/SettingsView.vue';

// 用 hash 路由：后端静态服务没有 SPA fallback（/xxx 会 404），
// hash 不会发到服务端，刷新/深链都安全。
export default createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', name: 'chat', component: ChatView },
    { path: '/settings', name: 'settings', component: SettingsView },
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
});
