# Resource Exhaustion Protection in WebSocket Programming

## Overview

Resource exhaustion protection is a critical aspect of WebSocket server development that prevents attackers or misbehaving clients from consuming all available system resources. Without proper safeguards, a WebSocket server can experience memory leaks, file descriptor exhaustion, CPU starvation, or bandwidth saturation, leading to service degradation or complete failure.

## Key Resource Exhaustion Vectors

### 1. **Memory Exhaustion**
- Unbounded message buffers
- Connection state accumulation
- Memory leaks from improper cleanup

### 2. **File Descriptor Exhaustion**
- Too many open connections
- Socket leaks from unclosed connections
- Operating system limits on file descriptors

### 3. **CPU Exhaustion**
- Complex message processing
- Inefficient parsing algorithms
- Lack of rate limiting

### 4. **Bandwidth Exhaustion**
- Large message floods
- Uncontrolled broadcast operations

## Protection Strategies

### Connection Limits
Restrict the maximum number of concurrent connections per client and globally.

### Message Size Limits
Enforce maximum frame and message sizes to prevent memory exhaustion.

### Rate Limiting
Control the number of messages or bytes per time window.

### Timeout Enforcement
Close idle or slow connections to free resources.

### Resource Tracking
Monitor and limit per-connection resource usage.

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#define MAX_CONNECTIONS 1000
#define MAX_CONNECTIONS_PER_IP 10
#define MAX_MESSAGE_SIZE (1 * 1024 * 1024)  // 1MB
#define MAX_FRAME_SIZE (64 * 1024)           // 64KB
#define RATE_LIMIT_MESSAGES 100              // messages per window
#define RATE_LIMIT_WINDOW 60                 // seconds
#define IDLE_TIMEOUT 300                     // 5 minutes
#define HANDSHAKE_TIMEOUT 10                 // 10 seconds

typedef struct {
    int fd;
    struct sockaddr_in addr;
    time_t last_activity;
    time_t connected_at;
    
    // Rate limiting
    int message_count;
    time_t rate_window_start;
    size_t bytes_received;
    size_t bytes_sent;
    
    // Message buffer
    char* buffer;
    size_t buffer_size;
    size_t buffer_used;
    
    int is_handshake_complete;
} websocket_connection_t;

typedef struct {
    websocket_connection_t connections[MAX_CONNECTIONS];
    int connection_count;
    int fd_limit;
    size_t total_memory_used;
    size_t memory_limit;
} resource_manager_t;

// Initialize resource manager with system limits
int init_resource_manager(resource_manager_t* mgr) {
    struct rlimit rl;
    
    memset(mgr, 0, sizeof(resource_manager_t));
    
    // Get file descriptor limit
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        mgr->fd_limit = rl.rlim_cur;
        printf("[INFO] File descriptor limit: %d\n", mgr->fd_limit);
    } else {
        mgr->fd_limit = 1024; // default fallback
    }
    
    // Set memory limit (e.g., 100MB for connection buffers)
    mgr->memory_limit = 100 * 1024 * 1024;
    
    return 0;
}

// Check if we can accept a new connection
int can_accept_connection(resource_manager_t* mgr, struct sockaddr_in* addr) {
    // Check global connection limit
    if (mgr->connection_count >= MAX_CONNECTIONS) {
        printf("[WARN] Global connection limit reached\n");
        return 0;
    }
    
    // Check file descriptor limit (leave some headroom)
    if (mgr->connection_count >= mgr->fd_limit - 50) {
        printf("[WARN] Approaching file descriptor limit\n");
        return 0;
    }
    
    // Check per-IP connection limit
    int ip_connections = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (mgr->connections[i].fd > 0 &&
            mgr->connections[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr) {
            ip_connections++;
        }
    }
    
    if (ip_connections >= MAX_CONNECTIONS_PER_IP) {
        printf("[WARN] Per-IP connection limit reached for %s\n",
               inet_ntoa(addr->sin_addr));
        return 0;
    }
    
    return 1;
}

// Allocate buffer with memory limit check
char* allocate_buffer(resource_manager_t* mgr, size_t size) {
    if (mgr->total_memory_used + size > mgr->memory_limit) {
        printf("[ERROR] Memory limit would be exceeded\n");
        return NULL;
    }
    
    char* buffer = malloc(size);
    if (buffer) {
        mgr->total_memory_used += size;
    }
    
    return buffer;
}

