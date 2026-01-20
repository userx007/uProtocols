# Multi-slave Timing Issues in SPI

## Overview

When working with multiple SPI slaves on the same bus, timing issues become critical. Unlike single-slave configurations, multi-slave setups introduce propagation delays, clock skew, capacitive loading, and signal integrity challenges that can cause communication failures if not properly addressed.

## Key Timing Challenges

### 1. **Propagation Delays**
- Signal travel time increases with bus length and number of devices
- Different slaves may be at different physical distances from the master
- PCB trace lengths affect signal arrival times

### 2. **Clock Skew**
- Clock signal may arrive at different times at different slaves
- MOSI/MISO data signals may not align properly with clock edges
- Inter-symbol interference can occur at higher frequencies

### 3. **Capacitive Loading**
- Each additional slave adds capacitance to the bus
- Slower rise/fall times affect maximum achievable clock speeds
- Signal reflections can cause data corruption

### 4. **CS (Chip Select) Timing**
- Setup and hold times must be met for each slave
- Minimum CS inactive time between transactions varies by device
- Simultaneous CS transitions can cause glitches

## Timing Parameters to Consider

```
Tsetup_CS:  CS setup time before first clock edge
Thold_CS:   CS hold time after last clock edge
Tcs_min:    Minimum CS inactive time between transactions
Tpd:        Propagation delay
Tskew:      Maximum clock skew across slaves
Tsu:        Data setup time
Th:         Data hold time
```

## C/C++ Implementation with Timing Control

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Timing configuration for each slave
typedef struct {
    uint8_t cs_pin;
    uint32_t max_speed_hz;        // Maximum safe clock speed
    uint16_t cs_setup_ns;         // CS setup time in nanoseconds
    uint16_t cs_hold_ns;          // CS hold time in nanoseconds
    uint16_t cs_inactive_ns;      // Minimum CS inactive time
    uint16_t propagation_delay_ns; // Estimated propagation delay
} spi_slave_timing_t;

// Multi-slave SPI bus manager
typedef struct {
    int num_slaves;
    spi_slave_timing_t slaves[8];
    uint8_t current_slave;
    struct timespec last_transaction_time;
    uint32_t bus_capacitance_pf;  // Total bus capacitance in picofarads
} spi_bus_manager_t;

// Initialize bus manager with timing considerations
void spi_bus_init(spi_bus_manager_t *bus, uint32_t base_speed_hz) {
    bus->num_slaves = 0;
    bus->current_slave = 0xFF;
    bus->bus_capacitance_pf = 20; // Base PCB capacitance
    clock_gettime(CLOCK_MONOTONIC, &bus->last_transaction_time);
}

// Add slave with specific timing requirements
bool spi_add_slave(spi_bus_manager_t *bus, spi_slave_timing_t slave_config) {
    if (bus->num_slaves >= 8) return false;
    
    bus->slaves[bus->num_slaves++] = slave_config;
    
    // Update bus capacitance (each slave adds ~5-10pF)
    bus->bus_capacitance_pf += 7;
    
    return true;
}

// Calculate safe clock speed based on bus loading
uint32_t calculate_safe_clock_speed(spi_bus_manager_t *bus, uint8_t slave_id) {
    spi_slave_timing_t *slave = &bus->slaves[slave_id];
    
    // Derating factor based on capacitive loading
    // Formula: f_max = 1 / (2 * pi * R * C)
    // Assuming 50 ohm impedance
    float capacitance_factor = 1.0f - (bus->bus_capacitance_pf / 1000.0f);
    if (capacitance_factor < 0.5f) capacitance_factor = 0.5f;
    
    uint32_t derated_speed = slave->max_speed_hz * capacitance_factor;
    
    // Account for propagation delay
    // Reduce speed if propagation delay is significant
    if (slave->propagation_delay_ns > 10) {
        uint32_t delay_limited_speed = 1000000000UL / (slave->propagation_delay_ns * 10);
        if (delay_limited_speed < derated_speed) {
            derated_speed = delay_limited_speed;
        }
    }
    
    return derated_speed;
}

// Precise delay function (implementation depends on platform)
void delay_nanoseconds(uint32_t ns) {
    struct timespec req = {0, ns};
    nanosleep(&req, NULL);
}

