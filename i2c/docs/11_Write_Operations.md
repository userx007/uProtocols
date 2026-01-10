# I²C Write Operations: Detailed Guide

I'll create a comprehensive guide covering master-to-slave data transmission patterns in I²C, with examples in C/C++ and Rust.I've created a comprehensive guide on I²C write operations covering:

## Key Topics Covered:

1. **Protocol Fundamentals** - The basic write sequence with START, address, data, ACK, and STOP phases

2. **Four Main Write Patterns**:
   - Single byte write (simple commands)
   - Register write (address + data, most common)
   - Multi-byte write (burst transfers)
   - EEPROM page write (with page boundary handling)

3. **Complete Code Examples** in both C/C++ and Rust for:
   - Low-level bit-banging implementation
   - Hardware abstraction layer usage
   - Device driver patterns
   - Error handling and retries

4. **Best Practices**:
   - ACK verification
   - Bus recovery mechanisms
   - Timeout protection
   - Timing considerations (Standard vs Fast mode)

The examples progress from simple bit-level operations to practical device drivers, showing how to implement robust I²C write operations in both bare-metal C and embedded Rust environments. Each pattern includes real-world use cases like configuring sensors, controlling LEDs, and writing to EEPROM.

---
# I²C Write Operations: Master-to-Slave Data Transmission

## Overview

Write operations in I²C are fundamental data transmission patterns where the master device sends data to a slave device. These operations form the basis for configuring peripherals, sending commands, and writing data to slave devices like sensors, EEPROMs, displays, and other I²C peripherals.

## Write Operation Protocol

### Basic Write Sequence

1. **START Condition**: Master generates a START condition
2. **Address Phase**: Master sends 7-bit slave address + Write bit (0)
3. **ACK from Slave**: Slave acknowledges address
4. **Data Phase**: Master sends one or more data bytes
5. **ACK from Slave**: Slave acknowledges each byte
6. **STOP Condition**: Master generates a STOP condition

```
START | ADDR+W | ACK | DATA1 | ACK | DATA2 | ACK | ... | STOP
  M       M      S      M      S      M      S          M
```

Where: M = Master, S = Slave, W = Write bit (0)

## Write Operation Patterns

### 1. Single Byte Write

The simplest form - writing one byte to a slave device.

**Use cases:**
- Sending simple commands
- Writing to single-byte registers
- Basic device control

**C/C++ Example (bit-banging):**

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction (platform-specific)
void i2c_set_sda(bool state);
void i2c_set_scl(bool state);
bool i2c_read_sda(void);
void i2c_delay(void);

// Generate START condition
void i2c_start(void) {
    i2c_set_sda(true);
    i2c_delay();
    i2c_set_scl(true);
    i2c_delay();
    i2c_set_sda(false);  // SDA falls while SCL high
    i2c_delay();
    i2c_set_scl(false);
    i2c_delay();
}

// Generate STOP condition
void i2c_stop(void) {
    i2c_set_sda(false);
    i2c_delay();
    i2c_set_scl(true);
    i2c_delay();
    i2c_set_sda(true);   // SDA rises while SCL high
    i2c_delay();
}

// Write a single bit
void i2c_write_bit(bool bit) {
    i2c_set_sda(bit);
    i2c_delay();
    i2c_set_scl(true);
    i2c_delay();
    i2c_set_scl(false);
    i2c_delay();
}

// Read ACK/NACK bit
bool i2c_read_ack(void) {
    bool ack;
    i2c_set_sda(true);  // Release SDA
    i2c_delay();
    i2c_set_scl(true);
    i2c_delay();
    ack = !i2c_read_sda();  // ACK is active low
    i2c_set_scl(false);
    i2c_delay();
    return ack;
}

// Write a byte and return ACK status
bool i2c_write_byte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit((data >> i) & 0x01);
    }
    return i2c_read_ack();
}

