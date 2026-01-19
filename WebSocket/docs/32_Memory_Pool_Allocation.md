# Memory Pool Allocation for WebSocket Programming

## Overview

Memory pool allocation is a crucial optimization technique in high-performance WebSocket servers and clients. Instead of repeatedly allocating and deallocating memory for frames, messages, and connection buffers through the system allocator, memory pools pre-allocate chunks of memory and reuse them, significantly reducing allocation overhead and memory fragmentation.

## Why Memory Pools Matter for WebSockets

WebSocket applications face unique memory management challenges:

**High Frequency Operations**: WebSocket servers handle thousands of frames per second, each requiring temporary buffers for parsing, masking/unmasking, and message assembly.

**Predictable Sizes**: WebSocket frames have well-defined structures with predictable size patterns (small control frames, variable-sized data frames).

**Short Lifetimes**: Most allocations (frame buffers, temporary parsing structures) have very short lifetimes, making them perfect candidates for pooling.

**Latency Sensitivity**: Every microsecond counts in real-time applications. System allocator calls can introduce unpredictable latency spikes.

## Core Concepts

Memory pools work by maintaining pre-allocated blocks of memory organized by size. When code requests memory, the pool returns a free block from the appropriate size class. When memory is released, it returns to the pool rather than being freed to the system.

## C Implementation

```c
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
```

## C++ Implementation with Template-Based Object Pool

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <array>
#include <cstring>

// Generic object pool template
template<typename T>
class ObjectPool {
private:
    struct PooledObject {
        alignas(T) unsigned char storage[sizeof(T)];
        bool in_use;
        
        T* get() { return reinterpret_cast<T*>(storage); }
    };
    
    std::vector<std::unique_ptr<PooledObject>> objects_;
    std::queue<PooledObject*> available_;
    std::mutex mutex_;
    size_t initial_size_;
    size_t max_size_;
    size_t allocated_count_;
    
public:
    ObjectPool(size_t initial_size = 100, size_t max_size = 1000) 
        : initial_size_(initial_size), max_size_(max_size), allocated_count_(0) {
        
        // Pre-allocate initial pool
        for (size_t i = 0; i < initial_size_; i++) {
            auto obj = std::make_unique<PooledObject>();
            obj->in_use = false;
            available_.push(obj.get());
            objects_.push_back(std::move(obj));
        }
    }
    
    ~ObjectPool() {
        // Destroy all objects
        for (auto& obj : objects_) {
            if (obj->in_use) {
                obj->get()->~T();
            }
        }
    }
    
    // Acquire object from pool
    template<typename... Args>
    T* acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PooledObject* obj = nullptr;
        
        if (!available_.empty()) {
            // Reuse from pool
            obj = available_.front();
            available_.pop();
        } else if (objects_.size() < max_size_) {
            // Expand pool
            auto new_obj = std::make_unique<PooledObject>();
            obj = new_obj.get();
            objects_.push_back(std::move(new_obj));
        } else {
            // Pool exhausted, return nullptr
            return nullptr;
        }
        
        obj->in_use = true;
        allocated_count_++;
        
        // Construct object in-place
        new (obj->get()) T(std::forward<Args>(args)...);
        
        return obj->get();
    }
    
    // Release object back to pool
    void release(T* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find the pooled object
        for (auto& obj : objects_) {
            if (obj->get() == ptr) {
                if (obj->in_use) {
                    // Explicitly destroy the object
                    ptr->~T();
                    obj->in_use = false;
                    available_.push(obj.get());
                }
                return;
            }
        }
    }
    
    size_t size() const { return objects_.size(); }
    size_t available() const { return available_.size(); }
    size_t allocated() const { return allocated_count_; }
};

// Custom deleter for pooled objects
template<typename T>
class PoolDeleter {
private:
    ObjectPool<T>* pool_;
    
public:
    PoolDeleter(ObjectPool<T>* pool = nullptr) : pool_(pool) {}
    
