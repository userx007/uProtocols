# Memory Management in C MQTT

Memory management is critical when implementing MQTT clients in C because unlike higher-level languages, C requires manual allocation and deallocation of memory. Poor memory management leads to memory leaks, buffer overflows, use-after-free errors, and crashes—particularly problematic in long-running MQTT applications that maintain persistent connections.

## Core Concepts

### Why Memory Management Matters in MQTT

MQTT clients typically:
- Maintain persistent connections for hours or days
- Handle variable-length messages (payloads can range from bytes to megabytes)
- Queue messages during disconnections
- Store subscription state and topic strings
- Process callbacks asynchronously

Without proper memory management, these operations can gradually consume all available memory or cause crashes.

### Common Memory Issues

1. **Memory Leaks**: Allocated memory never freed
2. **Double Free**: Freeing the same memory twice
3. **Use-After-Free**: Accessing memory after it's freed
4. **Buffer Overflows**: Writing beyond allocated boundaries
5. **Dangling Pointers**: Pointers to freed memory

## C/C++ Implementation

### Basic MQTT Client with Proper Memory Management

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>

// Structure to manage message queue with proper cleanup
typedef struct message_node {
    char *topic;
    void *payload;
    int payload_len;
    struct message_node *next;
} message_node_t;

typedef struct {
    struct mosquitto *mosq;
    message_node_t *queue_head;
    int queue_size;
    int max_queue_size;
} mqtt_client_t;

// Allocate and initialize client
mqtt_client_t* mqtt_client_create(const char *client_id, int max_queue) {
    mqtt_client_t *client = (mqtt_client_t*)calloc(1, sizeof(mqtt_client_t));
    if (!client) {
        fprintf(stderr, "Failed to allocate client structure\n");
        return NULL;
    }
    
    client->max_queue_size = max_queue;
    client->queue_head = NULL;
    client->queue_size = 0;
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    client->mosq = mosquitto_new(client_id, true, client);
    if (!client->mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        free(client);
        return NULL;
    }
    
    return client;
}

// Add message to queue with bounds checking
int mqtt_queue_message(mqtt_client_t *client, const char *topic, 
                       const void *payload, int payload_len) {
    if (!client || !topic || !payload) {
        return -1;
    }
    
    // Check queue limit to prevent unbounded growth
    if (client->queue_size >= client->max_queue_size) {
        fprintf(stderr, "Queue full, dropping message\n");
        return -1;
    }
    
    // Allocate new node
    message_node_t *node = (message_node_t*)malloc(sizeof(message_node_t));
    if (!node) {
        fprintf(stderr, "Failed to allocate message node\n");
        return -1;
    }
    
    // Duplicate topic string
    node->topic = strdup(topic);
    if (!node->topic) {
        free(node);
        return -1;
    }
    
    // Allocate and copy payload
    node->payload = malloc(payload_len);
    if (!node->payload) {
        free(node->topic);
        free(node);
        return -1;
    }
    memcpy(node->payload, payload, payload_len);
    node->payload_len = payload_len;
    
    // Add to queue
    node->next = client->queue_head;
    client->queue_head = node;
    client->queue_size++;
    
    return 0;
}

// Free a single message node
void mqtt_free_message_node(message_node_t *node) {
    if (node) {
        free(node->topic);
        free(node->payload);
        free(node);
    }
}

// Clear entire message queue
void mqtt_clear_queue(mqtt_client_t *client) {
    if (!client) return;
    
    message_node_t *current = client->queue_head;
    while (current) {
        message_node_t *next = current->next;
        mqtt_free_message_node(current);
        current = next;
    }
    
    client->queue_head = NULL;
    client->queue_size = 0;
}

// Properly destroy client and free all resources
void mqtt_client_destroy(mqtt_client_t *client) {
    if (!client) return;
    
    // Clear message queue
    mqtt_clear_queue(client);
    
    // Destroy mosquitto instance
    if (client->mosq) {
        mosquitto_disconnect(client->mosq);
        mosquitto_destroy(client->mosq);
    }
    
    // Cleanup mosquitto library
    mosquitto_lib_cleanup();
    
    // Free client structure
    free(client);
}

// Callback with proper memory handling
void on_message_callback(struct mosquitto *mosq, void *userdata, 
                        const struct mosquitto_message *msg) {
    if (!msg || !msg->payload) return;
    
    // Allocate memory for processing
    char *payload_copy = (char*)malloc(msg->payloadlen + 1);
    if (!payload_copy) {
        fprintf(stderr, "Failed to allocate memory for payload\n");
        return;
    }
    
    memcpy(payload_copy, msg->payload, msg->payloadlen);
    payload_copy[msg->payloadlen] = '\0';
    
    printf("Received: %s\n", payload_copy);
    
    // Clean up
    free(payload_copy);
}

