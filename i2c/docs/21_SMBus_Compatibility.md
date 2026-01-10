# SMBus Compatibility: Understanding System Management Bus and I2C Interoperability

## Overview

SMBus (System Management Bus) is a two-wire communication protocol derived from I2C, primarily used for system management and monitoring in computers and embedded systems. While SMBus and I2C share the same physical layer, they have important differences in timing, electrical specifications, and protocol requirements that affect compatibility.

## Key Differences Between I2C and SMBus

### 1. **Timing Requirements**

**I2C** has flexible timing:
- Clock frequency: typically 100 kHz (Standard), 400 kHz (Fast), up to 3.4 MHz (High-speed)
- No timeout requirements

**SMBus** has stricter timing constraints:
- Clock frequency: 10 kHz to 100 kHz (typically fixed at 100 kHz)
- **Timeout mechanism**: 35 ms maximum clock low period
- **Minimum clock frequency**: prevents devices from stalling the bus indefinitely

### 2. **Voltage Levels**

- **I2C**: Logic high can be up to VDD (commonly 5V or 3.3V)
- **SMBus**: Fixed logic levels (typically 3.3V max, 0.8V min for logic low, 2.1V min for logic high)

### 3. **Protocol Features**

SMBus adds several features not present in basic I2C:
- **Packet Error Checking (PEC)**: Optional CRC-8 checksum
- **Address Resolution Protocol (ARP)**: Dynamic address assignment
- **Alert Response Address (ARA)**: Interrupt-like notification mechanism

## Packet Error Checking (PEC)

PEC is an optional 8-bit CRC that provides error detection for SMBus transactions. The polynomial used is: **x^8 + x^2 + x + 1** (0x07).

### How PEC Works

1. CRC is calculated over all bytes in the transaction (including address and data)
2. The CRC byte is appended after the last data byte
3. The receiver calculates its own CRC and compares it with the received PEC byte

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// CRC-8 polynomial for SMBus PEC: x^8 + x^2 + x + 1
#define SMBUS_POLYNOMIAL 0x07

/**
 * Calculate SMBus Packet Error Check (PEC)
 * Uses CRC-8 with polynomial 0x07
 */
