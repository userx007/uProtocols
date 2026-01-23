# Profibus Identifier and Module ID

## Overview

In Profibus DP (Decentralized Periphery) networks, the **Ident_Number** is a crucial parameter used for device identification and configuration verification. This 16-bit identifier ensures that the correct slave device is connected at each station address and that the device matches the expected configuration defined by the master.

## What is Ident_Number?

The Ident_Number is a unique 16-bit identifier assigned to each Profibus device type by the manufacturer. It serves multiple purposes:

- **Device Type Identification**: Distinguishes between different device models and types
- **Configuration Verification**: The master compares the Ident_Number read from the slave against the expected value in the GSD (General Station Description) file
- **Safety Mechanism**: Prevents misconfiguration by detecting if the wrong device is connected at a station address
- **Module Configuration**: In modular devices, it helps verify that the correct modules are installed in the expected slots

## How It Works

During the initialization phase of a Profibus network:

1. The master sends a configuration request to each slave
2. The slave responds with its Ident_Number and other configuration data
3. The master compares the received Ident_Number with the expected value from the configuration
4. If there's a mismatch, the master reports a configuration error and may refuse to operate the slave
5. For modular devices, each module may have its own identification data

## Module ID and Modular Devices

Modular Profibus devices (like I/O systems with pluggable modules) extend this concept. Each module slot can have:

- **Module Ident_Number**: Identifies the specific module type
- **Module Length**: Specifies the data length (input/output bytes)
- **Module Configuration**: Defines the module's operating parameters

The master verifies not just the base device but the entire module configuration to ensure system integrity.

## C/C++ Implementation Examples

### Basic Ident_Number Structure

```c
#include <stdint.h>
#include <stdbool.h>

// Profibus Ident_Number structure
typedef struct {
    uint16_t ident_number;      // Device identification number
    uint8_t station_address;    // Profibus station address
    char device_name[32];       // Human-readable device name
} profibus_device_t;

// Module configuration structure
typedef struct {
    uint8_t slot_number;        // Module slot position
    uint16_t module_ident;      // Module identification
    uint8_t input_length;       // Input data length in bytes
    uint8_t output_length;      // Output data length in bytes
    uint8_t config_data[16];    // Module-specific configuration
} profibus_module_t;

// Complete device configuration
typedef struct {
    profibus_device_t device;
    profibus_module_t modules[8];  // Max 8 modules
    uint8_t module_count;
} profibus_config_t;
```

### Device Identification and Verification

```c
// Verify device Ident_Number
bool verify_device_ident(const profibus_device_t *expected,
                         const profibus_device_t *actual) {
    if (expected->ident_number != actual->ident_number) {
        fprintf(stderr, "Device Ident mismatch at station %d: "
                "expected 0x%04X, got 0x%04X\n",
                expected->station_address,
                expected->ident_number,
                actual->ident_number);
        return false;
    }
    return true;
}

// Read Ident_Number from device (simplified)
int read_device_ident(uint8_t station_addr, profibus_device_t *device) {
    // In real implementation, this would send a Profibus telegram
    // Here we simulate reading from the device
    
    uint8_t request_buffer[16];
    uint8_t response_buffer[32];
    
    // Build Read_Ident request telegram
    request_buffer[0] = 0x68;           // Start delimiter
    request_buffer[1] = 0x04;           // Length
    request_buffer[2] = 0x04;           // Repeated length
    request_buffer[3] = 0x68;           // Start delimiter
    request_buffer[4] = station_addr;    // Destination address
    request_buffer[5] = 0x00;           // Source address (master)
    request_buffer[6] = 0x7D;           // Function code: Read_Ident
    request_buffer[7] = 0x00;           // Checksum (simplified)
    request_buffer[8] = 0x16;           // End delimiter
    
    // Send request and receive response
    // send_profibus_telegram(request_buffer, 9);
    // int len = receive_profibus_telegram(response_buffer, 32);
    
    // Parse response (simulated)
    device->station_address = station_addr;
    device->ident_number = (response_buffer[10] << 8) | response_buffer[11];
    
    return 0;  // Success
}
```

