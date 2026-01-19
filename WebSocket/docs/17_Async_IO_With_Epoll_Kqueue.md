# Async I/O with epoll/kqueue for WebSocket Programming

## Overview

Async I/O with epoll (Linux) and kqueue (BSD/macOS) represents a highly efficient approach to handling multiple WebSocket connections simultaneously. These are event-driven I/O multiplexing mechanisms that allow a single thread to monitor thousands of file descriptors (sockets) and respond only when they're ready for I/O operations, eliminating the overhead of traditional polling or thread-per-connection models.

## Core Concepts

### What is I/O Multiplexing?

I/O multiplexing allows a program to monitor multiple file descriptors to see if I/O is possible on any of them. Instead of blocking on a single socket or creating a thread per connection, you can efficiently handle many connections with minimal resources.

### epoll vs kqueue

- **epoll** (Linux): Uses a red-black tree internally, provides edge-triggered and level-triggered modes
- **kqueue** (BSD/macOS): Uses a kernel event queue, provides a unified interface for various event types

Both are significantly more efficient than `select()` or `poll()` for large numbers of connections because they don't require scanning all file descriptors on each call.

### Event-Driven Architecture

The basic flow:
1. Register sockets with the event system
2. Wait for events (readable, writable, error)
3. Handle events as they occur
4. Return to waiting

## C Implementation with epoll (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define PORT 8080

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Create and configure listening socket
int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

// Accept new connections
void handle_new_connection(int epoll_fd, int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    struct epoll_event ev;

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more connections
            }
            perror("accept");
            break;
        }

        printf("New connection from %s:%d (fd=%d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_fd);

        set_nonblocking(client_fd);

        // Add to epoll with edge-triggered mode
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            perror("epoll_ctl: client_fd");
            close(client_fd);
        }
    }
}

// Handle data from client
void handle_client_data(int epoll_fd, int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t count;

    while (1) {
        count = read(client_fd, buffer, sizeof(buffer));
        
        if (count == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Read all available data
                break;
            }
            perror("read");
            goto close_conn;
        } else if (count == 0) {
            // Connection closed
            printf("Connection closed (fd=%d)\n", client_fd);
            goto close_conn;
        }

        printf("Received %zd bytes from fd=%d\n", count, client_fd);

        // Echo back (simplified WebSocket frame handling)
        ssize_t written = 0;
        while (written < count) {
            ssize_t n = write(client_fd, buffer + written, count - written);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Would block, modify epoll to wait for writable
                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
                    break;
                }
                perror("write");
                goto close_conn;
            }
            written += n;
        }
    }
    return;

close_conn:
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

int main() {
    int server_fd, epoll_fd;
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds;

    // Create server socket
    server_fd = create_server_socket(PORT);
    if (server_fd == -1) {
        exit(EXIT_FAILURE);
    }
    set_nonblocking(server_fd);

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // Add server socket to epoll
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }

    printf("WebSocket server listening on port %d\n", PORT);

    // Event loop
    while (1) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                // New connection(s)
                handle_new_connection(epoll_fd, server_fd);
            } else {
                // Data from existing connection
                handle_client_data(epoll_fd, events[i].data.fd);
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}
```

## C++ Implementation with kqueue (BSD/macOS)

```cpp
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

constexpr int MAX_EVENTS = 1024;
constexpr int BUFFER_SIZE = 4096;
constexpr int PORT = 8080;

class WebSocketServer {
private:
    int server_fd;
    int kqueue_fd;
    std::unordered_map<int, std::vector<char>> write_buffers;

    bool setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    int createServerSocket(int port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1) {
            perror("socket");
            return -1;
        }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("bind");
            close(fd);
            return -1;
        }

        if (listen(fd, SOMAXCONN) == -1) {
            perror("listen");
            close(fd);
            return -1;
        }

        return fd;
    }

    void handleNewConnection() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                perror("accept");
                break;
            }

            std::cout << "New connection from " 
                      << inet_ntoa(client_addr.sin_addr) << ":" 
                      << ntohs(client_addr.sin_port) 
                      << " (fd=" << client_fd << ")\n";

            setNonBlocking(client_fd);

            // Register for read events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            if (kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
                perror("kevent: add client");
                close(client_fd);
            }
        }
    }

    void handleClientRead(int client_fd) {
        char buffer[BUFFER_SIZE];
        
        while (true) {
            ssize_t count = read(client_fd, buffer, sizeof(buffer));
            
            if (count == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                perror("read");
                closeConnection(client_fd);
                return;
            } else if (count == 0) {
                std::cout << "Connection closed (fd=" << client_fd << ")\n";
                closeConnection(client_fd);
                return;
            }

            std::cout << "Received " << count << " bytes from fd=" << client_fd << "\n";

            // Queue data for echo (simplified WebSocket handling)
            auto& write_buf = write_buffers[client_fd];
            write_buf.insert(write_buf.end(), buffer, buffer + count);

            // Register for write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        }
    }

    void handleClientWrite(int client_fd) {
        auto it = write_buffers.find(client_fd);
        if (it == write_buffers.end() || it->second.empty()) {
            // No data to write, disable write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
            return;
        }

        auto& buffer = it->second;
        ssize_t written = write(client_fd, buffer.data(), buffer.size());

        if (written == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write");
                closeConnection(client_fd);
            }
            return;
        }

        // Remove written data
        buffer.erase(buffer.begin(), buffer.begin() + written);

        if (buffer.empty()) {
            write_buffers.erase(it);
            // Disable write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        }
    }

    void closeConnection(int fd) {
        struct kevent ev[2];
        EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kqueue_fd, ev, 2, nullptr, 0, nullptr);
        
        write_buffers.erase(fd);
        close(fd);
    }

