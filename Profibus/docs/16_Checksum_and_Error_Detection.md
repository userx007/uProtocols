# Checksum and Error Detection in Profibus

## Overview

Profibus employs robust error detection mechanisms to ensure reliable communication in industrial environments. The primary method is the **Frame Check Sequence (FCS)**, which uses a Hamming Distance calculation to detect transmission errors. This is critical in noisy industrial settings where electromagnetic interference, voltage fluctuations, and other factors can corrupt data.

## Frame Check Sequence (FCS)

Profibus uses an 8-bit FCS based on a polynomial checksum algorithm. The FCS is calculated over the entire frame (excluding start/end delimiters and the FCS field itself) and appended to each telegram.

### FCS Calculation Method

The standard algorithm uses polynomial division with the generator polynomial: **x⁸ + x⁷ + x⁶ + x⁴ + 1** (0x1D1 in hex, or 0xE5 for 8-bit implementation).

### Key Characteristics:
- **Detection capability**: Detects all single-bit errors, all double-bit errors, and most multi-bit burst errors
- **Hamming Distance**: Provides HD=4 for frames up to 256 bytes
- **Low overhead**: Only 1 byte per frame
- **Fast computation**: Can be implemented in hardware or optimized software

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Profibus FCS lookup table for optimized calculation
// Generator polynomial: 0xE5 (x^8 + x^7 + x^6 + x^4 + 1)
static const uint8_t profibus_fcs_table[256] = {
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53,
    0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E,
    0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
    0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4,
    0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
    0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19,
    0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
    0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40,
    0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
    0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D,
    0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
    0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7,
    0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
    0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A,
    0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
    0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75,
    0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
    0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8,
    0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
    0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2,
    0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
    0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F,
    0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
    0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66,
    0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
    0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB,
    0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
    0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1,
    0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
    0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C,
    0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4
};

/**
 * Calculate Profibus FCS using lookup table (fast method)
 * @param data: Pointer to data buffer
 * @param length: Length of data in bytes
 * @return: Calculated FCS value
 */
uint8_t profibus_calculate_fcs(const uint8_t *data, size_t length) {
    uint8_t fcs = 0x00;  // Initial value
    
    for (size_t i = 0; i < length; i++) {
        fcs = profibus_fcs_table[fcs ^ data[i]];
    }
    
    return fcs;
}

/**
 * Calculate FCS without lookup table (slower but more portable)
 * Uses the polynomial: x^8 + x^7 + x^6 + x^4 + 1 (0xE5)
 */
uint8_t profibus_calculate_fcs_slow(const uint8_t *data, size_t length) {
    uint8_t fcs = 0x00;
    const uint8_t polynomial = 0xE5;
    
    for (size_t i = 0; i < length; i++) {
        fcs ^= data[i];
        
        for (int bit = 0; bit < 8; bit++) {
            if (fcs & 0x80) {
                fcs = (fcs << 1) ^ polynomial;
            } else {
                fcs <<= 1;
            }
        }
    }
    
    return fcs;
}

/**
 * Verify a Profibus frame including FCS check
 * @param frame: Complete frame including FCS
 * @param length: Total frame length including FCS byte
 * @return: 1 if valid, 0 if FCS error detected
 */
int profibus_verify_frame(const uint8_t *frame, size_t length) {
    if (length < 2) {
        return 0;  // Frame too short
    }
    
    // Calculate FCS over all bytes except the last one (which is the FCS itself)
    uint8_t calculated_fcs = profibus_calculate_fcs(frame, length - 1);
    uint8_t received_fcs = frame[length - 1];
    
    return (calculated_fcs == received_fcs) ? 1 : 0;
}

/**
 * Profibus frame structure example
 */
typedef struct {
    uint8_t start_delimiter;    // 0x68 for variable length frames
    uint8_t length;             // Length of data + control fields
    uint8_t length_repeat;      // Repeated length for verification
    uint8_t start_delimiter2;   // 0x68 again
    uint8_t destination_addr;   // Destination station address
    uint8_t source_addr;        // Source station address
    uint8_t function_code;      // Function code
    uint8_t data[246];          // User data (max 246 bytes)
    uint8_t fcs;                // Frame Check Sequence
    uint8_t end_delimiter;      // 0x16
} profibus_frame_t;

/**
 * Build a complete Profibus telegram with FCS
 */
