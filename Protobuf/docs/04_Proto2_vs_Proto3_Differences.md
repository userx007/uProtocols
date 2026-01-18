# Proto2 vs Proto3 Differences

## Overview

Protocol Buffers has evolved through two major syntax versions: proto2 and proto3. While both serve the same fundamental purpose of serializing structured data, they have significant differences in syntax, semantics, and behavior. Understanding these differences is crucial when choosing which version to use or when migrating between them.

## Key Differences

### Field Labels and Requirements

**Proto2** supports three field labels:
- `required`: Field must be present in every message
- `optional`: Field may or may not be present
- `repeated`: Field can appear zero or more times

**Proto3** simplified this to:
- Removed `required` keyword entirely
- All singular fields are implicitly optional
- Retained `repeated` for arrays/lists
- Added `optional` keyword in proto3.12+ to restore explicit optional semantics

### Default Values

**Proto2**: 
- Allows custom default values using the `default` keyword
- Can detect if a field was explicitly set or is using the default
- Unset fields return specified defaults

**Proto3**:
- No custom default values allowed
- Uses zero values (0 for numbers, "" for strings, false for bools, empty for messages)
- Cannot distinguish between unset fields and fields set to zero values (unless using `optional`)

### Unknown Fields

**Proto2**: Preserves unknown fields during parsing and serialization

**Proto3**: 
- Initially discarded unknown fields (versions 3.0-3.4)
- Now preserves unknown fields (3.5+) for better forward/backward compatibility

### Enums

**Proto2**: First enum value can be any number

**Proto3**: First enum value must be zero (to align with zero-value defaults)

### Maps

**Proto2**: No native map support (use repeated nested messages)

**Proto3**: Native `map<key, value>` syntax

### JSON Mapping

**Proto3**: Has well-defined, standardized JSON mapping

**Proto2**: JSON mapping exists but is less standardized

### Extensions

**Proto2**: Full support for extensions with `extensions` and `extend` keywords

**Proto3**: Extensions removed (use `Any` type instead)

## Code Examples

### Proto2 Message Definition

```protobuf
syntax = "proto2";

message UserProfile {
  required int32 user_id = 1;
  optional string username = 2 [default = "anonymous"];
  optional string email = 3;
  repeated string tags = 4;
  
  optional bool is_active = 5 [default = true];
  optional int32 login_count = 6 [default = 0];
  
  enum Status {
    OFFLINE = 1;
    ONLINE = 2;
    AWAY = 3;
  }
  optional Status status = 7 [default = OFFLINE];
  
  extensions 100 to 199;
}

extend UserProfile {
  optional string department = 100;
}
```

### Proto3 Message Definition

```protobuf
syntax = "proto3";

message UserProfile {
  int32 user_id = 1;
  string username = 2;
  string email = 3;
  repeated string tags = 4;
  
  bool is_active = 5;
  int32 login_count = 6;
  
  enum Status {
    STATUS_UNSPECIFIED = 0;  // Must be zero
    OFFLINE = 1;
    ONLINE = 2;
    AWAY = 3;
  }
  Status status = 7;
  
  // Native map support
  map<string, string> metadata = 8;
  
  // Optional keyword (proto3.12+) for presence tracking
  optional string last_login = 9;
}
```

### C/C++ Examples

**Proto2 C++ Usage:**

```cpp
#include "userprofile.pb.h"
#include <iostream>
#include <string>

void proto2_example() {
    UserProfile profile;
    
    // Required field - must be set
    profile.set_user_id(12345);
    
    // Optional field with default value
    if (!profile.has_username()) {
        std::cout << "Username has default: " << profile.username() << std::endl;
        // Outputs: "Username has default: anonymous"
    }
    
    profile.set_username("john_doe");
    profile.set_email("john@example.com");
    
    // Repeated fields
    profile.add_tags("developer");
    profile.add_tags("admin");
    
    // Check if optional field is set
    if (profile.has_email()) {
        std::cout << "Email: " << profile.email() << std::endl;
    }
    
    // Default value for is_active is true
    std::cout << "Is active: " << profile.is_active() << std::endl;
    
    // Using extensions
    profile.SetExtension(department, "Engineering");
    
    // Serialization
    std::string serialized;
    profile.SerializeToString(&serialized);
    
    // Deserialization
    UserProfile parsed;
    parsed.ParseFromString(serialized);
    
    // Validation - will fail if required fields missing
    if (!profile.IsInitialized()) {
        std::cerr << "Required fields missing!" << std::endl;
    }
}
```

**Proto3 C++ Usage:**

