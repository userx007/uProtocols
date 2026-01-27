# DDoS Mitigation for WebSocket Services

## Overview

DDoS (Distributed Denial of Service) mitigation for WebSocket services involves implementing defensive strategies to protect real-time communication systems from coordinated attacks that attempt to overwhelm servers with illegitimate traffic. WebSockets are particularly vulnerable because they maintain persistent connections, making resource exhaustion attacks more effective than against traditional HTTP services.

## Key Concepts

### Attack Vectors
- **Connection exhaustion**: Opening thousands of connections to deplete server resources
- **Slowloris attacks**: Keeping connections alive indefinitely with minimal traffic
- **Message flooding**: Sending high volumes of messages to consume CPU and bandwidth
- **Protocol abuse**: Exploiting WebSocket handshake or framing mechanisms
- **Application-layer attacks**: Targeting business logic with valid but expensive operations

### Defense Strategies
- **Rate limiting**: Restricting connection attempts and message rates per IP/user
- **Connection limits**: Capping total and per-client concurrent connections
- **Handshake validation**: Verifying Origin headers and authentication tokens
- **Traffic shaping**: Prioritizing legitimate traffic patterns
- **Geo-blocking**: Filtering traffic from suspicious regions
- **IP reputation**: Leveraging threat intelligence databases
- **Resource quotas**: Limiting memory and CPU per connection

## C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 1000
#define MAX_CONN_PER_IP 5
#define RATE_LIMIT_WINDOW 60  // seconds
#define MAX_MESSAGES_PER_WINDOW 100
#define BLACKLIST_DURATION 300  // 5 minutes

// IP tracking structure
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int connection_count;
    time_t first_seen;
    int message_count;
    time_t last_message;
    int is_blacklisted;
    time_t blacklist_expiry;
} ip_tracker_t;

// Global IP tracking table
typedef struct {
    ip_tracker_t entries[MAX_CLIENTS];
    int count;
    pthread_mutex_t lock;
} ip_table_t;

ip_table_t ip_table = {0};

// Initialize the IP tracking system
void ddos_protection_init() {
    pthread_mutex_init(&ip_table.lock, NULL);
    ip_table.count = 0;
}

// Find or create IP tracker entry
ip_tracker_t* get_ip_tracker(const char* ip) {
    pthread_mutex_lock(&ip_table.lock);
    
    time_t now = time(NULL);
    
    // Check if IP exists
    for (int i = 0; i < ip_table.count; i++) {
        if (strcmp(ip_table.entries[i].ip, ip) == 0) {
            // Check if blacklist expired
            if (ip_table.entries[i].is_blacklisted && 
                now > ip_table.entries[i].blacklist_expiry) {
                ip_table.entries[i].is_blacklisted = 0;
            }
            
            pthread_mutex_unlock(&ip_table.lock);
            return &ip_table.entries[i];
        }
    }
    
    // Create new entry if space available
    if (ip_table.count < MAX_CLIENTS) {
        ip_tracker_t* tracker = &ip_table.entries[ip_table.count++];
        strncpy(tracker->ip, ip, INET_ADDRSTRLEN);
        tracker->connection_count = 0;
        tracker->first_seen = now;
        tracker->message_count = 0;
        tracker->last_message = now;
        tracker->is_blacklisted = 0;
        
        pthread_mutex_unlock(&ip_table.lock);
        return tracker;
    }
    
    pthread_mutex_unlock(&ip_table.lock);
    return NULL;
}

// Check if connection should be allowed
int check_connection_allowed(const char* ip) {
    ip_tracker_t* tracker = get_ip_tracker(ip);
    if (!tracker) {
        printf("IP tracking table full, rejecting: %s\n", ip);
        return 0;
    }
    
    // Check blacklist
    if (tracker->is_blacklisted) {
        printf("IP is blacklisted: %s\n", ip);
        return 0;
    }
    
    // Check connection limit per IP
    if (tracker->connection_count >= MAX_CONN_PER_IP) {
        printf("Connection limit exceeded for IP: %s (%d connections)\n", 
               ip, tracker->connection_count);
        
        // Blacklist aggressive IPs
        tracker->is_blacklisted = 1;
        tracker->blacklist_expiry = time(NULL) + BLACKLIST_DURATION;
        return 0;
    }
    
    return 1;
}

