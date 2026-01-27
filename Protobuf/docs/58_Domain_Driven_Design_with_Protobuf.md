# Domain-Driven Design with Protobuf

## Overview

Domain-Driven Design (DDD) is a software development approach that focuses on modeling software to match a business domain according to input from domain experts. Protocol Buffers (Protobuf) provides an excellent mechanism for defining and implementing DDD concepts such as bounded contexts, aggregates, entities, and value objects through its strongly-typed message definitions.

## Core DDD Concepts

### 1. Bounded Context
A bounded context defines a specific boundary within which a domain model is consistent and valid. Each bounded context has its own ubiquitous language and model, preventing ambiguity and conflicts.

### 2. Entity
An entity is a domain object with a unique identity that persists over time. Two entities are considered different even if all their attributes are identical, as long as their identities differ.

### 3. Value Object
A value object has no conceptual identity and is defined solely by its attributes. Two value objects with identical attributes are considered equal.

### 4. Aggregate
An aggregate is a cluster of entities and value objects treated as a single unit for data changes. It enforces invariants and maintains consistency.

### 5. Aggregate Root
The aggregate root is the entry point to an aggregate. All external access to the aggregate must go through the aggregate root, which enforces business rules and maintains consistency.

---

## Modeling DDD with Protobuf

### Defining Bounded Contexts with Protobuf

Protobuf packages can represent bounded contexts, providing clear separation between different domain models.

```protobuf
// order_context.proto - Order Management Bounded Context
syntax = "proto3";
package ecommerce.order;

option java_package = "com.ecommerce.order";
option csharp_namespace = "ECommerce.Order";

// Entities and Value Objects for Order Context
message Order {
  string order_id = 1;              // Entity identifier
  string customer_id = 2;           // Reference to customer (different context)
  OrderStatus status = 3;
  repeated OrderLine items = 4;     // Aggregate contains multiple order lines
  Money total_amount = 5;           // Value object
  google.protobuf.Timestamp created_at = 6;
  google.protobuf.Timestamp updated_at = 7;
}

message OrderLine {
  int32 position = 1;               // Local identity within aggregate
  string product_id = 2;
  string product_name = 3;          // Denormalized for historical accuracy
  int32 quantity = 4;
  Money unit_price = 5;
  Money line_total = 6;
}

enum OrderStatus {
  ORDER_STATUS_UNSPECIFIED = 0;
  ORDER_STATUS_PENDING = 1;
  ORDER_STATUS_CONFIRMED = 2;
  ORDER_STATUS_SHIPPED = 3;
  ORDER_STATUS_DELIVERED = 4;
  ORDER_STATUS_CANCELLED = 5;
}

message Money {
  string currency = 1;              // Value object (immutable)
  int64 amount_in_cents = 2;
}
```

```protobuf
// customer_context.proto - Customer Management Bounded Context
syntax = "proto3";
package ecommerce.customer;

option java_package = "com.ecommerce.customer";

// Different representation of customer in Customer context
message Customer {
  string customer_id = 1;
  string email = 2;
  PersonName name = 3;
  Address shipping_address = 4;
  Address billing_address = 5;
  CustomerStatus status = 6;
}

message PersonName {
  string first_name = 1;
  string last_name = 2;
  string middle_name = 3;
}

message Address {
  string street = 1;
  string city = 2;
  string state = 3;
  string postal_code = 4;
  string country = 5;
}

enum CustomerStatus {
  CUSTOMER_STATUS_UNSPECIFIED = 0;
  CUSTOMER_STATUS_ACTIVE = 1;
  CUSTOMER_STATUS_SUSPENDED = 2;
  CUSTOMER_STATUS_CLOSED = 3;
}
```

---

## C/C++ Implementation Examples

### Setting Up the Build Environment

First, ensure you have the protobuf compiler and C++ libraries installed.

```bash
# Install protobuf compiler and libraries
sudo apt-get install protobuf-compiler libprotobuf-dev

# Compile the proto files
protoc --cpp_out=. order_context.proto
protoc --cpp_out=. customer_context.proto
```

### C++ Implementation: Aggregate Root Pattern

