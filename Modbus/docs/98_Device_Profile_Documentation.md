# Modbus Device Profile Documentation

## Detailed Description

Device Profile Documentation in Modbus refers to the comprehensive technical documentation that describes how to interact with a Modbus device. This documentation serves as the essential interface specification between device manufacturers and integrators/users, detailing all accessible registers, their purposes, data types, scaling factors, and access permissions.

### What is a Device Profile?

A Modbus Device Profile is a structured document that contains:

1. **Register Maps**: Complete listings of all Holding Registers, Input Registers, Coils, and Discrete Inputs
2. **Data Specifications**: Data types, byte ordering (endianness), scaling factors, and units
3. **Functional Descriptions**: What each register controls or represents
4. **Access Rights**: Read-only, write-only, or read-write permissions
5. **Default Values**: Initial or factory-default register values
6. **Valid Ranges**: Minimum and maximum acceptable values
7. **Device Metadata**: Manufacturer info, model numbers, firmware versions

### Why It Matters

Good device profile documentation:
- Reduces integration time and errors
- Enables interoperability between different systems
- Provides a contract between hardware and software teams
- Facilitates maintenance and troubleshooting
- Supports automated code generation for drivers

### Common Documentation Formats

- **PDF/Word Documents**: Traditional human-readable format
- **Excel/CSV**: Structured tabular format, machine-parsable
- **XML**: Industry-standard structured format (e.g., EDS files)
- **JSON/YAML**: Modern structured formats for code generation
- **Markdown**: Version-control friendly documentation

## C/C++ Implementation

### Data Structures for Device Profiles

```c
// modbus_device_profile.h
#ifndef MODBUS_DEVICE_PROFILE_H
#define MODBUS_DEVICE_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

// Register data types
typedef enum {
    REG_TYPE_UINT16,
    REG_TYPE_INT16,
    REG_TYPE_UINT32,
    REG_TYPE_INT32,
    REG_TYPE_FLOAT32,
    REG_TYPE_STRING,
    REG_TYPE_BITFIELD
} RegisterDataType;

// Register access modes
typedef enum {
    ACCESS_READ_ONLY,
    ACCESS_WRITE_ONLY,
    ACCESS_READ_WRITE
} RegisterAccess;

// Function code types
typedef enum {
    FUNC_COIL = 0x01,
    FUNC_DISCRETE_INPUT = 0x02,
    FUNC_HOLDING_REGISTER = 0x03,
    FUNC_INPUT_REGISTER = 0x04
} FunctionCodeType;

// Register definition structure
typedef struct {
    uint16_t address;              // Modbus register address
    const char* name;               // Register name
    const char* description;        // Detailed description
    FunctionCodeType function_code; // Which function code to use
    RegisterDataType data_type;     // Data type
    RegisterAccess access;          // Access permissions
    uint16_t register_count;        // Number of registers (for 32-bit, etc.)
    float scale_factor;             // Scaling multiplier
    float offset;                   // Offset to add after scaling
    const char* unit;               // Engineering unit (°C, RPM, etc.)
    float min_value;                // Minimum valid value
    float max_value;                // Maximum valid value
    uint16_t default_value;         // Factory default
} RegisterDefinition;

// Device profile structure
typedef struct {
    const char* manufacturer;
    const char* model;
    const char* firmware_version;
    uint8_t slave_id;
    uint32_t baud_rate;
    char parity;
    uint8_t data_bits;
    uint8_t stop_bits;
    
    const RegisterDefinition* registers;
    size_t register_count;
} DeviceProfile;

// Function prototypes
const RegisterDefinition* find_register_by_name(
    const DeviceProfile* profile, 
    const char* name
);

const RegisterDefinition* find_register_by_address(
    const DeviceProfile* profile,
    uint16_t address,
    FunctionCodeType func_code
);

float decode_register_value(
    const RegisterDefinition* reg_def,
    const uint16_t* raw_data
);

int encode_register_value(
    const RegisterDefinition* reg_def,
    float value,
    uint16_t* raw_data
);

void print_device_profile(const DeviceProfile* profile);

#endif // MODBUS_DEVICE_PROFILE_H
```

