# Profibus Slot and Module Configuration

## Detailed Description

Slot and Module Configuration in Profibus refers to the process of defining and managing the hardware structure of modular devices on a Profibus network. Modular devices, such as remote I/O systems, distributed control units, and complex field devices, consist of a base unit (carrier) with multiple slots that can accept various modules (I/O modules, communication modules, function modules, etc.).

### Key Concepts

**1. Modular Device Architecture**
- **Slots**: Physical or logical positions in a device where modules can be installed
- **Modules**: Individual functional units (digital I/O, analog I/O, special functions)
- **Carrier/Base Unit**: The main structure that houses the modules
- **Module Identifiers**: Unique codes identifying module types and capabilities

**2. Configuration Data Structure**
- **Slot Assignment**: Mapping physical slots to logical addresses
- **Module Configuration**: Defining module-specific parameters and data formats
- **Consistency Checking**: Ensuring configured modules match physically installed modules
- **Configuration Telegram**: GSD-based configuration data sent during startup

**3. GSD File Role**
The General Station Description (GSD) file contains:
- Supported module types and their identifiers
- Data format for each module (input/output lengths)
- Valid slot combinations
- Module-specific parameters
- Diagnostic capabilities

**4. Configuration Process**
1. **Offline Configuration**: Define expected hardware layout in engineering tool
2. **Startup Phase**: Master sends configuration to slave
3. **Verification**: Slave validates actual hardware against configuration
4. **Diagnostic Feedback**: Report any mismatches or errors
5. **Data Exchange**: Establish cyclic communication based on configuration

### Data Flow

```
Master                          Modular Slave
  |                                  |
  |--- Configuration Telegram ------>| (Slot/Module definitions)
  |                                  |
  |<-- Module Identity Response -----|
  |                                  |
  |--- Parameter Data -------------->| (Module-specific params)
  |                                  |
  |<-- Configuration OK/Error -------|
  |                                  |
  |<=== Cyclic I/O Data Exchange ===>|
```

---

## Programming Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Module type identifiers (example values)
#define MODULE_TYPE_EMPTY       0x00
#define MODULE_TYPE_DI_16       0x01  // 16 Digital Inputs
#define MODULE_TYPE_DO_16       0x02  // 16 Digital Outputs
#define MODULE_TYPE_AI_8        0x03  // 8 Analog Inputs
#define MODULE_TYPE_AO_4        0x04  // 4 Analog Outputs

// Maximum slots per device
#define MAX_SLOTS 16

// Module configuration structure
typedef struct {
    uint8_t module_type;      // Module type identifier
    uint8_t input_length;     // Input data length in bytes
    uint8_t output_length;    // Output data length in bytes
    uint16_t module_id;       // Specific module identifier
    bool is_present;          // Module physically present
} ModuleConfig;

// Slot configuration structure
typedef struct {
    uint8_t slot_number;
    ModuleConfig module;
    uint8_t* input_data;      // Pointer to input data area
    uint8_t* output_data;     // Pointer to output data area
} SlotConfig;

// Device configuration
typedef struct {
    uint8_t station_address;
    uint8_t num_slots;
    SlotConfig slots[MAX_SLOTS];
    uint8_t total_input_length;
    uint8_t total_output_length;
} DeviceConfig;

// Initialize device configuration
void init_device_config(DeviceConfig* device, uint8_t address) {
    device->station_address = address;
    device->num_slots = 0;
    device->total_input_length = 0;
    device->total_output_length = 0;
    
    for (int i = 0; i < MAX_SLOTS; i++) {
        device->slots[i].slot_number = i;
        device->slots[i].module.module_type = MODULE_TYPE_EMPTY;
        device->slots[i].module.is_present = false;
        device->slots[i].input_data = NULL;
        device->slots[i].output_data = NULL;
    }
}

