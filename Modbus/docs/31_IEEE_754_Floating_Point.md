# IEEE 754 Floating Point in Modbus

## Overview

IEEE 754 is the standard for representing floating-point numbers in computer systems. In Modbus communication, 32-bit floating-point values present a unique challenge because Modbus registers are only 16 bits wide. Therefore, a single float must be split across two consecutive registers for transmission and then reconstructed on the receiving end.

## The Challenge

**Modbus Register Structure:**
- Each Modbus register holds 16 bits (2 bytes)
- A 32-bit IEEE 754 float requires 4 bytes
- Solution: Use two consecutive registers

**IEEE 754 Single Precision Format (32-bit):**
```
Bit 31: Sign bit (0 = positive, 1 = negative)
Bits 30-23: Exponent (8 bits, biased by 127)
Bits 22-0: Mantissa/Fraction (23 bits)
```

## Byte Order Complications

When transmitting floats over Modbus, you must consider:

1. **Register Order**: Which register contains the high word vs. low word?
   - Big-endian (AB CD): High word first, then low word
   - Little-endian (CD AB): Low word first, then high word

2. **Byte Order within Registers**: How bytes are arranged within each register
   - Big-endian bytes (ABCD)
   - Little-endian bytes (DCBA)
   - Mid-big-endian (BADC)
   - Mid-little-endian (CDAB)

## C/C++ Implementation

