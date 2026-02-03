# Profibus Bandwidth Management

## Detailed Description

Bandwidth Management in Profibus is a critical aspect of network optimization that focuses on efficiently utilizing the available bus capacity while preventing saturation that could lead to communication delays, missed deadlines, or system failures. Profibus networks operate with finite communication resources, and proper bandwidth management ensures deterministic behavior and reliable real-time communication.

### Key Concepts

**1. Bus Cycle Time**
The bus cycle time is the fundamental unit of bandwidth in Profibus. It represents the time required for the master to complete one full communication cycle with all active slaves on the network.

**2. Token Rotation Time**
In multi-master systems, the token rotation time includes the time for the token to pass through all masters and for each master to complete its communications. This must be managed to ensure fair access and prevent token monopolization.

**3. Bandwidth Components**
- **Target Rotation Time (TTR)**: Maximum desired token rotation time
- **Slot Time**: Maximum time a station waits for acknowledgment
- **Gap Update Factor**: Multiplier for inter-frame gaps
- **Real Rotation Time**: Actual measured token rotation time

**4. Traffic Types**
- **Cyclic Traffic**: Regular, predictable data exchange (highest priority)
- **Acyclic Traffic**: Event-driven, non-periodic communication (lower priority)
- **Time-stamped Messages**: Time-critical messages with guaranteed delivery times

### Bandwidth Optimization Strategies

1. **Station Address Optimization**: Minimize gaps in address ranges
2. **Baud Rate Selection**: Choose appropriate speed for network length and requirements
3. **Message Prioritization**: Balance cyclic and acyclic traffic
4. **Watchdog Configuration**: Set appropriate timeouts without wasting bandwidth
5. **Master Count Limitation**: Reduce number of masters when possible

## Programming Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus bandwidth management structures
typedef struct {
    uint32_t baud_rate;           // Baud rate in bps
    uint16_t target_rotation_time; // TTR in ms
    uint16_t slot_time;           // Slot time in bit times
    uint8_t gap_factor;           // Gap update factor (1-100)
    uint8_t num_masters;          // Number of masters
    uint8_t num_slaves;           // Number of slaves
    uint16_t max_tsdr;            // Max station delay responder
} ProfibusTimingConfig;

typedef struct {
    uint8_t station_address;
    uint16_t cyclic_data_length;  // Bytes per cycle
    uint16_t acyclic_quota;       // Max acyclic bytes per cycle
    uint32_t poll_interval;       // microseconds
    bool is_master;
} StationConfig;

typedef struct {
    uint32_t available_bandwidth; // bits per second
    uint32_t used_bandwidth;      // bits per second
    uint16_t actual_cycle_time;   // microseconds
    uint16_t max_cycle_time;      // microseconds
    float utilization_percent;
} BandwidthMetrics;

// Calculate theoretical bus cycle time
uint32_t calculate_bus_cycle_time(
    ProfibusTimingConfig *timing,
    StationConfig *stations,
    uint8_t num_stations
) {
    uint32_t cycle_time_us = 0;
    uint32_t bit_time_us = 1000000 / timing->baud_rate;
    
    for (uint8_t i = 0; i < num_stations; i++) {
        if (!stations[i].is_master) {
            // Time for request frame (master to slave)
            uint32_t request_frame_bits = 
                11 +  // SD1, DA, SA, FC
                stations[i].cyclic_data_length * 11 + // Data (with start/stop bits)
                11;   // FCS, ED
            
            // Time for response frame (slave to master)
            uint32_t response_frame_bits = 
                11 +  // SD1, DA, SA, FC
                stations[i].cyclic_data_length * 11 +
                11;
            
            // Add slot time for potential retries
            uint32_t transaction_time = 
                (request_frame_bits + response_frame_bits) * bit_time_us +
                timing->slot_time * bit_time_us +
                timing->max_tsdr +
                (timing->gap_factor * 11 * bit_time_us); // Inter-frame gap
            
            cycle_time_us += transaction_time;
        }
    }
    
    // Add token passing overhead for multi-master systems
    if (timing->num_masters > 1) {
        uint32_t token_overhead = 
            timing->num_masters * (33 * bit_time_us + timing->max_tsdr);
        cycle_time_us += token_overhead;
    }
    
    return cycle_time_us;
}

