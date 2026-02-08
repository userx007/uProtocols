# MPTCP (Multipath TCP)

## Overview

Multipath TCP (MPTCP) is an extension to standard TCP that enables a single TCP connection to use multiple network paths simultaneously. Instead of being bound to a single interface and IP address pair, MPTCP can distribute data across multiple network interfaces (WiFi, cellular, Ethernet) concurrently, providing improved throughput, resilience, and seamless failover.

MPTCP is particularly valuable in mobile devices that have both WiFi and cellular connections, data centers with multiple network paths, and any scenario requiring high availability or aggregated bandwidth.

## How MPTCP Works

### Core Concepts

**Connection Establishment:**
- Starts as a regular TCP connection with an MP_CAPABLE option in the SYN
- If both endpoints support MPTCP, they negotiate MPTCP-specific parameters
- The initial connection becomes the first "subflow"

**Subflows:**
- Each subflow is a regular TCP connection using different network paths
- Subflows can use different source/destination IP pairs
- Each subflow has its own sequence numbers and congestion control
- New subflows are added using MP_JOIN option

**Data-Level Sequence Numbers:**
- MPTCP maintains connection-level sequence numbers separate from subflow sequence numbers
- This allows reassembly of data that arrives out-of-order across different subflows
- Ensures data integrity even if subflows fail or arrive with different delays

**Path Management:**
- Full-mesh: All addresses paired with all addresses
- Single-path: One subflow at a time
- Backup paths: Alternative paths for failover

## Programming MPTCP

### Linux Kernel Support

MPTCP support was merged into the Linux kernel starting with version 5.6, with ongoing improvements in subsequent versions. Applications can use MPTCP with minimal code changes.

### C/C++ Implementation

#### Basic MPTCP Socket Creation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

// Create an MPTCP socket
int create_mptcp_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
    
    if (sockfd < 0) {
        if (errno == EPROTONOSUPPORT) {
            fprintf(stderr, "MPTCP not supported, falling back to TCP\n");
            sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        } else {
            perror("socket creation failed");
            return -1;
        }
    }
    
    return sockfd;
}

// MPTCP Server Example
int mptcp_server(int port) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Create MPTCP socket
    if ((server_fd = create_mptcp_socket()) < 0) {
        return -1;
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }
    
    printf("MPTCP Server listening on port %d\n", port);
    
    // Accept incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t*)&addrlen)) < 0) {
        perror("accept failed");
        close(server_fd);
        return -1;
    }
    
    printf("Connection accepted\n");
    
    // Handle data transfer
    char buffer[1024] = {0};
    ssize_t valread;
    
    while ((valread = read(new_socket, buffer, 1024)) > 0) {
        printf("Received: %s\n", buffer);
        send(new_socket, buffer, valread, 0);
        memset(buffer, 0, sizeof(buffer));
    }
    
    close(new_socket);
    close(server_fd);
    
    return 0;
}

// MPTCP Client Example
int mptcp_client(const char *server_ip, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *hello = "Hello from MPTCP client";
    char buffer[1024] = {0};
    
    // Create MPTCP socket
    if ((sock = create_mptcp_socket()) < 0) {
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported\n");
        close(sock);
        return -1;
    }
    
    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(sock);
        return -1;
    }
    
    printf("Connected to server via MPTCP\n");
    
    // Send data
    send(sock, hello, strlen(hello), 0);
    printf("Message sent\n");
    
    // Receive response
    ssize_t valread = read(sock, buffer, 1024);
    printf("Received: %s\n", buffer);
    
    close(sock);
    return 0;
}
```

#### Advanced: Monitoring MPTCP Subflows

```c
#include <linux/mptcp.h>

// Structure to hold MPTCP information
typedef struct {
    int subflow_count;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} mptcp_info_t;

// Get MPTCP connection information
int get_mptcp_info(int sockfd, mptcp_info_t *info) {
    struct mptcp_info minfo;
    socklen_t len = sizeof(minfo);
    
    if (getsockopt(sockfd, IPPROTO_MPTCP, MPTCP_INFO, &minfo, &len) < 0) {
        perror("getsockopt MPTCP_INFO failed");
        return -1;
    }
    
    info->subflow_count = minfo.mptcpi_subflows;
    
    // Note: Exact fields depend on kernel version
    printf("MPTCP Subflows: %d\n", minfo.mptcpi_subflows);
    
    return 0;
}

