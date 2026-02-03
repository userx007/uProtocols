# PROFIsafe Protocol: Safety-Related Communication over PROFIBUS

## Detailed Description

PROFIsafe is a safety protocol that enables safety-related communication over standard PROFIBUS networks, achieving Safety Integrity Level (SIL) 3 according to IEC 61508. It allows safety-critical devices (emergency stops, light curtains, safety PLCs) to communicate alongside standard automation devices on the same physical network without requiring dedicated safety cabling.

### Key Concepts

**Safety Layer Architecture:**
PROFIsafe operates as an additional software layer on top of standard PROFIBUS DP/PA communication. The protocol uses the "black channel" principle - it assumes the underlying communication channel is unreliable and implements comprehensive safety mechanisms at the application layer.

**Safety Mechanisms:**

1. **Consecutive Number (CRC)**: Each safety message includes a running counter to detect message loss, repetition, or incorrect sequence
2. **CRC Checksum**: Cryptographic checksums ensure data integrity
3. **Timeout Monitoring**: Watchdog timers detect communication failures
4. **Source/Destination Addressing**: Prevents messages from reaching wrong recipients
5. **Data Integrity**: Multiple layers of error detection

**F-Parameters:**
- **F_Source_Address**: Unique identifier for safety sender
- **F_Dest_Address**: Unique identifier for safety receiver  
- **F_WD_Time**: Watchdog timeout period
- **F_CRC_Length**: CRC algorithm selection (2, 3, or 4 bytes)
- **F_Par_Version**: Parameter version for configuration consistency

**Safety Frame Structure:**
```
[Status/Control] [Process Data] [CRC] [Consecutive Number]
```

## C/C++ Programming Examples

### Example 1: Basic PROFIsafe Frame Construction

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// PROFIsafe frame structure
typedef struct {
    uint8_t control_byte;      // Status and control information
    uint8_t process_data[12];  // Safety process data (up to 12 bytes)
    uint8_t data_length;       // Actual data length
    uint32_t crc;              // CRC checksum (2-4 bytes depending on config)
    uint16_t consecutive_num;  // Running counter
    uint16_t f_source_addr;    // Source address
    uint16_t f_dest_addr;      // Destination address
} profisafe_frame_t;

// PROFIsafe device configuration
typedef struct {
    uint16_t f_source_addr;
    uint16_t f_dest_addr;
    uint32_t f_wd_time;        // Watchdog time in ms
    uint8_t f_crc_length;      // 2, 3, or 4 bytes
    uint8_t f_par_version;
    bool is_configured;
} profisafe_config_t;

// CRC-32 calculation for PROFIsafe (simplified)
uint32_t profisafe_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint32_t polynomial = 0x1EDC6F41;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// Build a PROFIsafe safety telegram
int profisafe_build_frame(profisafe_frame_t *frame, 
                          const profisafe_config_t *config,
                          const uint8_t *safety_data, 
                          uint8_t data_len,
                          bool is_device_fault) {
    if (data_len > 12 || !config->is_configured) {
        return -1;
    }
    
    // Set control byte
    frame->control_byte = 0x00;
    if (is_device_fault) {
        frame->control_byte |= 0x01;  // Device fault bit
    }
    
    // Copy process data
    memcpy(frame->process_data, safety_data, data_len);
    frame->data_length = data_len;
    
    // Set addressing
    frame->f_source_addr = config->f_source_addr;
    frame->f_dest_addr = config->f_dest_addr;
    
    // Increment consecutive number (wraps at 0xFFFF)
    static uint16_t consecutive_counter = 0;
    frame->consecutive_num = consecutive_counter++;
    
    // Calculate CRC over control byte, data, and consecutive number
    uint8_t crc_buffer[32];
    size_t crc_offset = 0;
    
    crc_buffer[crc_offset++] = frame->control_byte;
    memcpy(&crc_buffer[crc_offset], frame->process_data, data_len);
    crc_offset += data_len;
    crc_buffer[crc_offset++] = (frame->consecutive_num >> 8) & 0xFF;
    crc_buffer[crc_offset++] = frame->consecutive_num & 0xFF;
    
    frame->crc = profisafe_crc32(crc_buffer, crc_offset);
    
    return 0;
}