    void operator()(T* ptr) {
        if (pool_) {
            pool_->release(ptr);
        } else {
            delete ptr;
        }
    }
};

// WebSocket frame class
class WSFrame {
public:
    enum class Opcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
    
private:
    std::vector<uint8_t> payload_;
    Opcode opcode_;
    bool fin_;
    
public:
    WSFrame(Opcode opcode = Opcode::TEXT, bool fin = true) 
        : opcode_(opcode), fin_(fin) {}
    
    void set_payload(const uint8_t* data, size_t len) {
        payload_.assign(data, data + len);
    }
    
    void set_payload(const std::string& data) {
        payload_.assign(data.begin(), data.end());
    }
    
    const std::vector<uint8_t>& payload() const { return payload_; }
    Opcode opcode() const { return opcode_; }
    bool fin() const { return fin_; }
    
    void clear() {
        payload_.clear();
        opcode_ = Opcode::TEXT;
        fin_ = true;
    }
};

// Memory arena for small allocations
class MemoryArena {
private:
    static constexpr size_t BLOCK_SIZE = 64 * 1024; // 64KB blocks
    
    struct Block {
        std::unique_ptr<uint8_t[]> data;
        size_t used;
        
        Block() : data(new uint8_t[BLOCK_SIZE]), used(0) {}
    };
    
    std::vector<std::unique_ptr<Block>> blocks_;
    Block* current_block_;
    
public:
    MemoryArena() {
        blocks_.push_back(std::make_unique<Block>());
        current_block_ = blocks_.back().get();
    }
    
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // Align the current position
        size_t padding = (alignment - (current_block_->used % alignment)) % alignment;
        size_t total = size + padding;
        
        if (current_block_->used + total > BLOCK_SIZE) {
            // Need new block
            blocks_.push_back(std::make_unique<Block>());
            current_block_ = blocks_.back().get();
            padding = 0;
            total = size;
        }
        
        void* ptr = current_block_->data.get() + current_block_->used + padding;
        current_block_->used += total;
        return ptr;
    }
    
    void reset() {
        for (auto& block : blocks_) {
            block->used = 0;
        }
        current_block_ = blocks_[0].get();
    }
    
    size_t total_allocated() const {
        return blocks_.size() * BLOCK_SIZE;
    }
};

// WebSocket connection with pooled resources
class WSConnection {
private:
    ObjectPool<WSFrame>* frame_pool_;
    MemoryArena arena_;
    uint64_t connection_id_;
    
public:
    WSConnection(ObjectPool<WSFrame>* pool, uint64_t id) 
        : frame_pool_(pool), connection_id_(id) {}
    
    std::unique_ptr<WSFrame, PoolDeleter<WSFrame>> create_frame(
        WSFrame::Opcode opcode = WSFrame::Opcode::TEXT) {
        
        WSFrame* frame = frame_pool_->acquire(opcode);
        return std::unique_ptr<WSFrame, PoolDeleter<WSFrame>>(
            frame, PoolDeleter<WSFrame>(frame_pool_));
    }
    
    void* allocate_temp(size_t size) {
        return arena_.allocate(size);
    }
    
    void reset_temp_memory() {
        arena_.reset();
    }
    
    uint64_t id() const { return connection_id_; }
};

