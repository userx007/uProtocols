# Custom Function Codes in Modbus (0x41-0x48, 0x64-0x6E)

## Overview

Modbus reserves specific function code ranges for custom, vendor-specific implementations. While standard Modbus defines function codes 0x01-0x18 and some diagnostic codes, the protocol explicitly allows manufacturers to implement proprietary extensions in designated ranges. This enables vendors to add specialized functionality while maintaining backward compatibility with standard Modbus operations.

## Function Code Ranges

The Modbus specification designates two primary ranges for custom implementations:

**User-Defined Function Codes:**
- **0x41-0x48** (65-72): Available for user-defined functions
- **0x64-0x6E** (100-110): Reserved for user-defined functions

These ranges allow vendors to implement proprietary features such as:
- Custom diagnostic operations
- Device-specific configuration
- Specialized data access patterns
- Enhanced security features
- Bulk operations not covered by standard codes

## Protocol Structure

Custom function codes follow the standard Modbus frame structure:

**Request Frame:**
- Device Address (1 byte)
- Function Code (1 byte) - 0x41-0x48 or 0x64-0x6E
- Custom Data (variable length)
- CRC/LRC (2 bytes for RTU, 1 byte for ASCII)

**Response Frame:**
- Device Address (1 byte)
- Function Code (1 byte) - echoes request
- Custom Response Data (variable length)
- CRC/LRC

**Error Response:**
- Device Address (1 byte)
- Function Code + 0x80 (error flag)
- Exception Code (1 byte)
- CRC/LRC

## C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// Custom function codes
#define MODBUS_FC_CUSTOM_READ_CONFIG      0x41
#define MODBUS_FC_CUSTOM_WRITE_CONFIG     0x42
#define MODBUS_FC_CUSTOM_BULK_READ        0x65
#define MODBUS_FC_CUSTOM_DEVICE_INFO      0x66

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION     0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE   0x03

// Custom data structure for device configuration
typedef struct {
    uint16_t config_id;
    uint16_t value;
    uint8_t  flags;
} custom_config_t;

