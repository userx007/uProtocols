# I2C Bus Reset Strategies

## Overview

I2C bus reset strategies are critical techniques for recovering from bus lockup conditions where the bus becomes stuck in an abnormal state. This typically occurs when a slave device holds the SDA line low, preventing communication. Understanding and implementing proper reset strategies ensures robust I2C communication in embedded systems.

## Common Bus Lockup Scenarios

1. **SDA Held Low by Slave**: A slave device crashes mid-transaction while pulling SDA low
2. **Master Reset During Transaction**: The master resets while a slave is expecting more clock pulses
3. **Clock Stretching Timeout**: A slave holds SCL low indefinitely
4. **Electrical Noise**: Glitches cause state machine corruption

## Hardware Reset Methods

### 1. Power Cycle Reset

The most reliable but least elegant method - cycling power to all I2C devices.

**C Example:**
```c
#include <stdint.h>
#include <stdbool.h>

// GPIO control for power switching
#define I2C_POWER_PIN GPIO_PIN_5

typedef struct {
    volatile uint32_t* gpio_base;
    uint8_t power_pin;
} i2c_power_control_t;

bool i2c_power_cycle_reset(i2c_power_control_t* ctrl, uint32_t delay_ms) {
    // Turn off I2C bus power
    *(ctrl->gpio_base) &= ~(1 << ctrl->power_pin);
    
    // Wait for capacitors to discharge
    delay_milliseconds(delay_ms);
    
    // Restore power
    *(ctrl->gpio_base) |= (1 << ctrl->power_pin);
    
    // Wait for devices to initialize
    delay_milliseconds(50);
    
    return true;
}
```

**Rust Example:**
```rust
use embedded_hal::digital::v2::OutputPin;
use embedded_hal::blocking::delay::DelayMs;

pub struct I2cPowerControl<P> {
    power_pin: P,
}

impl<P: OutputPin> I2cPowerControl<P> {
    pub fn new(power_pin: P) -> Self {
        Self { power_pin }
    }
    
    pub fn power_cycle_reset<D: DelayMs<u32>>(
        &mut self, 
        delay: &mut D,
        power_off_ms: u32
    ) -> Result<(), P::Error> {
        // Turn off power
        self.power_pin.set_low()?;
        delay.delay_ms(power_off_ms);
        
        // Restore power
        self.power_pin.set_high()?;
        delay.delay_ms(50);
        
        Ok(())
    }
}
```

### 2. Hardware Reset Pin

Many I2C devices have a dedicated reset pin.

**C Example:**
```c
#include <stdbool.h>

typedef struct {
    volatile uint32_t* reset_gpio;
    uint8_t reset_pin;
    bool active_low;
} i2c_device_reset_t;

void i2c_hardware_reset(i2c_device_reset_t* dev) {
    uint32_t assert_state = dev->active_low ? 0 : (1 << dev->reset_pin);
    uint32_t deassert_state = dev->active_low ? (1 << dev->reset_pin) : 0;
    
    // Assert reset
    if (dev->active_low) {
        *(dev->reset_gpio) &= ~(1 << dev->reset_pin);
    } else {
        *(dev->reset_gpio) |= (1 << dev->reset_pin);
    }
    
    delay_microseconds(10);  // Hold reset for minimum time
    
    // De-assert reset
    if (dev->active_low) {
        *(dev->reset_gpio) |= (1 << dev->reset_pin);
    } else {
        *(dev->reset_gpio) &= ~(1 << dev->reset_pin);
    }
    
    delay_milliseconds(10);  // Wait for device initialization
}
```

**Rust Example:**
```rust
use embedded_hal::digital::v2::OutputPin;
use embedded_hal::blocking::delay::{DelayMs, DelayUs};

pub struct I2cDeviceReset<P> {
    reset_pin: P,
    active_low: bool,
}

impl<P: OutputPin> I2cDeviceReset<P> {
    pub fn new(reset_pin: P, active_low: bool) -> Self {
        Self { reset_pin, active_low }
    }
    
    pub fn hardware_reset<D>(&mut self, delay: &mut D) -> Result<(), P::Error>
    where
        D: DelayUs<u32> + DelayMs<u32>,
    {
        // Assert reset
        if self.active_low {
            self.reset_pin.set_low()?;
        } else {
            self.reset_pin.set_high()?;
        }
        
        delay.delay_us(10);
        
        // De-assert reset
        if self.active_low {
            self.reset_pin.set_high()?;
        } else {
            self.reset_pin.set_low()?;
        }
        
        delay.delay_ms(10);
        
        Ok(())
    }
}
```

