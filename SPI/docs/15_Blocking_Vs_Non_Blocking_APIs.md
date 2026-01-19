# Blocking vs Non-blocking APIs in SPI Communication

## Detailed Description

When designing SPI (Serial Peripheral Interface) communication systems, one of the most critical architectural decisions is whether to implement **blocking (synchronous)** or **non-blocking (asynchronous)** APIs. This choice fundamentally affects how your application handles I/O operations, CPU utilization, and overall system responsiveness.

### Blocking APIs (Synchronous)

Blocking APIs halt the calling thread's execution until the SPI transaction completes. The CPU waits idle during the entire data transfer operation. While this approach is conceptually simpler and easier to debug, it can lead to inefficient CPU utilization, especially when dealing with slow peripheral devices or large data transfers.

**Characteristics:**
- Simplest programming model
- Sequential, predictable execution flow
- CPU remains blocked during the entire transfer
- Best for simple applications or when immediate results are required
- No need for callbacks or state management

### Non-blocking APIs (Asynchronous)

Non-blocking APIs return immediately after initiating the transfer, allowing the CPU to perform other tasks while the SPI hardware handles communication. The application is notified of completion through callbacks, interrupts, or polling status flags.

**Characteristics:**
- More complex programming model
- Better CPU utilization
- Enables concurrent operations
- Requires interrupt handling or polling mechanisms
- Essential for real-time systems and multi-tasking environments
- May involve DMA (Direct Memory Access) for efficiency

## Code Examples

### C/C++ Implementation

```c
// blocking_nonblocking_spi.h
#ifndef BLOCKING_NONBLOCKING_SPI_H
#define BLOCKING_NONBLOCKING_SPI_H

#include <stdint.h>
#include <stdbool.h>

// SPI transfer status
typedef enum {
    SPI_STATUS_IDLE = 0,
    SPI_STATUS_BUSY,
    SPI_STATUS_COMPLETE,
    SPI_STATUS_ERROR
} spi_status_t;

// Callback function type for async operations
typedef void (*spi_callback_t)(spi_status_t status, void* user_data);

// SPI transfer descriptor for non-blocking operations
typedef struct {
    uint8_t* tx_buffer;
    uint8_t* rx_buffer;
    uint32_t length;
    spi_callback_t callback;
    void* user_data;
    volatile spi_status_t status;
} spi_transfer_t;

// ============= BLOCKING API =============

/**
 * Blocking SPI transfer - waits until completion
 * @param tx_data: Data to transmit
 * @param rx_data: Buffer for received data
 * @param length: Number of bytes to transfer
 * @return: 0 on success, negative on error
 */
int spi_transfer_blocking(const uint8_t* tx_data, uint8_t* rx_data, uint32_t length);

/**
 * Blocking write operation
 */
int spi_write_blocking(const uint8_t* data, uint32_t length);

/**
 * Blocking read operation
 */
int spi_read_blocking(uint8_t* data, uint32_t length);

// ============= NON-BLOCKING API =============

/**
 * Non-blocking SPI transfer - returns immediately
 * @param transfer: Transfer descriptor with callback
 * @return: 0 on success, negative if busy or error
 */
int spi_transfer_async(spi_transfer_t* transfer);

/**
 * Check if SPI is busy with a transfer
 */
bool spi_is_busy(void);

/**
 * Get current transfer status
 */
spi_status_t spi_get_status(void);

/**
 * Cancel ongoing async transfer
 */
void spi_cancel_transfer(void);

#endif // BLOCKING_NONBLOCKING_SPI_H
```

