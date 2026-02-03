# DMA for High-Speed Transfer in Profibus Communication

## Overview

Direct Memory Access (DMA) is a hardware mechanism that enables peripheral devices to transfer data directly to/from system memory without CPU intervention. In Profibus communication, DMA significantly improves throughput and reduces CPU overhead, making it essential for high-speed, time-critical industrial automation applications.

## Why DMA Matters for Profibus

**Traditional Programmed I/O (PIO) Limitations:**
- CPU must handle every byte transfer
- High interrupt overhead for small data packets
- CPU cycles wasted on memory copying
- Poor performance with high-frequency Profibus traffic

**DMA Benefits:**
- CPU-independent data transfers
- Reduced interrupt frequency (per-buffer vs. per-byte)
- Lower latency and jitter
- Higher effective throughput
- CPU available for application logic

## Key Concepts

### DMA Transfer Modes
1. **Single Transfer**: One data unit per DMA request
2. **Block Transfer**: Entire block moved in one burst
3. **Demand Transfer**: Continues until peripheral deasserts request
4. **Scatter-Gather**: Multiple non-contiguous memory regions

### DMA Configuration Parameters
- **Source/Destination Addresses**: Memory and peripheral locations
- **Transfer Size**: Number of bytes to move
- **Transfer Width**: 8/16/32-bit data width
- **Channel Priority**: For systems with multiple DMA channels
- **Interrupt Configuration**: Transfer complete/error notifications

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// DMA Controller Register Definitions (platform-specific)
typedef struct {
    volatile uint32_t SAR;      // Source Address Register
    volatile uint32_t DAR;      // Destination Address Register
    volatile uint32_t TCR;      // Transfer Count Register
    volatile uint32_t CCR;      // Channel Control Register
    volatile uint32_t CSR;      // Channel Status Register
} DMA_Channel_t;

// Profibus DMA Configuration
typedef struct {
    uint8_t* rx_buffer;         // Receive buffer
    uint8_t* tx_buffer;         // Transmit buffer
    size_t buffer_size;         // Buffer size in bytes
    DMA_Channel_t* rx_channel;  // RX DMA channel
    DMA_Channel_t* tx_channel;  // TX DMA channel
    volatile bool rx_complete;  // RX completion flag
    volatile bool tx_complete;  // TX completion flag
} Profibus_DMA_t;

// DMA Control Register Flags
#define DMA_CCR_EN          (1 << 0)   // Channel Enable
#define DMA_CCR_TCIE        (1 << 1)   // Transfer Complete Interrupt Enable
#define DMA_CCR_HTIE        (1 << 2)   // Half Transfer Interrupt Enable
#define DMA_CCR_TEIE        (1 << 3)   // Transfer Error Interrupt Enable
#define DMA_CCR_DIR_M2P     (1 << 4)   // Memory to Peripheral
#define DMA_CCR_MINC        (1 << 7)   // Memory Increment Mode
#define DMA_CCR_PSIZE_8     (0 << 8)   // Peripheral Size 8-bit
#define DMA_CCR_MSIZE_8     (0 << 10)  // Memory Size 8-bit
#define DMA_CCR_CIRC        (1 << 5)   // Circular Mode

// Status Register Flags
#define DMA_CSR_TCIF        (1 << 1)   // Transfer Complete Interrupt Flag
#define DMA_CSR_TEIF        (1 << 3)   // Transfer Error Interrupt Flag

/**
 * Initialize DMA for Profibus RX
 */
