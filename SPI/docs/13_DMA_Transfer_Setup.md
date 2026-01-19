# DMA Transfer Setup for SPI Communication

## Overview

Direct Memory Access (DMA) is a hardware feature that enables peripheral devices like SPI controllers to transfer data directly to and from memory without CPU intervention. This dramatically improves efficiency for large data transfers by freeing the CPU to perform other tasks while data movement happens in the background.

## Why DMA Matters for SPI

Traditional SPI communication requires the CPU to manually move each byte between the SPI peripheral and memory. For high-speed or bulk transfers (like reading from flash memory, SD cards, or display controllers), this creates significant CPU overhead. DMA solves this by:

- **Eliminating CPU polling**: No need to check status registers repeatedly
- **Reducing interrupt overhead**: One interrupt at completion vs. one per byte
- **Enabling concurrent operations**: CPU can execute other code during transfers
- **Improving throughput**: Hardware-based transfers are faster and more predictable

## DMA Architecture Concepts

### Key Components

1. **DMA Controller**: Hardware unit managing memory-to-peripheral transfers
2. **Channels/Streams**: Independent transfer pipelines (typically 8-16 per controller)
3. **Transfer Descriptors**: Configuration structures defining source, destination, and size
4. **Circular vs. Normal Mode**: Continuous vs. one-shot transfers
5. **Priority Levels**: Arbitration when multiple channels request access

### Transfer Flow

1. Configure DMA channel with source/destination addresses
2. Set transfer size and data width
3. Enable peripheral DMA requests
4. Start DMA transfer
5. DMA controller moves data autonomously
6. Interrupt fires on completion (or error)

## C/C++ Implementation Example

Here's a comprehensive example for an STM32 microcontroller (adaptable to other platforms):

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (STM32-style)
typedef struct {
    volatile uint32_t CR;      // Control register
    volatile uint32_t SR;      // Status register
    volatile uint32_t DR;      // Data register
    volatile uint32_t CRCPR;   // CRC polynomial
    volatile uint32_t RXCRCR;  // RX CRC
    volatile uint32_t TXCRCR;  // TX CRC
} SPI_TypeDef;

typedef struct {
    volatile uint32_t CCR;     // Channel configuration
    volatile uint32_t CNDTR;   // Number of data register
    volatile uint32_t CPAR;    // Peripheral address
    volatile uint32_t CMAR;    // Memory address
} DMA_Channel_TypeDef;

// DMA configuration structure
typedef struct {
    uint32_t direction;        // Memory-to-peripheral or peripheral-to-memory
    uint32_t peripheral_inc;   // Peripheral address increment mode
    uint32_t memory_inc;       // Memory address increment mode
    uint32_t data_size;        // Data transfer width (8/16/32-bit)
    uint32_t mode;             // Normal or circular
    uint32_t priority;         // Channel priority
} DMA_Config_t;

// Configuration constants
#define DMA_DIR_PERIPH_TO_MEM  0
#define DMA_DIR_MEM_TO_PERIPH  1
#define DMA_PINC_DISABLE       0
#define DMA_MINC_ENABLE        1
#define DMA_SIZE_8BIT          0
#define DMA_SIZE_16BIT         1
#define DMA_MODE_NORMAL        0
#define DMA_MODE_CIRCULAR      1
#define DMA_PRIORITY_HIGH      2

// Register bit definitions
#define DMA_CCR_EN      (1U << 0)
#define DMA_CCR_TCIE    (1U << 1)  // Transfer complete interrupt
#define DMA_CCR_HTIE    (1U << 2)  // Half transfer interrupt
#define DMA_CCR_TEIE    (1U << 3)  // Transfer error interrupt
#define SPI_CR2_TXDMAEN (1U << 1)
#define SPI_CR2_RXDMAEN (1U << 0)

