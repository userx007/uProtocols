# Packet Filtering with BPF (Berkeley Packet Filter)

## Overview

Berkeley Packet Filter (BPF) is a powerful in-kernel packet filtering mechanism that allows user-space programs to efficiently capture and filter network packets. Originally developed for Unix-like systems, BPF provides a virtual machine that executes filter programs in kernel space, dramatically improving performance by filtering packets before they're copied to user space.

## How BPF Works

BPF operates by compiling filter expressions into bytecode that runs in a safe, sandboxed virtual machine within the kernel. When a packet arrives at a network interface:

1. The packet enters the kernel's network stack
2. BPF filters are evaluated against the packet
3. Only matching packets are copied to user space
4. Non-matching packets are discarded immediately

This approach minimizes context switches and memory copies, making packet capture orders of magnitude faster than filtering in user space.

## BPF Architecture

The BPF virtual machine is a register-based machine with:
- An accumulator register (A)
- An index register (X)
- A scratch memory store (M[])
- An implicit program counter

BPF instructions operate on packet data, performing comparisons, arithmetic, and conditional jumps to determine if a packet matches the filter criteria.

## Code Examples

### C/C++ - Basic Packet Capture with BPF

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

// Callback function for packet processing
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, 
                     const u_char *packet) {
    struct ether_header *eth_header;
    struct ip *ip_header;
    struct tcphdr *tcp_header;
    
    printf("\n=== Packet captured ===\n");
    printf("Packet length: %d bytes\n", pkthdr->len);
    printf("Capture length: %d bytes\n", pkthdr->caplen);
    
    // Parse Ethernet header
    eth_header = (struct ether_header *)packet;
    
    // Check if it's an IP packet
    if (ntohs(eth_header->ether_type) == ETHERTYPE_IP) {
        ip_header = (struct ip *)(packet + sizeof(struct ether_header));
        
        char src_ip[INET_ADDRSTRLEN];
        char dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        printf("IP: %s -> %s\n", src_ip, dst_ip);
        printf("Protocol: %d\n", ip_header->ip_p);
        
        // Check if it's TCP
        if (ip_header->ip_p == IPPROTO_TCP) {
            tcp_header = (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                           (ip_header->ip_hl * 4));
            printf("TCP: %d -> %d\n", ntohs(tcp_header->th_sport), 
                   ntohs(tcp_header->th_dport));
        }
    }
}

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    struct bpf_program filter;
    char filter_exp[] = "tcp port 80 or tcp port 443";
    bpf_u_int32 net, mask;
    char *dev;
    
    // Find default device
    dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return 1;
    }
    printf("Device: %s\n", dev);
    
    // Get network address and mask
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
        net = 0;
        mask = 0;
    }
    
    // Open device for packet capture
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    // Compile BPF filter
    if (pcap_compile(handle, &filter, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    // Apply BPF filter
    if (pcap_setfilter(handle, &filter) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    printf("Capturing packets with filter: %s\n", filter_exp);
    printf("Press Ctrl+C to stop...\n\n");
    
    // Capture 10 packets
    pcap_loop(handle, 10, packet_handler, NULL);
    
    // Cleanup
    pcap_freecode(&filter);
    pcap_close(handle);
    
    printf("\nCapture complete.\n");
    return 0;
}

// Compile: gcc -o bpf_capture bpf_capture.c -lpcap
// Run: sudo ./bpf_capture
```

### C++ - Advanced BPF Filter with Statistics

```cpp
#include <iostream>
#include <string>
#include <map>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <csignal>

class PacketCapture {
private:
    pcap_t *handle;
    std::map<std::string, int> protocol_stats;
    std::map<std::string, int> ip_stats;
    int total_packets;
    bool running;
    
public:
    PacketCapture() : handle(nullptr), total_packets(0), running(true) {}
    
    ~PacketCapture() {
        if (handle) {
            pcap_close(handle);
        }
    }
    
    bool initialize(const std::string &device, const std::string &filter_str) {
        char errbuf[PCAP_ERRBUF_SIZE];
        bpf_u_int32 net, mask;
        
        // Get network info
        if (pcap_lookupnet(device.c_str(), &net, &mask, errbuf) == -1) {
            std::cerr << "Warning: Couldn't get netmask: " << errbuf << std::endl;
            net = 0;
            mask = 0;
        }
        
        // Open device
        handle = pcap_open_live(device.c_str(), BUFSIZ, 1, 1000, errbuf);
        if (!handle) {
            std::cerr << "Couldn't open device: " << errbuf << std::endl;
            return false;
        }
        
        // Compile and set filter
        struct bpf_program filter;
        if (pcap_compile(handle, &filter, filter_str.c_str(), 0, net) == -1) {
            std::cerr << "Filter compilation failed: " << pcap_geterr(handle) << std::endl;
            return false;
        }
        
        if (pcap_setfilter(handle, &filter) == -1) {
            std::cerr << "Filter installation failed: " << pcap_geterr(handle) << std::endl;
            pcap_freecode(&filter);
            return false;
        }
        
        pcap_freecode(&filter);
        return true;
    }
    
    void process_packet(const u_char *packet, int len) {
        struct ether_header *eth = (struct ether_header *)packet;
        
        if (ntohs(eth->ether_type) != ETHERTYPE_IP) {
            return;
        }
        
        struct ip *ip_hdr = (struct ip *)(packet + sizeof(struct ether_header));
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_hdr->ip_src), src_ip, INET_ADDRSTRLEN);
        
        // Update statistics
        total_packets++;
        ip_stats[src_ip]++;
        
        switch (ip_hdr->ip_p) {
            case IPPROTO_TCP: {
                protocol_stats["TCP"]++;
                struct tcphdr *tcp = (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                                       (ip_hdr->ip_hl * 4));
                std::cout << "TCP packet: " << src_ip << ":" << ntohs(tcp->th_sport)
                          << " -> " << ntohs(tcp->th_dport) << std::endl;
                break;
            }
            case IPPROTO_UDP: {
                protocol_stats["UDP"]++;
                struct udphdr *udp = (struct udphdr *)(packet + sizeof(struct ether_header) + 
                                                       (ip_hdr->ip_hl * 4));
                std::cout << "UDP packet: " << src_ip << ":" << ntohs(udp->uh_sport)
                          << " -> " << ntohs(udp->uh_dport) << std::endl;
                break;
            }
            case IPPROTO_ICMP:
                protocol_stats["ICMP"]++;
                std::cout << "ICMP packet from: " << src_ip << std::endl;
                break;
            default:
                protocol_stats["Other"]++;
                break;
        }
    }
    
    static void packet_callback(u_char *user, const struct pcap_pkthdr *h, 
                                const u_char *bytes) {
        PacketCapture *capture = reinterpret_cast<PacketCapture*>(user);
        capture->process_packet(bytes, h->len);
    }
    
    void start_capture(int packet_count = -1) {
        std::cout << "Starting packet capture..." << std::endl;
        pcap_loop(handle, packet_count, packet_callback, (u_char*)this);
    }
    
    void print_statistics() {
        std::cout << "\n=== Capture Statistics ===" << std::endl;
        std::cout << "Total packets: " << total_packets << std::endl;
        
        std::cout << "\nProtocol distribution:" << std::endl;
        for (const auto &entry : protocol_stats) {
            double percentage = (entry.second * 100.0) / total_packets;
            std::cout << "  " << entry.first << ": " << entry.second 
                      << " (" << percentage << "%)" << std::endl;
        }
        
        std::cout << "\nTop 5 source IPs:" << std::endl;
        int count = 0;
        for (const auto &entry : ip_stats) {
            if (count++ >= 5) break;
            std::cout << "  " << entry.first << ": " << entry.second 
                      << " packets" << std::endl;
        }
    }
    
    void stop() {
        running = false;
        if (handle) {
            pcap_breakloop(handle);
        }
    }
};