### Implementation Example

```c
// modbus_device_profile.c
#include "modbus_device_profile.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Example device profile: Temperature Controller
static const RegisterDefinition temp_controller_registers[] = {
    {
        .address = 0,
        .name = "CURRENT_TEMPERATURE",
        .description = "Current measured temperature",
        .function_code = FUNC_INPUT_REGISTER,
        .data_type = REG_TYPE_INT16,
        .access = ACCESS_READ_ONLY,
        .register_count = 1,
        .scale_factor = 0.1f,
        .offset = 0.0f,
        .unit = "°C",
        .min_value = -50.0f,
        .max_value = 150.0f,
        .default_value = 0
    },
    {
        .address = 1,
        .name = "SETPOINT_TEMPERATURE",
        .description = "Target temperature setpoint",
        .function_code = FUNC_HOLDING_REGISTER,
        .data_type = REG_TYPE_INT16,
        .access = ACCESS_READ_WRITE,
        .register_count = 1,
        .scale_factor = 0.1f,
        .offset = 0.0f,
        .unit = "°C",
        .min_value = 0.0f,
        .max_value = 100.0f,
        .default_value = 250  // 25.0°C
    },
    {
        .address = 2,
        .name = "HEATER_POWER",
        .description = "Heater output power percentage",
        .function_code = FUNC_INPUT_REGISTER,
        .data_type = REG_TYPE_UINT16,
        .access = ACCESS_READ_ONLY,
        .register_count = 1,
        .scale_factor = 0.1f,
        .offset = 0.0f,
        .unit = "%",
        .min_value = 0.0f,
        .max_value = 100.0f,
        .default_value = 0
    },
    {
        .address = 10,
        .name = "DEVICE_STATUS",
        .description = "Device status flags (bit 0: error, bit 1: heating, bit 2: enabled)",
        .function_code = FUNC_HOLDING_REGISTER,
        .data_type = REG_TYPE_BITFIELD,
        .access = ACCESS_READ_ONLY,
        .register_count = 1,
        .scale_factor = 1.0f,
        .offset = 0.0f,
        .unit = "",
        .min_value = 0.0f,
        .max_value = 65535.0f,
        .default_value = 0
    },
    {
        .address = 100,
        .name = "TOTAL_RUNTIME",
        .description = "Total device runtime in hours",
        .function_code = FUNC_INPUT_REGISTER,
        .data_type = REG_TYPE_UINT32,
        .access = ACCESS_READ_ONLY,
        .register_count = 2,
        .scale_factor = 1.0f,
        .offset = 0.0f,
        .unit = "hours",
        .min_value = 0.0f,
        .max_value = 4294967295.0f,
        .default_value = 0
    }
};

// Device profile instance
static const DeviceProfile temp_controller_profile = {
    .manufacturer = "Example Corp",
    .model = "TC-1000",
    .firmware_version = "1.2.3",
    .slave_id = 1,
    .baud_rate = 9600,
    .parity = 'N',
    .data_bits = 8,
    .stop_bits = 1,
    .registers = temp_controller_registers,
    .register_count = sizeof(temp_controller_registers) / sizeof(RegisterDefinition)
};

// Find register by name
const RegisterDefinition* find_register_by_name(
    const DeviceProfile* profile, 
    const char* name)
{
    for (size_t i = 0; i < profile->register_count; i++) {
        if (strcmp(profile->registers[i].name, name) == 0) {
            return &profile->registers[i];
        }
    }
    return NULL;
}

// Find register by address and function code
const RegisterDefinition* find_register_by_address(
    const DeviceProfile* profile,
    uint16_t address,
    FunctionCodeType func_code)
{
    for (size_t i = 0; i < profile->register_count; i++) {
        if (profile->registers[i].address == address &&
            profile->registers[i].function_code == func_code) {
            return &profile->registers[i];
        }
    }
    return NULL;
}

// Decode raw register data to engineering units
float decode_register_value(
    const RegisterDefinition* reg_def,
    const uint16_t* raw_data)
{
    float value = 0.0f;
    
    switch (reg_def->data_type) {
        case REG_TYPE_UINT16:
            value = (float)raw_data[0];
            break;
            
        case REG_TYPE_INT16:
            value = (float)((int16_t)raw_data[0]);
            break;
            
        case REG_TYPE_UINT32:
            value = (float)((uint32_t)raw_data[0] << 16 | raw_data[1]);
            break;
            
        case REG_TYPE_INT32:
            value = (float)((int32_t)((uint32_t)raw_data[0] << 16 | raw_data[1]));
            break;
            
        case REG_TYPE_FLOAT32: {
            uint32_t temp = ((uint32_t)raw_data[0] << 16) | raw_data[1];
            memcpy(&value, &temp, sizeof(float));
            break;
        }
            
        case REG_TYPE_BITFIELD:
            value = (float)raw_data[0];
            return value; // No scaling for bitfields
            
        default:
            return 0.0f;
    }
    
    return value * reg_def->scale_factor + reg_def->offset;
}

// Encode engineering value to raw register data
int encode_register_value(
    const RegisterDefinition* reg_def,
    float value,
    uint16_t* raw_data)
{
    // Check bounds
    if (value < reg_def->min_value || value > reg_def->max_value) {
        return -1; // Out of range
    }
    
    // Remove offset and scaling
    float scaled_value = (value - reg_def->offset) / reg_def->scale_factor;
    
    switch (reg_def->data_type) {
        case REG_TYPE_UINT16:
            raw_data[0] = (uint16_t)roundf(scaled_value);
            break;
            
        case REG_TYPE_INT16:
            raw_data[0] = (uint16_t)((int16_t)roundf(scaled_value));
            break;
            
        case REG_TYPE_UINT32: {
            uint32_t temp = (uint32_t)roundf(scaled_value);
            raw_data[0] = (uint16_t)(temp >> 16);
            raw_data[1] = (uint16_t)(temp & 0xFFFF);
            break;
        }
            
        case REG_TYPE_INT32: {
            int32_t temp = (int32_t)roundf(scaled_value);
            raw_data[0] = (uint16_t)((uint32_t)temp >> 16);
            raw_data[1] = (uint16_t)((uint32_t)temp & 0xFFFF);
            break;
        }
            
        case REG_TYPE_FLOAT32: {
            uint32_t temp;
            memcpy(&temp, &scaled_value, sizeof(float));
            raw_data[0] = (uint16_t)(temp >> 16);
            raw_data[1] = (uint16_t)(temp & 0xFFFF);
            break;
        }
            
        default:
            return -1;
    }
    
    return 0;
}

// Print device profile documentation
void print_device_profile(const DeviceProfile* profile)
{
    printf("\n========================================\n");
    printf("MODBUS DEVICE PROFILE\n");
    printf("========================================\n\n");
    
    printf("Device Information:\n");
    printf("  Manufacturer: %s\n", profile->manufacturer);
    printf("  Model: %s\n", profile->model);
    printf("  Firmware: %s\n", profile->firmware_version);
    printf("  Slave ID: %d\n", profile->slave_id);
    printf("  Baud Rate: %u\n", profile->baud_rate);
    printf("  Parity: %c, Data Bits: %d, Stop Bits: %d\n\n",
           profile->parity, profile->data_bits, profile->stop_bits);
    
    printf("Register Map:\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("%-6s %-25s %-10s %-8s %-8s %s\n", 
           "Addr", "Name", "Type", "Access", "Unit", "Description");
    printf("--------------------------------------------------------------------------------\n");
    
    for (size_t i = 0; i < profile->register_count; i++) {
        const RegisterDefinition* reg = &profile->registers[i];
        
        const char* type_str;
        switch (reg->data_type) {
            case REG_TYPE_UINT16: type_str = "UINT16"; break;
            case REG_TYPE_INT16: type_str = "INT16"; break;
            case REG_TYPE_UINT32: type_str = "UINT32"; break;
            case REG_TYPE_INT32: type_str = "INT32"; break;
            case REG_TYPE_FLOAT32: type_str = "FLOAT32"; break;
            case REG_TYPE_BITFIELD: type_str = "BITFIELD"; break;
            default: type_str = "UNKNOWN";
        }
        
        const char* access_str;
        switch (reg->access) {
            case ACCESS_READ_ONLY: access_str = "RO"; break;
            case ACCESS_WRITE_ONLY: access_str = "WO"; break;
            case ACCESS_READ_WRITE: access_str = "RW"; break;
            default: access_str = "??";
        }
        
        printf("%-6d %-25s %-10s %-8s %-8s %s\n",
               reg->address, reg->name, type_str, access_str,
               reg->unit, reg->description);
        
        if (reg->data_type != REG_TYPE_BITFIELD) {
            printf("       Range: %.2f to %.2f, Scale: %.3f, Default: %d\n",
                   reg->min_value, reg->max_value, 
                   reg->scale_factor, reg->default_value);
        }
        printf("\n");
    }
}

// Example usage
int main(void)
{
    // Print the device profile
    print_device_profile(&temp_controller_profile);
    
    // Example: Find and read a register by name
    const RegisterDefinition* temp_reg = 
        find_register_by_name(&temp_controller_profile, "CURRENT_TEMPERATURE");
    
    if (temp_reg) {
        printf("\nExample: Reading CURRENT_TEMPERATURE register\n");
        printf("  Address: %d\n", temp_reg->address);
        printf("  Function Code: 0x%02X\n", temp_reg->function_code);
        
        // Simulate raw data from device (e.g., 235 = 23.5°C)
        uint16_t raw_data = 235;
        float temperature = decode_register_value(temp_reg, &raw_data);
        printf("  Raw Value: %d\n", raw_data);
        printf("  Decoded Value: %.1f %s\n", temperature, temp_reg->unit);
    }
    
    // Example: Encode a value for writing
    const RegisterDefinition* setpoint_reg = 
        find_register_by_name(&temp_controller_profile, "SETPOINT_TEMPERATURE");
    
    if (setpoint_reg) {
        printf("\nExample: Writing SETPOINT_TEMPERATURE register\n");
        float desired_temp = 30.0f; // 30°C
        uint16_t encoded_value;
        
        if (encode_register_value(setpoint_reg, desired_temp, &encoded_value) == 0) {
            printf("  Desired Value: %.1f %s\n", desired_temp, setpoint_reg->unit);
            printf("  Encoded Value: %d\n", encoded_value);
        } else {
            printf("  Error: Value out of range!\n");
        }
    }
    
    return 0;
}
```

