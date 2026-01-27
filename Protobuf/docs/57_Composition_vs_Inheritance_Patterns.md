# Protocol Buffers: Composition vs Inheritance Patterns

## Overview

Protocol Buffers (Protobuf) is a language-neutral, platform-neutral extensible mechanism for serializing structured data. Unlike object-oriented programming languages that support inheritance, **Protobuf does not support traditional inheritance**. Instead, it relies on **composition** and **interface-like patterns** to achieve similar design goals.

This document explores how to model relationships and shared behavior in Protobuf using composition patterns, with practical examples in C++, C, and Rust.

---

## Why No Inheritance in Protobuf?

Protobuf is designed for:
1. **Language neutrality** - Not all languages support inheritance the same way
2. **Serialization efficiency** - Simple, flat structures are easier to serialize
3. **Forward/backward compatibility** - Simpler schemas are easier to evolve
4. **Interoperability** - Works across different programming paradigms

---

## Key Patterns for Protobuf Design

### 1. Composition Pattern
Embed messages within other messages to build complex types.

### 2. Oneof Pattern
Represent polymorphic types or union-like structures.

### 3. Any Type
Store arbitrary message types dynamically.

### 4. Interface Simulation
Use shared field patterns and code generation to simulate interfaces.

---

## Pattern Examples

### Pattern 1: Basic Composition

Instead of inheritance, embed common data structures.

#### Protobuf Definition

```protobuf
syntax = "proto3";

package examples;

// Common address information (like a "base class")
message Address {
  string street = 1;
  string city = 2;
  string state = 3;
  string zip_code = 4;
  string country = 5;
}

// Person "composes" Address instead of inheriting
message Person {
  string name = 1;
  int32 age = 2;
  Address home_address = 3;  // Composition
  Address work_address = 4;   // Can have multiple
}

// Company also uses Address
message Company {
  string name = 1;
  Address headquarters = 2;   // Same composition pattern
  repeated Address branches = 3;
}
```

---

### Pattern 2: Oneof for Polymorphism

Use `oneof` to represent different types that share a common interface.

#### Protobuf Definition

```protobuf
syntax = "proto3";

package examples;

// Different payment methods (like subclasses)
message CreditCard {
  string card_number = 1;
  string expiry_date = 2;
  string cvv = 3;
}

message BankTransfer {
  string account_number = 1;
  string routing_number = 2;
  string bank_name = 3;
}

message PayPal {
  string email = 1;
}

// Payment uses oneof to represent different payment types
message Payment {
  string transaction_id = 1;
  double amount = 2;
  
  oneof payment_method {
    CreditCard credit_card = 3;
    BankTransfer bank_transfer = 4;
    PayPal paypal = 5;
  }
}
```

---

### Pattern 3: Common Fields Pattern

Duplicate common fields across messages (simulating shared interface).

#### Protobuf Definition

```protobuf
syntax = "proto3";

package examples;

import "google/protobuf/timestamp.proto";

// Common fields pattern - each message has id, created_at
message User {
  string id = 1;
  google.protobuf.Timestamp created_at = 2;
  string username = 3;
  string email = 4;
}

message Post {
  string id = 1;
  google.protobuf.Timestamp created_at = 2;
  string title = 3;
  string content = 4;
  string author_id = 5;
}

message Comment {
  string id = 1;
  google.protobuf.Timestamp created_at = 2;
  string text = 3;
  string post_id = 4;
  string author_id = 5;
}
```

---

## Code Examples

### C++ Examples

#### Example 1: Basic Composition

