# Function Code Field in Profibus

## Detailed Description

The **Function Code (FC)** field is a critical single-byte component in Profibus protocol frames that determines the type of service, communication direction, and frame control characteristics. Located in the telegram header, the FC byte encodes multiple pieces of information using bit flags that control how devices interpret and respond to messages.

### Structure of the FC Byte

The Function Code byte is organized into several bit fields:

```
Bit 7   6   5   4   3   2   1   0
    |   |   |   |   |   |   |   |
    |   |   |   +---+---+---+---+---- Function Code (FC)
    |   +---+------------------------ Frame Count Bit (FCB) / Frame Count Valid (FCV)
    +-------------------------------- Request/Response
```

**Key Components:**

1. **Request/Response Bit (Bit 6-7)**: Indicates if the frame is a request from master or response from slave
2. **Frame Count Bit (FCB, Bit 5)**: Toggles between 0 and 1 for consecutive frames to detect duplicates
3. **Frame Count Valid (FCV, Bit 4)**: Indicates whether FCB should be evaluated
4. **Function Code (Bits 0-3)**: Defines the specific service type

### Common Function Codes

- **0x00-0x03**: Slave-to-slave communication
- **0x04**: SDN (Send Data with No Acknowledge)
- **0x05**: SDR (Send Data with Reply)
- **0x06**: SDA (Send Data with Acknowledge)
- **0x09**: Request FDL Status
- **0x0C**: SRD (Send and Request Data)
- **0x0D**: Request Ident
- **0x0E**: Request LSAP Status

## C/C++ Code Examples

```c
// Profibus Function Code definitions and parsing

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Function Code bit masks
#define FC_MASK           0x0F  // Bits 0-3
#define FCV_MASK          0x10  // Bit 4
#define FCB_MASK          0x20  // Bit 5
#define REQUEST_MASK      0x40  // Bit 6

// Common Function Codes
#define FC_SDN            0x04  // Send Data with No acknowledge
#define FC_SDR            0x05  // Send Data with Reply
#define FC_SDA            0x06  // Send Data with Acknowledge
#define FC_REQ_FDL_STATUS 0x09  // Request FDL Status
#define FC_SRD            0x0C  // Send and Request Data
#define FC_REQ_IDENT      0x0D  // Request Identification
#define FC_REQ_LSAP       0x0E  // Request LSAP Status

// Structure to hold decoded FC information
typedef struct {
    uint8_t function_code;
    bool frame_count_valid;
    bool frame_count_bit;
    bool is_request;
} FunctionCodeInfo;

// Decode Function Code byte
FunctionCodeInfo decode_function_code(uint8_t fc_byte) {
    FunctionCodeInfo info;
    
    info.function_code = fc_byte & FC_MASK;
    info.frame_count_valid = (fc_byte & FCV_MASK) != 0;
    info.frame_count_bit = (fc_byte & FCB_MASK) != 0;
    info.is_request = (fc_byte & REQUEST_MASK) != 0;
    
    return info;
}

// Create Function Code byte
uint8_t create_function_code(uint8_t fc, bool fcv, bool fcb, bool request) {
    uint8_t fc_byte = 0;
    
    fc_byte |= (fc & FC_MASK);
    if (fcv) fc_byte |= FCV_MASK;
    if (fcb) fc_byte |= FCB_MASK;
    if (request) fc_byte |= REQUEST_MASK;
    
    return fc_byte;
}

// Get human-readable function name
const char* get_function_name(uint8_t fc) {
    switch(fc & FC_MASK) {
        case FC_SDN: return "Send Data No Ack";
        case FC_SDR: return "Send Data Reply";
        case FC_SDA: return "Send Data Ack";
        case FC_REQ_FDL_STATUS: return "Request FDL Status";
        case FC_SRD: return "Send Request Data";
        case FC_REQ_IDENT: return "Request Ident";
        case FC_REQ_LSAP: return "Request LSAP";
        default: return "Unknown";
    }
}

// Example usage
int main() {
    // Create a Function Code byte for SDA request with FCB=1, FCV=1
    uint8_t fc_byte = create_function_code(FC_SDA, true, true, true);
    printf("Created FC byte: 0x%02X\n", fc_byte);
    
    // Decode the Function Code
    FunctionCodeInfo info = decode_function_code(fc_byte);
    
    printf("Decoded Information:\n");
    printf("  Function: %s (0x%02X)\n", get_function_name(info.function_code), 
           info.function_code);
    printf("  Frame Count Valid: %s\n", info.frame_count_valid ? "Yes" : "No");
    printf("  Frame Count Bit: %d\n", info.frame_count_bit);
    printf("  Request Type: %s\n", info.is_request ? "Request" : "Response");
    
    return 0;
}
```

