# DDoS Mitigation: SYN Flood Protection, Rate Limiting, and Connection Limits

## Overview

Distributed Denial of Service (DDoS) attacks aim to overwhelm a server's resources, making it unavailable to legitimate users. Three critical mitigation techniques are:

1. **SYN Flood Protection**: Defending against TCP handshake exploitation
2. **Rate Limiting**: Controlling request frequency per client
3. **Connection Limits**: Restricting simultaneous connections

## Understanding the Attacks

### SYN Flood Attack
A SYN flood exploits the TCP three-way handshake. Attackers send numerous SYN packets without completing the handshake, exhausting the server's connection queue with half-open connections.

**Normal TCP Handshake:**
```
Client → Server: SYN
Server → Client: SYN-ACK
Client → Server: ACK
```

**SYN Flood:**
```
Attacker → Server: SYN (spoofed source)
Server → ???: SYN-ACK (goes nowhere)
[Server waits, connection queue fills up]
```

### Rate Limiting
Restricts the number of requests from a single source within a time window, preventing resource exhaustion from excessive requests.

### Connection Limits
Caps the total number of simultaneous connections per IP address or globally, preventing resource depletion.

---

## C Implementation

### SYN Flood Protection with SYN Cookies

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <errno.h>

// Enable SYN cookies at OS level
int enable_syn_cookies(int sockfd) {
    int enable = 1;
    
    // Linux-specific: enable TCP_DEFER_ACCEPT
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_DEFER_ACCEPT, 
                   &enable, sizeof(enable)) < 0) {
        perror("TCP_DEFER_ACCEPT failed");
        return -1;
    }
    
    // Reduce SYN backlog queue size to fail faster
    if (listen(sockfd, 128) < 0) {
        perror("listen failed");
        return -1;
    }
    
    printf("SYN flood protection enabled\n");
    return 0;
}

// Configure kernel parameters (requires root)
void configure_kernel_syncookies() {
    system("echo 1 > /proc/sys/net/ipv4/tcp_syncookies");
    system("echo 1024 > /proc/sys/net/ipv4/tcp_max_syn_backlog");
    system("echo 5 > /proc/sys/net/ipv4/tcp_syn_retries");
    system("echo 5 > /proc/sys/net/ipv4/tcp_synack_retries");
}
```

### Rate Limiting with Token Bucket Algorithm

```c
#include <time.h>
#include <sys/time.h>

#define MAX_CLIENTS 1024
#define TOKENS_PER_SECOND 10
#define MAX_TOKENS 50

typedef struct {
    char ip[INET_ADDRSTRLEN];
    double tokens;
    struct timeval last_update;
    int active;
} RateLimiter;

typedef struct {
    RateLimiter clients[MAX_CLIENTS];
    int count;
} RateLimiterTable;

RateLimiterTable rate_table = {0};

// Initialize rate limiter
void init_rate_limiter(RateLimiter *rl, const char *ip) {
    strncpy(rl->ip, ip, INET_ADDRSTRLEN);
    rl->tokens = MAX_TOKENS;
    gettimeofday(&rl->last_update, NULL);
    rl->active = 1;
}

// Find or create rate limiter for IP
RateLimiter* get_rate_limiter(const char *ip) {
    // Search existing
    for (int i = 0; i < rate_table.count; i++) {
        if (rate_table.clients[i].active && 
            strcmp(rate_table.clients[i].ip, ip) == 0) {
            return &rate_table.clients[i];
        }
    }
    
    // Create new if space available
    if (rate_table.count < MAX_CLIENTS) {
        RateLimiter *rl = &rate_table.clients[rate_table.count++];
        init_rate_limiter(rl, ip);
        return rl;
    }
    
    return NULL;
}

// Check if request is allowed (token bucket)
int is_rate_limited(const char *ip) {
    RateLimiter *rl = get_rate_limiter(ip);
    if (!rl) return 1; // Rate limit if table full
    
    struct timeval now;
    gettimeofday(&now, NULL);
    
    // Calculate elapsed time in seconds
    double elapsed = (now.tv_sec - rl->last_update.tv_sec) +
                     (now.tv_usec - rl->last_update.tv_usec) / 1000000.0;
    
    // Refill tokens
    rl->tokens += elapsed * TOKENS_PER_SECOND;
    if (rl->tokens > MAX_TOKENS) {
        rl->tokens = MAX_TOKENS;
    }
    
    rl->last_update = now;
    
    // Check if request allowed
    if (rl->tokens >= 1.0) {
        rl->tokens -= 1.0;
        return 0; // Not rate limited
    }
    
    return 1; // Rate limited
}
```

### Connection Limiting

```c
#include <pthread.h>

#define MAX_CONNECTIONS_PER_IP 10
#define MAX_TOTAL_CONNECTIONS 1000

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int count;
} ConnectionCounter;

