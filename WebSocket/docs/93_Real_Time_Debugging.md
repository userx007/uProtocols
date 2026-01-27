# Real-Time Debugging of WebSocket Connections

## Overview

Real-time debugging of WebSocket connections involves monitoring, inspecting, and troubleshooting live bidirectional communication channels between clients and servers. Unlike traditional HTTP debugging, WebSocket debugging requires specialized tools and techniques to capture and analyze the persistent, full-duplex nature of the protocol.

## Key Concepts

### WebSocket Connection Lifecycle
WebSocket connections progress through several stages that require different debugging approaches:

1. **Handshake Phase**: HTTP upgrade request/response
2. **Open Connection**: Active bidirectional communication
3. **Message Exchange**: Frame-level data transfer
4. **Close Handshake**: Graceful connection termination
5. **Error States**: Connection failures and exceptions

### Common Debugging Scenarios

**Connection Issues**: Failed handshakes, authentication problems, network timeouts

**Message Problems**: Malformed frames, encoding issues, lost messages

**Performance Bottlenecks**: Slow message delivery, buffer overflows, memory leaks

**Protocol Violations**: Invalid frame types, improper close sequences

## Debugging Tools and Techniques

### 1. Browser Developer Tools
Modern browsers provide built-in WebSocket inspection capabilities through their Network tabs, showing handshake details, message frames, timing information, and close codes.

### 2. Wireshark and Network Sniffers
Packet capture tools allow deep inspection of WebSocket traffic at the TCP/IP level, useful for analyzing encrypted connections (with proper SSL key configuration) and network-level issues.

### 3. Logging and Instrumentation
Strategic logging at both client and server sides captures connection events, message payloads, and error conditions for post-mortem analysis.

### 4. Custom Debug Proxies
Proxy servers sitting between client and server can intercept, log, and modify WebSocket traffic in real-time.

## C/C++ Implementation

Here's a comprehensive example using libwebsockets with extensive debugging capabilities:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Debug levels
#define DEBUG_HANDSHAKE  (1 << 0)
#define DEBUG_MESSAGES   (1 << 1)
#define DEBUG_FRAMES     (1 << 2)
#define DEBUG_ERRORS     (1 << 3)
#define DEBUG_ALL        0xFF

static int debug_flags = DEBUG_ALL;

// Debug logging function with timestamps
void debug_log(int level, const char *format, ...) {
    if (!(debug_flags & level)) return;
    
    time_t now;
    char timestamp[64];
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] ", timestamp);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

// Connection state tracking
struct connection_debug_info {
    char peer_address[128];
    time_t connect_time;
    unsigned long messages_sent;
    unsigned long messages_received;
    unsigned long bytes_sent;
    unsigned long bytes_received;
    int last_ping_ms;
};

struct per_session_data {
    struct connection_debug_info debug_info;
    char message_buffer[4096];
    size_t message_len;
};

