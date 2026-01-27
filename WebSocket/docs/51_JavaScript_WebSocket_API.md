# JavaScript WebSocket API - Detailed Description

## Overview

The **JavaScript WebSocket API** is a browser-native interface that enables full-duplex, bidirectional communication between web browsers and servers over a single TCP connection. Unlike traditional HTTP request-response cycles, WebSockets maintain a persistent connection, allowing real-time data exchange with minimal overhead.

## Core Concepts

### What Makes WebSockets Different?

1. **Persistent Connection**: Once established, the connection remains open until explicitly closed
2. **Full-Duplex Communication**: Both client and server can send messages simultaneously
3. **Low Latency**: No need to establish new connections for each message
4. **Reduced Overhead**: After the initial handshake, data frames are minimal compared to HTTP headers

### The WebSocket Lifecycle

1. **Connection Establishment**: Begins with an HTTP upgrade request
2. **Open State**: Connection is ready for bidirectional communication
3. **Message Exchange**: Data flows freely in both directions
4. **Connection Closure**: Either party can initiate a clean shutdown

## Programming Examples

### JavaScript (Browser)

```javascript
// Create a WebSocket connection
const socket = new WebSocket('ws://localhost:8080/chat');

// Connection opened
socket.addEventListener('open', (event) => {
    console.log('Connected to WebSocket server');
    socket.send('Hello Server!');
});

// Listen for messages
socket.addEventListener('message', (event) => {
    console.log('Message from server:', event.data);
    
    // Handle JSON data
    try {
        const data = JSON.parse(event.data);
        console.log('Parsed data:', data);
    } catch (e) {
        console.log('Plain text message:', event.data);
    }
});

// Connection closed
socket.addEventListener('close', (event) => {
    console.log('Disconnected:', event.code, event.reason);
    
    // Attempt reconnection
    if (event.code !== 1000) {
        setTimeout(() => {
            console.log('Reconnecting...');
            // Recreate connection
        }, 3000);
    }
});

// Handle errors
socket.addEventListener('error', (error) => {
    console.error('WebSocket error:', error);
});

// Send different types of data
function sendMessage(message) {
    if (socket.readyState === WebSocket.OPEN) {
        socket.send(message);
    } else {
        console.error('WebSocket is not open');
    }
}

// Send JSON data
function sendJSON(obj) {
    sendMessage(JSON.stringify(obj));
}

// Send binary data (ArrayBuffer)
function sendBinary(data) {
    const buffer = new ArrayBuffer(data.length);
    const view = new Uint8Array(buffer);
    for (let i = 0; i < data.length; i++) {
        view[i] = data.charCodeAt(i);
    }
    socket.send(buffer);
}

// Close connection gracefully
function closeConnection() {
    socket.close(1000, 'Client closing connection');
}
```

### Advanced JavaScript Example: Chat Application

```javascript
class ChatClient {
    constructor(url) {
        this.url = url;
        this.socket = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 3000;
    }

    connect() {
        this.socket = new WebSocket(this.url);
        
        this.socket.onopen = () => {
            console.log('✓ Connected');
            this.reconnectAttempts = 0;
            this.onConnect();
        };

        this.socket.onmessage = (event) => {
            this.handleMessage(event.data);
        };

        this.socket.onclose = (event) => {
            console.log('✗ Disconnected');
            this.onDisconnect(event);
            this.attemptReconnect();
        };

        this.socket.onerror = (error) => {
            console.error('WebSocket error:', error);
            this.onError(error);
        };
    }

    attemptReconnect() {
        if (this.reconnectAttempts < this.maxReconnectAttempts) {
            this.reconnectAttempts++;
            console.log(`Reconnecting... (${this.reconnectAttempts}/${this.maxReconnectAttempts})`);
            setTimeout(() => this.connect(), this.reconnectDelay);
        } else {
            console.error('Max reconnection attempts reached');
        }
    }

    handleMessage(data) {
        try {
            const message = JSON.parse(data);
            this.onMessage(message);
        } catch (e) {
            this.onMessage({ text: data });
        }
    }

    send(message) {
        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
            this.socket.send(JSON.stringify(message));
            return true;
        }
        return false;
    }

    close() {
        this.maxReconnectAttempts = 0; // Prevent reconnection
        if (this.socket) {
            this.socket.close(1000, 'Client closed');
        }
    }

    // Override these methods
    onConnect() {}
    onDisconnect(event) {}
    onMessage(message) {}
    onError(error) {}
}

// Usage
const chat = new ChatClient('ws://localhost:8080/chat');
chat.onConnect = () => {
    chat.send({ type: 'join', username: 'Alice' });
};
chat.onMessage = (msg) => {
    console.log('Received:', msg);
};
chat.connect();
```

## C/C++ WebSocket Server

Here's a WebSocket server implementation using the `libwebsockets` library:

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

static int interrupted = 0;

// Callback for WebSocket protocol
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Client connected\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            printf("Received: %.*s\n", (int)len, (char *)in);
            
            // Echo the message back
            unsigned char buf[LWS_PRE + 512];
            memcpy(&buf[LWS_PRE], in, len);
            lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            break;
        }

        case LWS_CALLBACK_CLOSED:
            printf("Client disconnected\n");
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
        0,
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

