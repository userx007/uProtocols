# CAN Message Batching

## Overview

Message batching is an optimization technique in CAN bus communication where multiple related signals or data points are combined into fewer CAN frames. This approach reduces bus utilization, decreases message overhead, and improves overall system efficiency.

## Why Message Batching?

### Benefits
- **Reduced Bus Load**: Fewer messages mean less arbitration overhead and more available bandwidth
- **Lower Latency**: Combined transmission reduces cumulative waiting times
- **Efficient Bandwidth Usage**: Maximizes use of the 8-byte CAN payload
- **Reduced CPU Overhead**: Fewer interrupts and processing cycles
- **Deterministic Timing**: Easier to guarantee timing constraints with fewer messages

### Trade-offs
- **Increased Latency for Individual Signals**: Some signals wait for the batch cycle
- **Complexity**: More sophisticated packing/unpacking logic required
- **Atomic Updates**: All signals in a batch update together, which may not always be desirable

## Batching Strategies

### 1. **Temporal Batching**
Group signals that are logically related or have similar update rates.

### 2. **Priority-Based Batching**
Combine lower-priority signals while keeping critical signals in separate frames.

### 3. **Source-Based Batching**
Group signals from the same ECU or subsystem.

### 4. **Size-Optimized Batching**
Pack signals efficiently to minimize wasted bits in the 8-byte payload.

## Signal Packing Considerations

When batching signals, consider:
- **Byte alignment**: Align signals to byte boundaries when possible for easier extraction
- **Endianness**: Maintain consistent byte ordering (typically big-endian in automotive)
- **Scaling and offsets**: Document conversion formulas for each signal
- **Signal boundaries**: Signals can span multiple bytes but should be well-documented

## C Implementation

```c
#include <stdint.h>
#include <string.h>

// Structure representing a batched sensor message
typedef struct {
    uint16_t temperature;  // 0.1°C resolution, range: -40 to 150°C
    uint16_t pressure;     // 0.01 bar resolution, range: 0 to 10 bar
    uint8_t humidity;      // 1% resolution, range: 0 to 100%
    uint8_t battery_voltage; // 0.1V resolution, range: 0 to 25.5V
    uint8_t status_flags;  // Bit flags for various states
    uint8_t reserved;      // Reserved for future use
} __attribute__((packed)) sensor_batch_t;

// Pack multiple sensor readings into a single CAN frame
void pack_sensor_batch(uint8_t* can_data, 
                       float temp_celsius,
                       float pressure_bar,
                       uint8_t humidity_percent,
                       float voltage,
                       uint8_t flags) {
    sensor_batch_t batch;
    
    // Convert physical values to raw values with scaling
    // Temperature: -40°C offset, 0.1°C resolution
    batch.temperature = (uint16_t)((temp_celsius + 40.0f) * 10.0f);
    
    // Pressure: 0.01 bar resolution
    batch.pressure = (uint16_t)(pressure_bar * 100.0f);
    
    // Humidity: direct percentage
    batch.humidity = humidity_percent;
    
    // Battery voltage: 0.1V resolution
    batch.battery_voltage = (uint8_t)(voltage * 10.0f);
    
    // Status flags
    batch.status_flags = flags;
    
    // Reserved byte
    batch.reserved = 0x00;
    
    // Copy to CAN data buffer
    memcpy(can_data, &batch, sizeof(sensor_batch_t));
}

// Unpack a batched sensor message from CAN frame
void unpack_sensor_batch(const uint8_t* can_data,
                         float* temp_celsius,
                         float* pressure_bar,
                         uint8_t* humidity_percent,
                         float* voltage,
                         uint8_t* flags) {
    sensor_batch_t batch;
    memcpy(&batch, can_data, sizeof(sensor_batch_t));
    
    // Convert raw values back to physical values
    *temp_celsius = (batch.temperature / 10.0f) - 40.0f;
    *pressure_bar = batch.pressure / 100.0f;
    *humidity_percent = batch.humidity;
    *voltage = batch.battery_voltage / 10.0f;
    *flags = batch.status_flags;
}

// Example: Batching multiple control commands
typedef struct {
    uint16_t throttle_position;  // 0.01% resolution
    int16_t steering_angle;      // 0.1° resolution, -180 to +180°
    uint8_t brake_pressure;      // 1% resolution
    uint8_t gear_position;       // 0-6 (N, 1-6)
    uint8_t control_mode;        // Mode flags
    uint8_t checksum;            // Simple checksum for validation
} __attribute__((packed)) control_batch_t;

uint8_t calculate_checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len - 1; i++) {
        sum += data[i];
    }
    return sum;
}

void pack_control_batch(uint8_t* can_data,
                        float throttle_percent,
                        float steering_deg,
                        uint8_t brake_percent,
                        uint8_t gear,
                        uint8_t mode) {
    control_batch_t batch;
    
    batch.throttle_position = (uint16_t)(throttle_percent * 100.0f);
    batch.steering_angle = (int16_t)(steering_deg * 10.0f);
    batch.brake_pressure = brake_percent;
    batch.gear_position = gear;
    batch.control_mode = mode;
    
    memcpy(can_data, &batch, sizeof(control_batch_t) - 1);
    can_data[7] = calculate_checksum(can_data, 8);
}

// Periodic batching with timer
#include <stdbool.h>

typedef struct {
    float accumulated_temp;
    float accumulated_pressure;
    uint8_t sample_count;
    uint32_t last_send_time_ms;
    uint32_t batch_period_ms;
} batch_accumulator_t;

void init_batch_accumulator(batch_accumulator_t* acc, uint32_t period_ms) {
    acc->accumulated_temp = 0.0f;
    acc->accumulated_pressure = 0.0f;
    acc->sample_count = 0;
    acc->last_send_time_ms = 0;
    acc->batch_period_ms = period_ms;
}

bool accumulate_and_send(batch_accumulator_t* acc,
                         uint32_t current_time_ms,
                         float temp,
                         float pressure,
                         uint8_t* can_data) {
    acc->accumulated_temp += temp;
    acc->accumulated_pressure += pressure;
    acc->sample_count++;
    
    if ((current_time_ms - acc->last_send_time_ms) >= acc->batch_period_ms) {
        // Average the accumulated values
        float avg_temp = acc->accumulated_temp / acc->sample_count;
        float avg_pressure = acc->accumulated_pressure / acc->sample_count;
        
        pack_sensor_batch(can_data, avg_temp, avg_pressure, 0, 12.5f, 0);
        
        // Reset accumulator
        acc->accumulated_temp = 0.0f;
        acc->accumulated_pressure = 0.0f;
        acc->sample_count = 0;
        acc->last_send_time_ms = current_time_ms;
        
        return true; // Time to send
    }
    
    return false; // Not yet time to send
}
```

