# Thread Pools and Work Queues for WebSocket Processing

## Overview

Thread pools and work queues are essential patterns for building scalable WebSocket servers. Instead of creating a new thread for each connection or blocking a single thread on I/O operations, thread pools maintain a fixed number of worker threads that process tasks from a shared queue. This approach prevents resource exhaustion, reduces context-switching overhead, and enables efficient handling of thousands of concurrent WebSocket connections.

## Core Concepts

### Thread Pool Architecture

A thread pool consists of:
- **Worker Threads**: A fixed number of threads waiting for work
- **Task Queue**: A thread-safe queue holding pending tasks
- **Work Distribution**: Logic to assign tasks to available workers
- **Connection Management**: Tracking active WebSocket connections

### Why Thread Pools for WebSockets?

WebSocket servers face unique challenges:
- **Long-lived connections**: Connections remain open for extended periods
- **Sporadic messages**: Data arrives unpredictably
- **Mixed workloads**: Some messages require quick responses, others need heavy processing
- **Scalability**: Servers must handle thousands of concurrent connections

Thread pools solve these by decoupling connection handling from message processing.

## C Implementation

Here's a practical implementation using POSIX threads:

```c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_QUEUE_SIZE 1000
#define THREAD_POOL_SIZE 8

// WebSocket message structure
typedef struct {
    int connection_id;
    char* payload;
    size_t payload_len;
    int opcode;  // 0x1 = text, 0x2 = binary, etc.
} ws_message_t;

// Task structure for the queue
typedef struct task_node {
    ws_message_t message;
    struct task_node* next;
} task_node_t;

// Thread pool structure
typedef struct {
    pthread_t* threads;
    size_t thread_count;
    
    task_node_t* queue_head;
    task_node_t* queue_tail;
    size_t queue_size;
    
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    
    bool shutdown;
} thread_pool_t;

// Initialize the thread pool
thread_pool_t* thread_pool_create(size_t thread_count) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;
    
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    pool->thread_count = thread_count;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_size = 0;
    pool->shutdown = false;
    
    pthread_mutex_init(&pool->queue_mutex, NULL);
    pthread_cond_init(&pool->queue_cond, NULL);
    
    return pool;
}

// Process a WebSocket message (worker task)
void process_websocket_message(ws_message_t* msg) {
    printf("[Thread %ld] Processing message from connection %d\n", 
           pthread_self(), msg->connection_id);
    
    // Simulate processing based on opcode
    switch(msg->opcode) {
        case 0x1: // Text frame
            printf("  Text message: %.*s\n", (int)msg->payload_len, msg->payload);
            // Echo back or process business logic
            break;
        case 0x2: // Binary frame
            printf("  Binary message: %zu bytes\n", msg->payload_len);
            break;
        case 0x8: // Close frame
            printf("  Connection closing\n");
            break;
    }
    
    // Simulate some work
    usleep(100000); // 100ms
}

// Worker thread function
void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for work or shutdown signal
        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Dequeue task
        task_node_t* task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            if (pool->queue_head == NULL) {
                pool->queue_tail = NULL;
            }
            pool->queue_size--;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        if (task) {
            process_websocket_message(&task->message);
            free(task->message.payload);
            free(task);
        }
    }
    
    return NULL;
}

// Start all worker threads
bool thread_pool_start(thread_pool_t* pool) {
    for (size_t i = 0; i < pool->thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            return false;
        }
    }
    return true;
}

// Submit a WebSocket message for processing
bool thread_pool_submit(thread_pool_t* pool, ws_message_t message) {
    task_node_t* task = malloc(sizeof(task_node_t));
    if (!task) return false;
    
    task->message = message;
    task->next = NULL;
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    if (pool->queue_size >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&pool->queue_mutex);
        free(task);
        return false; // Queue full
    }
    
    if (pool->queue_tail) {
        pool->queue_tail->next = task;
    } else {
        pool->queue_head = task;
    }
    pool->queue_tail = task;
    pool->queue_size++;
    
    pthread_cond_signal(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return true;
}

// Shutdown the thread pool
void thread_pool_destroy(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    for (size_t i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Clean up remaining tasks
    task_node_t* task = pool->queue_head;
    while (task) {
        task_node_t* next = task->next;
        free(task->message.payload);
        free(task);
        task = next;
    }
    
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_cond);
    free(pool->threads);
    free(pool);
}

// Example usage
int main() {
    thread_pool_t* pool = thread_pool_create(THREAD_POOL_SIZE);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }
    
    if (!thread_pool_start(pool)) {
        fprintf(stderr, "Failed to start threads\n");
        thread_pool_destroy(pool);
        return 1;
    }
    
    printf("Thread pool started with %d workers\n", THREAD_POOL_SIZE);
    
    // Simulate incoming WebSocket messages
    for (int i = 0; i < 20; i++) {
        ws_message_t msg;
        msg.connection_id = i % 5;
        msg.opcode = 0x1;
        msg.payload = strdup("Hello from WebSocket client!");
        msg.payload_len = strlen(msg.payload);
        
        if (thread_pool_submit(pool, msg)) {
            printf("Submitted message %d\n", i);
        } else {
            printf("Failed to submit message %d\n", i);
            free(msg.payload);
        }
        
        usleep(50000); // 50ms between submissions
    }
    
    // Let workers finish
    sleep(3);
    
    thread_pool_destroy(pool);
    printf("Thread pool destroyed\n");
    
    return 0;
}
```

