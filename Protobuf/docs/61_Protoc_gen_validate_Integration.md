# Protoc-gen-validate Integration

## Detailed Description

**protoc-gen-validate (PGV)** is a protoc plugin that generates validation code directly from declarative validation rules embedded in your `.proto` files. Instead of writing manual validation logic in your application code, you define constraints using special field options, and PGV automatically generates the validation functions during the protobuf compilation process.

### Key Concepts

**Declarative Validation**: You specify validation rules directly in your proto files using field options. These rules are language-agnostic and live alongside your data definitions.

**Code Generation**: The `protoc-gen-validate` plugin generates language-specific validation code (for C++, Go, Java, Python, etc.) that enforces your declared rules.

**Rich Constraint Library**: PGV provides an extensive set of validation rules including:
- Numeric constraints (min, max, ranges)
- String constraints (min/max length, patterns, email, hostname, IP addresses)
- Collection constraints (min/max items, unique elements)
- Message-level constraints
- Cross-field validation
- Custom error messages

**Performance**: Generated validation code is highly optimized and runs inline without reflection overhead.

### How It Works

1. Import the `validate/validate.proto` file in your proto definitions
2. Add validation rules using the `(validate.rules)` field option
3. Run `protoc` with the `--validate_out` plugin
4. Generated code includes `Validate()` methods for your messages
5. Call these methods in your application to enforce constraints

### Use Cases

- **API Input Validation**: Ensure incoming requests meet business rules before processing
- **Data Quality**: Enforce data integrity constraints at serialization boundaries
- **Configuration Validation**: Verify configuration files meet required specifications
- **Microservices**: Validate messages between services with consistent rules

## C/C++ Implementation

### Proto Definition with Validation Rules

```protobuf
syntax = "proto3";

package example;

import "validate/validate.proto";

message UserRegistration {
  // Username: 3-20 alphanumeric characters
  string username = 1 [(validate.rules).string = {
    min_len: 3,
    max_len: 20,
    pattern: "^[a-zA-Z0-9_]+$"
  }];
  
  // Email validation
  string email = 2 [(validate.rules).string.email = true];
  
  // Age: must be 18 or older
  int32 age = 3 [(validate.rules).int32 = {
    gte: 18,
    lte: 120
  }];
  
  // Password: minimum 8 characters
  string password = 4 [(validate.rules).string.min_len = 8];
  
  // Phone number pattern
  string phone = 5 [(validate.rules).string = {
    pattern: "^\\+?[1-9]\\d{1,14}$"
  }];
  
  // Optional field with validation when present
  string website = 6 [(validate.rules).string = {
    uri: true,
    ignore_empty: true
  }];
}

message OrderRequest {
  // Non-empty product list
  repeated string product_ids = 1 [(validate.rules).repeated = {
    min_items: 1,
    max_items: 100,
    unique: true,
    items: {
      string: {
        pattern: "^PROD-[0-9]{6}$"
      }
    }
  }];
  
  // Total must be positive
  double total_amount = 2 [(validate.rules).double = {
    gt: 0.0
  }];
  
  // Shipping address required
  Address shipping_address = 3 [(validate.rules).message.required = true];
}

message Address {
  string street = 1 [(validate.rules).string.min_len = 1];
  string city = 2 [(validate.rules).string.min_len = 1];
  string postal_code = 3 [(validate.rules).string = {
    pattern: "^[0-9]{5}(-[0-9]{4})?$"
  }];
  string country = 4 [(validate.rules).string.len = 2]; // ISO country code
}
```

### C++ Usage Example

