<script setup>
import { computed, onMounted, ref, watchEffect } from 'vue'
import { useRoute } from 'vue-router'
import { getJson } from './api.js'

const route = useRoute()
const title = ref('Device Discovery')
const heading = computed(() => route.meta.title ?? '')

const links = [
  { to: '/devices', label: 'Devices', icon: '▦' },
  { to: '/range', label: 'Range', icon: '⇔' },
  { to: '/network', label: 'Network', icon: '⊶' },
  { to: '/system', label: 'System', icon: '◷' },
]

onMounted(async () => {
  try {
    const info = await getJson('/api/info')
    title.value = info.title
  } catch {
  }
})

watchEffect(() => {
  document.title = heading.value ? `${heading.value} - ${title.value}` : title.value
})
</script>

<template>
  <nav>
    <div class="brand">{{ title }}</div>
    <RouterLink v-for="link in links" :key="link.to" :to="link.to" active-class="active">
      <span class="icon">{{ link.icon }}</span>{{ link.label }}
    </RouterLink>
  </nav>
  <main>
    <h1>{{ heading }}</h1>
    <RouterView />
  </main>
</template>
