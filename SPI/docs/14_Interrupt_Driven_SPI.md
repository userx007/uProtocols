# Interrupt-driven SPI: A Comprehensive Guide

## Overview

Interrupt-driven SPI is a communication technique that uses hardware interrupts to handle SPI data transfers asynchronously, rather than polling or blocking the CPU while waiting for transfers to complete. This approach significantly improves system responsiveness by allowing the processor to perform other tasks while SPI operations are in progress.

## Why Use Interrupt-driven SPI?

**Traditional polling approach** requires the CPU to continuously check if an SPI transfer is complete:
```c
// Inefficient polling
while (!(SPI->SR & SPI_SR_TXE)); // Wait for transmit buffer empty
SPI->DR = data;
while (!(SPI->SR & SPI_SR_RXNE)); // Wait for receive buffer full
```

This wastes CPU cycles that could be used for other tasks. **Interrupt-driven SPI** solves this by:
- Freeing the CPU to perform other work during transfers
- Reducing power consumption (CPU can enter low-power states)
- Enabling multi-tasking and real-time responsiveness
- Handling multiple SPI peripherals efficiently

## How It Works

1. **Initialize** SPI peripheral with interrupt enabled
2. **Start transfer** and immediately return to other tasks
3. **Hardware interrupt** fires when transfer completes
4. **ISR (Interrupt Service Routine)** handles the data
5. **Continue** with next transfer or signal completion

## Code Examples

### C/C++ Implementation (ARM Cortex-M)

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example for STM32)
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SR;
    volatile uint32_t DR;
} SPI_TypeDef;

#define SPI1 ((SPI_TypeDef*)0x40013000)
#define SPI_CR1_SPE     (1 << 6)
#define SPI_CR2_TXEIE   (1 << 7)  // TX buffer empty interrupt enable
#define SPI_CR2_RXNEIE  (1 << 6)  // RX buffer not empty interrupt enable
#define SPI_SR_TXE      (1 << 1)
#define SPI_SR_RXNE     (1 << 0)
#define SPI_SR_BSY      (1 << 7)

// Buffer management
static uint8_t tx_buffer[256];
static uint8_t rx_buffer[256];
static volatile uint16_t tx_index = 0;
static volatile uint16_t rx_index = 0;
static volatile uint16_t transfer_size = 0;
static volatile bool transfer_complete = false;

void spi_init_interrupt(void) {
    // Enable SPI clock (platform specific)
    // Configure GPIO pins for SPI
    
    // Configure SPI: Master mode, 8-bit, interrupt-driven
    SPI1->CR1 = 0;  // Disable SPI during configuration
    SPI1->CR1 |= (0x3 << 3);  // Baud rate prescaler /16
    SPI1->CR1 |= (1 << 2);    // Master mode
    
    // Enable TX and RX interrupts
    SPI1->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
    
    // Enable SPI
    SPI1->CR1 |= SPI_CR1_SPE;
    
    // Enable SPI interrupt in NVIC (Nested Vectored Interrupt Controller)
    NVIC_EnableIRQ(SPI1_IRQn);
    NVIC_SetPriority(SPI1_IRQn, 2);
}

bool spi_transfer_async(const uint8_t* tx_data, uint8_t* rx_data, uint16_t size) {
    if (transfer_size != 0) {
        return false;  // Transfer already in progress
    }
    
    // Setup transfer parameters
    for (uint16_t i = 0; i < size; i++) {
        tx_buffer[i] = tx_data ? tx_data[i] : 0xFF;
    }
    
    tx_index = 0;
    rx_index = 0;
    transfer_size = size;
    transfer_complete = false;
    
    // Enable interrupts - first byte will be sent in ISR
    SPI1->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
    
    return true;
}

// SPI Interrupt Service Routine
void SPI1_IRQHandler(void) {
    uint32_t sr = SPI1->SR;
    
    // Handle RX: Read received data
    if (sr & SPI_SR_RXNE) {
        if (rx_index < transfer_size) {
            rx_buffer[rx_index++] = (uint8_t)SPI1->DR;
            
            // Check if transfer complete
            if (rx_index >= transfer_size) {
                SPI1->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
                transfer_complete = true;
                transfer_size = 0;
            }
        }
    }
    
    // Handle TX: Send next byte
    if (sr & SPI_SR_TXE) {
        if (tx_index < transfer_size) {
            SPI1->DR = tx_buffer[tx_index++];
        } else {
            // Disable TX interrupt when done sending
            SPI1->CR2 &= ~SPI_CR2_TXEIE;
        }
    }
}

