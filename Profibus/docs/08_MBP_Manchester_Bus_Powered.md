# MBP (Manchester Bus Powered) - Profibus PA Physical Layer

## Overview

MBP (Manchester Bus Powered) is a specialized physical layer technology used in Profibus PA (Process Automation). It's designed specifically for hazardous environments where intrinsically safe operation is critical. MBP enables both data transmission and device power delivery over the same two-wire bus using Manchester encoding.

## Key Characteristics

### Manchester Encoding
Manchester encoding ensures that each bit period contains a transition, providing:
- **Self-clocking capability**: The receiver can extract timing information from the signal itself
- **DC balance**: No DC component in the signal, preventing transformer saturation
- **Error detection**: Missing transitions indicate transmission errors

In Manchester encoding:
- **Logic 0**: Transition from high to low in the middle of the bit period
- **Logic 1**: Transition from low to high in the middle of the bit period

### Intrinsic Safety
MBP is designed for **Ex (Explosive atmosphere) zones** where flammable gases or dust may be present. It limits:
- Maximum current
- Maximum voltage
- Maximum power

This ensures that even in fault conditions, there's insufficient energy to ignite hazardous atmospheres.

### Bus-Powered Operation
Devices receive both power and data over the same two-wire cable, eliminating the need for separate power supplies in hazardous areas.

## Technical Specifications

- **Transmission Speed**: 31.25 kbit/s
- **Topology**: Linear bus with branches (tree structure)
- **Cable**: Two-wire shielded twisted pair
- **Maximum Distance**: 1900m (without repeaters)
- **Power Supply**: Typically 9-32V DC
- **Number of Devices**: Up to 32 per segment (with standard supply)

## Code Examples

### C/C++ Implementation

Here's a low-level MBP frame handler in C:

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// MBP Frame Structure
#define MBP_PREAMBLE_LENGTH 33
#define MBP_START_DELIMITER 0x68
#define MBP_END_DELIMITER 0x16

typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t length_repeat;
    uint8_t start_delimiter_repeat;
    uint8_t destination_address;
    uint8_t source_address;
    uint8_t function_code;
    uint8_t data[246];  // Maximum data length
    uint8_t checksum;
    uint8_t end_delimiter;
} MBP_Frame;

// Manchester Encoding State
typedef enum {
    MANCHESTER_LOW_TO_HIGH = 0,
    MANCHESTER_HIGH_TO_LOW = 1
} ManchesterTransition;

// Manchester Encoder
void manchester_encode_byte(uint8_t byte, uint8_t *encoded_buffer, size_t *offset) {
    for (int bit = 7; bit >= 0; bit--) {
        bool bit_value = (byte >> bit) & 0x01;
        
        if (bit_value) {
            // Logic 1: Low-to-High transition
            encoded_buffer[(*offset)++] = 0x00;  // First half: low
            encoded_buffer[(*offset)++] = 0xFF;  // Second half: high
        } else {
            // Logic 0: High-to-Low transition
            encoded_buffer[(*offset)++] = 0xFF;  // First half: high
            encoded_buffer[(*offset)++] = 0x00;  // Second half: low
        }
    }
}

// Manchester Decoder
bool manchester_decode_byte(const uint8_t *encoded_buffer, size_t *offset, uint8_t *decoded_byte) {
    *decoded_byte = 0;
    
    for (int bit = 7; bit >= 0; bit--) {
        uint8_t first_half = encoded_buffer[(*offset)++];
        uint8_t second_half = encoded_buffer[(*offset)++];
        
        if (first_half == 0x00 && second_half == 0xFF) {
            // Low-to-High = Logic 1
            *decoded_byte |= (1 << bit);
        } else if (first_half == 0xFF && second_half == 0x00) {
            // High-to-Low = Logic 0
            // Bit remains 0
        } else {
            // Invalid Manchester encoding
            return false;
        }
    }
    
    return true;
}

