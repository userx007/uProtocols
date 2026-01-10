# I2C Timeout Implementation

## Overview

Timeout implementation in I2C communication is crucial for preventing your application from hanging indefinitely when bus operations fail. Without timeouts, a stuck bus, non-responsive slave, or hardware fault can cause your program to wait forever, making your system unresponsive.

Common scenarios requiring timeouts include:
- **Clock stretching** - When a slave holds SCL low indefinitely
- **Missing ACK/NACK** - Slave doesn't respond to addressing
- **Bus lockup** - Physical issues preventing communication
- **Arbitration loss** - In multi-master scenarios

## Timeout Strategies

### 1. Hardware Timeouts
Some microcontrollers provide hardware timeout features that automatically abort I2C operations. However, these are often limited or absent, making software timeouts necessary.

### 2. Software Timeouts
Software timeouts monitor elapsed time during bus operations and abort if a threshold is exceeded. This can be implemented using:
- Timer peripherals
- System tick counters
- Watchdog timers (for critical applications)

## C/C++ Implementation

### Basic Timeout with System Ticks

```c
#include <stdint.h>
#include <stdbool.h>

// Assuming a system tick function (e.g., HAL_GetTick() in STM32)
extern uint32_t get_system_tick_ms(void);

#define I2C_TIMEOUT_MS 100  // 100ms timeout

// I2C status flags (example register bits)
#define I2C_SR_BUSY     (1 << 0)
#define I2C_SR_TXE      (1 << 1)  // Transmit empty
#define I2C_SR_RXNE     (1 << 2)  // Receive not empty
#define I2C_SR_BTF      (1 << 3)  // Byte transfer finished
#define I2C_SR_ADDR     (1 << 4)  // Address sent

typedef enum {
    I2C_OK = 0,
    I2C_TIMEOUT,
    I2C_ERROR
} i2c_status_t;

// Wait for a specific flag with timeout
i2c_status_t i2c_wait_flag(volatile uint32_t* status_reg, 
                           uint32_t flag, 
                           bool wait_set,
                           uint32_t timeout_ms)
{
    uint32_t start_tick = get_system_tick_ms();
    
    while (1) {
        uint32_t current_status = *status_reg;
        
        // Check if flag is in desired state
        bool flag_set = (current_status & flag) != 0;
        if (flag_set == wait_set) {
            return I2C_OK;
        }
        
        // Check timeout
        if ((get_system_tick_ms() - start_tick) >= timeout_ms) {
            return I2C_TIMEOUT;
        }
    }
}
```

### Complete I2C Write with Timeout

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example)
typedef struct {
    volatile uint32_t CR1;      // Control register 1
    volatile uint32_t CR2;      // Control register 2
    volatile uint32_t SR1;      // Status register 1
    volatile uint32_t SR2;      // Status register 2
    volatile uint32_t DR;       // Data register
} I2C_TypeDef;

#define I2C1 ((I2C_TypeDef*)0x40005400)

// Control bits
#define I2C_CR1_START   (1 << 8)
#define I2C_CR1_STOP    (1 << 9)
#define I2C_CR1_ACK     (1 << 10)

// Status bits
#define I2C_SR1_SB      (1 << 0)  // Start bit
#define I2C_SR1_ADDR    (1 << 1)  // Address sent
#define I2C_SR1_TXE     (1 << 7)  // Transmit empty
#define I2C_SR1_BTF     (1 << 2)  // Byte transfer finished

