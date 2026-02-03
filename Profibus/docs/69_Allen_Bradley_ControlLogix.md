# Allen Bradley ControlLogix - Profibus Integration

## Detailed Description

### Overview
Allen Bradley ControlLogix is Rockwell Automation's flagship programmable logic controller (PLC) platform, part of the Logix family. While ControlLogix natively uses EtherNet/IP and ControlNet as its primary communication protocols, integrating Profibus devices requires special scanner modules that act as bridges between the Profibus network and the ControlLogix backplane.

### Integration Architecture

**Profibus Scanner Modules:**
The integration is typically achieved using:
- **1756-PB75** - Profibus DP Master/Scanner Module for ControlLogix
- **1756-PB72** - Alternative Profibus scanner option

These modules mount in the ControlLogix chassis and provide:
- Master functionality on the Profibus DP network
- Data mapping between Profibus slaves and ControlLogix I/O memory
- Configuration through RSNetWorx for Profibus or Studio 5000/RSLogix 5000
- Support for up to 125 Profibus slaves per scanner

**Communication Flow:**
```
Profibus Devices (Slaves) ←→ 1756-PB75 Scanner ←→ ControlLogix Backplane ←→ ControlLogix Processor
```

### Key Features

1. **Multi-Protocol Integration**: Allows legacy Profibus equipment to coexist with modern EtherNet/IP infrastructure
2. **High-Speed Data Exchange**: Profibus DP operates at speeds up to 12 Mbps
3. **Deterministic Communication**: Guaranteed cyclic data exchange with Profibus slaves
4. **GSD File Support**: Uses standard Profibus GSD (General Station Description) files for device configuration
5. **Diagnostics**: Comprehensive fault detection and status reporting

### Configuration Process

1. **Hardware Installation**: Mount the 1756-PB75 in a ControlLogix chassis
2. **Network Configuration**: Use RSNetWorx for Profibus to configure the Profibus network topology
3. **Device Addition**: Import GSD files and add Profibus slaves to the network
4. **I/O Mapping**: Map Profibus slave data to ControlLogix tags
5. **PLC Programming**: Access Profibus data through standard ControlLogix tags in ladder logic or structured text

## Programming Examples

### C/C++ Example - Simulating Profibus Scanner Communication

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Profibus DP telegram structure
#define MAX_PROFIBUS_DATA 244
#define MAX_SLAVES 125

// Profibus slave configuration
typedef struct {
    uint8_t station_address;
    uint8_t ident_number[2];
    bool active;
    uint8_t input_length;
    uint8_t output_length;
} ProfibusSlaveConfig;

// Profibus data exchange structure
typedef struct {
    uint8_t input_data[MAX_PROFIBUS_DATA];
    uint8_t output_data[MAX_PROFIBUS_DATA];
    uint8_t input_len;
    uint8_t output_len;
    uint16_t status;
} ProfibusSlaveData;

// ControlLogix scanner module simulation
typedef struct {
    char module_name[32];
    uint8_t slot_number;
    ProfibusSlaveConfig slaves[MAX_SLAVES];
    ProfibusSlaveData slave_data[MAX_SLAVES];
    uint8_t slave_count;
    bool scanner_running;
    uint32_t scan_cycle_time_us;
} ControlLogixProfibusScanner;

// Initialize Profibus scanner
void init_profibus_scanner(ControlLogixProfibusScanner *scanner, 
                           const char *name, uint8_t slot) {
    strncpy(scanner->module_name, name, sizeof(scanner->module_name) - 1);
    scanner->slot_number = slot;
    scanner->slave_count = 0;
    scanner->scanner_running = false;
    scanner->scan_cycle_time_us = 5000; // 5ms default cycle time
    
    printf("[Scanner] Initialized %s in slot %d\n", name, slot);
}

// Add Profibus slave device
int add_profibus_slave(ControlLogixProfibusScanner *scanner,
                       uint8_t address, uint8_t input_len, uint8_t output_len) {
    if (scanner->slave_count >= MAX_SLAVES) {
        printf("[Error] Maximum slave count reached\n");
        return -1;
    }
    
    ProfibusSlaveConfig *slave = &scanner->slaves[scanner->slave_count];
    slave->station_address = address;
    slave->active = true;
    slave->input_length = input_len;
    slave->output_length = output_len;
    
    // Initialize data buffers
    scanner->slave_data[scanner->slave_count].input_len = input_len;
    scanner->slave_data[scanner->slave_count].output_len = output_len;
    memset(scanner->slave_data[scanner->slave_count].input_data, 0, input_len);
    memset(scanner->slave_data[scanner->slave_count].output_data, 0, output_len);
    
    scanner->slave_count++;
    printf("[Scanner] Added slave at address %d (In: %d bytes, Out: %d bytes)\n",
           address, input_len, output_len);
    
    return scanner->slave_count - 1;
}