// Add an additional subflow address
int add_mptcp_address(const char *local_addr) {
    // This typically requires netlink interface or ip mptcp commands
    // Example using system command (not recommended for production)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip mptcp endpoint add %s dev eth1 signal", 
             local_addr);
    return system(cmd);
}
```

#### C++ Wrapper Class

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

class MPTCPSocket {
private:
    int sockfd_;
    bool is_mptcp_;
    
public:
    MPTCPSocket() : sockfd_(-1), is_mptcp_(false) {
        sockfd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
        
        if (sockfd_ < 0) {
            if (errno == EPROTONOSUPPORT) {
                std::cerr << "MPTCP not supported, using TCP" << std::endl;
                sockfd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                is_mptcp_ = false;
            } else {
                throw std::runtime_error("Failed to create socket");
            }
        } else {
            is_mptcp_ = true;
        }
    }
    
    ~MPTCPSocket() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    // Disable copy
    MPTCPSocket(const MPTCPSocket&) = delete;
    MPTCPSocket& operator=(const MPTCPSocket&) = delete;
    
    // Enable move
    MPTCPSocket(MPTCPSocket&& other) noexcept 
        : sockfd_(other.sockfd_), is_mptcp_(other.is_mptcp_) {
        other.sockfd_ = -1;
    }
    
    bool isMPTCP() const { return is_mptcp_; }
    int get() const { return sockfd_; }
    
    void connect(const std::string& ip, int port) {
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            throw std::runtime_error("Invalid IP address");
        }
        
        if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error("Connection failed: " + 
                                   std::string(strerror(errno)));
        }
    }
    
    ssize_t send(const void* data, size_t len) {
        return ::send(sockfd_, data, len, 0);
    }
    
    ssize_t recv(void* buffer, size_t len) {
        return ::recv(sockfd_, buffer, len, 0);
    }
};

// Usage example
void mptcp_client_example() {
    try {
        MPTCPSocket sock;
        std::cout << "Using " << (sock.isMPTCP() ? "MPTCP" : "TCP") << std::endl;
        
        sock.connect("192.168.1.100", 8080);
        
        const char* msg = "Hello MPTCP";
        sock.send(msg, strlen(msg));
        
        char buffer[1024];
        ssize_t n = sock.recv(buffer, sizeof(buffer));
        if (n > 0) {
            std::cout << "Received: " << std::string(buffer, n) << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
```

### Rust Implementation

Rust doesn't have native MPTCP support in the standard library yet, but we can use FFI or work with lower-level socket options.

#### Using libc FFI

```rust
use std::io::{self, Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::os::unix::io::{AsRawFd, RawFd};

const IPPROTO_MPTCP: libc::c_int = 262;

/// Attempt to create an MPTCP socket
fn create_mptcp_socket(addr: &SocketAddr) -> io::Result<TcpStream> {
    use std::os::unix::io::FromRawFd;
    
    let domain = match addr {
        SocketAddr::V4(_) => libc::AF_INET,
        SocketAddr::V6(_) => libc::AF_INET6,
    };
    
    let fd = unsafe {
        libc::socket(domain, libc::SOCK_STREAM, IPPROTO_MPTCP)
    };
    
    if fd < 0 {
        // Fall back to regular TCP
        eprintln!("MPTCP not supported, using regular TCP");
        return TcpStream::connect(addr);
    }
    
    // Convert to TcpStream and connect
    let stream = unsafe { TcpStream::from_raw_fd(fd) };
    
    // Note: For proper connection, you'd need to handle connect() manually
    // This is simplified for demonstration
    Ok(stream)
}

/// MPTCP Client
pub struct MptcpClient {
    stream: TcpStream,
    is_mptcp: bool,
}

impl MptcpClient {
    pub fn connect(addr: &str) -> io::Result<Self> {
        let socket_addr: SocketAddr = addr.parse()
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;
        
        // Try MPTCP first
        match create_mptcp_socket(&socket_addr) {
            Ok(stream) => Ok(MptcpClient {
                stream,
                is_mptcp: true,
            }),
            Err(_) => {
                // Fallback to regular TCP
                let stream = TcpStream::connect(addr)?;
                Ok(MptcpClient {
                    stream,
                    is_mptcp: false,
                })
            }
        }
    }
    
    pub fn is_mptcp(&self) -> bool {
        self.is_mptcp
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<usize> {
        self.stream.write(data)
    }
    
    pub fn recv(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.stream.read(buffer)
    }
}

// Example usage
fn main() -> io::Result<()> {
    let mut client = MptcpClient::connect("192.168.1.100:8080")?;
    
    println!("Connected using {}", 
             if client.is_mptcp() { "MPTCP" } else { "TCP" });
    
    client.send(b"Hello from Rust MPTCP client")?;
    
    let mut buffer = vec![0u8; 1024];
    let n = client.recv(&mut buffer)?;
    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
    
    Ok(())
}
```

#### Higher-Level Async Rust with Tokio

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::error::Error;
use std::os::unix::io::AsRawFd;

