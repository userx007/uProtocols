# WebSocket Debugging Tools and Techniques

## Overview

Debugging WebSocket applications requires specialized tools and techniques because WebSocket traffic involves persistent bidirectional connections, frame-level protocols, and asynchronous messaging patterns. Traditional HTTP debugging approaches often fall short when dealing with WebSocket's unique characteristics like connection upgrades, ping/pong frames, and continuous data streams.

## Key Debugging Tools

### 1. **Wireshark - Network Protocol Analyzer**

Wireshark excels at capturing and analyzing WebSocket traffic at the packet level, showing the complete handshake and frame-by-frame communication.

**Key Features for WebSocket Debugging:**
- Captures the HTTP upgrade handshake
- Decodes WebSocket frames (text, binary, control frames)
- Shows ping/pong heartbeat mechanisms
- Reveals fragmentation and masking details

**Wireshark Display Filters:**
```
# Filter WebSocket traffic
websocket

# Filter by specific WebSocket opcode
websocket.opcode == 1  # Text frames
websocket.opcode == 2  # Binary frames
websocket.opcode == 8  # Close frames
websocket.opcode == 9  # Ping frames
websocket.opcode == 10 # Pong frames

# Filter by port (common WebSocket ports)
tcp.port == 8080 || tcp.port == 443

# Show only WebSocket upgrade handshakes
http.upgrade == "websocket"
```

### 2. **strace - System Call Tracer**

`strace` traces system calls and signals, useful for debugging low-level socket operations, connection issues, and performance bottlenecks.

**Common strace Usage:**
```bash
# Trace all system calls for a WebSocket server
strace -f ./websocket_server

# Trace only network-related calls
strace -e trace=network ./websocket_server

# Trace socket operations with timestamps
strace -tt -e trace=socket,connect,send,recv,close ./websocket_client

# Save trace to file for analysis
strace -o trace.log -e trace=network,file ./websocket_server

# Show time spent in each system call
strace -c ./websocket_server
```

### 3. **GDB - GNU Debugger**

GDB provides source-level debugging for C/C++ WebSocket applications, allowing breakpoints, variable inspection, and step-through execution.

### 4. **Rust Debuggers**

Rust applications can be debugged with GDB/LLDB, but Rust-specific tools provide better integration with Rust's type system and ownership model.

---

## C/C++ WebSocket Debugging Examples

### Example 1: Basic WebSocket Server with Debug Logging

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <time.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define DEBUG_LOG(fmt, ...) \
    do { \
        time_t now = time(NULL); \
        char timebuf[26]; \
        ctime_r(&now, timebuf); \
        timebuf[24] = '\0'; \
        fprintf(stderr, "[%s] DEBUG: " fmt "\n", timebuf, ##__VA_ARGS__); \
    } while(0)

// Base64 encode function
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    char *result = (char*)malloc(bufferPtr->length + 1);
    memcpy(result, bufferPtr->data, bufferPtr->length);
    result[bufferPtr->length] = '\0';
    
    BIO_free_all(bio);
    return result;
}

// Generate WebSocket accept key
char* generate_accept_key(const char* client_key) {
    DEBUG_LOG("Generating accept key for client key: %s", client_key);
    
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", client_key, magic);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)concat, strlen(concat), hash);
    
    char* accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);
    DEBUG_LOG("Generated accept key: %s", accept_key);
    return accept_key;
}