### C++ Class-Based Implementation

```cpp
#include <iostream>
#include <string>
#include <cstdint>

class ProfibusFC {
private:
    uint8_t fc_byte_;
    
    static constexpr uint8_t FC_MASK = 0x0F;
    static constexpr uint8_t FCV_MASK = 0x10;
    static constexpr uint8_t FCB_MASK = 0x20;
    static constexpr uint8_t REQUEST_MASK = 0x40;
    
public:
    enum FunctionCode : uint8_t {
        SDN = 0x04,
        SDR = 0x05,
        SDA = 0x06,
        REQ_FDL_STATUS = 0x09,
        SRD = 0x0C,
        REQ_IDENT = 0x0D,
        REQ_LSAP = 0x0E
    };
    
    ProfibusFC(uint8_t fc_byte) : fc_byte_(fc_byte) {}
    
    ProfibusFC(FunctionCode fc, bool fcv, bool fcb, bool request) {
        fc_byte_ = static_cast<uint8_t>(fc) & FC_MASK;
        if (fcv) fc_byte_ |= FCV_MASK;
        if (fcb) fc_byte_ |= FCB_MASK;
        if (request) fc_byte_ |= REQUEST_MASK;
    }
    
    uint8_t get_function_code() const { return fc_byte_ & FC_MASK; }
    bool is_fcv_set() const { return (fc_byte_ & FCV_MASK) != 0; }
    bool is_fcb_set() const { return (fc_byte_ & FCB_MASK) != 0; }
    bool is_request() const { return (fc_byte_ & REQUEST_MASK) != 0; }
    uint8_t get_raw() const { return fc_byte_; }
    
    std::string to_string() const {
        std::string result = "FC: ";
        switch(get_function_code()) {
            case SDN: result += "SDN"; break;
            case SDR: result += "SDR"; break;
            case SDA: result += "SDA"; break;
            case REQ_FDL_STATUS: result += "REQ_FDL_STATUS"; break;
            case SRD: result += "SRD"; break;
            case REQ_IDENT: result += "REQ_IDENT"; break;
            case REQ_LSAP: result += "REQ_LSAP"; break;
            default: result += "UNKNOWN"; break;
        }
        result += " | FCV:" + std::to_string(is_fcv_set());
        result += " | FCB:" + std::to_string(is_fcb_set());
        result += " | " + std::string(is_request() ? "REQ" : "RSP");
        return result;
    }
};

int main() {
    // Create FC for SDA request
    ProfibusFC fc1(ProfibusFC::SDA, true, true, true);
    std::cout << "FC1: " << fc1.to_string() << " (0x" 
              << std::hex << static_cast<int>(fc1.get_raw()) << ")\n";
    
    // Parse existing FC byte
    ProfibusFC fc2(0x76);
    std::cout << "FC2: " << fc2.to_string() << " (0x" 
              << std::hex << static_cast<int>(fc2.get_raw()) << ")\n";
    
    return 0;
}
```

## Rust Code Examples

