# Profibus Frame Structure: Detailed Analysis

## Overview

The Profibus frame structure defines how data is packaged and transmitted across the Profibus network. Understanding this structure is critical for implementing reliable communication protocols, parsing incoming messages, and constructing valid telegrams for transmission.

## Frame Components

A Profibus telegram consists of several key fields that work together to ensure reliable data transmission:

### 1. **SD (Start Delimiter)**
- Marks the beginning of a frame
- Indicates the frame type
- Common values:
  - `0x10`: Short frame without data field
  - `0x68`: Long frame with variable data field
  - `0xA2`: Token frame
  - `0xDC`: Short acknowledgment

### 2. **DA (Destination Address)**
- Identifies the target device (1 byte)
- Range: 0-126 for stations, 127 for broadcast

### 3. **SA (Source Address)**
- Identifies the sending device (1 byte)
- Range: 0-126

### 4. **FC (Function Code)**
- Defines the function and frame properties (1 byte)
- Bits contain:
  - Frame type (request/response)
  - Function code (read, write, etc.)
  - Station type (master/slave)
  - Priority information

### 5. **PDU (Protocol Data Unit)**
- The actual payload/data
- Variable length depending on frame type
- Contains application-specific information

### 6. **FCS (Frame Check Sequence)**
- Checksum for error detection (1 byte)
- Calculated over DA, SA, FC, and PDU fields
- Uses simple addition modulo 256

### 7. **ED (End Delimiter)**
- Marks the end of the frame
- Value: `0x16`

## Frame Types

### Short Frame (Without Data)
```
| SD1 | DA | SA | FC | FCS | ED |
| 0x10| 1B | 1B | 1B | 1B  |0x16|
```

### Long Frame (With Variable Data)
```
| SD2 | LE | LEr| SD2 | DA | SA | FC | PDU | FCS | ED |
| 0x68| 1B | 1B |0x68 | 1B | 1B | 1B | var | 1B  |0x16|
```
- LE (Length): Number of bytes from DA to PDU
- LEr (Length repeat): Redundant length for verification

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Frame delimiters
#define SD1 0x10  // Short frame
#define SD2 0x68  // Long frame
#define SD3 0xA2  // Token frame
#define SD4 0xDC  // Short acknowledgment
#define ED  0x16  // End delimiter

// Maximum frame sizes
#define MAX_PDU_LENGTH 246
#define MAX_FRAME_LENGTH 256

// Function codes (examples)
#define FC_SRD_LOW  0x0C  // Send and Request Data Low Priority
#define FC_SRD_HIGH 0x0D  // Send and Request Data High Priority
#define FC_SDN_LOW  0x0E  // Send Data with No Acknowledge Low
#define FC_SDN_HIGH 0x0F  // Send Data with No Acknowledge High

typedef struct {
    uint8_t sd;           // Start delimiter
    uint8_t da;           // Destination address
    uint8_t sa;           // Source address
    uint8_t fc;           // Function code
    uint8_t pdu[MAX_PDU_LENGTH]; // Protocol data unit
    uint16_t pdu_length;  // Actual PDU length
    uint8_t fcs;          // Frame check sequence
    uint8_t ed;           // End delimiter
} profibus_frame_t;

// Calculate FCS (Frame Check Sequence)
uint8_t calculate_fcs(const uint8_t *data, uint16_t length) {
    uint8_t fcs = 0;
    for (uint16_t i = 0; i < length; i++) {
        fcs += data[i];
    }
    return fcs;
}

// Build a short frame (no data)
int build_short_frame(uint8_t *buffer, uint8_t da, uint8_t sa, uint8_t fc) {
    buffer[0] = SD1;
    buffer[1] = da;
    buffer[2] = sa;
    buffer[3] = fc;
    
    // Calculate FCS over DA, SA, FC
    buffer[4] = calculate_fcs(&buffer[1], 3);
    buffer[5] = ED;
    
    return 6; // Frame length
}

// Build a long frame (with data)
int build_long_frame(uint8_t *buffer, uint8_t da, uint8_t sa, 
                     uint8_t fc, const uint8_t *pdu, uint16_t pdu_length) {
    if (pdu_length > MAX_PDU_LENGTH) {
        return -1; // Error: PDU too long
    }
    
    uint8_t le = 3 + pdu_length; // DA + SA + FC + PDU
    int pos = 0;
    
    buffer[pos++] = SD2;
    buffer[pos++] = le;
    buffer[pos++] = le;  // Repeated length
    buffer[pos++] = SD2;
    buffer[pos++] = da;
    buffer[pos++] = sa;
    buffer[pos++] = fc;
    
    // Copy PDU
    if (pdu_length > 0) {
        memcpy(&buffer[pos], pdu, pdu_length);
        pos += pdu_length;
    }
    
    // Calculate FCS over DA, SA, FC, PDU
    buffer[pos] = calculate_fcs(&buffer[4], le);
    pos++;
    
    buffer[pos++] = ED;
    
    return pos; // Total frame length
}