// Parse WebSocket frame
int parse_ws_frame(unsigned char* buffer, int len, char* payload) {
    DEBUG_LOG("Parsing WebSocket frame of length: %d", len);
    
    if (len < 2) {
        DEBUG_LOG("Frame too short: %d bytes", len);
        return -1;
    }
    
    unsigned char fin = (buffer[0] & 0x80) >> 7;
    unsigned char opcode = buffer[0] & 0x0F;
    unsigned char masked = (buffer[1] & 0x80) >> 7;
    unsigned long long payload_len = buffer[1] & 0x7F;
    
    DEBUG_LOG("FIN: %d, Opcode: %d, Masked: %d, Payload len: %llu", 
              fin, opcode, masked, payload_len);
    
    int offset = 2;
    
    if (payload_len == 126) {
        payload_len = (buffer[2] << 8) | buffer[3];
        offset = 4;
        DEBUG_LOG("Extended payload length (16-bit): %llu", payload_len);
    } else if (payload_len == 127) {
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | buffer[2 + i];
        }
        offset = 10;
        DEBUG_LOG("Extended payload length (64-bit): %llu", payload_len);
    }
    
    unsigned char mask[4];
    if (masked) {
        memcpy(mask, buffer + offset, 4);
        offset += 4;
        DEBUG_LOG("Masking key: %02x %02x %02x %02x", 
                  mask[0], mask[1], mask[2], mask[3]);
    }
    
    // Unmask payload
    for (unsigned long long i = 0; i < payload_len; i++) {
        payload[i] = buffer[offset + i] ^ mask[i % 4];
    }
    payload[payload_len] = '\0';
    
    DEBUG_LOG("Decoded payload: %s", payload);
    
    // Handle control frames
    if (opcode == 0x8) {
        DEBUG_LOG("Close frame received");
        return -2;
    } else if (opcode == 0x9) {
        DEBUG_LOG("Ping frame received");
        return -3;
    } else if (opcode == 0xA) {
        DEBUG_LOG("Pong frame received");
        return -4;
    }
    
    return payload_len;
}

// Create WebSocket frame
int create_ws_frame(const char* payload, unsigned char* frame) {
    int payload_len = strlen(payload);
    int frame_len = 0;
    
    DEBUG_LOG("Creating WebSocket frame for payload: %s", payload);
    
    frame[0] = 0x81; // FIN + text frame
    
    if (payload_len <= 125) {
        frame[1] = payload_len;
        frame_len = 2;
    } else if (payload_len <= 65535) {
        frame[1] = 126;
        frame[2] = (payload_len >> 8) & 0xFF;
        frame[3] = payload_len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
        }
        frame_len = 10;
    }
    
    memcpy(frame + frame_len, payload, payload_len);
    frame_len += payload_len;
    
    DEBUG_LOG("Created frame of %d bytes", frame_len);
    return frame_len;
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    
    DEBUG_LOG("New client connected, socket fd: %d", client_sock);
    
    // Read HTTP upgrade request
    bytes_read = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        DEBUG_LOG("Failed to read upgrade request: %s", strerror(errno));
        close(client_sock);
        return;
    }
    buffer[bytes_read] = '\0';
    
    DEBUG_LOG("Received upgrade request:\n%s", buffer);
    
    // Extract Sec-WebSocket-Key
    char* key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        DEBUG_LOG("Missing Sec-WebSocket-Key header");
        close(client_sock);
        return;
    }
    
    key_start += strlen("Sec-WebSocket-Key: ");
    char* key_end = strstr(key_start, "\r\n");
    char client_key[256];
    strncpy(client_key, key_start, key_end - key_start);
    client_key[key_end - key_start] = '\0';
    
    // Generate response
    char* accept_key = generate_accept_key(client_key);
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);
    
    DEBUG_LOG("Sending upgrade response:\n%s", response);
    send(client_sock, response, strlen(response), 0);
    free(accept_key);
    
    // Handle WebSocket frames
    while (1) {
        bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            DEBUG_LOG("Connection closed or error: %s", 
                      bytes_read == 0 ? "EOF" : strerror(errno));
            break;
        }
        
        DEBUG_LOG("Received %d bytes", bytes_read);
        
        char payload[BUFFER_SIZE];
        int result = parse_ws_frame((unsigned char*)buffer, bytes_read, payload);
        
        if (result == -2) {
            DEBUG_LOG("Client requested close");
            break;
        } else if (result == -3) {
            DEBUG_LOG("Responding to ping with pong");
            unsigned char pong[2] = {0x8A, 0x00}; // Pong frame
            send(client_sock, pong, 2, 0);
        } else if (result > 0) {
            DEBUG_LOG("Echoing message back to client");
            unsigned char frame[BUFFER_SIZE];
            int frame_len = create_ws_frame(payload, frame);
            send(client_sock, frame, frame_len, 0);
        }
    }
    
    DEBUG_LOG("Closing client connection");
    close(client_sock);
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    DEBUG_LOG("Starting WebSocket server on port %d", PORT);
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    DEBUG_LOG("Server listening, waiting for connections...");
    
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            DEBUG_LOG("Accept failed: %s", strerror(errno));
            continue;
        }
        
        DEBUG_LOG("Accepted connection from %s:%d",
                  inet_ntoa(client_addr.sin_addr),
                  ntohs(client_addr.sin_port));
        
        handle_client(client_sock);
    }
    
    close(server_sock);
    return 0;
}
```

### Example 2: Using GDB with WebSocket Applications

**GDB Debugging Commands:**
```bash
# Compile with debug symbols
gcc -g -o ws_server websocket_server.c -lssl -lcrypto

