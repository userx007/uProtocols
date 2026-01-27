# Redis Pub/Sub Integration for WebSocket

## Overview

Redis Pub/Sub (Publish/Subscribe) is a messaging pattern that enables WebSocket servers to communicate with each other, allowing message broadcasting across multiple server instances. This is essential for horizontally scaled WebSocket applications where clients connected to different servers need to receive the same messages.

## Why Redis Pub/Sub with WebSockets?

When you scale WebSocket applications horizontally (multiple server instances behind a load balancer), clients connected to different servers can't directly communicate. Redis Pub/Sub solves this by acting as a message broker that synchronizes messages across all server instances.

**Key Benefits:**
- Enables horizontal scaling of WebSocket servers
- Decouples message producers from consumers
- Provides real-time message distribution
- Supports broadcasting to all connected clients across instances
- Allows topic-based message routing

## How It Works

1. Each WebSocket server subscribes to Redis channels
2. When a server receives a message from a client, it publishes to Redis
3. Redis broadcasts the message to all subscribed servers
4. Each server forwards the message to its connected clients

## C/C++ Implementation

```c
#include <hiredis/hiredis.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

// Connection tracking
typedef struct {
    struct lws *wsi;
    int user_id;
} ws_connection_t;

typedef struct {
    ws_connection_t *connections;
    size_t count;
    size_t capacity;
    pthread_mutex_t lock;
} connection_pool_t;

// Redis context
typedef struct {
    redisContext *pub_ctx;
    redisContext *sub_ctx;
    connection_pool_t *pool;
    pthread_t sub_thread;
} redis_ws_context_t;

// Initialize connection pool
connection_pool_t* init_connection_pool() {
    connection_pool_t *pool = malloc(sizeof(connection_pool_t));
    pool->capacity = 100;
    pool->count = 0;
    pool->connections = malloc(sizeof(ws_connection_t) * pool->capacity);
    pthread_mutex_init(&pool->lock, NULL);
    return pool;
}

// Add WebSocket connection
void add_connection(connection_pool_t *pool, struct lws *wsi, int user_id) {
    pthread_mutex_lock(&pool->lock);
    
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->connections = realloc(pool->connections, 
                                   sizeof(ws_connection_t) * pool->capacity);
    }
    
    pool->connections[pool->count].wsi = wsi;
    pool->connections[pool->count].user_id = user_id;
    pool->count++;
    
    pthread_mutex_unlock(&pool->lock);
}

// Remove WebSocket connection
void remove_connection(connection_pool_t *pool, struct lws *wsi) {
    pthread_mutex_lock(&pool->lock);
    
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->connections[i].wsi == wsi) {
            pool->connections[i] = pool->connections[pool->count - 1];
            pool->count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
}

// Redis subscriber thread
void* redis_subscriber_thread(void *arg) {
    redis_ws_context_t *ctx = (redis_ws_context_t*)arg;
    redisReply *reply;
    
    // Subscribe to channels
    reply = redisCommand(ctx->sub_ctx, "SUBSCRIBE chat:messages");
    freeReplyObject(reply);
    
    printf("Redis subscriber started\n");
    
    while (1) {
        if (redisGetReply(ctx->sub_ctx, (void**)&reply) == REDIS_OK) {
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
                if (strcmp(reply->element[0]->str, "message") == 0) {
                    char *channel = reply->element[1]->str;
                    char *message = reply->element[2]->str;
                    
                    printf("Received from Redis - Channel: %s, Message: %s\n", 
                           channel, message);
                    
                    // Broadcast to all connected WebSocket clients
                    pthread_mutex_lock(&ctx->pool->lock);
                    
                    unsigned char buf[LWS_PRE + 512];
                    size_t len = strlen(message);
                    memcpy(&buf[LWS_PRE], message, len);
                    
                    for (size_t i = 0; i < ctx->pool->count; i++) {
                        lws_write(ctx->pool->connections[i].wsi, 
                                 &buf[LWS_PRE], len, LWS_WRITE_TEXT);
                    }
                    
                    pthread_mutex_unlock(&ctx->pool->lock);
                }
            }
            freeReplyObject(reply);
        } else {
            printf("Redis connection error, reconnecting...\n");
            sleep(1);
            redisFree(ctx->sub_ctx);
            ctx->sub_ctx = redisConnect("127.0.0.1", 6379);
            
            reply = redisCommand(ctx->sub_ctx, "SUBSCRIBE chat:messages");
            freeReplyObject(reply);
        }
    }
    
    return NULL;
}

// Publish message to Redis
void publish_to_redis(redis_ws_context_t *ctx, const char *channel, 
                     const char *message) {
    redisReply *reply = redisCommand(ctx->pub_ctx, "PUBLISH %s %s", 
                                     channel, message);
    if (reply) {
        printf("Published to %d subscribers\n", (int)reply->integer);
        freeReplyObject(reply);
    }
}

// WebSocket callback
static int callback_websocket(struct lws *wsi, 
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    redis_ws_context_t *redis_ctx = 
        (redis_ws_context_t*)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket connection established\n");
            add_connection(redis_ctx->pool, wsi, 0);
            break;
            
        case LWS_CALLBACK_RECEIVE:
            printf("Received: %.*s\n", (int)len, (char*)in);
            
            // Publish to Redis (which will broadcast to all servers)
            char *msg = malloc(len + 1);
            memcpy(msg, in, len);
            msg[len] = '\0';
            
            publish_to_redis(redis_ctx, "chat:messages", msg);
            free(msg);
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket connection closed\n");
            remove_connection(redis_ctx->pool, wsi);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Main setup
int main() {
    redis_ws_context_t redis_ctx;
    
    // Initialize Redis connections
    redis_ctx.pub_ctx = redisConnect("127.0.0.1", 6379);
    redis_ctx.sub_ctx = redisConnect("127.0.0.1", 6379);
    
    if (redis_ctx.pub_ctx->err || redis_ctx.sub_ctx->err) {
        printf("Redis connection error\n");
        return 1;
    }
    
    // Initialize connection pool
    redis_ctx.pool = init_connection_pool();
    
    // Start Redis subscriber thread
    pthread_create(&redis_ctx.sub_thread, NULL, 
                   redis_subscriber_thread, &redis_ctx);
    
    // Setup WebSocket server (libwebsockets)
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 8080;
    info.protocols = (struct lws_protocols[]){
        {"chat-protocol", callback_websocket, 0, 512},
        {NULL, NULL, 0, 0}
    };
    info.user = &redis_ctx;
    
    struct lws_context *context = lws_create_context(&info);
    
    printf("WebSocket server started on port 8080\n");
    
    // Event loop
    while (1) {
        lws_service(context, 50);
    }
    
    // Cleanup
    lws_context_destroy(context);
    redisFree(redis_ctx.pub_ctx);
    redisFree(redis_ctx.sub_ctx);
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o redis_ws_server redis_ws_server.c -lwebsockets -lhiredis -lpthread
```