// Callback with comprehensive debugging
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct per_session_data *pss = (struct per_session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // Initialize debug tracking
            memset(&pss->debug_info, 0, sizeof(pss->debug_info));
            pss->debug_info.connect_time = time(NULL);
            
            // Get peer address
            char name[128], ip[128];
            lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi),
                                  name, sizeof(name), ip, sizeof(ip));
            snprintf(pss->debug_info.peer_address, sizeof(pss->debug_info.peer_address),
                    "%s (%s)", name, ip);
            
            debug_log(DEBUG_HANDSHAKE, "Connection established from %s",
                     pss->debug_info.peer_address);
            
            // Log protocol information
            debug_log(DEBUG_HANDSHAKE, "Protocol: %s",
                     lws_get_protocol(wsi)->name);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            debug_log(DEBUG_MESSAGES, "Received %zu bytes from %s",
                     len, pss->debug_info.peer_address);
            
            // Log frame details
            if (debug_flags & DEBUG_FRAMES) {
                int is_final = lws_is_final_fragment(wsi);
                int is_binary = lws_frame_is_binary(wsi);
                debug_log(DEBUG_FRAMES, "Frame: final=%d, binary=%d",
                         is_final, is_binary);
            }
            
            // Update statistics
            pss->debug_info.messages_received++;
            pss->debug_info.bytes_received += len;
            
            // Log message content (first 100 bytes for text)
            if (!lws_frame_is_binary(wsi) && len > 0) {
                char preview[101];
                size_t preview_len = len < 100 ? len : 100;
                memcpy(preview, in, preview_len);
                preview[preview_len] = '\0';
                debug_log(DEBUG_MESSAGES, "Content preview: %s%s",
                         preview, len > 100 ? "..." : "");
            }
            
            // Echo back with debugging
            memcpy(pss->message_buffer, in, len);
            pss->message_len = len;
            lws_callback_on_writable(wsi);
            break;
        }
        
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (pss->message_len > 0) {
                unsigned char buf[LWS_PRE + 4096];
                memcpy(&buf[LWS_PRE], pss->message_buffer, pss->message_len);
                
                debug_log(DEBUG_MESSAGES, "Sending %zu bytes to %s",
                         pss->message_len, pss->debug_info.peer_address);
                
                int written = lws_write(wsi, &buf[LWS_PRE], pss->message_len,
                                       LWS_WRITE_TEXT);
                
                if (written < 0) {
                    debug_log(DEBUG_ERRORS, "Write failed for %s",
                             pss->debug_info.peer_address);
                    return -1;
                }
                
                // Update statistics
                pss->debug_info.messages_sent++;
                pss->debug_info.bytes_sent += written;
                
                debug_log(DEBUG_MESSAGES, "Successfully sent %d bytes", written);
                pss->message_len = 0;
            }
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            time_t duration = time(NULL) - pss->debug_info.connect_time;
            debug_log(DEBUG_HANDSHAKE,
                     "Connection closed: %s\n"
                     "  Duration: %ld seconds\n"
                     "  Messages sent: %lu\n"
                     "  Messages received: %lu\n"
                     "  Bytes sent: %lu\n"
                     "  Bytes received: %lu",
                     pss->debug_info.peer_address,
                     duration,
                     pss->debug_info.messages_sent,
                     pss->debug_info.messages_received,
                     pss->debug_info.bytes_sent,
                     pss->debug_info.bytes_received);
            break;
        }
        
        case LWS_CALLBACK_WSI_DESTROY: {
            debug_log(DEBUG_HANDSHAKE, "WSI destroyed for %s",
                     pss->debug_info.peer_address);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE_PONG: {
            pss->debug_info.last_ping_ms = (int)(len);
            debug_log(DEBUG_MESSAGES, "Pong received from %s (latency: %d ms)",
                     pss->debug_info.peer_address, pss->debug_info.last_ping_ms);
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            debug_log(DEBUG_ERRORS, "Connection error: %s",
                     in ? (char *)in : "unknown");
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "debug-protocol",
        callback_websocket,
        sizeof(struct per_session_data),
        4096,
    },
    { NULL, NULL, 0, 0 }
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 9001;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    // Enable libwebsockets debugging
    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, NULL);
    
    debug_log(DEBUG_HANDSHAKE, "Starting WebSocket server on port %d", info.port);
    
    context = lws_create_context(&info);
    if (!context) {
        debug_log(DEBUG_ERRORS, "Failed to create context");
        return -1;
    }
    
    debug_log(DEBUG_HANDSHAKE, "Server started successfully");
    
    // Main event loop
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

### Advanced C++ Debugging with Beast (Boost.Asio)

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Debug logger with levels
class DebugLogger {
public:
    enum Level { DEBUG, INFO, WARN, ERROR };
    
    static void log(Level level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::cout << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                  << "] [" << levelToString(level) << "] " << message << std::endl;
    }
    
private:
    static std::string levelToString(Level level) {
        switch(level) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO";
            case WARN:  return "WARN";
            case ERROR: return "ERROR";
            default:    return "UNKNOWN";
        }
    }
};

