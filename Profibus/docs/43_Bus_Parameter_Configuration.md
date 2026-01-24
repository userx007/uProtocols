# Bus Parameter Configuration in Profibus

## Detailed Description

Bus Parameter Configuration in Profibus is a critical aspect of establishing reliable communication in a Profibus network. These parameters define the timing characteristics and behavior of the communication protocol, directly impacting network performance, reliability, and the ability to support different transmission speeds and cable lengths.

### Key Timing Parameters

**1. Slot Time (T_SLOT)**
The slot time defines the maximum time a master station waits for an acknowledgment or response from a slave station. This parameter is crucial for detecting communication failures and maintaining network synchronization. If a response is not received within the slot time, the master considers the transaction failed and may retry or move to the next station.

**2. Min_TSDR (Minimum Station Delay Responder)**
This parameter specifies the minimum time a responder (slave) must wait before sending its response after receiving a request. This ensures that the requesting station has sufficient time to switch from transmit to receive mode and prevents bus collisions. The min_TSDR accounts for transceiver turnaround times and propagation delays.

**3. Max_TSDR (Maximum Station Delay Responder)**
The maximum time allowed for a responder to begin transmitting its response. This parameter ensures that responses arrive within predictable time windows, allowing the master to efficiently manage token rotation and data exchange cycles.

**4. Quiet Time (T_QUI)**
The minimum idle time required on the bus before a new transmission can begin. This parameter ensures proper bus stabilization between frames.

**5. Setup Time (T_SET)**
The time required for initializing and synchronizing stations during network startup.

**6. Target Rotation Time (T_TR)**
The desired time for the token to make one complete rotation around all active masters in the network. This parameter is essential for real-time performance and determines how frequently each master gets access to the bus.

These parameters must be carefully calculated based on:
- Network baud rate (9.6 kbps to 12 Mbps)
- Maximum cable length
- Number of stations
- Real-time requirements
- Hardware characteristics of the transceivers

## C/C++ Code Example

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Profibus bus parameter structure
typedef struct {
    uint16_t slot_time;        // Slot time in bit times
    uint8_t min_tsdr;          // Minimum station delay responder (bit times)
    uint16_t max_tsdr;         // Maximum station delay responder (bit times)
    uint8_t quiet_time;        // Quiet time (bit times)
    uint8_t setup_time;        // Setup time (bit times)
    uint32_t target_rotation_time; // Target rotation time in microseconds
    uint8_t retry_limit;       // Maximum retry attempts
    uint32_t baud_rate;        // Communication speed in bps
} profibus_bus_params_t;

// Profibus configuration structure
typedef struct {
    uint8_t station_address;
    profibus_bus_params_t bus_params;
    uint8_t highest_station_address;
    uint8_t gap_update_factor;
} profibus_config_t;

// Calculate slot time based on baud rate and cable length
uint16_t calculate_slot_time(uint32_t baud_rate, uint16_t cable_length_m) {
    // Base slot time formula (simplified)
    // Actual calculation depends on specific Profibus specifications
    uint32_t bit_time_ns = 1000000000UL / baud_rate;
    uint32_t propagation_delay_ns = cable_length_m * 5; // ~5ns per meter
    
    // Add safety margins and processing time
    uint32_t total_time_ns = propagation_delay_ns + (bit_time_ns * 100);
    uint16_t slot_time_bits = (total_time_ns / bit_time_ns) + 50;
    
    return slot_time_bits;
}

// Calculate min_TSDR based on baud rate
uint8_t calculate_min_tsdr(uint32_t baud_rate) {
    // Min_TSDR values according to Profibus specification
    if (baud_rate <= 9600) return 2;
    else if (baud_rate <= 19200) return 2;
    else if (baud_rate <= 93750) return 11;
    else if (baud_rate <= 187500) return 11;
    else if (baud_rate <= 500000) return 11;
    else if (baud_rate <= 1500000) return 11;
    else if (baud_rate <= 3000000) return 22;
    else if (baud_rate <= 6000000) return 33;
    else return 44; // For 12 Mbps
}

// Calculate max_TSDR
uint16_t calculate_max_tsdr(uint32_t baud_rate) {
    // Typically min_TSDR + additional response time
    return calculate_min_tsdr(baud_rate) + 200;
}

