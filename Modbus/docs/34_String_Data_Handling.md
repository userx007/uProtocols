# String Data Handling in Modbus

## Overview

String data handling in Modbus involves encoding and decoding ASCII strings that are stored across multiple 16-bit registers. Since Modbus registers are 16-bit values but ASCII characters are 8-bit, each register can store two ASCII characters. This technique is commonly used for transmitting device names, serial numbers, configuration strings, firmware versions, and other textual information through the Modbus protocol.

## Technical Details

### Character Encoding in Registers

Each 16-bit Modbus register can hold two 8-bit ASCII characters. The byte order (endianness) determines how these characters are packed:

- **Big-endian (High byte first)**: The first character occupies the high byte (bits 15-8), the second character occupies the low byte (bits 7-0)
- **Little-endian (Low byte first)**: The first character occupies the low byte, the second character occupies the high byte

### Common Conventions

1. **Character Packing**: Two ASCII characters per register
2. **String Termination**: Strings may be null-terminated (`\0`) or fixed-length with padding
3. **Byte Order**: Varies by device manufacturer (commonly big-endian for strings)
4. **Padding**: Unused portions filled with null bytes or spaces

### Typical Use Cases

- Device identification strings
- Model numbers and serial numbers
- Firmware version strings
- Configuration parameters
- User-defined labels
- Error messages

## C/C++ Implementation

```cpp
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Encode ASCII string into Modbus registers (Big-endian)
// Two characters per register: High byte = first char, Low byte = second char
void encode_string_to_registers_be(const char* str, uint16_t* registers, size_t num_registers) {
    size_t str_len = strlen(str);
    size_t max_chars = num_registers * 2;
    
    for (size_t i = 0; i < num_registers; i++) {
        uint8_t high_byte = 0;
        uint8_t low_byte = 0;
        
        size_t char_idx = i * 2;
        
        // First character (high byte)
        if (char_idx < str_len) {
            high_byte = (uint8_t)str[char_idx];
        }
        
        // Second character (low byte)
        if (char_idx + 1 < str_len) {
            low_byte = (uint8_t)str[char_idx + 1];
        }
        
        registers[i] = (high_byte << 8) | low_byte;
    }
}

// Encode ASCII string into Modbus registers (Little-endian)
void encode_string_to_registers_le(const char* str, uint16_t* registers, size_t num_registers) {
    size_t str_len = strlen(str);
    
    for (size_t i = 0; i < num_registers; i++) {
        uint8_t low_byte = 0;
        uint8_t high_byte = 0;
        
        size_t char_idx = i * 2;
        
        // First character (low byte)
        if (char_idx < str_len) {
            low_byte = (uint8_t)str[char_idx];
        }
        
        // Second character (high byte)
        if (char_idx + 1 < str_len) {
            high_byte = (uint8_t)str[char_idx + 1];
        }
        
        registers[i] = (high_byte << 8) | low_byte;
    }
}

// Decode Modbus registers to ASCII string (Big-endian)
void decode_registers_to_string_be(const uint16_t* registers, size_t num_registers, 
                                   char* str, size_t str_buffer_size) {
    size_t max_chars = num_registers * 2;
    size_t str_idx = 0;
    
    for (size_t i = 0; i < num_registers && str_idx < str_buffer_size - 1; i++) {
        uint8_t high_byte = (registers[i] >> 8) & 0xFF;
        uint8_t low_byte = registers[i] & 0xFF;
        
        // Add high byte (first character)
        if (high_byte != 0 && str_idx < str_buffer_size - 1) {
            str[str_idx++] = (char)high_byte;
        } else if (high_byte == 0) {
            break; // Null terminator found
        }
        
        // Add low byte (second character)
        if (low_byte != 0 && str_idx < str_buffer_size - 1) {
            str[str_idx++] = (char)low_byte;
        } else if (low_byte == 0) {
            break; // Null terminator found
        }
    }
    
    str[str_idx] = '\0'; // Null terminate
}

// Decode Modbus registers to ASCII string (Little-endian)
void decode_registers_to_string_le(const uint16_t* registers, size_t num_registers,
                                   char* str, size_t str_buffer_size) {
    size_t str_idx = 0;
    
    for (size_t i = 0; i < num_registers && str_idx < str_buffer_size - 1; i++) {
        uint8_t low_byte = registers[i] & 0xFF;
        uint8_t high_byte = (registers[i] >> 8) & 0xFF;
        
        // Add low byte (first character)
        if (low_byte != 0 && str_idx < str_buffer_size - 1) {
            str[str_idx++] = (char)low_byte;
        } else if (low_byte == 0) {
            break;
        }
        
        // Add high byte (second character)
        if (high_byte != 0 && str_idx < str_buffer_size - 1) {
            str[str_idx++] = (char)high_byte;
        } else if (high_byte == 0) {
            break;
        }
    }
    
    str[str_idx] = '\0';
}

// Helper function to print registers in hex
void print_registers(const uint16_t* registers, size_t count) {
    printf("Registers: ");
    for (size_t i = 0; i < count; i++) {
        printf("0x%04X ", registers[i]);
    }
    printf("\n");
}

int main() {
    const char* device_name = "SENSOR-101";
    const char* firmware_ver = "v2.3.4";
    
    uint16_t name_registers[8] = {0}; // 16 characters max
    uint16_t fw_registers[4] = {0};   // 8 characters max
    
    printf("=== Big-Endian Encoding ===\n");
    
    // Encode device name
    encode_string_to_registers_be(device_name, name_registers, 8);
    printf("Device Name: '%s'\n", device_name);
    print_registers(name_registers, 8);
    
    // Decode device name
    char decoded_name[17] = {0};
    decode_registers_to_string_be(name_registers, 8, decoded_name, sizeof(decoded_name));
    printf("Decoded Name: '%s'\n\n", decoded_name);
    
    // Encode firmware version
    encode_string_to_registers_be(firmware_ver, fw_registers, 4);
    printf("Firmware Version: '%s'\n", firmware_ver);
    print_registers(fw_registers, 4);
    
    // Decode firmware version
    char decoded_fw[9] = {0};
    decode_registers_to_string_be(fw_registers, 4, decoded_fw, sizeof(decoded_fw));
    printf("Decoded Version: '%s'\n\n", decoded_fw);
    
    printf("=== Little-Endian Encoding ===\n");
    
    // Clear registers
    memset(name_registers, 0, sizeof(name_registers));
    
    // Encode with little-endian
    encode_string_to_registers_le(device_name, name_registers, 8);
    printf("Device Name: '%s'\n", device_name);
    print_registers(name_registers, 8);
    
    // Decode with little-endian
    memset(decoded_name, 0, sizeof(decoded_name));
    decode_registers_to_string_le(name_registers, 8, decoded_name, sizeof(decoded_name));
    printf("Decoded Name: '%s'\n", decoded_name);
    
    return 0;
}
```

