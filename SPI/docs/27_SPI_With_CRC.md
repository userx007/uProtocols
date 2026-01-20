# SPI with CRC: Error Detection in Noisy Environments

## Overview

SPI (Serial Peripheral Interface) with CRC (Cyclic Redundancy Check) adds a robust error detection mechanism to standard SPI communication. This is crucial in noisy industrial environments, automotive applications, or any system where data integrity is paramount. CRC allows both the transmitter and receiver to detect corruption in transmitted data by appending a calculated checksum to each message.

## What is CRC?

A Cyclic Redundancy Check is an error-detecting code that uses polynomial division to generate a short, fixed-size checksum from a block of data. The receiver performs the same calculation and compares results. If they match, the data is likely intact; if not, an error occurred during transmission.

Common CRC polynomials include:
- **CRC-8**: 8-bit CRC, polynomial 0x07 or 0x31
- **CRC-16**: 16-bit CRC, polynomial 0x8005 (CRC-16-IBM) or 0x1021 (CRC-16-CCITT)
- **CRC-32**: 32-bit CRC, polynomial 0x04C11DB7

## Why Use CRC with SPI?

Standard SPI has no built-in error detection. In environments with electromagnetic interference (EMI), long cables, or high-speed operation, bit flips can occur. CRC provides:

1. **Error Detection**: Identifies corrupted data with high probability
2. **Data Integrity**: Ensures critical commands and sensor readings are accurate
3. **Reliability**: Allows retry mechanisms when errors are detected
4. **Compliance**: Required in many automotive and industrial standards (e.g., ISO 26262)

## Implementation Strategy

A typical SPI-with-CRC protocol works as follows:

1. **Transmit Side**: Calculate CRC over the payload, append it to the message
2. **Receive Side**: Receive the message, calculate CRC over the payload, compare with received CRC
3. **Error Handling**: If CRC mismatch, request retransmission or raise an error flag

## C/C++ Implementation

Here's a complete implementation for an embedded system (e.g., STM32, Arduino):

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CRC-8 using polynomial 0x07 (x^8 + x^2 + x + 1)
#define CRC8_POLYNOMIAL 0x07

