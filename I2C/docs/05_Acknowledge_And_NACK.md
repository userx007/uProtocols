# I2C Acknowledge and NACK: Detailed Guide

## Overview

The Acknowledge (ACK) and Not-Acknowledge (NACK) mechanism is fundamental to I2C communication. It provides a way for the receiver to signal whether it successfully received data and is ready for more, creating a robust error detection system at the bit level.

## ACK/NACK Bit Mechanics

### How It Works

After every 8 bits of data transmitted on the I2C bus, the transmitter releases the SDA line, and the receiver takes control for one clock pulse (the 9th clock cycle) to send an acknowledgment bit:

- **ACK (0)**: Receiver pulls SDA LOW → "I received the data successfully"
- **NACK (1)**: Receiver leaves SDA HIGH → "I didn't receive the data" or "I'm done receiving"

### Timing Diagram

```
Clock:  ___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___
Data:   D7  D6  D5  D4  D3  D2  D1  D0  ACK/NACK
SDA:    ___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\_______
                                                                        ^
                                                                    9th clock
                                                                   (ACK bit)
```

### When ACK is Sent

- After receiving a slave address (if the slave recognizes its address)
- After receiving each data byte during a write operation
- After sending each data byte during a read operation (master sends ACK)

### When NACK is Sent

- Slave doesn't recognize its address
- Slave's receive buffer is full
- Slave encountered an error
- Master signals end of read operation (after last byte)

## Error Detection

The ACK/NACK mechanism provides immediate error detection:

1. **Address NACK**: Slave doesn't exist or is busy
2. **Data NACK**: Transmission error, buffer full, or protocol violation
3. **Missing ACK**: Bus fault or timing issue

## C/C++ Code Examples

### Example 1: Bit-Banged I2C with ACK/NACK Handling

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction (platform-specific)
#define SDA_PIN 2
#define SCL_PIN 3

void sda_high(void);
void sda_low(void);
void scl_high(void);
void scl_low(void);
uint8_t sda_read(void);
void delay_us(uint32_t us);

// I2C timing (adjust for your clock speed)
#define I2C_DELAY_US 5  // ~100kHz

// Set SDA as output and pull low
void sda_low(void) {
    // Set pin as output, drive low
    // Platform-specific implementation
}

// Set SDA as input (pulled high by external resistor)
void sda_high(void) {
    // Set pin as input or output high
    // Platform-specific implementation
}

// Read SDA state
uint8_t sda_read(void) {
    // Return pin state (0 or 1)
    // Platform-specific implementation
    return 0; // Placeholder
}

void scl_high(void) {
    // Set SCL high
    // Platform-specific implementation
}

void scl_low(void) {
    // Set SCL low
    // Platform-specific implementation
}

void delay_us(uint32_t us) {
    // Delay function
    // Platform-specific implementation
}

// Send START condition
void i2c_start(void) {
    sda_high();
    scl_high();
    delay_us(I2C_DELAY_US);
    sda_low();
    delay_us(I2C_DELAY_US);
    scl_low();
    delay_us(I2C_DELAY_US);
}

// Send STOP condition
void i2c_stop(void) {
    sda_low();
    delay_us(I2C_DELAY_US);
    scl_high();
    delay_us(I2C_DELAY_US);
    sda_high();
    delay_us(I2C_DELAY_US);
}

// Write a byte and return ACK/NACK status
// Returns: true if ACK received, false if NACK
bool i2c_write_byte(uint8_t data) {
    // Send 8 data bits, MSB first
    for (int i = 7; i >= 0; i--) {
        scl_low();
        delay_us(I2C_DELAY_US);
        
        if (data & (1 << i)) {
            sda_high();
        } else {
            sda_low();
        }
        
        delay_us(I2C_DELAY_US);
        scl_high();
        delay_us(I2C_DELAY_US);
    }
    
    // Release SDA for ACK/NACK
    scl_low();
    sda_high();  // Release the line
    delay_us(I2C_DELAY_US);
    
    // Read ACK/NACK bit (9th clock)
    scl_high();
    delay_us(I2C_DELAY_US);
    
    bool ack = (sda_read() == 0);  // ACK = 0, NACK = 1
    
    scl_low();
    delay_us(I2C_DELAY_US);
    
    return ack;
}

