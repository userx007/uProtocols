# PMBus Protocol: Comprehensive Guide

## Overview

PMBus (Power Management Bus) is a open standard communications protocol that defines a way of communicating with power conversion and other devices. It's based on SMBus (System Management Bus), which itself is a subset of I2C. PMBus extends SMBus with a command language specifically designed for power management applications.

**Key Characteristics:**
- Built on top of SMBus/I2C (typically 100 kHz)
- Standardized command set for power supplies
- Supports monitoring, control, and configuration
- Includes telemetry (voltage, current, temperature, power)
- Allows fault reporting and warnings
- Supports paging for multi-output devices

## PMBus vs I2C/SMBus

| Feature | I2C | SMBus | PMBus |
|---------|-----|-------|-------|
| Base Protocol | I2C | I2C | SMBus |
| Timeout | No | Yes (25-35ms) | Yes |
| Clock Speed | Up to 3.4 MHz | 10-100 kHz | 100-400 kHz |
| Application | General | System management | Power management |
| Command Set | None | Basic | Extensive (200+ commands) |

## PMBus Command Structure

PMBus commands are 8-bit values that follow the SMBus protocol formats:

- **Send Byte**: `[Address+W] [Command]`
- **Read Byte**: `[Address+W] [Command] [Address+R] [Data]`
- **Write Byte**: `[Address+W] [Command] [Data]`
- **Read Word**: `[Address+W] [Command] [Address+R] [DataLow] [DataHigh]`
- **Write Word**: `[Address+W] [Command] [DataLow] [DataHigh]`
- **Block Read/Write**: For larger data transfers

## Common PMBus Commands

```
0x00 - PAGE                 // Select page for multi-output devices
0x01 - OPERATION            // Turn on/off, margin control
0x02 - ON_OFF_CONFIG        // Configure power-up behavior
0x03 - CLEAR_FAULTS         // Clear fault conditions
0x10 - WRITE_PROTECT        // Write protection control

// Voltage commands
0x20 - VOUT_MODE            // Output voltage data format
0x21 - VOUT_COMMAND         // Set output voltage
0x24 - VOUT_MAX             // Maximum output voltage
0x25 - VOUT_MARGIN_HIGH     // Margin high voltage
0x26 - VOUT_MARGIN_LOW      // Margin low voltage

// Current/Power commands
0x46 - IOUT_CAL_GAIN        // Current calibration
0x47 - IOUT_CAL_OFFSET      // Current offset

// Reading telemetry
0x78 - STATUS_BYTE          // Status summary
0x79 - STATUS_WORD          // Detailed status
0x7A - STATUS_VOUT          // Output voltage status
0x7B - STATUS_IOUT          // Output current status
0x7C - STATUS_INPUT         // Input status
0x7D - STATUS_TEMPERATURE   // Temperature status

// Telemetry readings
0x88 - READ_VIN             // Read input voltage
0x89 - READ_IIN             // Read input current
0x8B - READ_VOUT            // Read output voltage
0x8C - READ_IOUT            // Read output current
0x8D - READ_TEMPERATURE_1   // Read temperature
0x96 - READ_POUT            // Read output power
0x97 - READ_PIN             // Read input power
```

## Data Formats

PMBus uses several data formats:

### Linear Format (LINEAR11)
Used for most telemetry readings. 16-bit value with 5-bit exponent and 11-bit mantissa:

```
Format: seeeeemmmmmmmmmm
s = sign (1 bit)
e = exponent (5 bits, two's complement)
m = mantissa (11 bits, two's complement)

Value = mantissa × 2^exponent
```

### Linear Format (LINEAR16)
For VOUT with fixed exponent in VOUT_MODE register.

## C/C++ Implementation

### Basic PMBus Library (C)

