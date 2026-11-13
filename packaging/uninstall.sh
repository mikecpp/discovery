#!/bin/sh
# Removes Device Discovery installed by install.sh.
#   sudo ./uninstall.sh [--prefix DIR]
set -eu

prefix=/usr/local
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) prefix=$2; shift ;;
        --prefix=*) prefix=${1#--prefix=} ;;
        -h|--help) sed -n '2,3p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [ -f /etc/systemd/system/discovery.service ]; then
    systemctl disable --now discovery.service 2>/dev/null || true
    rm -f /etc/systemd/system/discovery.service
    systemctl daemon-reload
fi
rm -f "$prefix/bin/discovery"
rm -rf "$prefix/share/doc/discovery"
echo "Removed discovery from $prefix"
