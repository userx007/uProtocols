# Multi-Drop Network Configuration in Modbus

## Overview

A multi-drop network configuration in Modbus refers to a topology where multiple slave (server) devices share a single communication bus with one or more masters (clients). This is one of Modbus's most powerful features, enabling cost-effective industrial automation by allowing dozens of devices to communicate over a single pair of wires (in RS-485) or a single network connection.

## Key Concepts

### Network Topology

In a multi-drop configuration:
- **One Master (Client)**: Initiates all communication and polls slaves sequentially
- **Multiple Slaves (Servers)**: Each has a unique address (1-247), respond only when addressed
- **Shared Medium**: All devices share the same physical bus (RS-485) or network segment

### Addressing Scheme

Each slave device must have a unique **Unit ID** (also called Slave Address):
- Valid range: 1-247 (0 is reserved for broadcast)
- Address 0: Broadcast address (slaves listen but don't respond)
- Addresses 248-255: Reserved for special purposes

### Bus Arbitration

Modbus uses a **master-slave** (client-server) arbitration model:
- Only the master initiates transactions
- Slaves never communicate directly with each other
- Slaves only respond when specifically addressed
- This eliminates collision issues inherent in peer-to-peer networks

### Physical Layer Considerations

For RS-485 multi-drop networks:
- **Termination resistors** (120Ω) at both ends of the bus
- **Biasing resistors** to maintain idle state
- **Maximum cable length**: ~1200m (4000ft) at lower baud rates
- **Maximum devices**: Typically 32 without repeaters (RS-485 limitation)
- **Proper grounding** and shielding for noise immunity

## Implementation Details

### Polling Strategy

The master must implement an efficient polling strategy:

1. **Sequential Polling**: Query each slave in order
2. **Priority Polling**: Query critical slaves more frequently
3. **Exception-Based**: Only query slaves when needed
4. **Round-Robin with Timeouts**: Move to next slave if one doesn't respond

### Timing Considerations

- **Response Timeout**: Time to wait for slave response (typically 100-1000ms)
- **Turnaround Delay**: Minimum time between frames (3.5 character times in RTU)
- **Inter-frame Gap**: Silence period that marks frame boundaries

## C/C++ Implementation Example

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_SLAVES 10
#define RESPONSE_TIMEOUT_MS 500
#define MAX_RETRIES 3

// Slave configuration structure
typedef struct {
    uint8_t unit_id;
    bool is_active;
    uint32_t error_count;
    uint32_t success_count;
} SlaveConfig;

// Multi-drop network manager
typedef struct {
    SlaveConfig slaves[MAX_SLAVES];
    uint8_t slave_count;
    uint8_t current_slave_index;
} ModbusNetwork;

// Initialize the multi-drop network
void modbus_network_init(ModbusNetwork *network) {
    memset(network, 0, sizeof(ModbusNetwork));
}

// Add a slave to the network
bool modbus_network_add_slave(ModbusNetwork *network, uint8_t unit_id) {
    if (network->slave_count >= MAX_SLAVES) {
        return false;
    }
    
    // Check for duplicate addresses
    for (int i = 0; i < network->slave_count; i++) {
        if (network->slaves[i].unit_id == unit_id) {
            return false;
        }
    }
    
    network->slaves[network->slave_count].unit_id = unit_id;
    network->slaves[network->slave_count].is_active = true;
    network->slave_count++;
    
    return true;
}

// Simulate reading holding registers from a specific slave
int modbus_read_holding_registers(uint8_t unit_id, uint16_t start_addr, 
                                   uint16_t count, uint16_t *dest) {
    // In real implementation, this would:
    // 1. Build Modbus RTU/TCP frame with unit_id
    // 2. Send frame over serial/network
    // 3. Wait for response with timeout
    // 4. Parse response and extract data
    
    printf("  Polling slave %d: Reading %d registers from address %d\n", 
           unit_id, count, start_addr);
    
    // Simulate response (in reality, parse actual Modbus response)
    for (int i = 0; i < count; i++) {
        dest[i] = 1000 + unit_id * 100 + i;
    }
    
    return 0; // Success
}

// Poll a single slave with retry logic
bool modbus_poll_slave(ModbusNetwork *network, uint8_t slave_index) {
    SlaveConfig *slave = &network->slaves[slave_index];
    uint16_t registers[10];
    int retries = 0;
    int result;
    
    while (retries < MAX_RETRIES) {
        result = modbus_read_holding_registers(slave->unit_id, 0, 5, registers);
        
        if (result == 0) {
            slave->success_count++;
            printf("  ✓ Slave %d responded: [%d, %d, %d, %d, %d]\n", 
                   slave->unit_id, registers[0], registers[1], 
                   registers[2], registers[3], registers[4]);
            return true;
        }
        
        retries++;
        printf("  ✗ Slave %d timeout, retry %d/%d\n", 
               slave->unit_id, retries, MAX_RETRIES);
    }
    
    slave->error_count++;
    slave->is_active = false; // Mark as inactive after failures
    return false;
}

// Sequential polling of all slaves
void modbus_poll_all_slaves(ModbusNetwork *network) {
    printf("\n=== Polling Cycle Started ===\n");
    
    for (int i = 0; i < network->slave_count; i++) {
        if (network->slaves[i].is_active) {
            modbus_poll_slave(network, i);
        }
    }
    
    printf("=== Polling Cycle Complete ===\n\n");
}

// Priority polling - poll critical slaves more frequently
void modbus_priority_poll(ModbusNetwork *network, uint8_t *priority_slaves, 
                          uint8_t priority_count) {
    printf("\n=== Priority Polling ===\n");
    
    // Poll priority slaves
    for (int i = 0; i < priority_count; i++) {
        for (int j = 0; j < network->slave_count; j++) {
            if (network->slaves[j].unit_id == priority_slaves[i]) {
                modbus_poll_slave(network, j);
                break;
            }
        }
    }
}

// Broadcast write (all slaves receive, none respond)
void modbus_broadcast_write(uint16_t address, uint16_t value) {
    uint8_t broadcast_id = 0;
    printf("\n=== Broadcast Write ===\n");
    printf("  Broadcasting to all slaves: Write %d to address %d\n", 
           value, address);
    printf("  (No responses expected)\n");
}

// Display network statistics
void modbus_display_statistics(ModbusNetwork *network) {
    printf("\n=== Network Statistics ===\n");
    for (int i = 0; i < network->slave_count; i++) {
        SlaveConfig *slave = &network->slaves[i];
        float success_rate = 0.0f;
        uint32_t total = slave->success_count + slave->error_count;
        
        if (total > 0) {
            success_rate = (float)slave->success_count / total * 100.0f;
        }
        
        printf("Slave %d: Success=%d, Errors=%d, Rate=%.1f%%, Status=%s\n",
               slave->unit_id, slave->success_count, slave->error_count,
               success_rate, slave->is_active ? "Active" : "Inactive");
    }
}

int main() {
    ModbusNetwork network;
    modbus_network_init(&network);
    
    // Configure multi-drop network with 5 slaves
    printf("=== Configuring Multi-Drop Network ===\n");
    modbus_network_add_slave(&network, 1);  // Temperature sensor
    modbus_network_add_slave(&network, 2);  // Pressure sensor
    modbus_network_add_slave(&network, 3);  // Flow meter
    modbus_network_add_slave(&network, 4);  // Valve controller
    modbus_network_add_slave(&network, 5);  // Motor drive
    printf("Added %d slaves to network\n", network.slave_count);
    
    // Sequential polling
    modbus_poll_all_slaves(&network);
    
    // Priority polling of critical devices
    uint8_t priority[] = {1, 3}; // Temperature and flow are critical
    modbus_priority_poll(&network, priority, 2);
    
    // Broadcast command
    modbus_broadcast_write(100, 5000);
    
    // Display statistics
    modbus_display_statistics(&network);
    
    return 0;
}
```

## Rust Implementation Example

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

const MAX_RETRIES: u8 = 3;
const RESPONSE_TIMEOUT: Duration = Duration::from_millis(500);

#[derive(Debug, Clone)]
struct SlaveConfig {
    unit_id: u8,
    is_active: bool,
    error_count: u32,
    success_count: u32,
    last_poll_time: Option<Instant>,
}

impl SlaveConfig {
    fn new(unit_id: u8) -> Self {
        Self {
            unit_id,
            is_active: true,
            error_count: 0,
            success_count: 0,
            last_poll_time: None,
        }
    }
    
    fn success_rate(&self) -> f32 {
        let total = self.success_count + self.error_count;
        if total == 0 {
            return 0.0;
        }
        (self.success_count as f32 / total as f32) * 100.0
    }
}

struct ModbusNetwork {
    slaves: HashMap<u8, SlaveConfig>,
    polling_order: Vec<u8>,
}

impl ModbusNetwork {
    fn new() -> Self {
        Self {
            slaves: HashMap::new(),
            polling_order: Vec::new(),
        }
    }
    
    fn add_slave(&mut self, unit_id: u8) -> Result<(), String> {
        if unit_id == 0 || unit_id > 247 {
            return Err(format!("Invalid unit ID: {}. Must be 1-247", unit_id));
        }
        
        if self.slaves.contains_key(&unit_id) {
            return Err(format!("Slave {} already exists", unit_id));
        }
        
        self.slaves.insert(unit_id, SlaveConfig::new(unit_id));
        self.polling_order.push(unit_id);
        
        println!("Added slave {} to network", unit_id);
        Ok(())
    }
    
    fn remove_slave(&mut self, unit_id: u8) {
        self.slaves.remove(&unit_id);
        self.polling_order.retain(|&id| id != unit_id);
    }
    
    // Simulate reading holding registers
    fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        count: u16,
    ) -> Result<Vec<u16>, String> {
        // In real implementation:
        // 1. Build Modbus frame
        // 2. Send over transport (serial/TCP)
        // 3. Wait for response with timeout
        // 4. Parse and validate response
        
        println!(
            "  Polling slave {}: Reading {} registers from address {}",
            unit_id, count, start_addr
        );
        
        // Simulate successful response
        let data: Vec<u16> = (0..count)
            .map(|i| 1000 + (unit_id as u16) * 100 + i)
            .collect();
        
        Ok(data)
    }
    
    fn poll_slave(&mut self, unit_id: u8) -> Result<Vec<u16>, String> {
        let mut retries = 0;
        
        while retries < MAX_RETRIES {
            match self.read_holding_registers(unit_id, 0, 5) {
                Ok(data) => {
                    if let Some(slave) = self.slaves.get_mut(&unit_id) {
                        slave.success_count += 1;
                        slave.last_poll_time = Some(Instant::now());
                        slave.is_active = true;
                    }
                    println!("  ✓ Slave {} responded: {:?}", unit_id, data);
                    return Ok(data);
                }
                Err(e) => {
                    retries += 1;
                    println!(
                        "  ✗ Slave {} error: {}, retry {}/{}",
                        unit_id, e, retries, MAX_RETRIES
                    );
                }
            }
        }
        
        // Mark slave as inactive after max retries
        if let Some(slave) = self.slaves.get_mut(&unit_id) {
            slave.error_count += 1;
            slave.is_active = false;
        }
        
        Err(format!("Slave {} failed after {} retries", unit_id, MAX_RETRIES))
    }
    
    fn poll_all_slaves(&mut self) {
        println!("\n=== Polling Cycle Started ===");
        
        for &unit_id in &self.polling_order.clone() {
            if let Some(slave) = self.slaves.get(&unit_id) {
                if slave.is_active {
                    let _ = self.poll_slave(unit_id);
                }
            }
        }
        
        println!("=== Polling Cycle Complete ===\n");
    }
    
    fn priority_poll(&mut self, priority_slaves: &[u8]) {
        println!("\n=== Priority Polling ===");
        
        for &unit_id in priority_slaves {
            if self.slaves.contains_key(&unit_id) {
                let _ = self.poll_slave(unit_id);
            }
        }
    }
    
    fn broadcast_write(&self, address: u16, value: u16) {
        println!("\n=== Broadcast Write ===");
        println!(
            "  Broadcasting to all slaves: Write {} to address {}",
            value, address
        );
        println!("  (No responses expected)");
        
        // In real implementation:
        // 1. Build Modbus frame with unit_id = 0
        // 2. Send frame
        // 3. Don't wait for response (broadcast)
    }
    
    fn display_statistics(&self) {
        println!("\n=== Network Statistics ===");
        
        let mut sorted_ids: Vec<_> = self.slaves.keys().collect();
        sorted_ids.sort();
        
        for &unit_id in sorted_ids {
            if let Some(slave) = self.slaves.get(unit_id) {
                println!(
                    "Slave {}: Success={}, Errors={}, Rate={:.1}%, Status={}",
                    slave.unit_id,
                    slave.success_count,
                    slave.error_count,
                    slave.success_rate(),
                    if slave.is_active { "Active" } else { "Inactive" }
                );
            }
        }
    }
    
    // Advanced: Adaptive polling based on response times
    fn adaptive_poll(&mut self) {
        println!("\n=== Adaptive Polling ===");
        
        // Sort slaves by success rate (poll reliable slaves more often)
        let mut slaves_by_reliability: Vec<_> = self.slaves.values().collect();
        slaves_by_reliability.sort_by(|a, b| {
            b.success_rate().partial_cmp(&a.success_rate()).unwrap()
        });
        
        for slave in slaves_by_reliability {
            if slave.is_active && slave.success_rate() > 80.0 {
                let _ = self.poll_slave(slave.unit_id);
            }
        }
    }
}

fn main() {
    let mut network = ModbusNetwork::new();
    
    // Configure multi-drop network
    println!("=== Configuring Multi-Drop Network ===");
    let _ = network.add_slave(1); // Temperature sensor
    let _ = network.add_slave(2); // Pressure sensor
    let _ = network.add_slave(3); // Flow meter
    let _ = network.add_slave(4); // Valve controller
    let _ = network.add_slave(5); // Motor drive
    
    // Sequential polling
    network.poll_all_slaves();
    
    // Priority polling
    network.priority_poll(&[1, 3]);
    
    // Broadcast write
    network.broadcast_write(100, 5000);
    
    // Adaptive polling
    network.adaptive_poll();
    
    // Display statistics
    network.display_statistics();
}
```

## Summary

**Multi-drop network configuration** is a fundamental Modbus architecture pattern that enables efficient communication between one master and multiple slave devices over a shared bus. Key implementation requirements include:

- **Unique addressing** (1-247) for each slave device to prevent conflicts
- **Master-driven arbitration** where only the master initiates communication, eliminating collisions
- **Proper physical layer setup** with termination, biasing, and grounding for RS-485 networks
- **Intelligent polling strategies** (sequential, priority-based, or adaptive) to optimize network performance
- **Robust error handling** with timeouts, retries, and slave health monitoring
- **Broadcast capability** for simultaneous commands to all devices without expecting responses

Both code examples demonstrate creating a network manager that tracks multiple slaves, implements various polling strategies, handles failures gracefully, and collects statistics for monitoring network health. The Rust implementation adds type safety and memory safety guarantees, while the C implementation provides lower-level control suitable for embedded systems. Proper multi-drop configuration is essential for scalable, reliable industrial automation systems.