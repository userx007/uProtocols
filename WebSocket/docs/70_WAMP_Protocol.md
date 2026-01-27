# WAMP Protocol: Detailed Description and Programming Guide

## Overview

The **Web Application Messaging Protocol (WAMP)** is an open WebSocket subprotocol that provides two messaging patterns in one unified protocol:

1. **Remote Procedure Calls (RPC)** - for calling remote functions
2. **Publish & Subscribe (PubSub)** - for event distribution

WAMP enables application components to communicate in distributed systems using JSON or MessagePack serialization. It's designed for real-time applications requiring both request-response and event-driven communication.

## Core Concepts

### Architecture

WAMP uses a **Router** (or Broker/Dealer) that acts as a message mediator between clients. Clients connect to the router and can assume four roles:

- **Caller** - invokes remote procedures
- **Callee** - provides procedures that can be called remotely
- **Publisher** - publishes events to topics
- **Subscriber** - subscribes to topics to receive events

### Message Types

WAMP defines several message types, each identified by an integer code:

- `HELLO` (1) - Client authenticates with realm
- `WELCOME` (2) - Router accepts client
- `CALL` (48) - Invoke a remote procedure
- `RESULT` (50) - Return result of procedure call
- `PUBLISH` (16) - Publish event to topic
- `SUBSCRIBE` (32) - Subscribe to topic
- `SUBSCRIBED` (33) - Subscription acknowledgment
- `EVENT` (36) - Receive published event

## C/C++ Implementation

Here's a practical WAMP client implementation using C++ with the WebSocket++ library:

```cpp
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <json/json.h>
#include <iostream>
#include <string>

typedef websocketpp::client<websocketpp::config::asio_client> client;

class WAMPClient {
private:
    client ws_client;
    websocketpp::connection_hdl hdl;
    uint64_t session_id;
    uint64_t request_id;
    
    std::string realm;
    
public:
    WAMPClient(const std::string& realm_name) 
        : realm(realm_name), session_id(0), request_id(1) {
        ws_client.init_asio();
        
        ws_client.set_open_handler([this](websocketpp::connection_hdl h) {
            this->on_open(h);
        });
        
        ws_client.set_message_handler([this](websocketpp::connection_hdl h, 
                                             client::message_ptr msg) {
            this->on_message(h, msg);
        });
    }
    
    void connect(const std::string& uri) {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = ws_client.get_connection(uri, ec);
        
        if (ec) {
            std::cout << "Connection error: " << ec.message() << std::endl;
            return;
        }
        
        hdl = con->get_handle();
        ws_client.connect(con);
        ws_client.run();
    }
    
    void on_open(websocketpp::connection_hdl h) {
        std::cout << "Connected to WAMP router" << std::endl;
        send_hello();
    }
    
    void send_hello() {
        // HELLO message format: [HELLO, Realm|uri, Details|dict]
        Json::Value hello(Json::arrayValue);
        hello.append(1); // HELLO message type
        hello.append(realm);
        
        Json::Value details(Json::objectValue);
        details["roles"]["caller"] = Json::objectValue();
        details["roles"]["subscriber"] = Json::objectValue();
        hello.append(details);
        
        Json::FastWriter writer;
        std::string message = writer.write(hello);
        
        ws_client.send(hdl, message, websocketpp::frame::opcode::text);
    }
    
    void on_message(websocketpp::connection_hdl h, client::message_ptr msg) {
        Json::Value root;
        Json::Reader reader;
        
        if (!reader.parse(msg->get_payload(), root)) {
            std::cerr << "Failed to parse message" << std::endl;
            return;
        }
        
        if (!root.isArray() || root.size() < 1) {
            return;
        }
        
        int message_type = root[0].asInt();
        
        switch (message_type) {
            case 2: // WELCOME
                handle_welcome(root);
                break;
            case 50: // RESULT
                handle_result(root);
                break;
            case 36: // EVENT
                handle_event(root);
                break;
            default:
                std::cout << "Unhandled message type: " << message_type << std::endl;
        }
    }
    
    void handle_welcome(const Json::Value& msg) {
        session_id = msg[1].asUInt64();
        std::cout << "Session established with ID: " << session_id << std::endl;
    }
    
    void call_procedure(const std::string& procedure, const Json::Value& args) {
        // CALL message: [CALL, Request|id, Options|dict, Procedure|uri, Arguments|list]
        Json::Value call_msg(Json::arrayValue);
        call_msg.append(48); // CALL
        call_msg.append(request_id++);
        call_msg.append(Json::objectValue()); // empty options
        call_msg.append(procedure);
        call_msg.append(args);
        
        Json::FastWriter writer;
        std::string message = writer.write(call_msg);
        
        ws_client.send(hdl, message, websocketpp::frame::opcode::text);
    }
    
    void subscribe(const std::string& topic) {
        // SUBSCRIBE message: [SUBSCRIBE, Request|id, Options|dict, Topic|uri]
        Json::Value sub_msg(Json::arrayValue);
        sub_msg.append(32); // SUBSCRIBE
        sub_msg.append(request_id++);
        sub_msg.append(Json::objectValue());
        sub_msg.append(topic);
        
        Json::FastWriter writer;
        std::string message = writer.write(sub_msg);
        
        ws_client.send(hdl, message, websocketpp::frame::opcode::text);
    }
    
    void publish(const std::string& topic, const Json::Value& args) {
        // PUBLISH message: [PUBLISH, Request|id, Options|dict, Topic|uri, Arguments|list]
        Json::Value pub_msg(Json::arrayValue);
        pub_msg.append(16); // PUBLISH
        pub_msg.append(request_id++);
        pub_msg.append(Json::objectValue());
        pub_msg.append(topic);
        pub_msg.append(args);
        
        Json::FastWriter writer;
        std::string message = writer.write(pub_msg);
        
        ws_client.send(hdl, message, websocketpp::frame::opcode::text);
    }
    
    void handle_result(const Json::Value& msg) {
        uint64_t req_id = msg[1].asUInt64();
        Json::Value result = msg[3];
        
        std::cout << "Result for request " << req_id << ": " 
                  << result.toStyledString() << std::endl;
    }
    
    void handle_event(const Json::Value& msg) {
        uint64_t subscription_id = msg[1].asUInt64();
        uint64_t publication_id = msg[2].asUInt64();
        Json::Value args = msg[4];
        
        std::cout << "Event received on subscription " << subscription_id 
                  << ": " << args.toStyledString() << std::endl;
    }
};

// Usage example
int main() {
    WAMPClient client("realm1");
    
    // Connect to WAMP router
    client.connect("ws://localhost:8080/ws");
    
    // Subscribe to a topic
    client.subscribe("com.myapp.events");
    
    // Publish an event
    Json::Value args(Json::arrayValue);
    args.append("Hello from C++");
    client.publish("com.myapp.events", args);
    
    // Call a remote procedure
    Json::Value call_args(Json::arrayValue);
    call_args.append(42);
    call_args.append(23);
    client.call_procedure("com.myapp.add", call_args);
    
    return 0;
}
```

