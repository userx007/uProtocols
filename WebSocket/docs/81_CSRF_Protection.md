# CSRF Protection for WebSocket Endpoints

## Overview

Cross-Site Request Forgery (CSRF) is a security vulnerability where an attacker tricks a user's browser into making unwanted requests to a web application where the user is authenticated. While traditional CSRF attacks target HTTP endpoints, WebSocket connections are also vulnerable since browsers automatically include cookies during the WebSocket handshake, potentially allowing malicious sites to establish unauthorized WebSocket connections on behalf of authenticated users.

## The WebSocket CSRF Threat

When a user visits a malicious website, that site can attempt to establish a WebSocket connection to a legitimate service where the user is authenticated. Since the browser includes authentication cookies in the WebSocket upgrade request, the server might accept the connection, allowing the attacker to:

1. **Establish unauthorized connections** using the victim's credentials
2. **Send commands** through the WebSocket as the authenticated user
3. **Receive sensitive data** meant for the legitimate user
4. **Perform actions** the user didn't intend to authorize

## CSRF Protection Strategies for WebSockets

### 1. Origin Header Verification
Check the `Origin` header during the WebSocket handshake to ensure requests come from trusted domains.

### 2. CSRF Tokens
Include anti-CSRF tokens that must be validated during connection establishment.

### 3. Custom Authentication Headers
Use custom headers (not cookies) for authentication, which JavaScript from other origins cannot set.

### 4. SameSite Cookies
Configure cookies with `SameSite` attribute to prevent cross-site cookie transmission.

---

## C/C++ Implementation Examples

### Example 1: Origin Header Verification with libwebsockets

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

// Whitelist of allowed origins
static const char *allowed_origins[] = {
    "https://yourdomain.com",
    "https://app.yourdomain.com",
    NULL
};

// Verify origin header
static int verify_origin(const char *origin) {
    if (!origin) {
        lwsl_warn("No Origin header present\n");
        return 0;
    }
    
    for (int i = 0; allowed_origins[i] != NULL; i++) {
        if (strcmp(origin, allowed_origins[i]) == 0) {
            return 1; // Valid origin
        }
    }
    
    lwsl_warn("Origin not allowed: %s\n", origin);
    return 0;
}

