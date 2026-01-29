CC = gcc
CFLAGS = -O2 -Wall

BINS = udp_receiver udp_sender

all: $(BINS)

udp_receiver: udp_receiver.c
	$(CC) $(CFLAGS) -o $@ $^

udp_sender: udp_sender.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(BINS)

.PHONY: all clean
