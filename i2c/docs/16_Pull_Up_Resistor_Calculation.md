# Pull-up Resistor Calculation for I2C

Pull-up resistors are essential components in I2C communication, as both SDA (data) and SCL (clock) lines are open-drain and require external pull-ups to function properly. Selecting the correct resistor values is crucial for reliable communication.

## Why Pull-up Resistors are Needed

I2C uses open-drain (or open-collector) outputs, meaning devices can only pull the lines LOW. Pull-up resistors provide the necessary HIGH voltage level. When no device is pulling the line low, the resistor pulls it to VCC.

## Key Factors in Resistor Selection

### 1. **Bus Capacitance (Cb)**
Total capacitance includes:
- Wire/trace capacitance (~10-20 pF per inch)
- Pin capacitance of all devices (~3-10 pF per device)
- PCB parasitic capacitance

### 2. **I2C Speed Mode**
- **Standard Mode**: 100 kHz, max capacitance 400 pF
- **Fast Mode**: 400 kHz, max capacitance 400 pF
- **Fast Mode Plus**: 1 MHz, max capacitance 550 pF
- **High Speed Mode**: 3.4 MHz, max capacitance 100 pF

### 3. **Rise Time Requirements**
- Standard Mode: max 1000 ns
- Fast Mode: max 300 ns
- Fast Mode Plus: max 120 ns

## Calculation Formula

The pull-up resistor value must satisfy both minimum and maximum constraints:

**Minimum Resistor Value:**
```
Rp(min) = (VCC - VOL(max)) / IOL
```
- VCC: Supply voltage
- VOL(max): Maximum LOW-level output voltage (typically 0.4V)
- IOL: LOW-level output current (typically 3mA for standard I2C)

**Maximum Resistor Value:**
```
Rp(max) = tr / (0.8473 × Cb)
```
- tr: Maximum rise time
- Cb: Total bus capacitance

## C/C++ Code Examples

### Basic Pull-up Resistor Calculator

