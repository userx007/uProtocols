# Priority Inversion Prevention in CAN Applications

## Understanding Priority Inversion

Priority inversion occurs when a high-priority task is blocked waiting for a resource held by a low-priority task, while a medium-priority task preempts the low-priority task, effectively causing the high-priority task to wait longer than intended. In CAN (Controller Area Network) applications, this can lead to missed deadlines, delayed critical messages, and degraded real-time performance.

### Why Priority Inversion Matters in CAN

CAN networks are inherently priority-based at the protocol level through message arbitration. However, priority inversion problems typically occur in the software layer when multiple threads or tasks handle CAN messages with different priorities. Common scenarios include:

- High-priority safety messages waiting for buffer access locked by low-priority diagnostic messages
- Time-critical control loops delayed by non-critical logging operations
- Emergency messages blocked by routine status updates

## Core Concepts

**Priority Inheritance Protocol (PIP)**: When a low-priority task holds a resource needed by a high-priority task, the low-priority task temporarily inherits the higher priority to complete quickly and release the resource.

**Priority Ceiling Protocol (PCP)**: Resources are assigned a priority ceiling equal to the highest priority of any task that may lock them. When a task locks a resource, it temporarily inherits the ceiling priority.

**Deadlock Avoidance**: Ensuring that resource acquisition follows consistent ordering to prevent circular dependencies.

## C/C++ Implementation Examples

### Example 1: Priority Inheritance with POSIX Mutexes

```c
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t dlc;
} can_frame_t;

typedef struct {
    can_frame_t buffer[256];
    size_t read_idx;
    size_t write_idx;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
} can_queue_t;

// Initialize queue with priority inheritance
int can_queue_init(can_queue_t *queue) {
    pthread_mutexattr_t attr;
    
    // Initialize mutex attributes
    if (pthread_mutexattr_init(&attr) != 0) {
        return -1;
    }
    
    // Enable priority inheritance protocol
    if (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != 0) {
        pthread_mutexattr_destroy(&attr);
        return -1;
    }
    
    // Create mutex with priority inheritance
    if (pthread_mutex_init(&queue->mutex, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        return -1;
    }
    
    pthread_mutexattr_destroy(&attr);
    
    pthread_cond_init(&queue->cond_not_empty, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);
    
    queue->read_idx = 0;
    queue->write_idx = 0;
    
    return 0;
}

// High-priority CAN message handler
void* high_priority_can_handler(void* arg) {
    can_queue_t *queue = (can_queue_t*)arg;
    can_frame_t frame;
    
    while (1) {
        // Receive critical CAN message
        frame.can_id = 0x100; // Emergency stop message
        
        pthread_mutex_lock(&queue->mutex);
        
        // Wait if queue is full
        while ((queue->write_idx + 1) % 256 == queue->read_idx) {
            pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
        }
        
        // Write to queue - low-priority task holding mutex
        // will inherit this thread's priority
        queue->buffer[queue->write_idx] = frame;
        queue->write_idx = (queue->write_idx + 1) % 256;
        
        pthread_cond_signal(&queue->cond_not_empty);
        pthread_mutex_unlock(&queue->mutex);
    }
    
    return NULL;
}

// Low-priority processing task
void* low_priority_processor(void* arg) {
    can_queue_t *queue = (can_queue_t*)arg;
    can_frame_t frame;
    
    while (1) {
        pthread_mutex_lock(&queue->mutex);
        
        while (queue->read_idx == queue->write_idx) {
            pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
        }
        
        frame = queue->buffer[queue->read_idx];
        queue->read_idx = (queue->read_idx + 1) % 256;
        
        pthread_cond_signal(&queue->cond_not_full);
        
        // Simulate processing time
        // If high-priority task needs mutex, this task inherits its priority
        for (volatile int i = 0; i < 1000000; i++);
        
        pthread_mutex_unlock(&queue->mutex);
    }
    
    return NULL;
}
```

### Example 2: Priority Ceiling Protocol Implementation

