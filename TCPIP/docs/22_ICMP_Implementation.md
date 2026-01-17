# ICMP Implementation: Building Ping and Traceroute Tools

## Introduction

The Internet Control Message Protocol (ICMP) is a network layer protocol used by network devices to send error messages and operational information. Unlike TCP and UDP, ICMP is not typically used to exchange data between systems, but rather to communicate network-level information such as unreachable hosts, packet loss, and network congestion.

ICMP is fundamental to network diagnostics, powering essential tools like `ping` (which tests connectivity) and `traceroute` (which maps the path packets take through a network). Implementing these tools requires understanding raw sockets, packet construction, and network byte ordering.

## ICMP Packet Structure

An ICMP packet consists of a simple header followed by data:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Code      |          Checksum             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Rest of Header                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Data Section                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Common ICMP Types:**
- Type 0: Echo Reply (ping response)
- Type 8: Echo Request (ping request)
- Type 3: Destination Unreachable
- Type 11: Time Exceeded (used in traceroute)

## Building a Ping Tool

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>

#define PACKET_SIZE 64
#define TIMEOUT_SEC 2

// Calculate checksum for ICMP packet
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    
    if (len == 1)
        sum += *(unsigned char *)buf;
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    
    return result;
}

// Send ICMP echo request
int send_ping(int sockfd, struct sockaddr_in *addr, int seq) {
    char packet[PACKET_SIZE];
    struct icmp *icmp_hdr = (struct icmp *)packet;
    
    memset(packet, 0, PACKET_SIZE);
    
    // Fill in ICMP header
    icmp_hdr->icmp_type = ICMP_ECHO;
    icmp_hdr->icmp_code = 0;
    icmp_hdr->icmp_id = getpid();
    icmp_hdr->icmp_seq = seq;
    
    // Fill data section with pattern
    for (int i = sizeof(struct icmp); i < PACKET_SIZE; i++)
        packet[i] = i;
    
    // Calculate checksum
    icmp_hdr->icmp_cksum = 0;
    icmp_hdr->icmp_cksum = checksum(packet, PACKET_SIZE);
    
    // Send packet
    if (sendto(sockfd, packet, PACKET_SIZE, 0, 
               (struct sockaddr *)addr, sizeof(*addr)) <= 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

// Receive ICMP echo reply
int receive_ping(int sockfd, struct sockaddr_in *addr, double *rtt) {
    char buffer[1024];
    struct timeval tv_start, tv_end;
    socklen_t addr_len = sizeof(*addr);
    
    gettimeofday(&tv_start, NULL);
    
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)addr, &addr_len);
    
    gettimeofday(&tv_end, NULL);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Request timeout\n");
            return -1;
        }
        perror("recvfrom failed");
        return -1;
    }
    
    // Calculate round-trip time
    *rtt = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0 +
           (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    
    // Extract IP and ICMP headers
    struct ip *ip_hdr = (struct ip *)buffer;
    int ip_hdr_len = ip_hdr->ip_hl << 2;
    struct icmp *icmp_hdr = (struct icmp *)(buffer + ip_hdr_len);
    
    // Verify it's an echo reply for our process
    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY && 
        icmp_hdr->icmp_id == getpid()) {
        return icmp_hdr->icmp_seq;
    }
    
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
        return 1;
    }
    
    // Resolve hostname
    struct hostent *host = gethostbyname(argv[1]);
    if (!host) {
        fprintf(stderr, "Unknown host: %s\n", argv[1]);
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = *(struct in_addr *)host->h_addr;
    
    printf("PING %s (%s): %d data bytes\n", 
           argv[1], inet_ntoa(addr.sin_addr), PACKET_SIZE);
    
    // Create raw socket (requires root/CAP_NET_RAW)
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket failed (need root privileges)");
        return 1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Send pings
    int packets_sent = 0, packets_received = 0;
    double total_rtt = 0.0;
    
    for (int seq = 0; seq < 4; seq++) {
        if (send_ping(sockfd, &addr, seq) < 0)
            continue;
        
        packets_sent++;
        
        double rtt;
        int recv_seq = receive_ping(sockfd, &addr, &rtt);
        
        if (recv_seq >= 0) {
            packets_received++;
            total_rtt += rtt;
            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\n",
                   PACKET_SIZE, inet_ntoa(addr.sin_addr), recv_seq, 64, rtt);
        }
        
        sleep(1);
    }
    
    // Print statistics
    printf("\n--- %s ping statistics ---\n", argv[1]);
    printf("%d packets transmitted, %d received, %.0f%% packet loss\n",
           packets_sent, packets_received, 
           100.0 * (packets_sent - packets_received) / packets_sent);
    
    if (packets_received > 0) {
        printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
               total_rtt / packets_received,
               total_rtt / packets_received,
               total_rtt / packets_received);
    }
    
    close(sockfd);
    return 0;
}
```

### Rust Implementation

```rust
use std::net::{IpAddr, ToSocketAddrs};
use std::time::{Duration, Instant};
use std::io;

