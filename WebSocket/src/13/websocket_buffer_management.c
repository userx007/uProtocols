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