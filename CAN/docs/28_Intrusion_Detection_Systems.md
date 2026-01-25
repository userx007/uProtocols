# CAN Intrusion Detection Systems (IDS)

## Overview

Controller Area Network (CAN) Intrusion Detection Systems monitor CAN bus traffic to identify anomalous behavior, security threats, and potential attacks in automotive and industrial control systems. Since CAN was designed without security features, IDS implementations are critical for detecting message injection, spoofing, denial-of-service attacks, and other malicious activities.

## CAN Protocol Background

CAN is a broadcast-based protocol where:
- All nodes receive all messages
- Messages are identified by arbitration IDs (11-bit or 29-bit)
- No authentication or encryption at the protocol level
- Priority-based arbitration determines bus access

## IDS Detection Approaches

### 1. **Signature-Based Detection**
Identifies known attack patterns by matching against predefined signatures of malicious behavior.

### 2. **Anomaly-Based Detection**
Establishes baseline "normal" behavior and flags deviations. Common techniques include:
- **Timing Analysis**: Monitors message intervals and frequencies
- **Sequence Analysis**: Tracks expected message ordering
- **Data Range Checking**: Validates payload values
- **Statistical Methods**: Uses machine learning for pattern recognition

### 3. **Specification-Based Detection**
Enforces known protocol specifications and vehicle behavior models.

## Key Metrics for Monitoring

- **Message Frequency**: Expected transmission rates for each CAN ID
- **Inter-arrival Times**: Time between consecutive messages
- **Payload Patterns**: Expected data ranges and relationships
- **Sequence Ordering**: Message dependencies and state transitions
- **Bus Load**: Overall traffic volume

## C Implementation Example

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define MAX_CAN_IDS 256
#define ALERT_THRESHOLD 100  // ms deviation threshold

// CAN frame structure
typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint64_t timestamp_ms;
} can_frame_t;

// Baseline profile for each CAN ID
typedef struct {
    uint32_t id;
    uint32_t expected_interval_ms;
    uint64_t last_seen_timestamp;
    uint32_t message_count;
    uint8_t min_payload[8];
    uint8_t max_payload[8];
    bool active;
} can_id_profile_t;

// IDS context
typedef struct {
    can_id_profile_t profiles[MAX_CAN_IDS];
    uint32_t alert_count;
    uint64_t start_time_ms;
} ids_context_t;

// Initialize IDS
void ids_init(ids_context_t *ctx) {
    memset(ctx, 0, sizeof(ids_context_t));
    ctx->start_time_ms = (uint64_t)time(NULL) * 1000;
}

// Create baseline profile during learning phase
void ids_learn_message(ids_context_t *ctx, const can_frame_t *frame) {
    can_id_profile_t *profile = NULL;
    
    // Find or create profile for this ID
    for (int i = 0; i < MAX_CAN_IDS; i++) {
        if (ctx->profiles[i].active && ctx->profiles[i].id == frame->id) {
            profile = &ctx->profiles[i];
            break;
        }
        if (!ctx->profiles[i].active) {
            profile = &ctx->profiles[i];
            profile->id = frame->id;
            profile->active = true;
            memset(profile->min_payload, 0xFF, 8);
            memset(profile->max_payload, 0x00, 8);
            break;
        }
    }
    
    if (!profile) return;
    
    // Update timing statistics
    if (profile->last_seen_timestamp > 0) {
        uint64_t interval = frame->timestamp_ms - profile->last_seen_timestamp;
        
        // Update expected interval (simple moving average)
        if (profile->message_count > 0) {
            profile->expected_interval_ms = 
                (profile->expected_interval_ms * profile->message_count + interval) 
                / (profile->message_count + 1);
        } else {
            profile->expected_interval_ms = interval;
        }
    }
    
    // Update payload ranges
    for (int i = 0; i < frame->dlc; i++) {
        if (frame->data[i] < profile->min_payload[i]) {
            profile->min_payload[i] = frame->data[i];
        }
        if (frame->data[i] > profile->max_payload[i]) {
            profile->max_payload[i] = frame->data[i];
        }
    }
    
    profile->last_seen_timestamp = frame->timestamp_ms;
    profile->message_count++;
}

