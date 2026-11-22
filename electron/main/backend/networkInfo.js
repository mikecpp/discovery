'use strict'

const fs = require('node:fs')
const os = require('node:os')
const { formatMac, run } = require('./util')

function sysfsNumber(name, file) {
  try {
    return Number(fs.readFileSync(`/sys/class/net/${name}/${file}`, 'utf8').trim()) || 0
  } catch {
    return 0
  }
}

function sysfsUp(name) {
  try {
    const flags = parseInt(fs.readFileSync(`/sys/class/net/${name}/flags`, 'utf8').trim(), 16)
    return { up: (flags & 0x1) !== 0, running: (flags & 0x40) !== 0 }
  } catch {
    return null
  }
}

async function macLinkStats() {
  const stats = new Map()
  const { output } = await run('/usr/sbin/netstat', ['-ibn'])
  for (const line of output.split('\n').slice(1)) {
    const fields = line.trim().split(/\s+/)
    if (fields.length < 7 || !fields[2].startsWith('<Link#')) continue
    const down = fields[0].endsWith('*')
    const name = fields[0].replace(/\*$/, '')
    const hasMac = formatMac(fields[3]) !== '' || /^[0-9a-f]{1,2}(:[0-9a-f]{1,2}){5}$/i.test(fields[3])
    const offset = hasMac ? 4 : 3
    stats.set(name, {
      mac: hasMac ? formatMac(fields[3]) : '',
      mtu: Number(fields[1]) || 0,
      rxBytes: Number(fields[offset + 2]) || 0,
      txBytes: Number(fields[offset + 5]) || 0,
      up: !down,
      running: !down,
    })
  }
  return stats
}

async function interfaces() {
  const result = new Map()
  const entry = (name) => {
    if (!result.has(name)) {
      result.set(name, { name, mac: '', up: true, running: true, loopback: false, mtu: 0, rxBytes: 0, txBytes: 0, addresses: [] })
    }
    return result.get(name)
  }

  const macStats = process.platform === 'darwin' ? await macLinkStats() : new Map()
  for (const name of macStats.keys()) entry(name)
  if (process.platform === 'linux') {
    try {
      for (const name of fs.readdirSync('/sys/class/net')) {
        const item = entry(name)
        try {
          item.mac = formatMac(fs.readFileSync(`/sys/class/net/${name}/address`, 'utf8').trim())
        } catch {
        }
      }
    } catch {
    }
  }

  for (const [name, addresses] of Object.entries(os.networkInterfaces())) {
    const item = entry(name)
    for (const address of addresses || []) {
      const family = address.family === 'IPv4' || address.family === 4 ? 'ipv4' : 'ipv6'
      const prefix = address.cidr ? Number(address.cidr.split('/')[1]) : 0
      item.loopback = item.loopback || address.internal
      if (!item.mac) item.mac = formatMac(address.mac)
      item.addresses.push({ family, address: address.address.replace(/%.*$/, ''), prefix })
    }
  }

  for (const item of result.values()) {
    if (process.platform === 'darwin') {
      const stats = macStats.get(item.name)
      if (stats) {
        Object.assign(item, { mtu: stats.mtu, rxBytes: stats.rxBytes, txBytes: stats.txBytes, up: stats.up, running: stats.running })
        if (!item.mac) item.mac = stats.mac
      }
      if (item.name.startsWith('lo')) item.loopback = true
    } else if (process.platform === 'linux') {
      item.rxBytes = sysfsNumber(item.name, 'statistics/rx_bytes')
      item.txBytes = sysfsNumber(item.name, 'statistics/tx_bytes')
      item.mtu = sysfsNumber(item.name, 'mtu')
      Object.assign(item, sysfsUp(item.name) || {})
    }
  }
  return [...result.values()]
}

module.exports = { interfaces }
