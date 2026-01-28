# Protocol Buffers: Range and Pattern Constraints

## Overview

Protocol Buffers (Protobuf) natively provides strong type guarantees for structured data but doesn't enforce semantic validation rules for field values. To address this limitation, validation frameworks have been developed to allow defining constraints directly in `.proto` files through custom options. These constraints include numeric ranges, string patterns, and collection size limits.

The two primary validation frameworks are:
- **protoc-gen-validate (PGV)**: The original validation library (now superseded)
- **protovalidate**: The next-generation validation library using Common Expression Language (CEL)

## Key Concepts

### 1. Numeric Range Constraints

Numeric constraints allow you to define boundaries for numeric field values using comparison operators:

- **`const`**: Field must equal exactly the specified value
- **`lt` / `lte`**: Less than / Less than or equal to
- **`gt` / `gte`**: Greater than / Greater than or equal to
- **`in` / `not_in`**: Value must be in/not in a specified list

### 2. String Pattern Constraints

String constraints validate text content through:

- **`pattern`**: Regular expression validation
- **`min_len` / `max_len`**: Length constraints (character count)
- **`min_bytes` / `max_bytes`**: Byte size constraints
- **Well-known formats**: Email, URI, UUID, IP address validation
- **`prefix` / `suffix` / `contains`**: String content matching

### 3. Collection Size Constraints

For repeated fields and maps:

- **`min_items` / `max_items`**: Number of elements
- **`unique`**: All elements must be unique
- **`items`**: Constraints on individual collection elements

## Proto Definition Examples

### Basic Numeric Range Constraints

```protobuf
syntax = "proto3";

package examples;

import "buf/validate/validate.proto";

message Product {
  // Price must be greater than 0
  double price = 1 [(buf.validate.field).double.gt = 0];
  
  // Quantity must be between 1 and 1000 (inclusive)
  int32 quantity = 2 [
    (buf.validate.field).int32 = {
      gte: 1,
      lte: 1000
    }
  ];
  
  // Discount percentage must be in range [0, 100)
  float discount_percent = 3 [
    (buf.validate.field).float = {
      gte: 0,
      lt: 100
    }
  ];
  
  // Age must be exactly one of these values
  int32 category_id = 4 [
    (buf.validate.field).int32 = {
      in: [1, 2, 3, 5, 8, 13]
    }
  ];
}
```

### String Pattern Constraints

```protobuf
syntax = "proto3";

package examples;

import "buf/validate/validate.proto";

message User {
  // ID must be a valid UUID
  string id = 1 [(buf.validate.field).string.uuid = true];
  
  // Email must be valid (RFC 5322)
  string email = 2 [(buf.validate.field).string.email = true];
  
  // Username: alphanumeric, 3-20 characters
  string username = 3 [
    (buf.validate.field).string = {
      pattern: "^[a-zA-Z0-9]{3,20}$",
      min_len: 3,
      max_len: 20
    }
  ];
  
  // Phone number pattern (US format)
  string phone = 4 [
    (buf.validate.field).string.pattern = "^\\+1[0-9]{10}$"
  ];
  
  // First name: letters and spaces only, max 64 bytes
  string first_name = 5 [
    (buf.validate.field).string = {
      pattern: "^[A-Za-z]+( [A-Za-z]+)*$",
      max_bytes: 64
    }
  ];
  
  // URL must be valid
  string website = 6 [(buf.validate.field).string.uri = true];
}
```

### Collection Size Constraints

```protobuf
syntax = "proto3";

package examples;

import "buf/validate/validate.proto";

message Order {
  // Must have at least 1 item, max 100
  repeated OrderItem items = 1 [
    (buf.validate.field).repeated = {
      min_items: 1,
      max_items: 100
    }
  ];
  
  // All tag values must be unique
  repeated string tags = 2 [
    (buf.validate.field).repeated = {
      unique: true,
      min_items: 0,
      max_items: 10
    }
  ];
  
  // Map with size constraints
  map<string, int32> metadata = 3 [
    (buf.validate.field).map = {
      min_pairs: 1,
      max_pairs: 50
    }
  ];
}

message OrderItem {
  string product_id = 1 [(buf.validate.field).string.uuid = true];
  int32 quantity = 2 [(buf.validate.field).int32.gt = 0];
}
```

