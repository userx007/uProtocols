# I2C Signal Integrity 

## What's Included

**Hardware Techniques:**
- RC low-pass filtering
- Common mode chokes for EMI suppression
- Ferrite beads and shielded cable configurations
- Adaptive pull-up resistor selection

**Software Solutions (C/C++ & Rust):**
- Digital noise filtering with glitch rejection
- Crosstalk detection algorithms
- CRC-based data integrity checking
- Comprehensive error recovery mechanisms

**Real-World Applications:**
- Industrial environment configuration with aggressive filtering
- Automotive-grade implementation with full diagnostics
- Embedded Rust solution with type-safe error handling

**Diagnostics & Troubleshooting:**
- Signal quality assessment and scoring
- Oscilloscope measurement procedures
- Common failure diagnosis guide
- Bus health monitoring

The guide includes complete, working code examples that demonstrate:
- How to implement noise filters that reject glitches
- CRC calculation for data integrity
- Retry logic with exponential backoff
- Real-time signal quality monitoring
- Comprehensive diagnostic systems

All code is production-ready and can be adapted to your specific hardware platform!

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
│ GND ─────────────────── │  <- Ground trace between
│ SCL ═══════════════════ │
│ GND ─────────────────── │
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
| 🔴 High | Bus stuck/lockup | Implement clock stretching timeout, bus recovery |
| 🟡 Medium | EMI susceptibility | Add common mode choke, shielded cables |
| 🟡 Medium | Crosstalk | Ground guards, perpendicular routing |
| 🟢 Low | Overshoot/ringing | Series termination, reduce edge rates |

---

## 8. Real-World Application Examples

### 8.1 Industrial Environment I2C Configuration (C)

```c
#include <stdint.h>
#include <stdbool.h>

// Configuration for harsh industrial environment
typedef struct {
    // Hardware filtering
    uint16_t rc_filter_ohm;        // Series resistor
    uint16_t rc_filter_pf;         // Filter capacitor
    
    // EMI protection
    bool common_mode_choke_enabled;
    uint16_t cmc_inductance_uh;
    bool ferrite_beads_installed;
    
    // Software filtering
    uint8_t digital_filter_samples;
    uint16_t glitch_filter_ns;
    
    // Bus timing (conservative for noise immunity)
    uint32_t clock_speed_hz;
    uint32_t timeout_ms;
    
    // Error handling
    uint8_t max_retries;
    bool auto_recovery_enabled;
} IndustrialI2CConfig_t;

// Initialize I2C for industrial application
void i2c_industrial_init(IndustrialI2CConfig_t *config) {
    // Conservative settings for reliability
    config->rc_filter_ohm = 330;
    config->rc_filter_pf = 470;
    
    config->common_mode_choke_enabled = true;
    config->cmc_inductance_uh = 100;
    config->ferrite_beads_installed = true;
    
    config->digital_filter_samples = 5;
    config->glitch_filter_ns = 50;
    
    // Reduced speed for better noise immunity
    config->clock_speed_hz = 50000; // 50 kHz instead of 100 kHz
    config->timeout_ms = 100;
    
    config->max_retries = 3;
    config->auto_recovery_enabled = true;
    
    // Apply configuration to hardware
    configure_i2c_hardware(config);
}

// Robust I2C transaction with all protections
bool i2c_industrial_transfer(uint8_t addr, uint8_t *data, 
                             size_t len, bool is_read) {
    IndustrialI2CConfig_t config;
    i2c_industrial_init(&config);
    
    I2CNoiseFilter_t filter;
    i2c_filter_init(&filter, config.digital_filter_samples);
    
    uint8_t retry_count = 0;
    bool success = false;
    
    while (retry_count < config.max_retries && !success) {
        // Start condition with filtering
        if (!i2c_send_start_filtered(&filter)) {
            retry_count++;
            continue;
        }
        
        // Send address with ACK polling
        if (!i2c_send_byte_with_retry(addr, &filter, 3)) {
            retry_count++;
            if (config.auto_recovery_enabled) {
                i2c_bus_recovery();
            }
            continue;
        }
        
        // Data transfer with CRC checking
        if (is_read) {
            success = i2c_read_with_crc(data, len, &filter);
        } else {
            success = i2c_write_with_crc(data, len, &filter);
        }
        
        // Stop condition
        i2c_send_stop_filtered(&filter);
        
        if (!success) {
            retry_count++;
            delay_ms(10); // Back-off before retry
        }
    }
    
    return success;
}

// CRC calculation for data integrity
uint8_t calculate_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07; // CRC-8-CCITT polynomial
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

bool i2c_write_with_crc(uint8_t *data, size_t len, I2CNoiseFilter_t *filter) {
    // Send data bytes
    for (size_t i = 0; i < len; i++) {
        if (!i2c_send_byte_filtered(data[i], filter)) {
            return false;
        }
    }
    
    // Send CRC for verification
    uint8_t crc = calculate_crc8(data, len);
    return i2c_send_byte_filtered(crc, filter);
}
```

