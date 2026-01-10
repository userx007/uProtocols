# Driver Abstraction Layers for I2C

Driver abstraction layers are software interfaces that provide a uniform API for I2C communication across different hardware platforms and operating systems. They allow you to write portable code that can work on various microcontrollers, embedded Linux systems, and other platforms without modification.

## Why Use Abstraction Layers?

The main benefits include:

**Portability**: Write code once and deploy it across multiple platforms (STM32, ESP32, Raspberry Pi, etc.)

**Maintainability**: Changes to underlying hardware drivers don't require rewriting application code

**Testability**: Mock implementations can be used for unit testing without hardware

**Separation of Concerns**: Hardware-specific details are isolated from business logic

## Architecture

A typical I2C driver abstraction has three layers:

1. **Hardware Abstraction Layer (HAL)**: Platform-specific implementation that talks directly to hardware
2. **Abstract Interface**: Platform-agnostic API that defines standard I2C operations
3. **Application Layer**: Your code that uses the abstract interface

## C/C++ Implementation

Here's a comprehensive example showing how to create an I2C abstraction layer in C/C++:

```c
// i2c_hal.h - Abstract Interface
#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Error codes
typedef enum {
    I2C_OK = 0,
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_NACK,
    I2C_ERROR_BUS,
    I2C_ERROR_INVALID_PARAM,
    I2C_ERROR_NOT_INITIALIZED
} i2c_status_t;

// I2C configuration
typedef struct {
    uint32_t clock_speed;  // Clock speed in Hz (e.g., 100000 for 100kHz)
    uint8_t address_mode;  // 7-bit or 10-bit addressing
    uint32_t timeout_ms;   // Timeout in milliseconds
} i2c_config_t;

// Forward declaration of platform-specific handle
typedef struct i2c_handle i2c_handle_t;

// Abstract interface - implemented by each platform
typedef struct {
    i2c_status_t (*init)(i2c_handle_t **handle, const i2c_config_t *config);
    i2c_status_t (*deinit)(i2c_handle_t *handle);
    i2c_status_t (*write)(i2c_handle_t *handle, uint8_t addr, const uint8_t *data, size_t len);
    i2c_status_t (*read)(i2c_handle_t *handle, uint8_t addr, uint8_t *data, size_t len);
    i2c_status_t (*write_read)(i2c_handle_t *handle, uint8_t addr, 
                               const uint8_t *wdata, size_t wlen,
                               uint8_t *rdata, size_t rlen);
    i2c_status_t (*write_reg)(i2c_handle_t *handle, uint8_t addr, uint8_t reg, uint8_t value);
    i2c_status_t (*read_reg)(i2c_handle_t *handle, uint8_t addr, uint8_t reg, uint8_t *value);
    bool (*is_device_ready)(i2c_handle_t *handle, uint8_t addr);
} i2c_driver_t;

// Global driver instance (set by platform-specific code)
extern const i2c_driver_t *g_i2c_driver;

// Convenience wrappers
static inline i2c_status_t i2c_init(i2c_handle_t **handle, const i2c_config_t *config) {
    return g_i2c_driver->init(handle, config);
}

static inline i2c_status_t i2c_deinit(i2c_handle_t *handle) {
    return g_i2c_driver->deinit(handle);
}

static inline i2c_status_t i2c_write(i2c_handle_t *handle, uint8_t addr, 
                                      const uint8_t *data, size_t len) {
    return g_i2c_driver->write(handle, addr, data, len);
}

static inline i2c_status_t i2c_read(i2c_handle_t *handle, uint8_t addr, 
                                     uint8_t *data, size_t len) {
    return g_i2c_driver->read(handle, addr, data, len);
}

static inline i2c_status_t i2c_write_read(i2c_handle_t *handle, uint8_t addr,
                                          const uint8_t *wdata, size_t wlen,
                                          uint8_t *rdata, size_t rlen) {
    return g_i2c_driver->write_read(handle, addr, wdata, wlen, rdata, rlen);
}

static inline i2c_status_t i2c_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                         uint8_t reg, uint8_t value) {
    return g_i2c_driver->write_reg(handle, addr, reg, value);
}

static inline i2c_status_t i2c_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                        uint8_t reg, uint8_t *value) {
    return g_i2c_driver->read_reg(handle, addr, reg, value);
}

static inline bool i2c_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    return g_i2c_driver->is_device_ready(handle, addr);
}

#endif // I2C_HAL_H

// ============================================================================
// PLATFORM IMPLEMENTATION 1: Linux (using /dev/i2c-X)
// ============================================================================
// i2c_linux.c
#ifdef PLATFORM_LINUX

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct i2c_handle {
    int fd;
    uint32_t timeout_ms;
};

static i2c_status_t linux_init(i2c_handle_t **handle, const i2c_config_t *config) {
    if (!handle || !config) return I2C_ERROR_INVALID_PARAM;
    
    i2c_handle_t *h = malloc(sizeof(i2c_handle_t));
    if (!h) return I2C_ERROR_NOT_INITIALIZED;
    
    // Open I2C bus (typically /dev/i2c-1 on Raspberry Pi)
    h->fd = open("/dev/i2c-1", O_RDWR);
    if (h->fd < 0) {
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    h->timeout_ms = config->timeout_ms;
    *handle = h;
    return I2C_OK;
}

static i2c_status_t linux_deinit(i2c_handle_t *handle) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    close(handle->fd);
    free(handle);
    return I2C_OK;
}

static i2c_status_t linux_write(i2c_handle_t *handle, uint8_t addr, 
                                const uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    if (ioctl(handle->fd, I2C_SLAVE, addr) < 0) {
        return I2C_ERROR_NACK;
    }
    
    if (write(handle->fd, data, len) != (ssize_t)len) {
        return I2C_ERROR_BUS;
    }
    
    return I2C_OK;
}

static i2c_status_t linux_read(i2c_handle_t *handle, uint8_t addr, 
                               uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    if (ioctl(handle->fd, I2C_SLAVE, addr) < 0) {
        return I2C_ERROR_NACK;
    }
    
    if (read(handle->fd, data, len) != (ssize_t)len) {
        return I2C_ERROR_BUS;
    }
    
    return I2C_OK;
}

static i2c_status_t linux_write_read(i2c_handle_t *handle, uint8_t addr,
                                     const uint8_t *wdata, size_t wlen,
                                     uint8_t *rdata, size_t rlen) {
    i2c_status_t status;
    status = linux_write(handle, addr, wdata, wlen);
    if (status != I2C_OK) return status;
    return linux_read(handle, addr, rdata, rlen);
}

static i2c_status_t linux_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                    uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return linux_write(handle, addr, buf, 2);
}

static i2c_status_t linux_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                   uint8_t reg, uint8_t *value) {
    return linux_write_read(handle, addr, &reg, 1, value, 1);
}

static bool linux_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    if (!handle) return false;
    return ioctl(handle->fd, I2C_SLAVE, addr) >= 0;
}

const i2c_driver_t i2c_linux_driver = {
    .init = linux_init,
    .deinit = linux_deinit,
    .write = linux_write,
    .read = linux_read,
    .write_read = linux_write_read,
    .write_reg = linux_write_reg,
    .read_reg = linux_read_reg,
    .is_device_ready = linux_is_device_ready
};

const i2c_driver_t *g_i2c_driver = &i2c_linux_driver;

#endif // PLATFORM_LINUX

// ============================================================================
// PLATFORM IMPLEMENTATION 2: STM32 HAL
// ============================================================================
// i2c_stm32.c
#ifdef PLATFORM_STM32

#include "stm32f4xx_hal.h"

struct i2c_handle {
    I2C_HandleTypeDef *hi2c;
    uint32_t timeout_ms;
};

static i2c_status_t stm32_init(i2c_handle_t **handle, const i2c_config_t *config) {
    if (!handle || !config) return I2C_ERROR_INVALID_PARAM;
    
    i2c_handle_t *h = malloc(sizeof(i2c_handle_t));
    if (!h) return I2C_ERROR_NOT_INITIALIZED;
    
    // Allocate STM32 HAL handle
    h->hi2c = malloc(sizeof(I2C_HandleTypeDef));
    if (!h->hi2c) {
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    // Configure I2C peripheral (I2C1 example)
    h->hi2c->Instance = I2C1;
    h->hi2c->Init.ClockSpeed = config->clock_speed;
    h->hi2c->Init.DutyCycle = I2C_DUTYCYCLE_2;
    h->hi2c->Init.OwnAddress1 = 0;
    h->hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    h->hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    if (HAL_I2C_Init(h->hi2c) != HAL_OK) {
        free(h->hi2c);
        free(h);
        return I2C_ERROR_NOT_INITIALIZED;
    }
    
    h->timeout_ms = config->timeout_ms;
    *handle = h;
    return I2C_OK;
}

static i2c_status_t stm32_deinit(i2c_handle_t *handle) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    HAL_I2C_DeInit(handle->hi2c);
    free(handle->hi2c);
    free(handle);
    return I2C_OK;
}

static i2c_status_t stm32_write(i2c_handle_t *handle, uint8_t addr, 
                                const uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(handle->hi2c, addr << 1, 
                                                        (uint8_t*)data, len, 
                                                        handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_read(i2c_handle_t *handle, uint8_t addr, 
                               uint8_t *data, size_t len) {
    if (!handle || !data) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(handle->hi2c, addr << 1, 
                                                       data, len, 
                                                       handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_write_read(i2c_handle_t *handle, uint8_t addr,
                                     const uint8_t *wdata, size_t wlen,
                                     uint8_t *rdata, size_t rlen) {
    i2c_status_t status;
    status = stm32_write(handle, addr, wdata, wlen);
    if (status != I2C_OK) return status;
    return stm32_read(handle, addr, rdata, rlen);
}

static i2c_status_t stm32_write_reg(i2c_handle_t *handle, uint8_t addr, 
                                    uint8_t reg, uint8_t value) {
    if (!handle) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(handle->hi2c, addr << 1, reg, 
                                                  I2C_MEMADD_SIZE_8BIT, &value, 
                                                  1, handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static i2c_status_t stm32_read_reg(i2c_handle_t *handle, uint8_t addr, 
                                   uint8_t reg, uint8_t *value) {
    if (!handle || !value) return I2C_ERROR_INVALID_PARAM;
    
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(handle->hi2c, addr << 1, reg, 
                                                 I2C_MEMADD_SIZE_8BIT, value, 
                                                 1, handle->timeout_ms);
    
    if (status == HAL_OK) return I2C_OK;
    if (status == HAL_TIMEOUT) return I2C_ERROR_TIMEOUT;
    return I2C_ERROR_BUS;
}

static bool stm32_is_device_ready(i2c_handle_t *handle, uint8_t addr) {
    if (!handle) return false;
    return HAL_I2C_IsDeviceReady(handle->hi2c, addr << 1, 3, 
                                  handle->timeout_ms) == HAL_OK;
}

const i2c_driver_t i2c_stm32_driver = {
    .init = stm32_init,
    .deinit = stm32_deinit,
    .write = stm32_write,
    .read = stm32_read,
    .write_read = stm32_write_read,
    .write_reg = stm32_write_reg,
    .read_reg = stm32_read_reg,
    .is_device_ready = stm32_is_device_ready
};

const i2c_driver_t *g_i2c_driver = &i2c_stm32_driver;

#endif // PLATFORM_STM32

// ============================================================================
// APPLICATION CODE - Platform Independent!
// ============================================================================
// sensor_app.c

#include <stdio.h>

#define BME280_ADDR 0x76
#define BME280_CHIP_ID_REG 0xD0

int main(void) {
    i2c_handle_t *i2c;
    
    // Initialize I2C with standard 100kHz speed
    i2c_config_t config = {
        .clock_speed = 100000,
        .address_mode = 7,
        .timeout_ms = 1000
    };
    
    if (i2c_init(&i2c, &config) != I2C_OK) {
        printf("Failed to initialize I2C\n");
        return -1;
    }
    
    // Check if BME280 sensor is present
    if (i2c_is_device_ready(i2c, BME280_ADDR)) {
        printf("BME280 sensor found!\n");
        
        // Read chip ID
        uint8_t chip_id;
        if (i2c_read_reg(i2c, BME280_ADDR, BME280_CHIP_ID_REG, &chip_id) == I2C_OK) {
            printf("Chip ID: 0x%02X\n", chip_id);
        }
        
        // Write to configuration register
        i2c_write_reg(i2c, BME280_ADDR, 0xF4, 0x27);  // Normal mode
    } else {
        printf("BME280 sensor not found\n");
    }
    
    // Cleanup
    i2c_deinit(i2c);
    return 0;
}
```

