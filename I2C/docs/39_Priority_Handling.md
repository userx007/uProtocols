# I2C Priority Handling: Detailed Guide

## Overview

Priority handling in I2C communication ensures that critical transactions (like safety sensors, real-time control signals, or emergency shutdowns) are processed before less urgent operations (like status updates or logging). Since I2C is a shared bus with a single master in most configurations, implementing priority schemes requires careful queue management and preemption strategies.

## Key Concepts

**Why Priority Handling Matters:**
- Safety-critical sensors need immediate attention
- Real-time control systems require deterministic response times
- Background tasks shouldn't block urgent communications
- System stability depends on handling emergencies promptly

**Common Approaches:**
1. **Priority Queues** - Multiple queues sorted by urgency
2. **Preemption** - Ability to interrupt lower-priority transactions
3. **Dynamic Scheduling** - Adjusting priorities based on deadlines
4. **Deadline-Monotonic** - Earlier deadlines get higher priority

## Implementation Strategies

### 1. Basic Priority Queue System

The simplest approach uses multiple queues with different priority levels.

**C Implementation:**

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Priority levels
typedef enum {
    PRIORITY_CRITICAL = 0,  // Emergency/safety
    PRIORITY_HIGH = 1,      // Real-time control
    PRIORITY_NORMAL = 2,    // Standard operations
    PRIORITY_LOW = 3,       // Background tasks
    PRIORITY_LEVELS = 4
} i2c_priority_t;

// I2C transaction structure
typedef struct {
    uint8_t device_addr;
    uint8_t *data;
    uint16_t length;
    bool is_read;
    void (*callback)(bool success);
    uint32_t deadline_ms;  // For deadline-aware scheduling
} i2c_transaction_t;

// Queue node
typedef struct queue_node {
    i2c_transaction_t transaction;
    struct queue_node *next;
} queue_node_t;

// Priority queue manager
typedef struct {
    queue_node_t *queues[PRIORITY_LEVELS];
    queue_node_t *queue_tails[PRIORITY_LEVELS];
    uint32_t queue_counts[PRIORITY_LEVELS];
    bool transaction_in_progress;
    i2c_priority_t current_priority;
} i2c_priority_manager_t;

static i2c_priority_manager_t priority_mgr = {0};

// Initialize priority manager
void i2c_priority_init(void) {
    memset(&priority_mgr, 0, sizeof(priority_mgr));
}

// Enqueue transaction with priority
bool i2c_enqueue_priority(i2c_transaction_t *trans, i2c_priority_t priority) {
    if (priority >= PRIORITY_LEVELS) return false;
    
    // Allocate node
    queue_node_t *node = (queue_node_t*)malloc(sizeof(queue_node_t));
    if (!node) return false;
    
    node->transaction = *trans;
    node->next = NULL;
    
    // Add to appropriate queue
    if (priority_mgr.queues[priority] == NULL) {
        priority_mgr.queues[priority] = node;
        priority_mgr.queue_tails[priority] = node;
    } else {
        priority_mgr.queue_tails[priority]->next = node;
        priority_mgr.queue_tails[priority] = node;
    }
    
    priority_mgr.queue_counts[priority]++;
    return true;
}

// Get next transaction (highest priority first)
bool i2c_get_next_transaction(i2c_transaction_t *trans, i2c_priority_t *priority) {
    for (int i = 0; i < PRIORITY_LEVELS; i++) {
        if (priority_mgr.queues[i] != NULL) {
            queue_node_t *node = priority_mgr.queues[i];
            *trans = node->transaction;
            *priority = (i2c_priority_t)i;
            
            // Remove from queue
            priority_mgr.queues[i] = node->next;
            if (priority_mgr.queues[i] == NULL) {
                priority_mgr.queue_tails[i] = NULL;
            }
            priority_mgr.queue_counts[i]--;
            
            free(node);
            return true;
        }
    }
    return false;
}

// Check if higher priority transaction is waiting
bool i2c_has_higher_priority(i2c_priority_t current) {
    for (int i = 0; i < current; i++) {
        if (priority_mgr.queue_counts[i] > 0) {
            return true;
        }
    }
    return false;
}

// Main scheduler function (call periodically)
void i2c_priority_scheduler(void) {
    if (priority_mgr.transaction_in_progress) {
        // Check for preemption opportunity
        if (i2c_has_higher_priority(priority_mgr.current_priority)) {
            // Could implement preemption here if hardware supports it
            // For now, we let current transaction complete
        }
        return;
    }
    
    i2c_transaction_t trans;
    i2c_priority_t priority;
    
    if (i2c_get_next_transaction(&trans, &priority)) {
        priority_mgr.transaction_in_progress = true;
        priority_mgr.current_priority = priority;
        
        // Execute transaction
        bool success = i2c_execute_transaction(&trans);
        
        if (trans.callback) {
            trans.callback(success);
        }
        
        priority_mgr.transaction_in_progress = false;
    }
}

