# Modbus Plus and Proprietary Extensions

## Detailed Description

### What is Modbus Plus?

Modbus Plus is a proprietary extension of the standard Modbus protocol developed by Modicon (now Schneider Electric) in the 1980s. Unlike standard Modbus RTU/ASCII (serial) or Modbus TCP (Ethernet), Modbus Plus was designed as a high-speed, peer-to-peer token-passing network operating at 1 Mbps over shielded twisted pair cabling.

**Key Characteristics:**
- **Token-passing architecture**: Devices pass a token in a logical ring, allowing deterministic network access
- **Peer-to-peer communication**: Unlike master-slave Modbus, any device can initiate communication
- **Higher speed**: 1 Mbps vs. typical 9600-19200 bps for Modbus RTU
- **More complex**: Requires special network adapters and configuration
- **Proprietary**: Not part of the open Modbus specification

### Legacy Status

Modbus Plus networks are considered legacy technology for several reasons:
- Modern Ethernet-based solutions (Modbus TCP, EtherNet/IP) offer better performance
- Limited vendor support outside Schneider Electric ecosystem
- Specialized hardware requirements
- Difficult to integrate with modern systems
- Higher maintenance costs

However, many industrial facilities still operate Modbus Plus networks installed decades ago, requiring ongoing support and integration.

### Vendor-Specific Protocol Variants

Beyond Modbus Plus, numerous vendors have created proprietary extensions to standard Modbus:

**Common Extensions:**
- **Extended function codes**: Vendor-specific functions beyond standard 0x01-0x7F range
- **Custom data formats**: Non-standard register layouts or data encoding
- **Security extensions**: Authentication, encryption (pre-dating Modbus Security)
- **Enhanced diagnostics**: Additional error codes and diagnostic information
- **Protocol encapsulation**: Modbus tunneled through other protocols

**Notable Vendor Variants:**
- **Schneider Electric**: Modbus Plus, Unity/Quantum extensions
- **Rockwell/Allen-Bradley**: ProSoft extensions for bridging
- **Siemens**: CP 341/343 specific implementations
- **ABB**: Drive profile extensions
- **Emerson**: DeltaV Modbus extensions

## Programming Modbus Plus in C/C++

### Basic Modbus Plus Structure

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Modbus Plus frame structure
#define MBRTU_MAX_PDU_SIZE 253
#define MBPLUS_TOKEN_SIZE 8
#define MBPLUS_MAX_DATA 64

// Modbus Plus packet types
typedef enum {
    MBPLUS_TOKEN_PASS = 0x00,
    MBPLUS_DATA_TRANSFER = 0x01,
    MBPLUS_PEER_COP = 0x02,
    MBPLUS_STATISTICS = 0x06
} mbplus_packet_type_t;

// Modbus Plus frame header
typedef struct __attribute__((packed)) {
    uint8_t dest_addr;
    uint8_t src_addr;
    uint8_t packet_type;
    uint8_t transaction_id;
    uint16_t data_length;
} mbplus_header_t;

// Modbus Plus frame
typedef struct {
    mbplus_header_t header;
    uint8_t data[MBPLUS_MAX_DATA];
    uint16_t crc;
} mbplus_frame_t;

// Calculate CRC for Modbus Plus (uses standard Modbus CRC)
uint16_t mbplus_crc16(const uint8_t *buffer, size_t len) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Build a Modbus Plus frame
int mbplus_build_frame(mbplus_frame_t *frame, 
                       uint8_t dest, 
                       uint8_t src,
                       mbplus_packet_type_t type,
                       const uint8_t *data,
                       size_t data_len) {
    if (!frame || data_len > MBPLUS_MAX_DATA) {
        return -1;
    }
    
    frame->header.dest_addr = dest;
    frame->header.src_addr = src;
    frame->header.packet_type = type;
    frame->header.transaction_id = 0; // Should be managed
    frame->header.data_length = data_len;
    
    if (data && data_len > 0) {
        memcpy(frame->data, data, data_len);
    }
    
    // Calculate CRC over header + data
    uint8_t *frame_bytes = (uint8_t *)frame;
    frame->crc = mbplus_crc16(frame_bytes, 
                              sizeof(mbplus_header_t) + data_len);
    
    return 0;
}

