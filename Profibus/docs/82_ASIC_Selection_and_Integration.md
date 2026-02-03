# PROFIBUS ASIC Selection and Integration

## Overview

PROFIBUS ASICs (Application-Specific Integrated Circuits) are specialized hardware chips designed to handle the complex protocol stack and real-time communication requirements of PROFIBUS networks. Rather than implementing PROFIBUS entirely in software on a general-purpose processor, these ASICs offload the time-critical functions to dedicated hardware, enabling deterministic communication and reducing CPU overhead.

## Common PROFIBUS ASICs

### 1. **SPC3 (Siemens PROFIBUS Communication Controller 3)**
- **Technology**: Older generation, widely used
- **Interface**: Parallel bus interface
- **RAM**: Built-in dual-port RAM (2KB or more depending on variant)
- **Speed**: Supports up to 12 Mbps
- **Features**: Hardware-based protocol handling, interrupt generation, watchdog
- **Use Case**: Legacy designs, cost-sensitive applications

### 2. **SPC4 (Siemens PROFIBUS Communication Controller 4)**
- **Technology**: Enhanced version of SPC3
- **Interface**: Parallel bus with improved timing
- **RAM**: Larger dual-port RAM (up to 8KB)
- **Speed**: Supports up to 12 Mbps with better performance
- **Features**: Enhanced diagnostics, improved timing, backward compatible with SPC3
- **Use Case**: Performance-critical industrial devices

### 3. **LSPM2 (Low-cost Slave PROFIBUS Module 2)**
- **Technology**: Simplified slave-only implementation
- **Interface**: Serial or parallel options
- **RAM**: Minimal integrated memory
- **Speed**: Up to 12 Mbps
- **Features**: Slave-only functionality, reduced cost
- **Use Case**: Simple I/O devices, sensors, actuators

## ASIC Selection Criteria

### Performance Requirements
- **Cycle time**: How fast must the device respond?
- **Data throughput**: How much data per cycle?
- **CPU availability**: How much processing power needed for application tasks?

### Functional Requirements
- **Master vs. Slave**: Most ASICs are slave-only; master functionality requires additional software
- **Isochronous mode**: Not all ASICs support this
- **Diagnostics depth**: More sophisticated devices need better diagnostic capabilities

### Cost and Availability
- **Component cost**: LSPM2 < SPC3 < SPC4
- **Development effort**: More integrated features reduce software complexity
- **Long-term availability**: Consider product lifecycle

## Integration Architecture

```
┌─────────────────────────────────────────────┐
│           Host Microcontroller              │
│  (Application Logic + PROFIBUS Stack SW)    │
└──────────────────┬──────────────────────────┘
                   │ Parallel/Serial Bus
                   │ (Address, Data, Control)
┌──────────────────▼──────────────────────────┐
│         PROFIBUS ASIC (SPC3/SPC4)           │
│  ┌──────────────────────────────────────┐   │
│  │    Dual-Port RAM (DPR)               │   │
│  │  - Input data buffer                 │   │
│  │  - Output data buffer                │   │
│  │  - Control/Status registers          │   │
│  └──────────────────────────────────────┘   │
│  ┌──────────────────────────────────────┐   │
│  │    Protocol State Machine            │   │
│  └──────────────────────────────────────┘   │
└──────────────────┬──────────────────────────┘
                   │ RS-485 Physical Layer
                   │
┌──────────────────▼──────────────────────────┐
│         RS-485 Transceiver                  │
└──────────────────┬──────────────────────────┘
                   │
              PROFIBUS Network
```

## C/C++ Programming Examples

### SPC3 Initialization and Communication

