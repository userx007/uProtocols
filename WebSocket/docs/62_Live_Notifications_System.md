# Live Notifications System - WebSocket Implementation

## Overview

A Live Notifications System uses WebSocket connections to deliver real-time push notifications to clients. Unlike traditional HTTP polling, WebSockets maintain persistent, bidirectional connections that allow servers to instantly push updates to connected clients with minimal latency and overhead.

## Core Concepts

### Why WebSockets for Notifications?

**Traditional Polling Problems:**
- High latency (delay between notification and delivery)
- Wasted bandwidth (constant polling even when no updates exist)
- Server overhead (handling repeated requests)

**WebSocket Advantages:**
- Instant delivery (server pushes immediately when events occur)
- Efficient (single persistent connection)
- Bidirectional (clients can acknowledge receipt)
- Lower resource consumption

### Architecture Components

1. **WebSocket Server**: Manages connections and broadcasts notifications
2. **Connection Manager**: Tracks active client connections
3. **Notification Queue**: Buffers notifications for delivery
4. **Event Publisher**: Generates notifications from business logic
5. **Client Handler**: Manages individual client connections

## C/C++ Implementation

### Using libwebsockets Library

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#define MAX_PAYLOAD 4096
#define MAX_CLIENTS 1000

// Notification structure
typedef struct notification {
    char user_id[64];
    char type[32];
    char message[512];
    long timestamp;
    struct notification *next;
} notification_t;

// Per-session data
struct per_session_data {
    char user_id[64];
    int authenticated;
    struct lws *wsi;
};

// Global notification queue
static notification_t *notification_queue = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct lws_context *context;

// Add notification to queue
void queue_notification(const char *user_id, const char *type, 
                       const char *message) {
    notification_t *notif = malloc(sizeof(notification_t));
    strncpy(notif->user_id, user_id, 63);
    strncpy(notif->type, type, 31);
    strncpy(notif->message, message, 511);
    notif->timestamp = time(NULL);
    
    pthread_mutex_lock(&queue_mutex);
    notif->next = notification_queue;
    notification_queue = notif;
    pthread_mutex_unlock(&queue_mutex);
    
    // Trigger WebSocket service to send notifications
    lws_cancel_service(context);
}

// WebSocket callback handler
static int callback_notifications(struct lws *wsi,
                                  enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len) {
    struct per_session_data *pss = (struct per_session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("New WebSocket connection established\n");
            pss->authenticated = 0;
            pss->wsi = wsi;
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Handle authentication and commands from client
            {
                char *msg = (char *)in;
                if (strncmp(msg, "AUTH:", 5) == 0) {
                    strncpy(pss->user_id, msg + 5, 63);
                    pss->authenticated = 1;
                    printf("User authenticated: %s\n", pss->user_id);
                    
                    // Send confirmation
                    unsigned char buf[LWS_PRE + 256];
                    unsigned char *p = &buf[LWS_PRE];
                    int n = sprintf((char *)p, 
                        "{\"type\":\"auth_success\",\"user_id\":\"%s\"}", 
                        pss->user_id);
                    lws_write(wsi, p, n, LWS_WRITE_TEXT);
                }
            }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (!pss->authenticated)
                break;
                
            // Check for pending notifications
            pthread_mutex_lock(&queue_mutex);
            notification_t *notif = notification_queue;
            notification_t *prev = NULL;
            
            while (notif) {
                if (strcmp(notif->user_id, pss->user_id) == 0) {
                    // Prepare notification JSON
                    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
                    unsigned char *p = &buf[LWS_PRE];
                    int n = snprintf((char *)p, MAX_PAYLOAD,
                        "{\"type\":\"%s\",\"message\":\"%s\",\"timestamp\":%ld}",
                        notif->type, notif->message, notif->timestamp);
                    
                    lws_write(wsi, p, n, LWS_WRITE_TEXT);
                    
                    // Remove from queue
                    if (prev)
                        prev->next = notif->next;
                    else
                        notification_queue = notif->next;
                    
                    notification_t *to_free = notif;
                    notif = notif->next;
                    free(to_free);
                } else {
                    prev = notif;
                    notif = notif->next;
                }
            }
            pthread_mutex_unlock(&queue_mutex);
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Connection closed for user: %s\n", pss->user_id);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "notification-protocol",
        callback_notifications,
        sizeof(struct per_session_data),
        MAX_PAYLOAD,
    },
    { NULL, NULL, 0, 0 } // terminator
};

int main(void) {
    struct lws_context_creation_info info;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create WebSocket context\n");
        return 1;
    }
    
    printf("WebSocket notification server running on port 8080\n");
    
    // Example: Simulate notification generation
    pthread_t notif_thread;
    // Start notification generator thread here
    
    // Main event loop
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

