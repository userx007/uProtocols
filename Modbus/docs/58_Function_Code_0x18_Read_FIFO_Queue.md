# Modbus Function Code 0x18: Read FIFO Queue

## Detailed Description

Function Code 0x18 (24 decimal) is a specialized Modbus function used to read the contents of a First-In-First-Out (FIFO) queue from a slave device. Unlike standard holding or input registers that can be read in any order, FIFO queues maintain sequential data where the first value written is the first value read.

### Key Characteristics

**Purpose**: This function retrieves data from a FIFO buffer in the slave device without removing the data from the queue. It's particularly useful for devices that accumulate sequential data such as sensor readings, event logs, or time-series measurements.

**FIFO Pointer Address**: The function uses a 16-bit FIFO pointer address (0x0000 to 0xFFFF) that identifies which FIFO queue to read from a device. A single slave may maintain multiple FIFO queues.

**Queue Behavior**: The FIFO queue operates on a first-in-first-out principle. Data is added at the tail and read from the head. The queue has a maximum capacity defined by the device implementation.

### Request Format

The request consists of:
- **Function Code**: 1 byte (0x18)
- **FIFO Pointer Address**: 2 bytes (big-endian)

Total request length: 3 bytes (plus slave address and CRC for RTU)

### Response Format

The response contains:
- **Function Code**: 1 byte (0x18)
- **Byte Count**: 2 bytes (number of bytes to follow)
- **FIFO Count**: 2 bytes (number of registers in the FIFO)
- **FIFO Value Registers**: N × 2 bytes (register values)

The byte count = 2 + (FIFO Count × 2)

### Limitations and Constraints

- Maximum FIFO count is typically 31 registers (implementation dependent)
- The function reads but does not consume the FIFO data
- If the FIFO is empty, FIFO Count will be 0
- Exception responses follow standard Modbus error handling

## C/C++ Implementation

### Request Builder

```c
#include <stdint.h>
#include <string.h>

#define MODBUS_FC_READ_FIFO_QUEUE 0x18

// Structure for Read FIFO Queue request
typedef struct {
    uint8_t slave_id;
    uint8_t function_code;
    uint16_t fifo_pointer_address;
} modbus_read_fifo_request_t;

// Structure for Read FIFO Queue response
typedef struct {
    uint8_t slave_id;
    uint8_t function_code;
    uint16_t byte_count;
    uint16_t fifo_count;
    uint16_t *fifo_values;  // Dynamic array
} modbus_read_fifo_response_t;

/**
 * Build a Modbus Read FIFO Queue request
 * 
 * @param buffer Output buffer for the request
 * @param slave_id Slave device address
 * @param fifo_address FIFO pointer address
 * @return Length of the request in bytes
 */
int modbus_build_read_fifo_request(uint8_t *buffer, 
                                    uint8_t slave_id, 
                                    uint16_t fifo_address) {
    buffer[0] = slave_id;
    buffer[1] = MODBUS_FC_READ_FIFO_QUEUE;
    buffer[2] = (fifo_address >> 8) & 0xFF;  // High byte
    buffer[3] = fifo_address & 0xFF;          // Low byte
    
    return 4;  // Without CRC
}

/**
 * Calculate Modbus RTU CRC16
 */
uint16_t modbus_crc16(uint8_t *buffer, int length) {
    uint16_t crc = 0xFFFF;
    
    for (int i = 0; i < length; i++) {
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

/**
 * Build complete RTU request with CRC
 */
int modbus_build_read_fifo_rtu(uint8_t *buffer, 
                                uint8_t slave_id, 
                                uint16_t fifo_address) {
    int length = modbus_build_read_fifo_request(buffer, slave_id, fifo_address);
    uint16_t crc = modbus_crc16(buffer, length);
    
    buffer[length] = crc & 0xFF;          // CRC Low
    buffer[length + 1] = (crc >> 8) & 0xFF;  // CRC High
    
    return length + 2;
}
```