// Read a byte and send ACK or NACK
uint8_t i2c_read_byte(bool send_ack) {
    uint8_t data = 0;
    
    sda_high();  // Release SDA so slave can control it
    
    // Read 8 data bits
    for (int i = 7; i >= 0; i--) {
        scl_low();
        delay_us(I2C_DELAY_US);
        scl_high();
        delay_us(I2C_DELAY_US);
        
        if (sda_read()) {
            data |= (1 << i);
        }
    }
    
    // Send ACK or NACK
    scl_low();
    delay_us(I2C_DELAY_US);
    
    if (send_ack) {
        sda_low();   // Pull low for ACK
    } else {
        sda_high();  // Leave high for NACK
    }
    
    delay_us(I2C_DELAY_US);
    scl_high();
    delay_us(I2C_DELAY_US);
    scl_low();
    delay_us(I2C_DELAY_US);
    
    sda_high();  // Release SDA
    
    return data;
}

// Write data to I2C device with error checking
typedef enum {
    I2C_SUCCESS = 0,
    I2C_ERROR_ADDR_NACK,
    I2C_ERROR_DATA_NACK,
    I2C_ERROR_BUS_FAULT
} i2c_status_t;

i2c_status_t i2c_write_data(uint8_t slave_addr, uint8_t* data, size_t len) {
    i2c_start();
    
    // Send slave address with write bit (0)
    if (!i2c_write_byte(slave_addr << 1)) {
        i2c_stop();
        return I2C_ERROR_ADDR_NACK;
    }
    
    // Send data bytes
    for (size_t i = 0; i < len; i++) {
        if (!i2c_write_byte(data[i])) {
            i2c_stop();
            return I2C_ERROR_DATA_NACK;
        }
    }
    
    i2c_stop();
    return I2C_SUCCESS;
}

// Read data from I2C device
i2c_status_t i2c_read_data(uint8_t slave_addr, uint8_t* buffer, size_t len) {
    if (len == 0) return I2C_SUCCESS;
    
    i2c_start();
    
    // Send slave address with read bit (1)
    if (!i2c_write_byte((slave_addr << 1) | 1)) {
        i2c_stop();
        return I2C_ERROR_ADDR_NACK;
    }
    
    // Read data bytes
    for (size_t i = 0; i < len; i++) {
        // Send ACK for all bytes except the last one
        bool send_ack = (i < len - 1);
        buffer[i] = i2c_read_byte(send_ack);
    }
    
    i2c_stop();
    return I2C_SUCCESS;
}
```

### Example 2: Using STM32 HAL with ACK/NACK Error Handling

```c
#include "stm32f4xx_hal.h"

// I2C handle (configured elsewhere)
extern I2C_HandleTypeDef hi2c1;

#define DEVICE_ADDR 0x50  // EEPROM address

typedef enum {
    I2C_OK = 0,
    I2C_ADDR_NACK,
    I2C_DATA_NACK,
    I2C_TIMEOUT,
    I2C_BUS_ERROR
} I2C_Result;

// Convert HAL status to our error codes
I2C_Result convert_hal_status(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK:
            return I2C_OK;
        case HAL_TIMEOUT:
            return I2C_TIMEOUT;
        case HAL_ERROR:
            // Check specific error flags
            if (hi2c1.ErrorCode & HAL_I2C_ERROR_AF) {
                return I2C_ADDR_NACK;  // Acknowledge failure
            }
            return I2C_BUS_ERROR;
        default:
            return I2C_BUS_ERROR;
    }
}

