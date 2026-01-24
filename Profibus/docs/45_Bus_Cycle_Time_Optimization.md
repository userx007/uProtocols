# Bus Cycle Time Optimization in Profibus

## Detailed Description

Bus cycle time optimization is a critical aspect of Profibus network design that directly impacts real-time performance and deterministic behavior of industrial automation systems. The bus cycle time represents the total duration required for the master to complete one complete communication cycle with all active slaves on the network.

### Understanding Bus Cycle Time

The bus cycle time consists of several components:

1. **Token rotation time** - Time for the token to circulate among all masters
2. **Data exchange time** - Time required to poll all slaves and exchange data
3. **Gap times** - Idle time between transmissions (slot time, synchronization gaps)
4. **Protocol overhead** - Frame headers, acknowledgments, and error checking

The total bus cycle time can be calculated as:

```
T_cycle = T_token + Σ(T_slave_i) + T_gaps + T_overhead
```

Where:
- `T_token` = Token rotation time among masters
- `T_slave_i` = Communication time with each slave
- `T_gaps` = Sum of all gap times
- `T_overhead` = Protocol overhead

### Key Optimization Strategies

1. **Baud rate selection** - Higher speeds reduce transmission times but require proper cable quality
2. **Station addressing** - Sequential addressing minimizes token passing overhead
3. **GAP factor tuning** - Balancing between determinism and throughput
4. **Data payload optimization** - Minimizing unnecessary data transfers
5. **Watchdog timer configuration** - Preventing excessive timeout delays

## C/C++ Code Examples

### Bus Cycle Time Calculator

```c
#include <stdio.h>
#include <stdint.h>
#include <math.h>

// Profibus DP configuration structure
typedef struct {
    uint32_t baud_rate;           // bits per second
    uint8_t num_slaves;
    uint8_t num_masters;
    uint16_t *input_data_length;  // bytes per slave
    uint16_t *output_data_length; // bytes per slave
    uint8_t gap_factor;
    uint16_t slot_time;           // bit times
} profibus_config_t;

// Calculate transmission time for a frame
double calculate_frame_time(uint16_t data_bytes, uint32_t baud_rate) {
    // Profibus DP frame overhead:
    // Start delimiter (1) + Destination (1) + Source (1) + 
    // Function code (1) + Data (N) + FCS (1) + End delimiter (1)
    const uint8_t FRAME_OVERHEAD = 6;
    uint16_t total_bits = (data_bytes + FRAME_OVERHEAD) * 11; // 11 bits per byte (1 start + 8 data + 1 parity + 1 stop)
    
    return (double)total_bits / baud_rate; // seconds
}

// Calculate bus cycle time
double calculate_bus_cycle_time(profibus_config_t *config) {
    double cycle_time = 0.0;
    
    // Token rotation time (simplified model)
    double token_time = (config->num_masters > 1) ? 
        (config->num_masters * 33.0 * 11.0 / config->baud_rate) : 0.0;
    
    cycle_time += token_time;
    
    // Time for each slave communication
    for (uint8_t i = 0; i < config->num_slaves; i++) {
        // Request frame time
        double request_time = calculate_frame_time(0, config->baud_rate);
        
        // Response frame time
        uint16_t response_bytes = config->input_data_length[i] + 
                                  config->output_data_length[i];
        double response_time = calculate_frame_time(response_bytes, config->baud_rate);
        
        // Slot time (synchronization gap)
        double slot = (double)(config->slot_time * 11) / config->baud_rate;
        
        cycle_time += request_time + response_time + slot;
    }
    
    // GAP update time
    double gap_time = (double)(config->gap_factor * config->slot_time * 11) / 
                      config->baud_rate;
    cycle_time += gap_time;
    
    return cycle_time * 1000.0; // Convert to milliseconds
}

// Optimize bus cycle time
void optimize_bus_cycle(profibus_config_t *config, double target_cycle_ms) {
    printf("=== Bus Cycle Time Optimization ===\n\n");
    
    double current_cycle = calculate_bus_cycle_time(config);
    printf("Current cycle time: %.3f ms\n", current_cycle);
    printf("Target cycle time: %.3f ms\n\n", target_cycle_ms);
    
    if (current_cycle <= target_cycle_ms) {
        printf("Current configuration meets target.\n");
        return;
    }
    
    // Optimization strategies
    printf("Optimization recommendations:\n");
    
    // 1. Try higher baud rate
    uint32_t baud_rates[] = {93750, 187500, 500000, 1500000, 3000000, 6000000, 12000000};
    for (int i = 0; i < 7; i++) {
        if (baud_rates[i] > config->baud_rate) {
            uint32_t old_baud = config->baud_rate;
            config->baud_rate = baud_rates[i];
            double new_cycle = calculate_bus_cycle_time(config);
            
            printf("  - Increase baud rate to %u bps: %.3f ms (%.1f%% improvement)\n",
                   baud_rates[i], new_cycle, 
                   (current_cycle - new_cycle) / current_cycle * 100);
            
            if (new_cycle <= target_cycle_ms) {
                printf("    ✓ Target achieved!\n");
                return;
            }
            config->baud_rate = old_baud;
        }
    }
    
    // 2. Reduce GAP factor
    if (config->gap_factor > 1) {
        uint8_t old_gap = config->gap_factor;
        config->gap_factor = 1;
        double new_cycle = calculate_bus_cycle_time(config);
        printf("  - Reduce GAP factor to 1: %.3f ms (%.1f%% improvement)\n",
               new_cycle, (current_cycle - new_cycle) / current_cycle * 100);
        config->gap_factor = old_gap;
    }
    
    // 3. Optimize slot time
    if (config->slot_time > 100) {
        uint16_t old_slot = config->slot_time;
        config->slot_time = 100;
        double new_cycle = calculate_bus_cycle_time(config);
        printf("  - Reduce slot time to 100 bit times: %.3f ms (%.1f%% improvement)\n",
               new_cycle, (current_cycle - new_cycle) / current_cycle * 100);
        config->slot_time = old_slot;
    }
}

int main() {
    // Example configuration
    uint16_t input_lengths[] = {4, 8, 2, 4, 6};
    uint16_t output_lengths[] = {2, 4, 1, 2, 3};
    
    profibus_config_t config = {
        .baud_rate = 500000,          // 500 kbps
        .num_slaves = 5,
        .num_masters = 1,
        .input_data_length = input_lengths,
        .output_data_length = output_lengths,
        .gap_factor = 10,
        .slot_time = 200
    };
    
    optimize_bus_cycle(&config, 5.0); // Target: 5ms cycle time
    
    return 0;
}
```

