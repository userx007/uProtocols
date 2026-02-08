# DCCP (Datagram Congestion Control Protocol)

## Overview

DCCP is a transport layer protocol (RFC 4340) that bridges the gap between TCP and UDP. It provides an **unreliable** datagram service similar to UDP, but with built-in **congestion control** mechanisms similar to TCP. This makes DCCP ideal for applications that need timely delivery over reliable delivery, such as streaming media, online gaming, and VoIP.

### Key Characteristics

- **Unreliable delivery**: No retransmission of lost packets
- **Connection-oriented**: Establishes explicit connections with handshakes
- **Congestion control**: Prevents network congestion like TCP
- **Negotiable congestion control mechanisms**: Supports multiple CCIDs (Congestion Control IDs)
- **Sequence numbers**: Tracks packet delivery without guaranteeing it
- **Feature negotiation**: Allows endpoints to negotiate parameters

### Common Use Cases

- Real-time video/audio streaming
- Online gaming
- VoIP applications
- Any scenario where recent data is more valuable than old data

## Programming DCCP

### C/C++ Implementation

DCCP uses standard socket APIs with the `SOCK_DCCP` socket type. Here's a comprehensive example:

#### DCCP Server (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define DCCP_SOCKOPT_SERVICE 2
#define DCCP_SOCKOPT_CCID 13
#define SOL_DCCP 269
#define IPPROTO_DCCP 33

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // Create DCCP socket
    server_fd = socket(AF_INET, SOCK_DCCP, IPPROTO_DCCP);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set service code (application-specific identifier)
    uint32_t service_code = htonl(42);
    if (setsockopt(server_fd, SOL_DCCP, DCCP_SOCKOPT_SERVICE, 
                   &service_code, sizeof(service_code)) < 0) {
        perror("Service code setting failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Optional: Set CCID (Congestion Control ID)
    // CCID 2 = TCP-like, CCID 3 = TFRC (TCP-Friendly Rate Control)
    uint8_t ccid = 2;
    if (setsockopt(server_fd, SOL_DCCP, DCCP_SOCKOPT_CCID, 
                   &ccid, sizeof(ccid)) < 0) {
        perror("CCID setting failed");
    }
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("DCCP Server listening on port %d...\n", PORT);
    
    // Accept connection
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("Accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Client connected: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), 
           ntohs(client_addr.sin_port));
    
    // Receive data
    while (1) {
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received < 0) {
            perror("Receive failed");
            break;
        } else if (bytes_received == 0) {
            printf("Client disconnected\n");
            break;
        }
        
        buffer[bytes_received] = '\0';
        printf("Received: %s", buffer);
        
        // Echo back (optional)
        send(client_fd, buffer, bytes_received, 0);
    }
    
    close(client_fd);
    close(server_fd);
    return 0;
}
```

#### DCCP Client (C++)

```cpp
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DCCP_SOCKOPT_SERVICE 2
#define SOL_DCCP 269
#define IPPROTO_DCCP 33

