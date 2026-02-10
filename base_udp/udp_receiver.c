// udp_receiver.c (Multi-machine UDP socket version)
// Uses actual UDP sockets traversing OSI layers 3-7 only.
// Layer 3 (Network): IP addressing with bind
// Layer 4 (Transport): UDP protocol via socket/recvfrom
// Layers 5-7: Application data parsing

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

static volatile sig_atomic_t exiting = 0;
static void sigint_handler(int sig) { exiting = 1; }

#define DEFAULT_LISTEN_ADDR "0.0.0.0"
#define DEFAULT_LISTEN_PORT 9999
#define MAX_PAYLOAD 1472

// Application-layer packet structure (Layer 7)
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
    signal(SIGINT, sigint_handler);

    int listen_port = DEFAULT_LISTEN_PORT;
    const char *listen_addr = DEFAULT_LISTEN_ADDR;

    // Parse command line: [listen_port] [listen_addr]
    if (argc >= 2) listen_port = atoi(argv[1]);
    if (argc >= 3) listen_addr = argv[2];

    // LAYER 4: Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Allow socket address reuse
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(sock);
        return 1;
    }

    // LAYER 3: Bind to listen address (Network layer - IP addressing)
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(listen_port);

    // inet_pton converts text IP address to binary (Layer 3)
    if (inet_pton(AF_INET, listen_addr, &local_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid listen IP address: %s\n", listen_addr);
        close(sock);
        return 1;
    }

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    printf("UDP receiver listening on %s:%d\n", listen_addr, listen_port);

    while (!exiting) {
        // LAYER 4: Receive UDP packet
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);

        struct app_pkt pkt;
        ssize_t recv_len = recvfrom(sock, &pkt, sizeof(struct app_pkt), 0,
                                     (struct sockaddr *)&src_addr, &src_len);
        if (recv_len < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        // LAYER 7: Parse application-layer packet
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        // Convert from network byte order (Layer 3)
        uint64_t send_sec = ntohll(pkt.send_sec);
        uint32_t send_nsec = ntohl(pkt.send_nsec);
        uint32_t payload_len = ntohl(pkt.payload_len);

        // Get source IP and port from sockaddr (Layer 3/4)
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src_addr.sin_addr, src_ip, INET_ADDRSTRLEN);
        uint16_t src_port = ntohs(src_addr.sin_port);

        printf("recv UDP: seq=%lu from %s:%u bytes=%zd at %ld.%09ld\n",
               pkt.seq, src_ip, src_port, recv_len, ts.tv_sec, ts.tv_nsec);

        // Print first bytes as hex (up to 16)
        size_t show = payload_len < 16 ? payload_len : 16;
        printf("  data: ");
        for (size_t i = 0; i < show; ++i) {
            printf("%02x ", (unsigned char)pkt.payload[i]);
        }
        if (payload_len > show) printf("... (total %u bytes)", payload_len);
        printf("\n");
    }

    printf("shutting down\n");
    close(sock);
    return 0;
}
