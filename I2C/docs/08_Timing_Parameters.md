# I2C Timing Parameters: A Comprehensive Guide

I2C communication relies on precise timing relationships between the clock (SCL) and data (SDA) lines. Understanding these timing parameters is crucial for reliable communication, especially at higher speeds or when designing custom I2C implementations.

## Core Timing Parameters

### 1. **Setup Time (tSU;DAT)**
The minimum time the data line (SDA) must be stable *before* the clock line (SCL) goes high. This ensures the receiver can reliably sample the data bit.

- **Standard mode (100 kHz)**: 250 ns minimum
- **Fast mode (400 kHz)**: 100 ns minimum  
- **Fast mode Plus (1 MHz)**: 50 ns minimum

### 2. **Hold Time (tHD;DAT)**
The minimum time the data line must remain stable *after* the clock line goes low. This ensures the transmitter doesn't change data while it's still being sampled.

- **Standard mode**: 0 ns (but practically 300 ns for noise immunity)
- **Fast mode**: 0 ns (but 300 ns recommended)
- **Fast mode Plus**: 0 ns

Note: After a START condition, the hold time is different (tHD;STA).

### 3. **Rise Time (tr)**
The time it takes for a signal to transition from 30% to 70% of VDD (or from 0.3V to 0.7×VDD). This is largely determined by the bus capacitance and pull-up resistors.

- **Standard mode**: 1000 ns maximum
- **Fast mode**: 300 ns maximum
- **Fast mode Plus**: 120 ns maximum

### 4. **Fall Time (tf)**
The time it takes for a signal to transition from 70% to 30% of VDD. Typically faster than rise time since it's actively driven low.

- **Standard mode**: 300 ns maximum
- **Fast mode**: 300 ns maximum
- **Fast mode Plus**: 120 ns maximum

### 5. **START and STOP Condition Timing**

**Setup time for START condition (tSU;STA)**: Minimum time SCL must be high before SDA goes low to generate a START condition.
- **All modes**: 4.7 μs (Standard), 0.6 μs (Fast), 0.26 μs (Fast Plus)

**Hold time for START condition (tHD;STA)**: Minimum time SDA must be held low after SCL goes low after a START.
- **Standard**: 4.0 μs, **Fast**: 0.6 μs, **Fast Plus**: 0.26 μs

**Setup time for STOP condition (tSU;STO)**: Minimum time SCL must be high before SDA goes high to generate a STOP.
- **Standard**: 4.0 μs, **Fast**: 0.6 μs, **Fast Plus**: 0.26 μs

### 6. **Bus Free Time (tBUF)**
The minimum time the bus must be idle (both lines high) between a STOP and a subsequent START condition.
- **Standard**: 4.7 μs, **Fast**: 1.3 μs, **Fast Plus**: 0.5 μs

## Code Examples

I2C Timing Parameters - C Implementation

