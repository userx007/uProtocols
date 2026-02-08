# DPDK (Data Plane Development Kit)

## Overview

**DPDK** is an open-source set of libraries and drivers for fast packet processing that enables applications to bypass the kernel's networking stack entirely. Originally developed by Intel and now maintained by the Linux Foundation, DPDK provides a framework for running network applications in userspace with direct access to network interface cards (NICs), achieving ultra-low latency and extremely high throughput.

## Core Concepts

### 1. **Kernel Bypass**
Traditional networking involves the kernel processing every packet through its TCP/IP stack, which introduces:
- System call overhead
- Context switches between user and kernel space
- Interrupt handling delays
- Multiple memory copies

DPDK eliminates these bottlenecks by allowing userspace applications to directly access NIC hardware through memory-mapped I/O regions.

### 2. **Poll Mode Drivers (PMD)**
Instead of interrupt-driven packet processing, DPDK uses polling:
- Dedicated CPU cores continuously poll NICs for packets
- No interrupt overhead
- Predictable, deterministic latency
- Trade-off: 100% CPU utilization on polling cores

### 3. **Hugepages**
DPDK uses large memory pages (2MB or 1GB instead of 4KB) to:
- Reduce TLB (Translation Lookaside Buffer) misses
- Improve memory access performance
- Enable efficient DMA operations

### 4. **Lock-Free Ring Buffers**
Multi-producer, multi-consumer ring buffers for efficient packet passing between cores without locks.

### 5. **Memory Pools**
Pre-allocated memory pools (mempools) for packet buffers to avoid dynamic allocation overhead.

## Architecture Components

```
┌─────────────────────────────────────────┐
│      User Application                   │
├─────────────────────────────────────────┤
│   DPDK Libraries (librte_*)             │
│   - EAL (Environment Abstraction Layer) │
│   - Mempool, Mbuf, Ring                 │
│   - Timer, Hash, LPM                    │
├─────────────────────────────────────────┤
│   Poll Mode Drivers (PMD)               │
│   - Physical NIC drivers                │
│   - Virtual device drivers              │
├─────────────────────────────────────────┤
│   Hardware (NIC) - Direct Access        │
└─────────────────────────────────────────┘
```

## C/C++ Programming Examples

### Example 1: Basic DPDK Initialization and Packet Reception

```c
#include <stdio.h>
#include <stdint.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

/* Port configuration structure */
static const struct rte_eth_conf port_conf = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
};

/* Initialize a port */
static int port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf_local = port_conf;
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        printf("Error getting device info: %s\n", strerror(-retval));
        return retval;
    }

    /* Configure the Ethernet device */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf_local);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    /* Allocate and set up RX queue */
    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd,
            rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
        return retval;

    /* Allocate and set up TX queue */
    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf_local.txmode.offloads;
    retval = rte_eth_tx_queue_setup(port, 0, nb_txd,
            rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0)
        return retval;

    /* Start the Ethernet port */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    /* Enable promiscuous mode */
    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0)
        return retval;

    return 0;
}

/* Main packet processing loop */
static void lcore_main(uint16_t port)
{
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;
    uint64_t total_packets = 0;
    uint64_t start_tsc = rte_rdtsc();

    printf("Core %u forwarding packets from port %u\n",
            rte_lcore_id(), port);

    while (1) {
        /* Receive burst of packets */
        nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        total_packets += nb_rx;

        /* Process packets (example: simple forwarding) */
        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *m = bufs[i];
            
            /* Access packet data */
            uint8_t *pkt_data = rte_pktmbuf_mtod(m, uint8_t *);
            uint16_t pkt_len = rte_pktmbuf_pkt_len(m);
            
            /* Your packet processing logic here */
            // Example: print first 14 bytes (Ethernet header)
            // printf("Packet %lu: len=%u\n", total_packets, pkt_len);
        }

        /* Send packets back out (or to another port) */
        uint16_t nb_tx = rte_eth_tx_burst(port, 0, bufs, nb_rx);

        /* Free any unsent packets */
        if (unlikely(nb_tx < nb_rx)) {
            for (uint16_t i = nb_tx; i < nb_rx; i++)
                rte_pktmbuf_free(bufs[i]);
        }

        /* Print statistics every 10M packets */
        if (total_packets % 10000000 == 0) {
            uint64_t end_tsc = rte_rdtsc();
            double seconds = (end_tsc - start_tsc) / (double)rte_get_tsc_hz();
            printf("Processed %lu packets in %.2f seconds (%.2f Mpps)\n",
                   total_packets, seconds, (total_packets / seconds) / 1000000.0);
        }
    }
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    uint16_t portid = 0;
    int ret;

    /* Initialize DPDK EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    /* Check that there is at least one port available */
    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    /* Create memory pool for packet buffers */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* Initialize port */
    if (port_init(portid, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", portid);

    /* Launch packet processing on main core */
    lcore_main(portid);

    /* Clean up */
    rte_eth_dev_stop(portid);
    rte_eth_dev_close(portid);

    return 0;
}
```