// Parse received Modbus Plus frame
int mbplus_parse_frame(const uint8_t *buffer, 
                       size_t buf_len,
                       mbplus_frame_t *frame) {
    if (!buffer || !frame || buf_len < sizeof(mbplus_header_t) + 2) {
        return -1;
    }
    
    memcpy(&frame->header, buffer, sizeof(mbplus_header_t));
    
    size_t data_len = frame->header.data_length;
    if (data_len > MBPLUS_MAX_DATA || 
        buf_len < sizeof(mbplus_header_t) + data_len + 2) {
        return -1;
    }
    
    memcpy(frame->data, buffer + sizeof(mbplus_header_t), data_len);
    memcpy(&frame->crc, buffer + sizeof(mbplus_header_t) + data_len, 2);
    
    // Verify CRC
    uint16_t calc_crc = mbplus_crc16(buffer, 
                                     sizeof(mbplus_header_t) + data_len);
    if (calc_crc != frame->crc) {
        return -2; // CRC error
    }
    
    return 0;
}
```

### Proprietary Function Code Handler

```c
// Vendor-specific function codes
#define MODBUS_FC_VENDOR_SPECIFIC_BASE 0x41
#define VENDOR_FC_READ_EXTENDED_REGS 0x41
#define VENDOR_FC_WRITE_CONFIG 0x42
#define VENDOR_FC_GET_DIAGNOSTICS 0x43

// Extended register read (vendor-specific)
typedef struct __attribute__((packed)) {
    uint8_t function_code;
    uint16_t start_addr;
    uint16_t num_registers;
    uint8_t register_bank; // Extended addressing
} vendor_read_extended_req_t;

typedef struct __attribute__((packed)) {
    uint8_t function_code;
    uint8_t byte_count;
    uint16_t registers[125]; // Variable length
} vendor_read_extended_resp_t;

// Handle vendor-specific function codes
int handle_vendor_function(uint8_t function_code,
                           const uint8_t *req_data,
                           size_t req_len,
                           uint8_t *resp_data,
                           size_t *resp_len) {
    switch (function_code) {
        case VENDOR_FC_READ_EXTENDED_REGS: {
            vendor_read_extended_req_t *req = 
                (vendor_read_extended_req_t *)req_data;
            
            uint16_t start = __builtin_bswap16(req->start_addr);
            uint16_t count = __builtin_bswap16(req->num_registers);
            uint8_t bank = req->register_bank;
            
            printf("Extended read: bank=%d, start=%d, count=%d\n",
                   bank, start, count);
            
            // Implementation would read from extended register space
            vendor_read_extended_resp_t *resp = 
                (vendor_read_extended_resp_t *)resp_data;
            resp->function_code = function_code;
            resp->byte_count = count * 2;
            
            // Fill with dummy data
            for (int i = 0; i < count; i++) {
                resp->registers[i] = __builtin_bswap16(i + start);
            }
            
            *resp_len = 2 + resp->byte_count;
            return 0;
        }
        
        case VENDOR_FC_GET_DIAGNOSTICS: {
            // Vendor-specific diagnostics format
            resp_data[0] = function_code;
            resp_data[1] = 8; // Byte count
            // Custom diagnostic data
            memset(&resp_data[2], 0xAA, 8);
            *resp_len = 10;
            return 0;
        }
        
        default:
            return -1; // Unsupported
    }
}
```

## Programming Modbus Extensions in Rust

### Modbus Plus Frame Handler

```rust
use std::io::{self, Error, ErrorKind};

