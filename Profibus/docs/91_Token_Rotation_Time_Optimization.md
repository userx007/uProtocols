# Token Rotation Time Optimization in Profibus

## Detailed Description

Token Rotation Time (TRT) is a critical performance parameter in Profibus networks that directly impacts real-time communication capabilities. In Profibus DP and FMS networks operating with token passing protocols, the token rotation time represents the maximum time it takes for the token to complete one full cycle through all active master stations on the bus.

### Key Concepts

**Token Passing Mechanism:**
- Profibus uses a hybrid access method combining token passing (for master-to-master communication) and master-slave polling
- Active masters form a logical token ring
- Each master holds the token for a specified time to communicate with its slaves and other masters
- After completing its communication or when its token hold time expires, it passes the token to the next master in the ring

**Token Rotation Time Components:**
- **Target Token Rotation Time (TTR):** The configured target time for one complete token cycle
- **Real Token Rotation Time (TRR):** The actual measured time for token rotation
- **Token Hold Time (THT):** Maximum time a master can hold the token
- **Slot Time (TSL):** Maximum wait time for expected events (acknowledgments, responses)
- **Gap Update Factor:** Determines how quickly the system adapts to timing changes

### Why Optimization Matters

1. **Real-time Performance:** Faster token rotation enables more frequent updates of process data
2. **Determinism:** Predictable token rotation ensures consistent cycle times for time-critical applications
3. **Throughput:** Optimized timing parameters maximize effective data transfer rates
4. **System Responsiveness:** Lower TRT reduces latency in distributed control systems
5. **Multi-master Efficiency:** Proper optimization ensures fair and efficient bus access among multiple masters

### Optimization Strategies

1. **Minimize TTR:** Set to the lowest value that meets application requirements
2. **Optimize Baud Rate:** Higher speeds reduce transmission times
3. **Reduce Active Masters:** Fewer masters in the token ring decrease rotation time
4. **Optimize Slave Configuration:** Minimize the number of slaves and their data lengths
5. **GAP Factor Tuning:** Adjust to balance responsiveness and stability
6. **Watchdog Timing:** Configure timeouts appropriately to avoid unnecessary delays

## Programming Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Profibus token rotation time optimization structures
typedef struct {
    uint16_t target_rotation_time;    // Target token rotation time in bit times
    uint16_t measured_rotation_time;  // Measured actual rotation time
    uint8_t token_hold_time;          // Token hold time in bit times
    uint8_t slot_time;                // Slot time in bit times
    uint8_t gap_update_factor;        // GAP update factor (1-100)
    uint8_t max_retry_limit;          // Maximum retry attempts
    bool high_speed_capable;          // High-speed transmission capability
} TokenTimingConfig;

typedef struct {
    uint8_t station_address;
    uint16_t poll_interval;           // Polling interval for this slave
    uint16_t watchdog_time;           // Watchdog timeout
    uint8_t data_length;              // Data length in bytes
    bool active;
} SlaveConfig;

typedef struct {
    uint8_t address;
    TokenTimingConfig timing;
    SlaveConfig slaves[32];
    uint8_t num_slaves;
    uint32_t baud_rate;
} ProfibusConfig;

// Calculate optimal token rotation time
uint16_t calculate_optimal_ttr(ProfibusConfig* config, uint8_t num_masters) {
    uint32_t bits_per_byte = 11; // Start + 8 data + parity + stop
    uint32_t token_frame_bits = 6 * bits_per_byte; // Typical token frame
    uint32_t overhead_per_master = token_frame_bits + config->timing.slot_time;
    
    // Calculate slave communication time
    uint32_t slave_comm_time = 0;
    for (uint8_t i = 0; i < config->num_slaves; i++) {
        if (config->slaves[i].active) {
            // Request + response frames
            uint32_t request_bits = (3 + config->slaves[i].data_length) * bits_per_byte;
            uint32_t response_bits = (3 + config->slaves[i].data_length) * bits_per_byte;
            slave_comm_time += request_bits + response_bits + (2 * config->timing.slot_time);
        }
    }
    
    // Total rotation time in bit times
    uint32_t total_time = (overhead_per_master * num_masters) + slave_comm_time;
    
    // Add safety margin (10%)
    total_time = (total_time * 110) / 100;
    
    return (uint16_t)(total_time > 65535 ? 65535 : total_time);
}

// Optimize token hold time for a master
uint16_t calculate_token_hold_time(ProfibusConfig* config) {
    uint32_t bits_per_byte = 11;
    uint32_t slave_service_time = 0;
    
    for (uint8_t i = 0; i < config->num_slaves; i++) {
        if (config->slaves[i].active) {
            // Time needed to service one slave
            uint32_t frame_bits = (6 + 2 * config->slaves[i].data_length) * bits_per_byte;
            slave_service_time += frame_bits + config->timing.slot_time;
        }
    }
    
    return (uint16_t)slave_service_time;
}

