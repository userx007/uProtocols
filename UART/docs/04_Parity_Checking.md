# UART Parity Checking: Comprehensive Guide

## Overview

Parity checking is a simple error detection mechanism used in UART (Universal Asynchronous Receiver/Transmitter) communication to verify data integrity during transmission. It adds a single bit to each data frame that allows the receiver to detect single-bit errors that may occur due to noise, interference, or signal degradation.

## How Parity Checking Works

Parity checking involves counting the number of '1' bits in the data byte and adding an extra parity bit to make the total count either even or odd, depending on the parity type selected. The receiver performs the same calculation and compares it with the received parity bit to detect errors.

### Types of Parity

1. **Even Parity**: The parity bit is set so the total number of '1' bits (including the parity bit) is even
2. **Odd Parity**: The parity bit is set so the total number of '1' bits (including the parity bit) is odd
3. **Mark Parity**: The parity bit is always '1' (rarely used, mainly for testing)
4. **Space Parity**: The parity bit is always '0' (rarely used, mainly for testing)
5. **No Parity**: No parity bit is transmitted (most common in modern systems)

### Frame Structure with Parity

```
[Start Bit][Data Bits (5-9)][Parity Bit][Stop Bit(s)]
```

For example, with 8 data bits, even parity, and 1 stop bit:
```
[0][D0 D1 D2 D3 D4 D5 D6 D7][P][1]
```

## C/C++ Implementation

### Basic Parity Calculation Functions

```c
#include <stdint.h>
#include <stdbool.h>

// Count number of set bits in a byte
uint8_t count_ones(uint8_t data) {
    uint8_t count = 0;
    while (data) {
        count += data & 1;
        data >>= 1;
    }
    return count;
}

// Alternative using Brian Kernighan's algorithm (faster)
uint8_t count_ones_fast(uint8_t data) {
    uint8_t count = 0;
    while (data) {
        data &= (data - 1);  // Clears the lowest set bit
        count++;
    }
    return count;
}

// Calculate even parity bit
bool calculate_even_parity(uint8_t data) {
    return (count_ones(data) & 1) == 1;  // Return 1 if odd number of bits
}

// Calculate odd parity bit
bool calculate_odd_parity(uint8_t data) {
    return (count_ones(data) & 1) == 0;  // Return 1 if even number of bits
}

// Verify even parity
bool verify_even_parity(uint8_t data, bool parity_bit) {
    uint8_t total_ones = count_ones(data) + (parity_bit ? 1 : 0);
    return (total_ones & 1) == 0;  // Should be even
}

// Verify odd parity
bool verify_odd_parity(uint8_t data, bool parity_bit) {
    uint8_t total_ones = count_ones(data) + (parity_bit ? 1 : 0);
    return (total_ones & 1) == 1;  // Should be odd
}
```

### Complete UART Parity Implementation (Embedded C)

