# RS-485 Physical Layer for Profibus DP

## Overview

RS-485 is the physical layer standard that underlies Profibus DP (Decentralized Peripherals) communication. It provides a robust, differential signaling method capable of supporting high-speed data transmission over long distances in industrial environments with significant electrical noise.

## Key Technical Characteristics

### Voltage Levels
- **Differential signaling**: RS-485 uses two wires (A and B) to transmit data
- **Logic HIGH (Mark)**: A > B by at least +200mV (typically +2V to +6V differential)
- **Logic LOW (Space)**: B > A by at least +200mV (typically -2V to -6V differential)
- **Common mode voltage range**: -7V to +12V
- **Receiver sensitivity**: ±200mV minimum differential voltage

### Termination Requirements
- **120Ω termination resistors** required at both ends of the bus
- Prevents signal reflections and maintains signal integrity
- Active termination (with pull-up/pull-down resistors) recommended for Profibus
- Typical active termination: 390Ω to +5V and 220Ω to GND at each end

### Cable Specifications
- **Characteristic impedance**: 120Ω (nominal)
- **Twisted pair construction**: Reduces electromagnetic interference
- **Shielded cable**: Recommended for industrial environments
- **Maximum cable length**: 
  - Up to 1200m at 9.6 kbps
  - Up to 100m at 12 Mbps
- **Maximum nodes**: 32 devices per segment (up to 126 with repeaters)

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// RS-485 Configuration Constants
#define RS485_BAUDRATE_9600     9600
#define RS485_BAUDRATE_19200    19200
#define RS485_BAUDRATE_93750    93750
#define RS485_BAUDRATE_187500   187500
#define RS485_BAUDRATE_500000   500000
#define RS485_BAUDRATE_1500000  1500000
#define RS485_BAUDRATE_12000000 12000000

#define RS485_TERMINATION_OHM   120
#define RS485_MAX_NODES         32
#define RS485_COMMON_MODE_MIN   -7
#define RS485_COMMON_MODE_MAX   12
#define RS485_MIN_DIFF_VOLTAGE  0.2f  // 200mV

// Cable length limits (in meters) based on baudrate
typedef struct {
    uint32_t baudrate;
    uint16_t max_length_m;
} RS485_CableSpec;

static const RS485_CableSpec cable_specs[] = {
    {RS485_BAUDRATE_9600,     1200},
    {RS485_BAUDRATE_19200,    1200},
    {RS485_BAUDRATE_93750,    1200},
    {RS485_BAUDRATE_187500,   1000},
    {RS485_BAUDRATE_500000,   400},
    {RS485_BAUDRATE_1500000,  200},
    {RS485_BAUDRATE_12000000, 100}
};

// RS-485 Transceiver Control
typedef struct {
    volatile uint8_t* tx_enable_port;  // Transmit enable GPIO
    uint8_t tx_enable_pin;
    volatile uint8_t* rx_enable_port;  // Receive enable GPIO
    uint8_t rx_enable_pin;
    bool is_transmitting;
} RS485_Transceiver;

// Initialize RS-485 transceiver
void rs485_init(RS485_Transceiver* trans, 
                volatile uint8_t* tx_port, uint8_t tx_pin,
                volatile uint8_t* rx_port, uint8_t rx_pin) {
    trans->tx_enable_port = tx_port;
    trans->tx_enable_pin = tx_pin;
    trans->rx_enable_port = rx_port;
    trans->rx_enable_pin = rx_pin;
    trans->is_transmitting = false;
    
    // Default to receive mode
    rs485_set_receive_mode(trans);
}

// Set transceiver to transmit mode
void rs485_set_transmit_mode(RS485_Transceiver* trans) {
    // Enable transmitter (DE = HIGH)
    *(trans->tx_enable_port) |= (1 << trans->tx_enable_pin);
    // Disable receiver (RE = HIGH, active low)
    *(trans->rx_enable_port) |= (1 << trans->rx_enable_pin);
    trans->is_transmitting = true;
}

// Set transceiver to receive mode
void rs485_set_receive_mode(RS485_Transceiver* trans) {
    // Disable transmitter (DE = LOW)
    *(trans->tx_enable_port) &= ~(1 << trans->tx_enable_pin);
    // Enable receiver (RE = LOW, active low)
    *(trans->rx_enable_port) &= ~(1 << trans->rx_enable_pin);
    trans->is_transmitting = false;
}

