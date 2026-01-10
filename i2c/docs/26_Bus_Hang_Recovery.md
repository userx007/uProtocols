# I2C Bus Hang Recovery

## Overview

I2C bus hangs occur when the SDA (data) or SCL (clock) lines become stuck in a LOW state, preventing further communication. This typically happens when a slave device is holding the line low due to:

- **Power glitches** during transmission
- **Master reset** while a slave is mid-transaction
- **Clock stretching gone wrong**
- **Software bugs** in slave devices
- **Electrical noise** causing state corruption

When the bus is hung, normal I2C transactions fail because the bus appears perpetually busy. Recovery requires specific techniques to reset the slave devices and restore the bus to its idle state (both lines HIGH).

## Understanding Bus Hang Scenarios

### Stuck SDA Line

The most common scenario occurs when a slave device is in the middle of transmitting an ACK or data bit and loses synchronization with the master. The slave continues to pull SDA low, waiting for clock pulses it will never receive.

### Stuck SCL Line

Less common but more problematic. This happens when a slave performs clock stretching (holding SCL low to slow down the master) and then crashes or loses power, leaving SCL stuck low.

## Recovery Techniques

### 1. Clock Pulse Recovery (Most Common)

Send up to 9 clock pulses on SCL while monitoring SDA. Each clock pulse allows a slave holding SDA low to advance one bit in its state machine. After 9 pulses (one full byte + ACK), the slave should release SDA.

### 2. Bus Reset via STOP Condition

After SDA is released, send a proper STOP condition to reset all slaves to their idle state.

### 3. Hardware Reset

If software recovery fails, toggle the reset pin of slave devices or power cycle them.

### 4. I2C Bus Clear Command

Some I2C master controllers have built-in bus clear functionality that automates the recovery process.

## C/C++ Implementation

### Basic Recovery Function

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction layer functions (platform-specific)
extern void gpio_set_mode_output(uint8_t pin);
extern void gpio_set_mode_input(uint8_t pin);
extern void gpio_write(uint8_t pin, bool state);
extern bool gpio_read(uint8_t pin);
extern void delay_us(uint32_t us);

// Pin definitions
#define SDA_PIN 4
#define SCL_PIN 5

/**
 * Check if I2C bus is stuck
 * Returns true if either SDA or SCL is stuck low
 */
bool i2c_is_bus_stuck(void) {
    // Both lines should be high when bus is idle
    bool sda_state = gpio_read(SDA_PIN);
    bool scl_state = gpio_read(SCL_PIN);
    
    return (!sda_state || !scl_state);
}

/**
 * Perform I2C bus hang recovery
 * Returns true if recovery was successful
 */
bool i2c_recover_bus(void) {
    uint8_t clock_count = 0;
    const uint8_t MAX_CLOCKS = 9;
    
    // Step 1: Configure pins as GPIO outputs
    gpio_set_mode_output(SDA_PIN);
    gpio_set_mode_output(SCL_PIN);
    
    // Step 2: Ensure SCL is high
    gpio_write(SCL_PIN, true);
    delay_us(10);
    
    // Step 3: Toggle SCL up to 9 times while monitoring SDA
    gpio_set_mode_input(SDA_PIN);  // SDA as input to monitor
    
    while (clock_count < MAX_CLOCKS) {
        gpio_write(SCL_PIN, false);
        delay_us(5);  // Half clock period
        
        gpio_write(SCL_PIN, true);
        delay_us(5);
        
        // Check if SDA is released (high)
        if (gpio_read(SDA_PIN)) {
            break;  // SDA released, slave is ready
        }
        
        clock_count++;
    }
    
    // Step 4: Generate STOP condition
    gpio_set_mode_output(SDA_PIN);
    gpio_write(SDA_PIN, false);
    delay_us(5);
    
    gpio_write(SCL_PIN, true);
    delay_us(5);
    
    gpio_write(SDA_PIN, true);  // SDA rises while SCL is high = STOP
    delay_us(10);
    
    // Step 5: Return pins to I2C peripheral control
    // (Platform-specific: re-initialize I2C peripheral)
    
    // Step 6: Verify bus is free
    gpio_set_mode_input(SDA_PIN);
    gpio_set_mode_input(SCL_PIN);
    delay_us(10);
    
    bool sda_high = gpio_read(SDA_PIN);
    bool scl_high = gpio_read(SCL_PIN);
    
    return (sda_high && scl_high);
}
```

### Advanced Recovery with Timeout

```c
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RECOVERY_SUCCESS = 0,
    RECOVERY_SDA_STUCK,
    RECOVERY_SCL_STUCK,
    RECOVERY_TIMEOUT,
    RECOVERY_PARTIAL
} recovery_status_t;

