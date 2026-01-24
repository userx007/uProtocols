# Electronic Device Description (EDD) in Profibus

## Detailed Description

### What is EDD?

Electronic Device Description (EDD) is a standardized device description language used primarily in **Profibus PA (Process Automation)** networks. EDD provides a comprehensive, manufacturer-independent method for describing field devices' functionality, parameters, and diagnostic capabilities.

### Key Characteristics

**Purpose and Function:**
- EDD files contain detailed information about a device's capabilities, parameters, configuration options, and diagnostic features
- They enable advanced configuration tools to provide sophisticated user interfaces for device setup and maintenance
- Unlike simpler GSD (General Station Description) files, EDDs support complex data types, conditional parameters, and device-specific functions

**EDD Components:**
- **Device parameters** - Configuration settings, measurement ranges, calibration data
- **User interface descriptions** - How parameters should be displayed and organized
- **Methods** - Device-specific functions and procedures
- **Menus and dialogs** - Structured organization of parameters
- **Variables** - Dynamic data representation including arrays and records
- **Conditional logic** - Parameters that appear based on device state or configuration

**Technology Stack:**
- Written in a specialized description language (similar to Pascal/Ada syntax)
- Compiled into binary format for use by configuration tools
- Supports internationalization with multiple language texts
- Includes version management and compatibility information

### Use Cases in Process Automation

1. **Advanced Device Configuration** - Setting complex parameters beyond basic GSD capabilities
2. **Diagnostics and Maintenance** - Accessing detailed device health information
3. **Calibration** - Performing precision adjustments and verification
4. **Asset Management** - Tracking device history, firmware versions, and maintenance records

## Code Examples

### C/C++ Implementation