PacketCapture *global_capture = nullptr;

void signal_handler(int signum) {
    std::cout << "\nInterrupt signal received. Stopping capture..." << std::endl;
    if (global_capture) {
        global_capture->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string device = "eth0";
    std::string filter = "ip";
    
    if (argc > 1) {
        device = argv[1];
    }
    if (argc > 2) {
        filter = argv[2];
    }
    
    PacketCapture capture;
    global_capture = &capture;
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    
    if (!capture.initialize(device, filter)) {
        return 1;
    }
    
    std::cout << "Capturing on device: " << device << std::endl;
    std::cout << "Filter: " << filter << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;
    
    capture.start_capture(50); // Capture 50 packets
    capture.print_statistics();
    
    return 0;
}

// Compile: g++ -o bpf_advanced bpf_advanced.cpp -lpcap -std=c++11
// Run: sudo ./bpf_advanced eth0 "tcp or udp"
```

### Rust - BPF Packet Filtering

```rust
// Cargo.toml dependencies:
// [dependencies]
// pcap = "1.1"
// pnet = "0.34"

use pcap::{Capture, Device, Active, Error};
use pnet::packet::ethernet::{EthernetPacket, EtherTypes};
use pnet::packet::ip::IpNextHeaderProtocols;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::tcp::TcpPacket;
use pnet::packet::udp::UdpPacket;
use pnet::packet::Packet;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Default)]
struct PacketStats {
    total: u64,
    tcp: u64,
    udp: u64,
    icmp: u64,
    other: u64,
    src_ips: HashMap<String, u64>,
}

