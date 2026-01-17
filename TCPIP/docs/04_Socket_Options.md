# Socket Options: Detailed Technical Guide

## Overview

Socket options are configuration parameters that control the behavior of network sockets at various protocol layers. They allow fine-grained control over socket operations, performance characteristics, and protocol-specific behaviors. These options are set using `setsockopt()` and retrieved using `getsockopt()` system calls.

## Core Concepts

### Socket Option Levels

Socket options operate at different protocol levels:
- **SOL_SOCKET**: Generic socket-level options
- **IPPROTO_TCP**: TCP-specific options
- **IPPROTO_IP**: IPv4-specific options
- **IPPROTO_IPV6**: IPv6-specific options

### Critical Socket Options

#### 1. SO_REUSEADDR

This option allows binding to an address that is in the TIME_WAIT state, which is crucial for server applications that need to restart quickly.

**Use Cases:**
- Allowing immediate server restart after crash or graceful shutdown
- Multiple sockets binding to the same port with different IP addresses
- Avoiding "Address already in use" errors

**Important Notes:**
- On TIME_WAIT connections, the kernel keeps the socket alive for 2*MSL (Maximum Segment Lifetime, typically 60-120 seconds)
- Different behavior on different operating systems (especially regarding port hijacking protection)

#### 2. SO_KEEPALIVE

Enables TCP keepalive probes to detect dead peers and broken connections.

**Mechanism:**
- After a period of inactivity, sends probe packets
- If no response after several probes, connection is terminated
- Helps detect half-open connections

**Parameters (TCP-level):**
- `TCP_KEEPIDLE`: Time before first probe
- `TCP_KEEPINTVL`: Interval between probes
- `TCP_KEEPCNT`: Number of probes before giving up

#### 3. TCP_NODELAY

Disables Nagle's algorithm, which buffers small packets to improve network efficiency.

**When to Use:**
- Interactive applications (SSH, gaming, real-time communication)
- Request-response protocols where latency matters
- When sending small, time-sensitive messages

**When NOT to Use:**
- Bulk data transfer
- Applications where throughput is more important than latency

#### 4. Other Important Options

- **SO_RCVBUF/SO_SNDBUF**: Set receive/send buffer sizes
- **SO_LINGER**: Control socket close behavior
- **SO_REUSEPORT**: Allow multiple sockets to bind to same address/port
- **TCP_CORK**: Accumulate data before sending (Linux)
- **SO_RCVTIMEO/SO_SNDTIMEO**: Set timeout for blocking operations

## Code Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

// Comprehensive socket options configuration
int configure_server_socket(int sockfd) {
    int opt = 1;
    int result;
    
    // SO_REUSEADDR: Allow address reuse
    result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
                       &opt, sizeof(opt));
    if (result < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        return -1;
    }
    printf("SO_REUSEADDR enabled\n");
    
    // SO_REUSEPORT: Allow multiple sockets on same port (Linux 3.9+)
    #ifdef SO_REUSEPORT
    result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, 
                       &opt, sizeof(opt));
    if (result < 0) {
        perror("setsockopt SO_REUSEPORT failed");
        // Non-fatal, continue
    } else {
        printf("SO_REUSEPORT enabled\n");
    }
    #endif
    
    // SO_KEEPALIVE: Enable TCP keepalive
    result = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, 
                       &opt, sizeof(opt));
    if (result < 0) {
        perror("setsockopt SO_KEEPALIVE failed");
        return -1;
    }
    printf("SO_KEEPALIVE enabled\n");
    
    // Configure keepalive parameters (Linux-specific)
    #ifdef TCP_KEEPIDLE
    int keepidle = 60;  // Start probes after 60 seconds
    result = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, 
                       &keepidle, sizeof(keepidle));
    if (result < 0) {
        perror("setsockopt TCP_KEEPIDLE failed");
    }
    #endif
    
    #ifdef TCP_KEEPINTVL
    int keepintvl = 10;  // Probe interval: 10 seconds
    result = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, 
                       &keepintvl, sizeof(keepintvl));
    if (result < 0) {
        perror("setsockopt TCP_KEEPINTVL failed");
    }
    #endif
    
    #ifdef TCP_KEEPCNT
    int keepcnt = 3;  // Number of probes
    result = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, 
                       &keepcnt, sizeof(keepcnt));
    if (result < 0) {
        perror("setsockopt TCP_KEEPCNT failed");
    }
    #endif
    
    // TCP_NODELAY: Disable Nagle's algorithm for low latency
    result = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, 
                       &opt, sizeof(opt));
    if (result < 0) {
        perror("setsockopt TCP_NODELAY failed");
        return -1;
    }
    printf("TCP_NODELAY enabled (Nagle's algorithm disabled)\n");
    
    // Set receive buffer size
    int rcvbuf = 256 * 1024;  // 256 KB
    result = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, 
                       &rcvbuf, sizeof(rcvbuf));
    if (result < 0) {
        perror("setsockopt SO_RCVBUF failed");
    }
    
    // Set send buffer size
    int sndbuf = 256 * 1024;  // 256 KB
    result = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, 
                       &sndbuf, sizeof(sndbuf));
    if (result < 0) {
        perror("setsockopt SO_SNDBUF failed");
    }
    
    // SO_LINGER: Control close behavior
    struct linger ling;
    ling.l_onoff = 1;   // Enable linger
    ling.l_linger = 5;  // Wait 5 seconds for unsent data
    result = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, 
                       &ling, sizeof(ling));
    if (result < 0) {
        perror("setsockopt SO_LINGER failed");
    }
    
    return 0;
}

