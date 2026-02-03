# OPC Server Integration for Profibus

## Detailed Description

OPC (OLE for Process Control) Server Integration is a critical middleware layer that bridges Profibus industrial networks with modern SCADA (Supervisory Control and Data Acquisition) systems, HMIs, and enterprise applications. This integration exposes Profibus field device data through standardized OPC interfaces, enabling seamless data exchange between industrial automation layers.

### Key Concepts

**OPC DA (Data Access)** is the traditional COM/DCOM-based protocol for real-time data access, while **OPC UA (Unified Architecture)** is the modern, platform-independent successor offering enhanced security, cross-platform compatibility, and rich information modeling.

The integration architecture typically consists of:

1. **Profibus Master/Gateway** - Communicates directly with Profibus DP slaves
2. **OPC Server** - Acts as middleware, translating Profibus data to OPC format
3. **OPC Client(s)** - SCADA systems, historians, or applications consuming the data

**Data Mapping** involves associating Profibus device addresses, diagnostic data, and process values with OPC item identifiers (DA) or nodes (UA). This includes handling cyclic data exchange, acyclic parameter access, and alarm/event propagation.

### Architecture Components

- **Device Configuration**: GSD file parsing to understand device capabilities and data structures
- **Cyclic Data Handling**: Continuous polling of input/output modules with configurable update rates
- **Acyclic Services**: On-demand parameter read/write operations
- **Diagnostics Integration**: Station status, extended diagnostics, and alarm propagation
- **Quality & Timestamp**: OPC quality codes mapped from Profibus communication status

## C/C++ Implementation

```c
// profibus_opc_server.h
#ifndef PROFIBUS_OPC_SERVER_H
#define PROFIBUS_OPC_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// Profibus data structures
typedef struct {
    uint8_t station_address;
    uint8_t *input_data;
    uint8_t *output_data;
    uint16_t input_length;
    uint16_t output_length;
    bool is_online;
    uint32_t last_update_ms;
} profibus_slave_t;

typedef struct {
    char item_id[256];
    uint8_t station_address;
    uint16_t byte_offset;
    uint8_t bit_offset;
    uint8_t data_type;  // 0=BOOL, 1=BYTE, 2=WORD, 3=DWORD, 4=REAL
    void *value_ptr;
    uint16_t quality;   // OPC quality code
    uint64_t timestamp;
} opc_item_t;

// OPC DA quality codes
#define OPC_QUALITY_GOOD            0xC0
#define OPC_QUALITY_BAD             0x00
#define OPC_QUALITY_UNCERTAIN       0x40
#define OPC_QUALITY_COMM_FAILURE    0x18

// Function prototypes
int profibus_init_master(const char *device_path);
int profibus_add_slave(uint8_t address, uint16_t input_len, uint16_t output_len);
int profibus_cyclic_exchange(void);
int opc_register_item(const char *item_id, uint8_t station, 
                      uint16_t offset, uint8_t bit, uint8_t type);
int opc_read_item(const char *item_id, void *value, 
                  uint16_t *quality, uint64_t *timestamp);
int opc_write_item(const char *item_id, void *value);

#endif
```