// Initialize bus parameters with recommended values
void init_bus_parameters(profibus_config_t *config, 
                         uint32_t baud_rate, 
                         uint16_t cable_length_m,
                         uint8_t num_stations) {
    
    config->bus_params.baud_rate = baud_rate;
    config->bus_params.slot_time = calculate_slot_time(baud_rate, cable_length_m);
    config->bus_params.min_tsdr = calculate_min_tsdr(baud_rate);
    config->bus_params.max_tsdr = calculate_max_tsdr(baud_rate);
    config->bus_params.quiet_time = 0; // Typically 0 for Profibus DP
    config->bus_params.setup_time = 1; // Minimum setup time
    
    // Calculate target rotation time (example: 10ms per station)
    config->bus_params.target_rotation_time = num_stations * 10000; // microseconds
    config->bus_params.retry_limit = 1; // Standard retry limit
}

// Apply bus parameters to hardware (pseudo-implementation)
int apply_bus_parameters(const profibus_config_t *config) {
    printf("Applying Profibus Bus Parameters:\n");
    printf("  Baud Rate: %u bps\n", config->bus_params.baud_rate);
    printf("  Slot Time: %u bit times\n", config->bus_params.slot_time);
    printf("  Min TSDR: %u bit times\n", config->bus_params.min_tsdr);
    printf("  Max TSDR: %u bit times\n", config->bus_params.max_tsdr);
    printf("  Quiet Time: %u bit times\n", config->bus_params.quiet_time);
    printf("  Target Rotation Time: %u µs\n", config->bus_params.target_rotation_time);
    printf("  Retry Limit: %u\n", config->bus_params.retry_limit);
    
    // In real implementation, this would configure hardware registers
    // write_register(SLOT_TIME_REG, config->bus_params.slot_time);
    // write_register(MIN_TSDR_REG, config->bus_params.min_tsdr);
    // etc.
    
    return 0; // Success
}

// Validate bus parameters
int validate_bus_parameters(const profibus_bus_params_t *params) {
    // Check if slot time is within valid range
    if (params->slot_time < 50 || params->slot_time > 16383) {
        fprintf(stderr, "Error: Slot time out of range\n");
        return -1;
    }
    
    // Check if max_TSDR > min_TSDR
    if (params->max_tsdr <= params->min_tsdr) {
        fprintf(stderr, "Error: Max TSDR must be greater than Min TSDR\n");
        return -1;
    }
    
    // Validate baud rate
    if (params->baud_rate != 9600 && params->baud_rate != 19200 &&
        params->baud_rate != 93750 && params->baud_rate != 187500 &&
        params->baud_rate != 500000 && params->baud_rate != 1500000 &&
        params->baud_rate != 3000000 && params->baud_rate != 6000000 &&
        params->baud_rate != 12000000) {
        fprintf(stderr, "Error: Invalid baud rate\n");
        return -1;
    }
    
    return 0; // Valid
}

int main() {
    profibus_config_t config;
    
    // Initialize configuration
    config.station_address = 2;
    config.highest_station_address = 10;
    config.gap_update_factor = 1;
    
    // Configure for 1.5 Mbps, 200m cable, 5 stations
    init_bus_parameters(&config, 1500000, 200, 5);
    
    // Validate parameters
    if (validate_bus_parameters(&config.bus_params) != 0) {
        fprintf(stderr, "Bus parameter validation failed\n");
        return 1;
    }
    
    // Apply parameters
    apply_bus_parameters(&config);
    
    printf("\nConfiguration complete!\n");
    
    return 0;
}
```

## Rust Code Example

```rust
use std::fmt;

#[derive(Debug, Clone)]
pub struct ProfibusBusParams {
    pub slot_time: u16,              // Slot time in bit times
    pub min_tsdr: u8,                // Minimum station delay responder
    pub max_tsdr: u16,               // Maximum station delay responder
    pub quiet_time: u8,              // Quiet time in bit times
    pub setup_time: u8,              // Setup time
    pub target_rotation_time: u32,   // Target rotation time in microseconds
    pub retry_limit: u8,             // Maximum retry attempts
    pub baud_rate: u32,              // Communication speed in bps
}

#[derive(Debug, Clone)]
pub struct ProfibusConfig {
    pub station_address: u8,
    pub bus_params: ProfibusBusParams,
    pub highest_station_address: u8,
    pub gap_update_factor: u8,
}

