# Profibus Conformance Test Specification

## Detailed Description

The Profibus Conformance Test Specification defines the standardized testing procedures, requirements, and methodologies used to verify that Profibus devices and systems comply with the official Profibus specifications (IEC 61158/61784). Conformance testing ensures interoperability, reliability, and proper implementation of the Profibus protocol across different manufacturers and device types.

### Key Components

**1. Test Scope and Coverage**
- **Protocol Layer Testing**: Verification of Data Link Layer (FDL - Fieldbus Data Link), Application Layer (FMS/DP/PA)
- **Physical Layer Testing**: Electrical characteristics, transmission speed, cable requirements (RS-485, MBP, fiber optic)
- **Timing Requirements**: Token rotation time, slot time, setup time, min_TSDR, max_TSDR
- **Cyclic and Acyclic Communication**: Testing both periodic data exchange and parameter/diagnostic access

**2. Test Categories**

- **Mandatory Tests**: Core protocol functionality that all devices must pass
- **Optional Tests**: Feature-specific tests for advanced capabilities
- **Interoperability Tests**: Multi-vendor device interaction verification
- **Performance Tests**: Throughput, latency, and real-time behavior
- **Robustness Tests**: Error handling, fault tolerance, recovery mechanisms

**3. Testing Phases**

- **Self-Testing**: Manufacturer's internal validation
- **Laboratory Testing**: Accredited test labs using standardized equipment
- **Certification**: Official conformance certification from PROFIBUS & PROFINET International (PI)
- **Field Testing**: Real-world validation in operational environments

**4. Test Documentation**

- **GSD Files**: Generic Station Description validation
- **Test Reports**: Detailed results documenting pass/fail criteria
- **Certification Marks**: Official PI certification labels and registration

## Programming Examples

### C/C++ Implementation - Conformance Test Framework

