# Graceful Connection Closure in WebSocket

## Overview

Graceful connection closure is a critical aspect of WebSocket protocol implementation that ensures clean termination of connections between client and server. Unlike abrupt disconnections, graceful closure follows a structured handshake process defined in RFC 6455, allowing both endpoints to properly release resources, notify the other party of termination intent, and maintain data integrity.

## The WebSocket Close Handshake

The WebSocket close handshake is a bidirectional process:

1. **Initiation**: Either endpoint sends a Close frame with an optional status code and reason
2. **Acknowledgment**: The receiving endpoint responds with its own Close frame
3. **Termination**: The underlying TCP connection is closed

### Close Frame Structure

A Close frame has the following characteristics:
- **Opcode**: 0x8 (Close frame)
- **Payload**: Optional 2-byte status code followed by UTF-8 encoded reason phrase
- **First frame to close**: Should be sent by the endpoint initiating closure

### Status Codes

Common WebSocket close status codes include:

- **1000**: Normal closure - connection completed its purpose
- **1001**: Going Away - server/client is going down or navigating away
- **1002**: Protocol Error - endpoint terminating due to protocol violation
- **1003**: Unsupported Data - received data type that can't be accepted
- **1006**: Abnormal Closure - no close frame received (reserved, not sent)
- **1007**: Invalid Frame Payload - inconsistent data received
- **1008**: Policy Violation - generic code for policy violations
- **1009**: Message Too Big - message too large to process
- **1010**: Mandatory Extension - client expected server to negotiate extensions
- **1011**: Internal Error - server encountered unexpected condition
- **1015**: TLS Handshake - TLS handshake failure (reserved, not sent)

## C/C++ Implementation

Here's a comprehensive example using libwebsockets:

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

// Global flag for graceful shutdown
static volatile int force_exit = 0;

// Close status code definitions
enum ws_close_status {
    WS_CLOSE_NORMAL = 1000,
    WS_CLOSE_GOING_AWAY = 1001,
    WS_CLOSE_PROTOCOL_ERROR = 1002,
    WS_CLOSE_UNSUPPORTED_DATA = 1003,
    WS_CLOSE_INVALID_PAYLOAD = 1007,
    WS_CLOSE_POLICY_VIOLATION = 1008,
    WS_CLOSE_MESSAGE_TOO_BIG = 1009,
    WS_CLOSE_INTERNAL_ERROR = 1011
};

// Per-session data
struct session_data {
    struct lws *wsi;
    int close_initiated;
    int close_received;
};

// Signal handler for graceful shutdown
void sigint_handler(int sig) {
    force_exit = 1;
}

// Initiate graceful close
int websocket_close_graceful(struct lws *wsi, enum ws_close_status status, 
                             const char *reason) {
    unsigned char buf[LWS_PRE + 125];
    unsigned char *p = &buf[LWS_PRE];
    size_t reason_len = reason ? strlen(reason) : 0;
    
    // Write status code (big-endian)
    *p++ = (status >> 8) & 0xFF;
    *p++ = status & 0xFF;
    
    // Add reason if provided
    if (reason && reason_len > 0) {
        // Limit reason to 123 bytes (125 - 2 for status code)
        if (reason_len > 123) {
            reason_len = 123;
        }
        memcpy(p, reason, reason_len);
        p += reason_len;
    }
    
    // Send close frame
    int n = lws_write(wsi, &buf[LWS_PRE], p - &buf[LWS_PRE], LWS_WRITE_CLOSE);
    
    if (n < 0) {
        lwsl_err("Failed to send close frame\n");
        return -1;
    }
    
    lwsl_notice("Close frame sent: status=%d, reason=%s\n", status, 
                reason ? reason : "(none)");
    return 0;
}