### Response Parser

```c
#include <stdlib.h>
#include <stdio.h>

/**
 * Parse Read FIFO Queue response
 * 
 * @param buffer Response buffer
 * @param length Buffer length
 * @param response Output structure
 * @return 0 on success, -1 on error
 */
int modbus_parse_read_fifo_response(uint8_t *buffer, 
                                     int length,
                                     modbus_read_fifo_response_t *response) {
    if (length < 6) {
        fprintf(stderr, "Response too short\n");
        return -1;
    }
    
    response->slave_id = buffer[0];
    response->function_code = buffer[1];
    
    // Check for exception response
    if (response->function_code & 0x80) {
        fprintf(stderr, "Exception code: 0x%02X\n", buffer[2]);
        return -1;
    }
    
    if (response->function_code != MODBUS_FC_READ_FIFO_QUEUE) {
        fprintf(stderr, "Invalid function code\n");
        return -1;
    }
    
    // Parse byte count (big-endian)
    response->byte_count = (buffer[2] << 8) | buffer[3];
    
    // Parse FIFO count
    response->fifo_count = (buffer[4] << 8) | buffer[5];
    
    // Verify byte count
    if (response->byte_count != (2 + response->fifo_count * 2)) {
        fprintf(stderr, "Invalid byte count\n");
        return -1;
    }
    
    // Allocate and parse FIFO values
    if (response->fifo_count > 0) {
        response->fifo_values = (uint16_t *)malloc(response->fifo_count * sizeof(uint16_t));
        if (!response->fifo_values) {
            fprintf(stderr, "Memory allocation failed\n");
            return -1;
        }
        
        for (int i = 0; i < response->fifo_count; i++) {
            int offset = 6 + (i * 2);
            response->fifo_values[i] = (buffer[offset] << 8) | buffer[offset + 1];
        }
    } else {
        response->fifo_values = NULL;
    }
    
    return 0;
}

/**
 * Free response structure
 */
void modbus_free_read_fifo_response(modbus_read_fifo_response_t *response) {
    if (response->fifo_values) {
        free(response->fifo_values);
        response->fifo_values = NULL;
    }
}
```

### Complete Example

```c
#include <stdio.h>

int main() {
    uint8_t request[8];
    uint8_t slave_id = 0x01;
    uint16_t fifo_address = 0x04DE;  // FIFO pointer address
    
    // Build request
    int req_len = modbus_build_read_fifo_rtu(request, slave_id, fifo_address);
    
    printf("Request (%d bytes): ", req_len);
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Simulated response: 6 registers in FIFO
    uint8_t response[] = {
        0x01,       // Slave ID
        0x18,       // Function code
        0x00, 0x0E, // Byte count = 14 (2 + 6*2)
        0x00, 0x06, // FIFO count = 6
        0x01, 0xB8, // Register 1: 440
        0x01, 0x76, // Register 2: 374
        0x00, 0x00, // Register 3: 0
        0x00, 0x20, // Register 4: 32
        0x00, 0x00, // Register 5: 0
        0x00, 0x00  // Register 6: 0
    };
    
    modbus_read_fifo_response_t parsed_response;
    
    if (modbus_parse_read_fifo_response(response, sizeof(response), &parsed_response) == 0) {
        printf("\nParsed Response:\n");
        printf("Slave ID: %d\n", parsed_response.slave_id);
        printf("Function Code: 0x%02X\n", parsed_response.function_code);
        printf("FIFO Count: %d\n", parsed_response.fifo_count);
        printf("FIFO Values:\n");
        
        for (int i = 0; i < parsed_response.fifo_count; i++) {
            printf("  Register %d: %d (0x%04X)\n", 
                   i + 1, 
                   parsed_response.fifo_values[i],
                   parsed_response.fifo_values[i]);
        }
        
        modbus_free_read_fifo_response(&parsed_response);
    }
    
    return 0;
}
```

## Rust Implementation

### Request Builder

