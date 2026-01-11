# I2C Oscilloscope Analysis: A Comprehensive Guide

Oscilloscope analysis is essential for debugging I2C communication issues, verifying signal integrity, and ensuring reliable operation. Let me provide a detailed explanation with practical code examples.

## Understanding I2C Signal Characteristics

I2C uses two bidirectional lines:
- **SDA (Serial Data)**: Carries data bits
- **SCL (Serial Clock)**: Provides timing synchronization

Both lines use open-drain/open-collector outputs with pull-up resistors, which means:
- Logic HIGH: Lines pulled up by resistors (typically 3.3V or 5V)
- Logic LOW: Actively driven low by devices

## Key Parameters to Analyze

### 1. **Rise and Fall Times**
The I2C specification defines maximum rise and fall times based on speed mode:
- Standard mode (100 kHz): 1000 ns max
- Fast mode (400 kHz): 300 ns max
- Fast mode plus (1 MHz): 120 ns max

### 2. **Setup and Hold Times**
- **Setup time**: Minimum time SDA must be stable before SCL goes HIGH
- **Hold time**: Minimum time SDA must remain stable after SCL goes LOW

### 3. **Bus Capacitance**
Total capacitance affects signal quality:
- Standard mode: 400 pF max
- Fast mode: 400 pF max
- Fast mode plus: 550 pF max

### 4. **Clock Stretching**
Slaves can hold SCL LOW to pause communication when they need more time.

## C/C++ Implementation with Timing Analysis

