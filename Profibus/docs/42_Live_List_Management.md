# Live List Management in Profibus

## Detailed Description

Live List Management is a critical diagnostic and monitoring feature in Profibus networks that tracks which devices (slaves/participants) are currently active and communicating on the bus. The live list is a dynamic data structure maintained by the master device that reflects the real-time operational status of all configured slaves.

### Purpose and Functionality

The live list serves several essential purposes:

1. **Network Topology Awareness**: Provides a real-time view of which devices are present and responding on the network
2. **Fault Detection**: Quickly identifies when devices drop offline or fail to respond
3. **Diagnostic Information**: Helps maintenance personnel troubleshoot communication issues
4. **Redundancy Management**: In systems with redundant masters, helps coordinate failover operations
5. **Startup Optimization**: Allows masters to detect available slaves during initialization

### Key Concepts

**Live List Structure**: Typically implemented as a bitmap or array where each bit/element represents one configured slave station. A set bit indicates the slave is active and responding; a cleared bit indicates the slave is offline or not responding.

**Update Mechanism**: The master updates the live list based on:
- Successful cyclic data exchanges
- Token rotation monitoring (in multi-master systems)
- Diagnostic read responses
- Timeout events when slaves fail to respond

**Station States**: Each slave can be in one of several states:
- **Active**: Device is responding normally to all requests
- **Inactive**: Device was configured but is not responding
- **Not Configured**: Station address has no device configured
- **Faulty**: Device responds but reports internal errors

### Implementation Considerations

- **Update Frequency**: The live list should be updated after each communication cycle or when significant events occur
- **Persistence**: Some implementations maintain historical data showing when devices went offline
- **Thread Safety**: In multi-threaded applications, the live list requires proper synchronization
- **Notification Mechanisms**: Applications often implement callbacks or events when the live list changes

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// Maximum number of Profibus stations (0-127)
#define MAX_PROFIBUS_STATIONS 128

// Station state enumeration
typedef enum {
    STATION_NOT_CONFIGURED = 0,
    STATION_ACTIVE = 1,
    STATION_INACTIVE = 2,
    STATION_FAULTY = 3
} StationState;

// Information about each station
typedef struct {
    uint8_t address;
    StationState state;
    time_t last_response_time;
    uint32_t consecutive_failures;
    uint32_t total_failures;
    uint32_t total_successes;
} StationInfo;

// Live list structure
typedef struct {
    StationInfo stations[MAX_PROFIBUS_STATIONS];
    uint8_t active_count;
    uint8_t configured_count;
    time_t last_update_time;
    void (*on_state_change)(uint8_t address, StationState old_state, StationState new_state);
} LiveList;

// Initialize the live list
void live_list_init(LiveList* list) {
    memset(list, 0, sizeof(LiveList));
    
    for (int i = 0; i < MAX_PROFIBUS_STATIONS; i++) {
        list->stations[i].address = i;
        list->stations[i].state = STATION_NOT_CONFIGURED;
    }
    
    list->last_update_time = time(NULL);
}

// Configure a station as expected participant
void live_list_add_station(LiveList* list, uint8_t address) {
    if (address >= MAX_PROFIBUS_STATIONS) return;
    
    if (list->stations[address].state == STATION_NOT_CONFIGURED) {
        list->stations[address].state = STATION_INACTIVE;
        list->configured_count++;
    }
}

// Update station status after communication attempt
void live_list_update_station(LiveList* list, uint8_t address, bool success) {
    if (address >= MAX_PROFIBUS_STATIONS) return;
    
    StationInfo* station = &list->stations[address];
    StationState old_state = station->state;
    
    if (station->state == STATION_NOT_CONFIGURED) {
        return; // Don't update unconfigured stations
    }
    
    time_t now = time(NULL);
    
    if (success) {
        station->last_response_time = now;
        station->consecutive_failures = 0;
        station->total_successes++;
        
        if (station->state != STATION_ACTIVE) {
            station->state = STATION_ACTIVE;
            list->active_count++;
        }
    } else {
        station->consecutive_failures++;
        station->total_failures++;
        
        // Mark as inactive after 3 consecutive failures
        if (station->consecutive_failures >= 3 && station->state == STATION_ACTIVE) {
            station->state = STATION_INACTIVE;
            list->active_count--;
        }
    }
    
    list->last_update_time = now;
    
    // Notify callback if state changed
    if (old_state != station->state && list->on_state_change) {
        list->on_state_change(address, old_state, station->state);
    }
}

// Get current live list as bitmap (for compatibility with some protocols)
void live_list_get_bitmap(LiveList* list, uint8_t bitmap[16]) {
    memset(bitmap, 0, 16);
    
    for (int i = 0; i < MAX_PROFIBUS_STATIONS; i++) {
        if (list->stations[i].state == STATION_ACTIVE) {
            bitmap[i / 8] |= (1 << (i % 8));
        }
    }
}

// Check if a specific station is active
bool live_list_is_active(LiveList* list, uint8_t address) {
    if (address >= MAX_PROFIBUS_STATIONS) return false;
    return list->stations[address].state == STATION_ACTIVE;
}

