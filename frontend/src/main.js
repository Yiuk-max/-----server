import { createApp } from 'vue';
import App from './App.vue';
import router from './router.js';
import { loadTheme, applyTheme } from './theme.js';
import './style.css';

// 首帧前就应用主题，避免闪烁
applyTheme(loadTheme());

createApp(App).use(router).mount('#app');
