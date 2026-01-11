# Polling vs Interrupts in I2C Communication

## Overview

I2C communication can be implemented using two fundamental approaches: **polling** and **interrupt-driven** methods. Each has distinct trade-offs in terms of CPU utilization, responsiveness, code complexity, and power consumption.

## Polling Approach

In polling mode, the CPU continuously checks the status of the I2C peripheral by reading status registers in a loop. The processor waits actively for operations to complete before proceeding.

### Characteristics of Polling:
- **Simplicity**: Straightforward to implement and debug
- **CPU Intensive**: Wastes CPU cycles waiting for hardware events
- **Deterministic**: Predictable timing and execution flow
- **Lower Power Consumption Issues**: CPU remains active during waits
- **Best for**: Simple applications, quick transactions, single-threaded systems

### C/C++ Polling Example

```c
#include <stdint.h>
#include <stdbool.h>

// Example registers (typical I2C peripheral)
#define I2C_CR1     (*(volatile uint32_t*)0x40005400)
#define I2C_CR2     (*(volatile uint32_t*)0x40005404)
#define I2C_ISR     (*(volatile uint32_t*)0x40005418)
#define I2C_TXDR    (*(volatile uint32_t*)0x40005428)
#define I2C_RXDR    (*(volatile uint32_t*)0x40005424)

// Status flags
#define I2C_ISR_TXIS    (1 << 1)  // Transmit interrupt status
#define I2C_ISR_RXNE    (1 << 2)  // Receive data register not empty
#define I2C_ISR_TC      (1 << 6)  // Transfer complete
#define I2C_ISR_NACKF   (1 << 4)  // NACK received
#define I2C_ISR_BUSY    (1 << 15) // Bus busy

#define TIMEOUT_MS      1000

// Polling-based I2C write function
bool i2c_write_polling(uint8_t dev_addr, uint8_t reg_addr, 
                       uint8_t* data, uint16_t len) {
    uint32_t timeout;
    
    // Wait for bus to be free
    timeout = TIMEOUT_MS;
    while ((I2C_ISR & I2C_ISR_BUSY) && timeout--) {
        delay_us(1);
    }
    if (timeout == 0) return false;
    
    // Configure transfer (address, number of bytes, start)
    I2C_CR2 = (dev_addr << 1) | ((len + 1) << 16) | (1 << 13); // START
    
    // Send register address
    timeout = TIMEOUT_MS;
    while (!(I2C_ISR & I2C_ISR_TXIS) && timeout--) {
        if (I2C_ISR & I2C_ISR_NACKF) return false;
        delay_us(1);
    }
    if (timeout == 0) return false;
    I2C_TXDR = reg_addr;
    
    // Send data bytes
    for (uint16_t i = 0; i < len; i++) {
        timeout = TIMEOUT_MS;
        while (!(I2C_ISR & I2C_ISR_TXIS) && timeout--) {
            if (I2C_ISR & I2C_ISR_NACKF) return false;
            delay_us(1);
        }
        if (timeout == 0) return false;
        I2C_TXDR = data[i];
    }
    
    // Wait for transfer complete
    timeout = TIMEOUT_MS;
    while (!(I2C_ISR & I2C_ISR_TC) && timeout--) {
        delay_us(1);
    }
    
    // Generate STOP condition
    I2C_CR2 |= (1 << 14);
    
    return (timeout > 0);
}

// Polling-based I2C read function
bool i2c_read_polling(uint8_t dev_addr, uint8_t reg_addr,
                      uint8_t* data, uint16_t len) {
    uint32_t timeout;
    
    // Write phase: send register address
    timeout = TIMEOUT_MS;
    while ((I2C_ISR & I2C_ISR_BUSY) && timeout--) {
        delay_us(1);
    }
    if (timeout == 0) return false;
    
    I2C_CR2 = (dev_addr << 1) | (1 << 16) | (1 << 13); // 1 byte, START
    
    timeout = TIMEOUT_MS;
    while (!(I2C_ISR & I2C_ISR_TXIS) && timeout--) {
        if (I2C_ISR & I2C_ISR_NACKF) return false;
        delay_us(1);
    }
    if (timeout == 0) return false;
    I2C_TXDR = reg_addr;
    
    timeout = TIMEOUT_MS;
    while (!(I2C_ISR & I2C_ISR_TC) && timeout--) {
        delay_us(1);
    }
    
    // Read phase: receive data
    I2C_CR2 = (dev_addr << 1) | (1 << 0) | (len << 16) | 
              (1 << 13) | (1 << 14); // READ, START, STOP
    
    for (uint16_t i = 0; i < len; i++) {
        timeout = TIMEOUT_MS;
        while (!(I2C_ISR & I2C_ISR_RXNE) && timeout--) {
            delay_us(1);
        }
        if (timeout == 0) return false;
        data[i] = I2C_RXDR;
    }
    
    return true;
}
```

