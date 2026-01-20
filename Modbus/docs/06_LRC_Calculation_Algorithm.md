# LRC Calculation Algorithm for Modbus ASCII Mode

## Overview

The Longitudinal Redundancy Check (LRC) is an error detection mechanism used exclusively in Modbus ASCII mode communications. Unlike Modbus RTU which uses CRC-16, Modbus ASCII employs LRC as a simpler checksum algorithm to verify data integrity during transmission.

## What is LRC?

LRC is a form of redundancy check that adds all bytes in a message and computes a checksum based on the two's complement of the sum. The receiving device performs the same calculation and compares results to detect transmission errors.

## LRC Calculation Process

The LRC algorithm follows these steps:

1. **Initialize**: Start with a sum of 0x00
2. **Sum all bytes**: Add all data bytes in the message (excluding the LRC field itself and frame delimiters)
3. **Two's complement**: Calculate the two's complement of the sum
4. **Extract LRC**: Keep only the least significant byte (8 bits)

### Mathematical Formula

```
LRC = ((Sum of all bytes) XOR 0xFF) + 1
```

Or equivalently:
```
LRC = (-(Sum of all bytes)) & 0xFF
```

## Modbus ASCII Frame Structure

In Modbus ASCII, the LRC appears at the end of the message:

```
: [Address] [Function] [Data...] [LRC] CR LF
```

- `:` - Start delimiter (colon)
- `CR LF` - End delimiters (Carriage Return, Line Feed)
- Each byte is transmitted as two ASCII hex characters

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdio.h>

/**
 * Calculate LRC for Modbus ASCII mode
 * @param data Pointer to data buffer
 * @param length Number of bytes to include in calculation
 * @return LRC value (8-bit)
 */
uint8_t calculate_lrc(const uint8_t *data, size_t length) {
    uint8_t lrc = 0;
    
    // Sum all bytes
    for (size_t i = 0; i < length; i++) {
        lrc += data[i];
    }
    
    // Two's complement
    lrc = (uint8_t)(-(int8_t)lrc);
    
    return lrc;
}

/**
 * Alternative implementation using XOR method
 */
uint8_t calculate_lrc_alt(const uint8_t *data, size_t length) {
    uint8_t lrc = 0;
    
    for (size_t i = 0; i < length; i++) {
        lrc += data[i];
    }
    
    lrc = (lrc ^ 0xFF) + 1;
    
    return lrc;
}

/**
 * Verify LRC of received message
 * @param data Pointer to complete message including LRC
 * @param length Total length including LRC byte
 * @return 1 if valid, 0 if invalid
 */
int verify_lrc(const uint8_t *data, size_t length) {
    if (length < 2) return 0;
    
    uint8_t calculated_lrc = calculate_lrc(data, length - 1);
    uint8_t received_lrc = data[length - 1];
    
    return (calculated_lrc == received_lrc);
}

// Example usage
int main() {
    // Example Modbus ASCII message: Read holding registers
    // Device address: 0x01, Function: 0x03, Start: 0x0000, Count: 0x0002
    uint8_t message[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    
    uint8_t lrc = calculate_lrc(message, sizeof(message));
    
    printf("Message bytes: ");
    for (size_t i = 0; i < sizeof(message); i++) {
        printf("%02X ", message[i]);
    }
    printf("\nCalculated LRC: 0x%02X\n", lrc);
    
    // Complete message with LRC
    uint8_t complete_msg[7];
    for (size_t i = 0; i < sizeof(message); i++) {
        complete_msg[i] = message[i];
    }
    complete_msg[6] = lrc;
    
    // Verify
    if (verify_lrc(complete_msg, 7)) {
        printf("LRC verification: PASSED\n");
    } else {
        printf("LRC verification: FAILED\n");
    }
    
    return 0;
}
```

### C++ Class-Based Implementation

```cpp
#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>

class ModbusLRC {
public:
    /**
     * Calculate LRC for given data
     */
    static uint8_t calculate(const std::vector<uint8_t>& data) {
        uint8_t lrc = 0;
        
        for (uint8_t byte : data) {
            lrc += byte;
        }
        
        return static_cast<uint8_t>(-static_cast<int8_t>(lrc));
    }
    
    /**
     * Calculate LRC from raw pointer
     */
    static uint8_t calculate(const uint8_t* data, size_t length) {
        uint8_t lrc = 0;
        
        for (size_t i = 0; i < length; i++) {
            lrc += data[i];
        }
        
        return static_cast<uint8_t>(-static_cast<int8_t>(lrc));
    }
    
    /**
     * Verify message integrity
     */
    static bool verify(const std::vector<uint8_t>& message) {
        if (message.size() < 2) return false;
        
        std::vector<uint8_t> data(message.begin(), message.end() - 1);
        uint8_t calculated = calculate(data);
        uint8_t received = message.back();
        
        return calculated == received;
    }
    
    /**
     * Append LRC to message
     */
    static void append(std::vector<uint8_t>& message) {
        uint8_t lrc = calculate(message);
        message.push_back(lrc);
    }
};

// Example usage
int main() {
    std::vector<uint8_t> message = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    
    std::cout << "Original message: ";
    for (uint8_t byte : message) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    
    ModbusLRC::append(message);
    
    std::cout << "With LRC: ";
    for (uint8_t byte : message) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    
    std::cout << "Verification: " 
              << (ModbusLRC::verify(message) ? "PASSED" : "FAILED") 
              << std::endl;
    
    return 0;
}
```

### Rust Implementation

```rust
/// Calculate LRC for Modbus ASCII mode
pub fn calculate_lrc(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &byte| acc.wrapping_add(byte));
    
    // Two's complement
    (!sum).wrapping_add(1)
}

