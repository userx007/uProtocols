# DDoS Protection for MQTT Brokers

## Detailed Description

DDoS (Distributed Denial of Service) protection for MQTT brokers involves implementing multiple layers of defense to prevent malicious actors from overwhelming the broker with excessive connection requests, message floods, or resource exhaustion attacks. MQTT brokers are particularly vulnerable to DDoS attacks because they maintain persistent connections and process real-time messages at scale.

### Key Attack Vectors

1. **Connection Flooding**: Attackers open thousands of MQTT connections to exhaust broker resources
2. **Message Flooding**: Overwhelming the broker with high-frequency publish messages
3. **Subscription Flooding**: Creating excessive subscriptions to consume memory and processing power
4. **Slow Read/Write Attacks**: Deliberately slow clients that tie up connection resources
5. **Protocol Exploitation**: Abusing MQTT protocol features like QoS levels or large payloads

### Protection Strategies

- **Rate Limiting**: Restrict connections per IP, messages per client, and subscription requests
- **Connection Throttling**: Limit concurrent connections and implement backoff mechanisms
- **Authentication & Authorization**: Strong client validation and ACL enforcement
- **Payload Size Limits**: Restrict maximum message sizes
- **Monitoring & Alerting**: Real-time detection of abnormal traffic patterns
- **Network-Level Protection**: Firewall rules, IP whitelisting/blacklisting
- **Resource Quotas**: Per-client memory and bandwidth limits

## C/C++ Code Examples

### Example 1: Connection Rate Limiter

```c
#include <mosquitto.h>
#include <time.h>
#include <stdbool.h>

#define MAX_CONNECTIONS_PER_IP 10
#define RATE_LIMIT_WINDOW 60  // seconds
#define MAX_MESSAGES_PER_MINUTE 100

typedef struct {
    char ip_address[46];  // IPv6 max length
    int connection_count;
    int message_count;
    time_t window_start;
    time_t last_connection;
} client_rate_limit_t;

// Hash table for tracking clients (simplified)
#define MAX_TRACKED_IPS 10000
client_rate_limit_t rate_limits[MAX_TRACKED_IPS];

bool check_connection_allowed(const char *ip_address) {
    time_t now = time(NULL);
    
    // Find or create rate limit entry
    client_rate_limit_t *limit = find_or_create_limit(ip_address);
    
    if (!limit) {
        return false;  // Too many tracked IPs
    }
    
    // Reset window if expired
    if (now - limit->window_start > RATE_LIMIT_WINDOW) {
        limit->connection_count = 0;
        limit->message_count = 0;
        limit->window_start = now;
    }
    
    // Check connection rate limit
    if (limit->connection_count >= MAX_CONNECTIONS_PER_IP) {
        mosquitto_log_printf(MOSQ_LOG_WARNING, 
            "Connection rate limit exceeded for IP: %s", ip_address);
        return false;
    }
    
    // Check minimum connection interval (prevent rapid reconnects)
    if (now - limit->last_connection < 1) {
        return false;
    }
    
    limit->connection_count++;
    limit->last_connection = now;
    return true;
}

bool check_message_allowed(const char *client_id) {
    client_rate_limit_t *limit = find_limit_by_client(client_id);
    
    if (!limit) return true;
    
    time_t now = time(NULL);
    if (now - limit->window_start > 60) {
        limit->message_count = 0;
        limit->window_start = now;
    }
    
    if (limit->message_count >= MAX_MESSAGES_PER_MINUTE) {
        return false;
    }
    
    limit->message_count++;
    return true;
}
```

### Example 2: Mosquitto Broker Plugin with DDoS Protection

