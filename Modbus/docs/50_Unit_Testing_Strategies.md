# Unit Testing Strategies for Modbus Implementations

## Overview

Unit testing Modbus implementations is crucial for ensuring reliability and correctness in industrial communication systems. Since Modbus devices may not always be available during development, and testing with real hardware can be time-consuming and risky, mock devices and simulators provide an essential testing strategy. This approach allows developers to verify protocol handling, error conditions, and edge cases in a controlled environment.

## Key Concepts

### Why Unit Testing Matters for Modbus

1. **Protocol Compliance**: Ensures your implementation correctly follows Modbus specifications
2. **Error Handling**: Validates proper response to communication failures, timeouts, and exception codes
3. **Edge Cases**: Tests boundary conditions like maximum register counts, invalid addresses
4. **Regression Prevention**: Catches bugs introduced by code changes
5. **Hardware Independence**: Enables testing without physical devices

### Testing Approaches

- **Mock Devices**: Simulated devices that respond predictably to Modbus requests
- **Test Doubles**: Stub implementations of communication interfaces
- **Simulators**: More complex virtual devices that model real device behavior
- **Integration Tests**: Testing with actual protocol stack but simulated I/O

## C/C++ Implementation

Here's a comprehensive example using a mock device approach:

```c
// modbus_mock.h
#ifndef MODBUS_MOCK_H
#define MODBUS_MOCK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_REGISTERS 1000
#define MAX_COILS 2000

// Mock Modbus device structure
typedef struct {
    uint16_t registers[MAX_REGISTERS];
    uint8_t coils[MAX_COILS / 8];  // Bit-packed coils
    bool connected;
    int error_inject;  // For testing error conditions
} modbus_mock_device_t;

// Function codes
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_FC_READ_COILS 0x01
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

// Mock device functions
void modbus_mock_init(modbus_mock_device_t *device);
void modbus_mock_set_register(modbus_mock_device_t *device, uint16_t addr, uint16_t value);
uint16_t modbus_mock_get_register(modbus_mock_device_t *device, uint16_t addr);
void modbus_mock_set_coil(modbus_mock_device_t *device, uint16_t addr, bool value);
bool modbus_mock_get_coil(modbus_mock_device_t *device, uint16_t addr);
int modbus_mock_process_request(modbus_mock_device_t *device, 
                                  const uint8_t *request, 
                                  int request_len,
                                  uint8_t *response,
                                  int *response_len);

#endif
```

```c
// modbus_mock.c
#include "modbus_mock.h"
#include <string.h>
#include <stdio.h>

void modbus_mock_init(modbus_mock_device_t *device) {
    memset(device->registers, 0, sizeof(device->registers));
    memset(device->coils, 0, sizeof(device->coils));
    device->connected = true;
    device->error_inject = 0;
}

void modbus_mock_set_register(modbus_mock_device_t *device, uint16_t addr, uint16_t value) {
    if (addr < MAX_REGISTERS) {
        device->registers[addr] = value;
    }
}

uint16_t modbus_mock_get_register(modbus_mock_device_t *device, uint16_t addr) {
    if (addr < MAX_REGISTERS) {
        return device->registers[addr];
    }
    return 0;
}

void modbus_mock_set_coil(modbus_mock_device_t *device, uint16_t addr, bool value) {
    if (addr < MAX_COILS) {
        uint16_t byte_idx = addr / 8;
        uint8_t bit_idx = addr % 8;
        if (value) {
            device->coils[byte_idx] |= (1 << bit_idx);
        } else {
            device->coils[byte_idx] &= ~(1 << bit_idx);
        }
    }
}

bool modbus_mock_get_coil(modbus_mock_device_t *device, uint16_t addr) {
    if (addr < MAX_COILS) {
        uint16_t byte_idx = addr / 8;
        uint8_t bit_idx = addr % 8;
        return (device->coils[byte_idx] & (1 << bit_idx)) != 0;
    }
    return false;
}

int modbus_mock_process_request(modbus_mock_device_t *device,
                                  const uint8_t *request,
                                  int request_len,
                                  uint8_t *response,
                                  int *response_len) {
    if (!device->connected) {
        return -1;  // Connection error
    }

    if (device->error_inject) {
        int error = device->error_inject;
        device->error_inject = 0;
        return error;
    }

    if (request_len < 2) {
        return -1;
    }

    uint8_t slave_id = request[0];
    uint8_t function_code = request[1];

    response[0] = slave_id;
    *response_len = 1;

    switch (function_code) {
        case MODBUS_FC_READ_HOLDING_REGISTERS: {
            if (request_len < 6) return -1;
            
            uint16_t start_addr = (request[2] << 8) | request[3];
            uint16_t count = (request[4] << 8) | request[5];

            if (start_addr + count > MAX_REGISTERS) {
                response[1] = function_code | 0x80;
                response[2] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
                *response_len = 3;
                return 0;
            }

            response[1] = function_code;
            response[2] = count * 2;  // Byte count
            *response_len = 3;

            for (uint16_t i = 0; i < count; i++) {
                uint16_t value = device->registers[start_addr + i];
                response[*response_len] = (value >> 8) & 0xFF;
                response[*response_len + 1] = value & 0xFF;
                *response_len += 2;
            }
            break;
        }

        case MODBUS_FC_WRITE_SINGLE_REGISTER: {
            if (request_len < 6) return -1;
            
            uint16_t addr = (request[2] << 8) | request[3];
            uint16_t value = (request[4] << 8) | request[5];

            if (addr >= MAX_REGISTERS) {
                response[1] = function_code | 0x80;
                response[2] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
                *response_len = 3;
                return 0;
            }

            device->registers[addr] = value;
            memcpy(response, request, 6);
            *response_len = 6;
            break;
        }

        default:
            response[1] = function_code | 0x80;
            response[2] = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
            *response_len = 3;
            break;
    }

    return 0;
}
```

