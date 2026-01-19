// WebSocket Gateway with Microservices Integration (Rust)
// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// tokio-tungstenite = "0.20"
// futures-util = "0.3"
// redis = { version = "0.23", features = ["tokio-comp", "connection-manager"] }
// serde = { version = "1", features = ["derive"] }
// serde_json = "1"
// tonic = "0.10"
// uuid = { version = "1", features = ["v4"] }

use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use redis::{Client, AsyncCommands, aio::ConnectionManager};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{RwLock, mpsc};
use uuid::Uuid;

#[derive(Clone, Debug, Serialize, Deserialize)]
struct ClientMessage {
    action: String,
    #[serde(default)]
    channel: Option<String>,
    #[serde(default)]
    service: Option<String>,
    #[serde(default)]
    request: Option<serde_json::Value>,
    #[serde(default)]
    event: Option<serde_json::Value>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct ServerMessage {
    msg_type: String,
    data: serde_json::Value,
}

#[derive(Clone)]
struct Session {
    id: String,
    user_id: String,
    subscriptions: Vec<String>,
    tx: mpsc::UnboundedSender<Message>,
}

type Sessions = Arc<RwLock<HashMap<String, Session>>>;

// Microservices communication layer
struct MicroservicesBridge {
    redis_conn: ConnectionManager,
    sessions: Sessions,
}

impl MicroservicesBridge {
    async fn new(redis_url: &str, sessions: Sessions) -> Result<Self, redis::RedisError> {
        let client = Client::open(redis_url)?;
        let redis_conn = ConnectionManager::new(client).await?;
        
        Ok(Self {
            redis_conn,
            sessions,
        })
    }
    
    // Publish event to microservices
    async fn publish_event(&mut self, channel: &str, event: &serde_json::Value) 
        -> Result<(), redis::RedisError> {
        let event_str = serde_json::to_string(event).unwrap();
        let full_channel = format!("events.{}", channel);
        
        self.redis_conn.publish(&full_channel, event_str).await?;
        println!("Published event to channel: {}", full_channel);
        
        Ok(())
    }
    
    // Call microservice using request/response pattern
    async fn call_service(&mut self, service: &str, request: &serde_json::Value) 
        -> Result<serde_json::Value, Box<dyn std::error::Error>> {
        let response_channel = format!("response_{}", Uuid::new_v4());
        let request_channel = format!("service.{}", service);
        
        // Create request with response channel
        let req = serde_json::json!({
            "response_channel": response_channel,
            "payload": request
        });
        
        // Publish request
        let req_str = serde_json::to_string(&req)?;
        self.redis_conn.publish(&request_channel, req_str).await?;
        
        // Wait for response (with timeout)
        let response: Vec<String> = tokio::time::timeout(
            std::time::Duration::from_secs(5),
            self.redis_conn.blpop(&response_channel, 0.0)
        ).await???;
        
        if response.len() > 1 {
            Ok(serde_json::from_str(&response[1])?)
        } else {
            Err("No response from service".into())
        }
    }
    