uint8_t smbus_calculate_pec(uint8_t *data, size_t length) {
    uint8_t crc = 0;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SMBUS_POLYNOMIAL;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

/**
 * SMBus write byte with PEC
 */
bool smbus_write_byte_pec(uint8_t i2c_fd, uint8_t device_addr, 
                          uint8_t reg_addr, uint8_t value) {
    uint8_t buffer[4];
    buffer[0] = device_addr << 1;      // Write address
    buffer[1] = reg_addr;               // Register/command
    buffer[2] = value;                  // Data byte
    buffer[3] = smbus_calculate_pec(buffer, 3);  // PEC
    
    // I2C write transaction
    // Start condition
    if (i2c_start(i2c_fd) < 0) return false;
    
    // Send address + write bit
    if (i2c_write_byte(i2c_fd, device_addr << 1) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Send register address
    if (i2c_write_byte(i2c_fd, reg_addr) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Send data
    if (i2c_write_byte(i2c_fd, value) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Send PEC
    if (i2c_write_byte(i2c_fd, buffer[3]) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Stop condition
    i2c_stop(i2c_fd);
    return true;
}

/**
 * SMBus read byte with PEC
 */
bool smbus_read_byte_pec(uint8_t i2c_fd, uint8_t device_addr, 
                         uint8_t reg_addr, uint8_t *value) {
    uint8_t buffer[5];
    uint8_t received_pec, calculated_pec;
    
    // Write phase: send register address
    if (i2c_start(i2c_fd) < 0) return false;
    if (i2c_write_byte(i2c_fd, device_addr << 1) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    if (i2c_write_byte(i2c_fd, reg_addr) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Repeated start for read phase
    if (i2c_start(i2c_fd) < 0) return false;
    if (i2c_write_byte(i2c_fd, (device_addr << 1) | 0x01) < 0) {
        i2c_stop(i2c_fd);
        return false;
    }
    
    // Read data byte
    *value = i2c_read_byte(i2c_fd, true);  // ACK
    
    // Read PEC byte
    received_pec = i2c_read_byte(i2c_fd, false);  // NACK
    
    i2c_stop(i2c_fd);
    
    // Calculate PEC for verification
    buffer[0] = device_addr << 1;           // Write address
    buffer[1] = reg_addr;                    // Command
    buffer[2] = (device_addr << 1) | 0x01;  // Read address
    buffer[3] = *value;                      // Data
    
    calculated_pec = smbus_calculate_pec(buffer, 4);
    
    // Verify PEC
    if (calculated_pec != received_pec) {
        return false;  // PEC error
    }
    
    return true;
}
```

### C++ Implementation with Linux SMBus Support

```cpp
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

class SMBusDevice {
private:
    int fd;
    uint8_t device_address;
    
    uint8_t calculatePEC(const std::vector<uint8_t>& data) {
        uint8_t crc = 0;
        
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int bit = 0; bit < 8; bit++) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0x07;
                } else {
                    crc = crc << 1;
                }
            }
        }
        
        return crc;
    }
    
public:
    SMBusDevice(const char* bus, uint8_t addr) : device_address(addr) {
        fd = open(bus, O_RDWR);
        if (fd < 0) {
            throw std::runtime_error("Failed to open I2C bus");
        }
        
        if (ioctl(fd, I2C_SLAVE, device_address) < 0) {
            close(fd);
            throw std::runtime_error("Failed to set I2C slave address");
        }
        
        // Enable PEC if supported
        ioctl(fd, I2C_PEC, 1);
    }
    
    ~SMBusDevice() {
        if (fd >= 0) {
            close(fd);
        }
    }
    
    // SMBus Quick Command
    bool quickCommand(bool value) {
        struct i2c_smbus_ioctl_data args;
        args.read_write = value ? I2C_SMBUS_READ : I2C_SMBUS_WRITE;
        args.command = 0;
        args.size = I2C_SMBUS_QUICK;
        args.data = nullptr;
        
        return ioctl(fd, I2C_SMBUS, &args) >= 0;
    }
    
    // SMBus Write Byte
    bool writeByte(uint8_t command, uint8_t value) {
        return i2c_smbus_write_byte_data(fd, command, value) >= 0;
    }
    
    // SMBus Read Byte
    int readByte(uint8_t command) {
        return i2c_smbus_read_byte_data(fd, command);
    }
    
    // SMBus Write Word
    bool writeWord(uint8_t command, uint16_t value) {
        return i2c_smbus_write_word_data(fd, command, value) >= 0;
    }
    
    // SMBus Read Word
    int readWord(uint8_t command) {
        return i2c_smbus_read_word_data(fd, command);
    }
    
    // SMBus Block Write with PEC
    bool blockWrite(uint8_t command, const std::vector<uint8_t>& data) {
        if (data.size() > 32) return false;
        
        return i2c_smbus_write_block_data(fd, command, 
                                         data.size(), 
                                         data.data()) >= 0;
    }
    
    // SMBus Block Read with PEC
    std::vector<uint8_t> blockRead(uint8_t command) {
        uint8_t buffer[32];
        int length = i2c_smbus_read_block_data(fd, command, buffer);
        
        if (length < 0) {
            throw std::runtime_error("Block read failed");
        }
        
        return std::vector<uint8_t>(buffer, buffer + length);
    }
};
```

## Timeout Mechanisms

SMBus requires timeout handling to prevent bus lockup. The specification mandates:
- **TTIMEOUT**: 35 ms maximum for clock held low
- **TLOW:SEXT**: 25 ms maximum for slave clock stretching

### C Implementation with Timeout

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define SMBUS_TIMEOUT_MS 35

typedef struct {
    int scl_pin;
    int sda_pin;
    uint32_t timeout_ms;
} smbus_config_t;

/**
 * Get current time in milliseconds
 */
uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

/**
 * Wait for clock line to go high with timeout
 */
bool smbus_wait_scl_high(smbus_config_t *config) {
    uint32_t start_time = get_time_ms();
    
    while (!gpio_read(config->scl_pin)) {
        if ((get_time_ms() - start_time) > config->timeout_ms) {
            return false;  // Timeout occurred
        }
    }
    
    return true;
}

/**
 * SMBus write with timeout protection
 */
bool smbus_write_byte_timeout(smbus_config_t *config, 
                              uint8_t device_addr, 
                              uint8_t reg_addr, 
                              uint8_t value) {
    uint32_t start_time = get_time_ms();
    
    // Start condition
    if (!i2c_start_condition(config)) return false;
    
    // Send device address
    if (!i2c_write_byte_with_timeout(config, device_addr << 1)) {
        i2c_stop_condition(config);
        return false;
    }
    
    // Check for timeout during entire transaction
    if ((get_time_ms() - start_time) > SMBUS_TIMEOUT_MS) {
        i2c_stop_condition(config);
        return false;
    }
    
    // Send register address
    if (!i2c_write_byte_with_timeout(config, reg_addr)) {
        i2c_stop_condition(config);
        return false;
    }
    
    // Send data
    if (!i2c_write_byte_with_timeout(config, value)) {
        i2c_stop_condition(config);
        return false;
    }
    
    // Stop condition
    i2c_stop_condition(config);
    
    return true;
}

/**
 * Write single byte with timeout
 */
bool i2c_write_byte_with_timeout(smbus_config_t *config, uint8_t byte) {
    for (int bit = 7; bit >= 0; bit--) {
        // Set data line
        gpio_write(config->sda_pin, (byte >> bit) & 0x01);
        
        // Wait for clock high (with timeout)
        if (!smbus_wait_scl_high(config)) {
            return false;  // Timeout
        }
        
        // Clock low
        gpio_write(config->scl_pin, 0);
    }
    
    // Wait for ACK/NACK
    gpio_set_input(config->sda_pin);
    
    if (!smbus_wait_scl_high(config)) {
        return false;
    }
    
    bool ack = !gpio_read(config->sda_pin);
    gpio_write(config->scl_pin, 0);
    
    return ack;
}
```

## Rust Implementation

```rust
use std::io;
use std::time::{Duration, Instant};

const SMBUS_POLYNOMIAL: u8 = 0x07;
const SMBUS_TIMEOUT_MS: u64 = 35;

/// Calculate SMBus PEC (Packet Error Check)
fn calculate_pec(data: &[u8]) -> u8 {
    let mut crc: u8 = 0;
    
    for &byte in data {
        crc ^= byte;
        
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ SMBUS_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    
    crc
}

/// SMBus transaction types
#[derive(Debug, Clone, Copy)]
pub enum SMBusCommand {
    QuickCommand,
    SendByte,
    ReceiveByte,
    WriteByte,
    ReadByte,
    WriteWord,
    ReadWord,
    BlockWrite,
    BlockRead,
}

/// SMBus device with PEC and timeout support
pub struct SMBusDevice {
    address: u8,
    use_pec: bool,
    timeout: Duration,
}

impl SMBusDevice {
    pub fn new(address: u8) -> Self {
        Self {
            address,
            use_pec: false,
            timeout: Duration::from_millis(SMBUS_TIMEOUT_MS),
        }
    }
    
    pub fn with_pec(mut self, enable: bool) -> Self {
        self.use_pec = enable;
        self
    }
    
    pub fn with_timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }
    
    /// Write byte with optional PEC
    pub fn write_byte(&self, register: u8, value: u8) -> io::Result<()> {
        let mut buffer = vec![
            self.address << 1,  // Write address
            register,            // Command/register
            value,               // Data
        ];
        
        if self.use_pec {
            let pec = calculate_pec(&buffer);
            buffer.push(pec);
        }
        
        // Perform I2C transaction with timeout
        self.write_with_timeout(&buffer[1..])?;
        
        Ok(())
    }
    
    /// Read byte with optional PEC
    pub fn read_byte(&self, register: u8) -> io::Result<u8> {
        let mut write_buffer = vec![
            self.address << 1,  // Write address
            register,            // Command
        ];
        
        // Write register address
        self.write_with_timeout(&write_buffer[1..])?;
        
        // Read data
        let mut read_buffer = if self.use_pec {
            vec![0u8; 2]  // Data + PEC
        } else {
            vec![0u8; 1]  // Data only
        };
        
        self.read_with_timeout(&mut read_buffer)?;
        
        let value = read_buffer[0];
        
        // Verify PEC if enabled
        if self.use_pec {
            let mut pec_buffer = write_buffer;
            pec_buffer.push((self.address << 1) | 0x01);  // Read address
            pec_buffer.push(value);
            
            let calculated_pec = calculate_pec(&pec_buffer);
            let received_pec = read_buffer[1];
            
            if calculated_pec != received_pec {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "PEC verification failed"
                ));
            }
        }
        
        Ok(value)
    }
    
    /// Write word (16-bit) with optional PEC
    pub fn write_word(&self, register: u8, value: u16) -> io::Result<()> {
        let mut buffer = vec![
            self.address << 1,
            register,
            (value & 0xFF) as u8,        // Low byte
            ((value >> 8) & 0xFF) as u8, // High byte
        ];
        
        if self.use_pec {
            let pec = calculate_pec(&buffer);
            buffer.push(pec);
        }
        
        self.write_with_timeout(&buffer[1..])?;
        
        Ok(())
    }
    
    /// Read word (16-bit) with optional PEC
    pub fn read_word(&self, register: u8) -> io::Result<u16> {
        let write_buffer = vec![
            self.address << 1,
            register,
        ];
        
        self.write_with_timeout(&write_buffer[1..])?;
        
        let mut read_buffer = if self.use_pec {
            vec![0u8; 3]  // Low byte + High byte + PEC
        } else {
            vec![0u8; 2]  // Low byte + High byte
        };
        
        self.read_with_timeout(&mut read_buffer)?;
        
        let value = (read_buffer[0] as u16) | ((read_buffer[1] as u16) << 8);
        
        // Verify PEC if enabled
        if self.use_pec {
            let mut pec_buffer = write_buffer;
            pec_buffer.push((self.address << 1) | 0x01);
            pec_buffer.push(read_buffer[0]);
            pec_buffer.push(read_buffer[1]);
            
            let calculated_pec = calculate_pec(&pec_buffer);
            let received_pec = read_buffer[2];
            
            if calculated_pec != received_pec {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "PEC verification failed"
                ));
            }
        }
        
        Ok(value)
    }
    
    /// Block write with optional PEC
    pub fn block_write(&self, register: u8, data: &[u8]) -> io::Result<()> {
        if data.len() > 32 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Block data exceeds 32 bytes"
            ));
        }
        
        let mut buffer = vec![self.address << 1, register, data.len() as u8];
        buffer.extend_from_slice(data);
        
        if self.use_pec {
            let pec = calculate_pec(&buffer);
            buffer.push(pec);
        }
        
        self.write_with_timeout(&buffer[1..])?;
        
        Ok(())
    }
    
    /// Placeholder for low-level write with timeout
    fn write_with_timeout(&self, data: &[u8]) -> io::Result<()> {
        let start = Instant::now();
        
        // Actual I2C write implementation would go here
        // This is a placeholder showing timeout logic
        
        while start.elapsed() < self.timeout {
            // Attempt write operation
            // If successful, return Ok(())
            // If bus busy, continue loop
        }
        
        Err(io::Error::new(io::ErrorKind::TimedOut, "SMBus timeout"))
    }
    
    /// Placeholder for low-level read with timeout
    fn read_with_timeout(&self, buffer: &mut [u8]) -> io::Result<()> {
        let start = Instant::now();
        
        while start.elapsed() < self.timeout {
            // Attempt read operation
            // If successful, return Ok(())
        }
        
        Err(io::Error::new(io::ErrorKind::TimedOut, "SMBus timeout"))
    }
}

/// Example usage
fn main() -> io::Result<()> {
    let device = SMBusDevice::new(0x50)
        .with_pec(true)
        .with_timeout(Duration::from_millis(50));
    
    // Write byte with PEC
    device.write_byte(0x10, 0xAB)?;
    
    // Read byte with PEC verification
    let value = device.read_byte(0x10)?;
    println!("Read value: 0x{:02X}", value);
    
    // Write word
    device.write_word(0x20, 0x1234)?;
    
    // Read word
    let word = device.read_word(0x20)?;
    println!("Read word: 0x{:04X}", word);
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_pec_calculation() {
        // Test vector from SMBus specification
        let data = vec![0x00, 0x5A, 0x00];
        let pec = calculate_pec(&data);
        assert_eq!(pec, 0x3F);
    }
    
    #[test]
    fn test_pec_read_transaction() {
        let data = vec![0xA0, 0x10, 0xA1, 0x5A];
        let pec = calculate_pec(&data);
        // Verify PEC calculation
        assert_ne!(pec, 0x00);
    }
}
```

## Key Compatibility Considerations

### Making I2C Devices SMBus Compatible

1. **Implement timeouts**: Prevent clock stretching beyond 35ms
2. **Support PEC**: Implement CRC-8 calculation if data integrity is critical
3. **Follow voltage levels**: Ensure compatibility with 3.3V logic
4. **Clock frequency**: Limit to 100 kHz maximum

### Making SMBus Devices I2C Compatible

1. **Disable PEC**: Make PEC optional
2. **Relax timing**: Support variable clock frequencies
3. **Handle missing timeouts**: Don't rely on timeout detection in master

## Summary

SMBus and I2C are closely related but have important differences in timing, error checking, and protocol features. Understanding these differences is crucial for ensuring reliable communication in mixed environments. The PEC mechanism provides valuable error detection, while proper timeout handling prevents bus lockup in real-world systems.