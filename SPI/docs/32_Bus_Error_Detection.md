# SPI Bus Error Detection

## Overview

SPI (Serial Peripheral Interface) bus error detection is crucial for building robust embedded systems. Unlike protocols like I2C or CAN that have built-in error detection mechanisms, SPI is a simple synchronous protocol without inherent error checking. This means developers must implement their own error detection strategies to identify hardware faults, disconnected devices, signal integrity issues, and communication failures.

## Common SPI Error Scenarios

### 1. **Hardware Faults**
- Broken or corroded PCB traces
- Damaged IC pins
- Power supply issues affecting signal levels
- ESD (Electrostatic Discharge) damage

### 2. **Disconnected Devices**
- Loose connectors
- Cable disconnections
- Device not powered
- Tri-state outputs (MISO floating)

### 3. **Signal Integrity Issues**
- Clock speed too high for cable length
- Impedance mismatch
- Crosstalk between signals
- Insufficient ground planes
- EMI (Electromagnetic Interference)

### 4. **Configuration Errors**
- Clock polarity/phase mismatch (CPOL/CPHA)
- Incorrect bit order (MSB/LSB first)
- Wrong chip select timing
- SPI mode mismatch between master and slave

## Error Detection Techniques

### 1. **CRC (Cyclic Redundancy Check)**
Add a CRC byte to each transaction to verify data integrity.

### 2. **Known Pattern Testing**
Send known test patterns and verify responses.

### 3. **Timeout Detection**
Monitor transaction completion times.

### 4. **MISO Line Monitoring**
Check for stuck-high, stuck-low, or floating conditions.

### 5. **Readback Verification**
Write to registers and read back to verify.

### 6. **Parity Checking**
Simple odd/even parity for basic error detection.

## C/C++ Implementation Examples

### Basic SPI Error Detection Framework

```c
// spi_error_detection.h
#ifndef SPI_ERROR_DETECTION_H
#define SPI_ERROR_DETECTION_H

#include <stdint.h>
#include <stdbool.h>

// Error codes
typedef enum {
    SPI_OK = 0,
    SPI_TIMEOUT,
    SPI_CRC_ERROR,
    SPI_DEVICE_NOT_RESPONDING,
    SPI_MISO_STUCK_HIGH,
    SPI_MISO_STUCK_LOW,
    SPI_MISO_FLOATING,
    SPI_HARDWARE_FAULT,
    SPI_CONFIG_ERROR
} spi_error_t;

// SPI transaction structure
typedef struct {
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    size_t length;
    uint32_t timeout_ms;
    bool use_crc;
} spi_transaction_t;

// Function prototypes
spi_error_t spi_transfer_with_error_check(spi_transaction_t *transaction);
uint8_t calculate_crc8(uint8_t *data, size_t length);
spi_error_t check_miso_line_health(void);
spi_error_t verify_device_presence(void);
bool is_timeout_exceeded(uint32_t start_time, uint32_t timeout_ms);

#endif // SPI_ERROR_DETECTION_H
```

