# Rate Limiting and Throttling in WebSocket Applications

Rate limiting and throttling are critical security and stability mechanisms in WebSocket applications. They prevent individual clients from overwhelming the server with excessive messages, protect against denial-of-service (DoS) attacks, and ensure fair resource distribution among all connected clients.

## Core Concepts

**Rate Limiting** restricts the number of messages or actions a client can perform within a specific time window. Once the limit is exceeded, additional requests are rejected or delayed.

**Throttling** controls the rate at which messages are processed, either by delaying message handling or by dropping excess messages to maintain system stability.

### Common Rate Limiting Strategies

1. **Token Bucket**: Clients accumulate tokens over time; each message costs a token
2. **Leaky Bucket**: Messages are processed at a constant rate, excess messages overflow
3. **Fixed Window**: Simple counter reset at fixed time intervals
4. **Sliding Window**: More accurate tracking using a rolling time window

## C/C++ Implementation

Here's a comprehensive example using the token bucket algorithm with libwebsockets:

```c
#include <libwebsockets.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 10
#define TOKEN_REFILL_RATE 2  // tokens per second
#define BUCKET_CAPACITY 10

// Per-connection rate limit state
struct rate_limiter {
    double tokens;
    time_t last_refill;
    int violations;
    int is_banned;
};

// Session data for each WebSocket connection
struct session_data {
    struct lws *wsi;
    struct rate_limiter limiter;
    char client_ip[64];
};

// Initialize rate limiter for a new connection
void init_rate_limiter(struct rate_limiter *limiter) {
    limiter->tokens = BUCKET_CAPACITY;
    limiter->last_refill = time(NULL);
    limiter->violations = 0;
    limiter->is_banned = 0;
}

// Refill tokens based on elapsed time
void refill_tokens(struct rate_limiter *limiter) {
    time_t now = time(NULL);
    double elapsed = difftime(now, limiter->last_refill);
    
    if (elapsed > 0) {
        double new_tokens = elapsed * TOKEN_REFILL_RATE;
        limiter->tokens += new_tokens;
        
        // Cap at bucket capacity
        if (limiter->tokens > BUCKET_CAPACITY) {
            limiter->tokens = BUCKET_CAPACITY;
        }
        
        limiter->last_refill = now;
    }
}

// Check if client can send a message (consumes 1 token)
int can_send_message(struct rate_limiter *limiter) {
    if (limiter->is_banned) {
        return 0;
    }
    
    refill_tokens(limiter);
    
    if (limiter->tokens >= 1.0) {
        limiter->tokens -= 1.0;
        return 1;
    }
    
    // Rate limit exceeded
    limiter->violations++;
    
    // Ban after 5 violations
    if (limiter->violations > 5) {
        limiter->is_banned = 1;
        lwsl_warn("Client banned due to repeated violations\n");
    }
    
    return 0;
}

// WebSocket callback function
static int callback_websocket(struct lws *wsi, 
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // Initialize new connection
            init_rate_limiter(&session->limiter);
            session->wsi = wsi;
            
            // Get client IP for logging
            char name[128];
            char rip[128];
            lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi),
                                  name, sizeof(name),
                                  rip, sizeof(rip));
            strncpy(session->client_ip, rip, sizeof(session->client_ip) - 1);
            
            lwsl_user("New connection from %s\n", session->client_ip);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            // Check rate limit before processing message
            if (!can_send_message(&session->limiter)) {
                lwsl_warn("Rate limit exceeded for %s (tokens: %.2f)\n",
                         session->client_ip, session->limiter.tokens);
                
                // Send rate limit error to client
                const char *error_msg = "{\"error\":\"Rate limit exceeded\"}";
                unsigned char buf[LWS_PRE + 256];
                memcpy(&buf[LWS_PRE], error_msg, strlen(error_msg));
                lws_write(wsi, &buf[LWS_PRE], strlen(error_msg), 
                         LWS_WRITE_TEXT);
                
                // Optionally close connection if banned
                if (session->limiter.is_banned) {
                    return -1;  // Close connection
                }
                
                break;
            }
            
            // Process the message normally
            lwsl_user("Processing message from %s: %.*s (tokens remaining: %.2f)\n",
                     session->client_ip, (int)len, (char *)in, 
                     session->limiter.tokens);
            
            // Echo message back (example)
            unsigned char buf[LWS_PRE + 4096];
            if (len < 4096) {
                memcpy(&buf[LWS_PRE], in, len);
                lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            }
            
            break;
        }
        
        case LWS_CALLBACK_CLOSED:
            lwsl_user("Connection closed: %s\n", session->client_ip);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "rate-limited-protocol",
        callback_websocket,
        sizeof(struct session_data),
        4096,  // rx buffer size
    },
    { NULL, NULL, 0, 0 } // terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return -1;
    }
    
    lwsl_user("WebSocket server started on port 8080\n");
    
    // Event loop
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

Here's a modern implementation using the `tokio-tungstenite` library with async/await:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;
use tokio::sync::Mutex;
use std::time::{Duration, Instant};
use std::collections::HashMap;
use std::net::SocketAddr;

/// Token bucket rate limiter
#[derive(Debug, Clone)]
struct TokenBucket {
    capacity: f64,
    tokens: f64,
    refill_rate: f64,  // tokens per second
    last_refill: Instant,
}

impl TokenBucket {
    fn new(capacity: f64, refill_rate: f64) -> Self {
        Self {
            capacity,
            tokens: capacity,
            refill_rate,
            last_refill: Instant::now(),
        }
    }
    
    /// Refill tokens based on elapsed time
    fn refill(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_refill).as_secs_f64();
        
        if elapsed > 0.0 {
            let new_tokens = elapsed * self.refill_rate;
            self.tokens = (self.tokens + new_tokens).min(self.capacity);
            self.last_refill = now;
        }
    }
    
    /// Try to consume tokens, returns true if successful
    fn consume(&mut self, tokens: f64) -> bool {
        self.refill();
        
        if self.tokens >= tokens {
            self.tokens -= tokens;
            true
        } else {
            false
        }
    }
    
    fn available_tokens(&self) -> f64 {
        self.tokens
    }
}

/// Rate limiter with violation tracking
struct RateLimiter {
    bucket: TokenBucket,
    violations: u32,
    is_banned: bool,
    max_violations: u32,
}

impl RateLimiter {
    fn new(capacity: f64, refill_rate: f64, max_violations: u32) -> Self {
        Self {
            bucket: TokenBucket::new(capacity, refill_rate),
            violations: 0,
            is_banned: false,
            max_violations,
        }
    }
    
    /// Check if message is allowed, track violations
    fn check_and_consume(&mut self) -> Result<(), String> {
        if self.is_banned {
            return Err("Client is banned".to_string());
        }
        
        if self.bucket.consume(1.0) {
            // Reset violations on successful consumption
            if self.violations > 0 {
                self.violations = self.violations.saturating_sub(1);
            }
            Ok(())
        } else {
            self.violations += 1;
            
            if self.violations >= self.max_violations {
                self.is_banned = true;
                Err(format!("Banned after {} violations", self.violations))
            } else {
                Err(format!(
                    "Rate limit exceeded. {} violations. {:.2} tokens available.",
                    self.violations,
                    self.bucket.available_tokens()
                ))
            }
        }
    }
}

/// Global rate limiter storage
type RateLimiterMap = Arc<Mutex<HashMap<SocketAddr, RateLimiter>>>;

/// Handle individual WebSocket connection
async fn handle_connection(
    stream: TcpStream,
    addr: SocketAddr,
    rate_limiters: RateLimiterMap,
) {
    println!("New connection from: {}", addr);
    
    // Initialize rate limiter for this connection
    {
        let mut limiters = rate_limiters.lock().await;
        limiters.insert(
            addr,
            RateLimiter::new(
                10.0,  // capacity: 10 tokens
                2.0,   // refill: 2 tokens per second
                5,     // max violations before ban
            ),
        );
    }
    
    // Accept WebSocket connection
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed for {}: {}", addr, e);
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Message handling loop
    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                // Check rate limit
                let limit_result = {
                    let mut limiters = rate_limiters.lock().await;
                    if let Some(limiter) = limiters.get_mut(&addr) {
                        limiter.check_and_consume()
                    } else {
                        Err("Limiter not found".to_string())
                    }
                };
                
                match limit_result {
                    Ok(()) => {
                        println!("Message from {}: {}", addr, text);
                        
                        // Echo message back
                        let response = format!("Echo: {}", text);
                        if let Err(e) = write.send(Message::Text(response)).await {
                            eprintln!("Failed to send message to {}: {}", addr, e);
                            break;
                        }
                    }
                    Err(error_msg) => {
                        eprintln!("Rate limit error for {}: {}", addr, error_msg);
                        
                        // Send error to client
                        let error_response = format!(
                            "{{\"error\": \"{}\"}}",
                            error_msg
                        );
                        
                        if let Err(e) = write.send(Message::Text(error_response)).await {
                            eprintln!("Failed to send error to {}: {}", addr, e);
                        }
                        
                        // Close connection if banned
                        if error_msg.contains("Banned") {
                            let _ = write.send(Message::Close(None)).await;
                            break;
                        }
                    }
                }
            }
            Ok(Message::Binary(data)) => {
                // Apply same rate limiting to binary messages
                let limit_result = {
                    let mut limiters = rate_limiters.lock().await;
                    if let Some(limiter) = limiters.get_mut(&addr) {
                        limiter.check_and_consume()
                    } else {
                        Err("Limiter not found".to_string())
                    }
                };
                
                if limit_result.is_ok() {
                    println!("Binary message from {}: {} bytes", addr, data.len());
                    let _ = write.send(Message::Binary(data)).await;
                }
            }
            Ok(Message::Ping(data)) => {
                let _ = write.send(Message::Pong(data)).await;
            }
            Ok(Message::Close(_)) => {
                println!("Connection closed by client: {}", addr);
                break;
            }
            Err(e) => {
                eprintln!("WebSocket error for {}: {}", addr, e);
                break;
            }
            _ => {}
        }
    }
    
    // Cleanup
    {
        let mut limiters = rate_limiters.lock().await;
        limiters.remove(&addr);
    }
    println!("Connection cleanup complete for: {}", addr);
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    let rate_limiters: RateLimiterMap = Arc::new(Mutex::new(HashMap::new()));
    
    // Accept connections
    while let Ok((stream, addr)) = listener.accept().await {
        let rate_limiters = Arc::clone(&rate_limiters);
        
        tokio::spawn(async move {
            handle_connection(stream, addr, rate_limiters).await;
        });
    }
    
    Ok(())
}
```

