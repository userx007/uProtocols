# CAN Frame Structure: A Detailed Technical Guide

## Introduction

The Controller Area Network (CAN) bus uses a sophisticated frame structure to ensure reliable, prioritized communication between Electronic Control Units (ECUs). Understanding the frame structure is essential for proper CAN programming and debugging.

## CAN Frame Types

CAN protocol defines four frame types:

1. **Data Frame** - Carries data from transmitter to receivers
2. **Remote Frame** - Requests data from another node
3. **Error Frame** - Transmitted when an error is detected
4. **Overload Frame** - Introduces delay between data/remote frames

We'll focus primarily on the **Data Frame**, as it's the most commonly used.

## Standard vs Extended Frame Formats

CAN supports two frame formats:

- **Standard (CAN 2.0A)**: 11-bit identifier (2,048 unique IDs)
- **Extended (CAN 2.0B)**: 29-bit identifier (536+ million unique IDs)

## Data Frame Structure Breakdown

### 1. Start of Frame (SOF)

The SOF is a single **dominant bit** (logic 0) that marks the beginning of a frame and synchronizes all nodes on the bus.

**Characteristics:**
- 1 bit in length
- Always dominant (0)
- Allows nodes to synchronize on the transmission

### 2. Arbitration Field

This field determines message priority and contains the identifier.

**Standard Frame (11-bit ID):**
- **Identifier (11 bits)**: Message priority (lower value = higher priority)
- **RTR bit (1 bit)**: Remote Transmission Request
  - Dominant (0) = Data Frame
  - Recessive (1) = Remote Frame

**Extended Frame (29-bit ID):**
- **Base Identifier (11 bits)**: Most significant part
- **SRR bit (1 bit)**: Substitute Remote Request (always recessive)
- **IDE bit (1 bit)**: Identifier Extension
  - Dominant (0) = Standard frame
  - Recessive (1) = Extended frame
- **Extended Identifier (18 bits)**: Least significant part
- **RTR bit (1 bit)**: Remote Transmission Request

**Arbitration Process:**
During simultaneous transmissions, nodes compare their identifiers bit-by-bit. A dominant bit (0) wins over recessive (1). The node with the lowest identifier value wins arbitration and continues transmitting.

### 3. Control Field

**Standard Frame:**
- **IDE bit (1 bit)**: Always dominant (0) for standard frames
- **r0 bit (1 bit)**: Reserved, must be dominant (0)
- **DLC (4 bits)**: Data Length Code (0-8 bytes)

**Extended Frame:**
- **r1 bit (1 bit)**: Reserved
- **r0 bit (1 bit)**: Reserved
- **DLC (4 bits)**: Data Length Code (0-8 bytes)

The DLC specifies how many data bytes follow (0-8 for CAN 2.0, up to 64 for CAN FD).

### 4. Data Field

Contains the actual payload data:
- Length: 0 to 8 bytes (CAN 2.0) or 0 to 64 bytes (CAN FD)
- Most significant byte transmitted first
- Each byte transmitted MSB first

### 5. CRC Field (Cyclic Redundancy Check)

Ensures data integrity:
- **CRC Sequence (15 bits)**: Calculated over SOF, Arbitration, Control, and Data fields
- **CRC Delimiter (1 bit)**: Recessive bit (1)

The receiving nodes calculate their own CRC and compare it with the transmitted CRC. A mismatch triggers an error frame.

### 6. ACK Field (Acknowledgment)

Confirms successful reception:
- **ACK Slot (1 bit)**: Transmitter sends recessive (1), receivers override with dominant (0) if frame received correctly
- **ACK Delimiter (1 bit)**: Recessive bit (1)

If no node acknowledges (ACK slot remains recessive), the transmitter detects an error.

### 7. End of Frame (EOF)

Marks the frame's end:
- **7 recessive bits** (all logic 1)
- Signals that the frame transmission is complete

After EOF, there's an **Interframe Space (IFS)** of at least 3 recessive bits before the next frame can begin.

## Visual Representation

