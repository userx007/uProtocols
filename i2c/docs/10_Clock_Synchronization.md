# I2C Clock Synchronization

## Overview

Clock synchronization is a critical aspect of I2C multi-master systems. When multiple masters attempt to communicate on the same bus, their clock signals must be properly coordinated to prevent bus conflicts and data corruption. The I2C specification defines a clock synchronization mechanism that allows multiple masters to coexist peacefully.

## How Clock Synchronization Works

### The Wired-AND Connection

I2C uses open-drain (or open-collector) outputs for both SDA and SCL lines. This creates a **wired-AND** configuration where:

- Any device can pull the line LOW
- The line only goes HIGH when ALL devices release it
- A LOW level always dominates over HIGH

This physical property is fundamental to clock synchronization.

### Clock Stretching vs. Clock Synchronization

While related, these are distinct concepts:

- **Clock Stretching**: A slave holds SCL LOW to slow down the master
- **Clock Synchronization**: Multiple masters coordinate their SCL signals to prevent conflicts

### The Synchronization Algorithm

When two or more masters generate clock pulses simultaneously:

1. **LOW Period Synchronization**: The SCL line remains LOW until ALL masters have completed their LOW period. The master with the longest LOW period determines the actual LOW duration.

2. **HIGH Period Synchronization**: When masters release SCL, the line goes HIGH. Masters then count their HIGH period. If another master pulls SCL LOW before a master finishes its HIGH period, that master's counter is reset.

3. **Wait State Injection**: Masters that finish their HIGH period early effectively insert wait states, slowing down to match the slowest clock.

The result: **All masters automatically synchronize to the slowest clock** without explicit negotiation.

## Arbitration and Clock Sync Working Together

Clock synchronization works hand-in-hand with arbitration:

- Masters start transmission simultaneously
- Clock synchronization ensures they use the same timing
- Arbitration determines which master wins based on data (SDA)
- The losing master detects the conflict and backs off

## Code Examples

### C/C++ Implementation

Here's a low-level example showing clock synchronization concepts:

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (platform-specific)
typedef struct {
    volatile uint32_t SCL_OUT;    // SCL output register
    volatile uint32_t SCL_IN;     // SCL input register
    volatile uint32_t SDA_OUT;
    volatile uint32_t SDA_IN;
    volatile uint32_t TIMING;
} I2C_Regs;

#define I2C_BASE ((I2C_Regs*)0x40000000)

// Timing parameters
typedef struct {
    uint32_t scl_low_period;   // Desired LOW period in clock cycles
    uint32_t scl_high_period;  // Desired HIGH period in clock cycles
} I2C_Timing;

// Read current SCL state (accounts for other masters)
static inline bool read_scl(void) {
    return (I2C_BASE->SCL_IN & 0x1) != 0;
}

// Release SCL (let it float high via pull-up)
static inline void release_scl(void) {
    I2C_BASE->SCL_OUT = 1;  // Open-drain release
}

// Pull SCL low
static inline void pull_scl_low(void) {
    I2C_BASE->SCL_OUT = 0;
}

// Similar functions for SDA
static inline bool read_sda(void) {
    return (I2C_BASE->SDA_IN & 0x1) != 0;
}

static inline void release_sda(void) {
    I2C_BASE->SDA_OUT = 1;
}

static inline void pull_sda_low(void) {
    I2C_BASE->SDA_OUT = 0;
}

// Delay function (simplified - would use timer in real implementation)
static void delay_cycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__("nop");
    }
}

/**
 * Generate one synchronized clock pulse
 * This implements the clock synchronization algorithm
 */
