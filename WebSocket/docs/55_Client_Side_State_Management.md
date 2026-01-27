# Client-Side State Management in WebSocket Applications

## Overview

Client-side state management in WebSocket applications involves tracking and managing the complete lifecycle of WebSocket connections, handling message queues, implementing offline support, and maintaining application state across connection interruptions. This is crucial for building robust, real-time applications that can gracefully handle network instability, server downtime, and other connectivity issues.

## Key Concepts

### 1. Connection State Management
The WebSocket connection goes through several states that clients must track:
- **CONNECTING**: Initial connection being established
- **OPEN**: Connection established and ready for communication
- **CLOSING**: Connection close handshake initiated
- **CLOSED**: Connection closed or failed to connect

### 2. Message Queue Management
- **Outbound Queue**: Messages waiting to be sent when connection is unavailable
- **Inbound Queue**: Messages received but not yet processed
- **Priority Queuing**: Critical messages sent before non-critical ones
- **Message Deduplication**: Preventing duplicate message processing

### 3. Offline Support
- **Connection Recovery**: Automatic reconnection with exponential backoff
- **State Persistence**: Saving application state locally
- **Message Buffering**: Storing messages during disconnection
- **Sync on Reconnect**: Reconciling state when connection is restored

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <libwebsockets.h>

// Connection states
typedef enum {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_RECONNECTING,
    WS_STATE_ERROR
} WebSocketState;

// Message structure
typedef struct Message {
    char* data;
    size_t length;
    int priority;
    time_t timestamp;
    int retry_count;
    struct Message* next;
} Message;

// Message queue
typedef struct MessageQueue {
    Message* head;
    Message* tail;
    int count;
    int max_size;
} MessageQueue;

// Client state manager
typedef struct ClientStateManager {
    WebSocketState state;
    struct lws* wsi;
    MessageQueue* outbound_queue;
    MessageQueue* inbound_queue;
    
    // Reconnection parameters
    int reconnect_attempts;
    int max_reconnect_attempts;
    int reconnect_delay_ms;
    time_t last_connect_attempt;
    
    // Connection metadata
    char* session_id;
    time_t connected_at;
    time_t disconnected_at;
    
    // Statistics
    unsigned long messages_sent;
    unsigned long messages_received;
    unsigned long connection_failures;
} ClientStateManager;

// Initialize message queue
MessageQueue* create_message_queue(int max_size) {
    MessageQueue* queue = (MessageQueue*)malloc(sizeof(MessageQueue));
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->max_size = max_size;
    return queue;
}

// Enqueue message with priority
bool enqueue_message(MessageQueue* queue, const char* data, 
                     size_t length, int priority) {
    if (queue->count >= queue->max_size) {
        printf("Queue full, dropping message\n");
        return false;
    }
    
    Message* msg = (Message*)malloc(sizeof(Message));
    msg->data = (char*)malloc(length + 1);
    memcpy(msg->data, data, length);
    msg->data[length] = '\0';
    msg->length = length;
    msg->priority = priority;
    msg->timestamp = time(NULL);
    msg->retry_count = 0;
    msg->next = NULL;
    
    // Insert based on priority
    if (queue->head == NULL || queue->head->priority < priority) {
        msg->next = queue->head;
        queue->head = msg;
        if (queue->tail == NULL) {
            queue->tail = msg;
        }
    } else {
        Message* current = queue->head;
        while (current->next != NULL && current->next->priority >= priority) {
            current = current->next;
        }
        msg->next = current->next;
        current->next = msg;
        if (msg->next == NULL) {
            queue->tail = msg;
        }
    }
    
    queue->count++;
    return true;
}

// Dequeue message
Message* dequeue_message(MessageQueue* queue) {
    if (queue->head == NULL) {
        return NULL;
    }
    
    Message* msg = queue->head;
    queue->head = msg->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;
    
    return msg;
}

// Free message
void free_message(Message* msg) {
    if (msg) {
        free(msg->data);
        free(msg);
    }
}

// Initialize client state manager
ClientStateManager* create_client_state_manager() {
    ClientStateManager* manager = (ClientStateManager*)malloc(
        sizeof(ClientStateManager));
    
    manager->state = WS_STATE_DISCONNECTED;
    manager->wsi = NULL;
    manager->outbound_queue = create_message_queue(1000);
    manager->inbound_queue = create_message_queue(1000);
    
    manager->reconnect_attempts = 0;
    manager->max_reconnect_attempts = 10;
    manager->reconnect_delay_ms = 1000;
    manager->last_connect_attempt = 0;
    
    manager->session_id = NULL;
    manager->connected_at = 0;
    manager->disconnected_at = 0;
    
    manager->messages_sent = 0;
    manager->messages_received = 0;
    manager->connection_failures = 0;
    
    return manager;
}

