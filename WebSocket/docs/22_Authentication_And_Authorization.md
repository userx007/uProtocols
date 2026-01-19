# WebSocket Authentication and Authorization

## Overview

Authentication and authorization are critical security mechanisms for WebSocket connections. Unlike HTTP, where each request can carry authentication credentials, WebSockets maintain persistent connections, requiring careful consideration of how to verify identity (authentication) and control access (authorization) throughout the connection lifecycle.

## Core Concepts

### Authentication
The process of verifying the identity of a client attempting to establish or use a WebSocket connection. This typically happens during:
- Initial handshake (HTTP upgrade request)
- First message after connection establishment
- Periodic re-validation during the connection lifetime

### Authorization
Determining what actions an authenticated client is permitted to perform, such as:
- Which channels/topics they can subscribe to
- What messages they can send
- What data they can access

### Token-Based Authentication
The most common approach for WebSocket authentication, where clients present a token (like JWT) that proves their identity without repeatedly sending credentials.

## Implementation Approaches

### 1. Handshake-Time Authentication
Authenticate during the HTTP upgrade request using headers or query parameters.

### 2. Post-Connection Authentication
Accept the connection, then require authentication via the first WebSocket message.

### 3. Hybrid Approach
Basic validation at handshake, full authentication after connection.

---

## C/C++ Implementation

Using libwebsockets library with JWT-based authentication:


```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define MAX_CLIENTS 100
#define TOKEN_EXPIRY 3600  // 1 hour in seconds

// Client session structure
typedef struct {
    struct lws *wsi;
    char user_id[64];
    char token[256];
    time_t token_expiry;
    int authenticated;
    char roles[128];  // Comma-separated roles
} client_session_t;

// Global session store
client_session_t sessions[MAX_CLIENTS];
int session_count = 0;

// Simple JWT validation (simplified - use a proper JWT library in production)
int validate_jwt_token(const char *token, char *user_id, char *roles) {
    // In production, use a proper JWT library like libjwt
    // This is a simplified example showing the concept
    
    // Split token into header.payload.signature
    char token_copy[256];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    
    char *header = strtok(token_copy, ".");
    char *payload = strtok(NULL, ".");
    char *signature = strtok(NULL, ".");
    
    if (!header || !payload || !signature) {
        return 0;
    }
    
    // Verify signature (simplified)
    const char *secret = "your-secret-key";
    unsigned char hmac_result[EVP_MAX_MD_SIZE];
    unsigned int hmac_len;
    
    char data_to_sign[512];
    snprintf(data_to_sign, sizeof(data_to_sign), "%s.%s", header, payload);
    
    HMAC(EVP_sha256(), secret, strlen(secret),
         (unsigned char*)data_to_sign, strlen(data_to_sign),
         hmac_result, &hmac_len);
    
    // Base64 decode signature and compare (simplified)
    // In production, properly decode and compare
    
    // Decode payload (base64 decode, then parse JSON)
    // This is simplified - use a proper base64 decoder and JSON parser
    strcpy(user_id, "user123");  // Extract from decoded payload
    strcpy(roles, "admin,user");  // Extract from decoded payload
    
    return 1;  // Token is valid
}

// Check if user has required role
int has_role(const char *user_roles, const char *required_role) {
    if (!user_roles || !required_role) return 0;
    return strstr(user_roles, required_role) != NULL;
}

// Find or create session for connection
client_session_t* get_session(struct lws *wsi) {
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].wsi == wsi) {
            return &sessions[i];
        }
    }
    
    // Create new session
    if (session_count < MAX_CLIENTS) {
        sessions[session_count].wsi = wsi;
        sessions[session_count].authenticated = 0;
        return &sessions[session_count++];
    }
    
    return NULL;
}

// Remove session
void remove_session(struct lws *wsi) {
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].wsi == wsi) {
            // Shift remaining sessions
            for (int j = i; j < session_count - 1; j++) {
                sessions[j] = sessions[j + 1];
            }
            session_count--;
            break;
        }
    }
}

// WebSocket protocol callback
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    client_session_t *session;
    
    switch (reason) {
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // Authentication during handshake
            char token[256] = {0};
            
            // Try to get token from query parameter
            char name[32], value[256];
            int n = 0;
            while (lws_hdr_copy_fragment(wsi, value, sizeof(value),
                                         WSI_TOKEN_HTTP_URI_ARGS, n) > 0) {
                if (sscanf(value, "token=%255s", token) == 1) {
                    break;
                }
                n++;
            }
            
            // Or from Authorization header
            if (token[0] == '\0') {
                if (lws_hdr_copy(wsi, token, sizeof(token),
                                WSI_TOKEN_HTTP_AUTHORIZATION) > 0) {
                    // Remove "Bearer " prefix if present
                    char *bearer = strstr(token, "Bearer ");
                    if (bearer) {
                        memmove(token, bearer + 7, strlen(bearer + 7) + 1);
                    }
                }
            }
            
            // Validate token
            char user_id[64], roles[128];
            if (token[0] != '\0' && validate_jwt_token(token, user_id, roles)) {
                // Token valid at handshake - mark for post-connection auth
                lwsl_user("Valid token presented at handshake for user: %s\n", user_id);
                return 0;  // Accept connection
            }
            
            // Can still accept and require auth after connection
            lwsl_user("No valid token at handshake, will require auth message\n");
            return 0;
        }
        
        case LWS_CALLBACK_ESTABLISHED: {
            session = get_session(wsi);
            if (!session) {
                lwsl_err("Failed to create session\n");
                return -1;
            }
            
            lwsl_user("WebSocket connection established\n");
            
            // Send authentication request if not authenticated
            const char *auth_req = "{\"type\":\"auth_required\"}";
            unsigned char buf[LWS_PRE + 256];
            memcpy(&buf[LWS_PRE], auth_req, strlen(auth_req));
            lws_write(wsi, &buf[LWS_PRE], strlen(auth_req), LWS_WRITE_TEXT);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            session = get_session(wsi);
            if (!session) return -1;
            
            char *message = (char *)in;
            
            // Parse message (simplified - use JSON parser in production)
            if (!session->authenticated) {
                // Expect authentication message
                char token[256] = {0};
                if (sscanf(message, "{\"type\":\"auth\",\"token\":\"%255[^\"]\"}", token) == 1) {
                    char user_id[64], roles[128];
                    
                    if (validate_jwt_token(token, user_id, roles)) {
                        // Authentication successful
                        session->authenticated = 1;
                        strncpy(session->user_id, user_id, sizeof(session->user_id) - 1);
                        strncpy(session->token, token, sizeof(session->token) - 1);
                        strncpy(session->roles, roles, sizeof(session->roles) - 1);
                        session->token_expiry = time(NULL) + TOKEN_EXPIRY;
                        
                        lwsl_user("User %s authenticated with roles: %s\n", 
                                 user_id, roles);
                        
                        const char *resp = "{\"type\":\"auth_success\"}";
                        unsigned char buf[LWS_PRE + 256];
                        memcpy(&buf[LWS_PRE], resp, strlen(resp));
                        lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                    } else {
                        const char *resp = "{\"type\":\"auth_failed\"}";
                        unsigned char buf[LWS_PRE + 256];
                        memcpy(&buf[LWS_PRE], resp, strlen(resp));
                        lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                        return -1;  // Close connection
                    }
                }
            } else {
                // Check token expiry
                if (time(NULL) > session->token_expiry) {
                    lwsl_user("Token expired for user %s\n", session->user_id);
                    const char *resp = "{\"type\":\"token_expired\"}";
                    unsigned char buf[LWS_PRE + 256];
                    memcpy(&buf[LWS_PRE], resp, strlen(resp));
                    lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                    return -1;
                }
                
                // Authorization check - example for admin-only action
                char action[64] = {0};
                if (sscanf(message, "{\"type\":\"%63[^\"]\"}", action) == 1) {
                    if (strcmp(action, "admin_action") == 0) {
                        if (!has_role(session->roles, "admin")) {
                            lwsl_user("Unauthorized: user %s attempted admin action\n",
                                     session->user_id);
                            const char *resp = "{\"type\":\"unauthorized\"}";
                            unsigned char buf[LWS_PRE + 256];
                            memcpy(&buf[LWS_PRE], resp, strlen(resp));
                            lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                            return 0;
                        }
                        
                        lwsl_user("User %s performed admin action\n", session->user_id);
                        // Process admin action...
                    }
                }
            }
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            remove_session(wsi);
            lwsl_user("Connection closed\n");
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
        "websocket-protocol",
        callback_websocket,
        0,
        4096,
    },
    { NULL, NULL, 0, 0 } /* terminator */
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
        lwsl_err("lws init failed\n");
        return -1;
    }
    
    lwsl_user("WebSocket server started on port 8080\n");
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

Using `tokio-tungstenite` with JWT authentication:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use serde::{Deserialize, Serialize};
use jsonwebtoken::{encode, decode, Header, Validation, EncodingKey, DecodingKey};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use std::time::{SystemTime, UNIX_EPOCH};

// JWT Claims structure
#[derive(Debug, Serialize, Deserialize, Clone)]
struct Claims {
    sub: String,           // Subject (user ID)
    roles: Vec<String>,    // User roles
    exp: usize,            // Expiry timestamp
}

// Client session
#[derive(Debug, Clone)]
struct Session {
    user_id: String,
    roles: Vec<String>,
    authenticated: bool,
    token_expiry: u64,
}

// Message types
#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "type")]
enum ClientMessage {
    #[serde(rename = "auth")]
    Auth { token: String },
    
    #[serde(rename = "subscribe")]
    Subscribe { channel: String },
    
    #[serde(rename = "admin_action")]
    AdminAction { action: String },
    
    #[serde(rename = "message")]
    Message { content: String },
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "type")]
enum ServerMessage {
    #[serde(rename = "auth_required")]
    AuthRequired,
    
    #[serde(rename = "auth_success")]
    AuthSuccess { user_id: String },
    
    #[serde(rename = "auth_failed")]
    AuthFailed { reason: String },
    
    #[serde(rename = "unauthorized")]
    Unauthorized { reason: String },
    
    #[serde(rename = "token_expired")]
    TokenExpired,
    
    #[serde(rename = "subscribed")]
    Subscribed { channel: String },
    
    #[serde(rename = "error")]
    Error { message: String },
}

// Session manager
type SessionStore = Arc<RwLock<HashMap<String, Session>>>;

// JWT secret (in production, load from environment)
const JWT_SECRET: &[u8] = b"your-secret-key-change-in-production";

// Create a JWT token (for testing)
fn create_token(user_id: &str, roles: Vec<String>) -> Result<String, jsonwebtoken::errors::Error> {
    let expiration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs() + 3600; // 1 hour
    
    let claims = Claims {
        sub: user_id.to_string(),
        roles,
        exp: expiration as usize,
    };
    
    encode(
        &Header::default(),
        &claims,
        &EncodingKey::from_secret(JWT_SECRET),
    )
}

// Validate JWT token
fn validate_token(token: &str) -> Result<Claims, jsonwebtoken::errors::Error> {
    let validation = Validation::default();
    let token_data = decode::<Claims>(
        token,
        &DecodingKey::from_secret(JWT_SECRET),
        &validation,
    )?;
    
    Ok(token_data.claims)
}

// Check if user has required role
fn has_role(session: &Session, required_role: &str) -> bool {
    session.roles.iter().any(|r| r == required_role)
}

// Handle individual WebSocket connection
async fn handle_connection(
    stream: TcpStream,
    addr: std::net::SocketAddr,
    sessions: SessionStore,
) {
    println!("New connection from: {}", addr);
    
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("Error during WebSocket handshake: {}", e);
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    let session_id = addr.to_string();
    
    // Send authentication request
    let auth_req = ServerMessage::AuthRequired;
    let msg = serde_json::to_string(&auth_req).unwrap();
    if write.send(Message::Text(msg)).await.is_err() {
        return;
    }
    
    // Message handling loop
    while let Some(msg) = read.next().await {
        let msg = match msg {
            Ok(msg) => msg,
            Err(e) => {
                eprintln!("Error receiving message: {}", e);
                break;
            }
        };
        
        if let Message::Text(text) = msg {
            // Parse incoming message
            let client_msg: Result<ClientMessage, _> = serde_json::from_str(&text);
            
            match client_msg {
                Ok(ClientMessage::Auth { token }) => {
                    // Validate token
                    match validate_token(&token) {
                        Ok(claims) => {
                            // Check token expiry
                            let now = SystemTime::now()
                                .duration_since(UNIX_EPOCH)
                                .unwrap()
                                .as_secs();
                            
                            if claims.exp < now as usize {
                                let response = ServerMessage::TokenExpired;
                                let msg = serde_json::to_string(&response).unwrap();
                                let _ = write.send(Message::Text(msg)).await;
                                break;
                            }
                            
                            // Create session
                            let session = Session {
                                user_id: claims.sub.clone(),
                                roles: claims.roles.clone(),
                                authenticated: true,
                                token_expiry: claims.exp as u64,
                            };
                            
                            sessions.write().await.insert(session_id.clone(), session);
                            
                            println!("User {} authenticated with roles: {:?}", 
                                    claims.sub, claims.roles);
                            
                            let response = ServerMessage::AuthSuccess {
                                user_id: claims.sub,
                            };
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                        }
                        Err(e) => {
                            eprintln!("Token validation failed: {}", e);
                            let response = ServerMessage::AuthFailed {
                                reason: "Invalid token".to_string(),
                            };
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                            break;
                        }
                    }
                }
                
                Ok(ClientMessage::Subscribe { channel }) => {
                    // Check authentication
                    let sessions_read = sessions.read().await;
                    if let Some(session) = sessions_read.get(&session_id) {
                        if !session.authenticated {
                            let response = ServerMessage::AuthRequired;
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                            continue;
                        }
                        
                        // Check token expiry
                        let now = SystemTime::now()
                            .duration_since(UNIX_EPOCH)
                            .unwrap()
                            .as_secs();
                        
                        if session.token_expiry < now {
                            let response = ServerMessage::TokenExpired;
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                            break;
                        }
                        
                        // Authorization check for premium channels
                        if channel.starts_with("premium_") && !has_role(session, "premium") {
                            let response = ServerMessage::Unauthorized {
                                reason: "Premium subscription required".to_string(),
                            };
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                            continue;
                        }
                        
                        println!("User {} subscribed to channel: {}", 
                                session.user_id, channel);
                        
                        let response = ServerMessage::Subscribed { channel };
                        let msg = serde_json::to_string(&response).unwrap();
                        let _ = write.send(Message::Text(msg)).await;
                    } else {
                        let response = ServerMessage::AuthRequired;
                        let msg = serde_json::to_string(&response).unwrap();
                        let _ = write.send(Message::Text(msg)).await;
                    }
                }
                
                Ok(ClientMessage::AdminAction { action }) => {
                    // Require admin role
                    let sessions_read = sessions.read().await;
                    if let Some(session) = sessions_read.get(&session_id) {
                        if !has_role(session, "admin") {
                            println!("Unauthorized: user {} attempted admin action", 
                                    session.user_id);
                            
                            let response = ServerMessage::Unauthorized {
                                reason: "Admin role required".to_string(),
                            };
                            let msg = serde_json::to_string(&response).unwrap();
                            let _ = write.send(Message::Text(msg)).await;
                            continue;
                        }
                        
                        println!("User {} performed admin action: {}", 
                                session.user_id, action);
                        
                        // Process admin action...
                    }
                }
                
                Ok(ClientMessage::Message { content }) => {
                    // Regular message - just requires authentication
                    let sessions_read = sessions.read().await;
                    if let Some(session) = sessions_read.get(&session_id) {
                        println!("Message from {}: {}", session.user_id, content);
                    } else {
                        let response = ServerMessage::AuthRequired;
                        let msg = serde_json::to_string(&response).unwrap();
                        let _ = write.send(Message::Text(msg)).await;
                    }
                }
                
                Err(e) => {
                    eprintln!("Failed to parse message: {}", e);
                    let response = ServerMessage::Error {
                        message: "Invalid message format".to_string(),
                    };
                    let msg = serde_json::to_string(&response).unwrap();
                    let _ = write.send(Message::Text(msg)).await;
                }
            }
        } else if let Message::Close(_) = msg {
            break;
        }
    }
    
    // Clean up session
    sessions.write().await.remove(&session_id);
    println!("Connection closed: {}", addr);
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(&addr).await.expect("Failed to bind");
    println!("WebSocket server listening on: {}", addr);
    
    // Shared session store
    let sessions: SessionStore = Arc::new(RwLock::new(HashMap::new()));
    
    // Example: Create a test token
    let test_token = create_token("user123", vec!["user".to_string(), "premium".to_string()])
        .expect("Failed to create token");
    println!("\nTest token for user123 with roles [user, premium]:");
    println!("{}\n", test_token);
    
    let admin_token = create_token("admin456", vec!["admin".to_string(), "user".to_string()])
        .expect("Failed to create token");
    println!("Test token for admin456 with roles [admin, user]:");
    println!("{}\n", admin_token);
    
    // Accept connections
    while let Ok((stream, addr)) = listener.accept().await {
        let sessions = Arc::clone(&sessions);
        tokio::spawn(handle_connection(stream, addr, sessions));
    }
}
```