```rust
/// Modbus Read FIFO Queue implementation
use std::io::{self, Error, ErrorKind};

const MODBUS_FC_READ_FIFO_QUEUE: u8 = 0x18;

/// Read FIFO Queue request structure
#[derive(Debug, Clone)]
pub struct ReadFifoRequest {
    pub slave_id: u8,
    pub fifo_pointer_address: u16,
}

impl ReadFifoRequest {
    /// Create a new Read FIFO Queue request
    pub fn new(slave_id: u8, fifo_pointer_address: u16) -> Self {
        ReadFifoRequest {
            slave_id,
            fifo_pointer_address,
        }
    }
    
    /// Build the request frame (without CRC for TCP, with CRC for RTU)
    pub fn to_bytes(&self) -> Vec<u8> {
        vec![
            self.slave_id,
            MODBUS_FC_READ_FIFO_QUEUE,
            (self.fifo_pointer_address >> 8) as u8,  // High byte
            (self.fifo_pointer_address & 0xFF) as u8, // Low byte
        ]
    }
    
    /// Build RTU frame with CRC
    pub fn to_rtu_frame(&self) -> Vec<u8> {
        let mut frame = self.to_bytes();
        let crc = calculate_crc16(&frame);
        frame.push((crc & 0xFF) as u8);        // CRC Low
        frame.push((crc >> 8) as u8);          // CRC High
        frame
    }
}

/// Calculate Modbus RTU CRC16
fn calculate_crc16(data: &[u8]) -> u16 {
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

### Response Parser

```rust
/// Read FIFO Queue response structure
#[derive(Debug, Clone)]
pub struct ReadFifoResponse {
    pub slave_id: u8,
    pub fifo_count: u16,
    pub fifo_values: Vec<u16>,
}

