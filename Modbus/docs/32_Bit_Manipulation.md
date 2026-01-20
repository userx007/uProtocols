# Bit Manipulation in Modbus Programming

## Overview

Bit manipulation is a fundamental technique in Modbus programming for efficiently handling coil and discrete input operations. Since Modbus packs multiple binary values (coils or discrete inputs) into bytes during transmission, developers must extract individual bits from received bytes and pack individual bit values into bytes for transmission.

## Why Bit Manipulation Matters in Modbus

Modbus function codes 01 (Read Coils), 02 (Read Discrete Inputs), 05 (Write Single Coil), and 15 (Write Multiple Coils) work with binary data. However, this data is transmitted as bytes over the network:

- **Reading operations**: When you request 12 coils, the response contains 2 bytes (16 bits) where only the first 12 bits are meaningful
- **Writing operations**: When setting multiple coils, you must pack the individual bit values into bytes
- **Bit ordering**: Modbus uses LSB (Least Significant Bit) first ordering within each byte

## Core Bit Manipulation Operations

### Setting a Bit

Setting a bit to 1 at a specific position uses the bitwise OR operation with a shifted mask:

```c
// C/C++ - Set bit at position 'pos' in byte 'data'
void set_bit(uint8_t *data, int pos) {
    *data |= (1 << pos);
}

// Set bit 3 in a byte
uint8_t value = 0x00;  // 00000000
set_bit(&value, 3);     // 00001000 (0x08)
```

### Clearing a Bit

Clearing a bit to 0 uses bitwise AND with an inverted mask:

```c
// C/C++ - Clear bit at position 'pos' in byte 'data'
void clear_bit(uint8_t *data, int pos) {
    *data &= ~(1 << pos);
}

// Clear bit 3 in a byte
uint8_t value = 0xFF;   // 11111111
clear_bit(&value, 3);   // 11110111 (0xF7)
```

### Testing a Bit

Reading a bit value uses bitwise AND and checks if the result is non-zero:

```c
// C/C++ - Test if bit at position 'pos' is set
int test_bit(uint8_t data, int pos) {
    return (data & (1 << pos)) != 0;
}

// Test bit 3
uint8_t value = 0x08;   // 00001000
int is_set = test_bit(value, 3);  // Returns 1 (true)
```

### Toggling a Bit

Flipping a bit uses XOR:

```c
// C/C++ - Toggle bit at position 'pos'
void toggle_bit(uint8_t *data, int pos) {
    *data ^= (1 << pos);
}
```

## Practical Modbus Implementation

### Extracting Coils from Response Data

When reading coils, the response contains packed bytes. Here's how to extract individual coil states:

```c
// C/C++ - Extract coil states from Modbus response
#include <stdint.h>
#include <stdbool.h>

bool get_coil_state(const uint8_t *data, int coil_number) {
    int byte_index = coil_number / 8;
    int bit_position = coil_number % 8;
    return (data[byte_index] & (1 << bit_position)) != 0;
}

// Example: Parse a response with 12 coils
void parse_coil_response(const uint8_t *response_data, int num_coils) {
    for (int i = 0; i < num_coils; i++) {
        bool state = get_coil_state(response_data, i);
        printf("Coil %d: %s\n", i, state ? "ON" : "OFF");
    }
}

// Usage
uint8_t response[] = {0xCD, 0x06};  // Binary: 11001101 00000110
// This represents 12 coils with states matching the bit pattern
parse_coil_response(response, 12);
```

### Packing Coils for Write Operations

When writing multiple coils, you must pack the values into bytes:

