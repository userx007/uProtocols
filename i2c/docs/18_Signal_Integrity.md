# I2C Signal Integrity: Noise, Crosstalk, EMI & Mitigation

## Overview

Signal integrity is critical for reliable I2C communication, especially in noisy industrial environments, long-distance communications, or high-speed applications. This guide covers noise immunity, crosstalk, electromagnetic interference (EMI), and practical mitigation strategies.

---

## 1. Fundamental Signal Integrity Challenges

### 1.1 Open-Drain Architecture Vulnerabilities

I2C uses open-drain outputs with pull-up resistors, making it susceptible to:
- **Slow rising edges** - vulnerable to noise during transitions
- **External interference** - coupled noise can be mistaken for valid signals
- **Ground bounce** - multiple devices switching simultaneously
- **Reflections** - impedance mismatches on longer buses

### 1.2 Common Noise Sources

```
External Sources          Internal Sources
├─ EMI from motors       ├─ Power supply noise
├─ Switching regulators  ├─ Digital switching noise
├─ RF transmitters       ├─ Ground loops
├─ Relay coils           └─ Crosstalk from adjacent traces
└─ Fluorescent lights
```

---

## 2. Noise Immunity Techniques

### 2.1 Hardware Filtering

**RC Low-Pass Filter on SDA/SCL:**

```c
/*
 * Hardware Configuration:
 * 
 *    VCC
 *     |
 *   [Rpu] Pull-up (2.2kΩ - 4.7kΩ)
 *     |
 *     +---- SDA/SCL line
 *     |
 *   [Rf]  Series resistor (100Ω - 330Ω)
 *     |
 *     +----[Cf] Capacitor to GND (100pF - 1nF)
 *     |
 *   Device
 *
 * Cutoff frequency: fc = 1 / (2π × Rf × Cf)
 * Example: 330Ω × 220pF ≈ 2.2 MHz
 */
```

### 2.2 Software Noise Filtering (C/C++)

```c
#include <stdint.h>
#include <stdbool.h>

// Debounce I2C lines to reject glitches
typedef struct {
    uint8_t sda_history;
    uint8_t scl_history;
    uint8_t stable_threshold;
} I2CNoiseFilter_t;

void i2c_filter_init(I2CNoiseFilter_t *filter, uint8_t threshold) {
    filter->sda_history = 0xFF;
    filter->scl_history = 0xFF;
    filter->stable_threshold = threshold; // e.g., 3 consecutive samples
}

bool i2c_read_sda_filtered(I2CNoiseFilter_t *filter, bool raw_sda) {
    // Shift history and add new sample
    filter->sda_history = (filter->sda_history << 1) | (raw_sda ? 1 : 0);
    
    // Line is HIGH only if last N samples are all HIGH
    uint8_t mask = (1 << filter->stable_threshold) - 1;
    return (filter->sda_history & mask) == mask;
}

bool i2c_read_scl_filtered(I2CNoiseFilter_t *filter, bool raw_scl) {
    filter->scl_history = (filter->scl_history << 1) | (raw_scl ? 1 : 0);
    
    uint8_t mask = (1 << filter->stable_threshold) - 1;
    return (filter->scl_history & mask) == mask;
}

// Usage example
void i2c_transaction_with_filtering(void) {
    I2CNoiseFilter_t filter;
    i2c_filter_init(&filter, 3); // Require 3 stable samples
    
    // In your bit-banged I2C read loop:
    for (int i = 0; i < 100; i++) {
        bool raw_sda = read_sda_pin();
        bool filtered_sda = i2c_read_sda_filtered(&filter, raw_sda);
        // Use filtered_sda for decisions
        delay_us(1); // Sample at high rate
    }
}
```

### 2.3 Rust Implementation with Digital Filtering

