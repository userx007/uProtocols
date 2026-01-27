# Event Sourcing Message Design with Protocol Buffers

## Overview

Event sourcing is an architectural pattern where application state is determined by replaying a sequence of immutable events rather than storing just the current state. Protocol Buffers (Protobuf) is an ideal serialization format for event sourcing due to its efficiency, type safety, schema evolution support, and immutability characteristics.

## Why Protocol Buffers for Event Sourcing?

### Key Benefits

1. **Immutability**: Protobuf-generated classes are immutable by design in most implementations, aligning perfectly with event sourcing principles
2. **Compact Binary Format**: Events are serialized efficiently, reducing storage costs and improving I/O performance
3. **Schema Evolution**: Built-in backward and forward compatibility through field numbering
4. **Type Safety**: Strong typing prevents data corruption
5. **Performance**: 4x faster than JSON for serialization/deserialization
6. **Language Neutral**: Events can be consumed across different services in different languages
7. **Versioning**: Field numbers provide explicit versioning of event schemas

### Trade-offs

- **Human Readability**: Binary format isn't directly readable (unlike JSON)
- **Tooling Required**: Requires protoc compiler and language-specific code generation
- **Learning Curve**: Schema definition syntax and concepts require initial learning

## Core Principles of Event Sourcing Message Design

### 1. Events Are Immutable Facts

Events represent something that has already happened in the system. Once persisted, they should never be modified.

```protobuf
syntax = "proto3";

package ecommerce.events;

import "google/protobuf/timestamp.proto";

// Good: Past tense naming indicates something happened
message OrderPlaced {
  string order_id = 1;
  string customer_id = 2;
  google.protobuf.Timestamp placed_at = 3;
  repeated LineItem items = 4;
  double total_amount = 5;
}

// Bad: Present/future tense suggests command, not event
message PlaceOrder {  // This should be a command, not an event
  string order_id = 1;
  // ...
}
```

### 2. Events Should Be Self-Contained

Each event should contain all necessary information to understand what happened without requiring external lookups.

```protobuf
message PaymentProcessed {
  string payment_id = 1;
  string order_id = 2;
  string customer_id = 3;  // Include for easy filtering/querying
  double amount = 4;
  string currency = 5;
  string payment_method = 6;
  google.protobuf.Timestamp processed_at = 7;
  string transaction_reference = 8;
}
```

### 3. Use Simple Types

Avoid complex nested value objects that might evolve differently than events themselves.

```protobuf
// Good: Simple types
message CustomerRegistered {
  string customer_id = 1;
  string email = 2;
  string full_name = 3;
  string country_code = 4;
}

// Avoid: Complex nested structures
message CustomerRegistered {
  string customer_id = 1;
  Address address = 2;  // Address definition might change over time
  ContactInfo contact = 3;  // Creates tight coupling
}
```

### 4. Include Event Metadata

Standard metadata helps with event processing, debugging, and auditing.

```protobuf
message EventMetadata {
  string event_id = 1;  // Unique identifier for this event
  string correlation_id = 2;  // Trace related events
  string causation_id = 3;  // The command/event that caused this
  google.protobuf.Timestamp timestamp = 4;
  string aggregate_id = 5;  // Which aggregate this belongs to
  int64 sequence_number = 6;  // Position in the event stream
  string event_type = 7;  // Fully qualified event type name
  string user_id = 8;  // Who triggered this event
}
```

### 5. Design for Schema Evolution

Reserve field numbers for future use and never reuse field numbers.

```protobuf
message OrderShipped {
  string order_id = 1;
  string tracking_number = 2;
  string carrier = 3;
  google.protobuf.Timestamp shipped_at = 4;
  
  // Reserved for deprecated fields
  reserved 5, 6;
  reserved "old_field_name", "deprecated_status";
  
  // New fields added in later versions
  string warehouse_id = 7;  // Added in v2
  ShippingAddress destination = 8;  // Added in v3
}
```

## Event Store Design Patterns

### Event Envelope Pattern

Wrap domain events in a standardized envelope for consistent handling.

```protobuf
syntax = "proto3";

package eventstore;

import "google/protobuf/any.proto";
import "google/protobuf/timestamp.proto";

message EventEnvelope {
  // Metadata
  string event_id = 1;
  string aggregate_type = 2;
  string aggregate_id = 3;
  int64 sequence_number = 4;
  google.protobuf.Timestamp timestamp = 5;
  string event_type = 6;
  
  // Correlation and causation
  string correlation_id = 7;
  string causation_id = 8;
  
  // The actual event data
  google.protobuf.Any event_data = 9;
  
  // Optional metadata
  map<string, string> metadata = 10;
}
```