size_t profibus_build_frame(uint8_t *buffer, 
                           uint8_t dest_addr, 
                           uint8_t src_addr,
                           uint8_t func_code,
                           const uint8_t *user_data,
                           size_t data_len) {
    size_t idx = 0;
    
    // Start delimiter
    buffer[idx++] = 0x68;
    
    // Length (DA + SA + FC + user data)
    uint8_t len = 3 + data_len;
    buffer[idx++] = len;
    buffer[idx++] = len;  // Repeated
    
    // Second start delimiter
    buffer[idx++] = 0x68;
    
    // Addresses and function code
    buffer[idx++] = dest_addr;
    buffer[idx++] = src_addr;
    buffer[idx++] = func_code;
    
    // User data
    if (user_data && data_len > 0) {
        memcpy(&buffer[idx], user_data, data_len);
        idx += data_len;
    }
    
    // Calculate and append FCS (over DA, SA, FC, and user data)
    buffer[idx] = profibus_calculate_fcs(&buffer[4], len);
    idx++;
    
    // End delimiter
    buffer[idx++] = 0x16;
    
    return idx;
}

// Example usage
int main() {
    // Example 1: Simple FCS calculation
    uint8_t test_data[] = {0x08, 0x00, 0x08};
    uint8_t fcs = profibus_calculate_fcs(test_data, sizeof(test_data));
    printf("FCS for test data: 0x%02X\n", fcs);
    
    // Example 2: Build a complete frame
    uint8_t frame_buffer[256];
    uint8_t user_data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    
    size_t frame_len = profibus_build_frame(
        frame_buffer,
        0x02,  // Destination address
        0x00,  // Source address (master)
        0x5D,  // Function code (Send Data with Acknowledge)
        user_data,
        sizeof(user_data)
    );
    
    printf("\nBuilt frame (%zu bytes):\n", frame_len);
    for (size_t i = 0; i < frame_len; i++) {
        printf("%02X ", frame_buffer[i]);
    }
    printf("\n");
    
    // Example 3: Verify frame
    int is_valid = profibus_verify_frame(frame_buffer, frame_len);
    printf("\nFrame verification: %s\n", is_valid ? "VALID" : "ERROR");
    
    // Example 4: Simulate transmission error
    frame_buffer[7] ^= 0x01;  // Flip one bit
    is_valid = profibus_verify_frame(frame_buffer, frame_len);
    printf("Frame with error: %s\n", is_valid ? "VALID" : "ERROR DETECTED");
    
    return 0;
}
```

## Rust Implementation

```rust
/// Profibus Frame Check Sequence (FCS) implementation in Rust
/// Generator polynomial: x^8 + x^7 + x^6 + x^4 + 1 (0xE5)

use std::fmt;

// Profibus FCS lookup table for optimized calculation
const PROFIBUS_FCS_TABLE: [u8; 256] = [
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53,
    0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E,
    0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
    0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4,
    0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
    0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19,
    0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
    0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40,
    0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
    0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D,
    0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
    0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7,
    0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
    0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A,
    0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
    0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75,
    0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
    0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8,
    0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
    0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2,
    0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
    0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F,
    0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
    0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66,
    0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
    0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB,
    0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
    0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1,
    0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
    0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C,
    0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4,
];

/// Calculate Profibus FCS using lookup table (optimized)
pub fn calculate_fcs(data: &[u8]) -> u8 {
    data.iter().fold(0u8, |fcs, &byte| {
        PROFIBUS_FCS_TABLE[(fcs ^ byte) as usize]
    })
}

/// Calculate FCS without lookup table (portable, bit-by-bit)
pub fn calculate_fcs_slow(data: &[u8]) -> u8 {
    const POLYNOMIAL: u8 = 0xE5;
    
    data.iter().fold(0u8, |mut fcs, &byte| {
        fcs ^= byte;
        
        for _ in 0..8 {
            if fcs & 0x80 != 0 {
                fcs = (fcs << 1) ^ POLYNOMIAL;
            } else {
                fcs <<= 1;
            }
        }
        fcs
    })
}

/// Error types for Profibus frame processing
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProfibusError {
    FrameTooShort,
    InvalidLength,
    InvalidDelimiter,
    FcsError,
    BufferTooSmall,
}

impl fmt::Display for ProfibusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ProfibusError::FrameTooShort => write!(f, "Frame too short"),
            ProfibusError::InvalidLength => write!(f, "Invalid length field"),
            ProfibusError::InvalidDelimiter => write!(f, "Invalid delimiter"),
            ProfibusError::FcsError => write!(f, "FCS checksum error"),
            ProfibusError::BufferTooSmall => write!(f, "Buffer too small"),
        }
    }
}

impl std::error::Error for ProfibusError {}

