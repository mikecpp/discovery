'use strict'

const networkInfo = require('./backend/networkInfo')
const { now } = require('./backend/util')

function deviceJson(device) {
  return {
    ip: device.ip,
    hostname: device.hostname,
    mac: device.mac,
    vendor: device.vendor,
    type: device.type,
    os: device.os,
    openPorts: device.openPorts,
    ttl: device.ttl,
    rtt: device.rttMs,
    online: device.online,
    self: device.self,
    gateway: device.gateway,
    identified: device.identified,
    firstSeen: device.firstSeen,
    lastSeen: device.lastSeen,
    lastChange: device.lastChange,
  }
}

function json(body, status = 200) {
  return { status, body }
}

function message(text, status) {
  return json({ message: text }, status)
}

function createApi({ settings, discovery, rangeScan, monitor }) {
  const routes = {
    'GET /api/info': () => json({ title: settings.title }),
    'GET /api/status': () => {
      const status = discovery.snapshot()
      const subnet = discovery.subnet
      return json({
        phase: status.phase,
        icmpMode: status.icmpMode,
        error: status.error,
        interface: subnet.interfaceName,
        address: subnet.address,
        netmask: subnet.netmask,
        subnet: subnet.cidr(),
        gateway: subnet.gateway,
        mac: subnet.macAddress,
        hostCount: status.hostCount,
        scanned: status.scanned,
        online: status.online,
        offline: status.offline,
        devices: status.online + status.offline,
        scanCount: status.scanCount,
        threads: status.threads,
        watchInterval: status.watchIntervalS,
        rescanInterval: status.rescanIntervalS,
        nextRescan: status.nextRescanS,
        lastScanStarted: status.lastScanStarted,
        lastScanFinished: status.lastScanFinished,
        lastScanDuration: status.lastScanDurationMs,
        lastWatch: status.lastWatch,
        now: now(),
      })
    },
    'GET /api/devices': () => json({ devices: discovery.deviceList().map(deviceJson), now: now() }),
    'POST /api/scan': () => {
      discovery.requestScan()
      return message('A full scan has been requested.', 202)
    },
    'GET /api/range': () => {
      const status = rangeScan.snapshot()
      const end = status.finished || now()
      return json({
        phase: status.phase,
        start: status.start,
        end: status.end,
        error: status.error,
        hostCount: status.hostCount,
        scanned: status.scanned,
        found: status.found,
        maxHosts: status.maxHosts,
        started: status.started,
        finished: status.finished,
        duration: status.started ? end - status.started : 0,
        devices: rangeScan.deviceList().map(deviceJson),
        now: now(),
      })
    },
    'POST /api/range': (query) => {
      const start = query.get('start') || ''
      const end = query.get('end') || ''
      const outcome = rangeScan.start(start, end)
      if (outcome.result === 'started') return message(`Scanning ${start} - ${end}.`, 202)
      return message(outcome.error, outcome.result === 'busy' ? 409 : 400)
    },
    'POST /api/range/stop': () => {
      rangeScan.stop()
      return message('The range scan is stopping.', 202)
    },
    'GET /api/system': async () => json(await monitor.snapshot()),
    'GET /api/network': async () => json({ interfaces: await networkInfo.interfaces(), now: now() }),
  }

  return async function handle(method, url) {
    const parsed = new URL(String(url), 'app://local')
    const route = routes[`${String(method).toUpperCase()} ${parsed.pathname}`]
    if (!route) return message('Not found', 404)
    try {
      return await route(parsed.searchParams)
    } catch (error) {
      return message(error.message || String(error), 500)
    }
  }
}

module.exports = { createApi }