class DCCPClient {
private:
    int sockfd;
    struct sockaddr_in server_addr;
    
public:
    DCCPClient(const char* server_ip, int port) {
        // Create DCCP socket
        sockfd = socket(AF_INET, SOCK_DCCP, IPPROTO_DCCP);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        // Set service code
        uint32_t service_code = htonl(42);
        if (setsockopt(sockfd, SOL_DCCP, DCCP_SOCKOPT_SERVICE,
                      &service_code, sizeof(service_code)) < 0) {
            close(sockfd);
            throw std::runtime_error("Service code setting failed");
        }
        
        // Configure server address
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error("Invalid address");
        }
    }
    
    bool connect() {
        if (::connect(sockfd, (struct sockaddr*)&server_addr, 
                     sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "Connected to server" << std::endl;
        return true;
    }
    
    bool sendData(const std::string& data) {
        ssize_t sent = send(sockfd, data.c_str(), data.length(), 0);
        if (sent < 0) {
            std::cerr << "Send failed: " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    
    std::string receiveData(size_t buffer_size = 1024) {
        char buffer[buffer_size];
        ssize_t received = recv(sockfd, buffer, buffer_size - 1, 0);
        
        if (received < 0) {
            std::cerr << "Receive failed: " << strerror(errno) << std::endl;
            return "";
        }
        
        buffer[received] = '\0';
        return std::string(buffer);
    }
    
    ~DCCPClient() {
        close(sockfd);
    }
};

int main() {
    try {
        DCCPClient client("127.0.0.1", 8080);
        
        if (!client.connect()) {
            return 1;
        }
        
        // Send streaming data
        for (int i = 0; i < 10; i++) {
            std::string message = "Frame " + std::to_string(i) + "\n";
            client.sendData(message);
            
            std::string response = client.receiveData();
            if (!response.empty()) {
                std::cout << "Received: " << response;
            }
            
            usleep(100000); // 100ms delay
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

Rust doesn't have native DCCP support in the standard library, but you can use FFI or create custom implementations. Here's an example using raw socket bindings:

```rust
use std::io::{self, Error, ErrorKind};
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::os::unix::io::RawFd;
use libc::{self, c_int, c_void, sockaddr, sockaddr_in, socklen_t};
use std::mem;

const IPPROTO_DCCP: c_int = 33;
const SOCK_DCCP: c_int = 6;
const SOL_DCCP: c_int = 269;
const DCCP_SOCKOPT_SERVICE: c_int = 2;
const DCCP_SOCKOPT_CCID: c_int = 13;

pub struct DccpSocket {
    fd: RawFd,
}

impl DccpSocket {
    pub fn new() -> io::Result<Self> {
        unsafe {
            let fd = libc::socket(libc::AF_INET, SOCK_DCCP, IPPROTO_DCCP);
            if fd < 0 {
                return Err(Error::last_os_error());
            }
            Ok(DccpSocket { fd })
        }
    }
    
    pub fn set_service_code(&self, service_code: u32) -> io::Result<()> {
        unsafe {
            let code = service_code.to_be();
            let ret = libc::setsockopt(
                self.fd,
                SOL_DCCP,
                DCCP_SOCKOPT_SERVICE,
                &code as *const _ as *const c_void,
                mem::size_of::<u32>() as socklen_t,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(())
        }
    }
    
    pub fn set_ccid(&self, ccid: u8) -> io::Result<()> {
        unsafe {
            let ret = libc::setsockopt(
                self.fd,
                SOL_DCCP,
                DCCP_SOCKOPT_CCID,
                &ccid as *const _ as *const c_void,
                mem::size_of::<u8>() as socklen_t,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(())
        }
    }
    
    pub fn bind(&self, addr: SocketAddr) -> io::Result<()> {
        unsafe {
            let sockaddr = Self::to_sockaddr(addr);
            let ret = libc::bind(
                self.fd,
                &sockaddr as *const _ as *const sockaddr,
                mem::size_of::<sockaddr_in>() as socklen_t,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(())
        }
    }
    
    pub fn listen(&self, backlog: c_int) -> io::Result<()> {
        unsafe {
            let ret = libc::listen(self.fd, backlog);
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(())
        }
    }
    
    pub fn accept(&self) -> io::Result<(DccpSocket, SocketAddr)> {
        unsafe {
            let mut client_addr: sockaddr_in = mem::zeroed();
            let mut addr_len = mem::size_of::<sockaddr_in>() as socklen_t;
            
            let client_fd = libc::accept(
                self.fd,
                &mut client_addr as *mut _ as *mut sockaddr,
                &mut addr_len,
            );
            
            if client_fd < 0 {
                return Err(Error::last_os_error());
            }
            
            let addr = Self::from_sockaddr(client_addr);
            Ok((DccpSocket { fd: client_fd }, addr))
        }
    }
    
    pub fn connect(&self, addr: SocketAddr) -> io::Result<()> {
        unsafe {
            let sockaddr = Self::to_sockaddr(addr);
            let ret = libc::connect(
                self.fd,
                &sockaddr as *const _ as *const sockaddr,
                mem::size_of::<sockaddr_in>() as socklen_t,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(())
        }
    }
    
    pub fn send(&self, data: &[u8]) -> io::Result<usize> {
        unsafe {
            let ret = libc::send(
                self.fd,
                data.as_ptr() as *const c_void,
                data.len(),
                0,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(ret as usize)
        }
    }
    
    pub fn recv(&self, buffer: &mut [u8]) -> io::Result<usize> {
        unsafe {
            let ret = libc::recv(
                self.fd,
                buffer.as_mut_ptr() as *mut c_void,
                buffer.len(),
                0,
            );
            
            if ret < 0 {
                return Err(Error::last_os_error());
            }
            Ok(ret as usize)
        }
    }
    
    fn to_sockaddr(addr: SocketAddr) -> sockaddr_in {
        match addr {
            SocketAddr::V4(addr) => {
                let mut sockaddr: sockaddr_in = unsafe { mem::zeroed() };
                sockaddr.sin_family = libc::AF_INET as u16;
                sockaddr.sin_port = addr.port().to_be();
                sockaddr.sin_addr.s_addr = u32::from_ne_bytes(addr.ip().octets());
                sockaddr
            }
            _ => panic!("IPv6 not supported in this example"),
        }
    }
    
    fn from_sockaddr(sockaddr: sockaddr_in) -> SocketAddr {
        let ip = Ipv4Addr::from(sockaddr.sin_addr.s_addr.to_ne_bytes());
        let port = u16::from_be(sockaddr.sin_port);
        SocketAddr::new(IpAddr::V4(ip), port)
    }
}

impl Drop for DccpSocket {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

// Server example
fn main() -> io::Result<()> {
    let socket = DccpSocket::new()?;
    socket.set_service_code(42)?;
    socket.set_ccid(2)?; // TCP-like congestion control
    
    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)), 8080);
    socket.bind(addr)?;
    socket.listen(5)?;
    
    println!("DCCP Server listening on port 8080...");
    
    let (client, client_addr) = socket.accept()?;
    println!("Client connected: {}", client_addr);
    
    let mut buffer = vec![0u8; 1024];
    loop {
        match client.recv(&mut buffer) {
            Ok(0) => {
                println!("Client disconnected");
                break;
            }
            Ok(n) => {
                let data = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", data);
                client.send(&buffer[..n])?;
            }
            Err(e) => {
                eprintln!("Error receiving: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

## Summary

DCCP fills a unique niche in network protocols by combining UDP's unreliable, low-latency delivery with TCP's congestion control mechanisms. This makes it particularly valuable for real-time applications where timely delivery matters more than guaranteed delivery—such as video streaming, gaming, and VoIP.

**Key advantages:**
- Prevents network congestion without the overhead of retransmissions
- Lower latency than TCP for time-sensitive data
- Negotiable congestion control algorithms (CCID 2 for TCP-like behavior, CCID 3 for TFRC rate-based control)
- Explicit connection management with feature negotiation

**Limitations:**
- Limited OS and firewall support compared to TCP/UDP
- More complex than UDP while still providing unreliable delivery
- Requires kernel support (Linux has good support; other OSes vary)
- Not as widely deployed or understood as TCP/UDP

Programming DCCP follows similar patterns to TCP socket programming, with the main differences being the socket type (`SOCK_DCCP`), protocol (`IPPROTO_DCCP`), and DCCP-specific socket options for service codes and congestion control identifiers. The protocol is ideal when you need congestion awareness but can tolerate packet loss.