# Connection Pooling

## Overview

Connection pooling is a software design pattern used to maintain a cache of reusable network connections. Instead of establishing a new TCP connection for each request and tearing it down afterward, applications maintain a pool of persistent connections that can be reused across multiple requests. This technique significantly reduces connection overhead, improves throughput, and provides better resource utilization.

## Why Connection Pooling Matters

Establishing a TCP connection involves a three-way handshake (SYN, SYN-ACK, ACK), which introduces latency—typically one round-trip time (RTT). For applications making frequent connections to the same server, this overhead accumulates quickly:

- **Connection establishment cost**: DNS lookup + TCP handshake + TLS handshake (if using HTTPS)
- **Connection teardown cost**: Four-way FIN handshake
- **Resource consumption**: Each new connection requires kernel resources (file descriptors, memory buffers)

Connection pooling eliminates most of these costs by reusing existing connections, making it essential for high-performance applications like web servers, database clients, and microservices.

## Key Concepts

### Pool Management Strategies

1. **Fixed-size pools**: Maintain a constant number of connections
2. **Dynamic pools**: Grow and shrink based on demand (min/max bounds)
3. **Per-thread pools**: Each thread maintains its own connection set
4. **Global pools**: Shared across all threads with synchronization

### Connection Lifecycle

1. **Creation**: Establish connection when pool initializes or grows
2. **Checkout**: Acquire connection from pool for use
3. **Validation**: Check if connection is still alive before use
4. **Return**: Release connection back to pool after use
5. **Eviction**: Remove stale or broken connections

### Important Parameters

- **Max pool size**: Maximum number of connections to maintain
- **Min pool size**: Minimum idle connections to keep ready
- **Connection timeout**: How long to wait for an available connection
- **Idle timeout**: When to close unused connections
- **Max lifetime**: Maximum age of a connection before recycling

## C Implementation

Here's a thread-safe connection pool implementation in C:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define MAX_POOL_SIZE 10
#define MIN_POOL_SIZE 2
#define IDLE_TIMEOUT 60  // seconds
#define MAX_LIFETIME 300 // seconds

typedef struct {
    int fd;
    time_t created;
    time_t last_used;
    int in_use;
} connection_t;

typedef struct {
    connection_t connections[MAX_POOL_SIZE];
    int size;
    char *host;
    int port;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} connection_pool_t;

// Initialize the connection pool
connection_pool_t* pool_init(const char *host, int port) {
    connection_pool_t *pool = malloc(sizeof(connection_pool_t));
    if (!pool) return NULL;
    
    pool->size = 0;
    pool->host = strdup(host);
    pool->port = port;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    
    memset(pool->connections, 0, sizeof(pool->connections));
    
    // Pre-create minimum connections
    for (int i = 0; i < MIN_POOL_SIZE; i++) {
        connection_t *conn = &pool->connections[i];
        conn->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (conn->fd < 0) continue;
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &server_addr.sin_addr);
        
        if (connect(conn->fd, (struct sockaddr*)&server_addr, 
                   sizeof(server_addr)) == 0) {
            conn->created = time(NULL);
            conn->last_used = conn->created;
            conn->in_use = 0;
            pool->size++;
        } else {
            close(conn->fd);
            conn->fd = -1;
        }
    }
    
    return pool;
}

