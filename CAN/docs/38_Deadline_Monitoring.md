# CAN Deadline Monitoring

## Overview

Deadline monitoring in CAN (Controller Area Network) systems is a critical safety and reliability feature that ensures time-critical messages are transmitted and received within their specified time constraints. This involves implementing watchdog timers and timeout mechanisms to detect when expected messages fail to arrive within their deadlines.

## Fundamental Concepts

### Why Deadline Monitoring Matters

In automotive and industrial control systems, many CAN messages are time-critical:
- **Periodic messages** must arrive at regular intervals (e.g., sensor data every 10ms)
- **Event-driven messages** must arrive within a maximum latency after an event
- **Acknowledgment messages** must arrive within timeout periods
- **Safety-critical messages** require guaranteed delivery times

Missing deadlines can indicate:
- Network failures or overload
- ECU malfunction
- Cable damage or electromagnetic interference
- Software bugs in transmission logic

### Monitoring Strategies

**1. Reception Deadline Monitoring**
- Track when messages were last received
- Trigger alerts when expected messages don't arrive

**2. Transmission Deadline Monitoring**
- Ensure outgoing messages are sent within time budgets
- Detect transmission failures or bus arbitration delays

**3. End-to-End Timing**
- Monitor total latency from sender to receiver
- Account for network delays and processing time

## Implementation in C/C++

### Basic Reception Deadline Monitor

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_MONITORED_MESSAGES 32

typedef struct {
    uint32_t can_id;
    uint32_t deadline_ms;        // Maximum time between messages
    uint64_t last_received_ms;   // Timestamp of last reception
    bool is_active;              // Monitor enabled
    bool deadline_missed;        // Current status
    uint32_t miss_count;         // Total deadline violations
} can_deadline_monitor_t;

typedef struct {
    can_deadline_monitor_t monitors[MAX_MONITORED_MESSAGES];
    uint32_t monitor_count;
} can_deadline_system_t;

// Initialize the deadline monitoring system
void can_deadline_init(can_deadline_system_t* system) {
    system->monitor_count = 0;
    for (int i = 0; i < MAX_MONITORED_MESSAGES; i++) {
        system->monitors[i].is_active = false;
        system->monitors[i].deadline_missed = false;
        system->monitors[i].miss_count = 0;
    }
}

// Get current timestamp in milliseconds
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

// Register a message for deadline monitoring
int can_deadline_register(can_deadline_system_t* system,
                          uint32_t can_id,
                          uint32_t deadline_ms) {
    if (system->monitor_count >= MAX_MONITORED_MESSAGES) {
        return -1; // No space available
    }
    
    can_deadline_monitor_t* monitor = &system->monitors[system->monitor_count];
    monitor->can_id = can_id;
    monitor->deadline_ms = deadline_ms;
    monitor->last_received_ms = get_timestamp_ms();
    monitor->is_active = true;
    monitor->deadline_missed = false;
    monitor->miss_count = 0;
    
    system->monitor_count++;
    return 0;
}

// Update monitor when message is received
void can_deadline_message_received(can_deadline_system_t* system,
                                   uint32_t can_id) {
    uint64_t now = get_timestamp_ms();
    
    for (uint32_t i = 0; i < system->monitor_count; i++) {
        can_deadline_monitor_t* monitor = &system->monitors[i];
        
        if (monitor->is_active && monitor->can_id == can_id) {
            monitor->last_received_ms = now;
            monitor->deadline_missed = false;
            break;
        }
    }
}

// Check all monitors for deadline violations
void can_deadline_check(can_deadline_system_t* system) {
    uint64_t now = get_timestamp_ms();
    
    for (uint32_t i = 0; i < system->monitor_count; i++) {
        can_deadline_monitor_t* monitor = &system->monitors[i];
        
        if (!monitor->is_active) {
            continue;
        }
        
        uint64_t elapsed = now - monitor->last_received_ms;
        
        if (elapsed > monitor->deadline_ms) {
            if (!monitor->deadline_missed) {
                monitor->deadline_missed = true;
                monitor->miss_count++;
                
                // Callback or logging
                printf("DEADLINE MISS: CAN ID 0x%03X, elapsed %llu ms, "
                       "deadline %u ms, total misses %u\n",
                       monitor->can_id, elapsed, monitor->deadline_ms,
                       monitor->miss_count);
            }
        }
    }
}
```

### Advanced C++ Deadline Monitor with Callbacks

```cpp
#include <iostream>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <memory>