// Select slave with proper timing
void spi_select_slave(spi_bus_manager_t *bus, uint8_t slave_id) {
    if (slave_id >= bus->num_slaves) return;
    
    spi_slave_timing_t *slave = &bus->slaves[slave_id];
    
    // If switching from another slave, ensure CS inactive time
    if (bus->current_slave != 0xFF && bus->current_slave != slave_id) {
        spi_slave_timing_t *prev_slave = &bus->slaves[bus->current_slave];
        
        // Calculate elapsed time since last transaction
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        uint64_t elapsed_ns = (current_time.tv_sec - bus->last_transaction_time.tv_sec) * 1000000000UL +
                              (current_time.tv_nsec - bus->last_transaction_time.tv_nsec);
        
        // Wait if not enough time has passed
        if (elapsed_ns < prev_slave->cs_inactive_ns) {
            delay_nanoseconds(prev_slave->cs_inactive_ns - elapsed_ns);
        }
    }
    
    // Apply CS setup time before asserting CS
    delay_nanoseconds(slave->cs_setup_ns);
    
    // Assert CS (active low)
    gpio_write(slave->cs_pin, 0);
    
    // Additional setup time after CS assertion
    delay_nanoseconds(slave->cs_setup_ns / 2);
    
    // Configure SPI clock for this slave
    uint32_t safe_speed = calculate_safe_clock_speed(bus, slave_id);
    spi_set_clock_speed(safe_speed);
    
    bus->current_slave = slave_id;
}

// Deselect slave with proper timing
void spi_deselect_slave(spi_bus_manager_t *bus) {
    if (bus->current_slave == 0xFF) return;
    
    spi_slave_timing_t *slave = &bus->slaves[bus->current_slave];
    
    // Apply CS hold time before deasserting CS
    delay_nanoseconds(slave->cs_hold_ns);
    
    // Deassert CS (active low)
    gpio_write(slave->cs_pin, 1);
    
    // Record transaction time
    clock_gettime(CLOCK_MONOTONIC, &bus->last_transaction_time);
    
    bus->current_slave = 0xFF;
}

// Transfer with skew compensation
uint8_t spi_transfer_with_compensation(spi_bus_manager_t *bus, uint8_t data) {
    if (bus->current_slave == 0xFF) return 0;
    
    spi_slave_timing_t *slave = &bus->slaves[bus->current_slave];
    
    // Add inter-byte delay for slaves with high propagation delay
    if (slave->propagation_delay_ns > 20) {
        delay_nanoseconds(slave->propagation_delay_ns);
    }
    
    return spi_transfer_byte(data);
}

// Example: Configure multiple slaves with different timing requirements
void example_multi_slave_setup(void) {
    spi_bus_manager_t bus;
    spi_bus_init(&bus, 10000000); // 10 MHz base speed
    
    // Slave 0: Fast flash memory, short traces
    spi_slave_timing_t flash = {
        .cs_pin = 10,
        .max_speed_hz = 50000000,  // 50 MHz max
        .cs_setup_ns = 5,
        .cs_hold_ns = 5,
        .cs_inactive_ns = 50,
        .propagation_delay_ns = 2
    };
    spi_add_slave(&bus, flash);
    
    // Slave 1: ADC with slower timing, longer traces
    spi_slave_timing_t adc = {
        .cs_pin = 11,
        .max_speed_hz = 5000000,   // 5 MHz max
        .cs_setup_ns = 20,
        .cs_hold_ns = 20,
        .cs_inactive_ns = 100,
        .propagation_delay_ns = 15
    };
    spi_add_slave(&bus, adc);
    
    // Slave 2: SD card, moderate timing
    spi_slave_timing_t sdcard = {
        .cs_pin = 12,
        .max_speed_hz = 25000000,  // 25 MHz max
        .cs_setup_ns = 10,
        .cs_hold_ns = 10,
        .cs_inactive_ns = 200,
        .propagation_delay_ns = 8
    };
    spi_add_slave(&bus, sdcard);
    
    // Transaction example
    uint8_t buffer[4];
    
    // Read from flash
    spi_select_slave(&bus, 0);
    buffer[0] = spi_transfer_with_compensation(&bus, 0x03); // Read command
    buffer[1] = spi_transfer_with_compensation(&bus, 0x00);
    buffer[2] = spi_transfer_with_compensation(&bus, 0x00);
    buffer[3] = spi_transfer_with_compensation(&bus, 0x00);
    spi_deselect_slave(&bus);
    
    // Read from ADC (automatically handles timing between slaves)
    spi_select_slave(&bus, 1);
    uint16_t adc_value = spi_transfer_with_compensation(&bus, 0x00) << 8;
    adc_value |= spi_transfer_with_compensation(&bus, 0x00);
    spi_deselect_slave(&bus);
}
```

## Rust Implementation with Type-Safe Timing

```rust
use std::time::{Duration, Instant};
use std::thread;

