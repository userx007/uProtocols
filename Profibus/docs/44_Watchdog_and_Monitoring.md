# Watchdog and Monitoring in Profibus

## Detailed Description

Watchdog timers are critical safety mechanisms in Profibus communication systems that detect and respond to communication failures, device malfunctions, or system anomalies. These timers act as supervisory circuits that monitor the health of communication channels and connected devices, ensuring system reliability and preventing undefined states in industrial automation environments.

### Core Concepts

**Watchdog Timer Fundamentals:**
A watchdog timer is a hardware or software timer that must be periodically reset (or "kicked") by the monitored system. If the system fails to reset the watchdog within a specified timeout period, the watchdog triggers a recovery action—such as raising an alarm, resetting a device, or switching to a safe state.

**Profibus-Specific Monitoring:**
In Profibus networks, watchdog mechanisms monitor several critical aspects:
- **Cyclic communication integrity**: Ensures master-slave data exchange occurs within expected intervals
- **Device presence**: Detects when devices drop off the network
- **Data freshness**: Validates that received data is current and not stale
- **Bus timing violations**: Identifies timing anomalies that could indicate network degradation

**Implementation Layers:**
1. **Application Layer Watchdog**: Monitors high-level application logic and data processing
2. **Communication Layer Watchdog**: Supervises Profibus protocol exchanges
3. **Hardware Watchdog**: External timer circuits that provide ultimate failsafe protection

### Key Parameters

- **Timeout Period**: The maximum allowable interval between watchdog resets (typically 100ms to several seconds in Profibus)
- **Response Time**: How quickly the system must react when a timeout occurs
- **Recovery Strategy**: Actions taken on timeout (soft reset, hard reset, safe state, alarm only)
- **Monitoring Granularity**: Per-device, per-channel, or system-wide monitoring

### Common Use Cases

1. **Master Station Monitoring**: Detecting when a Profibus master fails to communicate
2. **Slave Device Health**: Identifying unresponsive or malfunctioning slave devices
3. **Cyclic Data Validation**: Ensuring periodic data exchanges meet timing requirements
4. **Network Diagnostics**: Detecting degraded network performance before complete failure
5. **Safety-Critical Systems**: Implementing redundant monitoring for SIL-rated applications

---

## C/C++ Implementation