```cpp
// order_aggregate.h
#ifndef ORDER_AGGREGATE_H
#define ORDER_AGGREGATE_H

#include "order_context.pb.h"
#include <memory>
#include <vector>
#include <stdexcept>

namespace ecommerce {
namespace order {

// Aggregate Root for Order
class OrderAggregate {
private:
    Order order_;
    
    // Private helper to enforce invariants
    void recalculateTotal() {
        int64_t total = 0;
        for (const auto& line : order_.items()) {
            total += line.line_total().amount_in_cents();
        }
        order_.mutable_total_amount()->set_amount_in_cents(total);
    }
    
    // Validate invariants
    void validateInvariants() const {
        if (order_.items().empty()) {
            throw std::runtime_error("Order must have at least one item");
        }
        
        // Ensure total is correct
        int64_t calculated_total = 0;
        for (const auto& line : order_.items()) {
            calculated_total += line.line_total().amount_in_cents();
        }
        
        if (calculated_total != order_.total_amount().amount_in_cents()) {
            throw std::runtime_error("Order total does not match sum of line items");
        }
    }

public:
    // Constructor for new order
    explicit OrderAggregate(const std::string& order_id, 
                           const std::string& customer_id) {
        order_.set_order_id(order_id);
        order_.set_customer_id(customer_id);
        order_.set_status(ORDER_STATUS_PENDING);
        
        // Set created timestamp
        auto* timestamp = order_.mutable_created_at();
        timestamp->set_seconds(time(nullptr));
    }
    
    // Constructor from existing protobuf
    explicit OrderAggregate(const Order& order) : order_(order) {
        validateInvariants();
    }
    
    // Add item to order (business logic)
    void addItem(const std::string& product_id, 
                 const std::string& product_name,
                 int32_t quantity,
                 const Money& unit_price) {
        if (order_.status() != ORDER_STATUS_PENDING) {
            throw std::runtime_error("Cannot modify order that is not pending");
        }
        
        if (quantity <= 0) {
            throw std::invalid_argument("Quantity must be positive");
        }
        
        // Create new order line
        OrderLine* line = order_.add_items();
        line->set_position(order_.items_size());
        line->set_product_id(product_id);
        line->set_product_name(product_name);
        line->set_quantity(quantity);
        *line->mutable_unit_price() = unit_price;
        
        // Calculate line total
        int64_t line_total = quantity * unit_price.amount_in_cents();
        line->mutable_line_total()->set_currency(unit_price.currency());
        line->mutable_line_total()->set_amount_in_cents(line_total);
        
        // Update order total
        recalculateTotal();
        order_.mutable_total_amount()->set_currency(unit_price.currency());
        
        // Update modified timestamp
        order_.mutable_updated_at()->set_seconds(time(nullptr));
    }
    
    // Remove item by position
    void removeItem(int32_t position) {
        if (order_.status() != ORDER_STATUS_PENDING) {
            throw std::runtime_error("Cannot modify order that is not pending");
        }
        
        auto* items = order_.mutable_items();
        if (position < 0 || position >= items->size()) {
            throw std::out_of_range("Invalid item position");
        }
        
        items->erase(items->begin() + position);
        
        // Reindex positions
        for (int i = 0; i < items->size(); ++i) {
            items->at(i).set_position(i);
        }
        
        recalculateTotal();
        order_.mutable_updated_at()->set_seconds(time(nullptr));
    }
    
    // Confirm the order
    void confirm() {
        if (order_.status() != ORDER_STATUS_PENDING) {
            throw std::runtime_error("Only pending orders can be confirmed");
        }
        
        if (order_.items().empty()) {
            throw std::runtime_error("Cannot confirm order with no items");
        }
        
        order_.set_status(ORDER_STATUS_CONFIRMED);
        order_.mutable_updated_at()->set_seconds(time(nullptr));
    }
    
    // Cancel the order
    void cancel() {
        if (order_.status() == ORDER_STATUS_DELIVERED) {
            throw std::runtime_error("Cannot cancel delivered order");
        }
        
        order_.set_status(ORDER_STATUS_CANCELLED);
        order_.mutable_updated_at()->set_seconds(time(nullptr));
    }
    
    // Access to underlying protobuf (read-only)
    const Order& getOrder() const {
        return order_;
    }
    
    // Serialize for persistence
    std::string serialize() const {
        validateInvariants();
        return order_.SerializeAsString();
    }
    
    // Deserialize from storage
    static OrderAggregate deserialize(const std::string& data) {
        Order order;
        if (!order.ParseFromString(data)) {
            throw std::runtime_error("Failed to parse order data");
        }
        return OrderAggregate(order);
    }
};

} // namespace order
} // namespace ecommerce

#endif // ORDER_AGGREGATE_H
```