// Validate if connection is still alive
int connection_is_valid(connection_t *conn) {
    if (conn->fd < 0) return 0;
    
    // Check if connection is too old
    time_t now = time(NULL);
    if (now - conn->created > MAX_LIFETIME) return 0;
    
    // Simple check: try to peek at socket
    char buf;
    int result = recv(conn->fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
    if (result == 0) return 0; // Connection closed
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return 0;
    
    return 1;
}

// Acquire a connection from the pool
connection_t* pool_acquire(connection_pool_t *pool, int timeout_sec) {
    pthread_mutex_lock(&pool->mutex);
    
    time_t start = time(NULL);
    connection_t *conn = NULL;
    
    while (!conn) {
        // Look for available connection
        for (int i = 0; i < MAX_POOL_SIZE; i++) {
            connection_t *c = &pool->connections[i];
            if (c->fd >= 0 && !c->in_use && connection_is_valid(c)) {
                c->in_use = 1;
                c->last_used = time(NULL);
                conn = c;
                break;
            }
        }
        
        if (conn) break;
        
        // Try to create new connection if pool not full
        if (pool->size < MAX_POOL_SIZE) {
            for (int i = 0; i < MAX_POOL_SIZE; i++) {
                if (pool->connections[i].fd < 0) {
                    conn = &pool->connections[i];
                    conn->fd = socket(AF_INET, SOCK_STREAM, 0);
                    
                    struct sockaddr_in server_addr;
                    memset(&server_addr, 0, sizeof(server_addr));
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(pool->port);
                    inet_pton(AF_INET, pool->host, &server_addr.sin_addr);
                    
                    if (connect(conn->fd, (struct sockaddr*)&server_addr,
                               sizeof(server_addr)) == 0) {
                        conn->created = time(NULL);
                        conn->last_used = conn->created;
                        conn->in_use = 1;
                        pool->size++;
                        break;
                    } else {
                        close(conn->fd);
                        conn->fd = -1;
                        conn = NULL;
                    }
                }
            }
            if (conn) break;
        }
        
        // Wait for connection to become available
        if (timeout_sec > 0 && (time(NULL) - start) >= timeout_sec) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL; // Timeout
        }
        
        struct timespec ts;
        ts.tv_sec = time(NULL) + 1;
        ts.tv_nsec = 0;
        pthread_cond_timedwait(&pool->cond, &pool->mutex, &ts);
    }
    
    pthread_mutex_unlock(&pool->mutex);
    return conn;
}

// Release connection back to pool
void pool_release(connection_pool_t *pool, connection_t *conn) {
    pthread_mutex_lock(&pool->mutex);
    
    if (!connection_is_valid(conn)) {
        // Connection is bad, close it
        close(conn->fd);
        conn->fd = -1;
        conn->in_use = 0;
        pool->size--;
    } else {
        conn->in_use = 0;
        conn->last_used = time(NULL);
    }
    
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

// Cleanup idle connections
void pool_cleanup_idle(connection_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    
    time_t now = time(NULL);
    for (int i = 0; i < MAX_POOL_SIZE; i++) {
        connection_t *conn = &pool->connections[i];
        if (conn->fd >= 0 && !conn->in_use && 
            pool->size > MIN_POOL_SIZE &&
            (now - conn->last_used) > IDLE_TIMEOUT) {
            close(conn->fd);
            conn->fd = -1;
            pool->size--;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

// Destroy the pool
void pool_destroy(connection_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    
    for (int i = 0; i < MAX_POOL_SIZE; i++) {
        if (pool->connections[i].fd >= 0) {
            close(pool->connections[i].fd);
        }
    }
    
    free(pool->host);
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}

// Example usage
int main() {
    connection_pool_t *pool = pool_init("127.0.0.1", 8080);
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }
    
    // Acquire connection
    connection_t *conn = pool_acquire(pool, 5);
    if (conn) {
        printf("Acquired connection (fd=%d)\n", conn->fd);
        
        // Use the connection
        const char *msg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(conn->fd, msg, strlen(msg), 0);
        
        char buffer[1024];
        int n = recv(conn->fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            printf("Received: %s\n", buffer);
        }
        
        // Release back to pool
        pool_release(pool, conn);
    }
    
    pool_cleanup_idle(pool);
    pool_destroy(pool);
    
    return 0;
}
```

## C++ Implementation

Modern C++ implementation with RAII and move semantics:

```cpp
#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class TCPConnection {
private:
    int fd_;
    std::chrono::steady_clock::time_point created_;
    std::chrono::steady_clock::time_point last_used_;
    
public:
    TCPConnection(const std::string& host, int port) 
        : fd_(-1), created_(std::chrono::steady_clock::now()) {
        
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        if (connect(fd_, (struct sockaddr*)&server_addr, 
                   sizeof(server_addr)) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to connect");
        }
        
        last_used_ = created_;
    }
    
    ~TCPConnection() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    
    // Disable copy
    TCPConnection(const TCPConnection&) = delete;
    TCPConnection& operator=(const TCPConnection&) = delete;
    
    // Enable move
    TCPConnection(TCPConnection&& other) noexcept 
        : fd_(other.fd_), created_(other.created_), 
          last_used_(other.last_used_) {
        other.fd_ = -1;
    }
    
    bool is_valid() const {
        if (fd_ < 0) return false;
        
        // Check max lifetime (5 minutes)
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - created_).count();
        if (age > 300) return false;
        
        // Check if socket is still connected
        char buf;
        int result = recv(fd_, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
        if (result == 0) return false; // Closed
        if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) 
            return false;
        
        return true;
    }
    
    void update_last_used() {
        last_used_ = std::chrono::steady_clock::now();
    }
    
    auto get_idle_time() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - last_used_).count();
    }
    
    int fd() const { return fd_; }
    
    ssize_t send(const void* data, size_t len, int flags = 0) {
        return ::send(fd_, data, len, flags);
    }
    
    ssize_t receive(void* buffer, size_t len, int flags = 0) {
        return ::recv(fd_, buffer, len, flags);
    }
};