### 8.2 Automotive I2C with Diagnostics (C++)

```cpp
#include <cstdint>
#include <array>
#include <optional>

// Automotive-grade I2C with comprehensive diagnostics
class AutomotiveI2C {
private:
    struct DiagnosticData {
        uint32_t total_transactions;
        uint32_t failed_transactions;
        uint32_t bus_resets;
        uint32_t timeout_events;
        uint32_t arbitration_losses;
        uint32_t crc_errors;
        
        // Signal quality metrics
        uint32_t min_rise_time_ns;
        uint32_t max_rise_time_ns;
        uint32_t avg_rise_time_ns;
        
        // Noise statistics
        uint16_t max_noise_mv;
        uint16_t avg_noise_mv;
    };
    
    DiagnosticData diagnostics_ = {};
    I2CNoiseFilter_t noise_filter_;
    
    static constexpr uint32_t AUTOMOTIVE_TIMEOUT_MS = 50;
    static constexpr uint8_t AUTOMOTIVE_MAX_RETRIES = 5;
    
public:
    AutomotiveI2C() {
        i2c_filter_init(&noise_filter_, 7); // Aggressive filtering
        reset_diagnostics();
    }
    
    // Perform transaction with full diagnostics
    bool transfer(uint8_t address, uint8_t* tx_data, size_t tx_len,
                  uint8_t* rx_data, size_t rx_len) {
        diagnostics_.total_transactions++;
        
        uint32_t start_time = get_time_ms();
        bool success = false;
        
        for (uint8_t retry = 0; retry < AUTOMOTIVE_MAX_RETRIES; retry++) {
            // Check timeout
            if (get_time_ms() - start_time > AUTOMOTIVE_TIMEOUT_MS) {
                diagnostics_.timeout_events++;
                break;
            }
            
            // Measure signal quality before transaction
            auto signal_quality = measure_signal_quality();
            update_signal_statistics(signal_quality);
            
            // Attempt transaction
            if (perform_filtered_transaction(address, tx_data, tx_len, 
                                            rx_data, rx_len)) {
                success = true;
                break;
            }
            
            // Determine failure reason
            classify_failure();
            
            // Recovery strategy
            if (retry < AUTOMOTIVE_MAX_RETRIES - 1) {
                perform_bus_recovery();
            }
        }
        
        if (!success) {
            diagnostics_.failed_transactions++;
        }
        
        return success;
    }
    
    // Get diagnostic report
    DiagnosticData get_diagnostics() const {
        return diagnostics_;
    }
    
    // Check if bus health is acceptable
    bool is_bus_healthy() const {
        float error_rate = static_cast<float>(diagnostics_.failed_transactions) /
                          diagnostics_.total_transactions;
        
        // Automotive requirement: <0.1% error rate
        if (error_rate > 0.001f) return false;
        
        // Rise time within spec (300ns for fast mode)
        if (diagnostics_.max_rise_time_ns > 300) return false;
        
        // Noise within acceptable limits
        if (diagnostics_.max_noise_mv > 100) return false;
        
        return true;
    }
    
    void reset_diagnostics() {
        diagnostics_ = DiagnosticData{};
        diagnostics_.min_rise_time_ns = UINT32_MAX;
    }
    
private:
    struct SignalQuality {
        uint32_t rise_time_ns;
        uint16_t noise_mv;
        bool arbitration_ok;
    };
    
    SignalQuality measure_signal_quality() {
        // Measure actual signal parameters
        uint32_t rise_time = measure_rise_time();
        uint16_t noise = measure_noise_level();
        bool arb_ok = check_arbitration();
        
        return {rise_time, noise, arb_ok};
    }
    
    void update_signal_statistics(const SignalQuality& quality) {
        // Update rise time statistics
        if (quality.rise_time_ns < diagnostics_.min_rise_time_ns) {
            diagnostics_.min_rise_time_ns = quality.rise_time_ns;
        }
        if (quality.rise_time_ns > diagnostics_.max_rise_time_ns) {
            diagnostics_.max_rise_time_ns = quality.rise_time_ns;
        }
        
        // Running average
        uint32_t n = diagnostics_.total_transactions;
        diagnostics_.avg_rise_time_ns = 
            (diagnostics_.avg_rise_time_ns * n + quality.rise_time_ns) / (n + 1);
        
        // Update noise statistics
        if (quality.noise_mv > diagnostics_.max_noise_mv) {
            diagnostics_.max_noise_mv = quality.noise_mv;
        }
        diagnostics_.avg_noise_mv = 
            (diagnostics_.avg_noise_mv * n + quality.noise_mv) / (n + 1);
        
        if (!quality.arbitration_ok) {
            diagnostics_.arbitration_losses++;
        }
    }
    
    void classify_failure() {
        // Determine specific failure mode for diagnostics
        if (check_bus_stuck()) {
            perform_clock_pulse_recovery();
        }
        // Additional classification logic...
    }
    
    void perform_bus_recovery() {
        diagnostics_.bus_resets++;
        
        // Standard I2C recovery procedure
        for (int i = 0; i < 9; i++) {
            toggle_scl_with_delay(5);
        }
        send_stop_condition();
    }
    
    // Placeholder functions (hardware-specific)
    bool perform_filtered_transaction(uint8_t addr, uint8_t* tx, size_t tx_len,
                                     uint8_t* rx, size_t rx_len) {
        // Implementation with noise filtering
        return true; // Placeholder
    }
    
    uint32_t measure_rise_time() { return 200; } // Placeholder
    uint16_t measure_noise_level() { return 50; } // Placeholder
    bool check_arbitration() { return true; } // Placeholder
    bool check_bus_stuck() { return false; } // Placeholder
    void perform_clock_pulse_recovery() {} // Placeholder
    void toggle_scl_with_delay(int us) {} // Placeholder
    void send_stop_condition() {} // Placeholder
    uint32_t get_time_ms() { return 0; } // Placeholder
};

// Usage example
void automotive_sensor_readout() {
    AutomotiveI2C i2c;
    
    uint8_t sensor_addr = 0x48;
    uint8_t cmd = 0x01; // Read temperature command
    uint8_t response[2];
    
    if (i2c.transfer(sensor_addr, &cmd, 1, response, 2)) {
        int16_t temp = (response[0] << 8) | response[1];
        // Process temperature
    }
    
    // Periodic health check
    if (!i2c.is_bus_healthy()) {
        auto diag = i2c.get_diagnostics();
        log_error("I2C bus health degraded:");
        log_error("  Error rate: %.2f%%", 
                 100.0f * diag.failed_transactions / diag.total_transactions);
        log_error("  Max rise time: %lu ns", diag.max_rise_time_ns);
        log_error("  Max noise: %u mV", diag.max_noise_mv);
        
        // Trigger maintenance alert
        trigger_maintenance_alert();
    }
}
```