```c
// pmbus.h
#ifndef PMBUS_H
#define PMBUS_H

#include <stdint.h>
#include <stdbool.h>

// PMBus Commands
#define PMBUS_PAGE                  0x00
#define PMBUS_OPERATION             0x01
#define PMBUS_ON_OFF_CONFIG         0x02
#define PMBUS_CLEAR_FAULTS          0x03
#define PMBUS_WRITE_PROTECT         0x10

#define PMBUS_VOUT_MODE             0x20
#define PMBUS_VOUT_COMMAND          0x21
#define PMBUS_VOUT_MAX              0x24
#define PMBUS_VOUT_MARGIN_HIGH      0x25
#define PMBUS_VOUT_MARGIN_LOW       0x26

#define PMBUS_STATUS_BYTE           0x78
#define PMBUS_STATUS_WORD           0x79
#define PMBUS_STATUS_VOUT           0x7A
#define PMBUS_STATUS_IOUT           0x7B
#define PMBUS_STATUS_TEMPERATURE    0x7D

#define PMBUS_READ_VIN              0x88
#define PMBUS_READ_IIN              0x89
#define PMBUS_READ_VOUT             0x8B
#define PMBUS_READ_IOUT             0x8C
#define PMBUS_READ_TEMPERATURE_1    0x8D
#define PMBUS_READ_POUT             0x96
#define PMBUS_READ_PIN              0x97

// OPERATION register bits
#define PMBUS_OP_ON                 0x80
#define PMBUS_OP_MARGIN_HIGH        0x98
#define PMBUS_OP_MARGIN_LOW         0xA8

// VOUT_MODE format bits
#define PMBUS_VOUT_MODE_LINEAR      0x00
#define PMBUS_VOUT_MODE_VID         0x20
#define PMBUS_VOUT_MODE_DIRECT      0x40

// Structure for PMBus device
typedef struct {
    int i2c_fd;           // I2C file descriptor
    uint8_t address;      // 7-bit I2C address
    uint8_t current_page; // Current page selected
} pmbus_device_t;

// Function prototypes
int pmbus_init(pmbus_device_t *dev, const char *i2c_bus, uint8_t address);
void pmbus_close(pmbus_device_t *dev);

int pmbus_write_byte(pmbus_device_t *dev, uint8_t command, uint8_t data);
int pmbus_read_byte(pmbus_device_t *dev, uint8_t command, uint8_t *data);
int pmbus_write_word(pmbus_device_t *dev, uint8_t command, uint16_t data);
int pmbus_read_word(pmbus_device_t *dev, uint8_t command, uint16_t *data);
int pmbus_send_byte(pmbus_device_t *dev, uint8_t command);

// High-level functions
int pmbus_set_page(pmbus_device_t *dev, uint8_t page);
int pmbus_clear_faults(pmbus_device_t *dev);
int pmbus_set_output(pmbus_device_t *dev, bool enable);

// Telemetry functions
float pmbus_read_vin(pmbus_device_t *dev);
float pmbus_read_vout(pmbus_device_t *dev);
float pmbus_read_iout(pmbus_device_t *dev);
float pmbus_read_temperature(pmbus_device_t *dev);
float pmbus_read_pout(pmbus_device_t *dev);

// Data format conversion
float pmbus_linear11_to_float(uint16_t data);
uint16_t pmbus_float_to_linear11(float value);
float pmbus_linear16_to_float(uint16_t data, int8_t exponent);
uint16_t pmbus_float_to_linear16(float value, int8_t exponent);

#endif // PMBUS_H
```

