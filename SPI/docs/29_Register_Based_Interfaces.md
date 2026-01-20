# Register-based Interfaces: SPI Device Register Access

## Overview

Register-based interfaces are a fundamental abstraction layer used in embedded systems to interact with peripheral devices connected via SPI (Serial Peripheral Interface). These interfaces provide a structured way to read from and write to specific memory-mapped registers within devices like sensors, ADCs, DACs, display controllers, and other integrated circuits.

## SPI Addressing Schemes

SPI devices typically use one of several addressing schemes to access their internal registers:

### 1. **Single-Byte Address Scheme**
- Most common approach
- First byte contains register address
- Read/Write bit (R/W) typically in MSB or LSB
- Subsequent bytes are data

### 2. **Multi-Byte Address Scheme**
- Used for devices with >256 registers
- 16-bit or 24-bit addressing
- Common in flash memory and large EEPROMs

### 3. **Auto-Increment Mode**
- After initial address, subsequent bytes access sequential registers
- Efficient for burst reads/writes

## Common SPI Register Access Patterns

**Read Operation:**
```
CS LOW → [READ_CMD | ADDRESS] → [DUMMY/DATA] → [DATA...] → CS HIGH
```

**Write Operation:**
```
CS LOW → [WRITE_CMD | ADDRESS] → [DATA] → [DATA...] → CS HIGH
```

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction layer functions (platform-specific)
extern void spi_chip_select(bool state);
extern uint8_t spi_transfer(uint8_t data);
extern void spi_transfer_bulk(uint8_t *tx_buf, uint8_t *rx_buf, size_t len);

// Register address bit manipulation
#define SPI_READ_BIT    0x80  // MSB = 1 for read
#define SPI_WRITE_BIT   0x00  // MSB = 0 for write
#define SPI_ADDR_MASK   0x7F  // Lower 7 bits for address

/**
 * Read a single register from an SPI device
 * @param reg_addr Register address (0-127)
 * @return Register value
 */
uint8_t spi_read_register(uint8_t reg_addr) {
    uint8_t value;
    
    spi_chip_select(true);  // Assert CS (active low)
    
    // Send read command with address
    spi_transfer(SPI_READ_BIT | (reg_addr & SPI_ADDR_MASK));
    
    // Read the register value
    value = spi_transfer(0x00);  // Send dummy byte
    
    spi_chip_select(false);  // Deassert CS
    
    return value;
}

/**
 * Write a single register to an SPI device
 * @param reg_addr Register address
 * @param value Value to write
 */
void spi_write_register(uint8_t reg_addr, uint8_t value) {
    spi_chip_select(true);
    
    // Send write command with address
    spi_transfer(SPI_WRITE_BIT | (reg_addr & SPI_ADDR_MASK));
    
    // Write the value
    spi_transfer(value);
    
    spi_chip_select(false);
}

/**
 * Read multiple consecutive registers (burst read)
 * @param start_addr Starting register address
 * @param buffer Buffer to store read data
 * @param length Number of registers to read
 */
void spi_read_registers(uint8_t start_addr, uint8_t *buffer, size_t length) {
    spi_chip_select(true);
    
    // Send read command with auto-increment
    spi_transfer(SPI_READ_BIT | (start_addr & SPI_ADDR_MASK));
    
    // Read multiple bytes
    for (size_t i = 0; i < length; i++) {
        buffer[i] = spi_transfer(0x00);
    }
    
    spi_chip_select(false);
}

/**
 * Write multiple consecutive registers (burst write)
 * @param start_addr Starting register address
 * @param data Data buffer to write
 * @param length Number of registers to write
 */
void spi_write_registers(uint8_t start_addr, const uint8_t *data, size_t length) {
    spi_chip_select(true);
    
    // Send write command
    spi_transfer(SPI_WRITE_BIT | (start_addr & SPI_ADDR_MASK));
    
    // Write multiple bytes
    for (size_t i = 0; i < length; i++) {
        spi_transfer(data[i]);
    }
    
    spi_chip_select(false);
}

/**
 * Modify specific bits in a register (read-modify-write)
 * @param reg_addr Register address
 * @param mask Bit mask for modification
 * @param value New value for masked bits
 */
void spi_modify_register(uint8_t reg_addr, uint8_t mask, uint8_t value) {
    uint8_t current = spi_read_register(reg_addr);
    uint8_t modified = (current & ~mask) | (value & mask);
    spi_write_register(reg_addr, modified);
}

// Example: Device-specific register interface
#define ACCEL_REG_CTRL1     0x20
#define ACCEL_REG_CTRL2     0x21
#define ACCEL_REG_STATUS    0x27
#define ACCEL_REG_OUT_X_L   0x28
#define ACCEL_REG_OUT_X_H   0x29

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} accel_data_t;

/**
 * Initialize accelerometer via SPI registers
 */
void accel_init(void) {
    // Configure control register 1: Enable device, set data rate
    spi_write_register(ACCEL_REG_CTRL1, 0x47);  // 50Hz, normal mode
    
    // Configure control register 2: Set full-scale range
    spi_write_register(ACCEL_REG_CTRL2, 0x00);  // ±2g
}

