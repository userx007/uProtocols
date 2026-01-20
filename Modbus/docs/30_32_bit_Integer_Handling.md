# 32-bit Integer Handling in Modbus

## Overview

Modbus is fundamentally a 16-bit protocol - each register holds exactly 16 bits of data. However, modern applications frequently need to work with 32-bit integers, floating-point values, and other data types that exceed this 16-bit limitation. The solution is to combine two consecutive 16-bit registers to represent a single 32-bit value.

The challenge lies in the byte and word ordering - different manufacturers and systems may arrange the bytes differently, leading to potential data corruption if not handled correctly.

## Understanding the Problem

A 32-bit integer consists of 4 bytes. When stored across two Modbus registers, there are multiple ways to arrange these bytes:

- **Register order**: Which register comes first (high word vs low word)
- **Byte order within each register**: Big-endian vs little-endian

This creates four possible arrangements, commonly referred to as:

1. **Big-endian (ABCD)**: Most significant word in first register, big-endian bytes
2. **Little-endian (DCBA)**: Least significant word in first register, little-endian bytes
3. **Big-endian byte swap (BADC)**: Most significant word first, but bytes swapped within each word
4. **Little-endian byte swap (CDAB)**: Least significant word first, bytes swapped within each word

## Practical Implications

When reading a 32-bit value like `0x12345678`:
- **ABCD**: Registers contain `[0x1234, 0x5678]`
- **DCBA**: Registers contain `[0x7856, 0x3412]`
- **BADC**: Registers contain `[0x3412, 0x7856]`
- **CDAB**: Registers contain `[0x5678, 0x1234]`

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdio.h>

// Enumeration for byte order types
typedef enum {
    BYTE_ORDER_ABCD,  // Big-endian
    BYTE_ORDER_DCBA,  // Little-endian
    BYTE_ORDER_BADC,  // Big-endian with byte swap
    BYTE_ORDER_CDAB   // Little-endian with byte swap
} ByteOrder;

// Convert two 16-bit registers to 32-bit integer
int32_t registers_to_int32(uint16_t reg_high, uint16_t reg_low, ByteOrder order) {
    uint32_t result;
    uint8_t bytes[4];
    
    switch(order) {
        case BYTE_ORDER_ABCD:  // Big-endian
            bytes[0] = (reg_high >> 8) & 0xFF;
            bytes[1] = reg_high & 0xFF;
            bytes[2] = (reg_low >> 8) & 0xFF;
            bytes[3] = reg_low & 0xFF;
            break;
            
        case BYTE_ORDER_DCBA:  // Little-endian
            bytes[3] = (reg_high >> 8) & 0xFF;
            bytes[2] = reg_high & 0xFF;
            bytes[1] = (reg_low >> 8) & 0xFF;
            bytes[0] = reg_low & 0xFF;
            break;
            
        case BYTE_ORDER_BADC:  // Big-endian byte swap
            bytes[1] = (reg_high >> 8) & 0xFF;
            bytes[0] = reg_high & 0xFF;
            bytes[3] = (reg_low >> 8) & 0xFF;
            bytes[2] = reg_low & 0xFF;
            break;
            
        case BYTE_ORDER_CDAB:  // Little-endian byte swap
            bytes[2] = (reg_high >> 8) & 0xFF;
            bytes[3] = reg_high & 0xFF;
            bytes[0] = (reg_low >> 8) & 0xFF;
            bytes[1] = reg_low & 0xFF;
            break;
    }
    
    result = ((uint32_t)bytes[0] << 24) |
             ((uint32_t)bytes[1] << 16) |
             ((uint32_t)bytes[2] << 8) |
             ((uint32_t)bytes[3]);
    
    return (int32_t)result;
}

