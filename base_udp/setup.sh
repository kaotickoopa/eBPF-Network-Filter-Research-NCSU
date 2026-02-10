#!/bin/bash
# setup.sh - Setup script for TUN devices and routing
# Usage: sudo ./setup.sh [num_machines]

set -e

NUM_MACHINES=${1:-3}

if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  TUN Device Setup for Multi-Machine UDP Router            ║"
echo "║  Setting up $NUM_MACHINES virtual machines                       ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Create TUN devices
echo "Creating TUN devices..."
for i in $(seq 0 $((NUM_MACHINES - 1))); do
    DEV="tun$i"
    IP="10.0.0.$((i + 1))"
    
    echo "  $DEV → $IP/24"
    
    # Create the device (already done by ioctl in router, but ensure it's up)
    ip addr add "$IP/24" dev "$DEV" 2>/dev/null || true
    ip link set "$DEV" up 2>/dev/null || true
done

echo ""
echo "✓ TUN devices ready"
echo ""
echo "Routing configuration:"
for i in $(seq 0 $((NUM_MACHINES - 1))); do
    echo "  tun$i: 10.0.0.$((i + 1))"
done

echo ""
echo "To clean up, run: sudo ./cleanup.sh"
