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