## Rust Implementation


```rust
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Endianness {
    Big,
    Little,
}

/// Encodes an ASCII string into Modbus registers
pub fn encode_string_to_registers(
    s: &str,
    num_registers: usize,
    endianness: Endianness,
) -> Vec<u16> {
    let mut registers = vec![0u16; num_registers];
    let bytes = s.as_bytes();
    
    for i in 0..num_registers {
        let idx = i * 2;
        let first_byte = if idx < bytes.len() { bytes[idx] } else { 0 };
        let second_byte = if idx + 1 < bytes.len() { bytes[idx + 1] } else { 0 };
        
        registers[i] = match endianness {
            Endianness::Big => ((first_byte as u16) << 8) | (second_byte as u16),
            Endianness::Little => ((second_byte as u16) << 8) | (first_byte as u16),
        };
    }
    
    registers
}

/// Decodes Modbus registers into an ASCII string
pub fn decode_registers_to_string(
    registers: &[u16],
    endianness: Endianness,
) -> String {
    let mut bytes = Vec::new();
    
    for &reg in registers {
        let (first_byte, second_byte) = match endianness {
            Endianness::Big => ((reg >> 8) as u8, (reg & 0xFF) as u8),
            Endianness::Little => ((reg & 0xFF) as u8, (reg >> 8) as u8),
        };
        
        // Stop at null terminator
        if first_byte == 0 {
            break;
        }
        bytes.push(first_byte);
        
        if second_byte == 0 {
            break;
        }
        bytes.push(second_byte);
    }
    
    // Convert to string, replacing invalid UTF-8 sequences
    String::from_utf8_lossy(&bytes).into_owned()
}

/// A Modbus string wrapper that handles encoding/decoding
pub struct ModbusString {
    data: Vec<u16>,
    endianness: Endianness,
}

impl ModbusString {
    /// Creates a new ModbusString from a string slice
    pub fn new(s: &str, num_registers: usize, endianness: Endianness) -> Self {
        let data = encode_string_to_registers(s, num_registers, endianness);
        Self { data, endianness }
    }
    
    /// Creates a ModbusString from existing registers
    pub fn from_registers(registers: Vec<u16>, endianness: Endianness) -> Self {
        Self {
            data: registers,
            endianness,
        }
    }
    
    /// Returns the registers
    pub fn registers(&self) -> &[u16] {
        &self.data
    }
    
    /// Decodes to a String
    pub fn to_string(&self) -> String {
        decode_registers_to_string(&self.data, self.endianness)
    }
    
    /// Returns the endianness
    pub fn endianness(&self) -> Endianness {
        self.endianness
    }
}

impl fmt::Display for ModbusString {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_string())
    }
}

/// Example: Device information structure
#[derive(Debug)]
pub struct DeviceInfo {
    pub model: ModbusString,
    pub serial_number: ModbusString,
    pub firmware_version: ModbusString,
}

impl DeviceInfo {
    pub fn new(model: &str, serial: &str, firmware: &str) -> Self {
        Self {
            model: ModbusString::new(model, 8, Endianness::Big),
            serial_number: ModbusString::new(serial, 8, Endianness::Big),
            firmware_version: ModbusString::new(firmware, 4, Endianness::Big),
        }
    }
    
    pub fn display(&self) {
        println!("Model: {}", self.model);
        println!("Serial: {}", self.serial_number);
        println!("Firmware: {}", self.firmware_version);
    }
}

fn print_registers(registers: &[u16], label: &str) {
    print!("{}: ", label);
    for reg in registers {
        print!("0x{:04X} ", reg);
    }
    println!();
}

fn main() {
    println!("=== Modbus String Handling Demo ===\n");
    
    // Example 1: Basic encoding/decoding
    let device_name = "SENSOR-101";
    println!("Original String: '{}'", device_name);
    
    let registers_be = encode_string_to_registers(device_name, 8, Endianness::Big);
    print_registers(&registers_be, "Big-Endian   ");
    
    let registers_le = encode_string_to_registers(device_name, 8, Endianness::Little);
    print_registers(&registers_le, "Little-Endian");
    
    let decoded_be = decode_registers_to_string(&registers_be, Endianness::Big);
    let decoded_le = decode_registers_to_string(&registers_le, Endianness::Little);
    
    println!("Decoded (BE): '{}'", decoded_be);
    println!("Decoded (LE): '{}'\n", decoded_le);
    
    // Example 2: Using ModbusString wrapper
    println!("=== ModbusString Wrapper ===");
    let modbus_str = ModbusString::new("Temperature", 8, Endianness::Big);
    print_registers(modbus_str.registers(), "Registers");
    println!("Decoded: '{}'\n", modbus_str);
    
    // Example 3: Device information
    println!("=== Device Information ===");
    let device = DeviceInfo::new(
        "MB-CTRL-500",
        "SN12345678",
        "v1.2.3"
    );
    device.display();
    
    println!("\nRegister Details:");
    print_registers(device.model.registers(), "Model     ");
    print_registers(device.serial_number.registers(), "Serial    ");
    print_registers(device.firmware_version.registers(), "Firmware  ");
    
    // Example 4: Handling strings with various lengths
    println!("\n=== Variable Length Strings ===");
    let test_strings = vec!["A", "AB", "ABC", "ABCDEFGH", "ABCDEFGHIJKLMNOP"];
    
    for s in test_strings {
        let regs = encode_string_to_registers(s, 4, Endianness::Big);
        let decoded = decode_registers_to_string(&regs, Endianness::Big);
        println!("'{}' -> {:?} -> '{}'", s, regs, decoded);
    }
    
    // Example 5: Comparison of endianness
    println!("\n=== Endianness Comparison ===");
    let test = "MODBUS";
    let be_regs = encode_string_to_registers(test, 4, Endianness::Big);
    let le_regs = encode_string_to_registers(test, 4, Endianness::Little);
    
    println!("String: '{}'", test);
    for i in 0..4 {
        println!(
            "Reg {}: BE=0x{:04X} ({:08b} {:08b})  LE=0x{:04X} ({:08b} {:08b})",
            i,
            be_regs[i], (be_regs[i] >> 8) as u8, (be_regs[i] & 0xFF) as u8,
            le_regs[i], (le_regs[i] >> 8) as u8, (le_regs[i] & 0xFF) as u8
        );
    }
}
```

