# Modbus Function Code 0x10: Write Multiple Registers

## Overview

Function Code 0x10 (decimal 16) is a Modbus function that allows a client to write multiple consecutive holding registers in a single request. This is significantly more efficient than using Function Code 0x06 (Write Single Register) multiple times, as it reduces network overhead and ensures atomic updates of related register values.

## Use Cases

- Updating configuration parameters that span multiple registers
- Writing setpoints, recipes, or complex data structures
- Batch updating analog outputs or control values
- Uploading firmware data or large datasets
- Ensuring consistency when multiple related registers must change together

## Protocol Structure

### Request Format (PDU)

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| Function Code | 1 | 0x10 (16) |
| Starting Address | 2 | Address of first register (0x0000 - 0xFFFF) |
| Quantity of Registers | 2 | Number of registers to write (1-123) |
| Byte Count | 1 | Number of data bytes to follow (2 × quantity) |
| Register Values | N × 2 | Data to write (2 bytes per register, big-endian) |

### Response Format (PDU)

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| Function Code | 1 | 0x10 (16) |
| Starting Address | 2 | Echo of request starting address |
| Quantity of Registers | 2 | Echo of quantity written |

### Exception Response

| Field | Size (bytes) | Description |
|-------|--------------|-------------|
| Function Code | 1 | 0x90 (0x10 + 0x80) |
| Exception Code | 1 | Error code (1-4) |

**Common Exception Codes:**
- 0x01: Illegal Function
- 0x02: Illegal Data Address
- 0x03: Illegal Data Value
- 0x04: Slave Device Failure

## Constraints

- Maximum registers per request: **123 registers** (246 bytes of data)
- Registers must be consecutive
- All registers are written atomically (all or nothing)
- Register addresses are 0-based (0x0000 - 0xFFFF)

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h> // for htons/ntohs

#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10
#define MODBUS_MAX_WRITE_REGISTERS 123
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_VALUE 0x03

// Function to build Write Multiple Registers request
int modbus_build_write_multiple_registers_request(
    uint8_t *buffer,
    uint16_t start_address,
    uint16_t quantity,
    const uint16_t *values)
{
    if (quantity < 1 || quantity > MODBUS_MAX_WRITE_REGISTERS) {
        return -1; // Invalid quantity
    }
    
    int index = 0;
    buffer[index++] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    
    // Starting address (big-endian)
    buffer[index++] = (start_address >> 8) & 0xFF;
    buffer[index++] = start_address & 0xFF;
    
    // Quantity of registers (big-endian)
    buffer[index++] = (quantity >> 8) & 0xFF;
    buffer[index++] = quantity & 0xFF;
    
    // Byte count
    uint8_t byte_count = quantity * 2;
    buffer[index++] = byte_count;
    
    // Register values (big-endian)
    for (uint16_t i = 0; i < quantity; i++) {
        buffer[index++] = (values[i] >> 8) & 0xFF;
        buffer[index++] = values[i] & 0xFF;
    }
    
    return index; // Return total length
}

// Function to parse Write Multiple Registers response
int modbus_parse_write_multiple_registers_response(
    const uint8_t *buffer,
    int length,
    uint16_t *start_address,
    uint16_t *quantity)
{
    if (length < 5) {
        return -1; // Too short
    }
    
    // Check for exception
    if (buffer[0] == (MODBUS_FC_WRITE_MULTIPLE_REGISTERS | 0x80)) {
        return -(buffer[1]); // Return negative exception code
    }
    
    if (buffer[0] != MODBUS_FC_WRITE_MULTIPLE_REGISTERS) {
        return -1; // Wrong function code
    }
    
    // Parse starting address
    *start_address = (buffer[1] << 8) | buffer[2];
    
    // Parse quantity
    *quantity = (buffer[3] << 8) | buffer[4];
    
    return 0; // Success
}