// Get list of all active stations
int live_list_get_active_stations(LiveList* list, uint8_t* addresses, int max_count) {
    int count = 0;
    
    for (int i = 0; i < MAX_PROFIBUS_STATIONS && count < max_count; i++) {
        if (list->stations[i].state == STATION_ACTIVE) {
            addresses[count++] = i;
        }
    }
    
    return count;
}

// Print live list status
void live_list_print(LiveList* list) {
    printf("Profibus Live List Status:\n");
    printf("Configured Stations: %d\n", list->configured_count);
    printf("Active Stations: %d\n", list->active_count);
    printf("\nStation Details:\n");
    
    for (int i = 0; i < MAX_PROFIBUS_STATIONS; i++) {
        StationInfo* station = &list->stations[i];
        
        if (station->state != STATION_NOT_CONFIGURED) {
            const char* state_str;
            switch (station->state) {
                case STATION_ACTIVE: state_str = "ACTIVE"; break;
                case STATION_INACTIVE: state_str = "INACTIVE"; break;
                case STATION_FAULTY: state_str = "FAULTY"; break;
                default: state_str = "UNKNOWN"; break;
            }
            
            printf("  Station %3d: %-10s | Success: %5u | Failures: %5u\n",
                   i, state_str, station->total_successes, station->total_failures);
        }
    }
}

// Example callback function
void on_station_state_changed(uint8_t address, StationState old_state, StationState new_state) {
    printf("Station %d changed: ", address);
    
    if (new_state == STATION_ACTIVE) {
        printf("NOW ONLINE\n");
    } else if (old_state == STATION_ACTIVE) {
        printf("WENT OFFLINE\n");
    }
}

// Example usage
int main() {
    LiveList list;
    
    // Initialize live list
    live_list_init(&list);
    list.on_state_change = on_station_state_changed;
    
    // Configure expected stations
    live_list_add_station(&list, 2);
    live_list_add_station(&list, 5);
    live_list_add_station(&list, 10);
    
    // Simulate communication cycles
    printf("Simulating communication...\n\n");
    
    live_list_update_station(&list, 2, true);   // Station 2 responds
    live_list_update_station(&list, 5, true);   // Station 5 responds
    live_list_update_station(&list, 10, false); // Station 10 fails
    live_list_update_station(&list, 10, false); // Station 10 fails again
    live_list_update_station(&list, 10, false); // Station 10 third failure
    
    printf("\n");
    live_list_print(&list);
    
    // Get bitmap representation
    uint8_t bitmap[16];
    live_list_get_bitmap(&list, bitmap);
    
    printf("\nLive list bitmap (hex): ");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", bitmap[i]);
    }
    printf("\n");
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

// Maximum number of Profibus stations
const MAX_PROFIBUS_STATIONS: usize = 128;

// Station state enumeration
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StationState {
    NotConfigured,
    Active,
    Inactive,
    Faulty,
}

// Information about each station
#[derive(Debug, Clone)]
pub struct StationInfo {
    pub address: u8,
    pub state: StationState,
    pub last_response_time: u64,
    pub consecutive_failures: u32,
    pub total_failures: u32,
    pub total_successes: u32,
}

impl StationInfo {
    fn new(address: u8) -> Self {
        Self {
            address,
            state: StationState::NotConfigured,
            last_response_time: 0,
            consecutive_failures: 0,
            total_failures: 0,
            total_successes: 0,
        }
    }
}

// Type for state change callback
pub type StateChangeCallback = Box<dyn Fn(u8, StationState, StationState) + Send>;

// Live list structure
pub struct LiveList {
    stations: HashMap<u8, StationInfo>,
    active_count: usize,
    configured_count: usize,
    last_update_time: u64,
    on_state_change: Option<StateChangeCallback>,
}

impl LiveList {
    /// Create a new live list
    pub fn new() -> Self {
        Self {
            stations: HashMap::new(),
            active_count: 0,
            configured_count: 0,
            last_update_time: Self::current_timestamp(),
            on_state_change: None,
        }
    }

    /// Set the state change callback
    pub fn set_callback(&mut self, callback: StateChangeCallback) {
        self.on_state_change = Some(callback);
    }

