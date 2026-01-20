# Burst Mode Transfers in SPI

## Detailed Description

Burst mode transfers are a critical optimization technique in SPI (Serial Peripheral Interface) communication that allows continuous transmission of multiple bytes without deasserting the chip select (CS) line between individual bytes. This dramatically improves throughput and reduces overhead compared to single-byte transactions.

### How Burst Mode Works

In standard SPI communication, each byte transfer might involve:
1. Assert CS (pull low)
2. Transfer byte
3. Deassert CS (pull high)
4. Repeat for next byte

In burst mode:
1. Assert CS once
2. Transfer multiple bytes continuously
3. Deassert CS once

This eliminates the CS toggling overhead and allows the SPI peripheral to maintain continuous clock generation, resulting in much higher effective data rates.

### Key Benefits

- **Higher Throughput**: Eliminates inter-byte gaps and CS toggling delays
- **Reduced CPU Overhead**: Fewer function calls and state transitions
- **Better Bus Utilization**: Maintains continuous clock generation
- **Lower Latency**: Reduces total transaction time for multi-byte operations
- **Power Efficiency**: Fewer state transitions mean lower power consumption

### Common Use Cases

- **Memory Devices**: Reading/writing blocks of data to/from SPI flash or EEPROM
- **Sensor Data**: Retrieving multiple register values in one transaction
- **Display Updates**: Sending framebuffer data to SPI displays
- **ADC/DAC Operations**: Streaming continuous samples
- **Communication Protocols**: Implementing packet-based protocols over SPI

## Code Examples

### C/C++ Implementation

```c
// spi_burst.h
#ifndef SPI_BURST_H
#define SPI_BURST_H

#include <stdint.h>
#include <stddef.h>

// SPI configuration structure
typedef struct {
    uint32_t clock_speed;
    uint8_t mode;           // 0-3 (CPOL/CPHA combinations)
    uint8_t bit_order;      // 0 = MSB first, 1 = LSB first
} spi_config_t;

// SPI burst transfer structure
typedef struct {
    uint8_t *tx_buffer;     // Transmit buffer (NULL for receive-only)
    uint8_t *rx_buffer;     // Receive buffer (NULL for transmit-only)
    size_t length;          // Number of bytes to transfer
    uint8_t cs_pin;         // Chip select pin number
} spi_burst_t;

// Initialize SPI peripheral
int spi_init(spi_config_t *config);

// Perform burst mode transfer
int spi_burst_transfer(spi_burst_t *transfer);

// High-level helper functions
int spi_burst_write(uint8_t cs_pin, const uint8_t *data, size_t length);
int spi_burst_read(uint8_t cs_pin, uint8_t *data, size_t length);
int spi_burst_exchange(uint8_t cs_pin, const uint8_t *tx_data, 
                       uint8_t *rx_data, size_t length);

#endif // SPI_BURST_H
```

```c
// spi_burst.c
#include "spi_burst.h"
#include <string.h>

// Hardware-specific register definitions (example for ARM Cortex-M)
#define SPI1_BASE       0x40013000
#define SPI_CR1         (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI_CR2         (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI_SR          (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI_DR          (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

// Status register flags
#define SPI_SR_TXE      (1 << 1)  // Transmit buffer empty
#define SPI_SR_RXNE     (1 << 0)  // Receive buffer not empty
#define SPI_SR_BSY      (1 << 7)  // Busy flag

// Control register flags
#define SPI_CR1_SPE     (1 << 6)  // SPI enable
#define SPI_CR1_MSTR    (1 << 2)  // Master mode
#define SPI_CR1_SSM     (1 << 9)  // Software slave management
#define SPI_CR1_SSI     (1 << 8)  // Internal slave select

// GPIO control (simplified)
static void gpio_set_cs(uint8_t pin, uint8_t state) {
    // Platform-specific GPIO control
    // state: 0 = low (active), 1 = high (inactive)
}

int spi_init(spi_config_t *config) {
    // Configure SPI peripheral
    SPI_CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    
    // Set clock divider based on speed
    // Set mode (CPOL/CPHA)
    // Enable SPI
    SPI_CR1 |= SPI_CR1_SPE;
    
    return 0;
}

int spi_burst_transfer(spi_burst_t *transfer) {
    if (!transfer || transfer->length == 0) {
        return -1;
    }
    
    uint8_t *tx_ptr = transfer->tx_buffer;
    uint8_t *rx_ptr = transfer->rx_buffer;
    size_t remaining = transfer->length;
    
    // Assert chip select (active low)
    gpio_set_cs(transfer->cs_pin, 0);
    
    // Perform burst transfer
    while (remaining > 0) {
        // Wait for transmit buffer empty
        while (!(SPI_SR & SPI_SR_TXE));
        
        // Send byte (or dummy byte if tx_buffer is NULL)
        SPI_DR = tx_ptr ? *tx_ptr++ : 0xFF;
        
        // Wait for receive buffer not empty
        while (!(SPI_SR & SPI_SR_RXNE));
        
        // Read received byte
        uint8_t received = SPI_DR;
        if (rx_ptr) {
            *rx_ptr++ = received;
        }
        
        remaining--;
    }
    
    // Wait for SPI to finish
    while (SPI_SR & SPI_SR_BSY);
    
    // Deassert chip select
    gpio_set_cs(transfer->cs_pin, 1);
    
    return 0;
}

int spi_burst_write(uint8_t cs_pin, const uint8_t *data, size_t length) {
    spi_burst_t transfer = {
        .tx_buffer = (uint8_t *)data,
        .rx_buffer = NULL,
        .length = length,
        .cs_pin = cs_pin
    };
    return spi_burst_transfer(&transfer);
}

int spi_burst_read(uint8_t cs_pin, uint8_t *data, size_t length) {
    spi_burst_t transfer = {
        .tx_buffer = NULL,
        .rx_buffer = data,
        .length = length,
        .cs_pin = cs_pin
    };
    return spi_burst_transfer(&transfer);
}

int spi_burst_exchange(uint8_t cs_pin, const uint8_t *tx_data, 
                       uint8_t *rx_data, size_t length) {
    spi_burst_t transfer = {
        .tx_buffer = (uint8_t *)tx_data,
        .rx_buffer = rx_data,
        .length = length,
        .cs_pin = cs_pin
    };
    return spi_burst_transfer(&transfer);
}
```

