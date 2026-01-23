# DP Diagnostic Data in Profibus

## Overview

DP Diagnostic Data is a fundamental feature of Profibus-DP (Decentralized Periphery) that enables masters to read diagnostic information from slave devices. This diagnostic capability is essential for troubleshooting, preventive maintenance, and monitoring the health of distributed automation systems.

## What is DP Diagnostic Data?

Profibus-DP slaves can provide diagnostic information to the master in two forms:

### 1. **Standard Diagnostic Data**
Standard diagnostic data follows the Profibus specification and includes:
- **Status bytes** (6 bytes minimum) containing:
  - Station status information
  - Master address that configured the slave
  - Manufacturer and device identifiers
  - Error flags and warnings
- **Station-specific diagnostics** indicating common error conditions like:
  - Configuration errors
  - Parameter errors
  - Master lock status
  - Static/dynamic errors

### 2. **Extended Diagnostic Data**
Extended diagnostics are device-specific and can include:
- Channel-specific error information
- Detailed fault descriptions
- Device-specific status information
- Manufacturer-specific diagnostic codes
- Temperature, voltage, or other sensor readings

## Diagnostic Data Structure

The diagnostic data telegram structure typically includes:

```
[Standard Diagnostic (6+ bytes)] [Extended Diagnostic (variable length)]
```

**Standard Diagnostic Bytes:**
- Byte 0: Station Status 1
- Byte 1: Station Status 2
- Byte 2: Station Status 3
- Byte 3: Master Address
- Byte 4-5: Manufacturer ID and Ident Number

## Programming Examples

### C/C++ Implementation

