# libpcap Programming: Detailed Guide

## Overview

**libpcap** (Packet Capture Library) is a portable C/C++ library that provides a high-level interface for capturing network packets. It's the foundation for many network analysis tools like tcpdump, Wireshark, and countless network monitoring applications. libpcap allows programs to bypass the normal network stack and capture raw packets directly from network interfaces.

## Core Concepts

### What libpcap Does

libpcap provides a system-independent interface for user-level packet capture. It abstracts the differences between various operating systems' packet capture mechanisms:

- **Linux**: Uses AF_PACKET sockets or PF_RING
- **BSD/macOS**: Uses BPF (Berkeley Packet Filter)
- **Windows**: Uses WinPcap/Npcap (libpcap ports)

### Key Features

- **Device enumeration**: Find available network interfaces
- **Packet filtering**: Use BPF syntax to filter packets efficiently
- **Promiscuous mode**: Capture all packets on the network segment
- **Live capture**: Capture packets in real-time
- **Offline analysis**: Read packets from saved capture files (pcap format)

### Berkeley Packet Filter (BPF)

BPF is a kernel-level packet filtering mechanism that allows efficient filtering without copying every packet to userspace. Filters are compiled into bytecode and executed in the kernel.

**Example BPF filter expressions**:
- `tcp port 80` - Capture HTTP traffic
- `host 192.168.1.1` - Packets to/from specific host
- `icmp` - Only ICMP packets
- `tcp and dst port 443` - HTTPS traffic

## C/C++ Implementation

### Basic Packet Capture Example

```c
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

// Packet handler callback
void packet_handler(u_char *user_data, 
                   const struct pcap_pkthdr *pkthdr, 
                   const u_char *packet) {
    
    printf("Packet captured: %d bytes\n", pkthdr->len);
    printf("Timestamp: %ld.%06ld\n", 
           pkthdr->ts.tv_sec, pkthdr->ts.tv_usec);
    
    // Parse Ethernet header
    struct ether_header *eth_header = (struct ether_header *)packet;
    
    // Check if it's an IP packet
    if (ntohs(eth_header->ether_type) == ETHERTYPE_IP) {
        struct ip *ip_header = (struct ip *)(packet + sizeof(struct ether_header));
        
        char src_ip[INET_ADDRSTRLEN];
        char dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        printf("IP: %s -> %s\n", src_ip, dst_ip);
        printf("Protocol: %d\n", ip_header->ip_p);
        
        // If TCP, parse TCP header
        if (ip_header->ip_p == IPPROTO_TCP) {
            struct tcphdr *tcp_header = 
                (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                 (ip_header->ip_hl * 4));
            printf("TCP: Port %d -> %d\n", 
                   ntohs(tcp_header->th_sport), 
                   ntohs(tcp_header->th_dport));
        }
    }
    printf("---\n");
}

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    char *dev;
    struct bpf_program fp;
    bpf_u_int32 net, mask;
    
    // Find default device
    dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return 1;
    }
    printf("Device: %s\n", dev);
    
    // Get network number and mask
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
        net = 0;
        mask = 0;
    }
    
    // Open device for capturing
    // Parameters: device, snaplen, promiscuous, timeout, errbuf
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    // Compile and apply filter (capture only TCP packets)
    if (pcap_compile(handle, &fp, "tcp", 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    // Capture packets (num_packets = -1 means capture indefinitely)
    pcap_loop(handle, 10, packet_handler, NULL);
    
    // Cleanup
    pcap_freecode(&fp);
    pcap_close(handle);
    
    return 0;
}
```

### Advanced: Saving Packets to File

```c
#include <pcap.h>
#include <stdio.h>
#include <time.h>

void save_packet_handler(u_char *dumper, 
                        const struct pcap_pkthdr *header,
                        const u_char *packet) {
    pcap_dump(dumper, header, packet);
}

int main() {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    pcap_dumper_t *dumper;
    char *dev = "eth0";
    
    // Open device
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }
    
    // Open dump file
    dumper = pcap_dump_open(handle, "capture.pcap");
    if (dumper == NULL) {
        fprintf(stderr, "Error opening dump file: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    printf("Capturing packets to capture.pcap...\n");
    
    // Capture 100 packets
    pcap_loop(handle, 100, save_packet_handler, (u_char *)dumper);
    
    pcap_dump_close(dumper);
    pcap_close(handle);
    
    printf("Capture complete.\n");
    return 0;
}
```

### Device Enumeration