```c
#include <stdio.h>
#include <math.h>

// I2C speed modes
typedef enum {
    I2C_STANDARD_MODE = 0,    // 100 kHz
    I2C_FAST_MODE = 1,        // 400 kHz
    I2C_FAST_MODE_PLUS = 2,   // 1 MHz
    I2C_HIGH_SPEED_MODE = 3   // 3.4 MHz
} i2c_speed_mode_t;

// Configuration structure
typedef struct {
    float vcc;                 // Supply voltage (V)
    float vol_max;            // Max LOW-level output voltage (V)
    float iol;                // LOW-level output current (A)
    float bus_capacitance;    // Total bus capacitance (pF)
    i2c_speed_mode_t mode;    // Speed mode
} i2c_config_t;

// Result structure
typedef struct {
    float min_resistance;     // Minimum resistor value (ohms)
    float max_resistance;     // Maximum resistor value (ohms)
    float recommended;        // Recommended value (ohms)
    int is_valid;            // 1 if valid range exists
} resistor_result_t;

// Get maximum rise time for speed mode (nanoseconds)
float get_max_rise_time(i2c_speed_mode_t mode) {
    switch (mode) {
        case I2C_STANDARD_MODE:
            return 1000.0;    // 1000 ns
        case I2C_FAST_MODE:
            return 300.0;     // 300 ns
        case I2C_FAST_MODE_PLUS:
            return 120.0;     // 120 ns
        case I2C_HIGH_SPEED_MODE:
            return 80.0;      // 80 ns (approximate)
        default:
            return 1000.0;
    }
}

// Get speed mode name
const char* get_speed_mode_name(i2c_speed_mode_t mode) {
    switch (mode) {
        case I2C_STANDARD_MODE:
            return "Standard Mode (100 kHz)";
        case I2C_FAST_MODE:
            return "Fast Mode (400 kHz)";
        case I2C_FAST_MODE_PLUS:
            return "Fast Mode Plus (1 MHz)";
        case I2C_HIGH_SPEED_MODE:
            return "High Speed Mode (3.4 MHz)";
        default:
            return "Unknown";
    }
}

// Calculate pull-up resistor values
resistor_result_t calculate_pullup_resistor(const i2c_config_t* config) {
    resistor_result_t result = {0};
    
    // Calculate minimum resistance
    // Rp(min) = (VCC - VOL(max)) / IOL
    result.min_resistance = (config->vcc - config->vol_max) / config->iol;
    
    // Calculate maximum resistance
    // Rp(max) = tr / (0.8473 × Cb)
    // Convert capacitance from pF to F and rise time from ns to s
    float tr_seconds = get_max_rise_time(config->mode) * 1e-9;
    float cb_farads = config->bus_capacitance * 1e-12;
    result.max_resistance = tr_seconds / (0.8473 * cb_farads);
    
    // Check if valid range exists
    result.is_valid = (result.max_resistance > result.min_resistance);
    
    // Calculate recommended value (geometric mean of min and max)
    if (result.is_valid) {
        result.recommended = sqrt(result.min_resistance * result.max_resistance);
        
        // Round to nearest standard resistor value (E12 series)
        float e12_values[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
        float magnitude = pow(10, floor(log10(result.recommended)));
        float normalized = result.recommended / magnitude;
        
        float closest = e12_values[0];
        float min_diff = fabs(normalized - closest);
        
        for (int i = 1; i < 12; i++) {
            float diff = fabs(normalized - e12_values[i]);
            if (diff < min_diff) {
                min_diff = diff;
                closest = e12_values[i];
            }
        }
        
        result.recommended = closest * magnitude;
    }
    
    return result;
}

// Print results
void print_results(const i2c_config_t* config, const resistor_result_t* result) {
    printf("\n=== I2C Pull-up Resistor Calculation ===\n\n");
    printf("Configuration:\n");
    printf("  Supply Voltage (VCC): %.2f V\n", config->vcc);
    printf("  Bus Capacitance: %.1f pF\n", config->bus_capacitance);
    printf("  Speed Mode: %s\n", get_speed_mode_name(config->mode));
    printf("  Max Rise Time: %.0f ns\n\n", get_max_rise_time(config->mode));
    
    printf("Results:\n");
    printf("  Minimum Resistance: %.0f Ω (%.2f kΩ)\n", 
           result->min_resistance, result->min_resistance / 1000.0);
    printf("  Maximum Resistance: %.0f Ω (%.2f kΩ)\n", 
           result->max_resistance, result->max_resistance / 1000.0);
    
    if (result->is_valid) {
        printf("  Recommended Value: %.0f Ω (%.2f kΩ)\n", 
               result->recommended, result->recommended / 1000.0);
        printf("\n  ✓ Valid resistor range found!\n");
    } else {
        printf("\n  ✗ WARNING: No valid resistor range!\n");
        printf("    Bus capacitance too high or speed too fast.\n");
        printf("    Consider: reducing capacitance, lowering speed, or using active pull-ups.\n");
    }
}

int main(void) {
    // Example 1: Standard mode with typical conditions
    i2c_config_t config1 = {
        .vcc = 3.3,
        .vol_max = 0.4,
        .iol = 0.003,              // 3 mA
        .bus_capacitance = 100.0,  // 100 pF
        .mode = I2C_STANDARD_MODE
    };
    
    resistor_result_t result1 = calculate_pullup_resistor(&config1);
    print_results(&config1, &result1);
    
    // Example 2: Fast mode with higher capacitance
    printf("\n\n");
    i2c_config_t config2 = {
        .vcc = 5.0,
        .vol_max = 0.4,
        .iol = 0.003,
        .bus_capacitance = 300.0,  // 300 pF
        .mode = I2C_FAST_MODE
    };
    
    resistor_result_t result2 = calculate_pullup_resistor(&config2);
    print_results(&config2, &result2);
    
    // Example 3: Problematic case - too much capacitance
    printf("\n\n");
    i2c_config_t config3 = {
        .vcc = 3.3,
        .vol_max = 0.4,
        .iol = 0.003,
        .bus_capacitance = 500.0,  // High capacitance
        .mode = I2C_FAST_MODE_PLUS
    };
    
    resistor_result_t result3 = calculate_pullup_resistor(&config3);
    print_results(&config3, &result3);
    
    return 0;
}
```

