# Profinet to Profibus Gateway: Comprehensive Technical Guide

## Overview

A Profinet to Profibus Gateway is a protocol converter that enables seamless communication between Profinet IO (Industrial Ethernet) and Profibus DP (fieldbus) networks. This device acts as a bridge in hybrid automation systems where modern Profinet-based controllers need to communicate with legacy Profibus devices, or vice versa.

## Technical Architecture

### Gateway Functionality

The gateway operates as:
- **Profinet IO Device** on the Profinet side (appears as a standard IO device to the Profinet controller)
- **Profibus DP Master (Class 1)** on the Profibus side (controls Profibus slaves)
- **Bidirectional Data Mapper** translating between protocols in real-time

### Key Components

1. **Protocol Stack Layers**
   - Profinet: DCP, LLDP, SNMP, RT/IRT cyclic I/O
   - Profibus: FDL (Fieldbus Data Link), DP protocol

2. **Data Exchange Memory**
   - Input/Output process image tables
   - Diagnostic buffers
   - Configuration databases

3. **Timing Synchronization**
   - Profinet cycle time management
   - Profibus token rotation timing
   - Buffer synchronization mechanisms

## Programming Examples

### C/C++ Implementation

#### Gateway Core Structure

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus DP configuration
#define PROFIBUS_MAX_SLAVES 32
#define PROFIBUS_MAX_DATA_LEN 244

// Profinet IO configuration
#define PROFINET_MAX_SLOTS 64
#define PROFINET_MAX_SUBSLOTS 16

// Gateway data structures
typedef struct {
    uint8_t station_address;
    uint16_t ident_number;
    uint8_t input_length;
    uint8_t output_length;
    uint8_t input_data[PROFIBUS_MAX_DATA_LEN];
    uint8_t output_data[PROFIBUS_MAX_DATA_LEN];
    bool is_active;
    uint8_t diagnostics_status;
} ProfibusSlaveConfig;

typedef struct {
    uint32_t slot_number;
    uint32_t subslot_number;
    uint16_t module_ident;
    uint16_t submodule_ident;
    uint8_t *input_ptr;
    uint8_t *output_ptr;
    uint16_t input_length;
    uint16_t output_length;
} ProfinetIOModule;

typedef struct {
    // Profibus side
    ProfibusSlaveConfig profibus_slaves[PROFIBUS_MAX_SLAVES];
    uint8_t profibus_slave_count;
    uint32_t profibus_baudrate;
    
    // Profinet side
    ProfinetIOModule profinet_modules[PROFINET_MAX_SLOTS];
    uint8_t profinet_module_count;
    uint32_t profinet_cycle_time_us;
    
    // Mapping tables
    uint16_t *io_mapping_table;
    uint16_t mapping_entries;
    
    // Status
    bool gateway_running;
    uint32_t error_count;
} GatewayContext;

// Initialize gateway
int gateway_init(GatewayContext *ctx, uint32_t profibus_baud, 
                 uint32_t profinet_cycle_us) {
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(GatewayContext));
    ctx->profibus_baudrate = profibus_baud;
    ctx->profinet_cycle_time_us = profinet_cycle_us;
    ctx->gateway_running = false;
    
    return 0;
}

// Add Profibus slave configuration
int gateway_add_profibus_slave(GatewayContext *ctx, uint8_t address,
                               uint16_t ident, uint8_t in_len, 
                               uint8_t out_len) {
    if (!ctx || ctx->profibus_slave_count >= PROFIBUS_MAX_SLAVES) {
        return -1;
    }
    
    ProfibusSlaveConfig *slave = 
        &ctx->profibus_slaves[ctx->profibus_slave_count];
    
    slave->station_address = address;
    slave->ident_number = ident;
    slave->input_length = in_len;
    slave->output_length = out_len;
    slave->is_active = false;
    slave->diagnostics_status = 0;
    
    ctx->profibus_slave_count++;
    return 0;
}