## C++ Implementation

Modern C++ offers better abstractions with the standard library:

```cpp
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <chrono>

// WebSocket message structure
struct WebSocketMessage {
    int connection_id;
    std::string payload;
    uint8_t opcode;
    
    WebSocketMessage(int id, std::string data, uint8_t op)
        : connection_id(id), payload(std::move(data)), opcode(op) {}
};

// Thread pool implementation
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    
    size_t max_queue_size;
    
public:
    ThreadPool(size_t num_threads, size_t max_queue = 1000)
        : stop(false), max_queue_size(max_queue) {
        
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }
    
    // Submit a task to the pool
    template<class F>
    bool enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            if (tasks.size() >= max_queue_size) {
                return false; // Queue full
            }
            
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
        return true;
    }
    
    // Get current queue size
    size_t queue_size() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return tasks.size();
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        
        for (std::thread& worker : workers) {
            worker.join();
        }
    }
};

// WebSocket connection handler
class WebSocketHandler {
private:
    ThreadPool& pool;
    
public:
    WebSocketHandler(ThreadPool& tp) : pool(tp) {}
    
    void process_message(WebSocketMessage msg) {
        std::cout << "[Thread " << std::this_thread::get_id() 
                  << "] Processing connection " << msg.connection_id << "\n";
        
        switch (msg.opcode) {
            case 0x1: // Text
                std::cout << "  Text: " << msg.payload << "\n";
                // Echo or process
                break;
            case 0x2: // Binary
                std::cout << "  Binary: " << msg.payload.size() << " bytes\n";
                break;
            case 0x8: // Close
                std::cout << "  Connection closing\n";
                break;
            case 0x9: // Ping
                std::cout << "  Ping received, sending pong\n";
                // send_pong(msg.connection_id);
                break;
        }
        
        // Simulate processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    bool handle_incoming_message(WebSocketMessage msg) {
        return pool.enqueue([this, msg = std::move(msg)]() mutable {
            this->process_message(std::move(msg));
        });
    }
};

// Example usage
int main() {
    const size_t num_threads = 8;
    ThreadPool pool(num_threads);
    WebSocketHandler handler(pool);
    
    std::cout << "WebSocket thread pool started with " << num_threads << " workers\n";
    
    // Simulate incoming messages
    for (int i = 0; i < 30; ++i) {
        WebSocketMessage msg(i % 5, "Message #" + std::to_string(i), 0x1);
        
        if (handler.handle_incoming_message(std::move(msg))) {
            std::cout << "Queued message " << i << " (queue size: " 
                      << pool.queue_size() << ")\n";
        } else {
            std::cout << "Failed to queue message " << i << " - queue full\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Allow processing to complete
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "Shutting down...\n";
    return 0;
}
```

## Rust Implementation

Rust provides safe concurrency with channels and thread pools:

