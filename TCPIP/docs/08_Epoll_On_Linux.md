# epoll() on Linux: High-Performance Event Notification

## Overview

`epoll` (event poll) is a Linux-specific I/O event notification mechanism that provides a scalable and efficient way to monitor multiple file descriptors. It was introduced in Linux kernel 2.5.44 as a replacement for `select()` and `poll()`, addressing their performance limitations when handling thousands of connections.

Unlike `select()` and `poll()`, which have O(n) complexity for monitoring file descriptors, `epoll` operates in O(1) time for most operations, making it ideal for high-performance network servers handling massive numbers of concurrent connections.

## Why epoll?

**Problems with select() and poll():**
- They require the kernel to scan all file descriptors on each call, even inactive ones
- The entire set of file descriptors must be copied from user space to kernel space on every operation
- Performance degrades linearly as the number of file descriptors increases

**epoll advantages:**
- Only active file descriptors trigger notifications
- File descriptor set is maintained in kernel space, avoiding repeated copying
- Scalable to tens of thousands of connections
- More efficient CPU cache usage

## Core Concepts

**Three main operations:**

1. **epoll_create/epoll_create1**: Creates an epoll instance
2. **epoll_ctl**: Adds, modifies, or removes file descriptors from monitoring
3. **epoll_wait**: Waits for events on registered file descriptors

**Two triggering modes:**

1. **Level-Triggered (LT)**: Default mode. Events are reported as long as the condition persists (e.g., data remains readable)
2. **Edge-Triggered (ET)**: Events are reported only when state changes occur. More efficient but requires careful handling

## C/C++ Implementation

### Basic Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 64
#define BUFFER_SIZE 1024
#define PORT 8080

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd, epoll_fd, conn_fd, n;
    struct epoll_event ev, events[MAX_EVENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind and listen
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(listen_fd);

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // Add listening socket to epoll
    ev.events = EPOLLIN;  // Monitor for read events
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl: listen_fd");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    // Event loop
    while (1) {
        // Wait for events (-1 = block indefinitely)
        n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        // Process all ready events
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                // Accept new connection
                while ((conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, 
                                        &client_len)) != -1) {
                    printf("New connection: fd=%d\n", conn_fd);
                    set_nonblocking(conn_fd);

                    // Add new connection to epoll
                    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered mode
                    ev.data.fd = conn_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                        perror("epoll_ctl: conn_fd");
                        close(conn_fd);
                    }
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("accept");
                }
            } else {
                // Handle client data
                int fd = events[i].data.fd;
                ssize_t count;

                // Edge-triggered: must read all available data
                while ((count = read(fd, buffer, BUFFER_SIZE)) > 0) {
                    printf("Received %zd bytes from fd=%d\n", count, fd);
                    // Echo back to client
                    write(fd, buffer, count);
                }

                if (count == 0) {
                    // Connection closed
                    printf("Connection closed: fd=%d\n", fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                } else if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("read");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return 0;
}
```

### Level-Triggered vs Edge-Triggered Example

```c
#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>

void level_triggered_example(int epoll_fd, int fd) {
    struct epoll_event ev;
    
    // Level-triggered: event persists while data is available
    ev.events = EPOLLIN;  // Default is level-triggered
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    
    // If you read only part of the data, epoll_wait will
    // keep returning this fd until all data is consumed
}

void edge_triggered_example(int epoll_fd, int fd) {
    struct epoll_event ev;
    char buffer[1024];
    ssize_t n;
    
    // Edge-triggered: event only on state change
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    
    // MUST read all available data in one go
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        // Process data
    }
    // If you don't read everything, you won't get another
    // notification until NEW data arrives
}
```

### Monitoring Multiple Event Types

```c
#include <sys/epoll.h>

void monitor_multiple_events(int epoll_fd, int fd) {
    struct epoll_event ev;
    
    // Monitor for read, write, and error conditions
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

void handle_events(struct epoll_event *event) {
    if (event->events & EPOLLIN) {
        printf("Data available for reading\n");
    }
    if (event->events & EPOLLOUT) {
        printf("Socket ready for writing\n");
    }
    if (event->events & EPOLLERR) {
        printf("Error condition\n");
    }
    if (event->events & EPOLLHUP) {
        printf("Hang up (connection closed)\n");
    }
    if (event->events & EPOLLRDHUP) {
        printf("Peer closed connection or shut down writing half\n");
    }
}
```

## Rust Implementation

### Basic Server with Tokio-style Manual Epoll

```rust
use std::os::unix::io::{AsRawFd, RawFd};
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};
use std::collections::HashMap;

