# Modbus Function Code 0x17: Read/Write Multiple Registers

## Overview

Function Code 0x17 (23 decimal) is one of Modbus's most powerful operations, enabling **atomic read and write operations in a single transaction**. This function allows a client to simultaneously read from one set of holding registers and write to another set of holding registers on the same device, all within one request-response cycle.

This atomic operation is critical for applications requiring synchronized data exchange, where you need to ensure that both the read and write operations occur together without interruption from other Modbus transactions.

## Key Characteristics

- **Function Code**: 0x17 (23 decimal)
- **Data Type**: Holding Registers (16-bit values)
- **Operation**: Combined read and write in single transaction
- **Atomicity**: Both operations complete together or not at all
- **Use Case**: Synchronized parameter updates, state machines, control loops

## Protocol Structure

### Request Frame

| Field | Size | Description |
|-------|------|-------------|
| Function Code | 1 byte | 0x17 |
| Read Starting Address | 2 bytes | Starting address to read from |
| Read Quantity | 2 bytes | Number of registers to read (1-125) |
| Write Starting Address | 2 bytes | Starting address to write to |
| Write Quantity | 2 bytes | Number of registers to write (1-121) |
| Write Byte Count | 1 byte | Number of bytes to follow (2 × Write Quantity) |
| Write Register Values | N bytes | Register values to write |

### Response Frame

| Field | Size | Description |
|-------|------|-------------|
| Function Code | 1 byte | 0x17 |
| Byte Count | 1 byte | Number of bytes to follow (2 × Read Quantity) |
| Read Register Values | N bytes | Register values read |

## Practical Applications

1. **Control Loop Updates**: Read sensor values while simultaneously updating setpoints
2. **State Machine Operations**: Read current state while writing next state command
3. **Synchronized Configuration**: Read current config while applying new parameters
4. **Transaction Logging**: Read counters while updating control values

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS 0x17
#define MAX_READ_REGISTERS 125
#define MAX_WRITE_REGISTERS 121

// Modbus exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

// Build a Modbus 0x17 request
int modbus_build_read_write_request(
    uint8_t *buffer,
    uint16_t read_addr,
    uint16_t read_count,
    uint16_t write_addr,
    uint16_t write_count,
    const uint16_t *write_values)
{
    if (read_count < 1 || read_count > MAX_READ_REGISTERS ||
        write_count < 1 || write_count > MAX_WRITE_REGISTERS) {
        return -1;
    }

    int offset = 0;
    
    // Function code
    buffer[offset++] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS;
    
    // Read starting address (big-endian)
    buffer[offset++] = (read_addr >> 8) & 0xFF;
    buffer[offset++] = read_addr & 0xFF;
    
    // Read quantity
    buffer[offset++] = (read_count >> 8) & 0xFF;
    buffer[offset++] = read_count & 0xFF;
    
    // Write starting address
    buffer[offset++] = (write_addr >> 8) & 0xFF;
    buffer[offset++] = write_addr & 0xFF;
    
    // Write quantity
    buffer[offset++] = (write_count >> 8) & 0xFF;
    buffer[offset++] = write_count & 0xFF;
    
    // Write byte count
    uint8_t write_byte_count = write_count * 2;
    buffer[offset++] = write_byte_count;
    
    // Write register values
    for (int i = 0; i < write_count; i++) {
        buffer[offset++] = (write_values[i] >> 8) & 0xFF;
        buffer[offset++] = write_values[i] & 0xFF;
    }
    
    return offset;
}

// Parse a Modbus 0x17 response
int modbus_parse_read_write_response(
    const uint8_t *buffer,
    int buffer_len,
    uint16_t *read_values,
    uint16_t expected_read_count)
{
    if (buffer_len < 2) {
        return -1;
    }
    
    // Check for exception response
    if (buffer[0] == (MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80)) {
        printf("Exception: 0x%02X\n", buffer[1]);
        return -buffer[1];
    }
    
    // Verify function code
    if (buffer[0] != MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS) {
        return -1;
    }
    
    uint8_t byte_count = buffer[1];
    uint16_t register_count = byte_count / 2;
    
    if (register_count != expected_read_count) {
        return -1;
    }
    
    if (buffer_len < 2 + byte_count) {
        return -1;
    }
    
    // Extract register values
    for (int i = 0; i < register_count; i++) {
        int offset = 2 + (i * 2);
        read_values[i] = (buffer[offset] << 8) | buffer[offset + 1];
    }
    
    return register_count;
}

