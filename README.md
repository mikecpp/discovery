# Device Discovery

Scans the local IPv4 subnet with multi-threaded ICMP pings, guesses each device's type and operating system, keeps watching every device, and shows the result as green/red cards in a Vue 3 web page served by the built-in HTTP server on port 8080.

## Layout

| Path | Purpose |
| --- | --- |
| `Kconfig`, `defconfig` | Component and application configuration (Kconfig) |
| `components/icmp` | ICMP echo client (datagram socket, raw socket, or system `ping` fallback) |
| `components/webserver` | HTTP server with the embedded Vue 3 frontend (`frontend/`) |
| `src/` | Subnet detection, discovery engine, device identification, JSON API |
| `main.cpp` | Wires discovery, web server and browser launch together |

## Build

Requirements: CMake 3.16+, a C++17 compiler, Node.js with npm (frontend), Python 3 with `kconfiglib` (only for `menuconfig`).

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/discovery
```

The first build runs `npm ci` and `vite build` inside `components/webserver/frontend` and embeds the result into the binary. The browser is opened automatically once the server is listening.

## Configuration

`.config` is created from `defconfig` on the first configure. Change it with:

```bash
cmake --build build --target menuconfig
cmake --build build --target savedefconfig
```

Important options:

| Option | Default | Meaning |
| --- | --- | --- |
| `COMPONENT_WEBSERVER` / `WEBSERVER_PORT` | y / 8080 | Web UI and JSON API; without it the device table is printed on the console |
| `COMPONENT_ICMP` / `ICMP_TIMEOUT_MS` | y / 1000 | Ping component and reply timeout |
| `ICMP_EXTERNAL_FALLBACK` | y | Use the system `ping` when ICMP sockets are not permitted |
| `DISCOVERY_INTERFACE` | "" | Interface to scan; empty picks the default-route interface |
| `DISCOVERY_THREADS` | 32 | Parallel ping threads |
| `DISCOVERY_WATCH_INTERVAL` | 5 s | Re-ping known devices |
| `DISCOVERY_RESCAN_INTERVAL` | 120 s | Full subnet rescan to find new devices |
| `DISCOVERY_IDENTIFY` | y | Guess type/OS from TTL, MAC vendor and a TCP port probe |

## ICMP permissions

Native ICMP sockets need either `CAP_NET_RAW` or an enabled `net.ipv4.ping_group_range`. Pick one:

```bash
cmake --build build --target setcap            # sudo setcap cap_net_raw+ep build/discovery
sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"
```

Without either, the ICMP component falls back to spawning the system `ping` command (`ICMP_EXTERNAL_FALLBACK`). The active mode is shown on the Scan page.

## API

| Method | Path | Description |
| --- | --- | --- |
| GET | `/api/info` | Page title |
| GET | `/api/status` | Scan phase, progress, subnet and settings |
| GET | `/api/devices` | All known devices with status, type, OS, vendor, ports |
| POST | `/api/scan` | Request a full rescan now |

## Frontend development

```bash
cd components/webserver/frontend
npm run dev
```

The Vite dev server proxies `/api` to `http://localhost:8080`, so run `./build/discovery` alongside it.