### Module Configuration Verification

```c
// Verify complete module configuration
bool verify_module_config(const profibus_config_t *expected,
                          const profibus_config_t *actual) {
    // Check device base
    if (!verify_device_ident(&expected->device, &actual->device)) {
        return false;
    }
    
    // Check module count
    if (expected->module_count != actual->module_count) {
        fprintf(stderr, "Module count mismatch: expected %d, got %d\n",
                expected->module_count, actual->module_count);
        return false;
    }
    
    // Verify each module
    for (int i = 0; i < expected->module_count; i++) {
        const profibus_module_t *exp_mod = &expected->modules[i];
        const profibus_module_t *act_mod = &actual->modules[i];
        
        if (exp_mod->module_ident != act_mod->module_ident) {
            fprintf(stderr, "Module %d ident mismatch: "
                    "expected 0x%04X, got 0x%04X\n",
                    exp_mod->slot_number,
                    exp_mod->module_ident,
                    act_mod->module_ident);
            return false;
        }
        
        if (exp_mod->input_length != act_mod->input_length ||
            exp_mod->output_length != act_mod->output_length) {
            fprintf(stderr, "Module %d data length mismatch\n",
                    exp_mod->slot_number);
            return false;
        }
    }
    
    return true;
}
```

### Practical Configuration Example

```c
// Initialize expected configuration
void init_expected_config(profibus_config_t *config) {
    // Base device: ET200S I/O system
    config->device.ident_number = 0x809A;  // Example Siemens ET200S
    config->device.station_address = 5;
    strcpy(config->device.device_name, "ET200S_Station5");
    
    // Module 0: 8-channel digital input
    config->modules[0].slot_number = 0;
    config->modules[0].module_ident = 0x0001;
    config->modules[0].input_length = 1;    // 1 byte input
    config->modules[0].output_length = 0;   // No output
    
    // Module 1: 8-channel digital output
    config->modules[1].slot_number = 1;
    config->modules[1].module_ident = 0x0002;
    config->modules[1].input_length = 0;
    config->modules[1].output_length = 1;   // 1 byte output
    
    // Module 2: 4-channel analog input
    config->modules[2].slot_number = 2;
    config->modules[2].module_ident = 0x0010;
    config->modules[2].input_length = 8;    // 4 channels × 2 bytes
    config->modules[2].output_length = 0;
    
    config->module_count = 3;
}

// Master initialization routine
int initialize_profibus_station(uint8_t station_addr) {
    profibus_config_t expected_config;
    profibus_config_t actual_config;
    
    // Load expected configuration
    init_expected_config(&expected_config);
    
    // Read actual configuration from device
    read_device_ident(station_addr, &actual_config.device);
    
    // Read module configuration (simplified)
    // In practice, this involves reading diagnostic data
    actual_config.module_count = 3;  // Would be read from device
    
    // Verify configuration
    if (!verify_module_config(&expected_config, &actual_config)) {
        fprintf(stderr, "Configuration verification failed!\n");
        return -1;
    }
    
    printf("Station %d verified successfully\n", station_addr);
    return 0;
}
```

## Rust Implementation Examples

### Type-Safe Structures

```rust
use std::fmt;

// Device Ident_Number representation
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IdentNumber(u16);

impl IdentNumber {
    pub fn new(value: u16) -> Self {
        IdentNumber(value)
    }
    
    pub fn value(&self) -> u16 {
        self.0
    }
}

impl fmt::Display for IdentNumber {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:04X}", self.0)
    }
}

// Profibus device structure
#[derive(Debug, Clone)]
pub struct ProfibusDevice {
    pub ident_number: IdentNumber,
    pub station_address: u8,
    pub device_name: String,
}

// Module configuration
#[derive(Debug, Clone, PartialEq)]
pub struct ProfibusModule {
    pub slot_number: u8,
    pub module_ident: IdentNumber,
    pub input_length: u8,
    pub output_length: u8,
    pub config_data: Vec<u8>,
}

// Complete configuration
#[derive(Debug, Clone)]
pub struct ProfibusConfig {
    pub device: ProfibusDevice,
    pub modules: Vec<ProfibusModule>,
}
```

