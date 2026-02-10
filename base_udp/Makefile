CC = gcc
CFLAGS = -O2 -Wall -g
LDFLAGS = -lrt

# Socket-based UDP (original)
SOCKET_BINS = udp_receiver udp_sender

# TUN-based UDP multi-machine (new)
TUN_BINS = router tun_sender tun_receiver

ALL_BINS = $(SOCKET_BINS) $(TUN_BINS)

all: $(ALL_BINS)

# Original socket-based UDP
udp_receiver: udp_receiver.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

udp_sender: udp_sender.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# TUN-based multi-machine UDP
router: router.c
	$(CC) $(CFLAGS) -o $@ $^

tun_sender: tun_sender.c
	$(CC) $(CFLAGS) -o $@ $^

tun_receiver: tun_receiver.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ALL_BINS) *.o

.PHONY: all clean help

help:
	@echo ""
	@echo "╔═══════════════════════════════════════════════════════════╗"
	@echo "║  UDP Multi-Machine Communication Systems                  ║"
	@echo "╚═══════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "SOCKET-BASED UDP (Original - Uses Kernel Network Stack):"
	@echo "  make udp_sender"
	@echo "  make udp_receiver"
	@echo "  Usage: ./udp_receiver & ./udp_sender 10 32"
	@echo ""
	@echo "TUN-BASED MULTI-MACHINE (New - Skips Layers 1-2):"
	@echo "  make router"
	@echo "  make tun_sender"
	@echo "  make tun_receiver"
	@echo ""
	@echo "Build Everything:"
	@echo "  make all"
	@echo "  make clean"
	@echo ""
	@echo "Quick Start (TUN system):"
	@echo "  1. make all"
	@echo "  2. chmod +x *.sh"
	@echo "  3. sudo ./setup.sh 3"
	@echo "  4. sudo ./router 3 &"
	@echo "  5. sudo ./tun_receiver tun0 &"
	@echo "  6. sudo ./tun_sender tun1 10.0.0.1 5 32"
	@echo ""
