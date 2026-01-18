# Protocol Buffers Field Numbering Strategy

## Overview

Field numbering is one of the most critical aspects of Protocol Buffers design. Unlike traditional serialization formats where field names are used to identify data, Protocol Buffers use integer field numbers as identifiers in the binary wire format. These numbers are permanent identifiers that cannot be changed without breaking compatibility, making the numbering strategy a fundamental architectural decision.

## Core Concepts

### Field Numbers as Permanent Identifiers

In Protocol Buffers, each field is assigned a unique integer number that serves as its identifier in the serialized binary format. This number becomes part of your API contract:

```protobuf
message User {
  string username = 1;    // Field number 1
  string email = 2;       // Field number 2
  int32 age = 3;          // Field number 3
}
```

In the wire format, the field name "username" is never transmitted—only the number `1` is encoded along with its value. This makes Protocol Buffers extremely efficient but also means **field numbers must never be changed or reused**.

### Numbering Ranges and Their Implications

Protocol Buffers reserves certain number ranges for specific purposes:

- **1-15**: Single-byte encoding for field number + wire type (most efficient)
- **16-2047**: Two-byte encoding
- **19000-19999**: Reserved by Protocol Buffers for internal use
- **1-536,870,911**: Maximum valid field number range

The encoding efficiency difference is significant. Fields 1-15 use 1 byte for the tag (field number + wire type), while fields 16+ require at least 2 bytes.

## C/C++ Code Examples

### Basic Field Usage

```c
// user.proto
syntax = "proto3";

message User {
  string username = 1;
  string email = 2;
  int32 age = 3;
  repeated string interests = 4;
}
```

```cpp
#include "user.pb.h"
#include <iostream>
#include <fstream>

int main() {
    // Creating a message
    User user;
    user.set_username("alice");
    user.set_email("alice@example.com");
    user.set_age(30);
    user.add_interests("coding");
    user.add_interests("reading");
    
    // Serialize to binary
    std::string binary_output;
    if (!user.SerializeToString(&binary_output)) {
        std::cerr << "Failed to serialize" << std::endl;
        return 1;
    }
    
    // The binary format uses field numbers (1, 2, 3, 4), not names
    std::cout << "Serialized size: " << binary_output.size() << " bytes" << std::endl;
    
    // Deserialize
    User deserialized_user;
    if (!deserialized_user.ParseFromString(binary_output)) {
        std::cerr << "Failed to parse" << std::endl;
        return 1;
    }
    
    std::cout << "Username: " << deserialized_user.username() << std::endl;
    std::cout << "Email: " << deserialized_user.email() << std::endl;
    
    return 0;
}
```

### Demonstrating Field Number Permanence

```cpp
// Version 1 of your message
// user_v1.proto
message UserV1 {
  string username = 1;
  string email = 2;
  int32 age = 3;
}

// Version 2 - Adding new fields (SAFE)
// user_v2.proto
message UserV2 {
  string username = 1;
  string email = 2;
  int32 age = 3;
  string phone = 4;        // New field with new number - SAFE
  string address = 5;      // Another new field - SAFE
}
```

```cpp
// Forward compatibility example
void demonstrate_forward_compatibility() {
    // Old code writes data with V1
    UserV1 user_v1;
    user_v1.set_username("bob");
    user_v1.set_email("bob@example.com");
    user_v1.set_age(25);
    
    std::string data;
    user_v1.SerializeToString(&data);
    
    // New code reads with V2 - works perfectly
    UserV2 user_v2;
    user_v2.ParseFromString(data);
    
    std::cout << "Username: " << user_v2.username() << std::endl;
    std::cout << "Has phone: " << user_v2.has_phone() << std::endl; // false
}

// Backward compatibility example
void demonstrate_backward_compatibility() {
    // New code writes with V2
    UserV2 user_v2;
    user_v2.set_username("charlie");
    user_v2.set_email("charlie@example.com");
    user_v2.set_age(35);
    user_v2.set_phone("555-1234");  // V2 field
    
    std::string data;
    user_v2.SerializeToString(&data);
    
    // Old code reads with V1 - unknown fields preserved
    UserV1 user_v1;
    user_v1.ParseFromString(data);
    
    std::cout << "Username: " << user_v1.username() << std::endl;
    // phone field is unknown to V1, but preserved in unknown fields
}
```

