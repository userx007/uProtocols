# CAN Bus: Cyclic vs Sporadic Messages

## Overview

In CAN (Controller Area Network) communication, messages can be classified into two fundamental patterns based on their transmission behavior: **cyclic** and **sporadic**. Understanding when and how to use each pattern is critical for designing efficient, deterministic, and reliable automotive and industrial control systems.

## Message Classification

### Cyclic Messages
**Cyclic messages** are transmitted at fixed, periodic intervals regardless of whether the data has changed. They provide:

- **Predictable timing**: Messages arrive at known intervals
- **Guaranteed freshness**: Receivers know data is current within one cycle period
- **Timeout detection**: Missing messages indicate communication failures
- **Simple bandwidth calculation**: Fixed, deterministic bus load

**Use cases:**
- Sensor data (speed, temperature, position)
- Heartbeat/alive messages
- Control signals that require constant monitoring
- Safety-critical data requiring guaranteed update rates

### Sporadic Messages
**Sporadic messages** are transmitted only when triggered by an event or when data changes. They provide:

- **Efficient bandwidth usage**: No redundant transmissions
- **Event-driven responsiveness**: Immediate reaction to changes
- **Lower average bus load**: Reduced unnecessary traffic

**Use cases:**
- Button presses and user inputs
- Error/diagnostic messages
- Configuration changes
- Infrequent status updates

## Design Considerations

### Timing Analysis

**Cyclic Message Timing:**
- Cycle time must be faster than required update rate
- Consider worst-case response time = cycle time + transmission time
- Jitter is typically low (within one cycle period)

**Sporadic Message Timing:**
- Minimum inter-arrival time (MIT) must be defined
- Worst-case bus load occurs when all sporadic messages transmit simultaneously
- Priority assignment becomes critical for meeting deadlines

### Bandwidth Planning

For a CAN bus with bandwidth B:

```
Total_Load = Σ(Cyclic_Load) + Σ(Max_Sporadic_Load)

Cyclic_Load = Message_Size / Cycle_Time
Sporadic_Load = Message_Size / Min_Inter_Arrival_Time

Bus_Utilization = Total_Load / B < 0.7 (recommended threshold)
```

### Data Volatility

Choose transmission pattern based on data characteristics:

| Data Type | Volatility | Recommended Pattern |
|-----------|-----------|---------------------|
| Engine RPM | High | Cyclic (10-20ms) |
| Vehicle Speed | High | Cyclic (20-50ms) |
| Door Status | Low | Sporadic (on change) |
| Error Codes | Very Low | Sporadic (on event) |
| Temperature | Medium | Cyclic (100-500ms) |

## C/C++ Implementation Examples

### Example 1: Cyclic Message Transmission

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CAN frame structure
typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
} CAN_Frame;

// Cyclic message configuration
typedef struct {
    uint32_t message_id;
    uint32_t cycle_time_ms;
    uint32_t last_tx_time;
    void (*data_provider)(uint8_t* data, uint8_t* dlc);
} CyclicMessage;

// Example: Engine data provider
void provide_engine_data(uint8_t* data, uint8_t* dlc) {
    uint16_t rpm = get_engine_rpm();
    uint16_t temp = get_engine_temp();
    
    data[0] = rpm & 0xFF;
    data[1] = (rpm >> 8) & 0xFF;
    data[2] = temp & 0xFF;
    data[3] = (temp >> 8) & 0xFF;
    *dlc = 4;
}

// Cyclic transmission handler
void handle_cyclic_messages(CyclicMessage* messages, 
                            uint8_t count, 
                            uint32_t current_time_ms) {
    for (uint8_t i = 0; i < count; i++) {
        CyclicMessage* msg = &messages[i];
        
        // Check if cycle time has elapsed
        if ((current_time_ms - msg->last_tx_time) >= msg->cycle_time_ms) {
            CAN_Frame frame;
            frame.id = msg->message_id;
            
            // Get fresh data
            msg->data_provider(frame.data, &frame.dlc);
            
            // Transmit
            can_transmit(&frame);
            
            // Update timestamp
            msg->last_tx_time = current_time_ms;
        }
    }
}

// Usage example
CyclicMessage cyclic_msgs[] = {
    {0x100, 10, 0, provide_engine_data},    // 10ms cycle
    {0x200, 50, 0, provide_brake_data},     // 50ms cycle
    {0x300, 100, 0, provide_climate_data}   // 100ms cycle
};

void main_loop() {
    while (1) {
        uint32_t time = get_system_time_ms();
        handle_cyclic_messages(cyclic_msgs, 3, time);
        // Other tasks...
    }
}
```

### Example 2: Sporadic Message with Event Triggering

```c
#include <stdbool.h>