```c
// C/C++ - Pack coil values into bytes for transmission
void pack_coils(const bool *coil_states, int num_coils, uint8_t *output) {
    int num_bytes = (num_coils + 7) / 8;  // Round up
    
    // Initialize output to zero
    for (int i = 0; i < num_bytes; i++) {
        output[i] = 0;
    }
    
    // Set bits for each coil
    for (int i = 0; i < num_coils; i++) {
        if (coil_states[i]) {
            int byte_index = i / 8;
            int bit_position = i % 8;
            output[byte_index] |= (1 << bit_position);
        }
    }
}

// Example usage
bool coils[10] = {true, false, true, true, false, false, true, false, true, false};
uint8_t packed[2];
pack_coils(coils, 10, packed);
// Result: packed[0] = 01001101 (0x4D), packed[1] = 00000001 (0x01)
```

## C++ Object-Oriented Approach

```cpp
// C++ - Modbus bit manipulation class
#include <vector>
#include <cstdint>

class ModbusBitArray {
private:
    std::vector<uint8_t> data;
    size_t bit_count;

public:
    ModbusBitArray(size_t num_bits) : bit_count(num_bits) {
        size_t num_bytes = (num_bits + 7) / 8;
        data.resize(num_bytes, 0);
    }
    
    void set_bit(size_t pos, bool value) {
        if (pos >= bit_count) return;
        
        size_t byte_index = pos / 8;
        size_t bit_position = pos % 8;
        
        if (value) {
            data[byte_index] |= (1 << bit_position);
        } else {
            data[byte_index] &= ~(1 << bit_position);
        }
    }
    
    bool get_bit(size_t pos) const {
        if (pos >= bit_count) return false;
        
        size_t byte_index = pos / 8;
        size_t bit_position = pos % 8;
        
        return (data[byte_index] & (1 << bit_position)) != 0;
    }
    
    const uint8_t* get_data() const { return data.data(); }
    size_t get_byte_count() const { return data.size(); }
};

// Usage example
void demo_bitarray() {
    ModbusBitArray coils(16);
    coils.set_bit(0, true);
    coils.set_bit(3, true);
    coils.set_bit(7, true);
    coils.set_bit(15, true);
    
    for (size_t i = 0; i < 16; i++) {
        if (coils.get_bit(i)) {
            printf("Coil %zu is ON\n", i);
        }
    }
}
```

## Rust Implementation

Rust provides safe, efficient bit manipulation with strong type guarantees:

```rust
// Rust - Modbus bit manipulation

/// Extract a single bit from a byte array
pub fn get_bit(data: &[u8], bit_index: usize) -> bool {
    let byte_index = bit_index / 8;
    let bit_position = bit_index % 8;
    
    if byte_index >= data.len() {
        return false;
    }
    
    (data[byte_index] & (1 << bit_position)) != 0
}

/// Set a single bit in a byte array
pub fn set_bit(data: &mut [u8], bit_index: usize, value: bool) {
    let byte_index = bit_index / 8;
    let bit_position = bit_index % 8;
    
    if byte_index >= data.len() {
        return;
    }
    
    if value {
        data[byte_index] |= 1 << bit_position;
    } else {
        data[byte_index] &= !(1 << bit_position);
    }
}

/// Parse coils from Modbus response
pub fn parse_coils(data: &[u8], num_coils: usize) -> Vec<bool> {
    (0..num_coils)
        .map(|i| get_bit(data, i))
        .collect()
}

/// Pack coil states into bytes for transmission
pub fn pack_coils(coil_states: &[bool]) -> Vec<u8> {
    let num_bytes = (coil_states.len() + 7) / 8;
    let mut data = vec![0u8; num_bytes];
    
    for (i, &state) in coil_states.iter().enumerate() {
        if state {
            set_bit(&mut data, i, true);
        }
    }
    
    data
}

// Example usage
fn main() {
    // Parse received coils
    let response = vec![0xCD, 0x06];
    let coils = parse_coils(&response, 12);
    
    for (i, state) in coils.iter().enumerate() {
        println!("Coil {}: {}", i, if *state { "ON" } else { "OFF" });
    }
    
    // Pack coils for writing
    let states = vec![true, false, true, true, false, false, true, false];
    let packed = pack_coils(&states);
    println!("Packed bytes: {:02X?}", packed);
}
```

