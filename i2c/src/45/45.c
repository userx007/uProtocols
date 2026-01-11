#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// I2C timing parameters (in microseconds) for different modes
typedef struct {
    uint32_t scl_low_min;      // Minimum SCL low period
    uint32_t scl_high_min;     // Minimum SCL high period
    uint32_t start_setup_min;  // START condition setup time
    uint32_t start_hold_min;   // START condition hold time
    uint32_t data_setup_min;   // Data setup time
    uint32_t data_hold_min;    // Data hold time (min 0 for most modes)
    uint32_t stop_setup_min;   // STOP condition setup time
    uint32_t bus_free_min;     // Bus free time between transactions
} I2C_TimingSpec;

// Standard mode (100 kHz)
static const I2C_TimingSpec STANDARD_MODE = {
    .scl_low_min = 4700,      // 4.7 μs
    .scl_high_min = 4000,     // 4.0 μs
    .start_setup_min = 4700,  // 4.7 μs
    .start_hold_min = 4000,   // 4.0 μs
    .data_setup_min = 250,    // 250 ns
    .data_hold_min = 0,       // 0 ns (max 3450 ns)
    .stop_setup_min = 4000,   // 4.0 μs
    .bus_free_min = 4700      // 4.7 μs
};

// Fast mode (400 kHz)
static const I2C_TimingSpec FAST_MODE = {
    .scl_low_min = 1300,      // 1.3 μs
    .scl_high_min = 600,      // 0.6 μs
    .start_setup_min = 600,   // 0.6 μs
    .start_hold_min = 600,    // 0.6 μs
    .data_setup_min = 100,    // 100 ns
    .data_hold_min = 0,       // 0 ns (max 900 ns)
    .stop_setup_min = 600,    // 0.6 μs
    .bus_free_min = 1300      // 1.3 μs
};

// Verification result structure
typedef struct {
    bool passed;
    char error_msg[256];
    uint32_t measured_value;
    uint32_t expected_min;
} VerificationResult;

// Bus state enumeration
typedef enum {
    BUS_IDLE,
    BUS_START,
    BUS_ADDRESSING,
    BUS_DATA_TX,
    BUS_DATA_RX,
    BUS_ACK,
    BUS_NACK,
    BUS_STOP,
    BUS_ERROR
} I2C_BusState;

// Protocol verification context
typedef struct {
    I2C_BusState current_state;
    I2C_TimingSpec timing_spec;
    uint32_t error_count;
    uint32_t transaction_count;
    bool clock_stretching_detected;
    bool arbitration_loss_detected;
} I2C_ProtocolVerifier;

// Initialize protocol verifier
void i2c_verifier_init(I2C_ProtocolVerifier* verifier, const I2C_TimingSpec* spec) {
    verifier->current_state = BUS_IDLE;
    verifier->timing_spec = *spec;
    verifier->error_count = 0;
    verifier->transaction_count = 0;
    verifier->clock_stretching_detected = false;
    verifier->arbitration_loss_detected = false;
}

// Simulate timing measurement (in real implementation, use hardware timers)
uint32_t measure_timing_us(void) {
    // In real implementation, this would use hardware timers or logic analyzer
    // For demonstration, return simulated values
    return 5000; // 5 ms simulated
}

// Verify START condition timing
VerificationResult verify_start_condition(I2C_ProtocolVerifier* verifier, 
                                         uint32_t setup_time_ns,
                                         uint32_t hold_time_ns) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    // Verify setup time (SDA falling edge while SCL high)
    if (setup_time_ns < verifier->timing_spec.start_setup_min) {
        result.passed = false;
        result.measured_value = setup_time_ns;
        result.expected_min = verifier->timing_spec.start_setup_min;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "START setup time violation: %u ns < %u ns (min)",
                setup_time_ns, verifier->timing_spec.start_setup_min);
        verifier->error_count++;
        return result;
    }
    
    // Verify hold time (SCL remains low after START)
    if (hold_time_ns < verifier->timing_spec.start_hold_min) {
        result.passed = false;
        result.measured_value = hold_time_ns;
        result.expected_min = verifier->timing_spec.start_hold_min;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "START hold time violation: %u ns < %u ns (min)",
                hold_time_ns, verifier->timing_spec.start_hold_min);
        verifier->error_count++;
        return result;
    }
    
    verifier->current_state = BUS_START;
    return result;
}