/// Alternative implementation
pub fn calculate_lrc_alt(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &byte| acc.wrapping_add(byte));
    
    // Using negation with wrapping
    (-(sum as i8)) as u8
}

/// Verify LRC of a complete message
pub fn verify_lrc(message: &[u8]) -> bool {
    if message.len() < 2 {
        return false;
    }
    
    let data = &message[..message.len() - 1];
    let calculated_lrc = calculate_lrc(data);
    let received_lrc = message[message.len() - 1];
    
    calculated_lrc == received_lrc
}

/// Append LRC to a message vector
pub fn append_lrc(message: &mut Vec<u8>) {
    let lrc = calculate_lrc(message);
    message.push(lrc);
}

/// Modbus LRC calculator struct with builder pattern
pub struct ModbusLRC {
    data: Vec<u8>,
}

impl ModbusLRC {
    pub fn new() -> Self {
        ModbusLRC { data: Vec::new() }
    }
    
    pub fn with_capacity(capacity: usize) -> Self {
        ModbusLRC { 
            data: Vec::with_capacity(capacity) 
        }
    }
    
    pub fn add_byte(&mut self, byte: u8) -> &mut Self {
        self.data.push(byte);
        self
    }
    
    pub fn add_bytes(&mut self, bytes: &[u8]) -> &mut Self {
        self.data.extend_from_slice(bytes);
        self
    }
    
    pub fn calculate(&self) -> u8 {
        calculate_lrc(&self.data)
    }
    
    pub fn build(mut self) -> Vec<u8> {
        let lrc = self.calculate();
        self.data.push(lrc);
        self.data
    }
    
    pub fn verify(&self, lrc: u8) -> bool {
        self.calculate() == lrc
    }
}

impl Default for ModbusLRC {
    fn default() -> Self {
        Self::new()
    }
}

// Example usage
fn main() {
    // Example 1: Basic calculation
    let message = vec![0x01, 0x03, 0x00, 0x00, 0x00, 0x02];
    let lrc = calculate_lrc(&message);
    
    println!("Message: {:02X?}", message);
    println!("Calculated LRC: 0x{:02X}", lrc);
    
    // Example 2: Using builder pattern
    let complete_message = ModbusLRC::new()
        .add_byte(0x01)
        .add_byte(0x03)
        .add_bytes(&[0x00, 0x00, 0x00, 0x02])
        .build();
    
    println!("Complete message: {:02X?}", complete_message);
    
    // Example 3: Verification
    if verify_lrc(&complete_message) {
        println!("LRC verification: PASSED");
    } else {
        println!("LRC verification: FAILED");
    }
    
    // Example 4: Error detection
    let mut corrupted = complete_message.clone();
    corrupted[2] = 0xFF; // Corrupt data
    
    if verify_lrc(&corrupted) {
        println!("Corrupted message: NOT DETECTED (BAD)");
    } else {
        println!("Corrupted message: DETECTED (GOOD)");
    }
}
```

## Key Considerations

### Advantages of LRC
- **Simplicity**: Easy to implement and understand
- **Low overhead**: Single byte checksum
- **Human-readable**: Works well with ASCII mode's text-based format

### Limitations
- **Weak error detection**: Less robust than CRC-16 used in RTU mode
- **Limited coverage**: May miss certain multi-bit errors
- **Not suitable for noisy environments**: Best for relatively clean communication channels

### When to Use LRC
- Modbus ASCII mode only
- Low-noise environments
- Debug/development scenarios where readability matters
- Legacy systems requiring ASCII compatibility

## Summary

The LRC (Longitudinal Redundancy Check) is a simple checksum algorithm used in Modbus ASCII mode for error detection. It calculates the two's complement of the sum of all message bytes, providing basic integrity verification. While less robust than CRC-16, LRC is sufficient for ASCII mode communications in relatively clean environments. The algorithm is straightforward to implement across programming languages, requiring only basic arithmetic operations (addition, negation) and operates on 8-bit values. All implementations shown provide both calculation and verification functionality, with Rust offering additional safety through its type system and the C++ version providing object-oriented encapsulation.