# TCP Connection Management in Modbus

## Detailed Description

TCP Connection Management is a critical aspect of Modbus TCP implementations that deals with establishing, maintaining, and recovering network connections between Modbus clients and servers. Unlike Modbus RTU which uses serial connections, Modbus TCP operates over Ethernet networks using the TCP/IP protocol stack, which introduces unique challenges and opportunities for connection handling.

### Key Concepts

**Persistent Connections**: Modbus TCP typically uses persistent (keep-alive) connections rather than opening a new connection for each transaction. This reduces overhead from TCP handshakes and improves performance for frequent communications.

**Reconnection Logic**: Networks are inherently unreliable. Robust Modbus implementations must handle connection failures gracefully, implementing strategies like exponential backoff, retry limits, and health checking to recover from network issues without overwhelming the server.

**Connection Pooling**: In scenarios with multiple Modbus devices or high transaction volumes, connection pooling allows efficient reuse of TCP connections, managing a pool of active connections that can be shared across multiple requests while respecting device connection limits.

**Timeouts and Health Monitoring**: Proper timeout handling at multiple levels (connection timeout, read timeout, idle timeout) ensures that failed or stale connections are detected and replaced, preventing resource exhaustion and hung operations.

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>

#define MODBUS_TCP_PORT 502
#define MAX_RETRIES 3
#define INITIAL_BACKOFF_MS 100
#define MAX_BACKOFF_MS 5000
#define CONNECTION_TIMEOUT_SEC 5
#define IDLE_TIMEOUT_SEC 60

typedef enum {
    CONN_DISCONNECTED,
    CONN_CONNECTING,
    CONN_CONNECTED,
    CONN_ERROR
} ConnectionState;

typedef struct {
    int socket_fd;
    char host[256];
    int port;
    ConnectionState state;
    time_t last_activity;
    int retry_count;
    int backoff_ms;
} ModbusTCPConnection;

// Initialize connection structure
void modbus_connection_init(ModbusTCPConnection *conn, const char *host, int port) {
    conn->socket_fd = -1;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port > 0 ? port : MODBUS_TCP_PORT;
    conn->state = CONN_DISCONNECTED;
    conn->last_activity = time(NULL);
    conn->retry_count = 0;
    conn->backoff_ms = INITIAL_BACKOFF_MS;
}

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Enable TCP keepalive
int enable_keepalive(int fd) {
    int keepalive = 1;
    int keepidle = 60;
    int keepintvl = 10;
    int keepcnt = 3;
    
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        return -1;
    }
    
    #ifdef TCP_KEEPIDLE
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    #endif
    #ifdef TCP_KEEPINTVL
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    #endif
    #ifdef TCP_KEEPCNT
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    #endif
    
    return 0;
}