### Real-Time Cycle Monitor

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

typedef struct {
    double min_cycle_time;
    double max_cycle_time;
    double avg_cycle_time;
    uint32_t total_cycles;
    uint32_t missed_deadlines;
    double jitter;
} cycle_statistics_t;

typedef struct {
    struct timespec start_time;
    struct timespec last_cycle_time;
    cycle_statistics_t stats;
    double target_cycle_ms;
    bool initialized;
} cycle_monitor_t;

void cycle_monitor_init(cycle_monitor_t *monitor, double target_ms) {
    monitor->stats.min_cycle_time = 1e6;
    monitor->stats.max_cycle_time = 0.0;
    monitor->stats.avg_cycle_time = 0.0;
    monitor->stats.total_cycles = 0;
    monitor->stats.missed_deadlines = 0;
    monitor->stats.jitter = 0.0;
    monitor->target_cycle_ms = target_ms;
    monitor->initialized = false;
}

double timespec_diff_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 + 
           (end->tv_nsec - start->tv_nsec) / 1e6;
}

void cycle_monitor_update(cycle_monitor_t *monitor) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    if (!monitor->initialized) {
        monitor->last_cycle_time = current_time;
        monitor->initialized = true;
        return;
    }
    
    double cycle_time = timespec_diff_ms(&monitor->last_cycle_time, &current_time);
    
    // Update statistics
    monitor->stats.total_cycles++;
    
    if (cycle_time < monitor->stats.min_cycle_time)
        monitor->stats.min_cycle_time = cycle_time;
    
    if (cycle_time > monitor->stats.max_cycle_time)
        monitor->stats.max_cycle_time = cycle_time;
    
    // Running average
    monitor->stats.avg_cycle_time = 
        (monitor->stats.avg_cycle_time * (monitor->stats.total_cycles - 1) + cycle_time) / 
        monitor->stats.total_cycles;
    
    // Jitter calculation (standard deviation approximation)
    double deviation = cycle_time - monitor->stats.avg_cycle_time;
    monitor->stats.jitter = 
        sqrt((monitor->stats.jitter * monitor->stats.jitter * (monitor->stats.total_cycles - 1) + 
              deviation * deviation) / monitor->stats.total_cycles);
    
    // Check for missed deadline
    if (cycle_time > monitor->target_cycle_ms) {
        monitor->stats.missed_deadlines++;
    }
    
    monitor->last_cycle_time = current_time;
}