```rust
use std::sync::{Arc, Mutex, Condvar};
use std::sync::mpsc::{self, Sender, Receiver};
use std::thread;
use std::time::Duration;
use std::collections::VecDeque;

// WebSocket message structure
#[derive(Debug, Clone)]
struct WebSocketMessage {
    connection_id: u32,
    payload: Vec<u8>,
    opcode: u8,
}

impl WebSocketMessage {
    fn new(connection_id: u32, payload: Vec<u8>, opcode: u8) -> Self {
        Self {
            connection_id,
            payload,
            opcode,
        }
    }
    
    fn as_text(&self) -> Option<String> {
        String::from_utf8(self.payload.clone()).ok()
    }
}

// Work queue with bounded capacity
struct WorkQueue<T> {
    queue: Mutex<VecDeque<T>>,
    condvar: Condvar,
    max_size: usize,
}

impl<T> WorkQueue<T> {
    fn new(max_size: usize) -> Self {
        Self {
            queue: Mutex::new(VecDeque::new()),
            condvar: Condvar::new(),
            max_size,
        }
    }
    
    fn push(&self, item: T) -> Result<(), &'static str> {
        let mut queue = self.queue.lock().unwrap();
        
        if queue.len() >= self.max_size {
            return Err("Queue full");
        }
        
        queue.push_back(item);
        self.condvar.notify_one();
        Ok(())
    }
    
    fn pop(&self) -> Option<T> {
        let mut queue = self.queue.lock().unwrap();
        
        while queue.is_empty() {
            queue = self.condvar.wait(queue).unwrap();
        }
        
        queue.pop_front()
    }
    
    fn len(&self) -> usize {
        self.queue.lock().unwrap().len()
    }
}

// Thread pool implementation
struct ThreadPool {
    workers: Vec<thread::JoinHandle<()>>,
    sender: Option<Sender<Message>>,
}

enum Message {
    NewJob(Box<dyn FnOnce() + Send + 'static>),
    Terminate,
}

impl ThreadPool {
    fn new(size: usize) -> Self {
        assert!(size > 0);
        
        let (sender, receiver) = mpsc::channel();
        let receiver = Arc::new(Mutex::new(receiver));
        
        let mut workers = Vec::with_capacity(size);
        
        for id in 0..size {
            let receiver = Arc::clone(&receiver);
            
            let handle = thread::spawn(move || {
                loop {
                    let message = receiver.lock().unwrap().recv();
                    
                    match message {
                        Ok(Message::NewJob(job)) => {
                            println!("[Worker {}] Executing job", id);
                            job();
                        }
                        Ok(Message::Terminate) => {
                            println!("[Worker {}] Terminating", id);
                            break;
                        }
                        Err(_) => {
                            println!("[Worker {}] Sender disconnected", id);
                            break;
                        }
                    }
                }
            });
            
            workers.push(handle);
        }
        
        ThreadPool {
            workers,
            sender: Some(sender),
        }
    }
    
    fn execute<F>(&self, f: F) -> Result<(), &'static str>
    where
        F: FnOnce() + Send + 'static,
    {
        let job = Box::new(f);
        
        self.sender
            .as_ref()
            .ok_or("ThreadPool has been shut down")?
            .send(Message::NewJob(job))
            .map_err(|_| "Failed to send job")
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        if let Some(sender) = self.sender.take() {
            for _ in &self.workers {
                sender.send(Message::Terminate).unwrap();
            }
        }
        
        for worker in &mut self.workers {
            if let Some(handle) = std::mem::replace(worker, thread::spawn(|| {})).into() {
                handle.join().unwrap();
            }
        }
    }
}

// WebSocket message processor
struct WebSocketProcessor {
    pool: Arc<ThreadPool>,
}

impl WebSocketProcessor {
    fn new(num_threads: usize) -> Self {
        Self {
            pool: Arc::new(ThreadPool::new(num_threads)),
        }
    }
    
    fn process_message(&self, msg: WebSocketMessage) {
        let thread_id = thread::current().id();
        
        println!("[{:?}] Processing connection {}", thread_id, msg.connection_id);
        
        match msg.opcode {
            0x1 => {
                // Text frame
                if let Some(text) = msg.as_text() {
                    println!("  Text: {}", text);
                }
            }
            0x2 => {
                // Binary frame
                println!("  Binary: {} bytes", msg.payload.len());
            }
            0x8 => {
                // Close frame
                println!("  Connection closing");
            }
            0x9 => {
                // Ping
                println!("  Ping received");
            }
            _ => {
                println!("  Unknown opcode: 0x{:02x}", msg.opcode);
            }
        }
        
        // Simulate work
        thread::sleep(Duration::from_millis(100));
    }
    
    fn handle_message(&self, msg: WebSocketMessage) -> Result<(), &'static str> {
        let pool = Arc::clone(&self.pool);
        let processor = self.clone();
        
        pool.execute(move || {
            processor.process_message(msg);
        })
    }
}

impl Clone for WebSocketProcessor {
    fn clone(&self) -> Self {
        Self {
            pool: Arc::clone(&self.pool),
        }
    }
}

fn main() {
    const NUM_THREADS: usize = 8;
    
    let processor = WebSocketProcessor::new(NUM_THREADS);
    println!("WebSocket thread pool started with {} workers", NUM_THREADS);
    
    // Simulate incoming messages
    for i in 0..30 {
        let msg = WebSocketMessage::new(
            i % 5,
            format!("Message #{}", i).into_bytes(),
            0x1,
        );
        
        match processor.handle_message(msg) {
            Ok(_) => println!("Queued message {}", i),
            Err(e) => println!("Failed to queue message {}: {}", i, e),
        }
        
        thread::sleep(Duration::from_millis(50));
    }
    
    // Allow processing to complete
    println!("Waiting for processing to complete...");
    thread::sleep(Duration::from_secs(3));
    
    println!("Shutting down...");
}
```

## Summary

Thread pools and work queues are fundamental patterns for scalable WebSocket servers, enabling efficient processing of concurrent connections without overwhelming system resources. The key benefits include:

**Resource Management**: Fixed thread count prevents resource exhaustion from thousands of connections, while work queues buffer incoming messages during traffic spikes.

**Performance Optimization**: Reusing threads eliminates creation/destruction overhead, and asynchronous processing prevents blocking on slow operations.

**Implementation Approaches**: C uses POSIX threads with manual synchronization, C++ leverages standard library abstractions with RAII, and Rust ensures memory safety and prevents data races through its ownership system.

**Design Considerations**: Queue sizing balances memory usage against message loss risk, thread count should match CPU cores for CPU-bound tasks or exceed it for I/O-bound workloads, and graceful shutdown requires draining queues and joining all worker threads.

This pattern is essential for production WebSocket servers handling high-volume, real-time communication where responsiveness and scalability are critical requirements.