### C++ Usage Example

```cpp
// order_example.cpp
#include "order_aggregate.h"
#include <iostream>

using namespace ecommerce::order;

int main() {
    try {
        // Create a new order aggregate
        OrderAggregate order("ORD-12345", "CUST-789");
        
        // Create money value object
        Money price;
        price.set_currency("USD");
        price.set_amount_in_cents(2999); // $29.99
        
        // Add items through the aggregate root
        order.addItem("PROD-001", "Wireless Mouse", 2, price);
        
        price.set_amount_in_cents(4999); // $49.99
        order.addItem("PROD-002", "Mechanical Keyboard", 1, price);
        
        // Confirm the order
        order.confirm();
        
        // Serialize for persistence
        std::string serialized = order.serialize();
        std::cout << "Order serialized, size: " << serialized.size() 
                  << " bytes" << std::endl;
        
        // Later... deserialize
        OrderAggregate loaded = OrderAggregate::deserialize(serialized);
        
        const Order& orderData = loaded.getOrder();
        std::cout << "Order ID: " << orderData.order_id() << std::endl;
        std::cout << "Status: " << OrderStatus_Name(orderData.status()) << std::endl;
        std::cout << "Items: " << orderData.items_size() << std::endl;
        std::cout << "Total: $" 
                  << (orderData.total_amount().amount_in_cents() / 100.0) 
                  << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### C Implementation (for embedded systems)

```c
// order_handler.h
#ifndef ORDER_HANDLER_H
#define ORDER_HANDLER_H

#include "order_context.pb-c.h"
#include <stdbool.h>

typedef struct {
    Ecommerce__Order__Order *order;
} OrderHandler;

// Initialize order handler
bool order_handler_init(OrderHandler *handler, 
                        const char *order_id,
                        const char *customer_id);

// Add item to order
bool order_handler_add_item(OrderHandler *handler,
                           const char *product_id,
                           const char *product_name,
                           int32_t quantity,
                           int64_t unit_price_cents,
                           const char *currency);

// Confirm order
bool order_handler_confirm(OrderHandler *handler);

// Validate invariants
bool order_handler_validate(const OrderHandler *handler);

// Cleanup
void order_handler_cleanup(OrderHandler *handler);

#endif // ORDER_HANDLER_H
```

```c
// order_handler.c
#include "order_handler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool order_handler_init(OrderHandler *handler,
                       const char *order_id,
                       const char *customer_id) {
    handler->order = malloc(sizeof(Ecommerce__Order__Order));
    if (!handler->order) return false;
    
    ecommerce__order__order__init(handler->order);
    
    handler->order->order_id = strdup(order_id);
    handler->order->customer_id = strdup(customer_id);
    handler->order->status = ECOMMERCE__ORDER__ORDER_STATUS__ORDER_STATUS_PENDING;
    
    // Allocate timestamps
    handler->order->created_at = malloc(sizeof(Google__Protobuf__Timestamp));
    google__protobuf__timestamp__init(handler->order->created_at);
    handler->order->created_at->seconds = time(NULL);
    
    handler->order->updated_at = malloc(sizeof(Google__Protobuf__Timestamp));
    google__protobuf__timestamp__init(handler->order->updated_at);
    handler->order->updated_at->seconds = time(NULL);
    
    return true;
}

static void recalculate_total(OrderHandler *handler) {
    int64_t total = 0;
    
    for (size_t i = 0; i < handler->order->n_items; i++) {
        total += handler->order->items[i]->line_total->amount_in_cents;
    }
    
    if (!handler->order->total_amount) {
        handler->order->total_amount = malloc(sizeof(Ecommerce__Order__Money));
        ecommerce__order__money__init(handler->order->total_amount);
    }
    
    handler->order->total_amount->amount_in_cents = total;
}

