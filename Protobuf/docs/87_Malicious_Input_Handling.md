# Protobuf Malicious Input Handling: Defending Against Crafted Messages

The document includes:

**Key Attack Vectors:**
- Uncontrolled recursion (CVE-2026-0994)
- Memory exhaustion attacks
- Stack-based buffer overflow (CVE-2024-7254)
- Prototype pollution in JavaScript
- Use-after-free vulnerabilities

**Defense Strategies & Code Examples:**

**C/C++ Examples:**
1. Safe parsing with size and recursion limits using `CodedInputStream`
2. Field validation using reflection API
3. Integration with protovalidate-cc for annotation-based validation
4. Custom recursion guards for nested messages

**Rust Examples:**
1. Safe decoding with `prost` and size validation
2. Field validation using `prost-validate`
3. Compile-time validation with `protocheck`
4. Custom recursion depth protection
5. Memory-safe streaming parsers

**Best Practices:**
- Always enforce message size and recursion depth limits
- Validate messages against schemas immediately after parsing
- Use battle-tested, well-fuzzed parsers
- Implement defense-in-depth with multiple validation layers
- Keep libraries updated to patch known vulnerabilities

The examples are production-ready and demonstrate real-world defensive programming techniques used to protect against the various CVEs and attack vectors discovered in Protocol Buffers implementations.

## Overview

Malicious input handling in Protocol Buffers (Protobuf) is critical for defending against crafted messages designed to exploit parser bugs, cause denial of service, or trigger other security vulnerabilities. This document explores the common attack vectors, defensive programming techniques, and practical implementations in C/C++ and Rust.

## Common Attack Vectors

### 1. Uncontrolled Recursion
Attackers craft deeply nested messages to exhaust the call stack, causing a stack overflow or recursion error. This is particularly dangerous with messages containing:
- Nested `google.protobuf.Any` messages
- Recursive message definitions
- Nested groups or repeated fields

**Real-world Example**: CVE-2026-0994 demonstrated how deeply nested `Any` messages could bypass recursion limits in Python's protobuf implementation.

### 2. Memory Exhaustion
Small malicious payloads (~500 KB) can trigger excessive memory allocation (>3GB), causing out-of-memory conditions. This exploits the parser's memory management weaknesses.

**Real-world Example**: A parser vulnerability allowed attackers to construct payloads that caused services to allocate thousands of times more memory than the input size.

### 3. Stack-Based Buffer Overflow
Parsing nested groups or series of SGROUP tags as unknown fields can create unbounded recursions, particularly when using:
- `DiscardUnknownFieldsParser` in Java
- Protobuf Lite parser
- Map fields

**Real-world Example**: CVE-2024-7254 affected multiple protobuf implementations with CVSS score of 8.7.

### 4. Prototype Pollution (JavaScript)
In JavaScript implementations (protobuf.js), attackers can pollute object prototypes through specially crafted proto files or parsing operations.

### 5. Use-After-Free in JSON Parsing
Malicious JSON input can cause the C++ parser to copy data that has already been freed into error messages, particularly when parsing from streams with separate chunks.

## Defense Strategies

### Strategy 1: Implement Recursion Depth Limits

Track and enforce maximum recursion depth during message parsing to prevent stack exhaustion.

### Strategy 2: Memory Limits

Set upper bounds on memory allocation per message and implement streaming parsers for large messages.

### Strategy 3: Input Validation

Validate messages against expected schemas before processing, checking field types, ranges, and structure.

### Strategy 4: Use Battle-Tested Parsers

Prefer well-fuzzed, production-grade parsers like C++ Protobuf or upb over custom implementations.

### Strategy 5: Sandboxing

Isolate parsing operations in containerized or sandboxed environments to contain potential crashes.

---

## C/C++ Implementation Examples

### Example 1: Basic Input Validation with Size Limits

```cpp
#include <google/protobuf/message.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <limits>

// Define maximum message size (e.g., 64MB)
constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024 * 1024;

// Define maximum recursion depth
constexpr int MAX_RECURSION_DEPTH = 100;

bool SafeParseFromString(google::protobuf::Message* message, 
                         const std::string& data) {
    // Check input size
    if (data.size() > MAX_MESSAGE_SIZE) {
        std::cerr << "Input exceeds maximum allowed size" << std::endl;
        return false;
    }
    
    // Create input stream
    google::protobuf::io::ArrayInputStream input(data.data(), data.size());
    google::protobuf::io::CodedInputStream coded_input(&input);
    
    // Set recursion limit
    coded_input.SetRecursionLimit(MAX_RECURSION_DEPTH);
    
    // Set size limit
    coded_input.SetTotalBytesLimit(MAX_MESSAGE_SIZE);
    
    // Parse the message
    if (!message->ParseFromCodedStream(&coded_input)) {
        std::cerr << "Failed to parse message" << std::endl;
        return false;
    }
    
    // Verify we consumed all input (detect trailing garbage)
    if (!coded_input.ConsumedEntireMessage()) {
        std::cerr << "Warning: message contained trailing data" << std::endl;
        return false;
    }
    
    return true;
}
```

