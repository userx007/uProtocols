// Embedded Rust I2C Pull-up Configuration Validator
// For use in no_std embedded environments

#![no_std]

/// I2C speed configuration
#[derive(Debug, Clone, Copy)]
pub enum I2cSpeed {
    Standard100kHz,
    Fast400kHz,
    FastPlus1MHz,
}

impl I2cSpeed {
    /// Get maximum rise time in nanoseconds
    pub const fn max_rise_time_ns(&self) -> u32 {
        match self {
            I2cSpeed::Standard100kHz => 1000,
            I2cSpeed::Fast400kHz => 300,
            I2cSpeed::FastPlus1MHz => 120,
        }
    }
    
    /// Get clock frequency in Hz
    pub const fn frequency_hz(&self) -> u32 {
        match self {
            I2cSpeed::Standard100kHz => 100_000,
            I2cSpeed::Fast400kHz => 400_000,
            I2cSpeed::FastPlus1MHz => 1_000_000,
        }
    }
}

/// Pull-up resistor configuration validation result
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ResistorValidation {
    Valid,
    TooLow,
    TooHigh,
    CapacitanceExceeded,
}

/// Compile-time I2C pull-up configuration
pub struct I2cPullupConfig<const VCC_MV: u32, const R_PULLUP_OHM: u32> {
    speed: I2cSpeed,
    bus_capacitance_pf: u32,
}

impl<const VCC_MV: u32, const R_PULLUP_OHM: u32> I2cPullupConfig<VCC_MV, R_PULLUP_OHM> {
    /// Create new configuration
    pub const fn new(speed: I2cSpeed, bus_capacitance_pf: u32) -> Self {
        Self {
            speed,
            bus_capacitance_pf,
        }
    }
    
    /// Calculate minimum acceptable resistance (in ohms)
    const fn min_resistance_ohm() -> u32 {
        // Rmin = (VCC - VOL) / IOL
        // VOL = 400mV, IOL = 3mA
        const VOL_MV: u32 = 400;
        const IOL_MA: u32 = 3;
        
        ((VCC_MV - VOL_MV) * 1000) / (IOL_MA * 1000)
    }
    
    /// Calculate maximum acceptable resistance (in ohms)
    const fn max_resistance_ohm(&self) -> u32 {
        // Rmax = tr / (0.8473 × Cb)
        // Approximation using integer math: tr_ns * 1000 / (847 * Cb_pF)
        let tr_ns = self.speed.max_rise_time_ns();
        (tr_ns * 1000) / (847 * self.bus_capacitance_pf / 1000)
    }
    
    /// Validate the resistor value
    pub const fn validate(&self) -> ResistorValidation {
        let min = Self::min_resistance_ohm();
        let max = self.max_resistance_ohm();
        
        if R_PULLUP_OHM < min {
            ResistorValidation::TooLow
        } else if R_PULLUP_OHM > max {
            ResistorValidation::TooHigh
        } else if max < min {
            ResistorValidation::CapacitanceExceeded
        } else {
            ResistorValidation::Valid
        }
    }
    
    /// Get configured speed
    pub const fn speed(&self) -> I2cSpeed {
        self.speed
    }
    
    /// Get configured capacitance
    pub const fn capacitance_pf(&self) -> u32 {
        self.bus_capacitance_pf
    }
}

/// Runtime pull-up calculator for embedded systems
pub struct RuntimePullupCalc;

impl RuntimePullupCalc {
    /// Calculate resistor range
    pub fn calculate(
        vcc_mv: u32,
        bus_cap_pf: u32,
        speed: I2cSpeed,
    ) -> (u32, u32) {
        // Minimum resistance
        const VOL_MV: u32 = 400;
        const IOL_MA: u32 = 3;
        let r_min = ((vcc_mv - VOL_MV) * 1000) / (IOL_MA * 1000);
        
        // Maximum resistance (simplified integer calculation)
        let tr_ns = speed.max_rise_time_ns();
        let r_max = (tr_ns * 1000) / (847 * bus_cap_pf / 1000);
        
        (r_min, r_max)
    }
    
