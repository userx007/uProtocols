#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// WebSocket message structure
typedef struct {
    char* data;
    size_t length;
    int connection_id;
} ws_message_t;

// Lock-free queue node
typedef struct queue_node {
    ws_message_t message;
    _Atomic(struct queue_node*) next;
} queue_node_t;

// Lock-free MPSC (Multiple Producer Single Consumer) queue
typedef struct {
    _Atomic(queue_node_t*) head;  // Consumer reads from head
    _Atomic(queue_node_t*) tail;  // Producers write to tail
    _Atomic size_t count;          // Approximate count for monitoring
} lockfree_queue_t;

// Initialize the queue
lockfree_queue_t* queue_create() {
    lockfree_queue_t* q = malloc(sizeof(lockfree_queue_t));
    
    // Create dummy node to simplify implementation
    queue_node_t* dummy = malloc(sizeof(queue_node_t));
    atomic_store(&dummy->next, NULL);
    
    atomic_store(&q->head, dummy);
    atomic_store(&q->tail, dummy);
    atomic_store(&q->count, 0);
    
    return q;
}

// Enqueue a message (thread-safe for multiple producers)
bool queue_enqueue(lockfree_queue_t* q, const ws_message_t* msg) {
    // Allocate new node
    queue_node_t* node = malloc(sizeof(queue_node_t));
    if (!node) return false;
    
    // Copy message data
    node->message.data = malloc(msg->length);
    if (!node->message.data) {
        free(node);
        return false;
    }
    memcpy(node->message.data, msg->data, msg->length);
    node->message.length = msg->length;
    node->message.connection_id = msg->connection_id;
    
    atomic_store(&node->next, NULL);
    
    // Atomically append to tail using CAS loop
    queue_node_t* prev_tail;
    queue_node_t* expected_null;
    
    while (true) {
        prev_tail = atomic_load(&q->tail);
        expected_null = NULL;
        
        // Try to link our node after current tail
        if (atomic_compare_exchange_weak(&prev_tail->next, &expected_null, node)) {
            // Successfully linked, now try to update tail pointer
            // This may fail if another thread already updated it, which is fine
            atomic_compare_exchange_strong(&q->tail, &prev_tail, node);
            atomic_fetch_add(&q->count, 1);
            return true;
        }
        
        // Another thread modified tail, help move tail pointer forward
        queue_node_t* next = atomic_load(&prev_tail->next);
        if (next) {
            atomic_compare_exchange_strong(&q->tail, &prev_tail, next);
        }
    }
}

// Dequeue a message (single consumer only)
bool queue_dequeue(lockfree_queue_t* q, ws_message_t* msg) {
    queue_node_t* head = atomic_load(&q->head);
    queue_node_t* next = atomic_load(&head->next);
    
    if (next == NULL) {
        return false;  // Queue is empty
    }
    
    // Copy message out
    *msg = next->message;
    
    // Move head pointer forward
    atomic_store(&q->head, next);
    atomic_fetch_sub(&q->count, 1);
    
    // Free old head (dummy node)
    free(head);
    
    return true;
}

// Get approximate queue size
size_t queue_size(lockfree_queue_t* q) {
    return atomic_load(&q->count);
}

// Clean up queue
void queue_destroy(lockfree_queue_t* q) {
    ws_message_t msg;
    while (queue_dequeue(q, &msg)) {
        free(msg.data);
    }
    
    queue_node_t* head = atomic_load(&q->head);
    free(head);
    free(q);
}

// ============ LOCK-FREE RING BUFFER (SPSC) ============
// Single Producer Single Consumer - more efficient for point-to-point

typedef struct {
    ws_message_t* buffer;
    size_t capacity;
    _Atomic size_t write_pos;
    _Atomic size_t read_pos;
} lockfree_ringbuffer_t;

lockfree_ringbuffer_t* ringbuffer_create(size_t capacity) {
    lockfree_ringbuffer_t* rb = malloc(sizeof(lockfree_ringbuffer_t));
    rb->buffer = calloc(capacity, sizeof(ws_message_t));
    rb->capacity = capacity;
    atomic_store(&rb->write_pos, 0);
    atomic_store(&rb->read_pos, 0);
    return rb;
}

bool ringbuffer_push(lockfree_ringbuffer_t* rb, const ws_message_t* msg) {
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_relaxed);
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_acquire);
    size_t next_w = (w + 1) % rb->capacity;
    
    if (next_w == r) {
        return false;  // Buffer full
    }
    
    // Copy message
    rb->buffer[w].data = malloc(msg->length);
    memcpy(rb->buffer[w].data, msg->data, msg->length);
    rb->buffer[w].length = msg->length;
    rb->buffer[w].connection_id = msg->connection_id;
    
    // Release write to make it visible to consumer
    atomic_store_explicit(&rb->write_pos, next_w, memory_order_release);
    return true;
}

bool ringbuffer_pop(lockfree_ringbuffer_t* rb, ws_message_t* msg) {
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_relaxed);
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_acquire);
    
    if (r == w) {
        return false;  // Buffer empty
    }
    
    *msg = rb->buffer[r];
    
    // Release read position
    size_t next_r = (r + 1) % rb->capacity;
    atomic_store_explicit(&rb->read_pos, next_r, memory_order_release);
    return true;
}

void ringbuffer_destroy(lockfree_ringbuffer_t* rb) {
    ws_message_t msg;
    while (ringbuffer_pop(rb, &msg)) {
        free(msg.data);
    }
    free(rb->buffer);
    free(rb);
}

// ============ USAGE EXAMPLE ============

#include <pthread.h>
#include <stdio.h>

typedef struct {
    lockfree_queue_t* queue;
    int thread_id;
} producer_args_t;

void* producer_thread(void* arg) {
    producer_args_t* args = (producer_args_t*)arg;
    
    for (int i = 0; i < 1000; i++) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
                "Message %d from thread %d", i, args->thread_id);
        
        ws_message_t msg = {
            .data = buffer,
            .length = strlen(buffer) + 1,
            .connection_id = args->thread_id * 1000 + i
        };
        
        queue_enqueue(args->queue, &msg);
    }
    
    return NULL;
}

int main() {
    lockfree_queue_t* queue = queue_create();
    
    // Create multiple producer threads
    pthread_t threads[4];
    producer_args_t args[4];
    
    for (int i = 0; i < 4; i++) {
        args[i].queue = queue;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, producer_thread, &args[i]);
    }
    
    // Consumer processes messages
    ws_message_t msg;
    int processed = 0;
    int expected = 4000;  // 4 threads * 1000 messages
    
    while (processed < expected) {
        if (queue_dequeue(queue, &msg)) {
            printf("Processed: conn=%d, data=%s\n", 
                   msg.connection_id, msg.data);
            free(msg.data);
            processed++;
        }
    }
    
    // Wait for producers
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    queue_destroy(queue);
    printf("Total processed: %d\n", processed);
    
    return 0;
}