### Geographic Coordinates Example

```protobuf
syntax = "proto3";

package examples;

import "buf/validate/validate.proto";

message Location {
  // Latitude: -90 to 90
  double latitude = 1 [
    (buf.validate.field).double = {
      gte: -90,
      lte: 90
    }
  ];
  
  // Longitude: -180 to 180
  double longitude = 2 [
    (buf.validate.field).double = {
      gte: -180,
      lte: 180
    }
  ];
  
  // Altitude in meters, can be negative (below sea level)
  double altitude_meters = 3 [
    (buf.validate.field).double = {
      gte: -500,
      lte: 9000
    }
  ];
}
```

## C++ Implementation

### Setup and Installation

```bash
# Clone protovalidate-cc
git clone https://github.com/bufbuild/protovalidate-cc.git
cd protovalidate-cc
make build
```

### C++ Usage Example

```cpp
// validation_example.cpp
#include <iostream>
#include <memory>
#include <google/protobuf/arena.h>
#include "buf/validate/validator.h"
#include "examples.pb.h"

int main() {
    // Create validator factory
    auto factory_result = buf::validate::ValidatorFactory::New();
    if (!factory_result.ok()) {
        std::cerr << "Failed to create validator factory: " 
                  << factory_result.status().ToString() << std::endl;
        return 1;
    }
    
    std::unique_ptr<buf::validate::ValidatorFactory> factory = 
        std::move(factory_result.value());
    
    // Create arena for memory management
    google::protobuf::Arena arena;
    
    // Create validator
    buf::validate::Validator validator = factory->NewValidator(&arena);
    
    // Example 1: Valid Product
    examples::Product valid_product;
    valid_product.set_price(99.99);
    valid_product.set_quantity(10);
    valid_product.set_discount_percent(15.5);
    valid_product.set_category_id(3);
    
    auto result = validator.Validate(valid_product);
    if (!result.ok()) {
        std::cerr << "Validation failed: " << result.status().ToString() << std::endl;
        return 1;
    }
    
    buf::validate::Violations violations = result.value();
    if (violations.violations_size() == 0) {
        std::cout << "Product validation: PASSED" << std::endl;
    }
    
    // Example 2: Invalid Product (price too low)
    examples::Product invalid_product;
    invalid_product.set_price(-10.0);  // Invalid: must be > 0
    invalid_product.set_quantity(5000); // Invalid: must be <= 1000
    
    result = validator.Validate(invalid_product);
    if (result.ok()) {
        violations = result.value();
        if (violations.violations_size() > 0) {
            std::cout << "\nProduct validation failed with violations:" << std::endl;
            for (int i = 0; i < violations.violations_size(); ++i) {
                const auto& violation = violations.violations(i);
                std::cout << "  - Field: " << violation.field_path() 
                          << ", Message: " << violation.message() << std::endl;
            }
        }
    }
    
    // Example 3: User validation with string patterns
    examples::User user;
    user.set_id("550e8400-e29b-41d4-a716-446655440000"); // Valid UUID
    user.set_email("user@example.com");
    user.set_username("john_doe");
    user.set_phone("+12025551234");
    user.set_first_name("John Smith");
    user.set_website("https://example.com");
    
    result = validator.Validate(user);
    if (result.ok()) {
        violations = result.value();
        if (violations.violations_size() == 0) {
            std::cout << "\nUser validation: PASSED" << std::endl;
        } else {
            std::cout << "\nUser validation: FAILED" << std::endl;
            for (int i = 0; i < violations.violations_size(); ++i) {
                const auto& violation = violations.violations(i);
                std::cout << "  - " << violation.field_path() << ": " 
                          << violation.message() << std::endl;
            }
        }
    }
    
    // Example 4: Location with range constraints
    examples::Location location;
    location.set_latitude(37.7749);   // Valid: San Francisco latitude
    location.set_longitude(-122.4194); // Valid: San Francisco longitude
    location.set_altitude_meters(16.0);
    
    result = validator.Validate(location);
    if (result.ok() && result.value().violations_size() == 0) {
        std::cout << "\nLocation validation: PASSED" << std::endl;
    }
    
    // Invalid location
    examples::Location invalid_location;
    invalid_location.set_latitude(200.0);  // Invalid: > 90
    invalid_location.set_longitude(-200.0); // Invalid: < -180
    
    result = validator.Validate(invalid_location);
    if (result.ok()) {
        violations = result.value();
        if (violations.violations_size() > 0) {
            std::cout << "\nInvalid location violations:" << std::endl;
            for (int i = 0; i < violations.violations_size(); ++i) {
                std::cout << "  - " << violations.violations(i).message() << std::endl;
            }
        }
    }
    
    return 0;
}
```

