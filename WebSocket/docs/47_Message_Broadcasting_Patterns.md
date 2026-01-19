# Message Broadcasting Patterns in WebSocket

## Overview

Message broadcasting is a fundamental pattern in WebSocket applications where a single message needs to be distributed to multiple connected clients efficiently. This is essential for applications like chat rooms, real-time dashboards, multiplayer games, live notifications, and collaborative tools. The challenge lies in minimizing latency, managing memory efficiently, and handling connection failures gracefully.

## Core Concepts

### Broadcasting Strategies

1. **Direct Broadcasting**: Iterate through all connections and send messages individually
2. **Publisher-Subscriber (Pub/Sub)**: Clients subscribe to specific channels/topics
3. **Room-based Broadcasting**: Group clients into rooms for targeted distribution
4. **Filtered Broadcasting**: Send messages based on client attributes or permissions
5. **Hierarchical Broadcasting**: Multi-level distribution with message prioritization

### Key Considerations

- **Message Serialization**: Serialize once, send to many
- **Connection Management**: Track active connections efficiently
- **Backpressure Handling**: Manage slow consumers without blocking fast ones
- **Memory Efficiency**: Share message buffers when possible
- **Error Isolation**: One client's failure shouldn't affect others

---

## C/C++ Implementation

Here's a comprehensive example using libwebsockets with efficient broadcasting patterns:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_CLIENTS 1000
#define MAX_CHANNELS 100
#define MAX_MESSAGE_SIZE 4096

// Channel structure for pub/sub
typedef struct channel {
    char name[64];
    struct lws *subscribers[MAX_CLIENTS];
    int subscriber_count;
    pthread_mutex_t lock;
} channel_t;

// Per-session data
struct per_session_data {
    struct lws *wsi;
    int id;
    channel_t *subscribed_channels[MAX_CHANNELS];
    int channel_count;
    unsigned char *pending_buffer;
    size_t pending_len;
};

// Global broadcast manager
typedef struct broadcast_manager {
    struct lws *clients[MAX_CLIENTS];
    int client_count;
    channel_t channels[MAX_CHANNELS];
    int channel_count;
    pthread_mutex_t clients_lock;
    pthread_mutex_t channels_lock;
} broadcast_manager_t;

static broadcast_manager_t g_broadcast_mgr;

// Initialize broadcast manager
void init_broadcast_manager() {
    memset(&g_broadcast_mgr, 0, sizeof(broadcast_manager_t));
    pthread_mutex_init(&g_broadcast_mgr.clients_lock, NULL);
    pthread_mutex_init(&g_broadcast_mgr.channels_lock, NULL);
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        pthread_mutex_init(&g_broadcast_mgr.channels[i].lock, NULL);
    }
}

// Add client to broadcast list
void add_client(struct lws *wsi) {
    pthread_mutex_lock(&g_broadcast_mgr.clients_lock);
    
    if (g_broadcast_mgr.client_count < MAX_CLIENTS) {
        g_broadcast_mgr.clients[g_broadcast_mgr.client_count++] = wsi;
    }
    
    pthread_mutex_unlock(&g_broadcast_mgr.clients_lock);
}

