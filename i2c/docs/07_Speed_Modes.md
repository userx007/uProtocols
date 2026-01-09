# I2C Speed Modes: A Comprehensive Guide

## Overview

I2C (Inter-Integrated Circuit) supports multiple speed modes to accommodate different application requirements, ranging from simple sensor communication to high-bandwidth data transfers. The choice of speed mode affects bus timing, power consumption, and electrical characteristics.

## The Four Speed Modes

### 1. **Standard Mode (Sm)** - 100 kHz
The original I2C specification, suitable for most basic applications with relatively low data throughput requirements.

**Characteristics:**
- Clock frequency: 0-100 kHz
- Maximum bus capacitance: 400 pF
- Rise time: ≤1000 ns
- Fall time: ≤300 ns
- Lowest power consumption
- Best noise immunity due to slower edges

### 2. **Fast Mode (Fm)** - 400 kHz
The most commonly used mode in modern embedded systems, offering a good balance between speed and compatibility.

**Characteristics:**
- Clock frequency: 0-400 kHz
- Maximum bus capacitance: 400 pF
- Rise time: ≤300 ns
- Fall time: ≤300 ns
- Widely supported across devices

### 3. **Fast Mode Plus (Fm+)** - 1 MHz
An extension of Fast Mode for applications requiring higher throughput.

**Characteristics:**
- Clock frequency: 0-1 MHz
- Maximum bus capacitance: 550 pF
- Rise time: ≤120 ns
- Fall time: ≤120 ns
- Requires careful PCB layout and impedance matching
- May need external pull-up resistors optimization

### 4. **High-Speed Mode (Hs)** - 3.4 MHz
The fastest I2C mode, used in specialized high-bandwidth applications.

**Characteristics:**
- Clock frequency: 0-3.4 MHz
- Requires special master code for mode switching
- Uses current-source pull-ups instead of resistive pull-ups
- Backward compatible (can switch to Fm for communication)
- Very sensitive to bus capacitance and trace layout

## Timing Parameters Comparison

| Parameter | Standard | Fast | Fast Plus | High-Speed |
|-----------|----------|------|-----------|------------|
| SCL Clock | 100 kHz | 400 kHz | 1 MHz | 3.4 MHz |
| Rise Time (max) | 1000 ns | 300 ns | 120 ns | 80 ns |
| Fall Time (max) | 300 ns | 300 ns | 120 ns | 80 ns |
| Data Setup Time | 250 ns | 100 ns | 50 ns | 10 ns |
| Data Hold Time | 0 ns | 0 ns | 0 ns | 0 ns |
| Bus Capacitance | 400 pF | 400 pF | 550 pF | 400 pF |

## C/C++ Code Examples

### Example 1: Linux I2C Speed Configuration