### C++ CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.15)
project(protovalidate_example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Protobuf REQUIRED)
find_package(absl REQUIRED)

# Include protovalidate
include_directories(${CMAKE_SOURCE_DIR}/protovalidate-cc/include)
link_directories(${CMAKE_SOURCE_DIR}/protovalidate-cc/lib)

# Generate protobuf code
set(PROTO_FILES
    examples.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Add executable
add_executable(validation_example
    validation_example.cpp
    ${PROTO_SRCS}
)

target_link_libraries(validation_example
    ${Protobuf_LIBRARIES}
    protovalidate_cc
    absl::strings
    absl::status
)
```

## Rust Implementation

### Setup with prost-validate

Add dependencies to `Cargo.toml`:

```toml
[dependencies]
prost = "0.12"
prost-validate = { version = "0.2", features = ["derive"] }
prost-types = "0.12"

[build-dependencies]
prost-build = "0.12"
prost-validate-build = "0.2"
```

### Rust Build Script (build.rs)

```rust
// build.rs
fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure prost-validate builder
    prost_validate_build::Builder::new()
        .compile_protos(
            &[
                "proto/examples.proto",
            ],
            &[
                "proto",
                "proto/validate", // Path to validate.proto definitions
            ],
        )?;
    
    Ok(())
}
```

### Rust Usage Example

```rust
// main.rs
use prost::Message;
use prost_validate::Validator;

// Include generated code
pub mod examples {
    include!(concat!(env!("OUT_DIR"), "/examples.rs"));
}

use examples::*;

fn main() {
    println!("=== Protocol Buffer Validation Examples in Rust ===\n");
    
    // Example 1: Valid Product
    validate_product_valid();
    
    // Example 2: Invalid Product
    validate_product_invalid();
    
    // Example 3: User with string patterns
    validate_user();
    
    // Example 4: Location with range constraints
    validate_location();
    
    // Example 5: Order with collection constraints
    validate_order();
}

fn validate_product_valid() {
    let product = Product {
        price: 99.99,
        quantity: 50,
        discount_percent: 15.0,
        category_id: 3,
    };
    
    match product.validate() {
        Ok(_) => println!("✓ Valid product passed validation"),
        Err(e) => println!("✗ Product validation failed: {}", e),
    }
}

fn validate_product_invalid() {
    let invalid_product = Product {
        price: -10.0,      // Invalid: must be > 0
        quantity: 2000,    // Invalid: must be <= 1000
        discount_percent: 150.0, // Invalid: must be < 100
        category_id: 99,   // Invalid: not in allowed list
    };
    
    match invalid_product.validate() {
        Ok(_) => println!("Unexpected: Invalid product passed"),
        Err(e) => {
            println!("\n✗ Invalid product correctly rejected:");
            println!("   Error: {}", e);
        }
    }
}

