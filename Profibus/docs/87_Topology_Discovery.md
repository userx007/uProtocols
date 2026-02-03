# Profibus Topology Discovery

## Detailed Description

Topology Discovery in Profibus networks refers to the automated process of identifying and mapping the physical and logical structure of the network, including all connected devices, their addresses, locations, and interconnections. This capability is essential for network commissioning, maintenance, diagnostics, and documentation.

### Key Concepts

**What Topology Discovery Accomplishes:**
- **Device Enumeration**: Identifies all active stations on the bus
- **Physical Location**: Determines the position of devices along the network segment
- **Cable Length Calculation**: Estimates distances between devices using signal propagation analysis
- **Segment Identification**: Maps out different network segments and repeaters
- **Device Capabilities**: Queries device types, manufacturers, and supported features
- **Network Health**: Assesses signal quality and potential issues

**Methods for Topology Discovery:**

1. **Active Scanning**: Systematically querying each possible station address (0-126)
2. **Passive Monitoring**: Listening to network traffic to identify active participants
3. **Signal Analysis**: Using physical layer measurements (propagation delays, reflections)
4. **GSD File Correlation**: Matching discovered devices with their configuration files
5. **DP-V1/V2 Services**: Using diagnostic and identification services

**Profibus-Specific Challenges:**
- RS-485 physical layer provides limited diagnostic information compared to Ethernet
- No built-in topology protocol like LLDP in Ethernet networks
- Requires cooperation from all devices or specialized diagnostic tools
- Baudrate-dependent propagation delays affect distance calculations

### Architecture Components

```
┌─────────────────────────────────────────────────┐
│         Topology Discovery Engine               │
├─────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌───────────────────────┐    │
│  │   Scanner    │  │  Signal Analyzer      │    │
│  │   Module     │  │  (TDR/Reflectometry)  │    │
│  └──────────────┘  └───────────────────────┘    │
│  ┌──────────────┐  ┌───────────────────────┐    │
│  │  Device ID   │  │   Topology Builder    │    │
│  │  Database    │  │   (Graph Construction)│    │
│  └──────────────┘  └───────────────────────┘    │
└─────────────────────────────────────────────────┘
                      │
        ┌─────────────┼─────────────┐
        ▼             ▼             ▼
   ┌────────┐    ┌────────┐    ┌────────┐
   │Device 1│    │Device 2│    │Device N│
   │Addr: 3 │    │Addr: 7 │    │Addr: 15│
   └────────┘    └────────┘    └────────┘
```

---

## C/C++ Implementation

### Complete Topology Discovery System

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Profibus address range
#define PROFIBUS_MIN_ADDRESS 0
#define PROFIBUS_MAX_ADDRESS 126
#define PROFIBUS_BROADCAST_ADDRESS 127

// Device identification
#define IDENT_NUMBER_SIZE 4
#define MANUFACTURER_NAME_SIZE 32
#define DEVICE_NAME_SIZE 32

// Topology discovery status
typedef enum {
    TOPOLOGY_STATUS_UNKNOWN = 0,
    TOPOLOGY_STATUS_DISCOVERING,
    TOPOLOGY_STATUS_COMPLETE,
    TOPOLOGY_STATUS_ERROR
} TopologyStatus;

// Device information structure
typedef struct {
    uint8_t address;
    bool is_active;
    uint32_t ident_number;
    uint16_t vendor_id;
    char manufacturer[MANUFACTURER_NAME_SIZE];
    char device_name[DEVICE_NAME_SIZE];
    uint8_t device_type;
    double estimated_distance_m;  // Distance from master in meters
    uint32_t response_time_us;    // Response time in microseconds
    uint8_t signal_quality;       // 0-100%
    time_t last_seen;
} ProfibusDevice;

// Network segment structure
typedef struct NetworkSegment {
    uint8_t segment_id;
    uint8_t device_count;
    ProfibusDevice* devices[PROFIBUS_MAX_ADDRESS];
    double total_length_m;
    struct NetworkSegment* next;
} NetworkSegment;

// Topology map structure
typedef struct {
    TopologyStatus status;
    uint8_t total_devices;
    uint8_t active_devices;
    ProfibusDevice devices[PROFIBUS_MAX_ADDRESS + 1];
    NetworkSegment* segments;
    double total_network_length_m;
    time_t discovery_timestamp;
} TopologyMap;

// Simulated hardware interface
typedef struct {
    int fd;  // File descriptor for hardware interface
    uint32_t baudrate;
} ProfibusInterface;