// Modbus Plus packet types
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum MbPlusPacketType {
    TokenPass = 0x00,
    DataTransfer = 0x01,
    PeerCop = 0x02,
    Statistics = 0x06,
}

impl TryFrom<u8> for MbPlusPacketType {
    type Error = io::Error;
    
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x00 => Ok(MbPlusPacketType::TokenPass),
            0x01 => Ok(MbPlusPacketType::DataTransfer),
            0x02 => Ok(MbPlusPacketType::PeerCop),
            0x06 => Ok(MbPlusPacketType::Statistics),
            _ => Err(Error::new(ErrorKind::InvalidData, "Invalid packet type")),
        }
    }
}

// Modbus Plus frame header
#[derive(Debug, Clone)]
pub struct MbPlusHeader {
    pub dest_addr: u8,
    pub src_addr: u8,
    pub packet_type: MbPlusPacketType,
    pub transaction_id: u8,
    pub data_length: u16,
}

// Complete Modbus Plus frame
#[derive(Debug, Clone)]
pub struct MbPlusFrame {
    pub header: MbPlusHeader,
    pub data: Vec<u8>,
    pub crc: u16,
}

impl MbPlusFrame {
    const MAX_DATA_SIZE: usize = 64;
    
    // Create new frame
    pub fn new(
        dest: u8,
        src: u8,
        packet_type: MbPlusPacketType,
        data: Vec<u8>,
    ) -> Result<Self, io::Error> {
        if data.len() > Self::MAX_DATA_SIZE {
            return Err(Error::new(
                ErrorKind::InvalidInput,
                "Data exceeds maximum size",
            ));
        }
        
        let header = MbPlusHeader {
            dest_addr: dest,
            src_addr: src,
            packet_type,
            transaction_id: 0,
            data_length: data.len() as u16,
        };
        
        let mut frame = MbPlusFrame {
            header,
            data,
            crc: 0,
        };
        
        frame.calculate_crc();
        Ok(frame)
    }
    
    // Calculate CRC-16 (Modbus)
    fn calculate_crc(&mut self) {
        let mut bytes = Vec::new();
        bytes.push(self.header.dest_addr);
        bytes.push(self.header.src_addr);
        bytes.push(self.header.packet_type as u8);
        bytes.push(self.header.transaction_id);
        bytes.extend_from_slice(&self.header.data_length.to_le_bytes());
        bytes.extend_from_slice(&self.data);
        
        self.crc = crc16_modbus(&bytes);
    }
    
    // Serialize to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.header.dest_addr);
        bytes.push(self.header.src_addr);
        bytes.push(self.header.packet_type as u8);
        bytes.push(self.header.transaction_id);
        bytes.extend_from_slice(&self.header.data_length.to_le_bytes());
        bytes.extend_from_slice(&self.data);
        bytes.extend_from_slice(&self.crc.to_le_bytes());
        bytes
    }
    
    // Parse from bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, io::Error> {
        if bytes.len() < 8 {
            return Err(Error::new(ErrorKind::InvalidData, "Frame too short"));
        }
        
        let packet_type = MbPlusPacketType::try_from(bytes[2])?;
        let data_length = u16::from_le_bytes([bytes[4], bytes[5]]) as usize;
        
        if bytes.len() < 6 + data_length + 2 {
            return Err(Error::new(ErrorKind::InvalidData, "Incomplete frame"));
        }
        
        let header = MbPlusHeader {
            dest_addr: bytes[0],
            src_addr: bytes[1],
            packet_type,
            transaction_id: bytes[3],
            data_length: data_length as u16,
        };
        
        let data = bytes[6..6 + data_length].to_vec();
        let crc = u16::from_le_bytes([bytes[6 + data_length], bytes[6 + data_length + 1]]);
        
        let mut frame = MbPlusFrame { header, data, crc };
        
        // Verify CRC
        let calculated_crc = {
            let saved_crc = frame.crc;
            frame.calculate_crc();
            let calc = frame.crc;
            frame.crc = saved_crc;
            calc
        };
        
        if calculated_crc != frame.crc {
            return Err(Error::new(ErrorKind::InvalidData, "CRC mismatch"));
        }
        
        Ok(frame)
    }
}