```c
#include <stdio.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

// I2C speed mode definitions
#define I2C_SPEED_STANDARD    100000  // 100 kHz
#define I2C_SPEED_FAST        400000  // 400 kHz
#define I2C_SPEED_FAST_PLUS  1000000  // 1 MHz
#define I2C_SPEED_HIGH_SPEED 3400000  // 3.4 MHz

typedef enum {
    SPEED_STANDARD,
    SPEED_FAST,
    SPEED_FAST_PLUS,
    SPEED_HIGH_SPEED
} i2c_speed_mode_t;

typedef struct {
    int fd;
    uint8_t address;
    i2c_speed_mode_t speed_mode;
} i2c_bus_t;

/**
 * Initialize I2C bus with specific speed mode
 * Note: Actual speed setting is platform-dependent
 */
int i2c_init(i2c_bus_t *bus, const char *device, uint8_t slave_addr, 
             i2c_speed_mode_t speed) {
    bus->fd = open(device, O_RDWR);
    if (bus->fd < 0) {
        perror("Failed to open I2C device");
        return -1;
    }
    
    // Set slave address
    if (ioctl(bus->fd, I2C_SLAVE, slave_addr) < 0) {
        perror("Failed to set I2C slave address");
        close(bus->fd);
        return -1;
    }
    
    bus->address = slave_addr;
    bus->speed_mode = speed;
    
    printf("I2C initialized at address 0x%02X with speed mode: ", slave_addr);
    switch(speed) {
        case SPEED_STANDARD:
            printf("Standard (100 kHz)\n");
            break;
        case SPEED_FAST:
            printf("Fast (400 kHz)\n");
            break;
        case SPEED_FAST_PLUS:
            printf("Fast Plus (1 MHz)\n");
            break;
        case SPEED_HIGH_SPEED:
            printf("High-Speed (3.4 MHz)\n");
            break;
    }
    
    return 0;
}

/**
 * Write data to I2C device with timing considerations
 */
int i2c_write_with_timing(i2c_bus_t *bus, uint8_t reg, 
                          const uint8_t *data, size_t len) {
    uint8_t buffer[256];
    
    if (len > 255) {
        fprintf(stderr, "Data too large\n");
        return -1;
    }
    
    buffer[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buffer[i + 1] = data[i];
    }
    
    ssize_t written = write(bus->fd, buffer, len + 1);
    if (written != (ssize_t)(len + 1)) {
        perror("I2C write failed");
        return -1;
    }
    
    // Add inter-transaction delay for slower modes
    if (bus->speed_mode == SPEED_STANDARD) {
        usleep(100);  // 100 µs delay for standard mode
    }
    
    return 0;
}

/**
 * Read data from I2C device
 */
int i2c_read_with_timing(i2c_bus_t *bus, uint8_t reg, 
                         uint8_t *data, size_t len) {
    // Write register address
    if (write(bus->fd, &reg, 1) != 1) {
        perror("Failed to write register address");
        return -1;
    }
    
    // Read data
    ssize_t bytes_read = read(bus->fd, data, len);
    if (bytes_read != (ssize_t)len) {
        perror("I2C read failed");
        return -1;
    }
    
    return 0;
}

void i2c_close(i2c_bus_t *bus) {
    if (bus->fd >= 0) {
        close(bus->fd);
        bus->fd = -1;
    }
}
```

### Example 2: STM32 HAL I2C Speed Configuration

```c
#include "stm32f4xx_hal.h"

// I2C timing register values for different speeds (STM32F4 @ 42 MHz APB1)
#define I2C_TIMING_STANDARD   0x20404768  // 100 kHz
#define I2C_TIMING_FAST       0x00901850  // 400 kHz
#define I2C_TIMING_FAST_PLUS  0x00700818  // 1 MHz

I2C_HandleTypeDef hi2c1;

/**
 * Configure I2C for Standard Mode (100 kHz)
 */
HAL_StatusTypeDef i2c_config_standard_mode(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    return HAL_I2C_Init(&hi2c1);
}

/**
 * Configure I2C for Fast Mode (400 kHz)
 */
HAL_StatusTypeDef i2c_config_fast_mode(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    return HAL_I2C_Init(&hi2c1);
}

/**
 * Configure I2C for Fast Mode Plus (1 MHz)
 * Note: Requires special pin configuration and shorter traces
 */
HAL_StatusTypeDef i2c_config_fast_mode_plus(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 1000000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    // Enable Fast Mode Plus on I/O pins (STM32-specific)
    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_I2C1);
    
    return HAL_I2C_Init(&hi2c1);
}

/**
 * Example usage: Read sensor data at different speeds
 */
void read_sensor_at_speed(uint8_t sensor_addr, uint8_t reg_addr, 
                          uint8_t *data, uint16_t size) {
    HAL_StatusTypeDef status;
    
    // Reconfigure for desired speed
    i2c_config_fast_mode();  // 400 kHz
    
    // Transmit register address
    status = HAL_I2C_Master_Transmit(&hi2c1, sensor_addr << 1, 
                                     &reg_addr, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) {
        // Handle error
        return;
    }
    
    // Receive data
    status = HAL_I2C_Master_Receive(&hi2c1, sensor_addr << 1, 
                                    data, size, HAL_MAX_DELAY);
    if (status != HAL_OK) {
        // Handle error
        return;
    }
}
```

### Example 3: Arduino-style Speed Configuration

