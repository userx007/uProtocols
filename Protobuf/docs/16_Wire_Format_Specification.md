# Protocol Buffers Wire Format Specification

## Overview

The Protocol Buffers wire format is the binary encoding scheme that serializes structured data into a compact, efficient byte stream. Understanding the wire format is crucial for debugging, implementing custom serializers, or working with Protocol Buffers at a low level.

## Core Concepts

### Tag-Length-Value (TLV) Encoding

Protocol Buffers uses a **tag-length-value** encoding scheme where:
- **Tag**: Identifies the field number and wire type
- **Length**: For variable-length data, specifies the number of bytes
- **Value**: The actual data payload

The tag is encoded as `(field_number << 3) | wire_type`, combining the field number and wire type into a single varint.

### Wire Types

Protocol Buffers defines six wire types:

| Wire Type | Value | Description | Used For |
|-----------|-------|-------------|----------|
| VARINT | 0 | Variable-length integer | int32, int64, uint32, uint64, sint32, sint64, bool, enum |
| I64 | 1 | 64-bit fixed | fixed64, sfixed64, double |
| LEN | 2 | Length-delimited | string, bytes, embedded messages, packed repeated fields |
| SGROUP | 3 | Start group (deprecated) | Legacy group start |
| EGROUP | 4 | End group (deprecated) | Legacy group end |
| I32 | 5 | 32-bit fixed | fixed32, sfixed32, float |

## Encoding Details

### Varint Encoding

Varints encode integers using 1-10 bytes, with smaller values using fewer bytes. Each byte uses 7 bits for data and 1 bit (MSB) as a continuation flag.

**Example**: Encoding the number 300
- Binary: `100101100` (9 bits needed)
- Split into 7-bit groups: `0000010` `0101100`
- Add continuation bits: `10101100 00000010`
- Result: `0xAC 0x02`

### ZigZag Encoding

Signed integers use ZigZag encoding to map signed numbers to unsigned efficiently:
- `zigzag(n) = (n << 1) ^ (n >> 31)` for 32-bit
- `zigzag(n) = (n << 1) ^ (n >> 63)` for 64-bit

This ensures small negative numbers encode compactly (e.g., -1 becomes 1, -2 becomes 3).

### Length-Delimited Encoding

Strings, bytes, and embedded messages use wire type 2:
1. Tag (field number + wire type)
2. Length (varint encoding the byte count)
3. Data (the actual bytes)

## C/C++ Code Examples

### Example 1: Varint Encoding

```c
#include <stdint.h>
#include <stdio.h>

// Encode a 64-bit value as a varint
size_t encode_varint(uint64_t value, uint8_t* buffer) {
    size_t pos = 0;
    while (value >= 0x80) {
        buffer[pos++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    buffer[pos++] = (uint8_t)(value & 0x7F);
    return pos;
}

// Decode a varint from buffer
size_t decode_varint(const uint8_t* buffer, uint64_t* value) {
    size_t pos = 0;
    uint64_t result = 0;
    int shift = 0;
    
    while (pos < 10) {  // Max 10 bytes for 64-bit
        uint8_t byte = buffer[pos++];
        result |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return pos;
        }
        shift += 7;
    }
    return 0;  // Error: too many bytes
}

int main() {
    uint8_t buffer[10];
    uint64_t value = 300;
    
    size_t encoded_size = encode_varint(value, buffer);
    printf("Encoded %llu in %zu bytes: ", value, encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
        printf("0x%02X ", buffer[i]);
    }
    printf("\n");
    
    uint64_t decoded;
    decode_varint(buffer, &decoded);
    printf("Decoded: %llu\n", decoded);
    
    return 0;
}
```

### Example 2: ZigZag Encoding

```c
#include <stdint.h>
#include <stdio.h>

// ZigZag encode for 32-bit signed integers
uint32_t zigzag_encode_32(int32_t n) {
    return (uint32_t)((n << 1) ^ (n >> 31));
}

// ZigZag decode for 32-bit
int32_t zigzag_decode_32(uint32_t n) {
    return (int32_t)((n >> 1) ^ (-(n & 1)));
}

// ZigZag encode for 64-bit signed integers
uint64_t zigzag_encode_64(int64_t n) {
    return (uint64_t)((n << 1) ^ (n >> 63));
}

// ZigZag decode for 64-bit
int64_t zigzag_decode_64(uint64_t n) {
    return (int64_t)((n >> 1) ^ (-(int64_t)(n & 1)));
}

int main() {
    int32_t signed_vals[] = {0, -1, 1, -2, 2, -2147483648, 2147483647};
    
    printf("ZigZag Encoding Examples:\n");
    for (int i = 0; i < 7; i++) {
        uint32_t encoded = zigzag_encode_32(signed_vals[i]);
        int32_t decoded = zigzag_decode_32(encoded);
        printf("%11d -> %10u -> %11d\n", signed_vals[i], encoded, decoded);
    }
    
    return 0;
}
```

### Example 3: Complete Wire Format Encoder

