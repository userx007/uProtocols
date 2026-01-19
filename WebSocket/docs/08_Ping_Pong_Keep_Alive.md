# WebSocket Ping-Pong Keep-Alive Mechanisms

## Overview

WebSocket ping-pong frames are a built-in heartbeat mechanism designed to maintain active connections and detect when connections have died or become unresponsive. This is crucial for long-lived WebSocket connections where either the client or server needs to verify that the other endpoint is still alive and responsive.

## How Ping-Pong Works

The WebSocket protocol defines two specific control frames for this purpose:

**Ping Frame (opcode 0x9)**: Sent by either endpoint to check if the connection is alive. Can optionally contain application data (up to 125 bytes).

**Pong Frame (opcode 0xA)**: Must be sent in response to a ping frame, echoing back the same application data that was in the ping.

The RFC 6455 specification requires that upon receiving a ping frame, an endpoint must respond with a pong frame as soon as possible. This mechanism allows both parties to detect:
- Network failures
- Unresponsive peers
- Half-open connections
- Proxy timeouts

## Implementation Strategy

A typical implementation involves:
1. Sending periodic ping frames at regular intervals
2. Starting a timeout timer when a ping is sent
3. Expecting a pong response within the timeout period
4. Closing the connection if no pong is received
5. Automatically responding to received ping frames with pong frames

## C/C++ Implementation

Here's a comprehensive example using the `libwebsockets` library:

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#define PING_INTERVAL_SEC 30
#define PONG_TIMEOUT_SEC 10

struct session_data {
    time_t last_ping_sent;
    time_t last_pong_received;
    int waiting_for_pong;
    int connection_alive;
};