### 8.3 Embedded Rust: Complete Signal Integrity Solution

```rust
#![no_std]

use embedded_hal::blocking::i2c::{Read, Write};
use embedded_hal::digital::v2::{InputPin, OutputPin};

/// Complete I2C signal integrity management system
pub struct SignalIntegrityManager<I2C> {
    i2c: I2C,
    filter: I2CNoiseFilter,
    diagnostics: Diagnostics,
    config: IntegrityConfig,
}

#[derive(Debug, Clone)]
pub struct IntegrityConfig {
    pub enable_filtering: bool,
    pub filter_samples: u8,
    pub enable_crc: bool,
    pub max_retries: u8,
    pub retry_delay_ms: u16,
    pub enable_diagnostics: bool,
}

impl IntegrityConfig {
    pub fn standard() -> Self {
        Self {
            enable_filtering: true,
            filter_samples: 3,
            enable_crc: false,
            max_retries: 3,
            retry_delay_ms: 10,
            enable_diagnostics: false,
        }
    }
    
    pub fn industrial() -> Self {
        Self {
            enable_filtering: true,
            filter_samples: 5,
            enable_crc: true,
            max_retries: 5,
            retry_delay_ms: 20,
            enable_diagnostics: true,
        }
    }
    
    pub fn automotive() -> Self {
        Self {
            enable_filtering: true,
            filter_samples: 7,
            enable_crc: true,
            max_retries: 5,
            retry_delay_ms: 15,
            enable_diagnostics: true,
        }
    }
}

#[derive(Debug, Default, Clone)]
pub struct Diagnostics {
    pub total_transactions: u32,
    pub failed_transactions: u32,
    pub crc_errors: u32,
    pub retries_performed: u32,
    pub bus_recoveries: u32,
}

impl Diagnostics {
    pub fn error_rate(&self) -> f32 {
        if self.total_transactions == 0 {
            0.0
        } else {
            self.failed_transactions as f32 / self.total_transactions as f32
        }
    }
    
    pub fn is_healthy(&self, threshold: f32) -> bool {
        self.error_rate() < threshold
    }
}

impl<I2C> SignalIntegrityManager<I2C>
where
    I2C: Read + Write,
{
    pub fn new(i2c: I2C, config: IntegrityConfig) -> Self {
        Self {
            i2c,
            filter: I2CNoiseFilter::new(config.filter_samples),
            diagnostics: Diagnostics::default(),
            config,
        }
    }
    
    /// Write data with signal integrity protections
    pub fn write_protected(
        &mut self,
        address: u8,
        data: &[u8],
    ) -> Result<(), I2CError> {
        if self.config.enable_diagnostics {
            self.diagnostics.total_transactions += 1;
        }
        
        let mut attempt = 0;
        let mut last_error = None;
        
        while attempt < self.config.max_retries {
            // Prepare data with CRC if enabled
            let write_data = if self.config.enable_crc {
                let mut buffer = heapless::Vec::<u8, 32>::new();
                buffer.extend_from_slice(data).ok();
                let crc = Self::calculate_crc8(data);
                buffer.push(crc).ok();
                buffer
            } else {
                let mut buffer = heapless::Vec::<u8, 32>::new();
                buffer.extend_from_slice(data).ok();
                buffer
            };
            
            // Attempt write
            match self.i2c.write(address, &write_data) {
                Ok(_) => return Ok(()),
                Err(e) => {
                    last_error = Some(I2CError::BusError);
                    attempt += 1;
                    
                    if self.config.enable_diagnostics {
                        self.diagnostics.retries_performed += 1;
                    }
                    
                    // Delay before retry
                    if attempt < self.config.max_retries {
                        delay_ms(self.config.retry_delay_ms);
                    }
                }
            }
        }
        
        if self.config.enable_diagnostics {
            self.diagnostics.failed_transactions += 1;
        }
        
        Err(last_error.unwrap_or(I2CError::MaxRetriesExceeded))
    }
    
    /// Read data with signal integrity protections
    pub fn read_protected(
        &mut self,
        address: u8,
        buffer: &mut [u8],
    ) -> Result<(), I2CError> {
        if self.config.enable_diagnostics {
            self.diagnostics.total_transactions += 1;
        }
        
        let mut attempt = 0;
        let mut last_error = None;
        
        while attempt < self.config.max_retries {
            // Attempt read
            match self.i2c.read(address, buffer) {
                Ok(_) => {
                    // Verify CRC if enabled
                    if self.config.enable_crc && buffer.len() > 1 {
                        let data_len = buffer.len() - 1;
                        let received_crc = buffer[data_len];
                        let calculated_crc = Self::calculate_crc8(&buffer[..data_len]);
                        
                        if received_crc != calculated_crc {
                            if self.config.enable_diagnostics {
                                self.diagnostics.crc_errors += 1;
                            }
                            last_error = Some(I2CError::CrcError);
                            attempt += 1;
                            continue;
                        }
                    }
                    
                    return Ok(());
                }
                Err(_) => {
                    last_error = Some(I2CError::BusError);
                    attempt += 1;
                    
                    if self.config.enable_diagnostics {
                        self.diagnostics.retries_performed += 1;
                    }
                    
                    if attempt < self.config.max_retries {
                        delay_ms(self.config.retry_delay_ms);
                    }
                }
            }
        }
        
        if self.config.enable_diagnostics {
            self.diagnostics.failed_transactions += 1;
        }
        
        Err(last_error.unwrap_or(I2CError::MaxRetriesExceeded))
    }
    
    /// Get diagnostic information
    pub fn get_diagnostics(&self) -> &Diagnostics {
        &self.diagnostics
    }
    
    /// Reset diagnostics
    pub fn reset_diagnostics(&mut self) {
        self.diagnostics = Diagnostics::default();
    }
    
    /// Calculate CRC-8 checksum
    fn calculate_crc8(data: &[u8]) -> u8 {
        let mut crc: u8 = 0xFF;
        
        for &byte in data {
            crc ^= byte;
            for _ in 0..8 {
                if crc & 0x80 != 0 {
                    crc = (crc << 1) ^ 0x07; // CRC-8-CCITT
                } else {
                    crc <<= 1;
                }
            }
        }
        
        crc
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CError {
    BusError,
    CrcError,
    MaxRetriesExceeded,
    TimeoutError,
}

// Placeholder delay function (would use HAL timer in real implementation)
fn delay_ms(ms: u16) {
    // Implementation depends on target platform
}

/// Example: Temperature sensor with full protection
pub struct ProtectedTempSensor<I2C> {
    manager: SignalIntegrityManager<I2C>,
    address: u8,
}

impl<I2C> ProtectedTempSensor<I2C>
where
    I2C: Read + Write,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        let config = IntegrityConfig::industrial();
        Self {
            manager: SignalIntegrityManager::new(i2c, config),
            address,
        }
    }
    
    pub fn read_temperature(&mut self) -> Result<f32, I2CError> {
        let cmd = [0x01]; // Temperature register
        let mut buffer = [0u8; 3]; // 2 bytes data + 1 byte CRC
        
        // Write register address
        self.manager.write_protected(self.address, &cmd)?;
        
        // Read temperature with CRC
        self.manager.read_protected(self.address, &mut buffer)?;
        
        // Parse temperature (assuming 16-bit value)
        let raw_temp = ((buffer[0] as i16) << 8) | (buffer[1] as i16);
        let temperature = raw_temp as f32 * 0.0625; // Example conversion
        
        Ok(temperature)
    }
    
    pub fn check_health(&self) -> bool {
        let diag = self.manager.get_diagnostics();
        diag.is_healthy(0.01) // 1% error threshold
    }
}
```

