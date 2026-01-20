# Modbus Master/Client Implementation

## Overview

A Modbus Master (also called Client in Modbus TCP terminology) is the initiating device in a Modbus network that polls slave devices (servers), requests data, and sends control commands. The master orchestrates all communication, as slaves only respond to master requests and never initiate communication themselves.

## Key Concepts

### Master Responsibilities

The master device must:
- Initiate all communications on the network
- Poll slaves sequentially to read their data
- Send write commands to control slave devices
- Handle timeouts and communication errors
- Maintain proper timing between requests
- Manage multiple slave connections efficiently

### Communication Patterns

**Polling**: The master regularly requests data from slaves at defined intervals. This could be sensor readings, status information, or diagnostic data.

**Command and Control**: The master writes values to slaves to control actuators, update setpoints, or modify configuration.

**Event-Driven**: While primarily polling-based, some implementations respond to internal events or external triggers to initiate specific slave communications.

## Architecture Considerations

A robust master implementation requires:

**Connection Management**: Handling serial ports (RTU/ASCII) or TCP sockets, managing reconnection logic, and detecting disconnected slaves.

**Request Scheduling**: Prioritizing critical reads/writes, implementing round-robin or priority-based polling, and managing request queues.

**Error Handling**: Detecting timeouts, processing exception responses, implementing retry logic, and logging communication failures.

**Data Management**: Caching slave data, detecting data changes, and providing data to application layers.

## C/C++ Implementation Example

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <cstring>
#include <modbus/modbus.h>

// Slave configuration structure
struct SlaveConfig {
    int id;
    std::string ip_address;
    int port;
    int poll_interval_ms;
    std::chrono::steady_clock::time_point last_poll;
};

// Data cache for slave registers
struct SlaveData {
    std::vector<uint16_t> holding_registers;
    std::vector<uint16_t> input_registers;
    std::vector<uint8_t> coils;
    std::vector<uint8_t> discrete_inputs;
    bool is_online;
    int error_count;
};

class ModbusMaster {
private:
    std::map<int, modbus_t*> connections;
    std::map<int, SlaveConfig> slave_configs;
    std::map<int, SlaveData> slave_data;
    int max_retries;
    int timeout_ms;

public:
    ModbusMaster(int timeout = 1000, int retries = 3) 
        : max_retries(retries), timeout_ms(timeout) {}

    ~ModbusMaster() {
        for (auto& conn : connections) {
            if (conn.second) {
                modbus_close(conn.second);
                modbus_free(conn.second);
            }
        }
    }

    // Add a TCP slave to the master
    bool add_tcp_slave(int slave_id, const std::string& ip, int port, int poll_interval_ms) {
        SlaveConfig config;
        config.id = slave_id;
        config.ip_address = ip;
        config.port = port;
        config.poll_interval_ms = poll_interval_ms;
        config.last_poll = std::chrono::steady_clock::now();

        slave_configs[slave_id] = config;

        // Initialize connection
        modbus_t* ctx = modbus_new_tcp(ip.c_str(), port);
        if (!ctx) {
            std::cerr << "Failed to create Modbus context for slave " << slave_id << std::endl;
            return false;
        }

        modbus_set_slave(ctx, slave_id);
        modbus_set_response_timeout(ctx, timeout_ms / 1000, (timeout_ms % 1000) * 1000);

        if (modbus_connect(ctx) == -1) {
            std::cerr << "Connection failed to slave " << slave_id << ": " 
                      << modbus_strerror(errno) << std::endl;
            modbus_free(ctx);
            return false;
        }

        connections[slave_id] = ctx;

        // Initialize data cache
        SlaveData data;
        data.holding_registers.resize(100, 0);
        data.input_registers.resize(100, 0);
        data.coils.resize(100, 0);
        data.discrete_inputs.resize(100, 0);
        data.is_online = true;
        data.error_count = 0;
        slave_data[slave_id] = data;

        std::cout << "Slave " << slave_id << " added successfully" << std::endl;
        return true;
    }