```c
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    WIRE_VARINT = 0,
    WIRE_I64 = 1,
    WIRE_LEN = 2,
    WIRE_I32 = 5
} WireType;

// Encode tag (field_number << 3 | wire_type)
size_t encode_tag(uint32_t field_number, WireType wire_type, uint8_t* buffer) {
    uint32_t tag = (field_number << 3) | wire_type;
    return encode_varint(tag, buffer);
}

// Encode a length-delimited field (string/bytes)
size_t encode_length_delimited(uint32_t field_number, const uint8_t* data, 
                               size_t data_len, uint8_t* buffer) {
    size_t pos = 0;
    
    // Encode tag
    pos += encode_tag(field_number, WIRE_LEN, buffer + pos);
    
    // Encode length
    pos += encode_varint(data_len, buffer + pos);
    
    // Copy data
    memcpy(buffer + pos, data, data_len);
    pos += data_len;
    
    return pos;
}

// Encode a varint field
size_t encode_varint_field(uint32_t field_number, uint64_t value, uint8_t* buffer) {
    size_t pos = 0;
    pos += encode_tag(field_number, WIRE_VARINT, buffer + pos);
    pos += encode_varint(value, buffer + pos);
    return pos;
}

// Encode a 32-bit fixed field
size_t encode_fixed32_field(uint32_t field_number, uint32_t value, uint8_t* buffer) {
    size_t pos = 0;
    pos += encode_tag(field_number, WIRE_I32, buffer + pos);
    memcpy(buffer + pos, &value, 4);
    return pos + 4;
}

int main() {
    uint8_t buffer[256];
    size_t pos = 0;
    
    // Field 1: varint = 150
    pos += encode_varint_field(1, 150, buffer + pos);
    
    // Field 2: string = "testing"
    const char* str = "testing";
    pos += encode_length_delimited(2, (uint8_t*)str, strlen(str), buffer + pos);
    
    printf("Encoded message (%zu bytes): ", pos);
    for (size_t i = 0; i < pos; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
    
    return 0;
}
```

## Rust Code Examples

### Example 1: Varint Encoding in Rust

```rust
// Encode a u64 as a varint
fn encode_varint(mut value: u64, buffer: &mut Vec<u8>) {
    while value >= 0x80 {
        buffer.push(((value & 0x7F) | 0x80) as u8);
        value >>= 7;
    }
    buffer.push((value & 0x7F) as u8);
}

// Decode a varint from a slice
fn decode_varint(data: &[u8]) -> Option<(u64, usize)> {
    let mut result: u64 = 0;
    let mut shift = 0;
    
    for (pos, &byte) in data.iter().enumerate().take(10) {
        result |= ((byte & 0x7F) as u64) << shift;
        if byte & 0x80 == 0 {
            return Some((result, pos + 1));
        }
        shift += 7;
    }
    None  // Error: too many bytes
}

fn main() {
    let mut buffer = Vec::new();
    let value: u64 = 300;
    
    encode_varint(value, &mut buffer);
    println!("Encoded {} in {} bytes: {:02X?}", value, buffer.len(), buffer);
    
    if let Some((decoded, bytes_read)) = decode_varint(&buffer) {
        println!("Decoded: {} (read {} bytes)", decoded, bytes_read);
    }
}
```

### Example 2: ZigZag Encoding in Rust

```rust
// ZigZag encode for i32
fn zigzag_encode_32(n: i32) -> u32 {
    ((n << 1) ^ (n >> 31)) as u32
}

// ZigZag decode for i32
fn zigzag_decode_32(n: u32) -> i32 {
    ((n >> 1) ^ (-(n as i32 & 1))) as i32
}

// ZigZag encode for i64
fn zigzag_encode_64(n: i64) -> u64 {
    ((n << 1) ^ (n >> 63)) as u64
}

// ZigZag decode for i64
fn zigzag_decode_64(n: u64) -> i64 {
    ((n >> 1) ^ (-((n & 1) as i64))) as i64
}

fn main() {
    let signed_values = [0, -1, 1, -2, 2, i32::MIN, i32::MAX];
    
    println!("ZigZag Encoding Examples:");
    for &val in &signed_values {
        let encoded = zigzag_encode_32(val);
        let decoded = zigzag_decode_32(encoded);
        println!("{:11} -> {:10} -> {:11}", val, encoded, decoded);
    }
}
```

### Example 3: Complete Wire Format Implementation