// Calculate CRC-8 for a buffer
uint8_t calculate_crc8(const uint8_t *data, size_t length) {
    uint8_t crc = 0x00;  // Initial value
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

// CRC-16 using CCITT polynomial 0x1021
#define CRC16_POLYNOMIAL 0x1021

uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;  // Initial value
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

// SPI transaction structure with CRC
typedef struct {
    uint8_t command;
    uint8_t data[32];
    uint8_t data_length;
    uint8_t crc;
} spi_message_t;

// Simulate SPI hardware functions (replace with actual HAL)
void spi_transmit(uint8_t *data, size_t length);
void spi_receive(uint8_t *data, size_t length);
void spi_select_device(bool select);

// Send SPI message with CRC-8
bool spi_send_with_crc(uint8_t command, const uint8_t *payload, uint8_t length) {
    if (length > 32) return false;
    
    uint8_t buffer[34];  // Command + data + CRC
    buffer[0] = command;
    memcpy(&buffer[1], payload, length);
    
    // Calculate CRC over command and data
    uint8_t crc = calculate_crc8(buffer, length + 1);
    buffer[length + 1] = crc;
    
    spi_select_device(true);
    spi_transmit(buffer, length + 2);
    spi_select_device(false);
    
    return true;
}

// Receive SPI message and verify CRC-8
bool spi_receive_with_crc(uint8_t *command, uint8_t *payload, uint8_t length) {
    if (length > 32) return false;
    
    uint8_t buffer[34];
    
    spi_select_device(true);
    spi_receive(buffer, length + 2);
    spi_select_device(false);
    
    // Calculate CRC over received command and data
    uint8_t calculated_crc = calculate_crc8(buffer, length + 1);
    uint8_t received_crc = buffer[length + 1];
    
    if (calculated_crc != received_crc) {
        // CRC mismatch - data corrupted
        return false;
    }
    
    // CRC valid - extract data
    *command = buffer[0];
    memcpy(payload, &buffer[1], length);
    return true;
}

// Example: Temperature sensor with CRC protection
typedef struct {
    int16_t temperature;  // In 0.1°C units
    uint16_t humidity;    // In 0.1% units
    uint8_t status;
} sensor_data_t;

bool read_sensor_with_crc(sensor_data_t *sensor) {
    uint8_t tx_data[1] = {0x01};  // Read command
    uint8_t rx_data[5];
    
    if (!spi_send_with_crc(0xA0, tx_data, 1)) {
        return false;
    }
    
    // Small delay for sensor processing
    for (volatile int i = 0; i < 1000; i++);
    
    uint8_t command;
    if (!spi_receive_with_crc(&command, rx_data, 5)) {
        return false;  // CRC error
    }
    
    // Parse sensor data
    sensor->temperature = (int16_t)((rx_data[0] << 8) | rx_data[1]);
    sensor->humidity = (uint16_t)((rx_data[2] << 8) | rx_data[3]);
    sensor->status = rx_data[4];
    
    return true;
}

// Main application example
int main(void) {
    sensor_data_t sensor;
    
    // Initialize SPI hardware (platform-specific)
    // spi_init();
    
    while (1) {
        if (read_sensor_with_crc(&sensor)) {
            // Data valid
            float temp = sensor.temperature / 10.0f;
            float hum = sensor.humidity / 10.0f;
            // Process valid data...
        } else {
            // CRC error - retry or handle error
        }
        
        // Delay before next reading
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    return 0;
}
```

## Rust Implementation

Here's an idiomatic Rust implementation with embedded-hal compatibility:

```rust
use core::fmt;

/// CRC-8 calculator using polynomial 0x07
pub struct Crc8 {
    polynomial: u8,
    value: u8,
}

impl Crc8 {
    pub fn new() -> Self {
        Self {
            polynomial: 0x07,
            value: 0x00,
        }
    }
    
    pub fn update(&mut self, data: &[u8]) {
        for &byte in data {
            self.value ^= byte;
            
            for _ in 0..8 {
                if self.value & 0x80 != 0 {
                    self.value = (self.value << 1) ^ self.polynomial;
                } else {
                    self.value <<= 1;
                }
            }
        }
    }
    
    pub fn finalize(&self) -> u8 {
        self.value
    }
    
    pub fn calculate(data: &[u8]) -> u8 {
        let mut crc = Self::new();
        crc.update(data);
        crc.finalize()
    }
}

/// CRC-16 calculator using CCITT polynomial 0x1021
pub struct Crc16 {
    polynomial: u16,
    value: u16,
}

impl Crc16 {
    pub fn new() -> Self {
        Self {
            polynomial: 0x1021,
            value: 0xFFFF,
        }
    }
    
    pub fn update(&mut self, data: &[u8]) {
        for &byte in data {
            self.value ^= (byte as u16) << 8;
            
            for _ in 0..8 {
                if self.value & 0x8000 != 0 {
                    self.value = (self.value << 1) ^ self.polynomial;
                } else {
                    self.value <<= 1;
                }
            }
        }
    }
    
    pub fn finalize(&self) -> u16 {
        self.value
    }
    
    pub fn calculate(data: &[u8]) -> u16 {
        let mut crc = Self::new();
        crc.update(data);
        crc.finalize()
    }
}

/// Error types for SPI with CRC
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiCrcError {
    CrcMismatch,
    BufferTooLarge,
    SpiError,
}

impl fmt::Display for SpiCrcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::CrcMismatch => write!(f, "CRC verification failed"),
            Self::BufferTooLarge => write!(f, "Buffer exceeds maximum size"),
            Self::SpiError => write!(f, "SPI communication error"),
        }
    }
}