```c
// blocking_nonblocking_spi.c
#include "blocking_nonblocking_spi.h"
#include <string.h>

// Simulated hardware registers (replace with actual hardware addresses)
#define SPI_DATA_REG    (*((volatile uint8_t*)0x40000000))
#define SPI_STATUS_REG  (*((volatile uint32_t*)0x40000004))
#define SPI_CONTROL_REG (*((volatile uint32_t*)0x40000008))

#define SPI_SR_TXE  (1 << 0)  // Transmit buffer empty
#define SPI_SR_RXNE (1 << 1)  // Receive buffer not empty
#define SPI_SR_BSY  (1 << 2)  // SPI busy flag

// Global state for async operations
static spi_transfer_t* current_transfer = NULL;
static volatile uint32_t transfer_index = 0;

// ============= BLOCKING IMPLEMENTATION =============

int spi_transfer_blocking(const uint8_t* tx_data, uint8_t* rx_data, uint32_t length) {
    if (!tx_data && !rx_data) {
        return -1;  // Invalid parameters
    }
    
    for (uint32_t i = 0; i < length; i++) {
        // Wait for transmit buffer empty
        while (!(SPI_STATUS_REG & SPI_SR_TXE)) {
            // Blocking wait
        }
        
        // Send data (or dummy byte if tx_data is NULL)
        SPI_DATA_REG = tx_data ? tx_data[i] : 0xFF;
        
        // Wait for receive buffer not empty
        while (!(SPI_STATUS_REG & SPI_SR_RXNE)) {
            // Blocking wait
        }
        
        // Read received data
        uint8_t received = SPI_DATA_REG;
        if (rx_data) {
            rx_data[i] = received;
        }
    }
    
    // Wait until SPI is not busy
    while (SPI_STATUS_REG & SPI_SR_BSY) {
        // Blocking wait
    }
    
    return 0;  // Success
}

int spi_write_blocking(const uint8_t* data, uint32_t length) {
    return spi_transfer_blocking(data, NULL, length);
}

int spi_read_blocking(uint8_t* data, uint32_t length) {
    return spi_transfer_blocking(NULL, data, length);
}

// ============= NON-BLOCKING IMPLEMENTATION =============

int spi_transfer_async(spi_transfer_t* transfer) {
    if (!transfer || (!transfer->tx_buffer && !transfer->rx_buffer)) {
        return -1;  // Invalid parameters
    }
    
    if (current_transfer != NULL) {
        return -2;  // SPI busy with another transfer
    }
    
    // Initialize transfer
    current_transfer = transfer;
    transfer_index = 0;
    transfer->status = SPI_STATUS_BUSY;
    
    // Enable SPI interrupts (TXEIE and RXNEIE)
    SPI_CONTROL_REG |= (1 << 7) | (1 << 6);
    
    // Start first byte transmission
    if (transfer->tx_buffer) {
        SPI_DATA_REG = transfer->tx_buffer[0];
    } else {
        SPI_DATA_REG = 0xFF;  // Dummy byte
    }
    
    return 0;  // Successfully started
}

bool spi_is_busy(void) {
    return current_transfer != NULL;
}

spi_status_t spi_get_status(void) {
    if (current_transfer) {
        return current_transfer->status;
    }
    return SPI_STATUS_IDLE;
}

void spi_cancel_transfer(void) {
    // Disable SPI interrupts
    SPI_CONTROL_REG &= ~((1 << 7) | (1 << 6));
    
    if (current_transfer) {
        current_transfer->status = SPI_STATUS_ERROR;
        current_transfer = NULL;
    }
    transfer_index = 0;
}

// ============= INTERRUPT HANDLER =============

void SPI_IRQHandler(void) {
    if (!current_transfer) {
        return;
    }
    
    // Handle receive
    if (SPI_STATUS_REG & SPI_SR_RXNE) {
        uint8_t received = SPI_DATA_REG;
        
        if (current_transfer->rx_buffer) {
            current_transfer->rx_buffer[transfer_index] = received;
        }
        
        transfer_index++;
        
        // Check if transfer complete
        if (transfer_index >= current_transfer->length) {
            // Disable interrupts
            SPI_CONTROL_REG &= ~((1 << 7) | (1 << 6));
            
            current_transfer->status = SPI_STATUS_COMPLETE;
            
            // Call user callback
            if (current_transfer->callback) {
                current_transfer->callback(SPI_STATUS_COMPLETE, 
                                          current_transfer->user_data);
            }
            
            current_transfer = NULL;
            transfer_index = 0;
            return;
        }
    }
    
    // Handle transmit
    if (SPI_STATUS_REG & SPI_SR_TXE) {
        if (transfer_index < current_transfer->length) {
            if (current_transfer->tx_buffer) {
                SPI_DATA_REG = current_transfer->tx_buffer[transfer_index];
            } else {
                SPI_DATA_REG = 0xFF;  // Dummy byte
            }
        }
    }
}
```

