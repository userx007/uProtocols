# I²C Clock and Data Lines: A Comprehensive Guide

I'll provide a detailed exploration of I²C's SCL (Serial Clock) and SDA (Serial Data) lines, covering their electrical characteristics, pull-up resistor requirements, and practical implementation considerations.

## Overview of I²C Signal Lines

I²C (Inter-Integrated Circuit) uses just two bidirectional lines for communication:
- **SCL (Serial Clock)**: The clock line, driven by the master device
- **SDA (Serial Data)**: The data line, used for bidirectional data transfer

Both lines use an open-drain (or open-collector) configuration, requiring external pull-up resistors to function correctly.

## Electrical Characteristics

### Open-Drain Configuration

I²C devices never actively drive the lines HIGH. Instead:
- Devices can only pull lines LOW (to ground)
- External pull-up resistors pull lines HIGH when no device is driving them LOW
- This creates a "wired-AND" configuration where any device can pull the line LOW

**Why open-drain?**
- Allows multiple devices to safely share the same bus
- Prevents bus contention (no device ever "fights" another)
- Enables clock stretching (slaves can hold SCL LOW to slow down the master)
- Supports multi-master configurations

### Voltage Levels

Standard I²C voltage thresholds (for 5V systems):
- **V_IL (Input Low)**: Maximum 0.3 × V_DD (1.5V for 5V systems)
- **V_IH (Input High)**: Minimum 0.7 × V_DD (3.5V for 5V systems)
- **V_OL (Output Low)**: Maximum 0.4V at 3mA sink current

For 3.3V systems, these scale proportionally. Many modern devices support both 3.3V and 5V operation, but level shifters may be needed when mixing voltage levels on the same bus.

### Speed Modes and Timing

I²C defines several speed modes with different timing requirements:

| Mode | Max Frequency | Rise Time (t_r) | Fall Time (t_f) |
|------|---------------|-----------------|-----------------|
| Standard | 100 kHz | 1000 ns | 300 ns |
| Fast | 400 kHz | 300 ns | 300 ns |
| Fast Plus | 1 MHz | 120 ns | 120 ns |
| High Speed | 3.4 MHz | 80 ns | 80 ns |

## Pull-up Resistor Calculations

Selecting the correct pull-up resistor value is crucial for reliable I²C operation. The value must balance several competing requirements.

### Minimum Resistance (R_min)

The pull-up must not exceed the bus driver's current sinking capability:

```
R_min = V_DD / I_OL
```

Where:
- V_DD is the supply voltage (e.g., 5V or 3.3V)
- I_OL is the maximum sink current (typically 3mA for standard I²C)

**Example**: For 5V systems with 3mA sink current:
```
R_min = 5V / 0.003A = 1.67 kΩ
```

Use 2.2 kΩ or higher in practice to provide margin.

### Maximum Resistance (R_max)

The pull-up must charge the bus capacitance quickly enough to meet rise time requirements:

```
R_max = t_r / (0.8473 × C_bus)
```

Where:
- t_r is the maximum rise time for your speed mode
- C_bus is the total bus capacitance
- The factor 0.8473 comes from RC charging to 70% of V_DD

**Example**: For Fast mode (400 kHz) with 100 pF bus capacitance:
```
R_max = 300ns / (0.8473 × 100pF) ≈ 3.5 kΩ
```

### Bus Capacitance

Total bus capacitance includes:
- Wire/trace capacitance: ~10-15 pF per inch for typical PCB traces
- Device input capacitance: 3-10 pF per device (check datasheets)
- Connector capacitance if present

Maximum bus capacitance per speed mode:
- Standard mode: 400 pF
- Fast mode: 400 pF
- Fast Plus: 550 pF

### Practical Pull-up Values

Common pull-up resistor values for different scenarios:

- **Short buses (<10cm), few devices**: 2.2 kΩ - 4.7 kΩ
- **Medium buses (10-30cm), several devices**: 4.7 kΩ - 10 kΩ
- **Long buses (>30cm), many devices**: 10 kΩ - 47 kΩ (may limit speed)

For 3.3V systems, values are typically 10-20% higher than equivalent 5V systems.

## Signal Characteristics and Waveforms

### Clock Signal (SCL)

The clock signal exhibits several important characteristics:

1. **Clock stretching**: Slave devices can hold SCL LOW to pause communication
2. **Clock synchronization**: In multi-master systems, the slowest clock wins
3. **Duty cycle**: Not required to be 50%, but typically 40-60% in practice

### Data Signal (SDA)

Data transitions must occur when SCL is LOW, with data stable when SCL is HIGH:

1. **Setup time (t_su;dat)**: Minimum 100ns (Fast mode) before SCL rises
2. **Hold time (t_hd;dat)**: Minimum 0ns (Fast mode) after SCL falls
3. **SDA must be stable** while SCL is HIGH (except for START/STOP conditions)

### START and STOP Conditions

These are the only times SDA can transition while SCL is HIGH:

- **START**: SDA transitions HIGH→LOW while SCL is HIGH
- **STOP**: SDA transitions LOW→HIGH while SCL is HIGH
- **Repeated START**: START condition without preceding STOP

## Code Examples

### C/C++ Implementation (Bit-banging)

```C

/**
 * I2C Software (Bit-bang) Implementation
 * 
 * This example demonstrates low-level I2C control of SCL and SDA lines.
 * Assumes GPIO functions are available for pin control.
 */

#include <stdint.h>
#include <stdbool.h>

// Platform-specific GPIO functions (adjust for your hardware)
// These would typically be provided by your HAL or register access
extern void gpio_set_high(uint8_t pin);
extern void gpio_set_low(uint8_t pin);
extern bool gpio_read(uint8_t pin);
extern void gpio_set_input(uint8_t pin);
extern void gpio_set_output_open_drain(uint8_t pin);
extern void delay_us(uint32_t microseconds);

// I2C Configuration
typedef struct {
    uint8_t scl_pin;
    uint8_t sda_pin;
    uint32_t clock_speed_hz;  // e.g., 100000 for 100kHz
} I2C_Config;

// Calculate half-period delay for given clock speed
#define CALC_HALF_PERIOD_US(speed_hz) (500000 / (speed_hz))

static I2C_Config i2c_config;
static uint32_t i2c_half_period_us;

/**
 * Initialize I2C pins and configuration
 */
void i2c_init(uint8_t scl_pin, uint8_t sda_pin, uint32_t speed_hz) {
    i2c_config.scl_pin = scl_pin;
    i2c_config.sda_pin = sda_pin;
    i2c_config.clock_speed_hz = speed_hz;
    i2c_half_period_us = CALC_HALF_PERIOD_US(speed_hz);
    
    // Configure pins as open-drain outputs
    // Pull-up resistors must be present externally
    gpio_set_output_open_drain(scl_pin);
    gpio_set_output_open_drain(sda_pin);
    
    // Release both lines (HIGH due to pull-ups)
    gpio_set_high(scl_pin);
    gpio_set_high(sda_pin);
}

/**
 * Set SCL line state (open-drain)
 * HIGH releases the line, LOW pulls it down
 */
static inline void scl_set(bool state) {
    if (state) {
        gpio_set_high(i2c_config.scl_pin);
    } else {
        gpio_set_low(i2c_config.scl_pin);
    }
}

/**
 * Set SDA line state (open-drain)
 */
static inline void sda_set(bool state) {
    if (state) {
        gpio_set_high(i2c_config.sda_pin);
    } else {
        gpio_set_low(i2c_config.sda_pin);
    }
}

/**
 * Read SDA line state
 */
static inline bool sda_read(void) {
    return gpio_read(i2c_config.sda_pin);
}

/**
 * Read SCL line state (useful for clock stretching detection)
 */
static inline bool scl_read(void) {
    return gpio_read(i2c_config.scl_pin);
}

/**
 * Wait for SCL to go HIGH (handles clock stretching)
 * Returns false on timeout
 */
static bool scl_wait_high(uint32_t timeout_us) {
    uint32_t elapsed = 0;
    const uint32_t poll_interval = 1;
    
    while (!scl_read() && elapsed < timeout_us) {
        delay_us(poll_interval);
        elapsed += poll_interval;
    }
    
    return elapsed < timeout_us;
}

/**
 * I2C START condition
 * SDA transitions HIGH -> LOW while SCL is HIGH
 */
bool i2c_start(void) {
    // Ensure both lines are HIGH
    sda_set(true);
    delay_us(i2c_half_period_us);
    scl_set(true);
    delay_us(i2c_half_period_us);
    
    // Check if bus is free
    if (!sda_read() || !scl_read()) {
        return false;  // Bus is busy
    }
    
    // Generate START: SDA LOW while SCL HIGH
    sda_set(false);
    delay_us(i2c_half_period_us);
    scl_set(false);
    delay_us(i2c_half_period_us);
    
    return true;
}

/**
 * I2C STOP condition
 * SDA transitions LOW -> HIGH while SCL is HIGH
 */
void i2c_stop(void) {
    // Ensure SDA is LOW
    sda_set(false);
    delay_us(i2c_half_period_us);
    
    // Release SCL
    scl_set(true);
    if (!scl_wait_high(1000)) {
        // Clock stretching timeout - handle error
    }
    delay_us(i2c_half_period_us);
    
    // Generate STOP: SDA HIGH while SCL HIGH
    sda_set(true);
    delay_us(i2c_half_period_us);
}

/**
 * Write a single bit to I2C bus
 */
static void i2c_write_bit(bool bit) {
    // Set data while clock is LOW
    sda_set(bit);
    delay_us(i2c_half_period_us);
    
    // Clock HIGH - data is read by slave
    scl_set(true);
    if (!scl_wait_high(1000)) {
        // Clock stretching timeout
    }
    delay_us(i2c_half_period_us);
    
    // Clock LOW
    scl_set(false);
    delay_us(i2c_half_period_us);
}

/**
 * Read a single bit from I2C bus
 */
static bool i2c_read_bit(void) {
    bool bit;
    
    // Release SDA so slave can control it
    sda_set(true);
    delay_us(i2c_half_period_us);
    
    // Clock HIGH - sample data
    scl_set(true);
    if (!scl_wait_high(1000)) {
        // Clock stretching timeout
    }
    delay_us(i2c_half_period_us);
    
    // Read the bit
    bit = sda_read();
    
    // Clock LOW
    scl_set(false);
    delay_us(i2c_half_period_us);
    
    return bit;
}

/**
 * Write a byte to I2C bus
 * Returns true if ACK received, false if NACK
 */
bool i2c_write_byte(uint8_t data) {
    // Send 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit((data >> i) & 0x01);
    }
    
    // Read ACK/NACK bit
    bool ack = !i2c_read_bit();  // ACK is LOW, NACK is HIGH
    
    return ack;
}

/**
 * Read a byte from I2C bus
 * send_ack: true to send ACK, false to send NACK
 */
uint8_t i2c_read_byte(bool send_ack) {
    uint8_t data = 0;
    
    // Read 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        if (i2c_read_bit()) {
            data |= (1 << i);
        }
    }
    
    // Send ACK or NACK
    i2c_write_bit(!send_ack);  // ACK is LOW, NACK is HIGH
    
    return data;
}

/**
 * Complete I2C write transaction
 * Returns true on success
 */
bool i2c_write(uint8_t device_addr, const uint8_t *data, size_t length) {
    if (!i2c_start()) {
        return false;
    }
    
    // Send device address with write bit (0)
    if (!i2c_write_byte(device_addr << 1)) {
        i2c_stop();
        return false;  // No ACK from device
    }
    
    // Send data bytes
    for (size_t i = 0; i < length; i++) {
        if (!i2c_write_byte(data[i])) {
            i2c_stop();
            return false;  // No ACK
        }
    }
    
    i2c_stop();
    return true;
}

/**
 * Complete I2C read transaction
 * Returns true on success
 */
bool i2c_read(uint8_t device_addr, uint8_t *data, size_t length) {
    if (!i2c_start()) {
        return false;
    }
    
    // Send device address with read bit (1)
    if (!i2c_write_byte((device_addr << 1) | 0x01)) {
        i2c_stop();
        return false;  // No ACK from device
    }
    
    // Read data bytes
    for (size_t i = 0; i < length; i++) {
        // Send ACK for all bytes except the last one
        bool send_ack = (i < length - 1);
        data[i] = i2c_read_byte(send_ack);
    }
    
    i2c_stop();
    return true;
}

/**
 * Example usage: Read temperature from sensor
 */
void example_read_temperature(void) {
    const uint8_t TEMP_SENSOR_ADDR = 0x48;
    const uint8_t TEMP_REG = 0x00;
    uint8_t temp_data[2];
    
    // Initialize I2C at 100kHz
    i2c_init(5, 4, 100000);  // SCL=pin5, SDA=pin4, 100kHz
    
    // Write register address
    i2c_write(TEMP_SENSOR_ADDR, &TEMP_REG, 1);
    
    // Read 2 bytes of temperature data
    if (i2c_read(TEMP_SENSOR_ADDR, temp_data, 2)) {
        int16_t raw_temp = (temp_data[0] << 8) | temp_data[1];
        float temperature = raw_temp * 0.0625f;  // Example conversion
        // Use temperature value...
    }
}
```

