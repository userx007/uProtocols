# Thrift to Protobuf Migration Guide

## Overview

Migrating from Apache Thrift to Protocol Buffers (Protobuf) is a common task for organizations seeking better performance, broader language support, or improved tooling. This guide provides strategies for migrating with minimal service disruption, including dual-serialization approaches, compatibility layers, and incremental migration patterns.

## Table of Contents

1. [Key Differences Between Thrift and Protobuf](#key-differences)
2. [Migration Strategies](#migration-strategies)
3. [Schema Conversion](#schema-conversion)
4. [C/C++ Implementation Examples](#cc-examples)
5. [Rust Implementation Examples](#rust-examples)
6. [Best Practices](#best-practices)

---

## Key Differences Between Thrift and Protobuf

### Serialization Format
- **Thrift**: Multiple protocols (Binary, Compact, JSON)
- **Protobuf**: Single wire format (more compact and efficient)

### Type System
- **Thrift**: Rich type system with sets, maps, and exceptions as first-class types
- **Protobuf**: Simpler type system, uses repeated fields for lists

### RPC Support
- **Thrift**: Built-in RPC framework
- **Protobuf**: Requires gRPC or custom RPC implementation

### Field Numbering
- **Thrift**: Optional field IDs
- **Protobuf**: Mandatory field numbers

---

## Migration Strategies

### 1. **Big Bang Migration**
Replace entire service at once (high risk, minimal compatibility period)

### 2. **Dual Write Migration**
- Services write to both formats
- Consumers gradually migrate to Protobuf
- Remove Thrift support after full migration

### 3. **Adapter Pattern**
- Create translation layer between formats
- Allows gradual service-by-service migration

### 4. **Shadowing/Dark Launching**
- Send both Thrift and Protobuf requests
- Compare results for validation
- Switch over after confidence is established

---

## Schema Conversion

### Thrift Schema Example

```thrift
namespace cpp example
namespace rs example

enum Status {
    ACTIVE = 1,
    INACTIVE = 2,
    PENDING = 3
}

struct Address {
    1: required string street,
    2: required string city,
    3: required string state,
    4: required string zip_code
}

struct User {
    1: required i64 user_id,
    2: required string username,
    3: optional string email,
    4: required Status status,
    5: optional Address address,
    6: list<string> tags,
    7: map<string, string> metadata
}

service UserService {
    User getUser(1: i64 user_id),
    bool updateUser(1: User user),
    list<User> listUsers(1: i32 limit, 2: i32 offset)
}
```

### Equivalent Protobuf Schema

```protobuf
syntax = "proto3";

package example;

enum Status {
    STATUS_UNSPECIFIED = 0;
    ACTIVE = 1;
    INACTIVE = 2;
    PENDING = 3;
}

message Address {
    string street = 1;
    string city = 2;
    string state = 3;
    string zip_code = 4;
}

message User {
    int64 user_id = 1;
    string username = 2;
    string email = 3;
    Status status = 4;
    Address address = 5;
    repeated string tags = 6;
    map<string, string> metadata = 7;
}

message GetUserRequest {
    int64 user_id = 1;
}

message UpdateUserRequest {
    User user = 1;
}

message UpdateUserResponse {
    bool success = 1;
}

message ListUsersRequest {
    int32 limit = 1;
    int32 offset = 2;
}

message ListUsersResponse {
    repeated User users = 1;
}

service UserService {
    rpc GetUser(GetUserRequest) returns (User);
    rpc UpdateUser(UpdateUserRequest) returns (UpdateUserResponse);
    rpc ListUsers(ListUsersRequest) returns (ListUsersResponse);
}
```

### Key Conversion Notes

1. **Required/Optional**: Proto3 doesn't have required fields (all fields are optional)
2. **Enums**: Proto3 requires first enum value to be 0 (add UNSPECIFIED)
3. **Lists**: `list<T>` becomes `repeated T`
4. **Maps**: Direct mapping supported
5. **Services**: Methods need explicit request/response messages

---

## C/C++ Implementation Examples

### Migration Adapter Pattern (C++)

```cpp
// migration_adapter.h
#ifndef MIGRATION_ADAPTER_H
#define MIGRATION_ADAPTER_H

#include <memory>
#include <string>
#include "user.pb.h"  // Protobuf generated
#include "user_types.h"  // Thrift generated

namespace migration {

class ThriftToProtoAdapter {
public:
    // Convert Thrift User to Protobuf User
    static example::User ConvertUser(const thrift::User& thrift_user) {
        example::User proto_user;
        
        proto_user.set_user_id(thrift_user.user_id);
        proto_user.set_username(thrift_user.username);
        
        if (thrift_user.__isset.email) {
            proto_user.set_email(thrift_user.email);
        }
        
        proto_user.set_status(ConvertStatus(thrift_user.status));
        
        if (thrift_user.__isset.address) {
            auto* addr = proto_user.mutable_address();
            addr->set_street(thrift_user.address.street);
            addr->set_city(thrift_user.address.city);
            addr->set_state(thrift_user.address.state);
            addr->set_zip_code(thrift_user.address.zip_code);
        }
        
        for (const auto& tag : thrift_user.tags) {
            proto_user.add_tags(tag);
        }
        
        for (const auto& pair : thrift_user.metadata) {
            (*proto_user.mutable_metadata())[pair.first] = pair.second;
        }
        
        return proto_user;
    }
    
    // Convert Protobuf User to Thrift User
    static thrift::User ConvertUser(const example::User& proto_user) {
        thrift::User thrift_user;
        
        thrift_user.user_id = proto_user.user_id();
        thrift_user.username = proto_user.username();
        
        if (!proto_user.email().empty()) {
            thrift_user.email = proto_user.email();
            thrift_user.__isset.email = true;
        }
        
        thrift_user.status = ConvertStatus(proto_user.status());
        
        if (proto_user.has_address()) {
            thrift_user.address.street = proto_user.address().street();
            thrift_user.address.city = proto_user.address().city();
            thrift_user.address.state = proto_user.address().state();
            thrift_user.address.zip_code = proto_user.address().zip_code();
            thrift_user.__isset.address = true;
        }
        
        for (const auto& tag : proto_user.tags()) {
            thrift_user.tags.push_back(tag);
        }
        
        for (const auto& pair : proto_user.metadata()) {
            thrift_user.metadata[pair.first] = pair.second;
        }
        
        return thrift_user;
    }

private:
    static example::Status ConvertStatus(thrift::Status::type status) {
        switch (status) {
            case thrift::Status::ACTIVE: return example::ACTIVE;
            case thrift::Status::INACTIVE: return example::INACTIVE;
            case thrift::Status::PENDING: return example::PENDING;
            default: return example::STATUS_UNSPECIFIED;
        }
    }
    
    static thrift::Status::type ConvertStatus(example::Status status) {
        switch (status) {
            case example::ACTIVE: return thrift::Status::ACTIVE;
            case example::INACTIVE: return thrift::Status::INACTIVE;
            case example::PENDING: return thrift::Status::PENDING;
            default: return thrift::Status::ACTIVE;
        }
    }
};

} // namespace migration

#endif // MIGRATION_ADAPTER_H
```

### Dual Serialization Service (C++)

```cpp
// dual_service.cpp
#include <iostream>
#include <vector>
#include "migration_adapter.h"
#include "user.pb.h"
#include <google/protobuf/util/json_util.h>

class DualFormatUserService {
private:
    bool use_protobuf_;
    
public:
    DualFormatUserService(bool use_protobuf = false) 
        : use_protobuf_(use_protobuf) {}
    
    // Store user in both formats during migration
    void StoreUser(const example::User& proto_user) {
        // Always write to primary storage (Protobuf)
        std::string proto_data;
        proto_user.SerializeToString(&proto_data);
        WriteToPrimaryStorage(proto_data);
        
        // During migration, also write Thrift format
        if (!use_protobuf_) {
            auto thrift_user = migration::ThriftToProtoAdapter::ConvertUser(proto_user);
            WriteToLegacyStorage(thrift_user);
        }
    }
    
    // Retrieve user with fallback mechanism
    example::User GetUser(int64_t user_id) {
        if (use_protobuf_) {
            // Try Protobuf first
            std::string proto_data = ReadFromPrimaryStorage(user_id);
            if (!proto_data.empty()) {
                example::User user;
                user.ParseFromString(proto_data);
                return user;
            }
        }
        
        // Fallback to Thrift
        auto thrift_user = ReadFromLegacyStorage(user_id);
        return migration::ThriftToProtoAdapter::ConvertUser(thrift_user);
    }
    
    void SetUseProtobuf(bool use_protobuf) {
        use_protobuf_ = use_protobuf;
    }

private:
    void WriteToPrimaryStorage(const std::string& data) {
        // Implementation: write to database/file
        std::cout << "Writing to Protobuf storage: " << data.size() << " bytes\n";
    }
    
    void WriteToLegacyStorage(const thrift::User& user) {
        // Implementation: write to Thrift storage
        std::cout << "Writing to Thrift storage (legacy)\n";
    }
    
    std::string ReadFromPrimaryStorage(int64_t user_id) {
        // Implementation: read from Protobuf storage
        return "";
    }
    
    thrift::User ReadFromLegacyStorage(int64_t user_id) {
        // Implementation: read from Thrift storage
        thrift::User user;
        return user;
    }
};

// Example usage
int main() {
    DualFormatUserService service(false);  // Start with dual-write mode
    
    // Create a user
    example::User user;
    user.set_user_id(12345);
    user.set_username("john_doe");
    user.set_email("john@example.com");
    user.set_status(example::ACTIVE);
    
    // Store in both formats
    service.StoreUser(user);
    
    // After migration complete, switch to Protobuf-only
    service.SetUseProtobuf(true);
    
    // Retrieve user
    auto retrieved = service.GetUser(12345);
    std::cout << "Retrieved user: " << retrieved.username() << std::endl;
    
    return 0;
}
```

### Performance Comparison Tool (C++)

```cpp
// performance_comparison.cpp
#include <chrono>
#include <iostream>
#include <vector>
#include "user.pb.h"
#include "user_types.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

class PerformanceComparison {
public:
    static void CompareSerializationPerformance() {
        const int iterations = 100000;
        
        // Create test data
        example::User proto_user = CreateProtoUser();
        thrift::User thrift_user = CreateThriftUser();
        
        // Protobuf serialization benchmark
        auto proto_start = std::chrono::high_resolution_clock::now();
        size_t proto_total_size = 0;
        for (int i = 0; i < iterations; ++i) {
            std::string data;
            proto_user.SerializeToString(&data);
            proto_total_size += data.size();
        }
        auto proto_end = std::chrono::high_resolution_clock::now();
        auto proto_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            proto_end - proto_start).count();
        
        // Thrift serialization benchmark
        auto thrift_start = std::chrono::high_resolution_clock::now();
        size_t thrift_total_size = 0;
        for (int i = 0; i < iterations; ++i) {
            auto buffer = std::make_shared<TMemoryBuffer>();
            auto protocol = std::make_shared<TBinaryProtocol>(buffer);
            thrift_user.write(protocol.get());
            thrift_total_size += buffer->available_read();
        }
        auto thrift_end = std::chrono::high_resolution_clock::now();
        auto thrift_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            thrift_end - thrift_start).count();
        
        // Results
        std::cout << "=== Serialization Performance Comparison ===\n";
        std::cout << "Iterations: " << iterations << "\n\n";
        
        std::cout << "Protobuf:\n";
        std::cout << "  Time: " << proto_duration << " ms\n";
        std::cout << "  Avg size: " << proto_total_size / iterations << " bytes\n";
        std::cout << "  Throughput: " << (iterations * 1000.0 / proto_duration) << " ops/sec\n\n";
        
        std::cout << "Thrift:\n";
        std::cout << "  Time: " << thrift_duration << " ms\n";
        std::cout << "  Avg size: " << thrift_total_size / iterations << " bytes\n";
        std::cout << "  Throughput: " << (iterations * 1000.0 / thrift_duration) << " ops/sec\n\n";
        
        std::cout << "Speedup: " << (double)thrift_duration / proto_duration << "x\n";
        std::cout << "Size reduction: " << 
            (1.0 - (double)proto_total_size / thrift_total_size) * 100 << "%\n";
    }

private:
    static example::User CreateProtoUser() {
        example::User user;
        user.set_user_id(12345);
        user.set_username("test_user");
        user.set_email("test@example.com");
        user.set_status(example::ACTIVE);
        
        auto* addr = user.mutable_address();
        addr->set_street("123 Main St");
        addr->set_city("Springfield");
        addr->set_state("IL");
        addr->set_zip_code("62701");
        
        user.add_tags("premium");
        user.add_tags("verified");
        (*user.mutable_metadata())["created"] = "2024-01-01";
        
        return user;
    }
    
    static thrift::User CreateThriftUser() {
        thrift::User user;
        user.user_id = 12345;
        user.username = "test_user";
        user.email = "test@example.com";
        user.__isset.email = true;
        user.status = thrift::Status::ACTIVE;
        
        user.address.street = "123 Main St";
        user.address.city = "Springfield";
        user.address.state = "IL";
        user.address.zip_code = "62701";
        user.__isset.address = true;
        
        user.tags.push_back("premium");
        user.tags.push_back("verified");
        user.metadata["created"] = "2024-01-01";
        
        return user;
    }
};

int main() {
    PerformanceComparison::CompareSerializationPerformance();
    return 0;
}
```

---

## Rust Implementation Examples

### Migration Adapter (Rust)

```rust
// migration_adapter.rs
use std::collections::HashMap;

// Assume these are generated from .proto and .thrift files
mod proto {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

mod thrift {
    // Thrift generated types would be here
    pub struct User {
        pub user_id: i64,
        pub username: String,
        pub email: Option<String>,
        pub status: Status,
        pub address: Option<Address>,
        pub tags: Vec<String>,
        pub metadata: HashMap<String, String>,
    }
    
    pub struct Address {
        pub street: String,
        pub city: String,
        pub state: String,
        pub zip_code: String,
    }
    
    #[derive(Debug, Clone, Copy)]
    pub enum Status {
        Active = 1,
        Inactive = 2,
        Pending = 3,
    }
}

pub struct ThriftToProtoAdapter;

impl ThriftToProtoAdapter {
    /// Convert Thrift User to Protobuf User
    pub fn thrift_to_proto(thrift_user: &thrift::User) -> proto::User {
        let mut proto_user = proto::User::default();
        
        proto_user.user_id = thrift_user.user_id;
        proto_user.username = thrift_user.username.clone();
        proto_user.email = thrift_user.email.clone().unwrap_or_default();
        proto_user.status = Self::convert_status_to_proto(thrift_user.status) as i32;
        
        if let Some(ref addr) = thrift_user.address {
            proto_user.address = Some(proto::Address {
                street: addr.street.clone(),
                city: addr.city.clone(),
                state: addr.state.clone(),
                zip_code: addr.zip_code.clone(),
            });
        }
        
        proto_user.tags = thrift_user.tags.clone();
        proto_user.metadata = thrift_user.metadata.clone();
        
        proto_user
    }
    
    /// Convert Protobuf User to Thrift User
    pub fn proto_to_thrift(proto_user: &proto::User) -> thrift::User {
        thrift::User {
            user_id: proto_user.user_id,
            username: proto_user.username.clone(),
            email: if proto_user.email.is_empty() {
                None
            } else {
                Some(proto_user.email.clone())
            },
            status: Self::convert_status_to_thrift(proto_user.status),
            address: proto_user.address.as_ref().map(|addr| thrift::Address {
                street: addr.street.clone(),
                city: addr.city.clone(),
                state: addr.state.clone(),
                zip_code: addr.zip_code.clone(),
            }),
            tags: proto_user.tags.clone(),
            metadata: proto_user.metadata.clone(),
        }
    }
    
    fn convert_status_to_proto(status: thrift::Status) -> proto::Status {
        match status {
            thrift::Status::Active => proto::Status::Active,
            thrift::Status::Inactive => proto::Status::Inactive,
            thrift::Status::Pending => proto::Status::Pending,
        }
    }
    
    fn convert_status_to_thrift(status: i32) -> thrift::Status {
        match status {
            1 => thrift::Status::Active,
            2 => thrift::Status::Inactive,
            3 => thrift::Status::Pending,
            _ => thrift::Status::Active,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_thrift_to_proto_conversion() {
        let thrift_user = thrift::User {
            user_id: 12345,
            username: "john_doe".to_string(),
            email: Some("john@example.com".to_string()),
            status: thrift::Status::Active,
            address: Some(thrift::Address {
                street: "123 Main St".to_string(),
                city: "Springfield".to_string(),
                state: "IL".to_string(),
                zip_code: "62701".to_string(),
            }),
            tags: vec!["premium".to_string(), "verified".to_string()],
            metadata: HashMap::new(),
        };
        
        let proto_user = ThriftToProtoAdapter::thrift_to_proto(&thrift_user);
        
        assert_eq!(proto_user.user_id, 12345);
        assert_eq!(proto_user.username, "john_doe");
        assert_eq!(proto_user.email, "john@example.com");
        assert!(proto_user.address.is_some());
    }
    
    #[test]
    fn test_round_trip_conversion() {
        let original = create_test_thrift_user();
        let proto = ThriftToProtoAdapter::thrift_to_proto(&original);
        let back_to_thrift = ThriftToProtoAdapter::proto_to_thrift(&proto);
        
        assert_eq!(original.user_id, back_to_thrift.user_id);
        assert_eq!(original.username, back_to_thrift.username);
    }
    
    fn create_test_thrift_user() -> thrift::User {
        thrift::User {
            user_id: 12345,
            username: "test".to_string(),
            email: Some("test@example.com".to_string()),
            status: thrift::Status::Active,
            address: None,
            tags: vec![],
            metadata: HashMap::new(),
        }
    }
}
```

### Dual Write Service (Rust)

```rust
// dual_write_service.rs
use anyhow::{Result, Context};
use prost::Message;
use std::sync::Arc;
use tokio::sync::RwLock;

mod proto {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

mod thrift {
    // Thrift types from previous example
}

use crate::migration_adapter::ThriftToProtoAdapter;

#[derive(Clone)]
pub struct DualWriteUserService {
    use_protobuf: Arc<RwLock<bool>>,
    protobuf_store: Arc<ProtobufStore>,
    thrift_store: Arc<ThriftStore>,
}

impl DualWriteUserService {
    pub fn new(use_protobuf: bool) -> Self {
        Self {
            use_protobuf: Arc::new(RwLock::new(use_protobuf)),
            protobuf_store: Arc::new(ProtobufStore::new()),
            thrift_store: Arc::new(ThriftStore::new()),
        }
    }
    
    /// Store user in both formats during migration phase
    pub async fn store_user(&self, user: &proto::User) -> Result<()> {
        // Always write to Protobuf (new primary)
        self.protobuf_store.write(user).await
            .context("Failed to write to Protobuf store")?;
        
        // During migration, also write to Thrift
        let use_protobuf = *self.use_protobuf.read().await;
        if !use_protobuf {
            let thrift_user = ThriftToProtoAdapter::proto_to_thrift(user);
            self.thrift_store.write(&thrift_user).await
                .context("Failed to write to Thrift store (legacy)")?;
        }
        
        Ok(())
    }
    
    /// Retrieve user with fallback mechanism
    pub async fn get_user(&self, user_id: i64) -> Result<proto::User> {
        let use_protobuf = *self.use_protobuf.read().await;
        
        if use_protobuf {
            // Try Protobuf first
            if let Ok(user) = self.protobuf_store.read(user_id).await {
                return Ok(user);
            }
        }
        
        // Fallback to Thrift
        let thrift_user = self.thrift_store.read(user_id).await
            .context("Failed to read from both stores")?;
        
        Ok(ThriftToProtoAdapter::thrift_to_proto(&thrift_user))
    }
    
    /// Update users in batches
    pub async fn batch_update(&self, users: Vec<proto::User>) -> Result<usize> {
        let mut success_count = 0;
        
        for user in users {
            if self.store_user(&user).await.is_ok() {
                success_count += 1;
            }
        }
        
        Ok(success_count)
    }
    
    /// Switch to Protobuf-only mode
    pub async fn enable_protobuf_mode(&self) {
        *self.use_protobuf.write().await = true;
        println!("Switched to Protobuf-only mode");
    }
    
    /// Migration statistics
    pub async fn get_migration_stats(&self) -> MigrationStats {
        MigrationStats {
            protobuf_count: self.protobuf_store.count().await,
            thrift_count: self.thrift_store.count().await,
            using_protobuf: *self.use_protobuf.read().await,
        }
    }
}

pub struct MigrationStats {
    pub protobuf_count: usize,
    pub thrift_count: usize,
    pub using_protobuf: bool,
}

// Mock storage implementations
struct ProtobufStore {
    // In real implementation, this would be a database connection
    data: Arc<RwLock<std::collections::HashMap<i64, Vec<u8>>>>,
}

impl ProtobufStore {
    fn new() -> Self {
        Self {
            data: Arc::new(RwLock::new(std::collections::HashMap::new())),
        }
    }
    
    async fn write(&self, user: &proto::User) -> Result<()> {
        let mut buf = Vec::new();
        user.encode(&mut buf)?;
        
        let mut data = self.data.write().await;
        data.insert(user.user_id, buf);
        
        println!("Wrote user {} to Protobuf store ({} bytes)", 
                 user.user_id, buf.len());
        Ok(())
    }
    
    async fn read(&self, user_id: i64) -> Result<proto::User> {
        let data = self.data.read().await;
        let buf = data.get(&user_id)
            .ok_or_else(|| anyhow::anyhow!("User not found"))?;
        
        Ok(proto::User::decode(&buf[..])?)
    }
    
    async fn count(&self) -> usize {
        self.data.read().await.len()
    }
}

struct ThriftStore {
    data: Arc<RwLock<std::collections::HashMap<i64, thrift::User>>>,
}

impl ThriftStore {
    fn new() -> Self {
        Self {
            data: Arc::new(RwLock::new(std::collections::HashMap::new())),
        }
    }
    
    async fn write(&self, user: &thrift::User) -> Result<()> {
        let mut data = self.data.write().await;
        data.insert(user.user_id, user.clone());
        
        println!("Wrote user {} to Thrift store (legacy)", user.user_id);
        Ok(())
    }
    
    async fn read(&self, user_id: i64) -> Result<thrift::User> {
        let data = self.data.read().await;
        data.get(&user_id)
            .cloned()
            .ok_or_else(|| anyhow::anyhow!("User not found"))
    }
    
    async fn count(&self) -> usize {
        self.data.read().await.len()
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<()> {
    let service = DualWriteUserService::new(false);
    
    // Create test user
    let user = proto::User {
        user_id: 12345,
        username: "john_doe".to_string(),
        email: "john@example.com".to_string(),
        status: proto::Status::Active as i32,
        address: Some(proto::Address {
            street: "123 Main St".to_string(),
            city: "Springfield".to_string(),
            state: "IL".to_string(),
            zip_code: "62701".to_string(),
        }),
        tags: vec!["premium".to_string()],
        metadata: Default::default(),
    };
    
    // Store in dual-write mode
    service.store_user(&user).await?;
    
    // Check migration stats
    let stats = service.get_migration_stats().await;
    println!("Migration stats: Proto={}, Thrift={}, Mode={}", 
             stats.protobuf_count, stats.thrift_count,
             if stats.using_protobuf { "Protobuf" } else { "Dual" });
    
    // Switch to Protobuf-only
    service.enable_protobuf_mode().await;
    
    // Retrieve user
    let retrieved = service.get_user(12345).await?;
    println!("Retrieved user: {}", retrieved.username);
    
    Ok(())
}
```

### Performance Benchmarking (Rust)

```rust
// benches/serialization_benchmark.rs
use criterion::{black_box, criterion_group, criterion_main, Criterion, BenchmarkId};
use prost::Message;
use std::collections::HashMap;

mod proto {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

mod thrift {
    // Thrift types
}

fn create_proto_user() -> proto::User {
    proto::User {
        user_id: 12345,
        username: "benchmark_user".to_string(),
        email: "bench@example.com".to_string(),
        status: proto::Status::Active as i32,
        address: Some(proto::Address {
            street: "123 Benchmark Ave".to_string(),
            city: "Testville".to_string(),
            state: "TS".to_string(),
            zip_code: "12345".to_string(),
        }),
        tags: vec!["tag1".to_string(), "tag2".to_string(), "tag3".to_string()],
        metadata: {
            let mut map = HashMap::new();
            map.insert("key1".to_string(), "value1".to_string());
            map.insert("key2".to_string(), "value2".to_string());
            map
        },
    }
}

fn benchmark_protobuf_serialization(c: &mut Criterion) {
    let user = create_proto_user();
    
    c.bench_function("protobuf_encode", |b| {
        b.iter(|| {
            let mut buf = Vec::new();
            user.encode(&mut buf).unwrap();
            black_box(buf);
        });
    });
}

fn benchmark_protobuf_deserialization(c: &mut Criterion) {
    let user = create_proto_user();
    let mut buf = Vec::new();
    user.encode(&mut buf).unwrap();
    
    c.bench_function("protobuf_decode", |b| {
        b.iter(|| {
            let decoded = proto::User::decode(&buf[..]).unwrap();
            black_box(decoded);
        });
    });
}

fn benchmark_size_comparison(c: &mut Criterion) {
    let user = create_proto_user();
    
    let mut group = c.benchmark_group("serialization_size");
    
    group.bench_function("protobuf_size", |b| {
        b.iter(|| {
            let mut buf = Vec::new();
            user.encode(&mut buf).unwrap();
            black_box(buf.len());
        });
    });
    
    group.finish();
}

fn benchmark_round_trip(c: &mut Criterion) {
    let user = create_proto_user();
    
    c.bench_function("protobuf_round_trip", |b| {
        b.iter(|| {
            let mut buf = Vec::new();
            user.encode(&mut buf).unwrap();
            let decoded = proto::User::decode(&buf[..]).unwrap();
            black_box(decoded);
        });
    });
}

criterion_group!(
    benches,
    benchmark_protobuf_serialization,
    benchmark_protobuf_deserialization,
    benchmark_size_comparison,
    benchmark_round_trip
);
criterion_main!(benches);
```

---

## Best Practices

### 1. **Schema Design for Migration**

```protobuf
// Good: Maintain backward compatibility
message User {
    int64 user_id = 1;
    string username = 2;
    string email = 3;
    
    // Use reserved for deprecated fields
    reserved 4, 5;
    reserved "old_field_name";
    
    // New fields use higher numbers
    Status status = 10;
    Address address = 11;
}
```

### 2. **Gradual Rollout Strategy**

1. **Phase 1**: Deploy dual-write capability
2. **Phase 2**: Migrate read traffic (canary → full)
3. **Phase 3**: Backfill historical data
4. **Phase 4**: Remove Thrift write path
5. **Phase 5**: Clean up legacy code

### 3. **Testing Strategy**

```rust
// Shadow testing example
async fn shadow_test_migration(&self, user_id: i64) -> ShadowTestResult {
    let thrift_result = self.thrift_store.read(user_id).await;
    let proto_result = self.protobuf_store.read(user_id).await;
    
    // Compare results
    let matches = match (thrift_result, proto_result) {
        (Ok(t), Ok(p)) => {
            let converted = ThriftToProtoAdapter::thrift_to_proto(&t);
            compare_users(&converted, &p)
        },
        _ => false,
    };
    
    ShadowTestResult {
        user_id,
        formats_match: matches,
        timestamp: SystemTime::now(),
    }
}
```

### 4. **Monitoring and Metrics**

```rust
struct MigrationMetrics {
    dual_writes: Counter,
    protobuf_reads: Counter,
    thrift_reads: Counter,
    conversion_errors: Counter,
    format_mismatches: Counter,
}

impl MigrationMetrics {
    fn record_dual_write(&self) {
        self.dual_writes.increment();
    }
    
    fn record_format_mismatch(&self, user_id: i64) {
        self.format_mismatches.increment();
        log::warn!("Format mismatch detected for user {}", user_id);
    }
}
```

### 5. **Error Handling**

```rust
pub enum MigrationError {
    ConversionError(String),
    StorageError(String),
    ValidationError(String),
    NetworkError(String),
}

impl From<prost::DecodeError> for MigrationError {
    fn from(err: prost::DecodeError) -> Self {
        MigrationError::ConversionError(format!("Protobuf decode error: {}", err))
    }
}
```

---

## Summary

### Key Takeaways

1. **Schema Conversion**: Thrift to Protobuf requires careful mapping of types, especially for required/optional fields and enums

2. **Migration Strategies**:
   - Dual-write approach minimizes risk
   - Adapter pattern provides abstraction layer
   - Shadow testing validates correctness

3. **Implementation Considerations**:
   - **C/C++**: Direct struct conversion, manual memory management
   - **Rust**: Type-safe conversions, async/await for I/O operations
   - Both languages benefit from code generation

4. **Performance Benefits**:
   - Protobuf typically 20-30% smaller serialized size
   - 1.5-3x faster serialization/deserialization
   - Better language support and tooling

5. **Migration Timeline**:
   - Typical migration: 3-6 months for large systems
   - Phases: Dual-write → Read migration → Backfill → Cleanup
   - Requires robust monitoring and rollback capability

6. **Common Pitfalls**:
   - Not handling optional fields correctly in Proto3
   - Forgetting to add enum zero value
   - Inadequate testing of edge cases
   - Rushing the migration without proper shadowing

### When to Migrate

**Migrate when**:
- Need better performance and smaller payload sizes
- Require stronger backward/forward compatibility
- Want better language ecosystem support
- Planning to adopt gRPC

**Stay with Thrift if**:
- Heavy investment in Thrift infrastructure
- Using Thrift-specific features extensively
- Small, stable system with no performance issues
- Team lacks bandwidth for migration

The migration from Thrift to Protobuf is a significant undertaking but can yield substantial benefits in performance, maintainability, and ecosystem support when executed properly with the strategies outlined above.