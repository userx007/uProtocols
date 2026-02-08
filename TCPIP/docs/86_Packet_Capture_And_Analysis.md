# Packet Capture and Analysis: Deep Packet Inspection, Protocol Analysis, and Traffic Patterns

## Overview

Packet capture and analysis is the process of intercepting, recording, and examining network traffic as it flows across a network interface. This fundamental technique enables network troubleshooting, security monitoring, protocol development, and performance analysis. Deep packet inspection (DPI) goes beyond basic header analysis to examine packet payloads, while protocol analysis helps decode and interpret the data according to various network protocols.

## Core Concepts

### Packet Capture Mechanisms

Modern operating systems provide several mechanisms for packet capture:

- **Raw Sockets**: Low-level access to network packets, allowing programs to read packets before they're processed by the kernel's network stack
- **Packet Filter (BPF/eBPF)**: Berkeley Packet Filter provides efficient filtering at the kernel level to capture only relevant packets
- **libpcap/WinPcap**: Cross-platform libraries that abstract the underlying capture mechanisms
- **AF_PACKET (Linux)**: Linux-specific socket family for low-level packet access

### Deep Packet Inspection (DPI)

DPI involves examining the complete packet, including headers and payload data, to:

- Identify applications and protocols
- Detect malware and intrusions
- Enforce network policies
- Perform traffic shaping
- Extract metadata and statistics

### Protocol Analysis

Protocol analysis dissects packets according to their protocol specifications:

- **Layer 2 (Data Link)**: Ethernet, MAC addresses, VLANs
- **Layer 3 (Network)**: IP, ICMP, routing information
- **Layer 4 (Transport)**: TCP, UDP, port numbers, connection states
- **Layer 7 (Application)**: HTTP, DNS, TLS, application-specific data

## C/C++ Implementation

Here's a comprehensive packet capture and analysis implementation using libpcap:

