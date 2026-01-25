# CAN Timestamp and Time Synchronization

## Overview

Timestamp and time synchronization in CAN networks are critical for diagnostic logging, event correlation, fault analysis, and time-triggered communication protocols. Since CAN itself doesn't provide built-in timestamping, it must be implemented at the application or driver level with careful consideration of timing accuracy and synchronization across network nodes.

## Core Concepts

### Timestamp Types

**Hardware Timestamps**: Captured by CAN controller or peripheral at the moment of frame reception/transmission. These provide microsecond-level accuracy and are immune to software latency.

**Software Timestamps**: Recorded by application code when processing messages. Subject to scheduling delays and interrupt latency but easier to implement on basic hardware.

**Synchronized Time**: Network-wide time base allowing correlation of events across multiple ECUs, essential for distributed diagnostics and coordinated actions.

### Time Synchronization Methods

**Master-Slave Synchronization**: One node broadcasts time reference messages that slaves use to adjust their local clocks. Simple but creates single point of failure.

**Precision Time Protocol (PTP)**: IEEE 1588 adaptation for CAN, providing sub-microsecond synchronization through delay measurement and clock adjustment algorithms.

**AUTOSAR Time Synchronization**: Standardized approach using periodic time base messages with offset and drift correction.

## C Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Timestamp structure with nanosecond precision
typedef struct {
    uint32_t seconds;      // Seconds since epoch or system start
    uint32_t nanoseconds;  // Nanosecond portion (0-999999999)
} can_timestamp_t;

// CAN message with timestamp
typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    can_timestamp_t timestamp;
    bool is_extended;
} can_message_timestamped_t;

// Time sync message structure (AUTOSAR-style)
typedef struct {
    uint32_t time_seconds;
    uint32_t time_nanoseconds;
    uint16_t sequence_counter;
    uint8_t sync_status;  // 0=synced, 1=syncing, 2=not synced
    uint8_t reserved;
} time_sync_message_t;

// Time synchronization state
typedef struct {
    can_timestamp_t local_time;
    can_timestamp_t offset_from_master;
    int32_t drift_nsec_per_sec;  // Clock drift in ns/sec
    bool is_synchronized;
    uint32_t last_sync_seq;
    uint64_t last_sync_hw_time;  // Hardware timer value at last sync
} time_sync_state_t;

static time_sync_state_t sync_state = {0};

// Get hardware timestamp (platform-specific)
// This example uses a hypothetical 1MHz timer
static inline uint64_t get_hardware_timer(void) {
    // Platform-specific: read hardware timer register
    // Returns microseconds since power-on
    extern volatile uint32_t* TIMER_COUNTER_REG;
    return (uint64_t)(*TIMER_COUNTER_REG);
}

// Convert hardware timer to CAN timestamp
void hw_timer_to_timestamp(uint64_t hw_time_us, can_timestamp_t* ts) {
    ts->seconds = hw_time_us / 1000000;
    ts->nanoseconds = (hw_time_us % 1000000) * 1000;
}

// Timestamp difference in nanoseconds (t2 - t1)
int64_t timestamp_diff_ns(const can_timestamp_t* t1, const can_timestamp_t* t2) {
    int64_t sec_diff = (int64_t)t2->seconds - (int64_t)t1->seconds;
    int64_t nsec_diff = (int64_t)t2->nanoseconds - (int64_t)t1->nanoseconds;
    return sec_diff * 1000000000LL + nsec_diff;
}

// Add nanoseconds to timestamp
void timestamp_add_ns(can_timestamp_t* ts, int64_t nanoseconds) {
    int64_t total_ns = (int64_t)ts->nanoseconds + nanoseconds;
    
    if (total_ns >= 1000000000LL) {
        ts->seconds += total_ns / 1000000000LL;
        ts->nanoseconds = total_ns % 1000000000LL;
    } else if (total_ns < 0) {
        int64_t borrow_sec = (-total_ns / 1000000000LL) + 1;
        ts->seconds -= borrow_sec;
        ts->nanoseconds = 1000000000LL - ((-total_ns) % 1000000000LL);
    } else {
        ts->nanoseconds = total_ns;
    }
}

