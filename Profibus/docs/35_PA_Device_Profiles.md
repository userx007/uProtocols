# PA Device Profiles in Profibus

## Detailed Description

**PA Device Profiles** are standardized device descriptions defined by the PROFIBUS & PROFINET International (PI) organization for Process Automation (PA) devices. These profiles establish a uniform framework for representing process instruments (such as temperature transmitters, pressure sensors, flow meters, valve positioners, and analyzers) in PROFIBUS PA networks.

### Key Concepts

**Device Profiles** serve multiple purposes:

1. **Standardization**: They define common parameters, functions, and behaviors for specific device types, ensuring interoperability between devices from different manufacturers.

2. **Data Structure**: Each profile specifies a standard set of parameters organized into blocks (Physical Block, Transducer Block, Function Blocks) following the IEC 61804 standard.

3. **Parameter Access**: Profiles define how to read/write process values, configuration parameters, diagnostic information, and device identification data.

4. **Block Model**: PA devices use a block-oriented architecture:
   - **Physical Block (PB)**: Hardware and device-level information
   - **Transducer Block (TB)**: Sensor/actuator interface
   - **Function Blocks (FB)**: Process control functions (AI, AO, DI, DO, etc.)

### Common PA Device Profile Types

- **Temperature Transmitters** (Profile 3.0)
- **Pressure Transmitters** (Profile 3.0)
- **Flow Meters** (Profile 3.0)
- **Level Transmitters** (Profile 3.0)
- **Valve Positioners** (Profile 3.0)
- **Analyzers** (Profile 3.1)
- **Discrete Input/Output Devices** (Profile 3.0)

### Parameter Structure

PA device parameters follow a hierarchical structure with standard identifiers:

- **Slot/Index addressing**: Parameters organized by slot (block type) and index (parameter within block)
- **Standardized parameter IDs**: Common parameters like PV (Process Value), OUT (Output), MODE, STATUS
- **Data types**: Defined types for values, units, scaling, and quality information

## Code Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// PA Profile Block Types
#define PHYSICAL_BLOCK      0
#define TRANSDUCER_BLOCK    1
#define ANALOG_INPUT_BLOCK  2
#define ANALOG_OUTPUT_BLOCK 3

// Standard Parameter Indices (simplified)
#define PARAM_TAG           1
#define PARAM_DESCRIPTOR    2
#define PARAM_STRATEGY      3
#define PARAM_MODE          4
#define PARAM_PRIMARY_VALUE 5
#define PARAM_STATUS        6
#define PARAM_UNIT          7

// PA Status Byte Flags
#define STATUS_GOOD         0x80
#define STATUS_UNCERTAIN    0x40
#define STATUS_BAD          0x00
#define STATUS_OUT_OF_SPEC  0x04
#define STATUS_MAINTENANCE  0x02
#define STATUS_SIMULATE     0x01

// PA Device Operating Modes
typedef enum {
    MODE_AUTO = 0,
    MODE_MANUAL = 1,
    MODE_OUT_OF_SERVICE = 2,
    MODE_INITIALIZE = 3,
    MODE_LOCAL_OVERRIDE = 4
} PA_Mode;

// PA Value Structure (simplified)
typedef struct {
    float value;
    uint8_t status;
    uint8_t quality;
} PA_Value;

// PA Analog Input Block Structure
typedef struct {
    char tag[32];
    char descriptor[32];
    PA_Mode mode;
    PA_Value primary_value;
    uint16_t unit_code;  // Engineering units (e.g., °C, bar, m³/h)
    float range_high;
    float range_low;
} PA_AnalogInputBlock;

// PA Device Profile Structure
typedef struct {
    uint8_t profile_id;
    uint8_t profile_version;
    char manufacturer[32];
    char device_type[32];
    PA_AnalogInputBlock ai_block;
} PA_DeviceProfile;

// Initialize a temperature transmitter profile
void pa_init_temperature_transmitter(PA_DeviceProfile* device) {
    device->profile_id = 3;  // Temperature transmitter profile
    device->profile_version = 0;
    strcpy(device->manufacturer, "ExampleCorp");
    strcpy(device->device_type, "TT-100");
    
    // Initialize AI block
    strcpy(device->ai_block.tag, "TIC-101");
    strcpy(device->ai_block.descriptor, "Reactor Temperature");
    device->ai_block.mode = MODE_AUTO;
    device->ai_block.primary_value.value = 0.0;
    device->ai_block.primary_value.status = STATUS_GOOD;
    device->ai_block.primary_value.quality = 100;
    device->ai_block.unit_code = 1001;  // °C (Celsius)
    device->ai_block.range_high = 200.0;
    device->ai_block.range_low = 0.0;
}