```c
#include <stdint.h>
#include <stdbool.h>

// SPC3 Register Definitions
#define SPC3_BASE_ADDR      0x40000000  // Memory-mapped base address

// SPC3 Register Offsets
#define SPC3_MODE_REG0      0x00
#define SPC3_MODE_REG1      0x01
#define SPC3_STATUS         0x02
#define SPC3_INT_REQ        0x03
#define SPC3_INT_MASK       0x04
#define SPC3_WD_TIME        0x05
#define SPC3_ADDR_REG       0x06
#define SPC3_BAUD_RATE      0x07

// Dual-Port RAM offsets
#define DPR_INPUT_OFFSET    0x0100
#define DPR_OUTPUT_OFFSET   0x0200
#define DPR_DIAG_OFFSET     0x0300

// Helper macros
#define SPC3_WRITE_REG(offset, value) \
    (*((volatile uint8_t*)(SPC3_BASE_ADDR + offset)) = (value))
    
#define SPC3_READ_REG(offset) \
    (*((volatile uint8_t*)(SPC3_BASE_ADDR + offset)))

// SPC3 Status bits
#define SPC3_STATUS_BAUDRATE_OK    (1 << 0)
#define SPC3_STATUS_OPERATIONAL    (1 << 1)
#define SPC3_STATUS_DATA_EXCHANGE  (1 << 2)

// Interrupt request bits
#define SPC3_INT_NEW_OUTPUT_DATA   (1 << 0)
#define SPC3_INT_NEW_PARAM_DATA    (1 << 1)
#define SPC3_INT_DIAG_CHANGE       (1 << 2)

typedef struct {
    uint8_t station_address;
    uint8_t ident_high;
    uint8_t ident_low;
    uint8_t input_len;
    uint8_t output_len;
    bool initialized;
} SPC3_Config;

// Initialize SPC3 ASIC
bool spc3_init(SPC3_Config* config) {
    // Reset SPC3
    SPC3_WRITE_REG(SPC3_MODE_REG0, 0x00);
    
    // Wait for reset completion
    for (volatile int i = 0; i < 1000; i++);
    
    // Configure station address
    SPC3_WRITE_REG(SPC3_ADDR_REG, config->station_address);
    
    // Set watchdog time (100ms)
    SPC3_WRITE_REG(SPC3_WD_TIME, 100);
    
    // Configure mode register
    // Bit 0: Enable operation
    // Bit 1: Enable watchdog
    // Bit 2: Freeze mode off
    SPC3_WRITE_REG(SPC3_MODE_REG0, 0x07);
    
    // Enable interrupts
    SPC3_WRITE_REG(SPC3_INT_MASK, 
                   SPC3_INT_NEW_OUTPUT_DATA | 
                   SPC3_INT_NEW_PARAM_DATA | 
                   SPC3_INT_DIAG_CHANGE);
    
    // Set baudrate to auto-detect
    SPC3_WRITE_REG(SPC3_BAUD_RATE, 0xFF);
    
    config->initialized = true;
    return true;
}

// Read input data from application to DPR
void spc3_update_inputs(const uint8_t* data, uint8_t length) {
    volatile uint8_t* dpr_input = 
        (volatile uint8_t*)(SPC3_BASE_ADDR + DPR_INPUT_OFFSET);
    
    for (uint8_t i = 0; i < length; i++) {
        dpr_input[i] = data[i];
    }
}

// Read output data from DPR to application
void spc3_read_outputs(uint8_t* data, uint8_t length) {
    volatile uint8_t* dpr_output = 
        (volatile uint8_t*)(SPC3_BASE_ADDR + DPR_OUTPUT_OFFSET);
    
    for (uint8_t i = 0; i < length; i++) {
        data[i] = dpr_output[i];
    }
}

// Interrupt service routine
void spc3_isr(void) {
    uint8_t int_req = SPC3_READ_REG(SPC3_INT_REQ);
    
    if (int_req & SPC3_INT_NEW_OUTPUT_DATA) {
        // Master has sent new output data
        uint8_t output_buffer[32];
        spc3_read_outputs(output_buffer, 32);
        
        // Process output data in application
        // process_output_data(output_buffer);
        
        // Clear interrupt
        SPC3_WRITE_REG(SPC3_INT_REQ, SPC3_INT_NEW_OUTPUT_DATA);
    }
    
    if (int_req & SPC3_INT_NEW_PARAM_DATA) {
        // Master has sent new configuration parameters
        // handle_parameter_update();
        
        SPC3_WRITE_REG(SPC3_INT_REQ, SPC3_INT_NEW_PARAM_DATA);
    }
    
    if (int_req & SPC3_INT_DIAG_CHANGE) {
        // Diagnostic status has changed
        // handle_diagnostic_event();
        
        SPC3_WRITE_REG(SPC3_INT_REQ, SPC3_INT_DIAG_CHANGE);
    }
}

// Main application loop
int main(void) {
    SPC3_Config config = {
        .station_address = 5,
        .ident_high = 0x08,  // Device identification
        .ident_low = 0x7B,
        .input_len = 16,
        .output_len = 16,
        .initialized = false
    };
    
    // Initialize SPC3
    if (!spc3_init(&config)) {
        // Handle initialization error
        return -1;
    }
    
    // Wait for baudrate detection and operational state
    while (!(SPC3_READ_REG(SPC3_STATUS) & SPC3_STATUS_BAUDRATE_OK)) {
        // Wait for baudrate synchronization
    }
    
    uint8_t input_data[16] = {0};
    
    // Main loop
    while (1) {
        // Check if in data exchange mode
        if (SPC3_READ_REG(SPC3_STATUS) & SPC3_STATUS_DATA_EXCHANGE) {
            // Read sensor data or prepare process inputs
            input_data[0] = read_sensor_1();
            input_data[1] = read_sensor_2();
            
            // Update input data in DPR
            spc3_update_inputs(input_data, sizeof(input_data));
        }
        
        // Application tasks
        // ...
    }
    
    return 0;
}
```

