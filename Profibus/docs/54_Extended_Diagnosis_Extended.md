# Extended Diagnosis Extended (Profibus)

## Detailed Description

**Extended Diagnosis Extended** refers to advanced diagnostic capabilities in Profibus networks that go beyond the standard 6-byte diagnostic structure defined in the base Profibus specification. This feature enables field devices (slaves) to provide much more detailed information about their operational status, errors, and internal conditions to the master system.

### Key Concepts

**Standard vs. Extended Diagnostics:**
- **Standard Diagnosis**: Limited to 6 bytes containing basic status information (station status, master address, identifiers)
- **Extended Diagnosis**: Can include up to 244 bytes of additional diagnostic data
- **Extended Diagnosis Extended**: Provides even more sophisticated diagnostic structures with device-specific and manufacturer-specific information

### Diagnostic Data Structure

The extended diagnostic data follows a hierarchical structure:

1. **Header Information** (6 bytes - standard diagnosis)
   - Station status bytes
   - Master address
   - Manufacturer ID and ident number

2. **Extended Diagnostic Data** (variable length)
   - Device-related diagnosis
   - Channel-specific diagnosis
   - Identifier-related diagnosis
   - Manufacturer-specific diagnosis

### Diagnostic Types

**Device-Related Diagnosis:**
- General device errors
- Hardware failures
- Configuration issues
- Temperature warnings

**Channel-Specific Diagnosis:**
- Individual I/O channel errors
- Wire break detection
- Short circuit detection
- Out-of-range values

**Identifier-Related Diagnosis:**
- Specific error codes with detailed descriptions
- Multiple simultaneous error reporting
- Prioritized alarm structures

## Programming Implementation

### C/C++ Implementation

```c
// profibus_extended_diag.h
#ifndef PROFIBUS_EXTENDED_DIAG_H
#define PROFIBUS_EXTENDED_DIAG_H

#include <stdint.h>
#include <stdbool.h>

// Maximum diagnosis data size
#define MAX_DIAG_DATA_SIZE 244
#define STD_DIAG_SIZE 6

// Diagnostic status flags
#define DIAG_STATUS_ACTIVE 0x01
#define DIAG_STATUS_OVERFLOW 0x02
#define DIAG_STATUS_EXT_DIAG 0x04

// Diagnostic types
typedef enum {
    DIAG_TYPE_DEVICE = 0x00,
    DIAG_TYPE_CHANNEL = 0x01,
    DIAG_TYPE_IDENTIFIER = 0x02,
    DIAG_TYPE_MANUFACTURER = 0x03
} DiagnosticType;

// Channel error types
typedef enum {
    CHANNEL_ERROR_NONE = 0x00,
    CHANNEL_ERROR_SHORT_CIRCUIT = 0x01,
    CHANNEL_ERROR_WIRE_BREAK = 0x02,
    CHANNEL_ERROR_OVER_RANGE = 0x03,
    CHANNEL_ERROR_UNDER_RANGE = 0x04,
    CHANNEL_ERROR_OVERLOAD = 0x05,
    CHANNEL_ERROR_SENSOR_FAULT = 0x06
} ChannelErrorType;

// Standard diagnostic structure (6 bytes)
typedef struct {
    uint8_t station_status_1;
    uint8_t station_status_2;
    uint8_t station_status_3;
    uint8_t master_address;
    uint16_t ident_number;
} __attribute__((packed)) StandardDiagnosis;

// Extended diagnostic header
typedef struct {
    uint8_t diagnostic_type;
    uint8_t length;
} __attribute__((packed)) ExtDiagHeader;

// Channel-specific diagnostic entry
typedef struct {
    ExtDiagHeader header;
    uint8_t channel_number;
    uint8_t error_type;
    uint16_t additional_info;
} __attribute__((packed)) ChannelDiagnosis;

// Device diagnostic entry
typedef struct {
    ExtDiagHeader header;
    uint8_t device_status;
    uint8_t error_code;
    uint32_t error_count;
} __attribute__((packed)) DeviceDiagnosis;

// Identifier diagnostic entry
typedef struct {
    ExtDiagHeader header;
    uint16_t identifier;
    uint8_t qualifier;
    uint8_t channel_number;
} __attribute__((packed)) IdentifierDiagnosis;

// Complete diagnostic data structure
typedef struct {
    StandardDiagnosis std_diag;
    uint8_t ext_diag_data[MAX_DIAG_DATA_SIZE];
    uint16_t ext_diag_length;
} ProfibusDiagnostics;

// Function prototypes
bool profibus_add_channel_diag(ProfibusDiagnostics *diag, 
                               uint8_t channel, 
                               ChannelErrorType error,
                               uint16_t additional_info);

bool profibus_add_device_diag(ProfibusDiagnostics *diag,
                              uint8_t device_status,
                              uint8_t error_code,
                              uint32_t error_count);

bool profibus_add_identifier_diag(ProfibusDiagnostics *diag,
                                  uint16_t identifier,
                                  uint8_t qualifier,
                                  uint8_t channel);

int profibus_parse_extended_diag(const uint8_t *data, 
                                 uint16_t length,
                                 ProfibusDiagnostics *diag);

void profibus_clear_diagnostics(ProfibusDiagnostics *diag);

bool profibus_has_diagnostics(const ProfibusDiagnostics *diag);

#endif // PROFIBUS_EXTENDED_DIAG_H
```