// WebSocket callback handler
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_notice("Connection established\n");
            session->wsi = wsi;
            session->close_initiated = 0;
            session->close_received = 0;
            break;
            
        case LWS_CALLBACK_RECEIVE:
            lwsl_notice("Received %zu bytes\n", len);
            // Process received data...
            
            // Example: close on specific message
            if (len == 5 && memcmp(in, "close", 5) == 0) {
                websocket_close_graceful(wsi, WS_CLOSE_NORMAL, 
                                        "Client requested close");
                session->close_initiated = 1;
            }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Check if we need to initiate closure
            if (force_exit && !session->close_initiated) {
                websocket_close_graceful(wsi, WS_CLOSE_GOING_AWAY, 
                                        "Server shutting down");
                session->close_initiated = 1;
            }
            break;
            
        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
            // Peer initiated close - extract status and reason
            if (len >= 2) {
                unsigned short status = (in ? ((unsigned char *)in)[0] << 8 : 0) |
                                       (in ? ((unsigned char *)in)[1] : 0);
                const char *reason_str = len > 2 ? (char *)in + 2 : "";
                
                lwsl_notice("Peer initiated close: status=%d, reason=%.*s\n", 
                           status, (int)(len - 2), reason_str);
                session->close_received = 1;
            }
            // libwebsockets will automatically send close response
            break;
            
        case LWS_CALLBACK_CLOSED:
            lwsl_notice("Connection closed\n");
            // Cleanup resources
            session->wsi = NULL;
            session->close_initiated = 0;
            session->close_received = 0;
            break;
            
        case LWS_CALLBACK_WSI_DESTROY:
            lwsl_notice("WSI being destroyed\n");
            // Final cleanup
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "websocket-protocol",
        callback_websocket,
        sizeof(struct session_data),
        1024,
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    // Setup signal handler
    signal(SIGINT, sigint_handler);
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return -1;
    }
    
    lwsl_notice("WebSocket server started on port %d\n", info.port);
    
    // Main event loop
    while (!force_exit) {
        lws_service(context, 50);
    }
    
    lwsl_notice("Initiating graceful shutdown...\n");
    
    // Allow time for close handshakes to complete
    int countdown = 50; // 2.5 seconds
    while (countdown-- > 0) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    lwsl_notice("Server shutdown complete\n");
    
    return 0;
}
```

## Rust Implementation

Here's an example using the `tokio-tungstenite` crate:

```rust
use tokio_tungstenite::{
    accept_async, connect_async,
    tungstenite::{
        protocol::{CloseFrame, frame::coding::CloseCode},
        Message, Error
    }
};
use tokio::net::{TcpListener, TcpStream};
use futures_util::{StreamExt, SinkExt};
use std::borrow::Cow;
use std::time::Duration;

// Custom close codes enumeration
#[derive(Debug, Clone, Copy)]
pub enum WebSocketCloseCode {
    Normal = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    UnsupportedData = 1003,
    InvalidPayload = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009,
    InternalError = 1011,
}

impl From<WebSocketCloseCode> for CloseCode {
    fn from(code: WebSocketCloseCode) -> Self {
        match code {
            WebSocketCloseCode::Normal => CloseCode::Normal,
            WebSocketCloseCode::GoingAway => CloseCode::Away,
            WebSocketCloseCode::ProtocolError => CloseCode::Protocol,
            WebSocketCloseCode::UnsupportedData => CloseCode::Unsupported,
            WebSocketCloseCode::InvalidPayload => CloseCode::Invalid,
            WebSocketCloseCode::PolicyViolation => CloseCode::Policy,
            WebSocketCloseCode::MessageTooBig => CloseCode::Size,
            WebSocketCloseCode::InternalError => CloseCode::Error,
        }
    }
}

// Graceful close helper
async fn close_gracefully<S>(
    ws_stream: &mut S,
    code: WebSocketCloseCode,
    reason: Option<&str>
) -> Result<(), Error>
where
    S: SinkExt<Message> + Unpin,
    S::Error: Into<Error>,
{
    let close_frame = CloseFrame {
        code: code.into(),
        reason: reason.map(|r| Cow::from(r.to_string())).unwrap_or(Cow::from("")),
    };
    
    println!("Sending close frame: code={:?}, reason={:?}", 
             close_frame.code, close_frame.reason);
    
    ws_stream.send(Message::Close(Some(close_frame)))
        .await
        .map_err(|e| e.into())?;
    
    ws_stream.flush().await.map_err(|e| e.into())?;
    
    Ok(())
}

// Server-side handler
async fn handle_connection(stream: TcpStream) -> Result<(), Error> {
    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");
    
    let (mut write, mut read) = ws_stream.split();
    let mut close_sent = false;
    let mut close_received = false;
    
    // Message processing loop
    while let Some(message) = read.next().await {
        match message {
            Ok(msg) => {
                match msg {
                    Message::Text(text) => {
                        println!("Received text: {}", text);
                        
                        // Example: initiate close on "quit" command
                        if text.trim() == "quit" {
                            close_gracefully(
                                &mut write,
                                WebSocketCloseCode::Normal,
                                Some("Client requested shutdown")
                            ).await?;
                            close_sent = true;
                            break;
                        }
                        
                        // Echo the message back
                        write.send(Message::Text(text)).await?;
                    }
                    
                    Message::Binary(data) => {
                        println!("Received binary data: {} bytes", data.len());
                        write.send(Message::Binary(data)).await?;
                    }
                    
                    Message::Close(frame) => {
                        close_received = true;
                        
                        if let Some(cf) = frame {
                            println!("Received close frame: code={:?}, reason={}", 
                                   cf.code, cf.reason);
                        } else {
                            println!("Received close frame without payload");
                        }
                        
                        // Send close response if we haven't already
                        if !close_sent {
                            close_gracefully(
                                &mut write,
                                WebSocketCloseCode::Normal,
                                Some("Acknowledging close")
                            ).await?;
                            close_sent = true;
                        }
                        break;
                    }
                    
                    Message::Ping(data) => {
                        println!("Received ping");
                        write.send(Message::Pong(data)).await?;
                    }
                    
                    Message::Pong(_) => {
                        println!("Received pong");
                    }
                    
                    Message::Frame(_) => {
                        // Raw frames are typically not handled directly
                    }
                }
            }
            Err(e) => {
                eprintln!("Error receiving message: {}", e);
                
                // Attempt to send error close frame
                if !close_sent {
                    let _ = close_gracefully(
                        &mut write,
                        WebSocketCloseCode::InternalError,
                        Some("Processing error")
                    ).await;
                }
                break;
            }
        }
    }
    
    println!("Connection handler exiting (close_sent={}, close_received={})",
             close_sent, close_received);
    
    Ok(())
}

