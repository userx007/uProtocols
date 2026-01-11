# I2C Protocol Verification

Protocol verification is a critical aspect of I2C implementation that ensures your communication adheres to the I2C specification. This involves systematic testing of timing parameters, bus conditions, and error handling to guarantee reliable operation across different devices and conditions.

## Overview of I2C Protocol Verification

Protocol verification encompasses several key areas:

1. **Timing Verification** - Ensuring SCL/SDA timing meets specification
2. **Bus State Verification** - Validating START, STOP, and bus idle conditions
3. **Addressing Verification** - Confirming correct address transmission and ACK/NACK
4. **Data Integrity** - Verifying data transmission without corruption
5. **Error Condition Testing** - Testing clock stretching, arbitration, and bus recovery

## Key I2C Timing Parameters

The I2C specification defines critical timing parameters that must be verified:

- **Standard Mode (100 kHz)**: SCL high/low times, setup/hold times
- **Fast Mode (400 kHz)**: Tighter timing requirements
- **Fast Mode Plus (1 MHz)**: Even stricter timing constraints
- **Setup and Hold Times**: For START, STOP, and data bits

## C/C++ Implementation

Here's a comprehensive C implementation for I2C protocol verification:

```c
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
```

## Rust Implementation

Here's a comprehensive Rust implementation with type safety and modern error handling:

```rs
use std::fmt;
use std::time::Duration;

/// I2C timing specifications for different speed modes
#[derive(Debug, Clone, Copy)]
pub struct TimingSpec {
    pub scl_low_min: Duration,
    pub scl_high_min: Duration,
    pub start_setup_min: Duration,
    pub start_hold_min: Duration,
    pub data_setup_min: Duration,
    pub data_hold_min: Duration,
    pub stop_setup_min: Duration,
    pub bus_free_min: Duration,
}

impl TimingSpec {
    /// Standard mode (100 kHz)
    pub const STANDARD_MODE: Self = Self {
        scl_low_min: Duration::from_micros(4700),
        scl_high_min: Duration::from_micros(4000),
        start_setup_min: Duration::from_micros(4700),
        start_hold_min: Duration::from_micros(4000),
        data_setup_min: Duration::from_nanos(250),
        data_hold_min: Duration::from_nanos(0),
        stop_setup_min: Duration::from_micros(4000),
        bus_free_min: Duration::from_micros(4700),
    };

    /// Fast mode (400 kHz)
    pub const FAST_MODE: Self = Self {
        scl_low_min: Duration::from_nanos(1300),
        scl_high_min: Duration::from_nanos(600),
        start_setup_min: Duration::from_nanos(600),
        start_hold_min: Duration::from_nanos(600),
        data_setup_min: Duration::from_nanos(100),
        data_hold_min: Duration::from_nanos(0),
        stop_setup_min: Duration::from_nanos(600),
        bus_free_min: Duration::from_nanos(1300),
    };

    /// Fast mode plus (1 MHz)
    pub const FAST_MODE_PLUS: Self = Self {
        scl_low_min: Duration::from_nanos(500),
        scl_high_min: Duration::from_nanos(260),
        start_setup_min: Duration::from_nanos(260),
        start_hold_min: Duration::from_nanos(260),
        data_setup_min: Duration::from_nanos(50),
        data_hold_min: Duration::from_nanos(0),
        stop_setup_min: Duration::from_nanos(260),
        bus_free_min: Duration::from_nanos(500),
    };
}

/// Bus state enumeration
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusState {
    Idle,
    Start,
    Addressing,
    DataTx,
    DataRx,
    Ack,
    Nack,
    Stop,
    Error,
}

impl fmt::Display for BusState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BusState::Idle => write!(f, "IDLE"),
            BusState::Start => write!(f, "START"),
            BusState::Addressing => write!(f, "ADDRESSING"),
            BusState::DataTx => write!(f, "DATA_TX"),
            BusState::DataRx => write!(f, "DATA_RX"),
            BusState::Ack => write!(f, "ACK"),
            BusState::Nack => write!(f, "NACK"),
            BusState::Stop => write!(f, "STOP"),
            BusState::Error => write!(f, "ERROR"),
        }
    }
}

/// Verification error types
#[derive(Debug, Clone)]
pub enum VerificationError {
    TimingViolation {
        parameter: String,
        measured: Duration,
        expected_min: Duration,
    },
    InvalidAddress {
        address: u16,
        reason: String,
    },
    NoAcknowledge {
        address: u16,
    },
    ClockStretchTimeout {
        duration: Duration,
        max_allowed: Duration,
    },
    ArbitrationLost,
    BusError(String),
}

impl fmt::Display for VerificationError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            VerificationError::TimingViolation { parameter, measured, expected_min } => {
                write!(f, "{} timing violation: {:?} < {:?} (min)", 
                       parameter, measured, expected_min)
            }
            VerificationError::InvalidAddress { address, reason } => {
                write!(f, "Invalid address 0x{:X}: {}", address, reason)
            }
            VerificationError::NoAcknowledge { address } => {
                write!(f, "No ACK received for address 0x{:X}", address)
            }
            VerificationError::ClockStretchTimeout { duration, max_allowed } => {
                write!(f, "Clock stretch timeout: {:?} > {:?} (max)", 
                       duration, max_allowed)
            }
            VerificationError::ArbitrationLost => {
                write!(f, "Arbitration lost on bus")
            }
            VerificationError::BusError(msg) => {
                write!(f, "Bus error: {}", msg)
            }
        }
    }
}

impl std::error::Error for VerificationError {}

pub type VerificationResult<T> = Result<T, VerificationError>;

/// Protocol verification statistics
#[derive(Debug, Default)]
pub struct VerificationStats {
    pub transaction_count: u32,
    pub error_count: u32,
    pub clock_stretching_count: u32,
    pub arbitration_loss_count: u32,
}

/// I2C Protocol Verifier
pub struct ProtocolVerifier {
    timing_spec: TimingSpec,
    current_state: BusState,
    stats: VerificationStats,
    errors: Vec<VerificationError>,
}

impl ProtocolVerifier {
    /// Create a new protocol verifier with specified timing spec
    pub fn new(timing_spec: TimingSpec) -> Self {
        Self {
            timing_spec,
            current_state: BusState::Idle,
            stats: VerificationStats::default(),
            errors: Vec::new(),
        }
    }

    /// Get current bus state
    pub fn state(&self) -> BusState {
        self.current_state
    }

    /// Get verification statistics
    pub fn stats(&self) -> &VerificationStats {
        &self.stats
    }

    /// Get all errors encountered
    pub fn errors(&self) -> &[VerificationError] {
        &self.errors
    }

    /// Verify START condition timing
    pub fn verify_start_condition(
        &mut self,
        setup_time: Duration,
        hold_time: Duration,
    ) -> VerificationResult<()> {
        // Verify setup time (SDA falling while SCL high)
        if setup_time < self.timing_spec.start_setup_min {
            let err = VerificationError::TimingViolation {
                parameter: "START setup".to_string(),
                measured: setup_time,
                expected_min: self.timing_spec.start_setup_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        // Verify hold time (SCL remains low after START)
        if hold_time < self.timing_spec.start_hold_min {
            let err = VerificationError::TimingViolation {
                parameter: "START hold".to_string(),
                measured: hold_time,
                expected_min: self.timing_spec.start_hold_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        self.current_state = BusState::Start;
        Ok(())
    }

    /// Verify STOP condition timing
    pub fn verify_stop_condition(&mut self, setup_time: Duration) -> VerificationResult<()> {
        // Verify setup time (SDA rising while SCL high)
        if setup_time < self.timing_spec.stop_setup_min {
            let err = VerificationError::TimingViolation {
                parameter: "STOP setup".to_string(),
                measured: setup_time,
                expected_min: self.timing_spec.stop_setup_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        self.current_state = BusState::Stop;
        self.stats.transaction_count += 1;
        Ok(())
    }

    /// Verify data bit timing
    pub fn verify_data_timing(
        &mut self,
        setup_time: Duration,
        _hold_time: Duration,
        scl_low: Duration,
        scl_high: Duration,
    ) -> VerificationResult<()> {
        // Verify data setup time
        if setup_time < self.timing_spec.data_setup_min {
            let err = VerificationError::TimingViolation {
                parameter: "Data setup".to_string(),
                measured: setup_time,
                expected_min: self.timing_spec.data_setup_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        // Verify SCL low period
        if scl_low < self.timing_spec.scl_low_min {
            let err = VerificationError::TimingViolation {
                parameter: "SCL low".to_string(),
                measured: scl_low,
                expected_min: self.timing_spec.scl_low_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        // Verify SCL high period
        if scl_high < self.timing_spec.scl_high_min {
            let err = VerificationError::TimingViolation {
                parameter: "SCL high".to_string(),
                measured: scl_high,
                expected_min: self.timing_spec.scl_high_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        Ok(())
    }

    /// Verify bus free time between transactions
    pub fn verify_bus_free_time(&mut self, free_time: Duration) -> VerificationResult<()> {
        if free_time < self.timing_spec.bus_free_min {
            let err = VerificationError::TimingViolation {
                parameter: "Bus free".to_string(),
                measured: free_time,
                expected_min: self.timing_spec.bus_free_min,
            };
            self.record_error(err.clone());
            return Err(err);
        }
        Ok(())
    }

    /// Verify address frame
    pub fn verify_address_frame(
        &mut self,
        address: u16,
        is_10bit: bool,
        read_mode: bool,
        ack_received: bool,
    ) -> VerificationResult<()> {
        // Validate 7-bit address
        if !is_10bit && address > 0x7F {
            let err = VerificationError::InvalidAddress {
                address,
                reason: "7-bit address exceeds 0x7F".to_string(),
            };
            self.record_error(err.clone());
            return Err(err);
        }

        // Check for reserved addresses in 7-bit mode
        if !is_10bit {
            let addr_7bit = (address >> 1) as u8;
            if addr_7bit <= 0x07 || addr_7bit >= 0x78 {
                let err = VerificationError::InvalidAddress {
                    address,
                    reason: format!("Reserved address 0x{:02X}", addr_7bit),
                };
                self.record_error(err.clone());
                return Err(err);
            }
        }

        // Verify ACK received
        if !ack_received {
            let err = VerificationError::NoAcknowledge { address };
            self.record_error(err.clone());
            return Err(err);
        }

        self.current_state = if read_mode {
            BusState::DataRx
        } else {
            BusState::DataTx
        };

        Ok(())
    }

    /// Verify clock stretching compliance
    pub fn verify_clock_stretching(
        &mut self,
        stretch_duration: Duration,
        max_allowed: Duration,
    ) -> VerificationResult<()> {
        self.stats.clock_stretching_count += 1;

        if stretch_duration > max_allowed {
            let err = VerificationError::ClockStretchTimeout {
                duration: stretch_duration,
                max_allowed,
            };
            self.record_error(err.clone());
            return Err(err);
        }

        Ok(())
    }

    /// Record arbitration loss
    pub fn record_arbitration_loss(&mut self) {
        self.stats.arbitration_loss_count += 1;
        self.current_state = BusState::Error;
        let err = VerificationError::ArbitrationLost;
        self.record_error(err);
    }

    /// Record an error internally
    fn record_error(&mut self, error: VerificationError) {
        self.stats.error_count += 1;
        self.errors.push(error);
    }

    /// Generate verification report
    pub fn generate_report(&self) -> String {
        let mut report = String::new();
        
        report.push_str("\n=== I2C Protocol Verification Report ===\n");
        report.push_str(&format!("Total Transactions: {}\n", self.stats.transaction_count));
        report.push_str(&format!("Total Errors: {}\n", self.stats.error_count));
        report.push_str(&format!("Clock Stretching Events: {}\n", 
                                self.stats.clock_stretching_count));
        report.push_str(&format!("Arbitration Losses: {}\n", 
                                self.stats.arbitration_loss_count));
        report.push_str(&format!("Current Bus State: {}\n", self.current_state));
        
        if !self.errors.is_empty() {
            report.push_str("\nErrors Encountered:\n");
            for (idx, error) in self.errors.iter().enumerate() {
                report.push_str(&format!("  {}. {}\n", idx + 1, error));
            }
        }
        
        if self.stats.error_count == 0 {
            report.push_str("\n✓ All protocol verification tests PASSED\n");
        } else {
            report.push_str(&format!("\n✗ Protocol verification FAILED with {} error(s)\n", 
                                   self.stats.error_count));
        }
        
        report.push_str("========================================\n");
        report
    }

    /// Reset verifier state for new test session
    pub fn reset(&mut self) {
        self.current_state = BusState::Idle;
        self.stats = VerificationStats::default();
        self.errors.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_start_condition_valid() {
        let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);
        let result = verifier.verify_start_condition(
            Duration::from_nanos(650),
            Duration::from_nanos(700),
        );
        assert!(result.is_ok());
        assert_eq!(verifier.state(), BusState::Start);
    }

    #[test]
    fn test_start_condition_setup_violation() {
        let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);
        let result = verifier.verify_start_condition(
            Duration::from_nanos(500), // Too short
            Duration::from_nanos(700),
        );
        assert!(result.is_err());
        assert_eq!(verifier.stats().error_count, 1);
    }

    #[test]
    fn test_address_frame_valid() {
        let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);
        let result = verifier.verify_address_frame(0x50, false, false, true);
        assert!(result.is_ok());
        assert_eq!(verifier.state(), BusState::DataTx);
    }

    #[test]
    fn test_address_frame_no_ack() {
        let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);
        let result = verifier.verify_address_frame(0x50, false, false, false);
        assert!(result.is_err());
    }

    #[test]
    fn test_clock_stretching() {
        let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);
        let result = verifier.verify_clock_stretching(
            Duration::from_micros(500),
            Duration::from_micros(1000),
        );
        assert!(result.is_ok());
        assert_eq!(verifier.stats().clock_stretching_count, 1);
    }
}

fn main() {
    println!("I2C Protocol Verification System\n");

    // Initialize verifier for Fast Mode (400 kHz)
    let mut verifier = ProtocolVerifier::new(TimingSpec::FAST_MODE);

    println!("Testing I2C Fast Mode (400 kHz) Protocol Compliance");
    println!("--------------------------------------------------\n");

    // Test 1: START condition
    println!("Test 1: START Condition Timing");
    match verifier.verify_start_condition(
        Duration::from_nanos(650),
        Duration::from_nanos(700),
    ) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Test 2: Address frame
    println!("Test 2: Address Frame (0x50, Write, ACK)");
    match verifier.verify_address_frame(0xA0, false, false, true) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Test 3: Data timing
    println!("Test 3: Data Bit Timing");
    match verifier.verify_data_timing(
        Duration::from_nanos(150),
        Duration::from_nanos(50),
        Duration::from_nanos(1400),
        Duration::from_nanos(700),
    ) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Test 4: STOP condition
    println!("Test 4: STOP Condition Timing");
    match verifier.verify_stop_condition(Duration::from_nanos(650)) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Test 5: Bus free time
    println!("Test 5: Bus Free Time Between Transactions");
    match verifier.verify_bus_free_time(Duration::from_nanos(1500)) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Test 6: Clock stretching
    println!("Test 6: Clock Stretching (within limits)");
    match verifier.verify_clock_stretching(
        Duration::from_micros(500),
        Duration::from_micros(1000),
    ) {
        Ok(_) => println!("  Result: PASS"),
        Err(e) => println!("  Result: FAIL - {}", e),
    }
    println!();

    // Generate final report
    print!("{}", verifier.generate_report());
}
```