```cpp
#include <mosquitto_plugin.h>
#include <mosquitto_broker.h>
#include <unordered_map>
#include <chrono>
#include <mutex>

class DDoSProtectionPlugin {
private:
    struct ClientStats {
        int connection_attempts;
        int message_count;
        std::chrono::steady_clock::time_point window_start;
        bool is_blocked;
    };
    
    std::unordered_map<std::string, ClientStats> client_stats;
    std::mutex stats_mutex;
    
    const int MAX_CONN_ATTEMPTS = 5;
    const int MAX_MSGS_PER_MIN = 1000;
    const int BLOCK_DURATION_SEC = 300;
    
public:
    int on_basic_auth(const char *username, const char *password, 
                      const char *client_ip) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        auto now = std::chrono::steady_clock::now();
        
        auto &stats = client_stats[client_ip];
        
        // Check if IP is blocked
        if (stats.is_blocked) {
            auto blocked_duration = std::chrono::duration_cast<std::chrono::seconds>(
                now - stats.window_start).count();
            
            if (blocked_duration < BLOCK_DURATION_SEC) {
                mosquitto_log_printf(MOSQ_LOG_WARNING, 
                    "Blocked IP attempted connection: %s", client_ip);
                return MOSQ_ERR_AUTH;
            } else {
                // Unblock after timeout
                stats.is_blocked = false;
                stats.connection_attempts = 0;
            }
        }
        
        // Reset window if needed
        auto window_duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats.window_start).count();
        
        if (window_duration > 60) {
            stats.connection_attempts = 0;
            stats.message_count = 0;
            stats.window_start = now;
        }
        
        stats.connection_attempts++;
        
        // Block if too many attempts
        if (stats.connection_attempts > MAX_CONN_ATTEMPTS) {
            stats.is_blocked = true;
            mosquitto_log_printf(MOSQ_LOG_WARNING, 
                "IP blocked due to excessive connections: %s", client_ip);
            return MOSQ_ERR_AUTH;
        }
        
        return MOSQ_ERR_SUCCESS;
    }
    
    int on_message(const char *client_id, const mosquitto_message *msg) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        
        auto &stats = client_stats[client_id];
        auto now = std::chrono::steady_clock::now();
        
        auto window_duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats.window_start).count();
        
        if (window_duration > 60) {
            stats.message_count = 0;
            stats.window_start = now;
        }
        
        stats.message_count++;
        
        if (stats.message_count > MAX_MSGS_PER_MIN) {
            mosquitto_log_printf(MOSQ_LOG_WARNING, 
                "Message flood detected from: %s", client_id);
            return MOSQ_ERR_QUOTA_EXCEEDED;
        }
        
        // Check payload size
        if (msg->payloadlen > 1024 * 1024) {  // 1MB limit
            mosquitto_log_printf(MOSQ_LOG_WARNING, 
                "Oversized payload rejected from: %s", client_id);
            return MOSQ_ERR_PAYLOAD_SIZE;
        }
        
        return MOSQ_ERR_SUCCESS;
    }
};
```

## Rust Code Examples

### Example 1: Token Bucket Rate Limiter

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};

#[derive(Clone)]
pub struct TokenBucket {
    tokens: f64,
    capacity: f64,
    refill_rate: f64,  // tokens per second
    last_refill: Instant,
}

impl TokenBucket {
    pub fn new(capacity: f64, refill_rate: f64) -> Self {
        TokenBucket {
            tokens: capacity,
            capacity,
            refill_rate,
            last_refill: Instant::now(),
        }
    }
    
    pub fn try_consume(&mut self, tokens: f64) -> bool {
        self.refill();
        
        if self.tokens >= tokens {
            self.tokens -= tokens;
            true
        } else {
            false
        }
    }
    
    fn refill(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_refill).as_secs_f64();
        
        self.tokens = (self.tokens + elapsed * self.refill_rate).min(self.capacity);
        self.last_refill = now;
    }
}

pub struct DDoSProtection {
    connection_limiters: Arc<Mutex<HashMap<String, TokenBucket>>>,
    message_limiters: Arc<Mutex<HashMap<String, TokenBucket>>>,
    blocked_ips: Arc<Mutex<HashMap<String, Instant>>>,
    
    max_connections_per_ip: f64,
    max_messages_per_second: f64,
    block_duration: Duration,
}

impl DDoSProtection {
    pub fn new() -> Self {
        DDoSProtection {
            connection_limiters: Arc::new(Mutex::new(HashMap::new())),
            message_limiters: Arc::new(Mutex::new(HashMap::new())),
            blocked_ips: Arc::new(Mutex::new(HashMap::new())),
            max_connections_per_ip: 10.0,
            max_messages_per_second: 100.0,
            block_duration: Duration::from_secs(300),
        }
    }
    
    pub fn check_connection_allowed(&self, ip: &str) -> Result<(), String> {
        // Check if IP is blocked
        {
            let mut blocked = self.blocked_ips.lock().unwrap();
            if let Some(blocked_time) = blocked.get(ip) {
                if blocked_time.elapsed() < self.block_duration {
                    return Err(format!("IP {} is blocked", ip));
                } else {
                    blocked.remove(ip);
                }
            }
        }
        
        // Check rate limit
        let mut limiters = self.connection_limiters.lock().unwrap();
        let bucket = limiters.entry(ip.to_string())
            .or_insert_with(|| TokenBucket::new(
                self.max_connections_per_ip,
                self.max_connections_per_ip / 60.0  // refill per second
            ));
        
        if bucket.try_consume(1.0) {
            Ok(())
        } else {
            // Block the IP
            self.blocked_ips.lock().unwrap()
                .insert(ip.to_string(), Instant::now());
            Err(format!("Connection rate limit exceeded for IP: {}", ip))
        }
    }
    
