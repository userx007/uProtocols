# Raw Sockets: Creating and Using Raw Sockets for Custom Protocol Implementation

## Overview

Raw sockets provide direct access to the underlying network protocols, bypassing the typical transport layer (TCP/UDP). They allow you to craft custom packets with complete control over headers at the IP layer and below, making them essential for network diagnostics tools, packet sniffers, custom protocol implementations, and security applications.

## What Are Raw Sockets?

Raw sockets differ from standard stream (TCP) or datagram (UDP) sockets in several key ways:

- **Direct IP Access**: You can read and write packets at the IP layer, including full control over IP headers
- **Protocol Flexibility**: Implement custom protocols or work with protocols not supported by standard sockets
- **Packet Crafting**: Construct packets with custom headers for testing, diagnostics, or specialized applications
- **Privileged Access**: Typically require root/administrator privileges due to security implications

Common use cases include implementing ICMP (ping), creating packet sniffers, network scanning tools, VPN implementations, and custom routing protocols.

## Creating Raw Sockets

Raw sockets are created using the `socket()` system call with the `SOCK_RAW` type and specifying the protocol number.

## C/C++ Implementation

### Basic Raw Socket Creation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <errno.h>

// Checksum calculation for ICMP
unsigned short calculate_checksum(unsigned short *buf, int len) {
    unsigned long sum = 0;
    
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(unsigned char *)buf;
    }
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    
    return (unsigned short)(~sum);
}

// ICMP Echo Request (Ping) Implementation
int send_icmp_echo(const char *dest_ip) {
    int sockfd;
    struct sockaddr_in dest_addr;
    struct icmphdr icmp_hdr;
    char packet[64];
    
    // Create raw socket for ICMP
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("Socket creation failed (need root privileges)");
        return -1;
    }
    
    // Set up destination address
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
    
    // Construct ICMP header
    memset(&icmp_hdr, 0, sizeof(icmp_hdr));
    icmp_hdr.type = ICMP_ECHO;
    icmp_hdr.code = 0;
    icmp_hdr.un.echo.id = getpid();
    icmp_hdr.un.echo.sequence = 1;
    
    // Prepare packet
    memset(packet, 0, sizeof(packet));
    memcpy(packet, &icmp_hdr, sizeof(icmp_hdr));
    
    // Add some payload
    const char *payload = "PING_DATA";
    memcpy(packet + sizeof(icmp_hdr), payload, strlen(payload));
    
    // Calculate checksum
    struct icmphdr *icmp_ptr = (struct icmphdr *)packet;
    icmp_ptr->checksum = calculate_checksum((unsigned short *)packet, 
                                           sizeof(icmp_hdr) + strlen(payload));
    
    // Send packet
    ssize_t sent = sendto(sockfd, packet, sizeof(icmp_hdr) + strlen(payload), 0,
                         (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    if (sent < 0) {
        perror("sendto failed");
        close(sockfd);
        return -1;
    }
    
    printf("ICMP Echo Request sent to %s (%zd bytes)\n", dest_ip, sent);
    
    // Receive response
    char recv_buf[1024];
    struct sockaddr_in recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    
    ssize_t recv_len = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0,
                               (struct sockaddr *)&recv_addr, &addr_len);
    
    if (recv_len > 0) {
        struct iphdr *ip_hdr = (struct iphdr *)recv_buf;
        struct icmphdr *recv_icmp = (struct icmphdr *)(recv_buf + (ip_hdr->ihl * 4));
        
        if (recv_icmp->type == ICMP_ECHOREPLY) {
            printf("Received ICMP Echo Reply from %s\n", 
                   inet_ntoa(recv_addr.sin_addr));
        }
    }
    
    close(sockfd);
    return 0;
}
```

### Custom IP Header Construction

```c
#include <netinet/ip.h>
#include <netinet/tcp.h>

// Structure for pseudo header (used in TCP/UDP checksum calculation)
struct pseudo_header {
    uint32_t source_address;
    uint32_t dest_address;
    uint8_t placeholder;
    uint8_t protocol;
    uint16_t tcp_length;
};

