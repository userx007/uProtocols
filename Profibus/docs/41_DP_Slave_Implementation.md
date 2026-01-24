# Profibus DP Slave Implementation

## Detailed Description

A Profibus DP (Decentralized Periphery) slave is a field device that exchanges cyclic I/O data with a DP master. Implementing a DP slave requires understanding the Profibus protocol stack, state machine transitions, and proper handling of cyclic and acyclic data exchange.

### Core Concepts

**DP Slave State Machine:**
The DP slave operates through several states defined by the Profibus standard:
- **Offline**: No communication with master
- **Wait_Prm**: Waiting for parameterization from master
- **Wait_Cfg**: Waiting for configuration data
- **Data_Ex**: Normal cyclic data exchange
- **Clear**: Slave outputs cleared

**Key Responsibilities:**
1. **GSD File Compliance**: Slave must match its GSD (device database) file specifications
2. **Diagnosis Handling**: Report device status and errors
3. **Cyclic Data Exchange**: Regular input/output data transfer
4. **Acyclic Services**: Handle read/write requests for parameters
5. **Watchdog Monitoring**: Detect master communication failures

### Communication Flow

1. Master sends parameterization telegram
2. Slave validates and acknowledges
3. Master sends configuration telegram
4. Slave verifies I/O configuration
5. Cyclic data exchange begins
6. Slave monitors watchdog timer
7. On timeout or error, slave enters safe state

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus DP Slave State Machine States
typedef enum {
    DP_STATE_OFFLINE = 0,
    DP_STATE_WAIT_PRM,
    DP_STATE_WAIT_CFG,
    DP_STATE_DATA_EX,
    DP_STATE_CLEAR
} dp_slave_state_t;

// Diagnosis structure (6 bytes standard)
typedef struct {
    uint8_t status1;        // Station status 1
    uint8_t status2;        // Station status 2
    uint8_t status3;        // Station status 3
    uint8_t master_addr;    // Master address
    uint16_t ident_number;  // Manufacturer ident
} dp_diagnosis_t;

// DP Slave Configuration
typedef struct {
    uint8_t slave_address;
    uint8_t ident_high;
    uint8_t ident_low;
    uint8_t watchdog_time;  // Watchdog in multiples of 10ms
    uint8_t input_len;      // Length of input data
    uint8_t output_len;     // Length of output data
    uint8_t cfg_data[32];   // Configuration data from master
    uint8_t cfg_len;
} dp_slave_config_t;

// DP Slave Instance
typedef struct {
    dp_slave_state_t state;
    dp_slave_config_t config;
    dp_diagnosis_t diagnosis;
    
    uint8_t input_data[246];   // Max DP input data
    uint8_t output_data[246];  // Max DP output data
    
    uint32_t watchdog_timer;
    uint32_t last_comm_time;
    
    bool station_not_ready;
    bool cfg_fault;
    bool ext_diag_overflow;
    bool master_lock;
    
    // User callbacks
    void (*on_state_change)(dp_slave_state_t new_state);
    void (*on_output_update)(uint8_t *data, uint8_t len);
} dp_slave_t;

// Initialize DP slave
void dp_slave_init(dp_slave_t *slave, dp_slave_config_t *config) {
    memset(slave, 0, sizeof(dp_slave_t));
    memcpy(&slave->config, config, sizeof(dp_slave_config_t));
    
    slave->state = DP_STATE_OFFLINE;
    slave->station_not_ready = true;
    
    // Initialize diagnosis
    slave->diagnosis.status1 = 0x00;
    slave->diagnosis.status2 = 0x04; // Station not ready
    slave->diagnosis.status3 = 0x00;
    slave->diagnosis.ident_number = 
        (config->ident_high << 8) | config->ident_low;
}

