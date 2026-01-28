# Cross-Field Validation in Protocol Buffers

## Overview

Cross-field validation involves checking the relationships and dependencies between multiple fields in a Protocol Buffer message, rather than validating each field in isolation. This is essential for enforcing complex business rules where the validity of one field depends on the values of other fields.

## Key Concepts

**Independent vs. Cross-Field Validation:**
- Independent validation checks individual field constraints (e.g., string length, numeric ranges)
- Cross-field validation ensures logical consistency across multiple fields (e.g., start_date must be before end_date)

**Common Use Cases:**
- Date/time range validation
- Mutually exclusive fields (oneof alternatives)
- Conditional requirements (field A required when field B has certain value)
- Sum/total validations
- Geographic/hierarchical consistency
- State machine transitions

## Implementation Strategies

### 1. **Custom Validation Functions**
Write dedicated validation logic that examines the complete message after deserialization.

### 2. **Proto Validation Extensions**
Use validation libraries like `protoc-gen-validate` (PGV) which supports cross-field rules.

### 3. **Business Logic Layer**
Implement validation in application code where you have full control over business rules.

## C/C++ Implementation

```cpp
#include <iostream>
#include <string>
#include "google/protobuf/message.h"
#include "google/protobuf/timestamp.pb.h"

// Example proto definition (conceptual)
// message Reservation {
//   google.protobuf.Timestamp start_time = 1;
//   google.protobuf.Timestamp end_time = 2;
//   int32 num_guests = 3;
//   int32 max_capacity = 4;
//   string room_type = 5;
//   bool is_premium = 6;
// }

class ReservationValidator {
public:
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> errors;
        
        ValidationResult() : is_valid(true) {}
        
        void AddError(const std::string& error) {
            is_valid = false;
            errors.push_back(error);
        }
    };
    
    // Cross-field validation for reservation
    static ValidationResult ValidateReservation(const Reservation& reservation) {
        ValidationResult result;
        
        // Rule 1: start_time must be before end_time
        if (reservation.has_start_time() && reservation.has_end_time()) {
            int64_t start_seconds = reservation.start_time().seconds();
            int64_t end_seconds = reservation.end_time().seconds();
            
            if (start_seconds >= end_seconds) {
                result.AddError("start_time must be before end_time");
            }
            
            // Rule 2: Reservation duration must be at least 1 hour
            int64_t duration_seconds = end_seconds - start_seconds;
            if (duration_seconds < 3600) {
                result.AddError("Reservation must be at least 1 hour long");
            }
        }
        
        // Rule 3: num_guests cannot exceed max_capacity
        if (reservation.num_guests() > reservation.max_capacity()) {
            result.AddError("Number of guests (" + 
                          std::to_string(reservation.num_guests()) + 
                          ") exceeds capacity (" + 
                          std::to_string(reservation.max_capacity()) + ")");
        }
        
        // Rule 4: Premium rooms require at least 2 guests
        if (reservation.is_premium() && reservation.num_guests() < 2) {
            result.AddError("Premium reservations require at least 2 guests");
        }
        
        // Rule 5: Suite rooms have minimum capacity
        if (reservation.room_type() == "suite" && 
            reservation.max_capacity() < 4) {
            result.AddError("Suite rooms must have capacity of at least 4");
        }
        
        return result;
    }
};

// Example usage with error handling
void ProcessReservation(const Reservation& reservation) {
    auto validation = ReservationValidator::ValidateReservation(reservation);
    
    if (!validation.is_valid) {
        std::cerr << "Validation failed with " << validation.errors.size() 
                  << " error(s):\n";
        for (const auto& error : validation.errors) {
            std::cerr << "  - " << error << "\n";
        }
        return;
    }
    
    std::cout << "Reservation is valid, processing...\n";
    // Process the reservation
}

// Advanced: Template-based validator for reusability
template<typename MessageType>
class MessageValidator {
public:
    using ValidatorFunc = std::function<ValidationResult(const MessageType&)>;
    
    void AddRule(const std::string& rule_name, ValidatorFunc validator) {
        rules_[rule_name] = validator;
    }
    
    ValidationResult Validate(const MessageType& message) {
        ValidationResult combined_result;
        
        for (const auto& [name, validator] : rules_) {
            auto result = validator(message);
            if (!result.is_valid) {
                combined_result.is_valid = false;
                for (const auto& error : result.errors) {
                    combined_result.errors.push_back(name + ": " + error);
                }
            }
        }
        
        return combined_result;
    }
    
private:
    std::map<std::string, ValidatorFunc> rules_;
};
```