impl PacketStats {
    fn new() -> Self {
        Self::default()
    }
    
    fn record_packet(&mut self, protocol: &str, src_ip: String) {
        self.total += 1;
        match protocol {
            "TCP" => self.tcp += 1,
            "UDP" => self.udp += 1,
            "ICMP" => self.icmp += 1,
            _ => self.other += 1,
        }
        *self.src_ips.entry(src_ip).or_insert(0) += 1;
    }
    
    fn print_summary(&self) {
        println!("\n=== Packet Capture Statistics ===");
        println!("Total packets: {}", self.total);
        println!("TCP: {} ({:.2}%)", self.tcp, 
                 (self.tcp as f64 / self.total as f64) * 100.0);
        println!("UDP: {} ({:.2}%)", self.udp,
                 (self.udp as f64 / self.total as f64) * 100.0);
        println!("ICMP: {} ({:.2}%)", self.icmp,
                 (self.icmp as f64 / self.total as f64) * 100.0);
        println!("Other: {} ({:.2}%)", self.other,
                 (self.other as f64 / self.total as f64) * 100.0);
        
        println!("\nTop 5 Source IPs:");
        let mut sorted_ips: Vec<_> = self.src_ips.iter().collect();
        sorted_ips.sort_by(|a, b| b.1.cmp(a.1));
        
        for (i, (ip, count)) in sorted_ips.iter().take(5).enumerate() {
            println!("  {}. {}: {} packets", i + 1, ip, count);
        }
    }
}

struct BpfCapture {
    capture: Capture<Active>,
    stats: Arc<Mutex<PacketStats>>,
    running: Arc<AtomicBool>,
}

impl BpfCapture {
    fn new(device_name: &str, filter: &str) -> Result<Self, Error> {
        let device = Device::lookup()
            .unwrap()
            .unwrap_or_else(|| {
                println!("No default device found, using first available");
                Device::list().unwrap()[0].clone()
            });
        
        println!("Opening device: {}", device.name);
        
        let mut cap = Capture::from_device(device)?
            .promisc(true)
            .snaplen(65535)
            .timeout(1000)
            .open()?;
        
        // Apply BPF filter
        cap.filter(filter, true)?;
        println!("Applied filter: {}", filter);
        
        Ok(BpfCapture {
            capture: cap,
            stats: Arc::new(Mutex::new(PacketStats::new())),
            running: Arc::new(AtomicBool::new(true)),
        })
    }
    