bool order_handler_add_item(OrderHandler *handler,
                           const char *product_id,
                           const char *product_name,
                           int32_t quantity,
                           int64_t unit_price_cents,
                           const char *currency) {
    if (handler->order->status != ECOMMERCE__ORDER__ORDER_STATUS__ORDER_STATUS_PENDING) {
        return false;
    }
    
    if (quantity <= 0) {
        return false;
    }
    
    // Allocate new order line
    size_t new_size = handler->order->n_items + 1;
    handler->order->items = realloc(handler->order->items, 
                                    new_size * sizeof(Ecommerce__Order__OrderLine*));
    
    Ecommerce__Order__OrderLine *line = malloc(sizeof(Ecommerce__Order__OrderLine));
    ecommerce__order__order_line__init(line);
    
    line->position = handler->order->n_items;
    line->product_id = strdup(product_id);
    line->product_name = strdup(product_name);
    line->quantity = quantity;
    
    // Set unit price
    line->unit_price = malloc(sizeof(Ecommerce__Order__Money));
    ecommerce__order__money__init(line->unit_price);
    line->unit_price->currency = strdup(currency);
    line->unit_price->amount_in_cents = unit_price_cents;
    
    // Calculate line total
    line->line_total = malloc(sizeof(Ecommerce__Order__Money));
    ecommerce__order__money__init(line->line_total);
    line->line_total->currency = strdup(currency);
    line->line_total->amount_in_cents = quantity * unit_price_cents;
    
    handler->order->items[handler->order->n_items] = line;
    handler->order->n_items = new_size;
    
    recalculate_total(handler);
    
    if (!handler->order->total_amount->currency) {
        handler->order->total_amount->currency = strdup(currency);
    }
    
    handler->order->updated_at->seconds = time(NULL);
    
    return true;
}

bool order_handler_confirm(OrderHandler *handler) {
    if (handler->order->status != ECOMMERCE__ORDER__ORDER_STATUS__ORDER_STATUS_PENDING) {
        return false;
    }
    
    if (handler->order->n_items == 0) {
        return false;
    }
    
    handler->order->status = ECOMMERCE__ORDER__ORDER_STATUS__ORDER_STATUS_CONFIRMED;
    handler->order->updated_at->seconds = time(NULL);
    
    return true;
}

bool order_handler_validate(const OrderHandler *handler) {
    if (handler->order->n_items == 0) {
        return false;
    }
    
    int64_t calculated_total = 0;
    for (size_t i = 0; i < handler->order->n_items; i++) {
        calculated_total += handler->order->items[i]->line_total->amount_in_cents;
    }
    
    return calculated_total == handler->order->total_amount->amount_in_cents;
}

void order_handler_cleanup(OrderHandler *handler) {
    if (handler->order) {
        ecommerce__order__order__free_unpacked(handler->order, NULL);
        handler->order = NULL;
    }
}
```

---

## Rust Implementation Examples

### Setting Up Rust Project

First, add dependencies to your `Cargo.toml`:

```toml
[package]
name = "ddd-protobuf-example"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
prost-types = "0.12"
chrono = "0.4"
thiserror = "1.0"

[build-dependencies]
prost-build = "0.12"
```

Create `build.rs`:

```rust
// build.rs
fn main() {
    prost_build::Config::new()
        .compile_protos(
            &["proto/order_context.proto", "proto/customer_context.proto"],
            &["proto/"]
        )
        .unwrap();
}
```

### Rust Implementation: Aggregate with Type Safety

```rust
// src/order/mod.rs
pub mod proto {
    include!(concat!(env!("OUT_DIR"), "/ecommerce.order.rs"));
}

use proto::{Order, OrderLine, OrderStatus, Money};
use chrono::Utc;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum OrderError {
    #[error("Order must have at least one item")]
    EmptyOrder,
    
    #[error("Cannot modify order in status: {0:?}")]
    InvalidStatus(OrderStatus),
    
    #[error("Invalid quantity: {0}")]
    InvalidQuantity(i32),
    
    #[error("Order total mismatch: expected {expected}, got {actual}")]
    TotalMismatch { expected: i64, actual: i64 },
    
    #[error("Invalid item position: {0}")]
    InvalidPosition(usize),
}

/// Aggregate Root for Order
/// Encapsulates business logic and enforces invariants
pub struct OrderAggregate {
    inner: Order,
}