## C++ Implementation

```cpp
#include <cstdint>
#include <array>
#include <bitset>
#include <optional>
#include <chrono>
#include <vector>

// Modern C++ approach with type safety and RAII

class CANMessageBatcher {
public:
    struct SensorData {
        float temperature_celsius;
        float pressure_bar;
        uint8_t humidity_percent;
        float battery_voltage;
        std::bitset<8> status_flags;
        
        // Encode to CAN frame
        std::array<uint8_t, 8> encode() const {
            std::array<uint8_t, 8> data{};
            
            // Temperature: -40°C offset, 0.1°C resolution
            uint16_t temp_raw = static_cast<uint16_t>((temperature_celsius + 40.0f) * 10.0f);
            data[0] = temp_raw >> 8;
            data[1] = temp_raw & 0xFF;
            
            // Pressure: 0.01 bar resolution
            uint16_t press_raw = static_cast<uint16_t>(pressure_bar * 100.0f);
            data[2] = press_raw >> 8;
            data[3] = press_raw & 0xFF;
            
            // Humidity
            data[4] = humidity_percent;
            
            // Battery voltage: 0.1V resolution
            data[5] = static_cast<uint8_t>(battery_voltage * 10.0f);
            
            // Status flags
            data[6] = static_cast<uint8_t>(status_flags.to_ulong());
            
            // Reserved
            data[7] = 0x00;
            
            return data;
        }
        
        // Decode from CAN frame
        static SensorData decode(const std::array<uint8_t, 8>& data) {
            SensorData result;
            
            uint16_t temp_raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
            result.temperature_celsius = (temp_raw / 10.0f) - 40.0f;
            
            uint16_t press_raw = (static_cast<uint16_t>(data[2]) << 8) | data[3];
            result.pressure_bar = press_raw / 100.0f;
            
            result.humidity_percent = data[4];
            result.battery_voltage = data[5] / 10.0f;
            result.status_flags = std::bitset<8>(data[6]);
            
            return result;
        }
    };
    
    struct ControlData {
        float throttle_percent;
        float steering_angle_deg;
        uint8_t brake_pressure_percent;
        uint8_t gear_position;
        uint8_t control_mode;
        
        std::array<uint8_t, 8> encode() const {
            std::array<uint8_t, 8> data{};
            
            uint16_t throttle_raw = static_cast<uint16_t>(throttle_percent * 100.0f);
            data[0] = throttle_raw >> 8;
            data[1] = throttle_raw & 0xFF;
            
            int16_t steering_raw = static_cast<int16_t>(steering_angle_deg * 10.0f);
            data[2] = steering_raw >> 8;
            data[3] = steering_raw & 0xFF;
            
            data[4] = brake_pressure_percent;
            data[5] = gear_position;
            data[6] = control_mode;
            
            // Checksum
            uint8_t checksum = 0;
            for (size_t i = 0; i < 7; i++) {
                checksum += data[i];
            }
            data[7] = checksum;
            
            return data;
        }
        
        static std::optional<ControlData> decode(const std::array<uint8_t, 8>& data) {
            // Verify checksum
            uint8_t checksum = 0;
            for (size_t i = 0; i < 7; i++) {
                checksum += data[i];
            }
            if (checksum != data[7]) {
                return std::nullopt; // Invalid checksum
            }
            
            ControlData result;
            
            uint16_t throttle_raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
            result.throttle_percent = throttle_raw / 100.0f;
            
            int16_t steering_raw = (static_cast<int16_t>(data[2]) << 8) | data[3];
            result.steering_angle_deg = steering_raw / 10.0f;
            
            result.brake_pressure_percent = data[4];
            result.gear_position = data[5];
            result.control_mode = data[6];
            
            return result;
        }
    };
};

// Template-based batch accumulator
template<typename T>
class BatchAccumulator {
private:
    std::vector<T> samples_;
    std::chrono::milliseconds batch_period_;
    std::chrono::steady_clock::time_point last_send_time_;
    size_t max_samples_;
    
public:
    BatchAccumulator(std::chrono::milliseconds period, size_t max_samples = 100)
        : batch_period_(period)
        , last_send_time_(std::chrono::steady_clock::now())
        , max_samples_(max_samples) {
        samples_.reserve(max_samples);
    }
    
    void add_sample(const T& sample) {
        if (samples_.size() < max_samples_) {
            samples_.push_back(sample);
        }
    }
    
    std::optional<T> get_batch_and_reset() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_send_time_);
        
        if (elapsed >= batch_period_ && !samples_.empty()) {
            // Calculate average (simplified for demonstration)
            T result = samples_[0]; // In practice, implement proper averaging
            
            samples_.clear();
            last_send_time_ = now;
            
            return result;
        }
        
        return std::nullopt;
    }
    
    bool is_ready() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_send_time_);
        return elapsed >= batch_period_ && !samples_.empty();
    }
};

// Example usage with RAII and modern C++
class CANBatchManager {
private:
    BatchAccumulator<CANMessageBatcher::SensorData> sensor_accumulator_;
    
public:
    CANBatchManager(std::chrono::milliseconds batch_period)
        : sensor_accumulator_(batch_period) {}
    
    void add_sensor_reading(const CANMessageBatcher::SensorData& data) {
        sensor_accumulator_.add_sample(data);
    }
    
    std::optional<std::array<uint8_t, 8>> try_get_batched_message() {
        auto batch = sensor_accumulator_.get_batch_and_reset();
        if (batch) {
            return batch->encode();
        }
        return std::nullopt;
    }
};
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};

// Rust implementation with strong type safety and zero-cost abstractions

#[derive(Debug, Clone, Copy)]
pub struct SensorData {
    pub temperature_celsius: f32,
    pub pressure_bar: f32,
    pub humidity_percent: u8,
    pub battery_voltage: f32,
    pub status_flags: u8,
}

impl SensorData {
    /// Encode sensor data into CAN frame (8 bytes)
    pub fn encode(&self) -> [u8; 8] {
        let mut data = [0u8; 8];
        
        // Temperature: -40°C offset, 0.1°C resolution
        let temp_raw = ((self.temperature_celsius + 40.0) * 10.0) as u16;
        data[0] = (temp_raw >> 8) as u8;
        data[1] = (temp_raw & 0xFF) as u8;
        
        // Pressure: 0.01 bar resolution
        let press_raw = (self.pressure_bar * 100.0) as u16;
        data[2] = (press_raw >> 8) as u8;
        data[3] = (press_raw & 0xFF) as u8;
        
        // Humidity
        data[4] = self.humidity_percent;
        
        // Battery voltage: 0.1V resolution
        data[5] = (self.battery_voltage * 10.0) as u8;
        
        // Status flags
        data[6] = self.status_flags;
        
        // Reserved
        data[7] = 0x00;
        
        data
    }
    
    /// Decode sensor data from CAN frame
    pub fn decode(data: &[u8; 8]) -> Self {
        let temp_raw = u16::from_be_bytes([data[0], data[1]]);
        let temperature_celsius = (temp_raw as f32 / 10.0) - 40.0;
        
        let press_raw = u16::from_be_bytes([data[2], data[3]]);
        let pressure_bar = press_raw as f32 / 100.0;
        
        Self {
            temperature_celsius,
            pressure_bar,
            humidity_percent: data[4],
            battery_voltage: data[5] as f32 / 10.0,
            status_flags: data[6],
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct ControlData {
    pub throttle_percent: f32,
    pub steering_angle_deg: f32,
    pub brake_pressure_percent: u8,
    pub gear_position: u8,
    pub control_mode: u8,
}

impl ControlData {
    pub fn encode(&self) -> [u8; 8] {
        let mut data = [0u8; 8];
        
        let throttle_raw = (self.throttle_percent * 100.0) as u16;
        data[0] = (throttle_raw >> 8) as u8;
        data[1] = (throttle_raw & 0xFF) as u8;
        
        let steering_raw = (self.steering_angle_deg * 10.0) as i16;
        data[2] = (steering_raw >> 8) as u8;
        data[3] = (steering_raw & 0xFF) as u8;
        
        data[4] = self.brake_pressure_percent;
        data[5] = self.gear_position;
        data[6] = self.control_mode;
        
        // Calculate checksum
        let checksum: u8 = data[0..7].iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
        data[7] = checksum;
        
        data
    }
    
    pub fn decode(data: &[u8; 8]) -> Option<Self> {
        // Verify checksum
        let checksum: u8 = data[0..7].iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
        if checksum != data[7] {
            return None; // Invalid checksum
        }
        
        let throttle_raw = u16::from_be_bytes([data[0], data[1]]);
        let throttle_percent = throttle_raw as f32 / 100.0;
        
        let steering_raw = i16::from_be_bytes([data[2], data[3]]);
        let steering_angle_deg = steering_raw as f32 / 10.0;
        
        Some(Self {
            throttle_percent,
            steering_angle_deg,
            brake_pressure_percent: data[4],
            gear_position: data[5],
            control_mode: data[6],
        })
    }
}

/// Generic batch accumulator with configurable period
pub struct BatchAccumulator<T> {
    samples: Vec<T>,
    batch_period: Duration,
    last_send_time: Instant,
    max_samples: usize,
}

impl<T: Clone> BatchAccumulator<T> {
    pub fn new(batch_period: Duration, max_samples: usize) -> Self {
        Self {
            samples: Vec::with_capacity(max_samples),
            batch_period,
            last_send_time: Instant::now(),
            max_samples,
        }
    }
    
    pub fn add_sample(&mut self, sample: T) {
        if self.samples.len() < self.max_samples {
            self.samples.push(sample);
        }
    }
    
    pub fn is_ready(&self) -> bool {
        let elapsed = self.last_send_time.elapsed();
        elapsed >= self.batch_period && !self.samples.is_empty()
    }
    
    pub fn get_batch_and_reset(&mut self) -> Option<Vec<T>> {
        if self.is_ready() {
            let batch = self.samples.clone();
            self.samples.clear();
            self.last_send_time = Instant::now();
            Some(batch)
        } else {
            None
        }
    }
    
    pub fn sample_count(&self) -> usize {
        self.samples.len()
    }
}

/// Averaging batch accumulator for numeric types
pub struct AveragingBatchAccumulator {
    accumulated_temp: f32,
    accumulated_pressure: f32,
    sample_count: u32,
    last_send_time: Instant,
    batch_period: Duration,
}

impl AveragingBatchAccumulator {
    pub fn new(batch_period: Duration) -> Self {
        Self {
            accumulated_temp: 0.0,
            accumulated_pressure: 0.0,
            sample_count: 0,
            last_send_time: Instant::now(),
            batch_period,
        }
    }
    
    pub fn add_reading(&mut self, temp: f32, pressure: f32) {
        self.accumulated_temp += temp;
        self.accumulated_pressure += pressure;
        self.sample_count += 1;
    }
    
    pub fn try_get_average(&mut self) -> Option<(f32, f32)> {
        let elapsed = self.last_send_time.elapsed();
        
        if elapsed >= self.batch_period && self.sample_count > 0 {
            let avg_temp = self.accumulated_temp / self.sample_count as f32;
            let avg_pressure = self.accumulated_pressure / self.sample_count as f32;
            
            // Reset accumulator
            self.accumulated_temp = 0.0;
            self.accumulated_pressure = 0.0;
            self.sample_count = 0;
            self.last_send_time = Instant::now();
            
            Some((avg_temp, avg_pressure))
        } else {
            None
        }
    }
}

/// Complete CAN batch manager
pub struct CANBatchManager {
    sensor_accumulator: BatchAccumulator<SensorData>,
    control_accumulator: BatchAccumulator<ControlData>,
}

impl CANBatchManager {
    pub fn new(sensor_period: Duration, control_period: Duration) -> Self {
        Self {
            sensor_accumulator: BatchAccumulator::new(sensor_period, 100),
            control_accumulator: BatchAccumulator::new(control_period, 50),
        }
    }
    
    pub fn add_sensor_reading(&mut self, data: SensorData) {
        self.sensor_accumulator.add_sample(data);
    }
    
    pub fn add_control_command(&mut self, data: ControlData) {
        self.control_accumulator.add_sample(data);
    }
    
    pub fn try_get_sensor_batch(&mut self) -> Option<[u8; 8]> {
        self.sensor_accumulator
            .get_batch_and_reset()
            .and_then(|batch| batch.first().map(|data| data.encode()))
    }
    
    pub fn try_get_control_batch(&mut self) -> Option<[u8; 8]> {
        self.control_accumulator
            .get_batch_and_reset()
            .and_then(|batch| batch.first().map(|data| data.encode()))
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_sensor_data_encoding() {
        let sensor = SensorData {
            temperature_celsius: 25.5,
            pressure_bar: 2.45,
            humidity_percent: 60,
            battery_voltage: 12.6,
            status_flags: 0b10101010,
        };
        
        let encoded = sensor.encode();
        let decoded = SensorData::decode(&encoded);
        
        assert!((decoded.temperature_celsius - sensor.temperature_celsius).abs() < 0.1);
        assert!((decoded.pressure_bar - sensor.pressure_bar).abs() < 0.01);
        assert_eq!(decoded.humidity_percent, sensor.humidity_percent);
    }
    
    #[test]
    fn test_batch_accumulator() {
        let mut accumulator = AveragingBatchAccumulator::new(Duration::from_millis(100));
        
        accumulator.add_reading(20.0, 1.0);
        accumulator.add_reading(22.0, 1.2);
        accumulator.add_reading(24.0, 1.4);
        
        std::thread::sleep(Duration::from_millis(150));
        
        if let Some((avg_temp, avg_pressure)) = accumulator.try_get_average() {
            assert!((avg_temp - 22.0).abs() < 0.1);
            assert!((avg_pressure - 1.2).abs() < 0.1);
        }
    }
}
```

