# Master Class 1 Implementation in Profibus DP

## Detailed Description

### Overview

A **Profibus DP Class 1 Master** is the primary controller in a Profibus Decentralized Peripherals (DP) network, responsible for **cyclic I/O communication** with DP slaves. Unlike Class 2 Masters (which handle acyclic read/write operations for configuration and diagnostics), Class 1 Masters perform time-critical, deterministic data exchange with field devices.

### Key Responsibilities

1. **Bus Arbitration**: The Class 1 Master holds the token and controls bus access timing
2. **Cyclic Data Exchange**: Periodically sends output data to slaves and reads input data back
3. **Slave Management**: Monitors slave status, handles diagnostics, and manages slave lifecycle
4. **Deterministic Timing**: Ensures consistent cycle times for real-time control applications
5. **Error Handling**: Detects and responds to slave failures, watchdog timeouts, and communication errors

### Communication Cycle

A typical Class 1 Master cycle involves:

1. **Token Management**: Master acquires and holds the token
2. **Output Phase**: Master sends data request (Data_Exchange) to each configured slave
3. **Input Phase**: Slaves respond with their input data
4. **Diagnostics Monitoring**: Periodic status checks via slave diagnostic telegrams
5. **Watchdog Supervision**: Ensures communication remains active within configured intervals

### State Machine

The Class 1 Master typically implements a state machine for each slave:
- **IDLE**: Slave not yet configured
- **WAIT_PRM**: Waiting for slave to accept parameters
- **WAIT_CFG**: Waiting for slave to accept configuration
- **DATA_EXCHANGE**: Normal cyclic operation
- **FAULT**: Slave error condition

---

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus DP telegram structures
#define PROFIBUS_MAX_SLAVES 126
#define PROFIBUS_MAX_DATA_LEN 244

// Slave states
typedef enum {
    SLAVE_IDLE = 0,
    SLAVE_WAIT_PRM,
    SLAVE_WAIT_CFG,
    SLAVE_DATA_EXCHANGE,
    SLAVE_FAULT
} slave_state_t;

// DP telegram types
typedef enum {
    DP_SDN = 0x06,          // Send Data with No acknowledge
    DP_SDA = 0x0D,          // Send Data with Acknowledge
    DP_SDR = 0x0C,          // Send and Request Data
    DP_RDR = 0x0E           // Request Data with Reply
} dp_telegram_type_t;

// Slave configuration
typedef struct {
    uint8_t address;
    slave_state_t state;
    uint8_t input_len;      // Expected input data length
    uint8_t output_len;     // Output data length to send
    uint8_t config_data[32]; // GSD configuration
    uint8_t param_data[32];  // Parameter data
    uint32_t watchdog_timeout; // ms
    uint32_t last_exchange;    // Timestamp of last successful exchange
} dp_slave_t;

// Master Class 1 context
typedef struct {
    dp_slave_t slaves[PROFIBUS_MAX_SLAVES];
    uint8_t num_slaves;
    uint8_t current_slave_idx;
    uint32_t cycle_time_us;  // Target cycle time in microseconds
    bool bus_active;
    
    // I/O buffers
    uint8_t output_data[PROFIBUS_MAX_SLAVES][PROFIBUS_MAX_DATA_LEN];
    uint8_t input_data[PROFIBUS_MAX_SLAVES][PROFIBUS_MAX_DATA_LEN];
} dp_master_class1_t;

// Low-level driver functions (platform-specific)
extern bool profibus_send_telegram(uint8_t dest_addr, uint8_t fc, 
                                   const uint8_t* data, uint8_t len);
extern bool profibus_receive_telegram(uint8_t* src_addr, uint8_t* fc,
                                      uint8_t* data, uint8_t* len, uint32_t timeout_ms);
extern uint32_t profibus_get_timestamp_ms(void);

// Initialize Class 1 Master
void dp_master_init(dp_master_class1_t* master, uint32_t cycle_time_us) {
    memset(master, 0, sizeof(dp_master_class1_t));
    master->cycle_time_us = cycle_time_us;
    master->bus_active = false;
}

// Add a slave to the configuration
bool dp_master_add_slave(dp_master_class1_t* master, uint8_t address,
                        uint8_t input_len, uint8_t output_len,
                        const uint8_t* config, uint8_t config_len,
                        const uint8_t* params, uint8_t param_len) {
    if (master->num_slaves >= PROFIBUS_MAX_SLAVES) return false;
    
    dp_slave_t* slave = &master->slaves[master->num_slaves];
    slave->address = address;
    slave->state = SLAVE_IDLE;
    slave->input_len = input_len;
    slave->output_len = output_len;
    slave->watchdog_timeout = 100; // 100ms default
    
    memcpy(slave->config_data, config, config_len < 32 ? config_len : 32);
    memcpy(slave->param_data, params, param_len < 32 ? param_len : 32);
    
    master->num_slaves++;
    return true;
}

