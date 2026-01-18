# Varint and Zigzag Encoding in Protocol Buffers

## Detailed Description

Protocol Buffers uses two clever encoding techniques to minimize the size of serialized data: **Varint encoding** for unsigned integers and **Zigzag encoding** combined with Varint for signed integers.

### Varint Encoding

Varint (variable-length integer) encoding represents integers using a variable number of bytes, where smaller values use fewer bytes. This is particularly efficient because most integer values in real-world applications tend to be small.

**How it works:**
- Each byte in a varint has a continuation bit (the most significant bit, MSB)
- If the MSB is 1, there are more bytes to follow
- If the MSB is 0, this is the last byte
- The remaining 7 bits of each byte contain the actual data
- Bytes are stored in little-endian order (least significant group first)

**Efficiency:**
- Values 0-127: 1 byte
- Values 128-16,383: 2 bytes
- Values 16,384-2,097,151: 3 bytes
- And so on, up to a maximum of 10 bytes for 64-bit integers

### Zigzag Encoding

Zigzag encoding is used for signed integers (`sint32`, `sint64`) to make both small positive and small negative numbers efficient to encode. Without zigzag, negative numbers would always require the maximum number of bytes because of two's complement representation.

**How it works:**
Zigzag maps signed integers to unsigned integers in a way that alternates between positive and negative values:
- 0 → 0
- -1 → 1
- 1 → 2
- -2 → 3
- 2 → 4
- -3 → 5

**Formula:**
- For 32-bit: `(n << 1) ^ (n >> 31)`
- For 64-bit: `(n << 1) ^ (n >> 63)`

This ensures that both small positive and small negative numbers result in small unsigned values that can be efficiently encoded with varint.