/// Profibus frame structure
#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub destination_addr: u8,
    pub source_addr: u8,
    pub function_code: u8,
    pub data: Vec<u8>,
}

impl ProfibusFrame {
    /// Create a new Profibus frame
    pub fn new(dest: u8, src: u8, func_code: u8, data: Vec<u8>) -> Self {
        Self {
            destination_addr: dest,
            source_addr: src,
            function_code: func_code,
            data,
        }
    }
    
    /// Encode frame into byte buffer with FCS
    pub fn encode(&self) -> Result<Vec<u8>, ProfibusError> {
        let data_len = self.data.len();
        if data_len > 246 {
            return Err(ProfibusError::BufferTooSmall);
        }
        
        let mut buffer = Vec::with_capacity(9 + data_len);
        
        // Start delimiter
        buffer.push(0x68);
        
        // Length field (DA + SA + FC + data)
        let len = 3 + data_len as u8;
        buffer.push(len);
        buffer.push(len); // Repeated
        
        // Second start delimiter
        buffer.push(0x68);
        
        // Addresses and function code
        buffer.push(self.destination_addr);
        buffer.push(self.source_addr);
        buffer.push(self.function_code);
        
        // User data
        buffer.extend_from_slice(&self.data);
        
        // Calculate FCS over DA, SA, FC, and user data
        let fcs = calculate_fcs(&buffer[4..]);
        buffer.push(fcs);
        
        // End delimiter
        buffer.push(0x16);
        
        Ok(buffer)
    }
    
    /// Decode frame from byte buffer and verify FCS
    pub fn decode(buffer: &[u8]) -> Result<Self, ProfibusError> {
        if buffer.len() < 9 {
            return Err(ProfibusError::FrameTooShort);
        }
        
        // Check start delimiters
        if buffer[0] != 0x68 || buffer[3] != 0x68 {
            return Err(ProfibusError::InvalidDelimiter);
        }
        
        // Check end delimiter
        if buffer[buffer.len() - 1] != 0x16 {
            return Err(ProfibusError::InvalidDelimiter);
        }
        
        // Verify length fields match
        if buffer[1] != buffer[2] {
            return Err(ProfibusError::InvalidLength);
        }
        
        let len = buffer[1] as usize;
        
        // Verify FCS
        let fcs_calc = calculate_fcs(&buffer[4..4 + len]);
        let fcs_recv = buffer[4 + len];
        
        if fcs_calc != fcs_recv {
            return Err(ProfibusError::FcsError);
        }
        
        // Extract frame fields
        let destination_addr = buffer[4];
        let source_addr = buffer[5];
        let function_code = buffer[6];
        let data = buffer[7..4 + len].to_vec();
        
        Ok(Self {
            destination_addr,
            source_addr,
            function_code,
            data,
        })
    }
    
    /// Verify FCS of a complete frame buffer
    pub fn verify_fcs(buffer: &[u8]) -> Result<bool, ProfibusError> {
        if buffer.len() < 9 {
            return Err(ProfibusError::FrameTooShort);
        }
        
        let len = buffer[1] as usize;
        if buffer.len() < 6 + len {
            return Err(ProfibusError::FrameTooShort);
        }
        
        let fcs_calc = calculate_fcs(&buffer[4..4 + len]);
        let fcs_recv = buffer[4 + len];
        
        Ok(fcs_calc == fcs_recv)
    }
}