// Verify STOP condition timing
VerificationResult verify_stop_condition(I2C_ProtocolVerifier* verifier,
                                        uint32_t setup_time_ns) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    // Verify setup time (SDA rising edge while SCL high)
    if (setup_time_ns < verifier->timing_spec.stop_setup_min) {
        result.passed = false;
        result.measured_value = setup_time_ns;
        result.expected_min = verifier->timing_spec.stop_setup_min;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "STOP setup time violation: %u ns < %u ns (min)",
                setup_time_ns, verifier->timing_spec.stop_setup_min);
        verifier->error_count++;
        return result;
    }
    
    verifier->current_state = BUS_STOP;
    verifier->transaction_count++;
    return result;
}

// Verify data bit timing
VerificationResult verify_data_timing(I2C_ProtocolVerifier* verifier,
                                     uint32_t setup_time_ns,
                                     uint32_t hold_time_ns,
                                     uint32_t scl_low_ns,
                                     uint32_t scl_high_ns) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    // Verify data setup time (SDA stable before SCL rising)
    if (setup_time_ns < verifier->timing_spec.data_setup_min) {
        result.passed = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "Data setup time violation: %u ns < %u ns (min)",
                setup_time_ns, verifier->timing_spec.data_setup_min);
        verifier->error_count++;
        return result;
    }
    
    // Verify SCL low period
    if (scl_low_ns < verifier->timing_spec.scl_low_min) {
        result.passed = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "SCL low period violation: %u ns < %u ns (min)",
                scl_low_ns, verifier->timing_spec.scl_low_min);
        verifier->error_count++;
        return result;
    }
    
    // Verify SCL high period
    if (scl_high_ns < verifier->timing_spec.scl_high_min) {
        result.passed = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "SCL high period violation: %u ns < %u ns (min)",
                scl_high_ns, verifier->timing_spec.scl_high_min);
        verifier->error_count++;
        return result;
    }
    
    return result;
}

// Verify bus free time between transactions
VerificationResult verify_bus_free_time(I2C_ProtocolVerifier* verifier,
                                       uint32_t free_time_ns) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    if (free_time_ns < verifier->timing_spec.bus_free_min) {
        result.passed = false;
        result.measured_value = free_time_ns;
        result.expected_min = verifier->timing_spec.bus_free_min;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "Bus free time violation: %u ns < %u ns (min)",
                free_time_ns, verifier->timing_spec.bus_free_min);
        verifier->error_count++;
        return result;
    }
    
    return result;
}

// Verify address frame (7-bit or 10-bit)
VerificationResult verify_address_frame(I2C_ProtocolVerifier* verifier,
                                       uint8_t address,
                                       bool is_10bit,
                                       bool read_mode,
                                       bool ack_received) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    // Verify 7-bit address range
    if (!is_10bit && (address & 0x80)) {
        result.passed = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "Invalid 7-bit address: 0x%02X (bit 7 should be 0)", address);
        verifier->error_count++;
        return result;
    }
    
    // Check for reserved addresses (0x00-0x07, 0x78-0x7F for 7-bit)
    if (!is_10bit) {
        uint8_t addr_7bit = address >> 1;
        if (addr_7bit <= 0x07 || addr_7bit >= 0x78) {
            result.passed = false;
            snprintf(result.error_msg, sizeof(result.error_msg),
                    "Reserved address used: 0x%02X", addr_7bit);
            verifier->error_count++;
            return result;
        }
    }
    
    // Verify ACK was received after address
    if (!ack_received) {
        result.passed = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "No ACK received for address 0x%02X", address);
        verifier->error_count++;
        return result;
    }
    
    verifier->current_state = read_mode ? BUS_DATA_RX : BUS_DATA_TX;
    return result;
}