// CRC-16 calculation
fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}
```

### Vendor-Specific Extensions Handler

```rust
use std::collections::HashMap;

// Vendor-specific function codes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum VendorFunctionCode {
    ReadExtendedRegs = 0x41,
    WriteConfig = 0x42,
    GetDiagnostics = 0x43,
    SetParameter = 0x44,
}

impl TryFrom<u8> for VendorFunctionCode {
    type Error = io::Error;
    
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x41 => Ok(VendorFunctionCode::ReadExtendedRegs),
            0x42 => Ok(VendorFunctionCode::WriteConfig),
            0x43 => Ok(VendorFunctionCode::GetDiagnostics),
            0x44 => Ok(VendorFunctionCode::SetParameter),
            _ => Err(Error::new(
                ErrorKind::InvalidData,
                format!("Unknown vendor function code: 0x{:02X}", value),
            )),
        }
    }
}

// Vendor extension handler
pub struct VendorExtensionHandler {
    // Simulated extended register banks
    register_banks: HashMap<u8, Vec<u16>>,
    // Configuration storage
    config: HashMap<String, Vec<u8>>,
}

impl VendorExtensionHandler {
    pub fn new() -> Self {
        let mut handler = VendorExtensionHandler {
            register_banks: HashMap::new(),
            config: HashMap::new(),
        };
        
        // Initialize some register banks
        for bank in 0..4 {
            handler.register_banks.insert(bank, vec![0; 1000]);
        }
        
        handler
    }
    
    // Handle vendor-specific requests
    pub fn handle_request(
        &mut self,
        function_code: u8,
        request_data: &[u8],
    ) -> Result<Vec<u8>, io::Error> {
        let fc = VendorFunctionCode::try_from(function_code)?;
        
        match fc {
            VendorFunctionCode::ReadExtendedRegs => {
                self.read_extended_registers(request_data)
            }
            VendorFunctionCode::WriteConfig => {
                self.write_configuration(request_data)
            }
            VendorFunctionCode::GetDiagnostics => {
                self.get_diagnostics(request_data)
            }
            VendorFunctionCode::SetParameter => {
                self.set_parameter(request_data)
            }
        }
    }
    
    fn read_extended_registers(&self, request: &[u8]) -> Result<Vec<u8>, io::Error> {
        if request.len() < 5 {
            return Err(Error::new(ErrorKind::InvalidInput, "Request too short"));
        }
        
        let start_addr = u16::from_be_bytes([request[0], request[1]]);
        let count = u16::from_be_bytes([request[2], request[3]]);
        let bank = request[4];
        
        let registers = self.register_banks.get(&bank).ok_or_else(|| {
            Error::new(ErrorKind::NotFound, format!("Bank {} not found", bank))
        })?;
        
        if start_addr as usize + count as usize > registers.len() {
            return Err(Error::new(ErrorKind::InvalidInput, "Address out of range"));
        }
        
        let mut response = Vec::new();
        response.push(VendorFunctionCode::ReadExtendedRegs as u8);
        response.push((count * 2) as u8); // Byte count
        
        for i in start_addr..start_addr + count {
            response.extend_from_slice(&registers[i as usize].to_be_bytes());
        }
        
        Ok(response)
    }
    
    fn write_configuration(&mut self, request: &[u8]) -> Result<Vec<u8>, io::Error> {
        if request.len() < 2 {
            return Err(Error::new(ErrorKind::InvalidInput, "Request too short"));
        }
        
        let key_len = request[0] as usize;
        if request.len() < 2 + key_len {
            return Err(Error::new(ErrorKind::InvalidInput, "Invalid key length"));
        }
        
        let key = String::from_utf8_lossy(&request[1..1 + key_len]).to_string();
        let value = request[1 + key_len..].to_vec();
        
        self.config.insert(key.clone(), value);
        
        let mut response = Vec::new();
        response.push(VendorFunctionCode::WriteConfig as u8);
        response.push(0x01); // Success
        
        Ok(response)
    }
    
