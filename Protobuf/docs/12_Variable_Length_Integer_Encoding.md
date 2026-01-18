# Variable-Length Integer Encoding in Protocol Buffers

## Overview

Variable-length integer encoding (varint) is a fundamental optimization technique in Protocol Buffers that represents integers using a variable number of bytes rather than a fixed size. This encoding scheme is particularly effective for small numbers, which are common in many applications, by using fewer bytes to represent them.

## How Varint Encoding Works

### The Basic Mechanism

Varint encoding uses the most significant bit (MSB) of each byte as a continuation flag:
- **MSB = 1**: More bytes follow
- **MSB = 0**: This is the last byte

The remaining 7 bits of each byte store the actual data. Numbers are encoded in **little-endian** format (least significant group first).

### Encoding Process

For example, encoding the number **300**:

1. Binary representation: `100101100`
2. Split into 7-bit groups (right to left): `0000010` `0101100`
3. Reverse order (little-endian): `0101100` `0000010`
4. Add continuation bits: `10101100` `00000010`
5. Result: Two bytes `[0xAC, 0x02]`

### Size Implications

- **1 byte**: 0 to 127 (2^7 - 1)
- **2 bytes**: 128 to 16,383 (2^14 - 1)
- **3 bytes**: 16,384 to 2,097,151 (2^21 - 1)
- **5 bytes**: Up to 2^35 - 1
- **10 bytes**: Maximum for 64-bit integers

## C/C++ Code Examples

### Manual Varint Encoding

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Encode a uint64_t as varint
size_t encode_varint(uint64_t value, uint8_t* buffer) {
    size_t pos = 0;
    while (value >= 0x80) {
        buffer[pos++] = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    buffer[pos++] = (uint8_t)value;
    return pos;
}

// Decode a varint from buffer
size_t decode_varint(const uint8_t* buffer, size_t len, uint64_t* value) {
    *value = 0;
    size_t pos = 0;
    int shift = 0;
    
    while (pos < len) {
        uint8_t byte = buffer[pos++];
        *value |= (uint64_t)(byte & 0x7F) << shift;
        
        if ((byte & 0x80) == 0) {
            return pos; // Success
        }
        shift += 7;
        
        if (shift >= 64) {
            return 0; // Overflow
        }
    }
    return 0; // Incomplete varint
}

int main() {
    uint8_t buffer[10];
    uint64_t test_values[] = {0, 1, 127, 128, 300, 16384, UINT32_MAX};
    
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        size_t encoded_len = encode_varint(test_values[i], buffer);
        
        printf("Value: %lu -> ", test_values[i]);
        for (size_t j = 0; j < encoded_len; j++) {
            printf("0x%02X ", buffer[j]);
        }
        printf("(%zu bytes)\n", encoded_len);
        
        uint64_t decoded;
        decode_varint(buffer, encoded_len, &decoded);
        printf("Decoded: %lu\n\n", decoded);
    }
    
    return 0;
}
```

### Using Protocol Buffers in C++

```cpp
#include <iostream>
#include <fstream>
#include "example.pb.h"

// example.proto:
// syntax = "proto3";
// message IntegerExample {
//   int32 regular_int = 1;
//   sint32 signed_int = 2;
//   uint32 unsigned_int = 3;
//   fixed32 fixed_int = 4;
// }

int main() {
    IntegerExample example;
    
    // These use varint encoding
    example.set_regular_int(300);
    example.set_signed_int(-150);  // Uses zigzag encoding
    example.set_unsigned_int(300);
    
    // This uses fixed 4-byte encoding
    example.set_fixed_int(300);
    
    // Serialize to string
    std::string serialized;
    example.SerializeToString(&serialized);
    
    std::cout << "Serialized size: " << serialized.size() << " bytes\n";
    
    // Show bytes
    std::cout << "Bytes: ";
    for (unsigned char c : serialized) {
        printf("%02X ", c);
    }
    std::cout << "\n";
    
    // Deserialize
    IntegerExample parsed;
    parsed.ParseFromString(serialized);
    
    std::cout << "Parsed values:\n";
    std::cout << "  regular_int: " << parsed.regular_int() << "\n";
    std::cout << "  signed_int: " << parsed.signed_int() << "\n";
    std::cout << "  unsigned_int: " << parsed.unsigned_int() << "\n";
    std::cout << "  fixed_int: " << parsed.fixed_int() << "\n";
    
    return 0;
}
```

## Rust Code Examples

### Manual Varint Implementation

```rust
// Encode a u64 as varint
fn encode_varint(mut value: u64, buffer: &mut Vec<u8>) {
    while value >= 0x80 {
        buffer.push((value as u8) | 0x80);
        value >>= 7;
    }
    buffer.push(value as u8);
}

// Decode a varint from a slice
fn decode_varint(buffer: &[u8]) -> Option<(u64, usize)> {
    let mut value: u64 = 0;
    let mut shift = 0;
    
    for (pos, &byte) in buffer.iter().enumerate() {
        if shift >= 64 {
            return None; // Overflow
        }
        
        value |= ((byte & 0x7F) as u64) << shift;
        
        if byte & 0x80 == 0 {
            return Some((value, pos + 1));
        }
        
        shift += 7;
    }
    
    None // Incomplete varint
}

