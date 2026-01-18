# Protobuf Scalar Types and Their Mappings

Protocol Buffers defines a set of scalar (primitive) types that map to native types in different programming languages. Understanding these types and their characteristics is crucial for efficient message design and cross-language compatibility.

## Overview of Scalar Types

Protobuf provides 15 scalar types, each optimized for different use cases:

### Integer Types

**Variable-length encoded integers** (more efficient for small values):
- **int32/int64**: Use variable-length encoding, but inefficient for negative numbers (always uses 10 bytes)
- **uint32/uint64**: Unsigned integers with variable-length encoding
- **sint32/sint64**: Signed integers using ZigZag encoding, efficient for negative numbers

**Fixed-length integers** (use fixed bytes regardless of value):
- **fixed32/fixed64**: Always use 4/8 bytes, efficient when values are often > 2^28/2^56
- **sfixed32/sfixed64**: Signed fixed-length integers, always use 4/8 bytes

### Other Scalar Types
- **bool**: Boolean values (true/false)
- **string**: UTF-8 encoded or 7-bit ASCII text
- **bytes**: Arbitrary byte sequences
- **float**: 32-bit floating point
- **double**: 64-bit floating point

## Encoding Efficiency Guide

The choice of integer type significantly impacts message size:

- Use `int32/int64` for non-negative values that are typically small
- Use `sint32/sint64` for negative values or values that cross zero frequently
- Use `uint32/uint64` for values that are always non-negative
- Use `fixed32/fixed64` when values are frequently large (saves encoding overhead)
- Use `sfixed32/sfixed64` for signed values that are frequently large

## C/C++ Examples

### Protocol Buffer Definition

```protobuf
syntax = "proto3";

message ScalarTypesExample {
  // Variable-length integers
  int32 age = 1;                    // -2^31 to 2^31-1
  int64 population = 2;             // -2^63 to 2^63-1
  uint32 positive_count = 3;        // 0 to 2^32-1
  uint64 large_positive = 4;        // 0 to 2^64-1
  sint32 temperature = 5;           // ZigZag encoded, efficient for negatives
  sint64 balance = 6;               // ZigZag encoded signed 64-bit
  
  // Fixed-length integers
  fixed32 file_size = 7;            // Always 4 bytes
  fixed64 timestamp_nanos = 8;      // Always 8 bytes
  sfixed32 coordinate_x = 9;        // Signed, always 4 bytes
  sfixed64 precise_measurement = 10; // Signed, always 8 bytes
  
  // Floating point
  float precision_value = 11;       // 32-bit float
  double high_precision = 12;       // 64-bit double
  
  // Other types
  bool is_active = 13;
  string name = 14;
  bytes binary_data = 15;
}
```

### C++ Usage

```cpp
#include <iostream>
#include <fstream>
#include "scalar_types_example.pb.h"

int main() {
    // Create and populate a message
    ScalarTypesExample message;
    
    // Set variable-length integers
    message.set_age(25);
    message.set_population(7800000000L);
    message.set_positive_count(42);
    message.set_large_positive(18446744073709551615ULL);
    message.set_temperature(-15);  // ZigZag encoding efficient here
    message.set_balance(-1000000);
    
    // Set fixed-length integers
    message.set_file_size(1048576);  // 1MB
    message.set_timestamp_nanos(1234567890123456789L);
    message.set_coordinate_x(-12345);
    message.set_precise_measurement(-9876543210L);
    
    // Set floating point
    message.set_precision_value(3.14159f);
    message.set_high_precision(3.141592653589793);
    
    // Set other types
    message.set_is_active(true);
    message.set_name("Alice");
    message.set_binary_data("\x00\x01\x02\x03\xFF");
    
    // Serialize to file
    std::fstream output("message.bin", 
                       std::ios::out | std::ios::binary | std::ios::trunc);
    if (!message.SerializeToOstream(&output)) {
        std::cerr << "Failed to write message." << std::endl;
        return 1;
    }
    output.close();
    
    // Deserialize from file
    ScalarTypesExample read_message;
    std::fstream input("message.bin", std::ios::in | std::ios::binary);
    if (!read_message.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse message." << std::endl;
        return 1;
    }
    
    // Access fields
    std::cout << "Age: " << read_message.age() << std::endl;
    std::cout << "Name: " << read_message.name() << std::endl;
    std::cout << "Temperature: " << read_message.temperature() << std::endl;
    std::cout << "High precision: " << read_message.high_precision() << std::endl;
    std::cout << "Is active: " << std::boolalpha 
              << read_message.is_active() << std::endl;
    
    // Check if field is set (proto3 doesn't have has_* by default)
    std::cout << "Message size: " << read_message.ByteSizeLong() 
              << " bytes" << std::endl;
    
    return 0;
}
```