// Parse and validate a received frame
bool parse_frame(const uint8_t *buffer, uint16_t buffer_length, 
                 profibus_frame_t *frame) {
    if (buffer_length < 6) {
        return false; // Too short
    }
    
    frame->sd = buffer[0];
    
    if (frame->sd == SD1) {
        // Short frame
        if (buffer_length != 6) return false;
        
        frame->da = buffer[1];
        frame->sa = buffer[2];
        frame->fc = buffer[3];
        frame->pdu_length = 0;
        frame->fcs = buffer[4];
        frame->ed = buffer[5];
        
        // Verify FCS
        uint8_t calc_fcs = calculate_fcs(&buffer[1], 3);
        if (calc_fcs != frame->fcs) return false;
        
    } else if (frame->sd == SD2) {
        // Long frame
        if (buffer_length < 9) return false;
        
        uint8_t le = buffer[1];
        uint8_t ler = buffer[2];
        
        // Verify repeated length
        if (le != ler) return false;
        if (buffer[3] != SD2) return false;
        
        frame->da = buffer[4];
        frame->sa = buffer[5];
        frame->fc = buffer[6];
        frame->pdu_length = le - 3;
        
        if (frame->pdu_length > MAX_PDU_LENGTH) return false;
        if (buffer_length < (9 + frame->pdu_length)) return false;
        
        // Copy PDU
        memcpy(frame->pdu, &buffer[7], frame->pdu_length);
        
        frame->fcs = buffer[7 + frame->pdu_length];
        frame->ed = buffer[8 + frame->pdu_length];
        
        // Verify FCS
        uint8_t calc_fcs = calculate_fcs(&buffer[4], le);
        if (calc_fcs != frame->fcs) return false;
    } else {
        return false; // Unknown frame type
    }
    
    // Verify end delimiter
    if (frame->ed != ED) return false;
    
    return true;
}

