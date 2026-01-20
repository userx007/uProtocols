# SPI Fault Injection: Testing System Robustness

## Overview

Fault injection in SPI (Serial Peripheral Interface) communication is a critical testing methodology used to validate how embedded systems handle communication failures, hardware glitches, and error conditions. By deliberately introducing faults into SPI transactions, developers can ensure their systems fail gracefully, implement proper error recovery, and maintain reliability in production environments.

## Why Fault Injection Matters

In real-world deployments, SPI communication can fail due to:
- **Electrical noise** causing bit flips
- **Loose connections** leading to intermittent signals
- **Clock synchronization issues**
- **Device malfunction** or unexpected responses
- **EMI (Electromagnetic Interference)**
- **Power supply fluctuations**

Systems that don't handle these failures properly may crash, corrupt data, or enter undefined states. Fault injection testing helps identify these vulnerabilities before deployment.

## Types of SPI Faults to Inject

1. **Communication Errors**
   - Bit flips in transmitted/received data
   - Corrupted bytes
   - Incorrect checksums

2. **Timing Faults**
   - Clock glitches
   - Delayed responses
   - Timeout conditions

3. **Protocol Violations**
   - Missing acknowledgments
   - Invalid command sequences
   - Unexpected device behavior

4. **Hardware Faults**
   - Simulated disconnections
   - CS (Chip Select) signal failures
   - MISO line stuck high/low

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Fault injection configuration
typedef struct {
    bool enable_bit_flip;
    float bit_flip_probability;  // 0.0 to 1.0
    bool enable_timeout;
    uint32_t timeout_ms;
    bool enable_corruption;
    float corruption_probability;
    bool enable_cs_glitch;
} spi_fault_config_t;

typedef enum {
    SPI_SUCCESS = 0,
    SPI_TIMEOUT,
    SPI_CRC_ERROR,
    SPI_CORRUPTION,
    SPI_HW_FAULT
} spi_status_t;

// Global fault injection configuration
static spi_fault_config_t g_fault_config = {0};

// Initialize fault injection
void spi_fault_init(spi_fault_config_t *config) {
    memcpy(&g_fault_config, config, sizeof(spi_fault_config_t));
    srand(12345); // Deterministic for reproducible tests
}

// Simulate bit flip in data
static void inject_bit_flip(uint8_t *data, size_t len) {
    if (!g_fault_config.enable_bit_flip) return;
    
    for (size_t i = 0; i < len; i++) {
        if ((rand() / (float)RAND_MAX) < g_fault_config.bit_flip_probability) {
            uint8_t bit_pos = rand() % 8;
            data[i] ^= (1 << bit_pos);
            // Log fault for analysis
            printf("Fault injected: Bit flip at byte %zu, bit %u\n", i, bit_pos);
        }
    }
}

// Simulate data corruption
static void inject_corruption(uint8_t *data, size_t len) {
    if (!g_fault_config.enable_corruption) return;
    
    if ((rand() / (float)RAND_MAX) < g_fault_config.corruption_probability) {
        size_t corrupt_pos = rand() % len;
        data[corrupt_pos] = rand() % 256;
        printf("Fault injected: Byte corruption at position %zu\n", corrupt_pos);
    }
}

// SPI transfer with fault injection
spi_status_t spi_transfer_with_faults(uint8_t *tx_data, uint8_t *rx_data, 
                                      size_t len, uint32_t timeout_ms) {
    // Simulate CS glitch
    if (g_fault_config.enable_cs_glitch && 
        (rand() / (float)RAND_MAX) < 0.1) {
        printf("Fault injected: CS glitch\n");
        return SPI_HW_FAULT;
    }
    
    // Simulate timeout
    if (g_fault_config.enable_timeout && 
        (rand() / (float)RAND_MAX) < 0.05) {
        printf("Fault injected: Timeout\n");
        return SPI_TIMEOUT;
    }
    
    // Perform actual SPI transfer (hardware-specific)
    // spi_hw_transfer(tx_data, rx_data, len);
    memcpy(rx_data, tx_data, len); // Simplified for example
    
    // Inject faults into received data
    inject_bit_flip(rx_data, len);
    inject_corruption(rx_data, len);
    
    return SPI_SUCCESS;
}

