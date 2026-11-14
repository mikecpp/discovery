import { ref, watch } from 'vue'

const LAYOUT_KEY = 'discovery.layout'
let layout = null

export function useLayout() {
  if (!layout) {
    let saved = null
    try {
      saved = localStorage.getItem(LAYOUT_KEY)
    } catch {
    }
    layout = ref(saved === 'list' ? 'list' : 'cards')
    watch(layout, (value) => {
      try {
        localStorage.setItem(LAYOUT_KEY, value)
      } catch {
      }
    })
  }
  return layout
}

export function deviceLabel(device) {
  if (device.hostname) return device.hostname
  if (device.self) return 'This computer'
  if (device.gateway) return 'Default gateway'
  return ''
}

export async function getJson(path) {
  const response = await fetch(path)
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status} ${response.statusText}`)
  }
  return response.json()
}

export async function postJson(path) {
  const response = await fetch(path, { method: 'POST' })
  const text = await response.text()
  let message = text.trim()
  try {
    message = JSON.parse(text).message
  } catch {
  }
  return { ok: response.ok, message }
}

export function usePolling(callback, intervalMs) {
  let timer = null
  let active = false
  const tick = async () => {
    if (!active) return
    try {
      await callback()
    } catch {
    }
    if (active) timer = setTimeout(tick, intervalMs)
  }
  return {
    start() {
      active = true
      tick()
    },
    stop() {
      active = false
      clearTimeout(timer)
    },
  }
}

export function formatAgo(timestamp, now) {
  if (!timestamp) return '-'
  const seconds = Math.max(0, Math.round((now - timestamp) / 1000))
  if (seconds < 5) return 'just now'
  if (seconds < 60) return `${seconds} s ago`
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes} min ago`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours} h ${minutes % 60} min ago`
  return `${Math.floor(hours / 24)} d ago`
}

export function formatTime(timestamp) {
  if (!timestamp) return '-'
  return new Date(timestamp).toLocaleString()
}

export function formatDuration(ms) {
  if (!ms && ms !== 0) return '-'
  if (ms < 1000) return `${ms} ms`
  return `${(ms / 1000).toFixed(1)} s`
}

export function formatBytes(bytes) {
  if (!bytes && bytes !== 0) return '-'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let value = bytes
  let unit = 0
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit += 1
  }
  return `${value.toFixed(unit === 0 || value >= 100 ? 0 : 1)} ${units[unit]}`
}

export function formatUptime(seconds) {
  if (!seconds) return '-'
  const days = Math.floor(seconds / 86400)
  const hours = Math.floor((seconds % 86400) / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  if (days) return `${days}d ${hours}h`
  if (hours) return `${hours}h ${minutes}m`
  return `${minutes}m`
}