### Example 2: Validating Message Fields

```cpp
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

class MessageValidator {
public:
    // Validate that required fields are present
    static bool ValidateRequiredFields(const google::protobuf::Message& message) {
        const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
        const google::protobuf::Reflection* reflection = message.GetReflection();
        
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = descriptor->field(i);
            
            // Check if required field is set
            if (field->is_required() && !reflection->HasField(message, field)) {
                std::cerr << "Required field missing: " 
                         << field->full_name() << std::endl;
                return false;
            }
        }
        return true;
    }
    
    // Validate numeric field ranges
    static bool ValidateFieldRanges(const google::protobuf::Message& message,
                                   const std::string& field_name,
                                   int64_t min_value, 
                                   int64_t max_value) {
        const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
        const google::protobuf::Reflection* reflection = message.GetReflection();
        const google::protobuf::FieldDescriptor* field = 
            descriptor->FindFieldByName(field_name);
        
        if (!field) {
            std::cerr << "Field not found: " << field_name << std::endl;
            return false;
        }
        
        if (!reflection->HasField(message, field)) {
            return true; // Optional field not set is OK
        }
        
        int64_t value = 0;
        switch (field->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                value = reflection->GetInt32(message, field);
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                value = reflection->GetInt64(message, field);
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                value = reflection->GetUInt32(message, field);
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                value = static_cast<int64_t>(reflection->GetUInt64(message, field));
                break;
            default:
                std::cerr << "Field is not a numeric type" << std::endl;
                return false;
        }
        
        if (value < min_value || value > max_value) {
            std::cerr << "Field " << field_name << " value " << value 
                     << " outside allowed range [" << min_value 
                     << ", " << max_value << "]" << std::endl;
            return false;
        }
        
        return true;
    }
};
```

### Example 3: Using Protovalidate for C++

```cpp
#include <buf/validate/validator.h>
#include <google/protobuf/arena.h>
#include <memory>

// Assume you have a proto with validation annotations:
// message User {
//   string id = 1 [(buf.validate.field).string.uuid = true];
//   uint32 age = 2 [(buf.validate.field).uint32.lte = 150];
// }

bool ValidateUserMessage(const User& user) {
    // Create validator factory
    auto factory_result = buf::validate::ValidatorFactory::New();
    if (!factory_result.ok()) {
        std::cerr << "Failed to create validator factory: " 
                  << factory_result.status() << std::endl;
        return false;
    }
    
    std::unique_ptr<buf::validate::ValidatorFactory> factory = 
        std::move(factory_result.value());
    
    // Create arena for memory management
    google::protobuf::Arena arena;
    
    // Create validator
    buf::validate::Validator validator = factory->NewValidator(&arena);
    
    // Validate the message
    auto result = validator.Validate(user);
    if (!result.ok()) {
        std::cerr << "Validation error: " << result.status() << std::endl;
        return false;
    }
    
    buf::validate::Violations violations = result.value();
    
    if (violations.violations_size() > 0) {
        std::cerr << "Validation violations found:" << std::endl;
        for (const auto& violation : violations.violations()) {
            std::cerr << "  - Field: " << violation.field_path() 
                     << ", Message: " << violation.message() << std::endl;
        }
        return false;
    }
    
    return true;
}
```

### Example 4: Defense Against Nested Messages