```c
// profibus_extended_diag.c
#include "profibus_extended_diag.h"
#include <string.h>
#include <stdio.h>

// Initialize/clear diagnostic structure
void profibus_clear_diagnostics(ProfibusDiagnostics *diag) {
    memset(diag, 0, sizeof(ProfibusDiagnostics));
}

// Check if diagnostics are active
bool profibus_has_diagnostics(const ProfibusDiagnostics *diag) {
    return (diag->std_diag.station_status_1 & DIAG_STATUS_ACTIVE) != 0;
}

// Add channel-specific diagnostic
bool profibus_add_channel_diag(ProfibusDiagnostics *diag, 
                               uint8_t channel, 
                               ChannelErrorType error,
                               uint16_t additional_info) {
    
    ChannelDiagnosis ch_diag;
    
    // Check if there's enough space
    if (diag->ext_diag_length + sizeof(ChannelDiagnosis) > MAX_DIAG_DATA_SIZE) {
        diag->std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
        return false;
    }
    
    // Build channel diagnostic entry
    ch_diag.header.diagnostic_type = DIAG_TYPE_CHANNEL;
    ch_diag.header.length = sizeof(ChannelDiagnosis) - sizeof(ExtDiagHeader);
    ch_diag.channel_number = channel;
    ch_diag.error_type = error;
    ch_diag.additional_info = additional_info;
    
    // Copy to extended diagnostic buffer
    memcpy(&diag->ext_diag_data[diag->ext_diag_length], 
           &ch_diag, 
           sizeof(ChannelDiagnosis));
    
    diag->ext_diag_length += sizeof(ChannelDiagnosis);
    
    // Set diagnostic active flags
    diag->std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
    
    return true;
}

// Add device-level diagnostic
bool profibus_add_device_diag(ProfibusDiagnostics *diag,
                              uint8_t device_status,
                              uint8_t error_code,
                              uint32_t error_count) {
    
    DeviceDiagnosis dev_diag;
    
    if (diag->ext_diag_length + sizeof(DeviceDiagnosis) > MAX_DIAG_DATA_SIZE) {
        diag->std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
        return false;
    }
    
    dev_diag.header.diagnostic_type = DIAG_TYPE_DEVICE;
    dev_diag.header.length = sizeof(DeviceDiagnosis) - sizeof(ExtDiagHeader);
    dev_diag.device_status = device_status;
    dev_diag.error_code = error_code;
    dev_diag.error_count = error_count;
    
    memcpy(&diag->ext_diag_data[diag->ext_diag_length], 
           &dev_diag, 
           sizeof(DeviceDiagnosis));
    
    diag->ext_diag_length += sizeof(DeviceDiagnosis);
    diag->std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
    
    return true;
}

// Add identifier-based diagnostic
bool profibus_add_identifier_diag(ProfibusDiagnostics *diag,
                                  uint16_t identifier,
                                  uint8_t qualifier,
                                  uint8_t channel) {
    
    IdentifierDiagnosis id_diag;
    
    if (diag->ext_diag_length + sizeof(IdentifierDiagnosis) > MAX_DIAG_DATA_SIZE) {
        diag->std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
        return false;
    }
    
    id_diag.header.diagnostic_type = DIAG_TYPE_IDENTIFIER;
    id_diag.header.length = sizeof(IdentifierDiagnosis) - sizeof(ExtDiagHeader);
    id_diag.identifier = identifier;
    id_diag.qualifier = qualifier;
    id_diag.channel_number = channel;
    
    memcpy(&diag->ext_diag_data[diag->ext_diag_length], 
           &id_diag, 
           sizeof(IdentifierDiagnosis));
    
    diag->ext_diag_length += sizeof(IdentifierDiagnosis);
    diag->std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
    
    return true;
}

// Parse extended diagnostic data
int profibus_parse_extended_diag(const uint8_t *data, 
                                 uint16_t length,
                                 ProfibusDiagnostics *diag) {
    
    uint16_t offset = 0;
    int entry_count = 0;
    
    // Clear existing diagnostics
    profibus_clear_diagnostics(diag);
    
    // Parse standard diagnosis (first 6 bytes)
    if (length < STD_DIAG_SIZE) {
        return -1;
    }
    
    memcpy(&diag->std_diag, data, STD_DIAG_SIZE);
    offset = STD_DIAG_SIZE;
    
    // Parse extended diagnosis if present
    if (diag->std_diag.station_status_1 & DIAG_STATUS_EXT_DIAG) {
        while (offset < length && offset < STD_DIAG_SIZE + MAX_DIAG_DATA_SIZE) {
            ExtDiagHeader *header = (ExtDiagHeader *)&data[offset];
            
            if (offset + sizeof(ExtDiagHeader) + header->length > length) {
                break;
            }
            
            uint16_t entry_size = sizeof(ExtDiagHeader) + header->length;
            memcpy(&diag->ext_diag_data[diag->ext_diag_length], 
                   &data[offset], 
                   entry_size);
            
            diag->ext_diag_length += entry_size;
            offset += entry_size;
            entry_count++;
        }
    }
    
    return entry_count;
}

// Example usage and diagnostic reporting
void profibus_print_diagnostics(const ProfibusDiagnostics *diag) {
    printf("=== Profibus Diagnostics ===\n");
    printf("Station Status 1: 0x%02X\n", diag->std_diag.station_status_1);
    printf("Diagnostic Active: %s\n", 
           (diag->std_diag.station_status_1 & DIAG_STATUS_ACTIVE) ? "YES" : "NO");
    printf("Extended Diag: %s\n",
           (diag->std_diag.station_status_1 & DIAG_STATUS_EXT_DIAG) ? "YES" : "NO");
    
    if (diag->ext_diag_length > 0) {
        printf("\nExtended Diagnostic Data (%u bytes):\n", diag->ext_diag_length);
        
        uint16_t offset = 0;
        while (offset < diag->ext_diag_length) {
            ExtDiagHeader *header = (ExtDiagHeader *)&diag->ext_diag_data[offset];
            
            printf("  Type: ");
            switch (header->diagnostic_type) {
                case DIAG_TYPE_DEVICE:
                    printf("Device Diagnosis\n");
                    DeviceDiagnosis *dev = (DeviceDiagnosis *)&diag->ext_diag_data[offset];
                    printf("    Status: 0x%02X, Error Code: 0x%02X, Count: %u\n",
                           dev->device_status, dev->error_code, dev->error_count);
                    break;
                    
                case DIAG_TYPE_CHANNEL:
                    printf("Channel Diagnosis\n");
                    ChannelDiagnosis *ch = (ChannelDiagnosis *)&diag->ext_diag_data[offset];
                    printf("    Channel: %u, Error: %u, Info: 0x%04X\n",
                           ch->channel_number, ch->error_type, ch->additional_info);
                    break;
                    
                case DIAG_TYPE_IDENTIFIER:
                    printf("Identifier Diagnosis\n");
                    IdentifierDiagnosis *id = (IdentifierDiagnosis *)&diag->ext_diag_data[offset];
                    printf("    ID: 0x%04X, Qualifier: 0x%02X, Channel: %u\n",
                           id->identifier, id->qualifier, id->channel_number);
                    break;
                    
                default:
                    printf("Unknown (0x%02X)\n", header->diagnostic_type);
                    break;
            }
            
            offset += sizeof(ExtDiagHeader) + header->length;
        }
    }
}
```