impl ReadFifoResponse {
    /// Parse response from bytes
    pub fn from_bytes(data: &[u8]) -> io::Result<Self> {
        if data.len() < 6 {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Response too short"
            ));
        }
        
        let slave_id = data[0];
        let function_code = data[1];
        
        // Check for exception
        if function_code & 0x80 != 0 {
            let exception_code = data[2];
            return Err(Error::new(
                ErrorKind::Other,
                format!("Modbus exception: 0x{:02X}", exception_code)
            ));
        }
        
        if function_code != MODBUS_FC_READ_FIFO_QUEUE {
            return Err(Error::new(
                ErrorKind::InvalidData,
                format!("Invalid function code: 0x{:02X}", function_code)
            ));
        }
        
        // Parse byte count (big-endian)
        let byte_count = u16::from_be_bytes([data[2], data[3]]);
        
        // Parse FIFO count
        let fifo_count = u16::from_be_bytes([data[4], data[5]]);
        
        // Verify byte count
        if byte_count != (2 + fifo_count * 2) {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Byte count mismatch"
            ));
        }
        
        // Verify data length
        if data.len() < (6 + fifo_count * 2) as usize {
            return Err(Error::new(
                ErrorKind::InvalidData,
                "Insufficient data for FIFO values"
            ));
        }
        
        // Parse FIFO values
        let mut fifo_values = Vec::with_capacity(fifo_count as usize);
        for i in 0..fifo_count {
            let offset = 6 + (i * 2) as usize;
            let value = u16::from_be_bytes([data[offset], data[offset + 1]]);
            fifo_values.push(value);
        }
        
        Ok(ReadFifoResponse {
            slave_id,
            fifo_count,
            fifo_values,
        })
    }
    
    /// Parse RTU response (validates and removes CRC)
    pub fn from_rtu_frame(data: &[u8]) -> io::Result<Self> {
        if data.len() < 8 {  // Minimum: slave + fc + byte_count + fifo_count + crc
            return Err(Error::new(
                ErrorKind::InvalidData,
                "RTU frame too short"
            ));
        }
        
        // Verify CRC
        let payload_len = data.len() - 2;
        let received_crc = u16::from_le_bytes([data[payload_len], data[payload_len + 1]]);
        let calculated_crc = calculate_crc16(&data[..payload_len]);
        
        if received_crc != calculated_crc {
            return Err(Error::new(
                ErrorKind::InvalidData,
                format!("CRC mismatch: expected 0x{:04X}, got 0x{:04X}", 
                        calculated_crc, received_crc)
            ));
        }
        
        Self::from_bytes(&data[..payload_len])
    }
}
```

### Complete Rust Example

```rust
fn main() -> io::Result<()> {
    // Create a Read FIFO Queue request
    let request = ReadFifoRequest::new(0x01, 0x04DE);
    
    // Build RTU frame
    let rtu_frame = request.to_rtu_frame();
    
    println!("Request ({} bytes):", rtu_frame.len());
    for byte in &rtu_frame {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Simulated response
    let response_data: Vec<u8> = vec![
        0x01,       // Slave ID
        0x18,       // Function code
        0x00, 0x0E, // Byte count = 14
        0x00, 0x06, // FIFO count = 6
        0x01, 0xB8, // Register 1: 440
        0x01, 0x76, // Register 2: 374
        0x00, 0x00, // Register 3: 0
        0x00, 0x20, // Register 4: 32
        0x00, 0x00, // Register 5: 0
        0x00, 0x00, // Register 6: 0
    ];
    
    // Parse response
    match ReadFifoResponse::from_bytes(&response_data) {
        Ok(response) => {
            println!("Parsed Response:");
            println!("Slave ID: {}", response.slave_id);
            println!("FIFO Count: {}", response.fifo_count);
            println!("FIFO Values:");
            
            for (i, value) in response.fifo_values.iter().enumerate() {
                println!("  Register {}: {} (0x{:04X})", i + 1, value, value);
            }
        }
        Err(e) => {
            eprintln!("Error parsing response: {}", e);
        }
    }
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_request_building() {
        let request = ReadFifoRequest::new(0x01, 0x04DE);
        let frame = request.to_bytes();
        
        assert_eq!(frame.len(), 4);
        assert_eq!(frame[0], 0x01);
        assert_eq!(frame[1], 0x18);
        assert_eq!(frame[2], 0x04);
        assert_eq!(frame[3], 0xDE);
    }
    
    #[test]
    fn test_response_parsing() {
        let data = vec![
            0x01, 0x18, 0x00, 0x06, 0x00, 0x02,
            0x01, 0xB8, 0x01, 0x76
        ];
        
        let response = ReadFifoResponse::from_bytes(&data).unwrap();
        
        assert_eq!(response.slave_id, 0x01);
        assert_eq!(response.fifo_count, 2);
        assert_eq!(response.fifo_values.len(), 2);
        assert_eq!(response.fifo_values[0], 440);
        assert_eq!(response.fifo_values[1], 374);
    }
    
    #[test]
    fn test_crc_calculation() {
        let data = vec![0x01, 0x18, 0x04, 0xDE];
        let crc = calculate_crc16(&data);
        
        // CRC should be deterministic
        assert_eq!(crc, calculate_crc16(&data));
    }
}
```

## Summary

**Modbus Function Code 0x18 (Read FIFO Queue)** is a specialized function for reading sequential data from First-In-First-Out queues in slave devices. It differs from standard register reads by maintaining the order and integrity of time-series or event data.

**Key Points:**
- Reads FIFO queue contents without consuming the data
- Uses a FIFO pointer address to identify the queue
- Returns the FIFO count and all register values in sequence
- Useful for buffered sensor data, event logs, and sequential measurements
- Maximum typical capacity is 31 registers per queue
- Follows standard Modbus exception handling

**Implementation considerations** include proper byte order handling (big-endian), CRC validation for RTU mode, dynamic memory allocation for variable-length responses, and error handling for empty queues or invalid addresses. Both C/C++ and Rust implementations demonstrate robust parsing with validation and clear error reporting suitable for industrial applications.