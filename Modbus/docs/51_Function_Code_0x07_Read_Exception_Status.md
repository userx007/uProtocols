# Function Code 0x07: Read Exception Status

## Overview

Modbus Function Code 0x07 (Read Exception Status) is a specialized diagnostic function designed for rapid polling of device health and operational status. Unlike other Modbus functions that read data from registers or coils, this function returns a single byte (8 bits) representing the device's exception/alarm status, making it extremely lightweight and fast for monitoring critical system conditions.

## Protocol Details

### Request Format
The request is minimal, containing only:
- **Slave Address** (1 byte): Device address
- **Function Code** (1 byte): 0x07
- **CRC** (2 bytes): Error checking (Modbus RTU)

Total request size: **4 bytes** (RTU) or **2 bytes** (TCP, excluding MBAP header)

### Response Format
- **Slave Address** (1 byte)
- **Function Code** (1 byte): 0x07
- **Exception Status** (1 byte): 8-bit status flags
- **CRC** (2 bytes): Error checking (RTU)

Total response size: **5 bytes** (RTU) or **3 bytes** (TCP)

### Exception Status Byte
The 8-bit response contains device-specific status flags. Common interpretations:
- **Bit 0-7**: Device-specific exception conditions (overtemperature, low battery, communication errors, etc.)
- Each bit typically represents a boolean alarm/status condition
- The meaning of each bit is vendor-specific and must be documented in the device manual

## Use Cases

1. **Rapid Health Monitoring**: Quickly check if any critical alarms are active
2. **System Diagnostics**: Monitor multiple devices in a polling loop
3. **Alarm Notification**: Detect state changes requiring immediate attention
4. **Preventive Maintenance**: Track intermittent issues before failure

## C/C++ Implementation

### Basic C Implementation

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Modbus CRC16 calculation
uint16_t modbus_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
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

// Build Read Exception Status request
size_t build_read_exception_status_request(uint8_t slave_addr, uint8_t *buffer) {
    buffer[0] = slave_addr;
    buffer[1] = 0x07;  // Function code
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(buffer, 2);
    buffer[2] = crc & 0xFF;        // CRC low byte
    buffer[3] = (crc >> 8) & 0xFF; // CRC high byte
    
    return 4;  // Total request length
}

// Parse Read Exception Status response
int parse_read_exception_status_response(const uint8_t *response, size_t len, 
                                         uint8_t *exception_status) {
    if (len < 5) {
        fprintf(stderr, "Response too short\n");
        return -1;
    }
    
    // Verify function code
    if (response[1] != 0x07) {
        // Check for exception response
        if (response[1] == 0x87) {
            fprintf(stderr, "Modbus exception: 0x%02X\n", response[2]);
            return -1;
        }
        fprintf(stderr, "Invalid function code: 0x%02X\n", response[1]);
        return -1;
    }
    
    // Verify CRC
    uint16_t calc_crc = modbus_crc16(response, 3);
    uint16_t recv_crc = response[3] | (response[4] << 8);
    
    if (calc_crc != recv_crc) {
        fprintf(stderr, "CRC mismatch: calculated 0x%04X, received 0x%04X\n", 
                calc_crc, recv_crc);
        return -1;
    }
    
    *exception_status = response[2];
    return 0;
}

// Decode exception status bits (device-specific example)
void decode_exception_status(uint8_t status) {
    printf("Exception Status: 0x%02X\n", status);
    printf("  Bit 0 (Overtemperature): %s\n", (status & 0x01) ? "ALARM" : "OK");
    printf("  Bit 1 (Low Battery):     %s\n", (status & 0x02) ? "ALARM" : "OK");
    printf("  Bit 2 (Communication):   %s\n", (status & 0x04) ? "ERROR" : "OK");
    printf("  Bit 3 (Hardware Fault):  %s\n", (status & 0x08) ? "FAULT" : "OK");
    printf("  Bit 4 (Calibration):     %s\n", (status & 0x10) ? "NEEDED" : "OK");
    printf("  Bit 5 (Reserved):        %s\n", (status & 0x20) ? "SET" : "CLEAR");
    printf("  Bit 6 (Reserved):        %s\n", (status & 0x40) ? "SET" : "CLEAR");
    printf("  Bit 7 (Reserved):        %s\n", (status & 0x80) ? "SET" : "CLEAR");
}

