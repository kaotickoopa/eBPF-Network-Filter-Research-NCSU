// router.c - Multi-Machine IP Router using TUN Devices
// Skips Layer 1 (Physical) and Layer 2 (Data Link) entirely
// Handles Layer 3 (IP) routing and Layer 4 (UDP/port) forwarding
// Scalable to many virtual machines with port forwarding support

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>

#define MAX_MACHINES 100
#define MAX_ROUTES 256
#define MAX_PACKET 4096

static volatile int running = 1;
void signal_handler(int sig) { running = 0; }

// Virtual Machine (TUN Device)
typedef struct {
    int fd;
    uint32_t ip_addr;
    char name[16];
    int active;
} VirtualMachine;

// Routing entry - supports multi-criteria routing
typedef struct {
    uint32_t src_ip;          // Source IP (0 = any)
    uint32_t dst_ip;          // Destination IP
    uint16_t dst_port;        // Destination port (0 = any)
    int output_vm_index;      // Output VM index
    uint16_t forward_to_port; // Port forwarding (0 = no change)
} Route;

VirtualMachine vms[MAX_MACHINES];
Route routes[MAX_ROUTES];
int num_machines = 0;
int num_routes = 0;

// ============================================================================
// IP/UDP Checksum Calculation
// ============================================================================

uint16_t calculate_ip_checksum(struct iphdr *ip) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)ip;
    int len = ip->ihl * 4;
    
    uint16_t check_save = ip->check;
    ip->check = 0;
    
    while (len > 1) {
        sum += ntohs(*ptr);
        ptr++;
        len -= 2;
    }
    
    if (len) {
        sum += (*(uint8_t *)ptr) << 8;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    ip->check = check_save;
    return htons(~sum);
}

uint16_t calculate_udp_checksum(struct iphdr *ip, struct udphdr *udp,
                                 unsigned char *payload, int payload_len) {
    // UDP checksum over pseudo-header + UDP header + payload
    uint32_t sum = 0;
    
    // Pseudo-header checksum
    uint16_t *ptr;
    uint16_t len = ntohs(udp->len);
    
    // Source IP
    ptr = (uint16_t *)&ip->saddr;
    sum += ntohs(ptr[0]) + ntohs(ptr[1]);
    
    // Destination IP
    ptr = (uint16_t *)&ip->daddr;
    sum += ntohs(ptr[0]) + ntohs(ptr[1]);
    
    // Protocol and length
    sum += (uint16_t)IPPROTO_UDP;
    sum += len;
    
    // UDP header and payload
    uint16_t *data = (uint16_t *)udp;
    int remaining = len;
    
    // Zero checksum field for calculation
    uint16_t check_save = udp->check;
    udp->check = 0;
    
    while (remaining > 1) {
        sum += ntohs(*data);
        data++;
        remaining -= 2;
    }
    
    if (remaining) {
        sum += (*(uint8_t *)data) << 8;
    }
    
    udp->check = check_save;
    
    // Fold carries
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    uint16_t result = htons(~sum);
    return result ? result : 0xFFFF;  // RFC: 0xFFFF = 0x0000
}

// ============================================================================
// TUN Device Setup
// ============================================================================

int open_tun_device(const char *dev_name) {
    struct ifreq ifr;
    int fd;
    
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/net/tun");
        return -1;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);
    
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("Failed to setup TUN device");
        close(fd);
        return -1;
    }
    
    return fd;
}

// ============================================================================
// Routing and Forwarding
// ============================================================================

int find_output_vm(struct iphdr *ip, struct udphdr *udp) {
    uint16_t dst_port = udp ? ntohs(udp->dest) : 0;
    
    for (int i = 0; i < num_routes; i++) {
        // Match source IP (0 = wildcard)
        if (routes[i].src_ip != 0 && routes[i].src_ip != ip->saddr) {
            continue;
        }
        
        // Match destination IP
        if (routes[i].dst_ip != ip->daddr) {
            continue;
        }
        
        // Match destination port (0 = wildcard)
        if (routes[i].dst_port != 0 && routes[i].dst_port != dst_port) {
            continue;
        }
        
        return routes[i].output_vm_index;
    }
    
    // Default route: forward based on destination IP only
    for (int i = 0; i < num_machines; i++) {
        if (vms[i].ip_addr == ip->daddr && vms[i].active) {
            return i;
        }
    }
    
    return -1;  // No route found
}