### Advanced Rust Example: Sliding Window Rate Limiter

```rust
use std::collections::VecDeque;
use std::time::{Duration, Instant};

/// Sliding window rate limiter for more accurate tracking
struct SlidingWindowLimiter {
    window_size: Duration,
    max_requests: usize,
    timestamps: VecDeque<Instant>,
}

impl SlidingWindowLimiter {
    fn new(window_size: Duration, max_requests: usize) -> Self {
        Self {
            window_size,
            max_requests,
            timestamps: VecDeque::new(),
        }
    }
    
    /// Remove expired timestamps outside the window
    fn cleanup(&mut self) {
        let now = Instant::now();
        let cutoff = now - self.window_size;
        
        while let Some(&timestamp) = self.timestamps.front() {
            if timestamp < cutoff {
                self.timestamps.pop_front();
            } else {
                break;
            }
        }
    }
    
    /// Check if request is allowed and record it
    fn allow_request(&mut self) -> bool {
        self.cleanup();
        
        if self.timestamps.len() < self.max_requests {
            self.timestamps.push_back(Instant::now());
            true
        } else {
            false
        }
    }
    
    /// Get current request count in window
    fn current_count(&mut self) -> usize {
        self.cleanup();
        self.timestamps.len()
    }
}
```

## Summary

Rate limiting and throttling are essential defensive mechanisms for WebSocket servers. The token bucket algorithm provides a flexible, fair approach that allows brief bursts while maintaining long-term rate control. Key implementation considerations include:

- **Per-connection tracking**: Each WebSocket connection needs its own rate limiter state
- **Token refill mechanisms**: Gradual token replenishment enables sustained but controlled message flow
- **Violation tracking**: Detect and respond to repeated abuse attempts
- **Graceful degradation**: Send informative error messages before disconnecting clients
- **Configurable limits**: Adjust capacity and refill rates based on application requirements

The C implementation uses libwebsockets with manual token bucket management, while the Rust version leverages async/await and type safety for cleaner concurrent handling. Both approaches effectively prevent DoS attacks and ensure fair resource allocation across all connected clients.