```c
// example_usage.c
#include "blocking_nonblocking_spi.h"
#include <stdio.h>

// Callback for async transfer
void transfer_complete_callback(spi_status_t status, void* user_data) {
    if (status == SPI_STATUS_COMPLETE) {
        printf("Async transfer completed successfully!\n");
    } else {
        printf("Async transfer failed with status: %d\n", status);
    }
}

int main(void) {
    uint8_t tx_buffer[256];
    uint8_t rx_buffer[256];
    
    // Initialize buffers
    for (int i = 0; i < 256; i++) {
        tx_buffer[i] = i;
    }
    
    // ============= BLOCKING EXAMPLE =============
    printf("Starting blocking transfer...\n");
    
    if (spi_transfer_blocking(tx_buffer, rx_buffer, 256) == 0) {
        printf("Blocking transfer completed!\n");
        // Data is immediately available here
        printf("First received byte: 0x%02X\n", rx_buffer[0]);
    }
    
    // ============= NON-BLOCKING EXAMPLE =============
    printf("Starting non-blocking transfer...\n");
    
    spi_transfer_t async_transfer = {
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer,
        .length = 256,
        .callback = transfer_complete_callback,
        .user_data = NULL,
        .status = SPI_STATUS_IDLE
    };
    
    if (spi_transfer_async(&async_transfer) == 0) {
        printf("Async transfer initiated, CPU can do other work...\n");
        
        // Do other work while transfer happens in background
        for (volatile int i = 0; i < 1000000; i++) {
            // Simulate other processing
        }
        
        // Check status
        while (spi_is_busy()) {
            // Can do other work or sleep
        }
        
        printf("Async transfer finished!\n");
    }
    
    return 0;
}
```

### Rust Implementation