```cpp
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Profibus DP diagnostic data structures
#define DIAG_BUFSIZE 244
#define MIN_DIAG_LEN 6

// Status byte 0 flags
#define STAT0_STATION_NOT_READY    0x01
#define STAT0_CFG_FAULT           0x02
#define STAT0_EXT_DIAG_AVAILABLE  0x04
#define STAT0_NOT_SUPPORTED       0x08
#define STAT0_INVALID_RESPONSE    0x10
#define STAT0_MASTER_LOCK         0x20
#define STAT0_PRM_FAULT          0x40
#define STAT0_STATIC_DIAG        0x80

// Status byte 1 flags
#define STAT1_PRM_REQ            0x01
#define STAT1_STATIC_DIAG        0x02
#define STAT1_WD_ON              0x08
#define STAT1_FREEZE_MODE        0x10
#define STAT1_SYNC_MODE          0x20
#define STAT1_DEACTIVATED        0x80

// Status byte 2 flags
#define STAT2_EXT_DIAG_OVERFLOW  0x80

// Standard diagnostic data structure
typedef struct {
    uint8_t status[3];        // Station status bytes
    uint8_t master_addr;      // Address of master that configured slave
    uint16_t ident_number;    // Device identification
} dp_std_diag_t;

// Extended diagnostic entry
typedef struct {
    uint8_t type;             // Diagnostic type
    uint8_t slot;             // Slot number
    uint8_t specifier;        // Additional info
    uint8_t diag_data[4];     // Diagnostic specific data
} dp_ext_diag_entry_t;

// Complete diagnostic buffer
typedef struct {
    dp_std_diag_t std_diag;
    uint8_t ext_diag_len;
    uint8_t ext_diag[DIAG_BUFSIZE - MIN_DIAG_LEN];
} dp_diagnostic_t;

class ProfibusDPDiagnostics {
private:
    uint8_t slave_addr;
    dp_diagnostic_t diag_buffer;
    
public:
    ProfibusDPDiagnostics(uint8_t addr) : slave_addr(addr) {
        memset(&diag_buffer, 0, sizeof(diag_buffer));
    }
    
    // Read diagnostic data from slave
    int readDiagnostics() {
        // Simulated read - in real implementation, this would use
        // Profibus library calls like dp_read_diag()
        
        // Example: Using fictional Profibus library
        // return dp_read_diag(slave_addr, (uint8_t*)&diag_buffer, 
        //                     sizeof(diag_buffer));
        
        // Simulated diagnostic data for demonstration
        diag_buffer.std_diag.status[0] = STAT0_EXT_DIAG_AVAILABLE;
        diag_buffer.std_diag.status[1] = STAT1_WD_ON;
        diag_buffer.std_diag.status[2] = 0x00;
        diag_buffer.std_diag.master_addr = 2;
        diag_buffer.std_diag.ident_number = 0x1234;
        diag_buffer.ext_diag_len = 8;
        
        return 0; // Success
    }
    
    // Decode standard diagnostic status
    void decodeStandardDiag() {
        printf("=== Standard Diagnostic Data ===\n");
        printf("Slave Address: %d\n", slave_addr);
        printf("Master Address: %d\n", diag_buffer.std_diag.master_addr);
        printf("Ident Number: 0x%04X\n", diag_buffer.std_diag.ident_number);
        
        printf("\nStatus Byte 0 (0x%02X):\n", diag_buffer.std_diag.status[0]);
        if (diag_buffer.std_diag.status[0] & STAT0_STATION_NOT_READY)
            printf("  - Station Not Ready\n");
        if (diag_buffer.std_diag.status[0] & STAT0_CFG_FAULT)
            printf("  - Configuration Fault\n");
        if (diag_buffer.std_diag.status[0] & STAT0_EXT_DIAG_AVAILABLE)
            printf("  - Extended Diagnostics Available\n");
        if (diag_buffer.std_diag.status[0] & STAT0_PRM_FAULT)
            printf("  - Parameter Fault\n");
        if (diag_buffer.std_diag.status[0] & STAT0_MASTER_LOCK)
            printf("  - Master Lock Active\n");
        if (diag_buffer.std_diag.status[0] & STAT0_STATIC_DIAG)
            printf("  - Static Diagnostic\n");
            
        printf("\nStatus Byte 1 (0x%02X):\n", diag_buffer.std_diag.status[1]);
        if (diag_buffer.std_diag.status[1] & STAT1_PRM_REQ)
            printf("  - Parameter Request\n");
        if (diag_buffer.std_diag.status[1] & STAT1_WD_ON)
            printf("  - Watchdog ON\n");
        if (diag_buffer.std_diag.status[1] & STAT1_DEACTIVATED)
            printf("  - Slave Deactivated\n");
            
        printf("\nStatus Byte 2 (0x%02X):\n", diag_buffer.std_diag.status[2]);
        if (diag_buffer.std_diag.status[2] & STAT2_EXT_DIAG_OVERFLOW)
            printf("  - Extended Diagnostic Overflow\n");
    }
    
    // Decode extended diagnostic data
    void decodeExtendedDiag() {
        if (!(diag_buffer.std_diag.status[0] & STAT0_EXT_DIAG_AVAILABLE)) {
            printf("\n=== No Extended Diagnostics ===\n");
            return;
        }
        
        printf("\n=== Extended Diagnostic Data ===\n");
        printf("Extended Diag Length: %d bytes\n", diag_buffer.ext_diag_len);
        
        uint8_t* ext_ptr = diag_buffer.ext_diag;
        int offset = 0;
        
        while (offset < diag_buffer.ext_diag_len) {
            uint8_t header = ext_ptr[offset];
            uint8_t type = (header >> 6) & 0x03;
            uint8_t length = header & 0x3F;
            
            printf("\nDiagnostic Entry at offset %d:\n", offset);
            printf("  Type: ");
            
            switch(type) {
                case 0:
                    printf("Channel Diagnostic\n");
                    if (offset + 2 < diag_buffer.ext_diag_len) {
                        uint8_t channel = ext_ptr[offset + 1] & 0x3F;
                        uint8_t error_type = ext_ptr[offset + 2];
                        printf("  Channel: %d\n", channel);
                        printf("  Error Type: 0x%02X\n", error_type);
                    }
                    break;
                case 1:
                    printf("Device Diagnostic\n");
                    break;
                case 2:
                    printf("Manufacturer Specific\n");
                    break;
                default:
                    printf("Reserved\n");
                    break;
            }
            
            offset += length + 1;
        }
    }
    
    // Check if slave is operational
    bool isSlaveOK() {
        return !(diag_buffer.std_diag.status[0] & 
                (STAT0_STATION_NOT_READY | STAT0_CFG_FAULT | STAT0_PRM_FAULT));
    }
    
    // Get diagnostic summary
    void printSummary() {
        printf("\n=== Diagnostic Summary ===\n");
        printf("Slave %d: %s\n", slave_addr, 
               isSlaveOK() ? "OK" : "FAULT");
        
        if (!isSlaveOK()) {
            printf("Errors detected - check detailed diagnostics\n");
        }
    }
};

// Example usage
int main() {
    ProfibusDPDiagnostics slave_diag(5);
    
    // Read diagnostic data
    if (slave_diag.readDiagnostics() == 0) {
        slave_diag.decodeStandardDiag();
        slave_diag.decodeExtendedDiag();
        slave_diag.printSummary();
    } else {
        printf("Failed to read diagnostics from slave\n");
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

const DIAG_BUFSIZE: usize = 244;
const MIN_DIAG_LEN: usize = 6;

// Status byte 0 flags
const STAT0_STATION_NOT_READY: u8 = 0x01;
const STAT0_CFG_FAULT: u8 = 0x02;
const STAT0_EXT_DIAG_AVAILABLE: u8 = 0x04;
const STAT0_NOT_SUPPORTED: u8 = 0x08;
const STAT0_INVALID_RESPONSE: u8 = 0x10;
const STAT0_MASTER_LOCK: u8 = 0x20;
const STAT0_PRM_FAULT: u8 = 0x40;
const STAT0_STATIC_DIAG: u8 = 0x80;

// Status byte 1 flags
const STAT1_PRM_REQ: u8 = 0x01;
const STAT1_STATIC_DIAG: u8 = 0x02;
const STAT1_WD_ON: u8 = 0x08;
const STAT1_FREEZE_MODE: u8 = 0x10;
const STAT1_SYNC_MODE: u8 = 0x20;
const STAT1_DEACTIVATED: u8 = 0x80;

// Status byte 2 flags
const STAT2_EXT_DIAG_OVERFLOW: u8 = 0x80;

#[derive(Debug, Clone, Copy)]
struct StandardDiagnostic {
    status: [u8; 3],
    master_addr: u8,
    ident_number: u16,
}

impl StandardDiagnostic {
    fn new() -> Self {
        Self {
            status: [0; 3],
            master_addr: 0,
            ident_number: 0,
        }
    }
    
    fn has_extended_diag(&self) -> bool {
        (self.status[0] & STAT0_EXT_DIAG_AVAILABLE) != 0
    }
    
    fn is_slave_ok(&self) -> bool {
        (self.status[0] & (STAT0_STATION_NOT_READY | STAT0_CFG_FAULT | STAT0_PRM_FAULT)) == 0
    }
}

#[derive(Debug)]
enum DiagnosticType {
    ChannelDiagnostic,
    DeviceDiagnostic,
    ManufacturerSpecific,
    Reserved,
}

#[derive(Debug)]
struct ExtendedDiagEntry {
    diag_type: DiagnosticType,
    channel: Option<u8>,
    error_type: Option<u8>,
    data: Vec<u8>,
}

#[derive(Debug)]
struct DPDiagnostic {
    slave_addr: u8,
    std_diag: StandardDiagnostic,
    ext_diag: Vec<u8>,
}

impl DPDiagnostic {
    fn new(slave_addr: u8) -> Self {
        Self {
            slave_addr,
            std_diag: StandardDiagnostic::new(),
            ext_diag: Vec::new(),
        }
    }
    
    /// Read diagnostic data from slave
    /// In a real implementation, this would use a Profibus driver library
    fn read_diagnostics(&mut self) -> Result<(), String> {
        // Simulated diagnostic read
        // Real implementation would call something like:
        // profibus_driver::read_diag(self.slave_addr, &mut buffer)?;
        
        // Simulated diagnostic data
        self.std_diag.status[0] = STAT0_EXT_DIAG_AVAILABLE;
        self.std_diag.status[1] = STAT1_WD_ON;
        self.std_diag.status[2] = 0x00;
        self.std_diag.master_addr = 2;
        self.std_diag.ident_number = 0x1234;
        
        // Simulated extended diagnostic
        self.ext_diag = vec![
            0x04, 0x01, 0x05, // Channel diag: Channel 1, Error 5
            0x40, 0x02, 0x10, // Manufacturer specific
        ];
        
        Ok(())
    }
    
    /// Decode and print standard diagnostic information
    fn decode_standard_diag(&self) {
        println!("=== Standard Diagnostic Data ===");
        println!("Slave Address: {}", self.slave_addr);
        println!("Master Address: {}", self.std_diag.master_addr);
        println!("Ident Number: 0x{:04X}", self.std_diag.ident_number);
        
        println!("\nStatus Byte 0 (0x{:02X}):", self.std_diag.status[0]);
        self.print_status_flags(self.std_diag.status[0], &[
            (STAT0_STATION_NOT_READY, "Station Not Ready"),
            (STAT0_CFG_FAULT, "Configuration Fault"),
            (STAT0_EXT_DIAG_AVAILABLE, "Extended Diagnostics Available"),
            (STAT0_NOT_SUPPORTED, "Not Supported"),
            (STAT0_INVALID_RESPONSE, "Invalid Response"),
            (STAT0_MASTER_LOCK, "Master Lock Active"),
            (STAT0_PRM_FAULT, "Parameter Fault"),
            (STAT0_STATIC_DIAG, "Static Diagnostic"),
        ]);
        
        println!("\nStatus Byte 1 (0x{:02X}):", self.std_diag.status[1]);
        self.print_status_flags(self.std_diag.status[1], &[
            (STAT1_PRM_REQ, "Parameter Request"),
            (STAT1_STATIC_DIAG, "Static Diagnostic"),
            (STAT1_WD_ON, "Watchdog ON"),
            (STAT1_FREEZE_MODE, "Freeze Mode"),
            (STAT1_SYNC_MODE, "Sync Mode"),
            (STAT1_DEACTIVATED, "Slave Deactivated"),
        ]);
        
        println!("\nStatus Byte 2 (0x{:02X}):", self.std_diag.status[2]);
        self.print_status_flags(self.std_diag.status[2], &[
            (STAT2_EXT_DIAG_OVERFLOW, "Extended Diagnostic Overflow"),
        ]);
    }
    
    fn print_status_flags(&self, status: u8, flags: &[(u8, &str)]) {
        for (mask, description) in flags {
            if (status & mask) != 0 {
                println!("  - {}", description);
            }
        }
    }
    
    /// Parse and decode extended diagnostic data
    fn decode_extended_diag(&self) -> Vec<ExtendedDiagEntry> {
        if !self.std_diag.has_extended_diag() {
            return Vec::new();
        }
        
        let mut entries = Vec::new();
        let mut offset = 0;
        
        while offset < self.ext_diag.len() {
            let header = self.ext_diag[offset];
            let type_bits = (header >> 6) & 0x03;
            let length = (header & 0x3F) as usize;
            
            let diag_type = match type_bits {
                0 => DiagnosticType::ChannelDiagnostic,
                1 => DiagnosticType::DeviceDiagnostic,
                2 => DiagnosticType::ManufacturerSpecific,
                _ => DiagnosticType::Reserved,
            };
            
            let mut channel = None;
            let mut error_type = None;
            
            if matches!(diag_type, DiagnosticType::ChannelDiagnostic) 
                && offset + 2 < self.ext_diag.len() {
                channel = Some(self.ext_diag[offset + 1] & 0x3F);
                error_type = Some(self.ext_diag[offset + 2]);
            }
            
            let data_start = offset + 1;
            let data_end = (data_start + length).min(self.ext_diag.len());
            let data = self.ext_diag[data_start..data_end].to_vec();
            
            entries.push(ExtendedDiagEntry {
                diag_type,
                channel,
                error_type,
                data,
            });
            
            offset += length + 1;
        }
        
        entries
    }
    
    /// Print extended diagnostic information
    fn print_extended_diag(&self) {
        if !self.std_diag.has_extended_diag() {
            println!("\n=== No Extended Diagnostics ===");
            return;
        }
        
        println!("\n=== Extended Diagnostic Data ===");
        println!("Extended Diag Length: {} bytes", self.ext_diag.len());
        
        let entries = self.decode_extended_diag();
        
        for (i, entry) in entries.iter().enumerate() {
            println!("\nDiagnostic Entry {}:", i + 1);
            println!("  Type: {:?}", entry.diag_type);
            
            if let Some(ch) = entry.channel {
                println!("  Channel: {}", ch);
            }
            
            if let Some(err) = entry.error_type {
                println!("  Error Type: 0x{:02X}", err);
            }
            
            if !entry.data.is_empty() {
                print!("  Data: ");
                for byte in &entry.data {
                    print!("{:02X} ", byte);
                }
                println!();
            }
        }
    }
    
    /// Print diagnostic summary
    fn print_summary(&self) {
        println!("\n=== Diagnostic Summary ===");
        let status = if self.std_diag.is_slave_ok() {
            "OK"
        } else {
            "FAULT"
        };
        println!("Slave {}: {}", self.slave_addr, status);
        
        if !self.std_diag.is_slave_ok() {
            println!("Errors detected - check detailed diagnostics");
        }
    }
}

impl fmt::Display for DPDiagnostic {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "DP Slave {} - Status: {}",
            self.slave_addr,
            if self.std_diag.is_slave_ok() { "OK" } else { "FAULT" }
        )
    }
}

fn main() {
    let mut diag = DPDiagnostic::new(5);
    
    match diag.read_diagnostics() {
        Ok(_) => {
            diag.decode_standard_diag();
            diag.print_extended_diag();
            diag.print_summary();
            println!("\n{}", diag);
        }
        Err(e) => {
            eprintln!("Failed to read diagnostics: {}", e);
        }
    }
}
```