```cpp
#include <iostream>
#include <cstdint>
#include <cstring>
#include <iomanip>

// Union for type punning between float and uint32_t
union FloatConverter {
    float f;
    uint32_t u;
    uint16_t regs[2];
};

// Encode a float into two Modbus registers (Big-Endian, ABCD)
void encodeFloatABCD(float value, uint16_t* registers) {
    FloatConverter conv;
    conv.f = value;
    
    // ABCD format: high word first, then low word
    registers[0] = (conv.u >> 16) & 0xFFFF;  // High word (AB)
    registers[1] = conv.u & 0xFFFF;           // Low word (CD)
}

// Decode two Modbus registers into a float (Big-Endian, ABCD)
float decodeFloatABCD(const uint16_t* registers) {
    FloatConverter conv;
    
    // Reconstruct 32-bit value from two 16-bit registers
    conv.u = ((uint32_t)registers[0] << 16) | registers[1];
    
    return conv.f;
}

// Encode a float into two Modbus registers (Little-Endian, CDAB)
void encodeFloatCDAB(float value, uint16_t* registers) {
    FloatConverter conv;
    conv.f = value;
    
    // CDAB format: low word first, then high word
    registers[0] = conv.u & 0xFFFF;           // Low word (CD)
    registers[1] = (conv.u >> 16) & 0xFFFF;  // High word (AB)
}

// Decode two Modbus registers into a float (Little-Endian, CDAB)
float decodeFloatCDAB(const uint16_t* registers) {
    FloatConverter conv;
    
    // Reconstruct 32-bit value (swap register order)
    conv.u = ((uint32_t)registers[1] << 16) | registers[0];
    
    return conv.f;
}

// Encode a float into two Modbus registers (Mid-Big-Endian, BADC)
void encodeFloatBADC(float value, uint16_t* registers) {
    FloatConverter conv;
    conv.f = value;
    
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&conv.u);
    
    // BADC: Swap bytes within each word
    registers[0] = (bytes[2] << 8) | bytes[3];  // BA
    registers[1] = (bytes[0] << 8) | bytes[1];  // DC
}

// Decode two Modbus registers into a float (Mid-Big-Endian, BADC)
float decodeFloatBADC(const uint16_t* registers) {
    FloatConverter conv;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&conv.u);
    
    // Extract and swap bytes
    bytes[3] = registers[0] & 0xFF;
    bytes[2] = (registers[0] >> 8) & 0xFF;
    bytes[1] = registers[1] & 0xFF;
    bytes[0] = (registers[1] >> 8) & 0xFF;
    
    return conv.f;
}

// Generic encoder with byte order selection
enum ByteOrder {
    ABCD,  // Big-endian
    CDAB,  // Little-endian
    BADC,  // Mid-big-endian
    DCBA   // Mid-little-endian
};

void encodeFloat(float value, uint16_t* registers, ByteOrder order) {
    switch(order) {
        case ABCD:
            encodeFloatABCD(value, registers);
            break;
        case CDAB:
            encodeFloatCDAB(value, registers);
            break;
        case BADC:
            encodeFloatBADC(value, registers);
            break;
        default:
            std::cerr << "Unsupported byte order" << std::endl;
    }
}

float decodeFloat(const uint16_t* registers, ByteOrder order) {
    switch(order) {
        case ABCD:
            return decodeFloatABCD(registers);
        case CDAB:
            return decodeFloatCDAB(registers);
        case BADC:
            return decodeFloatBADC(registers);
        default:
            std::cerr << "Unsupported byte order" << std::endl;
            return 0.0f;
    }
}

// Helper function to display register contents
void printRegisters(const uint16_t* registers) {
    std::cout << std::hex << std::setfill('0');
    std::cout << "Register[0]: 0x" << std::setw(4) << registers[0] << std::endl;
    std::cout << "Register[1]: 0x" << std::setw(4) << registers[1] << std::endl;
    std::cout << std::dec;
}

int main() {
    float testValue = 123.456f;
    uint16_t registers[2];
    
    std::cout << "Original value: " << testValue << std::endl << std::endl;
    
    // Test ABCD encoding
    std::cout << "=== ABCD Format (Big-Endian) ===" << std::endl;
    encodeFloatABCD(testValue, registers);
    printRegisters(registers);
    float decoded = decodeFloatABCD(registers);
    std::cout << "Decoded value: " << decoded << std::endl << std::endl;
    
    // Test CDAB encoding
    std::cout << "=== CDAB Format (Little-Endian) ===" << std::endl;
    encodeFloatCDAB(testValue, registers);
    printRegisters(registers);
    decoded = decodeFloatCDAB(registers);
    std::cout << "Decoded value: " << decoded << std::endl << std::endl;
    
    // Test BADC encoding
    std::cout << "=== BADC Format (Mid-Big-Endian) ===" << std::endl;
    encodeFloatBADC(testValue, registers);
    printRegisters(registers);
    decoded = decodeFloatBADC(registers);
    std::cout << "Decoded value: " << decoded << std::endl << std::endl;
    
    // Test special values
    std::cout << "=== Special Values ===" << std::endl;
    float specialValues[] = {0.0f, -0.0f, 1.0f, -1.0f, 3.14159f, 
                             INFINITY, -INFINITY};
    
    for (float val : specialValues) {
        encodeFloatABCD(val, registers);
        float result = decodeFloatABCD(registers);
        std::cout << "Value: " << std::setw(10) << val 
                  << " -> Decoded: " << result << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ByteOrder {
    ABCD,  // Big-endian (most common in Modbus)
    CDAB,  // Little-endian
    BADC,  // Mid-big-endian
    DCBA,  // Mid-little-endian
}

impl fmt::Display for ByteOrder {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ByteOrder::ABCD => write!(f, "ABCD (Big-Endian)"),
            ByteOrder::CDAB => write!(f, "CDAB (Little-Endian)"),
            ByteOrder::BADC => write!(f, "BADC (Mid-Big-Endian)"),
            ByteOrder::DCBA => write!(f, "DCBA (Mid-Little-Endian)"),
        }
    }
}

/// Encode a 32-bit float into two 16-bit Modbus registers
pub fn encode_float(value: f32, byte_order: ByteOrder) -> [u16; 2] {
    let bytes = value.to_bits();
    
    match byte_order {
        ByteOrder::ABCD => {
            // ABCD: High word first (big-endian)
            [
                ((bytes >> 16) & 0xFFFF) as u16,  // High word (AB)
                (bytes & 0xFFFF) as u16,           // Low word (CD)
            ]
        }
        ByteOrder::CDAB => {
            // CDAB: Low word first (little-endian)
            [
                (bytes & 0xFFFF) as u16,           // Low word (CD)
                ((bytes >> 16) & 0xFFFF) as u16,  // High word (AB)
            ]
        }
        ByteOrder::BADC => {
            // BADC: Swap bytes within each word
            let byte_array = bytes.to_be_bytes();
            [
                u16::from_be_bytes([byte_array[1], byte_array[0]]),  // BA
                u16::from_be_bytes([byte_array[3], byte_array[2]]),  // DC
            ]
        }
        ByteOrder::DCBA => {
            // DCBA: Reverse all bytes
            let byte_array = bytes.to_le_bytes();
            [
                u16::from_le_bytes([byte_array[1], byte_array[0]]),  // DC
                u16::from_le_bytes([byte_array[3], byte_array[2]]),  // BA
            ]
        }
    }
}

/// Decode two 16-bit Modbus registers into a 32-bit float
pub fn decode_float(registers: &[u16; 2], byte_order: ByteOrder) -> f32 {
    let bits: u32 = match byte_order {
        ByteOrder::ABCD => {
            // ABCD: High word first (big-endian)
            ((registers[0] as u32) << 16) | (registers[1] as u32)
        }
        ByteOrder::CDAB => {
            // CDAB: Low word first (little-endian)
            ((registers[1] as u32) << 16) | (registers[0] as u32)
        }
        ByteOrder::BADC => {
            // BADC: Swap bytes within each word
            let bytes = [
                (registers[1] >> 8) as u8,   // D
                (registers[1] & 0xFF) as u8, // C
                (registers[0] >> 8) as u8,   // B
                (registers[0] & 0xFF) as u8, // A
            ];
            u32::from_be_bytes(bytes)
        }
        ByteOrder::DCBA => {
            // DCBA: Reverse all bytes
            let bytes = [
                (registers[0] & 0xFF) as u8,  // D
                (registers[0] >> 8) as u8,    // C
                (registers[1] & 0xFF) as u8,  // B
                (registers[1] >> 8) as u8,    // A
            ];
            u32::from_be_bytes(bytes)
        }
    };
    
    f32::from_bits(bits)
}

/// Modbus register pair for holding a float value
#[derive(Debug, Clone, Copy)]
pub struct FloatRegisters {
    pub registers: [u16; 2],
    pub byte_order: ByteOrder,
}

impl FloatRegisters {
    pub fn new(value: f32, byte_order: ByteOrder) -> Self {
        Self {
            registers: encode_float(value, byte_order),
            byte_order,
        }
    }
    
    pub fn from_registers(registers: [u16; 2], byte_order: ByteOrder) -> Self {
        Self {
            registers,
            byte_order,
        }
    }
    
    pub fn get_value(&self) -> f32 {
        decode_float(&self.registers, self.byte_order)
    }
    
    pub fn set_value(&mut self, value: f32) {
        self.registers = encode_float(value, self.byte_order);
    }
    
    pub fn print_registers(&self) {
        println!("Register[0]: 0x{:04X}", self.registers[0]);
        println!("Register[1]: 0x{:04X}", self.registers[1]);
    }
}

fn main() {
    let test_value: f32 = 123.456;
    
    println!("Original value: {}\n", test_value);
    
    // Test all byte orders
    let byte_orders = [
        ByteOrder::ABCD,
        ByteOrder::CDAB,
        ByteOrder::BADC,
        ByteOrder::DCBA,
    ];
    
    for order in &byte_orders {
        println!("=== {} ===", order);
        let float_regs = FloatRegisters::new(test_value, *order);
        float_regs.print_registers();
        let decoded = float_regs.get_value();
        println!("Decoded value: {}\n", decoded);
    }
    
    // Test special values
    println!("=== Special Values (ABCD format) ===");
    let special_values = [
        0.0f32,
        -0.0f32,
        1.0f32,
        -1.0f32,
        std::f32::consts::PI,
        f32::INFINITY,
        f32::NEG_INFINITY,
        f32::NAN,
    ];
    
    for &val in &special_values {
        let registers = encode_float(val, ByteOrder::ABCD);
        let decoded = decode_float(&registers, ByteOrder::ABCD);
        
        println!(
            "Value: {:>12} -> Registers: [0x{:04X}, 0x{:04X}] -> Decoded: {}",
            format!("{}", val),
            registers[0],
            registers[1],
            decoded
        );
    }
    
    // Demonstrate mutability
    println!("\n=== Modifying Float Registers ===");
    let mut temp_sensor = FloatRegisters::new(25.5, ByteOrder::ABCD);
    println!("Initial temperature: {}", temp_sensor.get_value());
    
    temp_sensor.set_value(26.8);
    println!("Updated temperature: {}", temp_sensor.get_value());
    temp_sensor.print_registers();
    
    // Round-trip test
    println!("\n=== Round-Trip Accuracy Test ===");
    let test_values = vec![
        0.0, 1.0, -1.0, 0.1, -0.1, 100.5, -200.75,
        123.456, 3.14159265, 2.71828, 1e10, 1e-10,
    ];
    
    for &val in &test_values {
        let registers = encode_float(val, ByteOrder::ABCD);
        let decoded = decode_float(&registers, ByteOrder::ABCD);
        let error = (val - decoded).abs();
        
        println!(
            "Original: {:>15.10} -> Decoded: {:>15.10} | Error: {:.2e}",
            val, decoded, error
        );
    }
}
```

