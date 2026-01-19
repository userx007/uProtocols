# Backpressure Management in WebSocket Applications

## Overview

**Backpressure** occurs when a producer (sender) generates data faster than a consumer (receiver) can process it. In WebSocket applications, this commonly happens when:

- A server sends messages faster than a client can handle them
- A client's network connection is slower than the server's send rate
- Message processing involves heavy computation or I/O operations
- Multiple concurrent connections exhaust system resources

Without proper backpressure management, applications risk memory exhaustion, message loss, degraded performance, and eventual crashes.

## Core Concepts

### 1. **Detection Mechanisms**
- Monitor send buffer sizes
- Track message queue depths
- Measure processing latency
- Observe memory consumption

### 2. **Mitigation Strategies**
- **Flow Control**: Pause/resume data transmission
- **Buffering**: Implement bounded queues with overflow policies
- **Rate Limiting**: Throttle message production
- **Load Shedding**: Drop non-critical messages when overwhelmed
- **Back-off Mechanisms**: Slow down senders dynamically

---

## C/C++ Implementation

This example uses **libwebsockets** with custom backpressure handling:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>

#define MAX_QUEUE_SIZE 100
#define HIGH_WATER_MARK 80
#define LOW_WATER_MARK 20

typedef struct {
    char *data;
    size_t len;
} message_t;

typedef struct {
    message_t queue[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    int backpressure_active;
} connection_context_t;

// Check if backpressure should be applied
int should_apply_backpressure(connection_context_t *ctx) {
    return ctx->count >= HIGH_WATER_MARK;
}

// Check if backpressure can be released
int can_release_backpressure(connection_context_t *ctx) {
    return ctx->count <= LOW_WATER_MARK;
}

// Enqueue message with overflow handling
int enqueue_message(connection_context_t *ctx, const char *data, size_t len) {
    if (ctx->count >= MAX_QUEUE_SIZE) {
        // Load shedding: drop oldest message
        free(ctx->queue[ctx->tail].data);
        ctx->tail = (ctx->tail + 1) % MAX_QUEUE_SIZE;
        ctx->count--;
    }
    
    ctx->queue[ctx->head].data = malloc(len);
    memcpy(ctx->queue[ctx->head].data, data, len);
    ctx->queue[ctx->head].len = len;
    ctx->head = (ctx->head + 1) % MAX_QUEUE_SIZE;
    ctx->count++;
    
    // Apply backpressure if needed
    if (should_apply_backpressure(ctx) && !ctx->backpressure_active) {
        ctx->backpressure_active = 1;
        lwsl_warn("Backpressure activated: queue=%d\n", ctx->count);
        return 1; // Signal to slow down
    }
    return 0;
}

// Dequeue and send message
int dequeue_and_send(struct lws *wsi, connection_context_t *ctx) {
    if (ctx->count == 0) return 0;
    
    message_t *msg = &ctx->queue[ctx->tail];
    
    // Prepare buffer with LWS_PRE padding
    unsigned char buf[LWS_PRE + msg->len];
    memcpy(&buf[LWS_PRE], msg->data, msg->len);
    
    int written = lws_write(wsi, &buf[LWS_PRE], msg->len, LWS_WRITE_TEXT);
    
    if (written < 0) return -1;
    
    // Successfully sent, remove from queue
    free(msg->data);
    ctx->tail = (ctx->tail + 1) % MAX_QUEUE_SIZE;
    ctx->count--;
    
    // Release backpressure if possible
    if (can_release_backpressure(ctx) && ctx->backpressure_active) {
        ctx->backpressure_active = 0;
        lwsl_notice("Backpressure released: queue=%d\n", ctx->count);
    }
    
    return ctx->count > 0 ? 1 : 0; // More messages pending?
}

static int callback_websocket(struct lws *wsi, 
                               enum lws_callback_reasons reason,
                               void *user, void *in, size_t len) {
    connection_context_t *ctx = (connection_context_t *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            memset(ctx, 0, sizeof(*ctx));
            lwsl_notice("Connection established\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Incoming message - check backpressure before processing
            if (ctx->backpressure_active) {
                lwsl_warn("Backpressure active, slowing processing\n");
                // Could implement rate limiting here
            }
            enqueue_message(ctx, (const char *)in, len);
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Try to drain the queue
            while (dequeue_and_send(wsi, ctx) > 0) {
                // Continue draining
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            // Cleanup
            for (int i = 0; i < ctx->count; i++) {
                int idx = (ctx->tail + i) % MAX_QUEUE_SIZE;
                free(ctx->queue[idx].data);
            }
            break;
    }
    
    return 0;
}
```

**Key Features:**
- Bounded circular queue with configurable limits
- High/low water mark thresholds for hysteresis
- Load shedding drops oldest messages when queue is full
- Explicit backpressure signaling to upstream components

---

## Rust Implementation

Using **tokio-tungstenite** with async backpressure control:

```rust
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::TcpListener;
use tokio::sync::mpsc;
use futures_util::{StreamExt, SinkExt};
use std::time::Duration;

const MAX_QUEUE_SIZE: usize = 100;
const HIGH_WATER_MARK: usize = 80;
const LOW_WATER_MARK: usize = 20;

#[derive(Debug)]
enum BackpressureState {
    Normal,
    Throttled,
    Overloaded,
}

struct BackpressureManager {
    state: BackpressureState,
    queue_size: usize,
}

impl BackpressureManager {
    fn new() -> Self {
        Self {
            state: BackpressureState::Normal,
            queue_size: 0,
        }
    }
    
    fn update(&mut self, queue_size: usize) -> BackpressureState {
        self.queue_size = queue_size;
        
        self.state = match queue_size {
            n if n >= HIGH_WATER_MARK => BackpressureState::Overloaded,
            n if n > LOW_WATER_MARK => BackpressureState::Throttled,
            _ => BackpressureState::Normal,
        };
        
        self.state
    }
    
    fn get_delay(&self) -> Option<Duration> {
        match self.state {
            BackpressureState::Normal => None,
            BackpressureState::Throttled => Some(Duration::from_millis(10)),
            BackpressureState::Overloaded => Some(Duration::from_millis(50)),
        }
    }
}

async fn handle_connection(stream: tokio::net::TcpStream) {
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake error: {}", e);
            return;
        }
    };
    
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    // Bounded channel for backpressure
    let (tx, mut rx) = mpsc::channel::<Message>(MAX_QUEUE_SIZE);
    
    let mut bp_manager = BackpressureManager::new();
    
    // Sender task with backpressure awareness
    let sender_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if let Err(e) = ws_sender.send(msg).await {
                eprintln!("Send error: {}", e);
                break;
            }
        }
    });
    
    // Receiver task with dynamic throttling
    let receiver_task = tokio::spawn(async move {
        while let Some(msg_result) = ws_receiver.next().await {
            match msg_result {
                Ok(msg) => {
                    // Check backpressure state
                    let queue_size = MAX_QUEUE_SIZE - tx.capacity();
                    let state = bp_manager.update(queue_size);
                    
                    match state {
                        BackpressureState::Normal => {
                            // Normal operation
                            if let Err(e) = tx.send(msg).await {
                                eprintln!("Channel send error: {}", e);
                                break;
                            }
                        }
                        BackpressureState::Throttled => {
                            println!("⚠️  Backpressure: Throttled (queue: {})", queue_size);
                            
                            // Apply delay before sending
                            if let Some(delay) = bp_manager.get_delay() {
                                tokio::time::sleep(delay).await;
                            }
                            
                            if let Err(e) = tx.send(msg).await {
                                eprintln!("Channel send error: {}", e);
                                break;
                            }
                        }
                        BackpressureState::Overloaded => {
                            println!("🔴 Backpressure: Overloaded (queue: {}), dropping message", 
                                     queue_size);
                            
                            // Load shedding: drop message or send error response
                            let _ = tx.try_send(Message::Text(
                                "Server overloaded, message dropped".to_string()
                            ));
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Receive error: {}", e);
                    break;
                }
            }
        }
    });
    
    // Wait for tasks to complete
    tokio::select! {
        _ = sender_task => println!("Sender task finished"),
        _ = receiver_task => println!("Receiver task finished"),
    }
}

#[tokio::main]
async fn main() {
    let listener = TcpListener::bind("127.0.0.1:8080").await.unwrap();
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        tokio::spawn(handle_connection(stream));
    }
}
```

**Advanced Features:**
- Bounded `mpsc::channel` provides natural backpressure
- Three-tier state machine (Normal/Throttled/Overloaded)
- Dynamic delays based on queue depth
- Graceful load shedding with client notification
- Async-first design with tokio runtime

---

## Additional Backpressure Techniques

### 1. **Adaptive Rate Limiting**
```rust
// Token bucket algorithm
struct RateLimiter {
    tokens: f64,
    capacity: f64,
    rate: f64, // tokens per second
    last_update: std::time::Instant,
}

