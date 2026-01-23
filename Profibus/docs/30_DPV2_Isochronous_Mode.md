# DPV2 Isochronous Mode: Time-Synchronized Cyclic Data Exchange

## Detailed Description

**DPV2 Isochronous Mode** is an advanced feature of the PROFIBUS protocol specifically designed for applications requiring precise time synchronization and deterministic cyclic data exchange. This mode is critical for motion control applications where multiple axes or devices must operate in perfect synchronization.

### Key Concepts

**Isochronous Operation** means that all participating devices exchange data within strictly defined time windows, ensuring that all slaves read inputs and write outputs at exactly the same time. This synchronization is essential for:

- Multi-axis motion control systems
- Coordinated robotics
- Electronic line shafting
- Print registration systems
- Flying shear applications

**Time Synchronization Architecture:**
The DPV1 Class 2 master (typically a PLC or motion controller) acts as the clock master, broadcasting a global control telegram that synchronizes all isochronous slaves. The cycle is divided into deterministic phases:

1. **Freeze phase**: All slaves simultaneously latch their input data
2. **Data exchange phase**: Master exchanges data with all slaves
3. **Sync phase**: All slaves simultaneously update their outputs

**Timing Parameters:**
- **T_dp**: Data cycle time (typically 1-32ms)
- **T_i**: Isochronous cycle time (must be multiple of T_dp)
- **Jitter**: Timing deviation (typically <1μs)

### Programming Considerations

When implementing DPV2 Isochronous Mode, developers must handle:

- Precise timing control and cycle monitoring
- Global control commands (Freeze, Sync)
- Status monitoring for timing violations
- Clock synchronization management
- Cycle counter management for diagnostics

---

## C/C++ Implementation Example

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// DPV2 Isochronous Mode Constants
#define PROFIBUS_FREEZE_CMD     0x02
#define PROFIBUS_SYNC_CMD       0x04
#define MAX_ISOCHRONOUS_SLAVES  32
#define TIMING_VIOLATION_ALARM  0x8000

// Isochronous timing configuration
typedef struct {
    uint32_t cycle_time_us;      // Isochronous cycle time in microseconds
    uint32_t data_freeze_offset; // Offset for freeze command
    uint32_t data_sync_offset;   // Offset for sync command
    uint16_t tolerance_us;       // Acceptable timing jitter
} IsocConfig_t;

// Isochronous slave status
typedef struct {
    uint8_t  slave_addr;
    bool     isoc_enabled;
    uint16_t cycle_counter;
    uint32_t last_sync_time;
    bool     timing_violation;
    uint8_t  consecutive_errors;
} IsocSlaveStatus_t;

// Isochronous master controller
typedef struct {
    IsocConfig_t config;
    IsocSlaveStatus_t slaves[MAX_ISOCHRONOUS_SLAVES];
    uint8_t active_slave_count;
    uint16_t global_cycle_counter;
    bool isoc_active;
    uint32_t cycle_start_time;
} IsocMaster_t;

// Global control telegram structure
typedef struct {
    uint8_t command;           // FREEZE or SYNC
    uint16_t cycle_counter;    // Incremented each cycle
    uint32_t timestamp;        // Master clock timestamp
    uint8_t control_flags;     // Additional control bits
} GlobalControlTelegram_t;

// Initialize isochronous master
void isoc_master_init(IsocMaster_t *master, uint32_t cycle_time_us) {
    memset(master, 0, sizeof(IsocMaster_t));
    master->config.cycle_time_us = cycle_time_us;
    master->config.data_freeze_offset = cycle_time_us / 10;
    master->config.data_sync_offset = cycle_time_us * 9 / 10;
    master->config.tolerance_us = 10; // 10us jitter tolerance
}

// Add slave to isochronous group
int isoc_add_slave(IsocMaster_t *master, uint8_t slave_addr) {
    if (master->active_slave_count >= MAX_ISOCHRONOUS_SLAVES) {
        return -1; // Too many slaves
    }
    
    IsocSlaveStatus_t *slave = &master->slaves[master->active_slave_count];
    slave->slave_addr = slave_addr;
    slave->isoc_enabled = false;
    slave->cycle_counter = 0;
    slave->consecutive_errors = 0;
    
    master->active_slave_count++;
    return 0;
}

