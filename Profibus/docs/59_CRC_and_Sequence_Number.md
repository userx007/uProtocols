# CRC and Sequence Number in PROFIsafe

## Detailed Description

**CRC (Cyclic Redundancy Check) and Sequence Numbers** are fundamental safety mechanisms in PROFIsafe communication, which is the functional safety extension of PROFIBUS and PROFINET designed for safety-critical applications up to SIL 3 (Safety Integrity Level 3) according to IEC 61508.

### Purpose and Function

These mechanisms work together to ensure:

1. **Data Integrity (CRC)**: Detects transmission errors, bit flips, and data corruption
2. **Message Ordering (Sequence Number)**: Prevents message loss, repetition, insertion, and incorrect sequencing
3. **Timeout Detection**: Identifies communication failures
4. **Combined Safety**: Together they create a "safety layer" on top of standard PROFIBUS communication

### CRC in PROFIsafe

The PROFIsafe protocol uses a specific CRC algorithm to verify data integrity:

- **Algorithm**: Typically CRC-16 or CRC-24 depending on safety requirements
- **Coverage**: Includes safety data payload and control information
- **Position**: Appended to each PROFIsafe telegram
- **Verification**: Receiver calculates CRC on received data and compares with transmitted CRC

### Sequence Number Mechanism

The sequence number is a counter that:

- **Range**: Typically 0-255 (8-bit) or other defined ranges
- **Increment**: Increases with each new safety message
- **Wrap-around**: Returns to 0 after reaching maximum value
- **Monitoring**: Receiver checks for proper incrementing to detect:
  - Lost messages (gap in sequence)
  - Repeated messages (same sequence number)
  - Out-of-order messages (sequence jump)

### PROFIsafe Telegram Structure

```
[Safety Data] [Control Byte] [CRC] [Sequence Number]
```

Control Byte typically contains:
- Toggle bit (alternates with each message)
- Status information
- Version information

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// PROFIsafe configuration
#define PROFISAFE_MAX_DATA_LENGTH 128
#define PROFISAFE_CRC_POLYNOMIAL 0x755B  // Example CRC-16 polynomial

// PROFIsafe telegram structure
typedef struct {
    uint8_t safety_data[PROFISAFE_MAX_DATA_LENGTH];
    uint16_t data_length;
    uint8_t control_byte;
    uint16_t crc;
    uint8_t sequence_number;
} profisafe_telegram_t;

// Sequence number context
typedef struct {
    uint8_t tx_sequence;        // Transmit sequence number
    uint8_t rx_sequence;        // Expected receive sequence number
    uint8_t rx_sequence_error_count;
    bool first_message;
} sequence_context_t;

/**
 * CRC-16 calculation for PROFIsafe
 * Uses table-driven approach for efficiency
 */
static const uint16_t crc16_table[256] = {
    0x0000, 0x755B, 0xEAB6, 0x9FED, 0x3A7D, 0x4F26, 0xD0CB, 0xA590,
    // ... (full table would be included in production code)
    0x92C4, 0xE79F, 0x7872, 0x0D29, 0xA8B9, 0xDDE2, 0x420F, 0x3754
};

uint16_t profisafe_calculate_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;  // Initial value
    
    for (uint16_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc16_table[index];
    }
    
    return crc;
}

/**
 * Build a PROFIsafe telegram with CRC and sequence number
 */
