# Protocol Compliance Testing in Modbus

## Detailed Description

Protocol Compliance Testing is a systematic verification process that ensures a Modbus implementation correctly adheres to the official Modbus specification. This testing validates that devices, libraries, and applications properly implement the protocol's requirements, ensuring interoperability between different vendors' equipment and preventing communication failures in industrial networks.

### Key Aspects of Compliance Testing

**1. Specification Conformance**
- Verifies adherence to Modbus Application Protocol (V1.1b3) and Modbus TCP/IP specification
- Tests proper implementation of function codes (read/write coils, registers, etc.)
- Validates exception responses and error handling
- Ensures correct frame formatting and byte ordering

**2. Test Coverage Areas**
- **Function Code Testing**: All standard function codes (0x01-0x17)
- **Data Model Testing**: Coils, discrete inputs, holding registers, input registers
- **Error Handling**: Exception codes, timeout behavior, malformed requests
- **Protocol Layers**: RTU/ASCII/TCP frame validation
- **Timing Requirements**: Response times, inter-frame delays
- **Edge Cases**: Boundary values, maximum PDU sizes, address ranges

**3. Standardized Test Suites**
- Automated test frameworks
- Conformance test scenarios
- Interoperability testing between vendors
- Performance benchmarking
- Security and robustness testing

## C/C++ Implementation

### Compliance Test Framework

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Modbus function codes
#define MODBUS_FC_READ_COILS                0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS      0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04
#define MODBUS_FC_WRITE_SINGLE_COIL         0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS      0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

// Exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION       0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     0x03
#define MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE  0x04

// Test result structure
typedef struct {
    char test_name[128];
    bool passed;
    char message[256];
} TestResult;

// Test suite structure
typedef struct {
    TestResult *results;
    int total_tests;
    int passed_tests;
    int failed_tests;
} TestSuite;

// Initialize test suite
TestSuite* init_test_suite(int max_tests) {
    TestSuite *suite = malloc(sizeof(TestSuite));
    suite->results = malloc(sizeof(TestResult) * max_tests);
    suite->total_tests = 0;
    suite->passed_tests = 0;
    suite->failed_tests = 0;
    return suite;
}

// Add test result
void add_test_result(TestSuite *suite, const char *name, bool passed, const char *msg) {
    TestResult *result = &suite->results[suite->total_tests++];
    strncpy(result->test_name, name, sizeof(result->test_name) - 1);
    result->passed = passed;
    strncpy(result->message, msg, sizeof(result->message) - 1);
    
    if (passed) {
        suite->passed_tests++;
    } else {
        suite->failed_tests++;
    }
}

// CRC16 calculation for Modbus RTU
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Test: Read Coils function code compliance
bool test_read_coils_compliance(TestSuite *suite) {
    uint8_t request[8];
    uint8_t expected_response[6];
    
    // Build read coils request: slave=1, function=1, start=0, count=10
    request[0] = 0x01;  // Slave address
    request[1] = MODBUS_FC_READ_COILS;
    request[2] = 0x00;  // Start address high
    request[3] = 0x00;  // Start address low
    request[4] = 0x00;  // Quantity high
    request[5] = 0x0A;  // Quantity low (10 coils)
    
    uint16_t crc = calculate_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Verify request format
    bool format_valid = (request[1] == MODBUS_FC_READ_COILS) && 
                        (request[4] == 0x00 && request[5] == 0x0A);
    
    add_test_result(suite, "Read Coils Request Format", format_valid,
                    format_valid ? "Request properly formatted" : "Invalid request format");
    
    return format_valid;
}