// Write with ACK checking
I2C_Result i2c_write_with_ack_check(uint8_t addr, uint8_t* data, uint16_t len) {
    HAL_StatusTypeDef status;
    
    // HAL automatically checks for ACK after address and each byte
    status = HAL_I2C_Master_Transmit(&hi2c1, addr << 1, data, len, 100);
    
    return convert_hal_status(status);
}

// Read with proper ACK/NACK handling
I2C_Result i2c_read_with_ack_check(uint8_t addr, uint8_t* buffer, uint16_t len) {
    HAL_StatusTypeDef status;
    
    // HAL automatically sends ACK for all bytes except last (sends NACK for last)
    status = HAL_I2C_Master_Receive(&hi2c1, addr << 1, buffer, len, 100);
    
    return convert_hal_status(status);
}

// Write to register with error recovery
I2C_Result write_register_with_retry(uint8_t dev_addr, uint8_t reg_addr, 
                                     uint8_t value, uint8_t max_retries) {
    uint8_t data[2] = {reg_addr, value};
    I2C_Result result;
    
    for (uint8_t retry = 0; retry < max_retries; retry++) {
        result = i2c_write_with_ack_check(dev_addr, data, 2);
        
        if (result == I2C_OK) {
            return I2C_OK;
        }
        
        // Handle different error types
        if (result == I2C_ADDR_NACK) {
            // Device not responding, wait and retry
            HAL_Delay(10);
        } else if (result == I2C_DATA_NACK) {
            // Data not accepted, device might be busy
            HAL_Delay(5);
        } else {
            // Bus error, try to recover
            HAL_I2C_DeInit(&hi2c1);
            HAL_I2C_Init(&hi2c1);
            HAL_Delay(1);
        }
    }
    
    return result;
}

// Example: EEPROM write with polling
I2C_Result eeprom_write_byte(uint16_t mem_addr, uint8_t data) {
    uint8_t buffer[3];
    buffer[0] = (mem_addr >> 8) & 0xFF;  // Address high byte
    buffer[1] = mem_addr & 0xFF;          // Address low byte
    buffer[2] = data;                     // Data byte
    
    I2C_Result result = i2c_write_with_ack_check(DEVICE_ADDR, buffer, 3);
    
    if (result != I2C_OK) {
        return result;
    }
    
    // Poll for write completion (EEPROM sends NACK while writing)
    uint8_t poll_count = 0;
    while (poll_count < 50) {  // Max 50ms timeout
        HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c1, 
                                                         DEVICE_ADDR << 1, 
                                                         1, 1);
        if (status == HAL_OK) {
            return I2C_OK;  // Device responded with ACK
        }
        HAL_Delay(1);
        poll_count++;
    }
    
    return I2C_TIMEOUT;
}
```

## Rust Code Examples

### Example 1: Embedded HAL I2C with ACK/NACK Handling

```rust
#![no_std]

use embedded_hal::blocking::i2c::{Read, Write, WriteRead};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    AddressNack,    // Slave didn't acknowledge address
    DataNack,       // Slave didn't acknowledge data
    Timeout,        // Operation timed out
    BusError,       // Bus arbitration or other error
}

// Wrapper around embedded-hal I2C to provide better error information
pub struct I2cDevice<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> I2cDevice<I2C>
where
    I2C: Write<Error = E> + Read<Error = E> + WriteRead<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }

    // Write data with ACK checking
    pub fn write(&mut self, data: &[u8]) -> Result<(), I2cError> {
        self.i2c
            .write(self.address, data)
            .map_err(|_| I2cError::DataNack)
    }

    // Read data (master sends ACK for all but last byte)
    pub fn read(&mut self, buffer: &mut [u8]) -> Result<(), I2cError> {
        self.i2c
            .read(self.address, buffer)
            .map_err(|_| I2cError::DataNack)
    }

    // Write then read (repeated start)
    pub fn write_read(&mut self, write_data: &[u8], read_buffer: &mut [u8]) 
        -> Result<(), I2cError> {
        self.i2c
            .write_read(self.address, write_data, read_buffer)
            .map_err(|_| I2cError::DataNack)
    }

    // Write to register with retry logic
    pub fn write_register_retry(&mut self, reg: u8, value: u8, max_retries: u8) 
        -> Result<(), I2cError> {
        let data = [reg, value];
        
        for attempt in 0..max_retries {
            match self.write(&data) {
                Ok(_) => return Ok(()),
                Err(e) => {
                    if attempt == max_retries - 1 {
                        return Err(e);
                    }
                    // Simple delay (implementation depends on platform)
                    // delay_ms(10);
                }
            }
        }
        
        Err(I2cError::Timeout)
    }

    // Read register with error handling
    pub fn read_register(&mut self, reg: u8) -> Result<u8, I2cError> {
        let mut buffer = [0u8; 1];
        self.write_read(&[reg], &mut buffer)?;
        Ok(buffer[0])
    }

    // Read multiple registers
    pub fn read_registers(&mut self, start_reg: u8, buffer: &mut [u8]) 
        -> Result<(), I2cError> {
        self.write_read(&[start_reg], buffer)
    }
}