bool profisafe_build_telegram(profisafe_telegram_t *telegram,
                               const uint8_t *safety_data,
                               uint16_t data_length,
                               sequence_context_t *seq_ctx) {
    if (data_length > PROFISAFE_MAX_DATA_LENGTH) {
        return false;
    }
    
    // Copy safety data
    memcpy(telegram->safety_data, safety_data, data_length);
    telegram->data_length = data_length;
    
    // Set sequence number
    telegram->sequence_number = seq_ctx->tx_sequence;
    
    // Prepare control byte (example: toggle bit in MSB)
    static uint8_t toggle = 0;
    telegram->control_byte = (toggle << 7) | 0x01;  // Version 1
    toggle ^= 1;  // Toggle for next message
    
    // Calculate CRC over data + control byte + sequence number
    uint8_t crc_buffer[PROFISAFE_MAX_DATA_LENGTH + 2];
    memcpy(crc_buffer, telegram->safety_data, data_length);
    crc_buffer[data_length] = telegram->control_byte;
    crc_buffer[data_length + 1] = telegram->sequence_number;
    
    telegram->crc = profisafe_calculate_crc16(crc_buffer, data_length + 2);
    
    // Increment sequence number for next transmission
    seq_ctx->tx_sequence = (seq_ctx->tx_sequence + 1) & 0xFF;
    
    return true;
}

/**
 * Verify received PROFIsafe telegram
 */
typedef enum {
    PROFISAFE_OK = 0,
    PROFISAFE_CRC_ERROR,
    PROFISAFE_SEQUENCE_ERROR,
    PROFISAFE_LENGTH_ERROR
} profisafe_status_t;

profisafe_status_t profisafe_verify_telegram(const profisafe_telegram_t *telegram,
                                              sequence_context_t *seq_ctx) {
    // Verify data length
    if (telegram->data_length > PROFISAFE_MAX_DATA_LENGTH) {
        return PROFISAFE_LENGTH_ERROR;
    }
    
    // Calculate CRC
    uint8_t crc_buffer[PROFISAFE_MAX_DATA_LENGTH + 2];
    memcpy(crc_buffer, telegram->safety_data, telegram->data_length);
    crc_buffer[telegram->data_length] = telegram->control_byte;
    crc_buffer[telegram->data_length + 1] = telegram->sequence_number;
    
    uint16_t calculated_crc = profisafe_calculate_crc16(crc_buffer, 
                                                         telegram->data_length + 2);
    
    if (calculated_crc != telegram->crc) {
        return PROFISAFE_CRC_ERROR;
    }
    
    // Verify sequence number
    if (!seq_ctx->first_message) {
        uint8_t expected_seq = (seq_ctx->rx_sequence + 1) & 0xFF;
        
        if (telegram->sequence_number != expected_seq) {
            seq_ctx->rx_sequence_error_count++;
            return PROFISAFE_SEQUENCE_ERROR;
        }
        
        seq_ctx->rx_sequence_error_count = 0;
    } else {
        seq_ctx->first_message = false;
    }
    
    // Update expected sequence number
    seq_ctx->rx_sequence = telegram->sequence_number;
    
    return PROFISAFE_OK;
}

/**
 * Example usage
 */
void profisafe_example(void) {
    sequence_context_t tx_seq_ctx = {0, 0, 0, true};
    sequence_context_t rx_seq_ctx = {0, 0, 0, true};
    
    // Transmit side
    profisafe_telegram_t tx_telegram;
    uint8_t safety_data[] = {0x01, 0x02, 0x03, 0x04};
    
    if (profisafe_build_telegram(&tx_telegram, safety_data, 
                                  sizeof(safety_data), &tx_seq_ctx)) {
        printf("Telegram built: CRC=0x%04X, Seq=%d\n", 
               tx_telegram.crc, tx_telegram.sequence_number);
    }
    
    // Receive side
    profisafe_status_t status = profisafe_verify_telegram(&tx_telegram, &rx_seq_ctx);
    
    switch (status) {
        case PROFISAFE_OK:
            printf("Telegram verified successfully\n");
            break;
        case PROFISAFE_CRC_ERROR:
            printf("CRC error detected!\n");
            break;
        case PROFISAFE_SEQUENCE_ERROR:
            printf("Sequence number error!\n");
            break;
        default:
            printf("Unknown error\n");
            break;
    }
}
```

### C++ Object-Oriented Implementation

```cpp
#include <vector>
#include <array>
#include <cstdint>
#include <stdexcept>