```cpp
#include <iostream>
#include <string>
#include "user_registration.pb.h"
#include "user_registration.pb.validate.h"

namespace example {

class UserService {
public:
    bool RegisterUser(const UserRegistration& request) {
        // Validate the incoming request
        pgv::ValidationMsg error;
        if (!pgv::Validate(request, &error)) {
            std::cerr << "Validation failed: " << error.DebugString() << std::endl;
            return false;
        }
        
        // Validation passed - proceed with registration
        std::cout << "Registering user: " << request.username() << std::endl;
        // ... actual registration logic ...
        return true;
    }
    
    void ProcessOrder(const OrderRequest& order) {
        pgv::ValidationMsg error;
        
        if (!pgv::Validate(order, &error)) {
            throw std::runtime_error(
                "Invalid order: " + error.DebugString()
            );
        }
        
        std::cout << "Processing order with " 
                  << order.product_ids_size() << " items" << std::endl;
        // ... order processing logic ...
    }
};

} // namespace example

int main() {
    example::UserRegistration user;
    user.set_username("john_doe");
    user.set_email("john@example.com");
    user.set_age(25);
    user.set_password("SecurePass123");
    user.set_phone("+12125551234");
    
    example::UserService service;
    
    // This will succeed
    if (service.RegisterUser(user)) {
        std::cout << "User registered successfully!" << std::endl;
    }
    
    // Invalid user - age too young
    example::UserRegistration invalid_user;
    invalid_user.set_username("kid");
    invalid_user.set_email("kid@example.com");
    invalid_user.set_age(15); // Under 18
    invalid_user.set_password("password");
    
    // This will fail validation
    service.RegisterUser(invalid_user);
    
    // Test order validation
    example::OrderRequest order;
    order.add_product_ids("PROD-123456");
    order.add_product_ids("PROD-789012");
    order.set_total_amount(99.99);
    
    auto* address = order.mutable_shipping_address();
    address->set_street("123 Main St");
    address->set_city("Springfield");
    address->set_postal_code("12345");
    address->set_country("US");
    
    try {
        service.ProcessOrder(order);
    } catch (const std::exception& e) {
        std::cerr << "Order error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

### Advanced C++ Pattern: Validation Wrapper

```cpp
#include <expected>
#include <string>
#include "user_registration.pb.validate.h"

namespace example {

template<typename T>
class ValidatedMessage {
public:
    static std::expected<ValidatedMessage<T>, std::string> 
    Create(const T& message) {
        pgv::ValidationMsg error;
        if (!pgv::Validate(message, &error)) {
            return std::unexpected(error.DebugString());
        }
        return ValidatedMessage<T>(message);
    }
    
    const T& get() const { return message_; }
    
private:
    explicit ValidatedMessage(const T& msg) : message_(msg) {}
    T message_;
};

// Usage
void SafeProcessing() {
    UserRegistration user;
    user.set_username("alice");
    user.set_email("alice@example.com");
    user.set_age(30);
    user.set_password("SecurePassword");
    
    auto validated = ValidatedMessage<UserRegistration>::Create(user);
    
    if (validated) {
        // Type-safe: we know this message is valid
        ProcessValidUser(validated->get());
    } else {
        std::cerr << "Validation error: " << validated.error() << std::endl;
    }
}

void ProcessValidUser(const UserRegistration& user) {
    // This function only accepts validated messages
    std::cout << "Processing validated user: " << user.username() << std::endl;
}

} // namespace example
```

## Rust Implementation

### Cargo.toml Dependencies

```toml
[dependencies]
prost = "0.12"
prost-types = "0.12"
validator = "0.16"

[build-dependencies]
prost-build = "0.12"
prost-validate = "0.2"
```

### Build Script (build.rs)

```rust
fn main() {
    let mut config = prost_build::Config::new();
    
    // Enable validation generation
    config
        .type_attribute(".", "#[derive(validator::Validate)]")
        .compile_protos(
            &["proto/user_registration.proto"],
            &["proto/", "proto/validate/"],
        )
        .expect("Failed to compile protos");
}
```

### Rust Proto Definition (Same as Above)

The proto file remains the same - validation rules are language-agnostic.

### Rust Usage Example

```rust
use prost::Message;
use validator::Validate;

// Generated from proto file
pub mod example {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use example::{UserRegistration, OrderRequest, Address};

struct UserService;

impl UserService {
    fn register_user(&self, request: &UserRegistration) -> Result<(), String> {
        // Validate using the generated validation
        request.validate()
            .map_err(|e| format!("Validation failed: {}", e))?;
        
        println!("Registering user: {}", request.username);
        // ... actual registration logic ...
        Ok(())
    }
    
