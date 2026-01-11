# I2C Start and Stop Conditions

## Overview

Start and Stop conditions are fundamental control signals in I2C communication that define the boundaries of data transactions on the bus. These special conditions are generated exclusively by the master device and serve as markers for when communication begins and ends.

## What Are Start and Stop Conditions?

### Start Condition (S)
A **Start condition** occurs when the SDA (data) line transitions from HIGH to LOW while the SCL (clock) line remains HIGH. This signals to all devices on the bus that the master is about to initiate a communication sequence.

```
SCL: ----HIGH------------------------
SDA: HIGH-----LOW (Start condition)
```

### Stop Condition (P)
A **Stop condition** occurs when the SDA line transitions from LOW to HIGH while the SCL line remains HIGH. This indicates that the master has finished the current transaction and is releasing the bus.

```
SCL: ----HIGH------------------------
SDA: LOW------HIGH (Stop condition)
```

### Repeated Start Condition (Sr)
A **Repeated Start** is a Start condition generated without first generating a Stop condition. This allows the master to maintain control of the bus while switching between different slaves or changing data direction without releasing the bus.

## Why They Matter

- **Bus Arbitration**: Define clear transaction boundaries in a multi-master environment
- **Synchronization**: All devices recognize when communication begins and ends
- **Power Efficiency**: Devices can enter low-power modes between transactions
- **Protocol Integrity**: Prevent data corruption by clearly marking message boundaries

## Timing Requirements

The I2C specification defines specific timing parameters:

- **tHD;STA** (Start hold time): Minimum time SCL must be HIGH before SDA falls
- **tSU;STA** (Start setup time): Minimum time SDA must be stable before SCL falls
- **tSU;STO** (Stop setup time): Minimum time before Stop condition
- **tBUF** (Bus free time): Minimum time between Stop and next Start

These vary by speed mode (Standard: 100kHz, Fast: 400kHz, Fast-mode Plus: 1MHz, High-speed: 3.4MHz).

## Implementation Examples

### C/C++ Implementation (Bare Metal)

Here's a low-level implementation for generating Start and Stop conditions:

```c
#include <stdint.h>
#include <stdbool.h>

// GPIO register definitions (example for ARM Cortex-M)
#define GPIO_BASE 0x40020000
#define SDA_PIN (1 << 9)  // Example: PB9
#define SCL_PIN (1 << 8)  // Example: PB8

typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
} GPIO_TypeDef;

#define GPIOB ((GPIO_TypeDef*)GPIO_BASE)

// Timing delays (adjust based on your clock frequency)
static inline void i2c_delay_half_clock(void) {
    // For 100kHz I2C, half period = 5us
    for(volatile int i = 0; i < 50; i++);
}

static inline void sda_high(void) {
    GPIOB->BSRR = SDA_PIN;  // Set SDA
}

static inline void sda_low(void) {
    GPIOB->BSRR = (SDA_PIN << 16);  // Reset SDA
}

static inline void scl_high(void) {
    GPIOB->BSRR = SCL_PIN;  // Set SCL
}

static inline void scl_low(void) {
    GPIOB->BSRR = (SCL_PIN << 16);  // Reset SCL
}

static inline bool sda_read(void) {
    return (GPIOB->IDR & SDA_PIN) != 0;
}

// Generate I2C Start condition
void i2c_start(void) {
    // Ensure SDA and SCL are high (idle state)
    sda_high();
    scl_high();
    i2c_delay_half_clock();
    
    // Start condition: SDA falls while SCL is high
    sda_low();
    i2c_delay_half_clock();
    
    // Pull SCL low to begin data transfer
    scl_low();
    i2c_delay_half_clock();
}

// Generate I2C Stop condition
void i2c_stop(void) {
    // Ensure SCL is low
    scl_low();
    sda_low();
    i2c_delay_half_clock();
    
    // Stop condition: SCL goes high first
    scl_high();
    i2c_delay_half_clock();
    
    // Then SDA goes high while SCL is high
    sda_high();
    i2c_delay_half_clock();
}

// Generate Repeated Start condition
void i2c_repeated_start(void) {
    // Ensure SCL is low
    scl_low();
    i2c_delay_half_clock();
    
    // Bring SDA high while SCL is low
    sda_high();
    i2c_delay_half_clock();
    
    // Then raise SCL
    scl_high();
    i2c_delay_half_clock();
    
    // Generate start: SDA falls while SCL is high
    sda_low();
    i2c_delay_half_clock();
    
    // Pull SCL low for data transfer
    scl_low();
    i2c_delay_half_clock();
}

// Example usage: Write byte with Start and Stop
bool i2c_write_byte(uint8_t data) {
    for(int i = 7; i >= 0; i--) {
        scl_low();
        if(data & (1 << i)) {
            sda_high();
        } else {
            sda_low();
        }
        i2c_delay_half_clock();
        
        scl_high();
        i2c_delay_half_clock();
    }
    
    // Check ACK
    scl_low();
    sda_high();  // Release SDA for slave ACK
    i2c_delay_half_clock();
    
    scl_high();
    bool ack = !sda_read();  // ACK is low
    i2c_delay_half_clock();
    scl_low();
    
    return ack;
}

// Complete transaction example
void i2c_write_register(uint8_t device_addr, uint8_t reg_addr, uint8_t value) {
    i2c_start();
    i2c_write_byte(device_addr << 1);  // Write mode (LSB = 0)
    i2c_write_byte(reg_addr);
    i2c_write_byte(value);
    i2c_stop();
}
```

