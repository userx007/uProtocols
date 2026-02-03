# Profibus Slave Prioritization

## Detailed Description

Profibus Slave Prioritization is a mechanism for implementing priority schemes that ensure critical devices and time-sensitive data receive preferential treatment in communication cycles. In standard Profibus DP (Decentralized Periphery) operation, the master polls slaves in a round-robin fashion, which can introduce latency for critical devices. Prioritization techniques allow systems to meet stringent timing requirements for safety-critical applications, emergency systems, and high-priority process data.

### Key Concepts

**1. Token Rotation and Cycle Time**
- Profibus uses a token-passing mechanism between masters
- The token rotation time determines how often each master can communicate
- Target Token Rotation Time (TTR) must be configured to accommodate priority requirements

**2. Priority Mechanisms**

- **Polling Order Optimization**: Place high-priority slaves early in the polling sequence
- **Shortened Cycle Times**: Configure faster polling intervals for critical slaves
- **Group Organization**: Organize slaves into priority groups with different polling frequencies
- **Master-Slave Assignment**: Assign critical slaves to dedicated master stations
- **Freeze/Sync Commands**: Use for coordinated priority data exchange

**3. Critical Device Categories**
- Safety systems (emergency stops, interlocks)
- Real-time control loops (motion control, servo drives)
- Alarm and event reporting devices
- Standard I/O devices (lower priority)

**4. Implementation Strategies**

- **Static Prioritization**: Fixed polling order and intervals
- **Dynamic Prioritization**: Adjust polling based on device state or data age
- **Bandwidth Reservation**: Allocate specific bus bandwidth percentages to priority levels
- **Interrupt-Driven**: Some implementations support priority interrupt mechanisms

## Programming Implementation

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Priority levels
typedef enum {
    PRIORITY_CRITICAL = 0,    // Safety-critical devices
    PRIORITY_HIGH = 1,        // Time-sensitive control
    PRIORITY_NORMAL = 2,      // Standard I/O
    PRIORITY_LOW = 3          // Diagnostics, status
} PriorityLevel;

// Slave device structure
typedef struct {
    uint8_t address;
    PriorityLevel priority;
    uint16_t poll_interval_ms;    // Desired polling interval
    uint64_t last_poll_time;      // Last successful poll timestamp
    uint32_t timeout_ms;          // Communication timeout
    bool is_critical;
    uint8_t consecutive_failures;
    uint8_t data_buffer[244];     // Max Profibus DP data
    uint16_t data_length;
} ProfibusSlaveDevice;

// Priority queue node
typedef struct PriorityNode {
    ProfibusSlaveDevice* slave;
    struct PriorityNode* next;
} PriorityNode;

// Master configuration
typedef struct {
    uint32_t target_rotation_time_ms;
    uint32_t min_slave_interval_ms;
    uint8_t max_retries;
    PriorityNode* priority_queue[4];  // One queue per priority level
} ProfibusMaster;

// Get current timestamp in milliseconds
uint64_t get_timestamp_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Initialize master
void init_profibus_master(Profibusmaster* master, uint32_t ttr_ms) {
    master->target_rotation_time_ms = ttr_ms;
    master->min_slave_interval_ms = 10;
    master->max_retries = 3;
    
    for (int i = 0; i < 4; i++) {
        master->priority_queue[i] = NULL;
    }
}

// Add slave to priority queue
void add_slave_to_queue(Profibusmaster* master, ProfibusSlaveDevice* slave) {
    PriorityNode* node = (PriorityNode*)malloc(sizeof(PriorityNode));
    node->slave = slave;
    node->next = NULL;
    
    PriorityLevel priority = slave->priority;
    
    if (master->priority_queue[priority] == NULL) {
        master->priority_queue[priority] = node;
    } else {
        PriorityNode* current = master->priority_queue[priority];
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = node;
    }
}