void cycle_monitor_print(cycle_monitor_t *monitor) {
    printf("\n=== Bus Cycle Statistics ===\n");
    printf("Total cycles: %u\n", monitor->stats.total_cycles);
    printf("Min cycle time: %.3f ms\n", monitor->stats.min_cycle_time);
    printf("Max cycle time: %.3f ms\n", monitor->stats.max_cycle_time);
    printf("Avg cycle time: %.3f ms\n", monitor->stats.avg_cycle_time);
    printf("Jitter (std dev): %.3f ms\n", monitor->stats.jitter);
    printf("Missed deadlines: %u (%.2f%%)\n", 
           monitor->stats.missed_deadlines,
           100.0 * monitor->stats.missed_deadlines / monitor->stats.total_cycles);
    printf("Target cycle: %.3f ms\n", monitor->target_cycle_ms);
}
```

## Rust Code Examples

### Bus Cycle Time Optimizer

```rust
use std::fmt;

#[derive(Debug, Clone)]
pub struct ProfibusConfig {
    pub baud_rate: u32,
    pub num_slaves: u8,
    pub num_masters: u8,
    pub input_data_lengths: Vec<u16>,
    pub output_data_lengths: Vec<u16>,
    pub gap_factor: u8,
    pub slot_time: u16,
}

#[derive(Debug)]
pub struct CycleTimeResult {
    pub total_time_ms: f64,
    pub token_time_ms: f64,
    pub slave_time_ms: f64,
    pub gap_time_ms: f64,
}

impl fmt::Display for CycleTimeResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Total: {:.3}ms (Token: {:.3}ms, Slaves: {:.3}ms, Gaps: {:.3}ms)",
               self.total_time_ms, self.token_time_ms, 
               self.slave_time_ms, self.gap_time_ms)
    }
}

pub struct BusCycleOptimizer {
    config: ProfibusConfig,
}

impl BusCycleOptimizer {
    pub fn new(config: ProfibusConfig) -> Self {
        Self { config }
    }
    
    /// Calculate transmission time for a frame in seconds
    fn calculate_frame_time(&self, data_bytes: u16) -> f64 {
        const FRAME_OVERHEAD: u16 = 6; // SD, DA, SA, FC, FCS, ED
        const BITS_PER_BYTE: u16 = 11; // Start + 8 data + parity + stop
        
        let total_bits = (data_bytes + FRAME_OVERHEAD) * BITS_PER_BYTE;
        total_bits as f64 / self.config.baud_rate as f64
    }
    
    /// Calculate complete bus cycle time
    pub fn calculate_cycle_time(&self) -> CycleTimeResult {
        // Token rotation time
        let token_time = if self.config.num_masters > 1 {
            (self.config.num_masters as f64 * 33.0 * 11.0) / self.config.baud_rate as f64
        } else {
            0.0
        };
        
        // Slave communication time
        let mut slave_time = 0.0;
        for i in 0..self.config.num_slaves as usize {
            // Request frame
            let request_time = self.calculate_frame_time(0);
            
            // Response frame
            let response_bytes = self.config.input_data_lengths[i] + 
                                 self.config.output_data_lengths[i];
            let response_time = self.calculate_frame_time(response_bytes);
            
            // Slot time
            let slot = (self.config.slot_time as f64 * 11.0) / self.config.baud_rate as f64;
            
            slave_time += request_time + response_time + slot;
        }
        
        // GAP update time
        let gap_time = (self.config.gap_factor as f64 * 
                        self.config.slot_time as f64 * 11.0) / 
                       self.config.baud_rate as f64;
        
        let total_time = (token_time + slave_time + gap_time) * 1000.0; // Convert to ms
        
        CycleTimeResult {
            total_time_ms: total_time,
            token_time_ms: token_time * 1000.0,
            slave_time_ms: slave_time * 1000.0,
            gap_time_ms: gap_time * 1000.0,
        }
    }
    
