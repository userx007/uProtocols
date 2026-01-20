# Big-Endian vs Little-Endian in Modbus

## Overview

Endianness refers to the order in which bytes are arranged within multi-byte data types. In Modbus communications, proper handling of byte order is critical for correctly interpreting 16-bit registers and 32-bit values. This becomes especially important when different systems with varying endianness communicate with each other.

## Understanding Endianness

**Big-Endian (BE)**: The most significant byte (MSB) is stored at the lowest memory address. This is the "natural" reading order for humans (left to right).

**Little-Endian (LE)**: The least significant byte (LSB) is stored at the lowest memory address. This is common in x86/x64 architectures.

### Example: The value 0x12345678

**Big-Endian memory layout:**
```
Address: 0x00  0x01  0x02  0x03
Value:   0x12  0x34  0x56  0x78
```

**Little-Endian memory layout:**
```
Address: 0x00  0x01  0x02  0x03
Value:   0x78  0x56  0x34  0x12
```

## Modbus Byte Order Considerations

### 16-bit Register Order
Modbus transmits data in **big-endian format** over the wire (network byte order). However, within a 16-bit register, the byte order follows the protocol specification.

### 32-bit Value Representations
When dealing with 32-bit values (floats, longs, doubles) that span multiple 16-bit registers, there are four possible byte orders:

1. **Big-Endian (ABCD)**: Most significant word first, big-endian bytes
2. **Little-Endian (DCBA)**: Least significant word first, little-endian bytes
3. **Big-Endian Byte Swap (BADC)**: Most significant word first, little-endian bytes
4. **Little-Endian Byte Swap (CDAB)**: Least significant word first, big-endian bytes

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Detect system endianness
int is_little_endian() {
    uint16_t test = 0x0001;
    return *((uint8_t*)&test) == 0x01;
}

// Swap bytes in a 16-bit value
uint16_t swap_bytes_16(uint16_t value) {
    return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
}

// Swap bytes in a 32-bit value
uint32_t swap_bytes_32(uint32_t value) {
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8)  |
           ((value & 0x0000FF00) << 8)  |
           ((value & 0x000000FF) << 24);
}

// Convert two Modbus registers to 32-bit float
// Assumes ABCD byte order (big-endian)
float modbus_get_float_abcd(uint16_t reg_high, uint16_t reg_low) {
    uint32_t combined = ((uint32_t)reg_high << 16) | reg_low;
    
    if (is_little_endian()) {
        combined = swap_bytes_32(combined);
    }
    
    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Convert two Modbus registers to 32-bit float
// Assumes CDAB byte order (mid-little-endian)
float modbus_get_float_cdab(uint16_t reg_high, uint16_t reg_low) {
    uint32_t combined = ((uint32_t)reg_low << 16) | reg_high;
    
    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Set 32-bit float into two Modbus registers (ABCD format)
void modbus_set_float_abcd(float value, uint16_t *reg_high, uint16_t *reg_low) {
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));
    
    if (is_little_endian()) {
        temp = swap_bytes_32(temp);
    }
    
    *reg_high = (temp >> 16) & 0xFFFF;
    *reg_low = temp & 0xFFFF;
}

// Example: Read and convert Modbus registers
int main() {
    // Simulated Modbus registers containing float value 123.456
    uint16_t registers[2] = {0x42F6, 0xE979}; // ABCD format
    
    float value = modbus_get_float_abcd(registers[0], registers[1]);
    printf("Float value (ABCD): %f\n", value);
    
    // Convert back
    uint16_t out_high, out_low;
    modbus_set_float_abcd(123.456f, &out_high, &out_low);
    printf("Registers: 0x%04X 0x%04X\n", out_high, out_low);
    
    // Demonstrate different byte orders
    printf("\nSystem is %s-endian\n", is_little_endian() ? "little" : "big");
    
    return 0;
}
```

### Advanced C++ Example with Templates

```cpp
#include <iostream>
#include <cstring>
#include <array>

enum class ModbusEndian {
    ABCD,  // Big-endian
    DCBA,  // Little-endian
    BADC,  // Big-endian byte swap
    CDAB   // Little-endian byte swap (mid-little-endian)
};

class ModbusConverter {
public:
    // Convert 32-bit value from Modbus registers
    template<typename T>
    static T from_registers(uint16_t reg_high, uint16_t reg_low, 
                           ModbusEndian order = ModbusEndian::ABCD) {
        static_assert(sizeof(T) == 4, "Only 32-bit types supported");
        
        uint32_t combined;
        
        switch (order) {
            case ModbusEndian::ABCD:  // Big-endian
                combined = (static_cast<uint32_t>(reg_high) << 16) | reg_low;
                break;
            case ModbusEndian::DCBA:  // Little-endian
                combined = (static_cast<uint32_t>(reg_low) << 16) | reg_high;
                combined = swap_bytes_32(combined);
                break;
            case ModbusEndian::BADC:  // Big-endian byte swap
                combined = (static_cast<uint32_t>(swap_bytes_16(reg_high)) << 16) | 
                           swap_bytes_16(reg_low);
                break;
            case ModbusEndian::CDAB:  // Little-endian byte swap
                combined = (static_cast<uint32_t>(reg_low) << 16) | reg_high;
                break;
        }
        
        T result;
        std::memcpy(&result, &combined, sizeof(T));
        return result;
    }
    