impl OrderAggregate {
    /// Create a new order
    pub fn new(order_id: String, customer_id: String) -> Self {
        let now = Utc::now();
        
        let order = Order {
            order_id,
            customer_id,
            status: OrderStatus::Pending as i32,
            items: Vec::new(),
            total_amount: Some(Money {
                currency: String::new(),
                amount_in_cents: 0,
            }),
            created_at: Some(prost_types::Timestamp {
                seconds: now.timestamp(),
                nanos: 0,
            }),
            updated_at: Some(prost_types::Timestamp {
                seconds: now.timestamp(),
                nanos: 0,
            }),
        };
        
        Self { inner: order }
    }
    
    /// Create from existing protobuf (e.g., from database)
    pub fn from_proto(order: Order) -> Result<Self, OrderError> {
        let aggregate = Self { inner: order };
        aggregate.validate()?;
        Ok(aggregate)
    }
    
    /// Add item to order
    pub fn add_item(
        &mut self,
        product_id: String,
        product_name: String,
        quantity: i32,
        unit_price: Money,
    ) -> Result<(), OrderError> {
        // Check order status
        if self.inner.status != OrderStatus::Pending as i32 {
            return Err(OrderError::InvalidStatus(
                OrderStatus::from_i32(self.inner.status).unwrap_or(OrderStatus::Unspecified)
            ));
        }
        
        // Validate quantity
        if quantity <= 0 {
            return Err(OrderError::InvalidQuantity(quantity));
        }
        
        // Calculate line total
        let line_total_cents = quantity as i64 * unit_price.amount_in_cents;
        
        // Create order line
        let line = OrderLine {
            position: self.inner.items.len() as i32,
            product_id,
            product_name,
            quantity,
            unit_price: Some(unit_price.clone()),
            line_total: Some(Money {
                currency: unit_price.currency.clone(),
                amount_in_cents: line_total_cents,
            }),
        };
        
        self.inner.items.push(line);
        
        // Update total
        self.recalculate_total();
        
        // Set currency if first item
        if let Some(total) = &mut self.inner.total_amount {
            if total.currency.is_empty() {
                total.currency = unit_price.currency;
            }
        }
        
        self.update_timestamp();
        
        Ok(())
    }
    
    /// Remove item by position
    pub fn remove_item(&mut self, position: usize) -> Result<(), OrderError> {
        // Check order status
        if self.inner.status != OrderStatus::Pending as i32 {
            return Err(OrderError::InvalidStatus(
                OrderStatus::from_i32(self.inner.status).unwrap_or(OrderStatus::Unspecified)
            ));
        }
        
        // Validate position
        if position >= self.inner.items.len() {
            return Err(OrderError::InvalidPosition(position));
        }
        
        // Remove item
        self.inner.items.remove(position);
        
        // Reindex positions
        for (idx, item) in self.inner.items.iter_mut().enumerate() {
            item.position = idx as i32;
        }
        
        self.recalculate_total();
        self.update_timestamp();
        
        Ok(())
    }
    
    /// Confirm the order
    pub fn confirm(&mut self) -> Result<(), OrderError> {
        if self.inner.status != OrderStatus::Pending as i32 {
            return Err(OrderError::InvalidStatus(
                OrderStatus::from_i32(self.inner.status).unwrap_or(OrderStatus::Unspecified)
            ));
        }
        
        if self.inner.items.is_empty() {
            return Err(OrderError::EmptyOrder);
        }
        
        self.inner.status = OrderStatus::Confirmed as i32;
        self.update_timestamp();
        
        Ok(())
    }
    
    /// Cancel the order
    pub fn cancel(&mut self) -> Result<(), OrderError> {
        if self.inner.status == OrderStatus::Delivered as i32 {
            return Err(OrderError::InvalidStatus(OrderStatus::Delivered));
        }
        
        self.inner.status = OrderStatus::Cancelled as i32;
        self.update_timestamp();
        
        Ok(())
    }
    
    /// Get read-only reference to underlying protobuf
    pub fn as_proto(&self) -> &Order {
        &self.inner
    }
    
    /// Consume and return the underlying protobuf
    pub fn into_proto(self) -> Order {
        self.inner
    }
    