// Start Profibus scanner
void start_scanner(ControlLogixProfibusScanner *scanner) {
    scanner->scanner_running = true;
    printf("[Scanner] Started - Managing %d slaves\n", scanner->slave_count);
}

// Cyclic data exchange simulation
void profibus_cycle(ControlLogixProfibusScanner *scanner) {
    if (!scanner->scanner_running) return;
    
    for (int i = 0; i < scanner->slave_count; i++) {
        if (scanner->slaves[i].active) {
            // Simulate reading input data from Profibus slave
            // In real system, this would be hardware communication
            
            // Example: Read temperature sensor (simulated)
            scanner->slave_data[i].input_data[0]++;  // Simulated data
            scanner->slave_data[i].status = 0x8000; // Good status
            
            // Write output data to Profibus slave
            // (Output data set by ControlLogix application)
        }
    }
}

// Map Profibus data to ControlLogix tags
typedef struct {
    char tag_name[64];
    uint8_t slave_index;
    uint8_t byte_offset;
    uint8_t bit_offset;
    bool is_input;
} ControlLogixTag;

// Read Profibus input to ControlLogix tag
uint8_t read_profibus_input(ControlLogixProfibusScanner *scanner,
                             uint8_t slave_index, uint8_t byte_offset) {
    if (slave_index >= scanner->slave_count) return 0;
    if (byte_offset >= scanner->slave_data[slave_index].input_len) return 0;
    
    return scanner->slave_data[slave_index].input_data[byte_offset];
}

// Write ControlLogix data to Profibus output
void write_profibus_output(ControlLogixProfibusScanner *scanner,
                           uint8_t slave_index, uint8_t byte_offset, uint8_t value) {
    if (slave_index >= scanner->slave_count) return;
    if (byte_offset >= scanner->slave_data[slave_index].output_len) return;
    
    scanner->slave_data[slave_index].output_data[byte_offset] = value;
}

// Example application
int main() {
    printf("=== Allen Bradley ControlLogix Profibus Integration ===\n\n");
    
    // Initialize scanner in slot 2 of ControlLogix chassis
    ControlLogixProfibusScanner scanner;
    init_profibus_scanner(&scanner, "1756-PB75", 2);
    
    // Add Profibus slaves
    int temp_sensor_idx = add_profibus_slave(&scanner, 3, 4, 0);  // Address 3: Temperature sensor
    int valve_idx = add_profibus_slave(&scanner, 5, 2, 1);        // Address 5: Valve controller
    int drive_idx = add_profibus_slave(&scanner, 10, 8, 4);       // Address 10: VFD
    
    // Start scanner
    start_scanner(&scanner);
    
    // Simulate control loop
    printf("\n=== Simulating Control Cycles ===\n");
    for (int cycle = 0; cycle < 5; cycle++) {
        printf("\nCycle %d:\n", cycle + 1);
        
        // Execute Profibus data exchange
        profibus_cycle(&scanner);
        
        // Read temperature from Profibus sensor
        uint8_t temp_value = read_profibus_input(&scanner, temp_sensor_idx, 0);
        printf("  Temperature Sensor (Slave 3): %d°C\n", temp_value);
        
        // Control logic: Open valve if temperature > 50
        if (temp_value > 50) {
            write_profibus_output(&scanner, valve_idx, 0, 0x01); // Open valve
            printf("  Valve Control (Slave 5): OPEN\n");
        } else {
            write_profibus_output(&scanner, valve_idx, 0, 0x00); // Close valve
            printf("  Valve Control (Slave 5): CLOSED\n");
        }
        
        // Set VFD speed based on temperature
        uint8_t speed_setpoint = (temp_value > 30) ? 75 : 50;
        write_profibus_output(&scanner, drive_idx, 0, speed_setpoint);
        printf("  VFD Speed (Slave 10): %d%%\n", speed_setpoint);
    }
    
    printf("\n=== Scanner Statistics ===\n");
    printf("Total slaves configured: %d\n", scanner.slave_count);
    printf("Scanner cycle time: %d µs\n", scanner.scan_cycle_time_us);
    
    return 0;
}
```

### Rust Example - Type-Safe Profibus Integration

```rust
use std::collections::HashMap;
use std::fmt;