static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Connection established\n");
            session->last_pong_received = time(NULL);
            session->waiting_for_pong = 0;
            session->connection_alive = 1;
            
            // Request a callback for the next writable opportunity
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            time_t current_time = time(NULL);
            
            // Check if we're waiting for a pong and it timed out
            if (session->waiting_for_pong) {
                time_t time_since_ping = current_time - session->last_ping_sent;
                if (time_since_ping > PONG_TIMEOUT_SEC) {
                    lwsl_err("Pong timeout - closing connection\n");
                    return -1; // Close connection
                }
            }
            
            // Send ping if interval has elapsed
            time_t time_since_last_pong = current_time - session->last_pong_received;
            if (time_since_last_pong >= PING_INTERVAL_SEC && !session->waiting_for_pong) {
                unsigned char ping_payload[LWS_PRE + 125];
                unsigned char *p = &ping_payload[LWS_PRE];
                
                // Optional: Add timestamp or identifier to ping payload
                snprintf((char *)p, 125, "ping-%ld", current_time);
                int payload_len = strlen((char *)p);
                
                lwsl_user("Sending ping frame\n");
                int result = lws_write(wsi, p, payload_len, LWS_WRITE_PING);
                
                if (result < 0) {
                    lwsl_err("Failed to send ping\n");
                    return -1;
                }
                
                session->last_ping_sent = current_time;
                session->waiting_for_pong = 1;
            }
            
            // Request another callback for next opportunity
            lws_callback_on_writable(wsi);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE_PONG:
            lwsl_user("Received pong frame (payload: %.*s)\n", (int)len, (char *)in);
            session->last_pong_received = time(NULL);
            session->waiting_for_pong = 0;
            break;
            
        case LWS_CALLBACK_RECEIVE_PING:
            // libwebsockets automatically responds with pong, but we can log it
            lwsl_user("Received ping frame - auto-responding with pong\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Handle normal data frames
            lwsl_user("Received data: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_CLOSED:
            lwsl_user("Connection closed\n");
            session->connection_alive = 0;
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "websocket-protocol",
        callback_websocket,
        sizeof(struct session_data),
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return -1;
    }
    
    lwsl_user("WebSocket server started on port %d\n", info.port);
    
    // Main event loop
    while (1) {
        lws_service(context, 1000); // Service with 1 second timeout
    }
    
    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

Here's an implementation using the popular `tokio-tungstenite` library with async/await:## Alternative C++ Implementation

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::time::{interval, timeout, Duration, Instant};
use tokio_tungstenite::{accept_async, tungstenite::Message, WebSocketStream};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;

const PING_INTERVAL: Duration = Duration::from_secs(30);
const PONG_TIMEOUT: Duration = Duration::from_secs(10);

struct ConnectionState {
    last_pong_received: Instant,
    waiting_for_pong: bool,
}

impl ConnectionState {
    fn new() -> Self {
        Self {
            last_pong_received: Instant::now(),
            waiting_for_pong: false,
        }
    }
}

async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");
    
    let (mut write, mut read) = ws_stream.split();
    let mut state = ConnectionState::new();
    let mut ping_interval = interval(PING_INTERVAL);
    ping_interval.tick().await; // First tick completes immediately
    
    loop {
        tokio::select! {
            // Handle incoming messages
            msg = read.next() => {
                match msg {
                    Some(Ok(message)) => {
                        match message {
                            Message::Text(text) => {
                                println!("Received text: {}", text);
                                // Echo back or handle the message
                                write.send(Message::Text(text)).await?;
                            }
                            Message::Binary(data) => {
                                println!("Received binary data: {} bytes", data.len());
                                write.send(Message::Binary(data)).await?;
                            }
                            Message::Ping(payload) => {
                                println!("Received ping, sending pong");
                                // tungstenite automatically sends pong, but we can do it manually
                                write.send(Message::Pong(payload)).await?;
                            }
                            Message::Pong(_payload) => {
                                println!("Received pong response");
                                state.last_pong_received = Instant::now();
                                state.waiting_for_pong = false;
                            }
                            Message::Close(frame) => {
                                println!("Received close frame: {:?}", frame);
                                write.send(Message::Close(None)).await?;
                                break;
                            }
                            _ => {}
                        }
                    }
                    Some(Err(e)) => {
                        eprintln!("WebSocket error: {}", e);
                        break;
                    }
                    None => {
                        println!("Connection closed by client");
                        break;
                    }
                }
            }
            
            // Send periodic pings
            _ = ping_interval.tick() => {
                // Check for pong timeout
                if state.waiting_for_pong {
                    let elapsed = state.last_pong_received.elapsed();
                    if elapsed > PONG_TIMEOUT {
                        eprintln!("Pong timeout exceeded - closing connection");
                        write.send(Message::Close(None)).await?;
                        break;
                    }
                }
                
                // Send ping with optional payload
                let ping_payload = format!("ping-{}", Instant::now().elapsed().as_secs());
                println!("Sending ping frame");
                
                match write.send(Message::Ping(ping_payload.into_bytes())).await {
                    Ok(_) => {
                        state.waiting_for_pong = true;
                    }
                    Err(e) => {
                        eprintln!("Failed to send ping: {}", e);
                        break;
                    }
                }
            }
        }
    }
    
    println!("Connection handler finished");
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    while let Ok((stream, peer_addr)) = listener.accept().await {
        println!("New connection from: {}", peer_addr);
        
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }
    
    Ok(())
}

// Client example with ping-pong handling
#[cfg(test)]
mod client_example {
    use super::*;
    use tokio_tungstenite::connect_async;
    