// Free buffer and update memory tracking
void free_buffer(resource_manager_t* mgr, char* buffer, size_t size) {
    if (buffer) {
        free(buffer);
        mgr->total_memory_used -= size;
    }
}

// Add a new connection with resource checks
websocket_connection_t* add_connection(resource_manager_t* mgr, 
                                       int fd, 
                                       struct sockaddr_in* addr) {
    if (!can_accept_connection(mgr, addr)) {
        return NULL;
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (mgr->connections[i].fd == 0) {
            websocket_connection_t* conn = &mgr->connections[i];
            
            conn->fd = fd;
            conn->addr = *addr;
            conn->connected_at = time(NULL);
            conn->last_activity = conn->connected_at;
            conn->is_handshake_complete = 0;
            
            // Allocate initial buffer
            conn->buffer_size = 4096;
            conn->buffer = allocate_buffer(mgr, conn->buffer_size);
            if (!conn->buffer) {
                memset(conn, 0, sizeof(websocket_connection_t));
                return NULL;
            }
            
            conn->buffer_used = 0;
            conn->message_count = 0;
            conn->rate_window_start = time(NULL);
            
            mgr->connection_count++;
            printf("[INFO] Connection added: %d total connections\n", 
                   mgr->connection_count);
            
            return conn;
        }
    }
    
    return NULL;
}

// Check rate limit for a connection
int check_rate_limit(websocket_connection_t* conn) {
    time_t now = time(NULL);
    
    // Reset counter if window expired
    if (now - conn->rate_window_start >= RATE_LIMIT_WINDOW) {
        conn->message_count = 0;
        conn->rate_window_start = now;
    }
    
    // Check if limit exceeded
    if (conn->message_count >= RATE_LIMIT_MESSAGES) {
        printf("[WARN] Rate limit exceeded for connection\n");
        return 0;
    }
    
    conn->message_count++;
    return 1;
}

// Process incoming data with size limits
int process_websocket_data(resource_manager_t* mgr, 
                          websocket_connection_t* conn,
                          const char* data, 
                          size_t len) {
    // Update activity timestamp
    conn->last_activity = time(NULL);
    
    // Check rate limit
    if (!check_rate_limit(conn)) {
        return -1;
    }
    
    // Check if data would exceed max message size
    if (conn->buffer_used + len > MAX_MESSAGE_SIZE) {
        printf("[ERROR] Message size limit exceeded\n");
        return -1;
    }
    
    // Resize buffer if needed
    if (conn->buffer_used + len > conn->buffer_size) {
        size_t new_size = conn->buffer_size * 2;
        if (new_size > MAX_MESSAGE_SIZE) {
            new_size = MAX_MESSAGE_SIZE;
        }
        
        char* new_buffer = allocate_buffer(mgr, new_size);
        if (!new_buffer) {
            return -1;
        }
        
        memcpy(new_buffer, conn->buffer, conn->buffer_used);
        free_buffer(mgr, conn->buffer, conn->buffer_size);
        
        conn->buffer = new_buffer;
        conn->buffer_size = new_size;
    }
    
    // Append data
    memcpy(conn->buffer + conn->buffer_used, data, len);
    conn->buffer_used += len;
    conn->bytes_received += len;
    
    return 0;
}

// Clean up connection and free resources
void close_connection(resource_manager_t* mgr, websocket_connection_t* conn) {
    if (conn->fd > 0) {
        close(conn->fd);
        
        if (conn->buffer) {
            free_buffer(mgr, conn->buffer, conn->buffer_size);
        }
        
        printf("[INFO] Connection closed. Bytes received: %zu, sent: %zu\n",
               conn->bytes_received, conn->bytes_sent);
        
        memset(conn, 0, sizeof(websocket_connection_t));
        mgr->connection_count--;
    }
}

