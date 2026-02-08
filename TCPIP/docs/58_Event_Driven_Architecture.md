# Event-Driven Architecture for TCP/IP

## Detailed Description

### Overview

**Event-Driven Architecture (EDA)** is a programming paradigm where the flow of the program is determined by events such as I/O operations, timers, or signals. In TCP/IP networking, EDA is essential for building high-performance servers that can handle thousands of concurrent connections efficiently without creating a thread per connection.

### Core Concepts

**1. The C10K Problem**
Traditional thread-per-connection models fail when handling 10,000+ concurrent connections due to:
- Context switching overhead
- Memory consumption (stack space per thread)
- Kernel limitations on thread count

**2. Event-Driven Solution**
Instead of blocking on I/O operations, event-driven servers:
- Use non-blocking I/O
- Monitor multiple sockets simultaneously
- React to I/O readiness events
- Use a small thread pool or single thread

### The Two Primary Patterns

## 1. Reactor Pattern (Synchronous Event Demultiplexing)

### Architecture
```
┌─────────────┐
│   Clients   │
└──────┬──────┘
       │
   ┌───▼────┐
   │ Handles│ (socket descriptors)
   └───┬────┘
       │
   ┌───▼──────────────┐
   │ Synchronous      │
   │ Event Demuxer    │ (select/poll/epoll/kqueue)
   │                  │
   └───┬──────────────┘
       │
   ┌───▼────────┐
   │  Reactor   │
   │  Dispatcher│
   └───┬────────┘
       │
   ┌───▼──────────┐
   │ Event        │
   │ Handlers     │
   └──────────────┘
```

**Flow:**
1. Register file descriptors with event demultiplexer
2. Wait for events (readable/writable)
3. Dispatch events to appropriate handlers
4. Handlers perform I/O operations synchronously
5. Return to waiting for events

**Characteristics:**
- Application waits for I/O readiness
- Application performs I/O operations
- Used by: Redis, Node.js, Nginx (event loop)

## 2. Proactor Pattern (Asynchronous Operation Processing)

### Architecture
```
┌─────────────┐
│   Clients   │
└──────┬──────┘
       │
   ┌───▼────────────┐
   │ Asynchronous   │
   │ Operation      │
   │ Processor      │ (OS kernel: IOCP, io_uring)
   └───┬────────────┘
       │
   ┌───▼──────────┐
   │  Proactor    │
   │  Dispatcher  │
   └───┬──────────┘
       │
   ┌───▼──────────┐
   │ Completion   │
   │ Handlers     │
   └──────────────┘
```

**Flow:**
1. Initiate asynchronous I/O operation
2. OS performs I/O in background
3. Application continues other work
4. OS notifies when operation completes
5. Completion handler processes results

**Characteristics:**
- Application initiates I/O operations
- OS performs I/O and notifies completion
- Used by: Windows IOCP applications, io_uring-based servers

---

## C/C++ Implementation Examples

### 1. Reactor Pattern with epoll (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define PORT 8080

// Event handler interface
typedef struct {
    int fd;
    void (*handle_read)(int fd);
    void (*handle_write)(int fd);
} event_handler_t;

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Handle read event
void handle_client_read(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        printf("Read %zd bytes from client %d\n", bytes_read, client_fd);
        
        // Echo back to client (simplified)
        write(client_fd, buffer, bytes_read);
    }
    
    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read");
        close(client_fd);
    } else if (bytes_read == 0) {
        printf("Client %d disconnected\n", client_fd);
        close(client_fd);
    }
}

// Reactor class
typedef struct {
    int epoll_fd;
    int listen_fd;
    struct epoll_event events[MAX_EVENTS];
} reactor_t;

