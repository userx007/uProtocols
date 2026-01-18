# MQTT Rate Limiting: Protecting Systems from Message Storms

## Overview

Rate limiting in MQTT is a critical protective mechanism that prevents message storms—scenarios where excessive message traffic overwhelms brokers, clients, or network infrastructure. Message storms can occur due to misconfigured devices, runaway loops in client code, DDoS attacks, or cascading failures in IoT deployments. Implementing effective rate limiting ensures system stability, fair resource allocation, and protection against both malicious and accidental traffic surges.

## Understanding Message Storms

Message storms in MQTT systems can manifest in several ways:

- **Publisher floods**: A single client publishing messages at unsustainable rates
- **Subscription amplification**: A topic with many subscribers creating multiplicative load
- **Wildcard abuse**: Overly broad wildcard subscriptions matching excessive topics
- **Reconnection storms**: Mass simultaneous reconnections after network failures
- **Feedback loops**: Clients responding to messages by publishing more messages

These scenarios can degrade broker performance, exhaust network bandwidth, increase latency, and potentially crash systems.

## Rate Limiting Strategies

### 1. Token Bucket Algorithm
Allows burst traffic while maintaining average rate limits. Tokens accumulate over time up to a maximum, with each message consuming tokens.

### 2. Leaky Bucket Algorithm
Smooths out traffic by processing messages at a constant rate, queuing excess messages up to a buffer limit.

### 3. Fixed Window Counters
Tracks message counts within fixed time windows (e.g., per second, per minute).

### 4. Sliding Window Log
Maintains timestamps of recent messages for more accurate rate limiting across window boundaries.

## Implementation Examples

### C/C++ Implementation

Here's a comprehensive rate limiting implementation in C++ using the token bucket algorithm with the Eclipse Paho MQTT library:

```cpp
#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <MQTTClient.h>

// Token Bucket Rate Limiter
class TokenBucketRateLimiter {
private:
    double tokens;
    double max_tokens;
    double refill_rate; // tokens per second
    std::chrono::steady_clock::time_point last_refill;
    std::mutex mtx;

public:
    TokenBucketRateLimiter(double rate_per_sec, double burst_size)
        : tokens(burst_size),
          max_tokens(burst_size),
          refill_rate(rate_per_sec),
          last_refill(std::chrono::steady_clock::now()) {}

    bool try_consume(double count = 1.0) {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        
        if (tokens >= count) {
            tokens -= count;
            return true;
        }
        return false;
    }

    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_refill).count() / 1000.0;
        
        tokens = std::min(max_tokens, tokens + elapsed * refill_rate);
        last_refill = now;
    }

    double available_tokens() {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        return tokens;
    }
};

// Rate-Limited MQTT Publisher
class RateLimitedMQTTPublisher {
private:
    MQTTClient client;
    TokenBucketRateLimiter rate_limiter;
    std::atomic<uint64_t> messages_sent;
    std::atomic<uint64_t> messages_dropped;

public:
    RateLimitedMQTTPublisher(const char* broker, const char* client_id,
                             double rate_limit, double burst_size)
        : rate_limiter(rate_limit, burst_size),
          messages_sent(0),
          messages_dropped(0) {
        
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        
        MQTTClient_create(&client, broker, client_id,
                         MQTTCLIENT_PERSISTENCE_NONE, NULL);
        
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        
        int rc = MQTTClient_connect(client, &conn_opts);
        if (rc != MQTTCLIENT_SUCCESS) {
            throw std::runtime_error("Failed to connect to broker");
        }
        
        std::cout << "Connected to broker: " << broker << std::endl;
        std::cout << "Rate limit: " << rate_limit << " msg/sec, Burst: " 
                  << burst_size << std::endl;
    }

    ~RateLimitedMQTTPublisher() {
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
    }

    bool publish(const char* topic, const char* payload, int qos = 0) {
        if (!rate_limiter.try_consume(1.0)) {
            messages_dropped++;
            std::cerr << "Rate limit exceeded. Message dropped. "
                     << "Dropped: " << messages_dropped << std::endl;
            return false;
        }

        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = (void*)payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = qos;
        pubmsg.retained = 0;

        MQTTClient_deliveryToken token;
        int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            messages_sent++;
            return true;
        }
        
        return false;
    }

    void print_stats() {
        std::cout << "\n=== Rate Limiter Statistics ===" << std::endl;
        std::cout << "Messages sent: " << messages_sent << std::endl;
        std::cout << "Messages dropped: " << messages_dropped << std::endl;
        std::cout << "Available tokens: " << rate_limiter.available_tokens() << std::endl;
    }
};

// Example usage
int main() {
    try {
        // Create publisher with 10 msg/sec rate limit and burst of 20
        RateLimitedMQTTPublisher publisher(
            "tcp://localhost:1883",
            "rate_limited_publisher",
            10.0,  // 10 messages per second
            20.0   // burst size of 20 messages
        );

        // Simulate message storm - try to send 100 messages rapidly
        std::cout << "\nSimulating message storm...\n" << std::endl;
        
        for (int i = 0; i < 100; i++) {
            char payload[64];
            snprintf(payload, sizeof(payload), "Message %d", i);
            
            bool sent = publisher.publish("sensors/temperature", payload, 0);
            
            if (sent && i % 10 == 0) {
                std::cout << "Sent message " << i << std::endl;
            }
            
            // Small delay to see rate limiting in action
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        publisher.print_stats();

        // Wait a bit for tokens to refill
        std::cout << "\nWaiting for token refill..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        publisher.print_stats();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

### Rust Implementation

Here's a robust Rust implementation using the `rumqttc` library with multiple rate limiting strategies:## Advanced Rate Limiting Patterns

```rust
use rumqttc::{Client, MqttOptions, QoS};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;

