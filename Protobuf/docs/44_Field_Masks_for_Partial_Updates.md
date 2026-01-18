# Field Masks for Partial Updates in Protocol Buffers

## Overview

Field masks are a powerful Protocol Buffers feature that allows you to specify exactly which fields should be updated in a PATCH operation, rather than replacing the entire message. This is implemented using `google.protobuf.FieldMask`, which contains a list of field paths indicating which fields are relevant for an operation.

## Why Field Masks?

When updating resources via APIs, you often want to:
- Update only specific fields without affecting others
- Avoid race conditions from overwriting fields you didn't intend to modify
- Clearly communicate which fields are being updated
- Support partial updates efficiently

Field masks solve these problems by explicitly listing which fields should be considered in an update operation.

## How Field Masks Work

A `FieldMask` contains a repeated string field called `paths`, where each path specifies a field in the message using dot notation. For example:
- `"name"` - refers to the top-level `name` field
- `"address.city"` - refers to the `city` field within the `address` nested message
- `"tags"` - refers to the entire `tags` repeated field

## C/C++ Implementation

### Proto Definition

```protobuf
syntax = "proto3";

import "google/protobuf/field_mask.proto";

package example;

message Address {
  string street = 1;
  string city = 2;
  string state = 3;
  string zip_code = 4;
}

message User {
  string id = 1;
  string name = 2;
  string email = 3;
  int32 age = 4;
  Address address = 5;
  repeated string tags = 6;
}

message UpdateUserRequest {
  User user = 1;
  google.protobuf.FieldMask update_mask = 2;
}
```

### C++ Example

```cpp
#include <google/protobuf/field_mask.pb.h>
#include <google/protobuf/util/field_mask_util.h>
#include "user.pb.h"
#include <iostream>

using google::protobuf::FieldMask;
using google::protobuf::util::FieldMaskUtil;

// Apply field mask to update only specified fields
void ApplyPartialUpdate(const example::User& source, 
                       const FieldMask& mask,
                       example::User* destination) {
    // Merge only the fields specified in the mask
    FieldMaskUtil::MergeMessageTo(source, mask, 
                                  FieldMaskUtil::MergeOptions(), 
                                  destination);
}

int main() {
    // Existing user in database
    example::User existing_user;
    existing_user.set_id("user123");
    existing_user.set_name("John Doe");
    existing_user.set_email("john@example.com");
    existing_user.set_age(30);
    existing_user.mutable_address()->set_city("New York");
    existing_user.mutable_address()->set_state("NY");
    
    std::cout << "Original User:\n" << existing_user.DebugString() << "\n";
    
    // Update request - only changing email and city
    example::UpdateUserRequest update_request;
    example::User* updated_fields = update_request.mutable_user();
    updated_fields->set_email("john.doe@newdomain.com");
    updated_fields->mutable_address()->set_city("San Francisco");
    
    // Create field mask specifying which fields to update
    FieldMask* mask = update_request.mutable_update_mask();
    mask->add_paths("email");
    mask->add_paths("address.city");
    
    std::cout << "Field Mask: " << mask->DebugString() << "\n";
    
    // Apply the partial update
    ApplyPartialUpdate(*updated_fields, *mask, &existing_user);
    
    std::cout << "After Partial Update:\n" << existing_user.DebugString() << "\n";
    
    // Demonstrate field mask utilities
    
    // Convert from field mask to string
    std::string mask_string;
    FieldMaskUtil::ToJsonString(*mask, &mask_string);
    std::cout << "Mask as JSON string: " << mask_string << "\n";
    
    // Create field mask from string
    FieldMask mask_from_string;
    FieldMaskUtil::FromJsonString("name,age,address.state", &mask_from_string);
    std::cout << "Mask from string: " << mask_from_string.DebugString() << "\n";
    
    // Check if a field is in the mask
    FieldMask check_mask;
    check_mask.add_paths("email");
    check_mask.add_paths("address.city");
    
    if (FieldMaskUtil::IsPathInFieldMask("email", check_mask)) {
        std::cout << "'email' is in the field mask\n";
    }
    
    // Validate field mask against descriptor
    std::string error;
    if (!FieldMaskUtil::IsValidFieldMask(check_mask, 
                                         example::User::descriptor(),
                                         &error)) {
        std::cerr << "Invalid field mask: " << error << "\n";
    } else {
        std::cout << "Field mask is valid\n";
    }
    
    return 0;
}
```