bool i2c_clock_pulse_synchronized(I2C_Timing* timing, bool data_bit) {
    // Set SDA to the desired bit value
    if (data_bit) {
        release_sda();
    } else {
        pull_sda_low();
    }
    
    // 1. Pull SCL LOW and wait for our LOW period
    pull_scl_low();
    delay_cycles(timing->scl_low_period);
    
    // 2. Release SCL and let it rise (clock synchronization begins here)
    release_scl();
    
    // 3. Wait for SCL to actually go HIGH (clock stretching/synchronization)
    uint32_t timeout = 100000;
    while (!read_scl() && timeout--) {
        // SCL is still LOW - either:
        // a) Another master is in its LOW period (synchronization)
        // b) A slave is stretching the clock
        // We must wait
    }
    
    if (timeout == 0) {
        return false;  // Timeout - bus locked up
    }
    
    // 4. Count our HIGH period
    // If another master pulls SCL low during this time,
    // our HIGH period effectively ends early (synchronization)
    uint32_t high_count = 0;
    while (read_scl() && high_count < timing->scl_high_period) {
        delay_cycles(1);
        high_count++;
        
        // Check if another master pulled SCL low
        if (!read_scl()) {
            break;  // Our HIGH period was cut short - we synchronized
        }
    }
    
    // Read SDA during HIGH period (for arbitration)
    bool sda_value = read_sda();
    
    // If we wrote 1 but read 0, we lost arbitration
    if (data_bit && !sda_value) {
        return false;  // Arbitration lost
    }
    
    return true;  // Arbitration won (or no conflict)
}

/**
 * Multi-master aware byte transmission
 */
typedef enum {
    I2C_RESULT_OK,
    I2C_RESULT_ARB_LOST,
    I2C_RESULT_TIMEOUT
} I2C_Result;

I2C_Result i2c_send_byte_multimaster(uint8_t byte, I2C_Timing* timing) {
    for (int bit = 7; bit >= 0; bit--) {
        bool bit_value = (byte >> bit) & 0x1;
        
        if (!i2c_clock_pulse_synchronized(timing, bit_value)) {
            // Either arbitration lost or timeout
            if (read_sda() != bit_value) {
                return I2C_RESULT_ARB_LOST;
            }
            return I2C_RESULT_TIMEOUT;
        }
    }
    
    return I2C_RESULT_OK;
}

/**
 * Example: Multi-master transmission with clock sync
 */
void example_multimaster_transmission(void) {
    I2C_Timing timing = {
        .scl_low_period = 5000,   // 5000 cycles for LOW
        .scl_high_period = 4000   // 4000 cycles for HIGH
    };
    
    // Generate START condition
    release_scl();
    release_sda();
    delay_cycles(timing.scl_high_period);
    pull_sda_low();  // SDA falls while SCL is HIGH
    delay_cycles(timing.scl_low_period);
    
    // Send address byte (0x50 with write bit)
    uint8_t addr = 0xA0;  // 0x50 << 1
    I2C_Result result = i2c_send_byte_multimaster(addr, &timing);
    
    if (result == I2C_RESULT_ARB_LOST) {
        // Another master won arbitration
        // Back off and retry later
        release_sda();
        release_scl();
        return;
    }
    
    if (result == I2C_RESULT_TIMEOUT) {
        // Bus error - handle recovery
        return;
    }
    
    // Continue with transmission...
}
```

### Rust Implementation

Here's a more idiomatic Rust implementation with proper abstractions:

```rust
use core::sync::atomic::{AtomicBool, Ordering};
use embedded_hal::blocking::delay::DelayUs;

/// I2C timing configuration
#[derive(Clone, Copy)]
pub struct I2cTiming {
    pub scl_low_us: u32,
    pub scl_high_us: u32,
}

impl I2cTiming {
    pub const fn standard_mode() -> Self {
        Self {
            scl_low_us: 5,    // ~4.7μs LOW period (100kHz)
            scl_high_us: 4,   // ~4.0μs HIGH period
        }
    }
    
    pub const fn fast_mode() -> Self {
        Self {
            scl_low_us: 2,    // ~1.3μs LOW period (400kHz)
            scl_high_us: 1,   // ~0.6μs HIGH period
        }
    }
}