public:
    WebSocketServer() : server_fd(-1), kqueue_fd(-1) {}

    ~WebSocketServer() {
        if (server_fd != -1) close(server_fd);
        if (kqueue_fd != -1) close(kqueue_fd);
    }

    bool start(int port) {
        server_fd = createServerSocket(port);
        if (server_fd == -1) return false;
        
        setNonBlocking(server_fd);

        kqueue_fd = kqueue();
        if (kqueue_fd == -1) {
            perror("kqueue");
            return false;
        }

        // Register server socket for read events
        struct kevent ev;
        EV_SET(&ev, server_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
            perror("kevent: add server");
            return false;
        }

        std::cout << "WebSocket server listening on port " << port << "\n";
        return true;
    }

    void run() {
        struct kevent events[MAX_EVENTS];

        while (true) {
            int nfds = kevent(kqueue_fd, nullptr, 0, events, MAX_EVENTS, nullptr);
            if (nfds == -1) {
                perror("kevent wait");
                break;
            }

            for (int i = 0; i < nfds; i++) {
                int fd = static_cast<int>(events[i].ident);

                if (fd == server_fd) {
                    handleNewConnection();
                } else if (events[i].filter == EVFILT_READ) {
                    handleClientRead(fd);
                } else if (events[i].filter == EVFILT_WRITE) {
                    handleClientWrite(fd);
                }
            }
        }
    }
};

int main() {
    WebSocketServer server;
    if (!server.start(PORT)) {
        return EXIT_FAILURE;
    }
    server.run();
    return 0;
}
```

## Rust Implementation with epoll (via mio)

```rust
// Cargo.toml dependencies:
// [dependencies]
// mio = { version = "0.8", features = ["os-poll", "net"] }

use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::SocketAddr;

const SERVER: Token = Token(0);
const BUFFER_SIZE: usize = 4096;

struct Client {
    stream: TcpStream,
    addr: SocketAddr,
    write_buffer: Vec<u8>,
}

impl Client {
    fn new(stream: TcpStream, addr: SocketAddr) -> Self {
        Self {
            stream,
            addr,
            write_buffer: Vec::new(),
        }
    }

