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