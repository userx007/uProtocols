# Safety Time Monitoring in Profibus

## Overview

Safety Time Monitoring is a critical safety mechanism in Profibus systems that ensures timely communication and response in safety-critical applications. It implements watchdog timers and timeout detection to identify communication failures, device malfunctions, or processing delays that could compromise system safety.

## Core Concepts

### 1. **Watchdog Timers**
Watchdog timers are countdown timers that must be periodically reset by the monitored system. If the timer expires without being reset, it indicates a fault condition (frozen process, communication failure, or system hang).

### 2. **Timeout Detection**
Timeout detection monitors the time between expected events (message exchanges, data updates, acknowledgments). If the expected event doesn't occur within the specified time window, a timeout fault is raised.

### 3. **Safety Time Parameters**

- **T_SDR (Safety Data Rate)**: Maximum time between safety data transmissions
- **T_WD (Watchdog Time)**: Maximum time before watchdog triggers
- **T_SAFE_OUT**: Time to enter safe state after fault detection
- **T_MONITORING**: Overall monitoring cycle time

### 4. **Safety Integrity Levels (SIL)**
Safety time monitoring must be implemented according to the required SIL level (SIL 1-3 in Profibus safety applications), with stricter timing requirements for higher SIL levels.

## Programming Implementation

### C/C++ Implementation

```c
// profibus_safety_monitor.h
#ifndef PROFIBUS_SAFETY_MONITOR_H
#define PROFIBUS_SAFETY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Safety time configuration structure
typedef struct {
    uint32_t watchdog_timeout_ms;      // Watchdog timeout in milliseconds
    uint32_t communication_timeout_ms; // Communication timeout
    uint32_t safe_state_delay_ms;      // Delay before entering safe state
    uint32_t monitoring_cycle_ms;      // Monitoring cycle time
} SafetyTimeConfig;

// Safety monitoring state
typedef enum {
    SAFETY_STATE_INIT,
    SAFETY_STATE_NORMAL,
    SAFETY_STATE_WARNING,
    SAFETY_STATE_FAULT,
    SAFETY_STATE_SAFE
} SafetyState;

// Watchdog timer structure
typedef struct {
    struct timespec last_reset;
    uint32_t timeout_ms;
    bool is_expired;
    uint32_t expiration_count;
} WatchdogTimer;

// Safety monitor context
typedef struct {
    SafetyTimeConfig config;
    SafetyState current_state;
    WatchdogTimer watchdog;
    struct timespec last_communication;
    struct timespec last_safety_data;
    bool communication_valid;
    uint32_t timeout_counter;
} SafetyMonitor;

// Function prototypes
void safety_monitor_init(SafetyMonitor* monitor, const SafetyTimeConfig* config);
void watchdog_reset(WatchdogTimer* watchdog);
bool watchdog_check(WatchdogTimer* watchdog);
void safety_monitor_update(SafetyMonitor* monitor);
void safety_handle_timeout(SafetyMonitor* monitor);
void safety_enter_safe_state(SafetyMonitor* monitor);
uint64_t get_time_ms(void);
uint64_t get_elapsed_time_ms(struct timespec* start);

#endif // PROFIBUS_SAFETY_MONITOR_H
```