```cpp
// Example usage in C++
#include "profibus_extended_diag.h"
#include <iostream>
#include <memory>

class ProfibusDevice {
private:
    ProfibusDiagnostics diagnostics;
    
public:
    ProfibusDevice() {
        profibus_clear_diagnostics(&diagnostics);
        // Set device identifier
        diagnostics.std_diag.ident_number = 0x1234;
    }
    
    void reportChannelError(uint8_t channel, ChannelErrorType error) {
        if (!profibus_add_channel_diag(&diagnostics, channel, error, 0)) {
            std::cerr << "Failed to add channel diagnostic\n";
        }
    }
    
    void reportDeviceError(uint8_t error_code) {
        static uint32_t error_count = 0;
        error_count++;
        
        if (!profibus_add_device_diag(&diagnostics, 0xFF, error_code, error_count)) {
            std::cerr << "Failed to add device diagnostic\n";
        }
    }
    
    const ProfibusDiagnostics& getDiagnostics() const {
        return diagnostics;
    }
    
    void clearDiagnostics() {
        profibus_clear_diagnostics(&diagnostics);
    }
};

int main() {
    ProfibusDevice device;
    
    // Simulate various errors
    device.reportChannelError(0, CHANNEL_ERROR_WIRE_BREAK);
    device.reportChannelError(3, CHANNEL_ERROR_SHORT_CIRCUIT);
    device.reportDeviceError(0x42);
    
    // Display diagnostics
    profibus_print_diagnostics(&device.getDiagnostics());
    
    return 0;
}
```

