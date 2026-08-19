#!/usr/bin/env bash
# Tear the virtual CAN interface back down.
#   sudo ./scripts/vcan-down.sh [iface]
set -euo pipefail

IFACE="${1:-vcan0}"

if ip link show "$IFACE" >/dev/null 2>&1; then
    ip link set down "$IFACE"
    ip link delete "$IFACE" type vcan
    echo "[vcan-down] removed $IFACE"
else
    echo "[vcan-down] $IFACE does not exist, nothing to do"
fi