// Example device driver: MPU6050 accelerometer/gyroscope
pub struct Mpu6050<I2C> {
    device: I2cDevice<I2C>,
}

impl<I2C, E> Mpu6050<I2C>
where
    I2C: Write<Error = E> + Read<Error = E> + WriteRead<Error = E>,
{
    const DEFAULT_ADDR: u8 = 0x68;
    const REG_WHO_AM_I: u8 = 0x75;
    const REG_PWR_MGMT_1: u8 = 0x6B;
    const REG_ACCEL_XOUT_H: u8 = 0x3B;

    pub fn new(i2c: I2C) -> Self {
        Self {
            device: I2cDevice::new(i2c, Self::DEFAULT_ADDR),
        }
    }

    // Initialize device with ACK checking
    pub fn init(&mut self) -> Result<(), I2cError> {
        // Check device ID
        let who_am_i = self.device.read_register(Self::REG_WHO_AM_I)?;
        if who_am_i != 0x68 {
            return Err(I2cError::AddressNack);
        }

        // Wake up device (clear sleep bit)
        self.device.write_register_retry(Self::REG_PWR_MGMT_1, 0x00, 3)?;

        Ok(())
    }

    // Read accelerometer data
    pub fn read_accel(&mut self) -> Result<(i16, i16, i16), I2cError> {
        let mut buffer = [0u8; 6];
        
        // Read 6 bytes starting from ACCEL_XOUT_H
        // Master will send ACK after each byte except the last
        self.device.read_registers(Self::REG_ACCEL_XOUT_H, &mut buffer)?;

        let x = i16::from_be_bytes([buffer[0], buffer[1]]);
        let y = i16::from_be_bytes([buffer[2], buffer[3]]);
        let z = i16::from_be_bytes([buffer[4], buffer[5]]);

        Ok((x, y, z))
    }
}
```

### Example 2: Bit-Banged I2C in Rust

```rust
#![no_std]

use embedded_hal::digital::v2::{InputPin, OutputPin};

pub struct BitBangI2c<SDA, SCL> {
    sda: SDA,
    scl: SCL,
    delay_us: u32,
}

