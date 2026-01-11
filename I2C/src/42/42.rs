/*!
 * I2C Oscilloscope Analysis and Signal Quality Verification in Rust
 * 
 * This implementation provides comprehensive timing analysis for I2C
 * communication with oscilloscope-level precision measurements.
 */

use std::time::{Duration, Instant};
use std::fmt;

/// I2C Speed Modes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CSpeed {
    Standard,    // 100 kHz
    Fast,        // 400 kHz
    FastPlus,    // 1 MHz
}

/// I2C Timing Parameters
#[derive(Debug, Clone)]
pub struct TimingParams {
    pub scl_frequency: u32,        // Hz
    pub t_hd_sta: Duration,        // Hold time START condition
    pub t_su_sta: Duration,        // Setup time START condition
    pub t_su_dat: Duration,        // Data setup time
    pub t_hd_dat: Duration,        // Data hold time
    pub t_su_sto: Duration,        // Setup time STOP condition
    pub t_buf: Duration,           // Bus free time
    pub max_rise_time: Duration,   // Maximum rise time
    pub max_fall_time: Duration,   // Maximum fall time
}

impl TimingParams {
    /// Create timing parameters for specified speed mode
    pub fn new(speed: I2CSpeed) -> Self {
        match speed {
            I2CSpeed::Standard => Self {
                scl_frequency: 100_000,
                t_hd_sta: Duration::from_micros(4),
                t_su_sta: Duration::from_nanos(4700),
                t_su_dat: Duration::from_nanos(250),
                t_hd_dat: Duration::from_nanos(300),
                t_su_sto: Duration::from_micros(4),
                t_buf: Duration::from_nanos(4700),
                max_rise_time: Duration::from_nanos(1000),
                max_fall_time: Duration::from_nanos(300),
            },
            I2CSpeed::Fast => Self {
                scl_frequency: 400_000,
                t_hd_sta: Duration::from_nanos(600),
                t_su_sta: Duration::from_nanos(600),
                t_su_dat: Duration::from_nanos(100),
                t_hd_dat: Duration::from_nanos(300),
                t_su_sto: Duration::from_nanos(600),
                t_buf: Duration::from_nanos(1300),
                max_rise_time: Duration::from_nanos(300),
                max_fall_time: Duration::from_nanos(300),
            },
            I2CSpeed::FastPlus => Self {
                scl_frequency: 1_000_000,
                t_hd_sta: Duration::from_nanos(260),
                t_su_sta: Duration::from_nanos(260),
                t_su_dat: Duration::from_nanos(50),
                t_hd_dat: Duration::from_nanos(0),
                t_su_sto: Duration::from_nanos(260),
                t_buf: Duration::from_nanos(500),
                max_rise_time: Duration::from_nanos(120),
                max_fall_time: Duration::from_nanos(120),
            },
        }
    }
    
    /// Get half clock period
    pub fn half_period(&self) -> Duration {
        Duration::from_nanos(500_000_000 / self.scl_frequency as u64)
    }
}

/// Signal timing measurements
#[derive(Debug, Default, Clone)]
pub struct SignalMeasurement {
    pub rise_time: Option<Duration>,
    pub fall_time: Option<Duration>,
    pub scl_high_time: Option<Duration>,
    pub scl_low_time: Option<Duration>,
    pub start_setup_time: Option<Duration>,
    pub stop_setup_time: Option<Duration>,
    pub clock_stretching_detected: bool,
    pub stretch_duration: Option<Duration>,
    pub glitches_detected: u32,
}

impl SignalMeasurement {
    pub fn new() -> Self {
        Self::default()
    }
}

/// Signal quality analysis results
#[derive(Debug)]
pub struct SignalQuality {
    pub rise_time_ok: bool,
    pub fall_time_ok: bool,
    pub timing_ok: bool,
    pub frequency_ok: bool,
    pub warnings: Vec<String>,
}

