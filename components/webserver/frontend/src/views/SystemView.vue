<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import CpuChart from '../components/CpuChart.vue'
import { formatBytes, formatUptime, getJson, usePolling } from '../api.js'

const system = ref(null)
const error = ref('')

const polling = usePolling(async () => {
  try {
    system.value = await getJson('/api/system')
    error.value = ''
  } catch (failure) {
    error.value = failure.message
  }
}, 1000)

onMounted(() => polling.start())
onUnmounted(() => polling.stop())

const memoryPercent = computed(() => {
  if (!system.value?.memoryTotal) return 0
  return Math.round((system.value.memoryUsed / system.value.memoryTotal) * 100)
})
const windowMs = computed(() => system.value?.window || 120000)
const level = (value) => (value >= 90 ? 'critical' : value >= 70 ? 'warning' : '')
</script>

<template>
  <div v-if="error" class="notice error">{{ error }}</div>
  <template v-if="system">
    <div class="tiles">
      <div class="tile">
        <span class="tile-label">CPU</span>
        <span class="tile-value">{{ system.cpu.toFixed(0) }}<small>%</small></span>
        <span class="muted">{{ system.cores }} cores</span>
      </div>
      <div class="tile">
        <span class="tile-label">Load average</span>
        <span class="tile-value">{{ system.load[0].toFixed(2) }}</span>
        <span class="muted">{{ system.load[1].toFixed(2) }} · {{ system.load[2].toFixed(2) }} (5 / 15 min)</span>
      </div>
      <div class="tile">
        <span class="tile-label">Memory</span>
        <span class="tile-value">{{ memoryPercent }}<small>%</small></span>
        <span class="muted">{{ formatBytes(system.memoryUsed) }} of {{ formatBytes(system.memoryTotal) }}</span>
      </div>
      <div class="tile">
        <span class="tile-label">Uptime</span>
        <span class="tile-value">{{ formatUptime(system.uptime) }}</span>
        <span class="muted">{{ system.hostname }}</span>
      </div>
    </div>

    <div class="card">
      <h2>CPU utilization</h2>
      <p class="muted chart-caption">All cores, sampled every {{ system.interval / 1000 }} s over the last {{ windowMs / 60000 }} minutes</p>
      <CpuChart :history="system.history" :window-ms="windowMs" />
    </div>

    <div class="card">
      <h2>Per core</h2>
      <div class="cores">
        <div v-for="(usage, index) in system.coreUsage" :key="index" class="core">
          <span class="core-name">CPU {{ index }}</span>
          <div class="meter" :class="level(usage)"><span :style="{ width: usage + '%' }"></span></div>
          <span class="core-value">{{ usage.toFixed(0) }}%</span>
        </div>
      </div>
    </div>

    <div class="card">
      <h2>System</h2>
      <table class="details">
        <tr><th>Hostname</th><td>{{ system.hostname || '-' }}</td></tr>
        <tr><th>Operating system</th><td>{{ system.os || '-' }}</td></tr>
        <tr><th>Kernel</th><td>{{ system.kernel || '-' }}</td></tr>
        <tr><th>Architecture</th><td>{{ system.arch || '-' }}</td></tr>
        <tr><th>Processor</th><td>{{ system.cpuModel || '-' }}</td></tr>
        <tr><th>Logical cores</th><td>{{ system.cores }}</td></tr>
        <tr><th>Memory</th><td>{{ formatBytes(system.memoryTotal) }}</td></tr>
      </table>
    </div>
  </template>
  <div v-else class="card muted">Loading...</div>
</template>
