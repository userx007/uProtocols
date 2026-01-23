# DP Configuration Data in Profibus

## Overview

DP Configuration Data is a critical component of Profibus DP (Decentralized Periphery) communication that defines how DP slaves are structured and how they communicate with the DP master. This configuration data tells the master what modules are installed in each slave, what data formats to expect, and how to interpret the I/O data exchanged during cyclic communication.

## What is DP Configuration Data?

Configuration data is a byte array sent from the DP master to each DP slave during the parameterization phase (before cyclic data exchange begins). It describes:

- **Module Configuration**: Which modules are plugged into the slave device
- **Data Length**: How many input and output bytes each module provides
- **Module Types**: The specific type identifiers for each module
- **Slot Assignment**: The physical or logical position of each module

The configuration data follows a specific format defined in the Profibus DP standard (IEC 61158/61784). Each module is represented by an identifier byte that encodes both the module type and the data length.

## Configuration Data Format

### Identifier Byte Structure

Each module in the configuration is represented by one or more identifier bytes:

```
Bit 7-6: Format identifier
  00 = Special format
  01 = Reserved
  10 = Compact format (1 byte per module)
  11 = Reserved

Bit 5-4: Consistency over (for compact format)
  00 = 1 byte
  01 = 2 bytes
  10 = 4 bytes
  11 = Reserved

Bit 3-0: Data length or type code
```

**Compact Format** (most common): Single byte per module, bits 3-0 indicate number of bytes (input or output)

**Special Format**: Uses multiple bytes for complex module descriptions

### Common Module Types

- `0x10` - 1 byte input
- `0x20` - 2 bytes input
- `0x30` - 3 bytes input
- `0x11` - 1 byte output
- `0x21` - 2 bytes output
- `0xF0` - Input/Output combined (followed by length bytes)

## C/C++ Implementation

Here's a comprehensive example of working with DP configuration data in C:

```c
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Configuration data format identifiers
#define CFG_FORMAT_SPECIAL  0x00
#define CFG_FORMAT_COMPACT  0x80
#define CFG_FORMAT_MASK     0xC0

// Consistency indicators for compact format
#define CFG_CONSISTENCY_1   0x00
#define CFG_CONSISTENCY_2   0x10
#define CFG_CONSISTENCY_4   0x20
#define CFG_CONSISTENCY_MASK 0x30

// Direction indicators
#define CFG_DIRECTION_INPUT  0x00
#define CFG_DIRECTION_OUTPUT 0x01
#define CFG_DIRECTION_MASK   0x08

// Module configuration structure
typedef struct {
    uint8_t identifier;
    uint8_t input_length;
    uint8_t output_length;
    uint8_t consistency;
} dp_module_config_t;

// Slave configuration structure
typedef struct {
    uint8_t station_address;
    uint8_t config_data[244];  // Maximum configuration data length
    uint16_t config_length;
    uint16_t total_input_length;
    uint16_t total_output_length;
} dp_slave_config_t;

// Parse a configuration identifier byte
int parse_config_identifier(uint8_t id_byte, dp_module_config_t *module) {
    uint8_t format = id_byte & CFG_FORMAT_MASK;
    
    if (format == CFG_FORMAT_COMPACT) {
        // Compact format
        module->identifier = id_byte;
        module->consistency = (id_byte & CFG_CONSISTENCY_MASK) >> 4;
        
        // Determine if input or output
        if (id_byte & CFG_DIRECTION_MASK) {
            // Output module
            module->output_length = id_byte & 0x0F;
            module->input_length = 0;
        } else {
            // Input module
            module->input_length = id_byte & 0x0F;
            module->output_length = 0;
        }
        return 1;  // 1 byte consumed
    } else if (format == CFG_FORMAT_SPECIAL) {
        // Special format (simplified - full implementation more complex)
        module->identifier = id_byte;
        return -1;  // Need more bytes - implementation dependent
    }
    
    return 0;  // Unknown format
}

// Build configuration data for a slave
int build_slave_configuration(dp_slave_config_t *slave, 
                              dp_module_config_t *modules,
                              int module_count) {
    uint16_t offset = 0;
    slave->total_input_length = 0;
    slave->total_output_length = 0;
    
    for (int i = 0; i < module_count; i++) {
        if (offset >= sizeof(slave->config_data)) {
            return -1;  // Configuration too large
        }
        
        // Add module identifier to configuration
        slave->config_data[offset++] = modules[i].identifier;
        
        // Accumulate total I/O lengths
        slave->total_input_length += modules[i].input_length;
        slave->total_output_length += modules[i].output_length;
    }
    
    slave->config_length = offset;
    return 0;
}

// Example: Create configuration for a typical slave
void create_example_configuration(dp_slave_config_t *slave) {
    // Define modules for this slave
    dp_module_config_t modules[] = {
        {0x10, 1, 0, 0},  // 1 byte digital input
        {0x10, 1, 0, 0},  // 1 byte digital input
        {0x11, 0, 1, 0},  // 1 byte digital output
        {0x20, 2, 0, 0},  // 2 bytes analog input
        {0x21, 0, 2, 0}   // 2 bytes analog output
    };
    
    slave->station_address = 3;
    build_slave_configuration(slave, modules, 5);
    
    printf("Slave %d configuration:\n", slave->station_address);
    printf("  Config length: %d bytes\n", slave->config_length);
    printf("  Total input: %d bytes\n", slave->total_input_length);
    printf("  Total output: %d bytes\n", slave->total_output_length);
    printf("  Config data: ");
    for (int i = 0; i < slave->config_length; i++) {
        printf("0x%02X ", slave->config_data[i]);
    }
    printf("\n");
}

// Validate received configuration against expected
int validate_configuration(dp_slave_config_t *expected,
                          uint8_t *received_config,
                          uint16_t received_length) {
    if (received_length != expected->config_length) {
        return 0;  // Length mismatch
    }
    
    return memcmp(expected->config_data, received_config, 
                  received_length) == 0;
}
```