// Monitor and adjust token rotation time dynamically
bool monitor_token_performance(ProfibusConfig* config, uint16_t measured_trt) {
    config->timing.measured_rotation_time = measured_trt;
    
    // Check if measured time exceeds target
    if (measured_trt > config->timing.target_rotation_time) {
        float overrun_percent = ((float)(measured_trt - config->timing.target_rotation_time) / 
                                 config->timing.target_rotation_time) * 100.0f;
        
        printf("WARNING: Token rotation time exceeded by %.2f%%\n", overrun_percent);
        
        // Suggest optimization if overrun is significant
        if (overrun_percent > 10.0f) {
            printf("Optimization needed:\n");
            printf("- Consider reducing poll intervals\n");
            printf("- Reduce number of active slaves\n");
            printf("- Increase baud rate if possible\n");
            return false;
        }
    }
    
    return true;
}

// Optimize GAP update factor
uint8_t optimize_gap_factor(uint16_t target_trt, uint8_t num_masters) {
    // GAP factor determines address scanning speed
    // Higher values = slower scanning, more stable
    // Lower values = faster scanning, more responsive
    
    if (target_trt < 1000) {
        return 1;  // Very fast rotation, minimal GAP
    } else if (target_trt < 5000) {
        return 5;  // Fast rotation
    } else if (target_trt < 10000) {
        return 10; // Medium rotation
    } else {
        return 20; // Slow rotation, can afford longer GAP
    }
}

// Configure Profibus master for optimal token timing
void configure_optimal_timing(ProfibusConfig* config, uint8_t num_masters) {
    // Calculate optimal TTR
    config->timing.target_rotation_time = calculate_optimal_ttr(config, num_masters);
    
    // Calculate token hold time
    config->timing.token_hold_time = calculate_token_hold_time(config) / 256; // Convert to THT units
    
    // Optimize GAP factor
    config->timing.gap_update_factor = optimize_gap_factor(
        config->timing.target_rotation_time, num_masters
    );
    
    // Set slot time based on baud rate
    if (config->baud_rate >= 1500000) {
        config->timing.slot_time = 100;  // High speed
    } else if (config->baud_rate >= 500000) {
        config->timing.slot_time = 200;  // Medium speed
    } else {
        config->timing.slot_time = 400;  // Low speed
    }
    
    printf("Optimized Token Timing Configuration:\n");
    printf("  Target Rotation Time: %u bit times\n", config->timing.target_rotation_time);
    printf("  Token Hold Time: %u\n", config->timing.token_hold_time);
    printf("  Slot Time: %u bit times\n", config->timing.slot_time);
    printf("  GAP Update Factor: %u\n", config->timing.gap_update_factor);
    printf("  Estimated cycle time: %.2f ms\n", 
           (float)config->timing.target_rotation_time / config->baud_rate * 1000.0f);
}