```c
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <time.h>

// Statistics structure
typedef struct {
    unsigned long total_packets;
    unsigned long tcp_packets;
    unsigned long udp_packets;
    unsigned long icmp_packets;
    unsigned long other_packets;
    unsigned long total_bytes;
} PacketStats;

PacketStats stats = {0};

// Protocol analysis structure
typedef struct {
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    uint32_t payload_size;
    struct timeval timestamp;
} PacketInfo;

// Print Ethernet header
void print_ethernet_header(const u_char *packet) {
    struct ether_header *eth = (struct ether_header *)packet;
    
    printf("Ethernet Header:\n");
    printf("  Destination MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->ether_dhost[0], eth->ether_dhost[1], eth->ether_dhost[2],
           eth->ether_dhost[3], eth->ether_dhost[4], eth->ether_dhost[5]);
    printf("  Source MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->ether_shost[0], eth->ether_shost[1], eth->ether_shost[2],
           eth->ether_shost[3], eth->ether_shost[4], eth->ether_shost[5]);
    printf("  Protocol: 0x%04x\n", ntohs(eth->ether_type));
}

// Print IP header
void print_ip_header(const u_char *packet, PacketInfo *info) {
    struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
    
    inet_ntop(AF_INET, &(iph->ip_src), info->src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(iph->ip_dst), info->dst_ip, INET_ADDRSTRLEN);
    info->protocol = iph->ip_p;
    
    printf("IP Header:\n");
    printf("  Version: %d\n", iph->ip_v);
    printf("  Header Length: %d bytes\n", iph->ip_hl * 4);
    printf("  Type of Service: 0x%02x\n", iph->ip_tos);
    printf("  Total Length: %d\n", ntohs(iph->ip_len));
    printf("  TTL: %d\n", iph->ip_ttl);
    printf("  Protocol: %d\n", iph->ip_p);
    printf("  Source IP: %s\n", info->src_ip);
    printf("  Destination IP: %s\n", info->dst_ip);
}

// Print TCP header and analyze flags
void print_tcp_header(const u_char *packet, PacketInfo *info) {
    struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
    unsigned int ip_header_len = iph->ip_hl * 4;
    struct tcphdr *tcph = (struct tcphdr *)(packet + sizeof(struct ether_header) + ip_header_len);
    
    info->src_port = ntohs(tcph->th_sport);
    info->dst_port = ntohs(tcph->th_dport);
    
    printf("TCP Header:\n");
    printf("  Source Port: %d\n", info->src_port);
    printf("  Destination Port: %d\n", info->dst_port);
    printf("  Sequence Number: %u\n", ntohl(tcph->th_seq));
    printf("  Acknowledgment Number: %u\n", ntohl(tcph->th_ack));
    printf("  Header Length: %d bytes\n", tcph->th_off * 4);
    printf("  Flags: ");
    if (tcph->th_flags & TH_FIN) printf("FIN ");
    if (tcph->th_flags & TH_SYN) printf("SYN ");
    if (tcph->th_flags & TH_RST) printf("RST ");
    if (tcph->th_flags & TH_PUSH) printf("PUSH ");
    if (tcph->th_flags & TH_ACK) printf("ACK ");
    if (tcph->th_flags & TH_URG) printf("URG ");
    printf("\n");
    printf("  Window Size: %d\n", ntohs(tcph->th_win));
    
    // Calculate payload size
    unsigned int total_header_size = sizeof(struct ether_header) + ip_header_len + (tcph->th_off * 4);
    info->payload_size = ntohs(iph->ip_len) - ip_header_len - (tcph->th_off * 4);
    
    stats.tcp_packets++;
}

// Print UDP header
void print_udp_header(const u_char *packet, PacketInfo *info) {
    struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
    unsigned int ip_header_len = iph->ip_hl * 4;
    struct udphdr *udph = (struct udphdr *)(packet + sizeof(struct ether_header) + ip_header_len);
    
    info->src_port = ntohs(udph->uh_sport);
    info->dst_port = ntohs(udph->uh_dport);
    info->payload_size = ntohs(udph->uh_ulen) - 8;
    
    printf("UDP Header:\n");
    printf("  Source Port: %d\n", info->src_port);
    printf("  Destination Port: %d\n", info->dst_port);
    printf("  Length: %d\n", ntohs(udph->uh_ulen));
    printf("  Checksum: 0x%04x\n", ntohs(udph->uh_sum));
    
    stats.udp_packets++;
}

// Print payload (first N bytes in hex and ASCII)
void print_payload(const u_char *packet, const struct pcap_pkthdr *header) {
    struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
    unsigned int ip_header_len = iph->ip_hl * 4;
    unsigned int transport_header_len;
    
    if (iph->ip_p == IPPROTO_TCP) {
        struct tcphdr *tcph = (struct tcphdr *)(packet + sizeof(struct ether_header) + ip_header_len);
        transport_header_len = tcph->th_off * 4;
    } else if (iph->ip_p == IPPROTO_UDP) {
        transport_header_len = 8;
    } else {
        return;
    }
    
    unsigned int total_header_size = sizeof(struct ether_header) + ip_header_len + transport_header_len;
    unsigned int payload_size = header->len - total_header_size;
    const u_char *payload = packet + total_header_size;
    
    if (payload_size > 0) {
        printf("Payload (%d bytes):\n", payload_size);
        int bytes_to_print = payload_size < 64 ? payload_size : 64;
        
        for (int i = 0; i < bytes_to_print; i++) {
            if (i % 16 == 0) printf("  %04x: ", i);
            printf("%02x ", payload[i]);
            if ((i + 1) % 16 == 0 || i == bytes_to_print - 1) {
                // Print ASCII representation
                int padding = 16 - ((i % 16) + 1);
                for (int j = 0; j < padding; j++) printf("   ");
                printf("  ");
                int start = i - (i % 16);
                for (int j = start; j <= i; j++) {
                    printf("%c", (payload[j] >= 32 && payload[j] <= 126) ? payload[j] : '.');
                }
                printf("\n");
            }
        }
    }
}

// Packet handler callback
void packet_handler(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    PacketInfo info = {0};
    info.timestamp = header->ts;
    
    stats.total_packets++;
    stats.total_bytes += header->len;
    
    printf("\n========== Packet #%lu ==========\n", stats.total_packets);
    printf("Timestamp: %s", ctime(&header->ts.tv_sec));
    printf("Capture Length: %d bytes\n", header->caplen);
    printf("Actual Length: %d bytes\n\n", header->len);
    
    // Parse Ethernet header
    print_ethernet_header(packet);
    printf("\n");
    
    struct ether_header *eth = (struct ether_header *)packet;
    if (ntohs(eth->ether_type) == ETHERTYPE_IP) {
        // Parse IP header
        print_ip_header(packet, &info);
        printf("\n");
        
        struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
        
        // Parse transport layer
        switch (iph->ip_p) {
            case IPPROTO_TCP:
                print_tcp_header(packet, &info);
                break;
            case IPPROTO_UDP:
                print_udp_header(packet, &info);
                break;
            case IPPROTO_ICMP:
                printf("ICMP Packet\n");
                stats.icmp_packets++;
                break;
            default:
                printf("Other Protocol: %d\n", iph->ip_p);
                stats.other_packets++;
                break;
        }
        
        printf("\n");
        print_payload(packet, header);
    }
    
    printf("=====================================\n");
}

// Print statistics
void print_statistics() {
    printf("\n\n========== Capture Statistics ==========\n");
    printf("Total Packets: %lu\n", stats.total_packets);
    printf("Total Bytes: %lu (%.2f MB)\n", stats.total_bytes, stats.total_bytes / (1024.0 * 1024.0));
    printf("TCP Packets: %lu (%.2f%%)\n", stats.tcp_packets, 
           (stats.total_packets > 0) ? (stats.tcp_packets * 100.0 / stats.total_packets) : 0);
    printf("UDP Packets: %lu (%.2f%%)\n", stats.udp_packets,
           (stats.total_packets > 0) ? (stats.udp_packets * 100.0 / stats.total_packets) : 0);
    printf("ICMP Packets: %lu (%.2f%%)\n", stats.icmp_packets,
           (stats.total_packets > 0) ? (stats.icmp_packets * 100.0 / stats.total_packets) : 0);
    printf("Other Packets: %lu (%.2f%%)\n", stats.other_packets,
           (stats.total_packets > 0) ? (stats.other_packets * 100.0 / stats.total_packets) : 0);
    printf("========================================\n");
}

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    char *dev;
    struct bpf_program fp;
    char filter_exp[] = "ip";  // Capture only IP packets
    bpf_u_int32 net, mask;
    
    // Find default device if not specified
    if (argc < 2) {
        dev = pcap_lookupdev(errbuf);
        if (dev == NULL) {
            fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
            return 1;
        }
    } else {
        dev = argv[1];
    }
    
    printf("Capturing on device: %s\n", dev);
    
    // Get network address and mask
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
        net = 0;
        mask = 0;
    }
    
    // Open device for capturing
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    // Compile and apply filter
    if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    printf("Filter: %s\n", filter_exp);
    printf("Starting packet capture... (Press Ctrl+C to stop)\n\n");
    
    // Start capturing packets
    pcap_loop(handle, 10, packet_handler, NULL);  // Capture 10 packets
    
    // Print statistics
    print_statistics();
    
    // Cleanup
    pcap_freecode(&fp);
    pcap_close(handle);
    
    return 0;
}
```

