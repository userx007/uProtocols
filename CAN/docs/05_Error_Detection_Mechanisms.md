# CAN Error Detection Mechanisms

Controller Area Network (CAN) is renowned for its robust error detection capabilities, making it highly reliable for safety-critical applications in automotive, industrial, and aerospace systems. The protocol implements multiple layers of error detection to ensure data integrity.

## Overview of CAN Error Detection

CAN employs five distinct error detection mechanisms that work together to achieve an extremely low undetected error probability (on the order of 4.7 × 10⁻¹¹):

1. **Cyclic Redundancy Check (CRC)**
2. **Frame Check**
3. **Acknowledgment (ACK) Check**
4. **Bit Monitoring**
5. **Bit Stuffing Rule Violation**

## 1. Cyclic Redundancy Check (CRC)

The CRC is a 15-bit checksum calculated over the Start of Frame (SOF), Arbitration Field, Control Field, and Data Field. The transmitter calculates the CRC and appends it to the frame. All receivers independently calculate the CRC and compare it with the received value.

### CRC Calculation Algorithm

The CAN CRC uses the polynomial: **x¹⁵ + x¹⁴ + x¹⁰ + x⁸ + x⁷ + x⁴ + x³ + 1**

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

#define CAN_CRC_POLYNOMIAL 0x4599  // CAN CRC-15 polynomial

// Calculate CRC for CAN frame
uint16_t calculate_can_crc(const uint8_t *data, size_t length) {
    uint16_t crc = 0;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            bool crc_bit = (crc & 0x4000) != 0;  // Check bit 14
            crc <<= 1;
            
            bool data_bit = (byte & (1 << bit)) != 0;
            
            if (crc_bit ^ data_bit) {
                crc ^= CAN_CRC_POLYNOMIAL;
            }
        }
    }
    
    return crc & 0x7FFF;  // Mask to 15 bits
}

// Verify CRC of received frame
bool verify_can_crc(const uint8_t *frame_data, size_t data_length, 
                     uint16_t received_crc) {
    uint16_t calculated_crc = calculate_can_crc(frame_data, data_length);
    return (calculated_crc == received_crc);
}

// Example usage in frame transmission
typedef struct {
    uint32_t id;           // CAN identifier
    uint8_t dlc;           // Data length code
    uint8_t data[8];       // Payload
    uint16_t crc;          // CRC field
} can_frame_t;

void prepare_can_frame(can_frame_t *frame) {
    // Build the bit stream for CRC calculation
    uint8_t crc_buffer[16];  // Worst case: SOF + 29-bit ID + control + 8 data bytes
    size_t bit_count = 0;
    
    // Add SOF, Arbitration, Control, and Data fields
    // (simplified - actual implementation would pack bits properly)
    
    // Calculate CRC
    frame->crc = calculate_can_crc(crc_buffer, bit_count / 8);
}
```

### Rust Implementation

```rust
const CAN_CRC_POLYNOMIAL: u16 = 0x4599;

pub struct CanFrame {
    pub id: u32,
    pub dlc: u8,
    pub data: [u8; 8],
    pub crc: u16,
}

impl CanFrame {
    /// Calculate CAN CRC-15 for the given data
    pub fn calculate_crc(data: &[u8]) -> u16 {
        let mut crc: u16 = 0;
        
        for &byte in data {
            for bit in (0..8).rev() {
                let crc_bit = (crc & 0x4000) != 0;  // Check bit 14
                crc <<= 1;
                
                let data_bit = (byte & (1 << bit)) != 0;
                
                if crc_bit ^ data_bit {
                    crc ^= CAN_CRC_POLYNOMIAL;
                }
            }
        }
        
        crc & 0x7FFF  // Mask to 15 bits
    }
    
    /// Verify the CRC of a received frame
    pub fn verify_crc(frame_data: &[u8], received_crc: u16) -> bool {
        let calculated_crc = Self::calculate_crc(frame_data);
        calculated_crc == received_crc
    }
    
    /// Prepare frame with CRC calculation
    pub fn new(id: u32, data: &[u8]) -> Result<Self, &'static str> {
        if data.len() > 8 {
            return Err("Data length exceeds 8 bytes");
        }
        
        let mut frame_data = [0u8; 8];
        frame_data[..data.len()].copy_from_slice(data);
        
        // Build bit stream for CRC (simplified)
        let mut crc_buffer = Vec::new();
        // Add SOF, arbitration, control fields...
        crc_buffer.extend_from_slice(data);
        
