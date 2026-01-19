# WebSocket Subprotocol Negotiation

## Overview

WebSocket subprotocol negotiation is a mechanism that allows the client and server to agree on an application-level protocol to use over the WebSocket connection. This is accomplished through the `Sec-WebSocket-Protocol` header during the WebSocket handshake. Subprotocols define higher-level conventions for message format, semantics, and communication patterns on top of the base WebSocket framing protocol.

## Why Subprotocol Negotiation?

While WebSocket provides a standardized framing and transport layer, it doesn't define what the messages should contain or how they should be structured. Subprotocol negotiation solves several problems:

1. **Protocol Identification**: Allows multiple applications or versions to coexist on the same endpoint
2. **Version Management**: Enables graceful protocol upgrades and backwards compatibility
3. **Message Format Agreement**: Establishes whether messages are JSON, Protocol Buffers, MessagePack, etc.
4. **Semantic Conventions**: Defines application-level command structures and message patterns

Common subprotocols include:
- `chat` - Simple chat applications
- `wamp.2.json` - Web Application Messaging Protocol
- `mqtt` - MQTT over WebSocket
- `soap` - SOAP protocol over WebSocket
- Custom application-specific protocols

## How It Works

### Handshake Process

1. **Client Request**: The client sends a WebSocket upgrade request including desired subprotocols:
```
GET /chat HTTP/1.1
Sec-WebSocket-Protocol: chat, superchat
```

2. **Server Response**: The server selects one supported subprotocol and responds:
```
HTTP/1.1 101 Switching Protocols
Sec-WebSocket-Protocol: chat
```

3. **Connection Established**: Both parties now communicate using the agreed-upon subprotocol

If the server doesn't support any requested subprotocol, it can either:
- Reject the connection (return 400 Bad Request)
- Accept without a subprotocol (omit `Sec-WebSocket-Protocol` header)

## Code Examples

### C/C++ Implementation

Here's a practical example using libwebsockets, demonstrating both client and server-side subprotocol negotiation:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

// Define our subprotocols
static struct lws_protocols protocols[] = {
    {
        "chat-v2",           // Protocol name
        callback_chat_v2,    // Callback function
        0,                   // Per-session data size
        1024,                // RX buffer size
    },
    {
        "chat-v1",
        callback_chat_v1,
        0,
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

// Callback for chat-v2 protocol
static int callback_chat_v2(struct lws *wsi, 
                            enum lws_callback_reasons reason,
                            void *user, void *in, size_t len)
{
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Connection established with chat-v2 protocol\n");
            // Send a v2-specific greeting
            {
                unsigned char buf[LWS_PRE + 256];
                unsigned char *p = &buf[LWS_PRE];
                int n = sprintf((char *)p, 
                    "{\"type\":\"welcome\",\"version\":2,\"features\":[\"emoji\",\"files\"]}");
                lws_write(wsi, p, n, LWS_WRITE_TEXT);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            printf("chat-v2 received: %.*s\n", (int)len, (char *)in);
            // Parse JSON message according to v2 protocol
            // Echo back with protocol version tag
            {
                unsigned char buf[LWS_PRE + 1024];
                unsigned char *p = &buf[LWS_PRE];
                int n = snprintf((char *)p, 1024,
                    "{\"protocol\":\"v2\",\"echo\":\"%.*s\"}", 
                    (int)len, (char *)in);
                lws_write(wsi, p, n, LWS_WRITE_TEXT);
            }
            break;

        case LWS_CALLBACK_CLOSED:
            printf("chat-v2 connection closed\n");
            break;

        default:
            break;
    }
    return 0;
}

// Callback for chat-v1 protocol
static int callback_chat_v1(struct lws *wsi,
                            enum lws_callback_reasons reason,
                            void *user, void *in, size_t len)
{
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Connection established with chat-v1 protocol\n");
            {
                unsigned char buf[LWS_PRE + 128];
                unsigned char *p = &buf[LWS_PRE];
                int n = sprintf((char *)p, "WELCOME v1");
                lws_write(wsi, p, n, LWS_WRITE_TEXT);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            printf("chat-v1 received: %.*s\n", (int)len, (char *)in);
            // Simple text echo for v1
            {
                unsigned char buf[LWS_PRE + 1024];
                memcpy(&buf[LWS_PRE], in, len);
                lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            }
            break;

        default:
            break;
    }
    return 0;
}

// Server setup
void create_websocket_server() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 9001;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return;
    }
    
