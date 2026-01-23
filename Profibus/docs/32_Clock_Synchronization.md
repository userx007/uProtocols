# Clock Synchronization in Profibus

## Overview

Clock synchronization in Profibus is a critical mechanism that ensures all devices on a fieldbus network maintain a common time reference. This synchronized time base is essential for coordinated automation tasks, distributed control systems, event timestamping, data logging, and ensuring deterministic behavior across multiple devices.

In industrial automation, precise time synchronization enables:
- **Coordinated actions** across multiple devices
- **Accurate event sequencing** and correlation
- **Synchronized data acquisition** from multiple sources
- **Deterministic control loops** with known timing relationships
- **Audit trails** with consistent timestamps

## Profibus Clock Synchronization Mechanisms

### Time Base Concepts

Profibus implements clock synchronization primarily through the **master-slave time distribution model**:

1. **Time Master**: Typically a Class 1 master (PLC or controller) that maintains the reference time
2. **Time Slaves**: All other devices on the bus that synchronize to the master's clock
3. **Synchronization Cycle**: Periodic transmission of time information
4. **Clock Drift Compensation**: Mechanisms to handle oscillator variations

### Synchronization Methods

**Broadcast Time Telegram**: The master periodically broadcasts time information to all slaves using a specific telegram format containing absolute time stamps.

**Slave Adjustment Algorithm**: Slaves adjust their local clocks based on received time information, accounting for transmission delays and drift.

## C/C++ Implementation

### Basic Profibus Time Structure

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Profibus time format structure
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
    uint16_t microsecond;
} profibus_time_t;

// Clock synchronization configuration
typedef struct {
    bool is_master;
    uint32_t sync_interval_ms;  // Synchronization interval
    int32_t max_drift_us;       // Maximum allowable drift in microseconds
    uint32_t last_sync_time;    // Last synchronization timestamp
} clock_sync_config_t;

// Clock synchronization state
typedef struct {
    profibus_time_t local_time;
    profibus_time_t master_time;
    int32_t drift_correction_us;
    uint32_t sync_count;
    bool is_synchronized;
} clock_sync_state_t;
```

### Time Master Implementation

```c
// Convert system time to Profibus time format
void system_to_profibus_time(const struct timespec *sys_time, 
                              profibus_time_t *pb_time) {
    struct tm *tm_info = gmtime(&sys_time->tv_sec);
    
    pb_time->year = tm_info->tm_year + 1900;
    pb_time->month = tm_info->tm_mon + 1;
    pb_time->day = tm_info->tm_mday;
    pb_time->hour = tm_info->tm_hour;
    pb_time->minute = tm_info->tm_min;
    pb_time->second = tm_info->tm_sec;
    pb_time->millisecond = sys_time->tv_nsec / 1000000;
    pb_time->microsecond = (sys_time->tv_nsec / 1000) % 1000;
}

// Encode time telegram for transmission
uint8_t encode_time_telegram(const profibus_time_t *time, 
                              uint8_t *buffer, 
                              size_t buffer_size) {
    if (buffer_size < 12) {
        return 0;  // Buffer too small
    }
    
    // Encode time into telegram format
    buffer[0] = 0x5C;  // Function code for time sync
    buffer[1] = (time->year >> 8) & 0xFF;
    buffer[2] = time->year & 0xFF;
    buffer[3] = time->month;
    buffer[4] = time->day;
    buffer[5] = time->hour;
    buffer[6] = time->minute;
    buffer[7] = time->second;
    buffer[8] = (time->millisecond >> 8) & 0xFF;
    buffer[9] = time->millisecond & 0xFF;
    buffer[10] = (time->microsecond >> 8) & 0xFF;
    buffer[11] = time->microsecond & 0xFF;
    
    return 12;  // Telegram length
}