// Initialize reactor
reactor_t* reactor_init(int port) {
    reactor_t* reactor = malloc(sizeof(reactor_t));
    
    // Create listening socket
    reactor->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (reactor->listen_fd == -1) {
        perror("socket");
        free(reactor);
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(reactor->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(reactor->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(reactor->listen_fd);
        free(reactor);
        return NULL;
    }
    
    // Listen
    if (listen(reactor->listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(reactor->listen_fd);
        free(reactor);
        return NULL;
    }
    
    // Set non-blocking
    set_nonblocking(reactor->listen_fd);
    
    // Create epoll instance
    reactor->epoll_fd = epoll_create1(0);
    if (reactor->epoll_fd == -1) {
        perror("epoll_create1");
        close(reactor->listen_fd);
        free(reactor);
        return NULL;
    }
    
    // Register listening socket with epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered
    ev.data.fd = reactor->listen_fd;
    
    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, reactor->listen_fd, &ev) == -1) {
        perror("epoll_ctl");
        close(reactor->epoll_fd);
        close(reactor->listen_fd);
        free(reactor);
        return NULL;
    }
    
    printf("Reactor initialized on port %d\n", port);
    return reactor;
}

// Handle accept event
void handle_accept(reactor_t* reactor) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (1) {
        int client_fd = accept(reactor->listen_fd, 
                              (struct sockaddr*)&client_addr, 
                              &client_len);
        
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more connections
            } else {
                perror("accept");
                break;
            }
        }
        
        printf("Accepted connection from %s:%d (fd=%d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_fd);
        
        // Set non-blocking
        set_nonblocking(client_fd);
        
        // Register with epoll
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET; // Edge-triggered
        ev.data.fd = client_fd;
        
        if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            perror("epoll_ctl: client_fd");
            close(client_fd);
        }
    }
}

// Run reactor event loop
void reactor_run(reactor_t* reactor) {
    printf("Reactor running...\n");
    
    while (1) {
        int nfds = epoll_wait(reactor->epoll_fd, reactor->events, 
                             MAX_EVENTS, -1);
        
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            int fd = reactor->events[i].data.fd;
            
            if (fd == reactor->listen_fd) {
                // New connection
                handle_accept(reactor);
            } else {
                // Client event
                if (reactor->events[i].events & EPOLLIN) {
                    handle_client_read(fd);
                }
                
                if (reactor->events[i].events & (EPOLLERR | EPOLLHUP)) {
                    printf("Error on fd %d\n", fd);
                    epoll_ctl(reactor->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            }
        }
    }
}

// Cleanup reactor
void reactor_destroy(reactor_t* reactor) {
    close(reactor->epoll_fd);
    close(reactor->listen_fd);
    free(reactor);
}

int main() {
    reactor_t* reactor = reactor_init(PORT);
    if (!reactor) {
        return 1;
    }
    
    reactor_run(reactor);
    reactor_destroy(reactor);
    
    return 0;
}
```

### 2. Proactor Pattern with io_uring (Modern Linux)

```cpp
#include <iostream>
#include <cstring>
#include <vector>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>
#include <unistd.h>

#define QUEUE_DEPTH 256
#define BUFFER_SIZE 4096
#define PORT 8080

enum class EventType {
    ACCEPT,
    READ,
    WRITE
};

struct IORequest {
    EventType type;
    int fd;
    char buffer[BUFFER_SIZE];
    size_t length;
    sockaddr_in client_addr;
    socklen_t client_len;
};

class Proactor {
private:
    io_uring ring;
    int listen_fd;
    
    void setup_listening_socket(int port) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == -1) {
            throw std::runtime_error("socket failed");
        }
        
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
            throw std::runtime_error("bind failed");
        }
        
        if (listen(listen_fd, SOMAXCONN) == -1) {
            throw std::runtime_error("listen failed");
        }
        
        std::cout << "Proactor listening on port " << port << std::endl;
    }
    
    void submit_accept() {
        auto* req = new IORequest;
        req->type = EventType::ACCEPT;
        req->fd = listen_fd;
        req->client_len = sizeof(req->client_addr);
        
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, listen_fd, 
                            (sockaddr*)&req->client_addr,
                            &req->client_len, 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }
    
    void submit_read(int client_fd) {
        auto* req = new IORequest;
        req->type = EventType::READ;
        req->fd = client_fd;
        req->length = 0;
        
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, client_fd, req->buffer, BUFFER_SIZE, 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }
    
    void submit_write(int client_fd, const char* data, size_t length) {
        auto* req = new IORequest;
        req->type = EventType::WRITE;
        req->fd = client_fd;
        req->length = length;
        memcpy(req->buffer, data, length);
        
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_send(sqe, client_fd, req->buffer, length, 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }
    
    void handle_completion(io_uring_cqe* cqe) {
        auto* req = static_cast<IORequest*>(io_uring_cqe_get_data(cqe));
        
        switch (req->type) {
            case EventType::ACCEPT: {
                if (cqe->res >= 0) {
                    int client_fd = cqe->res;
                    std::cout << "Accepted connection: fd=" << client_fd << std::endl;
                    
                    // Submit read for new client
                    submit_read(client_fd);
                    
                    // Submit another accept
                    submit_accept();
                } else {
                    std::cerr << "Accept failed: " << strerror(-cqe->res) << std::endl;
                }
                break;
            }
            
            case EventType::READ: {
                if (cqe->res > 0) {
                    size_t bytes_read = cqe->res;
                    std::cout << "Read " << bytes_read << " bytes from fd=" 
                             << req->fd << std::endl;
                    
                    // Echo back (submit write)
                    submit_write(req->fd, req->buffer, bytes_read);
                    
                    // Submit another read
                    submit_read(req->fd);
                    
                } else if (cqe->res == 0) {
                    std::cout << "Client fd=" << req->fd << " disconnected" << std::endl;
                    close(req->fd);
                } else {
                    std::cerr << "Read error on fd=" << req->fd << ": " 
                             << strerror(-cqe->res) << std::endl;
                    close(req->fd);
                }
                break;
            }
            
            case EventType::WRITE: {
                if (cqe->res > 0) {
                    std::cout << "Wrote " << cqe->res << " bytes to fd=" 
                             << req->fd << std::endl;
                } else {
                    std::cerr << "Write error on fd=" << req->fd << std::endl;
                    close(req->fd);
                }
                break;
            }
        }
        
        delete req;
    }
    
public:
    Proactor(int port) {
        // Initialize io_uring
        if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
            throw std::runtime_error("io_uring_queue_init failed");
        }
        
        setup_listening_socket(port);
        
        // Submit initial accept
        submit_accept();
    }
    
    ~Proactor() {
        io_uring_queue_exit(&ring);
        close(listen_fd);
    }
    
    void run() {
        std::cout << "Proactor running..." << std::endl;
        
        while (true) {
            io_uring_cqe* cqe;
            
            // Wait for completion
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << std::endl;
                break;
            }
            
            // Handle completion
            handle_completion(cqe);
            
            // Mark CQE as seen
            io_uring_cqe_seen(&ring, cqe);
        }
    }
};

