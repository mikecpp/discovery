# Device Discovery Installation Guide

Version 1.0.0

Device Discovery scans the local IPv4 subnet with multi-threaded ICMP pings, guesses each device's type and operating system, keeps watching every device, and shows the result as green/red cards in a web page served on port 8080.

This guide covers installing the Debian package or the pre-built release tarball on a Linux host, running the scanner by hand or as a systemd service, verifying that it works, and removing it again. Building from source is covered at the end.

## 1. Requirements

| Item | Requirement |
| --- | --- |
| Operating system | Linux, x86_64 (glibc 2.31 or newer; Ubuntu 20.04+, Debian 11+, Fedora 34+). WSL2 is supported. |
| Network | An interface with an IPv4 address on the subnet you want to scan |
| Privileges | `root` (or `sudo`) for the installation steps; the scanner itself runs as a normal user |
| Optional | `setcap` from `libcap2-bin` (Debian/Ubuntu) or `libcap` (Fedora) to grant native ICMP sockets |
| Optional | `systemd` to run the scanner as a background service |
| Optional | A web browser on the same host, or network access to port 8080 from another machine |

The binary is statically configured: the web port, scan intervals and other settings were chosen at build time and are listed in section 6. Nothing needs to be installed next to the binary except the ping fallback described in section 3.

## 2. Package contents

The release is available in two forms:

- `discovery_1.0.0_amd64.deb`: Debian package for Debian 12+, Ubuntu 22.04+ and derivatives. Runtime only: installs `/usr/bin/discovery` and the documentation under `/usr/share/doc/discovery/`. It does not install a service. See section 3.1.
- `discovery-1.0.0-Linux-x86_64.tar.gz`: generic tarball for any other Linux distribution, including the optional systemd service. See section 3.2.

The tarball is a single gzip-compressed tar archive named `discovery-1.0.0-Linux-x86_64.tar.gz`. Unpacking it creates one directory:

```text
discovery-1.0.0-Linux-x86_64/
  install.sh                        Installer (copies files, sets capability, optional service)
  uninstall.sh                      Removes everything install.sh created
  bin/discovery                     The scanner and embedded web server
  share/discovery/discovery.service systemd unit template
  share/doc/discovery/README.md     Project overview, configuration and API reference
  share/doc/discovery/INSTALL.md    This guide
  share/doc/discovery/INSTALL.pdf   This guide as PDF
```

## 3. Install

### 3.1 Debian package

```bash
sudo apt install ./discovery_1.0.0_amd64.deb
```

The package depends on `libcap2-bin`, which `apt` installs automatically. During installation the package grants `CAP_NET_RAW` to `/usr/bin/discovery`. It does not install or start a service; run `discovery` from a shell as described in section 4.1. If you want the scanner to run as a systemd service, use the tarball with `install.sh --service` (section 3.3) or copy the unit from the tarball by hand (section 3.5). To remove the package:

```bash
sudo apt remove discovery
```

The remaining subsections describe the tarball.

### 3.2 Unpack

```bash
tar -xzf discovery-1.0.0-Linux-x86_64.tar.gz
cd discovery-1.0.0-Linux-x86_64
```

### 3.3 Run the installer

The simplest installation copies the binary to `/usr/local/bin` and grants it the `CAP_NET_RAW` capability so it can send ICMP echo requests without running as root:

```bash
sudo ./install.sh
```

To also register and start a systemd service that runs the scanner at boot:

```bash
sudo ./install.sh --service
```

Installer options:

| Option | Effect |
| --- | --- |
| `--prefix DIR` | Install under `DIR` instead of `/usr/local` (binary goes to `DIR/bin`) |
| `--service` | Install `/etc/systemd/system/discovery.service`, enable it and start it now |
| `--no-setcap` | Do not call `setcap`; the scanner falls back to the system `ping` command |
| `--help` | Print the option summary |

### 3.4 How ICMP permission is chosen

The scanner tries three ways to send pings, in this order, and reports the one in use on the Network page and in its first log line:

1. **ICMP datagram socket**: works for unprivileged users when the kernel's `net.ipv4.ping_group_range` includes the user's group.
2. **Raw socket**: requires `CAP_NET_RAW`, which `install.sh` grants with `setcap`.
3. **External ping**: spawns `/usr/bin/ping` for each probe. Slower and heavier, but needs no privileges because the system `ping` binary is already privileged.

If `setcap` is unavailable, you can enable the first mode system-wide instead:

```bash
sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"
echo 'net.ipv4.ping_group_range = 0 2147483647' | sudo tee /etc/sysctl.d/60-ping.conf
```

### 3.5 Manual installation

If you prefer not to run the script, the equivalent steps are:

```bash
sudo install -m 755 bin/discovery /usr/local/bin/discovery
sudo setcap cap_net_raw+ep /usr/local/bin/discovery
sudo install -m 644 share/discovery/discovery.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now discovery.service
```

## 4. Run

### 4.1 In the foreground

```bash
discovery
```

Example output:

```text
[INFO] 2026-11-10 10:30:01 Scanning 192.168.1.0/24 on eth0 (192.168.1.42) using datagram socket with 32 threads
[INFO] 2026-11-10 10:30:01 Web server listening on http://localhost:8080/
```

The default browser is opened automatically (`open` on macOS, `xdg-open` on Linux, `explorer.exe` on WSL). If no browser can be opened the scanner logs an error and keeps running. Press `Ctrl+C` to stop.