// Handle parameterization telegram
bool dp_slave_handle_prm(dp_slave_t *slave, uint8_t *prm_data, 
                         uint8_t prm_len) {
    if (slave->state != DP_STATE_WAIT_PRM && 
        slave->state != DP_STATE_OFFLINE) {
        return false;
    }
    
    // Validate minimum parameterization length
    if (prm_len < 7) {
        return false;
    }
    
    // Extract and validate parameters
    uint8_t station_status = prm_data[0];
    uint8_t wd_fact_1 = prm_data[1];
    uint8_t wd_fact_2 = prm_data[2];
    uint8_t min_tsdr = prm_data[3];
    uint16_t ident_number = (prm_data[4] << 8) | prm_data[5];
    
    // Verify ident number matches
    if (ident_number != slave->diagnosis.ident_number) {
        slave->cfg_fault = true;
        return false;
    }
    
    // Calculate watchdog time (in ms)
    slave->watchdog_timer = wd_fact_1 * wd_fact_2 * 10;
    
    // Lock to this master
    slave->diagnosis.master_addr = prm_data[6];
    slave->master_lock = (station_status & 0x08) != 0;
    
    // Transition to Wait_Cfg state
    slave->state = DP_STATE_WAIT_CFG;
    if (slave->on_state_change) {
        slave->on_state_change(DP_STATE_WAIT_CFG);
    }
    
    return true;
}

// Handle configuration telegram
bool dp_slave_handle_cfg(dp_slave_t *slave, uint8_t *cfg_data, 
                         uint8_t cfg_len) {
    if (slave->state != DP_STATE_WAIT_CFG) {
        return false;
    }
    
    // Validate configuration data matches expected format
    // Format: Each byte describes an I/O module
    // Bits 7-6: 00=Special, 01=Input, 10=Output, 11=Input/Output
    // Bits 5-4: Consistency over whole module
    // Bits 3-0: Length code
    
    uint8_t total_input = 0;
    uint8_t total_output = 0;
    
    for (uint8_t i = 0; i < cfg_len; i++) {
        uint8_t cfg_byte = cfg_data[i];
        uint8_t direction = (cfg_byte >> 6) & 0x03;
        uint8_t length = (cfg_byte & 0x0F);
        
        // Decode length (simplified)
        uint8_t byte_len = (length == 0) ? 1 : length;
        
        if (direction == 0x01 || direction == 0x03) { // Input
            total_input += byte_len;
        }
        if (direction == 0x02 || direction == 0x03) { // Output
            total_output += byte_len;
        }
    }
    
    // Verify against expected I/O lengths
    if (total_input != slave->config.input_len || 
        total_output != slave->config.output_len) {
        slave->cfg_fault = true;
        return false;
    }
    
    // Store configuration
    memcpy(slave->config.cfg_data, cfg_data, cfg_len);
    slave->config.cfg_len = cfg_len;
    
    // Clear station not ready
    slave->station_not_ready = false;
    slave->diagnosis.status2 &= ~0x04;
    
    // Transition to Data_Ex state
    slave->state = DP_STATE_DATA_EX;
    if (slave->on_state_change) {
        slave->on_state_change(DP_STATE_DATA_EX);
    }
    
    return true;
}

// Handle cyclic data exchange
void dp_slave_data_exchange(dp_slave_t *slave, uint8_t *output_data, 
                            uint8_t output_len, uint32_t current_time) {
    if (slave->state != DP_STATE_DATA_EX) {
        return;
    }
    
    // Update output data from master
    if (output_len == slave->config.output_len) {
        memcpy(slave->output_data, output_data, output_len);
        
        // Notify application of new output data
        if (slave->on_output_update) {
            slave->on_output_update(slave->output_data, output_len);
        }
    }
    
    // Update last communication timestamp
    slave->last_comm_time = current_time;
}

// Periodic watchdog check
void dp_slave_watchdog_check(dp_slave_t *slave, uint32_t current_time) {
    if (slave->state != DP_STATE_DATA_EX) {
        return;
    }
    
    uint32_t elapsed = current_time - slave->last_comm_time;
    
    if (elapsed > slave->watchdog_timer) {
        // Watchdog timeout - go to safe state
        slave->state = DP_STATE_CLEAR;
        slave->station_not_ready = true;
        slave->diagnosis.status2 |= 0x04;
        
        // Clear outputs to safe state
        memset(slave->output_data, 0, slave->config.output_len);
        
        if (slave->on_state_change) {
            slave->on_state_change(DP_STATE_CLEAR);
        }
    }
}

