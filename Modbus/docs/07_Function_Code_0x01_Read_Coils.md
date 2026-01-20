# Modbus Function Code 0x01: Read Coils

## Overview

Function Code 0x01 (Read Coils) is one of the fundamental Modbus operations used to read the status of discrete output coils from a slave device. Coils are binary (on/off) values that typically represent the state of digital outputs, such as relays, LEDs, or any other binary actuators.

## Key Concepts

### Coils
- **1-bit values** representing binary states (ON/OFF, TRUE/FALSE, 1/0)
- Typically mapped to physical outputs on the slave device
- Read/write capable (unlike discrete inputs which are read-only)
- Addressed using a 16-bit address space (0x0000 to 0xFFFF)

### Request Structure
The master sends a request containing:
- **Slave Address** (1 byte): Target device ID
- **Function Code** (1 byte): 0x01
- **Starting Address** (2 bytes): First coil address to read
- **Quantity of Coils** (2 bytes): Number of coils to read (1-2000)
- **CRC** (2 bytes): Error checking for RTU mode

### Response Structure
The slave responds with:
- **Slave Address** (1 byte): Echoes the slave ID
- **Function Code** (1 byte): 0x01
- **Byte Count** (1 byte): Number of data bytes following
- **Coil Status** (N bytes): Packed bit values (LSB first)
- **CRC** (2 bytes): Error checking

### Bit Packing
Coil values are packed into bytes with LSB (Least Significant Bit) first. If you read 10 coils, you'll receive 2 bytes (16 bits), with the first 10 bits containing the coil states and the remaining bits typically set to 0.

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Modbus RTU frame structure for Read Coils request
typedef struct {
    uint8_t slave_address;
    uint8_t function_code;
    uint16_t starting_address;
    uint16_t quantity_of_coils;
    uint16_t crc;
} __attribute__((packed)) modbus_read_coils_request_t;

// Modbus RTU frame structure for Read Coils response
typedef struct {
    uint8_t slave_address;
    uint8_t function_code;
    uint8_t byte_count;
    uint8_t coil_status[250];  // Maximum possible bytes
    uint16_t crc;
} modbus_read_coils_response_t;