```cpp
#include <pcap.h>
#include <iostream>
#include <vector>

class NetworkDevice {
public:
    std::string name;
    std::string description;
    std::vector<std::string> addresses;
};

std::vector<NetworkDevice> list_devices() {
    std::vector<NetworkDevice> devices;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs, *d;
    
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        std::cerr << "Error finding devices: " << errbuf << std::endl;
        return devices;
    }
    
    for (d = alldevs; d != NULL; d = d->next) {
        NetworkDevice dev;
        dev.name = d->name;
        dev.description = d->description ? d->description : "No description";
        
        // Get addresses
        for (pcap_addr_t *a = d->addresses; a != NULL; a = a->next) {
            if (a->addr->sa_family == AF_INET) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, 
                         &((struct sockaddr_in *)a->addr)->sin_addr,
                         ip, INET_ADDRSTRLEN);
                dev.addresses.push_back(ip);
            }
        }
        
        devices.push_back(dev);
    }
    
    pcap_freealldevs(alldevs);
    return devices;
}

int main() {
    auto devices = list_devices();
    
    std::cout << "Available network devices:\n";
    for (const auto &dev : devices) {
        std::cout << "Name: " << dev.name << "\n";
        std::cout << "Description: " << dev.description << "\n";
        std::cout << "Addresses: ";
        for (const auto &addr : dev.addresses) {
            std::cout << addr << " ";
        }
        std::cout << "\n---\n";
    }
    
    return 0;
}
```

## Rust Implementation

For Rust, we use the `pcap` crate which provides safe bindings to libpcap.

### Basic Packet Capture

```rust
use pcap::{Capture, Device};
use std::net::Ipv4Addr;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Get default device
    let device = Device::lookup()?
        .ok_or("No device available")?;
    
    println!("Using device: {}", device.name);
    
    // Open device for capturing
    let mut cap = Capture::from_device(device)?
        .promisc(true)
        .snaplen(65535)
        .timeout(1000)
        .open()?;
    
    // Apply BPF filter
    cap.filter("tcp port 80 or tcp port 443", true)?;
    
    println!("Starting capture...");
    
    // Capture 10 packets
    for _ in 0..10 {
        match cap.next_packet() {
            Ok(packet) => {
                println!("Received packet:");
                println!("  Length: {} bytes", packet.header.len);
                println!("  Captured: {} bytes", packet.header.caplen);
                println!("  Timestamp: {}.{:06}", 
                         packet.header.ts.tv_sec,
                         packet.header.ts.tv_usec);
                
                // Parse Ethernet frame
                if packet.data.len() >= 14 {
                    let ethertype = u16::from_be_bytes([
                        packet.data[12], 
                        packet.data[13]
                    ]);
                    
                    // IPv4 packet
                    if ethertype == 0x0800 && packet.data.len() >= 34 {
                        let src_ip = Ipv4Addr::new(
                            packet.data[26],
                            packet.data[27],
                            packet.data[28],
                            packet.data[29]
                        );
                        let dst_ip = Ipv4Addr::new(
                            packet.data[30],
                            packet.data[31],
                            packet.data[32],
                            packet.data[33]
                        );
                        
                        println!("  IP: {} -> {}", src_ip, dst_ip);
                        
                        let protocol = packet.data[23];
                        match protocol {
                            6 => println!("  Protocol: TCP"),
                            17 => println!("  Protocol: UDP"),
                            1 => println!("  Protocol: ICMP"),
                            _ => println!("  Protocol: {}", protocol),
                        }
                    }
                }
                println!("---");
            }
            Err(e) => {
                eprintln!("Error capturing packet: {}", e);
            }
        }
    }
    
    Ok(())
}
```

### Advanced: Packet Analysis with pnet

