# FDL Status and Diagnostics in Profibus

## Overview

The Fieldbus Data Link (FDL) layer in Profibus provides status and diagnostic information crucial for monitoring network health, troubleshooting communication issues, and ensuring reliable operation. FDL status bytes contain information about station readiness, master/slave roles, and operational states, while diagnostic data helps identify specific problems in the network or individual devices.

## Key Concepts

### FDL Status Byte Structure

The FDL status byte typically contains:
- **Station Status**: Ready/not ready for data exchange
- **Master/Slave Role**: Indicates if the device is a master or slave
- **Token Possession**: Whether the station currently holds the token (for masters)
- **Fault Indicators**: Communication errors or configuration issues

### Common Status Bits

1. **Station_Non_Existent** (SNE): Device not responding
2. **Station_Ready** (SR): Device ready for communication
3. **Data_Exchange** (DX): Data exchange in progress
4. **Master_Mode**: Station operates as a master
5. **Token_Lost**: Master has lost the token

### Diagnostic Information

Diagnostic data includes:
- Communication error counters
- Timeout occurrences
- Configuration mismatches
- Hardware-specific diagnostic bytes
- Extended diagnostic information per DP specification

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// FDL Status byte bit definitions
#define FDL_STATUS_STATION_NON_EXISTENT  0x01
#define FDL_STATUS_STATION_READY         0x02
#define FDL_STATUS_DATA_EXCHANGE         0x04
#define FDL_STATUS_MASTER_MODE           0x08
#define FDL_STATUS_TOKEN_LOST            0x10
#define FDL_STATUS_CONFIGURATION_OK      0x20
#define FDL_STATUS_REDUNDANT_DEVICE      0x40
#define FDL_STATUS_RESERVED              0x80

// FDL Diagnostic structure
typedef struct {
    uint8_t status_byte;
    uint16_t error_count;
    uint16_t timeout_count;
    uint8_t last_error_code;
    uint32_t timestamp;
} FDL_Diagnostics;

// Station diagnostic information
typedef struct {
    uint8_t station_address;
    FDL_Diagnostics diagnostics;
    bool is_active;
    char station_name[32];
} FDL_StationInfo;

/**
 * Read FDL status byte from a Profibus station
 * In real implementation, this would communicate with hardware
 */
uint8_t fdl_read_status(uint8_t station_address) {
    // Simulated hardware read - replace with actual FDL call
    // Example: return profibus_read_register(station_address, FDL_STATUS_REG);
    
    // For demonstration, return a sample status
    return FDL_STATUS_STATION_READY | FDL_STATUS_CONFIGURATION_OK;
}

/**
 * Decode and interpret FDL status byte
 */
void fdl_decode_status(uint8_t status, char *output, size_t output_size) {
    output[0] = '\0';
    
    if (status & FDL_STATUS_STATION_NON_EXISTENT) {
        strncat(output, "Station Non-Existent; ", output_size - strlen(output) - 1);
    }
    if (status & FDL_STATUS_STATION_READY) {
        strncat(output, "Station Ready; ", output_size - strlen(output) - 1);
    }
    if (status & FDL_STATUS_DATA_EXCHANGE) {
        strncat(output, "Data Exchange Active; ", output_size - strlen(output) - 1);
    }
    if (status & FDL_STATUS_MASTER_MODE) {
        strncat(output, "Master Mode; ", output_size - strlen(output) - 1);
    }
    if (status & FDL_STATUS_TOKEN_LOST) {
        strncat(output, "Token Lost; ", output_size - strlen(output) - 1);
    }
    if (status & FDL_STATUS_CONFIGURATION_OK) {
        strncat(output, "Configuration OK; ", output_size - strlen(output) - 1);
    }
}

/**
 * Read comprehensive diagnostics from a station
 */
int fdl_read_diagnostics(uint8_t station_address, FDL_Diagnostics *diag) {
    if (!diag) return -1;
    
    diag->status_byte = fdl_read_status(station_address);
    
    // In real implementation, read from hardware registers
    // Example hardware access (simulated here):
    diag->error_count = 0;      // Read from error counter register
    diag->timeout_count = 0;    // Read from timeout counter register
    diag->last_error_code = 0;  // Read from error code register
    diag->timestamp = 0;        // Current timestamp
    
    return 0;
}

/**
 * Check if station is healthy based on diagnostics
 */
bool fdl_is_station_healthy(const FDL_Diagnostics *diag) {
    if (!diag) return false;
    
    // Check critical status bits
    if (diag->status_byte & FDL_STATUS_STATION_NON_EXISTENT) {
        return false;
    }
    
    if (!(diag->status_byte & FDL_STATUS_STATION_READY)) {
        return false;
    }
    
    if (diag->status_byte & FDL_STATUS_TOKEN_LOST) {
        return false;
    }
    
    // Check error thresholds
    if (diag->error_count > 100 || diag->timeout_count > 50) {
        return false;
    }
    
    return true;
}