```c
/*
 * I2C Oscilloscope Analysis and Timing Verification
 * 
 * This code demonstrates I2C communication with timing analysis
 * capabilities for oscilloscope verification and debugging.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Platform-specific includes (example for embedded systems)
#ifdef EMBEDDED
#include "gpio.h"
#include "timer.h"
#else
#include <time.h>
#include <unistd.h>
#endif

/* I2C Timing Parameters (in microseconds) */
typedef struct {
    uint32_t scl_frequency;        // Target SCL frequency in Hz
    uint32_t t_hd_sta;            // Hold time START condition (min)
    uint32_t t_su_sta;            // Setup time START condition (min)
    uint32_t t_su_dat;            // Data setup time (min)
    uint32_t t_hd_dat;            // Data hold time (min)
    uint32_t t_su_sto;            // Setup time STOP condition (min)
    uint32_t t_buf;               // Bus free time between transactions (min)
    uint32_t max_rise_time_ns;    // Maximum rise time in nanoseconds
    uint32_t max_fall_time_ns;    // Maximum fall time in nanoseconds
} I2C_TimingParams;

/* Standard Mode (100 kHz) timing parameters */
const I2C_TimingParams I2C_STANDARD_MODE = {
    .scl_frequency = 100000,      // 100 kHz
    .t_hd_sta = 4000,             // 4.0 µs
    .t_su_sta = 4700,             // 4.7 µs
    .t_su_dat = 250,              // 250 ns
    .t_hd_dat = 0,                // 0 ns (but 300ns typical)
    .t_su_sto = 4000,             // 4.0 µs
    .t_buf = 4700,                // 4.7 µs
    .max_rise_time_ns = 1000,     // 1000 ns max
    .max_fall_time_ns = 300       // 300 ns max
};

/* Fast Mode (400 kHz) timing parameters */
const I2C_TimingParams I2C_FAST_MODE = {
    .scl_frequency = 400000,      // 400 kHz
    .t_hd_sta = 600,              // 0.6 µs
    .t_su_sta = 600,              // 0.6 µs
    .t_su_dat = 100,              // 100 ns
    .t_hd_dat = 0,                // 0 ns (but 300ns typical)
    .t_su_sto = 600,              // 0.6 µs
    .t_buf = 1300,                // 1.3 µs
    .max_rise_time_ns = 300,      // 300 ns max
    .max_fall_time_ns = 300       // 300 ns max
};

/* I2C Signal Timing Measurement */
typedef struct {
    uint32_t rise_time_ns;        // Measured rise time
    uint32_t fall_time_ns;        // Measured fall time
    uint32_t scl_high_time_ns;    // SCL high period
    uint32_t scl_low_time_ns;     // SCL low period
    uint32_t start_setup_ns;      // START condition setup time
    uint32_t stop_setup_ns;       // STOP condition setup time
    bool clock_stretching_detected;
    uint32_t stretch_duration_ns;
} I2C_Measurement;

/* I2C Bus State */
typedef struct {
    volatile uint8_t *sda_port;
    volatile uint8_t *scl_port;
    uint8_t sda_pin;
    uint8_t scl_pin;
    I2C_TimingParams timing;
    I2C_Measurement measurement;
    bool debug_mode;
} I2C_Bus;

/* GPIO Helper Functions (platform-specific) */
static inline void sda_high(I2C_Bus *bus) {
    // Set SDA pin as input (pull-up will bring it high)
    // Actual implementation depends on platform
    #ifdef EMBEDDED
    gpio_set_input(bus->sda_pin);
    #endif
}

static inline void sda_low(I2C_Bus *bus) {
    // Set SDA pin as output and drive low
    #ifdef EMBEDDED
    gpio_set_output(bus->sda_pin);
    gpio_write(bus->sda_pin, 0);
    #endif
}

static inline void scl_high(I2C_Bus *bus) {
    #ifdef EMBEDDED
    gpio_set_input(bus->scl_pin);
    #endif
}

static inline void scl_low(I2C_Bus *bus) {
    #ifdef EMBEDDED
    gpio_set_output(bus->scl_pin);
    gpio_write(bus->scl_pin, 0);
    #endif
}

static inline bool sda_read(I2C_Bus *bus) {
    #ifdef EMBEDDED
    return gpio_read(bus->sda_pin);
    #else
    return true; // Simulation
    #endif
}

static inline bool scl_read(I2C_Bus *bus) {
    #ifdef EMBEDDED
    return gpio_read(bus->scl_pin);
    #else
    return true; // Simulation
    #endif
}

/* High-resolution timing functions */
static uint64_t get_time_ns(void) {
    #ifdef EMBEDDED
    return timer_get_ns();
    #else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    #endif
}

static void delay_ns(uint32_t ns) {
    #ifdef EMBEDDED
    timer_delay_ns(ns);
    #else
    struct timespec ts = {0, ns};
    nanosleep(&ts, NULL);
    #endif
}

/* Measure signal rise time */
static uint32_t measure_rise_time(I2C_Bus *bus, bool is_sda) {
    uint64_t start_time, end_time;
    
    // Wait for line to go LOW
    if (is_sda) {
        while (sda_read(bus));
    } else {
        while (scl_read(bus));
    }
    
    // Trigger rise by releasing line
    if (is_sda) {
        sda_high(bus);
    } else {
        scl_high(bus);
    }
    
    // Measure time from 30% to 70% of VDD (simplified: measure full rise)
    start_time = get_time_ns();
    
    if (is_sda) {
        while (!sda_read(bus)) {
            // Timeout check recommended
        }
    } else {
        while (!scl_read(bus)) {
            // Timeout check recommended
        }
    }
    
    end_time = get_time_ns();
    
    return (uint32_t)(end_time - start_time);
}

/* I2C START condition with timing analysis */
static void i2c_start_with_timing(I2C_Bus *bus) {
    uint64_t time_before, time_after;
    
    // Ensure both lines are high initially
    sda_high(bus);
    scl_high(bus);
    delay_ns(bus->timing.t_buf * 1000);
    
    if (bus->debug_mode) {
        printf("[SCOPE] START condition begin\n");
        printf("[SCOPE] SDA and SCL both HIGH\n");
    }
    
    // START condition: SDA transitions HIGH to LOW while SCL is HIGH
    time_before = get_time_ns();
    sda_low(bus);
    time_after = get_time_ns();
    
    if (bus->debug_mode) {
        printf("[SCOPE] SDA goes LOW (START)\n");
        printf("[SCOPE] SDA fall time: %lu ns\n", 
               (unsigned long)(time_after - time_before));
    }
    
    // Hold time for START condition
    delay_ns(bus->timing.t_hd_sta * 1000);
    
    // Pull SCL low to begin data transfer
    scl_low(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] SCL goes LOW (ready for data)\n");
    }
}

/* I2C STOP condition with timing analysis */
static void i2c_stop_with_timing(I2C_Bus *bus) {
    uint64_t time_before, time_after;
    
    // Ensure SDA is low
    sda_low(bus);
    delay_ns(bus->timing.t_hd_dat * 1000);
    
    // Release SCL
    scl_high(bus);
    
    // Wait for clock stretching
    uint64_t stretch_start = get_time_ns();
    while (!scl_read(bus)) {
        // Timeout check recommended
    }
    uint64_t stretch_end = get_time_ns();
    
    if (stretch_end - stretch_start > 1000) {
        bus->measurement.clock_stretching_detected = true;
        bus->measurement.stretch_duration_ns = 
            (uint32_t)(stretch_end - stretch_start);
    }
    
    // Setup time for STOP condition
    delay_ns(bus->timing.t_su_sto * 1000);
    
    if (bus->debug_mode) {
        printf("[SCOPE] STOP condition begin\n");
    }
    
    // STOP condition: SDA transitions LOW to HIGH while SCL is HIGH
    time_before = get_time_ns();
    sda_high(bus);
    time_after = get_time_ns();
    
    bus->measurement.rise_time_ns = (uint32_t)(time_after - time_before);
    
    if (bus->debug_mode) {
        printf("[SCOPE] SDA goes HIGH (STOP)\n");
        printf("[SCOPE] SDA rise time: %u ns\n", 
               bus->measurement.rise_time_ns);
    }
    
    // Bus free time
    delay_ns(bus->timing.t_buf * 1000);
}

/* Write one bit with timing analysis */
static void i2c_write_bit_with_timing(I2C_Bus *bus, bool bit) {
    uint64_t scl_low_start, scl_high_start, scl_high_end;
    
    // Set data line
    if (bit) {
        sda_high(bus);
    } else {
        sda_low(bus);
    }
    
    // Data setup time
    delay_ns(bus->timing.t_su_dat);
    
    // Release clock
    scl_low_start = get_time_ns();
    scl_high(bus);
    
    // Wait for clock to go high (check for stretching)
    while (!scl_read(bus)) {
        // Clock stretching
    }
    scl_high_start = get_time_ns();
    
    // Clock high period
    delay_ns((1000000000UL / bus->timing.scl_frequency) / 2);
    
    scl_high_end = get_time_ns();
    bus->measurement.scl_high_time_ns = 
        (uint32_t)(scl_high_end - scl_high_start);
    
    // Pull clock low
    scl_low(bus);
    
    bus->measurement.scl_low_time_ns = 
        (uint32_t)(scl_high_start - scl_low_start);
    
    if (bus->debug_mode) {
        printf("[SCOPE] Bit: %d, SCL high: %u ns, SCL low: %u ns\n",
               bit, bus->measurement.scl_high_time_ns,
               bus->measurement.scl_low_time_ns);
    }
}

/* Read one bit with timing analysis */
static bool i2c_read_bit_with_timing(I2C_Bus *bus) {
    bool bit;
    
    // Release SDA for slave to control
    sda_high(bus);
    delay_ns(bus->timing.t_su_dat);
    
    // Release clock
    scl_high(bus);
    
    // Wait for clock to go high (check for stretching)
    while (!scl_read(bus)) {
        // Clock stretching
    }
    
    // Clock high period
    delay_ns((1000000000UL / bus->timing.scl_frequency) / 2);
    
    // Read the bit
    bit = sda_read(bus);
    
    // Pull clock low
    scl_low(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] Read bit: %d\n", bit);
    }
    
    return bit;
}

/* Write byte and return ACK status */
static bool i2c_write_byte_with_timing(I2C_Bus *bus, uint8_t byte) {
    if (bus->debug_mode) {
        printf("[SCOPE] Writing byte: 0x%02X\n", byte);
    }
    
    // Write 8 bits
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit_with_timing(bus, (byte >> i) & 0x01);
    }
    
    // Read ACK bit
    bool ack = !i2c_read_bit_with_timing(bus);
    
    if (bus->debug_mode) {
        printf("[SCOPE] ACK received: %d\n", ack);
    }
    
    return ack;
}

/* Signal quality analysis */
static void analyze_signal_quality(I2C_Bus *bus) {
    printf("\n=== I2C Signal Quality Analysis ===\n");
    
    // Check rise time
    if (bus->measurement.rise_time_ns > bus->timing.max_rise_time_ns) {
        printf("⚠ WARNING: Rise time (%u ns) exceeds maximum (%u ns)\n",
               bus->measurement.rise_time_ns,
               bus->timing.max_rise_time_ns);
        printf("  → Reduce pull-up resistors or bus capacitance\n");
    } else {
        printf("✓ Rise time within specification: %u ns\n",
               bus->measurement.rise_time_ns);
    }
    
    // Check clock frequency
    uint32_t measured_freq = 1000000000UL / 
        (bus->measurement.scl_high_time_ns + bus->measurement.scl_low_time_ns);
    printf("✓ Measured SCL frequency: %u Hz (target: %u Hz)\n",
           measured_freq, bus->timing.scl_frequency);
    
    // Check clock stretching
    if (bus->measurement.clock_stretching_detected) {
        printf("ℹ Clock stretching detected: %u ns\n",
               bus->measurement.stretch_duration_ns);
    }
    
    printf("=====================================\n\n");
}

/* Example usage */
int main(void) {
    I2C_Bus bus = {0};
    
    // Configure for Fast Mode
    bus.timing = I2C_FAST_MODE;
    bus.debug_mode = true;
    
    printf("Starting I2C Oscilloscope Analysis\n");
    printf("Connect oscilloscope to SDA and SCL lines\n");
    printf("Trigger on START condition (SDA falling edge with SCL high)\n\n");
    
    // Perform a test transaction
    i2c_start_with_timing(&bus);
    
    // Write device address (0x50, write mode)
    uint8_t device_addr = (0x50 << 1) | 0;
    bool ack = i2c_write_byte_with_timing(&bus, device_addr);
    
    if (ack) {
        // Write register address
        i2c_write_byte_with_timing(&bus, 0x00);
        
        // Write data
        i2c_write_byte_with_timing(&bus, 0xAA);
    }
    
    i2c_stop_with_timing(&bus);
    
    // Analyze captured measurements
    analyze_signal_quality(&bus);
    
    return 0;
}
```