### Example 2: C++ Packet Parser with DPDK

```cpp
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <iostream>
#include <memory>
#include <vector>

class PacketParser {
public:
    struct ParsedPacket {
        bool is_ipv4;
        bool is_tcp;
        bool is_udp;
        uint32_t src_ip;
        uint32_t dst_ip;
        uint16_t src_port;
        uint16_t dst_port;
        uint16_t payload_len;
    };

    static ParsedPacket parse(rte_mbuf* mbuf) {
        ParsedPacket pkt = {};
        
        // Parse Ethernet header
        rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
        uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
        
        if (ether_type != RTE_ETHER_TYPE_IPV4) {
            return pkt;
        }
        
        pkt.is_ipv4 = true;
        
        // Parse IPv4 header
        rte_ipv4_hdr* ip_hdr = (rte_ipv4_hdr*)(eth_hdr + 1);
        pkt.src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
        pkt.dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        
        uint8_t protocol = ip_hdr->next_proto_id;
        uint8_t ip_hdr_len = (ip_hdr->version_ihl & 0x0f) * 4;
        
        // Parse transport layer
        if (protocol == IPPROTO_TCP) {
            pkt.is_tcp = true;
            rte_tcp_hdr* tcp_hdr = (rte_tcp_hdr*)((uint8_t*)ip_hdr + ip_hdr_len);
            pkt.src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
            pkt.dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
            
            uint8_t tcp_hdr_len = ((tcp_hdr->data_off & 0xf0) >> 4) * 4;
            pkt.payload_len = rte_be_to_cpu_16(ip_hdr->total_length) - 
                             ip_hdr_len - tcp_hdr_len;
        }
        else if (protocol == IPPROTO_UDP) {
            pkt.is_udp = true;
            rte_udp_hdr* udp_hdr = (rte_udp_hdr*)((uint8_t*)ip_hdr + ip_hdr_len);
            pkt.src_port = rte_be_to_cpu_16(udp_hdr->src_port);
            pkt.dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
            pkt.payload_len = rte_be_to_cpu_16(udp_hdr->dgram_len) - 
                             sizeof(rte_udp_hdr);
        }
        
        return pkt;
    }
    
    static std::string ip_to_string(uint32_t ip) {
        return std::to_string((ip >> 24) & 0xFF) + "." +
               std::to_string((ip >> 16) & 0xFF) + "." +
               std::to_string((ip >> 8) & 0xFF) + "." +
               std::to_string(ip & 0xFF);
    }
};

class DPDKPacketProcessor {
private:
    uint16_t port_id;
    rte_mempool* mbuf_pool;
    static constexpr uint16_t BURST_SIZE = 32;
    
public:
    DPDKPacketProcessor(uint16_t port) : port_id(port) {}
    
    void process_packets() {
        rte_mbuf* bufs[BURST_SIZE];
        
        while (true) {
            uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);
            
            if (nb_rx == 0)
                continue;
            
            for (uint16_t i = 0; i < nb_rx; i++) {
                auto parsed = PacketParser::parse(bufs[i]);
                
                if (parsed.is_ipv4) {
                    std::cout << "IPv4: " 
                             << PacketParser::ip_to_string(parsed.src_ip)
                             << " -> "
                             << PacketParser::ip_to_string(parsed.dst_ip);
                    
                    if (parsed.is_tcp) {
                        std::cout << " TCP " << parsed.src_port 
                                 << " -> " << parsed.dst_port
                                 << " (" << parsed.payload_len << " bytes)";
                    }
                    else if (parsed.is_udp) {
                        std::cout << " UDP " << parsed.src_port 
                                 << " -> " << parsed.dst_port
                                 << " (" << parsed.payload_len << " bytes)";
                    }
                    std::cout << std::endl;
                }
                
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
};
```

