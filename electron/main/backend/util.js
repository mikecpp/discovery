'use strict'

const { execFile } = require('node:child_process')

function now() {
  return Date.now()
}

function parseIp(text) {
  const parts = String(text || '').trim().split('.')
  if (parts.length !== 4) return null
  let value = 0
  for (const part of parts) {
    if (!/^\d{1,3}$/.test(part)) return null
    const byte = Number(part)
    if (byte > 255) return null
    value = value * 256 + byte
  }
  return value >>> 0
}

function ipToString(value) {
  return [24, 16, 8, 0].map((shift) => (value >>> shift) & 255).join('.')
}

function prefixLength(netmask) {
  let length = 0
  let mask = netmask >>> 0
  while (mask & 0x80000000) {
    length += 1
    mask = (mask << 1) >>> 0
  }
  return length
}

function formatMac(text) {
  const parts = String(text || '').split(/[:-]/)
  if (parts.length !== 6 || parts.some((part) => !/^[0-9a-fA-F]{1,2}$/.test(part))) return ''
  const mac = parts.map((part) => part.padStart(2, '0').toLowerCase()).join(':')
  return mac === '00:00:00:00:00:00' || mac === 'ff:ff:ff:ff:ff:ff' ? '' : mac
}

function run(command, args, timeoutMs = 5000) {
  return new Promise((resolve) => {
    execFile(command, args, { timeout: timeoutMs, windowsHide: true, maxBuffer: 4 * 1024 * 1024 }, (error, stdout) => {
      resolve({ code: error ? (typeof error.code === 'number' ? error.code : -1) : 0, output: String(stdout || '') })
    })
  })
}

async function pool(items, concurrency, isCancelled, task) {
  let next = 0
  const count = Math.min(Math.max(1, concurrency), items.length)
  const worker = async () => {
    while (!isCancelled()) {
      const index = next++
      if (index >= items.length) return
      try {
        await task(items[index])
      } catch {
      }
    }
  }
  await Promise.all(Array.from({ length: count }, worker))
}

module.exports = { now, parseIp, ipToString, prefixLength, formatMac, run, pool }