// Detect anomalies in incoming messages
bool ids_detect_anomaly(ids_context_t *ctx, const can_frame_t *frame, 
                        char *alert_msg, size_t msg_len) {
    can_id_profile_t *profile = NULL;
    
    // Find profile for this ID
    for (int i = 0; i < MAX_CAN_IDS; i++) {
        if (ctx->profiles[i].active && ctx->profiles[i].id == frame->id) {
            profile = &ctx->profiles[i];
            break;
        }
    }
    
    // Unknown CAN ID detected
    if (!profile) {
        snprintf(alert_msg, msg_len, 
                "ALERT: Unknown CAN ID 0x%03X detected", frame->id);
        ctx->alert_count++;
        return true;
    }
    
    // Check timing anomaly
    if (profile->last_seen_timestamp > 0) {
        uint64_t interval = frame->timestamp_ms - profile->last_seen_timestamp;
        int64_t deviation = (int64_t)interval - (int64_t)profile->expected_interval_ms;
        
        if (deviation < 0) deviation = -deviation;
        
        if (deviation > ALERT_THRESHOLD) {
            snprintf(alert_msg, msg_len,
                    "ALERT: Timing anomaly for ID 0x%03X - Expected: %ums, Got: %llums",
                    frame->id, profile->expected_interval_ms, 
                    (unsigned long long)interval);
            ctx->alert_count++;
            return true;
        }
    }
    
    // Check payload range anomaly
    for (int i = 0; i < frame->dlc; i++) {
        if (frame->data[i] < profile->min_payload[i] || 
            frame->data[i] > profile->max_payload[i]) {
            snprintf(alert_msg, msg_len,
                    "ALERT: Payload anomaly for ID 0x%03X byte[%d] - "
                    "Expected: [0x%02X-0x%02X], Got: 0x%02X",
                    frame->id, i, profile->min_payload[i], 
                    profile->max_payload[i], frame->data[i]);
            ctx->alert_count++;
            return true;
        }
    }
    
    profile->last_seen_timestamp = frame->timestamp_ms;
    return false;
}