    printf("Server started on port 9001\n");
    printf("Supporting protocols: chat-v2, chat-v1\n");
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
}

// Client connection with subprotocol
void connect_websocket_client() {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    
    struct lws_context *context = lws_create_context(&info);
    
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = "localhost";
    connect_info.port = 9001;
    connect_info.path = "/";
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    
    // Request specific subprotocols in order of preference
    connect_info.protocol = "chat-v2,chat-v1";
    
    struct lws *wsi = lws_client_connect_via_info(&connect_info);
    if (!wsi) {
        fprintf(stderr, "Client connection failed\n");
        return;
    }
    
    printf("Client requesting protocols: chat-v2, chat-v1\n");
    
    while (1) {
        lws_service(context, 50);
    }
}
```

### Rust Implementation

Here's a comprehensive example using the `tokio-tungstenite` crate:

```rust
use tokio_tungstenite::{
    connect_async, accept_async,
    tungstenite::{
        protocol::WebSocketConfig,
        handshake::server::{Request, Response},
        http::HeaderValue,
        Message, Error
    }
};
use tokio::net::{TcpListener, TcpStream};
use futures_util::{StreamExt, SinkExt};
use std::net::SocketAddr;

// Define our subprotocol handlers
#[derive(Debug, Clone)]
enum Subprotocol {
    ChatV2,
    ChatV1,
    None,
}

impl Subprotocol {
    fn from_str(s: &str) -> Option<Self> {
        match s.trim() {
            "chat-v2" => Some(Subprotocol::ChatV2),
            "chat-v1" => Some(Subprotocol::ChatV1),
            _ => None,
        }
    }
    
    fn to_str(&self) -> &str {
        match self {
            Subprotocol::ChatV2 => "chat-v2",
            Subprotocol::ChatV1 => "chat-v1",
            Subprotocol::None => "",
        }
    }
    
    fn format_welcome(&self) -> String {
        match self {
            Subprotocol::ChatV2 => {
                r#"{"type":"welcome","version":2,"features":["emoji","files"]}"#.to_string()
            }
            Subprotocol::ChatV1 => {
                "WELCOME v1".to_string()
            }
            Subprotocol::None => {
                "WELCOME".to_string()
            }
        }
    }
    
    fn format_message(&self, msg: &str) -> String {
        match self {
            Subprotocol::ChatV2 => {
                format!(r#"{{"protocol":"v2","message":"{}"}}"#, msg)
            }
            Subprotocol::ChatV1 => {
                format!("v1: {}", msg)
            }
            Subprotocol::None => {
                msg.to_string()
            }
        }
    }
}

// Server implementation
async fn handle_connection(stream: TcpStream, addr: SocketAddr) {
    println!("New connection from: {}", addr);
    
    let mut selected_protocol = Subprotocol::None;
    
    // Custom callback for subprotocol negotiation
    let callback = |req: &Request, response: Response| {
        println!("Incoming WebSocket handshake");
        
        // Extract requested subprotocols
        if let Some(protocols) = req.headers().get("Sec-WebSocket-Protocol") {
            if let Ok(protocol_str) = protocols.to_str() {
                println!("Client requested protocols: {}", protocol_str);
                
                // Parse and select first supported protocol
                for proto in protocol_str.split(',') {
                    if let Some(subproto) = Subprotocol::from_str(proto) {
                        selected_protocol = subproto.clone();
                        println!("Selected protocol: {}", selected_protocol.to_str());
                        
                        // Add selected protocol to response
                        let mut response = response;
                        response.headers_mut().insert(
                            "Sec-WebSocket-Protocol",
                            HeaderValue::from_str(selected_protocol.to_str()).unwrap()
                        );
                        return Ok(response);
                    }
                }
            }
        }
        
        println!("No matching protocol found, accepting without subprotocol");
        Ok(response)
    };
    
    // Accept WebSocket connection with callback
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed: {}", e);
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    // Send welcome message based on protocol
    let welcome = selected_protocol.format_welcome();
    if let Err(e) = write.send(Message::Text(welcome)).await {
        eprintln!("Failed to send welcome: {}", e);
        return;
    }
    
    // Handle messages according to selected protocol
    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                println!("Received ({}): {}", selected_protocol.to_str(), text);
                
                // Format response according to protocol
                let response = selected_protocol.format_message(&text);
                
                if let Err(e) = write.send(Message::Text(response)).await {
                    eprintln!("Send error: {}", e);
                    break;
                }
            }
            Ok(Message::Close(_)) => {
                println!("Connection closed by client");
                break;
            }
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }
}

