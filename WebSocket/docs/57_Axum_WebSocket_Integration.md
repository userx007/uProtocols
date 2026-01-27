# Axum WebSocket Integration

## Overview

Axum is a modern, ergonomic web framework for Rust built on top of Tokio and Tower. It provides excellent WebSocket support through its integration with `tokio-tungstenite`, making it straightforward to build WebSocket endpoints with clean, type-safe APIs. Axum's approach to WebSocket handling leverages async/await, extractors, and middleware for a composure-based architecture.

## Detailed Description

### Core Concepts

**1. Axum's WebSocket Architecture**
- Built on Tokio's async runtime
- Uses extractors for WebSocket upgrades
- Provides type-safe message handling
- Integrates seamlessly with Axum's routing system
- Supports both text and binary WebSocket messages

**2. WebSocket Lifecycle in Axum**
- HTTP upgrade request arrives at endpoint
- `WebSocketUpgrade` extractor handles negotiation
- Connection upgrades to WebSocket protocol
- Application handles bidirectional message flow
- Connection cleanup and graceful shutdown

**3. Key Features**
- Zero-cost abstractions
- Built-in backpressure handling
- Concurrent connection management
- Easy integration with state management
- Middleware support for authentication/logging

### When to Use Axum for WebSockets

- Building real-time web applications in Rust
- Need for high performance with type safety
- Microservices requiring WebSocket endpoints
- Chat applications, live dashboards, gaming servers
- When leveraging Rust's ecosystem for web services

---

## Code Examples

### Rust Implementation (Axum)

#### Basic WebSocket Echo Server

```rust
use axum::{
    extract::ws::{Message, WebSocket, WebSocketUpgrade},
    response::Response,
    routing::get,
    Router,
};
use std::net::SocketAddr;
use tokio::net::TcpListener;

#[tokio::main]
async fn main() {
    // Build application with WebSocket route
    let app = Router::new()
        .route("/ws", get(ws_handler));

    // Run server
    let addr = SocketAddr::from(([127, 0, 0, 1], 3000));
    println!("WebSocket server listening on {}", addr);
    
    let listener = TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

// WebSocket upgrade handler
async fn ws_handler(ws: WebSocketUpgrade) -> Response {
    ws.on_upgrade(handle_socket)
}

// Handle individual WebSocket connection
async fn handle_socket(mut socket: WebSocket) {
    println!("New WebSocket connection established");

    while let Some(msg) = socket.recv().await {
        match msg {
            Ok(Message::Text(text)) => {
                println!("Received: {}", text);
                // Echo message back
                if socket.send(Message::Text(text)).await.is_err() {
                    break;
                }
            }
            Ok(Message::Binary(data)) => {
                println!("Received {} bytes", data.len());
                if socket.send(Message::Binary(data)).await.is_err() {
                    break;
                }
            }
            Ok(Message::Close(_)) => {
                println!("Client disconnected");
                break;
            }
            Ok(Message::Ping(data)) => {
                if socket.send(Message::Pong(data)).await.is_err() {
                    break;
                }
            }
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }

    println!("WebSocket connection closed");
}
```

#### Advanced Example with State Management and Broadcasting

