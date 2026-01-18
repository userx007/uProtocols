# Jumbo Frames and MTU: Path MTU Discovery and Fragmentation Handling

## Overview

**Maximum Transmission Unit (MTU)** refers to the largest packet size that can be transmitted over a network link without fragmentation. The standard Ethernet MTU is 1500 bytes, but **jumbo frames** extend this to 9000 bytes or more, improving throughput for large data transfers by reducing packet overhead and CPU processing.

**Path MTU Discovery (PMTUD)** is a technique used to determine the smallest MTU along the network path between two hosts, preventing fragmentation and the associated performance penalties.

## Key Concepts

### MTU Sizes
- **Standard Ethernet**: 1500 bytes
- **Jumbo Frames**: Typically 9000 bytes (can range from 1501 to 9216 bytes)
- **Loopback**: Often 65536 bytes
- **PPPoE**: 1492 bytes (reduced due to PPPoE header overhead)

### Fragmentation
When a packet exceeds the MTU of a network link, it must be fragmented into smaller pieces. IPv4 allows routers to fragment packets, while IPv6 requires the source to perform fragmentation after discovering the path MTU.

### Path MTU Discovery
PMTUD works by:
1. Sending packets with the "Don't Fragment" (DF) bit set
2. Receiving ICMP "Fragmentation Needed" messages when packets are too large
3. Adjusting the packet size based on ICMP feedback
4. Caching the discovered MTU for future transmissions

## C/C++ Implementation

### Setting Socket MTU Options

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>

// Enable Path MTU Discovery
int enable_pmtud(int sockfd) {
    int val = IP_PMTUDISC_DO; // Enable PMTUD
    if (setsockopt(sockfd, IPPROTO_IP, IP_MTU_DISCOVER, 
                   &val, sizeof(val)) < 0) {
        perror("setsockopt IP_MTU_DISCOVER");
        return -1;
    }
    printf("Path MTU Discovery enabled\n");
    return 0;
}

// Get current Path MTU
int get_path_mtu(int sockfd) {
    int mtu;
    socklen_t mtu_len = sizeof(mtu);
    
    if (getsockopt(sockfd, IPPROTO_IP, IP_MTU, 
                   &mtu, &mtu_len) < 0) {
        perror("getsockopt IP_MTU");
        return -1;
    }
    return mtu;
}

// Set Don't Fragment flag
int set_dont_fragment(int sockfd) {
    int val = 1;
    
#ifdef __linux__
    // Linux uses IP_MTU_DISCOVER
    val = IP_PMTUDISC_DO;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MTU_DISCOVER, 
                   &val, sizeof(val)) < 0) {
        perror("setsockopt IP_MTU_DISCOVER");
        return -1;
    }
#else
    // BSD/macOS use IP_DONTFRAG
    if (setsockopt(sockfd, IPPROTO_IP, IP_DONTFRAG, 
                   &val, sizeof(val)) < 0) {
        perror("setsockopt IP_DONTFRAG");
        return -1;
    }
#endif
    
    printf("Don't Fragment flag set\n");
    return 0;
}

