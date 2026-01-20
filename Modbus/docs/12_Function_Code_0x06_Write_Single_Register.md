# Modbus Function Code 0x06: Write Single Register

## Overview

Function Code 0x06 is a fundamental Modbus command that writes a single 16-bit value to a holding register at a specified address. This is one of the most commonly used Modbus functions for updating individual configuration parameters, setpoints, or control values in real-time systems.

## Protocol Details

### Request Format
- **Function Code**: 0x06 (1 byte)
- **Register Address**: 16-bit address (2 bytes)
- **Register Value**: 16-bit value to write (2 bytes)

### Response Format
The response echoes the request if successful:
- **Function Code**: 0x06 (1 byte)
- **Register Address**: Same as request (2 bytes)
- **Register Value**: Value that was written (2 bytes)

### Data Flow
```
Master → Slave: [Device ID][0x06][Address Hi][Address Lo][Value Hi][Value Lo][CRC]
Slave → Master: [Device ID][0x06][Address Hi][Address Lo][Value Hi][Value Lo][CRC]
```

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// CRC-16 calculation for Modbus RTU
uint16_t modbus_crc16(const uint8_t *buffer, uint16_t length) {
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

// Build a Write Single Register request (0x06)
int modbus_build_write_single_register(
    uint8_t *buffer,
    uint8_t device_id,
    uint16_t register_address,
    uint16_t value
) {
    buffer[0] = device_id;
    buffer[1] = 0x06;  // Function code
    buffer[2] = (register_address >> 8) & 0xFF;  // Address Hi
    buffer[3] = register_address & 0xFF;         // Address Lo
    buffer[4] = (value >> 8) & 0xFF;             // Value Hi
    buffer[5] = value & 0xFF;                    // Value Lo
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(buffer, 6);
    buffer[6] = crc & 0xFF;         // CRC Lo
    buffer[7] = (crc >> 8) & 0xFF;  // CRC Hi
    
    return 8;  // Total message length
}

// Parse Write Single Register response
int modbus_parse_write_single_register_response(
    const uint8_t *response,
    uint8_t expected_device_id,
    uint16_t expected_address,
    uint16_t expected_value
) {
    // Verify CRC
    uint16_t received_crc = response[6] | (response[7] << 8);
    uint16_t calculated_crc = modbus_crc16(response, 6);
    
    if (received_crc != calculated_crc) {
        return -1;  // CRC error
    }
    
    // Check device ID and function code
    if (response[0] != expected_device_id || response[1] != 0x06) {
        return -2;  // Invalid response
    }
    
    // Extract address and value
    uint16_t response_address = (response[2] << 8) | response[3];
    uint16_t response_value = (response[4] << 8) | response[5];
    
    // Verify echo
    if (response_address != expected_address || response_value != expected_value) {
        return -3;  // Mismatch
    }
    
    return 0;  // Success
}

// Example usage
void example_write_register() {
    uint8_t request[8];
    uint8_t response[8];
    
    // Write value 1234 to register 100 on device 1
    int length = modbus_build_write_single_register(request, 1, 100, 1234);
    
    // Send request and receive response (implementation specific)
    // send_modbus(request, length);
    // receive_modbus(response, sizeof(response));
    
    // Parse response
    int result = modbus_parse_write_single_register_response(response, 1, 100, 1234);
    if (result == 0) {
        // Write successful
    }
}
```

## C++ Implementation (Object-Oriented)

```cpp
#include <vector>
#include <cstdint>
#include <stdexcept>

class ModbusWriteSingleRegister {
private:
    uint16_t calculate_crc(const std::vector<uint8_t>& data) {
        uint16_t crc = 0xFFFF;
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; i++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

public:
    std::vector<uint8_t> build_request(
        uint8_t device_id,
        uint16_t register_address,
        uint16_t value
    ) {
        std::vector<uint8_t> request(8);
        
        request[0] = device_id;
        request[1] = 0x06;
        request[2] = static_cast<uint8_t>(register_address >> 8);
        request[3] = static_cast<uint8_t>(register_address & 0xFF);
        request[4] = static_cast<uint8_t>(value >> 8);
        request[5] = static_cast<uint8_t>(value & 0xFF);
        
        std::vector<uint8_t> data(request.begin(), request.begin() + 6);
        uint16_t crc = calculate_crc(data);
        
        request[6] = static_cast<uint8_t>(crc & 0xFF);
        request[7] = static_cast<uint8_t>(crc >> 8);
        
        return request;
    }
    
    void validate_response(
        const std::vector<uint8_t>& response,
        uint8_t expected_device_id,
        uint16_t expected_address,
        uint16_t expected_value
    ) {
        if (response.size() != 8) {
            throw std::runtime_error("Invalid response length");
        }
        
        // Verify CRC
        std::vector<uint8_t> data(response.begin(), response.begin() + 6);
        uint16_t calculated_crc = calculate_crc(data);
        uint16_t received_crc = response[6] | (response[7] << 8);
        
        if (calculated_crc != received_crc) {
            throw std::runtime_error("CRC mismatch");
        }
        
        // Verify echo
        if (response[0] != expected_device_id) {
            throw std::runtime_error("Device ID mismatch");
        }
        
        if (response[1] != 0x06) {
            throw std::runtime_error("Function code mismatch");
        }
        
        uint16_t address = (response[2] << 8) | response[3];
        uint16_t value = (response[4] << 8) | response[5];
        
        if (address != expected_address || value != expected_value) {
            throw std::runtime_error("Register address or value mismatch");
        }
    }
};
```

## Rust Implementation

```rust
// CRC-16 Modbus calculation
fn modbus_crc16(data: &[u8]) -> u16 {
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

// Build Write Single Register request
pub fn build_write_single_register(
    device_id: u8,
    register_address: u16,
    value: u16,
) -> Vec<u8> {
    let mut request = Vec::with_capacity(8);
    
    request.push(device_id);
    request.push(0x06); // Function code
    request.push((register_address >> 8) as u8);
    request.push((register_address & 0xFF) as u8);
    request.push((value >> 8) as u8);
    request.push((value & 0xFF) as u8);
    
    // Calculate CRC
    let crc = modbus_crc16(&request);
    request.push((crc & 0xFF) as u8);
    request.push((crc >> 8) as u8);
    
    request
}

// Parse and validate response
pub fn parse_write_single_register_response(
    response: &[u8],
    expected_device_id: u8,
    expected_address: u16,
    expected_value: u16,
) -> Result<(), String> {
    if response.len() != 8 {
        return Err("Invalid response length".to_string());
    }
    
    // Verify CRC
    let received_crc = response[6] as u16 | ((response[7] as u16) << 8);
    let calculated_crc = modbus_crc16(&response[..6]);
    
    if received_crc != calculated_crc {
        return Err("CRC mismatch".to_string());
    }
    
    // Verify device ID
    if response[0] != expected_device_id {
        return Err("Device ID mismatch".to_string());
    }
    
    // Verify function code
    if response[1] != 0x06 {
        return Err(format!("Unexpected function code: 0x{:02X}", response[1]));
    }
    
    // Extract and verify address
    let address = ((response[2] as u16) << 8) | (response[3] as u16);
    if address != expected_address {
        return Err(format!(
            "Address mismatch: expected {}, got {}",
            expected_address, address
        ));
    }
    
    // Extract and verify value
    let value = ((response[4] as u16) << 8) | (response[5] as u16);
    if value != expected_value {
        return Err(format!(
            "Value mismatch: expected {}, got {}",
            expected_value, value
        ));
    }
    
    Ok(())
}

// Example usage with error handling
pub fn example_write_register() -> Result<(), String> {
    let device_id = 1;
    let register_address = 100;
    let value = 1234;
    
    // Build request
    let request = build_write_single_register(device_id, register_address, value);
    
    println!("Request: {:02X?}", request);
    
    // Simulate response (in real code, this would come from the device)
    let response = request.clone(); // Echo for demonstration
    
    // Parse response
    parse_write_single_register_response(
        &response,
        device_id,
        register_address,
        value,
    )?;
    
    println!("Write successful: Register {} = {}", register_address, value);
    
    Ok(())
}
```

## Summary

**Function Code 0x06 (Write Single Register)** is a simple yet critical Modbus operation that updates a single 16-bit holding register. The request contains the target register address and the value to write, and the slave device echoes the complete request back as confirmation of success.

**Key characteristics:**
- **Simple operation**: Only writes one register at a time
- **Echo response**: Successful response mirrors the request exactly
- **Atomic operation**: The write is immediate and complete
- **Common use cases**: Setting setpoints, configuration parameters, control flags, or operational modes

**Implementation considerations:**
- Always verify CRC on both request and response
- Validate that the response echoes the expected address and value
- Handle exception responses (function code 0x86) for errors like invalid address or read-only registers
- Consider using Function Code 0x10 (Write Multiple Registers) when updating several consecutive registers for efficiency

The implementations above demonstrate building requests, calculating CRC checksums, parsing responses, and validating the echo confirmation across C, C++, and Rust, providing robust error handling and type safety appropriate to each language.