Client example to demonstrate usage:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use serde::{Deserialize, Serialize};
use url::Url;

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "type")]
enum ClientMessage {
    #[serde(rename = "auth")]
    Auth { token: String },
    
    #[serde(rename = "subscribe")]
    Subscribe { channel: String },
    
    #[serde(rename = "admin_action")]
    AdminAction { action: String },
    
    #[serde(rename = "message")]
    Message { content: String },
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "type")]
enum ServerMessage {
    #[serde(rename = "auth_required")]
    AuthRequired,
    
    #[serde(rename = "auth_success")]
    AuthSuccess { user_id: String },
    
    #[serde(rename = "auth_failed")]
    AuthFailed { reason: String },
    
    #[serde(rename = "unauthorized")]
    Unauthorized { reason: String },
    
    #[serde(rename = "token_expired")]
    TokenExpired,
    
    #[serde(rename = "subscribed")]
    Subscribed { channel: String },
    
    #[serde(rename = "error")]
    Error { message: String },
}

#[tokio::main]
async fn main() {
    // Use token from server output (copy from server console)
    let token = std::env::var("WS_TOKEN")
        .expect("Set WS_TOKEN environment variable with token from server");
    
    let url = Url::parse("ws://127.0.0.1:8080").expect("Invalid URL");
    
    println!("Connecting to WebSocket server...");
    let (ws_stream, _) = connect_async(url)
        .await
        .expect("Failed to connect");
    
    println!("Connected!");
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn a task to handle incoming messages
    let read_handle = tokio::spawn(async move {
        while let Some(msg) = read.next().await {
            match msg {
                Ok(Message::Text(text)) => {
                    let server_msg: Result<ServerMessage, _> = serde_json::from_str(&text);
                    match server_msg {
                        Ok(msg) => println!("Received: {:?}", msg),
                        Err(e) => eprintln!("Failed to parse server message: {}", e),
                    }
                }
                Ok(Message::Close(_)) => {
                    println!("Connection closed by server");
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
    
    // Wait a bit for auth_required message
    tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
    
    // Send authentication
    println!("\nSending authentication...");
    let auth_msg = ClientMessage::Auth { 
        token: token.clone() 
    };
    let msg = serde_json::to_string(&auth_msg).unwrap();
    write.send(Message::Text(msg)).await.expect("Failed to send auth");
    
    // Wait for auth response
    tokio::time::sleep(tokio::time::Duration::from_millis(500)).await;
    
    // Try to subscribe to a regular channel
    println!("\nSubscribing to regular channel...");
    let subscribe_msg = ClientMessage::Subscribe {
        channel: "general".to_string(),
    };
    let msg = serde_json::to_string(&subscribe_msg).unwrap();
    write.send(Message::Text(msg)).await.expect("Failed to send subscribe");
    
    tokio::time::sleep(tokio::time::Duration::from_millis(300)).await;
    
    // Try to subscribe to a premium channel (will fail without premium role)
    println!("\nSubscribing to premium channel...");
    let subscribe_msg = ClientMessage::Subscribe {
        channel: "premium_trading".to_string(),
    };
    let msg = serde_json::to_string(&subscribe_msg).unwrap();
    write.send(Message::Text(msg)).await.expect("Failed to send subscribe");
    
    tokio::time::sleep(tokio::time::Duration::from_millis(300)).await;
    
    // Try an admin action (will fail without admin role)
    println!("\nAttempting admin action...");
    let admin_msg = ClientMessage::AdminAction {
        action: "delete_user".to_string(),
    };
    let msg = serde_json::to_string(&admin_msg).unwrap();
    write.send(Message::Text(msg)).await.expect("Failed to send admin action");
    
    tokio::time::sleep(tokio::time::Duration::from_millis(300)).await;
    
    // Send a regular message
    println!("\nSending message...");
    let chat_msg = ClientMessage::Message {
        content: "Hello, WebSocket!".to_string(),
    };
    let msg = serde_json::to_string(&chat_msg).unwrap();
    write.send(Message::Text(msg)).await.expect("Failed to send message");
    
    // Wait a bit before closing
    tokio::time::sleep(tokio::time::Duration::from_secs(2)).await;
    
    println!("\nClosing connection...");
    write.close().await.expect("Failed to close");
    
    read_handle.await.expect("Read task failed");
}

// To run this client:
// 1. Start the server
// 2. Copy one of the test tokens from server output
// 3. Run: WS_TOKEN="<token>" cargo run --example client
```

## Key Security Considerations

### 1. **Token Storage and Transmission**
- **Never** include tokens in URLs for production (visible in logs, browser history)
- Use secure WebSocket (wss://) in production
- Tokens should be stored securely on client (not localStorage for sensitive apps)

### 2. **Token Expiry and Refresh**
- Implement token expiration
- Support token refresh mechanisms
- Consider short-lived access tokens with longer-lived refresh tokens

### 3. **Connection Hijacking Prevention**
- Bind tokens to specific connection metadata (IP address, user agent)
- Implement rate limiting per user/connection
- Monitor for unusual connection patterns

### 4. **Authorization Granularity**
- Implement role-based access control (RBAC)
- Use permission-based authorization for fine-grained control
- Validate authorization for each action, not just at connection time

### 5. **Session Management**
- Track active sessions per user
- Implement session limits
- Provide session termination endpoints
- Clean up sessions on disconnect

### 6. **Audit Logging**
- Log all authentication attempts (success and failure)
- Log authorization failures
- Track privileged operations
- Include user context in logs

---

## Common Authentication Patterns

### Pattern 1: Query Parameter Authentication
```
wss://example.com/ws?token=eyJhbGc...
```
**Pros:** Simple, works with all clients  
**Cons:** Tokens visible in logs, not ideal for sensitive data

### Pattern 2: Header-Based Authentication
```
Authorization: Bearer eyJhbGc...
```
**Pros:** More secure, standard approach  
**Cons:** Not all WebSocket clients support custom headers easily

### Pattern 3: Post-Connection Authentication
```
Client connects → Server requests auth → Client sends token → Server validates
```
**Pros:** Most flexible, supports complex auth flows  
**Cons:** Requires additional round trip, more complex implementation

### Pattern 4: Cookie-Based Authentication
```
Cookie: session_id=abc123...
```
**Pros:** Works with existing session infrastructure  
**Cons:** CSRF considerations, not suitable for cross-origin

---

## Summary

**WebSocket Authentication and Authorization** is critical for securing real-time communication channels. Key takeaways:

1. **Multi-Layer Security**: Authenticate during handshake and validate throughout connection lifecycle
2. **Token-Based Auth**: JWT tokens are the most common approach, providing stateless authentication with embedded claims
3. **Session Management**: Maintain server-side session state to track authenticated users and enforce policies
4. **Authorization Controls**: Implement role-based or permission-based access control for different operations
5. **Security Best Practices**: 
   - Use secure WebSocket connections (wss://)
   - Implement token expiration and refresh
   - Validate authorization for every sensitive operation
   - Audit authentication and authorization events
   - Handle token expiry gracefully
   - Never trust client-side data

6. **Language Considerations**:
   - **C/C++**: Lower-level control, manual memory management, use libraries like libwebsockets with OpenSSL for crypto
   - **Rust**: Memory safety, excellent concurrency support, rich ecosystem (tokio, jsonwebtoken)

The implementations shown demonstrate practical patterns for production use, including JWT validation, role-based authorization, session management, and secure message handling. Always adapt these patterns to your specific security requirements and regulatory compliance needs.