## Key Concepts Explained

### 1. **Timing Parameter Verification**
The most critical aspect of I2C protocol verification is ensuring all timing parameters meet the specification. This includes:

- **SCL Clock Timing**: Both high and low periods must meet minimum durations
- **Setup Times**: Data and control signals must be stable before clock edges
- **Hold Times**: Signals must remain stable after clock edges
- **Bus Free Time**: Adequate time between transactions for bus to settle

### 2. **Address Frame Validation**
Proper address handling is essential:

- **7-bit addressing**: Most common, addresses 0x08-0x77 (0x00-0x07 and 0x78-0x7F are reserved)
- **10-bit addressing**: Extended addressing for more devices on the bus
- **R/W bit verification**: Ensuring correct read/write indication
- **ACK/NACK detection**: Confirming slave acknowledgment

### 3. **Clock Stretching**
Slaves can hold SCL low to pause the master:

- Must be monitored for timeouts
- Can indicate slow slave processing
- Should have configurable maximum duration limits

### 4. **Bus Error Detection**
The verification system should detect:

- **Arbitration loss**: Multiple masters detect conflicts
- **Bus hangs**: SCL or SDA stuck low/high
- **Framing errors**: Invalid START/STOP conditions
- **Glitches**: Unwanted transitions during valid data periods

## Practical Implementation Considerations

### Hardware Requirements
For real-world verification, you'll need:

1. **Logic Analyzer**: Captures precise timing of SCL/SDA signals
2. **Oscilloscope**: Verifies analog characteristics (rise/fall times, voltage levels)
3. **Protocol Analyzer**: Dedicated I2C decoder for detailed analysis
4. **Test Fixtures**: Known-good master and slave devices

### Integration with Test Systems

The verification code can be integrated with:

- **Automated test equipment (ATE)**
- **Continuous integration (CI) pipelines** for firmware testing
- **Production test systems** for board-level validation
- **Debug interfaces** for development troubleshooting

### Common Violations Found

Through systematic verification, you'll commonly find:

- **Insufficient setup/hold times** at higher speeds
- **Rise time violations** due to excessive bus capacitance
- **Clock stretching timeouts** with slow peripherals
- **Address conflicts** with improperly configured devices

Both implementations provide a solid foundation for I2C protocol verification, with the C version offering low-level control suitable for embedded systems, and the Rust version providing type safety and modern error handling for more complex verification frameworks.