fn validate_user() {
    // Valid user
    let valid_user = User {
        id: "550e8400-e29b-41d4-a716-446655440000".to_string(),
        email: "john.doe@example.com".to_string(),
        username: "johndoe123".to_string(),
        phone: "+12025551234".to_string(),
        first_name: "John Smith".to_string(),
        website: "https://example.com".to_string(),
    };
    
    match valid_user.validate() {
        Ok(_) => println!("\n✓ Valid user passed validation"),
        Err(e) => println!("✗ User validation failed: {}", e),
    }
    
    // Invalid user
    let invalid_user = User {
        id: "not-a-uuid".to_string(),           // Invalid UUID
        email: "invalid-email".to_string(),     // Invalid email
        username: "ab".to_string(),             // Too short (min 3)
        phone: "1234567890".to_string(),        // Invalid format
        first_name: "John123".to_string(),      // Contains numbers
        website: "not-a-url".to_string(),       // Invalid URL
    };
    
    match invalid_user.validate() {
        Ok(_) => println!("Unexpected: Invalid user passed"),
        Err(e) => {
            println!("\n✗ Invalid user correctly rejected:");
            println!("   Error: {}", e);
        }
    }
}

fn validate_location() {
    // Valid location (San Francisco)
    let sf_location = Location {
        latitude: 37.7749,
        longitude: -122.4194,
        altitude_meters: 16.0,
    };
    
    match sf_location.validate() {
        Ok(_) => println!("\n✓ San Francisco location is valid"),
        Err(e) => println!("✗ Location validation failed: {}", e),
    }
    
    // Invalid location (coordinates out of range)
    let invalid_location = Location {
        latitude: 200.0,   // Invalid: > 90
        longitude: -200.0, // Invalid: < -180
        altitude_meters: 10000.0, // Invalid: > 9000
    };
    
    match invalid_location.validate() {
        Ok(_) => println!("Unexpected: Invalid location passed"),
        Err(e) => {
            println!("\n✗ Invalid location correctly rejected:");
            println!("   Error: {}", e);
        }
    }
}

fn validate_order() {
    // Valid order
    let item1 = OrderItem {
        product_id: "550e8400-e29b-41d4-a716-446655440000".to_string(),
        quantity: 2,
    };
    
    let item2 = OrderItem {
        product_id: "6ba7b810-9dad-11d1-80b4-00c04fd430c8".to_string(),
        quantity: 5,
    };
    
    let mut metadata = std::collections::HashMap::new();
    metadata.insert("source".to_string(), 1);
    metadata.insert("priority".to_string(), 2);
    
    let order = Order {
        items: vec![item1, item2],
        tags: vec!["urgent".to_string(), "wholesale".to_string()],
        metadata,
    };
    
    match order.validate() {
        Ok(_) => println!("\n✓ Valid order passed validation"),
        Err(e) => println!("✗ Order validation failed: {}", e),
    }
    
    // Invalid order (empty items)
    let invalid_order = Order {
        items: vec![], // Invalid: must have min 1 item
        tags: vec![],
        metadata: std::collections::HashMap::new(),
    };
    
    match invalid_order.validate() {
        Ok(_) => println!("Unexpected: Empty order passed"),
        Err(e) => {
            println!("\n✗ Empty order correctly rejected:");
            println!("   Error: {}", e);
        }
    }
}