// Verify received PROFIsafe frame
bool profisafe_verify_frame(const profisafe_frame_t *frame,
                            const profisafe_config_t *config,
                            uint16_t expected_consecutive) {
    // Verify addressing
    if (frame->f_dest_addr != config->f_source_addr ||
        frame->f_source_addr != config->f_dest_addr) {
        return false;
    }
    
    // Verify consecutive number
    if (frame->consecutive_num != expected_consecutive) {
        return false;
    }
    
    // Recalculate and verify CRC
    uint8_t crc_buffer[32];
    size_t crc_offset = 0;
    
    crc_buffer[crc_offset++] = frame->control_byte;
    memcpy(&crc_buffer[crc_offset], frame->process_data, frame->data_length);
    crc_offset += frame->data_length;
    crc_buffer[crc_offset++] = (frame->consecutive_num >> 8) & 0xFF;
    crc_buffer[crc_offset++] = frame->consecutive_num & 0xFF;
    
    uint32_t calculated_crc = profisafe_crc32(crc_buffer, crc_offset);
    
    return (calculated_crc == frame->crc);
}
```

### Example 2: Safety Device Implementation with Watchdog

```cpp
#include <chrono>
#include <mutex>
#include <atomic>

class PROFIsafeSafetyDevice {
private:
    profisafe_config_t config_;
    std::atomic<bool> safety_mode_;
    std::atomic<bool> watchdog_expired_;
    std::chrono::steady_clock::time_point last_valid_msg_;
    std::mutex config_mutex_;
    uint16_t expected_consecutive_;
    
public:
    PROFIsafeSafetyDevice(uint16_t source_addr, uint16_t dest_addr) {
        config_.f_source_addr = source_addr;
        config_.f_dest_addr = dest_addr;
        config_.f_wd_time = 100;  // 100ms watchdog
        config_.f_crc_length = 4;  // 4-byte CRC
        config_.f_par_version = 1;
        config_.is_configured = false;
        safety_mode_ = true;  // Start in safe state
        watchdog_expired_ = false;
        expected_consecutive_ = 0;
    }
    
    // Configure F-parameters (must be done before operation)
    bool configure_f_parameters(uint32_t wd_time, uint8_t crc_length) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        if (wd_time < 10 || wd_time > 65535) {
            return false;  // Invalid watchdog time
        }
        
        if (crc_length != 2 && crc_length != 3 && crc_length != 4) {
            return false;  // Invalid CRC length
        }
        
        config_.f_wd_time = wd_time;
        config_.f_crc_length = crc_length;
        config_.is_configured = true;
        
        return true;
    }
    
    // Send safety data
    int send_safety_data(const uint8_t *data, uint8_t length) {
        if (!config_.is_configured) {
            return -1;
        }
        
        profisafe_frame_t frame;
        bool device_fault = watchdog_expired_.load();
        
        if (profisafe_build_frame(&frame, &config_, data, length, 
                                  device_fault) != 0) {
            return -1;
        }
        
        // Here you would transmit the frame over PROFIBUS
        // This is a placeholder for actual bus transmission
        // transmit_profibus_frame(&frame);
        
        return 0;
    }
    
    // Receive and process safety data
    bool receive_safety_data(const profisafe_frame_t *frame, 
                             uint8_t *output_data, 
                             uint8_t *output_len) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        // Verify frame integrity
        if (!profisafe_verify_frame(frame, &config_, expected_consecutive_)) {
            // Invalid frame - enter safe state
            safety_mode_ = true;
            return false;
        }
        
        // Check watchdog timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_valid_msg_).count();
        
        if (config_.is_configured && elapsed > config_.f_wd_time) {
            watchdog_expired_ = true;
            safety_mode_ = true;
            return false;
        }
        
        // Frame valid - update state
        last_valid_msg_ = now;
        watchdog_expired_ = false;
        expected_consecutive_ = (frame->consecutive_num + 1) & 0xFFFF;
        
        // Check if device reports fault
        if (frame->control_byte & 0x01) {
            safety_mode_ = true;
            return false;
        }
        
        // Process safety data
        safety_mode_ = false;
        memcpy(output_data, frame->process_data, frame->data_length);
        *output_len = frame->data_length;
        
        return true;
    }
    
    // Check if device is in safe state
    bool is_in_safe_state() const {
        return safety_mode_.load();
    }
    
    // Monitor watchdog (call periodically)
    void monitor_watchdog() {
        if (!config_.is_configured) return;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_valid_msg_).count();
        
        if (elapsed > config_.f_wd_time) {
            watchdog_expired_ = true;
            safety_mode_ = true;
        }
    }
};
```

## Rust Programming Examples

### Example 1: Type-Safe PROFIsafe Implementation

```rust
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};

// PROFIsafe error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PROFIsafeError {
    InvalidCRC,
    InvalidConsecutiveNumber,
    InvalidAddress,
    WatchdogTimeout,
    ConfigurationError,
    InvalidDataLength,
}