// Verify clock stretching compliance
VerificationResult verify_clock_stretching(I2C_ProtocolVerifier* verifier,
                                          uint32_t stretch_duration_us,
                                          uint32_t max_allowed_us) {
    VerificationResult result = {.passed = true, .error_msg = {0}};
    
    verifier->clock_stretching_detected = true;
    
    if (stretch_duration_us > max_allowed_us) {
        result.passed = false;
        result.measured_value = stretch_duration_us;
        result.expected_min = max_allowed_us;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "Clock stretching timeout: %u μs > %u μs (max)",
                stretch_duration_us, max_allowed_us);
        verifier->error_count++;
    }
    
    return result;
}

// Generate verification report
void generate_verification_report(const I2C_ProtocolVerifier* verifier) {
    printf("\n=== I2C Protocol Verification Report ===\n");
    printf("Total Transactions: %u\n", verifier->transaction_count);
    printf("Total Errors: %u\n", verifier->error_count);
    printf("Clock Stretching Detected: %s\n", 
           verifier->clock_stretching_detected ? "Yes" : "No");
    printf("Arbitration Loss Detected: %s\n",
           verifier->arbitration_loss_detected ? "Yes" : "No");
    printf("Current Bus State: ");
    
    switch(verifier->current_state) {
        case BUS_IDLE: printf("IDLE\n"); break;
        case BUS_START: printf("START\n"); break;
        case BUS_STOP: printf("STOP\n"); break;
        case BUS_ERROR: printf("ERROR\n"); break;
        default: printf("ACTIVE\n"); break;
    }
    
    if (verifier->error_count == 0) {
        printf("\n✓ All protocol verification tests PASSED\n");
    } else {
        printf("\n✗ Protocol verification FAILED with %u error(s)\n", 
               verifier->error_count);
    }
    printf("========================================\n\n");
}

// Example usage and test harness
int main(void) {
    printf("I2C Protocol Verification System\n\n");
    
    // Initialize verifier for Fast Mode (400 kHz)
    I2C_ProtocolVerifier verifier;
    i2c_verifier_init(&verifier, &FAST_MODE);
    
    printf("Testing I2C Fast Mode (400 kHz) Protocol Compliance\n");
    printf("------------------------------------------------\n\n");
    
    // Test 1: Verify START condition
    printf("Test 1: START Condition Timing\n");
    VerificationResult result = verify_start_condition(&verifier, 650, 700);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Test 2: Verify address frame
    printf("Test 2: Address Frame (0x50, Write, ACK)\n");
    result = verify_address_frame(&verifier, 0xA0, false, false, true);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Test 3: Verify data timing
    printf("Test 3: Data Bit Timing\n");
    result = verify_data_timing(&verifier, 150, 50, 1400, 700);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Test 4: Verify STOP condition
    printf("Test 4: STOP Condition Timing\n");
    result = verify_stop_condition(&verifier, 650);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Test 5: Verify bus free time
    printf("Test 5: Bus Free Time Between Transactions\n");
    result = verify_bus_free_time(&verifier, 1500);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Test 6: Clock stretching
    printf("Test 6: Clock Stretching (within limits)\n");
    result = verify_clock_stretching(&verifier, 500, 1000);
    printf("  Result: %s\n", result.passed ? "PASS" : "FAIL");
    if (!result.passed) printf("  Error: %s\n", result.error_msg);
    printf("\n");
    
    // Generate final report
    generate_verification_report(&verifier);
    
    return verifier.error_count > 0 ? 1 : 0;
}