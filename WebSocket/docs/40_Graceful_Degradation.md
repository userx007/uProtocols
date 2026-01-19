# WebSocket Graceful Degradation

## Overview

Graceful degradation in WebSocket programming refers to the ability of an application to maintain functionality when optimal conditions aren't met. This includes falling back to alternative communication methods when WebSocket connections fail, handling partial system failures, and ensuring service continuity despite network issues or server problems.

## Key Concepts

### 1. **Fallback Mechanisms**
When WebSocket connections cannot be established or maintained, applications should fall back to:
- HTTP long-polling
- Server-Sent Events (SSE)
- Traditional HTTP polling
- Local caching and queuing

### 2. **Partial Failure Handling**
- Detecting connection degradation
- Implementing retry strategies with exponential backoff
- Maintaining message queues during outages
- Graceful reconnection without data loss

### 3. **Service Continuity**
- Client-side state management
- Offline functionality
- Progressive enhancement
- User experience preservation

---

## C/C++ Implementation

Here's a comprehensive example showing graceful degradation with fallback to HTTP polling:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>

#define MAX_RETRIES 5
#define INITIAL_BACKOFF 1
#define MAX_BACKOFF 32
#define BUFFER_SIZE 4096

typedef enum {
    TRANSPORT_WEBSOCKET,
    TRANSPORT_POLLING,
    TRANSPORT_OFFLINE
} TransportMode;

typedef struct {
    int socket_fd;
    TransportMode mode;
    int retry_count;
    int backoff_seconds;
    bool connected;
    char message_queue[10][256];
    int queue_size;
} ConnectionState;

// Initialize connection state
void init_connection(ConnectionState* state) {
    state->socket_fd = -1;
    state->mode = TRANSPORT_WEBSOCKET;
    state->retry_count = 0;
    state->backoff_seconds = INITIAL_BACKOFF;
    state->connected = false;
    state->queue_size = 0;
}

// Calculate exponential backoff
int calculate_backoff(int retry_count) {
    int backoff = INITIAL_BACKOFF << retry_count;
    return backoff > MAX_BACKOFF ? MAX_BACKOFF : backoff;
}