# Start with GDB
gdb ./ws_server

# Useful GDB commands for WebSocket debugging:
(gdb) break parse_ws_frame          # Break at frame parsing
(gdb) break send                     # Break at send syscall
(gdb) run                            # Start program
(gdb) bt                             # Backtrace
(gdb) print buffer                   # Print buffer contents
(gdb) x/32xb buffer                  # Examine buffer as hex bytes
(gdb) watch payload_len              # Watch variable changes
(gdb) info threads                   # List threads
(gdb) thread 2                       # Switch to thread 2
(gdb) continue                       # Continue execution

# Advanced: conditional breakpoint
(gdb) break parse_ws_frame if opcode == 0x8

# Attach to running process
gdb -p $(pidof ws_server)

# Remote debugging
gdbserver :1234 ./ws_server          # On target machine
gdb ./ws_server                       # On development machine
(gdb) target remote target_ip:1234
```

---

## Rust WebSocket Debugging Examples

### Example 1: Rust WebSocket Server with Tracing

```rust
// Cargo.toml dependencies:
// [dependencies]
// tokio = { version = "1", features = ["full"] }
// tokio-tungstenite = "0.21"
// tracing = "0.1"
// tracing-subscriber = { version = "0.3", features = ["env-filter"] }
// futures-util = "0.3"

use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tracing::{debug, error, info, warn, instrument, Level};
use tracing_subscriber::{fmt, layer::SubscriberExt, util::SubscriberInitExt};
use std::net::SocketAddr;