```rust
// lib.rs
use core::cell::RefCell;
use core::ptr::{read_volatile, write_volatile};
use cortex_m::interrupt::{free, Mutex};

// SPI hardware registers (example addresses)
const SPI_DATA_REG: *mut u8 = 0x4000_0000 as *mut u8;
const SPI_STATUS_REG: *mut u32 = 0x4000_0004 as *mut u32;
const SPI_CONTROL_REG: *mut u32 = 0x4000_0008 as *mut u32;

const SPI_SR_TXE: u32 = 1 << 0;
const SPI_SR_RXNE: u32 = 1 << 1;
const SPI_SR_BSY: u32 = 1 << 2;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiStatus {
    Idle,
    Busy,
    Complete,
    Error,
}

#[derive(Debug)]
pub enum SpiError {
    Busy,
    InvalidParameters,
    TransferError,
}

// ============= BLOCKING API =============

pub struct BlockingSpi;

impl BlockingSpi {
    pub fn new() -> Self {
        BlockingSpi
    }

    /// Blocking SPI transfer - waits until completion
    pub fn transfer(&self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), SpiError> {
        if tx_data.len() != rx_data.len() {
            return Err(SpiError::InvalidParameters);
        }

        for i in 0..tx_data.len() {
            // Wait for transmit buffer empty
            while unsafe { read_volatile(SPI_STATUS_REG) } & SPI_SR_TXE == 0 {
                // Blocking wait
            }

            // Send data
            unsafe { write_volatile(SPI_DATA_REG, tx_data[i]) };

            // Wait for receive buffer not empty
            while unsafe { read_volatile(SPI_STATUS_REG) } & SPI_SR_RXNE == 0 {
                // Blocking wait
            }

            // Read received data
            rx_data[i] = unsafe { read_volatile(SPI_DATA_REG) };
        }

        // Wait until SPI is not busy
        while unsafe { read_volatile(SPI_STATUS_REG) } & SPI_SR_BSY != 0 {
            // Blocking wait
        }

        Ok(())
    }

    pub fn write(&self, data: &[u8]) -> Result<(), SpiError> {
        let mut dummy_rx = vec![0u8; data.len()];
        self.transfer(data, &mut dummy_rx)
    }

    pub fn read(&self, data: &mut [u8]) -> Result<(), SpiError> {
        let dummy_tx = vec![0xFFu8; data.len()];
        self.transfer(&dummy_tx, data)
    }
}

// ============= NON-BLOCKING API =============

pub type SpiCallback = fn(SpiStatus, Option<&[u8]>);

pub struct AsyncTransfer {
    tx_buffer: Vec<u8>,
    rx_buffer: Vec<u8>,
    length: usize,
    index: usize,
    callback: Option<SpiCallback>,
    status: SpiStatus,
}

impl AsyncTransfer {
    pub fn new(tx_data: &[u8], rx_len: usize, callback: Option<SpiCallback>) -> Self {
        AsyncTransfer {
            tx_buffer: tx_data.to_vec(),
            rx_buffer: vec![0u8; rx_len],
            length: tx_data.len().max(rx_len),
            index: 0,
            callback,
            status: SpiStatus::Idle,
        }
    }
}

static CURRENT_TRANSFER: Mutex<RefCell<Option<AsyncTransfer>>> = Mutex::new(RefCell::new(None));

pub struct NonBlockingSpi;

impl NonBlockingSpi {
    pub fn new() -> Self {
        NonBlockingSpi
    }

    /// Start non-blocking transfer - returns immediately
    pub fn transfer_async(&self, mut transfer: AsyncTransfer) -> Result<(), SpiError> {
        free(|cs| {
            let mut current = CURRENT_TRANSFER.borrow(cs).borrow_mut();
            
            if current.is_some() {
                return Err(SpiError::Busy);
            }

            transfer.status = SpiStatus::Busy;
            transfer.index = 0;

            // Enable SPI interrupts
            unsafe {
                let mut ctrl = read_volatile(SPI_CONTROL_REG);
                ctrl |= (1 << 7) | (1 << 6);  // TXEIE and RXNEIE
                write_volatile(SPI_CONTROL_REG, ctrl);
            }

            // Start first byte transmission
            let first_byte = if !transfer.tx_buffer.is_empty() {
                transfer.tx_buffer[0]
            } else {
                0xFF
            };
            unsafe { write_volatile(SPI_DATA_REG, first_byte) };

            *current = Some(transfer);
            Ok(())
        })
    }

    pub fn is_busy(&self) -> bool {
        free(|cs| CURRENT_TRANSFER.borrow(cs).borrow().is_some())
    }

    pub fn get_status(&self) -> SpiStatus {
        free(|cs| {
            CURRENT_TRANSFER
                .borrow(cs)
                .borrow()
                .as_ref()
                .map(|t| t.status)
                .unwrap_or(SpiStatus::Idle)
        })
    }

    pub fn cancel_transfer(&self) {
        free(|cs| {
            // Disable SPI interrupts
            unsafe {
                let mut ctrl = read_volatile(SPI_CONTROL_REG);
                ctrl &= !((1 << 7) | (1 << 6));
                write_volatile(SPI_CONTROL_REG, ctrl);
            }

            let mut current = CURRENT_TRANSFER.borrow(cs).borrow_mut();
            if let Some(ref mut transfer) = *current {
                transfer.status = SpiStatus::Error;
            }
            *current = None;
        })
    }
}

// ============= INTERRUPT HANDLER =============

#[allow(non_snake_case)]
pub fn SPI_IRQHandler() {
    free(|cs| {
        let mut current = CURRENT_TRANSFER.borrow(cs).borrow_mut();
        
        if let Some(ref mut transfer) = *current {
            let status = unsafe { read_volatile(SPI_STATUS_REG) };

            // Handle receive
            if status & SPI_SR_RXNE != 0 {
                let received = unsafe { read_volatile(SPI_DATA_REG) };
                
                if transfer.index < transfer.rx_buffer.len() {
                    transfer.rx_buffer[transfer.index] = received;
                }

                transfer.index += 1;

                // Check if transfer complete
                if transfer.index >= transfer.length {
                    // Disable interrupts
                    unsafe {
                        let mut ctrl = read_volatile(SPI_CONTROL_REG);
                        ctrl &= !((1 << 7) | (1 << 6));
                        write_volatile(SPI_CONTROL_REG, ctrl);
                    }

                    transfer.status = SpiStatus::Complete;

                    // Call callback
                    if let Some(callback) = transfer.callback {
                        callback(SpiStatus::Complete, Some(&transfer.rx_buffer));
                    }

                    *current = None;
                    return;
                }
            }

            // Handle transmit
            if status & SPI_SR_TXE != 0 {
                if transfer.index < transfer.length {
                    let byte = if transfer.index < transfer.tx_buffer.len() {
                        transfer.tx_buffer[transfer.index]
                    } else {
                        0xFF  // Dummy byte
                    };
                    unsafe { write_volatile(SPI_DATA_REG, byte) };
                }
            }
        }
    });
}
```

