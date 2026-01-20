# Modbus Function Code 0x02: Read Discrete Inputs

## Overview

Function Code 0x02 is used to read the status of **discrete inputs** from a Modbus slave device. Discrete inputs are 1-bit, read-only binary values typically used to represent the state of physical sensors, switches, or other digital input devices that the system monitors but cannot control.

## Key Characteristics

- **Function Code**: 0x02
- **Data Type**: 1-bit (boolean) read-only values
- **Typical Use Cases**: Reading sensor states, limit switches, push buttons, alarm conditions, door/window sensors
- **Address Range**: Typically 10001-19999 in Modbus addressing convention (though actual implementation uses 0-based addressing)
- **Key Difference from Coils**: Discrete inputs are read-only, while coils (0x01) are read/write

## Request Format

The request PDU (Protocol Data Unit) consists of:

| Field | Size | Description |
|-------|------|-------------|
| Function Code | 1 byte | 0x02 |
| Starting Address | 2 bytes | Address of first discrete input (0-based) |
| Quantity of Inputs | 2 bytes | Number of inputs to read (1-2000) |

## Response Format

| Field | Size | Description |
|-------|------|-------------|
| Function Code | 1 byte | 0x02 |
| Byte Count | 1 byte | Number of data bytes to follow |
| Input Status | N bytes | Status of inputs (packed bits, LSB first) |

## Bit Packing

Discrete input statuses are packed into bytes with the LSB (Least Significant Bit) of each byte representing the first input in that byte. If the number of inputs isn't a multiple of 8, the remaining bits in the last byte are padded with zeros.

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02
#define MAX_DISCRETE_INPUTS 2000

// Structure for Modbus request
typedef struct {
    uint8_t function_code;
    uint16_t starting_address;
    uint16_t quantity;
} modbus_read_discrete_request_t;

// Structure for Modbus response
typedef struct {
    uint8_t function_code;
    uint8_t byte_count;
    uint8_t input_status[250]; // Max bytes for 2000 inputs
} modbus_read_discrete_response_t;

// Build a Read Discrete Inputs request
int build_read_discrete_inputs_request(uint8_t *buffer, 
                                       uint16_t start_addr, 
                                       uint16_t quantity) {
    if (quantity < 1 || quantity > MAX_DISCRETE_INPUTS) {
        return -1; // Invalid quantity
    }
    
    buffer[0] = MODBUS_FC_READ_DISCRETE_INPUTS;
    buffer[1] = (start_addr >> 8) & 0xFF;  // High byte
    buffer[2] = start_addr & 0xFF;         // Low byte
    buffer[3] = (quantity >> 8) & 0xFF;    // High byte
    buffer[4] = quantity & 0xFF;           // Low byte
    
    return 5; // Return request length
}

// Parse response and extract discrete input states
int parse_read_discrete_inputs_response(const uint8_t *buffer, 
                                        int buffer_len,
                                        bool *inputs, 
                                        int max_inputs) {
    if (buffer_len < 3 || buffer[0] != MODBUS_FC_READ_DISCRETE_INPUTS) {
        return -1; // Invalid response
    }
    
    uint8_t byte_count = buffer[1];
    
    if (buffer_len < 2 + byte_count) {
        return -1; // Incomplete response
    }
    
    int input_count = 0;
    
    // Extract bits from response bytes
    for (int byte_idx = 0; byte_idx < byte_count; byte_idx++) {
        uint8_t data_byte = buffer[2 + byte_idx];
        
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            if (input_count >= max_inputs) {
                break;
            }
            inputs[input_count++] = (data_byte & (1 << bit_idx)) != 0;
        }
    }
    
    return input_count; // Return number of inputs read
}

// Example: Simulate slave response
void simulate_slave_response(uint8_t *response, 
                             uint16_t start_addr, 
                             uint16_t quantity,
                             const bool *input_states) {
    response[0] = MODBUS_FC_READ_DISCRETE_INPUTS;
    
    // Calculate byte count
    uint8_t byte_count = (quantity + 7) / 8;
    response[1] = byte_count;
    
    // Pack input states into bytes
    memset(&response[2], 0, byte_count);
    
    for (uint16_t i = 0; i < quantity; i++) {
        if (input_states[start_addr + i]) {
            response[2 + (i / 8)] |= (1 << (i % 8));
        }
    }
}

// Example usage
int main() {
    uint8_t request[5];
    uint8_t response[260];
    bool inputs[16];
    bool simulated_states[100] = {false};
    
    // Set some example input states
    simulated_states[0] = true;
    simulated_states[2] = true;
    simulated_states[7] = true;
    simulated_states[10] = true;
    
    // Build request to read 16 discrete inputs starting at address 0
    int req_len = build_read_discrete_inputs_request(request, 0, 16);
    
    printf("Request: ");
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Simulate slave response
    simulate_slave_response(response, 0, 16, simulated_states);
    
    printf("Response: ");
    for (int i = 0; i < 2 + response[1]; i++) {
        printf("%02X ", response[i]);
    }
    printf("\n");
    
    // Parse response
    int count = parse_read_discrete_inputs_response(response, 
                                                    2 + response[1], 
                                                    inputs, 16);
    
    printf("\nDiscrete Input States:\n");
    for (int i = 0; i < count; i++) {
        printf("Input %d: %s\n", i, inputs[i] ? "ON" : "OFF");
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

const MODBUS_FC_READ_DISCRETE_INPUTS: u8 = 0x02;
const MAX_DISCRETE_INPUTS: u16 = 2000;

#[derive(Debug)]
pub enum ModbusError {
    InvalidQuantity,
    InvalidResponse,
    BufferTooSmall,
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusError::InvalidQuantity => write!(f, "Invalid quantity of inputs"),
            ModbusError::InvalidResponse => write!(f, "Invalid response format"),
            ModbusError::BufferTooSmall => write!(f, "Buffer too small"),
        }
    }
}