/// Timing requirements for an SPI slave device
#[derive(Debug, Clone, Copy)]
pub struct SlaveTimingConfig {
    pub cs_pin: u8,
    pub max_speed_hz: u32,
    pub cs_setup_ns: u16,
    pub cs_hold_ns: u16,
    pub cs_inactive_ns: u16,
    pub propagation_delay_ns: u16,
}

/// Multi-slave SPI bus manager with timing control
pub struct SpiBusManager {
    slaves: Vec<SlaveTimingConfig>,
    current_slave: Option<usize>,
    last_transaction: Instant,
    bus_capacitance_pf: u32,
}

impl SpiBusManager {
    /// Create a new SPI bus manager
    pub fn new(base_capacitance_pf: u32) -> Self {
        Self {
            slaves: Vec::new(),
            current_slave: None,
            last_transaction: Instant::now(),
            bus_capacitance_pf: base_capacitance_pf,
        }
    }
    
    /// Add a slave device with its timing requirements
    pub fn add_slave(&mut self, config: SlaveTimingConfig) -> Result<usize, &'static str> {
        if self.slaves.len() >= 8 {
            return Err("Maximum number of slaves reached");
        }
        
        self.slaves.push(config);
        // Each slave adds approximately 7pF of capacitance
        self.bus_capacitance_pf += 7;
        
        Ok(self.slaves.len() - 1)
    }
    
    /// Calculate safe clock speed considering bus loading
    fn calculate_safe_speed(&self, slave_id: usize) -> u32 {
        let slave = &self.slaves[slave_id];
        
        // Derate based on capacitive loading
        let capacitance_factor = (1.0 - (self.bus_capacitance_pf as f32 / 1000.0)).max(0.5);
        let mut derated_speed = (slave.max_speed_hz as f32 * capacitance_factor) as u32;
        
        // Limit based on propagation delay
        if slave.propagation_delay_ns > 10 {
            let delay_limited = 1_000_000_000 / (slave.propagation_delay_ns as u32 * 10);
            derated_speed = derated_speed.min(delay_limited);
        }
        
        derated_speed
    }
    
    /// Precise nanosecond delay
    fn delay_ns(&self, ns: u64) {
        if ns > 0 {
            thread::sleep(Duration::from_nanos(ns));
        }
    }
    
    /// Select a slave with proper timing
    pub fn select_slave(&mut self, slave_id: usize) -> Result<(), &'static str> {
        if slave_id >= self.slaves.len() {
            return Err("Invalid slave ID");
        }
        
        let slave = self.slaves[slave_id];
        
        // Handle CS inactive time when switching slaves
        if let Some(prev_id) = self.current_slave {
            if prev_id != slave_id {
                let prev_slave = self.slaves[prev_id];
                let elapsed = self.last_transaction.elapsed();
                let required = Duration::from_nanos(prev_slave.cs_inactive_ns as u64);
                
                if elapsed < required {
                    self.delay_ns((required - elapsed).as_nanos() as u64);
                }
            }
        }
        
        // CS setup time
        self.delay_ns(slave.cs_setup_ns as u64);
        
        // Assert CS (simulated - would use actual GPIO)
        self.gpio_write(slave.cs_pin, false);
        
        // Additional setup after CS assertion
        self.delay_ns(slave.cs_setup_ns as u64 / 2);
        
        // Configure clock speed for this slave
        let safe_speed = self.calculate_safe_speed(slave_id);
        self.set_clock_speed(safe_speed);
        
        self.current_slave = Some(slave_id);
        Ok(())
    }
    
    /// Deselect current slave with proper timing
    pub fn deselect_slave(&mut self) -> Result<(), &'static str> {
        let slave_id = self.current_slave.ok_or("No slave selected")?;
        let slave = self.slaves[slave_id];
        
        // CS hold time
        self.delay_ns(slave.cs_hold_ns as u64);
        
        // Deassert CS
        self.gpio_write(slave.cs_pin, true);
        
        self.last_transaction = Instant::now();
        self.current_slave = None;
        
        Ok(())
    }
    
    /// Transfer data with skew compensation
    pub fn transfer_with_compensation(&self, data: u8) -> Result<u8, &'static str> {
        let slave_id = self.current_slave.ok_or("No slave selected")?;
        let slave = self.slaves[slave_id];
        
        // Add inter-byte delay for high propagation delay
        if slave.propagation_delay_ns > 20 {
            self.delay_ns(slave.propagation_delay_ns as u64);
        }
        
        // Actual SPI transfer (simulated)
        Ok(self.spi_transfer_byte(data))
    }
    
    /// Transfer multiple bytes with automatic timing
    pub fn transfer_bulk(&self, data: &[u8]) -> Result<Vec<u8>, &'static str> {
        let mut result = Vec::with_capacity(data.len());
        
        for &byte in data {
            result.push(self.transfer_with_compensation(byte)?);
        }
        
        Ok(result)
    }
    
    // Simulated hardware functions (would be actual HAL calls)
    fn gpio_write(&self, _pin: u8, _state: bool) {
        // GPIO write implementation
    }
    
    fn set_clock_speed(&self, _speed: u32) {
        // SPI clock configuration
    }
    
    fn spi_transfer_byte(&self, data: u8) -> u8 {
        // Actual SPI transfer
        data // Echo for simulation
    }
}