### Snapshot Pattern

For long-lived aggregates, snapshots improve performance by avoiding replay of entire history.

```protobuf
message AggregateSnapshot {
  string aggregate_id = 1;
  string aggregate_type = 2;
  int64 sequence_number = 3;  // Last event sequence included in snapshot
  google.protobuf.Timestamp created_at = 4;
  google.protobuf.Any state = 5;  // Serialized aggregate state
}
```

## C/C++ Implementation Examples

### Basic Event Definition and Usage

**events.proto**
```protobuf
syntax = "proto3";

package banking.events;

import "google/protobuf/timestamp.proto";

message AccountCreated {
  string account_id = 1;
  string customer_id = 2;
  string account_type = 3;
  google.protobuf.Timestamp created_at = 4;
  double initial_balance = 5;
}

message MoneyDeposited {
  string account_id = 1;
  double amount = 2;
  string currency = 3;
  google.protobuf.Timestamp deposited_at = 4;
  string transaction_id = 5;
}

message MoneyWithdrawn {
  string account_id = 1;
  double amount = 2;
  string currency = 3;
  google.protobuf.Timestamp withdrawn_at = 4;
  string transaction_id = 5;
}
```

**event_store.cpp**
```cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include "events.pb.h"

class EventStore {
private:
    std::string filename_;
    std::vector<std::unique_ptr<google::protobuf::Message>> events_;

public:
    EventStore(const std::string& filename) : filename_(filename) {}
    
    // Append an immutable event to the store
    template<typename T>
    void appendEvent(const T& event) {
        // Events are immutable - we store a copy
        auto event_copy = std::make_unique<T>(event);
        events_.push_back(std::move(event_copy));
        
        // Persist to disk
        persistEvent(event);
    }
    
    // Persist event to file (append-only)
    template<typename T>
    void persistEvent(const T& event) {
        std::ofstream output(filename_, 
                           std::ios::binary | std::ios::app);
        
        if (!output) {
            std::cerr << "Failed to open event store file" << std::endl;
            return;
        }
        
        // Write event size first (for reading back)
        uint32_t size = event.ByteSizeLong();
        output.write(reinterpret_cast<const char*>(&size), sizeof(size));
        
        // Write serialized event
        if (!event.SerializeToOstream(&output)) {
            std::cerr << "Failed to serialize event" << std::endl;
        }
    }
    
    // Load all events from store
    template<typename T>
    std::vector<T> loadEvents() {
        std::vector<T> loaded_events;
        std::ifstream input(filename_, std::ios::binary);
        
        if (!input) {
            return loaded_events;  // File doesn't exist yet
        }
        
        while (input.peek() != EOF) {
            uint32_t size;
            input.read(reinterpret_cast<char*>(&size), sizeof(size));
            
            if (input.eof()) break;
            
            std::vector<char> buffer(size);
            input.read(buffer.data(), size);
            
            T event;
            if (event.ParseFromArray(buffer.data(), size)) {
                loaded_events.push_back(event);
            }
        }
        
        return loaded_events;
    }
};

// Aggregate that rebuilds state from events
class BankAccount {
private:
    std::string account_id_;
    double balance_;
    std::string currency_;
    
public:
    BankAccount() : balance_(0.0) {}
    
    // Apply events to rebuild state (Event Sourcing pattern)
    void apply(const banking::events::AccountCreated& event) {
        account_id_ = event.account_id();
        balance_ = event.initial_balance();
    }
    
    void apply(const banking::events::MoneyDeposited& event) {
        balance_ += event.amount();
        currency_ = event.currency();
    }
    
    void apply(const banking::events::MoneyWithdrawn& event) {
        balance_ -= event.amount();
        currency_ = event.currency();
    }
    
    double getBalance() const { return balance_; }
    std::string getAccountId() const { return account_id_; }
};

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    EventStore store("banking_events.dat");
    
    // Create and store events (immutable)
    banking::events::AccountCreated created;
    created.set_account_id("ACC-001");
    created.set_customer_id("CUST-123");
    created.set_account_type("CHECKING");
    created.set_initial_balance(1000.0);
    created.mutable_created_at()->set_seconds(time(nullptr));
    
    store.appendEvent(created);
    
    banking::events::MoneyDeposited deposit;
    deposit.set_account_id("ACC-001");
    deposit.set_amount(500.0);
    deposit.set_currency("USD");
    deposit.set_transaction_id("TXN-001");
    deposit.mutable_deposited_at()->set_seconds(time(nullptr));
    
    store.appendEvent(deposit);
    
    banking::events::MoneyWithdrawn withdrawal;
    withdrawal.set_account_id("ACC-001");
    withdrawal.set_amount(200.0);
    withdrawal.set_currency("USD");
    withdrawal.set_transaction_id("TXN-002");
    withdrawal.mutable_withdrawn_at()->set_seconds(time(nullptr));
    
    store.appendEvent(withdrawal);
    
    // Rebuild account state from events
    BankAccount account;
    
    auto created_events = store.loadEvents<banking::events::AccountCreated>();
    for (const auto& evt : created_events) {
        account.apply(evt);
    }
    
    auto deposit_events = store.loadEvents<banking::events::MoneyDeposited>();
    for (const auto& evt : deposit_events) {
        account.apply(evt);
    }
    
    auto withdrawal_events = store.loadEvents<banking::events::MoneyWithdrawn>();
    for (const auto& evt : withdrawal_events) {
        account.apply(evt);
    }
    
    std::cout << "Account " << account.getAccountId() 
              << " balance: $" << account.getBalance() << std::endl;
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

### Event Versioning Example in C++

```cpp
// events_v2.proto - Evolved schema
syntax = "proto3";

