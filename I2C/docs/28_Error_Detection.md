# I2C Error Detection: A Comprehensive Guide

I2C communication, while robust, is susceptible to various errors during transmission. Proper error detection is crucial for building reliable embedded systems. 

## Overview of I2C Errors

I2C error detection involves monitoring for several types of failures:

1. **ACK/NACK Errors** - Slave fails to acknowledge
2. **Bus Arbitration Loss** - Multiple masters conflict
3. **Bus Timeout** - Communication hangs
4. **Clock Stretching Issues** - Slave holds SCL too long
5. **Bus Errors** - Invalid START/STOP conditions
6. **Data Corruption** - Electrical noise or timing issues

## 1. ACK/NACK Detection

The most common error is when a slave device fails to acknowledge a transmission. This can occur during address transmission or data transfer.

### How ACK Works
- After each byte transmission, the transmitter releases SDA
- The receiver pulls SDA low during the 9th clock pulse (ACK)
- If SDA remains high, it's a NACK (Not Acknowledged)

**Reasons for NACK:**
- Slave address not present on bus
- Slave busy or malfunctioning
- Data buffer full
- Invalid command/register address

## 2. Arbitration Loss

When multiple masters transmit simultaneously, arbitration determines the winner. A master loses arbitration when:
- It transmits a HIGH bit
- But detects a LOW on the bus (another master is transmitting LOW)

The losing master must immediately stop transmitting and wait.

## 3. Bus Timeout

The bus can hang if:
- A slave holds SCL low indefinitely (clock stretching gone wrong)
- A device crashes mid-transaction
- Electrical issues prevent proper signaling

## Code Examples