```cpp
// Example usage: SPI Flash burst operations
#include "spi_burst.h"
#include <stdio.h>

#define FLASH_CS_PIN    10
#define FLASH_CMD_READ  0x03
#define FLASH_PAGE_SIZE 256

// Read data from SPI flash using burst mode
int flash_read_burst(uint32_t address, uint8_t *buffer, size_t length) {
    // Prepare command sequence: CMD + 24-bit address
    uint8_t cmd[4] = {
        FLASH_CMD_READ,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };
    
    // Send command and address
    spi_burst_write(FLASH_CS_PIN, cmd, 4);
    
    // Read data in burst mode
    return spi_burst_read(FLASH_CS_PIN, buffer, length);
}

// Optimized burst write with DMA support
class SPIBurstDMA {
private:
    volatile bool transfer_complete;
    
public:
    SPIBurstDMA() : transfer_complete(false) {}
    
    // DMA-accelerated burst transfer
    int transfer_dma(uint8_t cs_pin, const uint8_t *tx_data, 
                     uint8_t *rx_data, size_t length) {
        transfer_complete = false;
        
        // Configure DMA channels for SPI TX and RX
        // This is hardware-specific
        
        // Assert CS
        gpio_set_cs(cs_pin, 0);
        
        // Enable DMA and start transfer
        // Start DMA TX channel
        // Start DMA RX channel
        
        // Wait for completion (or use interrupt)
        while (!transfer_complete) {
            // Could use interrupt or RTOS wait
        }
        
        // Deassert CS
        gpio_set_cs(cs_pin, 1);
        
        return 0;
    }
    
    // DMA interrupt handler (called by hardware)
    void dma_complete_handler() {
        transfer_complete = true;
    }
};

// Performance comparison example
void performance_test() {
    uint8_t buffer[1024];
    
    // Method 1: Single byte transfers (slow)
    uint32_t start = get_timer_us();
    for (int i = 0; i < 1024; i++) {
        spi_burst_write(FLASH_CS_PIN, &buffer[i], 1);
    }
    uint32_t single_time = get_timer_us() - start;
    
    // Method 2: Burst transfer (fast)
    start = get_timer_us();
    spi_burst_write(FLASH_CS_PIN, buffer, 1024);
    uint32_t burst_time = get_timer_us() - start;
    
    printf("Single-byte: %lu us\n", single_time);
    printf("Burst mode: %lu us\n", burst_time);
    printf("Speedup: %.2fx\n", (float)single_time / burst_time);
}
```

### Rust Implementation

