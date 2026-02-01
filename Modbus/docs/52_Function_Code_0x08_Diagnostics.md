# Function Code 0x08: Diagnostics - Detailed Description

## Overview

Modbus Function Code 0x08 (Diagnostics) is a specialized function used for testing and maintaining Modbus serial communication systems. It provides various diagnostic sub-functions that help verify communication integrity, reset counters, and perform loopback tests. This function is primarily used in Modbus RTU/ASCII serial communications and is not typically implemented in Modbus TCP.

## Protocol Structure

### Request Format
```
[Device Address][Function Code 0x08][Sub-function Code (2 bytes)][Data (2 bytes)][CRC (2 bytes)]
```

### Response Format
The response typically echoes the request for most sub-functions:
```
[Device Address][Function Code 0x08][Sub-function Code (2 bytes)][Data (2 bytes)][CRC (2 bytes)]
```

## Common Diagnostic Sub-Functions

| Sub-function Code | Name | Description |
|-------------------|------|-------------|
| 0x0000 | Return Query Data | Echo test - returns the data field unchanged |
| 0x0001 | Restart Communications Option | Restart communications with or without event log |
| 0x0002 | Return Diagnostic Register | Returns the contents of the diagnostic register |
| 0x0003 | Change ASCII Input Delimiter | Modifies the delimiter character (ASCII mode only) |
| 0x0004 | Force Listen Only Mode | Forces the device into listen-only mode |
| 0x000A | Clear Counters and Diagnostic Register | Resets all counters and diagnostic information |
| 0x000B | Return Bus Message Count | Returns count of messages device has detected |
| 0x000C | Return Bus Communication Error Count | Returns count of CRC/LRC errors |
| 0x000D | Return Bus Exception Error Count | Returns count of exception responses |
| 0x000E | Return Slave Message Count | Returns count of messages addressed to this device |
| 0x000F | Return Slave No Response Count | Returns count of messages with no response |

## C/C++ Implementation

### Header File (modbus_diagnostics.h)

```c
#ifndef MODBUS_DIAGNOSTICS_H
#define MODBUS_DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>

// Diagnostic sub-function codes
#define DIAG_RETURN_QUERY_DATA              0x0000
#define DIAG_RESTART_COMMUNICATIONS         0x0001
#define DIAG_RETURN_DIAGNOSTIC_REGISTER     0x0002
#define DIAG_CHANGE_ASCII_DELIMITER         0x0003
#define DIAG_FORCE_LISTEN_ONLY              0x0004
#define DIAG_CLEAR_COUNTERS                 0x000A
#define DIAG_RETURN_BUS_MESSAGE_COUNT       0x000B
#define DIAG_RETURN_BUS_COMM_ERROR_COUNT    0x000C
#define DIAG_RETURN_BUS_EXCEPTION_COUNT     0x000D
#define DIAG_RETURN_SLAVE_MESSAGE_COUNT     0x000E
#define DIAG_RETURN_SLAVE_NO_RESPONSE_COUNT 0x000F

// Diagnostic counters structure
typedef struct {
    uint16_t bus_message_count;
    uint16_t bus_comm_error_count;
    uint16_t bus_exception_count;
    uint16_t slave_message_count;
    uint16_t slave_no_response_count;
    uint16_t diagnostic_register;
} modbus_diag_counters_t;

// Function prototypes
int modbus_build_diagnostic_request(uint8_t slave_id, uint16_t sub_function, 
                                     uint16_t data, uint8_t *buffer);
int modbus_parse_diagnostic_response(const uint8_t *response, int response_len,
                                      uint16_t *sub_function, uint16_t *data);
int modbus_process_diagnostic_request(uint8_t slave_id, const uint8_t *request,
                                       int request_len, uint8_t *response,
                                       modbus_diag_counters_t *counters);

#endif // MODBUS_DIAGNOSTICS_H
```

### Implementation File (modbus_diagnostics.c)