class CANDeadlineMonitor {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;
    using DeadlineCallback = std::function<void(uint32_t can_id, 
                                                 Duration elapsed)>;
    
    struct MonitorConfig {
        uint32_t can_id;
        Duration deadline;
        DeadlineCallback on_deadline_miss;
        DeadlineCallback on_deadline_met;  // Optional: recovery callback
        bool enabled = true;
    };
    
    struct MonitorState {
        MonitorConfig config;
        TimePoint last_received;
        bool currently_violated = false;
        uint32_t total_violations = 0;
        uint32_t consecutive_violations = 0;
    };
    
private:
    std::unordered_map<uint32_t, MonitorState> monitors_;
    bool running_ = false;
    
    TimePoint now() const {
        return std::chrono::steady_clock::now();
    }
    
public:
    // Register a message for monitoring
    void registerMessage(const MonitorConfig& config) {
        MonitorState state;
        state.config = config;
        state.last_received = now();
        monitors_[config.can_id] = state;
    }
    
    // Update when message received
    void messageReceived(uint32_t can_id) {
        auto it = monitors_.find(can_id);
        if (it == monitors_.end()) {
            return;
        }
        
        MonitorState& state = it->second;
        TimePoint received_time = now();
        
        // Check if this ends a violation period
        if (state.currently_violated && state.config.on_deadline_met) {
            auto recovery_time = std::chrono::duration_cast<Duration>(
                received_time - state.last_received);
            state.config.on_deadline_met(can_id, recovery_time);
        }
        
        state.last_received = received_time;
        state.currently_violated = false;
        state.consecutive_violations = 0;
    }
    
    // Check all monitors
    void checkDeadlines() {
        TimePoint current_time = now();
        
        for (auto& [can_id, state] : monitors_) {
            if (!state.config.enabled) {
                continue;
            }
            
            auto elapsed = std::chrono::duration_cast<Duration>(
                current_time - state.last_received);
            
            if (elapsed > state.config.deadline) {
                if (!state.currently_violated) {
                    // New violation
                    state.currently_violated = true;
                    state.total_violations++;
                    state.consecutive_violations = 1;
                    
                    if (state.config.on_deadline_miss) {
                        state.config.on_deadline_miss(can_id, elapsed);
                    }
                } else {
                    // Continuing violation
                    state.consecutive_violations++;
                }
            }
        }
    }
    
    // Get statistics for a message
    struct Statistics {
        uint32_t total_violations;
        uint32_t consecutive_violations;
        bool currently_violated;
        Duration time_since_last_message;
    };
    
    std::optional<Statistics> getStatistics(uint32_t can_id) const {
        auto it = monitors_.find(can_id);
        if (it == monitors_.end()) {
            return std::nullopt;
        }
        
        const MonitorState& state = it->second;
        auto elapsed = std::chrono::duration_cast<Duration>(
            now() - state.last_received);
        
        return Statistics{
            state.total_violations,
            state.consecutive_violations,
            state.currently_violated,
            elapsed
        };
    }
    
    // Enable/disable monitoring for specific message
    void setEnabled(uint32_t can_id, bool enabled) {
        auto it = monitors_.find(can_id);
        if (it != monitors_.end()) {
            it->second.config.enabled = enabled;
        }
    }
};

// Example usage
void example_usage() {
    CANDeadlineMonitor monitor;
    
    // Monitor heartbeat message (expected every 100ms)
    monitor.registerMessage({
        .can_id = 0x100,
        .deadline = std::chrono::milliseconds(150),
        .on_deadline_miss = [](uint32_t id, auto elapsed) {
            std::cout << "CRITICAL: Heartbeat 0x" << std::hex << id 
                      << " missed! Elapsed: " << elapsed.count() 
                      << "ms" << std::endl;
        },
        .on_deadline_met = [](uint32_t id, auto recovery) {
            std::cout << "INFO: Heartbeat 0x" << std::hex << id 
                      << " recovered after " << recovery.count() 
                      << "ms" << std::endl;
        }
    });
    
    // Monitor sensor data (expected every 20ms)
    monitor.registerMessage({
        .can_id = 0x200,
        .deadline = std::chrono::milliseconds(50),
        .on_deadline_miss = [](uint32_t id, auto elapsed) {
            std::cout << "WARNING: Sensor 0x" << std::hex << id 
                      << " timeout: " << elapsed.count() 
                      << "ms" << std::endl;
        }
    });
    
    // Simulation loop
    monitor.messageReceived(0x100);
    monitor.messageReceived(0x200);
    
    // Check periodically (e.g., every 10ms)
    monitor.checkDeadlines();
}
```

### Transmission Deadline Monitor

```cpp
#include <queue>
#include <mutex>