// ICMP Echo Request packet structure
#[repr(C, packed)]
struct IcmpEchoPacket {
    icmp_type: u8,     // Type 8 for echo request
    code: u8,          // Code 0
    checksum: u16,     // Checksum
    identifier: u16,   // Process ID
    sequence: u16,     // Sequence number
    data: [u8; 56],    // Payload data
}

impl IcmpEchoPacket {
    fn new(identifier: u16, sequence: u16) -> Self {
        let mut packet = IcmpEchoPacket {
            icmp_type: 8,
            code: 0,
            checksum: 0,
            identifier: identifier.to_be(),
            sequence: sequence.to_be(),
            data: [0u8; 56],
        };
        
        // Fill data with pattern
        for (i, byte) in packet.data.iter_mut().enumerate() {
            *byte = i as u8;
        }
        
        packet.checksum = packet.calculate_checksum();
        packet
    }
    
    fn calculate_checksum(&self) -> u16 {
        let bytes = unsafe {
            std::slice::from_raw_parts(
                self as *const _ as *const u8,
                std::mem::size_of::<IcmpEchoPacket>(),
            )
        };
        
        let mut sum: u32 = 0;
        let mut i = 0;
        
        // Sum 16-bit words
        while i < bytes.len() - 1 {
            let word = ((bytes[i] as u32) << 8) | (bytes[i + 1] as u32);
            sum += word;
            i += 2;
        }
        
        // Add remaining byte if odd length
        if i < bytes.len() {
            sum += (bytes[i] as u32) << 8;
        }
        
        // Fold 32-bit sum to 16 bits
        while sum >> 16 != 0 {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        !sum as u16
    }
    
    fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                self as *const _ as *const u8,
                std::mem::size_of::<IcmpEchoPacket>(),
            )
        }
    }
}

// ICMP Echo Reply parsing
#[repr(C, packed)]
struct IcmpEchoReply {
    icmp_type: u8,
    code: u8,
    checksum: u16,
    identifier: u16,
    sequence: u16,
}

fn send_ping(
    socket: &socket2::Socket,
    addr: &socket2::SockAddr,
    identifier: u16,
    sequence: u16,
) -> io::Result<Instant> {
    let packet = IcmpEchoPacket::new(identifier, sequence);
    let send_time = Instant::now();
    
    socket.send_to(packet.as_bytes(), addr)?;
    
    Ok(send_time)
}