// Example usage
int main() {
    ids_context_t ids;
    ids_init(&ids);
    
    printf("=== CAN IDS Learning Phase ===\n");
    
    // Simulate learning phase with normal traffic
    can_frame_t normal_frames[] = {
        {0x100, 8, {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}, 1000},
        {0x100, 8, {0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71}, 1100},
        {0x100, 8, {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72}, 1200},
        {0x200, 4, {0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00, 0x00, 0x00}, 1050},
        {0x200, 4, {0xAB, 0xBC, 0xCD, 0xDE, 0x00, 0x00, 0x00, 0x00}, 1150},
    };
    
    for (int i = 0; i < 5; i++) {
        ids_learn_message(&ids, &normal_frames[i]);
    }
    
    printf("Learned %d CAN IDs\n\n", 2);
    
    printf("=== CAN IDS Detection Phase ===\n");
    
    // Simulate detection with anomalous traffic
    can_frame_t test_frames[] = {
        // Normal message
        {0x100, 8, {0x03, 0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73}, 1300},
        
        // Timing attack - message arrives too quickly
        {0x100, 8, {0x04, 0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74}, 1320},
        
        // Payload injection - out of range value
        {0x100, 8, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 1420},
        
        // Unknown ID injection
        {0x666, 8, {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1450},
    };
    
    char alert_msg[256];
    for (int i = 0; i < 4; i++) {
        if (ids_detect_anomaly(&ids, &test_frames[i], alert_msg, sizeof(alert_msg))) {
            printf("%s\n", alert_msg);
        } else {
            printf("OK: ID 0x%03X at %llums\n", 
                   test_frames[i].id, 
                   (unsigned long long)test_frames[i].timestamp_ms);
        }
    }
    
    printf("\nTotal alerts: %u\n", ids.alert_count);
    
    return 0;
}
```

## C++ Implementation with State Machine

```cpp
#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>

struct CANFrame {
    uint32_t id;
    uint8_t dlc;
    std::vector<uint8_t> data;
    std::chrono::milliseconds timestamp;
};

struct PayloadStatistics {
    std::vector<double> mean;
    std::vector<double> stddev;
    std::vector<uint8_t> min_val;
    std::vector<uint8_t> max_val;
    
    PayloadStatistics(size_t size = 8) 
        : mean(size, 0.0), stddev(size, 0.0), 
          min_val(size, 255), max_val(size, 0) {}
};

class CANIDProfile {
public:
    uint32_t id;
    double expected_interval_ms;
    double interval_stddev;
    std::chrono::milliseconds last_timestamp;
    PayloadStatistics payload_stats;
    uint32_t message_count;
    std::vector<uint64_t> intervals;
    
    CANIDProfile(uint32_t can_id) 
        : id(can_id), expected_interval_ms(0), interval_stddev(0),
          last_timestamp(0), message_count(0) {}
    
    void update_timing(std::chrono::milliseconds timestamp) {
        if (message_count > 0) {
            uint64_t interval = (timestamp - last_timestamp).count();
            intervals.push_back(interval);
            
            // Calculate mean and standard deviation
            if (intervals.size() >= 10) {
                double sum = 0;
                for (auto i : intervals) sum += i;
                expected_interval_ms = sum / intervals.size();
                
                double variance = 0;
                for (auto i : intervals) {
                    variance += std::pow(i - expected_interval_ms, 2);
                }
                interval_stddev = std::sqrt(variance / intervals.size());
                
                // Keep only recent samples
                if (intervals.size() > 100) {
                    intervals.erase(intervals.begin());
                }
            }
        }
        last_timestamp = timestamp;
        message_count++;
    }
    
    void update_payload(const std::vector<uint8_t>& data) {
        for (size_t i = 0; i < data.size() && i < 8; i++) {
            payload_stats.min_val[i] = std::min(payload_stats.min_val[i], data[i]);
            payload_stats.max_val[i] = std::max(payload_stats.max_val[i], data[i]);
        }
    }
};

class CANIDS {
private:
    std::unordered_map<uint32_t, CANIDProfile> profiles;
    uint32_t alert_count;
    bool learning_mode;
    static constexpr double TIMING_THRESHOLD_SIGMA = 3.0;
    
public:
    CANIDS() : alert_count(0), learning_mode(true) {}
    
    void set_learning_mode(bool enabled) {
        learning_mode = enabled;
    }
    
    void learn_frame(const CANFrame& frame) {
        auto it = profiles.find(frame.id);
        if (it == profiles.end()) {
            profiles.emplace(frame.id, CANIDProfile(frame.id));
            it = profiles.find(frame.id);
        }
        
        it->second.update_timing(frame.timestamp);
        it->second.update_payload(frame.data);
    }
    
    std::vector<std::string> detect_anomalies(const CANFrame& frame) {
        std::vector<std::string> alerts;
        
        // Unknown ID check
        auto it = profiles.find(frame.id);
        if (it == profiles.end()) {
            alerts.push_back("Unknown CAN ID: 0x" + 
                           std::to_string(frame.id));
            alert_count++;
            return alerts;
        }
        
        CANIDProfile& profile = it->second;
        
        // Timing anomaly detection using standard deviation
        if (profile.message_count > 10) {
            uint64_t interval = (frame.timestamp - profile.last_timestamp).count();
            double deviation = std::abs(interval - profile.expected_interval_ms);
            
            if (deviation > TIMING_THRESHOLD_SIGMA * profile.interval_stddev) {
                alerts.push_back("Timing anomaly for ID 0x" + 
                               std::to_string(frame.id) +
                               " - Expected: " + 
                               std::to_string(profile.expected_interval_ms) +
                               "ms ± " + 
                               std::to_string(profile.interval_stddev) +
                               "ms, Got: " + std::to_string(interval) + "ms");
                alert_count++;
            }
        }
        
        // Payload range anomaly
        for (size_t i = 0; i < frame.data.size() && i < 8; i++) {
            if (frame.data[i] < profile.payload_stats.min_val[i] ||
                frame.data[i] > profile.payload_stats.max_val[i]) {
                alerts.push_back("Payload anomaly for ID 0x" +
                               std::to_string(frame.id) + " byte[" +
                               std::to_string(i) + "] - Expected: [0x" +
                               std::to_string(profile.payload_stats.min_val[i]) +
                               "-0x" + 
                               std::to_string(profile.payload_stats.max_val[i]) +
                               "], Got: 0x" + std::to_string(frame.data[i]));
                alert_count++;
            }
        }
        
        // Update profile if in learning mode
        if (learning_mode && alerts.empty()) {
            profile.update_timing(frame.timestamp);
            profile.update_payload(frame.data);
        }
        
        return alerts;
    }
    
    uint32_t get_alert_count() const { return alert_count; }
    
    void print_statistics() const {
        std::cout << "\n=== IDS Statistics ===" << std::endl;
        std::cout << "Monitored CAN IDs: " << profiles.size() << std::endl;
        std::cout << "Total alerts: " << alert_count << std::endl;
        
        for (const auto& [id, profile] : profiles) {
            std::cout << "\nID 0x" << std::hex << id << std::dec 
                     << " - Messages: " << profile.message_count
                     << ", Interval: " << profile.expected_interval_ms 
                     << "ms ± " << profile.interval_stddev << "ms" << std::endl;
        }
    }
};

int main() {
    CANIDS ids;
    
    std::cout << "=== CAN IDS Demo ===" << std::endl;
    
    // Learning phase
    std::vector<CANFrame> training_data = {
        {0x100, 8, {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}, 
         std::chrono::milliseconds(1000)},
        {0x100, 8, {0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71}, 
         std::chrono::milliseconds(1100)},
        {0x100, 8, {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72}, 
         std::chrono::milliseconds(1200)},
    };
    
    for (const auto& frame : training_data) {
        ids.learn_frame(frame);
    }
    
    // Detection phase
    ids.set_learning_mode(false);
    
    std::vector<CANFrame> test_data = {
        // Normal
        {0x100, 8, {0x03, 0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73}, 
         std::chrono::milliseconds(1300)},
        // Attack: timing
        {0x100, 8, {0x04, 0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74}, 
         std::chrono::milliseconds(1320)},
        // Attack: payload
        {0x100, 8, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 
         std::chrono::milliseconds(1420)},
        // Attack: unknown ID
        {0x666, 8, {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 
         std::chrono::milliseconds(1450)},
    };
    
    for (const auto& frame : test_data) {
        auto alerts = ids.detect_anomalies(frame);
        if (!alerts.empty()) {
            for (const auto& alert : alerts) {
                std::cout << "ALERT: " << alert << std::endl;
            }
        } else {
            std::cout << "OK: ID 0x" << std::hex << frame.id << std::dec << std::endl;
        }
    }
    
    ids.print_statistics();
    
    return 0;
}
```

## Rust Implementation with Advanced Features

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

#[derive(Debug, Clone)]
struct CANFrame {
    id: u32,
    dlc: u8,
    data: Vec<u8>,
    timestamp: Instant,
}

#[derive(Debug, Clone)]
struct PayloadStats {
    min: Vec<u8>,
    max: Vec<u8>,
    mean: Vec<f64>,
    variance: Vec<f64>,
    sample_count: usize,
}

impl PayloadStats {
    fn new() -> Self {
        Self {
            min: vec![u8::MAX; 8],
            max: vec![u8::MIN; 8],
            mean: vec![0.0; 8],
            variance: vec![0.0; 8],
            sample_count: 0,
        }
    }
    
    fn update(&mut self, data: &[u8]) {
        for (i, &byte) in data.iter().enumerate().take(8) {
            self.min[i] = self.min[i].min(byte);
            self.max[i] = self.max[i].max(byte);
            
            // Welford's online algorithm for mean and variance
            self.sample_count += 1;
            let delta = byte as f64 - self.mean[i];
            self.mean[i] += delta / self.sample_count as f64;
            let delta2 = byte as f64 - self.mean[i];
            self.variance[i] += delta * delta2;
        }
    }
    
    fn get_stddev(&self, index: usize) -> f64 {
        if self.sample_count < 2 {
            return 0.0;
        }
        (self.variance[index] / (self.sample_count - 1) as f64).sqrt()
    }
}

#[derive(Debug)]
struct CANIDProfile {
    id: u32,
    intervals: Vec<Duration>,
    expected_interval: Duration,
    interval_stddev: f64,
    last_timestamp: Option<Instant>,
    payload_stats: PayloadStats,
    message_count: u64,
}

impl CANIDProfile {
    fn new(id: u32) -> Self {
        Self {
            id,
            intervals: Vec::new(),
            expected_interval: Duration::from_millis(0),
            interval_stddev: 0.0,
            last_timestamp: None,
            payload_stats: PayloadStats::new(),
            message_count: 0,
        }
    }
    
    fn update_timing(&mut self, timestamp: Instant) {
        if let Some(last) = self.last_timestamp {
            let interval = timestamp.duration_since(last);
            self.intervals.push(interval);
            
            // Keep sliding window
            if self.intervals.len() > 100 {
                self.intervals.remove(0);
            }
            
            // Calculate statistics
            if self.intervals.len() >= 10 {
                let sum: Duration = self.intervals.iter().sum();
                self.expected_interval = sum / self.intervals.len() as u32;
                
                let mean_ms = self.expected_interval.as_millis() as f64;
                let variance: f64 = self.intervals
                    .iter()
                    .map(|i| {
                        let diff = i.as_millis() as f64 - mean_ms;
                        diff * diff
                    })
                    .sum::<f64>() / self.intervals.len() as f64;
                
                self.interval_stddev = variance.sqrt();
            }
        }
        
        self.last_timestamp = Some(timestamp);
        self.message_count += 1;
    }
    
    fn update_payload(&mut self, data: &[u8]) {
        self.payload_stats.update(data);
    }
}

#[derive(Debug, Clone)]
enum AlertType {
    UnknownID,
    TimingAnomaly,
    PayloadAnomaly,
    FrequencyAnomaly,
}

#[derive(Debug, Clone)]
struct Alert {
    alert_type: AlertType,
    can_id: u32,
    message: String,
    timestamp: Instant,
}

struct CANIDS {
    profiles: HashMap<u32, CANIDProfile>,
    alerts: Vec<Alert>,
    learning_mode: bool,
    timing_threshold_sigma: f64,
    start_time: Instant,
}

impl CANIDS {
    fn new() -> Self {
        Self {
            profiles: HashMap::new(),
            alerts: Vec::new(),
            learning_mode: true,
            timing_threshold_sigma: 3.0,
            start_time: Instant::now(),
        }
    }
    
    fn set_learning_mode(&mut self, enabled: bool) {
        self.learning_mode = enabled;
    }
    
    fn learn_frame(&mut self, frame: &CANFrame) {
        let profile = self.profiles
            .entry(frame.id)
            .or_insert_with(|| CANIDProfile::new(frame.id));
        
        profile.update_timing(frame.timestamp);
        profile.update_payload(&frame.data);
    }
    
    fn detect_anomalies(&mut self, frame: &CANFrame) -> Vec<Alert> {
        let mut frame_alerts = Vec::new();
        
        // Check for unknown CAN ID
        if !self.profiles.contains_key(&frame.id) {
            let alert = Alert {
                alert_type: AlertType::UnknownID,
                can_id: frame.id,
                message: format!("Unknown CAN ID: 0x{:03X}", frame.id),
                timestamp: frame.timestamp,
            };
            frame_alerts.push(alert.clone());
            self.alerts.push(alert);
            return frame_alerts;
        }
        
        let profile = self.profiles.get(&frame.id).unwrap();
        
        // Timing anomaly detection
        if let Some(last_ts) = profile.last_timestamp {
            if profile.message_count > 10 {
                let interval = frame.timestamp.duration_since(last_ts);
                let interval_ms = interval.as_millis() as f64;
                let expected_ms = profile.expected_interval.as_millis() as f64;
                let deviation = (interval_ms - expected_ms).abs();
                
                if deviation > self.timing_threshold_sigma * profile.interval_stddev {
                    let alert = Alert {
                        alert_type: AlertType::TimingAnomaly,
                        can_id: frame.id,
                        message: format!(
                            "Timing anomaly for ID 0x{:03X} - Expected: {:.1}ms ± {:.1}ms, Got: {:.1}ms",
                            frame.id, expected_ms, profile.interval_stddev, interval_ms
                        ),
                        timestamp: frame.timestamp,
                    };
                    frame_alerts.push(alert.clone());
                    self.alerts.push(alert);
                }
            }
        }
        
        // Payload anomaly detection
        for (i, &byte) in frame.data.iter().enumerate().take(8) {
            if byte < profile.payload_stats.min[i] || byte > profile.payload_stats.max[i] {
                let alert = Alert {
                    alert_type: AlertType::PayloadAnomaly,
                    can_id: frame.id,
                    message: format!(
                        "Payload anomaly for ID 0x{:03X} byte[{}] - Expected: [0x{:02X}-0x{:02X}], Got: 0x{:02X}",
                        frame.id, i, profile.payload_stats.min[i], 
                        profile.payload_stats.max[i], byte
                    ),
                    timestamp: frame.timestamp,
                };
                frame_alerts.push(alert.clone());
                self.alerts.push(alert);
            }
        }
        
        // Update profile in learning mode
        if self.learning_mode && frame_alerts.is_empty() {
            let profile = self.profiles.get_mut(&frame.id).unwrap();
            profile.update_timing(frame.timestamp);
            profile.update_payload(&frame.data);
        }
        
        frame_alerts
    }
    
    fn print_statistics(&self) {
        println!("\n=== IDS Statistics ===");
        println!("Monitored CAN IDs: {}", self.profiles.len());
        println!("Total alerts: {}", self.alerts.len());
        println!("Runtime: {:?}", self.start_time.elapsed());
        
        for (id, profile) in &self.profiles {
            println!(
                "\nID 0x{:03X} - Messages: {}, Interval: {:?} ± {:.1}ms",
                id, profile.message_count, profile.expected_interval, profile.interval_stddev
            );
        }
        
        // Alert breakdown
        let mut alert_counts: HashMap<String, usize> = HashMap::new();
        for alert in &self.alerts {
            let key = format!("{:?}", alert.alert_type);
            *alert_counts.entry(key).or_insert(0) += 1;
        }
        
        println!("\n=== Alert Breakdown ===");
        for (alert_type, count) in alert_counts {
            println!("{}: {}", alert_type, count);
        }
    }
}

fn main() {
    println!("=== CAN IDS Demo (Rust) ===\n");
    
    let mut ids = CANIDS::new();
    let start = Instant::now();
    
    // Training data
    let training_frames = vec![
        CANFrame {
            id: 0x100,
            dlc: 8,
            data: vec![0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70],
            timestamp: start + Duration::from_millis(1000),
        },
        CANFrame {
            id: 0x100,
            dlc: 8,
            data: vec![0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71],
            timestamp: start + Duration::from_millis(1100),
        },
        CANFrame {
            id: 0x100,
            dlc: 8,
            data: vec![0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72],
            timestamp: start + Duration::from_millis(1200),
        },
    ];
    
    println!("Learning phase...");
    for frame in &training_frames {
        ids.learn_frame(frame);
    }
    
    // Detection phase
    ids.set_learning_mode(false);
    println!("\nDetection phase...");
    
    let test_frames = vec![
        CANFrame {
            id: 0x100,
            dlc: 8,
            data: vec![0x03, 0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73],
            timestamp: start + Duration::from_millis(1300),
        },
        CANFrame {
            id: 0x100,dlc: 8,
            data: vec![0x04, 0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74],
            timestamp: start + Duration::from_millis(1320),
        },
        CANFrame {
            id: 0x100,
            dlc: 8,
            data: vec![0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF],
            timestamp: start + Duration::from_millis(1420),
        },
        CANFrame {
            id: 0x666,
            dlc: 8,
            data: vec![0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00],
            timestamp: start + Duration::from_millis(1450),
        },
    ];
    
    for frame in &test_frames {
        let alerts = ids.detect_anomalies(frame);
        if !alerts.is_empty() {
            for alert in alerts {
                println!("ALERT: {}", alert.message);
            }
        } else {
            println!("OK: ID 0x{:03X}", frame.id);
        }
    }
    
    ids.print_statistics();
}
```

## Summary

**CAN Intrusion Detection Systems** are essential security components for automotive and industrial CAN networks. Key takeaways:

### Core Concepts
- **No Built-in Security**: CAN protocol lacks authentication, encryption, and access control
- **Detection Strategies**: Signature-based, anomaly-based, and specification-based approaches
- **Learning Phase**: Establishing baseline "normal" behavior profiles for each CAN ID
- **Detection Phase**: Identifying deviations from learned baselines

### Common Attack Vectors Detected
- **Message Injection**: Unauthorized messages from unknown CAN IDs
- **Replay Attacks**: Retransmission of captured legitimate messages
- **DoS Attacks**: Bus flooding or strategic message timing disruption
- **Fuzzing Attacks**: Random payload values outside normal ranges
- **Spoofing**: Impersonation of legitimate ECUs

### Implementation Approaches
- **Timing Analysis**: Monitor message intervals and detect rapid-fire or delayed transmissions
- **Statistical Methods**: Use mean, standard deviation, and variance to model expected behavior
- **Payload Validation**: Track min/max ranges and detect out-of-bounds values
- **Frequency Monitoring**: Detect abnormal message rates

### Practical Considerations
- **False Positives**: Balance sensitivity with operational tolerance
- **Computational Overhead**: IDS must operate in real-time without impacting bus performance
- **Deployment**: Can be implemented in gateways, dedicated monitoring nodes, or ECUs
- **Integration**: Works alongside firewalls, secure gateways, and authenticated CAN protocols (CAN-FD with security extensions)

The code examples demonstrate progressive sophistication from basic threshold checks (C) to statistical analysis (C++) to advanced online algorithms (Rust), providing flexible frameworks adaptable to specific automotive security requirements.