    /// Get current timestamp
    fn current_timestamp() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }

    /// Configure a station as an expected participant
    pub fn add_station(&mut self, address: u8) {
        if address >= MAX_PROFIBUS_STATIONS as u8 {
            return;
        }

        self.stations.entry(address).or_insert_with(|| {
            self.configured_count += 1;
            let mut info = StationInfo::new(address);
            info.state = StationState::Inactive;
            info
        });
    }

    /// Remove a station from configuration
    pub fn remove_station(&mut self, address: u8) {
        if let Some(station) = self.stations.remove(&address) {
            if station.state == StationState::Active {
                self.active_count -= 1;
            }
            self.configured_count -= 1;
        }
    }

    /// Update station status after communication attempt
    pub fn update_station(&mut self, address: u8, success: bool) {
        if address >= MAX_PROFIBUS_STATIONS as u8 {
            return;
        }

        let station = match self.stations.get_mut(&address) {
            Some(s) if s.state != StationState::NotConfigured => s,
            _ => return,
        };

        let old_state = station.state;
        let now = Self::current_timestamp();

        if success {
            station.last_response_time = now;
            station.consecutive_failures = 0;
            station.total_successes += 1;

            if station.state != StationState::Active {
                station.state = StationState::Active;
                self.active_count += 1;
            }
        } else {
            station.consecutive_failures += 1;
            station.total_failures += 1;

            // Mark as inactive after 3 consecutive failures
            if station.consecutive_failures >= 3 && station.state == StationState::Active {
                station.state = StationState::Inactive;
                self.active_count -= 1;
            }
        }

        self.last_update_time = now;

        // Trigger callback if state changed
        if old_state != station.state {
            if let Some(ref callback) = self.on_state_change {
                callback(address, old_state, station.state);
            }
        }
    }

    /// Get current live list as bitmap
    pub fn get_bitmap(&self) -> [u8; 16] {
        let mut bitmap = [0u8; 16];

        for (address, station) in &self.stations {
            if station.state == StationState::Active {
                let byte_idx = (*address / 8) as usize;
                let bit_idx = address % 8;
                bitmap[byte_idx] |= 1 << bit_idx;
            }
        }

        bitmap
    }

    /// Check if a specific station is active
    pub fn is_active(&self, address: u8) -> bool {
        self.stations
            .get(&address)
            .map(|s| s.state == StationState::Active)
            .unwrap_or(false)
    }

    /// Get list of all active stations
    pub fn get_active_stations(&self) -> Vec<u8> {
        let mut active: Vec<u8> = self
            .stations
            .values()
            .filter(|s| s.state == StationState::Active)
            .map(|s| s.address)
            .collect();

        active.sort_unstable();
        active
    }

    /// Get station information
    pub fn get_station_info(&self, address: u8) -> Option<&StationInfo> {
        self.stations.get(&address)
    }

    /// Get counts
    pub fn active_count(&self) -> usize {
        self.active_count
    }

    pub fn configured_count(&self) -> usize {
        self.configured_count
    }

    /// Print live list status
    pub fn print_status(&self) {
        println!("Profibus Live List Status:");
        println!("Configured Stations: {}", self.configured_count);
        println!("Active Stations: {}", self.active_count);
        println!("\nStation Details:");

        let mut addresses: Vec<u8> = self.stations.keys().copied().collect();
        addresses.sort_unstable();

        for address in addresses {
            if let Some(station) = self.stations.get(&address) {
                let state_str = match station.state {
                    StationState::Active => "ACTIVE",
                    StationState::Inactive => "INACTIVE",
                    StationState::Faulty => "FAULTY",
                    StationState::NotConfigured => "NOT_CONFIGURED",
                };

                println!(
                    "  Station {:3}: {:<14} | Success: {:5} | Failures: {:5}",
                    address, state_str, station.total_successes, station.total_failures
                );
            }
        }
    }
}

// Example usage
fn main() {
    let mut list = LiveList::new();

    // Set up callback
    list.set_callback(Box::new(|address, old_state, new_state| {
        print!("Station {} changed: ", address);
        
        if new_state == StationState::Active {
            println!("NOW ONLINE");
        } else if old_state == StationState::Active {
            println!("WENT OFFLINE");
        }
    }));

    // Configure expected stations
    list.add_station(2);
    list.add_station(5);
    list.add_station(10);

    // Simulate communication cycles
    println!("Simulating communication...\n");

    list.update_station(2, true);   // Station 2 responds
    list.update_station(5, true);   // Station 5 responds
    list.update_station(10, false); // Station 10 fails
    list.update_station(10, false); // Station 10 fails again
    list.update_station(10, false); // Station 10 third failure

    println!();
    list.print_status();

    // Get bitmap representation
    let bitmap = list.get_bitmap();
    print!("\nLive list bitmap (hex): ");
    for byte in &bitmap {
        print!("{:02X} ", byte);
    }
    println!();

    // Get active stations list
    let active = list.get_active_stations();
    println!("\nActive stations: {:?}", active);
}
```

## Summary

**Live List Management** is a fundamental monitoring capability in Profibus networks that maintains a real-time registry of active bus participants. The master device continuously updates this list based on communication success/failure, providing critical visibility into network health.

**Key Features:**
- Real-time tracking of which slaves are online and responding
- State tracking (active, inactive, faulty, not configured)
- Statistical data collection (success/failure counts)
- Event notification when devices go online/offline
- Bitmap representation for efficient status queries

**Implementation Highlights:**
- Uses bitmaps or hash maps for efficient storage
- Implements timeout-based failure detection (typically 3 consecutive failures)
- Provides callback mechanisms for state change notifications
- Thread-safe access in multi-threaded environments
- Maintains historical statistics for diagnostic purposes

Live list management enables predictive maintenance, rapid fault isolation, and improved overall system reliability by giving operators immediate visibility into the operational status of all networked devices.