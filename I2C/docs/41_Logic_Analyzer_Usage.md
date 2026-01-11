# Logic Analyzer Usage for I2C Debugging

Logic analyzers are essential tools for debugging I2C communication issues. They capture and visualize the actual electrical signals on the I2C bus, allowing you to see exactly what's happening at the hardware level.

## What is a Logic Analyzer?

A logic analyzer captures digital signals over time and displays them graphically. For I2C debugging, it shows:
- The timing of SDA (data) and SCL (clock) lines
- Start and stop conditions
- Address and data bytes
- ACK/NACK responses
- Protocol violations and timing issues

## Popular Logic Analyzers

- **Saleae Logic Analyzers** - Professional-grade with excellent software
- **DSLogic** - Affordable open-source hardware
- **PulseView/sigrok** - Open-source software supporting many devices
- **USB Logic Analyzers** - Budget-friendly 8-channel devices ($5-20)

## Setting Up for I2C Capture

### Hardware Connections

Connect your logic analyzer probes to:
1. **SCL (Clock)** - I2C clock line
2. **SDA (Data)** - I2C data line  
3. **GND** - Common ground reference

**Important**: Use high-impedance probes and avoid adding excessive capacitance to the bus, which can affect signal integrity.

## Interpreting I2C Captures

### Key Elements to Look For

1. **Start Condition**: SDA goes low while SCL is high
2. **Address Byte**: 7-bit address + R/W bit
3. **ACK/NACK**: SDA low = ACK, SDA high = NACK
4. **Data Bytes**: 8 bits per byte
5. **Stop Condition**: SDA goes high while SCL is high

### Common Issues Detected

- Missing ACK from slave device (device not responding)
- Bus contention (multiple devices driving the bus)
- Clock stretching problems
- Timing violations
- Electrical noise or signal integrity issues

## Code Examples