// Server-side handler
int modbus_handle_read_write_registers(
    const uint8_t *request,
    int request_len,
    uint8_t *response,
    uint16_t *register_map,
    uint16_t register_map_size)
{
    if (request_len < 10) {
        response[0] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }
    
    uint16_t read_addr = (request[1] << 8) | request[2];
    uint16_t read_count = (request[3] << 8) | request[4];
    uint16_t write_addr = (request[5] << 8) | request[6];
    uint16_t write_count = (request[7] << 8) | request[8];
    uint8_t write_byte_count = request[9];
    
    // Validate parameters
    if (read_count < 1 || read_count > MAX_READ_REGISTERS ||
        write_count < 1 || write_count > MAX_WRITE_REGISTERS ||
        write_byte_count != write_count * 2) {
        response[0] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }
    
    // Check address bounds
    if (read_addr + read_count > register_map_size ||
        write_addr + write_count > register_map_size) {
        response[0] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        return 2;
    }
    
    if (request_len < 10 + write_byte_count) {
        response[0] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }
    
    // ATOMIC OPERATION: Write first, then read
    // Write registers
    for (int i = 0; i < write_count; i++) {
        int offset = 10 + (i * 2);
        uint16_t value = (request[offset] << 8) | request[offset + 1];
        register_map[write_addr + i] = value;
    }
    
    // Build response with read values
    response[0] = MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS;
    response[1] = read_count * 2; // byte count
    
    for (int i = 0; i < read_count; i++) {
        uint16_t value = register_map[read_addr + i];
        response[2 + (i * 2)] = (value >> 8) & 0xFF;
        response[2 + (i * 2) + 1] = value & 0xFF;
    }
    
    return 2 + (read_count * 2);
}

// Example usage
int main() {
    uint8_t request[256];
    uint8_t response[256];
    uint16_t read_values[10];
    uint16_t write_values[] = {0x1234, 0x5678, 0xABCD};
    
    // Build request: read 5 registers from 0x0010, write 3 registers to 0x0020
    int req_len = modbus_build_read_write_request(
        request, 0x0010, 5, 0x0020, 3, write_values);
    
    printf("Request built: %d bytes\n", req_len);
    
    // Simulate server with register map
    uint16_t register_map[256] = {0};
    for (int i = 0; i < 256; i++) {
        register_map[i] = 0x1000 + i;
    }
    
    // Handle request
    int resp_len = modbus_handle_read_write_registers(
        request, req_len, response, register_map, 256);
    
    printf("Response built: %d bytes\n", resp_len);
    
    // Parse response
    int count = modbus_parse_read_write_response(
        response, resp_len, read_values, 5);
    
    if (count > 0) {
        printf("Read %d registers:\n", count);
        for (int i = 0; i < count; i++) {
            printf("  Register[%d] = 0x%04X\n", i, read_values[i]);
        }
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

const MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS: u8 = 0x17;
const MAX_READ_REGISTERS: u16 = 125;
const MAX_WRITE_REGISTERS: u16 = 121;

#[derive(Debug)]
enum ModbusException {
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
}

#[derive(Debug)]
enum ModbusError {
    InvalidLength,
    InvalidParameters,
    Exception(ModbusException),
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusError::InvalidLength => write!(f, "Invalid message length"),
            ModbusError::InvalidParameters => write!(f, "Invalid parameters"),
            ModbusError::Exception(ex) => write!(f, "Modbus exception: {:?}", ex),
        }
    }
}

impl Error for ModbusError {}

/// Build a Modbus Read/Write Multiple Registers request
fn build_read_write_request(
    read_addr: u16,
    read_count: u16,
    write_addr: u16,
    write_values: &[u16],
) -> Result<Vec<u8>, ModbusError> {
    let write_count = write_values.len() as u16;
    
    if read_count < 1 || read_count > MAX_READ_REGISTERS {
        return Err(ModbusError::InvalidParameters);
    }
    
    if write_count < 1 || write_count > MAX_WRITE_REGISTERS {
        return Err(ModbusError::InvalidParameters);
    }
    
    let mut buffer = Vec::new();
    
    // Function code
    buffer.push(MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS);
    
    // Read starting address
    buffer.extend_from_slice(&read_addr.to_be_bytes());
    
    // Read quantity
    buffer.extend_from_slice(&read_count.to_be_bytes());
    
    // Write starting address
    buffer.extend_from_slice(&write_addr.to_be_bytes());
    
    // Write quantity
    buffer.extend_from_slice(&write_count.to_be_bytes());
    
    // Write byte count
    let write_byte_count = (write_count * 2) as u8;
    buffer.push(write_byte_count);
    
    // Write register values
    for &value in write_values {
        buffer.extend_from_slice(&value.to_be_bytes());
    }
    
    Ok(buffer)
}

/// Parse a Modbus Read/Write Multiple Registers response
fn parse_read_write_response(
    buffer: &[u8],
    expected_read_count: u16,
) -> Result<Vec<u16>, ModbusError> {
    if buffer.len() < 2 {
        return Err(ModbusError::InvalidLength);
    }
    
    // Check for exception response
    if buffer[0] == MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80 {
        let exception = match buffer[1] {
            0x01 => ModbusException::IllegalFunction,
            0x02 => ModbusException::IllegalDataAddress,
            0x03 => ModbusException::IllegalDataValue,
            _ => return Err(ModbusError::InvalidParameters),
        };
        return Err(ModbusError::Exception(exception));
    }
    
    // Verify function code
    if buffer[0] != MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS {
        return Err(ModbusError::InvalidParameters);
    }
    
    let byte_count = buffer[1] as usize;
    let register_count = byte_count / 2;
    
    if register_count != expected_read_count as usize {
        return Err(ModbusError::InvalidParameters);
    }
    
    if buffer.len() < 2 + byte_count {
        return Err(ModbusError::InvalidLength);
    }
    
    // Extract register values
    let mut values = Vec::with_capacity(register_count);
    for i in 0..register_count {
        let offset = 2 + (i * 2);
        let value = u16::from_be_bytes([buffer[offset], buffer[offset + 1]]);
        values.push(value);
    }
    
    Ok(values)
}