### C++ Modern Implementation with Boost.Beast

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <queue>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

struct Notification {
    std::string user_id;
    std::string type;
    std::string message;
    std::time_t timestamp;
};

class NotificationManager {
private:
    std::unordered_map<std::string, 
        std::vector<std::shared_ptr<websocket::stream<tcp::socket>>>> clients_;
    std::mutex mutex_;
    
public:
    void register_client(const std::string& user_id,
                        std::shared_ptr<websocket::stream<tcp::socket>> ws) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[user_id].push_back(ws);
        std::cout << "Client registered: " << user_id << std::endl;
    }
    
    void unregister_client(const std::string& user_id,
                          std::shared_ptr<websocket::stream<tcp::socket>> ws) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& vec = clients_[user_id];
        vec.erase(std::remove(vec.begin(), vec.end(), ws), vec.end());
    }
    
    void send_notification(const Notification& notif) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        json j = {
            {"type", notif.type},
            {"message", notif.message},
            {"timestamp", notif.timestamp}
        };
        
        std::string payload = j.dump();
        
        if (clients_.count(notif.user_id)) {
            for (auto& ws : clients_[notif.user_id]) {
                try {
                    ws->write(net::buffer(payload));
                } catch (const std::exception& e) {
                    std::cerr << "Send error: " << e.what() << std::endl;
                }
            }
        }
    }
    
    void broadcast(const Notification& notif) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        json j = {
            {"type", notif.type},
            {"message", notif.message},
            {"timestamp", notif.timestamp}
        };
        
        std::string payload = j.dump();
        
        for (auto& [user_id, connections] : clients_) {
            for (auto& ws : connections) {
                try {
                    ws->write(net::buffer(payload));
                } catch (const std::exception& e) {
                    std::cerr << "Broadcast error: " << e.what() << std::endl;
                }
            }
        }
    }
};

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
private:
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::string user_id_;
    NotificationManager& manager_;
    
public:
    WebSocketSession(tcp::socket socket, NotificationManager& manager)
        : ws_(std::move(socket)), manager_(manager) {}
    
    void run() {
        ws_.async_accept(
            [self = shared_from_this()](beast::error_code ec) {
                if (!ec)
                    self->do_read();
            });
    }
    
private:
    void do_read() {
        ws_.async_read(
            buffer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec)
                    return self->on_close();
                    
                self->handle_message();
                self->buffer_.consume(self->buffer_.size());
                self->do_read();
            });
    }
    
    void handle_message() {
        std::string msg = beast::buffers_to_string(buffer_.data());
        
        try {
            json j = json::parse(msg);
            
            if (j["type"] == "auth") {
                user_id_ = j["user_id"];
                manager_.register_client(user_id_, 
                    std::make_shared<websocket::stream<tcp::socket>>(
                        std::move(ws_.next_layer())));
                
                json response = {{"type", "auth_success"}, {"user_id", user_id_}};
                ws_.write(net::buffer(response.dump()));
            }
        } catch (const std::exception& e) {
            std::cerr << "Message handling error: " << e.what() << std::endl;
        }
    }
    
    void on_close() {
        if (!user_id_.empty()) {
            manager_.unregister_client(user_id_, 
                std::make_shared<websocket::stream<tcp::socket>>(
                    std::move(ws_.next_layer())));
        }
    }
};

class NotificationServer {
private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    NotificationManager manager_;
    
public:
    NotificationServer(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc), acceptor_(ioc) {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        
        do_accept();
    }
    
    NotificationManager& get_manager() { return manager_; }
    
private:
    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<WebSocketSession>(
                        std::move(socket), manager_)->run();
                }
                do_accept();
            });
    }
};

