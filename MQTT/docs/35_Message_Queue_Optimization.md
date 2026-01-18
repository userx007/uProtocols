# Message Queue Optimization in MQTT

## Overview

Message Queue Optimization in MQTT focuses on efficiently managing the queues that buffer messages for clients, especially when dealing with QoS 1 and QoS 2 messages, offline clients, or high-throughput scenarios. Proper queue management prevents memory exhaustion, reduces latency, and ensures reliable message delivery.

## Key Concepts

### Queue Types in MQTT

1. **Outbound Message Queue**: Messages waiting to be sent to subscribers
2. **Inbound Message Queue**: Messages received but not yet processed
3. **Session State Queue**: QoS 1/2 messages stored for offline clients
4. **Inflight Messages**: Messages currently being transmitted (QoS > 0)

### Critical Parameters

- **Max Queue Size**: Maximum number of messages per client
- **Queue Drop Policy**: FIFO, LIFO, or priority-based dropping
- **Memory Limits**: Per-client and broker-wide memory constraints
- **Inflight Window**: Maximum concurrent unacknowledged messages
- **Message TTL**: Time-to-live for queued messages

## C/C++ Implementation

### Basic Queue Management with Eclipse Paho MQTT C

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "OptimizedClient"
#define QOS         1
#define TIMEOUT     10000L

// Queue configuration
#define MAX_INFLIGHT_MESSAGES 20
#define MAX_BUFFERED_MESSAGES 100

typedef struct {
    char* topic;
    char* payload;
    int qos;
    size_t payload_size;
} QueuedMessage;

typedef struct {
    QueuedMessage* messages;
    int capacity;
    int size;
    int head;
    int tail;
    size_t total_memory;
    size_t max_memory;
} MessageQueue;

// Initialize circular queue
MessageQueue* queue_init(int capacity, size_t max_memory) {
    MessageQueue* queue = (MessageQueue*)malloc(sizeof(MessageQueue));
    queue->messages = (QueuedMessage*)calloc(capacity, sizeof(QueuedMessage));
    queue->capacity = capacity;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->total_memory = 0;
    queue->max_memory = max_memory;
    return queue;
}

// Enqueue with memory limit checking
int queue_enqueue(MessageQueue* queue, const char* topic, 
                  const char* payload, int qos) {
    size_t msg_size = strlen(topic) + strlen(payload) + 
                      sizeof(QueuedMessage);
    
    // Check memory limit
    if (queue->total_memory + msg_size > queue->max_memory) {
        printf("Memory limit reached. Dropping oldest message.\n");
        // Drop oldest message (FIFO policy)
        if (queue->size > 0) {
            QueuedMessage* old = &queue->messages[queue->head];
            queue->total_memory -= old->payload_size;
            free(old->topic);
            free(old->payload);
            queue->head = (queue->head + 1) % queue->capacity;
            queue->size--;
        }
    }
    
    // Check capacity limit
    if (queue->size >= queue->capacity) {
        printf("Queue full. Cannot enqueue.\n");
        return -1;
    }
    
    // Add new message
    QueuedMessage* msg = &queue->messages[queue->tail];
    msg->topic = strdup(topic);
    msg->payload = strdup(payload);
    msg->qos = qos;
    msg->payload_size = msg_size;
    
    queue->total_memory += msg_size;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    printf("Queued message. Queue size: %d, Memory: %zu bytes\n", 
           queue->size, queue->total_memory);
    return 0;
}

