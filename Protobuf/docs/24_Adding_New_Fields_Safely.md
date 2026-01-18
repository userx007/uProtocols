# Adding New Fields Safely in Protocol Buffers

## Overview

One of Protocol Buffers' most powerful features is its ability to evolve schemas over time while maintaining backward and forward compatibility. Adding new fields safely is crucial for maintaining distributed systems where different services may be running different versions of your protocol definitions.

## Core Principles

### Backward Compatibility
Backward compatibility ensures that new code can read data written by old code. When you add a new field with a unique field number, old binaries simply ignore it when parsing.

### Forward Compatibility
Forward compatibility means old code can read data written by new code. Old parsers will skip unknown fields but preserve them during re-serialization, preventing data loss.

### Field Number Rules
- **Never reuse field numbers** from deleted fields
- **Never change field numbers** of existing fields
- **Always assign new, unique field numbers** to new fields
- Reserve deleted field numbers to prevent accidental reuse

## Best Practices

### 1. Use Optional Fields with Defaults

For primitive types, always consider what happens when the field is not present:

**Proto3:**
```protobuf
syntax = "proto3";

message UserProfile {
  string user_id = 1;
  string name = 2;
  
  // New fields added safely
  string email = 3;           // Empty string if not set
  int32 age = 4;              // 0 if not set
  bool is_verified = 5;       // false if not set
}
```

**Proto2:**
```protobuf
syntax = "proto2";

message UserProfile {
  required string user_id = 1;
  required string name = 2;
  
  // New fields should be optional
  optional string email = 3;
  optional int32 age = 4 [default = 0];
  optional bool is_verified = 5 [default = false];
}
```

### 2. Reserve Deleted Fields

When removing fields, always reserve their numbers and names:

```protobuf
message Product {
  reserved 2, 4, 6 to 10;
  reserved "old_price", "deprecated_field";
  
  string product_id = 1;
  string name = 3;
  double current_price = 5;
  
  // Future fields can use 11, 12, etc.
  string category = 11;
}
```

### 3. Use Nested Messages for Complex Additions

When adding multiple related fields, consider wrapping them in a message:

```protobuf
message Order {
  string order_id = 1;
  repeated string item_ids = 2;
  
  // Instead of adding many shipping fields, use a nested message
  ShippingInfo shipping = 3;
}

message ShippingInfo {
  string address = 1;
  string carrier = 2;
  string tracking_number = 3;
  // Easy to extend further
}
```

## Code Examples

### C++ Example

```cpp
#include <iostream>
#include <fstream>
#include "user_profile.pb.h"

// Original version - only has user_id and name
void WriteOriginalProfile(const std::string& filename) {
    UserProfile profile;
    profile.set_user_id("user123");
    profile.set_name("Alice Smith");
    
    std::ofstream output(filename, std::ios::binary);
    profile.SerializeToOstream(&output);
    output.close();
    
    std::cout << "Original profile written (2 fields)" << std::endl;
}

// New version - reads old data and adds new fields
void ReadAndUpgradeProfile(const std::string& filename) {
    UserProfile profile;
    
    std::ifstream input(filename, std::ios::binary);
    if (!profile.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse profile" << std::endl;
        return;
    }
    input.close();
    
    // Old fields are still accessible
    std::cout << "User ID: " << profile.user_id() << std::endl;
    std::cout << "Name: " << profile.name() << std::endl;
    
    // New fields return default values if not present
    std::cout << "Email: " << (profile.email().empty() ? "(not set)" : profile.email()) << std::endl;
    std::cout << "Age: " << profile.age() << std::endl;
    std::cout << "Verified: " << (profile.is_verified() ? "yes" : "no") << std::endl;
    
    // Add new data
    profile.set_email("alice@example.com");
    profile.set_age(30);
    profile.set_is_verified(true);
    
    // Save upgraded profile
    std::ofstream output(filename + ".upgraded", std::ios::binary);
    profile.SerializeToOstream(&output);
    output.close();
    
    std::cout << "\nUpgraded profile saved" << std::endl;
}

// Demonstrate safe field checking
void SafeFieldAccess(const UserProfile& profile) {
    // In proto3, check for empty/zero values
    if (!profile.email().empty()) {
        std::cout << "Email is set: " << profile.email() << std::endl;
    }
    
    // For proto2, you would use has_email()
    // if (profile.has_email()) { ... }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    WriteOriginalProfile("profile.bin");
    ReadAndUpgradeProfile("profile.bin");
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

### C Example (Using protobuf-c)

```c
#include <stdio.h>
#include <stdlib.h>
#include "user_profile.pb-c.h"

void write_original_profile(const char *filename) {
    UserProfile profile = USER_PROFILE__INIT;
    
    profile.user_id = "user123";
    profile.name = "Alice Smith";
    
    size_t len = user_profile__get_packed_size(&profile);
    uint8_t *buffer = malloc(len);
    user_profile__pack(&profile, buffer);
    
    FILE *file = fopen(filename, "wb");
    fwrite(buffer, len, 1, file);
    fclose(file);
    free(buffer);
    
    printf("Original profile written\n");
}