class DebugSession : public std::enable_shared_from_this<DebugSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string peer_address_;
    std::chrono::steady_clock::time_point connect_time_;
    size_t messages_received_ = 0;
    size_t messages_sent_ = 0;
    
public:
    explicit DebugSession(tcp::socket&& socket)
        : ws_(std::move(socket)) {
        connect_time_ = std::chrono::steady_clock::now();
    }
    
    void run() {
        // Get peer address
        auto endpoint = ws_.next_layer().socket().remote_endpoint();
        peer_address_ = endpoint.address().to_string() + ":" + 
                       std::to_string(endpoint.port());
        
        DebugLogger::log(DebugLogger::INFO, 
            "New connection from " + peer_address_);
        
        // Set timeout and debugging options
        ws_.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server));
        
        ws_.set_option(websocket::stream_base::decorator(
            [this](websocket::response_type& res) {
                DebugLogger::log(DebugLogger::DEBUG,
                    "Sending handshake response to " + peer_address_);
                res.set(beast::http::field::server, "Debug-WebSocket-Server");
            }));
        
        // Accept the handshake
        ws_.async_accept(
            beast::bind_front_handler(
                &DebugSession::on_accept,
                shared_from_this()));
    }
    
private:
    void on_accept(beast::error_code ec) {
        if(ec) {
            DebugLogger::log(DebugLogger::ERROR,
                "Accept failed for " + peer_address_ + ": " + ec.message());
            return;
        }
        
        DebugLogger::log(DebugLogger::INFO,
            "Handshake completed for " + peer_address_);
        
        do_read();
    }
    
    void do_read() {
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &DebugSession::on_read,
                shared_from_this()));
    }
    
    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        if(ec == websocket::error::closed) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - connect_time_).count();
            
            std::stringstream ss;
            ss << "Connection closed: " << peer_address_ << "\n"
               << "  Duration: " << duration << " seconds\n"
               << "  Messages received: " << messages_received_ << "\n"
               << "  Messages sent: " << messages_sent_;
            
            DebugLogger::log(DebugLogger::INFO, ss.str());
            return;
        }
        
        if(ec) {
            DebugLogger::log(DebugLogger::ERROR,
                "Read error for " + peer_address_ + ": " + ec.message());
            return;
        }
        
        messages_received_++;
        
        // Log message details
        std::stringstream ss;
        ss << "Received message from " << peer_address_ << ": "
           << bytes_transferred << " bytes, "
           << (ws_.got_text() ? "text" : "binary");
        DebugLogger::log(DebugLogger::DEBUG, ss.str());
        
        // Log content preview for text messages
        if(ws_.got_text() && bytes_transferred > 0) {
            std::string preview = beast::buffers_to_string(buffer_.data());
            if(preview.length() > 100) {
                preview = preview.substr(0, 100) + "...";
            }
            DebugLogger::log(DebugLogger::DEBUG, "Content: " + preview);
        }
        
        // Echo the message back
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &DebugSession::on_write,
                shared_from_this()));
    }
    
    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        if(ec) {
            DebugLogger::log(DebugLogger::ERROR,
                "Write error for " + peer_address_ + ": " + ec.message());
            return;
        }
        
        messages_sent_++;
        
        DebugLogger::log(DebugLogger::DEBUG,
            "Sent " + std::to_string(bytes_transferred) + " bytes to " + 
            peer_address_);
        
        buffer_.consume(buffer_.size());
        do_read();
    }
};
```

## Rust Implementation

Here's a comprehensive Rust example using tokio-tungstenite with extensive debugging:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;
use std::time::{SystemTime, Instant};
use tracing::{info, debug, warn, error, Level};
use tracing_subscriber;

#[derive(Debug, Clone)]
struct ConnectionStats {
    peer_addr: String,
    connect_time: SystemTime,
    start_instant: Instant,
    messages_sent: usize,
    messages_received: usize,
    bytes_sent: usize,
    bytes_received: usize,
}

impl ConnectionStats {
    fn new(peer_addr: String) -> Self {
        Self {
            peer_addr,
            connect_time: SystemTime::now(),
            start_instant: Instant::now(),
            messages_sent: 0,
            messages_received: 0,
            bytes_sent: 0,
            bytes_received: 0,
        }
    }
    
    fn log_summary(&self) {
        let duration = self.start_instant.elapsed();
        info!(
            peer = %self.peer_addr,
            duration_secs = duration.as_secs(),
            messages_sent = self.messages_sent,
            messages_received = self.messages_received,
            bytes_sent = self.bytes_sent,
            bytes_received = self.bytes_received,
            "Connection summary"
        );
    }
}

async fn handle_connection(stream: TcpStream, peer_addr: String) {
    info!(peer = %peer_addr, "New connection attempt");
    
    let mut stats = ConnectionStats::new(peer_addr.clone());
    
    // Accept WebSocket handshake
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => {
            info!(peer = %peer_addr, "WebSocket handshake completed");
            ws
        }
        Err(e) => {
            error!(peer = %peer_addr, error = %e, "Handshake failed");
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    info!(peer = %peer_addr, "Connection established, starting message loop");
    
    // Message processing loop
    while let Some(message) = read.next().await {
        match message {
            Ok(msg) => {
                // Log message details
                match &msg {
                    Message::Text(text) => {
                        stats.messages_received += 1;
                        stats.bytes_received += text.len();
                        
                        debug!(
                            peer = %peer_addr,
                            size = text.len(),
                            msg_type = "text",
                            "Received message"
                        );
                        
                        // Log preview of text content
                        let preview = if text.len() > 100 {
                            format!("{}...", &text[..100])
                        } else {
                            text.clone()
                        };
                        debug!(peer = %peer_addr, content = %preview, "Message content");
                    }
                    Message::Binary(data) => {
                        stats.messages_received += 1;
                        stats.bytes_received += data.len();
                        
                        debug!(
                            peer = %peer_addr,
                            size = data.len(),
                            msg_type = "binary",
                            "Received message"
                        );
                    }
                    Message::Ping(data) => {
                        debug!(
                            peer = %peer_addr,
                            size = data.len(),
                            "Received ping"
                        );
                    }
                    Message::Pong(data) => {
                        debug!(
                            peer = %peer_addr,
                            size = data.len(),
                            "Received pong"
                        );
                    }
                    Message::Close(frame) => {
                        if let Some(cf) = frame {
                            info!(
                                peer = %peer_addr,
                                code = cf.code.into(),
                                reason = %cf.reason,
                                "Received close frame"
                            );
                        } else {
                            info!(peer = %peer_addr, "Received close frame (no details)");
                        }
                        break;
                    }
                    Message::Frame(_) => {
                        debug!(peer = %peer_addr, "Received raw frame");
                    }
                }
                
                // Echo message back with debugging
                if !msg.is_close() {
                    let msg_size = match &msg {
                        Message::Text(t) => t.len(),
                        Message::Binary(b) => b.len(),
                        _ => 0,
                    };
                    
                    if let Err(e) = write.send(msg).await {
                        error!(
                            peer = %peer_addr,
                            error = %e,
                            "Failed to send message"
                        );
                        break;
                    }
                    
                    stats.messages_sent += 1;
                    stats.bytes_sent += msg_size;
                    
                    debug!(
                        peer = %peer_addr,
                        size = msg_size,
                        "Sent message"
                    );
                }
            }
            Err(e) => {
                warn!(
                    peer = %peer_addr,
                    error = %e,
                    "Error receiving message"
                );
                break;
            }
        }
    }
    
    info!(peer = %peer_addr, "Connection closing");
    stats.log_summary();
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing with custom formatting
    tracing_subscriber::fmt()
        .with_max_level(Level::DEBUG)
        .with_target(false)
        .with_thread_ids(true)
        .with_file(true)
        .with_line_number(true)
        .init();
    
    let addr = "127.0.0.1:9001";
    let listener = TcpListener::bind(&addr).await?;
    
    info!(address = %addr, "WebSocket debug server started");
    
    while let Ok((stream, peer_addr)) = listener.accept().await {
        let peer = peer_addr.to_string();
        tokio::spawn(async move {
            handle_connection(stream, peer).await;
        });
    }
    
    Ok(())
}
```