// Test: Exception response compliance
bool test_exception_response_compliance(TestSuite *suite) {
    uint8_t request[8];
    uint8_t exception_response[5];
    
    // Request with illegal data address
    request[0] = 0x01;
    request[1] = MODBUS_FC_READ_COILS;
    request[2] = 0xFF;  // Invalid high address
    request[3] = 0xFF;
    request[4] = 0x00;
    request[5] = 0x01;
    
    // Expected exception response
    exception_response[0] = 0x01;  // Slave address
    exception_response[1] = MODBUS_FC_READ_COILS | 0x80;  // Error bit set
    exception_response[2] = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    
    uint16_t crc = calculate_crc16(exception_response, 3);
    exception_response[3] = crc & 0xFF;
    exception_response[4] = (crc >> 8) & 0xFF;
    
    // Verify exception response format
    bool exception_valid = (exception_response[1] & 0x80) && 
                          (exception_response[2] == MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    
    add_test_result(suite, "Exception Response Format", exception_valid,
                    exception_valid ? "Exception properly formatted" : "Invalid exception format");
    
    return exception_valid;
}

// Test: Write single register compliance
bool test_write_single_register_compliance(TestSuite *suite) {
    uint8_t request[8];
    
    // Write single register: slave=1, function=6, address=100, value=0x1234
    request[0] = 0x01;
    request[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    request[2] = 0x00;  // Address high
    request[3] = 0x64;  // Address low (100)
    request[4] = 0x12;  // Value high
    request[5] = 0x34;  // Value low
    
    uint16_t crc = calculate_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Verify CRC
    uint16_t calculated_crc = calculate_crc16(request, 6);
    uint16_t received_crc = request[6] | (request[7] << 8);
    
    bool crc_valid = (calculated_crc == received_crc);
    
    add_test_result(suite, "Write Single Register CRC", crc_valid,
                    crc_valid ? "CRC validation passed" : "CRC validation failed");
    
    return crc_valid;
}

// Test: PDU size limits
bool test_pdu_size_limits(TestSuite *suite) {
    const int MAX_PDU_SIZE = 253;  // Maximum Modbus PDU size
    bool size_valid = true;
    
    // Test maximum write multiple registers request
    // 1 byte function + 2 bytes address + 2 bytes count + 1 byte byte count + N bytes data
    int max_registers = (MAX_PDU_SIZE - 6) / 2;  // Should be 123
    
    size_valid = (max_registers == 123);
    
    add_test_result(suite, "PDU Size Limits", size_valid,
                    size_valid ? "PDU size calculations correct" : "PDU size limit exceeded");
    
    return size_valid;
}

// Test: Address range validation
bool test_address_range_validation(TestSuite *suite) {
    const uint16_t MAX_ADDRESS = 0xFFFF;
    bool range_valid = true;
    
    // Test valid address range (0-65535)
    uint16_t test_address = 65535;
    range_valid = (test_address <= MAX_ADDRESS);
    
    add_test_result(suite, "Address Range Validation", range_valid,
                    range_valid ? "Address range valid" : "Address out of range");
    
    return range_valid;
}

// Run compliance test suite
void run_compliance_tests() {
    printf("=== Modbus Protocol Compliance Test Suite ===\n\n");
    
    TestSuite *suite = init_test_suite(100);
    
    // Run all tests
    test_read_coils_compliance(suite);
    test_exception_response_compliance(suite);
    test_write_single_register_compliance(suite);
    test_pdu_size_limits(suite);
    test_address_range_validation(suite);
    
    // Print results
    printf("\n=== Test Results ===\n");
    printf("Total Tests: %d\n", suite->total_tests);
    printf("Passed: %d\n", suite->passed_tests);
    printf("Failed: %d\n\n", suite->failed_tests);
    
    for (int i = 0; i < suite->total_tests; i++) {
        TestResult *result = &suite->results[i];
        printf("[%s] %s: %s\n", 
               result->passed ? "PASS" : "FAIL",
               result->test_name,
               result->message);
    }
    
    // Cleanup
    free(suite->results);
    free(suite);
}

int main() {
    run_compliance_tests();
    return 0;
}
```

### Advanced Compliance Testing with Timing

```c
#include <time.h>
#include <sys/time.h>

// Timing test structure
typedef struct {
    struct timeval start;
    struct timeval end;
    double elapsed_ms;
} TimingTest;

void start_timing(TimingTest *test) {
    gettimeofday(&test->start, NULL);
}

void stop_timing(TimingTest *test) {
    gettimeofday(&test->end, NULL);
    test->elapsed_ms = (test->end.tv_sec - test->start.tv_sec) * 1000.0;
    test->elapsed_ms += (test->end.tv_usec - test->start.tv_usec) / 1000.0;
}

// Test response time compliance
bool test_response_time_compliance(TestSuite *suite) {
    const double MAX_RESPONSE_TIME_MS = 100.0;  // Example: 100ms max
    TimingTest timing;
    
    start_timing(&timing);
    
    // Simulate Modbus transaction
    // In real implementation, send request and wait for response
    usleep(50000);  // Simulate 50ms response
    
    stop_timing(&timing);
    
    bool timing_valid = (timing.elapsed_ms <= MAX_RESPONSE_TIME_MS);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Response time: %.2fms (max: %.2fms)",
             timing.elapsed_ms, MAX_RESPONSE_TIME_MS);
    
    add_test_result(suite, "Response Time Compliance", timing_valid, msg);
    
    return timing_valid;
}
```

## Rust Implementation

### Compliance Test Framework in Rust

```rust
use std::time::{Duration, Instant};
use std::collections::HashMap;

// Modbus function codes
const FC_READ_COILS: u8 = 0x01;
const FC_READ_DISCRETE_INPUTS: u8 = 0x02;
const FC_READ_HOLDING_REGISTERS: u8 = 0x03;
const FC_READ_INPUT_REGISTERS: u8 = 0x04;
const FC_WRITE_SINGLE_COIL: u8 = 0x05;
const FC_WRITE_SINGLE_REGISTER: u8 = 0x06;
const FC_WRITE_MULTIPLE_COILS: u8 = 0x0F;
const FC_WRITE_MULTIPLE_REGISTERS: u8 = 0x10;

// Exception codes
const EXCEPTION_ILLEGAL_FUNCTION: u8 = 0x01;
const EXCEPTION_ILLEGAL_DATA_ADDRESS: u8 = 0x02;
const EXCEPTION_ILLEGAL_DATA_VALUE: u8 = 0x03;
const EXCEPTION_SERVER_DEVICE_FAILURE: u8 = 0x04;

// Test result structure
#[derive(Debug, Clone)]
struct TestResult {
    test_name: String,
    passed: bool,
    message: String,
    duration: Option<Duration>,
}

// Test suite
struct ComplianceTestSuite {
    results: Vec<TestResult>,
    passed_count: usize,
    failed_count: usize,
}

impl ComplianceTestSuite {
    fn new() -> Self {
        ComplianceTestSuite {
            results: Vec::new(),
            passed_count: 0,
            failed_count: 0,
        }
    }

    fn add_result(&mut self, name: &str, passed: bool, message: &str, duration: Option<Duration>) {
        self.results.push(TestResult {
            test_name: name.to_string(),
            passed,
            message: message.to_string(),
            duration,
        });

        if passed {
            self.passed_count += 1;
        } else {
            self.failed_count += 1;
        }
    }

    fn print_summary(&self) {
        println!("\n=== Compliance Test Summary ===");
        println!("Total Tests: {}", self.results.len());
        println!("Passed: {}", self.passed_count);
        println!("Failed: {}", self.failed_count);
        println!("Success Rate: {:.2}%\n", 
                 (self.passed_count as f64 / self.results.len() as f64) * 100.0);

        for result in &self.results {
            let status = if result.passed { "✓ PASS" } else { "✗ FAIL" };
            let duration_str = result.duration
                .map(|d| format!(" ({:.2}ms)", d.as_secs_f64() * 1000.0))
                .unwrap_or_default();
            
            println!("[{}] {}{}: {}", 
                     status, result.test_name, duration_str, result.message);
        }
    }
}

// CRC16 calculation for Modbus RTU
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;

    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

// Modbus request structure
#[derive(Debug)]
struct ModbusRequest {
    slave_id: u8,
    function_code: u8,
    data: Vec<u8>,
}

impl ModbusRequest {
    fn to_rtu_frame(&self) -> Vec<u8> {
        let mut frame = Vec::new();
        frame.push(self.slave_id);
        frame.push(self.function_code);
        frame.extend_from_slice(&self.data);

        let crc = calculate_crc16(&frame);
        frame.push((crc & 0xFF) as u8);
        frame.push(((crc >> 8) & 0xFF) as u8);

        frame
    }

    fn validate(&self) -> Result<(), String> {
        // Validate function code
        let valid_functions = [
            FC_READ_COILS, FC_READ_DISCRETE_INPUTS,
            FC_READ_HOLDING_REGISTERS, FC_READ_INPUT_REGISTERS,
            FC_WRITE_SINGLE_COIL, FC_WRITE_SINGLE_REGISTER,
            FC_WRITE_MULTIPLE_COILS, FC_WRITE_MULTIPLE_REGISTERS,
        ];

        if !valid_functions.contains(&self.function_code) {
            return Err(format!("Invalid function code: 0x{:02X}", self.function_code));
        }

        // Validate PDU size (max 253 bytes)
        let pdu_size = 1 + self.data.len(); // function code + data
        if pdu_size > 253 {
            return Err(format!("PDU size {} exceeds maximum of 253 bytes", pdu_size));
        }

        Ok(())
    }
}

// Test: Read Coils compliance
fn test_read_coils_compliance(suite: &mut ComplianceTestSuite) {
    let start = Instant::now();

    let mut data = Vec::new();
    data.extend_from_slice(&0u16.to_be_bytes());  // Start address
    data.extend_from_slice(&10u16.to_be_bytes()); // Quantity (10 coils)

    let request = ModbusRequest {
        slave_id: 1,
        function_code: FC_READ_COILS,
        data,
    };

    let frame = request.to_rtu_frame();
    let duration = start.elapsed();

    // Verify frame structure
    let passed = frame[0] == 1 && 
                 frame[1] == FC_READ_COILS &&
                 frame.len() == 8; // 1 slave + 1 fc + 4 data + 2 crc

    suite.add_result(
        "Read Coils Request Format",
        passed,
        if passed { "Request properly formatted" } else { "Invalid request format" },
        Some(duration),
    );
}

// Test: Exception response compliance
fn test_exception_response_compliance(suite: &mut ComplianceTestSuite) {
    let start = Instant::now();

    // Build exception response for illegal data address
    let mut response = Vec::new();
    response.push(1u8);  // Slave ID
    response.push(FC_READ_COILS | 0x80);  // Function code with error bit
    response.push(EXCEPTION_ILLEGAL_DATA_ADDRESS);

    let crc = calculate_crc16(&response);
    response.push((crc & 0xFF) as u8);
    response.push(((crc >> 8) & 0xFF) as u8);

    let duration = start.elapsed();

    // Verify exception response format
    let has_error_bit = response[1] & 0x80 != 0;
    let valid_exception = response[2] == EXCEPTION_ILLEGAL_DATA_ADDRESS;
    let passed = has_error_bit && valid_exception;

    suite.add_result(
        "Exception Response Format",
        passed,
        if passed { "Exception properly formatted" } else { "Invalid exception format" },
        Some(duration),
    );
}

// Test: Write multiple registers compliance
fn test_write_multiple_registers_compliance(suite: &mut ComplianceTestSuite) {
    let start = Instant::now();

    let start_address: u16 = 100;
    let values: Vec<u16> = vec![0x1234, 0x5678, 0xABCD];
    
    let mut data = Vec::new();
    data.extend_from_slice(&start_address.to_be_bytes());
    data.extend_from_slice(&(values.len() as u16).to_be_bytes());
    data.push((values.len() * 2) as u8); // Byte count
    
    for value in &values {
        data.extend_from_slice(&value.to_be_bytes());
    }

    let request = ModbusRequest {
        slave_id: 1,
        function_code: FC_WRITE_MULTIPLE_REGISTERS,
        data,
    };

    let validation_result = request.validate();
    let duration = start.elapsed();

    let passed = validation_result.is_ok();
    let message = validation_result.unwrap_or_else(|e| e);

    suite.add_result(
        "Write Multiple Registers Validation",
        passed,
        &message,
        Some(duration),
    );
}

// Test: CRC calculation compliance
fn test_crc_calculation_compliance(suite: &mut ComplianceTestSuite) {
    let start = Instant::now();

    // Test with known data and CRC
    let data = vec![0x01, 0x03, 0x00, 0x00, 0x00, 0x02];
    let calculated_crc = calculate_crc16(&data);
    let expected_crc: u16 = 0xC40B;  // Known CRC for this data

    let duration = start.elapsed();

    let passed = calculated_crc == expected_crc;

    suite.add_result(
        "CRC16 Calculation",
        passed,
        &format!("Calculated: 0x{:04X}, Expected: 0x{:04X}", calculated_crc, expected_crc),
        Some(duration),
    );
}

// Test: PDU size limits
fn test_pdu_size_limits(suite: &mut ComplianceTestSuite) {
    let start = Instant::now();

    const MAX_PDU_SIZE: usize = 253;
    
    // Calculate maximum registers for write multiple
    // 1 (fc) + 2 (addr) + 2 (count) + 1 (byte count) + N (data) <= 253
    let max_registers = (MAX_PDU_SIZE - 6) / 2;
    
    let mut data = Vec::new();
    data.extend_from_slice(&0u16.to_be_bytes());  // Address
    data.extend_from_slice(&(max_registers as u16).to_be_bytes());  // Count
    data.push((max_registers * 2) as u8);  // Byte count
    data.resize(data.len() + max_registers * 2, 0);  // Fill with zeros

    let request = ModbusRequest {
        slave_id: 1,
        function_code: FC_WRITE_MULTIPLE_REGISTERS,
        data,
    };

    let validation_result = request.validate();
    let duration = start.elapsed();

    let passed = validation_result.is_ok() && max_registers == 123;

    suite.add_result(
        "PDU Size Limits",
        passed,
        &format!("Max registers: {} (expected 123)", max_registers),
        Some(duration),
    );
}

// Test: Response timeout compliance
fn test_response_timeout_compliance(suite: &mut ComplianceTestSuite) {
    let max_timeout = Duration::from_millis(100);
    let start = Instant::now();

    // Simulate Modbus transaction
    std::thread::sleep(Duration::from_millis(50));

    let elapsed = start.elapsed();
    let passed = elapsed <= max_timeout;

    suite.add_result(
        "Response Timeout",
        passed,
        &format!("Response time: {:.2}ms (max: {:.2}ms)", 
                 elapsed.as_secs_f64() * 1000.0,
                 max_timeout.as_secs_f64() * 1000.0),
        Some(elapsed),
    );
}

// Main test runner
fn main() {
    println!("=== Modbus Protocol Compliance Test Suite ===\n");

    let mut suite = ComplianceTestSuite::new();

    // Run all compliance tests
    test_read_coils_compliance(&mut suite);
    test_exception_response_compliance(&mut suite);
    test_write_multiple_registers_compliance(&mut suite);
    test_crc_calculation_compliance(&mut suite);
    test_pdu_size_limits(&mut suite);
    test_response_timeout_compliance(&mut suite);

    // Print results
    suite.print_summary();
}
```

### Advanced Compliance Testing with Property-Based Testing

```rust
// Add to Cargo.toml: proptest = "1.0"

#[cfg(test)]
mod compliance_tests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn test_crc_reversibility(data in prop::collection::vec(any::<u8>(), 1..100)) {
            let crc = calculate_crc16(&data);
            
            // CRC should be deterministic
            let crc2 = calculate_crc16(&data);
            assert_eq!(crc, crc2);
        }

        #[test]
        fn test_address_range_validity(addr in 0u16..=0xFFFF) {
            // All 16-bit addresses should be valid
            let mut data = Vec::new();
            data.extend_from_slice(&addr.to_be_bytes());
            data.extend_from_slice(&1u16.to_be_bytes());

            let request = ModbusRequest {
                slave_id: 1,
                function_code: FC_READ_HOLDING_REGISTERS,
                data,
            };

            assert!(request.validate().is_ok());
        }

        #[test]
        fn test_pdu_size_enforcement(data_size in 0usize..300) {
            let data = vec![0u8; data_size];
            let request = ModbusRequest {
                slave_id: 1,
                function_code: FC_READ_HOLDING_REGISTERS,
                data,
            };

            let validation = request.validate();
            if data_size <= 252 { // 253 - 1 (function code)
                assert!(validation.is_ok());
            } else {
                assert!(validation.is_err());
            }
        }
    }
}
```

## Summary

**Protocol Compliance Testing** is essential for ensuring Modbus implementations meet specification requirements and maintain interoperability across vendors and devices. 

**Key takeaways:**

1. **Comprehensive Coverage**: Tests must cover function codes, exception handling, frame formatting, CRC validation, PDU size limits, and timing requirements

2. **Automated Testing**: Systematic test suites enable repeatable validation and regression detection during development

3. **Standards Adherence**: Compliance with Modbus specifications (Application Protocol V1.1b3, TCP/IP spec) prevents integration issues

4. **Performance Validation**: Response time testing ensures systems meet real-time requirements in industrial environments

5. **Implementation Benefits**:
   - **C/C++**: Low-level control, direct hardware access, suitable for embedded systems
   - **Rust**: Memory safety, modern testing frameworks (proptest), zero-cost abstractions

6. **Best Practices**:
   - Test both valid and invalid inputs (boundary conditions, error cases)
   - Validate CRC calculations with known test vectors
   - Verify exception responses match specification
   - Test edge cases (max PDU sizes, address ranges, timing limits)
   - Use property-based testing to discover unexpected failures

Compliance testing is not a one-time activity but an ongoing process throughout development and deployment, ensuring reliable industrial communication networks.