// Read parameter from PA device
int pa_read_parameter(PA_DeviceProfile* device, uint8_t block, 
                      uint8_t index, void* data, size_t* len) {
    if (block == ANALOG_INPUT_BLOCK) {
        switch (index) {
            case PARAM_TAG:
                strcpy((char*)data, device->ai_block.tag);
                *len = strlen(device->ai_block.tag);
                return 0;
            
            case PARAM_MODE:
                *(PA_Mode*)data = device->ai_block.mode;
                *len = sizeof(PA_Mode);
                return 0;
            
            case PARAM_PRIMARY_VALUE:
                *(PA_Value*)data = device->ai_block.primary_value;
                *len = sizeof(PA_Value);
                return 0;
            
            case PARAM_UNIT:
                *(uint16_t*)data = device->ai_block.unit_code;
                *len = sizeof(uint16_t);
                return 0;
                
            default:
                return -1;  // Parameter not found
        }
    }
    return -1;  // Block not found
}

// Write parameter to PA device
int pa_write_parameter(PA_DeviceProfile* device, uint8_t block,
                       uint8_t index, const void* data, size_t len) {
    if (block == ANALOG_INPUT_BLOCK) {
        switch (index) {
            case PARAM_MODE:
                if (len == sizeof(PA_Mode)) {
                    device->ai_block.mode = *(const PA_Mode*)data;
                    return 0;
                }
                break;
            
            case PARAM_TAG:
                if (len < sizeof(device->ai_block.tag)) {
                    memcpy(device->ai_block.tag, data, len);
                    device->ai_block.tag[len] = '\0';
                    return 0;
                }
                break;
        }
    }
    return -1;  // Write failed
}

// Update process value (simulating sensor reading)
void pa_update_process_value(PA_DeviceProfile* device, float new_value) {
    device->ai_block.primary_value.value = new_value;
    
    // Check if value is within range
    if (new_value > device->ai_block.range_high ||
        new_value < device->ai_block.range_low) {
        device->ai_block.primary_value.status = STATUS_UNCERTAIN | STATUS_OUT_OF_SPEC;
        device->ai_block.primary_value.quality = 50;
    } else {
        device->ai_block.primary_value.status = STATUS_GOOD;
        device->ai_block.primary_value.quality = 100;
    }
}