### Reserved Field Numbers

```c
// product.proto
syntax = "proto3";

message Product {
  reserved 2, 15, 9 to 11;  // Reserve specific numbers
  reserved "old_price", "deprecated_field";  // Reserve names
  
  string name = 1;
  // int32 old_price = 2;  // ERROR: 2 is reserved
  double current_price = 3;
  string description = 4;
  // Can't use numbers 9, 10, 11, or 15
  int32 stock_quantity = 16;
}
```

## Rust Code Examples

### Basic Field Usage with prost

```rust
// In build.rs
fn main() {
    prost_build::compile_protos(&["src/user.proto"], &["src/"]).unwrap();
}
```

```rust
// user.proto remains the same
// In your Rust code:

use prost::Message;

// Include generated code
pub mod user {
    include!(concat!(env!("OUT_DIR"), "/user.rs"));
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    use user::User;
    
    // Create a message
    let mut user = User {
        username: "alice".to_string(),
        email: "alice@example.com".to_string(),
        age: 30,
        interests: vec!["coding".to_string(), "reading".to_string()],
    };
    
    // Serialize - field numbers used in wire format
    let mut buf = Vec::new();
    user.encode(&mut buf)?;
    
    println!("Serialized size: {} bytes", buf.len());
    
    // Deserialize
    let decoded_user = User::decode(&buf[..])?;
    
    println!("Username: {}", decoded_user.username);
    println!("Email: {}", decoded_user.email);
    println!("Interests: {:?}", decoded_user.interests);
    
    Ok(())
}
```

### Optimal Field Number Strategy

```rust
// efficient_message.proto
syntax = "proto3";

message EfficientMessage {
  // Most frequently used fields in 1-15 range (1-byte encoding)
  string id = 1;
  string name = 2;
  int64 timestamp = 3;
  string status = 4;
  int32 priority = 5;
  
  // Less frequently used fields in 16+ range
  string detailed_description = 16;
  bytes large_payload = 17;
  map<string, string> metadata = 18;
}
```

```rust
use prost::Message;

pub mod efficient {
    include!(concat!(env!("OUT_DIR"), "/efficient_message.rs"));
}

fn demonstrate_encoding_efficiency() -> Result<(), Box<dyn std::error::Error>> {
    use efficient::EfficientMessage;
    
    let msg = EfficientMessage {
        id: "12345".to_string(),
        name: "Test".to_string(),
        timestamp: 1234567890,
        status: "active".to_string(),
        priority: 1,
        detailed_description: String::new(),
        large_payload: Vec::new(),
        metadata: std::collections::HashMap::new(),
    };
    
    let mut buf = Vec::new();
    msg.encode(&mut buf)?;
    
    // Fields 1-5 are encoded more efficiently than if they were 16-20
    println!("Message size with optimal numbering: {} bytes", buf.len());
    
    Ok(())
}
```

### Compatibility Pattern