impl Error for ModbusError {}

/// Build a Read Discrete Inputs request
pub fn build_read_discrete_inputs_request(
    start_addr: u16,
    quantity: u16,
) -> Result<Vec<u8>, ModbusError> {
    if quantity < 1 || quantity > MAX_DISCRETE_INPUTS {
        return Err(ModbusError::InvalidQuantity);
    }

    let mut buffer = Vec::with_capacity(5);
    buffer.push(MODBUS_FC_READ_DISCRETE_INPUTS);
    buffer.extend_from_slice(&start_addr.to_be_bytes());
    buffer.extend_from_slice(&quantity.to_be_bytes());

    Ok(buffer)
}

/// Parse Read Discrete Inputs response
pub fn parse_read_discrete_inputs_response(
    buffer: &[u8],
) -> Result<Vec<bool>, ModbusError> {
    if buffer.len() < 3 || buffer[0] != MODBUS_FC_READ_DISCRETE_INPUTS {
        return Err(ModbusError::InvalidResponse);
    }

    let byte_count = buffer[1] as usize;

    if buffer.len() < 2 + byte_count {
        return Err(ModbusError::BufferTooSmall);
    }

    let mut inputs = Vec::new();

    // Extract bits from response bytes
    for byte_idx in 0..byte_count {
        let data_byte = buffer[2 + byte_idx];

        for bit_idx in 0..8 {
            inputs.push((data_byte & (1 << bit_idx)) != 0);
        }
    }

    Ok(inputs)
}

/// Simulate a slave response for testing
pub fn simulate_slave_response(
    start_addr: u16,
    quantity: u16,
    input_states: &[bool],
) -> Vec<u8> {
    let byte_count = ((quantity + 7) / 8) as usize;
    let mut response = vec![0u8; 2 + byte_count];

    response[0] = MODBUS_FC_READ_DISCRETE_INPUTS;
    response[1] = byte_count as u8;

    // Pack input states into bytes
    for i in 0..quantity as usize {
        let addr = start_addr as usize + i;
        if addr < input_states.len() && input_states[addr] {
            response[2 + (i / 8)] |= 1 << (i % 8);
        }
    }

    response
}

fn main() -> Result<(), Box<dyn Error>> {
    // Create simulated input states
    let mut simulated_states = vec![false; 100];
    simulated_states[0] = true;
    simulated_states[2] = true;
    simulated_states[7] = true;
    simulated_states[10] = true;

    // Build request to read 16 discrete inputs starting at address 0
    let request = build_read_discrete_inputs_request(0, 16)?;

    println!("Request: {:02X?}", request);

    // Simulate slave response
    let response = simulate_slave_response(0, 16, &simulated_states);

    println!("Response: {:02X?}", response);

    // Parse response
    let inputs = parse_read_discrete_inputs_response(&response)?;

    println!("\nDiscrete Input States:");
    for (i, &state) in inputs.iter().enumerate().take(16) {
        println!("Input {}: {}", i, if state { "ON" } else { "OFF" });
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_building() {
        let request = build_read_discrete_inputs_request(100, 20).unwrap();
        assert_eq!(request[0], 0x02);
        assert_eq!(u16::from_be_bytes([request[1], request[2]]), 100);
        assert_eq!(u16::from_be_bytes([request[3], request[4]]), 20);
    }

    #[test]
    fn test_response_parsing() {
        let response = vec![0x02, 0x03, 0b10100101, 0b00001111, 0b00000001];
        let inputs = parse_read_discrete_inputs_response(&response).unwrap();
        
        assert_eq!(inputs[0], true);  // Bit 0 of byte 1
        assert_eq!(inputs[2], true);  // Bit 2 of byte 1
        assert_eq!(inputs[5], true);  // Bit 5 of byte 1
    }
}
```

---

## Summary

**Modbus Function Code 0x02 (Read Discrete Inputs)** is essential for monitoring read-only digital input devices in industrial automation systems. Unlike coils, discrete inputs cannot be written to—they're strictly for reading sensor and switch states.

**Key Points:**
- Used for reading 1-bit read-only values (sensors, switches, alarms)
- Can read 1-2000 discrete inputs in a single request
- Data is bit-packed with LSB-first ordering within each byte
- Request format: Function code + Starting address + Quantity
- Response format: Function code + Byte count + Packed input status bits

**Implementation Considerations:**
- Always validate quantity range (1-2000)
- Handle bit packing/unpacking carefully (LSB first)
- Account for padding bits in the last byte
- Implement proper error handling for malformed responses
- Use appropriate data types (uint8_t, u8) for byte-level operations

The C/C++ and Rust examples demonstrate complete request building, response parsing, and bit manipulation for this function code, providing a foundation for implementing Modbus communication in industrial control applications.