void read_and_upgrade_profile(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }
    
    fseek(file, 0, SEEK_END);
    size_t len = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    uint8_t *buffer = malloc(len);
    fread(buffer, len, 1, file);
    fclose(file);
    
    UserProfile *profile = user_profile__unpack(NULL, len, buffer);
    free(buffer);
    
    if (!profile) {
        fprintf(stderr, "Failed to parse profile\n");
        return;
    }
    
    // Access old fields
    printf("User ID: %s\n", profile->user_id);
    printf("Name: %s\n", profile->name);
    
    // New fields have default values if not present
    printf("Email: %s\n", profile->email ? profile->email : "(not set)");
    printf("Age: %d\n", profile->age);
    printf("Verified: %s\n", profile->is_verified ? "yes" : "no");
    
    user_profile__free_unpacked(profile, NULL);
}

int main(void) {
    write_original_profile("profile.bin");
    read_and_upgrade_profile("profile.bin");
    return 0;
}
```

### Rust Example

```rust
use protobuf::Message;
use std::fs::File;
use std::io::{Read, Write};

// Assuming generated code from user_profile.proto
mod user_profile_proto;
use user_profile_proto::UserProfile;

/// Write a profile using the original schema (only user_id and name)
fn write_original_profile(filename: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut profile = UserProfile::new();
    profile.set_user_id("user123".to_string());
    profile.set_name("Alice Smith".to_string());
    
    let mut file = File::create(filename)?;
    let bytes = profile.write_to_bytes()?;
    file.write_all(&bytes)?;
    
    println!("Original profile written (2 fields)");
    Ok(())
}

/// Read old data and demonstrate safe field access with new fields
fn read_and_upgrade_profile(filename: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open(filename)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;
    
    let mut profile = UserProfile::parse_from_bytes(&buffer)?;
    
    // Old fields are always accessible
    println!("User ID: {}", profile.get_user_id());
    println!("Name: {}", profile.get_name());
    
    // New fields return default values if not present
    let email = profile.get_email();
    println!("Email: {}", if email.is_empty() { "(not set)" } else { email });
    println!("Age: {}", profile.get_age());
    println!("Verified: {}", profile.get_is_verified());
    
    // Add new data
    profile.set_email("alice@example.com".to_string());
    profile.set_age(30);
    profile.set_is_verified(true);
    
    // Save upgraded profile
    let upgraded_filename = format!("{}.upgraded", filename);
    let mut output = File::create(&upgraded_filename)?;
    let bytes = profile.write_to_bytes()?;
    output.write_all(&bytes)?;
    
    println!("\nUpgraded profile saved to {}", upgraded_filename);
    Ok(())
}

/// Demonstrate safe field checking patterns
fn safe_field_access(profile: &UserProfile) {
    // Check if optional fields have meaningful values
    if !profile.get_email().is_empty() {
        println!("Email is set: {}", profile.get_email());
    }
    
    // For numeric fields, check against expected defaults
    if profile.get_age() > 0 {
        println!("Age is set: {}", profile.get_age());
    }
    
    // Boolean fields default to false
    if profile.get_is_verified() {
        println!("User is verified");
    }
}

/// Example with nested messages for complex additions
fn demonstrate_nested_fields() -> Result<(), Box<dyn std::error::Error>> {
    use user_profile_proto::{Order, ShippingInfo};
    
    let mut order = Order::new();
    order.set_order_id("order456".to_string());
    order.set_item_ids(vec!["item1".to_string(), "item2".to_string()].into());
    
    // Add new shipping info without breaking old code
    let mut shipping = ShippingInfo::new();
    shipping.set_address("123 Main St".to_string());
    shipping.set_carrier("UPS".to_string());
    shipping.set_tracking_number("1Z999AA10123456784".to_string());
    
    order.set_shipping(shipping);
    
    // Serialize
    let bytes = order.write_to_bytes()?;
    println!("Order with shipping info: {} bytes", bytes.len());
    
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    write_original_profile("profile.bin")?;
    read_and_upgrade_profile("profile.bin")?;
    demonstrate_nested_fields()?;
    
    Ok(())
}
```

## Common Pitfalls to Avoid

### ❌ Don't: Change Field Types
```protobuf
// WRONG - breaks compatibility
message Bad {
  string user_id = 1;  // Was: int32 user_id = 1;
}
```

### ✅ Do: Add New Field with Different Type
```protobuf
// CORRECT
message Good {
  int32 user_id = 1;          // Keep old field
  string user_id_string = 2;   // Add new field
}
```

### ❌ Don't: Make Fields Required (Proto2)
```protobuf
// WRONG - old data won't have this field
message Bad {
  optional string name = 1;
  required string email = 2;  // BREAKS old data!
}
```

### ✅ Do: Keep New Fields Optional
```protobuf
// CORRECT
message Good {
  optional string name = 1;
  optional string email = 2;  // Safe
}
```

## Summary

**Key Takeaways:**

1. **Field numbers are sacred** - Never reuse or change them; always reserve deleted field numbers to prevent future conflicts

2. **Default values matter** - Understand what happens when fields are missing; proto3 uses zero values, proto2 allows custom defaults

3. **Use optional semantics** - In proto2, new fields should be optional; in proto3, all fields are implicitly optional

4. **Check before accessing** - In production code, always verify field presence (proto3: check for zero/empty; proto2: use `has_*` methods)

5. **Nested messages for complexity** - When adding related fields, group them in nested messages for cleaner evolution

6. **Test compatibility** - Write tests that serialize with old schemas and deserialize with new ones, and vice versa

7. **Document changes** - Maintain clear documentation about when fields were added and any migration considerations

By following these practices, you can safely evolve your Protocol Buffer schemas over months or years while maintaining compatibility across distributed systems running different versions of your code. This is essential for zero-downtime deployments and gradual rollouts in production environments.