## Rust Implementation

Here's a WAMP client implementation in Rust using the `tokio-tungstenite` crate:

```rust
use tokio_tungstenite::{connect_async, tungstenite::protocol::Message};
use futures_util::{StreamExt, SinkExt};
use serde_json::{json, Value};
use std::sync::atomic::{AtomicU64, Ordering};

struct WAMPClient {
    realm: String,
    session_id: Option<u64>,
    request_id: AtomicU64,
}

impl WAMPClient {
    fn new(realm: &str) -> Self {
        WAMPClient {
            realm: realm.to_string(),
            session_id: None,
            request_id: AtomicU64::new(1),
        }
    }
    
    fn next_request_id(&self) -> u64 {
        self.request_id.fetch_add(1, Ordering::SeqCst)
    }
    
    fn create_hello(&self) -> Value {
        json!([
            1,  // HELLO message type
            self.realm,
            {
                "roles": {
                    "caller": {},
                    "subscriber": {},
                    "publisher": {}
                }
            }
        ])
    }
    
    fn create_subscribe(&self, topic: &str) -> Value {
        json!([
            32,  // SUBSCRIBE
            self.next_request_id(),
            {},  // options
            topic
        ])
    }
    
    fn create_publish(&self, topic: &str, args: Value) -> Value {
        json!([
            16,  // PUBLISH
            self.next_request_id(),
            {},  // options
            topic,
            args
        ])
    }
    
    fn create_call(&self, procedure: &str, args: Value) -> Value {
        json!([
            48,  // CALL
            self.next_request_id(),
            {},  // options
            procedure,
            args
        ])
    }
    
    fn handle_message(&mut self, msg: Value) {
        if let Some(msg_array) = msg.as_array() {
            if msg_array.is_empty() {
                return;
            }
            
            let msg_type = msg_array[0].as_u64().unwrap_or(0);
            
            match msg_type {
                2 => self.handle_welcome(&msg_array),
                33 => self.handle_subscribed(&msg_array),
                36 => self.handle_event(&msg_array),
                50 => self.handle_result(&msg_array),
                _ => println!("Unhandled message type: {}", msg_type),
            }
        }
    }
    
    fn handle_welcome(&mut self, msg: &[Value]) {
        if msg.len() > 1 {
            self.session_id = msg[1].as_u64();
            println!("Session established with ID: {:?}", self.session_id);
        }
    }
    
    fn handle_subscribed(&self, msg: &[Value]) {
        if msg.len() > 2 {
            let request_id = msg[1].as_u64().unwrap_or(0);
            let subscription_id = msg[2].as_u64().unwrap_or(0);
            println!("Subscribed: request={}, subscription={}", 
                     request_id, subscription_id);
        }
    }
    
    fn handle_event(&self, msg: &[Value]) {
        if msg.len() > 4 {
            let subscription_id = msg[1].as_u64().unwrap_or(0);
            let publication_id = msg[2].as_u64().unwrap_or(0);
            let args = &msg[4];
            
            println!("Event received:");
            println!("  Subscription: {}", subscription_id);
            println!("  Publication: {}", publication_id);
            println!("  Args: {}", args);
        }
    }
    
    fn handle_result(&self, msg: &[Value]) {
        if msg.len() > 3 {
            let request_id = msg[1].as_u64().unwrap_or(0);
            let result = &msg[3];
            
            println!("Result for request {}:", request_id);
            println!("  {}", result);
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let url = "ws://localhost:8080/ws";
    let (ws_stream, _) = connect_async(url).await?;
    println!("Connected to WAMP router");
    
    let (mut write, mut read) = ws_stream.split();
    let mut client = WAMPClient::new("realm1");
    
    // Send HELLO message
    let hello = client.create_hello();
    write.send(Message::Text(hello.to_string())).await?;
    
    // Message processing loop
    tokio::spawn(async move {
        while let Some(msg) = read.next().await {
            match msg {
                Ok(Message::Text(text)) => {
                    if let Ok(parsed) = serde_json::from_str::<Value>(&text) {
                        client.handle_message(parsed);
                    }
                }
                Ok(Message::Close(_)) => {
                    println!("Connection closed");
                    break;
                }
                Err(e) => {
                    eprintln!("Error receiving message: {}", e);
                    break;
                }
                _ => {}
            }
        }
    });
    
    // Give time for WELCOME message
    tokio::time::sleep(tokio::time::Duration::from_millis(500)).await;
    
    // Subscribe to a topic
    let subscribe_msg = client.create_subscribe("com.myapp.events");
    write.send(Message::Text(subscribe_msg.to_string())).await?;
    
    // Publish an event
    let publish_msg = client.create_publish(
        "com.myapp.events",
        json!(["Hello from Rust!"])
    );
    write.send(Message::Text(publish_msg.to_string())).await?;
    
    // Call a remote procedure
    let call_msg = client.create_call(
        "com.myapp.add",
        json!([42, 23])
    );
    write.send(Message::Text(call_msg.to_string())).await?;
    
    // Keep the connection alive
    tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
    
    Ok(())
}
```

### Cargo.toml for Rust example:

```toml
[dependencies]
tokio = { version = "1.35", features = ["full"] }
tokio-tungstenite = "0.21"
futures-util = "0.3"
serde_json = "1.0"
```

## Summary

**WAMP Protocol** is a powerful WebSocket subprotocol that unifies RPC and PubSub patterns into a single framework. Key characteristics:

**Strengths:**
- Dual messaging patterns (RPC + PubSub) in one protocol
- Language-agnostic with implementations in many languages
- Router-based architecture simplifies peer-to-peer complexity
- Support for both JSON and MessagePack serialization
- Advanced features like authentication, pattern-based subscriptions, and distributed calls

**Use Cases:**
- Real-time dashboards and monitoring systems
- IoT device communication and control
- Multiplayer games requiring both events and RPC
- Microservices architectures with mixed communication patterns
- Collaborative applications with real-time synchronization

**Considerations:**
- Requires a WAMP router (Crossbar.io, Autobahn, etc.)
- More complex than simple WebSocket communication
- Additional overhead compared to custom protocols
- Learning curve for understanding session management and message flows

WAMP excels when applications need both request-response and event-driven communication, providing a standardized, well-designed protocol that eliminates the need to build custom messaging infrastructure.