```rust
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        State,
    },
    response::Response,
    routing::get,
    Router,
};
use futures::{sink::SinkExt, stream::StreamExt};
use std::{
    collections::HashMap,
    net::SocketAddr,
    sync::Arc,
};
use tokio::sync::{broadcast, RwLock};
use serde::{Deserialize, Serialize};

// Application state with broadcast channel
#[derive(Clone)]
struct AppState {
    // Broadcast channel for messages
    tx: broadcast::Sender<String>,
    // Connected clients
    clients: Arc<RwLock<HashMap<String, Client>>>,
}

#[derive(Clone)]
struct Client {
    id: String,
    username: String,
}

#[derive(Serialize, Deserialize)]
struct ChatMessage {
    username: String,
    content: String,
    timestamp: i64,
}

#[tokio::main]
async fn main() {
    // Create broadcast channel (capacity of 100 messages)
    let (tx, _rx) = broadcast::channel(100);

    let state = AppState {
        tx,
        clients: Arc::new(RwLock::new(HashMap::new())),
    };

    let app = Router::new()
        .route("/ws", get(ws_handler))
        .with_state(state);

    let addr = SocketAddr::from(([127, 0, 0, 1], 3000));
    println!("Chat server listening on {}", addr);
    
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

async fn ws_handler(
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
) -> Response {
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

async fn handle_socket(socket: WebSocket, state: AppState) {
    // Split socket into sender and receiver
    let (mut sender, mut receiver) = socket.split();

    // Generate unique client ID
    let client_id = uuid::Uuid::new_v4().to_string();
    let mut username = String::from("Anonymous");

    // Subscribe to broadcast channel
    let mut rx = state.tx.subscribe();

    // Spawn task to handle broadcast messages
    let mut send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });

    // Handle incoming messages from this client
    let tx = state.tx.clone();
    let clients = state.clients.clone();
    let client_id_clone = client_id.clone();

    let mut recv_task = tokio::spawn(async move {
        while let Some(Ok(message)) = receiver.next().await {
            match message {
                Message::Text(text) => {
                    // Parse message
                    if let Ok(chat_msg) = serde_json::from_str::<ChatMessage>(&text) {
                        username = chat_msg.username.clone();
                        
                        // Update client info
                        {
                            let mut clients_guard = clients.write().await;
                            clients_guard.insert(
                                client_id_clone.clone(),
                                Client {
                                    id: client_id_clone.clone(),
                                    username: username.clone(),
                                },
                            );
                        }

                        // Broadcast to all clients
                        let broadcast_msg = serde_json::to_string(&chat_msg).unwrap();
                        let _ = tx.send(broadcast_msg);
                    }
                }
                Message::Close(_) => {
                    println!("Client {} disconnected", client_id_clone);
                    break;
                }
                _ => {}
            }
        }

        // Cleanup
        let mut clients_guard = clients.write().await;
        clients_guard.remove(&client_id_clone);
    });

    // Wait for either task to finish
    tokio::select! {
        _ = (&mut send_task) => recv_task.abort(),
        _ = (&mut recv_task) => send_task.abort(),
    }

    println!("Connection {} closed", client_id);
}
```

#### WebSocket with Authentication

```rust
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        State,
    },
    headers,
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
    Router,
    TypedHeader,
};

#[derive(Clone)]
struct AppState {
    // Add your authentication state here
}

async fn authenticated_ws_handler(
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
    TypedHeader(auth): TypedHeader<headers::Authorization<headers::authorization::Bearer>>,
) -> Result<Response, StatusCode> {
    // Validate token
    if !validate_token(auth.token()) {
        return Err(StatusCode::UNAUTHORIZED);
    }

    // Upgrade to WebSocket
    Ok(ws.on_upgrade(move |socket| handle_authenticated_socket(socket, state)))
}

fn validate_token(token: &str) -> bool {
    // Implement your token validation logic
    token == "valid_token_here"
}

async fn handle_authenticated_socket(mut socket: WebSocket, _state: AppState) {
    // Handle authenticated WebSocket connection
    while let Some(msg) = socket.recv().await {
        if let Ok(Message::Text(text)) = msg {
            println!("Authenticated message: {}", text);
            // Handle message
        }
    }
}
```

---

### C/C++ Comparison

While Axum is Rust-specific, here's how similar WebSocket functionality looks in C/C++:

#### C++ with Boost.Beast (Similar to Axum)

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <memory>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// WebSocket session handler
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;

public:
    explicit WebSocketSession(tcp::socket socket)
        : ws_(std::move(socket)) {}

    void run() {
        // Accept the WebSocket handshake
        ws_.async_accept(
            [self = shared_from_this()](beast::error_code ec) {
                if (!ec) {
                    self->do_read();
                }
            });
    }

    void do_read() {
        ws_.async_read(
            buffer_,
            [self = shared_from_this()](
                beast::error_code ec, std::size_t bytes_transferred) {
                
                if (ec) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                    return;
                }

                // Echo the message back
                self->ws_.text(self->ws_.got_text());
                self->ws_.async_write(
                    self->buffer_.data(),
                    [self](beast::error_code ec, std::size_t) {
                        if (!ec) {
                            self->buffer_.consume(self->buffer_.size());
                            self->do_read();
                        }
                    });
            });
    }
};

