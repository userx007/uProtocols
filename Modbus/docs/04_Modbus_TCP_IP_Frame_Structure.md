# Modbus TCP/IP Frame Structure

## Overview

Modbus TCP/IP is the Ethernet implementation of the Modbus protocol, which encapsulates the traditional Modbus RTU/ASCII application data unit (ADU) within TCP/IP packets. The key difference from serial Modbus is the addition of the **MBAP header** (Modbus Application Protocol header), which replaces the address and CRC fields used in serial variants.

## MBAP Header Structure

The MBAP header is **7 bytes** long and prepends every Modbus TCP message. It provides transaction management, protocol identification, and routing information.

### MBAP Header Fields

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| **Transaction Identifier** | 2 | Identification of a request/response transaction |
| **Protocol Identifier** | 2 | Always 0x0000 for Modbus protocol |
| **Length Field** | 2 | Number of following bytes (Unit ID + PDU) |
| **Unit Identifier** | 1 | Identification of remote slave/server |

### Complete Frame Structure

```
[MBAP Header (7 bytes)] + [Function Code (1 byte)] + [Data (n bytes)]
│                                                                    │
└─────────────────── TCP Packet ────────────────────────────────────┘

Breakdown:
┌──────────────────┬──────────────────┬────────────┬─────────────┬─────────────┐
│ Transaction ID   │ Protocol ID      │ Length     │ Unit ID     │ PDU         │
│ (2 bytes)        │ (2 bytes)        │ (2 bytes)  │ (1 byte)    │ (variable)  │
└──────────────────┴──────────────────┴────────────┴─────────────┴─────────────┘
```

### Field Details

**Transaction Identifier (2 bytes)**
- Used by the client to pair requests with responses
- Echoed back by the server unchanged
- Allows multiple simultaneous transactions
- Typically incremented for each new request

**Protocol Identifier (2 bytes)**
- Always `0x0000` for Modbus
- Reserved for future protocol extensions
- Allows multiplexing of different protocols on same connection

**Length Field (2 bytes)**
- Indicates the number of bytes that follow
- Includes Unit Identifier + entire PDU (function code + data)
- Does NOT include the first 6 bytes of MBAP header
- Used for frame boundary detection

**Unit Identifier (1 byte)**
- Identifies the target device on a serial subnet
- For direct TCP connections, typically set to `0x00` or `0xFF`
- Useful when TCP gateway bridges to serial Modbus devices
- Equivalent to the slave address in serial Modbus

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

// MBAP Header structure
typedef struct {
    uint16_t transaction_id;   // Transaction identifier
    uint16_t protocol_id;      // Protocol identifier (always 0)
    uint16_t length;           // Length of following bytes
    uint8_t  unit_id;          // Unit identifier
} __attribute__((packed)) mbap_header_t;

// Complete Modbus TCP frame
typedef struct {
    mbap_header_t header;
    uint8_t function_code;
    uint8_t data[252];  // Maximum PDU size is 253 bytes (1 FC + 252 data)
} modbus_tcp_frame_t;

// Build MBAP header
void build_mbap_header(mbap_header_t *header, uint16_t trans_id, 
                       uint8_t unit_id, uint16_t pdu_length) {
    header->transaction_id = htons(trans_id);  // Convert to network byte order
    header->protocol_id = htons(0x0000);       // Always 0 for Modbus
    header->length = htons(pdu_length + 1);    // PDU length + unit_id
    header->unit_id = unit_id;
}

// Example: Build Read Holding Registers request (FC 0x03)
int build_read_holding_registers(uint8_t *buffer, uint16_t trans_id, 
                                  uint8_t unit_id, uint16_t start_addr, 
                                  uint16_t num_registers) {
    mbap_header_t *header = (mbap_header_t *)buffer;
    
    // Build MBAP header (PDU = 1 byte FC + 4 bytes data)
    build_mbap_header(header, trans_id, unit_id, 5);
    
    // Build PDU
    buffer[7] = 0x03;  // Function code
    *(uint16_t *)(&buffer[8]) = htons(start_addr);
    *(uint16_t *)(&buffer[10]) = htons(num_registers);
    
    return 12;  // Total frame size
}

// Parse MBAP header from received frame
int parse_mbap_header(const uint8_t *buffer, mbap_header_t *header) {
    if (!buffer || !header) return -1;
    
    header->transaction_id = ntohs(*(uint16_t *)(&buffer[0]));
    header->protocol_id = ntohs(*(uint16_t *)(&buffer[2]));
    header->length = ntohs(*(uint16_t *)(&buffer[4]));
    header->unit_id = buffer[6];
    
    // Validate protocol ID
    if (header->protocol_id != 0x0000) {
        return -1;  // Invalid protocol
    }
    
    return 0;
}