    fn process_packet(&self, data: &[u8]) {
        if let Some(ethernet) = EthernetPacket::new(data) {
            match ethernet.get_ethertype() {
                EtherTypes::Ipv4 => {
                    if let Some(ipv4) = Ipv4Packet::new(ethernet.payload()) {
                        let src_ip = ipv4.get_source().to_string();
                        let dst_ip = ipv4.get_destination().to_string();
                        
                        let protocol = match ipv4.get_next_level_protocol() {
                            IpNextHeaderProtocols::Tcp => {
                                if let Some(tcp) = TcpPacket::new(ipv4.payload()) {
                                    println!("TCP: {}:{} -> {}:{}", 
                                             src_ip, tcp.get_source(),
                                             dst_ip, tcp.get_destination());
                                    
                                    // Print TCP flags
                                    print!("  Flags: ");
                                    if tcp.get_flags() & 0x02 != 0 { print!("SYN "); }
                                    if tcp.get_flags() & 0x10 != 0 { print!("ACK "); }
                                    if tcp.get_flags() & 0x01 != 0 { print!("FIN "); }
                                    if tcp.get_flags() & 0x04 != 0 { print!("RST "); }
                                    println!();
                                }
                                "TCP"
                            },
                            IpNextHeaderProtocols::Udp => {
                                if let Some(udp) = UdpPacket::new(ipv4.payload()) {
                                    println!("UDP: {}:{} -> {}:{} (len: {})", 
                                             src_ip, udp.get_source(),
                                             dst_ip, udp.get_destination(),
                                             udp.get_length());
                                }
                                "UDP"
                            },
                            IpNextHeaderProtocols::Icmp => {
                                println!("ICMP: {} -> {}", src_ip, dst_ip);
                                "ICMP"
                            },
                            _ => {
                                println!("Other protocol: {} -> {}", src_ip, dst_ip);
                                "Other"
                            },
                        };
                        
                        if let Ok(mut stats) = self.stats.lock() {
                            stats.record_packet(protocol, src_ip);
                        }
                    }
                },
                EtherTypes::Ipv6 => {
                    println!("IPv6 packet (skipping detailed parsing)");
                },
                _ => {
                    println!("Non-IP packet: {:?}", ethernet.get_ethertype());
                }
            }
        }
    }
    
    fn start(&mut self, max_packets: Option<usize>) {
        println!("Starting packet capture...\n");
        
        let mut count = 0;
        while self.running.load(Ordering::Relaxed) {
            match self.capture.next_packet() {
                Ok(packet) => {
                    self.process_packet(packet.data);
                    count += 1;
                    
                    if let Some(max) = max_packets {
                        if count >= max {
                            break;
                        }
                    }
                },
                Err(pcap::Error::TimeoutExpired) => continue,
                Err(e) => {
                    eprintln!("Error reading packet: {}", e);
                    break;
                }
            }
        }
    }
    