class ConnectionPool {
private:
    struct PoolConfig {
        size_t min_size = 2;
        size_t max_size = 10;
        int idle_timeout = 60; // seconds
        int acquire_timeout = 5; // seconds
    };
    
    std::string host_;
    int port_;
    PoolConfig config_;
    
    std::queue<std::unique_ptr<TCPConnection>> available_;
    size_t total_connections_ = 0;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutting_down_ = false;
    
    void ensure_min_connections() {
        while (total_connections_ < config_.min_size) {
            try {
                auto conn = std::make_unique<TCPConnection>(host_, port_);
                available_.push(std::move(conn));
                total_connections_++;
            } catch (const std::exception& e) {
                std::cerr << "Failed to create connection: " 
                         << e.what() << std::endl;
                break;
            }
        }
    }
    
public:
    ConnectionPool(const std::string& host, int port, 
                   PoolConfig config = PoolConfig())
        : host_(host), port_(port), config_(config) {
        
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_min_connections();
    }
    
    ~ConnectionPool() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutting_down_ = true;
        cv_.notify_all();
        
        // Clear all connections
        while (!available_.empty()) {
            available_.pop();
            total_connections_--;
        }
    }
    
    class ConnectionGuard {
    private:
        ConnectionPool* pool_;
        std::unique_ptr<TCPConnection> conn_;
        
    public:
        ConnectionGuard(ConnectionPool* pool, 
                       std::unique_ptr<TCPConnection> conn)
            : pool_(pool), conn_(std::move(conn)) {}
        
        ~ConnectionGuard() {
            if (conn_ && pool_) {
                pool_->release(std::move(conn_));
            }
        }
        
        // Disable copy
        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;
        
        // Enable move
        ConnectionGuard(ConnectionGuard&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)) {
            other.pool_ = nullptr;
        }
        
        TCPConnection* operator->() { return conn_.get(); }
        TCPConnection& operator*() { return *conn_; }
        TCPConnection* get() { return conn_.get(); }
    };
    
    ConnectionGuard acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto deadline = std::chrono::steady_clock::now() + 
                       std::chrono::seconds(config_.acquire_timeout);
        
        while (true) {
            if (shutting_down_) {
                throw std::runtime_error("Pool is shutting down");
            }
            
            // Try to get an available connection
            while (!available_.empty()) {
                auto conn = std::move(available_.front());
                available_.pop();
                
                if (conn->is_valid()) {
                    conn->update_last_used();
                    return ConnectionGuard(this, std::move(conn));
                } else {
                    // Connection is invalid, discard it
                    total_connections_--;
                }
            }
            
            // Try to create a new connection if under max
            if (total_connections_ < config_.max_size) {
                try {
                    auto conn = std::make_unique<TCPConnection>(host_, port_);
                    total_connections_++;
                    conn->update_last_used();
                    return ConnectionGuard(this, std::move(conn));
                } catch (const std::exception& e) {
                    std::cerr << "Failed to create connection: " 
                             << e.what() << std::endl;
                }
            }
            
            // Wait for a connection to become available
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                throw std::runtime_error("Timeout acquiring connection");
            }
        }
    }
    
    void release(std::unique_ptr<TCPConnection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (shutting_down_ || !conn->is_valid()) {
            total_connections_--;
            return;
        }
        
        available_.push(std::move(conn));
        cv_.notify_one();
    }
    
    void cleanup_idle() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::queue<std::unique_ptr<TCPConnection>> cleaned;
        
        while (!available_.empty()) {
            auto conn = std::move(available_.front());
            available_.pop();
            
            bool should_keep = total_connections_ <= config_.min_size ||
                             conn->get_idle_time() < config_.idle_timeout;
            
            if (should_keep && conn->is_valid()) {
                cleaned.push(std::move(conn));
            } else {
                total_connections_--;
            }
        }
        
        available_ = std::move(cleaned);
        ensure_min_connections();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_connections_;
    }
    
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }
};

