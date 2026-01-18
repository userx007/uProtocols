# Language-Specific API Patterns in Protocol Buffers

Protocol Buffers generates code differently for each programming language, adapting to native idioms and conventions. Understanding these patterns helps you write idiomatic code when working with protobuf messages across different languages.

## Core Concepts

Each language's protobuf compiler (protoc with language-specific plugins) generates accessor methods, builders, and utilities that follow that language's best practices:

- **Java**: Builder pattern with fluent APIs
- **C++**: Direct field access with getters/setters and mutable accessors
- **Rust**: Trait-based approach with ownership semantics
- **C#**: Property-based access with nullable reference types
- **Python**: Dynamic attribute access
- **Go**: Struct fields with getter methods

## Example .proto Definition

Let's use this message definition for all examples:

```protobuf
syntax = "proto3";

package example;

message User {
  string name = 1;
  int32 age = 2;
  repeated string emails = 3;
  Address address = 4;
}

message Address {
  string street = 1;
  string city = 2;
  string country = 3;
}
```

## C++ API Patterns

C++ protobuf APIs provide direct memory access with clear ownership semantics.

```cpp
#include "user.pb.h"
#include <iostream>
#include <memory>

void demonstrate_cpp_patterns() {
    // Creating and setting fields
    example::User user;
    user.set_name("Alice");
    user.set_age(30);
    
    // Repeated fields - add elements
    user.add_emails("alice@example.com");
    user.add_emails("alice.work@company.com");
    
    // Access repeated fields by index
    std::cout << "First email: " << user.emails(0) << std::endl;
    
    // Get repeated field count
    std::cout << "Email count: " << user.emails_size() << std::endl;
    
    // Mutable access to repeated fields
    user.mutable_emails()->Add("alice.personal@email.com");
    
    // Nested messages - mutable pointer pattern
    example::Address* addr = user.mutable_address();
    addr->set_street("123 Main St");
    addr->set_city("New York");
    addr->set_country("USA");
    
    // Const access to nested messages
    if (user.has_address()) {
        const example::Address& address = user.address();
        std::cout << "City: " << address.city() << std::endl;
    }
    
    // Getting fields (returns copy for primitives, const ref for strings)
    std::string name = user.name();
    const std::string& name_ref = user.name(); // More efficient
    int32_t age = user.age();
    
    // Check if optional field is set (proto2 style, proto3 uses presence)
    bool has_name = !user.name().empty();
    
    // Clear fields
    user.clear_name();
    user.clear_emails();
    
    // Serialization
    std::string serialized;
    user.SerializeToString(&serialized);
    
    // Deserialization
    example::User user2;
    user2.ParseFromString(serialized);
    
    // Move semantics (C++11+)
    example::User user3 = std::move(user2);
    
    // Arena allocation for better performance
    google::protobuf::Arena arena;
    example::User* arena_user = 
        google::protobuf::Arena::CreateMessage<example::User>(&arena);
    arena_user->set_name("Bob");
}

// Working with repeated fields using iterators
void iterate_repeated_fields() {
    example::User user;
    user.add_emails("one@example.com");
    user.add_emails("two@example.com");
    
    // Range-based for loop (C++11+)
    for (const auto& email : user.emails()) {
        std::cout << email << std::endl;
    }
    
    // Traditional iterator approach
    for (int i = 0; i < user.emails_size(); ++i) {
        std::cout << user.emails(i) << std::endl;
    }
    
    // Mutable iteration
    for (auto& email : *user.mutable_emails()) {
        email = email + ".backup";
    }
}
```

**Key C++ Patterns:**
- `set_*()` for setting scalar fields
- `mutable_*()` returns pointer for modification
- `*()` const accessor returns const reference
- `add_*()` for appending to repeated fields
- `has_*()` to check field presence (proto2)
- Arena allocation for performance-critical code

## Rust API Patterns

Rust protobuf libraries (like `prost`) generate idiomatic Rust code with traits and ownership semantics.