```rust
// spi_burst.rs
use core::ptr::{read_volatile, write_volatile};

/// SPI mode configuration
#[derive(Clone, Copy)]
pub enum SpiMode {
    Mode0 = 0, // CPOL=0, CPHA=0
    Mode1 = 1, // CPOL=0, CPHA=1
    Mode2 = 2, // CPOL=1, CPHA=0
    Mode3 = 3, // CPOL=1, CPHA=1
}

/// SPI configuration
pub struct SpiConfig {
    pub clock_speed: u32,
    pub mode: SpiMode,
    pub msb_first: bool,
}

/// SPI burst transfer descriptor
pub struct SpiBurstTransfer<'a> {
    pub tx_buffer: Option<&'a [u8]>,
    pub rx_buffer: Option<&'a mut [u8]>,
    pub cs_pin: u8,
}

/// SPI peripheral abstraction with burst mode support
pub struct SpiBurst {
    base_addr: usize,
}

impl SpiBurst {
    /// Create new SPI peripheral instance
    pub const fn new(base_addr: usize) -> Self {
        Self { base_addr }
    }
    
    /// Initialize SPI peripheral
    pub fn init(&mut self, config: &SpiConfig) -> Result<(), SpiError> {
        // Configure SPI registers
        unsafe {
            let cr1 = self.base_addr as *mut u32;
            
            // Enable master mode, software slave management
            let mut cr1_val = (1 << 2) | (1 << 9) | (1 << 8);
            
            // Set clock polarity and phase based on mode
            match config.mode {
                SpiMode::Mode0 => {},
                SpiMode::Mode1 => cr1_val |= 1 << 0,
                SpiMode::Mode2 => cr1_val |= 1 << 1,
                SpiMode::Mode3 => cr1_val |= (1 << 0) | (1 << 1),
            }
            
            // Set bit order
            if !config.msb_first {
                cr1_val |= 1 << 7;
            }
            
            // Enable SPI
            cr1_val |= 1 << 6;
            
            write_volatile(cr1, cr1_val);
        }
        
        Ok(())
    }
    
    /// Perform burst mode transfer
    pub fn burst_transfer(&mut self, transfer: SpiBurstTransfer) -> Result<(), SpiError> {
        let length = match (&transfer.tx_buffer, &transfer.rx_buffer) {
            (Some(tx), _) => tx.len(),
            (None, Some(rx)) => rx.len(),
            (None, None) => return Err(SpiError::InvalidParameter),
        };
        
        // Assert chip select
        self.set_cs(transfer.cs_pin, false);
        
        // Perform transfer
        for i in 0..length {
            // Get byte to transmit (or dummy byte)
            let tx_byte = transfer.tx_buffer
                .map(|buf| buf[i])
                .unwrap_or(0xFF);
            
            // Wait for TX buffer empty
            while !self.is_tx_empty() {}
            
            // Send byte
            self.write_data(tx_byte);
            
            // Wait for RX buffer not empty
            while !self.is_rx_ready() {}
            
            // Read received byte
            let rx_byte = self.read_data();
            
            // Store if RX buffer provided
            if let Some(ref mut rx_buf) = transfer.rx_buffer {
                rx_buf[i] = rx_byte;
            }
        }
        
        // Wait for completion
        while self.is_busy() {}
        
        // Deassert chip select
        self.set_cs(transfer.cs_pin, true);
        
        Ok(())
    }
    
    /// Write-only burst transfer
    pub fn burst_write(&mut self, cs_pin: u8, data: &[u8]) -> Result<(), SpiError> {
        let transfer = SpiBurstTransfer {
            tx_buffer: Some(data),
            rx_buffer: None,
            cs_pin,
        };
        self.burst_transfer(transfer)
    }
    
    /// Read-only burst transfer
    pub fn burst_read(&mut self, cs_pin: u8, data: &mut [u8]) -> Result<(), SpiError> {
        let transfer = SpiBurstTransfer {
            tx_buffer: None,
            rx_buffer: Some(data),
            cs_pin,
        };
        self.burst_transfer(transfer)
    }
    
    /// Full-duplex burst transfer
    pub fn burst_exchange(&mut self, cs_pin: u8, tx_data: &[u8], 
                          rx_data: &mut [u8]) -> Result<(), SpiError> {
        if tx_data.len() != rx_data.len() {
            return Err(SpiError::LengthMismatch);
        }
        
        let transfer = SpiBurstTransfer {
            tx_buffer: Some(tx_data),
            rx_buffer: Some(rx_data),
            cs_pin,
        };
        self.burst_transfer(transfer)
    }
    
    // Hardware access methods
    fn is_tx_empty(&self) -> bool {
        unsafe {
            let sr = (self.base_addr + 0x08) as *const u32;
            (read_volatile(sr) & (1 << 1)) != 0
        }
    }
    
    fn is_rx_ready(&self) -> bool {
        unsafe {
            let sr = (self.base_addr + 0x08) as *const u32;
            (read_volatile(sr) & (1 << 0)) != 0
        }
    }
    
    fn is_busy(&self) -> bool {
        unsafe {
            let sr = (self.base_addr + 0x08) as *const u32;
            (read_volatile(sr) & (1 << 7)) != 0
        }
    }
    
    fn write_data(&self, byte: u8) {
        unsafe {
            let dr = (self.base_addr + 0x0C) as *mut u32;
            write_volatile(dr, byte as u32);
        }
    }
    
    fn read_data(&self) -> u8 {
        unsafe {
            let dr = (self.base_addr + 0x0C) as *const u32;
            read_volatile(dr) as u8
        }
    }
    
    fn set_cs(&self, pin: u8, high: bool) {
        // Platform-specific GPIO control
    }
}

/// SPI error types
#[derive(Debug)]
pub enum SpiError {
    InvalidParameter,
    LengthMismatch,
    TransferError,
}

// Example: SPI Flash driver using burst mode
pub struct SpiFlash {
    spi: SpiBurst,
    cs_pin: u8,
}

impl SpiFlash {
    const CMD_READ: u8 = 0x03;
    const CMD_WRITE: u8 = 0x02;
    const CMD_WRITE_ENABLE: u8 = 0x06;
    const PAGE_SIZE: usize = 256;
    
    pub fn new(spi: SpiBurst, cs_pin: u8) -> Self {
        Self { spi, cs_pin }
    }
    
    /// Read data using burst mode
    pub fn read(&mut self, address: u32, buffer: &mut [u8]) -> Result<(), SpiError> {
        // Prepare command with 24-bit address
        let cmd = [
            Self::CMD_READ,
            ((address >> 16) & 0xFF) as u8,
            ((address >> 8) & 0xFF) as u8,
            (address & 0xFF) as u8,
        ];
        
        // Send command (write-only burst)
        self.spi.burst_write(self.cs_pin, &cmd)?;
        
        // Read data (read-only burst)
        self.spi.burst_read(self.cs_pin, buffer)?;
        
        Ok(())
    }
    
    /// Write page using burst mode
    pub fn write_page(&mut self, address: u32, data: &[u8]) -> Result<(), SpiError> {
        if data.len() > Self::PAGE_SIZE {
            return Err(SpiError::InvalidParameter);
        }
        
        // Send write enable command
        self.spi.burst_write(self.cs_pin, &[Self::CMD_WRITE_ENABLE])?;
        
        // Prepare write command with address
        let mut cmd_buf = [0u8; 4 + Self::PAGE_SIZE];
        cmd_buf[0] = Self::CMD_WRITE;
        cmd_buf[1] = ((address >> 16) & 0xFF) as u8;
        cmd_buf[2] = ((address >> 8) & 0xFF) as u8;
        cmd_buf[3] = (address & 0xFF) as u8;
        cmd_buf[4..4 + data.len()].copy_from_slice(data);
        
        // Write in single burst
        self.spi.burst_write(self.cs_pin, &cmd_buf[..4 + data.len()])?;
        
        Ok(())
    }
}

// Zero-copy async burst transfer with DMA
#[cfg(feature = "async")]
pub mod async_burst {
    use core::future::Future;
    use core::task::{Context, Poll};
    
    pub struct DmaBurstFuture {
        complete: bool,
    }
    
    impl Future for DmaBurstFuture {
        type Output = Result<(), super::SpiError>;
        
        fn poll(mut self: core::pin::Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            if self.complete {
                Poll::Ready(Ok(()))
            } else {
                // Register waker for DMA interrupt
                Poll::Pending
            }
        }
    }
    
    impl SpiBurst {
        /// Async DMA burst transfer
        pub async fn burst_transfer_dma(&mut self, 
                                        tx_data: &[u8], 
                                        rx_data: &mut [u8]) -> Result<(), super::SpiError> {
            // Configure DMA
            // Start transfer
            // Await completion
            DmaBurstFuture { complete: false }.await
        }
    }
}
```

## Summary

**Burst mode transfers** are an essential SPI optimization that enables continuous multi-byte data transmission without CS toggling between bytes. This technique provides significant performance improvements—often 2-10x faster than single-byte transfers—by eliminating overhead and maintaining continuous bus activity.

**Key implementation aspects** include maintaining CS assertion throughout the entire transfer, managing transmit and receive buffers efficiently, and optionally leveraging DMA for zero-CPU-overhead transfers. The technique is particularly valuable for memory operations, sensor data acquisition, and display updates where large blocks of data need to be transferred rapidly.

**Best practices** involve batching related operations, using DMA for large transfers (typically >16 bytes), implementing proper error handling, and ensuring buffer alignment for optimal performance. Both the C/C++ and Rust examples demonstrate production-ready implementations with proper abstractions, making burst mode accessible while maintaining safety and efficiency.