// Example: TCP socket with MTU discovery
int create_tcp_socket_with_pmtud(const char *host, int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Enable Path MTU Discovery
    if (enable_pmtud(sockfd) < 0) {
        close(sockfd);
        return -1;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);
    
    // Connect
    if (connect(sockfd, (struct sockaddr *)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    
    // Get and display Path MTU
    int mtu = get_path_mtu(sockfd);
    if (mtu > 0) {
        printf("Path MTU: %d bytes\n", mtu);
    }
    
    return sockfd;
}
```

### UDP with Manual MTU Handling

```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_MTU 1500
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
#define MAX_UDP_PAYLOAD (DEFAULT_MTU - IP_HEADER_SIZE - UDP_HEADER_SIZE)

typedef struct {
    int sockfd;
    int mtu;
    int max_payload;
} udp_mtu_context_t;

// Initialize UDP socket with MTU awareness
udp_mtu_context_t* create_udp_mtu_socket(int mtu) {
    udp_mtu_context_t *ctx = malloc(sizeof(udp_mtu_context_t));
    if (!ctx) return NULL;
    
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) {
        free(ctx);
        return NULL;
    }
    
    ctx->mtu = mtu > 0 ? mtu : DEFAULT_MTU;
    ctx->max_payload = ctx->mtu - IP_HEADER_SIZE - UDP_HEADER_SIZE;
    
    // Enable Path MTU Discovery
    int pmtu_mode = IP_PMTUDISC_DO;
    setsockopt(ctx->sockfd, IPPROTO_IP, IP_MTU_DISCOVER, 
               &pmtu_mode, sizeof(pmtu_mode));
    
    printf("UDP socket created with MTU: %d, max payload: %d\n", 
           ctx->mtu, ctx->max_payload);
    
    return ctx;
}

// Send data with automatic fragmentation
ssize_t udp_send_with_fragmentation(udp_mtu_context_t *ctx, 
                                     const char *data, size_t len,
                                     struct sockaddr_in *dest) {
    size_t offset = 0;
    ssize_t total_sent = 0;
    
    while (offset < len) {
        size_t chunk_size = (len - offset > ctx->max_payload) 
                            ? ctx->max_payload 
                            : (len - offset);
        
        ssize_t sent = sendto(ctx->sockfd, data + offset, chunk_size, 0,
                             (struct sockaddr *)dest, sizeof(*dest));
        
        if (sent < 0) {
            perror("sendto");
            return -1;
        }
        
        offset += sent;
        total_sent += sent;
        printf("Sent fragment: %zd bytes (offset: %zu)\n", sent, offset);
    }
    
    return total_sent;
}

// Example usage
int main() {
    udp_mtu_context_t *ctx = create_udp_mtu_socket(1500);
    if (!ctx) {
        fprintf(stderr, "Failed to create UDP socket\n");
        return 1;
    }
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.100", &dest_addr.sin_addr);
    
    // Send large data that exceeds MTU
    char large_data[5000];
    memset(large_data, 'A', sizeof(large_data));
    
    ssize_t sent = udp_send_with_fragmentation(ctx, large_data, 
                                               sizeof(large_data), &dest_addr);
    printf("Total sent: %zd bytes\n", sent);
    
    close(ctx->sockfd);
    free(ctx);
    return 0;
}
```

## Rust Implementation

### Basic MTU Discovery

```rust
use std::io;
use std::net::{TcpStream, UdpSocket, SocketAddr};
use std::os::unix::io::AsRawFd;

#[cfg(target_os = "linux")]
use libc::{setsockopt, getsockopt, IPPROTO_IP, IP_MTU_DISCOVER, IP_MTU, 
           IP_PMTUDISC_DO, SOL_SOCKET, socklen_t};

// Path MTU Discovery configuration
#[derive(Debug, Clone, Copy)]
pub enum PmtuMode {
    Want,      // Want PMTUD, but allow fragmentation as fallback
    Do,        // Do PMTUD, fail if packet is too large
    Dont,      // Don't do PMTUD
    Probe,     // Set DF but ignore Path MTU
}

impl PmtuMode {
    #[cfg(target_os = "linux")]
    fn to_libc(&self) -> i32 {
        match self {
            PmtuMode::Want => libc::IP_PMTUDISC_WANT,
            PmtuMode::Do => libc::IP_PMTUDISC_DO,
            PmtuMode::Dont => libc::IP_PMTUDISC_DONT,
            PmtuMode::Probe => libc::IP_PMTUDISC_PROBE,
        }
    }
}

// MTU utilities
pub struct MtuUtils;

impl MtuUtils {
    #[cfg(target_os = "linux")]
    pub fn enable_pmtud(socket: &TcpStream, mode: PmtuMode) -> io::Result<()> {
        let fd = socket.as_raw_fd();
        let mode_val = mode.to_libc();
        
        unsafe {
            let ret = setsockopt(
                fd,
                IPPROTO_IP,
                IP_MTU_DISCOVER,
                &mode_val as *const _ as *const libc::c_void,
                std::mem::size_of::<i32>() as socklen_t,
            );
            
            if ret < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        println!("Path MTU Discovery enabled with mode: {:?}", mode);
        Ok(())
    }
    
    #[cfg(target_os = "linux")]
    pub fn get_path_mtu(socket: &TcpStream) -> io::Result<i32> {
        let fd = socket.as_raw_fd();
        let mut mtu: i32 = 0;
        let mut mtu_len: socklen_t = std::mem::size_of::<i32>() as socklen_t;
        
        unsafe {
            let ret = getsockopt(
                fd,
                IPPROTO_IP,
                IP_MTU,
                &mut mtu as *mut _ as *mut libc::c_void,
                &mut mtu_len,
            );
            
            if ret < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        Ok(mtu)
    }
    
    // Calculate maximum payload for UDP
    pub fn calculate_udp_payload(mtu: usize) -> usize {
        const IP_HEADER: usize = 20;
        const UDP_HEADER: usize = 8;
        
        mtu.saturating_sub(IP_HEADER + UDP_HEADER)
    }
    
    // Calculate maximum payload for TCP
    pub fn calculate_tcp_payload(mtu: usize) -> usize {
        const IP_HEADER: usize = 20;
        const TCP_HEADER_MIN: usize = 20;
        
        mtu.saturating_sub(IP_HEADER + TCP_HEADER_MIN)
    }
}
```

### UDP Fragmenter with MTU Awareness

```rust
use std::net::{UdpSocket, SocketAddr};
use std::io::{self, Result};

pub struct UdpFragmenter {
    socket: UdpSocket,
    mtu: usize,
    max_payload: usize,
}

impl UdpFragmenter {
    pub fn new(bind_addr: &str, mtu: usize) -> Result<Self> {
        let socket = UdpSocket::bind(bind_addr)?;
        let max_payload = MtuUtils::calculate_udp_payload(mtu);
        
        println!("UDP Fragmenter created - MTU: {}, Max payload: {}", 
                 mtu, max_payload);
        
        Ok(UdpFragmenter {
            socket,
            mtu,
            max_payload,
        })
    }
    
    pub fn send_fragmented(&self, data: &[u8], dest: SocketAddr) -> Result<usize> {
        let mut total_sent = 0;
        let mut offset = 0;
        
        while offset < data.len() {
            let chunk_end = std::cmp::min(offset + self.max_payload, data.len());
            let chunk = &data[offset..chunk_end];
            
            let sent = self.socket.send_to(chunk, dest)?;
            total_sent += sent;
            offset += sent;
            
            println!("Sent fragment: {} bytes (offset: {})", sent, offset);
        }
        
        Ok(total_sent)
    }
    
    pub fn receive(&self, buffer: &mut [u8]) -> Result<(usize, SocketAddr)> {
        self.socket.recv_from(buffer)
    }
    
    pub fn mtu(&self) -> usize {
        self.mtu
    }
}
```

### Path MTU Discovery Implementation

```rust
use std::net::{TcpStream, SocketAddr};
use std::io::{self, Read, Write, Result};
use std::time::Duration;

pub struct PathMtuDiscovery {
    stream: TcpStream,
    current_mtu: usize,
    min_mtu: usize,
    max_mtu: usize,
}

impl PathMtuDiscovery {
    const DEFAULT_MTU: usize = 1500;
    const MIN_MTU: usize = 576;   // IPv4 minimum
    const MAX_MTU: usize = 9000;  // Jumbo frames
    
    pub fn connect(addr: SocketAddr) -> Result<Self> {
        let stream = TcpStream::connect(addr)?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        stream.set_write_timeout(Some(Duration::from_secs(5)))?;
        
        let mut pmtud = PathMtuDiscovery {
            stream,
            current_mtu: Self::DEFAULT_MTU,
            min_mtu: Self::MIN_MTU,
            max_mtu: Self::MAX_MTU,
        };
        
        #[cfg(target_os = "linux")]
        {
            // Enable PMTUD on the socket
            if let Err(e) = MtuUtils::enable_pmtud(&pmtud.stream, PmtuMode::Do) {
                eprintln!("Warning: Could not enable PMTUD: {}", e);
            }
            
            // Try to get actual path MTU
            if let Ok(mtu) = MtuUtils::get_path_mtu(&pmtud.stream) {
                pmtud.current_mtu = mtu as usize;
                println!("Discovered path MTU: {}", mtu);
            }
        }
        
        Ok(pmtud)
    }
    
    pub fn send_with_mtu_check(&mut self, data: &[u8]) -> Result<usize> {
        let max_payload = MtuUtils::calculate_tcp_payload(self.current_mtu);
        
        if data.len() > max_payload {
            println!("Data size {} exceeds max payload {}, fragmenting...", 
                     data.len(), max_payload);
            
            let mut total_sent = 0;
            for chunk in data.chunks(max_payload) {
                total_sent += self.stream.write(chunk)?;
            }
            Ok(total_sent)
        } else {
            self.stream.write(data)
        }
    }
    
    pub fn get_current_mtu(&self) -> usize {
        self.current_mtu
    }
    
    pub fn supports_jumbo_frames(&self) -> bool {
        self.current_mtu >= 9000
    }
}

// Example usage
fn main() -> Result<()> {
    let addr: SocketAddr = "127.0.0.1:8080".parse().unwrap();
    
    // Create fragmenter for sending
    let fragmenter = UdpFragmenter::new("0.0.0.0:0", 1500)?;
    
    let large_data = vec![0u8; 5000];
    let sent = fragmenter.send_fragmented(&large_data, addr)?;
    println!("Total bytes sent: {}", sent);
    
    // TCP with PMTUD
    let server_addr: SocketAddr = "93.184.216.34:80".parse().unwrap();
    let mut pmtud = PathMtuDiscovery::connect(server_addr)?;
    
    println!("Connected with MTU: {}", pmtud.get_current_mtu());
    println!("Supports jumbo frames: {}", pmtud.supports_jumbo_frames());
    
    let request = b"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    pmtud.send_with_mtu_check(request)?;
    
    Ok(())
}
```

## Summary

**MTU (Maximum Transmission Unit)** defines the largest packet size for network transmission, with standard Ethernet using 1500 bytes and jumbo frames extending up to 9000+ bytes for improved throughput. **Path MTU Discovery (PMTUD)** automatically determines the optimal packet size along a network path by setting the "Don't Fragment" flag and responding to ICMP feedback, preventing fragmentation that degrades performance.

In C/C++, MTU handling involves socket options like `IP_MTU_DISCOVER` (Linux) or `IP_DONTFRAG` (BSD/macOS) to enable PMTUD, and `IP_MTU` to query the discovered path MTU. Applications must manually fragment UDP datagrams exceeding the MTU by calculating usable payload space (MTU minus IP and UDP headers) and sending data in appropriately-sized chunks.

Rust implementations leverage platform-specific FFI calls through libc for low-level socket configuration while providing safe abstractions for MTU-aware communication. Both languages require careful consideration of header overhead (20 bytes IP + 8 bytes UDP or 20+ bytes TCP) when calculating maximum payload sizes, and proper error handling for PMTUD failures or ICMP messages indicating MTU limitations along the network path.