### C++ with protoc-gen-validate Integration

```cpp
// Using PGV (Protocol Buffers Validation) - proto file
/*
syntax = "proto3";

import "validate/validate.proto";

message PaymentRequest {
  double amount = 1 [(validate.rules).double = {gt: 0}];
  double tax = 2 [(validate.rules).double = {gte: 0}];
  double total = 3;
  
  // Cross-field validation
  option (validate.message).cel = {
    id: "total_equals_sum"
    message: "total must equal amount + tax"
    expression: "this.total == this.amount + this.tax"
  };
  
  option (validate.message).cel = {
    id: "tax_reasonable"
    message: "tax cannot exceed 50% of amount"
    expression: "this.tax <= this.amount * 0.5"
  };
}
*/

#include "payment.pb.h"
#include "payment.pb.validate.h"

bool ValidatePayment(const PaymentRequest& request, std::string* error) {
    // PGV generates Validate() method
    return request.Validate(error);
}
```

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

// Assume these structs are generated from protobuf
#[derive(Debug, Clone)]
pub struct Timestamp {
    pub seconds: i64,
    pub nanos: i32,
}

#[derive(Debug, Clone)]
pub struct Reservation {
    pub start_time: Option<Timestamp>,
    pub end_time: Option<Timestamp>,
    pub num_guests: i32,
    pub max_capacity: i32,
    pub room_type: String,
    pub is_premium: bool,
}

// Custom error type for validation
#[derive(Debug, Clone)]
pub struct ValidationError {
    pub errors: Vec<String>,
}

impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Validation failed: {}", self.errors.join(", "))
    }
}

impl Error for ValidationError {}

// Trait-based validation approach
pub trait Validate {
    fn validate(&self) -> Result<(), ValidationError>;
}

impl Validate for Reservation {
    fn validate(&self) -> Result<(), ValidationError> {
        let mut errors = Vec::new();
        
        // Rule 1: Time range validation
        if let (Some(start), Some(end)) = (&self.start_time, &self.end_time) {
            if start.seconds >= end.seconds {
                errors.push("start_time must be before end_time".to_string());
            }
            
            // Rule 2: Minimum duration
            let duration = end.seconds - start.seconds;
            if duration < 3600 {
                errors.push("Reservation must be at least 1 hour long".to_string());
            }
        }
        
        // Rule 3: Capacity validation
        if self.num_guests > self.max_capacity {
            errors.push(format!(
                "Number of guests ({}) exceeds capacity ({})",
                self.num_guests, self.max_capacity
            ));
        }
        
        // Rule 4: Premium guest requirement
        if self.is_premium && self.num_guests < 2 {
            errors.push("Premium reservations require at least 2 guests".to_string());
        }
        
        // Rule 5: Suite capacity requirement
        if self.room_type == "suite" && self.max_capacity < 4 {
            errors.push("Suite rooms must have capacity of at least 4".to_string());
        }
        
        if errors.is_empty() {
            Ok(())
        } else {
            Err(ValidationError { errors })
        }
    }
}

// Builder pattern with validation
pub struct ReservationBuilder {
    reservation: Reservation,
}

impl ReservationBuilder {
    pub fn new() -> Self {
        Self {
            reservation: Reservation {
                start_time: None,
                end_time: None,
                num_guests: 0,
                max_capacity: 0,
                room_type: String::new(),
                is_premium: false,
            },
        }
    }
    
    pub fn start_time(mut self, time: Timestamp) -> Self {
        self.reservation.start_time = Some(time);
        self
    }
    
    pub fn end_time(mut self, time: Timestamp) -> Self {
        self.reservation.end_time = Some(time);
        self
    }
    
    pub fn num_guests(mut self, count: i32) -> Self {
        self.reservation.num_guests = count;
        self
    }
    
    pub fn max_capacity(mut self, capacity: i32) -> Self {
        self.reservation.max_capacity = capacity;
        self
    }
    
    pub fn room_type(mut self, room_type: String) -> Self {
        self.reservation.room_type = room_type;
        self
    }
    
