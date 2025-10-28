BPF_CLANG ?= clang
BPF_LLVM_STRIP ?= llvm-strip
LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf || echo "-I/usr/include")
LIBBPF_LDFLAGS := $(shell pkg-config --libs libbpf || echo "-lbpf -lelf -pthread")

all: xdp_filter_port.bpf.o af_xdp_receiver

xdp_filter_port.bpf.o: xdp_filter_port.bpf.c
	$(BPF_CLANG) -O2 -g -target bpf -c $< -o $@

af_xdp_receiver: af_xdp_receiver.c
	$(CC) -O2 -g -o $@ $< $(LIBBPF_CFLAGS) $(LIBBPF_LDFLAGS) -lxdp

clean:
	rm -f xdp_filter_port.bpf.o af_xdp_receiver