### Rust with Type Safety

```rust
// Rust - Type-safe bit array structure
pub struct ModbusBitArray {
    data: Vec<u8>,
    bit_count: usize,
}

impl ModbusBitArray {
    pub fn new(num_bits: usize) -> Self {
        let num_bytes = (num_bits + 7) / 8;
        Self {
            data: vec![0; num_bytes],
            bit_count: num_bits,
        }
    }
    
    pub fn from_bytes(data: Vec<u8>, num_bits: usize) -> Self {
        Self { data, bit_count: num_bits }
    }
    
    pub fn set(&mut self, index: usize, value: bool) -> Result<(), &'static str> {
        if index >= self.bit_count {
            return Err("Index out of bounds");
        }
        
        let byte_index = index / 8;
        let bit_position = index % 8;
        
        if value {
            self.data[byte_index] |= 1 << bit_position;
        } else {
            self.data[byte_index] &= !(1 << bit_position);
        }
        
        Ok(())
    }
    
    pub fn get(&self, index: usize) -> Result<bool, &'static str> {
        if index >= self.bit_count {
            return Err("Index out of bounds");
        }
        
        let byte_index = index / 8;
        let bit_position = index % 8;
        
        Ok((self.data[byte_index] & (1 << bit_position)) != 0)
    }
    
    pub fn as_bytes(&self) -> &[u8] {
        &self.data
    }
}

// Usage
fn demo_type_safe() -> Result<(), &'static str> {
    let mut bits = ModbusBitArray::new(16);
    bits.set(0, true)?;
    bits.set(5, true)?;
    bits.set(15, true)?;
    
    println!("Bit 5 is: {}", bits.get(5)?);
    println!("Packed data: {:02X?}", bits.as_bytes());
    
    Ok(())
}
```

## Advanced: Efficient Bit Counting

Sometimes you need to count how many coils are ON:

```c
// C - Count set bits (population count)
int count_set_bits(uint8_t byte) {
    int count = 0;
    while (byte) {
        count += byte & 1;
        byte >>= 1;
    }
    return count;
}

// Using Brian Kernighan's algorithm (faster)
int count_set_bits_fast(uint8_t byte) {
    int count = 0;
    while (byte) {
        byte &= byte - 1;  // Clear the lowest set bit
        count++;
    }
    return count;
}
```

```rust
// Rust - Built-in population count
fn count_set_bits(byte: u8) -> u32 {
    byte.count_ones()
}

// Count all set coils in a response
fn count_active_coils(data: &[u8], num_coils: usize) -> usize {
    let full_bytes = num_coils / 8;
    let remaining_bits = num_coils % 8;
    
    let mut count = 0;
    
    // Count full bytes
    for &byte in &data[..full_bytes] {
        count += byte.count_ones() as usize;
    }
    
    // Count remaining bits in the last byte
    if remaining_bits > 0 && full_bytes < data.len() {
        let mask = (1 << remaining_bits) - 1;
        count += (data[full_bytes] & mask).count_ones() as usize;
    }
    
    count
}
```

## Summary

**Bit manipulation in Modbus** is essential for efficiently handling coil and discrete input data. The key operations include:

- **Setting bits**: Use OR with a shifted mask (`data |= (1 << pos)`)
- **Clearing bits**: Use AND with inverted mask (`data &= ~(1 << pos)`)
- **Testing bits**: Use AND and check for non-zero (`(data & (1 << pos)) != 0`)
- **Byte indexing**: Calculate as `bit_number / 8`
- **Bit position**: Calculate as `bit_number % 8`

Both C/C++ and Rust provide powerful bit manipulation capabilities, with Rust offering additional safety through bounds checking and type safety. Understanding these operations is crucial for correctly implementing Modbus communication, especially when working with function codes that handle binary I/O points.