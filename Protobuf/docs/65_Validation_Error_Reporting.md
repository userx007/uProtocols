# Validation Error Reporting in Protocol Buffers

## Overview

Validation error reporting in Protocol Buffers provides structured mechanisms for communicating validation failures when deserializing or processing protobuf messages. Rather than silent failures or generic error messages, this feature enables detailed reporting of what went wrong, where it went wrong (field path), and why validation failed.

## Core Concepts

**Structured Error Messages**: Instead of simple string errors, validation frameworks provide structured error objects containing:
- Field path (e.g., `user.address.postal_code`)
- Error type/code
- Human-readable description
- Context information (expected vs actual values)

**Field Path Reporting**: Hierarchical representation of where in the message structure the validation failed, crucial for nested messages and repeated fields.

**Error Aggregation**: Ability to collect multiple validation errors in a single pass rather than failing on the first error.

## C/C++ Implementation

In C++, validation error reporting typically uses custom validation frameworks since Protocol Buffers doesn't have built-in validation. Here's an example using a custom validation approach:

```cpp
#include <google/protobuf/message.h>
#include <vector>
#include <string>
#include <sstream>

// Validation error structure
struct ValidationError {
    std::string field_path;
    std::string error_code;
    std::string message;
    
    std::string ToString() const {
        std::ostringstream oss;
        oss << "[" << error_code << "] " << field_path << ": " << message;
        return oss.str();
    }
};

// Validation result container
class ValidationResult {
private:
    std::vector<ValidationError> errors_;
    
public:
    void AddError(const std::string& field_path, 
                  const std::string& error_code,
                  const std::string& message) {
        errors_.push_back({field_path, error_code, message});
    }
    
    bool IsValid() const { return errors_.empty(); }
    
    const std::vector<ValidationError>& GetErrors() const { return errors_; }
    
    std::string GetErrorSummary() const {
        std::ostringstream oss;
        oss << "Found " << errors_.size() << " validation error(s):\n";
        for (const auto& error : errors_) {
            oss << "  - " << error.ToString() << "\n";
        }
        return oss.str();
    }
};

// Example message validator with field path tracking
class MessageValidator {
private:
    std::string current_path_;
    
    void PushPath(const std::string& field) {
        if (!current_path_.empty()) {
            current_path_ += ".";
        }
        current_path_ += field;
    }
    
    void PopPath() {
        size_t pos = current_path_.find_last_of('.');
        if (pos != std::string::npos) {
            current_path_ = current_path_.substr(0, pos);
        } else {
            current_path_.clear();
        }
    }
    
public:
    ValidationResult Validate(const google::protobuf::Message& message) {
        ValidationResult result;
        current_path_.clear();
        ValidateMessage(message, result);
        return result;
    }
    
private:
    void ValidateMessage(const google::protobuf::Message& message, 
                        ValidationResult& result) {
        const auto* descriptor = message.GetDescriptor();
        const auto* reflection = message.GetReflection();
        
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const auto* field = descriptor->field(i);
            PushPath(field->name());
            
            // Check required fields
            if (field->is_required() && !reflection->HasField(message, field)) {
                result.AddError(current_path_, "REQUIRED_FIELD_MISSING",
                              "Required field is not set");
            }
            
            // Validate string fields
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                if (reflection->HasField(message, field)) {
                    std::string value = reflection->GetString(message, field);
                    if (value.empty()) {
                        result.AddError(current_path_, "EMPTY_STRING",
                                      "String field cannot be empty");
                    }
                    if (value.length() > 255) {
                        result.AddError(current_path_, "STRING_TOO_LONG",
                                      "String exceeds maximum length of 255");
                    }
                }
            }
            
            // Validate numeric fields
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_INT32) {
                if (reflection->HasField(message, field)) {
                    int32_t value = reflection->GetInt32(message, field);
                    if (value < 0) {
                        result.AddError(current_path_, "NEGATIVE_VALUE",
                                      "Value must be non-negative");
                    }
                }
            }
            
            // Recursively validate nested messages
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                if (field->is_repeated()) {
                    int count = reflection->FieldSize(message, field);
                    for (int j = 0; j < count; ++j) {
                        const auto& nested = reflection->GetRepeatedMessage(message, field, j);
                        std::string index_path = current_path_ + "[" + std::to_string(j) + "]";
                        PopPath();
                        PushPath(index_path);
                        ValidateMessage(nested, result);
                        PopPath();
                        PushPath(field->name());
                    }
                } else if (reflection->HasField(message, field)) {
                    const auto& nested = reflection->GetMessage(message, field);
                    ValidateMessage(nested, result);
                }
            }
            
            PopPath();
        }
    }
};

// Usage example
void ExampleUsage() {
    // Assuming we have a User message defined
    // message User {
    //   required string name = 1;
    //   required string email = 2;
    //   optional Address address = 3;
    // }
    
    MessageValidator validator;
    // User user; // populated message
    // ValidationResult result = validator.Validate(user);
    
    // if (!result.IsValid()) {
    //     std::cout << result.GetErrorSummary();
    //     for (const auto& error : result.GetErrors()) {
    //         std::cout << "Error at " << error.field_path << ": " 
    //                   << error.message << std::endl;
    //     }
    // }
}
```