## Key Concepts Explained

### 1. **Two Characters Per Register**
Since Modbus registers are 16-bit and ASCII characters are 8-bit, each register holds exactly two characters. For the string "SENSOR", you would need 3 registers.

### 2. **Endianness Matters**
- **Big-endian**: "AB" becomes `0x4142` (A=0x41, B=0x42)
- **Little-endian**: "AB" becomes `0x4241` (B in high byte, A in low byte)

### 3. **Null Termination**
Strings are typically null-terminated (`\0`) to indicate the end. Remaining register space is filled with zeros.

### 4. **Fixed vs Variable Length**
- **Fixed-length**: Always uses the same number of registers, padding with nulls
- **Variable-length**: Uses only as many registers as needed, with length prefix or terminator

### 5. **Buffer Management**
Always allocate sufficient buffer space when decoding to prevent overflows. The maximum string length is `num_registers * 2` characters.

## Practical Considerations

**Device Compatibility**: Check the device documentation for:
- Expected byte order (big-endian vs little-endian)
- Padding convention (null bytes, spaces, or 0xFF)
- Maximum string lengths
- Register addresses for string data

**Performance**: String operations are relatively slow compared to numeric data transfers. Cache decoded strings when possible rather than decoding on every read.

**Error Handling**: Validate that registers contain valid ASCII characters (0x00-0x7F) and handle non-printable characters appropriately.

**Memory Safety**: In C/C++, always null-terminate decoded strings and check buffer bounds. Rust's type system provides built-in protection against buffer overflows.

## Summary

String data handling in Modbus enables transmission of textual information through the register-based protocol by encoding ASCII characters into 16-bit registers. Each register stores two characters, with byte order determined by the device's endianness convention. The implementations in C/C++ and Rust demonstrate both encoding (string to registers) and decoding (registers to string) operations, supporting both big-endian and little-endian formats. Proper handling requires attention to null termination, buffer sizing, endianness, and character validation. This technique is essential for reading device identifiers, configuration strings, firmware versions, and other human-readable data from Modbus devices, making it a fundamental capability for industrial automation and SCADA systems.