// Calculate Checksum (simple XOR-based)
uint8_t calculate_checksum(const uint8_t *data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// Build MBP Frame
size_t build_mbp_frame(MBP_Frame *frame, uint8_t dest_addr, uint8_t src_addr, 
                       uint8_t func_code, const uint8_t *data, uint8_t data_len,
                       uint8_t *output_buffer) {
    frame->start_delimiter = MBP_START_DELIMITER;
    frame->length = data_len + 3;  // DA + SA + FC
    frame->length_repeat = frame->length;
    frame->start_delimiter_repeat = MBP_START_DELIMITER;
    frame->destination_address = dest_addr;
    frame->source_address = src_addr;
    frame->function_code = func_code;
    
    if (data_len > 0) {
        memcpy(frame->data, data, data_len);
    }
    
    // Calculate checksum over DA, SA, FC, and Data
    uint8_t checksum_data[250];
    checksum_data[0] = dest_addr;
    checksum_data[1] = src_addr;
    checksum_data[2] = func_code;
    memcpy(&checksum_data[3], data, data_len);
    
    frame->checksum = calculate_checksum(checksum_data, data_len + 3);
    frame->end_delimiter = MBP_END_DELIMITER;
    
    // Encode frame with Manchester encoding
    size_t offset = 0;
    
    // Preamble (33 bits of alternating pattern for synchronization)
    for (int i = 0; i < MBP_PREAMBLE_LENGTH; i++) {
        output_buffer[offset++] = (i % 2) ? 0xFF : 0x00;
    }
    
    // Encode frame fields
    manchester_encode_byte(frame->start_delimiter, output_buffer, &offset);
    manchester_encode_byte(frame->length, output_buffer, &offset);
    manchester_encode_byte(frame->length_repeat, output_buffer, &offset);
    manchester_encode_byte(frame->start_delimiter_repeat, output_buffer, &offset);
    manchester_encode_byte(frame->destination_address, output_buffer, &offset);
    manchester_encode_byte(frame->source_address, output_buffer, &offset);
    manchester_encode_byte(frame->function_code, output_buffer, &offset);
    
    for (int i = 0; i < data_len; i++) {
        manchester_encode_byte(frame->data[i], output_buffer, &offset);
    }
    
    manchester_encode_byte(frame->checksum, output_buffer, &offset);
    manchester_encode_byte(frame->end_delimiter, output_buffer, &offset);
    
    return offset;
}

// Parse received MBP Frame
bool parse_mbp_frame(const uint8_t *encoded_buffer, size_t buffer_len, MBP_Frame *frame) {
    size_t offset = 0;
    
    // Skip preamble (look for start delimiter)
    while (offset < buffer_len - 16) {  // At least one byte encoded
        uint8_t test_byte;
        size_t test_offset = offset;
        if (manchester_decode_byte(encoded_buffer, &test_offset, &test_byte)) {
            if (test_byte == MBP_START_DELIMITER) {
                break;
            }
        }
        offset += 2;  // Move to next possible byte boundary
    }
    
    // Decode frame
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->start_delimiter)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->length)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->length_repeat)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->start_delimiter_repeat)) return false;
    
    // Validate header
    if (frame->start_delimiter != MBP_START_DELIMITER || 
        frame->start_delimiter_repeat != MBP_START_DELIMITER ||
        frame->length != frame->length_repeat) {
        return false;
    }
    
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->destination_address)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->source_address)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->function_code)) return false;
    
    uint8_t data_len = frame->length - 3;
    for (int i = 0; i < data_len; i++) {
        if (!manchester_decode_byte(encoded_buffer, &offset, &frame->data[i])) return false;
    }
    
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->checksum)) return false;
    if (!manchester_decode_byte(encoded_buffer, &offset, &frame->end_delimiter)) return false;
    
    // Verify checksum
    uint8_t checksum_data[250];
    checksum_data[0] = frame->destination_address;
    checksum_data[1] = frame->source_address;
    checksum_data[2] = frame->function_code;
    memcpy(&checksum_data[3], frame->data, data_len);
    
    uint8_t calculated_checksum = calculate_checksum(checksum_data, data_len + 3);
    
    return (calculated_checksum == frame->checksum) && 
           (frame->end_delimiter == MBP_END_DELIMITER);
}
```

### Rust Implementation

Here's a safe, idiomatic Rust implementation:

```rust
use std::error::Error;
use std::fmt;

const MBP_PREAMBLE_LENGTH: usize = 33;
const MBP_START_DELIMITER: u8 = 0x68;
const MBP_END_DELIMITER: u8 = 0x16;
const MAX_DATA_LENGTH: usize = 246;

