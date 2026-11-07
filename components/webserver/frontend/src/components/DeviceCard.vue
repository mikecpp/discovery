<script setup>
import { deviceLabel, formatAgo } from '../api.js'

defineProps({
  device: { type: Object, required: true },
  now: { type: Number, required: true },
})
</script>

<template>
  <article class="device" :class="device.online ? 'on' : 'off'">
    <header>
      <span class="dot" :class="device.online ? 'on' : 'off'"></span>
      <span class="ip">{{ device.ip }}</span>
      <span class="state" :class="device.online ? 'on' : 'off'">{{ device.online ? 'ON' : 'OFF' }}</span>
    </header>
    <div class="name">
      {{ deviceLabel(device) }}
      <span v-if="device.self" class="tag self">self</span>
      <span v-if="device.gateway" class="tag gateway">gateway</span>
    </div>
    <div class="type">{{ device.identified ? device.type || 'Unknown device' : 'Identifying...' }}</div>
    <dl>
      <dt>OS</dt>
      <dd>{{ device.os || '-' }}</dd>
      <dt>Vendor</dt>
      <dd>{{ device.vendor || '-' }}</dd>
      <dt>MAC</dt>
      <dd>{{ device.mac || '-' }}</dd>
      <dt>Ping</dt>
      <dd>
        <template v-if="device.online">{{ device.rtt.toFixed(1) }} ms · TTL {{ device.ttl }}</template>
        <template v-else>no reply</template>
      </dd>
      <dt>Seen</dt>
      <dd>{{ formatAgo(device.lastSeen, now) }}</dd>
      <dt>Ports</dt>
      <dd>
        <template v-if="device.openPorts.length">
          <span v-for="port in device.openPorts" :key="port" class="tag">{{ port }}</span>
        </template>
        <template v-else>-</template>
      </dd>
    </dl>
  </article>
</template>