/**
 * Print diagnostic report for a station
 */
void fdl_print_diagnostics(const FDL_StationInfo *station) {
    if (!station) return;
    
    printf("\n=== FDL Diagnostics for Station %d (%s) ===\n", 
           station->station_address, station->station_name);
    
    char status_str[256];
    fdl_decode_status(station->diagnostics.status_byte, status_str, sizeof(status_str));
    printf("Status: 0x%02X - %s\n", station->diagnostics.status_byte, status_str);
    
    printf("Error Count: %u\n", station->diagnostics.error_count);
    printf("Timeout Count: %u\n", station->diagnostics.timeout_count);
    printf("Last Error Code: 0x%02X\n", station->diagnostics.last_error_code);
    printf("Health Status: %s\n", 
           fdl_is_station_healthy(&station->diagnostics) ? "HEALTHY" : "DEGRADED");
}

// Example usage
int main() {
    FDL_StationInfo station = {
        .station_address = 5,
        .is_active = true,
        .station_name = "Robot Controller"
    };
    
    // Read diagnostics
    if (fdl_read_diagnostics(station.station_address, &station.diagnostics) == 0) {
        fdl_print_diagnostics(&station);
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::fmt;

// FDL Status byte bit flags
#[derive(Debug, Clone, Copy)]
pub struct FdlStatusFlags {
    pub station_non_existent: bool,
    pub station_ready: bool,
    pub data_exchange: bool,
    pub master_mode: bool,
    pub token_lost: bool,
    pub configuration_ok: bool,
    pub redundant_device: bool,
}

impl FdlStatusFlags {
    const STATION_NON_EXISTENT: u8 = 0x01;
    const STATION_READY: u8 = 0x02;
    const DATA_EXCHANGE: u8 = 0x04;
    const MASTER_MODE: u8 = 0x08;
    const TOKEN_LOST: u8 = 0x10;
    const CONFIGURATION_OK: u8 = 0x20;
    const REDUNDANT_DEVICE: u8 = 0x40;

    pub fn from_byte(status: u8) -> Self {
        FdlStatusFlags {
            station_non_existent: (status & Self::STATION_NON_EXISTENT) != 0,
            station_ready: (status & Self::STATION_READY) != 0,
            data_exchange: (status & Self::DATA_EXCHANGE) != 0,
            master_mode: (status & Self::MASTER_MODE) != 0,
            token_lost: (status & Self::TOKEN_LOST) != 0,
            configuration_ok: (status & Self::CONFIGURATION_OK) != 0,
            redundant_device: (status & Self::REDUNDANT_DEVICE) != 0,
        }
    }

    pub fn to_byte(&self) -> u8 {
        let mut status = 0u8;
        if self.station_non_existent { status |= Self::STATION_NON_EXISTENT; }
        if self.station_ready { status |= Self::STATION_READY; }
        if self.data_exchange { status |= Self::DATA_EXCHANGE; }
        if self.master_mode { status |= Self::MASTER_MODE; }
        if self.token_lost { status |= Self::TOKEN_LOST; }
        if self.configuration_ok { status |= Self::CONFIGURATION_OK; }
        if self.redundant_device { status |= Self::REDUNDANT_DEVICE; }
        status
    }
}

impl fmt::Display for FdlStatusFlags {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut flags = Vec::new();
        if self.station_non_existent { flags.push("Non-Existent"); }
        if self.station_ready { flags.push("Ready"); }
        if self.data_exchange { flags.push("Data Exchange"); }
        if self.master_mode { flags.push("Master"); }
        if self.token_lost { flags.push("Token Lost"); }
        if self.configuration_ok { flags.push("Config OK"); }
        if self.redundant_device { flags.push("Redundant"); }
        
        write!(f, "{}", flags.join(", "))
    }
}

#[derive(Debug, Clone)]
pub struct FdlDiagnostics {
    pub status_byte: u8,
    pub status_flags: FdlStatusFlags,
    pub error_count: u16,
    pub timeout_count: u16,
    pub last_error_code: u8,
    pub timestamp: u64,
}

impl FdlDiagnostics {
    pub fn new(status_byte: u8) -> Self {
        FdlDiagnostics {
            status_byte,
            status_flags: FdlStatusFlags::from_byte(status_byte),
            error_count: 0,
            timeout_count: 0,
            last_error_code: 0,
            timestamp: 0,
        }
    }

    pub fn is_healthy(&self) -> bool {
        // Check critical conditions
        if self.status_flags.station_non_existent {
            return false;
        }
        
        if !self.status_flags.station_ready {
            return false;
        }
        
        if self.status_flags.token_lost {
            return false;
        }
        
        // Check error thresholds
        if self.error_count > 100 || self.timeout_count > 50 {
            return false;
        }
        
        true
    }

    pub fn health_status(&self) -> &str {
        if self.is_healthy() {
            "HEALTHY"
        } else {
            "DEGRADED"
        }
    }
}

#[derive(Debug, Clone)]
pub struct FdlStationInfo {
    pub station_address: u8,
    pub station_name: String,
    pub diagnostics: FdlDiagnostics,
    pub is_active: bool,
}

impl FdlStationInfo {
    pub fn new(address: u8, name: String) -> Self {
        FdlStationInfo {
            station_address: address,
            station_name: name,
            diagnostics: FdlDiagnostics::new(0),
            is_active: false,
        }
    }

    pub fn update_diagnostics(&mut self) -> Result<(), String> {
        // In real implementation, read from hardware
        let status = self.read_status()?;
        
        self.diagnostics = FdlDiagnostics {
            status_byte: status,
            status_flags: FdlStatusFlags::from_byte(status),
            error_count: self.read_error_count()?,
            timeout_count: self.read_timeout_count()?,
            last_error_code: self.read_last_error()?,
            timestamp: self.get_timestamp(),
        };
        
        Ok(())
    }

    fn read_status(&self) -> Result<u8, String> {
        // Simulated hardware read
        // Replace with actual FDL communication
        Ok(FdlStatusFlags::STATION_READY | FdlStatusFlags::CONFIGURATION_OK)
    }

    fn read_error_count(&self) -> Result<u16, String> {
        // Read from hardware register
        Ok(0)
    }

    fn read_timeout_count(&self) -> Result<u16, String> {
        // Read from hardware register
        Ok(0)
    }

    fn read_last_error(&self) -> Result<u8, String> {
        // Read from hardware register
        Ok(0)
    }

    fn get_timestamp(&self) -> u64 {
        // Return current timestamp
        0
    }

    pub fn print_diagnostics(&self) {
        println!("\n=== FDL Diagnostics for Station {} ({}) ===", 
                 self.station_address, self.station_name);
        println!("Status: 0x{:02X} - {}", 
                 self.diagnostics.status_byte, 
                 self.diagnostics.status_flags);
        println!("Error Count: {}", self.diagnostics.error_count);
        println!("Timeout Count: {}", self.diagnostics.timeout_count);
        println!("Last Error Code: 0x{:02X}", self.diagnostics.last_error_code);
        println!("Health Status: {}", self.diagnostics.health_status());
    }
}

// Diagnostic monitoring system
pub struct FdlMonitor {
    stations: Vec<FdlStationInfo>,
}

impl FdlMonitor {
    pub fn new() -> Self {
        FdlMonitor {
            stations: Vec::new(),
        }
    }

    pub fn add_station(&mut self, station: FdlStationInfo) {
        self.stations.push(station);
    }

    pub fn poll_all_stations(&mut self) -> Result<(), String> {
        for station in &mut self.stations {
            station.update_diagnostics()?;
        }
        Ok(())
    }

    pub fn get_unhealthy_stations(&self) -> Vec<&FdlStationInfo> {
        self.stations.iter()
            .filter(|s| !s.diagnostics.is_healthy())
            .collect()
    }

    pub fn print_summary(&self) {
        println!("\n=== FDL Network Summary ===");
        println!("Total Stations: {}", self.stations.len());
        
        let healthy_count = self.stations.iter()
            .filter(|s| s.diagnostics.is_healthy())
            .count();
        
        println!("Healthy: {}", healthy_count);
        println!("Degraded: {}", self.stations.len() - healthy_count);
        
        let unhealthy = self.get_unhealthy_stations();
        if !unhealthy.is_empty() {
            println!("\nUnhealthy Stations:");
            for station in unhealthy {
                println!("  - Station {}: {}", 
                         station.station_address, 
                         station.station_name);
            }
        }
    }
}

// Example usage
fn main() {
    let mut monitor = FdlMonitor::new();
    
    // Add stations to monitor
    let mut station1 = FdlStationInfo::new(5, "Robot Controller".to_string());
    let mut station2 = FdlStationInfo::new(7, "PLC Main".to_string());
    
    // Update diagnostics
    station1.update_diagnostics().ok();
    station2.update_diagnostics().ok();
    
    station1.print_diagnostics();
    station2.print_diagnostics();
    
    monitor.add_station(station1);
    monitor.add_station(station2);
    
    monitor.print_summary();
}
```

## Summary

FDL Status and Diagnostics provide essential visibility into Profibus network operations. The status byte reveals the operational state of each station through standardized bit flags indicating readiness, communication activity, and fault conditions. Diagnostic information extends this by tracking error counters, timeout events, and specific error codes that help pinpoint problems.

Key implementation considerations include regularly polling status bytes to detect changes in network health, maintaining error count thresholds to identify degrading stations before failure, implementing proper decoding logic for status bits according to Profibus specifications, and correlating diagnostic data across multiple stations to identify systemic issues. Both the C/C++ and Rust implementations demonstrate parsing status bytes into meaningful flags, aggregating diagnostic data for health assessment, and providing monitoring infrastructure for network-wide diagnostics. This diagnostic capability is fundamental for maintaining reliable industrial automation systems and reducing downtime through proactive maintenance.