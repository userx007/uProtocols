# Socket Programming Basics: A Comprehensive Guide

## Overview

Socket programming is the foundation of network communication in modern computing. A **socket** is an endpoint for sending or receiving data across a computer network. It serves as the interface between application programs and the network protocol stack (typically TCP/IP). Understanding sockets is essential for building any networked application, from web servers to chat applications.

## Core Concepts

### What is a Socket?

A socket is identified by:
- **IP Address**: The network address of the machine
- **Port Number**: A 16-bit number (0-65535) identifying the specific process
- **Protocol**: TCP (connection-oriented) or UDP (connectionless)

The combination of IP address, port, and protocol creates a unique endpoint for communication.

### Socket Types

1. **Stream Sockets (SOCK_STREAM)**: Use TCP for reliable, connection-oriented communication
2. **Datagram Sockets (SOCK_DGRAM)**: Use UDP for connectionless, unreliable but faster communication
3. **Raw Sockets (SOCK_RAW)**: Direct access to lower-level protocols

### Client-Server Architecture

The basic flow involves:

**Server Side:**
1. Create a socket
2. Bind the socket to an address and port
3. Listen for incoming connections
4. Accept connections
5. Read/Write data
6. Close the connection

**Client Side:**
1. Create a socket
2. Connect to the server's address and port
3. Read/Write data
4. Close the connection

## C/C++ Implementation

### TCP Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE] = {0};
    
    // 1. Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // 2. Bind socket to address and port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Accept connections on any interface
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // 3. Listen for connections (backlog of 5)
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", PORT);
    
    // 4. Accept incoming connection
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("Accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Client connected from %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), 
           ntohs(client_addr.sin_port));
    
    // 5. Read data from client
    ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Received: %s\n", buffer);
        
        // Send response
        const char* response = "Hello from server!";
        write(client_fd, response, strlen(response));
    }
    
    // 6. Close sockets
    close(client_fd);
    close(server_fd);
    
    return 0;
}
```

### TCP Client Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};
    
    // 1. Create socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // 2. Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    // 3. Connect to server
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server\n");
    
    // 4. Send data
    const char* message = "Hello from client!";
    write(sock_fd, message, strlen(message));
    
    // 5. Receive response
    ssize_t bytes_read = read(sock_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Server response: %s\n", buffer);
    }
    
    // 6. Close socket
    close(sock_fd);
    
    return 0;
}
```

## Rust Implementation

Rust provides safe abstractions over socket programming through the standard library's `std::net` module.

### TCP Server Example

```rust
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write, Result};
use std::thread;

fn handle_client(mut stream: TcpStream) -> Result<()> {
    let mut buffer = [0u8; 1024];
    
    // Read data from client
    let bytes_read = stream.read(&mut buffer)?;
    
    if bytes_read > 0 {
        let received = String::from_utf8_lossy(&buffer[..bytes_read]);
        println!("Received: {}", received);
        
        // Send response
        let response = b"Hello from Rust server!";
        stream.write_all(response)?;
        stream.flush()?;
    }
    
    Ok(())
}

fn main() -> Result<()> {
    // 1. Bind to address and port (creates socket + bind + listen)
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on port 8080...");
    
    // 2. Accept connections in a loop
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                println!("Client connected from {:?}", stream.peer_addr()?);
                
                // Handle each client in a separate thread
                thread::spawn(|| {
                    if let Err(e) = handle_client(stream) {
                        eprintln!("Error handling client: {}", e);
                    }
                });
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    Ok(())
}
```

### TCP Client Example

```rust
use std::net::TcpStream;
use std::io::{Read, Write, Result};

fn main() -> Result<()> {
    // 1. Connect to server (creates socket + connect)
    let mut stream = TcpStream::connect("127.0.0.1:8080")?;
    println!("Connected to server");
    
    // 2. Send data
    let message = b"Hello from Rust client!";
    stream.write_all(message)?;
    stream.flush()?;
    
    // 3. Receive response
    let mut buffer = [0u8; 1024];
    let bytes_read = stream.read(&mut buffer)?;
    
    if bytes_read > 0 {
        let response = String::from_utf8_lossy(&buffer[..bytes_read]);
        println!("Server response: {}", response);
    }
    
    // 4. Connection automatically closed when stream goes out of scope
    Ok(())
}
```

### Advanced Rust Example: Non-blocking I/O

```rust
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write, Result, ErrorKind};
use std::time::Duration;

fn main() -> Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    
    // Set non-blocking mode
    listener.set_nonblocking(true)?;
    
    println!("Server in non-blocking mode on port 8080...");
    
    loop {
        match listener.accept() {
            Ok((mut stream, addr)) => {
                println!("Client connected from {:?}", addr);
                stream.set_read_timeout(Some(Duration::from_secs(5)))?;
                
                let mut buffer = [0u8; 1024];
                match stream.read(&mut buffer) {
                    Ok(n) if n > 0 => {
                        println!("Received {} bytes", n);
                        stream.write_all(b"ACK")?;
                    }
                    Ok(_) => println!("Connection closed"),
                    Err(e) => eprintln!("Read error: {}", e),
                }
            }
            Err(ref e) if e.kind() == ErrorKind::WouldBlock => {
                // No pending connections, do other work
                std::thread::sleep(Duration::from_millis(100));
                continue;
            }
            Err(e) => {
                eprintln!("Accept error: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

## Key System Calls and Functions

### C/C++ Functions

- **socket()**: Creates a new socket
- **bind()**: Assigns an address to the socket
- **listen()**: Marks socket as passive (accepting connections)
- **accept()**: Accepts an incoming connection
- **connect()**: Initiates a connection to a server
- **send()/recv()** or **write()/read()**: Transfer data
- **close()**: Closes the socket
- **setsockopt()**: Sets socket options

### Rust Equivalents

- **TcpListener::bind()**: Creates, binds, and listens in one call
- **listener.accept()**: Accepts connections
- **TcpStream::connect()**: Creates socket and connects
- **stream.read()/write()**: Data transfer via Read/Write traits
- Automatic cleanup via RAII (Resource Acquisition Is Initialization)

## Summary

Socket programming forms the backbone of network communication. The basic workflow involves creating endpoints (sockets), establishing connections, exchanging data, and closing connections. 

**C/C++** provides low-level control through POSIX socket APIs, requiring manual resource management and error handling. This gives maximum flexibility but increases complexity.

**Rust** offers safer abstractions through the standard library, with automatic resource cleanup, type safety, and error handling via the Result type. While still allowing low-level control when needed, Rust's approach reduces common bugs like resource leaks and use-after-free errors.

Both languages follow the same fundamental socket model, but Rust's ownership system and modern abstractions make it easier to write safe, concurrent network applications. Understanding these basics enables building everything from simple chat applications to complex distributed systems.