## Rust Programming Examples

### Example 1: DPDK Wrapper in Rust using FFI

```rust
// Using capsule crate - a Rust framework for DPDK
use capsule::prelude::*;
use capsule::packets::{Ethernet, Ipv4, Tcp, Udp};
use std::net::Ipv4Addr;

#[derive(Default)]
pub struct PacketCounter {
    total_packets: u64,
    tcp_packets: u64,
    udp_packets: u64,
    other_packets: u64,
}

impl Batch for PacketCounter {
    fn replenish(&mut self) {
        // Called when the batch needs more packets
    }

    fn process(&mut self) {
        let mut iter = self.iter();
        
        while let Some(packet) = iter.next() {
            self.total_packets += 1;
            
            // Parse Ethernet frame
            if let Ok(ethernet) = packet.parse::<Ethernet>() {
                // Check if it's IPv4
                if let Ok(ipv4) = ethernet.parse::<Ipv4>() {
                    let src_ip = ipv4.src();
                    let dst_ip = ipv4.dst();
                    
                    // Check for TCP
                    if let Ok(tcp) = ipv4.parse::<Tcp<Ipv4>>() {
                        self.tcp_packets += 1;
                        println!(
                            "TCP: {}:{} -> {}:{}",
                            src_ip,
                            tcp.src_port(),
                            dst_ip,
                            tcp.dst_port()
                        );
                    }
                    // Check for UDP
                    else if let Ok(udp) = ipv4.parse::<Udp<Ipv4>>() {
                        self.udp_packets += 1;
                        println!(
                            "UDP: {}:{} -> {}:{}",
                            src_ip,
                            udp.src_port(),
                            dst_ip,
                            udp.dst_port()
                        );
                    }
                    else {
                        self.other_packets += 1;
                    }
                }
            }
            
            // Print statistics every 1M packets
            if self.total_packets % 1_000_000 == 0 {
                println!(
                    "Stats - Total: {}, TCP: {}, UDP: {}, Other: {}",
                    self.total_packets,
                    self.tcp_packets,
                    self.udp_packets,
                    self.other_packets
                );
            }
        }
    }
}
```

### Example 2: Simple DPDK-based Firewall in Rust