// Example usage
int main() {
    try {
        ConnectionPool pool("127.0.0.1", 8080);
        
        std::cout << "Pool size: " << pool.size() << std::endl;
        
        {
            // RAII: connection automatically returned to pool
            auto conn = pool.acquire();
            
            const char* request = "GET / HTTP/1.1\r\n"
                                "Host: localhost\r\n\r\n";
            conn->send(request, std::strlen(request));
            
            char buffer[1024];
            ssize_t n = conn->receive(buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                std::cout << "Received: " << buffer << std::endl;
            }
        } // Connection returned here
        
        // Simulate multiple concurrent requests
        std::vector<std::thread> threads;
        for (int i = 0; i < 5; i++) {
            threads.emplace_back([&pool, i]() {
                try {
                    auto conn = pool.acquire();
                    std::cout << "Thread " << i << " got connection\n";
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(100));
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << i << " error: " 
                             << e.what() << std::endl;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        pool.cleanup_idle();
        std::cout << "Final pool size: " << pool.size() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

Rust implementation leveraging async/await and tokio:

```rust
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpStream;
use tokio::sync::{Mutex, Semaphore};
use tokio::time::timeout;
use std::collections::VecDeque;

#[derive(Debug)]
pub struct PoolConfig {
    pub min_size: usize,
    pub max_size: usize,
    pub idle_timeout: Duration,
    pub max_lifetime: Duration,
    pub acquire_timeout: Duration,
}

impl Default for PoolConfig {
    fn default() -> Self {
        Self {
            min_size: 2,
            max_size: 10,
            idle_timeout: Duration::from_secs(60),
            max_lifetime: Duration::from_secs(300),
            acquire_timeout: Duration::from_secs(5),
        }
    }
}

struct PooledConnection {
    stream: TcpStream,
    created: Instant,
    last_used: Instant,
}

impl PooledConnection {
    fn new(stream: TcpStream) -> Self {
        let now = Instant::now();
        Self {
            stream,
            created: now,
            last_used: now,
        }
    }
    
    fn is_valid(&self, config: &PoolConfig) -> bool {
        // Check max lifetime
        if self.created.elapsed() > config.max_lifetime {
            return false;
        }
        
        // Could add additional validation here (e.g., send a ping)
        true
    }
    
    fn update_last_used(&mut self) {
        self.last_used = Instant::now();
    }
    
    fn idle_time(&self) -> Duration {
        self.last_used.elapsed()
    }
}

pub struct ConnectionPool {
    host: String,
    port: u16,
    config: PoolConfig,
    inner: Arc<Mutex<PoolInner>>,
    semaphore: Arc<Semaphore>,
}

struct PoolInner {
    available: VecDeque<PooledConnection>,
    total_connections: usize,
}

impl ConnectionPool {
    pub async fn new(host: String, port: u16, config: PoolConfig) 
        -> Result<Self, Box<dyn std::error::Error>> {
        
        let semaphore = Arc::new(Semaphore::new(config.max_size));
        let inner = Arc::new(Mutex::new(PoolInner {
            available: VecDeque::new(),
            total_connections: 0,
        }));
        
        let pool = Self {
            host,
            port,
            config,
            inner,
            semaphore,
        };
        
        // Pre-create minimum connections
        pool.ensure_min_connections().await?;
        
        Ok(pool)
    }
    
    async fn create_connection(&self) 
        -> Result<PooledConnection, Box<dyn std::error::Error>> {
        
        let addr = format!("{}:{}", self.host, self.port);
        let stream = TcpStream::connect(&addr).await?;
        Ok(PooledConnection::new(stream))
    }
    
    async fn ensure_min_connections(&self) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        let mut inner = self.inner.lock().await;
        
        while inner.total_connections < self.config.min_size {
            match self.create_connection().await {
                Ok(conn) => {
                    inner.available.push_back(conn);
                    inner.total_connections += 1;
                }
                Err(e) => {
                    eprintln!("Failed to create connection: {}", e);
                    break;
                }
            }
        }
        
        Ok(())
    }
    
    pub async fn acquire(&self) 
        -> Result<ConnectionGuard, Box<dyn std::error::Error>> {
        
        // Wait for available slot
        let permit = timeout(
            self.config.acquire_timeout,
            self.semaphore.clone().acquire_owned()
        ).await??;
        
        // Try to get existing connection or create new one
        let conn = {
            let mut inner = self.inner.lock().await;
            
            // Try to find a valid connection
            while let Some(mut conn) = inner.available.pop_front() {
                if conn.is_valid(&self.config) {
                    conn.update_last_used();
                    return Ok(ConnectionGuard {
                        connection: Some(conn),
                        pool: self.inner.clone(),
                        _permit: permit,
                    });
                } else {
                    inner.total_connections -= 1;
                }
            }
            
            // Create new connection if needed
            if inner.total_connections < self.config.max_size {
                match self.create_connection().await {
                    Ok(mut conn) => {
                        inner.total_connections += 1;
                        conn.update_last_used();
                        conn
                    }
                    Err(e) => return Err(e),
                }
            } else {
                return Err("No connections available".into());
            }
        };
        
        Ok(ConnectionGuard {
            connection: Some(conn),
            pool: self.inner.clone(),
            _permit: permit,
        })
    }
    
    pub async fn cleanup_idle(&self) {
        let mut inner = self.inner.lock().await;
        
        let mut cleaned = VecDeque::new();
        
        while let Some(conn) = inner.available.pop_front() {
            let should_keep = inner.total_connections <= self.config.min_size
                || conn.idle_time() < self.config.idle_timeout;
            
            if should_keep && conn.is_valid(&self.config) {
                cleaned.push_back(conn);
            } else {
                inner.total_connections -= 1;
            }
        }
        
        inner.available = cleaned;
    }
    
    pub async fn size(&self) -> usize {
        self.inner.lock().await.total_connections
    }
    
    pub async fn available(&self) -> usize {
        self.inner.lock().await.available.len()
    }
}

pub struct ConnectionGuard {
    connection: Option<PooledConnection>,
    pool: Arc<Mutex<PoolInner>>,
    _permit: tokio::sync::OwnedSemaphorePermit,
}

impl ConnectionGuard {
    pub fn stream(&mut self) -> &mut TcpStream {
        &mut self.connection.as_mut().unwrap().stream
    }
}

impl Drop for ConnectionGuard {
    fn drop(&mut self) {
        if let Some(conn) = self.connection.take() {
            let pool = self.pool.clone();
            tokio::spawn(async move {
                let mut inner = pool.lock().await;
                inner.available.push_back(conn);
            });
        }
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = PoolConfig::default();
    let pool = ConnectionPool::new(
        "127.0.0.1".to_string(),
        8080,
        config
    ).await?;
    
    println!("Pool size: {}", pool.size().await);
    
    // Single request
    {
        let mut conn = pool.acquire().await?;
        
        use tokio::io::{AsyncWriteExt, AsyncReadExt};
        
        conn.stream().write_all(
            b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
        ).await?;
        
        let mut buffer = vec![0u8; 1024];
        let n = conn.stream().read(&mut buffer).await?;
        
        println!("Received {} bytes", n);
    } // Connection returned to pool here
    
    // Concurrent requests
    let mut handles = vec![];
    
    for i in 0..5 {
        let pool_clone = Arc::new(pool);
        let handle = tokio::spawn(async move {
            match pool_clone.acquire().await {
                Ok(_conn) => {
                    println!("Task {} acquired connection", i);
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
                Err(e) => eprintln!("Task {} error: {}", i, e),
            }
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.await?;
    }
    
    pool.cleanup_idle().await;
    println!("Final pool size: {}", pool.size().await);
    
    Ok(())
}
```

## Summary

Connection pooling is a critical optimization technique for network applications that:

**Benefits:**
- **Reduces latency** by eliminating connection setup/teardown overhead (typically 1-3 RTTs saved per request)
- **Improves throughput** by enabling immediate request processing with warm connections
- **Conserves resources** by limiting total connections and reusing file descriptors
- **Enables better load management** through controlled connection limits
- **Reduces server load** by maintaining persistent connections instead of churning through new ones

**Key Implementation Considerations:**
- Thread-safe access to shared connection pool using mutexes/semaphores
- Connection validation before use (check for broken pipes, max lifetime)
- Dynamic sizing strategies (min/max bounds with idle cleanup)
- Proper timeout handling for both acquisition and connection operations
- RAII patterns (C++/Rust) to guarantee connections return to pool

**Common Use Cases:**
- Database clients (connection pool per database)
- HTTP client libraries (especially for microservices)
- Message queue consumers/producers
- Cache clients (Redis, Memcached)
- Any application making repeated connections to the same endpoints

The implementations shown demonstrate progressive sophistication: C provides low-level control with manual resource management, C++ adds RAII and type safety, while Rust leverages async/await for efficient concurrent access with compile-time safety guarantees. All three share the core concepts of lifecycle management, validation, and controlled resource allocation that make connection pooling effective.