## Summary

**Message Batching** is a critical optimization technique in CAN bus systems that combines multiple signals into fewer frames to improve efficiency:

**Key Benefits:**
- Reduces bus utilization by minimizing arbitration overhead (each CAN frame has 47+ bits of overhead)
- Lowers interrupt frequency and CPU load on receiving nodes
- Improves deterministic timing by reducing the number of messages competing for bus access
- Maximizes payload efficiency by filling the 8-byte CAN data field

**Implementation Approaches:**
- **Temporal batching**: Group signals with similar update rates into periodic batches
- **Logical batching**: Combine related signals (e.g., all sensor data from one subsystem)
- **Averaging batching**: Accumulate multiple samples and send averaged values to reduce noise

**Best Practices:**
- Use clear signal packing schemes with documented bit positions, scaling factors, and offsets
- Implement checksums or CRCs for data integrity verification
- Balance batch period vs. latency requirements (typical periods: 10-100ms)
- Consider byte alignment for easier extraction and better performance
- Maintain big-endian byte order for automotive compatibility

**Trade-offs:**
- Individual signal latency increases (signals wait for batch cycle)
- All signals in a batch update atomically, which may not suit all applications
- More complex encoding/decoding logic required
- Testing complexity increases due to tightly coupled signals

Message batching typically reduces bus load by 30-70% depending on the application, making it essential for bandwidth-constrained CAN systems while requiring careful design to balance efficiency with real-time requirements.