### Configuration Verification with Error Handling

```rust
use thiserror::Error;

#[derive(Error, Debug)]
pub enum ConfigError {
    #[error("Device Ident mismatch at station {station}: expected {expected}, got {actual}")]
    DeviceIdentMismatch {
        station: u8,
        expected: IdentNumber,
        actual: IdentNumber,
    },
    
    #[error("Module count mismatch: expected {expected}, got {actual}")]
    ModuleCountMismatch { expected: usize, actual: usize },
    
    #[error("Module {slot} ident mismatch: expected {expected}, got {actual}")]
    ModuleIdentMismatch {
        slot: u8,
        expected: IdentNumber,
        actual: IdentNumber,
    },
    
    #[error("Module {slot} data length mismatch")]
    ModuleLengthMismatch { slot: u8 },
    
    #[error("Communication error: {0}")]
    CommunicationError(String),
}

pub type Result<T> = std::result::Result<T, ConfigError>;

// Verify device identification
pub fn verify_device_ident(
    expected: &ProfibusDevice,
    actual: &ProfibusDevice,
) -> Result<()> {
    if expected.ident_number != actual.ident_number {
        return Err(ConfigError::DeviceIdentMismatch {
            station: expected.station_address,
            expected: expected.ident_number,
            actual: actual.ident_number,
        });
    }
    Ok(())
}

// Verify module configuration
pub fn verify_module(
    expected: &ProfibusModule,
    actual: &ProfibusModule,
) -> Result<()> {
    if expected.module_ident != actual.module_ident {
        return Err(ConfigError::ModuleIdentMismatch {
            slot: expected.slot_number,
            expected: expected.module_ident,
            actual: actual.module_ident,
        });
    }
    
    if expected.input_length != actual.input_length
        || expected.output_length != actual.output_length
    {
        return Err(ConfigError::ModuleLengthMismatch {
            slot: expected.slot_number,
        });
    }
    
    Ok(())
}

// Verify complete configuration
pub fn verify_config(
    expected: &ProfibusConfig,
    actual: &ProfibusConfig,
) -> Result<()> {
    // Verify base device
    verify_device_ident(&expected.device, &actual.device)?;
    
    // Check module count
    if expected.modules.len() != actual.modules.len() {
        return Err(ConfigError::ModuleCountMismatch {
            expected: expected.modules.len(),
            actual: actual.modules.len(),
        });
    }
    
    // Verify each module
    for (exp_mod, act_mod) in expected.modules.iter().zip(actual.modules.iter()) {
        verify_module(exp_mod, act_mod)?;
    }
    
    Ok(())
}
```

### Async Configuration Reader

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};

pub struct ProfibusInterface {
    // Connection details would be here
}

impl ProfibusInterface {
    pub async fn read_device_ident(
        &mut self,
        station_addr: u8,
    ) -> Result<ProfibusDevice> {
        // Build Read_Ident telegram
        let request = vec![
            0x68,           // Start delimiter
            0x04,           // Length
            0x04,           // Repeated length
            0x68,           // Start delimiter
            station_addr,   // Destination
            0x00,           // Source (master)
            0x7D,           // Function: Read_Ident
            0x00,           // Checksum placeholder
            0x16,           // End delimiter
        ];
        
        // Send request (simplified)
        // self.write_all(&request).await?;
        
        // Receive response (simplified)
        let mut response = vec![0u8; 32];
        // self.read_exact(&mut response).await?;
        
        // Parse Ident_Number from response
        let ident_number = IdentNumber::new(
            (response[10] as u16) << 8 | response[11] as u16
        );
        
        Ok(ProfibusDevice {
            ident_number,
            station_address: station_addr,
            device_name: format!("Device_{}", station_addr),
        })
    }
    
