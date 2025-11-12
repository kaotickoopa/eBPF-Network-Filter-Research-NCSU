// normal_networking/udp_receiver.c
// UDP receiver that parses a binary header (seq, sec, nsec) from the sender
// and computes one-way latency using CLOCK_REALTIME (requires clock sync).

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <endian.h>

static volatile sig_atomic_t exiting = 0;
static void sigint_handler(int sig) { exiting = 1; }

#ifndef PORT_X
#define PORT_X 12345
#endif

#define RECV_BUF 65536

struct pkt_hdr {
    uint64_t seq;
    uint64_t sec;
    uint64_t nsec;
};

int main(int argc, char **argv)
{
    int port = PORT_X;
    if (argc >= 2) port = atoi(argv[1]);

    signal(SIGINT, sigint_handler);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { perror("bind"); close(fd); return 1; }

    printf("normal UDP receiver listening on port %d\n", port);

    char *buf = malloc(RECV_BUF);
    if (!buf) { fprintf(stderr, "malloc failed\n"); close(fd); return 1; }

    while (!exiting) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        ssize_t r = recvfrom(fd, buf, RECV_BUF, 0, (struct sockaddr *)&src, &slen);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        struct timespec rts;
        clock_gettime(CLOCK_REALTIME, &rts);

        if (r >= (ssize_t)sizeof(struct pkt_hdr)) {
            struct pkt_hdr hdr;
            memcpy(&hdr, buf, sizeof(hdr));
            uint64_t seq = be64toh(hdr.seq);
            uint64_t ssec = be64toh(hdr.sec);
            uint64_t snsec = be64toh(hdr.nsec);

            long sec_diff = (long)rts.tv_sec - (long)ssec;
            long nsec_diff = (long)rts.tv_nsec - (long)snsec;
            long usec = sec_diff * 1000000L + nsec_diff / 1000L;

            char ipbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src.sin_addr, ipbuf, sizeof(ipbuf));

            printf("recv seq=%lu from %s:%d size=%zd latency=%ld us\n",
                   (unsigned long)seq, ipbuf, ntohs(src.sin_port), r, usec);
        } else {
            printf("recv short packet size=%zd\n", r);
        }
    }

    free(buf);
    close(fd);
    return 0;
}