typedef struct {
    uint32_t message_id;
    uint32_t min_inter_arrival_ms;
    uint32_t last_tx_time;
    bool pending;
} SporadicMessage;

// Global sporadic message definitions
SporadicMessage door_status_msg = {
    .message_id = 0x400,
    .min_inter_arrival_ms = 50,  // Minimum 50ms between transmissions
    .last_tx_time = 0,
    .pending = false
};

// Event callback (called when door state changes)
void on_door_state_change(uint8_t door_id, bool is_open) {
    door_status_msg.pending = true;
}

// Sporadic message transmission handler
void handle_sporadic_messages(uint32_t current_time_ms) {
    if (door_status_msg.pending) {
        // Check minimum inter-arrival time
        uint32_t time_since_last = current_time_ms - door_status_msg.last_tx_time;
        
        if (time_since_last >= door_status_msg.min_inter_arrival_ms) {
            CAN_Frame frame;
            frame.id = door_status_msg.message_id;
            frame.dlc = 2;
            
            // Gather current door states
            frame.data[0] = get_all_door_states();
            frame.data[1] = get_lock_states();
            
            can_transmit(&frame);
            
            door_status_msg.last_tx_time = current_time_ms;
            door_status_msg.pending = false;
        }
        // If MIT not satisfied, message remains pending
    }
}
```

### Example 3: Hybrid Approach with Change Detection

```cpp
#include <array>
#include <cstring>

class HybridCANMessage {
private:
    uint32_t id_;
    uint32_t cyclic_period_ms_;
    uint32_t sporadic_threshold_;
    uint32_t last_tx_time_;
    std::array<uint8_t, 8> last_data_;
    bool force_transmission_;
    
public:
    HybridCANMessage(uint32_t id, uint32_t cyclic_ms, uint32_t threshold)
        : id_(id), cyclic_period_ms_(cyclic_ms), 
          sporadic_threshold_(threshold), 
          last_tx_time_(0), force_transmission_(false) {
        last_data_.fill(0);
    }
    
    // Check if data has changed significantly
    bool has_significant_change(const uint8_t* new_data, uint8_t dlc) {
        for (uint8_t i = 0; i < dlc; i++) {
            int diff = abs(static_cast<int>(new_data[i]) - 
                          static_cast<int>(last_data_[i]));
            if (diff >= sporadic_threshold_) {
                return true;
            }
        }
        return false;
    }
    
    // Process and potentially transmit
    bool process(uint32_t current_time, const uint8_t* data, uint8_t dlc) {
        uint32_t elapsed = current_time - last_tx_time_;
        bool should_transmit = false;
        
        // Cyclic condition: time has elapsed
        if (elapsed >= cyclic_period_ms_) {
            should_transmit = true;
        }
        // Sporadic condition: significant change detected
        else if (has_significant_change(data, dlc)) {
            should_transmit = true;
        }
        // Forced transmission (e.g., error condition)
        else if (force_transmission_) {
            should_transmit = true;
        }
        
        if (should_transmit) {
            CAN_Frame frame;
            frame.id = id_;
            frame.dlc = dlc;
            std::memcpy(frame.data, data, dlc);
            
            can_transmit(&frame);
            
            std::memcpy(last_data_.data(), data, dlc);
            last_tx_time_ = current_time;
            force_transmission_ = false;
            
            return true;
        }
        
        return false;
    }
    
    void force_next_transmission() {
        force_transmission_ = true;
    }
};

// Usage
HybridCANMessage temp_sensor(0x250, 1000, 5);  // 1s cycle, ±5 change threshold

void temperature_task() {
    uint8_t data[2];
    uint16_t temp = read_temperature_sensor();
    
    data[0] = temp & 0xFF;
    data[1] = (temp >> 8) & 0xFF;
    
    temp_sensor.process(get_time_ms(), data, 2);
}
```

## Rust Implementation Examples

### Example 1: Type-Safe Cyclic Message System

```rust
use std::time::{Duration, Instant};

// Trait for data providers
trait DataProvider {
    fn provide_data(&self) -> Vec<u8>;
}

struct EngineDataProvider;

impl DataProvider for EngineDataProvider {
    fn provide_data(&self) -> Vec<u8> {
        let rpm: u16 = get_engine_rpm();
        let temp: u16 = get_engine_temp();
        
        vec![
            (rpm & 0xFF) as u8,
            ((rpm >> 8) & 0xFF) as u8,
            (temp & 0xFF) as u8,
            ((temp >> 8) & 0xFF) as u8,
        ]
    }
}