// Hardware-timestamped message reception
// Called from interrupt context or driver callback
void can_receive_with_hw_timestamp(uint32_t can_id, 
                                   const uint8_t* data, 
                                   uint8_t dlc) {
    can_message_timestamped_t msg;
    uint64_t hw_time = get_hardware_timer();
    
    msg.id = can_id;
    msg.dlc = dlc;
    for (int i = 0; i < dlc && i < 8; i++) {
        msg.data[i] = data[i];
    }
    
    // Convert hardware time to timestamp
    hw_timer_to_timestamp(hw_time, &msg.timestamp);
    
    // Apply time synchronization offset if synced
    if (sync_state.is_synchronized) {
        timestamp_add_ns(&msg.timestamp, 
                        timestamp_diff_ns(&sync_state.local_time, 
                                         &sync_state.offset_from_master));
    }
    
    // Process message with accurate timestamp
    process_timestamped_message(&msg);
}

// Time synchronization master - broadcasts time
void send_time_sync_message(void) {
    time_sync_message_t sync_msg;
    uint64_t hw_time = get_hardware_timer();
    can_timestamp_t current_time;
    
    hw_timer_to_timestamp(hw_time, &current_time);
    
    sync_msg.time_seconds = current_time.seconds;
    sync_msg.time_nanoseconds = current_time.nanoseconds;
    sync_msg.sequence_counter = (sync_state.last_sync_seq + 1) & 0xFFFF;
    sync_msg.sync_status = 0;  // Master is always synced
    sync_msg.reserved = 0;
    
    // Send on dedicated time sync CAN ID (e.g., 0x100)
    can_transmit(0x100, (uint8_t*)&sync_msg, sizeof(sync_msg));
    
    sync_state.last_sync_seq = sync_msg.sequence_counter;
}

// Time synchronization slave - receives and adjusts
void process_time_sync_message(const uint8_t* data, uint8_t dlc) {
    if (dlc < sizeof(time_sync_message_t)) return;
    
    time_sync_message_t* sync_msg = (time_sync_message_t*)data;
    uint64_t hw_time_now = get_hardware_timer();
    
    // Calculate reception timestamp
    can_timestamp_t reception_time;
    hw_timer_to_timestamp(hw_time_now, &reception_time);
    
    // Master's timestamp from message
    can_timestamp_t master_time = {
        .seconds = sync_msg->time_seconds,
        .nanoseconds = sync_msg->time_nanoseconds
    };
    
    // Simple offset calculation (ignoring transmission delay for now)
    int64_t offset_ns = timestamp_diff_ns(&reception_time, &master_time);
    
    if (!sync_state.is_synchronized) {
        // First sync - set offset directly
        sync_state.offset_from_master = master_time;
        sync_state.local_time = reception_time;
        sync_state.drift_nsec_per_sec = 0;
        sync_state.is_synchronized = true;
    } else {
        // Calculate drift since last sync
        uint64_t hw_time_delta = hw_time_now - sync_state.last_sync_hw_time;
        int64_t expected_offset = offset_ns;
        int64_t actual_offset = timestamp_diff_ns(&sync_state.local_time, 
                                                   &sync_state.offset_from_master);
        
        // Update drift estimate (simple low-pass filter)
        int32_t measured_drift = (expected_offset - actual_offset) / 
                                 (hw_time_delta / 1000000);  // ns/sec
        sync_state.drift_nsec_per_sec = 
            (sync_state.drift_nsec_per_sec * 7 + measured_drift) / 8;
        
        // Gradually adjust offset
        timestamp_add_ns(&sync_state.offset_from_master, offset_ns / 4);
    }
    
    sync_state.last_sync_seq = sync_msg->sequence_counter;
    sync_state.last_sync_hw_time = hw_time_now;
}