```rust
// Profibus Function Code implementation in Rust

use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum FunctionCode {
    SDN = 0x04,           // Send Data No Acknowledge
    SDR = 0x05,           // Send Data Reply
    SDA = 0x06,           // Send Data Acknowledge
    ReqFdlStatus = 0x09,  // Request FDL Status
    SRD = 0x0C,           // Send and Request Data
    ReqIdent = 0x0D,      // Request Identification
    ReqLsap = 0x0E,       // Request LSAP Status
}

impl FunctionCode {
    pub fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x04 => Some(FunctionCode::SDN),
            0x05 => Some(FunctionCode::SDR),
            0x06 => Some(FunctionCode::SDA),
            0x09 => Some(FunctionCode::ReqFdlStatus),
            0x0C => Some(FunctionCode::SRD),
            0x0D => Some(FunctionCode::ReqIdent),
            0x0E => Some(FunctionCode::ReqLsap),
            _ => None,
        }
    }
    
    pub fn name(&self) -> &'static str {
        match self {
            FunctionCode::SDN => "Send Data No Ack",
            FunctionCode::SDR => "Send Data Reply",
            FunctionCode::SDA => "Send Data Acknowledge",
            FunctionCode::ReqFdlStatus => "Request FDL Status",
            FunctionCode::SRD => "Send Request Data",
            FunctionCode::ReqIdent => "Request Ident",
            FunctionCode::ReqLsap => "Request LSAP",
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct ProfibusFC {
    fc_byte: u8,
}

impl ProfibusFC {
    const FC_MASK: u8 = 0x0F;
    const FCV_MASK: u8 = 0x10;
    const FCB_MASK: u8 = 0x20;
    const REQUEST_MASK: u8 = 0x40;
    
    /// Create from raw byte
    pub fn from_byte(fc_byte: u8) -> Self {
        Self { fc_byte }
    }
    
    /// Create from components
    pub fn new(fc: FunctionCode, fcv: bool, fcb: bool, is_request: bool) -> Self {
        let mut fc_byte = (fc as u8) & Self::FC_MASK;
        
        if fcv {
            fc_byte |= Self::FCV_MASK;
        }
        if fcb {
            fc_byte |= Self::FCB_MASK;
        }
        if is_request {
            fc_byte |= Self::REQUEST_MASK;
        }
        
        Self { fc_byte }
    }
    
    /// Get the function code
    pub fn function_code(&self) -> Option<FunctionCode> {
        FunctionCode::from_u8(self.fc_byte & Self::FC_MASK)
    }
    
    /// Check if Frame Count Valid bit is set
    pub fn is_fcv_set(&self) -> bool {
        (self.fc_byte & Self::FCV_MASK) != 0
    }
    
    /// Check if Frame Count Bit is set
    pub fn is_fcb_set(&self) -> bool {
        (self.fc_byte & Self::FCB_MASK) != 0
    }
    
    /// Check if this is a request (vs response)
    pub fn is_request(&self) -> bool {
        (self.fc_byte & Self::REQUEST_MASK) != 0
    }
    
    /// Get raw byte value
    pub fn as_byte(&self) -> u8 {
        self.fc_byte
    }
    
    /// Toggle the FCB bit (for next transmission)
    pub fn toggle_fcb(&mut self) {
        self.fc_byte ^= Self::FCB_MASK;
    }
}

impl fmt::Display for ProfibusFC {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let fc_name = self.function_code()
            .map(|fc| fc.name())
            .unwrap_or("Unknown");
        
        write!(
            f,
            "FC: {} (0x{:02X}) | FCV:{} | FCB:{} | {}",
            fc_name,
            self.fc_byte & Self::FC_MASK,
            if self.is_fcv_set() { "1" } else { "0" },
            if self.is_fcb_set() { "1" } else { "0" },
            if self.is_request() { "REQ" } else { "RSP" }
        )
    }
}

fn main() {
    // Create FC for SDA request with FCB and FCV set
    let fc1 = ProfibusFC::new(FunctionCode::SDA, true, true, true);
    println!("FC1: {} [Raw: 0x{:02X}]", fc1, fc1.as_byte());
    
    // Parse existing FC byte
    let fc2 = ProfibusFC::from_byte(0x76);
    println!("FC2: {} [Raw: 0x{:02X}]", fc2, fc2.as_byte());
    
    // Demonstrate FCB toggling
    let mut fc3 = ProfibusFC::new(FunctionCode::SRD, true, false, true);
    println!("\nBefore toggle: {}", fc3);
    fc3.toggle_fcb();
    println!("After toggle:  {}", fc3);
    
    // Decode various FC bytes
    println!("\n--- Decoding Examples ---");
    let test_bytes = [0x46, 0x76, 0x5C, 0x49];
    for &byte in &test_bytes {
        let fc = ProfibusFC::from_byte(byte);
        println!("0x{:02X} -> {}", byte, fc);
    }
}
```

## Summary

The **Function Code (FC)** field is a compact yet information-dense single-byte component in Profibus telegrams that orchestrates communication behavior between masters and slaves. It encodes the service type (like data transfer with/without acknowledgment, status requests), frame sequencing information (FCB/FCV bits for duplicate detection), and communication direction (request vs. response).

Understanding the FC byte is essential for Profibus protocol implementation because it:
- **Controls service types**: Determines whether data requires acknowledgment, reply, or status information
- **Ensures reliability**: The FCB/FCV mechanism detects duplicate frames in unreliable transmission environments
- **Defines communication roles**: Distinguishes master requests from slave responses
- **Enables protocol analysis**: Critical for debugging and monitoring Profibus networks

The code examples demonstrate how to encode, decode, and manipulate FC bytes in both system-level (C) and modern type-safe (C++/Rust) programming approaches, providing a foundation for building Profibus protocol stacks or diagnostic tools.