```c
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Profibus protocol constants for conformance testing
#define PROFIBUS_MIN_TSDR_9600      11  // bits
#define PROFIBUS_MIN_TSDR_19200     11
#define PROFIBUS_MIN_TSDR_187500    11
#define PROFIBUS_MIN_TSDR_500K      11
#define PROFIBUS_MIN_TSDR_1_5M      7
#define PROFIBUS_MIN_TSDR_3M        4
#define PROFIBUS_MIN_TSDR_6M        3
#define PROFIBUS_MIN_TSDR_12M       2

#define PROFIBUS_MAX_TSDR           1023 // bits

// Token rotation time limits
#define MIN_TOKEN_ROTATION_TIME     1000    // microseconds
#define MAX_TOKEN_ROTATION_TIME     16777215 // microseconds

// Conformance test result structure
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_WARNING,
    TEST_RESULT_NOT_APPLICABLE
} TestResult;

typedef struct {
    char test_name[64];
    char test_id[16];
    TestResult result;
    char details[256];
    bool is_mandatory;
} ConformanceTestCase;

typedef struct {
    uint8_t station_address;
    uint16_t ident_number;
    uint8_t baud_rate_support;  // Bitmap of supported rates
    uint16_t min_tsdr;
    uint16_t max_tsdr;
    uint32_t target_rotation_time;
    bool watchdog_supported;
    bool freeze_mode_supported;
    bool sync_mode_supported;
} ProfibusDeviceConfig;

// Conformance test execution context
typedef struct {
    ProfibusDeviceConfig device_config;
    ConformanceTestCase tests[100];
    int test_count;
    int tests_passed;
    int tests_failed;
    int tests_warning;
} ConformanceTestContext;

// Initialize conformance test context
void init_conformance_test_context(ConformanceTestContext *ctx, 
                                   ProfibusDeviceConfig *device) {
    memset(ctx, 0, sizeof(ConformanceTestContext));
    memcpy(&ctx->device_config, device, sizeof(ProfibusDeviceConfig));
}

// Add test case to context
void add_test_case(ConformanceTestContext *ctx, const char *name, 
                   const char *id, bool is_mandatory) {
    if (ctx->test_count >= 100) return;
    
    ConformanceTestCase *test = &ctx->tests[ctx->test_count++];
    strncpy(test->test_name, name, sizeof(test->test_name) - 1);
    strncpy(test->test_id, id, sizeof(test->test_id) - 1);
    test->is_mandatory = is_mandatory;
    test->result = TEST_RESULT_NOT_APPLICABLE;
}

// Test Case 1: Verify station address range
TestResult test_station_address_range(ConformanceTestContext *ctx) {
    uint8_t addr = ctx->device_config.station_address;
    
    if (addr < 0 || addr > 126) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Station address %d is out of valid range (0-126)", addr);
        return TEST_RESULT_FAIL;
    }
    
    snprintf(ctx->tests[ctx->test_count - 1].details, 256,
            "Station address %d is valid", addr);
    return TEST_RESULT_PASS;
}

// Test Case 2: Verify min_TSDR compliance
TestResult test_min_tsdr_compliance(ConformanceTestContext *ctx, 
                                   uint32_t baud_rate) {
    uint16_t expected_min_tsdr;
    
    switch (baud_rate) {
        case 9600:
        case 19200:
        case 187500:
        case 500000:
            expected_min_tsdr = 11;
            break;
        case 1500000:
            expected_min_tsdr = 7;
            break;
        case 3000000:
            expected_min_tsdr = 4;
            break;
        case 6000000:
            expected_min_tsdr = 3;
            break;
        case 12000000:
            expected_min_tsdr = 2;
            break;
        default:
            snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                    "Unsupported baud rate: %u", baud_rate);
            return TEST_RESULT_FAIL;
    }
    
    if (ctx->device_config.min_tsdr < expected_min_tsdr) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "min_TSDR %u is below required %u for %u baud",
                ctx->device_config.min_tsdr, expected_min_tsdr, baud_rate);
        return TEST_RESULT_FAIL;
    }
    
    snprintf(ctx->tests[ctx->test_count - 1].details, 256,
            "min_TSDR %u meets requirement %u for %u baud",
            ctx->device_config.min_tsdr, expected_min_tsdr, baud_rate);
    return TEST_RESULT_PASS;
}

// Test Case 3: Verify token rotation time
TestResult test_token_rotation_time(ConformanceTestContext *ctx) {
    uint32_t ttr = ctx->device_config.target_rotation_time;
    
    if (ttr < MIN_TOKEN_ROTATION_TIME) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Token rotation time %u us is below minimum %u us",
                ttr, MIN_TOKEN_ROTATION_TIME);
        return TEST_RESULT_FAIL;
    }
    
    if (ttr > MAX_TOKEN_ROTATION_TIME) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Token rotation time %u us exceeds maximum %u us",
                ttr, MAX_TOKEN_ROTATION_TIME);
        return TEST_RESULT_FAIL;
    }
    
    snprintf(ctx->tests[ctx->test_count - 1].details, 256,
            "Token rotation time %u us is valid", ttr);
    return TEST_RESULT_PASS;
}

// Test Case 4: Verify watchdog functionality
TestResult test_watchdog_function(ConformanceTestContext *ctx,
                                 bool (*watchdog_trigger_func)(void),
                                 bool (*watchdog_check_func)(void)) {
    if (!ctx->device_config.watchdog_supported) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Watchdog not supported - test skipped");
        return TEST_RESULT_NOT_APPLICABLE;
    }
    
    // Trigger watchdog timeout
    if (!watchdog_trigger_func()) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Failed to trigger watchdog timeout");
        return TEST_RESULT_FAIL;
    }
    
    // Check if watchdog timeout detected
    if (!watchdog_check_func()) {
        snprintf(ctx->tests[ctx->test_count - 1].details, 256,
                "Watchdog timeout not properly detected");
        return TEST_RESULT_FAIL;
    }
    
    snprintf(ctx->tests[ctx->test_count - 1].details, 256,
            "Watchdog function operates correctly");
    return TEST_RESULT_PASS;
}

// Execute all conformance tests
void execute_conformance_tests(ConformanceTestContext *ctx) {
    // Physical layer tests
    add_test_case(ctx, "Station Address Range", "PHY-001", true);
    ctx->tests[ctx->test_count - 1].result = 
        test_station_address_range(ctx);
    
    // Timing tests
    add_test_case(ctx, "min_TSDR Compliance at 1.5 Mbps", "TIM-001", true);
    ctx->tests[ctx->test_count - 1].result = 
        test_min_tsdr_compliance(ctx, 1500000);
    
    add_test_case(ctx, "Token Rotation Time", "TIM-002", true);
    ctx->tests[ctx->test_count - 1].result = 
        test_token_rotation_time(ctx);
    
    // Protocol tests
    add_test_case(ctx, "Watchdog Function", "PRO-001", false);
    ctx->tests[ctx->test_count - 1].result = 
        test_watchdog_function(ctx, NULL, NULL); // Would pass real functions
    
    // Count results
    for (int i = 0; i < ctx->test_count; i++) {
        switch (ctx->tests[i].result) {
            case TEST_RESULT_PASS:
                ctx->tests_passed++;
                break;
            case TEST_RESULT_FAIL:
                ctx->tests_failed++;
                break;
            case TEST_RESULT_WARNING:
                ctx->tests_warning++;
                break;
            default:
                break;
        }
    }
}

// Generate conformance test report
void generate_test_report(ConformanceTestContext *ctx) {
    printf("\n========================================\n");
    printf("PROFIBUS CONFORMANCE TEST REPORT\n");
    printf("========================================\n\n");
    
    printf("Device Information:\n");
    printf("  Station Address: %u\n", ctx->device_config.station_address);
    printf("  Ident Number: 0x%04X\n", ctx->device_config.ident_number);
    printf("  min_TSDR: %u bits\n", ctx->device_config.min_tsdr);
    printf("  Target Rotation Time: %u us\n\n", 
           ctx->device_config.target_rotation_time);
    
    printf("Test Results Summary:\n");
    printf("  Total Tests: %d\n", ctx->test_count);
    printf("  Passed: %d\n", ctx->tests_passed);
    printf("  Failed: %d\n", ctx->tests_failed);
    printf("  Warnings: %d\n\n", ctx->tests_warning);
    
    printf("Detailed Results:\n");
    printf("%-40s %-12s %-10s %s\n", "Test Name", "Test ID", "Result", "Mandatory");
    printf("------------------------------------------------------------"
           "--------------------\n");
    
    for (int i = 0; i < ctx->test_count; i++) {
        const char *result_str;
        switch (ctx->tests[i].result) {
            case TEST_RESULT_PASS: result_str = "PASS"; break;
            case TEST_RESULT_FAIL: result_str = "FAIL"; break;
            case TEST_RESULT_WARNING: result_str = "WARNING"; break;
            default: result_str = "N/A"; break;
        }
        
        printf("%-40s %-12s %-10s %s\n", 
               ctx->tests[i].test_name,
               ctx->tests[i].test_id,
               result_str,
               ctx->tests[i].is_mandatory ? "Yes" : "No");
        
        if (strlen(ctx->tests[i].details) > 0) {
            printf("  Details: %s\n", ctx->tests[i].details);
        }
    }
    
    printf("\n");
    if (ctx->tests_failed == 0) {
        printf("CONFORMANCE STATUS: PASSED\n");
    } else {
        printf("CONFORMANCE STATUS: FAILED\n");
    }
    printf("========================================\n");
}

// Main example usage
int main(void) {
    // Configure device under test
    ProfibusDeviceConfig device = {
        .station_address = 5,
        .ident_number = 0x0815,
        .baud_rate_support = 0xFF,  // All rates supported
        .min_tsdr = 11,
        .max_tsdr = 1023,
        .target_rotation_time = 5000,  // 5 ms
        .watchdog_supported = true,
        .freeze_mode_supported = true,
        .sync_mode_supported = false
    };
    
    // Initialize and execute conformance tests
    ConformanceTestContext test_ctx;
    init_conformance_test_context(&test_ctx, &device);
    execute_conformance_tests(&test_ctx);
    generate_test_report(&test_ctx);
    
    return 0;
}
```