    pub fn check_message_allowed(&self, client_id: &str) -> Result<(), String> {
        let mut limiters = self.message_limiters.lock().unwrap();
        let bucket = limiters.entry(client_id.to_string())
            .or_insert_with(|| TokenBucket::new(
                self.max_messages_per_second * 10.0,  // burst capacity
                self.max_messages_per_second
            ));
        
        if bucket.try_consume(1.0) {
            Ok(())
        } else {
            Err(format!("Message rate limit exceeded for client: {}", client_id))
        }
    }
    
    pub fn cleanup_old_entries(&self) {
        let timeout = Duration::from_secs(3600);
        
        // Cleanup connection limiters
        self.connection_limiters.lock().unwrap()
            .retain(|_, bucket| bucket.last_refill.elapsed() < timeout);
        
        // Cleanup message limiters
        self.message_limiters.lock().unwrap()
            .retain(|_, bucket| bucket.last_refill.elapsed() < timeout);
        
        // Cleanup expired blocks
        self.blocked_ips.lock().unwrap()
            .retain(|_, time| time.elapsed() < self.block_duration);
    }
}
```

### Example 2: MQTT Client with DDoS Protection Integration

```rust
use rumqttc::{MqttOptions, Client, QoS, Event, Packet};
use std::sync::Arc;
use std::time::Duration;

pub struct ProtectedMqttBroker {
    ddos_protection: Arc<DDoSProtection>,
}

impl ProtectedMqttBroker {
    pub fn new() -> Self {
        ProtectedMqttBroker {
            ddos_protection: Arc::new(DDoSProtection::new()),
        }
    }
    
    pub fn handle_connection(&self, client_ip: &str, client_id: &str) -> Result<(), String> {
        // Check connection rate limit
        self.ddos_protection.check_connection_allowed(client_ip)?;
        
        println!("Connection allowed for client: {} from IP: {}", client_id, client_ip);
        Ok(())
    }
    
    pub fn handle_publish(&self, client_id: &str, topic: &str, 
                          payload: &[u8], qos: QoS) -> Result<(), String> {
        // Check message rate limit
        self.ddos_protection.check_message_allowed(client_id)?;
        
        // Validate payload size
        const MAX_PAYLOAD_SIZE: usize = 1024 * 1024; // 1MB
        if payload.len() > MAX_PAYLOAD_SIZE {
            return Err(format!(
                "Payload size {} exceeds maximum allowed size {}",
                payload.len(), MAX_PAYLOAD_SIZE
            ));
        }
        
        // Validate topic depth
        const MAX_TOPIC_DEPTH: usize = 10;
        let topic_levels: Vec<&str> = topic.split('/').collect();
        if topic_levels.len() > MAX_TOPIC_DEPTH {
            return Err(format!(
                "Topic depth {} exceeds maximum allowed depth {}",
                topic_levels.len(), MAX_TOPIC_DEPTH
            ));
        }
        
        println!("Message allowed from client: {} to topic: {}", client_id, topic);
        Ok(())
    }
    
    pub fn start_cleanup_task(&self) {
        let protection = self.ddos_protection.clone();
        
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(Duration::from_secs(300));
                protection.cleanup_old_entries();
                println!("Cleaned up old DDoS protection entries");
            }
        });
    }
}

// Example usage
fn example_usage() {
    let broker = ProtectedMqttBroker::new();
    broker.start_cleanup_task();
    
    // Simulate connection attempt
    match broker.handle_connection("192.168.1.100", "client_001") {
        Ok(_) => println!("Connection established"),
        Err(e) => println!("Connection rejected: {}", e),
    }
    
    // Simulate publish attempt
    match broker.handle_publish("client_001", "sensors/temperature", 
                                 b"23.5", QoS::AtLeastOnce) {
        Ok(_) => println!("Message published"),
        Err(e) => println!("Message rejected: {}", e),
    }
}
```

## Summary

**DDoS Protection for MQTT Brokers** is a critical security measure that implements multiple defensive layers to prevent service disruption from malicious traffic. The core strategies include:

1. **Rate Limiting**: Using token bucket algorithms or sliding window counters to restrict connections and messages per client/IP
2. **Connection Management**: Throttling concurrent connections and implementing temporary IP blocks for abusive clients
3. **Payload Validation**: Enforcing size limits and topic depth restrictions to prevent resource exhaustion
4. **Monitoring**: Real-time tracking of connection attempts, message rates, and abnormal patterns

The C/C++ examples demonstrate low-level rate limiting suitable for broker plugins, while the Rust examples showcase thread-safe token bucket implementations with automatic cleanup. Effective DDoS protection requires combining application-level controls (shown in the code) with network-level defenses like firewalls, load balancers, and intrusion detection systems. Regular monitoring, logging, and dynamic threshold adjustment based on traffic patterns are essential for maintaining robust protection against evolving attack vectors.