# Buffer Management in WebSocket Programming

Buffer management is a critical aspect of WebSocket implementations that directly impacts performance, memory efficiency, and the reliability of real-time communication. Proper buffer management strategies ensure smooth data flow, prevent memory leaks, and optimize resource utilization in both client and server applications.

## Overview

In WebSocket programming, buffers serve as temporary storage areas for incoming and outgoing data. Since WebSocket connections are persistent and bidirectional, efficient buffer management becomes crucial for handling:

- Variable-length messages
- High-frequency data streams
- Concurrent connections (server-side)
- Frame fragmentation and reassembly
- Flow control and backpressure

## Key Concepts

### 1. **Buffer Allocation Strategies**

**Static Allocation**: Pre-allocated buffers with fixed sizes. Fast and predictable but may waste memory or be insufficient for large messages.

**Dynamic Allocation**: Buffers that grow as needed. Flexible but introduces allocation overhead and potential fragmentation.

**Pooled Allocation**: Reusable buffer pools that balance performance and flexibility by recycling memory blocks.

### 2. **Circular (Ring) Buffers**

Circular buffers are particularly useful for streaming data in WebSocket applications. They provide:
- Constant-time operations for typical read/write scenarios
- Efficient memory reuse without shifting data
- Natural handling of continuous data streams

### 3. **Memory Management Strategies**

- **Zero-copy techniques**: Minimizing data copying between buffers
- **Reference counting**: Tracking buffer ownership in multi-threaded environments
- **Memory pooling**: Reducing allocation/deallocation overhead
- **Backpressure handling**: Managing buffer overflow conditions

## Code Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CIRCULAR BUFFER IMPLEMENTATION
// ============================================================================

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t head;      // Write position
    size_t tail;      // Read position
    size_t size;      // Current data size
} CircularBuffer;

// Initialize circular buffer
CircularBuffer* circular_buffer_create(size_t capacity) {
    CircularBuffer *cb = (CircularBuffer*)malloc(sizeof(CircularBuffer));
    if (!cb) return NULL;
    
    cb->data = (uint8_t*)malloc(capacity);
    if (!cb->data) {
        free(cb);
        return NULL;
    }
    
    cb->capacity = capacity;
    cb->head = 0;
    cb->tail = 0;
    cb->size = 0;
    
    return cb;
}

// Write data to circular buffer
size_t circular_buffer_write(CircularBuffer *cb, const uint8_t *data, size_t len) {
    if (!cb || !data) return 0;
    
    size_t available = cb->capacity - cb->size;
    size_t to_write = (len < available) ? len : available;
    
    for (size_t i = 0; i < to_write; i++) {
        cb->data[cb->head] = data[i];
        cb->head = (cb->head + 1) % cb->capacity;
        cb->size++;
    }
    
    return to_write;
}

// Read data from circular buffer
size_t circular_buffer_read(CircularBuffer *cb, uint8_t *data, size_t len) {
    if (!cb || !data) return 0;
    
    size_t to_read = (len < cb->size) ? len : cb->size;
    
    for (size_t i = 0; i < to_read; i++) {
        data[i] = cb->data[cb->tail];
        cb->tail = (cb->tail + 1) % cb->capacity;
        cb->size--;
    }
    
    return to_read;
}

// Peek data without removing
size_t circular_buffer_peek(CircularBuffer *cb, uint8_t *data, size_t len) {
    if (!cb || !data) return 0;
    
    size_t to_peek = (len < cb->size) ? len : cb->size;
    size_t pos = cb->tail;
    
    for (size_t i = 0; i < to_peek; i++) {
        data[i] = cb->data[pos];
        pos = (pos + 1) % cb->capacity;
    }
    
    return to_peek;
}

// Clean up
void circular_buffer_destroy(CircularBuffer *cb) {
    if (cb) {
        free(cb->data);
        free(cb);
    }
}

// ============================================================================
// DYNAMIC BUFFER WITH GROWTH STRATEGY
// ============================================================================

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t size;
    size_t growth_factor;  // Percentage: 150 means 1.5x growth
} DynamicBuffer;