/// Result of I2C operations
#[derive(Debug, PartialEq)]
pub enum I2cError {
    ArbitrationLost,
    Timeout,
    BusError,
}

/// Trait for low-level I2C pin control
pub trait I2cPins {
    fn set_scl_low(&mut self);
    fn set_scl_high(&mut self);
    fn read_scl(&self) -> bool;
    
    fn set_sda_low(&mut self);
    fn set_sda_high(&mut self);
    fn read_sda(&self) -> bool;
}

/// Multi-master I2C driver with clock synchronization
pub struct MultiMasterI2c<P, D> 
where
    P: I2cPins,
    D: DelayUs<u32>,
{
    pins: P,
    delay: D,
    timing: I2cTiming,
    arbitration_lost: AtomicBool,
}

impl<P, D> MultiMasterI2c<P, D>
where
    P: I2cPins,
    D: DelayUs<u32>,
{
    pub fn new(pins: P, delay: D, timing: I2cTiming) -> Self {
        Self {
            pins,
            delay,
            timing,
            arbitration_lost: AtomicBool::new(false),
        }
    }
    
    /// Generate one clock pulse with synchronization
    /// Returns Ok(sda_value) if successful, Err if arbitration lost or timeout
    fn clock_pulse_sync(&mut self, data_bit: bool) -> Result<bool, I2cError> {
        // Set SDA to desired value
        if data_bit {
            self.pins.set_sda_high();
        } else {
            self.pins.set_sda_low();
        }
        
        // Phase 1: Pull SCL LOW and wait our LOW period
        self.pins.set_scl_low();
        self.delay.delay_us(self.timing.scl_low_us);
        
        // Phase 2: Release SCL (let it float high)
        self.pins.set_scl_high();
        
        // Phase 3: Wait for SCL to actually go HIGH
        // This implements clock synchronization - we wait for ALL masters
        // and slaves to release SCL
        let timeout_us = 1000; // 1ms timeout
        let mut elapsed = 0;
        
        while !self.pins.read_scl() {
            if elapsed >= timeout_us {
                return Err(I2cError::Timeout);
            }
            self.delay.delay_us(1);
            elapsed += 1;
        }
        
        // Phase 4: Count our HIGH period
        // If another master pulls SCL low, our HIGH period ends early
        let mut high_elapsed = 0;
        
        while self.pins.read_scl() && high_elapsed < self.timing.scl_high_us {
            self.delay.delay_us(1);
            high_elapsed += 1;
        }
        
        // Sample SDA while SCL is HIGH
        let sda_value = self.pins.read_sda();
        
        // Check for arbitration loss
        if data_bit && !sda_value {
            self.arbitration_lost.store(true, Ordering::Relaxed);
            return Err(I2cError::ArbitrationLost);
        }
        
        Ok(sda_value)
    }
    
    /// Send a byte with clock synchronization and arbitration
    pub fn send_byte(&mut self, byte: u8) -> Result<(), I2cError> {
        for bit_pos in (0..8).rev() {
            let bit = (byte >> bit_pos) & 1 == 1;
            self.clock_pulse_sync(bit)?;
        }
        Ok(())
    }
    
    /// Receive a byte with clock synchronization
    pub fn receive_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut byte = 0u8;
        
        // Release SDA so slave can control it
        self.pins.set_sda_high();
        
        for bit_pos in (0..8).rev() {
            let sda = self.clock_pulse_sync(true)?; // Read mode
            if sda {
                byte |= 1 << bit_pos;
            }
        }
        
        // Send ACK/NACK
        self.clock_pulse_sync(!send_ack)?;
        
        Ok(byte)
    }
    
    /// Generate START condition
    pub fn start(&mut self) -> Result<(), I2cError> {
        // Ensure bus is idle
        self.pins.set_sda_high();
        self.pins.set_scl_high();
        self.delay.delay_us(self.timing.scl_high_us);
        
        // Check if bus is available
        if !self.pins.read_sda() || !self.pins.read_scl() {
            return Err(I2cError::BusError);
        }
        
        // START: SDA falls while SCL is HIGH
        self.pins.set_sda_low();
        self.delay.delay_us(self.timing.scl_high_us);
        self.pins.set_scl_low();
        self.delay.delay_us(self.timing.scl_low_us);
        
        self.arbitration_lost.store(false, Ordering::Relaxed);
        Ok(())
    }
    
    /// Generate STOP condition
    pub fn stop(&mut self) {
        // STOP: SDA rises while SCL is HIGH
        self.pins.set_sda_low();
        self.delay.delay_us(self.timing.scl_low_us);
        self.pins.set_scl_high();
        self.delay.delay_us(self.timing.scl_high_us);
        self.pins.set_sda_high();
        self.delay.delay_us(self.timing.scl_high_us);
    }
    
    /// Check if arbitration was lost
    pub fn arbitration_lost(&self) -> bool {
        self.arbitration_lost.load(Ordering::Relaxed)
    }
}

