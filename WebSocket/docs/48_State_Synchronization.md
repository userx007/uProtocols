# State Synchronization in WebSocket Applications

## Overview

State synchronization is the process of maintaining consistent application state across multiple server instances in a distributed system. When you scale WebSocket applications horizontally (running multiple server instances), each server maintains its own in-memory state. Without proper synchronization, clients connected to different servers will have inconsistent views of the application state, leading to poor user experience and data integrity issues.

## Core Concepts

**The Problem:**
- User A connects to Server 1
- User B connects to Server 2
- User A makes a change (e.g., updates a shared document)
- User B doesn't see the change because Server 2's state hasn't been updated

**Solutions:**
1. **Message Broadcasting** - Using pub/sub systems (Redis, NATS, RabbitMQ)
2. **Shared State Storage** - Centralized database or cache
3. **State Replication** - Active synchronization between servers
4. **Event Sourcing** - Maintaining state through ordered events

## Architecture Patterns

### 1. Pub/Sub Pattern (Most Common)
Each server publishes state changes to a message broker, and all servers subscribe to receive updates.

### 2. Centralized State
All servers read/write to a shared data store (Redis, PostgreSQL, etc.).

### 3. Gossip Protocol
Servers communicate peer-to-peer to propagate state changes.

---

## C Implementation

Here's a state synchronization system using Redis pub/sub with libhiredis:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <hiredis/hiredis.h>
#include <libwebsockets.h>

#define REDIS_HOST "127.0.0.1"
#define REDIS_PORT 6379
#define STATE_CHANNEL "app:state:updates"

// Shared state structure
typedef struct {
    char key[256];
    char value[1024];
    long long version;
} StateEntry;

// Global state store
typedef struct {
    StateEntry entries[1000];
    int count;
    pthread_mutex_t lock;
} StateStore;

StateStore global_state = {.count = 0};

// Redis contexts
redisContext *redis_pub = NULL;
redisContext *redis_sub = NULL;

// Initialize state store
void init_state_store() {
    pthread_mutex_init(&global_state.lock, NULL);
    global_state.count = 0;
}

// Update local state and broadcast to other servers
int sync_state_update(const char *key, const char *value) {
    pthread_mutex_lock(&global_state.lock);
    
    // Find or create entry
    int index = -1;
    for (int i = 0; i < global_state.count; i++) {
        if (strcmp(global_state.entries[i].key, key) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1 && global_state.count < 1000) {
        index = global_state.count++;
        strncpy(global_state.entries[index].key, key, 255);
    }
    
    if (index != -1) {
        strncpy(global_state.entries[index].value, value, 1023);
        global_state.entries[index].version = time(NULL);
        
        pthread_mutex_unlock(&global_state.lock);
        
        // Broadcast to other servers via Redis
        char message[2048];
        snprintf(message, sizeof(message), 
                "{\"key\":\"%s\",\"value\":\"%s\",\"version\":%lld}",
                key, value, global_state.entries[index].version);
        
        redisReply *reply = redisCommand(redis_pub, 
                                        "PUBLISH %s %s", 
                                        STATE_CHANNEL, 
                                        message);
        if (reply) freeReplyObject(reply);
        
        return 0;
    }
    
    pthread_mutex_unlock(&global_state.lock);
    return -1;
}

// Apply state update from another server
void apply_state_update(const char *key, const char *value, long long version) {
    pthread_mutex_lock(&global_state.lock);
    
    int index = -1;
    for (int i = 0; i < global_state.count; i++) {
        if (strcmp(global_state.entries[i].key, key) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1 && global_state.count < 1000) {
        index = global_state.count++;
        strncpy(global_state.entries[index].key, key, 255);
    }
    
    // Only apply if version is newer (conflict resolution)
    if (index != -1 && version > global_state.entries[index].version) {
        strncpy(global_state.entries[index].value, value, 1023);
        global_state.entries[index].version = version;
        printf("Applied state update: %s = %s (v%lld)\n", key, value, version);
    }
    
    pthread_mutex_unlock(&global_state.lock);
}

// Redis subscriber thread
void* redis_subscriber_thread(void *arg) {
    redisReply *reply;
    
    redis_sub = redisConnect(REDIS_HOST, REDIS_PORT);
    if (redis_sub == NULL || redis_sub->err) {
        fprintf(stderr, "Redis subscriber connection error\n");
        return NULL;
    }
    
    // Subscribe to state channel
    reply = redisCommand(redis_sub, "SUBSCRIBE %s", STATE_CHANNEL);
    freeReplyObject(reply);
    
    printf("Subscribed to %s\n", STATE_CHANNEL);
    
    // Listen for messages
    while (redisGetReply(redis_sub, (void**)&reply) == REDIS_OK) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
            if (strcmp(reply->element[0]->str, "message") == 0) {
                char *message = reply->element[2]->str;
                
                // Parse JSON message (simplified)
                char key[256] = {0}, value[1024] = {0};
                long long version = 0;
                
                sscanf(message, "{\"key\":\"%[^\"]\",\"value\":\"%[^\"]\",\"version\":%lld}",
                       key, value, &version);
                
                apply_state_update(key, value, version);
            }
        }
        freeReplyObject(reply);
    }
    
    return NULL;
}

