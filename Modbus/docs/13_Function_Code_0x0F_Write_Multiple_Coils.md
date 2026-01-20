# Function Code 0x0F: Write Multiple Coils

## Overview

Modbus Function Code 0x0F (15 in decimal) allows you to write multiple coils (discrete outputs) in a single transaction. This is significantly more efficient than using Function Code 0x05 (Write Single Coil) multiple times, as it reduces network overhead and improves performance when you need to set the state of multiple coils at once.

## Use Cases

- **Batch Control Operations**: Simultaneously activating/deactivating multiple outputs (motors, valves, relays)
- **Configuration Updates**: Setting multiple control flags or system states in one operation
- **Performance Optimization**: Reducing network traffic when controlling groups of digital outputs
- **Pattern Writing**: Setting specific bit patterns across multiple coils

## Protocol Details

### Request Structure

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| Function Code | 1 | 0x0F (15) |
| Starting Address | 2 | First coil address (0x0000 to 0xFFFF) |
| Quantity of Outputs | 2 | Number of coils to write (1 to 1968) |
| Byte Count | 1 | Number of data bytes to follow |
| Output Values | N | Coil states packed into bytes (LSB first) |

### Response Structure

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| Function Code | 1 | 0x0F (15) |
| Starting Address | 2 | Echo of request starting address |
| Quantity of Outputs | 2 | Echo of number of coils written |

### Data Packing

Coil values are packed into bytes with the least significant bit (LSB) representing the first coil. Each byte contains up to 8 coil values. If the number of coils is not a multiple of 8, unused bits in the last byte should be set to 0.

**Example**: To write 10 coils with pattern `1010011001`:
- Byte 1: `0b11001010` (bits 0-7, LSB first)
- Byte 2: `0b000000010` (bits 8-9, remaining bits zero)

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// Modbus function code
#define MODBUS_FC_WRITE_MULTIPLE_COILS 0x0F

// Maximum coils per request (Modbus standard limit)
#define MAX_WRITE_COILS 1968

// Error codes
#define MODBUS_SUCCESS 0
#define MODBUS_ERROR_INVALID_QUANTITY -1
#define MODBUS_ERROR_BUFFER_TOO_SMALL -2

/**
 * Build a Modbus Write Multiple Coils request
 * 
 * @param buffer Output buffer for the request
 * @param buffer_size Size of the output buffer
 * @param slave_id Modbus slave/unit identifier
 * @param start_address Starting coil address
 * @param quantity Number of coils to write
 * @param coil_states Array of coil states (0 = OFF, non-zero = ON)
 * @return Number of bytes written, or negative error code
 */
int modbus_write_multiple_coils_request(
    uint8_t *buffer,
    size_t buffer_size,
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity,
    const uint8_t *coil_states)
{
    // Validate quantity
    if (quantity < 1 || quantity > MAX_WRITE_COILS) {
        return MODBUS_ERROR_INVALID_QUANTITY;
    }
    
    // Calculate byte count for packed coil data
    uint8_t byte_count = (quantity + 7) / 8;
    
    // Check buffer size (slave_id + FC + addr + qty + byte_count + data)
    size_t required_size = 1 + 1 + 2 + 2 + 1 + byte_count;
    if (buffer_size < required_size) {
        return MODBUS_ERROR_BUFFER_TOO_SMALL;
    }
    
    // Build request
    size_t idx = 0;
    buffer[idx++] = slave_id;
    buffer[idx++] = MODBUS_FC_WRITE_MULTIPLE_COILS;
    
    // Starting address (big-endian)
    buffer[idx++] = (start_address >> 8) & 0xFF;
    buffer[idx++] = start_address & 0xFF;
    
    // Quantity of outputs (big-endian)
    buffer[idx++] = (quantity >> 8) & 0xFF;
    buffer[idx++] = quantity & 0xFF;
    
    // Byte count
    buffer[idx++] = byte_count;
    
    // Pack coil states into bytes (LSB first)
    memset(&buffer[idx], 0, byte_count);
    for (uint16_t i = 0; i < quantity; i++) {
        if (coil_states[i]) {
            uint8_t byte_idx = i / 8;
            uint8_t bit_idx = i % 8;
            buffer[idx + byte_idx] |= (1 << bit_idx);
        }
    }
    idx += byte_count;
    
    return (int)idx;
}