i2c_status_t i2c_write_with_timeout(I2C_TypeDef* i2c,
                                     uint8_t slave_addr,
                                     const uint8_t* data,
                                     uint16_t len,
                                     uint32_t timeout_ms)
{
    i2c_status_t status;
    uint32_t start_time = get_system_tick_ms();
    
    // Generate START condition
    i2c->CR1 |= I2C_CR1_START;
    
    // Wait for START bit to be set
    status = i2c_wait_flag(&i2c->SR1, I2C_SR1_SB, true, timeout_ms);
    if (status != I2C_OK) {
        return status;
    }
    
    // Send slave address with write bit
    i2c->DR = slave_addr << 1;  // LSB = 0 for write
    
    // Wait for address to be sent
    uint32_t remaining_time = timeout_ms - (get_system_tick_ms() - start_time);
    status = i2c_wait_flag(&i2c->SR1, I2C_SR1_ADDR, true, remaining_time);
    if (status != I2C_OK) {
        // Generate STOP on error
        i2c->CR1 |= I2C_CR1_STOP;
        return status;
    }
    
    // Clear ADDR flag by reading SR1 and SR2
    (void)i2c->SR1;
    (void)i2c->SR2;
    
    // Send data bytes
    for (uint16_t i = 0; i < len; i++) {
        // Wait for TXE (transmit register empty)
        remaining_time = timeout_ms - (get_system_tick_ms() - start_time);
        status = i2c_wait_flag(&i2c->SR1, I2C_SR1_TXE, true, remaining_time);
        if (status != I2C_OK) {
            i2c->CR1 |= I2C_CR1_STOP;
            return status;
        }
        
        // Write data
        i2c->DR = data[i];
    }
    
    // Wait for BTF (byte transfer finished)
    remaining_time = timeout_ms - (get_system_tick_ms() - start_time);
    status = i2c_wait_flag(&i2c->SR1, I2C_SR1_BTF, true, remaining_time);
    if (status != I2C_OK) {
        i2c->CR1 |= I2C_CR1_STOP;
        return status;
    }
    
    // Generate STOP condition
    i2c->CR1 |= I2C_CR1_STOP;
    
    return I2C_OK;
}
```

### C++ RAII-Style Timeout Handler

```cpp
#include <chrono>
#include <stdexcept>

class I2CTimeout {
private:
    std::chrono::steady_clock::time_point start_time;
    std::chrono::milliseconds timeout_duration;
    
public:
    I2CTimeout(uint32_t timeout_ms) 
        : start_time(std::chrono::steady_clock::now()),
          timeout_duration(timeout_ms) {}
    
    bool is_expired() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return elapsed >= timeout_duration;
    }
    
    uint32_t remaining_ms() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        );
        
        if (elapsed >= timeout_duration) {
            return 0;
        }
        return (timeout_duration - elapsed).count();
    }
    
    void check() const {
        if (is_expired()) {
            throw std::runtime_error("I2C timeout expired");
        }
    }
};

class I2CDevice {
private:
    volatile uint32_t* control_reg;
    volatile uint32_t* status_reg;
    volatile uint32_t* data_reg;
    
    void wait_flag(uint32_t flag, bool wait_set, I2CTimeout& timeout) {
        while (true) {
            timeout.check();
            
            bool flag_status = (*status_reg & flag) != 0;
            if (flag_status == wait_set) {
                return;
            }
        }
    }
    
public:
    I2CDevice(volatile uint32_t* ctrl, 
              volatile uint32_t* stat, 
              volatile uint32_t* data)
        : control_reg(ctrl), status_reg(stat), data_reg(data) {}
    
    void write(uint8_t slave_addr, const uint8_t* data, size_t len, 
               uint32_t timeout_ms = 100) {
        I2CTimeout timeout(timeout_ms);
        
        // Start condition
        *control_reg |= (1 << 8);  // START bit
        wait_flag(0x01, true, timeout);  // Wait SB
        
        // Send address
        *data_reg = slave_addr << 1;
        wait_flag(0x02, true, timeout);  // Wait ADDR
        
        // Transmit data
        for (size_t i = 0; i < len; i++) {
            wait_flag(0x80, true, timeout);  // Wait TXE
            *data_reg = data[i];
        }
        
        wait_flag(0x04, true, timeout);  // Wait BTF
        *control_reg |= (1 << 9);  // STOP bit
    }
};
```

## Rust Implementation

### Basic Timeout with Embedded HAL

```rust
use embedded_hal::blocking::i2c::{Write, WriteRead};
use core::time::Duration;

#[derive(Debug)]
pub enum I2cError {
    Timeout,
    Nack,
    BusError,
}

/// Trait for getting current time (implemented by HAL)
pub trait Clock {
    fn now_ms(&self) -> u32;
}

/// Timeout tracker
pub struct Timeout {
    start_ms: u32,
    duration_ms: u32,
}

impl Timeout {
    pub fn new(duration_ms: u32, clock: &impl Clock) -> Self {
        Self {
            start_ms: clock.now_ms(),
            duration_ms,
        }
    }
    
