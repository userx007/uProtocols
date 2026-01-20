# SPI Integration Testing

## Overview

Integration testing for SPI (Serial Peripheral Interface) communication validates the complete interaction between SPI peripherals, drivers, and the overall system. Unlike unit testing that focuses on individual components, integration testing ensures that all parts work together correctly in real-world scenarios, including timing, data integrity, error handling, and multi-device coordination.

## Key Aspects of SPI Integration Testing

### 1. **End-to-End Communication Validation**
- Verifying complete data transactions between master and slave devices
- Testing multi-byte transfers and message sequencing
- Validating bidirectional data exchange

### 2. **Timing and Synchronization**
- Clock frequency compatibility across devices
- Setup and hold time requirements
- CS (Chip Select) timing and device switching

### 3. **Error Handling and Recovery**
- Handling transmission errors and retries
- Detecting and recovering from bus contention
- Timeout management

### 4. **Multi-Device Scenarios**
- Testing with multiple slaves on the same bus
- CS multiplexing and device selection
- Bus arbitration and priority handling

### 5. **Real Hardware Interaction**
- Testing with actual SPI peripherals (sensors, memory, displays)
- Environmental conditions (temperature, noise)
- Power management and sleep modes

---

## C/C++ Integration Testing Examples

### Example 1: Basic SPI Transaction Test

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Mock SPI hardware interface
typedef struct {
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    size_t buffer_size;
    bool transaction_complete;
} spi_handle_t;

// Simulated SPI peripheral registers
typedef struct {
    uint8_t control_reg;
    uint8_t status_reg;
    uint8_t data_reg;
} spi_peripheral_t;

// Test fixture
typedef struct {
    spi_handle_t spi;
    spi_peripheral_t peripheral;
    uint8_t tx_data[256];
    uint8_t rx_data[256];
    int test_passed;
    int test_failed;
} test_context_t;

// Initialize test context
void init_test_context(test_context_t *ctx) {
    memset(ctx, 0, sizeof(test_context_t));
    ctx->spi.tx_buffer = ctx->tx_data;
    ctx->spi.rx_buffer = ctx->rx_data;
}

// Simulated SPI transfer function
bool spi_transfer(spi_handle_t *spi, size_t length) {
    if (!spi || length > 256) return false;
    
    // Simulate data transfer (loopback for testing)
    for (size_t i = 0; i < length; i++) {
        spi->rx_buffer[i] = spi->tx_buffer[i] ^ 0xFF; // Simple transformation
    }
    
    spi->transaction_complete = true;
    return true;
}

// Integration test: Single transaction
bool test_single_transaction(test_context_t *ctx) {
    printf("Running: Single Transaction Test\n");
    
    // Prepare test data
    for (int i = 0; i < 16; i++) {
        ctx->tx_data[i] = i;
    }
    
    // Execute transfer
    ctx->spi.buffer_size = 16;
    bool result = spi_transfer(&ctx->spi, 16);
    
    // Validate results
    if (!result || !ctx->spi.transaction_complete) {
        printf("  FAILED: Transaction did not complete\n");
        ctx->test_failed++;
        return false;
    }
    
    // Check received data
    for (int i = 0; i < 16; i++) {
        uint8_t expected = i ^ 0xFF;
        if (ctx->rx_data[i] != expected) {
            printf("  FAILED: Data mismatch at index %d (expected 0x%02X, got 0x%02X)\n",
                   i, expected, ctx->rx_data[i]);
            ctx->test_failed++;
            return false;
        }
    }
    
    printf("  PASSED\n");
    ctx->test_passed++;
    return true;
}

// Integration test: Multiple sequential transactions
bool test_sequential_transactions(test_context_t *ctx) {
    printf("Running: Sequential Transactions Test\n");
    
    const int num_transactions = 5;
    const int transaction_size = 8;
    
    for (int txn = 0; txn < num_transactions; txn++) {
        // Prepare unique data for each transaction
        for (int i = 0; i < transaction_size; i++) {
            ctx->tx_data[i] = (txn * transaction_size) + i;
        }
        
        // Execute transfer
        if (!spi_transfer(&ctx->spi, transaction_size)) {
            printf("  FAILED: Transaction %d failed\n", txn);
            ctx->test_failed++;
            return false;
        }
        
        // Validate
        for (int i = 0; i < transaction_size; i++) {
            uint8_t expected = ((txn * transaction_size) + i) ^ 0xFF;
            if (ctx->rx_data[i] != expected) {
                printf("  FAILED: Transaction %d, byte %d mismatch\n", txn, i);
                ctx->test_failed++;
                return false;
            }
        }
    }
    
    printf("  PASSED\n");
    ctx->test_passed++;
    return true;
}