int send_custom_ip_packet(const char *src_ip, const char *dest_ip) {
    int sockfd;
    char packet[4096];
    struct sockaddr_in dest_addr;
    int one = 1;
    
    // Create raw socket
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sockfd < 0) {
        perror("Raw socket creation failed");
        return -1;
    }
    
    // Tell kernel we'll provide IP header
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt IP_HDRINCL failed");
        close(sockfd);
        return -1;
    }
    
    // Zero out packet buffer
    memset(packet, 0, sizeof(packet));
    
    // Construct IP header
    struct iphdr *iph = (struct iphdr *)packet;
    iph->ihl = 5;  // Header length (5 * 4 = 20 bytes)
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
    iph->id = htons(54321);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_TCP;
    iph->check = 0;  // Will be filled by kernel
    iph->saddr = inet_addr(src_ip);
    iph->daddr = inet_addr(dest_ip);
    
    // Construct TCP header (simplified example)
    struct tcphdr *tcph = (struct tcphdr *)(packet + sizeof(struct iphdr));
    tcph->source = htons(12345);
    tcph->dest = htons(80);
    tcph->seq = 0;
    tcph->ack_seq = 0;
    tcph->doff = 5;  // TCP header size
    tcph->syn = 1;
    tcph->window = htons(5840);
    tcph->check = 0;
    tcph->urg_ptr = 0;
    
    // Calculate TCP checksum with pseudo header
    struct pseudo_header psh;
    psh.source_address = inet_addr(src_ip);
    psh.dest_address = inet_addr(dest_ip);
    psh.placeholder = 0;
    psh.protocol = IPPROTO_TCP;
    psh.tcp_length = htons(sizeof(struct tcphdr));
    
    int psize = sizeof(struct pseudo_header) + sizeof(struct tcphdr);
    char *pseudogram = malloc(psize);
    memcpy(pseudogram, &psh, sizeof(struct pseudo_header));
    memcpy(pseudogram + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr));
    
    tcph->check = calculate_checksum((unsigned short *)pseudogram, psize);
    free(pseudogram);
    
    // Set up destination
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
    
    // Send packet
    if (sendto(sockfd, packet, iph->tot_len, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("sendto failed");
        close(sockfd);
        return -1;
    }
    
    printf("Custom IP packet sent successfully\n");
    close(sockfd);
    return 0;
}
```

### Packet Sniffer Example

```c
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>

void packet_sniffer() {
    int sockfd;
    unsigned char buffer[65536];
    struct sockaddr saddr;
    socklen_t saddr_len = sizeof(saddr);
    
    // Create raw socket to capture all packets
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) {
        perror("Socket creation failed");
        return;
    }
    
    printf("Starting packet capture...\n");
    
    while (1) {
        ssize_t data_size = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                     &saddr, &saddr_len);
        
        if (data_size < 0) {
            perror("recvfrom failed");
            break;
        }
        
        // Parse Ethernet header
        struct ethhdr *eth = (struct ethhdr *)buffer;
        
        printf("\n----- Packet Captured -----\n");
        printf("Ethernet Header:\n");
        printf("  Dest MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
               eth->h_dest[0], eth->h_dest[1], eth->h_dest[2],
               eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);
        printf("  Src MAC:  %02x:%02x:%02x:%02x:%02x:%02x\n",
               eth->h_source[0], eth->h_source[1], eth->h_source[2],
               eth->h_source[3], eth->h_source[4], eth->h_source[5]);
        printf("  Protocol: 0x%04x\n", ntohs(eth->h_proto));
        
        // Parse IP header if it's an IP packet
        if (ntohs(eth->h_proto) == ETH_P_IP) {
            struct iphdr *iph = (struct iphdr *)(buffer + sizeof(struct ethhdr));
            struct in_addr src, dest;
            src.s_addr = iph->saddr;
            dest.s_addr = iph->daddr;
            
            printf("IP Header:\n");
            printf("  Source IP: %s\n", inet_ntoa(src));
            printf("  Dest IP:   %s\n", inet_ntoa(dest));
            printf("  Protocol:  %d\n", iph->protocol);
        }
    }
    
    close(sockfd);
}
```

## Rust Implementation

### ICMP Echo Request (Ping)

```rust
use std::mem;
use std::net::Ipv4Addr;
use std::os::unix::io::AsRawFd;
use libc::{
    socket, sendto, recvfrom, close, sockaddr_in, AF_INET, SOCK_RAW,
    IPPROTO_ICMP, sockaddr, socklen_t, c_void
};