```c
/*
 * Profibus Watchdog and Monitoring Implementation in C
 * Demonstrates communication failure detection and recovery
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define MAX_DEVICES 32
#define DEFAULT_TIMEOUT_MS 1000
#define MAX_RETRY_COUNT 3

/* Watchdog states */
typedef enum {
    WD_STATE_INACTIVE = 0,
    WD_STATE_ACTIVE,
    WD_STATE_TIMEOUT,
    WD_STATE_FAULT
} watchdog_state_t;

/* Device health status */
typedef enum {
    DEVICE_OK = 0,
    DEVICE_WARNING,
    DEVICE_TIMEOUT,
    DEVICE_OFFLINE
} device_status_t;

/* Watchdog timer structure for individual device */
typedef struct {
    uint8_t device_id;
    uint32_t timeout_ms;
    uint32_t last_response_time;
    uint32_t timeout_count;
    uint32_t retry_count;
    watchdog_state_t state;
    device_status_t status;
    bool enabled;
} profibus_watchdog_t;

/* Monitoring context for entire network */
typedef struct {
    profibus_watchdog_t devices[MAX_DEVICES];
    uint32_t active_device_count;
    uint32_t system_time_ms;
    bool emergency_stop_triggered;
} profibus_monitor_t;

/* Get current system time in milliseconds */
static uint32_t get_system_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

/* Initialize monitoring system */
void profibus_monitor_init(profibus_monitor_t *monitor) {
    memset(monitor, 0, sizeof(profibus_monitor_t));
    monitor->system_time_ms = get_system_time_ms();
    monitor->emergency_stop_triggered = false;
}

/* Configure watchdog for a specific device */
int profibus_watchdog_configure(profibus_monitor_t *monitor, 
                                uint8_t device_id, 
                                uint32_t timeout_ms) {
    if (device_id >= MAX_DEVICES) {
        return -1;
    }
    
    profibus_watchdog_t *wd = &monitor->devices[device_id];
    
    wd->device_id = device_id;
    wd->timeout_ms = (timeout_ms > 0) ? timeout_ms : DEFAULT_TIMEOUT_MS;
    wd->last_response_time = get_system_time_ms();
    wd->timeout_count = 0;
    wd->retry_count = 0;
    wd->state = WD_STATE_ACTIVE;
    wd->status = DEVICE_OK;
    wd->enabled = true;
    
    monitor->active_device_count++;
    
    printf("[WATCHDOG] Device %d configured: timeout=%d ms\n", 
           device_id, timeout_ms);
    
    return 0;
}

/* Kick/reset the watchdog (called when valid communication occurs) */
void profibus_watchdog_kick(profibus_monitor_t *monitor, uint8_t device_id) {
    if (device_id >= MAX_DEVICES) {
        return;
    }
    
    profibus_watchdog_t *wd = &monitor->devices[device_id];
    
    if (!wd->enabled) {
        return;
    }
    
    uint32_t current_time = get_system_time_ms();
    wd->last_response_time = current_time;
    
    /* Reset retry counter on successful communication */
    if (wd->retry_count > 0) {
        printf("[WATCHDOG] Device %d recovered\n", device_id);
    }
    
    wd->retry_count = 0;
    wd->state = WD_STATE_ACTIVE;
    wd->status = DEVICE_OK;
}

/* Handle watchdog timeout event */
static void handle_timeout(profibus_monitor_t *monitor, 
                          profibus_watchdog_t *wd) {
    wd->timeout_count++;
    wd->retry_count++;
    
    printf("[WATCHDOG] Device %d TIMEOUT (count: %d, retry: %d/%d)\n",
           wd->device_id, wd->timeout_count, 
           wd->retry_count, MAX_RETRY_COUNT);
    
    if (wd->retry_count >= MAX_RETRY_COUNT) {
        wd->state = WD_STATE_FAULT;
        wd->status = DEVICE_OFFLINE;
        
        printf("[WATCHDOG] Device %d declared OFFLINE\n", wd->device_id);
        
        /* Trigger emergency stop for critical devices (example: device 0) */
        if (wd->device_id == 0) {
            monitor->emergency_stop_triggered = true;
            printf("[EMERGENCY] System emergency stop triggered!\n");
        }
    } else {
        wd->state = WD_STATE_TIMEOUT;
        wd->status = DEVICE_WARNING;
    }
}

/* Update all watchdog timers - should be called periodically */
void profibus_watchdog_update(profibus_monitor_t *monitor) {
    uint32_t current_time = get_system_time_ms();
    monitor->system_time_ms = current_time;
    
    for (int i = 0; i < MAX_DEVICES; i++) {
        profibus_watchdog_t *wd = &monitor->devices[i];
        
        if (!wd->enabled || wd->state == WD_STATE_INACTIVE) {
            continue;
        }
        
        uint32_t elapsed = current_time - wd->last_response_time;
        
        if (elapsed > wd->timeout_ms) {
            if (wd->state == WD_STATE_ACTIVE || wd->state == WD_STATE_TIMEOUT) {
                handle_timeout(monitor, wd);
            }
        }
    }
}

/* Get diagnostic information */
void profibus_monitor_get_diagnostics(profibus_monitor_t *monitor) {
    printf("\n=== Profibus Network Diagnostics ===\n");
    printf("Active Devices: %d\n", monitor->active_device_count);
    printf("Emergency Stop: %s\n", 
           monitor->emergency_stop_triggered ? "ACTIVE" : "Normal");
    printf("\nDevice Status:\n");
    
    for (int i = 0; i < MAX_DEVICES; i++) {
        profibus_watchdog_t *wd = &monitor->devices[i];
        
        if (!wd->enabled) {
            continue;
        }
        
        const char *status_str[] = {"OK", "WARNING", "TIMEOUT", "OFFLINE"};
        const char *state_str[] = {"INACTIVE", "ACTIVE", "TIMEOUT", "FAULT"};
        
        uint32_t elapsed = monitor->system_time_ms - wd->last_response_time;
        
        printf("  Device %d: Status=%s, State=%s, Elapsed=%d ms, "
               "Timeouts=%d\n",
               wd->device_id, 
               status_str[wd->status],
               state_str[wd->state],
               elapsed,
               wd->timeout_count);
    }
    printf("=====================================\n\n");
}

/* Simulate Profibus communication cycle */
void simulate_communication_cycle(profibus_monitor_t *monitor, 
                                 uint8_t device_id, 
                                 bool success) {
    if (success) {
        profibus_watchdog_kick(monitor, device_id);
        printf("[COMM] Device %d: Communication successful\n", device_id);
    } else {
        printf("[COMM] Device %d: Communication FAILED\n", device_id);
    }
}

/* Example usage demonstrating watchdog monitoring */
int main(void) {
    profibus_monitor_t monitor;
    
    printf("Profibus Watchdog and Monitoring Demo\n");
    printf("======================================\n\n");
    
    /* Initialize monitoring system */
    profibus_monitor_init(&monitor);
    
    /* Configure watchdogs for devices */
    profibus_watchdog_configure(&monitor, 0, 500);  /* Master: 500ms timeout */
    profibus_watchdog_configure(&monitor, 1, 1000); /* Slave 1: 1s timeout */
    profibus_watchdog_configure(&monitor, 2, 1000); /* Slave 2: 1s timeout */
    
    printf("\nStarting communication simulation...\n\n");
    
    /* Simulation loop */
    for (int cycle = 0; cycle < 15; cycle++) {
        printf("--- Cycle %d ---\n", cycle);
        
        /* Simulate various communication scenarios */
        simulate_communication_cycle(&monitor, 0, true);  /* Master always OK */
        
        /* Device 1: Fails after cycle 5 */
        simulate_communication_cycle(&monitor, 1, cycle < 5);
        
        /* Device 2: Intermittent failures */
        simulate_communication_cycle(&monitor, 2, (cycle % 3) != 2);
        
        /* Update watchdog timers */
        profibus_watchdog_update(&monitor);
        
        /* Sleep to simulate cycle time */
        struct timespec sleep_time = {0, 300000000}; /* 300ms */
        nanosleep(&sleep_time, NULL);
        
        /* Show diagnostics every 5 cycles */
        if ((cycle + 1) % 5 == 0) {
            profibus_monitor_get_diagnostics(&monitor);
        }
    }
    
    /* Final diagnostics */
    printf("\nFinal System State:\n");
    profibus_monitor_get_diagnostics(&monitor);
    
    return 0;
}
```

