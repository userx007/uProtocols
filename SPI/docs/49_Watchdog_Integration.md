# SPI Watchdog Integration

## Overview

Watchdog Integration in SPI systems involves carefully managing SPI communication timing to ensure operations complete within watchdog timer windows. This is critical in embedded systems where watchdog timers reset the system if not serviced regularly, preventing system hangs during SPI transactions.

## Core Concepts

### Why Watchdog Integration Matters for SPI

1. **Long Transactions**: SPI transfers, especially with large data buffers or slow clock speeds, can take significant time
2. **Blocking Operations**: Traditional SPI implementations often block CPU execution during transfers
3. **System Reliability**: Watchdog timers are essential safety mechanisms that must be serviced regularly
4. **Interrupt Conflicts**: SPI interrupts and watchdog servicing must coexist without conflicts

### Key Strategies

- **Transaction Segmentation**: Breaking large transfers into smaller chunks
- **Asynchronous Operations**: Using DMA or interrupt-driven transfers
- **Timing Budgeting**: Calculating maximum SPI transaction times
- **Strategic Watchdog Kicks**: Servicing watchdog during safe points in SPI operations
- **Timeout Mechanisms**: Implementing SPI operation timeouts to prevent infinite waits

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// Watchdog and SPI register definitions (platform-specific)
#define WDT_FEED_REG    (*(volatile uint32_t*)0x40000008)
#define WDT_TIMEOUT_MS  1000
#define SPI_DATA_REG    (*(volatile uint32_t*)0x40010000)
#define SPI_STATUS_REG  (*(volatile uint32_t*)0x40010004)
#define SPI_BUSY        (1 << 0)

// Configuration
#define SPI_MAX_CHUNK_SIZE      128     // Bytes per chunk
#define SPI_BYTE_TIME_US        10      // Time per byte at current SPI speed
#define WATCHDOG_MARGIN_MS      100     // Safety margin before watchdog timeout

// Calculate maximum safe chunk size based on watchdog timing
static uint32_t calculate_safe_chunk_size(void) {
    uint32_t available_time_ms = WDT_TIMEOUT_MS - WATCHDOG_MARGIN_MS;
    uint32_t available_time_us = available_time_ms * 1000;
    uint32_t max_bytes = available_time_us / SPI_BYTE_TIME_US;
    
    return (max_bytes < SPI_MAX_CHUNK_SIZE) ? max_bytes : SPI_MAX_CHUNK_SIZE;
}

// Feed the watchdog timer
static inline void watchdog_feed(void) {
    WDT_FEED_REG = 0xAA;  // Magic sequence (platform-specific)
    WDT_FEED_REG = 0x55;
}

// Blocking SPI transfer with watchdog consideration
bool spi_transfer_with_watchdog(const uint8_t* tx_data, uint8_t* rx_data, 
                                uint32_t length) {
    uint32_t chunk_size = calculate_safe_chunk_size();
    uint32_t offset = 0;
    
    // Feed watchdog before starting
    watchdog_feed();
    
    while (offset < length) {
        uint32_t bytes_to_transfer = 
            ((length - offset) < chunk_size) ? (length - offset) : chunk_size;
        
        // Transfer chunk
        for (uint32_t i = 0; i < bytes_to_transfer; i++) {
            // Write data
            SPI_DATA_REG = tx_data ? tx_data[offset + i] : 0xFF;
            
            // Wait for completion (with timeout)
            uint32_t timeout = 1000;
            while ((SPI_STATUS_REG & SPI_BUSY) && timeout--) {
                // Simple delay
                for (volatile int d = 0; d < 100; d++);
            }
            
            if (timeout == 0) {
                return false;  // Timeout error
            }
            
            // Read received data
            if (rx_data) {
                rx_data[offset + i] = (uint8_t)SPI_DATA_REG;
            }
        }
        
        offset += bytes_to_transfer;
        
        // Feed watchdog after each chunk
        if (offset < length) {
            watchdog_feed();
        }
    }
    
    return true;
}

// DMA-based transfer with watchdog integration
typedef struct {
    volatile bool transfer_complete;
    volatile bool transfer_error;
    uint32_t bytes_transferred;
} spi_dma_state_t;

static spi_dma_state_t dma_state;

// DMA interrupt handler
void SPI_DMA_IRQHandler(void) {
    // Check and clear DMA completion flags
    if (/* DMA transfer complete flag */) {
        dma_state.transfer_complete = true;
    }
    if (/* DMA error flag */) {
        dma_state.transfer_error = true;
    }
    
    // Feed watchdog in ISR (careful - keep ISR short)
    watchdog_feed();
}

