# Custom Validation Rules in Protocol Buffers

## Detailed Description

Custom validation rules in Protocol Buffers allow developers to implement application-specific validation logic beyond the basic type checking that protobuf provides out of the box. While Protocol Buffers ensures type safety and correct serialization/deserialization, it doesn't enforce business logic constraints such as:

- Value ranges (e.g., age must be between 0-150)
- String patterns (e.g., email format, phone numbers)
- Cross-field dependencies (e.g., end_date must be after start_date)
- Collection constraints (e.g., list must not be empty)
- Complex business rules

Custom validation is typically implemented in two ways:

1. **Post-generation validation**: Writing validation functions that operate on the generated protobuf classes
2. **Validation libraries**: Using third-party tools like `protoc-gen-validate` (PGV) that extend protobuf with validation annotations

The validation logic sits between the deserialization layer and your application logic, ensuring that only valid data enters your system.

## C/C++ Implementation

### Basic Proto Definition

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
  string user_id = 1;
  string email = 2;
  int32 age = 3;
  string phone = 4;
  repeated string tags = 5;
}

message DateRange {
  int64 start_timestamp = 1;
  int64 end_timestamp = 2;
}
```

### C++ Custom Validation Implementation

```cpp
// user_validator.h
#ifndef USER_VALIDATOR_H
#define USER_VALIDATOR_H

#include "user.pb.h"
#include <string>
#include <vector>
#include <regex>
#include <optional>

namespace example {

class ValidationError {
public:
    ValidationError(const std::string& field, const std::string& message)
        : field_(field), message_(message) {}
    
    std::string field() const { return field_; }
    std::string message() const { return message_; }
    std::string full_message() const {
        return "Field '" + field_ + "': " + message_;
    }

private:
    std::string field_;
    std::string message_;
};

class ValidationResult {
public:
    ValidationResult() : valid_(true) {}
    
    void add_error(const std::string& field, const std::string& message) {
        valid_ = false;
        errors_.emplace_back(field, message);
    }
    
    bool is_valid() const { return valid_; }
    const std::vector<ValidationError>& errors() const { return errors_; }
    
    std::string error_summary() const {
        std::string summary;
        for (const auto& error : errors_) {
            summary += error.full_message() + "\n";
        }
        return summary;
    }

private:
    bool valid_;
    std::vector<ValidationError> errors_;
};

class UserValidator {
public:
    static ValidationResult validate(const User& user) {
        ValidationResult result;
        
        // Validate user_id
        if (user.user_id().empty()) {
            result.add_error("user_id", "must not be empty");
        } else if (user.user_id().length() < 3) {
            result.add_error("user_id", "must be at least 3 characters");
        }
        
        // Validate email
        if (!validate_email(user.email())) {
            result.add_error("email", "invalid email format");
        }
        
        // Validate age
        if (user.age() < 0 || user.age() > 150) {
            result.add_error("age", "must be between 0 and 150");
        }
        
        // Validate phone (basic check)
        if (!user.phone().empty() && !validate_phone(user.phone())) {
            result.add_error("phone", "invalid phone format");
        }
        
        // Validate tags
        if (user.tags_size() > 10) {
            result.add_error("tags", "cannot have more than 10 tags");
        }
        
        return result;
    }

private:
    static bool validate_email(const std::string& email) {
        static const std::regex email_pattern(
            R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)"
        );
        return std::regex_match(email, email_pattern);
    }
    
    static bool validate_phone(const std::string& phone) {
        static const std::regex phone_pattern(R"(^\+?[1-9]\d{1,14}$)");
        return std::regex_match(phone, phone_pattern);
    }
};

class DateRangeValidator {
public:
    static ValidationResult validate(const DateRange& range) {
        ValidationResult result;
        
        if (range.start_timestamp() < 0) {
            result.add_error("start_timestamp", "must be non-negative");
        }
        
        if (range.end_timestamp() < 0) {
            result.add_error("end_timestamp", "must be non-negative");
        }
        
        if (range.end_timestamp() <= range.start_timestamp()) {
            result.add_error("end_timestamp", 
                           "must be greater than start_timestamp");
        }
        
        return result;
    }
};

} // namespace example

#endif // USER_VALIDATOR_H
```

### C++ Usage Example

```cpp
// main.cpp
#include "user.pb.h"
#include "user_validator.h"
#include <iostream>

using namespace example;

int main() {
    // Create a user
    User user;
    user.set_user_id("jd");  // Too short
    user.set_email("invalid-email");  // Invalid format
    user.set_age(200);  // Out of range
    user.set_phone("+1234567890");
    
    // Validate
    ValidationResult result = UserValidator::validate(user);
    
    if (!result.is_valid()) {
        std::cout << "Validation failed:\n";
        std::cout << result.error_summary();
        return 1;
    }
    
    std::cout << "User is valid!\n";
    
    // Example with DateRange
    DateRange range;
    range.set_start_timestamp(1000);
    range.set_end_timestamp(500);  // Invalid: less than start
    
    ValidationResult range_result = DateRangeValidator::validate(range);
    if (!range_result.is_valid()) {
        std::cout << "\nDateRange validation failed:\n";
        std::cout << range_result.error_summary();
    }
    
    return 0;
}
```

### C++ Generic Validator Pattern

```cpp
// generic_validator.h
#include <functional>
#include <memory>

