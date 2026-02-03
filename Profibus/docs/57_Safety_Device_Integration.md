# Safety Device Integration with PROFIsafe

## Detailed Description

### Overview
Safety Device Integration in Profibus networks involves connecting safety-critical components such as safety PLCs, emergency stop buttons, safety light curtains, and safety sensors through the PROFIsafe protocol. PROFIsafe is a safety-oriented extension of standard Profibus that enables the transmission of safety-related data alongside standard automation data on the same network infrastructure.

### PROFIsafe Protocol

PROFIsafe (defined in IEC 61784-3-3) achieves safety integrity levels up to SIL 3 (Safety Integrity Level 3) according to IEC 61508 and Performance Level e (PLe) according to ISO 13849-1. The protocol implements several safety mechanisms:

1. **Consecutive Number (CRC)**: Each safety message includes a consecutive counter to detect message loss, repetition, or incorrect sequence
2. **Time Monitoring**: Watchdog timers ensure communication occurs within defined intervals
3. **Data Integrity**: CRC checksums verify data hasn't been corrupted during transmission
4. **Source and Destination Addressing**: Ensures messages come from the correct sender and reach the intended receiver
5. **Timeout Monitoring**: Detects communication failures

### Safety Architecture

A typical safety integration consists of:

- **Safety Controller/PLC**: Central safety logic processor (e.g., Siemens F-CPU, Phoenix Contact SafetyBridge)
- **Safety I/O Modules**: Distributed safety inputs/outputs connected via Profibus DP
- **Safety Sensors**: Emergency stops, safety doors, light curtains, two-hand controls
- **Safety Actuators**: Safe motor controls, safety relays, brake controls
- **Standard I/O**: Non-safety devices sharing the same network

### PROFIsafe Frame Structure

A PROFIsafe telegram contains:
- **Safety Data**: 1-12 bytes of actual safety I/O information
- **Control Byte**: Status and control information
- **CRC**: 2 or 4 bytes for data integrity checking
- **Consecutive Number**: 2 bytes for message sequence verification

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// PROFIsafe frame structure
#define PROFISAFE_MAX_DATA_LEN 12
#define PROFISAFE_CRC_2BYTE 2
#define PROFISAFE_CRC_4BYTE 4

// Safety status bits
#define SAFETY_STATUS_OK        0x00
#define SAFETY_STATUS_FAULT     0x01
#define SAFETY_STATUS_TIMEOUT   0x02
#define SAFETY_STATUS_CRC_ERROR 0x04

// PROFIsafe telegram structure
typedef struct {
    uint8_t data[PROFISAFE_MAX_DATA_LEN];  // Safety data
    uint8_t data_length;                    // Actual data length
    uint8_t control_byte;                   // Control/status byte
    uint16_t consecutive_number;            // Message sequence counter
    uint32_t crc;                           // CRC checksum
    bool use_crc4;                          // Use 4-byte CRC (true) or 2-byte (false)
} profisafe_frame_t;

// Safety device context
typedef struct {
    uint8_t device_address;
    uint16_t expected_seq_num;
    uint32_t timeout_ms;
    uint32_t last_receive_time;
    uint8_t status;
    bool initialized;
} safety_device_t;

// CRC calculation for PROFIsafe (simplified CRC-16-CCITT)
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// CRC-32 for extended safety (simplified)
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// Build a PROFIsafe frame
void profisafe_build_frame(profisafe_frame_t *frame, const uint8_t *safety_data,
                           uint8_t data_len, uint16_t seq_num) {
    // Copy safety data
    memcpy(frame->data, safety_data, data_len);
    frame->data_length = data_len;
    frame->consecutive_number = seq_num;
    frame->control_byte = 0x00; // Normal operation
    
    // Prepare buffer for CRC calculation
    uint8_t crc_buffer[PROFISAFE_MAX_DATA_LEN + 3];
    memcpy(crc_buffer, frame->data, data_len);
    crc_buffer[data_len] = frame->control_byte;
    crc_buffer[data_len + 1] = (uint8_t)(seq_num >> 8);
    crc_buffer[data_len + 2] = (uint8_t)(seq_num & 0xFF);
    
    // Calculate CRC
    if (frame->use_crc4) {
        frame->crc = calculate_crc32(crc_buffer, data_len + 3);
    } else {
        frame->crc = calculate_crc16(crc_buffer, data_len + 3);
    }
}