// Configure a slot with a specific module
bool configure_slot(DeviceConfig* device, uint8_t slot_num, 
                   uint8_t module_type, uint16_t module_id) {
    if (slot_num >= MAX_SLOTS) {
        printf("Error: Invalid slot number %d\n", slot_num);
        return false;
    }
    
    SlotConfig* slot = &device->slots[slot_num];
    slot->module.module_type = module_type;
    slot->module.module_id = module_id;
    
    // Set data lengths based on module type
    switch (module_type) {
        case MODULE_TYPE_DI_16:
            slot->module.input_length = 2;   // 16 bits = 2 bytes
            slot->module.output_length = 0;
            break;
        case MODULE_TYPE_DO_16:
            slot->module.input_length = 0;
            slot->module.output_length = 2;
            break;
        case MODULE_TYPE_AI_8:
            slot->module.input_length = 16;  // 8 channels * 2 bytes
            slot->module.output_length = 0;
            break;
        case MODULE_TYPE_AO_4:
            slot->module.input_length = 0;
            slot->module.output_length = 8;  // 4 channels * 2 bytes
            break;
        default:
            printf("Error: Unknown module type 0x%02X\n", module_type);
            return false;
    }
    
    device->num_slots = (slot_num + 1 > device->num_slots) ? 
                        slot_num + 1 : device->num_slots;
    
    return true;
}

// Build configuration telegram for Profibus
uint8_t build_configuration_telegram(DeviceConfig* device, uint8_t* telegram) {
    uint8_t index = 0;
    
    // Configuration telegram header
    telegram[index++] = device->station_address;
    telegram[index++] = device->num_slots;
    
    // Add module identifiers for each configured slot
    for (uint8_t i = 0; i < device->num_slots; i++) {
        if (device->slots[i].module.module_type != MODULE_TYPE_EMPTY) {
            telegram[index++] = device->slots[i].slot_number;
            telegram[index++] = device->slots[i].module.module_type;
            telegram[index++] = (uint8_t)(device->slots[i].module.module_id >> 8);
            telegram[index++] = (uint8_t)(device->slots[i].module.module_id & 0xFF);
            telegram[index++] = device->slots[i].module.input_length;
            telegram[index++] = device->slots[i].module.output_length;
        }
    }
    
    return index; // Return telegram length
}

// Allocate I/O data buffers
bool allocate_io_buffers(DeviceConfig* device) {
    device->total_input_length = 0;
    device->total_output_length = 0;
    
    // Calculate total I/O lengths
    for (uint8_t i = 0; i < device->num_slots; i++) {
        device->total_input_length += device->slots[i].module.input_length;
        device->total_output_length += device->slots[i].module.output_length;
    }
    
    // Allocate buffers
    static uint8_t input_buffer[256];
    static uint8_t output_buffer[256];
    
    uint8_t input_offset = 0;
    uint8_t output_offset = 0;
    
    for (uint8_t i = 0; i < device->num_slots; i++) {
        if (device->slots[i].module.input_length > 0) {
            device->slots[i].input_data = &input_buffer[input_offset];
            input_offset += device->slots[i].module.input_length;
        }
        if (device->slots[i].module.output_length > 0) {
            device->slots[i].output_data = &output_buffer[output_offset];
            output_offset += device->slots[i].module.output_length;
        }
    }
    
    return true;
}

// Verify actual hardware matches configuration
bool verify_configuration(DeviceConfig* device, uint8_t* hardware_response) {
    // Parse hardware identification response
    uint8_t reported_slots = hardware_response[0];
    uint8_t index = 1;
    
    for (uint8_t i = 0; i < reported_slots && i < device->num_slots; i++) {
        uint8_t slot_num = hardware_response[index++];
        uint8_t module_type = hardware_response[index++];
        uint16_t module_id = (hardware_response[index] << 8) | hardware_response[index + 1];
        index += 2;
        
        if (device->slots[slot_num].module.module_type != module_type ||
            device->slots[slot_num].module.module_id != module_id) {
            printf("Configuration mismatch at slot %d: expected 0x%02X/0x%04X, got 0x%02X/0x%04X\n",
                   slot_num,
                   device->slots[slot_num].module.module_type,
                   device->slots[slot_num].module.module_id,
                   module_type, module_id);
            return false;
        }
        
        device->slots[slot_num].module.is_present = true;
    }
    
    return true;
}

