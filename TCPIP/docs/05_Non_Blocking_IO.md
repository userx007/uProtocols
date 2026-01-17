# Non-blocking I/O in Network Programming

## Overview

Non-blocking I/O is a fundamental technique in network programming that allows a program to initiate I/O operations without waiting for them to complete. Instead of blocking the execution thread when data isn't immediately available, non-blocking sockets return immediately with an error code, allowing the program to perform other tasks or check multiple sockets efficiently.

## Core Concepts

### Blocking vs Non-blocking Behavior

In **blocking mode** (the default), when you call `recv()` on a socket with no data available, your program stops and waits until data arrives. Similarly, `send()` blocks if the send buffer is full.

In **non-blocking mode**, these operations return immediately. If the operation can't complete right away, they return -1 and set `errno` to `EAGAIN` (or `EWOULDBLOCK`, which is typically the same value), indicating "try again later."

### Setting Non-blocking Mode

There are two primary methods to set a socket to non-blocking mode:

1. **Using `fcntl()` with `O_NONBLOCK`** - The portable POSIX approach
2. **Using `ioctl()` with `FIONBIO`** - An alternative method
3. **Using `accept4()` with `SOCK_NONBLOCK`** - When accepting connections (Linux)

The `fcntl()` approach is most common and portable across Unix-like systems.

## Implementation Details

### The fcntl() System Call

`fcntl()` (file control) is a versatile system call for manipulating file descriptor properties. The basic pattern for setting non-blocking mode is:

1. Get the current flags with `F_GETFL`
2. Add the `O_NONBLOCK` flag using bitwise OR
3. Set the new flags with `F_SETFL`

### Error Handling: EAGAIN and EWOULDBLOCK

When a non-blocking operation cannot complete immediately:
- **EAGAIN** (or **EWOULDBLOCK**): No data available for reading, or no buffer space for writing
- **EINTR**: The operation was interrupted by a signal (should retry)
- Other errors indicate genuine problems

## C/C++ Implementation

### Basic Non-blocking Socket Setup

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Function to set a socket to non-blocking mode
int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    
    return 0;
}

// Non-blocking server example
int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    // Set non-blocking mode
    if (set_nonblocking(server_fd) == -1) {
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port 8080 (non-blocking mode)\n");
    
    // Non-blocking accept loop
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, 
                          (socklen_t*)&addrlen);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No pending connections, do other work
                printf("No pending connections, doing other work...\n");
                sleep(1);  // Simulate other work
                continue;
            } else {
                perror("accept");
                break;
            }
        }
        
        printf("Accepted connection from %s:%d\n",
               inet_ntoa(address.sin_addr),
               ntohs(address.sin_port));
        
        // Set client socket to non-blocking
        set_nonblocking(client_fd);
        
        // Handle client (in real app, would add to event loop)
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}
```

### Non-blocking Read/Write with Error Handling

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

// Non-blocking read with proper error handling
ssize_t nonblocking_read(int sockfd, void *buffer, size_t len) {
    ssize_t n;
    
    while (1) {
        n = recv(sockfd, buffer, len, 0);
        
        if (n > 0) {
            // Successfully read data
            return n;
        } else if (n == 0) {
            // Connection closed by peer
            return 0;
        } else {
            // Error occurred
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available right now
                return -1;
            } else {
                // Real error
                perror("recv");
                return -1;
            }
        }
    }
}

// Non-blocking write with proper error handling
ssize_t nonblocking_write(int sockfd, const void *buffer, size_t len) {
    ssize_t n;
    size_t total_sent = 0;
    const char *ptr = (const char *)buffer;
    
    while (total_sent < len) {
        n = send(sockfd, ptr + total_sent, len - total_sent, 0);
        
        if (n > 0) {
            total_sent += n;
        } else if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Buffer full, return bytes sent so far
                return total_sent > 0 ? total_sent : -1;
            } else {
                perror("send");
                return -1;
            }
        }
    }
    
    return total_sent;
}

// Example usage
void handle_client(int client_fd) {
    char buffer[1024];
    ssize_t bytes_read;
    
    while (1) {
        bytes_read = nonblocking_read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Received: %s\n", buffer);
            
            // Echo back
            if (nonblocking_write(client_fd, buffer, bytes_read) < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("Write would block, buffer full\n");
                    // In real app, save data and use select/poll/epoll
                }
            }
        } else if (bytes_read == 0) {
            printf("Client disconnected\n");
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("No data available, doing other work...\n");
                usleep(100000);  // 100ms
            } else {
                break;  // Real error
            }
        }
    }
}
```

### C++ Wrapper Class