// Send global control command
int send_global_control(IsocMaster_t *master, uint8_t command, 
                       uint32_t current_time_us) {
    GlobalControlTelegram_t telegram;
    telegram.command = command;
    telegram.cycle_counter = master->global_cycle_counter;
    telegram.timestamp = current_time_us;
    telegram.control_flags = 0;
    
    // Broadcast to all isochronous slaves
    // In real implementation, this would use PROFIBUS broadcast mechanism
    for (int i = 0; i < master->active_slave_count; i++) {
        if (master->slaves[i].isoc_enabled) {
            // Send telegram via PROFIBUS (pseudo-code)
            // profibus_broadcast(&telegram, sizeof(telegram));
            master->slaves[i].last_sync_time = current_time_us;
        }
    }
    
    return 0;
}

// Execute one isochronous cycle
void isoc_cycle_execute(IsocMaster_t *master, uint32_t current_time_us) {
    uint32_t elapsed_time;
    
    if (!master->isoc_active) {
        master->cycle_start_time = current_time_us;
        master->isoc_active = true;
        master->global_cycle_counter = 0;
        return;
    }
    
    elapsed_time = current_time_us - master->cycle_start_time;
    
    // Check for cycle timing
    if (elapsed_time >= master->config.cycle_time_us) {
        // Start new cycle
        master->global_cycle_counter++;
        master->cycle_start_time = current_time_us;
        
        // Phase 1: Send FREEZE command
        send_global_control(master, PROFIBUS_FREEZE_CMD, current_time_us);
        
    } else if (elapsed_time >= master->config.data_sync_offset) {
        // Phase 3: Send SYNC command
        send_global_control(master, PROFIBUS_SYNC_CMD, current_time_us);
    }
    
    // Monitor timing violations
    for (int i = 0; i < master->active_slave_count; i++) {
        IsocSlaveStatus_t *slave = &master->slaves[i];
        if (slave->isoc_enabled) {
            uint32_t time_since_sync = current_time_us - slave->last_sync_time;
            if (time_since_sync > master->config.cycle_time_us + 
                master->config.tolerance_us) {
                slave->timing_violation = true;
                slave->consecutive_errors++;
                
                // Disable slave after 3 consecutive errors
                if (slave->consecutive_errors > 3) {
                    slave->isoc_enabled = false;
                }
            } else {
                slave->consecutive_errors = 0;
                slave->timing_violation = false;
            }
        }
    }
}

// Enable isochronous mode for a slave
int isoc_enable_slave(IsocMaster_t *master, uint8_t slave_addr) {
    for (int i = 0; i < master->active_slave_count; i++) {
        if (master->slaves[i].slave_addr == slave_addr) {
            master->slaves[i].isoc_enabled = true;
            master->slaves[i].cycle_counter = master->global_cycle_counter;
            return 0;
        }
    }
    return -1; // Slave not found
}

// Example usage
int main(void) {
    IsocMaster_t master;
    uint32_t simulated_time_us = 0;
    
    // Initialize with 4ms cycle time
    isoc_master_init(&master, 4000);
    
    // Add slaves
    isoc_add_slave(&master, 3);
    isoc_add_slave(&master, 5);
    isoc_add_slave(&master, 7);
    
    // Enable isochronous mode
    isoc_enable_slave(&master, 3);
    isoc_enable_slave(&master, 5);
    isoc_enable_slave(&master, 7);
    
    // Simulate 10 cycles
    for (int cycle = 0; cycle < 10; cycle++) {
        isoc_cycle_execute(&master, simulated_time_us);
        simulated_time_us += 4000; // Advance 4ms
        
        printf("Cycle %d: Global counter = %d\n", 
               cycle, master.global_cycle_counter);
    }
    
    return 0;
}
```

---

## Rust Implementation Example

```rust
use std::time::{Duration, Instant};
use std::collections::HashMap;

// DPV2 Isochronous Mode Constants
const PROFIBUS_FREEZE_CMD: u8 = 0x02;
const PROFIBUS_SYNC_CMD: u8 = 0x04;
const MAX_CONSECUTIVE_ERRORS: u8 = 3;