// Calculate maximum cable length for given baudrate
uint16_t rs485_get_max_cable_length(uint32_t baudrate) {
    for (int i = 0; i < sizeof(cable_specs) / sizeof(RS485_CableSpec); i++) {
        if (baudrate <= cable_specs[i].baudrate) {
            return cable_specs[i].max_length_m;
        }
    }
    return 100; // Default minimum
}

// Validate differential voltage levels
bool rs485_validate_signal(float voltage_a, float voltage_b) {
    float diff_voltage = voltage_a - voltage_b;
    
    // Check if differential voltage meets minimum threshold
    if (fabs(diff_voltage) < RS485_MIN_DIFF_VOLTAGE) {
        return false;
    }
    
    // Check common mode voltage range
    float common_mode = (voltage_a + voltage_b) / 2.0f;
    if (common_mode < RS485_COMMON_MODE_MIN || 
        common_mode > RS485_COMMON_MODE_MAX) {
        return false;
    }
    
    return true;
}

// Profibus DP specific timing calculations
typedef struct {
    uint32_t baudrate;
    uint32_t bit_time_us;        // Time per bit in microseconds
    uint32_t slot_time_us;       // Token rotation slot time
    uint32_t setup_time_us;      // Setup time before transmission
} Profibus_Timing;

void profibus_calculate_timing(Profibus_Timing* timing, uint32_t baudrate) {
    timing->baudrate = baudrate;
    timing->bit_time_us = 1000000 / baudrate;
    
    // Profibus DP timing calculations
    // Setup time: typically 1 bit time
    timing->setup_time_us = timing->bit_time_us;
    
    // Slot time depends on baudrate (simplified calculation)
    if (baudrate <= RS485_BAUDRATE_19200) {
        timing->slot_time_us = 400;
    } else if (baudrate <= RS485_BAUDRATE_187500) {
        timing->slot_time_us = 200;
    } else {
        timing->slot_time_us = 100;
    }
}

