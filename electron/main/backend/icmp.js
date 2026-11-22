'use strict'

const fs = require('node:fs')
const { parseIp, run } = require('./util')

const isWindows = process.platform === 'win32'
const isMac = process.platform === 'darwin'

const CANDIDATES = isWindows
  ? []
  : ['/sbin/ping', '/bin/ping', '/usr/bin/ping', '/usr/sbin/ping']

function findPing(preferred) {
  if (isWindows) return 'ping'
  const list = preferred ? [preferred, ...CANDIDATES] : CANDIDATES
  for (const candidate of list) {
    try {
      fs.accessSync(candidate, fs.constants.X_OK)
      return candidate
    } catch {
    }
  }
  return ''
}

function numberAfter(text, pattern) {
  const match = text.match(pattern)
  return match ? Number(match[1]) : 0
}

class Icmp {
  constructor(options = {}) {
    this.timeoutMs = Math.max(1, options.timeoutMs ?? 1000)
    this.dataSize = Math.min(1400, Math.max(0, options.dataSize ?? 56))
    this.command = findPing(options.command)
    this.mode = this.command ? 'external ping' : 'none'
    this.lastError = this.command ? '' : 'No executable ping command was found'
  }

  isOpen() {
    return this.mode !== 'none'
  }

  args(ip) {
    if (isWindows) return ['-n', '1', '-w', String(this.timeoutMs), '-l', String(this.dataSize), ip]
    const wait = isMac ? String(this.timeoutMs) : String(Math.ceil(this.timeoutMs / 1000))
    return ['-n', '-c', '1', '-W', wait, '-s', String(this.dataSize), ip]
  }

  async ping(ip) {
    if (!this.isOpen()) return { status: 'not open', ttl: 0, rttMs: 0 }
    if (parseIp(ip) === null) return { status: 'invalid address', ttl: 0, rttMs: 0 }
    const { code, output } = await run(this.command, this.args(ip), this.timeoutMs + 3000)
    const ttl = numberAfter(output, /ttl[=:](\d+)/i)
    if (ttl > 0) {
      const rttMs = output.match(/time\s*<\s*1\s*ms/i) ? 0.5 : numberAfter(output, /time[=<]\s*([\d.]+)/i)
      return { status: 'ok', ttl, rttMs }
    }
    if (/unreachable/i.test(output)) return { status: 'unreachable', ttl: 0, rttMs: 0 }
    return { status: code === 1 || code === 2 || code === 0 ? 'timeout' : 'send failed', ttl: 0, rttMs: 0 }
  }
}

module.exports = { Icmp }