## Rust Implementation

```rust
use redis::{Client, Commands, PubSub};
use tokio::sync::broadcast;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use tokio::net::TcpListener;

type ClientId = usize;
type Clients = Arc<RwLock<HashMap<ClientId, broadcast::Sender<String>>>>;

#[derive(Clone)]
struct RedisWebSocketServer {
    redis_client: Client,
    clients: Clients,
}

impl RedisWebSocketServer {
    fn new(redis_url: &str) -> Result<Self, redis::RedisError> {
        let redis_client = Client::open(redis_url)?;
        let clients = Arc::new(RwLock::new(HashMap::new()));
        
        Ok(Self {
            redis_client,
            clients,
        })
    }
    
    // Publish message to Redis
    async fn publish_message(&self, channel: &str, message: &str) 
        -> Result<(), redis::RedisError> {
        let mut conn = self.redis_client.get_connection()?;
        let _: i32 = conn.publish(channel, message)?;
        println!("Published to Redis: {}", message);
        Ok(())
    }
    
    // Subscribe to Redis and broadcast to WebSocket clients
    async fn subscribe_and_broadcast(self) {
        let mut conn = match self.redis_client.get_connection() {
            Ok(c) => c,
            Err(e) => {
                eprintln!("Failed to connect to Redis: {}", e);
                return;
            }
        };
        
        let mut pubsub: PubSub = conn.as_pubsub();
        
        if let Err(e) = pubsub.subscribe("chat:messages") {
            eprintln!("Failed to subscribe: {}", e);
            return;
        }
        
        println!("Subscribed to Redis channel: chat:messages");
        
        loop {
            match pubsub.get_message() {
                Ok(msg) => {
                    let payload: String = match msg.get_payload() {
                        Ok(p) => p,
                        Err(_) => continue,
                    };
                    
                    println!("Received from Redis: {}", payload);
                    
                    // Broadcast to all connected WebSocket clients
                    let clients = self.clients.read().await;
                    for (id, sender) in clients.iter() {
                        if let Err(e) = sender.send(payload.clone()) {
                            eprintln!("Failed to send to client {}: {}", id, e);
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error receiving message: {}", e);
                    tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
                }
            }
        }
    }
    
    // Handle individual WebSocket connection
    async fn handle_connection(
        self,
        client_id: ClientId,
        stream: tokio::net::TcpStream,
    ) {
        println!("New WebSocket connection: {}", client_id);
        
        let ws_stream = match accept_async(stream).await {
            Ok(ws) => ws,
            Err(e) => {
                eprintln!("WebSocket handshake failed: {}", e);
                return;
            }
        };
        
        let (mut ws_sender, mut ws_receiver) = ws_stream.split();
        
        // Create broadcast channel for this client
        let (tx, mut rx) = broadcast::channel(100);
        
        // Register client
        {
            let mut clients = self.clients.write().await;
            clients.insert(client_id, tx.clone());
        }
        
        // Spawn task to receive from broadcast and send to WebSocket
        let send_task = tokio::spawn(async move {
            while let Ok(msg) = rx.recv().await {
                if ws_sender.send(Message::Text(msg)).await.is_err() {
                    break;
                }
            }
        });
        
        // Handle incoming WebSocket messages
        let server = self.clone();
        let receive_task = tokio::spawn(async move {
            while let Some(msg) = ws_receiver.next().await {
                match msg {
                    Ok(Message::Text(text)) => {
                        println!("Received from client {}: {}", client_id, text);
                        
                        // Publish to Redis
                        if let Err(e) = server.publish_message("chat:messages", &text).await {
                            eprintln!("Failed to publish to Redis: {}", e);
                        }
                    }
                    Ok(Message::Close(_)) => {
                        println!("Client {} disconnected", client_id);
                        break;
                    }
                    Err(e) => {
                        eprintln!("WebSocket error: {}", e);
                        break;
                    }
                    _ => {}
                }
            }
        });
        
        // Wait for either task to complete
        tokio::select! {
            _ = send_task => {},
            _ = receive_task => {},
        }
        
        // Cleanup: remove client
        {
            let mut clients = self.clients.write().await;
            clients.remove(&client_id);
        }
        
        println!("Client {} connection closed", client_id);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let redis_url = "redis://127.0.0.1:6379";
    let server = RedisWebSocketServer::new(redis_url)?;
    
    // Spawn Redis subscriber task
    let subscriber_server = server.clone();
    tokio::spawn(async move {
        subscriber_server.subscribe_and_broadcast().await;
    });
    
    // Start WebSocket server
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    let mut client_id: ClientId = 0;
    
    loop {
        let (stream, _) = listener.accept().await?;
        let server_clone = server.clone();
        
        let current_id = client_id;
        client_id += 1;
        
        tokio::spawn(async move {
            server_clone.handle_connection(current_id, stream).await;
        });
    }
}
```

