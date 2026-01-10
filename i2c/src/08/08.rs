/// I2C Timing Parameters Implementation in Rust
/// Demonstrates type-safe bit-banged I2C with precise timing control

use core::time::Duration;

/// I2C operating modes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cMode {
    Standard,   // 100 kHz
    Fast,       // 400 kHz
    FastPlus,   // 1 MHz
}

/// Comprehensive I2C timing parameters
#[derive(Debug, Clone, Copy)]
pub struct I2cTiming {
    /// Data setup time (ns)
    pub t_su_dat: u32,
    /// Data hold time (ns)
    pub t_hd_dat: u32,
    /// START condition hold time (ns)
    pub t_hd_sta: u32,
    /// START condition setup time (ns)
    pub t_su_sta: u32,
    /// STOP condition setup time (ns)
    pub t_su_sto: u32,
    /// Bus free time between STOP and START (ns)
    pub t_buf: u32,
    /// SCL low period (ns)
    pub t_low: u32,
    /// SCL high period (ns)
    pub t_high: u32,
    /// Maximum rise time (ns)
    pub tr_max: u32,
    /// Maximum fall time (ns)
    pub tf_max: u32,
}

impl I2cTiming {
    /// Get timing parameters for specified mode
    pub const fn for_mode(mode: I2cMode) -> Self {
        match mode {
            I2cMode::Standard => Self::STANDARD,
            I2cMode::Fast => Self::FAST,
            I2cMode::FastPlus => Self::FAST_PLUS,
        }
    }

    /// Standard Mode (100 kHz) timing
    pub const STANDARD: Self = Self {
        t_su_dat: 250,      // 250 ns
        t_hd_dat: 300,      // 300 ns (0 min, but 300 recommended)
        t_hd_sta: 4_000,    // 4.0 μs
        t_su_sta: 4_700,    // 4.7 μs
        t_su_sto: 4_000,    // 4.0 μs
        t_buf: 4_700,       // 4.7 μs
        t_low: 4_700,       // 4.7 μs
        t_high: 4_000,      // 4.0 μs
        tr_max: 1_000,      // 1000 ns
        tf_max: 300,        // 300 ns
    };

    /// Fast Mode (400 kHz) timing
    pub const FAST: Self = Self {
        t_su_dat: 100,      // 100 ns
        t_hd_dat: 300,      // 300 ns (0 min)
        t_hd_sta: 600,      // 0.6 μs
        t_su_sta: 600,      // 0.6 μs
        t_su_sto: 600,      // 0.6 μs
        t_buf: 1_300,       // 1.3 μs
        t_low: 1_300,       // 1.3 μs
        t_high: 600,        // 0.6 μs
        tr_max: 300,        // 300 ns
        tf_max: 300,        // 300 ns
    };

    /// Fast Mode Plus (1 MHz) timing
    pub const FAST_PLUS: Self = Self {
        t_su_dat: 50,       // 50 ns
        t_hd_dat: 0,        // 0 ns min
        t_hd_sta: 260,      // 0.26 μs
        t_su_sta: 260,      // 0.26 μs
        t_su_sto: 260,      // 0.26 μs
        t_buf: 500,         // 0.5 μs
        t_low: 500,         // 0.5 μs
        t_high: 260,        // 0.26 μs
        tr_max: 120,        // 120 ns
        tf_max: 120,        // 120 ns
    };

    /// Calculate the theoretical maximum bus frequency
    pub fn max_frequency_hz(&self) -> u32 {
        // Period = t_low + t_high + tr + tf (approximately)
        let period_ns = self.t_low + self.t_high + self.tr_max + self.tf_max;
        1_000_000_000 / period_ns
    }
}

/// Hardware abstraction layer for GPIO operations
pub trait I2cPins {
    /// Set SCL line state (true = high, false = low)
    fn set_scl(&mut self, high: bool);
    
    /// Set SDA line state (true = high/release, false = low)
    fn set_sda(&mut self, high: bool);
    
    /// Read SDA line state
    fn read_sda(&self) -> bool;
    
