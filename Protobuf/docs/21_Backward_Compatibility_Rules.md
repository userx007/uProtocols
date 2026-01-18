# Protocol Buffers: Backward Compatibility Rules

## Overview

Backward compatibility in Protocol Buffers is crucial for maintaining interoperability between different versions of your application. When you evolve your `.proto` schema over time, you need to ensure that older code can still read messages produced by newer code, and vice versa. This allows for gradual rollouts, mixed-version deployments, and long-term data storage without breaking existing systems.

## Core Principles

### 1. **Never Change Field Numbers**

Field numbers are the fundamental identifiers in Protocol Buffers' binary format. Once assigned, a field number must remain constant for that field forever. Changing a field number is equivalent to creating an entirely different field.

### 2. **Handle Unknown Fields**

When a parser encounters field numbers it doesn't recognize (from a newer schema version), it must preserve them. This allows messages to pass through older systems without losing data added by newer versions.

### 3. **Reserved Fields**

When removing fields, mark their numbers and names as `reserved` to prevent accidental reuse that could cause compatibility issues.

### 4. **Default Values**

Understanding default values is critical. When a field is absent from the binary data, parsers use the default value (0 for numbers, empty string for strings, false for bools, empty for repeated fields).

## Detailed Compatibility Rules

### Safe Changes (Backward and Forward Compatible)

1. **Adding new optional fields** - Old code ignores them; new code gets defaults when reading old data
2. **Adding new repeated fields** - Treated as empty by old code
3. **Renaming fields** - Field names are only used in generated code, not in the wire format
4. **Adding new enum values** - Old code treats unknown values as the default or preserves them
5. **Adding new message types** - No impact on existing messages

### Unsafe Changes (Breaking Compatibility)

1. **Changing field numbers** - Completely breaks binary compatibility
2. **Changing field types** (in most cases) - May cause parsing errors or data corruption
3. **Changing between singular and repeated** - Binary format differs
4. **Removing required fields** - Breaks older writers and readers expecting the field
5. **Reusing field numbers** - Can cause data corruption

### Conditionally Safe Changes

1. **Changing numeric types** - Safe only for compatible types (e.g., int32 ↔ int64)
2. **Changing between bytes and string** - Safe if data is valid UTF-8
3. **Changing between message and bytes** - Safe if bytes contain a valid encoded message

## Code Examples

### C/C++ Implementation

```c
// version1.proto
syntax = "proto3";

message UserProfile {
  int32 user_id = 1;
  string username = 2;
  string email = 3;
}
```

```cpp
// Version 1: Writing a message (C++)
#include "version1.pb.h"
#include <fstream>
#include <iostream>

void WriteUserProfile() {
    UserProfile profile;
    profile.set_user_id(12345);
    profile.set_username("alice");
    profile.set_email("alice@example.com");
    
    // Serialize to file
    std::fstream output("user.bin", std::ios::out | std::ios::binary);
    if (!profile.SerializeToOstream(&output)) {
        std::cerr << "Failed to write profile" << std::endl;
    }
}
```

Now let's evolve the schema with backward-compatible changes:

```c
// version2.proto
syntax = "proto3";

message UserProfile {
  int32 user_id = 1;
  string username = 2;
  string email = 3;
  
  // New optional fields - backward compatible
  string full_name = 4;
  int64 created_timestamp = 5;
  repeated string tags = 6;
  
  // Reserved fields (previously removed)
  reserved 7, 8;
  reserved "old_field", "deprecated_flag";
}
```

```cpp
// Version 2: Reading with new schema (C++)
#include "version2.pb.h"
#include <fstream>
#include <iostream>

void ReadUserProfile() {
    UserProfile profile;
    
    // Read file written by version 1
    std::fstream input("user.bin", std::ios::in | std::ios::binary);
    if (!profile.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse profile" << std::endl;
        return;
    }
    
    // Can access old fields
    std::cout << "User ID: " << profile.user_id() << std::endl;
    std::cout << "Username: " << profile.username() << std::endl;
    
    // New fields have default values when reading old data
    std::cout << "Full name: " << profile.full_name() << std::endl; // Empty string
    std::cout << "Timestamp: " << profile.created_timestamp() << std::endl; // 0
    std::cout << "Tags count: " << profile.tags_size() << std::endl; // 0
}
```

```cpp
// Version 1 reading Version 2 data (C++)
#include "version1.pb.h"
#include <fstream>

void OldCodeReadingNewData() {
    UserProfile profile;
    
    // Read file written by version 2 (with extra fields)
    std::fstream input("user_v2.bin", std::ios::in | std::ios::binary);
    if (!profile.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse" << std::endl;
        return;
    }
    
    // Can still access known fields
    std::cout << "User ID: " << profile.user_id() << std::endl;
    
    // Unknown fields are automatically preserved
    // If we serialize this message again, the unknown fields will be retained
    std::fstream output("user_forwarded.bin", std::ios::out | std::ios::binary);
    profile.SerializeToOstream(&output);
    // The output will still contain fields 4, 5, and 6 even though version1 doesn't know about them
}
```

### Rust Implementation

