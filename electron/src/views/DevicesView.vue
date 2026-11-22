<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import DeviceList from '../components/DeviceList.vue'
import LayoutToggle from '../components/LayoutToggle.vue'
import { formatAgo, getJson, usePolling } from '../api.js'

const devices = ref([])
const status = ref(null)
const now = ref(Date.now())
const error = ref('')
const filter = ref('all')
const loaded = ref(false)

const polling = usePolling(async () => {
  try {
    const [list, state] = await Promise.all([getJson('/api/devices'), getJson('/api/status')])
    devices.value = list.devices
    status.value = state
    now.value = list.now
    error.value = ''
  } catch (failure) {
    error.value = failure.message
  } finally {
    loaded.value = true
  }
}, 2000)

onMounted(() => polling.start())
onUnmounted(() => polling.stop())

const online = computed(() => devices.value.filter((device) => device.online).length)
const offline = computed(() => devices.value.length - online.value)
const visible = computed(() => {
  if (filter.value === 'on') return devices.value.filter((device) => device.online)
  if (filter.value === 'off') return devices.value.filter((device) => !device.online)
  return devices.value
})
const scanning = computed(() => status.value?.phase === 'scanning')
const progress = computed(() => {
  if (!status.value || !status.value.hostCount) return 0
  return Math.min(100, Math.round((status.value.scanned / status.value.hostCount) * 100))
})
</script>

<template>
  <div v-if="error" class="notice error">{{ error }}</div>
  <div class="toolbar">
    <button class="chip" :class="{ active: filter === 'all' }" @click="filter = 'all'">
      All <span class="count">{{ devices.length }}</span>
    </button>
    <button class="chip" :class="{ active: filter === 'on' }" @click="filter = 'on'">
      <span class="dot on"></span> On <span class="count">{{ online }}</span>
    </button>
    <button class="chip" :class="{ active: filter === 'off' }" @click="filter = 'off'">
      <span class="dot off"></span> Off <span class="count">{{ offline }}</span>
    </button>
    <span class="spacer"></span>
    <span v-if="status" class="muted">
      {{ status.subnet }} on {{ status.interface }}
      <template v-if="scanning"> · scanning {{ status.scanned }}/{{ status.hostCount }}</template>
      <template v-else-if="status.lastWatch"> · checked {{ formatAgo(status.lastWatch, now) }}</template>
    </span>
    <LayoutToggle />
  </div>
  <div v-if="scanning" class="progress"><span :style="{ width: progress + '%' }"></span></div>

  <div v-if="loaded && visible.length === 0" class="card muted">
    <template v-if="scanning">Scanning the subnet, devices will appear as they answer...</template>
    <template v-else>No devices to show.</template>
  </div>
  <div v-else-if="!loaded" class="card muted">Loading...</div>

  <DeviceList :devices="visible" :now="now" />
</template>
