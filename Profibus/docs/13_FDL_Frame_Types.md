# FDL Frame Types in Profibus

## Overview

The Fieldbus Data Link (FDL) layer in Profibus uses five distinct telegram (frame) types to manage communication between devices. These frame types are identified by Start Delimiters (SD) and serve different purposes in the protocol stack. Understanding these frame structures is essential for implementing Profibus communication at the data link layer.

## The Five FDL Frame Types

### 1. **SD1 - Fixed Length Frame without Data Field**
- **Format**: `SD1 | DA | SA | FC | FCS | ED`
- **Length**: 6 bytes
- **Use**: Tokens, acknowledgments, and short control messages
- **Fields**:
  - SD1 (0x10): Start delimiter
  - DA: Destination address
  - SA: Source address
  - FC: Frame control byte
  - FCS: Frame check sequence
  - ED (0x16): End delimiter

### 2. **SD2 - Variable Length Frame with Data Field**
- **Format**: `SD2 | LE | LEr | SD2 | DA | SA | FC | [Data] | FCS | ED`
- **Length**: 11+ bytes (max 256 bytes data)
- **Use**: Data transmission, requests, and responses
- **Fields**:
  - SD2 (0x68): Start delimiter
  - LE: Length field (data + DA + SA + FC)
  - LEr: Repeated length field (for verification)
  - Data: Variable length payload

### 3. **SD3 - Fixed Length Frame with Data Field**
- **Format**: `SD3 | DA | SA | FC | [5 bytes Data] | FCS | ED`
- **Length**: 11 bytes
- **Use**: Short data telegrams with fixed 5-byte payload
- **Fields**:
  - SD3 (0xA2): Start delimiter
  - Fixed 5-byte data field

### 4. **SD4 - Token Frame**
- **Format**: `SD4 | DA | SA | FCS | ED`
- **Length**: 3 bytes
- **Use**: Token passing in the logical token ring
- **Fields**:
  - SD4 (0xDC): Start delimiter
  - DA: Destination address (next token holder)
  - SA: Source address (current token holder)

### 5. **SC - Short Acknowledgment**
- **Format**: `SC`
- **Length**: 1 byte
- **Use**: Immediate positive acknowledgment
- **Value**: 0xE5

## Code Examples

### C/C++ Implementation