// Single byte write operation
bool i2c_write_single(uint8_t slave_addr, uint8_t data) {
    i2c_start();
    
    // Send address with write bit (0)
    if (!i2c_write_byte(slave_addr << 1)) {
        i2c_stop();
        return false;  // No ACK from slave
    }
    
    // Send data byte
    if (!i2c_write_byte(data)) {
        i2c_stop();
        return false;  // No ACK from slave
    }
    
    i2c_stop();
    return true;
}
```

**Rust Example (using embedded-hal):**

```rust
use embedded_hal::i2c::I2c;

/// Single byte write to I²C device
pub fn write_single_byte<I2C, E>(
    i2c: &mut I2C,
    slave_addr: u8,
    data: u8,
) -> Result<(), E>
where
    I2C: I2c<Error = E>,
{
    i2c.write(slave_addr, &[data])
}

// Example usage with a specific device
pub struct SimpleDevice<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> SimpleDevice<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    pub fn send_command(&mut self, command: u8) -> Result<(), E> {
        self.i2c.write(self.address, &[command])
    }
}
```

### 2. Register Write (Address + Data)

Most common pattern - writing data to a specific register address.

**Use cases:**
- Configuring sensor registers
- Writing to specific memory locations
- Setting device parameters

**C/C++ Example:**

```c
// Write to a specific register
bool i2c_write_register(uint8_t slave_addr, uint8_t reg_addr, uint8_t data) {
    i2c_start();
    
    // Send slave address with write bit
    if (!i2c_write_byte(slave_addr << 1)) {
        i2c_stop();
        return false;
    }
    
    // Send register address
    if (!i2c_write_byte(reg_addr)) {
        i2c_stop();
        return false;
    }
    
    // Send data
    if (!i2c_write_byte(data)) {
        i2c_stop();
        return false;
    }
    
    i2c_stop();
    return true;
}

// Example: Configure an accelerometer
void configure_accelerometer(void) {
    const uint8_t ACCEL_ADDR = 0x1D;
    const uint8_t CTRL_REG1 = 0x20;
    const uint8_t CONFIG_VALUE = 0x57;  // 100Hz, all axes enabled
    
    i2c_write_register(ACCEL_ADDR, CTRL_REG1, CONFIG_VALUE);
}
```

**Rust Example:**

```rust
use embedded_hal::i2c::I2c;

/// Write to a register at a specific address
pub fn write_register<I2C, E>(
    i2c: &mut I2C,
    slave_addr: u8,
    reg_addr: u8,
    data: u8,
) -> Result<(), E>
where
    I2C: I2c<Error = E>,
{
    i2c.write(slave_addr, &[reg_addr, data])
}

// Device driver example
pub struct Accelerometer<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> Accelerometer<I2C>
where
    I2C: I2c<Error = E>,
{
    const CTRL_REG1: u8 = 0x20;
    
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    pub fn configure(&mut self, config: u8) -> Result<(), E> {
        self.i2c.write(self.address, &[Self::CTRL_REG1, config])
    }
    
    pub fn set_power_mode(&mut self, enabled: bool) -> Result<(), E> {
        let config = if enabled { 0x57 } else { 0x00 };
        self.configure(config)
    }
}
```

### 3. Multi-Byte Write

Writing multiple consecutive bytes to a device.

**Use cases:**
- Writing to consecutive registers
- Bulk data transfer
- EEPROM/Flash writes
- Display buffer updates

**C/C++ Example:**

```c
// Write multiple bytes
bool i2c_write_bytes(uint8_t slave_addr, const uint8_t *data, size_t length) {
    if (length == 0) return false;
    
    i2c_start();
    
    // Send slave address with write bit
    if (!i2c_write_byte(slave_addr << 1)) {
        i2c_stop();
        return false;
    }
    
    // Send all data bytes
    for (size_t i = 0; i < length; i++) {
        if (!i2c_write_byte(data[i])) {
            i2c_stop();
            return false;
        }
    }
    
    i2c_stop();
    return true;
}

