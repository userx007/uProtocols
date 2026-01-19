# Horizontal Scaling Strategies for WebSocket

## Overview

Horizontal scaling for WebSocket applications presents unique challenges compared to traditional HTTP services. Unlike stateless HTTP requests, WebSocket connections are long-lived, stateful, and bidirectional, requiring special consideration when distributing load across multiple servers.

## Core Concepts

### Load Balancing
Distributing incoming WebSocket connection requests across multiple backend servers to prevent any single server from becoming overwhelmed.

### Sticky Sessions (Session Affinity)
Ensuring that all messages from a particular client are routed to the same backend server for the duration of their WebSocket connection.

### Distributed Architecture
Coordinating state and messages across multiple WebSocket servers so clients connected to different servers can communicate.

---

## Load Balancing Strategies

### Layer 4 (TCP) Load Balancing
Routes connections based on IP address and port, maintaining the connection to the same backend server throughout the WebSocket lifecycle.

### Layer 7 (Application) Load Balancing
Inspects HTTP upgrade request headers to make routing decisions, allowing more sophisticated routing logic.

---

## Code Examples

### C/C++ - Simple Load Balancer with libwebsockets

```c
#include <libwebsockets.h>
#include <string.h>
#include <signal.h>

#define MAX_BACKENDS 10

struct backend_server {
    const char *host;
    int port;
    int active_connections;
    int is_healthy;
};

struct load_balancer {
    struct backend_server backends[MAX_BACKENDS];
    int backend_count;
    int current_backend; // For round-robin
};

// Round-robin backend selection
struct backend_server* select_backend(struct load_balancer *lb) {
    int attempts = 0;
    
    while (attempts < lb->backend_count) {
        lb->current_backend = (lb->current_backend + 1) % lb->backend_count;
        struct backend_server *backend = &lb->backends[lb->current_backend];
        
        if (backend->is_healthy) {
            backend->active_connections++;
            return backend;
        }
        attempts++;
    }
    
    return NULL; // No healthy backends
}

// Least connections backend selection
struct backend_server* select_backend_least_conn(struct load_balancer *lb) {
    struct backend_server *selected = NULL;
    int min_connections = INT_MAX;
    
    for (int i = 0; i < lb->backend_count; i++) {
        struct backend_server *backend = &lb->backends[i];
        
        if (backend->is_healthy && backend->active_connections < min_connections) {
            min_connections = backend->active_connections;
            selected = backend;
        }
    }
    
    if (selected) {
        selected->active_connections++;
    }
    
    return selected;
}

// Per-session data with sticky session support
struct per_session_data {
    struct backend_server *assigned_backend;
    char session_id[64];
};

static int callback_proxy(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    struct per_session_data *pss = (struct per_session_data *)user;
    struct load_balancer *lb = (struct load_balancer *)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            // New connection - assign to backend
            pss->assigned_backend = select_backend_least_conn(lb);
            
            if (!pss->assigned_backend) {
                lwsl_err("No healthy backends available\n");
                return -1;
            }
            
            lwsl_user("Connection assigned to backend %s:%d\n",
                     pss->assigned_backend->host,
                     pss->assigned_backend->port);
            break;
            
        case LWS_CALLBACK_CLOSED:
            if (pss->assigned_backend) {
                pss->assigned_backend->active_connections--;
            }
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Forward message to assigned backend
            if (pss->assigned_backend) {
                lwsl_user("Forwarding %zu bytes to backend %s:%d\n",
                         len, pss->assigned_backend->host,
                         pss->assigned_backend->port);
                // Implementation of actual forwarding would go here
            }
            break;
    }
    
    return 0;
}
```

### C++ - Redis-based Pub/Sub for Distributed WebSocket