```rust
use capsule::prelude::*;
use capsule::packets::{Ethernet, Ipv4, Tcp};
use std::collections::HashSet;
use std::net::Ipv4Addr;

pub struct SimpleFirewall {
    blocked_ips: HashSet<Ipv4Addr>,
    blocked_ports: HashSet<u16>,
    packets_processed: u64,
    packets_blocked: u64,
}

impl SimpleFirewall {
    pub fn new() -> Self {
        let mut blocked_ips = HashSet::new();
        blocked_ips.insert(Ipv4Addr::new(192, 168, 1, 100));
        blocked_ips.insert(Ipv4Addr::new(10, 0, 0, 50));
        
        let mut blocked_ports = HashSet::new();
        blocked_ports.insert(23);   // Telnet
        blocked_ports.insert(135);  // MSRPC
        blocked_ports.insert(445);  // SMB
        
        SimpleFirewall {
            blocked_ips,
            blocked_ports,
            packets_processed: 0,
            packets_blocked: 0,
        }
    }
    
    fn should_block(&self, src_ip: Ipv4Addr, dst_port: u16) -> bool {
        self.blocked_ips.contains(&src_ip) || 
        self.blocked_ports.contains(&dst_port)
    }
}

impl Batch for SimpleFirewall {
    fn replenish(&mut self) {}
    
    fn process(&mut self) {
        let mut iter = self.iter();
        
        while let Some(mut packet) = iter.next() {
            self.packets_processed += 1;
            let mut should_drop = false;
            
            // Parse packet
            if let Ok(ethernet) = packet.parse::<Ethernet>() {
                if let Ok(ipv4) = ethernet.parse::<Ipv4>() {
                    let src_ip = ipv4.src();
                    
                    // Check TCP packets
                    if let Ok(tcp) = ipv4.parse::<Tcp<Ipv4>>() {
                        let dst_port = tcp.dst_port();
                        
                        if self.should_block(src_ip, dst_port) {
                            should_drop = true;
                            self.packets_blocked += 1;
                            
                            println!(
                                "BLOCKED: {}:{} -> {}:{} (Rule: {} {})",
                                src_ip,
                                tcp.src_port(),
                                ipv4.dst(),
                                dst_port,
                                if self.blocked_ips.contains(&src_ip) {
                                    "IP"
                                } else {
                                    "PORT"
                                },
                                if self.blocked_ips.contains(&src_ip) {
                                    src_ip.to_string()
                                } else {
                                    dst_port.to_string()
                                }
                            );
                        }
                    }
                }
            }
            
            // Drop or forward packet
            if should_drop {
                packet.drop();
            } else {
                // Forward packet to output
                packet.deparse();
            }
            
            // Periodic statistics
            if self.packets_processed % 1_000_000 == 0 {
                let block_rate = (self.packets_blocked as f64 / 
                                 self.packets_processed as f64) * 100.0;
                println!(
                    "Firewall Stats - Processed: {}, Blocked: {} ({:.2}%)",
                    self.packets_processed,
                    self.packets_blocked,
                    block_rate
                );
            }
        }
    }
}

// Pipeline configuration
fn install(q: PortQueue) -> impl Pipeline {
    q.add(SimpleFirewall::new())
}

// Main function to run the pipeline
#[capsule::pipeline]
fn pipeline_main() {
    let config = load_config!();
    let pipeline = config.port("eth0").unwrap()
        .configure(Default::default())
        .expect("Failed to configure port");
    
    pipeline
        .rx_queue(0)
        .unwrap()
        .poll(install)
        .execute();
}
```

### Example 3: High-Performance Packet Generator in Rust