class PROFIsafeTelegram {
public:
    enum class Status {
        OK,
        CRC_ERROR,
        SEQUENCE_ERROR,
        LENGTH_ERROR,
        TIMEOUT_ERROR
    };
    
private:
    static constexpr size_t MAX_DATA_LENGTH = 128;
    static constexpr uint16_t CRC_POLYNOMIAL = 0x755B;
    
    std::vector<uint8_t> safety_data_;
    uint8_t control_byte_;
    uint16_t crc_;
    uint8_t sequence_number_;
    
    // CRC lookup table
    static const std::array<uint16_t, 256> crc_table_;
    
    static uint16_t calculateCRC16(const std::vector<uint8_t>& data) {
        uint16_t crc = 0xFFFF;
        
        for (uint8_t byte : data) {
            uint8_t index = (crc ^ byte) & 0xFF;
            crc = (crc >> 8) ^ crc_table_[index];
        }
        
        return crc;
    }
    
public:
    PROFIsafeTelegram() 
        : control_byte_(0), crc_(0), sequence_number_(0) {}
    
    void build(const std::vector<uint8_t>& safety_data, 
               uint8_t sequence_number, 
               bool toggle_bit) {
        if (safety_data.size() > MAX_DATA_LENGTH) {
            throw std::length_error("Safety data exceeds maximum length");
        }
        
        safety_data_ = safety_data;
        sequence_number_ = sequence_number;
        control_byte_ = (toggle_bit ? 0x80 : 0x00) | 0x01;
        
        // Build CRC buffer
        std::vector<uint8_t> crc_buffer = safety_data_;
        crc_buffer.push_back(control_byte_);
        crc_buffer.push_back(sequence_number_);
        
        crc_ = calculateCRC16(crc_buffer);
    }
    
    Status verify(uint8_t expected_sequence) const {
        // Verify CRC
        std::vector<uint8_t> crc_buffer = safety_data_;
        crc_buffer.push_back(control_byte_);
        crc_buffer.push_back(sequence_number_);
        
        uint16_t calculated_crc = calculateCRC16(crc_buffer);
        if (calculated_crc != crc_) {
            return Status::CRC_ERROR;
        }
        
        // Verify sequence number
        if (sequence_number_ != expected_sequence) {
            return Status::SEQUENCE_ERROR;
        }
        
        return Status::OK;
    }
    
    const std::vector<uint8_t>& getSafetyData() const { return safety_data_; }
    uint8_t getSequenceNumber() const { return sequence_number_; }
    uint16_t getCRC() const { return crc_; }
};

// Initialize static CRC table (abbreviated)
const std::array<uint16_t, 256> PROFIsafeTelegram::crc_table_ = {
    0x0000, 0x755B, 0xEAB6, 0x9FED, // ...
};

class SequenceManager {
private:
    uint8_t tx_sequence_;
    uint8_t rx_sequence_;
    bool first_message_;
    size_t error_count_;
    
public:
    SequenceManager() 
        : tx_sequence_(0), rx_sequence_(0), 
          first_message_(true), error_count_(0) {}
    
    uint8_t getNextTxSequence() {
        uint8_t current = tx_sequence_;
        tx_sequence_ = (tx_sequence_ + 1) & 0xFF;
        return current;
    }
    
    bool validateRxSequence(uint8_t received_sequence) {
        if (first_message_) {
            rx_sequence_ = received_sequence;
            first_message_ = false;
            return true;
        }
        
        uint8_t expected = (rx_sequence_ + 1) & 0xFF;
        if (received_sequence == expected) {
            rx_sequence_ = received_sequence;
            error_count_ = 0;
            return true;
        }
        
        error_count_++;
        return false;
    }
    
    size_t getErrorCount() const { return error_count_; }
    void reset() {
        tx_sequence_ = 0;
        rx_sequence_ = 0;
        first_message_ = true;
        error_count_ = 0;
    }
};
```

### Rust Implementation

```rust
use std::fmt;

const PROFISAFE_MAX_DATA_LENGTH: usize = 128;
const CRC_POLYNOMIAL: u16 = 0x755B;