void forward_packet(unsigned char *packet, int len, int src_vm_idx, int dst_vm_idx) {
    struct iphdr *ip = (struct iphdr *)packet;
    struct udphdr *udp = NULL;
    
    if (ip->protocol == IPPROTO_UDP && len > ip->ihl * 4 + 8) {
        udp = (struct udphdr *)(packet + ip->ihl * 4);
    }
    
    // Check if we need to forward to the destination port
    for (int i = 0; i < num_routes; i++) {
        if (routes[i].output_vm_index == dst_vm_idx &&
            routes[i].dst_ip == ip->daddr) {
            
            // Port forwarding: modify destination port if specified
            if (routes[i].forward_to_port != 0 && udp != NULL) {
                uint16_t old_port = udp->dest;
                udp->dest = htons(routes[i].forward_to_port);
                
                // Recalculate UDP checksum if not zero
                if (udp->check != 0) {
                    int payload_len = ntohs(udp->len) - 8;
                    udp->check = calculate_udp_checksum(ip, udp,
                                    packet + ip->ihl * 4 + 8, payload_len);
                }
                
                printf("  [Port Forward] %u → %u\n", ntohs(old_port), 
                       routes[i].forward_to_port);
            }
            break;
        }
    }
    
    // Recalculate IP checksum (may have changed due to port forwarding)
    ip->check = calculate_ip_checksum(ip);
    
    // Write to output VM's TUN
    if (write(vms[dst_vm_idx].fd, packet, len) < 0) {
        perror("Failed to write to output TUN");
    }
}

void process_packet_from_vm(int vm_idx) {
    unsigned char packet[MAX_PACKET];
    ssize_t len = read(vms[vm_idx].fd, packet, sizeof(packet));
    
    if (len < sizeof(struct iphdr)) {
        fprintf(stderr, "Packet too short\n");
        return;
    }
    
    struct iphdr *ip = (struct iphdr *)packet;
    
    // Basic IP header validation
    if (ip->version != 4) {
        printf("Non-IPv4 packet (version=%d), dropping\n", ip->version);
        return;
    }
    
    if (ip->ihl < 5) {
        printf("Invalid IP header length\n");
        return;
    }
    
    struct udphdr *udp = NULL;
    if (ip->protocol == IPPROTO_UDP && len > ip->ihl * 4 + 8) {
        udp = (struct udphdr *)(packet + ip->ihl * 4);
    }
    
    // Log packet
    struct in_addr src_addr, dst_addr;
    src_addr.s_addr = ip->saddr;
    dst_addr.s_addr = ip->daddr;
    
    printf("[%s] RX: %s → %s (len=%zd, proto=%d",
           vms[vm_idx].name, inet_ntoa(src_addr), inet_ntoa(dst_addr),
           len, ip->protocol);
    
    if (udp) {
        printf(", UDP %u→%u)", ntohs(udp->source), ntohs(udp->dest));
    } else {
        printf(")");
    }
    printf("\n");
    
    // Find output VM
    int output_vm = find_output_vm(ip, udp);
    
    if (output_vm >= 0 && output_vm != vm_idx) {
        printf("  ✓ Forward to %s\n", vms[output_vm].name);
        forward_packet(packet, len, vm_idx, output_vm);
    } else if (output_vm < 0) {
        printf("  ✗ No route found\n");
    } else if (output_vm == vm_idx) {
        printf("  ✗ Would create loop\n");
    }
}

// ============================================================================
// Routing Table Configuration
// ============================================================================

void add_route(uint32_t src_ip, uint32_t dst_ip, uint16_t dst_port,
               int output_vm_idx, uint16_t forward_to_port) {
    if (num_routes >= MAX_ROUTES) {
        fprintf(stderr, "Routing table full\n");
        return;
    }
    
    routes[num_routes].src_ip = src_ip;
    routes[num_routes].dst_ip = dst_ip;
    routes[num_routes].dst_port = dst_port;
    routes[num_routes].output_vm_index = output_vm_idx;
    routes[num_routes].forward_to_port = forward_to_port;
    
    num_routes++;
}