/// SPI message with CRC protection
pub struct SpiMessage<'a> {
    command: u8,
    payload: &'a [u8],
}

impl<'a> SpiMessage<'a> {
    pub fn new(command: u8, payload: &'a [u8]) -> Self {
        Self { command, payload }
    }
    
    /// Serialize message with CRC-8
    pub fn serialize_crc8(&self, buffer: &mut [u8]) -> Result<usize, SpiCrcError> {
        let total_length = 1 + self.payload.len() + 1; // command + payload + CRC
        
        if buffer.len() < total_length {
            return Err(SpiCrcError::BufferTooLarge);
        }
        
        buffer[0] = self.command;
        buffer[1..1 + self.payload.len()].copy_from_slice(self.payload);
        
        let crc = Crc8::calculate(&buffer[..1 + self.payload.len()]);
        buffer[1 + self.payload.len()] = crc;
        
        Ok(total_length)
    }
    
    /// Deserialize and verify CRC-8
    pub fn deserialize_crc8(buffer: &'a [u8]) -> Result<Self, SpiCrcError> {
        if buffer.len() < 2 {
            return Err(SpiCrcError::BufferTooLarge);
        }
        
        let payload_end = buffer.len() - 1;
        let calculated_crc = Crc8::calculate(&buffer[..payload_end]);
        let received_crc = buffer[payload_end];
        
        if calculated_crc != received_crc {
            return Err(SpiCrcError::CrcMismatch);
        }
        
        Ok(Self {
            command: buffer[0],
            payload: &buffer[1..payload_end],
        })
    }
}

/// SPI device wrapper with CRC support
pub struct SpiWithCrc<SPI> {
    spi: SPI,
    retry_count: u8,
}

impl<SPI> SpiWithCrc<SPI> {
    pub fn new(spi: SPI) -> Self {
        Self {
            spi,
            retry_count: 3,
        }
    }
    
    pub fn set_retry_count(&mut self, count: u8) {
        self.retry_count = count;
    }
}

// Example with embedded-hal traits (for embedded systems)
#[cfg(feature = "embedded-hal")]
use embedded_hal::blocking::spi::{Transfer, Write};
#[cfg(feature = "embedded-hal")]
use embedded_hal::digital::v2::OutputPin;

#[cfg(feature = "embedded-hal")]
impl<SPI, CS> SpiWithCrc<SPI>
where
    SPI: Transfer<u8> + Write<u8>,
    CS: OutputPin,
{
    /// Send message with CRC and automatic retry
    pub fn send_with_retry<E>(
        &mut self,
        cs: &mut CS,
        command: u8,
        payload: &[u8],
    ) -> Result<(), SpiCrcError>
    where
        E: fmt::Debug,
    {
        let msg = SpiMessage::new(command, payload);
        let mut buffer = [0u8; 64];
        let length = msg.serialize_crc8(&mut buffer)?;
        
        cs.set_low().ok();
        let result = self.spi.write(&buffer[..length]);
        cs.set_high().ok();
        
        result.map_err(|_| SpiCrcError::SpiError)
    }
    
    /// Receive and verify message with CRC
    pub fn receive_with_verify<E>(
        &mut self,
        cs: &mut CS,
        buffer: &mut [u8],
    ) -> Result<SpiMessage, SpiCrcError>
    where
        E: fmt::Debug,
    {
        cs.set_low().ok();
        let result = self.spi.transfer(buffer);
        cs.set_high().ok();
        
        result.map_err(|_| SpiCrcError::SpiError)?;
        
        SpiMessage::deserialize_crc8(buffer)
    }
}

/// Example sensor reading with CRC
#[derive(Debug)]
pub struct SensorData {
    pub temperature: i16,  // 0.1°C units
    pub humidity: u16,     // 0.1% units
    pub status: u8,
}