// Server-side handler
int modbus_handle_write_multiple_registers(
    const uint8_t *request,
    int request_len,
    uint8_t *response,
    uint16_t *register_map,
    uint16_t register_count)
{
    if (request_len < 7) {
        // Build exception response
        response[0] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_VALUE;
        return 2;
    }
    
    uint16_t start_addr = (request[1] << 8) | request[2];
    uint16_t quantity = (request[3] << 8) | request[4];
    uint8_t byte_count = request[5];
    
    // Validate
    if (quantity < 1 || quantity > MODBUS_MAX_WRITE_REGISTERS ||
        byte_count != quantity * 2 ||
        request_len < (6 + byte_count)) {
        response[0] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_VALUE;
        return 2;
    }
    
    // Check address range
    if (start_addr + quantity > register_count) {
        response[0] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_ADDRESS;
        return 2;
    }
    
    // Write registers
    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t value = (request[6 + i*2] << 8) | request[7 + i*2];
        register_map[start_addr + i] = value;
    }
    
    // Build success response
    response[0] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    response[1] = request[1]; // Echo start address
    response[2] = request[2];
    response[3] = request[3]; // Echo quantity
    response[4] = request[4];
    
    return 5;
}

// Example usage
void example_usage() {
    uint8_t request[256];
    uint8_t response[256];
    uint16_t values[10] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
    
    // Build request to write 10 registers starting at address 1000
    int req_len = modbus_build_write_multiple_registers_request(
        request, 1000, 10, values);
    
    // Simulate server response parsing
    uint16_t start_addr, quantity;
    int result = modbus_parse_write_multiple_registers_response(
        response, 5, &start_addr, &quantity);
    
    if (result == 0) {
        printf("Successfully wrote %d registers at address %d\n", 
               quantity, start_addr);
    }
}
```

## Rust Implementation

```rust
use std::io::{self, Cursor, Read, Write};
use byteorder::{BigEndian, ReadBytesExt, WriteBytesExt};

const FC_WRITE_MULTIPLE_REGISTERS: u8 = 0x10;
const MAX_WRITE_REGISTERS: u16 = 123;

#[derive(Debug, Clone)]
pub enum ModbusError {
    InvalidQuantity,
    InvalidAddress,
    BufferTooSmall,
    IllegalFunction,
    IllegalDataAddress,
    IllegalDataValue,
    SlaveDeviceFailure,
    ParseError,
}

#[derive(Debug, Clone)]
pub struct WriteMultipleRegistersRequest {
    pub start_address: u16,
    pub values: Vec<u16>,
}

impl WriteMultipleRegistersRequest {
    pub fn new(start_address: u16, values: Vec<u16>) -> Result<Self, ModbusError> {
        if values.is_empty() || values.len() > MAX_WRITE_REGISTERS as usize {
            return Err(ModbusError::InvalidQuantity);
        }
        
        Ok(Self {
            start_address,
            values,
        })
    }
    
    pub fn encode(&self) -> Result<Vec<u8>, ModbusError> {
        let mut buffer = Vec::new();
        
        buffer.write_u8(FC_WRITE_MULTIPLE_REGISTERS)?;
        buffer.write_u16::<BigEndian>(self.start_address)?;
        buffer.write_u16::<BigEndian>(self.values.len() as u16)?;
        buffer.write_u8((self.values.len() * 2) as u8)?;
        
        for &value in &self.values {
            buffer.write_u16::<BigEndian>(value)?;
        }
        
        Ok(buffer)
    }
    
    pub fn decode(data: &[u8]) -> Result<Self, ModbusError> {
        if data.len() < 7 {
            return Err(ModbusError::ParseError);
        }
        
        let mut cursor = Cursor::new(data);
        let fc = cursor.read_u8()?;
        
        if fc != FC_WRITE_MULTIPLE_REGISTERS {
            return Err(ModbusError::IllegalFunction);
        }
        
        let start_address = cursor.read_u16::<BigEndian>()?;
        let quantity = cursor.read_u16::<BigEndian>()?;
        let byte_count = cursor.read_u8()?;
        
        if byte_count != (quantity * 2) as u8 {
            return Err(ModbusError::IllegalDataValue);
        }
        
        let mut values = Vec::with_capacity(quantity as usize);
        for _ in 0..quantity {
            values.push(cursor.read_u16::<BigEndian>()?);
        }
        
        Ok(Self {
            start_address,
            values,
        })
    }
}