#[derive(Debug)]
pub enum MbpError {
    InvalidManchesterEncoding,
    InvalidFrameStructure,
    ChecksumMismatch,
    BufferTooSmall,
    DataTooLarge,
}

impl fmt::Display for MbpError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            MbpError::InvalidManchesterEncoding => write!(f, "Invalid Manchester encoding"),
            MbpError::InvalidFrameStructure => write!(f, "Invalid frame structure"),
            MbpError::ChecksumMismatch => write!(f, "Checksum mismatch"),
            MbpError::BufferTooSmall => write!(f, "Buffer too small"),
            MbpError::DataTooLarge => write!(f, "Data exceeds maximum length"),
        }
    }
}

impl Error for MbpError {}

#[derive(Debug, Clone)]
pub struct MbpFrame {
    pub destination_address: u8,
    pub source_address: u8,
    pub function_code: u8,
    pub data: Vec<u8>,
}

impl MbpFrame {
    pub fn new(dest: u8, src: u8, func: u8, data: Vec<u8>) -> Result<Self, MbpError> {
        if data.len() > MAX_DATA_LENGTH {
            return Err(MbpError::DataTooLarge);
        }
        
        Ok(MbpFrame {
            destination_address: dest,
            source_address: src,
            function_code: func,
            data,
        })
    }
    
    fn calculate_checksum(&self) -> u8 {
        let mut checksum = self.destination_address;
        checksum ^= self.source_address;
        checksum ^= self.function_code;
        
        for byte in &self.data {
            checksum ^= byte;
        }
        
        checksum
    }
}

pub struct ManchesterCodec;

impl ManchesterCodec {
    /// Encode a single byte using Manchester encoding
    pub fn encode_byte(byte: u8) -> Vec<u8> {
        let mut encoded = Vec::with_capacity(16);
        
        for bit_pos in (0..8).rev() {
            let bit = (byte >> bit_pos) & 0x01;
            
            if bit == 1 {
                // Logic 1: Low-to-High transition
                encoded.push(0x00); // First half: low
                encoded.push(0xFF); // Second half: high
            } else {
                // Logic 0: High-to-Low transition
                encoded.push(0xFF); // First half: high
                encoded.push(0x00); // Second half: low
            }
        }
        
        encoded
    }
    
    /// Decode a single Manchester-encoded byte
    pub fn decode_byte(encoded: &[u8], offset: &mut usize) -> Result<u8, MbpError> {
        if *offset + 16 > encoded.len() {
            return Err(MbpError::BufferTooSmall);
        }
        
        let mut decoded = 0u8;
        
        for bit_pos in (0..8).rev() {
            let first_half = encoded[*offset];
            let second_half = encoded[*offset + 1];
            *offset += 2;
            
            match (first_half, second_half) {
                (0x00, 0xFF) => {
                    // Low-to-High = Logic 1
                    decoded |= 1 << bit_pos;
                }
                (0xFF, 0x00) => {
                    // High-to-Low = Logic 0
                    // Bit remains 0
                }
                _ => {
                    return Err(MbpError::InvalidManchesterEncoding);
                }
            }
        }
        
        Ok(decoded)
    }
    
    /// Encode complete data buffer
    pub fn encode(data: &[u8]) -> Vec<u8> {
        let mut encoded = Vec::with_capacity(data.len() * 16);
        
        for byte in data {
            encoded.extend(Self::encode_byte(*byte));
        }
        
        encoded
    }
}

pub struct MbpProtocol;

impl MbpProtocol {
    /// Build a complete MBP frame with Manchester encoding
    pub fn build_frame(frame: &MbpFrame) -> Result<Vec<u8>, MbpError> {
        let data_len = frame.data.len() as u8;
        let length = data_len + 3; // DA + SA + FC
        
        let mut output = Vec::new();
        
        // Add preamble (alternating pattern for synchronization)
        for i in 0..MBP_PREAMBLE_LENGTH {
            output.push(if i % 2 == 0 { 0x00 } else { 0xFF });
        }
        
        // Encode frame fields
        output.extend(ManchesterCodec::encode_byte(MBP_START_DELIMITER));
        output.extend(ManchesterCodec::encode_byte(length));
        output.extend(ManchesterCodec::encode_byte(length)); // length_repeat
        output.extend(ManchesterCodec::encode_byte(MBP_START_DELIMITER)); // SD repeat
        output.extend(ManchesterCodec::encode_byte(frame.destination_address));
        output.extend(ManchesterCodec::encode_byte(frame.source_address));
        output.extend(ManchesterCodec::encode_byte(frame.function_code));
        
        // Encode data
        for byte in &frame.data {
            output.extend(ManchesterCodec::encode_byte(*byte));
        }
        
        // Calculate and encode checksum
        let checksum = frame.calculate_checksum();
        output.extend(ManchesterCodec::encode_byte(checksum));
        output.extend(ManchesterCodec::encode_byte(MBP_END_DELIMITER));
        
        Ok(output)
    }
    