    pub fn is_expired(&self, clock: &impl Clock) -> bool {
        let elapsed = clock.now_ms().wrapping_sub(self.start_ms);
        elapsed >= self.duration_ms
    }
    
    pub fn check(&self, clock: &impl Clock) -> Result<(), I2cError> {
        if self.is_expired(clock) {
            Err(I2cError::Timeout)
        } else {
            Ok(())
        }
    }
    
    pub fn remaining_ms(&self, clock: &impl Clock) -> u32 {
        let elapsed = clock.now_ms().wrapping_sub(self.start_ms);
        self.duration_ms.saturating_sub(elapsed)
    }
}

/// Wait for a flag with timeout
fn wait_flag(
    status_reg: *const u32,
    flag: u32,
    wait_set: bool,
    timeout: &Timeout,
    clock: &impl Clock,
) -> Result<(), I2cError> {
    loop {
        timeout.check(clock)?;
        
        let status = unsafe { core::ptr::read_volatile(status_reg) };
        let flag_set = (status & flag) != 0;
        
        if flag_set == wait_set {
            return Ok(());
        }
    }
}
```

### Complete I2C Driver with Timeouts

```rust
use core::ptr::{read_volatile, write_volatile};

// I2C register definitions
#[repr(C)]
struct I2cRegisters {
    cr1: u32,      // Control register 1
    cr2: u32,      // Control register 2
    sr1: u32,      // Status register 1
    sr2: u32,      // Status register 2
    dr: u32,       // Data register
}

// Control bits
const CR1_START: u32 = 1 << 8;
const CR1_STOP: u32 = 1 << 9;
const CR1_ACK: u32 = 1 << 10;

// Status bits
const SR1_SB: u32 = 1 << 0;      // Start bit
const SR1_ADDR: u32 = 1 << 1;    // Address sent
const SR1_TXE: u32 = 1 << 7;     // Transmit empty
const SR1_RXNE: u32 = 1 << 6;    // Receive not empty
const SR1_BTF: u32 = 1 << 2;     // Byte transfer finished

pub struct I2cDriver<C: Clock> {
    registers: *mut I2cRegisters,
    clock: C,
    default_timeout_ms: u32,
}

impl<C: Clock> I2cDriver<C> {
    pub fn new(base_addr: usize, clock: C, timeout_ms: u32) -> Self {
        Self {
            registers: base_addr as *mut I2cRegisters,
            clock,
            default_timeout_ms: timeout_ms,
        }
    }
    
    fn wait_flag(&self, flag: u32, wait_set: bool, timeout: &Timeout) 
        -> Result<(), I2cError> 
    {
        loop {
            timeout.check(&self.clock)?;
            
            let status = unsafe { 
                read_volatile(&(*self.registers).sr1) 
            };
            let flag_set = (status & flag) != 0;
            
            if flag_set == wait_set {
                return Ok(());
            }
        }
    }
    
    fn generate_start(&mut self, timeout: &Timeout) -> Result<(), I2cError> {
        unsafe {
            let cr1 = read_volatile(&(*self.registers).cr1);
            write_volatile(&mut (*self.registers).cr1, cr1 | CR1_START);
        }
        
        self.wait_flag(SR1_SB, true, timeout)
    }
    
    fn send_address(&mut self, addr: u8, read: bool, timeout: &Timeout) 
        -> Result<(), I2cError> 
    {
        let addr_byte = (addr << 1) | if read { 1 } else { 0 };
        
        unsafe {
            write_volatile(&mut (*self.registers).dr, addr_byte as u32);
        }
        
        self.wait_flag(SR1_ADDR, true, timeout)?;
        
        // Clear ADDR flag
        unsafe {
            let _ = read_volatile(&(*self.registers).sr1);
            let _ = read_volatile(&(*self.registers).sr2);
        }
        
        Ok(())
    }
    
    fn generate_stop(&mut self) {
        unsafe {
            let cr1 = read_volatile(&(*self.registers).cr1);
            write_volatile(&mut (*self.registers).cr1, cr1 | CR1_STOP);
        }
    }
    
