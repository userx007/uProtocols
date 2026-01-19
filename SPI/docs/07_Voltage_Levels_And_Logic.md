# SPI Voltage Levels and Logic: Detailed Guide

## Overview

When working with SPI (Serial Peripheral Interface) communication, one of the most critical considerations is ensuring electrical compatibility between devices. Different microcontrollers and peripherals operate at different logic voltage levels, primarily **3.3V** and **5V**. Mismatched voltage levels can lead to communication failures, signal integrity issues, or permanent damage to components.

## Core Concepts

### Voltage Level Standards

**5V Logic (TTL - Transistor-Transistor Logic):**
- Logic HIGH (VIH): Typically 2.0V minimum, 5V nominal
- Logic LOW (VIL): Typically 0.8V maximum, 0V nominal
- Output HIGH (VOH): Typically 2.4V minimum
- Output LOW (VOL): Typically 0.4V maximum

**3.3V Logic (CMOS - Complementary Metal-Oxide Semiconductor):**
- Logic HIGH (VIH): Typically 2.0V minimum, 3.3V nominal
- Logic LOW (VIL): Typically 0.8V maximum, 0V nominal
- Output HIGH (VOH): Typically 2.4V minimum
- Output LOW (VOL): Typically 0.4V maximum

### The Problem

When a 5V device drives a 3.3V input, the 5V signal can exceed the maximum voltage rating of the 3.3V device, potentially causing:
- Latchup conditions in CMOS devices
- Permanent damage to input protection diodes
- Degraded device lifespan
- Erratic behavior

### Solution Approaches

1. **Voltage Divider** (unidirectional, low-speed only)
2. **Level Shifter ICs** (bidirectional, fast switching)
3. **MOSFET-based shifters** (bidirectional, cost-effective)
4. **Diode clamp circuits** (protection)
5. **5V-tolerant inputs** (check datasheet!)

## Signal Integrity Considerations

- **Rise/Fall Times**: Faster edges can cause ringing and reflections
- **Trace Impedance**: Keep SPI traces short (<6 inches for high-speed)
- **Capacitive Loading**: Minimize stray capacitance on clock and data lines
- **Ground Bounce**: Ensure solid ground connections
- **EMI/RFI**: Use proper decoupling capacitors near devices

## Code Examples

### C/C++ Implementation (Arduino/ESP32)

```c
// SPI Master on ESP32 (3.3V) communicating with 5V device
// Using hardware SPI with level shifter

#include <SPI.h>

// Pin definitions
#define SPI_CS    5   // Chip Select
#define SPI_MOSI  23  // Master Out Slave In (through level shifter)
#define SPI_MISO  19  // Master In Slave Out (through level shifter)
#define SPI_CLK   18  // Clock (through level shifter)

// SPI configuration structure
struct SPIConfig {
    uint32_t clockSpeed;
    uint8_t dataMode;
    uint8_t bitOrder;
    bool enableLevelShift;
};

class SafeSPIController {
private:
    SPIConfig config;
    SPIClass* spiInstance;
    
public:
    SafeSPIController(SPIClass* spi = &SPI) : spiInstance(spi) {
        config.clockSpeed = 1000000;  // 1 MHz for safe operation with level shifters
        config.dataMode = SPI_MODE0;
        config.bitOrder = MSBFIRST;
        config.enableLevelShift = true;
    }
    
    // Initialize SPI with voltage level considerations
    bool begin() {
        pinMode(SPI_CS, OUTPUT);
        digitalWrite(SPI_CS, HIGH);
        
        // Reduced clock speed for level shifter propagation delay
        spiInstance->begin(SPI_CLK, SPI_MISO, SPI_MOSI);
        spiInstance->setFrequency(config.clockSpeed);
        spiInstance->setDataMode(config.dataMode);
        spiInstance->setBitOrder(config.bitOrder);
        
        return true;
    }
    
    // Transfer with timing considerations for level shifters
    uint8_t transferByte(uint8_t data) {
        digitalWrite(SPI_CS, LOW);
        delayMicroseconds(1);  // CS setup time for level shifter
        
        uint8_t result = spiInstance->transfer(data);
        
        delayMicroseconds(1);  // CS hold time
        digitalWrite(SPI_CS, HIGH);
        
        return result;
    }
    
    // Bulk transfer with proper timing
    void transferBuffer(uint8_t* txBuffer, uint8_t* rxBuffer, size_t length) {
        digitalWrite(SPI_CS, LOW);
        delayMicroseconds(1);
        
        for (size_t i = 0; i < length; i++) {
            rxBuffer[i] = spiInstance->transfer(txBuffer[i]);
        }
        
        delayMicroseconds(1);
        digitalWrite(SPI_CS, HIGH);
    }
    
    // Adjust clock speed based on signal integrity requirements
    void setClockSpeed(uint32_t speed) {
        // Limit speed when using level shifters (typical max: 10-20 MHz)
        if (config.enableLevelShift && speed > 10000000) {
            speed = 10000000;  // Cap at 10 MHz
        }
        config.clockSpeed = speed;
        spiInstance->setFrequency(config.clockSpeed);
    }
};

// Example: Reading from a 5V SPI sensor
void setup() {
    Serial.begin(115200);
    
    SafeSPIController spiController;
    
    if (spiController.begin()) {
        Serial.println("SPI initialized with level shifting");
        
        // Read device ID from sensor
        uint8_t txData[2] = {0x0F, 0x00};  // Read ID register command
        uint8_t rxData[2] = {0};
        
        spiController.transferBuffer(txData, rxData, 2);
        
        Serial.print("Device ID: 0x");
        Serial.println(rxData[1], HEX);
    }
}

void loop() {
    // Main loop
}
```