```rust
/// Digital noise filter for I2C lines
pub struct I2CNoiseFilter {
    sda_history: u8,
    scl_history: u8,
    stable_threshold: u8,
}

impl I2CNoiseFilter {
    pub fn new(stable_threshold: u8) -> Self {
        Self {
            sda_history: 0xFF,
            scl_history: 0xFF,
            stable_threshold,
        }
    }
    
    /// Read SDA with glitch filtering
    pub fn read_sda_filtered(&mut self, raw_sda: bool) -> bool {
        // Shift in new sample
        self.sda_history = (self.sda_history << 1) | (raw_sda as u8);
        
        // Check if last N samples are stable HIGH
        let mask = (1 << self.stable_threshold) - 1;
        (self.sda_history & mask) == mask
    }
    
    /// Read SCL with glitch filtering
    pub fn read_scl_filtered(&mut self, raw_scl: bool) -> bool {
        self.scl_history = (self.scl_history << 1) | (raw_scl as u8);
        let mask = (1 << self.stable_threshold) - 1;
        (self.scl_history & mask) == mask
    }
    
    /// Reset filter state
    pub fn reset(&mut self) {
        self.sda_history = 0xFF;
        self.scl_history = 0xFF;
    }
}

// Example usage with embedded-hal
use embedded_hal::digital::v2::InputPin;

pub fn i2c_read_byte_filtered<SDA, SCL>(
    sda: &SDA,
    scl: &SCL,
    filter: &mut I2CNoiseFilter,
) -> Result<u8, ()>
where
    SDA: InputPin,
    SCL: InputPin,
{
    let mut byte = 0u8;
    
    for bit_pos in (0..8).rev() {
        // Wait for clock to go high (with filtering)
        while !filter.read_scl_filtered(scl.is_high().map_err(|_| ())?) {
            // Timeout mechanism should be added here
        }
        
        // Read data bit (with filtering)
        if filter.read_sda_filtered(sda.is_high().map_err(|_| ())?) {
            byte |= 1 << bit_pos;
        }
        
        // Wait for clock to go low
        while filter.read_scl_filtered(scl.is_high().map_err(|_| ())?) {
            // Timeout mechanism should be added here
        }
    }
    
    Ok(byte)
}
```

---

## 3. Crosstalk Mitigation

### 3.1 PCB Layout Best Practices

```
Poor Layout (High Crosstalk):
┌─────────────────────────┐
│ SDA ═══════════════════ │
│ SCL ═══════════════════ │  <- Parallel routing
│ GPIO ══════════════════ │
└─────────────────────────┘

Good Layout (Low Crosstalk):
┌─────────────────────────┐
│ SDA ═══════════════════ │
│ GND ───────────────────  │  <- Ground trace between
│ SCL ═══════════════════ │
│ GND ───────────────────  │
│ GPIO ══════════════════ │
└─────────────────────────┘
```

### 3.2 Guard Traces Implementation

```c
/*
 * PCB Design Rules for Crosstalk Reduction:
 * 
 * 1. Spacing: Maintain 3x trace width spacing between I2C and noisy signals
 * 2. Ground guards: Route GND traces between I2C and high-speed signals
 * 3. Layer assignment: Place I2C on layers adjacent to ground planes
 * 4. Trace length matching: Keep SDA and SCL lengths within 10mm
 * 5. Perpendicular crossings: Cross noisy signals at 90° when necessary
 */
```

### 3.3 Crosstalk Detection in Software (C++)

```cpp
#include <cstdint>
#include <array>

class I2CCrosstalkDetector {
private:
    static constexpr size_t WINDOW_SIZE = 16;
    std::array<bool, WINDOW_SIZE> sda_transitions;
    std::array<bool, WINDOW_SIZE> scl_transitions;
    size_t index = 0;
    
public:
    // Detect simultaneous transitions (potential crosstalk)
    bool detect_crosstalk(bool sda_edge, bool scl_edge) {
        sda_transitions[index] = sda_edge;
        scl_transitions[index] = scl_edge;
        
        // Count simultaneous transitions in window
        size_t simultaneous = 0;
        for (size_t i = 0; i < WINDOW_SIZE; i++) {
            if (sda_transitions[i] && scl_transitions[i]) {
                simultaneous++;
            }
        }
        
        index = (index + 1) % WINDOW_SIZE;
        
        // Threshold: >30% simultaneous transitions indicates crosstalk
        return simultaneous > (WINDOW_SIZE * 3 / 10);
    }
    
    void reset() {
        sda_transitions.fill(false);
        scl_transitions.fill(false);
        index = 0;
    }
};

// Usage in I2C driver
void monitor_bus_quality() {
    I2CCrosstalkDetector detector;
    
    bool prev_sda = read_sda();
    bool prev_scl = read_scl();
    
    while (true) {
        bool curr_sda = read_sda();
        bool curr_scl = read_scl();
        
        bool sda_edge = (curr_sda != prev_sda);
        bool scl_edge = (curr_scl != prev_scl);
        
        if (detector.detect_crosstalk(sda_edge, scl_edge)) {
            log_warning("Possible crosstalk detected on I2C bus");
            // Implement recovery or signal conditioning
        }
        
        prev_sda = curr_sda;
        prev_scl = curr_scl;
        
        delay_us(10);
    }
}
```

