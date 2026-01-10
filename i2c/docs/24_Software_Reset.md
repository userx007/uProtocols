# Software Reset in I2C Communication

## Overview

Software reset is a mechanism that allows a master device to reset I2C slave devices through protocol-level commands rather than toggling a hardware reset pin. This is particularly useful in systems where:

- Hardware reset lines are not available or not connected
- Multiple slaves need coordinated resets
- Recovery from error states is needed without physical intervention
- Power cycling is impractical or undesirable

## Types of Software Reset

### 1. General Call Reset
The I2C specification defines a General Call address (0x00) that can be used to address all devices on the bus simultaneously. Some devices respond to specific General Call commands for reset operations.

### 2. Device-Specific Reset Commands
Many I2C devices implement their own reset commands through specific register writes. These are manufacturer and device-specific.

### 3. Bus Recovery Reset
A special sequence to recover from bus lockup conditions where a slave is holding SDA low.

## Implementation Details

### General Call Address
- **Address**: 0x00 (7-bit addressing)
- **Format**: [START] [0x00 + W] [Command Byte] [STOP]
- **Common Commands**:
  - 0x06: Reset and write programmable part of slave address
  - 0x04: Write programmable part of slave address

### Software Reset Command
Many devices use a specific register write sequence to trigger a software reset. Common patterns include:
- Writing a magic value (e.g., 0xB6, 0x5A) to a reset register
- Writing a specific bit in a control register
- Performing a sequence of register writes

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// Generic I2C hardware abstraction (platform-specific)
typedef struct {
    void *hw_handle;
} i2c_bus_t;

// I2C HAL functions (platform-specific implementations)
extern bool i2c_start(i2c_bus_t *bus);
extern bool i2c_stop(i2c_bus_t *bus);
extern bool i2c_write_byte(i2c_bus_t *bus, uint8_t data);
extern bool i2c_read_byte(i2c_bus_t *bus, uint8_t *data, bool ack);
extern void i2c_delay_us(uint32_t microseconds);

// General Call Reset
#define I2C_GENERAL_CALL_ADDR   0x00
#define I2C_RESET_COMMAND       0x06

/**
 * Perform a General Call reset on the I2C bus
 * This addresses all devices on the bus
 */
bool i2c_general_call_reset(i2c_bus_t *bus) {
    if (!i2c_start(bus)) {
        return false;
    }
    
    // Send General Call address with write bit
    if (!i2c_write_byte(bus, (I2C_GENERAL_CALL_ADDR << 1) | 0)) {
        i2c_stop(bus);
        return false;
    }
    
    // Send reset command
    if (!i2c_write_byte(bus, I2C_RESET_COMMAND)) {
        i2c_stop(bus);
        return false;
    }
    
    i2c_stop(bus);
    
    // Wait for devices to complete reset
    i2c_delay_us(10000); // 10ms typical
    
    return true;
}

/**
 * Device-specific software reset
 * Example: BME280 sensor reset
 */
#define BME280_ADDR         0x76
#define BME280_RESET_REG    0xE0
#define BME280_RESET_VALUE  0xB6

bool i2c_device_soft_reset(i2c_bus_t *bus, uint8_t device_addr, 
                           uint8_t reset_reg, uint8_t reset_value) {
    if (!i2c_start(bus)) {
        return false;
    }
    
    // Send device address with write bit
    if (!i2c_write_byte(bus, (device_addr << 1) | 0)) {
        i2c_stop(bus);
        return false;
    }
    
    // Send reset register address
    if (!i2c_write_byte(bus, reset_reg)) {
        i2c_stop(bus);
        return false;
    }
    
    // Send reset value
    if (!i2c_write_byte(bus, reset_value)) {
        i2c_stop(bus);
        return false;
    }
    
    i2c_stop(bus);
    
    // Wait for device to complete reset
    i2c_delay_us(5000); // 5ms typical
    
    return true;
}

/**
 * Example: Reset BME280 sensor
 */
bool bme280_soft_reset(i2c_bus_t *bus) {
    return i2c_device_soft_reset(bus, BME280_ADDR, 
                                 BME280_RESET_REG, 
                                 BME280_RESET_VALUE);
}

/**
 * Bus recovery - manually clock out stuck data
 * This is used when a slave holds SDA low
 */
bool i2c_bus_recovery(i2c_bus_t *bus) {
    // This requires direct GPIO access to I2C pins
    // Platform-specific implementation
    
    // 1. Configure SDA and SCL as GPIO outputs
    // 2. Generate 9 clock pulses on SCL
    // 3. Check if SDA is released
    // 4. Send STOP condition
    // 5. Reconfigure pins for I2C
    
    // Pseudo-code:
    /*
    gpio_set_output(SCL_PIN);
    gpio_set_input(SDA_PIN);
    
    for (int i = 0; i < 9; i++) {
        gpio_write(SCL_PIN, 0);
        delay_us(5);
        gpio_write(SCL_PIN, 1);
        delay_us(5);
        
        if (gpio_read(SDA_PIN) == 1) {
            break; // SDA released
        }
    }
    
    // Generate STOP condition
    gpio_set_output(SDA_PIN);
    gpio_write(SDA_PIN, 0);
    delay_us(5);
    gpio_write(SCL_PIN, 1);
    delay_us(5);
    gpio_write(SDA_PIN, 1);
    
    // Reconfigure for I2C
    i2c_init_pins(bus);
    */
    
    return true;
}