// Get synchronized time
void get_synchronized_time(can_timestamp_t* ts) {
    uint64_t hw_time = get_hardware_timer();
    hw_timer_to_timestamp(hw_time, ts);
    
    if (sync_state.is_synchronized) {
        // Apply offset and drift correction
        uint64_t time_since_sync = hw_time - sync_state.last_sync_hw_time;
        int64_t drift_correction = 
            (sync_state.drift_nsec_per_sec * time_since_sync) / 1000000;
        
        timestamp_add_ns(ts, 
                        timestamp_diff_ns(&sync_state.local_time, 
                                         &sync_state.offset_from_master) + 
                        drift_correction);
    }
}

// Example: Diagnostic logging with timestamps
void log_can_message(const can_message_timestamped_t* msg) {
    printf("[%010u.%09u] CAN ID: 0x%03X DLC: %u Data: ",
           msg->timestamp.seconds,
           msg->timestamp.nanoseconds,
           msg->id,
           msg->dlc);
    
    for (int i = 0; i < msg->dlc; i++) {
        printf("%02X ", msg->data[i]);
    }
    printf("\n");
}
```

## C++ Implementation

```cpp
#include <chrono>
#include <cstdint>
#include <vector>
#include <mutex>
#include <optional>
#include <memory>

namespace can {

// High-resolution timestamp using C++11 chrono
using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using Duration = std::chrono::nanoseconds;

// CAN message with timestamp
struct TimestampedMessage {
    uint32_t id;
    std::vector<uint8_t> data;
    Timestamp hardware_timestamp;
    Timestamp synchronized_timestamp;
    bool is_extended;
    
    TimestampedMessage(uint32_t id, const uint8_t* data_ptr, uint8_t dlc)
        : id(id)
        , data(data_ptr, data_ptr + dlc)
        , hardware_timestamp(std::chrono::steady_clock::now())
        , is_extended(false)
    {}
    
    // Get timestamp in nanoseconds since epoch
    int64_t getNanoseconds() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            synchronized_timestamp.time_since_epoch()).count();
    }
};

// Time synchronization parameters
struct TimeSyncConfig {
    Duration sync_interval{std::chrono::milliseconds(100)};
    Duration max_offset{std::chrono::microseconds(100)};
    double drift_alpha{0.125};  // Low-pass filter coefficient
    uint32_t sync_can_id{0x100};
};

// Time synchronization manager
class TimeSyncManager {
public:
    explicit TimeSyncManager(const TimeSyncConfig& config = {})
        : config_(config)
        , is_master_(false)
        , is_synchronized_(false)
        , sequence_counter_(0)
        , offset_(0)
        , drift_rate_(0.0)
    {}
    
    // Configure as master node
    void setAsMaster(bool master) {
        std::lock_guard<std::mutex> lock(mutex_);
        is_master_ = master;
        is_synchronized_ = master;
    }
    