### C Usage (with nanopb for embedded systems)

```c
#include <stdio.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "scalar_types_example.pb.h"

int main() {
    uint8_t buffer[256];
    size_t message_length;
    bool status;
    
    // Encoding
    {
        ScalarTypesExample message = ScalarTypesExample_init_zero;
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        
        // Populate fields
        message.age = 25;
        message.temperature = -15;
        message.is_active = true;
        message.precision_value = 3.14159f;
        
        // Handle string (requires manual setup with nanopb)
        strcpy(message.name, "Alice");
        
        // Handle bytes
        message.binary_data.size = 5;
        message.binary_data.bytes[0] = 0x00;
        message.binary_data.bytes[1] = 0x01;
        message.binary_data.bytes[2] = 0x02;
        message.binary_data.bytes[3] = 0x03;
        message.binary_data.bytes[4] = 0xFF;
        
        status = pb_encode(&stream, ScalarTypesExample_fields, &message);
        message_length = stream.bytes_written;
        
        if (!status) {
            printf("Encoding failed: %s\n", PB_GET_ERROR(&stream));
            return 1;
        }
        
        printf("Encoded %zu bytes\n", message_length);
    }
    
    // Decoding
    {
        ScalarTypesExample message = ScalarTypesExample_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);
        
        status = pb_decode(&stream, ScalarTypesExample_fields, &message);
        
        if (!status) {
            printf("Decoding failed: %s\n", PB_GET_ERROR(&stream));
            return 1;
        }
        
        printf("Age: %d\n", message.age);
        printf("Temperature: %d\n", message.temperature);
        printf("Name: %s\n", message.name);
        printf("Is active: %s\n", message.is_active ? "true" : "false");
        printf("Precision: %f\n", message.precision_value);
    }
    
    return 0;
}
```

## Rust Examples

### Cargo.toml Dependencies

```toml
[dependencies]
prost = "0.12"
bytes = "1.5"

[build-dependencies]
prost-build = "0.12"
```

### build.rs

```rust
fn main() {
    prost_build::compile_protos(&["proto/scalar_types_example.proto"],
                                 &["proto/"])
        .unwrap();
}
```

### Rust Usage

```rust
use bytes::Bytes;
use prost::Message;

// The generated code will be included automatically
include!(concat!(env!("OUT_DIR"), "/_.rs"));

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create and populate a message
    let message = ScalarTypesExample {
        // Variable-length integers
        age: 25,
        population: 7_800_000_000,
        positive_count: 42,
        large_positive: u64::MAX,
        temperature: -15,  // ZigZag encoding efficient here
        balance: -1_000_000,
        
        // Fixed-length integers
        file_size: 1_048_576,  // 1MB
        timestamp_nanos: 1_234_567_890_123_456_789,
        coordinate_x: -12_345,
        precise_measurement: -9_876_543_210,
        
        // Floating point
        precision_value: 3.14159,
        high_precision: 3.141592653589793,
        
        // Other types
        is_active: true,
        name: "Alice".to_string(),
        binary_data: vec![0x00, 0x01, 0x02, 0x03, 0xFF],
    };
    
    // Serialize to bytes
    let mut buf = Vec::new();
    message.encode(&mut buf)?;
    println!("Serialized {} bytes", buf.len());
    
    // Write to file
    std::fs::write("message.bin", &buf)?;
    
    // Read from file
    let data = std::fs::read("message.bin")?;
    
    // Deserialize
    let decoded = ScalarTypesExample::decode(&data[..])?;
    
    // Access fields
    println!("Age: {}", decoded.age);
    println!("Name: {}", decoded.name);
    println!("Temperature: {}", decoded.temperature);
    println!("High precision: {}", decoded.high_precision);
    println!("Is active: {}", decoded.is_active);
    println!("Binary data: {:?}", decoded.binary_data);
    
    // Demonstrate type mapping
    demonstrate_type_boundaries();
    
    Ok(())
}

fn demonstrate_type_boundaries() {
    println!("\n--- Type Boundaries ---");
    
    // int32 range
    let msg = ScalarTypesExample {
        age: i32::MIN,
        ..Default::default()
    };
    println!("int32 min: {}", msg.age);
    
    // uint32 range
    let msg = ScalarTypesExample {
        positive_count: u32::MAX,
        ..Default::default()
    };
    println!("uint32 max: {}", msg.positive_count);
    
    // sint32 with ZigZag encoding
    let negative_msg = ScalarTypesExample {
        temperature: -100,
        ..Default::default()
    };
    let mut buf = Vec::new();
    negative_msg.encode(&mut buf).unwrap();
    println!("sint32(-100) encoded size: {} bytes", buf.len());
    
    // Compare with int32 encoding of same value
    let int_msg = ScalarTypesExample {
        age: -100,
        ..Default::default()
    };
    let mut buf2 = Vec::new();
    int_msg.encode(&mut buf2).unwrap();
    println!("int32(-100) encoded size: {} bytes", buf2.len());
}

// Example with ownership and borrowing
fn process_message(msg: &ScalarTypesExample) {
    // String is String type in Rust
    let name_upper = msg.name.to_uppercase();
    println!("Uppercase name: {}", name_upper);
    
    // bytes is Vec<u8> in Rust
    let byte_count = msg.binary_data.len();
    println!("Binary data length: {}", byte_count);
}

// Example with mutation
fn increment_counters(msg: &mut ScalarTypesExample) {
    msg.age += 1;
    msg.positive_count += 1;
    msg.population += 1000;
}
```