// Function to configure DMA channel for SPI TX
void DMA_SPI_TX_Config(DMA_Channel_TypeDef* dma_channel, 
                       SPI_TypeDef* spi,
                       const uint8_t* tx_buffer, 
                       uint16_t length) {
    
    // Disable DMA channel during configuration
    dma_channel->CCR &= ~DMA_CCR_EN;
    
    // Configure transfer direction (memory to peripheral)
    dma_channel->CCR &= ~(1U << 4);  // Clear DIR bit
    dma_channel->CCR |= (DMA_DIR_MEM_TO_PERIPH << 4);
    
    // Peripheral address does not increment
    dma_channel->CCR &= ~(1U << 6);
    
    // Memory address increments
    dma_channel->CCR |= (DMA_MINC_ENABLE << 7);
    
    // 8-bit data size for both peripheral and memory
    dma_channel->CCR &= ~((3U << 8) | (3U << 10));
    
    // Normal mode (not circular)
    dma_channel->CCR &= ~(1U << 5);
    
    // High priority
    dma_channel->CCR |= (DMA_PRIORITY_HIGH << 12);
    
    // Enable transfer complete interrupt
    dma_channel->CCR |= DMA_CCR_TCIE;
    
    // Set addresses
    dma_channel->CPAR = (uint32_t)&(spi->DR);    // SPI data register
    dma_channel->CMAR = (uint32_t)tx_buffer;      // Memory buffer
    
    // Set transfer size
    dma_channel->CNDTR = length;
    
    // Enable SPI TX DMA request
    spi->CR2 |= SPI_CR2_TXDMAEN;
}

// Function to configure DMA channel for SPI RX
void DMA_SPI_RX_Config(DMA_Channel_TypeDef* dma_channel,
                       SPI_TypeDef* spi,
                       uint8_t* rx_buffer,
                       uint16_t length) {
    
    // Disable channel
    dma_channel->CCR &= ~DMA_CCR_EN;
    
    // Peripheral to memory
    dma_channel->CCR &= ~(1U << 4);
    
    // Memory address increments
    dma_channel->CCR |= (DMA_MINC_ENABLE << 7);
    
    // 8-bit transfers
    dma_channel->CCR &= ~((3U << 8) | (3U << 10));
    
    // High priority
    dma_channel->CCR |= (DMA_PRIORITY_HIGH << 12);
    
    // Enable transfer complete interrupt
    dma_channel->CCR |= DMA_CCR_TCIE;
    
    // Set addresses
    dma_channel->CPAR = (uint32_t)&(spi->DR);
    dma_channel->CMAR = (uint32_t)rx_buffer;
    dma_channel->CNDTR = length;
    
    // Enable SPI RX DMA request
    spi->CR2 |= SPI_CR2_RXDMAEN;
}

// Start DMA transfer
void DMA_Start_Transfer(DMA_Channel_TypeDef* dma_channel) {
    dma_channel->CCR |= DMA_CCR_EN;
}

// Check if transfer is complete
bool DMA_Transfer_Complete(DMA_Channel_TypeDef* dma_channel, uint32_t* status_reg) {
    return (*status_reg & (1U << 1)) != 0;  // TCIF flag
}

// Full duplex SPI transfer with DMA
typedef struct {
    volatile bool tx_complete;
    volatile bool rx_complete;
    volatile bool error;
} DMA_Transfer_State_t;

static DMA_Transfer_State_t transfer_state;

void SPI_DMA_FullDuplex_Transfer(SPI_TypeDef* spi,
                                 DMA_Channel_TypeDef* tx_channel,
                                 DMA_Channel_TypeDef* rx_channel,
                                 const uint8_t* tx_data,
                                 uint8_t* rx_data,
                                 uint16_t length) {
    
    // Initialize state
    transfer_state.tx_complete = false;
    transfer_state.rx_complete = false;
    transfer_state.error = false;
    
    // Configure both channels
    DMA_SPI_RX_Config(rx_channel, spi, rx_data, length);
    DMA_SPI_TX_Config(tx_channel, spi, tx_data, length);
    
    // Start RX first (important for full duplex)
    DMA_Start_Transfer(rx_channel);
    
    // Then start TX
    DMA_Start_Transfer(tx_channel);
}