#[instrument(skip(stream))]
async fn handle_connection(stream: TcpStream, addr: SocketAddr) {
    info!("New connection attempt from {}", addr);
    
    // Perform WebSocket handshake
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => {
            info!("WebSocket handshake successful with {}", addr);
            ws
        }
        Err(e) => {
            error!("WebSocket handshake failed with {}: {:?}", addr, e);
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    debug!("Split WebSocket stream for {}", addr);
    
    // Message counter for debugging
    let mut msg_count = 0u64;
    
    while let Some(msg) = read.next().await {
        match msg {
            Ok(msg) => {
                msg_count += 1;
                
                match &msg {
                    Message::Text(text) => {
                        debug!(
                            count = msg_count,
                            length = text.len(),
                            "Received text message from {}: {}",
                            addr,
                            text
                        );
                        
                        // Echo back with debug info
                        let response = format!("Echo #{}: {}", msg_count, text);
                        
                        if let Err(e) = write.send(Message::Text(response)).await {
                            error!("Failed to send response to {}: {:?}", addr, e);
                            break;
                        }
                        
                        debug!("Successfully echoed message #{} to {}", msg_count, addr);
                    }
                    
                    Message::Binary(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received binary message from {}",
                            addr
                        );
                        
                        // Log first few bytes for debugging
                        let preview: Vec<String> = data.iter()
                            .take(16)
                            .map(|b| format!("{:02x}", b))
                            .collect();
                        debug!("Binary data preview: {}", preview.join(" "));
                        
                        if let Err(e) = write.send(Message::Binary(data.clone())).await {
                            error!("Failed to send binary response to {}: {:?}", addr, e);
                            break;
                        }
                    }
                    
                    Message::Ping(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received ping from {}",
                            addr
                        );
                        
                        if let Err(e) = write.send(Message::Pong(data.clone())).await {
                            error!("Failed to send pong to {}: {:?}", addr, e);
                            break;
                        }
                        
                        debug!("Sent pong response to {}", addr);
                    }
                    
                    Message::Pong(data) => {
                        debug!(
                            count = msg_count,
                            length = data.len(),
                            "Received pong from {}",
                            addr
                        );
                    }
                    
                    Message::Close(frame) => {
                        if let Some(cf) = frame {
                            info!(
                                code = cf.code.into(),
                                reason = %cf.reason,
                                "Received close frame from {}",
                                addr
                            );
                        } else {
                            info!("Received close frame (no details) from {}", addr);
                        }
                        
                        debug!("Sending close acknowledgment to {}", addr);
                        let _ = write.send(Message::Close(None)).await;
                        break;
                    }
                    
                    Message::Frame(_) => {
                        warn!("Received raw frame from {} (unexpected)", addr);
                    }
                }
            }
            Err(e) => {
                error!("Error receiving message from {}: {:?}", addr, e);
                break;
            }
        }
    }
    
    info!(
        total_messages = msg_count,
        "Connection closed with {}",
        addr
    );
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing with environment filter
    // Set RUST_LOG environment variable to control log level
    // Examples:
    //   RUST_LOG=debug cargo run
    //   RUST_LOG=websocket_server=trace cargo run
    tracing_subscriber::registry()
        .with(fmt::layer().with_target(true).with_thread_ids(true))
        .with(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "debug".into())
        )
        .init();
    
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    
    info!("WebSocket server listening on {}", addr);
    info!("Set RUST_LOG environment variable to control verbosity");
    info!("Example: RUST_LOG=trace cargo run");
    
    while let Ok((stream, addr)) = listener.accept().await {
        debug!("Accepted TCP connection from {}", addr);
        
        // Spawn a new task for each connection
        tokio::spawn(async move {
            handle_connection(stream, addr).await;
        });
    }
    
    Ok(())
}

// Example client for testing
#[cfg(test)]
mod tests {
    use super::*;
    use tokio_tungstenite::connect_async;
    
    #[tokio::test]
    async fn test_websocket_echo() {
        // This would connect to a running server
        let url = "ws://127.0.0.1:8080";
        
        match connect_async(url).await {
            Ok((mut ws_stream, _)) => {
                info!("Test client connected");
                
                // Send test message
                ws_stream.send(Message::Text("Hello".into())).await.unwrap();
                
                // Receive echo
                if let Some(msg) = ws_stream.next().await {
                    match msg {
                        Ok(Message::Text(text)) => {
                            debug!("Received: {}", text);
                            assert!(text.contains("Hello"));
                        }
                        _ => panic!("Expected text message"),
                    }
                }
            }
            Err(e) => {
                error!("Connection failed: {:?}", e);
            }
        }
    }
}
```

### Example 2: Advanced Rust Debugging with Custom Instrumentation

```rust
// Advanced debugging features for WebSocket applications
// Cargo.toml dependencies:
// [dependencies]
// tokio = { version = "1", features = ["full", "tracing"] }
// tokio-tungstenite = "0.21"
// tracing = "0.1"
// tracing-subscriber = { version = "0.3", features = ["env-filter", "json"] }
// serde = { version = "1", features = ["derive"] }
// serde_json = "1"

use tokio::net::TcpListener;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tracing::{debug, error, info, warn, span, Level};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};
use serde::{Deserialize, Serialize};

/// Connection statistics for debugging
#[derive(Debug, Clone, Serialize)]
struct ConnectionStats {
    messages_received: u64,
    messages_sent: u64,
    bytes_received: u64,
    bytes_sent: u64,
    errors: u64,
    connection_duration: Duration,
}

impl ConnectionStats {
    fn new() -> Self {
        Self {
            messages_received: 0,
            messages_sent: 0,
            bytes_received: 0,
            bytes_sent: 0,
            errors: 0,
            connection_duration: Duration::from_secs(0),
        }
    }
}