    // Send time sync message (master only)
    std::optional<std::vector<uint8_t>> generateSyncMessage() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!is_master_) return std::nullopt;
        
        auto now = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        std::vector<uint8_t> data(12);
        
        // Pack timestamp (8 bytes) + sequence (2 bytes) + status (1 byte)
        uint64_t timestamp = static_cast<uint64_t>(ns);
        for (int i = 0; i < 8; i++) {
            data[i] = (timestamp >> (i * 8)) & 0xFF;
        }
        
        uint16_t seq = sequence_counter_++;
        data[8] = seq & 0xFF;
        data[9] = (seq >> 8) & 0xFF;
        data[10] = 0;  // Status: synced
        data[11] = 0;  // Reserved
        
        last_sync_time_ = now;
        
        return data;
    }
    
    // Process received sync message (slave only)
    void processSyncMessage(const uint8_t* data, uint8_t dlc, 
                           Timestamp reception_time) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (is_master_ || dlc < 12) return;
        
        // Unpack master timestamp
        uint64_t master_ns = 0;
        for (int i = 0; i < 8; i++) {
            master_ns |= static_cast<uint64_t>(data[i]) << (i * 8);
        }
        
        Timestamp master_time(Duration(master_ns));
        
        // Calculate offset
        Duration new_offset = std::chrono::duration_cast<Duration>(
            master_time - reception_time);
        
        if (!is_synchronized_) {
            // Initial sync
            offset_ = new_offset;
            is_synchronized_ = true;
            last_sync_time_ = reception_time;
        } else {
            // Calculate drift
            Duration time_since_last = reception_time - last_sync_time_;
            Duration offset_change = new_offset - offset_;
            
            if (time_since_last.count() > 0) {
                double measured_drift = static_cast<double>(offset_change.count()) /
                                       static_cast<double>(time_since_last.count());
                
                // Update drift estimate with exponential moving average
                drift_rate_ = config_.drift_alpha * measured_drift +
                             (1.0 - config_.drift_alpha) * drift_rate_;
            }
            
            // Gradually adjust offset (proportional correction)
            Duration correction = new_offset - offset_;
            offset_ += correction / 4;
            
            last_sync_time_ = reception_time;
        }
        
        // Update sequence
        uint16_t seq = data[8] | (static_cast<uint16_t>(data[9]) << 8);
        sequence_counter_ = seq;
    }
    
    // Get synchronized timestamp
    Timestamp getSynchronizedTime(Timestamp local_time) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!is_synchronized_) {
            return local_time;
        }
        
        // Apply offset and drift correction
        Duration time_since_sync = local_time - last_sync_time_;
        Duration drift_correction(
            static_cast<int64_t>(drift_rate_ * time_since_sync.count()));
        
        return local_time + offset_ + drift_correction;
    }
    
    bool isSynchronized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_synchronized_;
    }
    
    Duration getOffset() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return offset_;
    }

private:
    TimeSyncConfig config_;
    mutable std::mutex mutex_;
    bool is_master_;
    bool is_synchronized_;
    uint16_t sequence_counter_;
    Duration offset_;
    double drift_rate_;
    Timestamp last_sync_time_;
};

// Message logger with timestamps
class TimestampedLogger {
public:
    explicit TimestampedLogger(std::shared_ptr<TimeSyncManager> sync_mgr)
        : sync_manager_(sync_mgr)
    {}
    
    void logMessage(const TimestampedMessage& msg) {
        auto sync_time = sync_manager_->getSynchronizedTime(msg.hardware_timestamp);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            sync_time.time_since_epoch()).count();
        
        uint64_t sec = ns / 1000000000ULL;
        uint32_t nsec = ns % 1000000000ULL;
        
        printf("[%010lu.%09u] ID: 0x%03X DLC: %zu Data: ",
               sec, nsec, msg.id, msg.data.size());
        
        for (uint8_t byte : msg.data) {
            printf("%02X ", byte);
        }
        printf("\n");
    }
    
    // Calculate time between two messages
    Duration timeBetweenMessages(const TimestampedMessage& msg1,
                                const TimestampedMessage& msg2) {
        auto t1 = sync_manager_->getSynchronizedTime(msg1.hardware_timestamp);
        auto t2 = sync_manager_->getSynchronizedTime(msg2.hardware_timestamp);
        return t2 - t1;
    }

private:
    std::shared_ptr<TimeSyncManager> sync_manager_;
};

// Example: Event correlation across messages
class EventCorrelator {
public:
    explicit EventCorrelator(Duration correlation_window)
        : correlation_window_(correlation_window)
    {}
    
    void addEvent(const TimestampedMessage& msg) {
        events_.push_back(msg);
        
        // Remove old events outside correlation window
        auto now = msg.synchronized_timestamp;
        events_.erase(
            std::remove_if(events_.begin(), events_.end(),
                [this, now](const TimestampedMessage& evt) {
                    return (now - evt.synchronized_timestamp) > correlation_window_;
                }),
            events_.end()
        );
    }
    