// Function prototypes
TopologyMap* topology_create_map(void);
void topology_destroy_map(TopologyMap* map);
int topology_discover_network(ProfibusInterface* interface, TopologyMap* map);
int topology_scan_address(ProfibusInterface* interface, uint8_t address, 
                          ProfibusDevice* device);
int topology_read_device_ident(ProfibusInterface* interface, uint8_t address,
                                ProfibusDevice* device);
double topology_estimate_distance(uint32_t response_time_us, uint32_t baudrate);
void topology_analyze_segments(TopologyMap* map);
void topology_print_map(TopologyMap* map);
void topology_export_json(TopologyMap* map, const char* filename);

// Create a new topology map
TopologyMap* topology_create_map(void) {
    TopologyMap* map = (TopologyMap*)calloc(1, sizeof(TopologyMap));
    if (!map) {
        return NULL;
    }
    
    map->status = TOPOLOGY_STATUS_UNKNOWN;
    map->total_devices = 0;
    map->active_devices = 0;
    map->segments = NULL;
    map->total_network_length_m = 0.0;
    map->discovery_timestamp = time(NULL);
    
    // Initialize all devices as inactive
    for (int i = 0; i <= PROFIBUS_MAX_ADDRESS; i++) {
        map->devices[i].address = i;
        map->devices[i].is_active = false;
    }
    
    return map;
}

// Destroy topology map
void topology_destroy_map(TopologyMap* map) {
    if (!map) return;
    
    // Free segments
    NetworkSegment* segment = map->segments;
    while (segment) {
        NetworkSegment* next = segment->next;
        free(segment);
        segment = next;
    }
    
    free(map);
}

// Simulate reading device identification
int topology_read_device_ident(ProfibusInterface* interface, uint8_t address,
                                ProfibusDevice* device) {
    // In real implementation, this would send DP-V1 Read service
    // Request for identification data (manufacturer, device type, etc.)
    
    // Simulated identification data
    device->ident_number = 0x12340000 | address;
    device->vendor_id = 0x00AB;
    snprintf(device->manufacturer, MANUFACTURER_NAME_SIZE, "Vendor_%02X", address % 10);
    snprintf(device->device_name, DEVICE_NAME_SIZE, "Device_Type_%d", address % 5);
    device->device_type = address % 3; // 0=Master, 1=Slave, 2=Repeater
    
    return 0;  // Success
}

// Estimate distance based on response time
double topology_estimate_distance(uint32_t response_time_us, uint32_t baudrate) {
    // Profibus signal propagation speed: approximately 2/3 speed of light
    // Speed of light: 300,000 km/s = 300 m/us
    // Profibus: ~200 m/us
    const double SIGNAL_SPEED_M_PER_US = 200.0;
    
    // Calculate time for signal to travel (round trip, so divide by 2)
    // Subtract protocol overhead (bit times, processing delays)
    double bit_time_us = 1000000.0 / baudrate;
    double overhead_us = bit_time_us * 20;  // Approximate overhead
    
    double travel_time_us = response_time_us - overhead_us;
    if (travel_time_us < 0) travel_time_us = 0;
    
    double distance = (travel_time_us / 2.0) * SIGNAL_SPEED_M_PER_US;
    
    // Apply practical limits (0-1000m typical for Profibus)
    if (distance < 0) distance = 0;
    if (distance > 1000) distance = 1000;
    
    return distance;
}