/**
 * Complete reset sequence with retry
 */
typedef enum {
    RESET_SUCCESS,
    RESET_TIMEOUT,
    RESET_BUS_ERROR,
    RESET_DEVICE_NOT_RESPONDING
} reset_status_t;

reset_status_t i2c_reset_with_verify(i2c_bus_t *bus, uint8_t device_addr) {
    const int MAX_RETRIES = 3;
    
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        // Attempt software reset
        if (!i2c_device_soft_reset(bus, device_addr, 0xE0, 0xB6)) {
            // If soft reset fails, try bus recovery
            i2c_bus_recovery(bus);
            continue;
        }
        
        // Verify device responds after reset
        i2c_delay_us(10000); // Wait for reset to complete
        
        if (i2c_start(bus)) {
            if (i2c_write_byte(bus, (device_addr << 1) | 0)) {
                i2c_stop(bus);
                return RESET_SUCCESS;
            }
            i2c_stop(bus);
        }
    }
    
    return RESET_DEVICE_NOT_RESPONDING;
}
```

### C++ Implementation with RAII

```cpp
#include <cstdint>
#include <stdexcept>
#include <chrono>
#include <thread>

class I2CBus {
private:
    void* hw_handle;
    
    bool start();
    bool stop();
    bool writeByte(uint8_t data);
    bool readByte(uint8_t& data, bool ack);
    
public:
    I2CBus(void* handle) : hw_handle(handle) {}
    