## Practical Considerations

### 1. **Determining Device Byte Order**

When working with a new Modbus device, you need to determine its byte order:

```c
// Test approach: Write a known value and read it back
float known_value = 1.0f;
uint16_t test_registers[2];

// Try ABCD encoding
encodeFloatABCD(known_value, test_registers);
// Write test_registers to device
// Read back from device into read_registers

// Compare all possible decodings
float decoded_abcd = decodeFloatABCD(read_registers);
float decoded_cdab = decodeFloatCDAB(read_registers);
// ... test others

// Whichever gives you 1.0 is the correct byte order
```

### 2. **Endianness and Modbus Standards**

Modbus RTU and TCP specifications don't mandate a specific byte order for multi-register values. This is left to device manufacturers, which is why you encounter different conventions:

- **Modicon convention** (original): ABCD (big-endian)
- **Some PLCs**: CDAB or BADC
- **Always check device documentation!**

### 3. **Error Handling**

When dealing with floats in embedded systems or industrial environments:

```rust
pub fn safe_decode_float(registers: &[u16; 2], byte_order: ByteOrder) -> Result<f32, &'static str> {
    let value = decode_float(registers, byte_order);
    
    if value.is_nan() {
        Err("Decoded value is NaN")
    } else if value.is_infinite() {
        Err("Decoded value is infinite")
    } else {
        Ok(value)
    }
}
```

