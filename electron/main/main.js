'use strict'

process.env.UV_THREADPOOL_SIZE = process.env.UV_THREADPOOL_SIZE || '32'

const path = require('node:path')
const { app, BrowserWindow, dialog, ipcMain, shell } = require('electron')
const config = require('./config')
const { createApi } = require('./api')
const { Discovery } = require('./backend/discovery')
const { RangeScan } = require('./backend/rangeScan')
const { SystemMonitor } = require('./backend/systemMonitor')

let services = null
let window = null

function log(level, text) {
  const stamp = new Date().toISOString().replace('T', ' ').slice(0, 19)
  console.log(`[${level}] ${stamp} ${text}`)
}

async function startServices() {
  const settings = config.load(app.getPath('userData'))
  const discovery = new Discovery(settings)
  await discovery.start()
  const subnet = discovery.subnet
  log('INFO', `Scanning ${subnet.cidr()} on ${subnet.interfaceName} (${subnet.address}) using ` +
    `${discovery.status.icmpMode} with ${settings.threads} workers`)
  const rangeScan = new RangeScan(settings, subnet)
  const monitor = new SystemMonitor()
  monitor.start()
  return { settings, discovery, rangeScan, monitor, handle: createApi({ settings, discovery, rangeScan, monitor }) }
}

function createWindow(title) {
  window = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 720,
    minHeight: 480,
    title,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  })
  window.removeMenu()
  window.once('ready-to-show', () => window.show())
  window.on('closed', () => {
    window = null
  })
  window.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/.test(url)) shell.openExternal(url)
    return { action: 'deny' }
  })
  if (process.env.VITE_DEV_SERVER_URL) {
    window.loadURL(process.env.VITE_DEV_SERVER_URL)
  } else {
    window.loadFile(path.join(__dirname, '..', 'dist', 'index.html'))
  }
}

async function stopServices() {
  if (!services) return
  const current = services
  services = null
  current.monitor.stop()
  await Promise.all([current.discovery.stop(), current.rangeScan.dispose()])
  log('INFO', 'Discovery stopped')
}

if (!app.requestSingleInstanceLock()) {
  app.quit()
} else {
  app.on('second-instance', () => {
    if (window) {
      if (window.isMinimized()) window.restore()
      window.focus()
    }
  })

  ipcMain.handle('api:request', (_event, method, url) =>
    services ? services.handle(method, url) : { status: 503, body: { message: 'Discovery is not running.' } })

  app.whenReady().then(async () => {
    try {
      services = await startServices()
    } catch (error) {
      log('ERROR', `Discovery failed to start: ${error.message}`)
      dialog.showErrorBox('Device Discovery', `Discovery failed to start: ${error.message}`)
      app.exit(1)
      return
    }
    createWindow(services.settings.title)
    app.on('activate', () => {
      if (!window) createWindow(services.settings.title)
    })
  })

  app.on('window-all-closed', () => {
    app.quit()
  })

  let quitting = false
  app.on('before-quit', (event) => {
    if (quitting || !services) return
    event.preventDefault()
    quitting = true
    stopServices().finally(() => app.quit())
  })
}