```
Standard CAN Data Frame:
┌─────┬──────────────────┬────────────────┬──────────┬─────────┬─────┬─────┐
│ SOF │  Arbitration     │    Control     │   Data   │   CRC   │ ACK │ EOF │
│  1  │ ID(11) + RTR(1) │ IDE+r0+DLC(4) │  0-8B    │ 15+1    │ 2   │  7  │
└─────┴──────────────────┴────────────────┴──────────┴─────────┴─────┴─────┘

Extended CAN Data Frame:
┌─────┬────────────────────────────────────┬────────────────┬──────┬─────┬─────┬─────┐
│ SOF │          Arbitration               │    Control     │ Data │ CRC │ ACK │ EOF │
│  1  │ ID(11)+SRR+IDE+ID(18)+RTR         │ r1+r0+DLC(4)  │ 0-8B │15+1 │  2  │  7  │
└─────┴────────────────────────────────────┴────────────────┴──────┴─────┴─────┴─────┘
```

## Code Examples

### C/C++ Example - Frame Construction and Parsing

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CAN frame structure (standard format)
typedef struct {
    uint32_t id;           // 11-bit or 29-bit identifier
    uint8_t  dlc;          // Data length code (0-8)
    uint8_t  data[8];      // Data bytes
    bool     is_extended;  // Extended frame flag
    bool     is_rtr;       // Remote transmission request flag
} can_frame_t;

// Initialize a CAN frame
void can_frame_init(can_frame_t *frame, uint32_t id, bool extended) {
    memset(frame, 0, sizeof(can_frame_t));
    
    if (extended) {
        frame->id = id & 0x1FFFFFFF;  // Mask to 29 bits
        frame->is_extended = true;
    } else {
        frame->id = id & 0x7FF;        // Mask to 11 bits
        frame->is_extended = false;
    }
}

// Set data in CAN frame
bool can_frame_set_data(can_frame_t *frame, const uint8_t *data, uint8_t len) {
    if (len > 8) {
        return false;  // Invalid DLC
    }
    
    frame->dlc = len;
    memcpy(frame->data, data, len);
    return true;
}

// Example: Create and populate a standard CAN frame
void example_standard_frame(void) {
    can_frame_t frame;
    uint8_t sensor_data[4] = {0x12, 0x34, 0x56, 0x78};
    
    // Initialize frame with ID 0x123
    can_frame_init(&frame, 0x123, false);
    
    // Set data
    can_frame_set_data(&frame, sensor_data, 4);
    
    // Frame is ready to transmit
    // can_transmit(&frame);
}

// Example: Create extended CAN frame
void example_extended_frame(void) {
    can_frame_t frame;
    uint8_t engine_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 
                               0xEE, 0xFF, 0x11, 0x22};
    
    // Initialize extended frame with ID 0x12345678
    can_frame_init(&frame, 0x12345678, true);
    
    // Set 8 bytes of data
    can_frame_set_data(&frame, engine_data, 8);
}

// Parse received frame and extract information
void can_frame_parse(const can_frame_t *frame) {
    printf("CAN Frame Received:\n");
    printf("  ID: 0x%X (%s)\n", frame->id, 
           frame->is_extended ? "Extended" : "Standard");
    printf("  DLC: %d bytes\n", frame->dlc);
    printf("  RTR: %s\n", frame->is_rtr ? "Yes" : "No");
    printf("  Data: ");
    
    for (int i = 0; i < frame->dlc; i++) {
        printf("%02X ", frame->data[i]);
    }
    printf("\n");
}

// Calculate CRC15 (simplified example - real implementation more complex)
uint16_t can_calculate_crc15(const can_frame_t *frame) {
    // CRC-15-CAN polynomial: 0xC599
    uint16_t crc = 0;
    uint16_t polynomial = 0xC599;
    
    // In real implementation, CRC is calculated over:
    // SOF + Arbitration + Control + Data fields
    // This is a simplified placeholder
    
    return crc & 0x7FFF;  // 15-bit CRC
}
```

### C++ Example - Modern CAN Frame Handler

```cpp
#include <iostream>
#include <vector>
#include <cstdint>
#include <iomanip>

class CANFrame {
private:
    uint32_t identifier;
    std::vector<uint8_t> data;
    bool extended;
    bool remote;
    
public:
    // Constructor for standard frame
    CANFrame(uint32_t id, const std::vector<uint8_t>& payload) 
        : identifier(id & 0x7FF), data(payload), extended(false), remote(false) {
        if (data.size() > 8) {
            data.resize(8);  // Truncate to 8 bytes
        }
    }
    