typedef struct {
    uint8_t sda_pin;
    uint8_t scl_pin;
    uint32_t clock_speed_hz;
    uint8_t max_clock_pulses;
    uint32_t timeout_ms;
} i2c_recovery_config_t;

/**
 * Advanced I2C bus recovery with detailed status
 */
recovery_status_t i2c_recover_bus_advanced(const i2c_recovery_config_t* config) {
    uint8_t clock_count = 0;
    uint32_t half_period_us = (1000000 / config->clock_speed_hz) / 2;
    
    // Initial bus state check
    gpio_set_mode_input(config->sda_pin);
    gpio_set_mode_input(config->scl_pin);
    delay_us(10);
    
    bool initial_sda = gpio_read(config->sda_pin);
    bool initial_scl = gpio_read(config->scl_pin);
    
    // If both high, bus is already free
    if (initial_sda && initial_scl) {
        return RECOVERY_SUCCESS;
    }
    
    // If SCL is stuck low, try to release it first
    if (!initial_scl) {
        gpio_set_mode_output(config->scl_pin);
        gpio_write(config->scl_pin, true);
        delay_us(100);
        
        gpio_set_mode_input(config->scl_pin);
        delay_us(10);
        
        if (!gpio_read(config->scl_pin)) {
            return RECOVERY_SCL_STUCK;  // Cannot recover if SCL stuck
        }
    }
    
    // Configure for clock pulse generation
    gpio_set_mode_output(config->scl_pin);
    gpio_set_mode_input(config->sda_pin);
    
    // Generate clock pulses
    while (clock_count < config->max_clock_pulses) {
        // Clock low
        gpio_write(config->scl_pin, false);
        delay_us(half_period_us);
        
        // Clock high
        gpio_write(config->scl_pin, true);
        delay_us(half_period_us);
        
        // Check if SDA released
        if (gpio_read(config->sda_pin)) {
            break;
        }
        
        clock_count++;
    }
    
    // Check if SDA was released
    if (!gpio_read(config->sda_pin)) {
        return RECOVERY_SDA_STUCK;
    }
    
    // Generate STOP condition
    gpio_set_mode_output(config->sda_pin);
    gpio_write(config->sda_pin, false);
    delay_us(half_period_us);
    
    gpio_write(config->scl_pin, true);
    delay_us(half_period_us);
    
    gpio_write(config->sda_pin, true);
    delay_us(half_period_us * 2);
    
    // Final verification
    gpio_set_mode_input(config->sda_pin);
    gpio_set_mode_input(config->scl_pin);
    delay_us(10);
    
    bool final_sda = gpio_read(config->sda_pin);
    bool final_scl = gpio_read(config->scl_pin);
    
    if (final_sda && final_scl) {
        return RECOVERY_SUCCESS;
    } else {
        return RECOVERY_PARTIAL;
    }
}

/**
 * Example usage with error handling
 */
void example_recovery_usage(void) {
    i2c_recovery_config_t config = {
        .sda_pin = SDA_PIN,
        .scl_pin = SCL_PIN,
        .clock_speed_hz = 100000,  // 100kHz
        .max_clock_pulses = 9,
        .timeout_ms = 1000
    };
    
    recovery_status_t status = i2c_recover_bus_advanced(&config);
    
    switch (status) {
        case RECOVERY_SUCCESS:
            // Reinitialize I2C peripheral
            // i2c_init();
            break;
            
        case RECOVERY_SDA_STUCK:
            // SDA still stuck - possible hardware issue
            // Try hardware reset or power cycle
            break;
            
        case RECOVERY_SCL_STUCK:
            // SCL stuck - severe hardware issue
            // Requires hardware intervention
            break;
            
        case RECOVERY_PARTIAL:
            // Partial recovery - may need additional attempts
            break;
            
        default:
            break;
    }
}
```

### Integration with I2C Driver

```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool auto_recovery;
    uint8_t max_retries;
    uint32_t recovery_delay_ms;
} i2c_driver_config_t;