// CRC16 calculation for Modbus RTU
uint16_t modbus_crc16(const uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Custom function: Read device configuration
int modbus_custom_read_config(uint8_t slave_id, uint16_t config_id, 
                               uint8_t *response, uint16_t *response_len) {
    uint8_t request[8];
    uint16_t crc;
    
    // Build request frame
    request[0] = slave_id;
    request[1] = MODBUS_FC_CUSTOM_READ_CONFIG;
    request[2] = (config_id >> 8) & 0xFF;  // Config ID high byte
    request[3] = config_id & 0xFF;          // Config ID low byte
    request[4] = 0x00;                      // Reserved
    request[5] = 0x01;                      // Number of configs to read
    
    // Calculate and append CRC
    crc = modbus_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Send request (implementation-specific)
    // send_modbus_frame(request, 8);
    
    // Receive and parse response (simplified)
    // In real implementation, you would:
    // 1. Read response from serial port
    // 2. Verify CRC
    // 3. Check for exception response
    
    return 0; // Success
}

// Custom function: Write device configuration
int modbus_custom_write_config(uint8_t slave_id, uint16_t config_id, 
                                uint16_t value, uint8_t flags) {
    uint8_t request[12];
    uint16_t crc;
    
    request[0] = slave_id;
    request[1] = MODBUS_FC_CUSTOM_WRITE_CONFIG;
    request[2] = (config_id >> 8) & 0xFF;
    request[3] = config_id & 0xFF;
    request[4] = (value >> 8) & 0xFF;
    request[5] = value & 0xFF;
    request[6] = flags;
    request[7] = 0x00;  // Padding
    request[8] = 0x00;  // Padding
    request[9] = 0x00;  // Padding
    
    crc = modbus_crc16(request, 10);
    request[10] = crc & 0xFF;
    request[11] = (crc >> 8) & 0xFF;
    
    // Send request
    // send_modbus_frame(request, 12);
    
    return 0;
}

// Server-side handler for custom function codes
int modbus_handle_custom_function(uint8_t *request, uint16_t request_len,
                                   uint8_t *response, uint16_t *response_len) {
    uint8_t function_code = request[1];
    uint16_t crc;
    
    switch (function_code) {
        case MODBUS_FC_CUSTOM_READ_CONFIG: {
            uint16_t config_id = (request[2] << 8) | request[3];
            
            // Validate request
            if (request_len < 8) {
                // Build exception response
                response[0] = request[0];  // Slave ID
                response[1] = function_code | 0x80;  // Error flag
                response[2] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
                crc = modbus_crc16(response, 3);
                response[3] = crc & 0xFF;
                response[4] = (crc >> 8) & 0xFF;
                *response_len = 5;
                return -1;
            }
            
            // Read configuration (implementation-specific)
            uint16_t config_value = 0x1234;  // Example value
            uint8_t config_flags = 0x01;
            
            // Build response
            response[0] = request[0];  // Slave ID
            response[1] = function_code;
            response[2] = 5;  // Byte count
            response[3] = (config_id >> 8) & 0xFF;
            response[4] = config_id & 0xFF;
            response[5] = (config_value >> 8) & 0xFF;
            response[6] = config_value & 0xFF;
            response[7] = config_flags;
            
            crc = modbus_crc16(response, 8);
            response[8] = crc & 0xFF;
            response[9] = (crc >> 8) & 0xFF;
            *response_len = 10;
            break;
        }
        
        case MODBUS_FC_CUSTOM_WRITE_CONFIG: {
            uint16_t config_id = (request[2] << 8) | request[3];
            uint16_t value = (request[4] << 8) | request[5];
            uint8_t flags = request[6];
            
            // Write configuration (implementation-specific)
            // write_device_config(config_id, value, flags);
            
            // Echo request as confirmation
            memcpy(response, request, request_len);
            *response_len = request_len;
            break;
        }
        
        case MODBUS_FC_CUSTOM_BULK_READ: {
            // Implementation for bulk read operation
            // This could read multiple register ranges in one request
            response[0] = request[0];
            response[1] = function_code;
            response[2] = 0;  // Byte count (to be filled)
            // ... add bulk data ...
            *response_len = 3;  // + data length
            break;
        }
        
        default:
            // Unsupported function code
            response[0] = request[0];
            response[1] = function_code | 0x80;
            response[2] = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
            crc = modbus_crc16(response, 3);
            response[3] = crc & 0xFF;
            response[4] = (crc >> 8) & 0xFF;
            *response_len = 5;
            return -1;
    }
    
    return 0;
}

// Example usage
void example_custom_functions() {
    uint8_t response[256];
    uint16_t response_len;
    
    // Read custom configuration
    modbus_custom_read_config(0x01, 0x1000, response, &response_len);
    
    // Write custom configuration
    modbus_custom_write_config(0x01, 0x1000, 0x5678, 0x03);
}
```

## Rust Implementation

```rust
use std::io::{self, Read, Write};

// Custom function codes
const MODBUS_FC_CUSTOM_READ_CONFIG: u8 = 0x41;
const MODBUS_FC_CUSTOM_WRITE_CONFIG: u8 = 0x42;
const MODBUS_FC_CUSTOM_BULK_READ: u8 = 0x65;
const MODBUS_FC_CUSTOM_DEVICE_INFO: u8 = 0x66;

// Exception codes
const MODBUS_EXCEPTION_ILLEGAL_FUNCTION: u8 = 0x01;
const MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS: u8 = 0x02;
const MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE: u8 = 0x03;

// Custom data structure
#[derive(Debug, Clone)]
struct CustomConfig {
    config_id: u16,
    value: u16,
    flags: u8,
}

// Modbus frame types
#[derive(Debug)]
enum ModbusFrame {
    Request {
        slave_id: u8,
        function_code: u8,
        data: Vec<u8>,
    },
    Response {
        slave_id: u8,
        function_code: u8,
        data: Vec<u8>,
    },
    Exception {
        slave_id: u8,
        function_code: u8,
        exception_code: u8,
    },
}

// CRC16 calculation
fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

// Custom Modbus client
struct ModbusCustomClient {
    slave_id: u8,
}

impl ModbusCustomClient {
    fn new(slave_id: u8) -> Self {
        ModbusCustomClient { slave_id }
    }
    
    // Build request frame with CRC
    fn build_frame(&self, function_code: u8, data: &[u8]) -> Vec<u8> {
        let mut frame = Vec::with_capacity(data.len() + 4);
        frame.push(self.slave_id);
        frame.push(function_code);
        frame.extend_from_slice(data);
        
        let crc = modbus_crc16(&frame);
        frame.push((crc & 0xFF) as u8);
        frame.push((crc >> 8) as u8);
        
        frame
    }
    
    // Verify CRC of received frame
    fn verify_crc(&self, frame: &[u8]) -> bool {
        if frame.len() < 4 {
            return false;
        }
        
        let data_len = frame.len() - 2;
        let received_crc = u16::from_le_bytes([frame[data_len], frame[data_len + 1]]);
        let calculated_crc = modbus_crc16(&frame[..data_len]);
        
        received_crc == calculated_crc
    }
    
    // Read custom configuration
    fn read_config(&self, config_id: u16) -> Result<CustomConfig, String> {
        let mut data = Vec::new();
        data.push((config_id >> 8) as u8);
        data.push((config_id & 0xFF) as u8);
        data.push(0x00); // Reserved
        data.push(0x01); // Number of configs
        
        let frame = self.build_frame(MODBUS_FC_CUSTOM_READ_CONFIG, &data);
        
        // In a real implementation, send frame and receive response
        // let response = self.send_and_receive(&frame)?;
        
        // Parse response (simplified)
        Ok(CustomConfig {
            config_id,
            value: 0x1234,
            flags: 0x01,
        })
    }
    
    // Write custom configuration
    fn write_config(&self, config: &CustomConfig) -> Result<(), String> {
        let mut data = Vec::new();
        data.push((config.config_id >> 8) as u8);
        data.push((config.config_id & 0xFF) as u8);
        data.push((config.value >> 8) as u8);
        data.push((config.value & 0xFF) as u8);
        data.push(config.flags);
        data.extend_from_slice(&[0x00, 0x00, 0x00]); // Padding
        
        let frame = self.build_frame(MODBUS_FC_CUSTOM_WRITE_CONFIG, &data);
        
        // Send frame
        // self.send_frame(&frame)?;
        
        Ok(())
    }
    
    // Custom bulk read operation
    fn bulk_read(&self, addresses: &[(u16, u16)]) -> Result<Vec<u16>, String> {
        let mut data = Vec::new();
        data.push(addresses.len() as u8);
        
        for &(addr, count) in addresses {
            data.push((addr >> 8) as u8);
            data.push((addr & 0xFF) as u8);
            data.push((count >> 8) as u8);
            data.push((count & 0xFF) as u8);
        }
        
        let frame = self.build_frame(MODBUS_FC_CUSTOM_BULK_READ, &data);
        
        // Send and receive
        // let response = self.send_and_receive(&frame)?;
        
        Ok(vec![0x1111, 0x2222, 0x3333]) // Example data
    }
}

// Custom Modbus server handler
struct ModbusCustomServer {
    configs: std::collections::HashMap<u16, CustomConfig>,
}

impl ModbusCustomServer {
    fn new() -> Self {
        ModbusCustomServer {
            configs: std::collections::HashMap::new(),
        }
    }
    
    // Handle custom function codes
    fn handle_request(&mut self, request: &[u8]) -> Result<Vec<u8>, String> {
        if request.len() < 4 {
            return Err("Invalid request length".to_string());
        }
        
        let slave_id = request[0];
        let function_code = request[1];
        
        // Verify CRC
        let crc_valid = {
            let data_len = request.len() - 2;
            let received_crc = u16::from_le_bytes([request[data_len], request[data_len + 1]]);
            let calculated_crc = modbus_crc16(&request[..data_len]);
            received_crc == calculated_crc
        };
        
        if !crc_valid {
            return Err("CRC verification failed".to_string());
        }
        
        match function_code {
            MODBUS_FC_CUSTOM_READ_CONFIG => {
                self.handle_read_config(slave_id, &request[2..])
            }
            MODBUS_FC_CUSTOM_WRITE_CONFIG => {
                self.handle_write_config(slave_id, &request[2..])
            }
            MODBUS_FC_CUSTOM_BULK_READ => {
                self.handle_bulk_read(slave_id, &request[2..])
            }
            _ => {
                self.build_exception_response(slave_id, function_code, 
                                               MODBUS_EXCEPTION_ILLEGAL_FUNCTION)
            }
        }
    }
    
    fn handle_read_config(&self, slave_id: u8, data: &[u8]) -> Result<Vec<u8>, String> {
        if data.len() < 4 {
            return self.build_exception_response(
                slave_id, 
                MODBUS_FC_CUSTOM_READ_CONFIG,
                MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE
            );
        }
        
        let config_id = u16::from_be_bytes([data[0], data[1]]);
        
        match self.configs.get(&config_id) {
            Some(config) => {
                let mut response = Vec::new();
                response.push(slave_id);
                response.push(MODBUS_FC_CUSTOM_READ_CONFIG);
                response.push(5); // Byte count
                response.push((config.config_id >> 8) as u8);
                response.push((config.config_id & 0xFF) as u8);
                response.push((config.value >> 8) as u8);
                response.push((config.value & 0xFF) as u8);
                response.push(config.flags);
                
                let crc = modbus_crc16(&response);
                response.push((crc & 0xFF) as u8);
                response.push((crc >> 8) as u8);
                
                Ok(response)
            }
            None => {
                self.build_exception_response(
                    slave_id,
                    MODBUS_FC_CUSTOM_READ_CONFIG,
                    MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS
                )
            }
        }
    }
    
    fn handle_write_config(&mut self, slave_id: u8, data: &[u8]) -> Result<Vec<u8>, String> {
        if data.len() < 7 {
            return self.build_exception_response(
                slave_id,
                MODBUS_FC_CUSTOM_WRITE_CONFIG,
                MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE
            );
        }
        
        let config = CustomConfig {
            config_id: u16::from_be_bytes([data[0], data[1]]),
            value: u16::from_be_bytes([data[2], data[3]]),
            flags: data[4],
        };
        
        self.configs.insert(config.config_id, config.clone());
        
        // Echo request as confirmation
        let mut response = Vec::new();
        response.push(slave_id);
        response.push(MODBUS_FC_CUSTOM_WRITE_CONFIG);
        response.extend_from_slice(&data[..7]);
        
        let crc = modbus_crc16(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    fn handle_bulk_read(&self, slave_id: u8, data: &[u8]) -> Result<Vec<u8>, String> {
        // Implementation for bulk read
        let mut response = Vec::new();
        response.push(slave_id);
        response.push(MODBUS_FC_CUSTOM_BULK_READ);
        response.push(0); // Byte count placeholder
        
        // Add bulk data here
        
        let crc = modbus_crc16(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    fn build_exception_response(&self, slave_id: u8, function_code: u8, 
                                exception_code: u8) -> Result<Vec<u8>, String> {
        let mut response = Vec::new();
        response.push(slave_id);
        response.push(function_code | 0x80);
        response.push(exception_code);
        
        let crc = modbus_crc16(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
}

// Example usage
fn main() {
    // Client example
    let client = ModbusCustomClient::new(0x01);
    
    match client.read_config(0x1000) {
        Ok(config) => println!("Read config: {:?}", config),
        Err(e) => eprintln!("Error: {}", e),
    }
    
    let new_config = CustomConfig {
        config_id: 0x1000,
        value: 0x5678,
        flags: 0x03,
    };
    
    client.write_config(&new_config).unwrap();
    
    // Bulk read example
    let addresses = vec![(0x0000, 10), (0x0100, 5), (0x0200, 3)];
    match client.bulk_read(&addresses) {
        Ok(values) => println!("Bulk read: {:?}", values),
        Err(e) => eprintln!("Error: {}", e),
    }
    
    // Server example
    let mut server = ModbusCustomServer::new();
    
    // Simulate receiving a request
    let request = vec![0x01, 0x41, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00];
    match server.handle_request(&request) {
        Ok(response) => println!("Response: {:?}", response),
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

## Summary

Custom function codes (0x41-0x48, 0x64-0x6E) provide a standardized mechanism for extending Modbus with vendor-specific features while maintaining protocol compatibility. Key considerations include:

**Benefits:**
- Enables proprietary functionality without breaking standard compliance
- Maintains interoperability with standard Modbus devices
- Allows optimization for specific application requirements
- Supports innovation while preserving backward compatibility

**Implementation Requirements:**
- Follow standard Modbus frame structure
- Implement proper CRC/LRC checking
- Return standard exception codes for errors
- Document custom function behavior thoroughly
- Ensure non-interference with standard function codes

**Best Practices:**
- Reserve function codes systematically within your organization
- Provide clear documentation for custom functions
- Implement fallback to standard functions where possible
- Test interoperability with standard Modbus tools
- Version custom function implementations for future compatibility

Custom function codes are particularly valuable in industrial automation where specialized operations—such as advanced diagnostics, multi-register atomic operations, or device-specific calibration—require extensions beyond standard Modbus capabilities.