## C++ Implementation (Object-Oriented Approach)

```cpp
/*
 * Profibus Watchdog and Monitoring Implementation in C++
 * Object-oriented design with advanced monitoring features
 */

#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <map>
#include <iomanip>

using namespace std;
using namespace std::chrono;

// Watchdog callback function type
using WatchdogCallback = function<void(uint8_t device_id, const string& reason)>;

// Device status enumeration
enum class DeviceStatus {
    OK,
    WARNING,
    TIMEOUT,
    OFFLINE
};

// Watchdog state enumeration
enum class WatchdogState {
    INACTIVE,
    ACTIVE,
    TIMEOUT,
    FAULT
};

// Class representing individual device watchdog
class ProfibusWatchdog {
private:
    uint8_t deviceId;
    uint32_t timeoutMs;
    steady_clock::time_point lastResponseTime;
    uint32_t timeoutCount;
    uint32_t retryCount;
    uint32_t maxRetries;
    WatchdogState state;
    DeviceStatus status;
    bool enabled;
    WatchdogCallback onTimeoutCallback;
    WatchdogCallback onRecoveryCallback;

public:
    ProfibusWatchdog(uint8_t id, uint32_t timeout, uint32_t maxRetry = 3) 
        : deviceId(id), 
          timeoutMs(timeout),
          lastResponseTime(steady_clock::now()),
          timeoutCount(0),
          retryCount(0),
          maxRetries(maxRetry),
          state(WatchdogState::ACTIVE),
          status(DeviceStatus::OK),
          enabled(true) {
        
        cout << "[WATCHDOG] Device " << (int)deviceId 
             << " initialized with timeout " << timeoutMs << " ms" << endl;
    }

    // Reset/kick the watchdog
    void kick() {
        if (!enabled) return;

        lastResponseTime = steady_clock::now();
        
        if (retryCount > 0 && onRecoveryCallback) {
            onRecoveryCallback(deviceId, "Device recovered");
        }
        
        retryCount = 0;
        state = WatchdogState::ACTIVE;
        status = DeviceStatus::OK;
    }

    // Check if watchdog has timed out
    bool checkTimeout() {
        if (!enabled || state == WatchdogState::INACTIVE) {
            return false;
        }

        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - lastResponseTime).count();

        if (elapsed > timeoutMs) {
            handleTimeout();
            return true;
        }

        return false;
    }

    // Set timeout callback
    void setTimeoutCallback(WatchdogCallback callback) {
        onTimeoutCallback = callback;
    }

    // Set recovery callback
    void setRecoveryCallback(WatchdogCallback callback) {
        onRecoveryCallback = callback;
    }

    // Get current status
    DeviceStatus getStatus() const { return status; }
    WatchdogState getState() const { return state; }
    uint8_t getDeviceId() const { return deviceId; }
    uint32_t getTimeoutCount() const { return timeoutCount; }
    uint32_t getRetryCount() const { return retryCount; }
    
    uint32_t getElapsedTime() const {
        auto now = steady_clock::now();
        return duration_cast<milliseconds>(now - lastResponseTime).count();
    }

    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() const { return enabled; }

private:
    void handleTimeout() {
        timeoutCount++;
        retryCount++;

        cout << "[WATCHDOG] Device " << (int)deviceId 
             << " TIMEOUT (count: " << timeoutCount 
             << ", retry: " << retryCount << "/" << maxRetries << ")" << endl;

        if (retryCount >= maxRetries) {
            state = WatchdogState::FAULT;
            status = DeviceStatus::OFFLINE;
            
            if (onTimeoutCallback) {
                onTimeoutCallback(deviceId, "Maximum retries exceeded");
            }
        } else {
            state = WatchdogState::TIMEOUT;
            status = DeviceStatus::WARNING;
        }
    }
};

// Main monitoring system class
class ProfibusMonitor {
private:
    map<uint8_t, shared_ptr<ProfibusWatchdog>> watchdogs;
    bool emergencyStopActive;
    steady_clock::time_point startTime;

public:
    ProfibusMonitor() 
        : emergencyStopActive(false),
          startTime(steady_clock::now()) {
        
        cout << "=== Profibus Monitor Initialized ===" << endl << endl;
    }

    // Add a device to monitor
    void addDevice(uint8_t deviceId, uint32_t timeoutMs, uint32_t maxRetries = 3) {
        auto watchdog = make_shared<ProfibusWatchdog>(deviceId, timeoutMs, maxRetries);
        
        // Set default callbacks
        watchdog->setTimeoutCallback([this](uint8_t id, const string& reason) {
            this->handleDeviceTimeout(id, reason);
        });
        
        watchdog->setRecoveryCallback([](uint8_t id, const string& reason) {
            cout << "[MONITOR] Device " << (int)id << " recovered" << endl;
        });
        
        watchdogs[deviceId] = watchdog;
    }

    // Kick watchdog for a device (called on successful communication)
    void kickWatchdog(uint8_t deviceId) {
        auto it = watchdogs.find(deviceId);
        if (it != watchdogs.end()) {
            it->second->kick();
        }
    }

    // Update all watchdogs - call this periodically
    void update() {
        for (auto& [id, watchdog] : watchdogs) {
            watchdog->checkTimeout();
        }
    }

    // Get system diagnostics
    void printDiagnostics() {
        cout << "\n=== Profibus Network Diagnostics ===" << endl;
        cout << "Active Devices: " << watchdogs.size() << endl;
        cout << "Emergency Stop: " << (emergencyStopActive ? "ACTIVE" : "Normal") << endl;
        
        auto uptime = duration_cast<seconds>(steady_clock::now() - startTime).count();
        cout << "System Uptime: " << uptime << " seconds" << endl;
        cout << "\nDevice Status:" << endl;

        for (const auto& [id, watchdog] : watchdogs) {
            printDeviceStatus(watchdog);
        }
        
        cout << "=====================================" << endl << endl;
    }

    // Get statistics for a specific device
    void getDeviceStats(uint8_t deviceId) {
        auto it = watchdogs.find(deviceId);
        if (it == watchdogs.end()) {
            cout << "Device " << (int)deviceId << " not found" << endl;
            return;
        }

        auto& wd = it->second;
        cout << "\n--- Device " << (int)deviceId << " Statistics ---" << endl;
        cout << "  Total Timeouts: " << wd->getTimeoutCount() << endl;
        cout << "  Current Retries: " << wd->getRetryCount() << endl;
        cout << "  Elapsed Since Last Response: " << wd->getElapsedTime() << " ms" << endl;
        cout << "  Status: " << statusToString(wd->getStatus()) << endl;
        cout << "  State: " << stateToString(wd->getState()) << endl;
        cout << "-----------------------------------" << endl << endl;
    }

    bool isEmergencyStop() const { return emergencyStopActive; }

private:
    void handleDeviceTimeout(uint8_t deviceId, const string& reason) {
        cout << "[MONITOR] Device " << (int)deviceId 
             << " FAULT: " << reason << endl;

        // Trigger emergency stop for critical devices
        if (deviceId == 0) {  // Device 0 is master/critical
            emergencyStopActive = true;
            cout << "[EMERGENCY] System emergency stop triggered!" << endl;
        }
    }

    void printDeviceStatus(const shared_ptr<ProfibusWatchdog>& wd) {
        cout << "  Device " << setw(2) << (int)wd->getDeviceId() << ": "
             << "Status=" << setw(8) << statusToString(wd->getStatus()) << ", "
             << "State=" << setw(8) << stateToString(wd->getState()) << ", "
             << "Elapsed=" << setw(5) << wd->getElapsedTime() << " ms, "
             << "Timeouts=" << wd->getTimeoutCount() << endl;
    }

    static string statusToString(DeviceStatus status) {
        switch (status) {
            case DeviceStatus::OK: return "OK";
            case DeviceStatus::WARNING: return "WARNING";
            case DeviceStatus::TIMEOUT: return "TIMEOUT";
            case DeviceStatus::OFFLINE: return "OFFLINE";
            default: return "UNKNOWN";
        }
    }

    static string stateToString(WatchdogState state) {
        switch (state) {
            case WatchdogState::INACTIVE: return "INACTIVE";
            case WatchdogState::ACTIVE: return "ACTIVE";
            case WatchdogState::TIMEOUT: return "TIMEOUT";
            case WatchdogState::FAULT: return "FAULT";
            default: return "UNKNOWN";
        }
    }
};

// Simulation helper
void simulateCommunication(ProfibusMonitor& monitor, uint8_t deviceId, bool success) {
    if (success) {
        monitor.kickWatchdog(deviceId);
        cout << "[COMM] Device " << (int)deviceId << ": Communication OK" << endl;
    } else {
        cout << "[COMM] Device " << (int)deviceId << ": Communication FAILED" << endl;
    }
}

// Main demonstration
int main() {
    cout << "Profibus Watchdog and Monitoring Demo (C++)" << endl;
    cout << "============================================" << endl << endl;

    ProfibusMonitor monitor;

    // Configure devices
    monitor.addDevice(0, 500, 3);   // Master: 500ms timeout
    monitor.addDevice(1, 1000, 3);  // Slave 1: 1s timeout
    monitor.addDevice(2, 1000, 3);  // Slave 2: 1s timeout
    monitor.addDevice(3, 2000, 5);  // Slave 3: 2s timeout, 5 retries

    cout << "\nStarting communication simulation...\n" << endl;

    // Simulation loop
    for (int cycle = 0; cycle < 15; cycle++) {
        cout << "--- Cycle " << cycle << " ---" << endl;

        // Master always responds
        simulateCommunication(monitor, 0, true);
        
        // Device 1 fails after cycle 5
        simulateCommunication(monitor, 1, cycle < 5);
        
        // Device 2 has intermittent failures
        simulateCommunication(monitor, 2, (cycle % 3) != 2);
        
        // Device 3 is reliable
        simulateCommunication(monitor, 3, true);

        // Update watchdog monitoring
        monitor.update();

        // Show diagnostics periodically
        if ((cycle + 1) % 5 == 0) {
            monitor.printDiagnostics();
        }

        // Simulate cycle time
        this_thread::sleep_for(milliseconds(300));
    }

    // Final system state
    cout << "\nFinal System State:" << endl;
    monitor.printDiagnostics();
    
    // Individual device statistics
    monitor.getDeviceStats(1);
    monitor.getDeviceStats(2);

    return 0;
}
```