template<typename T>
class Validator {
public:
    using ValidationFunc = std::function<ValidationResult(const T&)>;
    
    Validator& add_rule(const std::string& name, ValidationFunc func) {
        rules_.emplace_back(name, func);
        return *this;
    }
    
    ValidationResult validate(const T& message) const {
        ValidationResult result;
        for (const auto& [name, func] : rules_) {
            ValidationResult rule_result = func(message);
            if (!rule_result.is_valid()) {
                for (const auto& error : rule_result.errors()) {
                    result.add_error(error.field(), error.message());
                }
            }
        }
        return result;
    }

private:
    std::vector<std::pair<std::string, ValidationFunc>> rules_;
};

// Usage
Validator<User> create_user_validator() {
    Validator<User> validator;
    
    validator.add_rule("user_id_required", [](const User& u) {
        ValidationResult r;
        if (u.user_id().empty()) {
            r.add_error("user_id", "is required");
        }
        return r;
    });
    
    validator.add_rule("age_range", [](const User& u) {
        ValidationResult r;
        if (u.age() < 0 || u.age() > 150) {
            r.add_error("age", "must be 0-150");
        }
        return r;
    });
    
    return validator;
}
```

## Rust Implementation

### Rust Custom Validation

```rust
// validation.rs
use std::fmt;
use regex::Regex;

#[derive(Debug, Clone)]
pub struct ValidationError {
    field: String,
    message: String,
}

impl ValidationError {
    pub fn new(field: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            field: field.into(),
            message: message.into(),
        }
    }
}

impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Field '{}': {}", self.field, self.message)
    }
}

#[derive(Debug, Default)]
pub struct ValidationResult {
    errors: Vec<ValidationError>,
}

impl ValidationResult {
    pub fn new() -> Self {
        Self::default()
    }
    
    pub fn add_error(&mut self, field: impl Into<String>, message: impl Into<String>) {
        self.errors.push(ValidationError::new(field, message));
    }
    
    pub fn is_valid(&self) -> bool {
        self.errors.is_empty()
    }
    
    pub fn errors(&self) -> &[ValidationError] {
        &self.errors
    }
    
    pub fn merge(&mut self, other: ValidationResult) {
        self.errors.extend(other.errors);
    }
}

impl fmt::Display for ValidationResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for error in &self.errors {
            writeln!(f, "{}", error)?;
        }
        Ok(())
    }
}

pub trait Validate {
    fn validate(&self) -> ValidationResult;
}
```

### Rust Proto Implementation

```rust
// Assuming generated code from user.proto
// user.rs (generated)

pub mod example {
    #[derive(Clone, PartialEq, ::prost::Message)]
    pub struct User {
        #[prost(string, tag = "1")]
        pub user_id: ::prost::alloc::string::String,
        #[prost(string, tag = "2")]
        pub email: ::prost::alloc::string::String,
        #[prost(int32, tag = "3")]
        pub age: i32,
        #[prost(string, tag = "4")]
        pub phone: ::prost::alloc::string::String,
        #[prost(string, repeated, tag = "5")]
        pub tags: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    }
    
    #[derive(Clone, PartialEq, ::prost::Message)]
    pub struct DateRange {
        #[prost(int64, tag = "1")]
        pub start_timestamp: i64,
        #[prost(int64, tag = "2")]
        pub end_timestamp: i64,
    }
}
```

### Rust Validation Implementation

```rust
// user_validator.rs
use crate::validation::{ValidationResult, Validate};
use crate::example::{User, DateRange};
use regex::Regex;
use lazy_static::lazy_static;

lazy_static! {
    static ref EMAIL_REGEX: Regex = Regex::new(
        r"^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$"
    ).unwrap();
    
    static ref PHONE_REGEX: Regex = Regex::new(
        r"^\+?[1-9]\d{1,14}$"
    ).unwrap();
}

impl Validate for User {
    fn validate(&self) -> ValidationResult {
        let mut result = ValidationResult::new();
        
        // Validate user_id
        if self.user_id.is_empty() {
            result.add_error("user_id", "must not be empty");
        } else if self.user_id.len() < 3 {
            result.add_error("user_id", "must be at least 3 characters");
        }
        
        // Validate email
        if !EMAIL_REGEX.is_match(&self.email) {
            result.add_error("email", "invalid email format");
        }
        
        // Validate age
        if self.age < 0 || self.age > 150 {
            result.add_error("age", "must be between 0 and 150");
        }
        
        // Validate phone
        if !self.phone.is_empty() && !PHONE_REGEX.is_match(&self.phone) {
            result.add_error("phone", "invalid phone format");
        }
        
        // Validate tags
        if self.tags.len() > 10 {
            result.add_error("tags", "cannot have more than 10 tags");
        }
        
        // Check for duplicate tags
        let mut unique_tags = std::collections::HashSet::new();
        for tag in &self.tags {
            if !unique_tags.insert(tag) {
                result.add_error("tags", format!("duplicate tag: {}", tag));
            }
        }
        
        result
    }
}