```c
// EDD Parameter Parser in C
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// EDD Parameter Types
typedef enum {
    EDD_TYPE_INTEGER,
    EDD_TYPE_FLOAT,
    EDD_TYPE_STRING,
    EDD_TYPE_ENUM,
    EDD_TYPE_BITFIELD
} EDDParameterType;

// EDD Parameter Structure
typedef struct {
    uint16_t param_id;
    EDDParameterType type;
    char name[64];
    char description[256];
    uint32_t access_rights;  // read, write, read-write
    void *value;
    void *min_value;
    void *max_value;
} EDDParameter;

// EDD Device Description
typedef struct {
    char manufacturer[64];
    char device_type[64];
    char firmware_version[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t parameter_count;
    EDDParameter *parameters;
} EDDDeviceDescription;

// Read EDD Parameter Value
int edd_read_parameter(EDDDeviceDescription *edd, uint16_t param_id, 
                       void *buffer, size_t buffer_size) {
    for (int i = 0; i < edd->parameter_count; i++) {
        if (edd->parameters[i].param_id == param_id) {
            EDDParameter *param = &edd->parameters[i];
            
            // Check access rights
            if (!(param->access_rights & 0x01)) {  // Read bit
                printf("Parameter %d is not readable\n", param_id);
                return -1;
            }
            
            // Copy value based on type
            switch (param->type) {
                case EDD_TYPE_INTEGER:
                    if (buffer_size >= sizeof(int32_t)) {
                        memcpy(buffer, param->value, sizeof(int32_t));
                        return sizeof(int32_t);
                    }
                    break;
                    
                case EDD_TYPE_FLOAT:
                    if (buffer_size >= sizeof(float)) {
                        memcpy(buffer, param->value, sizeof(float));
                        return sizeof(float);
                    }
                    break;
                    
                case EDD_TYPE_STRING:
                    strncpy((char*)buffer, (char*)param->value, buffer_size);
                    return strlen((char*)buffer);
                    
                default:
                    break;
            }
            
            return -1;
        }
    }
    
    printf("Parameter %d not found\n", param_id);
    return -1;
}

// Write EDD Parameter Value
int edd_write_parameter(EDDDeviceDescription *edd, uint16_t param_id, 
                        const void *data, size_t data_size) {
    for (int i = 0; i < edd->parameter_count; i++) {
        if (edd->parameters[i].param_id == param_id) {
            EDDParameter *param = &edd->parameters[i];
            
            // Check access rights
            if (!(param->access_rights & 0x02)) {  // Write bit
                printf("Parameter %d is not writable\n", param_id);
                return -1;
            }
            
            // Validate and write based on type
            switch (param->type) {
                case EDD_TYPE_INTEGER: {
                    int32_t value = *(int32_t*)data;
                    int32_t min = *(int32_t*)param->min_value;
                    int32_t max = *(int32_t*)param->max_value;
                    
                    if (value < min || value > max) {
                        printf("Value out of range [%d, %d]\n", min, max);
                        return -1;
                    }
                    
                    memcpy(param->value, data, sizeof(int32_t));
                    printf("Parameter %s set to %d\n", param->name, value);
                    return 0;
                }
                
                case EDD_TYPE_FLOAT: {
                    float value = *(float*)data;
                    float min = *(float*)param->min_value;
                    float max = *(float*)param->max_value;
                    
                    if (value < min || value > max) {
                        printf("Value out of range [%.2f, %.2f]\n", min, max);
                        return -1;
                    }
                    
                    memcpy(param->value, data, sizeof(float));
                    printf("Parameter %s set to %.2f\n", param->name, value);
                    return 0;
                }
                
                default:
                    break;
            }
        }
    }
    
    return -1;
}

// EDD Method Execution
typedef int (*EDDMethodHandler)(void *context, const void *input, void *output);

typedef struct {
    uint16_t method_id;
    char name[64];
    char description[256];
    EDDMethodHandler handler;
} EDDMethod;

// Execute device calibration method
int edd_calibrate_sensor(void *context, const void *input, void *output) {
    printf("Executing sensor calibration...\n");
    
    // Calibration logic here
    float *reference_value = (float*)input;
    float *calibration_offset = (float*)output;
    
    printf("Reference value: %.2f\n", *reference_value);
    
    // Simulate calibration calculation
    *calibration_offset = *reference_value * 0.01f;
    
    printf("Calibration complete. Offset: %.4f\n", *calibration_offset);
    return 0;
}

// Example usage
int main() {
    // Initialize EDD device description
    EDDDeviceDescription device;
    strcpy(device.manufacturer, "ACME Instruments");
    strcpy(device.device_type, "Temperature Transmitter");
    strcpy(device.firmware_version, "2.1.0");
    device.vendor_id = 0x1234;
    device.device_id = 0x5678;
    
    // Set up parameters
    device.parameter_count = 3;
    device.parameters = malloc(sizeof(EDDParameter) * device.parameter_count);
    
    // Parameter 1: Measurement Range
    EDDParameter *range_param = &device.parameters[0];
    range_param->param_id = 1;
    range_param->type = EDD_TYPE_FLOAT;
    strcpy(range_param->name, "MeasurementRange");
    strcpy(range_param->description, "Upper range value in degrees Celsius");
    range_param->access_rights = 0x03;  // Read-Write
    
    float range_value = 100.0f;
    float range_min = 0.0f;
    float range_max = 500.0f;
    range_param->value = malloc(sizeof(float));
    range_param->min_value = malloc(sizeof(float));
    range_param->max_value = malloc(sizeof(float));
    memcpy(range_param->value, &range_value, sizeof(float));
    memcpy(range_param->min_value, &range_min, sizeof(float));
    memcpy(range_param->max_value, &range_max, sizeof(float));
    
    // Read parameter
    float read_value;
    if (edd_read_parameter(&device, 1, &read_value, sizeof(float)) > 0) {
        printf("Current range: %.2f °C\n", read_value);
    }
    
    // Write parameter
    float new_range = 150.0f;
    edd_write_parameter(&device, 1, &new_range, sizeof(float));
    
    // Execute calibration method
    float ref_val = 25.0f;
    float offset;
    edd_calibrate_sensor(NULL, &ref_val, &offset);
    
    // Cleanup
    free(range_param->value);
    free(range_param->min_value);
    free(range_param->max_value);
    free(device.parameters);
    
    return 0;
}
```

### Rust Implementation