```cpp
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <hiredis/hiredis.h>
#include <thread>
#include <unordered_map>
#include <mutex>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;

class DistributedWebSocketServer {
private:
    server ws_server;
    redisContext *redis_pub;
    redisContext *redis_sub;
    std::unordered_map<std::string, websocketpp::connection_hdl> connections;
    std::mutex connections_mutex;
    std::string server_id;

public:
    DistributedWebSocketServer(const std::string& id) : server_id(id) {
        // Connect to Redis for pub/sub
        redis_pub = redisConnect("127.0.0.1", 6379);
        redis_sub = redisConnect("127.0.0.1", 6379);
        
        if (redis_pub == NULL || redis_pub->err) {
            throw std::runtime_error("Redis connection failed");
        }
        
        // Subscribe to broadcast channel
        redisReply *reply = (redisReply*)redisCommand(redis_sub, "SUBSCRIBE websocket_broadcast");
        freeReplyObject(reply);
        
        // Start Redis subscriber thread
        std::thread(&DistributedWebSocketServer::redis_subscriber, this).detach();
    }
    
    void on_open(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex);
        
        // Generate unique connection ID
        std::string conn_id = server_id + "_" + std::to_string(rand());
        connections[conn_id] = hdl;
        
        // Store connection mapping in Redis
        redisReply *reply = (redisReply*)redisCommand(redis_pub,
            "HSET connections:%s server %s",
            conn_id.c_str(), server_id.c_str());
        freeReplyObject(reply);
        
        std::cout << "Connection opened: " << conn_id << std::endl;
    }
    
    void on_close(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex);
        
        // Find and remove connection
        for (auto it = connections.begin(); it != connections.end(); ++it) {
            if (it->second.lock() == hdl.lock()) {
                redisReply *reply = (redisReply*)redisCommand(redis_pub,
                    "DEL connections:%s", it->first.c_str());
                freeReplyObject(reply);
                
                connections.erase(it);
                break;
            }
        }
    }
    
    void on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
        // Publish message to all servers via Redis
        std::string payload = msg->get_payload();
        
        redisReply *reply = (redisReply*)redisCommand(redis_pub,
            "PUBLISH websocket_broadcast %s", payload.c_str());
        freeReplyObject(reply);
    }
    
    void redis_subscriber() {
        redisReply *reply;
        
        while (true) {
            // Block waiting for messages
            if (redisGetReply(redis_sub, (void**)&reply) == REDIS_OK) {
                if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
                    std::string message(reply->element[2]->str, reply->element[2]->len);
                    
                    // Broadcast to all local connections
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    for (const auto& conn : connections) {
                        try {
                            ws_server.send(conn.second, message, websocketpp::frame::opcode::text);
                        } catch (...) {
                            // Handle disconnected clients
                        }
                    }
                }
                freeReplyObject(reply);
            }
        }
    }
    
    void run(uint16_t port) {
        ws_server.init_asio();
        ws_server.set_open_handler(std::bind(&DistributedWebSocketServer::on_open, this, std::placeholders::_1));
        ws_server.set_close_handler(std::bind(&DistributedWebSocketServer::on_close, this, std::placeholders::_1));
        ws_server.set_message_handler(std::bind(&DistributedWebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        
        ws_server.listen(port);
        ws_server.start_accept();
        ws_server.run();
    }
};

int main() {
    DistributedWebSocketServer server("server_1");
    server.run(9001);
    return 0;
}
```

### Rust - Load Balanced WebSocket with Tokio

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, WebSocketStream};
use futures_util::{StreamExt, SinkExt};
use std::sync::Arc;
use tokio::sync::RwLock;
use std::collections::HashMap;

#[derive(Clone)]
struct BackendServer {
    host: String,
    port: u16,
    active_connections: Arc<RwLock<usize>>,
    is_healthy: Arc<RwLock<bool>>,
}

impl BackendServer {
    fn new(host: String, port: u16) -> Self {
        Self {
            host,
            port,
            active_connections: Arc::new(RwLock::new(0)),
            is_healthy: Arc::new(RwLock::new(true)),
        }
    }
    
    async fn increment_connections(&self) {
        let mut count = self.active_connections.write().await;
        *count += 1;
    }
    
    async fn decrement_connections(&self) {
        let mut count = self.active_connections.write().await;
        if *count > 0 {
            *count -= 1;
        }
    }
    