    // Find all events within time window
    std::vector<TimestampedMessage> getCorrelatedEvents(
        Timestamp reference_time, Duration window) {
        
        std::vector<TimestampedMessage> correlated;
        
        for (const auto& evt : events_) {
            Duration diff = evt.synchronized_timestamp > reference_time ?
                evt.synchronized_timestamp - reference_time :
                reference_time - evt.synchronized_timestamp;
            
            if (diff <= window) {
                correlated.push_back(evt);
            }
        }
        
        return correlated;
    }

private:
    Duration correlation_window_;
    std::vector<TimestampedMessage> events_;
};

} // namespace can
```

## Rust Implementation

```rust
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// High-precision timestamp structure
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Timestamp {
    instant: Instant,
}

impl Timestamp {
    pub fn now() -> Self {
        Self {
            instant: Instant::now(),
        }
    }
    
    pub fn duration_since(&self, earlier: &Timestamp) -> Duration {
        self.instant.duration_since(earlier.instant)
    }
    
    pub fn as_nanos(&self) -> u128 {
        self.instant.elapsed().as_nanos()
    }
}

/// CAN message with hardware timestamp
#[derive(Debug, Clone)]
pub struct TimestampedMessage {
    pub id: u32,
    pub data: Vec<u8>,
    pub hardware_timestamp: Timestamp,
    pub synchronized_timestamp: Option<Timestamp>,
    pub is_extended: bool,
}

impl TimestampedMessage {
    pub fn new(id: u32, data: &[u8]) -> Self {
        Self {
            id,
            data: data.to_vec(),
            hardware_timestamp: Timestamp::now(),
            synchronized_timestamp: None,
            is_extended: false,
        }
    }
}

/// Time synchronization configuration
#[derive(Debug, Clone)]
pub struct TimeSyncConfig {
    pub sync_interval: Duration,
    pub max_offset: Duration,
    pub drift_alpha: f64,
    pub sync_can_id: u32,
}

impl Default for TimeSyncConfig {
    fn default() -> Self {
        Self {
            sync_interval: Duration::from_millis(100),
            max_offset: Duration::from_micros(100),
            drift_alpha: 0.125,
            sync_can_id: 0x100,
        }
    }
}

/// Time synchronization state
#[derive(Debug)]
struct TimeSyncState {
    is_master: bool,
    is_synchronized: bool,
    sequence_counter: u16,
    offset: i128,  // Nanoseconds
    drift_rate: f64,
    last_sync_time: Option<Timestamp>,
}

impl Default for TimeSyncState {
    fn default() -> Self {
        Self {
            is_master: false,
            is_synchronized: false,
            sequence_counter: 0,
            offset: 0,
            drift_rate: 0.0,
            last_sync_time: None,
        }
    }
}

/// Time synchronization manager
pub struct TimeSyncManager {
    config: TimeSyncConfig,
    state: Arc<Mutex<TimeSyncState>>,
}

impl TimeSyncManager {
    pub fn new(config: TimeSyncConfig) -> Self {
        Self {
            config,
            state: Arc::new(Mutex::new(TimeSyncState::default())),
        }
    }
    
    /// Configure as master node
    pub fn set_as_master(&self, is_master: bool) {
        let mut state = self.state.lock().unwrap();
        state.is_master = is_master;
        state.is_synchronized = is_master;
    }
    