// WebSocket callback with CSRF protection
static int callback_websocket(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    char origin[256];
    
    switch (reason) {
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
            // Get Origin header during handshake
            if (lws_hdr_copy(wsi, origin, sizeof(origin), 
                            WSI_TOKEN_ORIGIN) > 0) {
                if (!verify_origin(origin)) {
                    lwsl_err("CSRF: Rejecting connection from %s\n", origin);
                    return -1; // Reject connection
                }
            } else {
                lwsl_err("CSRF: No origin header\n");
                return -1;
            }
            lwsl_notice("Connection accepted from: %s\n", origin);
            break;
            
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_notice("WebSocket connection established\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            lwsl_notice("Received: %.*s\n", (int)len, (char *)in);
            // Handle received data
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
    info.gid = -1;
    info.uid = -1;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return -1;
    }
    
    lwsl_notice("Server started on port 8080\n");
    
    while (1) {
        lws_service(context, 1000);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

### Example 2: CSRF Token Validation in C++

```cpp
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include <set>
#include <string>
#include <random>
#include <sstream>

typedef websocketpp::server<websocketpp::config::asio> server;

class WebSocketServer {
private:
    server ws_server;
    std::set<std::string> valid_tokens;
    std::set<std::string> allowed_origins;
    
    // Generate CSRF token
    std::string generate_token() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::stringstream ss;
        for (int i = 0; i < 32; i++) {
            int val = dis(gen);
            ss << std::hex << val;
        }
        return ss.str();
    }
    
    // Verify origin
    bool verify_origin(const std::string& origin) {
        return allowed_origins.find(origin) != allowed_origins.end();
    }
    
    // Extract token from query string
    std::string extract_token(const std::string& uri) {
        size_t pos = uri.find("token=");
        if (pos == std::string::npos) {
            return "";
        }
        
        size_t start = pos + 6; // Length of "token="
        size_t end = uri.find("&", start);
        
        if (end == std::string::npos) {
            return uri.substr(start);
        }
        return uri.substr(start, end - start);
    }
    
public:
    WebSocketServer() {
        // Add allowed origins
        allowed_origins.insert("https://yourdomain.com");
        allowed_origins.insert("https://app.yourdomain.com");
        
        ws_server.init_asio();
        
        // Set connection validation handler
        ws_server.set_validate_handler([this](websocketpp::connection_hdl hdl) {
            server::connection_ptr con = ws_server.get_con_from_hdl(hdl);
            
            // Check Origin header
            std::string origin = con->get_request_header("Origin");
            if (!verify_origin(origin)) {
                std::cout << "CSRF: Invalid origin: " << origin << std::endl;
                return false;
            }
            
            // Extract and verify CSRF token
            std::string uri = con->get_resource();
            std::string token = extract_token(uri);
            
            if (token.empty() || valid_tokens.find(token) == valid_tokens.end()) {
                std::cout << "CSRF: Invalid or missing token" << std::endl;
                return false;
            }
            
            // Token is valid, remove it (single use)
            valid_tokens.erase(token);
            
            std::cout << "Connection validated successfully" << std::endl;
            return true;
        });
        
        // Set open handler
        ws_server.set_open_handler([](websocketpp::connection_hdl hdl) {
            std::cout << "Connection established" << std::endl;
        });
        
        // Set message handler
        ws_server.set_message_handler([this](websocketpp::connection_hdl hdl, 
                                            server::message_ptr msg) {
            std::cout << "Received: " << msg->get_payload() << std::endl;
            
            // Echo back
            try {
                ws_server.send(hdl, msg->get_payload(), msg->get_opcode());
            } catch (const std::exception& e) {
                std::cerr << "Send failed: " << e.what() << std::endl;
            }
        });
    }
    
    // Generate and store a new CSRF token
    std::string create_csrf_token() {
        std::string token = generate_token();
        valid_tokens.insert(token);
        return token;
    }
    
    void run(uint16_t port) {
        ws_server.listen(port);
        ws_server.start_accept();
        
        std::cout << "Server running on port " << port << std::endl;
        std::cout << "Use create_csrf_token() to generate valid tokens" << std::endl;
        
        ws_server.run();
    }
};

int main() {
    WebSocketServer server;
    
    // Generate some tokens for testing
    std::string token1 = server.create_csrf_token();
    std::string token2 = server.create_csrf_token();
    
    std::cout << "Valid tokens:" << std::endl;
    std::cout << "  " << token1 << std::endl;
    std::cout << "  " << token2 << std::endl;
    std::cout << "Connect using: ws://localhost:9002?token=TOKEN" << std::endl;
    
    server.run(9002);
    return 0;
}
```

---

## Rust Implementation Examples

### Example 1: Origin Verification with tokio-tungstenite

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_hdr_async, tungstenite::handshake::server::{Request, Response}};
use futures_util::{StreamExt, SinkExt};
use std::collections::HashSet;

// Allowed origins
fn get_allowed_origins() -> HashSet<String> {
    let mut origins = HashSet::new();
    origins.insert("https://yourdomain.com".to_string());
    origins.insert("https://app.yourdomain.com".to_string());
    origins
}

// Verify origin header
fn verify_origin(req: &Request) -> Result<(), &'static str> {
    let allowed_origins = get_allowed_origins();
    
    // Get origin header
    let origin = req.headers()
        .get("Origin")
        .and_then(|v| v.to_str().ok())
        .ok_or("No Origin header")?;
    
    // Verify against whitelist
    if allowed_origins.contains(origin) {
        println!("✓ Valid origin: {}", origin);
        Ok(())
    } else {
        println!("✗ Invalid origin: {}", origin);
        Err("Origin not allowed")
    }
}

async fn handle_connection(stream: TcpStream) {
    let callback = |req: &Request, mut response: Response| {
        println!("Validating WebSocket connection...");
        
        // Verify origin during handshake
        match verify_origin(req) {
            Ok(_) => {
                println!("Connection accepted");
                Ok(response)
            }
            Err(e) => {
                println!("Connection rejected: {}", e);
                *response.status_mut() = http::StatusCode::FORBIDDEN;
                Err(http::Response::builder()
                    .status(403)
                    .body(Some("Origin not allowed".to_string()))
                    .unwrap())
            }
        }
    };
    
    // Accept WebSocket with validation callback
    let ws_stream = match accept_hdr_async(stream, callback).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed: {}", e);
            return;
        }
    };
    
    println!("WebSocket connection established");
    
    let (mut write, mut read) = ws_stream.split();
    
    // Echo server
    while let Some(msg) = read.next().await {
        match msg {
            Ok(msg) => {
                if msg.is_text() || msg.is_binary() {
                    println!("Received: {:?}", msg);
                    if let Err(e) = write.send(msg).await {
                        eprintln!("Send error: {}", e);
                        break;
                    }
                }
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
    
    println!("Connection closed");
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await.expect("Failed to bind");
    println!("WebSocket server listening on {}", addr);
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        tokio::spawn(handle_connection(stream));
    }
}
```

### Example 2: CSRF Token Validation with Actix-Web

```rust
use actix::{Actor, StreamHandler, AsyncContext, ActorContext};
use actix_web::{web, App, HttpServer, HttpRequest, HttpResponse, Error};
use actix_web_actors::ws;
use std::collections::HashSet;
use std::sync::{Arc, Mutex};
use uuid::Uuid;

// Shared state for CSRF tokens
#[derive(Clone)]
struct AppState {
    csrf_tokens: Arc<Mutex<HashSet<String>>>,
    allowed_origins: HashSet<String>,
}

impl AppState {
    fn new() -> Self {
        let mut allowed_origins = HashSet::new();
        allowed_origins.insert("https://yourdomain.com".to_string());
        allowed_origins.insert("https://app.yourdomain.com".to_string());
        
        AppState {
            csrf_tokens: Arc::new(Mutex::new(HashSet::new())),
            allowed_origins,
        }
    }
    
    fn generate_token(&self) -> String {
        let token = Uuid::new_v4().to_string();
        self.csrf_tokens.lock().unwrap().insert(token.clone());
        token
    }
    
    fn validate_token(&self, token: &str) -> bool {
        let mut tokens = self.csrf_tokens.lock().unwrap();
        tokens.remove(token) // Single-use token
    }
    
    fn verify_origin(&self, origin: Option<&str>) -> bool {
        match origin {
            Some(o) => self.allowed_origins.contains(o),
            None => false,
        }
    }
}

// WebSocket actor
struct WsConnection;

impl Actor for WsConnection {
    type Context = ws::WebsocketContext<Self>;
    
    fn started(&mut self, _ctx: &mut Self::Context) {
        println!("WebSocket connection started");
    }
}

impl StreamHandler<Result<ws::Message, ws::ProtocolError>> for WsConnection {
    fn handle(&mut self, msg: Result<ws::Message, ws::ProtocolError>, ctx: &mut Self::Context) {
        match msg {
            Ok(ws::Message::Text(text)) => {
                println!("Received: {}", text);
                ctx.text(text); // Echo back
            }
            Ok(ws::Message::Binary(bin)) => {
                ctx.binary(bin); // Echo back
            }
            Ok(ws::Message::Ping(msg)) => {
                ctx.pong(&msg);
            }
            Ok(ws::Message::Close(reason)) => {
                println!("Connection closed: {:?}", reason);
                ctx.close(reason);
                ctx.stop();
            }
            _ => (),
        }
    }
}

// WebSocket route handler with CSRF protection
async fn ws_route(
    req: HttpRequest,
    stream: web::Payload,
    data: web::Data<AppState>,
) -> Result<HttpResponse, Error> {
    
    // Verify Origin header
    let origin = req.headers().get("Origin").and_then(|v| v.to_str().ok());
    if !data.verify_origin(origin) {
        println!("CSRF: Invalid origin: {:?}", origin);
        return Ok(HttpResponse::Forbidden().body("Invalid origin"));
    }
    
    // Extract CSRF token from query string
    let query = web::Query::<std::collections::HashMap<String, String>>::from_query(req.query_string())
        .map_err(|_| actix_web::error::ErrorBadRequest("Invalid query"))?;
    
    let token = query.get("token").map(|s| s.as_str()).unwrap_or("");
    
    // Validate CSRF token
    if !data.validate_token(token) {
        println!("CSRF: Invalid or missing token");
        return Ok(HttpResponse::Forbidden().body("Invalid CSRF token"));
    }
    
    println!("✓ CSRF validation passed");
    
    // Upgrade to WebSocket
    ws::start(WsConnection, &req, stream)
}

// Endpoint to generate CSRF tokens
async fn get_token(data: web::Data<AppState>) -> HttpResponse {
    let token = data.generate_token();
    HttpResponse::Ok().json(serde_json::json!({
        "csrf_token": token,
        "websocket_url": format!("ws://localhost:8080/ws?token={}", token)
    }))
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let app_state = web::Data::new(AppState::new());
    
    println!("Starting server on http://127.0.0.1:8080");
    println!("Get CSRF token: GET http://127.0.0.1:8080/token");
    println!("Connect WebSocket: ws://127.0.0.1:8080/ws?token=TOKEN");
    
    HttpServer::new(move || {
        App::new()
            .app_data(app_state.clone())
            .route("/token", web::get().to(get_token))
            .route("/ws", web::get().to(ws_route))
    })
    .bind("127.0.0.1:8080")?
    .run()
    .await
}
```

---

## Summary

**CSRF protection for WebSocket endpoints is critical** because browsers automatically include cookies during the WebSocket handshake, making authenticated users vulnerable to attacks from malicious websites. The primary defense mechanisms include:

1. **Origin Header Verification**: Validate that connections originate from trusted domains by checking the `Origin` header during the handshake
2. **CSRF Tokens**: Issue single-use tokens that must be included in the WebSocket connection URL and validated server-side before accepting the connection
3. **Custom Authentication**: Use non-cookie authentication methods (like tokens in headers or query parameters) that cross-origin JavaScript cannot set
4. **Defense in Depth**: Combine multiple strategies for robust protection

The code examples demonstrate these approaches across C/C++ (using libwebsockets and WebSocket++), and Rust (using tokio-tungstenite and actix-web). All implementations validate connections during the handshake phase before establishing the WebSocket connection, ensuring that unauthorized cross-site requests are rejected at the earliest possible point.