// Update connection state
void update_state(ClientStateManager* manager, WebSocketState new_state) {
    WebSocketState old_state = manager->state;
    manager->state = new_state;
    
    printf("State transition: %d -> %d\n", old_state, new_state);
    
    if (new_state == WS_STATE_CONNECTED) {
        manager->connected_at = time(NULL);
        manager->reconnect_attempts = 0;
        printf("Connected successfully. Processing queued messages...\n");
    } else if (new_state == WS_STATE_DISCONNECTED) {
        manager->disconnected_at = time(NULL);
        manager->connection_failures++;
    }
}

// Calculate exponential backoff delay
int calculate_backoff_delay(ClientStateManager* manager) {
    int base_delay = manager->reconnect_delay_ms;
    int max_delay = 60000; // 60 seconds max
    int delay = base_delay * (1 << manager->reconnect_attempts);
    return (delay > max_delay) ? max_delay : delay;
}

// Attempt reconnection
bool should_reconnect(ClientStateManager* manager) {
    if (manager->reconnect_attempts >= manager->max_reconnect_attempts) {
        printf("Max reconnection attempts reached\n");
        return false;
    }
    
    time_t now = time(NULL);
    int backoff = calculate_backoff_delay(manager) / 1000;
    
    if (now - manager->last_connect_attempt >= backoff) {
        return true;
    }
    
    return false;
}

// Send message (or queue if not connected)
bool send_message(ClientStateManager* manager, const char* data, 
                  size_t length, int priority) {
    if (manager->state == WS_STATE_CONNECTED && manager->wsi) {
        // Prepare buffer with LWS_PRE bytes before actual data
        unsigned char buf[LWS_PRE + length];
        memcpy(&buf[LWS_PRE], data, length);
        
        int result = lws_write(manager->wsi, &buf[LWS_PRE], length, 
                               LWS_WRITE_TEXT);
        
        if (result < 0) {
            printf("Write failed, queueing message\n");
            return enqueue_message(manager->outbound_queue, data, 
                                   length, priority);
        }
        
        manager->messages_sent++;
        return true;
    } else {
        // Queue message for later delivery
        printf("Not connected, queueing message\n");
        return enqueue_message(manager->outbound_queue, data, 
                               length, priority);
    }
}

// Process queued messages
void process_outbound_queue(ClientStateManager* manager) {
    if (manager->state != WS_STATE_CONNECTED || !manager->wsi) {
        return;
    }
    
    while (manager->outbound_queue->count > 0) {
        Message* msg = dequeue_message(manager->outbound_queue);
        if (!msg) break;
        
        unsigned char buf[LWS_PRE + msg->length];
        memcpy(&buf[LWS_PRE], msg->data, msg->length);
        
        int result = lws_write(manager->wsi, &buf[LWS_PRE], 
                               msg->length, LWS_WRITE_TEXT);
        
        if (result < 0) {
            printf("Failed to send queued message, re-queueing\n");
            msg->retry_count++;
            if (msg->retry_count < 3) {
                // Re-queue at end
                enqueue_message(manager->outbound_queue, msg->data, 
                                msg->length, msg->priority);
            }
        } else {
            manager->messages_sent++;
        }
        
        free_message(msg);
    }
}

// Save state to file (persistence)
bool save_state_to_file(ClientStateManager* manager, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to open state file");
        return false;
    }
    
    fprintf(file, "session_id=%s\n", 
            manager->session_id ? manager->session_id : "");
    fprintf(file, "messages_sent=%lu\n", manager->messages_sent);
    fprintf(file, "messages_received=%lu\n", manager->messages_received);
    fprintf(file, "connection_failures=%lu\n", manager->connection_failures);
    
    // Save queued messages
    fprintf(file, "queued_messages=%d\n", manager->outbound_queue->count);
    Message* msg = manager->outbound_queue->head;
    while (msg) {
        fprintf(file, "msg:%d:%ld:%s\n", msg->priority, 
                msg->timestamp, msg->data);
        msg = msg->next;
    }
    
    fclose(file);
    return true;
}