```cpp
#include "userprofile.pb.h"
#include <iostream>
#include <string>

void proto3_example() {
    UserProfile profile;
    
    // All fields implicitly optional (except repeated/map)
    profile.set_user_id(12345);
    profile.set_username("john_doe");
    profile.set_email("john@example.com");
    
    // Repeated fields
    profile.add_tags("developer");
    profile.add_tags("admin");
    
    // Cannot distinguish unset from zero value (without optional keyword)
    std::cout << "Login count: " << profile.login_count() << std::endl;
    // Outputs: 0 (could be unset or explicitly set to 0)
    
    // Map usage
    (*profile.mutable_metadata())["created_at"] = "2024-01-01";
    (*profile.mutable_metadata())["updated_at"] = "2024-01-15";
    
    // With optional keyword - has presence tracking
    if (profile.has_last_login()) {
        std::cout << "Last login: " << profile.last_login() << std::endl;
    } else {
        std::cout << "Last login not set" << std::endl;
    }
    
    profile.set_last_login("2024-01-15T10:30:00Z");
    
    // Enum with zero value
    if (profile.status() == UserProfile::STATUS_UNSPECIFIED) {
        std::cout << "Status not explicitly set" << std::endl;
    }
    
    profile.set_status(UserProfile::ONLINE);
    
    // Serialization
    std::string serialized;
    profile.SerializeToString(&serialized);
    
    // Deserialization
    UserProfile parsed;
    parsed.ParseFromString(serialized);
    
    // No IsInitialized() check needed - no required fields
}
```

**Proto2 vs Proto3 Default Behavior in C++:**

```cpp
#include <iostream>

void compare_defaults() {
    // Proto2 behavior
    Proto2UserProfile p2_profile;
    std::cout << "Proto2 - username (unset): " << p2_profile.username() << std::endl;
    // Outputs: "anonymous" (custom default)
    std::cout << "Proto2 - has_username: " << p2_profile.has_username() << std::endl;
    // Outputs: 0 (false - not set)
    
    p2_profile.set_username("anonymous");
    std::cout << "Proto2 - has_username (after set): " << p2_profile.has_username() << std::endl;
    // Outputs: 1 (true - explicitly set)
    
    // Proto3 behavior
    Proto3UserProfile p3_profile;
    std::cout << "Proto3 - username (unset): " << p3_profile.username() << std::endl;
    // Outputs: "" (zero value)
    // No has_username() method unless 'optional' keyword used
    
    p3_profile.set_username("");
    // Cannot tell if this was set or unset!
    
    // Proto3 with optional keyword
    Proto3UserProfile p3_optional;
    std::cout << "Proto3 - has_last_login: " << p3_optional.has_last_login() << std::endl;
    // Outputs: 0 (false)
    
    p3_optional.set_last_login("");
    std::cout << "Proto3 - has_last_login (after set): " << p3_optional.has_last_login() << std::endl;
    // Outputs: 1 (true - presence tracked)
}
```

### Rust Examples

**Proto2 Rust Usage:**

```rust
// Using prost or protobuf crate
use protobuf::Message;

fn proto2_example() -> Result<(), Box<dyn std::error::Error>> {
    let mut profile = UserProfile::new();
    
    // Required field - must be set
    profile.set_user_id(12345);
    
    // Optional field with default value
    if !profile.has_username() {
        println!("Username has default: {}", profile.get_username());
        // Outputs: "Username has default: anonymous"
    }
    
    profile.set_username("john_doe".to_string());
    profile.set_email("john@example.com".to_string());
    
    // Repeated fields
    profile.mut_tags().push("developer".to_string());
    profile.mut_tags().push("admin".to_string());
    
    // Check if optional field is set
    if profile.has_email() {
        println!("Email: {}", profile.get_email());
    }
    
    // Default value handling
    println!("Is active: {}", profile.get_is_active());
    
    // Enum with default
    profile.set_status(UserProfile_Status::ONLINE);
    
    // Serialization
    let serialized = profile.write_to_bytes()?;
    
    // Deserialization
    let parsed = UserProfile::parse_from_bytes(&serialized)?;
    
    // Validation for required fields
    if !profile.is_initialized() {
        eprintln!("Required fields missing!");
    }
    
    Ok(())
}
```

**Proto3 Rust Usage (with prost):**

