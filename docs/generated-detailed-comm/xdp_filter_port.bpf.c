// xdp_filter_port.bpf.c
// Compile: clang -O2 -g -target bpf -c xdp_filter_port.bpf.c -o xdp_filter_port.bpf.o
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

char LICENSE[] SEC("license") = "GPL";

// XSK map: index = rx queue id, value = sock fd placeholder handled by kernel (xsk)
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} xsk_map SEC(".maps");

// Set your port here (network byte order check done below)
#ifndef PORT_X
#define PORT_X 12345
#endif

SEC("xdp_port_filter")
int xdp_port_filter_func(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // parse ethernet
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // only ipv4 for simplicity
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    struct iphdr *iph = (void *)eth + sizeof(*eth);
    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    if (iph->protocol != IPPROTO_UDP)
        return XDP_PASS;

    // UDP header (account for ip header length)
    __u32 ip_hdr_len = iph->ihl * 4;
    struct udphdr *udph = (void *)iph + ip_hdr_len;
    if ((void *)(udph + 1) > data_end)
        return XDP_PASS;

    __u16 dport = bpf_ntohs(udph->dest);
    if (dport == PORT_X) {
        // redirect to AF_XDP socket bound to this rx queue
        __u32 rxq = ctx->rx_queue_index; // available on modern kernels
        return bpf_redirect_map(&xsk_map, rxq, 0);
    }

    // Non-matching packets: drop to minimize kernel work for this experiment.
    // If you want kernel to still see them, return XDP_PASS instead.
    return XDP_DROP;
}
