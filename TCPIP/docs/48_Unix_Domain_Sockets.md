# Unix Domain Sockets - Detailed Description

## Overview

Unix Domain Sockets (UDS), also known as IPC (Inter-Process Communication) sockets, are a data communication endpoint for exchanging data between processes executing on the same host operating system. Unlike network sockets that use TCP/IP and can communicate across networks, Unix domain sockets use the file system as their address namespace and are limited to communication between processes on the same machine.

## Key Characteristics

**Address Family**: Unix domain sockets use the `AF_UNIX` (or `AF_LOCAL`) address family instead of `AF_INET` or `AF_INET6` used by network sockets.

**File System Paths**: Instead of IP addresses and ports, Unix domain sockets are identified by filesystem paths (e.g., `/tmp/my_socket.sock`). These paths appear in the filesystem namespace but don't correspond to actual files in the traditional sense.

**Performance**: Since Unix domain sockets don't require the overhead of network protocol processing, they're significantly faster than TCP/IP sockets for local IPC. There's no need for checksumming, routing, or other network-layer operations.

**Security**: Access control can be managed through filesystem permissions, making it easier to restrict which processes can connect to a socket.

**Socket Types**: Unix domain sockets support both stream sockets (`SOCK_STREAM`, similar to TCP) and datagram sockets (`SOCK_DGRAM`, similar to UDP). Stream sockets are connection-oriented and reliable, while datagram sockets are connectionless.

**Credential Passing**: Unix domain sockets can pass file descriptors and authentication credentials between processes, a feature not available with network sockets.

## Use Cases

- Communication between client/server applications on the same host
- Database servers (MySQL, PostgreSQL use UDS for local connections)
- Docker daemon communication
- System services and daemons
- Container orchestration systems
- Any scenario requiring fast, secure local IPC

## How They Work

The workflow for Unix domain sockets mirrors that of network sockets:

1. **Server Side**:
   - Create a socket with `socket(AF_UNIX, SOCK_STREAM, 0)`
   - Bind to a filesystem path with `bind()`
   - Listen for connections with `listen()`
   - Accept connections with `accept()`
   - Read/write data

2. **Client Side**:
   - Create a socket with `socket(AF_UNIX, SOCK_STREAM, 0)`
   - Connect to the server's path with `connect()`
   - Read/write data

## C/C++ Code Examples

### Stream Socket Server (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define SOCKET_PATH "/tmp/uds_server.sock"
#define BUFFER_SIZE 256

int main() {
    int server_fd, client_fd;
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    // Remove any existing socket file
    unlink(SOCKET_PATH);
    
    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    
    // Bind socket to path
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        unlink(SOCKET_PATH);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on %s\n", SOCKET_PATH);
    
    // Accept connections
    client_len = sizeof(client_addr);
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        unlink(SOCKET_PATH);
        exit(EXIT_FAILURE);
    }
    
    printf("Client connected\n");
    
    // Read data from client
    while ((bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Received: %s", buffer);
        
        // Echo back to client
        write(client_fd, buffer, bytes_read);
    }
    
    // Cleanup
    close(client_fd);
    close(server_fd);
    unlink(SOCKET_PATH);
    
    return 0;
}
```

### Stream Socket Client (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/uds_server.sock"
#define BUFFER_SIZE 256

int main() {
    int sock_fd;
    struct sockaddr_un server_addr;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_sent, bytes_received;
    
    // Create socket
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    
    // Connect to server
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server\n");
    
    // Send message
    const char *message = "Hello from client!\n";
    bytes_sent = write(sock_fd, message, strlen(message));
    if (bytes_sent == -1) {
        perror("write");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    // Receive response
    bytes_received = read(sock_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Server response: %s", buffer);
    }
    
    close(sock_fd);
    return 0;
}
```

### Datagram Socket Example (C)

```c
// Datagram Server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SERVER_PATH "/tmp/uds_dgram_server.sock"
#define BUFFER_SIZE 256

int main() {
    int sock_fd;
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    unlink(SERVER_PATH);
    
    sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SERVER_PATH, sizeof(server_addr.sun_path) - 1);
    
    if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Datagram server listening on %s\n", SERVER_PATH);
    
    while (1) {
        client_len = sizeof(client_addr);
        bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0,
                                  (struct sockaddr*)&client_addr, &client_len);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Received: %s", buffer);
            
            // Send response
            sendto(sock_fd, buffer, bytes_received, 0,
                   (struct sockaddr*)&client_addr, client_len);
        }
    }
    
    close(sock_fd);
    unlink(SERVER_PATH);
    return 0;
}
```

### C++ RAII Wrapper Example

```cpp
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

class UnixSocket {
private:
    int fd_;
    std::string path_;
    
public:
    UnixSocket(const std::string& path, bool is_server = false) 
        : fd_(-1), path_(path) {
        
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to create socket");
        }
        
        if (is_server) {
            unlink(path_.c_str());
            
            struct sockaddr_un addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
            
            if (bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                close(fd_);
                throw std::runtime_error("Failed to bind socket");
            }
            
            if (listen(fd_, 5) == -1) {
                close(fd_);
                unlink(path_.c_str());
                throw std::runtime_error("Failed to listen");
            }
        }
    }
    
    ~UnixSocket() {
        if (fd_ != -1) {
            close(fd_);
        }
    }
    
    void connect() {
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            throw std::runtime_error("Failed to connect");
        }
    }
    
    int accept() {
        int client_fd = ::accept(fd_, nullptr, nullptr);
        if (client_fd == -1) {
            throw std::runtime_error("Failed to accept connection");
        }
        return client_fd;
    }
    
    ssize_t send(const std::string& data) {
        return write(fd_, data.c_str(), data.length());
    }
    
    std::string receive(size_t max_len = 256) {
        char buffer[max_len];
        ssize_t bytes = read(fd_, buffer, max_len - 1);
        if (bytes > 0) {
            return std::string(buffer, bytes);
        }
        return "";
    }
    
    int getFd() const { return fd_; }
};
```

