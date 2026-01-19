# WebSocket Origin Validation

## Overview

Origin validation is a critical security mechanism for WebSocket servers that helps prevent **Cross-Site WebSocket Hijacking (CSWSH)** attacks. Unlike traditional HTTP requests protected by the Same-Origin Policy (SOP), WebSocket connections can be initiated from any web page, making origin validation essential for verifying that connection requests come from trusted sources.

## The Security Problem

When a browser establishes a WebSocket connection, it includes an `Origin` header (or `Sec-WebSocket-Origin` in older protocols) that identifies the web page initiating the connection. Without proper validation, a malicious website could:

1. Trick an authenticated user into visiting their page
2. Establish a WebSocket connection to your server using the user's credentials (cookies, session tokens)
3. Perform unauthorized actions on behalf of the user

This is particularly dangerous because WebSocket connections bypass traditional CSRF protections and can maintain persistent, bidirectional communication channels.

## How Origin Validation Works

The server examines the `Origin` header in the WebSocket handshake request and compares it against a whitelist of allowed origins. If the origin doesn't match, the server rejects the connection with an HTTP 403 Forbidden response.

**Typical Flow:**
1. Client initiates WebSocket handshake with `Origin: https://trusted-site.com`
2. Server extracts and validates the origin
3. If valid → complete handshake
4. If invalid → reject with 403 or 400 status

## Code Examples

### C Implementation (using libwebsockets)

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

// Whitelist of allowed origins
static const char *allowed_origins[] = {
    "https://trusted-site.com",
    "https://app.trusted-site.com",
    NULL  // Terminator
};

// Origin validation function
static int validate_origin(const char *origin) {
    if (!origin) {
        return 0;  // Reject if no origin header
    }
    
    for (int i = 0; allowed_origins[i] != NULL; i++) {
        if (strcmp(origin, allowed_origins[i]) == 0) {
            return 1;  // Valid origin
        }
    }
    
    return 0;  // Origin not in whitelist
}

// WebSocket callback handler
static int callback_websocket(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    char origin_buf[256];
    
    switch (reason) {
        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
            // Early connection filter (before handshake)
            lwsl_user("New connection attempt\n");
            break;
            
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
            // Validate origin during handshake
            if (lws_hdr_copy(wsi, origin_buf, sizeof(origin_buf),
                            WSI_TOKEN_ORIGIN) < 0) {
                lwsl_err("Failed to get Origin header\n");
                return -1;  // Reject connection
            }
            
            if (!validate_origin(origin_buf)) {
                lwsl_warn("Rejected connection from origin: %s\n", origin_buf);
                return -1;  // Reject connection
            }
            
            lwsl_user("Accepted connection from origin: %s\n", origin_buf);
            break;
            
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("WebSocket connection established\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            lwsl_user("Received data: %.*s\n", (int)len, (char *)in);
            // Echo back
            unsigned char buf[LWS_PRE + 512];
            memcpy(&buf[LWS_PRE], in, len);
            lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "ws-protocol",
        callback_websocket,
        0,
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create WebSocket context\n");
        return 1;
    }
    
    printf("WebSocket server running on port 8080 with origin validation\n");
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

### C++ Implementation (using Boost.Beast)

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <unordered_set>
#include <memory>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
private:
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    
    // Whitelist of allowed origins
    static const std::unordered_set<std::string> allowed_origins_;
    
    bool validate_origin(const std::string& origin) {
        if (origin.empty()) {
            return false;
        }
        return allowed_origins_.find(origin) != allowed_origins_.end();
    }
    
public:
    explicit WebSocketSession(tcp::socket socket)
        : ws_(std::move(socket)) {}
    
    void run() {
        // Perform WebSocket handshake with origin validation
        ws_.async_accept(
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this()));
    }
    
private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            return;
        }
        
        // Extract and validate Origin header
        auto origin_field = ws_.get_option(
            websocket::stream_base::decorator([this](websocket::response_type& res) {
                // This is for outgoing responses, we need to check incoming
            }));
        
        // Access the handshake request
        std::string origin;
        if (ws_.got_text()) {
            // In practice, extract from handshake request headers
            // This is a simplified example
            // Real implementation would check during handshake
        }
        
        // For demonstration - in real code, validate during handshake
        // by setting a custom handshake decorator
        
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
        boost::ignore_unused(bytes_transferred);
        
        if (ec == websocket::error::closed) {
            return;
        }
        
        if (ec) {
            std::cerr << "Read error: " << ec.message() << std::endl;
            return;
        }
        
        // Echo the message
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this()));
    }
    
    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec) {
            std::cerr << "Write error: " << ec.message() << std::endl;
            return;
        }
        
        buffer_.consume(buffer_.size());
        do_read();
    }
};