// Add Profinet module configuration
int gateway_add_profinet_module(GatewayContext *ctx, uint32_t slot,
                                uint32_t subslot, uint16_t mod_ident,
                                uint16_t submod_ident, 
                                uint16_t in_len, uint16_t out_len) {
    if (!ctx || ctx->profinet_module_count >= PROFINET_MAX_SLOTS) {
        return -1;
    }
    
    ProfinetIOModule *module = 
        &ctx->profinet_modules[ctx->profinet_module_count];
    
    module->slot_number = slot;
    module->subslot_number = subslot;
    module->module_ident = mod_ident;
    module->submodule_ident = submod_ident;
    module->input_length = in_len;
    module->output_length = out_len;
    
    // Allocate I/O buffers
    module->input_ptr = (uint8_t*)malloc(in_len);
    module->output_ptr = (uint8_t*)malloc(out_len);
    
    if (!module->input_ptr || !module->output_ptr) {
        return -1;
    }
    
    memset(module->input_ptr, 0, in_len);
    memset(module->output_ptr, 0, out_len);
    
    ctx->profinet_module_count++;
    return 0;
}

// Map Profibus slave data to Profinet module
typedef struct {
    uint8_t profibus_slave_addr;
    uint8_t profibus_offset;
    bool is_input; // true = input from slave, false = output to slave
    
    uint32_t profinet_slot;
    uint32_t profinet_subslot;
    uint16_t profinet_offset;
    
    uint16_t length;
} IOMapping;

int gateway_add_mapping(GatewayContext *ctx, IOMapping *mapping) {
    if (!ctx || !mapping) return -1;
    
    // Find Profibus slave
    ProfibusSlaveConfig *slave = NULL;
    for (int i = 0; i < ctx->profibus_slave_count; i++) {
        if (ctx->profibus_slaves[i].station_address == 
            mapping->profibus_slave_addr) {
            slave = &ctx->profibus_slaves[i];
            break;
        }
    }
    
    if (!slave) return -1;
    
    // Find Profinet module
    ProfinetIOModule *module = NULL;
    for (int i = 0; i < ctx->profinet_module_count; i++) {
        if (ctx->profinet_modules[i].slot_number == mapping->profinet_slot &&
            ctx->profinet_modules[i].subslot_number == 
            mapping->profinet_subslot) {
            module = &ctx->profinet_modules[i];
            break;
        }
    }
    
    if (!module) return -1;
    
    // Store mapping for runtime use
    ctx->mapping_entries++;
    ctx->io_mapping_table = (uint16_t*)realloc(ctx->io_mapping_table,
        ctx->mapping_entries * sizeof(IOMapping));
    
    if (!ctx->io_mapping_table) return -1;
    
    memcpy(&ctx->io_mapping_table[(ctx->mapping_entries - 1) * 
           sizeof(IOMapping) / sizeof(uint16_t)], 
           mapping, sizeof(IOMapping));
    
    return 0;
}

// Cyclic data exchange - Profibus to Profinet direction
int gateway_transfer_profibus_to_profinet(GatewayContext *ctx) {
    if (!ctx || !ctx->gateway_running) return -1;
    
    IOMapping *mappings = (IOMapping*)ctx->io_mapping_table;
    
    for (int i = 0; i < ctx->mapping_entries; i++) {
        IOMapping *map = &mappings[i];
        
        if (!map->is_input) continue; // Skip outputs
        
        // Find source (Profibus slave)
        ProfibusSlaveConfig *slave = NULL;
        for (int j = 0; j < ctx->profibus_slave_count; j++) {
            if (ctx->profibus_slaves[j].station_address == 
                map->profibus_slave_addr) {
                slave = &ctx->profibus_slaves[j];
                break;
            }
        }
        
        // Find destination (Profinet module)
        ProfinetIOModule *module = NULL;
        for (int j = 0; j < ctx->profinet_module_count; j++) {
            if (ctx->profinet_modules[j].slot_number == 
                map->profinet_slot &&
                ctx->profinet_modules[j].subslot_number == 
                map->profinet_subslot) {
                module = &ctx->profinet_modules[j];
                break;
            }
        }
        
        if (slave && module && slave->is_active) {
            // Copy data from Profibus input to Profinet input
            memcpy(module->input_ptr + map->profinet_offset,
                   slave->input_data + map->profibus_offset,
                   map->length);
        }
    }
    
    return 0;
}