// Helper function to demonstrate manual validation
fn demonstrate_manual_validation() {
    println!("\n=== Manual Validation Checks ===\n");
    
    let product = Product {
        price: 50.0,
        quantity: 100,
        discount_percent: 10.0,
        category_id: 2,
    };
    
    // You can also implement custom validation logic
    if product.price > 0.0 && product.quantity > 0 {
        println!("✓ Product passes basic checks");
    }
    
    // Combining validation with business logic
    if product.discount_percent > 50.0 {
        println!("⚠ Warning: High discount percentage!");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_valid_product() {
        let product = Product {
            price: 99.99,
            quantity: 10,
            discount_percent: 15.0,
            category_id: 3,
        };
        
        assert!(product.validate().is_ok());
    }
    
    #[test]
    fn test_invalid_product_price() {
        let product = Product {
            price: -10.0, // Invalid
            quantity: 10,
            discount_percent: 15.0,
            category_id: 3,
        };
        
        assert!(product.validate().is_err());
    }
    
    #[test]
    fn test_invalid_product_quantity() {
        let product = Product {
            price: 99.99,
            quantity: 2000, // Invalid: exceeds max
            discount_percent: 15.0,
            category_id: 3,
        };
        
        assert!(product.validate().is_err());
    }
    
    #[test]
    fn test_location_boundaries() {
        // North Pole
        let north_pole = Location {
            latitude: 90.0,
            longitude: 0.0,
            altitude_meters: 0.0,
        };
        assert!(north_pole.validate().is_ok());
        
        // South Pole
        let south_pole = Location {
            latitude: -90.0,
            longitude: 180.0,
            altitude_meters: 0.0,
        };
        assert!(south_pole.validate().is_ok());
        
        // Invalid latitude
        let invalid = Location {
            latitude: 91.0,
            longitude: 0.0,
            altitude_meters: 0.0,
        };
        assert!(invalid.validate().is_err());
    }
}
```

### Alternative: Using protocheck (Compile-time Validation)

```rust
// Alternative Rust implementation using protocheck
// Cargo.toml additions:
// [dependencies]
// protocheck = "0.1"
// proto-types = "0.1"
//
// [build-dependencies]
// protocheck-build = "0.1"

use protocheck::ProtoValidator;

fn main() {
    let product = Product {
        price: 99.99,
        quantity: 50,
        discount_percent: 15.0,
        category_id: 3,
    };
    
    // Compile-time generated validation
    match product.validate() {
        Ok(_) => println!("Validation successful"),
        Err(violations) => {
            println!("Validation failed:");
            for violation in violations.violations {
                println!("  - Field: {}", 
                    violation.field_path_str().unwrap_or("unknown"));
                println!("    Message: {}", violation.message());
            }
        }
    }
}
```

## Advanced Patterns

### Custom CEL Expressions

```protobuf
syntax = "proto3";

package examples;

import "buf/validate/validate.proto";
import "google/protobuf/timestamp.proto";

message DateRange {
  google.protobuf.Timestamp start_date = 1;
  google.protobuf.Timestamp end_date = 2;
  
  // Custom rule: end_date must be after start_date
  option (buf.validate.message).cel = {
    id: "date_range.end_after_start"
    message: "end_date must be after start_date"
    expression: "this.end_date > this.start_date"
  };
}

message Account {
  string email = 1 [(buf.validate.field).string.email = true];
  string first_name = 2;
  string last_name = 3;
  
  // Custom rule: last_name required if first_name is present
  option (buf.validate.message).cel = {
    id: "account.last_name_required"
    message: "last_name must be present if first_name is present"
    expression: "!has(this.first_name) || has(this.last_name)"
  };
}
```

### Conditional Validation

```protobuf
message PaymentMethod {
  enum Type {
    UNSPECIFIED = 0;
    CREDIT_CARD = 1;
    BANK_TRANSFER = 2;
  }
  
  Type type = 1;
  string credit_card_number = 2;
  string bank_account = 3;
  
  // Validate credit card number only if type is CREDIT_CARD
  option (buf.validate.message).cel = {
    id: "payment.credit_card_required"
    message: "credit_card_number required for CREDIT_CARD payment"
    expression: "this.type != 1 || has(this.credit_card_number)"
  };
  
  option (buf.validate.message).cel = {
    id: "payment.bank_account_required"
    message: "bank_account required for BANK_TRANSFER payment"
    expression: "this.type != 2 || has(this.bank_account)"
  };
}
```

## Common Validation Patterns

### Email and Contact Information

```protobuf
message Contact {
  string email = 1 [
    (buf.validate.field).string.email = true,
    (buf.validate.field).ignore = IGNORE_IF_UNSPECIFIED
  ];
  
  string phone = 2 [
    (buf.validate.field).string.pattern = "^\\+?[1-9]\\d{1,14}$"
  ];
  
  string website = 3 [
    (buf.validate.field).string.uri = true
  ];
}
```

### Financial Data

```protobuf
message Transaction {
  // Amount in cents (to avoid floating point issues)
  int64 amount_cents = 1 [
    (buf.validate.field).int64 = {
      gt: 0,
      lte: 100000000  // Max $1M
    }
  ];
  
  string currency = 2 [
    (buf.validate.field).string = {
      len: 3,
      pattern: "^[A-Z]{3}$"  // ISO 4217
    }
  ];
}
```

### Identifier Validation

```protobuf
message Resource {
  // UUID v4
  string id = 1 [(buf.validate.field).string.uuid = true];
  
  // SKU: 2 letters + 6-10 digits
  string sku = 2 [
    (buf.validate.field).string.pattern = "^[A-Z]{2}\\d{6,10}$"
  ];
  
  // Slug: lowercase, hyphens allowed
  string slug = 3 [
    (buf.validate.field).string.pattern = "^[a-z0-9]+(?:-[a-z0-9]+)*$"
  ];
}
```

## Best Practices

1. **Start with Standard Rules**: Use built-in validators (email, UUID, URI) before writing custom patterns
2. **Fail Fast**: Validate at the earliest possible point in your application
3. **Clear Error Messages**: Provide descriptive messages in CEL expressions
4. **Performance Considerations**: Validation adds overhead; consider caching validators in hot paths
5. **Test Edge Cases**: Test boundary values, empty strings, null values
6. **Document Constraints**: Comment your validation rules in the proto files
7. **Version Carefully**: Changing validation rules can break existing clients
8. **Use Ignore Options**: Use `IGNORE_IF_UNSPECIFIED` for optional fields with constraints

## Performance Considerations

### C++
- Use arena allocation for large message hierarchies
- Cache validator instances when possible
- Consider validation granularity (field-level vs message-level)

### Rust
- Compile-time validation (protocheck) has zero runtime overhead for many checks
- Use derive-based validation (prost-validate) for better performance
- Consider lazy validation for large collections

## Troubleshooting

### Common Issues

1. **Pattern not matching**: Remember to escape special regex characters
   ```protobuf
   // Wrong
   pattern: "^test.com$"
   // Correct
   pattern: "^test\\.com$"
   ```

2. **UTF-8 validation**: String length vs byte length
   ```protobuf
   // Character count
   min_len: 5
   // Byte count (important for non-ASCII)
   min_bytes: 10
   ```

3. **Enum validation**: Use `in` for allowed enum values
   ```protobuf
   int32 status = 1 [(buf.validate.field).int32.in = [0, 1, 2]];
   ```

## Summary

Range and pattern constraints in Protocol Buffers provide a declarative way to enforce data validation rules directly in schema definitions. Key benefits include:

- **Type Safety**: Validation rules are part of the schema, checked at compile/generation time
- **Cross-Language**: Same validation logic works across C++, Rust, Go, Java, Python, etc.
- **Performance**: Generated code is optimized for each target language
- **Maintainability**: Validation rules colocated with data definitions
- **Expressiveness**: CEL expressions enable complex custom validation logic

The evolution from protoc-gen-validate to protovalidate represents a move toward more powerful expression capabilities (CEL), better runtime performance, and improved developer experience. Both C++ (protovalidate-cc) and Rust (prost-validate, protocheck) implementations provide idiomatic APIs that integrate smoothly with existing protobuf workflows.

Whether you're validating geographic coordinates, user inputs, financial transactions, or complex business rules, Protocol Buffer validation frameworks provide the tools to enforce data integrity at the schema level, reducing bugs and improving system reliability.