### Advanced Rust Example: Type Safety and Conversion

```rust
use prost::Message;
use std::convert::TryFrom;

// Custom wrapper types for type safety
#[derive(Debug, Clone, Copy)]
struct Temperature(i32);

impl From<Temperature> for i32 {
    fn from(temp: Temperature) -> i32 {
        temp.0
    }
}

impl TryFrom<i32> for Temperature {
    type Error = &'static str;
    
    fn try_from(value: i32) -> Result<Self, Self::Error> {
        if value < -273 {
            Err("Temperature below absolute zero")
        } else {
            Ok(Temperature(value))
        }
    }
}

fn typed_message_example() -> Result<(), Box<dyn std::error::Error>> {
    let temp = Temperature::try_from(-15)?;
    
    let message = ScalarTypesExample {
        temperature: temp.into(),
        name: "Weather Station".to_string(),
        is_active: true,
        ..Default::default()
    };
    
    // Serialize
    let bytes = message.encode_to_vec();
    
    // Deserialize
    let decoded = ScalarTypesExample::decode(&bytes[..])?;
    
    // Convert back with validation
    let decoded_temp = Temperature::try_from(decoded.temperature)?;
    println!("Decoded temperature: {:?}", decoded_temp);
    
    Ok(())
}
```

## Language Type Mappings

| Proto Type | C++ | Rust | Notes |
|------------|-----|------|-------|
| int32 | int32 | i32 | Variable-length, inefficient for negatives |
| int64 | int64 | i64 | Variable-length, inefficient for negatives |
| uint32 | uint32 | u32 | Variable-length unsigned |
| uint64 | uint64 | u64 | Variable-length unsigned |
| sint32 | int32 | i32 | ZigZag encoding, efficient for negatives |
| sint64 | int64 | i64 | ZigZag encoding, efficient for negatives |
| fixed32 | uint32 | u32 | Always 4 bytes |
| fixed64 | uint64 | u64 | Always 8 bytes |
| sfixed32 | int32 | i32 | Always 4 bytes, signed |
| sfixed64 | int64 | i64 | Always 8 bytes, signed |
| float | float | f32 | IEEE 754 single precision |
| double | double | f64 | IEEE 754 double precision |
| bool | bool | bool | True or false |
| string | std::string | String | UTF-8 or 7-bit ASCII |
| bytes | std::string | Vec\<u8\> | Arbitrary byte sequence |

## Summary

**Scalar types** are the fundamental building blocks of Protocol Buffer messages. Key takeaways:

1. **Integer Type Selection Matters**: Choose between variable-length (`int32/64`, `uint32/64`, `sint32/64`) and fixed-length (`fixed32/64`, `sfixed32/64`) based on your data distribution. Variable-length encoding saves space for small values, while fixed-length is better for consistently large values.

2. **Negative Number Efficiency**: Use `sint32/sint64` for negative numbers or values that frequently cross zero, as they use ZigZag encoding. Regular `int32/int64` types encode negative numbers inefficiently (always 10 bytes).

3. **Language Mapping Consistency**: Protobuf provides consistent type mappings across languages, though the exact native types vary (e.g., `string` maps to `std::string` in C++ and `String` in Rust, while `bytes` maps to `std::string` in C++ and `Vec<u8>` in Rust).

4. **Type Safety**: While Protobuf ensures wire format compatibility, consider wrapping scalar types in your application code (especially in Rust) to add domain-specific validation and type safety.

5. **Default Values**: In proto3, scalar fields have implicit default values (0 for numbers, false for bool, empty string/bytes). There's no distinction between unset and default values unless you use special options.

Understanding these scalar types and their characteristics enables you to design efficient, cross-language compatible message schemas that minimize bandwidth and storage requirements.