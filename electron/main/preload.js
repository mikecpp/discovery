'use strict'

const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('discovery', {
  request: (method, url) => ipcRenderer.invoke('api:request', method, url),
})