```c
#include <stdint.h>
#include <stdbool.h>

// Parity types enumeration
typedef enum {
    PARITY_NONE = 0,
    PARITY_EVEN,
    PARITY_ODD,
    PARITY_MARK,
    PARITY_SPACE
} parity_type_t;

// UART configuration structure
typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;     // 5-9
    uint8_t stop_bits;     // 1 or 2
    parity_type_t parity;
} uart_config_t;

// Statistics structure
typedef struct {
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t parity_errors;
    uint32_t frame_errors;
} uart_stats_t;

static uart_stats_t uart_stats = {0};

// Calculate parity bit based on type
bool calculate_parity(uint8_t data, parity_type_t parity_type) {
    switch (parity_type) {
        case PARITY_EVEN:
            return calculate_even_parity(data);
        case PARITY_ODD:
            return calculate_odd_parity(data);
        case PARITY_MARK:
            return true;
        case PARITY_SPACE:
            return false;
        case PARITY_NONE:
        default:
            return false;
    }
}

// Verify received parity
bool verify_parity(uint8_t data, bool parity_bit, parity_type_t parity_type) {
    switch (parity_type) {
        case PARITY_EVEN:
            return verify_even_parity(data, parity_bit);
        case PARITY_ODD:
            return verify_odd_parity(data, parity_bit);
        case PARITY_MARK:
            return parity_bit == true;
        case PARITY_SPACE:
            return parity_bit == false;
        case PARITY_NONE:
        default:
            return true;  // Always valid when no parity
    }
}

// Transmit byte with parity (pseudo-code for hardware interaction)
void uart_transmit_byte(uint8_t data, parity_type_t parity_type) {
    bool parity_bit = calculate_parity(data, parity_type);
    
    // Hardware-specific transmission
    // This is pseudo-code; actual implementation depends on microcontroller
    // UART_DATA_REG = data;
    // if (parity_type != PARITY_NONE) {
    //     UART_PARITY_REG = parity_bit;
    // }
    // Wait for transmission complete
    
    uart_stats.bytes_sent++;
}

// Receive byte with parity checking
bool uart_receive_byte(uint8_t *data, parity_type_t parity_type) {
    // Hardware-specific reception (pseudo-code)
    // *data = UART_DATA_REG;
    // bool received_parity = UART_PARITY_REG;
    
    // For demonstration, assume we have these values
    uint8_t received_data = 0;  // Would come from hardware
    bool received_parity = 0;    // Would come from hardware
    
    uart_stats.bytes_received++;
    
    if (parity_type != PARITY_NONE) {
        if (!verify_parity(received_data, received_parity, parity_type)) {
            uart_stats.parity_errors++;
            return false;  // Parity error detected
        }
    }
    
    *data = received_data;
    return true;  // Success
}
```

### C++ Implementation with Modern Features

```cpp
#include <cstdint>
#include <optional>
#include <array>
#include <bit>  // C++20 for std::popcount

enum class ParityType {
    None,
    Even,
    Odd,
    Mark,
    Space
};

class ParityCalculator {
public:
    // Modern C++20 using std::popcount
    static bool calculateEvenParity(uint8_t data) {
        return (std::popcount(data) & 1) == 1;
    }
    
    static bool calculateOddParity(uint8_t data) {
        return (std::popcount(data) & 1) == 0;
    }
    
    static bool calculateParity(uint8_t data, ParityType type) {
        switch (type) {
            case ParityType::Even:  return calculateEvenParity(data);
            case ParityType::Odd:   return calculateOddParity(data);
            case ParityType::Mark:  return true;
            case ParityType::Space: return false;
            case ParityType::None:  return false;
        }
        return false;
    }
    
    static bool verifyParity(uint8_t data, bool parityBit, ParityType type) {
        switch (type) {
            case ParityType::Even: {
                uint8_t totalOnes = std::popcount(data) + (parityBit ? 1 : 0);
                return (totalOnes & 1) == 0;
            }
            case ParityType::Odd: {
                uint8_t totalOnes = std::popcount(data) + (parityBit ? 1 : 0);
                return (totalOnes & 1) == 1;
            }
            case ParityType::Mark:  return parityBit == true;
            case ParityType::Space: return parityBit == false;
            case ParityType::None:  return true;
        }
        return false;
    }
};

class UARTInterface {
private:
    ParityType parityType_;
    uint32_t parityErrors_;
    uint32_t totalBytes_;
    
public:
    UARTInterface(ParityType parity = ParityType::None) 
        : parityType_(parity), parityErrors_(0), totalBytes_(0) {}
    
    void transmit(uint8_t data) {
        bool parityBit = ParityCalculator::calculateParity(data, parityType_);
        // Send to hardware (platform-specific)
        transmitToHardware(data, parityBit);
        totalBytes_++;
    }
    
    std::optional<uint8_t> receive() {
        auto [data, parityBit] = receiveFromHardware();
        totalBytes_++;
        
        if (parityType_ != ParityType::None) {
            if (!ParityCalculator::verifyParity(data, parityBit, parityType_)) {
                parityErrors_++;
                return std::nullopt;  // Parity error
            }
        }
        
        return data;
    }
    
    void setParityType(ParityType type) { parityType_ = type; }
    uint32_t getParityErrors() const { return parityErrors_; }
    double getErrorRate() const { 
        return totalBytes_ > 0 ? static_cast<double>(parityErrors_) / totalBytes_ : 0.0;
    }
    
private:
    void transmitToHardware(uint8_t data, bool parity) {
        // Platform-specific implementation
    }
    
    std::pair<uint8_t, bool> receiveFromHardware() {
        // Platform-specific implementation
        return {0, false};
    }
};
```