bool profibus_dma_init_rx(Profibus_DMA_t* dma, 
                          void* uart_data_register,
                          uint8_t* buffer, 
                          size_t buffer_size)
{
    if (!dma || !buffer || buffer_size == 0) {
        return false;
    }
    
    // Configure RX DMA channel
    dma->rx_buffer = buffer;
    dma->buffer_size = buffer_size;
    dma->rx_complete = false;
    
    // Disable channel during configuration
    dma->rx_channel->CCR &= ~DMA_CCR_EN;
    
    // Configure source (UART peripheral)
    dma->rx_channel->SAR = (uint32_t)uart_data_register;
    
    // Configure destination (memory buffer)
    dma->rx_channel->DAR = (uint32_t)buffer;
    
    // Set transfer count
    dma->rx_channel->TCR = buffer_size;
    
    // Configure channel control:
    // - Peripheral to Memory
    // - Memory increment
    // - 8-bit transfers
    // - Circular mode for continuous reception
    // - Transfer complete interrupt
    dma->rx_channel->CCR = DMA_CCR_MINC | 
                           DMA_CCR_PSIZE_8 | 
                           DMA_CCR_MSIZE_8 |
                           DMA_CCR_CIRC |
                           DMA_CCR_TCIE |
                           DMA_CCR_TEIE;
    
    // Enable DMA channel
    dma->rx_channel->CCR |= DMA_CCR_EN;
    
    return true;
}

/**
 * Initialize DMA for Profibus TX
 */
bool profibus_dma_init_tx(Profibus_DMA_t* dma,
                          void* uart_data_register,
                          uint8_t* buffer,
                          size_t buffer_size)
{
    if (!dma || !buffer || buffer_size == 0) {
        return false;
    }
    
    dma->tx_buffer = buffer;
    dma->tx_complete = true; // Initially ready
    
    // Disable channel during configuration
    dma->tx_channel->CCR &= ~DMA_CCR_EN;
    
    // Configure source (memory buffer)
    dma->tx_channel->SAR = (uint32_t)buffer;
    
    // Configure destination (UART peripheral)
    dma->tx_channel->DAR = (uint32_t)uart_data_register;
    
    // Set initial transfer count (will be updated per transmission)
    dma->tx_channel->TCR = 0;
    
    // Configure channel control:
    // - Memory to Peripheral
    // - Memory increment
    // - 8-bit transfers
    // - Transfer complete interrupt
    dma->tx_channel->CCR = DMA_CCR_DIR_M2P |
                           DMA_CCR_MINC |
                           DMA_CCR_PSIZE_8 |
                           DMA_CCR_MSIZE_8 |
                           DMA_CCR_TCIE |
                           DMA_CCR_TEIE;
    
    return true;
}

/**
 * Start DMA transmission
 */
bool profibus_dma_transmit(Profibus_DMA_t* dma, 
                           const uint8_t* data, 
                           size_t length)
{
    if (!dma || !data || length == 0 || length > dma->buffer_size) {
        return false;
    }
    
    // Wait for previous transmission to complete
    while (!dma->tx_complete) {
        // Could add timeout here
    }
    
    // Copy data to TX buffer
    memcpy(dma->tx_buffer, data, length);
    
    // Disable channel to reconfigure
    dma->tx_channel->CCR &= ~DMA_CCR_EN;
    
    // Update transfer count and source address
    dma->tx_channel->TCR = length;
    dma->tx_channel->SAR = (uint32_t)dma->tx_buffer;
    
    // Mark as in-progress
    dma->tx_complete = false;
    
    // Enable DMA channel to start transfer
    dma->tx_channel->CCR |= DMA_CCR_EN;
    
    return true;
}

/**
 * DMA RX Interrupt Handler
 */
void profibus_dma_rx_irq_handler(Profibus_DMA_t* dma)
{
    uint32_t status = dma->rx_channel->CSR;
    
    // Check for transfer complete
    if (status & DMA_CSR_TCIF) {
        // Clear interrupt flag
        dma->rx_channel->CSR = DMA_CSR_TCIF;
        
        // Signal completion (in circular mode, this indicates buffer full)
        dma->rx_complete = true;
        
        // Process received data
        // profibus_process_received_data(dma->rx_buffer, dma->buffer_size);
    }
    
    // Check for transfer error
    if (status & DMA_CSR_TEIF) {
        // Clear error flag
        dma->rx_channel->CSR = DMA_CSR_TEIF;
        
        // Handle error (log, reset, etc.)
        // profibus_handle_dma_error();
    }
}

