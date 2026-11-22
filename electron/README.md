# Device Discovery (Electron)

Desktop version of Device Discovery. The Electron main process runs a Node.js port of the C++ backend (subnet discovery, device watching, identification, range scan, system and network info). The window shows the same Vue 3 frontend, which talks to the backend over IPC instead of HTTP. No port is opened.

## Layout

| Path | Purpose |
| --- | --- |
| `main/main.js` | App lifecycle, window, IPC handler `api:request` |
| `main/preload.js` | Exposes `window.discovery.request(method, url)` to the renderer |
| `main/api.js` | Same routes and JSON shapes as the C++ web server (`/api/status`, `/api/devices`, `/api/scan`, `/api/range`, `/api/range/stop`, `/api/system`, `/api/network`, `/api/info`) |
| `main/config.js` | Settings (the defaults match `defconfig`) |
| `main/backend/` | `discovery`, `rangeScan`, `icmp`, `identifier`, `subnet`, `oui`, `systemMonitor`, `networkInfo` |
| `src/`, `index.html` | Vue frontend copied from `components/webserver/frontend` |
| `scripts/dev.mjs` | Starts the Vite dev server and launches Electron against it |

## Run

Requirements: Node.js 20+ and npm.

```bash
cd electron
npm install
npm start        # build the frontend and launch the app
npm run dev      # Vite hot reload + Electron
```

If you start it from a VS Code terminal and it runs as plain Node, unset `ELECTRON_RUN_AS_NODE` first.

## Package

```bash
npm run dist       # macOS: dmg/zip, Linux: deb/AppImage, Windows: nsis → release/
npm run dist:dir   # unpacked app only
```

## Configuration

On first launch the app writes `config.json` to its user-data folder:

- macOS: `~/Library/Application Support/device-discovery/`
- Linux: `~/.config/device-discovery/`
- Windows: `%APPDATA%\device-discovery\`

Edit that file and restart the app to apply changes.

| Key | Default | Meaning |
| --- | --- | --- |
| `title` | Device Discovery | Window and sidebar title |
| `interfaceName` | "" | Interface to scan; empty picks the default-route interface |
| `threads` | 32 | Parallel pings |
| `maxHosts` | 4096 | Host limit for a scan or a range |
| `watchIntervalS` / `rescanIntervalS` | 5 / 120 | How often known devices are re-pinged, and how often the subnet is fully rescanned |
| `offlineMisses` | 2 | Missed replies before a device is marked offline |
| `identify`, `probeTimeoutMs`, `resolveNames` | true, 500, true | Type and OS guessing (TTL, MAC vendor, TCP port probe, reverse DNS) |
| `icmp.timeoutMs`, `icmp.dataSize`, `icmp.command` | 1000, 56, "" | Ping options; an empty `command` finds the system `ping` automatically |

## Differences from the C++ version

- ICMP always goes through the system `ping` command (the C++ "external ping" mode), so the app needs no root access and no extra capabilities. The status line reports this as `external ping`.
- Kconfig/`menuconfig` is replaced by `config.json`.
- There is no built-in HTTP server and no browser launch. The window is the UI.
- The app also runs on Windows, using `ping`, `arp -a` and `route print`.
