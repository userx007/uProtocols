# Unit Testing SPI Drivers

## Overview

Unit testing SPI drivers is crucial for ensuring reliable communication with hardware peripherals. Since direct hardware testing can be slow, expensive, and difficult to automate, developers use mock objects and test frameworks to validate SPI driver logic independently. This approach allows you to test error handling, edge cases, and protocol compliance without physical hardware.

## Key Concepts

### Why Unit Test SPI Drivers?

1. **Early Bug Detection**: Catch logic errors before hardware integration
2. **Regression Prevention**: Ensure changes don't break existing functionality
3. **Hardware Independence**: Test without requiring physical SPI devices
4. **Continuous Integration**: Automate testing in CI/CD pipelines
5. **Protocol Validation**: Verify correct command sequences and timing

### Testing Strategies

- **Mocking**: Replace hardware HAL with controllable test doubles
- **Dependency Injection**: Make SPI HAL swappable for testing
- **State Verification**: Check that correct sequences are sent
- **Behavior Testing**: Verify driver responds correctly to device responses

## C Implementation

Here's a comprehensive example using a simple test framework approach:

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// SPI HAL Interface (would normally be hardware-specific)
// ============================================================================

typedef struct {
    void (*transfer)(uint8_t *tx_data, uint8_t *rx_data, size_t len);
    void (*chip_select)(bool active);
} spi_hal_t;

// ============================================================================
// Mock SPI HAL for Testing
// ============================================================================

#define MAX_TRANSACTIONS 10
#define MAX_TRANSFER_SIZE 256

typedef struct {
    uint8_t tx_data[MAX_TRANSFER_SIZE];
    uint8_t rx_data[MAX_TRANSFER_SIZE];
    size_t length;
} mock_transaction_t;

typedef struct {
    mock_transaction_t transactions[MAX_TRANSACTIONS];
    size_t transaction_count;
    size_t current_transaction;
    bool cs_active;
    size_t cs_toggle_count;
} mock_spi_state_t;

static mock_spi_state_t g_mock_state = {0};

// Mock transfer function
static void mock_spi_transfer(uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    assert(g_mock_state.current_transaction < MAX_TRANSACTIONS);
    
    mock_transaction_t *txn = &g_mock_state.transactions[g_mock_state.current_transaction];
    
    // Record transmitted data
    memcpy(txn->tx_data, tx_data, len);
    txn->length = len;
    
    // Provide mock response if configured
    if (rx_data && txn->rx_data[0] != 0) {
        memcpy(rx_data, txn->rx_data, len);
    }
    
    g_mock_state.current_transaction++;
}

// Mock chip select function
static void mock_chip_select(bool active) {
    g_mock_state.cs_active = active;
    g_mock_state.cs_toggle_count++;
}

// Setup mock responses
static void mock_set_response(size_t transaction_idx, const uint8_t *data, size_t len) {
    assert(transaction_idx < MAX_TRANSACTIONS);
    memcpy(g_mock_state.transactions[transaction_idx].rx_data, data, len);
}

// Reset mock state
static void mock_reset(void) {
    memset(&g_mock_state, 0, sizeof(g_mock_state));
}

// Create mock HAL
static spi_hal_t mock_hal = {
    .transfer = mock_spi_transfer,
    .chip_select = mock_chip_select
};

// ============================================================================
// Example SPI EEPROM Driver (System Under Test)
// ============================================================================

#define EEPROM_CMD_WRITE 0x02
#define EEPROM_CMD_READ  0x03
#define EEPROM_CMD_WREN  0x06

typedef struct {
    spi_hal_t *hal;
} spi_eeprom_t;

// Initialize EEPROM driver
void eeprom_init(spi_eeprom_t *eeprom, spi_hal_t *hal) {
    eeprom->hal = hal;
}