impl<SDA, SCL, E> BitBangI2c<SDA, SCL>
where
    SDA: InputPin<Error = E> + OutputPin<Error = E>,
    SCL: OutputPin<Error = E>,
{
    pub fn new(sda: SDA, scl: SCL, frequency_khz: u32) -> Self {
        let delay_us = 1_000_000 / (frequency_khz * 1000) / 2;
        Self { sda, scl, delay_us }
    }

    fn delay(&self) {
        // Platform-specific delay implementation
        // cortex_m::asm::delay(cycles);
    }

    fn sda_high(&mut self) -> Result<(), E> {
        self.sda.set_high()
    }

    fn sda_low(&mut self) -> Result<(), E> {
        self.sda.set_low()
    }

    fn scl_high(&mut self) -> Result<(), E> {
        self.scl.set_high()
    }

    fn scl_low(&mut self) -> Result<(), E> {
        self.scl.set_low()
    }

    fn sda_read(&self) -> Result<bool, E> {
        self.sda.is_high()
    }

    // Generate START condition
    pub fn start(&mut self) -> Result<(), E> {
        self.sda_high()?;
        self.scl_high()?;
        self.delay();
        self.sda_low()?;
        self.delay();
        self.scl_low()?;
        self.delay();
        Ok(())
    }

    // Generate STOP condition
    pub fn stop(&mut self) -> Result<(), E> {
        self.sda_low()?;
        self.delay();
        self.scl_high()?;
        self.delay();
        self.sda_high()?;
        self.delay();
        Ok(())
    }

    // Write a byte and check for ACK
    pub fn write_byte(&mut self, data: u8) -> Result<bool, E> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            self.scl_low()?;
            self.delay();

            if (data & (1 << i)) != 0 {
                self.sda_high()?;
            } else {
                self.sda_low()?;
            }

            self.delay();
            self.scl_high()?;
            self.delay();
        }

        // Release SDA for ACK/NACK
        self.scl_low()?;
        self.sda_high()?;
        self.delay();

        // Read ACK bit (9th clock)
        self.scl_high()?;
        self.delay();

        let ack = !self.sda_read()?; // ACK = 0, NACK = 1

        self.scl_low()?;
        self.delay();

        Ok(ack)
    }

    // Read a byte and send ACK or NACK
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8, E> {
        let mut data = 0u8;

        self.sda_high()?; // Release SDA

        // Read 8 bits
        for i in (0..8).rev() {
            self.scl_low()?;
            self.delay();
            self.scl_high()?;
            self.delay();

            if self.sda_read()? {
                data |= 1 << i;
            }
        }

        // Send ACK or NACK
        self.scl_low()?;
        self.delay();

        if send_ack {
            self.sda_low()?; // Pull low for ACK
        } else {
            self.sda_high()?; // Leave high for NACK
        }

        self.delay();
        self.scl_high()?;
        self.delay();
        self.scl_low()?;
        self.delay();

        self.sda_high()?; // Release SDA

        Ok(data)
    }

    // Write data with full error handling
    pub fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        self.start().map_err(|_| I2cError::BusError)?;

        // Send address with write bit
        if !self.write_byte(addr << 1).map_err(|_| I2cError::BusError)? {
            self.stop().ok();
            return Err(I2cError::AddressNack);
        }

        // Send data bytes
        for &byte in data {
            if !self.write_byte(byte).map_err(|_| I2cError::BusError)? {
                self.stop().ok();
                return Err(I2cError::DataNack);
            }
        }

        self.stop().map_err(|_| I2cError::BusError)?;
        Ok(())
    }

    // Read data with proper ACK/NACK
    pub fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        if buffer.is_empty() {
            return Ok(());
        }

        self.start().map_err(|_| I2cError::BusError)?;

        // Send address with read bit
        if !self.write_byte((addr << 1) | 1).map_err(|_| I2cError::BusError)? {
            self.stop().ok();
            return Err(I2cError::AddressNack);
        }

        // Read data bytes
        let last_idx = buffer.len() - 1;
        for (i, byte) in buffer.iter_mut().enumerate() {
            let send_ack = i < last_idx;
            *byte = self.read_byte(send_ack).map_err(|_| I2cError::BusError)?;
        }

        self.stop().map_err(|_| I2cError::BusError)?;
        Ok(())
    }
}
```

## Key Takeaways

1. **ACK/NACK provides immediate feedback** after every byte, enabling robust error detection
2. **ACK = 0 (LOW)**, NACK = 1 (HIGH) on the 9th clock pulse
3. **Master sends NACK** after the last read byte to signal end of transfer
4. **Address NACK** means the device isn't responding; data NACK indicates transmission problems
5. Always implement **retry logic** and **error recovery** for production systems
6. Different HAL libraries handle ACK/NACK checking automatically, but understanding the underlying mechanism helps with debugging