    async fn get_connection_count(&self) -> usize {
        *self.active_connections.read().await
    }
}

struct LoadBalancer {
    backends: Vec<BackendServer>,
    current_index: Arc<RwLock<usize>>,
}

impl LoadBalancer {
    fn new(backends: Vec<BackendServer>) -> Self {
        Self {
            backends,
            current_index: Arc::new(RwLock::new(0)),
        }
    }
    
    // Round-robin selection
    async fn select_backend_round_robin(&self) -> Option<BackendServer> {
        let mut index = self.current_index.write().await;
        
        for _ in 0..self.backends.len() {
            *index = (*index + 1) % self.backends.len();
            let backend = &self.backends[*index];
            
            if *backend.is_healthy.read().await {
                return Some(backend.clone());
            }
        }
        
        None
    }
    
    // Least connections selection
    async fn select_backend_least_connections(&self) -> Option<BackendServer> {
        let mut selected: Option<(BackendServer, usize)> = None;
        
        for backend in &self.backends {
            if !*backend.is_healthy.read().await {
                continue;
            }
            
            let conn_count = backend.get_connection_count().await;
            
            match &selected {
                None => selected = Some((backend.clone(), conn_count)),
                Some((_, min_count)) if conn_count < *min_count => {
                    selected = Some((backend.clone(), conn_count));
                }
                _ => {}
            }
        }
        
        selected.map(|(backend, _)| backend)
    }
}

// Sticky session manager using client IP
struct StickySessionManager {
    sessions: Arc<RwLock<HashMap<String, BackendServer>>>,
}

impl StickySessionManager {
    fn new() -> Self {
        Self {
            sessions: Arc::new(RwLock::new(HashMap::new())),
        }
    }
    
    async fn get_or_assign(&self, client_id: String, lb: &LoadBalancer) -> Option<BackendServer> {
        let sessions = self.sessions.read().await;
        
        if let Some(backend) = sessions.get(&client_id) {
            return Some(backend.clone());
        }
        
        drop(sessions);
        
        // Assign new backend
        if let Some(backend) = lb.select_backend_least_connections().await {
            let mut sessions = self.sessions.write().await;
            sessions.insert(client_id, backend.clone());
            return Some(backend);
        }
        
        None
    }
    
    async fn remove(&self, client_id: &str) {
        let mut sessions = self.sessions.write().await;
        sessions.remove(client_id);
    }
}

async fn handle_connection(
    stream: TcpStream,
    peer_addr: String,
    sticky_manager: Arc<StickySessionManager>,
    load_balancer: Arc<LoadBalancer>,
) {
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed: {}", e);
            return;
        }
    };
    
    // Get assigned backend using sticky sessions
    let backend = match sticky_manager.get_or_assign(peer_addr.clone(), &load_balancer).await {
        Some(b) => b,
        None => {
            eprintln!("No healthy backend available");
            return;
        }
    };
    
    backend.increment_connections().await;
    println!("Assigned {} to backend {}:{}", peer_addr, backend.host, backend.port);
    
    let (mut write, mut read) = ws_stream.split();
    
    // Echo messages (in production, forward to backend)
    while let Some(msg) = read.next().await {
        match msg {
            Ok(msg) => {
                if msg.is_text() || msg.is_binary() {
                    if write.send(msg).await.is_err() {
                        break;
                    }
                }
            }
            Err(_) => break,
        }
    }
    
    backend.decrement_connections().await;
    sticky_manager.remove(&peer_addr).await;
    println!("Connection closed: {}", peer_addr);
}

