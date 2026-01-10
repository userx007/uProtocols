# I²C Bus Arbitration: Multi-Master Collision Detection and Resolution

## Overview

I²C bus arbitration is a critical mechanism that allows multiple master devices to share the same bus without requiring a central arbiter. The protocol uses a clever **wired-AND** configuration combined with clock stretching to resolve conflicts when multiple masters attempt to communicate simultaneously.

## How Arbitration Works

### The Wired-AND Principle

The I²C bus uses open-drain (or open-collector) outputs with pull-up resistors. This creates a **wired-AND** logic:
- Any device can pull the line LOW (dominant state)
- A line only goes HIGH when all devices release it (recessive state)
- If one master writes 0 and another writes 1, the bus reads 0

### Arbitration Process

1. **Simultaneous START**: Multiple masters may issue START conditions simultaneously
2. **Bit-by-bit comparison**: Each master monitors the bus while transmitting
3. **Collision detection**: If a master writes 1 but reads 0, it has lost arbitration
4. **Graceful退出**: The losing master immediately stops driving the bus and becomes a slave
5. **Winner continues**: The winning master completes its transaction unaware of the conflict

### Key Rules

- Arbitration occurs on the **SDA line** while SCL is HIGH
- A master loses if it releases SDA (writes 1) but another master holds it LOW (writes 0)
- The master transmitting the lowest address/data value wins
- Lost masters must wait and retry later

## Code Examples

### C/C++ Implementation

Here's a comprehensive example showing multi-master arbitration handling:

```c
/*
 * I2C Multi-Master Bus Arbitration Implementation
 * Demonstrates collision detection and graceful arbitration loss handling
 */

#include <stdint.h>
#include <stdbool.h>

// Hardware-specific definitions (adjust for your platform)
#define I2C_SDA_PIN     (1 << 0)
#define I2C_SCL_PIN     (1 << 1)
#define I2C_PORT        (*((volatile uint32_t*)0x40000000))
#define I2C_PIN_STATE   (*((volatile uint32_t*)0x40000004))

// Arbitration states
typedef enum {
    ARB_WON,
    ARB_LOST,
    ARB_IN_PROGRESS
} ArbitrationState;

// I2C Master context
typedef struct {
    ArbitrationState arb_state;
    uint32_t retry_count;
    uint32_t max_retries;
    bool is_master;
} I2C_Master;

// Initialize I2C pins as open-drain
void i2c_init_multi_master(I2C_Master *master) {
    // Configure pins as open-drain outputs with pull-ups
    // Implementation depends on your microcontroller
    master->arb_state = ARB_IN_PROGRESS;
    master->retry_count = 0;
    master->max_retries = 3;
    master->is_master = true;
}

// Set SDA line (0 = LOW, 1 = release to HIGH via pull-up)
static inline void i2c_sda_write(bool state) {
    if (state) {
        I2C_PORT |= I2C_SDA_PIN;  // Release (HIGH via pull-up)
    } else {
        I2C_PORT &= ~I2C_SDA_PIN; // Drive LOW
    }
}

// Read SDA line state
static inline bool i2c_sda_read(void) {
    return (I2C_PIN_STATE & I2C_SDA_PIN) != 0;
}

// Set SCL line
static inline void i2c_scl_write(bool state) {
    if (state) {
        I2C_PORT |= I2C_SCL_PIN;
    } else {
        I2C_PORT &= ~I2C_SCL_PIN;
    }
}

// Read SCL line (for clock stretching)
static inline bool i2c_scl_read(void) {
    return (I2C_PIN_STATE & I2C_SCL_PIN) != 0;
}

// Delay function (adjust timing for your clock speed)
static void i2c_delay(void) {
    for (volatile int i = 0; i < 100; i++);
}

// Write a bit with arbitration checking
bool i2c_write_bit_with_arbitration(I2C_Master *master, bool bit) {
    // Release SCL
    i2c_scl_write(1);
    i2c_delay();
    
    // Wait for clock stretching
    while (!i2c_scl_read()) {
        // Slave might be stretching clock
    }
    
    // Write the bit
    i2c_sda_write(bit);
    i2c_delay();
    
    // Read back the actual bus state
    bool actual_bit = i2c_sda_read();
    
    // Arbitration check: if we wrote 1 but read 0, we lost
    if (bit && !actual_bit) {
        master->arb_state = ARB_LOST;
        master->is_master = false;
        return false; // Arbitration lost
    }
    
    // Pull SCL low for next bit
    i2c_scl_write(0);
    i2c_delay();
    
    return true; // Still winning or bit matched
}

// Send START condition with arbitration
bool i2c_start_with_arbitration(I2C_Master *master) {
    // Ensure bus is idle (both lines HIGH)
    i2c_sda_write(1);
    i2c_scl_write(1);
    i2c_delay();
    
    // Check if bus is busy
    if (!i2c_sda_read() || !i2c_scl_read()) {
        // Bus busy, another master is using it
        return false;
    }
    
    // Generate START: SDA falls while SCL is HIGH
    i2c_sda_write(0);
    i2c_delay();
    i2c_scl_write(0);
    i2c_delay();
    
    master->arb_state = ARB_IN_PROGRESS;
    return true;
}

// Write a byte with arbitration checking
bool i2c_write_byte_with_arbitration(I2C_Master *master, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        bool bit = (byte >> i) & 0x01;
        
        if (!i2c_write_bit_with_arbitration(master, bit)) {
            // Arbitration lost during transmission
            return false;
        }
    }
    
    // Read ACK/NACK bit
    i2c_scl_write(1);
    i2c_sda_write(1); // Release SDA for ACK
    i2c_delay();
    
    bool ack = !i2c_sda_read(); // ACK is LOW
    
    i2c_scl_write(0);
    i2c_delay();
    
    master->arb_state = ARB_WON;
    return ack;
}

// Send STOP condition
void i2c_stop(I2C_Master *master) {
    i2c_sda_write(0);
    i2c_delay();
    i2c_scl_write(1);
    i2c_delay();
    i2c_sda_write(1);
    i2c_delay();
    
    master->is_master = true;
}

// High-level transaction with automatic retry on arbitration loss
bool i2c_master_write_with_retry(I2C_Master *master, uint8_t addr, 
                                  uint8_t *data, uint8_t len) {
    master->retry_count = 0;
    
    while (master->retry_count < master->max_retries) {
        // Try to start transaction
        if (!i2c_start_with_arbitration(master)) {
            master->retry_count++;
            continue; // Bus busy, retry
        }
        
        // Send address with write bit
        if (!i2c_write_byte_with_arbitration(master, (addr << 1) | 0)) {
            if (master->arb_state == ARB_LOST) {
                // Lost arbitration, retry after delay
                master->retry_count++;
                i2c_delay();
                continue;
            }
            // NACK received
            i2c_stop(master);
            return false;
        }
        
        // Send data bytes
        for (uint8_t i = 0; i < len; i++) {
            if (!i2c_write_byte_with_arbitration(master, data[i])) {
                if (master->arb_state == ARB_LOST) {
                    master->retry_count++;
                    i2c_delay();
                    goto retry;
                }
                i2c_stop(master);
                return false;
            }
        }
        
        i2c_stop(master);
        return true; // Success
        
        retry:
        continue;
    }
    
    return false; // Max retries exceeded
}

// Example usage
void example_multi_master(void) {
    I2C_Master master;
    i2c_init_multi_master(&master);
    
    uint8_t data[] = {0x12, 0x34, 0x56};
    uint8_t slave_addr = 0x50;
    
    if (i2c_master_write_with_retry(&master, slave_addr, data, 3)) {
        // Transaction successful
    } else {
        // Failed after retries
    }
}
```

### Rust Implementation

Now let's see a type-safe Rust implementation with proper error handling:

```rust
/// I2C Multi-Master Bus Arbitration Implementation in Rust
/// Demonstrates type-safe collision detection and error handling

use core::fmt;

/// Arbitration result
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArbitrationResult {
    Won,
    Lost,
    InProgress,
}

/// I2C errors including arbitration loss
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    ArbitrationLost,
    Nack,
    BusBusy,
    Timeout,
    MaxRetriesExceeded,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cError::ArbitrationLost => write!(f, "Arbitration lost to another master"),
            I2cError::Nack => write!(f, "NACK received from slave"),
            I2cError::BusBusy => write!(f, "Bus is busy"),
            I2cError::Timeout => write!(f, "Operation timed out"),
            I2cError::MaxRetriesExceeded => write!(f, "Maximum retry attempts exceeded"),
        }
    }
}

/// Hardware abstraction layer trait for I2C pins
pub trait I2cHal {
    fn sda_write(&mut self, state: bool);
    fn sda_read(&self) -> bool;
    fn scl_write(&mut self, state: bool);
    fn scl_read(&self) -> bool;
    fn delay(&self);
}

/// I2C Multi-Master controller
pub struct I2cMultiMaster<HAL: I2cHal> {
    hal: HAL,
    arb_state: ArbitrationResult,
    retry_count: u32,
    max_retries: u32,
    is_master: bool,
}

impl<HAL: I2cHal> I2cMultiMaster<HAL> {
    /// Create a new multi-master I2C controller
    pub fn new(hal: HAL, max_retries: u32) -> Self {
        Self {
            hal,
            arb_state: ArbitrationResult::InProgress,
            retry_count: 0,
            max_retries,
            is_master: true,
        }
    }

    /// Write a single bit with arbitration checking
    fn write_bit_with_arbitration(&mut self, bit: bool) -> Result<(), I2cError> {
        // Release SCL
        self.hal.scl_write(true);
        self.hal.delay();

        // Wait for any clock stretching
        let mut timeout = 1000;
        while !self.hal.scl_read() {
            timeout -= 1;
            if timeout == 0 {
                return Err(I2cError::Timeout);
            }
        }

        // Write the bit
        self.hal.sda_write(bit);
        self.hal.delay();

        // Read back actual bus state
        let actual_bit = self.hal.sda_read();

        // Arbitration check: if we wrote 1 but read 0, we lost
        if bit && !actual_bit {
            self.arb_state = ArbitrationResult::Lost;
            self.is_master = false;
            return Err(I2cError::ArbitrationLost);
        }

        // Pull SCL low for next bit
        self.hal.scl_write(false);
        self.hal.delay();

        Ok(())
    }

    /// Generate START condition with arbitration awareness
    pub fn start(&mut self) -> Result<(), I2cError> {
        // Ensure bus is idle
        self.hal.sda_write(true);
        self.hal.scl_write(true);
        self.hal.delay();

        // Check if bus is busy
        if !self.hal.sda_read() || !self.hal.scl_read() {
            return Err(I2cError::BusBusy);
        }

        // Generate START: SDA falls while SCL is HIGH
        self.hal.sda_write(false);
        self.hal.delay();
        self.hal.scl_write(false);
        self.hal.delay();

        self.arb_state = ArbitrationResult::InProgress;
        Ok(())
    }

    /// Write a byte with arbitration checking
    pub fn write_byte(&mut self, byte: u8) -> Result<bool, I2cError> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            let bit = (byte >> i) & 0x01 != 0;
            self.write_bit_with_arbitration(bit)?;
        }

        // Read ACK/NACK
        self.hal.scl_write(true);
        self.hal.sda_write(true); // Release SDA for ACK
        self.hal.delay();

        let ack = !self.hal.sda_read(); // ACK is LOW

        self.hal.scl_write(false);
        self.hal.delay();

        if ack {
            self.arb_state = ArbitrationResult::Won;
        }

        Ok(ack)
    }

    /// Read a byte from the bus
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut byte = 0u8;

        // Release SDA so slave can control it
        self.hal.sda_write(true);

        // Read 8 bits
        for _ in 0..8 {
            byte <<= 1;

            self.hal.scl_write(true);
            self.hal.delay();

            if self.hal.sda_read() {
                byte |= 0x01;
            }

            self.hal.scl_write(false);
            self.hal.delay();
        }

        // Send ACK or NACK
        self.hal.sda_write(!send_ack);
        self.hal.scl_write(true);
        self.hal.delay();
        self.hal.scl_write(false);
        self.hal.delay();

        Ok(byte)
    }

    /// Generate STOP condition
    pub fn stop(&mut self) {
        self.hal.sda_write(false);
        self.hal.delay();
        self.hal.scl_write(true);
        self.hal.delay();
        self.hal.sda_write(true);
        self.hal.delay();

        self.is_master = true;
    }

    /// Write data to a slave with automatic retry on arbitration loss
    pub fn write_with_retry(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        self.retry_count = 0;

        while self.retry_count < self.max_retries {
            match self.write_internal(addr, data) {
                Ok(()) => return Ok(()),
                Err(I2cError::ArbitrationLost) => {
                    // Arbitration lost, retry after delay
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(I2cError::BusBusy) => {
                    // Bus busy, retry
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(e) => return Err(e),
            }
        }

        Err(I2cError::MaxRetriesExceeded)
    }

    /// Internal write implementation
    fn write_internal(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        // Send START
        self.start()?;

        // Send address with write bit (0)
        let addr_byte = (addr << 1) | 0;
        if !self.write_byte(addr_byte)? {
            self.stop();
            return Err(I2cError::Nack);
        }

        // Send data bytes
        for &byte in data {
            if !self.write_byte(byte)? {
                self.stop();
                return Err(I2cError::Nack);
            }
        }

        self.stop();
        Ok(())
    }

    /// Read data from a slave with automatic retry
    pub fn read_with_retry(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        self.retry_count = 0;

        while self.retry_count < self.max_retries {
            match self.read_internal(addr, buffer) {
                Ok(()) => return Ok(()),
                Err(I2cError::ArbitrationLost) | Err(I2cError::BusBusy) => {
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(e) => return Err(e),
            }
        }

        Err(I2cError::MaxRetriesExceeded)
    }

    /// Internal read implementation
    fn read_internal(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        // Send START
        self.start()?;

        // Send address with read bit (1)
        let addr_byte = (addr << 1) | 1;
        if !self.write_byte(addr_byte)? {
            self.stop();
            return Err(I2cError::Nack);
        }

        // Read bytes
        let last_idx = buffer.len() - 1;
        for (i, byte) in buffer.iter_mut().enumerate() {
            let send_ack = i != last_idx; // NACK on last byte
            *byte = self.read_byte(send_ack)?;
        }

        self.stop();
        Ok(())
    }

    /// Get current arbitration state
    pub fn arbitration_state(&self) -> ArbitrationResult {
        self.arb_state
    }

    /// Check if currently acting as master
    pub fn is_master(&self) -> bool {
        self.is_master
    }
}

// Example mock HAL implementation for testing
pub struct MockHal {
    sda_state: bool,
    scl_state: bool,
    sda_external: bool, // Simulates another master's output
}

impl MockHal {
    pub fn new() -> Self {
        Self {
            sda_state: true,
            scl_state: true,
            sda_external: true,
        }
    }

    pub fn set_external_sda(&mut self, state: bool) {
        self.sda_external = state;
    }
}

impl I2cHal for MockHal {
    fn sda_write(&mut self, state: bool) {
        self.sda_state = state;
    }

    fn sda_read(&self) -> bool {
        // Wired-AND: both internal and external must be HIGH
        self.sda_state && self.sda_external
    }

    fn scl_write(&mut self, state: bool) {
        self.scl_state = state;
    }

    fn scl_read(&self) -> bool {
        self.scl_state
    }

    fn delay(&self) {
        // Mock delay
    }
}

// Example usage
fn example_usage() {
    let hal = MockHal::new();
    let mut i2c = I2cMultiMaster::new(hal, 3);

    let data = [0x12, 0x34, 0x56];
    match i2c.write_with_retry(0x50, &data) {
        Ok(()) => println!("Write successful"),
        Err(e) => println!("Write failed: {}", e),
    }

    let mut buffer = [0u8; 4];
    match i2c.read_with_retry(0x50, &mut buffer) {
        Ok(()) => println!("Read data: {:02X?}", buffer),
        Err(e) => println!("Read failed: {}", e),
    }
}
```