// Write data to EEPROM
bool eeprom_write(spi_eeprom_t *eeprom, uint16_t address, const uint8_t *data, size_t len) {
    if (!eeprom || !data || len == 0) return false;
    
    // Enable write
    eeprom->hal->chip_select(true);
    uint8_t cmd = EEPROM_CMD_WREN;
    eeprom->hal->transfer(&cmd, NULL, 1);
    eeprom->hal->chip_select(false);
    
    // Write data
    eeprom->hal->chip_select(true);
    uint8_t write_buf[3 + len];
    write_buf[0] = EEPROM_CMD_WRITE;
    write_buf[1] = (address >> 8) & 0xFF;
    write_buf[2] = address & 0xFF;
    memcpy(&write_buf[3], data, len);
    eeprom->hal->transfer(write_buf, NULL, 3 + len);
    eeprom->hal->chip_select(false);
    
    return true;
}

// Read data from EEPROM
bool eeprom_read(spi_eeprom_t *eeprom, uint16_t address, uint8_t *data, size_t len) {
    if (!eeprom || !data || len == 0) return false;
    
    eeprom->hal->chip_select(true);
    
    uint8_t cmd_buf[3] = {
        EEPROM_CMD_READ,
        (address >> 8) & 0xFF,
        address & 0xFF
    };
    
    eeprom->hal->transfer(cmd_buf, NULL, 3);
    eeprom->hal->transfer(NULL, data, len);
    
    eeprom->hal->chip_select(false);
    
    return true;
}

// ============================================================================
// Test Framework
// ============================================================================

typedef struct {
    int passed;
    int failed;
} test_stats_t;

static test_stats_t g_stats = {0};

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running: %s... ", #name); \
    mock_reset(); \
    test_##name(); \
    printf("PASSED\n"); \
    g_stats.passed++; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED\n  Expected: %d, Got: %d\n", (int)(b), (int)(a)); \
        g_stats.failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) ASSERT_EQ(cond, true)
#define ASSERT_MEM_EQ(a, b, len) do { \
    if (memcmp(a, b, len) != 0) { \
        printf("FAILED\n  Memory mismatch\n"); \
        g_stats.failed++; \
        return; \
    } \
} while(0)

// ============================================================================
// Unit Tests
// ============================================================================

TEST(eeprom_write_sends_correct_sequence) {
    spi_eeprom_t eeprom;
    eeprom_init(&eeprom, &mock_hal);
    
    uint8_t test_data[] = {0xAA, 0xBB, 0xCC};
    eeprom_write(&eeprom, 0x1234, test_data, sizeof(test_data));
    
    // Verify WREN command sent first
    ASSERT_EQ(g_mock_state.transactions[0].tx_data[0], EEPROM_CMD_WREN);
    ASSERT_EQ(g_mock_state.transactions[0].length, 1);
    
    // Verify WRITE command with address and data
    ASSERT_EQ(g_mock_state.transactions[1].tx_data[0], EEPROM_CMD_WRITE);
    ASSERT_EQ(g_mock_state.transactions[1].tx_data[1], 0x12); // Address high
    ASSERT_EQ(g_mock_state.transactions[1].tx_data[2], 0x34); // Address low
    ASSERT_MEM_EQ(&g_mock_state.transactions[1].tx_data[3], test_data, 3);
}

TEST(eeprom_read_sends_correct_command) {
    spi_eeprom_t eeprom;
    eeprom_init(&eeprom, &mock_hal);
    
    uint8_t read_buf[4];
    uint8_t expected[] = {0x11, 0x22, 0x33, 0x44};
    
    // Configure mock to return test data
    mock_set_response(1, expected, sizeof(expected));
    
    eeprom_read(&eeprom, 0x5678, read_buf, sizeof(read_buf));
    
    // Verify READ command sent with correct address
    ASSERT_EQ(g_mock_state.transactions[0].tx_data[0], EEPROM_CMD_READ);
    ASSERT_EQ(g_mock_state.transactions[0].tx_data[1], 0x56);
    ASSERT_EQ(g_mock_state.transactions[0].tx_data[2], 0x78);
    
    // Verify data received correctly
    ASSERT_MEM_EQ(read_buf, expected, sizeof(expected));
}

