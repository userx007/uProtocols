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