        let crc = Self::calculate_crc(&crc_buffer);
        
        Ok(CanFrame {
            id,
            dlc: data.len() as u8,
            data: frame_data,
            crc,
        })
    }
}
```

## 2. Frame Check

The frame check ensures the structure of the CAN frame is valid by verifying that fixed-format bits have their expected values:

- CRC delimiter must be recessive (1)
- ACK delimiter must be recessive (1)
- End of Frame (EOF) must be 7 recessive bits

### C/C++ Implementation

```c
#define CAN_RECESSIVE 1
#define CAN_DOMINANT 0

typedef enum {
    FRAME_CHECK_OK,
    FRAME_CHECK_CRC_DELIMITER_ERROR,
    FRAME_CHECK_ACK_DELIMITER_ERROR,
    FRAME_CHECK_EOF_ERROR
} frame_check_result_t;

frame_check_result_t check_can_frame_structure(const uint8_t *frame_bits, 
                                                 size_t frame_length) {
    // Locate fixed-format fields (positions depend on frame type)
    size_t crc_delim_pos = frame_length - 18;  // Example position
    size_t ack_delim_pos = frame_length - 10;
    size_t eof_start_pos = frame_length - 7;
    
    // Check CRC delimiter
    if (frame_bits[crc_delim_pos] != CAN_RECESSIVE) {
        return FRAME_CHECK_CRC_DELIMITER_ERROR;
    }
    
    // Check ACK delimiter
    if (frame_bits[ack_delim_pos] != CAN_RECESSIVE) {
        return FRAME_CHECK_ACK_DELIMITER_ERROR;
    }
    
    // Check EOF (7 consecutive recessive bits)
    for (size_t i = eof_start_pos; i < eof_start_pos + 7; i++) {
        if (frame_bits[i] != CAN_RECESSIVE) {
            return FRAME_CHECK_EOF_ERROR;
        }
    }
    
    return FRAME_CHECK_OK;
}
```

### Rust Implementation

```rust
#[derive(Debug, PartialEq)]
pub enum FrameCheckResult {
    Ok,
    CrcDelimiterError,
    AckDelimiterError,
    EofError,
}

const RECESSIVE: u8 = 1;
const DOMINANT: u8 = 0;

pub fn check_frame_structure(frame_bits: &[u8]) -> FrameCheckResult {
    let frame_length = frame_bits.len();
    
    // Calculate positions of fixed-format fields
    let crc_delim_pos = frame_length.saturating_sub(18);
    let ack_delim_pos = frame_length.saturating_sub(10);
    let eof_start_pos = frame_length.saturating_sub(7);
    
    // Check CRC delimiter
    if frame_bits.get(crc_delim_pos) != Some(&RECESSIVE) {
        return FrameCheckResult::CrcDelimiterError;
    }
    
    // Check ACK delimiter
    if frame_bits.get(ack_delim_pos) != Some(&RECESSIVE) {
        return FrameCheckResult::AckDelimiterError;
    }
    
    // Check EOF (7 consecutive recessive bits)
    for i in eof_start_pos..eof_start_pos + 7 {
        if frame_bits.get(i) != Some(&RECESSIVE) {
            return FrameCheckResult::EofError;
        }
    }
    
    FrameCheckResult::Ok
}
```

## 3. Acknowledgment (ACK) Check

Every transmitting node monitors the ACK slot. If no receiver sends a dominant bit in the ACK slot, the transmitter detects an ACK error. This indicates that no node successfully received the frame.

### C/C++ Implementation

```c
typedef enum {
    ACK_OK,
    ACK_ERROR
} ack_status_t;

typedef struct {
    bool is_transmitter;
    uint8_t ack_slot_bit;
} can_node_t;

// Transmitter checks if ACK was received
ack_status_t check_ack_transmitter(const can_node_t *node) {
    if (!node->is_transmitter) {
        return ACK_OK;  // Only transmitter checks ACK
    }
    
    // Transmitter sends recessive bit in ACK slot
    // If it reads back recessive, no receiver acknowledged
    if (node->ack_slot_bit == CAN_RECESSIVE) {
        return ACK_ERROR;
    }
    
    return ACK_OK;
}