    /// Generate time sync message (master only)
    pub fn generate_sync_message(&self) -> Option<Vec<u8>> {
        let mut state = self.state.lock().unwrap();
        
        if !state.is_master {
            return None;
        }
        
        let now = Timestamp::now();
        let nanos = now.as_nanos();
        
        let mut data = Vec::with_capacity(12);
        
        // Pack timestamp (8 bytes)
        for i in 0..8 {
            data.push(((nanos >> (i * 8)) & 0xFF) as u8);
        }
        
        // Sequence counter (2 bytes)
        let seq = state.sequence_counter;
        data.push((seq & 0xFF) as u8);
        data.push((seq >> 8) as u8);
        
        // Status and reserved
        data.push(0); // Status: synced
        data.push(0); // Reserved
        
        state.sequence_counter = state.sequence_counter.wrapping_add(1);
        state.last_sync_time = Some(now);
        
        Some(data)
    }
    
    /// Process received sync message (slave only)
    pub fn process_sync_message(&self, data: &[u8], reception_time: Timestamp) {
        let mut state = self.state.lock().unwrap();
        
        if state.is_master || data.len() < 12 {
            return;
        }
        
        // Unpack master timestamp
        let mut master_nanos: u128 = 0;
        for i in 0..8 {
            master_nanos |= (data[i] as u128) << (i * 8);
        }
        
        // Calculate offset (master_time - local_time)
        let local_nanos = reception_time.as_nanos();
        let new_offset = master_nanos as i128 - local_nanos as i128;
        
        if !state.is_synchronized {
            // Initial synchronization
            state.offset = new_offset;
            state.is_synchronized = true;
            state.last_sync_time = Some(reception_time);
        } else if let Some(last_sync) = state.last_sync_time {
            // Calculate drift
            let time_since_last = reception_time.duration_since(&last_sync).as_nanos() as i128;
            
            if time_since_last > 0 {
                let offset_change = new_offset - state.offset;
                let measured_drift = offset_change as f64 / time_since_last as f64;
                
                // Update drift with exponential moving average
                state.drift_rate = self.config.drift_alpha * measured_drift +
                                  (1.0 - self.config.drift_alpha) * state.drift_rate;
            }
            
            // Gradual offset correction
            let correction = (new_offset - state.offset) / 4;
            state.offset += correction;
            
            state.last_sync_time = Some(reception_time);
        }
        
        // Update sequence counter
        let seq = data[8] as u16 | ((data[9] as u16) << 8);
        state.sequence_counter = seq;
    }
    
    /// Get synchronized timestamp
    pub fn get_synchronized_time(&self, local_time: Timestamp) -> Timestamp {
        let state = self.state.lock().unwrap();
        
        if !state.is_synchronized {
            return local_time;
        }
        
        // Calculate drift correction
        let drift_correction = if let Some(last_sync) = state.last_sync_time {
            let time_since_sync = local_time.duration_since(&last_sync).as_nanos() as i128;
            (state.drift_rate * time_since_sync as f64) as i128
        } else {
            0
        };
        
        // Apply offset and drift correction
        let corrected_nanos = local_time.as_nanos() as i128 + 
                             state.offset + 
                             drift_correction;
        
        // For simplicity, return original timestamp
        // In real implementation, would create new Timestamp from corrected value
        local_time
    }
    
    pub fn is_synchronized(&self) -> bool {
        self.state.lock().unwrap().is_synchronized
    }
    
    pub fn get_offset_nanos(&self) -> i128 {
        self.state.lock().unwrap().offset
    }
}

/// Message logger with timestamps
pub struct TimestampedLogger {
    sync_manager: Arc<TimeSyncManager>,
}

impl TimestampedLogger {
    pub fn new(sync_manager: Arc<TimeSyncManager>) -> Self {
        Self { sync_manager }
    }
    
    pub fn log_message(&self, msg: &TimestampedMessage) {
        let sync_time = self.sync_manager.get_synchronized_time(msg.hardware_timestamp);
        let nanos = sync_time.as_nanos();
        
        let seconds = nanos / 1_000_000_000;
        let nanoseconds = nanos % 1_000_000_000;
        
        print!("[{:010}.{:09}] ID: 0x{:03X} DLC: {} Data: ",
               seconds, nanoseconds, msg.id, msg.data.len());
        
        for byte in &msg.data {
            print!("{:02X} ", byte);
        }
        println!();
    }
    