// Example usage and tests
fn main() {
    println!("=== Profibus FCS Examples ===\n");
    
    // Example 1: Simple FCS calculation
    let test_data = [0x08, 0x00, 0x08];
    let fcs = calculate_fcs(&test_data);
    println!("Example 1: FCS for {:02X?} = 0x{:02X}", test_data, fcs);
    
    // Example 2: Build and encode a frame
    let frame = ProfibusFrame::new(
        0x02,                           // Destination
        0x00,                           // Source
        0x5D,                           // Function code
        vec![0xAA, 0xBB, 0xCC, 0xDD],  // User data
    );
    
    match frame.encode() {
        Ok(encoded) => {
            println!("\nExample 2: Encoded frame ({} bytes):", encoded.len());
            for (i, byte) in encoded.iter().enumerate() {
                print!("{:02X} ", byte);
                if (i + 1) % 16 == 0 {
                    println!();
                }
            }
            println!("\n");
            
            // Example 3: Verify the frame
            match ProfibusFrame::verify_fcs(&encoded) {
                Ok(valid) => println!("Example 3: Frame FCS verification: {}", 
                    if valid { "VALID ✓" } else { "INVALID ✗" }),
                Err(e) => println!("Verification error: {}", e),
            }
            
            // Example 4: Decode the frame
            match ProfibusFrame::decode(&encoded) {
                Ok(decoded) => {
                    println!("\nExample 4: Decoded frame:");
                    println!("  Destination: 0x{:02X}", decoded.destination_addr);
                    println!("  Source: 0x{:02X}", decoded.source_addr);
                    println!("  Function Code: 0x{:02X}", decoded.function_code);
                    println!("  Data: {:02X?}", decoded.data);
                }
                Err(e) => println!("Decode error: {}", e),
            }
            
            // Example 5: Simulate transmission error
            let mut corrupted = encoded.clone();
            corrupted[7] ^= 0x01;  // Flip one bit in data
            
            match ProfibusFrame::verify_fcs(&corrupted) {
                Ok(valid) => println!("\nExample 5: Corrupted frame verification: {}", 
                    if valid { "VALID ✓" } else { "ERROR DETECTED ✗" }),
                Err(e) => println!("Verification error: {}", e),
            }
        }
        Err(e) => println!("Encoding error: {}", e),
    }
    
    // Example 6: Compare fast vs slow FCS calculation
    let large_data: Vec<u8> = (0..100).collect();
    let fcs_fast = calculate_fcs(&large_data);
    let fcs_slow = calculate_fcs_slow(&large_data);
    
    println!("\nExample 6: FCS algorithm comparison:");
    println!("  Fast (table): 0x{:02X}", fcs_fast);
    println!("  Slow (bitwise): 0x{:02X}", fcs_slow);
    println!("  Match: {}", if fcs_fast == fcs_slow { "✓" } else { "✗" });
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_fcs_calculation() {
        let data = [0x08, 0x00, 0x08];
        let fcs = calculate_fcs(&data);
        assert_eq!(fcs, calculate_fcs_slow(&data));
    }
    
    #[test]
    fn test_frame_encode_decode() {
        let original = ProfibusFrame::new(0x02, 0x00, 0x5D, vec![0xAA, 0xBB]);
        let encoded = original.encode().unwrap();
        let decoded = ProfibusFrame::decode(&encoded).unwrap();
        
        assert_eq!(original.destination_addr, decoded.destination_addr);
        assert_eq!(original.source_addr, decoded.source_addr);
        assert_eq!(original.function_code, decoded.function_code);
        assert_eq!(original.data, decoded.data);
    }
    
    #[test]
    fn test_error_detection() {
        let frame = ProfibusFrame::new(0x02, 0x00, 0x5D, vec![0xAA]);
        let mut encoded = frame.encode().unwrap();
        
        // Corrupt one bit
        encoded[7] ^= 0x01;
        
        assert!(matches!(
            ProfibusFrame::decode(&encoded),
            Err(ProfibusError::FcsError)
        ));
    }
}
```

## Key Implementation Details

### 1. **Lookup Table Optimization**
Both implementations use a pre-calculated 256-byte lookup table for fast FCS computation. This reduces the operation to a simple XOR and table lookup per byte, making it suitable for real-time systems.

### 2. **Alternative Bit-by-Bit Method**
A slower but more portable implementation is provided that calculates the FCS bit-by-bit using polynomial division. This is useful for systems with limited memory or for verification purposes.

### 3. **Frame Structure**
Variable-length frames (SD2 format) include:
- Start delimiter: `0x68`
- Length field (repeated for validation)
- Addresses, function code, and data
- FCS byte
- End delimiter: `0x16`

### 4. **Error Detection Capabilities**
The 8-bit FCS can detect:
- All single-bit errors
- All double-bit errors  
- All odd numbers of bit errors
- Most burst errors up to 8 bits
- High percentage of longer burst errors

## Summary

Profibus employs a robust polynomial-based Frame Check Sequence for error detection in industrial environments. The FCS:

- **Uses an 8-bit checksum** calculated with the polynomial x⁸ + x⁷ + x⁶ + x⁴ + 1
- **Provides Hamming Distance 4** for frames up to 256 bytes, ensuring high reliability
- **Can be optimized** using lookup tables for real-time performance
- **Operates on the entire frame** excluding delimiters and the FCS field itself
- **Enables automatic error detection** at the receiving end without retransmission overhead

The implementations demonstrate both optimized (table-based) and portable (bit-by-bit) approaches, along with complete frame encoding/decoding with error handling. This makes Profibus suitable for safety-critical industrial automation where data integrity is paramount.