```rust
// main.rs (example usage)
use cortex_m_rt::entry;

fn transfer_callback(status: SpiStatus, data: Option<&[u8]>) {
    if status == SpiStatus::Complete {
        println!("Async transfer completed!");
        if let Some(rx_data) = data {
            println!("First received byte: 0x{:02X}", rx_data[0]);
        }
    }
}

#[entry]
fn main() -> ! {
    let tx_data: Vec<u8> = (0..=255).collect();
    let mut rx_data = vec![0u8; 256];

    // ============= BLOCKING EXAMPLE =============
    let blocking_spi = BlockingSpi::new();
    
    println!("Starting blocking transfer...");
    match blocking_spi.transfer(&tx_data, &mut rx_data) {
        Ok(_) => {
            println!("Blocking transfer completed!");
            println!("First received byte: 0x{:02X}", rx_data[0]);
        }
        Err(e) => println!("Transfer error: {:?}", e),
    }

    // ============= NON-BLOCKING EXAMPLE =============
    let async_spi = NonBlockingSpi::new();
    
    println!("Starting non-blocking transfer...");
    let transfer = AsyncTransfer::new(&tx_data, 256, Some(transfer_callback));
    
    match async_spi.transfer_async(transfer) {
        Ok(_) => {
            println!("Async transfer initiated!");
            
            // Do other work while transfer happens
            for _ in 0..1000000 {
                // Simulate processing
            }
            
            // Wait for completion
            while async_spi.is_busy() {
                // Can do other work or enter low-power mode
            }
            
            println!("Async operations complete!");
        }
        Err(e) => println!("Failed to start async transfer: {:?}", e),
    }

    loop {}
}
```

## Summary

**Blocking vs Non-blocking APIs** represent two fundamentally different approaches to SPI communication design. **Blocking APIs** offer simplicity and straightforward sequential programming but sacrifice CPU efficiency by idling during transfers. They're ideal for simple applications, prototyping, or when immediate response is required. **Non-blocking APIs** provide superior CPU utilization and system responsiveness by allowing concurrent operations through interrupt-driven or DMA-based transfers, making them essential for real-time systems and complex multi-tasking applications.

The choice between these approaches involves trade-offs: blocking APIs minimize code complexity and debugging challenges, while non-blocking APIs maximize performance at the cost of increased complexity in state management, callback handling, and potential race conditions. Modern embedded systems often implement both patterns, allowing developers to choose the appropriate approach based on specific use cases—blocking for critical sections requiring immediate results, and non-blocking for background operations that can proceed while the CPU handles other tasks. Understanding both paradigms is crucial for building efficient, responsive embedded applications.