```cpp
#include <Wire.h>

class I2CSpeedManager {
public:
    enum SpeedMode {
        STANDARD = 100000,      // 100 kHz
        FAST = 400000,          // 400 kHz
        FAST_PLUS = 1000000,    // 1 MHz
        HIGH_SPEED = 3400000    // 3.4 MHz (rare)
    };
    
private:
    TwoWire* wire;
    SpeedMode currentSpeed;
    
public:
    I2CSpeedManager(TwoWire* w = &Wire) : wire(w), currentSpeed(STANDARD) {}
    
    /**
     * Initialize I2C with specified speed
     */
    void begin(SpeedMode speed = FAST) {
        wire->begin();
        setSpeed(speed);
    }
    
    /**
     * Change I2C clock speed
     */
    void setSpeed(SpeedMode speed) {
        wire->setClock(speed);
        currentSpeed = speed;
        
        Serial.print("I2C speed set to: ");
        Serial.print(speed / 1000);
        Serial.println(" kHz");
    }
    
    /**
     * Write data with speed consideration
     */
    bool writeRegister(uint8_t deviceAddr, uint8_t regAddr, 
                       const uint8_t* data, size_t len) {
        wire->beginTransmission(deviceAddr);
        wire->write(regAddr);
        
        for (size_t i = 0; i < len; i++) {
            wire->write(data[i]);
        }
        
        uint8_t error = wire->endTransmission();
        
        // Add delay for standard mode if needed
        if (currentSpeed == STANDARD && len > 4) {
            delayMicroseconds(100);
        }
        
        return (error == 0);
    }
    
    /**
     * Read data from register
     */
    bool readRegister(uint8_t deviceAddr, uint8_t regAddr, 
                      uint8_t* data, size_t len) {
        // Write register address
        wire->beginTransmission(deviceAddr);
        wire->write(regAddr);
        uint8_t error = wire->endTransmission(false);  // Repeated start
        
        if (error != 0) return false;
        
        // Read data
        size_t received = wire->requestFrom(deviceAddr, len);
        if (received != len) return false;
        
        for (size_t i = 0; i < len; i++) {
            data[i] = wire->read();
        }
        
        return true;
    }
    
    /**
     * Get current speed mode
     */
    SpeedMode getSpeed() const {
        return currentSpeed;
    }
    
    /**
     * Benchmark transfer rate at current speed
     */
    void benchmark(uint8_t deviceAddr, uint8_t regAddr, size_t numBytes) {
        uint8_t buffer[32];
        unsigned long startTime = micros();
        
        const int iterations = 100;
        for (int i = 0; i < iterations; i++) {
            readRegister(deviceAddr, regAddr, buffer, 
                        (numBytes > 32) ? 32 : numBytes);
        }
        
        unsigned long duration = micros() - startTime;
        float bytesPerSecond = (numBytes * iterations * 1000000.0f) / duration;
        
        Serial.print("Transfer rate: ");
        Serial.print(bytesPerSecond / 1024.0f, 2);
        Serial.println(" KB/s");
    }
};

// Example usage
I2CSpeedManager i2cMgr;

void setup() {
    Serial.begin(115200);
    
    // Test different speeds
    i2cMgr.begin(I2CSpeedManager::STANDARD);
    delay(1000);
    
    i2cMgr.setSpeed(I2CSpeedManager::FAST);
    delay(1000);
    
    // Uncomment if hardware supports Fast Mode Plus
    // i2cMgr.setSpeed(I2CSpeedManager::FAST_PLUS);
}

void loop() {
    uint8_t data[16];
    uint8_t deviceAddr = 0x68;  // Example device address
    uint8_t regAddr = 0x00;
    
    if (i2cMgr.readRegister(deviceAddr, regAddr, data, sizeof(data))) {
        // Process data
    }
    
    delay(100);
}
```

## Rust Code Examples

### Example 1: Embedded HAL I2C Speed Configuration