// Example usage
void example_usage(void) {
    uint8_t tx_buffer[MAX_FRAME_LENGTH];
    profibus_frame_t rx_frame;
    
    // Build short frame
    int len = build_short_frame(tx_buffer, 5, 0, FC_SRD_LOW);
    
    // Build long frame with data
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    len = build_long_frame(tx_buffer, 10, 0, FC_SDN_LOW, data, 4);
    
    // Parse received frame
    if (parse_frame(tx_buffer, len, &rx_frame)) {
        // Frame valid - process it
    }
}
```

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

// Frame delimiters
const SD1: u8 = 0x10;  // Short frame
const SD2: u8 = 0x68;  // Long frame
const SD3: u8 = 0xA2;  // Token frame
const SD4: u8 = 0xDC;  // Short acknowledgment
const ED: u8 = 0x16;   // End delimiter

const MAX_PDU_LENGTH: usize = 246;

// Function codes
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum FunctionCode {
    SendRequestDataLow = 0x0C,
    SendRequestDataHigh = 0x0D,
    SendDataNoAckLow = 0x0E,
    SendDataNoAckHigh = 0x0F,
}

#[derive(Debug)]
pub enum FrameError {
    TooShort,
    InvalidLength,
    InvalidChecksum,
    InvalidDelimiter,
    PduTooLong,
}

impl fmt::Display for FrameError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            FrameError::TooShort => write!(f, "Frame too short"),
            FrameError::InvalidLength => write!(f, "Invalid length field"),
            FrameError::InvalidChecksum => write!(f, "Checksum mismatch"),
            FrameError::InvalidDelimiter => write!(f, "Invalid delimiter"),
            FrameError::PduTooLong => write!(f, "PDU exceeds maximum length"),
        }
    }
}

impl Error for FrameError {}

#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub sd: u8,
    pub da: u8,
    pub sa: u8,
    pub fc: u8,
    pub pdu: Vec<u8>,
    pub fcs: u8,
    pub ed: u8,
}

impl ProfibusFrame {
    /// Calculate frame check sequence
    fn calculate_fcs(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &byte| acc.wrapping_add(byte))
    }

    /// Build a short frame (no data)
    pub fn build_short(da: u8, sa: u8, fc: u8) -> Vec<u8> {
        let mut frame = vec![SD1, da, sa, fc];
        let fcs = Self::calculate_fcs(&frame[1..]);
        frame.push(fcs);
        frame.push(ED);
        frame
    }

    /// Build a long frame (with data)
    pub fn build_long(da: u8, sa: u8, fc: u8, pdu: &[u8]) -> Result<Vec<u8>, FrameError> {
        if pdu.len() > MAX_PDU_LENGTH {
            return Err(FrameError::PduTooLong);
        }

        let le = (3 + pdu.len()) as u8; // DA + SA + FC + PDU
        let mut frame = Vec::with_capacity(9 + pdu.len());

        frame.push(SD2);
        frame.push(le);
        frame.push(le); // Repeated length
        frame.push(SD2);
        frame.push(da);
        frame.push(sa);
        frame.push(fc);
        frame.extend_from_slice(pdu);

        // Calculate FCS over DA, SA, FC, PDU
        let fcs = Self::calculate_fcs(&frame[4..]);
        frame.push(fcs);
        frame.push(ED);

        Ok(frame)
    }

    /// Parse a received frame
    pub fn parse(buffer: &[u8]) -> Result<Self, FrameError> {
        if buffer.len() < 6 {
            return Err(FrameError::TooShort);
        }

        let sd = buffer[0];

        match sd {
            SD1 => {
                // Short frame
                if buffer.len() != 6 {
                    return Err(FrameError::InvalidLength);
                }

                let da = buffer[1];
                let sa = buffer[2];
                let fc = buffer[3];
                let fcs = buffer[4];
                let ed = buffer[5];

                // Verify FCS
                let calc_fcs = Self::calculate_fcs(&buffer[1..4]);
                if calc_fcs != fcs {
                    return Err(FrameError::InvalidChecksum);
                }

                if ed != ED {
                    return Err(FrameError::InvalidDelimiter);
                }

                Ok(ProfibusFrame {
                    sd,
                    da,
                    sa,
                    fc,
                    pdu: Vec::new(),
                    fcs,
                    ed,
                })
            }
            SD2 => {
                // Long frame
                if buffer.len() < 9 {
                    return Err(FrameError::TooShort);
                }

                let le = buffer[1];
                let ler = buffer[2];

                if le != ler {
                    return Err(FrameError::InvalidLength);
                }

                if buffer[3] != SD2 {
                    return Err(FrameError::InvalidDelimiter);
                }

                let da = buffer[4];
                let sa = buffer[5];
                let fc = buffer[6];
                let pdu_length = (le - 3) as usize;

                if pdu_length > MAX_PDU_LENGTH {
                    return Err(FrameError::PduTooLong);
                }

                if buffer.len() < (9 + pdu_length) {
                    return Err(FrameError::TooShort);
                }

                let pdu = buffer[7..7 + pdu_length].to_vec();
                let fcs = buffer[7 + pdu_length];
                let ed = buffer[8 + pdu_length];

                // Verify FCS
                let calc_fcs = Self::calculate_fcs(&buffer[4..7 + pdu_length]);
                if calc_fcs != fcs {
                    return Err(FrameError::InvalidChecksum);
                }

                if ed != ED {
                    return Err(FrameError::InvalidDelimiter);
                }

                Ok(ProfibusFrame {
                    sd,
                    da,
                    sa,
                    fc,
                    pdu,
                    fcs,
                    ed,
                })
            }
            _ => Err(FrameError::InvalidDelimiter),
        }
    }

    /// Serialize frame to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        if self.pdu.is_empty() {
            Self::build_short(self.da, self.sa, self.fc)
        } else {
            Self::build_long(self.da, self.sa, self.fc, &self.pdu).unwrap()
        }
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    // Build short frame
    let short_frame = ProfibusFrame::build_short(5, 0, 0x0C);
    println!("Short frame: {:02X?}", short_frame);

    // Build long frame
    let data = vec![0x01, 0x02, 0x03, 0x04];
    let long_frame = ProfibusFrame::build_long(10, 0, 0x0E, &data)?;
    println!("Long frame: {:02X?}", long_frame);

    // Parse frame
    let parsed = ProfibusFrame::parse(&long_frame)?;
    println!("Parsed frame: DA={}, SA={}, FC={:02X}, PDU={:02X?}",
             parsed.da, parsed.sa, parsed.fc, parsed.pdu);

    Ok(())
}
```

## Summary

**Profibus Frame Structure** defines the precise format for packaging data in Profibus telegrams. Each frame contains:
- **Start/End Delimiters** (SD/ED) for frame boundary detection
- **Address fields** (DA/SA) identifying source and destination
- **Function Code** (FC) defining the operation and frame properties
- **PDU** carrying the actual payload data
- **Frame Check Sequence** (FCS) for error detection

The implementation examples demonstrate how to build, serialize, parse, and validate Profibus frames in both C/C++ and Rust, including proper checksum calculation and error handling. Understanding this structure is fundamental for any Profibus master/slave implementation, enabling reliable industrial communication with proper error detection and frame validation mechanisms.