### Arduino/ESP32 Runtime Calculator

```
// Arduino/ESP32 I2C Pull-up Resistor Calculator and Validator
// This can help diagnose pull-up issues in real-time

#include <Wire.h>

class I2CPullupCalculator {
private:
    float vcc;
    float busCapacitance;
    uint32_t frequency;
    
    // Get max rise time based on frequency
    float getMaxRiseTime() {
        if (frequency <= 100000) return 1000e-9;      // Standard: 1000ns
        if (frequency <= 400000) return 300e-9;       // Fast: 300ns
        if (frequency <= 1000000) return 120e-9;      // Fast+: 120ns
        return 80e-9;                                  // High speed: ~80ns
    }
    
public:
    I2CPullupCalculator(float supplyVoltage, float capacitance_pF, uint32_t freq_Hz) 
        : vcc(supplyVoltage), busCapacitance(capacitance_pF), frequency(freq_Hz) {}
    
    void calculate() {
        Serial.println("\n=== I2C Pull-up Resistor Calculator ===");
        Serial.print("VCC: "); Serial.print(vcc); Serial.println(" V");
        Serial.print("Bus Capacitance: "); Serial.print(busCapacitance); Serial.println(" pF");
        Serial.print("I2C Frequency: "); Serial.print(frequency / 1000.0); Serial.println(" kHz");
        
        // Minimum resistance: (VCC - VOL) / IOL
        float volMax = 0.4;  // V
        float iol = 0.003;   // 3mA
        float rMin = (vcc - volMax) / iol;
        
        // Maximum resistance: tr / (0.8473 × Cb)
        float trSeconds = getMaxRiseTime();
        float cbFarads = busCapacitance * 1e-12;
        float rMax = trSeconds / (0.8473 * cbFarads);
        
        Serial.println("\nResults:");
        Serial.print("  Min Resistance: "); 
        Serial.print(rMin); Serial.print(" Ω (");
        Serial.print(rMin / 1000.0, 2); Serial.println(" kΩ)");
        
        Serial.print("  Max Resistance: ");
        Serial.print(rMax); Serial.print(" Ω (");
        Serial.print(rMax / 1000.0, 2); Serial.println(" kΩ)");
        
        if (rMax > rMin) {
            float recommended = sqrt(rMin * rMax);
            Serial.print("  Recommended: ");
            Serial.print(recommended); Serial.print(" Ω (");
            Serial.print(recommended / 1000.0, 2); Serial.println(" kΩ)");
            
            // Suggest standard values
            Serial.println("\n  Standard resistor suggestions:");
            if (recommended >= 2000 && recommended <= 3000) Serial.println("    → 2.2 kΩ (E12)");
            if (recommended >= 3000 && recommended <= 5000) Serial.println("    → 4.7 kΩ (E12)");
            if (recommended >= 5000 && recommended <= 12000) Serial.println("    → 10 kΩ (E12)");
        } else {
            Serial.println("  ✗ ERROR: No valid range!");
            Serial.println("    Try: Lower speed, reduce capacitance, or use active pull-ups");
        }
    }
    
    // Measure actual rise time (requires oscilloscope or logic analyzer)
    // This demonstrates the concept - actual implementation needs hardware timing
    void estimateRiseTime() {
        Serial.println("\n=== Rise Time Estimation ===");
        Serial.println("Connect oscilloscope to SDA/SCL to measure actual rise time.");
        Serial.println("Expected max rise time: ");
        Serial.print(getMaxRiseTime() * 1e9);
        Serial.println(" ns");
    }
};

// Estimate bus capacitance based on setup
float estimateBusCapacitance(int numDevices, float wireLength_cm) {
    // Rough estimates:
    // - Each device: ~5-10 pF
    // - Wire/trace: ~1-2 pF per cm
    float deviceCap = numDevices * 7.5;  // pF
    float wireCap = wireLength_cm * 1.5; // pF
    float pcbCap = 20.0;                  // pF (parasitic)
    
    return deviceCap + wireCap + pcbCap;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("I2C Pull-up Resistor Calculator");
    Serial.println("================================\n");
    
    // Example 1: Typical Arduino setup
    Serial.println("Example 1: Typical Arduino 3.3V setup");
    float cap1 = estimateBusCapacitance(2, 10.0); // 2 devices, 10cm wire
    I2CPullupCalculator calc1(3.3, cap1, 100000);
    calc1.calculate();
    
    // Example 2: ESP32 Fast Mode
    Serial.println("\n\nExample 2: ESP32 Fast Mode (400 kHz)");
    float cap2 = estimateBusCapacitance(3, 15.0); // 3 devices, 15cm wire
    I2CPullupCalculator calc2(3.3, cap2, 400000);
    calc2.calculate();
    
    // Example 3: 5V Arduino with many devices
    Serial.println("\n\nExample 3: 5V system with multiple devices");
    float cap3 = estimateBusCapacitance(5, 20.0); // 5 devices, 20cm wire
    I2CPullupCalculator calc3(5.0, cap3, 100000);
    calc3.calculate();
    
    // Initialize I2C with calculated frequency
    Wire.begin();
    Wire.setClock(100000); // Use your calculated frequency
    
    Serial.println("\n\nI2C initialized. Scanner starting...\n");
}

void loop() {
    // I2C bus scanner to verify pull-ups are working
    static unsigned long lastScan = 0;
    
    if (millis() - lastScan > 5000) { // Scan every 5 seconds
        lastScan = millis();
        
        Serial.println("Scanning I2C bus...");
        int deviceCount = 0;
        
        for (byte addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            byte error = Wire.endTransmission();
            
            if (error == 0) {
                Serial.print("  Device found at 0x");
                if (addr < 16) Serial.print("0");
                Serial.println(addr, HEX);
                deviceCount++;
            }
        }
        
        if (deviceCount == 0) {
            Serial.println("  No devices found!");
            Serial.println("  Check: Pull-up resistors, wiring, device power");
        } else {
            Serial.print("  Total devices: ");
            Serial.println(deviceCount);
        }
        Serial.println();
    }
}
```