#[derive(Debug)]
pub enum ProfibusError {
    InvalidSlotTime,
    InvalidTsdr,
    InvalidBaudRate,
    ConfigurationFailed(String),
}

impl fmt::Display for ProfibusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ProfibusError::InvalidSlotTime => write!(f, "Slot time out of valid range"),
            ProfibusError::InvalidTsdr => write!(f, "Max TSDR must be greater than Min TSDR"),
            ProfibusError::InvalidBaudRate => write!(f, "Invalid baud rate specified"),
            ProfibusError::ConfigurationFailed(msg) => write!(f, "Configuration failed: {}", msg),
        }
    }
}

impl std::error::Error for ProfibusError {}

// Calculate slot time based on baud rate and cable length
fn calculate_slot_time(baud_rate: u32, cable_length_m: u16) -> u16 {
    let bit_time_ns = 1_000_000_000u64 / baud_rate as u64;
    let propagation_delay_ns = cable_length_m as u64 * 5; // ~5ns per meter
    
    // Add safety margins and processing time
    let total_time_ns = propagation_delay_ns + (bit_time_ns * 100);
    let slot_time_bits = (total_time_ns / bit_time_ns) as u16 + 50;
    
    slot_time_bits
}

// Calculate min_TSDR based on baud rate
fn calculate_min_tsdr(baud_rate: u32) -> u8 {
    match baud_rate {
        0..=9_600 => 2,
        9_601..=19_200 => 2,
        19_201..=93_750 => 11,
        93_751..=187_500 => 11,
        187_501..=500_000 => 11,
        500_001..=1_500_000 => 11,
        1_500_001..=3_000_000 => 22,
        3_000_001..=6_000_000 => 33,
        _ => 44, // For 12 Mbps
    }
}

// Calculate max_TSDR
fn calculate_max_tsdr(baud_rate: u32) -> u16 {
    calculate_min_tsdr(baud_rate) as u16 + 200
}

// Initialize bus parameters with recommended values
pub fn init_bus_parameters(
    baud_rate: u32,
    cable_length_m: u16,
    num_stations: u8,
) -> ProfibusBusParams {
    ProfibusBusParams {
        baud_rate,
        slot_time: calculate_slot_time(baud_rate, cable_length_m),
        min_tsdr: calculate_min_tsdr(baud_rate),
        max_tsdr: calculate_max_tsdr(baud_rate),
        quiet_time: 0,  // Typically 0 for Profibus DP
        setup_time: 1,  // Minimum setup time
        target_rotation_time: num_stations as u32 * 10_000, // microseconds
        retry_limit: 1, // Standard retry limit
    }
}

// Validate bus parameters
pub fn validate_bus_parameters(params: &ProfibusBusParams) -> Result<(), ProfibusError> {
    // Check if slot time is within valid range
    if params.slot_time < 50 || params.slot_time > 16_383 {
        return Err(ProfibusError::InvalidSlotTime);
    }
    
    // Check if max_TSDR > min_TSDR
    if params.max_tsdr <= params.min_tsdr as u16 {
        return Err(ProfibusError::InvalidTsdr);
    }
    
    // Validate baud rate
    const VALID_BAUD_RATES: [u32; 9] = [
        9_600, 19_200, 93_750, 187_500, 500_000,
        1_500_000, 3_000_000, 6_000_000, 12_000_000
    ];
    
    if !VALID_BAUD_RATES.contains(&params.baud_rate) {
        return Err(ProfibusError::InvalidBaudRate);
    }
    
    Ok(())
}

// Apply bus parameters to hardware (pseudo-implementation)
pub fn apply_bus_parameters(config: &ProfibusConfig) -> Result<(), ProfibusError> {
    println!("Applying Profibus Bus Parameters:");
    println!("  Baud Rate: {} bps", config.bus_params.baud_rate);
    println!("  Slot Time: {} bit times", config.bus_params.slot_time);
    println!("  Min TSDR: {} bit times", config.bus_params.min_tsdr);
    println!("  Max TSDR: {} bit times", config.bus_params.max_tsdr);
    println!("  Quiet Time: {} bit times", config.bus_params.quiet_time);
    println!("  Target Rotation Time: {} µs", config.bus_params.target_rotation_time);
    println!("  Retry Limit: {}", config.bus_params.retry_limit);
    
    // In real implementation, this would configure hardware registers
    // write_register(SLOT_TIME_REG, config.bus_params.slot_time)?;
    // write_register(MIN_TSDR_REG, config.bus_params.min_tsdr)?;
    // etc.
    
    Ok(())
}

