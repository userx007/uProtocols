# I2C Loopback Testing: Self-Test Mechanisms for Validating I2C Controller Functionality

## Overview

I2C loopback testing is a critical self-test mechanism used to validate I2C controller functionality without requiring external devices. This technique involves configuring the I2C controller to route its transmit signals back to its receive inputs, allowing the system to verify proper operation of the I2C hardware, drivers, and protocol implementation.

## Purpose and Benefits

Loopback testing serves several important purposes:

- **Hardware Validation**: Verifies that the I2C controller hardware is functioning correctly
- **Driver Testing**: Validates that software drivers can properly control the hardware
- **Manufacturing Testing**: Provides a quick test during production without external I2C devices
- **Diagnostics**: Helps isolate issues between controller problems and bus/device problems
- **Regression Testing**: Ensures firmware updates haven't broken I2C functionality

## Loopback Testing Methods

### 1. Physical Loopback
Connecting SDA to SDA and SCL to SCL externally using jumper wires or test fixtures. This tests the complete signal path including I/O pins.

### 2. Internal Loopback
Some I2C controllers support internal loopback modes where data is routed internally within the chip without going through external pins.

### 3. Multi-Controller Loopback
Using two I2C controllers on the same chip, with one acting as controller and another as target, both connected to the same internal bus.

## Code Examples

### C Implementation

Here's a comprehensive C implementation for I2C loopback testing:

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// I2C Register definitions (example for a generic controller)
#define I2C_BASE_ADDR       0x40005400
#define I2C_CR1             (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x00))
#define I2C_CR2             (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x04))
#define I2C_OAR1            (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x08))
#define I2C_DR              (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x10))
#define I2C_SR1             (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x14))
#define I2C_SR2             (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x18))

// Control register bits
#define I2C_CR1_PE          (1 << 0)   // Peripheral enable
#define I2C_CR1_START       (1 << 8)   // Start generation
#define I2C_CR1_STOP        (1 << 9)   // Stop generation
#define I2C_CR1_ACK         (1 << 10)  // Acknowledge enable
#define I2C_CR1_LOOPBACK    (1 << 15)  // Loopback mode (if supported)

// Status register bits
#define I2C_SR1_SB          (1 << 0)   // Start bit
#define I2C_SR1_ADDR        (1 << 1)   // Address sent/matched
#define I2C_SR1_BTF         (1 << 2)   // Byte transfer finished
#define I2C_SR1_TXE         (1 << 7)   // Data register empty
#define I2C_SR1_RXNE        (1 << 6)   // Data register not empty

// Test configuration
#define LOOPBACK_ADDRESS    0x50
#define TEST_DATA_SIZE      16
#define TIMEOUT_MS          1000

typedef enum {
    LOOPBACK_SUCCESS = 0,
    LOOPBACK_TIMEOUT,
    LOOPBACK_DATA_MISMATCH,
    LOOPBACK_HARDWARE_ERROR,
    LOOPBACK_INIT_FAILED
} loopback_status_t;

typedef struct {
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t timeouts;
    uint32_t data_errors;
} loopback_stats_t;

static loopback_stats_t test_stats = {0};

/**
 * @brief Initialize I2C controller for loopback testing
 * @return true if initialization successful
 */
bool i2c_loopback_init(void) {
    // Disable I2C peripheral
    I2C_CR1 &= ~I2C_CR1_PE;
    
    // Configure I2C timing (example for 100kHz, adjust for your clock)
    // This is controller-specific
    I2C_CR2 = 0x0010; // Example timing value
    
    // Set own address for target mode
    I2C_OAR1 = (LOOPBACK_ADDRESS << 1) | (1 << 15); // Enable own address
    
    // Enable acknowledgment
    I2C_CR1 |= I2C_CR1_ACK;
    
    // Enable loopback mode if supported
    #ifdef I2C_CR1_LOOPBACK
    I2C_CR1 |= I2C_CR1_LOOPBACK;
    #endif
    
    // Enable I2C peripheral
    I2C_CR1 |= I2C_CR1_PE;
    
    return true;
}

/**
 * @brief Wait for a specific status flag with timeout
 */