### C++ Object-Oriented Wrapper

```cpp
#include <cstdint>
#include <array>
#include <functional>
#include <optional>

class ProfibusASIC {
public:
    enum class ASICType {
        SPC3,
        SPC4,
        LSPM2
    };
    
    enum class State {
        Idle,
        BaudrateDetect,
        Operational,
        DataExchange,
        Fault
    };
    
    struct Config {
        uint8_t station_address;
        uint16_t ident_number;
        size_t input_length;
        size_t output_length;
        uint8_t watchdog_time_ms;
    };
    
    using OutputDataCallback = std::function<void(const uint8_t*, size_t)>;
    using DiagnosticCallback = std::function<void(uint8_t)>;
    
private:
    uintptr_t base_address_;
    ASICType type_;
    Config config_;
    State state_;
    OutputDataCallback output_callback_;
    DiagnosticCallback diag_callback_;
    
    // Memory-mapped register access
    volatile uint8_t& reg(uint16_t offset) {
        return *reinterpret_cast<volatile uint8_t*>(base_address_ + offset);
    }
    
    volatile uint8_t* dpr(uint16_t offset) {
        return reinterpret_cast<volatile uint8_t*>(base_address_ + offset);
    }
    
public:
    ProfibusASIC(uintptr_t base_addr, ASICType type)
        : base_address_(base_addr)
        , type_(type)
        , state_(State::Idle)
    {}
    
    bool initialize(const Config& config) {
        config_ = config;
        
        // Hardware reset
        reg(0x00) = 0x00;
        delay_us(100);
        
        // Configure station address
        reg(0x06) = config.station_address;
        
        // Set watchdog
        reg(0x05) = config.watchdog_time_ms;
        
        // Enable operation
        reg(0x00) = 0x07;  // Enable + Watchdog + Run
        
        // Enable interrupts
        reg(0x04) = 0x07;  // All interrupts
        
        // Auto baudrate
        reg(0x07) = 0xFF;
        
        state_ = State::BaudrateDetect;
        return true;
    }
    
    void setOutputCallback(OutputDataCallback callback) {
        output_callback_ = std::move(callback);
    }
    
    void setDiagnosticCallback(DiagnosticCallback callback) {
        diag_callback_ = std::move(callback);
    }
    
    State getState() const {
        uint8_t status = const_cast<ProfibusASIC*>(this)->reg(0x02);
        
        if (status & 0x04) return State::DataExchange;
        if (status & 0x02) return State::Operational;
        if (status & 0x01) return State::BaudrateDetect;
        
        return State::Idle;
    }
    
    bool writeInputs(const uint8_t* data, size_t length) {
        if (length > config_.input_length) return false;
        
        volatile uint8_t* input_area = dpr(0x0100);
        for (size_t i = 0; i < length; i++) {
            input_area[i] = data[i];
        }
        return true;
    }
    
    template<size_t N>
    bool writeInputs(const std::array<uint8_t, N>& data) {
        return writeInputs(data.data(), N);
    }
    
    std::optional<std::vector<uint8_t>> readOutputs() {
        if (state_ != State::DataExchange) {
            return std::nullopt;
        }
        
        std::vector<uint8_t> output(config_.output_length);
        volatile uint8_t* output_area = dpr(0x0200);
        
        for (size_t i = 0; i < config_.output_length; i++) {
            output[i] = output_area[i];
        }
        
        return output;
    }
    
    void handleInterrupt() {
        uint8_t int_req = reg(0x03);
        
        if (int_req & 0x01) {  // New output data
            if (output_callback_) {
                volatile uint8_t* output_area = dpr(0x0200);
                output_callback_(const_cast<const uint8_t*>(output_area), 
                               config_.output_length);
            }
            reg(0x03) = 0x01;  // Clear interrupt
        }
        
        if (int_req & 0x04) {  // Diagnostic change
            if (diag_callback_) {
                uint8_t diag_status = *dpr(0x0300);
                diag_callback_(diag_status);
            }
            reg(0x03) = 0x04;
        }
    }
    
private:
    static void delay_us(uint32_t us) {
        for (volatile uint32_t i = 0; i < us * 10; i++);
    }
};

// Usage example
void example_usage() {
    ProfibusASIC asic(0x40000000, ProfibusASIC::ASICType::SPC3);
    
    ProfibusASIC::Config config{
        .station_address = 5,
        .ident_number = 0x087B,
        .input_length = 16,
        .output_length = 16,
        .watchdog_time_ms = 100
    };
    
    asic.initialize(config);
    
    // Set callback for output data
    asic.setOutputCallback([](const uint8_t* data, size_t len) {
        // Process received output data from master
        for (size_t i = 0; i < len; i++) {
            // control_actuator(i, data[i]);
        }
    });
    
    // Main loop
    std::array<uint8_t, 16> sensor_data{};
    while (true) {
        if (asic.getState() == ProfibusASIC::State::DataExchange) {
            // Update sensor readings
            sensor_data[0] = 0x42;  // read_sensor()
            asic.writeInputs(sensor_data);
        }
    }
}
```

