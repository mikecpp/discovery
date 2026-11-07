import { createRouter, createWebHistory } from 'vue-router'
import DevicesView from './views/DevicesView.vue'
import ScanView from './views/ScanView.vue'
import NotFoundView from './views/NotFoundView.vue'

export default createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/devices' },
    { path: '/devices', component: DevicesView, meta: { title: 'Devices' } },
    { path: '/scan', component: ScanView, meta: { title: 'Scan' } },
    { path: '/:path(.*)*', component: NotFoundView, meta: { title: 'Page Not Found' } },
  ],
})
