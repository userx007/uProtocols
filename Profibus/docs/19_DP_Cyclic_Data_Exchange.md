# DP Cyclic Data Exchange

## Overview

DP Cyclic Data Exchange is the core mechanism in PROFIBUS DP (Decentralized Periphery) for real-time, deterministic communication between a master (Class 1) and its slaves. Unlike acyclic communication used for configuration and diagnostics, cyclic data exchange handles the repetitive transfer of process data (inputs/outputs) that occurs every bus cycle.

## Key Concepts

### Cyclic Communication Model
- **Master → Slave**: Output data (commands, setpoints, control signals)
- **Slave → Master**: Input data (sensor values, status information)
- **Deterministic Timing**: Data exchange occurs at fixed, predictable intervals
- **Token Passing**: In multi-master systems, only the token holder can initiate cyclic data exchange

### Data Exchange Process
1. Master sends output data to slave
2. Slave processes data and prepares response
3. Slave returns input data to master
4. Process repeats every bus cycle (typically 1-10ms)

### Key Characteristics
- **Low Latency**: Minimal delay between master request and slave response
- **Consistency**: All I/O data updates occur within the same cycle
- **Efficiency**: Minimal protocol overhead for maximum throughput
- **Freeze/Sync Commands**: Optional commands for synchronized data capture/output

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// PROFIBUS DP cyclic data structures
#define MAX_IO_DATA_LENGTH 244  // Maximum I/O data per slave
#define MAX_SLAVES 126

// Slave I/O configuration
typedef struct {
    uint8_t station_address;
    uint16_t input_length;      // Number of input bytes
    uint16_t output_length;     // Number of output bytes
    uint8_t input_data[MAX_IO_DATA_LENGTH];
    uint8_t output_data[MAX_IO_DATA_LENGTH];
    bool data_valid;            // Data consistency flag
    uint32_t cycle_counter;     // Cycle counter for diagnostics
} dp_slave_io_t;

// Master cyclic data manager
typedef struct {
    dp_slave_io_t slaves[MAX_SLAVES];
    uint8_t active_slave_count;
    uint32_t bus_cycle_time_us; // Bus cycle time in microseconds
    bool freeze_mode;            // Freeze command active
    bool sync_mode;              // Sync command active
} dp_master_t;

// Initialize DP master
void dp_master_init(dp_master_t *master, uint32_t cycle_time_us) {
    memset(master, 0, sizeof(dp_master_t));
    master->bus_cycle_time_us = cycle_time_us;
}

// Add slave to cyclic data exchange
bool dp_add_slave(dp_master_t *master, uint8_t address, 
                  uint16_t input_len, uint16_t output_len) {
    if (master->active_slave_count >= MAX_SLAVES) {
        return false;
    }
    
    dp_slave_io_t *slave = &master->slaves[master->active_slave_count];
    slave->station_address = address;
    slave->input_length = input_len;
    slave->output_length = output_len;
    slave->data_valid = false;
    slave->cycle_counter = 0;
    
    master->active_slave_count++;
    return true;
}

// Write output data to slave
bool dp_write_output(dp_master_t *master, uint8_t address, 
                     const uint8_t *data, uint16_t length) {
    for (uint8_t i = 0; i < master->active_slave_count; i++) {
        dp_slave_io_t *slave = &master->slaves[i];
        if (slave->station_address == address) {
            if (length > slave->output_length) {
                return false;
            }
            memcpy(slave->output_data, data, length);
            return true;
        }
    }
    return false;
}

// Read input data from slave
bool dp_read_input(dp_master_t *master, uint8_t address, 
                   uint8_t *data, uint16_t *length) {
    for (uint8_t i = 0; i < master->active_slave_count; i++) {
        dp_slave_io_t *slave = &master->slaves[i];
        if (slave->station_address == address && slave->data_valid) {
            memcpy(data, slave->input_data, slave->input_length);
            *length = slave->input_length;
            return true;
        }
    }
    return false;
}

// Simulate cyclic data exchange (one bus cycle)
void dp_cyclic_exchange(dp_master_t *master) {
    for (uint8_t i = 0; i < master->active_slave_count; i++) {
        dp_slave_io_t *slave = &master->slaves[i];
        
        // Send output data to slave (simulated)
        // In real implementation: send_dp_telegram(slave->station_address, 
        //                                          slave->output_data, 
        //                                          slave->output_length);
        
        // Receive input data from slave (simulated)
        // In real implementation: receive_dp_telegram(slave->input_data, 
        //                                             &slave->input_length);
        
        slave->data_valid = true;
        slave->cycle_counter++;
    }
}