**Cargo.toml:**
```toml
[package]
name = "redis-websocket-server"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1.35", features = ["full"] }
tokio-tungstenite = "0.21"
futures-util = "0.3"
redis = "0.24"
```

## Advanced Pattern: Room-based Broadcasting

```rust
use std::collections::HashSet;

#[derive(Clone)]
struct RoomManager {
    redis_client: Client,
    // room_name -> set of client_ids
    rooms: Arc<RwLock<HashMap<String, HashSet<ClientId>>>>,
    clients: Clients,
}

impl RoomManager {
    async fn join_room(&self, client_id: ClientId, room: &str) {
        let mut rooms = self.rooms.write().await;
        rooms.entry(room.to_string())
            .or_insert_with(HashSet::new)
            .insert(client_id);
        
        // Subscribe to room-specific Redis channel
        let channel = format!("room:{}", room);
        println!("Client {} joined room: {}", client_id, room);
    }
    
    async fn publish_to_room(&self, room: &str, message: &str) 
        -> Result<(), redis::RedisError> {
        let channel = format!("room:{}", room);
        let mut conn = self.redis_client.get_connection()?;
        let _: i32 = conn.publish(&channel, message)?;
        Ok(())
    }
    
    async fn broadcast_to_room(&self, room: &str, message: String) {
        let rooms = self.rooms.read().await;
        if let Some(room_clients) = rooms.get(room) {
            let clients = self.clients.read().await;
            for client_id in room_clients {
                if let Some(sender) = clients.get(client_id) {
                    let _ = sender.send(message.clone());
                }
            }
        }
    }
}
```

## Summary

**Redis Pub/Sub Integration with WebSockets** enables horizontally scalable real-time applications by synchronizing messages across multiple server instances. Redis acts as a central message broker, allowing servers to publish and subscribe to channels.

**Key Concepts:**
- **Publisher**: WebSocket server publishes client messages to Redis channels
- **Subscriber**: Each server subscribes to Redis channels and broadcasts to connected clients
- **Horizontal Scaling**: Multiple server instances stay synchronized through Redis
- **Channel-based Routing**: Different topics/rooms use separate Redis channels

**Common Use Cases:**
- Chat applications with multiple server instances
- Real-time notifications across distributed systems
- Live collaboration tools
- Gaming servers with lobby/room systems
- IoT device communication

**Architecture Benefits:**
- Decouples server instances from each other
- Enables elastic scaling based on load
- Provides fault tolerance (servers can restart independently)
- Simple to implement and maintain
- Low latency message distribution

The pattern is essential for production WebSocket deployments that need to scale beyond a single server instance while maintaining real-time message delivery to all connected clients.