// Example usage
int main() {
    DeviceConfig device;
    uint8_t config_telegram[256];
    
    // Initialize device at station address 5
    init_device_config(&device, 5);
    
    // Configure slots
    configure_slot(&device, 0, MODULE_TYPE_DI_16, 0x1234);
    configure_slot(&device, 1, MODULE_TYPE_DO_16, 0x1235);
    configure_slot(&device, 2, MODULE_TYPE_AI_8, 0x3456);
    configure_slot(&device, 3, MODULE_TYPE_AO_4, 0x3457);
    
    // Build configuration telegram
    uint8_t telegram_length = build_configuration_telegram(&device, config_telegram);
    
    printf("Configuration Telegram (%d bytes):\n", telegram_length);
    for (int i = 0; i < telegram_length; i++) {
        printf("%02X ", config_telegram[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n\n");
    
    // Allocate I/O buffers
    allocate_io_buffers(&device);
    
    printf("Device Configuration Summary:\n");
    printf("Station Address: %d\n", device.station_address);
    printf("Number of Slots: %d\n", device.num_slots);
    printf("Total Input Length: %d bytes\n", device.total_input_length);
    printf("Total Output Length: %d bytes\n\n", device.total_output_length);
    
    // Print slot details
    for (uint8_t i = 0; i < device.num_slots; i++) {
        if (device.slots[i].module.module_type != MODULE_TYPE_EMPTY) {
            printf("Slot %d: Type=0x%02X, ID=0x%04X, In=%d bytes, Out=%d bytes\n",
                   i,
                   device.slots[i].module.module_type,
                   device.slots[i].module.module_id,
                   device.slots[i].module.input_length,
                   device.slots[i].module.output_length);
        }
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

// Module type identifiers
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum ModuleType {
    Empty = 0x00,
    DigitalInput16 = 0x01,
    DigitalOutput16 = 0x02,
    AnalogInput8 = 0x03,
    AnalogOutput4 = 0x04,
}

impl ModuleType {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x00 => Some(ModuleType::Empty),
            0x01 => Some(ModuleType::DigitalInput16),
            0x02 => Some(ModuleType::DigitalOutput16),
            0x03 => Some(ModuleType::AnalogInput8),
            0x04 => Some(ModuleType::AnalogOutput4),
            _ => None,
        }
    }
}

// Module configuration
#[derive(Debug, Clone)]
pub struct ModuleConfig {
    pub module_type: ModuleType,
    pub module_id: u16,
    pub input_length: usize,
    pub output_length: usize,
    pub is_present: bool,
}

impl ModuleConfig {
    fn new(module_type: ModuleType, module_id: u16) -> Self {
        let (input_length, output_length) = match module_type {
            ModuleType::Empty => (0, 0),
            ModuleType::DigitalInput16 => (2, 0),
            ModuleType::DigitalOutput16 => (0, 2),
            ModuleType::AnalogInput8 => (16, 0),
            ModuleType::AnalogOutput4 => (0, 8),
        };

        ModuleConfig {
            module_type,
            module_id,
            input_length,
            output_length,
            is_present: false,
        }
    }
}

// Slot configuration
#[derive(Debug, Clone)]
pub struct SlotConfig {
    pub slot_number: u8,
    pub module: ModuleConfig,
    pub input_offset: usize,
    pub output_offset: usize,
}

impl SlotConfig {
    fn new(slot_number: u8) -> Self {
        SlotConfig {
            slot_number,
            module: ModuleConfig::new(ModuleType::Empty, 0),
            input_offset: 0,
            output_offset: 0,
        }
    }
}

// Device configuration
pub struct DeviceConfig {
    pub station_address: u8,
    pub slots: Vec<SlotConfig>,
    pub input_buffer: Vec<u8>,
    pub output_buffer: Vec<u8>,
}

impl DeviceConfig {
    pub fn new(station_address: u8, max_slots: usize) -> Self {
        let mut slots = Vec::with_capacity(max_slots);
        for i in 0..max_slots {
            slots.push(SlotConfig::new(i as u8));
        }

        DeviceConfig {
            station_address,
            slots,
            input_buffer: Vec::new(),
            output_buffer: Vec::new(),
        }
    }

    pub fn configure_slot(
        &mut self,
        slot_number: u8,
        module_type: ModuleType,
        module_id: u16,
    ) -> Result<(), String> {
        let slot_index = slot_number as usize;
        
        if slot_index >= self.slots.len() {
            return Err(format!("Invalid slot number: {}", slot_number));
        }

        self.slots[slot_index].module = ModuleConfig::new(module_type, module_id);
        Ok(())
    }

    pub fn allocate_io_buffers(&mut self) {
        let mut input_offset = 0;
        let mut output_offset = 0;
        let mut total_input = 0;
        let mut total_output = 0;

        // Calculate offsets and totals
        for slot in &mut self.slots {
            if slot.module.module_type != ModuleType::Empty {
                slot.input_offset = input_offset;
                slot.output_offset = output_offset;
                
                input_offset += slot.module.input_length;
                output_offset += slot.module.output_length;
                
                total_input += slot.module.input_length;
                total_output += slot.module.output_length;
            }
        }

        // Allocate buffers
        self.input_buffer = vec![0u8; total_input];
        self.output_buffer = vec![0u8; total_output];
    }

    pub fn build_configuration_telegram(&self) -> Vec<u8> {
        let mut telegram = Vec::new();
        
        telegram.push(self.station_address);
        
        // Count configured slots
        let configured_slots: Vec<_> = self.slots.iter()
            .filter(|s| s.module.module_type != ModuleType::Empty)
            .collect();
        
        telegram.push(configured_slots.len() as u8);

        // Add module configurations
        for slot in configured_slots {
            telegram.push(slot.slot_number);
            telegram.push(slot.module.module_type as u8);
            telegram.push((slot.module.module_id >> 8) as u8);
            telegram.push((slot.module.module_id & 0xFF) as u8);
            telegram.push(slot.module.input_length as u8);
            telegram.push(slot.module.output_length as u8);
        }

        telegram
    }

    pub fn verify_configuration(&mut self, hardware_response: &[u8]) -> Result<(), String> {
        if hardware_response.is_empty() {
            return Err("Empty hardware response".to_string());
        }

        let reported_slots = hardware_response[0] as usize;
        let mut index = 1;

        for _ in 0..reported_slots {
            if index + 5 >= hardware_response.len() {
                return Err("Incomplete hardware response".to_string());
            }

            let slot_num = hardware_response[index];
            let module_type_val = hardware_response[index + 1];
            let module_id = ((hardware_response[index + 2] as u16) << 8) 
                          | (hardware_response[index + 3] as u16);
            index += 4;

            let module_type = ModuleType::from_u8(module_type_val)
                .ok_or_else(|| format!("Unknown module type: 0x{:02X}", module_type_val))?;

            let slot = &self.slots[slot_num as usize];
            
            if slot.module.module_type != module_type || slot.module.module_id != module_id {
                return Err(format!(
                    "Configuration mismatch at slot {}: expected {:?}/0x{:04X}, got {:?}/0x{:04X}",
                    slot_num, slot.module.module_type, slot.module.module_id,
                    module_type, module_id
                ));
            }

            self.slots[slot_num as usize].module.is_present = true;
        }

        Ok(())
    }

    pub fn get_slot_input_data(&self, slot_number: u8) -> Option<&[u8]> {
        let slot = &self.slots[slot_number as usize];
        if slot.module.input_length > 0 {
            let start = slot.input_offset;
            let end = start + slot.module.input_length;
            Some(&self.input_buffer[start..end])
        } else {
            None
        }
    }

    pub fn get_slot_output_data_mut(&mut self, slot_number: u8) -> Option<&mut [u8]> {
        let slot = &self.slots[slot_number as usize];
        if slot.module.output_length > 0 {
            let start = slot.output_offset;
            let end = start + slot.module.output_length;
            Some(&mut self.output_buffer[start..end])
        } else {
            None
        }
    }
}

impl fmt::Display for DeviceConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "Device Configuration Summary:")?;
        writeln!(f, "Station Address: {}", self.station_address)?;
        writeln!(f, "Total Input Length: {} bytes", self.input_buffer.len())?;
        writeln!(f, "Total Output Length: {} bytes", self.output_buffer.len())?;
        writeln!(f, "\nConfigured Slots:")?;
        
        for slot in &self.slots {
            if slot.module.module_type != ModuleType::Empty {
                writeln!(
                    f,
                    "  Slot {}: Type={:?}, ID=0x{:04X}, In={} bytes, Out={} bytes, Present={}",
                    slot.slot_number,
                    slot.module.module_type,
                    slot.module.module_id,
                    slot.module.input_length,
                    slot.module.output_length,
                    slot.module.is_present
                )?;
            }
        }
        
        Ok(())
    }
}