## Rust Implementation

```rust
// modbus_device_profile.rs
use std::collections::HashMap;

/// Register data types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RegisterDataType {
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32,
    String,
    Bitfield,
}

/// Register access modes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RegisterAccess {
    ReadOnly,
    WriteOnly,
    ReadWrite,
}

/// Modbus function code types
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FunctionCodeType {
    Coil = 0x01,
    DiscreteInput = 0x02,
    HoldingRegister = 0x03,
    InputRegister = 0x04,
}

/// Register definition structure
#[derive(Debug, Clone)]
pub struct RegisterDefinition {
    pub address: u16,
    pub name: String,
    pub description: String,
    pub function_code: FunctionCodeType,
    pub data_type: RegisterDataType,
    pub access: RegisterAccess,
    pub register_count: u16,
    pub scale_factor: f32,
    pub offset: f32,
    pub unit: String,
    pub min_value: f32,
    pub max_value: f32,
    pub default_value: u16,
}

/// Device profile structure
#[derive(Debug, Clone)]
pub struct DeviceProfile {
    pub manufacturer: String,
    pub model: String,
    pub firmware_version: String,
    pub slave_id: u8,
    pub baud_rate: u32,
    pub parity: char,
    pub data_bits: u8,
    pub stop_bits: u8,
    pub registers: Vec<RegisterDefinition>,
}

impl DeviceProfile {
    /// Create a new device profile
    pub fn new(
        manufacturer: impl Into<String>,
        model: impl Into<String>,
        firmware_version: impl Into<String>,
    ) -> Self {
        Self {
            manufacturer: manufacturer.into(),
            model: model.into(),
            firmware_version: firmware_version.into(),
            slave_id: 1,
            baud_rate: 9600,
            parity: 'N',
            data_bits: 8,
            stop_bits: 1,
            registers: Vec::new(),
        }
    }

    /// Add a register to the profile
    pub fn add_register(&mut self, register: RegisterDefinition) {
        self.registers.push(register);
    }

    /// Find register by name
    pub fn find_by_name(&self, name: &str) -> Option<&RegisterDefinition> {
        self.registers.iter().find(|r| r.name == name)
    }

    /// Find register by address and function code
    pub fn find_by_address(
        &self,
        address: u16,
        function_code: FunctionCodeType,
    ) -> Option<&RegisterDefinition> {
        self.registers.iter().find(|r| {
            r.address == address && r.function_code == function_code
        })
    }

    /// Print formatted device profile documentation
    pub fn print_documentation(&self) {
        println!("\n========================================");
        println!("MODBUS DEVICE PROFILE");
        println!("========================================\n");

        println!("Device Information:");
        println!("  Manufacturer: {}", self.manufacturer);
        println!("  Model: {}", self.model);
        println!("  Firmware: {}", self.firmware_version);
        println!("  Slave ID: {}", self.slave_id);
        println!("  Baud Rate: {}", self.baud_rate);
        println!(
            "  Parity: {}, Data Bits: {}, Stop Bits: {}\n",
            self.parity, self.data_bits, self.stop_bits
        );

        println!("Register Map:");
        println!("{:-<80}", "");
        println!(
            "{:<6} {:<25} {:<10} {:<8} {:<8} {}",
            "Addr", "Name", "Type", "Access", "Unit", "Description"
        );
        println!("{:-<80}", "");

        for reg in &self.registers {
            let type_str = format!("{:?}", reg.data_type);
            let access_str = match reg.access {
                RegisterAccess::ReadOnly => "RO",
                RegisterAccess::WriteOnly => "WO",
                RegisterAccess::ReadWrite => "RW",
            };

            println!(
                "{:<6} {:<25} {:<10} {:<8} {:<8} {}",
                reg.address, reg.name, type_str, access_str, reg.unit, reg.description
            );

            if reg.data_type != RegisterDataType::Bitfield {
                println!(
                    "       Range: {:.2} to {:.2}, Scale: {:.3}, Default: {}",
                    reg.min_value, reg.max_value, reg.scale_factor, reg.default_value
                );
            }
            println!();
        }
    }

    /// Export profile to JSON
    #[cfg(feature = "serde")]
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string_pretty(self)
    }

    /// Import profile from JSON
    #[cfg(feature = "serde")]
    pub fn from_json(json: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(json)
    }
}

impl RegisterDefinition {
    /// Builder pattern for creating register definitions
    pub fn builder(name: impl Into<String>) -> RegisterDefinitionBuilder {
        RegisterDefinitionBuilder {
            name: name.into(),
            address: 0,
            description: String::new(),
            function_code: FunctionCodeType::HoldingRegister,
            data_type: RegisterDataType::UInt16,
            access: RegisterAccess::ReadWrite,
            register_count: 1,
            scale_factor: 1.0,
            offset: 0.0,
            unit: String::new(),
            min_value: 0.0,
            max_value: 65535.0,
            default_value: 0,
        }
    }

    /// Decode raw register data to engineering units
    pub fn decode(&self, raw_data: &[u16]) -> Result<f32, String> {
        if raw_data.len() < self.register_count as usize {
            return Err("Insufficient data".to_string());
        }

        let value = match self.data_type {
            RegisterDataType::UInt16 => raw_data[0] as f32,
            RegisterDataType::Int16 => raw_data[0] as i16 as f32,
            RegisterDataType::UInt32 => {
                let val = ((raw_data[0] as u32) << 16) | (raw_data[1] as u32);
                val as f32
            }
            RegisterDataType::Int32 => {
                let val = ((raw_data[0] as u32) << 16) | (raw_data[1] as u32);
                val as i32 as f32
            }
            RegisterDataType::Float32 => {
                let bits = ((raw_data[0] as u32) << 16) | (raw_data[1] as u32);
                f32::from_bits(bits)
            }
            RegisterDataType::Bitfield => return Ok(raw_data[0] as f32),
            RegisterDataType::String => {
                return Err("String decoding not supported in this function".to_string())
            }
        };

        Ok(value * self.scale_factor + self.offset)
    }

    /// Encode engineering value to raw register data
    pub fn encode(&self, value: f32) -> Result<Vec<u16>, String> {
        // Validate range
        if value < self.min_value || value > self.max_value {
            return Err(format!(
                "Value {} out of range [{}, {}]",
                value, self.min_value, self.max_value
            ));
        }

        // Remove offset and scaling
        let scaled_value = (value - self.offset) / self.scale_factor;

        let result = match self.data_type {
            RegisterDataType::UInt16 => vec![scaled_value.round() as u16],
            RegisterDataType::Int16 => vec![(scaled_value.round() as i16) as u16],
            RegisterDataType::UInt32 => {
                let val = scaled_value.round() as u32;
                vec![(val >> 16) as u16, (val & 0xFFFF) as u16]
            }
            RegisterDataType::Int32 => {
                let val = scaled_value.round() as i32 as u32;
                vec![(val >> 16) as u16, (val & 0xFFFF) as u16]
            }
            RegisterDataType::Float32 => {
                let bits = scaled_value.to_bits();
                vec![(bits >> 16) as u16, (bits & 0xFFFF) as u16]
            }
            _ => return Err("Unsupported data type for encoding".to_string()),
        };

        Ok(result)
    }
}

/// Builder for RegisterDefinition
pub struct RegisterDefinitionBuilder {
    name: String,
    address: u16,
    description: String,
    function_code: FunctionCodeType,
    data_type: RegisterDataType,
    access: RegisterAccess,
    register_count: u16,
    scale_factor: f32,
    offset: f32,
    unit: String,
    min_value: f32,
    max_value: f32,
    default_value: u16,
}

impl RegisterDefinitionBuilder {
    pub fn address(mut self, address: u16) -> Self {
        self.address = address;
        self
    }

    pub fn description(mut self, description: impl Into<String>) -> Self {
        self.description = description.into();
        self
    }

    pub fn function_code(mut self, function_code: FunctionCodeType) -> Self {
        self.function_code = function_code;
        self
    }

    pub fn data_type(mut self, data_type: RegisterDataType) -> Self {
        self.data_type = data_type;
        // Automatically set register count based on data type
        self.register_count = match data_type {
            RegisterDataType::UInt16 | RegisterDataType::Int16 => 1,
            RegisterDataType::UInt32 | RegisterDataType::Int32 | RegisterDataType::Float32 => 2,
            _ => 1,
        };
        self
    }

    pub fn access(mut self, access: RegisterAccess) -> Self {
        self.access = access;
        self
    }

    pub fn scale_factor(mut self, scale_factor: f32) -> Self {
        self.scale_factor = scale_factor;
        self
    }

    pub fn offset(mut self, offset: f32) -> Self {
        self.offset = offset;
        self
    }

    pub fn unit(mut self, unit: impl Into<String>) -> Self {
        self.unit = unit.into();
        self
    }

    pub fn range(mut self, min: f32, max: f32) -> Self {
        self.min_value = min;
        self.max_value = max;
        self
    }

    pub fn default_value(mut self, default_value: u16) -> Self {
        self.default_value = default_value;
        self
    }

    pub fn build(self) -> RegisterDefinition {
        RegisterDefinition {
            address: self.address,
            name: self.name,
            description: self.description,
            function_code: self.function_code,
            data_type: self.data_type,
            access: self.access,
            register_count: self.register_count,
            scale_factor: self.scale_factor,
            offset: self.offset,
            unit: self.unit,
            min_value: self.min_value,
            max_value: self.max_value,
            default_value: self.default_value,
        }
    }
}

// Example usage
fn main() {
    // Create a device profile for a temperature controller
    let mut profile = DeviceProfile::new(
        "Example Corp",
        "TC-1000",
        "1.2.3",
    );

    // Add registers using builder pattern
    profile.add_register(
        RegisterDefinition::builder("CURRENT_TEMPERATURE")
            .address(0)
            .description("Current measured temperature")
            .function_code(FunctionCodeType::InputRegister)
            .data_type(RegisterDataType::Int16)
            .access(RegisterAccess::ReadOnly)
            .scale_factor(0.1)
            .unit("°C")
            .range(-50.0, 150.0)
            .build(),
    );

    profile.add_register(
        RegisterDefinition::builder("SETPOINT_TEMPERATURE")
            .address(1)
            .description("Target temperature setpoint")
            .function_code(FunctionCodeType::HoldingRegister)
            .data_type(RegisterDataType::Int16)
            .access(RegisterAccess::ReadWrite)
            .scale_factor(0.1)
            .unit("°C")
            .range(0.0, 100.0)
            .default_value(250)
            .build(),
    );

    profile.add_register(
        RegisterDefinition::builder("HEATER_POWER")
            .address(2)
            .description("Heater output power percentage")
            .function_code(FunctionCodeType::InputRegister)
            .data_type(RegisterDataType::UInt16)
            .access(RegisterAccess::ReadOnly)
            .scale_factor(0.1)
            .unit("%")
            .range(0.0, 100.0)
            .build(),
    );

    profile.add_register(
        RegisterDefinition::builder("TOTAL_RUNTIME")
            .address(100)
            .description("Total device runtime in hours")
            .function_code(FunctionCodeType::InputRegister)
            .data_type(RegisterDataType::UInt32)
            .access(RegisterAccess::ReadOnly)
            .unit("hours")
            .range(0.0, 4_294_967_295.0)
            .build(),
    );

    // Print the device profile documentation
    profile.print_documentation();

    // Example: Decode a temperature reading
    println!("\nExample: Reading CURRENT_TEMPERATURE register");
    if let Some(temp_reg) = profile.find_by_name("CURRENT_TEMPERATURE") {
        let raw_data = vec![235]; // 23.5°C
        match temp_reg.decode(&raw_data) {
            Ok(temperature) => {
                println!("  Raw Value: {}", raw_data[0]);
                println!("  Decoded Value: {:.1} {}", temperature, temp_reg.unit);
            }
            Err(e) => println!("  Error: {}", e),
        }
    }

    // Example: Encode a setpoint value
    println!("\nExample: Writing SETPOINT_TEMPERATURE register");
    if let Some(setpoint_reg) = profile.find_by_name("SETPOINT_TEMPERATURE") {
        let desired_temp = 30.0; // 30°C
        match setpoint_reg.encode(desired_temp) {
            Ok(encoded) => {
                println!("  Desired Value: {:.1} {}", desired_temp, setpoint_reg.unit);
                println!("  Encoded Value: {}", encoded[0]);
            }
            Err(e) => println!("  Error: {}", e),
        }
    }
}
```