```rust
// Define versioned messages
pub mod v1 {
    use prost::Message;
    
    #[derive(Clone, PartialEq, Message)]
    pub struct User {
        #[prost(string, tag = "1")]
        pub username: String,
        #[prost(string, tag = "2")]
        pub email: String,
        #[prost(int32, tag = "3")]
        pub age: i32,
    }
}

pub mod v2 {
    use prost::Message;
    
    #[derive(Clone, PartialEq, Message)]
    pub struct User {
        #[prost(string, tag = "1")]
        pub username: String,
        #[prost(string, tag = "2")]
        pub email: String,
        #[prost(int32, tag = "3")]
        pub age: i32,
        #[prost(string, optional, tag = "4")]
        pub phone: Option<String>,
        #[prost(string, optional, tag = "5")]
        pub address: Option<String>,
    }
}

fn test_compatibility() -> Result<(), Box<dyn std::error::Error>> {
    use prost::Message;
    
    // V1 writes data
    let user_v1 = v1::User {
        username: "alice".to_string(),
        email: "alice@example.com".to_string(),
        age: 30,
    };
    
    let mut buf = Vec::new();
    user_v1.encode(&mut buf)?;
    
    // V2 reads data - new fields are None
    let user_v2 = v2::User::decode(&buf[..])?;
    assert_eq!(user_v2.username, "alice");
    assert_eq!(user_v2.phone, None);
    
    // V2 writes data with new fields
    let user_v2_full = v2::User {
        username: "bob".to_string(),
        email: "bob@example.com".to_string(),
        age: 25,
        phone: Some("555-1234".to_string()),
        address: Some("123 Main St".to_string()),
    };
    
    let mut buf2 = Vec::new();
    user_v2_full.encode(&mut buf2)?;
    
    // V1 reads data - unknown fields preserved but inaccessible
    let user_v1_read = v1::User::decode(&buf2[..])?;
    assert_eq!(user_v1_read.username, "bob");
    // phone and address are in unknown fields, preserved for round-trip
    
    Ok(())
}
```

### Reserved Fields in Rust

```rust
// Using prost attributes for reserved fields
pub mod product {
    use prost::Message;
    
    #[derive(Clone, PartialEq, Message)]
    pub struct Product {
        #[prost(string, tag = "1")]
        pub name: String,
        
        // Field number 2 is intentionally skipped (reserved)
        
        #[prost(double, tag = "3")]
        pub current_price: f64,
        
        #[prost(string, tag = "4")]
        pub description: String,
        
        // Field numbers 9-11 and 15 are reserved
        
        #[prost(int32, tag = "16")]
        pub stock_quantity: i32,
    }
}

// Document reserved numbers
// Reserved: 2, 9-11, 15
// Reason: old_price (2), deprecated analytics fields (9-11), temp field (15)
```

## Best Practices and Impact

### Numbering Strategy Checklist

1. **Use 1-15 for frequent fields**: These encode in 1 byte and should be used for fields that appear in most messages or are accessed most often
2. **Reserve deleted field numbers**: Always add `reserved` statements when removing fields
3. **Plan for growth**: Leave gaps in numbering to accommodate related fields later
4. **Document the strategy**: Maintain comments explaining number assignments
5. **Never reuse numbers**: Once a field number is used and data serialized, it's permanently taken

### Wire Format Impact

```cpp
// Example showing wire format differences
message ComparisonMessage {
  string field_1 = 1;      // Tag = 0x0A (1 byte: field=1, wire_type=2)
  string field_16 = 16;    // Tag = 0x82 0x01 (2 bytes)
  string field_1000 = 1000; // Tag = 0xC0 0x3E (2 bytes)
}
```

The field number directly affects the tag size in the wire format, which impacts overall message size, especially for messages with many fields or in high-volume scenarios.

### Evolution Safety

```protobuf
// WRONG - Breaking change
message User {
  string name = 1;
  int32 age = 2;      // Changed from 3 to 2 - BREAKS COMPATIBILITY
  string email = 3;   // Changed from 2 to 3 - BREAKS COMPATIBILITY
}

// CORRECT - Safe evolution
message User {
  string name = 1;
  string email = 2;
  int32 age = 3;
  string phone = 4;   // New field, new number
  reserved 5;         // Reserve for potential future use
  string address = 6; // Another new field
}
```

## Summary

Field numbering in Protocol Buffers is a permanent architectural decision with far-reaching implications:

- **Field numbers are identifiers**: They replace field names in the binary format and must never change
- **Encoding efficiency matters**: Numbers 1-15 use 1-byte tags; optimize by placing frequent fields here
- **Backward/forward compatibility**: Proper numbering strategy enables schema evolution without breaking existing code
- **Reserved numbers prevent accidents**: Always reserve deleted field numbers to prevent reuse
- **Wire format directly affected**: Field numbers determine the size and structure of serialized data

Understanding field numbering is essential for designing robust, evolvable Protocol Buffer schemas that maintain compatibility across versions while maintaining optimal performance. The strategy you choose at the beginning will impact your system for its entire lifetime, making careful planning crucial.