// F-Parameter configuration
#[derive(Debug, Clone)]
pub struct FParameters {
    pub f_source_addr: u16,
    pub f_dest_addr: u16,
    pub f_wd_time: Duration,
    pub f_crc_length: u8,
    pub f_par_version: u8,
}

impl FParameters {
    pub fn new(source: u16, dest: u16, wd_time_ms: u32) -> Result<Self, PROFIsafeError> {
        if wd_time_ms < 10 || wd_time_ms > 65535 {
            return Err(PROFIsafeError::ConfigurationError);
        }
        
        Ok(FParameters {
            f_source_addr: source,
            f_dest_addr: dest,
            f_wd_time: Duration::from_millis(wd_time_ms as u64),
            f_crc_length: 4,
            f_par_version: 1,
        })
    }
}

// Safety frame structure
#[derive(Debug, Clone)]
pub struct SafetyFrame {
    pub control_byte: u8,
    pub process_data: Vec<u8>,
    pub crc: u32,
    pub consecutive_num: u16,
}

impl SafetyFrame {
    const MAX_DATA_LENGTH: usize = 12;
    
    pub fn new(data: &[u8], consecutive: u16, device_fault: bool) 
        -> Result<Self, PROFIsafeError> {
        if data.len() > Self::MAX_DATA_LENGTH {
            return Err(PROFIsafeError::InvalidDataLength);
        }
        
        let mut control_byte = 0u8;
        if device_fault {
            control_byte |= 0x01;
        }
        
        let mut frame = SafetyFrame {
            control_byte,
            process_data: data.to_vec(),
            crc: 0,
            consecutive_num: consecutive,
        };
        
        frame.crc = Self::calculate_crc(&frame);
        Ok(frame)
    }
    
    fn calculate_crc(frame: &SafetyFrame) -> u32 {
        let mut buffer = Vec::new();
        buffer.push(frame.control_byte);
        buffer.extend_from_slice(&frame.process_data);
        buffer.push((frame.consecutive_num >> 8) as u8);
        buffer.push((frame.consecutive_num & 0xFF) as u8);
        
        crc32_profisafe(&buffer)
    }
    
    pub fn verify_crc(&self) -> bool {
        let calculated = Self::calculate_crc(self);
        calculated == self.crc
    }
}

// CRC-32 implementation for PROFIsafe
fn crc32_profisafe(data: &[u8]) -> u32 {
    const POLYNOMIAL: u32 = 0x1EDC6F41;
    let mut crc: u32 = 0xFFFFFFFF;
    
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    !crc
}

// PROFIsafe safety device
pub struct SafetyDevice {
    config: FParameters,
    safety_mode: Arc<Mutex<bool>>,
    last_valid_msg: Arc<Mutex<Instant>>,
    expected_consecutive: Arc<Mutex<u16>>,
    consecutive_counter: Arc<Mutex<u16>>,
}

impl SafetyDevice {
    pub fn new(config: FParameters) -> Self {
        SafetyDevice {
            config,
            safety_mode: Arc::new(Mutex::new(true)),
            last_valid_msg: Arc::new(Mutex::new(Instant::now())),
            expected_consecutive: Arc::new(Mutex::new(0)),
            consecutive_counter: Arc::new(Mutex::new(0)),
        }
    }
    
    pub fn send_safety_data(&self, data: &[u8]) -> Result<SafetyFrame, PROFIsafeError> {
        let mut counter = self.consecutive_counter.lock().unwrap();
        let consecutive = *counter;
        *counter = counter.wrapping_add(1);
        
        let is_fault = *self.safety_mode.lock().unwrap();
        SafetyFrame::new(data, consecutive, is_fault)
    }
    
    pub fn receive_safety_data(&self, frame: &SafetyFrame) 
        -> Result<Vec<u8>, PROFIsafeError> {
        // Verify CRC
        if !frame.verify_crc() {
            *self.safety_mode.lock().unwrap() = true;
            return Err(PROFIsafeError::InvalidCRC);
        }
        
        // Verify consecutive number
        let mut expected = self.expected_consecutive.lock().unwrap();
        if frame.consecutive_num != *expected {
            *self.safety_mode.lock().unwrap() = true;
            return Err(PROFIsafeError::InvalidConsecutiveNumber);
        }
        *expected = expected.wrapping_add(1);
        
        // Check watchdog timeout
        let mut last_msg = self.last_valid_msg.lock().unwrap();
        if last_msg.elapsed() > self.config.f_wd_time {
            *self.safety_mode.lock().unwrap() = true;
            return Err(PROFIsafeError::WatchdogTimeout);
        }
        *last_msg = Instant::now();
        
        // Check device fault bit
        if frame.control_byte & 0x01 != 0 {
            *self.safety_mode.lock().unwrap() = true;
            return Ok(frame.process_data.clone());
        }
        
        // Valid frame - exit safe mode
        *self.safety_mode.lock().unwrap() = false;
        Ok(frame.process_data.clone())
    }
    