// Example: Control a motor with cyclic I/O
void example_motor_control(dp_master_t *master) {
    uint8_t motor_outputs[4] = {0};  // 4 bytes: speed, direction, enable, reserved
    uint8_t motor_inputs[8] = {0};   // 8 bytes: actual speed, position, status, etc.
    uint16_t input_length;
    
    // Configure motor slave (address 5)
    dp_add_slave(master, 5, 8, 4);
    
    // Control loop
    for (int cycle = 0; cycle < 100; cycle++) {
        // Set motor parameters
        motor_outputs[0] = 150;  // Speed setpoint (0-255)
        motor_outputs[1] = 1;    // Direction: forward
        motor_outputs[2] = 1;    // Enable motor
        
        // Write outputs
        dp_write_output(master, 5, motor_outputs, 4);
        
        // Execute cyclic exchange
        dp_cyclic_exchange(master);
        
        // Read inputs
        if (dp_read_input(master, 5, motor_inputs, &input_length)) {
            uint8_t actual_speed = motor_inputs[0];
            uint16_t position = (motor_inputs[2] << 8) | motor_inputs[1];
            uint8_t status = motor_inputs[3];
            
            // Process feedback data
            // ... control logic here ...
        }
    }
}
```

## C++ Object-Oriented Implementation

```cpp
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <stdexcept>

class DPSlave {
private:
    uint8_t address_;
    std::vector<uint8_t> input_data_;
    std::vector<uint8_t> output_data_;
    bool data_valid_;
    uint64_t cycle_count_;

public:
    DPSlave(uint8_t address, size_t input_len, size_t output_len)
        : address_(address), 
          input_data_(input_len, 0),
          output_data_(output_len, 0),
          data_valid_(false),
          cycle_count_(0) {}

    uint8_t getAddress() const { return address_; }
    
    void setOutputData(const std::vector<uint8_t>& data) {
        if (data.size() != output_data_.size()) {
            throw std::invalid_argument("Output data size mismatch");
        }
        output_data_ = data;
    }
    
    const std::vector<uint8_t>& getInputData() const {
        if (!data_valid_) {
            throw std::runtime_error("Input data not yet valid");
        }
        return input_data_;
    }
    
    void simulateExchange() {
        // Simulate hardware I/O exchange
        // In real implementation, this would communicate via PROFIBUS hardware
        data_valid_ = true;
        cycle_count_++;
    }
    
    uint64_t getCycleCount() const { return cycle_count_; }
};

class DPMaster {
private:
    std::vector<std::unique_ptr<DPSlave>> slaves_;
    uint32_t cycle_time_us_;
    
public:
    explicit DPMaster(uint32_t cycle_time_us) 
        : cycle_time_us_(cycle_time_us) {}
    
    void addSlave(uint8_t address, size_t input_len, size_t output_len) {
        slaves_.push_back(
            std::make_unique<DPSlave>(address, input_len, output_len)
        );
    }
    
    void writeOutput(uint8_t address, const std::vector<uint8_t>& data) {
        for (auto& slave : slaves_) {
            if (slave->getAddress() == address) {
                slave->setOutputData(data);
                return;
            }
        }
        throw std::runtime_error("Slave not found");
    }
    
    std::vector<uint8_t> readInput(uint8_t address) {
        for (auto& slave : slaves_) {
            if (slave->getAddress() == address) {
                return slave->getInputData();
            }
        }
        throw std::runtime_error("Slave not found");
    }
    
    void executeCycle() {
        for (auto& slave : slaves_) {
            slave->simulateExchange();
        }
    }
    
    uint32_t getCycleTime() const { return cycle_time_us_; }
};