int main() {
    try {
        net::io_context ioc{1};
        
        tcp::endpoint endpoint{tcp::v4(), 8080};
        NotificationServer server(ioc, endpoint);
        
        std::cout << "Notification server running on port 8080" << std::endl;
        
        // Example: Send notification
        std::thread([&server]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            Notification notif{
                "user123",
                "message",
                "You have a new message!",
                std::time(nullptr)
            };
            server.get_manager().send_notification(notif);
        }).detach();
        
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation

### Using tokio-tungstenite

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{mpsc, RwLock};
use chrono::Utc;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct Notification {
    #[serde(rename = "type")]
    notif_type: String,
    message: String,
    timestamp: i64,
}

#[derive(Debug, Serialize, Deserialize)]
struct ClientMessage {
    #[serde(rename = "type")]
    msg_type: String,
    user_id: Option<String>,
}

type Tx = mpsc::UnboundedSender<Message>;
type ClientMap = Arc<RwLock<HashMap<String, Vec<Tx>>>>;

#[derive(Clone)]
struct NotificationManager {
    clients: ClientMap,
}

impl NotificationManager {
    fn new() -> Self {
        Self {
            clients: Arc::new(RwLock::new(HashMap::new())),
        }
    }
    
    async fn register_client(&self, user_id: String, tx: Tx) {
        let mut clients = self.clients.write().await;
        clients.entry(user_id.clone()).or_insert_with(Vec::new).push(tx);
        println!("Client registered: {}", user_id);
    }
    
    async fn unregister_client(&self, user_id: &str) {
        let mut clients = self.clients.write().await;
        clients.remove(user_id);
        println!("Client unregistered: {}", user_id);
    }
    
    async fn send_notification(&self, user_id: &str, notification: Notification) {
        let clients = self.clients.read().await;
        
        if let Some(user_clients) = clients.get(user_id) {
            let json = serde_json::to_string(&notification).unwrap();
            let msg = Message::Text(json);
            
            for tx in user_clients {
                let _ = tx.send(msg.clone());
            }
        }
    }
    
    async fn broadcast(&self, notification: Notification) {
        let clients = self.clients.read().await;
        let json = serde_json::to_string(&notification).unwrap();
        let msg = Message::Text(json);
        
        for user_clients in clients.values() {
            for tx in user_clients {
                let _ = tx.send(msg.clone());
            }
        }
    }
}

async fn handle_connection(
    stream: TcpStream,
    manager: NotificationManager,
) {
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake error: {}", e);
            return;
        }
    };
    
    println!("New WebSocket connection established");
    
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    let (tx, mut rx) = mpsc::unbounded_channel();
    
    let mut user_id: Option<String> = None;
    
    // Spawn task to handle outgoing messages
    let send_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_sender.send(msg).await.is_err() {
                break;
            }
        }
    });
    
    // Handle incoming messages
    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                if let Ok(client_msg) = serde_json::from_str::<ClientMessage>(&text) {
                    match client_msg.msg_type.as_str() {
                        "auth" => {
                            if let Some(uid) = client_msg.user_id {
                                user_id = Some(uid.clone());
                                manager.register_client(uid.clone(), tx.clone()).await;
                                
                                let response = serde_json::json!({
                                    "type": "auth_success",
                                    "user_id": uid
                                });
                                let _ = tx.send(Message::Text(response.to_string()));
                            }
                        }
                        "ping" => {
                            let response = serde_json::json!({"type": "pong"});
                            let _ = tx.send(Message::Text(response.to_string()));
                        }
                        _ => {}
                    }
                }
            }
            Ok(Message::Close(_)) => break,
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }
    
    // Cleanup
    if let Some(uid) = user_id {
        manager.unregister_client(&uid).await;
    }
    
    send_task.abort();
}

#[tokio::main]
async fn main() {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await.unwrap();
    println!("Notification server running on ws://{}", addr);
    
    let manager = NotificationManager::new();
    
    // Spawn notification generator task
    let manager_clone = manager.clone();
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(tokio::time::Duration::from_secs(10));
        loop {
            interval.tick().await;
            
            let notification = Notification {
                notif_type: "info".to_string(),
                message: "This is a test notification".to_string(),
                timestamp: Utc::now().timestamp(),
            };
            
            manager_clone.send_notification("user123", notification).await;
        }
    });
    
    // Accept connections
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        let manager_clone = manager.clone();
        tokio::spawn(handle_connection(stream, manager_clone));
    }
}
```

### Advanced Rust Implementation with Authentication

```rust
use tokio::net::TcpListener;
use tokio_tungstenite::accept_async;
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::{broadcast, RwLock};
use std::collections::HashMap;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum NotificationType {
    #[serde(rename = "message")]
    Message { content: String, from: String },
    #[serde(rename = "alert")]
    Alert { level: String, content: String },
    #[serde(rename = "update")]
    Update { entity: String, action: String },
}

#[derive(Debug, Clone, Serialize)]
struct Notification {
    id: String,
    user_id: String,
    #[serde(flatten)]
    notification_type: NotificationType,
    timestamp: i64,
}

#[derive(Clone)]
struct NotificationHub {
    // Per-user channels
    user_channels: Arc<RwLock<HashMap<String, broadcast::Sender<Notification>>>>,
    // Global broadcast channel
    global_tx: broadcast::Sender<Notification>,
}

impl NotificationHub {
    fn new() -> Self {
        let (global_tx, _) = broadcast::channel(100);
        Self {
            user_channels: Arc::new(RwLock::new(HashMap::new())),
            global_tx,
        }
    }
    
