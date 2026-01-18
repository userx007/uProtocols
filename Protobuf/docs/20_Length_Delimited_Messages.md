# Length-Delimited Messages in Protocol Buffers

## Overview

Length-delimited messages are one of the fundamental wire types in Protocol Buffers, used to encode variable-length data such as strings, bytes, and nested (embedded) messages. This encoding scheme uses a length prefix to indicate how many bytes follow, allowing parsers to know exactly how much data to read without needing special terminators or scanning for boundaries.

## Wire Type and Encoding Structure

Length-delimited fields use **wire type 2** in Protocol Buffers. The encoding follows this structure:

1. **Tag byte(s)**: Contains the field number and wire type
2. **Length varint**: Specifies the number of bytes in the payload
3. **Payload**: The actual data (string bytes, raw bytes, or serialized nested message)

The tag is encoded as `(field_number << 3) | wire_type`, where wire type 2 indicates length-delimited encoding.

## Common Use Cases

Length-delimited encoding is used for:
- **String fields**: UTF-8 encoded text
- **Bytes fields**: Raw binary data
- **Nested messages**: Serialized submessages embedded within parent messages
- **Repeated packed fields**: In proto3, repeated numeric fields can be packed using length-delimited encoding

## Code Examples

### C/C++ Implementation

Here's how to work with length-delimited messages in C++:

```c
// message.proto
syntax = "proto3";

message Person {
    string name = 1;
    bytes avatar = 2;
    Address address = 3;
}

message Address {
    string street = 1;
    string city = 2;
    int32 zip_code = 3;
}
```

```cpp
#include <iostream>
#include <fstream>
#include "message.pb.h"

// Serializing length-delimited messages
void SerializeExample() {
    Person person;
    person.set_name("Alice Johnson");
    person.set_avatar("\x89PNG\x0D\x0A"); // Binary data
    
    // Nested message
    Address* address = person.mutable_address();
    address->set_street("123 Main St");
    address->set_city("Springfield");
    address->set_zip_code(12345);
    
    // Serialize to string
    std::string serialized;
    if (person.SerializeToString(&serialized)) {
        std::cout << "Serialized size: " << serialized.size() << " bytes\n";
        
        // Write to file
        std::ofstream output("person.bin", std::ios::binary);
        output.write(serialized.data(), serialized.size());
        output.close();
    }
}

// Deserializing length-delimited messages
void DeserializeExample() {
    std::ifstream input("person.bin", std::ios::binary);
    std::string serialized((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    
    Person person;
    if (person.ParseFromString(serialized)) {
        std::cout << "Name: " << person.name() << "\n";
        std::cout << "Avatar size: " << person.avatar().size() << " bytes\n";
        std::cout << "City: " << person.address().city() << "\n";
    }
}

// Manual length-delimited encoding (low-level)
void ManualEncodingExample() {
    std::string output;
    
    // Field 1 (name): tag + length + data
    const std::string name = "Bob";
    output.push_back((1 << 3) | 2);  // Tag: field 1, wire type 2
    output.push_back(name.size());    // Length varint
    output.append(name);              // Data
    
    std::cout << "Manual encoding result size: " << output.size() << " bytes\n";
}

int main() {
    SerializeExample();
    DeserializeExample();
    ManualEncodingExample();
    return 0;
}
```

### Rust Implementation

Using the `prost` crate for Protocol Buffers in Rust:

```rust
// In build.rs
use std::io::Result;

fn main() -> Result<()> {
    prost_build::compile_protos(&["src/message.proto"], &["src/"])?;
    Ok(())
}
```

```rust
// Cargo.toml dependencies:
// prost = "0.12"
// bytes = "1.0"

use prost::Message;
use bytes::{BytesMut, BufMut, Buf};

// Generated from the proto file
mod proto {
    include!(concat!(env!("OUT_DIR"), "/_.rs"));
}

use proto::{Person, Address};

// Serializing length-delimited messages
fn serialize_example() -> Vec<u8> {
    let address = Address {
        street: "123 Main St".to_string(),
        city: "Springfield".to_string(),
        zip_code: 12345,
    };
    
    let person = Person {
        name: "Alice Johnson".to_string(),
        avatar: vec![0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A], // PNG header
        address: Some(address),
    };
    
    // Encode to bytes
    let mut buf = Vec::new();
    person.encode(&mut buf).expect("Failed to encode");
    
    println!("Serialized size: {} bytes", buf.len());
    buf
}

// Deserializing length-delimited messages
fn deserialize_example(data: &[u8]) {
    match Person::decode(data) {
        Ok(person) => {
            println!("Name: {}", person.name);
            println!("Avatar size: {} bytes", person.avatar.len());
            if let Some(address) = person.address {
                println!("City: {}", address.city);
            }
        }
        Err(e) => eprintln!("Decode error: {}", e),
    }
}

// Manual length-delimited encoding (low-level)
fn manual_encoding_example() {
    let mut buffer = BytesMut::new();
    
    // Field 1 (name): tag + length + data
    let name = b"Bob";
    buffer.put_u8((1 << 3) | 2);  // Tag: field 1, wire type 2
    buffer.put_u8(name.len() as u8); // Length varint (simplified)
    buffer.put_slice(name);
    
    println!("Manual encoding result size: {} bytes", buffer.len());
}

// Length-delimited streaming (useful for large messages)
fn encode_length_delimited(message: &Person) -> Vec<u8> {
    let mut buf = Vec::new();
    
    // Calculate message size
    let size = message.encoded_len();
    
    // Encode length as varint
    prost::encoding::encode_varint(size as u64, &mut buf);
    
    // Encode message
    message.encode(&mut buf).expect("Failed to encode");
    
    buf
}

fn decode_length_delimited(mut data: &[u8]) -> Result<Person, prost::DecodeError> {
    // Decode length varint
    let length = prost::encoding::decode_varint(&mut data)?;
    
    // Decode message with specific length
    let message_data = &data[..length as usize];
    Person::decode(message_data)
}

fn main() {
    // Basic serialization
    let serialized = serialize_example();
    
    // Deserialization
    deserialize_example(&serialized);
    
    // Manual encoding
    manual_encoding_example();
    
    // Length-delimited streaming
    let person = Person {
        name: "Charlie".to_string(),
        avatar: vec![],
        address: None,
    };
    
    let delimited = encode_length_delimited(&person);
    println!("Length-delimited size: {} bytes", delimited.len());
    
    match decode_length_delimited(&delimited) {
        Ok(decoded) => println!("Decoded name: {}", decoded.name),
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

## Wire Format Details

For a message like:
```protobuf
message Example {
    string text = 1;  // "hello"
}
```

The wire format would be:
- `0x0A`: Tag (field 1, wire type 2)
- `0x05`: Length (5 bytes)
- `0x68 0x65 0x6C 0x6C 0x6F`: UTF-8 bytes for "hello"

For nested messages, the inner message is first serialized completely, then its byte array is treated as the payload for the length-delimited field.

## Summary

Length-delimited messages are essential for encoding variable-length data in Protocol Buffers. Using wire type 2, this encoding prefixes data with its byte length, enabling efficient parsing without terminators. This mechanism handles strings (UTF-8 text), bytes (raw binary), and nested messages (serialized submessages). Both C++ and Rust provide high-level APIs through generated code that abstracts the encoding details, though understanding the underlying wire format is valuable for debugging, optimization, and implementing custom serialization logic. The length-prefix approach ensures backward compatibility and allows parsers to skip unknown fields efficiently, making it a cornerstone of Protocol Buffers' flexibility and performance.