```c
// profibus_opc_server.c
#include "profibus_opc_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_SLAVES 126
#define MAX_OPC_ITEMS 1000

static profibus_slave_t slaves[MAX_SLAVES];
static opc_item_t opc_items[MAX_OPC_ITEMS];
static int slave_count = 0;
static int item_count = 0;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static int master_fd = -1;

// Simulated Profibus master initialization
int profibus_init_master(const char *device_path) {
    printf("Initializing Profibus master on %s\n", device_path);
    // In real implementation: open device, configure baudrate, etc.
    master_fd = 1; // Simulated file descriptor
    return 0;
}

// Add a Profibus slave device
int profibus_add_slave(uint8_t address, uint16_t input_len, uint16_t output_len) {
    if (slave_count >= MAX_SLAVES || address > 126) {
        return -1;
    }
    
    profibus_slave_t *slave = &slaves[slave_count];
    slave->station_address = address;
    slave->input_length = input_len;
    slave->output_length = output_len;
    slave->input_data = (uint8_t *)calloc(input_len, 1);
    slave->output_data = (uint8_t *)calloc(output_len, 1);
    slave->is_online = false;
    
    printf("Added slave %d: IN=%d bytes, OUT=%d bytes\n", 
           address, input_len, output_len);
    
    slave_count++;
    return 0;
}

// Cyclic data exchange with all configured slaves
int profibus_cyclic_exchange(void) {
    pthread_mutex_lock(&data_mutex);
    
    for (int i = 0; i < slave_count; i++) {
        profibus_slave_t *slave = &slaves[i];
        
        // Simulate Profibus DP cyclic exchange
        // In real implementation: FDL_DATA_EXCHANGE telegram
        
        // Simulated data update (increment values for demo)
        for (int j = 0; j < slave->input_length; j++) {
            slave->input_data[j]++;
        }
        
        slave->is_online = true;
        slave->last_update_ms = (uint32_t)(time(NULL) * 1000);
    }
    
    pthread_mutex_unlock(&data_mutex);
    return 0;
}

// Register an OPC item mapped to Profibus data
int opc_register_item(const char *item_id, uint8_t station, 
                      uint16_t offset, uint8_t bit, uint8_t type) {
    if (item_count >= MAX_OPC_ITEMS) {
        return -1;
    }
    
    opc_item_t *item = &opc_items[item_count];
    strncpy(item->item_id, item_id, sizeof(item->item_id) - 1);
    item->station_address = station;
    item->byte_offset = offset;
    item->bit_offset = bit;
    item->data_type = type;
    item->quality = OPC_QUALITY_BAD;
    item->timestamp = 0;
    
    printf("Registered OPC item: %s -> Station %d, Offset %d.%d\n",
           item_id, station, offset, bit);
    
    item_count++;
    return 0;
}

// Read OPC item value from Profibus data
int opc_read_item(const char *item_id, void *value, 
                  uint16_t *quality, uint64_t *timestamp) {
    pthread_mutex_lock(&data_mutex);
    
    // Find the OPC item
    opc_item_t *item = NULL;
    for (int i = 0; i < item_count; i++) {
        if (strcmp(opc_items[i].item_id, item_id) == 0) {
            item = &opc_items[i];
            break;
        }
    }
    
    if (!item) {
        pthread_mutex_unlock(&data_mutex);
        return -1;
    }
    
    // Find the corresponding slave
    profibus_slave_t *slave = NULL;
    for (int i = 0; i < slave_count; i++) {
        if (slaves[i].station_address == item->station_address) {
            slave = &slaves[i];
            break;
        }
    }
    
    if (!slave || !slave->is_online) {
        *quality = OPC_QUALITY_BAD;
        pthread_mutex_unlock(&data_mutex);
        return -1;
    }
    
    // Extract value based on data type
    uint8_t *data_ptr = &slave->input_data[item->byte_offset];
    
    switch (item->data_type) {
        case 0: // BOOL
            *(bool *)value = (*data_ptr >> item->bit_offset) & 0x01;
            break;
        case 1: // BYTE
            *(uint8_t *)value = *data_ptr;
            break;
        case 2: // WORD
            *(uint16_t *)value = *(uint16_t *)data_ptr;
            break;
        case 3: // DWORD
            *(uint32_t *)value = *(uint32_t *)data_ptr;
            break;
        case 4: // REAL (IEEE 754 float)
            *(float *)value = *(float *)data_ptr;
            break;
        default:
            pthread_mutex_unlock(&data_mutex);
            return -1;
    }
    
    *quality = slave->is_online ? OPC_QUALITY_GOOD : OPC_QUALITY_COMM_FAILURE;
    *timestamp = slave->last_update_ms;
    
    pthread_mutex_unlock(&data_mutex);
    return 0;
}

// Write OPC item value to Profibus output data
int opc_write_item(const char *item_id, void *value) {
    pthread_mutex_lock(&data_mutex);
    
    opc_item_t *item = NULL;
    for (int i = 0; i < item_count; i++) {
        if (strcmp(opc_items[i].item_id, item_id) == 0) {
            item = &opc_items[i];
            break;
        }
    }
    
    if (!item) {
        pthread_mutex_unlock(&data_mutex);
        return -1;
    }
    
    profibus_slave_t *slave = NULL;
    for (int i = 0; i < slave_count; i++) {
        if (slaves[i].station_address == item->station_address) {
            slave = &slaves[i];
            break;
        }
    }
    
    if (!slave || !slave->is_online) {
        pthread_mutex_unlock(&data_mutex);
        return -1;
    }
    
    // Write value to output buffer
    uint8_t *data_ptr = &slave->output_data[item->byte_offset];
    
    switch (item->data_type) {
        case 0: // BOOL
            if (*(bool *)value) {
                *data_ptr |= (1 << item->bit_offset);
            } else {
                *data_ptr &= ~(1 << item->bit_offset);
            }
            break;
        case 1: // BYTE
            *data_ptr = *(uint8_t *)value;
            break;
        case 2: // WORD
            *(uint16_t *)data_ptr = *(uint16_t *)value;
            break;
        case 3: // DWORD
            *(uint32_t *)data_ptr = *(uint32_t *)value;
            break;
        case 4: // REAL
            *(float *)data_ptr = *(float *)value;
            break;
        default:
            pthread_mutex_unlock(&data_mutex);
            return -1;
    }
    
    pthread_mutex_unlock(&data_mutex);
    return 0;
}

// Example usage
int main() {
    // Initialize Profibus master
    profibus_init_master("/dev/profibus0");
    
    // Configure slaves
    profibus_add_slave(3, 8, 4);   // Station 3: 8 bytes IN, 4 bytes OUT
    profibus_add_slave(5, 16, 8);  // Station 5: 16 bytes IN, 8 bytes OUT
    
    // Register OPC items
    opc_register_item("Device.Station3.DigitalInput1", 3, 0, 0, 0);  // BOOL
    opc_register_item("Device.Station3.AnalogInput1", 3, 2, 0, 2);   // WORD
    opc_register_item("Device.Station5.Temperature", 5, 0, 0, 4);    // REAL
    
    // Simulate cyclic operation
    for (int cycle = 0; cycle < 5; cycle++) {
        printf("\n--- Cycle %d ---\n", cycle);
        
        // Exchange data with Profibus
        profibus_cyclic_exchange();
        
        // Read OPC items
        bool digital_val;
        uint16_t quality;
        uint64_t timestamp;
        
        if (opc_read_item("Device.Station3.DigitalInput1", 
                         &digital_val, &quality, &timestamp) == 0) {
            printf("DigitalInput1: %d (Quality: 0x%02X)\n", 
                   digital_val, quality);
        }
        
        uint16_t analog_val;
        if (opc_read_item("Device.Station3.AnalogInput1", 
                         &analog_val, &quality, &timestamp) == 0) {
            printf("AnalogInput1: %u (Quality: 0x%02X)\n", 
                   analog_val, quality);
        }
        
        float temp_val;
        if (opc_read_item("Device.Station5.Temperature", 
                         &temp_val, &quality, &timestamp) == 0) {
            printf("Temperature: %.2f°C (Quality: 0x%02X)\n", 
                   temp_val, quality);
        }
        
        // Write example
        bool output_cmd = (cycle % 2 == 0);
        opc_write_item("Device.Station3.DigitalInput1", &output_cmd);
        
        usleep(1000000); // 1 second delay
    }
    
    return 0;
}
```