impl Validate for DateRange {
    fn validate(&self) -> ValidationResult {
        let mut result = ValidationResult::new();
        
        if self.start_timestamp < 0 {
            result.add_error("start_timestamp", "must be non-negative");
        }
        
        if self.end_timestamp < 0 {
            result.add_error("end_timestamp", "must be non-negative");
        }
        
        if self.end_timestamp <= self.start_timestamp {
            result.add_error(
                "end_timestamp",
                "must be greater than start_timestamp"
            );
        }
        
        result
    }
}

// Custom validator functions
pub fn validate_user_with_context(user: &User, context: &str) -> ValidationResult {
    let mut result = user.validate();
    
    // Add context-specific validation
    if context == "registration" && user.email.is_empty() {
        result.add_error("email", "email is required for registration");
    }
    
    result
}
```

### Rust Usage Example

```rust
// main.rs
mod validation;
mod example;
mod user_validator;

use example::{User, DateRange};
use validation::Validate;
use user_validator::validate_user_with_context;

fn main() {
    // Create an invalid user
    let user = User {
        user_id: "jd".to_string(),  // Too short
        email: "invalid-email".to_string(),  // Invalid format
        age: 200,  // Out of range
        phone: "+1234567890".to_string(),
        tags: vec!["rust".to_string(), "protobuf".to_string()],
    };
    
    // Validate
    let result = user.validate();
    
    if !result.is_valid() {
        println!("Validation failed:");
        println!("{}", result);
    } else {
        println!("User is valid!");
    }
    
    // Valid user example
    let valid_user = User {
        user_id: "john_doe".to_string(),
        email: "john@example.com".to_string(),
        age: 30,
        phone: "+1234567890".to_string(),
        tags: vec!["developer".to_string()],
    };
    
    let valid_result = valid_user.validate();
    println!("\nValid user check: {}", valid_result.is_valid());
    
    // DateRange example
    let range = DateRange {
        start_timestamp: 1000,
        end_timestamp: 500,  // Invalid
    };
    
    let range_result = range.validate();
    if !range_result.is_valid() {
        println!("\nDateRange validation failed:");
        println!("{}", range_result);
    }
    
    // Context-specific validation
    let reg_result = validate_user_with_context(&user, "registration");
    println!("\nRegistration validation: {}", !reg_result.is_valid());
}
```

### Rust Builder Pattern with Validation

```rust
// validated_builder.rs
use crate::example::User;
use crate::validation::{ValidationResult, Validate};

pub struct UserBuilder {
    user: User,
}

impl UserBuilder {
    pub fn new() -> Self {
        Self {
            user: User::default(),
        }
    }
    
    pub fn user_id(mut self, user_id: impl Into<String>) -> Self {
        self.user.user_id = user_id.into();
        self
    }
    
    pub fn email(mut self, email: impl Into<String>) -> Self {
        self.user.email = email.into();
        self
    }
    
    pub fn age(mut self, age: i32) -> Self {
        self.user.age = age;
        self
    }
    
    pub fn phone(mut self, phone: impl Into<String>) -> Self {
        self.user.phone = phone.into();
        self
    }
    
    pub fn add_tag(mut self, tag: impl Into<String>) -> Self {
        self.user.tags.push(tag.into());
        self
    }
    
    pub fn build(self) -> Result<User, ValidationResult> {
        let result = self.user.validate();
        if result.is_valid() {
            Ok(self.user)
        } else {
            Err(result)
        }
    }
}

// Usage
fn example_builder() {
    match UserBuilder::new()
        .user_id("john_doe")
        .email("john@example.com")
        .age(30)
        .build()
    {
        Ok(user) => println!("Created valid user: {:?}", user.user_id),
        Err(errors) => println!("Validation failed:\n{}", errors),
    }
}
```

## Summary

**Custom Validation Rules** in Protocol Buffers extend the basic type safety with application-specific business logic validation. Key points:

- **Purpose**: Enforce constraints beyond protobuf's type system (ranges, formats, cross-field rules, business logic)
- **Implementation Approaches**: Post-generation validators, trait/interface-based validation, builder patterns, or third-party tools (protoc-gen-validate)
- **C++ Pattern**: Validation classes with static methods returning result objects containing error lists
- **Rust Pattern**: Trait-based validation (`Validate` trait) with idiomatic error handling and builder patterns
- **Benefits**: Centralized validation logic, early error detection, improved data quality, self-documenting constraints
- **Best Practices**: Collect all errors (don't fail-fast), provide clear error messages, validate at system boundaries, compose validators for complex rules
- **Common Validations**: Range checks, regex patterns, required fields, collection sizes, cross-field dependencies, business rules

Custom validation transforms protobuf from a serialization tool into a complete data integrity system for distributed applications.