// Calculate bandwidth utilization
void calculate_bandwidth_metrics(
    ProfibusTimingConfig *timing,
    StationConfig *stations,
    uint8_t num_stations,
    BandwidthMetrics *metrics
) {
    metrics->available_bandwidth = timing->baud_rate;
    
    // Calculate used bandwidth based on actual data transfer
    uint32_t total_data_bits = 0;
    uint32_t cycle_time_us = calculate_bus_cycle_time(timing, stations, num_stations);
    
    for (uint8_t i = 0; i < num_stations; i++) {
        // Cyclic data throughput
        total_data_bits += stations[i].cyclic_data_length * 8 * 2; // bidirectional
    }
    
    metrics->actual_cycle_time = cycle_time_us / 1000; // Convert to ms
    metrics->max_cycle_time = timing->target_rotation_time;
    
    // Calculate effective throughput
    if (cycle_time_us > 0) {
        metrics->used_bandwidth = 
            (total_data_bits * 1000000UL) / cycle_time_us;
        metrics->utilization_percent = 
            ((float)cycle_time_us / (timing->target_rotation_time * 1000.0f)) * 100.0f;
    }
}

// Optimize station polling order to minimize gaps
void optimize_polling_order(
    StationConfig *stations,
    uint8_t num_stations,
    uint8_t *optimized_order
) {
    // Simple optimization: sort by address to minimize gaps
    uint8_t temp_order[num_stations];
    
    for (uint8_t i = 0; i < num_stations; i++) {
        temp_order[i] = i;
    }
    
    // Bubble sort by station address (slaves only)
    for (uint8_t i = 0; i < num_stations - 1; i++) {
        for (uint8_t j = 0; j < num_stations - i - 1; j++) {
            if (!stations[temp_order[j]].is_master && 
                !stations[temp_order[j+1]].is_master &&
                stations[temp_order[j]].station_address > 
                stations[temp_order[j+1]].station_address) {
                
                uint8_t temp = temp_order[j];
                temp_order[j] = temp_order[j+1];
                temp_order[j+1] = temp;
            }
        }
    }
    
    memcpy(optimized_order, temp_order, num_stations);
}

// Dynamic bandwidth allocation for acyclic services
bool allocate_acyclic_bandwidth(
    StationConfig *station,
    uint16_t requested_bytes,
    BandwidthMetrics *metrics
) {
    // Check if we have available bandwidth margin
    float available_margin = 100.0f - metrics->utilization_percent;
    
    if (available_margin < 10.0f) {
        // Less than 10% margin - deny request
        return false;
    }
    
    // Calculate additional time required
    uint32_t bit_time_us = 1000000 / metrics->available_bandwidth;
    uint32_t additional_time = requested_bytes * 11 * bit_time_us;
    
    // Check if it fits within quota
    if (requested_bytes <= station->acyclic_quota) {
        station->acyclic_quota -= requested_bytes;
        return true;
    }
    
    return false;
}

// Monitor and report bandwidth usage
void monitor_bandwidth(
    BandwidthMetrics *metrics,
    uint32_t threshold_percent
) {
    printf("\n=== Profibus Bandwidth Monitor ===\n");
    printf("Available Bandwidth: %u bps\n", metrics->available_bandwidth);
    printf("Used Bandwidth: %u bps\n", metrics->used_bandwidth);
    printf("Actual Cycle Time: %u ms\n", metrics->actual_cycle_time);
    printf("Max Cycle Time: %u ms\n", metrics->max_cycle_time);
    printf("Utilization: %.2f%%\n", metrics->utilization_percent);
    
    if (metrics->utilization_percent > threshold_percent) {
        printf("WARNING: Bandwidth utilization exceeds threshold!\n");
        printf("Consider:\n");
        printf("  - Reducing data payload sizes\n");
        printf("  - Increasing baud rate\n");
        printf("  - Optimizing polling intervals\n");
        printf("  - Reducing number of stations\n");
    } else if (metrics->actual_cycle_time > metrics->max_cycle_time) {
        printf("ERROR: Cycle time exceeds target rotation time!\n");
        printf("Real-time constraints may be violated.\n");
    } else {
        printf("Status: Normal operation\n");
    }
}

