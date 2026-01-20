# Data Model and Address Mapping in Modbus

## Overview

Modbus defines four distinct data types that represent different kinds of information in industrial control systems. Understanding how to organize and map these data types in memory is fundamental to implementing a robust Modbus server or client. The data model consists of:

1. **Coils** (read/write bits) - Discrete outputs
2. **Discrete Inputs** (read-only bits) - Discrete inputs
3. **Holding Registers** (read/write 16-bit words) - Analog outputs/configuration
4. **Input Registers** (read-only 16-bit words) - Analog inputs/sensor data

Each data type occupies its own address space starting from address 0, and proper memory organization is critical for efficient data access and protocol compliance.

## Address Spaces and Conventions

Modbus uses different address ranges for each data type, though the protocol itself uses zero-based addressing:

- **Coils**: Protocol addresses 0-65535 (traditional notation: 00001-09999)
- **Discrete Inputs**: Protocol addresses 0-65535 (traditional notation: 10001-19999)
- **Input Registers**: Protocol addresses 0-65535 (traditional notation: 30001-39999)
- **Holding Registers**: Protocol addresses 0-65535 (traditional notation: 40001-49999)

The traditional notation adds offsets (10000, 30000, 40000) to distinguish data types, but the actual protocol messages use zero-based addresses within each space.

## C/C++ Implementation

Here's a comprehensive implementation showing memory organization and address mapping:

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Define the size of each data space
#define MAX_COILS 1000
#define MAX_DISCRETE_INPUTS 1000
#define MAX_HOLDING_REGISTERS 1000
#define MAX_INPUT_REGISTERS 1000

// Modbus data model structure
typedef struct {
    // Coils - single bit read/write (outputs)
    uint8_t coils[MAX_COILS / 8];  // Packed bits
    
    // Discrete Inputs - single bit read-only (inputs)
    uint8_t discrete_inputs[MAX_DISCRETE_INPUTS / 8];  // Packed bits
    
    // Holding Registers - 16-bit read/write (analog outputs, configuration)
    uint16_t holding_registers[MAX_HOLDING_REGISTERS];
    
    // Input Registers - 16-bit read-only (analog inputs, sensor data)
    uint16_t input_registers[MAX_INPUT_REGISTERS];
} ModbusDataModel;

// Global data model instance
static ModbusDataModel modbus_data;

// Initialize the data model
void modbus_data_init(void) {
    memset(&modbus_data, 0, sizeof(ModbusDataModel));
}

// Coil operations (bit-level access)
bool modbus_get_coil(uint16_t address) {
    if (address >= MAX_COILS) return false;
    
    uint16_t byte_index = address / 8;
    uint8_t bit_index = address % 8;
    
    return (modbus_data.coils[byte_index] >> bit_index) & 0x01;
}

void modbus_set_coil(uint16_t address, bool value) {
    if (address >= MAX_COILS) return;
    
    uint16_t byte_index = address / 8;
    uint8_t bit_index = address % 8;
    
    if (value) {
        modbus_data.coils[byte_index] |= (1 << bit_index);
    } else {
        modbus_data.coils[byte_index] &= ~(1 << bit_index);
    }
}

// Discrete input operations (read-only bits)
bool modbus_get_discrete_input(uint16_t address) {
    if (address >= MAX_DISCRETE_INPUTS) return false;
    
    uint16_t byte_index = address / 8;
    uint8_t bit_index = address % 8;
    
    return (modbus_data.discrete_inputs[byte_index] >> bit_index) & 0x01;
}

void modbus_update_discrete_input(uint16_t address, bool value) {
    if (address >= MAX_DISCRETE_INPUTS) return;
    
    uint16_t byte_index = address / 8;
    uint8_t bit_index = address % 8;
    
    if (value) {
        modbus_data.discrete_inputs[byte_index] |= (1 << bit_index);
    } else {
        modbus_data.discrete_inputs[byte_index] &= ~(1 << bit_index);
    }
}

// Holding register operations (16-bit read/write)
uint16_t modbus_get_holding_register(uint16_t address) {
    if (address >= MAX_HOLDING_REGISTERS) return 0;
    return modbus_data.holding_registers[address];
}

void modbus_set_holding_register(uint16_t address, uint16_t value) {
    if (address >= MAX_HOLDING_REGISTERS) return;
    modbus_data.holding_registers[address] = value;
}

// Input register operations (16-bit read-only)
uint16_t modbus_get_input_register(uint16_t address) {
    if (address >= MAX_INPUT_REGISTERS) return 0;
    return modbus_data.input_registers[address];
}

void modbus_update_input_register(uint16_t address, uint16_t value) {
    if (address >= MAX_INPUT_REGISTERS) return;
    modbus_data.input_registers[address] = value;
}

// Example: Multi-register operations for 32-bit values
void modbus_set_holding_register_32(uint16_t address, uint32_t value) {
    if (address + 1 >= MAX_HOLDING_REGISTERS) return;
    
    // Big-endian storage (high word first)
    modbus_data.holding_registers[address] = (uint16_t)(value >> 16);
    modbus_data.holding_registers[address + 1] = (uint16_t)(value & 0xFFFF);
}