    /// Optimize configuration to meet target cycle time
    pub fn optimize(&mut self, target_cycle_ms: f64) -> Vec<String> {
        let mut recommendations = Vec::new();
        let current_cycle = self.calculate_cycle_time();
        
        if current_cycle.total_time_ms <= target_cycle_ms {
            recommendations.push(format!(
                "✓ Current configuration meets target ({:.3}ms <= {:.3}ms)",
                current_cycle.total_time_ms, target_cycle_ms
            ));
            return recommendations;
        }
        
        recommendations.push(format!(
            "Current cycle time: {:.3}ms (target: {:.3}ms)",
            current_cycle.total_time_ms, target_cycle_ms
        ));
        
        // Try different baud rates
        let baud_rates = [93750, 187500, 500000, 1500000, 3000000, 6000000, 12000000];
        for &baud in &baud_rates {
            if baud > self.config.baud_rate {
                let old_baud = self.config.baud_rate;
                self.config.baud_rate = baud;
                let new_cycle = self.calculate_cycle_time();
                let improvement = (current_cycle.total_time_ms - new_cycle.total_time_ms) / 
                                  current_cycle.total_time_ms * 100.0;
                
                recommendations.push(format!(
                    "  Baud rate {} bps: {:.3}ms ({:.1}% improvement){}",
                    baud, new_cycle.total_time_ms, improvement,
                    if new_cycle.total_time_ms <= target_cycle_ms { " ✓" } else { "" }
                ));
                
                if new_cycle.total_time_ms <= target_cycle_ms {
                    return recommendations;
                }
                
                self.config.baud_rate = old_baud;
            }
        }
        
        // Optimize GAP factor
        if self.config.gap_factor > 1 {
            let old_gap = self.config.gap_factor;
            self.config.gap_factor = 1;
            let new_cycle = self.calculate_cycle_time();
            let improvement = (current_cycle.total_time_ms - new_cycle.total_time_ms) / 
                              current_cycle.total_time_ms * 100.0;
            
            recommendations.push(format!(
                "  GAP factor 1: {:.3}ms ({:.1}% improvement)",
                new_cycle.total_time_ms, improvement
            ));
            
            self.config.gap_factor = old_gap;
        }
        
        // Optimize slot time
        if self.config.slot_time > 100 {
            let old_slot = self.config.slot_time;
            self.config.slot_time = 100;
            let new_cycle = self.calculate_cycle_time();
            let improvement = (current_cycle.total_time_ms - new_cycle.total_time_ms) / 
                              current_cycle.total_time_ms * 100.0;
            
            recommendations.push(format!(
                "  Slot time 100: {:.3}ms ({:.1}% improvement)",
                new_cycle.total_time_ms, improvement
            ));
            
            self.config.slot_time = old_slot;
        }
        
        recommendations
    }
}

// Cycle time monitoring with statistics
pub struct CycleMonitor {
    target_cycle_ms: f64,
    min_cycle_ms: f64,
    max_cycle_ms: f64,
    sum_cycle_ms: f64,
    sum_squared_ms: f64,
    total_cycles: u64,
    missed_deadlines: u64,
    last_cycle_time: Option<std::time::Instant>,
}

impl CycleMonitor {
    pub fn new(target_cycle_ms: f64) -> Self {
        Self {
            target_cycle_ms,
            min_cycle_ms: f64::MAX,
            max_cycle_ms: 0.0,
            sum_cycle_ms: 0.0,
            sum_squared_ms: 0.0,
            total_cycles: 0,
            missed_deadlines: 0,
            last_cycle_time: None,
        }
    }
    