```c
/*
 * I2C Timing Parameters Implementation in C
 * Demonstrates bit-banged I2C with precise timing control
 */

#include <stdint.h>
#include <stdbool.h>

// Timing constants for different I2C modes (in microseconds)
typedef enum {
    I2C_STANDARD_MODE = 0,  // 100 kHz
    I2C_FAST_MODE = 1,      // 400 kHz
    I2C_FAST_PLUS_MODE = 2  // 1 MHz
} i2c_mode_t;

typedef struct {
    float tSU_DAT;    // Data setup time (us)
    float tHD_DAT;    // Data hold time (us)
    float tHD_STA;    // START hold time (us)
    float tSU_STA;    // START setup time (us)
    float tSU_STO;    // STOP setup time (us)
    float tBUF;       // Bus free time (us)
    float tLOW;       // SCL low period (us)
    float tHIGH;      // SCL high period (us)
    float tr_max;     // Max rise time (us)
    float tf_max;     // Max fall time (us)
} i2c_timing_t;

// Timing specifications for each mode
static const i2c_timing_t i2c_timings[] = {
    // Standard Mode (100 kHz)
    {
        .tSU_DAT = 0.250,  // 250 ns
        .tHD_DAT = 0.300,  // 300 ns (0 min, but 300 recommended)
        .tHD_STA = 4.0,    // 4.0 us
        .tSU_STA = 4.7,    // 4.7 us
        .tSU_STO = 4.0,    // 4.0 us
        .tBUF = 4.7,       // 4.7 us
        .tLOW = 4.7,       // 4.7 us
        .tHIGH = 4.0,      // 4.0 us
        .tr_max = 1.0,     // 1000 ns
        .tf_max = 0.3      // 300 ns
    },
    // Fast Mode (400 kHz)
    {
        .tSU_DAT = 0.100,  // 100 ns
        .tHD_DAT = 0.300,  // 300 ns (0 min)
        .tHD_STA = 0.6,    // 0.6 us
        .tSU_STA = 0.6,    // 0.6 us
        .tSU_STO = 0.6,    // 0.6 us
        .tBUF = 1.3,       // 1.3 us
        .tLOW = 1.3,       // 1.3 us
        .tHIGH = 0.6,      // 0.6 us
        .tr_max = 0.3,     // 300 ns
        .tf_max = 0.3      // 300 ns
    },
    // Fast Mode Plus (1 MHz)
    {
        .tSU_DAT = 0.050,  // 50 ns
        .tHD_DAT = 0.000,  // 0 ns min
        .tHD_STA = 0.26,   // 0.26 us
        .tSU_STA = 0.26,   // 0.26 us
        .tSU_STO = 0.26,   // 0.26 us
        .tBUF = 0.5,       // 0.5 us
        .tLOW = 0.5,       // 0.5 us
        .tHIGH = 0.26,     // 0.26 us
        .tr_max = 0.12,    // 120 ns
        .tf_max = 0.12     // 120 ns
    }
};

// Hardware abstraction (implement these for your platform)
extern void gpio_set_scl(bool high);
extern void gpio_set_sda(bool high);
extern bool gpio_read_sda(void);
extern void delay_us(float microseconds);
extern uint64_t get_timestamp_ns(void); // For timing verification

// I2C Master Context
typedef struct {
    i2c_mode_t mode;
    const i2c_timing_t *timing;
    bool started;
} i2c_master_t;

// Initialize I2C master
void i2c_init(i2c_master_t *i2c, i2c_mode_t mode) {
    i2c->mode = mode;
    i2c->timing = &i2c_timings[mode];
    i2c->started = false;
    
    // Initialize both lines high (idle state)
    gpio_set_scl(true);
    gpio_set_sda(true);
}

// Generate START condition
// Timing: SDA falls while SCL is high
void i2c_start(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    
    if (i2c->started) {
        // Repeated START: ensure SDA is high first
        gpio_set_sda(true);
        delay_us(t->tSU_STA);  // Setup time for repeated START
        gpio_set_scl(true);
        delay_us(t->tSU_STA);
    } else {
        // Initial START: ensure bus free time has elapsed
        delay_us(t->tBUF);
        gpio_set_scl(true);
        gpio_set_sda(true);
        delay_us(t->tSU_STA);  // SCL must be high before SDA falls
    }
    
    // Generate START: SDA goes low while SCL is high
    gpio_set_sda(false);
    delay_us(t->tHD_STA);  // Hold time after START
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);  // Additional hold time
    
    i2c->started = true;
}

// Generate STOP condition
// Timing: SDA rises while SCL is high
void i2c_stop(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    
    // Ensure SCL is low and SDA is low
    gpio_set_scl(false);
    gpio_set_sda(false);
    delay_us(t->tLOW);
    
    // Bring SCL high
    gpio_set_scl(true);
    delay_us(t->tSU_STO);  // Setup time for STOP
    
    // Generate STOP: SDA goes high while SCL is high
    gpio_set_sda(true);
    delay_us(t->tBUF);  // Bus free time
    
    i2c->started = false;
}

// Write a single bit with proper timing
void i2c_write_bit(i2c_master_t *i2c, bool bit) {
    const i2c_timing_t *t = i2c->timing;
    
    // Set data line while clock is low
    gpio_set_scl(false);
    gpio_set_sda(bit);
    delay_us(t->tSU_DAT);  // Data setup time before clock high
    
    // Clock high period - data is sampled here
    gpio_set_scl(true);
    delay_us(t->tHIGH);
    
    // Clock low period - data can change after this
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);  // Data hold time
}

// Read a single bit with proper timing
bool i2c_read_bit(i2c_master_t *i2c) {
    const i2c_timing_t *t = i2c->timing;
    bool bit;
    
    // Release SDA (slave will drive it)
    gpio_set_scl(false);
    gpio_set_sda(true);  // Release to high-impedance
    delay_us(t->tSU_DAT);
    
    // Clock high - sample data
    gpio_set_scl(true);
    delay_us(t->tHIGH / 2);  // Sample in middle of high period
    bit = gpio_read_sda();
    delay_us(t->tHIGH / 2);
    
    // Clock low
    gpio_set_scl(false);
    delay_us(t->tHD_DAT);
    
    return bit;
}

// Write a byte and return ACK/NACK
bool i2c_write_byte(i2c_master_t *i2c, uint8_t data) {
    // Write 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit(i2c, (data >> i) & 0x01);
    }
    
    // Read ACK bit (0 = ACK, 1 = NACK)
    return !i2c_read_bit(i2c);
}

// Read a byte and send ACK/NACK
uint8_t i2c_read_byte(i2c_master_t *i2c, bool send_ack) {
    uint8_t data = 0;
    
    // Read 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        if (i2c_read_bit(i2c)) {
            data |= (1 << i);
        }
    }
    
    // Send ACK (0) or NACK (1)
    i2c_write_bit(i2c, !send_ack);
    
    return data;
}

// Example: Write to I2C device with timing verification
bool i2c_write_register(i2c_master_t *i2c, uint8_t dev_addr, 
                        uint8_t reg_addr, uint8_t value) {
    bool success = true;
    
    // Start condition
    i2c_start(i2c);
    
    // Send device address with write bit (0)
    if (!i2c_write_byte(i2c, (dev_addr << 1) | 0)) {
        success = false;
        goto cleanup;
    }
    
    // Send register address
    if (!i2c_write_byte(i2c, reg_addr)) {
        success = false;
        goto cleanup;
    }
    
    // Send data
    if (!i2c_write_byte(i2c, value)) {
        success = false;
        goto cleanup;
    }
    
cleanup:
    // Stop condition
    i2c_stop(i2c);
    return success;
}

// Calculate required pull-up resistor for desired rise time
// Vdd = supply voltage, Cb = bus capacitance (pF), tr = rise time (ns)
float calculate_pullup_resistor(float vdd, float cb_pf, float tr_ns) {
    // tr = 0.8473 * R * Cb (approximation for 30% to 70%)
    // R = tr / (0.8473 * Cb)
    float cb_farads = cb_pf * 1e-12;
    float tr_seconds = tr_ns * 1e-9;
    float r_ohms = tr_seconds / (0.8473 * cb_farads);
    return r_ohms;
}

// Example usage
void example_timing_usage(void) {
    i2c_master_t i2c;
    
    // Initialize for Fast Mode (400 kHz)
    i2c_init(&i2c, I2C_FAST_MODE);
    
    // Write to device at address 0x50
    uint8_t device_addr = 0x50;
    uint8_t register_addr = 0x10;
    uint8_t data = 0xAA;
    
    bool result = i2c_write_register(&i2c, device_addr, register_addr, data);
    
    // Calculate pull-up resistor for 300ns rise time
    // Assuming 3.3V, 100pF bus capacitance
    float r_pullup = calculate_pullup_resistor(3.3, 100.0, 300.0);
    // Result: ~3.5kΩ (use standard 3.3kΩ or 4.7kΩ)
}
```

