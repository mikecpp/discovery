'use strict'

const { Icmp } = require('./icmp')
const { Identifier } = require('./identifier')
const { newDevice, applyIdentity } = require('./discovery')
const { now, parseIp, ipToString, pool } = require('./util')

class RangeScan {
  constructor(settings, subnet) {
    this.settings = { ...settings, threads: Math.max(1, settings.threads) }
    this.subnet = subnet
    this.identifier = new Identifier({
      probeTimeoutMs: settings.probeTimeoutMs,
      resolveNames: settings.resolveNames,
      gateway: subnet.gateway,
      selfMac: subnet.macAddress,
    })
    this.devices = new Map()
    this.scanning = false
    this.cancel = false
    this.scanned = 0
    this.task = null
    this.status = {
      phase: 'idle',
      start: '',
      end: '',
      error: '',
      hostCount: 0,
      maxHosts: this.settings.maxHosts,
      started: 0,
      finished: 0,
    }
  }

  start(startIp, endIp) {
    const first = parseIp(startIp)
    const last = parseIp(endIp)
    if (first === null) return { result: 'invalid', error: `Invalid start address: ${startIp}` }
    if (last === null) return { result: 'invalid', error: `Invalid end address: ${endIp}` }
    if (last < first) return { result: 'invalid', error: 'The start address must not be after the end address.' }
    const count = last - first + 1
    if (count > this.settings.maxHosts) {
      return { result: 'invalid', error: `The range has ${count} addresses; the limit is ${this.settings.maxHosts}.` }
    }
    if (this.scanning) return { result: 'busy', error: 'A range scan is already running.' }

    const icmp = new Icmp(this.settings.icmp)
    if (!icmp.isOpen()) return { result: 'invalid', error: `Cannot open ICMP: ${icmp.lastError}` }
    const ips = Array.from({ length: count }, (_, index) => ipToString(first + index))
    this.devices.clear()
    Object.assign(this.status, {
      phase: 'scanning',
      start: ipToString(first),
      end: ipToString(last),
      error: '',
      hostCount: count,
      started: now(),
      finished: 0,
    })
    this.scanned = 0
    this.cancel = false
    this.scanning = true
    this.task = this.run(icmp, ips)
    return { result: 'started', start: this.status.start, end: this.status.end }
  }

  stop() {
    this.cancel = true
  }

  async dispose() {
    this.cancel = true
    await this.task
  }

  snapshot() {
    return { ...this.status, scanned: this.scanned, found: this.devices.size }
  }

  deviceList() {
    return [...this.devices.values()].sort((a, b) => a.address - b.address)
  }

  async run(icmp, ips) {
    await pool(ips, this.settings.threads, () => this.cancel, (ip) => this.probe(icmp, ip))
    this.status.finished = now()
    this.status.phase = this.cancel ? 'stopped' : 'done'
    this.scanning = false
  }

  async probe(icmp, ip) {
    const result = await icmp.ping(ip)
    this.scanned += 1
    if (result.status !== 'ok') return
    const time = now()
    const address = parseIp(ip)
    const device = newDevice(ip, address, this.subnet, time)
    Object.assign(device, {
      online: true,
      identified: !this.settings.identify,
      ttl: result.ttl,
      rttMs: result.rttMs,
      lastSeen: time,
    })
    this.devices.set(address, device)
    if (!this.settings.identify || this.cancel) return
    applyIdentity(device, await this.identifier.identify(ip, result.ttl, device.self))
  }
}

module.exports = { RangeScan }