/**
 * DMA TX Interrupt Handler
 */
void profibus_dma_tx_irq_handler(Profibus_DMA_t* dma)
{
    uint32_t status = dma->tx_channel->CSR;
    
    // Check for transfer complete
    if (status & DMA_CSR_TCIF) {
        // Clear interrupt flag
        dma->tx_channel->CSR = DMA_CSR_TCIF;
        
        // Disable channel
        dma->tx_channel->CCR &= ~DMA_CCR_EN;
        
        // Signal completion
        dma->tx_complete = true;
    }
    
    // Check for transfer error
    if (status & DMA_CSR_TEIF) {
        // Clear error flag
        dma->tx_channel->CSR = DMA_CSR_TEIF;
        
        // Handle error
        dma->tx_complete = true; // Allow retry
    }
}

/**
 * Example: High-level Profibus frame transmission
 */
bool profibus_send_frame_dma(Profibus_DMA_t* dma, 
                             const uint8_t* frame_data,
                             size_t frame_length)
{
    // Add Profibus protocol overhead (start delimiter, checksum, etc.)
    uint8_t profibus_frame[256];
    size_t total_length = 0;
    
    // Start delimiter
    profibus_frame[total_length++] = 0x68; // SD2
    profibus_frame[total_length++] = frame_length;
    profibus_frame[total_length++] = frame_length;
    profibus_frame[total_length++] = 0x68;
    
    // Copy frame data
    memcpy(&profibus_frame[total_length], frame_data, frame_length);
    total_length += frame_length;
    
    // Calculate and append checksum
    uint8_t checksum = 0;
    for (size_t i = 4; i < total_length; i++) {
        checksum += profibus_frame[i];
    }
    profibus_frame[total_length++] = checksum;
    
    // End delimiter
    profibus_frame[total_length++] = 0x16;
    
    // Transmit via DMA
    return profibus_dma_transmit(dma, profibus_frame, total_length);
}
```

### Rust Implementation

```rust
use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicBool, Ordering};

/// DMA Channel Register Layout
#[repr(C)]
struct DmaChannel {
    sar: u32,  // Source Address Register
    dar: u32,  // Destination Address Register
    tcr: u32,  // Transfer Count Register
    ccr: u32,  // Channel Control Register
    csr: u32,  // Channel Status Register
}

/// DMA Control Register Flags
mod dma_flags {
    pub const CCR_EN: u32 = 1 << 0;
    pub const CCR_TCIE: u32 = 1 << 1;
    pub const CCR_TEIE: u32 = 1 << 3;
    pub const CCR_DIR_M2P: u32 = 1 << 4;
    pub const CCR_CIRC: u32 = 1 << 5;
    pub const CCR_MINC: u32 = 1 << 7;
    pub const CSR_TCIF: u32 = 1 << 1;
    pub const CSR_TEIF: u32 = 1 << 3;
}

/// Profibus DMA Configuration
pub struct ProfibusDma {
    rx_buffer: &'static mut [u8],
    tx_buffer: &'static mut [u8],
    rx_channel: *mut DmaChannel,
    tx_channel: *mut DmaChannel,
    rx_complete: AtomicBool,
    tx_complete: AtomicBool,
}

impl ProfibusDma {
    /// Create new Profibus DMA instance
    pub fn new(
        rx_channel: *mut DmaChannel,
        tx_channel: *mut DmaChannel,
        rx_buffer: &'static mut [u8],
        tx_buffer: &'static mut [u8],
    ) -> Self {
        Self {
            rx_buffer,
            tx_buffer,
            rx_channel,
            tx_channel,
            rx_complete: AtomicBool::new(false),
            tx_complete: AtomicBool::new(true),
        }
    }

    /// Initialize RX DMA channel
    pub unsafe fn init_rx(&mut self, uart_data_reg: *const u8) -> Result<(), &'static str> {
        let channel = &mut *self.rx_channel;