    pub fn is_in_safe_state(&self) -> bool {
        *self.safety_mode.lock().unwrap()
    }
    
    pub fn monitor_watchdog(&self) {
        let last_msg = self.last_valid_msg.lock().unwrap();
        if last_msg.elapsed() > self.config.f_wd_time {
            *self.safety_mode.lock().unwrap() = true;
        }
    }
}
```

### Example 2: Emergency Stop System

```rust
use std::thread;
use std::time::Duration;

// Emergency stop button states
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum EmergencyStopState {
    Released = 0,
    Pressed = 1,
    Error = 2,
}

pub struct EmergencyStopSystem {
    device: SafetyDevice,
    button_state: Arc<Mutex<EmergencyStopState>>,
}

impl EmergencyStopSystem {
    pub fn new(source_addr: u16, dest_addr: u16) -> Result<Self, PROFIsafeError> {
        let config = FParameters::new(source_addr, dest_addr, 100)?;
        
        Ok(EmergencyStopSystem {
            device: SafetyDevice::new(config),
            button_state: Arc::new(Mutex::new(EmergencyStopState::Released)),
        })
    }
    
    pub fn set_button_state(&self, state: EmergencyStopState) {
        *self.button_state.lock().unwrap() = state;
    }
    
    pub fn transmit_safety_state(&self) -> Result<(), PROFIsafeError> {
        let state = *self.button_state.lock().unwrap();
        let data = vec![state as u8];
        
        let frame = self.device.send_safety_data(&data)?;
        
        // Transmit frame over PROFIBUS (placeholder)
        println!("TX Safety Frame: State={:?}, CRC={:08X}, CN={}", 
                 state, frame.crc, frame.consecutive_num);
        
        Ok(())
    }
    
    pub fn receive_safety_state(&self, frame: &SafetyFrame) 
        -> Result<EmergencyStopState, PROFIsafeError> {
        let data = self.device.receive_safety_data(frame)?;
        
        if data.is_empty() {
            return Err(PROFIsafeError::InvalidDataLength);
        }
        
        let state = match data[0] {
            0 => EmergencyStopState::Released,
            1 => EmergencyStopState::Pressed,
            _ => EmergencyStopState::Error,
        };
        
        println!("RX Safety Frame: State={:?}", state);
        Ok(state)
    }
    
    pub fn run_cyclic_transmission(&self, cycle_time_ms: u64) {
        loop {
            if let Err(e) = self.transmit_safety_state() {
                eprintln!("Safety transmission error: {:?}", e);
            }
            
            self.device.monitor_watchdog();
            thread::sleep(Duration::from_millis(cycle_time_ms));
        }
    }
}

// Example usage
fn main() {
    let estop = EmergencyStopSystem::new(0x0001, 0x0002).unwrap();
    
    // Simulate emergency stop press
    estop.set_button_state(EmergencyStopState::Pressed);
    estop.transmit_safety_state().unwrap();
    
    // Simulate receiving safety state from partner
    let rx_frame = SafetyFrame::new(&[1u8], 0, false).unwrap();
    match estop.receive_safety_state(&rx_frame) {
        Ok(state) => println!("Partner state: {:?}", state),
        Err(e) => eprintln!("Error: {:?}", e),
    }
}
```

## Summary

**PROFIsafe** enables SIL 3 safety communication over standard PROFIBUS networks through sophisticated protocol mechanisms. It uses the black channel principle, assuming the underlying network is unreliable and implementing multiple safety layers including CRC checksums, consecutive numbering, timeout monitoring, and addressing verification.

**Key features**: Operates as a software layer atop PROFIBUS DP, achieves SIL 3 certification, supports up to 12 bytes of safety data per frame, configurable watchdog times (10-65535ms), and multiple CRC lengths (2-4 bytes). The protocol ensures fail-safe behavior - any error triggers immediate transition to a safe state.

**Implementation considerations**: Proper F-parameter configuration is critical, watchdog monitoring must be continuous, consecutive number tracking prevents replay attacks, and all safety devices must start in safe state. The examples demonstrate frame construction/verification in C/C++ and type-safe implementations in Rust with emergency stop system integration.

PROFIsafe enables cost-effective safety automation by eliminating dedicated safety networks while maintaining highest safety integrity levels through robust protocol design.