bool spi_is_transfer_complete(void) {
    return transfer_complete;
}

void spi_wait_for_completion(void) {
    while (!transfer_complete) {
        // Could enter low-power mode here
        __WFI();  // Wait For Interrupt
    }
}
```

### C++ Implementation with Callbacks

```cpp
#include <functional>
#include <cstdint>

class InterruptSPI {
private:
    SPI_TypeDef* spi_base;
    uint8_t* tx_buffer;
    uint8_t* rx_buffer;
    volatile uint16_t tx_index;
    volatile uint16_t rx_index;
    volatile uint16_t transfer_size;
    std::function<void(uint8_t*, uint16_t)> completion_callback;
    
public:
    InterruptSPI(SPI_TypeDef* spi) : spi_base(spi), 
                                      tx_buffer(nullptr),
                                      rx_buffer(nullptr),
                                      tx_index(0),
                                      rx_index(0),
                                      transfer_size(0) {}
    
    void init() {
        spi_base->CR1 = 0;
        spi_base->CR1 |= (0x3 << 3) | (1 << 2);  // Prescaler, master mode
        spi_base->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
        spi_base->CR1 |= SPI_CR1_SPE;
        
        NVIC_EnableIRQ(SPI1_IRQn);
    }
    
    bool transferAsync(const uint8_t* tx_data, uint8_t* rx_data, 
                       uint16_t size, 
                       std::function<void(uint8_t*, uint16_t)> callback = nullptr) {
        if (transfer_size != 0) return false;
        
        tx_buffer = const_cast<uint8_t*>(tx_data);
        rx_buffer = rx_data;
        tx_index = 0;
        rx_index = 0;
        transfer_size = size;
        completion_callback = callback;
        
        spi_base->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
        return true;
    }
    
    void handleInterrupt() {
        uint32_t sr = spi_base->SR;
        
        if (sr & SPI_SR_RXNE) {
            if (rx_index < transfer_size && rx_buffer) {
                rx_buffer[rx_index++] = static_cast<uint8_t>(spi_base->DR);
                
                if (rx_index >= transfer_size) {
                    spi_base->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
                    
                    if (completion_callback) {
                        completion_callback(rx_buffer, transfer_size);
                    }
                    transfer_size = 0;
                }
            }
        }
        
        if (sr & SPI_SR_TXE) {
            if (tx_index < transfer_size) {
                spi_base->DR = tx_buffer ? tx_buffer[tx_index++] : 0xFF;
            } else {
                spi_base->CR2 &= ~SPI_CR2_TXEIE;
            }
        }
    }
};

// Usage example
InterruptSPI spi1(SPI1);

extern "C" void SPI1_IRQHandler(void) {
    spi1.handleInterrupt();
}

void example_usage() {
    uint8_t tx_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx_data[4];
    
    spi1.init();
    
    // Asynchronous transfer with callback
    spi1.transferAsync(tx_data, rx_data, 4, 
        [](uint8_t* data, uint16_t len) {
            // This runs when transfer completes
            // Process received data here
        });
    
    // CPU is free to do other work while transfer happens
}
```

### Rust Implementation

```rust
#![no_std]

use core::cell::RefCell;
use cortex_m::interrupt::{free, Mutex};
use cortex_m::peripheral::NVIC;

// Hardware abstraction (simplified)
struct SpiRegisters {
    cr1: u32,
    cr2: u32,
    sr: u32,
    dr: u32,
}

const SPI_CR1_SPE: u32 = 1 << 6;
const SPI_CR2_TXEIE: u32 = 1 << 7;
const SPI_CR2_RXNEIE: u32 = 1 << 6;
const SPI_SR_TXE: u32 = 1 << 1;
const SPI_SR_RXNE: u32 = 1 << 0;

// Transfer state
struct TransferState {
    tx_buffer: [u8; 256],
    rx_buffer: [u8; 256],
    tx_index: usize,
    rx_index: usize,
    transfer_size: usize,
    complete: bool,
}

impl TransferState {
    const fn new() -> Self {
        Self {
            tx_buffer: [0; 256],
            rx_buffer: [0; 256],
            tx_index: 0,
            rx_index: 0,
            transfer_size: 0,
            complete: false,
        }
    }
}

// Global state protected by mutex
static TRANSFER_STATE: Mutex<RefCell<TransferState>> = 
    Mutex::new(RefCell::new(TransferState::new()));

pub struct InterruptSpi {
    registers: *mut SpiRegisters,
}