### 4.2 As a service

After `install.sh --service` the scanner runs in the background and restarts on failure.

```bash
sudo systemctl status discovery        # state and recent log lines
sudo journalctl -u discovery -f        # follow the log
sudo systemctl restart discovery       # restart after a network change
sudo systemctl disable --now discovery # stop and do not start at boot
```

The unit runs the binary with only `CAP_NET_RAW`, a read-only filesystem view and a private `/tmp`. A browser cannot be opened from a service, so the "Cannot open web browser" error in the journal is expected and harmless.

### 4.3 Using the web page

Open `http://localhost:8080/` on the host, or `http://<host-ip>:8080/` from another machine on the network. The page has a vertical navigator with three views:

- **Devices**: one card per discovered device. Green means the device answered the last ping, red means it missed two consecutive pings. Each card shows the IP address, host name, MAC vendor, guessed type and operating system, and any open ports found by the TCP probe. A list view is available as well.
- **Scan**: the current phase (scanning, watching), progress, the subnet and interface in use, the active ICMP mode and the configured intervals. The **Scan now** button starts a full rescan immediately.
- **Discover**: scan an arbitrary IPv4 range (start and end address) that is not the local subnet, with a stop button.

The page polls the JSON API every few seconds; no manual refresh is needed.

## 5. Verify the installation

1. Check the binary and its capability:

```bash
which discovery
getcap /usr/local/bin/discovery      # expect: cap_net_raw=ep
```

2. Query the API from the shell:

```bash
curl -s http://localhost:8080/api/status
curl -s http://localhost:8080/api/devices | head -c 400
```

A JSON object with `phase`, `subnet` and `icmpMode` fields confirms the server is up; the first scan of a /24 subnet completes in a few seconds.

3. If the device list stays empty, see section 7.

## 6. Built-in settings

These values are compiled into the released binary. Changing them requires a rebuild (section 9).

| Setting | Value | Meaning |
| --- | --- | --- |
| Web bind address / port | `0.0.0.0` / `8080` | The web UI and API are reachable from every interface |
| Open browser on start | yes | Launches `open`, `xdg-open` or `explorer.exe` once listening |
| Network interface | auto | Interface of the default route, or the first with an IPv4 address |
| Scan threads | 32 | Parallel ping workers |
| Maximum hosts per scan | 4096 | Larger subnets are truncated |
| Watch interval | 5 s | How often each known device is pinged again |
| Full rescan interval | 120 s | How often the whole subnet is swept for new devices |
| Missed replies before offline | 2 | Consecutive misses that turn a card red |
| Ping timeout / payload | 1000 ms / 56 bytes | Per-probe reply timeout and echo size |
| Ping fallback command | `/usr/bin/ping` | Used when no ICMP socket can be opened |
| Identify type and OS | yes | TTL, MAC vendor and a 500 ms TCP port probe |
| Reverse DNS names | yes | Resolve host names for discovered devices |

## 7. Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Discovery failed to start: no interface with an IPv4 address` | The host has no usable interface. Connect to a network or rebuild with `DISCOVERY_INTERFACE` set. |
| `Web server failed to start: bind: Address already in use` | Another program uses port 8080. Stop it, or rebuild with a different `WEBSERVER_PORT`. |
| Network page shows mode `external ping` | Neither socket type could be opened. Run `sudo setcap cap_net_raw+ep $(which discovery)` or set `net.ipv4.ping_group_range` (section 3.4). |
| Devices known to be online show red | Their firewall drops ICMP echo. Such devices cannot be tracked by ping. |
| Page not reachable from another machine | A host firewall blocks TCP 8080. Allow it, for example `sudo ufw allow 8080/tcp`. |
| On WSL2 only the WSL virtual subnet is scanned | WSL2 has its own NAT network. Run the scanner on the Windows side of the network or use the Discover page to scan the LAN range. |
| `Cannot open web browser with xdg-open` | No desktop session (typical for a service or SSH login). Open the URL manually. |
| Service fails with capability errors | The kernel or container does not allow `CAP_NET_RAW`. Remove the `Capability*` lines from the unit; the scanner then uses the ping fallback. |

## 8. Uninstall

Debian package:

```bash
sudo apt remove discovery
```

Tarball:

```bash
sudo ./uninstall.sh              # from the unpacked tarball directory
sudo ./uninstall.sh --prefix DIR # if installed with a custom prefix
```

This stops and removes the systemd unit if present, then deletes the binary and documentation. Nothing else is written to the system: the scanner keeps no state files or configuration on disk.

## 9. Building from source

Requirements: CMake 3.16+, a C++17 compiler, Node.js with npm (for the embedded Vue 3 frontend), and optionally Python 3 with `kconfiglib` for `menuconfig` and `reportlab` for the PDF guide.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                      # builds frontend, binary and INSTALL.pdf
cmake --build build --target menuconfig  # optional: change settings, then rebuild
cmake --build build --target package     # writes build/discovery-1.0.0-Linux-x86_64.tar.gz
cmake --build build --target deb         # writes discovery_1.0.0_amd64.deb in the source root
```

The `package` target produces exactly the tarball described in section 2, with the binary stripped. The `deb` target builds the Debian package with CPack (`dpkg-deb` and `dpkg-shlibdeps` from `dpkg-dev` are required). `cmake --install build` installs directly into `CMAKE_INSTALL_PREFIX` without a tarball.