### Rust Implementation (Embedded HAL)

```rust
/**
 * I2C Implementation Examples in Rust
 * 
 * This demonstrates both bit-banging and using embedded-hal traits
 * for I2C communication with proper error handling.
 */

use core::convert::Infallible;
use embedded_hal::blocking::i2c::{Read, Write, WriteRead};
use embedded_hal::digital::v2::{InputPin, OutputPin};

// ============================================================================
// Bit-bang I2C Implementation
// ============================================================================

/// I2C error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    BusBusy,
    NoAcknowledge,
    ClockStretchTimeout,
    InvalidParameter,
}

/// I2C bit-bang driver using GPIO pins
pub struct I2cBitBang<SCL, SDA> {
    scl: SCL,
    sda: SDA,
    half_period_us: u32,
}

impl<SCL, SDA, E> I2cBitBang<SCL, SDA>
where
    SCL: OutputPin<Error = E> + InputPin<Error = E>,
    SDA: OutputPin<Error = E> + InputPin<Error = E>,
{
    /// Create a new I2C bit-bang driver
    /// 
    /// # Arguments
    /// * `scl` - SCL pin (must support open-drain or simulated open-drain)
    /// * `sda` - SDA pin (must support open-drain or simulated open-drain)
    /// * `frequency_hz` - Desired I2C clock frequency
    pub fn new(mut scl: SCL, mut sda: SDA, frequency_hz: u32) -> Result<Self, E> {
        // Calculate half period in microseconds
        let half_period_us = 500_000 / frequency_hz;
        
        // Initialize pins to HIGH (released, pulled up externally)
        scl.set_high()?;
        sda.set_high()?;
        
        Ok(Self {
            scl,
            sda,
            half_period_us,
        })
    }
    
    /// Delay for I2C timing (platform-specific implementation needed)
    #[inline]
    fn delay_us(&self, us: u32) {
        // This would use your platform's delay function
        // For example: cortex_m::asm::delay(cycles)
        for _ in 0..(us * 10) {
            cortex_m::asm::nop();
        }
    }
    
    /// Wait for SCL to go HIGH (clock stretching support)
    fn wait_scl_high(&mut self, timeout_us: u32) -> Result<(), I2cError> {
        let mut elapsed = 0;
        const POLL_INTERVAL: u32 = 1;
        
        while self.scl.is_low().map_err(|_| I2cError::ClockStretchTimeout)? {
            if elapsed >= timeout_us {
                return Err(I2cError::ClockStretchTimeout);
            }
            self.delay_us(POLL_INTERVAL);
            elapsed += POLL_INTERVAL;
        }
        
        Ok(())
    }
    
    /// Generate I2C START condition
    /// SDA: HIGH -> LOW while SCL is HIGH
    pub fn start(&mut self) -> Result<(), I2cError> {
        // Ensure both lines are HIGH initially
        self.sda.set_high().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        self.scl.set_high().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        // Check if bus is free
        if self.sda.is_low().map_err(|_| I2cError::BusBusy)? ||
           self.scl.is_low().map_err(|_| I2cError::BusBusy)? {
            return Err(I2cError::BusBusy);
        }
        
        // Generate START: SDA LOW while SCL HIGH
        self.sda.set_low().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        self.scl.set_low().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        Ok(())
    }
    
    /// Generate I2C STOP condition
    /// SDA: LOW -> HIGH while SCL is HIGH
    pub fn stop(&mut self) -> Result<(), I2cError> {
        // Ensure SDA is LOW
        self.sda.set_low().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        // Release SCL
        self.scl.set_high().map_err(|_| I2cError::BusBusy)?;
        self.wait_scl_high(1000)?;
        self.delay_us(self.half_period_us);
        
        // Generate STOP: SDA HIGH while SCL HIGH
        self.sda.set_high().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        Ok(())
    }
    
    /// Write a single bit
    fn write_bit(&mut self, bit: bool) -> Result<(), I2cError> {
        // Set data while clock is LOW
        if bit {
            self.sda.set_high().map_err(|_| I2cError::BusBusy)?;
        } else {
            self.sda.set_low().map_err(|_| I2cError::BusBusy)?;
        }
        self.delay_us(self.half_period_us);
        
        // Clock HIGH - data is sampled by slave
        self.scl.set_high().map_err(|_| I2cError::BusBusy)?;
        self.wait_scl_high(1000)?;
        self.delay_us(self.half_period_us);
        
        // Clock LOW
        self.scl.set_low().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        Ok(())
    }
    
    /// Read a single bit
    fn read_bit(&mut self) -> Result<bool, I2cError> {
        // Release SDA so slave can control it
        self.sda.set_high().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        // Clock HIGH - sample data
        self.scl.set_high().map_err(|_| I2cError::BusBusy)?;
        self.wait_scl_high(1000)?;
        self.delay_us(self.half_period_us);
        
        // Read the bit
        let bit = self.sda.is_high().map_err(|_| I2cError::BusBusy)?;
        
        // Clock LOW
        self.scl.set_low().map_err(|_| I2cError::BusBusy)?;
        self.delay_us(self.half_period_us);
        
        Ok(bit)
    }
    
    /// Write a byte to the I2C bus
    /// Returns Ok(true) if ACK received, Ok(false) if NACK
    pub fn write_byte(&mut self, data: u8) -> Result<bool, I2cError> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            let bit = (data >> i) & 0x01 != 0;
            self.write_bit(bit)?;
        }
        
        // Read ACK/NACK bit (ACK = LOW, NACK = HIGH)
        let ack = !self.read_bit()?;
        
        Ok(ack)
    }
    
    /// Read a byte from the I2C bus
    /// send_ack: true to send ACK, false to send NACK
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut data = 0u8;
        
        // Read 8 bits, MSB first
        for i in (0..8).rev() {
            if self.read_bit()? {
                data |= 1 << i;
            }
        }
        
        // Send ACK or NACK (ACK = LOW, NACK = HIGH)
        self.write_bit(!send_ack)?;
        
        Ok(data)
    }
}

// Implement embedded-hal Write trait
impl<SCL, SDA, E> Write for I2cBitBang<SCL, SDA>
where
    SCL: OutputPin<Error = E> + InputPin<Error = E>,
    SDA: OutputPin<Error = E> + InputPin<Error = E>,
{
    type Error = I2cError;
    
    fn write(&mut self, addr: u8, bytes: &[u8]) -> Result<(), Self::Error> {
        self.start()?;
        
        // Send address with write bit (0)
        if !self.write_byte(addr << 1)? {
            self.stop()?;
            return Err(I2cError::NoAcknowledge);
        }
        
        // Send data bytes
        for &byte in bytes {
            if !self.write_byte(byte)? {
                self.stop()?;
                return Err(I2cError::NoAcknowledge);
            }
        }
        
        self.stop()?;
        Ok(())
    }
}

// Implement embedded-hal Read trait
impl<SCL, SDA, E> Read for I2cBitBang<SCL, SDA>
where
    SCL: OutputPin<Error = E> + InputPin<Error = E>,
    SDA: OutputPin<Error = E> + InputPin<Error = E>,
{
    type Error = I2cError;
    
    fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), Self::Error> {
        if buffer.is_empty() {
            return Err(I2cError::InvalidParameter);
        }
        
        self.start()?;
        
        // Send address with read bit (1)
        if !self.write_byte((addr << 1) | 0x01)? {
            self.stop()?;
            return Err(I2cError::NoAcknowledge);
        }
        
        // Read data bytes
        let last_idx = buffer.len() - 1;
        for (i, byte) in buffer.iter_mut().enumerate() {
            // Send ACK for all bytes except the last one
            let send_ack = i < last_idx;
            *byte = self.read_byte(send_ack)?;
        }
        
        self.stop()?;
        Ok(())
    }
}

// Implement embedded-hal WriteRead trait
impl<SCL, SDA, E> WriteRead for I2cBitBang<SCL, SDA>
where
    SCL: OutputPin<Error = E> + InputPin<Error = E>,
    SDA: OutputPin<Error = E> + InputPin<Error = E>,
{
    type Error = I2cError;
    
    fn write_read(&mut self, addr: u8, bytes: &[u8], buffer: &mut [u8]) 
        -> Result<(), Self::Error> 
    {
        // Write phase
        self.start()?;
        
        if !self.write_byte(addr << 1)? {
            self.stop()?;
            return Err(I2cError::NoAcknowledge);
        }
        
        for &byte in bytes {
            if !self.write_byte(byte)? {
                self.stop()?;
                return Err(I2cError::NoAcknowledge);
            }
        }
        
        // Repeated START for read phase
        self.start()?;
        
        if !self.write_byte((addr << 1) | 0x01)? {
            self.stop()?;
            return Err(I2cError::NoAcknowledge);
        }
        
        let last_idx = buffer.len() - 1;
        for (i, byte) in buffer.iter_mut().enumerate() {
            let send_ack = i < last_idx;
            *byte = self.read_byte(send_ack)?;
        }
        
        self.stop()?;
        Ok(())
    }
}

// ============================================================================
// Example Usage with embedded-hal
// ============================================================================

/// Example: Reading from a temperature sensor
pub fn read_temperature_sensor<I2C>(i2c: &mut I2C) -> Result<f32, I2cError>
where
    I2C: WriteRead<Error = I2cError>,
{
    const TEMP_SENSOR_ADDR: u8 = 0x48;
    const TEMP_REGISTER: u8 = 0x00;
    
    let mut temp_data = [0u8; 2];
    
    // Write register address, then read 2 bytes
    i2c.write_read(TEMP_SENSOR_ADDR, &[TEMP_REGISTER], &mut temp_data)?;
    
    // Convert to temperature (example conversion for typical sensor)
    let raw_temp = i16::from_be_bytes(temp_data);
    let temperature = (raw_temp as f32) * 0.0625;
    
    Ok(temperature)
}

/// Example: Writing configuration to a device
pub fn configure_device<I2C>(i2c: &mut I2C) -> Result<(), I2cError>
where
    I2C: Write<Error = I2cError>,
{
    const DEVICE_ADDR: u8 = 0x50;
    const CONFIG_REG: u8 = 0x01;
    const CONFIG_VALUE: u8 = 0b10101010;
    
    // Write register address followed by value
    i2c.write(DEVICE_ADDR, &[CONFIG_REG, CONFIG_VALUE])?;
    
    Ok(())
}

// ============================================================================
// Pull-up Resistor Calculator
// ============================================================================

/// Calculate appropriate pull-up resistor values for I2C bus
pub struct PullUpCalculator {
    v_dd: f32,           // Supply voltage
    i_ol_max: f32,       // Max sink current (typically 3mA)
    c_bus: f32,          // Total bus capacitance in pF
    rise_time_ns: f32,   // Maximum rise time in ns
}

impl PullUpCalculator {
    pub fn new(v_dd: f32, speed_mode: SpeedMode) -> Self {
        let (rise_time_ns, c_bus_max) = match speed_mode {
            SpeedMode::Standard => (1000.0, 400.0),
            SpeedMode::Fast => (300.0, 400.0),
            SpeedMode::FastPlus => (120.0, 550.0),
        };
        
        Self {
            v_dd,
            i_ol_max: 0.003, // 3mA typical
            c_bus: c_bus_max * 0.7, // Assume 70% of max
            rise_time_ns,
        }
    }
    
    /// Calculate minimum pull-up resistance
    pub fn min_resistance(&self) -> f32 {
        self.v_dd / self.i_ol_max
    }
    
    /// Calculate maximum pull-up resistance
    pub fn max_resistance(&self) -> f32 {
        let c_bus_f = self.c_bus * 1e-12; // Convert pF to F
        let rise_time_s = self.rise_time_ns * 1e-9; // Convert ns to s
        rise_time_s / (0.8473 * c_bus_f)
    }
    
    /// Get recommended resistance value
    pub fn recommended_resistance(&self) -> f32 {
        let min = self.min_resistance();
        let max = self.max_resistance();
        // Return geometric mean, rounded to common value
        let ideal = (min * max).sqrt();
        
        // Round to common resistor values
        Self::round_to_standard_value(ideal)
    }
    
    fn round_to_standard_value(value: f32) -> f32 {
        const STANDARD_VALUES: &[f32] = &[
            1000.0, 1500.0, 2200.0, 3300.0, 4700.0, 
            6800.0, 10000.0, 15000.0, 22000.0, 33000.0, 47000.0
        ];
        
        STANDARD_VALUES.iter()
            .min_by(|a, b| {
                let diff_a = (value - **a).abs();
                let diff_b = (value - **b).abs();
                diff_a.partial_cmp(&diff_b).unwrap()
            })
            .copied()
            .unwrap_or(4700.0)
    }
}

#[derive(Debug, Clone, Copy)]
pub enum SpeedMode {
    Standard,   // 100 kHz
    Fast,       // 400 kHz
    FastPlus,   // 1 MHz
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_pullup_calculator() {
        let calc = PullUpCalculator::new(3.3, SpeedMode::Fast);
        
        let min = calc.min_resistance();
        let max = calc.max_resistance();
        let recommended = calc.recommended_resistance();
        
        assert!(min < recommended);
        assert!(recommended < max);
        assert!(recommended >= 2200.0 && recommended <= 10000.0);
    }
}

```