impl fmt::Display for SignalQuality {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "=== I2C Signal Quality Analysis ===")?;
        writeln!(f, "Rise Time:     {}", if self.rise_time_ok { "✓ OK" } else { "⚠ FAIL" })?;
        writeln!(f, "Fall Time:     {}", if self.fall_time_ok { "✓ OK" } else { "⚠ FAIL" })?;
        writeln!(f, "Timing:        {}", if self.timing_ok { "✓ OK" } else { "⚠ FAIL" })?;
        writeln!(f, "Frequency:     {}", if self.frequency_ok { "✓ OK" } else { "⚠ FAIL" })?;
        
        if !self.warnings.is_empty() {
            writeln!(f, "\nWarnings:")?;
            for warning in &self.warnings {
                writeln!(f, "  ⚠ {}", warning)?;
            }
        }
        
        write!(f, "===================================")
    }
}

/// GPIO Pin trait for hardware abstraction
pub trait GpioPin {
    fn set_high(&mut self);
    fn set_low(&mut self);
    fn read(&self) -> bool;
    fn set_input(&mut self);
    fn set_output(&mut self);
}

/// Mock GPIO for testing/simulation
#[derive(Debug)]
pub struct MockPin {
    state: bool,
    is_input: bool,
}

impl MockPin {
    pub fn new() -> Self {
        Self {
            state: true,  // Pull-up by default
            is_input: true,
        }
    }
}

impl GpioPin for MockPin {
    fn set_high(&mut self) {
        self.set_input();  // Open-drain: high = input with pull-up
    }
    
    fn set_low(&mut self) {
        self.set_output();
        self.state = false;
    }
    
    fn read(&self) -> bool {
        if self.is_input {
            true  // Pulled high
        } else {
            self.state
        }
    }
    
    fn set_input(&mut self) {
        self.is_input = true;
    }
    
    fn set_output(&mut self) {
        self.is_input = false;
    }
}

/// I2C Bus with timing analysis
pub struct I2CBus<SDA: GpioPin, SCL: GpioPin> {
    sda: SDA,
    scl: SCL,
    timing: TimingParams,
    measurement: SignalMeasurement,
    debug: bool,
}

impl<SDA: GpioPin, SCL: GpioPin> I2CBus<SDA, SCL> {
    /// Create new I2C bus with timing analysis
    pub fn new(sda: SDA, scl: SCL, speed: I2CSpeed) -> Self {
        Self {
            sda,
            scl,
            timing: TimingParams::new(speed),
            measurement: SignalMeasurement::new(),
            debug: false,
        }
    }
    
    /// Enable debug output for oscilloscope markers
    pub fn enable_debug(&mut self) {
        self.debug = true;
    }
    
    /// Initialize the bus
    pub fn init(&mut self) {
        self.sda.set_high();
        self.scl.set_high();
        std::thread::sleep(self.timing.t_buf);
    }
    
    /// Generate START condition with timing analysis
    pub fn start(&mut self) {
        if self.debug {
            println!("[SCOPE] START condition begin");
            println!("[SCOPE] SDA and SCL both HIGH");
        }
        
        // Ensure both lines are high
        self.sda.set_high();
        self.scl.set_high();
        std::thread::sleep(self.timing.t_buf);
        
        // START: SDA goes LOW while SCL is HIGH
        let fall_start = Instant::now();
        self.sda.set_low();
        let fall_end = Instant::now();
        
        self.measurement.fall_time = Some(fall_end - fall_start);
        
        if self.debug {
            println!("[SCOPE] SDA goes LOW (START)");
            println!("[SCOPE] Fall time: {:?}", self.measurement.fall_time);
        }
        
        // Hold time for START condition
        std::thread::sleep(self.timing.t_hd_sta);
        
        // Pull SCL low
        self.scl.set_low();
        
        if self.debug {
            println!("[SCOPE] SCL goes LOW (ready for data)");
        }
    }
    
