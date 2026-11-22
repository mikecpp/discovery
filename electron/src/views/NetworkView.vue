<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { formatAgo, formatBytes, formatDuration, formatTime, getJson, postJson, usePolling } from '../api.js'

const status = ref(null)
const interfaces = ref([])
const rates = ref({})
const showDown = ref(false)
const error = ref('')
const notice = ref('')
const busy = ref(false)
let previous = null

const polling = usePolling(async () => {
  try {
    const [state, network] = await Promise.all([getJson('/api/status'), getJson('/api/network')])
    status.value = state
    if (previous) {
      const seconds = (network.now - previous.now) / 1000
      const next = {}
      for (const item of network.interfaces) {
        const before = previous.byName[item.name]
        if (before && seconds > 0 && item.rxBytes >= before.rxBytes && item.txBytes >= before.txBytes) {
          next[item.name] = { rx: (item.rxBytes - before.rxBytes) / seconds, tx: (item.txBytes - before.txBytes) / seconds }
        }
      }
      rates.value = next
    }
    previous = { now: network.now, byName: Object.fromEntries(network.interfaces.map((item) => [item.name, item])) }
    interfaces.value = network.interfaces
    error.value = ''
  } catch (failure) {
    error.value = failure.message
  }
}, 2000)

onMounted(() => polling.start())
onUnmounted(() => polling.stop())

const progress = computed(() => {
  if (!status.value || !status.value.hostCount) return 0
  return Math.min(100, Math.round((status.value.scanned / status.value.hostCount) * 100))
})
const active = (item) => item.up && item.running && item.addresses.length > 0
const visible = computed(() =>
  [...interfaces.value]
    .filter((item) => showDown.value || active(item))
    .sort((a, b) => (b.name === status.value?.interface) - (a.name === status.value?.interface) || a.loopback - b.loopback),
)
const hiddenCount = computed(() => interfaces.value.filter((item) => !active(item)).length)
const formatRate = (value) => (value === undefined ? '-' : `${formatBytes(value)}/s`)

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
        <h2 style="margin: 0">Monitored subnet</h2>
        <span class="badge" :class="status.phase">{{ status.phase }}</span>
        <span class="spacer"></span>
        <button :disabled="busy || status.phase === 'scanning'" @click="scanNow">Scan now</button>
      </div>
      <div class="progress"><span :style="{ width: (status.phase === 'scanning' ? progress : 100) + '%' }"></span></div>
      <table class="details">
        <tr><th>Interface</th><td>{{ status.interface }}</td></tr>
        <tr><th>Address</th><td>{{ status.address }} / {{ status.netmask }}</td></tr>
        <tr><th>Subnet</th><td>{{ status.subnet }} ({{ status.hostCount }} hosts)</td></tr>
        <tr><th>Gateway</th><td>{{ status.gateway || '-' }}</td></tr>
        <tr><th>MAC</th><td>{{ status.mac || '-' }}</td></tr>
        <tr><th>Devices found</th><td>{{ status.devices }} ({{ status.online }} on, {{ status.offline }} off)</td></tr>
        <tr><th>Current pass</th><td>{{ status.scanned }} / {{ status.hostCount }} scanned</td></tr>
        <tr><th>Last full scan</th><td>{{ formatTime(status.lastScanFinished) }} <span v-if="status.lastScanFinished" class="muted">({{ formatDuration(status.lastScanDuration) }}, {{ status.scanCount }} completed)</span></td></tr>
        <tr><th>Last status check</th><td>{{ formatAgo(status.lastWatch, status.now) }}</td></tr>
        <tr><th>Next full scan</th><td>{{ status.phase === 'scanning' ? 'running' : `in ${status.nextRescan} s` }}</td></tr>
        <tr><th>ICMP mode</th><td>{{ status.icmpMode }}</td></tr>
        <tr><th>Scan settings</th><td>{{ status.threads }} threads · watch every {{ status.watchInterval }} s · rescan every {{ status.rescanInterval }} s</td></tr>
        <tr v-if="status.error"><th>Error</th><td class="notice error" style="display: block">{{ status.error }}</td></tr>
      </table>
    </div>

    <div class="card table-wrap">
      <div class="toolbar card-pad">
        <h2 style="margin: 0">Interfaces</h2>
        <span class="spacer"></span>
        <button v-if="hiddenCount" class="secondary" @click="showDown = !showDown">
          {{ showDown ? 'Hide inactive' : `Show inactive (${hiddenCount})` }}
        </button>
      </div>
      <table class="device-table">
        <thead>
          <tr><th></th><th>Name</th><th>Addresses</th><th>MAC</th><th>MTU</th><th>Receive</th><th>Send</th><th>Total in / out</th></tr>
        </thead>
        <tbody>
          <tr v-for="item in visible" :key="item.name" :class="{ off: !active(item) }">
            <td><span class="dot" :class="active(item) ? 'on' : 'off'" :title="active(item) ? 'Up' : 'Down'"></span></td>
            <td class="nowrap">
              <span class="ip">{{ item.name }}</span>
              <span v-if="item.name === status.interface" class="tag self">monitored</span>
              <span v-if="item.loopback" class="tag">loopback</span>
            </td>
            <td class="mono">
              <div v-for="address in item.addresses" :key="address.address">{{ address.address }}/{{ address.prefix }}</div>
              <span v-if="!item.addresses.length" class="muted">-</span>
            </td>
            <td class="mono">{{ item.mac || '-' }}</td>
            <td>{{ item.mtu || '-' }}</td>
            <td class="nowrap">{{ formatRate(rates[item.name]?.rx) }}</td>
            <td class="nowrap">{{ formatRate(rates[item.name]?.tx) }}</td>
            <td class="nowrap">{{ formatBytes(item.rxBytes) }} / {{ formatBytes(item.txBytes) }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </template>
  <div v-else class="card muted">Loading...</div>
</template>
