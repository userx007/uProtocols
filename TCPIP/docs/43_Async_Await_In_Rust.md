# Async/Await in Rust

## Overview

Asynchronous programming in Rust enables writing concurrent code that can handle thousands of simultaneous network connections efficiently without creating a thread per connection. Rust's async/await syntax, combined with runtime libraries like **Tokio** and **async-std**, provides a zero-cost abstraction for building high-performance network applications.

Unlike traditional blocking I/O where each operation halts thread execution, async operations return immediately and allow the runtime to schedule other tasks while waiting for I/O completion. This cooperative multitasking model is ideal for I/O-bound workloads like web servers, proxies, and network clients.

## Core Concepts

### Futures
A `Future` is a value that may not be ready yet. It implements the `Future` trait with a `poll` method that the runtime calls to check if the value is ready.

### async/await Syntax
- `async fn` returns a `Future`
- `await` suspends execution until a `Future` completes
- Async blocks create inline futures: `async { ... }`

### Runtimes
Rust's async is runtime-agnostic. Popular runtimes include:
- **Tokio**: Production-grade, feature-rich, most widely used
- **async-std**: Standard library-like API, simpler interface
- **smol**: Minimal, lightweight runtime

---

## Comparison with C/C++ Approaches

Before diving into Rust examples, let's see how C/C++ handle async I/O:

### C - Using epoll (Linux)

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

#define MAX_EVENTS 10
#define PORT 8080

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event ev, events[MAX_EVENTS];
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(1);
    }
    
    set_nonblocking(listen_fd);
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }
    
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(1);
    }
    
    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    // Add listening socket to epoll
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl");
        exit(1);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Event loop
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(1);
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                // Accept new connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, 
                                      (struct sockaddr*)&client_addr, 
                                      &client_len);
                
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }
                
                set_nonblocking(client_fd);
                
                // Add client to epoll
                ev.events = EPOLLIN | EPOLLET; // Edge-triggered
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                
                printf("Accepted connection from client fd=%d\n", client_fd);
            } else {
                // Handle client data
                char buffer[1024];
                int fd = events[i].data.fd;
                
                ssize_t n = read(fd, buffer, sizeof(buffer));
                if (n <= 0) {
                    if (n == 0 || errno != EAGAIN) {
                        printf("Closing connection fd=%d\n", fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    }
                } else {
                    // Echo back
                    write(fd, buffer, n);
                }
            }
        }
    }
    
    close(listen_fd);
    close(epoll_fd);
    return 0;
}
```

### C++ - Using Boost.Asio

```cpp
#include <iostream>
#include <memory>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket) : socket_(std::move(socket)) {}
    
    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(
            boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    do_write(length);
                }
            });
    }
    
    void do_write(std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(data_, length),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    do_read();
                }
            });
    }
    
    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
};

class Server {
public:
    Server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Session>(std::move(socket))->start();
                }
                do_accept();
            });
    }
    
    tcp::acceptor acceptor_;
};