// Register new connection
void register_connection(const char* ip) {
    ip_tracker_t* tracker = get_ip_tracker(ip);
    if (tracker) {
        pthread_mutex_lock(&ip_table.lock);
        tracker->connection_count++;
        pthread_mutex_unlock(&ip_table.lock);
        printf("Connection registered for %s (total: %d)\n", 
               ip, tracker->connection_count);
    }
}

// Unregister connection
void unregister_connection(const char* ip) {
    ip_tracker_t* tracker = get_ip_tracker(ip);
    if (tracker) {
        pthread_mutex_lock(&ip_table.lock);
        if (tracker->connection_count > 0) {
            tracker->connection_count--;
        }
        pthread_mutex_unlock(&ip_table.lock);
        printf("Connection closed for %s (remaining: %d)\n", 
               ip, tracker->connection_count);
    }
}

// Check message rate limiting
int check_message_rate(const char* ip) {
    ip_tracker_t* tracker = get_ip_tracker(ip);
    if (!tracker) return 0;
    
    time_t now = time(NULL);
    
    pthread_mutex_lock(&ip_table.lock);
    
    // Reset counter if window expired
    if (now - tracker->last_message > RATE_LIMIT_WINDOW) {
        tracker->message_count = 0;
        tracker->last_message = now;
    }
    
    tracker->message_count++;
    
    // Check if rate limit exceeded
    if (tracker->message_count > MAX_MESSAGES_PER_WINDOW) {
        printf("Rate limit exceeded for %s: %d messages in window\n", 
               ip, tracker->message_count);
        
        // Blacklist aggressive clients
        tracker->is_blacklisted = 1;
        tracker->blacklist_expiry = now + BLACKLIST_DURATION;
        
        pthread_mutex_unlock(&ip_table.lock);
        return 0;
    }
    
    pthread_mutex_unlock(&ip_table.lock);
    return 1;
}

// Validate WebSocket Origin header
int validate_origin(const char* origin, const char** allowed_origins, int count) {
    if (!origin) return 0;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(origin, allowed_origins[i]) == 0) {
            return 1;
        }
    }
    
    printf("Invalid origin rejected: %s\n", origin);
    return 0;
}

