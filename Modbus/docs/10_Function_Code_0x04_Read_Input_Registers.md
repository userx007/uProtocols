# Function Code 0x04: Read Input Registers

## Overview

Modbus Function Code 0x04 (Read Input Registers) is used to read the contents of a contiguous block of 16-bit input registers from a Modbus slave device. Input registers are read-only registers typically used to store analog sensor data, measurement values, or status information that the slave device provides to the master but which the master cannot modify.

## Key Characteristics

- **Function Code**: 0x04
- **Data Type**: 16-bit registers (2 bytes each)
- **Access**: Read-only
- **Typical Use Cases**: 
  - Analog sensor readings (temperature, pressure, flow)
  - Measurement values from instruments
  - Process variables
  - Device status information
  - Real-time data acquisition

## Register Addressing

Input registers are typically addressed starting from 30001 in the Modbus protocol documentation (addresses 30001-39999), but in the actual protocol data unit (PDU), addresses start from 0x0000. The 30000 offset is a convention used in documentation only.

## Protocol Details

### Request Format (Master → Slave)

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x04 |
| Starting Address | 2 bytes | Address of first register (0x0000 to 0xFFFF) |
| Quantity of Registers | 2 bytes | Number of registers to read (1 to 125) |

### Response Format (Slave → Master)

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x04 |
| Byte Count | 1 byte | Number of data bytes to follow (2 × register count) |
| Register Values | N bytes | Register values (2 bytes per register, big-endian) |

### Error Response Format

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x84 (0x04 + 0x80) |
| Exception Code | 1 byte | Error code |

## Common Exception Codes

- **0x01**: Illegal Function
- **0x02**: Illegal Data Address
- **0x03**: Illegal Data Value
- **0x04**: Slave Device Failure

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MODBUS_FC_READ_INPUT_REGISTERS 0x04
#define MAX_REGISTERS 125
#define EXCEPTION_OFFSET 0x80

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

// Simulated input register storage
uint16_t input_registers[1000] = {0};

// Build a Read Input Registers request
int modbus_build_read_input_registers_request(uint8_t *buffer, 
                                               uint16_t start_addr, 
                                               uint16_t quantity) {
    if (quantity < 1 || quantity > MAX_REGISTERS) {
        return -1;
    }
    
    buffer[0] = MODBUS_FC_READ_INPUT_REGISTERS;
    buffer[1] = (start_addr >> 8) & 0xFF;  // Address high byte
    buffer[2] = start_addr & 0xFF;          // Address low byte
    buffer[3] = (quantity >> 8) & 0xFF;     // Quantity high byte
    buffer[4] = quantity & 0xFF;            // Quantity low byte
    
    return 5; // Return length of request
}

// Process Read Input Registers request (slave side)
int modbus_process_read_input_registers(const uint8_t *request, 
                                         uint8_t *response,
                                         uint16_t register_count) {
    uint16_t start_addr = (request[1] << 8) | request[2];
    uint16_t quantity = (request[3] << 8) | request[4];
    
    // Validate quantity
    if (quantity < 1 || quantity > MAX_REGISTERS) {
        response[0] = MODBUS_FC_READ_INPUT_REGISTERS | EXCEPTION_OFFSET;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }
    
    // Validate address range
    if (start_addr + quantity > register_count) {
        response[0] = MODBUS_FC_READ_INPUT_REGISTERS | EXCEPTION_OFFSET;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        return 2;
    }
    
    // Build response
    response[0] = MODBUS_FC_READ_INPUT_REGISTERS;
    response[1] = quantity * 2; // Byte count
    
    // Copy register values (big-endian)
    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t reg_value = input_registers[start_addr + i];
        response[2 + (i * 2)] = (reg_value >> 8) & 0xFF;     // High byte
        response[2 + (i * 2) + 1] = reg_value & 0xFF;        // Low byte
    }
    
    return 2 + (quantity * 2); // Return response length
}

// Parse Read Input Registers response (master side)
int modbus_parse_read_input_registers_response(const uint8_t *response,
                                                uint16_t *registers,
                                                int response_len) {
    // Check for exception
    if (response[0] == (MODBUS_FC_READ_INPUT_REGISTERS | EXCEPTION_OFFSET)) {
        printf("Exception: 0x%02X\n", response[1]);
        return -1;
    }
    
    uint8_t byte_count = response[1];
    uint16_t reg_count = byte_count / 2;
    
    // Validate response length
    if (response_len < 2 + byte_count) {
        return -1;
    }
    
    // Extract register values
    for (uint16_t i = 0; i < reg_count; i++) {
        registers[i] = (response[2 + (i * 2)] << 8) | 
                       response[2 + (i * 2) + 1];
    }
    
    return reg_count;
}