## Interrupt-Driven Approach

In interrupt mode, the I2C peripheral generates hardware interrupts when events occur (TX buffer empty, RX data ready, transfer complete, errors). The CPU can perform other tasks while waiting for I2C events.

### Characteristics of Interrupts:
- **CPU Efficient**: CPU can perform other work during I2C transfers
- **More Complex**: Requires ISR management, state machines, and synchronization
- **Better Responsiveness**: Can handle multiple tasks concurrently
- **Lower Latency**: Immediate response to hardware events
- **Better Power Efficiency**: CPU can sleep while waiting
- **Best for**: Complex applications, RTOS environments, multiple peripherals

### C/C++ Interrupt Example

```c
#include <stdint.h>
#include <stdbool.h>

// Interrupt enable bits
#define I2C_CR1_TXIE    (1 << 1)  // TX interrupt enable
#define I2C_CR1_RXIE    (1 << 2)  // RX interrupt enable
#define I2C_CR1_TCIE    (1 << 6)  // Transfer complete interrupt enable
#define I2C_CR1_ERRIE   (1 << 7)  // Error interrupt enable

typedef enum {
    I2C_STATE_IDLE,
    I2C_STATE_WRITE_REG,
    I2C_STATE_WRITE_DATA,
    I2C_STATE_READ_REG,
    I2C_STATE_READ_DATA,
    I2C_STATE_COMPLETE,
    I2C_STATE_ERROR
} i2c_state_t;

typedef struct {
    volatile i2c_state_t state;
    uint8_t dev_addr;
    uint8_t reg_addr;
    uint8_t* buffer;
    uint16_t length;
    volatile uint16_t index;
    volatile bool complete;
    volatile bool error;
} i2c_transfer_t;

static i2c_transfer_t i2c_xfer = {0};

// Initialize interrupt-driven I2C
void i2c_init_interrupt(void) {
    // Enable I2C interrupts
    I2C_CR1 |= I2C_CR1_TXIE | I2C_CR1_RXIE | 
               I2C_CR1_TCIE | I2C_CR1_ERRIE;
    
    // Enable I2C interrupt in NVIC
    NVIC_EnableIRQ(I2C1_IRQn);
}

// Non-blocking I2C write
bool i2c_write_interrupt(uint8_t dev_addr, uint8_t reg_addr,
                        uint8_t* data, uint16_t len) {
    if (i2c_xfer.state != I2C_STATE_IDLE) {
        return false; // Transfer in progress
    }
    
    // Setup transfer context
    i2c_xfer.dev_addr = dev_addr;
    i2c_xfer.reg_addr = reg_addr;
    i2c_xfer.buffer = data;
    i2c_xfer.length = len;
    i2c_xfer.index = 0;
    i2c_xfer.complete = false;
    i2c_xfer.error = false;
    i2c_xfer.state = I2C_STATE_WRITE_REG;
    
    // Start transfer
    I2C_CR2 = (dev_addr << 1) | ((len + 1) << 16) | (1 << 13);
    
    return true;
}

// Non-blocking I2C read
bool i2c_read_interrupt(uint8_t dev_addr, uint8_t reg_addr,
                       uint8_t* data, uint16_t len) {
    if (i2c_xfer.state != I2C_STATE_IDLE) {
        return false;
    }
    
    i2c_xfer.dev_addr = dev_addr;
    i2c_xfer.reg_addr = reg_addr;
    i2c_xfer.buffer = data;
    i2c_xfer.length = len;
    i2c_xfer.index = 0;
    i2c_xfer.complete = false;
    i2c_xfer.error = false;
    i2c_xfer.state = I2C_STATE_READ_REG;
    
    // Start write phase (send register address)
    I2C_CR2 = (dev_addr << 1) | (1 << 16) | (1 << 13);
    
    return true;
}

// I2C Interrupt Service Routine
void I2C1_IRQHandler(void) {
    uint32_t isr = I2C_ISR;
    
    // Handle errors
    if (isr & I2C_ISR_NACKF) {
        I2C_ISR |= I2C_ISR_NACKF; // Clear flag
        i2c_xfer.state = I2C_STATE_ERROR;
        i2c_xfer.error = true;
        i2c_xfer.complete = true;
        return;
    }
    
    switch (i2c_xfer.state) {
        case I2C_STATE_WRITE_REG:
            if (isr & I2C_ISR_TXIS) {
                I2C_TXDR = i2c_xfer.reg_addr;
                i2c_xfer.state = I2C_STATE_WRITE_DATA;
            }
            break;
            
        case I2C_STATE_WRITE_DATA:
            if (isr & I2C_ISR_TXIS) {
                if (i2c_xfer.index < i2c_xfer.length) {
                    I2C_TXDR = i2c_xfer.buffer[i2c_xfer.index++];
                }
            }
            if (isr & I2C_ISR_TC) {
                I2C_CR2 |= (1 << 14); // STOP
                i2c_xfer.state = I2C_STATE_COMPLETE;
                i2c_xfer.complete = true;
            }
            break;
            
        case I2C_STATE_READ_REG:
            if (isr & I2C_ISR_TXIS) {
                I2C_TXDR = i2c_xfer.reg_addr;
            }
            if (isr & I2C_ISR_TC) {
                // Start read phase
                I2C_CR2 = (i2c_xfer.dev_addr << 1) | (1 << 0) | 
                         (i2c_xfer.length << 16) | (1 << 13) | (1 << 14);
                i2c_xfer.state = I2C_STATE_READ_DATA;
            }
            break;
            
        case I2C_STATE_READ_DATA:
            if (isr & I2C_ISR_RXNE) {
                if (i2c_xfer.index < i2c_xfer.length) {
                    i2c_xfer.buffer[i2c_xfer.index++] = I2C_RXDR;
                }
                if (i2c_xfer.index >= i2c_xfer.length) {
                    i2c_xfer.state = I2C_STATE_COMPLETE;
                    i2c_xfer.complete = true;
                }
            }
            break;
            
        default:
            break;
    }
}

// Check if transfer is complete
bool i2c_is_complete(void) {
    return i2c_xfer.complete;
}

// Wait for transfer completion (with timeout)
bool i2c_wait_complete(uint32_t timeout_ms) {
    while (!i2c_xfer.complete && timeout_ms--) {
        delay_ms(1);
    }
    
    if (i2c_xfer.complete && !i2c_xfer.error) {
        i2c_xfer.state = I2C_STATE_IDLE;
        return true;
    }
    
    return false;
}
```