/**
 * I2C transaction with automatic recovery
 */
bool i2c_write_with_recovery(uint8_t slave_addr, 
                              const uint8_t* data, 
                              size_t len,
                              const i2c_driver_config_t* config) {
    uint8_t retry_count = 0;
    
    while (retry_count < config->max_retries) {
        // Attempt normal I2C write
        if (i2c_write(slave_addr, data, len)) {
            return true;  // Success
        }
        
        // Check if bus is stuck
        if (config->auto_recovery && i2c_is_bus_stuck()) {
            // Attempt recovery
            if (i2c_recover_bus()) {
                // Reinitialize I2C peripheral
                i2c_init();
                
                // Wait before retry
                delay_ms(config->recovery_delay_ms);
                
                retry_count++;
                continue;
            } else {
                // Recovery failed
                return false;
            }
        }
        
        // Transaction failed for other reasons
        return false;
    }
    
    return false;  // Max retries exceeded
}
```

## Rust Implementation

### Basic Recovery Implementation

```rust
use embedded_hal::digital::v2::{InputPin, OutputPin};
use embedded_hal::blocking::delay::DelayUs;

pub struct I2cRecovery<SDA, SCL, DELAY>
where
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    sda: SDA,
    scl: SCL,
    delay: DELAY,
    clock_speed_hz: u32,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RecoveryError {
    SdaStuck,
    SclStuck,
    Timeout,
    GpioError,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RecoveryStatus {
    Success,
    AlreadyFree,
    Recovered(u8), // Number of clock pulses needed
}

impl<SDA, SCL, DELAY> I2cRecovery<SDA, SCL, DELAY>
where
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    pub fn new(sda: SDA, scl: SCL, delay: DELAY, clock_speed_hz: u32) -> Self {
        Self {
            sda,
            scl,
            delay,
            clock_speed_hz,
        }
    }

    /// Check if the I2C bus is stuck
    pub fn is_bus_stuck(&mut self) -> Result<bool, RecoveryError> {
        // Configure pins as inputs
        self.sda.set_high().map_err(|_| RecoveryError::GpioError)?;
        self.scl.set_high().map_err(|_| RecoveryError::GpioError)?;
        
        self.delay.delay_us(10);
        
        let sda_state = self.sda.is_high().map_err(|_| RecoveryError::GpioError)?;
        let scl_state = self.scl.is_high().map_err(|_| RecoveryError::GpioError)?;
        
        Ok(!sda_state || !scl_state)
    }

    /// Perform bus recovery
    pub fn recover(&mut self, max_clocks: u8) -> Result<RecoveryStatus, RecoveryError> {
        let half_period_us = (1_000_000 / self.clock_speed_hz) / 2;
        
        // Check initial state
        self.sda.set_high().map_err(|_| RecoveryError::GpioError)?;
        self.scl.set_high().map_err(|_| RecoveryError::GpioError)?;
        self.delay.delay_us(10);
        
        let sda_high = self.sda.is_high().map_err(|_| RecoveryError::GpioError)?;
        let scl_high = self.scl.is_high().map_err(|_| RecoveryError::GpioError)?;
        
        if sda_high && scl_high {
            return Ok(RecoveryStatus::AlreadyFree);
        }
        
        // If SCL is stuck, try to release it
        if !scl_high {
            self.scl.set_high().map_err(|_| RecoveryError::GpioError)?;
            self.delay.delay_us(100);
            
            if !self.scl.is_high().map_err(|_| RecoveryError::GpioError)? {
                return Err(RecoveryError::SclStuck);
            }
        }
        
        // Generate clock pulses to free SDA
        let mut clock_count = 0;
        
        while clock_count < max_clocks {
            // SCL low
            self.scl.set_low().map_err(|_| RecoveryError::GpioError)?;
            self.delay.delay_us(half_period_us);
            
            // SCL high
            self.scl.set_high().map_err(|_| RecoveryError::GpioError)?;
            self.delay.delay_us(half_period_us);
            
            // Check if SDA is released
            if self.sda.is_high().map_err(|_| RecoveryError::GpioError)? {
                break;
            }
            
            clock_count += 1;
        }
        
        // Verify SDA is released
        if !self.sda.is_high().map_err(|_| RecoveryError::GpioError)? {
            return Err(RecoveryError::SdaStuck);
        }
        
        // Generate STOP condition
        self.generate_stop_condition(half_period_us)?;
        
        // Final verification
        self.delay.delay_us(10);
        let final_sda = self.sda.is_high().map_err(|_| RecoveryError::GpioError)?;
        let final_scl = self.scl.is_high().map_err(|_| RecoveryError::GpioError)?;
        
        if final_sda && final_scl {
            Ok(RecoveryStatus::Recovered(clock_count))
        } else {
            Err(RecoveryError::SdaStuck)
        }
    }

    /// Generate a STOP condition
    fn generate_stop_condition(&mut self, half_period_us: u32) -> Result<(), RecoveryError> {
        // SDA low
        self.sda.set_low().map_err(|_| RecoveryError::GpioError)?;
        self.delay.delay_us(half_period_us);
        
        // SCL high
        self.scl.set_high().map_err(|_| RecoveryError::GpioError)?;
        self.delay.delay_us(half_period_us);
        
        // SDA high (while SCL is high = STOP)
        self.sda.set_high().map_err(|_| RecoveryError::GpioError)?;
        self.delay.delay_us(half_period_us * 2);
        
        Ok(())
    }

    /// Consume self and return the pins
    pub fn release(self) -> (SDA, SCL, DELAY) {
        (self.sda, self.scl, self.delay)
    }
}
```

### Advanced Recovery with Builder Pattern

```rust
use core::marker::PhantomData;

