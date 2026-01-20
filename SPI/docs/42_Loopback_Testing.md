# SPI Loopback Testing

## Overview

Loopback testing is a crucial diagnostic technique for validating SPI (Serial Peripheral Interface) functionality by creating a physical or software connection between the Master Out Slave In (MOSI) and Master In Slave Out (MISO) lines. This allows the SPI master to transmit data and immediately receive it back, verifying the integrity of the communication path, timing, and hardware configuration.

## Why Loopback Testing?

Loopback testing serves several important purposes:

- **Hardware Validation**: Confirms that the SPI peripheral and GPIO pins are functioning correctly
- **Driver Verification**: Tests SPI driver implementation without requiring external slave devices
- **Signal Integrity Checking**: Validates clock polarity, phase, and timing parameters
- **Debugging Aid**: Isolates issues between the SPI controller and external devices
- **Manufacturing Testing**: Used in production to verify board-level SPI functionality

## How It Works

In loopback mode, any data transmitted on MOSI is simultaneously received on MISO. The master sends a known pattern and verifies that the same pattern is received back. This can be implemented in two ways:

1. **Hardware Loopback**: Physically connecting MOSI to MISO with a jumper wire
2. **Software/Internal Loopback**: Some microcontrollers offer built-in loopback modes where the connection is made internally

## C/C++ Implementation Examples

### Basic Hardware Loopback Test (Embedded C)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Platform-specific SPI register definitions (example for STM32)
#define SPI1_BASE 0x40013000
#define SPI_CR1   (*(volatile uint32_t*)(SPI1_BASE + 0x00))
#define SPI_CR2   (*(volatile uint32_t*)(SPI1_BASE + 0x04))
#define SPI_SR    (*(volatile uint32_t*)(SPI1_BASE + 0x08))
#define SPI_DR    (*(volatile uint32_t*)(SPI1_BASE + 0x0C))

#define SPI_SR_TXE  (1 << 1)  // Transmit buffer empty
#define SPI_SR_RXNE (1 << 0)  // Receive buffer not empty
#define SPI_SR_BSY  (1 << 7)  // Busy flag

// Basic SPI transmit/receive function
uint8_t spi_transfer(uint8_t data) {
    // Wait until transmit buffer is empty
    while (!(SPI_SR & SPI_SR_TXE));
    
    // Send data
    SPI_DR = data;
    
    // Wait until receive buffer is not empty
    while (!(SPI_SR & SPI_SR_RXNE));
    
    // Return received data
    return (uint8_t)SPI_DR;
}

// Loopback test with single byte
bool spi_loopback_test_byte(uint8_t test_value) {
    uint8_t received = spi_transfer(test_value);
    return (received == test_value);
}

// Comprehensive loopback test with multiple patterns
typedef struct {
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint8_t failed_patterns[16];
    uint8_t failed_count;
} LoopbackTestResult;

LoopbackTestResult spi_loopback_test_comprehensive(void) {
    LoopbackTestResult result = {0};
    
    // Test pattern array
    uint8_t test_patterns[] = {
        0x00,  // All zeros
        0xFF,  // All ones
        0xAA,  // Alternating 10101010
        0x55,  // Alternating 01010101
        0xF0,  // High nibble
        0x0F,  // Low nibble
        0x5A,  // Mixed pattern
        0xA5,  // Inverse mixed
    };
    
    for (size_t i = 0; i < sizeof(test_patterns); i++) {
        uint8_t sent = test_patterns[i];
        uint8_t received = spi_transfer(sent);
        
        if (received == sent) {
            result.tests_passed++;
        } else {
            result.tests_failed++;
            if (result.failed_count < 16) {
                result.failed_patterns[result.failed_count++] = sent;
            }
        }
    }
    
    return result;
}

// Multi-byte loopback test
bool spi_loopback_test_buffer(const uint8_t* tx_buffer, 
                               uint8_t* rx_buffer, 
                               size_t length) {
    for (size_t i = 0; i < length; i++) {
        rx_buffer[i] = spi_transfer(tx_buffer[i]);
    }
    
    // Compare buffers
    return (memcmp(tx_buffer, rx_buffer, length) == 0);
}