```c
#include "modbus_diagnostics.h"
#include <string.h>

// CRC16 calculation for Modbus RTU
static uint16_t calculate_crc16(const uint8_t *data, int length) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; i++) {
        crc ^= data[i];
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

// Build a diagnostic request
int modbus_build_diagnostic_request(uint8_t slave_id, uint16_t sub_function,
                                     uint16_t data, uint8_t *buffer) {
    if (!buffer) return -1;
    
    buffer[0] = slave_id;
    buffer[1] = 0x08; // Function code
    buffer[2] = (sub_function >> 8) & 0xFF;  // Sub-function high byte
    buffer[3] = sub_function & 0xFF;         // Sub-function low byte
    buffer[4] = (data >> 8) & 0xFF;          // Data high byte
    buffer[5] = data & 0xFF;                 // Data low byte
    
    // Calculate and append CRC
    uint16_t crc = calculate_crc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;
    
    return 8; // Total length
}

// Parse a diagnostic response
int modbus_parse_diagnostic_response(const uint8_t *response, int response_len,
                                      uint16_t *sub_function, uint16_t *data) {
    if (!response || response_len < 8) return -1;
    
    // Verify CRC
    uint16_t received_crc = response[6] | (response[7] << 8);
    uint16_t calculated_crc = calculate_crc16(response, 6);
    
    if (received_crc != calculated_crc) {
        return -2; // CRC error
    }
    
    if (response[1] != 0x08) {
        return -3; // Wrong function code
    }
    
    *sub_function = (response[2] << 8) | response[3];
    *data = (response[4] << 8) | response[5];
    
    return 0; // Success
}

// Process diagnostic request (server-side)
int modbus_process_diagnostic_request(uint8_t slave_id, const uint8_t *request,
                                       int request_len, uint8_t *response,
                                       modbus_diag_counters_t *counters) {
    if (!request || !response || !counters || request_len < 8) return -1;
    
    uint16_t sub_function = (request[2] << 8) | request[3];
    uint16_t data = (request[4] << 8) | request[5];
    uint16_t response_data = 0;
    
    switch (sub_function) {
        case DIAG_RETURN_QUERY_DATA:
            // Echo the data back
            response_data = data;
            break;
            
        case DIAG_CLEAR_COUNTERS:
            // Reset all counters
            memset(counters, 0, sizeof(modbus_diag_counters_t));
            response_data = 0;
            break;
            
        case DIAG_RETURN_BUS_MESSAGE_COUNT:
            response_data = counters->bus_message_count;
            break;
            
        case DIAG_RETURN_BUS_COMM_ERROR_COUNT:
            response_data = counters->bus_comm_error_count;
            break;
            
        case DIAG_RETURN_BUS_EXCEPTION_COUNT:
            response_data = counters->bus_exception_count;
            break;
            
        case DIAG_RETURN_SLAVE_MESSAGE_COUNT:
            response_data = counters->slave_message_count;
            break;
            
        case DIAG_RETURN_SLAVE_NO_RESPONSE_COUNT:
            response_data = counters->slave_no_response_count;
            break;
            
        case DIAG_RETURN_DIAGNOSTIC_REGISTER:
            response_data = counters->diagnostic_register;
            break;
            
        default:
            return -1; // Unsupported sub-function
    }
    
    return modbus_build_diagnostic_request(slave_id, sub_function, 
                                           response_data, response);
}
```

### Example Usage (C++)

```cpp
#include "modbus_diagnostics.h"
#include <iostream>
#include <iomanip>

void print_buffer(const uint8_t *buffer, int length) {
    for (int i = 0; i < length; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(buffer[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

int main() {
    uint8_t request[8];
    uint8_t response[8];
    modbus_diag_counters_t counters = {0};
    
    // Example 1: Loopback test
    std::cout << "=== Loopback Test ===" << std::endl;
    int len = modbus_build_diagnostic_request(1, DIAG_RETURN_QUERY_DATA, 
                                              0xA5A5, request);
    std::cout << "Request: ";
    print_buffer(request, len);
    
    // Simulate response (echo)
    memcpy(response, request, len);
    
    uint16_t sub_func, data;
    if (modbus_parse_diagnostic_response(response, len, &sub_func, &data) == 0) {
        std::cout << "Response data: 0x" << std::hex << data << std::dec << std::endl;
    }
    
    // Example 2: Get bus message count
    std::cout << "\n=== Get Bus Message Count ===" << std::endl;
    counters.bus_message_count = 1234;
    
    len = modbus_build_diagnostic_request(1, DIAG_RETURN_BUS_MESSAGE_COUNT, 
                                          0, request);
    std::cout << "Request: ";
    print_buffer(request, len);
    
    len = modbus_process_diagnostic_request(1, request, len, response, &counters);
    std::cout << "Response: ";
    print_buffer(response, len);
    
    if (modbus_parse_diagnostic_response(response, len, &sub_func, &data) == 0) {
        std::cout << "Bus message count: " << data << std::endl;
    }
    
    // Example 3: Clear counters
    std::cout << "\n=== Clear Counters ===" << std::endl;
    len = modbus_build_diagnostic_request(1, DIAG_CLEAR_COUNTERS, 0, request);
    modbus_process_diagnostic_request(1, request, len, response, &counters);
    std::cout << "Counters cleared. Bus message count: " 
              << counters.bus_message_count << std::endl;
    
    return 0;
}
```

## Rust Implementation