// Example usage
int main() {
    uint8_t request[4];
    uint8_t response[5] = {0x01, 0x07, 0x0D, 0x00, 0x00}; // Example response
    uint8_t exception_status;
    
    // Build request for slave address 1
    size_t req_len = build_read_exception_status_request(0x01, request);
    
    printf("Request: ");
    for (size_t i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n\n");
    
    // Simulate CRC calculation for example response
    uint16_t crc = modbus_crc16(response, 3);
    response[3] = crc & 0xFF;
    response[4] = (crc >> 8) & 0xFF;
    
    // Parse response
    if (parse_read_exception_status_response(response, 5, &exception_status) == 0) {
        decode_exception_status(exception_status);
    }
    
    return 0;
}
```

### C++ Object-Oriented Implementation

```cpp
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <bitset>

class ModbusException : public std::runtime_error {
public:
    ModbusException(const std::string& msg) : std::runtime_error(msg) {}
};

class ModbusExceptionStatus {
private:
    uint8_t slave_address_;
    
    static uint16_t calculate_crc(const std::vector<uint8_t>& data) {
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
    explicit ModbusExceptionStatus(uint8_t slave_addr) 
        : slave_address_(slave_addr) {}
    
    std::vector<uint8_t> build_request() const {
        std::vector<uint8_t> request = {slave_address_, 0x07};
        uint16_t crc = calculate_crc(request);
        request.push_back(crc & 0xFF);
        request.push_back((crc >> 8) & 0xFF);
        return request;
    }
    
    uint8_t parse_response(const std::vector<uint8_t>& response) const {
        if (response.size() < 5) {
            throw ModbusException("Response too short");
        }
        
        if (response[0] != slave_address_) {
            throw ModbusException("Slave address mismatch");
        }
        
        if (response[1] == 0x87) {
            throw ModbusException("Modbus exception: " + 
                std::to_string(response[2]));
        }
        
        if (response[1] != 0x07) {
            throw ModbusException("Invalid function code");
        }
        
        // Verify CRC
        std::vector<uint8_t> data(response.begin(), response.begin() + 3);
        uint16_t calc_crc = calculate_crc(data);
        uint16_t recv_crc = response[3] | (response[4] << 8);
        
        if (calc_crc != recv_crc) {
            throw ModbusException("CRC mismatch");
        }
        
        return response[2];
    }
    
    static void print_status(uint8_t status) {
        std::cout << "Exception Status: 0x" << std::hex 
                  << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(status) << std::dec << std::endl;
        std::cout << "Binary: " << std::bitset<8>(status) << std::endl;
        
        const char* labels[] = {
            "Overtemperature", "Low Battery", "Communication Error",
            "Hardware Fault", "Calibration Required", "Reserved",
            "Reserved", "Reserved"
        };
        
        for (int i = 0; i < 8; i++) {
            std::cout << "  Bit " << i << " (" << labels[i] << "): "
                      << ((status & (1 << i)) ? "SET" : "CLEAR") << std::endl;
        }
    }
};

// Example usage
int main() {
    try {
        ModbusExceptionStatus modbus(0x01);
        
        // Build request
        auto request = modbus.build_request();
        std::cout << "Request: ";
        for (auto byte : request) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl << std::endl;
        
        // Simulate response
        std::vector<uint8_t> response = {0x01, 0x07, 0x0D, 0x00, 0x00};
        uint16_t crc = ModbusExceptionStatus::calculate_crc(
            std::vector<uint8_t>(response.begin(), response.begin() + 3));
        response[3] = crc & 0xFF;
        response[4] = (crc >> 8) & 0xFF;
        
        // Parse and display
        uint8_t status = modbus.parse_response(response);
        ModbusExceptionStatus::print_status(status);
        
    } catch (const ModbusException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

#[derive(Debug)]
pub enum ModbusError {
    ResponseTooShort,
    InvalidFunctionCode(u8),
    CrcMismatch { calculated: u16, received: u16 },
    ModbusException(u8),
    SlaveAddressMismatch,
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusError::ResponseTooShort => write!(f, "Response too short"),
            ModbusError::InvalidFunctionCode(code) => 
                write!(f, "Invalid function code: 0x{:02X}", code),
            ModbusError::CrcMismatch { calculated, received } => 
                write!(f, "CRC mismatch: calculated 0x{:04X}, received 0x{:04X}", 
                       calculated, received),
            ModbusError::ModbusException(code) => 
                write!(f, "Modbus exception: 0x{:02X}", code),
            ModbusError::SlaveAddressMismatch => 
                write!(f, "Slave address mismatch"),
        }
    }
}

impl Error for ModbusError {}

pub struct ModbusExceptionStatus {
    slave_address: u8,
}

impl ModbusExceptionStatus {
    pub fn new(slave_address: u8) -> Self {
        Self { slave_address }
    }
    
    /// Calculate Modbus RTU CRC16
    fn calculate_crc(data: &[u8]) -> u16 {
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
    
    /// Build Read Exception Status request
    pub fn build_request(&self) -> Vec<u8> {
        let mut request = vec![self.slave_address, 0x07];
        let crc = Self::calculate_crc(&request);
        request.push((crc & 0xFF) as u8);
        request.push(((crc >> 8) & 0xFF) as u8);
        request
    }
    
    /// Parse Read Exception Status response
    pub fn parse_response(&self, response: &[u8]) -> Result<u8, ModbusError> {
        if response.len() < 5 {
            return Err(ModbusError::ResponseTooShort);
        }
        
        if response[0] != self.slave_address {
            return Err(ModbusError::SlaveAddressMismatch);
        }
        
        // Check for exception response
        if response[1] == 0x87 {
            return Err(ModbusError::ModbusException(response[2]));
        }
        
        if response[1] != 0x07 {
            return Err(ModbusError::InvalidFunctionCode(response[1]));
        }
        
        // Verify CRC
        let calc_crc = Self::calculate_crc(&response[0..3]);
        let recv_crc = response[3] as u16 | ((response[4] as u16) << 8);
        
        if calc_crc != recv_crc {
            return Err(ModbusError::CrcMismatch {
                calculated: calc_crc,
                received: recv_crc,
            });
        }
        
        Ok(response[2])
    }
}

/// Decode exception status with device-specific meanings
pub fn decode_exception_status(status: u8) {
    println!("Exception Status: 0x{:02X}", status);
    println!("Binary: {:08b}", status);
    
    let labels = [
        "Overtemperature",
        "Low Battery",
        "Communication Error",
        "Hardware Fault",
        "Calibration Required",
        "Reserved",
        "Reserved",
        "Reserved",
    ];
    
    for (i, label) in labels.iter().enumerate() {
        let bit_set = (status & (1 << i)) != 0;
        println!("  Bit {} ({}): {}", i, label, 
                 if bit_set { "SET" } else { "CLEAR" });
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    let modbus = ModbusExceptionStatus::new(0x01);
    
    // Build request
    let request = modbus.build_request();
    print!("Request: ");
    for byte in &request {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Simulate response
    let mut response = vec![0x01, 0x07, 0x0D];
    let crc = ModbusExceptionStatus::calculate_crc(&response);
    response.push((crc & 0xFF) as u8);
    response.push(((crc >> 8) & 0xFF) as u8);
    
    // Parse response
    match modbus.parse_response(&response) {
        Ok(status) => {
            decode_exception_status(status);
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            return Err(Box::new(e));
        }
    }
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_crc_calculation() {
        let data = vec![0x01, 0x07];
        let crc = ModbusExceptionStatus::calculate_crc(&data);
        // Expected CRC for this data
        assert_eq!(crc, 0x4130);
    }
    
    #[test]
    fn test_request_building() {
        let modbus = ModbusExceptionStatus::new(0x01);
        let request = modbus.build_request();
        assert_eq!(request.len(), 4);
        assert_eq!(request[0], 0x01);
        assert_eq!(request[1], 0x07);
    }
    
    #[test]
    fn test_valid_response() {
        let modbus = ModbusExceptionStatus::new(0x01);
        let response = vec![0x01, 0x07, 0x0D, 0x30, 0x41];
        let result = modbus.parse_response(&response);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), 0x0D);
    }
    
    #[test]
    fn test_exception_response() {
        let modbus = ModbusExceptionStatus::new(0x01);
        let response = vec![0x01, 0x87, 0x01, 0x00, 0x00];
        let result = modbus.parse_response(&response);
        assert!(result.is_err());
    }
}
```

## Summary

**Function Code 0x07 (Read Exception Status)** is a highly efficient Modbus function specifically designed for rapid device health monitoring. It returns a single 8-bit status byte where each bit represents a device-specific alarm or exception condition. With minimal overhead (4-byte request, 5-byte response in RTU), this function enables fast polling loops to monitor multiple devices for critical conditions like overtemperature, low battery, communication errors, or hardware faults.

The implementations provided demonstrate:
- **C**: Low-level, efficient implementation with CRC calculation and bit-level status decoding
- **C++**: Object-oriented approach with exception handling and type safety
- **Rust**: Memory-safe implementation with comprehensive error handling and testing

This function is ideal for industrial automation systems requiring continuous health monitoring without the overhead of reading full register blocks, making it perfect for supervisory systems that need to quickly detect and respond to alarm conditions across multiple field devices.