```c
// pmbus.c
#include "pmbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <string.h>

int pmbus_init(pmbus_device_t *dev, const char *i2c_bus, uint8_t address) {
    dev->address = address;
    dev->current_page = 0;
    
    dev->i2c_fd = open(i2c_bus, O_RDWR);
    if (dev->i2c_fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    
    if (ioctl(dev->i2c_fd, I2C_SLAVE, address) < 0) {
        perror("Failed to set I2C slave address");
        close(dev->i2c_fd);
        return -1;
    }
    
    return 0;
}

void pmbus_close(pmbus_device_t *dev) {
    if (dev->i2c_fd >= 0) {
        close(dev->i2c_fd);
        dev->i2c_fd = -1;
    }
}

int pmbus_write_byte(pmbus_device_t *dev, uint8_t command, uint8_t data) {
    uint8_t buffer[2] = {command, data};
    
    if (write(dev->i2c_fd, buffer, 2) != 2) {
        perror("PMBus write byte failed");
        return -1;
    }
    
    return 0;
}

int pmbus_read_byte(pmbus_device_t *dev, uint8_t command, uint8_t *data) {
    if (write(dev->i2c_fd, &command, 1) != 1) {
        perror("PMBus read byte command failed");
        return -1;
    }
    
    if (read(dev->i2c_fd, data, 1) != 1) {
        perror("PMBus read byte data failed");
        return -1;
    }
    
    return 0;
}

int pmbus_write_word(pmbus_device_t *dev, uint8_t command, uint16_t data) {
    uint8_t buffer[3];
    buffer[0] = command;
    buffer[1] = data & 0xFF;        // Low byte
    buffer[2] = (data >> 8) & 0xFF; // High byte
    
    if (write(dev->i2c_fd, buffer, 3) != 3) {
        perror("PMBus write word failed");
        return -1;
    }
    
    return 0;
}

int pmbus_read_word(pmbus_device_t *dev, uint8_t command, uint16_t *data) {
    uint8_t buffer[2];
    
    if (write(dev->i2c_fd, &command, 1) != 1) {
        perror("PMBus read word command failed");
        return -1;
    }
    
    if (read(dev->i2c_fd, buffer, 2) != 2) {
        perror("PMBus read word data failed");
        return -1;
    }
    
    *data = buffer[0] | (buffer[1] << 8);
    return 0;
}

int pmbus_send_byte(pmbus_device_t *dev, uint8_t command) {
    if (write(dev->i2c_fd, &command, 1) != 1) {
        perror("PMBus send byte failed");
        return -1;
    }
    
    return 0;
}

int pmbus_set_page(pmbus_device_t *dev, uint8_t page) {
    if (pmbus_write_byte(dev, PMBUS_PAGE, page) < 0) {
        return -1;
    }
    dev->current_page = page;
    return 0;
}

int pmbus_clear_faults(pmbus_device_t *dev) {
    return pmbus_send_byte(dev, PMBUS_CLEAR_FAULTS);
}

int pmbus_set_output(pmbus_device_t *dev, bool enable) {
    uint8_t operation = enable ? PMBUS_OP_ON : 0x00;
    return pmbus_write_byte(dev, PMBUS_OPERATION, operation);
}

// Convert LINEAR11 format to float
float pmbus_linear11_to_float(uint16_t data) {
    // Extract exponent (5 bits, two's complement)
    int8_t exponent = (data >> 11) & 0x1F;
    if (exponent > 15) {
        exponent -= 32; // Sign extend
    }
    
    // Extract mantissa (11 bits, two's complement)
    int16_t mantissa = data & 0x7FF;
    if (mantissa > 1023) {
        mantissa -= 2048; // Sign extend
    }
    
    // Calculate value
    return mantissa * powf(2.0f, exponent);
}

// Convert float to LINEAR11 format
uint16_t pmbus_float_to_linear11(float value) {
    // Find optimal exponent
    int8_t exponent = 0;
    float abs_value = fabsf(value);
    
    if (abs_value != 0) {
        exponent = (int8_t)floor(log2(abs_value / 1023.0f));
        if (exponent < -15) exponent = -15;
        if (exponent > 15) exponent = 15;
    }
    
    // Calculate mantissa
    int16_t mantissa = (int16_t)round(value / powf(2.0f, exponent));
    
    // Clamp mantissa
    if (mantissa > 1023) mantissa = 1023;
    if (mantissa < -1024) mantissa = -1024;
    
    // Combine exponent and mantissa
    uint16_t result = ((exponent & 0x1F) << 11) | (mantissa & 0x7FF);
    return result;
}

// Convert LINEAR16 format to float
float pmbus_linear16_to_float(uint16_t data, int8_t exponent) {
    int16_t mantissa = (int16_t)data;
    return mantissa * powf(2.0f, exponent);
}

// Convert float to LINEAR16 format
uint16_t pmbus_float_to_linear16(float value, int8_t exponent) {
    int16_t mantissa = (int16_t)round(value / powf(2.0f, exponent));
    return (uint16_t)mantissa;
}

// Telemetry reading functions
float pmbus_read_vin(pmbus_device_t *dev) {
    uint16_t data;
    if (pmbus_read_word(dev, PMBUS_READ_VIN, &data) < 0) {
        return -1.0f;
    }
    return pmbus_linear11_to_float(data);
}

float pmbus_read_vout(pmbus_device_t *dev) {
    uint16_t data;
    uint8_t vout_mode;
    
    // Read VOUT_MODE to get exponent
    if (pmbus_read_byte(dev, PMBUS_VOUT_MODE, &vout_mode) < 0) {
        return -1.0f;
    }
    
    if (pmbus_read_word(dev, PMBUS_READ_VOUT, &data) < 0) {
        return -1.0f;
    }
    
    uint8_t mode = (vout_mode >> 5) & 0x07;
    
    if (mode == 0) { // LINEAR mode
        int8_t exponent = vout_mode & 0x1F;
        if (exponent > 15) {
            exponent -= 32; // Sign extend
        }
        return pmbus_linear16_to_float(data, exponent);
    } else {
        // For VID or DIRECT modes, would need additional handling
        return pmbus_linear11_to_float(data);
    }
}

float pmbus_read_iout(pmbus_device_t *dev) {
    uint16_t data;
    if (pmbus_read_word(dev, PMBUS_READ_IOUT, &data) < 0) {
        return -1.0f;
    }
    return pmbus_linear11_to_float(data);
}

float pmbus_read_temperature(pmbus_device_t *dev) {
    uint16_t data;
    if (pmbus_read_word(dev, PMBUS_READ_TEMPERATURE_1, &data) < 0) {
        return -1.0f;
    }
    return pmbus_linear11_to_float(data);
}

float pmbus_read_pout(pmbus_device_t *dev) {
    uint16_t data;
    if (pmbus_read_word(dev, PMBUS_READ_POUT, &data) < 0) {
        return -1.0f;
    }
    return pmbus_linear11_to_float(data);
}
```