```c
// spi_error_detection.c
#include "spi_error_detection.h"
#include <string.h>

// CRC-8 polynomial: x^8 + x^2 + x + 1 (0x07)
uint8_t calculate_crc8(uint8_t *data, size_t length) {
    uint8_t crc = 0x00;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Check MISO line for stuck or floating conditions
spi_error_t check_miso_line_health(void) {
    uint8_t test_pattern[] = {0x00, 0xFF, 0xAA, 0x55};
    uint8_t received[4];
    
    // Send test pattern and receive
    for (int i = 0; i < 4; i++) {
        received[i] = spi_transfer_byte(test_pattern[i]);
    }
    
    // Check for stuck-high (all 0xFF)
    bool all_high = true;
    for (int i = 0; i < 4; i++) {
        if (received[i] != 0xFF) {
            all_high = false;
            break;
        }
    }
    if (all_high) return SPI_MISO_STUCK_HIGH;
    
    // Check for stuck-low (all 0x00)
    bool all_low = true;
    for (int i = 0; i < 4; i++) {
        if (received[i] != 0x00) {
            all_low = false;
            break;
        }
    }
    if (all_low) return SPI_MISO_STUCK_LOW;
    
    return SPI_OK;
}

// Verify device presence by reading device ID
spi_error_t verify_device_presence(void) {
    const uint8_t READ_ID_CMD = 0x9F;
    const uint8_t EXPECTED_ID[] = {0xEF, 0x40, 0x16}; // Example device ID
    uint8_t tx_buf[4] = {READ_ID_CMD, 0x00, 0x00, 0x00};
    uint8_t rx_buf[4] = {0};
    
    // Chip select low
    spi_cs_low();
    
    // Transfer command
    for (int i = 0; i < 4; i++) {
        rx_buf[i] = spi_transfer_byte(tx_buf[i]);
    }
    
    // Chip select high
    spi_cs_high();
    
    // Check if received ID matches expected
    if (memcmp(&rx_buf[1], EXPECTED_ID, 3) == 0) {
        return SPI_OK;
    }
    
    return SPI_DEVICE_NOT_RESPONDING;
}

// Complete SPI transfer with error checking
spi_error_t spi_transfer_with_error_check(spi_transaction_t *transaction) {
    if (!transaction || !transaction->tx_buffer || !transaction->rx_buffer) {
        return SPI_CONFIG_ERROR;
    }
    
    uint32_t start_time = get_system_tick();
    
    // Check MISO line health before transfer
    spi_error_t error = check_miso_line_health();
    if (error != SPI_OK) {
        return error;
    }
    
    // Chip select low
    spi_cs_low();
    
    // Perform transfer
    for (size_t i = 0; i < transaction->length; i++) {
        // Check for timeout
        if (is_timeout_exceeded(start_time, transaction->timeout_ms)) {
            spi_cs_high();
            return SPI_TIMEOUT;
        }
        
        transaction->rx_buffer[i] = spi_transfer_byte(transaction->tx_buffer[i]);
    }
    
    // Handle CRC if enabled
    if (transaction->use_crc && transaction->length > 1) {
        uint8_t calculated_crc = calculate_crc8(transaction->rx_buffer, 
                                                transaction->length - 1);
        uint8_t received_crc = transaction->rx_buffer[transaction->length - 1];
        
        if (calculated_crc != received_crc) {
            spi_cs_high();
            return SPI_CRC_ERROR;
        }
    }
    
    // Chip select high
    spi_cs_high();
    
    return SPI_OK;
}

// Timeout checking helper
bool is_timeout_exceeded(uint32_t start_time, uint32_t timeout_ms) {
    uint32_t elapsed = get_system_tick() - start_time;
    return (elapsed > timeout_ms);
}
```

### Advanced Error Detection with Retry Logic

```cpp
// spi_robust_transfer.cpp
#include <cstdint>
#include <chrono>
#include <thread>

class SPIErrorDetector {
private:
    static constexpr uint8_t MAX_RETRIES = 3;
    static constexpr uint32_t RETRY_DELAY_MS = 10;
    
    struct ErrorStatistics {
        uint32_t crc_errors;
        uint32_t timeouts;
        uint32_t device_not_responding;
        uint32_t total_transfers;
        uint32_t successful_transfers;
    };
    
    ErrorStatistics stats;
    
public:
    SPIErrorDetector() : stats{0} {}
    
    // Transfer with automatic retry on failure
    spi_error_t transfer_with_retry(uint8_t *tx_data, uint8_t *rx_data, 
                                   size_t length, bool use_crc = true) {
        spi_error_t result;
        uint8_t attempts = 0;
        
        stats.total_transfers++;
        
        while (attempts < MAX_RETRIES) {
            spi_transaction_t transaction = {
                .tx_buffer = tx_data,
                .rx_buffer = rx_data,
                .length = length,
                .timeout_ms = 1000,
                .use_crc = use_crc
            };
            
            result = spi_transfer_with_error_check(&transaction);
            
            if (result == SPI_OK) {
                stats.successful_transfers++;
                return SPI_OK;
            }
            
            // Log specific error
            switch(result) {
                case SPI_CRC_ERROR:
                    stats.crc_errors++;
                    break;
                case SPI_TIMEOUT:
                    stats.timeouts++;
                    break;
                case SPI_DEVICE_NOT_RESPONDING:
                    stats.device_not_responding++;
                    break;
                default:
                    break;
            }
            
            attempts++;
            
            // Wait before retry
            if (attempts < MAX_RETRIES) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(RETRY_DELAY_MS)
                );
            }
        }
        
        return result;
    }
    
    // Get error statistics
    const ErrorStatistics& get_statistics() const {
        return stats;
    }
    
    // Calculate success rate
    double get_success_rate() const {
        if (stats.total_transfers == 0) return 0.0;
        return (double)stats.successful_transfers / stats.total_transfers * 100.0;
    }
    
    // Reset statistics
    void reset_statistics() {
        stats = {0};
    }
};
```

## Rust Implementation Examples

### Basic SPI Error Detection

