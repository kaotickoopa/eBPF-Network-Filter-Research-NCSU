// tun_receiver.c - UDP Receiver using TUN Device
// Receives UDP packets through TUN device (skipping L1-2)
// Usage: sudo ./tun_receiver <tun_device>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>

#define MAX_PACKET 4096

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

int open_tun_device(const char *dev_name) {
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    
    if (fd < 0) {
        perror("open /dev/net/tun");
        return -1;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);
    
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("ioctl TUNSETIFF");
        close(fd);
        return -1;
    }
    
    return fd;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <tun_device>\n", argv[0]);
        fprintf(stderr, "Example: sudo ./tun_receiver tun0\n");
        return 1;
    }
    
    char *dev_name = argv[1];
    int tun_fd = open_tun_device(dev_name);
    
    if (tun_fd < 0) return 1;
    
    signal(SIGINT, signal_handler);
    
    printf("UDP Receiver via TUN Device\n");
    printf("  Device: %s\n", dev_name);
    printf("  Listening on 10.0.0.* port 9999\n");
    printf("  Press Ctrl+C to exit\n\n");
    
    int pkt_count = 0;
    
    while (running) {
        unsigned char packet[MAX_PACKET];
        ssize_t len = read(tun_fd, packet, sizeof(packet));
        
        if (len < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        
        if (len < sizeof(struct iphdr)) {
            continue;
        }
        
        struct iphdr *ip = (struct iphdr *)packet;
        struct udphdr *udp = NULL;
        unsigned char *payload = NULL;
        int payload_len = 0;
        
        // Parse IP header
        if (ip->protocol == IPPROTO_UDP && len > ip->ihl * 4 + 8) {
            udp = (struct udphdr *)(packet + ip->ihl * 4);
            payload = packet + ip->ihl * 4 + 8;
            payload_len = len - ip->ihl * 4 - 8;
        }
        
        struct in_addr src_addr, dst_addr;
        src_addr.s_addr = ip->saddr;
        dst_addr.s_addr = ip->daddr;
        
        printf("Packet %d:\n", ++pkt_count);
        printf("  From: %s\n", inet_ntoa(src_addr));
        printf("  To: %s\n", inet_ntoa(dst_addr));
        printf("  Total length: %zd bytes\n", len);
        
        if (udp) {
            printf("  UDP: %u → %u\n", ntohs(udp->source), ntohs(udp->dest));
            printf("  Payload: %d bytes\n", payload_len);
            
            // Display first bytes of payload as hex
            if (payload_len > 0) {
                printf("  Data (hex): ");
                int show = payload_len < 16 ? payload_len : 16;
                for (int i = 0; i < show; i++) {
                    printf("%02x ", payload[i]);
                }
                if (payload_len > show) printf("...");
                printf("\n");
            }
        }
        printf("\n");
    }
    
    close(tun_fd);
    printf("\nReceived %d packets total\n", pkt_count);
    return 0;
}