// CRC16 calculation for Modbus RTU
uint16_t modbus_crc16(uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

// Convert uint16 to big-endian (Modbus uses big-endian)
uint16_t to_big_endian(uint16_t value) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

// Build a Read Coils request
int build_read_coils_request(uint8_t *buffer, uint8_t slave_addr, 
                             uint16_t start_addr, uint16_t num_coils) {
    if (num_coils < 1 || num_coils > 2000) {
        return -1;  // Invalid quantity
    }
    
    buffer[0] = slave_addr;
    buffer[1] = 0x01;  // Function code
    buffer[2] = (start_addr >> 8) & 0xFF;
    buffer[3] = start_addr & 0xFF;
    buffer[4] = (num_coils >> 8) & 0xFF;
    buffer[5] = num_coils & 0xFF;
    
    uint16_t crc = modbus_crc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;
    
    return 8;  // Total bytes in request
}

// Parse Read Coils response and extract coil values
int parse_read_coils_response(uint8_t *response, int response_len, 
                              uint8_t *coil_values, int expected_coils) {
    if (response_len < 5) {
        return -1;  // Response too short
    }
    
    uint8_t function_code = response[1];
    if (function_code == 0x81) {
        // Error response
        printf("Modbus error: Exception code 0x%02X\n", response[2]);
        return -1;
    }
    
    if (function_code != 0x01) {
        return -1;  // Wrong function code
    }
    
    uint8_t byte_count = response[2];
    int expected_bytes = (expected_coils + 7) / 8;
    
    if (byte_count != expected_bytes) {
        return -1;  // Byte count mismatch
    }
    
    // Verify CRC
    uint16_t received_crc = response[response_len - 2] | 
                           (response[response_len - 1] << 8);
    uint16_t calculated_crc = modbus_crc16(response, response_len - 2);
    
    if (received_crc != calculated_crc) {
        printf("CRC error\n");
        return -1;
    }
    
    // Extract coil values
    for (int i = 0; i < expected_coils; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        coil_values[i] = (response[3 + byte_index] >> bit_index) & 0x01;
    }
    
    return expected_coils;
}

// Example usage
int main() {
    uint8_t request[8];
    uint8_t response[] = {
        0x11,       // Slave address
        0x01,       // Function code
        0x02,       // Byte count
        0xCD, 0x6B, // Coil status (LSB first)
        0x2C, 0x3A  // CRC (example)
    };
    
    // Build request to read 10 coils starting at address 0x0013
    int req_len = build_read_coils_request(request, 0x11, 0x0013, 10);
    
    printf("Request: ");
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Parse response
    uint8_t coils[10];
    int num_coils = parse_read_coils_response(response, sizeof(response), 
                                              coils, 10);
    
    if (num_coils > 0) {
        printf("Coil values: ");
        for (int i = 0; i < num_coils; i++) {
            printf("%d ", coils[i]);
        }
        printf("\n");
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::io::{self, Error, ErrorKind};

// Calculate Modbus CRC16
fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        
        for _ in 0..8 {
            if (crc & 0x0001) != 0 {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

// Read Coils request builder
pub struct ReadCoilsRequest {
    slave_address: u8,
    starting_address: u16,
    quantity: u16,
}

impl ReadCoilsRequest {
    pub fn new(slave_address: u8, starting_address: u16, quantity: u16) 
        -> Result<Self, io::Error> {
        if quantity < 1 || quantity > 2000 {
            return Err(Error::new(
                ErrorKind::InvalidInput,
                "Quantity must be between 1 and 2000"
            ));
        }
        
        Ok(ReadCoilsRequest {
            slave_address,
            starting_address,
            quantity,
        })
    }
    
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buffer = Vec::with_capacity(8);
        
        buffer.push(self.slave_address);
        buffer.push(0x01); // Function code
        buffer.extend_from_slice(&self.starting_address.to_be_bytes());
        buffer.extend_from_slice(&self.quantity.to_be_bytes());
        
        let crc = modbus_crc16(&buffer);
        buffer.extend_from_slice(&crc.to_le_bytes());
        
        buffer
    }
}

// Read Coils response parser
pub struct ReadCoilsResponse {
    pub slave_address: u8,
    pub coil_values: Vec<bool>,
}

impl ReadCoilsResponse {
    pub fn from_bytes(data: &[u8], expected_quantity: u16) 
        -> Result<Self, io::Error> {
        if data.len() < 5 {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Response too short"
            ));
        }
        
        let slave_address = data[0];
        let function_code = data[1];
        
        // Check for exception response
        if function_code == 0x81 {
            return Err(Error::new(
                ErrorKind::Other,
                format!("Modbus exception: 0x{:02X}", data[2])
            ));
        }
        
        if function_code != 0x01 {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Invalid function code"
            ));
        }
        
        let byte_count = data[2] as usize;
        let expected_bytes = ((expected_quantity + 7) / 8) as usize;
        
        if byte_count != expected_bytes {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Byte count mismatch"
            ));
        }
        
        // Verify CRC
        let data_len = data.len();
        let received_crc = u16::from_le_bytes([
            data[data_len - 2],
            data[data_len - 1]
        ]);
        let calculated_crc = modbus_crc16(&data[0..data_len - 2]);
        
        if received_crc != calculated_crc {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "CRC verification failed"
            ));
        }
        
        // Extract coil values
        let mut coil_values = Vec::new();
        for i in 0..expected_quantity {
            let byte_index = (i / 8) as usize;
            let bit_index = i % 8;
            let bit_value = (data[3 + byte_index] >> bit_index) & 0x01;
            coil_values.push(bit_value != 0);
        }
        
        Ok(ReadCoilsResponse {
            slave_address,
            coil_values,
        })
    }
}

// Example usage
fn main() -> io::Result<()> {
    // Create a request to read 10 coils from address 0x0013 on slave 0x11
    let request = ReadCoilsRequest::new(0x11, 0x0013, 10)?;
    let request_bytes = request.to_bytes();
    
    println!("Request: {:02X?}", request_bytes);
    
    // Simulate response
    let response_data: Vec<u8> = vec![
        0x11,       // Slave address
        0x01,       // Function code
        0x02,       // Byte count
        0xCD, 0x6B, // Coil status
        0x2C, 0x3A  // CRC (example)
    ];
    
    // Parse response
    let response = ReadCoilsResponse::from_bytes(&response_data, 10)?;
    
    println!("Slave Address: 0x{:02X}", response.slave_address);
    println!("Coil Values: {:?}", response.coil_values);
    
    Ok(())
}
```

## Summary

**Function Code 0x01 (Read Coils)** is essential for monitoring discrete output states in Modbus networks. Key points include:

- **Purpose**: Read 1-2000 binary coil states from slave devices
- **Data Type**: 1-bit boolean values (ON/OFF states)
- **Addressing**: 16-bit address space, typically 0x0000-0xFFFF
- **Bit Packing**: Coil states packed into bytes, LSB first, requiring careful bit extraction
- **Error Handling**: CRC validation critical for data integrity; exception responses indicate errors
- **Common Uses**: Reading relay states, digital output status, switch positions

Both implementations demonstrate proper frame construction, CRC calculation, bit unpacking, and error handling—essential skills for robust Modbus communication in industrial automation systems.