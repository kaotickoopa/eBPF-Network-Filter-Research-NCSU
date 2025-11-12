CC = gcc
CFLAGS = -O2 -Wall

BINS = udp_sender udp_receiver

all: $(BINS)

udp_sender: udp_sender.c
	$(CC) $(CFLAGS) -o $@ $^

udp_receiver: udp_receiver.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(BINS)

.PHONY: all clean
