#!/bin/bash
# cleanup.sh - Cleanup TUN devices

echo "Cleaning up TUN devices..."

# Try to remove all tun devices
for i in {0..99}; do
    DEV="tun$i"
    ip link del "$DEV" 2>/dev/null || true
done

echo "✓ Cleanup complete"
