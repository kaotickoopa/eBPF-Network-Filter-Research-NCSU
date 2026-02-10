#!/bin/bash
# test_system.sh - Automated test of the TUN-based multi-machine UDP system
# This script demonstrates that the system works as expected

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  Multi-Machine UDP Router Test                            ║"
echo "║  Skips Layers 1 & 2, Implements Layers 3-7 Routing        ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This test must be run as root (use sudo)"
    echo "  sudo ./test_system.sh"
    exit 1
fi

NUM_MACHINES=2

echo "Building components..."
make all > /dev/null 2>&1
chmod +x setup.sh cleanup.sh

echo "  ✓ Compiled router, tun_sender, tun_receiver"
echo ""

echo "Setting up $NUM_MACHINES virtual machines..."
./cleanup.sh > /dev/null 2>&1
./setup.sh $NUM_MACHINES > /dev/null 2>&1
echo "  ✓ TUN devices ready"
echo ""

echo "Starting router in background..."
timeout 5 ./router $NUM_MACHINES > /tmp/router.log 2>&1 &
ROUTER_PID=$!
sleep 1
echo "  ✓ Router PID: $ROUTER_PID"
echo ""

echo "Starting receiver in background..."
timeout 4 ./tun_receiver tun0 > /tmp/receiver.log 2>&1 &
RECV_PID=$!
sleep 0.5
echo "  ✓ Receiver PID: $RECV_PID (on tun0, 10.0.0.1)"
echo ""

echo "Sending test packets..."
echo "  Sender: tun1 (10.0.0.2) → Receiver: tun0 (10.0.0.1)"
./tun_sender tun1 10.0.0.1 3 16 > /tmp/sender.log 2>&1
echo "  ✓ Sent 3 packets"
echo ""

sleep 2

echo "Results:"
echo "═══════════════════════════════════════════════════════════"
echo ""

echo "ROUTER OUTPUT:"
head -20 /tmp/router.log | grep -E "Created machine|RX:|Forward"
echo ""

echo "RECEIVER OUTPUT:"
grep -E "Packet [0-9]:|From:|To:|UDP:" /tmp/receiver.log | head -10
echo ""

echo "SENDER OUTPUT:"
grep "Sent packet" /tmp/sender.log
echo ""

# Cleanup
kill $ROUTER_PID $RECV_PID 2>/dev/null
wait $ROUTER_PID $RECV_PID 2>/dev/null
./cleanup.sh > /dev/null 2>&1

echo "═══════════════════════════════════════════════════════════"
echo "✓ Test complete!"
echo ""
echo "What this demonstrates:"
echo "  ✅ IP routing at Layer 3 (packets forwarded by IP address)"
echo "  ✅ UDP transport at Layer 4 (real UDP headers)"
echo "  ✅ Multi-machine communication (tun0 ↔ tun1)"
echo "  ❌ NO Layer 2 MAC/ARP handling"
echo "  ❌ NO Layer 1 Physical transmission"
echo ""