// CRC lookup table (abbreviated for brevity)
const CRC16_TABLE: [u16; 256] = [
    0x0000, 0x755B, 0xEAB6, 0x9FED, 0x3A7D, 0x4F26, 0xD0CB, 0xA590,
    // ... (full table in production code)
    0x0000, 0x755B, 0xEAB6, 0x9FED, 0x3A7D, 0x4F26, 0xD0CB, 0xA590,
    // Repeated for demonstration
];

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PROFIsafeStatus {
    Ok,
    CrcError,
    SequenceError,
    LengthError,
    TimeoutError,
}

impl fmt::Display for PROFIsafeStatus {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            PROFIsafeStatus::Ok => write!(f, "OK"),
            PROFIsafeStatus::CrcError => write!(f, "CRC Error"),
            PROFIsafeStatus::SequenceError => write!(f, "Sequence Error"),
            PROFIsafeStatus::LengthError => write!(f, "Length Error"),
            PROFIsafeStatus::TimeoutError => write!(f, "Timeout Error"),
        }
    }
}

/// Calculate CRC-16 for PROFIsafe
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        let index = ((crc ^ u16::from(byte)) & 0xFF) as usize;
        crc = (crc >> 8) ^ CRC16_TABLE[index];
    }
    
    crc
}

#[derive(Debug, Clone)]
pub struct PROFIsafeTelegram {
    safety_data: Vec<u8>,
    control_byte: u8,
    crc: u16,
    sequence_number: u8,
}

impl PROFIsafeTelegram {
    /// Create a new PROFIsafe telegram
    pub fn new(
        safety_data: Vec<u8>,
        sequence_number: u8,
        toggle_bit: bool,
    ) -> Result<Self, &'static str> {
        if safety_data.len() > PROFISAFE_MAX_DATA_LENGTH {
            return Err("Safety data exceeds maximum length");
        }
        
        let control_byte = if toggle_bit { 0x80 | 0x01 } else { 0x01 };
        
        // Build CRC buffer
        let mut crc_buffer = safety_data.clone();
        crc_buffer.push(control_byte);
        crc_buffer.push(sequence_number);
        
        let crc = calculate_crc16(&crc_buffer);
        
        Ok(PROFIsafeTelegram {
            safety_data,
            control_byte,
            crc,
            sequence_number,
        })
    }
    
    /// Verify telegram integrity and sequence
    pub fn verify(&self, expected_sequence: u8) -> PROFIsafeStatus {
        // Build CRC buffer
        let mut crc_buffer = self.safety_data.clone();
        crc_buffer.push(self.control_byte);
        crc_buffer.push(self.sequence_number);
        
        // Verify CRC
        let calculated_crc = calculate_crc16(&crc_buffer);
        if calculated_crc != self.crc {
            return PROFIsafeStatus::CrcError;
        }
        
        // Verify sequence number
        if self.sequence_number != expected_sequence {
            return PROFIsafeStatus::SequenceError;
        }
        
        PROFIsafeStatus::Ok
    }
    
    pub fn safety_data(&self) -> &[u8] {
        &self.safety_data
    }
    
    pub fn sequence_number(&self) -> u8 {
        self.sequence_number
    }
    
    pub fn crc(&self) -> u16 {
        self.crc
    }
    
    /// Serialize telegram for transmission
    pub fn serialize(&self) -> Vec<u8> {
        let mut buffer = self.safety_data.clone();
        buffer.push(self.control_byte);
        buffer.push((self.crc >> 8) as u8);
        buffer.push((self.crc & 0xFF) as u8);
        buffer.push(self.sequence_number);
        buffer
    }
    
    /// Deserialize telegram from received data
    pub fn deserialize(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < 4 {
            return Err("Data too short for PROFIsafe telegram");
        }
        
        let data_len = data.len() - 4;
        let safety_data = data[..data_len].to_vec();
        let control_byte = data[data_len];
        let crc = (u16::from(data[data_len + 1]) << 8) | u16::from(data[data_len + 2]);
        let sequence_number = data[data_len + 3];
        
        Ok(PROFIsafeTelegram {
            safety_data,
            control_byte,
            crc,
            sequence_number,
        })
    }
}