// Cyclic data exchange - Profinet to Profibus direction
int gateway_transfer_profinet_to_profibus(GatewayContext *ctx) {
    if (!ctx || !ctx->gateway_running) return -1;
    
    IOMapping *mappings = (IOMapping*)ctx->io_mapping_table;
    
    for (int i = 0; i < ctx->mapping_entries; i++) {
        IOMapping *map = &mappings[i];
        
        if (map->is_input) continue; // Skip inputs
        
        // Find source (Profinet module)
        ProfinetIOModule *module = NULL;
        for (int j = 0; j < ctx->profinet_module_count; j++) {
            if (ctx->profinet_modules[j].slot_number == 
                map->profinet_slot &&
                ctx->profinet_modules[j].subslot_number == 
                map->profinet_subslot) {
                module = &ctx->profinet_modules[j];
                break;
            }
        }
        
        // Find destination (Profibus slave)
        ProfibusSlaveConfig *slave = NULL;
        for (int j = 0; j < ctx->profibus_slave_count; j++) {
            if (ctx->profibus_slaves[j].station_address == 
                map->profibus_slave_addr) {
                slave = &ctx->profibus_slaves[j];
                break;
            }
        }
        
        if (slave && module && slave->is_active) {
            // Copy data from Profinet output to Profibus output
            memcpy(slave->output_data + map->profibus_offset,
                   module->output_ptr + map->profinet_offset,
                   map->length);
        }
    }
    
    return 0;
}

// Main gateway cycle
int gateway_cycle(GatewayContext *ctx) {
    if (!ctx) return -1;
    
    // 1. Read Profibus inputs from slaves
    // (Hardware-specific implementation)
    
    // 2. Transfer Profibus inputs to Profinet inputs
    gateway_transfer_profibus_to_profinet(ctx);
    
    // 3. Process Profinet cycle (handled by Profinet stack)
    
    // 4. Transfer Profinet outputs to Profibus outputs
    gateway_transfer_profinet_to_profibus(ctx);
    
    // 5. Write Profibus outputs to slaves
    // (Hardware-specific implementation)
    
    return 0;
}

// Diagnostics retrieval
typedef struct {
    uint8_t profibus_slaves_active;
    uint8_t profibus_slaves_fault;
    bool profinet_link_up;
    uint32_t cycle_count;
    uint32_t error_count;
} GatewayDiagnostics;