// Example usage
int main() {
    ddos_protection_init();
    
    const char* allowed_origins[] = {
        "https://example.com",
        "https://app.example.com"
    };
    
    // Simulate connection attempts
    const char* test_ip = "192.168.1.100";
    
    printf("=== Testing Connection Limits ===\n");
    for (int i = 0; i < 7; i++) {
        if (check_connection_allowed(test_ip)) {
            register_connection(test_ip);
        } else {
            printf("Connection rejected for %s\n", test_ip);
        }
    }
    
    printf("\n=== Testing Rate Limiting ===\n");
    const char* test_ip2 = "192.168.1.101";
    register_connection(test_ip2);
    
    for (int i = 0; i < 110; i++) {
        if (!check_message_rate(test_ip2)) {
            printf("Message rejected due to rate limiting\n");
            break;
        }
    }
    
    printf("\n=== Testing Origin Validation ===\n");
    validate_origin("https://example.com", allowed_origins, 2);
    validate_origin("https://malicious.com", allowed_origins, 2);
    
    // Cleanup
    unregister_connection(test_ip);
    unregister_connection(test_ip2);
    pthread_mutex_destroy(&ip_table.lock);
    
    return 0;
}
```

## C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>

using namespace std;
using namespace chrono;

class RateLimiter {
private:
    struct ClientInfo {
        int message_count = 0;
        steady_clock::time_point window_start;
        int connection_count = 0;
        bool is_blacklisted = false;
        steady_clock::time_point blacklist_expiry;
    };
    
    unordered_map<string, ClientInfo> clients_;
    mutex mutex_;
    
    const int max_messages_per_window_;
    const seconds window_duration_;
    const int max_connections_per_ip_;
    const seconds blacklist_duration_;
    
public:
    RateLimiter(int max_messages = 100, 
                seconds window = seconds(60),
                int max_connections = 5,
                seconds blacklist = seconds(300))
        : max_messages_per_window_(max_messages),
          window_duration_(window),
          max_connections_per_ip_(max_connections),
          blacklist_duration_(blacklist) {}
    
    bool check_connection_allowed(const string& ip) {
        lock_guard<mutex> lock(mutex_);
        auto now = steady_clock::now();
        auto& client = clients_[ip];
        
        // Check blacklist
        if (client.is_blacklisted) {
            if (now < client.blacklist_expiry) {
                cout << "Blacklisted IP blocked: " << ip << endl;
                return false;
            }
            client.is_blacklisted = false;
        }
        
        // Check connection limit
        if (client.connection_count >= max_connections_per_ip_) {
            cout << "Connection limit exceeded: " << ip << endl;
            client.is_blacklisted = true;
            client.blacklist_expiry = now + blacklist_duration_;
            return false;
        }
        
        return true;
    }
    
    void register_connection(const string& ip) {
        lock_guard<mutex> lock(mutex_);
        clients_[ip].connection_count++;
        cout << "Connection registered for " << ip 
             << " (total: " << clients_[ip].connection_count << ")" << endl;
    }
    
    void unregister_connection(const string& ip) {
        lock_guard<mutex> lock(mutex_);
        auto it = clients_.find(ip);
        if (it != clients_.end() && it->second.connection_count > 0) {
            it->second.connection_count--;
            cout << "Connection closed for " << ip 
                 << " (remaining: " << it->second.connection_count << ")" << endl;
        }
    }
    
    bool check_message_allowed(const string& ip) {
        lock_guard<mutex> lock(mutex_);
        auto now = steady_clock::now();
        auto& client = clients_[ip];
        
        // Check blacklist
        if (client.is_blacklisted && now < client.blacklist_expiry) {
            return false;
        }
        
        // Reset window if expired
        if (now - client.window_start > window_duration_) {
            client.message_count = 0;
            client.window_start = now;
        }
        
        client.message_count++;
        
        // Check rate limit
        if (client.message_count > max_messages_per_window_) {
            cout << "Rate limit exceeded for " << ip 
                 << ": " << client.message_count << " messages" << endl;
            client.is_blacklisted = true;
            client.blacklist_expiry = now + blacklist_duration_;
            return false;
        }
        
        return true;
    }
    
    void cleanup_expired_entries() {
        lock_guard<mutex> lock(mutex_);
        auto now = steady_clock::now();
        
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (it->second.connection_count == 0 &&
                now - it->second.window_start > window_duration_ * 2) {
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

class OriginValidator {
private:
    vector<string> allowed_origins_;
    bool allow_null_origin_;
    
public:
    OriginValidator(vector<string> origins, bool allow_null = false)
        : allowed_origins_(move(origins)), allow_null_origin_(allow_null) {}
    
    bool validate(const string& origin) const {
        if (origin.empty() && allow_null_origin_) {
            return true;
        }
        
        return find(allowed_origins_.begin(), allowed_origins_.end(), origin) 
               != allowed_origins_.end();
    }
    
    void add_origin(const string& origin) {
        if (find(allowed_origins_.begin(), allowed_origins_.end(), origin) 
            == allowed_origins_.end()) {
            allowed_origins_.push_back(origin);
        }
    }
};

class DDOSProtection {
private:
    RateLimiter rate_limiter_;
    OriginValidator origin_validator_;
    
public:
    DDOSProtection(vector<string> allowed_origins)
        : origin_validator_(move(allowed_origins)) {}
    
    bool allow_connection(const string& ip, const string& origin) {
        if (!origin_validator_.validate(origin)) {
            cout << "Invalid origin rejected: " << origin << endl;
            return false;
        }
        
        if (!rate_limiter_.check_connection_allowed(ip)) {
            return false;
        }
        
        rate_limiter_.register_connection(ip);
        return true;
    }
    
    void close_connection(const string& ip) {
        rate_limiter_.unregister_connection(ip);
    }
    
    bool allow_message(const string& ip) {
        return rate_limiter_.check_message_allowed(ip);
    }
    
    void cleanup() {
        rate_limiter_.cleanup_expired_entries();
    }
};

int main() {
    DDOSProtection protection({"https://example.com", "https://app.example.com"});
    
    cout << "=== Testing Connection Limits ===" << endl;
    string test_ip = "192.168.1.100";
    
    for (int i = 0; i < 7; i++) {
        if (protection.allow_connection(test_ip, "https://example.com")) {
            cout << "Connection " << i + 1 << " accepted" << endl;
        }
    }
    
    cout << "\n=== Testing Rate Limiting ===" << endl;
    string test_ip2 = "192.168.1.101";
    protection.allow_connection(test_ip2, "https://example.com");
    
    for (int i = 0; i < 110; i++) {
        if (!protection.allow_message(test_ip2)) {
            cout << "Message rejected at count: " << i + 1 << endl;
            break;
        }
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone)]
struct ClientInfo {
    message_count: u32,
    window_start: Instant,
    connection_count: u32,
    is_blacklisted: bool,
    blacklist_expiry: Option<Instant>,
}

impl ClientInfo {
    fn new() -> Self {
        Self {
            message_count: 0,
            window_start: Instant::now(),
            connection_count: 0,
            is_blacklisted: false,
            blacklist_expiry: None,
        }
    }
}

pub struct RateLimiter {
    clients: Arc<Mutex<HashMap<String, ClientInfo>>>,
    max_messages_per_window: u32,
    window_duration: Duration,
    max_connections_per_ip: u32,
    blacklist_duration: Duration,
}

impl RateLimiter {
    pub fn new(
        max_messages: u32,
        window_secs: u64,
        max_connections: u32,
        blacklist_secs: u64,
    ) -> Self {
        Self {
            clients: Arc::new(Mutex::new(HashMap::new())),
            max_messages_per_window: max_messages,
            window_duration: Duration::from_secs(window_secs),
            max_connections_per_ip: max_connections,
            blacklist_duration: Duration::from_secs(blacklist_secs),
        }
    }
    
    pub fn check_connection_allowed(&self, ip: &str) -> bool {
        let mut clients = self.clients.lock().unwrap();
        let client = clients.entry(ip.to_string()).or_insert_with(ClientInfo::new);
        let now = Instant::now();
        
        // Check blacklist
        if client.is_blacklisted {
            if let Some(expiry) = client.blacklist_expiry {
                if now < expiry {
                    println!("Blacklisted IP blocked: {}", ip);
                    return false;
                }
                client.is_blacklisted = false;
                client.blacklist_expiry = None;
            }
        }
        
        // Check connection limit
        if client.connection_count >= self.max_connections_per_ip {
            println!("Connection limit exceeded: {}", ip);
            client.is_blacklisted = true;
            client.blacklist_expiry = Some(now + self.blacklist_duration);
            return false;
        }
        
        true
    }
    
    pub fn register_connection(&self, ip: &str) {
        let mut clients = self.clients.lock().unwrap();
        let client = clients.entry(ip.to_string()).or_insert_with(ClientInfo::new);
        client.connection_count += 1;
        println!("Connection registered for {} (total: {})", ip, client.connection_count);
    }
    
    pub fn unregister_connection(&self, ip: &str) {
        let mut clients = self.clients.lock().unwrap();
        if let Some(client) = clients.get_mut(ip) {
            if client.connection_count > 0 {
                client.connection_count -= 1;
                println!("Connection closed for {} (remaining: {})", ip, client.connection_count);
            }
        }
    }
    
    pub fn check_message_allowed(&self, ip: &str) -> bool {
        let mut clients = self.clients.lock().unwrap();
        let client = clients.entry(ip.to_string()).or_insert_with(ClientInfo::new);
        let now = Instant::now();
        
        // Check blacklist
        if client.is_blacklisted {
            if let Some(expiry) = client.blacklist_expiry {
                if now < expiry {
                    return false;
                }
                client.is_blacklisted = false;
                client.blacklist_expiry = None;
            }
        }
        
        // Reset window if expired
        if now.duration_since(client.window_start) > self.window_duration {
            client.message_count = 0;
            client.window_start = now;
        }
        
        client.message_count += 1;
        
        // Check rate limit
        if client.message_count > self.max_messages_per_window {
            println!("Rate limit exceeded for {}: {} messages", ip, client.message_count);
            client.is_blacklisted = true;
            client.blacklist_expiry = Some(now + self.blacklist_duration);
            return false;
        }
        
        true
    }
    
    pub fn cleanup_expired_entries(&self) {
        let mut clients = self.clients.lock().unwrap();
        let now = Instant::now();
        
        clients.retain(|_, client| {
            client.connection_count > 0 || 
            now.duration_since(client.window_start) <= self.window_duration * 2
        });
    }
}

pub struct OriginValidator {
    allowed_origins: Vec<String>,
    allow_null_origin: bool,
}

impl OriginValidator {
    pub fn new(origins: Vec<String>, allow_null: bool) -> Self {
        Self {
            allowed_origins: origins,
            allow_null_origin: allow_null,
        }
    }
    
    pub fn validate(&self, origin: &str) -> bool {
        if origin.is_empty() && self.allow_null_origin {
            return true;
        }
        
        self.allowed_origins.contains(&origin.to_string())
    }
    
    pub fn add_origin(&mut self, origin: String) {
        if !self.allowed_origins.contains(&origin) {
            self.allowed_origins.push(origin);
        }
    }
}

pub struct DDoSProtection {
    rate_limiter: RateLimiter,
    origin_validator: OriginValidator,
}

impl DDoSProtection {
    pub fn new(allowed_origins: Vec<String>) -> Self {
        Self {
            rate_limiter: RateLimiter::new(100, 60, 5, 300),
            origin_validator: OriginValidator::new(allowed_origins, false),
        }
    }
    
    pub fn allow_connection(&self, ip: &str, origin: &str) -> bool {
        if !self.origin_validator.validate(origin) {
            println!("Invalid origin rejected: {}", origin);
            return false;
        }
        
        if !self.rate_limiter.check_connection_allowed(ip) {
            return false;
        }
        
        self.rate_limiter.register_connection(ip);
        true
    }
    
    pub fn close_connection(&self, ip: &str) {
        self.rate_limiter.unregister_connection(ip);
    }
    
    pub fn allow_message(&self, ip: &str) -> bool {
        self.rate_limiter.check_message_allowed(ip)
    }
    
    pub fn cleanup(&self) {
        self.rate_limiter.cleanup_expired_entries();
    }
}

fn main() {
    let protection = DDoSProtection::new(vec![
        "https://example.com".to_string(),
        "https://app.example.com".to_string(),
    ]);
    
    println!("=== Testing Connection Limits ===");
    let test_ip = "192.168.1.100";
    
    for i in 0..7 {
        if protection.allow_connection(test_ip, "https://example.com") {
            println!("Connection {} accepted", i + 1);
        }
    }
    
    println!("\n=== Testing Rate Limiting ===");
    let test_ip2 = "192.168.1.101";
    protection.allow_connection(test_ip2, "https://example.com");
    
    for i in 0..110 {
        if !protection.allow_message(test_ip2) {
            println!("Message rejected at count: {}", i + 1);
            break;
        }
    }
}
```

## Summary

DDoS mitigation for WebSocket services requires a multi-layered defense approach combining connection limiting, rate limiting, origin validation, and intelligent blacklisting. The implementations above demonstrate:

**Core Protection Mechanisms:**
- Per-IP connection limits prevent resource exhaustion
- Message rate limiting prevents flooding attacks
- Sliding window counters track abuse patterns
- Automatic blacklisting isolates aggressive clients
- Origin validation prevents unauthorized domains

**Implementation Considerations:**
- Thread-safe data structures protect shared state
- Efficient lookup with hash maps for IP tracking
- Time-based expiration for blacklists and rate windows
- Memory-efficient cleanup of stale entries
- Configurable thresholds for different threat levels

**Production Deployment:**
- Combine with reverse proxy solutions (nginx, HAProxy) for edge filtering
- Integrate with cloud-based DDoS services (Cloudflare, AWS Shield)
- Monitor metrics: connection rates, message volumes, blacklist sizes
- Implement graduated responses (warnings before blocking)
- Use geographic and ISP-based reputation scoring
- Deploy CAPTCHA challenges for suspicious patterns
- Maintain audit logs for security analysis

Effective WebSocket DDoS protection balances security with legitimate user experience, scaling defenses based on detected threat levels while minimizing false positives.