## Rust Implementation

Rust's trait system is particularly well-suited for abstraction layers. Here's a comprehensive Rust implementation:

```rust
// lib.rs - Abstract Interface using Traits

use std::time::Duration;

// Error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    Timeout,
    Nack,
    BusError,
    InvalidParam,
    NotInitialized,
    Io(std::io::ErrorKind),
}

impl std::fmt::Display for I2cError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            I2cError::Timeout => write!(f, "I2C timeout"),
            I2cError::Nack => write!(f, "I2C NACK received"),
            I2cError::BusError => write!(f, "I2C bus error"),
            I2cError::InvalidParam => write!(f, "Invalid parameter"),
            I2cError::NotInitialized => write!(f, "I2C not initialized"),
            I2cError::Io(kind) => write!(f, "IO error: {:?}", kind),
        }
    }
}

impl std::error::Error for I2cError {}

pub type Result<T> = std::result::Result<T, I2cError>;

// Configuration structure
#[derive(Debug, Clone)]
pub struct I2cConfig {
    pub clock_speed: u32,     // Clock speed in Hz
    pub address_mode: u8,     // 7 or 10 bit addressing
    pub timeout: Duration,
}

impl Default for I2cConfig {
    fn default() -> Self {
        Self {
            clock_speed: 100_000,  // 100 kHz
            address_mode: 7,
            timeout: Duration::from_millis(1000),
        }
    }
}

// Core I2C trait - defines the abstraction interface
pub trait I2cBus {
    /// Write data to a device
    fn write(&mut self, addr: u8, data: &[u8]) -> Result<()>;
    
    /// Read data from a device
    fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()>;
    
    /// Write then read (common for register access)
    fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()>;
    
    /// Check if device is present on the bus
    fn is_device_ready(&mut self, addr: u8) -> bool;
}

// Extended trait for register-based operations
pub trait I2cRegister: I2cBus {
    /// Write a single byte to a register
    fn write_reg(&mut self, addr: u8, reg: u8, value: u8) -> Result<()> {
        self.write(addr, &[reg, value])
    }
    
    /// Read a single byte from a register
    fn read_reg(&mut self, addr: u8, reg: u8) -> Result<u8> {
        let mut buffer = [0u8; 1];
        self.write_read(addr, &[reg], &mut buffer)?;
        Ok(buffer[0])
    }
    
    /// Write multiple bytes to consecutive registers
    fn write_regs(&mut self, addr: u8, reg: u8, data: &[u8]) -> Result<()> {
        let mut buffer = vec![reg];
        buffer.extend_from_slice(data);
        self.write(addr, &buffer)
    }
    
    /// Read multiple bytes from consecutive registers
    fn read_regs(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) -> Result<()> {
        self.write_read(addr, &[reg], buffer)
    }
    
    /// Modify bits in a register (read-modify-write)
    fn modify_reg<F>(&mut self, addr: u8, reg: u8, f: F) -> Result<()>
    where
        F: FnOnce(u8) -> u8,
    {
        let value = self.read_reg(addr, reg)?;
        let new_value = f(value);
        self.write_reg(addr, reg, new_value)
    }
}

// Auto-implement I2cRegister for any type that implements I2cBus
impl<T: I2cBus> I2cRegister for T {}

// ============================================================================
// PLATFORM IMPLEMENTATION 1: Linux using i2c-dev
// ============================================================================

#[cfg(target_os = "linux")]
pub mod linux {
    use super::*;
    use std::fs::{File, OpenOptions};
    use std::os::unix::io::AsRawFd;
    
    const I2C_SLAVE: u64 = 0x0703;
    
    pub struct LinuxI2c {
        file: File,
        timeout: Duration,
        current_addr: Option<u8>,
    }
    
    impl LinuxI2c {
        pub fn new(bus: u8, config: I2cConfig) -> Result<Self> {
            let path = format!("/dev/i2c-{}", bus);
            let file = OpenOptions::new()
                .read(true)
                .write(true)
                .open(&path)
                .map_err(|e| I2cError::Io(e.kind()))?;
            
            Ok(Self {
                file,
                timeout: config.timeout,
                current_addr: None,
            })
        }
        
        fn set_slave_addr(&mut self, addr: u8) -> Result<()> {
            if self.current_addr == Some(addr) {
                return Ok(());
            }
            
            unsafe {
                if libc::ioctl(self.file.as_raw_fd(), I2C_SLAVE, addr as libc::c_ulong) < 0 {
                    return Err(I2cError::Nack);
                }
            }
            
            self.current_addr = Some(addr);
            Ok(())
        }
    }
    
    impl I2cBus for LinuxI2c {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            self.set_slave_addr(addr)?;
            
            use std::io::Write;
            self.file.write_all(data)
                .map_err(|e| I2cError::Io(e.kind()))
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            self.set_slave_addr(addr)?;
            
            use std::io::Read;
            self.file.read_exact(buffer)
                .map_err(|e| I2cError::Io(e.kind()))
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            self.write(addr, write_data)?;
            self.read(addr, read_buffer)
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            self.set_slave_addr(addr).is_ok()
        }
    }
}

// ============================================================================
// PLATFORM IMPLEMENTATION 2: Embedded HAL (for embedded-hal ecosystem)
// ============================================================================

#[cfg(feature = "embedded-hal")]
pub mod embedded {
    use super::*;
    use embedded_hal::i2c::{I2c as EmbeddedI2c, ErrorType};
    
    /// Adapter that wraps any embedded-hal I2C implementation
    pub struct EmbeddedI2cAdapter<I2C> {
        i2c: I2C,
        timeout: Duration,
    }
    
    impl<I2C> EmbeddedI2cAdapter<I2C>
    where
        I2C: EmbeddedI2c,
    {
        pub fn new(i2c: I2C, config: I2cConfig) -> Self {
            Self {
                i2c,
                timeout: config.timeout,
            }
        }
    }
    
    impl<I2C> I2cBus for EmbeddedI2cAdapter<I2C>
    where
        I2C: EmbeddedI2c,
    {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            self.i2c.write(addr, data)
                .map_err(|_| I2cError::BusError)
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            self.i2c.read(addr, buffer)
                .map_err(|_| I2cError::BusError)
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            self.i2c.write_read(addr, write_data, read_buffer)
                .map_err(|_| I2cError::BusError)
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            // Try a zero-byte write
            self.i2c.write(addr, &[]).is_ok()
        }
    }
}

// ============================================================================
// MOCK IMPLEMENTATION - For Testing
// ============================================================================

#[cfg(test)]
pub mod mock {
    use super::*;
    use std::collections::HashMap;
    
    /// Mock I2C bus for unit testing
    pub struct MockI2c {
        devices: HashMap<u8, HashMap<u8, u8>>,  // addr -> (reg -> value)
        write_log: Vec<(u8, Vec<u8>)>,
        should_fail: bool,
    }
    
    impl MockI2c {
        pub fn new() -> Self {
            Self {
                devices: HashMap::new(),
                write_log: Vec::new(),
                should_fail: false,
            }
        }
        
        /// Add a device with initial register values
        pub fn add_device(&mut self, addr: u8, registers: HashMap<u8, u8>) {
            self.devices.insert(addr, registers);
        }
        
        /// Set whether operations should fail
        pub fn set_should_fail(&mut self, fail: bool) {
            self.should_fail = fail;
        }
        
        /// Get the write log for verification
        pub fn get_write_log(&self) -> &[(u8, Vec<u8>)] {
            &self.write_log
        }
    }
    
    impl I2cBus for MockI2c {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            self.write_log.push((addr, data.to_vec()));
            
            if let Some(device) = self.devices.get_mut(&addr) {
                // Assume first byte is register, rest is data
                if data.len() >= 2 {
                    let reg = data[0];
                    for (i, &value) in data[1..].iter().enumerate() {
                        device.insert(reg + i as u8, value);
                    }
                }
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            if self.devices.contains_key(&addr) {
                buffer.fill(0xFF);  // Return dummy data
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            if let Some(device) = self.devices.get(&addr) {
                // Assume write_data[0] is register address
                if !write_data.is_empty() {
                    let reg = write_data[0];
                    for (i, byte) in read_buffer.iter_mut().enumerate() {
                        *byte = *device.get(&(reg + i as u8)).unwrap_or(&0xFF);
                    }
                }
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            !self.should_fail && self.devices.contains_key(&addr)
        }
    }
}

// ============================================================================
// APPLICATION CODE - Platform Independent!
// ============================================================================

/// Example sensor driver that uses the abstraction
pub struct Bme280<I2C> {
    i2c: I2C,
    addr: u8,
}

impl<I2C: I2cBus> Bme280<I2C> {
    const CHIP_ID_REG: u8 = 0xD0;
    const CTRL_MEAS_REG: u8 = 0xF4;
    const EXPECTED_CHIP_ID: u8 = 0x60;
    
    pub fn new(i2c: I2C, addr: u8) -> Result<Self> {
        let mut sensor = Self { i2c, addr };
        
        // Verify chip ID
        let chip_id = sensor.i2c.read_reg(sensor.addr, Self::CHIP_ID_REG)?;
        if chip_id != Self::EXPECTED_CHIP_ID {
            return Err(I2cError::InvalidParam);
        }
        
        Ok(sensor)
    }
    
    pub fn set_mode(&mut self, mode: u8) -> Result<()> {
        self.i2c.write_reg(self.addr, Self::CTRL_MEAS_REG, mode)
    }
    
    pub fn read_temperature(&mut self) -> Result<f32> {
        // Simplified - real implementation would read temp registers
        let mut data = [0u8; 3];
        self.i2c.read_regs(self.addr, 0xFA, &mut data)?;
        
        let raw = ((data[0] as u32) << 12) | ((data[1] as u32) << 4) | ((data[2] as u32) >> 4);
        Ok(raw as f32 / 100.0)  // Simplified conversion
    }
}

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

#[cfg(target_os = "linux")]
fn example_linux() -> Result<()> {
    use linux::LinuxI2c;
    
    let config = I2cConfig::default();
    let mut i2c = LinuxI2c::new(1, config)?;
    
    let mut sensor = Bme280::new(i2c, 0x76)?;
    sensor.set_mode(0x27)?;
    
    let temp = sensor.read_temperature()?;
    println!("Temperature: {:.2}°C", temp);
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use mock::MockI2c;
    use std::collections::HashMap;
    
    #[test]
    fn test_sensor_with_mock() {
        let mut i2c = MockI2c::new();
        
        // Set up mock device
        let mut regs = HashMap::new();
        regs.insert(0xD0, 0x60);  // Chip ID
        regs.insert(0xF4, 0x00);  // Control register
        i2c.add_device(0x76, regs);
        
        // Test sensor initialization
        let mut sensor = Bme280::new(i2c, 0x76).unwrap();
        
        // Test setting mode
        assert!(sensor.set_mode(0x27).is_ok());
    }
    
    #[test]
    fn test_register_operations() {
        let mut i2c = MockI2c::new();
        let mut regs = HashMap::new();
        regs.insert(0x10, 0x42);
        i2c.add_device(0x50, regs);
        
        // Test read
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x42);
        
        // Test write
        i2c.write_reg(0x50, 0x10, 0x99).unwrap();
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x99);
        
        // Test modify
        i2c.modify_reg(0x50, 0x10, |v| v | 0x01).unwrap();
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x99 | 0x01);
    }
}

// Main function showing platform selection
fn main() -> Result<()> {
    #[cfg(target_os = "linux")]
    {
        println!("Running on Linux platform");
        example_linux()?;
    }
    
    #[cfg(not(target_os = "linux"))]
    {
        println!("Platform not supported in this example");
    }
    
    Ok(())
}
```