### C++ with Hardware Abstraction

```cpp
#include <cstdint>
#include <chrono>
#include <thread>

class I2CMaster {
private:
    volatile uint32_t* sda_port_;
    volatile uint32_t* scl_port_;
    uint32_t sda_pin_;
    uint32_t scl_pin_;
    std::chrono::microseconds half_period_;
    
    void delay() const {
        std::this_thread::sleep_for(half_period_);
    }
    
    void set_sda(bool state) {
        if(state) {
            *sda_port_ |= sda_pin_;
        } else {
            *sda_port_ &= ~sda_pin_;
        }
    }
    
    void set_scl(bool state) {
        if(state) {
            *scl_port_ |= scl_pin_;
        } else {
            *scl_port_ &= ~scl_pin_;
        }
    }
    
    bool read_sda() const {
        return (*sda_port_ & sda_pin_) != 0;
    }

public:
    I2CMaster(volatile uint32_t* sda_port, uint32_t sda_pin,
              volatile uint32_t* scl_port, uint32_t scl_pin,
              uint32_t frequency_hz = 100000)
        : sda_port_(sda_port), scl_port_(scl_port),
          sda_pin_(sda_pin), scl_pin_(scl_pin),
          half_period_(1000000 / (2 * frequency_hz)) {
    }
    
    void start() {
        // Idle state: both lines high
        set_sda(true);
        set_scl(true);
        delay();
        
        // Start: SDA goes low while SCL is high
        set_sda(false);
        delay();
        
        // Prepare for data transfer
        set_scl(false);
        delay();
    }
    
    void stop() {
        // Ensure proper state
        set_scl(false);
        set_sda(false);
        delay();
        
        // Stop: SCL goes high first
        set_scl(true);
        delay();
        
        // Then SDA goes high while SCL is high
        set_sda(true);
        delay();
    }
    
    void repeated_start() {
        set_scl(false);
        delay();
        
        set_sda(true);
        delay();
        
        set_scl(true);
        delay();
        
        // Start condition
        set_sda(false);
        delay();
        
        set_scl(false);
        delay();
    }
    
    bool write_byte(uint8_t data) {
        for(int i = 7; i >= 0; i--) {
            set_scl(false);
            set_sda((data >> i) & 1);
            delay();
            
            set_scl(true);
            delay();
        }
        
        // Read ACK
        set_scl(false);
        set_sda(true);  // Release for slave
        delay();
        
        set_scl(true);
        bool ack = !read_sda();
        delay();
        set_scl(false);
        
        return ack;
    }
    
    // High-level transaction methods
    bool write(uint8_t address, const uint8_t* data, size_t length) {
        start();
        
        if(!write_byte(address << 1)) {  // Write mode
            stop();
            return false;
        }
        
        for(size_t i = 0; i < length; i++) {
            if(!write_byte(data[i])) {
                stop();
                return false;
            }
        }
        
        stop();
        return true;
    }
};
```

### Rust Implementation