/// Token Bucket Rate Limiter
#[derive(Debug)]
pub struct TokenBucketRateLimiter {
    tokens: f64,
    max_tokens: f64,
    refill_rate: f64,
    last_refill: Instant,
}

impl TokenBucketRateLimiter {
    pub fn new(rate_per_sec: f64, burst_size: f64) -> Self {
        Self {
            tokens: burst_size,
            max_tokens: burst_size,
            refill_rate: rate_per_sec,
            last_refill: Instant::now(),
        }
    }

    fn refill(&mut self) {
        let elapsed = self.last_refill.elapsed().as_secs_f64();
        self.tokens = (self.tokens + elapsed * self.refill_rate).min(self.max_tokens);
        self.last_refill = Instant::now();
    }

    pub fn try_consume(&mut self, count: f64) -> bool {
        self.refill();
        if self.tokens >= count {
            self.tokens -= count;
            true
        } else {
            false
        }
    }

    pub fn available_tokens(&mut self) -> f64 {
        self.refill();
        self.tokens
    }
}

/// Sliding Window Rate Limiter
#[derive(Debug)]
pub struct SlidingWindowRateLimiter {
    timestamps: Vec<Instant>,
    max_requests: usize,
    window_duration: Duration,
}

impl SlidingWindowRateLimiter {
    pub fn new(max_requests: usize, window_duration: Duration) -> Self {
        Self {
            timestamps: Vec::with_capacity(max_requests),
            max_requests,
            window_duration,
        }
    }

    pub fn try_acquire(&mut self) -> bool {
        let now = Instant::now();
        
        // Remove timestamps outside the window
        self.timestamps.retain(|&ts| now.duration_since(ts) < self.window_duration);
        
        if self.timestamps.len() < self.max_requests {
            self.timestamps.push(now);
            true
        } else {
            false
        }
    }

    pub fn current_count(&mut self) -> usize {
        let now = Instant::now();
        self.timestamps.retain(|&ts| now.duration_since(ts) < self.window_duration);
        self.timestamps.len()
    }
}

/// Rate-Limited MQTT Publisher
pub struct RateLimitedPublisher {
    client: Client,
    rate_limiter: Arc<Mutex<TokenBucketRateLimiter>>,
    messages_sent: Arc<Mutex<u64>>,
    messages_dropped: Arc<Mutex<u64>>,
}