    pub async fn websocket_client() -> Result<(), Box<dyn std::error::Error>> {
        let url = "ws://127.0.0.1:8080";
        let (ws_stream, _) = connect_async(url).await?;
        println!("Connected to server");
        
        let (mut write, mut read) = ws_stream.split();
        let mut ping_interval = interval(Duration::from_secs(20));
        let mut last_pong = Instant::now();
        
        loop {
            tokio::select! {
                msg = read.next() => {
                    match msg {
                        Some(Ok(Message::Pong(_))) => {
                            println!("Client received pong");
                            last_pong = Instant::now();
                        }
                        Some(Ok(Message::Ping(payload))) => {
                            println!("Client received ping");
                            write.send(Message::Pong(payload)).await?;
                        }
                        Some(Ok(Message::Text(text))) => {
                            println!("Client received: {}", text);
                        }
                        Some(Ok(Message::Close(_))) => {
                            println!("Server closed connection");
                            break;
                        }
                        Some(Err(e)) => {
                            eprintln!("Error: {}", e);
                            break;
                        }
                        None => break,
                        _ => {}
                    }
                }
                
                _ = ping_interval.tick() => {
                    // Check if we haven't received a pong in too long
                    if last_pong.elapsed() > Duration::from_secs(40) {
                        eprintln!("No pong received - connection dead");
                        break;
                    }
                    
                    println!("Client sending ping");
                    write.send(Message::Ping(vec![])).await?;
                }
            }
        }
        
        Ok(())
    }
}
```

For a more modern C++ approach using the Beast library (part of Boost):

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    net::steady_timer ping_timer_;
    net::steady_timer pong_timeout_timer_;
    
    static constexpr std::chrono::seconds PING_INTERVAL{30};
    static constexpr std::chrono::seconds PONG_TIMEOUT{10};
    
    bool waiting_for_pong_ = false;
    std::chrono::steady_clock::time_point last_pong_time_;

public:
    explicit WebSocketSession(tcp::socket&& socket)
        : ws_(std::move(socket))
        , ping_timer_(ws_.get_executor())
        , pong_timeout_timer_(ws_.get_executor())
    {
        last_pong_time_ = std::chrono::steady_clock::now();
    }

    void run() {
        // Set control callback for ping/pong handling
        ws_.control_callback(
            [this](websocket::frame_type kind, beast::string_view payload) {
                on_control_frame(kind, payload);
            }
        );
        
        // Accept the WebSocket handshake
        ws_.async_accept(
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this()
            )
        );
    }

private:
    void on_control_frame(websocket::frame_type kind, beast::string_view payload) {
        boost::ignore_unused(payload);
        
        if (kind == websocket::frame_type::pong) {
            std::cout << "Received pong frame\n";
            last_pong_time_ = std::chrono::steady_clock::now();
            waiting_for_pong_ = false;
            pong_timeout_timer_.cancel();
        } else if (kind == websocket::frame_type::ping) {
            std::cout << "Received ping frame (auto-responding with pong)\n";
            // Beast automatically responds with pong
        }
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << "\n";
            return;
        }
        
        std::cout << "WebSocket connection accepted\n";
        
        // Start the ping mechanism
        schedule_ping();
        
        // Start reading messages
        do_read();
    }

    void do_read() {
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &WebSocketSession::on_read,
                shared_from_this()
            )
        );
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec == websocket::error::closed) {
            std::cout << "Connection closed normally\n";
            return;
        }
        
        if (ec) {
            std::cerr << "Read error: " << ec.message() << "\n";
            return;
        }
        
        // Echo the message back
        std::cout << "Received message: " 
                  << beast::make_printable(buffer_.data()) << "\n";
        
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this()
            )
        );
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec) {
            std::cerr << "Write error: " << ec.message() << "\n";
            return;
        }
        
        // Clear the buffer
        buffer_.consume(buffer_.size());
        
        // Continue reading
        do_read();
    }

    void schedule_ping() {
        ping_timer_.expires_after(PING_INTERVAL);
        ping_timer_.async_wait(
            beast::bind_front_handler(
                &WebSocketSession::on_ping_timer,
                shared_from_this()
            )
        );
    }

    void on_ping_timer(beast::error_code ec) {
        if (ec && ec != net::error::operation_aborted) {
            std::cerr << "Ping timer error: " << ec.message() << "\n";
            return;
        }
        
        if (ec == net::error::operation_aborted) {
            return; // Timer was cancelled
        }
        
        // Check if we're still waiting for a pong
        if (waiting_for_pong_) {
            auto elapsed = std::chrono::steady_clock::now() - last_pong_time_;
            if (elapsed > PONG_TIMEOUT) {
                std::cerr << "Pong timeout - closing connection\n";
                ws_.async_close(
                    websocket::close_code::normal,
                    [self = shared_from_this()](beast::error_code) {}
                );
                return;
            }
        }
        
        // Send ping frame
        std::cout << "Sending ping frame\n";
        waiting_for_pong_ = true;
        
        ws_.async_ping(
            websocket::ping_data{},
            beast::bind_front_handler(
                &WebSocketSession::on_ping_sent,
                shared_from_this()
            )
        );
        
        // Schedule pong timeout check
        pong_timeout_timer_.expires_after(PONG_TIMEOUT);
        pong_timeout_timer_.async_wait(
            beast::bind_front_handler(
                &WebSocketSession::on_pong_timeout,
                shared_from_this()
            )
        );
    }

    void on_ping_sent(beast::error_code ec) {
        if (ec) {
            std::cerr << "Ping send error: " << ec.message() << "\n";
            return;
        }
        
        // Schedule next ping
        schedule_ping();
    }

    void on_pong_timeout(beast::error_code ec) {
        if (ec == net::error::operation_aborted) {
            return; // Pong was received, timer was cancelled
        }
        
        if (waiting_for_pong_) {
            std::cerr << "Pong timeout reached - closing connection\n";
            ws_.async_close(
                websocket::close_code::normal,
                [self = shared_from_this()](beast::error_code) {}
            );
        }
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
    {
        beast::error_code ec;
        
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.bind(endpoint, ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) throw beast::system_error{ec};
    }

    void run() {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &Listener::on_accept,
                shared_from_this()
            )
        );
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << "\n";
        } else {
            std::cout << "New connection accepted\n";
            std::make_shared<WebSocketSession>(std::move(socket))->run();
        }
        
        // Accept another connection
        do_accept();
    }
};

int main() {
    try {
        auto const address = net::ip::make_address("0.0.0.0");
        auto const port = static_cast<unsigned short>(8080);
        
        net::io_context ioc{1};
        
        std::make_shared<Listener>(ioc, tcp::endpoint{address, port})->run();
        
        std::cout << "WebSocket server listening on port " << port << "\n";
        
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
```