```rust
// spi_error_detection.rs
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiError {
    Ok,
    Timeout,
    CrcError,
    DeviceNotResponding,
    MisoStuckHigh,
    MisoStuckLow,
    MisoFloating,
    HardwareFault,
    ConfigError,
}

pub struct SpiTransaction<'a> {
    pub tx_buffer: &'a [u8],
    pub rx_buffer: &'a mut [u8],
    pub timeout: Duration,
    pub use_crc: bool,
}

// CRC-8 calculation
pub fn calculate_crc8(data: &[u8]) -> u8 {
    const POLYNOMIAL: u8 = 0x07;
    let mut crc: u8 = 0x00;
    
    for byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

// Check MISO line health
pub fn check_miso_line_health(spi_transfer: impl Fn(u8) -> u8) -> SpiError {
    let test_pattern: [u8; 4] = [0x00, 0xFF, 0xAA, 0x55];
    let mut received: [u8; 4] = [0; 4];
    
    // Send test pattern
    for (i, &byte) in test_pattern.iter().enumerate() {
        received[i] = spi_transfer(byte);
    }
    
    // Check for stuck-high
    if received.iter().all(|&b| b == 0xFF) {
        return SpiError::MisoStuckHigh;
    }
    
    // Check for stuck-low
    if received.iter().all(|&b| b == 0x00) {
        return SpiError::MisoStuckLow;
    }
    
    SpiError::Ok
}

// Verify device presence
pub fn verify_device_presence(
    spi_transfer_multi: impl Fn(&[u8], &mut [u8])
) -> SpiError {
    const READ_ID_CMD: u8 = 0x9F;
    const EXPECTED_ID: [u8; 3] = [0xEF, 0x40, 0x16];
    
    let tx_buf: [u8; 4] = [READ_ID_CMD, 0x00, 0x00, 0x00];
    let mut rx_buf: [u8; 4] = [0; 4];
    
    spi_transfer_multi(&tx_buf, &mut rx_buf);
    
    if &rx_buf[1..4] == &EXPECTED_ID {
        SpiError::Ok
    } else {
        SpiError::DeviceNotResponding
    }
}

// Complete SPI transfer with error checking
pub fn spi_transfer_with_error_check<F>(
    transaction: &mut SpiTransaction,
    mut spi_transfer_byte: F,
) -> SpiError 
where
    F: FnMut(u8) -> u8,
{
    if transaction.tx_buffer.len() != transaction.rx_buffer.len() {
        return SpiError::ConfigError;
    }
    
    let start_time = Instant::now();
    
    // Perform transfer
    for (i, &tx_byte) in transaction.tx_buffer.iter().enumerate() {
        // Check timeout
        if start_time.elapsed() > transaction.timeout {
            return SpiError::Timeout;
        }
        
        transaction.rx_buffer[i] = spi_transfer_byte(tx_byte);
    }
    
    // Verify CRC if enabled
    if transaction.use_crc && transaction.rx_buffer.len() > 1 {
        let data_len = transaction.rx_buffer.len() - 1;
        let calculated_crc = calculate_crc8(&transaction.rx_buffer[..data_len]);
        let received_crc = transaction.rx_buffer[data_len];
        
        if calculated_crc != received_crc {
            return SpiError::CrcError;
        }
    }
    
    SpiError::Ok
}
```

### Advanced Rust Implementation with Retry Logic