    // Constructor for extended frame
    CANFrame(uint32_t id, const std::vector<uint8_t>& payload, bool is_extended) 
        : extended(is_extended), remote(false) {
        identifier = is_extended ? (id & 0x1FFFFFFF) : (id & 0x7FF);
        data = payload;
        if (data.size() > 8) {
            data.resize(8);
        }
    }
    
    // Getters
    uint32_t getId() const { return identifier; }
    uint8_t getDLC() const { return static_cast<uint8_t>(data.size()); }
    const std::vector<uint8_t>& getData() const { return data; }
    bool isExtended() const { return extended; }
    bool isRemote() const { return remote; }
    
    // Set as remote frame
    void setRemote(bool rtr) { 
        remote = rtr; 
        if (rtr) data.clear();  // Remote frames have no data
    }
    
    // Display frame information
    void print() const {
        std::cout << "CAN Frame:" << std::endl;
        std::cout << "  ID: 0x" << std::hex << std::setw(extended ? 8 : 3) 
                  << std::setfill('0') << identifier << std::dec
                  << " (" << (extended ? "Extended" : "Standard") << ")" << std::endl;
        std::cout << "  DLC: " << static_cast<int>(getDLC()) << std::endl;
        std::cout << "  RTR: " << (remote ? "Yes" : "No") << std::endl;
        
        if (!remote && !data.empty()) {
            std::cout << "  Data: ";
            for (uint8_t byte : data) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') 
                          << static_cast<int>(byte) << " ";
            }
            std::cout << std::dec << std::endl;
        }
    }
    
    // Serialize frame to raw bytes (simplified)
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> frame_bytes;
        
        // Add identifier bytes (simplified - real format more complex)
        if (extended) {
            frame_bytes.push_back((identifier >> 21) & 0xFF);
            frame_bytes.push_back((identifier >> 13) & 0xFF);
            frame_bytes.push_back((identifier >> 5) & 0xFF);
            frame_bytes.push_back(((identifier & 0x1F) << 3) | getDLC());
        } else {
            frame_bytes.push_back((identifier >> 3) & 0xFF);
            frame_bytes.push_back(((identifier & 0x07) << 5) | getDLC());
        }
        
        // Add data bytes
        frame_bytes.insert(frame_bytes.end(), data.begin(), data.end());
        
        return frame_bytes;
    }
};

// Example usage
int main() {
    // Standard frame example
    std::vector<uint8_t> temp_data = {0x15, 0x20, 0x00, 0x00};
    CANFrame temp_sensor(0x100, temp_data);
    temp_sensor.print();
    
    std::cout << "\n---\n\n";
    
    // Extended frame example
    std::vector<uint8_t> diagnostics = {0x01, 0x02, 0x03, 0x04, 
                                         0x05, 0x06, 0x07, 0x08};
    CANFrame diag_frame(0x18DA00F1, diagnostics, true);
    diag_frame.print();
    
    return 0;
}
```

### Rust Example - Safe CAN Frame Implementation

```rust
use std::fmt;

#[derive(Debug, Clone)]
pub struct CANFrame {
    identifier: u32,
    data: Vec<u8>,
    extended: bool,
    remote: bool,
}

#[derive(Debug)]
pub enum CANError {
    InvalidDLC,
    InvalidIdentifier,
}

impl CANFrame {
    /// Create a new standard CAN frame (11-bit ID)
    pub fn new_standard(id: u32, data: Vec<u8>) -> Result<Self, CANError> {
        if id > 0x7FF {
            return Err(CANError::InvalidIdentifier);
        }
        if data.len() > 8 {
            return Err(CANError::InvalidDLC);
        }
        
        Ok(CANFrame {
            identifier: id,
            data,
            extended: false,
            remote: false,
        })
    }
    
    /// Create a new extended CAN frame (29-bit ID)
    pub fn new_extended(id: u32, data: Vec<u8>) -> Result<Self, CANError> {
        if id > 0x1FFFFFFF {
            return Err(CANError::InvalidIdentifier);
        }
        if data.len() > 8 {
            return Err(CANError::InvalidDLC);
        }
        
        Ok(CANFrame {
            identifier: id,
            data,
            extended: true,
            remote: false,
        })
    }
    