// Dequeue message
QueuedMessage* queue_dequeue(MessageQueue* queue) {
    if (queue->size == 0) {
        return NULL;
    }
    
    QueuedMessage* msg = &queue->messages[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    queue->total_memory -= msg->payload_size;
    
    return msg;
}

// Configure MQTT client with optimized settings
MQTTClient create_optimized_client() {
    MQTTClient client;
    MQTTClient_createOptions create_opts = MQTTClient_createOptions_initializer;
    create_opts.sendWhileDisconnected = 1;
    create_opts.maxBufferedMessages = MAX_BUFFERED_MESSAGES;
    
    MQTTClient_createWithOptions(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL, &create_opts);
    
    return client;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    client = create_optimized_client();
    
    // Configure connection with inflight window
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0; // Persistent session
    conn_opts.maxInflight = MAX_INFLIGHT_MESSAGES;
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    // Initialize message queue with 1MB limit
    MessageQueue* queue = queue_init(MAX_BUFFERED_MESSAGES, 1024 * 1024);
    
    // Simulate message publishing with queue management
    for (int i = 0; i < 150; i++) {
        char topic[50];
        char payload[100];
        sprintf(topic, "sensor/data/%d", i);
        sprintf(payload, "Message %d with some payload data", i);
        
        // Try to enqueue
        if (queue_enqueue(queue, topic, payload, QOS) == 0) {
            // Dequeue and publish
            QueuedMessage* msg = queue_dequeue(queue);
            if (msg) {
                MQTTClient_message pubmsg = MQTTClient_message_initializer;
                pubmsg.payload = msg->payload;
                pubmsg.payloadlen = strlen(msg->payload);
                pubmsg.qos = msg->qos;
                pubmsg.retained = 0;
                
                MQTTClient_deliveryToken token;
                rc = MQTTClient_publishMessage(client, msg->topic, 
                                               &pubmsg, &token);
                
                free(msg->topic);
                free(msg->payload);
            }
        }
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    free(queue->messages);
    free(queue);
    
    return 0;
}
```

### Advanced C++ Queue with Priority

```cpp
#include <iostream>
#include <queue>
#include <memory>
#include <chrono>
#include <mutex>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID("CppOptimizedClient");
const int MAX_QUEUE_SIZE = 1000;
const size_t MAX_MEMORY_BYTES = 10 * 1024 * 1024; // 10MB

struct MQTTMessage {
    std::string topic;
    std::string payload;
    int qos;
    int priority; // Higher value = higher priority
    std::chrono::steady_clock::time_point timestamp;
    
    size_t memory_size() const {
        return topic.size() + payload.size() + sizeof(MQTTMessage);
    }
    
    // For priority queue (higher priority first)
    bool operator<(const MQTTMessage& other) const {
        return priority < other.priority;
    }
};

class OptimizedMessageQueue {
private:
    std::priority_queue<MQTTMessage> queue_;
    std::mutex mutex_;
    size_t current_memory_;
    size_t max_memory_;
    int max_size_;
    std::chrono::seconds message_ttl_;
    
    void enforce_limits() {
        // Remove expired messages
        std::priority_queue<MQTTMessage> temp_queue;
        auto now = std::chrono::steady_clock::now();
        
        while (!queue_.empty()) {
            auto msg = queue_.top();
            queue_.pop();
            
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - msg.timestamp);
            
            if (age < message_ttl_) {
                temp_queue.push(msg);
            } else {
                current_memory_ -= msg.memory_size();
                std::cout << "Dropped expired message on topic: " 
                         << msg.topic << std::endl;
            }
        }
        queue_ = std::move(temp_queue);
        
        // Remove lowest priority messages if over limits
        while (queue_.size() > max_size_ || current_memory_ > max_memory_) {
            if (queue_.empty()) break;
            
            auto msg = queue_.top();
            queue_.pop();
            current_memory_ -= msg.memory_size();
            
            std::cout << "Dropped low-priority message. Queue size: " 
                     << queue_.size() << ", Memory: " 
                     << current_memory_ << " bytes" << std::endl;
        }
    }
    
public:
    OptimizedMessageQueue(int max_size, size_t max_memory, 
                         std::chrono::seconds ttl)
        : current_memory_(0), max_memory_(max_memory), 
          max_size_(max_size), message_ttl_(ttl) {}
    
    bool enqueue(const std::string& topic, const std::string& payload,
                 int qos, int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        MQTTMessage msg{topic, payload, qos, priority, 
                       std::chrono::steady_clock::now()};
        
        size_t msg_size = msg.memory_size();
        
        queue_.push(msg);
        current_memory_ += msg_size;
        
        enforce_limits();
        
        return true;
    }
    
    std::unique_ptr<MQTTMessage> dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return nullptr;
        }
        
        auto msg = std::make_unique<MQTTMessage>(queue_.top());
        queue_.pop();
        current_memory_ -= msg->memory_size();
        
        return msg;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
        return queue_.size();
    }
    
    size_t memory_usage() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
        return current_memory_;
    }
};