---

## 4. EMI Considerations

### 4.1 Common Mode Choke Implementation

```c
/*
 * Hardware: Common Mode Choke on I2C lines
 * 
 *     Device A                    Common Mode              Device B
 *                                    Choke
 *   SDA ────────●───────────────●─────────●───────────────● SDA
 *               │               │ ┌─────┐ │               │
 *               │               └─┤  L  ├─┘               │
 *   SCL ────────●───────────────●─┤     ├─●───────────────● SCL
 *                               └─┤  L  ├─┘
 *                                 └─────┘
 * 
 * Recommended: 10µH - 100µH common mode choke
 * Benefit: Blocks common-mode EMI while allowing differential signals
 */

#define CMC_INDUCTANCE_UH  47  // 47µH typical
#define CMC_DCR_OHM        0.5 // Maximum DC resistance
```

### 4.2 Shielded Cable Configuration

```c
/*
 * Shielded Twisted Pair for I2C over cables
 * 
 *   Device A          Shielded Cable              Device B
 *   ┌──────┐         ╔════════════════╗          ┌──────┐
 *   │ SDA  ├─────────╫─→ Twisted  ←─→─╫──────────┤ SDA  │
 *   │ SCL  ├─────────╫─→ Pair     ←─→─╫──────────┤ SCL  │
 *   │ GND  ├────┬────╫─→ GND wire ←─→─╫────┬─────┤ GND  │
 *   └──────┘    │    ║                ║    │     └──────┘
 *              [R]   ║  Shield (drain)║   [R]
 *               │    ╚════════╪═══════╝    │
 *              GND            └────────────GND
 * 
 * Shield grounding: Ground at ONE end only (avoid ground loops)
 * Drain wire: 100Ω resistor to ground
 */

// Configuration verification
typedef struct {
    bool shield_grounded_at_master;
    bool drain_resistor_installed;
    float cable_length_meters;
    float max_capacitance_pf_per_meter;
} I2CCableConfig_t;

bool validate_cable_emi(I2CCableConfig_t *cfg) {
    // Check total capacitance doesn't exceed bus limits
    float total_cap = cfg->cable_length_meters * cfg->max_capacitance_pf_per_meter;
    
    if (total_cap > 400.0f) { // 400pF typical I2C limit
        return false;
    }
    
    // Verify proper shielding
    if (!cfg->shield_grounded_at_master && !cfg->drain_resistor_installed) {
        return false; // Floating shield - bad EMI performance
    }
    
    return true;
}
```

### 4.3 EMI Filtering with Ferrite Beads (Rust)

```rust
/// EMI suppression configuration for I2C
pub struct EmiSuppressionConfig {
    /// Ferrite bead impedance at 100MHz (Ω)
    pub ferrite_impedance_100mhz: u16,
    /// Common mode choke inductance (µH)
    pub cmc_inductance_uh: u16,
    /// Filter capacitance (pF)
    pub filter_cap_pf: u16,
}

impl EmiSuppressionConfig {
    /// Standard configuration for moderate EMI environments
    pub fn standard() -> Self {
        Self {
            ferrite_impedance_100mhz: 600,  // 600Ω @ 100MHz
            cmc_inductance_uh: 47,
            filter_cap_pf: 220,
        }
    }
    
    /// Heavy-duty configuration for harsh EMI environments
    pub fn heavy_duty() -> Self {
        Self {
            ferrite_impedance_100mhz: 1000,
            cmc_inductance_uh: 100,
            filter_cap_pf: 470,
        }
    }
    
    /// Validate configuration against I2C speed requirements
    pub fn validate_for_speed(&self, speed_khz: u32) -> Result<(), &'static str> {
        // Calculate cutoff frequency
        let r_ohm = 330.0; // Series resistor
        let c_f = self.filter_cap_pf as f32 * 1e-12;
        let fc_hz = 1.0 / (2.0 * 3.14159 * r_ohm * c_f);
        
        // Cutoff should be 5-10x above I2C clock frequency
        let required_fc = (speed_khz as f32 * 1000.0) * 5.0;
        
        if fc_hz < required_fc {
            return Err("Filter cutoff too low for I2C speed");
        }
        
        Ok(())
    }
}

// Example: Configure I2C with EMI suppression
pub fn setup_i2c_with_emi_protection(speed_khz: u32) -> Result<(), &'static str> {
    let emi_config = if speed_khz <= 100 {
        EmiSuppressionConfig::heavy_duty()
    } else {
        EmiSuppressionConfig::standard()
    };
    
    emi_config.validate_for_speed(speed_khz)?;
    
    // Configure hardware (pseudo-code)
    // configure_ferrite_beads(emi_config.ferrite_impedance_100mhz);
    // configure_common_mode_choke(emi_config.cmc_inductance_uh);
    // configure_filter_caps(emi_config.filter_cap_pf);
    
    Ok(())
}
```