// Hardware-specific transaction execution
bool i2c_execute_transaction(i2c_transaction_t *trans) {
    // Placeholder for actual I2C hardware operation
    // This would use your platform's I2C driver
    return true;
}
```

### 2. Advanced C++ Implementation with RTOS Integration

**C++ Implementation with Templates:**

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>
#include <chrono>

enum class I2CPriority {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3
};

struct I2CTransaction {
    uint8_t device_addr;
    std::vector<uint8_t> data;
    bool is_read;
    std::function<void(bool)> callback;
    std::chrono::steady_clock::time_point deadline;
    
    I2CTransaction(uint8_t addr, std::vector<uint8_t> d, bool read = false)
        : device_addr(addr), data(std::move(d)), is_read(read),
          deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(100)) {}
};

// Priority queue comparator
struct TransactionComparator {
    bool operator()(const std::pair<I2CPriority, std::shared_ptr<I2CTransaction>>& a,
                   const std::pair<I2CPriority, std::shared_ptr<I2CTransaction>>& b) {
        // Higher priority value means lower priority (inverted for priority_queue)
        if (a.first != b.first) {
            return a.first > b.first;
        }
        // For same priority, earlier deadline first
        return a.second->deadline > b.second->deadline;
    }
};

class I2CPriorityScheduler {
private:
    std::priority_queue
        std::pair<I2CPriority, std::shared_ptr<I2CTransaction>>,
        std::vector<std::pair<I2CPriority, std::shared_ptr<I2CTransaction>>>,
        TransactionComparator
    > transaction_queue;
    
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    bool running;
    bool transaction_in_progress;
    
public:
    I2CPriorityScheduler() : running(false), transaction_in_progress(false) {}
    
    void enqueue(std::shared_ptr<I2CTransaction> trans, I2CPriority priority) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        transaction_queue.push({priority, trans});
        queue_cv.notify_one();
    }
    
    void start() {
        running = true;
        // Start scheduler thread
        std::thread([this]() { this->schedulerLoop(); }).detach();
    }
    
    void stop() {
        running = false;
        queue_cv.notify_all();
    }
    
private:
    void schedulerLoop() {
        while (running) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // Wait for transactions
            queue_cv.wait(lock, [this]() { 
                return !transaction_queue.empty() || !running; 
            });
            
            if (!running) break;
            
            if (!transaction_queue.empty()) {
                auto [priority, trans] = transaction_queue.top();
                transaction_queue.pop();
                transaction_in_progress = true;
                
                lock.unlock();
                
                // Check deadline
                if (std::chrono::steady_clock::now() > trans->deadline) {
                    // Transaction missed deadline
                    if (trans->callback) {
                        trans->callback(false);
                    }
                } else {
                    // Execute transaction
                    bool success = executeTransaction(trans);
                    if (trans->callback) {
                        trans->callback(success);
                    }
                }
                
                transaction_in_progress = false;
            }
        }
    }
    
    bool executeTransaction(std::shared_ptr<I2CTransaction> trans) {
        // Platform-specific I2C execution
        // Example: HAL_I2C_Master_Transmit() for STM32
        return true;
    }
    
public:
    // Emergency flush - execute critical transactions immediately
    void flushCritical() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        std::vector<std::pair<I2CPriority, std::shared_ptr<I2CTransaction>>> critical_trans;
        
        // Extract critical transactions
        while (!transaction_queue.empty()) {
            auto item = transaction_queue.top();
            if (item.first == I2CPriority::Critical) {
                critical_trans.push_back(item);
            }
            transaction_queue.pop();
        }
        
        // Execute critical transactions immediately
        for (auto& [priority, trans] : critical_trans) {
            executeTransaction(trans);
        }
    }
};

// Usage example
void example_usage() {
    I2CPriorityScheduler scheduler;
    scheduler.start();
    
    // Critical sensor read
    auto critical_trans = std::make_shared<I2CTransaction>(0x50, std::vector<uint8_t>{0x00}, true);
    critical_trans->callback = [](bool success) {
        if (success) {
            // Handle critical data
        }
    };
    scheduler.enqueue(critical_trans, I2CPriority::Critical);
    
    // Normal operation
    auto normal_trans = std::make_shared<I2CTransaction>(0x51, std::vector<uint8_t>{0x01, 0x02});
    scheduler.enqueue(normal_trans, I2CPriority::Normal);
}
```

### 3. Rust Implementation with Async/Await

**Rust Implementation:**