// Example usage
int main() {
    RS485_Transceiver transceiver;
    volatile uint8_t tx_port = 0, rx_port = 0;
    
    // Initialize RS-485 transceiver
    rs485_init(&transceiver, &tx_port, 0, &rx_port, 1);
    
    // Check maximum cable length for 500 kbps
    uint16_t max_length = rs485_get_max_cable_length(RS485_BAUDRATE_500000);
    printf("Max cable length at 500 kbps: %d meters\n", max_length);
    
    // Calculate Profibus timing
    Profibus_Timing timing;
    profibus_calculate_timing(&timing, RS485_BAUDRATE_500000);
    printf("Bit time: %u us, Slot time: %u us\n", 
           timing.bit_time_us, timing.slot_time_us);
    
    // Validate signal levels
    float voltage_a = 2.5f;
    float voltage_b = -2.5f;
    bool valid = rs485_validate_signal(voltage_a, voltage_b);
    printf("Signal valid: %s\n", valid ? "Yes" : "No");
    
    // Switch to transmit mode
    rs485_set_transmit_mode(&transceiver);
    printf("Transceiver in transmit mode\n");
    
    // Transmit data here...
    
    // Switch back to receive mode
    rs485_set_receive_mode(&transceiver);
    printf("Transceiver in receive mode\n");
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

// RS-485 Configuration Constants
const RS485_TERMINATION_OHM: u16 = 120;
const RS485_MAX_NODES: u8 = 32;
const RS485_COMMON_MODE_MIN: f32 = -7.0;
const RS485_COMMON_MODE_MAX: f32 = 12.0;
const RS485_MIN_DIFF_VOLTAGE: f32 = 0.2; // 200mV

// Baudrate definitions
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Baudrate {
    Bps9600 = 9600,
    Bps19200 = 19200,
    Bps93750 = 93750,
    Bps187500 = 187500,
    Bps500000 = 500000,
    Bps1500000 = 1500000,
    Bps12000000 = 12000000,
}

impl Baudrate {
    pub fn as_u32(&self) -> u32 {
        *self as u32
    }
    
    pub fn max_cable_length(&self) -> u16 {
        match self {
            Baudrate::Bps9600 | Baudrate::Bps19200 | Baudrate::Bps93750 => 1200,
            Baudrate::Bps187500 => 1000,
            Baudrate::Bps500000 => 400,
            Baudrate::Bps1500000 => 200,
            Baudrate::Bps12000000 => 100,
        }
    }
}

// RS-485 Signal validation
#[derive(Debug)]
pub struct SignalLevels {
    pub voltage_a: f32,
    pub voltage_b: f32,
}

impl SignalLevels {
    pub fn new(voltage_a: f32, voltage_b: f32) -> Self {
        SignalLevels { voltage_a, voltage_b }
    }
    
    pub fn differential_voltage(&self) -> f32 {
        self.voltage_a - self.voltage_b
    }
    
    pub fn common_mode_voltage(&self) -> f32 {
        (self.voltage_a + self.voltage_b) / 2.0
    }
    
    pub fn is_valid(&self) -> Result<(), SignalError> {
        // Check differential voltage threshold
        if self.differential_voltage().abs() < RS485_MIN_DIFF_VOLTAGE {
            return Err(SignalError::InsufficientDifferential(
                self.differential_voltage()
            ));
        }
        
        // Check common mode voltage range
        let cm_voltage = self.common_mode_voltage();
        if cm_voltage < RS485_COMMON_MODE_MIN || cm_voltage > RS485_COMMON_MODE_MAX {
            return Err(SignalError::CommonModeOutOfRange(cm_voltage));
        }
        
        Ok(())
    }
    
    pub fn logic_state(&self) -> LogicState {
        if self.voltage_a > self.voltage_b {
            LogicState::High // Mark
        } else {
            LogicState::Low  // Space
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum LogicState {
    High, // Mark: A > B
    Low,  // Space: B > A
}

#[derive(Debug)]
pub enum SignalError {
    InsufficientDifferential(f32),
    CommonModeOutOfRange(f32),
}

impl fmt::Display for SignalError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SignalError::InsufficientDifferential(v) => {
                write!(f, "Differential voltage {:.3}V below 200mV threshold", v)
            }
            SignalError::CommonModeOutOfRange(v) => {
                write!(f, "Common mode voltage {:.3}V outside -7V to +12V range", v)
            }
        }
    }
}

// RS-485 Transceiver Control
#[derive(Debug)]
pub struct RS485Transceiver {
    is_transmitting: bool,
}

impl RS485Transceiver {
    pub fn new() -> Self {
        RS485Transceiver {
            is_transmitting: false,
        }
    }
    
    pub fn set_transmit_mode(&mut self) {
        // In real hardware, this would:
        // - Set DE (Driver Enable) pin HIGH
        // - Set RE (Receiver Enable) pin HIGH (disabled, active low)
        self.is_transmitting = true;
        println!("Transceiver: Transmit mode enabled");
    }
    
    pub fn set_receive_mode(&mut self) {
        // In real hardware, this would:
        // - Set DE (Driver Enable) pin LOW
        // - Set RE (Receiver Enable) pin LOW (enabled, active low)
        self.is_transmitting = false;
        println!("Transceiver: Receive mode enabled");
    }
    
    pub fn is_transmitting(&self) -> bool {
        self.is_transmitting
    }
}

// Profibus DP Timing Calculations
#[derive(Debug)]
pub struct ProfibusTiming {
    pub baudrate: Baudrate,
    pub bit_time_us: u32,
    pub slot_time_us: u32,
    pub setup_time_us: u32,
}

impl ProfibusTiming {
    pub fn new(baudrate: Baudrate) -> Self {
        let bit_time_us = 1_000_000 / baudrate.as_u32();
        
        // Calculate slot time based on baudrate
        let slot_time_us = match baudrate {
            Baudrate::Bps9600 | Baudrate::Bps19200 => 400,
            Baudrate::Bps93750 | Baudrate::Bps187500 => 200,
            _ => 100,
        };
        
        ProfibusTiming {
            baudrate,
            bit_time_us,
            slot_time_us,
            setup_time_us: bit_time_us, // Setup time = 1 bit time
        }
    }
    
    pub fn bits_to_microseconds(&self, num_bits: u32) -> u32 {
        num_bits * self.bit_time_us
    }
}

// Cable specification structure
#[derive(Debug)]
pub struct CableSpec {
    pub impedance_ohm: u16,
    pub termination_ohm: u16,
    pub max_nodes: u8,
    pub length_m: u16,
}

impl CableSpec {
    pub fn for_baudrate(baudrate: Baudrate, length_m: u16) -> Result<Self, &'static str> {
        let max_length = baudrate.max_cable_length();
        
        if length_m > max_length {
            return Err("Cable length exceeds maximum for this baudrate");
        }
        
        Ok(CableSpec {
            impedance_ohm: 120,
            termination_ohm: RS485_TERMINATION_OHM,
            max_nodes: RS485_MAX_NODES,
            length_m,
        })
    }
}