/// Server-side handler for Read/Write Multiple Registers
fn handle_read_write_registers(
    request: &[u8],
    register_map: &mut [u16],
) -> Result<Vec<u8>, ModbusError> {
    if request.len() < 10 {
        return Ok(vec![
            MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80,
            ModbusException::IllegalDataValue as u8,
        ]);
    }
    
    let read_addr = u16::from_be_bytes([request[1], request[2]]);
    let read_count = u16::from_be_bytes([request[3], request[4]]);
    let write_addr = u16::from_be_bytes([request[5], request[6]]);
    let write_count = u16::from_be_bytes([request[7], request[8]]);
    let write_byte_count = request[9];
    
    // Validate parameters
    if read_count < 1 || read_count > MAX_READ_REGISTERS ||
       write_count < 1 || write_count > MAX_WRITE_REGISTERS ||
       write_byte_count != (write_count * 2) as u8 {
        return Ok(vec![
            MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80,
            ModbusException::IllegalDataValue as u8,
        ]);
    }
    
    let register_map_size = register_map.len() as u16;
    
    // Check address bounds
    if read_addr.saturating_add(read_count) > register_map_size ||
       write_addr.saturating_add(write_count) > register_map_size {
        return Ok(vec![
            MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80,
            ModbusException::IllegalDataAddress as u8,
        ]);
    }
    
    if request.len() < 10 + write_byte_count as usize {
        return Ok(vec![
            MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS | 0x80,
            ModbusException::IllegalDataValue as u8,
        ]);
    }
    
    // ATOMIC OPERATION: Write first, then read
    // Write registers
    for i in 0..write_count {
        let offset = 10 + (i * 2) as usize;
        let value = u16::from_be_bytes([request[offset], request[offset + 1]]);
        register_map[(write_addr + i) as usize] = value;
    }
    
    // Build response with read values
    let mut response = Vec::new();
    response.push(MODBUS_FC_READ_WRITE_MULTIPLE_REGISTERS);
    response.push((read_count * 2) as u8); // byte count
    
    for i in 0..read_count {
        let value = register_map[(read_addr + i) as usize];
        response.extend_from_slice(&value.to_be_bytes());
    }
    
    Ok(response)
}

fn main() -> Result<(), Box<dyn Error>> {
    // Example usage
    let write_values = vec![0x1234_u16, 0x5678, 0xABCD];
    
    // Build request: read 5 registers from 0x0010, write 3 registers to 0x0020
    let request = build_read_write_request(0x0010, 5, 0x0020, &write_values)?;
    
    println!("Request built: {} bytes", request.len());
    println!("Request: {:02X?}", request);
    
    // Simulate server with register map
    let mut register_map: Vec<u16> = (0..256).map(|i| 0x1000 + i).collect();
    
    // Handle request
    let response = handle_read_write_registers(&request, &mut register_map)?;
    
    println!("Response built: {} bytes", response.len());
    println!("Response: {:02X?}", response);
    
    // Parse response
    let read_values = parse_read_write_response(&response, 5)?;
    
    println!("Read {} registers:", read_values.len());
    for (i, &value) in read_values.iter().enumerate() {
        println!("  Register[{}] = 0x{:04X}", i, value);
    }
    
    // Verify written values
    println!("\nVerifying written registers:");
    for (i, &expected) in write_values.iter().enumerate() {
        let actual = register_map[0x0020 + i];
        println!("  Register[0x{:04X}] = 0x{:04X} (expected 0x{:04X})", 
                 0x0020 + i, actual, expected);
    }
    
    Ok(())
}
```

## Summary

**Function Code 0x17 (Read/Write Multiple Registers)** provides atomic read and write operations in a single Modbus transaction. This function is essential when you need to ensure synchronized data exchange—reading sensor data while updating control parameters without any other transaction interrupting between the operations.

**Key points:**
- **Atomic operation**: Both read and write complete together, ensuring data consistency
- **Dual addressing**: Separate addresses for read and write operations (can overlap)
- **Quantity limits**: Up to 125 registers read, up to 121 registers written
- **Order of execution**: Write operation occurs first, followed by the read operation
- **Use cases**: Control loops, state machines, synchronized parameter updates, transaction-safe operations

This function code is particularly valuable in industrial automation where maintaining data integrity across read and write operations is critical for safe and reliable system operation.