```cpp
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

// Resource with priority ceiling
template<typename T>
class PriorityCeilingResource {
private:
    T resource;
    std::mutex mtx;
    int ceiling_priority;
    std::atomic<int> current_holder_original_priority{-1};
    
public:
    PriorityCeilingResource(int ceiling_prio) 
        : ceiling_priority(ceiling_prio) {}
    
    template<typename Func>
    auto access(int task_priority, Func&& func) -> decltype(func(resource)) {
        // Lock the resource
        std::lock_guard<std::mutex> lock(mtx);
        
        // Store original priority
        current_holder_original_priority.store(task_priority);
        
        // In real RTOS, we would elevate thread priority here
        // For demonstration, we just track it
        int elevated_priority = std::max(task_priority, ceiling_priority);
        
        // Execute critical section with elevated priority
        auto result = func(resource);
        
        // Restore original priority (would be done by RTOS)
        current_holder_original_priority.store(-1);
        
        return result;
    }
    
    int get_ceiling() const { return ceiling_priority; }
};

// CAN message buffer with priority ceiling
class CANMessageBuffer {
private:
    struct CANMessage {
        uint32_t id;
        uint8_t data[8];
        uint8_t dlc;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    PriorityCeilingResource<std::vector<CANMessage>> buffer;
    
public:
    // Ceiling priority = highest priority of any task accessing this resource
    CANMessageBuffer() : buffer(10) {} // Priority 10 (highest)
    
    void push_message(uint32_t id, const uint8_t* data, uint8_t dlc, int task_priority) {
        buffer.access(task_priority, [&](std::vector<CANMessage>& buf) {
            CANMessage msg;
            msg.id = id;
            msg.dlc = dlc;
            std::copy(data, data + dlc, msg.data);
            msg.timestamp = std::chrono::steady_clock::now();
            
            buf.push_back(msg);
            
            // Limit buffer size
            if (buf.size() > 100) {
                buf.erase(buf.begin());
            }
        });
    }
    
    bool pop_message(CANMessage& msg, int task_priority) {
        return buffer.access(task_priority, [&](std::vector<CANMessage>& buf) {
            if (buf.empty()) return false;
            
            msg = buf.front();
            buf.erase(buf.begin());
            return true;
        });
    }
};
```

### Example 3: Lock-Free Queue for Critical Paths

```cpp
#include <atomic>
#include <array>
#include <optional>

// Lock-free single-producer single-consumer queue
// Avoids priority inversion by eliminating locks on critical path
template<typename T, size_t Size>
class LockFreeCANQueue {
private:
    std::array<T, Size> buffer;
    std::atomic<size_t> write_index{0};
    std::atomic<size_t> read_index{0};
    
    size_t next_index(size_t current) const {
        return (current + 1) % Size;
    }
    
public:
    // Try to push a message (non-blocking)
    bool try_push(const T& item) {
        size_t current_write = write_index.load(std::memory_order_relaxed);
        size_t next_write = next_index(current_write);
        
        // Check if queue is full
        if (next_write == read_index.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        buffer[current_write] = item;
        write_index.store(next_write, std::memory_order_release);
        return true;
    }
    
    // Try to pop a message (non-blocking)
    std::optional<T> try_pop() {
        size_t current_read = read_index.load(std::memory_order_relaxed);
        
        // Check if queue is empty
        if (current_read == write_index.load(std::memory_order_acquire)) {
            return std::nullopt; // Queue empty
        }
        
        T item = buffer[current_read];
        read_index.store(next_index(current_read), std::memory_order_release);
        return item;
    }
    
    bool is_empty() const {
        return read_index.load(std::memory_order_acquire) == 
               write_index.load(std::memory_order_acquire);
    }
    
    bool is_full() const {
        return next_index(write_index.load(std::memory_order_acquire)) == 
               read_index.load(std::memory_order_acquire);
    }
};

struct CANFrame {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    uint64_t timestamp_us;
};

// Usage in high-priority interrupt context
LockFreeCANQueue<CANFrame, 128> rx_queue;

void can_rx_interrupt_handler() {
    CANFrame frame;
    // Read from hardware registers
    frame.id = 0x123;
    frame.dlc = 8;
    
    // No locks - no priority inversion possible
    if (!rx_queue.try_push(frame)) {
        // Handle queue overflow
    }
}

void high_priority_task() {
    while (true) {
        auto frame = rx_queue.try_pop();
        if (frame) {
            // Process high-priority message
        }
    }
}
```

## Rust Implementation Examples

### Example 1: Using Priority-Aware Mutex

```rust
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

#[derive(Clone, Copy)]
struct CANFrame {
    id: u32,
    data: [u8; 8],
    dlc: u8,
}

struct CANBuffer {
    frames: Vec<CANFrame>,
    max_size: usize,
}

impl CANBuffer {
    fn new(max_size: usize) -> Self {
        CANBuffer {
            frames: Vec::with_capacity(max_size),
            max_size,
        }
    }
    
    fn push(&mut self, frame: CANFrame) -> Result<(), &'static str> {
        if self.frames.len() >= self.max_size {
            return Err("Buffer full");
        }
        self.frames.push(frame);
        Ok(())
    }
    
    fn pop(&mut self) -> Option<CANFrame> {
        if self.frames.is_empty() {
            None
        } else {
            Some(self.frames.remove(0))
        }
    }
}

// Simulate priority-based system
fn demonstrate_priority_inversion() {
    let buffer = Arc::new(Mutex::new(CANBuffer::new(100)));
    
    // Low-priority task
    let buffer_low = Arc::clone(&buffer);
    let low_priority = thread::spawn(move || {
        for i in 0..5 {
            let mut buf = buffer_low.lock().unwrap();
            println!("Low priority: acquired lock {}", i);
            
            let frame = CANFrame {
                id: 0x200,
                data: [0; 8],
                dlc: 8,
            };
            buf.push(frame).ok();
            
            // Simulate long processing while holding lock
            thread::sleep(Duration::from_millis(100));
            println!("Low priority: releasing lock {}", i);
        }
    });
    
    // Give low priority time to acquire lock
    thread::sleep(Duration::from_millis(10));
    
    // High-priority task
    let buffer_high = Arc::clone(&buffer);
    let high_priority = thread::spawn(move || {
        println!("High priority: waiting for lock (priority inversion!)");
        let start = std::time::Instant::now();
        
        let mut buf = buffer_high.lock().unwrap();
        let elapsed = start.elapsed();
        
        println!("High priority: acquired lock after {:?}", elapsed);
        
        let frame = CANFrame {
            id: 0x100, // Emergency message
            data: [0xFF; 8],
            dlc: 8,
        };
        buf.push(frame).ok();
    });
    
    low_priority.join().unwrap();
    high_priority.join().unwrap();
}
```

