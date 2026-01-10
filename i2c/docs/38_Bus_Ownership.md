# I²C Bus Ownership: Managing Multi-Master Systems

## Overview

**Bus ownership** in I²C refers to the mechanisms that determine which master device has control of the shared bus at any given time. In single-master systems, this is straightforward—only one device initiates transactions. However, in **multi-master systems**, where multiple devices can act as masters, proper arbitration and ownership management become critical to prevent bus conflicts and data corruption.

## Key Concepts

### 1. Multi-Master Architecture

In a multi-master I²C system:
- Multiple devices can initiate communication (generate START conditions)
- Only one master can control the bus at a time
- Masters must detect and resolve conflicts when they attempt simultaneous access
- The bus appears "owned" by the master currently conducting a transaction

### 2. Bus States

The I²C bus transitions through several ownership states:

- **IDLE**: Bus is free (SDA and SCL both HIGH), no owner
- **BUSY**: A master owns the bus (between START and STOP conditions)
- **ARBITRATION**: Multiple masters competing for ownership
- **RESERVED**: Bus locked by a master using clock stretching or repeated START

### 3. Arbitration Mechanism

I²C uses a clever **non-destructive arbitration** scheme:
- Masters begin transmitting simultaneously
- Each master monitors the SDA line while driving it
- If a master writes '1' but reads '0', it has lost arbitration
- The losing master releases the bus immediately
- The winning master continues unaware of the competition

This works because I²C uses open-drain/open-collector outputs with pull-up resistors—any device pulling the line LOW overrides devices trying to drive it HIGH.

## C/C++ Implementation

Here's a comprehensive example of bus ownership management in C:

```c
#include <stdint.h>
#include <stdbool.h>

// I2C Hardware Registers (example for typical microcontroller)
typedef struct {
    volatile uint32_t CR1;      // Control register 1
    volatile uint32_t CR2;      // Control register 2
    volatile uint32_t SR1;      // Status register 1
    volatile uint32_t SR2;      // Status register 2
    volatile uint32_t DR;       // Data register
} I2C_TypeDef;

// Status flags
#define I2C_SR1_SB      (1 << 0)   // Start bit generated
#define I2C_SR1_ADDR    (1 << 1)   // Address sent
#define I2C_SR1_BTF     (1 << 2)   // Byte transfer finished
#define I2C_SR1_ARLO    (1 << 9)   // Arbitration lost
#define I2C_SR1_BERR    (1 << 8)   // Bus error
#define I2C_SR2_BUSY    (1 << 1)   // Bus busy
#define I2C_SR2_MSL     (1 << 0)   // Master/slave mode

// Bus ownership states
typedef enum {
    BUS_STATE_IDLE,
    BUS_STATE_ACQUIRING,
    BUS_STATE_OWNED,
    BUS_STATE_LOST_ARBITRATION,
    BUS_STATE_ERROR
} BusState;

// Master context
typedef struct {
    I2C_TypeDef *hw;
    BusState state;
    uint32_t retry_count;
    uint32_t max_retries;
    bool use_repeated_start;
} I2C_Master;

// Initialize master
void i2c_master_init(I2C_Master *master, I2C_TypeDef *hw) {
    master->hw = hw;
    master->state = BUS_STATE_IDLE;
    master->retry_count = 0;
    master->max_retries = 3;
    master->use_repeated_start = false;
}

// Check if bus is busy
bool i2c_bus_is_busy(I2C_Master *master) {
    return (master->hw->SR2 & I2C_SR2_BUSY) != 0;
}

// Wait for bus to become idle
bool i2c_wait_bus_idle(I2C_Master *master, uint32_t timeout_ms) {
    uint32_t start_time = get_tick_ms();
    
    while (i2c_bus_is_busy(master)) {
        if ((get_tick_ms() - start_time) > timeout_ms) {
            return false; // Timeout
        }
    }
    
    master->state = BUS_STATE_IDLE;
    return true;
}

// Attempt to acquire bus ownership
bool i2c_acquire_bus(I2C_Master *master) {
    // If we already own the bus via repeated START, we're good
    if (master->use_repeated_start && 
        master->state == BUS_STATE_OWNED) {
        return true;
    }
    
    // Wait for bus to be idle
    if (!i2c_wait_bus_idle(master, 100)) {
        master->state = BUS_STATE_ERROR;
        return false;
    }
    
    master->state = BUS_STATE_ACQUIRING;
    
    // Generate START condition
    master->hw->CR1 |= (1 << 8); // Set START bit
    
    // Wait for START condition to be generated
    uint32_t timeout = 1000;
    while (!(master->hw->SR1 & I2C_SR1_SB) && timeout--) {
        // Check for arbitration loss
        if (master->hw->SR1 & I2C_SR1_ARLO) {
            master->state = BUS_STATE_LOST_ARBITRATION;
            return false;
        }
    }
    
    if (timeout == 0) {
        master->state = BUS_STATE_ERROR;
        return false;
    }
    
    // Verify we're in master mode
    if (master->hw->SR2 & I2C_SR2_MSL) {
        master->state = BUS_STATE_OWNED;
        return true;
    }
    
    master->state = BUS_STATE_ERROR;
    return false;
}

// Release bus ownership
void i2c_release_bus(I2C_Master *master) {
    if (master->state == BUS_STATE_OWNED) {
        // Generate STOP condition
        master->hw->CR1 |= (1 << 9); // Set STOP bit
        
        // Wait for STOP to complete
        uint32_t timeout = 1000;
        while ((master->hw->CR1 & (1 << 9)) && timeout--);
        
        master->state = BUS_STATE_IDLE;
        master->use_repeated_start = false;
    }
}

// Handle arbitration loss
void i2c_handle_arbitration_loss(I2C_Master *master) {
    // Clear arbitration lost flag
    master->hw->SR1 &= ~I2C_SR1_ARLO;
    
    // Switch to slave mode if needed
    master->hw->CR1 &= ~(1 << 8); // Clear START
    
    master->state = BUS_STATE_IDLE;
    master->retry_count++;
}

// Perform transaction with retry on arbitration loss
bool i2c_master_transaction(I2C_Master *master, 
                            uint8_t slave_addr,
                            uint8_t *data, 
                            size_t len,
                            bool is_read) {
    master->retry_count = 0;
    
    while (master->retry_count < master->max_retries) {
        // Attempt to acquire bus
        if (!i2c_acquire_bus(master)) {
            if (master->state == BUS_STATE_LOST_ARBITRATION) {
                i2c_handle_arbitration_loss(master);
                
                // Random backoff before retry
                delay_us(10 + (rand() % 50));
                continue;
            }
            return false;
        }
        
        // Send slave address
        master->hw->DR = (slave_addr << 1) | (is_read ? 1 : 0);
        
        // Wait for address ACK
        uint32_t timeout = 1000;
        while (!(master->hw->SR1 & I2C_SR1_ADDR) && timeout--) {
            if (master->hw->SR1 & I2C_SR1_ARLO) {
                i2c_handle_arbitration_loss(master);
                delay_us(10 + (rand() % 50));
                goto retry;
            }
        }
        
        if (timeout == 0) {
            i2c_release_bus(master);
            return false;
        }
        
        // Clear ADDR flag by reading SR1 and SR2
        (void)master->hw->SR1;
        (void)master->hw->SR2;
        
        // Perform data transfer
        for (size_t i = 0; i < len; i++) {
            if (is_read) {
                // Wait for data
                timeout = 1000;
                while (!(master->hw->SR1 & I2C_SR1_BTF) && timeout--);
                if (timeout == 0) {
                    i2c_release_bus(master);
                    return false;
                }
                data[i] = master->hw->DR;
            } else {
                master->hw->DR = data[i];
                
                // Wait for byte transfer
                timeout = 1000;
                while (!(master->hw->SR1 & I2C_SR1_BTF) && timeout--);
                if (timeout == 0) {
                    i2c_release_bus(master);
                    return false;
                }
            }
        }
        
        // Transaction successful
        i2c_release_bus(master);
        return true;
        
    retry:
        continue;
    }
    
    // Max retries exceeded
    return false;
}

// Use repeated START to maintain bus ownership
bool i2c_master_write_read(I2C_Master *master,
                           uint8_t slave_addr,
                           uint8_t *write_data,
                           size_t write_len,
                           uint8_t *read_data,
                           size_t read_len) {
    master->retry_count = 0;
    
    while (master->retry_count < master->max_retries) {
        // Acquire bus for write
        if (!i2c_acquire_bus(master)) {
            if (master->state == BUS_STATE_LOST_ARBITRATION) {
                i2c_handle_arbitration_loss(master);
                delay_us(10 + (rand() % 50));
                continue;
            }
            return false;
        }
        
        // Write phase (implementation similar to above)
        // ... write_data ...
        
        // Generate repeated START to maintain ownership
        master->use_repeated_start = true;
        master->hw->CR1 |= (1 << 8); // Generate START
        
        // Wait for repeated START
        uint32_t timeout = 1000;
        while (!(master->hw->SR1 & I2C_SR1_SB) && timeout--) {
            if (master->hw->SR1 & I2C_SR1_ARLO) {
                i2c_handle_arbitration_loss(master);
                goto retry;
            }
        }
        
        // Read phase with bus ownership maintained
        // ... read_data ...
        
        // Release bus with STOP
        i2c_release_bus(master);
        return true;
        
    retry:
        continue;
    }
    
    return false;
}

// Example usage
void example_multi_master_usage(void) {
    I2C_TypeDef *i2c_hw = (I2C_TypeDef *)0x40005400; // Example address
    I2C_Master master;
    
    i2c_master_init(&master, i2c_hw);
    
    uint8_t data[] = {0x10, 0x20, 0x30};
    
    // Perform transaction with automatic retry on arbitration loss
    if (i2c_master_transaction(&master, 0x50, data, 3, false)) {
        // Success
    } else {
        // Failed after retries
    }
}
```