// Builder pattern for easier configuration
pub struct ProfibusConfigBuilder {
    station_address: u8,
    baud_rate: u32,
    cable_length_m: u16,
    num_stations: u8,
    highest_station_address: u8,
}

impl ProfibusConfigBuilder {
    pub fn new(station_address: u8) -> Self {
        Self {
            station_address,
            baud_rate: 1_500_000,
            cable_length_m: 200,
            num_stations: 5,
            highest_station_address: 10,
        }
    }
    
    pub fn baud_rate(mut self, baud_rate: u32) -> Self {
        self.baud_rate = baud_rate;
        self
    }
    
    pub fn cable_length(mut self, length_m: u16) -> Self {
        self.cable_length_m = length_m;
        self
    }
    
    pub fn num_stations(mut self, num: u8) -> Self {
        self.num_stations = num;
        self
    }
    
    pub fn build(self) -> Result<ProfibusConfig, ProfibusError> {
        let bus_params = init_bus_parameters(
            self.baud_rate,
            self.cable_length_m,
            self.num_stations,
        );
        
        validate_bus_parameters(&bus_params)?;
        
        Ok(ProfibusConfig {
            station_address: self.station_address,
            bus_params,
            highest_station_address: self.highest_station_address,
            gap_update_factor: 1,
        })
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Using builder pattern
    let config = ProfibusConfigBuilder::new(2)
        .baud_rate(1_500_000)
        .cable_length(200)
        .num_stations(5)
        .build()?;
    
    // Validate parameters
    validate_bus_parameters(&config.bus_params)?;
    
    // Apply parameters
    apply_bus_parameters(&config)?;
    
    println!("\nConfiguration complete!");
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_slot_time_calculation() {
        let slot_time = calculate_slot_time(1_500_000, 200);
        assert!(slot_time > 0);
        assert!(slot_time < 16_383);
    }
    
    #[test]
    fn test_min_tsdr_values() {
        assert_eq!(calculate_min_tsdr(9_600), 2);
        assert_eq!(calculate_min_tsdr(500_000), 11);
        assert_eq!(calculate_min_tsdr(3_000_000), 22);
        assert_eq!(calculate_min_tsdr(12_000_000), 44);
    }
    
    #[test]
    fn test_invalid_baud_rate() {
        let params = ProfibusBusParams {
            baud_rate: 115_200, // Invalid baud rate
            slot_time: 300,
            min_tsdr: 11,
            max_tsdr: 211,
            quiet_time: 0,
            setup_time: 1,
            target_rotation_time: 50_000,
            retry_limit: 1,
        };
        
        assert!(validate_bus_parameters(&params).is_err());
    }
    
    #[test]
    fn test_builder_pattern() {
        let config = ProfibusConfigBuilder::new(2)
            .baud_rate(500_000)
            .cable_length(100)
            .num_stations(3)
            .build();
        
        assert!(config.is_ok());
    }
}
```

## Summary

Bus Parameter Configuration is fundamental to Profibus network operation, defining the precise timing characteristics that ensure reliable, deterministic communication. The key parameters—slot time, min_TSDR, and max_TSDR—must be carefully calculated based on network topology, transmission speed, and real-time requirements.

**Critical points:**
- **Slot Time** determines timeout values for response detection
- **Min_TSDR** prevents bus collisions by enforcing minimum response delays
- **Max_TSDR** ensures predictable response timing for deterministic operation
- Parameters are interdependent and must be calculated based on baud rate, cable length, and number of stations
- Incorrect configuration can lead to communication failures, timeouts, or reduced network performance
- Standard baud rates range from 9.6 kbps to 12 Mbps, each with specific timing requirements

The code examples demonstrate practical implementation in both C/C++ and Rust, including parameter calculation, validation, and configuration application. The Rust example additionally showcases modern patterns like the builder pattern and comprehensive error handling, making it safer and more maintainable for industrial applications.