I2C Timing Parameters - Rust Implementation

```rust
/// I2C Timing Parameters Implementation in Rust
/// Demonstrates type-safe bit-banged I2C with precise timing control

use core::time::Duration;

/// I2C operating modes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cMode {
    Standard,   // 100 kHz
    Fast,       // 400 kHz
    FastPlus,   // 1 MHz
}

/// Comprehensive I2C timing parameters
#[derive(Debug, Clone, Copy)]
pub struct I2cTiming {
    /// Data setup time (ns)
    pub t_su_dat: u32,
    /// Data hold time (ns)
    pub t_hd_dat: u32,
    /// START condition hold time (ns)
    pub t_hd_sta: u32,
    /// START condition setup time (ns)
    pub t_su_sta: u32,
    /// STOP condition setup time (ns)
    pub t_su_sto: u32,
    /// Bus free time between STOP and START (ns)
    pub t_buf: u32,
    /// SCL low period (ns)
    pub t_low: u32,
    /// SCL high period (ns)
    pub t_high: u32,
    /// Maximum rise time (ns)
    pub tr_max: u32,
    /// Maximum fall time (ns)
    pub tf_max: u32,
}

impl I2cTiming {
    /// Get timing parameters for specified mode
    pub const fn for_mode(mode: I2cMode) -> Self {
        match mode {
            I2cMode::Standard => Self::STANDARD,
            I2cMode::Fast => Self::FAST,
            I2cMode::FastPlus => Self::FAST_PLUS,
        }
    }

    /// Standard Mode (100 kHz) timing
    pub const STANDARD: Self = Self {
        t_su_dat: 250,      // 250 ns
        t_hd_dat: 300,      // 300 ns (0 min, but 300 recommended)
        t_hd_sta: 4_000,    // 4.0 μs
        t_su_sta: 4_700,    // 4.7 μs
        t_su_sto: 4_000,    // 4.0 μs
        t_buf: 4_700,       // 4.7 μs
        t_low: 4_700,       // 4.7 μs
        t_high: 4_000,      // 4.0 μs
        tr_max: 1_000,      // 1000 ns
        tf_max: 300,        // 300 ns
    };

    /// Fast Mode (400 kHz) timing
    pub const FAST: Self = Self {
        t_su_dat: 100,      // 100 ns
        t_hd_dat: 300,      // 300 ns (0 min)
        t_hd_sta: 600,      // 0.6 μs
        t_su_sta: 600,      // 0.6 μs
        t_su_sto: 600,      // 0.6 μs
        t_buf: 1_300,       // 1.3 μs
        t_low: 1_300,       // 1.3 μs
        t_high: 600,        // 0.6 μs
        tr_max: 300,        // 300 ns
        tf_max: 300,        // 300 ns
    };

    /// Fast Mode Plus (1 MHz) timing
    pub const FAST_PLUS: Self = Self {
        t_su_dat: 50,       // 50 ns
        t_hd_dat: 0,        // 0 ns min
        t_hd_sta: 260,      // 0.26 μs
        t_su_sta: 260,      // 0.26 μs
        t_su_sto: 260,      // 0.26 μs
        t_buf: 500,         // 0.5 μs
        t_low: 500,         // 0.5 μs
        t_high: 260,        // 0.26 μs
        tr_max: 120,        // 120 ns
        tf_max: 120,        // 120 ns
    };

    /// Calculate the theoretical maximum bus frequency
    pub fn max_frequency_hz(&self) -> u32 {
        // Period = t_low + t_high + tr + tf (approximately)
        let period_ns = self.t_low + self.t_high + self.tr_max + self.tf_max;
        1_000_000_000 / period_ns
    }
}

/// Hardware abstraction layer for GPIO operations
pub trait I2cPins {
    /// Set SCL line state (true = high, false = low)
    fn set_scl(&mut self, high: bool);
    
    /// Set SDA line state (true = high/release, false = low)
    fn set_sda(&mut self, high: bool);
    
    /// Read SDA line state
    fn read_sda(&self) -> bool;
    
    /// Delay for specified duration (nanosecond precision)
    fn delay_ns(&mut self, ns: u32);
}

/// I2C error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    /// No acknowledgment received from slave
    Nack,
    /// Bus arbitration lost
    ArbitrationLost,
    /// Timeout waiting for operation
    Timeout,
    /// Invalid timing parameters
    InvalidTiming,
}

pub type Result<T> = core::result::Result<T, I2cError>;

/// Bit-banged I2C master implementation
pub struct I2cMaster<P: I2cPins> {
    pins: P,
    timing: I2cTiming,
    started: bool,
}

impl<P: I2cPins> I2cMaster<P> {
    /// Create a new I2C master instance
    pub fn new(pins: P, mode: I2cMode) -> Self {
        let mut master = Self {
            pins,
            timing: I2cTiming::for_mode(mode),
            started: false,
        };
        
        // Initialize bus to idle state
        master.pins.set_scl(true);
        master.pins.set_sda(true);
        master
    }

    /// Create with custom timing parameters
    pub fn with_timing(pins: P, timing: I2cTiming) -> Self {
        let mut master = Self {
            pins,
            timing,
            started: false,
        };
        
        master.pins.set_scl(true);
        master.pins.set_sda(true);
        master
    }

    /// Generate START condition
    /// Timing: SDA falls while SCL is high
    pub fn start(&mut self) -> Result<()> {
        if self.started {
            // Repeated START: ensure SDA is high first
            self.pins.set_sda(true);
            self.pins.delay_ns(self.timing.t_su_sta);
            self.pins.set_scl(true);
            self.pins.delay_ns(self.timing.t_su_sta);
        } else {
            // Initial START: ensure bus free time
            self.pins.delay_ns(self.timing.t_buf);
            self.pins.set_scl(true);
            self.pins.set_sda(true);
            self.pins.delay_ns(self.timing.t_su_sta);
        }
        
        // Generate START: SDA goes low while SCL is high
        self.pins.set_sda(false);
        self.pins.delay_ns(self.timing.t_hd_sta);
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        self.started = true;
        Ok(())
    }

    /// Generate STOP condition
    /// Timing: SDA rises while SCL is high
    pub fn stop(&mut self) -> Result<()> {
        // Ensure SCL is low and SDA is low
        self.pins.set_scl(false);
        self.pins.set_sda(false);
        self.pins.delay_ns(self.timing.t_low);
        
        // Bring SCL high
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_su_sto);
        
        // Generate STOP: SDA goes high while SCL is high
        self.pins.set_sda(true);
        self.pins.delay_ns(self.timing.t_buf);
        
        self.started = false;
        Ok(())
    }

    /// Write a single bit with proper timing
    fn write_bit(&mut self, bit: bool) -> Result<()> {
        // Set data while clock is low
        self.pins.set_scl(false);
        self.pins.set_sda(bit);
        self.pins.delay_ns(self.timing.t_su_dat);
        
        // Clock high period - data is sampled
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_high);
        
        // Clock low period
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        Ok(())
    }

    /// Read a single bit with proper timing
    fn read_bit(&mut self) -> Result<bool> {
        // Release SDA (slave will drive it)
        self.pins.set_scl(false);
        self.pins.set_sda(true);
        self.pins.delay_ns(self.timing.t_su_dat);
        
        // Clock high - sample data
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_high / 2);
        let bit = self.pins.read_sda();
        self.pins.delay_ns(self.timing.t_high / 2);
        
        // Clock low
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        Ok(bit)
    }

    /// Write a byte and return ACK status
    pub fn write_byte(&mut self, data: u8) -> Result<bool> {
        // Write 8 bits, MSB first
        for i in (0..8).rev() {
            self.write_bit((data >> i) & 0x01 != 0)?;
        }
        
        // Read ACK bit (false = ACK, true = NACK)
        let ack = !self.read_bit()?;
        Ok(ack)
    }

    /// Read a byte and send ACK/NACK
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8> {
        let mut data = 0u8;
        
        // Read 8 bits, MSB first
        for i in (0..8).rev() {
            if self.read_bit()? {
                data |= 1 << i;
            }
        }
        
        // Send ACK (false) or NACK (true)
        self.write_bit(!send_ack)?;
        
        Ok(data)
    }

    /// Write data to a register
    pub fn write_register(&mut self, dev_addr: u8, reg_addr: u8, 
                          value: u8) -> Result<()> {
        self.start()?;
        
        // Send device address with write bit
        if !self.write_byte((dev_addr << 1) | 0)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Send register address
        if !self.write_byte(reg_addr)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Send data
        if !self.write_byte(value)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        self.stop()?;
        Ok(())
    }

    /// Read data from a register
    pub fn read_register(&mut self, dev_addr: u8, reg_addr: u8) -> Result<u8> {
        // Write phase: send register address
        self.start()?;
        if !self.write_byte((dev_addr << 1) | 0)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        if !self.write_byte(reg_addr)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Read phase: read data
        self.start()?; // Repeated START
        if !self.write_byte((dev_addr << 1) | 1)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        let data = self.read_byte(false)?; // Send NACK (last byte)
        self.stop()?;
        
        Ok(data)
    }
}

/// Calculate required pull-up resistor value
/// Returns resistor value in ohms
pub fn calculate_pullup_resistor(vdd: f32, bus_capacitance_pf: f32, 
                                  rise_time_ns: f32) -> f32 {
    // Rise time equation: tr ≈ 0.8473 × R × Cb
    // Where tr is from 30% to 70% of Vdd
    // R = tr / (0.8473 × Cb)
    let cb_farads = bus_capacitance_pf * 1e-12;
    let tr_seconds = rise_time_ns * 1e-9;
    tr_seconds / (0.8473 * cb_farads)
}

/// Verify timing constraints for a given configuration
pub fn verify_timing(timing: &I2cTiming, measured_tr_ns: u32, 
                     measured_tf_ns: u32) -> Result<()> {
    if measured_tr_ns > timing.tr_max {
        return Err(I2cError::InvalidTiming);
    }
    if measured_tf_ns > timing.tf_max {
        return Err(I2cError::InvalidTiming);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_timing_calculations() {
        // Test pull-up resistor calculation
        // For 3.3V, 100pF bus, 300ns rise time
        let r = calculate_pullup_resistor(3.3, 100.0, 300.0);
        assert!((r - 3540.0).abs() < 100.0); // ~3.5kΩ
        
        // Fast mode max frequency
        let timing = I2cTiming::FAST;
        let max_freq = timing.max_frequency_hz();
        assert!(max_freq >= 400_000 && max_freq <= 500_000);
    }

    #[test]
    fn test_timing_verification() {
        let timing = I2cTiming::FAST;
        
        // Valid timing
        assert!(verify_timing(&timing, 250, 200).is_ok());
        
        // Invalid rise time
        assert!(verify_timing(&timing, 400, 200).is_err());
        
        // Invalid fall time
        assert!(verify_timing(&timing, 250, 400).is_err());
    }
}

// Example usage
fn example_usage() {
    // Define a mock pins implementation
    struct MockPins;
    
    impl I2cPins for MockPins {
        fn set_scl(&mut self, _high: bool) { /* GPIO operations */ }
        fn set_sda(&mut self, _high: bool) { /* GPIO operations */ }
        fn read_sda(&self) -> bool { true }
        fn delay_ns(&mut self, _ns: u32) { /* Precise delay */ }
    }
    
    let pins = MockPins;
    let mut i2c = I2cMaster::new(pins, I2cMode::Fast);
    
    // Write to device
    let _ = i2c.write_register(0x50, 0x10, 0xAA);
    
    // Read from device
    let _ = i2c.read_register(0x50, 0x10);
}
```