// Example usage
int main() {
    // Create frame pool
    ObjectPool<WSFrame> frame_pool(50, 500);
    
    std::cout << "Initial pool size: " << frame_pool.size() 
              << ", available: " << frame_pool.available() << std::endl;
    
    // Create connections
    std::vector<std::unique_ptr<WSConnection>> connections;
    for (int i = 0; i < 10; i++) {
        connections.push_back(
            std::make_unique<WSConnection>(&frame_pool, i));
    }
    
    // Simulate message processing
    std::vector<std::unique_ptr<WSFrame, PoolDeleter<WSFrame>>> active_frames;
    
    for (int round = 0; round < 3; round++) {
        std::cout << "\n--- Round " << round + 1 << " ---" << std::endl;
        
        // Each connection sends messages
        for (auto& conn : connections) {
            auto frame = conn->create_frame(WSFrame::Opcode::TEXT);
            if (frame) {
                std::string msg = "Message from connection " + 
                                std::to_string(conn->id());
                frame->set_payload(msg);
                active_frames.push_back(std::move(frame));
            }
        }
        
        std::cout << "Active frames: " << active_frames.size() << std::endl;
        std::cout << "Pool available: " << frame_pool.available() << std::endl;
        std::cout << "Total allocated: " << frame_pool.allocated() << std::endl;
        
        // Clear half the frames (simulating processing)
        if (round > 0) {
            active_frames.erase(
                active_frames.begin(), 
                active_frames.begin() + active_frames.size() / 2);
        }
    }
    
    // Cleanup
    active_frames.clear();
    std::cout << "\nAfter cleanup:" << std::endl;
    std::cout << "Pool available: " << frame_pool.available() << std::endl;
    
    return 0;
}
```

## Rust Implementation with Type-Safe Memory Pools

```rust
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

// Object pool with type safety
pub struct ObjectPool<T> {
    objects: Vec<Box<T>>,
    available: VecDeque<usize>,
    in_use: Vec<bool>,
    max_size: usize,
    allocations: usize,
    deallocations: usize,
}

impl<T: Default> ObjectPool<T> {
    pub fn new(initial_size: usize, max_size: usize) -> Self {
        let mut objects = Vec::with_capacity(initial_size);
        let mut available = VecDeque::with_capacity(initial_size);
        let mut in_use = Vec::with_capacity(initial_size);
        
        // Pre-allocate objects
        for i in 0..initial_size {
            objects.push(Box::new(T::default()));
            available.push_back(i);
            in_use.push(false);
        }
        
        ObjectPool {
            objects,
            available,
            in_use,
            max_size,
            allocations: 0,
            deallocations: 0,
        }
    }
    
    pub fn acquire(&mut self) -> Option<PooledObject<T>> {
        // Try to get from available pool
        if let Some(idx) = self.available.pop_front() {
            self.in_use[idx] = true;
            self.allocations += 1;
            return Some(PooledObject::new(idx));
        }
        
        // Expand pool if possible
        if self.objects.len() < self.max_size {
            let idx = self.objects.len();
            self.objects.push(Box::new(T::default()));
            self.in_use.push(true);
            self.allocations += 1;
            return Some(PooledObject::new(idx));
        }
        
        // Pool exhausted
        None
    }
    
    pub fn release(&mut self, obj: PooledObject<T>) {
        let idx = obj.index();
        if idx < self.in_use.len() && self.in_use[idx] {
            self.in_use[idx] = false;
            self.available.push_back(idx);
            self.deallocations += 1;
        }
    }
    
    pub fn get_mut(&mut self, obj: &PooledObject<T>) -> Option<&mut T> {
        let idx = obj.index();
        if idx < self.objects.len() && self.in_use[idx] {
            Some(&mut self.objects[idx])
        } else {
            None
        }
    }
    
    pub fn stats(&self) -> PoolStats {
        PoolStats {
            total_objects: self.objects.len(),
            available: self.available.len(),
            in_use: self.in_use.iter().filter(|&&x| x).count(),
            allocations: self.allocations,
            deallocations: self.deallocations,
        }
    }
}

// Handle to pooled object
pub struct PooledObject<T> {
    index: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> PooledObject<T> {
    fn new(index: usize) -> Self {
        PooledObject {
            index,
            _phantom: std::marker::PhantomData,
        }
    }
    
    fn index(&self) -> usize {
        self.index
    }
}

#[derive(Debug)]
pub struct PoolStats {
    pub total_objects: usize,
    pub available: usize,
    pub in_use: usize,
    pub allocations: usize,
    pub deallocations: usize,
}

// Thread-safe object pool
pub struct ThreadSafePool<T> {
    inner: Arc<Mutex<ObjectPool<T>>>,
}

impl<T: Default> ThreadSafePool<T> {
    pub fn new(initial_size: usize, max_size: usize) -> Self {
        ThreadSafePool {
            inner: Arc::new(Mutex::new(ObjectPool::new(initial_size, max_size))),
        }
    }
    