// Interrupt handlers (implementation depends on platform)
void DMA1_Channel2_IRQHandler(void) {  // Example RX channel
    extern volatile uint32_t DMA1_ISR;
    
    if (DMA1_ISR & (1U << 5)) {  // TCIF2 - transfer complete
        transfer_state.rx_complete = true;
        DMA1_ISR |= (1U << 5);  // Clear flag
    }
    
    if (DMA1_ISR & (1U << 7)) {  // TEIF2 - transfer error
        transfer_state.error = true;
        DMA1_ISR |= (1U << 7);
    }
}

void DMA1_Channel3_IRQHandler(void) {  // Example TX channel
    extern volatile uint32_t DMA1_ISR;
    
    if (DMA1_ISR & (1U << 9)) {  // TCIF3
        transfer_state.tx_complete = true;
        DMA1_ISR |= (1U << 9);
    }
    
    if (DMA1_ISR & (1U << 11)) {  // TEIF3
        transfer_state.error = true;
        DMA1_ISR |= (1U << 11);
    }
}

// Example usage
void example_usage(void) {
    uint8_t tx_buffer[256] = {0x01, 0x02, 0x03, /* ... */};
    uint8_t rx_buffer[256] = {0};
    
    extern SPI_TypeDef* SPI1;
    extern DMA_Channel_TypeDef* DMA1_Channel2;  // RX
    extern DMA_Channel_TypeDef* DMA1_Channel3;  // TX
    
    // Start transfer
    SPI_DMA_FullDuplex_Transfer(SPI1, DMA1_Channel3, DMA1_Channel2,
                                tx_buffer, rx_buffer, 256);
    
    // Wait for completion
    while (!transfer_state.rx_complete || !transfer_state.tx_complete) {
        // CPU is free to do other work here
        if (transfer_state.error) {
            // Handle error
            break;
        }
    }
    
    // Process received data
    for (int i = 0; i < 256; i++) {
        // Use rx_buffer[i]
    }
}
```

## Rust Implementation Example

Here's a Rust implementation using embedded-hal traits and type-safe abstractions:

```rust
#![no_std]

use core::sync::atomic::{AtomicBool, Ordering};
use embedded_hal::spi::SpiDevice;

// Hardware abstraction (would come from PAC/HAL crate)
#[repr(C)]
struct DmaChannel {
    ccr: u32,      // Configuration register
    cndtr: u32,    // Number of data register
    cpar: u32,     // Peripheral address
    cmar: u32,     // Memory address
}

#[repr(C)]
struct SpiRegisters {
    cr1: u32,
    cr2: u32,
    sr: u32,
    dr: u32,
}

// DMA transfer direction
#[derive(Copy, Clone)]
pub enum DmaDirection {
    PeripheralToMemory = 0,
    MemoryToPeripheral = 1,
}

// DMA data size
#[derive(Copy, Clone)]
pub enum DmaDataSize {
    Bits8 = 0,
    Bits16 = 1,
    Bits32 = 2,
}

// DMA priority level
#[derive(Copy, Clone)]
pub enum DmaPriority {
    Low = 0,
    Medium = 1,
    High = 2,
    VeryHigh = 3,
}

// DMA configuration
pub struct DmaConfig {
    pub direction: DmaDirection,
    pub memory_increment: bool,
    pub peripheral_increment: bool,
    pub data_size: DmaDataSize,
    pub priority: DmaPriority,
    pub circular: bool,
}

impl Default for DmaConfig {
    fn default() -> Self {
        Self {
            direction: DmaDirection::MemoryToPeripheral,
            memory_increment: true,
            peripheral_increment: false,
            data_size: DmaDataSize::Bits8,
            priority: DmaPriority::High,
            circular: false,
        }
    }
}

// Safe DMA transfer wrapper
pub struct DmaTransfer<'a, B: 'a> {
    channel: &'a mut DmaChannel,
    buffer: &'a mut [B],
    complete: &'a AtomicBool,
}

impl<'a, B> DmaTransfer<'a, B> {
    pub fn is_complete(&self) -> bool {
        self.complete.load(Ordering::Acquire)
    }
    
    pub fn wait(self) -> &'a mut [B] {
        while !self.is_complete() {
            core::hint::spin_loop();
        }
        self.buffer
    }
}