### Advanced C++ Packet Analyzer with Flow Tracking

```cpp
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <chrono>

class FlowKey {
public:
    std::string src_ip;
    std::string dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    
    bool operator<(const FlowKey& other) const {
        if (src_ip != other.src_ip) return src_ip < other.src_ip;
        if (dst_ip != other.dst_ip) return dst_ip < other.dst_ip;
        if (src_port != other.src_port) return src_port < other.src_port;
        if (dst_port != other.dst_port) return dst_port < other.dst_port;
        return protocol < other.protocol;
    }
    
    std::string to_string() const {
        return src_ip + ":" + std::to_string(src_port) + " -> " +
               dst_ip + ":" + std::to_string(dst_port) + " (" +
               std::to_string(protocol) + ")";
    }
};

class FlowStats {
public:
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
    std::chrono::system_clock::time_point first_seen;
    std::chrono::system_clock::time_point last_seen;
    std::vector<uint32_t> tcp_seq_numbers;
    
    void update(uint32_t bytes, uint32_t seq_num = 0) {
        packet_count++;
        byte_count += bytes;
        last_seen = std::chrono::system_clock::now();
        
        if (packet_count == 1) {
            first_seen = last_seen;
        }
        
        if (seq_num > 0) {
            tcp_seq_numbers.push_back(seq_num);
        }
    }
    
    double get_duration_seconds() const {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            last_seen - first_seen);
        return duration.count() / 1000.0;
    }
    
    double get_bandwidth_mbps() const {
        double duration = get_duration_seconds();
        if (duration > 0) {
            return (byte_count * 8.0) / (duration * 1000000.0);
        }
        return 0.0;
    }
};

class PacketAnalyzer {
private:
    std::map<FlowKey, FlowStats> flows;
    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;
    
public:
    void analyze_packet(const u_char *packet, const struct pcap_pkthdr *header) {
        total_packets++;
        total_bytes += header->len;
        
        struct ether_header *eth = (struct ether_header *)packet;
        if (ntohs(eth->ether_type) != ETHERTYPE_IP) {
            return;
        }
        
        struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
        unsigned int ip_header_len = iph->ip_hl * 4;
        
        FlowKey key;
        char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(iph->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(iph->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        key.src_ip = src_ip;
        key.dst_ip = dst_ip;
        key.protocol = iph->ip_p;
        
        uint32_t seq_num = 0;
        
        if (iph->ip_p == IPPROTO_TCP) {
            struct tcphdr *tcph = (struct tcphdr *)(packet + sizeof(struct ether_header) + ip_header_len);
            key.src_port = ntohs(tcph->th_sport);
            key.dst_port = ntohs(tcph->th_dport);
            seq_num = ntohl(tcph->th_seq);
        } else if (iph->ip_p == IPPROTO_UDP) {
            struct udphdr *udph = (struct udphdr *)(packet + sizeof(struct ether_header) + ip_header_len);
            key.src_port = ntohs(udph->uh_sport);
            key.dst_port = ntohs(udph->uh_dport);
        } else {
            key.src_port = 0;
            key.dst_port = 0;
        }
        
        flows[key].update(header->len, seq_num);
    }
    
    void print_flow_statistics() {
        std::cout << "\n========== Flow Statistics ==========\n";
        std::cout << "Total Flows: " << flows.size() << "\n";
        std::cout << "Total Packets: " << total_packets << "\n";
        std::cout << "Total Bytes: " << total_bytes << "\n\n";
        
        std::cout << "Top Flows by Bandwidth:\n";
        std::vector<std::pair<FlowKey, FlowStats>> sorted_flows(flows.begin(), flows.end());
        std::sort(sorted_flows.begin(), sorted_flows.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.byte_count > b.second.byte_count;
                  });
        
        int count = 0;
        for (const auto& [key, stats] : sorted_flows) {
            if (count++ >= 10) break;
            
            std::cout << "\nFlow: " << key.to_string() << "\n";
            std::cout << "  Packets: " << stats.packet_count << "\n";
            std::cout << "  Bytes: " << stats.byte_count << "\n";
            std::cout << "  Duration: " << stats.get_duration_seconds() << " seconds\n";
            std::cout << "  Bandwidth: " << stats.get_bandwidth_mbps() << " Mbps\n";
        }
        std::cout << "====================================\n";
    }
};

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    
    const char *dev = (argc > 1) ? argv[1] : pcap_lookupdev(errbuf);
    if (!dev) {
        std::cerr << "Error finding device: " << errbuf << "\n";
        return 1;
    }
    
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (!handle) {
        std::cerr << "Error opening device: " << errbuf << "\n";
        return 1;
    }
    
    PacketAnalyzer analyzer;
    
    std::cout << "Capturing on device: " << dev << "\n";
    std::cout << "Press Ctrl+C to stop...\n\n";
    
    struct pcap_pkthdr *header;
    const u_char *packet;
    int res;
    
    for (int i = 0; i < 100; i++) {  // Capture 100 packets
        res = pcap_next_ex(handle, &header, &packet);
        if (res == 1) {
            analyzer.analyze_packet(packet, header);
        }
    }
    
    analyzer.print_flow_statistics();
    
    pcap_close(handle);
    return 0;
}
```