```rust
// EDD Device Description and Parameter Management in Rust
use std::collections::HashMap;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum EDDParameterType {
    Integer,
    Float,
    String,
    Enum(Vec<String>),
    Bitfield,
}

#[derive(Debug, Clone)]
pub enum EDDValue {
    Integer(i32),
    Float(f32),
    String(String),
    Enum(usize),
    Bitfield(u32),
}

#[derive(Debug, Clone)]
pub struct EDDParameterRange {
    min: EDDValue,
    max: EDDValue,
}

#[derive(Debug, Clone)]
pub struct EDDParameter {
    id: u16,
    name: String,
    description: String,
    param_type: EDDParameterType,
    value: EDDValue,
    range: Option<EDDParameterRange>,
    readable: bool,
    writable: bool,
}

impl EDDParameter {
    pub fn new(
        id: u16,
        name: String,
        description: String,
        param_type: EDDParameterType,
        value: EDDValue,
        readable: bool,
        writable: bool,
    ) -> Self {
        EDDParameter {
            id,
            name,
            description,
            param_type,
            value,
            range: None,
            readable,
            writable,
        }
    }

    pub fn with_range(mut self, min: EDDValue, max: EDDValue) -> Self {
        self.range = Some(EDDParameterRange { min, max });
        self
    }

    pub fn validate(&self, new_value: &EDDValue) -> Result<(), String> {
        if !self.writable {
            return Err(format!("Parameter '{}' is read-only", self.name));
        }

        if let Some(range) = &self.range {
            match (new_value, &range.min, &range.max) {
                (EDDValue::Integer(v), EDDValue::Integer(min), EDDValue::Integer(max)) => {
                    if v < min || v > max {
                        return Err(format!(
                            "Value {} out of range [{}, {}]",
                            v, min, max
                        ));
                    }
                }
                (EDDValue::Float(v), EDDValue::Float(min), EDDValue::Float(max)) => {
                    if v < min || v > max {
                        return Err(format!(
                            "Value {} out of range [{}, {}]",
                            v, min, max
                        ));
                    }
                }
                _ => {}
            }
        }

        Ok(())
    }
}

#[derive(Debug)]
pub struct EDDMethod {
    id: u16,
    name: String,
    description: String,
    handler: fn(&EDDDeviceDescription, Option<&EDDValue>) -> Result<EDDValue, String>,
}

#[derive(Debug)]
pub struct EDDDeviceDescription {
    manufacturer: String,
    device_type: String,
    firmware_version: String,
    vendor_id: u16,
    device_id: u16,
    parameters: HashMap<u16, EDDParameter>,
    methods: HashMap<u16, EDDMethod>,
}

impl EDDDeviceDescription {
    pub fn new(
        manufacturer: String,
        device_type: String,
        firmware_version: String,
        vendor_id: u16,
        device_id: u16,
    ) -> Self {
        EDDDeviceDescription {
            manufacturer,
            device_type,
            firmware_version,
            vendor_id,
            device_id,
            parameters: HashMap::new(),
            methods: HashMap::new(),
        }
    }

    pub fn add_parameter(&mut self, param: EDDParameter) {
        self.parameters.insert(param.id, param);
    }

    pub fn add_method(&mut self, method: EDDMethod) {
        self.methods.insert(method.id, method);
    }

    pub fn read_parameter(&self, param_id: u16) -> Result<&EDDValue, String> {
        let param = self
            .parameters
            .get(&param_id)
            .ok_or_else(|| format!("Parameter {} not found", param_id))?;

        if !param.readable {
            return Err(format!("Parameter '{}' is not readable", param.name));
        }

        Ok(&param.value)
    }

    pub fn write_parameter(&mut self, param_id: u16, value: EDDValue) -> Result<(), String> {
        let param = self
            .parameters
            .get_mut(&param_id)
            .ok_or_else(|| format!("Parameter {} not found", param_id))?;

        param.validate(&value)?;
        param.value = value;

        println!("Parameter '{}' updated successfully", param.name);
        Ok(())
    }

    pub fn execute_method(
        &self,
        method_id: u16,
        input: Option<&EDDValue>,
    ) -> Result<EDDValue, String> {
        let method = self
            .methods
            .get(&method_id)
            .ok_or_else(|| format!("Method {} not found", method_id))?;

        println!("Executing method: {}", method.name);
        (method.handler)(self, input)
    }

    pub fn display_info(&self) {
        println!("Device Information:");
        println!("  Manufacturer: {}", self.manufacturer);
        println!("  Type: {}", self.device_type);
        println!("  Firmware: {}", self.firmware_version);
        println!("  Vendor ID: 0x{:04X}", self.vendor_id);
        println!("  Device ID: 0x{:04X}", self.device_id);
        println!("  Parameters: {}", self.parameters.len());
        println!("  Methods: {}", self.methods.len());
    }
}

// Example calibration method
fn calibrate_sensor(
    device: &EDDDeviceDescription,
    input: Option<&EDDValue>,
) -> Result<EDDValue, String> {
    let reference_value = match input {
        Some(EDDValue::Float(v)) => v,
        _ => return Err("Invalid input for calibration".to_string()),
    };

    println!("Calibrating sensor with reference: {} °C", reference_value);

    // Simulate calibration calculation
    let offset = reference_value * 0.01;
    
    println!("Calibration complete. Calculated offset: {:.4}", offset);
    Ok(EDDValue::Float(offset))
}

// Example diagnostic method
fn run_diagnostics(
    device: &EDDDeviceDescription,
    _input: Option<&EDDValue>,
) -> Result<EDDValue, String> {
    println!("Running device diagnostics...");
    
    // Simulate diagnostic checks
    let health_status = 100; // 0-100 scale
    
    println!("Diagnostics complete. Health: {}%", health_status);
    Ok(EDDValue::Integer(health_status))
}

fn main() {
    // Create EDD device description
    let mut device = EDDDeviceDescription::new(
        "ACME Instruments".to_string(),
        "PT-100 Temperature Transmitter".to_string(),
        "3.2.1".to_string(),
        0x1234,
        0x5678,
    );

    // Add parameters
    let temp_range = EDDParameter::new(
        1,
        "MeasurementRange".to_string(),
        "Upper temperature range in Celsius".to_string(),
        EDDParameterType::Float,
        EDDValue::Float(100.0),
        true,
        true,
    )
    .with_range(EDDValue::Float(-200.0), EDDValue::Float(850.0));

    let damping = EDDParameter::new(
        2,
        "DampingTime".to_string(),
        "Response damping time in seconds".to_string(),
        EDDParameterType::Float,
        EDDValue::Float(1.0),
        true,
        true,
    )
    .with_range(EDDValue::Float(0.1), EDDValue::Float(60.0));

    let sensor_type = EDDParameter::new(
        3,
        "SensorType".to_string(),
        "Type of temperature sensor".to_string(),
        EDDParameterType::Enum(vec![
            "PT-100".to_string(),
            "PT-1000".to_string(),
            "Thermocouple-K".to_string(),
        ]),
        EDDValue::Enum(0),
        true,
        true,
    );

    device.add_parameter(temp_range);
    device.add_parameter(damping);
    device.add_parameter(sensor_type);

    // Add methods
    device.add_method(EDDMethod {
        id: 1,
        name: "CalibrateSensor".to_string(),
        description: "Perform sensor calibration".to_string(),
        handler: calibrate_sensor,
    });

    device.add_method(EDDMethod {
        id: 2,
        name: "RunDiagnostics".to_string(),
        description: "Execute device self-diagnostics".to_string(),
        handler: run_diagnostics,
    });

    // Display device info
    device.display_info();
    println!();

    // Read parameter
    match device.read_parameter(1) {
        Ok(EDDValue::Float(val)) => println!("Current range: {} °C", val),
        Ok(_) => println!("Unexpected value type"),
        Err(e) => println!("Error: {}", e),
    }

    // Write parameter
    println!("\nUpdating measurement range...");
    match device.write_parameter(1, EDDValue::Float(200.0)) {
        Ok(_) => {
            if let Ok(EDDValue::Float(val)) = device.read_parameter(1) {
                println!("New range: {} °C", val);
            }
        }
        Err(e) => println!("Error: {}", e),
    }

    // Execute calibration method
    println!("\nExecuting calibration...");
    match device.execute_method(1, Some(&EDDValue::Float(25.0))) {
        Ok(EDDValue::Float(offset)) => println!("Calibration offset: {:.4}", offset),
        Ok(_) => println!("Unexpected return type"),
        Err(e) => println!("Error: {}", e),
    }

    // Execute diagnostics
    println!("\nExecuting diagnostics...");
    match device.execute_method(2, None) {
        Ok(EDDValue::Integer(health)) => println!("Device health: {}%", health),
        Ok(_) => println!("Unexpected return type"),
        Err(e) => println!("Error: {}", e),
    }

    // Attempt invalid operation
    println!("\nAttempting out-of-range write...");
    match device.write_parameter(1, EDDValue::Float(1000.0)) {
        Ok(_) => println!("Write successful"),
        Err(e) => println!("Write rejected: {}", e),
    }
}
```

## Summary

**Electronic Device Description (EDD)** is a sophisticated device description technology for Profibus PA networks that extends far beyond basic device identification. EDD files provide comprehensive information about field devices including complex parameter structures, conditional logic, device-specific methods, and rich diagnostic capabilities.

**Key advantages of EDD:**
- **Advanced Configuration** - Supports complex data types, arrays, records, and conditional parameters
- **Rich Diagnostics** - Enables detailed device health monitoring and troubleshooting
- **Manufacturer Independence** - Standardized format works with any EDD-compatible configuration tool
- **Internationalization** - Multi-language support for global deployment
- **Device Methods** - Execute calibration, diagnostics, and maintenance procedures remotely

The code examples demonstrate EDD parameter management (reading/writing with validation), method execution (calibration, diagnostics), and the type-safe implementation patterns needed for robust industrial automation systems. While C/C++ remains prevalent in embedded device firmware, Rust offers modern safety guarantees particularly valuable in critical process control applications where parameter corruption or memory errors could have serious consequences.