### 4. **Performance Considerations**

- Unions (C/C++) provide zero-cost abstraction for type conversion
- Rust's `to_bits()` and `from_bits()` are optimized by the compiler
- Avoid unnecessary copying when working with register arrays
- For bulk operations, consider SIMD instructions

### 5. **Common Pitfalls**

1. **Assuming byte order**: Always verify with device documentation
2. **Endianness confusion**: System endianness ≠ Modbus byte order
3. **Register alignment**: Ensure floats start at even register addresses
4. **NaN handling**: Different NaN representations exist; test edge cases
5. **Precision loss**: IEEE 754 floats have limited precision (~7 decimal digits)

## Summary

**IEEE 754 floating-point encoding in Modbus** involves splitting 32-bit float values across two 16-bit registers. The main challenges are:

- **Register splitting**: 32 bits require two consecutive 16-bit registers
- **Byte order variations**: Four common formats (ABCD, CDAB, BADC, DCBA) exist depending on the device manufacturer
- **No standard**: Modbus specification doesn't mandate a specific byte order for floats

**Key implementation approaches:**
- **C/C++**: Use unions for efficient type punning between float and uint32_t
- **Rust**: Leverage `to_bits()` and `from_bits()` methods for safe, zero-cost conversion
- **Both languages**: Implement separate functions for each byte order or use generic functions with byte order parameters

**Best practices:**
- Always verify device byte order from manufacturer documentation
- Test with known values during commissioning
- Handle special float values (NaN, infinity) appropriately
- Consider precision limitations of 32-bit floats
- Use proper error handling for invalid decoded values

This topic is fundamental for industrial automation applications where sensor data (temperature, pressure, flow rates) is transmitted as floating-point values over Modbus networks.