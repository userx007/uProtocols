# IPv4 vs IPv6: A Comprehensive Guide

## Introduction

The Internet Protocol (IP) serves as the fundamental communication protocol for routing data across networks. Two versions coexist today: IPv4 (Internet Protocol version 4) and IPv6 (Internet Protocol version 6). Understanding both protocols and how to work with them is essential for modern network programming.

## Address Families and Structures

### IPv4 Overview

IPv4 uses 32-bit addresses, typically represented in dotted-decimal notation (e.g., 192.168.1.1). This provides approximately 4.3 billion unique addresses, which has proven insufficient for the modern internet.

**Key characteristics:**
- 32-bit address space (2^32 addresses)
- Address family: `AF_INET`
- Header size: 20-60 bytes (variable)
- Address notation: Dotted decimal (xxx.xxx.xxx.xxx)

### IPv6 Overview

IPv6 uses 128-bit addresses, represented in hexadecimal colon-separated notation (e.g., 2001:0db8:85a3::8a2e:0370:7334). This provides an astronomically larger address space.

**Key characteristics:**
- 128-bit address space (2^128 addresses)
- Address family: `AF_INET6`
- Header size: 40 bytes (fixed)
- Address notation: Hexadecimal with colons
- Built-in IPsec support
- No broadcast (uses multicast instead)
- Simplified header structure

## Data Structures

### C/C++ Structures

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// IPv4 address structure
struct sockaddr_in {
    sa_family_t    sin_family;  // AF_INET
    in_port_t      sin_port;    // Port number (network byte order)
    struct in_addr sin_addr;    // IPv4 address
    char           sin_zero[8]; // Padding to match sockaddr size
};

struct in_addr {
    uint32_t s_addr;  // 32-bit IPv4 address (network byte order)
};

// IPv6 address structure
struct sockaddr_in6 {
    sa_family_t     sin6_family;   // AF_INET6
    in_port_t       sin6_port;     // Port number (network byte order)
    uint32_t        sin6_flowinfo; // IPv6 flow information
    struct in6_addr sin6_addr;     // IPv6 address
    uint32_t        sin6_scope_id; // Scope ID (for link-local addresses)
};

struct in6_addr {
    uint8_t s6_addr[16];  // 128-bit IPv6 address
};

// Generic socket address structure (for protocol-independent code)
struct sockaddr_storage {
    sa_family_t ss_family;
    // ... padding to accommodate any address family
};
```

### Rust Structures

```rust
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6};

fn demonstrate_rust_structures() {
    // IPv4 address
    let ipv4 = Ipv4Addr::new(192, 168, 1, 1);
    let socket_v4 = SocketAddrV4::new(ipv4, 8080);
    
    // IPv6 address
    let ipv6 = Ipv6Addr::new(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1);
    let socket_v6 = SocketAddrV6::new(ipv6, 8080, 0, 0);
    
    // Protocol-agnostic types
    let ip: IpAddr = IpAddr::V4(ipv4);
    let socket: SocketAddr = SocketAddr::V4(socket_v4);
    
    println!("IPv4: {}", ipv4);
    println!("IPv6: {}", ipv6);
    println!("Generic IP: {}", ip);
}
```

## Address Conversion Examples

### C/C++ Address Conversion

```c
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

void ipv4_conversion_example() {
    // String to binary conversion
    struct sockaddr_in addr;
    const char *ip_str = "192.168.1.100";
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    
    // Convert string to binary (newer, preferred method)
    if (inet_pton(AF_INET, ip_str, &addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        return;
    }
    
    // Binary to string conversion
    char ip_buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        return;
    }
    
    printf("IPv4 address: %s\n", ip_buffer);
}

void ipv6_conversion_example() {
    struct sockaddr_in6 addr6;
    const char *ipv6_str = "2001:db8::1";
    
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(8080);
    
    // Convert string to binary
    if (inet_pton(AF_INET6, ipv6_str, &addr6.sin6_addr) <= 0) {
        perror("inet_pton failed");
        return;
    }
    
    // Binary to string conversion
    char ipv6_buffer[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &addr6.sin6_addr, ipv6_buffer, INET6_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        return;
    }
    
    printf("IPv6 address: %s\n", ipv6_buffer);
}
```

### Rust Address Conversion

```rust
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use std::str::FromStr;

