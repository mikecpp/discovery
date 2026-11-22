'use strict'

const fs = require('node:fs')
const os = require('node:os')
const { execFileSync } = require('node:child_process')
const { now, run } = require('./util')

function osName() {
  if (process.platform === 'darwin') {
    try {
      const version = execFileSync('/usr/bin/sw_vers', ['-productVersion'], { encoding: 'utf8' }).trim()
      return version ? `macOS ${version}` : 'macOS'
    } catch {
      return 'macOS'
    }
  }
  if (process.platform === 'linux') {
    try {
      const match = fs.readFileSync('/etc/os-release', 'utf8').match(/^PRETTY_NAME=\s*"?([^"\n]*)"?/m)
      if (match && match[1]) return match[1]
    } catch {
    }
    return os.type()
  }
  return typeof os.version === 'function' ? os.version() : os.type()
}

function readTicks() {
  const cores = os.cpus().map(({ times }) => {
    const busy = times.user + times.nice + times.sys + times.irq
    return { busy, total: busy + times.idle }
  })
  const total = cores.reduce((sum, core) => ({ busy: sum.busy + core.busy, total: sum.total + core.total }), { busy: 0, total: 0 })
  return { total, cores }
}

function usage(before, after) {
  if (!before || after.total <= before.total || after.busy < before.busy) return 0
  const value = (100 * (after.busy - before.busy)) / (after.total - before.total)
  return Math.min(100, Math.max(0, value))
}

async function memory() {
  const total = os.totalmem()
  let available = os.freemem()
  if (process.platform === 'linux') {
    try {
      const match = fs.readFileSync('/proc/meminfo', 'utf8').match(/^MemAvailable:\s+(\d+)/m)
      if (match) available = Number(match[1]) * 1024
    } catch {
    }
  } else if (process.platform === 'darwin') {
    const { output } = await run('/usr/bin/vm_stat', [])
    const page = Number((output.match(/page size of (\d+)/) || [])[1]) || 4096
    const pages = (name) => Number((output.match(new RegExp(`${name}:\\s+(\\d+)`)) || [])[1]) || 0
    const free = pages('Pages free') + pages('Pages inactive') + pages('Pages speculative')
    if (output) available = free * page
  }
  return { total, used: total > available ? total - available : 0 }
}

class SystemMonitor {
  constructor(intervalMs = 1000, historySize = 120) {
    this.intervalMs = Math.max(100, intervalMs)
    this.historySize = Math.max(1, historySize)
    this.hostname = os.hostname()
    this.os = osName()
    this.kernel = `${os.type()} ${os.release()}`
    this.arch = typeof os.machine === 'function' ? os.machine() : os.arch()
    this.cpuModel = (os.cpus()[0] || {}).model || this.arch
    this.history = []
    this.cpuUsage = 0
    this.coreUsage = []
    this.last = null
    this.timer = null
    this.memoryCache = { at: 0, value: { total: os.totalmem(), used: 0 } }
  }

  start() {
    if (this.timer) return
    this.last = readTicks()
    this.timer = setInterval(() => this.sample(), this.intervalMs)
  }

  stop() {
    clearInterval(this.timer)
    this.timer = null
  }

  sample() {
    const current = readTicks()
    this.cpuUsage = usage(this.last.total, current.total)
    this.coreUsage = current.cores.map((core, index) => usage(this.last.cores[index], core))
    this.last = current
    this.history.push({ t: now(), cpu: this.cpuUsage })
    while (this.history.length > this.historySize) this.history.shift()
  }

  async memory() {
    if (now() - this.memoryCache.at > 2000) {
      this.memoryCache = { at: now(), value: await memory() }
    }
    return this.memoryCache.value
  }

  async snapshot() {
    const mem = await this.memory()
    return {
      hostname: this.hostname,
      os: this.os,
      kernel: this.kernel,
      arch: this.arch,
      cpuModel: this.cpuModel,
      cores: os.cpus().length,
      uptime: Math.floor(os.uptime()),
      load: os.loadavg(),
      memoryTotal: mem.total,
      memoryUsed: mem.used,
      cpu: this.cpuUsage,
      coreUsage: this.coreUsage,
      history: this.history.slice(),
      interval: this.intervalMs,
      window: this.intervalMs * this.historySize,
      now: now(),
    }
  }
}

module.exports = { SystemMonitor }