/**
 * Parse a Modbus Write Multiple Coils response
 * 
 * @param buffer Response buffer
 * @param buffer_size Size of response buffer
 * @param slave_id Expected slave ID
 * @param start_address Expected starting address
 * @param quantity Expected quantity written
 * @return MODBUS_SUCCESS or error code
 */
int modbus_write_multiple_coils_response(
    const uint8_t *buffer,
    size_t buffer_size,
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity)
{
    // Minimum response size
    if (buffer_size < 6) {
        return -1;
    }
    
    // Verify slave ID and function code
    if (buffer[0] != slave_id || buffer[1] != MODBUS_FC_WRITE_MULTIPLE_COILS) {
        return -1;
    }
    
    // Extract response fields
    uint16_t resp_address = ((uint16_t)buffer[2] << 8) | buffer[3];
    uint16_t resp_quantity = ((uint16_t)buffer[4] << 8) | buffer[5];
    
    // Verify echoed values
    if (resp_address != start_address || resp_quantity != quantity) {
        return -1;
    }
    
    return MODBUS_SUCCESS;
}

// Example usage
void example_usage() {
    uint8_t request[256];
    uint8_t coil_states[10] = {1, 0, 1, 0, 0, 1, 1, 0, 0, 1};
    
    int len = modbus_write_multiple_coils_request(
        request, sizeof(request),
        1,      // slave_id
        100,    // start_address
        10,     // quantity
        coil_states
    );
    
    if (len > 0) {
        // Send request via serial/TCP
        // ... transmission code ...
        
        // Parse response
        uint8_t response[6];
        // ... receive response ...
        
        int result = modbus_write_multiple_coils_response(
            response, sizeof(response),
            1, 100, 10
        );
    }
}
```

## Rust Implementation

```rust
use std::io::{self, Write};

const MODBUS_FC_WRITE_MULTIPLE_COILS: u8 = 0x0F;
const MAX_WRITE_COILS: u16 = 1968;

#[derive(Debug)]
pub enum ModbusError {
    InvalidQuantity,
    BufferTooSmall,
    InvalidResponse,
    IoError(io::Error),
}

impl From<io::Error> for ModbusError {
    fn from(err: io::Error) -> Self {
        ModbusError::IoError(err)
    }
}

pub struct WriteMultipleCoilsRequest {
    pub slave_id: u8,
    pub start_address: u16,
    pub coil_states: Vec<bool>,
}

impl WriteMultipleCoilsRequest {
    /// Create a new Write Multiple Coils request
    pub fn new(slave_id: u8, start_address: u16, coil_states: Vec<bool>) -> Result<Self, ModbusError> {
        let quantity = coil_states.len() as u16;
        
        if quantity < 1 || quantity > MAX_WRITE_COILS {
            return Err(ModbusError::InvalidQuantity);
        }
        
        Ok(Self {
            slave_id,
            start_address,
            coil_states,
        })
    }
    
    /// Encode the request into a byte buffer
    pub fn encode(&self) -> Vec<u8> {
        let quantity = self.coil_states.len() as u16;
        let byte_count = ((quantity + 7) / 8) as u8;
        
        let mut buffer = Vec::with_capacity(7 + byte_count as usize);
        
        // Header
        buffer.push(self.slave_id);
        buffer.push(MODBUS_FC_WRITE_MULTIPLE_COILS);
        buffer.extend_from_slice(&self.start_address.to_be_bytes());
        buffer.extend_from_slice(&quantity.to_be_bytes());
        buffer.push(byte_count);
        
        // Pack coil states into bytes (LSB first)
        let mut packed_bytes = vec![0u8; byte_count as usize];
        for (i, &state) in self.coil_states.iter().enumerate() {
            if state {
                let byte_idx = i / 8;
                let bit_idx = i % 8;
                packed_bytes[byte_idx] |= 1 << bit_idx;
            }
        }
        buffer.extend_from_slice(&packed_bytes);
        
        buffer
    }
}