fn address_conversion_example() {
    // String to IPv4
    let ipv4_str = "192.168.1.100";
    let ipv4: Ipv4Addr = ipv4_str.parse().expect("Invalid IPv4");
    println!("Parsed IPv4: {}", ipv4);
    
    // String to IPv6
    let ipv6_str = "2001:db8::1";
    let ipv6: Ipv6Addr = ipv6_str.parse().expect("Invalid IPv6");
    println!("Parsed IPv6: {}", ipv6);
    
    // Generic parsing
    let ip: IpAddr = IpAddr::from_str("::1").expect("Invalid IP");
    match ip {
        IpAddr::V4(v4) => println!("Got IPv4: {}", v4),
        IpAddr::V6(v6) => println!("Got IPv6: {}", v6),
    }
    
    // Convert back to string
    let as_string = ipv6.to_string();
    println!("IPv6 as string: {}", as_string);
}
```

## Dual-Stack Implementation Strategies

Dual-stack systems support both IPv4 and IPv6 simultaneously, allowing applications to communicate with both protocol versions seamlessly.

### Strategy 1: Protocol-Independent Code (C/C++)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

// Protocol-independent server using getaddrinfo
int create_dual_stack_server(const char *port) {
    struct addrinfo hints, *res, *p;
    int sockfd;
    int yes = 1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // Use my IP
    
    int status = getaddrinfo(NULL, port, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    
    // Loop through results and bind to the first available
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }
        
        // Allow address reuse
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }
        
        break; // Successfully bound
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to bind\n");
        return -1;
    }
    
    if (listen(sockfd, 10) == -1) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    printf("Server listening on port %s\n", port);
    return sockfd;
}

// Accept connections and print client address
void accept_connections(int sockfd) {
    struct sockaddr_storage client_addr;
    socklen_t addr_size;
    char client_ip[INET6_ADDRSTRLEN];
    int client_fd;
    
    while (1) {
        addr_size = sizeof(client_addr);
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
        
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        // Get printable IP address
        void *addr;
        char *ipver;
        int port;
        
        if (client_addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            addr = &s->sin_addr;
            port = ntohs(s->sin_port);
            ipver = "IPv4";
        } else { // AF_INET6
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            addr = &s->sin6_addr;
            port = ntohs(s->sin6_port);
            ipver = "IPv6";
        }
        
        inet_ntop(client_addr.ss_family, addr, client_ip, sizeof(client_ip));
        printf("Connection from %s: %s:%d\n", ipver, client_ip, port);
        
        // Handle client...
        close(client_fd);
    }
}
```

### Strategy 2: IPv6-Only Socket with IPv4 Mapping (C/C++)

```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// Create IPv6 socket that handles both IPv4 and IPv6
int create_ipv6_dual_stack(int port) {
    int sockfd;
    struct sockaddr_in6 addr;
    int no = 0; // Disable IPV6_V6ONLY
    
    // Create IPv6 socket
    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }
    
    // Disable IPV6_V6ONLY to allow IPv4 connections via IPv4-mapped addresses
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) == -1) {
        perror("setsockopt IPV6_V6ONLY");
        close(sockfd);
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any; // :: (all interfaces)
    addr.sin6_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 10) == -1) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    printf("Dual-stack server listening on port %d\n", port);
    return sockfd;
}

// Detect if connection is IPv4-mapped or native IPv6
void handle_dual_stack_connection(struct sockaddr_in6 *addr) {
    char ip_str[INET6_ADDRSTRLEN];
    
    // Check if this is an IPv4-mapped IPv6 address
    if (IN6_IS_ADDR_V4MAPPED(&addr->sin6_addr)) {
        // Extract IPv4 address from the last 4 bytes
        struct in_addr ipv4_addr;
        memcpy(&ipv4_addr, &addr->sin6_addr.s6_addr[12], 4);
        
        inet_ntop(AF_INET, &ipv4_addr, ip_str, INET_ADDRSTRLEN);
        printf("IPv4 client (mapped): %s:%d\n", ip_str, ntohs(addr->sin6_port));
    } else {
        inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, INET6_ADDRSTRLEN);
        printf("IPv6 client: %s:%d\n", ip_str, ntohs(addr->sin6_port));
    }
}
```

### Rust Dual-Stack Implementation

