# XDP (eXpress Data Path) - Detailed Technical Overview

## Introduction

**XDP (eXpress Data Path)** is a high-performance, programmable network data path in the Linux kernel that enables packet processing at the earliest possible point - right after packets arrive from the NIC (Network Interface Card) but before they enter the traditional kernel network stack. XDP leverages eBPF (extended Berkeley Packet Filter) to provide a safe, efficient, and flexible framework for packet processing.

## Key Concepts

### What is XDP?

XDP is a kernel hook that allows eBPF programs to run on network packets as soon as they're received from the hardware. It operates at layer 2 (data link layer) and can:
- Drop packets
- Pass packets to the network stack
- Redirect packets to other interfaces
- Modify packet contents
- Transmit packets back out the same interface

### XDP Operation Modes

1. **Native/Driver Mode**: XDP program runs in the driver itself (highest performance)
2. **Offloaded Mode**: XDP program runs on the NIC hardware (if supported)
3. **Generic Mode**: XDP program runs in the kernel network stack (lowest performance, compatibility mode)

### XDP Actions

An XDP program must return one of these actions:
- `XDP_DROP`: Drop the packet immediately
- `XDP_PASS`: Pass packet to normal network stack
- `XDP_TX`: Transmit packet back out the same interface
- `XDP_REDIRECT`: Redirect packet to another interface or CPU
- `XDP_ABORTED`: Drop packet and trigger tracepoint (error condition)

## Architecture

```
┌─────────────────────────────────────────┐
│         User Space Application          │
└─────────────────┬───────────────────────┘
                  │
        ┌─────────▼──────────┐
        │   BPF Compiler      │
        │   (clang/LLVM)      │
        └─────────┬───────────┘
                  │
        ┌─────────▼──────────┐
        │   BPF Bytecode      │
        └─────────┬───────────┘
                  │
        ┌─────────▼──────────┐
        │   BPF Verifier      │
        │  (Kernel Security)  │
        └─────────┬───────────┘
                  │
┌─────────────────▼───────────────────────┐
│            XDP Hook Point                │
│  ┌────────────────────────────────────┐ │
│  │        Network Driver              │ │
│  └────────────┬───────────────────────┘ │
│               │                          │
│  ┌────────────▼───────────────────────┐ │
│  │      XDP BPF Program               │ │
│  │  (Drop/Pass/TX/Redirect/Abort)     │ │
│  └────────────┬───────────────────────┘ │
└───────────────┼──────────────────────────┘
                │
     ┌──────────┼──────────┐
     │          │          │
  DROP      PASS/TX    REDIRECT
```

## Use Cases

1. **DDoS Mitigation**: Drop malicious packets at line rate
2. **Load Balancing**: Distribute traffic across backend servers
3. **Firewall**: High-performance packet filtering
4. **Traffic Monitoring**: Sampling and statistics collection
5. **NAT**: Network address translation at the kernel edge
6. **Packet Modification**: Header rewriting, encapsulation

---

## C/C++ Programming Examples

### Example 1: Simple Packet Drop Filter (XDP Program)

```c
// xdp_drop_tcp.c
// Compile: clang -O2 -target bpf -c xdp_drop_tcp.c -o xdp_drop_tcp.o

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>

// BPF map to track dropped packets
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} drop_count SEC(".maps");

SEC("xdp")
int xdp_drop_tcp_port(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    
    // Parse Ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    
    // Check if IP packet
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;
    
    // Parse IP header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    
    // Check if TCP packet
    if (ip->protocol != IPPROTO_TCP)
        return XDP_PASS;
    
    // Parse TCP header
    struct tcphdr *tcp = (void *)ip + (ip->ihl * 4);
    if ((void *)(tcp + 1) > data_end)
        return XDP_PASS;
    
    // Drop packets to port 80 (HTTP)
    if (tcp->dest == __constant_htons(80)) {
        __u32 key = 0;
        __u64 *count = bpf_map_lookup_elem(&drop_count, &key);
        if (count)
            __sync_fetch_and_add(count, 1);
        return XDP_DROP;
    }
    
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
```