## Rust Implementation

Rust's type system and error handling make validation error reporting more ergonomic. Here's an implementation using the `prost` library:

```rust
use std::fmt;
use prost::Message;

// Validation error types
#[derive(Debug, Clone)]
pub enum ValidationErrorCode {
    RequiredFieldMissing,
    InvalidFormat,
    OutOfRange,
    EmptyString,
    StringTooLong,
    InvalidEmail,
}

impl fmt::Display for ValidationErrorCode {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ValidationErrorCode::RequiredFieldMissing => write!(f, "REQUIRED_FIELD_MISSING"),
            ValidationErrorCode::InvalidFormat => write!(f, "INVALID_FORMAT"),
            ValidationErrorCode::OutOfRange => write!(f, "OUT_OF_RANGE"),
            ValidationErrorCode::EmptyString => write!(f, "EMPTY_STRING"),
            ValidationErrorCode::StringTooLong => write!(f, "STRING_TOO_LONG"),
            ValidationErrorCode::InvalidEmail => write!(f, "INVALID_EMAIL"),
        }
    }
}

// Single validation error
#[derive(Debug, Clone)]
pub struct ValidationError {
    pub field_path: String,
    pub error_code: ValidationErrorCode,
    pub message: String,
}

impl ValidationError {
    pub fn new(field_path: impl Into<String>, 
               error_code: ValidationErrorCode, 
               message: impl Into<String>) -> Self {
        Self {
            field_path: field_path.into(),
            error_code,
            message: message.into(),
        }
    }
}

impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "[{}] {}: {}", self.error_code, self.field_path, self.message)
    }
}

// Validation result with error collection
#[derive(Debug, Default)]
pub struct ValidationResult {
    errors: Vec<ValidationError>,
}

impl ValidationResult {
    pub fn new() -> Self {
        Self { errors: Vec::new() }
    }
    
    pub fn add_error(&mut self, error: ValidationError) {
        self.errors.push(error);
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
    
    pub fn error_summary(&self) -> String {
        if self.is_valid() {
            return "No validation errors".to_string();
        }
        
        let mut summary = format!("Found {} validation error(s):\n", self.errors.len());
        for error in &self.errors {
            summary.push_str(&format!("  - {}\n", error));
        }
        summary
    }
}

// Field path builder for nested validation
pub struct FieldPath {
    segments: Vec<String>,
}

impl FieldPath {
    pub fn new() -> Self {
        Self { segments: Vec::new() }
    }
    
    pub fn push(&mut self, field: impl Into<String>) {
        self.segments.push(field.into());
    }
    
    pub fn pop(&mut self) {
        self.segments.pop();
    }
    
    pub fn with_field<F, R>(&mut self, field: impl Into<String>, f: F) -> R
    where
        F: FnOnce(&mut Self) -> R,
    {
        self.push(field);
        let result = f(self);
        self.pop();
        result
    }
    
    pub fn with_index<F, R>(&mut self, index: usize, f: F) -> R
    where
        F: FnOnce(&mut Self) -> R,
    {
        self.push(format!("[{}]", index));
        let result = f(self);
        self.pop();
        result
    }
    
    pub fn to_string(&self) -> String {
        self.segments.join(".")
    }
}

// Validator trait
pub trait Validate {
    fn validate(&self) -> ValidationResult {
        let mut result = ValidationResult::new();
        let mut path = FieldPath::new();
        self.validate_with_path(&mut path, &mut result);
        result
    }
    
    fn validate_with_path(&self, path: &mut FieldPath, result: &mut ValidationResult);
}

// Example: User message validator
// Assuming prost-generated message:
// #[derive(Clone, PartialEq, prost::Message)]
// pub struct User {
//     #[prost(string, tag = "1")]
//     pub name: String,
//     #[prost(string, tag = "2")]
//     pub email: String,
//     #[prost(int32, tag = "3")]
//     pub age: i32,
//     #[prost(message, optional, tag = "4")]
//     pub address: Option<Address>,
// }

// Example implementation
pub struct User {
    pub name: String,
    pub email: String,
    pub age: i32,
    pub address: Option<Address>,
}

pub struct Address {
    pub street: String,
    pub city: String,
    pub postal_code: String,
}

impl Validate for User {
    fn validate_with_path(&self, path: &mut FieldPath, result: &mut ValidationResult) {
        // Validate name
        path.with_field("name", |path| {
            if self.name.is_empty() {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::EmptyString,
                    "Name cannot be empty"
                ));
            }
            if self.name.len() > 100 {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::StringTooLong,
                    format!("Name too long: {} characters (max 100)", self.name.len())
                ));
            }
        });
        
        // Validate email
        path.with_field("email", |path| {
            if self.email.is_empty() {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::EmptyString,
                    "Email cannot be empty"
                ));
            } else if !self.email.contains('@') {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::InvalidEmail,
                    "Email must contain @ symbol"
                ));
            }
        });
        
        // Validate age
        path.with_field("age", |path| {
            if self.age < 0 {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::OutOfRange,
                    format!("Age cannot be negative: {}", self.age)
                ));
            }
            if self.age > 150 {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::OutOfRange,
                    format!("Age seems unrealistic: {}", self.age)
                ));
            }
        });
        
        // Validate nested address
        if let Some(ref address) = self.address {
            path.with_field("address", |path| {
                address.validate_with_path(path, result);
            });
        }
    }
}

impl Validate for Address {
    fn validate_with_path(&self, path: &mut FieldPath, result: &mut ValidationResult) {
        path.with_field("street", |path| {
            if self.street.is_empty() {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::EmptyString,
                    "Street cannot be empty"
                ));
            }
        });
        
        path.with_field("city", |path| {
            if self.city.is_empty() {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::EmptyString,
                    "City cannot be empty"
                ));
            }
        });
        
        path.with_field("postal_code", |path| {
            if self.postal_code.is_empty() {
                result.add_error(ValidationError::new(
                    path.to_string(),
                    ValidationErrorCode::RequiredFieldMissing,
                    "Postal code is required"
                ));
            }
        });
    }
}

// Usage example
fn example_usage() {
    let user = User {
        name: "".to_string(),
        email: "invalid-email".to_string(),
        age: -5,
        address: Some(Address {
            street: "123 Main St".to_string(),
            city: "".to_string(),
            postal_code: "".to_string(),
        }),
    };
    
    let result = user.validate();
    
    if !result.is_valid() {
        println!("{}", result.error_summary());
        
        for error in result.errors() {
            println!("  Field: {}", error.field_path);
            println!("  Code: {}", error.error_code);
            println!("  Message: {}", error.message);
            println!();
        }
    }
}

// For validating repeated fields
pub fn validate_repeated<T: Validate>(
    items: &[T],
    path: &mut FieldPath,
    result: &mut ValidationResult
) {
    for (index, item) in items.iter().enumerate() {
        path.with_index(index, |path| {
            item.validate_with_path(path, result);
        });
    }
}
```

## Summary

Validation error reporting in Protocol Buffers enables robust error handling through:

- **Structured errors** with field paths, error codes, and descriptive messages
- **Field path tracking** for precise error location identification in nested structures
- **Error aggregation** to collect all validation issues in one pass
- **Type-safe implementations** especially in Rust's type system
- **Extensibility** for custom validation rules and business logic

This approach transforms opaque validation failures into actionable feedback, essential for API development, data integrity, and user-facing error messages. Both C++ and Rust implementations demonstrate how to build validation frameworks that provide detailed context about what failed and where, though Rust's native error handling and type system offer more ergonomic patterns for this task.