// Convert 32-bit integer to two 16-bit registers
void int32_to_registers(int32_t value, uint16_t *reg_high, uint16_t *reg_low, ByteOrder order) {
    uint8_t bytes[4];
    
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;
    
    switch(order) {
        case BYTE_ORDER_ABCD:  // Big-endian
            *reg_high = (bytes[0] << 8) | bytes[1];
            *reg_low = (bytes[2] << 8) | bytes[3];
            break;
            
        case BYTE_ORDER_DCBA:  // Little-endian
            *reg_high = (bytes[3] << 8) | bytes[2];
            *reg_low = (bytes[1] << 8) | bytes[0];
            break;
            
        case BYTE_ORDER_BADC:  // Big-endian byte swap
            *reg_high = (bytes[1] << 8) | bytes[0];
            *reg_low = (bytes[3] << 8) | bytes[2];
            break;
            
        case BYTE_ORDER_CDAB:  // Little-endian byte swap
            *reg_high = (bytes[2] << 8) | bytes[3];
            *reg_low = (bytes[0] << 8) | bytes[1];
            break;
    }
}

// Example usage
int main() {
    uint16_t regs[2] = {0x1234, 0x5678};
    int32_t value;
    
    // Read as big-endian
    value = registers_to_int32(regs[0], regs[1], BYTE_ORDER_ABCD);
    printf("ABCD: 0x%08X (%d)\n", value, value);
    
    // Read as little-endian
    value = registers_to_int32(regs[0], regs[1], BYTE_ORDER_CDAB);
    printf("CDAB: 0x%08X (%d)\n", value, value);
    
    // Write a value
    int32_t test_value = 305419896;  // 0x12345678
    uint16_t write_regs[2];
    int32_to_registers(test_value, &write_regs[0], &write_regs[1], BYTE_ORDER_ABCD);
    printf("\nWriting 0x%08X as ABCD: [0x%04X, 0x%04X]\n", 
           test_value, write_regs[0], write_regs[1]);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::convert::TryInto;

#[derive(Debug, Clone, Copy)]
pub enum ByteOrder {
    ABCD,  // Big-endian
    DCBA,  // Little-endian
    BADC,  // Big-endian with byte swap
    CDAB,  // Little-endian with byte swap
}

/// Convert two 16-bit registers to a 32-bit integer
pub fn registers_to_i32(reg_high: u16, reg_low: u16, order: ByteOrder) -> i32 {
    let bytes: [u8; 4] = match order {
        ByteOrder::ABCD => [
            (reg_high >> 8) as u8,
            reg_high as u8,
            (reg_low >> 8) as u8,
            reg_low as u8,
        ],
        ByteOrder::DCBA => [
            reg_low as u8,
            (reg_low >> 8) as u8,
            reg_high as u8,
            (reg_high >> 8) as u8,
        ],
        ByteOrder::BADC => [
            reg_high as u8,
            (reg_high >> 8) as u8,
            reg_low as u8,
            (reg_low >> 8) as u8,
        ],
        ByteOrder::CDAB => [
            (reg_low >> 8) as u8,
            reg_low as u8,
            (reg_high >> 8) as u8,
            reg_high as u8,
        ],
    };
    
    i32::from_be_bytes(bytes)
}

/// Convert a 32-bit integer to two 16-bit registers
pub fn i32_to_registers(value: i32, order: ByteOrder) -> (u16, u16) {
    let bytes = value.to_be_bytes();
    
    match order {
        ByteOrder::ABCD => {
            let reg_high = u16::from_be_bytes([bytes[0], bytes[1]]);
            let reg_low = u16::from_be_bytes([bytes[2], bytes[3]]);
            (reg_high, reg_low)
        }
        ByteOrder::DCBA => {
            let reg_high = u16::from_be_bytes([bytes[3], bytes[2]]);
            let reg_low = u16::from_be_bytes([bytes[1], bytes[0]]);
            (reg_high, reg_low)
        }
        ByteOrder::BADC => {
            let reg_high = u16::from_be_bytes([bytes[1], bytes[0]]);
            let reg_low = u16::from_be_bytes([bytes[3], bytes[2]]);
            (reg_high, reg_low)
        }
        ByteOrder::CDAB => {
            let reg_high = u16::from_be_bytes([bytes[2], bytes[3]]);
            let reg_low = u16::from_be_bytes([bytes[0], bytes[1]]);
            (reg_high, reg_low)
        }
    }
}

/// More idiomatic Rust approach using traits
pub trait ModbusConvert {
    fn from_registers(reg_high: u16, reg_low: u16, order: ByteOrder) -> Self;
    fn to_registers(&self, order: ByteOrder) -> (u16, u16);
}

impl ModbusConvert for i32 {
    fn from_registers(reg_high: u16, reg_low: u16, order: ByteOrder) -> Self {
        registers_to_i32(reg_high, reg_low, order)
    }
    
    fn to_registers(&self, order: ByteOrder) -> (u16, u16) {
        i32_to_registers(*self, order)
    }
}

impl ModbusConvert for u32 {
    fn from_registers(reg_high: u16, reg_low: u16, order: ByteOrder) -> Self {
        registers_to_i32(reg_high, reg_low, order) as u32
    }
    
    fn to_registers(&self, order: ByteOrder) -> (u16, u16) {
        i32_to_registers(*self as i32, order)
    }
}

// Example usage
fn main() {
    let regs = [0x1234u16, 0x5678u16];
    
    // Read as different byte orders
    println!("Reading [0x{:04X}, 0x{:04X}]:", regs[0], regs[1]);
    
    let value_abcd = registers_to_i32(regs[0], regs[1], ByteOrder::ABCD);
    println!("  ABCD: 0x{:08X} ({})", value_abcd, value_abcd);
    
    let value_cdab = registers_to_i32(regs[0], regs[1], ByteOrder::CDAB);
    println!("  CDAB: 0x{:08X} ({})", value_cdab, value_cdab);
    
    let value_badc = registers_to_i32(regs[0], regs[1], ByteOrder::BADC);
    println!("  BADC: 0x{:08X} ({})", value_badc, value_badc);
    
    let value_dcba = registers_to_i32(regs[0], regs[1], ByteOrder::DCBA);
    println!("  DCBA: 0x{:08X} ({})", value_dcba, value_dcba);
    
    // Write a value
    let test_value = 0x12345678i32;
    let (high, low) = i32_to_registers(test_value, ByteOrder::ABCD);
    println!("\nWriting 0x{:08X} as ABCD: [0x{:04X}, 0x{:04X}]", 
             test_value, high, low);
    
    // Using trait approach
    let value: i32 = ModbusConvert::from_registers(0x1234, 0x5678, ByteOrder::ABCD);
    let (h, l) = value.to_registers(ByteOrder::ABCD);
    println!("\nUsing trait: value={}, registers=[0x{:04X}, 0x{:04X}]", value, h, l);
}
```

## Best Practices

1. **Always verify byte order**: Check device documentation or test with known values before deployment
2. **Use configuration**: Make byte order configurable rather than hardcoded
3. **Test thoroughly**: Use test patterns like `0x12345678` to verify correct interpretation
4. **Document assumptions**: Clearly document which byte order your code expects
5. **Handle signed/unsigned**: Be explicit about whether you're working with signed or unsigned 32-bit integers
6. **Consider endianness**: Remember that Modbus registers themselves are transmitted big-endian on the wire, but this is separate from how multi-register values are organized

## Summary

Handling 32-bit integers in Modbus requires combining two consecutive 16-bit registers while accounting for byte and word ordering. The four common arrangements (ABCD, DCBA, BADC, CDAB) represent different combinations of register order and byte order within registers. Successful implementation requires understanding the specific byte order used by your device, implementing proper conversion functions, and thoroughly testing with known values. Both C/C++ and Rust can elegantly handle these conversions through bitwise operations and proper type handling, with Rust offering additional type safety through its trait system. Always consult device documentation and verify with test data before deploying in production systems.