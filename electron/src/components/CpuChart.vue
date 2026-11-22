<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'

const props = defineProps({
  history: { type: Array, default: () => [] },
  windowMs: { type: Number, default: 120000 },
  height: { type: Number, default: 220 },
})

const PAD = { top: 12, right: 48, bottom: 26, left: 40 }
const TICKS = [0, 25, 50, 75, 100]

const root = ref(null)
const width = ref(600)
const hover = ref(null)
let observer = null

onMounted(() => {
  observer = new ResizeObserver(([entry]) => {
    width.value = Math.max(240, Math.round(entry.contentRect.width))
  })
  observer.observe(root.value)
})
onUnmounted(() => observer?.disconnect())

const plotW = computed(() => width.value - PAD.left - PAD.right)
const plotH = computed(() => props.height - PAD.top - PAD.bottom)
const end = computed(() => (props.history.length ? props.history[props.history.length - 1].t : Date.now()))
const start = computed(() => end.value - props.windowMs)

const x = (t) => PAD.left + ((t - start.value) / props.windowMs) * plotW.value
const y = (value) => PAD.top + (1 - Math.min(100, Math.max(0, value)) / 100) * plotH.value

const points = computed(() => props.history.filter((sample) => sample.t >= start.value))
const linePath = computed(() =>
  points.value.map((sample, index) => `${index ? 'L' : 'M'}${x(sample.t).toFixed(1)},${y(sample.cpu).toFixed(1)}`).join(''),
)
const areaPath = computed(() => {
  if (points.value.length < 2) return ''
  const first = points.value[0]
  const last = points.value[points.value.length - 1]
  const base = y(0).toFixed(1)
  return `${linePath.value}L${x(last.t).toFixed(1)},${base}L${x(first.t).toFixed(1)},${base}Z`
})
const latest = computed(() => points.value[points.value.length - 1] ?? null)

const xTicks = computed(() => {
  const seconds = props.windowMs / 1000
  const step = seconds > 300 ? 60 : seconds > 60 ? 30 : 15
  const ticks = []
  for (let ago = 0; ago <= seconds; ago += step) {
    ticks.push({ x: x(end.value - ago * 1000), label: ago === 0 ? 'now' : ago % 60 === 0 ? `-${ago / 60} min` : `-${ago} s` })
  }
  return ticks
})

function onMove(event) {
  if (!points.value.length) return
  const rect = root.value.getBoundingClientRect()
  const px = event.clientX - rect.left
  let best = points.value[0]
  for (const sample of points.value) {
    if (Math.abs(x(sample.t) - px) < Math.abs(x(best.t) - px)) best = sample
  }
  hover.value = best
}

const tooltipStyle = computed(() => {
  if (!hover.value) return {}
  const left = x(hover.value.t)
  const flip = left > width.value - 140
  return {
    left: `${flip ? left - 12 : left + 12}px`,
    top: `${Math.max(0, y(hover.value.cpu) - 44)}px`,
    transform: flip ? 'translateX(-100%)' : 'none',
  }
})

const summary = computed(() =>
  latest.value
    ? `CPU utilization over the last ${props.windowMs / 1000} seconds, currently ${latest.value.cpu.toFixed(0)} percent.`
    : 'CPU utilization, waiting for samples.',
)
</script>

<template>
  <div ref="root" class="chart" @pointermove="onMove" @pointerleave="hover = null">
    <svg :width="width" :height="height" role="img" :aria-label="summary">
      <g class="grid">
        <g v-for="tick in TICKS" :key="tick">
          <line :x1="PAD.left" :x2="PAD.left + plotW" :y1="y(tick)" :y2="y(tick)" />
          <text :x="PAD.left - 8" :y="y(tick)" text-anchor="end" dominant-baseline="middle">{{ tick }}%</text>
        </g>
        <text v-for="tick in xTicks" :key="tick.label" :x="tick.x" :y="height - 6" text-anchor="middle">{{ tick.label }}</text>
      </g>
      <path v-if="areaPath" class="area" :d="areaPath" />
      <path v-if="points.length > 1" class="line" :d="linePath" />
      <template v-if="latest">
        <circle class="dot" :cx="x(latest.t)" :cy="y(latest.cpu)" r="4" />
        <text class="end-label" :x="x(latest.t) + 8" :y="y(latest.cpu)" dominant-baseline="middle">{{ latest.cpu.toFixed(0) }}%</text>
      </template>
      <template v-if="hover">
        <line class="crosshair" :x1="x(hover.t)" :x2="x(hover.t)" :y1="PAD.top" :y2="PAD.top + plotH" />
        <circle class="dot" :cx="x(hover.t)" :cy="y(hover.cpu)" r="5" />
      </template>
      <text v-if="!points.length" class="empty" :x="PAD.left + plotW / 2" :y="PAD.top + plotH / 2" text-anchor="middle">Collecting samples...</text>
    </svg>
    <div v-if="hover" class="chart-tooltip" :style="tooltipStyle">
      <strong>{{ hover.cpu.toFixed(1) }}%</strong>
      <span>{{ new Date(hover.t).toLocaleTimeString() }}</span>
    </div>
  </div>
</template>