### Rust Implementation - Conformance Test Framework

```rust
use std::fmt;

// Constants for Profibus conformance testing
const PROFIBUS_MIN_TSDR_MAP: [(u32, u16); 8] = [
    (9600, 11),
    (19200, 11),
    (187500, 11),
    (500000, 11),
    (1500000, 7),
    (3000000, 4),
    (6000000, 3),
    (12000000, 2),
];

const MIN_TOKEN_ROTATION_TIME: u32 = 1000; // microseconds
const MAX_TOKEN_ROTATION_TIME: u32 = 16777215; // microseconds

#[derive(Debug, Clone, Copy, PartialEq)]
enum TestResult {
    Pass,
    Fail,
    Warning,
    NotApplicable,
}

impl fmt::Display for TestResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            TestResult::Pass => write!(f, "PASS"),
            TestResult::Fail => write!(f, "FAIL"),
            TestResult::Warning => write!(f, "WARNING"),
            TestResult::NotApplicable => write!(f, "N/A"),
        }
    }
}

#[derive(Debug, Clone)]
struct ConformanceTestCase {
    test_name: String,
    test_id: String,
    result: TestResult,
    details: String,
    is_mandatory: bool,
}

impl ConformanceTestCase {
    fn new(name: &str, id: &str, is_mandatory: bool) -> Self {
        Self {
            test_name: name.to_string(),
            test_id: id.to_string(),
            result: TestResult::NotApplicable,
            details: String::new(),
            is_mandatory,
        }
    }
}

#[derive(Debug, Clone)]
struct ProfibusDeviceConfig {
    station_address: u8,
    ident_number: u16,
    baud_rate_support: u8,
    min_tsdr: u16,
    max_tsdr: u16,
    target_rotation_time: u32,
    watchdog_supported: bool,
    freeze_mode_supported: bool,
    sync_mode_supported: bool,
}

struct ConformanceTestContext {
    device_config: ProfibusDeviceConfig,
    tests: Vec<ConformanceTestCase>,
    tests_passed: usize,
    tests_failed: usize,
    tests_warning: usize,
}

impl ConformanceTestContext {
    fn new(device_config: ProfibusDeviceConfig) -> Self {
        Self {
            device_config,
            tests: Vec::new(),
            tests_passed: 0,
            tests_failed: 0,
            tests_warning: 0,
        }
    }

    fn add_test_case(&mut self, name: &str, id: &str, is_mandatory: bool) {
        self.tests.push(ConformanceTestCase::new(name, id, is_mandatory));
    }

    // Test Case 1: Station address range validation
    fn test_station_address_range(&mut self) -> TestResult {
        let addr = self.device_config.station_address;
        let idx = self.tests.len() - 1;
        
        if addr > 126 {
            self.tests[idx].details = 
                format!("Station address {} is out of valid range (0-126)", addr);
            return TestResult::Fail;
        }
        
        self.tests[idx].details = format!("Station address {} is valid", addr);
        TestResult::Pass
    }

    // Test Case 2: min_TSDR compliance verification
    fn test_min_tsdr_compliance(&mut self, baud_rate: u32) -> TestResult {
        let idx = self.tests.len() - 1;
        
        let expected_min_tsdr = PROFIBUS_MIN_TSDR_MAP
            .iter()
            .find(|(rate, _)| *rate == baud_rate)
            .map(|(_, tsdr)| *tsdr);
        
        let expected = match expected_min_tsdr {
            Some(val) => val,
            None => {
                self.tests[idx].details = 
                    format!("Unsupported baud rate: {}", baud_rate);
                return TestResult::Fail;
            }
        };
        
        if self.device_config.min_tsdr < expected {
            self.tests[idx].details = format!(
                "min_TSDR {} is below required {} for {} baud",
                self.device_config.min_tsdr, expected, baud_rate
            );
            return TestResult::Fail;
        }
        
        self.tests[idx].details = format!(
            "min_TSDR {} meets requirement {} for {} baud",
            self.device_config.min_tsdr, expected, baud_rate
        );
        TestResult::Pass
    }

    // Test Case 3: Token rotation time validation
    fn test_token_rotation_time(&mut self) -> TestResult {
        let ttr = self.device_config.target_rotation_time;
        let idx = self.tests.len() - 1;
        
        if ttr < MIN_TOKEN_ROTATION_TIME {
            self.tests[idx].details = format!(
                "Token rotation time {} µs is below minimum {} µs",
                ttr, MIN_TOKEN_ROTATION_TIME
            );
            return TestResult::Fail;
        }
        
        if ttr > MAX_TOKEN_ROTATION_TIME {
            self.tests[idx].details = format!(
                "Token rotation time {} µs exceeds maximum {} µs",
                ttr, MAX_TOKEN_ROTATION_TIME
            );
            return TestResult::Fail;
        }
        
        self.tests[idx].details = format!("Token rotation time {} µs is valid", ttr);
        TestResult::Pass
    }

    // Test Case 4: Cyclic data exchange test
    fn test_cyclic_data_exchange(&mut self) -> TestResult {
        let idx = self.tests.len() - 1;
        
        // Simulate cyclic data exchange test
        // In real implementation, this would communicate with actual device
        let cycles_tested = 1000;
        let cycles_successful = 1000;
        let success_rate = (cycles_successful as f64 / cycles_tested as f64) * 100.0;
        
        if success_rate < 99.9 {
            self.tests[idx].details = format!(
                "Cyclic data exchange success rate {:.2}% is below 99.9%",
                success_rate
            );
            return TestResult::Fail;
        }
        
        self.tests[idx].details = format!(
            "Cyclic data exchange: {}/{} cycles successful ({:.2}%)",
            cycles_successful, cycles_tested, success_rate
        );
        TestResult::Pass
    }

    // Test Case 5: GSD file validation
    fn test_gsd_file_validation(&mut self, gsd_content: &str) -> TestResult {
        let idx = self.tests.len() - 1;
        
        // Check for mandatory GSD keywords
        let mandatory_keywords = vec![
            "GSD_Revision",
            "Vendor_Name",
            "Model_Name",
            "Revision",
            "Ident_Number",
            "Protocol_Ident",
            "Station_Type",
        ];
        
        let mut missing_keywords = Vec::new();
        for keyword in mandatory_keywords {
            if !gsd_content.contains(keyword) {
                missing_keywords.push(keyword);
            }
        }
        
        if !missing_keywords.is_empty() {
            self.tests[idx].details = format!(
                "GSD file missing mandatory keywords: {:?}",
                missing_keywords
            );
            return TestResult::Fail;
        }
        
        self.tests[idx].details = "GSD file contains all mandatory keywords".to_string();
        TestResult::Pass
    }

    // Execute all conformance tests
    fn execute_conformance_tests(&mut self) {
        // Physical layer tests
        self.add_test_case("Station Address Range", "PHY-001", true);
        let result = self.test_station_address_range();
        let idx = self.tests.len() - 1;
        self.tests[idx].result = result;

        // Timing tests
        self.add_test_case("min_TSDR Compliance at 1.5 Mbps", "TIM-001", true);
        let result = self.test_min_tsdr_compliance(1500000);
        let idx = self.tests.len() - 1;
        self.tests[idx].result = result;

        self.add_test_case("Token Rotation Time", "TIM-002", true);
        let result = self.test_token_rotation_time();
        let idx = self.tests.len() - 1;
        self.tests[idx].result = result;

        // Protocol tests
        self.add_test_case("Cyclic Data Exchange", "PRO-001", true);
        let result = self.test_cyclic_data_exchange();
        let idx = self.tests.len() - 1;
        self.tests[idx].result = result;

        // GSD validation
        self.add_test_case("GSD File Validation", "GSD-001", true);
        let gsd_sample = "GSD_Revision = 5\nVendor_Name = \"Test\"\nModel_Name = \"Device\"\n\
                          Revision = \"V1.0\"\nIdent_Number = 0x0815\nProtocol_Ident = 0\n\
                          Station_Type = 0\n";
        let result = self.test_gsd_file_validation(gsd_sample);
        let idx = self.tests.len() - 1;
        self.tests[idx].result = result;

        // Count results
        self.tests_passed = 0;
        self.tests_failed = 0;
        self.tests_warning = 0;

        for test in &self.tests {
            match test.result {
                TestResult::Pass => self.tests_passed += 1,
                TestResult::Fail => self.tests_failed += 1,
                TestResult::Warning => self.tests_warning += 1,
                _ => {}
            }
        }
    }

    // Generate conformance test report
    fn generate_test_report(&self) {
        println!("\n========================================");
        println!("PROFIBUS CONFORMANCE TEST REPORT");
        println!("========================================\n");

        println!("Device Information:");
        println!("  Station Address: {}", self.device_config.station_address);
        println!("  Ident Number: 0x{:04X}", self.device_config.ident_number);
        println!("  min_TSDR: {} bits", self.device_config.min_tsdr);
        println!("  Target Rotation Time: {} µs\n", 
                 self.device_config.target_rotation_time);

        println!("Test Results Summary:");
        println!("  Total Tests: {}", self.tests.len());
        println!("  Passed: {}", self.tests_passed);
        println!("  Failed: {}", self.tests_failed);
        println!("  Warnings: {}\n", self.tests_warning);

        println!("Detailed Results:");
        println!("{:<40} {:<12} {:<10} {}", 
                 "Test Name", "Test ID", "Result", "Mandatory");
        println!("{}", "-".repeat(80));

        for test in &self.tests {
            println!("{:<40} {:<12} {:<10} {}", 
                     test.test_name,
                     test.test_id,
                     test.result,
                     if test.is_mandatory { "Yes" } else { "No" });
            
            if !test.details.is_empty() {
                println!("  Details: {}", test.details);
            }
        }

        println!();
        if self.tests_failed == 0 {
            println!("CONFORMANCE STATUS: PASSED");
        } else {
            println!("CONFORMANCE STATUS: FAILED");
        }
        println!("========================================");
    }
}

fn main() {
    // Configure device under test
    let device = ProfibusDeviceConfig {
        station_address: 5,
        ident_number: 0x0815,
        baud_rate_support: 0xFF,
        min_tsdr: 11,
        max_tsdr: 1023,
        target_rotation_time: 5000,
        watchdog_supported: true,
        freeze_mode_supported: true,
        sync_mode_supported: false,
    };

    // Initialize and execute conformance tests
    let mut test_ctx = ConformanceTestContext::new(device);
    test_ctx.execute_conformance_tests();
    test_ctx.generate_test_report();
}
```

## Summary

**Profibus Conformance Test Specification** establishes the comprehensive framework for validating that Profibus devices meet official protocol standards and ensure multi-vendor interoperability. The specification encompasses:

- **Protocol Compliance**: Verification of proper implementation across all protocol layers (physical, data link, application)
- **Timing Accuracy**: Validation of critical timing parameters like min_TSDR, token rotation time, and slot time
- **Functional Testing**: Assessment of mandatory features (cyclic data exchange, diagnostics) and optional capabilities (freeze/sync modes, redundancy)
- **Documentation Validation**: GSD file verification and test report generation
- **Certification Process**: Multi-stage validation from self-testing through official PI certification

The code examples demonstrate practical implementations of conformance test frameworks in both C/C++ and Rust, including test case execution, result tracking, and automated report generation. These frameworks validate critical parameters such as station addressing, timing requirements, protocol behavior, and GSD file correctness.

Conformance testing is essential for ensuring reliable, interoperable Profibus networks by guaranteeing that all devices adhere to the same rigorous standards regardless of manufacturer, ultimately reducing integration issues and improving system reliability in industrial automation environments.