I2C Timing Analysis & Debugging Tools - C++

```cpp
/**
 * I2C Timing Analysis and Debugging Tools - C++
 * Advanced timing verification, measurement, and troubleshooting
 */

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

namespace i2c {

// Timing measurement result
struct TimingMeasurement {
    uint64_t timestamp_ns;
    bool scl;
    bool sda;
    std::string event;
};

// Timing analysis result
struct TimingAnalysis {
    double t_su_dat_measured;
    double t_hd_dat_measured;
    double t_low_measured;
    double t_high_measured;
    double rise_time_measured;
    double fall_time_measured;
    bool meets_spec;
    std::vector<std::string> violations;
};

// Bus capacitance calculator
class BusCapacitanceCalculator {
public:
    // Calculate total bus capacitance from components
    static double calculate_total_capacitance(
        double wire_cap_pf_per_cm,
        double wire_length_cm,
        const std::vector<double>& device_caps_pf
    ) {
        double total = wire_cap_pf_per_cm * wire_length_cm;
        for (double cap : device_caps_pf) {
            total += cap;
        }
        return total;
    }
    
    // Calculate capacitance from measured rise time
    static double capacitance_from_rise_time(
        double rise_time_ns,
        double pullup_ohms
    ) {
        // tr ≈ 0.8473 × R × C
        // C = tr / (0.8473 × R)
        double tr_seconds = rise_time_ns * 1e-9;
        return tr_seconds / (0.8473 * pullup_ohms) * 1e12; // Return in pF
    }
};

// Pull-up resistor calculator
class PullupCalculator {
public:
    struct ResistorResult {
        double calculated_value;
        double nearest_standard;
        double actual_rise_time_ns;
        bool meets_spec;
    };
    
    // Standard E12 resistor values
    static const std::vector<double> STANDARD_RESISTORS;
    
    // Calculate optimal pull-up resistor
    static ResistorResult calculate(
        double vdd_volts,
        double bus_cap_pf,
        double target_rise_time_ns,
        double max_rise_time_ns,
        double min_current_ma = 3.0  // Minimum sink current
    ) {
        ResistorResult result;
        
        // Calculate from rise time constraint
        double cb_farads = bus_cap_pf * 1e-12;
        double tr_seconds = target_rise_time_ns * 1e-9;
        result.calculated_value = tr_seconds / (0.8473 * cb_farads);
        
        // Check minimum current constraint (VOL = 0.4V typical)
        double r_max_current = (vdd_volts - 0.4) / (min_current_ma * 1e-3);
        if (result.calculated_value > r_max_current) {
            result.calculated_value = r_max_current;
        }
        
        // Find nearest standard value
        result.nearest_standard = find_nearest_standard(result.calculated_value);
        
        // Verify actual rise time with standard value
        result.actual_rise_time_ns = 0.8473 * result.nearest_standard * 
                                     cb_farads * 1e9;
        result.meets_spec = result.actual_rise_time_ns <= max_rise_time_ns;
        
        return result;
    }
    
private:
    static double find_nearest_standard(double target) {
        double best = STANDARD_RESISTORS[0];
        double min_diff = std::abs(target - best);
        
        for (double value : STANDARD_RESISTORS) {
            double diff = std::abs(target - value);
            if (diff < min_diff) {
                min_diff = diff;
                best = value;
            }
        }
        return best;
    }
};

// E12 standard values (1kΩ to 10kΩ range, most common for I2C)
const std::vector<double> PullupCalculator::STANDARD_RESISTORS = {
    1000, 1200, 1500, 1800, 2200, 2700, 3300, 3900, 4700, 5600, 6800, 8200, 10000
};

// Timing analyzer for captured waveforms
class TimingAnalyzer {
public:
    TimingAnalyzer(double spec_t_su_dat, double spec_t_hd_dat,
                   double spec_t_low, double spec_t_high,
                   double spec_tr_max, double spec_tf_max)
        : spec_t_su_dat_(spec_t_su_dat)
        , spec_t_hd_dat_(spec_t_hd_dat)
        , spec_t_low_(spec_t_low)
        , spec_t_high_(spec_t_high)
        , spec_tr_max_(spec_tr_max)
        , spec_tf_max_(spec_tf_max) {}
    
    // Analyze captured timing data
    TimingAnalysis analyze(const std::vector<TimingMeasurement>& data) {
        TimingAnalysis result = {};
        result.meets_spec = true;
        
        // Find setup and hold times
        analyze_setup_hold(data, result);
        
        // Find clock periods
        analyze_clock_periods(data, result);
        
        // Find rise and fall times
        analyze_transitions(data, result);
        
        // Check against specifications
        check_specifications(result);
        
        return result;
    }
    
private:
    void analyze_setup_hold(const std::vector<TimingMeasurement>& data,
                           TimingAnalysis& result) {
        for (size_t i = 1; i < data.size(); i++) {
            const auto& curr = data[i];
            const auto& prev = data[i-1];
            
            // Find SDA stable before SCL rising (setup time)
            if (curr.scl && !prev.scl && curr.sda == prev.sda) {
                // Look back to find last SDA change
                for (int j = i - 1; j >= 0; j--) {
                    if (data[j].sda != curr.sda) {
                        double setup = (prev.timestamp_ns - data[j].timestamp_ns);
                        result.t_su_dat_measured = std::max(
                            result.t_su_dat_measured, setup
                        );
                        break;
                    }
                }
            }
            
            // Find SDA changes after SCL falling (hold time)
            if (!curr.scl && prev.scl) {
                uint64_t scl_fall_time = curr.timestamp_ns;
                // Look forward for SDA change
                for (size_t j = i + 1; j < data.size(); j++) {
                    if (data[j].sda != curr.sda) {
                        double hold = (data[j].timestamp_ns - scl_fall_time);
                        result.t_hd_dat_measured = std::max(
                            result.t_hd_dat_measured, hold
                        );
                        break;
                    }
                }
            }
        }
    }
    
    void analyze_clock_periods(const std::vector<TimingMeasurement>& data,
                              TimingAnalysis& result) {
        uint64_t last_rising = 0;
        uint64_t last_falling = 0;
        
        for (const auto& sample : data) {
            if (sample.scl && last_falling > 0) {
                double t_low = (sample.timestamp_ns - last_falling);
                result.t_low_measured = std::max(result.t_low_measured, t_low);
            }
            
            if (!sample.scl && last_rising > 0) {
                double t_high = (sample.timestamp_ns - last_rising);
                result.t_high_measured = std::max(result.t_high_measured, t_high);
            }
            
            if (sample.scl) last_rising = sample.timestamp_ns;
            else last_falling = sample.timestamp_ns;
        }
    }
    
    void analyze_transitions(const std::vector<TimingMeasurement>& data,
                           TimingAnalysis& result) {
        // Simplified: find maximum transition time
        // In practice, you'd measure from 30% to 70% of Vdd
        for (size_t i = 1; i < data.size(); i++) {
            const auto& curr = data[i];
            const auto& prev = data[i-1];
            
            double transition_time = curr.timestamp_ns - prev.timestamp_ns;
            
            // Rising edge
            if (curr.scl && !prev.scl) {
                result.rise_time_measured = std::max(
                    result.rise_time_measured, transition_time
                );
            }
            
            // Falling edge
            if (!curr.scl && prev.scl) {
                result.fall_time_measured = std::max(
                    result.fall_time_measured, transition_time
                );
            }
        }
    }
    
    void check_specifications(TimingAnalysis& result) {
        if (result.t_su_dat_measured < spec_t_su_dat_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Setup time violation: " + 
                std::to_string(result.t_su_dat_measured) + 
                " ns < " + std::to_string(spec_t_su_dat_) + " ns"
            );
        }
        
        if (result.t_hd_dat_measured < spec_t_hd_dat_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Hold time violation: " + 
                std::to_string(result.t_hd_dat_measured) + 
                " ns < " + std::to_string(spec_t_hd_dat_) + " ns"
            );
        }
        
        if (result.rise_time_measured > spec_tr_max_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Rise time too slow: " + 
                std::to_string(result.rise_time_measured) + 
                " ns > " + std::to_string(spec_tr_max_) + " ns"
            );
        }
        
        if (result.fall_time_measured > spec_tf_max_) {
            result.meets_spec = false;
            result.violations.push_back(
                "Fall time too slow: " + 
                std::to_string(result.fall_time_measured) + 
                " ns > " + std::to_string(spec_tf_max_) + " ns"
            );
        }
    }
    
    double spec_t_su_dat_;
    double spec_t_hd_dat_;
    double spec_t_low_;
    double spec_t_high_;
    double spec_tr_max_;
    double spec_tf_max_;
};

// Troubleshooting helper
class I2cTroubleshooter {
public:
    static void diagnose_timing_issue(const TimingAnalysis& analysis) {
        std::cout << "=== I2C Timing Diagnosis ===" << std::endl;
        std::cout << std::fixed << std::setprecision(1);
        
        std::cout << "\nMeasured Timings:" << std::endl;
        std::cout << "  Setup time:  " << analysis.t_su_dat_measured << " ns" << std::endl;
        std::cout << "  Hold time:   " << analysis.t_hd_dat_measured << " ns" << std::endl;
        std::cout << "  SCL low:     " << analysis.t_low_measured << " ns" << std::endl;
        std::cout << "  SCL high:    " << analysis.t_high_measured << " ns" << std::endl;
        std::cout << "  Rise time:   " << analysis.rise_time_measured << " ns" << std::endl;
        std::cout << "  Fall time:   " << analysis.fall_time_measured << " ns" << std::endl;
        
        if (!analysis.meets_spec) {
            std::cout << "\n⚠️  TIMING VIOLATIONS DETECTED:" << std::endl;
            for (const auto& violation : analysis.violations) {
                std::cout << "  • " << violation << std::endl;
            }
            
            std::cout << "\nRecommendations:" << std::endl;
            suggest_fixes(analysis);
        } else {
            std::cout << "\n✓ All timing parameters within specification" << std::endl;
        }
    }
    
private:
    static void suggest_fixes(const TimingAnalysis& analysis) {
        for (const auto& violation : analysis.violations) {
            if (violation.find("Rise time") != std::string::npos) {
                std::cout << "  → Reduce pull-up resistor value" << std::endl;
                std::cout << "  → Reduce bus capacitance (shorter wires, fewer devices)" << std::endl;
                std::cout << "  → Use stronger pull-ups or active pull-up circuit" << std::endl;
            }
            if (violation.find("Fall time") != std::string::npos) {
                std::cout << "  → Check device output drivers" << std::endl;
                std::cout << "  → Reduce bus capacitance" << std::endl;
            }
            if (violation.find("Setup time") != std::string::npos) {
                std::cout << "  → Reduce clock frequency" << std::endl;
                std::cout << "  → Check for signal integrity issues" << std::endl;
                std::cout << "  → Verify device timing specifications" << std::endl;
            }
            if (violation.find("Hold time") != std::string::npos) {
                std::cout << "  → Add delay before data transitions" << std::endl;
                std::cout << "  → Check for clock/data skew" << std::endl;
            }
        }
    }
};

} // namespace i2c

// Example usage and test
int main() {
    using namespace i2c;
    
    std::cout << "=== I2C Timing Analysis Example ===" << std::endl;
    
    // Example 1: Calculate pull-up resistor for Fast Mode
    std::cout << "\n1. Pull-up Resistor Calculation (Fast Mode, 400 kHz):" << std::endl;
    double vdd = 3.3;
    double bus_cap = 120.0; // pF
    double target_rise = 250.0; // ns
    double max_rise = 300.0; // ns (Fast mode spec)
    
    auto resistor = PullupCalculator::calculate(
        vdd, bus_cap, target_rise, max_rise
    );
    
    std::cout << "  Bus capacitance: " << bus_cap << " pF" << std::endl;
    std::cout << "  Target rise time: " << target_rise << " ns" << std::endl;
    std::cout << "  Calculated R: " << std::fixed << std::setprecision(0) 
              << resistor.calculated_value << " Ω" << std::endl;
    std::cout << "  Nearest standard: " << resistor.nearest_standard << " Ω" << std::endl;
    std::cout << "  Actual rise time: " << std::setprecision(1) 
              << resistor.actual_rise_time_ns << " ns" << std::endl;
    std::cout << "  Meets spec: " << (resistor.meets_spec ? "✓ Yes" : "✗ No") << std::endl;
    
    // Example 2: Calculate bus capacitance
    std::cout << "\n2. Bus Capacitance Calculation:" << std::endl;
    double wire_cap = 1.2; // pF/cm (typical for PCB trace)
    double wire_length = 10.0; // cm
    std::vector<double> device_caps = {10.0, 15.0, 8.0}; // pF for 3 devices
    
    double total_cap = BusCapacitanceCalculator::calculate_total_capacitance(
        wire_cap, wire_length, device_caps
    );
    
    std::cout << "  Wire: " << wire_length << " cm × " << wire_cap 
              << " pF/cm = " << (wire_cap * wire_length) << " pF" << std::endl;
    std::cout << "  Devices: ";
    for (size_t i = 0; i < device_caps.size(); i++) {
        std::cout << device_caps[i] << " pF";
        if (i < device_caps.size() - 1) std::cout << " + ";
    }
    std::cout << std::endl;
    std::cout << "  Total capacitance: " << total_cap << " pF" << std::endl;
    
    // Example 3: Reverse calculate capacitance from measured rise time
    std::cout << "\n3. Capacitance from Measured Rise Time:" << std::endl;
    double measured_rise = 280.0; // ns
    double pullup = 3300.0; // Ω
    double calc_cap = BusCapacitanceCalculator::capacitance_from_rise_time(
        measured_rise, pullup
    );
    std::cout << "  Measured rise time: " << measured_rise << " ns" << std::endl;
    std::cout << "  Pull-up resistor: " << pullup << " Ω" << std::endl;
    std::cout << "  Calculated capacitance: " << std::setprecision(1) 
              << calc_cap << " pF" << std::endl;
    
    return 0;
}
```

