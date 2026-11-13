#!/bin/sh
# Installs Device Discovery from the unpacked release tarball.
#
#   sudo ./install.sh                 install to /usr/local, grant CAP_NET_RAW
#   sudo ./install.sh --service       also install and start the systemd unit
#   sudo ./install.sh --prefix DIR    install under DIR instead of /usr/local
#   ./install.sh --no-setcap          skip setcap (falls back to the system ping)
set -eu

prefix=/usr/local
service=0
setcap_wanted=1

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) prefix=$2; shift ;;
        --prefix=*) prefix=${1#--prefix=} ;;
        --service) service=1 ;;
        --no-setcap) setcap_wanted=0 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

here=$(cd "$(dirname "$0")" && pwd)
bindir=$prefix/bin
docdir=$prefix/share/doc/discovery

if [ ! -x "$here/bin/discovery" ]; then
    echo "bin/discovery not found next to this script" >&2
    exit 1
fi

echo "Installing discovery to $bindir"
install -d "$bindir" "$docdir"
install -m 755 "$here/bin/discovery" "$bindir/discovery"
for doc in README.md INSTALL.md INSTALL.pdf; do
    [ -f "$here/share/doc/discovery/$doc" ] && install -m 644 "$here/share/doc/discovery/$doc" "$docdir/$doc"
done

if [ "$setcap_wanted" -eq 1 ]; then
    if command -v setcap >/dev/null 2>&1; then
        if setcap cap_net_raw+ep "$bindir/discovery"; then
            echo "Granted CAP_NET_RAW to $bindir/discovery"
        else
            echo "setcap failed; discovery will fall back to the system ping command" >&2
        fi
    else
        echo "setcap not found (install libcap2-bin); discovery will fall back to the system ping command" >&2
    fi
fi

if [ "$service" -eq 1 ]; then
    unit=/etc/systemd/system/discovery.service
    echo "Installing systemd unit $unit"
    sed "s#/usr/local/bin/discovery#$bindir/discovery#" "$here/share/discovery/discovery.service" > "$unit"
    chmod 644 "$unit"
    systemctl daemon-reload
    systemctl enable --now discovery.service
    systemctl --no-pager --lines=5 status discovery.service || true
fi

echo "Done. Run '$bindir/discovery' and open http://localhost:8080/"