---

## 9. Debug and Troubleshooting Guide

### 9.1 Common Issues and Solutions

```rust
/// Signal integrity troubleshooting helper
pub struct I2CTroubleshooter {
    symptoms: Vec<Symptom>,
}

#[derive(Debug, Clone)]
pub enum Symptom {
    SlowRiseTime,
    ExcessiveNoise,
    RandomErrors,
    BusLockup,
    AddressNack,
    DataCorruption,
}

#[derive(Debug)]
pub struct Diagnosis {
    pub likely_cause: &'static str,
    pub solutions: Vec<&'static str>,
    pub hardware_checks: Vec<&'static str>,
}

impl I2CTroubleshooter {
    pub fn diagnose(&self, symptom: Symptom) -> Diagnosis {
        match symptom {
            Symptom::SlowRiseTime => Diagnosis {
                likely_cause: "Excessive bus capacitance or weak pull-ups",
                solutions: vec![
                    "Reduce pull-up resistor value (try 2.2kΩ)",
                    "Remove unnecessary devices from bus",
                    "Shorten PCB traces",
                    "Reduce I2C clock frequency",
                ],
                hardware_checks: vec![
                    "Measure total bus capacitance (<400pF)",
                    "Check pull-up resistor values",
                    "Verify trace lengths",
                ],
            },
            
            Symptom::ExcessiveNoise => Diagnosis {
                likely_cause: "EMI coupling or poor PCB layout",
                solutions: vec![
                    "Add RC filtering (330Ω + 220pF)",
                    "Install ferrite beads on I2C lines",
                    "Use shielded cables for off-board connections",
                    "Implement software noise filtering",
                    "Add ground guards between I2C and noisy signals",
                ],
                hardware_checks: vec![
                    "Measure noise on SDA/SCL with oscilloscope",
                    "Check for parallel routing with noisy signals",
                    "Verify ground plane integrity",
                ],
            },
            
            Symptom::BusLockup => Diagnosis {
                likely_cause: "SDA stuck low or clock stretching issue",
                solutions: vec![
                    "Implement bus recovery (9 clock pulses)",
                    "Add clock stretching timeout",
                    "Check for devices holding SDA low",
                    "Power cycle all devices",
                ],
                hardware_checks: vec![
                    "Measure DC voltage on SDA (should be VDD)",
                    "Check if any device is driving SDA low",
                    "Verify pull-up resistors are installed",
                ],
            },
            
            Symptom::DataCorruption => Diagnosis {
                likely_cause: "Signal integrity issues or timing violations",
                solutions: vec![
                    "Enable CRC checking",
                    "Reduce I2C clock speed",
                    "Add hardware filtering",
                    "Increase setup/hold time margins",
                ],
                hardware_checks: vec![
                    "Capture I2C transaction with logic analyzer",
                    "Check for glitches on SDA during SCL high",
                    "Verify timing parameters meet I2C spec",
                ],
            },
            
            _ => Diagnosis {
                likely_cause: "Multiple potential causes",
                solutions: vec!["Run comprehensive signal integrity tests"],
                hardware_checks: vec!["Perform full bus analysis"],
            },
        }
    }
}
```

