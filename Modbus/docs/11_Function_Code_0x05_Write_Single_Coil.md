# Modbus Function Code 0x05: Write Single Coil

## Overview

Function Code 0x05 (Write Single Coil) is used to write a single discrete output (coil) to either ON or OFF state in a Modbus slave device. Coils are binary outputs that can be controlled individually, such as relays, motors, valves, or indicator lights.

This function writes to a single bit at a specific address in the coil address space (0x0000 to 0xFFFF). The request and response formats are identical when the write operation succeeds, providing a simple acknowledgment mechanism.

## Protocol Details

### Request Format

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x05 |
| Output Address | 2 bytes | Address of the coil (0x0000 to 0xFFFF) |
| Output Value | 2 bytes | 0xFF00 = ON, 0x0000 = OFF |

**Output Value Convention:**
- `0xFF00` (65280 decimal) = Coil ON
- `0x0000` (0 decimal) = Coil OFF
- Any other value is invalid and should result in an error

### Response Format (Normal)

The normal response echoes the request exactly:

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x05 |
| Output Address | 2 bytes | Echo of the requested address |
| Output Value | 2 bytes | Echo of the requested value |

### Error Response

| Field | Length | Description |
|-------|--------|-------------|
| Error Code | 1 byte | 0x85 (0x05 + 0x80) |
| Exception Code | 1 byte | See exception codes below |

