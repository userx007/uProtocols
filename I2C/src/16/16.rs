/// I2C Pull-up Resistor Calculator in Rust
/// Provides type-safe calculation with comprehensive validation

use std::fmt;

/// I2C speed modes with associated timing requirements
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cSpeedMode {
    Standard,    // 100 kHz
    Fast,        // 400 kHz
    FastPlus,    // 1 MHz
    HighSpeed,   // 3.4 MHz
}

impl I2cSpeedMode {
    /// Get maximum rise time in nanoseconds
    pub fn max_rise_time_ns(&self) -> f64 {
        match self {
            I2cSpeedMode::Standard => 1000.0,
            I2cSpeedMode::Fast => 300.0,
            I2cSpeedMode::FastPlus => 120.0,
            I2cSpeedMode::HighSpeed => 80.0,
        }
    }
    
    /// Get frequency in Hz
    pub fn frequency_hz(&self) -> u32 {
        match self {
            I2cSpeedMode::Standard => 100_000,
            I2cSpeedMode::Fast => 400_000,
            I2cSpeedMode::FastPlus => 1_000_000,
            I2cSpeedMode::HighSpeed => 3_400_000,
        }
    }
    
    /// Get maximum bus capacitance in picofarads
    pub fn max_capacitance_pf(&self) -> f64 {
        match self {
            I2cSpeedMode::Standard => 400.0,
            I2cSpeedMode::Fast => 400.0,
            I2cSpeedMode::FastPlus => 550.0,
            I2cSpeedMode::HighSpeed => 100.0,
        }
    }
}

impl fmt::Display for I2cSpeedMode {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cSpeedMode::Standard => write!(f, "Standard Mode (100 kHz)"),
            I2cSpeedMode::Fast => write!(f, "Fast Mode (400 kHz)"),
            I2cSpeedMode::FastPlus => write!(f, "Fast Mode Plus (1 MHz)"),
            I2cSpeedMode::HighSpeed => write!(f, "High Speed Mode (3.4 MHz)"),
        }
    }
}

/// Configuration for pull-up resistor calculation
#[derive(Debug, Clone)]
pub struct I2cConfig {
    pub vcc: f64,              // Supply voltage (V)
    pub vol_max: f64,          // Max LOW output voltage (V)
    pub iol: f64,              // LOW output current (A)
    pub bus_capacitance: f64,  // Total capacitance (pF)
    pub speed_mode: I2cSpeedMode,
}

impl Default for I2cConfig {
    fn default() -> Self {
        I2cConfig {
            vcc: 3.3,
            vol_max: 0.4,
            iol: 0.003,  // 3 mA
            bus_capacitance: 100.0,
            speed_mode: I2cSpeedMode::Standard,
        }
    }
}

/// Result of pull-up resistor calculation
#[derive(Debug, Clone)]
pub struct ResistorResult {
    pub min_resistance: f64,     // Ohms
    pub max_resistance: f64,     // Ohms
    pub recommended: Option<f64>, // Ohms (None if invalid)
    pub warnings: Vec<String>,
}

impl ResistorResult {
    /// Check if the calculated range is valid
    pub fn is_valid(&self) -> bool {
        self.recommended.is_some()
    }
    
    /// Get recommended value in kilohms
    pub fn recommended_kohms(&self) -> Option<f64> {
        self.recommended.map(|r| r / 1000.0)
    }
}

/// Pull-up resistor calculator
pub struct PullupCalculator;

impl PullupCalculator {
    /// Calculate pull-up resistor values
    pub fn calculate(config: &I2cConfig) -> ResistorResult {
        let mut warnings = Vec::new();
        
        // Validate configuration
        if config.bus_capacitance > config.speed_mode.max_capacitance_pf() {
            warnings.push(format!(
                "Bus capacitance ({:.1} pF) exceeds maximum for {} ({:.1} pF)",
                config.bus_capacitance,
                config.speed_mode,
                config.speed_mode.max_capacitance_pf()
            ));
        }
        
        // Calculate minimum resistance: (VCC - VOL_max) / IOL
        let min_resistance = (config.vcc - config.vol_max) / config.iol;
        
        // Calculate maximum resistance: tr / (0.8473 × Cb)
        let tr_seconds = config.speed_mode.max_rise_time_ns() * 1e-9;
        let cb_farads = config.bus_capacitance * 1e-12;
        let max_resistance = tr_seconds / (0.8473 * cb_farads);
        
        // Determine if valid range exists and calculate recommendation
        let recommended = if max_resistance > min_resistance {
            // Geometric mean for optimal performance
            let geometric_mean = (min_resistance * max_resistance).sqrt();
            Some(Self::round_to_standard_value(geometric_mean))
        } else {
            warnings.push(
                "No valid resistor range! Consider: reducing capacitance, \
                 lowering speed, or using active pull-ups.".to_string()
            );
            None
        };
        
        ResistorResult {
            min_resistance,
            max_resistance,
            recommended,
            warnings,
        }
    }
    
