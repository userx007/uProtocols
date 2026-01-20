# Modbus Slave/Server Implementation

## Overview

A Modbus slave (also called a server in modern terminology) is a device that responds to requests from a Modbus master/client. The slave maintains a data model consisting of coils, discrete inputs, holding registers, and input registers, and processes incoming Modbus commands to read or write these data points.

## Key Concepts

### Slave Responsibilities

1. **Listen for requests** - Monitor the communication channel for incoming Modbus frames
2. **Validate requests** - Check frame integrity (CRC/LRC), address matching, and command validity
3. **Process commands** - Execute read/write operations on the internal data model
4. **Generate responses** - Create properly formatted response frames
5. **Handle exceptions** - Return error codes for invalid requests

### Data Model

A Modbus slave typically maintains four data areas:

- **Coils (0x)** - Read/write binary outputs (function codes 0x01, 0x05, 0x0F)
- **Discrete Inputs (1x)** - Read-only binary inputs (function code 0x02)
- **Input Registers (3x)** - Read-only 16-bit registers (function code 0x04)
- **Holding Registers (4x)** - Read/write 16-bit registers (function codes 0x03, 0x06, 0x10)

## C/C++ Implementation

Here's a comprehensive Modbus RTU slave implementation in C:

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Modbus function codes
#define MB_FC_READ_COILS           0x01
#define MB_FC_READ_DISCRETE_INPUTS 0x02
#define MB_FC_READ_HOLDING_REGS    0x03
#define MB_FC_READ_INPUT_REGS      0x04
#define MB_FC_WRITE_SINGLE_COIL    0x05
#define MB_FC_WRITE_SINGLE_REG     0x06
#define MB_FC_WRITE_MULTIPLE_COILS 0x0F
#define MB_FC_WRITE_MULTIPLE_REGS  0x10

// Exception codes
#define MB_EX_ILLEGAL_FUNCTION     0x01
#define MB_EX_ILLEGAL_DATA_ADDRESS 0x02
#define MB_EX_ILLEGAL_DATA_VALUE   0x03
#define MB_EX_SLAVE_DEVICE_FAILURE 0x04

#define MAX_COILS 256
#define MAX_DISCRETE_INPUTS 256
#define MAX_HOLDING_REGS 128
#define MAX_INPUT_REGS 128
#define MAX_FRAME_SIZE 256

typedef struct {
    uint8_t slave_id;
    
    // Data model
    uint8_t coils[MAX_COILS / 8];           // Packed bits
    uint8_t discrete_inputs[MAX_DISCRETE_INPUTS / 8];
    uint16_t holding_registers[MAX_HOLDING_REGS];
    uint16_t input_registers[MAX_INPUT_REGS];
    
    // Frame buffers
    uint8_t rx_buffer[MAX_FRAME_SIZE];
    uint8_t tx_buffer[MAX_FRAME_SIZE];
    uint16_t rx_length;
} modbus_slave_t;

// CRC-16 calculation for Modbus RTU
uint16_t modbus_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
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

// Get/set coil value
bool get_coil(modbus_slave_t *slave, uint16_t address) {
    uint16_t byte_idx = address / 8;
    uint8_t bit_idx = address % 8;
    return (slave->coils[byte_idx] >> bit_idx) & 0x01;
}

void set_coil(modbus_slave_t *slave, uint16_t address, bool value) {
    uint16_t byte_idx = address / 8;
    uint8_t bit_idx = address % 8;
    
    if (value) {
        slave->coils[byte_idx] |= (1 << bit_idx);
    } else {
        slave->coils[byte_idx] &= ~(1 << bit_idx);
    }
}

// Build exception response
uint16_t build_exception_response(modbus_slave_t *slave, uint8_t function_code, 
                                   uint8_t exception_code) {
    slave->tx_buffer[0] = slave->slave_id;
    slave->tx_buffer[1] = function_code | 0x80;  // Set MSB for exception
    slave->tx_buffer[2] = exception_code;
    
    uint16_t crc = modbus_crc16(slave->tx_buffer, 3);
    slave->tx_buffer[3] = crc & 0xFF;
    slave->tx_buffer[4] = (crc >> 8) & 0xFF;
    
    return 5;
}