struct CyclicMessage<T: DataProvider> {
    id: u32,
    cycle_time: Duration,
    last_tx: Instant,
    provider: T,
}

impl<T: DataProvider> CyclicMessage<T> {
    fn new(id: u32, cycle_ms: u64, provider: T) -> Self {
        Self {
            id,
            cycle_time: Duration::from_millis(cycle_ms),
            last_tx: Instant::now(),
            provider,
        }
    }
    
    fn process(&mut self, can_tx: &mut dyn CanTransmitter) -> bool {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_tx);
        
        if elapsed >= self.cycle_time {
            let data = self.provider.provide_data();
            
            let frame = CanFrame {
                id: self.id,
                data,
            };
            
            can_tx.transmit(frame);
            self.last_tx = now;
            true
        } else {
            false
        }
    }
}

// Example usage
fn cyclic_task_example() {
    let mut engine_msg = CyclicMessage::new(
        0x100,
        10,  // 10ms cycle
        EngineDataProvider
    );
    
    let mut can = CanInterface::new();
    
    loop {
        engine_msg.process(&mut can);
        std::thread::sleep(Duration::from_millis(1));
    }
}
```

### Example 2: Event-Driven Sporadic Messages

```rust
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone)]
struct SporadicMessage {
    id: u32,
    min_inter_arrival: Duration,
    last_tx: Arc<Mutex<Instant>>,
    pending: Arc<Mutex<Option<Vec<u8>>>>,
}

impl SporadicMessage {
    fn new(id: u32, min_interval_ms: u64) -> Self {
        Self {
            id,
            min_inter_arrival: Duration::from_millis(min_interval_ms),
            last_tx: Arc::new(Mutex::new(Instant::now())),
            pending: Arc::new(Mutex::new(None)),
        }
    }
    
    // Called by event handlers
    fn trigger(&self, data: Vec<u8>) {
        let mut pending = self.pending.lock().unwrap();
        *pending = Some(data);
    }
    
    // Called by periodic task
    fn process(&self, can_tx: &mut dyn CanTransmitter) -> bool {
        let mut pending = self.pending.lock().unwrap();
        
        if let Some(data) = pending.take() {
            let mut last_tx = self.last_tx.lock().unwrap();
            let now = Instant::now();
            let elapsed = now.duration_since(*last_tx);
            
            if elapsed >= self.min_inter_arrival {
                let frame = CanFrame {
                    id: self.id,
                    data,
                };
                
                can_tx.transmit(frame);
                *last_tx = now;
                true
            } else {
                // Put data back, MIT not satisfied yet
                *pending = Some(data);
                false
            }
        } else {
            false
        }
    }
}

// Example: Door state monitoring
fn door_monitoring_example() {
    let door_msg = SporadicMessage::new(0x400, 50);
    
    // Clone for event handler
    let door_msg_clone = door_msg.clone();
    
    // Simulate event handler
    std::thread::spawn(move || {
        loop {
            let door_state = wait_for_door_event();
            let data = vec![door_state.bits(), 0x00];
            door_msg_clone.trigger(data);
        }
    });
    
    // Main transmission task
    let mut can = CanInterface::new();
    loop {
        door_msg.process(&mut can);
        std::thread::sleep(Duration::from_millis(5));
    }
}
```

### Example 3: Advanced Hybrid Message with Change Detection

```rust
use std::time::{Duration, Instant};

struct HybridMessage {
    id: u32,
    cyclic_period: Duration,
    change_threshold: u8,
    last_tx: Instant,
    last_data: Vec<u8>,
    forced: bool,
}

impl HybridMessage {
    fn new(id: u32, cyclic_ms: u64, threshold: u8) -> Self {
        Self {
            id,
            cyclic_period: Duration::from_millis(cyclic_ms),
            change_threshold: threshold,
            last_tx: Instant::now(),
            last_data: Vec::new(),
            forced: false,
        }
    }
    
    fn has_significant_change(&self, new_data: &[u8]) -> bool {
        if self.last_data.len() != new_data.len() {
            return true;
        }
        
        self.last_data.iter()
            .zip(new_data.iter())
            .any(|(old, new)| {
                let diff = (*old as i16 - *new as i16).abs();
                diff >= self.change_threshold as i16
            })
    }
    
    fn process(&mut self, data: Vec<u8>, can_tx: &mut dyn CanTransmitter) -> TransmitReason {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_tx);
        