// Check for idle and timed-out connections
void cleanup_idle_connections(resource_manager_t* mgr) {
    time_t now = time(NULL);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        websocket_connection_t* conn = &mgr->connections[i];
        
        if (conn->fd > 0) {
            // Check handshake timeout
            if (!conn->is_handshake_complete &&
                now - conn->connected_at > HANDSHAKE_TIMEOUT) {
                printf("[WARN] Handshake timeout\n");
                close_connection(mgr, conn);
                continue;
            }
            
            // Check idle timeout
            if (now - conn->last_activity > IDLE_TIMEOUT) {
                printf("[WARN] Idle timeout\n");
                close_connection(mgr, conn);
            }
        }
    }
}

// Example usage
int main() {
    resource_manager_t mgr;
    init_resource_manager(&mgr);
    
    printf("Resource Manager initialized:\n");
    printf("- Max connections: %d\n", MAX_CONNECTIONS);
    printf("- Max message size: %d bytes\n", MAX_MESSAGE_SIZE);
    printf("- Rate limit: %d messages/%d seconds\n", 
           RATE_LIMIT_MESSAGES, RATE_LIMIT_WINDOW);
    printf("- Memory limit: %zu MB\n", mgr.memory_limit / (1024 * 1024));
    
    // Simulate adding connections
    struct sockaddr_in addr = {0};
    addr.sin_addr.s_addr = inet_addr("192.168.1.100");
    
    for (int i = 0; i < 5; i++) {
        websocket_connection_t* conn = add_connection(&mgr, i + 10, &addr);
        if (conn) {
            printf("Added connection %d\n", i);
        }
    }
    
    printf("\nFinal stats:\n");
    printf("- Active connections: %d\n", mgr.connection_count);
    printf("- Memory used: %zu KB\n", mgr.total_memory_used / 1024);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

const MAX_CONNECTIONS: usize = 1000;
const MAX_CONNECTIONS_PER_IP: usize = 10;
const MAX_MESSAGE_SIZE: usize = 1 * 1024 * 1024; // 1MB
const MAX_FRAME_SIZE: usize = 64 * 1024; // 64KB
const RATE_LIMIT_MESSAGES: u32 = 100;
const RATE_LIMIT_WINDOW: Duration = Duration::from_secs(60);
const IDLE_TIMEOUT: Duration = Duration::from_secs(300);
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);
const MEMORY_LIMIT: usize = 100 * 1024 * 1024; // 100MB

#[derive(Debug)]
struct RateLimiter {
    message_count: u32,
    window_start: Instant,
}

impl RateLimiter {
    fn new() -> Self {
        Self {
            message_count: 0,
            window_start: Instant::now(),
        }
    }

    fn check_and_increment(&mut self) -> bool {
        let now = Instant::now();

        // Reset if window expired
        if now.duration_since(self.window_start) >= RATE_LIMIT_WINDOW {
            self.message_count = 0;
            self.window_start = now;
        }

        // Check limit
        if self.message_count >= RATE_LIMIT_MESSAGES {
            eprintln!("[WARN] Rate limit exceeded");
            return false;
        }

        self.message_count += 1;
        true
    }
}

#[derive(Debug)]
struct ConnectionStats {
    bytes_received: usize,
    bytes_sent: usize,
    messages_received: u64,
    messages_sent: u64,
}

impl ConnectionStats {
    fn new() -> Self {
        Self {
            bytes_received: 0,
            bytes_sent: 0,
            messages_received: 0,
            messages_sent: 0,
        }
    }
}

#[derive(Debug)]
struct WebSocketConnection {
    addr: SocketAddr,
    connected_at: Instant,
    last_activity: Instant,
    is_handshake_complete: bool,
    
    // Buffers
    receive_buffer: Vec<u8>,
    
    // Rate limiting
    rate_limiter: RateLimiter,
    
    // Statistics
    stats: ConnectionStats,
}

impl WebSocketConnection {
    fn new(addr: SocketAddr) -> Self {
        let now = Instant::now();
        
        Self {
            addr,
            connected_at: now,
            last_activity: now,
            is_handshake_complete: false,
            receive_buffer: Vec::with_capacity(4096),
            rate_limiter: RateLimiter::new(),
            stats: ConnectionStats::new(),
        }
    }