    // Read holding registers from a slave
    bool read_holding_registers(int slave_id, int start_addr, int count) {
        auto it = connections.find(slave_id);
        if (it == connections.end()) {
            std::cerr << "Slave " << slave_id << " not found" << std::endl;
            return false;
        }

        modbus_t* ctx = it->second;
        std::vector<uint16_t> buffer(count);

        int rc = modbus_read_registers(ctx, start_addr, count, buffer.data());
        
        if (rc == -1) {
            std::cerr << "Read failed for slave " << slave_id << ": " 
                      << modbus_strerror(errno) << std::endl;
            slave_data[slave_id].error_count++;
            slave_data[slave_id].is_online = false;
            return false;
        }

        // Update cache
        for (int i = 0; i < count && (start_addr + i) < slave_data[slave_id].holding_registers.size(); i++) {
            slave_data[slave_id].holding_registers[start_addr + i] = buffer[i];
        }

        slave_data[slave_id].is_online = true;
        slave_data[slave_id].error_count = 0;
        return true;
    }

    // Write single holding register
    bool write_register(int slave_id, int addr, uint16_t value) {
        auto it = connections.find(slave_id);
        if (it == connections.end()) {
            return false;
        }

        modbus_t* ctx = it->second;
        int rc = modbus_write_register(ctx, addr, value);

        if (rc == -1) {
            std::cerr << "Write failed for slave " << slave_id << ": " 
                      << modbus_strerror(errno) << std::endl;
            return false;
        }

        // Update local cache
        if (addr < slave_data[slave_id].holding_registers.size()) {
            slave_data[slave_id].holding_registers[addr] = value;
        }

        return true;
    }

    // Write multiple registers
    bool write_registers(int slave_id, int start_addr, const std::vector<uint16_t>& values) {
        auto it = connections.find(slave_id);
        if (it == connections.end()) {
            return false;
        }

        modbus_t* ctx = it->second;
        int rc = modbus_write_registers(ctx, start_addr, values.size(), values.data());

        if (rc == -1) {
            std::cerr << "Write multiple failed for slave " << slave_id << ": " 
                      << modbus_strerror(errno) << std::endl;
            return false;
        }

        return true;
    }

    // Polling loop - reads data from all slaves based on their poll intervals
    void poll_all_slaves() {
        auto now = std::chrono::steady_clock::now();

        for (auto& config : slave_configs) {
            int slave_id = config.first;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - config.second.last_poll);

            if (elapsed.count() >= config.second.poll_interval_ms) {
                std::cout << "Polling slave " << slave_id << std::endl;
                
                // Read holding registers (example: addresses 0-9)
                read_holding_registers(slave_id, 0, 10);
                
                config.second.last_poll = now;
            }
        }
    }

    // Get cached register value
    uint16_t get_register(int slave_id, int addr) {
        if (slave_data.find(slave_id) != slave_data.end() && 
            addr < slave_data[slave_id].holding_registers.size()) {
            return slave_data[slave_id].holding_registers[addr];
        }
        return 0;
    }

    // Check if slave is online
    bool is_slave_online(int slave_id) {
        if (slave_data.find(slave_id) != slave_data.end()) {
            return slave_data[slave_id].is_online;
        }
        return false;
    }

    // Display all slave statuses
    void print_status() {
        std::cout << "\n=== Modbus Master Status ===" << std::endl;
        for (const auto& data : slave_data) {
            std::cout << "Slave " << data.first << ": " 
                      << (data.second.is_online ? "ONLINE" : "OFFLINE")
                      << " (Errors: " << data.second.error_count << ")" << std::endl;
        }
        std::cout << "========================\n" << std::endl;
    }
};