// Example usage
fn main() {
    println!("=== RS-485 Profibus DP Physical Layer ===\n");
    
    // Create transceiver
    let mut transceiver = RS485Transceiver::new();
    
    // Test signal validation
    let signal = SignalLevels::new(2.5, -2.5);
    println!("Signal levels: A={:.2}V, B={:.2}V", signal.voltage_a, signal.voltage_b);
    println!("Differential: {:.2}V", signal.differential_voltage());
    println!("Common mode: {:.2}V", signal.common_mode_voltage());
    println!("Logic state: {:?}", signal.logic_state());
    
    match signal.is_valid() {
        Ok(_) => println!("Signal is valid\n"),
        Err(e) => println!("Signal error: {}\n", e),
    }
    
    // Calculate timing for 500 kbps
    let timing = ProfibusTiming::new(Baudrate::Bps500000);
    println!("Profibus Timing @ {:?}:", timing.baudrate);
    println!("  Bit time: {} µs", timing.bit_time_us);
    println!("  Slot time: {} µs", timing.slot_time_us);
    println!("  Setup time: {} µs", timing.setup_time_us);
    println!("  8-bit frame: {} µs\n", timing.bits_to_microseconds(8));
    
    // Check cable specifications
    let baudrate = Baudrate::Bps500000;
    println!("Cable specs @ {:?}:", baudrate);
    println!("  Max length: {} m", baudrate.max_cable_length());
    
    match CableSpec::for_baudrate(baudrate, 350) {
        Ok(spec) => {
            println!("  Impedance: {} Ω", spec.impedance_ohm);
            println!("  Termination: {} Ω", spec.termination_ohm);
            println!("  Max nodes: {}", spec.max_nodes);
            println!("  Cable length: {} m\n", spec.length_m);
        }
        Err(e) => println!("  Error: {}\n", e),
    }
    
    // Demonstrate transceiver control
    transceiver.set_transmit_mode();
    println!("Transmitting: {}\n", transceiver.is_transmitting());
    
    transceiver.set_receive_mode();
    println!("Transmitting: {}", transceiver.is_transmitting());
}
```

## Summary

**RS-485 Physical Layer for Profibus DP** forms the foundation of industrial communication by providing:

1. **Robust Differential Signaling**: Uses two wires (A and B) with ±200mV minimum differential voltage, providing excellent noise immunity in harsh industrial environments with common mode voltage tolerance from -7V to +12V.

2. **Distance and Speed Trade-offs**: Supports cable lengths from 100m at 12 Mbps up to 1200m at lower speeds (9.6-93.75 kbps), allowing flexible network topology design based on application requirements.

3. **Proper Termination**: Requires 120Ω termination resistors at both bus ends to prevent signal reflections and maintain signal integrity, with active termination recommended for optimal Profibus performance.

4. **Multi-drop Capability**: Supports up to 32 nodes per segment (expandable to 126 with repeaters) on a single twisted-pair cable, enabling cost-effective distributed control systems.

The code examples demonstrate practical implementation aspects including transceiver control (transmit/receive mode switching), signal validation (differential and common-mode voltage checking), cable length calculations based on baudrate, and Profibus-specific timing parameters. Both C/C++ and Rust implementations show how to manage the physical layer characteristics programmatically, which is essential for building reliable Profibus DP master and slave devices.