impl InterruptSpi {
    pub fn new(base_address: usize) -> Self {
        Self {
            registers: base_address as *mut SpiRegisters,
        }
    }
    
    pub fn init(&mut self) {
        unsafe {
            // Configure SPI in master mode
            (*self.registers).cr1 = 0;
            (*self.registers).cr1 |= (0x3 << 3) | (1 << 2);
            
            // Enable interrupts
            (*self.registers).cr2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
            
            // Enable SPI
            (*self.registers).cr1 |= SPI_CR1_SPE;
        }
        
        // Enable NVIC interrupt (platform specific)
        unsafe {
            NVIC::unmask(/* SPI1 interrupt number */);
        }
    }
    
    pub fn transfer_async(&mut self, tx_data: &[u8], size: usize) -> Result<(), &'static str> {
        free(|cs| {
            let mut state = TRANSFER_STATE.borrow(cs).borrow_mut();
            
            if state.transfer_size != 0 {
                return Err("Transfer already in progress");
            }
            
            // Copy data to buffer
            for (i, &byte) in tx_data.iter().enumerate().take(size) {
                state.tx_buffer[i] = byte;
            }
            
            state.tx_index = 0;
            state.rx_index = 0;
            state.transfer_size = size;
            state.complete = false;
            
            Ok(())
        })
    }
    
    pub fn is_complete(&self) -> bool {
        free(|cs| {
            TRANSFER_STATE.borrow(cs).borrow().complete
        })
    }
    
    pub fn get_received_data(&self, buffer: &mut [u8]) -> usize {
        free(|cs| {
            let state = TRANSFER_STATE.borrow(cs).borrow();
            let len = state.transfer_size.min(buffer.len());
            buffer[..len].copy_from_slice(&state.rx_buffer[..len]);
            len
        })
    }
    
    // Called from interrupt handler
    pub fn handle_interrupt(&mut self) {
        unsafe {
            let sr = (*self.registers).sr;
            
            free(|cs| {
                let mut state = TRANSFER_STATE.borrow(cs).borrow_mut();
                
                // Handle RX
                if sr & SPI_SR_RXNE != 0 {
                    if state.rx_index < state.transfer_size {
                        state.rx_buffer[state.rx_index] = (*self.registers).dr as u8;
                        state.rx_index += 1;
                        
                        if state.rx_index >= state.transfer_size {
                            (*self.registers).cr2 &= !(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
                            state.complete = true;
                            state.transfer_size = 0;
                        }
                    }
                }
                
                // Handle TX
                if sr & SPI_SR_TXE != 0 {
                    if state.tx_index < state.transfer_size {
                        (*self.registers).dr = state.tx_buffer[state.tx_index] as u32;
                        state.tx_index += 1;
                    } else {
                        (*self.registers).cr2 &= !SPI_CR2_TXEIE;
                    }
                }
            });
        }
    }
}

// Interrupt handler
#[interrupt]
fn SPI1() {
    static mut SPI: Option<InterruptSpi> = None;
    
    if let Some(spi) = SPI {
        spi.handle_interrupt();
    }
}

// Usage example
pub fn example_usage() {
    let mut spi = InterruptSpi::new(0x4001_3000);
    spi.init();
    
    let tx_data = [0x01, 0x02, 0x03, 0x04];
    
    match spi.transfer_async(&tx_data, 4) {
        Ok(_) => {
            // Transfer started, CPU can do other work
            while !spi.is_complete() {
                // Do other tasks or sleep
                cortex_m::asm::wfi();
            }
            
            let mut rx_data = [0u8; 4];
            let received = spi.get_received_data(&mut rx_data);
            // Process received data
        }
        Err(e) => {
            // Handle error
        }
    }
}
```

## Summary

**Interrupt-driven SPI** transforms blocking SPI communication into an asynchronous, non-blocking operation by leveraging hardware interrupts. The CPU initiates a transfer and immediately returns to other tasks, with an ISR handling data transmission and reception in the background. This approach dramatically improves system responsiveness, enables true multi-tasking, and reduces power consumption.

**Key benefits:**
- **CPU efficiency**: No polling loops wasting cycles
- **Responsiveness**: System can handle multiple tasks concurrently
- **Power savings**: CPU can sleep during transfers
- **Scalability**: Easier to manage multiple peripherals

**Implementation considerations:**
- Keep ISRs short and fast
- Use circular buffers for continuous streaming
- Protect shared data with critical sections/mutexes
- Consider DMA for high-throughput applications
- Handle error conditions and timeouts properly

This technique is essential for embedded systems requiring real-time performance and efficient resource utilization.