### Rust Implementation

```rust
// profibus_extended_diag.rs

use std::mem;

// Constants
const MAX_DIAG_DATA_SIZE: usize = 244;
const STD_DIAG_SIZE: usize = 6;

// Diagnostic status flags
const DIAG_STATUS_ACTIVE: u8 = 0x01;
const DIAG_STATUS_OVERFLOW: u8 = 0x02;
const DIAG_STATUS_EXT_DIAG: u8 = 0x04;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DiagnosticType {
    Device = 0x00,
    Channel = 0x01,
    Identifier = 0x02,
    Manufacturer = 0x03,
}

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ChannelErrorType {
    None = 0x00,
    ShortCircuit = 0x01,
    WireBreak = 0x02,
    OverRange = 0x03,
    UnderRange = 0x04,
    Overload = 0x05,
    SensorFault = 0x06,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct StandardDiagnosis {
    pub station_status_1: u8,
    pub station_status_2: u8,
    pub station_status_3: u8,
    pub master_address: u8,
    pub ident_number: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct ExtDiagHeader {
    diagnostic_type: u8,
    length: u8,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ChannelDiagnosis {
    header: ExtDiagHeader,
    pub channel_number: u8,
    pub error_type: u8,
    pub additional_info: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct DeviceDiagnosis {
    header: ExtDiagHeader,
    pub device_status: u8,
    pub error_code: u8,
    pub error_count: u32,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct IdentifierDiagnosis {
    header: ExtDiagHeader,
    pub identifier: u16,
    pub qualifier: u8,
    pub channel_number: u8,
}

#[derive(Debug)]
pub struct ProfibusDiagnostics {
    std_diag: StandardDiagnosis,
    ext_diag_data: Vec<u8>,
}

impl Default for StandardDiagnosis {
    fn default() -> Self {
        StandardDiagnosis {
            station_status_1: 0,
            station_status_2: 0,
            station_status_3: 0,
            master_address: 0,
            ident_number: 0,
        }
    }
}

impl ProfibusDiagnostics {
    pub fn new() -> Self {
        ProfibusDiagnostics {
            std_diag: StandardDiagnosis::default(),
            ext_diag_data: Vec::new(),
        }
    }
    
    pub fn clear(&mut self) {
        self.std_diag = StandardDiagnosis::default();
        self.ext_diag_data.clear();
    }
    
    pub fn has_diagnostics(&self) -> bool {
        (self.std_diag.station_status_1 & DIAG_STATUS_ACTIVE) != 0
    }
    
    pub fn set_ident_number(&mut self, ident: u16) {
        self.std_diag.ident_number = ident;
    }
    
    pub fn add_channel_diag(
        &mut self,
        channel: u8,
        error: ChannelErrorType,
        additional_info: u16,
    ) -> Result<(), &'static str> {
        let diag_size = mem::size_of::<ChannelDiagnosis>();
        
        if self.ext_diag_data.len() + diag_size > MAX_DIAG_DATA_SIZE {
            self.std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
            return Err("Diagnostic buffer overflow");
        }
        
        let ch_diag = ChannelDiagnosis {
            header: ExtDiagHeader {
                diagnostic_type: DiagnosticType::Channel as u8,
                length: (diag_size - mem::size_of::<ExtDiagHeader>()) as u8,
            },
            channel_number: channel,
            error_type: error as u8,
            additional_info,
        };
        
        // Convert struct to bytes
        let bytes = unsafe {
            std::slice::from_raw_parts(
                &ch_diag as *const _ as *const u8,
                diag_size,
            )
        };
        
        self.ext_diag_data.extend_from_slice(bytes);
        self.std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
        
        Ok(())
    }
    
    pub fn add_device_diag(
        &mut self,
        device_status: u8,
        error_code: u8,
        error_count: u32,
    ) -> Result<(), &'static str> {
        let diag_size = mem::size_of::<DeviceDiagnosis>();
        
        if self.ext_diag_data.len() + diag_size > MAX_DIAG_DATA_SIZE {
            self.std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
            return Err("Diagnostic buffer overflow");
        }
        
        let dev_diag = DeviceDiagnosis {
            header: ExtDiagHeader {
                diagnostic_type: DiagnosticType::Device as u8,
                length: (diag_size - mem::size_of::<ExtDiagHeader>()) as u8,
            },
            device_status,
            error_code,
            error_count,
        };
        
        let bytes = unsafe {
            std::slice::from_raw_parts(
                &dev_diag as *const _ as *const u8,
                diag_size,
            )
        };
        
        self.ext_diag_data.extend_from_slice(bytes);
        self.std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
        
        Ok(())
    }
    
    pub fn add_identifier_diag(
        &mut self,
        identifier: u16,
        qualifier: u8,
        channel: u8,
    ) -> Result<(), &'static str> {
        let diag_size = mem::size_of::<IdentifierDiagnosis>();
        
        if self.ext_diag_data.len() + diag_size > MAX_DIAG_DATA_SIZE {
            self.std_diag.station_status_1 |= DIAG_STATUS_OVERFLOW;
            return Err("Diagnostic buffer overflow");
        }
        
        let id_diag = IdentifierDiagnosis {
            header: ExtDiagHeader {
                diagnostic_type: DiagnosticType::Identifier as u8,
                length: (diag_size - mem::size_of::<ExtDiagHeader>()) as u8,
            },
            identifier,
            qualifier,
            channel_number: channel,
        };
        
        let bytes = unsafe {
            std::slice::from_raw_parts(
                &id_diag as *const _ as *const u8,
                diag_size,
            )
        };
        
        self.ext_diag_data.extend_from_slice(bytes);
        self.std_diag.station_status_1 |= DIAG_STATUS_ACTIVE | DIAG_STATUS_EXT_DIAG;
        
        Ok(())
    }
    
    pub fn parse_from_bytes(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < STD_DIAG_SIZE {
            return Err("Data too short for standard diagnosis");
        }
        
        let mut diagnostics = ProfibusDiagnostics::new();
        
        // Parse standard diagnosis
        unsafe {
            std::ptr::copy_nonoverlapping(
                data.as_ptr(),
                &mut diagnostics.std_diag as *mut _ as *mut u8,
                STD_DIAG_SIZE,
            );
        }
        
        // Parse extended diagnosis if present
        if (diagnostics.std_diag.station_status_1 & DIAG_STATUS_EXT_DIAG) != 0 {
            let ext_data = &data[STD_DIAG_SIZE..];
            diagnostics.ext_diag_data.extend_from_slice(ext_data);
        }
        
        Ok(diagnostics)
    }
    
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        // Add standard diagnosis
        let std_bytes = unsafe {
            std::slice::from_raw_parts(
                &self.std_diag as *const _ as *const u8,
                STD_DIAG_SIZE,
            )
        };
        bytes.extend_from_slice(std_bytes);
        
        // Add extended diagnosis
        bytes.extend_from_slice(&self.ext_diag_data);
        
        bytes
    }
    
    pub fn print(&self) {
        println!("=== Profibus Diagnostics ===");
        println!("Station Status 1: 0x{:02X}", self.std_diag.station_status_1);
        println!(
            "Diagnostic Active: {}",
            if self.has_diagnostics() { "YES" } else { "NO" }
        );
        println!(
            "Extended Diag: {}",
            if (self.std_diag.station_status_1 & DIAG_STATUS_EXT_DIAG) != 0 {
                "YES"
            } else {
                "NO"
            }
        );
        
        if !self.ext_diag_data.is_empty() {
            println!("\nExtended Diagnostic Data ({} bytes):", self.ext_diag_data.len());
            self.print_extended_entries();
        }
    }
    
    fn print_extended_entries(&self) {
        let mut offset = 0;
        
        while offset < self.ext_diag_data.len() {
            if offset + 2 > self.ext_diag_data.len() {
                break;
            }
            
            let diag_type = self.ext_diag_data[offset];
            let length = self.ext_diag_data[offset + 1] as usize;
            
            print!("  Type: ");
            match diag_type {
                x if x == DiagnosticType::Device as u8 => {
                    println!("Device Diagnosis");
                    if offset + mem::size_of::<DeviceDiagnosis>() <= self.ext_diag_data.len() {
                        let dev_diag = unsafe {
                            &*(self.ext_diag_data[offset..].as_ptr() as *const DeviceDiagnosis)
                        };
                        println!(
                            "    Status: 0x{:02X}, Error Code: 0x{:02X}, Count: {}",
                            dev_diag.device_status, dev_diag.error_code, dev_diag.error_count
                        );
                    }
                }
                x if x == DiagnosticType::Channel as u8 => {
                    println!("Channel Diagnosis");
                    if offset + mem::size_of::<ChannelDiagnosis>() <= self.ext_diag_data.len() {
                        let ch_diag = unsafe {
                            &*(self.ext_diag_data[offset..].as_ptr() as *const ChannelDiagnosis)
                        };
                        println!(
                            "    Channel: {}, Error: {}, Info: 0x{:04X}",
                            ch_diag.channel_number, ch_diag.error_type, ch_diag.additional_info
                        );
                    }
                }
                x if x == DiagnosticType::Identifier as u8 => {
                    println!("Identifier Diagnosis");
                    if offset + mem::size_of::<IdentifierDiagnosis>() <= self.ext_diag_data.len() {
                        let id_diag = unsafe {
                            &*(self.ext_diag_data[offset..].as_ptr() as *const IdentifierDiagnosis)
                        };
                        println!(
                            "    ID: 0x{:04X}, Qualifier: 0x{:02X}, Channel: {}",
                            id_diag.identifier, id_diag.qualifier, id_diag.channel_number
                        );
                    }
                }
                _ => println!("Unknown (0x{:02X})", diag_type),
            }
            
            offset += 2 + length;
        }
    }
}

// Example device implementation
pub struct ProfibusDevice {
    diagnostics: ProfibusDiagnostics,
    error_count: u32,
}

impl ProfibusDevice {
    pub fn new(ident_number: u16) -> Self {
        let mut device = ProfibusDevice {
            diagnostics: ProfibusDiagnostics::new(),
            error_count: 0,
        };
        device.diagnostics.set_ident_number(ident_number);
        device
    }
    
    pub fn report_channel_error(&mut self, channel: u8, error: ChannelErrorType) {
        if let Err(e) = self.diagnostics.add_channel_diag(channel, error, 0) {
            eprintln!("Failed to add channel diagnostic: {}", e);
        }
    }
    
    pub fn report_device_error(&mut self, error_code: u8) {
        self.error_count += 1;
        if let Err(e) = self.diagnostics.add_device_diag(0xFF, error_code, self.error_count) {
            eprintln!("Failed to add device diagnostic: {}", e);
        }
    }
    
    pub fn report_identifier(&mut self, identifier: u16, qualifier: u8, channel: u8) {
        if let Err(e) = self.diagnostics.add_identifier_diag(identifier, qualifier, channel) {
            eprintln!("Failed to add identifier diagnostic: {}", e);
        }
    }
    
    pub fn get_diagnostics(&self) -> &ProfibusDiagnostics {
        &self.diagnostics
    }
    
    pub fn clear_diagnostics(&mut self) {
        self.diagnostics.clear();
        self.error_count = 0;
    }
    
    pub fn get_diagnostic_bytes(&self) -> Vec<u8> {
        self.diagnostics.to_bytes()
    }
}

// Example usage
fn main() {
    let mut device = ProfibusDevice::new(0x1234);
    
    // Simulate various errors
    device.report_channel_error(0, ChannelErrorType::WireBreak);
    device.report_channel_error(3, ChannelErrorType::ShortCircuit);
    device.report_device_error(0x42);
    device.report_identifier(0x8000, 0x01, 5);
    
    // Display diagnostics
    device.get_diagnostics().print();
    
    // Serialize to bytes
    let bytes = device.get_diagnostic_bytes();
    println!("\nDiagnostic data size: {} bytes", bytes.len());
    
    // Parse back from bytes
    match ProfibusDiagnostics::parse_from_bytes(&bytes) {
        Ok(parsed) => {
            println!("\n=== Parsed Diagnostics ===");
            parsed.print();
        }
        Err(e) => eprintln!("Parse error: {}", e),
    }
}
```

## Summary

**Extended Diagnosis Extended** in Profibus provides comprehensive fault detection and diagnostic capabilities that significantly exceed the basic 6-byte diagnostic structure. This advanced feature enables:

- **Detailed Error Reporting**: Multiple simultaneous errors can be reported with specific channel and device information
- **Hierarchical Structure**: Organized diagnostic data with device-level, channel-specific, and identifier-based entries
- **Manufacturer Flexibility**: Support for vendor-specific diagnostic information
- **Scalability**: Up to 244 bytes of extended diagnostic data

The implementations shown demonstrate how to build, parse, and manage extended diagnostic structures in both C/C++ and Rust, with proper memory management and type safety. This functionality is critical for industrial automation systems requiring detailed fault analysis, predictive maintenance, and comprehensive system monitoring.