int main(void) {
    test_context_t ctx;
    init_test_context(&ctx);
    
    printf("=== SPI Integration Testing ===\n\n");
    
    test_single_transaction(&ctx);
    test_sequential_transactions(&ctx);
    
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", ctx.test_passed);
    printf("Failed: %d\n", ctx.test_failed);
    
    return ctx.test_failed == 0 ? 0 : 1;
}
```

### Example 2: Multi-Device Integration Test

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>

// SPI Device abstraction
class SPIDevice {
protected:
    uint8_t cs_pin;
    std::string device_name;
    
public:
    SPIDevice(uint8_t cs, const std::string& name) 
        : cs_pin(cs), device_name(name) {}
    
    virtual ~SPIDevice() = default;
    
    virtual bool initialize() = 0;
    virtual bool transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx) = 0;
    
    const std::string& name() const { return device_name; }
    uint8_t chip_select() const { return cs_pin; }
};

// Simulated Flash Memory Device
class FlashMemory : public SPIDevice {
private:
    std::vector<uint8_t> memory;
    static constexpr uint8_t CMD_READ = 0x03;
    static constexpr uint8_t CMD_WRITE = 0x02;
    
public:
    FlashMemory(uint8_t cs) 
        : SPIDevice(cs, "Flash Memory"), memory(1024, 0) {}
    
    bool initialize() override {
        std::fill(memory.begin(), memory.end(), 0xFF);
        return true;
    }
    
    bool transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx) override {
        if (tx.empty()) return false;
        
        rx.resize(tx.size());
        
        uint8_t cmd = tx[0];
        if (cmd == CMD_READ && tx.size() >= 4) {
            uint16_t addr = (tx[1] << 8) | tx[2];
            for (size_t i = 3; i < tx.size(); i++) {
                rx[i] = (addr + i - 3 < memory.size()) ? memory[addr + i - 3] : 0xFF;
            }
            return true;
        } else if (cmd == CMD_WRITE && tx.size() >= 4) {
            uint16_t addr = (tx[1] << 8) | tx[2];
            for (size_t i = 3; i < tx.size(); i++) {
                if (addr + i - 3 < memory.size()) {
                    memory[addr + i - 3] = tx[i];
                }
            }
            return true;
        }
        
        return false;
    }
};

// Simulated Temperature Sensor
class TemperatureSensor : public SPIDevice {
private:
    float temperature;
    
public:
    TemperatureSensor(uint8_t cs) 
        : SPIDevice(cs, "Temperature Sensor"), temperature(25.0f) {}
    
    bool initialize() override {
        temperature = 25.0f;
        return true;
    }
    
    bool transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx) override {
        rx.resize(tx.size());
        
        if (tx.size() >= 2) {
            // Simulate temperature reading (16-bit value)
            int16_t temp_raw = static_cast<int16_t>(temperature * 100);
            rx[0] = (temp_raw >> 8) & 0xFF;
            rx[1] = temp_raw & 0xFF;
            return true;
        }
        
        return false;
    }
};

// SPI Bus Manager for multi-device testing
class SPIBusManager {
private:
    std::vector<std::shared_ptr<SPIDevice>> devices;
    
public:
    void add_device(std::shared_ptr<SPIDevice> device) {
        devices.push_back(device);
    }
    
    bool initialize_all() {
        for (auto& dev : devices) {
            if (!dev->initialize()) {
                std::cerr << "Failed to initialize " << dev->name() << std::endl;
                return false;
            }
        }
        return true;
    }
    
    bool communicate_with_device(size_t device_index, 
                                  const std::vector<uint8_t>& tx, 
                                  std::vector<uint8_t>& rx) {
        if (device_index >= devices.size()) return false;
        
        // Simulate CS activation delay
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        
        bool result = devices[device_index]->transfer(tx, rx);
        
        // Simulate CS deactivation delay
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        
        return result;
    }
    
    size_t device_count() const { return devices.size(); }
};

// Integration test suite
class SPIIntegrationTestSuite {
private:
    SPIBusManager bus;
    int tests_passed;
    int tests_failed;
    
public:
    SPIIntegrationTestSuite() : tests_passed(0), tests_failed(0) {}
    
    void setup() {
        auto flash = std::make_shared<FlashMemory>(0);
        auto temp_sensor = std::make_shared<TemperatureSensor>(1);
        
        bus.add_device(flash);
        bus.add_device(temp_sensor);
        
        if (!bus.initialize_all()) {
            std::cerr << "Setup failed!" << std::endl;
        }
    }
    
    void test_flash_write_read() {
        std::cout << "Running: Flash Write/Read Test" << std::endl;
        
        // Write data
        std::vector<uint8_t> write_cmd = {0x02, 0x00, 0x10, 0xAA, 0xBB, 0xCC, 0xDD};
        std::vector<uint8_t> write_rx;
        
        if (!bus.communicate_with_device(0, write_cmd, write_rx)) {
            std::cout << "  FAILED: Write command failed" << std::endl;
            tests_failed++;
            return;
        }
        
        // Read data back
        std::vector<uint8_t> read_cmd = {0x03, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
        std::vector<uint8_t> read_rx;
        
        if (!bus.communicate_with_device(0, read_cmd, read_rx)) {
            std::cout << "  FAILED: Read command failed" << std::endl;
            tests_failed++;
            return;
        }
        
        // Verify
        std::vector<uint8_t> expected = {0xAA, 0xBB, 0xCC, 0xDD};
        for (size_t i = 0; i < expected.size(); i++) {
            if (read_rx[3 + i] != expected[i]) {
                std::cout << "  FAILED: Data mismatch at offset " << i << std::endl;
                tests_failed++;
                return;
            }
        }
        
        std::cout << "  PASSED" << std::endl;
        tests_passed++;
    }
    
    void test_temperature_sensor() {
        std::cout << "Running: Temperature Sensor Test" << std::endl;
        
        std::vector<uint8_t> cmd = {0x00, 0x00};
        std::vector<uint8_t> rx;
        
        if (!bus.communicate_with_device(1, cmd, rx)) {
            std::cout << "  FAILED: Sensor communication failed" << std::endl;
            tests_failed++;
            return;
        }
        
        if (rx.size() < 2) {
            std::cout << "  FAILED: Insufficient data received" << std::endl;
            tests_failed++;
            return;
        }
        
        int16_t temp_raw = (rx[0] << 8) | rx[1];
        float temperature = temp_raw / 100.0f;
        
        std::cout << "  Temperature: " << temperature << "°C" << std::endl;
        std::cout << "  PASSED" << std::endl;
        tests_passed++;
    }
    
    void run_all_tests() {
        std::cout << "=== SPI Multi-Device Integration Tests ===" << std::endl << std::endl;
        
        setup();
        test_flash_write_read();
        test_temperature_sensor();
        
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Passed: " << tests_passed << std::endl;
        std::cout << "Failed: " << tests_failed << std::endl;
    }
};

int main() {
    SPIIntegrationTestSuite test_suite;
    test_suite.run_all_tests();
    return 0;
}
```