// Master: Broadcast time synchronization
int broadcast_time_sync(int bus_fd, clock_sync_config_t *config) {
    struct timespec current_time;
    profibus_time_t pb_time;
    uint8_t telegram[256];
    uint8_t telegram_len;
    
    // Get current system time
    clock_gettime(CLOCK_REALTIME, &current_time);
    
    // Convert to Profibus format
    system_to_profibus_time(&current_time, &pb_time);
    
    // Encode telegram
    telegram_len = encode_time_telegram(&pb_time, telegram, sizeof(telegram));
    
    // Broadcast to all slaves (address 0xFF for broadcast)
    if (profibus_send_broadcast(bus_fd, telegram, telegram_len) < 0) {
        return -1;
    }
    
    config->last_sync_time = get_tick_count();
    return 0;
}
```

### Time Slave Implementation

```c
// Decode received time telegram
bool decode_time_telegram(const uint8_t *buffer, 
                          size_t length, 
                          profibus_time_t *time) {
    if (length < 12 || buffer[0] != 0x5C) {
        return false;  // Invalid telegram
    }
    
    time->year = (buffer[1] << 8) | buffer[2];
    time->month = buffer[3];
    time->day = buffer[4];
    time->hour = buffer[5];
    time->minute = buffer[6];
    time->second = buffer[7];
    time->millisecond = (buffer[8] << 8) | buffer[9];
    time->microsecond = (buffer[10] << 8) | buffer[11];
    
    return true;
}

// Calculate time difference in microseconds
int64_t calculate_time_diff_us(const profibus_time_t *t1, 
                                const profibus_time_t *t2) {
    // Simplified calculation - convert both to microseconds since epoch
    // and find difference (full implementation would be more complex)
    int64_t diff = 0;
    
    diff += (int64_t)(t1->hour - t2->hour) * 3600000000LL;
    diff += (int64_t)(t1->minute - t2->minute) * 60000000LL;
    diff += (int64_t)(t1->second - t2->second) * 1000000LL;
    diff += (int64_t)(t1->millisecond - t2->millisecond) * 1000LL;
    diff += (int64_t)(t1->microsecond - t2->microsecond);
    
    return diff;
}

// Slave: Process received time synchronization
int process_time_sync(clock_sync_state_t *state, 
                      const uint8_t *telegram, 
                      size_t length) {
    profibus_time_t received_time;
    struct timespec local_sys_time;
    profibus_time_t local_time;
    int64_t drift_us;
    
    // Decode received time
    if (!decode_time_telegram(telegram, length, &received_time)) {
        return -1;
    }
    
    // Get local time
    clock_gettime(CLOCK_REALTIME, &local_sys_time);
    system_to_profibus_time(&local_sys_time, &local_time);
    
    // Calculate drift
    drift_us = calculate_time_diff_us(&local_time, &received_time);
    
    // Apply correction if drift exceeds threshold
    if (abs(drift_us) > 1000) {  // 1ms threshold
        struct timespec correction;
        correction.tv_sec = drift_us / 1000000;
        correction.tv_nsec = (drift_us % 1000000) * 1000;
        
        // Adjust system clock (requires privileges)
        clock_settime(CLOCK_REALTIME, &correction);
        
        state->drift_correction_us = drift_us;
    }
    
    state->master_time = received_time;
    state->is_synchronized = true;
    state->sync_count++;
    
    return 0;
}
```

### Complete Clock Sync Manager

```c
// Clock synchronization manager
typedef struct {
    clock_sync_config_t config;
    clock_sync_state_t state;
    int bus_fd;
    pthread_t sync_thread;
    bool running;
} clock_sync_manager_t;

// Synchronization thread for master
void* master_sync_thread(void *arg) {
    clock_sync_manager_t *manager = (clock_sync_manager_t*)arg;
    
    while (manager->running) {
        broadcast_time_sync(manager->bus_fd, &manager->config);
        
        // Wait for next sync interval
        usleep(manager->config.sync_interval_ms * 1000);
    }
    
    return NULL;
}

// Initialize clock sync manager
int init_clock_sync(clock_sync_manager_t *manager, 
                    int bus_fd, 
                    bool is_master) {
    manager->bus_fd = bus_fd;
    manager->config.is_master = is_master;
    manager->config.sync_interval_ms = 1000;  // 1 second default
    manager->config.max_drift_us = 100000;    // 100ms max drift
    manager->state.is_synchronized = false;
    manager->state.sync_count = 0;
    manager->running = true;
    
    if (is_master) {
        // Start synchronization thread for master
        pthread_create(&manager->sync_thread, NULL, 
                      master_sync_thread, manager);
    }
    
    return 0;
}

// Cleanup clock sync manager
void cleanup_clock_sync(clock_sync_manager_t *manager) {
    manager->running = false;
    
    if (manager->config.is_master) {
        pthread_join(manager->sync_thread, NULL);
    }
}
```

## Rust Implementation

### Rust Time Structures

```rust
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use std::sync::{Arc, Mutex};
use std::thread;

#[derive(Debug, Clone, Copy)]
pub struct ProfibusTime {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
    pub millisecond: u16,
    pub microsecond: u16,
}