## Rust Implementation## Summary

```rust
/*
 * Profibus Watchdog and Monitoring Implementation in Rust
 * Type-safe, memory-safe implementation with strong error handling
 */

use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::thread;

// Device status enumeration
#[derive(Debug, Clone, Copy, PartialEq)]
enum DeviceStatus {
    Ok,
    Warning,
    Timeout,
    Offline,
}

// Watchdog state enumeration
#[derive(Debug, Clone, Copy, PartialEq)]
enum WatchdogState {
    Inactive,
    Active,
    Timeout,
    Fault,
}

// Callback type for watchdog events
type WatchdogCallback = Box<dyn Fn(u8, &str) + Send>;

// Individual device watchdog structure
struct ProfibusWatchdog {
    device_id: u8,
    timeout: Duration,
    last_response: Instant,
    timeout_count: u32,
    retry_count: u32,
    max_retries: u32,
    state: WatchdogState,
    status: DeviceStatus,
    enabled: bool,
    on_timeout: Option<WatchdogCallback>,
    on_recovery: Option<WatchdogCallback>,
}

impl ProfibusWatchdog {
    // Create new watchdog instance
    fn new(device_id: u8, timeout_ms: u64, max_retries: u32) -> Self {
        println!(
            "[WATCHDOG] Device {} initialized with timeout {} ms",
            device_id, timeout_ms
        );

        Self {
            device_id,
            timeout: Duration::from_millis(timeout_ms),
            last_response: Instant::now(),
            timeout_count: 0,
            retry_count: 0,
            max_retries,
            state: WatchdogState::Active,
            status: DeviceStatus::Ok,
            enabled: true,
            on_timeout: None,
            on_recovery: None,
        }
    }

    // Reset/kick the watchdog
    fn kick(&mut self) {
        if !self.enabled {
            return;
        }

        // Notify recovery if device was in warning/fault state
        if self.retry_count > 0 {
            if let Some(ref callback) = self.on_recovery {
                callback(self.device_id, "Device recovered");
            }
        }

        self.last_response = Instant::now();
        self.retry_count = 0;
        self.state = WatchdogState::Active;
        self.status = DeviceStatus::Ok;
    }

    // Check if watchdog has timed out
    fn check_timeout(&mut self) -> bool {
        if !self.enabled || self.state == WatchdogState::Inactive {
            return false;
        }

        let elapsed = self.last_response.elapsed();

        if elapsed > self.timeout {
            self.handle_timeout();
            true
        } else {
            false
        }
    }

    // Handle timeout event
    fn handle_timeout(&mut self) {
        self.timeout_count += 1;
        self.retry_count += 1;

        println!(
            "[WATCHDOG] Device {} TIMEOUT (count: {}, retry: {}/{})",
            self.device_id, self.timeout_count, self.retry_count, self.max_retries
        );

        if self.retry_count >= self.max_retries {
            self.state = WatchdogState::Fault;
            self.status = DeviceStatus::Offline;

            if let Some(ref callback) = self.on_timeout {
                callback(self.device_id, "Maximum retries exceeded");
            }
        } else {
            self.state = WatchdogState::Timeout;
            self.status = DeviceStatus::Warning;
        }
    }

    // Get elapsed time since last response
    fn elapsed_ms(&self) -> u128 {
        self.last_response.elapsed().as_millis()
    }

    // Set timeout callback
    fn set_timeout_callback<F>(&mut self, callback: F)
    where
        F: Fn(u8, &str) + Send + 'static,
    {
        self.on_timeout = Some(Box::new(callback));
    }

    // Set recovery callback
    fn set_recovery_callback<F>(&mut self, callback: F)
    where
        F: Fn(u8, &str) + Send + 'static,
    {
        self.on_recovery = Some(Box::new(callback));
    }
}

// Main monitoring system
struct ProfibusMonitor {
    watchdogs: HashMap<u8, ProfibusWatchdog>,
    emergency_stop: bool,
    start_time: Instant,
}

impl ProfibusMonitor {
    // Create new monitor instance
    fn new() -> Self {
        println!("=== Profibus Monitor Initialized ===\n");

        Self {
            watchdogs: HashMap::new(),
            emergency_stop: false,
            start_time: Instant::now(),
        }
    }

    // Add device to monitor
    fn add_device(&mut self, device_id: u8, timeout_ms: u64, max_retries: u32) {
        let mut watchdog = ProfibusWatchdog::new(device_id, timeout_ms, max_retries);

        // Set timeout callback
        watchdog.set_timeout_callback(move |id, reason| {
            println!("[MONITOR] Device {} FAULT: {}", id, reason);
        });

        // Set recovery callback
        watchdog.set_recovery_callback(move |id, _reason| {
            println!("[MONITOR] Device {} recovered", id);
        });

        self.watchdogs.insert(device_id, watchdog);
    }

    // Kick watchdog for specific device
    fn kick_watchdog(&mut self, device_id: u8) {
        if let Some(watchdog) = self.watchdogs.get_mut(&device_id) {
            watchdog.kick();
        }
    }

    // Update all watchdogs
    fn update(&mut self) {
        // Check for critical device failures
        let mut critical_failure = false;

        for (id, watchdog) in self.watchdogs.iter_mut() {
            watchdog.check_timeout();

            // Check if master device (ID 0) has failed
            if *id == 0 && watchdog.status == DeviceStatus::Offline {
                critical_failure = true;
            }
        }

        // Trigger emergency stop if critical device failed
        if critical_failure && !self.emergency_stop {
            self.emergency_stop = true;
            println!("[EMERGENCY] System emergency stop triggered!");
        }
    }

    // Print comprehensive diagnostics
    fn print_diagnostics(&self) {
        println!("\n=== Profibus Network Diagnostics ===");
        println!("Active Devices: {}", self.watchdogs.len());
        println!(
            "Emergency Stop: {}",
            if self.emergency_stop { "ACTIVE" } else { "Normal" }
        );
        println!("System Uptime: {} seconds", self.start_time.elapsed().as_secs());
        println!("\nDevice Status:");

        let mut devices: Vec<_> = self.watchdogs.iter().collect();
        devices.sort_by_key(|(id, _)| *id);

        for (id, watchdog) in devices {
            println!(
                "  Device {:2}: Status={:8}, State={:8}, Elapsed={:5} ms, Timeouts={}",
                id,
                format!("{:?}", watchdog.status),
                format!("{:?}", watchdog.state),
                watchdog.elapsed_ms(),
                watchdog.timeout_count
            );
        }

        println!("=====================================\n");
    }

    // Get detailed statistics for specific device
    fn get_device_stats(&self, device_id: u8) {
        match self.watchdogs.get(&device_id) {
            Some(wd) => {
                println!("\n--- Device {} Statistics ---", device_id);
                println!("  Total Timeouts: {}", wd.timeout_count);
                println!("  Current Retries: {}", wd.retry_count);
                println!("  Elapsed Since Last Response: {} ms", wd.elapsed_ms());
                println!("  Status: {:?}", wd.status);
                println!("  State: {:?}", wd.state);
                println!("-----------------------------------\n");
            }
            None => {
                println!("Device {} not found", device_id);
            }
        }
    }

    // Check if emergency stop is active
    fn is_emergency_stop(&self) -> bool {
        self.emergency_stop
    }
}

// Simulate communication cycle
fn simulate_communication(monitor: &mut ProfibusMonitor, device_id: u8, success: bool) {
    if success {
        monitor.kick_watchdog(device_id);
        println!("[COMM] Device {}: Communication OK", device_id);
    } else {
        println!("[COMM] Device {}: Communication FAILED", device_id);
    }
}

// Main demonstration
fn main() {
    println!("Profibus Watchdog and Monitoring Demo (Rust)");
    println!("=============================================\n");

    let mut monitor = ProfibusMonitor::new();

    // Configure devices with different timeouts and retry limits
    monitor.add_device(0, 500, 3);   // Master: 500ms timeout, 3 retries
    monitor.add_device(1, 1000, 3);  // Slave 1: 1s timeout, 3 retries
    monitor.add_device(2, 1000, 3);  // Slave 2: 1s timeout, 3 retries
    monitor.add_device(3, 2000, 5);  // Slave 3: 2s timeout, 5 retries

    println!("\nStarting communication simulation...\n");

    // Simulation loop
    for cycle in 0..15 {
        println!("--- Cycle {} ---", cycle);

        // Master (device 0) always responds
        simulate_communication(&mut monitor, 0, true);

        // Device 1 fails after cycle 5
        simulate_communication(&mut monitor, 1, cycle < 5);

        // Device 2 has intermittent failures (every 3rd cycle)
        simulate_communication(&mut monitor, 2, cycle % 3 != 2);

        // Device 3 is reliable
        simulate_communication(&mut monitor, 3, true);

        // Update all watchdog timers
        monitor.update();

        // Print diagnostics every 5 cycles
        if (cycle + 1) % 5 == 0 {
            monitor.print_diagnostics();
        }

        // Simulate cycle time (300ms)
        thread::sleep(Duration::from_millis(300));
    }

    // Final system state
    println!("\nFinal System State:");
    monitor.print_diagnostics();

    // Individual device statistics
    monitor.get_device_stats(1);
    monitor.get_device_stats(2);

    // Check emergency stop status
    if monitor.is_emergency_stop() {
        println!("WARNING: System is in emergency stop state!");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_watchdog_kick() {
        let mut wd = ProfibusWatchdog::new(1, 1000, 3);
        thread::sleep(Duration::from_millis(100));
        wd.kick();
        assert_eq!(wd.status, DeviceStatus::Ok);
        assert_eq!(wd.retry_count, 0);
    }

    #[test]
    fn test_watchdog_timeout() {
        let mut wd = ProfibusWatchdog::new(1, 100, 3);
        thread::sleep(Duration::from_millis(150));
        let timed_out = wd.check_timeout();
        assert!(timed_out);
        assert_eq!(wd.status, DeviceStatus::Warning);
    }

    #[test]
    fn test_multiple_timeouts() {
        let mut wd = ProfibusWatchdog::new(1, 50, 2);
        
        // First timeout
        thread::sleep(Duration::from_millis(60));
        wd.check_timeout();
        assert_eq!(wd.status, DeviceStatus::Warning);
        
        // Second timeout - should go offline
        thread::sleep(Duration::from_millis(60));
        wd.check_timeout();
        assert_eq!(wd.status, DeviceStatus::Offline);
    }
}
```