---

## 5. Advanced Mitigation Strategies

### 5.1 Adaptive Pull-Up Resistors (C)

```c
#include <stdint.h>

// Dynamic pull-up adjustment based on bus conditions
typedef enum {
    PULLUP_STRONG = 0,   // 1kΩ (fast rise, more power)
    PULLUP_MEDIUM = 1,   // 2.2kΩ (balanced)
    PULLUP_WEAK = 2,     // 4.7kΩ (slow rise, less power)
} PullupStrength_t;

typedef struct {
    uint32_t rise_time_ns;
    uint32_t bus_capacitance_pf;
    uint32_t noise_margin_mv;
} I2CBusMetrics_t;

PullupStrength_t calculate_optimal_pullup(I2CBusMetrics_t *metrics) {
    // Target rise time: 300ns for standard mode, 120ns for fast mode
    uint32_t target_rise_ns = 300;
    
    if (metrics->rise_time_ns > target_rise_ns * 1.5) {
        // Rise time too slow, increase pull-up strength
        return PULLUP_STRONG;
    } else if (metrics->rise_time_ns < target_rise_ns * 0.7) {
        // Rise time fast enough, reduce power consumption
        return PULLUP_WEAK;
    } else if (metrics->noise_margin_mv < 500) {
        // Low noise margin, use stronger pull-ups
        return PULLUP_STRONG;
    }
    
    return PULLUP_MEDIUM;
}

// Hardware control (example with GPIO-controlled MOSFETs)
void set_pullup_strength(PullupStrength_t strength) {
    switch (strength) {
        case PULLUP_STRONG:
            gpio_set(PULLUP_1K_ENABLE);
            gpio_clear(PULLUP_2K2_ENABLE);
            gpio_clear(PULLUP_4K7_ENABLE);
            break;
        case PULLUP_MEDIUM:
            gpio_clear(PULLUP_1K_ENABLE);
            gpio_set(PULLUP_2K2_ENABLE);
            gpio_clear(PULLUP_4K7_ENABLE);
            break;
        case PULLUP_WEAK:
            gpio_clear(PULLUP_1K_ENABLE);
            gpio_clear(PULLUP_2K2_ENABLE);
            gpio_set(PULLUP_4K7_ENABLE);
            break;
    }
}
```

### 5.2 Error Detection and Recovery (C++)