TEST(chip_select_toggles_correctly) {
    spi_eeprom_t eeprom;
    eeprom_init(&eeprom, &mock_hal);
    
    uint8_t data = 0x42;
    eeprom_write(&eeprom, 0x0000, &data, 1);
    
    // Should toggle: CS low (WREN), CS high, CS low (WRITE), CS high
    ASSERT_EQ(g_mock_state.cs_toggle_count, 4);
}

TEST(null_pointer_handling) {
    spi_eeprom_t eeprom;
    eeprom_init(&eeprom, &mock_hal);
    
    uint8_t data = 0x00;
    
    // Should return false for null data pointer
    ASSERT_EQ(eeprom_write(&eeprom, 0x0000, NULL, 1), false);
    ASSERT_EQ(eeprom_read(&eeprom, 0x0000, NULL, 1), false);
    
    // Should return false for zero length
    ASSERT_EQ(eeprom_write(&eeprom, 0x0000, &data, 0), false);
}

// ============================================================================
// Test Runner
// ============================================================================

int main(void) {
    printf("=== SPI Driver Unit Tests ===\n\n");
    
    RUN_TEST(eeprom_write_sends_correct_sequence);
    RUN_TEST(eeprom_read_sends_correct_command);
    RUN_TEST(chip_select_toggles_correctly);
    RUN_TEST(null_pointer_handling);
    
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", g_stats.passed);
    printf("Failed: %d\n", g_stats.failed);
    
    return g_stats.failed == 0 ? 0 : 1;
}
```

## C++ Implementation with Google Test

Here's a modern C++ approach using dependency injection and Google Test:

```cpp
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>

// Mock framework simulation (in real code, use Google Mock)
#define MOCK_METHOD2(name, signature) virtual signature { return {}; }
#define MOCK_METHOD1(name, signature) virtual signature {}

// ============================================================================
// SPI HAL Interface
// ============================================================================

class ISpiHal {
public:
    virtual ~ISpiHal() = default;
    
    virtual std::vector<uint8_t> transfer(const std::vector<uint8_t>& tx_data) = 0;
    virtual void setChipSelect(bool active) = 0;
};

// ============================================================================
// Mock SPI HAL
// ============================================================================

class MockSpiHal : public ISpiHal {
public:
    struct Transaction {
        std::vector<uint8_t> tx_data;
        std::vector<uint8_t> rx_data;
    };
    
    std::vector<Transaction> transactions;
    size_t current_response_idx = 0;
    int cs_toggle_count = 0;
    
    // Configure expected responses
    void expectResponse(const std::vector<uint8_t>& response) {
        Transaction txn;
        txn.rx_data = response;
        transactions.push_back(txn);
    }
    
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& tx_data) override {
        if (current_response_idx < transactions.size()) {
            transactions[current_response_idx].tx_data = tx_data;
            auto response = transactions[current_response_idx].rx_data;
            current_response_idx++;
            return response;
        }
        
        // Record transaction even without configured response
        Transaction txn;
        txn.tx_data = tx_data;
        txn.rx_data.resize(tx_data.size(), 0);
        transactions.push_back(txn);
        
        return txn.rx_data;
    }
    
    void setChipSelect(bool active) override {
        cs_toggle_count++;
    }
    
    // Verification helpers
    const Transaction& getTransaction(size_t idx) const {
        return transactions.at(idx);
    }
    
    size_t getTransactionCount() const {
        return transactions.size();
    }
};

// ============================================================================
// SPI EEPROM Driver
// ============================================================================

class SpiEeprom {
public:
    static constexpr uint8_t CMD_WRITE = 0x02;
    static constexpr uint8_t CMD_READ = 0x03;
    static constexpr uint8_t CMD_WREN = 0x06;
    
    explicit SpiEeprom(std::shared_ptr<ISpiHal> hal) : hal_(hal) {}
    