// ============================================================================
// Virtual Machine Setup
// ============================================================================

int add_virtual_machine(const char *name, const char *ip_str) {
    if (num_machines >= MAX_MACHINES) {
        fprintf(stderr, "Max machines reached\n");
        return -1;
    }
    
    int fd = open_tun_device(name);
    if (fd < 0) {
        return -1;
    }
    
    vms[num_machines].fd = fd;
    vms[num_machines].ip_addr = inet_addr(ip_str);
    strcpy(vms[num_machines].name, name);
    vms[num_machines].active = 1;
    
    printf("✓ Created machine %d: %s (%s)\n", num_machines, name, ip_str);
    
    return num_machines++;
}

// ============================================================================
// Main Routing Loop
// ============================================================================

void run_router() {
    fd_set readfds;
    int max_fd = 0;
    
    for (int i = 0; i < num_machines; i++) {
        if (vms[i].fd > max_fd) {
            max_fd = vms[i].fd;
        }
    }
    
    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("IP Router (L3-4 only - L1-2 SKIPPED)\n");
    printf("Machines: %d, Routes: %d\n", num_machines, num_routes);
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    while (running) {
        FD_ZERO(&readfds);
        
        for (int i = 0; i < num_machines; i++) {
            if (vms[i].active) {
                FD_SET(vms[i].fd, &readfds);
            }
        }
        
        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        
        for (int i = 0; i < num_machines; i++) {
            if (vms[i].active && FD_ISSET(vms[i].fd, &readfds)) {
                process_packet_from_vm(i);
            }
        }
    }
}

// ============================================================================
// Configuration & Main
// ============================================================================

int main(int argc, char **argv) {
    signal(SIGINT, signal_handler);
    
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  Multi-Machine UDP Router (TUN Device Based)              ║\n");
    printf("║  Skips Layer 1 (Physical) and Layer 2 (Data Link)         ║\n");
    printf("║  Implements Layer 3 (IP) routing and Layer 4 (UDP)        ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    // Create virtual machines
    // Format: N machines on 10.0.0.{1..N}
    int num_vms = 3;
    if (argc > 1) {
        num_vms = atoi(argv[1]);
    }
    
    if (num_vms < 2 || num_vms > MAX_MACHINES) {
        fprintf(stderr, "Usage: %s [num_machines]\n", argv[0]);
        fprintf(stderr, "num_machines: 2-%d (default: 3)\n", MAX_MACHINES);
        return 1;
    }
    
    printf("Setting up %d virtual machines...\n\n", num_vms);
    
    for (int i = 0; i < num_vms; i++) {
        char name[16];
        char ip_str[32];
        
        snprintf(name, sizeof(name), "tun%d", i);
        snprintf(ip_str, sizeof(ip_str), "10.0.0.%d", i + 1);
        
        if (add_virtual_machine(name, ip_str) < 0) {
            fprintf(stderr, "Failed to create machine %d\n", i);
            return 1;
        }
    }
    
    // Configure routing
    printf("\nConfiguring routing...\n\n");
    
    // Default routes: each machine reaches others via their IP
    for (int i = 0; i < num_vms; i++) {
        for (int j = 0; j < num_vms; j++) {
            if (i != j) {
                char ip_str[32];
                snprintf(ip_str, sizeof(ip_str), "10.0.0.%d", j + 1);
                add_route(0, inet_addr(ip_str), 0, j, 0);
            }
        }
    }
    
    printf("✓ Configured %d routes\n", num_routes);
    printf("\nNote: Run setup.sh first to configure TUN devices:\n");
    printf("  sudo ./setup.sh %d\n\n", num_vms);
    
    // Start routing
    run_router();
    
    // Cleanup
    printf("\nShutting down...\n");
    for (int i = 0; i < num_machines; i++) {
        if (vms[i].fd >= 0) {
            close(vms[i].fd);
        }
    }
    
    return 0;
}