## Rust Examples

Rust provides strong type safety and memory safety guarantees, making it excellent for embedded I2C implementations. Here are both polling and interrupt-driven approaches.

### Rust Polling Example

```rust
// Using embedded-hal traits
use embedded_hal::blocking::i2c::{Write, Read, WriteRead};

pub struct I2cPolling {
    // Hardware register access would go here
    // Using memory-mapped I/O with volatile reads/writes
}

impl I2cPolling {
    pub fn new() -> Self {
        Self {}
    }
    
    fn wait_flag(&self, flag: u32, timeout_us: u32) -> Result<(), I2cError> {
        let mut timeout = timeout_us;
        
        while timeout > 0 {
            let isr = unsafe { 
                core::ptr::read_volatile(0x4000_5418 as *const u32) 
            };
            
            if isr & flag != 0 {
                return Ok(());
            }
            
            // Busy wait
            cortex_m::asm::delay(100); // ~1us on typical ARM Cortex-M
            timeout -= 1;
        }
        
        Err(I2cError::Timeout)
    }
    
    fn check_error(&self) -> Result<(), I2cError> {
        let isr = unsafe { 
            core::ptr::read_volatile(0x4000_5418 as *const u32) 
        };
        
        if isr & (1 << 4) != 0 { // NACKF
            return Err(I2cError::Nack);
        }
        
        Ok(())
    }
}

#[derive(Debug)]
pub enum I2cError {
    Timeout,
    Nack,
    BusError,
}

// Implement embedded-hal Write trait (polling)
impl Write for I2cPolling {
    type Error = I2cError;
    
    fn write(&mut self, addr: u8, bytes: &[u8]) -> Result<(), Self::Error> {
        const I2C_CR2: *mut u32 = 0x4000_5404 as *mut u32;
        const I2C_TXDR: *mut u32 = 0x4000_5428 as *mut u32;
        const I2C_ISR_TXIS: u32 = 1 << 1;
        const I2C_ISR_TC: u32 = 1 << 6;
        
        // Configure transfer
        unsafe {
            core::ptr::write_volatile(
                I2C_CR2,
                ((addr as u32) << 1) | ((bytes.len() as u32) << 16) | (1 << 13)
            );
        }
        
        // Send all bytes
        for &byte in bytes {
            self.wait_flag(I2C_ISR_TXIS, 10_000)?;
            self.check_error()?;
            
            unsafe {
                core::ptr::write_volatile(I2C_TXDR, byte as u32);
            }
        }
        
        // Wait for completion
        self.wait_flag(I2C_ISR_TC, 10_000)?;
        
        // Generate STOP
        unsafe {
            let cr2 = core::ptr::read_volatile(I2C_CR2);
            core::ptr::write_volatile(I2C_CR2, cr2 | (1 << 14));
        }
        
        Ok(())
    }
}

// Implement embedded-hal Read trait (polling)
impl Read for I2cPolling {
    type Error = I2cError;
    
    fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), Self::Error> {
        const I2C_CR2: *mut u32 = 0x4000_5404 as *mut u32;
        const I2C_RXDR: *const u32 = 0x4000_5424 as *const u32;
        const I2C_ISR_RXNE: u32 = 1 << 2;
        
        // Configure read transfer
        unsafe {
            core::ptr::write_volatile(
                I2C_CR2,
                ((addr as u32) << 1) | (1 << 0) | 
                ((buffer.len() as u32) << 16) | (1 << 13) | (1 << 14)
            );
        }
        
        // Receive all bytes
        for byte in buffer.iter_mut() {
            self.wait_flag(I2C_ISR_RXNE, 10_000)?;
            
            *byte = unsafe {
                core::ptr::read_volatile(I2C_RXDR) as u8
            };
        }
        
        Ok(())
    }
}

// Usage example
fn polling_example() -> Result<(), I2cError> {
    let mut i2c = I2cPolling::new();
    
    // Write to device
    let data = [0x01, 0x02, 0x03];
    i2c.write(0x50, &data)?;
    
    // Read from device
    let mut buffer = [0u8; 4];
    i2c.read(0x50, &mut buffer)?;
    
    Ok(())
}
```

