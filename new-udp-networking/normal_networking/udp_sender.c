// normal_networking/udp_sender.c
// UDP sender that embeds a binary header (seq, sec, nsec) at packet start.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <endian.h>

struct pkt_hdr {
    uint64_t seq;
    uint64_t sec;
    uint64_t nsec;
};

int main(int argc, char **argv)
{
    const char *dst = "127.0.0.1";
    int port = 12345;
    int count = 100;
    int payload = 32;

    if (argc >= 2) dst = argv[1];
    if (argc >= 3) port = atoi(argv[2]);
    if (argc >= 4) count = atoi(argv[3]);
    if (argc >= 5) payload = atoi(argv[4]);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, dst, &sa.sin_addr) != 1) { fprintf(stderr, "bad dst\n"); close(fd); return 1; }

    size_t pkt_size = sizeof(struct pkt_hdr) + payload;
    char *pkt = malloc(pkt_size);
    if (!pkt) { fprintf(stderr, "malloc failed\n"); close(fd); return 1; }

    printf("normal UDP sender -> %s:%d, count=%d payload=%d\n", dst, port, count, payload);

    for (uint64_t i = 1; i <= (uint64_t)count; ++i) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        struct pkt_hdr hdr;
        hdr.seq = htobe64(i);
        hdr.sec = htobe64((uint64_t)ts.tv_sec);
        hdr.nsec = htobe64((uint64_t)ts.tv_nsec);
        memcpy(pkt, &hdr, sizeof(hdr));

        for (int j = 0; j < payload; ++j) pkt[sizeof(hdr) + j] = (char)((i + j) & 0xff);

        ssize_t s = sendto(fd, pkt, pkt_size, 0, (struct sockaddr *)&sa, sizeof(sa));
        if (s < 0) { perror("sendto"); break; }

        /* human timestamp */
        struct tm tm;
        char timestr[64];
        if (localtime_r(&ts.tv_sec, &tm) != NULL) {
            strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);
        } else snprintf(timestr, sizeof(timestr), "%ld", (long)ts.tv_sec);

        printf("sent seq=%lu bytes=%zd at %s.%09ld\n", (unsigned long)i, s, timestr, (long)ts.tv_nsec);
        usleep(10000); // small gap (10ms)
    }

    free(pkt);
    close(fd);
    return 0;
}