    /// Serialize to bytes
    pub fn to_bytes(&self) -> Result<Vec<u8>, OrderError> {
        self.validate()?;
        use prost::Message;
        let mut buf = Vec::new();
        self.inner.encode(&mut buf).map_err(|_| OrderError::EmptyOrder)?;
        Ok(buf)
    }
    
    /// Deserialize from bytes
    pub fn from_bytes(data: &[u8]) -> Result<Self, OrderError> {
        use prost::Message;
        let order = Order::decode(data).map_err(|_| OrderError::EmptyOrder)?;
        Self::from_proto(order)
    }
    
    // Private helper methods
    
    fn recalculate_total(&mut self) {
        let total: i64 = self.inner.items.iter()
            .filter_map(|item| item.line_total.as_ref())
            .map(|lt| lt.amount_in_cents)
            .sum();
        
        if let Some(total_amount) = &mut self.inner.total_amount {
            total_amount.amount_in_cents = total;
        }
    }
    
    fn update_timestamp(&mut self) {
        let now = Utc::now();
        self.inner.updated_at = Some(prost_types::Timestamp {
            seconds: now.timestamp(),
            nanos: 0,
        });
    }
    
    fn validate(&self) -> Result<(), OrderError> {
        if self.inner.items.is_empty() {
            return Err(OrderError::EmptyOrder);
        }
        
        // Verify total is correct
        let calculated: i64 = self.inner.items.iter()
            .filter_map(|item| item.line_total.as_ref())
            .map(|lt| lt.amount_in_cents)
            .sum();
        
        let actual = self.inner.total_amount
            .as_ref()
            .map(|t| t.amount_in_cents)
            .unwrap_or(0);
        
        if calculated != actual {
            return Err(OrderError::TotalMismatch {
                expected: calculated,
                actual,
            });
        }
        
        Ok(())
    }
}

// Implement Display for better debugging
impl std::fmt::Display for OrderAggregate {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Order[id={}, status={:?}, items={}, total={}]",
            self.inner.order_id,
            OrderStatus::from_i32(self.inner.status),
            self.inner.items.len(),
            self.inner.total_amount.as_ref()
                .map(|t| t.amount_in_cents)
                .unwrap_or(0)
        )
    }
}
```

### Rust Usage Example

```rust
// src/main.rs
mod order;

use order::{OrderAggregate, proto::Money};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create a new order aggregate
    let mut order = OrderAggregate::new(
        "ORD-12345".to_string(),
        "CUST-789".to_string(),
    );
    
    // Create money value objects
    let mouse_price = Money {
        currency: "USD".to_string(),
        amount_in_cents: 2999, // $29.99
    };
    
    let keyboard_price = Money {
        currency: "USD".to_string(),
        amount_in_cents: 4999, // $49.99
    };
    
    // Add items through the aggregate root
    order.add_item(
        "PROD-001".to_string(),
        "Wireless Mouse".to_string(),
        2,
        mouse_price,
    )?;
    
    order.add_item(
        "PROD-002".to_string(),
        "Mechanical Keyboard".to_string(),
        1,
        keyboard_price,
    )?;
    
    println!("Order created: {}", order);
    
    // Confirm the order
    order.confirm()?;
    
    // Serialize for persistence
    let bytes = order.to_bytes()?;
    println!("Serialized order: {} bytes", bytes.len());
    
    // Later... deserialize
    let loaded_order = OrderAggregate::from_bytes(&bytes)?;
    println!("Loaded order: {}", loaded_order);
    
    // Access read-only data
    let proto = loaded_order.as_proto();
    println!("Order ID: {}", proto.order_id);
    println!("Items: {}", proto.items.len());
    
    if let Some(total) = &proto.total_amount {
        println!("Total: {:.2} {}", 
                 total.amount_in_cents as f64 / 100.0,
                 total.currency);
    }
    
    Ok(())
}
```

### Advanced Pattern: Value Objects in Rust

```rust
// src/value_objects.rs
use std::fmt;

/// Money value object - immutable and compared by value
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MoneyVO {
    currency: String,
    amount_in_cents: i64,
}

impl MoneyVO {
    pub fn new(currency: String, amount_in_cents: i64) -> Result<Self, String> {
        if amount_in_cents < 0 {
            return Err("Amount cannot be negative".to_string());
        }
        
        if currency.is_empty() {
            return Err("Currency must be specified".to_string());
        }
        
        Ok(Self {
            currency,
            amount_in_cents,
        })
    }
    