fn main() {
    let test_values = vec![0u64, 1, 127, 128, 300, 16384, u32::MAX as u64];
    
    for value in test_values {
        let mut buffer = Vec::new();
        encode_varint(value, &mut buffer);
        
        print!("Value: {} -> ", value);
        for byte in &buffer {
            print!("0x{:02X} ", byte);
        }
        println!("({} bytes)", buffer.len());
        
        if let Some((decoded, _)) = decode_varint(&buffer) {
            println!("Decoded: {}\n", decoded);
        }
    }
}
```

### Using prost (Rust Protobuf Library)

```rust
// In Cargo.toml:
// [dependencies]
// prost = "0.12"
// [build-dependencies]
// prost-build = "0.12"

use prost::Message;

// Generated from:
// syntax = "proto3";
// message IntegerExample {
//   int32 regular_int = 1;
//   sint32 signed_int = 2;
//   uint32 unsigned_int = 3;
//   fixed32 fixed_int = 4;
// }

#[derive(Clone, PartialEq, Message)]
pub struct IntegerExample {
    #[prost(int32, tag = "1")]
    pub regular_int: i32,
    
    #[prost(sint32, tag = "2")]
    pub signed_int: i32,
    
    #[prost(uint32, tag = "3")]
    pub unsigned_int: u32,
    
    #[prost(fixed32, tag = "4")]
    pub fixed_int: u32,
}

fn main() {
    let example = IntegerExample {
        regular_int: 300,
        signed_int: -150,
        unsigned_int: 300,
        fixed_int: 300,
    };
    
    // Serialize
    let mut buffer = Vec::new();
    example.encode(&mut buffer).unwrap();
    
    println!("Serialized size: {} bytes", buffer.len());
    print!("Bytes: ");
    for byte in &buffer {
        print!("{:02X} ", byte);
    }
    println!();
    
    // Deserialize
    let parsed = IntegerExample::decode(&buffer[..]).unwrap();
    
    println!("Parsed values:");
    println!("  regular_int: {}", parsed.regular_int);
    println!("  signed_int: {}", parsed.signed_int);
    println!("  unsigned_int: {}", parsed.unsigned_int);
    println!("  fixed_int: {}", parsed.fixed_int);
}
```

## Integer Type Selection Guide

### Standard Varint Types

**`int32` / `int64`**: Use for non-negative or small negative numbers
- Negative numbers are inefficient (always 10 bytes for int64)
- Best for: counters, IDs, small ranges

**`uint32` / `uint64`**: Use for non-negative numbers only
- Same encoding as int32/int64 but semantically unsigned
- Best for: sizes, counts, unsigned IDs

### Optimized Signed Integers

**`sint32` / `sint64`**: Use for signed numbers that may be negative
- Uses zigzag encoding: maps signed integers to unsigned
  - 0 → 0, -1 → 1, 1 → 2, -2 → 3, 2 → 4...
- Best for: temperatures, deltas, coordinates

### Fixed-Size Types

**`fixed32` / `fixed64`**: Always use 4/8 bytes
- More efficient when values are typically > 2^28 (for fixed32) or 2^56 (for fixed64)
- Best for: hashes, large numbers, uniformly distributed values

**`sfixed32` / `sfixed64`**: Signed fixed-size
- Same as fixed32/64 but for signed values
- Best for: signed large numbers

## Performance Comparison

```rust
// Size comparison for different values
fn compare_encodings() {
    let values = vec![
        ("Small (100)", 100u64),
        ("Medium (10000)", 10000),
        ("Large (1000000)", 1000000),
        ("Very Large (2^32)", u32::MAX as u64),
    ];
    
    for (label, value) in values {
        let mut varint_buf = Vec::new();
        encode_varint(value, &mut varint_buf);
        
        let fixed_size = 8; // fixed64
        
        println!("{}: varint={} bytes, fixed64={} bytes", 
                 label, varint_buf.len(), fixed_size);
    }
}
// Output:
// Small (100): varint=1 bytes, fixed64=8 bytes
// Medium (10000): varint=2 bytes, fixed64=8 bytes
// Large (1000000): varint=3 bytes, fixed64=8 bytes
// Very Large (2^32): varint=5 bytes, fixed64=8 bytes
```

## Summary

Variable-length integer encoding is a space-efficient technique that adaptively uses 1-10 bytes based on the magnitude of the integer value. It works by using 7 bits per byte for data and 1 bit as a continuation flag, encoding numbers in little-endian order. For optimal performance, choose `int32/uint32` for small non-negative values, `sint32/sint64` for signed values that may be negative (using zigzag encoding), and `fixed32/fixed64` only when values are consistently large (above 2^28 or 2^56). This encoding makes Protocol Buffers highly efficient for common use cases where most integers are small, while still supporting the full range of 32-bit and 64-bit values when needed.