// Example usage
void run_loopback_tests(void) {
    // Single byte test
    if (spi_loopback_test_byte(0x5A)) {
        // Test passed
    }
    
    // Comprehensive test
    LoopbackTestResult result = spi_loopback_test_comprehensive();
    
    // Buffer test
    uint8_t tx_data[64];
    uint8_t rx_data[64];
    
    // Fill with sequential pattern
    for (int i = 0; i < 64; i++) {
        tx_data[i] = i;
    }
    
    bool buffer_test_passed = spi_loopback_test_buffer(tx_data, rx_data, 64);
}
```

### C++ Class-Based Loopback Testing

```cpp
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

class SPILoopbackTester {
private:
    // Hardware abstraction layer function pointer
    using TransferFunc = uint8_t(*)(uint8_t);
    TransferFunc spi_transfer_;
    
    std::vector<uint8_t> failed_patterns_;
    uint32_t passed_count_;
    uint32_t failed_count_;
    
public:
    explicit SPILoopbackTester(TransferFunc transfer_func) 
        : spi_transfer_(transfer_func), passed_count_(0), failed_count_(0) {}
    
    // Test single byte
    bool testByte(uint8_t value) {
        uint8_t received = spi_transfer_(value);
        bool passed = (received == value);
        
        if (passed) {
            passed_count_++;
        } else {
            failed_count_++;
            failed_patterns_.push_back(value);
        }
        
        return passed;
    }
    
    // Test standard patterns
    bool testStandardPatterns() {
        constexpr std::array<uint8_t, 8> patterns = {
            0x00, 0xFF, 0xAA, 0x55, 0xF0, 0x0F, 0x5A, 0xA5
        };
        
        bool all_passed = true;
        for (uint8_t pattern : patterns) {
            if (!testByte(pattern)) {
                all_passed = false;
            }
        }
        
        return all_passed;
    }
    
    // Test buffer transfer
    bool testBuffer(const std::vector<uint8_t>& tx_data, 
                    std::vector<uint8_t>& rx_data) {
        rx_data.resize(tx_data.size());
        
        for (size_t i = 0; i < tx_data.size(); i++) {
            rx_data[i] = spi_transfer_(tx_data[i]);
        }
        
        return std::equal(tx_data.begin(), tx_data.end(), rx_data.begin());
    }
    
    // Walking bit pattern test (tests each bit position)
    bool testWalkingBits() {
        bool all_passed = true;
        
        // Test walking 1s
        for (int i = 0; i < 8; i++) {
            uint8_t pattern = 1 << i;
            if (!testByte(pattern)) {
                all_passed = false;
            }
        }
        
        // Test walking 0s
        for (int i = 0; i < 8; i++) {
            uint8_t pattern = ~(1 << i);
            if (!testByte(pattern)) {
                all_passed = false;
            }
        }
        
        return all_passed;
    }
    
    // Get test statistics
    struct TestStats {
        uint32_t passed;
        uint32_t failed;
        std::vector<uint8_t> failed_patterns;
    };
    
    TestStats getStats() const {
        return {passed_count_, failed_count_, failed_patterns_};
    }
    
    void resetStats() {
        passed_count_ = 0;
        failed_count_ = 0;
        failed_patterns_.clear();
    }
};

// Example usage
extern "C" uint8_t hardware_spi_transfer(uint8_t data);

void run_cpp_loopback_test() {
    SPILoopbackTester tester(hardware_spi_transfer);
    
    // Run standard pattern tests
    if (tester.testStandardPatterns()) {
        // All standard patterns passed
    }
    
    // Run walking bit tests
    if (tester.testWalkingBits()) {
        // All bit patterns passed
    }
    
    // Test buffer transfer
    std::vector<uint8_t> tx_data(256);
    std::vector<uint8_t> rx_data;
    
    for (size_t i = 0; i < tx_data.size(); i++) {
        tx_data[i] = static_cast<uint8_t>(i);
    }
    
    if (tester.testBuffer(tx_data, rx_data)) {
        // Buffer test passed
    }
    
    auto stats = tester.getStats();
}
```

## Rust Implementation Examples

### Basic Loopback Testing in Rust

```rust
/// SPI transfer trait abstraction
pub trait SpiTransfer {
    fn transfer(&mut self, data: u8) -> Result<u8, SpiError>;
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiError {
    TransferTimeout,
    HardwareError,
    LoopbackMismatch,
}

/// Loopback test result
#[derive(Debug, Default)]
pub struct LoopbackTestResult {
    pub tests_passed: u32,
    pub tests_failed: u32,
    pub failed_patterns: Vec<u8>,
}

/// SPI Loopback Tester
pub struct SpiLoopbackTester<T: SpiTransfer> {
    spi: T,
    results: LoopbackTestResult,
}

impl<T: SpiTransfer> SpiLoopbackTester<T> {
    pub fn new(spi: T) -> Self {
        Self {
            spi,
            results: LoopbackTestResult::default(),
        }
    }
    