/**
 * Read accelerometer data from consecutive registers
 */
accel_data_t accel_read_data(void) {
    uint8_t raw_data[6];
    accel_data_t result;
    
    // Read 6 consecutive registers (X_L, X_H, Y_L, Y_H, Z_L, Z_H)
    spi_read_registers(ACCEL_REG_OUT_X_L, raw_data, 6);
    
    // Combine low and high bytes
    result.x = (int16_t)((raw_data[1] << 8) | raw_data[0]);
    result.y = (int16_t)((raw_data[3] << 8) | raw_data[2]);
    result.z = (int16_t)((raw_data[5] << 8) | raw_data[4]);
    
    return result;
}
```

### Rust Implementation

```rust
// traits.rs - Define SPI trait abstraction
pub trait SpiTransfer {
    type Error;
    
    fn transfer<'w>(&mut self, words: &'w mut [u8]) -> Result<&'w [u8], Self::Error>;
    fn write(&mut self, words: &[u8]) -> Result<(), Self::Error>;
}

pub trait OutputPin {
    type Error;
    
    fn set_low(&mut self) -> Result<(), Self::Error>;
    fn set_high(&mut self) -> Result<(), Self::Error>;
}

// register_device.rs - Generic register-based SPI device
use core::marker::PhantomData;

/// Register access mode marker traits
pub trait ReadOnly {}
pub trait WriteOnly {}
pub trait ReadWrite {}

/// Generic register wrapper with compile-time access control
pub struct Register<MODE> {
    address: u8,
    _mode: PhantomData<MODE>,
}

impl<MODE> Register<MODE> {
    pub const fn new(address: u8) -> Self {
        Self {
            address,
            _mode: PhantomData,
        }
    }
    
    pub fn address(&self) -> u8 {
        self.address
    }
}

/// SPI register interface with configurable addressing
pub struct SpiRegisterDevice<SPI, CS> {
    spi: SPI,
    cs: CS,
    read_bit: u8,
    write_bit: u8,
}

impl<SPI, CS, E> SpiRegisterDevice<SPI, CS>
where
    SPI: SpiTransfer<Error = E>,
    CS: OutputPin<Error = E>,
{
    /// Create new SPI register device
    /// 
    /// # Arguments
    /// * `spi` - SPI peripheral
    /// * `cs` - Chip select pin
    /// * `read_bit` - Bit pattern for read operations (e.g., 0x80)
    /// * `write_bit` - Bit pattern for write operations (e.g., 0x00)
    pub fn new(spi: SPI, cs: CS, read_bit: u8, write_bit: u8) -> Self {
        Self {
            spi,
            cs,
            read_bit,
            write_bit,
        }
    }
    
    /// Read a single register
    pub fn read_register(&mut self, reg: &Register<impl ReadOnly>) -> Result<u8, E> {
        let mut buffer = [self.read_bit | reg.address(), 0x00];
        
        self.cs.set_low()?;
        let result = self.spi.transfer(&mut buffer)?;
        self.cs.set_high()?;
        
        Ok(result[1])
    }
    
    /// Write a single register
    pub fn write_register(&mut self, reg: &Register<impl WriteOnly>, value: u8) -> Result<(), E> {
        let buffer = [self.write_bit | reg.address(), value];
        
        self.cs.set_low()?;
        self.spi.write(&buffer)?;
        self.cs.set_high()?;
        
        Ok(())
    }
    
    /// Read multiple consecutive registers
    pub fn read_registers(&mut self, start_addr: u8, buffer: &mut [u8]) -> Result<(), E> {
        self.cs.set_low()?;
        
        // Send read command
        self.spi.write(&[self.read_bit | start_addr])?;
        
        // Read data
        self.spi.transfer(buffer)?;
        
        self.cs.set_high()?;
        
        Ok(())
    }
    
    /// Write multiple consecutive registers
    pub fn write_registers(&mut self, start_addr: u8, data: &[u8]) -> Result<(), E> {
        self.cs.set_low()?;
        
        // Send write command
        self.spi.write(&[self.write_bit | start_addr])?;
        
        // Write data
        self.spi.write(data)?;
        
        self.cs.set_high()?;
        
        Ok(())
    }
    
    /// Read-modify-write operation
    pub fn modify_register<MODE: ReadOnly + WriteOnly>(
        &mut self,
        reg: &Register<MODE>,
        mask: u8,
        value: u8,
    ) -> Result<(), E> {
        let current = self.read_register(reg)?;
        let modified = (current & !mask) | (value & mask);
        self.write_register(reg, modified)?;
        Ok(())
    }
}

// device_example.rs - Concrete device implementation
pub struct Accelerometer<SPI, CS> {
    device: SpiRegisterDevice<SPI, CS>,
}

// Define register map with type safety
pub mod registers {
    use super::*;
    
    // Read-only registers
    impl ReadOnly for StatusReg {}
    pub struct StatusReg;
    pub const STATUS: Register<StatusReg> = Register::new(0x27);
    