bool spi_dma_transfer_with_watchdog(const uint8_t* tx_data, uint8_t* rx_data,
                                    uint32_t length, uint32_t timeout_ms) {
    // Initialize state
    dma_state.transfer_complete = false;
    dma_state.transfer_error = false;
    dma_state.bytes_transferred = 0;
    
    // Configure and start DMA transfer
    // ... (platform-specific DMA setup)
    
    // Feed watchdog before waiting
    watchdog_feed();
    
    uint32_t start_time = get_system_time_ms();  // Platform-specific
    
    // Wait for completion with periodic watchdog feeding
    while (!dma_state.transfer_complete && !dma_state.transfer_error) {
        uint32_t elapsed = get_system_time_ms() - start_time;
        
        // Check timeout
        if (elapsed >= timeout_ms) {
            // Abort DMA transfer
            return false;
        }
        
        // Feed watchdog periodically during wait
        if (elapsed % (WDT_TIMEOUT_MS / 2) == 0) {
            watchdog_feed();
        }
        
        // Small delay to prevent busy-waiting
        delay_ms(1);
    }
    
    return !dma_state.transfer_error;
}

// Watchdog-aware SPI transaction wrapper
typedef struct {
    void (*pre_transaction_callback)(void);
    void (*post_transaction_callback)(void);
    uint32_t max_transaction_time_ms;
} spi_watchdog_config_t;

static spi_watchdog_config_t wdt_config = {
    .pre_transaction_callback = NULL,
    .post_transaction_callback = NULL,
    .max_transaction_time_ms = 500
};

bool spi_transaction_safe(uint8_t cs_pin, const uint8_t* tx_data,
                          uint8_t* rx_data, uint32_t length) {
    bool result;
    
    // Pre-transaction callback (e.g., disable non-critical interrupts)
    if (wdt_config.pre_transaction_callback) {
        wdt_config.pre_transaction_callback();
    }
    
    // Feed watchdog
    watchdog_feed();
    
    // Assert chip select
    gpio_write(cs_pin, 0);
    
    // Perform transfer
    result = spi_transfer_with_watchdog(tx_data, rx_data, length);
    
    // Deassert chip select
    gpio_write(cs_pin, 1);
    
    // Post-transaction callback (e.g., re-enable interrupts)
    if (wdt_config.post_transaction_callback) {
        wdt_config.post_transaction_callback();
    }
    
    // Final watchdog feed
    watchdog_feed();
    
    return result;
}

// Example: Reading large flash memory with watchdog safety
bool read_flash_with_watchdog(uint32_t address, uint8_t* buffer, 
                              uint32_t length) {
    const uint32_t FLASH_PAGE_SIZE = 256;
    uint8_t cmd[4];
    
    for (uint32_t offset = 0; offset < length; offset += FLASH_PAGE_SIZE) {
        uint32_t bytes_to_read = 
            ((length - offset) < FLASH_PAGE_SIZE) ? 
            (length - offset) : FLASH_PAGE_SIZE;
        
        // Prepare read command
        cmd[0] = 0x03;  // READ command
        cmd[1] = (address + offset) >> 16;
        cmd[2] = (address + offset) >> 8;
        cmd[3] = (address + offset) & 0xFF;
        
        // Execute transaction with watchdog safety
        if (!spi_transaction_safe(FLASH_CS_PIN, cmd, NULL, 4)) {
            return false;
        }
        
        if (!spi_transaction_safe(FLASH_CS_PIN, NULL, 
                                  &buffer[offset], bytes_to_read)) {
            return false;
        }
        
        // Watchdog is fed inside spi_transaction_safe
    }
    
    return true;
}
```

### Rust Implementation

```rust
use core::time::Duration;
use embedded_hal::spi::SpiBus;
use embedded_hal::digital::OutputPin;

/// Watchdog timer trait
pub trait Watchdog {
    fn feed(&mut self);
    fn timeout(&self) -> Duration;
}

/// SPI transfer statistics
#[derive(Debug, Clone, Copy)]
pub struct TransferStats {
    pub bytes_transferred: usize,
    pub chunks_processed: usize,
    pub watchdog_feeds: usize,
}

/// Configuration for watchdog-aware SPI operations
pub struct SpiWatchdogConfig {
    pub max_chunk_size: usize,
    pub byte_time_us: u32,
    pub safety_margin_ms: u32,
}

impl Default for SpiWatchdogConfig {
    fn default() -> Self {
        Self {
            max_chunk_size: 128,
            byte_time_us: 10,
            safety_margin_ms: 100,
        }
    }
}

