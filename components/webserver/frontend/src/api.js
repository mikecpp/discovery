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