// Scan a specific address
int topology_scan_address(ProfibusInterface* interface, uint8_t address, 
                          ProfibusDevice* device) {
    if (address > PROFIBUS_MAX_ADDRESS) {
        return -1;
    }
    
    // Simulate sending a request and measuring response time
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // In real implementation: Send FDL_STATUS or diagnostic request
    // Simulated: Random chance of device being present
    bool device_responds = (address % 3 != 0) && (address < 20);
    
    if (!device_responds) {
        device->is_active = false;
        return 1;  // No device at this address
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate response time
    uint32_t response_time_us = 
        (end.tv_sec - start.tv_sec) * 1000000 + 
        (end.tv_nsec - start.tv_nsec) / 1000;
    
    // Simulate response time (10-500 microseconds)
    response_time_us = 50 + (address * 10);
    
    // Fill device information
    device->address = address;
    device->is_active = true;
    device->response_time_us = response_time_us;
    device->estimated_distance_m = topology_estimate_distance(
        response_time_us, interface->baudrate);
    device->signal_quality = 85 + (address % 15);  // Simulated
    device->last_seen = time(NULL);
    
    // Read device identification
    topology_read_device_ident(interface, address, device);
    
    return 0;  // Success
}

// Discover all devices on the network
int topology_discover_network(ProfibusInterface* interface, TopologyMap* map) {
    printf("Starting topology discovery...\n");
    map->status = TOPOLOGY_STATUS_DISCOVERING;
    
    uint8_t active_count = 0;
    
    // Scan all possible addresses
    for (uint8_t addr = PROFIBUS_MIN_ADDRESS; addr <= PROFIBUS_MAX_ADDRESS; addr++) {
        ProfibusDevice* device = &map->devices[addr];
        
        int result = topology_scan_address(interface, addr, device);
        
        if (result == 0 && device->is_active) {
            active_count++;
            printf("  Found device at address %d: %s\n", 
                   addr, device->device_name);
        }
    }
    
    map->total_devices = PROFIBUS_MAX_ADDRESS + 1;
    map->active_devices = active_count;
    map->status = TOPOLOGY_STATUS_COMPLETE;
    map->discovery_timestamp = time(NULL);
    
    printf("Discovery complete: %d active devices found\n", active_count);
    
    return 0;
}

// Analyze and segment the network
void topology_analyze_segments(TopologyMap* map) {
    // Simple segmentation: group devices by distance ranges
    // Real implementation would analyze signal propagation, repeaters, etc.
    
    const double SEGMENT_THRESHOLD_M = 200.0;  // Max 200m per segment
    
    NetworkSegment* current_segment = NULL;
    double segment_start_distance = 0.0;
    
    for (uint8_t addr = 0; addr <= PROFIBUS_MAX_ADDRESS; addr++) {
        ProfibusDevice* device = &map->devices[addr];
        
        if (!device->is_active) continue;
        
        // Check if we need a new segment
        if (!current_segment || 
            (device->estimated_distance_m - segment_start_distance) > SEGMENT_THRESHOLD_M) {
            
            NetworkSegment* new_segment = (NetworkSegment*)calloc(1, sizeof(NetworkSegment));
            new_segment->segment_id = (current_segment ? current_segment->segment_id + 1 : 0);
            new_segment->device_count = 0;
            new_segment->next = NULL;
            
            if (!map->segments) {
                map->segments = new_segment;
            } else {
                current_segment->next = new_segment;
            }
            
            current_segment = new_segment;
            segment_start_distance = device->estimated_distance_m;
        }
        
        // Add device to current segment
        current_segment->devices[current_segment->device_count++] = device;
        current_segment->total_length_m = 
            device->estimated_distance_m - segment_start_distance;
    }
}

// Print topology map
void topology_print_map(TopologyMap* map) {
    printf("\n========== PROFIBUS TOPOLOGY MAP ==========\n");
    printf("Discovery Time: %s", ctime(&map->discovery_timestamp));
    printf("Status: %s\n", 
           map->status == TOPOLOGY_STATUS_COMPLETE ? "Complete" : "Unknown");
    printf("Active Devices: %d / %d\n", map->active_devices, map->total_devices);
    printf("==========================================\n\n");
    
    printf("%-5s %-15s %-20s %-12s %-12s %-8s\n",
           "Addr", "Manufacturer", "Device Name", "Distance(m)", "Response(us)", "Quality");
    printf("----------------------------------------------------------------------\n");
    
    for (uint8_t addr = 0; addr <= PROFIBUS_MAX_ADDRESS; addr++) {
        ProfibusDevice* device = &map->devices[addr];
        
        if (!device->is_active) continue;
        
        printf("%-5d %-15s %-20s %-12.2f %-12u %-7d%%\n",
               device->address,
               device->manufacturer,
               device->device_name,
               device->estimated_distance_m,
               device->response_time_us,
               device->signal_quality);
    }
    
    printf("\n========== NETWORK SEGMENTS ==========\n");
    NetworkSegment* segment = map->segments;
    while (segment) {
        printf("Segment %d: %d devices, %.2f meters\n",
               segment->segment_id,
               segment->device_count,
               segment->total_length_m);
        segment = segment->next;
    }
}

// Export topology to JSON
void topology_export_json(TopologyMap* map, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"discovery_time\": %ld,\n", map->discovery_timestamp);
    fprintf(fp, "  \"active_devices\": %d,\n", map->active_devices);
    fprintf(fp, "  \"devices\": [\n");
    
    bool first = true;
    for (uint8_t addr = 0; addr <= PROFIBUS_MAX_ADDRESS; addr++) {
        ProfibusDevice* device = &map->devices[addr];
        
        if (!device->is_active) continue;
        
        if (!first) fprintf(fp, ",\n");
        first = false;
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"address\": %d,\n", device->address);
        fprintf(fp, "      \"manufacturer\": \"%s\",\n", device->manufacturer);
        fprintf(fp, "      \"device_name\": \"%s\",\n", device->device_name);
        fprintf(fp, "      \"distance_m\": %.2f,\n", device->estimated_distance_m);
        fprintf(fp, "      \"response_time_us\": %u,\n", device->response_time_us);
        fprintf(fp, "      \"signal_quality\": %d\n", device->signal_quality);
        fprintf(fp, "    }");
    }
    
    fprintf(fp, "\n  ]\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    printf("Topology exported to: %s\n", filename);
}