// Attempt WebSocket connection
bool try_websocket_connect(ConnectionState* state, const char* host, int port) {
    struct sockaddr_in server_addr;
    
    state->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (state->socket_fd < 0) {
        perror("Socket creation failed");
        return false;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(state->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);
    
    if (connect(state->socket_fd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        close(state->socket_fd);
        state->socket_fd = -1;
        return false;
    }
    
    // Send WebSocket upgrade request
    char upgrade_request[512];
    snprintf(upgrade_request, sizeof(upgrade_request),
             "GET / HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             host, port);
    
    if (send(state->socket_fd, upgrade_request, strlen(upgrade_request), 0) < 0) {
        close(state->socket_fd);
        state->socket_fd = -1;
        return false;
    }
    
    // Read upgrade response
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(state->socket_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0 || strstr(buffer, "101 Switching Protocols") == NULL) {
        close(state->socket_fd);
        state->socket_fd = -1;
        return false;
    }
    
    state->connected = true;
    state->retry_count = 0;
    state->backoff_seconds = INITIAL_BACKOFF;
    return true;
}

// Fallback to HTTP polling
bool fallback_to_polling(ConnectionState* state) {
    printf("Falling back to HTTP polling mode\n");
    state->mode = TRANSPORT_POLLING;
    state->connected = true; // Polling is always "connected"
    return true;
}

// Queue message for later delivery
bool queue_message(ConnectionState* state, const char* message) {
    if (state->queue_size >= 10) {
        printf("Message queue full, dropping oldest message\n");
        // Shift queue
        for (int i = 0; i < 9; i++) {
            strcpy(state->message_queue[i], state->message_queue[i + 1]);
        }
        state->queue_size = 9;
    }
    
    strcpy(state->message_queue[state->queue_size], message);
    state->queue_size++;
    printf("Queued message: %s (queue size: %d)\n", message, state->queue_size);
    return true;
}

// Flush queued messages
void flush_queue(ConnectionState* state) {
    if (state->queue_size == 0) return;
    
    printf("Flushing %d queued messages\n", state->queue_size);
    for (int i = 0; i < state->queue_size; i++) {
        printf("Sending queued: %s\n", state->message_queue[i]);
        // Send via current transport
    }
    state->queue_size = 0;
}

// Attempt connection with graceful degradation
bool connect_with_fallback(ConnectionState* state, const char* host, int port) {
    // Try WebSocket first
    if (try_websocket_connect(state, host, port)) {
        printf("WebSocket connection established\n");
        state->mode = TRANSPORT_WEBSOCKET;
        flush_queue(state);
        return true;
    }
    
    // Implement exponential backoff
    if (state->retry_count < MAX_RETRIES) {
        state->backoff_seconds = calculate_backoff(state->retry_count);
        printf("WebSocket failed, retrying in %d seconds (attempt %d/%d)\n",
               state->backoff_seconds, state->retry_count + 1, MAX_RETRIES);
        sleep(state->backoff_seconds);
        state->retry_count++;
        return connect_with_fallback(state, host, port);
    }
    
    // All WebSocket retries exhausted, fallback to polling
    printf("WebSocket connection failed after %d retries\n", MAX_RETRIES);
    return fallback_to_polling(state);
}

// Send message with degradation handling
bool send_message(ConnectionState* state, const char* message) {
    if (!state->connected) {
        return queue_message(state, message);
    }
    
    switch (state->mode) {
        case TRANSPORT_WEBSOCKET:
            // Send via WebSocket (simplified, should include framing)
            if (send(state->socket_fd, message, strlen(message), 0) < 0) {
                printf("WebSocket send failed, queuing message\n");
                state->connected = false;
                return queue_message(state, message);
            }
            printf("Sent via WebSocket: %s\n", message);
            return true;
            
        case TRANSPORT_POLLING:
            // Simulate HTTP POST
            printf("Sent via HTTP polling: %s\n", message);
            return true;
            
        case TRANSPORT_OFFLINE:
            return queue_message(state, message);
    }
    
    return false;
}

// Monitor connection health
void monitor_connection(ConnectionState* state, const char* host, int port) {
    if (!state->connected) {
        printf("Connection lost, attempting reconnection...\n");
        connect_with_fallback(state, host, port);
    }
}

int main() {
    ConnectionState state;
    init_connection(&state);
    
    const char* host = "127.0.0.1";
    int port = 8080;
    
    // Initial connection attempt
    connect_with_fallback(&state, host, port);
    
    // Simulate sending messages
    send_message(&state, "Hello");
    send_message(&state, "World");
    
    // Simulate connection loss
    state.connected = false;
    close(state.socket_fd);
    
    // Try sending while disconnected
    send_message(&state, "Queued message 1");
    send_message(&state, "Queued message 2");
    
    // Reconnect and flush
    monitor_connection(&state, host, port);
    
    if (state.socket_fd >= 0) {
        close(state.socket_fd);
    }
    
    return 0;
}
```

---

## Rust Implementation

Here's a robust Rust implementation with advanced graceful degradation:

```rust
use std::time::{Duration, Instant};
use std::collections::VecDeque;
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::time::sleep;

#[derive(Debug, Clone, Copy, PartialEq)]
enum TransportMode {
    WebSocket,
    Polling,
    Offline,
}

#[derive(Debug)]
struct ConnectionConfig {
    max_retries: u32,
    initial_backoff: Duration,
    max_backoff: Duration,
    queue_capacity: usize,
    health_check_interval: Duration,
}

impl Default for ConnectionConfig {
    fn default() -> Self {
        Self {
            max_retries: 5,
            initial_backoff: Duration::from_secs(1),
            max_backoff: Duration::from_secs(32),
            queue_capacity: 100,
            health_check_interval: Duration::from_secs(30),
        }
    }
}

struct ConnectionState {
    mode: TransportMode,
    retry_count: u32,
    connected: bool,
    message_queue: VecDeque<String>,
    last_successful_send: Option<Instant>,
    config: ConnectionConfig,
}

impl ConnectionState {
    fn new(config: ConnectionConfig) -> Self {
        Self {
            mode: TransportMode::WebSocket,
            retry_count: 0,
            connected: false,
            message_queue: VecDeque::with_capacity(config.queue_capacity),
            last_successful_send: None,
            config,
        }
    }

    fn calculate_backoff(&self) -> Duration {
        let exponential = self.config.initial_backoff
            .as_secs()
            .saturating_mul(2u64.saturating_pow(self.retry_count));
        
        Duration::from_secs(exponential.min(self.config.max_backoff.as_secs()))
    }

    fn queue_message(&mut self, message: String) -> Result<(), String> {
        if self.message_queue.len() >= self.config.queue_capacity {
            // Remove oldest message
            self.message_queue.pop_front();
            println!("Queue full, dropped oldest message");
        }
        
        self.message_queue.push_back(message.clone());
        println!("Queued message: {} (queue size: {})", 
                 message, self.message_queue.len());
        Ok(())
    }

    fn flush_queue(&mut self) -> Vec<String> {
        let messages: Vec<String> = self.message_queue.drain(..).collect();
        println!("Flushing {} queued messages", messages.len());
        messages
    }

    fn mark_degraded(&mut self) {
        self.connected = false;
        self.retry_count += 1;
        println!("Connection degraded, retry count: {}", self.retry_count);
    }

    fn mark_healthy(&mut self) {
        self.connected = true;
        self.retry_count = 0;
        self.last_successful_send = Some(Instant::now());
        println!("Connection healthy");
    }
}

struct WebSocketClient {
    state: ConnectionState,
    host: String,
    port: u16,
}

impl WebSocketClient {
    fn new(host: String, port: u16, config: ConnectionConfig) -> Self {
        Self {
            state: ConnectionState::new(config),
            host,
            port,
        }
    }

    async fn try_websocket_connect(&mut self) -> Result<TcpStream, Box<dyn std::error::Error>> {
        println!("Attempting WebSocket connection to {}:{}", self.host, self.port);
        
        let stream = tokio::time::timeout(
            Duration::from_secs(5),
            TcpStream::connect(format!("{}:{}", self.host, self.port))
        ).await??;

        // Send WebSocket upgrade request
        let mut stream = stream;
        let upgrade_request = format!(
            "GET / HTTP/1.1\r\n\
             Host: {}:{}\r\n\
             Upgrade: websocket\r\n\
             Connection: Upgrade\r\n\
             Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\
             Sec-WebSocket-Version: 13\r\n\r\n",
            self.host, self.port
        );

        stream.write_all(upgrade_request.as_bytes()).await?;

        // Read upgrade response
        let mut buffer = vec![0u8; 4096];
        let n = stream.read(&mut buffer).await?;
        let response = String::from_utf8_lossy(&buffer[..n]);

        if !response.contains("101 Switching Protocols") {
            return Err("WebSocket upgrade failed".into());
        }

        println!("WebSocket connection established");
        self.state.mode = TransportMode::WebSocket;
        self.state.mark_healthy();

        Ok(stream)
    }

    async fn fallback_to_polling(&mut self) {
        println!("Falling back to HTTP polling mode");
        self.state.mode = TransportMode::Polling;
        self.state.connected = true;
        self.state.retry_count = 0;
    }

    async fn connect_with_fallback(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        // Try WebSocket with retries
        while self.state.retry_count < self.state.config.max_retries {
            match self.try_websocket_connect().await {
                Ok(_) => {
                    // Flush queued messages
                    let messages = self.state.flush_queue();
                    for msg in messages {
                        // Send queued messages (simplified)
                        println!("Sending queued: {}", msg);
                    }
                    return Ok(());
                }
                Err(e) => {
                    let backoff = self.state.calculate_backoff();
                    println!("WebSocket connection failed: {}. Retrying in {:?} (attempt {}/{})",
                             e, backoff, self.state.retry_count + 1, 
                             self.state.config.max_retries);
                    
                    self.state.mark_degraded();
                    sleep(backoff).await;
                }
            }
        }

        // All retries exhausted, fallback to polling
        println!("WebSocket failed after {} retries, using fallback", 
                 self.state.config.max_retries);
        self.fallback_to_polling().await;
        Ok(())
    }

    async fn send_message(&mut self, message: String) -> Result<(), Box<dyn std::error::Error>> {
        if !self.state.connected {
            return self.state.queue_message(message)
                .map_err(|e| e.into());
        }

        match self.state.mode {
            TransportMode::WebSocket => {
                // Attempt WebSocket send (simplified)
                println!("Sent via WebSocket: {}", message);
                self.state.mark_healthy();
                Ok(())
            }
            TransportMode::Polling => {
                // Simulate HTTP polling
                println!("Sent via HTTP polling: {}", message);
                self.state.last_successful_send = Some(Instant::now());
                Ok(())
            }
            TransportMode::Offline => {
                self.state.queue_message(message)?;
                Ok(())
            }
        }
    }

    async fn health_check(&mut self) {
        if let Some(last_send) = self.state.last_successful_send {
            let elapsed = last_send.elapsed();
            if elapsed > self.state.config.health_check_interval {
                println!("Health check: No activity for {:?}, checking connection", elapsed);
                
                if self.state.mode == TransportMode::WebSocket && !self.state.connected {
                    println!("Connection appears unhealthy, attempting reconnection");
                    let _ = self.connect_with_fallback().await;
                }
            }
        }
    }

    fn get_stats(&self) -> ConnectionStats {
        ConnectionStats {
            mode: self.state.mode,
            connected: self.state.connected,
            retry_count: self.state.retry_count,
            queued_messages: self.state.message_queue.len(),
        }
    }
}

#[derive(Debug)]
struct ConnectionStats {
    mode: TransportMode,
    connected: bool,
    retry_count: u32,
    queued_messages: usize,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = ConnectionConfig::default();
    let mut client = WebSocketClient::new(
        "127.0.0.1".to_string(),
        8080,
        config,
    );

    // Initial connection
    client.connect_with_fallback().await?;

    // Send some messages
    client.send_message("Hello from Rust!".to_string()).await?;
    client.send_message("WebSocket message".to_string()).await?;

    // Simulate connection loss
    client.state.connected = false;
    
    // Messages while disconnected will be queued
    client.send_message("Queued message 1".to_string()).await?;
    client.send_message("Queued message 2".to_string()).await?;

    println!("Connection stats: {:?}", client.get_stats());

    // Reconnect and flush queue
    client.connect_with_fallback().await?;

    // Periodic health check
    client.health_check().await;

    println!("Final stats: {:?}", client.get_stats());

    Ok(())
}
```

---

## Summary

**Graceful degradation** in WebSocket programming ensures applications remain functional even when optimal conditions fail. Key strategies include:

### Core Principles:
1. **Multiple Transport Layers**: WebSocket → HTTP Polling → Offline mode
2. **Exponential Backoff**: Prevents overwhelming failed servers with retry attempts
3. **Message Queuing**: Preserves data during outages for later delivery
4. **Health Monitoring**: Proactive detection of connection degradation
5. **Transparent Fallback**: Seamless transition between transport modes

### Implementation Highlights:

**C/C++**: Demonstrates low-level socket handling with manual retry logic, queue management, and transport switching using system calls.

**Rust**: Provides async/await patterns with tokio, type-safe state management, and sophisticated error handling with automatic reconnection strategies.

Both implementations showcase production-ready patterns for maintaining service continuity, ensuring user experience isn't compromised by network instability or server failures. The key is detecting failures early, maintaining state, and degrading gracefully rather than failing catastrophically.