#[tokio::main]
async fn run_server() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("127.0.0.1:9001").await?;
    println!("WebSocket server listening on ws://127.0.0.1:9001");
    println!("Supported protocols: chat-v2, chat-v1");
    
    while let Ok((stream, addr)) = listener.accept().await {
        tokio::spawn(handle_connection(stream, addr));
    }
    
    Ok(())
}

// Client implementation
#[tokio::main]
async fn run_client() -> Result<(), Box<dyn std::error::Error>> {
    // Create request with subprotocol header
    let url = "ws://127.0.0.1:9001";
    
    let mut request = url.into_client_request()?;
    
    // Add subprotocol header (request multiple in order of preference)
    request.headers_mut().insert(
        "Sec-WebSocket-Protocol",
        HeaderValue::from_static("chat-v2, chat-v1")
    );
    
    println!("Connecting to {} with protocols: chat-v2, chat-v1", url);
    
    let (ws_stream, response) = connect_async(request).await?;
    
    // Check which protocol was selected
    let selected = response
        .headers()
        .get("Sec-WebSocket-Protocol")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("none");
    
    println!("Connected! Server selected protocol: {}", selected);
    
    let (mut write, mut read) = ws_stream.split();
    
    // Receive welcome message
    if let Some(Ok(Message::Text(msg))) = read.next().await {
        println!("Welcome message: {}", msg);
    }
    
    // Send test messages
    let test_messages = vec!["Hello", "How are you?", "Testing protocol"];
    
    for msg in test_messages {
        write.send(Message::Text(msg.to_string())).await?;
        println!("Sent: {}", msg);
        
        if let Some(Ok(Message::Text(response))) = read.next().await {
            println!("Received: {}", response);
        }
    }
    
    write.send(Message::Close(None)).await?;
    
    Ok(())
}

// Advanced: Dynamic protocol selection based on capabilities
fn select_protocol_by_capabilities(
    requested: &str,
    server_capabilities: &[&str]
) -> Option<String> {
    // Parse client's requested protocols
    let client_protocols: Vec<&str> = requested
        .split(',')
        .map(|s| s.trim())
        .collect();
    
    // Find first match with server capabilities
    for proto in client_protocols {
        if server_capabilities.contains(&proto) {
            return Some(proto.to_string());
        }
    }
    
    None
}
```

## Best Practices

1. **Client-side**: Always list subprotocols in order of preference, most preferred first
2. **Server-side**: Select the first supported protocol from the client's list
3. **Fallback**: Design your application to handle connections without a subprotocol if appropriate
4. **Versioning**: Use clear version identifiers in protocol names (e.g., `myapp-v2`, `myapp-v1`)
5. **Documentation**: Clearly document your subprotocol's message format and semantics
6. **Validation**: Always validate that both parties agreed on the same subprotocol after handshake
7. **Error Handling**: Gracefully handle mismatched or unsupported protocols

## Summary

WebSocket subprotocol negotiation via the `Sec-WebSocket-Protocol` header provides a standardized way to establish application-level communication conventions over WebSocket connections. The client proposes one or more subprotocols during the handshake, and the server selects exactly one supported protocol or none. This mechanism enables version management, protocol identification, and semantic agreement between client and server.

In practice, subprotocols allow you to run multiple application versions simultaneously, support different message formats (JSON vs binary), and implement domain-specific protocols (like MQTT, WAMP, or custom chat protocols) on top of the WebSocket transport layer. Both C/C++ and Rust provide robust libraries for implementing subprotocol negotiation, with the handshake process handled automatically by the WebSocket implementation once you've configured your supported protocols.