```cpp
#include <iostream>
#include <memory>
#include "examples.pb.h"

using namespace examples;

void demonstrateComposition() {
    // Create a Person with composed Address
    Person person;
    person.set_name("John Doe");
    person.set_age(30);
    
    // Set home address through composition
    Address* home = person.mutable_home_address();
    home->set_street("123 Main St");
    home->set_city("Springfield");
    home->set_state("IL");
    home->set_zip_code("62701");
    home->set_country("USA");
    
    // Set work address
    Address* work = person.mutable_work_address();
    work->set_street("456 Corporate Blvd");
    work->set_city("Chicago");
    work->set_state("IL");
    work->set_zip_code("60601");
    work->set_country("USA");
    
    std::cout << "Person: " << person.name() << std::endl;
    std::cout << "Home: " << person.home_address().city() << ", " 
              << person.home_address().state() << std::endl;
    std::cout << "Work: " << person.work_address().city() << ", " 
              << person.work_address().state() << std::endl;
}

// Reusable function that works with any message containing Address
void printAddress(const Address& addr) {
    std::cout << addr.street() << std::endl;
    std::cout << addr.city() << ", " << addr.state() 
              << " " << addr.zip_code() << std::endl;
    std::cout << addr.country() << std::endl;
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    demonstrateComposition();
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

#### Example 2: Oneof Pattern (Polymorphism)

```cpp
#include <iostream>
#include "examples.pb.h"

using namespace examples;