        let reason = if self.forced {
            TransmitReason::Forced
        } else if elapsed >= self.cyclic_period {
            TransmitReason::Cyclic
        } else if self.has_significant_change(&data) {
            TransmitReason::ChangeDetected
        } else {
            return TransmitReason::NotTransmitted;
        };
        
        let frame = CanFrame {
            id: self.id,
            data: data.clone(),
        };
        
        can_tx.transmit(frame);
        
        self.last_data = data;
        self.last_tx = now;
        self.forced = false;
        
        reason
    }
    
    fn force(&mut self) {
        self.forced = true;
    }
}

#[derive(Debug, PartialEq)]
enum TransmitReason {
    Cyclic,
    ChangeDetected,
    Forced,
    NotTransmitted,
}

// Usage example
fn hybrid_sensor_example() {
    let mut temp_msg = HybridMessage::new(0x250, 1000, 5);
    let mut can = CanInterface::new();
    
    loop {
        let temperature = read_temperature_sensor();
        let data = vec![
            (temperature & 0xFF) as u8,
            ((temperature >> 8) & 0xFF) as u8,
        ];
        
        let reason = temp_msg.process(data, &mut can);
        
        if reason != TransmitReason::NotTransmitted {
            println!("Transmitted due to: {:?}", reason);
        }
        
        std::thread::sleep(Duration::from_millis(10));
    }
}
```

## Design Patterns and Best Practices

### 1. Message Priority Assignment

```
High Priority (Low CAN ID):
├── Safety-critical cyclic (airbag, brake)
├── Fast cyclic control (engine, steering)
├── Critical sporadic (error messages)
└── Normal sporadic (user inputs)

Low Priority (High CAN ID):
├── Slow cyclic status
└── Diagnostic messages
```

### 2. Timeout Monitoring

For cyclic messages, implement timeout detection:

```c
#define TIMEOUT_FACTOR 3  // 3x cycle time

typedef struct {
    uint32_t message_id;
    uint32_t expected_cycle_ms;
    uint32_t last_rx_time;
    bool timeout_detected;
} MessageMonitor;

void check_message_timeout(MessageMonitor* monitor, uint32_t current_time) {
    uint32_t elapsed = current_time - monitor->last_rx_time;
    uint32_t timeout_threshold = monitor->expected_cycle_ms * TIMEOUT_FACTOR;
    
    if (elapsed > timeout_threshold) {
        if (!monitor->timeout_detected) {
            monitor->timeout_detected = true;
            handle_communication_error(monitor->message_id);
        }
    }
}
```

### 3. Bandwidth Optimization

Balance cyclic and sporadic messages:

```rust
struct BandwidthAnalyzer {
    cyclic_messages: Vec<(u32, Duration, usize)>,  // (id, period, size)
    sporadic_messages: Vec<(u32, Duration, usize)>, // (id, MIT, size)
}

impl BandwidthAnalyzer {
    fn calculate_utilization(&self, bitrate: u32) -> f64 {
        let cyclic_load: f64 = self.cyclic_messages.iter()
            .map(|(_, period, size)| {
                let frame_bits = size * 8 + 47; // Data + CAN overhead
                frame_bits as f64 / period.as_secs_f64()
            })
            .sum();
        
        let sporadic_load: f64 = self.sporadic_messages.iter()
            .map(|(_, mit, size)| {
                let frame_bits = size * 8 + 47;
                frame_bits as f64 / mit.as_secs_f64()
            })
            .sum();
        
        (cyclic_load + sporadic_load) / bitrate as f64
    }
}
```

## Summary

**Cyclic messages** transmit at fixed intervals, providing predictable timing, guaranteed freshness, and simple timeout detection. They're ideal for sensor data, control signals, and safety-critical information requiring constant monitoring. The trade-off is higher bandwidth consumption with redundant transmissions.

**Sporadic messages** transmit only on events or changes, optimizing bandwidth and providing immediate event response. They're perfect for user inputs, error messages, and infrequent status updates. The challenge is managing worst-case bus load and ensuring minimum inter-arrival times are respected.

**Key design principles:**
- Match message pattern to data volatility and latency requirements
- Assign CAN priorities based on criticality and timing needs
- Keep bus utilization below 70% accounting for worst-case sporadic bursts
- Implement timeout monitoring for cyclic messages
- Consider hybrid approaches with change detection for semi-volatile data
- Define minimum inter-arrival times for all sporadic messages

Modern automotive systems typically use both patterns: cyclic for continuous monitoring (engine control, safety systems) and sporadic for events (door status, button presses, diagnostics). The choice depends on data characteristics, timing requirements, and bandwidth constraints, with proper analysis ensuring deterministic, reliable communication.