## Rust Implementation

```rust
use std::collections::VecDeque;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ParityType {
    None,
    Even,
    Odd,
    Mark,
    Space,
}

pub struct ParityCalculator;

impl ParityCalculator {
    /// Count the number of set bits in a byte
    pub fn count_ones(data: u8) -> u32 {
        data.count_ones()
    }
    
    /// Calculate even parity bit
    pub fn calculate_even_parity(data: u8) -> bool {
        (Self::count_ones(data) & 1) == 1
    }
    
    /// Calculate odd parity bit
    pub fn calculate_odd_parity(data: u8) -> bool {
        (Self::count_ones(data) & 1) == 0
    }
    
    /// Calculate parity bit based on type
    pub fn calculate_parity(data: u8, parity_type: ParityType) -> bool {
        match parity_type {
            ParityType::Even => Self::calculate_even_parity(data),
            ParityType::Odd => Self::calculate_odd_parity(data),
            ParityType::Mark => true,
            ParityType::Space => false,
            ParityType::None => false,
        }
    }
    
    /// Verify parity of received data
    pub fn verify_parity(data: u8, parity_bit: bool, parity_type: ParityType) -> bool {
        match parity_type {
            ParityType::Even => {
                let total_ones = Self::count_ones(data) + if parity_bit { 1 } else { 0 };
                (total_ones & 1) == 0
            }
            ParityType::Odd => {
                let total_ones = Self::count_ones(data) + if parity_bit { 1 } else { 0 };
                (total_ones & 1) == 1
            }
            ParityType::Mark => parity_bit,
            ParityType::Space => !parity_bit,
            ParityType::None => true,
        }
    }
}

#[derive(Debug)]
pub struct UartStats {
    pub bytes_sent: u64,
    pub bytes_received: u64,
    pub parity_errors: u64,
    pub frame_errors: u64,
}

impl UartStats {
    pub fn new() -> Self {
        Self {
            bytes_sent: 0,
            bytes_received: 0,
            parity_errors: 0,
            frame_errors: 0,
        }
    }
    
    pub fn error_rate(&self) -> f64 {
        if self.bytes_received > 0 {
            self.parity_errors as f64 / self.bytes_received as f64
        } else {
            0.0
        }
    }
}

pub struct UartInterface {
    parity_type: ParityType,
    stats: UartStats,
    rx_buffer: VecDeque<u8>,
}

impl UartInterface {
    pub fn new(parity_type: ParityType) -> Self {
        Self {
            parity_type,
            stats: UartStats::new(),
            rx_buffer: VecDeque::new(),
        }
    }
    
    /// Transmit a byte with parity
    pub fn transmit(&mut self, data: u8) -> Result<(), &'static str> {
        let parity_bit = ParityCalculator::calculate_parity(data, self.parity_type);
        
        // Hardware transmission (platform-specific)
        self.transmit_to_hardware(data, parity_bit)?;
        
        self.stats.bytes_sent += 1;
        Ok(())
    }
    
    /// Receive a byte and verify parity
    pub fn receive(&mut self) -> Result<u8, &'static str> {
        let (data, parity_bit) = self.receive_from_hardware()?;
        
        self.stats.bytes_received += 1;
        
        if self.parity_type != ParityType::None {
            if !ParityCalculator::verify_parity(data, parity_bit, self.parity_type) {
                self.stats.parity_errors += 1;
                return Err("Parity error detected");
            }
        }
        
        Ok(data)
    }
    
    /// Transmit a buffer of data
    pub fn transmit_buffer(&mut self, buffer: &[u8]) -> Result<usize, &'static str> {
        let mut sent = 0;
        for &byte in buffer {
            self.transmit(byte)?;
            sent += 1;
        }
        Ok(sent)
    }
    
    /// Set parity type
    pub fn set_parity_type(&mut self, parity_type: ParityType) {
        self.parity_type = parity_type;
    }
    
    /// Get statistics
    pub fn get_stats(&self) -> &UartStats {
        &self.stats
    }
    
    // Platform-specific methods (would be implemented for actual hardware)
    fn transmit_to_hardware(&self, _data: u8, _parity: bool) -> Result<(), &'static str> {
        // Platform-specific implementation
        Ok(())
    }
    
    fn receive_from_hardware(&self) -> Result<(u8, bool), &'static str> {
        // Platform-specific implementation
        Ok((0, false))
    }
}

// Example usage with error handling
pub fn example_usage() {
    let mut uart = UartInterface::new(ParityType::Even);
    
    // Transmit data
    let data_to_send = b"Hello UART";
    match uart.transmit_buffer(data_to_send) {
        Ok(sent) => println!("Transmitted {} bytes", sent),
        Err(e) => eprintln!("Transmission error: {}", e),
    }
    
    // Receive data
    match uart.receive() {
        Ok(data) => println!("Received: 0x{:02X}", data),
        Err(e) => eprintln!("Reception error: {}", e),
    }
    
    // Check statistics
    let stats = uart.get_stats();
    println!("Parity errors: {}", stats.parity_errors);
    println!("Error rate: {:.4}%", stats.error_rate() * 100.0);
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_even_parity() {
        // 0b01010101 has 4 ones (even), so parity bit should be 0
        assert_eq!(ParityCalculator::calculate_even_parity(0b01010101), false);
        // 0b01010111 has 5 ones (odd), so parity bit should be 1
        assert_eq!(ParityCalculator::calculate_even_parity(0b01010111), true);
    }
    
    #[test]
    fn test_odd_parity() {
        // 0b01010101 has 4 ones (even), so parity bit should be 1
        assert_eq!(ParityCalculator::calculate_odd_parity(0b01010101), true);
        // 0b01010111 has 5 ones (odd), so parity bit should be 0
        assert_eq!(ParityCalculator::calculate_odd_parity(0b01010111), false);
    }
    
    #[test]
    fn test_verify_even_parity() {
        let data = 0b01010101; // 4 ones
        assert!(ParityCalculator::verify_parity(data, false, ParityType::Even));
        assert!(!ParityCalculator::verify_parity(data, true, ParityType::Even));
    }
    
    #[test]
    fn test_mark_space_parity() {
        assert_eq!(ParityCalculator::calculate_parity(0xFF, ParityType::Mark), true);
        assert_eq!(ParityCalculator::calculate_parity(0x00, ParityType::Space), false);
    }
}
```

## Summary

**Parity checking** is a fundamental error detection mechanism in UART communication that adds a single bit to each transmitted byte to detect single-bit errors. The five parity types (even, odd, mark, space, and none) offer different trade-offs between overhead and error detection capability.

**Key Points:**
- **Even parity** sets the parity bit to make the total number of 1s even
- **Odd parity** sets the parity bit to make the total number of 1s odd
- **Mark/Space parity** are constant values used primarily for testing
- Parity can only detect single-bit errors (and odd numbers of bit errors)
- Modern systems often use **no parity** and rely on higher-level error detection

**Implementation Considerations:**
- Efficient bit counting using hardware instructions (`popcount` in modern CPUs)
- Statistics tracking for monitoring link quality
- Type-safe enumerations for parity configuration
- Error handling for parity failures
- Hardware abstraction for platform portability

**Limitations:**
- Cannot detect even numbers of bit errors
- Cannot correct errors, only detect them
- Adds overhead (1 extra bit per byte)
- For robust communication, higher-level protocols (CRC, checksums) are preferred

The provided code examples demonstrate complete implementations in C, C++, and Rust with proper error handling, testing, and statistics tracking suitable for embedded systems and real-world applications.