## Rust Implementation

Here's a comprehensive Rust implementation using the `pnet` crate:

```rust
use pnet::datalink::{self, NetworkInterface, Channel};
use pnet::packet::ethernet::{EthernetPacket, EtherTypes};
use pnet::packet::ip::IpNextHeaderProtocols;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::tcp::TcpPacket;
use pnet::packet::udp::UdpPacket;
use pnet::packet::Packet;
use std::collections::HashMap;
use std::net::Ipv4Addr;
use std::time::{SystemTime, Duration};

#[derive(Debug, Clone, Hash, Eq, PartialEq)]
struct FlowKey {
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    src_port: u16,
    dst_port: u16,
    protocol: u8,
}

impl FlowKey {
    fn to_string(&self) -> String {
        format!(
            "{}:{} -> {}:{} (proto: {})",
            self.src_ip, self.src_port, self.dst_ip, self.dst_port, self.protocol
        )
    }
}

#[derive(Debug, Clone)]
struct FlowStats {
    packet_count: u64,
    byte_count: u64,
    first_seen: SystemTime,
    last_seen: SystemTime,
}

impl FlowStats {
    fn new() -> Self {
        let now = SystemTime::now();
        FlowStats {
            packet_count: 0,
            byte_count: 0,
            first_seen: now,
            last_seen: now,
        }
    }

    fn update(&mut self, bytes: u64) {
        self.packet_count += 1;
        self.byte_count += bytes;
        self.last_seen = SystemTime::now();
    }

    fn duration(&self) -> Duration {
        self.last_seen.duration_since(self.first_seen).unwrap_or(Duration::from_secs(0))
    }

    fn bandwidth_mbps(&self) -> f64 {
        let duration_secs = self.duration().as_secs_f64();
        if duration_secs > 0.0 {
            (self.byte_count as f64 * 8.0) / (duration_secs * 1_000_000.0)
        } else {
            0.0
        }
    }
}

struct PacketAnalyzer {
    flows: HashMap<FlowKey, FlowStats>,
    total_packets: u64,
    total_bytes: u64,
    tcp_packets: u64,
    udp_packets: u64,
    other_packets: u64,
}

impl PacketAnalyzer {
    fn new() -> Self {
        PacketAnalyzer {
            flows: HashMap::new(),
            total_packets: 0,
            total_bytes: 0,
            tcp_packets: 0,
            udp_packets: 0,
            other_packets: 0,
        }
    }

    fn analyze_packet(&mut self, ethernet: &EthernetPacket) {
        self.total_packets += 1;
        self.total_bytes += ethernet.packet().len() as u64;

        match ethernet.get_ethertype() {
            EtherTypes::Ipv4 => {
                if let Some(ipv4) = Ipv4Packet::new(ethernet.payload()) {
                    self.process_ipv4_packet(&ipv4);
                }
            }
            _ => {}
        }
    }

    fn process_ipv4_packet(&mut self, ipv4: &Ipv4Packet) {
        let src_ip = ipv4.get_source();
        let dst_ip = ipv4.get_destination();
        let protocol = ipv4.get_next_level_protocol();

        match protocol {
            IpNextHeaderProtocols::Tcp => {
                if let Some(tcp) = TcpPacket::new(ipv4.payload()) {
                    self.tcp_packets += 1;
                    self.process_tcp_packet(&tcp, src_ip, dst_ip);
                }
            }
            IpNextHeaderProtocols::Udp => {
                if let Some(udp) = UdpPacket::new(ipv4.payload()) {
                    self.udp_packets += 1;
                    self.process_udp_packet(&udp, src_ip, dst_ip);
                }
            }
            _ => {
                self.other_packets += 1;
            }
        }
    }

    fn process_tcp_packet(&mut self, tcp: &TcpPacket, src_ip: Ipv4Addr, dst_ip: Ipv4Addr) {
        let key = FlowKey {
            src_ip,
            dst_ip,
            src_port: tcp.get_source(),
            dst_port: tcp.get_destination(),
            protocol: 6, // TCP
        };

        let bytes = tcp.packet().len() as u64;
        self.flows.entry(key).or_insert_with(FlowStats::new).update(bytes);

        // Print TCP details
        println!("\n=== TCP Packet ===");
        println!("Source: {}:{}", src_ip, tcp.get_source());
        println!("Destination: {}:{}", dst_ip, tcp.get_destination());
        println!("Sequence: {}", tcp.get_sequence());
        println!("Acknowledgment: {}", tcp.get_acknowledgement());
        println!("Flags: {}", self.format_tcp_flags(tcp));
        println!("Window: {}", tcp.get_window());
        println!("Payload size: {} bytes", tcp.payload().len());
    }

    fn process_udp_packet(&mut self, udp: &UdpPacket, src_ip: Ipv4Addr, dst_ip: Ipv4Addr) {
        let key = FlowKey {
            src_ip,
            dst_ip,
            src_port: udp.get_source(),
            dst_port: udp.get_destination(),
            protocol: 17, // UDP
        };

        let bytes = udp.packet().len() as u64;
        self.flows.entry(key).or_insert_with(FlowStats::new).update(bytes);

        println!("\n=== UDP Packet ===");
        println!("Source: {}:{}", src_ip, udp.get_source());
        println!("Destination: {}:{}", dst_ip, udp.get_destination());
        println!("Length: {}", udp.get_length());
        println!("Payload size: {} bytes", udp.payload().len());
    }

    fn format_tcp_flags(&self, tcp: &TcpPacket) -> String {
        let mut flags = Vec::new();
        if tcp.get_flags() & 0x01 != 0 { flags.push("FIN"); }
        if tcp.get_flags() & 0x02 != 0 { flags.push("SYN"); }
        if tcp.get_flags() & 0x04 != 0 { flags.push("RST"); }
        if tcp.get_flags() & 0x08 != 0 { flags.push("PSH"); }
        if tcp.get_flags() & 0x10 != 0 { flags.push("ACK"); }
        if tcp.get_flags() & 0x20 != 0 { flags.push("URG"); }
        flags.join(" ")
    }

    fn print_statistics(&self) {
        println!("\n========== Capture Statistics ==========");
        println!("Total Packets: {}", self.total_packets);
        println!("Total Bytes: {} ({:.2} MB)", self.total_bytes, 
                 self.total_bytes as f64 / (1024.0 * 1024.0));
        println!("TCP Packets: {} ({:.2}%)", self.tcp_packets,
                 (self.tcp_packets as f64 / self.total_packets as f64) * 100.0);
        println!("UDP Packets: {} ({:.2}%)", self.udp_packets,
                 (self.udp_packets as f64 / self.total_packets as f64) * 100.0);
        println!("Other Packets: {} ({:.2}%)", self.other_packets,
                 (self.other_packets as f64 / self.total_packets as f64) * 100.0);
        println!("========================================");
    }

    fn print_flow_statistics(&self) {
        println!("\n========== Flow Statistics ==========");
        println!("Total Flows: {}", self.flows.len());

        let mut sorted_flows: Vec<_> = self.flows.iter().collect();
        sorted_flows.sort_by(|a, b| b.1.byte_count.cmp(&a.1.byte_count));

        println!("\nTop 10 Flows by Bytes:");
        for (i, (key, stats)) in sorted_flows.iter().take(10).enumerate() {
            println!("\n{}. {}", i + 1, key.to_string());
            println!("   Packets: {}", stats.packet_count);
            println!("   Bytes: {}", stats.byte_count);
            println!("   Duration: {:.2} seconds", stats.duration().as_secs_f64());
            println!("   Bandwidth: {:.2} Mbps", stats.bandwidth_mbps());
        }
        println!("====================================");
    }
}

fn main() {
    // Get available network interfaces
    let interfaces = datalink::interfaces();
    
    // Find the first non-loopback interface
    let interface = interfaces
        .into_iter()
        .find(|iface| !iface.is_loopback() && iface.is_up())
        .expect("No suitable network interface found");

    println!("Capturing on interface: {}", interface.name);
    println!("Press Ctrl+C to stop...\n");

    // Create a channel to receive packets
    let (_, mut rx) = match datalink::channel(&interface, Default::default()) {
        Ok(Channel::Ethernet(tx, rx)) => (tx, rx),
        Ok(_) => panic!("Unhandled channel type"),
        Err(e) => panic!("Failed to create datalink channel: {}", e),
    };

    let mut analyzer = PacketAnalyzer::new();
    let mut packet_count = 0;
    let max_packets = 100; // Capture 100 packets for demonstration

    loop {
        match rx.next() {
            Ok(packet) => {
                if let Some(ethernet) = EthernetPacket::new(packet) {
                    analyzer.analyze_packet(&ethernet);
                    packet_count += 1;
                    
                    if packet_count >= max_packets {
                        break;
                    }
                }
            }
            Err(e) => {
                eprintln!("Error receiving packet: {}", e);
            }
        }
    }

    analyzer.print_statistics();
    analyzer.print_flow_statistics();
}
```