pub struct SequenceManager {
    tx_sequence: u8,
    rx_sequence: u8,
    first_message: bool,
    error_count: usize,
    toggle_bit: bool,
}

impl SequenceManager {
    pub fn new() -> Self {
        SequenceManager {
            tx_sequence: 0,
            rx_sequence: 0,
            first_message: true,
            error_count: 0,
            toggle_bit: false,
        }
    }
    
    /// Get next transmit sequence number and toggle bit
    pub fn get_next_tx(&mut self) -> (u8, bool) {
        let seq = self.tx_sequence;
        let toggle = self.toggle_bit;
        
        self.tx_sequence = self.tx_sequence.wrapping_add(1);
        self.toggle_bit = !self.toggle_bit;
        
        (seq, toggle)
    }
    
    /// Validate received sequence number
    pub fn validate_rx_sequence(&mut self, received_sequence: u8) -> bool {
        if self.first_message {
            self.rx_sequence = received_sequence;
            self.first_message = false;
            return true;
        }
        
        let expected = self.rx_sequence.wrapping_add(1);
        if received_sequence == expected {
            self.rx_sequence = received_sequence;
            self.error_count = 0;
            true
        } else {
            self.error_count += 1;
            false
        }
    }
    
    pub fn error_count(&self) -> usize {
        self.error_count
    }
    
    pub fn reset(&mut self) {
        self.tx_sequence = 0;
        self.rx_sequence = 0;
        self.first_message = true;
        self.error_count = 0;
        self.toggle_bit = false;
    }
}

impl Default for SequenceManager {
    fn default() -> Self {
        Self::new()
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_profisafe_telegram() {
        let mut tx_seq_mgr = SequenceManager::new();
        let mut rx_seq_mgr = SequenceManager::new();
        
        // Create telegram
        let safety_data = vec![0x01, 0x02, 0x03, 0x04];
        let (seq, toggle) = tx_seq_mgr.get_next_tx();
        let telegram = PROFIsafeTelegram::new(safety_data, seq, toggle).unwrap();
        
        println!("Telegram: CRC=0x{:04X}, Seq={}", telegram.crc(), telegram.sequence_number());
        
        // Verify telegram
        let status = telegram.verify(0);
        assert_eq!(status, PROFIsafeStatus::Ok);
        
        assert!(rx_seq_mgr.validate_rx_sequence(telegram.sequence_number()));
    }
    
    #[test]
    fn test_sequence_error_detection() {
        let mut seq_mgr = SequenceManager::new();
        
        // First message
        assert!(seq_mgr.validate_rx_sequence(0));
        
        // Correct sequence
        assert!(seq_mgr.validate_rx_sequence(1));
        
        // Skip sequence (error)
        assert!(!seq_mgr.validate_rx_sequence(3));
        assert_eq!(seq_mgr.error_count(), 1);
    }
}
```

## Summary

**CRC and Sequence Numbers** are critical safety mechanisms in PROFIsafe that work synergistically to ensure reliable and safe communication in safety-critical industrial applications:

**Key Points:**
- **CRC** provides data integrity verification, detecting transmission errors and corruption
- **Sequence Numbers** prevent message loss, repetition, and ordering issues
- Together they enable **SIL 3** certification for safety applications
- **Implementation** requires careful attention to CRC algorithms, sequence tracking, and error handling
- **Performance** considerations include efficient CRC calculation (table-driven methods) and minimal overhead
- **Standards Compliance** follows IEC 61784-3-3 (PROFIsafe profile)

These mechanisms form the foundation of PROFIsafe's "Black Channel" approach, where safety is ensured by the safety layer independent of the underlying communication infrastructure, making it suitable for protecting human life and critical machinery in industrial automation environments.