    pub fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        self.write_with_timeout(addr, data, self.default_timeout_ms)
    }
    
    pub fn write_with_timeout(&mut self, addr: u8, data: &[u8], timeout_ms: u32) 
        -> Result<(), I2cError> 
    {
        let timeout = Timeout::new(timeout_ms, &self.clock);
        
        // Generate START
        self.generate_start(&timeout)?;
        
        // Send address
        if let Err(e) = self.send_address(addr, false, &timeout) {
            self.generate_stop();
            return Err(e);
        }
        
        // Send data bytes
        for &byte in data {
            // Wait for TXE
            if let Err(e) = self.wait_flag(SR1_TXE, true, &timeout) {
                self.generate_stop();
                return Err(e);
            }
            
            unsafe {
                write_volatile(&mut (*self.registers).dr, byte as u32);
            }
        }
        
        // Wait for BTF
        if let Err(e) = self.wait_flag(SR1_BTF, true, &timeout) {
            self.generate_stop();
            return Err(e);
        }
        
        self.generate_stop();
        Ok(())
    }
    
    pub fn read_with_timeout(&mut self, addr: u8, buffer: &mut [u8], timeout_ms: u32) 
        -> Result<(), I2cError> 
    {
        let timeout = Timeout::new(timeout_ms, &self.clock);
        
        if buffer.is_empty() {
            return Ok(());
        }
        
        // Generate START
        self.generate_start(&timeout)?;
        
        // Send address (read mode)
        if let Err(e) = self.send_address(addr, true, &timeout) {
            self.generate_stop();
            return Err(e);
        }
        
        // Enable ACK
        unsafe {
            let cr1 = read_volatile(&(*self.registers).cr1);
            write_volatile(&mut (*self.registers).cr1, cr1 | CR1_ACK);
        }
        
        for i in 0..buffer.len() {
            // Disable ACK before last byte
            if i == buffer.len() - 1 {
                unsafe {
                    let cr1 = read_volatile(&(*self.registers).cr1);
                    write_volatile(&mut (*self.registers).cr1, cr1 & !CR1_ACK);
                }
            }
            
            // Wait for RXNE
            if let Err(e) = self.wait_flag(SR1_RXNE, true, &timeout) {
                self.generate_stop();
                return Err(e);
            }
            
            // Read data
            buffer[i] = unsafe {
                read_volatile(&(*self.registers).dr) as u8
            };
        }
        
        self.generate_stop();
        Ok(())
    }
}

// Implement embedded-hal traits
impl<C: Clock> Write for I2cDriver<C> {
    type Error = I2cError;
    
    fn write(&mut self, addr: u8, bytes: &[u8]) -> Result<(), Self::Error> {
        self.write(addr, bytes)
    }
}
```

### Usage Example

```rust
// Example clock implementation using a system timer
struct SystemClock;

impl Clock for SystemClock {
    fn now_ms(&self) -> u32 {
        // Get system tick count (implementation depends on platform)
        unsafe { SYSTEM_TICK_MS }
    }
}

static mut SYSTEM_TICK_MS: u32 = 0;

// In your application
fn main() -> ! {
    let clock = SystemClock;
    let mut i2c = I2cDriver::new(0x4000_5400, clock, 100);
    
    let sensor_addr = 0x48;
    let config_data = [0x01, 0xC0];  // Register 0x01, value 0xC0
    
    match i2c.write(sensor_addr, &config_data) {
        Ok(_) => {
            // Success
        }
        Err(I2cError::Timeout) => {
            // Handle timeout - sensor not responding
        }
        Err(e) => {
            // Handle other errors
        }
    }
    
    loop {}
}
```

## Best Practices

1. **Choose appropriate timeout values**: Too short causes false failures; too long delays error detection. Typical values: 10-100ms for most operations.

2. **Always cleanup on timeout**: Generate STOP condition and reset peripheral state.

3. **Use dynamic timeouts**: Adjust based on data length and bus speed.

4. **Track remaining time**: When multiple operations share a timeout, subtract elapsed time.

5. **Log timeout events**: Essential for debugging intermittent issues.

6. **Consider retry logic**: Transient issues may succeed on retry.

7. **Watchdog integration**: For critical systems, coordinate with watchdog timers.

The timeout implementation is fundamental to robust I2C communication, preventing system hangs and enabling graceful error recovery.