pub struct WriteMultipleCoilsResponse {
    pub slave_id: u8,
    pub start_address: u16,
    pub quantity: u16,
}

impl WriteMultipleCoilsResponse {
    /// Decode a response from a byte buffer
    pub fn decode(buffer: &[u8]) -> Result<Self, ModbusError> {
        if buffer.len() < 6 {
            return Err(ModbusError::InvalidResponse);
        }
        
        if buffer[1] != MODBUS_FC_WRITE_MULTIPLE_COILS {
            return Err(ModbusError::InvalidResponse);
        }
        
        Ok(Self {
            slave_id: buffer[0],
            start_address: u16::from_be_bytes([buffer[2], buffer[3]]),
            quantity: u16::from_be_bytes([buffer[4], buffer[5]]),
        })
    }
    
    /// Verify the response matches the request
    pub fn verify(&self, request: &WriteMultipleCoilsRequest) -> Result<(), ModbusError> {
        if self.slave_id != request.slave_id 
            || self.start_address != request.start_address
            || self.quantity != request.coil_states.len() as u16 {
            return Err(ModbusError::InvalidResponse);
        }
        Ok(())
    }
}

// Example usage
fn example() -> Result<(), ModbusError> {
    // Create request to write 10 coils
    let coil_states = vec![true, false, true, false, false, true, true, false, false, true];
    let request = WriteMultipleCoilsRequest::new(1, 100, coil_states)?;
    
    // Encode to bytes
    let request_bytes = request.encode();
    println!("Request: {:02X?}", request_bytes);
    
    // Simulate sending and receiving
    // ... actual I/O operations ...
    
    // Parse response
    let response_bytes = vec![0x01, 0x0F, 0x00, 0x64, 0x00, 0x0A];
    let response = WriteMultipleCoilsResponse::decode(&response_bytes)?;
    
    // Verify response
    response.verify(&request)?;
    
    println!("Successfully wrote {} coils at address {}", 
             response.quantity, response.start_address);
    
    Ok(())
}

// Helper function to create coil patterns
pub fn create_coil_pattern(pattern: &str) -> Vec<bool> {
    pattern.chars()
        .filter_map(|c| match c {
            '1' => Some(true),
            '0' => Some(false),
            _ => None,
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_encode_10_coils() {
        let states = vec![true, false, true, false, false, true, true, false, false, true];
        let req = WriteMultipleCoilsRequest::new(1, 100, states).unwrap();
        let encoded = req.encode();
        
        // Verify structure
        assert_eq!(encoded[0], 1);  // slave_id
        assert_eq!(encoded[1], 0x0F);  // function code
        assert_eq!(encoded[2..4], [0x00, 0x64]);  // address 100
        assert_eq!(encoded[4..6], [0x00, 0x0A]);  // quantity 10
        assert_eq!(encoded[6], 2);  // byte count
        
        // Verify packed data: 1010011001 -> 0xCA, 0x02
        assert_eq!(encoded[7], 0b01011001);  // bits 0-7
        assert_eq!(encoded[8], 0b00000010);  // bits 8-9
    }
}
```

## Summary

**Function Code 0x0F (Write Multiple Coils)** is essential for efficient batch control of discrete outputs in Modbus systems. It significantly reduces network overhead compared to individual write operations by allowing up to 1968 coils to be written in a single transaction. The coil states are packed into bytes using LSB-first ordering, making the protocol compact and bandwidth-efficient.

**Key advantages** include reduced network traffic, atomic multi-output updates, and improved system performance. The implementations above demonstrate proper handling of bit packing, big-endian byte ordering for addresses and quantities, and thorough validation of responses. Both C/C++ and Rust examples provide production-ready code with error handling, making them suitable for industrial automation, SCADA systems, and IoT device control applications.