    pub fn currency(&self) -> &str {
        &self.currency
    }
    
    pub fn amount_in_cents(&self) -> i64 {
        self.amount_in_cents
    }
    
    pub fn amount_in_dollars(&self) -> f64 {
        self.amount_in_cents as f64 / 100.0
    }
    
    /// Add two money values (must be same currency)
    pub fn add(&self, other: &MoneyVO) -> Result<MoneyVO, String> {
        if self.currency != other.currency {
            return Err(format!(
                "Cannot add different currencies: {} and {}",
                self.currency, other.currency
            ));
        }
        
        Ok(MoneyVO {
            currency: self.currency.clone(),
            amount_in_cents: self.amount_in_cents + other.amount_in_cents,
        })
    }
    
    /// Multiply by quantity
    pub fn multiply(&self, quantity: i32) -> Result<MoneyVO, String> {
        if quantity < 0 {
            return Err("Quantity cannot be negative".to_string());
        }
        
        Ok(MoneyVO {
            currency: self.currency.clone(),
            amount_in_cents: self.amount_in_cents * quantity as i64,
        })
    }
    
    /// Convert to protobuf Money
    pub fn to_proto(&self) -> order::proto::Money {
        order::proto::Money {
            currency: self.currency.clone(),
            amount_in_cents: self.amount_in_cents,
        }
    }
    
    /// Create from protobuf Money
    pub fn from_proto(money: &order::proto::Money) -> Result<Self, String> {
        Self::new(money.currency.clone(), money.amount_in_cents)
    }
}

impl fmt::Display for MoneyVO {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:.2} {}", self.amount_in_dollars(), self.currency)
    }
}

/// Address value object - immutable
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Address {
    street: String,
    city: String,
    state: String,
    postal_code: String,
    country: String,
}

impl Address {
    pub fn new(
        street: String,
        city: String,
        state: String,
        postal_code: String,
        country: String,
    ) -> Result<Self, String> {
        if street.is_empty() || city.is_empty() || country.is_empty() {
            return Err("Address fields cannot be empty".to_string());
        }
        
        Ok(Self {
            street,
            city,
            state,
            postal_code,
            country,
        })
    }
    
    pub fn street(&self) -> &str {
        &self.street
    }
    
    pub fn city(&self) -> &str {
        &self.city
    }
}
```

---

## Summary

**Domain-Driven Design with Protobuf** provides a powerful combination for building maintainable, scalable systems:

### Key Benefits:

1. **Bounded Contexts**: Protobuf packages naturally represent bounded contexts, ensuring clear separation between different domain models.

2. **Strongly-Typed Models**: Protobuf's type system enforces domain model integrity across language boundaries.

3. **Aggregate Roots**: Implementing aggregate roots around protobuf messages enforces invariants and business rules while maintaining data consistency.

4. **Value Objects**: Protobuf messages work well as immutable value objects when wrapped in appropriate language constructs.

5. **Language Interoperability**: The same protobuf definitions can be used across C/C++, Rust, Java, Go, Python, and other languages while maintaining consistent domain models.

6. **Version Evolution**: Protobuf's backward compatibility features align well with DDD's need for evolving domain models.

### Best Practices:

- **Encapsulate protobuf messages**: Never expose raw protobuf messages outside aggregate roots
- **Enforce invariants**: Use aggregate roots to validate all state changes
- **Separate concerns**: Keep domain logic in aggregate classes, not in protobuf messages
- **Use value objects**: Model concepts without identity as immutable value objects
- **Design boundaries carefully**: Ensure each bounded context has its own protobuf package
- **Version strategically**: Use protobuf field numbers and reserved fields to manage evolution

### Common Patterns:

1. **Aggregate Pattern**: Protobuf message as internal state, wrapped by aggregate class
2. **Repository Pattern**: Load/save aggregate roots using protobuf serialization
3. **Anti-Corruption Layer**: Transform between bounded contexts using separate protobuf definitions
4. **Published Language**: Share protobuf schemas as integration contracts between contexts

This approach combines the clarity of DDD tactical patterns with the efficiency and cross-platform capabilities of Protocol Buffers, resulting in robust, maintainable domain models.