// Example usage
fn main() {
    let mut device = DeviceConfig::new(5, 16);

    // Configure slots
    device.configure_slot(0, ModuleType::DigitalInput16, 0x1234).unwrap();
    device.configure_slot(1, ModuleType::DigitalOutput16, 0x1235).unwrap();
    device.configure_slot(2, ModuleType::AnalogInput8, 0x3456).unwrap();
    device.configure_slot(3, ModuleType::AnalogOutput4, 0x3457).unwrap();

    // Allocate I/O buffers
    device.allocate_io_buffers();

    // Build configuration telegram
    let telegram = device.build_configuration_telegram();
    println!("Configuration Telegram ({} bytes):", telegram.len());
    for (i, byte) in telegram.iter().enumerate() {
        print!("{:02X} ", byte);
        if (i + 1) % 16 == 0 {
            println!();
        }
    }
    println!("\n");

    // Display configuration
    println!("{}", device);

    // Simulate hardware verification
    let hardware_response = vec![
        4,    // 4 modules present
        0, 0x01, 0x12, 0x34,  // Slot 0: DI16, ID 0x1234
        1, 0x02, 0x12, 0x35,  // Slot 1: DO16, ID 0x1235
        2, 0x03, 0x34, 0x56,  // Slot 2: AI8, ID 0x3456
        3, 0x04, 0x34, 0x57,  // Slot 3: AO4, ID 0x3457
    ];

    match device.verify_configuration(&hardware_response) {
        Ok(_) => println!("\n✓ Configuration verified successfully"),
        Err(e) => println!("\n✗ Configuration verification failed: {}", e),
    }

    // Example: Write to output module (slot 1)
    if let Some(output_data) = device.get_slot_output_data_mut(1) {
        output_data[0] = 0xFF;  // Set all outputs high
        output_data[1] = 0x00;
        println!("\nSet outputs on slot 1: {:02X} {:02X}", output_data[0], output_data[1]);
    }
}
```

---

## Summary

**Slot and Module Configuration** is fundamental to working with modular Profibus devices. It involves defining the hardware structure (which modules are installed in which slots), communicating this configuration to devices during startup, verifying that the actual hardware matches the expected configuration, and establishing the proper data exchange format.

**Key Points:**

1. **Modular Architecture**: Devices consist of slots that accept various module types (I/O, communication, special functions)

2. **Configuration Process**: Master sends configuration data to slaves, which verify against actual hardware and report mismatches

3. **GSD Files**: Define valid module combinations, data formats, and capabilities for each device type

4. **Data Mapping**: Each module's input/output data is mapped to specific offsets in the cyclic data exchange

5. **Verification Critical**: Configuration mismatches between expected and actual hardware must be detected before entering operational mode

6. **Dynamic Behavior**: Some systems support hot-swapping or online reconfiguration with appropriate diagnostic feedback

This configuration mechanism enables flexible, scalable automation systems where devices can be adapted to specific application requirements through modular hardware composition while maintaining communication integrity and diagnostic capabilities.