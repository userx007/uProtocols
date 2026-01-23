# Profibus Protocol Overview

## Detailed Description

**Profibus** (Process Field Bus) is a standardized, open digital communications protocol widely used in industrial automation and process control. Developed in the 1980s in Germany and standardized as IEC 61158/61784, Profibus enables communication between automation systems, field devices, and control systems in manufacturing and process industries.

### Historical Context

Profibus emerged from a German government-sponsored initiative in 1987 to create a vendor-independent fieldbus standard. It quickly gained adoption across Europe and globally, becoming one of the dominant industrial communication protocols alongside Modbus, DeviceNet, and later, industrial Ethernet variants.

### Profibus Variants

**1. Profibus DP (Decentralized Peripherals)**
- Most common variant, used in ~90% of Profibus installations
- High-speed communication (9.6 kbps to 12 Mbps)
- Optimized for time-critical automation tasks
- Connects PLCs to distributed I/O, drives, and sensors
- Cycle times as low as 1-2 milliseconds

**2. Profibus PA (Process Automation)**
- Designed for process industries (chemical, pharmaceutical, oil & gas)
- Operates on MBP (Manchester Bus Powered) physical layer
- Intrinsically safe for hazardous environments
- Supports power and data on same two-wire cable (31.25 kbps)
- Enables field device power supply through the bus

**3. Profibus FMS (Fieldbus Message Specification)**
- Application layer for complex peer-to-peer communication
- Used for communication between PLCs and engineering workstations
- Largely obsolete, replaced by industrial Ethernet solutions

### Architecture

Profibus uses a **master-slave architecture** with token-passing between masters:

- **Class 1 Masters (DPM1)**: Central controllers (PLCs, PCs) that control bus access
- **Class 2 Masters (DPM2)**: Engineering/diagnostic tools with temporary bus access
- **Slaves**: Field devices (sensors, actuators, drives) that respond to master requests

The protocol supports up to 126 devices on a single bus segment, with network topology supporting line, tree, and star configurations using RS-485 physical layer (DP) or MBP (PA).

## Code Examples

### C/C++ Example: Basic Profibus DP Master Communication

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Profibus DP frame structure
typedef struct {
    uint8_t start_delimiter;
    uint8_t destination_address;
    uint8_t source_address;
    uint8_t function_code;
    uint8_t data_length;
    uint8_t data[246];  // Max DP payload
    uint8_t frame_check_sequence;
    uint8_t end_delimiter;
} profibus_dp_frame_t;

// Function codes
#define FDL_DATA_EXCHANGE 0x08
#define FDL_診斷_REQUEST 0x05

// Frame delimiters
#define SD2 0x68  // Variable length frame start
#define ED 0x16   // End delimiter

// Calculate FCS (Frame Check Sequence)
uint8_t calculate_fcs(uint8_t *data, size_t length) {
    uint8_t fcs = 0;
    for (size_t i = 0; i < length; i++) {
        fcs += data[i];
    }
    return fcs;
}

// Build a Profibus DP request frame
int build_dp_frame(profibus_dp_frame_t *frame, 
                   uint8_t dest_addr, 
                   uint8_t src_addr,
                   uint8_t *payload, 
                   uint8_t payload_len) {
    
    if (payload_len > 246) return -1;
    
    frame->start_delimiter = SD2;
    frame->destination_address = dest_addr;
    frame->source_address = src_addr;
    frame->function_code = FDL_DATA_EXCHANGE;
    frame->data_length = payload_len;
    
    memcpy(frame->data, payload, payload_len);
    
    // Calculate FCS over DA, SA, FC, and data
    uint8_t fcs_data[3 + payload_len];
    fcs_data[0] = dest_addr;
    fcs_data[1] = src_addr;
    fcs_data[2] = FDL_DATA_EXCHANGE;
    memcpy(&fcs_data[3], payload, payload_len);
    
    frame->frame_check_sequence = calculate_fcs(fcs_data, 3 + payload_len);
    frame->end_delimiter = ED;
    
    return 0;
}

// Example: Read input data from slave
void read_slave_inputs(uint8_t slave_address) {
    profibus_dp_frame_t frame;
    uint8_t read_command[] = {0x00};  // Read inputs command
    
    if (build_dp_frame(&frame, slave_address, 0x02, read_command, 1) == 0) {
        printf("Sending read request to slave %d\n", slave_address);
        // In real implementation, send frame via serial port/hardware interface
        // send_frame(&frame);
    }
}

int main() {
    printf("Profibus DP Master Example\n");
    
    // Read inputs from slave at address 5
    read_slave_inputs(5);
    
    // In production code:
    // 1. Initialize serial port (RS-485)
    // 2. Configure baud rate (typically 1.5 Mbps)
    // 3. Implement token passing for multi-master
    // 4. Handle cyclic data exchange
    // 5. Process diagnostics and alarms
    
    return 0;
}
```

### C++ Example: Profibus DP Slave Device Simulation

```cpp
#include <iostream>
#include <vector>
#include <cstdint>
#include <array>