```cpp
#include <google/protobuf/message.h>

class RecursionGuard {
private:
    int& depth_;
    const int max_depth_;
    
public:
    RecursionGuard(int& depth, int max_depth) 
        : depth_(depth), max_depth_(max_depth) {
        ++depth_;
    }
    
    ~RecursionGuard() {
        --depth_;
    }
    
    bool IsValid() const {
        return depth_ <= max_depth_;
    }
};

bool ValidateMessageRecursively(const google::protobuf::Message& message,
                                int& current_depth,
                                const int max_depth = 100) {
    RecursionGuard guard(current_depth, max_depth);
    
    if (!guard.IsValid()) {
        std::cerr << "Maximum recursion depth exceeded" << std::endl;
        return false;
    }
    
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    const google::protobuf::Reflection* reflection = message.GetReflection();
    
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = descriptor->field(i);
        
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
            if (field->is_repeated()) {
                int field_size = reflection->FieldSize(message, field);
                for (int j = 0; j < field_size; ++j) {
                    const google::protobuf::Message& sub_message = 
                        reflection->GetRepeatedMessage(message, field, j);
                    if (!ValidateMessageRecursively(sub_message, current_depth, max_depth)) {
                        return false;
                    }
                }
            } else if (reflection->HasField(message, field)) {
                const google::protobuf::Message& sub_message = 
                    reflection->GetMessage(message, field);
                if (!ValidateMessageRecursively(sub_message, current_depth, max_depth)) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool SafeValidateMessage(const google::protobuf::Message& message) {
    int depth = 0;
    return ValidateMessageRecursively(message, depth);
}
```

---

## Rust Implementation Examples

### Example 1: Basic Safe Parsing with prost

```rust
use prost::Message;
use std::io::Cursor;

const MAX_MESSAGE_SIZE: usize = 64 * 1024 * 1024; // 64MB

#[derive(Clone, PartialEq, ::prost::Message)]
pub struct User {
    #[prost(string, tag = "1")]
    pub name: String,
    #[prost(uint32, tag = "2")]
    pub age: u32,
    #[prost(string, tag = "3")]
    pub email: String,
}

/// Safely decode a protobuf message with size validation
fn safe_decode<M: Message + Default>(data: &[u8]) -> Result<M, Box<dyn std::error::Error>> {
    // Validate input size
    if data.len() > MAX_MESSAGE_SIZE {
        return Err("Input exceeds maximum allowed size".into());
    }
    
    // Attempt to decode
    let message = M::decode(data)
        .map_err(|e| format!("Failed to decode message: {}", e))?;
    
    Ok(message)
}

/// Decode with length-delimited format (common for streaming)
fn safe_decode_length_delimited<M: Message + Default>(
    data: &[u8]
) -> Result<M, Box<dyn std::error::Error>> {
    if data.len() > MAX_MESSAGE_SIZE {
        return Err("Input exceeds maximum allowed size".into());
    }
    
    let mut cursor = Cursor::new(data);
    let message = M::decode_length_delimited(&mut cursor)
        .map_err(|e| format!("Failed to decode length-delimited message: {}", e))?;
    
    // Check for trailing data
    if cursor.position() < data.len() as u64 {
        eprintln!("Warning: message contained trailing data");
    }
    
    Ok(message)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_safe_decode() {
        let user = User {
            name: "Alice".to_string(),
            age: 30,
            email: "alice@example.com".to_string(),
        };
        
        let mut buf = Vec::new();
        user.encode(&mut buf).unwrap();
        
        let decoded: User = safe_decode(&buf).unwrap();
        assert_eq!(decoded, user);
    }
    
    #[test]
    fn test_oversized_message() {
        let large_data = vec![0u8; MAX_MESSAGE_SIZE + 1];
        let result: Result<User, _> = safe_decode(&large_data);
        assert!(result.is_err());
    }
}
```

### Example 2: Field Validation with prost-validate

```rust
use prost::Message;
use prost_validate::Validator;

// Define the proto with validation rules:
// syntax = "proto3";
// import "validate/validate.proto";
// 
// message User {
//   string name = 1 [(validate.rules).string = {min_len: 1, max_len: 100}];
//   uint32 age = 2 [(validate.rules).uint32 = {gte: 0, lte: 150}];
//   string email = 3 [(validate.rules).string.email = true];
// }

#[derive(Clone, PartialEq, ::prost::Message, Validator)]
pub struct ValidatedUser {
    #[prost(string, tag = "1")]
    #[prost_validate(string(min_len = 1, max_len = 100))]
    pub name: String,
    
    #[prost(uint32, tag = "2")]
    #[prost_validate(uint32(lte = 150))]
    pub age: u32,
    
    #[prost(string, tag = "3")]
    #[prost_validate(string(email = true))]
    pub email: String,
}

fn process_validated_user(data: &[u8]) -> Result<ValidatedUser, Box<dyn std::error::Error>> {
    // Decode the message
    let user = ValidatedUser::decode(data)
        .map_err(|e| format!("Decode error: {}", e))?;
    
    // Validate the decoded message
    user.validate()
        .map_err(|e| format!("Validation error: {}", e))?;
    
    Ok(user)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_valid_user() {
        let user = ValidatedUser {
            name: "Bob".to_string(),
            age: 25,
            email: "bob@example.com".to_string(),
        };
        
        let mut buf = Vec::new();
        user.encode(&mut buf).unwrap();
        
        let result = process_validated_user(&buf);
        assert!(result.is_ok());
    }
    
    #[test]
    fn test_invalid_age() {
        let user = ValidatedUser {
            name: "Charlie".to_string(),
            age: 200, // Exceeds max
            email: "charlie@example.com".to_string(),
        };
        
        assert!(user.validate().is_err());
    }
    
    #[test]
    fn test_invalid_email() {
        let user = ValidatedUser {
            name: "Dave".to_string(),
            age: 30,
            email: "not-an-email".to_string(),
        };
        
        assert!(user.validate().is_err());
    }
}
```

