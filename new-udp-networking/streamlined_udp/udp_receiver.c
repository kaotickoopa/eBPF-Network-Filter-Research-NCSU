// udp_receiver.c
// Simple UDP receiver that mirrors the AF_XDP receiver's behavior at the application
// level: listen on a UDP port, receive packets, print timestamp and packet length.

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

static volatile sig_atomic_t exiting = 0;
static void sigint_handler(int sig) { exiting = 1; }

#ifndef PORT_X
#define PORT_X 12345
#endif

#define RECV_BUF 65536

int main(int argc, char **argv)
{
    int port = PORT_X;
    if (argc >= 2) port = atoi(argv[1]);

    signal(SIGINT, sigint_handler);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }

    printf("UDP receiver listening on port %d\n", port);

    char *buf = malloc(RECV_BUF);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        close(fd);
        return 1;
    }

    while (!exiting) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        struct timespec ts;
        ssize_t r = recvfrom(fd, buf, RECV_BUF, 0, (struct sockaddr *)&src, &slen);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, ipbuf, sizeof(ipbuf));

        printf("recv: %zd bytes from %s:%d at %ld.%09ld\n",
               r, ipbuf, ntohs(src.sin_port), ts.tv_sec, ts.tv_nsec);

        // Optional: print first bytes as hex (up to 16)
        size_t show = r < 16 ? r : 16;
        printf("  data: ");
        for (size_t i = 0; i < show; ++i) printf("%02x ", (unsigned char)buf[i]);
        if (r > show) printf("... (total %zd bytes)", r);
        printf("\n");
    }

    printf("shutting down\n");
    free(buf);
    close(fd);
    return 0;
}