impl<'a, B> Drop for DmaTransfer<'a, B> {
    fn drop(&mut self) {
        // Ensure DMA is stopped if transfer is dropped early
        self.channel.ccr &= !(1 << 0); // Clear EN bit
    }
}

// SPI with DMA support
pub struct SpiDma<'a> {
    spi: &'a mut SpiRegisters,
    tx_channel: &'a mut DmaChannel,
    rx_channel: &'a mut DmaChannel,
    tx_complete: AtomicBool,
    rx_complete: AtomicBool,
}

impl<'a> SpiDma<'a> {
    pub fn new(
        spi: &'a mut SpiRegisters,
        tx_channel: &'a mut DmaChannel,
        rx_channel: &'a mut DmaChannel,
    ) -> Self {
        Self {
            spi,
            tx_channel,
            rx_channel,
            tx_complete: AtomicBool::new(false),
            rx_complete: AtomicBool::new(false),
        }
    }
    
    // Configure DMA channel
    fn configure_channel(
        channel: &mut DmaChannel,
        peripheral_addr: u32,
        memory_addr: u32,
        data_count: u16,
        config: &DmaConfig,
    ) {
        // Disable channel
        channel.ccr &= !(1 << 0);
        
        // Build configuration register value
        let mut ccr = 0u32;
        
        // Direction
        ccr |= (config.direction as u32) << 4;
        
        // Memory increment
        if config.memory_increment {
            ccr |= 1 << 7;
        }
        
        // Peripheral increment
        if config.peripheral_increment {
            ccr |= 1 << 6;
        }
        
        // Data size (both memory and peripheral)
        ccr |= (config.data_size as u32) << 8;
        ccr |= (config.data_size as u32) << 10;
        
        // Priority
        ccr |= (config.priority as u32) << 12;
        
        // Circular mode
        if config.circular {
            ccr |= 1 << 5;
        }
        
        // Enable transfer complete interrupt
        ccr |= 1 << 1;
        
        // Apply configuration
        channel.ccr = ccr;
        channel.cpar = peripheral_addr;
        channel.cmar = memory_addr;
        channel.cndtr = data_count as u32;
    }
    
    // Transfer data using DMA
    pub fn transfer<'b>(
        &'b mut self,
        tx_data: &'b [u8],
        rx_data: &'b mut [u8],
    ) -> Result<(), &'static str> {
        if tx_data.len() != rx_data.len() {
            return Err("TX and RX buffers must be same length");
        }
        
        if tx_data.len() > u16::MAX as usize {
            return Err("Transfer too large");
        }
        
        // Reset completion flags
        self.tx_complete.store(false, Ordering::Release);
        self.rx_complete.store(false, Ordering::Release);
        
        let len = tx_data.len() as u16;
        let spi_dr_addr = &self.spi.dr as *const u32 as u32;
        
        // Configure RX channel
        let rx_config = DmaConfig {
            direction: DmaDirection::PeripheralToMemory,
            memory_increment: true,
            peripheral_increment: false,
            data_size: DmaDataSize::Bits8,
            priority: DmaPriority::High,
            circular: false,
        };
        
        Self::configure_channel(
            self.rx_channel,
            spi_dr_addr,
            rx_data.as_mut_ptr() as u32,
            len,
            &rx_config,
        );
        
        // Configure TX channel
        let tx_config = DmaConfig {
            direction: DmaDirection::MemoryToPeripheral,
            memory_increment: true,
            peripheral_increment: false,
            data_size: DmaDataSize::Bits8,
            priority: DmaPriority::High,
            circular: false,
        };
        
        Self::configure_channel(
            self.tx_channel,
            spi_dr_addr,
            tx_data.as_ptr() as u32,
            len,
            &tx_config,
        );
        
        // Enable DMA requests on SPI peripheral
        self.spi.cr2 |= (1 << 1) | (1 << 0); // TXDMAEN | RXDMAEN
        
        // Start RX first, then TX
        self.rx_channel.ccr |= 1 << 0; // Enable
        self.tx_channel.ccr |= 1 << 0; // Enable
        
        Ok(())
    }
    
    // Wait for transfer completion
    pub fn wait_complete(&self) {
        while !self.tx_complete.load(Ordering::Acquire) 
            || !self.rx_complete.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }
    }
    
    // Called from interrupt handler
    pub fn handle_tx_interrupt(&self) {
        self.tx_complete.store(true, Ordering::Release);
    }
    
    pub fn handle_rx_interrupt(&self) {
        self.rx_complete.store(true, Ordering::Release);
    }
}

