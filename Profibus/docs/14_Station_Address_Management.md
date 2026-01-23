# Station Address Management in Profibus

## Overview

Station Address Management is a fundamental aspect of Profibus network configuration that defines how devices are identified and communicate on the bus. Profibus supports up to 126 addressable stations (addresses 1-126), with address 0 reserved for broadcast messages. Proper address management is critical for network stability, deterministic communication, and avoiding conflicts.

## Key Concepts

### Addressing Scheme

**Address Range:**
- Valid station addresses: 1-126
- Address 0: Reserved for broadcast messages (all stations listen)
- Total capacity: 126 devices on a single segment

**Address Types:**
- **Master Addresses (1-126):** Active stations that can control the token
- **Slave Addresses (1-126):** Passive stations that only respond when polled
- **Mixed Networks:** Masters and slaves can coexist using different addresses

### Address Assignment Methods

1. **Hardware-based (DIP switches):** Physical switches on devices
2. **Software-based:** Configuration via engineering tools
3. **Dynamic assignment:** Rare in Profibus, mostly static addressing
4. **GSD-file defined:** Device capabilities define allowable address ranges

### Address Collision Prevention

- Each address must be unique on the segment
- Network scan tools detect conflicts before operation
- Token passing mechanism ensures single master control at any time

## Programming Examples

### C/C++ Implementation