```rust
// modbus_diagnostics.rs

use std::io::{self, Error, ErrorKind};

// Diagnostic sub-function codes
pub const DIAG_RETURN_QUERY_DATA: u16 = 0x0000;
pub const DIAG_RESTART_COMMUNICATIONS: u16 = 0x0001;
pub const DIAG_RETURN_DIAGNOSTIC_REGISTER: u16 = 0x0002;
pub const DIAG_CHANGE_ASCII_DELIMITER: u16 = 0x0003;
pub const DIAG_FORCE_LISTEN_ONLY: u16 = 0x0004;
pub const DIAG_CLEAR_COUNTERS: u16 = 0x000A;
pub const DIAG_RETURN_BUS_MESSAGE_COUNT: u16 = 0x000B;
pub const DIAG_RETURN_BUS_COMM_ERROR_COUNT: u16 = 0x000C;
pub const DIAG_RETURN_BUS_EXCEPTION_COUNT: u16 = 0x000D;
pub const DIAG_RETURN_SLAVE_MESSAGE_COUNT: u16 = 0x000E;
pub const DIAG_RETURN_SLAVE_NO_RESPONSE_COUNT: u16 = 0x000F;

const MODBUS_FUNC_DIAGNOSTICS: u8 = 0x08;

/// Diagnostic counters structure
#[derive(Debug, Clone, Default)]
pub struct DiagnosticCounters {
    pub bus_message_count: u16,
    pub bus_comm_error_count: u16,
    pub bus_exception_count: u16,
    pub slave_message_count: u16,
    pub slave_no_response_count: u16,
    pub diagnostic_register: u16,
}

/// Calculate CRC16 for Modbus RTU
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
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

/// Build a diagnostic request
pub fn build_diagnostic_request(
    slave_id: u8,
    sub_function: u16,
    data: u16,
) -> Vec<u8> {
    let mut buffer = Vec::with_capacity(8);
    
    buffer.push(slave_id);
    buffer.push(MODBUS_FUNC_DIAGNOSTICS);
    buffer.push((sub_function >> 8) as u8);
    buffer.push((sub_function & 0xFF) as u8);
    buffer.push((data >> 8) as u8);
    buffer.push((data & 0xFF) as u8);
    
    // Calculate and append CRC
    let crc = calculate_crc16(&buffer);
    buffer.push((crc & 0xFF) as u8);
    buffer.push((crc >> 8) as u8);
    
    buffer
}

/// Parse a diagnostic response
pub fn parse_diagnostic_response(response: &[u8]) -> io::Result<(u16, u16)> {
    if response.len() < 8 {
        return Err(Error::new(ErrorKind::InvalidData, "Response too short"));
    }
    
    // Verify CRC
    let received_crc = response[6] as u16 | ((response[7] as u16) << 8);
    let calculated_crc = calculate_crc16(&response[0..6]);
    
    if received_crc != calculated_crc {
        return Err(Error::new(ErrorKind::InvalidData, "CRC mismatch"));
    }
    
    if response[1] != MODBUS_FUNC_DIAGNOSTICS {
        return Err(Error::new(ErrorKind::InvalidData, "Wrong function code"));
    }
    
    let sub_function = ((response[2] as u16) << 8) | (response[3] as u16);
    let data = ((response[4] as u16) << 8) | (response[5] as u16);
    
    Ok((sub_function, data))
}

/// Process diagnostic request (server-side)
pub fn process_diagnostic_request(
    slave_id: u8,
    request: &[u8],
    counters: &mut DiagnosticCounters,
) -> io::Result<Vec<u8>> {
    if request.len() < 8 {
        return Err(Error::new(ErrorKind::InvalidData, "Request too short"));
    }
    
    let sub_function = ((request[2] as u16) << 8) | (request[3] as u16);
    let data = ((request[4] as u16) << 8) | (request[5] as u16);
    
    let response_data = match sub_function {
        DIAG_RETURN_QUERY_DATA => data,
        
        DIAG_CLEAR_COUNTERS => {
            *counters = DiagnosticCounters::default();
            0
        }
        
        DIAG_RETURN_BUS_MESSAGE_COUNT => counters.bus_message_count,
        DIAG_RETURN_BUS_COMM_ERROR_COUNT => counters.bus_comm_error_count,
        DIAG_RETURN_BUS_EXCEPTION_COUNT => counters.bus_exception_count,
        DIAG_RETURN_SLAVE_MESSAGE_COUNT => counters.slave_message_count,
        DIAG_RETURN_SLAVE_NO_RESPONSE_COUNT => counters.slave_no_response_count,
        DIAG_RETURN_DIAGNOSTIC_REGISTER => counters.diagnostic_register,
        
        _ => return Err(Error::new(ErrorKind::InvalidInput, 
                                   "Unsupported sub-function")),
    };
    
    Ok(build_diagnostic_request(slave_id, sub_function, response_data))
}

/// Diagnostic client for easy testing
pub struct DiagnosticClient {
    slave_id: u8,
}

impl DiagnosticClient {
    pub fn new(slave_id: u8) -> Self {
        DiagnosticClient { slave_id }
    }
    
    /// Perform a loopback test
    pub fn loopback_test(&self, test_data: u16) -> Vec<u8> {
        build_diagnostic_request(self.slave_id, DIAG_RETURN_QUERY_DATA, test_data)
    }
    
    /// Request bus message count
    pub fn get_bus_message_count(&self) -> Vec<u8> {
        build_diagnostic_request(self.slave_id, DIAG_RETURN_BUS_MESSAGE_COUNT, 0)
    }
    
    /// Request communication error count
    pub fn get_comm_error_count(&self) -> Vec<u8> {
        build_diagnostic_request(self.slave_id, DIAG_RETURN_BUS_COMM_ERROR_COUNT, 0)
    }
    
    /// Clear all counters
    pub fn clear_counters(&self) -> Vec<u8> {
        build_diagnostic_request(self.slave_id, DIAG_CLEAR_COUNTERS, 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_loopback() {
        let request = build_diagnostic_request(1, DIAG_RETURN_QUERY_DATA, 0xA5A5);
        assert_eq!(request.len(), 8);
        assert_eq!(request[0], 1);  // Slave ID
        assert_eq!(request[1], 0x08);  // Function code
        
        let (sub_func, data) = parse_diagnostic_response(&request).unwrap();
        assert_eq!(sub_func, DIAG_RETURN_QUERY_DATA);
        assert_eq!(data, 0xA5A5);
    }
    
    #[test]
    fn test_counters() {
        let mut counters = DiagnosticCounters::default();
        counters.bus_message_count = 100;
        
        let request = build_diagnostic_request(1, DIAG_RETURN_BUS_MESSAGE_COUNT, 0);
        let response = process_diagnostic_request(1, &request, &mut counters).unwrap();
        
        let (_, count) = parse_diagnostic_response(&response).unwrap();
        assert_eq!(count, 100);
    }
}

// Example usage
fn main() {
    let mut counters = DiagnosticCounters::default();
    counters.bus_message_count = 1234;
    counters.bus_comm_error_count = 5;
    
    println!("=== Loopback Test ===");
    let client = DiagnosticClient::new(1);
    let request = client.loopback_test(0xA5A5);
    println!("Request: {:02X?}", request);
    
    match parse_diagnostic_response(&request) {
        Ok((sub_func, data)) => {
            println!("Sub-function: 0x{:04X}, Data: 0x{:04X}", sub_func, data);
        }
        Err(e) => println!("Error: {}", e),
    }
    
    println!("\n=== Get Bus Message Count ===");
    let request = client.get_bus_message_count();
    match process_diagnostic_request(1, &request, &mut counters) {
        Ok(response) => {
            println!("Response: {:02X?}", response);
            if let Ok((_, count)) = parse_diagnostic_response(&response) {
                println!("Bus message count: {}", count);
            }
        }
        Err(e) => println!("Error: {}", e),
    }
    
    println!("\n=== Clear Counters ===");
    let request = client.clear_counters();
    process_diagnostic_request(1, &request, &mut counters).unwrap();
    println!("Counters cleared. Bus message count: {}", counters.bus_message_count);
}
```

## Summary

**Modbus Function Code 0x08 (Diagnostics)** is a maintenance and testing function that provides essential capabilities for monitoring and troubleshooting Modbus serial networks. Key points include:

**Core Capabilities**: Loopback testing with echo data verification, communication counter retrieval for traffic analysis, diagnostic register access for system health monitoring, counter reset functionality for maintenance operations, and specialized tests like forcing listen-only mode.

**Common Use Cases**: Verifying communication channel integrity during installation or troubleshooting, monitoring communication quality through error counters, debugging protocol implementation issues, performing preventive maintenance by tracking message counts, and resetting statistics after system changes.

**Implementation Considerations**: The function uses a two-byte sub-function code followed by two bytes of data, responses typically echo the request for verification purposes, CRC validation is critical for reliable diagnostics, and different sub-functions serve specific diagnostic purposes from simple echo tests to complex counter queries.

**Limitations**: Primarily designed for serial Modbus (RTU/ASCII), not commonly used in Modbus TCP, sub-function support varies by device manufacturer, and some diagnostic features may require device-specific implementation.

The diagnostic function is invaluable for system integrators and maintenance personnel, providing low-level access to communication statistics and health indicators that enable proactive monitoring and rapid troubleshooting of Modbus networks.