    pub fn update(&mut self) {
        let now = std::time::Instant::now();
        
        if let Some(last) = self.last_cycle_time {
            let cycle_ms = now.duration_since(last).as_secs_f64() * 1000.0;
            
            self.min_cycle_ms = self.min_cycle_ms.min(cycle_ms);
            self.max_cycle_ms = self.max_cycle_ms.max(cycle_ms);
            self.sum_cycle_ms += cycle_ms;
            self.sum_squared_ms += cycle_ms * cycle_ms;
            self.total_cycles += 1;
            
            if cycle_ms > self.target_cycle_ms {
                self.missed_deadlines += 1;
            }
        }
        
        self.last_cycle_time = Some(now);
    }
    
    pub fn get_statistics(&self) -> CycleStatistics {
        let avg = if self.total_cycles > 0 {
            self.sum_cycle_ms / self.total_cycles as f64
        } else {
            0.0
        };
        
        let variance = if self.total_cycles > 1 {
            (self.sum_squared_ms / self.total_cycles as f64) - (avg * avg)
        } else {
            0.0
        };
        
        CycleStatistics {
            min_ms: self.min_cycle_ms,
            max_ms: self.max_cycle_ms,
            avg_ms: avg,
            jitter_ms: variance.sqrt(),
            total_cycles: self.total_cycles,
            missed_deadlines: self.missed_deadlines,
            target_ms: self.target_cycle_ms,
        }
    }
}

#[derive(Debug)]
pub struct CycleStatistics {
    pub min_ms: f64,
    pub max_ms: f64,
    pub avg_ms: f64,
    pub jitter_ms: f64,
    pub total_cycles: u64,
    pub missed_deadlines: u64,
    pub target_ms: f64,
}

impl fmt::Display for CycleStatistics {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, 
            "Cycles: {}, Min: {:.3}ms, Max: {:.3}ms, Avg: {:.3}ms, Jitter: {:.3}ms, Missed: {} ({:.2}%)",
            self.total_cycles, self.min_ms, self.max_ms, self.avg_ms, 
            self.jitter_ms, self.missed_deadlines,
            if self.total_cycles > 0 {
                100.0 * self.missed_deadlines as f64 / self.total_cycles as f64
            } else {
                0.0
            }
        )
    }
}

fn main() {
    // Example configuration
    let config = ProfibusConfig {
        baud_rate: 500000,
        num_slaves: 5,
        num_masters: 1,
        input_data_lengths: vec![4, 8, 2, 4, 6],
        output_data_lengths: vec![2, 4, 1, 2, 3],
        gap_factor: 10,
        slot_time: 200,
    };
    
    let mut optimizer = BusCycleOptimizer::new(config);
    
    println!("=== Bus Cycle Time Optimization ===\n");
    let current = optimizer.calculate_cycle_time();
    println!("Current: {}\n", current);
    
    let recommendations = optimizer.optimize(5.0);
    println!("Optimization recommendations:");
    for rec in recommendations {
        println!("{}", rec);
    }
}
```

## Summary

**Bus cycle time optimization** is essential for achieving deterministic real-time performance in Profibus networks. The total cycle time encompasses token rotation, slave polling, gap times, and protocol overhead. Key optimization techniques include:

1. **Baud rate selection** - Higher speeds (up to 12 Mbps) reduce transmission times
2. **GAP factor tuning** - Lower values reduce idle time but may affect network stability
3. **Slot time minimization** - Reduces synchronization overhead between messages
4. **Data payload optimization** - Transmit only necessary data to minimize frame sizes
5. **Network topology** - Sequential addressing and minimal master count reduce overhead

The code examples demonstrate practical implementations for calculating cycle times, identifying bottlenecks, and automatically suggesting optimization strategies. Real-time monitoring capabilities track cycle statistics including minimum, maximum, average, and jitter measurements to ensure compliance with timing requirements.

Proper bus cycle optimization ensures predictable, deterministic behavior critical for time-sensitive industrial applications such as motion control, synchronized manufacturing processes, and safety systems where timing deviations can lead to coordination failures or hazardous conditions.