fn receive_ping(
    socket: &socket2::Socket,
    identifier: u16,
    send_time: Instant,
) -> io::Result<(u16, Duration)> {
    let mut buffer = [0u8; 1024];
    
    let (len, _) = socket.recv_from(&mut buffer)?;
    let recv_time = Instant::now();
    
    if len < 28 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Packet too short",
        ));
    }
    
    // Skip IP header (typically 20 bytes, but check IHL field)
    let ip_header_len = ((buffer[0] & 0x0F) * 4) as usize;
    
    if len < ip_header_len + 8 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "ICMP packet too short",
        ));
    }
    
    // Parse ICMP header
    let icmp_data = &buffer[ip_header_len..];
    let icmp_type = icmp_data[0];
    let icmp_id = u16::from_be_bytes([icmp_data[4], icmp_data[5]]);
    let icmp_seq = u16::from_be_bytes([icmp_data[6], icmp_data[7]]);
    
    // Verify it's an echo reply for our request
    if icmp_type == 0 && icmp_id == identifier {
        let rtt = recv_time.duration_since(send_time);
        Ok((icmp_seq, rtt))
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Not our echo reply",
        ))
    }
}

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() != 2 {
        eprintln!("Usage: {} <hostname>", args[0]);
        std::process::exit(1);
    }
    
    let host = &args[1];
    
    // Resolve hostname to IP address
    let addr = format!("{}:0", host)
        .to_socket_addrs()?
        .find(|addr| addr.is_ipv4())
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "Host not found"))?;
    
    println!("PING {} ({}): 64 data bytes", host, addr.ip());
    
    // Create raw ICMP socket (requires CAP_NET_RAW capability)
    let socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::RAW,
        Some(socket2::Protocol::ICMPV4),
    )?;
    
    // Set receive timeout
    socket.set_read_timeout(Some(Duration::from_secs(2)))?;
    
    let identifier = std::process::id() as u16;
    let sock_addr = socket2::SockAddr::from(addr);
    
    let mut packets_sent = 0;
    let mut packets_received = 0;
    let mut total_rtt = Duration::ZERO;
    
    for seq in 0..4 {
        let send_time = send_ping(&socket, &sock_addr, identifier, seq)?;
        packets_sent += 1;
        
        match receive_ping(&socket, identifier, send_time) {
            Ok((recv_seq, rtt)) => {
                packets_received += 1;
                total_rtt += rtt;
                println!(
                    "64 bytes from {}: icmp_seq={} ttl=64 time={:.3} ms",
                    addr.ip(),
                    recv_seq,
                    rtt.as_secs_f64() * 1000.0
                );
            }
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                println!("Request timeout for icmp_seq {}", seq);
            }
            Err(e) => {
                eprintln!("Error receiving: {}", e);
            }
        }
        
        std::thread::sleep(Duration::from_secs(1));
    }
    
    // Print statistics
    println!("\n--- {} ping statistics ---", host);
    println!(
        "{} packets transmitted, {} received, {:.0}% packet loss",
        packets_sent,
        packets_received,
        100.0 * (packets_sent - packets_received) as f64 / packets_sent as f64
    );
    
    if packets_received > 0 {
        let avg_rtt = total_rtt / packets_received;
        println!(
            "rtt min/avg/max = {:.3}/{:.3}/{:.3} ms",
            avg_rtt.as_secs_f64() * 1000.0,
            avg_rtt.as_secs_f64() * 1000.0,
            avg_rtt.as_secs_f64() * 1000.0
        );
    }
    
    Ok(())
}

