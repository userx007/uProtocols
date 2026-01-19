# WebSocket Connection Lifecycle

## Overview

The WebSocket connection lifecycle encompasses all stages from initial handshake through active communication to graceful termination. Understanding this lifecycle is crucial for building robust real-time applications that handle connections reliably and efficiently.

## Connection States

A WebSocket connection progresses through several distinct states:

1. **CONNECTING (0)** - Initial handshake in progress
2. **OPEN (1)** - Connection established and ready for communication
3. **CLOSING (2)** - Connection closing handshake initiated
4. **CLOSED (3)** - Connection terminated

## Lifecycle Stages

### 1. Connection Establishment

The lifecycle begins with an HTTP upgrade request that transitions to the WebSocket protocol. The client initiates a handshake, and upon successful negotiation, both parties can begin exchanging data.

### 2. Active Data Transfer

Once established, the connection remains open for bidirectional communication. Both client and server can send messages independently without the request-response overhead of traditional HTTP.

### 3. Connection Maintenance

During the active phase, implementations typically use ping/pong frames to detect broken connections and maintain keep-alive functionality.

### 4. Graceful Closure

Either party can initiate closure by sending a close frame with an optional status code and reason. The other party responds with a close frame, completing the handshake before the underlying TCP connection terminates.

## Code Examples

### C/C++ Implementation

Here's a comprehensive example using libwebsockets that demonstrates complete lifecycle management:### Rust Implementation

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

// Connection state tracking
typedef enum {
    STATE_CONNECTING,
    STATE_OPEN,
    STATE_CLOSING,
    STATE_CLOSED
} connection_state_t;

// Per-session data structure
struct session_data {
    connection_state_t state;
    int ping_count;
    time_t last_ping;
    char *pending_message;
};

static int interrupted = 0;

// Signal handler for graceful shutdown
static void sigint_handler(int sig) {
    interrupted = 1;
}