// Example usage
int main() {
    ProfibusConfig config = {
        .address = 2,
        .num_slaves = 3,
        .baud_rate = 1500000, // 1.5 Mbaud
        .timing = {
            .max_retry_limit = 1,
            .high_speed_capable = true
        }
    };
    
    // Configure slaves
    config.slaves[0] = (SlaveConfig){.station_address = 10, .data_length = 8, .active = true};
    config.slaves[1] = (SlaveConfig){.station_address = 11, .data_length = 16, .active = true};
    config.slaves[2] = (SlaveConfig){.station_address = 12, .data_length = 4, .active = true};
    
    // Configure optimal timing for 2 masters
    configure_optimal_timing(&config, 2);
    
    // Simulate monitoring
    uint16_t simulated_measured_trt = config.timing.target_rotation_time + 50;
    monitor_token_performance(&config, simulated_measured_trt);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

// Token timing configuration structure
#[derive(Debug, Clone)]
pub struct TokenTimingConfig {
    pub target_rotation_time: u16,    // Target token rotation time in bit times
    pub measured_rotation_time: u16,  // Measured actual rotation time
    pub token_hold_time: u8,          // Token hold time
    pub slot_time: u8,                // Slot time in bit times
    pub gap_update_factor: u8,        // GAP update factor (1-100)
    pub max_retry_limit: u8,          // Maximum retry attempts
    pub high_speed_capable: bool,     // High-speed capability
}

#[derive(Debug, Clone)]
pub struct SlaveConfig {
    pub station_address: u8,
    pub poll_interval: u16,
    pub watchdog_time: u16,
    pub data_length: u8,
    pub active: bool,
}

#[derive(Debug)]
pub struct ProfibusConfig {
    pub address: u8,
    pub timing: TokenTimingConfig,
    pub slaves: Vec<SlaveConfig>,
    pub baud_rate: u32,
}

impl ProfibusConfig {
    pub fn new(address: u8, baud_rate: u32) -> Self {
        Self {
            address,
            timing: TokenTimingConfig {
                target_rotation_time: 0,
                measured_rotation_time: 0,
                token_hold_time: 0,
                slot_time: 0,
                gap_update_factor: 10,
                max_retry_limit: 1,
                high_speed_capable: true,
            },
            slaves: Vec::new(),
            baud_rate,
        }
    }

    // Add a slave to the configuration
    pub fn add_slave(&mut self, slave: SlaveConfig) {
        self.slaves.push(slave);
    }

    // Calculate optimal token rotation time
    pub fn calculate_optimal_ttr(&self, num_masters: u8) -> u16 {
        const BITS_PER_BYTE: u32 = 11; // Start + 8 data + parity + stop
        const TOKEN_FRAME_BITS: u32 = 6 * BITS_PER_BYTE;
        
        let overhead_per_master = TOKEN_FRAME_BITS + self.timing.slot_time as u32;
        
        // Calculate slave communication time
        let slave_comm_time: u32 = self.slaves.iter()
            .filter(|s| s.active)
            .map(|s| {
                let request_bits = (3 + s.data_length as u32) * BITS_PER_BYTE;
                let response_bits = (3 + s.data_length as u32) * BITS_PER_BYTE;
                request_bits + response_bits + (2 * self.timing.slot_time as u32)
            })
            .sum();
        
        // Total rotation time in bit times
        let mut total_time = (overhead_per_master * num_masters as u32) + slave_comm_time;
        
        // Add 10% safety margin
        total_time = (total_time * 110) / 100;
        
        total_time.min(65535) as u16
    }

    // Calculate token hold time
    pub fn calculate_token_hold_time(&self) -> u16 {
        const BITS_PER_BYTE: u32 = 11;
        
        let slave_service_time: u32 = self.slaves.iter()
            .filter(|s| s.active)
            .map(|s| {
                let frame_bits = (6 + 2 * s.data_length as u32) * BITS_PER_BYTE;
                frame_bits + self.timing.slot_time as u32
            })
            .sum();
        
        slave_service_time as u16
    }

    // Optimize GAP update factor
    pub fn optimize_gap_factor(target_trt: u16, _num_masters: u8) -> u8 {
        match target_trt {
            0..=999 => 1,      // Very fast rotation
            1000..=4999 => 5,  // Fast rotation
            5000..=9999 => 10, // Medium rotation
            _ => 20,           // Slow rotation
        }
    }

    // Configure optimal timing parameters
    pub fn configure_optimal_timing(&mut self, num_masters: u8) {
        // Calculate optimal TTR
        self.timing.target_rotation_time = self.calculate_optimal_ttr(num_masters);
        
        // Calculate token hold time
        let tht = self.calculate_token_hold_time();
        self.timing.token_hold_time = (tht / 256).min(255) as u8;
        
        // Optimize GAP factor
        self.timing.gap_update_factor = Self::optimize_gap_factor(
            self.timing.target_rotation_time,
            num_masters,
        );
        
        // Set slot time based on baud rate
        self.timing.slot_time = match self.baud_rate {
            1_500_000.. => 100,  // High speed
            500_000..=1_499_999 => 200,  // Medium speed
            _ => 400,  // Low speed
        };
        
        self.print_configuration();
    }

    // Monitor token performance
    pub fn monitor_token_performance(&mut self, measured_trt: u16) -> Result<(), String> {
        self.timing.measured_rotation_time = measured_trt;
        
        if measured_trt > self.timing.target_rotation_time {
            let overrun_percent = ((measured_trt - self.timing.target_rotation_time) as f32 
                                  / self.timing.target_rotation_time as f32) * 100.0;
            
            println!("WARNING: Token rotation time exceeded by {:.2}%", overrun_percent);
            
            if overrun_percent > 10.0 {
                return Err(format!(
                    "Token rotation time overrun: {:.2}%. Optimization required.",
                    overrun_percent
                ));
            }
        }
        
        Ok(())
    }

    // Print configuration details
    pub fn print_configuration(&self) {
        println!("Optimized Token Timing Configuration:");
        println!("  Target Rotation Time: {} bit times", self.timing.target_rotation_time);
        println!("  Token Hold Time: {}", self.timing.token_hold_time);
        println!("  Slot Time: {} bit times", self.timing.slot_time);
        println!("  GAP Update Factor: {}", self.timing.gap_update_factor);
        
        let cycle_time_ms = (self.timing.target_rotation_time as f32 / self.baud_rate as f32) * 1000.0;
        println!("  Estimated cycle time: {:.2} ms", cycle_time_ms);
    }

    // Calculate theoretical maximum throughput
    pub fn calculate_throughput(&self) -> f32 {
        let active_slaves = self.slaves.iter().filter(|s| s.active).count();
        let total_data_bytes: u32 = self.slaves.iter()
            .filter(|s| s.active)
            .map(|s| s.data_length as u32)
            .sum();
        
        if self.timing.target_rotation_time == 0 {
            return 0.0;
        }
        
        let cycles_per_second = self.baud_rate as f32 / self.timing.target_rotation_time as f32;
        let bytes_per_second = total_data_bytes as f32 * cycles_per_second;
        
        bytes_per_second / 1024.0 // KB/s
    }
}

// Performance analyzer for token timing
pub struct TokenPerformanceAnalyzer {
    measurements: Vec<u16>,
    max_samples: usize,
}

impl TokenPerformanceAnalyzer {
    pub fn new(max_samples: usize) -> Self {
        Self {
            measurements: Vec::with_capacity(max_samples),
            max_samples,
        }
    }

    pub fn add_measurement(&mut self, trt: u16) {
        if self.measurements.len() >= self.max_samples {
            self.measurements.remove(0);
        }
        self.measurements.push(trt);
    }

    pub fn get_statistics(&self) -> Option<TokenStatistics> {
        if self.measurements.is_empty() {
            return None;
        }

        let sum: u32 = self.measurements.iter().map(|&x| x as u32).sum();
        let avg = sum / self.measurements.len() as u32;
        
        let min = *self.measurements.iter().min()?;
        let max = *self.measurements.iter().max()?;
        
        // Calculate jitter (standard deviation)
        let variance: f32 = self.measurements.iter()
            .map(|&x| {
                let diff = x as f32 - avg as f32;
                diff * diff
            })
            .sum::<f32>() / self.measurements.len() as f32;
        
        let jitter = variance.sqrt();

        Some(TokenStatistics {
            average: avg as u16,
            min,
            max,
            jitter,
            sample_count: self.measurements.len(),
        })
    }
}

#[derive(Debug)]
pub struct TokenStatistics {
    pub average: u16,
    pub min: u16,
    pub max: u16,
    pub jitter: f32,
    pub sample_count: usize,
}

impl fmt::Display for TokenStatistics {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Token Statistics (n={}):\n  Average: {}\n  Min: {}\n  Max: {}\n  Jitter: {:.2}",
            self.sample_count, self.average, self.min, self.max, self.jitter
        )
    }
}