```rust
// Using prost for proto3
use prost::Message;
use std::collections::HashMap;

fn proto3_example() -> Result<(), Box<dyn std::error::Error>> {
    let mut profile = UserProfile {
        user_id: 12345,
        username: "john_doe".to_string(),
        email: "john@example.com".to_string(),
        tags: vec!["developer".to_string(), "admin".to_string()],
        is_active: false,
        login_count: 0,
        status: Status::Online as i32,
        metadata: HashMap::new(),
        last_login: None,  // Option type for optional field
    };
    
    // Map usage
    profile.metadata.insert("created_at".to_string(), "2024-01-01".to_string());
    profile.metadata.insert("updated_at".to_string(), "2024-01-15".to_string());
    
    // Optional field with presence tracking
    match &profile.last_login {
        Some(login) => println!("Last login: {}", login),
        None => println!("Last login not set"),
    }
    
    profile.last_login = Some("2024-01-15T10:30:00Z".to_string());
    
    // Zero value semantics
    if profile.login_count == 0 {
        println!("Login count is zero (could be unset or explicitly set)");
    }
    
    // Serialization
    let mut buf = Vec::new();
    profile.encode(&mut buf)?;
    
    // Deserialization
    let parsed = UserProfile::decode(&buf[..])?;
    
    println!("Parsed username: {}", parsed.username);
    println!("Parsed tags: {:?}", parsed.tags);
    
    Ok(())
}
```

**Comparing Field Presence in Rust:**

```rust
use prost::Message;

fn compare_field_presence() {
    // Proto3 without optional
    let profile1 = UserProfile {
        user_id: 0,  // Zero value
        username: "".to_string(),  // Zero value
        ..Default::default()
    };
    
    // Cannot distinguish if user_id was set to 0 or never set
    if profile1.user_id == 0 {
        println!("Could be unset or explicitly zero");
    }
    
    // Proto3 with optional (becomes Option<T>)
    let mut profile2 = UserProfile::default();
    
    // last_login is Option<String>
    match profile2.last_login {
        None => println!("Last login definitely not set"),
        Some(ref val) if val.is_empty() => println!("Last login set to empty string"),
        Some(ref val) => println!("Last login: {}", val),
    }
    
    // Explicitly set to empty string
    profile2.last_login = Some("".to_string());
    
    assert!(profile2.last_login.is_some());  // We know it was set!
}
```

**Enum Handling Differences:**

```rust
// Proto2 enum
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Proto2Status {
    OFFLINE = 1,
    ONLINE = 2,
    AWAY = 3,
}

// Proto3 enum - must start at 0
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Proto3Status {
    Unspecified = 0,  // Required zero value
    Offline = 1,
    Online = 2,
    Away = 3,
}

fn enum_examples() {
    // Proto3 enum usage
    let mut profile = UserProfile::default();
    
    // Default is the zero value
    assert_eq!(profile.status, Status::Unspecified as i32);
    
    profile.status = Status::Online as i32;
    
    // Pattern matching
    match Status::try_from(profile.status) {
        Ok(Status::Unspecified) => println!("Status not set"),
        Ok(Status::Online) => println!("User is online"),
        Ok(_) => println!("Other status"),
        Err(_) => println!("Invalid status value"),
    }
}
```

## Migration Considerations

### Proto2 to Proto3

When migrating from proto2 to proto3, consider:

1. **Remove `required` fields**: Convert to regular fields or use `optional` keyword if presence detection is needed
2. **Remove custom defaults**: Update application logic to handle zero values
3. **Update enum values**: Ensure first enum value is 0 and add `_UNSPECIFIED` variant
4. **Replace extensions**: Use `Any` type or nested messages
5. **Update field presence checks**: Use `optional` keyword where needed
6. **Test thoroughly**: Zero-value semantics can change behavior

### Proto3 to Proto2

Reverse migration is generally not recommended but possible:

1. **Add field labels**: Choose between `optional`, `required`, `repeated`
2. **Add default values**: Specify custom defaults if needed
3. **Update enums**: First value can be non-zero
4. **Consider extensions**: If extensibility is needed

## Summary

Proto2 and Proto3 represent different philosophies in API design. Proto2 offers more explicit control over field presence, custom defaults, and strict validation through required fields. Proto3 simplifies the syntax, embraces zero-value semantics, and provides better JSON interoperability at the cost of some field presence detection (unless using `optional`).

**Choose Proto2 when:**
- You need required field validation
- Custom default values are important
- You need to distinguish between unset and zero values extensively
- Working with legacy systems

**Choose Proto3 when:**
- Starting new projects
- JSON compatibility is important
- You prefer simpler, cleaner syntax
- Zero-value semantics align with your use case
- You want better language interoperability

The addition of the `optional` keyword in proto3.12+ has narrowed the gap, allowing proto3 users to opt-in to field presence tracking when needed while maintaining the simplified syntax. For most modern applications, proto3 is the recommended choice, with selective use of `optional` for fields where presence detection is critical.