**Common Exception Codes:**
- `0x01` - Illegal Function
- `0x02` - Illegal Data Address (coil doesn't exist)
- `0x03` - Illegal Data Value (value not 0x0000 or 0xFF00)
- `0x04` - Slave Device Failure

## Example Messages

### Example 1: Turn ON coil at address 0x00AC

**Request:** `05 00 AC FF 00`
- Function: 0x05
- Address: 0x00AC (172 decimal)
- Value: 0xFF00 (ON)

**Response:** `05 00 AC FF 00` (echoes request)

### Example 2: Turn OFF coil at address 0x0001

**Request:** `05 00 01 00 00`
- Function: 0x05
- Address: 0x0001 (1 decimal)
- Value: 0x0000 (OFF)

**Response:** `05 00 01 00 00` (echoes request)

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MODBUS_FC_WRITE_SINGLE_COIL 0x05
#define MODBUS_COIL_ON  0xFF00
#define MODBUS_COIL_OFF 0x0000

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS  0x02
#define MODBUS_EXCEPTION_ILLEGAL_VALUE    0x03
#define MODBUS_EXCEPTION_DEVICE_FAILURE   0x04

typedef struct {
    uint8_t function_code;
    uint16_t address;
    uint16_t value;
} modbus_write_single_coil_t;

/**
 * Build a Write Single Coil request
 */
int modbus_build_write_single_coil_request(
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t coil_address,
    bool coil_state)
{
    if (buffer_size < 5) {
        return -1; // Insufficient buffer
    }
    
    buffer[0] = MODBUS_FC_WRITE_SINGLE_COIL;
    buffer[1] = (coil_address >> 8) & 0xFF;  // Address high byte
    buffer[2] = coil_address & 0xFF;          // Address low byte
    
    uint16_t value = coil_state ? MODBUS_COIL_ON : MODBUS_COIL_OFF;
    buffer[3] = (value >> 8) & 0xFF;          // Value high byte
    buffer[4] = value & 0xFF;                 // Value low byte
    
    return 5; // Return message length
}

/**
 * Parse a Write Single Coil request
 */
int modbus_parse_write_single_coil_request(
    const uint8_t *buffer,
    size_t length,
    modbus_write_single_coil_t *request)
{
    if (length < 5) {
        return -1; // Insufficient data
    }
    
    if (buffer[0] != MODBUS_FC_WRITE_SINGLE_COIL) {
        return -2; // Wrong function code
    }
    
    request->function_code = buffer[0];
    request->address = ((uint16_t)buffer[1] << 8) | buffer[2];
    request->value = ((uint16_t)buffer[3] << 8) | buffer[4];
    
    // Validate value
    if (request->value != MODBUS_COIL_ON && request->value != MODBUS_COIL_OFF) {
        return -3; // Invalid value
    }
    
    return 0; // Success
}

/**
 * Build a Write Single Coil response (echoes request)
 */
int modbus_build_write_single_coil_response(
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t coil_address,
    bool coil_state)
{
    // Response is identical to request
    return modbus_build_write_single_coil_request(
        buffer, buffer_size, coil_address, coil_state);
}

/**
 * Build an error response
 */
int modbus_build_error_response(
    uint8_t *buffer,
    size_t buffer_size,
    uint8_t function_code,
    uint8_t exception_code)
{
    if (buffer_size < 2) {
        return -1;
    }
    
    buffer[0] = function_code | 0x80; // Set MSB to indicate error
    buffer[1] = exception_code;
    
    return 2;
}

/**
 * Example slave handler for Write Single Coil
 */
int handle_write_single_coil(
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t response_size,
    bool *coil_array,
    size_t coil_count)
{
    modbus_write_single_coil_t req;
    
    // Parse request
    int result = modbus_parse_write_single_coil_request(request, request_len, &req);
    
    if (result == -3) {
        // Invalid value
        return modbus_build_error_response(
            response, response_size,
            MODBUS_FC_WRITE_SINGLE_COIL,
            MODBUS_EXCEPTION_ILLEGAL_VALUE);
    }
    
    if (result < 0) {
        // Parse error
        return modbus_build_error_response(
            response, response_size,
            MODBUS_FC_WRITE_SINGLE_COIL,
            MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
    }
    
    // Check address validity
    if (req.address >= coil_count) {
        return modbus_build_error_response(
            response, response_size,
            MODBUS_FC_WRITE_SINGLE_COIL,
            MODBUS_EXCEPTION_ILLEGAL_ADDRESS);
    }
    
    // Write the coil
    coil_array[req.address] = (req.value == MODBUS_COIL_ON);
    
    // Echo the request as response
    memcpy(response, request, 5);
    return 5;
}

// Example usage
int main(void) {
    uint8_t request[5];
    uint8_t response[5];
    bool coils[256] = {false}; // Array of 256 coils
    
    // Build request to turn ON coil 172
    int len = modbus_build_write_single_coil_request(
        request, sizeof(request), 172, true);
    
    // Handle the request
    len = handle_write_single_coil(
        request, len, response, sizeof(response),
        coils, 256);
    
    // Check if coil was set
    if (coils[172]) {
        // Success - coil 172 is now ON
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::io::{self, Error, ErrorKind};

const MODBUS_FC_WRITE_SINGLE_COIL: u8 = 0x05;
const MODBUS_COIL_ON: u16 = 0xFF00;
const MODBUS_COIL_OFF: u16 = 0x0000;

// Exception codes
const MODBUS_EXCEPTION_ILLEGAL_FUNCTION: u8 = 0x01;
const MODBUS_EXCEPTION_ILLEGAL_ADDRESS: u8 = 0x02;
const MODBUS_EXCEPTION_ILLEGAL_VALUE: u8 = 0x03;

#[derive(Debug, Clone, Copy)]
pub struct WriteSingleCoilRequest {
    pub address: u16,
    pub value: u16,
}

impl WriteSingleCoilRequest {
    /// Create a new Write Single Coil request
    pub fn new(address: u16, state: bool) -> Self {
        Self {
            address,
            value: if state { MODBUS_COIL_ON } else { MODBUS_COIL_OFF },
        }
    }
    
    /// Encode the request to bytes
    pub fn encode(&self) -> Vec<u8> {
        vec![
            MODBUS_FC_WRITE_SINGLE_COIL,
            (self.address >> 8) as u8,
            (self.address & 0xFF) as u8,
            (self.value >> 8) as u8,
            (self.value & 0xFF) as u8,
        ]
    }
    
    /// Decode a request from bytes
    pub fn decode(buffer: &[u8]) -> io::Result<Self> {
        if buffer.len() < 5 {
            return Err(Error::new(ErrorKind::InvalidData, "Insufficient data"));
        }
        
        if buffer[0] != MODBUS_FC_WRITE_SINGLE_COIL {
            return Err(Error::new(ErrorKind::InvalidData, "Wrong function code"));
        }
        
        let address = u16::from_be_bytes([buffer[1], buffer[2]]);
        let value = u16::from_be_bytes([buffer[3], buffer[4]]);
        
        // Validate value
        if value != MODBUS_COIL_ON && value != MODBUS_COIL_OFF {
            return Err(Error::new(ErrorKind::InvalidData, "Invalid coil value"));
        }
        
        Ok(Self { address, value })
    }
    
    /// Get the coil state
    pub fn get_state(&self) -> bool {
        self.value == MODBUS_COIL_ON
    }
}

#[derive(Debug)]
pub struct WriteSingleCoilResponse {
    pub address: u16,
    pub value: u16,
}

impl WriteSingleCoilResponse {
    /// Create a response (echoes the request)
    pub fn from_request(request: &WriteSingleCoilRequest) -> Self {
        Self {
            address: request.address,
            value: request.value,
        }
    }
    
    /// Encode the response to bytes
    pub fn encode(&self) -> Vec<u8> {
        vec![
            MODBUS_FC_WRITE_SINGLE_COIL,
            (self.address >> 8) as u8,
            (self.address & 0xFF) as u8,
            (self.value >> 8) as u8,
            (self.value & 0xFF) as u8,
        ]
    }
    
    /// Decode a response from bytes
    pub fn decode(buffer: &[u8]) -> io::Result<Self> {
        if buffer.len() < 5 {
            return Err(Error::new(ErrorKind::InvalidData, "Insufficient data"));
        }
        
        if buffer[0] != MODBUS_FC_WRITE_SINGLE_COIL {
            return Err(Error::new(ErrorKind::InvalidData, "Wrong function code"));
        }
        
        let address = u16::from_be_bytes([buffer[1], buffer[2]]);
        let value = u16::from_be_bytes([buffer[3], buffer[4]]);
        
        Ok(Self { address, value })
    }
}

#[derive(Debug)]
pub enum ModbusError {
    IllegalFunction,
    IllegalAddress,
    IllegalValue,
}

impl ModbusError {
    pub fn to_exception_code(&self) -> u8 {
        match self {
            ModbusError::IllegalFunction => MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
            ModbusError::IllegalAddress => MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
            ModbusError::IllegalValue => MODBUS_EXCEPTION_ILLEGAL_VALUE,
        }
    }
    
    pub fn encode(&self) -> Vec<u8> {
        vec![
            MODBUS_FC_WRITE_SINGLE_COIL | 0x80,
            self.to_exception_code(),
        ]
    }
}

/// Modbus slave handler for Write Single Coil
pub struct ModbusSlave {
    coils: Vec<bool>,
}

impl ModbusSlave {
    pub fn new(coil_count: usize) -> Self {
        Self {
            coils: vec![false; coil_count],
        }
    }
    
    /// Handle a Write Single Coil request
    pub fn handle_write_single_coil(
        &mut self,
        request: &[u8],
    ) -> Result<Vec<u8>, ModbusError> {
        // Parse request
        let req = WriteSingleCoilRequest::decode(request)
            .map_err(|_| ModbusError::IllegalValue)?;
        
        // Check address validity
        if req.address as usize >= self.coils.len() {
            return Err(ModbusError::IllegalAddress);
        }
        
        // Write the coil
        self.coils[req.address as usize] = req.get_state();
        
        // Create and encode response (echo request)
        let response = WriteSingleCoilResponse::from_request(&req);
        Ok(response.encode())
    }
    
    /// Get coil state
    pub fn get_coil(&self, address: u16) -> Option<bool> {
        self.coils.get(address as usize).copied()
    }
}

// Example usage
fn main() -> io::Result<()> {
    // Create a slave with 256 coils
    let mut slave = ModbusSlave::new(256);
    
    // Create request to turn ON coil 172
    let request = WriteSingleCoilRequest::new(172, true);
    let request_bytes = request.encode();
    
    // Handle the request
    match slave.handle_write_single_coil(&request_bytes) {
        Ok(response) => {
            println!("Response: {:02X?}", response);
            
            // Verify coil was set
            if let Some(state) = slave.get_coil(172) {
                println!("Coil 172 is now: {}", if state { "ON" } else { "OFF" });
            }
        }
        Err(e) => {
            println!("Error: {:?}", e);
            let error_response = e.encode();
            println!("Error response: {:02X?}", error_response);
        }
    }
    
    // Create request to turn OFF coil 10
    let request = WriteSingleCoilRequest::new(10, false);
    let request_bytes = request.encode();
    
    match slave.handle_write_single_coil(&request_bytes) {
        Ok(response) => {
            println!("Response: {:02X?}", response);
        }
        Err(e) => {
            println!("Error: {:?}", e);
        }
    }
    
    Ok(())
}
```

## Summary

**Modbus Function Code 0x05 (Write Single Coil)** provides a simple mechanism for controlling individual discrete outputs in industrial automation systems. Key characteristics include:

- **Purpose:** Write a single binary output (ON/OFF) to a specific coil address
- **Value Convention:** 0xFF00 for ON, 0x0000 for OFF (any other value is invalid)
- **Echo Response:** The slave echoes the request exactly when successful, providing simple acknowledgment
- **Address Range:** 0x0000 to 0xFFFF (65,536 possible coils)
- **Use Cases:** Controlling relays, motors, valves, indicator lights, or any binary actuator

This function code is fundamental for discrete control applications and is commonly paired with Function Code 0x01 (Read Coils) to provide complete read/write access to binary outputs. The echo-based response format makes verification straightforward, ensuring that the master knows exactly which coil was written and what value was set.