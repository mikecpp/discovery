'use strict'

const fs = require('node:fs')
const path = require('node:path')

const DEFAULTS = {
  title: 'Device Discovery',
  interfaceName: '',
  threads: 32,
  maxHosts: 4096,
  watchIntervalS: 5,
  rescanIntervalS: 120,
  offlineMisses: 2,
  identify: true,
  probeTimeoutMs: 500,
  resolveNames: true,
  icmp: {
    timeoutMs: 1000,
    dataSize: 56,
    command: '',
  },
}

const RANGES = {
  threads: [1, 256],
  maxHosts: [1, 65534],
  watchIntervalS: [1, 3600],
  rescanIntervalS: [5, 86400],
  offlineMisses: [1, 10],
  probeTimeoutMs: [50, 10000],
}

function clamp(value, [min, max], fallback) {
  const number = Number(value)
  return Number.isFinite(number) ? Math.min(max, Math.max(min, Math.round(number))) : fallback
}

function load(userDataDir) {
  const file = path.join(userDataDir, 'config.json')
  let user = {}
  try {
    user = JSON.parse(fs.readFileSync(file, 'utf8'))
  } catch (error) {
    if (error.code === 'ENOENT') {
      try {
        fs.mkdirSync(userDataDir, { recursive: true })
        fs.writeFileSync(file, `${JSON.stringify(DEFAULTS, null, 2)}\n`)
      } catch {
      }
    }
  }
  const settings = { ...DEFAULTS, ...user, icmp: { ...DEFAULTS.icmp, ...(user.icmp || {}) } }
  for (const [key, range] of Object.entries(RANGES)) {
    settings[key] = clamp(settings[key], range, DEFAULTS[key])
  }
  settings.icmp.timeoutMs = clamp(settings.icmp.timeoutMs, [10, 60000], DEFAULTS.icmp.timeoutMs)
  settings.icmp.dataSize = clamp(settings.icmp.dataSize, [0, 1400], DEFAULTS.icmp.dataSize)
  settings.file = file
  return settings
}

module.exports = { load, DEFAULTS }