## Rust Implementation

Here's an equivalent implementation in Rust with better type safety and error handling:

```rust
use std::fmt;

// Configuration format constants
const CFG_FORMAT_COMPACT: u8 = 0x80;
const CFG_FORMAT_MASK: u8 = 0xC0;
const CFG_CONSISTENCY_MASK: u8 = 0x30;
const CFG_DIRECTION_MASK: u8 = 0x08;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Consistency {
    Byte1 = 0,
    Byte2 = 1,
    Byte4 = 2,
}

#[derive(Debug, Clone, Copy)]
pub enum ModuleDirection {
    Input,
    Output,
}

#[derive(Debug, Clone)]
pub struct ModuleConfig {
    pub identifier: u8,
    pub input_length: u8,
    pub output_length: u8,
    pub consistency: Consistency,
}

impl ModuleConfig {
    // Create a new input module configuration
    pub fn new_input(bytes: u8, consistency: Consistency) -> Self {
        let cons_bits = (consistency as u8) << 4;
        let identifier = CFG_FORMAT_COMPACT | cons_bits | bytes;
        
        ModuleConfig {
            identifier,
            input_length: bytes,
            output_length: 0,
            consistency,
        }
    }
    
    // Create a new output module configuration
    pub fn new_output(bytes: u8, consistency: Consistency) -> Self {
        let cons_bits = (consistency as u8) << 4;
        let identifier = CFG_FORMAT_COMPACT | cons_bits | CFG_DIRECTION_MASK | bytes;
        
        ModuleConfig {
            identifier,
            input_length: 0,
            output_length: bytes,
            consistency,
        }
    }
    
    // Parse a configuration identifier byte
    pub fn from_identifier(id_byte: u8) -> Result<Self, String> {
        let format = id_byte & CFG_FORMAT_MASK;
        
        if format == CFG_FORMAT_COMPACT {
            let cons_value = (id_byte & CFG_CONSISTENCY_MASK) >> 4;
            let consistency = match cons_value {
                0 => Consistency::Byte1,
                1 => Consistency::Byte2,
                2 => Consistency::Byte4,
                _ => return Err("Invalid consistency value".to_string()),
            };
            
            let is_output = (id_byte & CFG_DIRECTION_MASK) != 0;
            let length = id_byte & 0x0F;
            
            if is_output {
                Ok(ModuleConfig {
                    identifier: id_byte,
                    input_length: 0,
                    output_length: length,
                    consistency,
                })
            } else {
                Ok(ModuleConfig {
                    identifier: id_byte,
                    input_length: length,
                    output_length: 0,
                    consistency,
                })
            }
        } else {
            Err("Unsupported configuration format".to_string())
        }
    }
}

#[derive(Debug, Clone)]
pub struct SlaveConfig {
    pub station_address: u8,
    pub config_data: Vec<u8>,
    pub total_input_length: u16,
    pub total_output_length: u16,
}

impl SlaveConfig {
    // Create a new slave configuration
    pub fn new(station_address: u8) -> Self {
        SlaveConfig {
            station_address,
            config_data: Vec::new(),
            total_input_length: 0,
            total_output_length: 0,
        }
    }
    
    // Add a module to the configuration
    pub fn add_module(&mut self, module: &ModuleConfig) -> Result<(), String> {
        if self.config_data.len() >= 244 {
            return Err("Configuration data too large".to_string());
        }
        
        self.config_data.push(module.identifier);
        self.total_input_length += module.input_length as u16;
        self.total_output_length += module.output_length as u16;
        
        Ok(())
    }
    
    // Build configuration from a list of modules
    pub fn build(station_address: u8, modules: &[ModuleConfig]) -> Result<Self, String> {
        let mut config = SlaveConfig::new(station_address);
        
        for module in modules {
            config.add_module(module)?;
        }
        
        Ok(config)
    }
    
    // Validate received configuration data
    pub fn validate(&self, received: &[u8]) -> bool {
        self.config_data == received
    }
    
    // Get configuration as byte slice
    pub fn as_bytes(&self) -> &[u8] {
        &self.config_data
    }
}

impl fmt::Display for SlaveConfig {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "Slave {} configuration:", self.station_address)?;
        writeln!(f, "  Config length: {} bytes", self.config_data.len())?;
        writeln!(f, "  Total input: {} bytes", self.total_input_length)?;
        writeln!(f, "  Total output: {} bytes", self.total_output_length)?;
        write!(f, "  Config data: ")?;
        for byte in &self.config_data {
            write!(f, "0x{:02X} ", byte)?;
        }
        Ok(())
    }
}

// Example usage
fn create_example_configuration() -> SlaveConfig {
    let modules = vec![
        ModuleConfig::new_input(1, Consistency::Byte1),   // 1 byte DI
        ModuleConfig::new_input(1, Consistency::Byte1),   // 1 byte DI
        ModuleConfig::new_output(1, Consistency::Byte1),  // 1 byte DO
        ModuleConfig::new_input(2, Consistency::Byte2),   // 2 bytes AI
        ModuleConfig::new_output(2, Consistency::Byte2),  // 2 bytes AO
    ];
    
    SlaveConfig::build(3, &modules).expect("Failed to build configuration")
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_module_config_creation() {
        let input_module = ModuleConfig::new_input(4, Consistency::Byte1);
        assert_eq!(input_module.input_length, 4);
        assert_eq!(input_module.output_length, 0);
        
        let output_module = ModuleConfig::new_output(2, Consistency::Byte2);
        assert_eq!(output_module.input_length, 0);
        assert_eq!(output_module.output_length, 2);
    }
    
    #[test]
    fn test_slave_config_build() {
        let config = create_example_configuration();
        assert_eq!(config.total_input_length, 4);  // 1+1+2
        assert_eq!(config.total_output_length, 3); // 1+2
        assert_eq!(config.config_data.len(), 5);
    }
    
    #[test]
    fn test_config_validation() {
        let config = create_example_configuration();
        let received = config.config_data.clone();
        assert!(config.validate(&received));
        
        let wrong_data = vec![0x10, 0x20];
        assert!(!config.validate(&wrong_data));
    }
}
```