pub struct I2cRecoveryBuilder<SDA, SCL, DELAY> {
    sda: Option<SDA>,
    scl: Option<SCL>,
    delay: Option<DELAY>,
    clock_speed_hz: u32,
    max_clock_pulses: u8,
    timeout_ms: u32,
}

impl<SDA, SCL, DELAY> Default for I2cRecoveryBuilder<SDA, SCL, DELAY> {
    fn default() -> Self {
        Self {
            sda: None,
            scl: None,
            delay: None,
            clock_speed_hz: 100_000, // 100kHz default
            max_clock_pulses: 9,
            timeout_ms: 1000,
        }
    }
}

impl<SDA, SCL, DELAY> I2cRecoveryBuilder<SDA, SCL, DELAY>
where
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    pub fn new() -> Self {
        Self::default()
    }

    pub fn sda(mut self, sda: SDA) -> Self {
        self.sda = Some(sda);
        self
    }

    pub fn scl(mut self, scl: SCL) -> Self {
        self.scl = Some(scl);
        self
    }

    pub fn delay(mut self, delay: DELAY) -> Self {
        self.delay = Some(delay);
        self
    }

    pub fn clock_speed(mut self, hz: u32) -> Self {
        self.clock_speed_hz = hz;
        self
    }

    pub fn max_clock_pulses(mut self, pulses: u8) -> Self {
        self.max_clock_pulses = pulses;
        self
    }

    pub fn build(self) -> Option<I2cRecovery<SDA, SCL, DELAY>> {
        Some(I2cRecovery {
            sda: self.sda?,
            scl: self.scl?,
            delay: self.delay?,
            clock_speed_hz: self.clock_speed_hz,
        })
    }
}
```

### Integration with embedded-hal I2C Driver

```rust
use embedded_hal::blocking::i2c::{Write, WriteRead};

pub struct I2cWithRecovery<I2C, SDA, SCL, DELAY>
where
    I2C: Write + WriteRead,
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    i2c: I2C,
    recovery: I2cRecovery<SDA, SCL, DELAY>,
    auto_recovery: bool,
    max_retries: u8,
}