    // Convert value to Modbus registers
    template<typename T>
    static std::array<uint16_t, 2> to_registers(T value, 
                                                 ModbusEndian order = ModbusEndian::ABCD) {
        static_assert(sizeof(T) == 4, "Only 32-bit types supported");
        
        uint32_t temp;
        std::memcpy(&temp, &value, sizeof(T));
        
        std::array<uint16_t, 2> regs;
        
        switch (order) {
            case ModbusEndian::ABCD:
                regs[0] = (temp >> 16) & 0xFFFF;
                regs[1] = temp & 0xFFFF;
                break;
            case ModbusEndian::DCBA:
                temp = swap_bytes_32(temp);
                regs[0] = temp & 0xFFFF;
                regs[1] = (temp >> 16) & 0xFFFF;
                break;
            case ModbusEndian::BADC:
                regs[0] = swap_bytes_16((temp >> 16) & 0xFFFF);
                regs[1] = swap_bytes_16(temp & 0xFFFF);
                break;
            case ModbusEndian::CDAB:
                regs[0] = temp & 0xFFFF;
                regs[1] = (temp >> 16) & 0xFFFF;
                break;
        }
        
        return regs;
    }
    
private:
    static uint16_t swap_bytes_16(uint16_t value) {
        return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
    }
    
    static uint32_t swap_bytes_32(uint32_t value) {
        return ((value & 0xFF000000) >> 24) |
               ((value & 0x00FF0000) >> 8)  |
               ((value & 0x0000FF00) << 8)  |
               ((value & 0x000000FF) << 24);
    }
};

int main() {
    // Example with float
    float test_float = 123.456f;
    
    auto regs = ModbusConverter::to_registers(test_float, ModbusEndian::ABCD);
    std::cout << "Float " << test_float << " as registers (ABCD): "
              << std::hex << "0x" << regs[0] << " 0x" << regs[1] << std::dec << "\n";
    
    float recovered = ModbusConverter::from_registers<float>(regs[0], regs[1], 
                                                              ModbusEndian::ABCD);
    std::cout << "Recovered float: " << recovered << "\n";
    
    // Example with int32_t
    int32_t test_int = -987654321;
    auto int_regs = ModbusConverter::to_registers(test_int, ModbusEndian::CDAB);
    std::cout << "\nInt32 " << test_int << " as registers (CDAB): "
              << std::hex << "0x" << int_regs[0] << " 0x" << int_regs[1] << std::dec << "\n";
    
    return 0;
}
```

## Rust Implementation

```rust
use std::mem;

#[derive(Debug, Clone, Copy)]
pub enum ModbusEndian {
    ABCD,  // Big-endian
    DCBA,  // Little-endian
    BADC,  // Big-endian byte swap
    CDAB,  // Little-endian byte swap (mid-little-endian)
}

/// Modbus byte order converter
pub struct ModbusConverter;

impl ModbusConverter {
    /// Convert two 16-bit registers to a 32-bit float
    pub fn registers_to_f32(
        reg_high: u16,
        reg_low: u16,
        order: ModbusEndian,
    ) -> f32 {
        let combined = match order {
            ModbusEndian::ABCD => {
                ((reg_high as u32) << 16) | (reg_low as u32)
            }
            ModbusEndian::DCBA => {
                let temp = ((reg_low as u32) << 16) | (reg_high as u32);
                temp.swap_bytes()
            }
            ModbusEndian::BADC => {
                ((reg_high.swap_bytes() as u32) << 16) | (reg_low.swap_bytes() as u32)
            }
            ModbusEndian::CDAB => {
                ((reg_low as u32) << 16) | (reg_high as u32)
            }
        };
        
        f32::from_bits(combined)
    }
    
    /// Convert a 32-bit float to two 16-bit registers
    pub fn f32_to_registers(value: f32, order: ModbusEndian) -> [u16; 2] {
        let bits = value.to_bits();
        
        match order {
            ModbusEndian::ABCD => [
                ((bits >> 16) & 0xFFFF) as u16,
                (bits & 0xFFFF) as u16,
            ],
            ModbusEndian::DCBA => {
                let swapped = bits.swap_bytes();
                [
                    (swapped & 0xFFFF) as u16,
                    ((swapped >> 16) & 0xFFFF) as u16,
                ]
            }
            ModbusEndian::BADC => [
                (((bits >> 16) & 0xFFFF) as u16).swap_bytes(),
                ((bits & 0xFFFF) as u16).swap_bytes(),
            ],
            ModbusEndian::CDAB => [
                (bits & 0xFFFF) as u16,
                ((bits >> 16) & 0xFFFF) as u16,
            ],
        }
    }
    