The code examples demonstrate several critical aspects of I2C timing:

### **Timing Relationships**
1. **Setup time (tSU;DAT)** ensures data is stable *before* the clock edge where it's sampled
2. **Hold time (tHD;DAT)** ensures data remains stable *after* the clock edge
3. These create a "valid data window" that prevents metastability and ensures reliable communication

### **Rise/Fall Time Impact**
- Rise time is typically the limiting factor due to passive pull-ups
- The RC time constant (R × C) determines rise time
- Smaller pull-up resistors = faster rise time, but higher power consumption
- Bus capacitance increases with: wire length, number of devices, PCB parasitics

### **Mode-Specific Constraints**
- **Standard mode (100 kHz)**: Most relaxed timing, suitable for long cables
- **Fast mode (400 kHz)**: Requires careful resistor selection and shorter traces
- **Fast mode Plus (1 MHz)**: Demands low capacitance, strong pull-ups, often needs active termination

### **Practical Design Considerations**

**Pull-up resistor selection** involves balancing:
- Minimum value: Limited by device current sink capability (typically 3mA)
- Maximum value: Limited by required rise time
- Formula: R = tr / (0.8473 × Cb)

**Common pitfalls**:
- Rise time violations from weak pull-ups or high capacitance
- Setup time violations from running too fast for the bus conditions
- Hold time violations from improper software timing or signal integrity issues

The code examples provide tools to calculate optimal values, verify timing constraints, and troubleshoot issues when they arise. Proper timing is essential for reliable I2C communication, especially in production environments with varying conditions.