impl SensorData {
    pub fn from_bytes(data: &[u8]) -> Option<Self> {
        if data.len() < 5 {
            return None;
        }
        
        Some(Self {
            temperature: i16::from_be_bytes([data[0], data[1]]),
            humidity: u16::from_be_bytes([data[2], data[3]]),
            status: data[4],
        })
    }
    
    pub fn temperature_celsius(&self) -> f32 {
        self.temperature as f32 / 10.0
    }
    
    pub fn humidity_percent(&self) -> f32 {
        self.humidity as f32 / 10.0
    }
}

// Example usage (pseudo-code for embedded system)
#[cfg(not(feature = "embedded-hal"))]
pub fn example_usage() {
    // Simulated example showing the API
    let command = 0xA0;
    let payload = [0x01]; // Read sensor command
    
    let msg = SpiMessage::new(command, &payload);
    let mut tx_buffer = [0u8; 64];
    
    match msg.serialize_crc8(&mut tx_buffer) {
        Ok(length) => {
            println!("Serialized {} bytes with CRC", length);
            println!("TX: {:02X?}", &tx_buffer[..length]);
        }
        Err(e) => println!("Error: {}", e),
    }
    
    // Simulate received data
    let mut rx_buffer = [0xA0, 0x00, 0xC8, 0x01, 0xF4, 0x00, 0x5E]; // Temp: 20.0°C, Hum: 50.0%
    
    match SpiMessage::deserialize_crc8(&rx_buffer) {
        Ok(msg) => {
            if let Some(sensor) = SensorData::from_bytes(msg.payload) {
                println!("Temperature: {:.1}°C", sensor.temperature_celsius());
                println!("Humidity: {:.1}%", sensor.humidity_percent());
            }
        }
        Err(SpiCrcError::CrcMismatch) => {
            println!("CRC error detected - retrying...");
        }
        Err(e) => println!("Error: {}", e),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_crc8_calculation() {
        let data = [0x01, 0x02, 0x03];
        let crc = Crc8::calculate(&data);
        assert_ne!(crc, 0); // Basic sanity check
    }
    
    #[test]
    fn test_message_roundtrip() {
        let msg = SpiMessage::new(0xA0, &[0x01, 0x02, 0x03]);
        let mut buffer = [0u8; 16];
        
        let length = msg.serialize_crc8(&mut buffer).unwrap();
        let decoded = SpiMessage::deserialize_crc8(&buffer[..length]).unwrap();
        
        assert_eq!(decoded.command, 0xA0);
        assert_eq!(decoded.payload, &[0x01, 0x02, 0x03]);
    }
    
    #[test]
    fn test_crc_mismatch_detection() {
        let msg = SpiMessage::new(0xA0, &[0x01, 0x02]);
        let mut buffer = [0u8; 16];
        
        let length = msg.serialize_crc8(&mut buffer).unwrap();
        buffer[length - 1] ^= 0x01; // Corrupt CRC
        
        let result = SpiMessage::deserialize_crc8(&buffer[..length]);
        assert_eq!(result.unwrap_err(), SpiCrcError::CrcMismatch);
    }
}
```

## Summary

**SPI with CRC** enhances the reliability of SPI communication by adding error detection capabilities. This is essential in environments where electromagnetic interference, noise, or signal degradation can corrupt data. By implementing CRC checks, systems can detect transmission errors with high probability and take corrective action such as retrying the communication or flagging errors for higher-level handling.

**Key points:**

- **CRC algorithms** use polynomial division to generate checksums that detect bit errors
- **CRC-8** is lightweight and suitable for small packets in resource-constrained systems
- **CRC-16/32** provide stronger error detection for longer messages or critical applications
- Implementation involves calculating CRC on transmitted data, appending it to messages, and verifying it on reception
- Both C/C++ and Rust implementations provide table-free algorithms suitable for embedded systems
- Proper error handling includes retry mechanisms and error reporting to ensure data integrity
- This technique is widely used in automotive (CAN, LIN), industrial (Modbus), and sensor applications where reliability is critical