## Summary

**Device Profile Documentation** is the critical bridge between Modbus hardware and software integration. It provides structured, comprehensive information about all accessible registers, their data types, scaling factors, and valid ranges. Good device profiles significantly reduce integration time, prevent errors, and enable automated code generation.

### Key Takeaways:

1. **Structured Data**: Device profiles organize register information including addresses, data types, access modes, scaling factors, and valid ranges

2. **Multiple Formats**: Documentation can be provided in various formats (PDF, Excel, XML, JSON) depending on the use case—human-readable for manual integration or machine-parsable for code generation

3. **Encoding/Decoding**: Proper profiles include scaling and offset information to convert between raw register values and engineering units (e.g., raw value 235 → 23.5°C)

4. **Type Safety**: Both C/C++ and Rust implementations demonstrate how to create type-safe abstractions over raw Modbus registers, reducing runtime errors

5. **Builder Patterns**: Modern implementations use builder patterns (especially in Rust) to make register definitions more readable and maintainable

6. **Validation**: Good implementations validate data ranges and types before encoding values for transmission to devices

7. **Metadata**: Complete profiles include communication parameters (baud rate, parity, etc.) and device identification (manufacturer, model, firmware version)

Well-documented device profiles are essential for successful Modbus deployments, enabling faster integration, better maintainability, and more reliable systems.