    fn process_order(&self, order: &OrderRequest) -> Result<(), String> {
        order.validate()
            .map_err(|e| format!("Invalid order: {}", e))?;
        
        println!("Processing order with {} items", order.product_ids.len());
        // ... order processing logic ...
        Ok(())
    }
}

fn main() {
    let mut user = UserRegistration {
        username: "john_doe".to_string(),
        email: "john@example.com".to_string(),
        age: 25,
        password: "SecurePass123".to_string(),
        phone: "+12125551234".to_string(),
        website: String::new(),
    };
    
    let service = UserService;
    
    // Valid user - should succeed
    match service.register_user(&user) {
        Ok(_) => println!("User registered successfully!"),
        Err(e) => eprintln!("Registration failed: {}", e),
    }
    
    // Invalid user - age too young
    let invalid_user = UserRegistration {
        username: "kid".to_string(),
        email: "kid@example.com".to_string(),
        age: 15, // Under 18
        password: "password".to_string(),
        phone: "+12125555678".to_string(),
        website: String::new(),
    };
    
    // This will fail validation
    if let Err(e) = service.register_user(&invalid_user) {
        eprintln!("Expected validation error: {}", e);
    }
    
    // Test order validation
    let order = OrderRequest {
        product_ids: vec![
            "PROD-123456".to_string(),
            "PROD-789012".to_string(),
        ],
        total_amount: 99.99,
        shipping_address: Some(Address {
            street: "123 Main St".to_string(),
            city: "Springfield".to_string(),
            postal_code: "12345".to_string(),
            country: "US".to_string(),
        }),
    };
    
    match service.process_order(&order) {
        Ok(_) => println!("Order processed successfully!"),
        Err(e) => eprintln!("Order error: {}", e),
    }
}
```

### Advanced Rust Pattern: Type-Safe Validated Messages

```rust
use std::marker::PhantomData;
use validator::Validate;

/// Marker trait for validated messages
pub trait Validated {}

/// Wrapper that guarantees a message has been validated
pub struct ValidatedMessage<T> {
    inner: T,
    _validated: PhantomData<dyn Validated>,
}

impl<T: Validate> ValidatedMessage<T> {
    /// Create a validated message, returning an error if validation fails
    pub fn new(message: T) -> Result<Self, validator::ValidationErrors> {
        message.validate()?;
        Ok(ValidatedMessage {
            inner: message,
            _validated: PhantomData,
        })
    }
    
    /// Get a reference to the validated message
    pub fn get(&self) -> &T {
        &self.inner
    }
    
    /// Consume the wrapper and return the inner message
    pub fn into_inner(self) -> T {
        self.inner
    }
}

// Example usage with type safety
fn process_validated_user(user: &ValidatedMessage<UserRegistration>) {
    // Type system guarantees this user has been validated
    println!("Processing validated user: {}", user.get().username);
}

fn example_type_safe_validation() {
    let user = UserRegistration {
        username: "alice".to_string(),
        email: "alice@example.com".to_string(),
        age: 30,
        password: "SecurePassword".to_string(),
        phone: "+12125551234".to_string(),
        website: String::new(),
    };
    
    match ValidatedMessage::new(user) {
        Ok(validated_user) => {
            // Can only pass validated messages to this function
            process_validated_user(&validated_user);
        }
        Err(e) => {
            eprintln!("Validation failed: {}", e);
        }
    }
}
```

### Custom Validation with Business Logic

```rust
use validator::{Validate, ValidationError};

impl UserRegistration {
    /// Additional custom validation beyond proto rules
    pub fn validate_business_rules(&self) -> Result<(), ValidationError> {
        // Check if username is not a reserved word
        let reserved = ["admin", "root", "system"];
        if reserved.contains(&self.username.as_str()) {
            return Err(ValidationError::new("reserved_username"));
        }
        
        // Check password strength (example)
        if !self.password.chars().any(|c| c.is_uppercase()) {
            return Err(ValidationError::new("password_needs_uppercase"));
        }
        
        Ok(())
    }
    
    /// Complete validation: proto rules + business rules
    pub fn validate_complete(&self) -> Result<(), String> {
        // First, validate proto rules
        self.validate()
            .map_err(|e| format!("Proto validation failed: {}", e))?;
        
        // Then, validate business rules
        self.validate_business_rules()
            .map_err(|e| format!("Business rule validation failed: {:?}", e))?;
        
        Ok(())
    }
}
```

## Summary

**protoc-gen-validate** is a powerful tool that brings declarative, type-safe validation to Protocol Buffers by:

- **Embedding validation rules directly in proto files** - making constraints part of your schema definition
- **Generating optimized validation code** for multiple languages (C++, Rust, Go, Java, Python, etc.)
- **Providing comprehensive constraint types** - from simple ranges to complex regex patterns and cross-field validations
- **Eliminating boilerplate** - no need to write repetitive validation logic in application code
- **Ensuring consistency** - the same validation rules apply across all services and languages using the proto definitions

**Key Benefits:**
- Language-agnostic validation rules
- Compile-time code generation (zero runtime overhead)
- Rich built-in validators (email, URL, IP, UUID, regex, etc.)
- Support for nested messages and repeated fields
- Custom error messages
- Integration with existing protobuf workflows

**Both C++ and Rust implementations** leverage the generated validation code to provide type-safe, performant validation at deserialization boundaries, helping catch invalid data early before it propagates through your system. The pattern of wrapping validated messages in dedicated types adds an extra layer of compile-time safety, ensuring that functions only receive data that has passed validation checks.