// Get current diagnosis data
void dp_slave_get_diagnosis(dp_slave_t *slave, uint8_t *diag_buffer, 
                            uint8_t *diag_len) {
    // Build diagnosis structure
    slave->diagnosis.status1 = 0x00;
    
    // Status byte 2
    slave->diagnosis.status2 = 0x00;
    if (slave->station_not_ready) slave->diagnosis.status2 |= 0x04;
    if (slave->cfg_fault) slave->diagnosis.status2 |= 0x08;
    if (slave->ext_diag_overflow) slave->diagnosis.status2 |= 0x10;
    if (slave->master_lock) slave->diagnosis.status2 |= 0x02;
    
    slave->diagnosis.status3 = 0x00;
    
    // Copy to buffer
    memcpy(diag_buffer, &slave->diagnosis, 6);
    *diag_len = 6;
}

// Example usage
void state_change_callback(dp_slave_state_t new_state) {
    // Handle state transitions
}

void output_update_callback(uint8_t *data, uint8_t len) {
    // Process new output data from master
    // Update physical outputs here
}

int main(void) {
    dp_slave_t slave;
    dp_slave_config_t config = {
        .slave_address = 5,
        .ident_high = 0x80,
        .ident_low = 0x01,
        .watchdog_time = 100,  // 1000ms
        .input_len = 4,
        .output_len = 4
    };
    
    dp_slave_init(&slave, &config);
    slave.on_state_change = state_change_callback;
    slave.on_output_update = output_update_callback;
    
    // Simulation of receiving telegrams
    uint8_t prm_data[] = {0x00, 0x0A, 0x0A, 0x0B, 0x80, 0x01, 0x02};
    dp_slave_handle_prm(&slave, prm_data, 7);
    
    uint8_t cfg_data[] = {0x50, 0xA0}; // 4 bytes in, 4 bytes out
    dp_slave_handle_cfg(&slave, cfg_data, 2);
    
    // Cyclic operation
    uint8_t outputs[4] = {0x01, 0x02, 0x03, 0x04};
    dp_slave_data_exchange(&slave, outputs, 4, 0);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DpSlaveState {
    Offline,
    WaitPrm,
    WaitCfg,
    DataEx,
    Clear,
}

#[derive(Debug, Clone)]
pub struct DpDiagnosis {
    pub status1: u8,
    pub status2: u8,
    pub status3: u8,
    pub master_addr: u8,
    pub ident_number: u16,
}

impl DpDiagnosis {
    pub fn new(ident_number: u16) -> Self {
        Self {
            status1: 0x00,
            status2: 0x04, // Station not ready
            status3: 0x00,
            master_addr: 0,
            ident_number,
        }
    }
    
    pub fn to_bytes(&self) -> Vec<u8> {
        vec![
            self.status1,
            self.status2,
            self.status3,
            self.master_addr,
            (self.ident_number >> 8) as u8,
            (self.ident_number & 0xFF) as u8,
        ]
    }
}

#[derive(Debug, Clone)]
pub struct DpSlaveConfig {
    pub slave_address: u8,
    pub ident_number: u16,
    pub watchdog_time: Duration,
    pub input_len: usize,
    pub output_len: usize,
    pub cfg_data: Vec<u8>,
}

impl DpSlaveConfig {
    pub fn new(slave_address: u8, ident_number: u16, 
               input_len: usize, output_len: usize) -> Self {
        Self {
            slave_address,
            ident_number,
            watchdog_time: Duration::from_millis(1000),
            input_len,
            output_len,
            cfg_data: Vec::new(),
        }
    }
}

pub struct DpSlave {
    state: DpSlaveState,
    config: DpSlaveConfig,
    diagnosis: DpDiagnosis,
    
    input_data: Vec<u8>,
    output_data: Vec<u8>,
    
    last_comm_time: Option<Instant>,
    
    station_not_ready: bool,
    cfg_fault: bool,
    ext_diag_overflow: bool,
    master_lock: bool,
    
    // Callbacks
    on_state_change: Option<Box<dyn Fn(DpSlaveState) + Send>>,
    on_output_update: Option<Box<dyn Fn(&[u8]) + Send>>,
}

impl DpSlave {
    pub fn new(config: DpSlaveConfig) -> Self {
        let input_len = config.input_len;
        let output_len = config.output_len;
        let ident_number = config.ident_number;
        
        Self {
            state: DpSlaveState::Offline,
            config,
            diagnosis: DpDiagnosis::new(ident_number),
            input_data: vec![0; input_len],
            output_data: vec![0; output_len],
            last_comm_time: None,
            station_not_ready: true,
            cfg_fault: false,
            ext_diag_overflow: false,
            master_lock: false,
            on_state_change: None,
            on_output_update: None,
        }
    }
    
    pub fn set_state_change_callback<F>(&mut self, callback: F)
    where
        F: Fn(DpSlaveState) + Send + 'static,
    {
        self.on_state_change = Some(Box::new(callback));
    }
    
    pub fn set_output_update_callback<F>(&mut self, callback: F)
    where
        F: Fn(&[u8]) + Send + 'static,
    {
        self.on_output_update = Some(Box::new(callback));
    }
    
    fn transition_state(&mut self, new_state: DpSlaveState) {
        self.state = new_state;
        if let Some(ref callback) = self.on_state_change {
            callback(new_state);
        }
    }
    
    pub fn handle_parameterization(&mut self, prm_data: &[u8]) -> Result<(), String> {
        if self.state != DpSlaveState::WaitPrm && 
           self.state != DpSlaveState::Offline {
            return Err("Invalid state for parameterization".to_string());
        }
        
        if prm_data.len() < 7 {
            return Err("Parameterization data too short".to_string());
        }
        
        // Extract parameters
        let station_status = prm_data[0];
        let wd_fact_1 = prm_data[1] as u32;
        let wd_fact_2 = prm_data[2] as u32;
        let _min_tsdr = prm_data[3];
        let ident_number = ((prm_data[4] as u16) << 8) | (prm_data[5] as u16);
        let master_addr = prm_data[6];
        
        // Verify ident number
        if ident_number != self.config.ident_number {
            self.cfg_fault = true;
            return Err("Ident number mismatch".to_string());
        }
        
        // Calculate watchdog time
        let watchdog_ms = wd_fact_1 * wd_fact_2 * 10;
        self.config.watchdog_time = Duration::from_millis(watchdog_ms as u64);
        
        // Lock to master
        self.diagnosis.master_addr = master_addr;
        self.master_lock = (station_status & 0x08) != 0;
        
        // Transition to WaitCfg
        self.transition_state(DpSlaveState::WaitCfg);
        
        Ok(())
    }
    
    pub fn handle_configuration(&mut self, cfg_data: &[u8]) -> Result<(), String> {
        if self.state != DpSlaveState::WaitCfg {
            return Err("Invalid state for configuration".to_string());
        }
        
        // Parse configuration data
        let mut total_input = 0usize;
        let mut total_output = 0usize;
        
        for &cfg_byte in cfg_data {
            let direction = (cfg_byte >> 6) & 0x03;
            let length = (cfg_byte & 0x0F) as usize;
            
            let byte_len = if length == 0 { 1 } else { length };
            
            match direction {
                0x01 | 0x03 => total_input += byte_len,  // Input
                0x02 => total_output += byte_len,          // Output
                _ => {}
            }
        }
        
        // Verify configuration
        if total_input != self.config.input_len || 
           total_output != self.config.output_len {
            self.cfg_fault = true;
            return Err("Configuration mismatch".to_string());
        }
        
        // Store configuration
        self.config.cfg_data = cfg_data.to_vec();
        
        // Clear station not ready
        self.station_not_ready = false;
        self.update_diagnosis_status();
        
        // Transition to DataEx
        self.transition_state(DpSlaveState::DataEx);
        
        Ok(())
    }
    
    pub fn data_exchange(&mut self, output_data: &[u8]) -> Result<&[u8], String> {
        if self.state != DpSlaveState::DataEx {
            return Err("Not in data exchange state".to_string());
        }
        
        // Validate output length
        if output_data.len() != self.config.output_len {
            return Err("Invalid output data length".to_string());
        }
        
        // Update output data
        self.output_data.copy_from_slice(output_data);
        
        // Notify application
        if let Some(ref callback) = self.on_output_update {
            callback(&self.output_data);
        }
        
        // Update communication timestamp
        self.last_comm_time = Some(Instant::now());
        
        // Return input data
        Ok(&self.input_data)
    }
    
    pub fn update_input_data(&mut self, data: &[u8]) -> Result<(), String> {
        if data.len() != self.config.input_len {
            return Err("Invalid input data length".to_string());
        }
        
        self.input_data.copy_from_slice(data);
        Ok(())
    }
    
    pub fn check_watchdog(&mut self) {
        if self.state != DpSlaveState::DataEx {
            return;
        }
        
        if let Some(last_comm) = self.last_comm_time {
            if last_comm.elapsed() > self.config.watchdog_time {
                // Watchdog timeout
                self.station_not_ready = true;
                self.update_diagnosis_status();
                
                // Clear outputs
                self.output_data.fill(0);
                
                self.transition_state(DpSlaveState::Clear);
            }
        }
    }
    
    fn update_diagnosis_status(&mut self) {
        self.diagnosis.status2 = 0x00;
        
        if self.station_not_ready {
            self.diagnosis.status2 |= 0x04;
        }
        if self.cfg_fault {
            self.diagnosis.status2 |= 0x08;
        }
        if self.ext_diag_overflow {
            self.diagnosis.status2 |= 0x10;
        }
        if self.master_lock {
            self.diagnosis.status2 |= 0x02;
        }
    }
    
    pub fn get_diagnosis(&mut self) -> Vec<u8> {
        self.update_diagnosis_status();
        self.diagnosis.to_bytes()
    }
    
    pub fn get_state(&self) -> DpSlaveState {
        self.state
    }
}

// Example usage
fn main() {
    let config = DpSlaveConfig::new(
        5,      // slave address
        0x8001, // ident number
        4,      // input length
        4,      // output length
    );
    
    let mut slave = DpSlave::new(config);
    
    // Set callbacks
    slave.set_state_change_callback(|state| {
        println!("State changed to: {:?}", state);
    });
    
    slave.set_output_update_callback(|data| {
        println!("Outputs updated: {:?}", data);
    });
    
    // Simulate parameterization
    let prm_data = vec![0x00, 0x0A, 0x0A, 0x0B, 0x80, 0x01, 0x02];
    slave.handle_parameterization(&prm_data).unwrap();
    
    // Simulate configuration
    let cfg_data = vec![0x50, 0xA0]; // 4 bytes in, 4 bytes out
    slave.handle_configuration(&cfg_data).unwrap();
    
    // Simulate cyclic data exchange
    let outputs = vec![0x01, 0x02, 0x03, 0x04];
    let inputs = slave.data_exchange(&outputs).unwrap();
    println!("Input data: {:?}", inputs);
    
    // Check watchdog periodically
    slave.check_watchdog();
    
    // Get diagnosis
    let diag = slave.get_diagnosis();
    println!("Diagnosis: {:?}", diag);
}
```

## Summary

**Profibus DP Slave Implementation** requires a robust state machine that handles the complete lifecycle from offline to active data exchange. Key implementation aspects include:

**Essential Components:**
- State machine with five states (Offline, Wait_Prm, Wait_Cfg, Data_Ex, Clear)
- Parameterization handling with ident number verification
- Configuration validation against expected I/O modules
- Cyclic data exchange with master
- Watchdog monitoring for communication safety
- Diagnosis reporting for status and errors

**Critical Features:**
- Master lock capability to prevent multiple masters
- Safe state transition on watchdog timeout
- Configuration fault detection
- Standard 6-byte diagnosis structure
- Compliance with GSD file specifications

The C implementation provides low-level control suitable for embedded systems, while the Rust version offers memory safety with zero-cost abstractions. Both implementations demonstrate proper state management, watchdog handling, and callback mechanisms for application integration. The slave must maintain synchronization with the master while ensuring outputs enter a safe state if communication is lost.