const IPPROTO_MPTCP: i32 = 262;

/// Set socket to use MPTCP if available
fn enable_mptcp(stream: &TcpStream) -> std::io::Result<bool> {
    let fd = stream.as_raw_fd();
    
    // This is a simplified check - actual implementation would use setsockopt
    // to enable MPTCP on the socket
    unsafe {
        let optval: i32 = 1;
        let ret = libc::setsockopt(
            fd,
            IPPROTO_MPTCP,
            1, // Hypothetical MPTCP_ENABLED option
            &optval as *const _ as *const libc::c_void,
            std::mem::size_of::<i32>() as libc::socklen_t,
        );
        
        Ok(ret == 0)
    }
}

/// MPTCP Server
async fn mptcp_server(port: u16) -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind(format!("0.0.0.0:{}", port)).await?;
    println!("MPTCP Server listening on port {}", port);
    
    loop {
        let (mut socket, addr) = listener.accept().await?;
        println!("Connection from: {}", addr);
        
        // Attempt to enable MPTCP
        match enable_mptcp(&socket) {
            Ok(true) => println!("MPTCP enabled for connection"),
            _ => println!("Using regular TCP"),
        }
        
        tokio::spawn(async move {
            let mut buf = vec![0; 1024];
            
            loop {
                match socket.read(&mut buf).await {
                    Ok(0) => break, // Connection closed
                    Ok(n) => {
                        println!("Received {} bytes", n);
                        if socket.write_all(&buf[..n]).await.is_err() {
                            break;
                        }
                    }
                    Err(_) => break,
                }
            }
        });
    }
}

/// MPTCP Client
async fn mptcp_client(server_addr: &str) -> Result<(), Box<dyn Error>> {
    let mut stream = TcpStream::connect(server_addr).await?;
    
    match enable_mptcp(&stream) {
        Ok(true) => println!("Connected with MPTCP"),
        _ => println!("Connected with regular TCP"),
    }
    
    stream.write_all(b"Hello from async Rust MPTCP client").await?;
    
    let mut buffer = vec![0; 1024];
    let n = stream.read(&mut buffer).await?;
    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Run server in background
    tokio::spawn(async {
        if let Err(e) = mptcp_server(8080).await {
            eprintln!("Server error: {}", e);
        }
    });
    
    // Give server time to start
    tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
    
    // Run client
    mptcp_client("127.0.0.1:8080").await?;
    
    Ok(())
}
```

#### Configuration and Path Management in Rust

```rust
use std::process::Command;
use std::io;

pub struct MptcpConfig;

impl MptcpConfig {
    /// Add an MPTCP endpoint
    pub fn add_endpoint(ip: &str, interface: &str, flags: &str) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&["mptcp", "endpoint", "add", ip, "dev", interface, flags])
            .output()?;
        
        if !output.status.success() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        
        Ok(())
    }
    
    /// Remove an MPTCP endpoint
    pub fn remove_endpoint(id: u32) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&["mptcp", "endpoint", "delete", "id", &id.to_string()])
            .output()?;
        
        if !output.status.success() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        
        Ok(())
    }
    
    /// Set MPTCP limits
    pub fn set_limits(add_addr_accepted: u32, subflows: u32) -> io::Result<()> {
        let output = Command::new("ip")
            .args(&[
                "mptcp", "limits", "set",
                "add_addr_accepted", &add_addr_accepted.to_string(),
                "subflows", &subflows.to_string()
            ])
            .output()?;
        
        if !output.status.success() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        
        Ok(())
    }
}
```

## Summary

**MPTCP (Multipath TCP)** extends TCP to use multiple network paths simultaneously, providing:

**Key Benefits:**
- **Resilience**: Automatic failover when one path fails
- **Higher throughput**: Aggregate bandwidth across multiple interfaces
- **Seamless mobility**: Maintain connections during network transitions
- **Better resource utilization**: Load balancing across available paths

**Programming Considerations:**
- Minimal application changes needed - mostly transparent to applications
- Create sockets with `IPPROTO_MPTCP` protocol
- Graceful fallback to regular TCP when MPTCP unavailable
- Kernel support required (Linux 5.6+)
- Path management through netlink or ip commands

**Use Cases:**
- Mobile devices (WiFi + cellular bonding)
- Data centers (multiple network paths for redundancy)
- Load balancing and failover scenarios
- Bandwidth aggregation applications

**Limitations:**
- Requires support on both endpoints
- Some middleboxes may interfere with MPTCP options
- Increased connection establishment overhead
- More complex congestion control coordination

MPTCP represents a significant evolution in TCP, enabling applications to leverage multiple network paths transparently while maintaining backward compatibility with standard TCP.