### Rust Interrupt-Driven Example

```rust
use core::cell::RefCell;
use core::sync::atomic::{AtomicBool, Ordering};
use cortex_m::interrupt::{free, Mutex};

#[derive(Debug, Clone, Copy, PartialEq)]
enum I2cState {
    Idle,
    Writing,
    Reading,
    Complete,
    Error,
}

struct I2cTransfer {
    state: I2cState,
    buffer: [u8; 64],
    length: usize,
    index: usize,
    error: Option<I2cError>,
}

impl I2cTransfer {
    const fn new() -> Self {
        Self {
            state: I2cState::Idle,
            buffer: [0; 64],
            length: 0,
            index: 0,
            error: None,
        }
    }
}

// Global transfer state (protected by interrupt-free sections)
static I2C_TRANSFER: Mutex<RefCell<I2cTransfer>> = 
    Mutex::new(RefCell::new(I2cTransfer::new()));

static TRANSFER_COMPLETE: AtomicBool = AtomicBool::new(false);

pub struct I2cInterrupt {
    // Hardware peripheral handle
}

impl I2cInterrupt {
    pub fn new() -> Self {
        // Enable I2C interrupts
        const I2C_CR1: *mut u32 = 0x4000_5400 as *mut u32;
        
        unsafe {
            let cr1 = core::ptr::read_volatile(I2C_CR1);
            // Enable TX, RX, TC, and Error interrupts
            core::ptr::write_volatile(
                I2C_CR1, 
                cr1 | (1 << 1) | (1 << 2) | (1 << 6) | (1 << 7)
            );
        }
        
        Self {}
    }
    
    pub fn write_async(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        if data.len() > 64 {
            return Err(I2cError::BufferTooLarge);
        }
        
        free(|cs| {
            let mut xfer = I2C_TRANSFER.borrow(cs).borrow_mut();
            
            if xfer.state != I2cState::Idle {
                return Err(I2cError::Busy);
            }
            
            // Copy data to transfer buffer
            xfer.buffer[..data.len()].copy_from_slice(data);
            xfer.length = data.len();
            xfer.index = 0;
            xfer.state = I2cState::Writing;
            xfer.error = None;
            
            TRANSFER_COMPLETE.store(false, Ordering::Release);
            
            // Start transfer
            const I2C_CR2: *mut u32 = 0x4000_5404 as *mut u32;
            unsafe {
                core::ptr::write_volatile(
                    I2C_CR2,
                    ((addr as u32) << 1) | ((data.len() as u32) << 16) | (1 << 13)
                );
            }
            
            Ok(())
        })
    }
    
    pub fn read_async(&mut self, addr: u8, length: usize) -> Result<(), I2cError> {
        if length > 64 {
            return Err(I2cError::BufferTooLarge);
        }
        
        free(|cs| {
            let mut xfer = I2C_TRANSFER.borrow(cs).borrow_mut();
            
            if xfer.state != I2cState::Idle {
                return Err(I2cError::Busy);
            }
            
            xfer.length = length;
            xfer.index = 0;
            xfer.state = I2cState::Reading;
            xfer.error = None;
            
            TRANSFER_COMPLETE.store(false, Ordering::Release);
            
            // Start read transfer
            const I2C_CR2: *mut u32 = 0x4000_5404 as *mut u32;
            unsafe {
                core::ptr::write_volatile(
                    I2C_CR2,
                    ((addr as u32) << 1) | (1 << 0) | 
                    ((length as u32) << 16) | (1 << 13) | (1 << 14)
                );
            }
            
            Ok(())
        })
    }
    
    pub fn is_complete(&self) -> bool {
        TRANSFER_COMPLETE.load(Ordering::Acquire)
    }
    
    pub fn wait_complete(&self, timeout_ms: u32) -> Result<(), I2cError> {
        let mut timeout = timeout_ms;
        
        while !self.is_complete() && timeout > 0 {
            cortex_m::asm::delay(1000); // Approximate 1ms
            timeout -= 1;
        }
        
        if timeout == 0 {
            return Err(I2cError::Timeout);
        }
        
        // Check for errors
        free(|cs| {
            let mut xfer = I2C_TRANSFER.borrow(cs).borrow_mut();
            
            if let Some(error) = xfer.error {
                xfer.state = I2cState::Idle;
                return Err(error);
            }
            
            xfer.state = I2cState::Idle;
            Ok(())
        })
    }
    
    pub fn get_read_data(&self, buffer: &mut [u8]) -> Result<usize, I2cError> {
        free(|cs| {
            let xfer = I2C_TRANSFER.borrow(cs).borrow();
            
            if xfer.state != I2cState::Complete {
                return Err(I2cError::NotComplete);
            }
            
            let len = xfer.length.min(buffer.len());
            buffer[..len].copy_from_slice(&xfer.buffer[..len]);
            
            Ok(len)
        })
    }
}

#[derive(Debug, Clone, Copy)]
pub enum I2cError {
    Timeout,
    Nack,
    BusError,
    Busy,
    BufferTooLarge,
    NotComplete,
}

// Interrupt handler (called by hardware)
#[interrupt]
fn I2C1_EV() {
    const I2C_ISR: *const u32 = 0x4000_5418 as *const u32;
    const I2C_TXDR: *mut u32 = 0x4000_5428 as *mut u32;
    const I2C_RXDR: *const u32 = 0x4000_5424 as *const u32;
    const I2C_CR2: *mut u32 = 0x4000_5404 as *mut u32;
    
    let isr = unsafe { core::ptr::read_volatile(I2C_ISR) };
    
    free(|cs| {
        let mut xfer = I2C_TRANSFER.borrow(cs).borrow_mut();
        
        // Handle NACK error
        if isr & (1 << 4) != 0 {
            xfer.state = I2cState::Error;
            xfer.error = Some(I2cError::Nack);
            TRANSFER_COMPLETE.store(true, Ordering::Release);
            return;
        }
        
        match xfer.state {
            I2cState::Writing => {
                if isr & (1 << 1) != 0 { // TXIS
                    if xfer.index < xfer.length {
                        unsafe {
                            core::ptr::write_volatile(
                                I2C_TXDR, 
                                xfer.buffer[xfer.index] as u32
                            );
                        }
                        xfer.index += 1;
                    }
                }
                
                if isr & (1 << 6) != 0 { // TC
                    // Generate STOP
                    unsafe {
                        let cr2 = core::ptr::read_volatile(I2C_CR2);
                        core::ptr::write_volatile(I2C_CR2, cr2 | (1 << 14));
                    }
                    
                    xfer.state = I2cState::Complete;
                    TRANSFER_COMPLETE.store(true, Ordering::Release);
                }
            }
            
            I2cState::Reading => {
                if isr & (1 << 2) != 0 { // RXNE
                    if xfer.index < xfer.length {
                        xfer.buffer[xfer.index] = unsafe {
                            core::ptr::read_volatile(I2C_RXDR) as u8
                        };
                        xfer.index += 1;
                        
                        if xfer.index >= xfer.length {
                            xfer.state = I2cState::Complete;
                            TRANSFER_COMPLETE.store(true, Ordering::Release);
                        }
                    }
                }
            }
            
            _ => {}
        }
    });
}

// Usage example
fn interrupt_example() -> Result<(), I2cError> {
    let mut i2c = I2cInterrupt::new();
    
    // Non-blocking write
    let data = [0x01, 0x02, 0x03];
    i2c.write_async(0x50, &data)?;
    
    // Do other work here...
    
    // Wait for completion
    i2c.wait_complete(100)?;
    
    // Non-blocking read
    i2c.read_async(0x50, 4)?;
    i2c.wait_complete(100)?;
    
    let mut buffer = [0u8; 4];
    i2c.get_read_data(&mut buffer)?;
    
    Ok(())
}
```