class CANTransmissionMonitor {
public:
    struct TransmitRequest {
        uint32_t can_id;
        uint8_t data[8];
        uint8_t dlc;
        TimePoint submit_time;
        Duration max_latency;
        std::function<void(bool success, Duration actual_latency)> callback;
    };
    
private:
    std::queue<TransmitRequest> pending_queue_;
    std::unordered_map<uint32_t, TransmitRequest> in_flight_;
    std::mutex mutex_;
    
public:
    // Submit message for transmission with deadline
    void submitMessage(uint32_t can_id, const uint8_t* data, uint8_t dlc,
                      Duration max_latency,
                      std::function<void(bool, Duration)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        TransmitRequest req;
        req.can_id = can_id;
        std::copy(data, data + dlc, req.data);
        req.dlc = dlc;
        req.submit_time = std::chrono::steady_clock::now();
        req.max_latency = max_latency;
        req.callback = callback;
        
        pending_queue_.push(req);
    }
    
    // Call when message successfully transmitted
    void transmitComplete(uint32_t can_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = in_flight_.find(can_id);
        if (it == in_flight_.end()) {
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<Duration>(
            now - it->second.submit_time);
        
        bool met_deadline = latency <= it->second.max_latency;
        
        if (it->second.callback) {
            it->second.callback(met_deadline, latency);
        }
        
        in_flight_.erase(it);
    }
    
    // Check for transmission timeout
    void checkTransmitDeadlines() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = in_flight_.begin(); it != in_flight_.end();) {
            auto elapsed = std::chrono::duration_cast<Duration>(
                now - it->second.submit_time);
            
            if (elapsed > it->second.max_latency) {
                if (it->second.callback) {
                    it->second.callback(false, elapsed);
                }
                it = in_flight_.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

## Implementation in Rust

### Safe Deadline Monitor with Type Safety

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

#[derive(Debug, Clone)]
pub struct DeadlineConfig {
    pub can_id: u32,
    pub deadline: Duration,
    pub enabled: bool,
}

#[derive(Debug)]
pub struct MonitorState {
    config: DeadlineConfig,
    last_received: Instant,
    currently_violated: bool,
    total_violations: u32,
    consecutive_violations: u32,
}

#[derive(Debug, Clone)]
pub struct Statistics {
    pub total_violations: u32,
    pub consecutive_violations: u32,
    pub currently_violated: bool,
    pub time_since_last: Duration,
}

pub type DeadlineCallback = Box<dyn Fn(u32, Duration) + Send>;

pub struct CANDeadlineMonitor {
    monitors: HashMap<u32, MonitorState>,
    on_deadline_miss: Option<DeadlineCallback>,
    on_deadline_met: Option<DeadlineCallback>,
}

impl CANDeadlineMonitor {
    pub fn new() -> Self {
        Self {
            monitors: HashMap::new(),
            on_deadline_miss: None,
            on_deadline_met: None,
        }
    }
    
    pub fn with_callbacks(
        miss_cb: DeadlineCallback,
        met_cb: Option<DeadlineCallback>,
    ) -> Self {
        Self {
            monitors: HashMap::new(),
            on_deadline_miss: Some(miss_cb),
            on_deadline_met: met_cb,
        }
    }
    
    /// Register a message for deadline monitoring
    pub fn register_message(&mut self, config: DeadlineConfig) {
        let state = MonitorState {
            config,
            last_received: Instant::now(),
            currently_violated: false,
            total_violations: 0,
            consecutive_violations: 0,
        };
        
        self.monitors.insert(state.config.can_id, state);
    }
    
    /// Update when message is received
    pub fn message_received(&mut self, can_id: u32) {
        if let Some(state) = self.monitors.get_mut(&can_id) {
            let received_time = Instant::now();
            
            // Check if this ends a violation period
            if state.currently_violated {
                if let Some(ref callback) = self.on_deadline_met {
                    let recovery_time = received_time - state.last_received;
                    callback(can_id, recovery_time);
                }
            }
            
            state.last_received = received_time;
            state.currently_violated = false;
            state.consecutive_violations = 0;
        }
    }
    
    /// Check all monitors for deadline violations
    pub fn check_deadlines(&mut self) {
        let now = Instant::now();
        
        for (can_id, state) in self.monitors.iter_mut() {
            if !state.config.enabled {
                continue;
            }
            
            let elapsed = now - state.last_received;
            
            if elapsed > state.config.deadline {
                if !state.currently_violated {
                    // New violation
                    state.currently_violated = true;
                    state.total_violations += 1;
                    state.consecutive_violations = 1;
                    
                    if let Some(ref callback) = self.on_deadline_miss {
                        callback(*can_id, elapsed);
                    }
                } else {
                    // Continuing violation
                    state.consecutive_violations += 1;
                }
            }
        }
    }
    
    /// Get statistics for a monitored message
    pub fn get_statistics(&self, can_id: u32) -> Option<Statistics> {
        self.monitors.get(&can_id).map(|state| {
            let elapsed = Instant::now() - state.last_received;
            
            Statistics {
                total_violations: state.total_violations,
                consecutive_violations: state.consecutive_violations,
                currently_violated: state.currently_violated,
                time_since_last: elapsed,
            }
        })
    }
    
    /// Enable or disable monitoring for a specific message
    pub fn set_enabled(&mut self, can_id: u32, enabled: bool) {
        if let Some(state) = self.monitors.get_mut(&can_id) {
            state.config.enabled = enabled;
        }
    }
    
    /// Get list of all currently violated messages
    pub fn get_violations(&self) -> Vec<u32> {
        self.monitors
            .iter()
            .filter(|(_, state)| state.currently_violated)
            .map(|(id, _)| *id)
            .collect()
    }
}

// Example usage
fn example_usage() {
    let mut monitor = CANDeadlineMonitor::with_callbacks(
        Box::new(|can_id, elapsed| {
            eprintln!(
                "DEADLINE MISS: CAN ID 0x{:03X}, elapsed {:?}",
                can_id, elapsed
            );
        }),
        Some(Box::new(|can_id, recovery| {
            println!(
                "RECOVERED: CAN ID 0x{:03X}, recovery time {:?}",
                can_id, recovery
            );
        })),
    );
    
    // Register heartbeat (100ms expected, 150ms deadline)
    monitor.register_message(DeadlineConfig {
        can_id: 0x100,
        deadline: Duration::from_millis(150),
        enabled: true,
    });
    
    // Register sensor data (20ms expected, 50ms deadline)
    monitor.register_message(DeadlineConfig {
        can_id: 0x200,
        deadline: Duration::from_millis(50),
        enabled: true,
    });
    
    // Simulate reception
    monitor.message_received(0x100);
    monitor.message_received(0x200);
    
    // Check deadlines periodically
    monitor.check_deadlines();
    
    // Get statistics
    if let Some(stats) = monitor.get_statistics(0x100) {
        println!("Heartbeat stats: {:?}", stats);
    }
}
```

### Async Deadline Monitor with Tokio

```rust
use tokio::time::{interval, Duration, Instant};
use tokio::sync::mpsc;
use std::collections::HashMap;

#[derive(Debug)]
pub enum MonitorEvent {
    MessageReceived(u32),
    CheckDeadlines,
    GetStats(u32, tokio::sync::oneshot::Sender<Option<Statistics>>),
    Shutdown,
}

pub struct AsyncDeadlineMonitor {
    tx: mpsc::Sender<MonitorEvent>,
}

impl AsyncDeadlineMonitor {
    pub fn new(
        configs: Vec<DeadlineConfig>,
        miss_callback: DeadlineCallback,
    ) -> Self {
        let (tx, rx) = mpsc::channel(100);
        
        tokio::spawn(monitor_task(rx, configs, miss_callback));
        
        Self { tx }
    }
    
    pub async fn message_received(&self, can_id: u32) {
        let _ = self.tx.send(MonitorEvent::MessageReceived(can_id)).await;
    }
    
    pub async fn get_statistics(&self, can_id: u32) -> Option<Statistics> {
        let (resp_tx, resp_rx) = tokio::sync::oneshot::channel();
        
        if self.tx.send(MonitorEvent::GetStats(can_id, resp_tx)).await.is_ok() {
            resp_rx.await.ok().flatten()
        } else {
            None
        }
    }
    
    pub async fn shutdown(self) {
        let _ = self.tx.send(MonitorEvent::Shutdown).await;
    }
}

async fn monitor_task(
    mut rx: mpsc::Receiver<MonitorEvent>,
    configs: Vec<DeadlineConfig>,
    miss_callback: DeadlineCallback,
) {
    let mut monitors: HashMap<u32, MonitorState> = configs
        .into_iter()
        .map(|config| {
            let state = MonitorState {
                config,
                last_received: Instant::now(),
                currently_violated: false,
                total_violations: 0,
                consecutive_violations: 0,
            };
            (state.config.can_id, state)
        })
        .collect();
    
    // Check deadlines every 10ms
    let mut check_interval = interval(Duration::from_millis(10));
    
    loop {
        tokio::select! {
            _ = check_interval.tick() => {
                check_deadlines_internal(&mut monitors, &miss_callback);
            }
            
            Some(event) = rx.recv() => {
                match event {
                    MonitorEvent::MessageReceived(can_id) => {
                        if let Some(state) = monitors.get_mut(&can_id) {
                            state.last_received = Instant::now();
                            state.currently_violated = false;
                            state.consecutive_violations = 0;
                        }
                    }
                    
                    MonitorEvent::CheckDeadlines => {
                        check_deadlines_internal(&mut monitors, &miss_callback);
                    }
                    
                    MonitorEvent::GetStats(can_id, resp_tx) => {
                        let stats = monitors.get(&can_id).map(|state| {
                            let elapsed = Instant::now() - state.last_received;
                            Statistics {
                                total_violations: state.total_violations,
                                consecutive_violations: state.consecutive_violations,
                                currently_violated: state.currently_violated,
                                time_since_last: elapsed,
                            }
                        });
                        let _ = resp_tx.send(stats);
                    }
                    
                    MonitorEvent::Shutdown => break,
                }
            }
        }
    }
}

fn check_deadlines_internal(
    monitors: &mut HashMap<u32, MonitorState>,
    miss_callback: &DeadlineCallback,
) {
    let now = Instant::now();
    
    for (can_id, state) in monitors.iter_mut() {
        if !state.config.enabled {
            continue;
        }
        
        let elapsed = now - state.last_received;
        
        if elapsed > state.config.deadline {
            if !state.currently_violated {
                state.currently_violated = true;
                state.total_violations += 1;
                state.consecutive_violations = 1;
                miss_callback(*can_id, elapsed);
            } else {
                state.consecutive_violations += 1;
            }
        }
    }
}
```

## Summary

**Deadline Monitoring** is essential for ensuring real-time performance and reliability in CAN networks:

### Key Points:
- **Reception monitoring** detects when expected periodic messages fail to arrive within their time constraints
- **Transmission monitoring** ensures outgoing messages meet their latency requirements
- **Watchdog mechanisms** use timestamps and periodic checks to identify deadline violations
- **Callbacks and statistics** enable system-level responses to timing failures

### Implementation Considerations:
- **C/C++ implementations** offer low-level control and minimal overhead for embedded systems
- **Rust implementations** provide memory safety and powerful async capabilities through tokio
- **Configurable deadlines** allow different timing requirements for different message types
- **Violation tracking** helps identify chronic vs. transient timing issues

### Best Practices:
- Set deadlines with margin above expected intervals (e.g., 1.5x the nominal period)
- Implement both immediate callbacks and statistical tracking
- Use different severity levels for different message types
- Consider network load when setting transmission deadlines
- Test deadline monitoring under worst-case network conditions
- Log violations for post-incident analysis and system tuning

Deadline monitoring transforms CAN from a best-effort bus into a predictable real-time communication system suitable for safety-critical applications.