```cpp
#include <cstdint>
#include <cstring>

class I2CSignalIntegrityMonitor {
private:
    uint32_t error_count = 0;
    uint32_t total_transactions = 0;
    uint32_t consecutive_errors = 0;
    
    static constexpr uint32_t ERROR_THRESHOLD = 10;
    static constexpr uint32_t RECOVERY_THRESHOLD = 3;
    
public:
    enum class ErrorType {
        NONE,
        NOISE_GLITCH,
        BUS_STUCK,
        ARBITRATION_LOST,
        TIMEOUT,
        SIGNAL_INTEGRITY_FAIL
    };
    
    void record_transaction(bool success, ErrorType error = ErrorType::NONE) {
        total_transactions++;
        
        if (!success) {
            error_count++;
            consecutive_errors++;
            
            if (consecutive_errors >= RECOVERY_THRESHOLD) {
                initiate_bus_recovery();
            }
        } else {
            consecutive_errors = 0;
        }
    }
    
    float get_error_rate() const {
        return total_transactions > 0 
            ? static_cast<float>(error_count) / total_transactions 
            : 0.0f;
    }
    
    bool needs_signal_conditioning() const {
        return get_error_rate() > 0.05f; // >5% error rate
    }
    
private:
    void initiate_bus_recovery() {
        // Send 9 clock pulses to clear stuck SDA
        for (int i = 0; i < 9; i++) {
            toggle_scl();
            delay_us(5);
        }
        
        // Send STOP condition
        send_stop_condition();
        
        // Reset error counter after recovery
        consecutive_errors = 0;
    }
    
    void toggle_scl() {
        // Hardware-specific SCL toggling
    }
    
    void send_stop_condition() {
        // Hardware-specific STOP condition
    }
};

// Usage example
void i2c_transaction_with_monitoring() {
    I2CSignalIntegrityMonitor monitor;
    
    for (int i = 0; i < 1000; i++) {
        bool success = perform_i2c_transfer();
        monitor.record_transaction(success);
        
        if (monitor.needs_signal_conditioning()) {
            log_warning("High error rate detected - check signal integrity");
            // Reduce speed, increase filtering, check hardware
        }
    }
}
```

### 5.3 Comprehensive Signal Quality Assessment (Rust)

```rust
use core::fmt;

/// Comprehensive I2C signal quality metrics
#[derive(Debug, Clone)]
pub struct SignalQualityMetrics {
    pub rise_time_ns: u32,
    pub fall_time_ns: u32,
    pub undershoot_mv: u16,
    pub overshoot_mv: u16,
    pub noise_pp_mv: u16,
    pub error_rate: f32,
}

impl SignalQualityMetrics {
    /// Assess overall signal quality
    pub fn assess_quality(&self) -> SignalQuality {
        let mut score = 100u8;
        
        // Rise time penalty (should be < 1000ns for standard mode)
        if self.rise_time_ns > 1000 {
            score = score.saturating_sub(20);
        } else if self.rise_time_ns > 300 {
            score = score.saturating_sub(10);
        }
        
        // Overshoot/undershoot penalty (should be < 10% of VDD)
        if self.overshoot_mv > 330 || self.undershoot_mv > 330 {
            score = score.saturating_sub(15);
        }
        
        // Noise penalty (should be < 100mVpp)
        if self.noise_pp_mv > 200 {
            score = score.saturating_sub(25);
        } else if self.noise_pp_mv > 100 {
            score = score.saturating_sub(10);
        }
        
        // Error rate penalty
        if self.error_rate > 0.05 {
            score = score.saturating_sub(30);
        } else if self.error_rate > 0.01 {
            score = score.saturating_sub(15);
        }
        
        match score {
            90..=100 => SignalQuality::Excellent,
            70..=89 => SignalQuality::Good,
            50..=69 => SignalQuality::Fair,
            30..=49 => SignalQuality::Poor,
            _ => SignalQuality::Critical,
        }
    }
    
    /// Generate recommendations for improvement
    pub fn get_recommendations(&self) -> Vec<&'static str> {
        let mut recommendations = Vec::new();
        
        if self.rise_time_ns > 300 {
            recommendations.push("Reduce pull-up resistor value");
            recommendations.push("Check bus capacitance (max 400pF)");
        }
        
        if self.overshoot_mv > 330 || self.undershoot_mv > 330 {
            recommendations.push("Add series termination resistors");
            recommendations.push("Check trace impedance matching");
        }
        
        if self.noise_pp_mv > 100 {
            recommendations.push("Add RC filtering on SDA/SCL");
            recommendations.push("Implement ground guards on PCB");
            recommendations.push("Use shielded cables for long runs");
        }
        
        if self.error_rate > 0.01 {
            recommendations.push("Enable software noise filtering");
            recommendations.push("Reduce I2C clock speed");
            recommendations.push("Check for EMI sources nearby");
        }
        
        recommendations
    }
}

#[derive(Debug, PartialEq)]
pub enum SignalQuality {
    Excellent,
    Good,
    Fair,
    Poor,
    Critical,
}

impl fmt::Display for SignalQuality {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SignalQuality::Excellent => write!(f, "Excellent (90-100)"),
            SignalQuality::Good => write!(f, "Good (70-89)"),
            SignalQuality::Fair => write!(f, "Fair (50-69)"),
            SignalQuality::Poor => write!(f, "Poor (30-49)"),
            SignalQuality::Critical => write!(f, "Critical (<30)"),
        }
    }
}

// Example usage
pub fn monitor_and_report_signal_quality() {
    let metrics = SignalQualityMetrics {
        rise_time_ns: 450,
        fall_time_ns: 120,
        undershoot_mv: 150,
        overshoot_mv: 200,
        noise_pp_mv: 80,
        error_rate: 0.02,
    };
    
    let quality = metrics.assess_quality();
    println!("Signal Quality: {}", quality);
    
    let recommendations = metrics.get_recommendations();
    if !recommendations.is_empty() {
        println!("Recommendations:");
        for rec in recommendations {
            println!("  - {}", rec);
        }
    }
}
```