static bool wait_for_flag(volatile uint32_t* reg, uint32_t flag, 
                          bool set, uint32_t timeout_ms) {
    uint32_t start_time = get_tick_count(); // Platform-specific
    
    while ((get_tick_count() - start_time) < timeout_ms) {
        bool flag_state = (*reg & flag) != 0;
        if (flag_state == set) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Perform I2C write in controller mode
 */
static loopback_status_t i2c_loopback_write(uint8_t addr, 
                                            const uint8_t* data, 
                                            size_t len) {
    // Generate start condition
    I2C_CR1 |= I2C_CR1_START;
    
    // Wait for start bit
    if (!wait_for_flag(&I2C_SR1, I2C_SR1_SB, true, TIMEOUT_MS)) {
        return LOOPBACK_TIMEOUT;
    }
    
    // Send address with write bit
    I2C_DR = (addr << 1) | 0;
    
    // Wait for address acknowledged
    if (!wait_for_flag(&I2C_SR1, I2C_SR1_ADDR, true, TIMEOUT_MS)) {
        return LOOPBACK_TIMEOUT;
    }
    
    // Clear ADDR flag by reading SR1 and SR2
    (void)I2C_SR1;
    (void)I2C_SR2;
    
    // Send data bytes
    for (size_t i = 0; i < len; i++) {
        // Wait for TXE (transmit buffer empty)
        if (!wait_for_flag(&I2C_SR1, I2C_SR1_TXE, true, TIMEOUT_MS)) {
            return LOOPBACK_TIMEOUT;
        }
        
        I2C_DR = data[i];
    }
    
    // Wait for byte transfer finished
    if (!wait_for_flag(&I2C_SR1, I2C_SR1_BTF, true, TIMEOUT_MS)) {
        return LOOPBACK_TIMEOUT;
    }
    
    // Generate stop condition
    I2C_CR1 |= I2C_CR1_STOP;
    
    return LOOPBACK_SUCCESS;
}

/**
 * @brief Perform I2C read in controller mode
 */
static loopback_status_t i2c_loopback_read(uint8_t addr, 
                                           uint8_t* data, 
                                           size_t len) {
    // Generate start condition
    I2C_CR1 |= I2C_CR1_START;
    
    // Wait for start bit
    if (!wait_for_flag(&I2C_SR1, I2C_SR1_SB, true, TIMEOUT_MS)) {
        return LOOPBACK_TIMEOUT;
    }
    
    // Send address with read bit
    I2C_DR = (addr << 1) | 1;
    
    // Wait for address acknowledged
    if (!wait_for_flag(&I2C_SR1, I2C_SR1_ADDR, true, TIMEOUT_MS)) {
        return LOOPBACK_TIMEOUT;
    }
    
    // Clear ADDR flag
    (void)I2C_SR1;
    (void)I2C_SR2;
    
    // Read data bytes
    for (size_t i = 0; i < len; i++) {
        if (i == len - 1) {
            // Last byte: disable ACK
            I2C_CR1 &= ~I2C_CR1_ACK;
            // Generate stop
            I2C_CR1 |= I2C_CR1_STOP;
        }
        
        // Wait for RXNE (receive buffer not empty)
        if (!wait_for_flag(&I2C_SR1, I2C_SR1_RXNE, true, TIMEOUT_MS)) {
            return LOOPBACK_TIMEOUT;
        }
        
        data[i] = I2C_DR;
    }
    
    // Re-enable ACK for next transfer
    I2C_CR1 |= I2C_CR1_ACK;
    
    return LOOPBACK_SUCCESS;
}

/**
 * @brief Perform comprehensive loopback test
 */
loopback_status_t i2c_loopback_test(void) {
    uint8_t write_data[TEST_DATA_SIZE];
    uint8_t read_data[TEST_DATA_SIZE];
    loopback_status_t status;
    
    // Initialize test data with pattern
    for (size_t i = 0; i < TEST_DATA_SIZE; i++) {
        write_data[i] = (uint8_t)(0xA5 ^ i); // Pattern: 0xA5, 0xA4, 0xA7...
    }
    
    printf("Starting I2C loopback test...\n");
    printf("Test address: 0x%02X\n", LOOPBACK_ADDRESS);
    printf("Data size: %d bytes\n", TEST_DATA_SIZE);
    
    // Perform write operation
    printf("Writing data...\n");
    status = i2c_loopback_write(LOOPBACK_ADDRESS, write_data, TEST_DATA_SIZE);
    if (status != LOOPBACK_SUCCESS) {
        printf("Write failed with status: %d\n", status);
        test_stats.tests_failed++;
        if (status == LOOPBACK_TIMEOUT) test_stats.timeouts++;
        return status;
    }
    
    // Small delay between write and read
    delay_ms(10);
    
    // Perform read operation
    printf("Reading data...\n");
    memset(read_data, 0, TEST_DATA_SIZE);
    status = i2c_loopback_read(LOOPBACK_ADDRESS, read_data, TEST_DATA_SIZE);
    if (status != LOOPBACK_SUCCESS) {
        printf("Read failed with status: %d\n", status);
        test_stats.tests_failed++;
        if (status == LOOPBACK_TIMEOUT) test_stats.timeouts++;
        return status;
    }
    
    // Verify data
    printf("Verifying data...\n");
    bool data_match = true;
    for (size_t i = 0; i < TEST_DATA_SIZE; i++) {
        if (write_data[i] != read_data[i]) {
            printf("Mismatch at byte %zu: expected 0x%02X, got 0x%02X\n",
                   i, write_data[i], read_data[i]);
            data_match = false;
            test_stats.data_errors++;
        }
    }
    
    if (!data_match) {
        test_stats.tests_failed++;
        return LOOPBACK_DATA_MISMATCH;
    }
    
    test_stats.tests_passed++;
    printf("Loopback test PASSED!\n");
    return LOOPBACK_SUCCESS;
}

/**
 * @brief Run multiple loopback tests with different patterns
 */
void i2c_loopback_test_suite(void) {
    const uint8_t test_patterns[] = {
        0x00, 0xFF, 0xAA, 0x55, 0xA5, 0x5A
    };
    
    printf("\n=== I2C Loopback Test Suite ===\n\n");
    
    if (!i2c_loopback_init()) {
        printf("Failed to initialize I2C for loopback testing\n");
        return;
    }
    
    // Test different data patterns
    for (size_t p = 0; p < sizeof(test_patterns); p++) {
        uint8_t pattern = test_patterns[p];
        uint8_t write_buf[8], read_buf[8];
        
        printf("Test %zu: Pattern 0x%02X\n", p + 1, pattern);
        
        memset(write_buf, pattern, sizeof(write_buf));
        memset(read_buf, 0, sizeof(read_buf));
        
        if (i2c_loopback_write(LOOPBACK_ADDRESS, write_buf, sizeof(write_buf)) 
            == LOOPBACK_SUCCESS) {
            delay_ms(5);
            if (i2c_loopback_read(LOOPBACK_ADDRESS, read_buf, sizeof(read_buf)) 
                == LOOPBACK_SUCCESS) {
                if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0) {
                    printf("  PASS\n");
                    test_stats.tests_passed++;
                } else {
                    printf("  FAIL: Data mismatch\n");
                    test_stats.tests_failed++;
                    test_stats.data_errors++;
                }
            }
        }
    }
    
    printf("\n=== Test Statistics ===\n");
    printf("Tests passed: %u\n", test_stats.tests_passed);
    printf("Tests failed: %u\n", test_stats.tests_failed);
    printf("Timeouts: %u\n", test_stats.timeouts);
    printf("Data errors: %u\n", test_stats.data_errors);
}
```

### C++ Implementation

Here's a modern C++ implementation with RAII and better abstraction:

```cpp
#include <cstdint>
#include <vector>
#include <array>
#include <memory>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <stdexcept>

class I2CLoopbackTester {
public:
    enum class Status {
        SUCCESS,
        TIMEOUT,
        DATA_MISMATCH,
        HARDWARE_ERROR,
        INIT_FAILED
    };
    
    struct TestResult {
        Status status;
        size_t bytes_tested;
        std::chrono::microseconds duration;
        std::vector<size_t> error_positions;
    };
    
    struct Statistics {
        uint32_t tests_passed{0};
        uint32_t tests_failed{0};
        uint32_t timeouts{0};
        uint32_t data_errors{0};
        std::chrono::microseconds total_duration{0};
    };

private:
    static constexpr uint32_t I2C_BASE = 0x40005400;
    static constexpr uint8_t LOOPBACK_ADDR = 0x50;
    static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT{1000};
    
    volatile uint32_t* const cr1_;
    volatile uint32_t* const cr2_;
    volatile uint32_t* const dr_;
    volatile uint32_t* const sr1_;
    volatile uint32_t* const sr2_;
    
    Statistics stats_;
    bool initialized_{false};

public:
    I2CLoopbackTester() 
        : cr1_(reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x00)),
          cr2_(reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x04)),
          dr_(reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x10)),
          sr1_(reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x14)),
          sr2_(reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x18)) {
    }
    
    ~I2CLoopbackTester() {
        if (initialized_) {
            disable();
        }
    }
    
    // Prevent copying
    I2CLoopbackTester(const I2CLoopbackTester&) = delete;
    I2CLoopbackTester& operator=(const I2CLoopbackTester&) = delete;
    
    /**
     * @brief Initialize the I2C controller for loopback testing
     */
    void initialize() {
        // Disable peripheral
        *cr1_ &= ~(1 << 0);
        
        // Configure timing
        *cr2_ = 0x0010;
        
        // Set own address
        volatile uint32_t* oar1 = reinterpret_cast<volatile uint32_t*>(I2C_BASE + 0x08);
        *oar1 = (LOOPBACK_ADDR << 1) | (1 << 15);
        
        // Enable ACK and loopback mode
        *cr1_ |= (1 << 10); // ACK
        *cr1_ |= (1 << 15); // Loopback (if supported)
        
        // Enable peripheral
        *cr1_ |= (1 << 0);
        
        initialized_ = true;
    }
    
    /**
     * @brief Disable the I2C controller
     */
    void disable() {
        *cr1_ &= ~(1 << 0);
        initialized_ = false;
    }
    
    /**
     * @brief Test with a specific data pattern
     */
    TestResult testPattern(const std::vector<uint8_t>& data) {
        if (!initialized_) {
            throw std::runtime_error("I2C not initialized");
        }
        
        auto start_time = std::chrono::steady_clock::now();
        TestResult result;
        result.bytes_tested = data.size();
        
        // Write data
        Status write_status = write(LOOPBACK_ADDR, data);
        if (write_status != Status::SUCCESS) {
            result.status = write_status;
            updateStats(result);
            return result;
        }
        
        // Small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Read data
        std::vector<uint8_t> read_data(data.size());
        Status read_status = read(LOOPBACK_ADDR, read_data);
        if (read_status != Status::SUCCESS) {
            result.status = read_status;
            updateStats(result);
            return result;
        }
        
        // Verify data
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] != read_data[i]) {
                result.error_positions.push_back(i);
            }
        }
        
        result.status = result.error_positions.empty() ? 
                       Status::SUCCESS : Status::DATA_MISMATCH;
        
        auto end_time = std::chrono::steady_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        
        updateStats(result);
        return result;
    }
    
    /**
     * @brief Run comprehensive test suite
     */
    void runTestSuite() {
        std::cout << "\n=== I2C Loopback Test Suite ===\n\n";
        
        // Test patterns
        std::vector<std::pair<std::string, std::vector<uint8_t>>> tests = {
            {"All zeros", std::vector<uint8_t>(16, 0x00)},
            {"All ones", std::vector<uint8_t>(16, 0xFF)},
            {"Alternating 0xAA", std::vector<uint8_t>(16, 0xAA)},
            {"Alternating 0x55", std::vector<uint8_t>(16, 0x55)},
            {"Sequential", generateSequential(16)},
            {"Random", generateRandom(16)}
        };
        
        for (const auto& [name, data] : tests) {
            std::cout << "Test: " << name << " (" << data.size() << " bytes)\n";
            
            auto result = testPattern(data);
            
            std::cout << "  Status: " << statusToString(result.status) << "\n";
            std::cout << "  Duration: " << result.duration.count() << " µs\n";
            
            if (result.status == Status::DATA_MISMATCH) {
                std::cout << "  Errors at positions: ";
                for (size_t pos : result.error_positions) {
                    std::cout << pos << " ";
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }
        
        printStatistics();
    }
    
    /**
     * @brief Get current statistics
     */
    const Statistics& getStatistics() const {
        return stats_;
    }
    
    /**
     * @brief Reset statistics
     */
    void resetStatistics() {
        stats_ = Statistics{};
    }
    
    /**
     * @brief Print detailed statistics
     */
    void printStatistics() const {
        std::cout << "=== Test Statistics ===\n";
        std::cout << "Tests passed: " << stats_.tests_passed << "\n";
        std::cout << "Tests failed: " << stats_.tests_failed << "\n";
        std::cout << "Timeouts: " << stats_.timeouts << "\n";
        std::cout << "Data errors: " << stats_.data_errors << "\n";
        std::cout << "Total duration: " << stats_.total_duration.count() << " µs\n";
        
        if (stats_.tests_passed + stats_.tests_failed > 0) {
            double pass_rate = 100.0 * stats_.tests_passed / 
                              (stats_.tests_passed + stats_.tests_failed);
            std::cout << "Pass rate: " << std::fixed << std::setprecision(2) 
                     << pass_rate << "%\n";
        }
    }

private:
    /**
     * @brief Write data to I2C
     */
    Status write(uint8_t addr, const std::vector<uint8_t>& data) {
        // Generate start
        *cr1_ |= (1 << 8);
        if (!waitForFlag(sr1_, (1 << 0), true, DEFAULT_TIMEOUT)) {
            return Status::TIMEOUT;
        }
        
        // Send address
        *dr_ = (addr << 1);
        if (!waitForFlag(sr1_, (1 << 1), true, DEFAULT_TIMEOUT)) {
            return Status::TIMEOUT;
        }
        
        // Clear ADDR
        (void)*sr1_;
        (void)*sr2_;
        
        // Send data
        for (uint8_t byte : data) {
            if (!waitForFlag(sr1_, (1 << 7), true, DEFAULT_TIMEOUT)) {
                return Status::TIMEOUT;
            }
            *dr_ = byte;
        }
        
        // Wait for BTF
        if (!waitForFlag(sr1_, (1 << 2), true, DEFAULT_TIMEOUT)) {
            return Status::TIMEOUT;
        }
        
        // Generate stop
        *cr1_ |= (1 << 9);
        
        return Status::SUCCESS;
    }
    
    /**
     * @brief Read data from I2C
     */
    Status read(uint8_t addr, std::vector<uint8_t>& data) {
        // Generate start
        *cr1_ |= (1 << 8);
        if (!waitForFlag(sr1_, (1 << 0), true, DEFAULT_TIMEOUT)) {
            return Status::TIMEOUT;
        }
        
        // Send address with read bit
        *dr_ = (addr << 1) | 1;
        if (!waitForFlag(sr1_, (1 << 1), true, DEFAULT_TIMEOUT)) {
            return Status::TIMEOUT;
        }
        
        // Clear ADDR
        (void)*sr1_;
        (void)*sr2_;
        
        // Read data
        for (size_t i = 0; i < data.size(); ++i) {
            if (i == data.size() - 1) {
                *cr1_ &= ~(1 << 10); // Disable ACK
                *cr1_ |= (1 << 9);   // Generate stop
            }
            
            if (!waitForFlag(sr1_, (1 << 6), true, DEFAULT_TIMEOUT)) {
                return Status::TIMEOUT;
            }
            
            data[i] = *dr_;
        }
        
        *cr1_ |= (1 << 10); // Re-enable ACK
        
        return Status::SUCCESS;
    }
    
    /**
     * @brief Wait for a status flag
     */
    bool waitForFlag(volatile uint32_t* reg, uint32_t flag, bool set,
                    std::chrono::milliseconds timeout) {
        auto start = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start < timeout) {
            bool flag_state = (*reg & flag) != 0;
            if (flag_state == set) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Update statistics based on test result
     */
    void updateStats(const TestResult& result) {
        stats_.total_duration += result.duration;
        
        switch (result.status) {
            case Status::SUCCESS:
                stats_.tests_passed++;
                break;
            case Status::TIMEOUT:
                stats_.tests_failed++;
                stats_.timeouts++;
                break;
            case Status::DATA_MISMATCH:
                stats_.tests_failed++;
                stats_.data_errors += result.error_positions.size();
                break;
            default:
                stats_.tests_failed++;
                break;
        }
    }
    
    /**
     * @brief Convert status to string
     */
    static std::string statusToString(Status status) {
        switch (status) {
            case Status::SUCCESS: return "SUCCESS";
            case Status::TIMEOUT: return "TIMEOUT";
            case Status::DATA_MISMATCH: return "DATA_MISMATCH";
            case Status::HARDWARE_ERROR: return "HARDWARE_ERROR";
            case Status::INIT_FAILED: return "INIT_FAILED";
            default: return "UNKNOWN";
        }
    }
    
    /**
     * @brief Generate sequential test data
     */
    static std::vector<uint8_t> generateSequential(size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i);
        }
        return data;
    }
    
    /**
     * @brief Generate random test data
     */
    static std::vector<uint8_t> generateRandom(size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(rand() & 0xFF);
        }
        return data;
    }
};