// Load state from file
bool load_state_from_file(ClientStateManager* manager, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        return false;
    }
    
    char line[4096];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "msg:", 4) == 0) {
            int priority;
            long timestamp;
            char data[4000];
            if (sscanf(line, "msg:%d:%ld:%[^\n]", &priority, 
                       &timestamp, data) == 3) {
                enqueue_message(manager->outbound_queue, data, 
                                strlen(data), priority);
            }
        }
    }
    
    fclose(file);
    printf("Loaded %d queued messages from state file\n", 
           manager->outbound_queue->count);
    return true;
}

// Cleanup
void destroy_client_state_manager(ClientStateManager* manager) {
    if (manager) {
        // Free queued messages
        while (manager->outbound_queue->count > 0) {
            Message* msg = dequeue_message(manager->outbound_queue);
            free_message(msg);
        }
        while (manager->inbound_queue->count > 0) {
            Message* msg = dequeue_message(manager->inbound_queue);
            free_message(msg);
        }
        
        free(manager->outbound_queue);
        free(manager->inbound_queue);
        free(manager->session_id);
        free(manager);
    }
}

int main() {
    ClientStateManager* manager = create_client_state_manager();
    
    // Simulate connection lifecycle
    update_state(manager, WS_STATE_CONNECTING);
    
    // Queue some messages while disconnected
    send_message(manager, "Hello, World!", 13, 1);
    send_message(manager, "Important message", 17, 10);
    send_message(manager, "Another message", 15, 5);
    
    printf("Queued messages: %d\n", manager->outbound_queue->count);
    
    // Save state
    save_state_to_file(manager, "client_state.txt");
    
    // Simulate reconnection
    update_state(manager, WS_STATE_CONNECTED);
    
    // Cleanup
    destroy_client_state_manager(manager);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::VecDeque;
use std::time::{Duration, Instant, SystemTime};
use std::sync::{Arc, Mutex};
use tokio_tungstenite::{connect_async, tungstenite::Message as WsMessage};
use futures_util::{StreamExt, SinkExt};
use serde::{Serialize, Deserialize};
use tokio::sync::mpsc;

// Connection states
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error,
}

// Message priority levels
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum Priority {
    Low = 0,
    Normal = 5,
    High = 10,
    Critical = 20,
}

// Queued message structure
#[derive(Debug, Clone)]
struct QueuedMessage {
    data: String,
    priority: Priority,
    timestamp: SystemTime,
    retry_count: u32,
    id: u64,
}

impl QueuedMessage {
    fn new(data: String, priority: Priority, id: u64) -> Self {
        Self {
            data,
            priority,
            timestamp: SystemTime::now(),
            retry_count: 0,
            id,
        }
    }
}

// Message queue with priority support
struct MessageQueue {
    queue: VecDeque<QueuedMessage>,
    max_size: usize,
    next_id: u64,
}

impl MessageQueue {
    fn new(max_size: usize) -> Self {
        Self {
            queue: VecDeque::new(),
            max_size,
            next_id: 0,
        }
    }

    fn enqueue(&mut self, data: String, priority: Priority) -> Result<u64, String> {
        if self.queue.len() >= self.max_size {
            return Err("Queue is full".to_string());
        }

        let id = self.next_id;
        self.next_id += 1;

        let msg = QueuedMessage::new(data, priority, id);
        
        // Insert based on priority (higher priority first)
        let insert_pos = self.queue
            .iter()
            .position(|m| m.priority < priority)
            .unwrap_or(self.queue.len());
        
        self.queue.insert(insert_pos, msg);
        Ok(id)
    }

    fn dequeue(&mut self) -> Option<QueuedMessage> {
        self.queue.pop_front()
    }

    fn len(&self) -> usize {
        self.queue.len()
    }

    fn clear(&mut self) {
        self.queue.clear();
    }
}

// Statistics tracking
#[derive(Debug, Clone, Serialize, Deserialize)]
struct ConnectionStats {
    messages_sent: u64,
    messages_received: u64,
    connection_failures: u64,
    total_reconnects: u64,
    last_connected: Option<SystemTime>,
    last_disconnected: Option<SystemTime>,
}

impl ConnectionStats {
    fn new() -> Self {
        Self {
            messages_sent: 0,
            messages_received: 0,
            connection_failures: 0,
            total_reconnects: 0,
            last_connected: None,
            last_disconnected: None,
        }
    }
}

// Client state manager
pub struct ClientStateManager {
    state: Arc<Mutex<ConnectionState>>,
    outbound_queue: Arc<Mutex<MessageQueue>>,
    inbound_queue: Arc<Mutex<MessageQueue>>,
    
