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