## Rust Implementation with Signal Analysis

```rs
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
```

## Oscilloscope Setup and Analysis Guide

### **Oscilloscope Configuration**

**Probe Setup:**
1. **Channel 1 (CH1)**: Connect to SDA line
2. **Channel 2 (CH2)**: Connect to SCL line
3. **Ground**: Connect oscilloscope ground to circuit ground
4. Use 10:1 probes to minimize capacitive loading

**Trigger Settings:**
- **Trigger source**: CH1 (SDA)
- **Trigger type**: Edge trigger, falling edge
- **Trigger condition**: CH2 (SCL) must be HIGH
- This captures the START condition

**Timebase:**
- Standard mode (100 kHz): 20-50 µs/div
- Fast mode (400 kHz): 5-10 µs/div
- Fast mode plus (1 MHz): 2-5 µs/div

**Voltage Scale:**
- 1-2 V/div (for 3.3V or 5V systems)
- Position traces for clear visibility

### **What to Look For**

**1. START Condition:**
- SDA falls while SCL is HIGH
- Should see clean falling edge on SDA
- SCL remains HIGH during SDA transition

**2. Data Bits:**
- SDA changes only while SCL is LOW
- SDA stable while SCL is HIGH
- 8 data bits followed by ACK/NACK

**3. STOP Condition:**
- SDA rises while SCL is HIGH
- Clean rising edge on SDA
- Both lines return to HIGH state