    /// Delay for specified duration (nanosecond precision)
    fn delay_ns(&mut self, ns: u32);
}

/// I2C error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    /// No acknowledgment received from slave
    Nack,
    /// Bus arbitration lost
    ArbitrationLost,
    /// Timeout waiting for operation
    Timeout,
    /// Invalid timing parameters
    InvalidTiming,
}

pub type Result<T> = core::result::Result<T, I2cError>;

/// Bit-banged I2C master implementation
pub struct I2cMaster<P: I2cPins> {
    pins: P,
    timing: I2cTiming,
    started: bool,
}

impl<P: I2cPins> I2cMaster<P> {
    /// Create a new I2C master instance
    pub fn new(pins: P, mode: I2cMode) -> Self {
        let mut master = Self {
            pins,
            timing: I2cTiming::for_mode(mode),
            started: false,
        };
        
        // Initialize bus to idle state
        master.pins.set_scl(true);
        master.pins.set_sda(true);
        master
    }

    /// Create with custom timing parameters
    pub fn with_timing(pins: P, timing: I2cTiming) -> Self {
        let mut master = Self {
            pins,
            timing,
            started: false,
        };
        
        master.pins.set_scl(true);
        master.pins.set_sda(true);
        master
    }

    /// Generate START condition
    /// Timing: SDA falls while SCL is high
    pub fn start(&mut self) -> Result<()> {
        if self.started {
            // Repeated START: ensure SDA is high first
            self.pins.set_sda(true);
            self.pins.delay_ns(self.timing.t_su_sta);
            self.pins.set_scl(true);
            self.pins.delay_ns(self.timing.t_su_sta);
        } else {
            // Initial START: ensure bus free time
            self.pins.delay_ns(self.timing.t_buf);
            self.pins.set_scl(true);
            self.pins.set_sda(true);
            self.pins.delay_ns(self.timing.t_su_sta);
        }
        
        // Generate START: SDA goes low while SCL is high
        self.pins.set_sda(false);
        self.pins.delay_ns(self.timing.t_hd_sta);
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        self.started = true;
        Ok(())
    }

    /// Generate STOP condition
    /// Timing: SDA rises while SCL is high
    pub fn stop(&mut self) -> Result<()> {
        // Ensure SCL is low and SDA is low
        self.pins.set_scl(false);
        self.pins.set_sda(false);
        self.pins.delay_ns(self.timing.t_low);
        
        // Bring SCL high
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_su_sto);
        
        // Generate STOP: SDA goes high while SCL is high
        self.pins.set_sda(true);
        self.pins.delay_ns(self.timing.t_buf);
        