// Listener for accepting connections
class Listener : public std::enable_shared_from_this<Listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc), acceptor_(ioc) {
        
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
    }

    void run() {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<WebSocketSession>(std::move(socket))->run();
                }
                self->do_accept();
            });
    }
};

int main() {
    try {
        auto const address = net::ip::make_address("127.0.0.1");
        auto const port = static_cast<unsigned short>(3000);

        net::io_context ioc{1};

        std::make_shared<Listener>(ioc, tcp::endpoint{address, port})->run();

        std::cout << "WebSocket server listening on " 
                  << address << ":" << port << std::endl;

        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
```

#### C with libwebsockets

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

// Per-session data
struct session_data {
    int message_count;
};

// WebSocket callback
static int callback_echo(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    struct session_data *session_data = (struct session_data *)user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Connection established\n");
            session_data->message_count = 0;
            break;

        case LWS_CALLBACK_RECEIVE:
            printf("Received %zu bytes: %.*s\n", len, (int)len, (char *)in);
            session_data->message_count++;

            // Echo message back
            unsigned char buf[LWS_PRE + 512];
            unsigned char *p = &buf[LWS_PRE];
            size_t n = len < 512 ? len : 512;
            
            memcpy(p, in, n);
            lws_write(wsi, p, n, LWS_WRITE_TEXT);
            break;

        case LWS_CALLBACK_CLOSED:
            printf("Connection closed. Messages received: %d\n", 
                   session_data->message_count);
            break;

        default:
            break;
    }

    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "echo-protocol",
        callback_echo,
        sizeof(struct session_data),
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;

    memset(&info, 0, sizeof(info));
    info.port = 3000;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    printf("WebSocket server listening on port 3000\n");

    // Event loop
    while (1) {
        lws_service(context, 50);
    }

    lws_context_destroy(context);
    return 0;
}
```

---

## Key Differences: Axum vs C/C++

| Aspect | Axum (Rust) | C/C++ |
|--------|-------------|-------|
| **Memory Safety** | Compile-time guarantees | Manual management |
| **Async Model** | Native async/await | Callback-based or coroutines |
| **Type Safety** | Strong, zero-cost | Weaker, requires care |
| **Ergonomics** | High-level extractors | Lower-level APIs |
| **Performance** | Near C++ with safety | Maximum control |
| **Ecosystem** | Growing, modern | Mature, extensive |

---

## Summary

**Axum WebSocket Integration** provides a modern, type-safe approach to building WebSocket servers in Rust. Built on Tokio's async runtime, Axum offers:

- **Clean API Design**: Extractors and handlers make WebSocket upgrade intuitive
- **Type Safety**: Rust's type system prevents common WebSocket errors at compile time
- **Performance**: Zero-cost abstractions deliver C-like performance with high-level APIs
- **Concurrency**: Tokio enables efficient handling of thousands of concurrent connections
- **Ecosystem Integration**: Seamless integration with Rust's web ecosystem (Tower, Hyper)

Compared to C/C++ alternatives like libwebsockets or Boost.Beast, Axum provides:
- Memory safety without garbage collection
- More ergonomic async programming model
- Better compile-time error detection
- Easier state management and middleware integration

The trade-off is maturity—C/C++ WebSocket libraries have longer production histories, but Axum's Rust foundation offers compelling advantages for new projects prioritizing safety and developer productivity alongside performance.

**Use Axum when**: Building new Rust web services, need type safety with performance, want modern async patterns, or are already invested in the Rust ecosystem.

**Use C/C++ when**: Maintaining legacy systems, need maximum low-level control, have existing C++ infrastructure, or require specific C++ libraries.