```c
/*
 * I2C Logic Analyzer Debugging Examples (C/C++)
 * 
 * This code demonstrates I2C patterns useful for logic analyzer debugging
 * Includes timing markers and deliberate test cases
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// Platform-specific I2C headers
#ifdef __linux__
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#elif defined(ARDUINO)
#include <Wire.h>
#endif

// =============================================================================
// Configuration for Logic Analyzer Debugging
// =============================================================================

#define I2C_BUS "/dev/i2c-1"
#define SLAVE_ADDR 0x48  // Example: TMP102 temperature sensor

// Debug markers - toggle GPIO to create timing markers in logic analyzer
#define DEBUG_PIN_START 23
#define DEBUG_PIN_END 24

// =============================================================================
// Debug Helper Functions
// =============================================================================

typedef struct {
    int fd;
    uint8_t addr;
    char name[32];
} i2c_device_t;

typedef struct {
    uint64_t start_time;
    uint64_t end_time;
    uint8_t address;
    uint8_t reg;
    uint8_t data_len;
    uint8_t data[64];
    bool success;
    char error_msg[128];
} i2c_transaction_log_t;

// Log buffer for post-analysis
#define MAX_LOG_ENTRIES 100
static i2c_transaction_log_t transaction_log[MAX_LOG_ENTRIES];
static int log_index = 0;

// Simple timestamp function (microseconds)
uint64_t get_timestamp_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Log transaction for correlation with logic analyzer capture
void log_transaction(uint8_t addr, uint8_t reg, uint8_t *data, 
                     uint8_t len, bool success, const char *error) {
    if (log_index >= MAX_LOG_ENTRIES) log_index = 0;
    
    i2c_transaction_log_t *entry = &transaction_log[log_index++];
    entry->end_time = get_timestamp_us();
    entry->address = addr;
    entry->reg = reg;
    entry->data_len = len;
    entry->success = success;
    
    if (data && len > 0) {
        memcpy(entry->data, data, len < 64 ? len : 64);
    }
    
    if (error) {
        strncpy(entry->error_msg, error, 127);
        entry->error_msg[127] = '\0';
    }
}

// =============================================================================
// I2C Communication Functions with Debug Support
// =============================================================================

int i2c_open(i2c_device_t *dev, const char *bus, uint8_t addr) {
    dev->fd = open(bus, O_RDWR);
    if (dev->fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    
    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        perror("Failed to set I2C slave address");
        close(dev->fd);
        return -1;
    }
    
    dev->addr = addr;
    return 0;
}

// Write with detailed logging for logic analyzer correlation
int i2c_write_reg_debug(i2c_device_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    uint8_t buffer[65];
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    
    uint64_t start = get_timestamp_us();
    printf("[%llu us] WRITE: Addr=0x%02X Reg=0x%02X Len=%zu Data=", 
           start, dev->addr, reg, len);
    for (size_t i = 0; i < len; i++) {
        printf("0x%02X ", data[i]);
    }
    printf("\n");
    
    ssize_t result = write(dev->fd, buffer, len + 1);
    
    bool success = (result == (ssize_t)(len + 1));
    log_transaction(dev->addr, reg, data, len, success, 
                    success ? NULL : "Write failed");
    
    if (!success) {
        printf("[ERROR] Write failed: expected %zu bytes, got %zd\n", 
               len + 1, result);
    }
    
    return success ? 0 : -1;
}

// Read with detailed logging
int i2c_read_reg_debug(i2c_device_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    uint64_t start = get_timestamp_us();
    
    // Write register address
    if (write(dev->fd, &reg, 1) != 1) {
        printf("[ERROR] Failed to write register address\n");
        log_transaction(dev->addr, reg, NULL, 0, false, "Reg write failed");
        return -1;
    }
    
    // Read data
    ssize_t result = read(dev->fd, data, len);
    
    printf("[%llu us] READ: Addr=0x%02X Reg=0x%02X Len=%zu Data=", 
           get_timestamp_us(), dev->addr, reg, len);
    
    if (result == (ssize_t)len) {
        for (size_t i = 0; i < len; i++) {
            printf("0x%02X ", data[i]);
        }
        printf("\n");
        log_transaction(dev->addr, reg, data, len, true, NULL);
        return 0;
    } else {
        printf("[ERROR] Read failed: expected %zu bytes, got %zd\n", len, result);
        log_transaction(dev->addr, reg, NULL, 0, false, "Read failed");
        return -1;
    }
}

// =============================================================================
// Test Patterns for Logic Analyzer Debugging
// =============================================================================

// Test 1: Single byte write/read sequence
void test_basic_transaction(i2c_device_t *dev) {
    printf("\n=== Test 1: Basic Transaction ===\n");
    uint8_t config = 0x60;
    
    i2c_write_reg_debug(dev, 0x01, &config, 1);
    usleep(10000); // 10ms delay
    
    uint8_t read_back;
    i2c_read_reg_debug(dev, 0x01, &read_back, 1);
    
    if (read_back == config) {
        printf("✓ Read-back verification passed\n");
    } else {
        printf("✗ Read-back mismatch: wrote 0x%02X, read 0x%02X\n", 
               config, read_back);
    }
}

// Test 2: Burst write - good for testing multi-byte transactions
void test_burst_write(i2c_device_t *dev) {
    printf("\n=== Test 2: Burst Write ===\n");
    uint8_t burst_data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    
    i2c_write_reg_debug(dev, 0x00, burst_data, sizeof(burst_data));
}

// Test 3: Rapid successive transactions - test timing
void test_rapid_transactions(i2c_device_t *dev) {
    printf("\n=== Test 3: Rapid Transactions ===\n");
    uint8_t data;
    
    for (int i = 0; i < 5; i++) {
        i2c_read_reg_debug(dev, 0x00, &data, 1);
        usleep(100); // Very short delay
    }
}

// Test 4: Deliberate error conditions
void test_error_conditions(i2c_device_t *dev) {
    printf("\n=== Test 4: Error Conditions ===\n");
    
    // Try to access non-existent register
    uint8_t data;
    printf("Attempting to read invalid register 0xFF:\n");
    i2c_read_reg_debug(dev, 0xFF, &data, 1);
    
    // Try wrong slave address
    printf("\nAttempting to access wrong address 0x7F:\n");
    i2c_device_t wrong_dev = *dev;
    wrong_dev.addr = 0x7F;
    ioctl(wrong_dev.fd, I2C_SLAVE, 0x7F);
    i2c_read_reg_debug(&wrong_dev, 0x00, &data, 1);
}

// Test 5: Clock stretching scenario (device-dependent)
void test_clock_stretching(i2c_device_t *dev) {
    printf("\n=== Test 5: Clock Stretching Test ===\n");
    printf("Reading temperature (may cause clock stretching):\n");
    
    uint8_t temp_data[2];
    i2c_read_reg_debug(dev, 0x00, temp_data, 2);
    
    // Convert to temperature
    int16_t raw = (temp_data[0] << 4) | (temp_data[1] >> 4);
    float temp = raw * 0.0625;
    printf("Temperature: %.2f°C\n", temp);
}

// =============================================================================
// Analysis and Reporting
// =============================================================================

void print_transaction_summary() {
    printf("\n=== Transaction Summary ===\n");
    printf("Total transactions: %d\n", log_index);
    
    int success_count = 0;
    int failure_count = 0;
    uint64_t total_time = 0;
    
    for (int i = 0; i < log_index; i++) {
        if (transaction_log[i].success) {
            success_count++;
        } else {
            failure_count++;
            printf("Failed transaction %d: Addr=0x%02X Reg=0x%02X Error=%s\n",
                   i, transaction_log[i].address, transaction_log[i].reg,
                   transaction_log[i].error_msg);
        }
        
        if (i > 0) {
            total_time += transaction_log[i].end_time - 
                         transaction_log[i-1].end_time;
        }
    }
    
    printf("Success: %d, Failures: %d\n", success_count, failure_count);
    if (log_index > 1) {
        printf("Average time between transactions: %llu us\n", 
               total_time / (log_index - 1));
    }
}

// Export log in CSV format for correlation with logic analyzer
void export_log_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open CSV file");
        return;
    }
    
    fprintf(f, "Timestamp_us,Address,Register,Length,Success,Data,Error\n");
    
    for (int i = 0; i < log_index; i++) {
        i2c_transaction_log_t *entry = &transaction_log[i];
        fprintf(f, "%llu,0x%02X,0x%02X,%d,%s,",
                entry->end_time, entry->address, entry->reg,
                entry->data_len, entry->success ? "true" : "false");
        
        for (int j = 0; j < entry->data_len && j < 64; j++) {
            fprintf(f, "%02X ", entry->data[j]);
        }
        
        fprintf(f, ",%s\n", entry->error_msg);
    }
    
    fclose(f);
    printf("Log exported to %s\n", filename);
}

// =============================================================================
// Main Test Routine
// =============================================================================

int main(int argc, char *argv[]) {
    i2c_device_t device;
    
    printf("I2C Logic Analyzer Debug Tool\n");
    printf("==============================\n\n");
    printf("Connect your logic analyzer to:\n");
    printf("  - SDA (GPIO2 on Raspberry Pi)\n");
    printf("  - SCL (GPIO3 on Raspberry Pi)\n");
    printf("  - GND\n\n");
    printf("Press Enter to start tests...");
    getchar();
    
    // Open I2C device
    if (i2c_open(&device, I2C_BUS, SLAVE_ADDR) < 0) {
        return 1;
    }
    
    printf("\n[%llu us] Starting test sequence\n", get_timestamp_us());
    
    // Run test patterns
    test_basic_transaction(&device);
    usleep(100000); // 100ms between tests
    
    test_burst_write(&device);
    usleep(100000);
    
    test_rapid_transactions(&device);
    usleep(100000);
    
    test_error_conditions(&device);
    usleep(100000);
    
    test_clock_stretching(&device);
    
    printf("\n[%llu us] Test sequence complete\n", get_timestamp_us());
    
    // Analysis
    print_transaction_summary();
    export_log_csv("i2c_debug_log.csv");
    
    close(device.fd);
    
    printf("\nAnalysis Tips:\n");
    printf("1. Import i2c_debug_log.csv into your logic analyzer software\n");
    printf("2. Use timestamps to correlate software events with captures\n");
    printf("3. Look for ACK/NACK patterns on failed transactions\n");
    printf("4. Check for clock stretching during temperature reads\n");
    printf("5. Verify timing meets I2C spec (100kHz/400kHz)\n");
    
    return 0;
}
```
---