        self.started = false;
        Ok(())
    }

    /// Write a single bit with proper timing
    fn write_bit(&mut self, bit: bool) -> Result<()> {
        // Set data while clock is low
        self.pins.set_scl(false);
        self.pins.set_sda(bit);
        self.pins.delay_ns(self.timing.t_su_dat);
        
        // Clock high period - data is sampled
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_high);
        
        // Clock low period
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        Ok(())
    }

    /// Read a single bit with proper timing
    fn read_bit(&mut self) -> Result<bool> {
        // Release SDA (slave will drive it)
        self.pins.set_scl(false);
        self.pins.set_sda(true);
        self.pins.delay_ns(self.timing.t_su_dat);
        
        // Clock high - sample data
        self.pins.set_scl(true);
        self.pins.delay_ns(self.timing.t_high / 2);
        let bit = self.pins.read_sda();
        self.pins.delay_ns(self.timing.t_high / 2);
        
        // Clock low
        self.pins.set_scl(false);
        self.pins.delay_ns(self.timing.t_hd_dat);
        
        Ok(bit)
    }

    /// Write a byte and return ACK status
    pub fn write_byte(&mut self, data: u8) -> Result<bool> {
        // Write 8 bits, MSB first
        for i in (0..8).rev() {
            self.write_bit((data >> i) & 0x01 != 0)?;
        }
        
        // Read ACK bit (false = ACK, true = NACK)
        let ack = !self.read_bit()?;
        Ok(ack)
    }

    /// Read a byte and send ACK/NACK
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8> {
        let mut data = 0u8;
        
        // Read 8 bits, MSB first
        for i in (0..8).rev() {
            if self.read_bit()? {
                data |= 1 << i;
            }
        }
        
        // Send ACK (false) or NACK (true)
        self.write_bit(!send_ack)?;
        
        Ok(data)
    }

    /// Write data to a register
    pub fn write_register(&mut self, dev_addr: u8, reg_addr: u8, 
                          value: u8) -> Result<()> {
        self.start()?;
        
        // Send device address with write bit
        if !self.write_byte((dev_addr << 1) | 0)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Send register address
        if !self.write_byte(reg_addr)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Send data
        if !self.write_byte(value)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        self.stop()?;
        Ok(())
    }

    /// Read data from a register
    pub fn read_register(&mut self, dev_addr: u8, reg_addr: u8) -> Result<u8> {
        // Write phase: send register address
        self.start()?;
        if !self.write_byte((dev_addr << 1) | 0)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        if !self.write_byte(reg_addr)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        // Read phase: read data
        self.start()?; // Repeated START
        if !self.write_byte((dev_addr << 1) | 1)? {
            self.stop()?;
            return Err(I2cError::Nack);
        }
        
        let data = self.read_byte(false)?; // Send NACK (last byte)
        self.stop()?;
        
        Ok(data)
    }
}

/// Calculate required pull-up resistor value
/// Returns resistor value in ohms
pub fn calculate_pullup_resistor(vdd: f32, bus_capacitance_pf: f32, 
                                  rise_time_ns: f32) -> f32 {
    // Rise time equation: tr ≈ 0.8473 × R × Cb
    // Where tr is from 30% to 70% of Vdd
    // R = tr / (0.8473 × Cb)
    let cb_farads = bus_capacitance_pf * 1e-12;
    let tr_seconds = rise_time_ns * 1e-9;
    tr_seconds / (0.8473 * cb_farads)
}

/// Verify timing constraints for a given configuration
pub fn verify_timing(timing: &I2cTiming, measured_tr_ns: u32, 
                     measured_tf_ns: u32) -> Result<()> {
    if measured_tr_ns > timing.tr_max {
        return Err(I2cError::InvalidTiming);
    }
    if measured_tf_ns > timing.tf_max {
        return Err(I2cError::InvalidTiming);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_timing_calculations() {
        // Test pull-up resistor calculation
        // For 3.3V, 100pF bus, 300ns rise time
        let r = calculate_pullup_resistor(3.3, 100.0, 300.0);
        assert!((r - 3540.0).abs() < 100.0); // ~3.5kΩ
        
        // Fast mode max frequency
        let timing = I2cTiming::FAST;
        let max_freq = timing.max_frequency_hz();
        assert!(max_freq >= 400_000 && max_freq <= 500_000);
    }

    #[test]
    fn test_timing_verification() {
        let timing = I2cTiming::FAST;
        
        // Valid timing
        assert!(verify_timing(&timing, 250, 200).is_ok());
        
        // Invalid rise time
        assert!(verify_timing(&timing, 400, 200).is_err());
        
        // Invalid fall time
        assert!(verify_timing(&timing, 250, 400).is_err());
    }
}

// Example usage
fn example_usage() {
    // Define a mock pins implementation
    struct MockPins;
    
    impl I2cPins for MockPins {
        fn set_scl(&mut self, _high: bool) { /* GPIO operations */ }
        fn set_sda(&mut self, _high: bool) { /* GPIO operations */ }
        fn read_sda(&self) -> bool { true }
        fn delay_ns(&mut self, _ns: u32) { /* Precise delay */ }
    }
    
    let pins = MockPins;
    let mut i2c = I2cMaster::new(pins, I2cMode::Fast);
    
    // Write to device
    let _ = i2c.write_register(0x50, 0x10, 0xAA);
    
    // Read from device
    let _ = i2c.read_register(0x50, 0x10);
}