// Add to Cargo.toml:
// [dependencies]
// socket2 = "0.5"
```

## Building a Traceroute Tool

Traceroute works by sending packets with incrementally increasing TTL (Time To Live) values. Each router along the path decrements the TTL, and when it reaches zero, the router sends back an ICMP Time Exceeded message, revealing its identity.

### C/C++ Traceroute Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>

#define MAX_HOPS 30
#define PACKET_SIZE 64
#define TIMEOUT_SEC 2
#define DEST_PORT 33434  // Standard traceroute port

// Calculate checksum for ICMP packet
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    
    if (len == 1)
        sum += *(unsigned char *)buf;
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    
    return result;
}

// Send UDP probe packet with specific TTL
int send_probe(int sockfd, struct sockaddr_in *addr, int ttl, int seq) {
    char packet[PACKET_SIZE];
    
    // Set TTL for this packet
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt TTL failed");
        return -1;
    }
    
    memset(packet, 0, PACKET_SIZE);
    
    // Fill with sequence number for identification
    *(int *)packet = seq;
    
    struct sockaddr_in dest = *addr;
    dest.sin_port = htons(DEST_PORT + seq);
    
    if (sendto(sockfd, packet, PACKET_SIZE, 0, 
               (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

// Receive ICMP response (Time Exceeded or Dest Unreachable)
int receive_response(int icmp_sockfd, struct sockaddr_in *from_addr, 
                     double *rtt, int expected_seq) {
    char buffer[1024];
    struct timeval tv_start, tv_end;
    socklen_t addr_len = sizeof(*from_addr);
    
    gettimeofday(&tv_start, NULL);
    
    int n = recvfrom(icmp_sockfd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)from_addr, &addr_len);
    
    gettimeofday(&tv_end, NULL);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;  // Timeout
        }
        perror("recvfrom failed");
        return -1;
    }
    
    // Calculate RTT
    *rtt = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0 +
           (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    
    // Parse IP header
    struct ip *ip_hdr = (struct ip *)buffer;
    int ip_hdr_len = ip_hdr->ip_hl << 2;
    
    // Parse ICMP header
    struct icmp *icmp_hdr = (struct icmp *)(buffer + ip_hdr_len);
    
    // Check ICMP type
    if (icmp_hdr->icmp_type == ICMP_TIMXCEED) {
        return 0;  // Time exceeded - intermediate router
    } else if (icmp_hdr->icmp_type == ICMP_UNREACH) {
        return 1;  // Destination unreachable - final destination
    }
    
    return -1;  // Other ICMP type
}

// Resolve IP address to hostname
char* reverse_dns(struct in_addr addr) {
    static char hostname[256];
    struct hostent *host;
    
    host = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    
    if (host && host->h_name) {
        snprintf(hostname, sizeof(hostname), "%s (%s)", 
                 host->h_name, inet_ntoa(addr));
    } else {
        snprintf(hostname, sizeof(hostname), "%s", inet_ntoa(addr));
    }
    
    return hostname;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
        return 1;
    }
    
    // Resolve hostname
    struct hostent *host = gethostbyname(argv[1]);
    if (!host) {
        fprintf(stderr, "Unknown host: %s\n", argv[1]);
        return 1;
    }
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = *(struct in_addr *)host->h_addr;
    
    printf("traceroute to %s (%s), %d hops max, %d byte packets\n",
           argv[1], inet_ntoa(dest_addr.sin_addr), MAX_HOPS, PACKET_SIZE);
    
    // Create UDP socket for sending probes
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sockfd < 0) {
        perror("UDP socket creation failed");
        return 1;
    }
    
    // Create raw ICMP socket for receiving responses (requires root)
    int icmp_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_sockfd < 0) {
        perror("ICMP socket creation failed (need root privileges)");
        close(udp_sockfd);
        return 1;
    }
    
    // Set timeout on ICMP socket
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(icmp_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Traceroute loop
    int reached_dest = 0;
    
    for (int ttl = 1; ttl <= MAX_HOPS && !reached_dest; ttl++) {
        printf("%2d  ", ttl);
        fflush(stdout);
        
        int got_response = 0;
        struct sockaddr_in from_addr;
        double rtt;
        
        // Send 3 probes per hop
        for (int probe = 0; probe < 3; probe++) {
            int seq = ttl * 1000 + probe;
            
            if (send_probe(udp_sockfd, &dest_addr, ttl, seq) < 0) {
                printf("* ");
                continue;
            }
            
            int result = receive_response(icmp_sockfd, &from_addr, &rtt, seq);
            
            if (result >= 0) {
                if (!got_response) {
                    // Print hostname/IP for first response
                    char *hostname = reverse_dns(from_addr.sin_addr);
                    printf("%s  ", hostname);
                    got_response = 1;
                }
                
                printf("%.3f ms  ", rtt);
                
                // Check if we reached destination
                if (result == 1 || 
                    from_addr.sin_addr.s_addr == dest_addr.sin_addr.s_addr) {
                    reached_dest = 1;
                }
            } else {
                printf("* ");
            }
            
            fflush(stdout);
        }
        
        printf("\n");
    }
    
    close(udp_sockfd);
    close(icmp_sockfd);
    
    return 0;
}
```
### Rust Traceroute Implementation