impl RateLimiter {
    fn allow(&mut self) -> bool {
        let now = std::time::Instant::now();
        let elapsed = now.duration_since(self.last_update).as_secs_f64();
        
        self.tokens = (self.tokens + elapsed * self.rate).min(self.capacity);
        self.last_update = now;
        
        if self.tokens >= 1.0 {
            self.tokens -= 1.0;
            true
        } else {
            false
        }
    }
}
```

### 2. **Circuit Breaker Pattern**
```c
typedef enum {
    CLOSED,    // Normal operation
    OPEN,      // Stop sending, backpressure active
    HALF_OPEN  // Testing recovery
} circuit_state_t;

typedef struct {
    circuit_state_t state;
    int failure_count;
    time_t last_failure;
} circuit_breaker_t;
```

---

## Summary

**Backpressure management** is critical for building robust WebSocket applications that handle varying loads gracefully. Key takeaways:

- **Monitor queue depths** and memory usage continuously
- **Implement bounded buffers** to prevent unbounded growth
- **Use high/low water marks** for hysteresis to avoid oscillation
- **Apply progressive strategies**: throttling → load shedding → connection termination
- **Leverage language features**: C/C++ requires manual queue management; Rust's async channels provide built-in backpressure
- **Communicate clearly**: Notify clients when backpressure is active
- **Test under load**: Simulate slow consumers and high-throughput scenarios

Proper backpressure handling transforms brittle systems into resilient ones that degrade gracefully under stress rather than collapsing catastrophically.