```rust
#[derive(Debug, Clone, Copy)]
enum WireType {
    Varint = 0,
    I64 = 1,
    Len = 2,
    I32 = 5,
}

struct WireEncoder {
    buffer: Vec<u8>,
}

impl WireEncoder {
    fn new() -> Self {
        WireEncoder { buffer: Vec::new() }
    }
    
    fn encode_varint(&mut self, mut value: u64) {
        while value >= 0x80 {
            self.buffer.push(((value & 0x7F) | 0x80) as u8);
            value >>= 7;
        }
        self.buffer.push((value & 0x7F) as u8);
    }
    
    fn encode_tag(&mut self, field_number: u32, wire_type: WireType) {
        let tag = (field_number << 3) | (wire_type as u32);
        self.encode_varint(tag as u64);
    }
    
    fn write_varint_field(&mut self, field_number: u32, value: u64) {
        self.encode_tag(field_number, WireType::Varint);
        self.encode_varint(value);
    }
    
    fn write_string_field(&mut self, field_number: u32, value: &str) {
        self.encode_tag(field_number, WireType::Len);
        self.encode_varint(value.len() as u64);
        self.buffer.extend_from_slice(value.as_bytes());
    }
    
    fn write_fixed32_field(&mut self, field_number: u32, value: u32) {
        self.encode_tag(field_number, WireType::I32);
        self.buffer.extend_from_slice(&value.to_le_bytes());
    }
    
    fn write_fixed64_field(&mut self, field_number: u32, value: u64) {
        self.encode_tag(field_number, WireType::I64);
        self.buffer.extend_from_slice(&value.to_le_bytes());
    }
    
    fn write_bytes_field(&mut self, field_number: u32, value: &[u8]) {
        self.encode_tag(field_number, WireType::Len);
        self.encode_varint(value.len() as u64);
        self.buffer.extend_from_slice(value);
    }
    
    fn get_bytes(&self) -> &[u8] {
        &self.buffer
    }
}

struct WireDecoder<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> WireDecoder<'a> {
    fn new(data: &'a [u8]) -> Self {
        WireDecoder { data, pos: 0 }
    }
    
    fn decode_varint(&mut self) -> Option<u64> {
        let mut result: u64 = 0;
        let mut shift = 0;
        
        for _ in 0..10 {
            if self.pos >= self.data.len() {
                return None;
            }
            
            let byte = self.data[self.pos];
            self.pos += 1;
            
            result |= ((byte & 0x7F) as u64) << shift;
            if byte & 0x80 == 0 {
                return Some(result);
            }
            shift += 7;
        }
        None
    }
    
    fn decode_tag(&mut self) -> Option<(u32, WireType)> {
        let tag = self.decode_varint()? as u32;
        let field_number = tag >> 3;
        let wire_type = match tag & 0x7 {
            0 => WireType::Varint,
            1 => WireType::I64,
            2 => WireType::Len,
            5 => WireType::I32,
            _ => return None,
        };
        Some((field_number, wire_type))
    }
    
    fn decode_length_delimited(&mut self) -> Option<&'a [u8]> {
        let len = self.decode_varint()? as usize;
        if self.pos + len > self.data.len() {
            return None;
        }
        let result = &self.data[self.pos..self.pos + len];
        self.pos += len;
        Some(result)
    }
    
    fn has_more(&self) -> bool {
        self.pos < self.data.len()
    }
}

fn main() {
    // Encoding example
    let mut encoder = WireEncoder::new();
    
    encoder.write_varint_field(1, 150);
    encoder.write_string_field(2, "testing");
    encoder.write_fixed32_field(3, 42);
    
    let encoded = encoder.get_bytes();
    println!("Encoded message ({} bytes): {:02X?}", encoded.len(), encoded);
    
    // Decoding example
    let mut decoder = WireDecoder::new(encoded);
    
    while decoder.has_more() {
        if let Some((field_number, wire_type)) = decoder.decode_tag() {
            match wire_type {
                WireType::Varint => {
                    if let Some(value) = decoder.decode_varint() {
                        println!("Field {}: varint = {}", field_number, value);
                    }
                }
                WireType::Len => {
                    if let Some(data) = decoder.decode_length_delimited() {
                        if let Ok(s) = std::str::from_utf8(data) {
                            println!("Field {}: string = \"{}\"", field_number, s);
                        } else {
                            println!("Field {}: bytes = {:02X?}", field_number, data);
                        }
                    }
                }
                WireType::I32 => {
                    println!("Field {}: fixed32 (skipped)", field_number);
                }
                WireType::I64 => {
                    println!("Field {}: fixed64 (skipped)", field_number);
                }
            }
        }
    }
}
```

## Summary

The Protocol Buffers wire format is a highly efficient binary encoding that uses tag-length-value encoding to serialize structured data. Key points include:

- **Tags** combine field numbers and wire types using `(field_number << 3) | wire_type`
- **Varint encoding** compresses integers efficiently, using 1-10 bytes based on magnitude
- **ZigZag encoding** optimizes signed integers by mapping them to unsigned values
- **Six wire types** support various data types: VARINT (0), I64 (1), LEN (2), I32 (5), plus deprecated group types
- **Length-delimited encoding** handles strings, bytes, embedded messages, and packed repeated fields
- **Fixed-width encoding** uses 4 or 8 bytes for fixed32/fixed64 and float/double types

Understanding the wire format enables low-level Protocol Buffers work, custom implementations, debugging binary data, and optimization of message sizes. The format's simplicity and efficiency make it ideal for high-performance serialization across languages and platforms.