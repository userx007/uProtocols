#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Memory pool for WebSocket frame buffers
#define POOL_BLOCK_SIZE 4096
#define SMALL_FRAME_SIZE 128
#define MEDIUM_FRAME_SIZE 1024
#define LARGE_FRAME_SIZE 4096

typedef struct MemBlock {
    void *data;
    size_t size;
    bool in_use;
    struct MemBlock *next;
} MemBlock;

typedef struct MemPool {
    MemBlock *small_blocks;   // For control frames
    MemBlock *medium_blocks;  // For typical messages
    MemBlock *large_blocks;   // For large payloads
    size_t small_count;
    size_t medium_count;
    size_t large_count;
    size_t allocations;
    size_t deallocations;
} MemPool;

// Initialize memory pool
MemPool* mempool_create(size_t small_blocks, size_t medium_blocks, size_t large_blocks) {
    MemPool *pool = malloc(sizeof(MemPool));
    if (!pool) return NULL;
    
    pool->small_blocks = NULL;
    pool->medium_blocks = NULL;
    pool->large_blocks = NULL;
    pool->small_count = 0;
    pool->medium_count = 0;
    pool->large_count = 0;
    pool->allocations = 0;
    pool->deallocations = 0;
    
    // Pre-allocate small blocks
    for (size_t i = 0; i < small_blocks; i++) {
        MemBlock *block = malloc(sizeof(MemBlock));
        block->data = malloc(SMALL_FRAME_SIZE);
        block->size = SMALL_FRAME_SIZE;
        block->in_use = false;
        block->next = pool->small_blocks;
        pool->small_blocks = block;
        pool->small_count++;
    }
    
    // Pre-allocate medium blocks
    for (size_t i = 0; i < medium_blocks; i++) {
        MemBlock *block = malloc(sizeof(MemBlock));
        block->data = malloc(MEDIUM_FRAME_SIZE);
        block->size = MEDIUM_FRAME_SIZE;
        block->in_use = false;
        block->next = pool->medium_blocks;
        pool->medium_blocks = block;
        pool->medium_count++;
    }
    
    // Pre-allocate large blocks
    for (size_t i = 0; i < large_blocks; i++) {
        MemBlock *block = malloc(sizeof(MemBlock));
        block->data = malloc(LARGE_FRAME_SIZE);
        block->size = LARGE_FRAME_SIZE;
        block->in_use = false;
        block->next = pool->large_blocks;
        pool->large_blocks = block;
        pool->large_count++;
    }
    
    return pool;
}

// Allocate memory from pool
void* mempool_alloc(MemPool *pool, size_t size) {
    MemBlock *block = NULL;
    MemBlock **list = NULL;
    
    // Select appropriate size class
    if (size <= SMALL_FRAME_SIZE) {
        list = &pool->small_blocks;
    } else if (size <= MEDIUM_FRAME_SIZE) {
        list = &pool->medium_blocks;
    } else if (size <= LARGE_FRAME_SIZE) {
        list = &pool->large_blocks;
    } else {
        // Size too large, fall back to malloc
        pool->allocations++;
        return malloc(size);
    }
    
    // Find free block
    MemBlock *current = *list;
    while (current) {
        if (!current->in_use) {
            current->in_use = true;
            pool->allocations++;
            return current->data;
        }
        current = current->next;
    }
    
    // No free blocks, allocate new one
    block = malloc(sizeof(MemBlock));
    block->data = malloc(size <= SMALL_FRAME_SIZE ? SMALL_FRAME_SIZE :
                        size <= MEDIUM_FRAME_SIZE ? MEDIUM_FRAME_SIZE : LARGE_FRAME_SIZE);
    block->size = size <= SMALL_FRAME_SIZE ? SMALL_FRAME_SIZE :
                  size <= MEDIUM_FRAME_SIZE ? MEDIUM_FRAME_SIZE : LARGE_FRAME_SIZE;
    block->in_use = true;
    block->next = *list;
    *list = block;
    pool->allocations++;
    
    return block->data;
}

// Free memory back to pool
void mempool_free(MemPool *pool, void *ptr) {
    if (!ptr) return;
    
    // Search all lists for the block
    MemBlock *lists[] = {pool->small_blocks, pool->medium_blocks, pool->large_blocks};
    
    for (int i = 0; i < 3; i++) {
        MemBlock *current = lists[i];
        while (current) {
            if (current->data == ptr) {
                current->in_use = false;
                pool->deallocations++;
                return;
            }
            current = current->next;
        }
    }
    
    // Not found in pool, must be direct malloc
    free(ptr);
    pool->deallocations++;
}

// Destroy memory pool
void mempool_destroy(MemPool *pool) {
    MemBlock *lists[] = {pool->small_blocks, pool->medium_blocks, pool->large_blocks};
    
    for (int i = 0; i < 3; i++) {
        MemBlock *current = lists[i];
        while (current) {
            MemBlock *next = current->next;
            free(current->data);
            free(current);
            current = next;
        }
    }
    
    printf("Pool stats - Allocations: %zu, Deallocations: %zu\n", 
           pool->allocations, pool->deallocations);
    free(pool);
}

// WebSocket frame structure
typedef struct WSFrame {
    uint8_t *payload;
    size_t payload_len;
    uint8_t opcode;
} WSFrame;

// Create WebSocket frame using memory pool
WSFrame* ws_frame_create(MemPool *pool, size_t payload_size, uint8_t opcode) {
    WSFrame *frame = malloc(sizeof(WSFrame));
    frame->payload = mempool_alloc(pool, payload_size);
    frame->payload_len = payload_size;
    frame->opcode = opcode;
    return frame;
}

// Destroy WebSocket frame
void ws_frame_destroy(MemPool *pool, WSFrame *frame) {
    mempool_free(pool, frame->payload);
    free(frame);
}

// Example usage
int main() {
    // Create pool with 100 small, 50 medium, 10 large blocks
    MemPool *pool = mempool_create(100, 50, 10);
    
    printf("Memory pool created\n");
    
    // Simulate WebSocket frame processing
    WSFrame *frames[20];
    
    // Allocate various frame sizes
    for (int i = 0; i < 20; i++) {
        size_t size = (i % 3 == 0) ? 64 :    // Small control frame
                     (i % 3 == 1) ? 512 :     // Medium message
                     2048;                     // Large message
        frames[i] = ws_frame_create(pool, size, i % 3);
        memcpy(frames[i]->payload, "Hello WebSocket", 15);
        printf("Frame %d allocated (%zu bytes)\n", i, size);
    }
    
    // Free half the frames
    for (int i = 0; i < 10; i++) {
        ws_frame_destroy(pool, frames[i]);
        printf("Frame %d freed\n", i);
    }
    
    // Allocate more frames (should reuse freed blocks)
    for (int i = 0; i < 10; i++) {
        frames[i] = ws_frame_create(pool, 128, 1);
        printf("Frame %d reallocated (reused pool memory)\n", i);
    }
    
    // Cleanup
    for (int i = 0; i < 20; i++) {
        ws_frame_destroy(pool, frames[i]);
    }
    
    mempool_destroy(pool);
    
    return 0;
}