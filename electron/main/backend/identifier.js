'use strict'

const dns = require('node:dns')
const fs = require('node:fs')
const net = require('node:net')
const os = require('node:os')
const oui = require('./oui')
const { formatMac, run } = require('./util')

const PROBE_PORTS = [21, 22, 23, 53, 80, 135, 139, 443, 445, 548, 554, 631, 1883, 3389,
  5000, 5900, 8008, 8009, 8080, 8443, 9100, 62078]

const ARP_CACHE_MS = 2000
let arpTable = new Map()
let arpReadAt = 0
let arpReading = null

async function readArpTable() {
  const table = new Map()
  if (process.platform === 'linux') {
    try {
      for (const line of fs.readFileSync('/proc/net/arp', 'utf8').split('\n').slice(1)) {
        const [ip, , , mac] = line.trim().split(/\s+/)
        const value = formatMac(mac)
        if (ip && value) table.set(ip, value)
      }
    } catch {
    }
    return table
  }
  const { output } = process.platform === 'win32'
    ? await run('arp', ['-a'])
    : await run('/usr/sbin/arp', ['-an'])
  for (const line of output.split('\n')) {
    const match = process.platform === 'win32'
      ? line.match(/^\s*(\d+\.\d+\.\d+\.\d+)\s+([0-9a-fA-F-]{11,17})\s/)
      : line.match(/\((\d+\.\d+\.\d+\.\d+)\)\s+at\s+([0-9a-fA-F:]+)/)
    if (!match) continue
    const value = formatMac(match[2])
    if (value) table.set(match[1], value)
  }
  return table
}

function refreshArp() {
  if (!arpReading) {
    const startedAt = Date.now()
    arpReading = readArpTable().then((table) => {
      arpTable = table
      arpReadAt = startedAt
      arpReading = null
    })
  }
  return arpReading
}

async function arpLookup(ip) {
  const requestedAt = Date.now()
  if (requestedAt - arpReadAt > ARP_CACHE_MS) await refreshArp()
  if (!arpTable.has(ip)) {
    while (arpReadAt < requestedAt) await refreshArp()
  }
  return arpTable.get(ip) || ''
}

function resolveName(ip, timeoutMs = 2000) {
  return new Promise((resolve) => {
    const timer = setTimeout(() => resolve(''), timeoutMs)
    dns.lookupService(ip, 0, (error, hostname) => {
      clearTimeout(timer)
      resolve(!error && hostname && hostname !== ip ? hostname : '')
    })
  })
}

function probePort(ip, port, timeoutMs) {
  return new Promise((resolve) => {
    const socket = new net.Socket()
    const done = (open) => {
      socket.destroy()
      resolve(open)
    }
    socket.setTimeout(timeoutMs)
    socket.once('connect', () => done(true))
    socket.once('timeout', () => done(false))
    socket.once('error', () => done(false))
    socket.connect(port, ip)
  })
}

async function probePorts(ip, timeoutMs) {
  const results = await Promise.all(PROBE_PORTS.map((port) => probePort(ip, port, timeoutMs)))
  return PROBE_PORTS.filter((_, index) => results[index])
}

function osFromTtl(ttl) {
  if (ttl <= 0) return ''
  if (ttl > 128) return 'Embedded / network OS'
  if (ttl > 64) return 'Windows'
  return 'Linux / Unix'
}

function classify(ip, ttl, self, identity, gateway) {
  const ports = identity.openPorts
  const vendor = identity.vendor
  const has = (port) => ports.includes(port)
  const any = (list) => list.some(has)
  const set = (type, osName) => {
    identity.type = type
    if (osName !== undefined) identity.os = osName
  }
  identity.os = osFromTtl(ttl)

  if (self) return set('This computer', `${os.type()} ${os.release()}`)
  if (gateway && ip === gateway) {
    return set('Router / gateway', !identity.os || ttl > 128 ? 'Router firmware' : identity.os)
  }
  if (vendor === 'Espressif') return set('IoT device (ESP)', 'ESP firmware')
  if (vendor === 'Raspberry Pi') return set('Raspberry Pi', 'Linux')
  if (vendor === 'Apple') {
    if (has(62078)) return set('iPhone / iPad', 'iOS')
    if (any([22, 548, 5900, 88])) return set('Mac', 'macOS')
    return set('Apple device', 'iOS / macOS')
  }
  if (has(62078)) return set('iPhone / iPad', 'iOS')
  if (has(3389) || (has(445) && has(135))) return set('Windows PC', 'Windows')
  if (vendor === 'Hikvision' || vendor === 'Dahua' || has(554)) return set('IP camera', 'Embedded Linux')
  if (['Brother', 'Canon', 'Epson'].includes(vendor) || (any([9100, 631]) && !any([22, 445]))) {
    return set('Printer', 'Printer firmware')
  }
  if (vendor === 'Synology' || vendor === 'QNAP' || (has(445) && has(22))) return set('NAS / file server', 'Linux')
  if (vendor === 'Sonos') return set('Speaker', 'Embedded Linux')
  if (vendor === 'Philips Hue') return set('Smart home hub', 'Embedded Linux')
  if (vendor === 'Roku' || vendor === 'Amazon' || any([8008, 8009])) {
    return set(vendor === 'Google' || has(8009) ? 'Chromecast / Google device' : 'Media player', 'Embedded Linux')
  }
  if (vendor === 'Nintendo' || vendor === 'Sony') return set('Game console', `${vendor} system software`)
  if (['VMware', 'VirtualBox', 'QEMU', 'Docker'].includes(vendor) || identity.mac.startsWith('00:15:5d')) {
    return set('Virtual machine')
  }
  if (['TP-Link', 'Netgear', 'Ubiquiti', 'Cisco'].includes(vendor) || (vendor === 'ASUS' && ttl > 128)) {
    return set('Network device', 'Network firmware')
  }
  if (['Samsung', 'Xiaomi', 'Huawei', 'LG'].includes(vendor)) {
    return set(ttl > 64 ? 'Smart device' : 'Phone / smart device', ttl > 64 ? identity.os : 'Android / Linux')
  }
  if (has(22)) return set('Linux / Unix host')
  if (has(1883)) return set('MQTT broker')
  if (ttl > 128) return set('Network / embedded device')
  if (ttl > 64) return set('Windows PC')
  if (ttl > 0) return set(any([80, 443]) ? 'Linux device with web UI' : 'Linux / Unix host')
  return set('Unknown device')
}

class Identifier {
  constructor(options = {}) {
    this.probeTimeoutMs = options.probeTimeoutMs ?? 500
    this.resolveNames = options.resolveNames ?? true
    this.gateway = options.gateway || ''
    this.selfMac = options.selfMac || ''
  }

  async identify(ip, ttl, self) {
    const identity = { hostname: '', mac: '', vendor: '', type: '', os: '', openPorts: [] }
    if (self) {
      identity.hostname = os.hostname()
      identity.mac = this.selfMac
    } else {
      const [ports, hostname] = await Promise.all([
        probePorts(ip, this.probeTimeoutMs),
        this.resolveNames ? resolveName(ip) : Promise.resolve(''),
      ])
      identity.openPorts = ports
      identity.hostname = hostname
      identity.mac = await arpLookup(ip)
    }
    identity.vendor = oui.vendor(identity.mac)
    classify(ip, ttl, self, identity, this.gateway)
    return identity
  }
}

module.exports = { Identifier }