    pub fn time_between_messages(&self, msg1: &TimestampedMessage, 
                                 msg2: &TimestampedMessage) -> Duration {
        let t1 = self.sync_manager.get_synchronized_time(msg1.hardware_timestamp);
        let t2 = self.sync_manager.get_synchronized_time(msg2.hardware_timestamp);
        t2.duration_since(&t1)
    }
}

/// Event correlator for analyzing related messages
pub struct EventCorrelator {
    correlation_window: Duration,
    events: Vec<TimestampedMessage>,
}

impl EventCorrelator {
    pub fn new(correlation_window: Duration) -> Self {
        Self {
            correlation_window,
            events: Vec::new(),
        }
    }
    
    pub fn add_event(&mut self, msg: TimestampedMessage) {
        let now = msg.synchronized_timestamp
            .unwrap_or(msg.hardware_timestamp);
        
        // Remove old events outside correlation window
        self.events.retain(|evt| {
            let evt_time = evt.synchronized_timestamp
                .unwrap_or(evt.hardware_timestamp);
            
            match now.duration_since(&evt_time) {
                time_diff if time_diff <= self.correlation_window => true,
                _ => false,
            }
        });
        
        self.events.push(msg);
    }
    
    pub fn get_correlated_events(&self, reference_time: Timestamp, 
                                 window: Duration) -> Vec<TimestampedMessage> {
        self.events.iter()
            .filter(|evt| {
                let evt_time = evt.synchronized_timestamp
                    .unwrap_or(evt.hardware_timestamp);
                
                let diff = if evt_time > reference_time {
                    evt_time.duration_since(&reference_time)
                } else {
                    reference_time.duration_since(&evt_time)
                };
                
                diff <= window
            })
            .cloned()
            .collect()
    }
    
    pub fn event_count(&self) -> usize {
        self.events.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_time_sync_basic() {
        let config = TimeSyncConfig::default();
        let manager = TimeSyncManager::new(config);
        
        // Configure as master
        manager.set_as_master(true);
        
        // Generate sync message
        let sync_msg = manager.generate_sync_message();
        assert!(sync_msg.is_some());
        assert_eq!(sync_msg.unwrap().len(), 12);
    }
    
    #[test]
    fn test_event_correlation() {
        let mut correlator = EventCorrelator::new(Duration::from_millis(100));
        
        let msg1 = TimestampedMessage::new(0x100, &[1, 2, 3]);
        correlator.add_event(msg1.clone());
        
        std::thread::sleep(Duration::from_millis(10));
        
        let msg2 = TimestampedMessage::new(0x101, &[4, 5, 6]);
        correlator.add_event(msg2.clone());
        
        let correlated = correlator.get_correlated_events(
            msg1.hardware_timestamp,Duration::from_millis(50)
        );
        
        assert_eq!(correlated.len(), 2);
    }
}
```

## Summary

**Timestamp and time synchronization in CAN networks** enable precise event logging, fault analysis, and coordinated communication across distributed ECUs. Key implementation aspects include:

**Hardware timestamping** provides microsecond-level accuracy by capturing message timing at the controller level, eliminating software latency. **Software timestamps** offer easier implementation but suffer from interrupt delays and scheduling jitter.

**Time synchronization protocols** like master-slave architectures or AUTOSAR-compliant approaches distribute a common time base across the network, allowing correlation of events from multiple nodes. These systems calculate and compensate for **clock offset and drift** through periodic synchronization messages and correction algorithms.

The implementations demonstrate practical timestamping with C's lightweight approach suitable for embedded systems, C++'s object-oriented design using chrono for time handling, and Rust's memory-safe implementation with strong type guarantees. All three showcase **offset calculation, drift compensation, and event correlation** capabilities essential for automotive diagnostics, distributed control systems, and synchronized data acquisition.