void sigint_handler(int sig) {
    interrupted = 1;
}

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;

    signal(SIGINT, sigint_handler);

    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    printf("WebSocket server started on port 8080\n");

    while (!interrupted) {
        lws_service(context, 50);
    }

    lws_context_destroy(context);
    printf("Server stopped\n");

    return 0;
}
```

### C++ WebSocket Server (using Boost.Beast)

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;

public:
    explicit WebSocketSession(tcp::socket socket)
        : ws_(std::move(socket)) {}

    void run() {
        // Accept the WebSocket handshake
        ws_.async_accept(
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            return;
        }

        std::cout << "Client connected" << std::endl;
        do_read();
    }

    void do_read() {
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &WebSocketSession::on_read,
                shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec == websocket::error::closed) {
            std::cout << "Client disconnected" << std::endl;
            return;
        }

        if (ec) {
            std::cerr << "Read error: " << ec.message() << std::endl;
            return;
        }

        // Echo the message back
        std::cout << "Received: " << beast::make_printable(buffer_.data()) << std::endl;
        
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec) {
            std::cerr << "Write error: " << ec.message() << std::endl;
            return;
        }

        buffer_.consume(buffer_.size());
        do_read();
    }
};

class WebSocketServer {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    WebSocketServer(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc), acceptor_(ioc) {
        
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);

        if (!ec) {
            do_accept();
        }
    }

    void do_accept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &WebSocketServer::on_accept,
                this));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<WebSocketSession>(std::move(socket))->run();
        }
        do_accept();
    }
};

int main() {
    try {
        net::io_context ioc{1};
        tcp::endpoint endpoint{tcp::v4(), 8080};
        
        WebSocketServer server(ioc, endpoint);
        std::cout << "WebSocket server running on port 8080" << std::endl;
        
        ioc.run();
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## Rust WebSocket Implementation

### Using `tokio-tungstenite` for Server

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::error::Error;

async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn Error>> {
    let addr = stream.peer_addr()?;
    println!("New connection from: {}", addr);

    let ws_stream = accept_async(stream).await?;
    println!("WebSocket handshake completed: {}", addr);

    let (mut write, mut read) = ws_stream.split();

    // Echo server: forward all messages back
    while let Some(msg) = read.next().await {
        let msg = msg?;
        
        match msg {
            Message::Text(text) => {
                println!("Received text: {}", text);
                write.send(Message::Text(text)).await?;
            }
            Message::Binary(bin) => {
                println!("Received binary: {} bytes", bin.len());
                write.send(Message::Binary(bin)).await?;
            }
            Message::Close(_) => {
                println!("Client {} closed connection", addr);
                break;
            }
            Message::Ping(data) => {
                write.send(Message::Pong(data)).await?;
            }
            _ => {}
        }
    }

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);

    while let Ok((stream, _)) = listener.accept().await {
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }

    Ok(())
}
```

### Rust WebSocket Client

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let url = "ws://localhost:8080";
    
    println!("Connecting to {}", url);
    let (ws_stream, _) = connect_async(url).await?;
    println!("Connected!");

    let (mut write, mut read) = ws_stream.split();

    // Send a message
    write.send(Message::Text("Hello from Rust!".into())).await?;

    // Receive messages
    tokio::spawn(async move {
        while let Some(msg) = read.next().await {
            match msg {
                Ok(Message::Text(text)) => {
                    println!("Received: {}", text);
                }
                Ok(Message::Binary(bin)) => {
                    println!("Received binary: {} bytes", bin.len());
                }
                Ok(Message::Close(_)) => {
                    println!("Connection closed");
                    break;
                }
                Err(e) => {
                    eprintln!("Error: {}", e);
                    break;
                }
                _ => {}
            }
        }
    });

    // Keep connection alive
    tokio::time::sleep(tokio::time::Duration::from_secs(10)).await;

    Ok(())
}
```

### Advanced Rust Example: Broadcast Server

```rust
use tokio::net::TcpListener;
use tokio::sync::broadcast;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;

type Tx = broadcast::Sender<String>;

async fn handle_client(
    stream: tokio::net::TcpStream,
    tx: Tx,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws = accept_async(stream).await?;
    let (mut ws_tx, mut ws_rx) = ws.split();
    let mut rx = tx.subscribe();

    // Task to receive broadcasts and send to client
    let mut send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if ws_tx.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });

    // Task to receive from client and broadcast
    let tx_clone = tx.clone();
    let mut recv_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_rx.next().await {
            if let Message::Text(text) = msg {
                let _ = tx_clone.send(text);
            }
        }
    });

    tokio::select! {
        _ = &mut send_task => recv_task.abort(),
        _ = &mut recv_task => send_task.abort(),
    }

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    let (tx, _) = broadcast::channel::<String>(100);
    let tx = Arc::new(tx);

    println!("Broadcast server running on port 8080");

    while let Ok((stream, addr)) = listener.accept().await {
        println!("New client: {}", addr);
        let tx = Arc::clone(&tx);
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream, (*tx).clone()).await {
                eprintln!("Error with client {}: {}", addr, e);
            }
        });
    }

    Ok(())
}
```

## Summary

The **JavaScript WebSocket API** provides a powerful, standardized way to implement real-time communication in web applications. Key characteristics include:

- **Browser-Native**: No external libraries required in the browser
- **Event-Driven**: Simple callbacks for connection events, messages, errors, and closures
- **Bidirectional**: Both client and server can initiate communication
- **Protocol Support**: Works with both `ws://` (unencrypted) and `wss://` (encrypted via TLS)
- **Data Flexibility**: Supports text (UTF-8) and binary data (ArrayBuffer, Blob)

**Common Use Cases**: Real-time chat applications, live notifications, multiplayer games, collaborative editing tools, financial trading platforms, IoT dashboards, and live sports scores.

**Server-Side Implementation**: While JavaScript handles the browser side, servers can be implemented in any language (C/C++ with libwebsockets or Boost.Beast, Rust with tokio-tungstenite, Node.js, Python, Go, etc.), making WebSockets a versatile solution for modern web applications requiring instant data updates.

The combination of low latency, persistent connections, and broad browser support makes WebSockets the standard choice for real-time web communication.