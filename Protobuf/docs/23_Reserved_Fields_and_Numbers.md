# Reserved Fields and Numbers in Protocol Buffers

## Overview

Reserved fields and numbers in Protocol Buffers provide a mechanism to prevent accidental reuse of field numbers or field names that were previously used but have been removed from a message definition. This is crucial for maintaining backward and forward compatibility in evolving schemas.

## Why Reserved Fields Matter

When you remove a field from a Protocol Buffers message, there's a risk that someone might later add a new field using the same field number or name. This can cause serious problems:

- **Data corruption**: Old binaries reading new messages (or vice versa) might interpret data incorrectly
- **Parsing errors**: Field numbers that get reused can cause confusion in serialization/deserialization
- **Debugging nightmares**: Subtle bugs that only appear when mixing different versions of the schema

The `reserved` keyword prevents these issues by permanently marking field numbers and names as unavailable.

## Syntax and Usage

You can reserve field numbers, ranges of field numbers, and field names:

```protobuf
message MyMessage {
  reserved 2, 15, 9 to 11;           // Reserve specific numbers and ranges
  reserved "foo", "bar";              // Reserve field names
  
  string name = 1;
  int32 id = 3;
  // Cannot use field numbers 2, 9, 10, 11, or 15
  // Cannot use field names "foo" or "bar"
}
```

**Important notes:**
- Field numbers and field names must be reserved in separate `reserved` statements
- Ranges are inclusive (9 to 11 includes 9, 10, and 11)
- You can use `max` to reserve up to the maximum field number: `reserved 1000 to max;`

## C/C++ Code Examples

### Defining a Message with Reserved Fields

```c
// person.proto
syntax = "proto3";

message Person {
  reserved 2, 5, 9 to 11;
  reserved "old_address", "deprecated_field";
  
  string name = 1;
  int32 age = 3;
  string email = 4;
  repeated string phone_numbers = 6;
}
```

### Using the Generated C++ Code

```cpp
#include <iostream>
#include <fstream>
#include "person.pb.h"

int main() {
    // Create and populate a Person message
    Person person;
    person.set_name("Alice Johnson");
    person.set_age(30);
    person.set_email("alice@example.com");
    person.add_phone_numbers("555-1234");
    person.add_phone_numbers("555-5678");
    
    // Serialize to binary format
    std::string serialized;
    if (!person.SerializeToString(&serialized)) {
        std::cerr << "Failed to serialize person." << std::endl;
        return 1;
    }
    
    // Write to file
    std::ofstream output("person.bin", std::ios::binary);
    output.write(serialized.data(), serialized.size());
    output.close();
    
    // Read from file and deserialize
    std::ifstream input("person.bin", std::ios::binary);
    std::string read_data((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    input.close();
    
    Person parsed_person;
    if (!parsed_person.ParseFromString(read_data)) {
        std::cerr << "Failed to parse person." << std::endl;
        return 1;
    }
    
    // Access fields
    std::cout << "Name: " << parsed_person.name() << std::endl;
    std::cout << "Age: " << parsed_person.age() << std::endl;
    std::cout << "Email: " << parsed_person.email() << std::endl;
    std::cout << "Phone numbers:" << std::endl;
    for (int i = 0; i < parsed_person.phone_numbers_size(); i++) {
        std::cout << "  " << parsed_person.phone_numbers(i) << std::endl;
    }
    
    return 0;
}
```

### Evolution Example - What Happens When You Try to Reuse Reserved Numbers

```protobuf
// This will cause a compilation error!
message Person {
  reserved 2, 5, 9 to 11;
  reserved "old_address", "deprecated_field";
  
  string name = 1;
  int32 age = 3;
  string email = 4;
  
  // ERROR: Field number 2 is reserved
  // string middle_name = 2;
  
  // ERROR: Field name is reserved
  // string old_address = 7;
}
```

## Rust Code Examples

### Defining and Using Reserved Fields

First, define your proto file:

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
  reserved 3, 7 to 10;
  reserved "legacy_token", "old_password";
  
  string username = 1;
  string email = 2;
  int64 created_at = 4;
  bool is_active = 5;
  string display_name = 6;
}
```

### Rust Implementation

```rust
// Using prost for Rust Protocol Buffers
use prost::Message;
use std::fs::File;
use std::io::{Read, Write};

// Generated from user.proto
#[derive(Clone, PartialEq, Message)]
pub struct User {
    #[prost(string, tag = "1")]
    pub username: String,
    
    #[prost(string, tag = "2")]
    pub email: String,
    
    // Field numbers 3, 7, 8, 9, 10 are reserved
    
    #[prost(int64, tag = "4")]
    pub created_at: i64,
    
    #[prost(bool, tag = "5")]
    pub is_active: bool,
    
    #[prost(string, tag = "6")]
    pub display_name: String,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create a new User
    let user = User {
        username: "rustacean".to_string(),
        email: "rust@example.com".to_string(),
        created_at: 1704067200, // Unix timestamp
        is_active: true,
        display_name: "The Rustacean".to_string(),
    };
    
    // Serialize to bytes
    let mut buffer = Vec::new();
    user.encode(&mut buffer)?;
    
    println!("Serialized size: {} bytes", buffer.len());
    
    // Write to file
    let mut file = File::create("user.bin")?;
    file.write_all(&buffer)?;
    
    // Read from file
    let mut file = File::open("user.bin")?;
    let mut read_buffer = Vec::new();
    file.read_to_end(&mut read_buffer)?;
    
    // Deserialize
    let decoded_user = User::decode(&read_buffer[..])?;
    
    // Display user information
    println!("\nDecoded User:");
    println!("  Username: {}", decoded_user.username);
    println!("  Email: {}", decoded_user.email);
    println!("  Created at: {}", decoded_user.created_at);
    println!("  Is active: {}", decoded_user.is_active);
    println!("  Display name: {}", decoded_user.display_name);
    
    Ok(())
}
```

### Build Configuration (Cargo.toml)

```toml
[package]
name = "protobuf-reserved-example"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"

[build-dependencies]
prost-build = "0.12"
```

### Build Script (build.rs)

```rust
fn main() -> Result<(), Box<dyn std::error::Error>> {
    prost_build::compile_protos(&["src/user.proto"], &["src/"])?;
    Ok(())
}
```

## Best Practices

**Always reserve when deleting fields**: Whenever you remove a field from a message, immediately add its number and name to the reserved list.

**Document why fields are reserved**: Add comments explaining why fields were reserved to help future developers understand the schema's history.

```protobuf
message Product {
  reserved 2;  // Removed 2024-01-15: price field moved to nested Price message
  reserved "price";
  
  string name = 1;
  Price pricing = 3;
}
```

**Reserve ranges for future expansion**: If you know certain number ranges won't be used, reserve them to prevent accidental use.

**Be cautious with max**: Using `reserved 1000 to max;` prevents using any field number above 1000, which might be too restrictive for large schemas.

## Summary

Reserved fields and numbers are a critical feature of Protocol Buffers that ensures schema evolution safety. By using the `reserved` keyword, you prevent accidental reuse of deleted field numbers and names, which could lead to data corruption, parsing errors, and compatibility issues between different versions of your application. Both C++ and Rust implementations respect these reservations at compile time, making it impossible to accidentally violate these constraints. Always reserve field numbers and names when removing fields from your message definitions to maintain long-term schema integrity.