## Advanced Topics

### Multi-Master Considerations

When multiple masters share the I²C bus:

1. **Arbitration**: Masters monitor SDA while transmitting. If a master tries to send a '1' but reads '0', it has lost arbitration and must immediately stop transmitting
2. **Clock synchronization**: All masters synchronize to the slowest SCL (the one held LOW longest)
3. **Bus conflicts**: The open-drain nature prevents hardware damage during conflicts

### Level Shifting

When connecting devices with different voltage levels (e.g., 3.3V and 5V):

**Options**:
1. **Dedicated I²C level shifters** (e.g., PCA9306, TXS0102)
2. **MOSFET-based bidirectional shifters**
3. **Diode + separate pull-ups** (for 5V→3.3V only, with caveats)

Never directly connect 5V devices to 3.3V pins without protection, as this can damage the 3.3V device.

### Common Issues and Troubleshooting

**Symptoms and causes**:

1. **Communication fails intermittently**
   - Pull-up resistors too weak (value too high)
   - Bus capacitance too high for speed mode
   - EMI/noise on long cables

2. **No ACK from slave**
   - Wrong device address
   - Device not powered
   - Device in sleep/reset mode
   - SDA stuck LOW

3. **Clock stretching issues**
   - Master doesn't support clock stretching
   - Slave hangs (requires bus reset)