    /// Test a single byte
    pub fn test_byte(&mut self, value: u8) -> Result<bool, SpiError> {
        let received = self.spi.transfer(value)?;
        let passed = received == value;
        
        if passed {
            self.results.tests_passed += 1;
        } else {
            self.results.tests_failed += 1;
            self.results.failed_patterns.push(value);
        }
        
        Ok(passed)
    }
    
    /// Test standard bit patterns
    pub fn test_standard_patterns(&mut self) -> Result<bool, SpiError> {
        const PATTERNS: [u8; 8] = [
            0x00, // All zeros
            0xFF, // All ones
            0xAA, // Alternating 10101010
            0x55, // Alternating 01010101
            0xF0, // High nibble
            0x0F, // Low nibble
            0x5A, // Mixed pattern
            0xA5, // Inverse mixed
        ];
        
        let mut all_passed = true;
        
        for &pattern in &PATTERNS {
            if !self.test_byte(pattern)? {
                all_passed = false;
            }
        }
        
        Ok(all_passed)
    }
    
    /// Test walking bit patterns (tests each bit position)
    pub fn test_walking_bits(&mut self) -> Result<bool, SpiError> {
        let mut all_passed = true;
        
        // Test walking 1s (00000001, 00000010, 00000100, etc.)
        for i in 0..8 {
            let pattern = 1u8 << i;
            if !self.test_byte(pattern)? {
                all_passed = false;
            }
        }
        
        // Test walking 0s (11111110, 11111101, 11111011, etc.)
        for i in 0..8 {
            let pattern = !(1u8 << i);
            if !self.test_byte(pattern)? {
                all_passed = false;
            }
        }
        
        Ok(all_passed)
    }
    
    /// Test buffer transfer
    pub fn test_buffer(&mut self, tx_data: &[u8]) -> Result<Vec<u8>, SpiError> {
        let mut rx_data = Vec::with_capacity(tx_data.len());
        
        for &byte in tx_data {
            let received = self.spi.transfer(byte)?;
            rx_data.push(received);
        }
        
        Ok(rx_data)
    }
    
    /// Verify buffer loopback
    pub fn verify_buffer_loopback(&mut self, tx_data: &[u8]) -> Result<bool, SpiError> {
        let rx_data = self.test_buffer(tx_data)?;
        Ok(tx_data == rx_data.as_slice())
    }
    
    /// Get test results
    pub fn results(&self) -> &LoopbackTestResult {
        &self.results
    }
    