### Advanced C Example: Software-Controlled Level Shifting

```c
#include <stdint.h>
#include <stdbool.h>

// GPIO control (platform-specific, example for STM32)
#define GPIO_SET(port, pin)   ((port)->BSRR = (1 << (pin)))
#define GPIO_CLEAR(port, pin) ((port)->BSRR = (1 << ((pin) + 16)))
#define GPIO_READ(port, pin)  (((port)->IDR >> (pin)) & 1)

// Voltage level detection and adaptation
typedef enum {
    LOGIC_LEVEL_3V3,
    LOGIC_LEVEL_5V,
    LOGIC_LEVEL_AUTO
} LogicLevel_t;

typedef struct {
    void* csPort;
    uint8_t csPin;
    void* mosiPort;
    uint8_t mosiPin;
    void* misoPort;
    uint8_t misoPin;
    void* clkPort;
    uint8_t clkPin;
    LogicLevel_t logicLevel;
    uint32_t clockDelay;  // Delay in microseconds
} BitBangSPI_t;

// Bit-bang SPI with voltage level awareness
void spi_bitbang_init(BitBangSPI_t* spi, LogicLevel_t level) {
    spi->logicLevel = level;
    
    // Adjust timing based on logic level
    if (level == LOGIC_LEVEL_5V) {
        // Slower for 5V devices with level shifters
        spi->clockDelay = 2;  // 250 kHz max
    } else {
        // Faster for direct 3.3V communication
        spi->clockDelay = 1;  // 500 kHz max
    }
}

uint8_t spi_bitbang_transfer(BitBangSPI_t* spi, uint8_t data) {
    uint8_t result = 0;
    
    for (int i = 7; i >= 0; i--) {
        // Set MOSI
        if (data & (1 << i)) {
            GPIO_SET(spi->mosiPort, spi->mosiPin);
        } else {
            GPIO_CLEAR(spi->mosiPort, spi->mosiPin);
        }
        
        // Delay for setup time
        delay_us(spi->clockDelay);
        
        // Clock high
        GPIO_SET(spi->clkPort, spi->clkPin);
        delay_us(spi->clockDelay);
        
        // Read MISO
        result |= (GPIO_READ(spi->misoPort, spi->misoPin) << i);
        
        // Clock low
        GPIO_CLEAR(spi->clkPort, spi->clkPin);
        delay_us(spi->clockDelay);
    }
    
    return result;
}

// Voltage level auto-detection (requires ADC)
LogicLevel_t detect_logic_level(uint16_t adc_reading) {
    // Assuming 12-bit ADC with 3.3V reference
    // 3.3V input = 4095, 5V input would be clamped/divided
    if (adc_reading > 3500) {
        return LOGIC_LEVEL_5V;
    } else if (adc_reading > 2000) {
        return LOGIC_LEVEL_3V3;
    }
    return LOGIC_LEVEL_AUTO;
}
```

### Rust Implementation