    // Reconnection parameters
    reconnect_attempts: Arc<Mutex<u32>>,
    max_reconnect_attempts: u32,
    base_reconnect_delay: Duration,
    last_connect_attempt: Arc<Mutex<Option<Instant>>>,
    
    // Connection metadata
    session_id: Arc<Mutex<Option<String>>>,
    stats: Arc<Mutex<ConnectionStats>>,
    
    // Channels for communication
    command_tx: mpsc::Sender<ClientCommand>,
}

// Commands for controlling the client
#[derive(Debug)]
enum ClientCommand {
    Connect(String),
    Disconnect,
    SendMessage(String, Priority),
    GetState,
}

impl ClientStateManager {
    pub fn new(
        max_queue_size: usize,
        max_reconnect_attempts: u32,
        base_reconnect_delay: Duration,
    ) -> Self {
        let (command_tx, _command_rx) = mpsc::channel(100);

        Self {
            state: Arc::new(Mutex::new(ConnectionState::Disconnected)),
            outbound_queue: Arc::new(Mutex::new(MessageQueue::new(max_queue_size))),
            inbound_queue: Arc::new(Mutex::new(MessageQueue::new(max_queue_size))),
            reconnect_attempts: Arc::new(Mutex::new(0)),
            max_reconnect_attempts,
            base_reconnect_delay,
            last_connect_attempt: Arc::new(Mutex::new(None)),
            session_id: Arc::new(Mutex::new(None)),
            stats: Arc::new(Mutex::new(ConnectionStats::new())),
            command_tx,
        }
    }

    // Update connection state
    pub fn update_state(&self, new_state: ConnectionState) {
        let mut state = self.state.lock().unwrap();
        let old_state = *state;
        *state = new_state;

        println!("State transition: {:?} -> {:?}", old_state, new_state);

        let mut stats = self.stats.lock().unwrap();
        
        match new_state {
            ConnectionState::Connected => {
                stats.last_connected = Some(SystemTime::now());
                *self.reconnect_attempts.lock().unwrap() = 0;
                println!("Connected successfully");
            }
            ConnectionState::Disconnected | ConnectionState::Error => {
                stats.last_disconnected = Some(SystemTime::now());
                stats.connection_failures += 1;
            }
            _ => {}
        }
    }

    // Calculate exponential backoff delay
    fn calculate_backoff(&self) -> Duration {
        let attempts = *self.reconnect_attempts.lock().unwrap();
        let max_delay = Duration::from_secs(60);
        
        let delay = self.base_reconnect_delay * 2u32.pow(attempts);
        
        if delay > max_delay {
            max_delay
        } else {
            delay
        }
    }

    // Check if reconnection should be attempted
    pub fn should_reconnect(&self) -> bool {
        let attempts = *self.reconnect_attempts.lock().unwrap();
        if attempts >= self.max_reconnect_attempts {
            println!("Max reconnection attempts reached");
            return false;
        }

        let last_attempt = self.last_connect_attempt.lock().unwrap();
        if let Some(last) = *last_attempt {
            let backoff = self.calculate_backoff();
            if last.elapsed() >= backoff {
                return true;
            }
        } else {
            return true;
        }

        false
    }

    // Queue a message for sending
    pub fn queue_message(&self, data: String, priority: Priority) -> Result<u64, String> {
        let mut queue = self.outbound_queue.lock().unwrap();
        queue.enqueue(data, priority)
    }

    // Process outbound queue (send all queued messages)
    pub async fn process_outbound_queue<S>(&self, sink: &mut S) -> Result<(), Box<dyn std::error::Error>>
    where
        S: SinkExt<WsMessage> + Unpin,
        S::Error: std::error::Error + 'static,
    {
        let mut queue = self.outbound_queue.lock().unwrap();
        let mut stats = self.stats.lock().unwrap();

        while let Some(mut msg) = queue.dequeue() {
            match sink.send(WsMessage::Text(msg.data.clone())).await {
                Ok(_) => {
                    stats.messages_sent += 1;
                    println!("Sent queued message ID {}", msg.id);
                }
                Err(e) => {
                    println!("Failed to send message: {:?}", e);
                    msg.retry_count += 1;
                    
                    if msg.retry_count < 3 {
                        // Re-queue for retry
                        queue.queue.push_back(msg);
                    } else {
                        println!("Message {} exceeded retry limit, dropping", msg.id);
                    }
                    break;
                }
            }
        }

        Ok(())
    }