## Key Implementation Considerations

### Timing Parameters

**Ping Interval**: The frequency of sending ping frames varies by use case. Common values range from 15 seconds (for applications needing quick failure detection) to 60 seconds (for less critical applications). A 30-second interval is a reasonable default.

**Pong Timeout**: How long to wait for a pong response before considering the connection dead. Typically 5-15 seconds, accounting for network latency and processing delays.

### Best Practices

**Asymmetric Heartbeats**: Often only the server sends pings to clients, as servers typically manage more connections and need to detect dead clients to free resources.

**Payload Usage**: While ping/pong frames can carry up to 125 bytes of application data, it's generally kept minimal or empty. Some implementations include timestamps or sequence numbers for debugging.

**Graceful Degradation**: When a pong timeout occurs, send a proper WebSocket close frame before terminating the connection, allowing the other endpoint to clean up gracefully.

**Resource Management**: Cancel all timers and clean up resources when a connection closes to prevent memory leaks.

**Network Conditions**: Be conservative with timeout values to account for high-latency networks, temporary congestion, or processing delays on mobile devices.

### Common Issues

**Proxy Interference**: Some intermediate proxies may strip ping/pong frames or have their own timeout mechanisms that conflict with application-level heartbeats.

**Battery Concerns**: On mobile devices, frequent pings can impact battery life. Consider longer intervals or allow the application to suspend heartbeats during background operation.

**Clock Skew**: Use monotonic clocks (steady clocks) for timing rather than wall clocks to avoid issues with system time adjustments.

## Summary

WebSocket ping-pong mechanisms provide a standardized, lightweight method for maintaining connection health through periodic heartbeat checks. By sending ping frames at regular intervals and expecting timely pong responses, applications can reliably detect network failures, unresponsive peers, and half-open connections. 

The implementations shown demonstrate how to integrate ping-pong handling into event-driven architectures across C, modern C++, and Rust. Each implementation follows the same conceptual pattern: periodic ping transmission, automatic pong responses to received pings, timeout-based connection monitoring, and graceful connection termination when failures are detected.

Proper implementation of ping-pong keep-alive is essential for production WebSocket applications, ensuring resources are efficiently managed and users receive timely feedback when connectivity issues occur. The mechanism's simplicity and protocol-level support make it the preferred approach over application-level heartbeats for WebSocket connection monitoring.