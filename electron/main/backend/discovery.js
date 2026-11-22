'use strict'

const { Icmp } = require('./icmp')
const { Identifier } = require('./identifier')
const subnetModule = require('./subnet')
const { now, parseIp, pool } = require('./util')

function newDevice(ip, address, subnet, time) {
  return {
    ip,
    address,
    hostname: '',
    mac: '',
    vendor: '',
    type: '',
    os: '',
    openPorts: [],
    ttl: 0,
    rttMs: 0,
    online: false,
    self: address === subnet.self,
    gateway: !!subnet.gateway && ip === subnet.gateway,
    identified: false,
    missed: 0,
    firstSeen: time,
    lastSeen: 0,
    lastChange: time,
  }
}

function applyIdentity(device, identity) {
  Object.assign(device, {
    hostname: identity.hostname,
    mac: identity.mac,
    vendor: identity.vendor,
    type: identity.type,
    os: identity.os,
    openPorts: identity.openPorts,
    identified: true,
  })
}

class Discovery {
  constructor(settings) {
    this.settings = { ...settings, threads: Math.max(1, settings.threads) }
    this.subnet = null
    this.icmp = null
    this.identifier = null
    this.devices = new Map()
    this.running = false
    this.scanRequested = false
    this.scanned = 0
    this.nextRescanAt = 0
    this.wake = null
    this.loop = null
    this.status = {
      phase: 'idle',
      icmpMode: '',
      error: '',
      hostCount: 0,
      scanCount: 0,
      threads: this.settings.threads,
      watchIntervalS: this.settings.watchIntervalS,
      rescanIntervalS: this.settings.rescanIntervalS,
      lastScanStarted: 0,
      lastScanFinished: 0,
      lastScanDurationMs: 0,
      lastWatch: 0,
    }
  }

  async start() {
    if (this.running) return
    this.subnet = await subnetModule.detect(this.settings.interfaceName)
    this.icmp = new Icmp(this.settings.icmp)
    if (!this.icmp.isOpen()) throw new Error(`Cannot open ICMP: ${this.icmp.lastError}`)
    this.identifier = new Identifier({
      probeTimeoutMs: this.settings.probeTimeoutMs,
      resolveNames: this.settings.resolveNames,
      gateway: this.subnet.gateway,
      selfMac: this.subnet.macAddress,
    })
    this.status.icmpMode = this.icmp.mode
    this.status.hostCount = Math.min(this.subnet.hostCount(), this.settings.maxHosts)
    this.running = true
    this.loop = this.run()
  }

  async stop() {
    if (!this.running) return
    this.running = false
    this.notify()
    await this.loop
  }

  requestScan() {
    this.scanRequested = true
    this.notify()
  }

  notify() {
    if (this.wake) this.wake()
  }

  deviceList() {
    return [...this.devices.values()].sort((a, b) => a.address - b.address)
  }

  snapshot() {
    let online = 0
    let offline = 0
    for (const device of this.devices.values()) {
      if (device.online) online += 1
      else offline += 1
    }
    const remaining = this.nextRescanAt - now()
    return {
      ...this.status,
      scanned: this.scanned,
      online,
      offline,
      nextRescanS: remaining > 0 ? Math.ceil(remaining / 1000) : 0,
    }
  }

  async run() {
    while (this.running) {
      this.scanRequested = false
      await this.scanAll()
      this.nextRescanAt = now() + this.settings.rescanIntervalS * 1000
      while (this.running && !this.scanRequested && now() < this.nextRescanAt) {
        if (!(await this.waitFor(this.settings.watchIntervalS))) break
        await this.watchKnown()
      }
    }
    this.status.phase = 'stopped'
  }

  waitFor(seconds) {
    return new Promise((resolve) => {
      const finish = () => {
        clearTimeout(timer)
        this.wake = null
        resolve(this.running && !this.scanRequested)
      }
      const timer = setTimeout(finish, seconds * 1000)
      this.wake = finish
      if (!this.running || this.scanRequested) finish()
    })
  }

  async scanAll() {
    const started = now()
    this.status.lastScanStarted = started
    await this.runBatch(this.subnet.hosts(this.settings.maxHosts), 'scanning')
    this.status.lastScanFinished = now()
    this.status.lastScanDurationMs = this.status.lastScanFinished - started
    this.status.scanCount += 1
    this.status.phase = 'watching'
  }

  async watchKnown() {
    const ips = [...this.devices.values()].map((device) => device.ip)
    if (ips.length) await this.runBatch(ips, 'watching')
    this.status.lastWatch = now()
    this.status.phase = 'watching'
  }

  async runBatch(ips, phase) {
    this.scanned = 0
    this.status.phase = phase
    await pool(ips, this.settings.threads, () => !this.running, (ip) => this.probe(ip))
  }

  async probe(ip) {
    const result = await this.icmp.ping(ip)
    const time = now()
    const address = parseIp(ip)
    let device = this.devices.get(address)
    let needIdentify = false
    if (result.status === 'ok') {
      if (!device) {
        device = newDevice(ip, address, this.subnet, time)
        this.devices.set(address, device)
      }
      if (!device.online) {
        device.online = true
        device.lastChange = time
      }
      device.missed = 0
      device.ttl = result.ttl
      device.rttMs = result.rttMs
      device.lastSeen = time
      needIdentify = this.settings.identify && !device.identified && !device.identifying
    } else if (device) {
      device.missed += 1
      if (device.online && device.missed >= this.settings.offlineMisses) {
        device.online = false
        device.lastChange = time
      }
    }
    this.scanned += 1
    if (!needIdentify) return
    device.identifying = true
    try {
      applyIdentity(device, await this.identifier.identify(ip, result.ttl, device.self))
    } finally {
      device.identifying = false
    }
  }
}

module.exports = { Discovery, newDevice, applyIdentity }