### Example Usage (C)

```c
// main.c
#include <stdio.h>
#include <unistd.h>
#include "pmbus.h"

int main() {
    pmbus_device_t psu;
    
    // Initialize PMBus device on /dev/i2c-1, address 0x58
    if (pmbus_init(&psu, "/dev/i2c-1", 0x58) < 0) {
        fprintf(stderr, "Failed to initialize PMBus device\n");
        return 1;
    }
    
    printf("PMBus Power Supply Monitor\n");
    printf("===========================\n\n");
    
    // Clear any existing faults
    pmbus_clear_faults(&psu);
    
    // Turn on the output
    if (pmbus_set_output(&psu, true) < 0) {
        fprintf(stderr, "Failed to turn on output\n");
    } else {
        printf("Output enabled\n");
    }
    
    // Monitor telemetry for 10 seconds
    for (int i = 0; i < 10; i++) {
        float vin = pmbus_read_vin(&psu);
        float vout = pmbus_read_vout(&psu);
        float iout = pmbus_read_iout(&psu);
        float temp = pmbus_read_temperature(&psu);
        float pout = pmbus_read_pout(&psu);
        
        printf("\nTelemetry Reading %d:\n", i + 1);
        printf("  Input Voltage:    %7.2f V\n", vin);
        printf("  Output Voltage:   %7.2f V\n", vout);
        printf("  Output Current:   %7.2f A\n", iout);
        printf("  Temperature:      %7.2f °C\n", temp);
        printf("  Output Power:     %7.2f W\n", pout);
        
        // Check status
        uint16_t status_word;
        if (pmbus_read_word(&psu, PMBUS_STATUS_WORD, &status_word) == 0) {
            if (status_word & 0x8000) printf("  WARNING: Fault detected!\n");
            if (status_word & 0x4000) printf("  WARNING: Communication error!\n");
            if (status_word & 0x0800) printf("  WARNING: Over temperature!\n");
            if (status_word & 0x0080) printf("  WARNING: Output overvoltage!\n");
        }
        
        sleep(1);
    }
    
    // Turn off output
    pmbus_set_output(&psu, false);
    printf("\nOutput disabled\n");
    
    pmbus_close(&psu);
    return 0;
}
```