```rust
use std::cmp::Ordering;
use std::collections::BinaryHeap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::Notify;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum I2CPriority {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3,
}

struct I2CTransaction {
    device_addr: u8,
    data: Vec<u8>,
    is_read: bool,
    deadline: Instant,
}

// Wrapper for priority queue ordering
struct PriorityTransaction {
    priority: I2CPriority,
    transaction: I2CTransaction,
}

impl PartialEq for PriorityTransaction {
    fn eq(&self, other: &Self) -> bool {
        self.priority == other.priority && 
        self.transaction.deadline == other.transaction.deadline
    }
}

impl Eq for PriorityTransaction {}

impl PartialOrd for PriorityTransaction {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for PriorityTransaction {
    fn cmp(&self, other: &Self) -> Ordering {
        // Reverse ordering for priority (lower value = higher priority)
        match other.priority.cmp(&self.priority) {
            Ordering::Equal => {
                // Earlier deadline has higher priority
                other.transaction.deadline.cmp(&self.transaction.deadline)
            }
            ord => ord,
        }
    }
}

pub struct I2CPriorityScheduler {
    queue: Arc<Mutex<BinaryHeap<PriorityTransaction>>>,
    notify: Arc<Notify>,
    running: Arc<Mutex<bool>>,
}

impl I2CPriorityScheduler {
    pub fn new() -> Self {
        Self {
            queue: Arc::new(Mutex::new(BinaryHeap::new())),
            notify: Arc::new(Notify::new()),
            running: Arc::new(Mutex::new(false)),
        }
    }
    
    pub fn enqueue(&self, transaction: I2CTransaction, priority: I2CPriority) {
        let mut queue = self.queue.lock().unwrap();
        queue.push(PriorityTransaction {
            priority,
            transaction,
        });
        self.notify.notify_one();
    }
    
    pub async fn start(&self) {
        *self.running.lock().unwrap() = true;
        
        let queue = Arc::clone(&self.queue);
        let notify = Arc::clone(&self.notify);
        let running = Arc::clone(&self.running);
        
        tokio::spawn(async move {
            while *running.lock().unwrap() {
                // Wait for notification
                notify.notified().await;
                
                // Process all available transactions
                loop {
                    let transaction = {
                        let mut queue = queue.lock().unwrap();
                        queue.pop()
                    };
                    
                    match transaction {
                        Some(priority_trans) => {
                            // Check deadline
                            if Instant::now() > priority_trans.transaction.deadline {
                                eprintln!("Transaction missed deadline!");
                                continue;
                            }
                            
                            // Execute transaction
                            Self::execute_transaction(&priority_trans.transaction).await;
                        }
                        None => break,
                    }
                }
            }
        });
    }
    
    pub fn stop(&self) {
        *self.running.lock().unwrap() = false;
        self.notify.notify_one();
    }
    
    async fn execute_transaction(trans: &I2CTransaction) {
        // Platform-specific I2C execution
        // This would use embedded-hal traits or platform-specific drivers
        println!("Executing I2C transaction to device 0x{:02X}", trans.device_addr);
        
        // Simulate I2C operation
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    
    // Emergency function to process only critical transactions
    pub async fn flush_critical(&self) {
        let critical_transactions: Vec<I2CTransaction> = {
            let mut queue = self.queue.lock().unwrap();
            let mut temp_heap = BinaryHeap::new();
            let mut critical = Vec::new();
            
            // Extract and separate critical transactions
            while let Some(item) = queue.pop() {
                if item.priority == I2CPriority::Critical {
                    critical.push(item.transaction);
                } else {
                    temp_heap.push(item);
                }
            }
            
            // Restore non-critical transactions
            *queue = temp_heap;
            critical
        };
        
        // Execute critical transactions immediately
        for trans in critical_transactions {
            Self::execute_transaction(&trans).await;
        }
    }
}

// Usage example
#[tokio::main]
async fn main() {
    let scheduler = I2CPriorityScheduler::new();
    scheduler.start().await;
    
    // Critical sensor read
    let critical_trans = I2CTransaction {
        device_addr: 0x50,
        data: vec![0x00],
        is_read: true,
        deadline: Instant::now() + Duration::from_millis(50),
    };
    scheduler.enqueue(critical_trans, I2CPriority::Critical);
    
    // Normal operation
    let normal_trans = I2CTransaction {
        device_addr: 0x51,
        data: vec![0x01, 0x02],
        is_read: false,
        deadline: Instant::now() + Duration::from_millis(200),
    };
    scheduler.enqueue(normal_trans, I2CPriority::Normal);
    
    // Let scheduler run
    tokio::time::sleep(Duration::from_secs(1)).await;
    
    scheduler.stop();
}
```

## Best Practices

1. **Define Clear Priority Levels** - Establish what constitutes critical vs normal traffic
2. **Set Realistic Deadlines** - Account for worst-case I2C transaction times
3. **Monitor Queue Depths** - Prevent starvation of low-priority tasks
4. **Implement Aging** - Gradually increase priority of waiting transactions
5. **Handle Deadline Misses** - Log and respond appropriately to timing failures
6. **Test Under Load** - Verify priority handling under high transaction volumes
7. **Consider Bus Arbitration** - In multi-master systems, hardware arbitration adds complexity

Priority handling transforms I2C from a simple sequential bus into a responsive, deterministic communication system suitable for real-time and safety-critical applications.