// Example usage
fn main() {
    let mut config = ProfibusConfig::new(2, 1_500_000); // 1.5 Mbaud
    
    // Add slaves
    config.add_slave(SlaveConfig {
        station_address: 10,
        poll_interval: 10,
        watchdog_time: 1000,
        data_length: 8,
        active: true,
    });
    
    config.add_slave(SlaveConfig {
        station_address: 11,
        poll_interval: 10,
        watchdog_time: 1000,
        data_length: 16,
        active: true,
    });
    
    config.add_slave(SlaveConfig {
        station_address: 12,
        poll_interval: 10,
        watchdog_time: 1000,
        data_length: 4,
        active: true,
    });
    
    // Configure optimal timing for 2 masters
    config.configure_optimal_timing(2);
    
    println!("\nTheoretical throughput: {:.2} KB/s", config.calculate_throughput());
    
    // Simulate performance monitoring
    let mut analyzer = TokenPerformanceAnalyzer::new(100);
    
    for i in 0..10 {
        let simulated_trt = config.timing.target_rotation_time + (i % 3) * 10;
        analyzer.add_measurement(simulated_trt);
        
        if let Err(e) = config.monitor_token_performance(simulated_trt) {
            eprintln!("Performance issue: {}", e);
        }
    }
    
    if let Some(stats) = analyzer.get_statistics() {
        println!("\n{}", stats);
    }
}
```

## Summary

Token Rotation Time Optimization is essential for maximizing Profibus network performance and ensuring deterministic real-time behavior. Key takeaways include:

- **TRT determines system responsiveness**: Lower token rotation times enable faster update rates and reduced latency in control applications
- **Optimization is multi-faceted**: Involves tuning TTR, token hold time, slot time, GAP factor, baud rate, and network topology
- **Trade-offs exist**: Aggressive optimization can reduce system stability margins; balance performance with reliability
- **Monitoring is critical**: Continuous measurement of actual TRT helps identify performance degradation and optimization opportunities
- **Configuration must match application needs**: Safety-critical systems may require conservative settings, while high-speed applications benefit from aggressive optimization

The provided code examples demonstrate practical approaches to calculating optimal timing parameters, monitoring performance, and dynamically adjusting configuration to maintain optimal token rotation performance in Profibus networks.