```cpp
#include <iostream>
#include <system_error>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

class NonBlockingSocket {
private:
    int sockfd_;
    
public:
    explicit NonBlockingSocket(int domain = AF_INET, int type = SOCK_STREAM) {
        sockfd_ = socket(domain, type, 0);
        if (sockfd_ < 0) {
            throw std::system_error(errno, std::system_category(), 
                                  "socket creation failed");
        }
        setNonBlocking();
    }
    
    ~NonBlockingSocket() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    // Delete copy constructor and assignment
    NonBlockingSocket(const NonBlockingSocket&) = delete;
    NonBlockingSocket& operator=(const NonBlockingSocket&) = delete;
    
    // Move constructor and assignment
    NonBlockingSocket(NonBlockingSocket&& other) noexcept 
        : sockfd_(other.sockfd_) {
        other.sockfd_ = -1;
    }
    
    NonBlockingSocket& operator=(NonBlockingSocket&& other) noexcept {
        if (this != &other) {
            if (sockfd_ >= 0) close(sockfd_);
            sockfd_ = other.sockfd_;
            other.sockfd_ = -1;
        }
        return *this;
    }
    
    void setNonBlocking() {
        int flags = fcntl(sockfd_, F_GETFL, 0);
        if (flags < 0) {
            throw std::system_error(errno, std::system_category(), 
                                  "fcntl F_GETFL failed");
        }
        
        if (fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            throw std::system_error(errno, std::system_category(), 
                                  "fcntl F_SETFL failed");
        }
    }
    
    ssize_t recv(void* buffer, size_t length) {
        ssize_t result = ::recv(sockfd_, buffer, length, 0);
        if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw std::system_error(errno, std::system_category(), 
                                  "recv failed");
        }
        return result;
    }
    
    ssize_t send(const void* buffer, size_t length) {
        ssize_t result = ::send(sockfd_, buffer, length, 0);
        if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw std::system_error(errno, std::system_category(), 
                                  "send failed");
        }
        return result;
    }
    
    int fd() const { return sockfd_; }
    
    bool wouldBlock() const {
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
};
```

## Rust Implementation

### Basic Non-blocking Socket Setup

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::os::unix::io::AsRawFd;
use std::time::Duration;

// Set a file descriptor to non-blocking mode
fn set_nonblocking(fd: i32) -> io::Result<()> {
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL, 0);
        if flags < 0 {
            return Err(io::Error::last_os_error());
        }
        
        if libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) < 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}

// Non-blocking server example
fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    
    // Set listener to non-blocking mode
    listener.set_nonblocking(true)?;
    
    println!("Server listening on port 8080 (non-blocking mode)");
    
    loop {
        match listener.accept() {
            Ok((mut stream, addr)) => {
                println!("Accepted connection from: {}", addr);
                
                // Set client stream to non-blocking
                stream.set_nonblocking(true)?;
                
                // Handle client
                handle_client(stream)?;
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                // No pending connections
                println!("No pending connections, doing other work...");
                std::thread::sleep(Duration::from_millis(100));
            }
            Err(e) => {
                eprintln!("Error accepting connection: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}

fn handle_client(mut stream: TcpStream) -> io::Result<()> {
    let mut buffer = [0u8; 1024];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client disconnected");
                break;
            }
            Ok(n) => {
                println!("Received {} bytes", n);
                
                // Echo back
                match stream.write_all(&buffer[..n]) {
                    Ok(_) => println!("Echoed data back"),
                    Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                        println!("Write would block");
                        // In real app, buffer data and use event loop
                    }
                    Err(e) => return Err(e),
                }
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                println!("Read would block, no data available");
                std::thread::sleep(Duration::from_millis(100));
            }
            Err(ref e) if e.kind() == io::ErrorKind::Interrupted => {
                println!("Read interrupted, retrying");
                continue;
            }
            Err(e) => return Err(e),
        }
    }
    
    Ok(())
}
```

### Idiomatic Rust with Error Handling

```rust
use std::io::{self, ErrorKind, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::time::Duration;

pub struct NonBlockingServer {
    listener: TcpListener,
}

impl NonBlockingServer {
    pub fn new(addr: &str) -> io::Result<Self> {
        let listener = TcpListener::bind(addr)?;
        listener.set_nonblocking(true)?;
        Ok(Self { listener })
    }
    
    pub fn accept(&self) -> io::Result<Option<TcpStream>> {
        match self.listener.accept() {
            Ok((stream, _addr)) => {
                stream.set_nonblocking(true)?;
                Ok(Some(stream))
            }
            Err(ref e) if e.kind() == ErrorKind::WouldBlock => {
                Ok(None)
            }
            Err(e) => Err(e),
        }
    }
}

pub struct NonBlockingClient {
    stream: TcpStream,
}

impl NonBlockingClient {
    pub fn new(stream: TcpStream) -> io::Result<Self> {
        stream.set_nonblocking(true)?;
        Ok(Self { stream })
    }
    
    pub fn read_available(&mut self, buffer: &mut [u8]) -> io::Result<Option<usize>> {
        match self.stream.read(buffer) {
            Ok(0) => Ok(Some(0)), // Connection closed
            Ok(n) => Ok(Some(n)),
            Err(ref e) if e.kind() == ErrorKind::WouldBlock => Ok(None),
            Err(ref e) if e.kind() == ErrorKind::Interrupted => {
                self.read_available(buffer) // Retry on interrupt
            }
            Err(e) => Err(e),
        }
    }
    
    pub fn write_available(&mut self, data: &[u8]) -> io::Result<Option<usize>> {
        match self.stream.write(data) {
            Ok(n) => Ok(Some(n)),
            Err(ref e) if e.kind() == ErrorKind::WouldBlock => Ok(None),
            Err(ref e) if e.kind() == ErrorKind::Interrupted => {
                self.write_available(data) // Retry on interrupt
            }
            Err(e) => Err(e),
        }
    }
    
    pub fn write_all_available(&mut self, mut data: &[u8]) -> io::Result<usize> {
        let mut total_written = 0;
        
        while !data.is_empty() {
            match self.write_available(data)? {
                Some(0) => break, // Connection closed
                Some(n) => {
                    total_written += n;
                    data = &data[n..];
                }
                None => break, // Would block
            }
        }
        
        Ok(total_written)
    }
}

// Example usage
fn example_usage() -> io::Result<()> {
    let server = NonBlockingServer::new("127.0.0.1:8080")?;
    
    println!("Server listening on port 8080");
    
    loop {
        if let Some(stream) = server.accept()? {
            println!("New client connected");
            
            let mut client = NonBlockingClient::new(stream)?;
            let mut buffer = [0u8; 1024];
            
            match client.read_available(&mut buffer)? {
                Some(0) => println!("Client disconnected"),
                Some(n) => {
                    println!("Read {} bytes", n);
                    client.write_all_available(&buffer[..n])?;
                }
                None => println!("No data available yet"),
            }
        }
        
        // Do other work
        std::thread::sleep(Duration::from_millis(10));
    }
}
```

### Using Rust's mio for Production Non-blocking I/O

```rust
use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};