    pub async fn read_module_config(
        &mut self,
        station_addr: u8,
    ) -> Result<Vec<ProfibusModule>> {
        // Read diagnostic data to get module configuration
        // This is simplified - real implementation would parse
        // the diagnostic telegram properly
        
        let mut modules = Vec::new();
        
        // Example: Read 3 modules
        for slot in 0..3 {
            let module = ProfibusModule {
                slot_number: slot,
                module_ident: IdentNumber::new(0x0001 + slot as u16),
                input_length: if slot == 0 { 1 } else { 0 },
                output_length: if slot == 1 { 1 } else { 0 },
                config_data: Vec::new(),
            };
            modules.push(module);
        }
        
        Ok(modules)
    }
}
```

### Builder Pattern for Configuration

```rust
pub struct ConfigBuilder {
    config: ProfibusConfig,
}

impl ConfigBuilder {
    pub fn new(ident_number: u16, station_address: u8) -> Self {
        ConfigBuilder {
            config: ProfibusConfig {
                device: ProfibusDevice {
                    ident_number: IdentNumber::new(ident_number),
                    station_address,
                    device_name: String::new(),
                },
                modules: Vec::new(),
            },
        }
    }
    
    pub fn device_name(mut self, name: impl Into<String>) -> Self {
        self.config.device.device_name = name.into();
        self
    }
    
    pub fn add_digital_input(mut self, slot: u8, channels: u8) -> Self {
        self.config.modules.push(ProfibusModule {
            slot_number: slot,
            module_ident: IdentNumber::new(0x0001),
            input_length: (channels + 7) / 8,  // Bytes needed
            output_length: 0,
            config_data: Vec::new(),
        });
        self
    }
    
    pub fn add_digital_output(mut self, slot: u8, channels: u8) -> Self {
        self.config.modules.push(ProfibusModule {
            slot_number: slot,
            module_ident: IdentNumber::new(0x0002),
            input_length: 0,
            output_length: (channels + 7) / 8,
            config_data: Vec::new(),
        });
        self
    }
    
    pub fn add_analog_input(mut self, slot: u8, channels: u8) -> Self {
        self.config.modules.push(ProfibusModule {
            slot_number: slot,
            module_ident: IdentNumber::new(0x0010),
            input_length: channels * 2,  // 2 bytes per channel
            output_length: 0,
            config_data: Vec::new(),
        });
        self
    }
    
    pub fn build(self) -> ProfibusConfig {
        self.config
    }
}

// Usage example
fn create_example_config() -> ProfibusConfig {
    ConfigBuilder::new(0x809A, 5)
        .device_name("ET200S_Station5")
        .add_digital_input(0, 8)
        .add_digital_output(1, 8)
        .add_analog_input(2, 4)
        .build()
}
```

### Complete Initialization Example

```rust
pub async fn initialize_station(
    interface: &mut ProfibusInterface,
    expected: &ProfibusConfig,
) -> Result<()> {
    let station_addr = expected.device.station_address;
    
    // Read actual device identification
    let actual_device = interface.read_device_ident(station_addr).await?;
    
    // Read module configuration
    let actual_modules = interface.read_module_config(station_addr).await?;
    
    let actual_config = ProfibusConfig {
        device: actual_device,
        modules: actual_modules,
    };
    
    // Verify configuration matches
    verify_config(expected, &actual_config)?;
    
    println!("Station {} verified successfully", station_addr);
    println!("Device: {}", actual_config.device.ident_number);
    println!("Modules: {}", actual_config.modules.len());
    
    Ok(())
}
```

## Summary

**Identifier and Module ID** functionality in Profibus provides critical configuration verification:

- **Ident_Number** is a 16-bit unique identifier for each device type that ensures the correct device is connected at each station address
- **Configuration Verification** happens during network initialization, where the master compares expected and actual Ident_Numbers to prevent misconfiguration
- **Modular Devices** extend this concept with per-module identification, allowing verification of complex I/O configurations with pluggable modules
- **Safety Mechanism** prevents operational errors by detecting wrong devices or incorrect module installations before the system goes live

In C/C++ implementations, you typically work with structures and functions that handle telegram construction and parsing, while Rust implementations benefit from strong typing, error handling with Result types, and async capabilities for network communication. Both approaches emphasize the importance of validating device identity before allowing operation, which is essential for industrial automation safety and reliability.