    // General Call Reset
    void generalCallReset() {
        if (!start()) {
            throw std::runtime_error("I2C start condition failed");
        }
        
        if (!writeByte(0x00 << 1)) {
            stop();
            throw std::runtime_error("General call address write failed");
        }
        
        if (!writeByte(0x06)) {
            stop();
            throw std::runtime_error("Reset command write failed");
        }
        
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Device-specific reset
    void deviceReset(uint8_t addr, uint8_t resetReg, uint8_t resetValue) {
        if (!start()) {
            throw std::runtime_error("I2C start condition failed");
        }
        
        if (!writeByte((addr << 1) | 0)) {
            stop();
            throw std::runtime_error("Device address write failed");
        }
        
        if (!writeByte(resetReg)) {
            stop();
            throw std::runtime_error("Reset register write failed");
        }
        
        if (!writeByte(resetValue)) {
            stop();
            throw std::runtime_error("Reset value write failed");
        }
        
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Verify device presence
    bool devicePresent(uint8_t addr) {
        if (!start()) {
            return false;
        }
        
        bool present = writeByte((addr << 1) | 0);
        stop();
        return present;
    }
};

// Device-specific class
class BME280 {
private:
    I2CBus& bus;
    static constexpr uint8_t DEVICE_ADDR = 0x76;
    static constexpr uint8_t RESET_REG = 0xE0;
    static constexpr uint8_t RESET_VALUE = 0xB6;
    
public:
    BME280(I2CBus& i2cBus) : bus(i2cBus) {}
    
    void softReset() {
        bus.deviceReset(DEVICE_ADDR, RESET_REG, RESET_VALUE);
        
        // Wait and verify
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (!bus.devicePresent(DEVICE_ADDR)) {
            throw std::runtime_error("Device not responding after reset");
        }
    }
};
```

### Rust Implementation

```rust
use std::thread;
use std::time::Duration;

// Error types
#[derive(Debug)]
pub enum I2CError {
    BusError,
    NackReceived,
    Timeout,
    DeviceNotResponding,
}

// I2C hardware abstraction trait
pub trait I2CHardware {
    fn start(&mut self) -> Result<(), I2CError>;
    fn stop(&mut self) -> Result<(), I2CError>;
    fn write_byte(&mut self, data: u8) -> Result<(), I2CError>;
    fn read_byte(&mut self, ack: bool) -> Result<u8, I2CError>;
}

// I2C Bus wrapper
pub struct I2CBus<T: I2CHardware> {
    hardware: T,
}

impl<T: I2CHardware> I2CBus<T> {
    pub fn new(hardware: T) -> Self {
        Self { hardware }
    }
    
    /// Perform a General Call reset on all devices
    pub fn general_call_reset(&mut self) -> Result<(), I2CError> {
        const GENERAL_CALL_ADDR: u8 = 0x00;
        const RESET_COMMAND: u8 = 0x06;
        
        self.hardware.start()?;
        
        // Send General Call address with write bit
        if let Err(e) = self.hardware.write_byte((GENERAL_CALL_ADDR << 1) | 0) {
            self.hardware.stop().ok();
            return Err(e);
        }
        
        // Send reset command
        if let Err(e) = self.hardware.write_byte(RESET_COMMAND) {
            self.hardware.stop().ok();
            return Err(e);
        }
        
        self.hardware.stop()?;
        
        // Wait for devices to reset
        thread::sleep(Duration::from_millis(10));
        
        Ok(())
    }
    
    /// Device-specific software reset
    pub fn device_reset(
        &mut self,
        device_addr: u8,
        reset_reg: u8,
        reset_value: u8,
    ) -> Result<(), I2CError> {
        self.hardware.start()?;
        
        // Send device address
        if let Err(e) = self.hardware.write_byte((device_addr << 1) | 0) {
            self.hardware.stop().ok();
            return Err(e);
        }
        
        // Send reset register
        if let Err(e) = self.hardware.write_byte(reset_reg) {
            self.hardware.stop().ok();
            return Err(e);
        }
        
        // Send reset value
        if let Err(e) = self.hardware.write_byte(reset_value) {
            self.hardware.stop().ok();
            return Err(e);
        }
        
        self.hardware.stop()?;
        
        // Wait for device to reset
        thread::sleep(Duration::from_millis(5));
        
        Ok(())
    }
    
    /// Check if device is present on the bus
    pub fn device_present(&mut self, device_addr: u8) -> bool {
        if self.hardware.start().is_err() {
            return false;
        }
        
        let present = self.hardware.write_byte((device_addr << 1) | 0).is_ok();
        self.hardware.stop().ok();
        
        present
    }
    
    /// Reset with retry and verification
    pub fn reset_with_verify(
        &mut self,
        device_addr: u8,
        reset_reg: u8,
        reset_value: u8,
        max_retries: u32,
    ) -> Result<(), I2CError> {
        for retry in 0..max_retries {
            // Attempt reset
            if self.device_reset(device_addr, reset_reg, reset_value).is_ok() {
                // Verify device responds
                thread::sleep(Duration::from_millis(10));
                
                if self.device_present(device_addr) {
                    return Ok(());
                }
            }
            
            // Small delay before retry
            if retry < max_retries - 1 {
                thread::sleep(Duration::from_millis(50));
            }
        }
        
        Err(I2CError::DeviceNotResponding)
    }
}

// Device-specific implementation example
pub struct BME280<T: I2CHardware> {
    bus: I2CBus<T>,
    address: u8,
}

impl<T: I2CHardware> BME280<T> {
    const DEFAULT_ADDR: u8 = 0x76;
    const RESET_REG: u8 = 0xE0;
    const RESET_VALUE: u8 = 0xB6;
    
    pub fn new(hardware: T) -> Self {
        Self {
            bus: I2CBus::new(hardware),
            address: Self::DEFAULT_ADDR,
        }
    }
    
    pub fn with_address(hardware: T, address: u8) -> Self {
        Self {
            bus: I2CBus::new(hardware),
            address,
        }
    }
    
    /// Perform software reset
    pub fn soft_reset(&mut self) -> Result<(), I2CError> {
        self.bus.reset_with_verify(
            self.address,
            Self::RESET_REG,
            Self::RESET_VALUE,
            3, // Max retries
        )
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    // Mock hardware for testing
    struct MockI2C;
    
    impl I2CHardware for MockI2C {
        fn start(&mut self) -> Result<(), I2CError> {
            Ok(())
        }
        
        fn stop(&mut self) -> Result<(), I2CError> {
            Ok(())
        }
        
        fn write_byte(&mut self, _data: u8) -> Result<(), I2CError> {
            Ok(())
        }
        
        fn read_byte(&mut self, _ack: bool) -> Result<u8, I2CError> {
            Ok(0)
        }
    }
    
    #[test]
    fn test_general_call_reset() {
        let mut bus = I2CBus::new(MockI2C);
        assert!(bus.general_call_reset().is_ok());
    }
    
    #[test]
    fn test_bme280_reset() {
        let mut sensor = BME280::new(MockI2C);
        assert!(sensor.soft_reset().is_ok());
    }
}
```

## Important Considerations

### 1. **Timing Requirements**
- Most devices require a delay after reset (typically 2-10ms)
- Check device datasheet for specific timing requirements
- Some devices have internal startup sequences that take longer

### 2. **State After Reset**
- Registers return to default values
- Previous configurations are lost
- Need to reinitialize device after reset

### 3. **Bus State**
- Ensure no other transaction is in progress
- Wait for bus to be idle before reset
- General Call affects ALL devices on the bus

### 4. **Error Handling**
- Verify device responds after reset
- Implement retry mechanisms
- Have fallback to hardware reset if available

### 5. **Device Compatibility**
- Not all devices support General Call reset
- Some devices use proprietary reset commands
- Always consult device datasheet

## Common Reset Sequences by Device Type

| Device Family | Reset Register | Reset Value | Post-Reset Delay |
|---------------|----------------|-------------|------------------|
| BME280/BMP280 | 0xE0 | 0xB6 | 2-10ms |
| MPU6050/9250 | 0x6B | 0x80 (bit 7) | 100ms |
| ADS1115 | General Call | 0x06 | 1ms |
| MCP23017 | 0x0A | 0xFF (IOCON) | Immediate |

This software reset capability is essential for robust I2C systems, enabling recovery from error conditions and initialization without requiring physical reset lines.