// Example usage
void example_usage() {
    uint8_t request[12];
    uint16_t transaction_id = 1;
    
    // Build request to read 10 registers starting at address 100
    int frame_size = build_read_holding_registers(
        request, transaction_id, 0x01, 100, 10
    );
    
    // Parse received response
    uint8_t response[256];
    mbap_header_t resp_header;
    // ... receive data into response buffer ...
    
    if (parse_mbap_header(response, &resp_header) == 0) {
        if (resp_header.transaction_id == transaction_id) {
            // Process matching response
            uint8_t function_code = response[7];
            // ... process PDU ...
        }
    }
}
```

## Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::TcpStream;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct MbapHeader {
    pub transaction_id: u16,  // Network byte order (big-endian)
    pub protocol_id: u16,     // Always 0x0000
    pub length: u16,          // Length of following bytes
    pub unit_id: u8,          // Unit identifier
}

impl MbapHeader {
    pub fn new(transaction_id: u16, unit_id: u8, pdu_length: u16) -> Self {
        MbapHeader {
            transaction_id: transaction_id.to_be(),
            protocol_id: 0u16.to_be(),
            length: (pdu_length + 1).to_be(),  // +1 for unit_id
            unit_id,
        }
    }
    
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, &'static str> {
        if bytes.len() < 7 {
            return Err("Insufficient bytes for MBAP header");
        }
        
        let header = MbapHeader {
            transaction_id: u16::from_be_bytes([bytes[0], bytes[1]]),
            protocol_id: u16::from_be_bytes([bytes[2], bytes[3]]),
            length: u16::from_be_bytes([bytes[4], bytes[5]]),
            unit_id: bytes[6],
        };
        
        // Validate protocol ID
        if u16::from_be(header.protocol_id) != 0 {
            return Err("Invalid protocol identifier");
        }
        
        Ok(header)
    }
    
    pub fn to_bytes(&self) -> [u8; 7] {
        let mut bytes = [0u8; 7];
        bytes[0..2].copy_from_slice(&self.transaction_id.to_be_bytes());
        bytes[2..4].copy_from_slice(&self.protocol_id.to_be_bytes());
        bytes[4..6].copy_from_slice(&self.length.to_be_bytes());
        bytes[6] = self.unit_id;
        bytes
    }
    
    pub fn get_transaction_id(&self) -> u16 {
        u16::from_be(self.transaction_id)
    }
    
    pub fn get_length(&self) -> u16 {
        u16::from_be(self.length)
    }
}

pub struct ModbusTcpFrame {
    pub header: MbapHeader,
    pub function_code: u8,
    pub data: Vec<u8>,
}

impl ModbusTcpFrame {
    // Build Read Holding Registers request (FC 0x03)
    pub fn read_holding_registers(
        transaction_id: u16,
        unit_id: u8,
        start_address: u16,
        num_registers: u16,
    ) -> Self {
        let mut data = Vec::with_capacity(4);
        data.extend_from_slice(&start_address.to_be_bytes());
        data.extend_from_slice(&num_registers.to_be_bytes());
        
        let pdu_length = 1 + data.len() as u16;  // FC + data
        let header = MbapHeader::new(transaction_id, unit_id, pdu_length);
        
        ModbusTcpFrame {
            header,
            function_code: 0x03,
            data,
        }
    }
    
    // Serialize frame to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(7 + 1 + self.data.len());
        bytes.extend_from_slice(&self.header.to_bytes());
        bytes.push(self.function_code);
        bytes.extend_from_slice(&self.data);
        bytes
    }
    
    // Parse frame from bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, &'static str> {
        if bytes.len() < 8 {
            return Err("Frame too short");
        }
        
        let header = MbapHeader::from_bytes(&bytes[0..7])?;
        let function_code = bytes[7];
        let data = bytes[8..].to_vec();
        
        // Validate length field matches actual data
        let expected_length = header.get_length();
        let actual_length = 1 + data.len() as u16;  // unit_id + FC + data
        
        if expected_length != actual_length {
            return Err("Length mismatch");
        }
        
        Ok(ModbusTcpFrame {
            header,
            function_code,
            data,
        })
    }
}

// Example client implementation
pub struct ModbusTcpClient {
    stream: TcpStream,
    transaction_id: u16,
}

impl ModbusTcpClient {
    pub fn connect(addr: &str) -> io::Result<Self> {
        let stream = TcpStream::connect(addr)?;
        Ok(ModbusTcpClient {
            stream,
            transaction_id: 0,
        })
    }
    
    pub fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_address: u16,
        count: u16,
    ) -> io::Result<Vec<u16>> {
        // Build request
        self.transaction_id = self.transaction_id.wrapping_add(1);
        let request = ModbusTcpFrame::read_holding_registers(
            self.transaction_id,
            unit_id,
            start_address,
            count,
        );
        
        // Send request
        let request_bytes = request.to_bytes();
        self.stream.write_all(&request_bytes)?;
        
        // Receive response
        let mut response_buf = vec![0u8; 261];  // Max: 7 MBAP + 1 FC + 1 byte count + 252 data
        let bytes_read = self.stream.read(&mut response_buf)?;
        response_buf.truncate(bytes_read);
        
        // Parse response
        let response = ModbusTcpFrame::from_bytes(&response_buf)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        
        // Validate transaction ID
        if response.header.get_transaction_id() != self.transaction_id {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Transaction ID mismatch",
            ));
        }
        
        // Parse register values
        if response.function_code == 0x03 && !response.data.is_empty() {
            let byte_count = response.data[0] as usize;
            let mut registers = Vec::new();
            
            for i in (1..byte_count + 1).step_by(2) {
                if i + 1 < response.data.len() {
                    let value = u16::from_be_bytes([
                        response.data[i],
                        response.data[i + 1],
                    ]);
                    registers.push(value);
                }
            }
            
            Ok(registers)
        } else {
            Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Invalid response",
            ))
        }
    }
}
```

## Summary

**Modbus TCP/IP Frame Structure** extends the traditional Modbus protocol for Ethernet networks by adding a 7-byte MBAP header that handles transaction management and routing. The **Transaction Identifier** enables concurrent request/response pairing, the **Protocol Identifier** (always 0x0000) reserves space for future protocols, the **Length Field** specifies the PDU size for frame boundary detection, and the **Unit Identifier** routes messages to specific devices, particularly useful when bridging TCP to serial networks. Unlike serial Modbus, TCP implementations eliminate the need for CRC checks since TCP handles error detection at the transport layer. This structure makes Modbus TCP ideal for industrial Ethernet applications while maintaining backward compatibility with the established Modbus PDU format. The implementations above demonstrate proper byte-order handling (big-endian network order), transaction tracking, and frame validation essential for reliable Modbus TCP communication.