// Profibus slave configuration
#[derive(Debug, Clone)]
struct ProfibusSlaveConfig {
    station_address: u8,
    ident_number: u16,
    active: bool,
    input_length: usize,
    output_length: usize,
    gsd_file: String,
}

// Profibus slave data
#[derive(Debug, Clone)]
struct ProfibusSlaveData {
    input_data: Vec<u8>,
    output_data: Vec<u8>,
    status: u16,
    diagnostics: Vec<u8>,
}

impl ProfibusSlaveData {
    fn new(input_len: usize, output_len: usize) -> Self {
        Self {
            input_data: vec![0; input_len],
            output_data: vec![0; output_len],
            status: 0,
            diagnostics: Vec::new(),
        }
    }
}

// ControlLogix Profibus Scanner Module
struct ControlLogixProfibusScanner {
    module_name: String,
    slot_number: u8,
    slaves: HashMap<u8, ProfibusSlaveConfig>,
    slave_data: HashMap<u8, ProfibusSlaveData>,
    scanner_running: bool,
    scan_cycle_time_us: u32,
    error_count: u32,
}

impl ControlLogixProfibusScanner {
    fn new(module_name: &str, slot_number: u8) -> Self {
        println!("[Scanner] Initializing {} in slot {}", module_name, slot_number);
        
        Self {
            module_name: module_name.to_string(),
            slot_number,
            slaves: HashMap::new(),
            slave_data: HashMap::new(),
            scanner_running: false,
            scan_cycle_time_us: 5000, // 5ms default
            error_count: 0,
        }
    }
    
    fn add_slave(&mut self, address: u8, input_len: usize, output_len: usize, 
                 gsd_file: &str) -> Result<(), String> {
        if self.slaves.contains_key(&address) {
            return Err(format!("Slave address {} already exists", address));
        }
        
        let config = ProfibusSlaveConfig {
            station_address: address,
            ident_number: 0x0000,
            active: true,
            input_length: input_len,
            output_length: output_len,
            gsd_file: gsd_file.to_string(),
        };
        
        let data = ProfibusSlaveData::new(input_len, output_len);
        
        self.slaves.insert(address, config);
        self.slave_data.insert(address, data);
        
        println!("[Scanner] Added slave at address {} (In: {} bytes, Out: {} bytes)",
                 address, input_len, output_len);
        
        Ok(())
    }
    
    fn start(&mut self) -> Result<(), String> {
        if self.slaves.is_empty() {
            return Err("No slaves configured".to_string());
        }
        
        self.scanner_running = true;
        println!("[Scanner] Started - Managing {} slaves", self.slaves.len());
        Ok(())
    }
    
    fn stop(&mut self) {
        self.scanner_running = false;
        println!("[Scanner] Stopped");
    }
    
    // Cyclic data exchange
    fn execute_cycle(&mut self) -> Result<(), String> {
        if !self.scanner_running {
            return Err("Scanner not running".to_string());
        }
        
        for (address, config) in &self.slaves {
            if config.active {
                if let Some(data) = self.slave_data.get_mut(address) {
                    // Simulate Profibus DP data exchange
                    // In real implementation, this would communicate via hardware
                    
                    // Simulate input data (e.g., sensor readings)
                    for byte in &mut data.input_data {
                        *byte = (*byte).wrapping_add(1);
                    }
                    
                    // Status: 0x8000 = OK
                    data.status = 0x8000;
                }
            }
        }
        
        Ok(())
    }
    
    // Read input data from Profibus slave
    fn read_input(&self, slave_address: u8, offset: usize, length: usize) 
                  -> Result<Vec<u8>, String> {
        let data = self.slave_data.get(&slave_address)
            .ok_or_else(|| format!("Slave {} not found", slave_address))?;
        
        if offset + length > data.input_data.len() {
            return Err("Read beyond input buffer".to_string());
        }
        
        Ok(data.input_data[offset..offset + length].to_vec())
    }
    
    // Write output data to Profibus slave
    fn write_output(&mut self, slave_address: u8, offset: usize, values: &[u8]) 
                    -> Result<(), String> {
        let data = self.slave_data.get_mut(&slave_address)
            .ok_or_else(|| format!("Slave {} not found", slave_address))?;
        
        if offset + values.len() > data.output_data.len() {
            return Err("Write beyond output buffer".to_string());
        }
        
        data.output_data[offset..offset + values.len()].copy_from_slice(values);
        Ok(())
    }
    
    fn get_slave_status(&self, slave_address: u8) -> Option<u16> {
        self.slave_data.get(&slave_address).map(|d| d.status)
    }
}