```rust
// Using the prost crate for Protocol Buffers in Rust

// version1.proto (same as above)

// Generated code usage - Version 1
use prost::Message;
use std::fs::File;
use std::io::Write;

#[derive(Clone, PartialEq, Message)]
pub struct UserProfile {
    #[prost(int32, tag = "1")]
    pub user_id: i32,
    #[prost(string, tag = "2")]
    pub username: String,
    #[prost(string, tag = "3")]
    pub email: String,
}

fn write_user_profile() -> Result<(), Box<dyn std::error::Error>> {
    let profile = UserProfile {
        user_id: 12345,
        username: "alice".to_string(),
        email: "alice@example.com".to_string(),
    };
    
    // Serialize to bytes
    let mut buf = Vec::new();
    profile.encode(&mut buf)?;
    
    // Write to file
    let mut file = File::create("user.bin")?;
    file.write_all(&buf)?;
    
    Ok(())
}
```

```rust
// Version 2 with backward-compatible changes
#[derive(Clone, PartialEq, Message)]
pub struct UserProfileV2 {
    #[prost(int32, tag = "1")]
    pub user_id: i32,
    #[prost(string, tag = "2")]
    pub username: String,
    #[prost(string, tag = "3")]
    pub email: String,
    
    // New optional fields
    #[prost(string, tag = "4")]
    pub full_name: String,
    #[prost(int64, tag = "5")]
    pub created_timestamp: i64,
    #[prost(string, repeated, tag = "6")]
    pub tags: Vec<String>,
    
    // Reserved: tags 7 and 8 cannot be used
}

fn read_user_profile_v2() -> Result<(), Box<dyn std::error::Error>> {
    use std::io::Read;
    
    let mut file = File::open("user.bin")?;
    let mut buf = Vec::new();
    file.read_to_end(&mut buf)?;
    
    // Decode with V2 schema (reading V1 data)
    let profile = UserProfileV2::decode(&buf[..])?;
    
    println!("User ID: {}", profile.user_id);
    println!("Username: {}", profile.username);
    
    // New fields have default values
    println!("Full name: {}", profile.full_name); // Empty string
    println!("Timestamp: {}", profile.created_timestamp); // 0
    println!("Tags: {:?}", profile.tags); // Empty vec
    
    Ok(())
}
```

```rust
// Demonstrating unknown field preservation in Rust
use prost::Message;
use bytes::Bytes;

fn preserve_unknown_fields() -> Result<(), Box<dyn std::error::Error>> {
    // V2 writes data with new fields
    let profile_v2 = UserProfileV2 {
        user_id: 12345,
        username: "alice".to_string(),
        email: "alice@example.com".to_string(),
        full_name: "Alice Smith".to_string(),
        created_timestamp: 1640000000,
        tags: vec!["premium".to_string(), "verified".to_string()],
    };
    
    let mut buf = Vec::new();
    profile_v2.encode(&mut buf)?;
    
    // V1 code reads this data
    let profile_v1 = UserProfile::decode(&buf[..])?;
    
    // V1 can access known fields
    println!("V1 sees user_id: {}", profile_v1.user_id);
    
    // When V1 re-encodes, unknown fields are preserved by prost
    let mut rebuf = Vec::new();
    profile_v1.encode(&mut rebuf)?;
    
    // V2 can decode and still see all fields
    let recovered = UserProfileV2::decode(&rebuf[..])?;
    println!("Full name preserved: {}", recovered.full_name);
    
    Ok(())
}
```

### Handling Enum Evolution

```c
// version1.proto
enum UserStatus {
  STATUS_UNKNOWN = 0;
  ACTIVE = 1;
  SUSPENDED = 2;
}
```

```c
// version2.proto - adding new enum value
enum UserStatus {
  STATUS_UNKNOWN = 0;
  ACTIVE = 1;
  SUSPENDED = 2;
  PENDING_VERIFICATION = 3;  // New value
}
```

```cpp
// C++ handling unknown enum values
void HandleEnumEvolution() {
    // Old code reading new data with PENDING_VERIFICATION
    UserProfile profile;
    // ... parse from stream ...
    
    // In proto3, unknown enum values are preserved as integers
    int status_value = static_cast<int>(profile.status());
    
    if (profile.status() == UserStatus::STATUS_UNKNOWN) {
        // Could be genuinely unknown or an unrecognized value from newer version
        std::cout << "Unknown status, raw value: " << status_value << std::endl;
    }
}
```

```rust
// Rust handling unknown enum values
fn handle_enum_evolution() -> Result<(), Box<dyn std::error::Error>> {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, prost::Enumeration)]
    #[repr(i32)]
    pub enum UserStatus {
        Unknown = 0,
        Active = 1,
        Suspended = 2,
        // If old code encounters value 3, prost provides from_i32()
    }
    
    let raw_value = 3; // PENDING_VERIFICATION from newer version
    
    match UserStatus::from_i32(raw_value) {
        Some(status) => println!("Known status: {:?}", status),
        None => println!("Unknown status value: {}", raw_value),
    }
    
    Ok(())
}
```

## Summary

Protocol Buffers backward compatibility relies on three fundamental practices: never changing field numbers (they're the binary format's identifiers), preserving unknown fields to enable forward compatibility, and using reserved declarations to prevent dangerous field reuse. Safe schema evolution includes adding new optional or repeated fields, renaming fields, and adding new enum values, while dangerous operations include changing field numbers, switching between singular and repeated fields, or reusing deleted field numbers.

The key insight is that field numbers are permanent identifiers in the wire format, whereas field names only exist in generated code. This separation allows renaming while maintaining binary compatibility. Both C/C++ and Rust implementations automatically handle unknown field preservation, enabling older code to pass through newer messages without data loss. By following these rules, you can evolve schemas over months or years while maintaining compatibility across all versions of your system, supporting gradual rollouts and mixed-version deployments without service interruptions.