    /// Validate resistor is in acceptable range
    pub fn validate_resistor(
        resistor_ohm: u32,
        vcc_mv: u32,
        bus_cap_pf: u32,
        speed: I2cSpeed,
    ) -> bool {
        let (r_min, r_max) = Self::calculate(vcc_mv, bus_cap_pf, speed);
        resistor_ohm >= r_min && resistor_ohm <= r_max
    }
}

// Example usage in embedded firmware
#[cfg(feature = "example")]
mod example {
    use super::*;
    
    // Type-safe compile-time validated configuration
    // VCC = 3300mV, R_PULLUP = 4700Ω
    type MyI2cConfig = I2cPullupConfig<3300, 4700>;
    
    pub fn init_i2c() {
        // Configuration with 150pF bus capacitance, 400kHz
        const CONFIG: MyI2cConfig = MyI2cConfig::new(
            I2cSpeed::Fast400kHz,
            150, // 150 pF
        );
        
        // Compile-time validation (const context)
        match CONFIG.validate() {
            ResistorValidation::Valid => {
                // Safe to initialize I2C hardware
                // configure_i2c_hardware(CONFIG.speed().frequency_hz());
            }
            ResistorValidation::TooLow => {
                // panic!("Pull-up resistor too low!");
            }
            ResistorValidation::TooHigh => {
                // panic!("Pull-up resistor too high!");
            }
            ResistorValidation::CapacitanceExceeded => {
                // panic!("Bus capacitance too high for speed mode!");
            }
        }
    }
    
    // Runtime validation example
    pub fn validate_runtime_config() {
        let vcc_mv = 3300;
        let resistor = 4700;
        let capacitance = 200; // pF
        let speed = I2cSpeed::Fast400kHz;
        
        let is_valid = RuntimePullupCalc::validate_resistor(
            resistor,
            vcc_mv,
            capacitance,
            speed,
        );
        
        if !is_valid {
            // Handle invalid configuration
            let (r_min, r_max) = RuntimePullupCalc::calculate(
                vcc_mv,
                capacitance,
                speed,
            );
            // Log error: resistor out of range [r_min, r_max]
        }
    }
}

// Useful constants for common configurations
pub mod common_configs {
    use super::*;
    
    /// Standard 3.3V, 4.7kΩ, 100kHz configuration
    pub type Standard3V3_4K7 = I2cPullupConfig<3300, 4700>;
    
    /// 3.3V, 2.2kΩ, 400kHz (lower resistance for higher speed)
    pub type Fast3V3_2K2 = I2cPullupConfig<3300, 2200>;
    
    /// 5V, 4.7kΩ, 100kHz configuration
    pub type Standard5V_4K7 = I2cPullupConfig<5000, 4700>;
    
    /// 5V, 2.2kΩ, 400kHz configuration
    pub type Fast5V_2K2 = I2cPullupConfig<5000, 2200>;
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_compile_time_validation() {
        const CONFIG: I2cPullupConfig<3300, 4700> = 
            I2cPullupConfig::new(I2cSpeed::Standard100kHz, 100);
        
        assert_eq!(CONFIG.validate(), ResistorValidation::Valid);
    }
    
    #[test]
    fn test_runtime_calculation() {
        let (r_min, r_max) = RuntimePullupCalc::calculate(
            3300,
            100,
            I2cSpeed::Standard100kHz,
        );
        
        assert!(r_min < r_max);
        assert!(r_min > 0);
    }
    
    #[test]
    fn test_resistor_too_low() {
        const CONFIG: I2cPullupConfig<3300, 100> = 
            I2cPullupConfig::new(I2cSpeed::Standard100kHz, 100);
        
        assert_eq!(CONFIG.validate(), ResistorValidation::TooLow);
    }
}