class ProfibusDP_Slave {
private:
    uint8_t station_address;
    std::vector<uint8_t> input_data;
    std::vector<uint8_t> output_data;
    bool configured;
    
    // GSD (General Station Description) parameters
    struct DeviceInfo {
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t max_input_length;
        uint8_t max_output_length;
    } device_info;

public:
    ProfibusDP_Slave(uint8_t address, uint16_t vendor_id, uint16_t device_id) 
        : station_address(address), configured(false) {
        device_info.vendor_id = vendor_id;
        device_info.device_id = device_id;
        device_info.max_input_length = 32;
        device_info.max_output_length = 32;
        
        input_data.resize(32, 0);
        output_data.resize(32, 0);
    }
    
    // Handle configuration telegram
    bool handleConfiguration(const std::vector<uint8_t>& config_data) {
        if (config_data.size() < 3) return false;
        
        uint8_t input_len = config_data[0];
        uint8_t output_len = config_data[1];
        
        if (input_len <= device_info.max_input_length && 
            output_len <= device_info.max_output_length) {
            input_data.resize(input_len);
            output_data.resize(output_len);
            configured = true;
            std::cout << "Slave " << (int)station_address << " configured: "
                      << (int)input_len << " inputs, " 
                      << (int)output_len << " outputs\n";
            return true;
        }
        return false;
    }
    
    // Cyclic data exchange
    std::vector<uint8_t> processDataExchange(const std::vector<uint8_t>& output_from_master) {
        if (!configured) {
            return {};
        }
        
        // Update outputs from master
        if (output_from_master.size() == output_data.size()) {
            output_data = output_from_master;
        }
        
        // Simulate sensor readings
        updateInputs();
        
        return input_data;
    }
    
    // Diagnostic information
    std::array<uint8_t, 6> getDiagnostics() const {
        std::array<uint8_t, 6> diag = {0};
        diag[0] = station_address;
        diag[1] = configured ? 0x00 : 0x01;  // Status: OK or Not Configured
        diag[2] = device_info.vendor_id >> 8;
        diag[3] = device_info.vendor_id & 0xFF;
        diag[4] = device_info.device_id >> 8;
        diag[5] = device_info.device_id & 0xFF;
        return diag;
    }

private:
    void updateInputs() {
        // Simulate sensor data (e.g., temperature, pressure)
        for (size_t i = 0; i < input_data.size(); i++) {
            input_data[i] = (input_data[i] + 1) % 256;
        }
    }
};

int main() {
    // Create a Profibus DP slave device
    ProfibusDP_Slave slave(5, 0x04F4, 0x0001);  // Siemens vendor ID example
    
    // Configuration phase
    std::vector<uint8_t> config = {8, 4, 0x00};  // 8 input bytes, 4 output bytes
    slave.handleConfiguration(config);
    
    // Cyclic data exchange simulation
    std::vector<uint8_t> master_outputs = {0xAA, 0xBB, 0xCC, 0xDD};
    
    for (int cycle = 0; cycle < 5; cycle++) {
        std::cout << "\nCycle " << cycle << ":\n";
        auto inputs = slave.processDataExchange(master_outputs);
        
        std::cout << "Inputs from slave: ";
        for (auto byte : inputs) {
            printf("%02X ", byte);
        }
        std::cout << "\n";
    }
    
    // Get diagnostics
    auto diag = slave.getDiagnostics();
    std::cout << "\nDiagnostics: ";
    for (auto byte : diag) {
        printf("%02X ", byte);
    }
    std::cout << "\n";
    
    return 0;
}
```

### Rust Example: Profibus Frame Parser

```rust
use std::fmt;

// Profibus frame delimiters
const SD1: u8 = 0x10; // Fixed length frame without data
const SD2: u8 = 0x68; // Variable length frame
const SD3: u8 = 0xA2; // Fixed length frame with data
const SD4: u8 = 0xDC; // Token frame
const ED: u8 = 0x16;  // End delimiter

#[derive(Debug, Clone)]
pub enum ProfibusFrame {
    DataExchange {
        destination: u8,
        source: u8,
        function_code: u8,
        data: Vec<u8>,
    },
    Token {
        destination: u8,
        source: u8,
    },
    Acknowledgment {
        destination: u8,
        source: u8,
    },
}

#[derive(Debug)]
pub enum ParseError {
    InvalidDelimiter,
    InvalidLength,
    ChecksumMismatch,
    InsufficientData,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ParseError::InvalidDelimiter => write!(f, "Invalid frame delimiter"),
            ParseError::InvalidLength => write!(f, "Invalid frame length"),
            ParseError::ChecksumMismatch => write!(f, "Checksum verification failed"),
            ParseError::InsufficientData => write!(f, "Insufficient data in buffer"),
        }
    }
}

pub struct ProfibusParser;