impl<I2C, SDA, SCL, DELAY> I2cWithRecovery<I2C, SDA, SCL, DELAY>
where
    I2C: Write + WriteRead,
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    pub fn new(
        i2c: I2C,
        recovery: I2cRecovery<SDA, SCL, DELAY>,
        auto_recovery: bool,
    ) -> Self {
        Self {
            i2c,
            recovery,
            auto_recovery,
            max_retries: 3,
        }
    }

    /// Write with automatic recovery on bus hang
    pub fn write_with_recovery(
        &mut self,
        address: u8,
        bytes: &[u8],
    ) -> Result<(), RecoveryError> {
        let mut retries = 0;

        loop {
            match self.i2c.write(address, bytes) {
                Ok(_) => return Ok(()),
                Err(_) => {
                    if self.auto_recovery && retries < self.max_retries {
                        // Check if bus is stuck
                        if self.recovery.is_bus_stuck()? {
                            // Attempt recovery
                            match self.recovery.recover(9) {
                                Ok(status) => {
                                    // Recovery successful, retry transaction
                                    retries += 1;
                                    continue;
                                }
                                Err(e) => return Err(e),
                            }
                        }
                    }
                    return Err(RecoveryError::Timeout);
                }
            }
        }
    }
}
```

### Example Usage

```rust
// Example for embedded systems (no_std)
#![no_std]

use embedded_hal::digital::v2::{InputPin, OutputPin};
use embedded_hal::blocking::delay::DelayUs;

fn example_recovery<SDA, SCL, DELAY>(
    sda_pin: SDA,
    scl_pin: SCL,
    mut delay: DELAY,
) -> Result<(), RecoveryError>
where
    SDA: InputPin + OutputPin,
    SCL: InputPin + OutputPin,
    DELAY: DelayUs<u32>,
{
    // Create recovery instance
    let mut recovery = I2cRecovery::new(sda_pin, scl_pin, delay, 100_000);

    // Check if bus is stuck
    if recovery.is_bus_stuck()? {
        // Perform recovery
        match recovery.recover(9) {
            Ok(RecoveryStatus::Success) => {
                // Bus recovered, reinitialize I2C peripheral
                println!("Bus recovered successfully");
            }
            Ok(RecoveryStatus::AlreadyFree) => {
                println!("Bus was already free");
            }
            Ok(RecoveryStatus::Recovered(pulses)) => {
                println!("Bus recovered after {} clock pulses", pulses);
            }
            Err(RecoveryError::SdaStuck) => {
                println!("SDA line is stuck - hardware issue");
            }
            Err(RecoveryError::SclStuck) => {
                println!("SCL line is stuck - severe hardware issue");
            }
            Err(e) => {
                println!("Recovery failed: {:?}", e);
            }
        }
    }

    Ok(())
}
```

## Best Practices

1. **Check before recovery**: Always verify the bus is actually stuck before attempting recovery to avoid unnecessary operations.

2. **Limit clock pulses**: Send a maximum of 9 clock pulses (one byte + ACK bit). More pulses risk confusing devices further.

3. **Use appropriate timing**: Match your recovery clock speed to the normal I2C bus speed (typically 100kHz or lower for recovery).

4. **Always send STOP**: After recovering SDA, always generate a proper STOP condition to reset slave state machines.

5. **Implement retry logic**: Combine recovery with transaction retry mechanisms for robust operation.

6. **Log recovery events**: Track when recoveries occur to identify problematic devices or environmental issues.

7. **Hardware reset as fallback**: If software recovery fails repeatedly, implement hardware reset capabilities for slave devices.

8. **Power cycle option**: For critical systems, consider adding relay/MOSFET control to power cycle slave devices.

9. **Avoid during normal operation**: Don't call recovery routines unnecessarily as they temporarily disable the I2C peripheral.

10. **Test thoroughly**: Simulate bus hang conditions during development to validate recovery mechanisms work correctly.

These implementations provide robust I2C bus hang recovery suitable for embedded systems where bus reliability is critical.