        // Disable channel during configuration
        let mut ccr = read_volatile(&channel.ccr);
        ccr &= !dma_flags::CCR_EN;
        write_volatile(&mut channel.ccr, ccr);

        // Configure addresses
        write_volatile(&mut channel.sar, uart_data_reg as u32);
        write_volatile(&mut channel.dar, self.rx_buffer.as_ptr() as u32);
        write_volatile(&mut channel.tcr, self.rx_buffer.len() as u32);

        // Configure control: P2M, increment, circular, interrupts
        let ccr_config = dma_flags::CCR_MINC
            | dma_flags::CCR_CIRC
            | dma_flags::CCR_TCIE
            | dma_flags::CCR_TEIE;

        write_volatile(&mut channel.ccr, ccr_config);

        // Enable channel
        let mut ccr = read_volatile(&channel.ccr);
        ccr |= dma_flags::CCR_EN;
        write_volatile(&mut channel.ccr, ccr);

        Ok(())
    }

    /// Initialize TX DMA channel
    pub unsafe fn init_tx(&mut self, uart_data_reg: *mut u8) -> Result<(), &'static str> {
        let channel = &mut *self.tx_channel;

        // Disable channel
        let mut ccr = read_volatile(&channel.ccr);
        ccr &= !dma_flags::CCR_EN;
        write_volatile(&mut channel.ccr, ccr);

        // Configure destination (UART)
        write_volatile(&mut channel.dar, uart_data_reg as u32);

        // Configure control: M2P, increment, interrupts
        let ccr_config = dma_flags::CCR_DIR_M2P
            | dma_flags::CCR_MINC
            | dma_flags::CCR_TCIE
            | dma_flags::CCR_TEIE;

        write_volatile(&mut channel.ccr, ccr_config);

        Ok(())
    }

    /// Transmit data via DMA
    pub unsafe fn transmit(&mut self, data: &[u8]) -> Result<(), &'static str> {
        if data.len() > self.tx_buffer.len() {
            return Err("Data exceeds buffer size");
        }

        // Wait for previous transfer to complete
        while !self.tx_complete.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }

        // Copy data to TX buffer
        self.tx_buffer[..data.len()].copy_from_slice(data);

        let channel = &mut *self.tx_channel;

        // Disable channel
        let mut ccr = read_volatile(&channel.ccr);
        ccr &= !dma_flags::CCR_EN;
        write_volatile(&mut channel.ccr, ccr);

        // Update transfer parameters
        write_volatile(&mut channel.sar, self.tx_buffer.as_ptr() as u32);
        write_volatile(&mut channel.tcr, data.len() as u32);

        // Mark as in-progress
        self.tx_complete.store(false, Ordering::Release);

        // Enable channel
        let mut ccr = read_volatile(&channel.ccr);
        ccr |= dma_flags::CCR_EN;
        write_volatile(&mut channel.ccr, ccr);

        Ok(())
    }

    /// RX DMA interrupt handler
    pub unsafe fn handle_rx_interrupt(&mut self) {
        let channel = &mut *self.rx_channel;
        let status = read_volatile(&channel.csr);

        if status & dma_flags::CSR_TCIF != 0 {
            // Clear flag
            write_volatile(&mut channel.csr, dma_flags::CSR_TCIF);

            // Signal completion
            self.rx_complete.store(true, Ordering::Release);

            // Process received data
            // self.process_received_data();
        }

        if status & dma_flags::CSR_TEIF != 0 {
            // Clear error flag
            write_volatile(&mut channel.csr, dma_flags::CSR_TEIF);

            // Handle error
            // self.handle_error();
        }
    }

    /// TX DMA interrupt handler
    pub unsafe fn handle_tx_interrupt(&mut self) {
        let channel = &mut *self.tx_channel;
        let status = read_volatile(&channel.csr);

        if status & dma_flags::CSR_TCIF != 0 {
            // Clear flag
            write_volatile(&mut channel.csr, dma_flags::CSR_TCIF);

            // Disable channel
            let mut ccr = read_volatile(&channel.ccr);
            ccr &= !dma_flags::CCR_EN;
            write_volatile(&mut channel.ccr, ccr);

            // Signal completion
            self.tx_complete.store(true, Ordering::Release);
        }

        if status & dma_flags::CSR_TEIF != 0 {
            write_volatile(&mut channel.csr, dma_flags::CSR_TEIF);
            self.tx_complete.store(true, Ordering::Release);
        }
    }
}