// Example: Simulate sensor data
void simulate_sensor_data() {
    // Temperature sensor (in 0.1°C units): 25.3°C
    input_registers[0] = 253;
    
    // Pressure sensor (in kPa): 101.325 kPa
    input_registers[1] = 10132;
    
    // Humidity sensor (in 0.1% units): 65.5%
    input_registers[2] = 655;
    
    // Flow rate (in L/min): 15.7 L/min
    input_registers[3] = 157;
}

int main() {
    uint8_t request[260];
    uint8_t response[260];
    uint16_t read_values[10];
    
    // Initialize sensor data
    simulate_sensor_data();
    
    // Master: Build request to read 4 registers starting at address 0
    int req_len = modbus_build_read_input_registers_request(request, 0, 4);
    printf("Request (%d bytes): ", req_len);
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Slave: Process request
    int resp_len = modbus_process_read_input_registers(request, response, 1000);
    printf("Response (%d bytes): ", resp_len);
    for (int i = 0; i < resp_len; i++) {
        printf("%02X ", response[i]);
    }
    printf("\n");
    
    // Master: Parse response
    int reg_count = modbus_parse_read_input_registers_response(response, 
                                                                read_values, 
                                                                resp_len);
    if (reg_count > 0) {
        printf("\nRead %d registers:\n", reg_count);
        printf("Temperature: %.1f°C\n", read_values[0] / 10.0);
        printf("Pressure: %.3f kPa\n", read_values[1] / 100.0);
        printf("Humidity: %.1f%%\n", read_values[2] / 10.0);
        printf("Flow Rate: %.1f L/min\n", read_values[3] / 10.0);
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

const MODBUS_FC_READ_INPUT_REGISTERS: u8 = 0x04;
const MAX_REGISTERS: u16 = 125;
const EXCEPTION_OFFSET: u8 = 0x80;

#[derive(Debug, Clone)]
enum ModbusException {
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
}

impl fmt::Display for ModbusException {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusException::IllegalFunction => write!(f, "Illegal Function"),
            ModbusException::IllegalDataAddress => write!(f, "Illegal Data Address"),
            ModbusException::IllegalDataValue => write!(f, "Illegal Data Value"),
        }
    }
}

impl Error for ModbusException {}

// Simulated input register storage
struct InputRegisterBank {
    registers: Vec<u16>,
}

impl InputRegisterBank {
    fn new(size: usize) -> Self {
        InputRegisterBank {
            registers: vec![0; size],
        }
    }
    
    fn set_register(&mut self, addr: u16, value: u16) {
        if (addr as usize) < self.registers.len() {
            self.registers[addr as usize] = value;
        }
    }
    
    fn get_register(&self, addr: u16) -> Option<u16> {
        self.registers.get(addr as usize).copied()
    }
}

// Build Read Input Registers request
fn build_read_input_registers_request(
    start_addr: u16,
    quantity: u16,
) -> Result<Vec<u8>, Box<dyn Error>> {
    if quantity < 1 || quantity > MAX_REGISTERS {
        return Err("Quantity out of range".into());
    }
    
    let mut buffer = Vec::new();
    buffer.push(MODBUS_FC_READ_INPUT_REGISTERS);
    buffer.extend_from_slice(&start_addr.to_be_bytes());
    buffer.extend_from_slice(&quantity.to_be_bytes());
    
    Ok(buffer)
}