**4. Rise/Fall Times:**
- Measure from 30% to 70% of VDD
- Should be within specification for your speed mode
- Slow rise times indicate excessive capacitance or weak pull-ups

**5. Clock Stretching:**
- SCL held LOW longer than expected
- Indicates slave needs more time
- Look for extended LOW periods on SCL

### **Common Issues and Solutions**

| Issue | Symptoms | Solution |
|-------|----------|----------|
| Slow rise times | Rounded edges, long transitions | Reduce pull-up resistor value (2.2kΩ → 1kΩ) |
| Excessive capacitance | Very slow rise times | Shorten wires, reduce number of devices |
| Noise/glitches | Spikes on signals | Add filtering capacitors (100nF near devices) |
| Weak pull-ups | Doesn't reach full VDD | Use lower value resistors |
| Ringing | Oscillations after transitions | Add small series resistors (22-100Ω) |
| No ACK | Ninth bit stays HIGH | Check device address, connections, power |

### **Pull-up Resistor Calculation**

The pull-up resistor value affects rise time:

```
Rise time ≈ 2.2 × R_pullup × C_bus
```

Where:
- `R_pullup` = pull-up resistor value
- `C_bus` = total bus capacitance (wire + devices)

**Example:**
- Bus capacitance: 200 pF
- Desired rise time: < 300 ns (Fast mode)
- Required R_pullup: < 300ns / (2.2 × 200pF) ≈ 680Ω

However, also consider:
- **Minimum resistance**: I_max = VDD / R_pullup (typically 3mA per spec)
- For 3.3V: R_min = 3.3V / 3mA ≈ 1.1kΩ

**Safe range for Fast mode (3.3V):**
- 1kΩ to 4.7kΩ depending on bus capacitance

### **Advanced Analysis Techniques**

**Jitter Measurement:**
Use oscilloscope's persistence mode to see timing variations across multiple transactions.

**Eye Diagram:**
For high-speed modes, use infinite persistence and trigger on data edges to visualize signal integrity.

**Protocol Decode:**
Modern oscilloscopes can decode I2C automatically:
- Shows device addresses
- Displays data bytes in hex
- Highlights ACK/NACK bits
- Reports timing violations

**FFT Analysis:**
Use FFT to identify noise sources affecting signal quality.

The code examples provided give you complete implementations with timing measurements that correlate directly to what you'll see on an oscilloscope. The debug output helps you understand exactly what's happening at each stage of the I2C transaction!