    bool write(uint16_t address, const std::vector<uint8_t>& data) {
        if (data.empty()) return false;
        
        // Enable write
        hal_->setChipSelect(true);
        hal_->transfer({CMD_WREN});
        hal_->setChipSelect(false);
        
        // Write data
        hal_->setChipSelect(true);
        std::vector<uint8_t> write_cmd = {
            CMD_WRITE,
            static_cast<uint8_t>((address >> 8) & 0xFF),
            static_cast<uint8_t>(address & 0xFF)
        };
        write_cmd.insert(write_cmd.end(), data.begin(), data.end());
        hal_->transfer(write_cmd);
        hal_->setChipSelect(false);
        
        return true;
    }
    
    std::vector<uint8_t> read(uint16_t address, size_t length) {
        if (length == 0) return {};
        
        hal_->setChipSelect(true);
        
        // Send read command with address
        hal_->transfer({
            CMD_READ,
            static_cast<uint8_t>((address >> 8) & 0xFF),
            static_cast<uint8_t>(address & 0xFF)
        });
        
        // Read data
        std::vector<uint8_t> dummy(length, 0);
        auto data = hal_->transfer(dummy);
        
        hal_->setChipSelect(false);
        
        return data;
    }
    
private:
    std::shared_ptr<ISpiHal> hal_;
};

// ============================================================================
// Unit Tests (Google Test style)
// ============================================================================

#include <iostream>
#include <cassert>

// Simplified test macros (use actual Google Test in production)
#define TEST_F(suite, name) void suite##_##name()
#define EXPECT_EQ(a, b) assert((a) == (b))
#define EXPECT_TRUE(cond) assert(cond)
#define ASSERT_THAT(val, matcher) assert(matcher(val))

class SpiEepromTest {
protected:
    void SetUp() {
        mock_hal = std::make_shared<MockSpiHal>();
        eeprom = std::make_unique<SpiEeprom>(mock_hal);
    }
    
    std::shared_ptr<MockSpiHal> mock_hal;
    std::unique_ptr<SpiEeprom> eeprom;
};

TEST_F(SpiEepromTest, WriteCommandSequence) {
    SpiEepromTest test;
    test.SetUp();
    
    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC};
    test.eeprom->write(0x1234, data);
    
    // Verify WREN command
    auto txn0 = test.mock_hal->getTransaction(0);
    EXPECT_EQ(txn0.tx_data[0], SpiEeprom::CMD_WREN);
    
    // Verify WRITE command with address and data
    auto txn1 = test.mock_hal->getTransaction(1);
    EXPECT_EQ(txn1.tx_data[0], SpiEeprom::CMD_WRITE);
    EXPECT_EQ(txn1.tx_data[1], 0x12);
    EXPECT_EQ(txn1.tx_data[2], 0x34);
    EXPECT_EQ(txn1.tx_data[3], 0xAA);
    EXPECT_EQ(txn1.tx_data[4], 0xBB);
    EXPECT_EQ(txn1.tx_data[5], 0xCC);
    
    std::cout << "WriteCommandSequence: PASSED\n";
}

TEST_F(SpiEepromTest, ReadReturnsCorrectData) {
    SpiEepromTest test;
    test.SetUp();
    
    // Configure mock response
    test.mock_hal->expectResponse({});  // Command response
    test.mock_hal->expectResponse({0x11, 0x22, 0x33, 0x44});  // Data response
    
    auto data = test.eeprom->read(0x5678, 4);
    
    // Verify command sent
    auto txn0 = test.mock_hal->getTransaction(0);
    EXPECT_EQ(txn0.tx_data[0], SpiEeprom::CMD_READ);
    EXPECT_EQ(txn0.tx_data[1], 0x56);
    EXPECT_EQ(txn0.tx_data[2], 0x78);
    
    // Verify data received
    EXPECT_EQ(data.size(), 4u);
    EXPECT_EQ(data[0], 0x11);
    EXPECT_EQ(data[1], 0x22);
    EXPECT_EQ(data[2], 0x33);
    EXPECT_EQ(data[3], 0x44);
    
    std::cout << "ReadReturnsCorrectData: PASSED\n";
}