#[derive(Debug, Clone)]
struct IsocConfig {
    cycle_time: Duration,
    data_freeze_offset: Duration,
    data_sync_offset: Duration,
    tolerance: Duration,
}

impl IsocConfig {
    fn new(cycle_time_us: u64) -> Self {
        let cycle_time = Duration::from_micros(cycle_time_us);
        Self {
            cycle_time,
            data_freeze_offset: cycle_time / 10,
            data_sync_offset: cycle_time * 9 / 10,
            tolerance: Duration::from_micros(10),
        }
    }
}

#[derive(Debug, Clone)]
struct IsocSlaveStatus {
    slave_addr: u8,
    isoc_enabled: bool,
    cycle_counter: u16,
    last_sync_time: Option<Instant>,
    timing_violation: bool,
    consecutive_errors: u8,
}

impl IsocSlaveStatus {
    fn new(slave_addr: u8) -> Self {
        Self {
            slave_addr,
            isoc_enabled: false,
            cycle_counter: 0,
            last_sync_time: None,
            timing_violation: false,
            consecutive_errors: 0,
        }
    }
}

#[derive(Debug, Clone)]
struct GlobalControlTelegram {
    command: u8,
    cycle_counter: u16,
    timestamp: Instant,
    control_flags: u8,
}

struct IsocMaster {
    config: IsocConfig,
    slaves: HashMap<u8, IsocSlaveStatus>,
    global_cycle_counter: u16,
    isoc_active: bool,
    cycle_start_time: Option<Instant>,
    last_freeze_sent: bool,
    last_sync_sent: bool,
}

impl IsocMaster {
    fn new(cycle_time_us: u64) -> Self {
        Self {
            config: IsocConfig::new(cycle_time_us),
            slaves: HashMap::new(),
            global_cycle_counter: 0,
            isoc_active: false,
            cycle_start_time: None,
            last_freeze_sent: false,
            last_sync_sent: false,
        }
    }

    fn add_slave(&mut self, slave_addr: u8) -> Result<(), &'static str> {
        if self.slaves.contains_key(&slave_addr) {
            return Err("Slave already exists");
        }
        
