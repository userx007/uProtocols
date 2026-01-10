# I2C Burst Transfers

## Overview

Burst transfers are sequential multi-byte read/write operations that allow you to transfer multiple bytes of data in a single I2C transaction. Instead of sending separate start/stop conditions for each byte, burst transfers maintain the communication session open, significantly improving efficiency and reducing bus overhead.

## Why Use Burst Transfers?

**Efficiency Benefits:**
- Reduces I2C bus overhead by eliminating repeated start/stop conditions
- Faster data transfer for sequential register access
- Lower CPU overhead compared to individual byte operations
- Essential for reading sensor data arrays, display buffers, and memory devices

**Common Use Cases:**
- Reading multi-byte sensor values (accelerometer X/Y/Z axes)
- Writing display framebuffers
- Accessing EEPROM or flash memory
- Transferring configuration blocks
- Reading FIFO buffers

## How Burst Transfers Work

In a typical burst read operation:

1. **START** condition
2. Send device address + **WRITE** bit
3. Send starting register address
4. **Repeated START** condition
5. Send device address + **READ** bit
6. Read multiple bytes (ACK after each except the last)
7. Send **NACK** after final byte
8. **STOP** condition

## C/C++ Examples

### Example 1: Basic Burst Read (Linux i2c-dev)

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>

#define I2C_DEVICE "/dev/i2c-1"
#define SENSOR_ADDR 0x68  // Example: MPU6050 accelerometer

// Burst read multiple bytes from consecutive registers
int i2c_burst_read(int file, uint8_t reg_addr, uint8_t *buffer, size_t length) {
    // Write register address
    if (write(file, &reg_addr, 1) != 1) {
        perror("Failed to write register address");
        return -1;
    }
    
    // Read multiple bytes
    if (read(file, buffer, length) != length) {
        perror("Failed to read data");
        return -1;
    }
    
    return 0;
}

// Burst write multiple bytes to consecutive registers
int i2c_burst_write(int file, uint8_t reg_addr, const uint8_t *data, size_t length) {
    uint8_t buffer[length + 1];
    buffer[0] = reg_addr;
    
    for (size_t i = 0; i < length; i++) {
        buffer[i + 1] = data[i];
    }
    
    if (write(file, buffer, length + 1) != length + 1) {
        perror("Failed to write data");
        return -1;
    }
    
    return 0;
}