```rust
#![no_std]

use embedded_hal::blocking::i2c::{Write, WriteRead};

/// I2C speed modes
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CSpeed {
    Standard,   // 100 kHz
    Fast,       // 400 kHz
    FastPlus,   // 1 MHz
    HighSpeed,  // 3.4 MHz
}

impl I2CSpeed {
    /// Get frequency in Hz
    pub const fn frequency_hz(&self) -> u32 {
        match self {
            I2CSpeed::Standard => 100_000,
            I2CSpeed::Fast => 400_000,
            I2CSpeed::FastPlus => 1_000_000,
            I2CSpeed::HighSpeed => 3_400_000,
        }
    }
    
    /// Get human-readable name
    pub const fn name(&self) -> &'static str {
        match self {
            I2CSpeed::Standard => "Standard (100 kHz)",
            I2CSpeed::Fast => "Fast (400 kHz)",
            I2CSpeed::FastPlus => "Fast Plus (1 MHz)",
            I2CSpeed::HighSpeed => "High-Speed (3.4 MHz)",
        }
    }
    
    /// Check if speed requires special hardware considerations
    pub const fn requires_special_hardware(&self) -> bool {
        matches!(self, I2CSpeed::FastPlus | I2CSpeed::HighSpeed)
    }
}

/// I2C bus configuration
pub struct I2CConfig {
    pub speed: I2CSpeed,
    pub timeout_ms: u32,
    pub enable_clock_stretching: bool,
}

impl Default for I2CConfig {
    fn default() -> Self {
        Self {
            speed: I2CSpeed::Fast,
            timeout_ms: 1000,
            enable_clock_stretching: true,
        }
    }
}

/// Generic I2C device interface
pub struct I2CDevice<I2C> {
    i2c: I2C,
    address: u8,
    config: I2CConfig,
}

impl<I2C, E> I2CDevice<I2C>
where
    I2C: Write<Error = E> + WriteRead<Error = E>,
{
    /// Create new I2C device interface
    pub fn new(i2c: I2C, address: u8, config: I2CConfig) -> Self {
        Self {
            i2c,
            address,
            config,
        }
    }
    
    /// Write to register
    pub fn write_register(&mut self, register: u8, data: &[u8]) -> Result<(), E> {
        let mut buffer = [0u8; 33]; // Max 32 bytes + register address
        
        if data.len() > 32 {
            // Handle error - data too large
            // In no_std, we can't use standard Error trait easily
            // This would need custom error handling
            return self.i2c.write(self.address, &[]);
        }
        
        buffer[0] = register;
        buffer[1..data.len() + 1].copy_from_slice(data);
        
        self.i2c.write(self.address, &buffer[..data.len() + 1])
    }
    
    /// Read from register
    pub fn read_register(&mut self, register: u8, buffer: &mut [u8]) -> Result<(), E> {
        self.i2c.write_read(self.address, &[register], buffer)
    }
    
    /// Get current speed configuration
    pub fn speed(&self) -> I2CSpeed {
        self.config.speed
    }
}

// Example for STM32 with stm32f4xx-hal
#[cfg(feature = "stm32f4")]
mod stm32_example {
    use super::*;
    use stm32f4xx_hal::{
        i2c::{I2c, Mode},
        pac::I2C1,
        prelude::*,
    };
    
    /// Configure I2C1 with specific speed
    pub fn configure_i2c_speed(
        i2c: I2C1,
        scl: impl Into<stm32f4xx_hal::gpio::AF4>,
        sda: impl Into<stm32f4xx_hal::gpio::AF4>,
        clocks: &stm32f4xx_hal::rcc::Clocks,
        speed: I2CSpeed,
    ) -> I2c<I2C1> {
        let mode = match speed {
            I2CSpeed::Standard => Mode::Standard {
                frequency: 100_000.Hz(),
            },
            I2CSpeed::Fast => Mode::Fast {
                frequency: 400_000.Hz(),
                duty_cycle: stm32f4xx_hal::i2c::DutyCycle::Ratio2to1,
            },
            I2CSpeed::FastPlus => Mode::Fast {
                frequency: 1_000_000.Hz(),
                duty_cycle: stm32f4xx_hal::i2c::DutyCycle::Ratio16to9,
            },
            I2CSpeed::HighSpeed => {
                // High-speed mode not typically supported in standard HAL
                Mode::Fast {
                    frequency: 400_000.Hz(),
                    duty_cycle: stm32f4xx_hal::i2c::DutyCycle::Ratio2to1,
                }
            }
        };
        
        I2c::new(i2c, (scl, sda), mode, clocks)
    }
}
```

### Example 2: Linux I2C with Speed Configuration