---

## Rust Integration Testing Examples

### Example 1: SPI Integration Test with Error Handling

```rust
use std::fmt;

// Error types for SPI operations
#[derive(Debug, Clone, PartialEq)]
enum SPIError {
    TransferFailed,
    InvalidLength,
    DeviceNotFound,
    TimeoutError,
}

impl fmt::Display for SPIError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SPIError::TransferFailed => write!(f, "SPI transfer failed"),
            SPIError::InvalidLength => write!(f, "Invalid buffer length"),
            SPIError::DeviceNotFound => write!(f, "Device not found"),
            SPIError::TimeoutError => write!(f, "Operation timeout"),
        }
    }
}

// SPI Device trait
trait SPIDevice {
    fn initialize(&mut self) -> Result<(), SPIError>;
    fn transfer(&mut self, tx_data: &[u8]) -> Result<Vec<u8>, SPIError>;
    fn device_id(&self) -> u8;
    fn name(&self) -> &str;
}

// Simulated EEPROM device
struct EEPROM {
    id: u8,
    memory: Vec<u8>,
}

impl EEPROM {
    fn new(id: u8, size: usize) -> Self {
        EEPROM {
            id,
            memory: vec![0xFF; size],
        }
    }
    
    fn write_byte(&mut self, address: u16, data: u8) {
        let addr = address as usize;
        if addr < self.memory.len() {
            self.memory[addr] = data;
        }
    }
    
    fn read_byte(&self, address: u16) -> u8 {
        let addr = address as usize;
        if addr < self.memory.len() {
            self.memory[addr]
        } else {
            0xFF
        }
    }
}

impl SPIDevice for EEPROM {
    fn initialize(&mut self) -> Result<(), SPIError> {
        self.memory.fill(0xFF);
        Ok(())
    }
    
    fn transfer(&mut self, tx_data: &[u8]) -> Result<Vec<u8>, SPIError> {
        if tx_data.is_empty() {
            return Err(SPIError::InvalidLength);
        }
        
        let cmd = tx_data[0];
        let mut rx_data = vec![0; tx_data.len()];
        
        match cmd {
            0x03 if tx_data.len() >= 3 => {
                // Read command
                let address = ((tx_data[1] as u16) << 8) | (tx_data[2] as u16);
                for i in 3..tx_data.len() {
                    rx_data[i] = self.read_byte(address + (i - 3) as u16);
                }
                Ok(rx_data)
            }
            0x02 if tx_data.len() >= 3 => {
                // Write command
                let address = ((tx_data[1] as u16) << 8) | (tx_data[2] as u16);
                for i in 3..tx_data.len() {
                    self.write_byte(address + (i - 3) as u16, tx_data[i]);
                }
                Ok(rx_data)
            }
            _ => Err(SPIError::TransferFailed),
        }
    }
    
    fn device_id(&self) -> u8 {
        self.id
    }
    
    fn name(&self) -> &str {
        "EEPROM"
    }
}

// Test context and results
struct TestResult {
    name: String,
    passed: bool,
    message: Option<String>,
}

impl TestResult {
    fn pass(name: &str) -> Self {
        TestResult {
            name: name.to_string(),
            passed: true,
            message: None,
        }
    }
    
    fn fail(name: &str, message: &str) -> Self {
        TestResult {
            name: name.to_string(),
            passed: false,
            message: Some(message.to_string()),
        }
    }
}

// Integration test suite
struct SPIIntegrationTests {
    device: Box<dyn SPIDevice>,
    results: Vec<TestResult>,
}

impl SPIIntegrationTests {
    fn new(device: Box<dyn SPIDevice>) -> Self {
        SPIIntegrationTests {
            device,
            results: Vec::new(),
        }
    }
    
    fn test_basic_write_read(&mut self) {
        let test_name = "Basic Write/Read";
        println!("Running: {}", test_name);
        
        // Write sequence
        let write_data = vec![0x02, 0x00, 0x20, 0x11, 0x22, 0x33, 0x44];
        match self.device.transfer(&write_data) {
            Ok(_) => {},
            Err(e) => {
                self.results.push(TestResult::fail(test_name, &format!("Write failed: {}", e)));
                println!("  FAILED");
                return;
            }
        }
        
        // Read sequence
        let read_data = vec![0x03, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00];
        match self.device.transfer(&read_data) {
            Ok(rx) => {
                let expected = vec![0x11, 0x22, 0x33, 0x44];
                for (i, &exp_val) in expected.iter().enumerate() {
                    if rx[3 + i] != exp_val {
                        let msg = format!("Mismatch at byte {}: expected 0x{:02X}, got 0x{:02X}", 
                                         i, exp_val, rx[3 + i]);
                        self.results.push(TestResult::fail(test_name, &msg));
                        println!("  FAILED");
                        return;
                    }
                }
                self.results.push(TestResult::pass(test_name));
                println!("  PASSED");
            }
            Err(e) => {
                self.results.push(TestResult::fail(test_name, &format!("Read failed: {}", e)));
                println!("  FAILED");
            }
        }
    }
    
    fn test_boundary_conditions(&mut self) {
        let test_name = "Boundary Conditions";
        println!("Running: {}", test_name);
        
        // Test empty transfer
        let empty_data: Vec<u8> = vec![];
        match self.device.transfer(&empty_data) {
            Err(SPIError::InvalidLength) => {},
            _ => {
                self.results.push(TestResult::fail(test_name, "Empty transfer should fail"));
                println!("  FAILED");
                return;
            }
        }
        
        // Test invalid command
        let invalid_cmd = vec![0xFF, 0x00, 0x00];
        match self.device.transfer(&invalid_cmd) {
            Err(_) => {},
            Ok(_) => {
                self.results.push(TestResult::fail(test_name, "Invalid command should fail"));
                println!("  FAILED");
                return;
            }
        }
        
        self.results.push(TestResult::pass(test_name));
        println!("  PASSED");
    }
    
    fn test_sequential_operations(&mut self) {
        let test_name = "Sequential Operations";
        println!("Running: {}", test_name);
        
        for iteration in 0..5 {
            let address = (iteration * 16) as u16;
            let data_byte = (0x50 + iteration) as u8;
            
            // Write
            let write_cmd = vec![0x02, (address >> 8) as u8, address as u8, data_byte];
            if let Err(e) = self.device.transfer(&write_cmd) {
                let msg = format!("Iteration {} write failed: {}", iteration, e);
                self.results.push(TestResult::fail(test_name, &msg));
                println!("  FAILED");
                return;
            }
            
            // Read back
            let read_cmd = vec![0x03, (address >> 8) as u8, address as u8, 0x00];
            match self.device.transfer(&read_cmd) {
                Ok(rx) => {
                    if rx[3] != data_byte {
                        let msg = format!("Iteration {} mismatch", iteration);
                        self.results.push(TestResult::fail(test_name, &msg));
                        println!("  FAILED");
                        return;
                    }
                }
                Err(e) => {
                    let msg = format!("Iteration {} read failed: {}", iteration, e);
                    self.results.push(TestResult::fail(test_name, &msg));
                    println!("  FAILED");
                    return;
                }
            }
        }
        
        self.results.push(TestResult::pass(test_name));
        println!("  PASSED");
    }
    
    fn run_all_tests(&mut self) {
        println!("=== SPI Integration Testing (Rust) ===\n");
        
        if let Err(e) = self.device.initialize() {
            println!("Device initialization failed: {}", e);
            return;
        }
        
        self.test_basic_write_read();
        self.test_boundary_conditions();
        self.test_sequential_operations();
        
        self.print_summary();
    }
    
    fn print_summary(&self) {
        println!("\n=== Test Summary ===");
        let passed = self.results.iter().filter(|r| r.passed).count();
        let failed = self.results.iter().filter(|r| !r.passed).count();
        
        println!("Passed: {}", passed);
        println!("Failed: {}", failed);
        
        if failed > 0 {
            println!("\nFailed Tests:");
            for result in &self.results {
                if !result.passed {
                    println!("  - {}: {}", result.name, 
                            result.message.as_ref().unwrap_or(&"Unknown error".to_string()));
                }
            }
        }
    }
}

fn main() {
    let eeprom = EEPROM::new(1, 256);
    let mut test_suite = SPIIntegrationTests::new(Box::new(eeprom));
    test_suite.run_all_tests();
}
```

---

## Summary

**SPI Integration Testing** is crucial for ensuring reliable communication in embedded systems with multiple SPI devices. Key takeaways:

1. **Comprehensive Coverage**: Integration tests must validate end-to-end transactions, timing, error handling, and multi-device scenarios beyond what unit tests cover.

2. **Real-World Simulation**: Tests should simulate actual hardware behavior including delays, error conditions, and concurrent device access to catch integration issues early.

3. **Multi-Device Coordination**: Testing multiple SPI devices on the same bus validates chip select management, bus arbitration, and ensures devices don't interfere with each other.

4. **Error Resilience**: Integration tests must verify robust error handling, timeout management, and recovery mechanisms that are critical in production environments.

5. **Language-Specific Approaches**:
   - **C/C++**: Offers direct hardware control with manual memory management, suitable for bare-metal and RTOS environments
   - **Rust**: Provides memory safety guarantees and strong error handling through Result types, making integration tests more robust and preventing entire classes of bugs

6. **Test Automation**: Automated integration test suites enable continuous validation during development and catch regressions when hardware configurations or drivers change.

Integration testing bridges the gap between isolated unit tests and final system validation, ensuring SPI communication works reliably in complete, real-world system contexts.