const SERVER: Token = Token(0);

struct ClientConnection {
    stream: TcpStream,
    buffer: Vec<u8>,
}

fn main() -> io::Result<()> {
    let addr = "127.0.0.1:8080".parse().unwrap();
    let mut listener = TcpListener::bind(addr)?;
    
    let mut poll = Poll::new()?;
    let mut events = Events::with_capacity(128);
    
    poll.registry().register(
        &mut listener,
        SERVER,
        Interest::READABLE
    )?;
    
    let mut clients: HashMap<Token, ClientConnection> = HashMap::new();
    let mut next_token = Token(SERVER.0 + 1);
    
    println!("Server listening on {}", addr);
    
    loop {
        poll.poll(&mut events, None)?;
        
        for event in events.iter() {
            match event.token() {
                SERVER => {
                    // Accept new connections
                    loop {
                        match listener.accept() {
                            Ok((mut stream, address)) => {
                                println!("Accepted connection from: {}", address);
                                
                                let token = next_token;
                                next_token.0 += 1;
                                
                                poll.registry().register(
                                    &mut stream,
                                    token,
                                    Interest::READABLE
                                )?;
                                
                                clients.insert(token, ClientConnection {
                                    stream,
                                    buffer: Vec::new(),
                                });
                            }
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                break;
                            }
                            Err(e) => return Err(e),
                        }
                    }
                }
                token => {
                    // Handle client events
                    if let Some(client) = clients.get_mut(&token) {
                        let mut buf = [0u8; 1024];
                        
                        match client.stream.read(&mut buf) {
                            Ok(0) => {
                                println!("Client disconnected");
                                clients.remove(&token);
                            }
                            Ok(n) => {
                                println!("Read {} bytes from client", n);
                                client.stream.write_all(&buf[..n])?;
                            }
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                                continue;
                            }
                            Err(e) => {
                                eprintln!("Error reading from client: {}", e);
                                clients.remove(&token);
                            }
                        }
                    }
                }
            }
        }
    }
}
```

## Summary

**Non-blocking I/O** is essential for building scalable network applications. Key points:

- **Purpose**: Allows programs to handle multiple connections without dedicating a thread to each one, improving scalability and resource efficiency.

- **Implementation**: Use `fcntl()` with `O_NONBLOCK` to set sockets to non-blocking mode. Operations return immediately rather than waiting for data or buffer space.

- **Error Handling**: `EAGAIN`/`EWOULDBLOCK` indicates "try again later" (not a real error), while `EINTR` means retry after signal interruption. Other errors indicate genuine problems.

- **Best Practices**: 
  - Always check return values and handle `EAGAIN`/`EWOULDBLOCK` appropriately
  - Combine non-blocking I/O with event notification mechanisms (select, poll, epoll, kqueue)
  - In Rust, leverage standard library support or use libraries like `mio` or `tokio` for production code

- **Trade-offs**: Non-blocking I/O adds complexity but is crucial for high-performance servers handling thousands of concurrent connections. It's the foundation for event-driven architectures and async I/O patterns.

Non-blocking I/O by itself requires busy-waiting or periodic polling. In practice, it's combined with multiplexing mechanisms (select/poll/epoll) to efficiently monitor multiple sockets, which we'll explore in subsequent topics.