```rust
/*
 * I2C Logic Analyzer Debugging Examples (Rust)
 * 
 * Demonstrates I2C patterns for logic analyzer debugging with
 * comprehensive logging and analysis capabilities
 */

use std::fs::File;
use std::io::{self, Write};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// External crate dependencies:
// linux-embedded-hal = "0.4"
// embedded-hal = "1.0"

#[cfg(target_os = "linux")]
use linux_embedded_hal::{I2cdev, Delay};
use embedded_hal::i2c::{I2c, Operation};

// =============================================================================
// Transaction Logging Structures
// =============================================================================

#[derive(Debug, Clone)]
struct TransactionLog {
    timestamp_us: u64,
    address: u8,
    register: u8,
    data: Vec<u8>,
    operation: OperationType,
    success: bool,
    error_message: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
enum OperationType {
    Write,
    Read,
    WriteRead,
}

struct I2cDebugger {
    device: I2cdev,
    address: u8,
    transaction_log: Vec<TransactionLog>,
    start_time: Instant,
}

// =============================================================================
// Debug Implementation
// =============================================================================

impl I2cDebugger {
    fn new(bus_path: &str, address: u8) -> Result<Self, Box<dyn std::error::Error>> {
        let device = I2cdev::new(bus_path)?;
        
        Ok(Self {
            device,
            address,
            transaction_log: Vec::new(),
            start_time: Instant::now(),
        })
    }
    
    fn get_timestamp_us(&self) -> u64 {
        self.start_time.elapsed().as_micros() as u64
    }
    
    fn get_system_time_us() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_micros() as u64
    }
    
    // Write to register with debug logging
    fn write_register_debug(
        &mut self,
        register: u8,
        data: &[u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let timestamp = self.get_timestamp_us();
        
        print!("[{} us] WRITE: Addr=0x{:02X} Reg=0x{:02X} Len={} Data=",
               timestamp, self.address, register, data.len());
        for byte in data {
            print!("0x{:02X} ", byte);
        }
        println!();
        
        // Prepare write buffer with register address
        let mut buffer = vec![register];
        buffer.extend_from_slice(data);
        
        let result = self.device.write(self.address, &buffer);
        
        let success = result.is_ok();
        let error_msg = result.err().map(|e| e.to_string());
        
        self.log_transaction(TransactionLog {
            timestamp_us: timestamp,
            address: self.address,
            register,
            data: data.to_vec(),
            operation: OperationType::Write,
            success,
            error_message: error_msg.clone(),
        });
        
        if let Some(err) = error_msg {
            println!("[ERROR] Write failed: {}", err);
            return Err(err.into());
        }
        
        Ok(())
    }
    
    // Read from register with debug logging
    fn read_register_debug(
        &mut self,
        register: u8,
        buffer: &mut [u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let timestamp = self.get_timestamp_us();
        
        // Use write_read for combined operation
        let result = self.device.write_read(self.address, &[register], buffer);
        
        print!("[{} us] READ: Addr=0x{:02X} Reg=0x{:02X} Len={} Data=",
               timestamp, self.address, register, buffer.len());
        
        let success = result.is_ok();
        let error_msg = result.err().map(|e| e.to_string());
        
        if success {
            for byte in buffer.iter() {
                print!("0x{:02X} ", byte);
            }
            println!();
        } else {
            println!("[ERROR] Read failed: {}", error_msg.as_ref().unwrap());
        }
        
        self.log_transaction(TransactionLog {
            timestamp_us: timestamp,
            address: self.address,
            register,
            data: buffer.to_vec(),
            operation: OperationType::Read,
            success,
            error_message: error_msg.clone(),
        });
        
        if let Some(err) = error_msg {
            return Err(err.into());
        }
        
        Ok(())
    }
    
    fn log_transaction(&mut self, log: TransactionLog) {
        self.transaction_log.push(log);
    }
    
    // =============================================================================
    // Test Patterns
    // =============================================================================
    
    fn test_basic_transaction(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 1: Basic Transaction ===");
        
        let config_value = 0x60u8;
        self.write_register_debug(0x01, &[config_value])?;
        
        thread::sleep(Duration::from_millis(10));
        
        let mut read_back = [0u8];
        self.read_register_debug(0x01, &mut read_back)?;
        
        if read_back[0] == config_value {
            println!("✓ Read-back verification passed");
        } else {
            println!("✗ Read-back mismatch: wrote 0x{:02X}, read 0x{:02X}",
                     config_value, read_back[0]);
        }
        
        Ok(())
    }
    
    fn test_burst_write(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 2: Burst Write ===");
        
        let burst_data = [0x11, 0x22, 0x33, 0x44, 0x55];
        self.write_register_debug(0x00, &burst_data)?;
        
        Ok(())
    }
    
    fn test_rapid_transactions(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 3: Rapid Transactions ===");
        
        for i in 0..5 {
            let mut data = [0u8];
            match self.read_register_debug(0x00, &mut data) {
                Ok(_) => println!("Transaction {} completed", i),
                Err(e) => println!("Transaction {} failed: {}", i, e),
            }
            thread::sleep(Duration::from_micros(100));
        }
        
        Ok(())
    }
    
    fn test_error_conditions(&mut self) {
        println!("\n=== Test 4: Error Conditions ===");
        
        // Try invalid register
        println!("Attempting to read invalid register 0xFF:");
        let mut data = [0u8];
        let _ = self.read_register_debug(0xFF, &mut data);
        
        // Try wrong address (store original)
        let original_addr = self.address;
        self.address = 0x7F;
        
        println!("\nAttempting to access wrong address 0x7F:");
        let _ = self.read_register_debug(0x00, &mut data);
        
        // Restore original address
        self.address = original_addr;
    }
    
    fn test_clock_stretching(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 5: Clock Stretching Test ===");
        println!("Reading temperature (may cause clock stretching):");
        
        let mut temp_data = [0u8; 2];
        self.read_register_debug(0x00, &mut temp_data)?;
        
        // Convert to temperature (TMP102 format)
        let raw = ((temp_data[0] as i16) << 4) | ((temp_data[1] as i16) >> 4);
        let temperature = (raw as f32) * 0.0625;
        println!("Temperature: {:.2}°C", temperature);
        
        Ok(())
    }
    
    fn test_repeated_start(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 6: Repeated Start Condition ===");
        
        // This generates: START - ADDR+W - REG - RESTART - ADDR+R - DATA - STOP
        let mut buffer = [0u8; 2];
        self.read_register_debug(0x00, &mut buffer)?;
        
        println!("Repeated start transaction completed");
        Ok(())
    }
    
    // =============================================================================
    // Analysis and Reporting
    // =============================================================================
    
    fn print_transaction_summary(&self) {
        println!("\n=== Transaction Summary ===");
        println!("Total transactions: {}", self.transaction_log.len());
        
        let success_count = self.transaction_log.iter()
            .filter(|log| log.success)
            .count();
        let failure_count = self.transaction_log.len() - success_count;
        
        println!("Success: {}, Failures: {}", success_count, failure_count);
        
        // Print failed transactions
        for (i, log) in self.transaction_log.iter().enumerate() {
            if !log.success {
                println!("Failed transaction {}: Addr=0x{:02X} Reg=0x{:02X} Error={}",
                         i, log.address, log.register,
                         log.error_message.as_deref().unwrap_or("Unknown"));
            }
        }
        
        // Calculate timing statistics
        if self.transaction_log.len() > 1 {
            let mut intervals: Vec<u64> = Vec::new();
            for i in 1..self.transaction_log.len() {
                let interval = self.transaction_log[i].timestamp_us - 
                              self.transaction_log[i-1].timestamp_us;
                intervals.push(interval);
            }
            
            let avg_interval = intervals.iter().sum::<u64>() / intervals.len() as u64;
            let min_interval = intervals.iter().min().unwrap();
            let max_interval = intervals.iter().max().unwrap();
            
            println!("\nTiming Statistics:");
            println!("  Average interval: {} us", avg_interval);
            println!("  Min interval: {} us", min_interval);
            println!("  Max interval: {} us", max_interval);
        }
        
        // Operation type breakdown
        let write_count = self.transaction_log.iter()
            .filter(|log| log.operation == OperationType::Write)
            .count();
        let read_count = self.transaction_log.iter()
            .filter(|log| log.operation == OperationType::Read)
            .count();
        
        println!("\nOperation Breakdown:");
        println!("  Writes: {}", write_count);
        println!("  Reads: {}", read_count);
    }
    
    fn export_csv(&self, filename: &str) -> io::Result<()> {
        let mut file = File::create(filename)?;
        
        writeln!(file, "Timestamp_us,Address,Register,Length,Operation,Success,Data,Error")?;
        
        for log in &self.transaction_log {
            let data_str = log.data.iter()
                .map(|b| format!("{:02X}", b))
                .collect::<Vec<_>>()
                .join(" ");
            
            let op_str = match log.operation {
                OperationType::Write => "WRITE",
                OperationType::Read => "READ",
                OperationType::WriteRead => "WRITE_READ",
            };
            
            writeln!(
                file,
                "{},0x{:02X},0x{:02X},{},{},{},\"{}\",\"{}\"",
                log.timestamp_us,
                log.address,
                log.register,
                log.data.len(),
                op_str,
                log.success,
                data_str,
                log.error_message.as_deref().unwrap_or("")
            )?;
        }
        
        println!("Log exported to {}", filename);
        Ok(())
    }
    
    fn export_vcd(&self, filename: &str) -> io::Result<()> {
        let mut file = File::create(filename)?;
        
        // VCD header
        writeln!(file, "$version Generated by I2C Debugger $end")?;
        writeln!(file, "$timescale 1us $end")?;
        writeln!(file, "$scope module i2c $end")?;
        writeln!(file, "$var wire 1 ! sda $end")?;
        writeln!(file, "$var wire 1 @ scl $end")?;
        writeln!(file, "$upscope $end")?;
        writeln!(file, "$enddefinitions $end")?;
        
        // VCD body (simplified - just marks transaction times)
        for log in &self.transaction_log {
            writeln!(file, "#{}", log.timestamp_us)?;
            writeln!(file, "0!")?; // SDA activity marker
            writeln!(file, "#{}", log.timestamp_us + 1)?;
            writeln!(file, "1!")?;
        }
        
        println!("VCD waveform exported to {}", filename);
        Ok(())
    }
}

// =============================================================================
// Main Test Application
// =============================================================================

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("I2C Logic Analyzer Debug Tool (Rust)");
    println!("====================================\n");
    println!("Connect your logic analyzer to:");
    println!("  - SDA (GPIO2 on Raspberry Pi)");
    println!("  - SCL (GPIO3 on Raspberry Pi)");
    println!("  - GND\n");
    println!("Press Enter to start tests...");
    
    let mut input = String::new();
    io::stdin().read_line(&mut input)?;
    
    // Initialize debugger
    let mut debugger = I2cDebugger::new("/dev/i2c-1", 0x48)?;
    
    println!("\n[{} us] Starting test sequence", debugger.get_timestamp_us());
    
    // Run test patterns
    let _ = debugger.test_basic_transaction();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_burst_write();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_rapid_transactions();
    thread::sleep(Duration::from_millis(100));
    
    debugger.test_error_conditions();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_clock_stretching();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_repeated_start();
    
    println!("\n[{} us] Test sequence complete", debugger.get_timestamp_us());
    
    // Analysis and export
    debugger.print_transaction_summary();
    debugger.export_csv("i2c_debug_log.csv")?;
    debugger.export_vcd("i2c_waveform.vcd")?;
    
    println!("\n=== Analysis Tips ===");
    println!("1. Import i2c_debug_log.csv into your logic analyzer software");
    println!("2. Use timestamps to correlate software events with captures");
    println!("3. Check i2c_waveform.vcd for timing visualization");
    println!("4. Look for ACK/NACK patterns on failed transactions");
    println!("5. Verify timing meets I2C spec:");
    println!("   - Standard mode: 100 kHz");
    println!("   - Fast mode: 400 kHz");
    println!("   - Fast mode plus: 1 MHz");
    println!("6. Check for clock stretching (SCL held low by slave)");
    println!("7. Verify proper START and STOP conditions");
    
    Ok(())
}

// =============================================================================
// Additional Utility Functions
// =============================================================================

#[allow(dead_code)]
fn analyze_bus_speed(timestamps: &[u64]) -> Option<f64> {
    if timestamps.len() < 2 {
        return None;
    }
    
    let total_time = timestamps.last().unwrap() - timestamps.first().unwrap();
    let num_transactions = timestamps.len() as f64;
    
    // Rough estimate: assume ~9 clocks per byte (8 data + 1 ACK)
    let estimated_clocks = num_transactions * 9.0;
    let clock_period_us = total_time as f64 / estimated_clocks;
    let frequency_khz = 1000.0 / clock_period_us;
    
    Some(frequency_khz)
}

#[allow(dead_code)]
fn detect_clock_stretching(log: &[TransactionLog]) -> Vec<usize> {
    // Detect transactions that took unusually long
    let mut stretching_indices = Vec::new();
    
    if log.len() < 2 {
        return stretching_indices;
    }
    
    let mut intervals: Vec<u64> = Vec::new();
    for i in 1..log.len() {
        intervals.push(log[i].timestamp_us - log[i-1].timestamp_us);
    }
    
    let avg = intervals.iter().sum::<u64>() / intervals.len() as u64;
    let threshold = avg * 3; // Transactions taking 3x average
    
    for (i, &interval) in intervals.iter().enumerate() {
        if interval > threshold {
            stretching_indices.push(i + 1);
        }
    }
    
    stretching_indices
}
```