```cpp
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Start delimiter constants
#define SD1 0x10
#define SD2 0x68
#define SD3 0xA2
#define SD4 0xDC
#define SC  0xE5
#define ED  0x16

// Frame types
typedef enum {
    FRAME_SD1,
    FRAME_SD2,
    FRAME_SD3,
    FRAME_SD4,
    FRAME_SC
} FrameType;

// SD1 Frame structure (6 bytes)
typedef struct __attribute__((packed)) {
    uint8_t sd1;      // 0x10
    uint8_t da;       // Destination address
    uint8_t sa;       // Source address
    uint8_t fc;       // Frame control
    uint8_t fcs;      // Frame check sequence
    uint8_t ed;       // 0x16
} SD1Frame;

// SD2 Frame structure (variable length)
typedef struct __attribute__((packed)) {
    uint8_t sd2;      // 0x68
    uint8_t le;       // Length
    uint8_t ler;      // Length repeated
    uint8_t sd2_rep;  // 0x68 repeated
    uint8_t da;       // Destination address
    uint8_t sa;       // Source address
    uint8_t fc;       // Frame control
    uint8_t data[];   // Variable length data + FCS + ED
} SD2Frame;

// SD3 Frame structure (11 bytes)
typedef struct __attribute__((packed)) {
    uint8_t sd3;      // 0xA2
    uint8_t da;       // Destination address
    uint8_t sa;       // Source address
    uint8_t fc;       // Frame control
    uint8_t data[5];  // Fixed 5 bytes
    uint8_t fcs;      // Frame check sequence
    uint8_t ed;       // 0x16
} SD3Frame;

// SD4 Frame structure (3 bytes)
typedef struct __attribute__((packed)) {
    uint8_t sd4;      // 0xDC
    uint8_t da;       // Destination address
    uint8_t sa;       // Source address
} SD4Frame;

// Calculate FCS (simple XOR checksum for Profibus)
uint8_t calculate_fcs(const uint8_t* data, size_t len) {
    uint8_t fcs = 0;
    for (size_t i = 0; i < len; i++) {
        fcs ^= data[i];
    }
    return fcs;
}

// Build SD1 frame
void build_sd1_frame(SD1Frame* frame, uint8_t da, uint8_t sa, uint8_t fc) {
    frame->sd1 = SD1;
    frame->da = da;
    frame->sa = sa;
    frame->fc = fc;
    frame->ed = ED;
    
    // Calculate FCS over DA, SA, FC
    uint8_t fcs_data[3] = {da, sa, fc};
    frame->fcs = calculate_fcs(fcs_data, 3);
}

// Build SD2 frame
size_t build_sd2_frame(uint8_t* buffer, uint8_t da, uint8_t sa, 
                       uint8_t fc, const uint8_t* data, uint8_t data_len) {
    size_t idx = 0;
    
    // Calculate length (DA + SA + FC + data)
    uint8_t le = 3 + data_len;
    
    buffer[idx++] = SD2;
    buffer[idx++] = le;
    buffer[idx++] = le;  // Repeated
    buffer[idx++] = SD2; // Repeated
    buffer[idx++] = da;
    buffer[idx++] = sa;
    buffer[idx++] = fc;
    
    // Copy data
    memcpy(&buffer[idx], data, data_len);
    idx += data_len;
    
    // Calculate FCS over DA, SA, FC, and data
    uint8_t fcs = da ^ sa ^ fc;
    for (uint8_t i = 0; i < data_len; i++) {
        fcs ^= data[i];
    }
    
    buffer[idx++] = fcs;
    buffer[idx++] = ED;
    
    return idx;
}

// Parse incoming frame type
FrameType identify_frame(const uint8_t* buffer) {
    switch (buffer[0]) {
        case SD1: return FRAME_SD1;
        case SD2: return FRAME_SD2;
        case SD3: return FRAME_SD3;
        case SD4: return FRAME_SD4;
        case SC:  return FRAME_SC;
        default:  return FRAME_SD1; // Invalid
    }
}

// Example usage
int main() {
    // Create SD1 frame (token acknowledgment)
    SD1Frame sd1;
    build_sd1_frame(&sd1, 0x05, 0x02, 0x49);
    
    printf("SD1 Frame: ");
    uint8_t* ptr = (uint8_t*)&sd1;
    for (size_t i = 0; i < sizeof(SD1Frame); i++) {
        printf("%02X ", ptr[i]);
    }
    printf("\n");
    
    // Create SD2 frame (data transmission)
    uint8_t sd2_buffer[256];
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    size_t sd2_len = build_sd2_frame(sd2_buffer, 0x05, 0x02, 
                                     0x73, payload, 4);
    
    printf("SD2 Frame: ");
    for (size_t i = 0; i < sd2_len; i++) {
        printf("%02X ", sd2_buffer[i]);
    }
    printf("\n");
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

// Start delimiter constants
const SD1: u8 = 0x10;
const SD2: u8 = 0x68;
const SD3: u8 = 0xA2;
const SD4: u8 = 0xDC;
const SC: u8 = 0xE5;
const ED: u8 = 0x16;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FrameType {
    SD1,
    SD2,
    SD3,
    SD4,
    SC,
}

// SD1 Frame (6 bytes)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct SD1Frame {
    pub sd1: u8,
    pub da: u8,
    pub sa: u8,
    pub fc: u8,
    pub fcs: u8,
    pub ed: u8,
}

impl SD1Frame {
    pub fn new(da: u8, sa: u8, fc: u8) -> Self {
        let fcs = da ^ sa ^ fc;
        SD1Frame {
            sd1: SD1,
            da,
            sa,
            fc,
            fcs,
            ed: ED,
        }
    }
    
    pub fn as_bytes(&self) -> [u8; 6] {
        [self.sd1, self.da, self.sa, self.fc, self.fcs, self.ed]
    }
    
    pub fn verify_fcs(&self) -> bool {
        let calculated = self.da ^ self.sa ^ self.fc;
        calculated == self.fcs
    }
}

// SD2 Frame (variable length)
#[derive(Debug, Clone)]
pub struct SD2Frame {
    pub da: u8,
    pub sa: u8,
    pub fc: u8,
    pub data: Vec<u8>,
}

impl SD2Frame {
    pub fn new(da: u8, sa: u8, fc: u8, data: Vec<u8>) -> Result<Self, &'static str> {
        if data.len() > 246 {
            return Err("Data too long for SD2 frame");
        }
        Ok(SD2Frame { da, sa, fc, data })
    }
    
    pub fn to_bytes(&self) -> Vec<u8> {
        let le = 3 + self.data.len() as u8;
        let mut buffer = Vec::with_capacity(11 + self.data.len());
        
        buffer.push(SD2);
        buffer.push(le);
        buffer.push(le); // Repeated
        buffer.push(SD2); // Repeated
        buffer.push(self.da);
        buffer.push(self.sa);
        buffer.push(self.fc);
        buffer.extend_from_slice(&self.data);
        
        // Calculate FCS
        let mut fcs = self.da ^ self.sa ^ self.fc;
        for byte in &self.data {
            fcs ^= byte;
        }
        
        buffer.push(fcs);
        buffer.push(ED);
        
        buffer
    }
    
    pub fn from_bytes(buffer: &[u8]) -> Result<Self, &'static str> {
        if buffer.len() < 11 {
            return Err("Buffer too short for SD2 frame");
        }
        if buffer[0] != SD2 || buffer[3] != SD2 {
            return Err("Invalid SD2 start delimiter");
        }
        
        let le = buffer[1];
        if buffer[2] != le {
            return Err("Length mismatch");
        }
        
        let da = buffer[4];
        let sa = buffer[5];
        let fc = buffer[6];
        
        let data_len = (le as usize) - 3;
        let data = buffer[7..7 + data_len].to_vec();
        
        // Verify FCS
        let fcs_idx = 7 + data_len;
        let received_fcs = buffer[fcs_idx];
        let mut calculated_fcs = da ^ sa ^ fc;
        for byte in &data {
            calculated_fcs ^= byte;
        }
        
        if received_fcs != calculated_fcs {
            return Err("FCS mismatch");
        }
        
        Ok(SD2Frame { da, sa, fc, data })
    }
}

// SD3 Frame (11 bytes, fixed 5-byte data)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct SD3Frame {
    pub sd3: u8,
    pub da: u8,
    pub sa: u8,
    pub fc: u8,
    pub data: [u8; 5],
    pub fcs: u8,
    pub ed: u8,
}

impl SD3Frame {
    pub fn new(da: u8, sa: u8, fc: u8, data: [u8; 5]) -> Self {
        let mut fcs = da ^ sa ^ fc;
        for byte in &data {
            fcs ^= byte;
        }
        
        SD3Frame {
            sd3: SD3,
            da,
            sa,
            fc,
            data,
            fcs,
            ed: ED,
        }
    }
}

// SD4 Frame (3 bytes - token)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct SD4Frame {
    pub sd4: u8,
    pub da: u8,
    pub sa: u8,
}

impl SD4Frame {
    pub fn new(da: u8, sa: u8) -> Self {
        SD4Frame {
            sd4: SD4,
            da,
            sa,
        }
    }
    
    pub fn as_bytes(&self) -> [u8; 3] {
        [self.sd4, self.da, self.sa]
    }
}

// Frame identifier
pub fn identify_frame(buffer: &[u8]) -> Option<FrameType> {
    if buffer.is_empty() {
        return None;
    }
    
    match buffer[0] {
        SD1 => Some(FrameType::SD1),
        SD2 => Some(FrameType::SD2),
        SD3 => Some(FrameType::SD3),
        SD4 => Some(FrameType::SD4),
        SC => Some(FrameType::SC),
        _ => None,
    }
}

fn main() {
    // Create SD1 frame
    let sd1 = SD1Frame::new(0x05, 0x02, 0x49);
    println!("SD1 Frame: {:02X?}", sd1.as_bytes());
    println!("FCS Valid: {}", sd1.verify_fcs());
    
    // Create SD2 frame
    let payload = vec![0x01, 0x02, 0x03, 0x04];
    let sd2 = SD2Frame::new(0x05, 0x02, 0x73, payload).unwrap();
    let sd2_bytes = sd2.to_bytes();
    println!("\nSD2 Frame: {:02X?}", sd2_bytes);
    
    // Parse SD2 frame back
    let parsed = SD2Frame::from_bytes(&sd2_bytes).unwrap();
    println!("Parsed SD2 - DA: 0x{:02X}, SA: 0x{:02X}, Data: {:02X?}", 
             parsed.da, parsed.sa, parsed.data);
    
    // Create SD3 frame
    let sd3 = SD3Frame::new(0x05, 0x02, 0x73, [0xAA, 0xBB, 0xCC, 0xDD, 0xEE]);
    println!("\nSD3 Frame created with 5-byte payload");
    
    // Create SD4 token frame
    let sd4 = SD4Frame::new(0x03, 0x02);
    println!("\nSD4 Token Frame: {:02X?}", sd4.as_bytes());
    
    // Identify frame types
    println!("\nFrame identification:");
    println!("0x10 -> {:?}", identify_frame(&[0x10]));
    println!("0x68 -> {:?}", identify_frame(&[0x68]));
    println!("0xE5 -> {:?}", identify_frame(&[0xE5]));
}
```

## Summary

FDL frame types form the foundation of Profibus communication at the data link layer. The five frame types serve distinct purposes: SD1 handles fixed-length control messages and acknowledgments, SD2 manages variable-length data transfers up to 256 bytes, SD3 provides efficient transmission of fixed 5-byte payloads, SD4 implements the token-passing mechanism for bus arbitration, and SC offers single-byte positive acknowledgments. Each frame includes checksums (FCS) for error detection, with SD2 using redundant length fields for additional reliability. Understanding these structures is critical for implementing Profibus protocol stacks, debugging communication issues, and optimizing network performance. The code examples demonstrate frame construction, validation, and parsing in both C/C++ and Rust, showing how to handle the packed binary structures and checksum calculations required for real-world Profibus implementations.