TEST_F(SpiEepromTest, ChipSelectToggles) {
    SpiEepromTest test;
    test.SetUp();
    
    test.eeprom->write(0x0000, {0x42});
    
    // CS should toggle 4 times: low/high for WREN, low/high for WRITE
    EXPECT_EQ(test.mock_hal->cs_toggle_count, 4);
    
    std::cout << "ChipSelectToggles: PASSED\n";
}

// ============================================================================
// Test Runner
// ============================================================================

int main() {
    std::cout << "=== C++ SPI Driver Unit Tests ===\n\n";
    
    SpiEepromTest_WriteCommandSequence();
    SpiEepromTest_ReadReturnsCorrectData();
    SpiEepromTest_ChipSelectToggles();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
```

## Rust Implementation

Rust's trait system makes dependency injection natural:

```rust
use std::collections::VecDeque;

// ============================================================================
// SPI HAL Trait
// ============================================================================

trait SpiHal {
    fn transfer(&mut self, tx_data: &[u8]) -> Vec<u8>;
    fn set_chip_select(&mut self, active: bool);
}

// ============================================================================
// Mock SPI HAL
// ============================================================================

#[derive(Debug, Clone)]
struct Transaction {
    tx_data: Vec<u8>,
    rx_data: Vec<u8>,
}

struct MockSpiHal {
    transactions: Vec<Transaction>,
    response_queue: VecDeque<Vec<u8>>,
    cs_toggle_count: usize,
}

impl MockSpiHal {
    fn new() -> Self {
        Self {
            transactions: Vec::new(),
            response_queue: VecDeque::new(),
            cs_toggle_count: 0,
        }
    }
    
    fn expect_response(&mut self, response: Vec<u8>) {
        self.response_queue.push_back(response);
    }
    
    fn get_transaction(&self, idx: usize) -> Option<&Transaction> {
        self.transactions.get(idx)
    }
    
    fn transaction_count(&self) -> usize {
        self.transactions.len()
    }
}

impl SpiHal for MockSpiHal {
    fn transfer(&mut self, tx_data: &[u8]) -> Vec<u8> {
        let rx_data = self.response_queue
            .pop_front()
            .unwrap_or_else(|| vec![0; tx_data.len()]);
        
        self.transactions.push(Transaction {
            tx_data: tx_data.to_vec(),
            rx_data: rx_data.clone(),
        });
        
        rx_data
    }
    
    fn set_chip_select(&mut self, _active: bool) {
        self.cs_toggle_count += 1;
    }
}

// ============================================================================
// SPI EEPROM Driver
// ============================================================================

const CMD_WRITE: u8 = 0x02;
const CMD_READ: u8 = 0x03;
const CMD_WREN: u8 = 0x06;

struct SpiEeprom<H: SpiHal> {
    hal: H,
}

impl<H: SpiHal> SpiEeprom<H> {
    fn new(hal: H) -> Self {
        Self { hal }
    }
    
    fn write(&mut self, address: u16, data: &[u8]) -> Result<(), &'static str> {
        if data.is_empty() {
            return Err("Data cannot be empty");
        }
        
        // Enable write
        self.hal.set_chip_select(true);
        self.hal.transfer(&[CMD_WREN]);
        self.hal.set_chip_select(false);
        
        // Write data
        self.hal.set_chip_select(true);
        let mut write_cmd = vec![
            CMD_WRITE,
            (address >> 8) as u8,
            address as u8,
        ];
        write_cmd.extend_from_slice(data);
        self.hal.transfer(&write_cmd);
        self.hal.set_chip_select(false);
        
        Ok(())
    }
    
    fn read(&mut self, address: u16, length: usize) -> Result<Vec<u8>, &'static str> {
        if length == 0 {
            return Err("Length cannot be zero");
        }
        
        self.hal.set_chip_select(true);
        
        // Send read command
        self.hal.transfer(&[
            CMD_READ,
            (address >> 8) as u8,
            address as u8,
        ]);
        
        // Read data
        let dummy = vec![0; length];
        let data = self.hal.transfer(&dummy);
        
        self.hal.set_chip_select(false);
        
        Ok(data)
    }
}