    pub fn is_premium(mut self, premium: bool) -> Self {
        self.reservation.is_premium = premium;
        self
    }
    
    // Build with automatic validation
    pub fn build(self) -> Result<Reservation, ValidationError> {
        self.reservation.validate()?;
        Ok(self.reservation)
    }
}

// Composable validators using function composition
pub struct ValidationRule<T> {
    name: String,
    validator: Box<dyn Fn(&T) -> Option<String>>,
}

impl<T> ValidationRule<T> {
    pub fn new<F>(name: &str, validator: F) -> Self 
    where
        F: Fn(&T) -> Option<String> + 'static,
    {
        Self {
            name: name.to_string(),
            validator: Box::new(validator),
        }
    }
    
    pub fn validate(&self, value: &T) -> Option<String> {
        (self.validator)(value)
    }
}

pub struct Validator<T> {
    rules: Vec<ValidationRule<T>>,
}

impl<T> Validator<T> {
    pub fn new() -> Self {
        Self { rules: Vec::new() }
    }
    
    pub fn add_rule<F>(mut self, name: &str, validator: F) -> Self 
    where
        F: Fn(&T) -> Option<String> + 'static,
    {
        self.rules.push(ValidationRule::new(name, validator));
        self
    }
    
    pub fn validate(&self, value: &T) -> Result<(), ValidationError> {
        let errors: Vec<String> = self.rules
            .iter()
            .filter_map(|rule| rule.validate(value))
            .collect();
        
        if errors.is_empty() {
            Ok(())
        } else {
            Err(ValidationError { errors })
        }
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    // Create validator with multiple rules
    let validator = Validator::new()
        .add_rule("time_order", |r: &Reservation| {
            if let (Some(start), Some(end)) = (&r.start_time, &r.end_time) {
                if start.seconds >= end.seconds {
                    return Some("start_time must be before end_time".to_string());
                }
            }
            None
        })
        .add_rule("capacity_check", |r: &Reservation| {
            if r.num_guests > r.max_capacity {
                return Some(format!(
                    "Guests ({}) exceed capacity ({})",
                    r.num_guests, r.max_capacity
                ));
            }
            None
        });
    
    // Using builder with validation
    let reservation = ReservationBuilder::new()
        .start_time(Timestamp { seconds: 1000, nanos: 0 })
        .end_time(Timestamp { seconds: 5000, nanos: 0 })
        .num_guests(3)
        .max_capacity(5)
        .room_type("standard".to_string())
        .is_premium(false)
        .build()?;
    
    println!("Valid reservation created: {:?}", reservation);
    
    Ok(())
}
```

## Advanced Patterns

### State Machine Validation

```rust
#[derive(Debug, Clone, PartialEq)]
pub enum OrderStatus {
    Pending,
    Confirmed,
    Shipped,
    Delivered,
    Cancelled,
}

pub struct OrderTransitionValidator;

impl OrderTransitionValidator {
    pub fn can_transition(from: &OrderStatus, to: &OrderStatus) -> bool {
        match (from, to) {
            (OrderStatus::Pending, OrderStatus::Confirmed) => true,
            (OrderStatus::Pending, OrderStatus::Cancelled) => true,
            (OrderStatus::Confirmed, OrderStatus::Shipped) => true,
            (OrderStatus::Confirmed, OrderStatus::Cancelled) => true,
            (OrderStatus::Shipped, OrderStatus::Delivered) => true,
            _ => false,
        }
    }
}
```

## Summary

**Cross-field validation** is essential for enforcing complex business rules in Protocol Buffer messages. Key takeaways:

- **Go beyond single-field constraints**: Validate relationships between multiple fields to ensure logical consistency
- **Multiple implementation approaches**: Custom functions, validation libraries (PGV), or business logic layer
- **Language-specific patterns**: C++ uses classes/templates, Rust leverages traits and the type system
- **Common validation scenarios**: Date ranges, capacity limits, conditional requirements, state transitions, and sum validations
- **Design considerations**: Separate validation from serialization logic, make validators reusable, provide clear error messages

Cross-field validation adds a critical layer of data integrity that complements Protocol Buffers' built-in type safety, ensuring messages are not just well-formed but also semantically correct according to your business rules.