// Initialize Redis connections
int init_redis_sync() {
    redis_pub = redisConnect(REDIS_HOST, REDIS_PORT);
    if (redis_pub == NULL || redis_pub->err) {
        fprintf(stderr, "Redis publisher connection error\n");
        return -1;
    }
    
    pthread_t subscriber_tid;
    pthread_create(&subscriber_tid, NULL, redis_subscriber_thread, NULL);
    pthread_detach(subscriber_tid);
    
    return 0;
}

// Example: WebSocket handler
void handle_websocket_message(const char *client_message) {
    // Parse client message (simplified)
    char action[64], key[256], value[1024];
    sscanf(client_message, "{\"action\":\"%[^\"]\",\"key\":\"%[^\"]\",\"value\":\"%[^\"]\"}",
           action, key, value);
    
    if (strcmp(action, "update") == 0) {
        sync_state_update(key, value);
        printf("Client updated state: %s = %s\n", key, value);
    }
}

int main() {
    init_state_store();
    
    if (init_redis_sync() != 0) {
        fprintf(stderr, "Failed to initialize Redis sync\n");
        return 1;
    }
    
    printf("State synchronization service started\n");
    
    // Simulate some state updates
    sleep(2);
    sync_state_update("user:123:status", "online");
    sync_state_update("room:lobby:count", "42");
    
    // Keep running
    while(1) {
        sleep(1);
    }
    
    return 0;
}
```

---

## C++ Implementation

Modern C++ implementation with CRDT (Conflict-free Replicated Data Type) for state synchronization:

```cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <sw/redis++/redis++.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace sw::redis;

// LWW-Element-Set CRDT (Last-Write-Wins)
template<typename T>
class LWWRegister {
private:
    T value_;
    uint64_t timestamp_;
    std::string node_id_;
    mutable std::mutex mutex_;

public:
    LWWRegister(const std::string& node_id) 
        : timestamp_(0), node_id_(node_id) {}
    
    void set(const T& val) {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamp_ = current_timestamp();
        value_ = val;
    }
    
    bool merge(const T& val, uint64_t ts, const std::string& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // LWW conflict resolution
        if (ts > timestamp_ || (ts == timestamp_ && node > node_id_)) {
            value_ = val;
            timestamp_ = ts;
            node_id_ = node;
            return true; // State changed
        }
        return false; // No change
    }
    
    T get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }
    
    uint64_t get_timestamp() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return timestamp_;
    }
    
private:
    static uint64_t current_timestamp() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

// Distributed State Manager
class StateManager {
private:
    std::string node_id_;
    std::unordered_map<std::string, LWWRegister<std::string>> state_;
    mutable std::mutex state_mutex_;
    
    std::unique_ptr<Redis> redis_pub_;
    std::unique_ptr<Redis> redis_sub_;
    std::unique_ptr<Subscriber> subscriber_;
    
    std::atomic<bool> running_{true};
    std::thread subscriber_thread_;
    