// Send parameterization telegram to slave
bool dp_master_send_parameters(dp_master_class1_t* master, dp_slave_t* slave) {
    uint8_t telegram[64];
    telegram[0] = 0x00; // Station status (master locked)
    telegram[1] = 0x00; // WD_Fact_1
    telegram[2] = 0x00; // WD_Fact_2
    telegram[3] = 0x00; // Min_Tsdr
    memcpy(&telegram[4], slave->param_data, 7); // User parameters
    
    return profibus_send_telegram(slave->address, DP_SDN, telegram, 11);
}

// Send configuration telegram to slave
bool dp_master_send_configuration(dp_master_class1_t* master, dp_slave_t* slave) {
    // Configuration format: specifies input/output module layout
    uint8_t config[16];
    config[0] = 0xC0 | slave->output_len; // Output configuration
    config[1] = 0x80 | slave->input_len;  // Input configuration
    
    return profibus_send_telegram(slave->address, DP_SDN, config, 2);
}

// Perform cyclic data exchange with a slave
bool dp_master_data_exchange(dp_master_class1_t* master, uint8_t slave_idx) {
    dp_slave_t* slave = &master->slaves[slave_idx];
    uint8_t recv_data[PROFIBUS_MAX_DATA_LEN];
    uint8_t recv_len, src_addr, fc;
    
    // Send output data and request input data (SDR telegram)
    if (!profibus_send_telegram(slave->address, DP_SDR,
                                master->output_data[slave_idx],
                                slave->output_len)) {
        return false;
    }
    
    // Wait for slave response
    if (!profibus_receive_telegram(&src_addr, &fc, recv_data, &recv_len, 10)) {
        return false; // Timeout or error
    }
    
    // Validate response
    if (src_addr == slave->address && recv_len == slave->input_len) {
        memcpy(master->input_data[slave_idx], recv_data, recv_len);
        slave->last_exchange = profibus_get_timestamp_ms();
        return true;
    }
    
    return false;
}

// Main cyclic task for Class 1 Master
void dp_master_cycle(dp_master_class1_t* master) {
    uint32_t cycle_start = profibus_get_timestamp_ms();
    
    for (uint8_t i = 0; i < master->num_slaves; i++) {
        dp_slave_t* slave = &master->slaves[i];
        
        switch (slave->state) {
            case SLAVE_IDLE:
                // Start parameterization
                if (dp_master_send_parameters(master, slave)) {
                    slave->state = SLAVE_WAIT_PRM;
                }
                break;
                
            case SLAVE_WAIT_PRM:
                // Send configuration after parameterization
                if (dp_master_send_configuration(master, slave)) {
                    slave->state = SLAVE_WAIT_CFG;
                }
                break;
                
            case SLAVE_WAIT_CFG:
                // Attempt first data exchange
                if (dp_master_data_exchange(master, i)) {
                    slave->state = SLAVE_DATA_EXCHANGE;
                }
                break;
                
            case SLAVE_DATA_EXCHANGE:
                // Normal cyclic operation
                if (!dp_master_data_exchange(master, i)) {
                    // Check for watchdog timeout
                    uint32_t now = profibus_get_timestamp_ms();
                    if ((now - slave->last_exchange) > slave->watchdog_timeout) {
                        slave->state = SLAVE_FAULT;
                    }
                }
                break;
                
            case SLAVE_FAULT:
                // Attempt recovery - restart parameterization
                slave->state = SLAVE_IDLE;
                break;
        }
    }
    
    // Maintain cycle time
    uint32_t elapsed = profibus_get_timestamp_ms() - cycle_start;
    if (elapsed < (master->cycle_time_us / 1000)) {
        // Sleep for remaining time (platform-specific)
    }
}