#[derive(Debug, Clone)]
pub struct WriteMultipleRegistersResponse {
    pub start_address: u16,
    pub quantity: u16,
}

impl WriteMultipleRegistersResponse {
    pub fn encode(&self) -> Result<Vec<u8>, ModbusError> {
        let mut buffer = Vec::new();
        
        buffer.write_u8(FC_WRITE_MULTIPLE_REGISTERS)?;
        buffer.write_u16::<BigEndian>(self.start_address)?;
        buffer.write_u16::<BigEndian>(self.quantity)?;
        
        Ok(buffer)
    }
    
    pub fn decode(data: &[u8]) -> Result<Self, ModbusError> {
        if data.len() < 5 {
            return Err(ModbusError::ParseError);
        }
        
        let mut cursor = Cursor::new(data);
        let fc = cursor.read_u8()?;
        
        // Check for exception
        if fc == (FC_WRITE_MULTIPLE_REGISTERS | 0x80) {
            let exception_code = cursor.read_u8()?;
            return Err(match exception_code {
                0x01 => ModbusError::IllegalFunction,
                0x02 => ModbusError::IllegalDataAddress,
                0x03 => ModbusError::IllegalDataValue,
                0x04 => ModbusError::SlaveDeviceFailure,
                _ => ModbusError::ParseError,
            });
        }
        
        if fc != FC_WRITE_MULTIPLE_REGISTERS {
            return Err(ModbusError::IllegalFunction);
        }
        
        let start_address = cursor.read_u16::<BigEndian>()?;
        let quantity = cursor.read_u16::<BigEndian>()?;
        
        Ok(Self {
            start_address,
            quantity,
        })
    }
}

// Server-side handler
pub struct ModbusServer {
    registers: Vec<u16>,
}

impl ModbusServer {
    pub fn new(register_count: usize) -> Self {
        Self {
            registers: vec![0; register_count],
        }
    }
    
    pub fn handle_write_multiple_registers(
        &mut self,
        request: &WriteMultipleRegistersRequest,
    ) -> Result<WriteMultipleRegistersResponse, ModbusError> {
        let end_address = request.start_address as usize + request.values.len();
        
        if end_address > self.registers.len() {
            return Err(ModbusError::IllegalDataAddress);
        }
        
        // Write registers atomically
        for (i, &value) in request.values.iter().enumerate() {
            self.registers[request.start_address as usize + i] = value;
        }
        
        Ok(WriteMultipleRegistersResponse {
            start_address: request.start_address,
            quantity: request.values.len() as u16,
        })
    }
}

// Convert io::Error to ModbusError
impl From<io::Error> for ModbusError {
    fn from(_: io::Error) -> Self {
        ModbusError::ParseError
    }
}

// Example usage
fn main() -> Result<(), ModbusError> {
    // Client side: create request
    let values = vec![100, 200, 300, 400, 500];
    let request = WriteMultipleRegistersRequest::new(1000, values)?;
    let request_bytes = request.encode()?;
    
    println!("Request: {:02X?}", request_bytes);
    
    // Server side: handle request
    let mut server = ModbusServer::new(2000);
    let response = server.handle_write_multiple_registers(&request)?;
    let response_bytes = response.encode()?;
    
    println!("Response: {:02X?}", response_bytes);
    println!("Wrote {} registers starting at address {}", 
             response.quantity, response.start_address);
    
    // Client side: parse response
    let parsed_response = WriteMultipleRegistersResponse::decode(&response_bytes)?;
    println!("Confirmed write of {} registers", parsed_response.quantity);
    
    Ok(())
}
```

## Summary

**Modbus Function Code 0x10 (Write Multiple Registers)** is an essential function for efficient batch writing of consecutive holding registers. It reduces network traffic, ensures atomic updates of related data, and is critical for applications requiring consistent multi-register updates.

**Key Points:**
- Writes 1-123 consecutive holding registers in one transaction
- More efficient than multiple single-register writes
- Atomic operation ensures data consistency
- Request includes function code, start address, quantity, byte count, and register values
- Response echoes the start address and quantity for confirmation
- Common in industrial automation for configuration, setpoint management, and data uploads
- Both implementations demonstrate proper encoding/decoding with big-endian byte order and comprehensive error handling