// Example usage with type safety
pub fn example_spi_dma_transfer() {
    // These would come from HAL/PAC
    let spi_regs: &mut SpiRegisters = unsafe { &mut *(0x4001_3000 as *mut _) };
    let tx_dma: &mut DmaChannel = unsafe { &mut *(0x4002_0014 as *mut _) };
    let rx_dma: &mut DmaChannel = unsafe { &mut *(0x4002_0028 as *mut _) };
    
    let mut spi = SpiDma::new(spi_regs, tx_dma, rx_dma);
    
    let tx_buffer = [0x01u8, 0x02, 0x03, 0x04, 0x05];
    let mut rx_buffer = [0u8; 5];
    
    // Start DMA transfer
    spi.transfer(&tx_buffer, &mut rx_buffer).unwrap();
    
    // CPU is free to do other work here
    
    // Wait for completion
    spi.wait_complete();
    
    // Use received data
    for byte in &rx_buffer {
        // Process data
    }
}

// Zero-copy circular buffer implementation
pub struct CircularDmaBuffer<const N: usize> {
    buffer: [u8; N],
    read_pos: usize,
}

impl<const N: usize> CircularDmaBuffer<N> {
    pub const fn new() -> Self {
        Self {
            buffer: [0u8; N],
            read_pos: 0,
        }
    }
    
    pub fn setup_circular_rx(&mut self, channel: &mut DmaChannel, spi_dr: u32) {
        let config = DmaConfig {
            direction: DmaDirection::PeripheralToMemory,
            memory_increment: true,
            peripheral_increment: false,
            data_size: DmaDataSize::Bits8,
            priority: DmaPriority::High,
            circular: true, // Continuous reception
        };
        
        SpiDma::configure_channel(
            channel,
            spi_dr,
            self.buffer.as_mut_ptr() as u32,
            N as u16,
            &config,
        );
        
        // Enable channel
        channel.ccr |= 1 << 0;
    }
    
    pub fn read_available(&mut self, channel: &DmaChannel) -> &[u8] {
        // Calculate how much data DMA has written
        let dma_write_pos = N - (channel.cndtr as usize);
        
        let available = if dma_write_pos >= self.read_pos {
            &self.buffer[self.read_pos..dma_write_pos]
        } else {
            // Wrapped around - return first segment
            &self.buffer[self.read_pos..]
        };
        
        self.read_pos = dma_write_pos;
        available
    }
}
```

## Summary

DMA transfer setup for SPI enables high-performance, CPU-efficient data communication by offloading byte-by-byte transfers to dedicated hardware. Key aspects include:

**Benefits:**
- **Performance**: Eliminates CPU polling and reduces interrupt overhead
- **Efficiency**: Frees CPU for concurrent tasks during transfers
- **Scalability**: Essential for high-speed peripherals and large data volumes

**Configuration Steps:**
1. Configure DMA channel (direction, addresses, size, priority)
2. Enable peripheral DMA requests (SPI TX/RX DMA enable bits)
3. Start DMA transfer
4. Handle completion via interrupts or polling

**Best Practices:**
- Start RX DMA before TX for full-duplex to avoid data loss
- Use memory alignment appropriate for data width
- Implement proper error handling for transfer errors
- Consider circular buffers for continuous streaming applications
- Ensure buffers remain valid during entire transfer (no stack allocation for async transfers)

Both C and Rust implementations demonstrate proper abstractions: C focuses on direct register manipulation with clear documentation, while Rust adds type safety, ownership semantics, and zero-cost abstractions to prevent common errors like buffer invalidation during transfers.