/// Global server statistics
struct ServerStats {
    total_connections: AtomicU64,
    active_connections: AtomicU64,
    total_messages: AtomicU64,
}

impl ServerStats {
    fn new() -> Self {
        Self {
            total_connections: AtomicU64::new(0),
            active_connections: AtomicU64::new(0),
            total_messages: AtomicU64::new(0),
        }
    }
    
    fn connection_opened(&self) {
        self.total_connections.fetch_add(1, Ordering::Relaxed);
        self.active_connections.fetch_add(1, Ordering::Relaxed);
    }
    
    fn connection_closed(&self) {
        self.active_connections.fetch_sub(1, Ordering::Relaxed);
    }
    
    fn message_received(&self) {
        self.total_messages.fetch_add(1, Ordering::Relaxed);
    }
    
    fn log_stats(&self) {
        info!(
            total_connections = self.total_connections.load(Ordering::Relaxed),
            active_connections = self.active_connections.load(Ordering::Relaxed),
            total_messages = self.total_messages.load(Ordering::Relaxed),
            "Server statistics"
        );
    }
}

/// Debug wrapper for WebSocket messages
#[derive(Debug, Serialize)]
struct MessageDebugInfo {
    #[serde(rename = "type")]
    msg_type: String,
    size: usize,
    timestamp: String,
    payload_preview: String,
}

impl MessageDebugInfo {
    fn from_message(msg: &Message) -> Self {
        let msg_type = match msg {
            Message::Text(_) => "text",
            Message::Binary(_) => "binary",
            Message::Ping(_) => "ping",
            Message::Pong(_) => "pong",
            Message::Close(_) => "close",
            Message::Frame(_) => "frame",
        }.to_string();
        
        let (size, payload_preview) = match msg {
            Message::Text(text) => {
                let preview = if text.len() > 50 {
                    format!("{}...", &text[..50])
                } else {
                    text.clone()
                };
                (text.len(), preview)
            }
            Message::Binary(data) => {
                let preview: Vec<String> = data.iter()
                    .take(16)
                    .map(|b| format!("{:02x}", b))
                    .collect();
                (data.len(), format!("[{}]", preview.join(" ")))
            }
            Message::Ping(data) | Message::Pong(data) => {
                (data.len(), format!("{} bytes", data.len()))
            }
            Message::Close(frame) => {
                let preview = frame.as_ref()
                    .map(|f| format!("code: {}, reason: {}", f.code, f.reason))
                    .unwrap_or_else(|| "no details".to_string());
                (0, preview)
            }
            Message::Frame(_) => (0, "raw frame".to_string()),
        };
        
        Self {
            msg_type,
            size,
            timestamp: chrono::Utc::now().to_rfc3339(),
            payload_preview,
        }
    }
}