impl ProfibusParser {
    /// Calculate Frame Check Sequence
    fn calculate_fcs(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x))
    }
    
    /// Parse a Profibus frame from raw bytes
    pub fn parse_frame(buffer: &[u8]) -> Result<ProfibusFrame, ParseError> {
        if buffer.is_empty() {
            return Err(ParseError::InsufficientData);
        }
        
        match buffer[0] {
            SD2 => Self::parse_variable_frame(buffer),
            SD4 => Self::parse_token_frame(buffer),
            SD1 => Self::parse_fixed_frame(buffer),
            _ => Err(ParseError::InvalidDelimiter),
        }
    }
    
    /// Parse variable length frame (SD2)
    fn parse_variable_frame(buffer: &[u8]) -> Result<ProfibusFrame, ParseError> {
        if buffer.len() < 9 {
            return Err(ParseError::InsufficientData);
        }
        
        let length = buffer[1] as usize;
        let repeat_length = buffer[2] as usize;
        
        if length != repeat_length {
            return Err(ParseError::InvalidLength);
        }
        
        if buffer.len() < 6 + length {
            return Err(ParseError::InsufficientData);
        }
        
        let destination = buffer[4];
        let source = buffer[5];
        let function_code = buffer[6];
        
        let data_start = 7;
        let data_end = 7 + length - 3; // Subtract DA, SA, FC
        let data = buffer[data_start..data_end].to_vec();
        
        // Verify FCS
        let fcs_data = &buffer[4..data_end];
        let calculated_fcs = Self::calculate_fcs(fcs_data);
        let received_fcs = buffer[data_end];
        
        if calculated_fcs != received_fcs {
            return Err(ParseError::ChecksumMismatch);
        }
        
        Ok(ProfibusFrame::DataExchange {
            destination,
            source,
            function_code,
            data,
        })
    }
    
    /// Parse token frame (SD4)
    fn parse_token_frame(buffer: &[u8]) -> Result<ProfibusFrame, ParseError> {
        if buffer.len() < 4 {
            return Err(ParseError::InsufficientData);
        }
        
        Ok(ProfibusFrame::Token {
            destination: buffer[1],
            source: buffer[2],
        })
    }
    
    /// Parse fixed frame without data (SD1)
    fn parse_fixed_frame(buffer: &[u8]) -> Result<ProfibusFrame, ParseError> {
        if buffer.len() < 6 {
            return Err(ParseError::InsufficientData);
        }
        
        Ok(ProfibusFrame::Acknowledgment {
            destination: buffer[1],
            source: buffer[2],
        })
    }
    
    /// Encode a frame to bytes
    pub fn encode_frame(frame: &ProfibusFrame) -> Vec<u8> {
        match frame {
            ProfibusFrame::DataExchange { destination, source, function_code, data } => {
                let length = (3 + data.len()) as u8;
                let mut buffer = vec![
                    SD2, length, length, SD2,
                    *destination, *source, *function_code
                ];
                buffer.extend_from_slice(data);
                
                // Calculate and append FCS
                let fcs = Self::calculate_fcs(&buffer[4..]);
                buffer.push(fcs);
                buffer.push(ED);
                buffer
            }
            ProfibusFrame::Token { destination, source } => {
                vec![SD4, *destination, *source, ED]
            }
            ProfibusFrame::Acknowledgment { destination, source } => {
                let fcs = Self::calculate_fcs(&[*destination, *source]);
                vec![SD1, *destination, *source, fcs, ED]
            }
        }
    }
}

fn main() {
    println!("Profibus Frame Parser Example\n");
    
    // Example: Create and encode a data exchange frame
    let frame = ProfibusFrame::DataExchange {
        destination: 5,
        source: 2,
        function_code: 0x08,
        data: vec![0x01, 0x02, 0x03, 0x04],
    };
    
    let encoded = ProfibusParser::encode_frame(&frame);
    println!("Encoded frame: {:02X?}", encoded);
    
    // Parse it back
    match ProfibusParser::parse_frame(&encoded) {
        Ok(parsed_frame) => {
            println!("\nParsed frame: {:?}", parsed_frame);
        }
        Err(e) => {
            println!("Parse error: {}", e);
        }
    }
    
    // Example: Token passing
    let token = ProfibusFrame::Token {
        destination: 3,
        source: 2,
    };
    let token_encoded = ProfibusParser::encode_frame(&token);
    println!("\nToken frame: {:02X?}", token_encoded);
}
```

## Summary

**Profibus** is a mature, widely-deployed industrial fieldbus protocol that revolutionized factory automation by providing deterministic, real-time communication between controllers and field devices. The three main variants serve different purposes: **DP** for fast factory automation, **PA** for process industries with intrinsic safety requirements, and **FMS** for complex messaging (now largely obsolete).

Key characteristics include master-slave architecture with token passing, support for up to 126 devices per segment, speeds from 9.6 kbps to 12 Mbps, and standardized device profiles (GSD files) for interoperability. While newer protocols like Profinet and EtherCAT are gaining ground, Profibus remains prevalent in legacy installations and industries requiring proven, reliable communication.

The code examples demonstrate fundamental operations: frame construction and FCS calculation in C, object-oriented slave device simulation in C++, and safe frame parsing with Rust's type system. Production implementations would require hardware interfaces (RS-485 transceivers), real-time operating systems, and comprehensive error handling for industrial reliability.