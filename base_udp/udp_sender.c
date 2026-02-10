// udp_sender.c (Multi-machine UDP socket version)
// Uses actual UDP sockets traversing OSI layers 3-7 only.
// Layer 3 (Network): IP addressing with inet_pton
// Layer 4 (Transport): UDP protocol via socket/sendto
// Layers 5-7: Application data with timestamps

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

#define DEFAULT_DST_ADDR "127.0.0.1"
#define DEFAULT_DST_PORT 9999
#define DEFAULT_SRC_PORT 5555
#define MAX_PAYLOAD 1472

// Application-layer packet structure (Layer 7)
// No embedded UDP header (kernel handles it via socket API)
struct app_pkt {
    uint64_t seq;           // Packet sequence number
    uint64_t send_sec;      // Send time - seconds (network order via htonll)
    uint64_t send_nsec;     // Send time - nanoseconds
    uint32_t payload_len;   // Payload size (network order via htonl)
    char payload[MAX_PAYLOAD];
};

// Network byte order conversion for 64-bit
static inline uint64_t htonll(uint64_t x) {
    return ((uint64_t)htonl(x & 0xFFFFFFFFUL) << 32) | htonl(x >> 32);
}

static inline uint64_t ntohll(uint64_t x) {
    return ((uint64_t)ntohl(x & 0xFFFFFFFFUL) << 32) | ntohl(x >> 32);
}

int main(int argc, char **argv)
{
    int count = 10;
    int payload = 32;
    int src_port = DEFAULT_SRC_PORT;
    int dst_port = DEFAULT_DST_PORT;
    const char *dst_addr = DEFAULT_DST_ADDR;

    // Parse command line: count [payload] [dst_ip] [dst_port] [src_port]
    if (argc >= 2) count = atoi(argv[1]);
    if (argc >= 3) payload = atoi(argv[2]);
    if (argc >= 4) dst_addr = argv[3];
    if (argc >= 5) dst_port = atoi(argv[4]);
    if (argc >= 6) src_port = atoi(argv[5]);

    if (payload > MAX_PAYLOAD) payload = MAX_PAYLOAD;
    if (payload < 0) payload = 0;

    // LAYER 4: Create UDP socket (SOCK_DGRAM = UDP)
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // LAYER 4: Bind local socket to source port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Bind to any local interface
    local_addr.sin_port = htons(src_port);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    // LAYER 3: Configure destination address (Network layer - IP addressing)
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(dst_port);

    // inet_pton converts text IP address to binary (Layer 3)
    if (inet_pton(AF_INET, dst_addr, &remote_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid destination IP address: %s\n", dst_addr);
        close(sock);
        return 1;
    }

    printf("Sending %d UDP packets to %s:%d (from port %d)\n",
           count, dst_addr, dst_port, src_port);
    for (uint64_t i = 1; i <= (uint64_t)count; ++i) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        // LAYER 7: Build application-layer packet
        struct app_pkt pkt;
        memset(&pkt, 0, sizeof(pkt));

        pkt.seq = i;
        pkt.send_sec = htonll((uint64_t)ts.tv_sec);    // Network byte order (Layer 3)
        pkt.send_nsec = htonl((uint32_t)ts.tv_nsec);   // Network byte order (Layer 3)
        pkt.payload_len = htonl((uint32_t)payload);    // Network byte order (Layer 3)

        // LAYER 7: Fill payload with pattern
        for (int j = 0; j < payload; ++j) {
            pkt.payload[j] = (char)(i + j);
        }

        // Calculate total packet size (header + payload)
        size_t pkt_size = sizeof(struct app_pkt) - MAX_PAYLOAD + payload;

        // LAYER 4: Send via UDP socket (kernel handles UDP header creation)
        ssize_t sent = sendto(sock, &pkt, pkt_size, 0,
                              (struct sockaddr *)&remote_addr, sizeof(remote_addr));
        if (sent < 0) {
            perror("sendto");
            close(sock);
            return 1;
        }

        // human-readable timestamp
        struct tm tm;
        char timestr[64];
        if (localtime_r(&ts.tv_sec, &tm) != NULL) {
            strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);
        } else {
            snprintf(timestr, sizeof(timestr), "%ld", (long)ts.tv_sec);
        }
        printf("sent UDP seq=%lu bytes=%zd to %s:%d at %s.%09ld\n",
               (unsigned long)i, sent, dst_addr, dst_port, timestr, (long)ts.tv_nsec);
        usleep(100000); // 100ms between packets
    }

    close(sock);
    return 0;
}