// Write multiple bytes to consecutive registers
bool i2c_write_register_burst(uint8_t slave_addr, uint8_t start_reg, 
                               const uint8_t *data, size_t length) {
    i2c_start();
    
    if (!i2c_write_byte(slave_addr << 1)) {
        i2c_stop();
        return false;
    }
    
    // Send starting register address
    if (!i2c_write_byte(start_reg)) {
        i2c_stop();
        return false;
    }
    
    // Send all data bytes
    for (size_t i = 0; i < length; i++) {
        if (!i2c_write_byte(data[i])) {
            i2c_stop();
            return false;
        }
    }
    
    i2c_stop();
    return true;
}

// Example: Write RGB color to LED controller
void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t LED_ADDR = 0x40;
    const uint8_t COLOR_REG = 0x10;
    uint8_t colors[3] = {r, g, b};
    
    i2c_write_register_burst(LED_ADDR, COLOR_REG, colors, 3);
}
```

**Rust Example:**

```rust
use embedded_hal::i2c::I2c;

/// Write multiple bytes to consecutive registers
pub fn write_burst<I2C, E>(
    i2c: &mut I2C,
    slave_addr: u8,
    start_reg: u8,
    data: &[u8],
) -> Result<(), E>
where
    I2C: I2c<Error = E>,
{
    let mut buffer = Vec::with_capacity(data.len() + 1);
    buffer.push(start_reg);
    buffer.extend_from_slice(data);
    i2c.write(slave_addr, &buffer)
}

// For embedded no_std environments
pub fn write_burst_no_alloc<I2C, E>(
    i2c: &mut I2C,
    slave_addr: u8,
    start_reg: u8,
    data: &[u8],
) -> Result<(), E>
where
    I2C: I2c<Error = E>,
{
    // Use a fixed-size buffer
    let mut buffer = [0u8; 32];
    if data.len() + 1 > buffer.len() {
        panic!("Data too large for buffer");
    }
    
    buffer[0] = start_reg;
    buffer[1..=data.len()].copy_from_slice(data);
    i2c.write(slave_addr, &buffer[..=data.len()])
}

// LED controller driver
pub struct RgbLed<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> RgbLed<I2C>
where
    I2C: I2c<Error = E>,
{
    const COLOR_REG: u8 = 0x10;
    
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    pub fn set_color(&mut self, r: u8, g: u8, b: u8) -> Result<(), E> {
        let colors = [r, g, b];
        write_burst_no_alloc(&mut self.i2c, self.address, Self::COLOR_REG, &colors)
    }
}
```

### 4. EEPROM Write with Page Mode

Writing to EEPROM devices often requires special handling for page boundaries.

**C/C++ Example:**

```c
#include <string.h>

#define EEPROM_PAGE_SIZE 32
#define EEPROM_WRITE_DELAY_MS 5

// Write to EEPROM with automatic page handling
bool eeprom_write(uint8_t slave_addr, uint16_t mem_addr, 
                  const uint8_t *data, size_t length) {
    size_t written = 0;
    
    while (written < length) {
        // Calculate bytes remaining in current page
        size_t page_offset = (mem_addr + written) % EEPROM_PAGE_SIZE;
        size_t page_remaining = EEPROM_PAGE_SIZE - page_offset;
        size_t to_write = (length - written < page_remaining) ? 
                          (length - written) : page_remaining;
        
        i2c_start();
        
        // Send slave address
        if (!i2c_write_byte(slave_addr << 1)) {
            i2c_stop();
            return false;
        }
        
        // Send 16-bit memory address (MSB first for most EEPROMs)
        uint16_t current_addr = mem_addr + written;
        if (!i2c_write_byte((current_addr >> 8) & 0xFF)) {
            i2c_stop();
            return false;
        }
        if (!i2c_write_byte(current_addr & 0xFF)) {
            i2c_stop();
            return false;
        }
        
        // Write page data
        for (size_t i = 0; i < to_write; i++) {
            if (!i2c_write_byte(data[written + i])) {
                i2c_stop();
                return false;
            }
        }
        
        i2c_stop();
        
        // Wait for write cycle to complete
        delay_ms(EEPROM_WRITE_DELAY_MS);
        
        written += to_write;
    }
    
    return true;
}
```

**Rust Example:**

```rust
use embedded_hal::i2c::I2c;
use embedded_hal::delay::DelayNs;