// Handle read coils (0x01)
uint16_t handle_read_coils(modbus_slave_t *slave, uint16_t start_addr, 
                            uint16_t quantity) {
    if (quantity < 1 || quantity > 2000) {
        return build_exception_response(slave, MB_FC_READ_COILS, 
                                        MB_EX_ILLEGAL_DATA_VALUE);
    }
    
    if (start_addr + quantity > MAX_COILS) {
        return build_exception_response(slave, MB_FC_READ_COILS, 
                                        MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    
    uint8_t byte_count = (quantity + 7) / 8;
    
    slave->tx_buffer[0] = slave->slave_id;
    slave->tx_buffer[1] = MB_FC_READ_COILS;
    slave->tx_buffer[2] = byte_count;
    
    // Pack coil values into bytes
    for (uint16_t i = 0; i < byte_count; i++) {
        slave->tx_buffer[3 + i] = 0;
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint16_t coil_idx = start_addr + i * 8 + bit;
            if (coil_idx < start_addr + quantity) {
                if (get_coil(slave, coil_idx)) {
                    slave->tx_buffer[3 + i] |= (1 << bit);
                }
            }
        }
    }
    
    uint16_t crc = modbus_crc16(slave->tx_buffer, 3 + byte_count);
    slave->tx_buffer[3 + byte_count] = crc & 0xFF;
    slave->tx_buffer[4 + byte_count] = (crc >> 8) & 0xFF;
    
    return 5 + byte_count;
}

// Handle read holding registers (0x03)
uint16_t handle_read_holding_registers(modbus_slave_t *slave, uint16_t start_addr,
                                        uint16_t quantity) {
    if (quantity < 1 || quantity > 125) {
        return build_exception_response(slave, MB_FC_READ_HOLDING_REGS,
                                        MB_EX_ILLEGAL_DATA_VALUE);
    }
    
    if (start_addr + quantity > MAX_HOLDING_REGS) {
        return build_exception_response(slave, MB_FC_READ_HOLDING_REGS,
                                        MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    
    uint8_t byte_count = quantity * 2;
    
    slave->tx_buffer[0] = slave->slave_id;
    slave->tx_buffer[1] = MB_FC_READ_HOLDING_REGS;
    slave->tx_buffer[2] = byte_count;
    
    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t value = slave->holding_registers[start_addr + i];
        slave->tx_buffer[3 + i * 2] = (value >> 8) & 0xFF;
        slave->tx_buffer[4 + i * 2] = value & 0xFF;
    }
    
    uint16_t crc = modbus_crc16(slave->tx_buffer, 3 + byte_count);
    slave->tx_buffer[3 + byte_count] = crc & 0xFF;
    slave->tx_buffer[4 + byte_count] = (crc >> 8) & 0xFF;
    
    return 5 + byte_count;
}

// Handle write single coil (0x05)
uint16_t handle_write_single_coil(modbus_slave_t *slave, uint16_t address,
                                   uint16_t value) {
    if (address >= MAX_COILS) {
        return build_exception_response(slave, MB_FC_WRITE_SINGLE_COIL,
                                        MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    
    if (value != 0x0000 && value != 0xFF00) {
        return build_exception_response(slave, MB_FC_WRITE_SINGLE_COIL,
                                        MB_EX_ILLEGAL_DATA_VALUE);
    }
    
    set_coil(slave, address, value == 0xFF00);
    
    // Echo back the request
    memcpy(slave->tx_buffer, slave->rx_buffer, 6);
    uint16_t crc = modbus_crc16(slave->tx_buffer, 6);
    slave->tx_buffer[6] = crc & 0xFF;
    slave->tx_buffer[7] = (crc >> 8) & 0xFF;
    
    return 8;
}

// Handle write single register (0x06)
uint16_t handle_write_single_register(modbus_slave_t *slave, uint16_t address,
                                       uint16_t value) {
    if (address >= MAX_HOLDING_REGS) {
        return build_exception_response(slave, MB_FC_WRITE_SINGLE_REG,
                                        MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    
    slave->holding_registers[address] = value;
    
    // Echo back the request
    memcpy(slave->tx_buffer, slave->rx_buffer, 6);
    uint16_t crc = modbus_crc16(slave->tx_buffer, 6);
    slave->tx_buffer[6] = crc & 0xFF;
    slave->tx_buffer[7] = (crc >> 8) & 0xFF;
    
    return 8;
}

// Process received Modbus frame
uint16_t modbus_slave_process(modbus_slave_t *slave) {
    // Verify minimum length
    if (slave->rx_length < 4) {
        return 0;  // Invalid frame, no response
    }
    
    // Check CRC
    uint16_t received_crc = slave->rx_buffer[slave->rx_length - 2] |
                           (slave->rx_buffer[slave->rx_length - 1] << 8);
    uint16_t calculated_crc = modbus_crc16(slave->rx_buffer, slave->rx_length - 2);
    
    if (received_crc != calculated_crc) {
        return 0;  // CRC error, no response
    }
    
    // Check slave address
    if (slave->rx_buffer[0] != slave->slave_id) {
        return 0;  // Not for this slave
    }
    
    uint8_t function_code = slave->rx_buffer[1];
    uint16_t start_addr = (slave->rx_buffer[2] << 8) | slave->rx_buffer[3];
    uint16_t quantity = (slave->rx_buffer[4] << 8) | slave->rx_buffer[5];
    
    switch (function_code) {
        case MB_FC_READ_COILS:
            return handle_read_coils(slave, start_addr, quantity);
            
        case MB_FC_READ_HOLDING_REGS:
            return handle_read_holding_registers(slave, start_addr, quantity);
            
        case MB_FC_WRITE_SINGLE_COIL:
            return handle_write_single_coil(slave, start_addr, quantity);
            
        case MB_FC_WRITE_SINGLE_REG:
            return handle_write_single_register(slave, start_addr, quantity);
            
        default:
            return build_exception_response(slave, function_code,
                                           MB_EX_ILLEGAL_FUNCTION);
    }
}

// Initialize slave
void modbus_slave_init(modbus_slave_t *slave, uint8_t slave_id) {
    memset(slave, 0, sizeof(modbus_slave_t));
    slave->slave_id = slave_id;
}
```

## Rust Implementation

Here's a safe, idiomatic Rust implementation:

```rust
use std::collections::HashMap;

// Function codes
const READ_COILS: u8 = 0x01;
const READ_DISCRETE_INPUTS: u8 = 0x02;
const READ_HOLDING_REGISTERS: u8 = 0x03;
const READ_INPUT_REGISTERS: u8 = 0x04;
const WRITE_SINGLE_COIL: u8 = 0x05;
const WRITE_SINGLE_REGISTER: u8 = 0x06;
const WRITE_MULTIPLE_COILS: u8 = 0x0F;
const WRITE_MULTIPLE_REGISTERS: u8 = 0x10;

// Exception codes
const EX_ILLEGAL_FUNCTION: u8 = 0x01;
const EX_ILLEGAL_DATA_ADDRESS: u8 = 0x02;
const EX_ILLEGAL_DATA_VALUE: u8 = 0x03;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ModbusError {
    IllegalFunction,
    IllegalDataAddress,
    IllegalDataValue,
    CrcError,
    InvalidFrame,
}

pub struct ModbusSlave {
    slave_id: u8,
    coils: HashMap<u16, bool>,
    discrete_inputs: HashMap<u16, bool>,
    holding_registers: HashMap<u16, u16>,
    input_registers: HashMap<u16, u16>,
}

impl ModbusSlave {
    pub fn new(slave_id: u8) -> Self {
        Self {
            slave_id,
            coils: HashMap::new(),
            discrete_inputs: HashMap::new(),
            holding_registers: HashMap::new(),
            input_registers: HashMap::new(),
        }
    }
    
    // CRC-16 Modbus calculation
    fn calculate_crc(data: &[u8]) -> u16 {
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
    
    // Set coil value
    pub fn set_coil(&mut self, address: u16, value: bool) {
        self.coils.insert(address, value);
    }
    
    // Get coil value
    pub fn get_coil(&self, address: u16) -> Option<bool> {
        self.coils.get(&address).copied()
    }
    
    // Set holding register
    pub fn set_holding_register(&mut self, address: u16, value: u16) {
        self.holding_registers.insert(address, value);
    }
    
    // Get holding register
    pub fn get_holding_register(&self, address: u16) -> Option<u16> {
        self.holding_registers.get(&address).copied()
    }
    
    // Set input register
    pub fn set_input_register(&mut self, address: u16, value: u16) {
        self.input_registers.insert(address, value);
    }
    
    // Build exception response
    fn build_exception(&self, function_code: u8, exception_code: u8) -> Vec<u8> {
        let mut response = Vec::new();
        response.push(self.slave_id);
        response.push(function_code | 0x80);
        response.push(exception_code);
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        response
    }
    
    // Handle read coils
    fn handle_read_coils(&self, start_address: u16, quantity: u16) -> Result<Vec<u8>, ModbusError> {
        if quantity < 1 || quantity > 2000 {
            return Err(ModbusError::IllegalDataValue);
        }
        
        let byte_count = ((quantity + 7) / 8) as u8;
        let mut response = Vec::new();
        
        response.push(self.slave_id);
        response.push(READ_COILS);
        response.push(byte_count);
        
        // Pack coils into bytes
        for byte_idx in 0..byte_count {
            let mut byte_value = 0u8;
            for bit_idx in 0..8 {
                let coil_address = start_address + (byte_idx as u16) * 8 + (bit_idx as u16);
                if coil_address < start_address + quantity {
                    if self.get_coil(coil_address).unwrap_or(false) {
                        byte_value |= 1 << bit_idx;
                    }
                }
            }
            response.push(byte_value);
        }
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    // Handle read holding registers
    fn handle_read_holding_registers(&self, start_address: u16, quantity: u16) 
        -> Result<Vec<u8>, ModbusError> {
        if quantity < 1 || quantity > 125 {
            return Err(ModbusError::IllegalDataValue);
        }
        
        let byte_count = (quantity * 2) as u8;
        let mut response = Vec::new();
        
        response.push(self.slave_id);
        response.push(READ_HOLDING_REGISTERS);
        response.push(byte_count);
        
        for i in 0..quantity {
            let address = start_address + i;
            let value = self.get_holding_register(address).unwrap_or(0);
            response.push((value >> 8) as u8);
            response.push((value & 0xFF) as u8);
        }
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    // Handle write single coil
    fn handle_write_single_coil(&mut self, address: u16, value: u16) 
        -> Result<Vec<u8>, ModbusError> {
        if value != 0x0000 && value != 0xFF00 {
            return Err(ModbusError::IllegalDataValue);
        }
        
        self.set_coil(address, value == 0xFF00);
        
        // Echo response
        let mut response = Vec::new();
        response.push(self.slave_id);
        response.push(WRITE_SINGLE_COIL);
        response.push((address >> 8) as u8);
        response.push((address & 0xFF) as u8);
        response.push((value >> 8) as u8);
        response.push((value & 0xFF) as u8);
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    // Handle write single register
    fn handle_write_single_register(&mut self, address: u16, value: u16) 
        -> Result<Vec<u8>, ModbusError> {
        self.set_holding_register(address, value);
        
        // Echo response
        let mut response = Vec::new();
        response.push(self.slave_id);
        response.push(WRITE_SINGLE_REGISTER);
        response.push((address >> 8) as u8);
        response.push((address & 0xFF) as u8);
        response.push((value >> 8) as u8);
        response.push((value & 0xFF) as u8);
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    // Handle write multiple registers
    fn handle_write_multiple_registers(&mut self, frame: &[u8]) 
        -> Result<Vec<u8>, ModbusError> {
        if frame.len() < 9 {
            return Err(ModbusError::InvalidFrame);
        }
        
        let start_address = ((frame[2] as u16) << 8) | (frame[3] as u16);
        let quantity = ((frame[4] as u16) << 8) | (frame[5] as u16);
        let byte_count = frame[6];
        
        if quantity < 1 || quantity > 123 || byte_count != (quantity * 2) as u8 {
            return Err(ModbusError::IllegalDataValue);
        }
        
        // Write registers
        for i in 0..quantity {
            let offset = 7 + (i * 2) as usize;
            let value = ((frame[offset] as u16) << 8) | (frame[offset + 1] as u16);
            self.set_holding_register(start_address + i, value);
        }
        
        // Build response
        let mut response = Vec::new();
        response.push(self.slave_id);
        response.push(WRITE_MULTIPLE_REGISTERS);
        response.push((start_address >> 8) as u8);
        response.push((start_address & 0xFF) as u8);
        response.push((quantity >> 8) as u8);
        response.push((quantity & 0xFF) as u8);
        
        let crc = Self::calculate_crc(&response);
        response.push((crc & 0xFF) as u8);
        response.push((crc >> 8) as u8);
        
        Ok(response)
    }
    
    // Process incoming frame
    pub fn process_request(&mut self, frame: &[u8]) -> Option<Vec<u8>> {
        // Validate frame
        if frame.len() < 4 {
            return None;
        }
        
        // Verify CRC
        let received_crc = (frame[frame.len() - 2] as u16) | 
                          ((frame[frame.len() - 1] as u16) << 8);
        let calculated_crc = Self::calculate_crc(&frame[..frame.len() - 2]);
        
        if received_crc != calculated_crc {
            return None;
        }
        
        // Check slave ID
        if frame[0] != self.slave_id {
            return None;
        }
        
        let function_code = frame[1];
        
        let result = match function_code {
            READ_COILS => {
                let start_addr = ((frame[2] as u16) << 8) | (frame[3] as u16);
                let quantity = ((frame[4] as u16) << 8) | (frame[5] as u16);
                self.handle_read_coils(start_addr, quantity)
            },
            READ_HOLDING_REGISTERS => {
                let start_addr = ((frame[2] as u16) << 8) | (frame[3] as u16);
                let quantity = ((frame[4] as u16) << 8) | (frame[5] as u16);
                self.handle_read_holding_registers(start_addr, quantity)
            },
            WRITE_SINGLE_COIL => {
                let address = ((frame[2] as u16) << 8) | (frame[3] as u16);
                let value = ((frame[4] as u16) << 8) | (frame[5] as u16);
                self.handle_write_single_coil(address, value)
            },
            WRITE_SINGLE_REGISTER => {
                let address = ((frame[2] as u16) << 8) | (frame[3] as u16);
                let value = ((frame[4] as u16) << 8) | (frame[5] as u16);
                self.handle_write_single_register(address, value)
            },
            WRITE_MULTIPLE_REGISTERS => {
                self.handle_write_multiple_registers(frame)
            },
            _ => Err(ModbusError::IllegalFunction),
        };
        
        match result {
            Ok(response) => Some(response),
            Err(error) => {
                let exception_code = match error {
                    ModbusError::IllegalFunction => EX_ILLEGAL_FUNCTION,
                    ModbusError::IllegalDataAddress => EX_ILLEGAL_DATA_ADDRESS,
                    ModbusError::IllegalDataValue => EX_ILLEGAL_DATA_VALUE,
                    _ => return None,
                };
                Some(self.build_exception(function_code, exception_code))
            }
        }
    }
}

// Example usage
fn main() {
    let mut slave = ModbusSlave::new(1);
    
    // Initialize some data
    slave.set_holding_register(0, 100);
    slave.set_holding_register(1, 200);
    slave.set_coil(0, true);
    
    // Simulate a read holding registers request (FC 03)
    let request = vec![
        0x01,       // Slave ID
        0x03,       // Function code
        0x00, 0x00, // Start address
        0x00, 0x02, // Quantity
        0xC4, 0x0B  // CRC
    ];
    
    if let Some(response) = slave.process_request(&request) {
        println!("Response: {:02X?}", response);
    }
}
```

## Summary

Implementing a Modbus slave/server involves creating a system that maintains the standard Modbus data model and responds appropriately to master requests. Key implementation requirements include proper frame validation with CRC checking, maintaining separate storage for coils, discrete inputs, holding registers, and input registers, and generating correct responses or exceptions based on the request validity.

The C implementation demonstrates low-level control suitable for embedded systems with packed bit storage and minimal memory overhead. The Rust implementation provides memory safety through ownership semantics, uses HashMap for flexible data storage, and leverages the type system to prevent common errors. Both implementations handle the core function codes for reading and writing data while properly generating exception responses for invalid requests.

A production-ready slave would typically add serial/TCP communication layers, configurable data ranges, callback mechanisms for application-specific processing, and timing compliance with the Modbus specification's inter-frame delays and response timeouts.