## Rust Implementation

Here's an idiomatic Rust implementation with safety guarantees:

```rust
use core::sync::atomic::{AtomicBool, Ordering};
use embedded_hal::blocking::i2c::{Write, Read, WriteRead};

// I2C Status flags
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BusState {
    Idle,
    Acquiring,
    Owned,
    LostArbitration,
    Error,
}

#[derive(Debug, Clone, Copy)]
pub enum I2cError {
    ArbitrationLost,
    BusError,
    Timeout,
    Nack,
}

// Hardware abstraction layer
pub trait I2cHardware {
    fn is_busy(&self) -> bool;
    fn generate_start(&mut self) -> Result<(), I2cError>;
    fn generate_stop(&mut self);
    fn is_master(&self) -> bool;
    fn arbitration_lost(&self) -> bool;
    fn clear_arbitration_lost(&mut self);
    fn send_address(&mut self, addr: u8, read: bool) -> Result<(), I2cError>;
    fn write_byte(&mut self, byte: u8) -> Result<(), I2cError>;
    fn read_byte(&mut self) -> Result<u8, I2cError>;
    fn generate_repeated_start(&mut self) -> Result<(), I2cError>;
}

// Bus ownership manager
pub struct I2cMaster<H: I2cHardware> {
    hardware: H,
    state: BusState,
    retry_count: u32,
    max_retries: u32,
    owns_bus: AtomicBool,
}

impl<H: I2cHardware> I2cMaster<H> {
    pub fn new(hardware: H, max_retries: u32) -> Self {
        Self {
            hardware,
            state: BusState::Idle,
            retry_count: 0,
            max_retries,
            owns_bus: AtomicBool::new(false),
        }
    }

    /// Wait for the bus to become idle
    fn wait_bus_idle(&self, timeout_ms: u32) -> Result<(), I2cError> {
        let start = get_time_ms();
        
        while self.hardware.is_busy() {
            if get_time_ms() - start > timeout_ms {
                return Err(I2cError::Timeout);
            }
        }
        
        Ok(())
    }

    /// Acquire bus ownership with arbitration
    fn acquire_bus(&mut self) -> Result<(), I2cError> {
        // If we already own the bus, we're done
        if self.owns_bus.load(Ordering::Acquire) {
            return Ok(());
        }

        // Wait for bus to be idle
        self.wait_bus_idle(100)?;
        
        self.state = BusState::Acquiring;

        // Generate START condition (initiates arbitration)
        self.hardware.generate_start()?;

        // Check if we won arbitration
        if self.hardware.arbitration_lost() {
            self.state = BusState::LostArbitration;
            self.hardware.clear_arbitration_lost();
            return Err(I2cError::ArbitrationLost);
        }

        // Verify we're in master mode
        if !self.hardware.is_master() {
            self.state = BusState::Error;
            return Err(I2cError::BusError);
        }

        self.state = BusState::Owned;
        self.owns_bus.store(true, Ordering::Release);
        Ok(())
    }

    /// Release bus ownership
    fn release_bus(&mut self) {
        if self.owns_bus.load(Ordering::Acquire) {
            self.hardware.generate_stop();
            self.state = BusState::Idle;
            self.owns_bus.store(false, Ordering::Release);
        }
    }

    /// Perform transaction with automatic retry on arbitration loss
    pub fn transaction<'a>(
        &mut self,
        address: u8,
        operations: &mut [Operation<'a>],
    ) -> Result<(), I2cError> {
        self.retry_count = 0;

        while self.retry_count < self.max_retries {
            match self.try_transaction(address, operations) {
                Ok(()) => return Ok(()),
                Err(I2cError::ArbitrationLost) => {
                    self.retry_count += 1;
                    // Exponential backoff with jitter
                    let backoff_us = 10 * (1 << self.retry_count) + (rand() % 50);
                    delay_us(backoff_us);
                    continue;
                }
                Err(e) => return Err(e),
            }
        }

        Err(I2cError::ArbitrationLost)
    }

    /// Single transaction attempt
    fn try_transaction<'a>(
        &mut self,
        address: u8,
        operations: &mut [Operation<'a>],
    ) -> Result<(), I2cError> {
        // Acquire bus ownership
        self.acquire_bus()?;

        // Perform all operations
        for (i, operation) in operations.iter_mut().enumerate() {
            // Use repeated START for subsequent operations
            if i > 0 {
                self.hardware.generate_repeated_start()?;
                
                if self.hardware.arbitration_lost() {
                    self.hardware.clear_arbitration_lost();
                    self.release_bus();
                    return Err(I2cError::ArbitrationLost);
                }
            }

            match operation {
                Operation::Write(buffer) => {
                    self.hardware.send_address(address, false)?;
                    
                    for &byte in buffer.iter() {
                        self.hardware.write_byte(byte)?;
                        
                        // Check for arbitration loss during data phase
                        if self.hardware.arbitration_lost() {
                            self.hardware.clear_arbitration_lost();
                            self.release_bus();
                            return Err(I2cError::ArbitrationLost);
                        }
                    }
                }
                Operation::Read(buffer) => {
                    self.hardware.send_address(address, true)?;
                    
                    for byte in buffer.iter_mut() {
                        *byte = self.hardware.read_byte()?;
                    }
                }
            }
        }

        // Release bus
        self.release_bus();
        Ok(())
    }

    /// Write-then-read with maintained bus ownership
    pub fn write_read(
        &mut self,
        address: u8,
        write_buf: &[u8],
        read_buf: &mut [u8],
    ) -> Result<(), I2cError> {
        let mut ops = [
            Operation::Write(write_buf),
            Operation::Read(read_buf),
        ];
        
        self.transaction(address, &mut ops)
    }
}

// Operation types
#[derive(Debug)]
pub enum Operation<'a> {
    Write(&'a [u8]),
    Read(&'a mut [u8]),
}

// RAII guard for bus ownership
pub struct BusGuard<'a, H: I2cHardware> {
    master: &'a mut I2cMaster<H>,
}

impl<'a, H: I2cHardware> BusGuard<'a, H> {
    pub fn new(master: &'a mut I2cMaster<H>) -> Result<Self, I2cError> {
        master.acquire_bus()?;
        Ok(Self { master })
    }
}

impl<'a, H: I2cHardware> Drop for BusGuard<'a, H> {
    fn drop(&mut self) {
        self.master.release_bus();
    }
}

// Example usage
pub fn example_usage<H: I2cHardware>(mut master: I2cMaster<H>) {
    let write_data = [0x10, 0x20, 0x30];
    let mut read_data = [0u8; 4];

    // Simple transaction with automatic retry
    match master.transaction(0x50, &mut [Operation::Write(&write_data)]) {
        Ok(()) => println!("Write successful"),
        Err(I2cError::ArbitrationLost) => println!("Lost arbitration after retries"),
        Err(e) => println!("Error: {:?}", e),
    }

    // Write-read with maintained ownership via repeated START
    match master.write_read(0x50, &write_data, &mut read_data) {
        Ok(()) => println!("Write-read successful: {:?}", read_data),
        Err(e) => println!("Error: {:?}", e),
    }

    // Manual bus ownership with RAII guard
    {
        let _guard = BusGuard::new(&mut master).expect("Failed to acquire bus");
        // Bus is owned here, automatically released when guard drops
    } // Bus released here
}

// Helper functions (platform-specific implementations needed)
fn get_time_ms() -> u32 {
    // Platform-specific timer implementation
    0
}

fn delay_us(us: u32) {
    // Platform-specific delay implementation
}

fn rand() -> u32 {
    // Simple RNG for backoff jitter
    42
}
```

## Key Takeaways

1. **Arbitration is automatic**: The hardware detects conflicts, but software must handle recovery
2. **Retry with backoff**: Always implement exponential backoff with jitter after arbitration loss
3. **Repeated START maintains ownership**: Use it for atomic multi-operation transactions
4. **Check bus state**: Always verify the bus is idle before attempting to acquire it
5. **Timeout protection**: Never wait indefinitely for bus conditions
6. **RAII in Rust**: Use ownership patterns to ensure proper bus release

Multi-master I²C systems require careful state management, but with proper arbitration handling and retry logic, they can operate reliably with multiple competing masters sharing the same bus.