    /// Create a remote frame (RTR)
    pub fn new_remote(id: u32, extended: bool) -> Result<Self, CANError> {
        let max_id = if extended { 0x1FFFFFFF } else { 0x7FF };
        
        if id > max_id {
            return Err(CANError::InvalidIdentifier);
        }
        
        Ok(CANFrame {
            identifier: id,
            data: Vec::new(),
            extended,
            remote: true,
        })
    }
    
    /// Get the CAN identifier
    pub fn id(&self) -> u32 {
        self.identifier
    }
    
    /// Get the Data Length Code
    pub fn dlc(&self) -> u8 {
        self.data.len() as u8
    }
    
    /// Get reference to data
    pub fn data(&self) -> &[u8] {
        &self.data
    }
    
    /// Check if frame is extended
    pub fn is_extended(&self) -> bool {
        self.extended
    }
    
    /// Check if frame is remote
    pub fn is_remote(&self) -> bool {
        self.remote
    }
    
    /// Calculate CRC-15 (simplified example)
    pub fn calculate_crc(&self) -> u16 {
        // CRC-15-CAN polynomial: 0x4599
        const CRC_POLY: u16 = 0x4599;
        let mut crc: u16 = 0;
        
        // In real implementation, calculate over entire frame
        // This is a simplified placeholder
        
        crc & 0x7FFF  // Mask to 15 bits
    }
    
    /// Serialize frame structure (simplified representation)
    pub fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        if self.extended {
            // Extended frame: 29-bit ID
            bytes.push(((self.identifier >> 21) & 0xFF) as u8);
            bytes.push(((self.identifier >> 13) & 0xFF) as u8);
            bytes.push(((self.identifier >> 5) & 0xFF) as u8);
            bytes.push((((self.identifier & 0x1F) << 3) | (self.dlc() as u32)) as u8);
        } else {
            // Standard frame: 11-bit ID
            bytes.push(((self.identifier >> 3) & 0xFF) as u8);
            bytes.push((((self.identifier & 0x07) << 5) | (self.dlc() as u32)) as u8);
        }
        
        // Add data bytes
        bytes.extend_from_slice(&self.data);
        
        bytes
    }
}

impl fmt::Display for CANFrame {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "CAN Frame:")?;
        writeln!(f, "  ID: 0x{:X} ({})", 
                 self.identifier,
                 if self.extended { "Extended" } else { "Standard" })?;
        writeln!(f, "  DLC: {} bytes", self.dlc())?;
        writeln!(f, "  RTR: {}", if self.remote { "Yes" } else { "No" })?;
        
        if !self.remote && !self.data.is_empty() {
            write!(f, "  Data: ")?;
            for byte in &self.data {
                write!(f, "{:02X} ", byte)?;
            }
            writeln!(f)?;
        }
        
        Ok(())
    }
}

// Example usage
fn main() {
    // Create standard frame
    let std_frame = CANFrame::new_standard(
        0x123,
        vec![0x11, 0x22, 0x33, 0x44]
    ).unwrap();
    
    println!("{}", std_frame);
    
    // Create extended frame
    let ext_frame = CANFrame::new_extended(
        0x12345678,
        vec![0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11]
    ).unwrap();
    
    println!("{}", ext_frame);
    
    // Create remote frame
    let remote_frame = CANFrame::new_remote(0x200, false).unwrap();
    println!("{}", remote_frame);
    
    // Demonstrate serialization
    let serialized = std_frame.serialize();
    println!("Serialized bytes: {:02X?}", serialized);
}
```

## Summary

**CAN Frame Structure** is the backbone of CAN communication, consisting of seven key components:

1. **SOF (1 bit)** - Synchronization marker
2. **Arbitration Field (12-32 bits)** - Contains identifier and determines priority through non-destructive bitwise arbitration
3. **Control Field (6 bits)** - Specifies frame format and data length
4. **Data Field (0-64 bytes)** - Actual payload
5. **CRC Field (16 bits)** - Error detection mechanism
6. **ACK Field (2 bits)** - Reception confirmation
7. **EOF (7 bits)** - Frame termination marker

The frame structure ensures **reliable**, **prioritized**, and **deterministic** communication. Lower identifier values win arbitration, enabling real-time critical messages to take precedence. The CRC and ACK mechanisms provide robust error detection and correction, making CAN suitable for safety-critical automotive and industrial applications.

Understanding frame structure is essential for proper CAN implementation, debugging bus conflicts, and optimizing network performance.