// Validate received PROFIsafe frame
bool profisafe_validate_frame(const profisafe_frame_t *frame, safety_device_t *device) {
    // Build buffer for CRC verification
    uint8_t crc_buffer[PROFISAFE_MAX_DATA_LEN + 3];
    memcpy(crc_buffer, frame->data, frame->data_length);
    crc_buffer[frame->data_length] = frame->control_byte;
    crc_buffer[frame->data_length + 1] = (uint8_t)(frame->consecutive_number >> 8);
    crc_buffer[frame->data_length + 2] = (uint8_t)(frame->consecutive_number & 0xFF);
    
    // Verify CRC
    uint32_t calculated_crc;
    if (frame->use_crc4) {
        calculated_crc = calculate_crc32(crc_buffer, frame->data_length + 3);
    } else {
        calculated_crc = calculate_crc16(crc_buffer, frame->data_length + 3);
    }
    
    if (calculated_crc != frame->crc) {
        device->status |= SAFETY_STATUS_CRC_ERROR;
        return false;
    }
    
    // Verify consecutive number
    if (device->initialized) {
        uint16_t expected = (device->expected_seq_num + 1) & 0xFFFF;
        if (frame->consecutive_number != expected) {
            device->status |= SAFETY_STATUS_FAULT;
            return false;
        }
    }
    
    device->expected_seq_num = frame->consecutive_number;
    device->initialized = true;
    device->status = SAFETY_STATUS_OK;
    return true;
}

// Emergency Stop Device Implementation
typedef struct {
    safety_device_t base;
    bool estop_activated;      // Emergency stop status
    bool reset_button;         // Reset button status
    uint8_t channel_1;         // Redundant channel 1
    uint8_t channel_2;         // Redundant channel 2
    bool discrepancy_detected; // Channel mismatch
} emergency_stop_device_t;

// Read emergency stop status from PROFIsafe frame
void estop_process_frame(emergency_stop_device_t *estop, const profisafe_frame_t *frame) {
    if (!profisafe_validate_frame(frame, &estop->base)) {
        // Safety fault - enter safe state
        estop->estop_activated = true;
        return;
    }
    
    // Parse redundant channels (typical dual-channel emergency stop)
    estop->channel_1 = frame->data[0];
    estop->channel_2 = frame->data[1];
    estop->reset_button = (frame->data[2] & 0x01) != 0;
    
    // Check for discrepancy between redundant channels
    if (estop->channel_1 != estop->channel_2) {
        estop->discrepancy_detected = true;
        estop->estop_activated = true;
    } else {
        estop->discrepancy_detected = false;
        estop->estop_activated = (estop->channel_1 == 0); // 0 = activated
    }
}

// Safety PLC Master Example
typedef struct {
    uint16_t tx_sequence_number;
    uint16_t rx_sequence_number;
    bool safety_mode_active;
    uint8_t num_safety_devices;
    emergency_stop_device_t estop_devices[8];
} safety_plc_t;

// Initialize safety PLC
void safety_plc_init(safety_plc_t *plc) {
    plc->tx_sequence_number = 0;
    plc->rx_sequence_number = 0;
    plc->safety_mode_active = true;
    plc->num_safety_devices = 0;
}

// Add emergency stop device
void safety_plc_add_estop(safety_plc_t *plc, uint8_t device_addr, uint32_t timeout_ms) {
    if (plc->num_safety_devices < 8) {
        emergency_stop_device_t *estop = &plc->estop_devices[plc->num_safety_devices];
        estop->base.device_address = device_addr;
        estop->base.timeout_ms = timeout_ms;
        estop->base.status = SAFETY_STATUS_OK;
        estop->base.initialized = false;
        estop->estop_activated = true; // Safe default
        plc->num_safety_devices++;
    }
}

// Check if all safety devices are OK
bool safety_plc_all_devices_ok(const safety_plc_t *plc) {
    for (uint8_t i = 0; i < plc->num_safety_devices; i++) {
        if (plc->estop_devices[i].estop_activated) {
            return false;
        }
        if (plc->estop_devices[i].base.status != SAFETY_STATUS_OK) {
            return false;
        }
    }
    return true;
}

