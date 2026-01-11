# I2C Combined Transactions: Detailed Guide

## Overview

Combined transactions in I2C allow you to perform multiple operations (typically write-then-read) without releasing the bus between them. This is accomplished using **repeated START conditions** instead of issuing a STOP condition between operations.

## Why Combined Transactions Matter

**Without Combined Transactions (Separate Operations):**
```
START - ADDR(W) - REG_ADDR - STOP
START - ADDR(R) - DATA - STOP
```

**With Combined Transactions (Repeated START):**
```
START - ADDR(W) - REG_ADDR - REPEATED_START - ADDR(R) - DATA - STOP
```

The key difference is that the bus remains under the master's control between operations, preventing other masters from interrupting the transaction sequence.

### Critical Use Cases

1. **Register Reading**: Writing a register address, then reading its value
2. **Atomic Operations**: Ensuring no other device can interrupt between write and read
3. **Multi-master Systems**: Maintaining bus ownership for complete transactions
4. **EEPROM Access**: Reading from specific memory addresses
5. **Sensor Data Retrieval**: Selecting a sensor register then reading its data

## How Repeated START Works

The repeated START condition:
- Keeps SDA and SCL under master control
- Doesn't release the bus to other masters
- Signals a new transaction without ending the current one
- Changes the direction (write to read or vice versa) without a STOP

**Timing Diagram:**
```
     ___     _______________     ___     _______________
SCL     |___|               |___|   |___|               |___
        
     _____   ___   ___   ___     _____   ___   ___   ___
SDA       |_|   |_|   |_|   |...|     |_|   |_|   |_|   |...
     
     S   ADDR+W  REG_ADDR    Sr  ADDR+R   DATA        P
```
- S = START
- Sr = Repeated START (no STOP before it)
- P = STOP

## C/C++ Implementation Examples

### Example 1: Linux I2C with ioctl

```c
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

// Read a register using combined transaction
int i2c_read_register(int file, uint8_t device_addr, 
                      uint8_t reg_addr, uint8_t *data, size_t length) {
    struct i2c_msg messages[2];
    struct i2c_rdwr_ioctl_data transaction;
    
    // First message: write register address
    messages[0].addr = device_addr;
    messages[0].flags = 0;  // Write operation
    messages[0].len = 1;
    messages[0].buf = &reg_addr;
    
    // Second message: read data
    messages[1].addr = device_addr;
    messages[1].flags = I2C_M_RD;  // Read operation
    messages[1].len = length;
    messages[1].buf = data;
    
    // Combined transaction with repeated START
    transaction.msgs = messages;
    transaction.nmsgs = 2;
    
    if (ioctl(file, I2C_RDWR, &transaction) < 0) {
        perror("Failed to perform I2C transaction");
        return -1;
    }
    
    return 0;
}

// Example usage: Read temperature from sensor
int main() {
    int file;
    uint8_t data[2];
    
    // Open I2C bus
    file = open("/dev/i2c-1", O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }
    
    // Read 2 bytes from register 0x00 of device at 0x48
    if (i2c_read_register(file, 0x48, 0x00, data, 2) == 0) {
        int16_t temp = (data[0] << 8) | data[1];
        printf("Temperature raw value: %d\n", temp);
    }
    
    close(file);
    return 0;
}
```

### Example 2: STM32 HAL (Embedded C)

```c
#include "stm32f4xx_hal.h"

I2C_HandleTypeDef hi2c1;

// Read multiple bytes from a register using combined transaction
HAL_StatusTypeDef read_sensor_register(uint8_t device_addr, 
                                       uint8_t reg_addr,
                                       uint8_t *data, 
                                       uint16_t length) {
    HAL_StatusTypeDef status;
    
    // HAL_I2C_Mem_Read automatically uses repeated START
    status = HAL_I2C_Mem_Read(
        &hi2c1,                    // I2C handle
        device_addr << 1,          // Device address (7-bit shifted)
        reg_addr,                  // Memory address (register)
        I2C_MEMADD_SIZE_8BIT,      // Register address size
        data,                      // Data buffer
        length,                    // Number of bytes to read
        HAL_MAX_DELAY              // Timeout
    );
    
    return status;
}

// Example: Read accelerometer data (6 bytes from register 0x28)
void read_accelerometer(void) {
    uint8_t accel_data[6];
    int16_t x, y, z;
    
    if (read_sensor_register(0x19, 0x28, accel_data, 6) == HAL_OK) {
        // Combine bytes into 16-bit values
        x = (int16_t)((accel_data[1] << 8) | accel_data[0]);
        y = (int16_t)((accel_data[3] << 8) | accel_data[2]);
        z = (int16_t)((accel_data[5] << 8) | accel_data[4]);
        
        printf("Accel X: %d, Y: %d, Z: %d\n", x, y, z);
    }
}
```

### Example 3: Arduino/ESP32 (C++)