uint32_t modbus_get_holding_register_32(uint16_t address) {
    if (address + 1 >= MAX_HOLDING_REGISTERS) return 0;
    
    uint32_t high = modbus_data.holding_registers[address];
    uint32_t low = modbus_data.holding_registers[address + 1];
    
    return (high << 16) | low;
}

// Example usage
int main(void) {
    modbus_data_init();
    
    // Set some coils (digital outputs)
    modbus_set_coil(0, true);   // Motor start
    modbus_set_coil(1, false);  // Valve closed
    
    // Update discrete inputs (digital inputs from sensors)
    modbus_update_discrete_input(0, true);  // Emergency stop pressed
    modbus_update_discrete_input(1, false); // Door open sensor
    
    // Set holding registers (configuration/setpoints)
    modbus_set_holding_register(0, 2500);  // Temperature setpoint (25.00°C)
    modbus_set_holding_register_32(10, 123456789);  // 32-bit counter
    
    // Update input registers (sensor readings)
    modbus_update_input_register(0, 2475);  // Current temperature (24.75°C)
    modbus_update_input_register(1, 5000);  // Pressure reading
    
    // Read back values
    printf("Coil 0: %d\n", modbus_get_coil(0));
    printf("Discrete Input 0: %d\n", modbus_get_discrete_input(0));
    printf("Holding Register 0: %u\n", modbus_get_holding_register(0));
    printf("Input Register 0: %u\n", modbus_get_input_register(0));
    printf("32-bit Holding Register: %u\n", modbus_get_holding_register_32(10));
    
    return 0;
}
```

## Rust Implementation

Here's a modern, safe Rust implementation with proper error handling:

```rust
use std::error::Error;
use std::fmt;

const MAX_COILS: usize = 1000;
const MAX_DISCRETE_INPUTS: usize = 1000;
const MAX_HOLDING_REGISTERS: usize = 1000;
const MAX_INPUT_REGISTERS: usize = 1000;

#[derive(Debug)]
pub enum ModbusError {
    AddressOutOfRange,
    InvalidDataType,
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusError::AddressOutOfRange => write!(f, "Address out of range"),
            ModbusError::InvalidDataType => write!(f, "Invalid data type"),
        }
    }
}

impl Error for ModbusError {}

pub struct ModbusDataModel {
    // Packed bit arrays for efficiency
    coils: Vec<u8>,
    discrete_inputs: Vec<u8>,
    
    // 16-bit register arrays
    holding_registers: Vec<u16>,
    input_registers: Vec<u16>,
}

impl ModbusDataModel {
    pub fn new() -> Self {
        Self {
            coils: vec![0; MAX_COILS / 8],
            discrete_inputs: vec![0; MAX_DISCRETE_INPUTS / 8],
            holding_registers: vec![0; MAX_HOLDING_REGISTERS],
            input_registers: vec![0; MAX_INPUT_REGISTERS],
        }
    }
    
    // Coil operations
    pub fn get_coil(&self, address: u16) -> Result<bool, ModbusError> {
        let addr = address as usize;
        if addr >= MAX_COILS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        let byte_index = addr / 8;
        let bit_index = addr % 8;
        
        Ok((self.coils[byte_index] >> bit_index) & 0x01 != 0)
    }
    
    pub fn set_coil(&mut self, address: u16, value: bool) -> Result<(), ModbusError> {
        let addr = address as usize;
        if addr >= MAX_COILS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        let byte_index = addr / 8;
        let bit_index = addr % 8;
        
        if value {
            self.coils[byte_index] |= 1 << bit_index;
        } else {
            self.coils[byte_index] &= !(1 << bit_index);
        }
        
        Ok(())
    }
    
    // Discrete input operations
    pub fn get_discrete_input(&self, address: u16) -> Result<bool, ModbusError> {
        let addr = address as usize;
        if addr >= MAX_DISCRETE_INPUTS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        let byte_index = addr / 8;
        let bit_index = addr % 8;
        
        Ok((self.discrete_inputs[byte_index] >> bit_index) & 0x01 != 0)
    }
    
    pub fn update_discrete_input(&mut self, address: u16, value: bool) -> Result<(), ModbusError> {
        let addr = address as usize;
        if addr >= MAX_DISCRETE_INPUTS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        let byte_index = addr / 8;
        let bit_index = addr % 8;
        
        if value {
            self.discrete_inputs[byte_index] |= 1 << bit_index;
        } else {
            self.discrete_inputs[byte_index] &= !(1 << bit_index);
        }
        
        Ok(())
    }
    
    // Holding register operations
    pub fn get_holding_register(&self, address: u16) -> Result<u16, ModbusError> {
        let addr = address as usize;
        if addr >= MAX_HOLDING_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        Ok(self.holding_registers[addr])
    }
    
    pub fn set_holding_register(&mut self, address: u16, value: u16) -> Result<(), ModbusError> {
        let addr = address as usize;
        if addr >= MAX_HOLDING_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        self.holding_registers[addr] = value;
        Ok(())
    }
    