```rust
// Using prost (popular Rust protobuf library)
use prost::Message;

// Generated struct (simplified representation)
#[derive(Clone, PartialEq, Message)]
pub struct User {
    #[prost(string, tag = "1")]
    pub name: String,
    
    #[prost(int32, tag = "2")]
    pub age: i32,
    
    #[prost(string, repeated, tag = "3")]
    pub emails: Vec<String>,
    
    #[prost(message, optional, tag = "4")]
    pub address: Option<Address>,
}

#[derive(Clone, PartialEq, Message)]
pub struct Address {
    #[prost(string, tag = "1")]
    pub street: String,
    
    #[prost(string, tag = "2")]
    pub city: String,
    
    #[prost(string, tag = "3")]
    pub country: String,
}

fn demonstrate_rust_patterns() {
    // Creating with struct literal syntax
    let mut user = User {
        name: "Alice".to_string(),
        age: 30,
        emails: vec![
            "alice@example.com".to_string(),
            "alice.work@company.com".to_string(),
        ],
        address: Some(Address {
            street: "123 Main St".to_string(),
            city: "New York".to_string(),
            country: "USA".to_string(),
        }),
    };
    
    // Direct field access (public fields)
    println!("Name: {}", user.name);
    user.age += 1;
    
    // Working with repeated fields (Vec)
    user.emails.push("alice.personal@email.com".to_string());
    
    for email in &user.emails {
        println!("Email: {}", email);
    }
    
    // Pattern matching on optional fields
    match &user.address {
        Some(addr) => println!("City: {}", addr.city),
        None => println!("No address"),
    }
    
    // Using if let for optional fields
    if let Some(addr) = &user.address {
        println!("Street: {}", addr.street);
    }
    
    // Modifying nested optional messages
    if let Some(addr) = &mut user.address {
        addr.city = "Boston".to_string();
    }
    
    // Serialization (returns Vec<u8>)
    let serialized = user.encode_to_vec();
    
    // Deserialization (returns Result)
    match User::decode(&serialized[..]) {
        Ok(decoded_user) => println!("Decoded: {}", decoded_user.name),
        Err(e) => eprintln!("Decode error: {}", e),
    }
    
    // Clone trait automatically derived
    let user_copy = user.clone();
    
    // Move semantics (default Rust behavior)
    let user_moved = user;
    // user is no longer accessible here
    
    println!("Moved user: {}", user_moved.name);
}

// Using Default trait
fn create_with_default() {
    let mut user = User::default();
    user.name = "Bob".to_string();
    user.age = 25;
    
    // address is None by default for optional fields
    assert!(user.address.is_none());
    
    // emails is empty Vec by default for repeated fields
    assert!(user.emails.is_empty());
}

// Builder pattern (if using a builder library)
fn builder_pattern() {
    let user = User {
        name: "Charlie".to_string(),
        age: 35,
        emails: vec!["charlie@example.com".to_string()],
        address: Some(Address {
            street: "456 Oak Ave".to_string(),
            city: "Seattle".to_string(),
            country: "USA".to_string(),
        }),
    };
    
    // More idiomatic with ..Default::default()
    let user2 = User {
        name: "David".to_string(),
        ..Default::default()
    };
    
    println!("User2 age (default): {}", user2.age); // 0
}
```

**Key Rust Patterns:**
- Public struct fields for direct access
- `Vec<T>` for repeated fields
- `Option<T>` for optional/nested messages
- Traits: `Message`, `Clone`, `PartialEq`, `Default`
- `encode_to_vec()` and `decode()` for serialization
- Ownership and borrowing follow Rust semantics
- Pattern matching for optional fields

## C++ vs Rust Comparison

```cpp
// C++ - Pointer-based mutable access
user.mutable_address()->set_city("Boston");

// Mutable repeated field access
user.mutable_emails()->Add("new@example.com");

// Checking presence
if (user.has_address()) { /* ... */ }
```

```rust
// Rust - Direct mutable reference
if let Some(addr) = &mut user.address {
    addr.city = "Boston".to_string();
}

// Direct Vec operations
user.emails.push("new@example.com".to_string());

// Checking presence
if user.address.is_some() { /* ... */ }
```

## Summary

**Language-specific protobuf API patterns reflect each language's idioms:**

- **C++** uses getters/setters with `mutable_*()` methods returning pointers for modification. It provides fine-grained control with arena allocation for performance and supports both value and reference semantics.

- **Rust** exposes public struct fields directly, using `Vec<T>` for repeated fields and `Option<T>` for optional messages. Code is safe by default with ownership/borrowing semantics, and common traits (`Clone`, `Default`, `Message`) are automatically derived.

- **Java** (mentioned in the topic) uses immutable messages with separate Builder classes, fluent APIs for construction, and method chaining for readability.

- **C#** uses properties with null-safe patterns in modern versions, capitalizing field names following .NET conventions.

Understanding these patterns helps you:
1. Write idiomatic code in each language
2. Choose the right language for your use case
3. Interoperate between services written in different languages
4. Optimize performance using language-specific features (arena allocation in C++, zero-copy in Rust)

The core protobuf wire format remains identical across all languages, ensuring seamless interoperability while each language provides APIs that feel natural to developers.