#[derive(Debug)]
pub struct ClockSyncConfig {
    pub is_master: bool,
    pub sync_interval_ms: u32,
    pub max_drift_us: i32,
}

#[derive(Debug)]
pub struct ClockSyncState {
    pub local_time: ProfibusTime,
    pub master_time: ProfibusTime,
    pub drift_correction_us: i32,
    pub sync_count: u32,
    pub is_synchronized: bool,
}

impl ProfibusTime {
    // Convert from SystemTime to ProfibusTime
    pub fn from_system_time(time: SystemTime) -> Result<Self, String> {
        use chrono::{DateTime, Datelike, Timelike, Utc};
        
        let datetime: DateTime<Utc> = time.into();
        
        Ok(ProfibusTime {
            year: datetime.year() as u16,
            month: datetime.month() as u8,
            day: datetime.day() as u8,
            hour: datetime.hour() as u8,
            minute: datetime.minute() as u8,
            second: datetime.second() as u8,
            millisecond: (datetime.timestamp_subsec_millis()) as u16,
            microsecond: (datetime.timestamp_subsec_micros() % 1000) as u16,
        })
    }
    
    // Encode time to telegram format
    pub fn encode(&self) -> Vec<u8> {
        vec![
            0x5C,  // Function code
            (self.year >> 8) as u8,
            (self.year & 0xFF) as u8,
            self.month,
            self.day,
            self.hour,
            self.minute,
            self.second,
            (self.millisecond >> 8) as u8,
            (self.millisecond & 0xFF) as u8,
            (self.microsecond >> 8) as u8,
            (self.microsecond & 0xFF) as u8,
        ]
    }
    
    // Decode time from telegram
    pub fn decode(buffer: &[u8]) -> Result<Self, String> {
        if buffer.len() < 12 || buffer[0] != 0x5C {
            return Err("Invalid time telegram".to_string());
        }
        
        Ok(ProfibusTime {
            year: ((buffer[1] as u16) << 8) | (buffer[2] as u16),
            month: buffer[3],
            day: buffer[4],
            hour: buffer[5],
            minute: buffer[6],
            second: buffer[7],
            millisecond: ((buffer[8] as u16) << 8) | (buffer[9] as u16),
            microsecond: ((buffer[10] as u16) << 8) | (buffer[11] as u16),
        })
    }
    
    // Calculate difference in microseconds
    pub fn diff_us(&self, other: &ProfibusTime) -> i64 {
        let mut diff: i64 = 0;
        
        diff += (self.hour as i64 - other.hour as i64) * 3_600_000_000;
        diff += (self.minute as i64 - other.minute as i64) * 60_000_000;
        diff += (self.second as i64 - other.second as i64) * 1_000_000;
        diff += (self.millisecond as i64 - other.millisecond as i64) * 1_000;
        diff += self.microsecond as i64 - other.microsecond as i64;
        
        diff
    }
}
```

### Master Implementation in Rust

```rust
pub struct TimeMaster {
    config: ClockSyncConfig,
    running: Arc<Mutex<bool>>,
}

impl TimeMaster {
    pub fn new(sync_interval_ms: u32) -> Self {
        TimeMaster {
            config: ClockSyncConfig {
                is_master: true,
                sync_interval_ms,
                max_drift_us: 100_000,
            },
            running: Arc::new(Mutex::new(false)),
        }
    }
    
    // Broadcast time synchronization
    pub fn broadcast_time(&self) -> Result<Vec<u8>, String> {
        let current_time = SystemTime::now();
        let pb_time = ProfibusTime::from_system_time(current_time)?;
        
        Ok(pb_time.encode())
    }
    
    // Start synchronization loop
    pub fn start<F>(&mut self, mut send_fn: F) 
    where 
        F: FnMut(Vec<u8>) -> Result<(), String> + Send + 'static 
    {
        *self.running.lock().unwrap() = true;
        let running = Arc::clone(&self.running);
        let interval = Duration::from_millis(self.config.sync_interval_ms as u64);
        
        thread::spawn(move || {
            while *running.lock().unwrap() {
                let current_time = SystemTime::now();
                if let Ok(pb_time) = ProfibusTime::from_system_time(current_time) {
                    let telegram = pb_time.encode();
                    
                    if let Err(e) = send_fn(telegram) {
                        eprintln!("Time sync broadcast error: {}", e);
                    }
                }
                
                thread::sleep(interval);
            }
        });
    }
    