// Get socket options to verify configuration
void print_socket_options(int sockfd) {
    int opt;
    socklen_t optlen = sizeof(opt);
    
    // Check SO_REUSEADDR
    if (getsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, &optlen) == 0) {
        printf("SO_REUSEADDR: %s\n", opt ? "enabled" : "disabled");
    }
    
    // Check SO_KEEPALIVE
    if (getsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, &optlen) == 0) {
        printf("SO_KEEPALIVE: %s\n", opt ? "enabled" : "disabled");
    }
    
    // Check TCP_NODELAY
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, &optlen) == 0) {
        printf("TCP_NODELAY: %s\n", opt ? "enabled" : "disabled");
    }
    
    // Check buffer sizes
    if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &opt, &optlen) == 0) {
        printf("Receive buffer size: %d bytes\n", opt);
    }
    
    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &opt, &optlen) == 0) {
        printf("Send buffer size: %d bytes\n", opt);
    }
}

// Complete server example with socket options
int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure socket options
    if (configure_server_socket(server_fd) < 0) {
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Print current socket options
    printf("\nCurrent socket configuration:\n");
    print_socket_options(server_fd);
    
    // Bind to address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("\nServer listening on port 8080...\n");
    
    // Accept connection
    client_fd = accept(server_fd, (struct sockaddr *)&address, 
                      (socklen_t*)&addrlen);
    if (client_fd < 0) {
        perror("accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Client connected\n");
    
    // Configure client socket options
    configure_server_socket(client_fd);
    
    // Simple echo loop
    char buffer[1024] = {0};
    ssize_t bytes_read;
    
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        write(client_fd, buffer, bytes_read);
    }
    
    close(client_fd);
    close(server_fd);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::os::unix::io::AsRawFd;
use std::time::Duration;

// For socket options, we need to use libc directly
use libc::{c_int, c_void, socklen_t};
use libc::{
    setsockopt, getsockopt, SOL_SOCKET, SO_REUSEADDR, SO_KEEPALIVE,
    SO_RCVBUF, SO_SNDBUF, SO_LINGER, IPPROTO_TCP, TCP_NODELAY,
};

#[cfg(target_os = "linux")]
use libc::{TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT, SO_REUSEPORT};

// Structure for SO_LINGER option
#[repr(C)]
struct Linger {
    l_onoff: c_int,
    l_linger: c_int,
}

/// Configure socket with comprehensive options
fn configure_socket(stream: &TcpStream) -> io::Result<()> {
    let fd = stream.as_raw_fd();
    
    // SO_REUSEADDR: Allow address reuse
    unsafe {
        let opt: c_int = 1;
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        println!("SO_REUSEADDR enabled");
    }
    
    // SO_REUSEPORT: Multiple sockets on same port (Linux)
    #[cfg(target_os = "linux")]
    unsafe {
        let opt: c_int = 1;
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEPORT,
            &opt as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret == 0 {
            println!("SO_REUSEPORT enabled");
        }
    }
    
    // SO_KEEPALIVE: Enable TCP keepalive
    unsafe {
        let opt: c_int = 1;
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_KEEPALIVE,
            &opt as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        println!("SO_KEEPALIVE enabled");
    }
    
    // Configure keepalive parameters (Linux-specific)
    #[cfg(target_os = "linux")]
    unsafe {
        // TCP_KEEPIDLE: Start probes after 60 seconds
        let keepidle: c_int = 60;
        setsockopt(
            fd,
            IPPROTO_TCP,
            TCP_KEEPIDLE,
            &keepidle as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        
        // TCP_KEEPINTVL: Probe interval: 10 seconds
        let keepintvl: c_int = 10;
        setsockopt(
            fd,
            IPPROTO_TCP,
            TCP_KEEPINTVL,
            &keepintvl as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        
        // TCP_KEEPCNT: Number of probes
        let keepcnt: c_int = 3;
        setsockopt(
            fd,
            IPPROTO_TCP,
            TCP_KEEPCNT,
            &keepcnt as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        
        println!("Keepalive parameters configured");
    }
    
    // TCP_NODELAY: Disable Nagle's algorithm
    unsafe {
        let opt: c_int = 1;
        let ret = setsockopt(
            fd,
            IPPROTO_TCP,
            TCP_NODELAY,
            &opt as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        println!("TCP_NODELAY enabled (Nagle's algorithm disabled)");
    }
    
    // SO_RCVBUF: Set receive buffer size
    unsafe {
        let rcvbuf: c_int = 256 * 1024; // 256 KB
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVBUF,
            &rcvbuf as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret < 0 {
            eprintln!("Warning: Failed to set SO_RCVBUF");
        }
    }
    
    // SO_SNDBUF: Set send buffer size
    unsafe {
        let sndbuf: c_int = 256 * 1024; // 256 KB
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDBUF,
            &sndbuf as *const _ as *const c_void,
            std::mem::size_of::<c_int>() as socklen_t,
        );
        if ret < 0 {
            eprintln!("Warning: Failed to set SO_SNDBUF");
        }
    }
    
    // SO_LINGER: Control close behavior
    unsafe {
        let linger = Linger {
            l_onoff: 1,   // Enable linger
            l_linger: 5,  // Wait 5 seconds
        };
        let ret = setsockopt(
            fd,
            SOL_SOCKET,
            SO_LINGER,
            &linger as *const _ as *const c_void,
            std::mem::size_of::<Linger>() as socklen_t,
        );
        if ret < 0 {
            eprintln!("Warning: Failed to set SO_LINGER");
        }
    }
    
    // Use Rust's native API for timeouts
    stream.set_read_timeout(Some(Duration::from_secs(30)))?;
    stream.set_write_timeout(Some(Duration::from_secs(30)))?;
    
    Ok(())
}

/// Get and print current socket options
fn print_socket_options(stream: &TcpStream) {
    let fd = stream.as_raw_fd();
    
    unsafe {
        let mut opt: c_int = 0;
        let mut optlen: socklen_t = std::mem::size_of::<c_int>() as socklen_t;
        
        // Check SO_REUSEADDR
        if getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
                     &mut opt as *mut _ as *mut c_void, &mut optlen) == 0 {
            println!("SO_REUSEADDR: {}", if opt != 0 { "enabled" } else { "disabled" });
        }
        
        // Check SO_KEEPALIVE
        optlen = std::mem::size_of::<c_int>() as socklen_t;
        if getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 
                     &mut opt as *mut _ as *mut c_void, &mut optlen) == 0 {
            println!("SO_KEEPALIVE: {}", if opt != 0 { "enabled" } else { "disabled" });
        }
        
        // Check TCP_NODELAY
        optlen = std::mem::size_of::<c_int>() as socklen_t;
        if getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 
                     &mut opt as *mut _ as *mut c_void, &mut optlen) == 0 {
            println!("TCP_NODELAY: {}", if opt != 0 { "enabled" } else { "disabled" });
        }
        
        // Check buffer sizes
        optlen = std::mem::size_of::<c_int>() as socklen_t;
        if getsockopt(fd, SOL_SOCKET, SO_RCVBUF, 
                     &mut opt as *mut _ as *mut c_void, &mut optlen) == 0 {
            println!("Receive buffer size: {} bytes", opt);
        }
        
        optlen = std::mem::size_of::<c_int>() as socklen_t;
        if getsockopt(fd, SOL_SOCKET, SO_SNDBUF, 
                     &mut opt as *mut _ as *mut c_void, &mut optlen) == 0 {
            println!("Send buffer size: {} bytes", opt);
        }
    }
}

/// Handle client connection
fn handle_client(mut stream: TcpStream) -> io::Result<()> {
    println!("Client connected from: {}", stream.peer_addr()?);
    
    // Configure socket options for this connection
    configure_socket(&stream)?;
    
    println!("\nClient socket configuration:");
    print_socket_options(&stream);
    
    // Simple echo loop
    let mut buffer = [0u8; 1024];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client disconnected");
                break;
            }
            Ok(n) => {
                stream.write_all(&buffer[..n])?;
            }
            Err(e) => {
                eprintln!("Error reading from client: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}

fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on 127.0.0.1:8080");
    
    // Configure listener socket options
    if let Ok(stream) = listener.incoming().next().unwrap() {
        // The listener itself doesn't need all options,
        // but we configure each accepted connection
        handle_client(stream)?;
    }
    
    Ok(())
}
```

### High-Level Rust Alternative (Using `socket2` Crate)

```rust
// Cargo.toml:
// [dependencies]
// socket2 = "0.5"

use socket2::{Domain, Socket, Type, Protocol};
use std::io::{self, Read, Write};
use std::net::SocketAddr;
use std::time::Duration;

fn create_configured_socket(addr: SocketAddr) -> io::Result<Socket> {
    let socket = Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?;
    
    // SO_REUSEADDR
    socket.set_reuse_address(true)?;
    println!("SO_REUSEADDR enabled");
    
    // SO_REUSEPORT (platform-specific)
    #[cfg(all(unix, not(target_os = "solaris")))]
    {
        socket.set_reuse_port(true)?;
        println!("SO_REUSEPORT enabled");
    }
    
    // SO_KEEPALIVE
    socket.set_keepalive(true)?;
    println!("SO_KEEPALIVE enabled");
    
    // TCP_NODELAY
    socket.set_nodelay(true)?;
    println!("TCP_NODELAY enabled");
    
    // Buffer sizes
    socket.set_recv_buffer_size(256 * 1024)?;
    socket.set_send_buffer_size(256 * 1024)?;
    
    // Timeouts
    socket.set_read_timeout(Some(Duration::from_secs(30)))?;
    socket.set_write_timeout(Some(Duration::from_secs(30)))?;
    
    // Linger
    socket.set_linger(Some(Duration::from_secs(5)))?;
    
    Ok(socket)
}

fn main() -> io::Result<()> {
    let addr: SocketAddr = "127.0.0.1:8080".parse().unwrap();
    let socket = create_configured_socket(addr)?;
    
    socket.bind(&addr.into())?;
    socket.listen(128)?;
    
    println!("\nServer listening on {}", addr);
    println!("Buffer sizes - Recv: {}, Send: {}", 
             socket.recv_buffer_size()?,
             socket.send_buffer_size()?);
    
    let (stream, peer_addr) = socket.accept()?;
    println!("Client connected from: {}", peer_addr.as_socket().unwrap());
    
    // Convert to std::net::TcpStream for easier use
    let mut stream: std::net::TcpStream = stream.into();
    
    let mut buffer = [0u8; 1024];
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => break,
            Ok(n) => stream.write_all(&buffer[..n])?,
            Err(e) => {
                eprintln!("Error: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

## Summary

**Socket options** provide essential control over network socket behavior at multiple protocol layers. Key takeaways:

1. **SO_REUSEADDR** is critical for production servers to enable quick restarts and avoid "address already in use" errors during the TIME_WAIT period.

2. **SO_KEEPALIVE** detects dead connections through periodic probes, essential for long-lived connections and preventing resource leaks from half-open sockets.

3. **TCP_NODELAY** disables Nagle's algorithm, trading bandwidth efficiency for lower latency—vital for interactive applications but potentially harmful for bulk transfers.

4. **Buffer sizing** (SO_RCVBUF/SO_SNDBUF) impacts throughput and memory usage; larger buffers improve performance for high-bandwidth connections but consume more memory.

5. **SO_LINGER** controls socket close behavior, determining whether unsent data is discarded immediately or the close waits for transmission completion.

6. **Platform differences** exist, particularly between Linux, BSD variants, and Windows. Always test socket options on target platforms and handle errors gracefully.

7. **Configuration timing** matters: most socket options must be set before `bind()` for listeners, though connection-specific options can be set on accepted sockets.

Socket options transform generic sockets into production-ready communication endpoints optimized for specific application requirements, whether prioritizing throughput, latency, reliability, or resource management.