package banking.events.v2;

import "google/protobuf/timestamp.proto";

message MoneyDeposited {
  string account_id = 1;
  double amount = 2;
  string currency = 3;
  google.protobuf.Timestamp deposited_at = 4;
  string transaction_id = 5;
  
  // New field added in v2 - old events won't have this
  string deposit_channel = 6;  // "ATM", "ONLINE", "BRANCH"
  
  // Another new field
  optional string reference_note = 7;
}

// Handler that works with both v1 and v2 events
class DepositHandler {
public:
    void handle(const banking::events::v2::MoneyDeposited& event) {
        std::cout << "Processing deposit of " << event.amount() 
                  << " " << event.currency() << std::endl;
        
        // New field might not be present in old events
        if (!event.deposit_channel().empty()) {
            std::cout << "  Channel: " << event.deposit_channel() << std::endl;
        } else {
            std::cout << "  Channel: UNKNOWN (legacy event)" << std::endl;
        }
        
        if (event.has_reference_note()) {
            std::cout << "  Note: " << event.reference_note() << std::endl;
        }
    }
};
```

## Rust Implementation Examples

### Basic Event Store with Prost

**Cargo.toml**
```toml
[package]
name = "event-sourcing-example"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
prost-types = "0.12"
serde = { version = "1.0", features = ["derive"] }
uuid = { version = "1.0", features = ["v4"] }

[build-dependencies]
prost-build = "0.12"
```

**build.rs**
```rust
fn main() -> Result<(), Box<dyn std::error::Error>> {
    prost_build::compile_protos(&["proto/events.proto"], &["proto/"])?;
    Ok(())
}
```

**proto/events.proto**
```protobuf
syntax = "proto3";

package banking.events;

message AccountCreated {
  string account_id = 1;
  string customer_id = 2;
  string account_type = 3;
  int64 created_at = 4;
  double initial_balance = 5;
}

message MoneyDeposited {
  string account_id = 1;
  double amount = 2;
  string currency = 3;
  int64 deposited_at = 4;
  string transaction_id = 5;
}

message MoneyWithdrawn {
  string account_id = 1;
  double amount = 2;
  string currency = 3;
  int64 withdrawn_at = 4;
  string transaction_id = 5;
}

message EventEnvelope {
  string event_id = 1;
  string event_type = 2;
  int64 sequence_number = 3;
  int64 timestamp = 4;
  bytes event_data = 5;
}
```

**src/event_store.rs**
```rust
use prost::Message;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write, BufReader, BufWriter};
use std::path::Path;

// Include generated protobuf code
pub mod events {
    include!(concat!(env!("OUT_DIR"), "/banking.events.rs"));
}

use events::*;

pub struct EventStore {
    file_path: String,
}

impl EventStore {
    pub fn new(file_path: &str) -> Self {
        EventStore {
            file_path: file_path.to_string(),
        }
    }
    
    /// Append an event to the store (append-only, immutable)
    pub fn append_event<T: Message>(&self, event: &T) -> Result<(), Box<dyn std::error::Error>> {
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.file_path)?;
        
        let mut writer = BufWriter::new(file);
        
        // Serialize event to bytes
        let mut buffer = Vec::new();
        event.encode(&mut buffer)?;
        
        // Write length prefix (for reading back)
        let len = buffer.len() as u32;
        writer.write_all(&len.to_le_bytes())?;
        
        // Write event data
        writer.write_all(&buffer)?;
        writer.flush()?;
        