// Example usage
int main() {
    // Configure Profibus timing parameters
    ProfibusTimingConfig timing = {
        .baud_rate = 1500000,          // 1.5 Mbps
        .target_rotation_time = 10,     // 10 ms
        .slot_time = 200,               // 200 bit times
        .gap_factor = 10,
        .num_masters = 1,
        .num_slaves = 8,
        .max_tsdr = 50                  // 50 microseconds
    };
    
    // Configure stations
    StationConfig stations[] = {
        {.station_address = 0, .cyclic_data_length = 0, 
         .acyclic_quota = 0, .poll_interval = 0, .is_master = true},
        {.station_address = 2, .cyclic_data_length = 16, 
         .acyclic_quota = 64, .poll_interval = 10000, .is_master = false},
        {.station_address = 3, .cyclic_data_length = 32, 
         .acyclic_quota = 64, .poll_interval = 10000, .is_master = false},
        {.station_address = 5, .cyclic_data_length = 8, 
         .acyclic_quota = 32, .poll_interval = 10000, .is_master = false},
        {.station_address = 7, .cyclic_data_length = 24, 
         .acyclic_quota = 64, .poll_interval = 10000, .is_master = false},
        {.station_address = 10, .cyclic_data_length = 16, 
         .acyclic_quota = 64, .poll_interval = 10000, .is_master = false},
        {.station_address = 12, .cyclic_data_length = 12, 
         .acyclic_quota = 32, .poll_interval = 10000, .is_master = false},
        {.station_address = 15, .cyclic_data_length = 20, 
         .acyclic_quota = 64, .poll_interval = 10000, .is_master = false},
        {.station_address = 18, .cyclic_data_length = 8, 
         .acyclic_quota = 32, .poll_interval = 10000, .is_master = false},
    };
    uint8_t num_stations = sizeof(stations) / sizeof(stations[0]);
    
    // Calculate bandwidth metrics
    BandwidthMetrics metrics;
    calculate_bandwidth_metrics(&timing, stations, num_stations, &metrics);
    
    // Monitor bandwidth
    monitor_bandwidth(&metrics, 80);
    
    // Optimize polling order
    uint8_t optimized_order[num_stations];
    optimize_polling_order(stations, num_stations, optimized_order);
    
    printf("\nOptimized Polling Order:\n");
    for (uint8_t i = 0; i < num_stations; i++) {
        if (!stations[optimized_order[i]].is_master) {
            printf("  Station %u (Address %u)\n", 
                   i, stations[optimized_order[i]].station_address);
        }
    }
    
    // Test acyclic allocation
    printf("\nTesting Acyclic Bandwidth Allocation:\n");
    if (allocate_acyclic_bandwidth(&stations[1], 48, &metrics)) {
        printf("  Allocated 48 bytes for acyclic service\n");
        printf("  Remaining quota: %u bytes\n", stations[1].acyclic_quota);
    } else {
        printf("  Acyclic allocation denied - insufficient bandwidth\n");
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct ProfibusTimingConfig {
    pub baud_rate: u32,
    pub target_rotation_time: u16,
    pub slot_time: u16,
    pub gap_factor: u8,
    pub num_masters: u8,
    pub num_slaves: u8,
    pub max_tsdr: u16,
}

#[derive(Debug, Clone)]
pub struct StationConfig {
    pub station_address: u8,
    pub cyclic_data_length: u16,
    pub acyclic_quota: u16,
    pub poll_interval: u32,
    pub is_master: bool,
}

#[derive(Debug, Clone)]
pub struct BandwidthMetrics {
    pub available_bandwidth: u32,
    pub used_bandwidth: u32,
    pub actual_cycle_time: u16,
    pub max_cycle_time: u16,
    pub utilization_percent: f32,
}

pub struct BandwidthManager {
    timing: ProfibusTimingConfig,
    stations: Vec<StationConfig>,
    metrics: BandwidthMetrics,
    traffic_history: HashMap<u8, Vec<u32>>,
}

impl BandwidthManager {
    pub fn new(timing: ProfibusTimingConfig) -> Self {
        BandwidthManager {
            metrics: BandwidthMetrics {
                available_bandwidth: timing.baud_rate,
                used_bandwidth: 0,
                actual_cycle_time: 0,
                max_cycle_time: timing.target_rotation_time,
                utilization_percent: 0.0,
            },
            timing,
            stations: Vec::new(),
            traffic_history: HashMap::new(),
        }
    }

    pub fn add_station(&mut self, station: StationConfig) {
        self.traffic_history.insert(station.station_address, Vec::new());
        self.stations.push(station);
        self.recalculate_metrics();
    }

    pub fn calculate_bus_cycle_time(&self) -> u32 {
        let bit_time_us = 1_000_000 / self.timing.baud_rate;
        let mut cycle_time_us = 0u32;

        for station in &self.stations {
            if !station.is_master {
                // Request frame bits
                let request_frame_bits = 
                    11 + station.cyclic_data_length as u32 * 11 + 11;
                
                // Response frame bits
                let response_frame_bits = 
                    11 + station.cyclic_data_length as u32 * 11 + 11;

                // Transaction time with overhead
                let transaction_time = 
                    (request_frame_bits + response_frame_bits) * bit_time_us +
                    self.timing.slot_time as u32 * bit_time_us +
                    self.timing.max_tsdr as u32 +
                    (self.timing.gap_factor as u32 * 11 * bit_time_us);

                cycle_time_us += transaction_time;
            }
        }

        // Token passing overhead
        if self.timing.num_masters > 1 {
            let token_overhead = 
                self.timing.num_masters as u32 * 
                (33 * bit_time_us + self.timing.max_tsdr as u32);
            cycle_time_us += token_overhead;
        }

        cycle_time_us
    }

    pub fn recalculate_metrics(&mut self) {
        let cycle_time_us = self.calculate_bus_cycle_time();
        
        let mut total_data_bits = 0u32;
        for station in &self.stations {
            total_data_bits += station.cyclic_data_length as u32 * 8 * 2;
        }

        self.metrics.actual_cycle_time = (cycle_time_us / 1000) as u16;
        
        if cycle_time_us > 0 {
            self.metrics.used_bandwidth = 
                (total_data_bits * 1_000_000) / cycle_time_us;
            
            self.metrics.utilization_percent = 
                (cycle_time_us as f32 / 
                 (self.timing.target_rotation_time as f32 * 1000.0)) * 100.0;
        }
    }

    pub fn optimize_polling_order(&mut self) {
        // Sort slaves by station address to minimize gaps
        self.stations.sort_by(|a, b| {
            match (a.is_master, b.is_master) {
                (true, false) => std::cmp::Ordering::Less,
                (false, true) => std::cmp::Ordering::Greater,
                _ => a.station_address.cmp(&b.station_address),
            }
        });
        
        self.recalculate_metrics();
    }

    pub fn allocate_acyclic_bandwidth(
        &mut self,
        station_address: u8,
        requested_bytes: u16,
    ) -> Result<(), String> {
        let available_margin = 100.0 - self.metrics.utilization_percent;
        
        if available_margin < 10.0 {
            return Err("Insufficient bandwidth margin".to_string());
        }

        if let Some(station) = self.stations
            .iter_mut()
            .find(|s| s.station_address == station_address) {
            
            if requested_bytes <= station.acyclic_quota {
                station.acyclic_quota -= requested_bytes;
                Ok(())
            } else {
                Err("Exceeds station acyclic quota".to_string())
            }
        } else {
            Err("Station not found".to_string())
        }
    }

    pub fn record_traffic(&mut self, station_address: u8, bytes: u32) {
        if let Some(history) = self.traffic_history.get_mut(&station_address) {
            history.push(bytes);
            if history.len() > 1000 {
                history.remove(0);
            }
        }
    }

    pub fn get_average_traffic(&self, station_address: u8) -> Option<f32> {
        self.traffic_history.get(&station_address).map(|history| {
            if history.is_empty() {
                0.0
            } else {
                history.iter().sum::<u32>() as f32 / history.len() as f32
            }
        })
    }

    pub fn monitor_bandwidth(&self, threshold_percent: u32) {
        println!("\n=== Profibus Bandwidth Monitor ===");
        println!("Available Bandwidth: {} bps", self.metrics.available_bandwidth);
        println!("Used Bandwidth: {} bps", self.metrics.used_bandwidth);
        println!("Actual Cycle Time: {} ms", self.metrics.actual_cycle_time);
        println!("Max Cycle Time: {} ms", self.metrics.max_cycle_time);
        println!("Utilization: {:.2}%", self.metrics.utilization_percent);

        if self.metrics.utilization_percent > threshold_percent as f32 {
            println!("WARNING: Bandwidth utilization exceeds threshold!");
            println!("Consider:");
            println!("  - Reducing data payload sizes");
            println!("  - Increasing baud rate");
            println!("  - Optimizing polling intervals");
            println!("  - Reducing number of stations");
        } else if self.metrics.actual_cycle_time > self.metrics.max_cycle_time {
            println!("ERROR: Cycle time exceeds target rotation time!");
            println!("Real-time constraints may be violated.");
        } else {
            println!("Status: Normal operation");
        }
    }

    pub fn analyze_bandwidth_trends(&self) {
        println!("\n=== Bandwidth Trend Analysis ===");
        
        for station in &self.stations {
            if let Some(avg) = self.get_average_traffic(station.station_address) {
                println!(
                    "Station {}: Avg traffic {:.2} bytes/cycle",
                    station.station_address, avg
                );
            }
        }
    }

    pub fn suggest_optimizations(&self) -> Vec<String> {
        let mut suggestions = Vec::new();

        if self.metrics.utilization_percent > 80.0 {
            suggestions.push(
                "High utilization detected. Consider increasing baud rate.".to_string()
            );
        }

        // Check for address gaps
        let mut slave_addresses: Vec<u8> = self.stations
            .iter()
            .filter(|s| !s.is_master)
            .map(|s| s.station_address)
            .collect();
        slave_addresses.sort();

        let mut gap_count = 0;
        for window in slave_addresses.windows(2) {
            if window[1] - window[0] > 1 {
                gap_count += 1;
            }
        }

        if gap_count > 3 {
            suggestions.push(
                format!("Found {} address gaps. Consider reassigning addresses for efficiency.", 
                        gap_count)
            );
        }

        if self.timing.num_masters > 2 {
            suggestions.push(
                "Multiple masters detected. Evaluate if all are necessary.".to_string()
            );
        }

        suggestions
    }
}

fn main() {
    // Create timing configuration
    let timing = ProfibusTimingConfig {
        baud_rate: 1_500_000,
        target_rotation_time: 10,
        slot_time: 200,
        gap_factor: 10,
        num_masters: 1,
        num_slaves: 8,
        max_tsdr: 50,
    };

    let mut manager = BandwidthManager::new(timing);

    // Add master
    manager.add_station(StationConfig {
        station_address: 0,
        cyclic_data_length: 0,
        acyclic_quota: 0,
        poll_interval: 0,
        is_master: true,
    });

    // Add slaves
    let slave_configs = vec![
        (2, 16, 64), (3, 32, 64), (5, 8, 32), (7, 24, 64),
        (10, 16, 64), (12, 12, 32), (15, 20, 64), (18, 8, 32),
    ];

    for (addr, data_len, quota) in slave_configs {
        manager.add_station(StationConfig {
            station_address: addr,
            cyclic_data_length: data_len,
            acyclic_quota: quota,
            poll_interval: 10_000,
            is_master: false,
        });
    }

    // Monitor initial state
    manager.monitor_bandwidth(80);

    // Optimize polling order
    println!("\nOptimizing polling order...");
    manager.optimize_polling_order();
    manager.recalculate_metrics();
    
    println!("\nAfter optimization:");
    manager.monitor_bandwidth(80);

    // Test acyclic allocation
    println!("\nTesting acyclic bandwidth allocation:");
    match manager.allocate_acyclic_bandwidth(2, 48) {
        Ok(_) => println!("  Successfully allocated 48 bytes"),
        Err(e) => println!("  Allocation failed: {}", e),
    }

    // Simulate traffic and record
    for _ in 0..100 {
        manager.record_traffic(2, 16);
        manager.record_traffic(3, 32);
    }

    manager.analyze_bandwidth_trends();

    // Get optimization suggestions
    println!("\n=== Optimization Suggestions ===");
    for suggestion in manager.suggest_optimizations() {
        println!("• {}", suggestion);
    }
}
```

## Summary

Profibus Bandwidth Management is essential for maintaining deterministic real-time communication in industrial automation networks. Key takeaways include:

1. **Bandwidth Components**: Understanding bus cycle time, token rotation time, slot times, and inter-frame gaps is crucial for calculating and managing bandwidth effectively.

2. **Optimization Strategies**: Minimizing address gaps, selecting appropriate baud rates, optimizing polling orders, and balancing cyclic/acyclic traffic all contribute to efficient bandwidth utilization.

3. **Monitoring & Prevention**: Continuous monitoring of bandwidth utilization, cycle times, and traffic patterns helps prevent saturation and ensures real-time constraints are met. The target should typically be to keep utilization below 80% to maintain safety margins.

4. **Dynamic Allocation**: Implementing intelligent acyclic bandwidth allocation with quota systems allows for flexible communication while protecting critical cyclic traffic.

5. **Trade-offs**: Network designers must balance factors like determinism, throughput, latency, and flexibility when configuring Profibus networks for optimal bandwidth management.

The code examples demonstrate practical implementation of bandwidth calculation, monitoring, and optimization techniques that can be integrated into master devices or network management tools to ensure reliable Profibus operation.