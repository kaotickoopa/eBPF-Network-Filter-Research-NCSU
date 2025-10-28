// af_xdp_receiver.c
// Build: see Makefile. Link with -lbpf -lxdp -lelf -pthread (Makefile handles it)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <xdp/libxdp.h> // for xdp helpers (optional, we use raw syscalls here minimally)

/* Minimal AF_XDP includes */
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <xsk.h> // from libxdp (if available). If not installed, you'll need to adapt.

static volatile bool exiting = false;
static void sigint_handler(int sig) { exiting = true; }

#define IFNAME "eth0"   // change to your interface
#define PORT_X 12345    // must match BPF program or override when compiling BPF
#define NUM_DESCS 4096
#define FRAME_SIZE 2048
#define UMEM_SIZE (NUM_DESCS * FRAME_SIZE)

int main(int argc, char **argv)
{
    const char *ifname = IFNAME;
    if (argc >= 2) ifname = argv[1];

    signal(SIGINT, sigint_handler);

    // 1) Open and load BPF object
    struct bpf_object *obj;
    struct bpf_program *prog;
    int prog_fd;
    int err;

    obj = bpf_object__open_file("xdp_filter_port.bpf.o", NULL);
    if (!obj) {
        fprintf(stderr, "failed to open BPF object\n");
        return 1;
    }

    // Optionally set re-used PORT_X via global var map (skipped for brevity).
    if (bpf_object__load(obj)) {
        fprintf(stderr, "failed to load BPF object\n");
        return 1;
    }

    prog = bpf_object__find_program_by_title(obj, "xdp_port_filter");
    if (!prog) prog = bpf_object__next_program(obj, NULL);
    prog_fd = bpf_program__fd(prog);
    if (prog_fd < 0) {
        fprintf(stderr, "failed to get prog fd\n");
        return 1;
    }

    // Retrieve xsk_map fd
    int xsk_map_fd = bpf_object__find_map_fd_by_name(obj, "xsk_map");
    if (xsk_map_fd < 0) {
        fprintf(stderr, "failed to find xsk_map in object\n");
        return 1;
    }

    // 2) Attach XDP program to interface
    int ifindex = if_nametoindex(ifname);
    if (!ifindex) {
        perror("if_nametoindex");
        return 1;
    }

    // Attach with generic xdp attach via libbpf helper
    err = bpf_set_link_xdp_fd(ifindex, prog_fd, 0);
    if (err) {
        fprintf(stderr, "failed to attach xdp program to %s: %s\n", ifname, strerror(-err));
        return 1;
    }
    printf("Attached XDP program to %s\n", ifname);

    // 3) Create UMEM and XSK socket (using libxdp's simpler helpers would help).
    // For brevity and portability, we'll use libxdp's xsk helper if available.
    // Here we use libxdp's xsk_* APIs from 'libxdp' (which must be installed).
    //
    // If libxdp isn't available, swap to the tutorial's example or libbpf's example.

    struct xsk_socket_info *xsk = NULL;
    struct xsk_umem_info *umem = NULL;

    // Try to use libxdp helper functions
    // -- Create UMEM
    err = xsk_configure_umem(&umem, UMEM_SIZE, FRAME_SIZE);
    if (err) {
        fprintf(stderr, "UMEM configure failed (is libxdp installed?) err=%d\n", err);
        goto cleanup_detach;
    }

    // -- Create XSK socket bound to queue 0
    int queue_id = 0;
    err = xsk_configure_socket(ifname, ifindex, queue_id, umem, &xsk);
    if (err) {
        fprintf(stderr, "xsk_configure_socket failed: %d\n", err);
        goto cleanup_umem;
    }

    // 4) Put socket FD into the xsk_map at key == queue_id
    int sock_fd = xsk_socket__fd(xsk->xsk_socket); // libxdp helper to get sock fd
    if (sock_fd < 0) {
        fprintf(stderr, "unable to get socket fd from xsk\n");
        goto cleanup_socket;
    }

    __u32 key = queue_id;
    __u32 value = 0; // for XSKMAP the kernel uses internal sock storage; we still need to update with the fd
    // Use bpf_map_update_elem with the socket fd via BPF map API is performed by 'setsockopt' in kernel,
    // but the typical sequence is xsk_map_update via libbpf helper that takes xsk socket. For simplicity:
    err = bpf_map_update_elem(xsk_map_fd, &key, &sock_fd, 0);
    if (err) {
        fprintf(stderr, "bpf_map_update_elem failed: %s\n", strerror(errno));
        goto cleanup_socket;
    }
    printf("Inserted xsk socket fd %d into xsk_map at key %u\n", sock_fd, key);

    // 5) Poll for packets and print timestamp & pkt len
    struct pollfd pfd = {
        .fd = sock_fd,
        .events = POLLIN,
    };

    printf("Receiving packets for port %d (ctrl-c to stop)...\n", PORT_X);
    while (!exiting) {
        int pret = poll(&pfd, 1, 1000);
        if (pret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        } else if (pret == 0) continue;

        if (pfd.revents & POLLIN) {
            // simple receive - we use libxdp xsk ring helpers here; if not present,
            // use recvfrom on the socket as a fallback (but recvfrom returns raw frames only if kernel supports)
            struct xsk_ring_cons *rx = &xsk->rx;
            unsigned int rcv;
            unsigned int idx;
            rcv = xsk_ring_cons__peek(rx, 32, &idx);
            if (!rcv) continue;

            for (unsigned int i = 0; i < rcv; ++i) {
                u64 addr = xsk_ring_cons__rx_desc(rx, idx + i)->addr;
                u32 len = xsk_ring_cons__rx_desc(rx, idx + i)->len;
                void *pkt = xsk_umem__get_data(umem->buffer, addr);
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                printf("pkt: len=%u ts=%ld.%09ld\n", len, ts.tv_sec, ts.tv_nsec);

                // Here you can parse headers in pkt to extract UDP payload and timestamps
                // For example, skip ethernet + ipv4 + udp headers to the payload.
            }

            xsk_ring_cons__release(rx, rcv);
            // Refill fill ring / kick if necessary (libxdp typically does this).
        }
    }

    printf("Exiting...\n");

    // cleanup sequence
    bpf_map_delete_elem(xsk_map_fd, &key);

cleanup_socket:
    xsk_close(xsk);
cleanup_umem:
    xsk_umem__delete(umem);
cleanup_detach:
    bpf_set_link_xdp_fd(ifindex, -1, 0);
    printf("Detached XDP program\n");
    bpf_object__close(obj);
    return 0;
}