### Advanced Rust Debugging with Custom Inspector

```rust
use std::sync::Arc;
use tokio::sync::Mutex;
use serde::{Serialize, Deserialize};
use chrono::{DateTime, Utc};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct MessageLog {
    timestamp: DateTime<Utc>,
    direction: String, // "sent" or "received"
    message_type: String,
    size: usize,
    content_preview: Option<String>,
}

#[derive(Debug, Clone)]
struct ConnectionInspector {
    logs: Arc<Mutex<Vec<MessageLog>>>,
    stats: Arc<Mutex<ConnectionStats>>,
}

impl ConnectionInspector {
    fn new(peer_addr: String) -> Self {
        Self {
            logs: Arc::new(Mutex::new(Vec::new())),
            stats: Arc::new(Mutex::new(ConnectionStats::new(peer_addr))),
        }
    }
    
    async fn log_message(&self, direction: &str, msg: &Message) {
        let (msg_type, size, preview) = match msg {
            Message::Text(text) => {
                let preview = if text.len() > 50 {
                    Some(format!("{}...", &text[..50]))
                } else {
                    Some(text.clone())
                };
                ("text", text.len(), preview)
            }
            Message::Binary(data) => {
                ("binary", data.len(), Some(format!("{} bytes", data.len())))
            }
            Message::Ping(_) => ("ping", 0, None),
            Message::Pong(_) => ("pong", 0, None),
            Message::Close(_) => ("close", 0, None),
            Message::Frame(_) => ("frame", 0, None),
        };
        
        let log_entry = MessageLog {
            timestamp: Utc::now(),
            direction: direction.to_string(),
            message_type: msg_type.to_string(),
            size,
            content_preview: preview,
        };
        
        self.logs.lock().await.push(log_entry.clone());
        
        // Update statistics
        let mut stats = self.stats.lock().await;
        if direction == "received" {
            stats.messages_received += 1;
            stats.bytes_received += size;
        } else {
            stats.messages_sent += 1;
            stats.bytes_sent += size;
        }
    }
    
    async fn export_logs(&self) -> String {
        let logs = self.logs.lock().await;
        serde_json::to_string_pretty(&*logs).unwrap_or_default()
    }
    
    async fn get_stats(&self) -> ConnectionStats {
        self.stats.lock().await.clone()
    }
}
```

## Summary

Real-time debugging of WebSocket connections requires a multi-layered approach combining protocol-level inspection, message logging, statistical tracking, and error monitoring. The key challenges include:

**Persistent Connection Nature**: Unlike HTTP's request-response model, WebSockets maintain long-lived connections requiring continuous monitoring and state tracking throughout the connection lifecycle.

**Bidirectional Communication**: Both client and server can initiate messages, necessitating comprehensive logging on both ends with proper correlation and timestamps.

**Frame-Level Complexity**: WebSocket frames can be fragmented, masked, or use different opcodes (text, binary, ping, pong, close), requiring detailed frame inspection capabilities.

**Performance Considerations**: Debugging overhead must be minimized in production environments while still capturing essential information for troubleshooting.

The code examples demonstrate practical implementations in C/C++ and Rust with features including connection lifecycle tracking, message content logging, statistical analysis, error handling, timestamp correlation, and configurable debug levels. These tools enable developers to effectively diagnose issues ranging from handshake failures to message delivery problems in real-time WebSocket applications.