    async fn subscribe_user(&self, user_id: String) -> broadcast::Receiver<Notification> {
        let mut channels = self.user_channels.write().await;
        let tx = channels.entry(user_id).or_insert_with(|| {
            let (tx, _) = broadcast::channel(50);
            tx
        });
        tx.subscribe()
    }
    
    async fn send_to_user(&self, notification: Notification) {
        let channels = self.user_channels.read().await;
        if let Some(tx) = channels.get(&notification.user_id) {
            let _ = tx.send(notification);
        }
    }
    
    fn broadcast_global(&self, notification: Notification) {
        let _ = self.global_tx.send(notification);
    }
}

async fn handle_client(
    stream: tokio::net::TcpStream,
    hub: NotificationHub,
) -> Result<(), Box<dyn std::error::Error>> {
    let ws_stream = accept_async(stream).await?;
    let (mut ws_tx, mut ws_rx) = ws_stream.split();
    
    // Wait for authentication
    let user_id = if let Some(Ok(tokio_tungstenite::tungstenite::Message::Text(auth_msg))) = 
        ws_rx.next().await {
        let auth: serde_json::Value = serde_json::from_str(&auth_msg)?;
        auth["user_id"].as_str().unwrap_or("anonymous").to_string()
    } else {
        return Ok(());
    };
    
    println!("Authenticated user: {}", user_id);
    
    // Subscribe to user-specific notifications
    let mut rx = hub.subscribe_user(user_id.clone()).await;
    
    // Send confirmation
    let confirm = serde_json::json!({
        "type": "connected",
        "user_id": user_id
    });
    ws_tx.send(tokio_tungstenite::tungstenite::Message::Text(
        confirm.to_string()
    )).await?;
    
    // Handle notifications
    loop {
        tokio::select! {
            // Receive notification from hub
            Ok(notification) = rx.recv() => {
                let json = serde_json::to_string(&notification)?;
                if ws_tx.send(tokio_tungstenite::tungstenite::Message::Text(json))
                    .await.is_err() {
                    break;
                }
            }
            
            // Receive message from client
            msg = ws_rx.next() => {
                match msg {
                    Some(Ok(tokio_tungstenite::tungstenite::Message::Close(_))) | None => break,
                    Some(Ok(tokio_tungstenite::tungstenite::Message::Ping(data))) => {
                        let _ = ws_tx.send(
                            tokio_tungstenite::tungstenite::Message::Pong(data)
                        ).await;
                    }
                    _ => {}
                }
            }
        }
    }
    
    println!("Client disconnected: {}", user_id);
    Ok(())
}

#[tokio::main]
async fn main() {
    let listener = TcpListener::bind("127.0.0.1:8080").await.unwrap();
    let hub = NotificationHub::new();
    
    println!("WebSocket notification server running on ws://127.0.0.1:8080");
    
    // Example notification generator
    let hub_clone = hub.clone();
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(
            tokio::time::Duration::from_secs(5)
        );
        
        loop {
            interval.tick().await;
            
            let notification = Notification {
                id: Uuid::new_v4().to_string(),
                user_id: "user123".to_string(),
                notification_type: NotificationType::Message {
                    content: "Hello from server!".to_string(),
                    from: "system".to_string(),
                },
                timestamp: chrono::Utc::now().timestamp(),
            };
            
            hub_clone.send_to_user(notification).await;
        }
    });
    
    // Accept connections
    while let Ok((stream, _)) = listener.accept().await {
        let hub_clone = hub.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream, hub_clone).await {
                eprintln!("Error handling client: {}", e);
            }
        });
    }
}
```

## Summary

**Live Notifications System using WebSocket** enables real-time push notification delivery with persistent bidirectional connections. Key benefits include instant delivery, reduced server load, and efficient resource usage compared to HTTP polling.

**Core Implementation Elements:**
- Persistent WebSocket connections per client
- Connection management and user authentication
- Notification queue/channel system
- Event-driven architecture for instant delivery
- Support for targeted (per-user) and broadcast notifications

**Language Highlights:**
- **C/C++**: Low-level control with libwebsockets or Boost.Beast, ideal for high-performance systems
- **Rust**: Memory-safe async implementation with tokio/tungstenite, excellent for scalable microservices

**Common Patterns:**
- User authentication on connection establishment
- Heartbeat/ping-pong for connection health
- Message acknowledgment for reliability
- Graceful reconnection handling
- JSON-based message protocol

This architecture is widely used in chat applications, social media platforms, collaborative tools, monitoring dashboards, and any system requiring real-time user engagement.