### Example 2: XDP Loader (User Space C++)

```cpp
// xdp_loader.cpp
// Compile: g++ -o xdp_loader xdp_loader.cpp -lbpf

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <net/if.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

class XDPLoader {
private:
    struct bpf_object *obj;
    struct bpf_program *prog;
    int prog_fd;
    int ifindex;
    
public:
    XDPLoader(const char *filename, const char *prog_name, const char *ifname) 
        : obj(nullptr), prog(nullptr), prog_fd(-1) {
        
        // Get interface index
        ifindex = if_nametoindex(ifname);
        if (ifindex == 0) {
            throw std::runtime_error("Failed to get interface index");
        }
        
        // Open BPF object file
        obj = bpf_object__open_file(filename, nullptr);
        if (!obj) {
            throw std::runtime_error("Failed to open BPF object file");
        }
        
        // Find program
        prog = bpf_object__find_program_by_name(obj, prog_name);
        if (!prog) {
            bpf_object__close(obj);
            throw std::runtime_error("Failed to find BPF program");
        }
        
        // Set program type
        bpf_program__set_type(prog, BPF_PROG_TYPE_XDP);
        
        // Load BPF object
        if (bpf_object__load(obj) != 0) {
            bpf_object__close(obj);
            throw std::runtime_error("Failed to load BPF object");
        }
        
        prog_fd = bpf_program__fd(prog);
        if (prog_fd < 0) {
            bpf_object__close(obj);
            throw std::runtime_error("Failed to get program fd");
        }
    }
    
    bool attach(uint32_t flags = XDP_FLAGS_UPDATE_IF_NOEXIST) {
        int ret = bpf_xdp_attach(ifindex, prog_fd, flags, nullptr);
        if (ret < 0) {
            std::cerr << "Failed to attach XDP program: " << strerror(-ret) << std::endl;
            return false;
        }
        std::cout << "XDP program attached successfully" << std::endl;
        return true;
    }
    
    bool detach() {
        int ret = bpf_xdp_detach(ifindex, 0, nullptr);
        if (ret < 0) {
            std::cerr << "Failed to detach XDP program: " << strerror(-ret) << std::endl;
            return false;
        }
        std::cout << "XDP program detached successfully" << std::endl;
        return true;
    }
    
    int get_prog_fd() const { return prog_fd; }
    int get_ifindex() const { return ifindex; }
    
    ~XDPLoader() {
        if (obj) {
            bpf_object__close(obj);
        }
    }
};

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <bpf_file> <prog_name> <interface>" << std::endl;
        return 1;
    }
    
    try {
        XDPLoader loader(argv[1], argv[2], argv[3]);
        
        if (!loader.attach(XDP_FLAGS_DRV_MODE)) {
            std::cerr << "Failed to attach in driver mode, trying SKB mode..." << std::endl;
            if (!loader.attach(XDP_FLAGS_SKB_MODE)) {
                return 1;
            }
        }
        
        std::cout << "Press Enter to detach and exit..." << std::endl;
        std::cin.get();
        
        loader.detach();
        
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### Example 3: XDP Packet Counter with Statistics

```c
// xdp_stats.c
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>

struct packet_stats {
    __u64 total_packets;
    __u64 total_bytes;
    __u64 tcp_packets;
    __u64 udp_packets;
    __u64 icmp_packets;
    __u64 other_packets;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct packet_stats);
} stats_map SEC(".maps");