---

## 6. Testing and Validation

### 6.1 Signal Integrity Test Suite (C)

```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char test_name[64];
    bool (*test_func)(void);
    bool passed;
} SignalIntegrityTest_t;

// Test 1: Rise time measurement
bool test_rise_time(void) {
    // Toggle SDA and measure rise time
    uint32_t start_time, end_time;
    
    gpio_set_low(SDA_PIN);
    delay_us(10);
    
    gpio_set_high_impedance(SDA_PIN); // Release to pull-up
    start_time = get_timer_ns();
    
    while (gpio_read(SDA_PIN) < VIH_THRESHOLD) {
        if (get_timer_ns() - start_time > 2000) { // 2µs timeout
            return false;
        }
    }
    
    end_time = get_timer_ns();
    uint32_t rise_time = end_time - start_time;
    
    // Standard mode: max 1000ns, Fast mode: max 300ns
    return rise_time < 1000;
}

// Test 2: Noise margin verification
bool test_noise_margin(void) {
    uint16_t vil_max_mv = 900;  // 0.3 * VDD for VDD=3.3V
    uint16_t vih_min_mv = 2310; // 0.7 * VDD for VDD=3.3V
    
    // Measure actual thresholds with noise
    uint16_t measured_low = measure_low_level_with_noise();
    uint16_t measured_high = measure_high_level_with_noise();
    
    return (measured_low < vil_max_mv) && (measured_high > vih_min_mv);
}

// Test 3: Bus capacitance estimation
bool test_bus_capacitance(void) {
    // C = (t_rise * I_pullup) / (0.7 * VDD)
    // Assume 3.3V, 2.2kΩ pull-up
    
    uint32_t rise_time_ns = measure_actual_rise_time();
    float current_ma = 3.3f / 2.2f; // VDD / R_pullup
    float capacitance_pf = (rise_time_ns * current_ma) / (0.7f * 3.3f);
    
    return capacitance_pf < 400.0f; // I2C spec limit
}

// Run all tests
void run_signal_integrity_tests(void) {
    SignalIntegrityTest_t tests[] = {
        {"Rise Time Test", test_rise_time, false},
        {"Noise Margin Test", test_noise_margin, false},
        {"Bus Capacitance Test", test_bus_capacitance, false},
    };
    
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    
    for (int i = 0; i < num_tests; i++) {
        tests[i].passed = tests[i].test_func();
        if (tests[i].passed) {
            passed++;
            printf("[PASS] %s\n", tests[i].test_name);
        } else {
            printf("[FAIL] %s\n", tests[i].test_name);
        }
    }
    
    printf("\nResults: %d/%d tests passed\n", passed, num_tests);
}
```

---

## 7. Summary and Best Practices

### Key Takeaways

1. **Hardware Foundation**
   - Use proper pull-up resistors (calculate based on bus capacitance)
   - Implement RC filtering for noise suppression
   - Add ferrite beads and common mode chokes for EMI

2. **PCB Design**
   - Minimize trace length and parallel routing
   - Use ground guards between I2C and noisy signals
   - Keep SDA/SCL traces impedance-matched

3. **Software Techniques**
   - Implement digital filtering for glitch rejection
   - Monitor signal quality metrics
   - Adaptive error recovery mechanisms

4. **Testing**
   - Measure rise/fall times
   - Verify noise margins
   - Validate under worst-case conditions (max capacitance, EMI)

### Quick Reference: Mitigation Priority

| Priority | Issue | Solution |
|----------|-------|----------|
| 🔴 High | Slow rise time | Reduce pull-up resistor, reduce bus capacitance |
| 🔴 High | Bus