## Key Concepts Demonstrated

### 1. **Collision Detection**
Both implementations monitor the SDA line after writing each bit. If a master writes `1` (releases SDA) but reads `0` (another master pulls it low), arbitration is lost immediately.

### 2. **Graceful Degradation**
When arbitration is lost:
- The losing master immediately stops driving the bus
- It can become a slave or wait to retry
- No bus corruption occurs

### 3. **Retry Mechanism**
Both implementations include automatic retry logic:
- Failed transactions due to arbitration loss trigger retries
- Configurable maximum retry attempts
- Exponential backoff can be added for better behavior

### 4. **Wired-AND Logic**
The open-drain configuration means:
```
Master A writes: 1  0  1  0  1  1  0
Master B writes: 1  1  0  0  1  0  1
Bus reads:       1  0  0  0  1  0  0
                    ^  ^        ^  ^
                    B  A        B  B wins
                    wins loses
```

### 5. **Address-Based Priority**
Since lower addresses have more leading zeros, devices with lower addresses naturally win arbitration more often. This can be used strategically for priority assignment.

## Practical Considerations

**Arbitration happens only during:**
- Address transmission
- Data transmission
- NOT during acknowledge bits (receiver controls SDA)

**Clock stretching interaction:**
- Slaves can stretch the clock (hold SCL low)
- This is independent of arbitration
- All masters must wait for clock stretching to complete

**Real-world timing:**
- Fast-mode (400 kHz): ~2.5 µs per bit
- Standard-mode (100 kHz): ~10 µs per bit
- Arbitration adds no overhead if won
- Lost arbitration detected within one bit time

The Rust implementation provides stronger type safety and error handling, while the C implementation offers more direct hardware control. Both demonstrate the fundamental arbitration mechanism that makes I²C's multi-master capability practical and reliable.