## Key Concepts in Implementation

### 1. **Standard Diagnostic Reading**
The standard diagnostic provides essential health information:
- Always 6 bytes minimum
- Contains station status flags that indicate operational state
- Includes master address and device identification

### 2. **Status Flag Interpretation**
Critical flags to monitor:
- **Station Not Ready**: Device cannot operate
- **Configuration Fault**: Mismatch between expected and actual configuration
- **Parameter Fault**: Invalid parameter settings
- **Extended Diagnostics Available**: Additional diagnostic information present

### 3. **Extended Diagnostic Parsing**
Extended diagnostics use a header-data structure:
- First byte contains type (2 bits) and length (6 bits)
- Channel diagnostics identify specific I/O problems
- Manufacturer-specific diagnostics provide vendor-defined information

### 4. **Diagnostic Polling Strategy**
In production systems:
- Poll diagnostics cyclically (e.g., every 100ms-1s depending on application)
- Read immediately when a slave goes offline
- Trigger alarms based on specific diagnostic conditions
- Log diagnostic history for predictive maintenance

## Common Use Cases

1. **Fault Diagnosis**: Identifying why a slave device stopped communicating
2. **Configuration Verification**: Ensuring slaves are properly configured
3. **Preventive Maintenance**: Detecting warnings before critical failures
4. **Channel-Level Troubleshooting**: Pinpointing specific sensor/actuator problems
5. **System Monitoring**: Real-time health status of distributed I/O

## Summary

**DP Diagnostic Data** is a critical feature of Profibus-DP that enables comprehensive monitoring and troubleshooting of slave devices. The diagnostic system provides both standardized status information (minimum 6 bytes) and optional extended device-specific diagnostics.

**Key points:**
- Standard diagnostics include station status flags, master address, and device identification
- Extended diagnostics provide channel-specific and manufacturer-specific fault information
- Status bytes use bit flags to indicate various error conditions and operational states
- Proper diagnostic interpretation is essential for maintaining system reliability
- Both C/C++ and Rust implementations follow similar patterns: read diagnostic buffer, decode standard status bytes, parse extended diagnostic entries

The code examples demonstrate how to read, parse, and interpret diagnostic data, check critical status flags, and present diagnostic information in a structured format. In production systems, this functionality would be integrated with Profibus driver libraries and coupled with alarm management and logging systems for comprehensive device monitoring.