async fn handle_connection_with_stats(
    stream: tokio::net::TcpStream,
    addr: std::net::SocketAddr,
    server_stats: Arc<ServerStats>,
) {
    let conn_span = span!(Level::INFO, "connection", %addr);
    let _enter = conn_span.enter();
    
    let start_time = Instant::now();
    let mut stats = ConnectionStats::new();
    
    server_stats.connection_opened();
    info!("Connection opened");
    
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            error!(error = ?e, "Handshake failed");
            server_stats.connection_closed();
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn periodic stats logger
    let stats_task = {
        let addr = addr;
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(30));
            loop {
                interval.tick().await;
                debug!(
                    messages_rx = stats.messages_received,
                    messages_tx = stats.messages_sent,
                    bytes_rx = stats.bytes_received,
                    bytes_tx = stats.bytes_sent,
                    errors = stats.errors,
                    "Periodic stats for {}",
                    addr
                );
            }
        })
    };
    
    while let Some(msg_result) = read.next().await {
        match msg_result {
            Ok(msg) => {
                // Log detailed message information
                let debug_info = MessageDebugInfo::from_message(&msg);
                debug!(
                    msg_type = %debug_info.msg_type,
                    size = debug_info.size,
                    preview = %debug_info.payload_preview,
                    "Received message"
                );
                
                stats.messages_received += 1;
                server_stats.message_received();
                
                match &msg {
                    Message::Text(text) => {
                        stats.bytes_received += text.len() as u64;
                        
                        // Echo with debug metadata
                        let response = Message::Text(format!(
                            "{{\"echo\":\"{}\",\"stats\":{}}}",
                            text,
                            serde_json::to_string(&stats).unwrap_or_default()
                        ));
                        
                        if let Err(e) = write.send(response).await {
                            error!(error = ?e, "Send failed");
                            stats.errors += 1;
                            break;
                        }
                        
                        stats.messages_sent += 1;
                        stats.bytes_sent += text.len() as u64;
                    }
                    
                    Message::Binary(data) => {
                        stats.bytes_received += data.len() as u64;
                        
                        if let Err(e) = write.send(Message::Binary(data.clone())).await {
                            error!(error = ?e, "Binary send failed");
                            stats.errors += 1;
                            break;
                        }
                        
                        stats.messages_sent += 1;
                        stats.bytes_sent += data.len() as u64;
                    }
                    
                    Message::Ping(data) => {
                        debug!("Responding to ping");
                        if let Err(e) = write.send(Message::Pong(data.clone())).await {
                            error!(error = ?e, "Pong send failed");
                            stats.errors += 1;
                        }
                    }
                    
                    Message::Close(frame) => {
                        if let Some(cf) = frame {
                            info!(code = cf.code.into(), reason = %cf.reason, "Close received");
                        }
                        let _ = write.send(Message::Close(None)).await;
                        break;
                    }
                    
                    _ => {}
                }
            }
            Err(e) => {
                error!(error = ?e, "Message receive error");
                stats.errors += 1;
                break;
            }
        }
    }
    
    stats_task.abort();
    
    stats.connection_duration = start_time.elapsed();
    server_stats.connection_closed();
    
    info!(
        duration_secs = stats.connection_duration.as_secs(),
        messages_rx = stats.messages_received,
        messages_tx = stats.messages_sent,
        bytes_rx = stats.bytes_received,
        bytes_tx = stats.bytes_sent,
        errors = stats.errors,
        "Connection closed"
    );
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize JSON tracing for structured logging
    tracing_subscriber::fmt()
        .with_target(true)
        .with_thread_ids(true)
        .with_file(true)
        .with_line_number(true)
        .json()
        .init();
    
    let server_stats = Arc::new(ServerStats::new());
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    
    info!(address = %addr, "Server started");
    
    // Spawn periodic server stats logger
    let stats_clone = Arc::clone(&server_stats);
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(60));
        loop {
            interval.tick().await;
            stats_clone.log_stats();
        }
    });
    
    while let Ok((stream, addr)) = listener.accept().await {
        let stats = Arc::clone(&server_stats);
        tokio::spawn(async move {
            handle_connection_with_stats(stream, addr, stats).await;
        });
    }
    
    Ok(())
}
```

## Debugging Rust WebSocket Applications

### Using LLDB/GDB with Rust:

```bash
# Build with debug symbols
cargo build

# Debug with rust-lldb (macOS/Linux)
rust-lldb ./target/debug/websocket_server

# Debug with rust-gdb (Linux)
rust-gdb ./target/debug/websocket_server

# Common LLDB/GDB commands for Rust:
(lldb) breakpoint set -n handle_connection
(lldb) run
(lldb) bt          # Backtrace
(lldb) frame variable  # Show local variables
(lldb) print msg   # Print variable
(lldb) continue

# Set environment variables for logging
(lldb) settings set target.env-vars RUST_LOG=debug
```

### Using Rust-Specific Tools:

**1. cargo-expand** - See macro expansions
```bash
cargo install cargo-expand
cargo expand
```

**2. cargo-flamegraph** - Profile WebSocket performance
```bash
cargo install flamegraph
cargo flamegraph --bin websocket_server
```

**3. tokio-console** - Monitor async runtime
```bash
# Add to Cargo.toml:
# console-subscriber = "0.1"