    // Subscribe to microservice events and broadcast to WebSocket clients
    async fn start_event_subscriber(sessions: Sessions, redis_url: String) {
        tokio::spawn(async move {
            let client = Client::open(redis_url.as_str()).unwrap();
            let mut conn = client.get_async_connection().await.unwrap();
            let mut pubsub = conn.into_pubsub();
            
            // Subscribe to all event channels
            pubsub.psubscribe("events.*").await.unwrap();
            println!("Subscribed to microservice events");
            
            let mut stream = pubsub.on_message();
            
            while let Some(msg) = stream.next().await {
                let channel: String = msg.get_channel_name().to_string();
                let payload: String = msg.get_payload().unwrap();
                
                if let Ok(event) = serde_json::from_str::<serde_json::Value>(&payload) {
                    // Broadcast to subscribed clients
                    let sessions = sessions.read().await;
                    
                    for (_, session) in sessions.iter() {
                        // Check if client is subscribed to this channel
                        let subscribed = session.subscriptions.iter()
                            .any(|sub| channel.contains(sub));
                        
                        if subscribed {
                            let server_msg = ServerMessage {
                                msg_type: "event".to_string(),
                                data: event.clone(),
                            };
                            
                            let msg_json = serde_json::to_string(&server_msg).unwrap();
                            let _ = session.tx.send(Message::Text(msg_json));
                        }
                    }
                }
            }
        });
    }
}

// Handle individual WebSocket connection
async fn handle_connection(
    stream: TcpStream,
    sessions: Sessions,
    redis_url: String,
) {
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed: {}", e);
            return;
        }
    };
    
    println!("New WebSocket connection established");
    
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    let (tx, mut rx) = mpsc::unbounded_channel();
    
    // Create session
    let session_id = Uuid::new_v4().to_string();
    let session = Session {
        id: session_id.clone(),
        user_id: format!("user_{}", Uuid::new_v4()),
        subscriptions: Vec::new(),
        tx: tx.clone(),
    };
    
    sessions.write().await.insert(session_id.clone(), session.clone());
    
    // Create microservices bridge
    let mut bridge = MicroservicesBridge::new(&redis_url, sessions.clone())
        .await
        .unwrap();
    
    // Spawn task to send messages to WebSocket client
    tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_sender.send(msg).await.is_err() {
                break;
            }
        }
    });
    
    // Handle incoming WebSocket messages
    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                if let Ok(client_msg) = serde_json::from_str::<ClientMessage>(&text) {
                    match client_msg.action.as_str() {
                        "subscribe" => {
                            if let Some(channel) = client_msg.channel {
                                let mut sessions_write = sessions.write().await;
                                if let Some(session) = sessions_write.get_mut(&session_id) {
                                    session.subscriptions.push(channel.clone());
                                    println!("Client subscribed to: {}", channel);
                                }
                            }
                        },
                        
                        "call_service" => {
                            if let (Some(service), Some(request)) = 
                                (client_msg.service, client_msg.request) {
                                match bridge.call_service(&service, &request).await {
                                    Ok(response) => {
                                        let server_msg = ServerMessage {
                                            msg_type: "service_response".to_string(),
                                            data: response,
                                        };
                                        let msg_json = serde_json::to_string(&server_msg).unwrap();
                                        let _ = tx.send(Message::Text(msg_json));
                                    },
                                    Err(e) => {
                                        eprintln!("Service call failed: {}", e);
                                    }
                                }
                            }
                        },
                        
                        "publish_event" => {
                            if let (Some(channel), Some(event)) = 
                                (client_msg.channel, client_msg.event) {
                                let _ = bridge.publish_event(&channel, &event).await;
                            }
                        },
                        
                        _ => {
                            println!("Unknown action: {}", client_msg.action);
                        }
                    }
                }
            },
            
            Ok(Message::Close(_)) => {
                println!("Client closed connection");
                break;
            },
            
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            },
            
            _ => {}
        }
    }
    
    // Clean up session
    sessions.write().await.remove(&session_id);
    println!("Session removed: {}", session_id);
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:8080";
    let redis_url = "redis://127.0.0.1:6379";
    
    let listener = TcpListener::bind(addr).await.unwrap();
    println!("WebSocket Gateway listening on: {}", addr);
    println!("Connecting to Redis at: {}", redis_url);
    
    let sessions: Sessions = Arc::new(RwLock::new(HashMap::new()));
    
    // Start event subscriber
    MicroservicesBridge::start_event_subscriber(
        sessions.clone(),
        redis_url.to_string()
    ).await;
    
    // Accept WebSocket connections
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        
        let sessions = sessions.clone();
        let redis_url = redis_url.to_string();
        
        tokio::spawn(handle_connection(stream, sessions, redis_url));
    }
}

// Example microservice event publisher (separate process)
#[allow(dead_code)]
async fn example_microservice_publisher() {
    let client = redis::Client::open("redis://127.0.0.1:6379").unwrap();
    let mut conn = client.get_async_connection().await.unwrap();
    
    loop {
        tokio::time::sleep(std::time::Duration::from_secs(5)).await;
        
        let event = serde_json::json!({
            "timestamp": chrono::Utc::now().to_rfc3339(),
            "event_type": "user_activity",
            "data": {
                "user_id": "12345",
                "action": "login"
            }
        });
        
        let event_str = serde_json::to_string(&event).unwrap();
        let _: () = conn.publish("events.user_activity", event_str).await.unwrap();
        
        println!("Published event from microservice");
    }
}