    pub fn stop(&mut self) {
        *self.running.lock().unwrap() = false;
    }
}
```

### Slave Implementation in Rust

```rust
pub struct TimeSlave {
    config: ClockSyncConfig,
    state: Arc<Mutex<ClockSyncState>>,
}

impl TimeSlave {
    pub fn new(max_drift_us: i32) -> Self {
        TimeSlave {
            config: ClockSyncConfig {
                is_master: false,
                sync_interval_ms: 0,
                max_drift_us,
            },
            state: Arc::new(Mutex::new(ClockSyncState {
                local_time: ProfibusTime {
                    year: 0, month: 0, day: 0,
                    hour: 0, minute: 0, second: 0,
                    millisecond: 0, microsecond: 0,
                },
                master_time: ProfibusTime {
                    year: 0, month: 0, day: 0,
                    hour: 0, minute: 0, second: 0,
                    millisecond: 0, microsecond: 0,
                },
                drift_correction_us: 0,
                sync_count: 0,
                is_synchronized: false,
            })),
        }
    }
    
    // Process received time synchronization
    pub fn process_time_sync(&self, telegram: &[u8]) -> Result<(), String> {
        let received_time = ProfibusTime::decode(telegram)?;
        let local_time = ProfibusTime::from_system_time(SystemTime::now())?;
        
        let drift_us = local_time.diff_us(&received_time);
        
        let mut state = self.state.lock().unwrap();
        
        // Apply correction if drift exceeds threshold (1ms)
        if drift_us.abs() > 1000 {
            state.drift_correction_us = drift_us as i32;
            println!("Clock drift detected: {} μs - correction applied", drift_us);
        }
        
        state.master_time = received_time;
        state.local_time = local_time;
        state.is_synchronized = true;
        state.sync_count += 1;
        
        Ok(())
    }
    
    pub fn get_state(&self) -> ClockSyncState {
        let state = self.state.lock().unwrap();
        ClockSyncState {
            local_time: state.local_time,
            master_time: state.master_time,
            drift_correction_us: state.drift_correction_us,
            sync_count: state.sync_count,
            is_synchronized: state.is_synchronized,
        }
    }
    
    pub fn is_synchronized(&self) -> bool {
        self.state.lock().unwrap().is_synchronized
    }
}
```

### Example Usage in Rust

```rust
// Example: Time Master
fn example_time_master() {
    let mut master = TimeMaster::new(1000); // 1 second interval
    
    // Simulated send function
    master.start(|telegram| {
        println!("Broadcasting time sync: {:?}", telegram);
        // In real implementation: send via Profibus
        Ok(())
    });
    
    // Run for some time
    thread::sleep(Duration::from_secs(10));
    master.stop();
}

// Example: Time Slave
fn example_time_slave() {
    let slave = TimeSlave::new(100_000); // 100ms max drift
    
    // Simulate receiving time telegram
    let master_time = ProfibusTime::from_system_time(SystemTime::now()).unwrap();
    let telegram = master_time.encode();
    
    match slave.process_time_sync(&telegram) {
        Ok(_) => {
            let state = slave.get_state();
            println!("Sync count: {}", state.sync_count);
            println!("Is synchronized: {}", state.is_synchronized);
            println!("Drift correction: {} μs", state.drift_correction_us);
        }
        Err(e) => eprintln!("Sync error: {}", e),
    }
}
```

## Summary

**Clock Synchronization in Profibus** provides a distributed time base across all devices on the fieldbus network, essential for coordinated industrial automation:

**Key Concepts:**
- Master-slave time distribution model where a time master broadcasts absolute time
- Periodic synchronization telegrams with microsecond precision
- Drift detection and compensation algorithms in slave devices
- Support for coordinated actions, event sequencing, and data correlation

**Implementation Features:**
- Time encoding/decoding in Profibus telegram format
- Master broadcasts with configurable intervals (typically 1 second)
- Slave synchronization with drift calculation and correction
- Thread-safe state management for concurrent access
- Configurable drift thresholds and correction policies

**Use Cases:**
- Synchronized motion control across multiple axes
- Coordinated sequential operations in manufacturing
- Precise event timestamping for diagnostics and audit trails
- Distributed data acquisition with time-correlated samples
- Deterministic control loops with guaranteed timing relationships

The implementations in C/C++ and Rust demonstrate both master and slave roles, with proper error handling, thread management, and real-time considerations necessary for industrial automation systems.