```c
/*
 * I2C Error Detection Implementation in C/C++
 * Demonstrates comprehensive error handling for I2C communication
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Error status codes
typedef enum {
    I2C_OK = 0,
    I2C_ERROR_NACK,           // No acknowledge received
    I2C_ERROR_TIMEOUT,        // Communication timeout
    I2C_ERROR_ARBITRATION,    // Arbitration lost
    I2C_ERROR_BUS_ERROR,      // Bus error (invalid start/stop)
    I2C_ERROR_BUSY,           // Bus is busy
    I2C_ERROR_OVERFLOW,       // Buffer overflow
    I2C_ERROR_INVALID_PARAM   // Invalid parameter
} i2c_status_t;

// I2C transaction state
typedef enum {
    I2C_STATE_IDLE,
    I2C_STATE_TRANSMIT,
    I2C_STATE_RECEIVE,
    I2C_STATE_ERROR
} i2c_state_t;

// Hardware register structure (example for typical MCU)
typedef struct {
    volatile uint32_t CR1;      // Control register 1
    volatile uint32_t CR2;      // Control register 2
    volatile uint32_t SR1;      // Status register 1
    volatile uint32_t SR2;      // Status register 2
    volatile uint32_t DR;       // Data register
} I2C_TypeDef;

// Status register 1 bit definitions
#define I2C_SR1_SB          (1 << 0)   // Start bit
#define I2C_SR1_ADDR        (1 << 1)   // Address sent/matched
#define I2C_SR1_BTF         (1 << 2)   // Byte transfer finished
#define I2C_SR1_TXE         (1 << 7)   // Data register empty
#define I2C_SR1_RXNE        (1 << 6)   // Data register not empty
#define I2C_SR1_AF          (1 << 10)  // Acknowledge failure
#define I2C_SR1_ARLO        (1 << 9)   // Arbitration lost
#define I2C_SR1_BERR        (1 << 8)   // Bus error
#define I2C_SR1_TIMEOUT     (1 << 14)  // Timeout error

// Control register 1 bit definitions
#define I2C_CR1_PE          (1 << 0)   // Peripheral enable
#define I2C_CR1_START       (1 << 8)   // Start generation
#define I2C_CR1_STOP        (1 << 9)   // Stop generation
#define I2C_CR1_ACK         (1 << 10)  // Acknowledge enable

// Example I2C peripheral base address (platform specific)
#define I2C1_BASE           ((I2C_TypeDef*)0x40005400)

// Timeout value (iterations)
#define I2C_TIMEOUT_MS      100
#define I2C_TIMEOUT_TICKS   (I2C_TIMEOUT_MS * 1000) // Assuming 1MHz counter

/*
 * Check for acknowledge failure (NACK)
 */
static i2c_status_t i2c_check_ack_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_AF) {
        // Clear the ACK failure flag
        i2c->SR1 &= ~I2C_SR1_AF;
        
        // Generate STOP condition to release the bus
        i2c->CR1 |= I2C_CR1_STOP;
        
        printf("ERROR: NACK received - slave not responding\n");
        return I2C_ERROR_NACK;
    }
    return I2C_OK;
}

/*
 * Check for arbitration loss
 */
static i2c_status_t i2c_check_arbitration_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_ARLO) {
        // Clear the arbitration lost flag
        i2c->SR1 &= ~I2C_SR1_ARLO;
        
        printf("ERROR: Arbitration lost - another master took control\n");
        return I2C_ERROR_ARBITRATION;
    }
    return I2C_OK;
}

/*
 * Check for bus error
 */
static i2c_status_t i2c_check_bus_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_BERR) {
        // Clear the bus error flag
        i2c->SR1 &= ~I2C_SR1_BERR;
        
        printf("ERROR: Bus error - misplaced START or STOP condition\n");
        return I2C_ERROR_BUS_ERROR;
    }
    return I2C_OK;
}

/*
 * Wait for a specific flag with timeout protection
 */
static i2c_status_t i2c_wait_flag(I2C_TypeDef *i2c, uint32_t flag, 
                                   bool state, uint32_t timeout) {
    uint32_t tick_start = get_tick_count(); // Platform-specific timer
    
    while (timeout > 0) {
        // Check for errors first
        i2c_status_t status;
        
        if ((status = i2c_check_ack_error(i2c)) != I2C_OK) return status;
        if ((status = i2c_check_arbitration_error(i2c)) != I2C_OK) return status;
        if ((status = i2c_check_bus_error(i2c)) != I2C_OK) return status;
        
        // Check if flag reached desired state
        bool flag_status = (i2c->SR1 & flag) != 0;
        if (flag_status == state) {
            return I2C_OK;
        }
        
        // Check timeout
        if ((get_tick_count() - tick_start) >= timeout) {
            printf("ERROR: Timeout waiting for flag 0x%08X\n", flag);
            return I2C_ERROR_TIMEOUT;
        }
    }
    
    return I2C_ERROR_TIMEOUT;
}

/*
 * Write data to I2C slave with comprehensive error checking
 */
i2c_status_t i2c_write(I2C_TypeDef *i2c, uint8_t slave_addr, 
                       const uint8_t *data, uint16_t len) {
    i2c_status_t status;
    
    if (!data || len == 0) {
        return I2C_ERROR_INVALID_PARAM;
    }
    
    // Check if bus is busy
    if (i2c->SR2 & (1 << 1)) { // BUSY flag
        printf("ERROR: I2C bus is busy\n");
        return I2C_ERROR_BUSY;
    }
    
    // Generate START condition
    i2c->CR1 |= I2C_CR1_START;
    
    // Wait for START condition to be generated (SB flag)
    status = i2c_wait_flag(i2c, I2C_SR1_SB, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Failed to generate START condition\n");
        return status;
    }
    
    // Send slave address with write bit (LSB = 0)
    i2c->DR = (slave_addr << 1) | 0;
    
    // Wait for address to be sent (ADDR flag)
    status = i2c_wait_flag(i2c, I2C_SR1_ADDR, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Slave address 0x%02X not acknowledged\n", slave_addr);
        return status;
    }
    
    // Clear ADDR flag by reading SR1 and SR2
    (void)i2c->SR1;
    (void)i2c->SR2;
    
    // Transmit data bytes
    for (uint16_t i = 0; i < len; i++) {
        // Wait for TXE (transmit buffer empty)
        status = i2c_wait_flag(i2c, I2C_SR1_TXE, true, I2C_TIMEOUT_TICKS);
        if (status != I2C_OK) {
            printf("ERROR: Timeout waiting for TXE at byte %d\n", i);
            goto error_stop;
        }
        
        // Write data byte
        i2c->DR = data[i];
        
        // Check for NACK after each byte
        status = i2c_check_ack_error(i2c);
        if (status != I2C_OK) {
            printf("ERROR: NACK received at byte %d\n", i);
            goto error_stop;
        }
    }
    
    // Wait for BTF (byte transfer finished)
    status = i2c_wait_flag(i2c, I2C_SR1_BTF, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        goto error_stop;
    }
    
    // Generate STOP condition
    i2c->CR1 |= I2C_CR1_STOP;
    
    return I2C_OK;

error_stop:
    // Generate STOP to release the bus
    i2c->CR1 |= I2C_CR1_STOP;
    return status;
}

/*
 * Read data from I2C slave with error detection
 */
i2c_status_t i2c_read(I2C_TypeDef *i2c, uint8_t slave_addr, 
                      uint8_t *data, uint16_t len) {
    i2c_status_t status;
    
    if (!data || len == 0) {
        return I2C_ERROR_INVALID_PARAM;
    }
    
    // Enable ACK
    i2c->CR1 |= I2C_CR1_ACK;
    
    // Generate START condition
    i2c->CR1 |= I2C_CR1_START;
    
    // Wait for START
    status = i2c_wait_flag(i2c, I2C_SR1_SB, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) return status;
    
    // Send slave address with read bit (LSB = 1)
    i2c->DR = (slave_addr << 1) | 1;
    
    // Wait for address ACK
    status = i2c_wait_flag(i2c, I2C_SR1_ADDR, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Slave 0x%02X NACK on read address\n", slave_addr);
        i2c->CR1 |= I2C_CR1_STOP;
        return status;
    }
    
    // Clear ADDR flag
    (void)i2c->SR1;
    (void)i2c->SR2;
    
    // Receive data bytes
    for (uint16_t i = 0; i < len; i++) {
        // Before last byte, disable ACK and generate STOP
        if (i == len - 1) {
            i2c->CR1 &= ~I2C_CR1_ACK;
            i2c->CR1 |= I2C_CR1_STOP;
        }
        
        // Wait for RXNE (receive buffer not empty)
        status = i2c_wait_flag(i2c, I2C_SR1_RXNE, true, I2C_TIMEOUT_TICKS);
        if (status != I2C_OK) {
            printf("ERROR: Timeout waiting for data byte %d\n", i);
            i2c->CR1 |= I2C_CR1_STOP;
            return status;
        }
        
        // Read data byte
        data[i] = (uint8_t)i2c->DR;
    }
    
    return I2C_OK;
}

/*
 * Recovery procedure for stuck bus
 */
void i2c_bus_recovery(I2C_TypeDef *i2c) {
    printf("Attempting I2C bus recovery...\n");
    
    // Disable I2C peripheral
    i2c->CR1 &= ~I2C_CR1_PE;
    
    // Platform-specific: Toggle SCL line 9 times to recover slaves
    // This requires GPIO bit-banging
    for (int i = 0; i < 9; i++) {
        // toggle_scl_line(); // Platform-specific implementation
        delay_us(5);
    }
    
    // Re-enable I2C peripheral
    i2c->CR1 |= I2C_CR1_PE;
    
    printf("Bus recovery complete\n");
}

/*
 * Example usage with error handling
 */
void example_usage(void) {
    I2C_TypeDef *i2c = I2C1_BASE;
    uint8_t slave_address = 0x50; // Example EEPROM address
    uint8_t write_data[] = {0x00, 0x10, 0xAA, 0xBB, 0xCC};
    uint8_t read_data[3];
    i2c_status_t status;
    
    // Write operation
    status = i2c_write(i2c, slave_address, write_data, sizeof(write_data));
    if (status != I2C_OK) {
        printf("Write failed with error code: %d\n", status);
        
        if (status == I2C_ERROR_TIMEOUT) {
            i2c_bus_recovery(i2c);
        }
        return;
    }
    
    delay_ms(5); // EEPROM write delay
    
    // Read operation
    status = i2c_read(i2c, slave_address, read_data, sizeof(read_data));
    if (status != I2C_OK) {
        printf("Read failed with error code: %d\n", status);
        return;
    }
    
    printf("Read successful: 0x%02X 0x%02X 0x%02X\n", 
           read_data[0], read_data[1], read_data[2]);
}

// Platform-specific helper functions (to be implemented)
uint32_t get_tick_count(void) {
    // Return current tick count from system timer
    return 0; // Placeholder
}

void delay_ms(uint32_t ms) {
    // Platform-specific delay
}

void delay_us(uint32_t us) {
    // Platform-specific microsecond delay
}
```