```rust
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::unix::io::AsRawFd;

// I2C ioctl commands
const I2C_SLAVE: u64 = 0x0703;
const I2C_RDWR: u64 = 0x0707;

#[derive(Debug, Clone, Copy)]
pub enum I2CSpeed {
    Standard = 100_000,
    Fast = 400_000,
    FastPlus = 1_000_000,
    HighSpeed = 3_400_000,
}

impl I2CSpeed {
    pub fn as_hz(&self) -> u32 {
        *self as u32
    }
    
    pub fn name(&self) -> &'static str {
        match self {
            I2CSpeed::Standard => "Standard (100 kHz)",
            I2CSpeed::Fast => "Fast (400 kHz)",
            I2CSpeed::FastPlus => "Fast Plus (1 MHz)",
            I2CSpeed::HighSpeed => "High-Speed (3.4 MHz)",
        }
    }
}

pub struct I2CBus {
    file: File,
    speed: I2CSpeed,
    current_address: Option<u8>,
}

impl I2CBus {
    /// Open I2C bus device
    pub fn new(device_path: &str, speed: I2CSpeed) -> io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(device_path)?;
        
        println!("Opened I2C bus: {} at {}", device_path, speed.name());
        
        Ok(Self {
            file,
            speed,
            current_address: None,
        })
    }
    
    /// Set slave address for communication
    pub fn set_slave_address(&mut self, address: u8) -> io::Result<()> {
        if self.current_address == Some(address) {
            return Ok(());
        }
        
        unsafe {
            if libc::ioctl(self.file.as_raw_fd(), I2C_SLAVE, address as libc::c_ulong) < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        self.current_address = Some(address);
        Ok(())
    }
    
    /// Write data to device
    pub fn write(&mut self, address: u8, data: &[u8]) -> io::Result<usize> {
        self.set_slave_address(address)?;
        self.file.write(data)
    }
    
    /// Read data from device
    pub fn read(&mut self, address: u8, buffer: &mut [u8]) -> io::Result<usize> {
        self.set_slave_address(address)?;
        self.file.read(buffer)
    }
    
    /// Write to register and read response
    pub fn write_read(&mut self, address: u8, write_data: &[u8], 
                      read_buffer: &mut [u8]) -> io::Result<()> {
        self.set_slave_address(address)?;
        
        // Write register address
        self.file.write_all(write_data)?;
        
        // Add small delay for slower speeds
        if matches!(self.speed, I2CSpeed::Standard) {
            std::thread::sleep(std::time::Duration::from_micros(100));
        }
        
        // Read data
        self.file.read_exact(read_buffer)?;
        Ok(())
    }
    
    /// Get current speed setting
    pub fn speed(&self) -> I2CSpeed {
        self.speed
    }
}

/// High-level I2C device interface
pub struct I2CDevice {
    bus: I2CBus,
    address: u8,
}

impl I2CDevice {
    pub fn new(device_path: &str, address: u8, speed: I2CSpeed) -> io::Result<Self> {
        let bus = I2CBus::new(device_path, speed)?;
        Ok(Self { bus, address })
    }
    
    /// Write to register
    pub fn write_register(&mut self, register: u8, data: &[u8]) -> io::Result<()> {
        let mut buffer = vec![0u8; data.len() + 1];
        buffer[0] = register;
        buffer[1..].copy_from_slice(data);
        
        self.bus.write(self.address, &buffer)?;
        Ok(())
    }
    
    /// Read from register
    pub fn read_register(&mut self, register: u8, buffer: &mut [u8]) -> io::Result<()> {
        self.bus.write_read(self.address, &[register], buffer)
    }
    
    /// Read single byte from register
    pub fn read_byte(&mut self, register: u8) -> io::Result<u8> {
        let mut buffer = [0u8; 1];
        self.read_register(register, &mut buffer)?;
        Ok(buffer[0])
    }
    
    /// Write single byte to register
    pub fn write_byte(&mut self, register: u8, value: u8) -> io::Result<()> {
        self.write_register(register, &[value])
    }
}

// Example usage
fn main() -> io::Result<()> {
    // Create I2C device at Fast Mode (400 kHz)
    let mut device = I2CDevice::new("/dev/i2c-1", 0x68, I2CSpeed::Fast)?;
    
    // Write configuration
    device.write_byte(0x6B, 0x00)?;  // Wake up device
    
    // Read sensor data
    let mut data = [0u8; 6];
    device.read_register(0x3B, &mut data)?;
    
    // Parse accelerometer data
    let accel_x = i16::from_be_bytes([data[0], data[1]]);
    let accel_y = i16::from_be_bytes([data[2], data[3]]);
    let accel_z = i16::from_be_bytes([data[4], data[5]]);
    
    println!("Acceleration: X={}, Y={}, Z={}", accel_x, accel_y, accel_z);
    
    Ok(())
}
```

### Example 3: Speed Benchmarking and Comparison