```rust
use std::net::{TcpListener, TcpStream, SocketAddr};
use std::io::{Read, Write};

fn dual_stack_server_example() -> std::io::Result<()> {
    // Bind to IPv6 address with dual-stack support
    // Using [::]:8080 allows both IPv4 and IPv6 connections
    let listener = TcpListener::bind("[::]:8080")?;
    println!("Server listening on [::]:8080");
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                handle_client(stream);
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    Ok(())
}

fn handle_client(mut stream: TcpStream) {
    let peer_addr = stream.peer_addr().unwrap();
    
    match peer_addr {
        SocketAddr::V4(v4_addr) => {
            println!("IPv4 connection from: {}", v4_addr);
        }
        SocketAddr::V6(v6_addr) => {
            // Check if it's an IPv4-mapped IPv6 address
            if let Some(ipv4) = v6_addr.ip().to_ipv4_mapped() {
                println!("IPv4 connection (mapped): {}", ipv4);
            } else {
                println!("IPv6 connection from: {}", v6_addr);
            }
        }
    }
    
    // Handle the connection...
    let _ = stream.write_all(b"Hello from dual-stack server!\n");
}

// Client example with protocol detection
fn connect_dual_stack(host: &str, port: u16) -> std::io::Result<TcpStream> {
    use std::net::ToSocketAddrs;
    
    let addr_string = format!("{}:{}", host, port);
    
    // Resolve hostname to get all available addresses
    let addresses: Vec<SocketAddr> = addr_string.to_socket_addrs()?.collect();
    
    println!("Available addresses for {}:", host);
    for addr in &addresses {
        match addr {
            SocketAddr::V4(_) => println!("  IPv4: {}", addr),
            SocketAddr::V6(_) => println!("  IPv6: {}", addr),
        }
    }
    
    // Try to connect to the first available address
    TcpStream::connect(&addresses[..])
}
```

### Strategy 3: Separate IPv4 and IPv6 Sockets (C/C++)

```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/select.h>

typedef struct {
    int ipv4_sock;
    int ipv6_sock;
} DualStackServer;

DualStackServer create_separate_sockets(int port) {
    DualStackServer server = {-1, -1};
    int yes = 1;
    
    // Create IPv4 socket
    server.ipv4_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server.ipv4_sock != -1) {
        struct sockaddr_in addr4;
        setsockopt(server.ipv4_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = INADDR_ANY;
        addr4.sin_port = htons(port);
        
        if (bind(server.ipv4_sock, (struct sockaddr *)&addr4, sizeof(addr4)) == -1) {
            perror("IPv4 bind");
            close(server.ipv4_sock);
            server.ipv4_sock = -1;
        } else if (listen(server.ipv4_sock, 10) == -1) {
            perror("IPv4 listen");
            close(server.ipv4_sock);
            server.ipv4_sock = -1;
        } else {
            printf("IPv4 socket listening on port %d\n", port);
        }
    }
    
    // Create IPv6 socket
    server.ipv6_sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (server.ipv6_sock != -1) {
        struct sockaddr_in6 addr6;
        setsockopt(server.ipv6_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        
        // Enable IPv6-only mode to avoid conflicts with IPv4 socket
        yes = 1;
        setsockopt(server.ipv6_sock, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
        
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port);
        
        if (bind(server.ipv6_sock, (struct sockaddr *)&addr6, sizeof(addr6)) == -1) {
            perror("IPv6 bind");
            close(server.ipv6_sock);
            server.ipv6_sock = -1;
        } else if (listen(server.ipv6_sock, 10) == -1) {
            perror("IPv6 listen");
            close(server.ipv6_sock);
            server.ipv6_sock = -1;
        } else {
            printf("IPv6 socket listening on port %d\n", port);
        }
    }
    
    return server;
}

void run_dual_stack_server(DualStackServer *server) {
    fd_set read_fds;
    int max_fd;
    
    while (1) {
        FD_ZERO(&read_fds);
        max_fd = -1;
        
        if (server->ipv4_sock != -1) {
            FD_SET(server->ipv4_sock, &read_fds);
            if (server->ipv4_sock > max_fd) max_fd = server->ipv4_sock;
        }
        
        if (server->ipv6_sock != -1) {
            FD_SET(server->ipv6_sock, &read_fds);
            if (server->ipv6_sock > max_fd) max_fd = server->ipv6_sock;
        }
        
        if (max_fd == -1) {
            fprintf(stderr, "No valid sockets\n");
            break;
        }
        
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }
        
        // Check IPv4 socket
        if (server->ipv4_sock != -1 && FD_ISSET(server->ipv4_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(server->ipv4_sock, 
                                  (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd != -1) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                printf("IPv4 connection from %s:%d\n", ip, ntohs(client_addr.sin_port));
                close(client_fd);
            }
        }
        
        // Check IPv6 socket
        if (server->ipv6_sock != -1 && FD_ISSET(server->ipv6_sock, &read_fds)) {
            struct sockaddr_in6 client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(server->ipv6_sock, 
                                  (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd != -1) {
                char ip[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &client_addr.sin6_addr, ip, sizeof(ip));
                printf("IPv6 connection from %s:%d\n", ip, ntohs(client_addr.sin6_port));
                close(client_fd);
            }
        }
    }
}
```