// Example usage
void example_safety_integration(void) {
    safety_plc_t safety_plc;
    safety_plc_init(&safety_plc);
    
    // Add emergency stop devices
    safety_plc_add_estop(&safety_plc, 0x10, 100); // Device 1, 100ms timeout
    safety_plc_add_estop(&safety_plc, 0x11, 100); // Device 2, 100ms timeout
    
    // Simulate receiving PROFIsafe frame
    profisafe_frame_t rx_frame;
    rx_frame.data[0] = 0x01; // Channel 1: Not activated
    rx_frame.data[1] = 0x01; // Channel 2: Not activated
    rx_frame.data[2] = 0x00; // Reset button: Not pressed
    rx_frame.data_length = 3;
    rx_frame.consecutive_number = 1;
    rx_frame.use_crc4 = false;
    
    // Build complete frame with CRC
    uint8_t safety_data[] = {0x01, 0x01, 0x00};
    profisafe_build_frame(&rx_frame, safety_data, 3, 1);
    
    // Process frame
    estop_process_frame(&safety_plc.estop_devices[0], &rx_frame);
    
    // Check safety status
    if (safety_plc_all_devices_ok(&safety_plc)) {
        // Safe to operate
        printf("System safe - all emergency stops released\n");
    } else {
        // Safety fault
        printf("SAFETY FAULT - Emergency stop activated\n");
    }
}
```

### Rust Implementation

```rust
use std::time::{Duration, Instant};

// Safety status flags
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SafetyStatus {
    Ok,
    Fault,
    Timeout,
    CrcError,
}

// PROFIsafe frame structure
#[derive(Debug, Clone)]
pub struct ProfisafeFrame {
    pub data: Vec<u8>,
    pub control_byte: u8,
    pub consecutive_number: u16,
    pub crc: u32,
    pub use_crc4: bool,
}

impl ProfisafeFrame {
    pub fn new(data: Vec<u8>, seq_num: u16, use_crc4: bool) -> Self {
        let mut frame = ProfisafeFrame {
            data,
            control_byte: 0x00,
            consecutive_number: seq_num,
            crc: 0,
            use_crc4,
        };
        frame.calculate_crc();
        frame
    }

    // Calculate CRC-16-CCITT
    fn crc16(data: &[u8]) -> u16 {
        let mut crc: u16 = 0xFFFF;
        
        for &byte in data {
            crc ^= (byte as u16) << 8;
            for _ in 0..8 {
                if crc & 0x8000 != 0 {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc <<= 1;
                }
            }
        }
        crc
    }