## Rust Code Examples

### Stream Socket Server (Rust)

```rust
use std::fs;
use std::io::{Read, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;

const SOCKET_PATH: &str = "/tmp/rust_uds_server.sock";

fn handle_client(mut stream: UnixStream) -> std::io::Result<()> {
    let mut buffer = [0u8; 256];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client disconnected");
                break;
            }
            Ok(n) => {
                let message = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", message);
                
                // Echo back to client
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

fn main() -> std::io::Result<()> {
    // Remove existing socket file if it exists
    if Path::new(SOCKET_PATH).exists() {
        fs::remove_file(SOCKET_PATH)?;
    }
    
    // Create and bind the listener
    let listener = UnixListener::bind(SOCKET_PATH)?;
    println!("Server listening on {}", SOCKET_PATH);
    
    // Accept connections
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                println!("Client connected");
                if let Err(e) = handle_client(stream) {
                    eprintln!("Error handling client: {}", e);
                }
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    // Cleanup
    fs::remove_file(SOCKET_PATH)?;
    Ok(())
}
```

### Stream Socket Client (Rust)

```rust
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

const SOCKET_PATH: &str = "/tmp/rust_uds_server.sock";

fn main() -> std::io::Result<()> {
    // Connect to the server
    let mut stream = UnixStream::connect(SOCKET_PATH)?;
    println!("Connected to server");
    
    // Send a message
    let message = "Hello from Rust client!\n";
    stream.write_all(message.as_bytes())?;
    println!("Sent: {}", message);
    
    // Receive response
    let mut buffer = [0u8; 256];
    let bytes_read = stream.read(&mut buffer)?;
    
    if bytes_read > 0 {
        let response = String::from_utf8_lossy(&buffer[..bytes_read]);
        println!("Server response: {}", response);
    }
    
    Ok(())
}
```

### Datagram Socket Example (Rust)

```rust
use std::fs;
use std::os::unix::net::UnixDatagram;
use std::path::Path;

const SERVER_PATH: &str = "/tmp/rust_dgram_server.sock";
const CLIENT_PATH: &str = "/tmp/rust_dgram_client.sock";

// Datagram Server
fn run_server() -> std::io::Result<()> {
    if Path::new(SERVER_PATH).exists() {
        fs::remove_file(SERVER_PATH)?;
    }
    
    let socket = UnixDatagram::bind(SERVER_PATH)?;
    println!("Datagram server listening on {}", SERVER_PATH);
    
    let mut buffer = [0u8; 256];
    
    loop {
        let (bytes_read, client_addr) = socket.recv_from(&mut buffer)?;
        let message = String::from_utf8_lossy(&buffer[..bytes_read]);
        println!("Received: {}", message);
        
        // Send response back to client
        if let Some(addr) = client_addr.as_pathname() {
            socket.send_to(&buffer[..bytes_read], addr)?;
        }
    }
}

// Datagram Client
fn run_client() -> std::io::Result<()> {
    if Path::new(CLIENT_PATH).exists() {
        fs::remove_file(CLIENT_PATH)?;
    }
    
    let socket = UnixDatagram::bind(CLIENT_PATH)?;
    
    let message = "Hello from datagram client!";
    socket.send_to(message.as_bytes(), SERVER_PATH)?;
    println!("Sent: {}", message);
    
    let mut buffer = [0u8; 256];
    let bytes_read = socket.recv(&mut buffer)?;
    let response = String::from_utf8_lossy(&buffer[..bytes_read]);
    println!("Received: {}", response);
    
    fs::remove_file(CLIENT_PATH)?;
    Ok(())
}
```

### Async Tokio Example (Rust)

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{UnixListener, UnixStream};
use std::path::Path;
use std::fs;

const SOCKET_PATH: &str = "/tmp/tokio_uds_server.sock";

async fn handle_client(mut stream: UnixStream) -> std::io::Result<()> {
    let mut buffer = [0u8; 256];
    
    loop {
        match stream.read(&mut buffer).await {
            Ok(0) => break,
            Ok(n) => {
                let message = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", message);
                stream.write_all(&buffer[..n]).await?;
            }
            Err(e) => {
                eprintln!("Error: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    if Path::new(SOCKET_PATH).exists() {
        fs::remove_file(SOCKET_PATH)?;
    }
    
    let listener = UnixListener::bind(SOCKET_PATH)?;
    println!("Async server listening on {}", SOCKET_PATH);
    
    loop {
        let (stream, _) = listener.accept().await?;
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream).await {
                eprintln!("Client error: {}", e);
            }
        });
    }
}
```

## Summary

Unix Domain Sockets provide a powerful, efficient mechanism for inter-process communication on the same host. They combine the familiar socket API with the performance benefits of local communication, avoiding network protocol overhead. Key advantages include faster data transfer compared to TCP/IP sockets, filesystem-based access control, and the ability to pass file descriptors between processes. They're identified by filesystem paths rather than IP addresses and ports, making them ideal for local services like database connections, Docker daemon communication, and system services. Both C/C++ and Rust provide robust support for Unix domain sockets, with Rust offering additional safety guarantees and modern async capabilities through libraries like Tokio. While limited to same-host communication, Unix domain sockets are the preferred choice when processes need fast, secure local IPC with minimal overhead.