## Software Reset Methods

### 3. Clock Pulse Recovery (9-Clock Method)

The most common software recovery technique - sending 9 clock pulses to allow the slave to complete its byte transmission and release SDA.

**C Example:**
```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    volatile uint32_t* scl_gpio;
    volatile uint32_t* sda_gpio;
    uint8_t scl_pin;
    uint8_t sda_pin;
} i2c_gpio_t;

// Configure pin as output
static void gpio_set_output(volatile uint32_t* gpio, uint8_t pin) {
    // Implementation depends on MCU
    *(gpio + 1) |= (1 << pin);  // Example: set direction register
}

// Configure pin as input
static void gpio_set_input(volatile uint32_t* gpio, uint8_t pin) {
    *(gpio + 1) &= ~(1 << pin);
}

// Read pin state
static bool gpio_read(volatile uint32_t* gpio, uint8_t pin) {
    return (*(gpio) & (1 << pin)) != 0;
}

// Write pin state
static void gpio_write(volatile uint32_t* gpio, uint8_t pin, bool state) {
    if (state) {
        *(gpio) |= (1 << pin);
    } else {
        *(gpio) &= ~(1 << pin);
    }
}

bool i2c_bus_recovery_9clock(i2c_gpio_t* bus) {
    // Switch to GPIO mode
    gpio_set_output(bus->scl_gpio, bus->scl_pin);
    gpio_set_input(bus->sda_gpio, bus->sda_pin);
    
    // Ensure SCL is high
    gpio_write(bus->scl_gpio, bus->scl_pin, true);
    delay_microseconds(5);
    
    // Check if SDA is already high (bus is idle)
    if (gpio_read(bus->sda_gpio, bus->sda_pin)) {
        return true;  // Bus is already free
    }
    
    // Send up to 9 clock pulses
    for (int i = 0; i < 9; i++) {
        // SCL low
        gpio_write(bus->scl_gpio, bus->scl_pin, false);
        delay_microseconds(5);
        
        // SCL high
        gpio_write(bus->scl_gpio, bus->scl_pin, true);
        delay_microseconds(5);
        
        // Check if SDA went high
        if (gpio_read(bus->sda_gpio, bus->sda_pin)) {
            break;  // Slave released SDA
        }
    }
    
    // Generate STOP condition
    gpio_set_output(bus->sda_gpio, bus->sda_pin);
    gpio_write(bus->sda_gpio, bus->sda_pin, false);
    delay_microseconds(5);
    gpio_write(bus->scl_gpio, bus->scl_pin, true);
    delay_microseconds(5);
    gpio_write(bus->sda_gpio, bus->sda_pin, true);
    delay_microseconds(5);
    
    // Check if recovery was successful
    bool success = gpio_read(bus->sda_gpio, bus->sda_pin);
    
    return success;
}
```

**Rust Example:**
```rust
use embedded_hal::digital::v2::{InputPin, OutputPin};
use embedded_hal::blocking::delay::DelayUs;

pub struct I2cBusRecovery<SCL, SDA> {
    scl: SCL,
    sda: SDA,
}

impl<SCL, SDA> I2cBusRecovery<SCL, SDA>
where
    SCL: OutputPin,
    SDA: InputPin + OutputPin,
{
    pub fn new(scl: SCL, sda: SDA) -> Self {
        Self { scl, sda }
    }
    
    pub fn recover_9clock<D: DelayUs<u32>>(
        &mut self,
        delay: &mut D
    ) -> Result<bool, ()> {
        // Ensure SCL is high
        self.scl.set_high().map_err(|_| ())?;
        delay.delay_us(5);
        
        // Check if SDA is already high
        if self.sda.is_high().map_err(|_| ())? {
            return Ok(true);  // Bus is already idle
        }
        
        // Send up to 9 clock pulses
        for _ in 0..9 {
            self.scl.set_low().map_err(|_| ())?;
            delay.delay_us(5);
            
            self.scl.set_high().map_err(|_| ())?;
            delay.delay_us(5);
            
            // Check if SDA went high
            if self.sda.is_high().map_err(|_| ())? {
                break;
            }
        }
        
        // Generate STOP condition
        self.sda.set_low().map_err(|_| ())?;
        delay.delay_us(5);
        self.scl.set_high().map_err(|_| ())?;
        delay.delay_us(5);
        self.sda.set_high().map_err(|_| ())?;
        delay.delay_us(5);
        
        // Verify recovery
        Ok(self.sda.is_high().map_err(|_| ())?)
    }
}
```

