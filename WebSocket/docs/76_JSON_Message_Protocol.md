# JSON Message Protocol for WebSocket Communication

## Overview

JSON Message Protocol refers to structuring WebSocket messages using JSON (JavaScript Object Notation) as the serialization format. This approach provides a standardized, human-readable way to exchange structured data between clients and servers over WebSocket connections.

## Key Concepts

**Why JSON for WebSockets?**
- **Human-readable**: Easy to debug and inspect messages
- **Language-agnostic**: Native support across virtually all programming languages
- **Flexible schema**: Can evolve over time without breaking compatibility
- **Self-describing**: Field names provide context without external documentation

**Common Message Structure**

A typical JSON message protocol includes:
- **Message type/action**: Identifies the purpose of the message
- **Payload/data**: The actual content being transmitted
- **Metadata**: Timestamps, IDs, version info, etc.
- **Error handling**: Status codes or error descriptions

## C/C++ Implementation

Here's a WebSocket JSON message protocol implementation using C++ with the `websocketpp` and `nlohmann/json` libraries:

```cpp
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <set>

using json = nlohmann::json;
using websocketpp::connection_hdl;
typedef websocketpp::server<websocketpp::config::asio> server;

class WebSocketJSONServer {
private:
    server ws_server;
    std::set<connection_hdl, std::owner_less<connection_hdl>> connections;

    // Create a standardized JSON message
    json create_message(const std::string& type, const json& data, 
                       const std::string& status = "success") {
        json msg;
        msg["type"] = type;
        msg["timestamp"] = std::time(nullptr);
        msg["status"] = status;
        msg["data"] = data;
        return msg;
    }

    // Handle incoming messages
    void on_message(connection_hdl hdl, server::message_ptr msg) {
        try {
            // Parse incoming JSON
            json incoming = json::parse(msg->get_payload());
            
            std::string msg_type = incoming["type"];
            std::cout << "Received message type: " << msg_type << std::endl;

            // Route based on message type
            if (msg_type == "chat") {
                handle_chat_message(hdl, incoming);
            } else if (msg_type == "subscribe") {
                handle_subscribe(hdl, incoming);
            } else if (msg_type == "ping") {
                handle_ping(hdl);
            } else {
                send_error(hdl, "Unknown message type");
            }
        } catch (json::exception& e) {
            send_error(hdl, "Invalid JSON: " + std::string(e.what()));
        }
    }

    void handle_chat_message(connection_hdl hdl, const json& msg) {
        json response = create_message("chat", {
            {"user", msg["data"]["user"]},
            {"message", msg["data"]["message"]},
            {"room", msg["data"]["room"]}
        });

        // Broadcast to all connections
        broadcast(response.dump());
    }

    void handle_subscribe(connection_hdl hdl, const json& msg) {
        std::string channel = msg["data"]["channel"];
        
        json response = create_message("subscribed", {
            {"channel", channel},
            {"message", "Successfully subscribed"}
        });

        ws_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }

    void handle_ping(connection_hdl hdl) {
        json response = create_message("pong", {
            {"timestamp", std::time(nullptr)}
        });

        ws_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }

    void send_error(connection_hdl hdl, const std::string& error_msg) {
        json response = create_message("error", {
            {"message", error_msg}
        }, "error");

        ws_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }

    void broadcast(const std::string& message) {
        for (auto& hdl : connections) {
            ws_server.send(hdl, message, websocketpp::frame::opcode::text);
        }
    }

    void on_open(connection_hdl hdl) {
        connections.insert(hdl);
        
        json welcome = create_message("welcome", {
            {"message", "Connected to server"},
            {"protocol_version", "1.0"}
        });

        ws_server.send(hdl, welcome.dump(), websocketpp::frame::opcode::text);
    }

    void on_close(connection_hdl hdl) {
        connections.erase(hdl);
    }

public:
    WebSocketJSONServer() {
        ws_server.set_message_handler(
            [this](connection_hdl hdl, server::message_ptr msg) {
                on_message(hdl, msg);
            });

        ws_server.set_open_handler(
            [this](connection_hdl hdl) {
                on_open(hdl);
            });

        ws_server.set_close_handler(
            [this](connection_hdl hdl) {
                on_close(hdl);
            });
    }

    void run(uint16_t port) {
        ws_server.init_asio();
        ws_server.listen(port);
        ws_server.start_accept();
        
        std::cout << "WebSocket JSON server running on port " << port << std::endl;
        ws_server.run();
    }
};

int main() {
    WebSocketJSONServer server;
    server.run(9002);
    return 0;
}
```

## Rust Implementation

Here's an implementation using Rust with the `tokio-tungstenite` and `serde_json` crates:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::sync::Arc;
use tokio::sync::RwLock;
use std::collections::HashMap;

// Define message structures
#[derive(Serialize, Deserialize, Debug)]
struct WebSocketMessage {
    #[serde(rename = "type")]
    msg_type: String,
    timestamp: i64,
    status: String,
    data: Value,
}

#[derive(Serialize, Deserialize, Debug)]
struct ChatData {
    user: String,
    message: String,
    room: String,
}

#[derive(Serialize, Deserialize, Debug)]
struct SubscribeData {
    channel: String,
}

type Connections = Arc<RwLock<HashMap<usize, tokio::sync::mpsc::UnboundedSender<Message>>>>;

// Helper function to create standardized messages
fn create_message(msg_type: &str, data: Value, status: &str) -> WebSocketMessage {
    WebSocketMessage {
        msg_type: msg_type.to_string(),
        timestamp: chrono::Utc::now().timestamp(),
        status: status.to_string(),
        data,
    }
}