impl RateLimitedPublisher {
    pub fn new(
        broker: &str,
        port: u16,
        client_id: &str,
        rate_limit: f64,
        burst_size: f64,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqttoptions = MqttOptions::new(client_id, broker, port);
        mqttoptions.set_keep_alive(Duration::from_secs(20));

        let (client, mut connection) = Client::new(mqttoptions, 10);

        // Spawn event loop in background
        thread::spawn(move || {
            for notification in connection.iter() {
                if let Err(e) = notification {
                    eprintln!("MQTT connection error: {:?}", e);
                    break;
                }
            }
        });

        // Wait a moment for connection to establish
        thread::sleep(Duration::from_millis(100));

        println!("Connected to broker: {}:{}", broker, port);
        println!("Rate limit: {} msg/sec, Burst: {}", rate_limit, burst_size);

        Ok(Self {
            client,
            rate_limiter: Arc::new(Mutex::new(TokenBucketRateLimiter::new(
                rate_limit,
                burst_size,
            ))),
            messages_sent: Arc::new(Mutex::new(0)),
            messages_dropped: Arc::new(Mutex::new(0)),
        })
    }

    pub fn publish(&self, topic: &str, payload: &str, qos: QoS) -> Result<bool, Box<dyn std::error::Error>> {
        let mut limiter = self.rate_limiter.lock().unwrap();
        
        if !limiter.try_consume(1.0) {
            let mut dropped = self.messages_dropped.lock().unwrap();
            *dropped += 1;
            eprintln!("Rate limit exceeded. Message dropped. Total dropped: {}", *dropped);
            return Ok(false);
        }
        drop(limiter);

        match self.client.publish(topic, qos, false, payload.as_bytes()) {
            Ok(_) => {
                let mut sent = self.messages_sent.lock().unwrap();
                *sent += 1;
                Ok(true)
            }
            Err(e) => Err(Box::new(e)),
        }
    }

    pub fn print_stats(&self) {
        let sent = self.messages_sent.lock().unwrap();
        let dropped = self.messages_dropped.lock().unwrap();
        let mut limiter = self.rate_limiter.lock().unwrap();
        
        println!("\n=== Rate Limiter Statistics ===");
        println!("Messages sent: {}", *sent);
        println!("Messages dropped: {}", *dropped);
        println!("Available tokens: {:.2}", limiter.available_tokens());
    }
}

/// Adaptive Rate Limiter with backoff
pub struct AdaptiveRateLimiter {
    base_rate: f64,
    current_rate: f64,
    min_rate: f64,
    max_rate: f64,
    limiter: TokenBucketRateLimiter,
}

impl AdaptiveRateLimiter {
    pub fn new(base_rate: f64, min_rate: f64, max_rate: f64) -> Self {
        Self {
            base_rate,
            current_rate: base_rate,
            min_rate,
            max_rate,
            limiter: TokenBucketRateLimiter::new(base_rate, base_rate * 2.0),
        }
    }

    pub fn try_consume(&mut self) -> bool {
        self.limiter.try_consume(1.0)
    }

    pub fn report_success(&mut self) {
        // Gradually increase rate on success
        self.current_rate = (self.current_rate * 1.05).min(self.max_rate);
        self.update_limiter();
    }

    pub fn report_failure(&mut self) {
        // Decrease rate on failure (backoff)
        self.current_rate = (self.current_rate * 0.5).max(self.min_rate);
        self.update_limiter();
    }