int main() {
    try {
        boost::asio::io_context io_context;
        Server server(io_context, 8080);
        std::cout << "Server listening on port 8080\n";
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
```

---

## Rust Async/Await with Tokio

### Basic TCP Echo Server

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::error::Error;

async fn handle_client(mut socket: TcpStream, addr: std::net::SocketAddr) {
    println!("Accepted connection from: {}", addr);
    
    let mut buffer = [0u8; 1024];
    
    loop {
        match socket.read(&mut buffer).await {
            Ok(0) => {
                // Connection closed
                println!("Connection closed: {}", addr);
                break;
            }
            Ok(n) => {
                // Echo back
                if let Err(e) = socket.write_all(&buffer[..n]).await {
                    eprintln!("Failed to write to socket: {}", e);
                    break;
                }
            }
            Err(e) => {
                eprintln!("Failed to read from socket: {}", e);
                break;
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Server listening on 127.0.0.1:8080");
    
    loop {
        let (socket, addr) = listener.accept().await?;
        
        // Spawn a new task for each connection
        tokio::spawn(async move {
            handle_client(socket, addr).await;
        });
    }
}
```

### Concurrent HTTP Requests

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncWriteExt, AsyncReadExt};
use std::error::Error;

async fn fetch_url(host: &str, path: &str) -> Result<String, Box<dyn Error>> {
    // Connect to the server
    let mut stream = TcpStream::connect(format!("{}:80", host)).await?;
    
    // Send HTTP request
    let request = format!(
        "GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        path, host
    );
    
    stream.write_all(request.as_bytes()).await?;
    
    // Read response
    let mut response = String::new();
    stream.read_to_string(&mut response).await?;
    
    Ok(response)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    println!("Fetching multiple URLs concurrently...\n");
    
    // Launch multiple requests concurrently
    let handles = vec![
        tokio::spawn(fetch_url("example.com", "/")),
        tokio::spawn(fetch_url("httpbin.org", "/ip")),
        tokio::spawn(fetch_url("ifconfig.me", "/")),
    ];
    
    // Wait for all to complete
    for (i, handle) in handles.into_iter().enumerate() {
        match handle.await {
            Ok(Ok(response)) => {
                let lines: Vec<&str> = response.lines().take(5).collect();
                println!("Request {} response (first 5 lines):", i + 1);
                for line in lines {
                    println!("  {}", line);
                }
                println!();
            }
            Ok(Err(e)) => eprintln!("Request {} failed: {}", i + 1, e),
            Err(e) => eprintln!("Task {} panicked: {}", i + 1, e),
        }
    }
    
    Ok(())
}
```

### Advanced: Select and Timeout

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::time::{sleep, Duration, timeout};
use std::error::Error;

async fn process_with_timeout(mut stream: TcpStream) -> Result<(), Box<dyn Error>> {
    let mut buffer = [0u8; 1024];
    
    // Read with a 5-second timeout
    match timeout(Duration::from_secs(5), stream.read(&mut buffer)).await {
        Ok(Ok(n)) if n > 0 => {
            println!("Received {} bytes within timeout", n);
            stream.write_all(&buffer[..n]).await?;
        }
        Ok(Ok(_)) => {
            println!("Connection closed");
        }
        Ok(Err(e)) => {
            eprintln!("Read error: {}", e);
        }
        Err(_) => {
            eprintln!("Operation timed out!");
        }
    }
    
    Ok(())
}

async fn select_example() {
    let task1 = sleep(Duration::from_secs(2));
    let task2 = sleep(Duration::from_secs(1));
    
    tokio::select! {
        _ = task1 => println!("Task 1 completed first"),
        _ = task2 => println!("Task 2 completed first"),
    }
}

#[tokio::main]
async fn main() {
    println!("=== Timeout Example ===");
    select_example().await;
}
```

### Connection Pool with Semaphore

```rust
use tokio::net::TcpStream;
use tokio::sync::Semaphore;
use std::sync::Arc;
use std::error::Error;

struct ConnectionPool {
    semaphore: Arc<Semaphore>,
    max_connections: usize,
}

impl ConnectionPool {
    fn new(max_connections: usize) -> Self {
        ConnectionPool {
            semaphore: Arc::new(Semaphore::new(max_connections)),
            max_connections,
        }
    }
    
    async fn acquire(&self) -> Result<ConnectionGuard, Box<dyn Error>> {
        let permit = self.semaphore.clone().acquire_owned().await?;
        Ok(ConnectionGuard { _permit: permit })
    }
    
    fn available(&self) -> usize {
        self.semaphore.available_permits()
    }
}

struct ConnectionGuard {
    _permit: tokio::sync::OwnedSemaphorePermit,
}

async fn use_connection(pool: Arc<ConnectionPool>, id: usize) {
    println!("[{}] Waiting for connection slot...", id);
    
    let _guard = pool.acquire().await.unwrap();
    println!("[{}] Acquired connection (available: {})", 
             id, pool.available());
    
    // Simulate work
    tokio::time::sleep(tokio::time::Duration::from_secs(2)).await;
    
    println!("[{}] Releasing connection", id);
    // Guard dropped here, releasing the permit
}

#[tokio::main]
async fn main() {
    let pool = Arc::new(ConnectionPool::new(3));
    
    let mut handles = vec![];
    
    // Try to create 10 concurrent "connections"
    for i in 0..10 {
        let pool_clone = pool.clone();
        handles.push(tokio::spawn(async move {
            use_connection(pool_clone, i).await;
        }));
    }
    
    // Wait for all tasks
    for handle in handles {
        handle.await.unwrap();
    }
    
    println!("All connections completed");
}
```

---

## Rust Async/Await with async-std

async-std provides a familiar standard library-like API:

### Basic TCP Server

```rust
use async_std::net::{TcpListener, TcpStream};
use async_std::prelude::*;
use async_std::task;

async fn handle_client(mut stream: TcpStream) -> std::io::Result<()> {
    let mut buffer = [0u8; 1024];
    
    loop {
        let n = stream.read(&mut buffer).await?;
        
        if n == 0 {
            return Ok(());
        }
        
        stream.write_all(&buffer[..n]).await?;
    }
}

#[async_std::main]
async fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Server listening on 127.0.0.1:8080");
    
    let mut incoming = listener.incoming();
    
    while let Some(stream) = incoming.next().await {
        let stream = stream?;
        
        task::spawn(async move {
            if let Err(e) = handle_client(stream).await {
                eprintln!("Error handling client: {}", e);
            }
        });
    }
    
    Ok(())
}
```

### Async Channels for Communication

```rust
use async_std::channel::{bounded, Sender, Receiver};
use async_std::task;
use std::time::Duration;

async fn producer(tx: Sender<i32>, id: i32) {
    for i in 0..5 {
        let value = id * 10 + i;
        println!("Producer {} sending: {}", id, value);
        tx.send(value).await.unwrap();
        task::sleep(Duration::from_millis(100)).await;
    }
    println!("Producer {} done", id);
}

async fn consumer(rx: Receiver<i32>) {
    while let Ok(value) = rx.recv().await {
        println!("Consumer received: {}", value);
        task::sleep(Duration::from_millis(50)).await;
    }
    println!("Consumer done");
}

#[async_std::main]
async fn main() {
    let (tx, rx) = bounded(10);
    
    // Spawn multiple producers
    let tx1 = tx.clone();
    let tx2 = tx.clone();
    
    let p1 = task::spawn(producer(tx1, 1));
    let p2 = task::spawn(producer(tx2, 2));
    let c = task::spawn(consumer(rx));
    
    // Drop original sender so consumer can finish
    drop(tx);
    
    p1.await;
    p2.await;
    c.await;
}
```

---

## Advanced Patterns

### Stream Processing

```rust
use tokio::net::TcpListener;
use tokio_stream::{StreamExt, wrappers::TcpListenerStream};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    let mut stream = TcpListenerStream::new(listener);
    
    println!("Server listening...");
    
    // Process connections as a stream
    while let Some(socket) = stream.next().await {
        match socket {
            Ok(socket) => {
                let addr = socket.peer_addr()?;
                println!("New connection from: {}", addr);
                
                tokio::spawn(async move {
                    // Handle connection
                });
            }
            Err(e) => eprintln!("Accept error: {}", e),
        }
    }
    
    Ok(())
}
```

### Custom Future Implementation

```rust
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};
use std::time::{Duration, Instant};

struct Delay {
    when: Instant,
}

impl Future for Delay {
    type Output = ();
    
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        if Instant::now() >= self.when {
            Poll::Ready(())
        } else {
            // Wake up later (in real impl, would register with reactor)
            cx.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

async fn use_custom_future() {
    println!("Starting delay...");
    
    let delay = Delay {
        when: Instant::now() + Duration::from_secs(2),
    };
    
    delay.await;
    println!("Delay completed!");
}

#[tokio::main]
async fn main() {
    use_custom_future().await;
}
```

### Async Trait Methods

```rust
use async_trait::async_trait;
use std::error::Error;

#[async_trait]
trait NetworkService {
    async fn connect(&self, addr: &str) -> Result<(), Box<dyn Error>>;
    async fn send(&self, data: &[u8]) -> Result<usize, Box<dyn Error>>;
    async fn receive(&self) -> Result<Vec<u8>, Box<dyn Error>>;
}

struct TcpService;

#[async_trait]
impl NetworkService for TcpService {
    async fn connect(&self, addr: &str) -> Result<(), Box<dyn Error>> {
        println!("Connecting to {}...", addr);
        tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
        println!("Connected!");
        Ok(())
    }
    
    async fn send(&self, data: &[u8]) -> Result<usize, Box<dyn Error>> {
        println!("Sending {} bytes", data.len());
        Ok(data.len())
    }
    
    async fn receive(&self) -> Result<Vec<u8>, Box<dyn Error>> {
        tokio::time::sleep(tokio::time::Duration::from_millis(50)).await;
        Ok(vec![1, 2, 3, 4])
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let service = TcpService;
    
    service.connect("127.0.0.1:8080").await?;
    service.send(b"Hello").await?;
    let data = service.receive().await?;
    println!("Received: {:?}", data);
    
    Ok(())
}
```

### Graceful Shutdown

```rust
use tokio::net::TcpListener;
use tokio::signal;
use tokio::sync::broadcast;
use std::error::Error;

async fn run_server(
    mut shutdown_rx: broadcast::Receiver<()>
) -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Server listening on 127.0.0.1:8080");
    
    loop {
        tokio::select! {
            result = listener.accept() => {
                let (socket, addr) = result?;
                println!("Accepted connection from {}", addr);
                // Handle socket...
            }
            _ = shutdown_rx.recv() => {
                println!("Shutdown signal received");
                break;
            }
        }
    }
    
    println!("Server stopped");
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let (shutdown_tx, shutdown_rx) = broadcast::channel(1);
    
    // Spawn server
    let server_handle = tokio::spawn(run_server(shutdown_rx));
    
    // Wait for Ctrl+C
    signal::ctrl_c().await?;
    println!("\nShutting down...");
    
    // Send shutdown signal
    let _ = shutdown_tx.send(());
    
    // Wait for server to finish
    server_handle.await??;
    
    Ok(())
}
```

---

## Performance Comparison: Tokio vs async-std vs Traditional Threading

```rust
// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// async-std = { version = "1", features = ["attributes"] }

use std::time::Instant;
use tokio::task;

async fn simulate_io_work() {
    tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;
}

#[tokio::main]
async fn main() {
    let num_tasks = 10_000;
    
    // Tokio benchmark
    let start = Instant::now();
    let mut handles = vec![];
    
    for _ in 0..num_tasks {
        handles.push(task::spawn(simulate_io_work()));
    }
    
    for handle in handles {
        handle.await.unwrap();
    }
    
    let duration = start.elapsed();
    println!("Tokio: {} tasks in {:?}", num_tasks, duration);
    println!("Throughput: {:.2} tasks/sec", 
             num_tasks as f64 / duration.as_secs_f64());
}
```

---

## Summary

**Async/await in Rust** provides powerful tools for building efficient, concurrent network applications:

### Key Advantages
- **Zero-cost abstractions**: Async code compiles to efficient state machines
- **Type safety**: The compiler ensures Send/Sync correctness across await points
- **No callback hell**: Linear async/await syntax is more readable than callbacks
- **Fearless concurrency**: Ownership rules prevent data races even in concurrent code

### Tokio vs async-std
- **Tokio**: Industry standard, comprehensive feature set, excellent for production
- **async-std**: Simpler API mirroring std library, good for learning
- Both support the same `async/await` syntax and are largely interchangeable

### Compared to C/C++
- **C with epoll/kqueue**: Manual state management, verbose, error-prone
- **C++ Boost.Asio**: Callback-based, requires careful lifetime management
- **Rust async**: Type-safe, ergonomic syntax, compiler-enforced correctness

### Best Practices
- Use `tokio::spawn` for CPU-bound tasks to avoid blocking the runtime
- Apply timeouts to prevent indefinite hangs
- Use channels for task communication instead of shared state
- Implement graceful shutdown for production services
- Monitor task counts and connection pools to prevent resource exhaustion

### Common Patterns
- **Task spawning**: Handle each connection independently
- **Select/join**: Wait for multiple futures simultaneously  
- **Channels**: Communicate between tasks safely
- **Timeouts**: Bound operation duration
- **Streams**: Process sequences of values asynchronously
- **Backpressure**: Use bounded channels to control flow

Rust's async ecosystem combines the performance of low-level I/O multiplexing with high-level ergonomics and safety guarantees, making it ideal for building scalable network services.