```rust
use std::net::{IpAddr, Ipv4Addr, SocketAddr, ToSocketAddrs};
use std::time::{Duration, Instant};
use std::io;

const MAX_HOPS: u8 = 30;
const PACKET_SIZE: usize = 64;
const TIMEOUT: Duration = Duration::from_secs(2);
const DEST_PORT_BASE: u16 = 33434;

// ICMP header types
const ICMP_ECHOREPLY: u8 = 0;
const ICMP_DEST_UNREACH: u8 = 3;
const ICMP_TIME_EXCEEDED: u8 = 11;

fn send_probe(
    socket: &socket2::Socket,
    dest: &socket2::SockAddr,
    ttl: u8,
    seq: u32,
) -> io::Result<Instant> {
    // Set TTL for this probe
    socket.set_ttl(ttl as u32)?;
    
    // Create probe packet with sequence number
    let mut packet = vec![0u8; PACKET_SIZE];
    packet[0..4].copy_from_slice(&seq.to_be_bytes());
    
    let send_time = Instant::now();
    socket.send_to(&packet, dest)?;
    
    Ok(send_time)
}

fn receive_icmp_response(
    socket: &socket2::Socket,
    send_time: Instant,
) -> io::Result<(Ipv4Addr, Duration, bool)> {
    let mut buffer = [0u8; 1024];
    
    let (len, addr) = socket.recv_from(&mut buffer)?;
    let recv_time = Instant::now();
    let rtt = recv_time.duration_since(send_time);
    
    if len < 28 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Packet too short",
        ));
    }
    
    // Parse IP header
    let ip_header_len = ((buffer[0] & 0x0F) * 4) as usize;
    
    if len < ip_header_len + 8 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "ICMP packet too short",
        ));
    }
    
    // Extract source IP from IP header (bytes 12-15)
    let src_ip = Ipv4Addr::new(
        buffer[12],
        buffer[13],
        buffer[14],
        buffer[15],
    );
    
    // Parse ICMP header
    let icmp_type = buffer[ip_header_len];
    
    // Determine if we reached the destination
    let is_destination = match icmp_type {
        ICMP_TIME_EXCEEDED => false,      // Intermediate hop
        ICMP_DEST_UNREACH => true,        // Destination reached
        ICMP_ECHOREPLY => true,           // Destination reached (if using ICMP)
        _ => false,
    };
    
    Ok((src_ip, rtt, is_destination))
}

fn resolve_hostname(ip: Ipv4Addr) -> String {
    // Attempt reverse DNS lookup
    match dns_lookup::lookup_addr(&IpAddr::V4(ip)) {
        Ok(hostname) => format!("{} ({})", hostname, ip),
        Err(_) => format!("{}", ip),
    }
}

fn traceroute(dest_ip: Ipv4Addr, dest_name: &str) -> io::Result<()> {
    println!(
        "traceroute to {} ({}), {} hops max, {} byte packets",
        dest_name, dest_ip, MAX_HOPS, PACKET_SIZE
    );
    
    // Create UDP socket for sending probes
    let udp_socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::DGRAM,
        Some(socket2::Protocol::UDP),
    )?;
    
    // Create raw ICMP socket for receiving responses
    let icmp_socket = socket2::Socket::new(
        socket2::Domain::IPV4,
        socket2::Type::RAW,
        Some(socket2::Protocol::ICMPV4),
    )?;
    
    icmp_socket.set_read_timeout(Some(TIMEOUT))?;
    
    let mut reached_dest = false;
    
    for ttl in 1..=MAX_HOPS {
        if reached_dest {
            break;
        }
        
        print!("{:2}  ", ttl);
        
        let mut hop_responded = false;
        let mut hop_ip: Option<Ipv4Addr> = None;
        
        // Send 3 probes per hop
        for probe in 0..3 {
            let seq = (ttl as u32) * 1000 + probe;
            let port = DEST_PORT_BASE + seq as u16;
            
            let dest_sock_addr = socket2::SockAddr::from(
                SocketAddr::new(IpAddr::V4(dest_ip), port)
            );
            
            match send_probe(&udp_socket, &dest_sock_addr, ttl, seq) {
                Ok(send_time) => {
                    match receive_icmp_response(&icmp_socket, send_time) {
                        Ok((from_ip, rtt, is_dest)) => {
                            // Print hostname on first response from this hop
                            if !hop_responded {
                                let hostname = resolve_hostname(from_ip);
                                print!("{}  ", hostname);
                                hop_responded = true;
                                hop_ip = Some(from_ip);
                            }
                            
                            print!("{:.3} ms  ", rtt.as_secs_f64() * 1000.0);
                            
                            if is_dest || from_ip == dest_ip {
                                reached_dest = true;
                            }
                        }
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                            print!("* ");
                        }
                        Err(_) => {
                            print!("* ");
                        }
                    }
                }
                Err(_) => {
                    print!("* ");
                }
            }
        }
        
        println!();
    }
    
    Ok(())
}

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() != 2 {
        eprintln!("Usage: {} <hostname>", args[0]);
        std::process::exit(1);
    }
    
    let host = &args[1];
    
    // Resolve hostname to IP address
    let addr = format!("{}:0", host)
        .to_socket_addrs()?
        .find(|addr| addr.is_ipv4())
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "Host not found"))?;
    
    let dest_ip = match addr.ip() {
        IpAddr::V4(ip) => ip,
        _ => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "IPv6 not supported",
            ))
        }
    };
    
    traceroute(dest_ip, host)?;
    
    Ok(())
}

// Add to Cargo.toml:
// [dependencies]
// socket2 = "0.5"
// dns-lookup = "2.0"
```