#[tokio::main]
async fn main() {
    let backends = vec![
        BackendServer::new("localhost".to_string(), 8001),
        BackendServer::new("localhost".to_string(), 8002),
        BackendServer::new("localhost".to_string(), 8003),
    ];
    
    let load_balancer = Arc::new(LoadBalancer::new(backends));
    let sticky_manager = Arc::new(StickySessionManager::new());
    
    let listener = TcpListener::bind("127.0.0.1:9000").await.unwrap();
    println!("Load balancer listening on 127.0.0.1:9000");
    
    while let Ok((stream, addr)) = listener.accept().await {
        let peer_addr = addr.to_string();
        let sticky_manager = sticky_manager.clone();
        let load_balancer = load_balancer.clone();
        
        tokio::spawn(async move {
            handle_connection(stream, peer_addr, sticky_manager, load_balancer).await;
        });
    }
}
```

### Rust - Redis Pub/Sub for Distributed WebSocket

```rust
use tokio_tungstenite::tungstenite::Message;
use redis::AsyncCommands;
use std::sync::Arc;
use tokio::sync::RwLock;
use std::collections::HashMap;
use uuid::Uuid;

type ConnectionId = String;
type Connections = Arc<RwLock<HashMap<ConnectionId, tokio::sync::mpsc::UnboundedSender<Message>>>>;

struct DistributedWebSocketServer {
    server_id: String,
    connections: Connections,
    redis_client: redis::Client,
}

impl DistributedWebSocketServer {
    fn new(server_id: String, redis_url: &str) -> redis::RedisResult<Self> {
        let redis_client = redis::Client::open(redis_url)?;
        
        Ok(Self {
            server_id,
            connections: Arc::new(RwLock::new(HashMap::new())),
            redis_client,
        })
    }
    
    async fn start_subscriber(&self) {
        let mut pubsub_conn = self.redis_client.get_async_pubsub().await.unwrap();
        pubsub_conn.subscribe("websocket_broadcast").await.unwrap();
        
        let connections = self.connections.clone();
        let mut pubsub_stream = pubsub_conn.on_message();
        
        tokio::spawn(async move {
            while let Some(msg) = pubsub_stream.next().await {
                let payload: String = msg.get_payload().unwrap();
                let connections = connections.read().await;
                
                // Broadcast to all local connections
                for (_, tx) in connections.iter() {
                    let _ = tx.send(Message::Text(payload.clone()));
                }
            }
        });
    }
    
    async fn add_connection(&self, tx: tokio::sync::mpsc::UnboundedSender<Message>) -> ConnectionId {
        let conn_id = format!("{}_{}", self.server_id, Uuid::new_v4());
        let mut connections = self.connections.write().await;
        connections.insert(conn_id.clone(), tx);
        
        // Store in Redis
        let mut redis_conn = self.redis_client.get_multiplexed_async_connection().await.unwrap();
        let _: () = redis_conn.hset(
            format!("connections:{}", conn_id),
            "server",
            &self.server_id
        ).await.unwrap();
        
        conn_id
    }
    
    async fn remove_connection(&self, conn_id: &str) {
        let mut connections = self.connections.write().await;
        connections.remove(conn_id);
        
        // Remove from Redis
        let mut redis_conn = self.redis_client.get_multiplexed_async_connection().await.unwrap();
        let _: () = redis_conn.del(format!("connections:{}", conn_id)).await.unwrap();
    }
    
    async fn broadcast_message(&self, message: String) {
        let mut redis_conn = self.redis_client.get_multiplexed_async_connection().await.unwrap();
        let _: () = redis_conn.publish("websocket_broadcast", message).await.unwrap();
    }
}
```

---

## Summary

**Horizontal scaling for WebSocket applications** requires careful architectural planning due to the stateful, persistent nature of WebSocket connections. Key strategies include:

1. **Load Balancing**: Distribute new connections across multiple servers using round-robin, least-connections, or IP-hash algorithms
2. **Sticky Sessions**: Ensure client messages consistently route to the same backend server, typically using IP-based hashing or session cookies
3. **Distributed Message Bus**: Use Redis Pub/Sub, RabbitMQ, or NATS to enable communication between clients connected to different servers
4. **Shared State Management**: Store connection metadata and session data in centralized stores like Redis for cross-server coordination
5. **Health Checking**: Monitor backend server health and automatically route traffic away from failing instances

The examples demonstrate implementing these patterns in C/C++ using libwebsockets and in Rust using tokio-tungstenite, showing both the load balancing proxy pattern and the distributed pub/sub pattern for horizontal scalability.