        Ok(())
    }
    
    /// Load all events of a specific type from the store
    pub fn load_events<T: Message + Default>(&self) -> Result<Vec<T>, Box<dyn std::error::Error>> {
        if !Path::new(&self.file_path).exists() {
            return Ok(Vec::new());
        }
        
        let file = File::open(&self.file_path)?;
        let mut reader = BufReader::new(file);
        let mut events = Vec::new();
        
        loop {
            // Read length prefix
            let mut len_bytes = [0u8; 4];
            match reader.read_exact(&mut len_bytes) {
                Ok(_) => {},
                Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => break,
                Err(e) => return Err(e.into()),
            }
            
            let len = u32::from_le_bytes(len_bytes) as usize;
            
            // Read event data
            let mut buffer = vec![0u8; len];
            reader.read_exact(&mut buffer)?;
            
            // Decode event
            if let Ok(event) = T::decode(&buffer[..]) {
                events.push(event);
            }
        }
        
        Ok(events)
    }
}

/// Aggregate that rebuilds state from events
#[derive(Debug, Default)]
pub struct BankAccount {
    account_id: String,
    balance: f64,
    currency: String,
}

impl BankAccount {
    /// Apply AccountCreated event
    pub fn apply_account_created(&mut self, event: &AccountCreated) {
        self.account_id = event.account_id.clone();
        self.balance = event.initial_balance;
    }
    
    /// Apply MoneyDeposited event
    pub fn apply_money_deposited(&mut self, event: &MoneyDeposited) {
        self.balance += event.amount;
        self.currency = event.currency.clone();
    }
    
    /// Apply MoneyWithdrawn event
    pub fn apply_money_withdrawn(&mut self, event: &MoneyWithdrawn) {
        self.balance -= event.amount;
        self.currency = event.currency.clone();
    }
    
    pub fn balance(&self) -> f64 {
        self.balance
    }
    
    pub fn account_id(&self) -> &str {
        &self.account_id
    }
}
```

**src/main.rs**
```rust
mod event_store;

use event_store::*;
use std::time::{SystemTime, UNIX_EPOCH};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let store = EventStore::new("banking_events.dat");
    
    // Create immutable events
    let created = events::AccountCreated {
        account_id: "ACC-001".to_string(),
        customer_id: "CUST-123".to_string(),
        account_type: "CHECKING".to_string(),
        created_at: SystemTime::now()
            .duration_since(UNIX_EPOCH)?
            .as_secs() as i64,
        initial_balance: 1000.0,
    };
    
    store.append_event(&created)?;
    
    let deposit = events::MoneyDeposited {
        account_id: "ACC-001".to_string(),
        amount: 500.0,
        currency: "USD".to_string(),
        deposited_at: SystemTime::now()
            .duration_since(UNIX_EPOCH)?
            .as_secs() as i64,
        transaction_id: "TXN-001".to_string(),
    };
    
    store.append_event(&deposit)?;
    
    let withdrawal = events::MoneyWithdrawn {
        account_id: "ACC-001".to_string(),
        amount: 200.0,
        currency: "USD".to_string(),
        withdrawn_at: SystemTime::now()
            .duration_since(UNIX_EPOCH)?
            .as_secs() as i64,
        transaction_id: "TXN-002".to_string(),
    };
    
    store.append_event(&withdrawal)?;
    
    // Rebuild account state from events
    let mut account = BankAccount::default();
    
    for event in store.load_events::<events::AccountCreated>()? {
        account.apply_account_created(&event);
    }
    
    for event in store.load_events::<events::MoneyDeposited>()? {
        account.apply_money_deposited(&event);
    }
    
    for event in store.load_events::<events::MoneyWithdrawn>()? {
        account.apply_money_withdrawn(&event);
    }
    
    println!(
        "Account {} balance: ${:.2}",
        account.account_id(),
        account.balance()
    );
    
    Ok(())
}
```

### Advanced Event Envelope Pattern in Rust

```rust
use prost::Message;
use uuid::Uuid;

pub mod envelope {
    include!(concat!(env!("OUT_DIR"), "/eventstore.rs"));
}

/// Helper to create event envelopes
pub struct EventEnvelopeBuilder {
    aggregate_type: String,
    aggregate_id: String,
    correlation_id: Option<String>,
}

impl EventEnvelopeBuilder {
    pub fn new(aggregate_type: &str, aggregate_id: &str) -> Self {
        Self {
            aggregate_type: aggregate_type.to_string(),
            aggregate_id: aggregate_id.to_string(),
            correlation_id: None,
        }
    }
    
    pub fn with_correlation_id(mut self, id: &str) -> Self {
        self.correlation_id = Some(id.to_string());
        self
    }
    