### 4. General Call Reset

Using the I2C General Call address (0x00) to reset devices that support it.

**C Example:**
```c
#include <stdint.h>
#include <stdbool.h>

#define I2C_GENERAL_CALL_ADDR   0x00
#define I2C_RESET_COMMAND       0x06

typedef struct {
    void (*start)(void);
    bool (*write_byte)(uint8_t byte);
    void (*stop)(void);
} i2c_ops_t;

bool i2c_general_call_reset(i2c_ops_t* i2c) {
    // Send START condition
    i2c->start();
    
    // Send General Call address with write bit
    if (!i2c->write_byte(I2C_GENERAL_CALL_ADDR << 1)) {
        i2c->stop();
        return false;  // No device responded
    }
    
    // Send reset command (SWRST)
    if (!i2c->write_byte(I2C_RESET_COMMAND)) {
        i2c->stop();
        return false;
    }
    
    // Send STOP condition
    i2c->stop();
    
    delay_milliseconds(10);  // Allow devices to reset
    
    return true;
}
```

**Rust Example:**
```rust
use embedded_hal::blocking::i2c::Write;

const I2C_GENERAL_CALL_ADDR: u8 = 0x00;
const I2C_RESET_COMMAND: u8 = 0x06;

pub fn general_call_reset<I2C, E>(i2c: &mut I2C) -> Result<(), E>
where
    I2C: Write<Error = E>,
{
    // Send General Call address with reset command
    i2c.write(I2C_GENERAL_CALL_ADDR, &[I2C_RESET_COMMAND])?;
    Ok(())
}
```

### 5. SMBus Host Notify Protocol

For devices supporting SMBus, use the host notify protocol.

**C Example:**
```c
#include <stdint.h>
#include <stdbool.h>

#define SMBUS_HOST_NOTIFY_ADDR  0x08

typedef struct {
    void (*start)(void);
    bool (*write_byte)(uint8_t byte);
    uint8_t (*read_byte)(bool ack);
    void (*stop)(void);
} smbus_ops_t;

bool smbus_reset_via_host_notify(smbus_ops_t* bus) {
    bus->start();
    
    // Send Host Notify address
    if (!bus->write_byte(SMBUS_HOST_NOTIFY_ADDR << 1)) {
        bus->stop();
        return false;
    }
    
    bus->stop();
    return true;
}
```

## Comprehensive Recovery Strategy