```rust
use capsule::prelude::*;
use capsule::packets::{Ethernet, Ipv4, Udp, RawPacket};
use std::net::Ipv4Addr;

pub struct PacketGenerator {
    src_mac: [u8; 6],
    dst_mac: [u8; 6],
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    src_port: u16,
    dst_port: u16,
    packet_count: u64,
    target_pps: u64,
}

impl PacketGenerator {
    pub fn new() -> Self {
        PacketGenerator {
            src_mac: [0x00, 0x11, 0x22, 0x33, 0x44, 0x55],
            dst_mac: [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF],
            src_ip: Ipv4Addr::new(10, 0, 0, 1),
            dst_ip: Ipv4Addr::new(10, 0, 0, 2),
            src_port: 12345,
            dst_port: 80,
            packet_count: 0,
            target_pps: 10_000_000, // 10 Mpps
        }
    }
    
    fn create_packet(&mut self) -> RawPacket {
        let mut packet = RawPacket::new();
        
        // Build Ethernet header
        let ethernet = Ethernet::new()
            .src(self.src_mac)
            .dst(self.dst_mac);
        
        // Build IPv4 header
        let ipv4 = Ipv4::new()
            .src(self.src_ip)
            .dst(self.dst_ip)
            .ttl(64);
        
        // Build UDP header
        let udp = Udp::new()
            .src_port(self.src_port)
            .dst_port(self.dst_port);
        
        // Add payload
        let payload = format!("Packet #{}", self.packet_count);
        
        // Assemble packet
        packet
            .push(ethernet)
            .push(ipv4)
            .push(udp)
            .push_bytes(payload.as_bytes());
        
        packet
    }
}

impl Batch for PacketGenerator {
    fn replenish(&mut self) {
        // Generate a burst of packets
        const BURST_SIZE: usize = 32;
        
        for _ in 0..BURST_SIZE {
            let packet = self.create_packet();
            self.allocate_packet(packet);
            self.packet_count += 1;
        }
        
        // Print stats every 10M packets
        if self.packet_count % 10_000_000 == 0 {
            println!("Generated {} packets", self.packet_count);
        }
    }
    
    fn process(&mut self) {
        // For generator, just forward all packets
        let mut iter = self.iter();
        while let Some(packet) = iter.next() {
            packet.deparse();
        }
    }
}
```

## Use Cases

### 1. **Network Function Virtualization (NFV)**
- Virtual routers, switches, firewalls
- Load balancers
- DPI (Deep Packet Inspection) appliances

### 2. **High-Frequency Trading (HFT)**
- Ultra-low latency market data processing
- Order execution systems
- Risk management systems

### 3. **Telecommunications**
- 5G packet core (UPF, SMF)
- Mobile edge computing
- Carrier-grade NAT

### 4. **Security Appliances**
- IDS/IPS systems
- DDoS mitigation
- Packet capture and analysis

### 5. **Content Delivery Networks (CDN)**
- Edge caching
- Traffic optimization
- Protocol acceleration

## Performance Considerations

### Advantages:
- **Throughput**: Can achieve 100+ Gbps on commodity hardware
- **Latency**: Sub-microsecond packet processing
- **Determinism**: Predictable performance through polling
- **CPU Efficiency**: Minimizes cache misses and context switches

### Trade-offs:
- **CPU Dedication**: Polling cores run at 100% utilization
- **Complexity**: Requires understanding of hardware and memory management
- **Portability**: Tied to specific NIC models and drivers
- **Development Effort**: More complex than standard socket programming

## Building and Running

### C/C++ Compilation:
```bash
# Compile with pkg-config
gcc -O3 myapp.c -o myapp $(pkg-config --cflags --libs libdpdk)

# Or with explicit flags
gcc -O3 myapp.c -o myapp \
    -I/usr/local/include/dpdk \
    -L/usr/local/lib \
    -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive \
    -lpthread -ldl -lnuma
```

### Running DPDK Applications:
```bash
# Setup hugepages
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Bind NIC to DPDK-compatible driver
dpdk-devbind.py --bind=vfio-pci 0000:03:00.0

# Run application
./myapp -l 0-3 -n 4 -- -p 0x1
```

## Summary

**DPDK** revolutionizes packet processing by bypassing the kernel networking stack and giving applications direct access to network hardware. Through techniques like poll-mode drivers, hugepages, and lock-free data structures, DPDK achieves:

- **10-100x performance improvement** over traditional networking
- **Sub-microsecond latency** for packet processing
- **Line-rate processing** at 100+ Gbps

The framework is ideal for applications requiring extreme performance: network appliances, telecom infrastructure, financial trading systems, and high-performance computing. While it demands dedicated CPU cores and careful resource management, DPDK has become the de facto standard for high-speed packet processing in both cloud and bare-metal environments.

The ecosystem includes robust C/C++ libraries and growing Rust support through projects like Capsule, making it accessible for modern systems programming while maintaining the performance characteristics that made it essential for next-generation networking infrastructure.