    fn update_limiter(&mut self) {
        self.limiter = TokenBucketRateLimiter::new(
            self.current_rate,
            self.current_rate * 2.0,
        );
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Example 1: Token Bucket Rate Limiter
    println!("=== Example 1: Token Bucket Rate Limiter ===\n");
    
    let publisher = RateLimitedPublisher::new(
        "localhost",
        1883,
        "rate_limited_rust_client",
        10.0,  // 10 messages per second
        20.0,  // burst of 20
    )?;

    println!("\nSimulating message storm...\n");
    
    for i in 0..100 {
        let payload = format!("Message {}", i);
        let sent = publisher.publish("sensors/temperature", &payload, QoS::AtMostOnce)?;
        
        if sent && i % 10 == 0 {
            println!("Sent message {}", i);
        }
        
        thread::sleep(Duration::from_millis(50));
    }

    publisher.print_stats();

    println!("\nWaiting for token refill...");
    thread::sleep(Duration::from_secs(3));
    publisher.print_stats();

    // Example 2: Sliding Window Rate Limiter
    println!("\n\n=== Example 2: Sliding Window Rate Limiter ===\n");
    
    let mut sliding_limiter = SlidingWindowRateLimiter::new(
        10,
        Duration::from_secs(1),
    );

    let mut allowed = 0;
    let mut denied = 0;

    for i in 0..50 {
        if sliding_limiter.try_acquire() {
            allowed += 1;
            if i % 5 == 0 {
                println!("Request {} allowed (current: {})", 
                    i, sliding_limiter.current_count());
            }
        } else {
            denied += 1;
        }
        thread::sleep(Duration::from_millis(50));
    }

    println!("\nSliding Window Results:");
    println!("Allowed: {}, Denied: {}", allowed, denied);

    // Example 3: Adaptive Rate Limiter
    println!("\n\n=== Example 3: Adaptive Rate Limiter ===\n");
    
    let mut adaptive = AdaptiveRateLimiter::new(5.0, 1.0, 20.0);
    
    for i in 0..30 {
        if adaptive.try_consume() {
            // Simulate success/failure
            if i % 7 == 0 {
                adaptive.report_failure();
                println!("Message {} - FAILED (rate decreased)", i);
            } else {
                adaptive.report_success();
                println!("Message {} - SUCCESS (rate increased)", i);
            }
        } else {
            println!("Message {} - RATE LIMITED", i);
        }
        thread::sleep(Duration::from_millis(100));
    }

    Ok(())
}
```

### Per-Client Rate Limiting

Implementing rate limits on a per-client basis ensures fair resource allocation:

```c
// Hash table mapping client IDs to rate limiters
typedef struct {
    char client_id[128];
    TokenBucketRateLimiter* limiter;
    UT_hash_handle hh;
} ClientRateLimiter;

ClientRateLimiter* client_limiters = NULL;

TokenBucketRateLimiter* get_client_limiter(const char* client_id) {
    ClientRateLimiter* entry;
    HASH_FIND_STR(client_limiters, client_id, entry);
    
    if (!entry) {
        entry = malloc(sizeof(ClientRateLimiter));
        strcpy(entry->client_id, client_id);
        entry->limiter = create_rate_limiter(10.0, 20.0);
        HASH_ADD_STR(client_limiters, client_id, entry);
    }
    
    return entry->limiter;
}
```

### Topic-Based Rate Limiting

Different topics may require different rate limits based on their importance or data volume:

```rust
use std::collections::HashMap;

pub struct TopicBasedRateLimiter {
    limiters: HashMap<String, TokenBucketRateLimiter>,
    default_rate: f64,
    default_burst: f64,
}

impl TopicBasedRateLimiter {
    pub fn new(default_rate: f64, default_burst: f64) -> Self {
        Self {
            limiters: HashMap::new(),
            default_rate,
            default_burst,
        }
    }

    pub fn set_topic_limit(&mut self, topic: &str, rate: f64, burst: f64) {
        self.limiters.insert(
            topic.to_string(),
            TokenBucketRateLimiter::new(rate, burst),
        );
    }