```c
// test_modbus.c - Unit tests using mock device
#include "modbus_mock.h"
#include <assert.h>
#include <stdio.h>

void test_read_holding_registers() {
    printf("Testing: Read Holding Registers\n");
    
    modbus_mock_device_t device;
    modbus_mock_init(&device);
    
    // Setup test data
    modbus_mock_set_register(&device, 0, 0x1234);
    modbus_mock_set_register(&device, 1, 0x5678);
    
    // Create request: Read 2 registers starting at address 0
    uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    uint8_t response[256];
    int response_len;
    
    int result = modbus_mock_process_request(&device, request, 6, response, &response_len);
    
    assert(result == 0);
    assert(response_len == 7);
    assert(response[0] == 0x01);  // Slave ID
    assert(response[1] == 0x03);  // Function code
    assert(response[2] == 0x04);  // Byte count
    assert(response[3] == 0x12 && response[4] == 0x34);  // First register
    assert(response[5] == 0x56 && response[6] == 0x78);  // Second register
    
    printf("  PASSED\n");
}

void test_write_single_register() {
    printf("Testing: Write Single Register\n");
    
    modbus_mock_device_t device;
    modbus_mock_init(&device);
    
    // Create request: Write 0xABCD to address 10
    uint8_t request[] = {0x01, 0x06, 0x00, 0x0A, 0xAB, 0xCD};
    uint8_t response[256];
    int response_len;
    
    int result = modbus_mock_process_request(&device, request, 6, response, &response_len);
    
    assert(result == 0);
    assert(response_len == 6);
    assert(memcmp(response, request, 6) == 0);  // Echo response
    assert(modbus_mock_get_register(&device, 10) == 0xABCD);
    
    printf("  PASSED\n");
}

void test_invalid_address() {
    printf("Testing: Invalid Address Exception\n");
    
    modbus_mock_device_t device;
    modbus_mock_init(&device);
    
    // Request address beyond valid range
    uint8_t request[] = {0x01, 0x03, 0xFF, 0xFF, 0x00, 0x01};
    uint8_t response[256];
    int response_len;
    
    int result = modbus_mock_process_request(&device, request, 6, response, &response_len);
    
    assert(result == 0);
    assert(response[1] == 0x83);  // Exception response
    assert(response[2] == MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    
    printf("  PASSED\n");
}

void test_connection_failure() {
    printf("Testing: Connection Failure\n");
    
    modbus_mock_device_t device;
    modbus_mock_init(&device);
    device.connected = false;
    
    uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t response[256];
    int response_len;
    
    int result = modbus_mock_process_request(&device, request, 6, response, &response_len);
    
    assert(result == -1);
    
    printf("  PASSED\n");
}

int main() {
    printf("Running Modbus Unit Tests\n");
    printf("==========================\n\n");
    
    test_read_holding_registers();
    test_write_single_register();
    test_invalid_address();
    test_connection_failure();
    
    printf("\nAll tests passed!\n");
    return 0;
}
```

## Rust Implementation

Rust's type system and trait system make it particularly well-suited for testing with mocks:

```rust
// modbus_mock.rs
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ModbusException {
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
}

#[derive(Debug)]
pub enum ModbusError {
    ConnectionError,
    InvalidRequest,
    Exception(ModbusException),
}

// Trait for Modbus communication (allows mocking)
pub trait ModbusTransport {
    fn send_request(&mut self, request: &[u8]) -> Result<Vec<u8>, ModbusError>;
}

// Mock device implementation
pub struct MockModbusDevice {
    registers: HashMap<u16, u16>,
    coils: HashMap<u16, bool>,
    connected: bool,
    error_to_inject: Option<ModbusError>,
}

impl MockModbusDevice {
    pub fn new() -> Self {
        MockModbusDevice {
            registers: HashMap::new(),
            coils: HashMap::new(),
            connected: true,
            error_to_inject: None,
        }
    }

    pub fn set_register(&mut self, address: u16, value: u16) {
        self.registers.insert(address, value);
    }

    pub fn get_register(&self, address: u16) -> Option<u16> {
        self.registers.get(&address).copied()
    }

    pub fn set_coil(&mut self, address: u16, value: bool) {
        self.coils.insert(address, value);
    }

    pub fn get_coil(&self, address: u16) -> Option<bool> {
        self.coils.get(&address).copied()
    }

    pub fn set_connected(&mut self, connected: bool) {
        self.connected = connected;
    }

    pub fn inject_error(&mut self, error: ModbusError) {
        self.error_to_inject = Some(error);
    }

    fn process_read_holding_registers(&self, start_addr: u16, count: u16) 
        -> Result<Vec<u8>, ModbusError> {
        let mut response = vec![0x01, 0x03, (count * 2) as u8];

        for i in 0..count {
            let addr = start_addr + i;
            let value = self.registers.get(&addr).copied().unwrap_or(0);
            response.push((value >> 8) as u8);
            response.push((value & 0xFF) as u8);
        }

        Ok(response)
    }

    fn process_write_single_register(&mut self, address: u16, value: u16) 
        -> Result<Vec<u8>, ModbusError> {
        self.registers.insert(address, value);
        
        Ok(vec![
            0x01, 0x06,
            (address >> 8) as u8, (address & 0xFF) as u8,
            (value >> 8) as u8, (value & 0xFF) as u8,
        ])
    }

    fn process_read_coils(&self, start_addr: u16, count: u16) 
        -> Result<Vec<u8>, ModbusError> {
        let byte_count = ((count + 7) / 8) as u8;
        let mut response = vec![0x01, 0x01, byte_count];

        for byte_idx in 0..byte_count {
            let mut byte_value = 0u8;
            for bit_idx in 0..8 {
                let coil_addr = start_addr + (byte_idx as u16 * 8) + bit_idx as u16;
                if coil_addr < start_addr + count {
                    if self.coils.get(&coil_addr).copied().unwrap_or(false) {
                        byte_value |= 1 << bit_idx;
                    }
                }
            }
            response.push(byte_value);
        }

        Ok(response)
    }
}

impl ModbusTransport for MockModbusDevice {
    fn send_request(&mut self, request: &[u8]) -> Result<Vec<u8>, ModbusError> {
        // Check for injected errors
        if let Some(error) = self.error_to_inject.take() {
            return Err(error);
        }

        if !self.connected {
            return Err(ModbusError::ConnectionError);
        }

        if request.len() < 2 {
            return Err(ModbusError::InvalidRequest);
        }

        let function_code = request[1];

        match function_code {
            0x03 => { // Read Holding Registers
                if request.len() < 6 {
                    return Err(ModbusError::InvalidRequest);
                }
                let start_addr = u16::from_be_bytes([request[2], request[3]]);
                let count = u16::from_be_bytes([request[4], request[5]]);
                self.process_read_holding_registers(start_addr, count)
            }
            0x06 => { // Write Single Register
                if request.len() < 6 {
                    return Err(ModbusError::InvalidRequest);
                }
                let address = u16::from_be_bytes([request[2], request[3]]);
                let value = u16::from_be_bytes([request[4], request[5]]);
                self.process_write_single_register(address, value)
            }
            0x01 => { // Read Coils
                if request.len() < 6 {
                    return Err(ModbusError::InvalidRequest);
                }
                let start_addr = u16::from_be_bytes([request[2], request[3]]);
                let count = u16::from_be_bytes([request[4], request[5]]);
                self.process_read_coils(start_addr, count)
            }
            _ => Err(ModbusError::Exception(ModbusException::IllegalFunction)),
        }
    }
}

// Modbus client that uses the transport trait
pub struct ModbusClient<T: ModbusTransport> {
    transport: T,
}

impl<T: ModbusTransport> ModbusClient<T> {
    pub fn new(transport: T) -> Self {
        ModbusClient { transport }
    }

    pub fn read_holding_registers(&mut self, start_addr: u16, count: u16) 
        -> Result<Vec<u16>, ModbusError> {
        let request = vec![
            0x01, 0x03,
            (start_addr >> 8) as u8, (start_addr & 0xFF) as u8,
            (count >> 8) as u8, (count & 0xFF) as u8,
        ];

        let response = self.transport.send_request(&request)?;

        if response.len() < 3 {
            return Err(ModbusError::InvalidRequest);
        }

        let byte_count = response[2] as usize;
        let mut registers = Vec::new();

        for i in 0..(byte_count / 2) {
            let offset = 3 + i * 2;
            let value = u16::from_be_bytes([response[offset], response[offset + 1]]);
            registers.push(value);
        }

        Ok(registers)
    }

    pub fn write_single_register(&mut self, address: u16, value: u16) 
        -> Result<(), ModbusError> {
        let request = vec![
            0x01, 0x06,
            (address >> 8) as u8, (address & 0xFF) as u8,
            (value >> 8) as u8, (value & 0xFF) as u8,
        ];

        self.transport.send_request(&request)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_read_holding_registers() {
        let mut mock_device = MockModbusDevice::new();
        mock_device.set_register(0, 0x1234);
        mock_device.set_register(1, 0x5678);

        let mut client = ModbusClient::new(mock_device);
        let registers = client.read_holding_registers(0, 2).unwrap();

        assert_eq!(registers.len(), 2);
        assert_eq!(registers[0], 0x1234);
        assert_eq!(registers[1], 0x5678);
    }

    #[test]
    fn test_write_single_register() {
        let mock_device = MockModbusDevice::new();
        let mut client = ModbusClient::new(mock_device);

        client.write_single_register(10, 0xABCD).unwrap();

        // Verify by reading back
        let registers = client.read_holding_registers(10, 1).unwrap();
        assert_eq!(registers[0], 0xABCD);
    }

    #[test]
    fn test_connection_failure() {
        let mut mock_device = MockModbusDevice::new();
        mock_device.set_connected(false);

        let mut client = ModbusClient::new(mock_device);
        let result = client.read_holding_registers(0, 1);

        assert!(matches!(result, Err(ModbusError::ConnectionError)));
    }

    #[test]
    fn test_injected_error() {
        let mut mock_device = MockModbusDevice::new();
        mock_device.inject_error(ModbusError::Exception(ModbusException::IllegalDataAddress));

        let mut client = ModbusClient::new(mock_device);
        let result = client.read_holding_registers(0, 1);

        assert!(matches!(
            result,
            Err(ModbusError::Exception(ModbusException::IllegalDataAddress))
        ));
    }

    #[test]
    fn test_read_coils() {
        let mut mock_device = MockModbusDevice::new();
        mock_device.set_coil(0, true);
        mock_device.set_coil(2, true);
        mock_device.set_coil(7, true);

        let request = vec![0x01, 0x01, 0x00, 0x00, 0x00, 0x08];
        let response = mock_device.send_request(&request).unwrap();

        assert_eq!(response[0], 0x01); // Slave ID
        assert_eq!(response[1], 0x01); // Function code
        assert_eq!(response[2], 0x01); // Byte count
        assert_eq!(response[3], 0b10000101); // Coil states
    }

    #[test]
    fn test_multiple_register_operations() {
        let mock_device = MockModbusDevice::new();
        let mut client = ModbusClient::new(mock_device);

        // Write multiple registers
        for i in 0..10 {
            client.write_single_register(i, i * 100).unwrap();
        }

        // Read them back
        let registers = client.read_holding_registers(0, 10).unwrap();
        
        for (i, &value) in registers.iter().enumerate() {
            assert_eq!(value, (i as u16) * 100);
        }
    }
}
```

## Summary

**Unit testing Modbus implementations** with mock devices and simulators is essential for building reliable industrial communication systems. The key strategies include:

- **Mock Devices**: Create simulated devices that respond to Modbus requests predictably, allowing comprehensive testing without hardware
- **Trait/Interface Abstraction**: Design code with clear interfaces (traits in Rust, function pointers in C) that can be swapped for testing
- **Error Injection**: Deliberately inject errors to test exception handling, timeouts, and recovery mechanisms
- **Edge Case Testing**: Verify behavior at protocol boundaries, invalid addresses, and maximum register counts
- **Regression Testing**: Maintain test suites that catch bugs introduced by future changes

Both C/C++ and Rust implementations benefit from this approach, though Rust's type system provides additional compile-time guarantees. The mock device pattern enables fast, repeatable tests that verify protocol compliance, error handling, and business logic without the complexity and time investment of hardware testing. This foundation of unit tests complements integration testing with real devices, creating a comprehensive quality assurance strategy for Modbus-based systems.