### C++ Wrapper

```cpp
// PMBus.hpp
#ifndef PMBUS_HPP
#define PMBUS_HPP

#include <string>
#include <memory>
#include <vector>
#include <stdexcept>

extern "C" {
    #include "pmbus.h"
}

class PMBusException : public std::runtime_error {
public:
    explicit PMBusException(const std::string& msg) : std::runtime_error(msg) {}
};

struct Telemetry {
    float input_voltage;
    float output_voltage;
    float output_current;
    float temperature;
    float output_power;
    uint16_t status_word;
};

class PMBusDevice {
private:
    pmbus_device_t device_;
    bool initialized_;
    
public:
    PMBusDevice(const std::string& bus, uint8_t address) : initialized_(false) {
        if (pmbus_init(&device_, bus.c_str(), address) < 0) {
            throw PMBusException("Failed to initialize PMBus device");
        }
        initialized_ = true;
    }
    
    ~PMBusDevice() {
        if (initialized_) {
            pmbus_close(&device_);
        }
    }
    
    // Prevent copying
    PMBusDevice(const PMBusDevice&) = delete;
    PMBusDevice& operator=(const PMBusDevice&) = delete;
    
    void setPage(uint8_t page) {
        if (pmbus_set_page(&device_, page) < 0) {
            throw PMBusException("Failed to set page");
        }
    }
    
    void clearFaults() {
        if (pmbus_clear_faults(&device_) < 0) {
            throw PMBusException("Failed to clear faults");
        }
    }
    
    void setOutput(bool enable) {
        if (pmbus_set_output(&device_, enable) < 0) {
            throw PMBusException("Failed to set output state");
        }
    }
    
    void setVoltage(float voltage, int8_t exponent = -9) {
        uint16_t data = pmbus_float_to_linear16(voltage, exponent);
        if (pmbus_write_word(&device_, PMBUS_VOUT_COMMAND, data) < 0) {
            throw PMBusException("Failed to set voltage");
        }
    }
    
    Telemetry readTelemetry() {
        Telemetry telem;
        
        telem.input_voltage = pmbus_read_vin(&device_);
        telem.output_voltage = pmbus_read_vout(&device_);
        telem.output_current = pmbus_read_iout(&device_);
        telem.temperature = pmbus_read_temperature(&device_);
        telem.output_power = pmbus_read_pout(&device_);
        
        if (pmbus_read_word(&device_, PMBUS_STATUS_WORD, &telem.status_word) < 0) {
            telem.status_word = 0xFFFF;
        }
        
        return telem;
    }
    
    uint8_t readStatusByte() {
        uint8_t status;
        if (pmbus_read_byte(&device_, PMBUS_STATUS_BYTE, &status) < 0) {
            throw PMBusException("Failed to read status byte");
        }
        return status;
    }
};

#endif // PMBUS_HPP
```