    // Save state to JSON file
    pub fn save_state(&self, path: &str) -> Result<(), Box<dyn std::error::Error>> {
        use std::fs::File;
        use std::io::Write;

        #[derive(Serialize)]
        struct SavedState {
            session_id: Option<String>,
            stats: ConnectionStats,
            queued_messages: Vec<(String, u8, u64)>,
        }

        let session_id = self.session_id.lock().unwrap().clone();
        let stats = self.stats.lock().unwrap().clone();
        let queue = self.outbound_queue.lock().unwrap();
        
        let queued_messages: Vec<(String, u8, u64)> = queue.queue
            .iter()
            .map(|msg| (msg.data.clone(), msg.priority as u8, msg.id))
            .collect();

        let state = SavedState {
            session_id,
            stats,
            queued_messages,
        };

        let json = serde_json::to_string_pretty(&state)?;
        let mut file = File::create(path)?;
        file.write_all(json.as_bytes())?;

        println!("State saved to {}", path);
        Ok(())
    }

    // Load state from JSON file
    pub fn load_state(&self, path: &str) -> Result<(), Box<dyn std::error::Error>> {
        use std::fs::File;
        use std::io::Read;

        #[derive(Deserialize)]
        struct SavedState {
            session_id: Option<String>,
            stats: ConnectionStats,
            queued_messages: Vec<(String, u8, u64)>,
        }

        let mut file = File::open(path)?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;

        let saved: SavedState = serde_json::from_str(&contents)?;

        *self.session_id.lock().unwrap() = saved.session_id;
        *self.stats.lock().unwrap() = saved.stats;

        let mut queue = self.outbound_queue.lock().unwrap();
        for (data, priority_val, _id) in saved.queued_messages {
            let priority = match priority_val {
                0..=2 => Priority::Low,
                3..=7 => Priority::Normal,
                8..=15 => Priority::High,
                _ => Priority::Critical,
            };
            let _ = queue.enqueue(data, priority);
        }

        println!("State loaded from {} ({} queued messages)", 
                 path, queue.len());
        Ok(())
    }

    // Get current statistics
    pub fn get_stats(&self) -> ConnectionStats {
        self.stats.lock().unwrap().clone()
    }

    // Get current state
    pub fn get_state(&self) -> ConnectionState {
        *self.state.lock().unwrap()
    }
}

// Example usage with tokio
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let manager = ClientStateManager::new(
        1000, // max queue size
        10,   // max reconnect attempts
        Duration::from_secs(1), // base reconnect delay
    );

    // Queue some messages while disconnected
    manager.queue_message("Hello, World!".to_string(), Priority::Normal)?;
    manager.queue_message("Important!".to_string(), Priority::High)?;
    manager.queue_message("Background task".to_string(), Priority::Low)?;

    println!("Queued 3 messages");

    // Save state
    manager.save_state("client_state.json")?;

    // Simulate state changes
    manager.update_state(ConnectionState::Connecting);
    tokio::time::sleep(Duration::from_millis(500)).await;
    manager.update_state(ConnectionState::Connected);

    // Print statistics
    let stats = manager.get_stats();
    println!("Statistics: {:#?}", stats);

    Ok(())
}
```

---

## Summary

**Client-side state management** is essential for building robust WebSocket applications that maintain reliability despite network issues. The key aspects include:

1. **Connection State Tracking**: Monitor and react to connection lifecycle events (connecting, connected, disconnected, reconnecting, error states).

2. **Message Queue Management**: Implement priority-based queues for both outbound and inbound messages, ensuring critical messages are processed first and nothing is lost during disconnections.

3. **Offline Support**: Buffer messages when offline, implement exponential backoff for reconnection attempts, and synchronize state when the connection is restored.

4. **State Persistence**: Save application state and queued messages to disk, allowing recovery after application restarts or crashes.

5. **Automatic Reconnection**: Implement intelligent reconnection logic with exponential backoff to avoid overwhelming the server while ensuring eventual reconnection.

6. **Statistics & Monitoring**: Track connection metrics (messages sent/received, failures, reconnection attempts) for debugging and performance analysis.

Both implementations demonstrate production-ready patterns including priority queuing, retry logic with limits, state persistence, and comprehensive error handling. The C implementation uses libwebsockets with manual memory management, while the Rust implementation leverages tokio-tungstenite with async/await and thread-safe primitives for modern concurrent programming.