    /// Reset test statistics
    pub fn reset_stats(&mut self) {
        self.results = LoopbackTestResult::default();
    }
}

// Example embedded HAL implementation
use embedded_hal::spi::SpiBus;

pub struct EmbeddedHalSpi<SPI> {
    spi: SPI,
}

impl<SPI: SpiBus<u8>> SpiTransfer for EmbeddedHalSpi<SPI> {
    fn transfer(&mut self, data: u8) -> Result<u8, SpiError> {
        let mut buffer = [data];
        self.spi.transfer_in_place(&mut buffer)
            .map_err(|_| SpiError::HardwareError)?;
        Ok(buffer[0])
    }
}

// Example usage
#[cfg(feature = "example")]
pub fn run_loopback_tests<SPI: SpiBus<u8>>(spi: SPI) -> Result<(), SpiError> {
    let spi_wrapper = EmbeddedHalSpi { spi };
    let mut tester = SpiLoopbackTester::new(spi_wrapper);
    
    // Test standard patterns
    println!("Running standard pattern tests...");
    if tester.test_standard_patterns()? {
        println!("Standard patterns: PASSED");
    } else {
        println!("Standard patterns: FAILED");
    }
    
    // Test walking bits
    println!("Running walking bit tests...");
    if tester.test_walking_bits()? {
        println!("Walking bits: PASSED");
    } else {
        println!("Walking bits: FAILED");
    }
    
    // Test sequential buffer
    let tx_data: Vec<u8> = (0..=255).collect();
    println!("Running buffer test (256 bytes)...");
    if tester.verify_buffer_loopback(&tx_data)? {
        println!("Buffer test: PASSED");
    } else {
        println!("Buffer test: FAILED");
    }
    
    // Print results
    let results = tester.results();
    println!("\nTest Results:");
    println!("  Passed: {}", results.tests_passed);
    println!("  Failed: {}", results.tests_failed);
    
    if !results.failed_patterns.is_empty() {
        println!("  Failed patterns: {:02X?}", results.failed_patterns);
    }
    
    Ok(())
}
```

### Advanced Rust Implementation with Timing Analysis

```rust
use core::time::Duration;

#[derive(Debug, Clone)]
pub struct TimingStats {
    pub min_transfer_time: Duration,
    pub max_transfer_time: Duration,
    pub avg_transfer_time: Duration,
    pub total_transfers: u32,
}

pub struct AdvancedLoopbackTester<T: SpiTransfer> {
    spi: T,
    timing_enabled: bool,
    timing_stats: Option<TimingStats>,
}

impl<T: SpiTransfer> AdvancedLoopbackTester<T> {
    pub fn new(spi: T) -> Self {
        Self {
            spi,
            timing_enabled: false,
            timing_stats: None,
        }
    }
    
    pub fn enable_timing(&mut self) {
        self.timing_enabled = true;
        self.timing_stats = Some(TimingStats {
            min_transfer_time: Duration::MAX,
            max_transfer_time: Duration::ZERO,
            avg_transfer_time: Duration::ZERO,
            total_transfers: 0,
        });
    }
    
    /// Test with throughput measurement
    pub fn benchmark_throughput(&mut self, data_size: usize) 
        -> Result<(bool, f64), SpiError> 
    {
        let test_data: Vec<u8> = (0..data_size).map(|i| i as u8).collect();
        
        // Simulate timing (in real implementation, use platform timer)
        let start_time = Duration::from_millis(0); // Replace with actual timer
        
        let mut rx_data = Vec::with_capacity(data_size);
        for &byte in &test_data {
            let received = self.spi.transfer(byte)?;
            rx_data.push(received);
        }
        
        let elapsed = Duration::from_millis(100); // Replace with actual elapsed time
        
        let bytes_per_sec = (data_size as f64) / elapsed.as_secs_f64();
        let all_match = test_data == rx_data;
        
        Ok((all_match, bytes_per_sec))
    }
    
    pub fn timing_stats(&self) -> Option<&TimingStats> {
        self.timing_stats.as_ref()
    }
}
```

## Summary

**SPI Loopback Testing** is an essential diagnostic and validation technique that verifies SPI communication functionality by connecting the MOSI and MISO lines, either physically or internally. This method allows transmitted data to be immediately received back, confirming proper operation of the SPI peripheral, drivers, and signal integrity.

**Key Benefits:**
- Hardware and driver validation without external devices
- Isolation of communication issues
- Production testing capability
- Signal integrity verification

**Implementation approaches** include testing single bytes, comprehensive pattern testing (alternating bits, walking bits, all zeros/ones), buffer transfers, and timing analysis. The code examples demonstrate progressively sophisticated testing strategies across C, C++, and Rust, from basic byte transfers to class-based and trait-based architectures with comprehensive statistics and error handling.

**Common test patterns** include 0x00, 0xFF, 0xAA, 0x55, walking 1s (0x01, 0x02, 0x04...), and walking 0s (0xFE, 0xFD, 0xFB...), each designed to validate different aspects of the communication path and bit integrity.