#[repr(C)]
#[derive(Debug, Copy, Clone)]
struct IcmpHeader {
    icmp_type: u8,
    code: u8,
    checksum: u16,
    id: u16,
    sequence: u16,
}

fn calculate_checksum(data: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut i = 0;
    
    while i < data.len() - 1 {
        let word = u16::from_ne_bytes([data[i], data[i + 1]]);
        sum += word as u32;
        i += 2;
    }
    
    if i < data.len() {
        sum += (data[i] as u32) << 8;
    }
    
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    !sum as u16
}

fn send_icmp_ping(dest_ip: &str) -> Result<(), Box<dyn std::error::Error>> {
    unsafe {
        // Create raw socket
        let sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if sockfd < 0 {
            return Err("Failed to create socket (need root privileges)".into());
        }
        
        // Parse destination IP
        let dest_addr: Ipv4Addr = dest_ip.parse()?;
        let mut dest_sockaddr: sockaddr_in = mem::zeroed();
        dest_sockaddr.sin_family = AF_INET as u16;
        dest_sockaddr.sin_addr.s_addr = u32::from(dest_addr).to_be();
        
        // Build ICMP packet
        let mut icmp = IcmpHeader {
            icmp_type: 8,  // Echo Request
            code: 0,
            checksum: 0,
            id: std::process::id() as u16,
            sequence: 1,
        };
        
        let payload = b"PING_DATA_FROM_RUST";
        let mut packet = Vec::new();
        
        // Add header
        let header_bytes = std::slice::from_raw_parts(
            &icmp as *const _ as *const u8,
            mem::size_of::<IcmpHeader>()
        );
        packet.extend_from_slice(header_bytes);
        packet.extend_from_slice(payload);
        
        // Calculate and set checksum
        let checksum = calculate_checksum(&packet);
        let icmp_mut = &mut packet[0..mem::size_of::<IcmpHeader>()];
        icmp_mut[2..4].copy_from_slice(&checksum.to_ne_bytes());
        
        // Send packet
        let sent = sendto(
            sockfd,
            packet.as_ptr() as *const c_void,
            packet.len(),
            0,
            &dest_sockaddr as *const _ as *const sockaddr,
            mem::size_of::<sockaddr_in>() as socklen_t,
        );
        
        if sent < 0 {
            close(sockfd);
            return Err("Failed to send packet".into());
        }
        
        println!("ICMP Echo Request sent to {} ({} bytes)", dest_ip, sent);
        
        // Receive response
        let mut recv_buf = [0u8; 1024];
        let mut recv_addr: sockaddr_in = mem::zeroed();
        let mut addr_len = mem::size_of::<sockaddr_in>() as socklen_t;
        
        let recv_len = recvfrom(
            sockfd,
            recv_buf.as_mut_ptr() as *mut c_void,
            recv_buf.len(),
            0,
            &mut recv_addr as *mut _ as *mut sockaddr,
            &mut addr_len,
        );
        
        if recv_len > 0 {
            println!("Received {} bytes", recv_len);
            
            // Skip IP header (typically 20 bytes)
            let ip_header_len = (recv_buf[0] & 0x0F) * 4;
            let icmp_start = ip_header_len as usize;
            
            if recv_buf[icmp_start] == 0 {  // Echo Reply
                println!("Received ICMP Echo Reply");
            }
        }
        
        close(sockfd);
    }
    
    Ok(())
}