// Process different payment methods (like handling subclasses)
void processPayment(const Payment& payment) {
    std::cout << "Processing payment ID: " << payment.transaction_id() << std::endl;
    std::cout << "Amount: $" << payment.amount() << std::endl;
    
    // Check which payment method is set (polymorphic behavior)
    switch (payment.payment_method_case()) {
        case Payment::kCreditCard: {
            const CreditCard& cc = payment.credit_card();
            std::cout << "Payment Method: Credit Card" << std::endl;
            std::cout << "Card ending in: " 
                      << cc.card_number().substr(cc.card_number().length() - 4) 
                      << std::endl;
            break;
        }
        case Payment::kBankTransfer: {
            const BankTransfer& bt = payment.bank_transfer();
            std::cout << "Payment Method: Bank Transfer" << std::endl;
            std::cout << "Bank: " << bt.bank_name() << std::endl;
            break;
        }
        case Payment::kPaypal: {
            const PayPal& pp = payment.paypal();
            std::cout << "Payment Method: PayPal" << std::endl;
            std::cout << "PayPal Email: " << pp.email() << std::endl;
            break;
        }
        case Payment::PAYMENT_METHOD_NOT_SET:
            std::cout << "No payment method set!" << std::endl;
            break;
    }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // Create payment with credit card
    Payment payment1;
    payment1.set_transaction_id("TXN-001");
    payment1.set_amount(99.99);
    payment1.mutable_credit_card()->set_card_number("4111111111111111");
    payment1.mutable_credit_card()->set_expiry_date("12/25");
    payment1.mutable_credit_card()->set_cvv("123");
    
    processPayment(payment1);
    
    std::cout << "\n---\n\n";
    
    // Create payment with PayPal
    Payment payment2;
    payment2.set_transaction_id("TXN-002");
    payment2.set_amount(149.99);
    payment2.mutable_paypal()->set_email("user@example.com");
    
    processPayment(payment2);
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

#### Example 3: Interface-like Pattern with Templates

```cpp
#include <iostream>
#include <string>
#include "examples.pb.h"

using namespace examples;

// Template function that works with any message having id and created_at
// (simulating an interface)
template<typename T>
void printEntityInfo(const T& entity) {
    std::cout << "ID: " << entity.id() << std::endl;
    std::cout << "Created: " << entity.created_at().seconds() << std::endl;
}

// Specialized processing based on type
void processUser(const User& user) {
    printEntityInfo(user);
    std::cout << "Username: " << user.username() << std::endl;
    std::cout << "Email: " << user.email() << std::endl;
}

void processPost(const Post& post) {
    printEntityInfo(post);
    std::cout << "Title: " << post.title() << std::endl;
    std::cout << "Author ID: " << post.author_id() << std::endl;
}

void processComment(const Comment& comment) {
    printEntityInfo(comment);
    std::cout << "Text: " << comment.text() << std::endl;
    std::cout << "Post ID: " << comment.post_id() << std::endl;
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    User user;
    user.set_id("user-123");
    user.mutable_created_at()->set_seconds(time(nullptr));
    user.set_username("johndoe");
    user.set_email("john@example.com");
    
    Post post;
    post.set_id("post-456");
    post.mutable_created_at()->set_seconds(time(nullptr));
    post.set_title("Hello Protobuf");
    post.set_author_id("user-123");
    
    processUser(user);
    std::cout << "\n---\n\n";
    processPost(post);
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

---

### C Examples

Since Protobuf's C support is limited, here's how you might work with composition patterns using the C API:

```c
#include <stdio.h>
#include <stdlib.h>
#include "examples.pb-c.h"

void demonstrate_composition_c() {
    // Create a Person message
    Examples__Person person = EXAMPLES__PERSON__INIT;
    Examples__Address home_addr = EXAMPLES__ADDRESS__INIT;
    Examples__Address work_addr = EXAMPLES__ADDRESS__INIT;
    
    // Set person fields
    person.name = "John Doe";
    person.age = 30;
    
    // Set home address (composition)
    home_addr.street = "123 Main St";
    home_addr.city = "Springfield";
    home_addr.state = "IL";
    home_addr.zip_code = "62701";
    home_addr.country = "USA";
    person.home_address = &home_addr;
    
    // Set work address
    work_addr.street = "456 Corporate Blvd";
    work_addr.city = "Chicago";
    work_addr.state = "IL";
    work_addr.zip_code = "60601";
    work_addr.country = "USA";
    person.work_address = &work_addr;
    
    printf("Person: %s\n", person.name);
    printf("Home: %s, %s\n", person.home_address->city, 
           person.home_address->state);
    printf("Work: %s, %s\n", person.work_address->city, 
           person.work_address->state);
}

void demonstrate_oneof_c() {
    // Create a Payment message with credit card
    Examples__Payment payment = EXAMPLES__PAYMENT__INIT;
    Examples__CreditCard cc = EXAMPLES__CREDIT_CARD__INIT;
    
    payment.transaction_id = "TXN-001";
    payment.amount = 99.99;
    
    // Set credit card (oneof field)
    cc.card_number = "4111111111111111";
    cc.expiry_date = "12/25";
    cc.cvv = "123";
    
    payment.payment_method_case = EXAMPLES__PAYMENT__PAYMENT_METHOD_CREDIT_CARD;
    payment.credit_card = &cc;
    
    // Process payment based on type
    printf("Transaction ID: %s\n", payment.transaction_id);
    printf("Amount: $%.2f\n", payment.amount);
    
    switch (payment.payment_method_case) {
        case EXAMPLES__PAYMENT__PAYMENT_METHOD_CREDIT_CARD:
            printf("Payment Method: Credit Card\n");
            printf("Card: %s\n", payment.credit_card->card_number);
            break;
        case EXAMPLES__PAYMENT__PAYMENT_METHOD_BANK_TRANSFER:
            printf("Payment Method: Bank Transfer\n");
            break;
        case EXAMPLES__PAYMENT__PAYMENT_METHOD_PAYPAL:
            printf("Payment Method: PayPal\n");
            break;
        default:
            printf("No payment method set\n");
            break;
    }
}

int main() {
    demonstrate_composition_c();
    printf("\n---\n\n");
    demonstrate_oneof_c();
    
    return 0;
}
```

---

### Rust Examples

Rust's type system works particularly well with Protobuf's composition patterns.

#### Example 1: Basic Composition in Rust

```rust
// Generated from protobuf
mod examples {
    include!(concat!(env!("OUT_DIR"), "/examples.rs"));
}

use examples::{Person, Address, Company};

fn demonstrate_composition() {
    // Create a Person with composed Address
    let mut person = Person::default();
    person.name = "John Doe".to_string();
    person.age = 30;
    
    // Set home address through composition
    person.home_address = Some(Address {
        street: "123 Main St".to_string(),
        city: "Springfield".to_string(),
        state: "IL".to_string(),
        zip_code: "62701".to_string(),
        country: "USA".to_string(),
    });
    
    // Set work address
    person.work_address = Some(Address {
        street: "456 Corporate Blvd".to_string(),
        city: "Chicago".to_string(),
        state: "IL".to_string(),
        zip_code: "60601".to_string(),
        country: "USA".to_string(),
    });
    
    println!("Person: {}", person.name);
    if let Some(home) = &person.home_address {
        println!("Home: {}, {}", home.city, home.state);
    }
    if let Some(work) = &person.work_address {
        println!("Work: {}, {}", work.city, work.state);
    }
}

// Reusable function that works with Address
fn print_address(addr: &Address) {
    println!("{}", addr.street);
    println!("{}, {} {}", addr.city, addr.state, addr.zip_code);
    println!("{}", addr.country);
}

fn main() {
    demonstrate_composition();
}
```

#### Example 2: Oneof Pattern in Rust

```rust
use examples::{Payment, payment::PaymentMethod, CreditCard, BankTransfer, PayPal};

fn process_payment(payment: &Payment) {
    println!("Processing payment ID: {}", payment.transaction_id);
    println!("Amount: ${:.2}", payment.amount);
    
    // Match on the oneof field (Rust enum)
    match &payment.payment_method {
        Some(PaymentMethod::CreditCard(cc)) => {
            println!("Payment Method: Credit Card");
            let card_len = cc.card_number.len();
            if card_len >= 4 {
                println!("Card ending in: {}", &cc.card_number[card_len-4..]);
            }
        }
        Some(PaymentMethod::BankTransfer(bt)) => {
            println!("Payment Method: Bank Transfer");
            println!("Bank: {}", bt.bank_name);
        }
        Some(PaymentMethod::Paypal(pp)) => {
            println!("Payment Method: PayPal");
            println!("PayPal Email: {}", pp.email);
        }
        None => {
            println!("No payment method set!");
        }
    }
}

fn main() {
    // Create payment with credit card
    let payment1 = Payment {
        transaction_id: "TXN-001".to_string(),
        amount: 99.99,
        payment_method: Some(PaymentMethod::CreditCard(CreditCard {
            card_number: "4111111111111111".to_string(),
            expiry_date: "12/25".to_string(),
            cvv: "123".to_string(),
        })),
    };
    
    process_payment(&payment1);
    
    println!("\n---\n");
    
    // Create payment with PayPal
    let payment2 = Payment {
        transaction_id: "TXN-002".to_string(),
        amount: 149.99,
        payment_method: Some(PaymentMethod::Paypal(PayPal {
            email: "user@example.com".to_string(),
        })),
    };
    
    process_payment(&payment2);
}
```

#### Example 3: Trait-based Interface Pattern in Rust

```rust
use examples::{User, Post, Comment};
use prost_types::Timestamp;

// Define a trait for entities with common fields
trait Entity {
    fn id(&self) -> &str;
    fn created_at(&self) -> Option<&Timestamp>;
    
    fn print_entity_info(&self) {
        println!("ID: {}", self.id());
        if let Some(ts) = self.created_at() {
            println!("Created: {}", ts.seconds);
        }
    }
}

// Implement trait for User
impl Entity for User {
    fn id(&self) -> &str {
        &self.id
    }
    
    fn created_at(&self) -> Option<&Timestamp> {
        self.created_at.as_ref()
    }
}

// Implement trait for Post
impl Entity for Post {
    fn id(&self) -> &str {
        &self.id
    }
    
    fn created_at(&self) -> Option<&Timestamp> {
        self.created_at.as_ref()
    }
}

// Implement trait for Comment
impl Entity for Comment {
    fn id(&self) -> &str {
        &self.id
    }
    
    fn created_at(&self) -> Option<&Timestamp> {
        self.created_at.as_ref()
    }
}

// Generic function that works with any Entity
fn process_entity<T: Entity>(entity: &T) {
    entity.print_entity_info();
}

fn process_user(user: &User) {
    user.print_entity_info();
    println!("Username: {}", user.username);
    println!("Email: {}", user.email);
}

fn process_post(post: &Post) {
    post.print_entity_info();
    println!("Title: {}", post.title);
    println!("Author ID: {}", post.author_id);
}

fn main() {
    use std::time::{SystemTime, UNIX_EPOCH};
    
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs() as i64;
    
    let user = User {
        id: "user-123".to_string(),
        created_at: Some(Timestamp {
            seconds: now,
            nanos: 0,
        }),
        username: "johndoe".to_string(),
        email: "john@example.com".to_string(),
    };
    
    let post = Post {
        id: "post-456".to_string(),
        created_at: Some(Timestamp {
            seconds: now,
            nanos: 0,
        }),
        title: "Hello Protobuf".to_string(),
        content: "Learning composition patterns".to_string(),
        author_id: "user-123".to_string(),
    };
    
    process_user(&user);
    println!("\n---\n");
    process_post(&post);
}
```

---

## Best Practices

### 1. **Use Composition Over Duplication**
- Extract common structures into separate messages
- Embed them where needed
- Example: Address, Location, Metadata messages

### 2. **Choose the Right Pattern for Polymorphism**
- **Oneof**: When you need exactly one of several types
- **Repeated + Oneof**: For collections of different types
- **Any**: For truly dynamic types (use sparingly)

### 3. **Design for Evolution**
- Use field numbers wisely
- Reserve numbers for removed fields
- Consider future extensions

### 4. **Leverage Language Features**
- **C++**: Templates for generic code
- **Rust**: Traits for shared behavior
- **Go**: Interfaces for duck typing

### 5. **Document Common Patterns**
- Comment your proto files
- Explain design decisions
- Provide usage examples

---

## Comparison: Inheritance vs Composition

### Traditional OOP Inheritance
```cpp
// Traditional C++ inheritance (NOT possible in Protobuf)
class Entity {
protected:
    std::string id;
    time_t created_at;
public:
    virtual void process() = 0;
};

class User : public Entity {
    std::string username;
public:
    void process() override { /* ... */ }
};
```

### Protobuf Composition Approach
```protobuf
// Protobuf composition approach
message EntityMetadata {
  string id = 1;
  google.protobuf.Timestamp created_at = 2;
}

message User {
  EntityMetadata metadata = 1;  // Composition
  string username = 2;
}
```

### Advantages of Composition
1. **Flexibility**: Can compose multiple types
2. **Clarity**: Explicit relationships
3. **Serialization**: Straightforward wire format
4. **Language Support**: Works everywhere
5. **Versioning**: Easier schema evolution

---

## Common Pitfalls

### 1. **Over-nesting**
❌ Bad:
```protobuf
message Person {
  message Name {
    message First {
      string value = 1;
    }
    First first = 1;
  }
  Name name = 1;
}
```

✅ Good:
```protobuf
message Person {
  string first_name = 1;
  string last_name = 2;
}
```

### 2. **Misusing Oneof**
❌ Bad: Using oneof for optional fields
```protobuf
message User {
  oneof optional_email {
    string email = 1;
  }
}
```

✅ Good: Use optional or just regular fields
```protobuf
message User {
  optional string email = 1;
}
```

### 3. **Duplicating Everything**
❌ Bad: Copy-paste common fields
```protobuf
message User {
  string id = 1;
  string name = 2;
  string email = 3;
}

message Admin {
  string id = 1;  // Duplicated
  string name = 2;  // Duplicated
  string email = 3;  // Duplicated
  repeated string permissions = 4;
}
```

✅ Good: Use composition
```protobuf
message UserInfo {
  string id = 1;
  string name = 2;
  string email = 3;
}

message User {
  UserInfo info = 1;
}

message Admin {
  UserInfo info = 1;
  repeated string permissions = 2;
}
```

---

## Summary

Protocol Buffers doesn't support traditional inheritance, but provides powerful composition mechanisms:

1. **Message Composition**: Embed messages within messages for code reuse
2. **Oneof Fields**: Represent polymorphic types and union-like structures
3. **Common Field Patterns**: Share field conventions across messages
4. **Language-Level Abstractions**: Use templates (C++), traits (Rust), interfaces (Go)

These patterns provide:
- **Language neutrality**: Work across all target languages
- **Schema evolution**: Easy to extend and modify
- **Serialization efficiency**: Simple, predictable wire format
- **Clear semantics**: Explicit, understandable relationships

The key is to think in terms of **composition** ("has-a") rather than **inheritance** ("is-a"), which often leads to more flexible and maintainable designs.

When designing Protobuf schemas:
- Start with composition
- Use oneof for polymorphism
- Leverage language features for abstraction
- Keep schemas simple and evolvable
- Document your design patterns

By embracing composition over inheritance, you create Protobuf schemas that are robust, maintainable, and work seamlessly across different programming languages and platforms.