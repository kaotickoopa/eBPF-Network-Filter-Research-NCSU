// tun_sender.c - UDP Sender using TUN Device
// Sends UDP packets through TUN device (skipping L1-2)
// Usage: sudo ./tun_sender <tun_device> <dst_ip> <count> [payload_size]

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
#include <stdint.h>

#define MAX_PACKET 4096
#define DEFAULT_PAYLOAD 32

static uint16_t ip_id = 0;

// Simple IP checksum
uint16_t calculate_ip_checksum(struct iphdr *ip) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)ip;
    int len = ip->ihl * 4;
    
    ip->check = 0;
    
    while (len > 1) {
        sum += ntohs(*ptr);
        ptr++;
        len -= 2;
    }
    
    if (len) sum += (*(uint8_t *)ptr) << 8;
    
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    
    return htons(~sum);
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
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <tun_device> <dst_ip> <count> [payload_size]\n", argv[0]);
        fprintf(stderr, "Example: sudo ./tun_sender tun0 10.0.0.2 5 64\n");
        return 1;
    }
    
    char *dev_name = argv[1];
    uint32_t dst_ip = inet_addr(argv[2]);
    int count = atoi(argv[3]);
    int payload_size = argc > 4 ? atoi(argv[4]) : DEFAULT_PAYLOAD;
    
    if (payload_size < 0 || payload_size > (MAX_PACKET - 28)) {
        fprintf(stderr, "Payload size must be 0-%d\n", MAX_PACKET - 28);
        return 1;
    }
    
    int tun_fd = open_tun_device(dev_name);
    if (tun_fd < 0) return 1;
    
    printf("UDP Sender via TUN Device\n");
    printf("  Device: %s\n", dev_name);
    printf("  Destination: %s\n", argv[2]);
    printf("  Packets: %d\n", count);
    printf("  Payload: %d bytes\n\n", payload_size);
    
    for (int i = 1; i <= count; i++) {
        unsigned char packet[MAX_PACKET];
        struct iphdr *ip = (struct iphdr *)packet;
        struct udphdr *udp = (struct udphdr *)(packet + 20);
        
        // Build IP header
        ip->version = 4;
        ip->ihl = 5;
        ip->tos = 0;
        ip->tot_len = htons(20 + 8 + payload_size);
        ip->id = htons(ip_id++);
        ip->frag_off = 0;
        ip->ttl = 64;
        ip->protocol = IPPROTO_UDP;
        ip->saddr = inet_addr("10.0.0.1");
        ip->daddr = dst_ip;
        ip->check = calculate_ip_checksum(ip);
        
        // Build UDP header
        udp->source = htons(5555);
        udp->dest = htons(9999);
        udp->len = htons(8 + payload_size);
        udp->check = 0;
        
        // Fill payload with pattern
        unsigned char *payload = packet + 28;
        for (int j = 0; j < payload_size; j++) {
            payload[j] = (unsigned char)(i + j);
        }
        
        int total_len = 20 + 8 + payload_size;
        
        if (write(tun_fd, packet, total_len) < 0) {
            perror("write");
            close(tun_fd);
            return 1;
        }
        
        printf("Sent packet %d (%d bytes total)\n", i, total_len);
        usleep(100000);  // 100ms between packets
    }
    
    close(tun_fd);
    printf("\nDone!\n");
    return 0;
}