DynamicBuffer* dynamic_buffer_create(size_t initial_capacity) {
    DynamicBuffer *db = (DynamicBuffer*)malloc(sizeof(DynamicBuffer));
    if (!db) return NULL;
    
    db->data = (uint8_t*)malloc(initial_capacity);
    if (!db->data) {
        free(db);
        return NULL;
    }
    
    db->capacity = initial_capacity;
    db->size = 0;
    db->growth_factor = 150;  // 1.5x growth
    
    return db;
}

// Ensure capacity (resize if needed)
bool dynamic_buffer_ensure_capacity(DynamicBuffer *db, size_t required) {
    if (db->capacity >= required) return true;
    
    size_t new_capacity = db->capacity;
    while (new_capacity < required) {
        new_capacity = (new_capacity * db->growth_factor) / 100;
    }
    
    uint8_t *new_data = (uint8_t*)realloc(db->data, new_capacity);
    if (!new_data) return false;
    
    db->data = new_data;
    db->capacity = new_capacity;
    
    return true;
}

// Append data to dynamic buffer
bool dynamic_buffer_append(DynamicBuffer *db, const uint8_t *data, size_t len) {
    if (!db || !data) return false;
    
    if (!dynamic_buffer_ensure_capacity(db, db->size + len)) {
        return false;
    }
    
    memcpy(db->data + db->size, data, len);
    db->size += len;
    
    return true;
}

// Consume data from front
void dynamic_buffer_consume(DynamicBuffer *db, size_t len) {
    if (!db || len == 0) return;
    
    if (len >= db->size) {
        db->size = 0;
        return;
    }
    
    memmove(db->data, db->data + len, db->size - len);
    db->size -= len;
}

void dynamic_buffer_destroy(DynamicBuffer *db) {
    if (db) {
        free(db->data);
        free(db);
    }
}

// ============================================================================
// BUFFER POOL FOR EFFICIENT REUSE
// ============================================================================

#define POOL_SIZE 16

typedef struct BufferNode {
    uint8_t *data;
    size_t capacity;
    struct BufferNode *next;
} BufferNode;

typedef struct {
    BufferNode *free_list;
    size_t buffer_size;
    size_t total_allocated;
} BufferPool;

BufferPool* buffer_pool_create(size_t buffer_size, size_t initial_count) {
    BufferPool *pool = (BufferPool*)malloc(sizeof(BufferPool));
    if (!pool) return NULL;
    
    pool->free_list = NULL;
    pool->buffer_size = buffer_size;
    pool->total_allocated = 0;
    
    // Pre-allocate buffers
    for (size_t i = 0; i < initial_count; i++) {
        BufferNode *node = (BufferNode*)malloc(sizeof(BufferNode));
        if (!node) continue;
        
        node->data = (uint8_t*)malloc(buffer_size);
        if (!node->data) {
            free(node);
            continue;
        }
        
        node->capacity = buffer_size;
        node->next = pool->free_list;
        pool->free_list = node;
        pool->total_allocated++;
    }
    
    return pool;
}

// Acquire buffer from pool
uint8_t* buffer_pool_acquire(BufferPool *pool) {
    if (!pool) return NULL;
    
    if (pool->free_list) {
        BufferNode *node = pool->free_list;
        pool->free_list = node->next;
        uint8_t *data = node->data;
        free(node);
        return data;
    }
    
    // Pool exhausted, allocate new buffer
    uint8_t *data = (uint8_t*)malloc(pool->buffer_size);
    if (data) pool->total_allocated++;
    return data;
}

// Return buffer to pool
void buffer_pool_release(BufferPool *pool, uint8_t *data) {
    if (!pool || !data) return;
    
    BufferNode *node = (BufferNode*)malloc(sizeof(BufferNode));
    if (!node) {
        free(data);
        return;
    }
    
    node->data = data;
    node->capacity = pool->buffer_size;
    node->next = pool->free_list;
    pool->free_list = node;
}