    // Write-only registers
    impl WriteOnly for CtrlReg1 {}
    pub struct CtrlReg1;
    pub const CTRL1: Register<CtrlReg1> = Register::new(0x20);
    
    // Read-write registers
    impl ReadOnly for CtrlReg2 {}
    impl WriteOnly for CtrlReg2 {}
    pub struct CtrlReg2;
    pub const CTRL2: Register<CtrlReg2> = Register::new(0x21);
}

#[derive(Debug, Clone, Copy)]
pub struct AccelData {
    pub x: i16,
    pub y: i16,
    pub z: i16,
}

impl<SPI, CS, E> Accelerometer<SPI, CS>
where
    SPI: SpiTransfer<Error = E>,
    CS: OutputPin<Error = E>,
{
    pub fn new(spi: SPI, cs: CS) -> Self {
        Self {
            device: SpiRegisterDevice::new(spi, cs, 0x80, 0x00),
        }
    }
    
    /// Initialize the accelerometer
    pub fn init(&mut self) -> Result<(), E> {
        // Enable device, set 50Hz data rate
        self.device.write_register(&registers::CTRL1, 0x47)?;
        
        // Set ±2g full-scale range
        self.device.write_register(&registers::CTRL2, 0x00)?;
        
        Ok(())
    }
    
    /// Read accelerometer data
    pub fn read_data(&mut self) -> Result<AccelData, E> {
        let mut raw_data = [0u8; 6];
        
        // Read 6 consecutive registers starting at OUT_X_L (0x28)
        self.device.read_registers(0x28, &mut raw_data)?;
        
        Ok(AccelData {
            x: i16::from_le_bytes([raw_data[0], raw_data[1]]),
            y: i16::from_le_bytes([raw_data[2], raw_data[3]]),
            z: i16::from_le_bytes([raw_data[4], raw_data[5]]),
        })
    }
    
    /// Check if new data is available
    pub fn data_ready(&mut self) -> Result<bool, E> {
        let status = self.device.read_register(&registers::STATUS)?;
        Ok((status & 0x08) != 0)  // Check data ready bit
    }
    
    /// Enable/disable specific axis
    pub fn set_axis_enable(&mut self, x: bool, y: bool, z: bool) -> Result<(), E> {
        let mask = 0x07;  // Lower 3 bits control axes
        let value = ((z as u8) << 2) | ((y as u8) << 1) | (x as u8);
        
        self.device.modify_register(&registers::CTRL2, mask, value)?;
        
        Ok(())
    }
}

// Usage example
#[cfg(feature = "example")]
pub fn example_usage() {
    use embedded_hal::spi::MODE_3;
    
    // Platform-specific SPI and GPIO initialization would go here
    let spi = /* initialize SPI */;
    let cs_pin = /* initialize GPIO pin */;
    
    let mut accel = Accelerometer::new(spi, cs_pin);
    
    // Initialize device
    accel.init().expect("Failed to initialize accelerometer");
    
    // Wait for data
    while !accel.data_ready().unwrap() {
        // busy wait or sleep
    }
    
    // Read data
    let data = accel.read_data().expect("Failed to read data");
    println!("Accel: X={}, Y={}, Z={}", data.x, data.y, data.z);
    
    // Configure device
    accel.set_axis_enable(true, true, false).expect("Failed to configure axes");
}
```

## Key Concepts

### 1. **Address Encoding**
- Read/Write bit typically in MSB (0x80 for read, 0x00 for write)
- Some devices use LSB or separate command bytes
- Multi-byte addressing for large register spaces

### 2. **Chip Select Management**
- CS must be asserted before transaction
- Held low for entire register operation
- Deasserted after complete read/write

### 3. **Burst Operations**
- Auto-increment addressing reduces overhead
- Efficient for reading sensor data (X, Y, Z axes)
- Critical for high-throughput applications

### 4. **Read-Modify-Write**
- Essential for bit-field manipulation
- Preserves unrelated bits in register
- Atomic at software level (requires critical section in multi-threaded environments)

### 5. **Type Safety (Rust)**
- Compile-time enforcement of read/write permissions
- Phantom types prevent misuse
- Zero-cost abstractions

## Summary

Register-based interfaces provide a structured, efficient method for interacting with SPI devices by abstracting hardware registers into software-accessible memory locations. The addressing scheme—typically a command byte containing read/write flags and register address followed by data bytes—enables both single and burst operations. 

C/C++ implementations focus on direct hardware manipulation with function-based abstractions, offering maximum control and minimal overhead. Rust implementations leverage the type system for compile-time safety, preventing common errors like writing to read-only registers while maintaining zero-cost abstractions.

Key advantages include:
- **Structured access**: Clear mapping between software and hardware
- **Efficiency**: Burst operations reduce SPI overhead
- **Flexibility**: Support for various addressing schemes
- **Safety**: Type-safe abstractions prevent misuse

This approach is foundational for drivers interfacing with accelerometers, gyroscopes, ADCs, DACs, display controllers, and virtually all complex SPI peripherals in embedded systems.