// Process Read Input Registers request (slave side)
fn process_read_input_registers(
    request: &[u8],
    register_bank: &InputRegisterBank,
) -> Vec<u8> {
    if request.len() < 5 {
        return create_exception_response(ModbusException::IllegalDataValue);
    }
    
    let start_addr = u16::from_be_bytes([request[1], request[2]]);
    let quantity = u16::from_be_bytes([request[3], request[4]]);
    
    // Validate quantity
    if quantity < 1 || quantity > MAX_REGISTERS {
        return create_exception_response(ModbusException::IllegalDataValue);
    }
    
    // Validate address range
    if start_addr as usize + quantity as usize > register_bank.registers.len() {
        return create_exception_response(ModbusException::IllegalDataAddress);
    }
    
    // Build response
    let mut response = Vec::new();
    response.push(MODBUS_FC_READ_INPUT_REGISTERS);
    response.push((quantity * 2) as u8); // Byte count
    
    // Add register values (big-endian)
    for i in 0..quantity {
        if let Some(value) = register_bank.get_register(start_addr + i) {
            response.extend_from_slice(&value.to_be_bytes());
        }
    }
    
    response
}

// Create exception response
fn create_exception_response(exception: ModbusException) -> Vec<u8> {
    vec![
        MODBUS_FC_READ_INPUT_REGISTERS | EXCEPTION_OFFSET,
        exception as u8,
    ]
}

// Parse Read Input Registers response (master side)
fn parse_read_input_registers_response(
    response: &[u8],
) -> Result<Vec<u16>, Box<dyn Error>> {
    if response.is_empty() {
        return Err("Empty response".into());
    }
    
    // Check for exception
    if response[0] == (MODBUS_FC_READ_INPUT_REGISTERS | EXCEPTION_OFFSET) {
        if response.len() < 2 {
            return Err("Invalid exception response".into());
        }
        return Err(format!("Modbus exception: 0x{:02X}", response[1]).into());
    }
    
    if response.len() < 2 {
        return Err("Response too short".into());
    }
    
    let byte_count = response[1] as usize;
    let reg_count = byte_count / 2;
    
    if response.len() < 2 + byte_count {
        return Err("Incomplete response".into());
    }
    
    let mut registers = Vec::new();
    for i in 0..reg_count {
        let offset = 2 + (i * 2);
        let value = u16::from_be_bytes([response[offset], response[offset + 1]]);
        registers.push(value);
    }
    
    Ok(registers)
}

// Simulate sensor data
fn simulate_sensor_data(bank: &mut InputRegisterBank) {
    bank.set_register(0, 253);      // Temperature: 25.3°C (in 0.1°C units)
    bank.set_register(1, 10132);    // Pressure: 101.32 kPa (in 0.01 kPa units)
    bank.set_register(2, 655);      // Humidity: 65.5% (in 0.1% units)
    bank.set_register(3, 157);      // Flow rate: 15.7 L/min (in 0.1 L/min units)
}

fn main() -> Result<(), Box<dyn Error>> {
    // Initialize register bank and sensor data
    let mut register_bank = InputRegisterBank::new(1000);
    simulate_sensor_data(&mut register_bank);
    
    // Master: Build request to read 4 registers starting at address 0
    let request = build_read_input_registers_request(0, 4)?;
    println!("Request ({} bytes): {:02X?}", request.len(), request);
    
    // Slave: Process request
    let response = process_read_input_registers(&request, &register_bank);
    println!("Response ({} bytes): {:02X?}", response.len(), response);
    
    // Master: Parse response
    match parse_read_input_registers_response(&response) {
        Ok(registers) => {
            println!("\nRead {} registers:", registers.len());
            println!("Temperature: {:.1}°C", registers[0] as f32 / 10.0);
            println!("Pressure: {:.2} kPa", registers[1] as f32 / 100.0);
            println!("Humidity: {:.1}%", registers[2] as f32 / 10.0);
            println!("Flow Rate: {:.1} L/min", registers[3] as f32 / 10.0);
        }
        Err(e) => println!("Error parsing response: {}", e),
    }
    
    Ok(())
}
```

---

## Summary

**Function Code 0x04 (Read Input Registers)** is a fundamental Modbus command for reading read-only 16-bit registers containing sensor data and measurements. It allows a master device to retrieve multiple contiguous register values in a single transaction, making it efficient for data acquisition applications.

**Key Points:**
- Reads 1-125 input registers per request
- Each register is 16 bits (2 bytes), transmitted big-endian
- Input registers are read-only (unlike holding registers)
- Commonly used for analog sensor values and process variables
- Response includes byte count followed by register values
- Supports exception handling for invalid addresses or values
- Addresses in PDU start from 0x0000 (30001+ is documentation convention)

Both implementations demonstrate complete request/response handling, exception processing, and practical sensor data representation with proper scaling factors for real-world measurements.