// Example usage
int main() {
    try {
        I2CLoopbackTester tester;
        tester.initialize();
        tester.runTestSuite();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

Here's a Rust implementation with strong type safety and error handling:

```rs
use std::time::{Duration, Instant};
use std::fmt;

// Hardware register addresses
const I2C_BASE: usize = 0x40005400;
const CR1_OFFSET: usize = 0x00;
const CR2_OFFSET: usize = 0x04;
const OAR1_OFFSET: usize = 0x08;
const DR_OFFSET: usize = 0x10;
const SR1_OFFSET: usize = 0x14;
const SR2_OFFSET: usize = 0x18;

// Control register bits
const CR1_PE: u32 = 1 << 0;
const CR1_START: u32 = 1 << 8;
const CR1_STOP: u32 = 1 << 9;
const CR1_ACK: u32 = 1 << 10;
const CR1_LOOPBACK: u32 = 1 << 15;

// Status register bits
const SR1_SB: u32 = 1 << 0;
const SR1_ADDR: u32 = 1 << 1;
const SR1_BTF: u32 = 1 << 2;
const SR1_RXNE: u32 = 1 << 6;
const SR1_TXE: u32 = 1 << 7;

const LOOPBACK_ADDRESS: u8 = 0x50;
const DEFAULT_TIMEOUT: Duration = Duration::from_millis(1000);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LoopbackError {
    Timeout,
    DataMismatch,
    HardwareError,
    InitFailed,
    NotInitialized,
}

impl fmt::Display for LoopbackError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LoopbackError::Timeout => write!(f, "Operation timed out"),
            LoopbackError::DataMismatch => write!(f, "Data verification failed"),
            LoopbackError::HardwareError => write!(f, "Hardware error"),
            LoopbackError::InitFailed => write!(f, "Initialization failed"),
            LoopbackError::NotInitialized => write!(f, "I2C not initialized"),
        }
    }
}

impl std::error::Error for LoopbackError {}

pub type Result<T> = std::result::Result<T, LoopbackError>;

#[derive(Debug, Clone)]
pub struct TestResult {
    pub status: Result<()>,
    pub bytes_tested: usize,
    pub duration: Duration,
    pub error_positions: Vec<usize>,
}

#[derive(Debug, Default, Clone)]
pub struct Statistics {
    pub tests_passed: u32,
    pub tests_failed: u32,
    pub timeouts: u32,
    pub data_errors: u32,
    pub total_duration: Duration,
}

impl Statistics {
    pub fn pass_rate(&self) -> Option<f64> {
        let total = self.tests_passed + self.tests_failed;
        if total > 0 {
            Some(100.0 * self.tests_passed as f64 / total as f64)
        } else {
            None
        }
    }
}

/// Safe wrapper around I2C hardware registers
struct I2CRegisters {
    cr1: *mut u32,
    cr2: *mut u32,
    oar1: *mut u32,
    dr: *mut u32,
    sr1: *mut u32,
    sr2: *mut u32,
}

impl I2CRegisters {
    unsafe fn new() -> Self {
        Self {
            cr1: (I2C_BASE + CR1_OFFSET) as *mut u32,
            cr2: (I2C_BASE + CR2_OFFSET) as *mut u32,
            oar1: (I2C_BASE + OAR1_OFFSET) as *mut u32,
            dr: (I2C_BASE + DR_OFFSET) as *mut u32,
            sr1: (I2C_BASE + SR1_OFFSET) as *mut u32,
            sr2: (I2C_BASE + SR2_OFFSET) as *mut u32,
        }
    }

    unsafe fn read_cr1(&self) -> u32 {
        std::ptr::read_volatile(self.cr1)
    }

    unsafe fn write_cr1(&self, value: u32) {
        std::ptr::write_volatile(self.cr1, value);
    }

    unsafe fn modify_cr1<F>(&self, f: F)
    where
        F: FnOnce(u32) -> u32,
    {
        let value = self.read_cr1();
        self.write_cr1(f(value));
    }

    unsafe fn read_sr1(&self) -> u32 {
        std::ptr::read_volatile(self.sr1)
    }

    unsafe fn read_sr2(&self) -> u32 {
        std::ptr::read_volatile(self.sr2)
    }

    unsafe fn write_dr(&self, value: u8) {
        std::ptr::write_volatile(self.dr, value as u32);
    }

    unsafe fn read_dr(&self) -> u8 {
        std::ptr::read_volatile(self.dr) as u8
    }
}

/// I2C Loopback Tester
pub struct I2CLoopbackTester {
    regs: I2CRegisters,
    initialized: bool,
    stats: Statistics,
}

impl I2CLoopbackTester {
    /// Create a new I2C loopback tester
    pub unsafe fn new() -> Self {
        Self {
            regs: I2CRegisters::new(),
            initialized: false,
            stats: Statistics::default(),
        }
    }

    /// Initialize the I2C controller for loopback testing
    pub fn initialize(&mut self) -> Result<()> {
        unsafe {
            // Disable I2C peripheral
            self.regs.modify_cr1(|v| v & !CR1_PE);

            // Configure timing (example value, adjust for your system)
            std::ptr::write_volatile(self.regs.cr2, 0x0010);

            // Set own address for slave mode
            let own_addr = (LOOPBACK_ADDRESS as u32) << 1 | (1 << 15);
            std::ptr::write_volatile(self.regs.oar1, own_addr);

            // Enable ACK
            self.regs.modify_cr1(|v| v | CR1_ACK);

            // Enable loopback mode if supported
            self.regs.modify_cr1(|v| v | CR1_LOOPBACK);

            // Enable I2C peripheral
            self.regs.modify_cr1(|v| v | CR1_PE);

            self.initialized = true;
            Ok(())
        }
    }

    /// Disable the I2C controller
    pub fn disable(&mut self) {
        unsafe {
            self.regs.modify_cr1(|v| v & !CR1_PE);
        }
        self.initialized = false;
    }

    /// Wait for a specific flag with timeout
    fn wait_for_flag(&self, flag: u32, set: bool, timeout: Duration) -> Result<()> {
        let start = Instant::now();

        while start.elapsed() < timeout {
            unsafe {
                let sr1_value = self.regs.read_sr1();
                let flag_state = (sr1_value & flag) != 0;
                if flag_state == set {
                    return Ok(());
                }
            }
        }

        Err(LoopbackError::Timeout)
    }

    /// Write data to I2C in master mode
    fn write(&self, addr: u8, data: &[u8]) -> Result<()> {
        if !self.initialized {
            return Err(LoopbackError::NotInitialized);
        }

        unsafe {
            // Generate START condition
            self.regs.modify_cr1(|v| v | CR1_START);

            // Wait for start bit
            self.wait_for_flag(SR1_SB, true, DEFAULT_TIMEOUT)?;

            // Send address with write bit
            self.regs.write_dr((addr << 1) | 0);

            // Wait for address acknowledged
            self.wait_for_flag(SR1_ADDR, true, DEFAULT_TIMEOUT)?;

            // Clear ADDR flag by reading SR1 and SR2
            let _ = self.regs.read_sr1();
            let _ = self.regs.read_sr2();

            // Send data bytes
            for &byte in data {
                // Wait for TXE (transmit buffer empty)
                self.wait_for_flag(SR1_TXE, true, DEFAULT_TIMEOUT)?;
                self.regs.write_dr(byte);
            }

            // Wait for byte transfer finished
            self.wait_for_flag(SR1_BTF, true, DEFAULT_TIMEOUT)?;

            // Generate STOP condition
            self.regs.modify_cr1(|v| v | CR1_STOP);
        }

        Ok(())
    }

    /// Read data from I2C in master mode
    fn read(&self, addr: u8, buffer: &mut [u8]) -> Result<()> {
        if !self.initialized {
            return Err(LoopbackError::NotInitialized);
        }

        unsafe {
            // Generate START condition
            self.regs.modify_cr1(|v| v | CR1_START);

            // Wait for start bit
            self.wait_for_flag(SR1_SB, true, DEFAULT_TIMEOUT)?;

            // Send address with read bit
            self.regs.write_dr((addr << 1) | 1);

            // Wait for address acknowledged
            self.wait_for_flag(SR1_ADDR, true, DEFAULT_TIMEOUT)?;

            // Clear ADDR flag
            let _ = self.regs.read_sr1();
            let _ = self.regs.read_sr2();

            // Read data bytes
            for (i, byte) in buffer.iter_mut().enumerate() {
                if i == buffer.len() - 1 {
                    // Last byte: disable ACK
                    self.regs.modify_cr1(|v| v & !CR1_ACK);
                    // Generate STOP
                    self.regs.modify_cr1(|v| v | CR1_STOP);
                }

                // Wait for RXNE (receive buffer not empty)
                self.wait_for_flag(SR1_RXNE, true, DEFAULT_TIMEOUT)?;

                *byte = self.regs.read_dr();
            }

            // Re-enable ACK for next transfer
            self.regs.modify_cr1(|v| v | CR1_ACK);
        }

        Ok(())
    }

    /// Test with a specific data pattern
    pub fn test_pattern(&mut self, data: &[u8]) -> TestResult {
        let start_time = Instant::now();
        let bytes_tested = data.len();
        let mut error_positions = Vec::new();

        // Write data
        if let Err(e) = self.write(LOOPBACK_ADDRESS, data) {
            self.update_stats_error(&e);
            return TestResult {
                status: Err(e),
                bytes_tested,
                duration: start_time.elapsed(),
                error_positions,
            };
        }

        // Small delay between write and read
        std::thread::sleep(Duration::from_millis(10));

        // Read data
        let mut read_buffer = vec![0u8; data.len()];
        if let Err(e) = self.read(LOOPBACK_ADDRESS, &mut read_buffer) {
            self.update_stats_error(&e);
            return TestResult {
                status: Err(e),
                bytes_tested,
                duration: start_time.elapsed(),
                error_positions,
            };
        }

        // Verify data
        for (i, (&expected, &actual)) in data.iter().zip(read_buffer.iter()).enumerate() {
            if expected != actual {
                error_positions.push(i);
            }
        }

        let duration = start_time.elapsed();
        let status = if error_positions.is_empty() {
            self.stats.tests_passed += 1;
            Ok(())
        } else {
            self.stats.tests_failed += 1;
            self.stats.data_errors += error_positions.len() as u32;
            Err(LoopbackError::DataMismatch)
        };

        self.stats.total_duration += duration;

        TestResult {
            status,
            bytes_tested,
            duration,
            error_positions,
        }
    }

    /// Update statistics for errors
    fn update_stats_error(&mut self, error: &LoopbackError) {
        self.stats.tests_failed += 1;
        if *error == LoopbackError::Timeout {
            self.stats.timeouts += 1;
        }
    }

    /// Run a comprehensive test suite
    pub fn run_test_suite(&mut self) {
        println!("\n=== I2C Loopback Test Suite ===\n");

        let test_cases = vec![
            ("All zeros", vec![0x00; 16]),
            ("All ones", vec![0xFF; 16]),
            ("Alternating 0xAA", vec![0xAA; 16]),
            ("Alternating 0x55", vec![0x55; 16]),
            ("Sequential", (0..16).map(|i| i as u8).collect()),
            ("Pattern 0xA5", (0..16).map(|i| 0xA5 ^ i).collect()),
        ];

        for (name, data) in test_cases {
            println!("Test: {} ({} bytes)", name, data.len());

            let result = self.test_pattern(&data);

            match result.status {
                Ok(()) => println!("  Status: SUCCESS"),
                Err(e) => println!("  Status: {}", e),
            }

            println!("  Duration: {:?}", result.duration);

            if !result.error_positions.is_empty() {
                print!("  Errors at positions: ");
                for pos in &result.error_positions {
                    print!("{} ", pos);
                }
                println!();
            }
            println!();
        }

        self.print_statistics();
    }

    /// Get current statistics
    pub fn statistics(&self) -> &Statistics {
        &self.stats
    }

    /// Reset statistics
    pub fn reset_statistics(&mut self) {
        self.stats = Statistics::default();
    }

    /// Print detailed statistics
    pub fn print_statistics(&self) {
        println!("=== Test Statistics ===");
        println!("Tests passed: {}", self.stats.tests_passed);
        println!("Tests failed: {}", self.stats.tests_failed);
        println!("Timeouts: {}", self.stats.timeouts);
        println!("Data errors: {}", self.stats.data_errors);
        println!("Total duration: {:?}", self.stats.total_duration);

        if let Some(pass_rate) = self.stats.pass_rate() {
            println!("Pass rate: {:.2}%", pass_rate);
        }
    }
}

impl Drop for I2CLoopbackTester {
    fn drop(&mut self) {
        if self.initialized {
            self.disable();
        }
    }
}

/// Builder pattern for creating custom test configurations
pub struct LoopbackTestBuilder {
    patterns: Vec<(String, Vec<u8>)>,
    iterations: usize,
    delay_between_tests: Duration,
}

impl LoopbackTestBuilder {
    pub fn new() -> Self {
        Self {
            patterns: Vec::new(),
            iterations: 1,
            delay_between_tests: Duration::from_millis(0),
        }
    }

    pub fn add_pattern(mut self, name: impl Into<String>, data: Vec<u8>) -> Self {
        self.patterns.push((name.into(), data));
        self
    }

    pub fn iterations(mut self, count: usize) -> Self {
        self.iterations = count;
        self
    }

    pub fn delay_between_tests(mut self, delay: Duration) -> Self {
        self.delay_between_tests = delay;
        self
    }

    pub fn run(self, tester: &mut I2CLoopbackTester) {
        println!("\n=== Custom I2C Loopback Test ===");
        println!("Iterations: {}", self.iterations);
        println!("Test patterns: {}\n", self.patterns.len());

        for iteration in 0..self.iterations {
            if self.iterations > 1 {
                println!("--- Iteration {} ---", iteration + 1);
            }

            for (name, data) in &self.patterns {
                println!("Test: {} ({} bytes)", name, data.len());
                let result = tester.test_pattern(data);

                match result.status {
                    Ok(()) => println!("  Status: SUCCESS"),
                    Err(e) => println!("  Status: {}", e),
                }
                println!("  Duration: {:?}", result.duration);

                if !result.error_positions.is_empty() {
                    print!("  Errors at positions: ");
                    for pos in &result.error_positions {
                        print!("{} ", pos);
                    }
                    println!();
                }
                println!();

                if self.delay_between_tests.as_millis() > 0 {
                    std::thread::sleep(self.delay_between_tests);
                }
            }
        }

        tester.print_statistics();
    }
}

impl Default for LoopbackTestBuilder {
    fn default() -> Self {
        Self::new()
    }
}

// Example usage
#[cfg(feature = "example")]
fn main() {
    unsafe {
        let mut tester = I2CLoopbackTester::new();

        match tester.initialize() {
            Ok(()) => {
                println!("I2C initialized for loopback testing");
                
                // Run standard test suite
                tester.run_test_suite();
                
                // Reset statistics for custom tests
                tester.reset_statistics();
                
                // Run custom tests with builder
                LoopbackTestBuilder::new()
                    .add_pattern("Custom pattern 1", vec![0x12, 0x34, 0x56, 0x78])
                    .add_pattern("Custom pattern 2", vec![0xDE, 0xAD, 0xBE, 0xEF])
                    .iterations(3)
                    .delay_between_tests(Duration::from_millis(50))
                    .run(&mut tester);
            }
            Err(e) => {
                eprintln!("Failed to initialize I2C: {}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_statistics_pass_rate() {
        let mut stats = Statistics::default();
        assert_eq!(stats.pass_rate(), None);

        stats.tests_passed = 8;
        stats.tests_failed = 2;
        assert_eq!(stats.pass_rate(), Some(80.0));
    }

    #[test]
    fn test_error_display() {
        assert_eq!(format!("{}", LoopbackError::Timeout), "Operation timed out");
        assert_eq!(format!("{}", LoopbackError::DataMismatch), "Data verification failed");
    }

    #[test]
    fn test_loopback_error_implements_error_trait() {
        let error: Box<dyn std::error::Error> = Box::new(LoopbackError::Timeout);
        assert!(error.to_string().contains("timed out"));
    }

    #[test]
    fn test_test_builder() {
        let builder = LoopbackTestBuilder::new()
            .add_pattern("test", vec![0x00])
            .iterations(5)
            .delay_between_tests(Duration::from_millis(10));

        assert_eq!(builder.patterns.len(), 1);
        assert_eq!(builder.iterations, 5);
    }
}
```

The Rust implementation provides a safe, modern approach to I2C loopback testing with:

## Key Concepts Explained

### 1. **Loopback Configuration**
Loopback testing requires either:
- **Physical loopback**: External connection between SDA/SCL pins
- **Internal loopback**: Hardware feature that routes signals internally (using the `CR1_LOOPBACK` bit in examples)
- **Dual-controller loopback**: One controller acts as master, another as slave

### 2. **Test Patterns**
Different patterns help identify various failure modes:
- **0x00/0xFF**: Tests for stuck-at faults
- **0xAA/0x55**: Tests alternating bit patterns
- **Sequential**: Tests data path integrity
- **Random**: Tests general reliability

### 3. **Verification Strategy**
- Write known data pattern
- Read back the same amount of data
- Compare byte-by-byte
- Record any mismatches with positions

### 4. **Timeout Handling**
All operations must include timeout protection to prevent infinite loops when hardware fails to respond.

### 5. **Statistics Tracking**
Comprehensive statistics help identify:
- Reliability (pass/fail rates)
- Common failure modes (timeouts vs. data errors)
- Performance characteristics (timing)

## Best Practices

1. **Always include timeout protection** - Hardware can fail in unexpected ways
2. **Test multiple patterns** - Different patterns expose different failure modes
3. **Track detailed statistics** - Helps with debugging and reliability analysis
4. **Use RAII in C++/Rust** - Ensures proper cleanup even on errors
5. **Test at multiple speeds** - If configurable, test different I2C clock rates
6. **Verify address handling** - Test both 7-bit and 10-bit addressing if supported

## Common Pitfalls

- Forgetting to clear status flags (ADDR flag must be cleared by reading SR1 and SR2)
- Not waiting for byte transfer completion before generating STOP
- Insufficient delays between write and read operations
- Missing ACK disable for the last byte in read operations
- Not re-enabling ACK after read operations

These implementations provide a solid foundation for I2C loopback testing across different programming languages and can be adapted to specific hardware platforms.