# TCP Segmentation Offload (TSO) and Generic Segmentation Offload (GSO)

## Overview

TSO (TCP Segmentation Offload) and GSO (Generic Segmentation Offload) are performance optimization techniques that offload the CPU-intensive task of segmenting large data packets into smaller, network-transmittable segments from the operating system kernel to the network interface card (NIC) hardware or lower network layers.

## What Problem Do They Solve?

When an application sends large amounts of data over TCP, the network stack must break that data into Maximum Transmission Unit (MTU)-sized segments (typically 1500 bytes for Ethernet). This segmentation process involves:

- Creating multiple TCP headers
- Calculating checksums for each segment
- Managing sequence numbers
- Copying data into properly sized buffers

This is CPU-intensive work. TSO/GSO offloads this work, allowing the CPU to hand off larger "super packets" (up to 64KB) to the NIC or lower layers, which then handle the segmentation.

## TSO vs GSO

**TSO (TCP Segmentation Offload)**: Hardware-based offloading where the NIC performs the actual segmentation. The NIC must support TSO in hardware.

**GSO (Generic Segmentation Offload)**: Software-based approach that delays segmentation until the last possible moment before the packet leaves the system. Works even when hardware doesn't support TSO. GSO is protocol-agnostic and supports TCP, UDP, and other protocols.

## How They Work

1. **Application sends large buffer** (e.g., 64KB) via `send()` or `write()`
2. **Kernel creates a large segment** instead of many small ones
3. **TSO/GSO enabled path**:
   - Single large packet traverses most of the network stack
   - Segmentation happens at NIC (TSO) or just before transmission (GSO)
4. **Result**: Fewer CPU cycles, fewer interrupts, better throughput

## Benefits

- **Reduced CPU usage**: One large packet instead of many small ones
- **Better cache utilization**: Fewer packet descriptors to process
- **Higher throughput**: Less overhead per byte transmitted
- **Lower latency**: Reduced processing time in network stack

## C/C++ Programming Examples

### Example 1: Checking and Enabling TSO/GSO (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Check if TSO is enabled on network interface
int check_tso_enabled(const char *interface) {
    int sock;
    struct ifreq ifr;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    
    // Note: Actual TSO status checking requires ethtool ioctl
    // This is simplified - in practice use ethtool commands
    
    close(sock);
    return 0;
}

// Create a socket optimized for large sends (TSO-friendly)
int create_optimized_tcp_socket() {
    int sockfd;
    int optval = 1;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Enable TCP_NODELAY to disable Nagle's algorithm
    // This works well with TSO for streaming large data
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, 
                   &optval, sizeof(optval)) < 0) {
        perror("TCP_NODELAY failed");
        close(sockfd);
        return -1;
    }
    
    // Set larger send buffer to allow kernel to build larger segments
    int sendbuf_size = 256 * 1024; // 256KB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, 
                   &sendbuf_size, sizeof(sendbuf_size)) < 0) {
        perror("SO_SNDBUF failed");
        // Non-fatal, continue
    }
    
    return sockfd;
}
```

### Example 2: Sending Large Data with TSO/GSO Benefits (C++)

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

class TSOOptimizedSender {
private:
    int sockfd;
    
public:
    TSOOptimizedSender() : sockfd(-1) {}
    
    ~TSOOptimizedSender() {
        if (sockfd >= 0) {
            close(sockfd);
        }
    }
    
    bool connect(const char* host, int port) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket");
            return false;
        }
        
        // Optimize for TSO
        configureTSO();
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
            perror("inet_pton");
            return false;
        }
        
        if (::connect(sockfd, (struct sockaddr*)&server_addr, 
                      sizeof(server_addr)) < 0) {
            perror("connect");
            return false;
        }
        
        return true;
    }
    
    void configureTSO() {
        // Disable Nagle's algorithm for better TSO performance
        int flag = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        
        // Increase send buffer size
        int sendbuf = 512 * 1024; // 512KB
        setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
        
        // Set TCP window scaling
        int window_size = 1024 * 1024; // 1MB
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &window_size, sizeof(window_size));
    }
    
    // Send large buffer - TSO will handle segmentation
    ssize_t sendLargeData(const std::vector<char>& data) {
        ssize_t total_sent = 0;
        size_t remaining = data.size();
        const char* ptr = data.data();
        
        while (remaining > 0) {
            // Send large chunks - let TSO/GSO handle the segmentation
            ssize_t sent = send(sockfd, ptr, remaining, 0);
            
            if (sent < 0) {
                if (errno == EINTR) continue;
                perror("send");
                return -1;
            }
            
            total_sent += sent;
            ptr += sent;
            remaining -= sent;
        }
        
        return total_sent;
    }
    
    // Send with sendmsg for more control
    ssize_t sendLargeDataScatter(const std::vector<std::vector<char>>& buffers) {
        std::vector<struct iovec> iov;
        
        for (const auto& buf : buffers) {
            struct iovec io;
            io.iov_base = (void*)buf.data();
            io.iov_len = buf.size();
            iov.push_back(io);
        }
        
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov.data();
        msg.msg_iovlen = iov.size();
        
        // TSO/GSO will coalesce these scattered buffers
        return sendmsg(sockfd, &msg, 0);
    }
};

int main() {
    TSOOptimizedSender sender;
    
    if (!sender.connect("127.0.0.1", 8080)) {
        return 1;
    }
    
    // Create large data buffer (64KB)
    std::vector<char> large_data(64 * 1024, 'X');
    
    std::cout << "Sending large data with TSO/GSO optimization..." << std::endl;
    
    ssize_t sent = sender.sendLargeData(large_data);
    
    if (sent > 0) {
        std::cout << "Successfully sent " << sent << " bytes" << std::endl;
    }
    
    return 0;
}
```