int main() {
    // Create optimized queue with 5-minute TTL
    OptimizedMessageQueue msg_queue(MAX_QUEUE_SIZE, MAX_MEMORY_BYTES, 
                                    std::chrono::seconds(300));
    
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(false);
    connOpts.set_max_inflight(20);
    
    try {
        client.connect(connOpts)->wait();
        std::cout << "Connected to MQTT broker" << std::endl;
        
        // Enqueue messages with different priorities
        for (int i = 0; i < 100; i++) {
            std::string topic = "sensor/" + std::to_string(i);
            std::string payload = "Data packet " + std::to_string(i);
            int priority = (i % 10 == 0) ? 10 : 1; // Every 10th message is high priority
            
            msg_queue.enqueue(topic, payload, 1, priority);
        }
        
        std::cout << "Queue size: " << msg_queue.size() 
                 << ", Memory: " << msg_queue.memory_usage() 
                 << " bytes" << std::endl;
        
        // Process queue
        while (auto msg = msg_queue.dequeue()) {
            auto pubmsg = mqtt::make_message(msg->topic, msg->payload);
            pubmsg->set_qos(msg->qos);
            client.publish(pubmsg)->wait();
        }
        
        client.disconnect()->wait();
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use std::collections::VecDeque;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::time::sleep;

const MAX_QUEUE_SIZE: usize = 1000;
const MAX_MEMORY_BYTES: usize = 10 * 1024 * 1024; // 10MB
const MESSAGE_TTL_SECS: u64 = 300; // 5 minutes

#[derive(Clone, Debug)]
struct QueuedMessage {
    topic: String,
    payload: Vec<u8>,
    qos: QoS,
    priority: u8,
    timestamp: Instant,
}

impl QueuedMessage {
    fn memory_size(&self) -> usize {
        self.topic.len() + self.payload.len() + std::mem::size_of::<Self>()
    }
    
    fn is_expired(&self, ttl: Duration) -> bool {
        self.timestamp.elapsed() > ttl
    }
}

struct OptimizedMessageQueue {
    queue: VecDeque<QueuedMessage>,
    current_memory: usize,
    max_memory: usize,
    max_size: usize,
    message_ttl: Duration,
}

impl OptimizedMessageQueue {
    fn new(max_size: usize, max_memory: usize, ttl_secs: u64) -> Self {
        Self {
            queue: VecDeque::with_capacity(max_size),
            current_memory: 0,
            max_memory,
            max_size,
            message_ttl: Duration::from_secs(ttl_secs),
        }
    }
    
    fn enqueue(&mut self, topic: String, payload: Vec<u8>, qos: QoS, priority: u8) -> bool {
        let msg = QueuedMessage {
            topic,
            payload,
            qos,
            priority,
            timestamp: Instant::now(),
        };
        
        let msg_size = msg.memory_size();
        
        // Remove expired messages first
        self.remove_expired();
        
        // Check if we need to drop messages
        while self.current_memory + msg_size > self.max_memory || 
              self.queue.len() >= self.max_size {
            if let Some(dropped) = self.drop_lowest_priority() {
                println!("Dropped message on topic: {} (priority: {})", 
                        dropped.topic, dropped.priority);
            } else {
                break;
            }
        }
        
        // Insert message in priority order (higher priority first)
        let insert_pos = self.queue
            .iter()
            .position(|m| m.priority < msg.priority)
            .unwrap_or(self.queue.len());
        
        self.queue.insert(insert_pos, msg);
        self.current_memory += msg_size;
        
        println!("Enqueued message. Queue size: {}, Memory: {} bytes", 
                self.queue.len(), self.current_memory);
        
        true
    }
    
    fn dequeue(&mut self) -> Option<QueuedMessage> {
        if let Some(msg) = self.queue.pop_front() {
            self.current_memory -= msg.memory_size();
            Some(msg)
        } else {
            None
        }
    }
    
    fn remove_expired(&mut self) {
        self.queue.retain(|msg| {
            let expired = msg.is_expired(self.message_ttl);
            if expired {
                self.current_memory -= msg.memory_size();
                println!("Removed expired message on topic: {}", msg.topic);
            }
            !expired
        });
    }
    
    fn drop_lowest_priority(&mut self) -> Option<QueuedMessage> {
        if let Some(pos) = self.queue.iter()
            .enumerate()
            .min_by_key(|(_, msg)| msg.priority)
            .map(|(i, _)| i) {
            
            if let Some(msg) = self.queue.remove(pos) {
                self.current_memory -= msg.memory_size();
                return Some(msg);
            }
        }
        None
    }
    
    fn size(&self) -> usize {
        self.queue.len()
    }
    
    fn memory_usage(&self) -> usize {
        self.current_memory
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create MQTT options with optimized settings
    let mut mqttoptions = MqttOptions::new("rust_optimized_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_clean_session(false);
    mqttoptions.set_inflight(20); // Max inflight messages
    mqttoptions.set_pending_throttle(Duration::from_millis(100));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 100);
    
    // Create optimized message queue
    let message_queue = Arc::new(Mutex::new(
        OptimizedMessageQueue::new(MAX_QUEUE_SIZE, MAX_MEMORY_BYTES, MESSAGE_TTL_SECS)
    ));
    
    let queue_clone = Arc::clone(&message_queue);
    
    // Spawn task to handle incoming events
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    println!("Received: {:?} on topic: {}", p.payload, p.topic);
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Connection error: {}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });
    
    // Simulate message publishing with queue
    tokio::spawn(async move {
        sleep(Duration::from_secs(1)).await;
        
        for i in 0..150 {
            let topic = format!("sensor/data/{}", i);
            let payload = format!("Message {} with payload", i).into_bytes();
            let priority = if i % 10 == 0 { 10 } else { 1 };
            
            let mut queue = queue_clone.lock().unwrap();
            queue.enqueue(topic.clone(), payload.clone(), QoS::AtLeastOnce, priority);
            
            // Dequeue and publish
            if let Some(msg) = queue.dequeue() {
                drop(queue); // Release lock before async operation
                
                if let Err(e) = client.publish(msg.topic, msg.qos, false, msg.payload).await {
                    eprintln!("Publish error: {}", e);
                }
            }
            
            sleep(Duration::from_millis(10)).await;
        }
        
        let queue = queue_clone.lock().unwrap();
        println!("Final queue size: {}, Memory: {} bytes", 
                queue.size(), queue.memory_usage());
    }).await?;
    
    Ok(())
}
```

## Summary

**Message Queue Optimization** is critical for building scalable and reliable MQTT systems. Key optimization strategies include:

- **Memory Management**: Implementing bounded queues with configurable memory limits prevents broker exhaustion
- **Drop Policies**: FIFO, LIFO, or priority-based dropping ensures important messages are retained
- **Inflight Control**: Limiting concurrent unacknowledged messages prevents network congestion
- **Message TTL**: Expiring old messages prevents stale data accumulation
- **Priority Queuing**: Ensuring critical messages are delivered first in high-load scenarios

The provided implementations demonstrate practical queue management in C/C++ using Eclipse Paho and in Rust using rumqttc, showing circular buffers, priority queues, memory tracking, and TTL enforcement. Proper tuning of these parameters based on application requirements—throughput, latency, reliability, and available resources—is essential for optimal MQTT performance.