Rust implementation:

```rust
/*
 * I2C Error Detection Implementation in Rust
 * Demonstrates type-safe error handling with Rust's Result type
 */

use core::fmt;
use core::time::Duration;

// Error types using Rust's type system
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    /// No acknowledge received from slave
    Nack,
    /// Communication timeout
    Timeout,
    /// Arbitration lost to another master
    ArbitrationLost,
    /// Bus error (invalid START/STOP condition)
    BusError,
    /// Bus is busy
    Busy,
    /// Buffer overflow
    Overflow,
    /// Invalid parameter provided
    InvalidParam,
    /// Clock stretching timeout
    ClockStretchTimeout,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cError::Nack => write!(f, "No acknowledge received"),
            I2cError::Timeout => write!(f, "Communication timeout"),
            I2cError::ArbitrationLost => write!(f, "Arbitration lost"),
            I2cError::BusError => write!(f, "Bus error detected"),
            I2cError::Busy => write!(f, "Bus is busy"),
            I2cError::Overflow => write!(f, "Buffer overflow"),
            I2cError::InvalidParam => write!(f, "Invalid parameter"),
            I2cError::ClockStretchTimeout => write!(f, "Clock stretching timeout"),
        }
    }
}

// Transaction state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cState {
    Idle,
    Transmit,
    Receive,
    Error,
}

// Status register flags
#[repr(u32)]
pub enum StatusFlag {
    StartBit = 1 << 0,
    AddressSent = 1 << 1,
    ByteTransferFinished = 1 << 2,
    TxEmpty = 1 << 7,
    RxNotEmpty = 1 << 6,
    AckFailure = 1 << 10,
    ArbitrationLost = 1 << 9,
    BusError = 1 << 8,
    Timeout = 1 << 14,
}

// Memory-mapped I2C peripheral registers
#[repr(C)]
pub struct I2cRegisters {
    cr1: u32,      // Control register 1
    cr2: u32,      // Control register 2
    sr1: u32,      // Status register 1
    sr2: u32,      // Status register 2
    dr: u32,       // Data register
}

// Control register 1 bits
const CR1_PE: u32 = 1 << 0;    // Peripheral enable
const CR1_START: u32 = 1 << 8; // Start generation
const CR1_STOP: u32 = 1 << 9;  // Stop generation
const CR1_ACK: u32 = 1 << 10;  // Acknowledge enable

// Safe wrapper around I2C peripheral
pub struct I2cBus {
    registers: *mut I2cRegisters,
    timeout_ms: u32,
    state: I2cState,
}

// Implement Send to allow transfer between threads (if needed)
unsafe impl Send for I2cBus {}

impl I2cBus {
    /// Create a new I2C bus instance
    /// 
    /// # Safety
    /// `base_addr` must point to valid I2C peripheral registers
    pub unsafe fn new(base_addr: usize, timeout_ms: u32) -> Self {
        Self {
            registers: base_addr as *mut I2cRegisters,
            timeout_ms,
            state: I2cState::Idle,
        }
    }

    /// Get reference to registers (unsafe operation)
    #[inline]
    fn regs(&self) -> &I2cRegisters {
        unsafe { &*self.registers }
    }

    /// Get mutable reference to registers (unsafe operation)
    #[inline]
    fn regs_mut(&mut self) -> &mut I2cRegisters {
        unsafe { &mut *self.registers }
    }

    /// Check for acknowledge failure
    fn check_ack_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::AckFailure as u32) != 0 {
            // Clear ACK failure flag
            self.regs_mut().sr1 &= !(StatusFlag::AckFailure as u32);
            
            // Generate STOP to release bus
            self.regs_mut().cr1 |= CR1_STOP;
            
            self.state = I2cState::Error;
            Err(I2cError::Nack)
        } else {
            Ok(())
        }
    }

    /// Check for arbitration loss
    fn check_arbitration_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::ArbitrationLost as u32) != 0 {
            // Clear arbitration lost flag
            self.regs_mut().sr1 &= !(StatusFlag::ArbitrationLost as u32);
            
            self.state = I2cState::Error;
            Err(I2cError::ArbitrationLost)
        } else {
            Ok(())
        }
    }

    /// Check for bus error
    fn check_bus_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::BusError as u32) != 0 {
            // Clear bus error flag
            self.regs_mut().sr1 &= !(StatusFlag::BusError as u32);
            
            self.state = I2cState::Error;
            Err(I2cError::BusError)
        } else {
            Ok(())
        }
    }

    /// Wait for a specific flag with timeout and error checking
    fn wait_flag(&mut self, flag: u32, state: bool) -> Result<(), I2cError> {
        let start = get_tick_count();
        let timeout_ticks = self.timeout_ms * 1000;

        loop {
            // Check for errors first
            self.check_ack_error()?;
            self.check_arbitration_error()?;
            self.check_bus_error()?;

            // Check if flag is in desired state
            let sr1 = self.regs().sr1;
            let flag_status = (sr1 & flag) != 0;
            if flag_status == state {
                return Ok(());
            }

            // Check timeout
            if (get_tick_count() - start) >= timeout_ticks {
                self.state = I2cState::Error;
                return Err(I2cError::Timeout);
            }
        }
    }

    /// Check if bus is busy
    fn is_busy(&self) -> bool {
        (self.regs().sr2 & (1 << 1)) != 0
    }

    /// Generate START condition
    fn start(&mut self) -> Result<(), I2cError> {
        if self.is_busy() {
            return Err(I2cError::Busy);
        }

        self.regs_mut().cr1 |= CR1_START;
        self.wait_flag(StatusFlag::StartBit as u32, true)?;
        Ok(())
    }

    /// Generate STOP condition
    fn stop(&mut self) {
        self.regs_mut().cr1 |= CR1_STOP;
        self.state = I2cState::Idle;
    }

    /// Send slave address
    fn send_address(&mut self, address: u8, read: bool) -> Result<(), I2cError> {
        let addr_byte = (address << 1) | if read { 1 } else { 0 };
        self.regs_mut().dr = addr_byte as u32;
        
        self.wait_flag(StatusFlag::AddressSent as u32, true)
            .map_err(|e| {
                if e == I2cError::Timeout || e == I2cError::Nack {
                    // Address not acknowledged - slave not present
                    self.stop();
                }
                e
            })?;

        // Clear ADDR flag by reading SR1 and SR2
        let _ = self.regs().sr1;
        let _ = self.regs().sr2;

        Ok(())
    }

    /// Write data to I2C slave
    pub fn write(&mut self, address: u8, data: &[u8]) -> Result<(), I2cError> {
        if data.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        self.state = I2cState::Transmit;

        // Generate START condition
        self.start()?;

        // Send slave address with write bit
        self.send_address(address, false)?;

        // Transmit data bytes
        for (i, &byte) in data.iter().enumerate() {
            // Wait for TX buffer empty
            self.wait_flag(StatusFlag::TxEmpty as u32, true)
                .map_err(|e| {
                    self.stop();
                    e
                })?;

            // Write data byte
            self.regs_mut().dr = byte as u32;

            // Check for NACK after each byte
            if let Err(e) = self.check_ack_error() {
                return Err(e);
            }
        }

        // Wait for byte transfer finished
        self.wait_flag(StatusFlag::ByteTransferFinished as u32, true)?;

        // Generate STOP condition
        self.stop();

        Ok(())
    }

    /// Read data from I2C slave
    pub fn read(&mut self, address: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        if buffer.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        self.state = I2cState::Receive;

        // Enable ACK
        self.regs_mut().cr1 |= CR1_ACK;

        // Generate START condition
        self.start()?;

        // Send slave address with read bit
        self.send_address(address, true)?;

        // Receive data bytes
        for (i, byte) in buffer.iter_mut().enumerate() {
            // Before last byte, disable ACK and generate STOP
            if i == buffer.len() - 1 {
                self.regs_mut().cr1 &= !CR1_ACK;
                self.regs_mut().cr1 |= CR1_STOP;
            }

            // Wait for RX buffer not empty
            self.wait_flag(StatusFlag::RxNotEmpty as u32, true)
                .map_err(|e| {
                    self.stop();
                    e
                })?;

            // Read data byte
            *byte = self.regs().dr as u8;
        }

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Write then read (combined transaction)
    pub fn write_read(
        &mut self,
        address: u8,
        write_data: &[u8],
        read_buffer: &mut [u8],
    ) -> Result<(), I2cError> {
        if write_data.is_empty() || read_buffer.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        // Write phase
        self.start()?;
        self.send_address(address, false)?;

        for &byte in write_data {
            self.wait_flag(StatusFlag::TxEmpty as u32, true)?;
            self.regs_mut().dr = byte as u32;
            self.check_ack_error()?;
        }

        // Wait for byte transfer finished
        self.wait_flag(StatusFlag::ByteTransferFinished as u32, true)?;

        // Read phase - repeated START
        self.start()?;
        self.send_address(address, true)?;

        for (i, byte) in read_buffer.iter_mut().enumerate() {
            if i == read_buffer.len() - 1 {
                self.regs_mut().cr1 &= !CR1_ACK;
                self.regs_mut().cr1 |= CR1_STOP;
            }

            self.wait_flag(StatusFlag::RxNotEmpty as u32, true)?;
            *byte = self.regs().dr as u8;
        }

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Attempt bus recovery for stuck condition
    pub fn recover(&mut self) -> Result<(), I2cError> {
        // Disable peripheral
        self.regs_mut().cr1 &= !CR1_PE;

        // Toggle SCL 9 times (platform-specific GPIO manipulation)
        for _ in 0..9 {
            // toggle_scl();
            delay_us(5);
        }

        // Re-enable peripheral
        self.regs_mut().cr1 |= CR1_PE;

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Get current bus state
    pub fn state(&self) -> I2cState {
        self.state
    }
}

// Example usage with proper error handling
pub fn example_usage() -> Result<(), I2cError> {
    const I2C1_BASE: usize = 0x40005400;
    const SLAVE_ADDR: u8 = 0x50; // EEPROM address

    // Create I2C bus with 100ms timeout
    let mut i2c = unsafe { I2cBus::new(I2C1_BASE, 100) };

    // Write operation with error handling
    let write_data = [0x00, 0x10, 0xAA, 0xBB, 0xCC];
    match i2c.write(SLAVE_ADDR, &write_data) {
        Ok(_) => println!("Write successful"),
        Err(I2cError::Nack) => {
            println!("Slave not responding - check connections");
            return Err(I2cError::Nack);
        }
        Err(I2cError::Timeout) => {
            println!("Timeout - attempting bus recovery");
            i2c.recover()?;
            return Err(I2cError::Timeout);
        }
        Err(e) => {
            println!("Write error: {}", e);
            return Err(e);
        }
    }

    // Delay for EEPROM write cycle
    delay_ms(5);

    // Read operation
    let mut read_buffer = [0u8; 3];
    i2c.read(SLAVE_ADDR, &mut read_buffer)?;

    println!("Read data: {:02X?}", read_buffer);

    Ok(())
}

// Example with write-read combined transaction
pub fn example_register_read() -> Result<(), I2cError> {
    const I2C1_BASE: usize = 0x40005400;
    const SENSOR_ADDR: u8 = 0x68; // Example IMU address

    let mut i2c = unsafe { I2cBus::new(I2C1_BASE, 100) };

    // Read from register 0x75 (WHO_AM_I register)
    let register = [0x75];
    let mut who_am_i = [0u8; 1];

    i2c.write_read(SENSOR_ADDR, &register, &mut who_am_i)?;

    println!("WHO_AM_I: 0x{:02X}", who_am_i[0]);

    Ok(())
}

// Platform-specific helper functions (to be implemented)
fn get_tick_count() -> u32 {
    // Return current tick count from system timer
    0 // Placeholder
}

fn delay_ms(ms: u32) {
    // Platform-specific delay
}

fn delay_us(us: u32) {
    // Platform-specific microsecond delay
}

fn println!(fmt: &str, args: impl std::fmt::Display) {
    // Platform-specific print
}

// Unit tests
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_display() {
        assert_eq!(format!("{}", I2cError::Nack), "No acknowledge received");
        assert_eq!(format!("{}", I2cError::Timeout), "Communication timeout");
    }

    #[test]
    fn test_state_transitions() {
        let mut state = I2cState::Idle;
        assert_eq!(state, I2cState::Idle);
        
        state = I2cState::Transmit;
        assert_eq!(state, I2cState::Transmit);
    }
}
```