    /// Round to nearest E12 standard resistor value
    fn round_to_standard_value(resistance: f64) -> f64 {
        const E12_VALUES: [f64; 12] = [
            1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2
        ];
        
        let magnitude = 10_f64.powf(resistance.log10().floor());
        let normalized = resistance / magnitude;
        
        let closest = E12_VALUES
            .iter()
            .min_by(|a, b| {
                let diff_a = (normalized - **a).abs();
                let diff_b = (normalized - **b).abs();
                diff_a.partial_cmp(&diff_b).unwrap()
            })
            .unwrap();
        
        closest * magnitude
    }
    
    /// Estimate bus capacitance from physical parameters
    pub fn estimate_capacitance(
        num_devices: usize,
        wire_length_cm: f64,
        include_pcb_parasitic: bool,
    ) -> f64 {
        let device_cap = num_devices as f64 * 7.5; // ~7.5 pF per device
        let wire_cap = wire_length_cm * 1.5;        // ~1.5 pF per cm
        let pcb_cap = if include_pcb_parasitic { 20.0 } else { 0.0 };
        
        device_cap + wire_cap + pcb_cap
    }
}

/// Pretty print results
impl fmt::Display for ResistorResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "Pull-up Resistor Calculation Results:")?;
        writeln!(f, "  Minimum: {:.0} Ω ({:.2} kΩ)", 
                 self.min_resistance, self.min_resistance / 1000.0)?;
        writeln!(f, "  Maximum: {:.0} Ω ({:.2} kΩ)", 
                 self.max_resistance, self.max_resistance / 1000.0)?;
        
        if let Some(rec) = self.recommended {
            writeln!(f, "  Recommended: {:.0} Ω ({:.2} kΩ)", rec, rec / 1000.0)?;
            writeln!(f, "  Status: ✓ Valid range")?;
        } else {
            writeln!(f, "  Status: ✗ No valid range")?;
        }
        
        if !self.warnings.is_empty() {
            writeln!(f, "\nWarnings:")?;
            for warning in &self.warnings {
                writeln!(f, "  • {}", warning)?;
            }
        }
        
        Ok(())
    }
}

fn main() {
    println!("=== I2C Pull-up Resistor Calculator ===\n");
    
    // Example 1: Standard configuration
    println!("Example 1: Typical 3.3V system");
    let config1 = I2cConfig {
        vcc: 3.3,
        bus_capacitance: PullupCalculator::estimate_capacitance(2, 10.0, true),
        speed_mode: I2cSpeedMode::Standard,
        ..Default::default()
    };
    
    let result1 = PullupCalculator::calculate(&config1);
    println!("{}", result1);
    
    // Example 2: Fast mode with higher capacitance
    println!("\nExample 2: Fast Mode with multiple devices");
    let config2 = I2cConfig {
        vcc: 5.0,
        bus_capacitance: PullupCalculator::estimate_capacitance(4, 20.0, true),
        speed_mode: I2cSpeedMode::Fast,
        ..Default::default()
    };
    
    let result2 = PullupCalculator::calculate(&config2);
    println!("{}", result2);
    
    // Example 3: Edge case - too much capacitance
    println!("\nExample 3: Problematic configuration (high capacitance)");
    let config3 = I2cConfig {
        vcc: 3.3,
        bus_capacitance: 500.0,
        speed_mode: I2cSpeedMode::FastPlus,
        ..Default::default()
    };
    
    let result3 = PullupCalculator::calculate(&config3);
    println!("{}", result3);
    
    // Example 4: Using type safety
    println!("\nExample 4: High-speed mode requirements");
    for mode in [I2cSpeedMode::Standard, I2cSpeedMode::Fast, 
                 I2cSpeedMode::FastPlus, I2cSpeedMode::HighSpeed] {
        println!("\n{}:", mode);
        println!("  Max rise time: {:.0} ns", mode.max_rise_time_ns());
        println!("  Max capacitance: {:.0} pF", mode.max_capacitance_pf());
        println!("  Frequency: {} kHz", mode.frequency_hz() / 1000);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_standard_mode_calculation() {
        let config = I2cConfig::default();
        let result = PullupCalculator::calculate(&config);
        
        assert!(result.is_valid());
        assert!(result.recommended.unwrap() > result.min_resistance);
        assert!(result.recommended.unwrap() < result.max_resistance);
    }
    
    #[test]
    fn test_invalid_configuration() {
        let config = I2cConfig {
            bus_capacitance: 1000.0, // Very high capacitance
            speed_mode: I2cSpeedMode::HighSpeed,
            ..Default::default()
        };
        
        let result = PullupCalculator::calculate(&config);
        assert!(!result.is_valid());
        assert!(!result.warnings.is_empty());
    }
    
    #[test]
    fn test_capacitance_estimation() {
        let cap = PullupCalculator::estimate_capacitance(3, 15.0, true);
        assert!(cap > 0.0);
        // 3 devices * 7.5 + 15cm * 1.5 + 20 = 64.5 pF
        assert!((cap - 64.5).abs() < 0.1);
    }
}