// Receiver sends ACK for correctly received frame
void send_ack_receiver(can_node_t *node, bool frame_valid) {
    if (node->is_transmitter) {
        return;  // Only receivers send ACK
    }
    
    if (frame_valid) {
        // Send dominant bit in ACK slot
        node->ack_slot_bit = CAN_DOMINANT;
    } else {
        // Send recessive bit (no ACK)
        node->ack_slot_bit = CAN_RECESSIVE;
    }
}
```

### Rust Implementation

```rust
#[derive(Debug, PartialEq)]
pub enum AckStatus {
    Ok,
    Error,
}

pub struct CanNode {
    pub is_transmitter: bool,
    pub ack_slot_bit: u8,
}

impl CanNode {
    /// Transmitter checks if ACK was received
    pub fn check_ack_transmitter(&self) -> AckStatus {
        if !self.is_transmitter {
            return AckStatus::Ok;
        }
        
        // Transmitter sends recessive in ACK slot
        // If it reads recessive, no one acknowledged
        if self.ack_slot_bit == RECESSIVE {
            AckStatus::Error
        } else {
            AckStatus::Ok
        }
    }
    
    /// Receiver sends ACK for correctly received frame
    pub fn send_ack_receiver(&mut self, frame_valid: bool) {
        if self.is_transmitter {
            return;
        }
        
        self.ack_slot_bit = if frame_valid {
            DOMINANT  // Send ACK
        } else {
            RECESSIVE  // Don't ACK
        };
    }
}
```

## 4. Bit Monitoring

Each transmitting node monitors the bus while transmitting. If the bit value on the bus differs from the transmitted bit (except during arbitration and ACK slot), a bit error is detected.

### C/C++ Implementation

```c
typedef enum {
    BIT_MONITOR_OK,
    BIT_MONITOR_ERROR
} bit_monitor_result_t;

typedef struct {
    bool in_arbitration;
    bool in_ack_slot;
    uint8_t transmitted_bit;
    uint8_t monitored_bit;
} bit_monitor_t;

bit_monitor_result_t monitor_transmitted_bit(const bit_monitor_t *monitor) {
    // Don't check during arbitration or ACK slot
    if (monitor->in_arbitration || monitor->in_ack_slot) {
        return BIT_MONITOR_OK;
    }
    
    // Check if transmitted bit matches what's on the bus
    if (monitor->transmitted_bit != monitor->monitored_bit) {
        return BIT_MONITOR_ERROR;
    }
    
    return BIT_MONITOR_OK;
}

// Simulate bit transmission with monitoring
void transmit_with_monitoring(const uint8_t *data, size_t length,
                               bool *error_detected) {
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            bit_monitor_t monitor = {0};
            monitor.transmitted_bit = (byte >> bit) & 1;
            
            // Simulate bus reading
            monitor.monitored_bit = /* read from bus */;
            
            if (monitor_transmitted_bit(&monitor) == BIT_MONITOR_ERROR) {
                *error_detected = true;
                return;
            }
        }
    }
}
```

### Rust Implementation

```rust
#[derive(Debug, PartialEq)]
pub enum BitMonitorResult {
    Ok,
    Error,
}

pub struct BitMonitor {
    pub in_arbitration: bool,
    pub in_ack_slot: bool,
    pub transmitted_bit: u8,
    pub monitored_bit: u8,
}

impl BitMonitor {
    pub fn new() -> Self {
        BitMonitor {
            in_arbitration: false,
            in_ack_slot: false,
            transmitted_bit: 0,
            monitored_bit: 0,
        }
    }
    
    /// Monitor transmitted bit for errors
    pub fn check(&self) -> BitMonitorResult {
        // Don't check during arbitration or ACK slot
        if self.in_arbitration || self.in_ack_slot {
            return BitMonitorResult::Ok;
        }
        
        // Verify transmitted bit matches bus state
        if self.transmitted_bit != self.monitored_bit {
            BitMonitorResult::Error
        } else {
            BitMonitorResult::Ok
        }
    }
}

/// Transmit data with bit monitoring
pub fn transmit_with_monitoring(data: &[u8]) -> Result<(), &'static str> {
    for &byte in data {
        for bit in (0..8).rev() {
            let mut monitor = BitMonitor::new();
            monitor.transmitted_bit = (byte >> bit) & 1;
            
            // Simulate reading from bus
            monitor.monitored_bit = /* read from actual bus */;
            
            if monitor.check() == BitMonitorResult::Error {
                return Err("Bit monitoring error detected");
            }
        }
    }
    
    Ok(())
}
```

## 5. Bit Stuffing Rule Violation

CAN uses bit stuffing to ensure sufficient edges for synchronization. After five consecutive identical bits, a complementary bit is inserted. A violation of this rule indicates a transmission error.

**Bit Stuffing Rule**: After 5 consecutive bits of the same polarity, insert 1 bit of opposite polarity.

### C/C++ Implementation

```c
#define MAX_CONSECUTIVE_BITS 5