/// Example: Multi-master write with retry on arbitration loss
pub fn multi_master_write_with_retry<P, D>(
    i2c: &mut MultiMasterI2c<P, D>,
    address: u8,
    data: &[u8],
    max_retries: u32,
) -> Result<(), I2cError>
where
    P: I2cPins,
    D: DelayUs<u32>,
{
    for attempt in 0..max_retries {
        // Generate START
        i2c.start()?;
        
        // Send address with write bit
        let addr_byte = (address << 1) | 0; // Write = 0
        
        match i2c.send_byte(addr_byte) {
            Err(I2cError::ArbitrationLost) => {
                // Lost arbitration, back off exponentially
                let backoff_us = 100 * (1 << attempt.min(4));
                // Would need delay here - simplified
                continue;
            }
            Err(e) => {
                i2c.stop();
                return Err(e);
            }
            Ok(_) => {}
        }
        
        // Send data bytes
        for &byte in data {
            if let Err(e) = i2c.send_byte(byte) {
                i2c.stop();
                if e == I2cError::ArbitrationLost {
                    break; // Retry
                }
                return Err(e);
            }
        }
        
        // Check if we completed successfully
        if !i2c.arbitration_lost() {
            i2c.stop();
            return Ok(());
        }
    }
    
    Err(I2cError::ArbitrationLost)
}

// Example usage
#[cfg(feature = "example")]
fn example() {
    // Platform-specific pin and delay implementations would go here
    // let pins = MyI2cPins::new();
    // let delay = MyDelay::new();
    
    // let mut i2c = MultiMasterI2c::new(
    //     pins,
    //     delay,
    //     I2cTiming::standard_mode()
    // );
    
    // let data = [0x12, 0x34, 0x56];
    // match multi_master_write_with_retry(&mut i2c, 0x50, &data, 5) {
    //     Ok(_) => println!("Write successful"),
    //     Err(I2cError::ArbitrationLost) => println!("Failed after retries"),
    //     Err(e) => println!("Error: {:?}", e),
    // }
}
```

## Key Takeaways

1. **Clock synchronization is automatic** - it emerges from the wired-AND bus topology and the I2C protocol timing rules

2. **The slowest clock wins** - all masters automatically slow down to match the longest LOW or HIGH period

3. **Works with arbitration** - clock sync ensures masters stay in phase while arbitration determines the winner

4. **Implementation requires careful timing** - you must properly wait for SCL to rise and monitor it during the HIGH period

5. **Timeout protection is essential** - always include timeouts to handle stuck bus conditions

6. **Multi-master systems need retry logic** - when arbitration is lost, back off and retry with exponential backoff

This synchronization mechanism is elegant and robust, allowing I2C to support multiple masters without complex negotiation protocols.