// Connect with timeout
int connect_with_timeout(ModbusTCPConnection *conn) {
    struct sockaddr_in server_addr;
    struct timeval timeout;
    fd_set write_fds;
    int result;
    
    // Create socket
    conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Set non-blocking
    if (set_nonblocking(conn->socket_fd) < 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        return -1;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(conn->port);
    
    if (inet_pton(AF_INET, conn->host, &server_addr.sin_addr) <= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        return -1;
    }
    
    // Attempt connection
    conn->state = CONN_CONNECTING;
    result = connect(conn->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (result < 0 && errno != EINPROGRESS) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        conn->state = CONN_ERROR;
        return -1;
    }
    
    // Wait for connection with timeout
    FD_ZERO(&write_fds);
    FD_SET(conn->socket_fd, &write_fds);
    timeout.tv_sec = CONNECTION_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    
    result = select(conn->socket_fd + 1, NULL, &write_fds, NULL, &timeout);
    
    if (result <= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        conn->state = CONN_ERROR;
        return -1;
    }
    
    // Check if connection succeeded
    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(conn->socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
        conn->state = CONN_ERROR;
        return -1;
    }
    
    // Enable keepalive
    enable_keepalive(conn->socket_fd);
    
    conn->state = CONN_CONNECTED;
    conn->last_activity = time(NULL);
    conn->retry_count = 0;
    conn->backoff_ms = INITIAL_BACKOFF_MS;
    
    printf("Connected to %s:%d\n", conn->host, conn->port);
    return 0;
}

// Reconnect with exponential backoff
int modbus_reconnect(ModbusTCPConnection *conn) {
    if (conn->retry_count >= MAX_RETRIES) {
        printf("Max retries reached. Giving up.\n");
        return -1;
    }
    
    printf("Reconnecting (attempt %d/%d) after %dms...\n", 
           conn->retry_count + 1, MAX_RETRIES, conn->backoff_ms);
    
    usleep(conn->backoff_ms * 1000);
    
    if (connect_with_timeout(conn) == 0) {
        return 0;
    }
    
    conn->retry_count++;
    conn->backoff_ms *= 2;
    if (conn->backoff_ms > MAX_BACKOFF_MS) {
        conn->backoff_ms = MAX_BACKOFF_MS;
    }
    
    return -1;
}

// Check connection health
int modbus_check_connection(ModbusTCPConnection *conn) {
    if (conn->state != CONN_CONNECTED || conn->socket_fd < 0) {
        return -1;
    }
    
    // Check for idle timeout
    time_t now = time(NULL);
    if (now - conn->last_activity > IDLE_TIMEOUT_SEC) {
        printf("Connection idle timeout\n");
        close(conn->socket_fd);
        conn->socket_fd = -1;
        conn->state = CONN_DISCONNECTED;
        return -1;
    }
    
    return 0;
}

// Ensure connection is active
int modbus_ensure_connected(ModbusTCPConnection *conn) {
    if (modbus_check_connection(conn) == 0) {
        return 0;
    }
    
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
    
    conn->state = CONN_DISCONNECTED;
    return modbus_reconnect(conn);
}

// Disconnect
void modbus_disconnect(ModbusTCPConnection *conn) {
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
    conn->state = CONN_DISCONNECTED;
    printf("Disconnected from %s:%d\n", conn->host, conn->port);
}

// Example usage
int main() {
    ModbusTCPConnection conn;
    modbus_connection_init(&conn, "192.168.1.100", MODBUS_TCP_PORT);
    
    // Initial connection
    if (connect_with_timeout(&conn) < 0) {
        printf("Initial connection failed\n");
        return 1;
    }
    
    // Simulate some operations
    for (int i = 0; i < 5; i++) {
        if (modbus_ensure_connected(&conn) < 0) {
            printf("Failed to maintain connection\n");
            break;
        }
        
        printf("Performing operation %d...\n", i + 1);
        conn.last_activity = time(NULL);
        sleep(1);
    }
    
    modbus_disconnect(&conn);
    return 0;
}
```

## C++ Implementation with Connection Pool

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class ModbusTCPConnection {
private:
    int socket_fd_;
    std::string host_;
    int port_;
    bool connected_;
    std::chrono::steady_clock::time_point last_used_;
    
public:
    ModbusTCPConnection(const std::string& host, int port)
        : socket_fd_(-1), host_(host), port_(port), connected_(false) {
        last_used_ = std::chrono::steady_clock::now();
    }
    
    ~ModbusTCPConnection() {
        disconnect();
    }
    
    bool connect() {
        if (connected_) return true;
        
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }
        
        // Set socket options
        int keepalive = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        
        if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        connected_ = true;
        last_used_ = std::chrono::steady_clock::now();
        std::cout << "Connected to " << host_ << ":" << port_ << std::endl;
        return true;
    }
    
    void disconnect() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        connected_ = false;
    }
    
    bool isConnected() const { return connected_; }
    int getSocket() const { return socket_fd_; }
    
    void updateLastUsed() {
        last_used_ = std::chrono::steady_clock::now();
    }
    
    bool isStale(std::chrono::seconds timeout) const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - last_used_) > timeout;
    }
    
    bool healthCheck() {
        if (!connected_) return false;
        
        // Use MSG_PEEK to check socket without consuming data
        char buf;
        int result = recv(socket_fd_, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
        
        if (result == 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            connected_ = false;
            return false;
        }
        
        return true;
    }
};

class ModbusTCPConnectionPool {
private:
    std::string host_;
    int port_;
    size_t max_connections_;
    std::chrono::seconds idle_timeout_;
    
    std::vector<std::unique_ptr<ModbusTCPConnection>> connections_;
    std::queue<ModbusTCPConnection*> available_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_;
    
    std::thread cleanup_thread_;
    
public:
    ModbusTCPConnectionPool(const std::string& host, int port, size_t max_connections = 10)
        : host_(host), port_(port), max_connections_(max_connections),
          idle_timeout_(60), shutdown_(false) {
        
        cleanup_thread_ = std::thread(&ModbusTCPConnectionPool::cleanupLoop, this);
    }
    
    ~ModbusTCPConnectionPool() {
        shutdown_ = true;
        cv_.notify_all();
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }
    
    std::shared_ptr<ModbusTCPConnection> acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto deadline = std::chrono::steady_clock::now() + timeout;
        
        while (true) {
            // Try to get an available connection
            if (!available_.empty()) {
                ModbusTCPConnection* conn = available_.front();
                available_.pop();
                
                // Verify connection is healthy
                if (conn->healthCheck()) {
                    conn->updateLastUsed();
                    return std::shared_ptr<ModbusTCPConnection>(conn, [this](ModbusTCPConnection* c) {
                        this->release(c);
                    });
                } else {
                    // Connection is dead, reconnect
                    conn->disconnect();
                    if (conn->connect()) {
                        conn->updateLastUsed();
                        return std::shared_ptr<ModbusTCPConnection>(conn, [this](ModbusTCPConnection* c) {
                            this->release(c);
                        });
                    }
                    // If reconnect fails, connection will be cleaned up later
                }
            }
            
            // Try to create a new connection
            if (connections_.size() < max_connections_) {
                auto conn = std::make_unique<ModbusTCPConnection>(host_, port_);
                if (conn->connect()) {
                    ModbusTCPConnection* raw_ptr = conn.get();
                    connections_.push_back(std::move(conn));
                    raw_ptr->updateLastUsed();
                    return std::shared_ptr<ModbusTCPConnection>(raw_ptr, [this](ModbusTCPConnection* c) {
                        this->release(c);
                    });
                }
            }
            
            // Wait for a connection to become available
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                throw std::runtime_error("Connection pool timeout");
            }
        }
    }
    
    void release(ModbusTCPConnection* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(conn);
        cv_.notify_one();
    }
    
    size_t getConnectionCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connections_.size();
    }
    
    size_t getAvailableCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }
    
private:
    void cleanupLoop() {
        while (!shutdown_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Remove stale connections from available queue
            std::queue<ModbusTCPConnection*> fresh;
            while (!available_.empty()) {
                ModbusTCPConnection* conn = available_.front();
                available_.pop();
                
                if (!conn->isStale(idle_timeout_) && conn->healthCheck()) {
                    fresh.push(conn);
                } else {
                    conn->disconnect();
                }
            }
            available_ = std::move(fresh);
        }
    }
};

// Example usage
int main() {
    try {
        ModbusTCPConnectionPool pool("192.168.1.100", 502, 5);
        
        std::cout << "Testing connection pool..." << std::endl;
        
        // Simulate multiple concurrent requests
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&pool, i]() {
                try {
                    auto conn = pool.acquire();
                    std::cout << "Thread " << i << " acquired connection" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    std::cout << "Thread " << i << " releasing connection" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << i << " error: " << e.what() << std::endl;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "Pool stats - Total: " << pool.getConnectionCount() 
                  << ", Available: " << pool.getAvailableCount() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

const MODBUS_TCP_PORT: u16 = 502;
const MAX_RETRIES: u32 = 3;
const INITIAL_BACKOFF: Duration = Duration::from_millis(100);
const MAX_BACKOFF: Duration = Duration::from_secs(5);
const CONNECTION_TIMEOUT: Duration = Duration::from_secs(5);
const IDLE_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
}

pub struct ModbusTCPConnection {
    stream: Option<TcpStream>,
    host: String,
    port: u16,
    state: ConnectionState,
    last_activity: Instant,
    retry_count: u32,
    backoff: Duration,
}

impl ModbusTCPConnection {
    pub fn new(host: &str, port: Option<u16>) -> Self {
        ModbusTCPConnection {
            stream: None,
            host: host.to_string(),
            port: port.unwrap_or(MODBUS_TCP_PORT),
            state: ConnectionState::Disconnected,
            last_activity: Instant::now(),
            retry_count: 0,
            backoff: INITIAL_BACKOFF,
        }
    }
    
    pub fn connect(&mut self) -> io::Result<()> {
        self.state = ConnectionState::Connecting;
        
        let addr = format!("{}:{}", self.host, self.port)
            .to_socket_addrs()?
            .next()
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "Invalid address"))?;
        
        let stream = TcpStream::connect_timeout(&addr, CONNECTION_TIMEOUT)?;
        
        // Configure socket options
        stream.set_read_timeout(Some(CONNECTION_TIMEOUT))?;
        stream.set_write_timeout(Some(CONNECTION_TIMEOUT))?;
        stream.set_nodelay(true)?; // Disable Nagle's algorithm for low latency
        
        self.stream = Some(stream);
        self.state = ConnectionState::Connected;
        self.last_activity = Instant::now();
        self.retry_count = 0;
        self.backoff = INITIAL_BACKOFF;
        
        println!("Connected to {}:{}", self.host, self.port);
        Ok(())
    }
    
    pub fn reconnect(&mut self) -> io::Result<()> {
        if self.retry_count >= MAX_RETRIES {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "Max retries reached"
            ));
        }
        
        println!(
            "Reconnecting (attempt {}/{}) after {:?}...",
            self.retry_count + 1,
            MAX_RETRIES,
            self.backoff
        );
        
        std::thread::sleep(self.backoff);
        
        match self.connect() {
            Ok(_) => Ok(()),
            Err(e) => {
                self.retry_count += 1;
                self.backoff = std::cmp::min(self.backoff * 2, MAX_BACKOFF);
                Err(e)
            }
        }
    }
    
    pub fn disconnect(&mut self) {
        if let Some(stream) = self.stream.take() {
            drop(stream);
            println!("Disconnected from {}:{}", self.host, self.port);
        }
        self.state = ConnectionState::Disconnected;
    }
    
    pub fn is_connected(&self) -> bool {
        self.state == ConnectionState::Connected && self.stream.is_some()
    }
    
    pub fn check_connection(&mut self) -> bool {
        if !self.is_connected() {
            return false;
        }
        
        // Check idle timeout
        if self.last_activity.elapsed() > IDLE_TIMEOUT {
            println!("Connection idle timeout");
            self.disconnect();
            return false;
        }
        
        // Try to peek at the socket to verify it's still alive
        if let Some(ref stream) = self.stream {
            if stream.peek(&mut [0u8; 1]).is_err() {
                println!("Connection health check failed");
                self.disconnect();
                return false;
            }
        }
        
        true
    }
    
    pub fn ensure_connected(&mut self) -> io::Result<()> {
        if self.check_connection() {
            return Ok(());
        }
        
        self.disconnect();
        self.reconnect()
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<usize> {
        self.ensure_connected()?;
        
        if let Some(ref mut stream) = self.stream {
            let result = stream.write(data)?;
            self.last_activity = Instant::now();
            Ok(result)
        } else {
            Err(io::Error::new(io::ErrorKind::NotConnected, "Not connected"))
        }
    }
    
    pub fn receive(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.ensure_connected()?;
        
        if let Some(ref mut stream) = self.stream {
            let result = stream.read(buffer)?;
            self.last_activity = Instant::now();
            Ok(result)
        } else {
            Err(io::Error::new(io::ErrorKind::NotConnected, "Not connected"))
        }
    }
}

// Connection Pool Implementation
pub struct ModbusTCPConnectionPool {
    host: String,
    port: u16,
    max_connections: usize,
    available: Arc<Mutex<VecDeque<ModbusTCPConnection>>>,
    in_use: Arc<Mutex<usize>>,
}

impl ModbusTCPConnectionPool {
    pub fn new(host: &str, port: Option<u16>, max_connections: usize) -> Self {
        ModbusTCPConnectionPool {
            host: host.to_string(),
            port: port.unwrap_or(MODBUS_TCP_PORT),
            max_connections,
            available: Arc::new(Mutex::new(VecDeque::new())),
            in_use: Arc::new(Mutex::new(0)),
        }
    }
    
    pub fn acquire(&self, timeout: Duration) -> io::Result<PooledConnection> {
        let start = Instant::now();
        
        loop {
            // Try to get an available connection
            {
                let mut available = self.available.lock().unwrap();
                if let Some(mut conn) = available.pop_front() {
                    if conn.check_connection() {
                        *self.in_use.lock().unwrap() += 1;
                        return Ok(PooledConnection::new(conn, self.clone_refs()));
                    }
                    // Connection is stale, try to reconnect
                    if conn.reconnect().is_ok() {
                        *self.in_use.lock().unwrap() += 1;
                        return Ok(PooledConnection::new(conn, self.clone_refs()));
                    }
                }
            }
            
            // Try to create a new connection
            {
                let in_use = *self.in_use.lock().unwrap();
                let available_count = self.available.lock().unwrap().len();
                
                if in_use + available_count < self.max_connections {
                    let mut conn = ModbusTCPConnection::new(&self.host, Some(self.port));
                    if conn.connect().is_ok() {
                        *self.in_use.lock().unwrap() += 1;
                        return Ok(PooledConnection::new(conn, self.clone_refs()));
                    }
                }
            }
            
            // Check timeout
            if start.elapsed() > timeout {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "Connection pool timeout"
                ));
            }
            
            // Wait a bit before retrying
            std::thread::sleep(Duration::from_millis(50));
        }
    }
    
    fn release(&self, conn: ModbusTCPConnection) {
        let mut available = self.available.lock().unwrap();
        available.push_back(conn);
        *self.in_use.lock().unwrap() -= 1;
    }
    
    fn clone_refs(&self) -> PoolRefs {
        PoolRefs {
            available: Arc::clone(&self.available),
            in_use: Arc::clone(&self.in_use),
        }
    }
    
    pub fn stats(&self) -> (usize, usize) {
        let available = self.available.lock().unwrap().len();
        let in_use = *self.in_use.lock().unwrap();
        (available, in_use)
    }
}

struct PoolRefs {
    available: Arc<Mutex<VecDeque<ModbusTCPConnection>>>,
    in_use: Arc<Mutex<usize>>,
}

pub struct PooledConnection {
    conn: Option<ModbusTCPConnection>,
    pool_refs: PoolRefs,
}

impl PooledConnection {
    fn new(conn: ModbusTCPConnection, pool_refs: PoolRefs) -> Self {
        PooledConnection {
            conn: Some(conn),
            pool_refs,
        }
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<usize> {
        self.conn.as_mut().unwrap().send(data)
    }
    
    pub fn receive(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.conn.as_mut().unwrap().receive(buffer)
    }
}

impl Drop for PooledConnection {
    fn drop(&mut self) {
        if let Some(conn) = self.conn.take() {
            let mut available = self.pool_refs.available.lock().unwrap();
            available.push_back(conn);
            *self.pool_refs.in_use.lock().unwrap() -= 1;
        }
    }
}

// Example usage
fn main() -> io::Result<()> {
    println!("=== Simple Connection Example ===");
    let mut conn = ModbusTCPConnection::new("192.168.1.100", None);
    
    match conn.connect() {
        Ok(_) => {
            println!("Connection successful!");
            
            // Simulate some operations
            for i in 0..5 {
                if conn.ensure_connected().is_ok() {
                    println!("Performing operation {}...", i + 1);
                    std::thread::sleep(Duration::from_secs(1));
                }
            }
            
            conn.disconnect();
        }
        Err(e) => println!("Connection failed: {}", e),
    }
    
    println!("\n=== Connection Pool Example ===");
    let pool = Arc::new(ModbusTCPConnectionPool::new("192.168.1.100", None, 5));
    
    let mut handles = vec![];
    
    for i in 0..10 {
        let pool_clone = Arc::clone(&pool);
        let handle = std::thread::spawn(move || {
            match pool_clone.acquire(Duration::from_secs(5)) {
                Ok(mut conn) => {
                    println!("Thread {} acquired connection", i);
                    std::thread::sleep(Duration::from_millis(100));
                    println!("Thread {} releasing connection", i);
                }
                Err(e) => println!("Thread {} error: {}", i, e),
            }
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.join().unwrap();
    }
    
    let (available, in_use) = pool.stats();
    println!("Pool stats - Available: {}, In use: {}", available, in_use);
    
    Ok(())
}
```

## Summary

TCP Connection Management in Modbus is essential for building reliable and efficient industrial communication systems. The implementations above demonstrate three key aspects:

### Key Takeaways

**Persistent Connections**: Rather than establishing a new TCP connection for each Modbus request, maintaining persistent connections dramatically reduces latency and overhead. The examples show how to keep connections alive using TCP keepalive mechanisms and proper timeout handling.

**Robust Reconnection Logic**: Network failures are inevitable in industrial environments. Implementing exponential backoff prevents overwhelming servers during outages, while retry limits ensure systems don't get stuck in infinite reconnection loops. The C implementation demonstrates basic reconnection with configurable backoff intervals.

**Connection Pooling**: For applications communicating with multiple devices or handling high request volumes, connection pools provide efficient resource management. The C++ and Rust examples show how to maintain a pool of reusable connections, automatically handling connection lifecycle, health checking, and fair distribution across concurrent requests.

**Health Monitoring**: Regular health checks detect stale or broken connections before they cause transaction failures. The implementations use techniques like socket peeking, idle timeouts, and connection validation to ensure only healthy connections are used for Modbus communication.

These patterns enable Modbus applications to maintain stable, high-performance communication even in challenging industrial network environments with intermittent connectivity, multiple devices, and varying load patterns.