typedef struct {
    ConnectionCounter clients[MAX_CLIENTS];
    int total_connections;
    pthread_mutex_t lock;
} ConnectionLimiter;

ConnectionLimiter conn_limiter = {
    .total_connections = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// Check and increment connection count
int allow_connection(const char *ip) {
    pthread_mutex_lock(&conn_limiter.lock);
    
    // Check total connections
    if (conn_limiter.total_connections >= MAX_TOTAL_CONNECTIONS) {
        pthread_mutex_unlock(&conn_limiter.lock);
        return 0;
    }
    
    // Find IP entry
    ConnectionCounter *cc = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (conn_limiter.clients[i].count > 0 &&
            strcmp(conn_limiter.clients[i].ip, ip) == 0) {
            cc = &conn_limiter.clients[i];
            break;
        }
    }
    
    // Create new entry if needed
    if (!cc) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (conn_limiter.clients[i].count == 0) {
                cc = &conn_limiter.clients[i];
                strncpy(cc->ip, ip, INET_ADDRSTRLEN);
                break;
            }
        }
    }
    
    if (!cc) {
        pthread_mutex_unlock(&conn_limiter.lock);
        return 0; // Table full
    }
    
    // Check per-IP limit
    if (cc->count >= MAX_CONNECTIONS_PER_IP) {
        pthread_mutex_unlock(&conn_limiter.lock);
        return 0;
    }
    
    // Allow connection
    cc->count++;
    conn_limiter.total_connections++;
    pthread_mutex_unlock(&conn_limiter.lock);
    return 1;
}

// Decrement connection count
void release_connection(const char *ip) {
    pthread_mutex_lock(&conn_limiter.lock);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(conn_limiter.clients[i].ip, ip) == 0) {
            conn_limiter.clients[i].count--;
            conn_limiter.total_connections--;
            break;
        }
    }
    
    pthread_mutex_unlock(&conn_limiter.lock);
}
```

### Complete Server Example

```c
void* handle_client(void *arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_sock, (struct sockaddr*)&addr, &addr_len);
    
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
    
    // Check rate limiting
    if (is_rate_limited(ip)) {
        const char *response = "HTTP/1.1 429 Too Many Requests\r\n\r\n";
        send(client_sock, response, strlen(response), 0);
        close(client_sock);
        release_connection(ip);
        return NULL;
    }
    
    // Handle request
    char buffer[1024];
    int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        const char *response = "HTTP/1.1 200 OK\r\n\r\nHello World";
        send(client_sock, response, strlen(response), 0);
    }
    
    close(client_sock);
    release_connection(ip);
    return NULL;
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }
    
    int reuse = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Enable SYN flood protection
    enable_syn_cookies(server_sock);
    
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_sock, 128) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("Server listening on port 8080\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) {
            perror("accept");
            continue;
        }
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
        
        // Check connection limit
        if (!allow_connection(ip)) {
            const char *response = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
            send(client_sock, response, strlen(response), 0);
            close(client_sock);
            continue;
        }
        
        // Handle in thread
        pthread_t thread;
        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_sock;
        pthread_create(&thread, NULL, handle_client, sock_ptr);
        pthread_detach(thread);
    }
    
    close(server_sock);
    return 0;
}
```

---

## C++ Implementation

```cpp
#include <iostream>
#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;
using namespace chrono;

// Token Bucket Rate Limiter
class TokenBucketRateLimiter {
private:
    struct Bucket {
        double tokens;
        steady_clock::time_point last_update;
    };
    
    unordered_map<string, Bucket> buckets;
    mutex mtx;
    double rate;           // tokens per second
    double max_tokens;
    
public:
    TokenBucketRateLimiter(double rate, double max_tokens)
        : rate(rate), max_tokens(max_tokens) {}
    
    bool allow(const string& client_id) {
        lock_guard<mutex> lock(mtx);
        auto now = steady_clock::now();
        
        auto& bucket = buckets[client_id];
        if (bucket.tokens == 0 && bucket.last_update == steady_clock::time_point()) {
            // Initialize new bucket
            bucket.tokens = max_tokens;
            bucket.last_update = now;
        }
        
        // Refill tokens
        auto elapsed = duration_cast<duration<double>>(now - bucket.last_update).count();
        bucket.tokens = min(max_tokens, bucket.tokens + elapsed * rate);
        bucket.last_update = now;
        
        // Check if request allowed
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return true;
        }
        