```rust
use pcap::{Capture, Device};
use pnet::packet::ethernet::{EthernetPacket, EtherTypes};
use pnet::packet::ip::IpNextHeaderProtocols;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::tcp::TcpPacket;
use pnet::packet::Packet;

fn analyze_packet(data: &[u8]) {
    if let Some(ethernet) = EthernetPacket::new(data) {
        println!("Ethernet Frame:");
        println!("  Source MAC: {}", ethernet.get_source());
        println!("  Dest MAC: {}", ethernet.get_destination());
        
        match ethernet.get_ethertype() {
            EtherTypes::Ipv4 => {
                if let Some(ipv4) = Ipv4Packet::new(ethernet.payload()) {
                    println!("IPv4 Packet:");
                    println!("  Source IP: {}", ipv4.get_source());
                    println!("  Dest IP: {}", ipv4.get_destination());
                    println!("  TTL: {}", ipv4.get_ttl());
                    
                    match ipv4.get_next_level_protocol() {
                        IpNextHeaderProtocols::Tcp => {
                            if let Some(tcp) = TcpPacket::new(ipv4.payload()) {
                                println!("TCP Segment:");
                                println!("  Source Port: {}", tcp.get_source());
                                println!("  Dest Port: {}", tcp.get_destination());
                                println!("  Seq: {}", tcp.get_sequence());
                                println!("  Ack: {}", tcp.get_acknowledgement());
                                println!("  Flags: SYN={} ACK={} FIN={} RST={}",
                                    tcp.get_flags() & 0x02 != 0,
                                    tcp.get_flags() & 0x10 != 0,
                                    tcp.get_flags() & 0x01 != 0,
                                    tcp.get_flags() & 0x04 != 0
                                );
                            }
                        }
                        IpNextHeaderProtocols::Udp => {
                            println!("UDP Datagram");
                        }
                        IpNextHeaderProtocols::Icmp => {
                            println!("ICMP Message");
                        }
                        _ => {}
                    }
                }
            }
            EtherTypes::Ipv6 => {
                println!("IPv6 Packet (not parsed)");
            }
            _ => {}
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let device = Device::lookup()?.unwrap();
    
    let mut cap = Capture::from_device(device)?
        .promisc(true)
        .snaplen(65535)
        .open()?;
    
    cap.filter("tcp", true)?;
    
    println!("Analyzing TCP packets...\n");
    
    while let Ok(packet) = cap.next_packet() {
        analyze_packet(packet.data);
        println!("---");
    }
    
    Ok(())
}
```

### Saving to PCAP File

```rust
use pcap::{Capture, Device, Savefile};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let device = Device::lookup()?.unwrap();
    
    let mut cap = Capture::from_device(device)?
        .promisc(true)
        .open()?;
    
    let mut savefile = cap.savefile("output.pcap")?;
    
    println!("Capturing 100 packets to output.pcap...");
    
    for i in 0..100 {
        match cap.next_packet() {
            Ok(packet) => {
                savefile.write(&packet);
                if (i + 1) % 10 == 0 {
                    println!("Captured {} packets", i + 1);
                }
            }
            Err(e) => {
                eprintln!("Error: {}", e);
            }
        }
    }
    
    println!("Capture complete!");
    
    Ok(())
}
```

### Reading from PCAP File

```rust
use pcap::Capture;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut cap = Capture::from_file("output.pcap")?;
    
    let mut packet_count = 0;
    let mut total_bytes = 0u64;
    
    while let Ok(packet) = cap.next_packet() {
        packet_count += 1;
        total_bytes += packet.header.len as u64;
        
        println!("Packet {}: {} bytes at {}.{:06}",
                 packet_count,
                 packet.header.len,
                 packet.header.ts.tv_sec,
                 packet.header.ts.tv_usec);
    }
    
    println!("\nStatistics:");
    println!("Total packets: {}", packet_count);
    println!("Total bytes: {}", total_bytes);
    println!("Average packet size: {:.2} bytes", 
             total_bytes as f64 / packet_count as f64);
    
    Ok(())
}
```

## Key API Functions

### C API

- `pcap_lookupdev()`: Find default capture device
- `pcap_findalldevs()`: List all available devices
- `pcap_open_live()`: Open device for live capture
- `pcap_open_offline()`: Open saved pcap file
- `pcap_compile()`: Compile BPF filter
- `pcap_setfilter()`: Apply compiled filter
- `pcap_loop()`: Process packets with callback
- `pcap_next()`: Get next packet
- `pcap_dump_open()`: Open file for writing
- `pcap_dump()`: Write packet to file
- `pcap_close()`: Close capture handle

### Rust Crate Methods

- `Device::lookup()`: Get default device
- `Capture::from_device()`: Create capture from device
- `Capture::from_file()`: Read from pcap file
- `.promisc()`: Set promiscuous mode
- `.snaplen()`: Set snapshot length
- `.timeout()`: Set read timeout
- `.filter()`: Apply BPF filter
- `.next_packet()`: Get next packet
- `.savefile()`: Create savefile for writing

## Summary

**libpcap** is the essential library for network packet capture and analysis, providing:

- **Cross-platform packet capture** with a unified API across operating systems
- **Efficient kernel-level filtering** using Berkeley Packet Filter (BPF) syntax
- **Live capture and offline analysis** capabilities for real-time monitoring and forensics
- **Foundation for network tools** like tcpdump, Wireshark, and custom network analyzers

**C/C++ implementation** offers direct access to the native libpcap API with maximum performance and control, requiring manual memory management and packet parsing.

**Rust implementation** provides safe, ergonomic bindings through the `pcap` crate, with additional parsing capabilities via `pnet`, preventing common bugs while maintaining performance.

Both implementations support device enumeration, BPF filtering, promiscuous mode capture, and reading/writing standard pcap file formats. libpcap remains the cornerstone of network analysis programming, enabling everything from simple packet sniffers to sophisticated intrusion detection systems.