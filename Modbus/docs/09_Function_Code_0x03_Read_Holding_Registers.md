# Function Code 0x03: Read Holding Registers

## Overview

Function Code 0x03 is one of the most commonly used Modbus functions. It reads the contents of contiguous 16-bit holding registers from a Modbus slave device. Holding registers are read/write registers typically used for configuration parameters, setpoints, and data storage that can be both read and modified by the master.

## Protocol Details

**Request Structure (from Master):**
- **Function Code**: 0x03 (1 byte)
- **Starting Address**: Address of the first register to read (2 bytes, big-endian)
- **Quantity of Registers**: Number of consecutive registers to read (2 bytes, big-endian)
- **CRC/LRC**: Error checking (2 bytes for RTU, 1 byte for ASCII)

**Response Structure (from Slave):**
- **Function Code**: 0x03 (1 byte)
- **Byte Count**: Number of data bytes to follow (1 byte) = Quantity × 2
- **Register Values**: The actual register data (2 bytes per register, big-endian)
- **CRC/LRC**: Error checking

**Valid Range:**
- Starting address: 0x0000 to 0xFFFF
- Quantity of registers: 1 to 125 (0x01 to 0x7D)

## Use Cases

- Reading configuration parameters (motor speed setpoints, temperature thresholds)
- Retrieving process variables (pressure values, flow rates)
- Accessing device status information stored in holding registers
- Polling multiple related parameters in a single transaction

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// Calculate Modbus RTU CRC16
uint16_t modbus_crc16(uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Build Modbus Read Holding Registers request (0x03)
int modbus_build_read_holding_request(uint8_t slave_id, 
                                       uint16_t start_addr, 
                                       uint16_t num_registers,
                                       uint8_t *request_buffer) {
    request_buffer[0] = slave_id;
    request_buffer[1] = 0x03;  // Function code
    request_buffer[2] = (start_addr >> 8) & 0xFF;  // Start address high byte
    request_buffer[3] = start_addr & 0xFF;         // Start address low byte
    request_buffer[4] = (num_registers >> 8) & 0xFF;  // Quantity high byte
    request_buffer[5] = num_registers & 0xFF;         // Quantity low byte
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(request_buffer, 6);
    request_buffer[6] = crc & 0xFF;        // CRC low byte
    request_buffer[7] = (crc >> 8) & 0xFF; // CRC high byte
    
    return 8;  // Total request length
}

// Parse Modbus Read Holding Registers response
int modbus_parse_read_holding_response(uint8_t *response_buffer, 
                                        uint16_t response_length,
                                        uint16_t *register_values,
                                        uint16_t expected_count) {
    // Minimum response: Slave ID + Function + Byte Count + CRC (5 bytes)
    if (response_length < 5) {
        return -1;  // Response too short
    }
    
    uint8_t function_code = response_buffer[1];
    
    // Check for exception response
    if (function_code & 0x80) {
        return -2;  // Exception occurred
    }
    
    if (function_code != 0x03) {
        return -3;  // Wrong function code
    }
    
    uint8_t byte_count = response_buffer[2];
    
    // Verify byte count matches expected registers
    if (byte_count != expected_count * 2) {
        return -4;  // Byte count mismatch
    }
    
    // Verify CRC
    uint16_t received_crc = response_buffer[response_length - 2] | 
                           (response_buffer[response_length - 1] << 8);
    uint16_t calculated_crc = modbus_crc16(response_buffer, response_length - 2);
    
    if (received_crc != calculated_crc) {
        return -5;  // CRC error
    }
    
    // Extract register values (big-endian)
    for (uint16_t i = 0; i < expected_count; i++) {
        uint16_t offset = 3 + (i * 2);
        register_values[i] = (response_buffer[offset] << 8) | 
                            response_buffer[offset + 1];
    }
    
    return expected_count;  // Number of registers read
}

// Example usage
void example_usage() {
    uint8_t request[8];
    uint8_t response[256];
    uint16_t registers[10];
    
    // Build request to read 10 registers starting at address 100 from slave 1
    int req_len = modbus_build_read_holding_request(1, 100, 10, request);
    
    // Send request and receive response (platform-specific code omitted)
    // int resp_len = send_and_receive(request, req_len, response);
    
    // Parse the response
    // int result = modbus_parse_read_holding_response(response, resp_len, registers, 10);
}
```

## Rust Implementation

```rust
/// Calculate Modbus RTU CRC16
fn modbus_crc16(buffer: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in buffer {
        crc ^= *byte as u16;
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

/// Build a Modbus Read Holding Registers request (Function Code 0x03)
fn modbus_build_read_holding_request(
    slave_id: u8,
    start_addr: u16,
    num_registers: u16,
) -> Result<Vec<u8>, &'static str> {
    // Validate number of registers
    if num_registers == 0 || num_registers > 125 {
        return Err("Number of registers must be between 1 and 125");
    }
    
    let mut request = Vec::with_capacity(8);
    
    request.push(slave_id);
    request.push(0x03); // Function code
    request.extend_from_slice(&start_addr.to_be_bytes());
    request.extend_from_slice(&num_registers.to_be_bytes());
    
    // Calculate and append CRC
    let crc = modbus_crc16(&request);
    request.extend_from_slice(&crc.to_le_bytes()); // CRC is little-endian
    
    Ok(request)
}

/// Parse a Modbus Read Holding Registers response
fn modbus_parse_read_holding_response(
    response: &[u8],
    expected_count: u16,
) -> Result<Vec<u16>, &'static str> {
    // Minimum response length check
    if response.len() < 5 {
        return Err("Response too short");
    }
    
    let function_code = response[1];
    
    // Check for exception response
    if function_code & 0x80 != 0 {
        let exception_code = response[2];
        return Err("Modbus exception received");
    }
    
    if function_code != 0x03 {
        return Err("Wrong function code in response");
    }
    
    let byte_count = response[2] as u16;
    
    // Verify byte count
    if byte_count != expected_count * 2 {
        return Err("Byte count mismatch");
    }
    
    // Verify CRC
    let data_len = response.len() - 2;
    let received_crc = u16::from_le_bytes([response[data_len], response[data_len + 1]]);
    let calculated_crc = modbus_crc16(&response[..data_len]);
    
    if received_crc != calculated_crc {
        return Err("CRC validation failed");
    }
    
    // Extract register values (big-endian)
    let mut registers = Vec::with_capacity(expected_count as usize);
    for i in 0..expected_count as usize {
        let offset = 3 + (i * 2);
        let value = u16::from_be_bytes([response[offset], response[offset + 1]]);
        registers.push(value);
    }
    
    Ok(registers)
}

// Example usage with error handling
fn example_usage() -> Result<(), Box<dyn std::error::Error>> {
    // Build request to read 10 registers starting at address 100 from slave 1
    let request = modbus_build_read_holding_request(1, 100, 10)?;
    
    println!("Request: {:02X?}", request);
    
    // Simulate a response (in real code, this would come from serial/TCP)
    let mut response = vec![
        0x01, // Slave ID
        0x03, // Function code
        0x14, // Byte count (20 bytes = 10 registers)
    ];
    
    // Add 10 dummy register values
    for i in 0..10u16 {
        response.extend_from_slice(&(1000 + i).to_be_bytes());
    }
    
    // Add CRC
    let crc = modbus_crc16(&response);
    response.extend_from_slice(&crc.to_le_bytes());
    
    // Parse the response
    let registers = modbus_parse_read_holding_response(&response, 10)?;
    
    println!("Registers read: {:?}", registers);
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_read_holding_request() {
        let request = modbus_build_read_holding_request(1, 0x0000, 2).unwrap();
        // Expected: [0x01, 0x03, 0x00, 0x00, 0x00, 0x02, CRC_L, CRC_H]
        assert_eq!(request[0], 0x01);
        assert_eq!(request[1], 0x03);
        assert_eq!(request.len(), 8);
    }
    
    #[test]
    fn test_invalid_register_count() {
        let result = modbus_build_read_holding_request(1, 0, 0);
        assert!(result.is_err());
        
        let result = modbus_build_read_holding_request(1, 0, 126);
        assert!(result.is_err());
    }
}
```

## Summary

Function Code 0x03 (Read Holding Registers) is essential for retrieving configuration data and process values from Modbus devices. It reads contiguous blocks of 16-bit registers (1-125 at a time) using a simple request/response pattern. The master specifies the starting address and quantity, while the slave responds with the register values in big-endian format. Proper implementation requires validation of register counts, CRC verification for data integrity, and handling of exception responses. Both the C/C++ and Rust implementations demonstrate complete request building and response parsing with error handling, making them suitable for industrial automation, SCADA systems, and IoT applications where reliable data acquisition is critical.