SEC("xdp")
int xdp_stats_func(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    __u32 key = 0;
    
    struct packet_stats *stats = bpf_map_lookup_elem(&stats_map, &key);
    if (!stats)
        return XDP_PASS;
    
    // Calculate packet size
    __u64 bytes = data_end - data;
    __sync_fetch_and_add(&stats->total_packets, 1);
    __sync_fetch_and_add(&stats->total_bytes, bytes);
    
    // Parse Ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;
    
    // Parse IP header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    
    // Count by protocol
    switch (ip->protocol) {
        case IPPROTO_TCP:
            __sync_fetch_and_add(&stats->tcp_packets, 1);
            break;
        case IPPROTO_UDP:
            __sync_fetch_and_add(&stats->udp_packets, 1);
            break;
        case IPPROTO_ICMP:
            __sync_fetch_and_add(&stats->icmp_packets, 1);
            break;
        default:
            __sync_fetch_and_add(&stats->other_packets, 1);
            break;
    }
    
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
```

---

## Rust Programming Examples

### Example 1: XDP Program in Rust (using Aya)

```rust
// xdp-filter/src/main.rs
#![no_std]
#![no_main]

use aya_ebpf::{
    bindings::xdp_action,
    macros::{map, xdp},
    maps::Array,
    programs::XdpContext,
};
use aya_log_ebpf::info;
use core::mem;
use network_types::{
    eth::{EthHdr, EtherType},
    ip::{IpProto, Ipv4Hdr},
    tcp::TcpHdr,
};

#[repr(C)]
pub struct PacketStats {
    pub dropped: u64,
    pub passed: u64,
}

#[map]
static STATS: Array<PacketStats> = Array::with_max_entries(1, 0);

#[xdp]
pub fn xdp_filter(ctx: XdpContext) -> u32 {
    match try_xdp_filter(ctx) {
        Ok(ret) => ret,
        Err(_) => xdp_action::XDP_ABORTED,
    }
}

fn try_xdp_filter(ctx: XdpContext) -> Result<u32, ()> {
    // Parse Ethernet header
    let ethhdr: *const EthHdr = unsafe { ptr_at(&ctx, 0)? };
    
    match unsafe { (*ethhdr).ether_type } {
        EtherType::Ipv4 => {}
        _ => return Ok(xdp_action::XDP_PASS),
    }
    
    // Parse IPv4 header
    let ipv4hdr: *const Ipv4Hdr = unsafe { ptr_at(&ctx, EthHdr::LEN)? };
    
    match unsafe { (*ipv4hdr).proto } {
        IpProto::Tcp => {}
        _ => return Ok(xdp_action::XDP_PASS),
    }
    
    // Parse TCP header
    let tcphdr: *const TcpHdr = unsafe {
        ptr_at(&ctx, EthHdr::LEN + Ipv4Hdr::LEN)?
    };
    
    let dest_port = u16::from_be(unsafe { (*tcphdr).dest });
    
    // Drop SSH traffic (port 22) as an example
    if dest_port == 22 {
        if let Some(stats) = STATS.get_ptr_mut(0) {
            unsafe {
                (*stats).dropped += 1;
            }
        }
        info!(&ctx, "Dropped SSH packet to port 22");
        return Ok(xdp_action::XDP_DROP);
    }
    
    if let Some(stats) = STATS.get_ptr_mut(0) {
        unsafe {
            (*stats).passed += 1;
        }
    }
    
    Ok(xdp_action::XDP_PASS)
}

#[inline(always)]
unsafe fn ptr_at<T>(ctx: &XdpContext, offset: usize) -> Result<*const T, ()> {
    let start = ctx.data();
    let end = ctx.data_end();
    let len = mem::size_of::<T>();
    
    if start + offset + len > end {
        return Err(());
    }
    
    Ok((start + offset) as *const T)
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { core::hint::unreachable_unchecked() }
}
```

### Example 2: XDP Loader in Rust

```rust
// xdp-loader/src/main.rs
use anyhow::{Context, Result};
use aya::{
    maps::Array,
    programs::{Xdp, XdpFlags},
    Ebpf,
};
use clap::Parser;
use std::time::Duration;
use tokio::signal;

#[derive(Debug, Parser)]
struct Opt {
    /// Network interface to attach XDP program to
    #[clap(short, long)]
    iface: String,
    
    /// Path to the eBPF object file
    #[clap(short, long, default_value = "target/bpf/xdp-filter")]
    path: String,
}

#[repr(C)]
struct PacketStats {
    dropped: u64,
    passed: u64,
}

#[tokio::main]
async fn main() -> Result<()> {
    let opt = Opt::parse();
    
    env_logger::init();
    
    // Load eBPF program
    let mut bpf = Ebpf::load_file(&opt.path)
        .context("Failed to load eBPF program")?;
    
    // Attach XDP program to interface
    let program: &mut Xdp = bpf.program_mut("xdp_filter")
        .context("Failed to find xdp_filter program")?
        .try_into()?;
    
    program.load()?;
    program.attach(&opt.iface, XdpFlags::default())
        .context("Failed to attach XDP program")?;
    
    println!("XDP program attached to interface: {}", opt.iface);
    println!("Press Ctrl-C to detach and exit");
    
    // Get stats map
    let stats_map: Array<_, PacketStats> = 
        bpf.map("STATS")
            .context("Failed to get STATS map")?
            .try_into()?;
    
    // Periodically print statistics
    let stats_task = tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(1));
        
        loop {
            interval.tick().await;
            
            if let Ok(stats) = stats_map.get(&0, 0) {
                println!(
                    "Stats - Passed: {}, Dropped: {}",
                    stats.passed, stats.dropped
                );
            }
        }
    });
    
    // Wait for Ctrl-C
    signal::ctrl_c().await?;
    
    println!("\nDetaching XDP program...");
    stats_task.abort();
    
    Ok(())
}
```

### Example 3: Advanced XDP Load Balancer in Rust

```rust
// xdp-lb/src/main.rs (eBPF program)
#![no_std]
#![no_main]