## C/C++ Code Examples

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Encode a 64-bit unsigned integer as varint
// Returns the number of bytes written
size_t encode_varint(uint64_t value, uint8_t* buffer) {
    size_t pos = 0;
    
    while (value >= 0x80) {
        // Write 7 bits + continuation bit
        buffer[pos++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    
    // Write the final byte (no continuation bit)
    buffer[pos++] = (uint8_t)(value & 0x7F);
    
    return pos;
}

// Decode a varint from buffer
// Returns the number of bytes read, or 0 on error
size_t decode_varint(const uint8_t* buffer, size_t buffer_len, uint64_t* value) {
    *value = 0;
    size_t pos = 0;
    int shift = 0;
    
    while (pos < buffer_len) {
        uint8_t byte = buffer[pos++];
        
        // Add the lower 7 bits to our value
        *value |= (uint64_t)(byte & 0x7F) << shift;
        
        // If continuation bit is not set, we're done
        if ((byte & 0x80) == 0) {
            return pos;
        }
        
        shift += 7;
        
        // Prevent overflow (varint max is 10 bytes)
        if (shift >= 64) {
            return 0;  // Error
        }
    }
    
    return 0;  // Error: incomplete varint
}

// Zigzag encode a signed 32-bit integer
uint32_t zigzag_encode_32(int32_t n) {
    return (uint32_t)((n << 1) ^ (n >> 31));
}

// Zigzag decode to signed 32-bit integer
int32_t zigzag_decode_32(uint32_t n) {
    return (int32_t)((n >> 1) ^ (-(int32_t)(n & 1)));
}

// Zigzag encode a signed 64-bit integer
uint64_t zigzag_encode_64(int64_t n) {
    return (uint64_t)((n << 1) ^ (n >> 63));
}

// Zigzag decode to signed 64-bit integer
int64_t zigzag_decode_64(uint64_t n) {
    return (int64_t)((n >> 1) ^ (-(int64_t)(n & 1)));
}

// Example usage
int main() {
    uint8_t buffer[10];
    size_t bytes_written, bytes_read;
    uint64_t decoded_value;
    
    // Test varint encoding
    printf("=== Varint Encoding ===\n");
    uint64_t test_values[] = {0, 1, 127, 128, 300, 16384, 2097151};
    
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        bytes_written = encode_varint(test_values[i], buffer);
        printf("Value %llu encoded in %zu bytes: ", 
               (unsigned long long)test_values[i], bytes_written);
        
        for (size_t j = 0; j < bytes_written; j++) {
            printf("0x%02X ", buffer[j]);
        }
        
        bytes_read = decode_varint(buffer, bytes_written, &decoded_value);
        printf("-> decoded: %llu\n", (unsigned long long)decoded_value);
    }
    
    // Test zigzag encoding
    printf("\n=== Zigzag Encoding ===\n");
    int32_t signed_values[] = {0, -1, 1, -2, 2, -64, 64, -1000, 1000};
    
    for (size_t i = 0; i < sizeof(signed_values) / sizeof(signed_values[0]); i++) {
        uint32_t zigzag = zigzag_encode_32(signed_values[i]);
        int32_t decoded_signed = zigzag_decode_32(zigzag);
        
        printf("Value %d -> zigzag: %u -> decoded: %d\n", 
               signed_values[i], zigzag, decoded_signed);
    }
    
    return 0;
}
```

## Rust Code Examples

```rust
// Encode a u64 as varint
fn encode_varint(mut value: u64, buffer: &mut Vec<u8>) {
    while value >= 0x80 {
        // Write 7 bits + continuation bit
        buffer.push(((value & 0x7F) | 0x80) as u8);
        value >>= 7;
    }
    
    // Write the final byte (no continuation bit)
    buffer.push((value & 0x7F) as u8);
}

// Decode a varint from a byte slice
// Returns (decoded_value, bytes_consumed) or None on error
fn decode_varint(buffer: &[u8]) -> Option<(u64, usize)> {
    let mut value: u64 = 0;
    let mut shift = 0;
    
    for (pos, &byte) in buffer.iter().enumerate() {
        // Add the lower 7 bits to our value
        value |= ((byte & 0x7F) as u64) << shift;
        
        // If continuation bit is not set, we're done
        if (byte & 0x80) == 0 {
            return Some((value, pos + 1));
        }
        
        shift += 7;
        
        // Prevent overflow (varint max is 10 bytes for u64)
        if shift >= 64 {
            return None;
        }
    }
    
    None  // Incomplete varint
}

// Zigzag encode a signed 32-bit integer
fn zigzag_encode_32(n: i32) -> u32 {
    ((n << 1) ^ (n >> 31)) as u32
}

// Zigzag decode to signed 32-bit integer
fn zigzag_decode_32(n: u32) -> i32 {
    ((n >> 1) ^ (-(n as i32 & 1)) as u32) as i32
}

// Zigzag encode a signed 64-bit integer
fn zigzag_encode_64(n: i64) -> u64 {
    ((n << 1) ^ (n >> 63)) as u64
}

// Zigzag decode to signed 64-bit integer
fn zigzag_decode_64(n: u64) -> i64 {
    ((n >> 1) ^ (-((n & 1) as i64)) as u64) as i64
}

fn main() {
    // Test varint encoding
    println!("=== Varint Encoding ===");
    let test_values = vec![0u64, 1, 127, 128, 300, 16384, 2097151];
    
    for &value in &test_values {
        let mut buffer = Vec::new();
        encode_varint(value, &mut buffer);
        
        print!("Value {} encoded in {} bytes: ", value, buffer.len());
        for byte in &buffer {
            print!("0x{:02X} ", byte);
        }
        
        if let Some((decoded, bytes_read)) = decode_varint(&buffer) {
            println!("-> decoded: {} ({} bytes read)", decoded, bytes_read);
        }
    }
    
    // Test zigzag encoding
    println!("\n=== Zigzag Encoding ===");
    let signed_values = vec![0i32, -1, 1, -2, 2, -64, 64, -1000, 1000];
    
    for &value in &signed_values {
        let zigzag = zigzag_encode_32(value);
        let decoded = zigzag_decode_32(zigzag);
        
        println!("Value {} -> zigzag: {} -> decoded: {}", 
                 value, zigzag, decoded);
    }
    
    // Example with combined zigzag + varint for sint32
    println!("\n=== Combined Zigzag + Varint (sint32) ===");
    for &value in &signed_values {
        let zigzag = zigzag_encode_32(value);
        let mut buffer = Vec::new();
        encode_varint(zigzag as u64, &mut buffer);
        
        println!("sint32 value {} -> {} bytes on wire", value, buffer.len());
    }
}
```

## Summary

Varint and zigzag encoding are fundamental space-saving techniques in Protocol Buffers. Varint encoding uses 1-10 bytes to represent integers based on their magnitude, making small numbers extremely efficient by using only one byte for values 0-127. Zigzag encoding preprocesses signed integers by mapping them to unsigned values in an alternating pattern, ensuring that both small positive and negative numbers benefit from varint's efficiency. Together, these encodings allow Protocol Buffers to achieve compact serialization: the value 1 uses just 1 byte whether positive or negative (when using `sint32`), while traditional fixed-width encoding would always use 4 bytes for `int32`. This makes protobuf ideal for network protocols and storage where bandwidth and space are at a premium.