// Example usage
int main() {
    ModbusMaster master(1000, 3);

    // Add slaves
    master.add_tcp_slave(1, "192.168.1.10", 502, 1000);  // Poll every 1 second
    master.add_tcp_slave(2, "192.168.1.11", 502, 2000);  // Poll every 2 seconds

    // Main polling loop
    for (int i = 0; i < 20; i++) {
        master.poll_all_slaves();
        master.print_status();
        
        // Example: write a value to slave 1
        if (i == 5) {
            std::cout << "Writing value 1234 to slave 1, register 5" << std::endl;
            master.write_register(1, 5, 1234);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}
```

## Rust Implementation Example

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::net::SocketAddr;
use tokio_modbus::prelude::*;
use tokio;

#[derive(Clone)]
struct SlaveConfig {
    id: u8,
    address: SocketAddr,
    poll_interval: Duration,
    last_poll: Instant,
}

#[derive(Clone, Debug)]
struct SlaveData {
    holding_registers: Vec<u16>,
    input_registers: Vec<u16>,
    coils: Vec<bool>,
    discrete_inputs: Vec<bool>,
    is_online: bool,
    error_count: u32,
}

impl SlaveData {
    fn new(size: usize) -> Self {
        Self {
            holding_registers: vec![0; size],
            input_registers: vec![0; size],
            coils: vec![false; size],
            discrete_inputs: vec![false; size],
            is_online: false,
            error_count: 0,
        }
    }
}

struct ModbusMaster {
    slave_configs: HashMap<u8, SlaveConfig>,
    slave_data: HashMap<u8, SlaveData>,
    timeout: Duration,
    max_retries: u32,
}

impl ModbusMaster {
    fn new(timeout: Duration, max_retries: u32) -> Self {
        Self {
            slave_configs: HashMap::new(),
            slave_data: HashMap::new(),
            timeout,
            max_retries,
        }
    }

    fn add_slave(&mut self, slave_id: u8, address: SocketAddr, poll_interval: Duration) {
        let config = SlaveConfig {
            id: slave_id,
            address,
            poll_interval,
            last_poll: Instant::now(),
        };

        self.slave_configs.insert(slave_id, config);
        self.slave_data.insert(slave_id, SlaveData::new(100));
        
        println!("Added slave {} at {}", slave_id, address);
    }

    async fn read_holding_registers(
        &mut self,
        slave_id: u8,
        start_addr: u16,
        count: u16,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let config = self.slave_configs.get(&slave_id)
            .ok_or("Slave not found")?;

        // Create TCP client
        let socket_addr = config.address;
        let mut ctx = tcp::connect_slave(socket_addr, Slave(slave_id)).await?;

        // Read registers with retry logic
        let mut attempts = 0;
        let result = loop {
            match ctx.read_holding_registers(start_addr, count).await {
                Ok(data) => break Ok(data),
                Err(e) => {
                    attempts += 1;
                    if attempts >= self.max_retries {
                        break Err(e);
                    }
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
            }
        };

        match result {
            Ok(data) => {
                // Update cache
                if let Some(slave_data) = self.slave_data.get_mut(&slave_id) {
                    let start = start_addr as usize;
                    for (i, &value) in data.iter().enumerate() {
                        if start + i < slave_data.holding_registers.len() {
                            slave_data.holding_registers[start + i] = value;
                        }
                    }
                    slave_data.is_online = true;
                    slave_data.error_count = 0;
                }
                println!("Successfully read {} registers from slave {}", count, slave_id);
                Ok(())
            }
            Err(e) => {
                if let Some(slave_data) = self.slave_data.get_mut(&slave_id) {
                    slave_data.is_online = false;
                    slave_data.error_count += 1;
                }
                eprintln!("Failed to read from slave {}: {}", slave_id, e);
                Err(Box::new(e))
            }
        }
    }

    async fn write_single_register(
        &mut self,
        slave_id: u8,
        addr: u16,
        value: u16,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let config = self.slave_configs.get(&slave_id)
            .ok_or("Slave not found")?;

        let socket_addr = config.address;
        let mut ctx = tcp::connect_slave(socket_addr, Slave(slave_id)).await?;

        ctx.write_single_register(addr, value).await?;

        // Update local cache
        if let Some(slave_data) = self.slave_data.get_mut(&slave_id) {
            if (addr as usize) < slave_data.holding_registers.len() {
                slave_data.holding_registers[addr as usize] = value;
            }
        }

        println!("Wrote {} to register {} on slave {}", value, addr, slave_id);
        Ok(())
    }

    async fn write_multiple_registers(
        &mut self,
        slave_id: u8,
        start_addr: u16,
        values: &[u16],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let config = self.slave_configs.get(&slave_id)
            .ok_or("Slave not found")?;

        let socket_addr = config.address;
        let mut ctx = tcp::connect_slave(socket_addr, Slave(slave_id)).await?;

        ctx.write_multiple_registers(start_addr, values).await?;

        println!("Wrote {} registers to slave {} starting at address {}", 
                 values.len(), slave_id, start_addr);
        Ok(())
    }

    async fn read_coils(
        &mut self,
        slave_id: u8,
        start_addr: u16,
        count: u16,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let config = self.slave_configs.get(&slave_id)
            .ok_or("Slave not found")?;

        let socket_addr = config.address;
        let mut ctx = tcp::connect_slave(socket_addr, Slave(slave_id)).await?;

        let data = ctx.read_coils(start_addr, count).await?;

        if let Some(slave_data) = self.slave_data.get_mut(&slave_id) {
            let start = start_addr as usize;
            for (i, &value) in data.iter().enumerate() {
                if start + i < slave_data.coils.len() {
                    slave_data.coils[start + i] = value;
                }
            }
        }

        Ok(())
    }

    async fn poll_slave(&mut self, slave_id: u8) -> Result<(), Box<dyn std::error::Error>> {
        // Example: Read first 10 holding registers
        self.read_holding_registers(slave_id, 0, 10).await?;
        
        // Example: Read first 8 coils
        self.read_coils(slave_id, 0, 8).await?;
        
        Ok(())
    }

    async fn poll_all_slaves(&mut self) {
        let now = Instant::now();
        let slave_ids: Vec<u8> = self.slave_configs.keys().cloned().collect();

        for slave_id in slave_ids {
            if let Some(config) = self.slave_configs.get_mut(&slave_id) {
                let elapsed = now.duration_since(config.last_poll);
                
                if elapsed >= config.poll_interval {
                    println!("Polling slave {}", slave_id);
                    
                    if let Err(e) = self.poll_slave(slave_id).await {
                        eprintln!("Poll failed for slave {}: {}", slave_id, e);
                    }
                    
                    config.last_poll = now;
                }
            }
        }
    }

    fn get_register(&self, slave_id: u8, addr: u16) -> Option<u16> {
        self.slave_data.get(&slave_id)
            .and_then(|data| data.holding_registers.get(addr as usize))
            .copied()
    }

    fn is_slave_online(&self, slave_id: u8) -> bool {
        self.slave_data.get(&slave_id)
            .map(|data| data.is_online)
            .unwrap_or(false)
    }

    fn print_status(&self) {
        println!("\n=== Modbus Master Status ===");
        for (slave_id, data) in &self.slave_data {
            println!(
                "Slave {}: {} (Errors: {})",
                slave_id,
                if data.is_online { "ONLINE" } else { "OFFLINE" },
                data.error_count
            );
            
            // Print first few registers
            print!("  Registers [0-4]: ");
            for i in 0..5.min(data.holding_registers.len()) {
                print!("{} ", data.holding_registers[i]);
            }
            println!();
        }
        println!("============================\n");
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut master = ModbusMaster::new(Duration::from_secs(1), 3);

    // Add slaves
    let addr1 = "192.168.1.10:502".parse()?;
    let addr2 = "192.168.1.11:502".parse()?;
    
    master.add_slave(1, addr1, Duration::from_secs(1));
    master.add_slave(2, addr2, Duration::from_secs(2));

    // Main polling loop
    for iteration in 0..20 {
        master.poll_all_slaves().await;
        master.print_status();

        // Example: Write a value after 5 iterations
        if iteration == 5 {
            println!("Writing value 5678 to slave 1, register 5");
            if let Err(e) = master.write_single_register(1, 5, 5678).await {
                eprintln!("Write failed: {}", e);
            }
        }

        // Example: Write multiple registers
        if iteration == 10 {
            let values = vec![100, 200, 300, 400];
            println!("Writing multiple values to slave 2");
            if let Err(e) = master.write_multiple_registers(2, 10, &values).await {
                eprintln!("Write failed: {}", e);
            }
        }

        tokio::time::sleep(Duration::from_millis(500)).await;
    }

    Ok(())
}

// Cargo.toml dependencies needed:
// [dependencies]
// tokio = { version = "1", features = ["full"] }
// tokio-modbus = "0.7"
```

## Summary

**Modbus Master/Client Implementation** is the foundation of any Modbus control system. The master orchestrates all network communication by polling slaves for data and sending control commands. Key implementation aspects include:

**Core Functions**: Connection management for serial or TCP links, scheduled polling of multiple slaves, reading input/holding registers and coils, writing to holding registers and coils, timeout and retry handling, and data caching for application access.

**Design Patterns**: Round-robin polling ensures fair access to all slaves, priority queuing allows critical operations to execute first, error recovery through retry logic and reconnection, and data validation ensures received values are within expected ranges.

**Performance Optimization**: Batching register reads reduces network overhead, adjusting poll intervals based on data criticality, connection pooling for TCP implementations, and asynchronous I/O for handling multiple slaves concurrently.

**Best Practices**: Always implement timeout handling to prevent indefinite blocking, cache slave data locally to reduce network traffic, log all communication errors for diagnostics, validate slave responses before using data, implement watchdogs to detect stalled communications, and provide status monitoring for operators.

The C++ example demonstrates a synchronous polling approach using the libmodbus library, suitable for smaller systems with sequential slave access. The Rust example showcases an asynchronous implementation using tokio-modbus, ideal for systems managing many slaves concurrently with better resource utilization. Both implementations maintain data caches, handle errors gracefully, and provide flexible polling schedules—essential features for reliable industrial automation systems.