```c
// profibus_safety_monitor.c
#include "profibus_safety_monitor.h"
#include <stdio.h>
#include <string.h>

// Get current time in milliseconds
uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Calculate elapsed time from a start point
uint64_t get_elapsed_time_ms(struct timespec* start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    uint64_t start_ms = start->tv_sec * 1000 + start->tv_nsec / 1000000;
    uint64_t now_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    
    return now_ms - start_ms;
}

// Initialize safety monitor
void safety_monitor_init(SafetyMonitor* monitor, const SafetyTimeConfig* config) {
    memset(monitor, 0, sizeof(SafetyMonitor));
    monitor->config = *config;
    monitor->current_state = SAFETY_STATE_INIT;
    
    // Initialize watchdog
    monitor->watchdog.timeout_ms = config->watchdog_timeout_ms;
    clock_gettime(CLOCK_MONOTONIC, &monitor->watchdog.last_reset);
    monitor->watchdog.is_expired = false;
    monitor->watchdog.expiration_count = 0;
    
    // Initialize timestamps
    clock_gettime(CLOCK_MONOTONIC, &monitor->last_communication);
    clock_gettime(CLOCK_MONOTONIC, &monitor->last_safety_data);
    
    monitor->communication_valid = false;
    monitor->timeout_counter = 0;
    
    printf("[SAFETY] Monitor initialized with:\n");
    printf("  Watchdog timeout: %u ms\n", config->watchdog_timeout_ms);
    printf("  Communication timeout: %u ms\n", config->communication_timeout_ms);
    printf("  Monitoring cycle: %u ms\n", config->monitoring_cycle_ms);
}

// Reset watchdog timer
void watchdog_reset(WatchdogTimer* watchdog) {
    clock_gettime(CLOCK_MONOTONIC, &watchdog->last_reset);
    watchdog->is_expired = false;
}

// Check watchdog timer status
bool watchdog_check(WatchdogTimer* watchdog) {
    uint64_t elapsed = get_elapsed_time_ms(&watchdog->last_reset);
    
    if (elapsed > watchdog->timeout_ms) {
        if (!watchdog->is_expired) {
            watchdog->is_expired = true;
            watchdog->expiration_count++;
            printf("[WATCHDOG] TIMEOUT! Elapsed: %lu ms, Limit: %u ms\n",
                   elapsed, watchdog->timeout_ms);
        }
        return true; // Timeout occurred
    }
    
    return false; // Normal operation
}

// Update safety monitoring state
void safety_monitor_update(SafetyMonitor* monitor) {
    // Check watchdog
    if (watchdog_check(&monitor->watchdog)) {
        if (monitor->current_state != SAFETY_STATE_FAULT) {
            printf("[SAFETY] Watchdog timeout detected!\n");
            monitor->current_state = SAFETY_STATE_FAULT;
            safety_handle_timeout(monitor);
        }
        return;
    }
    
    // Check communication timeout
    uint64_t comm_elapsed = get_elapsed_time_ms(&monitor->last_communication);
    if (comm_elapsed > monitor->config.communication_timeout_ms) {
        if (monitor->current_state != SAFETY_STATE_FAULT) {
            printf("[SAFETY] Communication timeout! Elapsed: %lu ms\n", comm_elapsed);
            monitor->current_state = SAFETY_STATE_FAULT;
            safety_handle_timeout(monitor);
        }
        return;
    }
    
    // Check safety data timeout
    uint64_t data_elapsed = get_elapsed_time_ms(&monitor->last_safety_data);
    if (data_elapsed > monitor->config.monitoring_cycle_ms * 2) {
        if (monitor->current_state == SAFETY_STATE_NORMAL) {
            printf("[SAFETY] Safety data timeout warning! Elapsed: %lu ms\n", data_elapsed);
            monitor->current_state = SAFETY_STATE_WARNING;
        }
    } else {
        // Normal operation
        if (monitor->current_state == SAFETY_STATE_WARNING) {
            monitor->current_state = SAFETY_STATE_NORMAL;
            printf("[SAFETY] Returned to normal operation\n");
        }
    }
}

// Handle timeout condition
void safety_handle_timeout(SafetyMonitor* monitor) {
    monitor->timeout_counter++;
    
    printf("[SAFETY] Timeout #%u detected\n", monitor->timeout_counter);
    
    // Trigger safe state after configured delay
    struct timespec delay_start;
    clock_gettime(CLOCK_MONOTONIC, &delay_start);
    
    uint64_t elapsed = 0;
    while (elapsed < monitor->config.safe_state_delay_ms) {
        elapsed = get_elapsed_time_ms(&delay_start);
        // Could check for recovery here
    }
    
    safety_enter_safe_state(monitor);
}

// Enter safe state
void safety_enter_safe_state(SafetyMonitor* monitor) {
    monitor->current_state = SAFETY_STATE_SAFE;
    
    printf("[SAFETY] *** ENTERING SAFE STATE ***\n");
    printf("[SAFETY] All outputs disabled\n");
    printf("[SAFETY] System requires manual reset\n");
    
    // In real implementation:
    // - Disable all safety-critical outputs
    // - Activate safe state relays
    // - Log the event
    // - Notify operators
    // - Require manual acknowledgment to reset
}

// Simulate receiving safety data
void safety_data_received(SafetyMonitor* monitor, const uint8_t* data, size_t len) {
    clock_gettime(CLOCK_MONOTONIC, &monitor->last_safety_data);
    clock_gettime(CLOCK_MONOTONIC, &monitor->last_communication);
    monitor->communication_valid = true;
    
    // Reset watchdog on valid data
    watchdog_reset(&monitor->watchdog);
}
```