```rust
use core::sync::atomic::{AtomicBool, Ordering};
use embedded_hal::digital::v2::{InputPin, OutputPin};

pub struct I2CMaster<SDA, SCL> 
where
    SDA: InputPin + OutputPin,
    SCL: OutputPin,
{
    sda: SDA,
    scl: SCL,
    delay_cycles: u32,
}

impl<SDA, SCL> I2CMaster<SDA, SCL>
where
    SDA: InputPin + OutputPin,
    SCL: OutputPin,
{
    pub fn new(sda: SDA, scl: SCL, frequency_hz: u32, cpu_freq_hz: u32) -> Self {
        let delay_cycles = cpu_freq_hz / (2 * frequency_hz);
        Self {
            sda,
            scl,
            delay_cycles,
        }
    }
    
    #[inline]
    fn delay(&self) {
        // Simple delay loop - replace with proper timer in production
        for _ in 0..self.delay_cycles {
            core::sync::atomic::compiler_fence(Ordering::SeqCst);
        }
    }
    
    /// Generate I2C Start condition
    /// SDA transitions from HIGH to LOW while SCL is HIGH
    pub fn start(&mut self) -> Result<(), ()> {
        // Ensure idle state (both lines high)
        self.sda.set_high().map_err(|_| ())?;
        self.scl.set_high().map_err(|_| ())?;
        self.delay();
        
        // Start condition: SDA goes low while SCL is high
        self.sda.set_low().map_err(|_| ())?;
        self.delay();
        
        // Pull SCL low to begin data transfer
        self.scl.set_low().map_err(|_| ())?;
        self.delay();
        
        Ok(())
    }
    
    /// Generate I2C Stop condition
    /// SDA transitions from LOW to HIGH while SCL is HIGH
    pub fn stop(&mut self) -> Result<(), ()> {
        // Ensure SCL is low and SDA is low
        self.scl.set_low().map_err(|_| ())?;
        self.sda.set_low().map_err(|_| ())?;
        self.delay();
        
        // Stop condition: SCL goes high first
        self.scl.set_high().map_err(|_| ())?;
        self.delay();
        
        // Then SDA goes high while SCL is high
        self.sda.set_high().map_err(|_| ())?;
        self.delay();
        
        Ok(())
    }
    
    /// Generate Repeated Start condition
    /// Start without preceding Stop - maintains bus control
    pub fn repeated_start(&mut self) -> Result<(), ()> {
        // SCL should be low from previous data bit
        self.scl.set_low().map_err(|_| ())?;
        self.delay();
        
        // Bring SDA high while SCL is low
        self.sda.set_high().map_err(|_| ())?;
        self.delay();
        
        // Raise SCL
        self.scl.set_high().map_err(|_| ())?;
        self.delay();
        
        // Start condition: SDA falls while SCL is high
        self.sda.set_low().map_err(|_| ())?;
        self.delay();
        
        // Pull SCL low for data transfer
        self.scl.set_low().map_err(|_| ())?;
        self.delay();
        
        Ok(())
    }
    
    /// Write a single byte to the bus
    pub fn write_byte(&mut self, data: u8) -> Result<bool, ()> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            self.scl.set_low().map_err(|_| ())?;
            
            if (data >> i) & 1 == 1 {
                self.sda.set_high().map_err(|_| ())?;
            } else {
                self.sda.set_low().map_err(|_| ())?;
            }
            self.delay();
            
            self.scl.set_high().map_err(|_| ())?;
            self.delay();
        }
        
        // Read ACK bit
        self.scl.set_low().map_err(|_| ())?;
        self.sda.set_high().map_err(|_| ())?;  // Release SDA for slave
        self.delay();
        
        self.scl.set_high().map_err(|_| ())?;
        let ack = self.sda.is_low().map_err(|_| ())?;  // ACK is low
        self.delay();
        self.scl.set_low().map_err(|_| ())?;
        
        Ok(ack)
    }
    
    /// High-level write transaction
    pub fn write_to_device(&mut self, address: u8, data: &[u8]) -> Result<(), ()> {
        self.start()?;
        
        // Send address with write bit (0)
        if !self.write_byte(address << 1)? {
            self.stop()?;
            return Err(());
        }
        
        // Send data bytes
        for &byte in data {
            if !self.write_byte(byte)? {
                self.stop()?;
                return Err(());
            }
        }
        
        self.stop()?;
        Ok(())
    }
    
    /// Example: Read with Repeated Start
    pub fn write_then_read(&mut self, address: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<(), ()> {
        // Write phase
        self.start()?;
        if !self.write_byte(address << 1)? {  // Write mode
            self.stop()?;
            return Err(());
        }
        
        for &byte in write_data {
            if !self.write_byte(byte)? {
                self.stop()?;
                return Err(());
            }
        }
        
        // Repeated Start for read phase
        self.repeated_start()?;
        
        if !self.write_byte((address << 1) | 1)? {  // Read mode
            self.stop()?;
            return Err(());
        }
        
        // Read data bytes (implementation would continue here)
        // ...
        
        self.stop()?;
        Ok(())
    }
}

// Example usage with embedded-hal traits
#[cfg(feature = "example")]
mod example {
    use super::*;
    
    pub fn demonstrate_start_stop<SDA, SCL>(mut i2c: I2CMaster<SDA, SCL>)
    where
        SDA: InputPin + OutputPin,
        SCL: OutputPin,
    {
        // Simple write transaction
        let device_addr = 0x50;  // EEPROM address
        let data = [0x00, 0x42];  // Register address and value
        
        if let Err(_) = i2c.write_to_device(device_addr, &data) {
            // Handle error
        }
        
        // Write then read transaction with Repeated Start
        let write_buf = [0x00];  // Register address
        let mut read_buf = [0u8; 4];
        
        if let Err(_) = i2c.write_then_read(device_addr, &write_buf, &mut read_buf) {
            // Handle error
        }
    }
}
```

## Key Concepts Summary

1. **Start Condition**: Master gains bus control; SDA falls while SCL is HIGH
2. **Stop Condition**: Master releases bus; SDA rises while SCL is HIGH
3. **Repeated Start**: Maintains bus control between transactions without releasing
4. **Bus Ownership**: Only the master generates these conditions
5. **Timing Critical**: Must respect I2C specification timing parameters

These conditions are the foundation of all I2C communication, and proper implementation ensures reliable data transfer on the bus.