/// Watchdog-aware SPI manager
pub struct WatchdogSpi<SPI, WDT, CS> {
    spi: SPI,
    watchdog: WDT,
    cs: CS,
    config: SpiWatchdogConfig,
}

impl<SPI, WDT, CS> WatchdogSpi<SPI, WDT, CS>
where
    SPI: SpiBus,
    WDT: Watchdog,
    CS: OutputPin,
{
    pub fn new(spi: SPI, watchdog: WDT, cs: CS, config: SpiWatchdogConfig) -> Self {
        Self {
            spi,
            watchdog,
            cs,
            config,
        }
    }

    /// Calculate safe chunk size based on watchdog timing
    fn calculate_safe_chunk_size(&self) -> usize {
        let wdt_timeout_ms = self.watchdog.timeout().as_millis() as u32;
        let available_time_ms = wdt_timeout_ms.saturating_sub(self.config.safety_margin_ms);
        let available_time_us = available_time_ms * 1000;
        let max_bytes = (available_time_us / self.config.byte_time_us) as usize;

        max_bytes.min(self.config.max_chunk_size)
    }

    /// Perform SPI transfer with watchdog feeding
    pub fn transfer_with_watchdog(
        &mut self,
        tx_data: &[u8],
        rx_data: &mut [u8],
    ) -> Result<TransferStats, SPI::Error> {
        let chunk_size = self.calculate_safe_chunk_size();
        let length = tx_data.len().min(rx_data.len());
        
        let mut stats = TransferStats {
            bytes_transferred: 0,
            chunks_processed: 0,
            watchdog_feeds: 0,
        };

        // Feed watchdog before starting
        self.watchdog.feed();
        stats.watchdog_feeds += 1;

        let mut offset = 0;
        while offset < length {
            let bytes_to_transfer = (length - offset).min(chunk_size);
            
            // Transfer chunk
            self.spi.transfer(
                &mut rx_data[offset..offset + bytes_to_transfer],
                &tx_data[offset..offset + bytes_to_transfer],
            )?;

            offset += bytes_to_transfer;
            stats.bytes_transferred += bytes_to_transfer;
            stats.chunks_processed += 1;

            // Feed watchdog after each chunk (except last)
            if offset < length {
                self.watchdog.feed();
                stats.watchdog_feeds += 1;
            }
        }

        Ok(stats)
    }

    /// Perform a complete transaction with chip select
    pub fn transaction<F, R>(
        &mut self,
        operation: F,
    ) -> Result<R, TransactionError<SPI::Error, CS::Error>>
    where
        F: FnOnce(&mut Self) -> Result<R, SPI::Error>,
    {
        // Feed watchdog before transaction
        self.watchdog.feed();

        // Assert CS
        self.cs.set_low().map_err(TransactionError::CsError)?;

        // Perform operation
        let result = operation(self).map_err(TransactionError::SpiError);

        // Deassert CS
        self.cs.set_high().map_err(TransactionError::CsError)?;

        // Feed watchdog after transaction
        self.watchdog.feed();

        result
    }

    /// Write data to SPI device with watchdog safety
    pub fn write_with_watchdog(&mut self, data: &[u8]) -> Result<TransferStats, SPI::Error> {
        let chunk_size = self.calculate_safe_chunk_size();
        let mut stats = TransferStats {
            bytes_transferred: 0,
            chunks_processed: 0,
            watchdog_feeds: 0,
        };

        self.watchdog.feed();
        stats.watchdog_feeds += 1;

        let mut offset = 0;
        while offset < data.len() {
            let bytes_to_write = (data.len() - offset).min(chunk_size);
            
            self.spi.write(&data[offset..offset + bytes_to_write])?;

            offset += bytes_to_write;
            stats.bytes_transferred += bytes_to_write;
            stats.chunks_processed += 1;

            if offset < data.len() {
                self.watchdog.feed();
                stats.watchdog_feeds += 1;
            }
        }

        Ok(stats)
    }

    /// Read data from SPI device with watchdog safety
    pub fn read_with_watchdog(&mut self, buffer: &mut [u8]) -> Result<TransferStats, SPI::Error> {
        let chunk_size = self.calculate_safe_chunk_size();
        let mut stats = TransferStats {
            bytes_transferred: 0,
            chunks_processed: 0,
            watchdog_feeds: 0,
        };

        self.watchdog.feed();
        stats.watchdog_feeds += 1;

        let mut offset = 0;
        while offset < buffer.len() {
            let bytes_to_read = (buffer.len() - offset).min(chunk_size);
            
            self.spi.read(&mut buffer[offset..offset + bytes_to_read])?;

            offset += bytes_to_read;
            stats.bytes_transferred += bytes_to_read;
            stats.chunks_processed += 1;

            if offset < buffer.len() {
                self.watchdog.feed();
                stats.watchdog_feeds += 1;
            }
        }

        Ok(stats)
    }
}