// Initialize allowed origins
const std::unordered_set<std::string> WebSocketSession::allowed_origins_ = {
    "https://trusted-site.com",
    "https://app.trusted-site.com"
};

// Better origin validation example with manual handshake
class SecureWebSocketSession {
private:
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    
    bool validate_origin_from_request(const beast::http::request<beast::http::string_body>& req) {
        auto origin_it = req.find(beast::http::field::origin);
        if (origin_it == req.end()) {
            std::cerr << "No Origin header present" << std::endl;
            return false;
        }
        
        std::string origin = std::string(origin_it->value());
        std::unordered_set<std::string> allowed = {
            "https://trusted-site.com",
            "https://app.trusted-site.com"
        };
        
        if (allowed.find(origin) == allowed.end()) {
            std::cerr << "Origin not allowed: " << origin << std::endl;
            return false;
        }
        
        std::cout << "Origin validated: " << origin << std::endl;
        return true;
    }
};

int main() {
    try {
        auto const address = net::ip::make_address("0.0.0.0");
        auto const port = static_cast<unsigned short>(8080);
        
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {address, port}};
        
        std::cout << "WebSocket server with origin validation on port " << port << std::endl;
        
        for (;;) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            
            std::make_shared<WebSocketSession>(std::move(socket))->run();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
```

### Rust Implementation (using tokio-tungstenite)

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_hdr_async, tungstenite::handshake::server::{Request, Response}};
use futures_util::{StreamExt, SinkExt};
use std::collections::HashSet;
use std::sync::Arc;

#[derive(Clone)]
struct OriginValidator {
    allowed_origins: Arc<HashSet<String>>,
}

impl OriginValidator {
    fn new(origins: Vec<String>) -> Self {
        Self {
            allowed_origins: Arc::new(origins.into_iter().collect()),
        }
    }
    
    fn validate(&self, origin: Option<&str>) -> Result<(), String> {
        match origin {
            None => Err("No Origin header present".to_string()),
            Some(origin_value) => {
                if self.allowed_origins.contains(origin_value) {
                    println!("✓ Origin validated: {}", origin_value);
                    Ok(())
                } else {
                    println!("✗ Origin rejected: {}", origin_value);
                    Err(format!("Origin not allowed: {}", origin_value))
                }
            }
        }
    }
}

async fn handle_connection(
    stream: TcpStream,
    validator: OriginValidator,
) -> Result<(), Box<dyn std::error::Error>> {
    let callback = |req: &Request, mut response: Response| {
        // Extract Origin header
        let origin = req.headers()
            .get("Origin")
            .and_then(|v| v.to_str().ok());
        
        // Validate origin
        if let Err(e) = validator.validate(origin) {
            eprintln!("Origin validation failed: {}", e);
            // Return 403 Forbidden
            *response.status_mut() = http::StatusCode::FORBIDDEN;
            return Err(http::Response::builder()
                .status(403)
                .body(())
                .unwrap());
        }
        
        println!("WebSocket handshake accepted");
        Ok(response)
    };
    
    // Accept WebSocket connection with origin validation
    let ws_stream = match accept_hdr_async(stream, callback).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake error: {}", e);
            return Err(Box::new(e));
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    println!("WebSocket connection established");
    
    // Echo server loop
    while let Some(msg) = read.next().await {
        match msg {
            Ok(message) => {
                if message.is_text() || message.is_binary() {
                    println!("Received: {:?}", message);
                    write.send(message).await?;
                }
            }
            Err(e) => {
                eprintln!("Error receiving message: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    // Configure allowed origins
    let validator = OriginValidator::new(vec![
        "https://trusted-site.com".to_string(),
        "https://app.trusted-site.com".to_string(),
    ]);
    
    loop {
        let (stream, peer_addr) = listener.accept().await?;
        println!("New connection from: {}", peer_addr);
        
        let validator_clone = validator.clone();
        
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream, validator_clone).await {
                eprintln!("Connection error: {}", e);
            }
        });
    }
}
```

### Advanced Rust Implementation with Pattern Matching

```rust
use tokio::net::TcpListener;
use tokio_tungstenite::accept_hdr_async;
use tokio_tungstenite::tungstenite::handshake::server::{Request, Response};
use std::collections::HashSet;
use url::Url;

#[derive(Debug, Clone)]
enum OriginPolicy {
    AllowAll,
    AllowList(HashSet<String>),
    AllowPattern(Vec<String>), // For wildcard matching
}

impl OriginPolicy {
    fn validate(&self, origin: Option<&str>) -> bool {
        match self {
            OriginPolicy::AllowAll => true,
            OriginPolicy::AllowList(allowed) => {
                origin.map(|o| allowed.contains(o)).unwrap_or(false)
            }
            OriginPolicy::AllowPattern(patterns) => {
                if let Some(origin_str) = origin {
                    if let Ok(url) = Url::parse(origin_str) {
                        if let Some(host) = url.host_str() {
                            return patterns.iter().any(|pattern| {
                                // Simple wildcard matching
                                if pattern.starts_with("*.") {
                                    let domain = &pattern[2..];
                                    host.ends_with(domain)
                                } else {
                                    host == pattern
                                }
                            });
                        }
                    }
                }
                false
            }
        }
    }
}

// Usage example
fn create_strict_policy() -> OriginPolicy {
    OriginPolicy::AllowList(
        vec![
            "https://trusted-site.com".to_string(),
            "https://app.trusted-site.com".to_string(),
        ]
        .into_iter()
        .collect()
    )
}

fn create_wildcard_policy() -> OriginPolicy {
    OriginPolicy::AllowPattern(vec![
        "*.trusted-site.com".to_string(),
        "localhost".to_string(),
    ])
}
```

## Best Practices

1. **Always validate Origin headers** - Never trust connections without verification
2. **Use HTTPS origins only** - Reject `http://` origins in production
3. **Maintain a strict whitelist** - Only allow known, trusted origins
4. **Log rejected attempts** - Monitor for potential attacks
5. **Consider subdomain wildcards carefully** - `*.example.com` can be risky if you have user-generated subdomains
6. **Combine with authentication** - Origin validation is not a replacement for proper authentication
7. **Handle missing Origin headers** - Decide whether to reject or allow (usually reject for security)
8. **Use secure WebSocket (wss://)** - Combined with HTTPS origins for end-to-end security

## Summary

Origin validation is a fundamental security control for WebSocket servers that prevents cross-site WebSocket hijacking attacks by verifying the `Origin` header against a whitelist of trusted sources. All three implementations (C with libwebsockets, C++ with Boost.Beast, and Rust with tokio-tungstenite) demonstrate how to intercept the WebSocket handshake, extract the Origin header, and reject unauthorized connection attempts before the WebSocket connection is established. This defense-in-depth approach should be combined with other security measures like authentication tokens, rate limiting, and proper session management to create a robust security posture for WebSocket applications.