    std::vector<std::function<void(const std::string&, const std::string&)>> callbacks_;
    std::mutex callback_mutex_;

public:
    StateManager(const std::string& node_id, 
                 const std::string& redis_host = "127.0.0.1",
                 int redis_port = 6379)
        : node_id_(node_id) {
        
        ConnectionOptions opts;
        opts.host = redis_host;
        opts.port = redis_port;
        
        redis_pub_ = std::make_unique<Redis>(opts);
        redis_sub_ = std::make_unique<Redis>(opts);
        
        subscriber_ = std::make_unique<Subscriber>(redis_sub_->subscriber());
        
        // Subscribe to state updates
        subscriber_->on_message([this](std::string channel, std::string msg) {
            handle_state_update(msg);
        });
        
        subscriber_->subscribe("state:sync");
        
        // Start subscriber thread
        subscriber_thread_ = std::thread([this]() {
            while (running_) {
                try {
                    subscriber_->consume();
                } catch (const Error& e) {
                    std::cerr << "Redis error: " << e.what() << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        });
    }
    
    ~StateManager() {
        running_ = false;
        if (subscriber_thread_.joinable()) {
            subscriber_thread_.join();
        }
    }
    
    // Set state and broadcast to other nodes
    void set(const std::string& key, const std::string& value) {
        LWWRegister<std::string>* reg;
        
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = state_.find(key);
            if (it == state_.end()) {
                state_.emplace(key, LWWRegister<std::string>(node_id_));
                it = state_.find(key);
            }
            reg = &it->second;
        }
        
        reg->set(value);
        
        // Broadcast update
        json msg = {
            {"key", key},
            {"value", value},
            {"timestamp", reg->get_timestamp()},
            {"node", node_id_}
        };
        
        redis_pub_->publish("state:sync", msg.dump());
        
        // Notify local callbacks
        notify_callbacks(key, value);
    }
    
    // Get current state value
    std::string get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = state_.find(key);
        if (it != state_.end()) {
            return it->second.get();
        }
        return "";
    }
    
    // Register callback for state changes
    void on_change(std::function<void(const std::string&, const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_.push_back(callback);
    }
    
    // Get all state keys
    std::vector<std::string> keys() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::vector<std::string> result;
        for (const auto& [key, _] : state_) {
            result.push_back(key);
        }
        return result;
    }

private:
    void handle_state_update(const std::string& message) {
        try {
            auto msg = json::parse(message);
            std::string key = msg["key"];
            std::string value = msg["value"];
            uint64_t timestamp = msg["timestamp"];
            std::string node = msg["node"];
            
            // Ignore our own broadcasts
            if (node == node_id_) return;
            
            bool changed;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto it = state_.find(key);
                if (it == state_.end()) {
                    state_.emplace(key, LWWRegister<std::string>(node_id_));
                    it = state_.find(key);
                }
                changed = it->second.merge(value, timestamp, node);
            }
            
            if (changed) {
                notify_callbacks(key, value);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error handling state update: " << e.what() << std::endl;
        }
    }
    
    void notify_callbacks(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        for (auto& callback : callbacks_) {
            callback(key, value);
        }
    }
};