typedef struct {
    uint8_t consecutive_count;
    uint8_t last_bit;
    bool error_detected;
} bit_stuff_monitor_t;

// Initialize bit stuffing monitor
void init_bit_stuff_monitor(bit_stuff_monitor_t *monitor) {
    monitor->consecutive_count = 0;
    monitor->last_bit = 0xFF;  // Invalid initial value
    monitor->error_detected = false;
}

// Check for bit stuffing rule violation
bool check_bit_stuffing(bit_stuff_monitor_t *monitor, uint8_t current_bit,
                        bool expect_stuff_bit) {
    if (expect_stuff_bit) {
        // This should be a stuff bit (opposite of previous bits)
        if (current_bit == monitor->last_bit) {
            monitor->error_detected = true;
            return false;  // Stuff bit violation
        }
        // Reset counter after stuff bit
        monitor->consecutive_count = 1;
        monitor->last_bit = current_bit;
        return true;
    }
    
    // Check consecutive bits
    if (current_bit == monitor->last_bit) {
        monitor->consecutive_count++;
        
        if (monitor->consecutive_count > MAX_CONSECUTIVE_BITS) {
            // Missing stuff bit
            monitor->error_detected = true;
            return false;
        }
    } else {
        monitor->consecutive_count = 1;
        monitor->last_bit = current_bit;
    }
    
    return true;
}

// Encode data with bit stuffing
size_t encode_with_bit_stuffing(const uint8_t *input, size_t input_len,
                                 uint8_t *output, size_t output_max_len) {
    size_t output_bit_count = 0;
    uint8_t consecutive_count = 0;
    uint8_t last_bit = 0xFF;
    
    for (size_t i = 0; i < input_len; i++) {
        uint8_t byte = input[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t current_bit = (byte >> bit) & 1;
            
            // Write current bit
            if (output_bit_count / 8 >= output_max_len) {
                return 0;  // Output buffer too small
            }
            
            size_t byte_pos = output_bit_count / 8;
            size_t bit_pos = 7 - (output_bit_count % 8);
            
            if (current_bit) {
                output[byte_pos] |= (1 << bit_pos);
            } else {
                output[byte_pos] &= ~(1 << bit_pos);
            }
            output_bit_count++;
            
            // Check if stuff bit needed
            if (current_bit == last_bit) {
                consecutive_count++;
                
                if (consecutive_count == MAX_CONSECUTIVE_BITS) {
                    // Insert stuff bit
                    uint8_t stuff_bit = !current_bit;
                    
                    byte_pos = output_bit_count / 8;
                    bit_pos = 7 - (output_bit_count % 8);
                    
                    if (stuff_bit) {
                        output[byte_pos] |= (1 << bit_pos);
                    } else {
                        output[byte_pos] &= ~(1 << bit_pos);
                    }
                    output_bit_count++;
                    
                    consecutive_count = 1;
                    last_bit = stuff_bit;
                }
            } else {
                consecutive_count = 1;
                last_bit = current_bit;
            }
        }
    }
    
    return output_bit_count;
}
```

### Rust Implementation

```rust
const MAX_CONSECUTIVE_BITS: u8 = 5;

pub struct BitStuffMonitor {
    consecutive_count: u8,
    last_bit: Option<u8>,
    pub error_detected: bool,
}

impl BitStuffMonitor {
    pub fn new() -> Self {
        BitStuffMonitor {
            consecutive_count: 0,
            last_bit: None,
            error_detected: false,
        }
    }
    