## Rust Code Examples

### Pull-up Resistor Calculator Library

```rust

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
```

### Embedded Rust Example (no_std)

```rust
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
```

## Practical Considerations

### Common Resistor Values and Their Applications

**2.2 kΩ**
- Fast Mode (400 kHz) with low capacitance
- Short traces, few devices
- Higher current consumption

**4.7 kΩ** (Most Common)
- Standard Mode (100 kHz) 
- Fast Mode with moderate capacitance
- Good balance of speed and power

**10 kΩ**
- Standard Mode with higher capacitance
- Low power applications
- Slower rise times acceptable

### Troubleshooting Guide

**Symptoms of incorrect pull-ups:**

1. **No pull-ups or values too high:**
   - Communication fails completely
   - Bus stuck LOW
   - Slow rise times cause bit errors

2. **Pull-ups too low:**
   - Excessive current draw
   - Device unable to pull line LOW
   - Overheating of pull-up resistors

3. **Mismatched rise/fall times:**
   - Intermittent errors
   - Works at low speed, fails at high speed
   - Clock stretching issues

### Active Pull-ups Alternative

For very high-speed or high-capacitance applications where passive resistors don't work:

```c
// Active pull-up concept (requires additional transistors)
// Provides stronger pull-up during rising edge
// Switches to weaker pull-up during steady state
// Typically implemented with dedicated I2C bus buffer ICs
```

This comprehensive guide shows how to properly calculate and implement I2C pull-up resistors across different programming environments, ensuring reliable communication at your desired bus speed!