```c
// Profibus Station Address Manager
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PROFIBUS_MIN_ADDRESS 1
#define PROFIBUS_MAX_ADDRESS 126
#define PROFIBUS_BROADCAST_ADDRESS 0
#define MAX_STATIONS 126

// Station types
typedef enum {
    STATION_TYPE_MASTER_CLASS1,  // Active master with token
    STATION_TYPE_MASTER_CLASS2,  // Passive master (diagnostic)
    STATION_TYPE_SLAVE           // Passive slave device
} StationType;

// Station status
typedef enum {
    STATION_STATUS_OFFLINE,
    STATION_STATUS_ONLINE,
    STATION_STATUS_ERROR,
    STATION_STATUS_CONFIGURED
} StationStatus;

// Station information structure
typedef struct {
    uint8_t address;
    StationType type;
    StationStatus status;
    char device_name[32];
    uint16_t vendor_id;
    uint16_t device_id;
    bool is_active;
} StationInfo;

// Address management context
typedef struct {
    StationInfo stations[MAX_STATIONS];
    bool address_map[MAX_STATIONS + 1];  // 0-126
    uint8_t active_station_count;
    uint8_t master_count;
    uint8_t slave_count;
} AddressManager;

// Initialize address manager
void address_manager_init(AddressManager *mgr) {
    memset(mgr, 0, sizeof(AddressManager));
    
    for (int i = 0; i < MAX_STATIONS; i++) {
        mgr->stations[i].address = 0;
        mgr->stations[i].status = STATION_STATUS_OFFLINE;
        mgr->stations[i].is_active = false;
    }
}

// Check if address is valid
bool is_valid_address(uint8_t address) {
    return (address >= PROFIBUS_MIN_ADDRESS && 
            address <= PROFIBUS_MAX_ADDRESS);
}

// Check if address is available
bool is_address_available(AddressManager *mgr, uint8_t address) {
    if (!is_valid_address(address)) {
        return false;
    }
    return !mgr->address_map[address];
}

// Assign address to a station
int assign_station_address(AddressManager *mgr, uint8_t address, 
                           StationType type, const char *device_name) {
    if (!is_valid_address(address)) {
        return -1; // Invalid address
    }
    
    if (!is_address_available(mgr, address)) {
        return -2; // Address already in use
    }
    
    // Find free slot in stations array
    for (int i = 0; i < MAX_STATIONS; i++) {
        if (!mgr->stations[i].is_active) {
            mgr->stations[i].address = address;
            mgr->stations[i].type = type;
            mgr->stations[i].status = STATION_STATUS_CONFIGURED;
            mgr->stations[i].is_active = true;
            strncpy(mgr->stations[i].device_name, device_name, 31);
            
            mgr->address_map[address] = true;
            mgr->active_station_count++;
            
            if (type == STATION_TYPE_SLAVE) {
                mgr->slave_count++;
            } else {
                mgr->master_count++;
            }
            
            return 0; // Success
        }
    }
    
    return -3; // No free slots
}

// Remove station by address
int remove_station_address(AddressManager *mgr, uint8_t address) {
    if (!is_valid_address(address)) {
        return -1;
    }
    
    for (int i = 0; i < MAX_STATIONS; i++) {
        if (mgr->stations[i].is_active && 
            mgr->stations[i].address == address) {
            
            if (mgr->stations[i].type == STATION_TYPE_SLAVE) {
                mgr->slave_count--;
            } else {
                mgr->master_count--;
            }
            
            mgr->stations[i].is_active = false;
            mgr->address_map[address] = false;
            mgr->active_station_count--;
            
            return 0;
        }
    }
    
    return -2; // Address not found
}

// Get station info by address
StationInfo* get_station_info(AddressManager *mgr, uint8_t address) {
    for (int i = 0; i < MAX_STATIONS; i++) {
        if (mgr->stations[i].is_active && 
            mgr->stations[i].address == address) {
            return &mgr->stations[i];
        }
    }
    return NULL;
}

// Find next available address
int find_next_available_address(AddressManager *mgr, uint8_t start_address) {
    for (uint8_t addr = start_address; addr <= PROFIBUS_MAX_ADDRESS; addr++) {
        if (is_address_available(mgr, addr)) {
            return addr;
        }
    }
    return -1; // No available address
}

// Scan for address conflicts
bool scan_address_conflicts(AddressManager *mgr, uint8_t *conflict_addresses, 
                            int *conflict_count) {
    *conflict_count = 0;
    bool conflicts_found = false;
    
    for (int i = 0; i < MAX_STATIONS; i++) {
        if (!mgr->stations[i].is_active) continue;
        
        int count = 0;
        for (int j = 0; j < MAX_STATIONS; j++) {
            if (mgr->stations[j].is_active && 
                mgr->stations[i].address == mgr->stations[j].address) {
                count++;
            }
        }
        
        if (count > 1) {
            conflict_addresses[*conflict_count] = mgr->stations[i].address;
            (*conflict_count)++;
            conflicts_found = true;
        }
    }
    
    return conflicts_found;
}

// Example usage
int main() {
    AddressManager mgr;
    address_manager_init(&mgr);
    
    // Assign addresses
    assign_station_address(&mgr, 2, STATION_TYPE_MASTER_CLASS1, "PLC_Master");
    assign_station_address(&mgr, 5, STATION_TYPE_SLAVE, "Temperature_Sensor");
    assign_station_address(&mgr, 10, STATION_TYPE_SLAVE, "Valve_Controller");
    assign_station_address(&mgr, 15, STATION_TYPE_SLAVE, "Motor_Drive");
    
    printf("Active stations: %d (Masters: %d, Slaves: %d)\n", 
           mgr.active_station_count, mgr.master_count, mgr.slave_count);
    
    // Find next available address
    int next_addr = find_next_available_address(&mgr, 1);
    printf("Next available address: %d\n", next_addr);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;

const PROFIBUS_MIN_ADDRESS: u8 = 1;
const PROFIBUS_MAX_ADDRESS: u8 = 126;
const PROFIBUS_BROADCAST_ADDRESS: u8 = 0;

#[derive(Debug, Clone, Copy, PartialEq)]
enum StationType {
    MasterClass1,  // Active master
    MasterClass2,  // Passive master
    Slave,         // Slave device
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum StationStatus {
    Offline,
    Online,
    Error,
    Configured,
}

#[derive(Debug, Clone)]
struct StationInfo {
    address: u8,
    station_type: StationType,
    status: StationStatus,
    device_name: String,
    vendor_id: u16,
    device_id: u16,
}

impl StationInfo {
    fn new(address: u8, station_type: StationType, device_name: String) -> Self {
        Self {
            address,
            station_type,
            status: StationStatus::Configured,
            device_name,
            vendor_id: 0,
            device_id: 0,
        }
    }
}

#[derive(Debug)]
struct AddressManager {
    stations: HashMap<u8, StationInfo>,
    address_map: [bool; 127], // 0-126
}

impl AddressManager {
    fn new() -> Self {
        Self {
            stations: HashMap::new(),
            address_map: [false; 127],
        }
    }
    
    /// Check if address is within valid range
    fn is_valid_address(&self, address: u8) -> bool {
        (PROFIBUS_MIN_ADDRESS..=PROFIBUS_MAX_ADDRESS).contains(&address)
    }
    
    /// Check if address is available
    fn is_address_available(&self, address: u8) -> bool {
        self.is_valid_address(address) && !self.address_map[address as usize]
    }
    
    /// Assign address to a station
    fn assign_address(
        &mut self,
        address: u8,
        station_type: StationType,
        device_name: String,
    ) -> Result<(), String> {
        if !self.is_valid_address(address) {
            return Err(format!("Invalid address: {}", address));
        }
        
        if !self.is_address_available(address) {
            return Err(format!("Address {} already in use", address));
        }
        
        let station = StationInfo::new(address, station_type, device_name);
        self.stations.insert(address, station);
        self.address_map[address as usize] = true;
        
        Ok(())
    }
    
    /// Remove station by address
    fn remove_address(&mut self, address: u8) -> Result<(), String> {
        if !self.is_valid_address(address) {
            return Err(format!("Invalid address: {}", address));
        }
        
        match self.stations.remove(&address) {
            Some(_) => {
                self.address_map[address as usize] = false;
                Ok(())
            }
            None => Err(format!("Address {} not found", address)),
        }
    }
    
    /// Get station information
    fn get_station(&self, address: u8) -> Option<&StationInfo> {
        self.stations.get(&address)
    }
    
    /// Find next available address starting from given address
    fn find_next_available(&self, start_address: u8) -> Option<u8> {
        (start_address..=PROFIBUS_MAX_ADDRESS)
            .find(|&addr| self.is_address_available(addr))
    }
    
    /// Get count of stations by type
    fn count_by_type(&self, station_type: StationType) -> usize {
        self.stations
            .values()
            .filter(|s| s.station_type == station_type)
            .count()
    }
    
    /// Get all active addresses
    fn get_active_addresses(&self) -> Vec<u8> {
        let mut addresses: Vec<u8> = self.stations.keys().copied().collect();
        addresses.sort_unstable();
        addresses
    }
    
    /// Validate entire address configuration
    fn validate_configuration(&self) -> Result<(), Vec<String>> {
        let mut errors = Vec::new();
        
        // Check for masters
        if self.count_by_type(StationType::MasterClass1) == 0 {
            errors.push("No active master configured".to_string());
        }
        
        // Check address gaps for token ring efficiency
        let addresses = self.get_active_addresses();
        if let Some(max_addr) = addresses.last() {
            let gap_ratio = (*max_addr as f32) / (addresses.len() as f32);
            if gap_ratio > 3.0 {
                errors.push(format!(
                    "Large address gaps detected (ratio: {:.2}). Consider reassigning addresses.",
                    gap_ratio
                ));
            }
        }
        
        if errors.is_empty() {
            Ok(())
        } else {
            Err(errors)
        }
    }
    
    /// Generate address assignment report
    fn generate_report(&self) -> String {
        let masters = self.count_by_type(StationType::MasterClass1) +
                     self.count_by_type(StationType::MasterClass2);
        let slaves = self.count_by_type(StationType::Slave);
        
        format!(
            "Profibus Address Assignment Report\n\
             ===================================\n\
             Total Stations: {}\n\
             Masters: {}\n\
             Slaves: {}\n\
             Available Addresses: {}\n\
             Active Addresses: {:?}",
            self.stations.len(),
            masters,
            slaves,
            PROFIBUS_MAX_ADDRESS - self.stations.len() as u8,
            self.get_active_addresses()
        )
    }
}

fn main() {
    let mut manager = AddressManager::new();
    
    // Assign addresses
    manager.assign_address(2, StationType::MasterClass1, "PLC_Master".to_string())
        .expect("Failed to assign address 2");
    
    manager.assign_address(5, StationType::Slave, "Temperature_Sensor".to_string())
        .expect("Failed to assign address 5");
    
    manager.assign_address(10, StationType::Slave, "Valve_Controller".to_string())
        .expect("Failed to assign address 10");
    
    manager.assign_address(15, StationType::Slave, "Motor_Drive".to_string())
        .expect("Failed to assign address 15");
    
    // Print report
    println!("{}", manager.generate_report());
    
    // Find next available address
    if let Some(next_addr) = manager.find_next_available(1) {
        println!("\nNext available address: {}", next_addr);
    }
    
    // Validate configuration
    match manager.validate_configuration() {
        Ok(_) => println!("\n✓ Configuration valid"),
        Err(errors) => {
            println!("\n✗ Configuration errors:");
            for error in errors {
                println!("  - {}", error);
            }
        }
    }
}
```

## Summary

**Station Address Management** in Profibus is the systematic approach to assigning and managing unique identifiers (1-126) for devices on the network. Key points include:

- **Fixed Address Range:** 126 usable addresses (1-126), with 0 reserved for broadcast
- **Unique Assignment:** Each device requires a unique address to prevent conflicts
- **Static Configuration:** Addresses are typically assigned during commissioning and remain fixed
- **Master/Slave Distinction:** Both masters and slaves use the same address space but have different communication roles
- **Network Efficiency:** Compact address assignment (minimizing gaps) improves token rotation time
- **Conflict Prevention:** Pre-deployment scanning and validation tools prevent address collisions

Proper address management ensures deterministic communication, efficient token passing, and reliable network operation. The code examples demonstrate address validation, assignment, conflict detection, and configuration management essential for robust Profibus implementations.