## Rust Programming Examples

### Low-Level Register Interface

```rust
use core::ptr::{read_volatile, write_volatile};

// ASIC register definitions
const SPC3_BASE: usize = 0x4000_0000;
const MODE_REG0: usize = 0x00;
const MODE_REG1: usize = 0x01;
const STATUS: usize = 0x02;
const INT_REQ: usize = 0x03;
const INT_MASK: usize = 0x04;
const WD_TIME: usize = 0x05;
const ADDR_REG: usize = 0x06;
const BAUD_RATE: usize = 0x07;

const DPR_INPUT: usize = 0x0100;
const DPR_OUTPUT: usize = 0x0200;
const DPR_DIAG: usize = 0x0300;

// Status bits
const STATUS_BAUDRATE_OK: u8 = 1 << 0;
const STATUS_OPERATIONAL: u8 = 1 << 1;
const STATUS_DATA_EXCHANGE: u8 = 1 << 2;

// Interrupt bits
const INT_NEW_OUTPUT: u8 = 1 << 0;
const INT_NEW_PARAM: u8 = 1 << 1;
const INT_DIAG_CHANGE: u8 = 1 << 2;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ASICState {
    Idle,
    BaudrateDetect,
    Operational,
    DataExchange,
    Fault,
}

#[derive(Debug, Clone)]
pub struct ASICConfig {
    pub station_address: u8,
    pub ident_number: u16,
    pub input_length: usize,
    pub output_length: usize,
    pub watchdog_ms: u8,
}

pub struct SPC3Controller {
    base_addr: usize,
    config: ASICConfig,
}

impl SPC3Controller {
    pub fn new(base_addr: usize, config: ASICConfig) -> Self {
        Self { base_addr, config }
    }
    
    // Low-level register access
    fn write_reg(&self, offset: usize, value: u8) {
        unsafe {
            write_volatile((self.base_addr + offset) as *mut u8, value);
        }
    }
    
    fn read_reg(&self, offset: usize) -> u8 {
        unsafe {
            read_volatile((self.base_addr + offset) as *const u8)
        }
    }
    
    // DPR access
    fn write_dpr(&self, offset: usize, data: &[u8]) {
        unsafe {
            let ptr = (self.base_addr + offset) as *mut u8;
            for (i, &byte) in data.iter().enumerate() {
                write_volatile(ptr.add(i), byte);
            }
        }
    }
    
    fn read_dpr(&self, offset: usize, length: usize) -> Vec<u8> {
        unsafe {
            let ptr = (self.base_addr + offset) as *const u8;
            (0..length)
                .map(|i| read_volatile(ptr.add(i)))
                .collect()
        }
    }
    
    pub fn initialize(&mut self) -> Result<(), &'static str> {
        // Reset ASIC
        self.write_reg(MODE_REG0, 0x00);
        
        // Small delay for reset
        for _ in 0..1000 {
            core::hint::spin_loop();
        }
        
        // Configure station address
        self.write_reg(ADDR_REG, self.config.station_address);
        
        // Set watchdog time
        self.write_reg(WD_TIME, self.config.watchdog_ms);
        
        // Enable operation: Run + Watchdog + Enable
        self.write_reg(MODE_REG0, 0x07);
        
        // Enable all interrupts
        self.write_reg(INT_MASK, INT_NEW_OUTPUT | INT_NEW_PARAM | INT_DIAG_CHANGE);
        
        // Auto baudrate detection
        self.write_reg(BAUD_RATE, 0xFF);
        
        Ok(())
    }
    
    pub fn get_state(&self) -> ASICState {
        let status = self.read_reg(STATUS);
        
        if status & STATUS_DATA_EXCHANGE != 0 {
            ASICState::DataExchange
        } else if status & STATUS_OPERATIONAL != 0 {
            ASICState::Operational
        } else if status & STATUS_BAUDRATE_OK != 0 {
            ASICState::BaudrateDetect
        } else {
            ASICState::Idle
        }
    }
    
    pub fn write_inputs(&self, data: &[u8]) -> Result<(), &'static str> {
        if data.len() > self.config.input_length {
            return Err("Input data too long");
        }
        
        self.write_dpr(DPR_INPUT, data);
        Ok(())
    }
    
    pub fn read_outputs(&self) -> Vec<u8> {
        self.read_dpr(DPR_OUTPUT, self.config.output_length)
    }
    
    pub fn handle_interrupt(&self) -> InterruptFlags {
        let int_req = self.read_reg(INT_REQ);
        
        let flags = InterruptFlags {
            new_output_data: int_req & INT_NEW_OUTPUT != 0,
            new_param_data: int_req & INT_NEW_PARAM != 0,
            diag_change: int_req & INT_DIAG_CHANGE != 0,
        };
        
        // Clear interrupts
        self.write_reg(INT_REQ, int_req);
        
        flags
    }
}

#[derive(Debug, Clone, Copy)]
pub struct InterruptFlags {
    pub new_output_data: bool,
    pub new_param_data: bool,
    pub diag_change: bool,
}

// Safe, higher-level abstraction
pub struct ProfibusDevice {
    controller: SPC3Controller,
    output_handler: Option<fn(&[u8])>,
}

impl ProfibusDevice {
    pub fn new(config: ASICConfig) -> Self {
        let controller = SPC3Controller::new(SPC3_BASE, config);
        Self {
            controller,
            output_handler: None,
        }
    }
    
    pub fn init(&mut self) -> Result<(), &'static str> {
        self.controller.initialize()?;
        
        // Wait for baudrate detection
        while self.controller.get_state() != ASICState::Operational 
            && self.controller.get_state() != ASICState::DataExchange {
            core::hint::spin_loop();
        }
        
        Ok(())
    }
    
    pub fn set_output_handler(&mut self, handler: fn(&[u8])) {
        self.output_handler = Some(handler);
    }
    
    pub fn update_inputs(&self, data: &[u8]) -> Result<(), &'static str> {
        if self.controller.get_state() == ASICState::DataExchange {
            self.controller.write_inputs(data)
        } else {
            Err("Not in data exchange state")
        }
    }
    
    pub fn process_interrupts(&self) {
        let flags = self.controller.handle_interrupt();
        
        if flags.new_output_data {
            let output_data = self.controller.read_outputs();
            if let Some(handler) = self.output_handler {
                handler(&output_data);
            }
        }
        
        if flags.new_param_data {
            // Handle parameter update
        }
        
        if flags.diag_change {
            // Handle diagnostic event
        }
    }
}

// Usage example
fn main() {
    let config = ASICConfig {
        station_address: 5,
        ident_number: 0x087B,
        input_length: 16,
        output_length: 16,
        watchdog_ms: 100,
    };
    
    let mut device = ProfibusDevice::new(config);
    
    device.init().expect("Failed to initialize PROFIBUS ASIC");
    
    device.set_output_handler(|data| {
        // Process output data from master
        for (i, &value) in data.iter().enumerate() {
            println!("Output {}: {}", i, value);
        }
    });
    
    // Main loop
    let mut input_data = [0u8; 16];
    loop {
        // Update sensor readings
        input_data[0] = read_sensor();
        
        if let Err(e) = device.update_inputs(&input_data) {
            eprintln!("Failed to update inputs: {}", e);
        }
        
        // Process any pending interrupts
        device.process_interrupts();
        
        // Other application tasks...
    }
}

fn read_sensor() -> u8 {
    // Simulate sensor reading
    42
}
```

## Summary

**PROFIBUS ASIC integration** involves selecting appropriate hardware (SPC3, SPC4, or LSPM2) based on performance, cost, and functional requirements, then implementing the software interface to control the ASIC and manage data exchange through dual-port RAM.

**Key Points:**
- **Hardware offload**: ASICs handle time-critical protocol tasks, freeing the host CPU for application logic
- **Dual-port RAM architecture**: Shared memory between host and ASIC enables efficient data exchange
- **Interrupt-driven**: ASICs signal events (new data, diagnostics) via interrupts for responsive operation
- **Register interface**: Control via memory-mapped registers for configuration and status monitoring
- **Selection criteria**: Balance performance needs, cost constraints, and feature requirements
- **Integration patterns**: Memory-mapped I/O with DPR buffers for inputs/outputs and control/status registers

The choice of ASIC and quality of integration directly impact system performance, determinism, and development complexity in PROFIBUS applications.