```cpp
// profibus_safety_monitor.cpp (C++ version with additional features)
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>

class ProfibusWatchdog {
private:
    std::chrono::steady_clock::time_point last_reset_;
    std::chrono::milliseconds timeout_;
    std::atomic<bool> expired_{false};
    std::atomic<uint32_t> expiration_count_{0};
    std::function<void()> timeout_callback_;

public:
    ProfibusWatchdog(std::chrono::milliseconds timeout, 
                     std::function<void()> callback = nullptr)
        : timeout_(timeout), timeout_callback_(callback) {
        reset();
    }
    
    void reset() {
        last_reset_ = std::chrono::steady_clock::now();
        expired_ = false;
    }
    
    bool check() {
        auto elapsed = std::chrono::steady_clock::now() - last_reset_;
        
        if (elapsed > timeout_) {
            if (!expired_.exchange(true)) {
                expiration_count_++;
                std::cout << "[WATCHDOG] TIMEOUT after " 
                          << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                          << " ms (limit: " << timeout_.count() << " ms)" << std::endl;
                
                if (timeout_callback_) {
                    timeout_callback_();
                }
            }
            return true;
        }
        return false;
    }
    
    uint32_t getExpirationCount() const { return expiration_count_; }
    bool isExpired() const { return expired_; }
};

class SafetyTimeMonitor {
public:
    enum class State {
        INIT,
        NORMAL,
        WARNING,
        FAULT,
        SAFE
    };

private:
    ProfibusWatchdog watchdog_;
    std::chrono::steady_clock::time_point last_communication_;
    std::chrono::steady_clock::time_point last_safety_data_;
    std::chrono::milliseconds comm_timeout_;
    std::chrono::milliseconds cycle_time_;
    std::atomic<State> state_{State::INIT};
    std::atomic<bool> running_{false};
    
public:
    SafetyTimeMonitor(std::chrono::milliseconds watchdog_timeout,
                      std::chrono::milliseconds comm_timeout,
                      std::chrono::milliseconds cycle_time)
        : watchdog_(watchdog_timeout, [this]() { handleWatchdogTimeout(); }),
          comm_timeout_(comm_timeout),
          cycle_time_(cycle_time) {
        
        last_communication_ = std::chrono::steady_clock::now();
        last_safety_data_ = std::chrono::steady_clock::now();
        state_ = State::NORMAL;
    }
    
    void update() {
        // Check watchdog
        if (watchdog_.check()) {
            if (state_ != State::FAULT) {
                state_ = State::FAULT;
            }
            return;
        }
        
        // Check communication timeout
        auto comm_elapsed = std::chrono::steady_clock::now() - last_communication_;
        if (comm_elapsed > comm_timeout_) {
            std::cout << "[SAFETY] Communication timeout!" << std::endl;
            enterSafeState();
            return;
        }
        
        // Check cycle time
        auto data_elapsed = std::chrono::steady_clock::now() - last_safety_data_;
        if (data_elapsed > cycle_time_ * 2) {
            if (state_ == State::NORMAL) {
                state_ = State::WARNING;
                std::cout << "[SAFETY] Warning: Safety data delayed" << std::endl;
            }
        } else if (state_ == State::WARNING) {
            state_ = State::NORMAL;
        }
    }
    
    void receiveSafetyData() {
        last_safety_data_ = std::chrono::steady_clock::now();
        last_communication_ = std::chrono::steady_clock::now();
        watchdog_.reset();
    }
    
    void handleWatchdogTimeout() {
        std::cout << "[SAFETY] Watchdog timeout handler triggered" << std::endl;
        enterSafeState();
    }
    
    void enterSafeState() {
        state_ = State::SAFE;
        std::cout << "\n*** SAFETY STATE ACTIVATED ***" << std::endl;
        std::cout << "All safety-critical operations halted" << std::endl;
        // Disable outputs, activate safety relays, etc.
    }
    
    State getState() const { return state_; }
};
```

### Rust Implementation