```rust
// SPI with voltage level safety using embedded-hal
use embedded_hal::blocking::spi::{Transfer, Write};
use embedded_hal::digital::v2::OutputPin;

/// Voltage level enum
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum VoltageLevel {
    V3_3,
    V5_0,
}

/// SPI configuration with voltage level awareness
pub struct SafeSpiConfig {
    voltage_level: VoltageLevel,
    max_frequency: u32,
    use_level_shifter: bool,
}

impl SafeSpiConfig {
    /// Create new configuration with voltage level
    pub fn new(voltage_level: VoltageLevel) -> Self {
        let (max_frequency, use_level_shifter) = match voltage_level {
            VoltageLevel::V3_3 => (20_000_000, false), // 20 MHz direct
            VoltageLevel::V5_0 => (10_000_000, true),  // 10 MHz with shifter
        };
        
        Self {
            voltage_level,
            max_frequency,
            use_level_shifter,
        }
    }
    
    /// Get recommended clock frequency
    pub fn clock_frequency(&self) -> u32 {
        self.max_frequency
    }
    
    /// Check if level shifter is needed
    pub fn needs_level_shifter(&self) -> bool {
        self.use_level_shifter
    }
}

/// Safe SPI device wrapper
pub struct SafeSpiDevice<SPI, CS> {
    spi: SPI,
    cs: CS,
    config: SafeSpiConfig,
}

impl<SPI, CS, E> SafeSpiDevice<SPI, CS>
where
    SPI: Transfer<u8, Error = E> + Write<u8, Error = E>,
    CS: OutputPin,
{
    /// Create new SPI device with voltage level configuration
    pub fn new(spi: SPI, cs: CS, config: SafeSpiConfig) -> Self {
        Self { spi, cs, config }
    }
    
    /// Transfer single byte with proper timing
    pub fn transfer_byte(&mut self, data: u8) -> Result<u8, E> {
        self.cs.set_low().ok();
        
        // Add delay for level shifter propagation if needed
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100); // ~1μs at 100MHz
        }
        
        let mut buffer = [data];
        self.spi.transfer(&mut buffer)?;
        
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100);
        }
        
        self.cs.set_high().ok();
        Ok(buffer[0])
    }
    
    /// Transfer buffer with voltage-aware timing
    pub fn transfer_buffer(&mut self, buffer: &mut [u8]) -> Result<(), E> {
        self.cs.set_low().ok();
        
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100);
        }
        
        self.spi.transfer(buffer)?;
        
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100);
        }
        
        self.cs.set_high().ok();
        Ok(())
    }
    
    /// Write-only operation
    pub fn write_buffer(&mut self, buffer: &[u8]) -> Result<(), E> {
        self.cs.set_low().ok();
        
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100);
        }
        
        self.spi.write(buffer)?;
        
        if self.config.use_level_shifter {
            cortex_m::asm::delay(100);
        }
        
        self.cs.set_high().ok();
        Ok(())
    }
}

/// Example: Using the safe SPI wrapper
#[cfg(feature = "example")]
mod example {
    use super::*;
    
    pub fn spi_example<SPI, CS>(spi: SPI, cs: CS) 
    where
        SPI: Transfer<u8> + Write<u8>,
        CS: OutputPin,
    {
        // Configure for 5V device with level shifter
        let config = SafeSpiConfig::new(VoltageLevel::V5_0);
        let mut device = SafeSpiDevice::new(spi, cs, config);
        
        // Read device ID
        let mut cmd_buffer = [0x0F_u8, 0x00];
        if let Ok(_) = device.transfer_buffer(&mut cmd_buffer) {
            println!("Device ID: 0x{:02X}", cmd_buffer[1]);
        }
        
        // Write configuration
        let write_data = [0x20, 0x47]; // Example command
        let _ = device.write_buffer(&write_data);
    }
}

/// Type-safe voltage level conversion utilities
pub mod level_shifter {
    use super::VoltageLevel;
    
    /// Calculate voltage divider resistor values
    pub fn voltage_divider_resistors(
        v_in: VoltageLevel,
        v_out: VoltageLevel,
    ) -> Option<(u32, u32)> {
        match (v_in, v_out) {
            (VoltageLevel::V5_0, VoltageLevel::V3_3) => {
                // R1 = 1kΩ, R2 = 2kΩ gives ~3.33V from 5V
                Some((1000, 2000))
            }
            (VoltageLevel::V3_3, VoltageLevel::V5_0) => {
                // Cannot step up voltage with resistors alone
                None
            }
            _ => Some((0, 0)), // Same voltage, no divider needed
        }
    }
    
    /// Check if levels are compatible without shifting
    pub fn is_compatible(master: VoltageLevel, slave: VoltageLevel) -> bool {
        match (master, slave) {
            (VoltageLevel::V3_3, VoltageLevel::V3_3) => true,
            (VoltageLevel::V5_0, VoltageLevel::V5_0) => true,
            (VoltageLevel::V3_3, VoltageLevel::V5_0) => false, // Need pull-up
            (VoltageLevel::V5_0, VoltageLevel::V3_3) => false, // Dangerous!
        }
    }
}
```

## Summary

**Key Takeaways:**

1. **Voltage Mismatch is Dangerous**: Always verify logic levels before connecting SPI devices. A 5V output can damage 3.3V inputs.

2. **Level Shifters Are Essential**: When interfacing different voltage domains, use proper bidirectional level shifters (like TXS0108E, TXB0104) rather than simple resistor dividers.

3. **Speed Considerations**: Level shifters introduce propagation delays (typically 1-5ns), so reduce SPI clock speeds accordingly—typically 10-20 MHz maximum with level shifters vs. 50+ MHz for direct connections.

4. **Signal Integrity Matters**: Keep traces short, use proper grounding, add decoupling capacitors (0.1µF near each device), and consider termination resistors for long traces or high speeds.

5. **Always Check Datasheets**: Some 3.3V devices have 5V-tolerant inputs (marked as "5VT"), eliminating the need for level shifters on those specific pins.

6. **Software Timing**: Add small delays (1-2µs) around chip select operations when using level shifters to ensure proper signal settling.

The code examples demonstrate practical implementations in C/C++ and Rust, showing how to handle voltage level considerations, adjust timing parameters, and implement safe SPI communication across voltage domains. The Rust example additionally provides type-safe abstractions and compile-time guarantees for voltage level compatibility.