    fn process_data(&mut self, data: &[u8]) -> Result<(), String> {
        // Update activity
        self.last_activity = Instant::now();

        // Check rate limit
        if !self.rate_limiter.check_and_increment() {
            return Err("Rate limit exceeded".to_string());
        }

        // Check message size limit
        if self.receive_buffer.len() + data.len() > MAX_MESSAGE_SIZE {
            return Err(format!(
                "Message size limit exceeded: {} bytes",
                self.receive_buffer.len() + data.len()
            ));
        }

        // Append to buffer
        self.receive_buffer.extend_from_slice(data);
        self.stats.bytes_received += data.len();

        Ok(())
    }

    fn is_idle(&self, timeout: Duration) -> bool {
        self.last_activity.elapsed() > timeout
    }

    fn is_handshake_timed_out(&self) -> bool {
        !self.is_handshake_complete 
            && self.connected_at.elapsed() > HANDSHAKE_TIMEOUT
    }

    fn clear_buffer(&mut self) {
        self.receive_buffer.clear();
        self.receive_buffer.shrink_to(4096);
    }
}

#[derive(Debug)]
struct ResourceManager {
    connections: HashMap<usize, WebSocketConnection>,
    next_id: usize,
    total_memory_used: usize,
    memory_limit: usize,
}

impl ResourceManager {
    fn new() -> Self {
        Self {
            connections: HashMap::new(),
            next_id: 0,
            total_memory_used: 0,
            memory_limit: MEMORY_LIMIT,
        }
    }

    fn can_accept_connection(&self, addr: &SocketAddr) -> Result<(), String> {
        // Check global limit
        if self.connections.len() >= MAX_CONNECTIONS {
            return Err("Global connection limit reached".to_string());
        }

        // Check per-IP limit
        let ip = addr.ip();
        let ip_count = self.connections
            .values()
            .filter(|c| c.addr.ip() == ip)
            .count();

        if ip_count >= MAX_CONNECTIONS_PER_IP {
            return Err(format!(
                "Per-IP connection limit reached for {}",
                ip
            ));
        }

        // Check memory limit
        let estimated_memory = 4096; // Initial buffer size
        if self.total_memory_used + estimated_memory > self.memory_limit {
            return Err("Memory limit would be exceeded".to_string());
        }

        Ok(())
    }

    fn add_connection(&mut self, addr: SocketAddr) -> Result<usize, String> {
        self.can_accept_connection(&addr)?;

        let id = self.next_id;
        self.next_id += 1;

        let conn = WebSocketConnection::new(addr);
        self.total_memory_used += conn.receive_buffer.capacity();

        self.connections.insert(id, conn);

        println!(
            "[INFO] Connection {} added from {}. Total: {}",
            id,
            addr,
            self.connections.len()
        );

        Ok(id)
    }

    fn remove_connection(&mut self, id: usize) {
        if let Some(conn) = self.connections.remove(&id) {
            self.total_memory_used = self.total_memory_used
                .saturating_sub(conn.receive_buffer.capacity());

            println!(
                "[INFO] Connection {} closed. Stats: RX={} bytes, TX={} bytes",
                id,
                conn.stats.bytes_received,
                conn.stats.bytes_sent
            );
        }
    }

    fn process_connection_data(
        &mut self,
        id: usize,
        data: &[u8],
    ) -> Result<(), String> {
        let conn = self.connections
            .get_mut(&id)
            .ok_or("Connection not found")?;

        // Store old capacity
        let old_capacity = conn.receive_buffer.capacity();

        // Process data
        conn.process_data(data)?;

        // Update memory tracking if buffer grew
        let new_capacity = conn.receive_buffer.capacity();
        if new_capacity > old_capacity {
            let increase = new_capacity - old_capacity;
            if self.total_memory_used + increase > self.memory_limit {
                conn.receive_buffer.truncate(old_capacity);
                return Err("Memory limit would be exceeded".to_string());
            }
            self.total_memory_used += increase;
        }

        Ok(())
    }

    fn cleanup_idle_connections(&mut self) {
        let mut to_remove = Vec::new();

        for (&id, conn) in &self.connections {
            if conn.is_handshake_timed_out() {
                println!("[WARN] Connection {} handshake timeout", id);
                to_remove.push(id);
            } else if conn.is_idle(IDLE_TIMEOUT) {
                println!("[WARN] Connection {} idle timeout", id);
                to_remove.push(id);
            }
        }

        for id in to_remove {
            self.remove_connection(id);
        }
    }