// ============================================================================
// Unit Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_write_command_sequence() {
        let mut mock = MockSpiHal::new();
        let mut eeprom = SpiEeprom::new(&mut mock);
        
        let data = vec![0xAA, 0xBB, 0xCC];
        eeprom.write(0x1234, &data).unwrap();
        
        // Verify WREN command
        let txn0 = mock.get_transaction(0).unwrap();
        assert_eq!(txn0.tx_data[0], CMD_WREN);
        
        // Verify WRITE command
        let txn1 = mock.get_transaction(1).unwrap();
        assert_eq!(txn1.tx_data[0], CMD_WRITE);
        assert_eq!(txn1.tx_data[1], 0x12);
        assert_eq!(txn1.tx_data[2], 0x34);
        assert_eq!(&txn1.tx_data[3..], &data[..]);
    }
    
    #[test]
    fn test_read_returns_correct_data() {
        let mock = MockSpiHal::new();
        let mut eeprom = SpiEeprom::new(mock);
        
        // Configure mock responses
        eeprom.hal.expect_response(vec![]);  // Command response
        eeprom.hal.expect_response(vec![0x11, 0x22, 0x33, 0x44]);  // Data
        
        let data = eeprom.read(0x5678, 4).unwrap();
        
        // Verify command
        let txn0 = eeprom.hal.get_transaction(0).unwrap();
        assert_eq!(txn0.tx_data[0], CMD_READ);
        assert_eq!(txn0.tx_data[1], 0x56);
        assert_eq!(txn0.tx_data[2], 0x78);
        
        // Verify data
        assert_eq!(data, vec![0x11, 0x22, 0x33, 0x44]);
    }
    
    #[test]
    fn test_chip_select_toggles() {
        let mock = MockSpiHal::new();
        let mut eeprom = SpiEeprom::new(mock);
        
        eeprom.write(0x0000, &[0x42]).unwrap();
        
        assert_eq!(eeprom.hal.cs_toggle_count, 4);
    }
    
    #[test]
    fn test_empty_data_returns_error() {
        let mock = MockSpiHal::new();
        let mut eeprom = SpiEeprom::new(mock);
        
        assert!(eeprom.write(0x0000, &[]).is_err());
        assert!(eeprom.read(0x0000, 0).is_err());
    }
}

fn main() {
    println!("Run tests with: cargo test");
}
```

## Summary

Unit testing SPI drivers requires abstracting hardware dependencies through interfaces or traits, allowing you to inject mock implementations during testing. Key practices include:

**Testing Approach:**
- Use dependency injection to make HAL layers swappable
- Create mock objects that record transactions and provide controlled responses
- Verify command sequences, data payloads, and timing signals
- Test error conditions and edge cases without hardware

**Best Practices:**
- Test one behavior per test case for clarity
- Use descriptive test names that explain what's being verified
- Mock at the HAL boundary, not internal driver logic
- Verify both state (what was sent) and behavior (correct responses)
- Automate tests in CI/CD pipelines

**Language-Specific Strengths:**
- **C**: Requires manual mock creation but offers full control; use function pointers for dependency injection
- **C++**: Leverage classes and virtual functions; integrate with Google Test/Google Mock for powerful assertions
- **Rust**: Traits make mocking natural; built-in test framework with `cargo test` simplifies automation

Well-tested SPI drivers catch protocol errors early, enable safe refactoring, and provide executable documentation of expected behavior. This investment in testing quality pays dividends during hardware integration and long-term maintenance.