### Example 3: Monitoring TSO/GSO Statistics

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Get TSO/GSO statistics from network interface
void get_offload_stats(const char *interface) {
    int sock;
    struct ifreq ifr;
    struct ethtool_cmd edata;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    
    // Get ethtool data
    memset(&edata, 0, sizeof(edata));
    edata.cmd = ETHTOOL_GSET;
    ifr.ifr_data = (caddr_t)&edata;
    
    if (ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
        perror("ioctl SIOCETHTOOL");
        close(sock);
        return;
    }
    
    printf("Interface: %s\n", interface);
    printf("Offload features status:\n");
    printf("(Use 'ethtool -k %s' for detailed status)\n", interface);
    
    close(sock);
}

int main() {
    get_offload_stats("eth0");
    
    printf("\nTo check TSO/GSO status, use:\n");
    printf("  ethtool -k eth0 | grep segmentation\n");
    printf("\nTo enable TSO:\n");
    printf("  ethtool -K eth0 tso on\n");
    printf("\nTo enable GSO:\n");
    printf("  ethtool -K eth0 gso on\n");
    
    return 0;
}
```

## Rust Programming Examples

### Example 1: Basic TSO-Optimized TCP Sender

```rust
use std::io::{self, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::os::unix::io::AsRawFd;

const TCP_NODELAY: libc::c_int = 1;
const SO_SNDBUF: libc::c_int = 7;

struct TSOOptimizedStream {
    stream: TcpStream,
}

impl TSOOptimizedStream {
    pub fn connect<A: ToSocketAddrs>(addr: A) -> io::Result<Self> {
        let stream = TcpStream::connect(addr)?;
        
        let mut optimized = TSOOptimizedStream { stream };
        optimized.configure_tso()?;
        
        Ok(optimized)
    }
    
    fn configure_tso(&mut self) -> io::Result<()> {
        let fd = self.stream.as_raw_fd();
        
        // Enable TCP_NODELAY
        let enable: libc::c_int = 1;
        unsafe {
            let ret = libc::setsockopt(
                fd,
                libc::IPPROTO_TCP,
                libc::TCP_NODELAY,
                &enable as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::c_int>() as libc::socklen_t,
            );
            
            if ret != 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        // Set large send buffer (512KB)
        let sendbuf_size: libc::c_int = 512 * 1024;
        unsafe {
            let ret = libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_SNDBUF,
                &sendbuf_size as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::c_int>() as libc::socklen_t,
            );
            
            if ret != 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    pub fn send_large_data(&mut self, data: &[u8]) -> io::Result<usize> {
        // Send large buffer - TSO/GSO will handle segmentation
        self.stream.write_all(data)?;
        Ok(data.len())
    }
}

fn main() -> io::Result<()> {
    let mut sender = TSOOptimizedStream::connect("127.0.0.1:8080")?;
    
    // Create large data buffer (64KB)
    let large_data = vec![b'X'; 64 * 1024];
    
    println!("Sending large data with TSO/GSO optimization...");
    
    let sent = sender.send_large_data(&large_data)?;
    println!("Successfully sent {} bytes", sent);
    
    Ok(())
}
```

### Example 2: Advanced TSO Configuration with Socket Options

```rust
use std::io;
use std::net::TcpStream;
use std::os::unix::io::AsRawFd;

pub struct TSOConfig {
    pub tcp_nodelay: bool,
    pub send_buffer_size: usize,
    pub recv_buffer_size: usize,
}

impl Default for TSOConfig {
    fn default() -> Self {
        TSOConfig {
            tcp_nodelay: true,
            send_buffer_size: 512 * 1024,  // 512KB
            recv_buffer_size: 512 * 1024,  // 512KB
        }
    }
}

pub struct OptimizedTcpStream {
    stream: TcpStream,
    config: TSOConfig,
}

impl OptimizedTcpStream {
    pub fn new(stream: TcpStream, config: TSOConfig) -> io::Result<Self> {
        let mut optimized = OptimizedTcpStream { stream, config };
        optimized.apply_configuration()?;
        Ok(optimized)
    }
    
    fn apply_configuration(&mut self) -> io::Result<()> {
        let fd = self.stream.as_raw_fd();
        
        // TCP_NODELAY
        if self.config.tcp_nodelay {
            self.set_socket_option(
                libc::IPPROTO_TCP,
                libc::TCP_NODELAY,
                1,
            )?;
        }
        
        // SO_SNDBUF
        self.set_socket_option(
            libc::SOL_SOCKET,
            libc::SO_SNDBUF,
            self.config.send_buffer_size as i32,
        )?;
        
        // SO_RCVBUF
        self.set_socket_option(
            libc::SOL_SOCKET,
            libc::SO_RCVBUF,
            self.config.recv_buffer_size as i32,
        )?;
        
        Ok(())
    }
    
    fn set_socket_option(&self, level: i32, optname: i32, value: i32) -> io::Result<()> {
        let fd = self.stream.as_raw_fd();
        
        unsafe {
            let ret = libc::setsockopt(
                fd,
                level,
                optname,
                &value as *const _ as *const libc::c_void,
                std::mem::size_of::<i32>() as libc::socklen_t,
            );
            
            if ret != 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    pub fn get_socket_option(&self, level: i32, optname: i32) -> io::Result<i32> {
        let fd = self.stream.as_raw_fd();
        let mut value: i32 = 0;
        let mut len: libc::socklen_t = std::mem::size_of::<i32>() as libc::socklen_t;
        
        unsafe {
            let ret = libc::getsockopt(
                fd,
                level,
                optname,
                &mut value as *mut _ as *mut libc::c_void,
                &mut len,
            );
            
            if ret != 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        Ok(value)
    }
    
    pub fn print_configuration(&self) -> io::Result<()> {
        println!("TSO-Optimized Socket Configuration:");
        
        let nodelay = self.get_socket_option(libc::IPPROTO_TCP, libc::TCP_NODELAY)?;
        println!("  TCP_NODELAY: {}", nodelay != 0);
        
        let sndbuf = self.get_socket_option(libc::SOL_SOCKET, libc::SO_SNDBUF)?;
        println!("  SO_SNDBUF: {} bytes", sndbuf);
        
        let rcvbuf = self.get_socket_option(libc::SOL_SOCKET, libc::SO_RCVBUF)?;
        println!("  SO_RCVBUF: {} bytes", rcvbuf);
        
        Ok(())
    }
}

impl std::io::Write for OptimizedTcpStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.stream.write(buf)
    }
    
    fn flush(&mut self) -> io::Result<()> {
        self.stream.flush()
    }
}

impl std::io::Read for OptimizedTcpStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.stream.read(buf)
    }
}
```

### Example 3: High-Performance Bulk Transfer with TSO

```rust
use std::io::{self, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;
use std::time::Instant;

const BUFFER_SIZE: usize = 64 * 1024; // 64KB chunks for TSO
const TOTAL_SIZE: usize = 100 * 1024 * 1024; // 100MB total

fn create_optimized_stream(stream: TcpStream) -> io::Result<TcpStream> {
    let fd = stream.as_raw_fd();
    
    // Enable TCP_NODELAY
    unsafe {
        let enable: i32 = 1;
        libc::setsockopt(
            fd,
            libc::IPPROTO_TCP,
            libc::TCP_NODELAY,
            &enable as *const _ as *const libc::c_void,
            std::mem::size_of::<i32>() as u32,
        );
    }
    
    // Large send buffer
    unsafe {
        let size: i32 = 1024 * 1024; // 1MB
        libc::setsockopt(
            fd,
            libc::SOL_SOCKET,
            libc::SO_SNDBUF,
            &size as *const _ as *const libc::c_void,
            std::mem::size_of::<i32>() as u32,
        );
    }
    
    Ok(stream)
}

fn server_thread() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:9999")?;
    println!("Server listening on 127.0.0.1:9999");
    
    for stream in listener.incoming() {
        let mut stream = stream?;
        
        thread::spawn(move || {
            let mut buffer = vec![0u8; BUFFER_SIZE];
            let mut total_received = 0;
            
            while let Ok(n) = stream.read(&mut buffer) {
                if n == 0 { break; }
                total_received += n;
            }
            
            println!("Server received: {} bytes", total_received);
        });
    }
    
    Ok(())
}

fn client_transfer() -> io::Result<()> {
    thread::sleep(std::time::Duration::from_millis(100)); // Let server start
    
    let stream = TcpStream::connect("127.0.0.1:9999")?;
    let mut stream = create_optimized_stream(stream)?;
    
    println!("Client connected, starting bulk transfer with TSO...");
    
    let data = vec![0xABu8; BUFFER_SIZE];
    let chunks = TOTAL_SIZE / BUFFER_SIZE;
    
    let start = Instant::now();
    
    for _ in 0..chunks {
        stream.write_all(&data)?;
    }
    
    stream.flush()?;
    
    let duration = start.elapsed();
    let throughput = (TOTAL_SIZE as f64) / duration.as_secs_f64() / (1024.0 * 1024.0);
    
    println!("Transfer complete:");
    println!("  Size: {} MB", TOTAL_SIZE / (1024 * 1024));
    println!("  Time: {:?}", duration);
    println!("  Throughput: {:.2} MB/s", throughput);
    
    Ok(())
}

use std::os::unix::io::AsRawFd;
use std::io::Read;

fn main() -> io::Result<()> {
    println!("TSO/GSO High-Performance Transfer Demo\n");
    
    // Start server in background thread
    thread::spawn(|| {
        server_thread().expect("Server failed");
    });
    
    // Run client transfer
    client_transfer()?;
    
    thread::sleep(std::time::Duration::from_secs(1));
    
    Ok(())
}
```

## Summary

**TSO (TCP Segmentation Offload)** and **GSO (Generic Segmentation Offload)** are critical performance optimizations that move packet segmentation work from the CPU to the network hardware or lower layers. TSO is hardware-based and NIC-specific, while GSO is a software approach that works across different protocols and hardware.

**Key takeaways:**

- **Performance boost**: Applications can send large buffers (up to 64KB) instead of MTU-sized segments, reducing CPU overhead and increasing throughput
- **Transparent operation**: Once enabled, TSO/GSO work automatically without application code changes
- **Configuration**: Enable via socket options (TCP_NODELAY, larger buffers) and system tools (ethtool)
- **Best practices**: Use large send buffers, disable Nagle's algorithm for streaming, and let the kernel/hardware handle segmentation
- **Trade-offs**: Requires hardware support (TSO) or recent kernel (GSO), and may increase latency slightly for small packets

Both C/C++ and Rust examples demonstrate how to configure sockets for optimal TSO/GSO performance, with Rust offering additional memory safety while maintaining low-level control over socket options.