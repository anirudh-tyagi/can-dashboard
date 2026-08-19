#!/usr/bin/env bash
# Bring up a virtual CAN interface. Run on the Linux host, as root.
#   sudo ./scripts/vcan-up.sh [iface]
set -euo pipefail

IFACE="${1:-vcan0}"

# vcan is a kernel module that implements a loopback CAN driver: anything
# written to the interface is delivered to every other socket bound to it.
# That loopback IS our bus.
modprobe vcan

# Create the interface if it isn't there already (idempotent re-runs).
if ! ip link show "$IFACE" >/dev/null 2>&1; then
    ip link add dev "$IFACE" type vcan
fi

ip link set up "$IFACE"

echo "[vcan-up] $IFACE is up:"
ip -details -brief link show "$IFACE"