// Check if slave needs polling based on priority and time
bool slave_needs_polling(ProfibusSlaveDevice* slave, uint64_t current_time) {
    uint64_t elapsed = current_time - slave->last_poll_time;
    return elapsed >= slave->poll_interval_ms;
}

// Simulate Profibus data exchange
int profibus_exchange_data(ProfibusSlaveDevice* slave, 
                           const uint8_t* output_data, 
                           uint16_t output_len,
                           uint8_t* input_data,
                           uint16_t* input_len) {
    // Simulate communication delay
    usleep(1000 + (rand() % 2000)); // 1-3ms
    
    // Simulate occasional failures for non-critical devices
    if (!slave->is_critical && (rand() % 100) < 5) {
        return -1; // 5% failure rate for simulation
    }
    
    // Copy simulated input data
    *input_len = slave->data_length;
    memcpy(input_data, slave->data_buffer, slave->data_length);
    
    return 0; // Success
}

// Priority-based polling scheduler
void profibus_priority_scheduler(Profibusmaster* master) {
    uint64_t cycle_start = get_timestamp_ms();
    uint64_t cycle_budget = master->target_rotation_time_ms;
    
    printf("\n=== Polling Cycle Start (Budget: %u ms) ===\n", cycle_budget);
    
    // Process each priority level in order
    for (int priority = PRIORITY_CRITICAL; priority <= PRIORITY_LOW; priority++) {
        PriorityNode* current = master->priority_queue[priority];
        
        while (current != NULL) {
            ProfibusSlaveDevice* slave = current->slave;
            uint64_t current_time = get_timestamp_ms();
            
            // Check if we've exceeded cycle budget
            if ((current_time - cycle_start) >= cycle_budget) {
                printf("Cycle budget exceeded, deferring lower priority slaves\n");
                return;
            }
            
            // Check if this slave needs polling
            if (slave_needs_polling(slave, current_time)) {
                uint8_t output_data[244] = {0};
                uint8_t input_data[244];
                uint16_t input_len;
                
                printf("Polling slave %d (Priority: %d) - ", 
                       slave->address, slave->priority);
                
                int result = profibus_exchange_data(slave, output_data, 0, 
                                                   input_data, &input_len);
                
                if (result == 0) {
                    slave->last_poll_time = current_time;
                    slave->consecutive_failures = 0;
                    printf("SUCCESS (Time: %llu ms)\n", 
                           get_timestamp_ms() - cycle_start);
                } else {
                    slave->consecutive_failures++;
                    printf("FAILED (Failures: %d)\n", slave->consecutive_failures);
                    
                    // Handle critical device failures immediately
                    if (slave->is_critical && slave->consecutive_failures >= 2) {
                        printf("ALERT: Critical device %d communication failure!\n", 
                               slave->address);
                    }
                }
            }
            
            current = current->next;
        }
    }
    
    uint64_t cycle_time = get_timestamp_ms() - cycle_start;
    printf("=== Cycle Complete (Duration: %llu ms) ===\n\n", cycle_time);
}

