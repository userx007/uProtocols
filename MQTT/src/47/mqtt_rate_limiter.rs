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