## Key Implementation Concepts

### Raw Sockets and Privileges

Both ping and traceroute require raw socket access, which typically requires elevated privileges:

**Linux/Unix:**
- Run with `sudo` or as root
- Or set capabilities: `sudo setcap cap_net_raw+ep ./ping`

**Windows:**
- Run as Administrator
- Or use `IcmpCreateFile()` and `IcmpSendEcho()` from `iphlpapi.dll`

### ICMP Checksum Calculation

The checksum is critical for ICMP packets. It's computed by:
1. Setting checksum field to zero
2. Summing all 16-bit words in the packet
3. Adding any carry bits back into the sum
4. Taking the one's complement

### TTL Manipulation in Traceroute

Traceroute works by exploiting the TTL field in IP packets. Each router decrements TTL by 1, and when it reaches 0, the router sends back an ICMP Time Exceeded message. By incrementally increasing TTL (1, 2, 3...), we discover each hop along the route.

### UDP vs ICMP Probes

Traditional Unix traceroute uses UDP packets to high-numbered ports, while Windows tracert uses ICMP Echo Requests. Both work, but UDP is preferred because:
- Less likely to be filtered by firewalls
- Easier to distinguish responses
- Standard behavior for most traceroute implementations

### Packet Identification

To match responses to requests:
- **Ping**: Uses ICMP identifier (usually process ID) and sequence number
- **Traceroute**: Uses destination port number or ICMP sequence to correlate probes with responses

## Summary

ICMP implementation provides foundational network diagnostic capabilities. Building ping and traceroute tools teaches essential concepts including raw socket programming, packet construction, checksum calculation, and network protocol analysis. The ping tool measures round-trip latency by sending ICMP Echo Request packets and timing the replies, while traceroute maps network paths by incrementally increasing packet TTL values to elicit ICMP Time Exceeded messages from intermediate routers.

Key takeaways include understanding that raw sockets require elevated privileges, ICMP checksums must be correctly calculated for packets to be accepted, proper timeout handling is crucial for reliable network tools, and byte ordering (network vs host) must be carefully managed in packet structures. These implementations form the basis for more sophisticated network monitoring and diagnostic tools used in production environments.

Both C/C++ and Rust implementations demonstrate similar architectural patterns but with different safety guarantees and error handling approaches, with Rust providing memory safety without garbage collection while C offers direct low-level control over packet structures.