**C++ Example:**
```cpp
#include <cstdint>
#include <functional>

class I2cBusRecovery {
public:
    enum class RecoveryResult {
        SUCCESS,
        FAILED_SOFTWARE,
        FAILED_HARDWARE,
        FAILED_ALL
    };
    
    struct Config {
        std::function<void()> hw_reset;
        std::function<bool()> clock_recovery;
        std::function<bool()> general_call_reset;
        std::function<bool()> verify_bus_idle;
        uint32_t max_attempts;
    };
    
private:
    Config config_;
    
public:
    I2cBusRecovery(const Config& config) : config_(config) {}
    
    RecoveryResult recover() {
        // Step 1: Try software recovery (9-clock method)
        for (uint32_t i = 0; i < config_.max_attempts; i++) {
            if (config_.clock_recovery && config_.clock_recovery()) {
                if (config_.verify_bus_idle()) {
                    return RecoveryResult::SUCCESS;
                }
            }
        }
        
        // Step 2: Try General Call reset
        if (config_.general_call_reset && config_.general_call_reset()) {
            if (config_.verify_bus_idle()) {
                return RecoveryResult::SUCCESS;
            }
        }
        
        // Step 3: Hardware reset as last resort
        if (config_.hw_reset) {
            config_.hw_reset();
            if (config_.verify_bus_idle()) {
                return RecoveryResult::SUCCESS;
            }
            return RecoveryResult::FAILED_HARDWARE;
        }
        
        return RecoveryResult::FAILED_ALL;
    }
    
    bool verify_and_recover() {
        if (config_.verify_bus_idle()) {
            return true;  // Bus is healthy
        }
        
        RecoveryResult result = recover();
        return result == RecoveryResult::SUCCESS;
    }
};

// Usage example
void example_usage() {
    I2cBusRecovery::Config config;
    config.hw_reset = []() { /* hardware reset implementation */ };
    config.clock_recovery = []() { /* 9-clock recovery */ return true; };
    config.general_call_reset = []() { /* general call */ return true; };
    config.verify_bus_idle = []() { /* verify SDA and SCL high */ return true; };
    config.max_attempts = 3;
    
    I2cBusRecovery recovery(config);
    
    if (!recovery.verify_and_recover()) {
        // Handle recovery failure
    }
}
```

**Rust Example:**
```rust
use embedded_hal::digital::v2::InputPin;

pub enum RecoveryResult {
    Success,
    FailedSoftware,
    FailedHardware,
    FailedAll,
}

pub struct I2cBusRecoveryManager<SCL, SDA> {
    scl: SCL,
    sda: SDA,
    max_attempts: u32,
}

impl<SCL, SDA> I2cBusRecoveryManager<SCL, SDA>
where
    SCL: InputPin,
    SDA: InputPin,
{
    pub fn new(scl: SCL, sda: SDA, max_attempts: u32) -> Self {
        Self {
            scl,
            sda,
            max_attempts,
        }
    }
    
    pub fn verify_bus_idle(&self) -> Result<bool, ()> {
        // Both lines should be high when idle
        let scl_high = self.scl.is_high().map_err(|_| ())?;
        let sda_high = self.sda.is_high().map_err(|_| ())?;
        Ok(scl_high && sda_high)
    }
    
    pub fn recover<F1, F2, F3>(
        &mut self,
        clock_recovery: F1,
        general_call: F2,
        hw_reset: F3,
    ) -> Result<RecoveryResult, ()>
    where
        F1: Fn() -> Result<bool, ()>,
        F2: Fn() -> Result<bool, ()>,
        F3: Fn() -> Result<(), ()>,
    {
        // Step 1: Software recovery attempts
        for _ in 0..self.max_attempts {
            if clock_recovery()? {
                if self.verify_bus_idle()? {
                    return Ok(RecoveryResult::Success);
                }
            }
        }
        
        // Step 2: General call reset
        if general_call()? {
            if self.verify_bus_idle()? {
                return Ok(RecoveryResult::Success);
            }
        }
        
        // Step 3: Hardware reset
        hw_reset()?;
        if self.verify_bus_idle()? {
            return Ok(RecoveryResult::Success);
        }
        
        Ok(RecoveryResult::FailedAll)
    }
}
```

## Best Practices

1. **Always verify bus state** before and after recovery attempts
2. **Implement timeouts** for all recovery operations
3. **Log recovery events** for debugging and reliability analysis
4. **Use progressive recovery** - start with least disruptive methods
5. **Design hardware** with reset capabilities (dedicated pins, power switching)
6. **Test recovery** under various fault conditions during development
7. **Handle recovery failures gracefully** with appropriate error reporting
8. **Consider bus capacitance** when timing clock pulses

## Platform-Specific Considerations

- **Linux**: Many I2C adapters support `I2C_RECOVER_BUS` ioctl
- **STM32**: Hardware I2C peripheral has automatic recovery features
- **ESP32**: Software bit-banging typically required for recovery
- **Nordic nRF**: TWIM peripheral supports automatic error recovery

These strategies ensure robust I2C communication in production embedded systems where bus lockup conditions can occur due to power glitches, EMI, or device failures.