/// Transaction error types
#[derive(Debug)]
pub enum TransactionError<SpiError, CsError> {
    SpiError(SpiError),
    CsError(CsError),
}

/// Example: Flash memory reader with watchdog integration
pub struct FlashReader<SPI, WDT, CS> {
    spi_wdt: WatchdogSpi<SPI, WDT, CS>,
}

impl<SPI, WDT, CS> FlashReader<SPI, WDT, CS>
where
    SPI: SpiBus,
    WDT: Watchdog,
    CS: OutputPin,
{
    pub fn new(spi_wdt: WatchdogSpi<SPI, WDT, CS>) -> Self {
        Self { spi_wdt }
    }

    /// Read data from flash with automatic watchdog management
    pub fn read_flash(
        &mut self,
        address: u32,
        buffer: &mut [u8],
    ) -> Result<TransferStats, TransactionError<SPI::Error, CS::Error>> {
        const PAGE_SIZE: usize = 256;
        let mut total_stats = TransferStats {
            bytes_transferred: 0,
            chunks_processed: 0,
            watchdog_feeds: 0,
        };

        let mut offset = 0;
        while offset < buffer.len() {
            let bytes_to_read = (buffer.len() - offset).min(PAGE_SIZE);
            let current_address = address + offset as u32;

            self.spi_wdt.transaction(|spi_wdt| {
                // Send read command
                let cmd = [
                    0x03, // READ command
                    (current_address >> 16) as u8,
                    (current_address >> 8) as u8,
                    current_address as u8,
                ];
                
                spi_wdt.spi.write(&cmd)?;
                
                // Read data
                let stats = spi_wdt.read_with_watchdog(
                    &mut buffer[offset..offset + bytes_to_read]
                )?;
                
                total_stats.bytes_transferred += stats.bytes_transferred;
                total_stats.chunks_processed += stats.chunks_processed;
                total_stats.watchdog_feeds += stats.watchdog_feeds;

                Ok(())
            })?;

            offset += bytes_to_read;
        }

        Ok(total_stats)
    }
}

// Example usage
#[cfg(feature = "example")]
mod example {
    use super::*;

    pub fn example_usage<SPI, WDT, CS>(spi: SPI, watchdog: WDT, cs: CS)
    where
        SPI: SpiBus,
        WDT: Watchdog,
        CS: OutputPin,
    {
        let config = SpiWatchdogConfig {
            max_chunk_size: 128,
            byte_time_us: 8,
            safety_margin_ms: 150,
        };

        let mut spi_wdt = WatchdogSpi::new(spi, watchdog, cs, config);
        let mut flash = FlashReader::new(spi_wdt);

        let mut buffer = [0u8; 1024];
        match flash.read_flash(0x0000, &mut buffer) {
            Ok(stats) => {
                // Successfully read with watchdog protection
                println!("Read {} bytes in {} chunks with {} watchdog feeds",
                    stats.bytes_transferred,
                    stats.chunks_processed,
                    stats.watchdog_feeds);
            }
            Err(_e) => {
                // Handle error
            }
        }
    }
}
```

## Summary

**SPI Watchdog Integration** is essential for building reliable embedded systems that use SPI communication alongside watchdog timers. The key principles include:

1. **Chunked Transfers**: Breaking large SPI operations into smaller segments that fit within watchdog timeout windows with safety margins
2. **Strategic Feeding**: Servicing the watchdog at safe points between chunks, not during critical atomic operations
3. **Timing Calculation**: Computing maximum safe chunk sizes based on SPI clock speed, watchdog timeout, and safety margins
4. **Asynchronous Operations**: Using DMA or interrupt-driven transfers to free the CPU for watchdog servicing
5. **Error Handling**: Implementing timeouts and error recovery to prevent infinite waits that would trigger watchdog resets

The C/C++ examples demonstrate practical implementations with blocking transfers, DMA integration, and transaction wrappers. The Rust implementation leverages type safety and trait abstractions to create a reusable, zero-cost abstraction for watchdog-aware SPI operations with comprehensive error handling and transfer statistics tracking. Both approaches ensure system stability by preventing watchdog-induced resets during legitimate SPI operations while maintaining the watchdog's protective function against true system hangs.