### Example 2: Lock-Free Queue with Crossbeam

```rust
use crossbeam::queue::ArrayQueue;
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug)]
struct CANMessage {
    id: u32,
    data: [u8; 8],
    dlc: u8,
    timestamp: u64,
}

struct LockFreeCANSystem {
    high_priority_queue: Arc<ArrayQueue<CANMessage>>,
    low_priority_queue: Arc<ArrayQueue<CANMessage>>,
}

impl LockFreeCANSystem {
    fn new() -> Self {
        LockFreeCANSystem {
            high_priority_queue: Arc::new(ArrayQueue::new(128)),
            low_priority_queue: Arc::new(ArrayQueue::new(256)),
        }
    }
    
    fn send_high_priority(&self, msg: CANMessage) -> Result<(), CANMessage> {
        self.high_priority_queue.push(msg)
    }
    
    fn send_low_priority(&self, msg: CANMessage) -> Result<(), CANMessage> {
        self.low_priority_queue.push(msg)
    }
    
    fn receive(&self) -> Option<CANMessage> {
        // Always check high-priority queue first
        self.high_priority_queue.pop()
            .or_else(|| self.low_priority_queue.pop())
    }
}

fn demonstrate_lock_free_can() {
    let system = Arc::new(LockFreeCANSystem::new());
    
    // High-priority sender
    let sys_high = Arc::clone(&system);
    let high_sender = thread::spawn(move || {
        for i in 0..10 {
            let msg = CANMessage {
                id: 0x100 + i,
                data: [i as u8; 8],
                dlc: 8,
                timestamp: Instant::now().elapsed().as_micros() as u64,
            };
            
            // Non-blocking - no priority inversion possible
            match sys_high.send_high_priority(msg) {
                Ok(_) => println!("High priority sent: 0x{:X}", msg.id),
                Err(_) => println!("High priority queue full"),
            }
            
            thread::sleep(Duration::from_millis(5));
        }
    });
    
    // Low-priority sender
    let sys_low = Arc::clone(&system);
    let low_sender = thread::spawn(move || {
        for i in 0..20 {
            let msg = CANMessage {
                id: 0x200 + i,
                data: [i as u8; 8],
                dlc: 8,
                timestamp: Instant::now().elapsed().as_micros() as u64,
            };
            
            sys_low.send_low_priority(msg).ok();
            thread::sleep(Duration::from_millis(10));
        }
    });
    
    // Receiver - always processes high-priority first
    let sys_recv = Arc::clone(&system);
    let receiver = thread::spawn(move || {
        for _ in 0..30 {
            if let Some(msg) = sys_recv.receive() {
                println!("Received: ID=0x{:X}, Priority={}",
                    msg.id,
                    if msg.id < 0x200 { "HIGH" } else { "LOW" }
                );
            }
            thread::sleep(Duration::from_millis(3));
        }
    });
    
    high_sender.join().unwrap();
    low_sender.join().unwrap();
    receiver.join().unwrap();
}
```

### Example 3: Priority-Based Message Scheduler