pub struct Eeprom<I2C, DELAY> {
    i2c: I2C,
    delay: DELAY,
    address: u8,
    page_size: usize,
}

impl<I2C, DELAY, E> Eeprom<I2C, DELAY>
where
    I2C: I2c<Error = E>,
    DELAY: DelayNs,
{
    pub fn new(i2c: I2C, delay: DELAY, address: u8, page_size: usize) -> Self {
        Self {
            i2c,
            delay,
            address,
            page_size,
        }
    }
    
    pub fn write(&mut self, mem_addr: u16, data: &[u8]) -> Result<(), E> {
        let mut written = 0;
        
        while written < data.len() {
            // Calculate page boundary
            let current_addr = mem_addr + written as u16;
            let page_offset = (current_addr as usize) % self.page_size;
            let page_remaining = self.page_size - page_offset;
            let to_write = core::cmp::min(data.len() - written, page_remaining);
            
            // Prepare write buffer with address + data
            let mut buffer = [0u8; 34]; // 2 bytes addr + up to 32 bytes data
            buffer[0] = (current_addr >> 8) as u8;
            buffer[1] = current_addr as u8;
            buffer[2..2 + to_write].copy_from_slice(&data[written..written + to_write]);
            
            // Write page
            self.i2c.write(self.address, &buffer[..2 + to_write])?;
            
            // Wait for write cycle
            self.delay.delay_ms(5);
            
            written += to_write;
        }
        
        Ok(())
    }
}
```

## Error Handling and Best Practices

### 1. ACK Checking

Always verify that the slave acknowledges each byte:

```c
bool i2c_write_with_retry(uint8_t slave_addr, uint8_t reg_addr, 
                          uint8_t data, int max_retries) {
    for (int retry = 0; retry < max_retries; retry++) {
        if (i2c_write_register(slave_addr, reg_addr, data)) {
            return true;
        }
        delay_ms(10);  // Wait before retry
    }
    return false;
}
```

### 2. Bus Recovery

Implement bus recovery for hung slaves:

```c
void i2c_bus_reset(void) {
    // Generate 9 clock pulses to reset any confused slaves
    for (int i = 0; i < 9; i++) {
        i2c_set_scl(true);
        i2c_delay();
        i2c_set_scl(false);
        i2c_delay();
    }
    
    // Generate STOP condition
    i2c_stop();
}
```

### 3. Timeout Protection

**Rust Example:**

```rust
use embedded_hal::i2c::I2c;

pub struct I2cWithTimeout<I2C> {
    i2c: I2C,
    timeout_ms: u32,
}

impl<I2C, E> I2cWithTimeout<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn write_with_timeout(
        &mut self,
        addr: u8,
        data: &[u8],
    ) -> Result<(), I2cError<E>> {
        // Implement timeout logic here
        self.i2c.write(addr, data).map_err(I2cError::I2c)
    }
}

pub enum I2cError<E> {
    I2c(E),
    Timeout,
}
```

## Timing Considerations

### Standard vs Fast Mode

| Parameter | Standard Mode | Fast Mode |
|-----------|---------------|-----------|
| Clock Speed | 100 kHz | 400 kHz |
| Low Period | 4.7 μs | 1.3 μs |
| High Period | 4.0 μs | 0.6 μs |
| Setup Time (SDA) | 250 ns | 100 ns |
| Hold Time (SDA) | 0 ns | 0 ns |

**C Implementation with Timing:**

```c
// Timing constants for 100kHz I²C
#define I2C_DELAY_US 5

void i2c_delay(void) {
    delay_us(I2C_DELAY_US);
}
```

## Summary

Write operations in I²C follow these key patterns:

1. **Single Byte Write**: Simple command/data transmission
2. **Register Write**: Most common pattern for peripheral control
3. **Multi-Byte Write**: Burst transfers for efficiency
4. **Page Write**: Special handling for memory devices

Always implement:
- Proper START/STOP conditions
- ACK checking after each byte
- Error handling and retries
- Timeout protection
- Bus recovery mechanisms

The choice between bit-banging and hardware I²C depends on your microcontroller capabilities, timing requirements, and code complexity preferences.