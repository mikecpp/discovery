import { createRouter, createWebHistory } from 'vue-router'
import DevicesView from './views/DevicesView.vue'
import NetworkView from './views/NetworkView.vue'
import SystemView from './views/SystemView.vue'
import RangeView from './views/RangeView.vue'
import NotFoundView from './views/NotFoundView.vue'

export default createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/devices' },
    { path: '/devices', component: DevicesView, meta: { title: 'Devices' } },
    { path: '/range', component: RangeView, meta: { title: 'Range Scan' } },
    { path: '/network', component: NetworkView, meta: { title: 'Network' } },
    { path: '/system', component: SystemView, meta: { title: 'System' } },
    { path: '/scan', redirect: '/network' },
    { path: '/:path(.*)*', component: NotFoundView, meta: { title: 'Page Not Found' } },
  ],
})