/// Profibus Frame Builder with DMA Support
pub struct ProfibusFrameBuilder {
    buffer: [u8; 256],
    length: usize,
}

impl ProfibusFrameBuilder {
    pub fn new() -> Self {
        Self {
            buffer: [0u8; 256],
            length: 0,
        }
    }

    /// Build a Profibus frame with protocol overhead
    pub fn build_frame(&mut self, data: &[u8]) -> Result<&[u8], &'static str> {
        if data.len() > 246 {
            return Err("Data too large");
        }

        self.length = 0;

        // Start delimiter (SD2)
        self.buffer[self.length] = 0x68;
        self.length += 1;

        // Length fields
        self.buffer[self.length] = data.len() as u8;
        self.length += 1;
        self.buffer[self.length] = data.len() as u8;
        self.length += 1;
        self.buffer[self.length] = 0x68;
        self.length += 1;

        let checksum_start = self.length;

        // Copy payload
        self.buffer[self.length..self.length + data.len()].copy_from_slice(data);
        self.length += data.len();

        // Calculate checksum
        let mut checksum: u8 = 0;
        for i in checksum_start..self.length {
            checksum = checksum.wrapping_add(self.buffer[i]);
        }
        self.buffer[self.length] = checksum;
        self.length += 1;

        // End delimiter
        self.buffer[self.length] = 0x16;
        self.length += 1;

        Ok(&self.buffer[..self.length])
    }
}

/// Example usage
pub unsafe fn example_profibus_dma_usage() {
    // Allocate static buffers
    static mut RX_BUFFER: [u8; 512] = [0u8; 512];
    static mut TX_BUFFER: [u8; 512] = [0u8; 512];

    // Initialize DMA (addresses would be platform-specific)
    let rx_channel = 0x4002_0008 as *mut DmaChannel;
    let tx_channel = 0x4002_001C as *mut DmaChannel;
    let uart_data = 0x4001_3804 as *mut u8;

    let mut dma = ProfibusDma::new(
        rx_channel,
        tx_channel,
        &mut RX_BUFFER,
        &mut TX_BUFFER,
    );

    dma.init_rx(uart_data).unwrap();
    dma.init_tx(uart_data).unwrap();

    // Build and send a frame
    let mut frame_builder = ProfibusFrameBuilder::new();
    let payload = &[0x01, 0x02, 0x03, 0x04];
    let frame = frame_builder.build_frame(payload).unwrap();

    dma.transmit(frame).unwrap();
}
```

## Summary

DMA for high-speed Profibus transfer offloads data movement from the CPU to dedicated hardware, dramatically improving system efficiency and communication performance. Key advantages include:

- **Reduced CPU overhead**: CPU freed for application logic while DMA handles byte-level transfers
- **Lower latency**: Deterministic transfer timing without software interrupt overhead
- **Higher throughput**: Sustained high-speed communication without CPU bottlenecks
- **Better real-time performance**: Reduced jitter in time-critical industrial control systems

Implementation requires careful configuration of DMA channels, proper interrupt handling, and buffer management strategies (circular buffers for RX, ping-pong buffers for TX). The examples demonstrate platform-agnostic approaches using both C/C++ and Rust, with memory-safe patterns in Rust using atomic operations and volatile access for hardware registers.

For production Profibus systems operating at 12 Mbps, DMA is essential to maintain protocol timing requirements while supporting multiple slaves and complex diagnostic operations.