**Watchdog and Monitoring in Profibus** provides essential communication failure detection and system protection mechanisms for industrial automation networks. The key takeaways from this implementation topic include:

### Core Concepts
- **Purpose**: Watchdog timers detect communication failures, device malfunctions, and network anomalies before they cause system-wide problems
- **Mechanism**: Periodic "heartbeat" validation where devices must respond within defined timeout periods
- **Recovery Actions**: Progressive response strategies from warnings to emergency stops based on fault severity

### Implementation Highlights

**C Implementation**:
- Lightweight, procedural approach suitable for embedded systems
- Minimal memory footprint with static device arrays
- Direct hardware timer integration potential
- Simple state machine for timeout handling

**C++ Implementation**:
- Object-oriented design with encapsulation of watchdog logic
- Callback mechanisms for flexible event handling
- STL containers for dynamic device management
- RAII principles for resource safety

**Rust Implementation**:
- Memory-safe implementation with zero-cost abstractions
- Strong type system prevents configuration errors
- Pattern matching for elegant state transitions
- Built-in testing framework for validation
- Thread-safe design with Send trait bounds

### Key Features Demonstrated
1. **Multi-device monitoring** with individual timeout configurations
2. **Retry logic** with configurable attempt limits
3. **Progressive fault detection** (warning → timeout → offline)
4. **Emergency stop triggering** for critical device failures
5. **Comprehensive diagnostics** and health reporting
6. **Recovery detection** when failed devices resume communication

### Practical Applications
- Safety-critical industrial control systems
- Redundant master-slave architectures
- Network health monitoring dashboards
- Predictive maintenance based on timeout patterns
- Quality of Service (QoS) enforcement in real-time systems

Watchdog systems are fundamental to building reliable, fault-tolerant Profibus networks that can detect and respond to communication failures before they compromise safety or production continuity.