// Example usage
int main(void) {
    // Initialize simulated Profibus interface
    ProfibusInterface interface = {
        .fd = 0,
        .baudrate = 1500000  // 1.5 Mbaud
    };
    
    // Create topology map
    TopologyMap* map = topology_create_map();
    if (!map) {
        fprintf(stderr, "Failed to create topology map\n");
        return 1;
    }
    
    // Discover network topology
    topology_discover_network(&interface, map);
    
    // Analyze segments
    topology_analyze_segments(map);
    
    // Print results
    topology_print_map(map);
    
    // Export to JSON
    topology_export_json(map, "profibus_topology.json");
    
    // Cleanup
    topology_destroy_map(map);
    
    return 0;
}
```

---

## Rust Implementation

### Modern, Safe Topology Discovery

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant, SystemTime};
use serde::{Serialize, Deserialize};
use std::fs::File;
use std::io::Write;

// Profibus address constants
const PROFIBUS_MIN_ADDRESS: u8 = 0;
const PROFIBUS_MAX_ADDRESS: u8 = 126;
const PROFIBUS_BROADCAST_ADDRESS: u8 = 127;

// Signal propagation speed (meters per microsecond)
const SIGNAL_SPEED_M_PER_US: f64 = 200.0;

/// Device type enumeration
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum DeviceType {
    Master,
    Slave,
    Repeater,
    Unknown,
}

/// Topology discovery status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum TopologyStatus {
    Unknown,
    Discovering,
    Complete,
    Error,
}

/// Profibus device information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProfibusDevice {
    pub address: u8,
    pub is_active: bool,
    pub ident_number: u32,
    pub vendor_id: u16,
    pub manufacturer: String,
    pub device_name: String,
    pub device_type: DeviceType,
    pub estimated_distance_m: f64,
    pub response_time_us: u32,
    pub signal_quality: u8,
    pub last_seen: SystemTime,
}

impl ProfibusDevice {
    pub fn new(address: u8) -> Self {
        Self {
            address,
            is_active: false,
            ident_number: 0,
            vendor_id: 0,
            manufacturer: String::new(),
            device_name: String::new(),
            device_type: DeviceType::Unknown,
            estimated_distance_m: 0.0,
            response_time_us: 0,
            signal_quality: 0,
            last_seen: SystemTime::now(),
        }
    }
}

/// Network segment representation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NetworkSegment {
    pub segment_id: u8,
    pub devices: Vec<u8>,  // Device addresses
    pub total_length_m: f64,
    pub start_distance_m: f64,
}

/// Complete topology map
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TopologyMap {
    pub status: TopologyStatus,
    pub devices: HashMap<u8, ProfibusDevice>,
    pub segments: Vec<NetworkSegment>,
    pub total_network_length_m: f64,
    pub discovery_timestamp: SystemTime,
}

impl TopologyMap {
    pub fn new() -> Self {
        Self {
            status: TopologyStatus::Unknown,
            devices: HashMap::new(),
            segments: Vec::new(),
            total_network_length_m: 0.0,
            discovery_timestamp: SystemTime::now(),
        }
    }
    
    pub fn active_devices(&self) -> usize {
        self.devices.values().filter(|d| d.is_active).count()
    }
    
    pub fn total_devices(&self) -> usize {
        self.devices.len()
    }
}

/// Profibus interface abstraction
pub struct ProfibusInterface {
    baudrate: u32,
}

impl ProfibusInterface {
    pub fn new(baudrate: u32) -> Self {
        Self { baudrate }
    }
    
    /// Scan a specific address for device presence
    pub fn scan_address(&self, address: u8) -> Result<Option<ProfibusDevice>, String> {
        if address > PROFIBUS_MAX_ADDRESS {
            return Err(format!("Invalid address: {}", address));
        }
        
        let start = Instant::now();
        
        // Simulate device detection
        // In real implementation: send FDL_STATUS or diagnostic request
        let device_responds = (address % 3 != 0) && (address < 20);
        
        if !device_responds {
            return Ok(None);
        }
        
        let response_time = start.elapsed();
        
        // Simulate response time (50-500 microseconds)
        let response_time_us = 50 + (address as u32 * 10);
        
        let mut device = ProfibusDevice::new(address);
        device.is_active = true;
        device.response_time_us = response_time_us;
        device.estimated_distance_m = self.estimate_distance(response_time_us);
        device.signal_quality = 85 + (address % 15);
        device.last_seen = SystemTime::now();
        
        // Read device identification
        self.read_device_ident(&mut device)?;
        
        Ok(Some(device))
    }
    
    /// Read device identification information
    fn read_device_ident(&self, device: &mut ProfibusDevice) -> Result<(), String> {
        // In real implementation: send DP-V1 Read service
        // Simulate identification data
        device.ident_number = 0x12340000 | (device.address as u32);
        device.vendor_id = 0x00AB;
        device.manufacturer = format!("Vendor_{:02}", device.address % 10);
        device.device_name = format!("Device_Type_{}", device.address % 5);
        device.device_type = match device.address % 3 {
            0 => DeviceType::Master,
            1 => DeviceType::Slave,
            _ => DeviceType::Repeater,
        };
        
        Ok(())
    }
    
    /// Estimate distance based on response time
    fn estimate_distance(&self, response_time_us: u32) -> f64 {
        let bit_time_us = 1_000_000.0 / self.baudrate as f64;
        let overhead_us = bit_time_us * 20.0;
        
        let travel_time_us = (response_time_us as f64 - overhead_us).max(0.0);
        let distance = (travel_time_us / 2.0) * SIGNAL_SPEED_M_PER_US;
        
        // Apply practical limits (0-1000m)
        distance.max(0.0).min(1000.0)
    }
}

/// Topology discovery engine
pub struct TopologyDiscovery {
    interface: ProfibusInterface,
}

impl TopologyDiscovery {
    pub fn new(baudrate: u32) -> Self {
        Self {
            interface: ProfibusInterface::new(baudrate),
        }
    }
    
    /// Discover all devices on the network
    pub fn discover_network(&self) -> Result<TopologyMap, String> {
        println!("Starting topology discovery...");
        
        let mut map = TopologyMap::new();
        map.status = TopologyStatus::Discovering;
        
        // Scan all possible addresses
        for address in PROFIBUS_MIN_ADDRESS..=PROFIBUS_MAX_ADDRESS {
            match self.interface.scan_address(address) {
                Ok(Some(device)) => {
                    println!("  Found device at address {}: {}", 
                             address, device.device_name);
                    map.devices.insert(address, device);
                }
                Ok(None) => {
                    // No device at this address
                }
                Err(e) => {
                    eprintln!("Error scanning address {}: {}", address, e);
                }
            }
        }
        
        map.status = TopologyStatus::Complete;
        map.discovery_timestamp = SystemTime::now();
        
        println!("Discovery complete: {} active devices found", map.active_devices());
        
        Ok(map)
    }
    
    /// Analyze and segment the network
    pub fn analyze_segments(&self, map: &mut TopologyMap) {
        const SEGMENT_THRESHOLD_M: f64 = 200.0;
        
        // Get sorted list of active devices by distance
        let mut sorted_devices: Vec<_> = map.devices.values()
            .filter(|d| d.is_active)
            .collect();
        sorted_devices.sort_by(|a, b| 
            a.estimated_distance_m.partial_cmp(&b.estimated_distance_m).unwrap()
        );
        
        let mut segments = Vec::new();
        let mut current_segment: Option<NetworkSegment> = None;
        let mut segment_start_distance = 0.0;
        
        for device in sorted_devices {
            let needs_new_segment = current_segment.is_none() || 
                (device.estimated_distance_m - segment_start_distance) > SEGMENT_THRESHOLD_M;
            
            if needs_new_segment {
                if let Some(seg) = current_segment.take() {
                    segments.push(seg);
                }
                
                current_segment = Some(NetworkSegment {
                    segment_id: segments.len() as u8,
                    devices: Vec::new(),
                    total_length_m: 0.0,
                    start_distance_m: device.estimated_distance_m,
                });
                segment_start_distance = device.estimated_distance_m;
            }
            
            if let Some(ref mut segment) = current_segment {
                segment.devices.push(device.address);
                segment.total_length_m = device.estimated_distance_m - segment_start_distance;
            }
        }
        
        if let Some(seg) = current_segment {
            segments.push(seg);
        }
        
        map.segments = segments;
    }
    
    /// Print topology map
    pub fn print_map(&self, map: &TopologyMap) {
        println!("\n========== PROFIBUS TOPOLOGY MAP ==========");
        println!("Discovery Time: {:?}", map.discovery_timestamp);
        println!("Status: {:?}", map.status);
        println!("Active Devices: {} / {}", map.active_devices(), map.total_devices());
        println!("==========================================\n");
        
        println!("{:<5} {:<15} {:<20} {:<12} {:<12} {:<8}",
                 "Addr", "Manufacturer", "Device Name", "Distance(m)", "Response(us)", "Quality");
        println!("----------------------------------------------------------------------");
        
        let mut sorted_addresses: Vec<_> = map.devices.keys().collect();
        sorted_addresses.sort();
        
        for address in sorted_addresses {
            if let Some(device) = map.devices.get(address) {
                if !device.is_active {
                    continue;
                }
                
                println!("{:<5} {:<15} {:<20} {:<12.2} {:<12} {:<7}%",
                         device.address,
                         device.manufacturer,
                         device.device_name,
                         device.estimated_distance_m,
                         device.response_time_us,
                         device.signal_quality);
            }
        }
        
        println!("\n========== NETWORK SEGMENTS ==========");
        for segment in &map.segments {
            println!("Segment {}: {} devices, {:.2} meters",
                     segment.segment_id,
                     segment.devices.len(),
                     segment.total_length_m);
        }
    }
    
    /// Export topology to JSON
    pub fn export_json(&self, map: &TopologyMap, filename: &str) -> Result<(), String> {
        let json = serde_json::to_string_pretty(map)
            .map_err(|e| format!("JSON serialization error: {}", e))?;
        
        let mut file = File::create(filename)
            .map_err(|e| format!("Failed to create file: {}", e))?;
        
        file.write_all(json.as_bytes())
            .map_err(|e| format!("Failed to write file: {}", e))?;
        
        println!("Topology exported to: {}", filename);
        Ok(())
    }
}

// Example usage
fn main() {
    // Create topology discovery engine
    let discovery = TopologyDiscovery::new(1_500_000); // 1.5 Mbaud
    
    // Discover network
    let mut map = match discovery.discover_network() {
        Ok(map) => map,
        Err(e) => {
            eprintln!("Discovery failed: {}", e);
            return;
        }
    };
    
    // Analyze segments
    discovery.analyze_segments(&mut map);
    
    // Print results
    discovery.print_map(&map);
    
    // Export to JSON
    if let Err(e) = discovery.export_json(&map, "profibus_topology.json") {
        eprintln!("Export failed: {}", e);
    }
}
```

---

## Summary

**Profibus Topology Discovery** is the automated process of identifying and mapping all devices, their locations, and interconnections in a Profibus network. This is essential for network commissioning, maintenance, and diagnostics.

**Key Features:**
- **Automated Device Detection**: Scans all possible station addresses to identify active devices
- **Distance Estimation**: Uses signal propagation analysis to estimate physical device locations
- **Device Identification**: Retrieves manufacturer, device type, and capability information
- **Network Segmentation**: Analyzes and maps logical network segments
- **Diagnostics Support**: Provides signal quality and response time metrics

**Implementation Approaches:**
- **Active Scanning**: Systematically queries each address using FDL_STATUS or diagnostic services
- **Signal Analysis**: Measures response times to calculate distances based on propagation delays
- **Device Interrogation**: Uses DP-V1/V2 identification services to read device details

**Practical Applications:**
- Automatic network documentation and visualization
- Troubleshooting network issues and locating faults
- Verifying physical installation against design specifications
- Monitoring network health and performance over time
- Planning network expansions and modifications

Both C/C++ and Rust implementations demonstrate complete topology discovery systems with device scanning, distance calculation, segmentation analysis, and JSON export capabilities for integration with visualization and management tools.