// CRC-8 calculation for error detection
uint8_t calculate_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Robust SPI transaction with retry logic
spi_status_t spi_robust_transfer(uint8_t *tx_data, uint8_t *rx_data, 
                                  size_t len, uint8_t max_retries) {
    uint8_t expected_crc = calculate_crc8(tx_data, len);
    
    for (uint8_t retry = 0; retry < max_retries; retry++) {
        spi_status_t status = spi_transfer_with_faults(tx_data, rx_data, len, 1000);
        
        if (status == SPI_TIMEOUT || status == SPI_HW_FAULT) {
            printf("Retry %u/%u due to %s\n", retry + 1, max_retries,
                   status == SPI_TIMEOUT ? "timeout" : "hardware fault");
            continue;
        }
        
        uint8_t received_crc = calculate_crc8(rx_data, len);
        if (received_crc != expected_crc) {
            printf("Retry %u/%u due to CRC mismatch (expected: 0x%02X, got: 0x%02X)\n",
                   retry + 1, max_retries, expected_crc, received_crc);
            continue;
        }
        
        return SPI_SUCCESS;
    }
    
    return SPI_CRC_ERROR;
}

// Test scenario
int main(void) {
    spi_fault_config_t config = {
        .enable_bit_flip = true,
        .bit_flip_probability = 0.01,  // 1% chance per byte
        .enable_timeout = true,
        .timeout_ms = 100,
        .enable_corruption = true,
        .corruption_probability = 0.05, // 5% chance per transfer
        .enable_cs_glitch = true
    };
    
    spi_fault_init(&config);
    
    uint8_t tx_buffer[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t rx_buffer[16];
    
    printf("Starting fault injection test...\n");
    spi_status_t result = spi_robust_transfer(tx_buffer, rx_buffer, 16, 5);
    
    if (result == SPI_SUCCESS) {
        printf("Transfer succeeded after fault recovery\n");
    } else {
        printf("Transfer failed after all retries\n");
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use rand::{Rng, SeedableRng};
use rand::rngs::StdRng;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiStatus {
    Success,
    Timeout,
    CrcError,
    Corruption,
    HardwareFault,
}

#[derive(Debug, Clone)]
pub struct FaultConfig {
    pub enable_bit_flip: bool,
    pub bit_flip_probability: f32,
    pub enable_timeout: bool,
    pub timeout_ms: u32,
    pub enable_corruption: bool,
    pub corruption_probability: f32,
    pub enable_cs_glitch: bool,
}

impl Default for FaultConfig {
    fn default() -> Self {
        Self {
            enable_bit_flip: false,
            bit_flip_probability: 0.0,
            enable_timeout: false,
            timeout_ms: 100,
            enable_corruption: false,
            corruption_probability: 0.0,
            enable_cs_glitch: false,
        }
    }
}

pub struct SpiFaultInjector {
    config: FaultConfig,
    rng: StdRng,
}

impl SpiFaultInjector {
    pub fn new(config: FaultConfig) -> Self {
        Self {
            config,
            rng: StdRng::seed_from_u64(12345), // Deterministic for testing
        }
    }

    /// Inject bit flip faults into data
    fn inject_bit_flip(&mut self, data: &mut [u8]) {
        if !self.config.enable_bit_flip {
            return;
        }

        for (idx, byte) in data.iter_mut().enumerate() {
            if self.rng.gen::<f32>() < self.config.bit_flip_probability {
                let bit_pos = self.rng.gen_range(0..8);
                *byte ^= 1 << bit_pos;
                println!("Fault injected: Bit flip at byte {}, bit {}", idx, bit_pos);
            }
        }
    }

    /// Inject byte corruption
    fn inject_corruption(&mut self, data: &mut [u8]) {
        if !self.config.enable_corruption {
            return;
        }

        if self.rng.gen::<f32>() < self.config.corruption_probability {
            let corrupt_pos = self.rng.gen_range(0..data.len());
            data[corrupt_pos] = self.rng.gen();
            println!("Fault injected: Byte corruption at position {}", corrupt_pos);
        }
    }

    /// Perform SPI transfer with fault injection
    pub fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> SpiStatus {
        assert_eq!(tx_data.len(), rx_data.len());

        // Simulate CS glitch
        if self.config.enable_cs_glitch && self.rng.gen::<f32>() < 0.1 {
            println!("Fault injected: CS glitch");
            return SpiStatus::HardwareFault;
        }

        // Simulate timeout
        if self.config.enable_timeout && self.rng.gen::<f32>() < 0.05 {
            println!("Fault injected: Timeout");
            return SpiStatus::Timeout;
        }

        // Simulate actual hardware transfer
        rx_data.copy_from_slice(tx_data);

        // Inject faults into received data
        self.inject_bit_flip(rx_data);
        self.inject_corruption(rx_data);

        SpiStatus::Success
    }
}

/// Calculate CRC-8 for error detection
fn calculate_crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF;
    
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    
    crc
}

/// Robust SPI transfer with retry logic
pub fn robust_transfer(
    injector: &mut SpiFaultInjector,
    tx_data: &[u8],
    rx_data: &mut [u8],
    max_retries: u8,
) -> SpiStatus {
    let expected_crc = calculate_crc8(tx_data);

    for retry in 0..max_retries {
        let status = injector.transfer(tx_data, rx_data);

        match status {
            SpiStatus::Timeout | SpiStatus::HardwareFault => {
                println!("Retry {}/{} due to {:?}", retry + 1, max_retries, status);
                continue;
            }
            SpiStatus::Success => {
                let received_crc = calculate_crc8(rx_data);
                if received_crc != expected_crc {
                    println!(
                        "Retry {}/{} due to CRC mismatch (expected: 0x{:02X}, got: 0x{:02X})",
                        retry + 1, max_retries, expected_crc, received_crc
                    );
                    continue;
                }
                return SpiStatus::Success;
            }
            _ => continue,
        }
    }

    SpiStatus::CrcError
}

fn main() {
    let config = FaultConfig {
        enable_bit_flip: true,
        bit_flip_probability: 0.01,
        enable_timeout: true,
        timeout_ms: 100,
        enable_corruption: true,
        corruption_probability: 0.05,
        enable_cs_glitch: true,
    };

    let mut injector = SpiFaultInjector::new(config);
    
    let tx_buffer: Vec<u8> = (1..=16).collect();
    let mut rx_buffer = vec![0u8; 16];

    println!("Starting fault injection test...");
    let result = robust_transfer(&mut injector, &tx_buffer, &mut rx_buffer, 5);

    match result {
        SpiStatus::Success => println!("Transfer succeeded after fault recovery"),
        _ => println!("Transfer failed after all retries: {:?}", result),
    }
}
```

## Testing Strategies

### 1. **Deterministic Testing**
Use seeded random number generators to ensure reproducible test results, as shown in the examples above.

### 2. **Stress Testing**
Gradually increase fault injection rates to find the breaking point of your error recovery mechanisms.

### 3. **Edge Case Testing**
- Inject faults at critical moments (first byte, last byte, during handshake)
- Test boundary conditions (maximum retry counts, timeout thresholds)

### 4. **Statistical Analysis**
Track metrics like:
- Success rate vs. fault injection rate
- Average number of retries needed
- Types of faults that cause the most failures

### 5. **Hardware-in-the-Loop**
Combine software fault injection with physical signal manipulation using tools like logic analyzers or programmable signal generators.

## Best Practices

1. **Implement Checksums/CRC**: Always validate data integrity
2. **Use Retry Logic**: Don't give up on first failure
3. **Set Reasonable Timeouts**: Balance responsiveness with reliability
4. **Log Failures**: Track what types of faults occur in production
5. **Graceful Degradation**: System should enter safe state on unrecoverable errors
6. **Test Coverage**: Inject faults at every layer (bit, byte, transaction, protocol)

## Summary

SPI fault injection is an essential testing technique for building robust embedded systems. By simulating real-world communication failures—including bit flips, timeouts, corrupted data, and hardware glitches—developers can verify that their error handling, retry mechanisms, and recovery logic work correctly before deployment.

The provided C/C++ and Rust examples demonstrate practical approaches to fault injection, including:
- Configurable fault types and probabilities
- Bit-level and byte-level corruption
- Timeout and hardware fault simulation
- CRC-based error detection
- Retry logic with exponential backoff potential

Implementing comprehensive fault injection testing ensures your SPI-based systems maintain reliability even in harsh electromagnetic environments, with unreliable connections, or under component failures. This proactive approach to testing prevents costly field failures and improves overall system robustness.