## Key Design Patterns

### Function Pointers (C) vs Traits (Rust)

C uses a struct of function pointers to achieve polymorphism, while Rust uses its trait system which provides compile-time guarantees and zero-cost abstractions.

### Platform Selection

Both implementations use conditional compilation to select the appropriate platform implementation at build time:
- **C**: Uses `#ifdef PLATFORM_LINUX` or `#ifdef PLATFORM_STM32`
- **Rust**: Uses `#[cfg(target_os = "linux")]` or feature flags

### Error Handling

- **C**: Returns status codes via `i2c_status_t` enum
- **Rust**: Uses `Result<T, I2cError>` for type-safe error handling with the `?` operator

## Advanced Features

### Mock Implementation for Testing

The Rust example includes a complete mock implementation that allows you to unit test device drivers without hardware. This is invaluable for:

- Testing edge cases and error conditions
- Continuous integration pipelines
- Development before hardware arrives
- Regression testing

### Register Operations

Both implementations provide higher-level register access functions built on top of the basic read/write operations. The Rust version includes a particularly elegant `modify_reg` function using closures.

### Memory Management

- **C**: Manual memory management with `malloc`/`free`
- **Rust**: Automatic memory management with ownership system - no manual cleanup needed

## Best Practices

When creating abstraction layers:

1. **Keep the interface minimal**: Only include operations that are truly platform-independent
2. **Make common operations easy**: Provide convenience functions for typical tasks like register access
3. **Handle errors consistently**: Use a unified error type across all platforms
4. **Document platform differences**: Note any quirks or limitations of specific implementations
5. **Provide examples**: Show how to use the abstraction with real hardware drivers
6. **Test thoroughly**: Use mocks and hardware testing to verify correctness

This abstraction approach allows you to write device drivers once and deploy them across any platform that implements the HAL interface, dramatically reducing code duplication and maintenance burden.