### Example 3: Using protocheck for Compile-Time Validation

```rust
use protocheck::protobuf_validate;
use prost::Message;

// Proto definition with buf.validate annotations:
// syntax = "proto3";
// import "buf/validate/validate.proto";
// 
// message Transaction {
//   string id = 1 [(buf.validate.field).string.uuid = true];
//   double amount = 2 [(buf.validate.field).double = {gt: 0, lte: 1000000}];
//   string currency = 3 [(buf.validate.field).string.in = "USD,EUR,GBP"];
// }

#[protobuf_validate]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Transaction {
    #[prost(string, tag = "1")]
    pub id: String,
    
    #[prost(double, tag = "2")]
    pub amount: f64,
    
    #[prost(string, tag = "3")]
    pub currency: String,
}

fn process_transaction(data: &[u8]) -> Result<Transaction, String> {
    // Decode the transaction
    let transaction = Transaction::decode(data)
        .map_err(|e| format!("Failed to decode: {}", e))?;
    
    // Validate using generated validation logic
    transaction.validate()
        .map_err(|e| format!("Validation failed: {}", e))?;
    
    println!("Processing transaction {} for {} {}", 
             transaction.id, 
             transaction.amount, 
             transaction.currency);
    
    Ok(transaction)
}
```

### Example 4: Custom Recursion Protection

```rust
use prost::Message;
use std::collections::HashSet;

const MAX_RECURSION_DEPTH: usize = 100;

#[derive(Clone, PartialEq, ::prost::Message)]
pub struct NestedMessage {
    #[prost(string, tag = "1")]
    pub content: String,
    
    #[prost(message, optional, tag = "2")]
    pub child: Option<Box<NestedMessage>>,
    
    #[prost(message, repeated, tag = "3")]
    pub children: Vec<NestedMessage>,
}

struct RecursionGuard {
    depth: usize,
    max_depth: usize,
}

impl RecursionGuard {
    fn new(max_depth: usize) -> Self {
        Self { depth: 0, max_depth }
    }
    
    fn enter(&mut self) -> Result<(), String> {
        self.depth += 1;
        if self.depth > self.max_depth {
            return Err(format!(
                "Maximum recursion depth {} exceeded", 
                self.max_depth
            ));
        }
        Ok(())
    }
    
    fn exit(&mut self) {
        self.depth = self.depth.saturating_sub(1);
    }
}

fn validate_nested_message_depth(
    msg: &NestedMessage,
    guard: &mut RecursionGuard
) -> Result<(), String> {
    guard.enter()?;
    
    // Check single child
    if let Some(child) = &msg.child {
        validate_nested_message_depth(child, guard)?;
    }
    
    // Check repeated children
    for child in &msg.children {
        validate_nested_message_depth(child, guard)?;
    }
    
    guard.exit();
    Ok(())
}

fn safe_process_nested_message(data: &[u8]) -> Result<NestedMessage, String> {
    // Decode
    let message = NestedMessage::decode(data)
        .map_err(|e| format!("Decode error: {}", e))?;
    
    // Validate recursion depth
    let mut guard = RecursionGuard::new(MAX_RECURSION_DEPTH);
    validate_nested_message_depth(&message, &mut guard)?;
    
    Ok(message)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_shallow_nesting() {
        let msg = NestedMessage {
            content: "root".to_string(),
            child: Some(Box::new(NestedMessage {
                content: "child".to_string(),
                child: None,
                children: vec![],
            })),
            children: vec![],
        };
        
        let mut buf = Vec::new();
        msg.encode(&mut buf).unwrap();
        
        let result = safe_process_nested_message(&buf);
        assert!(result.is_ok());
    }
    
    #[test]
    fn test_deep_nesting() {
        // Create a deeply nested structure
        let mut msg = NestedMessage {
            content: "leaf".to_string(),
            child: None,
            children: vec![],
        };
        
        // Nest beyond the limit
        for i in 0..MAX_RECURSION_DEPTH + 10 {
            msg = NestedMessage {
                content: format!("level_{}", i),
                child: Some(Box::new(msg)),
                children: vec![],
            };
        }
        
        let mut buf = Vec::new();
        msg.encode(&mut buf).unwrap();
        
        let result = safe_process_nested_message(&buf);
        assert!(result.is_err());
    }
}
```