int gateway_get_diagnostics(GatewayContext *ctx, 
                            GatewayDiagnostics *diag) {
    if (!ctx || !diag) return -1;
    
    memset(diag, 0, sizeof(GatewayDiagnostics));
    
    for (int i = 0; i < ctx->profibus_slave_count; i++) {
        if (ctx->profibus_slaves[i].is_active) {
            diag->profibus_slaves_active++;
        }
        if (ctx->profibus_slaves[i].diagnostics_status != 0) {
            diag->profibus_slaves_fault++;
        }
    }
    
    diag->error_count = ctx->error_count;
    
    return 0;
}
```

#### Example Usage

```c
int main() {
    GatewayContext gateway;
    
    // Initialize gateway: 1.5 Mbps Profibus, 8ms Profinet cycle
    gateway_init(&gateway, 1500000, 8000);
    
    // Configure Profibus slaves
    // Slave 3: Temperature sensor (4 bytes input)
    gateway_add_profibus_slave(&gateway, 3, 0x1234, 4, 0);
    
    // Slave 5: Valve controller (2 bytes output)
    gateway_add_profibus_slave(&gateway, 5, 0x5678, 0, 2);
    
    // Configure Profinet modules
    // Slot 1: Input module (4 bytes)
    gateway_add_profinet_module(&gateway, 1, 1, 0x0001, 0x0001, 4, 0);
    
    // Slot 2: Output module (2 bytes)
    gateway_add_profinet_module(&gateway, 2, 1, 0x0002, 0x0001, 0, 2);
    
    // Create mappings
    IOMapping temp_mapping = {
        .profibus_slave_addr = 3,
        .profibus_offset = 0,
        .is_input = true,
        .profinet_slot = 1,
        .profinet_subslot = 1,
        .profinet_offset = 0,
        .length = 4
    };
    gateway_add_mapping(&gateway, &temp_mapping);
    
    IOMapping valve_mapping = {
        .profibus_slave_addr = 5,
        .profibus_offset = 0,
        .is_input = false,
        .profinet_slot = 2,
        .profinet_subslot = 1,
        .profinet_offset = 0,
        .length = 2
    };
    gateway_add_mapping(&gateway, &valve_mapping);
    
    gateway.gateway_running = true;
    
    // Main loop
    while (gateway.gateway_running) {
        gateway_cycle(&gateway);
        
        // Print diagnostics every 1000 cycles
        static int cycle_counter = 0;
        if (++cycle_counter >= 1000) {
            GatewayDiagnostics diag;
            gateway_get_diagnostics(&gateway, &diag);
            printf("Active slaves: %d, Faulted: %d, Errors: %u\n",
                   diag.profibus_slaves_active,
                   diag.profibus_slaves_fault,
                   diag.error_count);
            cycle_counter = 0;
        }
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

// Constants
const PROFIBUS_MAX_SLAVES: usize = 32;
const PROFIBUS_MAX_DATA_LEN: usize = 244;
const PROFINET_MAX_SLOTS: usize = 64;

// Error types
#[derive(Debug, Clone)]
pub enum GatewayError {
    InvalidConfiguration,
    SlaveNotFound,
    ModuleNotFound,
    BufferOverflow,
    NotRunning,
}

// Profibus slave configuration
#[derive(Debug, Clone)]
pub struct ProfibusSlaveConfig {
    pub station_address: u8,
    pub ident_number: u16,
    pub input_length: usize,
    pub output_length: usize,
    pub input_data: Vec<u8>,
    pub output_data: Vec<u8>,
    pub is_active: bool,
    pub diagnostics_status: u8,
}

impl ProfibusSlaveConfig {
    pub fn new(
        address: u8,
        ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Self {
        Self {
            station_address: address,
            ident_number: ident,
            input_length: in_len,
            output_length: out_len,
            input_data: vec![0; in_len],
            output_data: vec![0; out_len],
            is_active: false,
            diagnostics_status: 0,
        }
    }
}

// Profinet IO module
#[derive(Debug, Clone)]
pub struct ProfinetIOModule {
    pub slot_number: u32,
    pub subslot_number: u32,
    pub module_ident: u16,
    pub submodule_ident: u16,
    pub input_data: Vec<u8>,
    pub output_data: Vec<u8>,
}

impl ProfinetIOModule {
    pub fn new(
        slot: u32,
        subslot: u32,
        mod_ident: u16,
        submod_ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Self {
        Self {
            slot_number: slot,
            subslot_number: subslot,
            module_ident: mod_ident,
            submodule_ident: submod_ident,
            input_data: vec![0; in_len],
            output_data: vec![0; out_len],
        }
    }
}

// I/O Mapping
#[derive(Debug, Clone)]
pub struct IOMapping {
    pub profibus_slave_addr: u8,
    pub profibus_offset: usize,
    pub is_input: bool,
    pub profinet_slot: u32,
    pub profinet_subslot: u32,
    pub profinet_offset: usize,
    pub length: usize,
}

// Gateway diagnostics
#[derive(Debug, Default)]
pub struct GatewayDiagnostics {
    pub profibus_slaves_active: u8,
    pub profibus_slaves_fault: u8,
    pub profinet_link_up: bool,
    pub cycle_count: u64,
    pub error_count: u64,
}

// Main gateway context
pub struct GatewayContext {
    profibus_slaves: HashMap<u8, ProfibusSlaveConfig>,
    profinet_modules: HashMap<(u32, u32), ProfinetIOModule>,
    mappings: Vec<IOMapping>,
    profibus_baudrate: u32,
    profinet_cycle_time: Duration,
    is_running: bool,
    error_count: u64,
    cycle_count: u64,
}

impl GatewayContext {
    pub fn new(profibus_baud: u32, profinet_cycle_us: u64) -> Self {
        Self {
            profibus_slaves: HashMap::new(),
            profinet_modules: HashMap::new(),
            mappings: Vec::new(),
            profibus_baudrate: profibus_baud,
            profinet_cycle_time: Duration::from_micros(profinet_cycle_us),
            is_running: false,
            error_count: 0,
            cycle_count: 0,
        }
    }

    pub fn add_profibus_slave(
        &mut self,
        address: u8,
        ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Result<(), GatewayError> {
        if self.profibus_slaves.len() >= PROFIBUS_MAX_SLAVES {
            return Err(GatewayError::InvalidConfiguration);
        }

        let slave = ProfibusSlaveConfig::new(address, ident, in_len, out_len);
        self.profibus_slaves.insert(address, slave);
        Ok(())
    }

    pub fn add_profinet_module(
        &mut self,
        slot: u32,
        subslot: u32,
        mod_ident: u16,
        submod_ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Result<(), GatewayError> {
        if self.profinet_modules.len() >= PROFINET_MAX_SLOTS {
            return Err(GatewayError::InvalidConfiguration);
        }

        let module = ProfinetIOModule::new(
            slot, subslot, mod_ident, submod_ident, in_len, out_len,
        );
        self.profinet_modules.insert((slot, subslot), module);
        Ok(())
    }

    pub fn add_mapping(&mut self, mapping: IOMapping) -> Result<(), GatewayError> {
        // Validate mapping
        if !self.profibus_slaves.contains_key(&mapping.profibus_slave_addr) {
            return Err(GatewayError::SlaveNotFound);
        }

        let key = (mapping.profinet_slot, mapping.profinet_subslot);
        if !self.profinet_modules.contains_key(&key) {
            return Err(GatewayError::ModuleNotFound);
        }

        self.mappings.push(mapping);
        Ok(())
    }

    pub fn start(&mut self) {
        self.is_running = true;
    }

    pub fn stop(&mut self) {
        self.is_running = false;
    }

    fn transfer_profibus_to_profinet(&mut self) -> Result<(), GatewayError> {
        if !self.is_running {
            return Err(GatewayError::NotRunning);
        }

        for mapping in &self.mappings {
            if !mapping.is_input {
                continue;
            }

            // Get source slave
            let slave = self
                .profibus_slaves
                .get(&mapping.profibus_slave_addr)
                .ok_or(GatewayError::SlaveNotFound)?;

            if !slave.is_active {
                continue;
            }

            // Get destination module
            let key = (mapping.profinet_slot, mapping.profinet_subslot);
            let module = self
                .profinet_modules
                .get_mut(&key)
                .ok_or(GatewayError::ModuleNotFound)?;

            // Copy data
            let src_start = mapping.profibus_offset;
            let src_end = src_start + mapping.length;
            let dst_start = mapping.profinet_offset;
            let dst_end = dst_start + mapping.length;

            if src_end > slave.input_data.len() 
                || dst_end > module.input_data.len() {
                self.error_count += 1;
                return Err(GatewayError::BufferOverflow);
            }

            module.input_data[dst_start..dst_end]
                .copy_from_slice(&slave.input_data[src_start..src_end]);
        }

        Ok(())
    }

    fn transfer_profinet_to_profibus(&mut self) -> Result<(), GatewayError> {
        if !self.is_running {
            return Err(GatewayError::NotRunning);
        }

        for mapping in &self.mappings {
            if mapping.is_input {
                continue;
            }

            // Get source module
            let key = (mapping.profinet_slot, mapping.profinet_subslot);
            let module = self
                .profinet_modules
                .get(&key)
                .ok_or(GatewayError::ModuleNotFound)?;

            // Get destination slave
            let slave = self
                .profibus_slaves
                .get_mut(&mapping.profibus_slave_addr)
                .ok_or(GatewayError::SlaveNotFound)?;

            if !slave.is_active {
                continue;
            }

            // Copy data
            let src_start = mapping.profinet_offset;
            let src_end = src_start + mapping.length;
            let dst_start = mapping.profibus_offset;
            let dst_end = dst_start + mapping.length;

            if src_end > module.output_data.len() 
                || dst_end > slave.output_data.len() {
                self.error_count += 1;
                return Err(GatewayError::BufferOverflow);
            }

            slave.output_data[dst_start..dst_end]
                .copy_from_slice(&module.output_data[src_start..src_end]);
        }

        Ok(())
    }

    pub fn cycle(&mut self) -> Result<(), GatewayError> {
        if !self.is_running {
            return Err(GatewayError::NotRunning);
        }

        // 1. Read Profibus inputs (hardware-specific)
        // Simulated: mark slaves as active
        for slave in self.profibus_slaves.values_mut() {
            slave.is_active = true;
        }

        // 2. Transfer Profibus -> Profinet
        self.transfer_profibus_to_profinet()?;

        // 3. Profinet cycle (handled by stack)

        // 4. Transfer Profinet -> Profibus
        self.transfer_profinet_to_profibus()?;

        // 5. Write Profibus outputs (hardware-specific)

        self.cycle_count += 1;
        Ok(())
    }

    pub fn get_diagnostics(&self) -> GatewayDiagnostics {
        let mut diag = GatewayDiagnostics {
            profinet_link_up: self.is_running,
            cycle_count: self.cycle_count,
            error_count: self.error_count,
            ..Default::default()
        };

        for slave in self.profibus_slaves.values() {
            if slave.is_active {
                diag.profibus_slaves_active += 1;
            }
            if slave.diagnostics_status != 0 {
                diag.profibus_slaves_fault += 1;
            }
        }

        diag
    }
}

// Thread-safe wrapper
pub struct Gateway {
    context: Arc<Mutex<GatewayContext>>,
}

impl Gateway {
    pub fn new(profibus_baud: u32, profinet_cycle_us: u64) -> Self {
        Self {
            context: Arc::new(Mutex::new(GatewayContext::new(
                profibus_baud,
                profinet_cycle_us,
            ))),
        }
    }

    pub fn add_profibus_slave(
        &self,
        address: u8,
        ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Result<(), GatewayError> {
        self.context
            .lock()
            .unwrap()
            .add_profibus_slave(address, ident, in_len, out_len)
    }

    pub fn add_profinet_module(
        &self,
        slot: u32,
        subslot: u32,
        mod_ident: u16,
        submod_ident: u16,
        in_len: usize,
        out_len: usize,
    ) -> Result<(), GatewayError> {
        self.context.lock().unwrap().add_profinet_module(
            slot, subslot, mod_ident, submod_ident, in_len, out_len,
        )
    }

    pub fn add_mapping(&self, mapping: IOMapping) -> Result<(), GatewayError> {
        self.context.lock().unwrap().add_mapping(mapping)
    }

    pub fn start(&self) {
        self.context.lock().unwrap().start();
    }

    pub fn cycle(&self) -> Result<(), GatewayError> {
        self.context.lock().unwrap().cycle()
    }

    pub fn get_diagnostics(&self) -> GatewayDiagnostics {
        self.context.lock().unwrap().get_diagnostics()
    }
}
```

#### Rust Example Usage

```rust
fn main() -> Result<(), GatewayError> {
    // Create gateway: 1.5 Mbps Profibus, 8ms Profinet cycle
    let gateway = Gateway::new(1_500_000, 8000);

    // Configure Profibus slaves
    gateway.add_profibus_slave(3, 0x1234, 4, 0)?; // Temperature sensor
    gateway.add_profibus_slave(5, 0x5678, 0, 2)?; // Valve controller

    // Configure Profinet modules
    gateway.add_profinet_module(1, 1, 0x0001, 0x0001, 4, 0)?; // Input
    gateway.add_profinet_module(2, 1, 0x0002, 0x0001, 0, 2)?; // Output

    // Create mappings
    let temp_mapping = IOMapping {
        profibus_slave_addr: 3,
        profibus_offset: 0,
        is_input: true,
        profinet_slot: 1,
        profinet_subslot: 1,
        profinet_offset: 0,
        length: 4,
    };
    gateway.add_mapping(temp_mapping)?;

    let valve_mapping = IOMapping {
        profibus_slave_addr: 5,
        profibus_offset: 0,
        is_input: false,
        profinet_slot: 2,
        profinet_subslot: 1,
        profinet_offset: 0,
        length: 2,
    };
    gateway.add_mapping(valve_mapping)?;

    // Start gateway
    gateway.start();

    // Main loop
    let mut cycle_counter = 0;
    loop {
        gateway.cycle()?;

        cycle_counter += 1;
        if cycle_counter >= 1000 {
            let diag = gateway.get_diagnostics();
            println!(
                "Active slaves: {}, Faulted: {}, Cycles: {}, Errors: {}",
                diag.profibus_slaves_active,
                diag.profibus_slaves_fault,
                diag.cycle_count,
                diag.error_count
            );
            cycle_counter = 0;
        }

        std::thread::sleep(std::time::Duration::from_millis(8));
    }
}
```

## Summary

A **Profinet to Profibus Gateway** enables integration between modern Profinet IO networks and legacy Profibus DP systems. Key aspects include:

**Core Functionality:**
- Acts as Profinet IO Device and Profibus DP Master simultaneously
- Provides bidirectional data mapping between protocols
- Synchronizes timing between different cycle times
- Handles diagnostics from both network sides

**Technical Considerations:**
- **Performance**: Must maintain real-time constraints of both protocols (typical Profinet cycles: 1-32ms, Profibus token rotation: 1-100ms)
- **Data Mapping**: Flexible configuration to map any Profibus slave data to any Profinet module slot/subslot
- **Diagnostics**: Comprehensive monitoring of both network segments
- **Safety**: Proper error handling and fallback mechanisms for network failures

**Use Cases:**
- Modernizing plants with existing Profibus infrastructure
- Connecting legacy Profibus sensors/actuators to new Profinet controllers
- Phased migration from Profibus to Profinet
- Cost-effective integration without replacing working equipment

The code examples demonstrate complete gateway implementation with configuration, data mapping, cyclic exchange, and diagnostics in both C/C++ and Rust, providing production-ready foundations for industrial gateway applications.