```rust
use std::time::{Duration, Instant};
use std::io;

pub struct I2CSpeedBenchmark {
    device: I2CDevice,
}

impl I2CSpeedBenchmark {
    pub fn new(device_path: &str, address: u8) -> io::Result<Self> {
        let device = I2CDevice::new(device_path, address, I2CSpeed::Fast)?;
        Ok(Self { device })
    }
    
    /// Benchmark read performance at current speed
    pub fn benchmark_read(&mut self, register: u8, 
                          bytes: usize, iterations: usize) -> io::Result<BenchmarkResult> {
        let mut buffer = vec![0u8; bytes];
        let start = Instant::now();
        
        for _ in 0..iterations {
            self.device.read_register(register, &mut buffer)?;
        }
        
        let duration = start.elapsed();
        let total_bytes = bytes * iterations;
        let throughput = (total_bytes as f64 / duration.as_secs_f64()) / 1024.0; // KB/s
        
        Ok(BenchmarkResult {
            speed: self.device.bus.speed(),
            duration,
            total_bytes,
            throughput_kbps: throughput,
        })
    }
    
    /// Benchmark write performance at current speed
    pub fn benchmark_write(&mut self, register: u8, 
                           bytes: usize, iterations: usize) -> io::Result<BenchmarkResult> {
        let data = vec![0xAA; bytes];
        let start = Instant::now();
        
        for _ in 0..iterations {
            self.device.write_register(register, &data)?;
        }
        
        let duration = start.elapsed();
        let total_bytes = bytes * iterations;
        let throughput = (total_bytes as f64 / duration.as_secs_f64()) / 1024.0;
        
        Ok(BenchmarkResult {
            speed: self.device.bus.speed(),
            duration,
            total_bytes,
            throughput_kbps: throughput,
        })
    }
    
    /// Compare all supported speeds
    pub fn compare_speeds(&mut self, register: u8, 
                          bytes: usize, iterations: usize) -> io::Result<Vec<BenchmarkResult>> {
        let speeds = vec![
            I2CSpeed::Standard,
            I2CSpeed::Fast,
            I2CSpeed::FastPlus,
        ];
        
        let mut results = Vec::new();
        
        for speed in speeds {
            // Recreate device with new speed
            self.device = I2CDevice::new(
                "/dev/i2c-1", 
                self.device.address, 
                speed
            )?;
            
            let result = self.benchmark_read(register, bytes, iterations)?;
            results.push(result);
            
            // Cooldown between tests
            std::thread::sleep(Duration::from_millis(100));
        }
        
        Ok(results)
    }
}

#[derive(Debug)]
pub struct BenchmarkResult {
    pub speed: I2CSpeed,
    pub duration: Duration,
    pub total_bytes: usize,
    pub throughput_kbps: f64,
}

impl BenchmarkResult {
    pub fn print(&self) {
        println!("\n=== {} ===", self.speed.name());
        println!("Duration: {:?}", self.duration);
        println!("Total bytes: {}", self.total_bytes);
        println!("Throughput: {:.2} KB/s", self.throughput_kbps);
        println!("Efficiency: {:.1}%", 
                 (self.throughput_kbps / (self.speed.as_hz() as f64 / 8192.0)) * 100.0);
    }
}
```

## Hardware Considerations

### Pull-up Resistor Selection

Different speeds require different pull-up resistor values:

- **Standard Mode (100 kHz)**: 4.7kΩ - 10kΩ
- **Fast Mode (400 kHz)**: 2.2kΩ - 4.7kΩ
- **Fast Mode Plus (1 MHz)**: 1kΩ - 2.2kΩ
- **High-Speed Mode (3.4 MHz)**: Active current sources, not resistive pull-ups

### PCB Layout Guidelines

**For Fast Mode Plus and High-Speed:**
- Keep traces as short as possible (< 10 cm for Fm+)
- Use controlled impedance traces (around 100Ω)
- Minimize bus capacitance
- Place termination close to endpoints
- Use ground planes for shielding
- Avoid sharp corners (use 45° or curved traces)

## Choosing the Right Speed

- **Standard Mode**: Battery-powered devices, long cable runs, noisy environments
- **Fast Mode**: Most modern embedded systems, balanced performance
- **Fast Mode Plus**: High-throughput sensors, displays, memory devices
- **High-Speed Mode**: Specialized applications requiring maximum bandwidth (cameras, video)

The code examples demonstrate practical implementation across different platforms while highlighting the timing and electrical considerations unique to each speed mode.