impl fmt::Display for ControlLogixProfibusScanner {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Scanner: {} (Slot {})\n", self.module_name, self.slot_number)?;
        write!(f, "Status: {}\n", if self.scanner_running { "Running" } else { "Stopped" })?;
        write!(f, "Slaves: {}\n", self.slaves.len())?;
        write!(f, "Cycle Time: {} µs", self.scan_cycle_time_us)
    }
}

// ControlLogix tag mapping
#[derive(Debug)]
struct ControlLogixTag {
    name: String,
    slave_address: u8,
    offset: usize,
    is_input: bool,
}

impl ControlLogixTag {
    fn new(name: &str, slave_address: u8, offset: usize, is_input: bool) -> Self {
        Self {
            name: name.to_string(),
            slave_address,
            offset,
            is_input,
        }
    }
}

// Application example
fn main() {
    println!("=== Allen Bradley ControlLogix Profibus Integration (Rust) ===\n");
    
    // Create scanner module
    let mut scanner = ControlLogixProfibusScanner::new("1756-PB75", 2);
    
    // Configure Profibus slaves
    scanner.add_slave(3, 4, 0, "TEMP_SENSOR.GSD").unwrap();
    scanner.add_slave(5, 2, 1, "VALVE_CTRL.GSD").unwrap();
    scanner.add_slave(10, 8, 4, "VFD.GSD").unwrap();
    
    // Start scanner
    scanner.start().unwrap();
    
    // Create ControlLogix tags
    let temp_tag = ControlLogixTag::new("Temperature_PV", 3, 0, true);
    let valve_tag = ControlLogixTag::new("Valve_Cmd", 5, 0, false);
    let vfd_speed_tag = ControlLogixTag::new("VFD_Speed_SP", 10, 0, false);
    
    println!("\n=== Simulating Control Cycles ===");
    
    // Run control loop
    for cycle in 1..=5 {
        println!("\nCycle {}:", cycle);
        
        // Execute Profibus cycle
        scanner.execute_cycle().unwrap();
        
        // Read temperature
        let temp_data = scanner.read_input(temp_tag.slave_address, 
                                           temp_tag.offset, 1).unwrap();
        let temperature = temp_data[0];
        println!("  {}: {}°C", temp_tag.name, temperature);
        
        // Control logic
        let valve_state = if temperature > 50 { 0x01 } else { 0x00 };
        scanner.write_output(valve_tag.slave_address, 
                           valve_tag.offset, &[valve_state]).unwrap();
        println!("  {}: {}", valve_tag.name, 
                if valve_state == 0x01 { "OPEN" } else { "CLOSED" });
        
        // VFD speed control
        let speed_setpoint = if temperature > 30 { 75 } else { 50 };
        scanner.write_output(vfd_speed_tag.slave_address, 
                           vfd_speed_tag.offset, &[speed_setpoint]).unwrap();
        println!("  {}: {}%", vfd_speed_tag.name, speed_setpoint);
        
        // Check slave status
        for addr in [3, 5, 10] {
            if let Some(status) = scanner.get_slave_status(addr) {
                println!("  Slave {} status: 0x{:04X}", addr, status);
            }
        }
    }
    
    println!("\n=== Scanner Information ===");
    println!("{}", scanner);
    
    scanner.stop();
}
```

## Summary

**Allen Bradley ControlLogix Profibus Integration** enables seamless communication between Rockwell Automation's ControlLogix PLC platform and Profibus DP fieldbus devices using dedicated scanner modules (1756-PB75/PB72). This integration is crucial for:

- **Legacy System Integration**: Connecting existing Profibus infrastructure to modern ControlLogix systems
- **Multi-Vendor Automation**: Combining devices from different manufacturers in a unified control architecture
- **Cost-Effective Upgrades**: Preserving Profibus investments while migrating to ControlLogix platforms

**Key Technical Points:**
- Scanner modules act as Profibus DP masters, managing up to 125 slaves
- Data mapping occurs between Profibus I/O and ControlLogix tags in the controller's memory
- Configuration uses RSNetWorx for Profibus and Studio 5000 (RSLogix 5000)
- Supports standard GSD files for device configuration
- Provides deterministic, cyclic data exchange with diagnostic capabilities

**Programming Considerations:**
- Access Profibus data through standard ControlLogix tag structures
- Implement proper error handling for communication faults
- Consider data type conversions between Profibus byte arrays and ControlLogix data types
- Use structured programming (Function Blocks, Add-On Instructions) for modular Profibus device handling

This integration solution is widely deployed in industries requiring mixed automation architectures, particularly in process control, automotive manufacturing, and pharmaceutical production where both legacy Profibus equipment and modern ControlLogix systems coexist.