// Server main
#[tokio::main]
async fn run_server() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    // Graceful shutdown handling
    let (shutdown_tx, mut shutdown_rx) = tokio::sync::mpsc::channel::<()>(1);
    
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        println!("Received shutdown signal");
        shutdown_tx.send(()).await.ok();
    });
    
    loop {
        tokio::select! {
            result = listener.accept() => {
                match result {
                    Ok((stream, addr)) => {
                        println!("New connection from: {}", addr);
                        tokio::spawn(async move {
                            if let Err(e) = handle_connection(stream).await {
                                eprintln!("Error handling connection: {}", e);
                            }
                        });
                    }
                    Err(e) => {
                        eprintln!("Accept error: {}", e);
                    }
                }
            }
            _ = shutdown_rx.recv() => {
                println!("Shutting down server...");
                break;
            }
        }
    }
    
    // Allow time for existing connections to close gracefully
    tokio::time::sleep(Duration::from_secs(2)).await;
    println!("Server shutdown complete");
    
    Ok(())
}

// Client example
async fn run_client() -> Result<(), Box<dyn std::error::Error>> {
    let (ws_stream, _) = connect_async("ws://127.0.0.1:8080").await?;
    println!("Connected to server");
    
    let (mut write, mut read) = ws_stream.split();
    
    // Send some messages
    write.send(Message::Text("Hello, server!".into())).await?;
    
    // Receive response
    if let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => println!("Server responded: {}", text),
            _ => {}
        }
    }
    
    // Initiate graceful close
    close_gracefully(
        &mut write,
        WebSocketCloseCode::Normal,
        Some("Client finished")
    ).await?;
    
    // Wait for close response from server
    if let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Close(frame)) => {
                if let Some(cf) = frame {
                    println!("Server acknowledged close: code={:?}, reason={}", 
                           cf.code, cf.reason);
                }
            }
            _ => {}
        }
    }
    
    println!("Client disconnected gracefully");
    Ok(())
}

fn main() {
    // Run server or client based on arguments
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() > 1 && args[1] == "client" {
        if let Err(e) = run_client() {
            eprintln!("Client error: {}", e);
        }
    } else {
        if let Err(e) = run_server() {
            eprintln!("Server error: {}", e);
        }
    }
}
```

## Best Practices for Graceful Closure

### 1. **Always Respond to Close Frames**
When receiving a close frame, always send a close frame in response before terminating the TCP connection.

### 2. **Use Appropriate Status Codes**
Select status codes that accurately represent the reason for closure to help with debugging and logging.

### 3. **Implement Timeout Mechanisms**
Don't wait indefinitely for a close response. Implement timeouts (typically 2-5 seconds) before forcibly closing the connection.

### 4. **Resource Cleanup**
Ensure all resources (buffers, file handles, database connections) are properly released during closure.

### 5. **Logging**
Log all close events with status codes and reasons for monitoring and debugging purposes.

### 6. **Handle Abnormal Closures**
Detect and handle cases where the TCP connection drops without a proper WebSocket close handshake.

## Summary

Graceful connection closure is fundamental to robust WebSocket implementations. The process involves a bidirectional handshake where one endpoint initiates closure by sending a Close frame containing a status code and optional reason, and the other endpoint acknowledges with its own Close frame before the underlying TCP connection terminates.

Key aspects include:
- **Structured termination**: Following the RFC 6455 close handshake protocol
- **Status codes**: Using standardized codes (1000-1015) to communicate closure reasons
- **Bidirectional acknowledgment**: Both endpoints participate in the closure process
- **Resource management**: Proper cleanup of buffers, connections, and allocated resources
- **Timeout handling**: Preventing indefinite waits with appropriate timeout mechanisms
- **Error handling**: Managing both normal and abnormal closure scenarios

Implementing graceful closure correctly ensures data integrity, prevents resource leaks, provides clear debugging information, and maintains protocol compliance. Both C/C++ (using libraries like libwebsockets) and Rust (using tokio-tungstenite) provide robust mechanisms for implementing proper WebSocket connection termination, though the approaches differ based on language paradigms—C/C++ using callback-based patterns and Rust leveraging async/await with futures.