    fn get_diagnostics(&self, _request: &[u8]) -> Result<Vec<u8>, io::Error> {
        let mut response = Vec::new();
        response.push(VendorFunctionCode::GetDiagnostics as u8);
        
        // Simulated diagnostic data
        response.push(16); // Byte count
        response.extend_from_slice(&[
            0x00, 0x01, // Uptime hours
            0x00, 0x05, // Error count
            0x00, 0x64, // Temperature
            0x00, 0x32, // CPU load %
            0x00, 0x0A, // Network utilization %
            0x00, 0x00, // Reserved
            0x00, 0x00, // Reserved
            0x00, 0x00, // Reserved
        ]);
        
        Ok(response)
    }
    
    fn set_parameter(&mut self, request: &[u8]) -> Result<Vec<u8>, io::Error> {
        if request.len() < 4 {
            return Err(Error::new(ErrorKind::InvalidInput, "Request too short"));
        }
        
        let param_id = u16::from_be_bytes([request[0], request[1]]);
        let param_value = u16::from_be_bytes([request[2], request[3]]);
        
        println!("Setting parameter {} to value {}", param_id, param_value);
        
        let mut response = Vec::new();
        response.push(VendorFunctionCode::SetParameter as u8);
        response.extend_from_slice(&param_id.to_be_bytes());
        response.extend_from_slice(&param_value.to_be_bytes());
        
        Ok(response)
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut handler = VendorExtensionHandler::new();
    
    // Example: Read extended registers
    let request = vec![
        0x00, 0x00, // Start address: 0
        0x00, 0x0A, // Count: 10 registers
        0x01,       // Bank: 1
    ];
    
    let response = handler.handle_request(
        VendorFunctionCode::ReadExtendedRegs as u8,
        &request,
    )?;
    
    println!("Response: {:02X?}", response);
    
    // Example: Create and parse Modbus Plus frame
    let data = vec![0x03, 0x00, 0x00, 0x00, 0x0A]; // Standard Modbus read
    let frame = MbPlusFrame::new(
        1,  // Destination
        0,  // Source
        MbPlusPacketType::DataTransfer,
        data,
    )?;
    
    let bytes = frame.to_bytes();
    println!("Frame bytes: {:02X?}", bytes);
    
    let parsed = MbPlusFrame::from_bytes(&bytes)?;
    println!("Parsed frame: {:?}", parsed);
    
    Ok(())
}
```

## Summary

**Modbus Plus and proprietary extensions** represent the non-standardized aspects of the Modbus ecosystem:

**Key Takeaways:**

1. **Modbus Plus** is a legacy high-speed token-passing network (1 Mbps) that predates modern Ethernet solutions but remains in use in many older industrial installations

2. **Architectural Differences**: Unlike standard master-slave Modbus, Modbus Plus supports peer-to-peer communication with deterministic token-passing access control

3. **Vendor Extensions** are common across the industry, including custom function codes, extended addressing, proprietary data formats, and enhanced diagnostics

4. **Integration Challenges**: Working with these systems requires specialized knowledge, vendor-specific documentation, and often custom gateway/bridge hardware

5. **Modern Approach**: When maintaining legacy systems, consider using protocol converters or gateways to bridge Modbus Plus/proprietary variants to standard Modbus TCP for easier integration with modern SCADA and control systems

6. **Documentation is Critical**: Proprietary extensions often lack public documentation, making vendor support and reverse-engineering common practices in maintenance scenarios

These extensions highlight the balance between standardization and vendor innovation in industrial protocols, though the industry trend favors open, standardized solutions over proprietary implementations.