```rust
// profibus_safety_monitor.rs
use std::time::{Duration, Instant};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SafetyState {
    Init,
    Normal,
    Warning,
    Fault,
    Safe,
}

#[derive(Debug, Clone)]
pub struct SafetyTimeConfig {
    pub watchdog_timeout: Duration,
    pub communication_timeout: Duration,
    pub safe_state_delay: Duration,
    pub monitoring_cycle: Duration,
}

impl Default for SafetyTimeConfig {
    fn default() -> Self {
        Self {
            watchdog_timeout: Duration::from_millis(100),
            communication_timeout: Duration::from_millis(500),
            safe_state_delay: Duration::from_millis(50),
            monitoring_cycle: Duration::from_millis(10),
        }
    }
}

pub struct WatchdogTimer {
    last_reset: Instant,
    timeout: Duration,
    expired: AtomicBool,
    expiration_count: AtomicU32,
    callback: Option<Box<dyn Fn() + Send + Sync>>,
}

impl WatchdogTimer {
    pub fn new(timeout: Duration) -> Self {
        Self {
            last_reset: Instant::now(),
            timeout,
            expired: AtomicBool::new(false),
            expiration_count: AtomicU32::new(0),
            callback: None,
        }
    }
    
    pub fn with_callback<F>(timeout: Duration, callback: F) -> Self
    where
        F: Fn() + Send + Sync + 'static,
    {
        Self {
            last_reset: Instant::now(),
            timeout,
            expired: AtomicBool::new(false),
            expiration_count: AtomicU32::new(0),
            callback: Some(Box::new(callback)),
        }
    }
    
    pub fn reset(&mut self) {
        self.last_reset = Instant::now();
        self.expired.store(false, Ordering::SeqCst);
    }
    
    pub fn check(&self) -> bool {
        let elapsed = self.last_reset.elapsed();
        
        if elapsed > self.timeout {
            if !self.expired.swap(true, Ordering::SeqCst) {
                let count = self.expiration_count.fetch_add(1, Ordering::SeqCst) + 1;
                println!(
                    "[WATCHDOG] TIMEOUT #{} after {:?} (limit: {:?})",
                    count, elapsed, self.timeout
                );
                
                if let Some(ref callback) = self.callback {
                    callback();
                }
            }
            return true;
        }
        
        false
    }
    
    pub fn is_expired(&self) -> bool {
        self.expired.load(Ordering::SeqCst)
    }
    
    pub fn expiration_count(&self) -> u32 {
        self.expiration_count.load(Ordering::SeqCst)
    }
}

pub struct SafetyMonitor {
    config: SafetyTimeConfig,
    state: SafetyState,
    watchdog: WatchdogTimer,
    last_communication: Instant,
    last_safety_data: Instant,
    timeout_counter: u32,
}

impl SafetyMonitor {
    pub fn new(config: SafetyTimeConfig) -> Self {
        println!("[SAFETY] Initializing monitor:");
        println!("  Watchdog timeout: {:?}", config.watchdog_timeout);
        println!("  Communication timeout: {:?}", config.communication_timeout);
        println!("  Monitoring cycle: {:?}", config.monitoring_cycle);
        
        let watchdog = WatchdogTimer::new(config.watchdog_timeout);
        
        Self {
            config,
            state: SafetyState::Init,
            watchdog,
            last_communication: Instant::now(),
            last_safety_data: Instant::now(),
            timeout_counter: 0,
        }
    }
    
    pub fn update(&mut self) {
        // Check watchdog
        if self.watchdog.check() {
            if self.state != SafetyState::Fault {
                println!("[SAFETY] Watchdog timeout detected!");
                self.state = SafetyState::Fault;
                self.handle_timeout();
            }
            return;
        }
        
        // Check communication timeout
        let comm_elapsed = self.last_communication.elapsed();
        if comm_elapsed > self.config.communication_timeout {
            if self.state != SafetyState::Fault {
                println!("[SAFETY] Communication timeout! Elapsed: {:?}", comm_elapsed);
                self.state = SafetyState::Fault;
                self.handle_timeout();
            }
            return;
        }
        
        // Check safety data timeout
        let data_elapsed = self.last_safety_data.elapsed();
        if data_elapsed > self.config.monitoring_cycle * 2 {
            if self.state == SafetyState::Normal {
                println!("[SAFETY] Safety data timeout warning! Elapsed: {:?}", data_elapsed);
                self.state = SafetyState::Warning;
            }
        } else if self.state == SafetyState::Warning {
            self.state = SafetyState::Normal;
            println!("[SAFETY] Returned to normal operation");
        }
    }
    
    pub fn receive_safety_data(&mut self, _data: &[u8]) {
        self.last_safety_data = Instant::now();
        self.last_communication = Instant::now();
        self.watchdog.reset();
        
        if self.state == SafetyState::Init {
            self.state = SafetyState::Normal;
            println!("[SAFETY] Transitioned to NORMAL state");
        }
    }
    
    fn handle_timeout(&mut self) {
        self.timeout_counter += 1;
        println!("[SAFETY] Timeout #{} detected", self.timeout_counter);
        
        // Wait for safe state delay
        std::thread::sleep(self.config.safe_state_delay);
        
        self.enter_safe_state();
    }
    
    fn enter_safe_state(&mut self) {
        self.state = SafetyState::Safe;
        
        println!("\n*** ENTERING SAFE STATE ***");
        println!("[SAFETY] All outputs disabled");
        println!("[SAFETY] System requires manual reset");
        
        // In production:
        // - Disable all safety-critical outputs
        // - Activate safety relays
        // - Log event to persistent storage
        // - Trigger alarms
        // - Require operator acknowledgment
    }
    
    pub fn get_state(&self) -> SafetyState {
        self.state
    }
    
    pub fn reset(&mut self) -> Result<(), String> {
        if self.state != SafetyState::Safe {
            return Err("Can only reset from SAFE state".to_string());
        }
        
        println!("[SAFETY] Resetting monitor...");
        self.state = SafetyState::Init;
        self.watchdog.reset();
        self.last_communication = Instant::now();
        self.last_safety_data = Instant::now();
        self.timeout_counter = 0;
        
        Ok(())
    }
}

// Example usage with multi-threaded monitoring
pub struct SafetyMonitorThread {
    monitor: Arc<std::sync::Mutex<SafetyMonitor>>,
    running: Arc<AtomicBool>,
}

impl SafetyMonitorThread {
    pub fn new(config: SafetyTimeConfig) -> Self {
        Self {
            monitor: Arc::new(std::sync::Mutex::new(SafetyMonitor::new(config))),
            running: Arc::new(AtomicBool::new(false)),
        }
    }
    
    pub fn start(&self) {
        self.running.store(true, Ordering::SeqCst);
        
        let monitor = Arc::clone(&self.monitor);
        let running = Arc::clone(&self.running);
        
        std::thread::spawn(move || {
            while running.load(Ordering::SeqCst) {
                if let Ok(mut mon) = monitor.lock() {
                    mon.update();
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        });
    }
    
    pub fn stop(&self) {
        self.running.store(false, Ordering::SeqCst);
    }
    
    pub fn receive_data(&self, data: &[u8]) {
        if let Ok(mut monitor) = self.monitor.lock() {
            monitor.receive_safety_data(data);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_watchdog_normal_operation() {
        let mut watchdog = WatchdogTimer::new(Duration::from_millis(100));
        
        for _ in 0..5 {
            std::thread::sleep(Duration::from_millis(50));
            watchdog.reset();
            assert!(!watchdog.check());
        }
    }
    
    #[test]
    fn test_watchdog_timeout() {
        let watchdog = WatchdogTimer::new(Duration::from_millis(50));
        std::thread::sleep(Duration::from_millis(100));
        
        assert!(watchdog.check());
        assert_eq!(watchdog.expiration_count(), 1);
    }
    
    #[test]
    fn test_safety_monitor_normal() {
        let config = SafetyTimeConfig::default();
        let mut monitor = SafetyMonitor::new(config);
        
        for _ in 0..10 {
            monitor.receive_safety_data(&[0x01, 0x02]);
            std::thread::sleep(Duration::from_millis(5));
            monitor.update();
            assert_eq!(monitor.get_state(), SafetyState::Normal);
        }
    }
}
```

## Summary

**Safety Time Monitoring** is essential for Profibus safety-critical systems, providing multiple layers of temporal supervision:

### Key Components:
1. **Watchdog Timers** - Detect frozen processes and system hangs
2. **Communication Timeouts** - Monitor message exchange intervals
3. **Cycle Time Monitoring** - Ensure timely data updates
4. **Safe State Transitions** - Controlled shutdown on faults

### Implementation Features:
- Multi-level timeout detection (watchdog, communication, data cycle)
- State machine management (Init → Normal → Warning → Fault → Safe)
- Atomic operations for thread-safe monitoring
- Configurable timing parameters for different SIL levels
- Callback mechanisms for timeout handling
- Automatic safe state entry on critical failures

### Best Practices:
- Set watchdog timeouts at 2-3× the expected cycle time
- Implement redundant monitoring mechanisms
- Log all timeout events for diagnostics
- Require manual reset after safe state entry
- Test timeout scenarios thoroughly
- Use hardware watchdogs as backup to software timers

Safety time monitoring ensures that even in the event of communication failures, processing delays, or system malfunctions, the system transitions to a safe state within deterministic time bounds, preventing hazardous conditions in industrial automation environments.