int main() {
    try {
        Proactor proactor(PORT);
        proactor.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

---

## Rust Implementation Examples

### 1. Reactor Pattern with Tokio

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::error::Error;

/// Reactor-based echo server using Tokio
/// Tokio implements the Reactor pattern internally with epoll/kqueue
struct ReactorServer {
    addr: String,
}

impl ReactorServer {
    fn new(addr: String) -> Self {
        Self { addr }
    }
    
    /// Handle a single client connection
    async fn handle_client(mut socket: TcpStream, addr: std::net::SocketAddr) {
        println!("Accepted connection from: {}", addr);
        
        let mut buffer = vec![0u8; 4096];
        
        loop {
            // Wait for socket to be readable (reactor pattern)
            match socket.read(&mut buffer).await {
                Ok(0) => {
                    // Connection closed
                    println!("Client {} disconnected", addr);
                    break;
                }
                Ok(n) => {
                    println!("Read {} bytes from {}", n, addr);
                    
                    // Echo back
                    if let Err(e) = socket.write_all(&buffer[..n]).await {
                        eprintln!("Failed to write to {}: {}", addr, e);
                        break;
                    }
                }
                Err(e) => {
                    eprintln!("Failed to read from {}: {}", addr, e);
                    break;
                }
            }
        }
    }
    
    /// Run the reactor event loop
    async fn run(&self) -> Result<(), Box<dyn Error>> {
        // Create TCP listener
        let listener = TcpListener::bind(&self.addr).await?;
        println!("Reactor server listening on: {}", self.addr);
        
        loop {
            // Wait for new connection (reactor pattern)
            let (socket, addr) = listener.accept().await?;
            
            // Spawn task to handle client
            // Each task is lightweight (green thread)
            tokio::spawn(async move {
                Self::handle_client(socket, addr).await;
            });
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let server = ReactorServer::new("127.0.0.1:8080".to_string());
    server.run().await
}
```

### 2. Advanced Reactor with mio (Low-level)

```rust
use mio::{Events, Interest, Poll, Token};
use mio::net::{TcpListener, TcpStream};
use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::SocketAddr;

const SERVER: Token = Token(0);
const MAX_EVENTS: usize = 1024;

/// Client connection state
struct Connection {
    socket: TcpStream,
    token: Token,
    addr: SocketAddr,
    buffer: Vec<u8>,
    bytes_read: usize,
}

impl Connection {
    fn new(socket: TcpStream, token: Token, addr: SocketAddr) -> Self {
        Self {
            socket,
            token,
            addr,
            buffer: vec![0u8; 4096],
            bytes_read: 0,
        }
    }
    
    /// Try to read from socket
    fn readable(&mut self) -> io::Result<bool> {
        loop {
            match self.socket.read(&mut self.buffer[self.bytes_read..]) {
                Ok(0) => {
                    // Connection closed
                    return Ok(false);
                }
                Ok(n) => {
                    self.bytes_read += n;
                    println!("Read {} bytes from {}", n, self.addr);
                    
                    if self.bytes_read == self.buffer.len() {
                        // Buffer full, echo back
                        return Ok(true);
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // No more data available
                    break;
                }
                Err(e) => return Err(e),
            }
        }
        
        Ok(true)
    }
    
    /// Try to write to socket
    fn writable(&mut self) -> io::Result<bool> {
        if self.bytes_read == 0 {
            return Ok(true);
        }
        
        match self.socket.write(&self.buffer[..self.bytes_read]) {
            Ok(n) => {
                println!("Wrote {} bytes to {}", n, self.addr);
                self.bytes_read = 0;
                Ok(true)
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                Ok(true)
            }
            Err(e) => Err(e),
        }
    }
}

/// Reactor implementation using mio
struct Reactor {
    poll: Poll,
    listener: TcpListener,
    connections: HashMap<Token, Connection>,
    next_token: usize,
}

impl Reactor {
    fn new(addr: &str) -> io::Result<Self> {
        // Create poll instance (epoll/kqueue wrapper)
        let poll = Poll::new()?;
        
        // Create listening socket
        let listener = TcpListener::bind(addr.parse().unwrap())?;
        
        // Register listener with poll
        poll.registry().register(
            &mut &listener,
            SERVER,
            Interest::READABLE
        )?;
        
        println!("Reactor listening on: {}", addr);
        
        Ok(Self {
            poll,
            listener,
            connections: HashMap::new(),
            next_token: 1,
        })
    }
    
    /// Accept new connection
    fn accept(&mut self) -> io::Result<()> {
        loop {
            match self.listener.accept() {
                Ok((mut socket, addr)) => {
                    let token = Token(self.next_token);
                    self.next_token += 1;
                    
                    println!("Accepted connection from {} (token={:?})", addr, token);
                    
                    // Register socket with poll
                    self.poll.registry().register(
                        &mut socket,
                        token,
                        Interest::READABLE | Interest::WRITABLE
                    )?;
                    
                    // Store connection
                    let conn = Connection::new(socket, token, addr);
                    self.connections.insert(token, conn);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    break;
                }
                Err(e) => return Err(e),
            }
        }
        
        Ok(())
    }
    
    /// Run reactor event loop
    fn run(&mut self) -> io::Result<()> {
        let mut events = Events::with_capacity(MAX_EVENTS);
        
        println!("Reactor running...");
        
        loop {
            // Wait for events (blocks here)
            self.poll.poll(&mut events, None)?;
            
            for event in events.iter() {
                match event.token() {
                    SERVER => {
                        // New connection
                        self.accept()?;
                    }
                    token => {
                        // Client event
                        let done = if event.is_readable() {
                            self.handle_read(token)?
                        } else if event.is_writable() {
                            self.handle_write(token)?
                        } else {
                            false
                        };
                        
                        if !done {
                            self.connections.remove(&token);
                        }
                    }
                }
            }
        }
    }
    
    fn handle_read(&mut self, token: Token) -> io::Result<bool> {
        if let Some(conn) = self.connections.get_mut(&token) {
            conn.readable()
        } else {
            Ok(false)
        }
    }
    
    fn handle_write(&mut self, token: Token) -> io::Result<bool> {
        if let Some(conn) = self.connections.get_mut(&token) {
            conn.writable()
        } else {
            Ok(false)
        }
    }
}

fn main() -> io::Result<()> {
    let mut reactor = Reactor::new("127.0.0.1:8080")?;
    reactor.run()
}
```

### 3. Proactor Pattern with Tokio-uring

```rust
use tokio_uring::net::{TcpListener, TcpStream};
use std::rc::Rc;

/// Proactor-based server using io_uring
/// Operations are truly asynchronous - kernel does the I/O
struct ProactorServer {
    addr: String,
}

impl ProactorServer {
    fn new(addr: String) -> Self {
        Self { addr }
    }
    
    /// Handle client connection with proactor pattern
    async fn handle_client(stream: TcpStream, addr: std::net::SocketAddr) {
        println!("Accepted connection from: {}", addr);
        
        let mut buffer = vec![0u8; 4096];
        
        loop {
            // Initiate async read (proactor pattern)
            // The kernel performs the actual I/O
            let (result, buf) = stream.read(buffer).await;
            buffer = buf;
            
            match result {
                Ok(0) => {
                    println!("Client {} disconnected", addr);
                    break;
                }
                Ok(n) => {
                    println!("Read {} bytes from {}", n, addr);
                    
                    // Initiate async write
                    let write_buf = buffer[..n].to_vec();
                    let (result, _) = stream.write(write_buf).await;
                    
                    if let Err(e) = result {
                        eprintln!("Write error: {}", e);
                        break;
                    }
                }
                Err(e) => {
                    eprintln!("Read error: {}", e);
                    break;
                }
            }
        }
    }
    
    /// Run proactor event loop
    async fn run(&self) {
        // Create TCP listener
        let listener = TcpListener::bind(self.addr.parse().unwrap())
            .expect("Failed to bind");
        
        println!("Proactor server listening on: {}", self.addr);
        
        loop {
            // Initiate async accept
            match listener.accept().await {
                Ok((stream, addr)) => {
                    // Spawn task to handle client
                    tokio_uring::spawn(async move {
                        Self::handle_client(stream, addr).await;
                    });
                }
                Err(e) => {
                    eprintln!("Accept error: {}", e);
                }
            }
        }
    }
}

fn main() {
    // Run on io_uring runtime
    tokio_uring::start(async {
        let server = ProactorServer::new("127.0.0.1:8080".to_string());
        server.run().await;
    });
}
```

---

## Performance Comparison

### Reactor vs Proactor

| Aspect | Reactor | Proactor |
|--------|---------|----------|
| **I/O Model** | Synchronous (readiness) | Asynchronous (completion) |
| **Who does I/O** | Application | Operating System |
| **Notification** | "Ready to read/write" | "Operation completed" |
| **System Calls** | More (check + I/O) | Fewer (just initiate) |
| **Portability** | High (select/poll/epoll/kqueue) | Limited (IOCP, io_uring) |
| **Complexity** | Moderate | Higher |
| **CPU Efficiency** | Good | Excellent |
| **Use Cases** | Most servers | Ultra high-performance |

### Platform Support

**Reactor APIs:**
- Linux: `epoll`, `io_uring` (polling mode)
- BSD/macOS: `kqueue`
- Windows: `select`, `WSAPoll`
- Portable: `select`, `poll`

**Proactor APIs:**
- Windows: IOCP (I/O Completion Ports)
- Linux: `io_uring` (modern, 5.1+)
- Solaris: `/dev/poll`, event ports

---

## Summary

**Event-Driven Architecture** solves the C10K problem by using non-blocking I/O and event notification instead of thread-per-connection models.

**Key Points:**

1. **Reactor Pattern** (Synchronous):
   - Waits for I/O readiness notifications
   - Application performs I/O when ready
   - Implemented with: epoll (Linux), kqueue (BSD/macOS)
   - Used by: Redis, Nginx, Node.js, Tokio (Rust)

2. **Proactor Pattern** (Asynchronous):
   - Initiates I/O operations asynchronously
   - OS performs I/O and notifies on completion
   - Implemented with: io_uring (Linux), IOCP (Windows)
   - Used by: High-performance Windows servers, modern Linux servers

3. **Benefits:**
   - Handles thousands of connections efficiently
   - Low memory overhead
   - Minimal context switching
   - Scales to C10K+ connections

4. **Trade-offs:**
   - Reactor: More portable, simpler, slightly more system calls
   - Proactor: Better performance, less portable, more complex

5. **Modern Implementations:**
   - **C/C++**: libevent, libuv, Boost.Asio
   - **Rust**: Tokio (reactor), tokio-uring (proactor), mio (low-level reactor)
   - **Node.js**: libuv (reactor with proactor emulation)

Choose **Reactor** for portability and simplicity, **Proactor** for maximum performance on supported platforms.