    /// Check for bit stuffing rule violation
    pub fn check(&mut self, current_bit: u8, expect_stuff_bit: bool) -> bool {
        if expect_stuff_bit {
            // This should be a stuff bit (opposite of previous)
            if let Some(last) = self.last_bit {
                if current_bit == last {
                    self.error_detected = true;
                    return false;  // Stuff bit violation
                }
            }
            self.consecutive_count = 1;
            self.last_bit = Some(current_bit);
            return true;
        }
        
        // Check consecutive bits
        if let Some(last) = self.last_bit {
            if current_bit == last {
                self.consecutive_count += 1;
                
                if self.consecutive_count > MAX_CONSECUTIVE_BITS {
                    self.error_detected = true;
                    return false;  // Missing stuff bit
                }
            } else {
                self.consecutive_count = 1;
            }
        } else {
            self.consecutive_count = 1;
        }
        
        self.last_bit = Some(current_bit);
        true
    }
}

/// Encode data with bit stuffing
pub fn encode_with_bit_stuffing(input: &[u8]) -> Vec<u8> {
    let mut output_bits = Vec::new();
    let mut consecutive_count = 0;
    let mut last_bit = None;
    
    for &byte in input {
        for bit in (0..8).rev() {
            let current_bit = (byte >> bit) & 1;
            
            // Add current bit
            output_bits.push(current_bit);
            
            // Check if stuff bit needed
            if let Some(last) = last_bit {
                if current_bit == last {
                    consecutive_count += 1;
                    
                    if consecutive_count == MAX_CONSECUTIVE_BITS {
                        // Insert stuff bit
                        let stuff_bit = if current_bit == 0 { 1 } else { 0 };
                        output_bits.push(stuff_bit);
                        
                        consecutive_count = 1;
                        last_bit = Some(stuff_bit);
                        continue;
                    }
                } else {
                    consecutive_count = 1;
                }
            } else {
                consecutive_count = 1;
            }
            
            last_bit = Some(current_bit);
        }
    }
    
    // Convert bit vector to bytes
    let mut output = Vec::new();
    for chunk in output_bits.chunks(8) {
        let mut byte = 0u8;
        for (i, &bit) in chunk.iter().enumerate() {
            byte |= bit << (7 - i);
        }
        output.push(byte);
    }
    
    output
}

/// Decode data by removing stuff bits
pub fn decode_with_bit_stuffing(input: &[u8]) -> Result<Vec<u8>, &'static str> {
    let mut monitor = BitStuffMonitor::new();
    let mut output_bits = Vec::new();
    let mut bit_count = 0;
    
    for &byte in input {
        for bit in (0..8).rev() {
            let current_bit = (byte >> bit) & 1;
            bit_count += 1;
            
            // Check if this should be a stuff bit
            let expect_stuff = monitor.consecutive_count == MAX_CONSECUTIVE_BITS;
            
            if !monitor.check(current_bit, expect_stuff) {
                return Err("Bit stuffing violation detected");
            }
            
            // Only add data bits, skip stuff bits
            if !expect_stuff {
                output_bits.push(current_bit);
            }
        }
    }
    
    // Convert bits back to bytes
    let mut output = Vec::new();
    for chunk in output_bits.chunks(8) {
        let mut byte = 0u8;
        for (i, &bit) in chunk.iter().enumerate() {
            byte |= bit << (7 - i);
        }
        output.push(byte);
    }
    
    Ok(output)
}
```

## Five Types of CAN Errors

When a node detects an error, it signals this to other nodes by transmitting an **Error Frame**. CAN defines five types of errors:

### 1. **Bit Error**
Detected when a transmitter monitors the bus and finds a different bit value than transmitted (except during arbitration and ACK).

### 2. **Stuff Error**
Detected when the bit stuffing rule is violated (more than 5 consecutive identical bits without a stuff bit).

### 3. **CRC Error**
Detected when the calculated CRC doesn't match the received CRC value.

### 4. **Form Error**
Detected when a fixed-form bit field contains an illegal bit (e.g., EOF not having 7 recessive bits).

### 5. **Acknowledgment Error**
Detected by the transmitter when no dominant bit is received in the ACK slot.

### C/C++ Error Handling

```c
typedef enum {
    CAN_ERROR_NONE = 0,
    CAN_ERROR_BIT = 1,
    CAN_ERROR_STUFF = 2,
    CAN_ERROR_CRC = 3,
    CAN_ERROR_FORM = 4,
    CAN_ERROR_ACK = 5
} can_error_type_t;

typedef struct {
    can_error_type_t error_type;
    uint8_t transmit_error_count;
    uint8_t receive_error_count;
    bool error_passive;
    bool bus_off;
} can_error_state_t;