fn main() {
    match send_icmp_ping("8.8.8.8") {
        Ok(_) => println!("Ping successful"),
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

### Safe Rust Wrapper Using Socket2

```rust
use socket2::{Socket, Domain, Type, Protocol};
use std::net::SocketAddr;
use std::time::Duration;

fn create_raw_icmp_socket() -> std::io::Result<Socket> {
    let socket = Socket::new(
        Domain::IPV4,
        Type::RAW,
        Some(Protocol::ICMPV4),
    )?;
    
    // Set receive timeout
    socket.set_read_timeout(Some(Duration::from_secs(5)))?;
    
    Ok(socket)
}

fn send_ping_safe(dest: &str) -> std::io::Result<()> {
    let socket = create_raw_icmp_socket()?;
    
    // ICMP Echo Request packet
    let mut packet = vec![
        8,    // Type: Echo Request
        0,    // Code
        0, 0, // Checksum (to be calculated)
        0, 0, // ID
        0, 1, // Sequence
    ];
    
    // Add payload
    packet.extend_from_slice(b"PING");
    
    // Calculate checksum
    let checksum = calculate_checksum(&packet);
    packet[2..4].copy_from_slice(&checksum.to_be_bytes());
    
    // Parse destination
    let dest_addr: SocketAddr = format!("{}:0", dest).parse()
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidInput, e))?;
    
    // Send packet
    socket.send_to(&packet, &dest_addr.into())?;
    println!("ICMP packet sent to {}", dest);
    
    // Receive response
    let mut buf = [0u8; 1024];
    let (size, _) = socket.recv_from(&mut buf)?;
    
    println!("Received {} bytes", size);
    
    Ok(())
}
```

### Custom Protocol Implementation

```rust
use std::io;

const CUSTOM_PROTOCOL: i32 = 253; // Reserved for experimentation

#[repr(C)]
struct CustomProtocolHeader {
    version: u8,
    msg_type: u8,
    payload_length: u16,
    sequence: u32,
}

fn send_custom_protocol_packet(
    dest: &str,
    msg_type: u8,
    payload: &[u8]
) -> io::Result<()> {
    unsafe {
        use libc::*;
        
        let sockfd = socket(AF_INET, SOCK_RAW, CUSTOM_PROTOCOL);
        if sockfd < 0 {
            return Err(io::Error::last_os_error());
        }
        
        // Construct custom header
        let header = CustomProtocolHeader {
            version: 1,
            msg_type,
            payload_length: payload.len() as u16,
            sequence: 42,
        };
        
        // Build complete packet
        let mut packet = Vec::new();
        let header_bytes = std::slice::from_raw_parts(
            &header as *const _ as *const u8,
            std::mem::size_of::<CustomProtocolHeader>()
        );
        packet.extend_from_slice(header_bytes);
        packet.extend_from_slice(payload);
        
        // Set up destination
        let dest_ip: std::net::Ipv4Addr = dest.parse()
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;
        
        let mut dest_addr: sockaddr_in = std::mem::zeroed();
        dest_addr.sin_family = AF_INET as u16;
        dest_addr.sin_addr.s_addr = u32::from(dest_ip).to_be();
        
        // Send packet
        let result = sendto(
            sockfd,
            packet.as_ptr() as *const c_void,
            packet.len(),
            0,
            &dest_addr as *const _ as *const sockaddr,
            std::mem::size_of::<sockaddr_in>() as socklen_t,
        );
        
        close(sockfd);
        
        if result < 0 {
            return Err(io::Error::last_os_error());
        }
        
        println!("Custom protocol packet sent ({} bytes)", result);
        Ok(())
    }
}
```

## Key Considerations

**Security and Privileges**: Raw sockets require elevated privileges (root on Unix, Administrator on Windows) because they can bypass normal protocol checks and potentially spoof packets. Always validate that your application genuinely needs raw socket access.

**Byte Order**: Network protocols use big-endian byte order. Use `htons()`, `htonl()`, `ntohs()`, `ntohl()` in C or `.to_be()`, `.from_be()` in Rust for proper conversion.

**Checksum Calculation**: Many protocols require correct checksums. The kernel may calculate IP checksums automatically, but you must calculate checksums for higher-layer protocols like ICMP, TCP, and UDP.

**Packet Filtering**: On receive, raw sockets may deliver all packets or only those matching the specified protocol. Use appropriate filtering to handle only relevant packets.

**Platform Differences**: Raw socket behavior varies between operating systems. Windows, Linux, and BSD systems have different capabilities and restrictions. Always test on your target platforms.

## Summary

Raw sockets provide powerful low-level network access for implementing custom protocols, network diagnostics, and packet manipulation. They enable complete control over packet construction from the IP layer up, making them invaluable for specialized networking applications. However, this power comes with responsibility: raw sockets require privileged access, careful attention to protocol details like byte ordering and checksums, and robust error handling. Both C/C++ and Rust provide mechanisms for working with raw sockets, with Rust offering additional safety through libraries like `socket2` while still allowing unsafe operations when necessary. Whether building a ping utility, packet sniffer, or custom protocol stack, understanding raw sockets is essential for advanced network programming.