    /// Convert two 16-bit registers to a 32-bit signed integer
    pub fn registers_to_i32(
        reg_high: u16,
        reg_low: u16,
        order: ModbusEndian,
    ) -> i32 {
        let combined = match order {
            ModbusEndian::ABCD => {
                ((reg_high as u32) << 16) | (reg_low as u32)
            }
            ModbusEndian::DCBA => {
                let temp = ((reg_low as u32) << 16) | (reg_high as u32);
                temp.swap_bytes()
            }
            ModbusEndian::BADC => {
                ((reg_high.swap_bytes() as u32) << 16) | (reg_low.swap_bytes() as u32)
            }
            ModbusEndian::CDAB => {
                ((reg_low as u32) << 16) | (reg_high as u32)
            }
        };
        
        combined as i32
    }
    
    /// Convert a 32-bit signed integer to two 16-bit registers
    pub fn i32_to_registers(value: i32, order: ModbusEndian) -> [u16; 2] {
        let bits = value as u32;
        
        match order {
            ModbusEndian::ABCD => [
                ((bits >> 16) & 0xFFFF) as u16,
                (bits & 0xFFFF) as u16,
            ],
            ModbusEndian::DCBA => {
                let swapped = bits.swap_bytes();
                [
                    (swapped & 0xFFFF) as u16,
                    ((swapped >> 16) & 0xFFFF) as u16,
                ]
            }
            ModbusEndian::BADC => [
                (((bits >> 16) & 0xFFFF) as u16).swap_bytes(),
                ((bits & 0xFFFF) as u16).swap_bytes(),
            ],
            ModbusEndian::CDAB => [
                (bits & 0xFFFF) as u16,
                ((bits >> 16) & 0xFFFF) as u16,
            ],
        }
    }
}

// Generic converter using traits
pub trait ModbusConvertible: Sized {
    fn from_registers(reg_high: u16, reg_low: u16, order: ModbusEndian) -> Self;
    fn to_registers(self, order: ModbusEndian) -> [u16; 2];
}

impl ModbusConvertible for f32 {
    fn from_registers(reg_high: u16, reg_low: u16, order: ModbusEndian) -> Self {
        ModbusConverter::registers_to_f32(reg_high, reg_low, order)
    }
    
    fn to_registers(self, order: ModbusEndian) -> [u16; 2] {
        ModbusConverter::f32_to_registers(self, order)
    }
}

impl ModbusConvertible for i32 {
    fn from_registers(reg_high: u16, reg_low: u16, order: ModbusEndian) -> Self {
        ModbusConverter::registers_to_i32(reg_high, reg_low, order)
    }
    
    fn to_registers(self, order: ModbusEndian) -> [u16; 2] {
        ModbusConverter::i32_to_registers(self, order)
    }
}

fn main() {
    // Test with float
    let test_float = 123.456_f32;
    println!("Original float: {}", test_float);
    
    // Convert to registers using different byte orders
    let regs_abcd = ModbusConverter::f32_to_registers(test_float, ModbusEndian::ABCD);
    println!("ABCD format: 0x{:04X} 0x{:04X}", regs_abcd[0], regs_abcd[1]);
    
    let regs_cdab = ModbusConverter::f32_to_registers(test_float, ModbusEndian::CDAB);
    println!("CDAB format: 0x{:04X} 0x{:04X}", regs_cdab[0], regs_cdab[1]);
    
    // Convert back
    let recovered_abcd = ModbusConverter::registers_to_f32(
        regs_abcd[0], regs_abcd[1], ModbusEndian::ABCD
    );
    println!("Recovered from ABCD: {}", recovered_abcd);
    
    // Test with i32
    let test_int: i32 = -987654321;
    println!("\nOriginal i32: {}", test_int);
    
    let int_regs = ModbusConverter::i32_to_registers(test_int, ModbusEndian::ABCD);
    println!("ABCD format: 0x{:04X} 0x{:04X}", int_regs[0], int_regs[1]);
    
    let recovered_int = ModbusConverter::registers_to_i32(
        int_regs[0], int_regs[1], ModbusEndian::ABCD
    );
    println!("Recovered i32: {}", recovered_int);
    
    // Using trait-based approach
    let value: f32 = f32::from_registers(0x42F6, 0xE979, ModbusEndian::ABCD);
    println!("\nTrait-based conversion: {}", value);
}
```

## Summary

**Endianness in Modbus** is critical for correctly interpreting multi-byte data types across different systems. Modbus transmits data in big-endian format over the network, but 32-bit values spanning multiple registers can be arranged in four different byte orders (ABCD, DCBA, BADC, CDAB).

**Key Points:**
- Always verify the byte order convention used by your Modbus devices
- The most common format is ABCD (big-endian) or CDAB (mid-little-endian)
- System endianness (x86 is little-endian) affects how you handle conversions
- Use memcpy or bit manipulation to safely convert between types
- In Rust, use built-in methods like `to_bits()`, `from_bits()`, and `swap_bytes()`
- Test thoroughly with known values to ensure correct byte order handling
- Document the byte order convention in your code and configuration

Proper endianness handling ensures reliable data exchange between Modbus masters and slaves, preventing data corruption and misinterpretation that could lead to incorrect readings, control errors, or system failures.