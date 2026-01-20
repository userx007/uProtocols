# CRC-16 Calculation Algorithm for Modbus RTU

## Overview

The CRC-16 (Cyclic Redundancy Check) is a critical error detection mechanism used in Modbus RTU communications. It ensures data integrity by appending a 16-bit checksum to each message frame. The receiver recalculates the CRC and compares it with the transmitted value to verify that no transmission errors occurred.

## Technical Details

### Polynomial and Algorithm

Modbus RTU uses the **CRC-16-ANSI** algorithm with:
- **Polynomial**: 0xA001 (reversed representation of 0x8005)
- **Initial value**: 0xFFFF
- **Reflection**: Input and output are reflected (LSB-first processing)
- **Final XOR**: None

The algorithm processes each byte of the message sequentially, updating a 16-bit CRC register through XOR operations and bit shifting. The CRC is calculated over all bytes in the message except the CRC itself.

### How It Works

1. Initialize CRC register to 0xFFFF
2. For each data byte:
   - XOR the byte with the low byte of the CRC register
   - Shift right 8 times, checking the LSB each time
   - If LSB is 1, XOR with polynomial 0xA001
3. Append CRC to message as two bytes (low byte first, then high byte)

### Message Frame Structure

```
[Device Address][Function Code][Data...][CRC Low][CRC High]
```

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdio.h>

/**
 * Calculate CRC-16 for Modbus RTU
 * @param data Pointer to data buffer
 * @param length Number of bytes to process
 * @return 16-bit CRC value
 */
uint16_t modbus_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;  // Initialize CRC register
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];  // XOR byte into LSB of crc
        
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;  // Apply polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

/**
 * Append CRC to Modbus message
 */
void append_crc(uint8_t *message, size_t data_length) {
    uint16_t crc = modbus_crc16(message, data_length);
    message[data_length] = crc & 0xFF;        // Low byte first
    message[data_length + 1] = (crc >> 8) & 0xFF;  // High byte
}

/**
 * Verify CRC of received message
 */
int verify_crc(const uint8_t *message, size_t total_length) {
    if (total_length < 3) return 0;  // Message too short
    
    size_t data_length = total_length - 2;
    uint16_t calculated_crc = modbus_crc16(message, data_length);
    uint16_t received_crc = message[data_length] | (message[data_length + 1] << 8);
    
    return calculated_crc == received_crc;
}

// Example usage
int main() {
    // Read holding register example: Device 1, Function 3, Start 0x0000, Count 10
    uint8_t request[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00};
    
    append_crc(request, 6);
    
    printf("Modbus RTU Request: ");
    for (int i = 0; i < 8; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Verify the message
    if (verify_crc(request, 8)) {
        printf("CRC verification: PASSED\n");
    } else {
        printf("CRC verification: FAILED\n");
    }
    
    return 0;
}
```

### Optimized C++ Implementation with Lookup Table

```cpp
#include <array>
#include <cstdint>
#include <vector>

class ModbusCRC {
private:
    // Pre-computed lookup table for faster CRC calculation
    static constexpr std::array<uint16_t, 256> generateCRCTable() {
        std::array<uint16_t, 256> table{};
        
        for (uint16_t i = 0; i < 256; i++) {
            uint16_t crc = i;
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
        return table;
    }
    
    static constexpr auto CRC_TABLE = generateCRCTable();

public:
    static uint16_t calculate(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        
        for (size_t i = 0; i < length; i++) {
            uint8_t index = crc ^ data[i];
            crc = (crc >> 8) ^ CRC_TABLE[index];
        }
        
        return crc;
    }
    
    static uint16_t calculate(const std::vector<uint8_t>& data) {
        return calculate(data.data(), data.size());
    }
    
    static bool verify(const std::vector<uint8_t>& message) {
        if (message.size() < 3) return false;
        
        size_t dataLen = message.size() - 2;
        uint16_t calculated = calculate(message.data(), dataLen);
        uint16_t received = message[dataLen] | (message[dataLen + 1] << 8);
        
        return calculated == received;
    }
};
```

### Rust Implementation

```rust
/// Calculate CRC-16 for Modbus RTU using polynomial 0xA001
pub fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

/// Append CRC to a Modbus message (returns new vector)
pub fn append_crc(data: &[u8]) -> Vec<u8> {
    let crc = modbus_crc16(data);
    let mut message = data.to_vec();
    message.push((crc & 0xFF) as u8);        // Low byte first
    message.push(((crc >> 8) & 0xFF) as u8); // High byte
    message
}

/// Verify CRC of received message
pub fn verify_crc(message: &[u8]) -> bool {
    if message.len() < 3 {
        return false;
    }
    
    let data_length = message.len() - 2;
    let calculated_crc = modbus_crc16(&message[..data_length]);
    let received_crc = message[data_length] as u16 | ((message[data_length + 1] as u16) << 8);
    
    calculated_crc == received_crc
}

// Optimized version using lookup table
pub struct ModbusCRC {
    table: [u16; 256],
}

impl ModbusCRC {
    pub fn new() -> Self {
        let mut table = [0u16; 256];
        
        for i in 0..256 {
            let mut crc = i as u16;
            for _ in 0..8 {
                if crc & 0x0001 != 0 {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
        
        ModbusCRC { table }
    }
    
    pub fn calculate(&self, data: &[u8]) -> u16 {
        let mut crc: u16 = 0xFFFF;
        
        for byte in data {
            let index = (crc ^ (*byte as u16)) & 0xFF;
            crc = (crc >> 8) ^ self.table[index as usize];
        }
        
        crc
    }
}

impl Default for ModbusCRC {
    fn default() -> Self {
        Self::new()
    }
}

// Example usage
fn main() {
    // Read holding register: Device 1, Func 3, Start 0x0000, Count 10
    let request_data = vec![0x01, 0x03, 0x00, 0x00, 0x00, 0x0A];
    let request = append_crc(&request_data);
    
    println!("Modbus RTU Request: {:02X?}", request);
    
    if verify_crc(&request) {
        println!("CRC verification: PASSED");
    } else {
        println!("CRC verification: FAILED");
    }
    
    // Using lookup table version
    let crc_calculator = ModbusCRC::new();
    let crc = crc_calculator.calculate(&request_data);
    println!("CRC (lookup table): 0x{:04X}", crc);
}
```

## Summary

The CRC-16 algorithm is fundamental to Modbus RTU's reliability, providing robust error detection for serial communications. Using the 0xA001 polynomial with LSB-first processing, it generates a 16-bit checksum appended to each message frame. The implementation can be optimized using lookup tables, trading memory (512 bytes) for significant performance gains—especially important in embedded systems with frequent message processing. Both master and slave devices must implement identical CRC calculation to ensure interoperability. Modern implementations in C/C++ and Rust provide type safety and efficiency, with Rust's ownership model offering additional protection against common memory errors in protocol implementations.