int main() {
    int file;
    uint8_t accel_data[6];  // X, Y, Z axes (2 bytes each)
    
    // Open I2C bus
    file = open(I2C_DEVICE, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C device");
        return 1;
    }
    
    // Set slave address
    if (ioctl(file, I2C_SLAVE, SENSOR_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(file);
        return 1;
    }
    
    // Burst read 6 bytes starting from register 0x3B (ACCEL_XOUT_H)
    if (i2c_burst_read(file, 0x3B, accel_data, 6) == 0) {
        // Combine high and low bytes
        int16_t accel_x = (accel_data[0] << 8) | accel_data[1];
        int16_t accel_y = (accel_data[2] << 8) | accel_data[3];
        int16_t accel_z = (accel_data[4] << 8) | accel_data[5];
        
        printf("Acceleration: X=%d, Y=%d, Z=%d\n", accel_x, accel_y, accel_z);
    }
    
    close(file);
    return 0;
}
```

### Example 2: Arduino/Embedded C++ with Wire Library

```cpp
#include <Wire.h>

#define SENSOR_ADDR 0x68
#define ACCEL_XOUT_H 0x3B

class I2CBurstTransfer {
public:
    void begin() {
        Wire.begin();
        Wire.setClock(400000);  // 400kHz Fast Mode
    }
    
    // Burst read from consecutive registers
    bool burstRead(uint8_t deviceAddr, uint8_t regAddr, 
                   uint8_t *buffer, size_t length) {
        Wire.beginTransmission(deviceAddr);
        Wire.write(regAddr);
        
        if (Wire.endTransmission(false) != 0) {  // Repeated START
            return false;
        }
        
        Wire.requestFrom(deviceAddr, length);
        
        size_t i = 0;
        while (Wire.available() && i < length) {
            buffer[i++] = Wire.read();
        }
        
        return (i == length);
    }
    
    // Burst write to consecutive registers
    bool burstWrite(uint8_t deviceAddr, uint8_t regAddr, 
                    const uint8_t *data, size_t length) {
        Wire.beginTransmission(deviceAddr);
        Wire.write(regAddr);
        
        for (size_t i = 0; i < length; i++) {
            Wire.write(data[i]);
        }
        
        return (Wire.endTransmission() == 0);
    }
    
    // Read 3-axis accelerometer data in one burst
    bool readAccelerometer(int16_t &x, int16_t &y, int16_t &z) {
        uint8_t data[6];
        
        if (!burstRead(SENSOR_ADDR, ACCEL_XOUT_H, data, 6)) {
            return false;
        }
        
        x = (data[0] << 8) | data[1];
        y = (data[2] << 8) | data[3];
        z = (data[4] << 8) | data[5];
        
        return true;
    }
};

I2CBurstTransfer i2c;

void setup() {
    Serial.begin(115200);
    i2c.begin();
}

void loop() {
    int16_t ax, ay, az;
    
    if (i2c.readAccelerometer(ax, ay, az)) {
        Serial.print("Accel X: "); Serial.print(ax);
        Serial.print(" Y: "); Serial.print(ay);
        Serial.print(" Z: "); Serial.println(az);
    }
    
    delay(100);
}
```

### Example 3: STM32 HAL Burst Transfer

```c
#include "stm32f4xx_hal.h"

#define SENSOR_ADDR (0x68 << 1)  // 7-bit address shifted for HAL
#define TIMEOUT_MS 100

extern I2C_HandleTypeDef hi2c1;

// Burst read using HAL
HAL_StatusTypeDef i2c_burst_read_hal(uint8_t reg_addr, uint8_t *data, 
                                      uint16_t length) {
    HAL_StatusTypeDef status;
    
    // Send register address using Memory Read function
    status = HAL_I2C_Mem_Read(&hi2c1, SENSOR_ADDR, reg_addr, 
                              I2C_MEMADD_SIZE_8BIT, data, length, TIMEOUT_MS);
    
    return status;
}

// Burst write using HAL
HAL_StatusTypeDef i2c_burst_write_hal(uint8_t reg_addr, uint8_t *data, 
                                       uint16_t length) {
    HAL_StatusTypeDef status;
    
    status = HAL_I2C_Mem_Write(&hi2c1, SENSOR_ADDR, reg_addr, 
                               I2C_MEMADD_SIZE_8BIT, data, length, TIMEOUT_MS);
    
    return status;
}

// Example: Read sensor FIFO in burst mode
void read_sensor_fifo(void) {
    uint8_t fifo_data[128];
    HAL_StatusTypeDef status;
    
    status = i2c_burst_read_hal(0x74, fifo_data, sizeof(fifo_data));
    
    if (status == HAL_OK) {
        // Process FIFO data
        for (int i = 0; i < sizeof(fifo_data); i++) {
            printf("FIFO[%d] = 0x%02X\n", i, fifo_data[i]);
        }
    }
}
```

## Rust Examples

### Example 1: Using linux-embedded-hal

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::error::Error;

const SENSOR_ADDR: u8 = 0x68;
const ACCEL_XOUT_H: u8 = 0x3B;

struct AccelData {
    x: i16,
    y: i16,
    z: i16,
}

fn burst_read(i2c: &mut I2cdev, reg_addr: u8, length: usize) 
    -> Result<Vec<u8>, Box<dyn Error>> {
    let mut buffer = vec![0u8; length];
    
    // Write register address, then read multiple bytes
    i2c.write_read(SENSOR_ADDR, &[reg_addr], &mut buffer)?;
    
    Ok(buffer)
}

fn burst_write(i2c: &mut I2cdev, reg_addr: u8, data: &[u8]) 
    -> Result<(), Box<dyn Error>> {
    let mut buffer = Vec::with_capacity(data.len() + 1);
    buffer.push(reg_addr);
    buffer.extend_from_slice(data);
    
    i2c.write(SENSOR_ADDR, &buffer)?;
    
    Ok(())
}

fn read_accelerometer(i2c: &mut I2cdev) -> Result<AccelData, Box<dyn Error>> {
    // Burst read 6 bytes (X, Y, Z high and low bytes)
    let data = burst_read(i2c, ACCEL_XOUT_H, 6)?;
    
    Ok(AccelData {
        x: i16::from_be_bytes([data[0], data[1]]),
        y: i16::from_be_bytes([data[2], data[3]]),
        z: i16::from_be_bytes([data[4], data[5]]),
    })
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut i2c = I2cdev::new("/dev/i2c-1")?;
    
    loop {
        let accel = read_accelerometer(&mut i2c)?;
        println!("Acceleration: X={}, Y={}, Z={}", accel.x, accel.y, accel.z);
        
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
}
```

### Example 2: Embedded Rust with embedded-hal

```rust
use embedded_hal::i2c::{I2c, SevenBitAddress};

pub struct Mpu6050<I2C> {
    i2c: I2C,
    address: SevenBitAddress,
}

impl<I2C, E> Mpu6050<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C, address: SevenBitAddress) -> Self {
        Self { i2c, address }
    }
    
    /// Burst read from consecutive registers
    pub fn burst_read(&mut self, reg: u8, buffer: &mut [u8]) -> Result<(), E> {
        self.i2c.write_read(self.address, &[reg], buffer)
    }
    
    /// Burst write to consecutive registers
    pub fn burst_write(&mut self, reg: u8, data: &[u8]) -> Result<(), E> {
        let mut buffer = [0u8; 32];
        buffer[0] = reg;
        buffer[1..=data.len()].copy_from_slice(data);
        
        self.i2c.write(self.address, &buffer[..=data.len()])
    }
    
    /// Read all accelerometer and gyroscope data in one burst
    pub fn read_all(&mut self) -> Result<SensorData, E> {
        let mut data = [0u8; 14];
        self.burst_read(0x3B, &mut data)?;
        
        Ok(SensorData {
            accel_x: i16::from_be_bytes([data[0], data[1]]),
            accel_y: i16::from_be_bytes([data[2], data[3]]),
            accel_z: i16::from_be_bytes([data[4], data[5]]),
            temp: i16::from_be_bytes([data[6], data[7]]),
            gyro_x: i16::from_be_bytes([data[8], data[9]]),
            gyro_y: i16::from_be_bytes([data[10], data[11]]),
            gyro_z: i16::from_be_bytes([data[12], data[13]]),
        })
    }
}

#[derive(Debug)]
pub struct SensorData {
    pub accel_x: i16,
    pub accel_y: i16,
    pub accel_z: i16,
    pub temp: i16,
    pub gyro_x: i16,
    pub gyro_y: i16,
    pub gyro_z: i16,
}
```

### Example 3: EEPROM Burst Transfer with Page Writes

```rust
use embedded_hal::i2c::I2c;

const EEPROM_ADDR: u8 = 0x50;
const PAGE_SIZE: usize = 32;  // Typical EEPROM page size

pub struct Eeprom<I2C> {
    i2c: I2C,
}

impl<I2C, E> Eeprom<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C) -> Self {
        Self { i2c }
    }
    
    /// Burst read from EEPROM
    pub fn read(&mut self, address: u16, buffer: &mut [u8]) -> Result<(), E> {
        let addr_bytes = address.to_be_bytes();
        self.i2c.write_read(EEPROM_ADDR, &addr_bytes, buffer)
    }
    
    /// Page write (burst write with page boundary awareness)
    pub fn write_page(&mut self, address: u16, data: &[u8]) -> Result<(), E> {
        if data.len() > PAGE_SIZE {
            return Err(/* error handling */);
        }
        
        let mut buffer = [0u8; PAGE_SIZE + 2];
        buffer[0..2].copy_from_slice(&address.to_be_bytes());
        buffer[2..2 + data.len()].copy_from_slice(data);
        
        self.i2c.write(EEPROM_ADDR, &buffer[..2 + data.len()])
    }
    
    /// Write data across multiple pages using burst transfers
    pub fn write(&mut self, mut address: u16, data: &[u8]) -> Result<(), E> {
        let mut offset = 0;
        
        while offset < data.len() {
            // Calculate bytes remaining in current page
            let page_offset = (address as usize) % PAGE_SIZE;
            let bytes_in_page = PAGE_SIZE - page_offset;
            let bytes_to_write = bytes_in_page.min(data.len() - offset);
            
            // Write to current page
            self.write_page(address, &data[offset..offset + bytes_to_write])?;
            
            // Move to next page
            address += bytes_to_write as u16;
            offset += bytes_to_write;
            
            // Wait for write cycle to complete (typically 5ms)
            // In embedded: delay.delay_ms(5);
        }
        
        Ok(())
    }
}
```

## Performance Comparison

**Individual Byte Reads (6 bytes):**
- 6 × (START + Address + Register + ReSTART + Address + Data + STOP)
- Approximately 6 × 50μs = 300μs at 100kHz

**Burst Read (6 bytes):**
- START + Address + Register + ReSTART + Address + 6×Data + STOP
- Approximately 120μs at 100kHz

**Result:** Burst transfers are ~2.5× faster for this example.

## Best Practices

1. **Check device support** - Not all I2C devices support burst transfers; consult the datasheet
2. **Respect page boundaries** - For EEPROMs and flash, writes often must stay within page boundaries
3. **Use appropriate buffer sizes** - Avoid stack overflow with large transfers
4. **Handle repeated START** - Ensure your I2C library supports repeated START conditions
5. **Add timeouts** - Prevent hanging on failed transfers
6. **Verify data integrity** - Consider checksums for critical data
7. **Mind byte order** - Multi-byte values often use big-endian ordering

Burst transfers are essential for efficient I2C communication, especially when dealing with sensors, displays, and memory devices that provide sequential data.