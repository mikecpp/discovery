<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import DeviceCard from '../components/DeviceCard.vue'
import { formatDuration, formatTime, getJson, postJson, usePolling } from '../api.js'

const IPV4 = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/

const range = ref(null)
const start = ref('')
const end = ref('')
const error = ref('')
const notice = ref('')
const noticeError = ref(false)
const busy = ref(false)

const polling = usePolling(async () => {
  try {
    range.value = await getJson('/api/range')
    if (!start.value && !end.value && range.value.start) {
      start.value = range.value.start
      end.value = range.value.end
    }
    error.value = ''
  } catch (failure) {
    error.value = failure.message
  }
}, 1000)

onMounted(() => polling.start())
onUnmounted(() => polling.stop())

function toNumber(ip) {
  return ip.split('.').reduce((value, part) => value * 256 + Number(part), 0)
}

const scanning = computed(() => range.value?.phase === 'scanning')
const inputError = computed(() => {
  if (!start.value || !end.value) return ''
  if (!IPV4.test(start.value)) return 'Start address is not a valid IPv4 address.'
  if (!IPV4.test(end.value)) return 'End address is not a valid IPv4 address.'
  const count = toNumber(end.value) - toNumber(start.value) + 1
  if (count < 1) return 'The start address must not be after the end address.'
  if (range.value && count > range.value.maxHosts) {
    return `The range has ${count} addresses; the limit is ${range.value.maxHosts}.`
  }
  return ''
})
const addressCount = computed(() => {
  if (!IPV4.test(start.value) || !IPV4.test(end.value)) return 0
  return Math.max(0, toNumber(end.value) - toNumber(start.value) + 1)
})
const progress = computed(() => {
  if (!range.value || !range.value.hostCount) return 0
  return Math.min(100, Math.round((range.value.scanned / range.value.hostCount) * 100))
})

function flash(message, isError = false) {
  notice.value = message
  noticeError.value = isError
  setTimeout(() => (notice.value = ''), 4000)
}

async function scan() {
  busy.value = true
  const query = new URLSearchParams({ start: start.value.trim(), end: end.value.trim() })
  const result = await postJson(`/api/range?${query}`)
  busy.value = false
  flash(result.message, !result.ok)
}

async function stop() {
  const result = await postJson('/api/range/stop')
  flash(result.message)
}
</script>

<template>
  <div v-if="error" class="notice error">{{ error }}</div>
  <div v-if="notice" class="notice" :class="{ error: noticeError }">{{ notice }}</div>

  <div class="card">
    <h2>Address range</h2>
    <form class="range-form" @submit.prevent="scan">
      <label>
        Start IP
        <input v-model="start" placeholder="192.168.1.1" spellcheck="false" autocomplete="off" :disabled="scanning" />
      </label>
      <label>
        End IP
        <input v-model="end" placeholder="192.168.1.254" spellcheck="false" autocomplete="off" :disabled="scanning" />
      </label>
      <button type="submit" :disabled="busy || scanning || !start || !end || !!inputError">Scan range</button>
      <button v-if="scanning" type="button" class="secondary" @click="stop">Stop</button>
    </form>
    <p v-if="inputError" class="notice error" style="margin: 12px 0 0">{{ inputError }}</p>
    <p v-else class="muted" style="margin: 12px 0 0">
      <template v-if="addressCount">{{ addressCount }} addresses. </template>
      Scans any IPv4 range, including other subnets reachable through a router. MAC address and vendor are only
      available for hosts on the local subnet.
    </p>
  </div>

  <div v-if="range && range.phase !== 'idle'" class="card">
    <div class="toolbar">
      <h2 style="margin: 0">{{ range.start }} – {{ range.end }}</h2>
      <span class="badge" :class="scanning ? 'scanning' : 'watching'">{{ range.phase }}</span>
    </div>
    <div class="progress"><span :style="{ width: (scanning ? progress : 100) + '%' }"></span></div>
    <table class="details">
      <tr><th>Addresses</th><td>{{ range.hostCount }}</td></tr>
      <tr><th>Scanned</th><td>{{ range.scanned }}</td></tr>
      <tr><th>Devices found</th><td>{{ range.found }}</td></tr>
      <tr><th>Started</th><td>{{ formatTime(range.started) }}</td></tr>
      <tr><th>{{ scanning ? 'Elapsed' : 'Duration' }}</th><td>{{ formatDuration(range.duration) }}</td></tr>
      <tr v-if="range.error"><th>Error</th><td class="notice error" style="display: block">{{ range.error }}</td></tr>
    </table>
  </div>

  <div v-if="range && range.phase !== 'idle' && range.devices.length === 0" class="card muted">
    <template v-if="scanning">Scanning the range, devices will appear as they answer...</template>
    <template v-else>No devices answered in this range.</template>
  </div>
  <div v-else-if="!range" class="card muted">Loading...</div>

  <div v-if="range" class="grid">
    <DeviceCard v-for="device in range.devices" :key="device.ip" :device="device" :now="range.now" />
  </div>
</template>