    pub fn try_publish(&mut self, topic: &str) -> bool {
        let limiter = self.limiters
            .entry(topic.to_string())
            .or_insert_with(|| {
                TokenBucketRateLimiter::new(self.default_rate, self.default_burst)
            });
        
        limiter.try_consume(1.0)
    }
}
```

### Broker-Side Rate Limiting

Many MQTT brokers support configuration-based rate limiting:

**Mosquitto Configuration:**
```conf
# Maximum message rate per client (messages/second)
max_publish_rate 10

# Maximum connection rate
max_connections 1000
connection_messages true
max_queued_messages 100
```

**HiveMQ Configuration:**
```xml
<mqtt>
    <client-write-buffer-size>65536</client-write-buffer-size>
    <max-queued-messages>1000</max-queued-messages>
    <throttling>
        <incoming-limit>10000</incoming-limit>
    </throttling>
</mqtt>
```

## Best Practices

### 1. Graceful Degradation
When rate limits are exceeded, implement proper error handling rather than dropping messages silently:

```rust
pub enum RateLimitAction {
    Allow,
    Delay(Duration),
    Drop,
}

impl RateLimitedPublisher {
    pub fn publish_with_retry(&self, topic: &str, payload: &str, max_retries: u32) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        for attempt in 0..max_retries {
            if self.publish(topic, payload, QoS::AtMostOnce)? {
                return Ok(());
            }
            
            // Exponential backoff
            let delay = Duration::from_millis(100 * 2_u64.pow(attempt));
            thread::sleep(delay);
        }
        
        Err("Max retries exceeded".into())
    }
}
```

### 2. Monitoring and Metrics
Track rate limiting metrics to identify issues and tune limits:

```cpp
struct RateLimiterMetrics {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> allowed_requests{0};
    std::atomic<uint64_t> denied_requests{0};
    std::atomic<uint64_t> delayed_requests{0};
    
    void record_allowed() { 
        total_requests++; 
        allowed_requests++; 
    }
    
    void record_denied() { 
        total_requests++; 
        denied_requests++; 
    }
    
    double get_denial_rate() const {
        return total_requests > 0 
            ? (double)denied_requests / total_requests 
            : 0.0;
    }
};
```

### 3. Dynamic Rate Adjustment
Adapt rate limits based on system load, time of day, or other factors:

```rust
pub struct DynamicRateLimiter {
    limiter: TokenBucketRateLimiter,
    system_load_threshold: f64,
}

impl DynamicRateLimiter {
    pub fn adjust_for_load(&mut self, current_load: f64) {
        if current_load > self.system_load_threshold {
            // Reduce rate under high load
            let new_rate = self.limiter.refill_rate * 0.7;
            self.limiter = TokenBucketRateLimiter::new(
                new_rate,
                new_rate * 2.0,
            );
        }
    }
}
```

### 4. QoS-Aware Rate Limiting
Apply different limits based on message QoS levels:

```cpp
bool publish_with_qos_aware_limit(int qos, const char* topic, const char* payload) {
    double cost = qos == 2 ? 2.0 : (qos == 1 ? 1.5 : 1.0);
    
    if (rate_limiter.try_consume(cost)) {
        return mqtt_publish(topic, payload, qos);
    }
    
    return false;
}
```

## Summary

Rate limiting is an essential defense mechanism in MQTT systems that protects against message storms and ensures fair resource utilization. Key takeaways include:

**Core Concepts:**
- Token bucket and sliding window algorithms provide flexible rate limiting with burst handling
- Rate limiting can be applied at client, topic, or broker levels
- Message storms can result from bugs, misconfigurations, or malicious activity

**Implementation Strategies:**
- Token bucket algorithm allows controlled bursts while maintaining average rates
- Sliding window provides more accurate limiting across time boundaries
- Adaptive rate limiting responds dynamically to system conditions

**Best Practices:**
- Implement graceful degradation with retry mechanisms and exponential backoff
- Monitor rate limiting metrics to identify patterns and tune limits appropriately
- Use QoS-aware limiting to prioritize critical messages
- Combine client-side and broker-side rate limiting for defense in depth
- Consider topic-based and per-client limits for fine-grained control

**C/C++ and Rust Advantages:**
- Both languages provide excellent performance for high-throughput MQTT applications
- Thread-safe primitives (mutexes, atomics) enable safe concurrent rate limiting
- Zero-cost abstractions in both languages minimize overhead
- Rust's ownership system prevents common concurrency bugs in rate limiter implementations

Effective rate limiting balances system protection with usability, ensuring MQTT deployments remain stable and responsive even under adverse conditions. The choice between algorithms depends on specific requirements around burst tolerance, precision, and computational overhead.