        return false;
    }
    
    void cleanup_old_entries() {
        lock_guard<mutex> lock(mtx);
        auto now = steady_clock::now();
        
        for (auto it = buckets.begin(); it != buckets.end();) {
            auto elapsed = duration_cast<seconds>(now - it->second.last_update).count();
            if (elapsed > 300) { // Remove after 5 minutes inactive
                it = buckets.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// Connection Limiter
class ConnectionLimiter {
private:
    unordered_map<string, int> connections;
    int total_connections = 0;
    int max_per_ip;
    int max_total;
    mutex mtx;
    
public:
    ConnectionLimiter(int max_per_ip, int max_total)
        : max_per_ip(max_per_ip), max_total(max_total) {}
    
    bool acquire(const string& ip) {
        lock_guard<mutex> lock(mtx);
        
        if (total_connections >= max_total) {
            return false;
        }
        
        if (connections[ip] >= max_per_ip) {
            return false;
        }
        
        connections[ip]++;
        total_connections++;
        return true;
    }
    
    void release(const string& ip) {
        lock_guard<mutex> lock(mtx);
        
        auto it = connections.find(ip);
        if (it != connections.end() && it->second > 0) {
            it->second--;
            total_connections--;
            
            if (it->second == 0) {
                connections.erase(it);
            }
        }
    }
    
    int get_total() const { return total_connections; }
};

// Protected Server
class ProtectedServer {
private:
    int server_fd;
    TokenBucketRateLimiter rate_limiter;
    ConnectionLimiter conn_limiter;
    
public:
    ProtectedServer(int port)
        : rate_limiter(10.0, 50.0),  // 10 req/s, burst of 50
          conn_limiter(10, 1000)      // 10 per IP, 1000 total
    {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw runtime_error("Socket creation failed");
        }
        
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            throw runtime_error("Bind failed");
        }
        
        if (listen(server_fd, 128) < 0) {
            throw runtime_error("Listen failed");
        }
        
        cout << "Protected server listening on port " << port << endl;
    }
    
    ~ProtectedServer() {
        close(server_fd);
    }
    
    void handle_client(int client_fd, const string& client_ip) {
        // Check rate limiting
        if (!rate_limiter.allow(client_ip)) {
            string response = "HTTP/1.1 429 Too Many Requests\r\n\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            conn_limiter.release(client_ip);
            return;
        }
        
        // Handle request
        char buffer[1024];
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes > 0) {
            string response = "HTTP/1.1 200 OK\r\n\r\nProtected Server Response";
            send(client_fd, response.c_str(), response.size(), 0);
        }
        
        close(client_fd);
        conn_limiter.release(client_ip);
    }
    
    void run() {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                continue;
            }
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            string client_ip(ip_str);
            
            // Check connection limit
            if (!conn_limiter.acquire(client_ip)) {
                string response = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
                send(client_fd, response.c_str(), response.size(), 0);
                close(client_fd);
                continue;
            }
            
            // Handle in thread
            thread(&ProtectedServer::handle_client, this, client_fd, client_ip).detach();
        }
    }
};