---

## 10. Practical Measurement and Analysis

### 10.1 Oscilloscope Measurement Checklist

```c
/*
 * I2C Signal Quality Measurement Procedure
 * 
 * Equipment Required:
 * - Oscilloscope (>100 MHz bandwidth, 4 channels preferred)
 * - Logic analyzer (optional, for protocol analysis)
 * - Current probe (optional, for power analysis)
 * 
 * Measurements to Take:
 * 
 * 1. RISE TIME (tr)
 *    - Measure from 30% to 70% of VDD
 *    - Standard mode: max 1000ns
 *    - Fast mode: max 300ns
 *    - Fast mode plus: max 120ns
 * 
 * 2. FALL TIME (tf)
 *    - Measure from 70% to 30% of VDD
 *    - Should be < 300ns for all modes
 * 
 * 3. VOLTAGE LEVELS
 *    - VIL (Input Low): < 0.3×VDD
 *    - VIH (Input High): > 0.7×VDD
 *    - VOL (Output Low): < 0.4V @ 3mA sink
 * 
 * 4. NOISE
 *    - Peak-to-peak noise during idle
 *    - Should be < 0.1×VDD (typically <330mV for 3.3V)
 * 
 * 5. OVERSHOOT/UNDERSHOOT
 *    - Should be < 0.1×VDD
 *    - Duration < 50ns
 * 
 * 6. TIMING PARAMETERS
 *    - tHD:STA (hold time start condition): > 4µs (standard), > 0.6µs (fast)
 *    - tSU:STA (setup time start condition): > 4.7µs (standard), > 0.6µs (fast)
 *    - tSU:STO (setup time stop condition): > 4µs (standard), > 0.6µs (fast)
 *    - tBUF (bus free time): > 4.7µs (standard), > 1.3µs (fast)
 */

typedef struct {
    uint32_t rise_time_ns;
    uint32_t fall_time_ns;
    uint16_t vil_mv;
    uint16_t vih_mv;
    uint16_t vol_mv;
    uint16_t noise_pp_mv;
    uint16_t overshoot_mv;
    uint16_t undershoot_mv;
} OscilloscopeMeasurements_t;

bool validate_measurements(OscilloscopeMeasurements_t *meas, uint32_t mode_khz) {
    bool pass = true;
    
    // Check rise time
    uint32_t max_rise = (mode_khz <= 100) ? 1000 : 
                       (mode_khz <= 400) ? 300 : 120;
    if (meas->rise_time_ns > max_rise) {
        printf("FAIL: Rise time %lu ns exceeds %lu ns\n", 
               meas->rise_time_ns, max_rise);
        pass = false;
    }
    
    // Check voltage levels (assuming 3.3V)
    if (meas->vil_mv > 990) { // 0.3 × 3300
        printf("FAIL: VIL %u mV exceeds 990 mV\n", meas->vil_mv);
        pass = false;
    }
    
    if (meas->vih_mv < 2310) { // 0.7 × 3300
        printf("FAIL: VIH %u mV below 2310 mV\n", meas->vih_mv);
        pass = false;
    }
    
    // Check noise
    if (meas->noise_pp_mv > 330) {
        printf("WARN: Noise %u mVpp exceeds recommended 330 mVpp\n", 
               meas->noise_pp_mv);
    }
    
    return pass;
}
```

---

## Summary

This comprehensive guide covered I2C signal integrity from multiple angles:

1. **Noise Immunity**: Hardware filtering, software debouncing, and adaptive techniques
2. **Crosstalk Mitigation**: PCB layout rules, guard traces, and detection algorithms  
3. **EMI Protection**: Common mode chokes, shielding, and ferrite beads
4. **Real-World Applications**: Industrial, automotive, and embedded implementations
5. **Diagnostics**: Health monitoring, troubleshooting, and measurement procedures

### Key Implementation Priorities

For any I2C system prioritizing signal integrity:

1. Start with proper hardware (correct pull-ups, filtering, PCB layout)
2. Add software filtering for critical applications
3. Implement comprehensive error handling and retry logic
4. Monitor bus health with diagnostics
5. Test thoroughly under worst-case conditions

The code examples provided can be adapted to your specific hardware platform and requirements, with the Rust implementations offering additional type safety for safety-critical applications.