    /// Generate STOP condition with timing analysis
    pub fn stop(&mut self) {
        // Ensure SDA is low
        self.sda.set_low();
        std::thread::sleep(self.timing.t_hd_dat);
        
        // Release SCL
        self.scl.set_high();
        
        // Check for clock stretching
        let stretch_start = Instant::now();
        let mut stretch_timeout = Duration::from_millis(10);
        
        while !self.scl.read() {
            if stretch_start.elapsed() > stretch_timeout {
                break;
            }
            std::thread::sleep(Duration::from_micros(1));
        }
        
        let stretch_duration = stretch_start.elapsed();
        if stretch_duration > Duration::from_micros(10) {
            self.measurement.clock_stretching_detected = true;
            self.measurement.stretch_duration = Some(stretch_duration);
            
            if self.debug {
                println!("[SCOPE] Clock stretching detected: {:?}", stretch_duration);
            }
        }
        
        // Setup time for STOP
        std::thread::sleep(self.timing.t_su_sto);
        
        if self.debug {
            println!("[SCOPE] STOP condition begin");
        }
        
        // STOP: SDA goes HIGH while SCL is HIGH
        let rise_start = Instant::now();
        self.sda.set_high();
        let rise_end = Instant::now();
        
        self.measurement.rise_time = Some(rise_end - rise_start);
        
        if self.debug {
            println!("[SCOPE] SDA goes HIGH (STOP)");
            println!("[SCOPE] Rise time: {:?}", self.measurement.rise_time);
        }
        
        // Bus free time
        std::thread::sleep(self.timing.t_buf);
    }
    
    /// Write a single bit with timing measurement
    fn write_bit(&mut self, bit: bool) {
        // Set data line
        if bit {
            self.sda.set_high();
        } else {
            self.sda.set_low();
        }
        
        // Data setup time
        std::thread::sleep(self.timing.t_su_dat);
        
        // Release clock
        let scl_low_start = Instant::now();
        self.scl.set_high();
        
        // Wait for clock to go high (clock stretching)
        while !self.scl.read() {
            std::thread::sleep(Duration::from_micros(1));
        }
        let scl_high_start = Instant::now();
        
        // Clock high period
        std::thread::sleep(self.timing.half_period());
        
        let scl_high_end = Instant::now();
        self.measurement.scl_high_time = Some(scl_high_end - scl_high_start);
        
        // Pull clock low
        self.scl.set_low();
        self.measurement.scl_low_time = Some(scl_high_start - scl_low_start);
        
        if self.debug {
            println!("[SCOPE] Bit: {}, SCL high: {:?}, SCL low: {:?}",
                     bit,
                     self.measurement.scl_high_time,
                     self.measurement.scl_low_time);
        }
    }
    
    /// Read a single bit
    fn read_bit(&mut self) -> bool {
        // Release SDA for slave to control
        self.sda.set_high();
        std::thread::sleep(self.timing.t_su_dat);
        
        // Release clock
        self.scl.set_high();
        
        // Wait for clock to go high
        while !self.scl.read() {
            std::thread::sleep(Duration::from_micros(1));
        }
        
        // Clock high period
        std::thread::sleep(self.timing.half_period());
        
        // Read the bit
        let bit = self.sda.read();
        
        // Pull clock low
        self.scl.set_low();
        
        if self.debug {
            println!("[SCOPE] Read bit: {}", bit);
        }
        
        bit
    }
    
    /// Write a byte and return ACK status
    pub fn write_byte(&mut self, byte: u8) -> Result<(), &'static str> {
        if self.debug {
            println!("[SCOPE] Writing byte: 0x{:02X}", byte);
        }
        
        // Write 8 bits (MSB first)
        for i in (0..8).rev() {
            let bit = (byte >> i) & 0x01 != 0;
            self.write_bit(bit);
        }
        
        // Read ACK bit (LOW = ACK)
        let ack = !self.read_bit();
        
        if self.debug {
            println!("[SCOPE] ACK: {}", ack);
        }
        