/// RAII-style slave selection guard
pub struct SlaveGuard<'a> {
    bus: &'a mut SpiBusManager,
}

impl<'a> SlaveGuard<'a> {
    pub fn new(bus: &'a mut SpiBusManager, slave_id: usize) -> Result<Self, &'static str> {
        bus.select_slave(slave_id)?;
        Ok(Self { bus })
    }
    
    pub fn transfer(&self, data: u8) -> Result<u8, &'static str> {
        self.bus.transfer_with_compensation(data)
    }
    
    pub fn transfer_bulk(&self, data: &[u8]) -> Result<Vec<u8>, &'static str> {
        self.bus.transfer_bulk(data)
    }
}

impl<'a> Drop for SlaveGuard<'a> {
    fn drop(&mut self) {
        let _ = self.bus.deselect_slave();
    }
}

/// Example: Multi-slave configuration
fn example_multi_slave_setup() {
    let mut bus = SpiBusManager::new(20); // 20pF base capacitance
    
    // Add flash memory slave
    let flash_config = SlaveTimingConfig {
        cs_pin: 10,
        max_speed_hz: 50_000_000,
        cs_setup_ns: 5,
        cs_hold_ns: 5,
        cs_inactive_ns: 50,
        propagation_delay_ns: 2,
    };
    let flash_id = bus.add_slave(flash_config).unwrap();
    
    // Add ADC slave
    let adc_config = SlaveTimingConfig {
        cs_pin: 11,
        max_speed_hz: 5_000_000,
        cs_setup_ns: 20,
        cs_hold_ns: 20,
        cs_inactive_ns: 100,
        propagation_delay_ns: 15,
    };
    let adc_id = bus.add_slave(adc_config).unwrap();
    
    // Read from flash using RAII guard
    {
        let flash = SlaveGuard::new(&mut bus, flash_id).unwrap();
        let _cmd = flash.transfer(0x03).unwrap(); // Read command
        let data = flash.transfer_bulk(&[0x00, 0x00, 0x00]).unwrap();
        println!("Flash data: {:?}", data);
    } // Automatically deselects on drop
    
    // Read from ADC
    {
        let adc = SlaveGuard::new(&mut bus, adc_id).unwrap();
        let high = adc.transfer(0x00).unwrap();
        let low = adc.transfer(0x00).unwrap();
        let value = ((high as u16) << 8) | (low as u16);
        println!("ADC value: {}", value);
    }
}

fn main() {
    example_multi_slave_setup();
}
```

## Best Practices for Multi-Slave Timing

### 1. **PCB Layout Considerations**
- Keep trace lengths as equal as possible to all slaves
- Use star topology from master rather than daisy-chain
- Add series termination resistors (22-33Ω) near the master
- Maintain controlled impedance (typically 50Ω)

### 2. **Clock Speed Selection**
- Start with conservative speeds and increase gradually
- Test at temperature extremes
- Derate speed by 20-30% for production margin
- Consider worst-case bus loading

### 3. **Software Strategies**
- Implement per-slave timing profiles
- Add configurable delays for problematic slaves
- Use oscilloscope to measure actual timing
- Validate setup/hold times with margin

### 4. **Signal Integrity**
- Add pull-up/pull-down resistors on CS lines
- Use low-capacitance buffers for heavily loaded buses
- Consider using level shifters with integrated buffers
- Monitor signal quality with eye diagrams

## Summary

Multi-slave SPI timing issues arise from physical realities: propagation delays, capacitive loading, and clock skew. Successfully managing multiple slaves requires:

- **Per-device timing profiles** that respect each slave's requirements
- **Dynamic clock speed adjustment** based on bus loading and slave capabilities
- **Precise delay control** for CS setup/hold and inter-transaction spacing
- **Careful PCB design** to minimize trace length differences and impedance mismatches
- **Conservative margins** to ensure reliable operation across temperature and production variations

The code examples demonstrate practical implementations in both C/C++ and Rust, showing how to track timing parameters, calculate safe clock speeds, and enforce proper delays. The Rust implementation adds type safety through RAII guards that automatically handle slave deselection, reducing the risk of timing violations from forgotten cleanup code.