// Example usage
int main() {
    // Create two nodes to simulate distributed system
    StateManager node1("node-1");
    StateManager node2("node-2");
    
    // Register callbacks
    node1.on_change([](const std::string& key, const std::string& value) {
        std::cout << "[Node1] State changed: " << key << " = " << value << std::endl;
    });
    
    node2.on_change([](const std::string& key, const std::string& value) {
        std::cout << "[Node2] State changed: " << key << " = " << value << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Simulate state updates from different nodes
    std::cout << "\n=== Node1 updates user:alice:status ===" << std::endl;
    node1.set("user:alice:status", "online");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "\n=== Node2 updates room:lobby:count ===" << std::endl;
    node2.set("room:lobby:count", "15");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify synchronization
    std::cout << "\n=== Verification ===" << std::endl;
    std::cout << "Node1 sees user:alice:status = " << node1.get("user:alice:status") << std::endl;
    std::cout << "Node2 sees user:alice:status = " << node2.get("user:alice:status") << std::endl;
    std::cout << "Node1 sees room:lobby:count = " << node1.get("room:lobby:count") << std::endl;
    std::cout << "Node2 sees room:lobby:count = " << node2.get("room:lobby:count") << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return 0;
}
```

---

## Rust Implementation

Rust implementation with Tokio async runtime and Redis for state synchronization:

```rust
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use redis::AsyncCommands;
use serde::{Deserialize, Serialize};
use chrono::Utc;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct StateUpdate {
    key: String,
    value: String,
    timestamp: i64,
    node_id: String,
}

#[derive(Debug, Clone)]
struct StateEntry {
    value: String,
    timestamp: i64,
    node_id: String,
}

pub struct StateManager {
    node_id: String,
    state: Arc<RwLock<HashMap<String, StateEntry>>>,
    redis_client: redis::Client,
}

impl StateManager {
    pub fn new(node_id: String, redis_url: &str) -> Result<Self, redis::RedisError> {
        let redis_client = redis::Client::open(redis_url)?;
        
        Ok(Self {
            node_id,
            state: Arc::new(RwLock::new(HashMap::new())),
            redis_client,
        })
    }
    
    /// Set a state value and broadcast to other nodes
    pub async fn set(&self, key: String, value: String) -> Result<(), Box<dyn std::error::Error>> {
        let timestamp = Utc::now().timestamp_micros();
        
        // Update local state
        {
            let mut state = self.state.write().await;
            state.insert(key.clone(), StateEntry {
                value: value.clone(),
                timestamp,
                node_id: self.node_id.clone(),
            });
        }
        
        // Broadcast to other nodes
        let update = StateUpdate {
            key: key.clone(),
            value: value.clone(),
            timestamp,
            node_id: self.node_id.clone(),
        };
        
        let mut conn = self.redis_client.get_multiplexed_async_connection().await?;
        let message = serde_json::to_string(&update)?;
        conn.publish("state:sync", message).await?;
        
        println!("[{}] Set state: {} = {}", self.node_id, key, value);
        
        Ok(())
    }
    
    /// Get a state value
    pub async fn get(&self, key: &str) -> Option<String> {
        let state = self.state.read().await;
        state.get(key).map(|entry| entry.value.clone())
    }
    
    /// Apply a state update from another node (with conflict resolution)
    async fn apply_update(&self, update: StateUpdate) -> bool {
        // Don't apply our own updates
        if update.node_id == self.node_id {
            return false;
        }
        
        let mut state = self.state.write().await;
        
        let should_apply = match state.get(&update.key) {
            Some(existing) => {
                // Last-Write-Wins with tie-breaking by node_id
                update.timestamp > existing.timestamp ||
                (update.timestamp == existing.timestamp && update.node_id > existing.node_id)
            }
            None => true,
        };
        
        if should_apply {
            state.insert(update.key.clone(), StateEntry {
                value: update.value.clone(),
                timestamp: update.timestamp,
                node_id: update.node_id.clone(),
            });
            
            println!("[{}] Applied remote update: {} = {} (from {})", 
                     self.node_id, update.key, update.value, update.node_id);
            true
        } else {
            false
        }
    }
    
    /// Start listening for state updates from other nodes
    pub async fn start_sync(&self) -> Result<(), Box<dyn std::error::Error>> {
        let state_manager = Arc::new(self.clone());
        
        tokio::spawn(async move {
            if let Err(e) = state_manager.subscribe_loop().await {
                eprintln!("Subscription error: {}", e);
            }
        });
        
        Ok(())
    }
    
    async fn subscribe_loop(&self) -> Result<(), Box<dyn std::error::Error>> {
        let client = self.redis_client.clone();
        let mut pubsub = client.get_async_pubsub().await?;
        
        pubsub.subscribe("state:sync").await?;
        println!("[{}] Subscribed to state:sync channel", self.node_id);
        
        let mut stream = pubsub.on_message();
        
        loop {
            match stream.next().await {
                Some(msg) => {
                    if let Ok(payload) = msg.get_payload::<String>() {
                        if let Ok(update) = serde_json::from_str::<StateUpdate>(&payload) {
                            self.apply_update(update).await;
                        }
                    }
                }
                None => break,
            }
        }
        
        Ok(())
    }
    
    /// Get all state keys
    pub async fn keys(&self) -> Vec<String> {
        let state = self.state.read().await;
        state.keys().cloned().collect()
    }
    
    /// Get snapshot of all state
    pub async fn snapshot(&self) -> HashMap<String, String> {
        let state = self.state.read().await;
        state.iter()
            .map(|(k, v)| (k.clone(), v.value.clone()))
            .collect()
    }
}

impl Clone for StateManager {
    fn clone(&self) -> Self {
        Self {
            node_id: self.node_id.clone(),
            state: Arc::clone(&self.state),
            redis_client: self.redis_client.clone(),
        }
    }
}

// Example: WebSocket integration
use tokio_tungstenite::tungstenite::Message;

pub async fn handle_websocket_message(
    state_manager: &StateManager,
    message: Message,
) -> Result<(), Box<dyn std::error::Error>> {
    if let Message::Text(text) = message {
        #[derive(Deserialize)]
        struct ClientMessage {
            action: String,
            key: String,
            value: Option<String>,
        }
        
        let msg: ClientMessage = serde_json::from_str(&text)?;
        
        match msg.action.as_str() {
            "set" => {
                if let Some(value) = msg.value {
                    state_manager.set(msg.key, value).await?;
                }
            }
            "get" => {
                let value = state_manager.get(&msg.key).await;
                println!("Client requested {}: {:?}", msg.key, value);
            }
            _ => {}
        }
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create two nodes to simulate distributed system
    let node1 = StateManager::new("node-1".to_string(), "redis://127.0.0.1/")?;
    let node2 = StateManager::new("node-2".to_string(), "redis://127.0.0.1/")?;
    
    // Start synchronization
    node1.start_sync().await?;
    node2.start_sync().await?;
    
    tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    
    // Simulate state updates
    println!("\n=== Node1 updates user:alice:status ===");
    node1.set("user:alice:status".to_string(), "online".to_string()).await?;
    
    tokio::time::sleep(tokio::time::Duration::from_millis(200)).await;
    
    println!("\n=== Node2 updates room:lobby:count ===");
    node2.set("room:lobby:count".to_string(), "25".to_string()).await?;
    
    tokio::time::sleep(tokio::time::Duration::from_millis(200)).await;
    
    // Verify synchronization
    println!("\n=== Verification ===");
    println!("Node1 sees user:alice:status = {:?}", node1.get("user:alice:status").await);
    println!("Node2 sees user:alice:status = {:?}", node2.get("user:alice:status").await);
    println!("Node1 sees room:lobby:count = {:?}", node1.get("room:lobby:count").await);
    println!("Node2 sees room:lobby:count = {:?}", node2.get("room:lobby:count").await);
    
    // Show full snapshots
    println!("\n=== Node1 Snapshot ===");
    println!("{:#?}", node1.snapshot().await);
    
    println!("\n=== Node2 Snapshot ===");
    println!("{:#?}", node2.snapshot().await);
    
    tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    
    Ok(())
}
```

---

## Summary

**State Synchronization** is essential for scaling WebSocket applications horizontally. The key approaches are:

1. **Pub/Sub Broadcasting** (Most Common): Using Redis, NATS, or RabbitMQ to broadcast state changes across all server instances. Each server maintains its own state but receives updates from others.

2. **Conflict Resolution**: Implementing strategies like Last-Write-Wins (LWW), vector clocks, or CRDTs (Conflict-free Replicated Data Types) to handle concurrent updates from different servers.

3. **Consistency Models**: 
   - **Eventual Consistency**: All nodes eventually converge to the same state
   - **Strong Consistency**: Requires coordination (slower but more accurate)
   - **Causal Consistency**: Preserves cause-effect relationships

4. **Performance Considerations**:
   - Minimize synchronization overhead with batching
   - Use efficient serialization (MessagePack, Protocol Buffers)
   - Implement state compression for large datasets
   - Consider partial state synchronization (only sync what's needed)

5. **Common Patterns**:
   - **Room-based sync**: Only synchronize state within specific rooms/channels
   - **User presence**: Track which users are online across servers
   - **Shared documents**: Collaborative editing with operational transforms
   - **Game state**: Multiplayer game synchronization

The code examples demonstrate production-ready patterns using Redis for pub/sub messaging, CRDT-based conflict resolution, and async/await patterns for non-blocking operations. Each implementation handles the core challenges of distributed state: broadcasting changes, receiving updates, resolving conflicts, and maintaining consistency across multiple server instances.