        self.slaves.insert(slave_addr, IsocSlaveStatus::new(slave_addr));
        Ok(())
    }

    fn enable_slave(&mut self, slave_addr: u8) -> Result<(), &'static str> {
        let slave = self.slaves.get_mut(&slave_addr)
            .ok_or("Slave not found")?;
        
        slave.isoc_enabled = true;
        slave.cycle_counter = self.global_cycle_counter;
        Ok(())
    }

    fn send_global_control(&mut self, command: u8, current_time: Instant) {
        let telegram = GlobalControlTelegram {
            command,
            cycle_counter: self.global_cycle_counter,
            timestamp: current_time,
            control_flags: 0,
        };

        // Broadcast to all isochronous slaves
        for (_, slave) in self.slaves.iter_mut() {
            if slave.isoc_enabled {
                // In real implementation: profibus_broadcast(&telegram)
                slave.last_sync_time = Some(current_time);
                println!(
                    "Sent {} command to slave {} (cycle: {})",
                    if command == PROFIBUS_FREEZE_CMD { "FREEZE" } else { "SYNC" },
                    slave.slave_addr,
                    self.global_cycle_counter
                );
            }
        }
    }

    fn check_timing_violations(&mut self, current_time: Instant) {
        let max_elapsed = self.config.cycle_time + self.config.tolerance;

        for (_, slave) in self.slaves.iter_mut() {
            if !slave.isoc_enabled {
                continue;
            }

            if let Some(last_sync) = slave.last_sync_time {
                let elapsed = current_time.duration_since(last_sync);
                
                if elapsed > max_elapsed {
                    slave.timing_violation = true;
                    slave.consecutive_errors += 1;

                    println!(
                        "⚠ Timing violation on slave {}: {}μs exceeded",
                        slave.slave_addr,
                        elapsed.as_micros()
                    );

                    if slave.consecutive_errors > MAX_CONSECUTIVE_ERRORS {
                        slave.isoc_enabled = false;
                        println!(
                            "✗ Slave {} disabled due to repeated timing violations",
                            slave.slave_addr
                        );
                    }
                } else {
                    slave.timing_violation = false;
                    slave.consecutive_errors = 0;
                }
            }
        }
    }

    fn execute_cycle(&mut self, current_time: Instant) {
        if !self.isoc_active {
            self.cycle_start_time = Some(current_time);
            self.isoc_active = true;
            self.global_cycle_counter = 0;
            self.last_freeze_sent = false;
            self.last_sync_sent = false;
            return;
        }

        let cycle_start = self.cycle_start_time.unwrap();
        let elapsed = current_time.duration_since(cycle_start);

        // Phase 1: FREEZE command at start of cycle
        if elapsed >= Duration::ZERO && !self.last_freeze_sent {
            self.send_global_control(PROFIBUS_FREEZE_CMD, current_time);
            self.last_freeze_sent = true;
        }

        // Phase 2: Data exchange happens here (handled by application)

        // Phase 3: SYNC command near end of cycle
        if elapsed >= self.config.data_sync_offset && !self.last_sync_sent {
            self.send_global_control(PROFIBUS_SYNC_CMD, current_time);
            self.last_sync_sent = true;
        }

        // Start new cycle
        if elapsed >= self.config.cycle_time {
            self.global_cycle_counter = self.global_cycle_counter.wrapping_add(1);
            self.cycle_start_time = Some(current_time);
            self.last_freeze_sent = false;
            self.last_sync_sent = false;
        }

        // Monitor timing violations
        self.check_timing_violations(current_time);
    }

    fn get_status_report(&self) -> String {
        let mut report = format!(
            "Isochronous Mode Status:\n\
             Active: {}\n\
             Global Cycle Counter: {}\n\
             Configured Cycle Time: {}μs\n\
             Active Slaves: {}\n\n",
            self.isoc_active,
            self.global_cycle_counter,
            self.config.cycle_time.as_micros(),
            self.slaves.values().filter(|s| s.isoc_enabled).count()
        );

        for (addr, slave) in &self.slaves {
            report.push_str(&format!(
                "  Slave {}: {} | Errors: {} | Violation: {}\n",
                addr,
                if slave.isoc_enabled { "ENABLED " } else { "DISABLED" },
                slave.consecutive_errors,
                slave.timing_violation
            ));
        }

        report
    }
}

fn main() {
    println!("DPV2 Isochronous Mode Demonstration\n");
    
    // Create master with 4ms cycle time
    let mut master = IsocMaster::new(4000);
    
    // Add and enable slaves
    master.add_slave(3).unwrap();
    master.add_slave(5).unwrap();
    master.add_slave(7).unwrap();
    
    master.enable_slave(3).unwrap();
    master.enable_slave(5).unwrap();
    master.enable_slave(7).unwrap();
    
    let start_time = Instant::now();
    let mut current_time = start_time;
    
    // Simulate 10 cycles
    for cycle in 0..10 {
        master.execute_cycle(current_time);
        
        // Simulate 4ms advancement
        current_time += Duration::from_millis(4);
        
        println!("\n--- Cycle {} completed ---", cycle);
        std::thread::sleep(Duration::from_millis(100)); // Slow down for demo
    }
    
    println!("\n{}", master.get_status_report());
}
```

---

## Summary

**DPV2 Isochronous Mode** enables deterministic, time-synchronized data exchange across multiple PROFIBUS devices, which is essential for motion control applications requiring precise coordination. The mode operates through a three-phase cycle: FREEZE (simultaneous input latching), data exchange, and SYNC (simultaneous output update).

**Key implementation aspects include:**

- **Global control telegrams** broadcast by the master to synchronize all slaves
- **Cycle counter management** for tracking synchronization state
- **Timing violation detection** to identify slaves falling out of sync
- **Jitter tolerance** typically under 10 microseconds
- **Deterministic cycle times** ranging from 1-32ms depending on application needs

The code examples demonstrate how to implement an isochronous master controller that manages multiple slaves, broadcasts synchronization commands, monitors timing violations, and maintains cycle counters. Both C/C++ and Rust implementations show robust error handling, status monitoring, and the fundamental structure needed for real-time motion control applications.

This feature is critical for applications like multi-axis CNC machines, coordinated robotics, electronic line shafting in printing presses, and any system where multiple actuators must move in perfect synchronization.