```rust
// spi_robust_transfer.rs
use std::time::Duration;
use std::thread;

#[derive(Debug, Default, Clone)]
pub struct ErrorStatistics {
    pub crc_errors: u32,
    pub timeouts: u32,
    pub device_not_responding: u32,
    pub total_transfers: u32,
    pub successful_transfers: u32,
}

impl ErrorStatistics {
    pub fn success_rate(&self) -> f64 {
        if self.total_transfers == 0 {
            return 0.0;
        }
        (self.successful_transfers as f64 / self.total_transfers as f64) * 100.0
    }
}

pub struct SpiErrorDetector {
    max_retries: u8,
    retry_delay: Duration,
    stats: ErrorStatistics,
}

impl SpiErrorDetector {
    pub fn new(max_retries: u8, retry_delay_ms: u64) -> Self {
        Self {
            max_retries,
            retry_delay: Duration::from_millis(retry_delay_ms),
            stats: ErrorStatistics::default(),
        }
    }
    
    pub fn transfer_with_retry<F>(
        &mut self,
        tx_data: &[u8],
        rx_data: &mut [u8],
        use_crc: bool,
        mut spi_transfer: F,
    ) -> SpiError 
    where
        F: FnMut(u8) -> u8,
    {
        self.stats.total_transfers += 1;
        
        for attempt in 0..self.max_retries {
            let mut transaction = SpiTransaction {
                tx_buffer: tx_data,
                rx_buffer,
                timeout: Duration::from_secs(1),
                use_crc,
            };
            
            let result = spi_transfer_with_error_check(
                &mut transaction,
                &mut spi_transfer
            );
            
            if result == SpiError::Ok {
                self.stats.successful_transfers += 1;
                return SpiError::Ok;
            }
            
            // Update statistics
            match result {
                SpiError::CrcError => self.stats.crc_errors += 1,
                SpiError::Timeout => self.stats.timeouts += 1,
                SpiError::DeviceNotResponding => {
                    self.stats.device_not_responding += 1
                }
                _ => {}
            }
            
            // Wait before retry (except on last attempt)
            if attempt < self.max_retries - 1 {
                thread::sleep(self.retry_delay);
            }
        }
        
        SpiError::HardwareFault
    }
    
    pub fn get_statistics(&self) -> &ErrorStatistics {
        &self.stats
    }
    
    pub fn reset_statistics(&mut self) {
        self.stats = ErrorStatistics::default();
    }
}

// Builder pattern for easy configuration
pub struct SpiErrorDetectorBuilder {
    max_retries: u8,
    retry_delay_ms: u64,
}

impl SpiErrorDetectorBuilder {
    pub fn new() -> Self {
        Self {
            max_retries: 3,
            retry_delay_ms: 10,
        }
    }
    
    pub fn max_retries(mut self, retries: u8) -> Self {
        self.max_retries = retries;
        self
    }
    
    pub fn retry_delay_ms(mut self, delay: u64) -> Self {
        self.retry_delay_ms = delay;
        self
    }
    
    pub fn build(self) -> SpiErrorDetector {
        SpiErrorDetector::new(self.max_retries, self.retry_delay_ms)
    }
}
```

### Practical Usage Example in Rust

```rust
// main.rs - Example usage
mod spi_error_detection;
mod spi_robust_transfer;

use spi_robust_transfer::{SpiErrorDetector, SpiErrorDetectorBuilder};
use spi_error_detection::SpiError;

fn main() {
    // Create error detector with custom configuration
    let mut detector = SpiErrorDetectorBuilder::new()
        .max_retries(5)
        .retry_delay_ms(20)
        .build();
    
    // Simulate SPI transfer function
    let spi_transfer = |byte: u8| -> u8 {
        // Hardware-specific implementation
        byte // Echo for demonstration
    };
    
    // Example data transfer
    let tx_data = [0x03, 0x00, 0x00, 0x00]; // Read command
    let mut rx_data = [0u8; 4];
    
    match detector.transfer_with_retry(&tx_data, &mut rx_data, true, spi_transfer) {
        SpiError::Ok => {
            println!("Transfer successful!");
            println!("Received: {:02X?}", rx_data);
        }
        error => {
            eprintln!("Transfer failed: {:?}", error);
        }
    }
    
    // Print statistics
    let stats = detector.get_statistics();
    println!("\nError Statistics:");
    println!("Total transfers: {}", stats.total_transfers);
    println!("Successful: {}", stats.successful_transfers);
    println!("CRC errors: {}", stats.crc_errors);
    println!("Timeouts: {}", stats.timeouts);
    println!("Success rate: {:.2}%", stats.success_rate());
}
```

## Summary

SPI bus error detection is essential for robust embedded systems since SPI lacks built-in error checking. Key strategies include:

**Detection Methods:**
- CRC/checksum validation for data integrity
- MISO line monitoring for stuck or floating conditions
- Device presence verification through ID reads
- Timeout monitoring for unresponsive devices
- Known pattern testing for signal integrity

**Implementation Best Practices:**
- Implement automatic retry logic with exponential backoff
- Track error statistics to identify recurring issues
- Use appropriate timeout values based on expected response times
- Combine multiple detection techniques for comprehensive coverage
- Log errors for debugging and system health monitoring

**Common Pitfalls to Avoid:**
- Ignoring MISO line state during initialization
- Setting timeout values too short or too long
- Not validating configuration parameters before transfers
- Failing to handle partial transfer failures
- Overlooking signal integrity issues at higher speeds

Both C/C++ and Rust implementations demonstrate professional error handling with retry mechanisms, statistics tracking, and comprehensive error reporting. The Rust version additionally benefits from memory safety guarantees and modern language features like the builder pattern for configuration.