### Example 5: Memory-Safe Parsing with Streaming

```rust
use prost::Message;
use std::io::{self, Read};

const CHUNK_SIZE: usize = 8192; // 8KB chunks
const MAX_TOTAL_SIZE: usize = 64 * 1024 * 1024; // 64MB

fn safe_stream_decode<M, R>(reader: &mut R) -> Result<M, Box<dyn std::error::Error>>
where
    M: Message + Default,
    R: Read,
{
    let mut buffer = Vec::new();
    let mut chunk = [0u8; CHUNK_SIZE];
    let mut total_read = 0;
    
    loop {
        match reader.read(&mut chunk)? {
            0 => break, // EOF
            n => {
                total_read += n;
                
                // Enforce size limit
                if total_read > MAX_TOTAL_SIZE {
                    return Err("Stream exceeds maximum allowed size".into());
                }
                
                buffer.extend_from_slice(&chunk[..n]);
            }
        }
    }
    
    // Decode the complete message
    M::decode(&buffer[..])
        .map_err(|e| format!("Failed to decode message: {}", e).into())
}

// Example with graceful error handling
pub enum ParseError {
    IoError(io::Error),
    DecodeError(prost::DecodeError),
    SizeExceeded,
    ValidationFailed(String),
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ParseError::IoError(e) => write!(f, "IO error: {}", e),
            ParseError::DecodeError(e) => write!(f, "Decode error: {}", e),
            ParseError::SizeExceeded => write!(f, "Message size exceeds limit"),
            ParseError::ValidationFailed(msg) => write!(f, "Validation failed: {}", msg),
        }
    }
}

impl std::error::Error for ParseError {}

impl From<io::Error> for ParseError {
    fn from(err: io::Error) -> Self {
        ParseError::IoError(err)
    }
}

impl From<prost::DecodeError> for ParseError {
    fn from(err: prost::DecodeError) -> Self {
        ParseError::DecodeError(err)
    }
}
```

---

## Best Practices Summary

### 1. **Always Set Limits**
- Maximum message size (e.g., 64MB)
- Maximum recursion depth (e.g., 100 levels)
- Maximum field counts
- Memory allocation limits

### 2. **Validate Early**
- Validate message structure immediately after parsing
- Use schema-based validation (protovalidate, prost-validate)
- Check field types, ranges, and constraints
- Reject malformed or unexpected data

### 3. **Use Safe Parsing APIs**
- In C++: Use `CodedInputStream` with limits set
- In Rust: Use `prost` with size checks
- Enable fuzzing and security testing
- Keep libraries up to date

### 4. **Defense in Depth**
- Implement multiple validation layers
- Use sandboxing for untrusted input
- Monitor resource consumption
- Log and alert on suspicious patterns

### 5. **Handle Errors Gracefully**
- Never panic on malformed input
- Return descriptive error messages
- Implement rate limiting for repeated failures
- Consider circuit breakers for production systems

### 6. **Stay Updated**
- Monitor CVE databases for protobuf vulnerabilities
- Apply security patches promptly
- Test with fuzzing tools
- Review security advisories regularly

---

## Summary

Defending against malicious Protocol Buffer input requires a multi-layered approach combining input validation, resource limits, safe parsing practices, and defense-in-depth strategies. The key vulnerabilities—uncontrolled recursion, memory exhaustion, stack overflow, and parser bugs—can be mitigated through:

1. **Strict size and recursion limits** enforced at the parser level
2. **Schema-based validation** using tools like protovalidate for both C++ and Rust
3. **Safe API usage** with proper error handling and resource constraints
4. **Regular security updates** to address known vulnerabilities
5. **Runtime monitoring** and anomaly detection

Both C++ and Rust ecosystems provide robust tools for secure protobuf handling. C++ offers battle-tested parsers with extensive fuzzing, while Rust provides memory safety guarantees and modern validation libraries. By implementing the defensive coding patterns shown in this document, developers can significantly reduce the attack surface of their protobuf-based systems.

The examples demonstrate practical implementations ranging from basic size checking to advanced recursion guards and validation frameworks. These patterns should be adapted to specific application requirements while maintaining the core principle: **never trust untrusted input**.