    /// Parse a received MBP frame
    pub fn parse_frame(encoded_buffer: &[u8]) -> Result<MbpFrame, MbpError> {
        let mut offset = 0;
        
        // Skip preamble and find start delimiter
        while offset < encoded_buffer.len().saturating_sub(16) {
            if let Ok(byte) = ManchesterCodec::decode_byte(encoded_buffer, &mut offset) {
                if byte == MBP_START_DELIMITER {
                    offset -= 16; // Reset to beginning of start delimiter
                    break;
                }
            } else {
                offset += 2; // Move forward by one potential bit
            }
        }
        
        // Decode frame header
        let start_delimiter = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let length = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let length_repeat = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let start_delimiter_repeat = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        
        // Validate header
        if start_delimiter != MBP_START_DELIMITER 
            || start_delimiter_repeat != MBP_START_DELIMITER 
            || length != length_repeat {
            return Err(MbpError::InvalidFrameStructure);
        }
        
        // Decode addresses and function code
        let destination_address = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let source_address = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let function_code = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        
        // Decode data
        let data_len = length.saturating_sub(3) as usize;
        let mut data = Vec::with_capacity(data_len);
        
        for _ in 0..data_len {
            let byte = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
            data.push(byte);
        }
        
        // Decode and verify checksum
        let received_checksum = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        let end_delimiter = ManchesterCodec::decode_byte(encoded_buffer, &mut offset)?;
        
        if end_delimiter != MBP_END_DELIMITER {
            return Err(MbpError::InvalidFrameStructure);
        }
        
        let frame = MbpFrame {
            destination_address,
            source_address,
            function_code,
            data,
        };
        
        let calculated_checksum = frame.calculate_checksum();
        if calculated_checksum != received_checksum {
            return Err(MbpError::ChecksumMismatch);
        }
        
        Ok(frame)
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_manchester_encoding() {
        let byte = 0xA5; // 10100101
        let encoded = ManchesterCodec::encode_byte(byte);
        
        let mut offset = 0;
        let decoded = ManchesterCodec::decode_byte(&encoded, &mut offset).unwrap();
        
        assert_eq!(byte, decoded);
    }
    
    #[test]
    fn test_frame_build_and_parse() {
        let data = vec![0x12, 0x34, 0x56, 0x78];
        let frame = MbpFrame::new(0x10, 0x20, 0x05, data).unwrap();
        
        let encoded = MbpProtocol::build_frame(&frame).unwrap();
        let parsed_frame = MbpProtocol::parse_frame(&encoded).unwrap();
        
        assert_eq!(frame.destination_address, parsed_frame.destination_address);
        assert_eq!(frame.source_address, parsed_frame.source_address);
        assert_eq!(frame.function_code, parsed_frame.function_code);
        assert_eq!(frame.data, parsed_frame.data);
    }
}
```

## Summary

**MBP (Manchester Bus Powered)** is the physical layer for Profibus PA that enables safe operation in hazardous environments. Its key features include:

- **Manchester encoding** for self-clocking, DC-balanced communication
- **Intrinsic safety** certification for explosive atmospheres (Ex zones)
- **Bus-powered operation** delivering both data and power over two wires
- **31.25 kbit/s** transmission speed optimized for process automation
- **Extended reach** up to 1900m for distributed field devices

The provided code examples demonstrate Manchester encoding/decoding and MBP frame construction, showing how low-level industrial protocols handle both data integrity and safety requirements. The C implementation focuses on embedded efficiency, while the Rust version emphasizes memory safety and error handling—both critical in industrial automation contexts.