// Example usage
int main() {
    PA_DeviceProfile temp_transmitter;
    
    // Initialize device
    pa_init_temperature_transmitter(&temp_transmitter);
    
    printf("PA Device Profile Demo\n");
    printf("======================\n");
    printf("Device: %s %s\n", temp_transmitter.manufacturer, 
           temp_transmitter.device_type);
    printf("Tag: %s\n", temp_transmitter.ai_block.tag);
    printf("Description: %s\n", temp_transmitter.ai_block.descriptor);
    
    // Simulate process value updates
    pa_update_process_value(&temp_transmitter, 85.5);
    printf("\nProcess Value: %.2f °C\n", 
           temp_transmitter.ai_block.primary_value.value);
    printf("Status: 0x%02X\n", 
           temp_transmitter.ai_block.primary_value.status);
    printf("Quality: %d%%\n", 
           temp_transmitter.ai_block.primary_value.quality);
    
    // Read parameter example
    PA_Value pv;
    size_t len;
    if (pa_read_parameter(&temp_transmitter, ANALOG_INPUT_BLOCK,
                         PARAM_PRIMARY_VALUE, &pv, &len) == 0) {
        printf("\nRead PV: %.2f (Status: 0x%02X)\n", pv.value, pv.status);
    }
    
    // Write parameter example (change mode)
    PA_Mode new_mode = MODE_MANUAL;
    if (pa_write_parameter(&temp_transmitter, ANALOG_INPUT_BLOCK,
                          PARAM_MODE, &new_mode, sizeof(new_mode)) == 0) {
        printf("Mode changed to: %d\n", temp_transmitter.ai_block.mode);
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

// PA Profile Block Types
const PHYSICAL_BLOCK: u8 = 0;
const TRANSDUCER_BLOCK: u8 = 1;
const ANALOG_INPUT_BLOCK: u8 = 2;
const ANALOG_OUTPUT_BLOCK: u8 = 3;

// Standard Parameter Indices
const PARAM_TAG: u8 = 1;
const PARAM_DESCRIPTOR: u8 = 2;
const PARAM_MODE: u8 = 4;
const PARAM_PRIMARY_VALUE: u8 = 5;
const PARAM_STATUS: u8 = 6;
const PARAM_UNIT: u8 = 7;

// PA Status Flags
const STATUS_GOOD: u8 = 0x80;
const STATUS_UNCERTAIN: u8 = 0x40;
const STATUS_BAD: u8 = 0x00;
const STATUS_OUT_OF_SPEC: u8 = 0x04;
const STATUS_MAINTENANCE: u8 = 0x02;

// PA Device Operating Modes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum PaMode {
    Auto = 0,
    Manual = 1,
    OutOfService = 2,
    Initialize = 3,
    LocalOverride = 4,
}

// PA Value with status and quality
#[derive(Debug, Clone, Copy)]
pub struct PaValue {
    pub value: f32,
    pub status: u8,
    pub quality: u8,
}

impl PaValue {
    pub fn new(value: f32) -> Self {
        Self {
            value,
            status: STATUS_GOOD,
            quality: 100,
        }
    }

    pub fn is_good(&self) -> bool {
        (self.status & STATUS_GOOD) != 0
    }

    pub fn is_uncertain(&self) -> bool {
        (self.status & STATUS_UNCERTAIN) != 0
    }
}

// PA Analog Input Block
#[derive(Debug, Clone)]
pub struct PaAnalogInputBlock {
    pub tag: String,
    pub descriptor: String,
    pub mode: PaMode,
    pub primary_value: PaValue,
    pub unit_code: u16,
    pub range_high: f32,
    pub range_low: f32,
}

impl PaAnalogInputBlock {
    pub fn new(tag: &str, descriptor: &str) -> Self {
        Self {
            tag: tag.to_string(),
            descriptor: descriptor.to_string(),
            mode: PaMode::Auto,
            primary_value: PaValue::new(0.0),
            unit_code: 1001, // °C
            range_high: 200.0,
            range_low: 0.0,
        }
    }

    pub fn update_value(&mut self, new_value: f32) {
        self.primary_value.value = new_value;

        if new_value > self.range_high || new_value < self.range_low {
            self.primary_value.status = STATUS_UNCERTAIN | STATUS_OUT_OF_SPEC;
            self.primary_value.quality = 50;
        } else {
            self.primary_value.status = STATUS_GOOD;
            self.primary_value.quality = 100;
        }
    }
}

// PA Device Profile
#[derive(Debug, Clone)]
pub struct PaDeviceProfile {
    pub profile_id: u8,
    pub profile_version: u8,
    pub manufacturer: String,
    pub device_type: String,
    pub ai_block: PaAnalogInputBlock,
}

impl PaDeviceProfile {
    pub fn new_temperature_transmitter(
        tag: &str,
        manufacturer: &str,
        device_type: &str,
    ) -> Self {
        Self {
            profile_id: 3, // Temperature transmitter
            profile_version: 0,
            manufacturer: manufacturer.to_string(),
            device_type: device_type.to_string(),
            ai_block: PaAnalogInputBlock::new(tag, "Temperature Sensor"),
        }
    }

    // Read parameter from device
    pub fn read_parameter(&self, block: u8, index: u8) -> Result<Vec<u8>, String> {
        if block == ANALOG_INPUT_BLOCK {
            match index {
                PARAM_TAG => Ok(self.ai_block.tag.as_bytes().to_vec()),
                PARAM_DESCRIPTOR => Ok(self.ai_block.descriptor.as_bytes().to_vec()),
                PARAM_MODE => Ok(vec![self.ai_block.mode as u8]),
                PARAM_PRIMARY_VALUE => {
                    let mut data = Vec::new();
                    data.extend_from_slice(&self.ai_block.primary_value.value.to_le_bytes());
                    data.push(self.ai_block.primary_value.status);
                    data.push(self.ai_block.primary_value.quality);
                    Ok(data)
                }
                PARAM_UNIT => Ok(self.ai_block.unit_code.to_le_bytes().to_vec()),
                _ => Err("Parameter not found".to_string()),
            }
        } else {
            Err("Block not found".to_string())
        }
    }

    // Write parameter to device
    pub fn write_parameter(&mut self, block: u8, index: u8, data: &[u8]) -> Result<(), String> {
        if block == ANALOG_INPUT_BLOCK {
            match index {
                PARAM_TAG => {
                    self.ai_block.tag = String::from_utf8_lossy(data).to_string();
                    Ok(())
                }
                PARAM_MODE => {
                    if !data.is_empty() {
                        self.ai_block.mode = match data[0] {
                            0 => PaMode::Auto,
                            1 => PaMode::Manual,
                            2 => PaMode::OutOfService,
                            3 => PaMode::Initialize,
                            4 => PaMode::LocalOverride,
                            _ => return Err("Invalid mode".to_string()),
                        };
                        Ok(())
                    } else {
                        Err("Empty data".to_string())
                    }
                }
                _ => Err("Parameter not writable".to_string()),
            }
        } else {
            Err("Block not found".to_string())
        }
    }
}

impl fmt::Display for PaDeviceProfile {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "PA Device: {} {}\nTag: {}\nValue: {:.2} (Status: 0x{:02X}, Quality: {}%)",
            self.manufacturer,
            self.device_type,
            self.ai_block.tag,
            self.ai_block.primary_value.value,
            self.ai_block.primary_value.status,
            self.ai_block.primary_value.quality
        )
    }
}