void buffer_pool_destroy(BufferPool *pool) {
    if (!pool) return;
    
    BufferNode *current = pool->free_list;
    while (current) {
        BufferNode *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    
    free(pool);
}

// ============================================================================
// DEMONSTRATION
// ============================================================================

int main() {
    printf("=== WebSocket Buffer Management Demo ===\n\n");
    
    // Test circular buffer
    printf("1. Circular Buffer Test:\n");
    CircularBuffer *cb = circular_buffer_create(64);
    
    const char *msg1 = "Hello, WebSocket!";
    size_t written = circular_buffer_write(cb, (uint8_t*)msg1, strlen(msg1));
    printf("   Written: %zu bytes\n", written);
    printf("   Buffer size: %zu/%zu\n", cb->size, cb->capacity);
    
    uint8_t read_buf[64];
    size_t read = circular_buffer_read(cb, read_buf, 5);
    read_buf[read] = '\0';
    printf("   Read: '%s' (%zu bytes)\n", read_buf, read);
    printf("   Remaining: %zu bytes\n\n", cb->size);
    
    circular_buffer_destroy(cb);
    
    // Test dynamic buffer
    printf("2. Dynamic Buffer Test:\n");
    DynamicBuffer *db = dynamic_buffer_create(16);
    printf("   Initial capacity: %zu\n", db->capacity);
    
    const char *msg2 = "This is a longer message that will trigger buffer growth!";
    dynamic_buffer_append(db, (uint8_t*)msg2, strlen(msg2));
    printf("   After append - Size: %zu, Capacity: %zu\n", db->size, db->capacity);
    
    dynamic_buffer_consume(db, 10);
    printf("   After consuming 10 bytes - Size: %zu\n\n", db->size);
    
    dynamic_buffer_destroy(db);
    
    // Test buffer pool
    printf("3. Buffer Pool Test:\n");
    BufferPool *pool = buffer_pool_create(1024, 4);
    printf("   Pool created with %zu buffers\n", pool->total_allocated);
    
    uint8_t *buf1 = buffer_pool_acquire(pool);
    uint8_t *buf2 = buffer_pool_acquire(pool);
    printf("   Acquired 2 buffers\n");
    
    buffer_pool_release(pool, buf1);
    buffer_pool_release(pool, buf2);
    printf("   Released 2 buffers back to pool\n");
    
    buffer_pool_destroy(pool);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}
```

Now let's look at a modern C++ implementation with RAII and smart pointers:

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <cstring>
#include <algorithm>

// ============================================================================
// CIRCULAR BUFFER (Thread-Safe)
// ============================================================================

template<typename T>
class CircularBuffer {
private:
    std::vector<T> buffer_;
    size_t head_;
    size_t tail_;
    size_t size_;
    size_t capacity_;
    mutable std::mutex mutex_;

public:
    explicit CircularBuffer(size_t capacity) 
        : buffer_(capacity), head_(0), tail_(0), size_(0), capacity_(capacity) {}

    bool write(const T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (count > available_write()) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            buffer_[head_] = data[i];
            head_ = (head_ + 1) % capacity_;
            ++size_;
        }

        return true;
    }

    size_t read(T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t to_read = std::min(count, size_);

        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[tail_];
            tail_ = (tail_ + 1) % capacity_;
            --size_;
        }

        return to_read;
    }

    size_t peek(T* data, size_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t to_peek = std::min(count, size_);
        size_t pos = tail_;

        for (size_t i = 0; i < to_peek; ++i) {
            data[i] = buffer_[pos];
            pos = (pos + 1) % capacity_;
        }

        return to_peek;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = size_ = 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    size_t available_write() const {
        return capacity_ - size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == capacity_;
    }
};

// ============================================================================
// DYNAMIC BUFFER WITH SMART GROWTH
// ============================================================================

class DynamicBuffer {
private:
    std::vector<uint8_t> data_;
    size_t read_pos_;
    double growth_factor_;

public:
    explicit DynamicBuffer(size_t initial_capacity = 4096, double growth_factor = 1.5)
        : data_(), read_pos_(0), growth_factor_(growth_factor) {
        data_.reserve(initial_capacity);
    }

    void append(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        data_.insert(data_.end(), bytes, bytes + size);
    }

    void append(const std::vector<uint8_t>& data) {
        data_.insert(data_.end(), data.begin(), data.end());
    }

    template<typename T>
    void append(const T& value) {
        append(&value, sizeof(T));
    }

    size_t consume(size_t count) {
        size_t available = data_.size() - read_pos_;
        size_t to_consume = std::min(count, available);
        
        read_pos_ += to_consume;

        // Compact buffer if we've consumed more than half
        if (read_pos_ > data_.size() / 2 && read_pos_ > 0) {
            data_.erase(data_.begin(), data_.begin() + read_pos_);
            read_pos_ = 0;
        }

        return to_consume;
    }

    const uint8_t* data() const {
        return data_.data() + read_pos_;
    }

    uint8_t* data() {
        return data_.data() + read_pos_;
    }

    size_t size() const {
        return data_.size() - read_pos_;
    }

    void clear() {
        data_.clear();
        read_pos_ = 0;
    }

    void reserve(size_t capacity) {
        data_.reserve(capacity);
    }

    std::vector<uint8_t> extract(size_t count) {
        size_t available = std::min(count, size());
        std::vector<uint8_t> result(data_.begin() + read_pos_, 
                                    data_.begin() + read_pos_ + available);
        consume(available);
        return result;
    }
};

// ============================================================================
// BUFFER POOL WITH SHARED POINTERS
// ============================================================================

class BufferPool {
private:
    struct Buffer {
        std::vector<uint8_t> data;
        explicit Buffer(size_t size) : data(size) {}
    };

    std::queue<std::shared_ptr<Buffer>> pool_;
    size_t buffer_size_;
    size_t max_pool_size_;
    mutable std::mutex mutex_;

public:
    BufferPool(size_t buffer_size, size_t initial_count, size_t max_pool_size = 100)
        : buffer_size_(buffer_size), max_pool_size_(max_pool_size) {
        
        for (size_t i = 0; i < initial_count; ++i) {
            pool_.push(std::make_shared<Buffer>(buffer_size_));
        }
    }

    std::shared_ptr<std::vector<uint8_t>> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!pool_.empty()) {
            auto buffer = pool_.front();
            pool_.pop();
            return std::shared_ptr<std::vector<uint8_t>>(
                buffer, &buffer->data
            );
        }

        // Create new buffer if pool is empty
        auto buffer = std::make_shared<Buffer>(buffer_size_);
        return std::shared_ptr<std::vector<uint8_t>>(
            buffer, &buffer->data
        );
    }

    void release(std::shared_ptr<std::vector<uint8_t>> buffer) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pool_.size() < max_pool_size_) {
            // Clear buffer data before returning to pool
            buffer->clear();
            buffer->reserve(buffer_size_);
            
            // Re-wrap in Buffer struct
            auto buf = std::make_shared<Buffer>(buffer_size_);
            buf->data = std::move(*buffer);
            pool_.push(buf);
        }
        // Otherwise, let it be destroyed
    }

    size_t pool_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
};

// ============================================================================
// WEBSOCKET FRAME BUFFER (Practical Example)
// ============================================================================

class WebSocketFrameBuffer {
private:
    DynamicBuffer recv_buffer_;
    CircularBuffer<uint8_t> send_buffer_;
    
    static constexpr size_t MAX_FRAME_SIZE = 65536;
    static constexpr size_t SEND_BUFFER_SIZE = 131072;

public:
    WebSocketFrameBuffer() 
        : recv_buffer_(4096), send_buffer_(SEND_BUFFER_SIZE) {}

    // Add received data
    void add_received_data(const uint8_t* data, size_t size) {
        recv_buffer_.append(data, size);
    }

    // Try to extract a complete frame
    bool try_extract_frame(std::vector<uint8_t>& frame) {
        if (recv_buffer_.size() < 2) {
            return false; // Not enough data for header
        }

        const uint8_t* data = recv_buffer_.data();
        
        // Parse WebSocket frame header (simplified)
        bool fin = (data[0] & 0x80) != 0;
        uint8_t opcode = data[0] & 0x0F;
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;

        size_t header_size = 2;
        size_t offset = 2;

        // Extended payload length
        if (payload_len == 126) {
            if (recv_buffer_.size() < 4) return false;
            payload_len = (data[2] << 8) | data[3];
            header_size = 4;
            offset = 4;
        } else if (payload_len == 127) {
            if (recv_buffer_.size() < 10) return false;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            header_size = 10;
            offset = 10;
        }

        // Masking key
        uint8_t mask_key[4] = {0};
        if (masked) {
            if (recv_buffer_.size() < offset + 4) return false;
            std::memcpy(mask_key, data + offset, 4);
            header_size += 4;
            offset += 4;
        }

        // Check if we have complete frame
        size_t total_size = header_size + payload_len;
        if (recv_buffer_.size() < total_size) {
            return false;
        }

        // Extract payload
        frame.resize(payload_len);
        std::memcpy(frame.data(), data + offset, payload_len);

        // Unmask if needed
        if (masked) {
            for (size_t i = 0; i < payload_len; ++i) {
                frame[i] ^= mask_key[i % 4];
            }
        }

        // Consume processed data
        recv_buffer_.consume(total_size);

        return true;
    }

    // Queue data for sending
    bool queue_send_data(const uint8_t* data, size_t size) {
        return send_buffer_.write(data, size);
    }

    // Get data to send
    size_t get_send_data(uint8_t* buffer, size_t max_size) {
        return send_buffer_.read(buffer, max_size);
    }

    size_t pending_send_size() const {
        return send_buffer_.size();
    }
};

// ============================================================================
// DEMONSTRATION
// ============================================================================

int main() {
    std::cout << "=== WebSocket Buffer Management (C++) ===\n\n";

    // Test circular buffer
    std::cout << "1. Circular Buffer Test:\n";
    CircularBuffer<uint8_t> cb(64);
    
    const char* msg = "Hello, WebSocket!";
    cb.write(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg));
    std::cout << "   Buffer size: " << cb.size() << "/64\n";
    
    uint8_t read_buf[10];
    size_t read = cb.read(read_buf, 5);
    std::cout << "   Read: " << read << " bytes\n";
    std::cout << "   Remaining: " << cb.size() << " bytes\n\n";

    // Test dynamic buffer
    std::cout << "2. Dynamic Buffer Test:\n";
    DynamicBuffer db(16);
    
    const char* msg2 = "This message will grow the buffer!";
    db.append(msg2, std::strlen(msg2));
    std::cout << "   Buffer size: " << db.size() << " bytes\n";
    
    db.consume(10);
    std::cout << "   After consuming 10 bytes: " << db.size() << " bytes\n\n";

    // Test buffer pool
    std::cout << "3. Buffer Pool Test:\n";
    BufferPool pool(1024, 4);
    std::cout << "   Initial pool size: " << pool.pool_size() << "\n";
    
    auto buf1 = pool.acquire();
    auto buf2 = pool.acquire();
    std::cout << "   After acquiring 2 buffers: " << pool.pool_size() << "\n";
    
    pool.release(buf1);
    pool.release(buf2);
    std::cout << "   After releasing 2 buffers: " << pool.pool_size() << "\n\n";

    // Test WebSocket frame buffer
    std::cout << "4. WebSocket Frame Buffer Test:\n";
    WebSocketFrameBuffer ws_buffer;
    
    // Simulate received data (simple unmasked text frame)
    uint8_t frame_data[] = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
    ws_buffer.add_received_data(frame_data, sizeof(frame_data));
    
    std::vector<uint8_t> extracted_frame;
    if (ws_buffer.try_extract_frame(extracted_frame)) {
        std::cout << "   Extracted frame: ";
        std::cout.write(reinterpret_cast<char*>(extracted_frame.data()), 
                       extracted_frame.size());
        std::cout << "\n";
    }

    std::cout << "\n=== Demo Complete ===\n";
    return 0;
}
```
Now let's implement the same concepts in Rust with its ownership model and safety guarantees:

```rust
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

// ============================================================================
// CIRCULAR BUFFER
// ============================================================================

pub struct CircularBuffer {
    buffer: Vec<u8>,
    capacity: usize,
    head: usize,
    tail: usize,
    size: usize,
}

impl CircularBuffer {
    pub fn new(capacity: usize) -> Self {
        Self {
            buffer: vec![0; capacity],
            capacity,
            head: 0,
            tail: 0,
            size: 0,
        }
    }

    pub fn write(&mut self, data: &[u8]) -> usize {
        let available = self.capacity - self.size;
        let to_write = data.len().min(available);

        for i in 0..to_write {
            self.buffer[self.head] = data[i];
            self.head = (self.head + 1) % self.capacity;
            self.size += 1;
        }

        to_write
    }

    pub fn read(&mut self, data: &mut [u8]) -> usize {
        let to_read = data.len().min(self.size);

        for i in 0..to_read {
            data[i] = self.buffer[self.tail];
            self.tail = (self.tail + 1) % self.capacity;
            self.size -= 1;
        }

        to_read
    }

    pub fn peek(&self, data: &mut [u8]) -> usize {
        let to_peek = data.len().min(self.size);
        let mut pos = self.tail;

        for i in 0..to_peek {
            data[i] = self.buffer[pos];
            pos = (pos + 1) % self.capacity;
        }

        to_peek
    }

    pub fn size(&self) -> usize {
        self.size
    }

    pub fn available(&self) -> usize {
        self.capacity - self.size
    }

    pub fn is_empty(&self) -> bool {
        self.size == 0
    }

    pub fn is_full(&self) -> bool {
        self.size == self.capacity
    }

    pub fn clear(&mut self) {
        self.head = 0;
        self.tail = 0;
        self.size = 0;
    }
}

// ============================================================================
// DYNAMIC BUFFER
// ============================================================================

pub struct DynamicBuffer {
    data: Vec<u8>,
    read_pos: usize,
    growth_factor: f64,
}

impl DynamicBuffer {
    pub fn new(initial_capacity: usize) -> Self {
        Self {
            data: Vec::with_capacity(initial_capacity),
            read_pos: 0,
            growth_factor: 1.5,
        }
    }

    pub fn with_growth_factor(initial_capacity: usize, growth_factor: f64) -> Self {
        Self {
            data: Vec::with_capacity(initial_capacity),
            read_pos: 0,
            growth_factor,
        }
    }

    pub fn append(&mut self, data: &[u8]) {
        self.data.extend_from_slice(data);
    }

    pub fn consume(&mut self, count: usize) -> usize {
        let available = self.data.len() - self.read_pos;
        let to_consume = count.min(available);

        self.read_pos += to_consume;

        // Compact buffer if we've consumed more than half
        if self.read_pos > self.data.len() / 2 && self.read_pos > 0 {
            self.data.drain(0..self.read_pos);
            self.read_pos = 0;
        }

        to_consume
    }

    pub fn data(&self) -> &[u8] {
        &self.data[self.read_pos..]
    }

    pub fn size(&self) -> usize {
        self.data.len() - self.read_pos
    }

    pub fn clear(&mut self) {
        self.data.clear();
        self.read_pos = 0;
    }

    pub fn extract(&mut self, count: usize) -> Vec<u8> {
        let available = count.min(self.size());
        let result = self.data[self.read_pos..self.read_pos + available].to_vec();
        self.consume(available);
        result
    }

    pub fn peek(&self, count: usize) -> &[u8] {
        let available = count.min(self.size());
        &self.data[self.read_pos..self.read_pos + available]
    }
}

// ============================================================================
// BUFFER POOL
// ============================================================================

pub struct BufferPool {
    pool: Arc<Mutex<VecDeque<Vec<u8>>>>,
    buffer_size: usize,
    max_pool_size: usize,
}

impl BufferPool {
    pub fn new(buffer_size: usize, initial_count: usize, max_pool_size: usize) -> Self {
        let mut pool = VecDeque::new();

        for _ in 0..initial_count {
            pool.push_back(vec![0; buffer_size]);
        }

        Self {
            pool: Arc::new(Mutex::new(pool)),
            buffer_size,
            max_pool_size,
        }
    }

    pub fn acquire(&self) -> PooledBuffer {
        let mut pool = self.pool.lock().unwrap();
        
        let buffer = pool.pop_front().unwrap_or_else(|| {
            vec![0; self.buffer_size]
        });

        PooledBuffer {
            buffer: Some(buffer),
            pool: Arc::clone(&self.pool),
            max_pool_size: self.max_pool_size,
        }
    }

    pub fn pool_size(&self) -> usize {
        self.pool.lock().unwrap().len()
    }
}

pub struct PooledBuffer {
    buffer: Option<Vec<u8>>,
    pool: Arc<Mutex<VecDeque<Vec<u8>>>>,
    max_pool_size: usize,
}

impl PooledBuffer {
    pub fn as_slice(&self) -> &[u8] {
        self.buffer.as_ref().unwrap()
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.buffer.as_mut().unwrap()
    }
}

impl Drop for PooledBuffer {
    fn drop(&mut self) {
        if let Some(mut buffer) = self.buffer.take() {
            let mut pool = self.pool.lock().unwrap();
            
            if pool.len() < self.max_pool_size {
                buffer.clear();
                buffer.resize(buffer.capacity(), 0);
                pool.push_back(buffer);
            }
            // Otherwise, buffer is dropped
        }
    }
}

impl std::ops::Deref for PooledBuffer {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.as_slice()
    }
}

impl std::ops::DerefMut for PooledBuffer {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut_slice()
    }
}

// ============================================================================
// WEBSOCKET FRAME BUFFER
// ============================================================================

pub struct WebSocketFrameBuffer {
    recv_buffer: DynamicBuffer,
    send_buffer: CircularBuffer,
}

impl WebSocketFrameBuffer {
    const MAX_FRAME_SIZE: usize = 65536;
    const SEND_BUFFER_SIZE: usize = 131072;

    pub fn new() -> Self {
        Self {
            recv_buffer: DynamicBuffer::new(4096),
            send_buffer: CircularBuffer::new(Self::SEND_BUFFER_SIZE),
        }
    }

    pub fn add_received_data(&mut self, data: &[u8]) {
        self.recv_buffer.append(data);
    }

    pub fn try_extract_frame(&mut self) -> Option<Vec<u8>> {
        let data = self.recv_buffer.data();

        if data.len() < 2 {
            return None; // Not enough data for header
        }

        let fin = (data[0] & 0x80) != 0;
        let opcode = data[0] & 0x0F;
        let masked = (data[1] & 0x80) != 0;
        let mut payload_len = (data[1] & 0x7F) as u64;

        let mut header_size = 2;
        let mut offset = 2;

        // Extended payload length
        if payload_len == 126 {
            if data.len() < 4 {
                return None;
            }
            payload_len = u16::from_be_bytes([data[2], data[3]]) as u64;
            header_size = 4;
            offset = 4;
        } else if payload_len == 127 {
            if data.len() < 10 {
                return None;
            }
            payload_len = u64::from_be_bytes([
                data[2], data[3], data[4], data[5],
                data[6], data[7], data[8], data[9],
            ]);
            header_size = 10;
            offset = 10;
        }

        // Masking key
        let mask_key = if masked {
            if data.len() < offset + 4 {
                return None;
            }
            let key = [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]];
            header_size += 4;
            offset += 4;
            Some(key)
        } else {
            None
        };

        // Check if we have complete frame
        let total_size = header_size + payload_len as usize;
        if data.len() < total_size {
            return None;
        }

        // Extract payload
        let mut payload = data[offset..offset + payload_len as usize].to_vec();

        // Unmask if needed
        if let Some(key) = mask_key {
            for (i, byte) in payload.iter_mut().enumerate() {
                *byte ^= key[i % 4];
            }
        }

        // Consume processed data
        self.recv_buffer.consume(total_size);

        Some(payload)
    }

    pub fn queue_send_data(&mut self, data: &[u8]) -> bool {
        self.send_buffer.write(data) == data.len()
    }

    pub fn get_send_data(&mut self, buffer: &mut [u8]) -> usize {
        self.send_buffer.read(buffer)
    }

    pub fn pending_send_size(&self) -> usize {
        self.send_buffer.size()
    }
}

impl Default for WebSocketFrameBuffer {
    fn default() -> Self {
        Self::new()
    }
}

// ============================================================================
// DEMONSTRATION
// ============================================================================

fn main() {
    println!("=== WebSocket Buffer Management (Rust) ===\n");

    // Test circular buffer
    println!("1. Circular Buffer Test:");
    let mut cb = CircularBuffer::new(64);
    
    let msg = b"Hello, WebSocket!";
    let written = cb.write(msg);
    println!("   Written: {} bytes", written);
    println!("   Buffer size: {}/{}", cb.size(), 64);
    
    let mut read_buf = [0u8; 10];
    let read = cb.read(&mut read_buf[..5]);
    println!("   Read: {} bytes", read);
    println!("   Remaining: {} bytes\n", cb.size());

    // Test dynamic buffer
    println!("2. Dynamic Buffer Test:");
    let mut db = DynamicBuffer::new(16);
    
    let msg2 = b"This message will grow the buffer!";
    db.append(msg2);
    println!("   Buffer size: {} bytes", db.size());
    
    db.consume(10);
    println!("   After consuming 10 bytes: {} bytes\n", db.size());

    // Test buffer pool
    println!("3. Buffer Pool Test:");
    let pool = BufferPool::new(1024, 4, 100);
    println!("   Initial pool size: {}", pool.pool_size());
    
    {
        let buf1 = pool.acquire();
        let buf2 = pool.acquire();
        println!("   After acquiring 2 buffers: {}", pool.pool_size());
        // Buffers are automatically returned when dropped
    }
    
    println!("   After releasing 2 buffers: {}\n", pool.pool_size());

    // Test WebSocket frame buffer
    println!("4. WebSocket Frame Buffer Test:");
    let mut ws_buffer = WebSocketFrameBuffer::new();
    
    // Simulate received data (simple unmasked text frame)
    let frame_data = vec![0x81, 0x05, b'H', b'e', b'l', b'l', b'o'];
    ws_buffer.add_received_data(&frame_data);
    
    if let Some(extracted_frame) = ws_buffer.try_extract_frame() {
        println!("   Extracted frame: {}", 
                 String::from_utf8_lossy(&extracted_frame));
    }

    println!("\n=== Demo Complete ===");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_circular_buffer_wrap_around() {
        let mut cb = CircularBuffer::new(8);
        
        cb.write(b"12345678");
        assert_eq!(cb.size(), 8);
        assert!(cb.is_full());
        
        let mut buf = [0u8; 4];
        cb.read(&mut buf);
        assert_eq!(&buf, b"1234");
        assert_eq!(cb.size(), 4);
        
        cb.write(b"ABCD");
        assert_eq!(cb.size(), 8);
        
        let mut result = [0u8; 8];
        cb.read(&mut result);
        assert_eq!(&result, b"5678ABCD");
    }

    #[test]
    fn test_dynamic_buffer_growth() {
        let mut db = DynamicBuffer::new(4);
        
        db.append(b"Hello, World!");
        assert!(db.size() >= 13);
        
        let consumed = db.consume(7);
        assert_eq!(consumed, 7);
        assert_eq!(db.data(), b"World!");
    }

    #[test]
    fn test_buffer_pool() {
        let pool = BufferPool::new(1024, 2, 10);
        assert_eq!(pool.pool_size(), 2);
        
        let _buf1 = pool.acquire();
        assert_eq!(pool.pool_size(), 1);
        
        drop(_buf1);
        assert_eq!(pool.pool_size(), 2);
    }
}
```

## Summary

Buffer management is fundamental to efficient WebSocket implementations. Here are the key takeaways:

### Core Concepts

1. **Circular Buffers** excel at continuous streaming scenarios, providing constant-time operations and eliminating the need for data shifting. They're ideal for send/receive queues in WebSocket connections.

2. **Dynamic Buffers** offer flexibility for variable-length messages, automatically growing to accommodate data while implementing smart compaction strategies to prevent unbounded growth.

3. **Buffer Pools** reduce allocation overhead by reusing pre-allocated buffers, which is crucial for high-throughput WebSocket servers handling thousands of concurrent connections.

### Language-Specific Insights

**C Implementation**: Provides maximum control and performance but requires careful manual memory management. The examples show explicit malloc/free patterns and manual bookkeeping of buffer state.

**C++ Implementation**: Leverages RAII, smart pointers, and STL containers for safer memory management. Thread safety is achieved through mutex locks, and the buffer pool uses shared_ptr for automatic lifetime management.

**Rust Implementation**: Offers memory safety guarantees through ownership and borrowing. The buffer pool demonstrates automatic resource cleanup via the Drop trait, eliminating manual memory management while maintaining zero-cost abstractions.

### Best Practices

- **Choose the right buffer type**: Circular for streams, dynamic for variable messages, pooled for high-frequency allocations
- **Implement backpressure handling**: Prevent unbounded buffer growth by signaling when buffers are full
- **Use zero-copy techniques**: Minimize data copying through pointer manipulation and reference sharing
- **Consider thread safety**: Protect shared buffers with appropriate synchronization primitives
- **Monitor memory usage**: Implement metrics to track buffer utilization and detect leaks
- **Compact strategically**: Balance performance (avoiding frequent compaction) with memory efficiency

Effective buffer management directly impacts WebSocket application performance, scalability, and resource utilization. The choice of buffer strategy should align with your specific use case, whether you're building a chat application, real-time gaming server, or financial data streaming platform.