// Remove client from broadcast list
void remove_client(struct lws *wsi) {
    pthread_mutex_lock(&g_broadcast_mgr.clients_lock);
    
    for (int i = 0; i < g_broadcast_mgr.client_count; i++) {
        if (g_broadcast_mgr.clients[i] == wsi) {
            // Shift remaining clients
            memmove(&g_broadcast_mgr.clients[i], 
                    &g_broadcast_mgr.clients[i + 1],
                    (g_broadcast_mgr.client_count - i - 1) * sizeof(struct lws *));
            g_broadcast_mgr.client_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_broadcast_mgr.clients_lock);
}

// Find or create channel
channel_t* get_or_create_channel(const char *name) {
    pthread_mutex_lock(&g_broadcast_mgr.channels_lock);
    
    // Search for existing channel
    for (int i = 0; i < g_broadcast_mgr.channel_count; i++) {
        if (strcmp(g_broadcast_mgr.channels[i].name, name) == 0) {
            pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
            return &g_broadcast_mgr.channels[i];
        }
    }
    
    // Create new channel
    if (g_broadcast_mgr.channel_count < MAX_CHANNELS) {
        channel_t *ch = &g_broadcast_mgr.channels[g_broadcast_mgr.channel_count++];
        strncpy(ch->name, name, sizeof(ch->name) - 1);
        ch->subscriber_count = 0;
        pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
        return ch;
    }
    
    pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
    return NULL;
}

// Subscribe to channel
int subscribe_to_channel(struct lws *wsi, const char *channel_name) {
    channel_t *ch = get_or_create_channel(channel_name);
    if (!ch) return -1;
    
    pthread_mutex_lock(&ch->lock);
    
    // Check if already subscribed
    for (int i = 0; i < ch->subscriber_count; i++) {
        if (ch->subscribers[i] == wsi) {
            pthread_mutex_unlock(&ch->lock);
            return 0; // Already subscribed
        }
    }
    
    // Add subscriber
    if (ch->subscriber_count < MAX_CLIENTS) {
        ch->subscribers[ch->subscriber_count++] = wsi;
        pthread_mutex_unlock(&ch->lock);
        return 0;
    }
    
    pthread_mutex_unlock(&ch->lock);
    return -1;
}

// Unsubscribe from channel
void unsubscribe_from_channel(struct lws *wsi, const char *channel_name) {
    pthread_mutex_lock(&g_broadcast_mgr.channels_lock);
    
    for (int i = 0; i < g_broadcast_mgr.channel_count; i++) {
        if (strcmp(g_broadcast_mgr.channels[i].name, channel_name) == 0) {
            channel_t *ch = &g_broadcast_mgr.channels[i];
            pthread_mutex_lock(&ch->lock);
            
            for (int j = 0; j < ch->subscriber_count; j++) {
                if (ch->subscribers[j] == wsi) {
                    memmove(&ch->subscribers[j], 
                            &ch->subscribers[j + 1],
                            (ch->subscriber_count - j - 1) * sizeof(struct lws *));
                    ch->subscriber_count--;
                    break;
                }
            }
            
            pthread_mutex_unlock(&ch->lock);
            break;
        }
    }
    
    pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
}

// Broadcast to all clients
void broadcast_to_all(const unsigned char *message, size_t len) {
    // Prepare message buffer (LWS_PRE bytes before actual message)
    unsigned char *buf = malloc(LWS_PRE + len);
    memcpy(buf + LWS_PRE, message, len);
    
    pthread_mutex_lock(&g_broadcast_mgr.clients_lock);
    
    for (int i = 0; i < g_broadcast_mgr.client_count; i++) {
        struct lws *wsi = g_broadcast_mgr.clients[i];
        
        // Queue message for sending
        lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
        
        // Trigger write callback
        lws_callback_on_writable(wsi);
    }
    
    pthread_mutex_unlock(&g_broadcast_mgr.clients_lock);
    free(buf);
}

// Broadcast to specific channel
void broadcast_to_channel(const char *channel_name, 
                          const unsigned char *message, 
                          size_t len) {
    channel_t *ch = NULL;
    
    pthread_mutex_lock(&g_broadcast_mgr.channels_lock);
    
    // Find channel
    for (int i = 0; i < g_broadcast_mgr.channel_count; i++) {
        if (strcmp(g_broadcast_mgr.channels[i].name, channel_name) == 0) {
            ch = &g_broadcast_mgr.channels[i];
            break;
        }
    }
    
    if (!ch) {
        pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
        return;
    }
    
    pthread_mutex_lock(&ch->lock);
    pthread_mutex_unlock(&g_broadcast_mgr.channels_lock);
    
    // Prepare message buffer
    unsigned char *buf = malloc(LWS_PRE + len);
    memcpy(buf + LWS_PRE, message, len);
    
    // Send to all subscribers
    for (int i = 0; i < ch->subscriber_count; i++) {
        lws_write(ch->subscribers[i], buf + LWS_PRE, len, LWS_WRITE_TEXT);
        lws_callback_on_writable(ch->subscribers[i]);
    }
    
    pthread_mutex_unlock(&ch->lock);
    free(buf);
}

// WebSocket callback
static int callback_websocket(struct lws *wsi, 
                               enum lws_callback_reasons reason,
                               void *user, 
                               void *in, 
                               size_t len) {
    struct per_session_data *pss = (struct per_session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            pss->wsi = wsi;
            add_client(wsi);
            printf("Client connected\n");
            break;
            
        case LWS_CALLBACK_CLOSED:
            remove_client(wsi);
            printf("Client disconnected\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Example: Parse message and route to appropriate channel
            // Format: "SUBSCRIBE:channel_name" or "MESSAGE:channel_name:content"
            {
                char *msg = (char *)in;
                if (strncmp(msg, "SUBSCRIBE:", 10) == 0) {
                    subscribe_to_channel(wsi, msg + 10);
                } else if (strncmp(msg, "MESSAGE:", 8) == 0) {
                    char *sep = strchr(msg + 8, ':');
                    if (sep) {
                        *sep = '\0';
                        const char *channel = msg + 8;
                        const char *content = sep + 1;
                        broadcast_to_channel(channel, 
                                            (unsigned char *)content, 
                                            strlen(content));
                    }
                } else if (strcmp(msg, "BROADCAST") == 0) {
                    broadcast_to_all((unsigned char *)"Server broadcast", 17);
                }
            }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Handle pending messages if any
            if (pss->pending_buffer) {
                lws_write(wsi, pss->pending_buffer, pss->pending_len, 
                         LWS_WRITE_TEXT);
                free(pss->pending_buffer);
                pss->pending_buffer = NULL;
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "broadcast-protocol",
        callback_websocket,
        sizeof(struct per_session_data),
        MAX_MESSAGE_SIZE,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    init_broadcast_manager();
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return -1;
    }
    
    printf("WebSocket broadcast server running on port 8080\n");
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

---

## Rust Implementation

Here's a modern Rust implementation using `tokio-tungstenite` with async broadcasting:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{broadcast, RwLock, mpsc};
use serde::{Deserialize, Serialize};

// Message types
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum ClientMessage {
    Subscribe { channel: String },
    Unsubscribe { channel: String },
    Publish { channel: String, content: String },
    Broadcast { content: String },
}

#[derive(Debug, Clone)]
enum BroadcastMessage {
    Message { channel: String, content: String },
    GlobalMessage { content: String },
}

// Channel manager for pub/sub
struct ChannelManager {
    channels: RwLock<HashMap<String, broadcast::Sender<String>>>,
}

impl ChannelManager {
    fn new() -> Self {
        Self {
            channels: RwLock::new(HashMap::new()),
        }
    }
    
    async fn get_or_create_channel(&self, name: &str) -> broadcast::Sender<String> {
        let channels = self.channels.read().await;
        
        if let Some(sender) = channels.get(name) {
            return sender.clone();
        }
        
        drop(channels);
        
        // Create new channel
        let mut channels = self.channels.write().await;
        
        // Double-check in case another task created it
        if let Some(sender) = channels.get(name) {
            return sender.clone();
        }
        
        let (tx, _) = broadcast::channel(1000);
        channels.insert(name.to_string(), tx.clone());
        tx
    }
    
    async fn broadcast_to_channel(&self, channel: &str, message: String) {
        let channels = self.channels.read().await;
        
        if let Some(sender) = channels.get(channel) {
            // Ignore error if no receivers
            let _ = sender.send(message);
        }
    }
    
    async fn subscribe(&self, channel: &str) -> broadcast::Receiver<String> {
        let sender = self.get_or_create_channel(channel).await;
        sender.subscribe()
    }
}

// Global broadcast manager
struct BroadcastManager {
    global_tx: broadcast::Sender<String>,
    channel_manager: Arc<ChannelManager>,
}

impl BroadcastManager {
    fn new() -> Self {
        let (global_tx, _) = broadcast::channel(1000);
        
        Self {
            global_tx,
            channel_manager: Arc::new(ChannelManager::new()),
        }
    }
    
    fn subscribe_global(&self) -> broadcast::Receiver<String> {
        self.global_tx.subscribe()
    }
    
    fn broadcast_global(&self, message: String) {
        let _ = self.global_tx.send(message);
    }
    
    async fn broadcast_to_channel(&self, channel: &str, message: String) {
        self.channel_manager.broadcast_to_channel(channel, message).await;
    }
    
    async fn subscribe_channel(&self, channel: &str) -> broadcast::Receiver<String> {
        self.channel_manager.subscribe(channel).await
    }
}

// Handle individual WebSocket connection
async fn handle_connection(
    stream: TcpStream,
    broadcast_mgr: Arc<BroadcastManager>,
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
    
    // Subscribe to global broadcasts
    let mut global_rx = broadcast_mgr.subscribe_global();
    
    // Channel subscriptions
    let mut channel_receivers: HashMap<String, broadcast::Receiver<String>> = HashMap::new();
    
    // Channel for internal communication
    let (internal_tx, mut internal_rx) = mpsc::unbounded_channel::<Message>();
    
    // Task to handle outgoing messages
    let send_task = tokio::spawn(async move {
        while let Some(msg) = internal_rx.recv().await {
            if ws_sender.send(msg).await.is_err() {
                break;
            }
        }
    });
    
    // Clone for tasks
    let internal_tx_clone = internal_tx.clone();
    let broadcast_mgr_clone = broadcast_mgr.clone();
    
    // Task to handle global broadcasts
    tokio::spawn(async move {
        loop {
            match global_rx.recv().await {
                Ok(msg) => {
                    let json = serde_json::json!({
                        "type": "global",
                        "content": msg
                    });
                    
                    if internal_tx_clone.send(Message::Text(json.to_string())).is_err() {
                        break;
                    }
                }
                Err(broadcast::error::RecvError::Lagged(n)) => {
                    eprintln!("Global broadcast lagged by {} messages", n);
                }
                Err(_) => break,
            }
        }
    });
    
    // Main message processing loop
    while let Some(msg) = ws_receiver.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                // Parse client message
                if let Ok(client_msg) = serde_json::from_str::<ClientMessage>(&text) {
                    match client_msg {
                        ClientMessage::Subscribe { channel } => {
                            println!("Client subscribing to channel: {}", channel);
                            let rx = broadcast_mgr.subscribe_channel(&channel).await;
                            
                            // Spawn task to forward channel messages
                            let tx = internal_tx.clone();
                            let ch = channel.clone();
                            tokio::spawn(async move {
                                let mut receiver = rx;
                                loop {
                                    match receiver.recv().await {
                                        Ok(msg) => {
                                            let json = serde_json::json!({
                                                "type": "channel",
                                                "channel": ch,
                                                "content": msg
                                            });
                                            
                                            if tx.send(Message::Text(json.to_string())).is_err() {
                                                break;
                                            }
                                        }
                                        Err(_) => break,
                                    }
                                }
                            });
                        }
                        
                        ClientMessage::Publish { channel, content } => {
                            println!("Broadcasting to channel {}: {}", channel, content);
                            broadcast_mgr.broadcast_to_channel(&channel, content).await;
                        }
                        
                        ClientMessage::Broadcast { content } => {
                            println!("Global broadcast: {}", content);
                            broadcast_mgr.broadcast_global(content);
                        }
                        
                        ClientMessage::Unsubscribe { channel } => {
                            println!("Client unsubscribing from channel: {}", channel);
                            channel_receivers.remove(&channel);
                        }
                    }
                }
            }
            
            Ok(Message::Close(_)) => {
                println!("Client disconnected");
                break;
            }
            
            Ok(Message::Ping(data)) => {
                let _ = internal_tx.send(Message::Pong(data));
            }
            
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            
            _ => {}
        }
    }
    
    send_task.abort();
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    
    println!("WebSocket broadcast server listening on: {}", addr);
    
    let broadcast_mgr = Arc::new(BroadcastManager::new());
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        let mgr = broadcast_mgr.clone();
        
        tokio::spawn(async move {
            handle_connection(stream, mgr).await;
        });
    }
    
    Ok(())
}
```

---

## Summary

**Message Broadcasting Patterns** are essential for efficient one-to-many communication in WebSocket applications:

### Key Takeaways:

1. **Broadcast Strategies**: Direct broadcasting, pub/sub channels, room-based grouping, and filtered distribution serve different use cases

2. **Performance Optimization**: Serialize messages once, share buffers when possible, and handle backpressure to prevent slow clients from blocking others

3. **Concurrency Management**: Use locks (C/C++) or async channels (Rust) to safely manage shared state across multiple connections

4. **Channel-based Pub/Sub**: Allows selective message routing, reducing bandwidth and processing overhead for clients interested in specific topics

5. **Error Isolation**: Individual client failures (disconnections, slow consumers) should not disrupt broadcasting to other clients

6. **Memory Efficiency**: Both implementations demonstrate buffer reuse and efficient message distribution without unnecessary copying

The C/C++ implementation shows low-level control with explicit mutex management, while the Rust implementation leverages async/await and Tokio's broadcast channels for elegant, type-safe concurrent broadcasting. Both approaches scale well for real-time applications requiring efficient message distribution to multiple clients.