4. **Waveform distortion**
   - Pull-ups too strong (value too low)
   - Excessive bus capacitance
   - Reflections on long traces

**Debug techniques**:
- Use a logic analyzer to visualize SCL and SDA
- Check voltage levels meet VIH/VIL thresholds
- Measure rise/fall times with oscilloscope
- Verify pull-up resistor values with multimeter
- Check for proper ground connections

### Best Practices

1. **PCB Layout**:
   - Keep I²C traces short and parallel
   - Route away from high-speed signals
   - Use ground plane for shielding
   - Place pull-ups close to the bus

2. **Cable Runs**:
   - Use twisted pair or shielded cable for runs >30cm
   - Minimize stubs on multi-drop configurations
   - Consider bus buffers (e.g., PCA9615) for long distances

3. **Software**:
   - Always implement timeout handling
   - Support clock stretching unless you know slaves don't use it
   - Use repeated START for atomic write-read sequences
   - Implement proper error recovery (bus reset)

4. **Testing**:
   - Test at maximum expected bus capacitance
   - Verify operation across temperature range
   - Test with all devices connected
   - Measure actual rise/fall times

## Summary

The I²C clock and data lines use a clever open-drain configuration that enables simple, robust multi-device communication with just two wires. The key electrical requirements are:

- Pull-up resistors sized appropriately for bus capacitance and speed
- Proper voltage levels and timing characteristics
- Rise and fall times within specification
- Support for clock stretching when needed

By understanding these fundamentals and following best practices for resistor selection, PCB layout, and software implementation, you can build reliable I²C systems that work consistently across varying conditions and device combinations.