### Advanced Rust Pattern Matching and DPI

```rust
use pnet::packet::Packet;

struct ProtocolDetector;

impl ProtocolDetector {
    fn detect_http(payload: &[u8]) -> bool {
        if payload.len() < 4 {
            return false;
        }
        
        let http_methods = [
            b"GET ", b"POST", b"PUT ", b"HEAD", 
            b"DELE", b"OPTI", b"PATC", b"TRAC"
        ];
        
        http_methods.iter().any(|method| payload.starts_with(method))
    }

    fn detect_tls(payload: &[u8]) -> bool {
        if payload.len() < 3 {
            return false;
        }
        
        // TLS record header: content type (0x16 for handshake) + version
        payload[0] == 0x16 && 
        payload[1] == 0x03 && 
        (payload[2] >= 0x01 && payload[2] <= 0x03)
    }

    fn detect_dns(payload: &[u8]) -> bool {
        if payload.len() < 12 {
            return false;
        }
        
        // DNS has specific flags in bytes 2-3
        let flags = u16::from_be_bytes([payload[2], payload[3]]);
        let qr = (flags >> 15) & 0x01;
        let opcode = (flags >> 11) & 0x0F;
        
        // Basic DNS validation
        opcode <= 2 && (qr == 0 || qr == 1)
    }

    fn analyze_application_layer(payload: &[u8], src_port: u16, dst_port: u16) -> String {
        if Self::detect_http(payload) {
            return "HTTP".to_string();
        }
        
        if Self::detect_tls(payload) {
            return "TLS/SSL".to_string();
        }
        
        if (src_port == 53 || dst_port == 53) && Self::detect_dns(payload) {
            return "DNS".to_string();
        }
        
        // Port-based detection as fallback
        match (src_port, dst_port) {
            (80, _) | (_, 80) => "HTTP (port)".to_string(),
            (443, _) | (_, 443) => "HTTPS (port)".to_string(),
            (22, _) | (_, 22) => "SSH".to_string(),
            (21, _) | (_, 21) => "FTP".to_string(),
            (25, _) | (_, 25) => "SMTP".to_string(),
            _ => "Unknown".to_string(),
        }
    }
}
```