// Handle individual client connection
async fn handle_connection(
    stream: TcpStream,
    connections: Connections,
    client_id: usize,
) {
    let ws_stream = accept_async(stream)
        .await
        .expect("Failed to accept WebSocket");

    println!("New WebSocket connection: {}", client_id);

    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel();

    // Store connection
    connections.write().await.insert(client_id, tx);

    // Send welcome message
    let welcome = create_message(
        "welcome",
        json!({
            "message": "Connected to server",
            "protocol_version": "1.0",
            "client_id": client_id
        }),
        "success",
    );
    
    let welcome_str = serde_json::to_string(&welcome).unwrap();
    let _ = ws_sender.send(Message::Text(welcome_str)).await;

    // Spawn task to send messages
    let mut send_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_sender.send(msg).await.is_err() {
                break;
            }
        }
    });

    // Handle incoming messages
    let connections_clone = connections.clone();
    let mut recv_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_receiver.next().await {
            if let Message::Text(text) = msg {
                handle_message(text, client_id, connections_clone.clone()).await;
            }
        }
    });

    // Wait for either task to finish
    tokio::select! {
        _ = &mut send_task => recv_task.abort(),
        _ = &mut recv_task => send_task.abort(),
    }

    // Clean up connection
    connections.write().await.remove(&client_id);
    println!("Client {} disconnected", client_id);
}

async fn handle_message(text: String, client_id: usize, connections: Connections) {
    match serde_json::from_str::<Value>(&text) {
        Ok(json_msg) => {
            let msg_type = json_msg["type"].as_str().unwrap_or("unknown");

            match msg_type {
                "chat" => handle_chat(json_msg, connections).await,
                "subscribe" => handle_subscribe(json_msg, client_id, connections).await,
                "ping" => handle_ping(client_id, connections).await,
                _ => send_error(client_id, "Unknown message type", connections).await,
            }
        }
        Err(e) => {
            send_error(
                client_id,
                &format!("Invalid JSON: {}", e),
                connections,
            )
            .await;
        }
    }
}

async fn handle_chat(json_msg: Value, connections: Connections) {
    if let Ok(chat_data) = serde_json::from_value::<ChatData>(json_msg["data"].clone()) {
        let response = create_message(
            "chat",
            json!({
                "user": chat_data.user,
                "message": chat_data.message,
                "room": chat_data.room
            }),
            "success",
        );

        broadcast(response, connections).await;
    }
}

async fn handle_subscribe(json_msg: Value, client_id: usize, connections: Connections) {
    if let Ok(sub_data) = serde_json::from_value::<SubscribeData>(json_msg["data"].clone()) {
        let response = create_message(
            "subscribed",
            json!({
                "channel": sub_data.channel,
                "message": "Successfully subscribed"
            }),
            "success",
        );

        send_to_client(client_id, response, connections).await;
    }
}

async fn handle_ping(client_id: usize, connections: Connections) {
    let response = create_message(
        "pong",
        json!({
            "timestamp": chrono::Utc::now().timestamp()
        }),
        "success",
    );

    send_to_client(client_id, response, connections).await;
}

async fn send_error(client_id: usize, error_msg: &str, connections: Connections) {
    let response = create_message(
        "error",
        json!({
            "message": error_msg
        }),
        "error",
    );

    send_to_client(client_id, response, connections).await;
}

async fn send_to_client(client_id: usize, msg: WebSocketMessage, connections: Connections) {
    let msg_str = serde_json::to_string(&msg).unwrap();
    let connections = connections.read().await;
    
    if let Some(tx) = connections.get(&client_id) {
        let _ = tx.send(Message::Text(msg_str));
    }
}

async fn broadcast(msg: WebSocketMessage, connections: Connections) {
    let msg_str = serde_json::to_string(&msg).unwrap();
    let connections = connections.read().await;

    for (_, tx) in connections.iter() {
        let _ = tx.send(Message::Text(msg_str.clone()));
    }
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:9002";
    let listener = TcpListener::bind(&addr).await.expect("Failed to bind");
    println!("WebSocket JSON server running on {}", addr);

    let connections: Connections = Arc::new(RwLock::new(HashMap::new()));
    let mut client_counter = 0;

    while let Ok((stream, _)) = listener.accept().await {
        client_counter += 1;
        tokio::spawn(handle_connection(stream, connections.clone(), client_counter));
    }
}
```

### Cargo.toml for Rust example:
```toml
[dependencies]
tokio = { version = "1", features = ["full"] }
tokio-tungstenite = "0.21"
futures-util = "0.3"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
chrono = "0.4"
```

## Example Message Formats

**Chat Message (Client → Server):**
```json
{
  "type": "chat",
  "data": {
    "user": "alice",
    "message": "Hello everyone!",
    "room": "general"
  }
}
```

**Subscription Request:**
```json
{
  "type": "subscribe",
  "data": {
    "channel": "stock-updates"
  }
}
```

**Server Response:**
```json
{
  "type": "subscribed",
  "timestamp": 1706371200,
  "status": "success",
  "data": {
    "channel": "stock-updates",
    "message": "Successfully subscribed"
  }
}
```

**Error Response:**
```json
{
  "type": "error",
  "timestamp": 1706371200,
  "status": "error",
  "data": {
    "message": "Invalid message format"
  }
}
```

## Summary

JSON Message Protocol for WebSockets provides a flexible, maintainable approach to real-time communication. The standardized message structure with type, timestamp, status, and data fields creates consistency across your application. Both C++ and Rust implementations demonstrate pattern-matching on message types, error handling for malformed JSON, and broadcasting capabilities. This protocol design scales well from simple chat applications to complex real-time systems, offering the perfect balance between human readability during development and machine efficiency in production environments.