void handle_can_error(can_error_state_t *state, can_error_type_t error) {
    state->error_type = error;
    
    // Update error counters based on error type
    switch (error) {
        case CAN_ERROR_BIT:
        case CAN_ERROR_STUFF:
        case CAN_ERROR_CRC:
        case CAN_ERROR_FORM:
        case CAN_ERROR_ACK:
            state->transmit_error_count += 8;
            break;
        default:
            break;
    }
    
    // Check error states
    if (state->transmit_error_count > 127 || state->receive_error_count > 127) {
        state->error_passive = true;
    }
    
    if (state->transmit_error_count > 255) {
        state->bus_off = true;
    }
}

// Transmit error frame
void transmit_error_frame(bool error_passive) {
    if (error_passive) {
        // Passive error flag: 6 recessive bits
        for (int i = 0; i < 6; i++) {
            // transmit_bit(CAN_RECESSIVE);
        }
    } else {
        // Active error flag: 6 dominant bits
        for (int i = 0; i < 6; i++) {
            // transmit_bit(CAN_DOMINANT);
        }
    }
    
    // Error delimiter: 8 recessive bits
    for (int i = 0; i < 8; i++) {
        // transmit_bit(CAN_RECESSIVE);
    }
}
```

### Rust Error Handling

```rust
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CanErrorType {
    None,
    Bit,
    Stuff,
    Crc,
    Form,
    Ack,
}

pub struct CanErrorState {
    pub error_type: CanErrorType,
    pub transmit_error_count: u8,
    pub receive_error_count: u8,
    pub error_passive: bool,
    pub bus_off: bool,
}

impl CanErrorState {
    pub fn new() -> Self {
        CanErrorState {
            error_type: CanErrorType::None,
            transmit_error_count: 0,
            receive_error_count: 0,
            error_passive: false,
            bus_off: false,
        }
    }
    
    /// Handle detected error and update state
    pub fn handle_error(&mut self, error: CanErrorType) {
        self.error_type = error;
        
        // Update error counters
        match error {
            CanErrorType::Bit | CanErrorType::Stuff | 
            CanErrorType::Crc | CanErrorType::Form | 
            CanErrorType::Ack => {
                self.transmit_error_count = self.transmit_error_count.saturating_add(8);
            }
            CanErrorType::None => {}
        }
        
        // Update error states
        self.error_passive = self.transmit_error_count > 127 || 
                            self.receive_error_count > 127;
        
        self.bus_off = self.transmit_error_count > 255;
    }
    
    /// Transmit error frame based on current state
    pub fn transmit_error_frame(&self) -> Vec<u8> {
        let mut error_frame = Vec::new();
        
        if self.error_passive {
            // Passive error flag: 6 recessive bits
            error_frame.extend_from_slice(&[RECESSIVE; 6]);
        } else {
            // Active error flag: 6 dominant bits
            error_frame.extend_from_slice(&[DOMINANT; 6]);
        }
        
        // Error delimiter: 8 recessive bits
        error_frame.extend_from_slice(&[RECESSIVE; 8]);
        
        error_frame
    }
}
```

## Summary

CAN's error detection mechanisms provide exceptional reliability through multiple complementary layers:

**Key Error Detection Methods:**
- **CRC (15-bit)** - Detects bit errors in frame content using polynomial-based checksum
- **Frame Check** - Validates fixed-format bits (delimiters, EOF) are correct
- **ACK Check** - Ensures at least one receiver successfully received the frame
- **Bit Monitoring** - Transmitter verifies each sent bit matches the bus state
- **Bit Stuffing** - Enforces max 5 consecutive identical bits for synchronization

**Five Error Types:**
1. Bit Error - Transmitted bit differs from monitored bit
2. Stuff Error - Bit stuffing rule violation detected
3. CRC Error - Calculated CRC doesn't match received value
4. Form Error - Fixed-format fields contain illegal bits
5. ACK Error - No receiver acknowledged the frame

**Error Management:**
- Nodes maintain Transmit and Receive Error Counters
- Error-Active state (TEC/REC ≤ 127) - sends active error flags
- Error-Passive state (TEC/REC > 127) - sends passive error flags
- Bus-Off state (TEC > 255) - node disconnects from bus

This multi-layered approach achieves a Hamming Distance of 6, meaning it can detect up to 5-bit errors in a single frame, resulting in an undetected error rate below 4.7 × 10⁻¹¹ messages. The combination of hardware-level monitoring and protocol-level checks makes CAN exceptionally robust for safety-critical applications.