use aya_ebpf::{
    bindings::xdp_action,
    macros::{map, xdp},
    maps::{HashMap, Array},
    programs::XdpContext,
};
use core::mem;
use network_types::{
    eth::{EthHdr, EtherType},
    ip::{IpProto, Ipv4Hdr},
    tcp::TcpHdr,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Backend {
    pub ip: u32,
    pub mac: [u8; 6],
}

// Map to store backend servers
#[map]
static BACKENDS: Array<Backend> = Array::with_max_entries(16, 0);

// Connection tracking map
#[map]
static CONN_TRACK: HashMap<u32, u32> = HashMap::with_max_entries(65536, 0);

// Statistics
#[map]
static LB_STATS: Array<u64> = Array::with_max_entries(16, 0);

const VIP: u32 = 0x0A000001; // 10.0.0.1 - Virtual IP

#[xdp]
pub fn xdp_load_balancer(ctx: XdpContext) -> u32 {
    match try_load_balance(ctx) {
        Ok(ret) => ret,
        Err(_) => xdp_action::XDP_PASS,
    }
}

fn try_load_balance(ctx: XdpContext) -> Result<u32, ()> {
    let ethhdr: *mut EthHdr = unsafe { ptr_at(&ctx, 0)? };
    
    match unsafe { (*ethhdr).ether_type } {
        EtherType::Ipv4 => {}
        _ => return Ok(xdp_action::XDP_PASS),
    }
    
    let ipv4hdr: *mut Ipv4Hdr = unsafe { ptr_at(&ctx, EthHdr::LEN)? };
    let dst_addr = u32::from_be(unsafe { (*ipv4hdr).dst_addr });
    
    // Check if destination is our VIP
    if dst_addr != VIP {
        return Ok(xdp_action::XDP_PASS);
    }
    
    match unsafe { (*ipv4hdr).proto } {
        IpProto::Tcp => {}
        _ => return Ok(xdp_action::XDP_PASS),
    }
    
    let tcphdr: *const TcpHdr = unsafe {
        let ihl = ((*ipv4hdr).version_ihl & 0x0F) as usize;
        ptr_at(&ctx, EthHdr::LEN + ihl * 4)?
    };
    
    let src_addr = u32::from_be(unsafe { (*ipv4hdr).src_addr });
    let src_port = u16::from_be(unsafe { (*tcphdr).source });
    
    // Simple connection tracking hash
    let conn_hash = hash_connection(src_addr, src_port);
    
    // Look up existing connection or create new one
    let backend_idx = if let Some(idx) = CONN_TRACK.get(&conn_hash) {
        *idx
    } else {
        // Simple round-robin selection
        let idx = (conn_hash % 4) as u32; // Assuming 4 backends
        CONN_TRACK.insert(&conn_hash, &idx, 0).ok();
        idx
    };
    
    // Get backend
    let backend = BACKENDS.get(backend_idx).ok_or(())?;
    
    // Rewrite destination IP and MAC
    unsafe {
        (*ipv4hdr).dst_addr = u32::to_be(backend.ip);
        (*ethhdr).dst_addr = backend.mac;
        
        // Recalculate IP checksum
        (*ipv4hdr).check = 0;
        (*ipv4hdr).check = calculate_ip_checksum(ipv4hdr);
    }
    
    // Update statistics
    if let Some(count) = LB_STATS.get_ptr_mut(backend_idx) {
        unsafe {
            *count += 1;
        }
    }
    
    Ok(xdp_action::XDP_TX)
}

fn hash_connection(src_ip: u32, src_port: u16) -> u32 {
    // Simple hash function
    (src_ip.wrapping_mul(31).wrapping_add(src_port as u32)) % 65536
}

unsafe fn calculate_ip_checksum(iph: *const Ipv4Hdr) -> u16 {
    let mut sum: u32 = 0;
    let ptr = iph as *const u16;
    let ihl = ((*iph).version_ihl & 0x0F) as usize;
    
    for i in 0..ihl * 2 {
        if i != 5 { // Skip checksum field
            sum += u16::from_be(*ptr.add(i)) as u32;
        }
    }
    
    while sum >> 16 != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    u16::to_be(!sum as u16)
}

#[inline(always)]
unsafe fn ptr_at<T>(ctx: &XdpContext, offset: usize) -> Result<*mut T, ()> {
    let start = ctx.data();
    let end = ctx.data_end();
    let len = mem::size_of::<T>();
    
    if start + offset + len > end {
        return Err(());
    }
    
    Ok((start + offset) as *mut T)
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { core::hint::unreachable_unchecked() }
}
```

---

## Performance Considerations

1. **Zero-Copy**: XDP operates directly on DMA buffers without packet copies
2. **CPU Efficiency**: Bypasses most of the kernel network stack
3. **Line Rate**: Can process packets at 10+ million packets per second per core
4. **Memory**: Uses BPF maps for state sharing with minimal overhead
5. **Instruction Limit**: XDP programs have a complexity limit (~1 million instructions)

## Limitations

- Program complexity is limited by the BPF verifier
- Cannot make blocking calls or sleep
- Limited to packet processing at L2/L3/L4
- Requires compatible network drivers (not all drivers support XDP)
- Debugging can be challenging

## Best Practices

1. **Bounds Checking**: Always verify packet boundaries before accessing data
2. **Map Usage**: Use appropriate map types for your use case
3. **Testing**: Test in generic mode before deploying in driver mode
4. **Monitoring**: Implement statistics and logging
5. **Graceful Degradation**: Handle XDP_ABORTED cases properly

---

## Summary

**XDP (eXpress Data Path)** is a revolutionary technology that brings programmable, high-performance packet processing to Linux. By leveraging eBPF programs executed at the earliest point in the network stack, XDP enables:

- **Extreme Performance**: Processing packets at near-wire speed (10+ Mpps/core)
- **Flexibility**: Programmable packet handling without kernel modifications
- **Safety**: BPF verifier ensures programs are safe to run in kernel context
- **Efficiency**: Zero-copy packet processing with minimal CPU overhead

XDP is ideal for DDoS mitigation, load balancing, firewalling, and high-speed packet filtering. Both C/C++ (via libbpf) and Rust (via Aya framework) provide robust development environments for creating XDP programs. The technology represents a significant advancement in Linux networking, enabling software-defined networking at unprecedented speeds while maintaining the security and stability of the kernel.