### Rust Separate Sockets Implementation

```rust
use std::net::{TcpListener, SocketAddr};
use std::thread;
use std::sync::mpsc;

fn separate_socket_server() -> std::io::Result<()> {
    let (tx, rx) = mpsc::channel();
    
    // Spawn IPv4 listener thread
    let tx_v4 = tx.clone();
    thread::spawn(move || {
        if let Ok(listener) = TcpListener::bind("0.0.0.0:8080") {
            println!("IPv4 listener started on 0.0.0.0:8080");
            for stream in listener.incoming() {
                if let Ok(stream) = stream {
                    let addr = stream.peer_addr().unwrap();
                    tx_v4.send(format!("IPv4 connection: {}", addr)).unwrap();
                }
            }
        }
    });
    
    // Spawn IPv6 listener thread
    let tx_v6 = tx.clone();
    thread::spawn(move || {
        if let Ok(listener) = TcpListener::bind("[::]:8080") {
            println!("IPv6 listener started on [::]:8080");
            for stream in listener.incoming() {
                if let Ok(stream) = stream {
                    let addr = stream.peer_addr().unwrap();
                    tx_v6.send(format!("IPv6 connection: {}", addr)).unwrap();
                }
            }
        }
    });
    
    // Main thread processes connections
    for msg in rx {
        println!("{}", msg);
    }
    
    Ok(())
}
```

## Best Practices for Dual-Stack Development

1. **Use protocol-independent code**: Leverage `getaddrinfo()` in C/C++ or Rust's `ToSocketAddrs` trait for automatic protocol handling.

2. **Prefer IPv6 with IPv4 fallback**: Design systems to prefer IPv6 when available but gracefully fall back to IPv4.

3. **Avoid hardcoded address families**: Write code that can handle both AF_INET and AF_INET6.

4. **Use `sockaddr_storage`**: In C/C++, use this structure to hold addresses of any family.

5. **Test with both protocols**: Ensure your application works correctly with pure IPv4, pure IPv6, and dual-stack configurations.

6. **Handle IPv4-mapped addresses**: Be aware that IPv4 connections to IPv6 sockets may appear as IPv4-mapped IPv6 addresses (::ffff:192.168.1.1).

7. **Consider security implications**: IPv6 introduces new attack vectors; ensure firewall rules cover both protocols.

## Summary

IPv4 and IPv6 represent different evolutionary stages of internet addressing, with IPv6 designed to overcome IPv4's address exhaustion while providing enhanced features. Modern network applications must support both protocols through dual-stack implementations.

**Key takeaways:**

- IPv4 uses 32-bit addresses (4.3 billion), while IPv6 uses 128-bit addresses (virtually unlimited)
- Different address families require different socket structures: `sockaddr_in` for IPv4 and `sockaddr_in6` for IPv6
- Protocol-independent code using `getaddrinfo()` (C/C++) or `ToSocketAddrs` (Rust) provides the most flexible dual-stack support
- Three main dual-stack strategies: protocol-independent code, single IPv6 socket with IPv4 mapping, or separate sockets for each protocol
- IPv6 sockets can handle IPv4 connections through IPv4-mapped addresses when `IPV6_V6ONLY` is disabled
- Rust provides excellent abstractions with `IpAddr`, `SocketAddr`, and automatic protocol handling
- Testing with both protocols is essential for robust network applications

The transition from IPv4 to IPv6 is ongoing, making dual-stack support critical for ensuring application compatibility across diverse network environments.