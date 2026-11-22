'use strict'

const fs = require('node:fs')
const os = require('node:os')
const { parseIp, ipToString, prefixLength, formatMac, run } = require('./util')

async function defaultRoute() {
  if (process.platform === 'linux') {
    try {
      const lines = fs.readFileSync('/proc/net/route', 'utf8').split('\n').slice(1)
      for (const line of lines) {
        const [name, destination, gateway, flags] = line.trim().split(/\s+/)
        if (destination === '00000000' && (parseInt(flags, 16) & 0x2) !== 0) {
          const raw = parseInt(gateway, 16)
          const value = ((raw & 255) << 24 | ((raw >>> 8) & 255) << 16 | ((raw >>> 16) & 255) << 8 | (raw >>> 24)) >>> 0
          return { interfaceName: name, gateway: ipToString(value) }
        }
      }
    } catch {
    }
    return null
  }
  if (process.platform === 'darwin') {
    const { output } = await run('/sbin/route', ['-n', 'get', 'default'])
    const gateway = output.match(/gateway:\s*(\S+)/)
    const name = output.match(/interface:\s*(\S+)/)
    if (!name) return null
    return { interfaceName: name[1], gateway: gateway && parseIp(gateway[1]) !== null ? gateway[1] : '' }
  }
  if (process.platform === 'win32') {
    const { output } = await run('route', ['print', '-4', '0.0.0.0'])
    for (const line of output.split('\n')) {
      const fields = line.trim().split(/\s+/)
      if (fields.length >= 5 && fields[0] === '0.0.0.0' && fields[1] === '0.0.0.0' && parseIp(fields[2]) !== null) {
        return { interfaceAddress: fields[3], gateway: fields[2] }
      }
    }
  }
  return null
}

function candidates() {
  const result = []
  for (const [name, entries] of Object.entries(os.networkInterfaces())) {
    for (const entry of entries || []) {
      if (entry.family !== 'IPv4' && entry.family !== 4) continue
      if (entry.internal) continue
      const address = parseIp(entry.address)
      const netmask = parseIp(entry.netmask)
      if (address === null || netmask === null) continue
      result.push({ name, address, netmask, mac: formatMac(entry.mac) })
    }
  }
  return result
}

async function detect(preferredInterface) {
  const list = candidates()
  const route = await defaultRoute()
  const matchesRoute = (candidate) =>
    route && (route.interfaceName ? candidate.name === route.interfaceName : ipToString(candidate.address) === route.interfaceAddress)

  let chosen = null
  if (preferredInterface) {
    chosen = list.find((candidate) => candidate.name === preferredInterface) || null
  } else {
    chosen = list.find((candidate) => matchesRoute(candidate) && prefixLength(candidate.netmask) < 32) ||
      list.find((candidate) => prefixLength(candidate.netmask) < 32) || null
  }
  if (!chosen) {
    throw new Error(preferredInterface
      ? `Interface ${preferredInterface} has no IPv4 address`
      : 'No network interface with an IPv4 subnet was found')
  }

  const prefix = prefixLength(chosen.netmask)
  const network = (chosen.address & chosen.netmask) >>> 0
  const broadcast = (network | ~chosen.netmask) >>> 0
  const first = prefix >= 31 ? network : network + 1
  const last = prefix >= 31 ? broadcast : broadcast - 1
  return {
    interfaceName: chosen.name,
    address: ipToString(chosen.address),
    netmask: ipToString(chosen.netmask),
    network: ipToString(network),
    gateway: matchesRoute(chosen) ? route.gateway : '',
    macAddress: chosen.mac,
    prefix,
    self: chosen.address,
    first,
    last,
    cidr() {
      return `${this.network}/${this.prefix}`
    },
    hostCount() {
      return last < first ? 0 : last - first + 1
    },
    hosts(maxHosts) {
      const count = Math.min(this.hostCount(), maxHosts)
      return Array.from({ length: count }, (_, index) => ipToString(first + index))
    },
  }
}

module.exports = { detect }