### C Example (using nanopb for embedded systems)

```c
#include "user.pb.h"
#include <stdio.h>
#include <string.h>

// Simple field mask implementation for embedded C
typedef struct {
    char paths[10][64];  // Max 10 paths, 64 chars each
    size_t count;
} SimpleFieldMask;

// Check if a field should be updated based on mask
bool should_update_field(const SimpleFieldMask* mask, const char* field_path) {
    for (size_t i = 0; i < mask->count; i++) {
        if (strcmp(mask->paths[i], field_path) == 0) {
            return true;
        }
    }
    return false;
}

// Apply partial update with field mask
void apply_partial_update(const User* source, 
                         const SimpleFieldMask* mask,
                         User* destination) {
    if (should_update_field(mask, "name")) {
        strcpy(destination->name, source->name);
    }
    if (should_update_field(mask, "email")) {
        strcpy(destination->email, source->email);
    }
    if (should_update_field(mask, "age")) {
        destination->age = source->age;
    }
    if (should_update_field(mask, "address.city")) {
        strcpy(destination->address.city, source->address.city);
    }
    if (should_update_field(mask, "address.state")) {
        strcpy(destination->address.state, source->address.state);
    }
}

int main() {
    User existing = {
        .id = "user123",
        .name = "John Doe",
        .email = "john@example.com",
        .age = 30,
        .address = {.city = "New York", .state = "NY"}
    };
    
    User updates = {
        .email = "john.doe@newdomain.com",
        .address = {.city = "San Francisco"}
    };
    
    SimpleFieldMask mask = {
        .paths = {"email", "address.city"},
        .count = 2
    };
    
    printf("Before: %s, %s, %s\n", 
           existing.name, existing.email, existing.address.city);
    
    apply_partial_update(&updates, &mask, &existing);
    
    printf("After: %s, %s, %s\n", 
           existing.name, existing.email, existing.address.city);
    
    return 0;
}
```

## Rust Implementation

### Proto Definition (same as above)

### Rust Example using prost

```rust
use prost::Message;
use prost_types::FieldMask;

// Generated from proto file
mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use example::{User, Address, UpdateUserRequest};

/// Apply field mask to perform partial update
fn apply_field_mask(
    source: &User,
    mask: &FieldMask,
    destination: &mut User,
) {
    for path in &mask.paths {
        match path.as_str() {
            "name" => destination.name = source.name.clone(),
            "email" => destination.email = source.email.clone(),
            "age" => destination.age = source.age,
            "address.street" => {
                if let Some(ref addr) = source.address {
                    destination.address
                        .get_or_insert_with(Address::default)
                        .street = addr.street.clone();
                }
            }
            "address.city" => {
                if let Some(ref addr) = source.address {
                    destination.address
                        .get_or_insert_with(Address::default)
                        .city = addr.city.clone();
                }
            }
            "address.state" => {
                if let Some(ref addr) = source.address {
                    destination.address
                        .get_or_insert_with(Address::default)
                        .state = addr.state.clone();
                }
            }
            "address.zip_code" => {
                if let Some(ref addr) = source.address {
                    destination.address
                        .get_or_insert_with(Address::default)
                        .zip_code = addr.zip_code.clone();
                }
            }
            "address" => {
                destination.address = source.address.clone();
            }
            "tags" => {
                destination.tags = source.tags.clone();
            }
            _ => {
                eprintln!("Unknown field path: {}", path);
            }
        }
    }
}

/// Check if a specific field is in the mask
fn is_field_in_mask(mask: &FieldMask, field_path: &str) -> bool {
    mask.paths.iter().any(|path| {
        path == field_path || path.starts_with(&format!("{}.", field_path))
    })
}

/// Normalize field mask paths (convert from JSON to protobuf format)
fn normalize_field_mask(mask: &mut FieldMask) {
    for path in &mut mask.paths {
        // Convert camelCase to snake_case if needed
        *path = path.replace(".", ".");
    }
}

fn main() {
    // Existing user in database
    let mut existing_user = User {
        id: "user123".to_string(),
        name: "John Doe".to_string(),
        email: "john@example.com".to_string(),
        age: 30,
        address: Some(Address {
            street: "123 Main St".to_string(),
            city: "New York".to_string(),
            state: "NY".to_string(),
            zip_code: "10001".to_string(),
        }),
        tags: vec!["developer".to_string(), "rust".to_string()],
    };
    
    println!("Original User:\n{:#?}\n", existing_user);
    
    // Update request - only changing email and city
    let update_data = User {
        id: String::new(),
        name: String::new(),
        email: "john.doe@newdomain.com".to_string(),
        age: 0,
        address: Some(Address {
            street: String::new(),
            city: "San Francisco".to_string(),
            state: String::new(),
            zip_code: String::new(),
        }),
        tags: vec![],
    };
    
    // Create field mask
    let field_mask = FieldMask {
        paths: vec![
            "email".to_string(),
            "address.city".to_string(),
        ],
    };
    
    println!("Field Mask: {:?}\n", field_mask);
    
    // Apply partial update
    apply_field_mask(&update_data, &field_mask, &mut existing_user);
    
    println!("After Partial Update:\n{:#?}\n", existing_user);
    
    // Check if specific fields are in mask
    println!("Is 'email' in mask? {}", is_field_in_mask(&field_mask, "email"));
    println!("Is 'name' in mask? {}", is_field_in_mask(&field_mask, "name"));
    println!("Is 'address' in mask? {}", is_field_in_mask(&field_mask, "address"));
    
    // Example: Create UpdateUserRequest
    let update_request = UpdateUserRequest {
        user: Some(update_data),
        update_mask: Some(field_mask),
    };
    
    // Serialize to bytes
    let mut buf = Vec::new();
    update_request.encode(&mut buf).unwrap();
    println!("\nSerialized request size: {} bytes", buf.len());
    
    // Deserialize
    let decoded = UpdateUserRequest::decode(&buf[..]).unwrap();
    println!("Decoded field mask: {:?}", decoded.update_mask);
}
```

