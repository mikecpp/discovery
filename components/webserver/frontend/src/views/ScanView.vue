<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { formatAgo, formatDuration, formatTime, getJson, postJson, usePolling } from '../api.js'

const status = ref(null)
const error = ref('')
const notice = ref('')
const busy = ref(false)

const polling = usePolling(async () => {
  try {
    status.value = await getJson('/api/status')
    error.value = ''
  } catch (failure) {
    error.value = failure.message
  }
}, 1000)

onMounted(() => polling.start())
onUnmounted(() => polling.stop())

const progress = computed(() => {
  if (!status.value || !status.value.hostCount) return 0
  return Math.min(100, Math.round((status.value.scanned / status.value.hostCount) * 100))
})

async function scanNow() {
  busy.value = true
  const result = await postJson('/api/scan')
  notice.value = result.message
  busy.value = false
  setTimeout(() => (notice.value = ''), 4000)
}
</script>

<template>
  <div v-if="error" class="notice error">{{ error }}</div>
  <div v-if="notice" class="notice">{{ notice }}</div>
  <template v-if="status">
    <div class="card">
      <div class="toolbar">
        <h2 style="margin: 0">Progress</h2>
        <span class="badge" :class="status.phase">{{ status.phase }}</span>
        <span class="spacer"></span>
        <button :disabled="busy || status.phase === 'scanning'" @click="scanNow">Scan now</button>
      </div>
      <div class="progress"><span :style="{ width: (status.phase === 'scanning' ? progress : 100) + '%' }"></span></div>
      <table class="details">
        <tr><th>Hosts in subnet</th><td>{{ status.hostCount }}</td></tr>
        <tr><th>Scanned in current pass</th><td>{{ status.scanned }}</td></tr>
        <tr><th>Devices found</th><td>{{ status.devices }} ({{ status.online }} on, {{ status.offline }} off)</td></tr>
        <tr><th>Full scans completed</th><td>{{ status.scanCount }}</td></tr>
        <tr><th>Last full scan</th><td>{{ formatTime(status.lastScanFinished) }} <span v-if="status.lastScanFinished" class="muted">({{ formatDuration(status.lastScanDuration) }})</span></td></tr>
        <tr><th>Last status check</th><td>{{ formatAgo(status.lastWatch, status.now) }}</td></tr>
        <tr><th>Next full scan</th><td>{{ status.phase === 'scanning' ? 'running' : `in ${status.nextRescan} s` }}</td></tr>
      </table>
    </div>
    <div class="card">
      <h2>Network</h2>
      <table class="details">
        <tr><th>Interface</th><td>{{ status.interface }}</td></tr>
        <tr><th>Address</th><td>{{ status.address }} / {{ status.netmask }}</td></tr>
        <tr><th>Subnet</th><td>{{ status.subnet }}</td></tr>
        <tr><th>Gateway</th><td>{{ status.gateway || '-' }}</td></tr>
        <tr><th>MAC</th><td>{{ status.mac || '-' }}</td></tr>
      </table>
    </div>
    <div class="card">
      <h2>Settings</h2>
      <table class="details">
        <tr><th>ICMP mode</th><td>{{ status.icmpMode }}</td></tr>
        <tr><th>Scan threads</th><td>{{ status.threads }}</td></tr>
        <tr><th>Status watch interval</th><td>{{ status.watchInterval }} s</td></tr>
        <tr><th>Full rescan interval</th><td>{{ status.rescanInterval }} s</td></tr>
        <tr v-if="status.error"><th>Error</th><td class="notice error" style="display: block">{{ status.error }}</td></tr>
      </table>
    </div>
  </template>
  <div v-else class="card muted">Loading...</div>
</template>