int main() {
    try {
        ProtectedServer server(8080);
        server.run();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::HashMap;
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use std::io::{Read, Write};

// Token Bucket Rate Limiter
struct TokenBucket {
    tokens: f64,
    last_update: Instant,
}

struct RateLimiter {
    buckets: Arc<Mutex<HashMap<String, TokenBucket>>>,
    rate: f64,        // tokens per second
    max_tokens: f64,
}

impl RateLimiter {
    fn new(rate: f64, max_tokens: f64) -> Self {
        RateLimiter {
            buckets: Arc::new(Mutex::new(HashMap::new())),
            rate,
            max_tokens,
        }
    }
    
    fn allow(&self, client_id: &str) -> bool {
        let mut buckets = self.buckets.lock().unwrap();
        let now = Instant::now();
        
        let bucket = buckets.entry(client_id.to_string()).or_insert(TokenBucket {
            tokens: self.max_tokens,
            last_update: now,
        });
        
        // Refill tokens
        let elapsed = now.duration_since(bucket.last_update).as_secs_f64();
        bucket.tokens = (bucket.tokens + elapsed * self.rate).min(self.max_tokens);
        bucket.last_update = now;
        
        // Check if request allowed
        if bucket.tokens >= 1.0 {
            bucket.tokens -= 1.0;
            true
        } else {
            false
        }
    }
    
    fn cleanup_old(&self) {
        let mut buckets = self.buckets.lock().unwrap();
        let now = Instant::now();
        
        buckets.retain(|_, bucket| {
            now.duration_since(bucket.last_update) < Duration::from_secs(300)
        });
    }
}

// Connection Limiter
struct ConnectionLimiter {
    connections: Arc<Mutex<HashMap<String, usize>>>,
    total: Arc<Mutex<usize>>,
    max_per_ip: usize,
    max_total: usize,
}

impl ConnectionLimiter {
    fn new(max_per_ip: usize, max_total: usize) -> Self {
        ConnectionLimiter {
            connections: Arc::new(Mutex::new(HashMap::new())),
            total: Arc::new(Mutex::new(0)),
            max_per_ip,
            max_total,
        }
    }
    
    fn acquire(&self, ip: &str) -> bool {
        let mut connections = self.connections.lock().unwrap();
        let mut total = self.total.lock().unwrap();
        
        if *total >= self.max_total {
            return false;
        }
        
        let count = connections.entry(ip.to_string()).or_insert(0);
        if *count >= self.max_per_ip {
            return false;
        }
        
        *count += 1;
        *total += 1;
        true
    }
    
    fn release(&self, ip: &str) {
        let mut connections = self.connections.lock().unwrap();
        let mut total = self.total.lock().unwrap();
        
        if let Some(count) = connections.get_mut(ip) {
            if *count > 0 {
                *count -= 1;
                *total -= 1;
                
                if *count == 0 {
                    connections.remove(ip);
                }
            }
        }
    }
}

// Protected Server
struct ProtectedServer {
    rate_limiter: Arc<RateLimiter>,
    conn_limiter: Arc<ConnectionLimiter>,
}

impl ProtectedServer {
    fn new() -> Self {
        ProtectedServer {
            rate_limiter: Arc::new(RateLimiter::new(10.0, 50.0)),
            conn_limiter: Arc::new(ConnectionLimiter::new(10, 1000)),
        }
    }
    
    fn handle_client(&self, mut stream: TcpStream, client_ip: String) {
        // Check rate limiting
        if !self.rate_limiter.allow(&client_ip) {
            let response = b"HTTP/1.1 429 Too Many Requests\r\n\r\n";
            let _ = stream.write_all(response);
            drop(stream);
            self.conn_limiter.release(&client_ip);
            return;
        }
        
        // Handle request
        let mut buffer = [0u8; 1024];
        match stream.read(&mut buffer) {
            Ok(n) if n > 0 => {
                let response = b"HTTP/1.1 200 OK\r\n\r\nProtected Server Response";
                let _ = stream.write_all(response);
            }
            _ => {}
        }
        
        drop(stream);
        self.conn_limiter.release(&client_ip);
    }
    
    fn run(&self, addr: &str) -> std::io::Result<()> {
        let listener = TcpListener::bind(addr)?;
        println!("Protected server listening on {}", addr);
        
        // Cleanup thread
        let rate_limiter_clone = Arc::clone(&self.rate_limiter);
        thread::spawn(move || {
            loop {
                thread::sleep(Duration::from_secs(60));
                rate_limiter_clone.cleanup_old();
            }
        });
        
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    let peer_addr = stream.peer_addr().ok();
                    let client_ip = peer_addr
                        .map(|a| a.ip().to_string())
                        .unwrap_or_else(|| "unknown".to_string());
                    
                    // Check connection limit
                    if !self.conn_limiter.acquire(&client_ip) {
                        let response = b"HTTP/1.1 503 Service Unavailable\r\n\r\n";
                        let mut s = stream;
                        let _ = s.write_all(response);
                        continue;
                    }
                    
                    // Handle in thread
                    let rate_limiter = Arc::clone(&self.rate_limiter);
                    let conn_limiter = Arc::clone(&self.conn_limiter);
                    
                    thread::spawn(move || {
                        let server = ProtectedServer {
                            rate_limiter,
                            conn_limiter,
                        };
                        server.handle_client(stream, client_ip);
                    });
                }
                Err(e) => eprintln!("Connection error: {}", e),
            }
        }
        
        Ok(())
    }
}

fn main() -> std::io::Result<()> {
    let server = ProtectedServer::new();
    server.run("127.0.0.1:8080")
}
```

---

## Summary

**DDoS mitigation** requires layered defense mechanisms:

1. **SYN Flood Protection**:
   - Enable SYN cookies at the OS level
   - Reduce SYN backlog queue size
   - Use TCP_DEFER_ACCEPT to defer accept() until data arrives
   - Configure aggressive timeout values

2. **Rate Limiting**:
   - Token bucket algorithm refills tokens at a fixed rate
   - Allows bursts while preventing sustained abuse
   - Track per-client request rates using IP addresses
   - Clean up old entries periodically

3. **Connection Limits**:
   - Enforce per-IP maximum connections
   - Set global connection caps
   - Use thread-safe data structures for tracking
   - Release connections promptly after handling

**Key Implementation Considerations**:
- Use mutexes/locks for thread-safe state management
- Implement cleanup routines for stale entries
- Return appropriate HTTP status codes (429, 503)
- Balance security with legitimate traffic needs
- Consider combining with firewall rules and reverse proxies
- Monitor metrics to tune thresholds

These techniques form the foundation of application-level DDoS defense, complementing network-level protections like firewalls, load balancers, and CDNs.