```cpp
// main.cpp - C++ example
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include "PMBus.hpp"

int main() {
    try {
        PMBusDevice psu("/dev/i2c-1", 0x58);
        
        std::cout << "PMBus Power Supply Monitor (C++)" << std::endl;
        std::cout << "=================================" << std::endl << std::endl;
        
        psu.clearFaults();
        psu.setOutput(true);
        std::cout << "Output enabled" << std::endl;
        
        // Optional: Set output voltage to 3.3V
        // psu.setVoltage(3.3f);
        
        for (int i = 0; i < 10; i++) {
            auto telem = psu.readTelemetry();
            
            std::cout << "\nTelemetry Reading " << (i + 1) << ":" << std::endl;
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  Input Voltage:  " << std::setw(7) << telem.input_voltage << " V" << std::endl;
            std::cout << "  Output Voltage: " << std::setw(7) << telem.output_voltage << " V" << std::endl;
            std::cout << "  Output Current: " << std::setw(7) << telem.output_current << " A" << std::endl;
            std::cout << "  Temperature:    " << std::setw(7) << telem.temperature << " °C" << std::endl;
            std::cout << "  Output Power:   " << std::setw(7) << telem.output_power << " W" << std::endl;
            
            // Check for faults
            if (telem.status_word & 0x8000) {
                std::cout << "  ⚠️  FAULT DETECTED!" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        psu.setOutput(false);
        std::cout << "\nOutput disabled" << std::endl;
        
    } catch (const PMBusException& e) {
        std::cerr << "PMBus Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
// Cargo.toml
// [dependencies]
// i2cdev = "0.6"

// pmbus.rs
use i2cdev::core::*;
use i2cdev::linux::{LinuxI2CDevice, LinuxI2CError};
use std::thread;
use std::time::Duration;

// PMBus Command Definitions
pub mod commands {
    pub const PAGE: u8 = 0x00;
    pub const OPERATION: u8 = 0x01;
    pub const ON_OFF_CONFIG: u8 = 0x02;
    pub const CLEAR_FAULTS: u8 = 0x03;
    pub const WRITE_PROTECT: u8 = 0x10;
    
    pub const VOUT_MODE: u8 = 0x20;
    pub const VOUT_COMMAND: u8 = 0x21;
    pub const VOUT_MAX: u8 = 0x24;
    
    pub const STATUS_BYTE: u8 = 0x78;
    pub const STATUS_WORD: u8 = 0x79;
    pub const STATUS_VOUT: u8 = 0x7A;
    pub const STATUS_IOUT: u8 = 0x7B;
    pub const STATUS_TEMPERATURE: u8 = 0x7D;
    
    pub const READ_VIN: u8 = 0x88;
    pub const READ_IIN: u8 = 0x89;
    pub const READ_VOUT: u8 = 0x8B;
    pub const READ_IOUT: u8 = 0x8C;
    pub const READ_TEMPERATURE_1: u8 = 0x8D;
    pub const READ_POUT: u8 = 0x96;
    pub const READ_PIN: u8 = 0x97;
}

#[derive(Debug)]
pub struct Telemetry {
    pub input_voltage: f32,
    pub output_voltage: f32,
    pub output_current: f32,
    pub temperature: f32,
    pub output_power: f32,
    pub status_word: u16,
}

pub struct PMBusDevice {
    i2c: LinuxI2CDevice,
    current_page: u8,
}

impl PMBusDevice {
    /// Create a new PMBus device
    pub fn new(bus: &str, address: u16) -> Result<Self, LinuxI2CError> {
        let i2c = LinuxI2CDevice::new(bus, address)?;
        
        Ok(PMBusDevice {
            i2c,
            current_page: 0,
        })
    }
    
    /// Write a byte command
    pub fn write_byte(&mut self, command: u8, data: u8) -> Result<(), LinuxI2CError> {
        self.i2c.smbus_write_byte_data(command, data)
    }
    
    /// Read a byte
    pub fn read_byte(&mut self, command: u8) -> Result<u8, LinuxI2CError> {
        self.i2c.smbus_read_byte_data(command)
    }
    
    /// Write a word (16-bit)
    pub fn write_word(&mut self, command: u8, data: u16) -> Result<(), LinuxI2CError> {
        self.i2c.smbus_write_word_data(command, data)
    }
    
    /// Read a word (16-bit)
    pub fn read_word(&mut self, command: u8) -> Result<u16, LinuxI2CError> {
        self.i2c.smbus_read_word_data(command)
    }
    
    /// Send a command byte (no data)
    pub fn send_byte(&mut self, command: u8) -> Result<(), LinuxI2CError> {
        self.i2c.smbus_write_byte(command)
    }
    
    /// Set the current page
    pub fn set_page(&mut self, page: u8) -> Result<(), LinuxI2CError> {
        self.write_byte(commands::PAGE, page)?;
        self.current_page = page;
        Ok(())
    }
    
    /// Clear all faults
    pub fn clear_faults(&mut self) -> Result<(), LinuxI2CError> {
        self.send_byte(commands::CLEAR_FAULTS)
    }
    
    /// Enable or disable output
    pub fn set_output(&mut self, enable: bool) -> Result<(), LinuxI2CError> {
        let operation = if enable { 0x80 } else { 0x00 };
        self.write_byte(commands::OPERATION, operation)
    }
    
    /// Convert LINEAR11 format to float
    fn linear11_to_float(data: u16) -> f32 {
        // Extract 5-bit exponent (two's complement)
        let mut exponent = ((data >> 11) & 0x1F) as i8;
        if exponent > 15 {
            exponent -= 32;
        }
        
        // Extract 11-bit mantissa (two's complement)
        let mut mantissa = (data & 0x7FF) as i16;
        if mantissa > 1023 {
            mantissa -= 2048;
        }
        
        // Calculate value: mantissa * 2^exponent
        (mantissa as f32) * 2_f32.powi(exponent as i32)
    }

    /// Convert float to LINEAR11 format
    fn float_to_linear11(value: f32) -> u16 {
        if value == 0.0 {
            return 0;
        }
        
        // Find optimal exponent
        let abs_value = value.abs();
        let mut exponent = (abs_value / 1023.0).log2().floor() as i8;
        exponent = exponent.clamp(-15, 15);
        
        // Calculate mantissa
        let mantissa = (value / 2_f32.powi(exponent as i32)).round() as i16;
        let mantissa = mantissa.clamp(-1024, 1023);
        
        // Combine exponent and mantissa
        let exp_bits = (exponent as u16) & 0x1F;
        let mant_bits = (mantissa as u16) & 0x7FF;
        
        (exp_bits << 11) | mant_bits
    }
    
    /// Convert LINEAR16 format to float
    fn linear16_to_float(data: u16, exponent: i8) -> f32 {
        let mantissa = data as i16;
        (mantissa as f32) * 2_f32.powi(exponent as i32)
    }
    
    /// Convert float to LINEAR16 format
    fn float_to_linear16(value: f32, exponent: i8) -> u16 {
        let mantissa = (value / 2_f32.powi(exponent as i32)).round() as i16;
        mantissa as u16
    }
    
    /// Read input voltage
    pub fn read_vin(&mut self) -> Result<f32, LinuxI2CError> {
        let data = self.read_word(commands::READ_VIN)?;
        Ok(Self::linear11_to_float(data))
    }
    
    /// Read output voltage
    pub fn read_vout(&mut self) -> Result<f32, LinuxI2CError> {
        let vout_mode = self.read_byte(commands::VOUT_MODE)?;
        let data = self.read_word(commands::READ_VOUT)?;
        
        let mode = (vout_mode >> 5) & 0x07;
        
        if mode == 0 {
            // LINEAR mode
            let mut exponent = (vout_mode & 0x1F) as i8;
            if exponent > 15 {
                exponent -= 32;
            }
            Ok(Self::linear16_to_float(data, exponent))
        } else {
            // VID or DIRECT mode - fallback to LINEAR11
            Ok(Self::linear11_to_float(data))
        }
    }
    
    /// Read output current
    pub fn read_iout(&mut self) -> Result<f32, LinuxI2CError> {
        let data = self.read_word(commands::READ_IOUT)?;
        Ok(Self::linear11_to_float(data))
    }
    
    /// Read temperature
    pub fn read_temperature(&mut self) -> Result<f32, LinuxI2CError> {
        let data = self.read_word(commands::READ_TEMPERATURE_1)?;
        Ok(Self::linear11_to_float(data))
    }
    
    /// Read output power
    pub fn read_pout(&mut self) -> Result<f32, LinuxI2CError> {
        let data = self.read_word(commands::READ_POUT)?;
        Ok(Self::linear11_to_float(data))
    }
    
    /// Read status word
    pub fn read_status_word(&mut self) -> Result<u16, LinuxI2CError> {
        self.read_word(commands::STATUS_WORD)
    }
    
    /// Read all telemetry at once
    pub fn read_telemetry(&mut self) -> Result<Telemetry, LinuxI2CError> {
        Ok(Telemetry {
            input_voltage: self.read_vin()?,
            output_voltage: self.read_vout()?,
            output_current: self.read_iout()?,
            temperature: self.read_temperature()?,
            output_power: self.read_pout()?,
            status_word: self.read_status_word()?,
        })
    }
    
    /// Set output voltage (LINEAR16 mode with exponent -9)
    pub fn set_voltage(&mut self, voltage: f32) -> Result<(), LinuxI2CError> {
        let data = Self::float_to_linear16(voltage, -9);
        self.write_word(commands::VOUT_COMMAND, data)
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut psu = PMBusDevice::new("/dev/i2c-1", 0x58)?;
    
    println!("PMBus Power Supply Monitor (Rust)");
    println!("==================================\n");
    
    // Clear faults and enable output
    psu.clear_faults()?;
    psu.set_output(true)?;
    println!("Output enabled\n");
    
    // Optional: Set voltage to 3.3V
    // psu.set_voltage(3.3)?;
    
    // Monitor for 10 seconds
    for i in 0..10 {
        match psu.read_telemetry() {
            Ok(telem) => {
                println!("Telemetry Reading {}:", i + 1);
                println!("  Input Voltage:  {:7.2} V", telem.input_voltage);
                println!("  Output Voltage: {:7.2} V", telem.output_voltage);
                println!("  Output Current: {:7.2} A", telem.output_current);
                println!("  Temperature:    {:7.2} °C", telem.temperature);
                println!("  Output Power:   {:7.2} W", telem.output_power);
                
                // Check for faults
                if telem.status_word & 0x8000 != 0 {
                    println!("  ⚠️  FAULT DETECTED!");
                }
                if telem.status_word & 0x4000 != 0 {
                    println!("  ⚠️  COMMUNICATION ERROR!");
                }
                if telem.status_word & 0x0800 != 0 {
                    println!("  ⚠️  OVER TEMPERATURE!");
                }
                
                println!();
            }
            Err(e) => {
                eprintln!("Error reading telemetry: {}", e);
            }
        }
        
        thread::sleep(Duration::from_secs(1));
    }
    
    // Disable output
    psu.set_output(false)?;
    println!("Output disabled");
    
    Ok(())
}
```

## Key Concepts Summary

1. **PMBus is SMBus-based**: Uses standard I2C hardware with SMBus protocol extensions
2. **Standardized commands**: 200+ defined commands for power management
3. **Multiple data formats**: LINEAR11, LINEAR16, VID, and DIRECT modes
4. **Telemetry focus**: Built for monitoring voltage, current, temperature, and power
5. **Fault management**: Comprehensive status reporting and fault clearing
6. **Multi-output support**: PAGE command allows selecting different outputs
7. **Write protection**: Prevents accidental configuration changes

PMBus simplifies power supply management by providing a standard interface that works across different manufacturers and models, making it ideal for server power supplies, DC-DC converters, and other managed power systems.