### Advanced Rust Example with Validation

```rust
use prost_types::FieldMask;
use std::collections::HashSet;

/// Validate that all paths in the field mask are valid for User
fn validate_field_mask(mask: &FieldMask) -> Result<(), String> {
    let valid_paths: HashSet<&str> = [
        "id", "name", "email", "age", "address", "tags",
        "address.street", "address.city", "address.state", "address.zip_code"
    ].iter().copied().collect();
    
    for path in &mask.paths {
        if !valid_paths.contains(path.as_str()) {
            return Err(format!("Invalid field path: {}", path));
        }
    }
    
    Ok(())
}

/// Create a field mask from a comma-separated string
fn field_mask_from_string(s: &str) -> FieldMask {
    FieldMask {
        paths: s.split(',')
            .map(|p| p.trim().to_string())
            .filter(|p| !p.is_empty())
            .collect(),
    }
}

/// Convert field mask to a comma-separated string
fn field_mask_to_string(mask: &FieldMask) -> String {
    mask.paths.join(",")
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_field_mask_validation() {
        let valid_mask = FieldMask {
            paths: vec!["email".to_string(), "address.city".to_string()],
        };
        assert!(validate_field_mask(&valid_mask).is_ok());
        
        let invalid_mask = FieldMask {
            paths: vec!["invalid_field".to_string()],
        };
        assert!(validate_field_mask(&invalid_mask).is_err());
    }
    
    #[test]
    fn test_field_mask_from_string() {
        let mask = field_mask_from_string("name,email,address.city");
        assert_eq!(mask.paths.len(), 3);
        assert_eq!(mask.paths[0], "name");
        assert_eq!(mask.paths[2], "address.city");
    }
}
```

## Summary

**Field Masks for Partial Updates** provide a standardized, efficient way to perform PATCH operations in Protocol Buffers:

- **Purpose**: Specify exactly which fields to update, avoiding unintended overwrites and race conditions
- **Implementation**: Uses `google.protobuf.FieldMask` with dot-notation paths (e.g., `"address.city"`)
- **C/C++ Support**: Rich utility functions in `google/protobuf/util/field_mask_util.h` for merging, validation, and path manipulation
- **Rust Support**: Manual implementation required with `prost`, involving path matching and selective field updates
- **Best Practices**:
  - Always validate field masks against message descriptors
  - Use clear, documented field paths
  - Handle nested messages and repeated fields appropriately
  - Implement server-side validation to prevent invalid updates
  - Consider using field masks in conjunction with ETags for optimistic concurrency control

Field masks are essential for building robust REST/gRPC APIs that support partial updates, making them a critical pattern for modern microservices and API design.