    pub fn acquire(&self) -> Option<ThreadSafePooledObject<T>> {
        let mut pool = self.inner.lock().unwrap();
        pool.acquire().map(|obj| {
            ThreadSafePooledObject {
                inner: obj,
                pool: Arc::clone(&self.inner),
            }
        })
    }
    
    pub fn stats(&self) -> PoolStats {
        self.inner.lock().unwrap().stats()
    }
}

impl<T> Clone for ThreadSafePool<T> {
    fn clone(&self) -> Self {
        ThreadSafePool {
            inner: Arc::clone(&self.inner),
        }
    }
}

// RAII wrapper for thread-safe pooled objects
pub struct ThreadSafePooledObject<T> {
    inner: PooledObject<T>,
    pool: Arc<Mutex<ObjectPool<T>>>,
}

impl<T> Drop for ThreadSafePooledObject<T> {
    fn drop(&mut self) {
        // Release back to pool when dropped
        let mut pool = self.pool.lock().unwrap();
        let idx = self.inner.index();
        if idx < pool.in_use.len() && pool.in_use[idx] {
            pool.in_use[idx] = false;
            pool.available.push_back(idx);
            pool.deallocations += 1;
        }
    }
}

impl<T> ThreadSafePooledObject<T> {
    pub fn get_mut(&mut self) -> Option<&mut T> {
        let mut pool = self.pool.lock().unwrap();
        let idx = self.inner.index();
        if idx < pool.objects.len() && pool.in_use[idx] {
            // SAFETY: We know only one reference exists due to mutex
            unsafe {
                let ptr = pool.objects[idx].as_mut() as *mut T;
                Some(&mut *ptr)
            }
        } else {
            None
        }
    }
}

// WebSocket frame structure
#[derive(Debug, Clone)]
pub enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

#[derive(Default)]
pub struct WSFrame {
    pub payload: Vec<u8>,
    pub opcode: u8,
    pub fin: bool,
}

impl WSFrame {
    pub fn new(opcode: Opcode, fin: bool) -> Self {
        WSFrame {
            payload: Vec::new(),
            opcode: opcode as u8,
            fin,
        }
    }
    
    pub fn set_payload(&mut self, data: &[u8]) {
        self.payload.clear();
        self.payload.extend_from_slice(data);
    }
    
    pub fn clear(&mut self) {
        self.payload.clear();
        self.opcode = Opcode::Text as u8;
        self.fin = true;
    }
}

// Memory arena for temporary allocations
pub struct Arena {
    blocks: Vec<Vec<u8>>,
    current_block: usize,
    current_offset: usize,
    block_size: usize,
}

impl Arena {
    pub fn new(block_size: usize) -> Self {
        let mut blocks = Vec::new();
        blocks.push(vec![0u8; block_size]);
        
        Arena {
            blocks,
            current_block: 0,
            current_offset: 0,
            block_size,
        }
    }
    
    pub fn allocate(&mut self, size: usize) -> &mut [u8] {
        if self.current_offset + size > self.block_size {
            // Need new block
            self.blocks.push(vec![0u8; self.block_size]);
            self.current_block += 1;
            self.current_offset = 0;
        }
        
        let start = self.current_offset;
        self.current_offset += size;
        &mut self.blocks[self.current_block][start..start + size]
    }
    
    pub fn reset(&mut self) {
        self.current_block = 0;
        self.current_offset = 0;
    }
    
    pub fn total_allocated(&self) -> usize {
        self.blocks.len() * self.block_size
    }
}

// WebSocket connection with pooled resources
pub struct WSConnection {
    id: u64,
    arena: RefCell<Arena>,
}

impl WSConnection {
    pub fn new(id: u64) -> Self {
        WSConnection {
            id,
            arena: RefCell::new(Arena::new(64 * 1024)),
        }
    }
    