// Example usage
int main() {
    Profibusmaster master;
    init_profibus_master(&master, 50); // 50ms target rotation time
    
    // Configure critical safety device (Emergency stop)
    ProfibusSlaveDevice emergency_stop = {
        .address = 3,
        .priority = PRIORITY_CRITICAL,
        .poll_interval_ms = 10,
        .last_poll_time = 0,
        .timeout_ms = 20,
        .is_critical = true,
        .consecutive_failures = 0,
        .data_length = 2
    };
    
    // Configure high-priority servo drive
    ProfibusSlaveDevice servo_drive = {
        .address = 5,
        .priority = PRIORITY_HIGH,
        .poll_interval_ms = 20,
        .last_poll_time = 0,
        .timeout_ms = 50,
        .is_critical = false,
        .consecutive_failures = 0,
        .data_length = 16
    };
    
    // Configure normal priority I/O module
    ProfibusSlaveDevice io_module = {
        .address = 10,
        .priority = PRIORITY_NORMAL,
        .poll_interval_ms = 50,
        .last_poll_time = 0,
        .timeout_ms = 100,
        .is_critical = false,
        .consecutive_failures = 0,
        .data_length = 8
    };
    
    // Configure low priority diagnostic device
    ProfibusSlaveDevice diagnostic = {
        .address = 15,
        .priority = PRIORITY_LOW,
        .poll_interval_ms = 100,
        .last_poll_time = 0,
        .timeout_ms = 200,
        .is_critical = false,
        .consecutive_failures = 0,
        .data_length = 32
    };
    
    // Add slaves to priority queues
    add_slave_to_queue(&master, &emergency_stop);
    add_slave_to_queue(&master, &servo_drive);
    add_slave_to_queue(&master, &io_module);
    add_slave_to_queue(&master, &diagnostic);
    
    // Run several polling cycles
    for (int cycle = 0; cycle < 5; cycle++) {
        profibus_priority_scheduler(&master);
        usleep(10000); // 10ms between cycles
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::thread;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum PriorityLevel {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3,
}

#[derive(Debug, Clone)]
struct ProfibusSlaveDevice {
    address: u8,
    priority: PriorityLevel,
    poll_interval: Duration,
    last_poll_time: Option<Instant>,
    timeout: Duration,
    is_critical: bool,
    consecutive_failures: u8,
    data_buffer: Vec<u8>,
}

impl ProfibusSlaveDevice {
    fn new(
        address: u8,
        priority: PriorityLevel,
        poll_interval_ms: u64,
        is_critical: bool,
    ) -> Self {
        Self {
            address,
            priority,
            poll_interval: Duration::from_millis(poll_interval_ms),
            last_poll_time: None,
            timeout: Duration::from_millis(poll_interval_ms * 2),
            is_critical,
            consecutive_failures: 0,
            data_buffer: Vec::new(),
        }
    }

    fn needs_polling(&self, current_time: Instant) -> bool {
        match self.last_poll_time {
            None => true,
            Some(last_poll) => current_time.duration_since(last_poll) >= self.poll_interval,
        }
    }
}

struct Profibusmaster {
    target_rotation_time: Duration,
    min_slave_interval: Duration,
    max_retries: u8,
    priority_queues: HashMap<PriorityLevel, Vec<ProfibusSlaveDevice>>,
}

impl Profibusmaster {
    fn new(target_rotation_time_ms: u64) -> Self {
        let mut priority_queues = HashMap::new();
        priority_queues.insert(PriorityLevel::Critical, Vec::new());
        priority_queues.insert(PriorityLevel::High, Vec::new());
        priority_queues.insert(PriorityLevel::Normal, Vec::new());
        priority_queues.insert(PriorityLevel::Low, Vec::new());

        Self {
            target_rotation_time: Duration::from_millis(target_rotation_time_ms),
            min_slave_interval: Duration::from_millis(10),
            max_retries: 3,
            priority_queues,
        }
    }

    fn add_slave(&mut self, slave: ProfibusSlaveDevice) {
        if let Some(queue) = self.priority_queues.get_mut(&slave.priority) {
            queue.push(slave);
        }
    }

    fn exchange_data(
        &self,
        slave: &ProfibusSlaveDevice,
        output_data: &[u8],
    ) -> Result<Vec<u8>, String> {
        // Simulate communication delay
        thread::sleep(Duration::from_millis(1 + (rand::random::<u64>() % 2)));

        // Simulate occasional failures for non-critical devices
        if !slave.is_critical && (rand::random::<u8>() % 100) < 5 {
            return Err("Communication failure".to_string());
        }

        // Return simulated input data
        Ok(slave.data_buffer.clone())
    }

    fn priority_scheduler(&mut self) {
        let cycle_start = Instant::now();
        let cycle_budget = self.target_rotation_time;

        println!("\n=== Polling Cycle Start (Budget: {:?}) ===", cycle_budget);

        // Process each priority level in order
        let priorities = vec![
            PriorityLevel::Critical,
            PriorityLevel::High,
            PriorityLevel::Normal,
            PriorityLevel::Low,
        ];

        for priority in priorities {
            if let Some(slaves) = self.priority_queues.get_mut(&priority) {
                for slave in slaves.iter_mut() {
                    let current_time = Instant::now();

                    // Check if we've exceeded cycle budget
                    if current_time.duration_since(cycle_start) >= cycle_budget {
                        println!("Cycle budget exceeded, deferring lower priority slaves");
                        return;
                    }

                    // Check if this slave needs polling
                    if slave.needs_polling(current_time) {
                        print!(
                            "Polling slave {} (Priority: {:?}) - ",
                            slave.address, slave.priority
                        );

                        let output_data: Vec<u8> = vec![0; 0];
                        let result = self.exchange_data(slave, &output_data);

                        match result {
                            Ok(_data) => {
                                slave.last_poll_time = Some(current_time);
                                slave.consecutive_failures = 0;
                                let elapsed = Instant::now().duration_since(cycle_start);
                                println!("SUCCESS (Time: {:?})", elapsed);
                            }
                            Err(_err) => {
                                slave.consecutive_failures += 1;
                                println!("FAILED (Failures: {})", slave.consecutive_failures);

                                // Handle critical device failures immediately
                                if slave.is_critical && slave.consecutive_failures >= 2 {
                                    println!(
                                        "ALERT: Critical device {} communication failure!",
                                        slave.address
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }

        let cycle_time = Instant::now().duration_since(cycle_start);
        println!("=== Cycle Complete (Duration: {:?}) ===\n", cycle_time);
    }
}

fn main() {
    let mut master = Profibusmaster::new(50); // 50ms target rotation time

    // Configure critical safety device (Emergency stop)
    let emergency_stop = ProfibusSlaveDevice::new(
        3,
        PriorityLevel::Critical,
        10,
        true,
    );

    // Configure high-priority servo drive
    let servo_drive = ProfibusSlaveDevice::new(
        5,
        PriorityLevel::High,
        20,
        false,
    );

    // Configure normal priority I/O module
    let io_module = ProfibusSlaveDevice::new(
        10,
        PriorityLevel::Normal,
        50,
        false,
    );

    // Configure low priority diagnostic device
    let diagnostic = ProfibusSlaveDevice::new(
        15,
        PriorityLevel::Low,
        100,
        false,
    );

    // Add slaves to priority queues
    master.add_slave(emergency_stop);
    master.add_slave(servo_drive);
    master.add_slave(io_module);
    master.add_slave(diagnostic);

    // Run several polling cycles
    for _cycle in 0..5 {
        master.priority_scheduler();
        thread::sleep(Duration::from_millis(10));
    }
}
```

## Summary

**Profibus Slave Prioritization** enables deterministic communication for critical industrial applications by implementing priority-based polling schemes. The key implementation strategies include:

1. **Multi-level Priority Queues**: Organize slaves into priority tiers (Critical, High, Normal, Low) with different polling frequencies
2. **Time-Budget Management**: Enforce target token rotation times while ensuring high-priority devices are polled within their timing requirements
3. **Failure Handling**: Implement enhanced monitoring for critical devices with immediate alerting on communication failures
4. **Dynamic Scheduling**: Poll devices based on elapsed time since last communication, ensuring critical devices receive preferential bandwidth

The provided implementations demonstrate practical prioritization algorithms that can handle safety-critical devices (emergency stops), time-sensitive control systems (servo drives), standard I/O, and diagnostic devices within a single Profibus network. This approach ensures that critical systems receive deterministic, low-latency communication while still servicing lower-priority devices when bandwidth permits, making it essential for safety-critical industrial automation applications.