## Practical Logic Analyzer Analysis Tips

### Setting Up Your Capture

1. **Configure Sampling Rate**: Use at least 4-10x the I2C clock frequency
   - For 100 kHz I2C: 1-4 MHz sampling
   - For 400 kHz I2C: 4-10 MHz sampling

2. **Set Trigger Conditions**: Trigger on START condition (SDA falling while SCL high) or specific address patterns

3. **Protocol Decoder**: Enable I2C protocol decoder in your software (Saleae, PulseView, etc.)

### Common Debugging Scenarios

**Problem: Device Not Responding**
- Look for: Missing ACK after address byte
- Causes: Wrong address, device not powered, bus pulled up incorrectly

**Problem: Intermittent Communication**
- Look for: Glitches on SDA/SCL, timing violations
- Causes: EMI, poor connections, capacitance issues

**Problem: Clock Stretching Issues**
- Look for: SCL held low by slave between bytes
- Causes: Slave device processing time exceeded

**Problem: Bus Contention**
- Look for: Both devices driving SDA simultaneously
- Causes: Address conflicts, improper multi-master handling

### Software Integration Workflow

1. **Run your test code** (examples above) to generate known I2C traffic
2. **Capture with logic analyzer** - synchronize start with software timestamp
3. **Export software logs** - CSV files with timestamps
4. **Correlate events** - match software operations to hardware captures
5. **Identify issues** - compare expected vs actual waveforms

### Key Measurements to Check

- **Setup time (tSU)**: SDA stable before SCL rises (min 100ns for Fast mode)
- **Hold time (tHD)**: SDA stable after SCL falls (min 0ns for Fast mode)
- **Clock low period**: Minimum 1.3μs (Fast mode)
- **Clock high period**: Minimum 0.6μs (Fast mode)
- **Rise time**: Should be <300ns (depends on pull-ups and capacitance)
- **Fall time**: Should be <300ns

The code examples I've provided generate specific test patterns that help you verify these timing parameters and identify common issues. The transaction logs with timestamps allow you to correlate what your software thinks happened with what actually occurred on the bus.