    // Input register operations
    pub fn get_input_register(&self, address: u16) -> Result<u16, ModbusError> {
        let addr = address as usize;
        if addr >= MAX_INPUT_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        Ok(self.input_registers[addr])
    }
    
    pub fn update_input_register(&mut self, address: u16, value: u16) -> Result<(), ModbusError> {
        let addr = address as usize;
        if addr >= MAX_INPUT_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        self.input_registers[addr] = value;
        Ok(())
    }
    
    // Multi-register operations for larger data types
    pub fn set_holding_register_32(&mut self, address: u16, value: u32) -> Result<(), ModbusError> {
        let addr = address as usize;
        if addr + 1 >= MAX_HOLDING_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        // Big-endian storage
        self.holding_registers[addr] = (value >> 16) as u16;
        self.holding_registers[addr + 1] = (value & 0xFFFF) as u16;
        
        Ok(())
    }
    
    pub fn get_holding_register_32(&self, address: u16) -> Result<u32, ModbusError> {
        let addr = address as usize;
        if addr + 1 >= MAX_HOLDING_REGISTERS {
            return Err(ModbusError::AddressOutOfRange);
        }
        
        let high = self.holding_registers[addr] as u32;
        let low = self.holding_registers[addr + 1] as u32;
        
        Ok((high << 16) | low)
    }
    
    // Float operations (IEEE 754)
    pub fn set_holding_register_float(&mut self, address: u16, value: f32) -> Result<(), ModbusError> {
        let bits = value.to_bits();
        self.set_holding_register_32(address, bits)
    }
    
    pub fn get_holding_register_float(&self, address: u16) -> Result<f32, ModbusError> {
        let bits = self.get_holding_register_32(address)?;
        Ok(f32::from_bits(bits))
    }
}

// Example application mapping
pub struct TemperatureController {
    data_model: ModbusDataModel,
}

impl TemperatureController {
    pub fn new() -> Self {
        Self {
            data_model: ModbusDataModel::new(),
        }
    }
    
    // Map specific addresses to application functions
    pub fn set_heater_enable(&mut self, enabled: bool) -> Result<(), ModbusError> {
        self.data_model.set_coil(0, enabled)
    }
    
    pub fn get_heater_enable(&self) -> Result<bool, ModbusError> {
        self.data_model.get_coil(0)
    }
    
    pub fn set_temperature_setpoint(&mut self, temp: f32) -> Result<(), ModbusError> {
        self.data_model.set_holding_register_float(0, temp)
    }
    
    pub fn get_temperature_setpoint(&self) -> Result<f32, ModbusError> {
        self.data_model.get_holding_register_float(0)
    }
    
    pub fn update_current_temperature(&mut self, temp: f32) -> Result<(), ModbusError> {
        // Convert to fixed point: multiply by 100 for 0.01 degree resolution
        let temp_scaled = (temp * 100.0) as u16;
        self.data_model.update_input_register(0, temp_scaled)
    }
    
    pub fn get_current_temperature(&self) -> Result<f32, ModbusError> {
        let temp_scaled = self.data_model.get_input_register(0)?;
        Ok(temp_scaled as f32 / 100.0)
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut controller = TemperatureController::new();
    
    // Configure the system
    controller.set_heater_enable(true)?;
    controller.set_temperature_setpoint(25.5)?;
    
    // Simulate sensor update
    controller.update_current_temperature(24.8)?;
    
    // Read back values
    println!("Heater enabled: {}", controller.get_heater_enable()?);
    println!("Setpoint: {:.2}°C", controller.get_temperature_setpoint()?);
    println!("Current temp: {:.2}°C", controller.get_current_temperature()?);
    
    // Direct data model access
    let mut data = ModbusDataModel::new();
    data.set_coil(5, true)?;
    data.set_holding_register(100, 0x1234)?;
    data.set_holding_register_32(200, 0xDEADBEEF)?;
    
    println!("Coil 5: {}", data.get_coil(5)?);
    println!("Holding Register 100: 0x{:04X}", data.get_holding_register(100)?);
    println!("32-bit value at 200: 0x{:08X}", data.get_holding_register_32(200)?);
    
    Ok(())
}
```

## Summary

The Modbus data model organizes four distinct memory spaces for different types of data in industrial control systems. Coils and discrete inputs handle single-bit digital data, while holding registers and input registers manage 16-bit analog and configuration data. Proper address mapping requires understanding zero-based protocol addressing versus traditional notation, efficient bit-packing for boolean values, and strategies for handling multi-register data types like 32-bit integers and floats.

The C implementation demonstrates low-level memory management with packed bit arrays and direct buffer manipulation, suitable for embedded systems with limited resources. The Rust implementation provides memory safety through ownership and bounds checking, with a type-safe error handling approach using Result types. Both implementations show how to build higher-level application abstractions on top of the raw data model, mapping specific Modbus addresses to meaningful application functions like temperature control or motor operation.

Key considerations include maintaining separate address spaces for each data type, implementing efficient bit-level operations for coils and discrete inputs, handling byte ordering for multi-register values, and creating clear application-level mappings that hide the complexity of raw address manipulation from higher-level code.