// Usage example
void example_cpp_usage() {
    DPMaster master(5000);  // 5ms cycle time
    
    // Add I/O slaves
    master.addSlave(3, 16, 8);   // Address 3: 16 input bytes, 8 output bytes
    master.addSlave(5, 8, 4);    // Address 5: 8 input bytes, 4 output bytes
    
    // Control loop
    for (int i = 0; i < 100; i++) {
        // Write outputs
        master.writeOutput(5, {0xAA, 0x55, 0x01, 0x00});
        
        // Execute bus cycle
        master.executeCycle();
        
        // Read inputs
        try {
            auto inputs = master.readInput(5);
            // Process input data...
        } catch (const std::exception& e) {
            // Handle error
        }
    }
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::time::Duration;

const MAX_IO_DATA_LENGTH: usize = 244;

#[derive(Debug, Clone)]
pub struct IoData {
    data: Vec<u8>,
    valid: bool,
}

impl IoData {
    fn new(size: usize) -> Self {
        IoData {
            data: vec![0u8; size],
            valid: false,
        }
    }
    
    fn set_data(&mut self, data: &[u8]) -> Result<(), String> {
        if data.len() != self.data.len() {
            return Err(format!(
                "Data size mismatch: expected {}, got {}", 
                self.data.len(), 
                data.len()
            ));
        }
        self.data.copy_from_slice(data);
        Ok(())
    }
    
    fn get_data(&self) -> Result<&[u8], String> {
        if !self.valid {
            return Err("Data not valid yet".to_string());
        }
        Ok(&self.data)
    }
    
    fn mark_valid(&mut self) {
        self.valid = true;
    }
}

pub struct DpSlave {
    address: u8,
    input_data: IoData,
    output_data: IoData,
    cycle_count: u64,
}

impl DpSlave {
    pub fn new(address: u8, input_len: usize, output_len: usize) -> Self {
        DpSlave {
            address,
            input_data: IoData::new(input_len),
            output_data: IoData::new(output_len),
            cycle_count: 0,
        }
    }
    
    pub fn set_output(&mut self, data: &[u8]) -> Result<(), String> {
        self.output_data.set_data(data)
    }
    
    pub fn get_input(&self) -> Result<&[u8], String> {
        self.input_data.get_data()
    }
    
    pub fn simulate_exchange(&mut self) {
        // Simulate PROFIBUS hardware exchange
        // In real implementation: communicate via PROFIBUS driver
        self.input_data.mark_valid();
        self.cycle_count += 1;
    }
    
    pub fn get_cycle_count(&self) -> u64 {
        self.cycle_count
    }
}

pub struct DpMaster {
    slaves: HashMap<u8, DpSlave>,
    cycle_time: Duration,
}

impl DpMaster {
    pub fn new(cycle_time: Duration) -> Self {
        DpMaster {
            slaves: HashMap::new(),
            cycle_time,
        }
    }
    
    pub fn add_slave(&mut self, address: u8, input_len: usize, output_len: usize) {
        self.slaves.insert(
            address, 
            DpSlave::new(address, input_len, output_len)
        );
    }
    
    pub fn write_output(&mut self, address: u8, data: &[u8]) -> Result<(), String> {
        self.slaves
            .get_mut(&address)
            .ok_or_else(|| format!("Slave {} not found", address))?
            .set_output(data)
    }
    
    pub fn read_input(&self, address: u8) -> Result<Vec<u8>, String> {
        let slave = self.slaves
            .get(&address)
            .ok_or_else(|| format!("Slave {} not found", address))?;
        
        Ok(slave.get_input()?.to_vec())
    }
    
    pub fn execute_cycle(&mut self) {
        for slave in self.slaves.values_mut() {
            slave.simulate_exchange();
        }
    }
    
    pub fn get_cycle_time(&self) -> Duration {
        self.cycle_time
    }
}

// Example usage with error handling
pub fn example_motor_control() -> Result<(), String> {
    let mut master = DpMaster::new(Duration::from_millis(5));
    
    // Add motor controller slave
    master.add_slave(5, 8, 4);
    
    // Control loop
    for cycle in 0..100 {
        // Prepare output data: [speed, direction, enable, reserved]
        let outputs = vec![150u8, 1, 1, 0];
        master.write_output(5, &outputs)?;
        
        // Execute cyclic exchange
        master.execute_cycle();
        
        // Read input data
        let inputs = master.read_input(5)?;
        let actual_speed = inputs[0];
        let position = u16::from_le_bytes([inputs[1], inputs[2]]);
        let status = inputs[3];
        
        println!(
            "Cycle {}: Speed={}, Position={}, Status=0x{:02X}", 
            cycle, actual_speed, position, status
        );
    }
    
    Ok(())
}

// Advanced example with freeze/sync support
#[derive(Debug, Clone, Copy)]
pub enum SyncMode {
    None,
    Freeze,  // Freeze inputs at specific time
    Sync,    // Synchronize outputs
}

pub struct AdvancedDpMaster {
    master: DpMaster,
    sync_mode: SyncMode,
}

impl AdvancedDpMaster {
    pub fn new(cycle_time: Duration) -> Self {
        AdvancedDpMaster {
            master: DpMaster::new(cycle_time),
            sync_mode: SyncMode::None,
        }
    }
    
    pub fn set_sync_mode(&mut self, mode: SyncMode) {
        self.sync_mode = mode;
        // In real implementation: send freeze/sync control commands
    }
    
    pub fn add_slave(&mut self, address: u8, input_len: usize, output_len: usize) {
        self.master.add_slave(address, input_len, output_len);
    }
    
    pub fn execute_synchronized_cycle(&mut self) {
        match self.sync_mode {
            SyncMode::Freeze => {
                // All slaves capture inputs simultaneously
                self.master.execute_cycle();
            }
            SyncMode::Sync => {
                // All slaves update outputs simultaneously
                self.master.execute_cycle();
            }
            SyncMode::None => {
                self.master.execute_cycle();
            }
        }
    }
}
```

## Summary

**DP Cyclic Data Exchange** is the deterministic, real-time communication backbone of PROFIBUS DP systems. It enables predictable, low-latency transfer of process data between masters and slaves every bus cycle.

**Key Points:**
- **Deterministic timing** ensures data exchange occurs at fixed intervals (1-10ms typical)
- **Bidirectional communication** with outputs from master to slave and inputs from slave to master
- **Minimal overhead** maximizes throughput for time-critical applications
- **Data consistency** through synchronized read/write operations
- **Optional freeze/sync commands** for coordinated multi-slave operations

The code examples demonstrate implementing cyclic I/O management in C (procedural), C++ (object-oriented), and Rust (safe, modern). All implementations handle slave registration, output writes, input reads, and cycle execution. The Rust version adds comprehensive error handling using `Result` types, while the C++ version uses exceptions for safety. Real-world implementations would interface with PROFIBUS hardware drivers (ASIC/UART) to perform actual telegram transmission and reception, but the logic structure remains similar across all three languages.