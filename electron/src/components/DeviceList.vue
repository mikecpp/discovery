<script setup>
import DeviceCard from './DeviceCard.vue'
import { deviceLabel, formatAgo, useLayout } from '../api.js'

defineProps({
  devices: { type: Array, required: true },
  now: { type: Number, required: true },
})

const layout = useLayout()
</script>

<template>
  <div v-if="layout === 'cards'" class="grid">
    <DeviceCard v-for="device in devices" :key="device.ip" :device="device" :now="now" />
  </div>
  <div v-else-if="devices.length" class="card table-wrap">
    <table class="device-table">
      <thead>
        <tr>
          <th></th>
          <th>IP</th>
          <th>Name</th>
          <th>Type</th>
          <th>OS</th>
          <th>Vendor</th>
          <th>MAC</th>
          <th>Ping</th>
          <th>Seen</th>
          <th>Ports</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="device in devices" :key="device.ip" :class="{ off: !device.online }">
          <td><span class="dot" :class="device.online ? 'on' : 'off'" :title="device.online ? 'On' : 'Off'"></span></td>
          <td class="ip">{{ device.ip }}</td>
          <td>
            {{ deviceLabel(device) }}
            <span v-if="device.self" class="tag self">self</span>
            <span v-if="device.gateway" class="tag gateway">gateway</span>
          </td>
          <td>{{ device.identified ? device.type || 'Unknown device' : 'Identifying...' }}</td>
          <td>{{ device.os || '-' }}</td>
          <td>{{ device.vendor || '-' }}</td>
          <td class="mono">{{ device.mac || '-' }}</td>
          <td class="nowrap">
            <template v-if="device.online">{{ device.rtt.toFixed(1) }} ms · TTL {{ device.ttl }}</template>
            <template v-else>no reply</template>
          </td>
          <td class="nowrap">{{ formatAgo(device.lastSeen, now) }}</td>
          <td>
            <span v-for="port in device.openPorts" :key="port" class="tag">{{ port }}</span>
            <template v-if="!device.openPorts.length">-</template>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