    pub fn build<T: Message>(
        &self,
        event: T,
        sequence_number: i64,
    ) -> Result<envelope::EventEnvelope, Box<dyn std::error::Error>> {
        let mut event_data = Vec::new();
        event.encode(&mut event_data)?;
        
        let envelope = envelope::EventEnvelope {
            event_id: Uuid::new_v4().to_string(),
            aggregate_type: self.aggregate_type.clone(),
            aggregate_id: self.aggregate_id.clone(),
            sequence_number,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)?
                .as_secs() as i64,
            event_type: std::any::type_name::<T>().to_string(),
            correlation_id: self.correlation_id.clone().unwrap_or_default(),
            causation_id: String::new(),
            event_data,
            metadata: std::collections::HashMap::new(),
        };
        
        Ok(envelope)
    }
}

// Usage example
fn create_and_store_event() -> Result<(), Box<dyn std::error::Error>> {
    let store = EventStore::new("events.dat");
    
    let deposit = events::MoneyDeposited {
        account_id: "ACC-001".to_string(),
        amount: 100.0,
        currency: "USD".to_string(),
        deposited_at: SystemTime::now()
            .duration_since(UNIX_EPOCH)?
            .as_secs() as i64,
        transaction_id: "TXN-123".to_string(),
    };
    
    let envelope = EventEnvelopeBuilder::new("BankAccount", "ACC-001")
        .with_correlation_id("CORR-456")
        .build(deposit, 5)?;
    
    store.append_event(&envelope)?;
    
    Ok(())
}
```

## Best Practices Summary

### DO:
1. ✅ Use past-tense names for events (OrderPlaced, PaymentProcessed)
2. ✅ Include all necessary context in each event
3. ✅ Use simple, primitive types when possible
4. ✅ Add metadata fields (event_id, timestamp, correlation_id)
5. ✅ Reserve field numbers for deprecated fields
6. ✅ Design for schema evolution from the start
7. ✅ Use optional fields for new additions
8. ✅ Include version information in event metadata
9. ✅ Implement upcasting for old event versions
10. ✅ Use envelopes for consistent event handling

### DON'T:
1. ❌ Modify existing events (they're immutable facts)
2. ❌ Reuse field numbers
3. ❌ Delete old event definitions
4. ❌ Use complex nested value objects
5. ❌ Forget to handle missing fields in old events
6. ❌ Use present/future tense for event names
7. ❌ Store commands as events
8. ❌ Depend on external state in events
9. ❌ Make breaking changes to existing fields
10. ❌ Forget to version your schemas

## Schema Evolution Strategies

### Adding New Fields
```protobuf
// Version 1
message OrderPlaced {
  string order_id = 1;
  double total = 2;
}

// Version 2 - backward compatible
message OrderPlaced {
  string order_id = 1;
  double total = 2;
  optional string promo_code = 3;  // New optional field
}
```

### Deprecating Fields
```protobuf
message OrderPlaced {
  string order_id = 1;
  double total = 2;
  
  reserved 3;  // Deprecated field, never reuse
  reserved "old_field";
  
  string new_field = 4;  // Replacement field
}
```

### Handling Type Changes (Upcasting Pattern)
```rust
// In application code
fn upcast_old_event_to_new(old_data: &[u8]) -> NewEventType {
    let old_event = OldEventType::decode(old_data).unwrap();
    
    NewEventType {
        // Map old fields to new structure
        id: old_event.id,
        amount: old_event.amount,
        // Provide defaults for new fields
        currency: "USD".to_string(),  // Default for old events
    }
}
```

## Performance Considerations

1. **Use Snapshots**: For aggregates with long histories, store periodic snapshots
2. **Batch Events**: Write events in batches when possible
3. **Stream Processing**: Use protobuf's streaming capabilities for large event streams
4. **Compression**: Consider compressing event batches for storage
5. **Indexing**: Index event metadata (aggregate_id, event_type) for fast retrieval

## Summary

Protocol Buffers provides an excellent foundation for event sourcing architectures:

- **Immutability**: Events are naturally immutable once created
- **Performance**: Compact binary format with fast serialization
- **Evolution**: Built-in support for backward/forward compatibility
- **Type Safety**: Strong typing prevents many runtime errors
- **Cross-Language**: Events can be consumed by services in different languages

By following the principles and patterns outlined in this guide, you can build robust, scalable event-sourced systems that handle schema evolution gracefully while maintaining data integrity and performance.

The key is to design events as immutable historical facts, use simple types, plan for evolution from the start, and leverage Protocol Buffers' strengths in efficiency and schema management.