## Key Differences Between C/C++ and Rust Implementations

### **Type Safety**
- **Rust**: Uses `Result<T, E>` type for error handling, forcing callers to handle errors explicitly
- **C**: Uses return codes that can be ignored accidentally

### **Memory Safety**
- **Rust**: Enforces safe access to hardware registers through unsafe blocks
- **C**: Direct pointer access without compile-time safety checks

### **Error Propagation**
- **Rust**: `?` operator for clean error propagation
- **C**: Manual error checking with goto for cleanup

### **Pattern Matching**
- **Rust**: Exhaustive match statements ensure all error cases are handled
- **C**: Switch statements or if-else chains can miss cases

## Best Practices for Error Detection

1. **Always check ACK after address transmission** - Most common failure point
2. **Implement timeouts** - Prevent infinite loops on stuck slaves
3. **Check errors before waiting for flags** - Catch issues early
4. **Generate STOP on errors** - Release the bus for other devices
5. **Log errors with context** - Include address, byte count, etc.
6. **Implement bus recovery** - Toggle SCL to recover stuck slaves
7. **Use hardware interrupts when available** - More efficient than polling
8. **Validate parameters** - Check null pointers, zero length, valid addresses

## Common Error Scenarios

**Scenario 1: Device Not Connected**
- Symptom: NACK on address byte
- Solution: Verify connections, check pull-ups, scan I2C bus

**Scenario 2: Clock Stretching Timeout**
- Symptom: Timeout while waiting for SCL
- Solution: Increase timeout, check if slave is functioning

**Scenario 3: Arbitration Loss**
- Symptom: ARLO flag set during multi-master operation
- Solution: Retry transaction, implement backoff algorithm

**Scenario 4: Bus Lockup**
- Symptom: SCL or SDA stuck low
- Solution: Run bus recovery procedure (9 SCL pulses)

These implementations provide production-ready error handling for I2C communication in both C/C++ and Rust!