    pub fn allocate_temp(&self, size: usize) -> &mut [u8] {
        self.arena.borrow_mut().allocate(size)
    }
    
    pub fn reset_temp(&self) {
        self.arena.borrow_mut().reset();
    }
    
    pub fn id(&self) -> u64 {
        self.id
    }
}

fn main() {
    println!("WebSocket Memory Pool Demo");
    println!("===========================\n");
    
    // Create thread-safe frame pool
    let frame_pool = ThreadSafePool::<WSFrame>::new(50, 500);
    
    println!("Initial pool stats: {:?}\n", frame_pool.stats());
    
    // Create connections
    let mut connections: Vec<WSConnection> = (0..10)
        .map(|i| WSConnection::new(i))
        .collect();
    
    // Simulate message processing
    let mut active_frames: Vec<ThreadSafePooledObject<WSFrame>> = Vec::new();
    
    for round in 0..3 {
        println!("--- Round {} ---", round + 1);
        
        // Each connection sends messages
        for conn in &connections {
            if let Some(mut frame) = frame_pool.acquire() {
                if let Some(f) = frame.get_mut() {
                    f.opcode = Opcode::Text as u8;
                    let msg = format!("Message from connection {}", conn.id());
                    f.set_payload(msg.as_bytes());
                }
                active_frames.push(frame);
            }
        }
        
        println!("Active frames: {}", active_frames.len());
        println!("Pool stats: {:?}", frame_pool.stats());
        
        // Clear half the frames (simulating processing)
        if round > 0 {
            let mid = active_frames.len() / 2;
            active_frames.drain(0..mid);
        }
        
        println!();
    }
    
    // Cleanup
    active_frames.clear();
    println!("After cleanup:");
    println!("Pool stats: {:?}", frame_pool.stats());
    
    // Demonstrate arena allocation
    println!("\n--- Arena Allocation Demo ---");
    let conn = WSConnection::new(100);
    
    for i in 0..5 {
        let buffer = conn.allocate_temp(1024);
        let msg = format!("Temporary buffer {}", i);
        buffer[0..msg.len()].copy_from_slice(msg.as_bytes());
        println!("Allocated temp buffer {}", i);
    }
    
    conn.reset_temp();
    println!("Arena reset - memory reused");
}
```

## Summary

Memory pool allocation is a powerful optimization technique for WebSocket applications that dramatically improves performance by eliminating allocation overhead and reducing memory fragmentation. The key concepts include:

**Pre-allocation Strategy**: Memory pools allocate large blocks upfront and subdivide them as needed, avoiding expensive system allocator calls during message processing.

**Size Class Organization**: Effective pools organize memory into size classes matching typical WebSocket workloads - small pools for control frames, medium for typical messages, and large for maximum payload sizes.

**Object Pooling**: Beyond raw memory, pooling complete frame objects eliminates construction/destruction overhead. The C++ and Rust examples demonstrate RAII patterns where objects automatically return to pools when they go out of scope.

**Arena Allocation**: For very short-lived allocations within a single request or frame processing cycle, arena allocators provide bump-pointer allocation with bulk deallocation, offering the absolute fastest allocation pattern.

**Thread Safety Considerations**: High-performance WebSocket servers are multi-threaded, requiring either thread-local pools or mutex-protected shared pools. The Rust example shows both patterns with compile-time safety guarantees.

**Performance Gains**: Properly implemented memory pools can reduce allocation overhead from microseconds to nanoseconds, eliminate memory fragmentation, improve cache locality, and provide predictable latency - critical for real-time WebSocket applications handling thousands of concurrent connections.

The implementations demonstrate production-ready patterns: C provides direct control over memory layout, C++ offers template-based type safety with RAII semantics, and Rust guarantees memory safety with zero-cost abstractions and ownership tracking. Each approach trades different aspects of control, safety, and ergonomics while achieving the core goal of efficient memory reuse.