```cpp
#include <Wire.h>

class I2CDevice {
private:
    uint8_t address;
    TwoWire* wire;
    
public:
    I2CDevice(uint8_t addr, TwoWire* wireInstance = &Wire) 
        : address(addr), wire(wireInstance) {}
    
    // Read register using combined transaction
    bool readRegister(uint8_t reg, uint8_t* data, size_t length) {
        // Begin transmission to write register address
        wire->beginTransmission(address);
        wire->write(reg);
        
        // End transmission with repeated START (false = no STOP)
        if (wire->endTransmission(false) != 0) {
            return false;
        }
        
        // Request data from device (this creates the repeated START)
        if (wire->requestFrom(address, length) != length) {
            return false;
        }
        
        // Read received bytes
        for (size_t i = 0; i < length; i++) {
            data[i] = wire->read();
        }
        
        return true;
    }
    
    // Read 16-bit register (big-endian)
    bool readRegister16(uint8_t reg, uint16_t* value) {
        uint8_t data[2];
        if (!readRegister(reg, data, 2)) {
            return false;
        }
        *value = (data[0] << 8) | data[1];
        return true;
    }
};

// Example usage with BME280 sensor
void setup() {
    Serial.begin(115200);
    Wire.begin();
    
    I2CDevice bme280(0x76);  // BME280 address
    
    // Read chip ID (register 0xD0)
    uint8_t chipId;
    if (bme280.readRegister(0xD0, &chipId, 1)) {
        Serial.printf("Chip ID: 0x%02X\n", chipId);
    }
    
    // Read temperature calibration data (16-bit)
    uint16_t dig_T1;
    if (bme280.readRegister16(0x88, &dig_T1)) {
        Serial.printf("Calibration T1: %u\n", dig_T1);
    }
}

void loop() {
    delay(1000);
}
```

## Rust Implementation Examples

### Example 1: Using linux-embedded-hal

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::error::Error;

struct I2CDevice {
    i2c: I2cdev,
    address: u8,
}

impl I2CDevice {
    fn new(bus: &str, address: u8) -> Result<Self, Box<dyn Error>> {
        let i2c = I2cdev::new(bus)?;
        Ok(I2CDevice { i2c, address })
    }
    
    // Read register using combined transaction
    fn read_register(&mut self, reg: u8, buffer: &mut [u8]) -> Result<(), Box<dyn Error>> {
        // write_read performs a combined transaction automatically
        // It writes the register address, then reads data with repeated START
        self.i2c.write_read(self.address, &[reg], buffer)?;
        Ok(())
    }
    
    // Read a single byte from a register
    fn read_register_byte(&mut self, reg: u8) -> Result<u8, Box<dyn Error>> {
        let mut data = [0u8; 1];
        self.read_register(reg, &mut data)?;
        Ok(data[0])
    }
    
    // Read a 16-bit value (big-endian)
    fn read_register_u16(&mut self, reg: u8) -> Result<u16, Box<dyn Error>> {
        let mut data = [0u8; 2];
        self.read_register(reg, &mut data)?;
        Ok(u16::from_be_bytes(data))
    }
}

// Example: Reading from an EEPROM
fn main() -> Result<(), Box<dyn Error>> {
    let mut eeprom = I2CDevice::new("/dev/i2c-1", 0x50)?;
    
    // Read 16 bytes starting from address 0x00
    let mut data = [0u8; 16];
    eeprom.read_register(0x00, &mut data)?;
    
    println!("EEPROM data: {:02X?}", data);
    
    // Read temperature sensor value
    let mut sensor = I2CDevice::new("/dev/i2c-1", 0x48)?;
    let temp = sensor.read_register_u16(0x00)?;
    println!("Temperature raw: {}", temp);
    
    Ok(())
}
```

### Example 2: Embedded Rust (no_std) with embedded-hal

```rust
#![no_std]
#![no_main]

use embedded_hal::i2c::I2c;
use panic_halt as _;

// Generic I2C device abstraction
pub struct I2CRegisterDevice<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> I2CRegisterDevice<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    // Read register - uses combined transaction internally
    pub fn read_register(&mut self, reg: u8, buffer: &mut [u8]) -> Result<(), E> {
        // write_read is the standard embedded-hal method for combined transactions
        self.i2c.write_read(self.address, &[reg], buffer)
    }
    
    // Read single byte
    pub fn read_byte(&mut self, reg: u8) -> Result<u8, E> {
        let mut byte = [0u8];
        self.read_register(reg, &mut byte)?;
        Ok(byte[0])
    }
    
    // Read 16-bit big-endian value
    pub fn read_u16_be(&mut self, reg: u8) -> Result<u16, E> {
        let mut bytes = [0u8; 2];
        self.read_register(reg, &mut bytes)?;
        Ok(u16::from_be_bytes(bytes))
    }
    
    // Read 16-bit little-endian value
    pub fn read_u16_le(&mut self, reg: u8) -> Result<u16, E> {
        let mut bytes = [0u8; 2];
        self.read_register(reg, &mut bytes)?;
        Ok(u16::from_le_bytes(bytes))
    }
    
    // Write then read multiple registers (burst read)
    pub fn burst_read(&mut self, start_reg: u8, buffer: &mut [u8]) -> Result<(), E> {
        self.i2c.write_read(self.address, &[start_reg], buffer)
    }
}