// Example usage
fn main() {
    println!("PA Device Profile Demo (Rust)");
    println!("==============================\n");

    // Create temperature transmitter
    let mut temp_transmitter = PaDeviceProfile::new_temperature_transmitter(
        "TIC-101",
        "ExampleCorp",
        "TT-100",
    );

    println!("{}\n", temp_transmitter);

    // Update process value
    temp_transmitter.ai_block.update_value(85.5);
    println!("Updated value: {:.2} °C", temp_transmitter.ai_block.primary_value.value);
    println!("Status: {:?}\n", 
             if temp_transmitter.ai_block.primary_value.is_good() { "GOOD" } else { "BAD" });

    // Read parameter
    match temp_transmitter.read_parameter(ANALOG_INPUT_BLOCK, PARAM_PRIMARY_VALUE) {
        Ok(data) => {
            if data.len() >= 4 {
                let value = f32::from_le_bytes([data[0], data[1], data[2], data[3]]);
                println!("Read PV via parameter access: {:.2}", value);
            }
        }
        Err(e) => println!("Read error: {}", e),
    }

    // Write parameter (change mode)
    if temp_transmitter.write_parameter(ANALOG_INPUT_BLOCK, PARAM_MODE, &[1]).is_ok() {
        println!("Mode changed to: {:?}", temp_transmitter.ai_block.mode);
    }

    // Simulate out-of-range condition
    temp_transmitter.ai_block.update_value(250.0);
    println!("\nOut-of-range test:");
    println!("Value: {:.2} °C", temp_transmitter.ai_block.primary_value.value);
    println!("Status: 0x{:02X}", temp_transmitter.ai_block.primary_value.status);
    println!("Quality: {}%", temp_transmitter.ai_block.primary_value.quality);
}
```

## Summary

**PA Device Profiles** are essential standardized specifications in PROFIBUS Process Automation that define how process instruments communicate and present their data. They establish a uniform block-oriented architecture (Physical Block, Transducer Block, Function Blocks) based on IEC 61804, ensuring that devices from different manufacturers can be integrated seamlessly into process control systems.

Key benefits include:
- **Interoperability**: Devices with the same profile type can be substituted without changing control logic
- **Standardized Parameters**: Common parameter structure simplifies engineering and maintenance
- **Diagnostic Information**: Built-in status and quality indicators for process values
- **Reduced Integration Effort**: Pre-defined profiles accelerate commissioning and configuration

The code examples demonstrate how to implement PA device profile structures, manage block-oriented parameter access, handle process values with status/quality information, and perform read/write operations on standardized parameters. This abstraction layer is crucial for building scalable process automation systems that can accommodate devices from multiple vendors while maintaining consistent behavior and interfaces.