```rust
use std::collections::BinaryHeap;
use std::cmp::Ordering;
use std::sync::{Arc, Mutex, Condvar};
use std::thread;
use std::time::Duration;

#[derive(Clone, Copy, Debug)]
struct PrioritizedCANMessage {
    id: u32,
    data: [u8; 8],
    dlc: u8,
    priority: u8, // 0 = highest
    sequence: u64,
}

impl Eq for PrioritizedCANMessage {}

impl PartialEq for PrioritizedCANMessage {
    fn eq(&self, other: &Self) -> bool {
        self.priority == other.priority && self.sequence == other.sequence
    }
}

impl Ord for PrioritizedCANMessage {
    fn cmp(&self, other: &Self) -> Ordering {
        // Reverse ordering for min-heap behavior (lower priority value = higher priority)
        other.priority.cmp(&self.priority)
            .then_with(|| self.sequence.cmp(&other.sequence))
    }
}

impl PartialOrd for PrioritizedCANMessage {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

struct PriorityCANScheduler {
    queue: Mutex<BinaryHeap<PrioritizedCANMessage>>,
    condvar: Condvar,
    sequence_counter: Mutex<u64>,
}

impl PriorityCANScheduler {
    fn new() -> Self {
        PriorityCANScheduler {
            queue: Mutex::new(BinaryHeap::new()),
            condvar: Condvar::new(),
            sequence_counter: Mutex::new(0),
        }
    }
    
    fn schedule(&self, mut msg: PrioritizedCANMessage) {
        // Assign sequence number for FIFO within same priority
        let mut seq = self.sequence_counter.lock().unwrap();
        msg.sequence = *seq;
        *seq += 1;
        drop(seq);
        
        let mut queue = self.queue.lock().unwrap();
        queue.push(msg);
        self.condvar.notify_one();
    }
    
    fn get_next(&self) -> Option<PrioritizedCANMessage> {
        let mut queue = self.queue.lock().unwrap();
        
        while queue.is_empty() {
            queue = self.condvar.wait(queue).unwrap();
        }
        
        queue.pop()
    }
    
    fn try_get_next(&self) -> Option<PrioritizedCANMessage> {
        let mut queue = self.queue.lock().unwrap();
        queue.pop()
    }
}

fn demonstrate_priority_scheduler() {
    let scheduler = Arc::new(PriorityCANScheduler::new());
    
    // Emergency message sender
    let sched_emergency = Arc::clone(&scheduler);
    let emergency_sender = thread::spawn(move || {
        thread::sleep(Duration::from_millis(50)); // Start after others
        
        let msg = PrioritizedCANMessage {
            id: 0x001,
            data: [0xFF; 8],
            dlc: 8,
            priority: 0, // Highest priority
            sequence: 0,
        };
        
        println!("Scheduling EMERGENCY message");
        sched_emergency.schedule(msg);
    });
    
    // Normal message sender
    let sched_normal = Arc::clone(&scheduler);
    let normal_sender = thread::spawn(move || {
        for i in 0..5 {
            let msg = PrioritizedCANMessage {
                id: 0x100 + i,
                data: [i as u8; 8],
                dlc: 8,
                priority: 5, // Medium priority
                sequence: 0,
            };
            
            println!("Scheduling normal message {}", i);
            sched_normal.schedule(msg);
            thread::sleep(Duration::from_millis(20));
        }
    });
    
    // Receiver - processes by priority
    let sched_recv = Arc::clone(&scheduler);
    let receiver = thread::spawn(move || {
        for _ in 0..6 {
            if let Some(msg) = sched_recv.get_next() {
                println!("Processing: ID=0x{:03X}, Priority={}, Seq={}",
                    msg.id, msg.priority, msg.sequence);
                thread::sleep(Duration::from_millis(30));
            }
        }
    });
    
    emergency_sender.join().unwrap();
    normal_sender.join().unwrap();
    receiver.join().unwrap();
}

fn main() {
    println!("=== Lock-Free CAN Demo ===");
    demonstrate_lock_free_can();
    
    println!("\n=== Priority Scheduler Demo ===");
    demonstrate_priority_scheduler();
}
```

## Summary

**Priority inversion** in CAN applications occurs when high-priority message handlers are blocked by lower-priority tasks holding shared resources, creating a serious real-time constraint violation. This document explored three primary prevention strategies:

**Priority Inheritance Protocol** allows low-priority tasks to temporarily inherit the priority of blocked high-priority tasks, ensuring they complete quickly and release contested resources. The C examples demonstrated POSIX mutex configuration with `PTHREAD_PRIO_INHERIT` for automatic priority boosting.

**Priority Ceiling Protocol** assigns each resource a ceiling priority equal to the highest priority of any task that may access it, preventing medium-priority tasks from preempting resource holders. The C++ template example showed how to implement ceiling-based resource access control.

**Lock-Free Data Structures** eliminate priority inversion entirely on critical paths by removing blocking synchronization. Both C++ and Rust examples demonstrated lock-free queues using atomic operations and memory ordering, ideal for interrupt-driven CAN reception where deterministic latency is essential.

The Rust implementations leveraged `crossbeam`'s lock-free queues and demonstrated priority-based scheduling using binary heaps, showing how modern language features can simplify concurrent CAN system design while maintaining real-time guarantees. Key takeaways include: minimize lock hold times, use appropriate synchronization primitives for your RTOS, consider lock-free designs for highest-priority paths, and always validate timing requirements under worst-case scenarios including priority inversion conditions.