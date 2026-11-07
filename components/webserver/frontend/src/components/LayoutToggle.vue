<script setup>
import { useLayout } from '../api.js'

const layout = useLayout()
const options = [
  { value: 'cards', label: 'Cards', icon: '▦' },
  { value: 'list', label: 'List', icon: '☰' },
]
</script>

<template>
  <div class="segmented" role="group" aria-label="Device layout">
    <button
      v-for="option in options"
      :key="option.value"
      type="button"
      :class="{ active: layout === option.value }"
      :aria-pressed="layout === option.value"
      @click="layout = option.value"
    >
      <span class="icon">{{ option.icon }}</span>{{ option.label }}
    </button>
  </div>
</template>