// Example usage with cleanup
int main() {
    mqtt_client_t *client = mqtt_client_create("c_client", 100);
    if (!client) {
        return 1;
    }
    
    mosquitto_message_callback_set(client->mosq, on_message_callback);
    
    // Connect and subscribe
    if (mosquitto_connect(client->mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Connection failed\n");
        mqtt_client_destroy(client);
        return 1;
    }
    
    mosquitto_subscribe(client->mosq, NULL, "test/topic", 0);
    
    // Queue some messages
    mqtt_queue_message(client, "test/topic", "Hello", 5);
    
    // Run loop (in real app, handle signals for cleanup)
    mosquitto_loop_start(client->mosq);
    
    // Simulate running for a while
    sleep(10);
    
    mosquitto_loop_stop(client->mosq, true);
    
    // Proper cleanup
    mqtt_client_destroy(client);
    
    return 0;
}
```

### Advanced: Custom Allocator for MQTT

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Memory pool for fixed-size allocations
typedef struct {
    void *pool;
    size_t block_size;
    size_t total_blocks;
    size_t used_blocks;
    unsigned char *free_list;
} memory_pool_t;

memory_pool_t* pool_create(size_t block_size, size_t num_blocks) {
    memory_pool_t *pool = (memory_pool_t*)malloc(sizeof(memory_pool_t));
    if (!pool) return NULL;
    
    pool->block_size = block_size;
    pool->total_blocks = num_blocks;
    pool->used_blocks = 0;
    
    // Allocate memory pool
    pool->pool = malloc(block_size * num_blocks);
    if (!pool->pool) {
        free(pool);
        return NULL;
    }
    
    // Initialize free list
    pool->free_list = (unsigned char*)calloc(num_blocks, 1);
    if (!pool->free_list) {
        free(pool->pool);
        free(pool);
        return NULL;
    }
    
    return pool;
}

void* pool_alloc(memory_pool_t *pool) {
    if (!pool || pool->used_blocks >= pool->total_blocks) {
        return NULL;
    }
    
    // Find free block
    for (size_t i = 0; i < pool->total_blocks; i++) {
        if (pool->free_list[i] == 0) {
            pool->free_list[i] = 1;
            pool->used_blocks++;
            return (char*)pool->pool + (i * pool->block_size);
        }
    }
    
    return NULL;
}

void pool_free(memory_pool_t *pool, void *ptr) {
    if (!pool || !ptr) return;
    
    size_t offset = (char*)ptr - (char*)pool->pool;
    size_t index = offset / pool->block_size;
    
    if (index < pool->total_blocks && pool->free_list[index] == 1) {
        pool->free_list[index] = 0;
        pool->used_blocks--;
    }
}

void pool_destroy(memory_pool_t *pool) {
    if (pool) {
        free(pool->pool);
        free(pool->free_list);
        free(pool);
    }
}

// Usage statistics
void pool_stats(memory_pool_t *pool) {
    if (pool) {
        printf("Pool: %zu/%zu blocks used (%.1f%%)\n",
               pool->used_blocks, pool->total_blocks,
               (pool->used_blocks * 100.0) / pool->total_blocks);
    }
}
```

## Rust Implementation

Rust provides automatic memory management through ownership and RAII, preventing most memory issues at compile time.

```rust
use rumqttc::{MqttOptions, AsyncClient, QoS, Event, Packet};
use tokio::time::Duration;
use std::sync::Arc;
use tokio::sync::Mutex;

// Message queue with automatic cleanup
struct MessageQueue {
    messages: Vec<QueuedMessage>,
    max_size: usize,
}

struct QueuedMessage {
    topic: String,
    payload: Vec<u8>,
}

impl MessageQueue {
    fn new(max_size: usize) -> Self {
        MessageQueue {
            messages: Vec::with_capacity(max_size),
            max_size,
        }
    }
    
    fn push(&mut self, topic: String, payload: Vec<u8>) -> Result<(), &'static str> {
        if self.messages.len() >= self.max_size {
            return Err("Queue full");
        }
        
        self.messages.push(QueuedMessage { topic, payload });
        Ok(())
    }
    
    fn clear(&mut self) {
        // Vector automatically deallocates when cleared
        self.messages.clear();
    }
    
    fn size(&self) -> usize {
        self.messages.len()
    }
}

// MQTT client with automatic resource management
struct MqttClient {
    client: AsyncClient,
    queue: Arc<Mutex<MessageQueue>>,
}

impl MqttClient {
    async fn new(client_id: &str, max_queue: usize) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, "localhost", 1883);
        mqtt_options.set_keep_alive(Duration::from_secs(60));
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        let queue = Arc::new(Mutex::new(MessageQueue::new(max_queue)));
        
        // Spawn event loop handler
        let queue_clone = queue.clone();
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(p))) => {
                        let mut q = queue_clone.lock().await;
                        let _ = q.push(
                            p.topic.to_string(),
                            p.payload.to_vec()
                        );
                        
                        // Process message
                        if let Ok(msg) = String::from_utf8(p.payload.to_vec()) {
                            println!("Received: {}", msg);
                        }
                    }
                    Ok(_) => {}
                    Err(e) => {
                        eprintln!("Error: {:?}", e);
                        break;
                    }
                }
            }
        });
        
        Ok(MqttClient { client, queue })
    }
    
    async fn publish(&self, topic: &str, payload: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        self.client.publish(topic, QoS::AtLeastOnce, false, payload).await?;
        Ok(())
    }
    
    async fn subscribe(&self, topic: &str) -> Result<(), Box<dyn std::error::Error>> {
        self.client.subscribe(topic, QoS::AtLeastOnce).await?;
        Ok(())
    }
    
    async fn queue_size(&self) -> usize {
        self.queue.lock().await.size()
    }
    
    async fn clear_queue(&self) {
        self.queue.lock().await.clear();
    }
}

// Drop trait ensures cleanup
impl Drop for MqttClient {
    fn drop(&mut self) {
        println!("MQTT client being cleaned up");
        // Rust automatically cleans up all resources
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = MqttClient::new("rust_client", 100).await?;
    
    client.subscribe("test/topic").await?;
    client.publish("test/topic", b"Hello from Rust").await?;
    
    tokio::time::sleep(Duration::from_secs(5)).await;
    
    println!("Queue size: {}", client.queue_size().await);
    client.clear_queue().await;
    
    // Automatic cleanup when client goes out of scope
    Ok(())
}
```

### Memory-Safe Custom Allocator in Rust

```rust
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

// Custom allocator that tracks memory usage
struct TrackingAllocator {
    allocated: AtomicUsize,
    deallocated: AtomicUsize,
}

unsafe impl GlobalAlloc for TrackingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let ptr = System.alloc(layout);
        if !ptr.is_null() {
            self.allocated.fetch_add(layout.size(), Ordering::SeqCst);
        }
        ptr
    }
    
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        System.dealloc(ptr, layout);
        self.deallocated.fetch_add(layout.size(), Ordering::SeqCst);
    }
}

impl TrackingAllocator {
    const fn new() -> Self {
        TrackingAllocator {
            allocated: AtomicUsize::new(0),
            deallocated: AtomicUsize::new(0),
        }
    }
    
    fn current_usage(&self) -> usize {
        self.allocated.load(Ordering::SeqCst) - self.deallocated.load(Ordering::SeqCst)
    }
    
    fn total_allocated(&self) -> usize {
        self.allocated.load(Ordering::SeqCst)
    }
}

#[global_allocator]
static ALLOCATOR: TrackingAllocator = TrackingAllocator::new();

// Example usage
fn memory_usage_example() {
    let initial = ALLOCATOR.current_usage();
    
    {
        let mut data = Vec::with_capacity(1000);
        for i in 0..1000 {
            data.push(i);
        }
        println!("Current memory usage: {} bytes", ALLOCATOR.current_usage());
    } // data automatically dropped here
    
    println!("After cleanup: {} bytes", ALLOCATOR.current_usage());
    println!("Total allocated: {} bytes", ALLOCATOR.total_allocated());
}
```

## Summary

**Memory Management in C MQTT** is crucial for building reliable, long-running applications:

**Key Challenges:**
- Manual allocation/deallocation required in C
- Variable-length MQTT messages
- Persistent connections requiring careful cleanup
- Asynchronous callbacks complicating ownership

**C/C++ Best Practices:**
- Always pair `malloc`/`calloc` with `free`
- Use `strdup` carefully and free copied strings
- Implement proper cleanup functions
- Bounds-check all queues and buffers
- Consider memory pools for fixed-size allocations
- Use tools like Valgrind to detect leaks

**Rust Advantages:**
- Ownership system prevents most memory errors at compile time
- RAII ensures automatic cleanup via Drop trait
- No manual free() needed
- Thread-safe memory management with Arc/Mutex
- Zero-cost abstractions for safety

The fundamental difference: C requires discipline and vigilance, while Rust enforces safety through its type system, making memory-safe MQTT clients significantly easier to implement correctly.