    fn print_stats(&self) {
        if let Ok(stats) = self.stats.lock() {
            stats.print_summary();
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    let device = if args.len() > 1 {
        &args[1]
    } else {
        "any"
    };
    
    let filter = if args.len() > 2 {
        &args[2]
    } else {
        "tcp or udp"
    };
    
    println!("BPF Packet Capture in Rust");
    println!("==========================");
    
    let mut capture = BpfCapture::new(device, filter)?;
    
    // Setup Ctrl+C handler
    let running = Arc::clone(&capture.running);
    ctrlc::set_handler(move || {
        println!("\nStopping capture...");
        running.store(false, Ordering::Relaxed);
    }).expect("Error setting Ctrl+C handler");
    
    capture.start(Some(50)); // Capture 50 packets
    capture.print_stats();
    
    Ok(())
}

// To run:
// cargo build --release
// sudo ./target/release/bpf_rust [device] [filter]
// Example: sudo ./target/release/bpf_rust eth0 "port 80 or port 443"
```

### Raw BPF Bytecode Example (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/*
 * This example demonstrates creating BPF filters at the bytecode level.
 * The filter matches TCP packets on port 80 (HTTP).
 * 
 * BPF instruction format:
 * - opcode: operation to perform
 * - jt: jump offset if true
 * - jf: jump offset if false
 * - k: generic field (offset, value, etc.)
 */

void print_packet_info(const u_char *packet, int len) {
    struct ip *ip_header;
    struct tcphdr *tcp_header;
    
    // Skip Ethernet header (14 bytes)
    ip_header = (struct ip *)(packet + 14);
    
    if (ip_header->ip_p == IPPROTO_TCP) {
        tcp_header = (struct tcphdr *)(packet + 14 + (ip_header->ip_hl * 4));
        
        char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        printf("TCP: %s:%d -> %s:%d\n",
               src_ip, ntohs(tcp_header->th_sport),
               dst_ip, ntohs(tcp_header->th_dport));
    }
}

void packet_handler(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes) {
    static int count = 0;
    printf("[%d] ", ++count);
    print_packet_info(bytes, h->len);
}

int main() {
    pcap_t *handle;
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program filter_compiled;
    struct bpf_insn raw_filter[] = {
        /*
         * Hand-crafted BPF filter for TCP port 80
         * This is equivalent to the tcpdump filter: "tcp port 80"
         */
        
        // Load the Ethernet type at offset 12 (2 bytes)
        { 0x28, 0, 0, 0x0000000c },  // ldh [12]
        
        // Check if it's IPv4 (0x0800)
        { 0x15, 0, 8, 0x00000800 },  // jeq #0x800, true:0, false:8
        
        // Load IP protocol at offset 23 (1 byte after Ethernet header + 9 bytes into IP)
        { 0x30, 0, 0, 0x00000017 },  // ldb [23]
        
        // Check if it's TCP (protocol 6)
        { 0x15, 0, 6, 0x00000006 },  // jeq #0x6, true:0, false:6
        
        // Load IP header length (needed to find TCP header)
        { 0x28, 0, 0, 0x00000014 },  // ldh [20]
        
        // Mask off fragment offset and reserved bits
        { 0x45, 4, 0, 0x00001fff },  // jset #0x1fff, true:4, false:0
        
        // Load X with IP header length * 4
        { 0xb1, 0, 0, 0x0000000e },  // ldxb 4*([14]&0xf)
        
        // Load source port
        { 0x48, 0, 0, 0x0000000e },  // ldh [x + 14]
        
        // Check if source port is 80
        { 0x15, 0, 1, 0x00000050 },  // jeq #80, true:0, false:1
        
        // Return full packet (accept)
        { 0x06, 0, 0, 0x00040000 },  // ret #262144
        
        // Load destination port
        { 0x48, 0, 0, 0x00000010 },  // ldh [x + 16]
        
        // Check if destination port is 80
        { 0x15, 0, 1, 0x00000050 },  // jeq #80, true:0, false:1
        
        // Return full packet (accept)
        { 0x06, 0, 0, 0x00040000 },  // ret #262144
        
        // Return 0 (reject)
        { 0x06, 0, 0, 0x00000000 },  // ret #0
    };
    
    // Alternative: Use pcap_compile for easier filter creation
    char filter_exp[] = "tcp port 80";
    
    printf("BPF Raw Bytecode Example\n");
    printf("=========================\n\n");
    
    // Open device
    char *dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Error finding device: %s\n", errbuf);
        return 1;
    }
    
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    printf("Using device: %s\n", dev);
    
    // Option 1: Use raw BPF bytecode
    printf("\n--- Using Raw BPF Bytecode ---\n");
    filter_compiled.bf_len = sizeof(raw_filter) / sizeof(struct bpf_insn);
    filter_compiled.bf_insns = raw_filter;
    
    if (pcap_setfilter(handle, &filter_compiled) == -1) {
        fprintf(stderr, "Error setting raw filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    printf("Raw BPF filter installed (%d instructions)\n", filter_compiled.bf_len);
    printf("Capturing TCP port 80 packets...\n\n");
    
    pcap_loop(handle, 5, packet_handler, NULL);
    
    // Option 2: Compare with compiled filter
    printf("\n--- Using Compiled Filter Expression ---\n");
    bpf_u_int32 net, mask;
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        net = 0;
        mask = 0;
    }
    
    struct bpf_program compiled;
    if (pcap_compile(handle, &compiled, filter_exp, 1, mask) == -1) {
        fprintf(stderr, "Error compiling filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    printf("Compiled filter: %s\n", filter_exp);
    printf("Number of BPF instructions: %d\n\n", compiled.bf_len);
    
    // Print the generated BPF bytecode
    printf("Generated BPF bytecode:\n");
    for (int i = 0; i < compiled.bf_len && i < 20; i++) {
        printf("  [%2d] code=0x%02x jt=%d jf=%d k=0x%08x\n",
               i,
               compiled.bf_insns[i].code,
               compiled.bf_insns[i].jt,
               compiled.bf_insns[i].jf,
               compiled.bf_insns[i].k);
    }
    
    pcap_freecode(&compiled);
    pcap_close(handle);
    
    printf("\nCapture complete.\n");
    return 0;
}

// Compile: gcc -o raw_bpf raw_bpf.c -lpcap
// Run: sudo ./raw_bpf
```

## BPF Filter Syntax

BPF filters use a high-level expression language that gets compiled to bytecode. Here are common filter expressions:

**Protocol filters:**
- `tcp` - All TCP packets
- `udp` - All UDP packets
- `icmp` - All ICMP packets
- `ip` - All IPv4 packets

**Port filters:**
- `port 80` - Traffic on port 80 (source or destination)
- `src port 443` - Source port 443
- `dst port 53` - Destination port 53
- `portrange 8000-9000` - Ports between 8000-9000

**Host filters:**
- `host 192.168.1.1` - Traffic to/from specific IP
- `src host 10.0.0.5` - Source IP
- `dst host example.com` - Destination hostname
- `net 192.168.0.0/24` - Network range

**Logical operators:**
- `tcp and port 80` - TCP AND port 80
- `tcp or udp` - TCP OR UDP
- `not icmp` - NOT ICMP
- `(tcp and port 80) or (udp and port 53)` - Complex expressions

**Advanced filters:**
- `tcp[tcpflags] & tcp-syn != 0` - TCP SYN packets
- `ip[2:2] > 576` - IP packets larger than 576 bytes
- `ether src 00:11:22:33:44:55` - Specific MAC address

## Performance Benefits

BPF provides significant performance improvements:

1. **Kernel-space filtering**: Packets are filtered before copying to user space, reducing memory bandwidth
2. **Zero-copy**: Matched packets can be mapped directly to user space
3. **Efficient instruction set**: The BPF VM is designed for fast execution
4. **JIT compilation**: Modern kernels compile BPF to native code for even better performance

Without BPF, an application might need to copy every packet to user space and filter there, which is 10-100x slower depending on traffic volume.

## Modern Extensions

The original BPF (cBPF) has evolved into extended BPF (eBPF), which provides:

- More registers (10 instead of 2)
- 64-bit operations
- Maps and helper functions
- Ability to modify packets
- Integration with kernel tracing and security

eBPF is now used for advanced networking (XDP), security (seccomp), observability, and more.

## Summary

Berkeley Packet Filter is a fundamental technology for efficient network packet capture and filtering. By executing filter programs in kernel space before copying packets to user space, BPF dramatically reduces the overhead of packet capture. The filter language provides an intuitive way to specify complex packet matching criteria, which gets compiled into efficient bytecode. BPF forms the foundation of many network analysis tools like tcpdump, Wireshark, and modern network monitoring systems. Understanding BPF is essential for network programming, security analysis, and performance monitoring tasks. Modern eBPF has expanded these capabilities far beyond simple packet filtering, becoming a general-purpose kernel programmability framework used throughout the Linux networking and tracing ecosystem.