// Example: MPU6050 accelerometer/gyroscope driver
pub struct MPU6050<I2C> {
    device: I2CRegisterDevice<I2C>,
}

impl<I2C, E> MPU6050<I2C>
where
    I2C: I2c<Error = E>,
{
    const ADDRESS: u8 = 0x68;
    const WHO_AM_I: u8 = 0x75;
    const ACCEL_XOUT_H: u8 = 0x3B;
    
    pub fn new(i2c: I2C) -> Self {
        Self {
            device: I2CRegisterDevice::new(i2c, Self::ADDRESS),
        }
    }
    
    // Verify device identity
    pub fn verify(&mut self) -> Result<bool, E> {
        let id = self.device.read_byte(Self::WHO_AM_I)?;
        Ok(id == 0x68)
    }
    
    // Read all 6 accelerometer bytes in one combined transaction
    pub fn read_accel(&mut self) -> Result<(i16, i16, i16), E> {
        let mut data = [0u8; 6];
        // This reads 6 bytes starting at ACCEL_XOUT_H using a single
        // combined transaction: write register address, repeated START, read 6 bytes
        self.device.burst_read(Self::ACCEL_XOUT_H, &mut data)?;
        
        let x = i16::from_be_bytes([data[0], data[1]]);
        let y = i16::from_be_bytes([data[2], data[3]]);
        let z = i16::from_be_bytes([data[4], data[5]]);
        
        Ok((x, y, z))
    }
}

#[cortex_m_rt::entry]
fn main() -> ! {
    // Platform-specific I2C initialization
    let peripherals = stm32f4xx_hal::pac::Peripherals::take().unwrap();
    let gpiob = peripherals.GPIOB.split();
    let scl = gpiob.pb8.into_alternate_open_drain();
    let sda = gpiob.pb9.into_alternate_open_drain();
    
    let i2c = stm32f4xx_hal::i2c::I2c::new(
        peripherals.I2C1,
        (scl, sda),
        400.kHz(),
        clocks,
    );
    
    let mut mpu = MPU6050::new(i2c);
    
    // Verify device
    if mpu.verify().unwrap() {
        loop {
            // Read accelerometer with combined transaction
            if let Ok((x, y, z)) = mpu.read_accel() {
                // Process data...
            }
        }
    }
    
    loop {}
}
```

### Example 3: Async Rust with embassy

```rust
use embassy_stm32::i2c::I2c;
use embassy_time::{Duration, Timer};
use embedded_hal_async::i2c::I2c as I2cTrait;

pub struct AsyncSensor<'d, T: embassy_stm32::i2c::Instance> {
    i2c: I2c<'d, T>,
    address: u8,
}

impl<'d, T: embassy_stm32::i2c::Instance> AsyncSensor<'d, T> {
    pub fn new(i2c: I2c<'d, T>, address: u8) -> Self {
        Self { i2c, address }
    }
    
    // Async read register with combined transaction
    pub async fn read_register(&mut self, reg: u8, buffer: &mut [u8]) -> Result<(), embassy_stm32::i2c::Error> {
        // write_read performs combined transaction asynchronously
        self.i2c.write_read(self.address, &[reg], buffer).await
    }
    
    // Read sensor continuously
    pub async fn read_continuous(&mut self, reg: u8) -> Result<(), embassy_stm32::i2c::Error> {
        let mut data = [0u8; 2];
        
        loop {
            self.read_register(reg, &mut data).await?;
            let value = u16::from_be_bytes(data);
            
            // Process value...
            
            Timer::after(Duration::from_millis(100)).await;
        }
    }
}
```

## Key Advantages of Combined Transactions

1. **Atomicity**: Prevents other masters from interrupting
2. **Efficiency**: Reduces overhead by eliminating extra START/STOP conditions
3. **Reliability**: Ensures the register pointer doesn't change between write and read
4. **Multi-master Safety**: Critical in systems with multiple I2C masters
5. **Standard Practice**: Expected by most I2C peripheral devices

## Common Pitfalls

1. **Using separate transactions**: Some implementations incorrectly use STOP between write and read
2. **Not checking return values**: Always verify that combined transactions succeed
3. **Incorrect register auto-increment**: Some devices auto-increment registers; combined transactions handle this correctly
4. **Timing assumptions**: Don't assume the bus state without proper repeated START

Combined transactions are fundamental to reliable I2C communication and are the standard way to read from register-based devices.