## Key Concepts

### Configuration Process Flow

1. **Master startup**: Master reads GSD file to understand slave capabilities
2. **Parameterization**: Master sends configuration data to slave
3. **Verification**: Slave checks if configuration matches installed hardware
4. **Acknowledgment**: Slave responds with success or configuration fault
5. **Data exchange**: If successful, cyclic I/O exchange begins

### Consistency Lengths

Consistency ensures atomic data transfer for multi-byte values:
- **1-byte consistency**: Each byte transferred independently
- **2-byte consistency**: Word values (16-bit) transferred atomically
- **4-byte consistency**: Double-word values (32-bit) transferred atomically

### Configuration Faults

Common errors during configuration:
- Configuration length mismatch
- Module not present in expected slot
- Unsupported module type
- Total I/O length exceeds slave capacity

## Summary

DP Configuration Data is fundamental to Profibus DP operation, serving as the contract between master and slave for data exchange structure. It defines module arrangement, data lengths, and consistency requirements through a compact binary format. Proper configuration ensures reliable communication by matching the master's expectations with the slave's actual hardware setup. The configuration process happens during initialization before cyclic data exchange, and mismatches result in configuration faults that prevent the slave from entering data exchange mode. Understanding configuration data format and validation is essential for implementing robust Profibus DP masters and diagnosing communication issues.