    // Calculate CRC-32
    fn crc32(data: &[u8]) -> u32 {
        let mut crc: u32 = 0xFFFFFFFF;
        
        for &byte in data {
            crc ^= byte as u32;
            for _ in 0..8 {
                if crc & 1 != 0 {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
        !crc
    }

    fn calculate_crc(&mut self) {
        // Build CRC buffer: data + control_byte + consecutive_number
        let mut crc_buffer = self.data.clone();
        crc_buffer.push(self.control_byte);
        crc_buffer.push((self.consecutive_number >> 8) as u8);
        crc_buffer.push((self.consecutive_number & 0xFF) as u8);

        self.crc = if self.use_crc4 {
            Self::crc32(&crc_buffer)
        } else {
            Self::crc16(&crc_buffer) as u32
        };
    }

    pub fn validate(&self) -> bool {
        let mut crc_buffer = self.data.clone();
        crc_buffer.push(self.control_byte);
        crc_buffer.push((self.consecutive_number >> 8) as u8);
        crc_buffer.push((self.consecutive_number & 0xFF) as u8);

        let calculated_crc = if self.use_crc4 {
            Self::crc32(&crc_buffer)
        } else {
            Self::crc16(&crc_buffer) as u32
        };

        calculated_crc == self.crc
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = self.data.clone();
        bytes.push(self.control_byte);
        bytes.push((self.consecutive_number >> 8) as u8);
        bytes.push((self.consecutive_number & 0xFF) as u8);
        
        if self.use_crc4 {
            bytes.extend_from_slice(&self.crc.to_le_bytes());
        } else {
            bytes.extend_from_slice(&(self.crc as u16).to_le_bytes());
        }
        bytes
    }
}

// Safety device base
pub struct SafetyDevice {
    pub address: u8,
    pub expected_seq_num: u16,
    pub timeout: Duration,
    pub last_receive_time: Option<Instant>,
    pub status: SafetyStatus,
    pub initialized: bool,
}

impl SafetyDevice {
    pub fn new(address: u8, timeout: Duration) -> Self {
        SafetyDevice {
            address,
            expected_seq_num: 0,
            timeout,
            last_receive_time: None,
            status: SafetyStatus::Fault,
            initialized: false,
        }
    }

    pub fn validate_frame(&mut self, frame: &ProfisafeFrame) -> Result<(), SafetyStatus> {
        // Verify CRC
        if !frame.validate() {
            self.status = SafetyStatus::CrcError;
            return Err(SafetyStatus::CrcError);
        }

        // Check sequence number
        if self.initialized {
            let expected = self.expected_seq_num.wrapping_add(1);
            if frame.consecutive_number != expected {
                self.status = SafetyStatus::Fault;
                return Err(SafetyStatus::Fault);
            }
        }

        self.expected_seq_num = frame.consecutive_number;
        self.initialized = true;
        self.last_receive_time = Some(Instant::now());
        self.status = SafetyStatus::Ok;
        Ok(())
    }

    pub fn check_timeout(&mut self) -> bool {
        if let Some(last_time) = self.last_receive_time {
            if last_time.elapsed() > self.timeout {
                self.status = SafetyStatus::Timeout;
                return true;
            }
        }
        false
    }
}

// Emergency Stop Device
pub struct EmergencyStopDevice {
    pub base: SafetyDevice,
    pub estop_activated: bool,
    pub reset_button: bool,
    pub channel_1: u8,
    pub channel_2: u8,
    pub discrepancy_detected: bool,
}

impl EmergencyStopDevice {
    pub fn new(address: u8, timeout_ms: u64) -> Self {
        EmergencyStopDevice {
            base: SafetyDevice::new(address, Duration::from_millis(timeout_ms)),
            estop_activated: true, // Safe default
            reset_button: false,
            channel_1: 0,
            channel_2: 0,
            discrepancy_detected: false,
        }
    }

    pub fn process_frame(&mut self, frame: &ProfisafeFrame) -> Result<(), String> {
        // Validate frame
        if let Err(status) = self.base.validate_frame(frame) {
            self.estop_activated = true; // Enter safe state on error
            return Err(format!("Frame validation failed: {:?}", status));
        }

        // Parse redundant channels
        if frame.data.len() < 3 {
            return Err("Insufficient data in frame".to_string());
        }

        self.channel_1 = frame.data[0];
        self.channel_2 = frame.data[1];
        self.reset_button = (frame.data[2] & 0x01) != 0;

        // Check for discrepancy
        if self.channel_1 != self.channel_2 {
            self.discrepancy_detected = true;
            self.estop_activated = true;
            return Err("Channel discrepancy detected".to_string());
        }

        self.discrepancy_detected = false;
        self.estop_activated = self.channel_1 == 0; // 0 = activated
        Ok(())
    }

    pub fn is_safe_to_operate(&self) -> bool {
        !self.estop_activated 
            && self.base.status == SafetyStatus::Ok
            && !self.discrepancy_detected
    }
}

// Safety Light Curtain
pub struct SafetyLightCurtain {
    pub base: SafetyDevice,
    pub beam_interrupted: bool,
    pub muting_active: bool,
    pub fault_detected: bool,
    pub override_active: bool,
}

impl SafetyLightCurtain {
    pub fn new(address: u8, timeout_ms: u64) -> Self {
        SafetyLightCurtain {
            base: SafetyDevice::new(address, Duration::from_millis(timeout_ms)),
            beam_interrupted: true, // Safe default
            muting_active: false,
            fault_detected: false,
            override_active: false,
        }
    }

    pub fn process_frame(&mut self, frame: &ProfisafeFrame) -> Result<(), String> {
        if let Err(status) = self.base.validate_frame(frame) {
            self.beam_interrupted = true; // Safe state
            return Err(format!("Frame validation failed: {:?}", status));
        }

        if frame.data.is_empty() {
            return Err("No data in frame".to_string());
        }

        let status_byte = frame.data[0];
        self.beam_interrupted = (status_byte & 0x01) != 0;
        self.muting_active = (status_byte & 0x02) != 0;
        self.fault_detected = (status_byte & 0x04) != 0;
        self.override_active = (status_byte & 0x08) != 0;

        Ok(())
    }

    pub fn is_safe_to_operate(&self) -> bool {
        (!self.beam_interrupted || self.muting_active)
            && !self.fault_detected
            && self.base.status == SafetyStatus::Ok
    }
}

// Safety PLC
pub struct SafetyPlc {
    pub tx_sequence: u16,
    pub emergency_stops: Vec<EmergencyStopDevice>,
    pub light_curtains: Vec<SafetyLightCurtain>,
    pub safety_mode_active: bool,
}

impl SafetyPlc {
    pub fn new() -> Self {
        SafetyPlc {
            tx_sequence: 0,
            emergency_stops: Vec::new(),
            light_curtains: Vec::new(),
            safety_mode_active: true,
        }
    }

    pub fn add_emergency_stop(&mut self, address: u8, timeout_ms: u64) {
        self.emergency_stops.push(EmergencyStopDevice::new(address, timeout_ms));
    }

    pub fn add_light_curtain(&mut self, address: u8, timeout_ms: u64) {
        self.light_curtains.push(SafetyLightCurtain::new(address, timeout_ms));
    }

    pub fn check_all_devices(&mut self) -> bool {
        // Check emergency stops
        for estop in &mut self.emergency_stops {
            estop.base.check_timeout();
            if !estop.is_safe_to_operate() {
                return false;
            }
        }

        // Check light curtains
        for curtain in &mut self.light_curtains {
            curtain.base.check_timeout();
            if !curtain.is_safe_to_operate() {
                return false;
            }
        }

        true
    }

    pub fn get_next_sequence(&mut self) -> u16 {
        let seq = self.tx_sequence;
        self.tx_sequence = self.tx_sequence.wrapping_add(1);
        seq
    }

    pub fn create_safety_output(&mut self, enable_outputs: bool) -> ProfisafeFrame {
        let data = vec![if enable_outputs { 0x01 } else { 0x00 }];
        let seq = self.get_next_sequence();
        ProfisafeFrame::new(data, seq, false)
    }
}

// Example usage
pub fn example_safety_system() {
    let mut safety_plc = SafetyPlc::new();
    
    // Add safety devices
    safety_plc.add_emergency_stop(0x10, 100); // 100ms timeout
    safety_plc.add_light_curtain(0x20, 100);

    // Simulate receiving emergency stop frame
    let estop_data = vec![0x01, 0x01, 0x00]; // Both channels OK, no reset
    let estop_frame = ProfisafeFrame::new(estop_data, 1, false);

    if let Some(estop) = safety_plc.emergency_stops.get_mut(0) {
        match estop.process_frame(&estop_frame) {
            Ok(_) => println!("Emergency stop: OK"),
            Err(e) => println!("Emergency stop error: {}", e),
        }
    }

    // Check overall safety status
    if safety_plc.check_all_devices() {
        println!("System safe - all safety devices OK");
        
        // Create safety output frame to enable machine
        let output_frame = safety_plc.create_safety_output(true);
        println!("Safety output frame: {} bytes", output_frame.to_bytes().len());
    } else {
        println!("SAFETY FAULT - Machine disabled");
    }
}

fn main() {
    example_safety_system();
}
```

## Summary

**Safety Device Integration with PROFIsafe** enables the connection of safety-critical components (emergency stops, safety PLCs, light curtains, safety sensors) to standard Profibus networks while maintaining required safety integrity levels up to SIL 3. The PROFIsafe protocol adds safety mechanisms including CRC checksums, consecutive numbering, timeout monitoring, and source/destination verification on top of standard Profibus communication.

**Key Points:**
- **Safety Integrity**: Achieves SIL 3 / PLe through redundant safety mechanisms
- **Black Channel Principle**: Safety functions are independent of the transmission medium
- **Dual-Channel Architecture**: Redundant sensor channels detect discrepancies
- **Deterministic Behavior**: Defined timeout and fault responses ensure safe states
- **Mixed Networks**: Safety and standard devices coexist on the same Profibus infrastructure

**Programming Considerations:**
- Always validate CRC and sequence numbers before processing safety data
- Implement watchdog timers and timeout detection
- Default to safe states (e.g., machine stopped) on communication errors
- Use redundant channels for critical inputs and verify consistency
- Follow fail-safe principles: any fault leads to the safe state
- Proper initialization sequences prevent startup in unsafe conditions

PROFIsafe integration is essential for modern industrial automation where safety and productivity must coexist on unified network infrastructures while meeting stringent safety standards.