    fn get_stats(&self) -> ResourceStats {
        ResourceStats {
            active_connections: self.connections.len(),
            memory_used: self.total_memory_used,
            memory_limit: self.memory_limit,
        }
    }
}

#[derive(Debug)]
struct ResourceStats {
    active_connections: usize,
    memory_used: usize,
    memory_limit: usize,
}

// Thread-safe wrapper
type SharedResourceManager = Arc<Mutex<ResourceManager>>;

fn create_resource_manager() -> SharedResourceManager {
    Arc::new(Mutex::new(ResourceManager::new()))
}

// Example usage
fn main() {
    let manager = create_resource_manager();

    println!("Resource Manager initialized:");
    println!("- Max connections: {}", MAX_CONNECTIONS);
    println!("- Max message size: {} bytes", MAX_MESSAGE_SIZE);
    println!("- Rate limit: {} messages per {:?}", 
             RATE_LIMIT_MESSAGES, RATE_LIMIT_WINDOW);
    println!("- Memory limit: {} MB", MEMORY_LIMIT / (1024 * 1024));
    println!();

    // Simulate adding connections
    let addr: SocketAddr = "192.168.1.100:8080".parse().unwrap();

    {
        let mut mgr = manager.lock().unwrap();

        for i in 0..5 {
            match mgr.add_connection(addr) {
                Ok(id) => {
                    println!("Added connection {}", id);
                    
                    // Simulate processing data
                    let data = b"Hello, WebSocket!";
                    if let Err(e) = mgr.process_connection_data(id, data) {
                        eprintln!("Error processing data: {}", e);
                    }
                }
                Err(e) => eprintln!("Failed to add connection: {}", e),
            }
        }

        println!();
        let stats = mgr.get_stats();
        println!("Final stats:");
        println!("- Active connections: {}", stats.active_connections);
        println!("- Memory used: {} KB", stats.memory_used / 1024);
        println!(
            "- Memory usage: {:.2}%",
            (stats.memory_used as f64 / stats.memory_limit as f64) * 100.0
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_connection_limits() {
        let mut mgr = ResourceManager::new();
        let addr: SocketAddr = "192.168.1.100:8080".parse().unwrap();

        // Should succeed
        for i in 0..MAX_CONNECTIONS_PER_IP {
            assert!(mgr.add_connection(addr).is_ok());
        }

        // Should fail (per-IP limit)
        assert!(mgr.add_connection(addr).is_err());
    }

    #[test]
    fn test_rate_limiting() {
        let mut limiter = RateLimiter::new();

        // Should succeed up to limit
        for _ in 0..RATE_LIMIT_MESSAGES {
            assert!(limiter.check_and_increment());
        }

        // Should fail (limit exceeded)
        assert!(!limiter.check_and_increment());
    }

    #[test]
    fn test_message_size_limit() {
        let mut conn = WebSocketConnection::new(
            "192.168.1.100:8080".parse().unwrap()
        );

        let large_data = vec![0u8; MAX_MESSAGE_SIZE + 1];
        assert!(conn.process_data(&large_data).is_err());
    }
}
```

---

## Summary

**Resource exhaustion protection** is essential for building robust WebSocket servers that can withstand both malicious attacks and legitimate traffic spikes. The key protection mechanisms include:

1. **Connection Limits**: Enforce both global and per-IP connection limits to prevent connection flooding attacks and ensure fair resource distribution.

2. **Memory Management**: Track buffer allocations, enforce message size limits, and implement memory budgets to prevent unbounded memory growth and potential out-of-memory conditions.

3. **Rate Limiting**: Control the number of messages or bytes per time window to prevent abuse and ensure service quality for all clients.

4. **Timeout Enforcement**: Implement handshake timeouts and idle timeouts to reclaim resources from stalled or abandoned connections.

5. **Resource Tracking**: Monitor file descriptors, memory usage, and connection statistics to detect resource leaks and enable informed operational decisions.

The C/C++ implementation demonstrates low-level resource management with explicit memory tracking, while the Rust implementation leverages RAII and type safety for safer resource handling. Both approaches emphasize defense in depth—combining multiple protective layers to create resilient WebSocket servers capable of operating reliably under adverse conditions.