# In code:
console_subscriber::init();

# Run console
tokio-console
```

---

## Comparative Debugging Workflow

### 1. **Network Layer (Wireshark)**
```
Use when: Investigating connection issues, handshake problems, or frame-level protocol errors
Look for: 
- HTTP 101 Switching Protocols response
- Proper Sec-WebSocket-Accept header
- Masked vs unmasked frames
- Unexpected connection resets
```

### 2. **System Call Layer (strace)**
```bash
# Common patterns to investigate:
strace -e trace=network -o trace.log ./ws_server

# Look for:
# - EINVAL, ECONNRESET, EPIPE errors
# - Blocked recv/send calls
# - Unexpected socket closures
# - Resource exhaustion (EMFILE)
```

### 3. **Application Layer (GDB/LLDB)**
```
Use when: Logic errors, crashes, memory issues
Set breakpoints at:
- Message parsing functions
- Frame creation functions  
- Error handling paths
- State transitions
```

### 4. **Async Runtime (Rust-specific)**
```bash
# Enable tokio tracing
RUSTFLAGS="--cfg tokio_unstable" cargo build

# Use tokio-console for:
# - Task blocking detection
# - Async task statistics
# - Future polling patterns
```

---

## Common WebSocket Issues and Debug Strategies

### Issue 1: Connection Drops Unexpectedly

**Debug approach:**
1. **Wireshark**: Check for close frames, look at close codes
2. **strace**: Look for `EPIPE`, `ECONNRESET` errors  
3. **Application logs**: Check ping/pong mechanism
4. **GDB**: Set breakpoint in error handlers

**C Debug:**
```c
DEBUG_LOG("Connection error: %s (errno: %d)", strerror(errno), errno);
```

**Rust Debug:**
```rust
error!(error = ?e, errno = std::io::Error::last_os_error(), "Connection failed");
```

### Issue 2: Messages Not Being Received

**Debug approach:**
1. **Wireshark**: Verify frames are arriving, check masking
2. **Application**: Log buffer contents in hex
3. **GDB**: Examine frame parsing logic step-by-step

**Hex dump in C:**
```c
void hex_dump(const unsigned char* data, int len) {
    for (int i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", data[i]);
        if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}
```

### Issue 3: Performance Bottlenecks

**Debug approach:**
1. **strace -c**: Time spent in syscalls
2. **perf/flamegraph**: CPU profiling
3. **Wireshark**: Look for bufferbloat, delayed ACKs

---

## Summary

**Debugging WebSocket applications requires a multi-layered approach:**

### Tool Selection Matrix

| Issue Type | Primary Tool | Secondary Tools |
|------------|-------------|-----------------|
| Handshake failures | Wireshark | curl, browser DevTools |
| Connection drops | Wireshark + strace | Application logs |
| Message corruption | Wireshark + GDB | Hex dumps |
| Performance issues | strace -c, perf | tokio-console (Rust) |
| Logic errors | GDB/LLDB | Unit tests |
| Async/concurrency | tokio-console | Thread debugging in GDB |

### Best Practices

1. **Use structured logging** - Timestamp, severity, context
2. **Instrument frame parsing** - Log opcodes, lengths, masking
3. **Track connection lifecycle** - Open, close, error states
4. **Monitor system resources** - File descriptors, memory
5. **Capture network traffic** - Keep pcap files for post-mortem
6. **Enable debug symbols** - Always compile with `-g` during development
7. **Use async-aware tools** - tokio-console for Rust async debugging

### C/C++ Debugging Strengths
- Direct system call visibility with strace
- Fine-grained memory inspection with GDB
- Minimal overhead for production debugging

### Rust Debugging Strengths
- Structured tracing with spans and fields
- Async runtime introspection (tokio-console)
- Type-safe logging and error handling
- Zero-cost abstractions maintain performance while debugging

The combination of Wireshark for protocol analysis, strace for system-level debugging, GDB/LLDB for source-level debugging, and language-specific tools (like Rust's tracing ecosystem) provides comprehensive coverage for troubleshooting WebSocket applications at every layer of the stack.