## Trade-off Comparison Table

| Aspect | Polling | Interrupts |
|--------|---------|------------|
| **CPU Utilization** | High (busy waiting) | Low (event-driven) |
| **Code Complexity** | Simple, linear flow | More complex, state machines required |
| **Responsiveness** | Limited to polling frequency | Immediate hardware response |
| **Power Consumption** | Higher (CPU always active) | Lower (CPU can sleep) |
| **Debugging** | Easier to trace | More challenging (async behavior) |
| **Determinism** | Highly deterministic | Less deterministic (interrupt latency) |
| **Concurrency** | Blocks other tasks | Supports multitasking |
| **Real-time Performance** | Poor for long transfers | Excellent |
| **Memory Usage** | Lower (no ISR context) | Higher (ISR stack, state buffers) |

## When to Use Each Approach

### Use Polling When:
- Simple, single-threaded applications
- Short I2C transactions (< 10ms)
- Predictable, deterministic timing is critical
- Debugging and simplicity are priorities
- No other tasks need CPU time during I2C operations
- Learning or prototyping

### Use Interrupts When:
- Multi-tasking or RTOS environments
- Long or frequent I2C transactions
- Power efficiency is important
- Multiple peripherals need CPU attention
- Building production-grade systems
- CPU time is valuable for other computations

## Hybrid Approach

Many production systems use a **hybrid approach**: interrupts for data transfer with polling for setup/completion checking, or DMA (Direct Memory Access) combined with interrupts for maximum efficiency.

```c
// Hybrid: Use interrupts but provide blocking API
bool i2c_write_blocking_with_interrupts(uint8_t addr, uint8_t* data, uint16_t len) {
    i2c_write_interrupt(addr, 0x00, data, len);
    return i2c_wait_complete(1000); // Poll completion flag set by ISR
}
```

This combines the efficiency of interrupts with the simplicity of a blocking API for application code.