## Summary

Packet capture and analysis is a critical skill for network engineering, security analysis, and system troubleshooting. Key takeaways include:

**Core Capabilities:**
- Intercepting network traffic at various layers using raw sockets and packet capture libraries
- Filtering packets efficiently using BPF to reduce overhead and focus on relevant traffic
- Parsing protocol headers (Ethernet, IP, TCP, UDP) to extract metadata and connection information
- Performing deep packet inspection to identify applications and detect anomalies

**Implementation Considerations:**
- **Performance**: Use kernel-level filtering (BPF) to minimize user-space processing overhead
- **Security**: Packet capture requires elevated privileges; implement proper access controls
- **Storage**: High-volume captures generate massive amounts of data; consider real-time analysis and selective storage
- **Privacy**: Be aware of legal and ethical implications when capturing network traffic

**Practical Applications:**
- Network troubleshooting and performance analysis
- Security monitoring and intrusion detection
- Protocol development and testing
- Traffic pattern analysis and bandwidth management
- Forensic analysis and compliance monitoring

Both C/C++ (with libpcap) and Rust (with pnet) provide powerful capabilities for packet analysis. C/C++ offers mature libraries and extensive platform support, while Rust provides memory safety guarantees and modern abstractions that prevent common security vulnerabilities in network code. The choice between them depends on your project requirements, existing codebase, and team expertise.