    fn readable(&mut self) -> io::Result<Option<Vec<u8>>> {
        let mut buffer = [0u8; BUFFER_SIZE];
        let mut data = Vec::new();

        loop {
            match self.stream.read(&mut buffer) {
                Ok(0) => {
                    // Connection closed
                    return Ok(None);
                }
                Ok(n) => {
                    println!("Received {} bytes from {}", n, self.addr);
                    data.extend_from_slice(&buffer[..n]);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No more data available
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        if data.is_empty() {
            Ok(Some(Vec::new()))
        } else {
            Ok(Some(data))
        }
    }

    fn writable(&mut self, poll: &Poll, token: Token) -> io::Result<()> {
        while !self.write_buffer.is_empty() {
            match self.stream.write(&self.write_buffer) {
                Ok(n) => {
                    self.write_buffer.drain(..n);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // Can't write more right now
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        // If write buffer is empty, remove write interest
        if self.write_buffer.is_empty() {
            poll.registry().reregister(
                &mut self.stream,
                token,
                Interest::READABLE,
            )?;
        }

        Ok(())
    }

    fn queue_write(&mut self, poll: &Poll, token: Token, data: Vec<u8>) -> io::Result<()> {
        self.write_buffer.extend(data);

        // Add write interest
        poll.registry().reregister(
            &mut self.stream,
            token,
            Interest::READABLE.add(Interest::WRITABLE),
        )?;

        Ok(())
    }
}

struct WebSocketServer {
    listener: TcpListener,
    clients: HashMap<Token, Client>,
    next_token: usize,
    poll: Poll,
}

impl WebSocketServer {
    fn new(addr: &str) -> io::Result<Self> {
        let address: SocketAddr = addr.parse().expect("Invalid address");
        let mut listener = TcpListener::bind(address)?;
        let poll = Poll::new()?;

        poll.registry()
            .register(&mut listener, SERVER, Interest::READABLE)?;

        println!("WebSocket server listening on {}", addr);

        Ok(Self {
            listener,
            clients: HashMap::new(),
            next_token: 1,
            poll,
        })
    }

    fn accept_connections(&mut self) -> io::Result<()> {
        loop {
            match self.listener.accept() {
                Ok((mut stream, addr)) => {
                    let token = Token(self.next_token);
                    self.next_token += 1;

                    println!("New connection from {} (token={:?})", addr, token);

                    self.poll.registry().register(
                        &mut stream,
                        token,
                        Interest::READABLE,
                    )?;

                    self.clients.insert(token, Client::new(stream, addr));
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        Ok(())
    }

    fn handle_client_readable(&mut self, token: Token) -> io::Result<()> {
        if let Some(client) = self.clients.get_mut(&token) {
            match client.readable()? {
                Some(data) if !data.is_empty() => {
                    // Echo back the data (simplified WebSocket frame handling)
                    client.queue_write(&self.poll, token, data)?;
                }
                Some(_) => {
                    // Empty read, connection still alive
                }
                None => {
                    // Connection closed
                    println!("Connection closed: {:?}", token);
                    self.clients.remove(&token);
                }
            }
        }

        Ok(())
    }

    fn handle_client_writable(&mut self, token: Token) -> io::Result<()> {
        if let Some(client) = self.clients.get_mut(&token) {
            client.writable(&self.poll, token)?;
        }

        Ok(())
    }

    fn run(&mut self) -> io::Result<()> {
        let mut events = Events::with_capacity(1024);

        loop {
            self.poll.poll(&mut events, None)?;

            for event in events.iter() {
                match event.token() {
                    SERVER => {
                        self.accept_connections()?;
                    }
                    token => {
                        if event.is_readable() {
                            if let Err(e) = self.handle_client_readable(token) {
                                eprintln!("Error handling read for {:?}: {}", token, e);
                                self.clients.remove(&token);
                            }
                        }

                        if event.is_writable() {
                            if let Err(e) = self.handle_client_writable(token) {
                                eprintln!("Error handling write for {:?}: {}", token, e);
                                self.clients.remove(&token);
                            }
                        }
                    }
                }
            }
        }
    }
}

fn main() -> io::Result<()> {
    let mut server = WebSocketServer::new("0.0.0.0:8080")?;
    server.run()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_client_buffer() {
        // Test client write buffer management
        let data = vec![1, 2, 3, 4, 5];
        assert_eq!(data.len(), 5);
    }
}
```

## Key Concepts Explained

### Edge-Triggered vs Level-Triggered

**Level-Triggered (LT)**:
- Events are triggered as long as the condition is true
- If you don't read all data, the event will fire again
- Easier to use but potentially less efficient

**Edge-Triggered (ET)**:
- Events are triggered only when the state changes
- You must read/write until you get EAGAIN/EWOULDBLOCK
- More efficient but requires careful handling
- Used in the examples above

### Why This Matters for WebSockets

WebSocket servers typically handle:
- Thousands of concurrent connections
- Mostly idle connections (waiting for messages)
- Sporadic bursts of activity

Traditional approaches like thread-per-connection would require thousands of threads, each consuming memory and causing context-switching overhead. With epoll/kqueue:
- One thread can handle 10,000+ connections
- Memory footprint is minimal
- CPU only works when there's actual I/O
- Scales linearly with actual activity, not connection count

### Performance Characteristics

**epoll/kqueue advantages**:
- O(1) performance for adding/removing/modifying descriptors
- O(number of ready descriptors) for waiting
- No limit on number of file descriptors (unlike select)
- Kernel maintains the ready list

**Compared to select/poll**:
- select: O(n) scan, limited to 1024 descriptors by default
- poll: O(n) scan, no hard limit but inefficient at scale
- epoll/kqueue: O(1) operations, scales to millions of connections

## Summary

Async I/O with epoll/kqueue provides a high-performance foundation for WebSocket servers by enabling efficient event-driven I/O multiplexing. Instead of dedicating resources to each connection, a single thread monitors thousands of sockets and responds only when activity occurs. The edge-triggered mode ensures maximum efficiency by requiring applications to exhaust all available I/O before returning to the event loop.

The key advantages are:
- **Scalability**: Handle 10,000+ concurrent connections with minimal resources
- **Efficiency**: CPU cycles spent only on actual I/O operations
- **Low latency**: No thread context switching overhead
- **Resource conservation**: Minimal memory per connection

This architecture is fundamental to modern high-performance network servers and is the building block used by frameworks like Node.js (libuv), nginx, Redis, and many WebSocket libraries. The C example demonstrates raw epoll usage on Linux, the C++ example shows kqueue on BSD/macOS with better abstractions, and the Rust example leverages the `mio` library which provides a cross-platform abstraction over both epoll and kqueue with Rust's safety guarantees.