// WebSocket protocol callback - handles all lifecycle events
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection error: %s\n", in ? (char *)in : "unknown");
            if (session) {
                session->state = STATE_CLOSED;
            }
            return -1;
            
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connection established\n");
            session->state = STATE_OPEN;
            session->ping_count = 0;
            session->last_ping = time(NULL);
            session->pending_message = NULL;
            
            // Request periodic callback for ping/pong
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf("Received message (%zu bytes): %.*s\n", len, (int)len, (char *)in);
            
            // Echo the message back
            if (session->state == STATE_OPEN) {
                size_t msg_len = len;
                session->pending_message = malloc(LWS_PRE + msg_len + 1);
                if (session->pending_message) {
                    memcpy(&session->pending_message[LWS_PRE], in, msg_len);
                    session->pending_message[LWS_PRE + msg_len] = '\0';
                    lws_callback_on_writable(wsi);
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (session->state == STATE_OPEN) {
                // Send pending message if available
                if (session->pending_message) {
                    size_t msg_len = strlen(&session->pending_message[LWS_PRE]);
                    int written = lws_write(wsi, 
                                          (unsigned char *)&session->pending_message[LWS_PRE],
                                          msg_len, LWS_WRITE_TEXT);
                    if (written < 0) {
                        printf("Write error\n");
                        return -1;
                    }
                    printf("Sent message (%d bytes)\n", written);
                    free(session->pending_message);
                    session->pending_message = NULL;
                }
                
                // Send periodic ping
                time_t now = time(NULL);
                if (now - session->last_ping > 30) {
                    unsigned char ping_payload[125];
                    snprintf((char *)ping_payload, sizeof(ping_payload), 
                            "ping-%d", session->ping_count++);
                    lws_write(wsi, ping_payload, strlen((char *)ping_payload), 
                             LWS_WRITE_PING);
                    printf("Sent ping: %s\n", ping_payload);
                    session->last_ping = now;
                }
                
                // Request next writable callback
                lws_callback_on_writable(wsi);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
            printf("Received pong: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
            printf("Peer initiated close: code=%d, reason=%.*s\n",
                   ((unsigned short *)in)[0], (int)(len - 2), (char *)in + 2);
            session->state = STATE_CLOSING;
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Connection closed\n");
            session->state = STATE_CLOSED;
            if (session->pending_message) {
                free(session->pending_message);
                session->pending_message = NULL;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            printf("Appending handshake headers\n");
            session->state = STATE_CONNECTING;
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "websocket-lifecycle-protocol",
        callback_websocket,
        sizeof(struct session_data),
        4096,
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

int main(int argc, char **argv) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct lws_context *context;
    struct lws *wsi;
    int n = 0;
    
    signal(SIGINT, sigint_handler);
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    printf("Connecting to server...\n");
    
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = "echo.websocket.org";
    connect_info.port = 443;
    connect_info.path = "/";
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    connect_info.protocol = protocols[0].name;
    connect_info.ssl_connection = LWSSSLFLAG_SELFSIGNED_OK;
    
    wsi = lws_client_connect_via_info(&connect_info);
    if (!wsi) {
        fprintf(stderr, "Connection failed\n");
        lws_context_destroy(context);
        return 1;
    }
    
    // Main event loop
    while (n >= 0 && !interrupted) {
        n = lws_service(context, 1000);
    }
    
    // Graceful shutdown
    printf("\nInitiating graceful shutdown...\n");
    lws_context_destroy(context);
    
    return 0;
}
```

Here's a Rust example using the `tokio-tungstenite` library:## Key Lifecycle Considerations

```rust
use tokio_tungstenite::{connect_async, tungstenite::protocol::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::time::{interval, Duration};
use url::Url;

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Connecting,
    Open,
    Closing,
    Closed,
}

struct WebSocketConnection {
    state: ConnectionState,
    ping_count: u32,
    message_count: u32,
}

impl WebSocketConnection {
    fn new() -> Self {
        Self {
            state: ConnectionState::Connecting,
            ping_count: 0,
            message_count: 0,
        }
    }
    
    fn transition_to(&mut self, new_state: ConnectionState) {
        println!("State transition: {:?} -> {:?}", self.state, new_state);
        self.state = new_state;
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("Starting WebSocket client...");
    
    let mut connection = WebSocketConnection::new();
    
    // Establish connection
    let url = Url::parse("wss://echo.websocket.org/")?;
    println!("Connecting to: {}", url);
    
    let (ws_stream, response) = match connect_async(url).await {
        Ok((stream, resp)) => {
            println!("Handshake successful!");
            println!("Response status: {}", resp.status());
            connection.transition_to(ConnectionState::Open);
            (stream, resp)
        }
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            connection.transition_to(ConnectionState::Closed);
            return Err(e.into());
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn ping task for connection maintenance
    let ping_handle = tokio::spawn(async move {
        let mut interval = interval(Duration::from_secs(30));
        let mut ping_count = 0;
        
        loop {
            interval.tick().await;
            let payload = format!("ping-{}", ping_count);
            
            match write.send(Message::Ping(payload.as_bytes().to_vec())).await {
                Ok(_) => {
                    println!("Sent ping: {}", payload);
                    ping_count += 1;
                }
                Err(e) => {
                    eprintln!("Failed to send ping: {}", e);
                    break;
                }
            }
            
            // Send a test message periodically
            if ping_count % 2 == 0 {
                let test_msg = format!("Test message #{}", ping_count / 2);
                if let Err(e) = write.send(Message::Text(test_msg.clone())).await {
                    eprintln!("Failed to send message: {}", e);
                    break;
                }
                println!("Sent message: {}", test_msg);
            }
            
            // Initiate graceful close after 5 pings
            if ping_count >= 5 {
                println!("\nInitiating graceful close...");
                let close_msg = Message::Close(Some(
                    tokio_tungstenite::tungstenite::protocol::CloseFrame {
                        code: tokio_tungstenite::tungstenite::protocol::frame::coding::CloseCode::Normal,
                        reason: "Normal closure".into(),
                    }
                ));
                
                if let Err(e) = write.send(close_msg).await {
                    eprintln!("Failed to send close frame: {}", e);
                }
                break;
            }
        }
        
        write
    });
    
    // Message receiving task
    let receive_handle = tokio::spawn(async move {
        let mut local_connection = WebSocketConnection::new();
        local_connection.transition_to(ConnectionState::Open);
        
        while let Some(message_result) = read.next().await {
            match message_result {
                Ok(message) => {
                    match message {
                        Message::Text(text) => {
                            local_connection.message_count += 1;
                            println!("Received text message #{}: {}", 
                                   local_connection.message_count, text);
                        }
                        Message::Binary(data) => {
                            println!("Received binary message: {} bytes", data.len());
                        }
                        Message::Ping(payload) => {
                            println!("Received ping: {:?}", 
                                   String::from_utf8_lossy(&payload));
                        }
                        Message::Pong(payload) => {
                            println!("Received pong: {:?}", 
                                   String::from_utf8_lossy(&payload));
                        }
                        Message::Close(frame) => {
                            if let Some(cf) = frame {
                                println!("Received close frame: code={:?}, reason={}", 
                                       cf.code, cf.reason);
                            } else {
                                println!("Received close frame without details");
                            }
                            local_connection.transition_to(ConnectionState::Closing);
                            break;
                        }
                        Message::Frame(_) => {
                            // Raw frame - typically not handled at this level
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error receiving message: {}", e);
                    local_connection.transition_to(ConnectionState::Closed);
                    break;
                }
            }
        }
        
        println!("Message receiving loop ended");
        local_connection.transition_to(ConnectionState::Closed);
        local_connection
    });
    
    // Wait for both tasks to complete
    let mut write_back = ping_handle.await?;
    let final_connection = receive_handle.await?;
    
    // Ensure clean closure
    if final_connection.state != ConnectionState::Closed {
        println!("Forcing connection closure...");
        let _ = write_back.close().await;
    }
    
    connection.transition_to(ConnectionState::Closed);
    println!("\nConnection lifecycle completed");
    println!("Total messages exchanged: {}", final_connection.message_count);
    
    Ok(())
}

// Example of a more structured server implementation
#[cfg(test)]
mod server_example {
    use super::*;
    use tokio::net::{TcpListener, TcpStream};
    use tokio_tungstenite::accept_async;
    
    async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
        let addr = stream.peer_addr()?;
        println!("New connection from: {}", addr);
        
        let ws_stream = accept_async(stream).await?;
        println!("WebSocket handshake completed for: {}", addr);
        
        let (mut write, mut read) = ws_stream.split();
        
        while let Some(message_result) = read.next().await {
            match message_result {
                Ok(message) => {
                    match message {
                        Message::Text(_) | Message::Binary(_) => {
                            // Echo message back
                            write.send(message).await?;
                        }
                        Message::Ping(payload) => {
                            // Respond with pong
                            write.send(Message::Pong(payload)).await?;
                        }
                        Message::Close(frame) => {
                            println!("Client initiated close: {:?}", frame);
                            // Respond with close frame
                            write.send(Message::Close(frame)).await?;
                            break;
                        }
                        _ => {}
                    }
                }
                Err(e) => {
                    eprintln!("Error processing message from {}: {}", addr, e);
                    break;
                }
            }
        }
        
        println!("Connection closed for: {}", addr);
        Ok(())
    }
    
    pub async fn run_server() -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind("127.0.0.1:8080").await?;
        println!("WebSocket server listening on ws://127.0.0.1:8080");
        
        while let Ok((stream, addr)) = listener.accept().await {
            tokio::spawn(async move {
                if let Err(e) = handle_connection(stream).await {
                    eprintln!("Error handling connection from {}: {}", addr, e);
                }
            });
        }
        
        Ok(())
    }
}
```

### Connection Health Monitoring

**Ping/Pong Mechanism**: Regular ping frames help detect stale or broken connections. If a pong response isn't received within a timeout period, the connection should be considered dead and closed.

**Timeout Management**: Implement appropriate timeouts for connection establishment, idle connections, and close handshakes to prevent resource leaks.

### Error Handling

Robust lifecycle management requires handling various failure scenarios:

- **Connection failures during handshake**: Network issues, refused connections, or authentication failures
- **Unexpected disconnections**: Network interruptions, server crashes, or client termination
- **Protocol violations**: Malformed frames or invalid state transitions
- **Resource exhaustion**: Too many connections or memory constraints

### Graceful Shutdown

Proper closure involves:
1. Initiating close handshake with appropriate status code
2. Waiting for acknowledgment from the peer
3. Closing the underlying TCP connection
4. Cleaning up resources (buffers, timers, callbacks)

Common close status codes:
- **1000**: Normal closure
- **1001**: Going away (server shutdown or browser navigation)
- **1002**: Protocol error
- **1003**: Unsupported data type
- **1006**: Abnormal closure (no close frame received)
- **1011**: Server error

### Reconnection Strategy

Production applications should implement reconnection logic with:
- Exponential backoff to avoid overwhelming the server
- Maximum retry limits
- Connection state persistence for seamless recovery
- Jitter to prevent thundering herd problems

## Summary

The WebSocket connection lifecycle provides a framework for reliable bidirectional communication. Proper lifecycle management involves carefully handling state transitions from initial connection through active data transfer to graceful termination. Key aspects include monitoring connection health through ping/pong mechanisms, implementing robust error handling for various failure scenarios, ensuring graceful shutdown with proper close handshakes, and designing reconnection strategies for production resilience.

The examples demonstrated show how C/C++ with libwebsockets and Rust with tokio-tungstenite handle these lifecycle stages differently—C uses callback-driven architecture while Rust leverages async/await patterns—but both emphasize the importance of explicit state management, proper resource cleanup, and handling all protocol events correctly. Understanding and implementing these lifecycle patterns is essential for building production-ready WebSocket applications that maintain reliable real-time connections even in adverse network conditions.