        if ack {
            Ok(())
        } else {
            Err("No ACK received")
        }
    }
    
    /// Read a byte and send ACK/NACK
    pub fn read_byte(&mut self, send_ack: bool) -> u8 {
        let mut byte = 0u8;
        
        // Read 8 bits
        for _ in 0..8 {
            byte = (byte << 1) | (self.read_bit() as u8);
        }
        
        // Send ACK or NACK
        self.write_bit(!send_ack);
        
        if self.debug {
            println!("[SCOPE] Read byte: 0x{:02X}", byte);
        }
        
        byte
    }
    
    /// Analyze signal quality
    pub fn analyze_signal_quality(&self) -> SignalQuality {
        let mut warnings = Vec::new();
        
        // Check rise time
        let rise_time_ok = if let Some(rise_time) = self.measurement.rise_time {
            if rise_time > self.timing.max_rise_time {
                warnings.push(format!(
                    "Rise time ({:?}) exceeds maximum ({:?}). Reduce pull-up resistors or bus capacitance.",
                    rise_time, self.timing.max_rise_time
                ));
                false
            } else {
                true
            }
        } else {
            warnings.push("Rise time not measured".to_string());
            false
        };
        
        // Check fall time
        let fall_time_ok = if let Some(fall_time) = self.measurement.fall_time {
            if fall_time > self.timing.max_fall_time {
                warnings.push(format!(
                    "Fall time ({:?}) exceeds maximum ({:?}).",
                    fall_time, self.timing.max_fall_time
                ));
                false
            } else {
                true
            }
        } else {
            warnings.push("Fall time not measured".to_string());
            false
        };
        
        // Check frequency
        let frequency_ok = if let (Some(high), Some(low)) = 
            (self.measurement.scl_high_time, self.measurement.scl_low_time) {
            let period = high + low;
            let freq = Duration::from_secs(1).as_nanos() / period.as_nanos();
            let target_freq = self.timing.scl_frequency as u128;
            let tolerance = target_freq / 10; // 10% tolerance
            
            if freq < target_freq - tolerance || freq > target_freq + tolerance {
                warnings.push(format!(
                    "Measured frequency ({} Hz) differs from target ({} Hz)",
                    freq, target_freq
                ));
                false
            } else {
                true
            }
        } else {
            warnings.push("Clock frequency not measured".to_string());
            false
        };
        
        // Check clock stretching
        if self.measurement.clock_stretching_detected {
            if let Some(duration) = self.measurement.stretch_duration {
                warnings.push(format!("Clock stretching detected: {:?}", duration));
            }
        }
        
        SignalQuality {
            rise_time_ok,
            fall_time_ok,
            timing_ok: rise_time_ok && fall_time_ok,
            frequency_ok,
            warnings,
        }
    }
    
    /// Get current measurements
    pub fn get_measurements(&self) -> &SignalMeasurement {
        &self.measurement
    }
}

/// Example usage
fn main() {
    println!("I2C Oscilloscope Analysis Tool\n");
    println!("Connect oscilloscope probes:");
    println!("  CH1: SDA line");
    println!("  CH2: SCL line");
    println!("Trigger: SDA falling edge with SCL high (START condition)\n");
    
    // Create mock I2C bus for demonstration
    let sda = MockPin::new();
    let scl = MockPin::new();
    let mut bus = I2CBus::new(sda, scl, I2CSpeed::Fast);
    bus.enable_debug();
    
    // Initialize bus
    bus.init();
    
    println!("\n--- Starting I2C Transaction ---\n");
    
    // Perform a test write transaction
    bus.start();
    
    // Write device address (0x50 << 1 | 0 for write)
    let device_addr = 0xA0; // 0x50 with write bit
    match bus.write_byte(device_addr) {
        Ok(_) => {
            println!("Device acknowledged");
            
            // Write register address
            let _ = bus.write_byte(0x00);
            
            // Write data
            let _ = bus.write_byte(0xAA);
        }
        Err(e) => {
            println!("Error: {}", e);
        }
    }
    
    bus.stop();
    
    // Analyze signal quality
    println!("\n--- Analysis Results ---\n");
    let quality = bus.analyze_signal_quality();
    println!("{}", quality);
    
    // Display detailed measurements
    let measurements = bus.get_measurements();
    println!("\n--- Detailed Measurements ---");
    if let Some(rt) = measurements.rise_time {
        println!("Rise time: {:?}", rt);
    }
    if let Some(ft) = measurements.fall_time {
        println!("Fall time: {:?}", ft);
    }
    if let Some(high) = measurements.scl_high_time {
        println!("SCL high time: {:?}", high);
    }
    if let Some(low) = measurements.scl_low_time {
        println!("SCL low time: {:?}", low);
    }
}