## Rust Implementation

```rust
// Cargo.toml dependencies:
// [dependencies]
// chrono = "0.4"
// thiserror = "1.0"

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum ProfibusOpcError {
    #[error("Slave not found: {0}")]
    SlaveNotFound(u8),
    #[error("Item not found: {0}")]
    ItemNotFound(String),
    #[error("Invalid data type")]
    InvalidDataType,
    #[error("Communication failure")]
    CommunicationFailure,
    #[error("Buffer overflow")]
    BufferOverflow,
}

// OPC quality codes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OpcQuality {
    Good = 0xC0,
    Bad = 0x00,
    Uncertain = 0x40,
    CommFailure = 0x18,
}

// Profibus data types
#[derive(Debug, Clone, Copy)]
pub enum DataType {
    Bool,
    Byte,
    Word,
    DWord,
    Real,
}

// Value variant for different data types
#[derive(Debug, Clone)]
pub enum OpcValue {
    Bool(bool),
    Byte(u8),
    Word(u16),
    DWord(u32),
    Real(f32),
}

// Profibus slave device
#[derive(Debug)]
pub struct ProfibusDevice {
    station_address: u8,
    input_data: Vec<u8>,
    output_data: Vec<u8>,
    is_online: bool,
    last_update: SystemTime,
}

impl ProfibusDevice {
    pub fn new(address: u8, input_len: usize, output_len: usize) -> Self {
        Self {
            station_address: address,
            input_data: vec![0; input_len],
            output_data: vec![0; output_len],
            is_online: false,
            last_update: SystemTime::now(),
        }
    }

    pub fn read_bool(&self, byte_offset: usize, bit_offset: u8) -> Result<bool, ProfibusOpcError> {
        if byte_offset >= self.input_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        Ok((self.input_data[byte_offset] >> bit_offset) & 0x01 != 0)
    }

    pub fn read_byte(&self, byte_offset: usize) -> Result<u8, ProfibusOpcError> {
        if byte_offset >= self.input_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        Ok(self.input_data[byte_offset])
    }

    pub fn read_word(&self, byte_offset: usize) -> Result<u16, ProfibusOpcError> {
        if byte_offset + 1 >= self.input_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        Ok(u16::from_le_bytes([
            self.input_data[byte_offset],
            self.input_data[byte_offset + 1],
        ]))
    }

    pub fn read_dword(&self, byte_offset: usize) -> Result<u32, ProfibusOpcError> {
        if byte_offset + 3 >= self.input_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        Ok(u32::from_le_bytes([
            self.input_data[byte_offset],
            self.input_data[byte_offset + 1],
            self.input_data[byte_offset + 2],
            self.input_data[byte_offset + 3],
        ]))
    }

    pub fn read_real(&self, byte_offset: usize) -> Result<f32, ProfibusOpcError> {
        let dword = self.read_dword(byte_offset)?;
        Ok(f32::from_bits(dword))
    }

    pub fn write_bool(
        &mut self,
        byte_offset: usize,
        bit_offset: u8,
        value: bool,
    ) -> Result<(), ProfibusOpcError> {
        if byte_offset >= self.output_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        if value {
            self.output_data[byte_offset] |= 1 << bit_offset;
        } else {
            self.output_data[byte_offset] &= !(1 << bit_offset);
        }
        Ok(())
    }

    pub fn write_word(&mut self, byte_offset: usize, value: u16) -> Result<(), ProfibusOpcError> {
        if byte_offset + 1 >= self.output_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        let bytes = value.to_le_bytes();
        self.output_data[byte_offset] = bytes[0];
        self.output_data[byte_offset + 1] = bytes[1];
        Ok(())
    }

    pub fn write_real(&mut self, byte_offset: usize, value: f32) -> Result<(), ProfibusOpcError> {
        self.write_dword(byte_offset, value.to_bits())
    }

    pub fn write_dword(&mut self, byte_offset: usize, value: u32) -> Result<(), ProfibusOpcError> {
        if byte_offset + 3 >= self.output_data.len() {
            return Err(ProfibusOpcError::BufferOverflow);
        }
        let bytes = value.to_le_bytes();
        for (i, &byte) in bytes.iter().enumerate() {
            self.output_data[byte_offset + i] = byte;
        }
        Ok(())
    }
}

// OPC item mapping
#[derive(Debug, Clone)]
pub struct OpcItem {
    item_id: String,
    station_address: u8,
    byte_offset: usize,
    bit_offset: u8,
    data_type: DataType,
}

// OPC Server for Profibus integration
pub struct ProfibusOpcServer {
    devices: Arc<Mutex<HashMap<u8, ProfibusDevice>>>,
    items: Arc<Mutex<HashMap<String, OpcItem>>>,
}

impl ProfibusOpcServer {
    pub fn new() -> Self {
        Self {
            devices: Arc::new(Mutex::new(HashMap::new())),
            items: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    pub fn add_device(&self, address: u8, input_len: usize, output_len: usize) {
        let device = ProfibusDevice::new(address, input_len, output_len);
        let mut devices = self.devices.lock().unwrap();
        devices.insert(address, device);
        println!(
            "Added Profibus device: Station {}, IN={} bytes, OUT={} bytes",
            address, input_len, output_len
        );
    }

    pub fn register_item(
        &self,
        item_id: &str,
        station: u8,
        byte_offset: usize,
        bit_offset: u8,
        data_type: DataType,
    ) {
        let item = OpcItem {
            item_id: item_id.to_string(),
            station_address: station,
            byte_offset,
            bit_offset,
            data_type,
        };
        let mut items = self.items.lock().unwrap();
        items.insert(item_id.to_string(), item);
        println!(
            "Registered OPC item: {} -> Station {}, Offset {}.{}",
            item_id, station, byte_offset, bit_offset
        );
    }

    // Simulate cyclic data exchange
    pub fn cyclic_exchange(&self) -> Result<(), ProfibusOpcError> {
        let mut devices = self.devices.lock().unwrap();
        
        for (_, device) in devices.iter_mut() {
            // Simulate data update (increment for demo)
            for byte in device.input_data.iter_mut() {
                *byte = byte.wrapping_add(1);
            }
            
            device.is_online = true;
            device.last_update = SystemTime::now();
        }
        
        Ok(())
    }

    pub fn read_item(&self, item_id: &str) -> Result<(OpcValue, OpcQuality, u64), ProfibusOpcError> {
        let items = self.items.lock().unwrap();
        let item = items
            .get(item_id)
            .ok_or_else(|| ProfibusOpcError::ItemNotFound(item_id.to_string()))?;

        let devices = self.devices.lock().unwrap();
        let device = devices
            .get(&item.station_address)
            .ok_or(ProfibusOpcError::SlaveNotFound(item.station_address))?;

        if !device.is_online {
            return Ok((
                OpcValue::Bool(false),
                OpcQuality::CommFailure,
                0,
            ));
        }

        let value = match item.data_type {
            DataType::Bool => {
                OpcValue::Bool(device.read_bool(item.byte_offset, item.bit_offset)?)
            }
            DataType::Byte => OpcValue::Byte(device.read_byte(item.byte_offset)?),
            DataType::Word => OpcValue::Word(device.read_word(item.byte_offset)?),
            DataType::DWord => OpcValue::DWord(device.read_dword(item.byte_offset)?),
            DataType::Real => OpcValue::Real(device.read_real(item.byte_offset)?),
        };

        let timestamp = device
            .last_update
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_millis() as u64;

        Ok((value, OpcQuality::Good, timestamp))
    }

    pub fn write_item(&self, item_id: &str, value: OpcValue) -> Result<(), ProfibusOpcError> {
        let items = self.items.lock().unwrap();
        let item = items
            .get(item_id)
            .ok_or_else(|| ProfibusOpcError::ItemNotFound(item_id.to_string()))?
            .clone();
        drop(items);

        let mut devices = self.devices.lock().unwrap();
        let device = devices
            .get_mut(&item.station_address)
            .ok_or(ProfibusOpcError::SlaveNotFound(item.station_address))?;

        if !device.is_online {
            return Err(ProfibusOpcError::CommunicationFailure);
        }

        match (item.data_type, value) {
            (DataType::Bool, OpcValue::Bool(v)) => {
                device.write_bool(item.byte_offset, item.bit_offset, v)?
            }
            (DataType::Word, OpcValue::Word(v)) => device.write_word(item.byte_offset, v)?,
            (DataType::Real, OpcValue::Real(v)) => device.write_real(item.byte_offset, v)?,
            _ => return Err(ProfibusOpcError::InvalidDataType),
        }

        Ok(())
    }
}

// Example usage
fn main() -> Result<(), ProfibusOpcError> {
    let server = ProfibusOpcServer::new();

    // Configure devices
    server.add_device(3, 8, 4);
    server.add_device(5, 16, 8);

    // Register OPC items
    server.register_item("Device.Station3.DigitalInput1", 3, 0, 0, DataType::Bool);
    server.register_item("Device.Station3.AnalogInput1", 3, 2, 0, DataType::Word);
    server.register_item("Device.Station5.Temperature", 5, 0, 0, DataType::Real);

    // Simulate cyclic operation
    for cycle in 0..5 {
        println!("\n--- Cycle {} ---", cycle);

        // Exchange data
        server.cyclic_exchange()?;

        // Read items
        match server.read_item("Device.Station3.DigitalInput1") {
            Ok((value, quality, _timestamp)) => {
                println!("DigitalInput1: {:?} (Quality: {:?})", value, quality);
            }
            Err(e) => println!("Error reading item: {}", e),
        }

        match server.read_item("Device.Station3.AnalogInput1") {
            Ok((value, quality, _timestamp)) => {
                println!("AnalogInput1: {:?} (Quality: {:?})", value, quality);
            }
            Err(e) => println!("Error reading item: {}", e),
        }

        match server.read_item("Device.Station5.Temperature") {
            Ok((value, quality, _timestamp)) => {
                println!("Temperature: {:?} (Quality: {:?})", value, quality);
            }
            Err(e) => println!("Error reading item: {}", e),
        }

        // Write example
        let output_value = OpcValue::Bool(cycle % 2 == 0);
        server.write_item("Device.Station3.DigitalInput1", output_value)?;

        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    Ok(())
}
```

## Summary

**OPC Server Integration** for Profibus creates a standardized data bridge between industrial Profibus networks and enterprise systems. The key aspects include:

- **Data Mapping**: Translating Profibus device addresses and data structures to OPC item identifiers with proper type handling (boolean, analog, floating-point)
- **Quality & Timestamps**: Propagating communication status as OPC quality codes and maintaining accurate timestamps for all data points
- **Bidirectional Communication**: Supporting both read operations (process inputs, diagnostics) and write operations (setpoints, commands)
- **Real-time Performance**: Maintaining cyclic data exchange with configurable update rates while serving multiple OPC clients simultaneously
- **Protocol Translation**: Bridging between Profibus DP cyclic/acyclic services and OPC DA (COM-based) or OPC UA (service-oriented) architectures

The code examples demonstrate core functionality for device management, item registration, cyclic data exchange, and read/write operations with proper error handling and thread safety—essential foundations for production OPC server implementations.