// Example usage
int main(void) {
    dp_master_class1_t master;
    dp_master_init(&master, 10000); // 10ms cycle time
    
    // Configure a slave: address 3, 4 bytes input, 2 bytes output
    uint8_t config[] = {0xC2, 0x84}; // 2 output, 4 input
    uint8_t params[] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05};
    
    dp_master_add_slave(&master, 3, 4, 2, config, 2, params, 7);
    
    master.bus_active = true;
    
    // Main loop
    while (master.bus_active) {
        dp_master_cycle(&master);
        
        // Application can read/write I/O data:
        // master.output_data[0][0] = sensor_value;
        // actuator_value = master.input_data[0][0];
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::time::{Duration, Instant};
use std::collections::HashMap;

// Slave states
#[derive(Debug, Clone, Copy, PartialEq)]
enum SlaveState {
    Idle,
    WaitPrm,
    WaitCfg,
    DataExchange,
    Fault,
}

// DP telegram types
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
enum DpTelegramType {
    Sdn = 0x06,  // Send Data with No acknowledge
    Sda = 0x0D,  // Send Data with Acknowledge
    Sdr = 0x0C,  // Send and Request Data
    Rdr = 0x0E,  // Request Data with Reply
}

// Slave configuration
#[derive(Debug, Clone)]
struct DpSlave {
    address: u8,
    state: SlaveState,
    input_len: usize,
    output_len: usize,
    config_data: Vec<u8>,
    param_data: Vec<u8>,
    watchdog_timeout: Duration,
    last_exchange: Option<Instant>,
}

impl DpSlave {
    fn new(
        address: u8,
        input_len: usize,
        output_len: usize,
        config: Vec<u8>,
        params: Vec<u8>,
    ) -> Self {
        Self {
            address,
            state: SlaveState::Idle,
            input_len,
            output_len,
            config_data: config,
            param_data: params,
            watchdog_timeout: Duration::from_millis(100),
            last_exchange: None,
        }
    }
}

// Low-level driver trait (to be implemented by hardware layer)
trait ProfibusDriver {
    fn send_telegram(&mut self, dest: u8, fc: u8, data: &[u8]) -> Result<(), String>;
    fn receive_telegram(&mut self, timeout: Duration) -> Result<(u8, u8, Vec<u8>), String>;
}

// Master Class 1 implementation
struct DpMasterClass1<D: ProfibusDriver> {
    slaves: Vec<DpSlave>,
    output_data: HashMap<u8, Vec<u8>>,
    input_data: HashMap<u8, Vec<u8>>,
    cycle_time: Duration,
    driver: D,
    bus_active: bool,
}

impl<D: ProfibusDriver> DpMasterClass1<D> {
    fn new(driver: D, cycle_time: Duration) -> Self {
        Self {
            slaves: Vec::new(),
            output_data: HashMap::new(),
            input_data: HashMap::new(),
            cycle_time,
            driver,
            bus_active: false,
        }
    }

    fn add_slave(
        &mut self,
        address: u8,
        input_len: usize,
        output_len: usize,
        config: Vec<u8>,
        params: Vec<u8>,
    ) {
        let slave = DpSlave::new(address, input_len, output_len, config, params);
        
        // Initialize I/O buffers
        self.output_data.insert(address, vec![0u8; output_len]);
        self.input_data.insert(address, vec![0u8; input_len]);
        
        self.slaves.push(slave);
    }

    fn send_parameters(&mut self, slave: &DpSlave) -> Result<(), String> {
        let mut telegram = vec![0u8; 11];
        telegram[0] = 0x00; // Station status
        telegram[1] = 0x00; // WD_Fact_1
        telegram[2] = 0x00; // WD_Fact_2
        telegram[3] = 0x00; // Min_Tsdr
        
        let param_len = slave.param_data.len().min(7);
        telegram[4..4 + param_len].copy_from_slice(&slave.param_data[..param_len]);
        
        self.driver.send_telegram(
            slave.address,
            DpTelegramType::Sdn as u8,
            &telegram,
        )
    }

    fn send_configuration(&mut self, slave: &DpSlave) -> Result<(), String> {
        let mut config = Vec::new();
        config.push(0xC0 | (slave.output_len as u8)); // Output config
        config.push(0x80 | (slave.input_len as u8));  // Input config
        
        self.driver.send_telegram(
            slave.address,
            DpTelegramType::Sdn as u8,
            &config,
        )
    }

    fn data_exchange(&mut self, slave_idx: usize) -> Result<(), String> {
        let slave = &self.slaves[slave_idx];
        let output = self.output_data.get(&slave.address)
            .ok_or("Output buffer not found")?;
        
        // Send output data and request input (SDR telegram)
        self.driver.send_telegram(
            slave.address,
            DpTelegramType::Sdr as u8,
            output,
        )?;
        
        // Receive input data from slave
        let timeout = Duration::from_millis(10);
        let (src_addr, _fc, recv_data) = self.driver.receive_telegram(timeout)?;
        
        // Validate response
        if src_addr == slave.address && recv_data.len() == slave.input_len {
            if let Some(input_buf) = self.input_data.get_mut(&slave.address) {
                input_buf.copy_from_slice(&recv_data);
            }
            
            // Update timestamp
            self.slaves[slave_idx].last_exchange = Some(Instant::now());
            Ok(())
        } else {
            Err("Invalid response from slave".to_string())
        }
    }

    fn cycle(&mut self) {
        let cycle_start = Instant::now();
        
        for i in 0..self.slaves.len() {
            let current_state = self.slaves[i].state;
            
            match current_state {
                SlaveState::Idle => {
                    if self.send_parameters(&self.slaves[i].clone()).is_ok() {
                        self.slaves[i].state = SlaveState::WaitPrm;
                    }
                }
                
                SlaveState::WaitPrm => {
                    if self.send_configuration(&self.slaves[i].clone()).is_ok() {
                        self.slaves[i].state = SlaveState::WaitCfg;
                    }
                }
                
                SlaveState::WaitCfg => {
                    if self.data_exchange(i).is_ok() {
                        self.slaves[i].state = SlaveState::DataExchange;
                    }
                }
                
                SlaveState::DataExchange => {
                    if self.data_exchange(i).is_err() {
                        // Check watchdog timeout
                        if let Some(last_ex) = self.slaves[i].last_exchange {
                            if last_ex.elapsed() > self.slaves[i].watchdog_timeout {
                                self.slaves[i].state = SlaveState::Fault;
                            }
                        }
                    }
                }
                
                SlaveState::Fault => {
                    // Attempt recovery
                    self.slaves[i].state = SlaveState::Idle;
                }
            }
        }
        
        // Maintain cycle time
        let elapsed = cycle_start.elapsed();
        if elapsed < self.cycle_time {
            std::thread::sleep(self.cycle_time - elapsed);
        }
    }

    // Public API for application layer
    pub fn write_output(&mut self, slave_addr: u8, data: &[u8]) -> Result<(), String> {
        self.output_data
            .get_mut(&slave_addr)
            .ok_or("Slave not found")?
            .copy_from_slice(data);
        Ok(())
    }

    pub fn read_input(&self, slave_addr: u8) -> Result<&[u8], String> {
        self.input_data
            .get(&slave_addr)
            .map(|v| v.as_slice())
            .ok_or("Slave not found".to_string())
    }
}

// Mock driver for demonstration
struct MockProfibusDriver;

impl ProfibusDriver for MockProfibusDriver {
    fn send_telegram(&mut self, _dest: u8, _fc: u8, _data: &[u8]) -> Result<(), String> {
        Ok(())
    }

    fn receive_telegram(&mut self, _timeout: Duration) -> Result<(u8, u8, Vec<u8>), String> {
        Ok((3, 0, vec![0x12, 0x34, 0x56, 0x78]))
    }
}

// Example usage
fn main() {
    let driver = MockProfibusDriver;
    let mut master = DpMasterClass1::new(driver, Duration::from_millis(10));
    
    // Add a slave at address 3
    let config = vec![0xC2, 0x84]; // 2 output bytes, 4 input bytes
    let params = vec![0x00, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05];
    
    master.add_slave(3, 4, 2, config, params);
    master.bus_active = true;
    
    // Main control loop
    for _ in 0..100 {
        master.cycle();
        
        // Application can read/write I/O
        let _ = master.write_output(3, &[0xAA, 0xBB]);
        if let Ok(inputs) = master.read_input(3) {
            println!("Slave inputs: {:02X?}", inputs);
        }
    }
}
```

---

## Summary

**Master Class 1 Implementation** is the core component of a Profibus DP control system, providing deterministic cyclic I/O exchange with field devices. The implementation involves managing a state machine for each slave (parameterization → configuration → data exchange), maintaining strict timing requirements, and handling fault conditions gracefully.

**Key technical aspects include:**
- **State machine management** for slave lifecycle (IDLE → WAIT_PRM → WAIT_CFG → DATA_EXCHANGE)
- **Telegram handling** using DP-specific frame types (SDN, SDA, SDR, RDR)
- **Deterministic timing** to meet real-time control requirements
- **Watchdog monitoring** to detect communication failures
- **I/O buffering** for decoupling application logic from bus communication

Both C/C++ and Rust implementations demonstrate production-grade patterns: the C version shows embedded-friendly structures with explicit memory management, while the Rust version leverages type safety, ownership semantics, and trait-based abstraction for hardware drivers. These implementations form the foundation for industrial automation controllers in manufacturing, process control, and building automation systems.