const MAX_EVENTS: usize = 64;

// Wrapper around epoll system calls
struct Epoll {
    epoll_fd: RawFd,
}

impl Epoll {
    fn new() -> std::io::Result<Self> {
        let epoll_fd = unsafe { libc::epoll_create1(0) };
        if epoll_fd == -1 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(Epoll { epoll_fd })
    }

    fn add(&self, fd: RawFd, events: u32) -> std::io::Result<()> {
        let mut event = libc::epoll_event {
            events,
            u64: fd as u64,
        };
        
        let result = unsafe {
            libc::epoll_ctl(
                self.epoll_fd,
                libc::EPOLL_CTL_ADD,
                fd,
                &mut event as *mut _,
            )
        };
        
        if result == -1 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }

    fn modify(&self, fd: RawFd, events: u32) -> std::io::Result<()> {
        let mut event = libc::epoll_event {
            events,
            u64: fd as u64,
        };
        
        let result = unsafe {
            libc::epoll_ctl(
                self.epoll_fd,
                libc::EPOLL_CTL_MOD,
                fd,
                &mut event as *mut _,
            )
        };
        
        if result == -1 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }

    fn delete(&self, fd: RawFd) -> std::io::Result<()> {
        let result = unsafe {
            libc::epoll_ctl(
                self.epoll_fd,
                libc::EPOLL_CTL_DEL,
                fd,
                std::ptr::null_mut(),
            )
        };
        
        if result == -1 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }

    fn wait(&self, timeout_ms: i32) -> std::io::Result<Vec<libc::epoll_event>> {
        let mut events = vec![
            libc::epoll_event { events: 0, u64: 0 };
            MAX_EVENTS
        ];
        
        let n = unsafe {
            libc::epoll_wait(
                self.epoll_fd,
                events.as_mut_ptr(),
                MAX_EVENTS as i32,
                timeout_ms,
            )
        };
        
        if n == -1 {
            return Err(std::io::Error::last_os_error());
        }
        
        events.truncate(n as usize);
        Ok(events)
    }
}

impl Drop for Epoll {
    fn drop(&mut self) {
        unsafe { libc::close(self.epoll_fd) };
    }
}

// Set socket to non-blocking
fn set_nonblocking(stream: &TcpStream) -> std::io::Result<()> {
    let fd = stream.as_raw_fd();
    let flags = unsafe { libc::fcntl(fd, libc::F_GETFL, 0) };
    if flags == -1 {
        return Err(std::io::Error::last_os_error());
    }
    
    let result = unsafe { libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) };
    if result == -1 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}

fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    set_nonblocking(&listener.try_clone()?)?;
    
    let epoll = Epoll::new()?;
    let listener_fd = listener.as_raw_fd();
    
    // Add listener to epoll (level-triggered for accept)
    epoll.add(listener_fd, libc::EPOLLIN as u32)?;
    
    let mut connections: HashMap<RawFd, TcpStream> = HashMap::new();
    println!("Server listening on port 8080");
    
    loop {
        // Wait for events
        let events = epoll.wait(-1)?;
        
        for event in events {
            let fd = event.u64 as RawFd;
            
            if fd == listener_fd {
                // Accept new connections
                loop {
                    match listener.accept() {
                        Ok((stream, addr)) => {
                            println!("New connection from: {}", addr);
                            set_nonblocking(&stream)?;
                            
                            let client_fd = stream.as_raw_fd();
                            
                            // Edge-triggered mode for client connections
                            epoll.add(
                                client_fd,
                                (libc::EPOLLIN | libc::EPOLLET) as u32,
                            )?;
                            
                            connections.insert(client_fd, stream);
                        }
                        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            break;
                        }
                        Err(e) => return Err(e),
                    }
                }
            } else {
                // Handle client I/O
                if let Some(stream) = connections.get_mut(&fd) {
                    let mut buffer = [0u8; 1024];
                    
                    // Edge-triggered: read all available data
                    loop {
                        match stream.read(&mut buffer) {
                            Ok(0) => {
                                // Connection closed
                                println!("Connection closed: fd={}", fd);
                                epoll.delete(fd)?;
                                connections.remove(&fd);
                                break;
                            }
                            Ok(n) => {
                                println!("Received {} bytes from fd={}", n, fd);
                                // Echo back
                                stream.write_all(&buffer[..n])?;
                            }
                            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                break;
                            }
                            Err(e) => {
                                eprintln!("Read error: {}", e);
                                epoll.delete(fd)?;
                                connections.remove(&fd);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}
```

### Using Higher-Level Abstractions (mio crate)

```rust
use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{Read, Write};

const SERVER: Token = Token(0);

fn main() -> std::io::Result<()> {
    // Create poll instance (wraps epoll on Linux)
    let mut poll = Poll::new()?;
    let mut events = Events::with_capacity(128);
    
    // Bind listener
    let addr = "127.0.0.1:8080".parse().unwrap();
    let mut listener = TcpListener::bind(addr)?;
    
    // Register listener with poll
    poll.registry().register(
        &mut listener,
        SERVER,
        Interest::READABLE,
    )?;
    
    let mut connections: HashMap<Token, TcpStream> = HashMap::new();
    let mut unique_token = Token(1);
    
    println!("Server listening on {}", addr);
    
    loop {
        // Wait for events
        poll.poll(&mut events, None)?;
        
        for event in events.iter() {
            match event.token() {
                SERVER => {
                    // Accept connections
                    loop {
                        match listener.accept() {
                            Ok((mut stream, address)) => {
                                println!("New connection from: {}", address);
                                
                                let token = unique_token;
                                unique_token.0 += 1;
                                
                                poll.registry().register(
                                    &mut stream,
                                    token,
                                    Interest::READABLE,
                                )?;
                                
                                connections.insert(token, stream);
                            }
                            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                break;
                            }
                            Err(e) => return Err(e),
                        }
                    }
                }
                token => {
                    // Handle client I/O
                    if let Some(stream) = connections.get_mut(&token) {
                        let mut buffer = [0u8; 1024];
                        
                        match stream.read(&mut buffer) {
                            Ok(0) => {
                                // Connection closed
                                println!("Connection closed: {:?}", token);
                                connections.remove(&token);
                            }
                            Ok(n) => {
                                println!("Received {} bytes", n);
                                stream.write_all(&buffer[..n])?;
                            }
                            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                continue;
                            }
                            Err(e) => {
                                eprintln!("Read error: {}", e);
                                connections.remove(&token);
                            }
                        }
                    }
                }
            }
        }
    }
}
```

### Event Types and Flags

```rust
// Common epoll event flags
const EPOLLIN: u32 = 0x001;      // Readable
const EPOLLOUT: u32 = 0x004;     // Writable
const EPOLLERR: u32 = 0x008;     // Error condition
const EPOLLHUP: u32 = 0x010;     // Hang up
const EPOLLRDHUP: u32 = 0x2000;  // Peer closed writing half
const EPOLLET: u32 = 1 << 31;    // Edge-triggered mode
const EPOLLONESHOT: u32 = 1 << 30; // One-shot mode

fn demonstrate_event_handling(event: &libc::epoll_event) {
    if event.events & libc::EPOLLIN as u32 != 0 {
        println!("Data available for reading");
    }
    if event.events & libc::EPOLLOUT as u32 != 0 {
        println!("Ready for writing");
    }
    if event.events & libc::EPOLLERR as u32 != 0 {
        println!("Error occurred");
    }
    if event.events & libc::EPOLLHUP as u32 != 0 {
        println!("Hang up detected");
    }
}
```

## Summary

**epoll** is Linux's premier I/O multiplexing mechanism, offering superior scalability compared to `select()` and `poll()`. It operates in O(1) time by maintaining file descriptors in kernel space and only reporting active events. The three core operations—`epoll_create1()`, `epoll_ctl()`, and `epoll_wait()`—provide efficient management of thousands of concurrent connections.

**Key advantages** include constant-time performance regardless of file descriptor count, reduced system call overhead, and two triggering modes (level-triggered for simplicity, edge-triggered for maximum efficiency). **Edge-triggered mode** requires non-blocking sockets and exhaustive reading of available data but minimizes unnecessary wake-ups.

In C/C++, epoll is accessed directly via system calls, requiring careful error handling and manual memory management. Rust offers both low-level bindings through `libc` and high-level abstractions like `mio` (which powers async runtimes like Tokio), providing safety guarantees while maintaining performance.

**Best practices** include using edge-triggered mode with non-blocking I/O for high-performance servers, properly handling EAGAIN/EWOULDBLOCK errors, monitoring for EPOLLHUP and EPOLLRDHUP to detect disconnections, and using EPOLLONESHOT when needed to prevent event notification races in multithreaded environments. This makes epoll the foundation for building scalable network applications on Linux.