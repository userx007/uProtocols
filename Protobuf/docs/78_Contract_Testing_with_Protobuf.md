# Contract Testing with Protobuf

## Overview

Contract Testing with Protobuf is a methodology for verifying API contracts between services using Protocol Buffer schemas. It ensures that service providers and consumers maintain compatible interfaces throughout the software development lifecycle, preventing breaking changes and enabling safe schema evolution.

## What is Contract Testing?

Contract testing validates that services can communicate correctly by testing their agreed-upon interface (the "contract") independently, without requiring end-to-end integration tests. With Protobuf, the `.proto` files serve as the contract definition, specifying message structures and service methods that both producer and consumer must adhere to.

### Key Benefits

- **Early Detection**: Identifies breaking changes before they reach production
- **Independent Testing**: Services can be tested in isolation without complex test environments
- **Schema Evolution**: Enables safe evolution of APIs with backward/forward compatibility guarantees
- **Documentation**: The `.proto` files serve as living documentation of the API contract
- **Type Safety**: Protobuf's strong typing catches many errors at compile time

## Core Concepts

### 1. Contract Definition

The Protobuf schema (`.proto` file) defines the contract between services:

```protobuf
syntax = "proto3";

package user.v1;

message User {
  int32 id = 1;
  string name = 2;
  string email = 3;
  int64 created_at = 4;
}

message CreateUserRequest {
  string name = 1;
  string email = 2;
}

message CreateUserResponse {
  User user = 1;
  string status = 2;
}

service UserService {
  rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
}
```

### 2. Consumer-Driven Contract Testing

In consumer-driven contract testing, the consumer defines expectations about how the provider should behave. The provider must then verify it meets these expectations.

### 3. Provider Contract Testing

Providers verify they can fulfill the contracts defined by their consumers without breaking existing integrations.

## Testing Approaches

### Approach 1: Schema Validation with Buf

Buf is a modern tool for managing Protobuf schemas that includes linting and breaking change detection.

**buf.yaml configuration:**

```yaml
version: v2
modules:
  - path: proto
    name: buf.build/myorg/userservice
lint:
  use:
    - STANDARD
  except:
    - FIELD_NOT_REQUIRED
    - PACKAGE_NO_IMPORT_CYCLE
breaking:
  use:
    - FILE  # Strictest level - prevents most breaking changes
  except:
    - EXTENSION_NO_DELETE
    - FIELD_SAME_DEFAULT
```

**Breaking change detection in CI/CD:**

```bash
# Compare against main branch
buf breaking --against '.git#branch=main'

# Compare against a specific tag
buf breaking --against '.git#tag=v1.0.0'

# Compare against remote repository
buf breaking --against 'https://github.com/org/repo.git#branch=main'
```

### Approach 2: Pact Framework with Protobuf Plugin

Pact is a popular contract testing framework that now supports Protobuf through its plugin system.

**Consumer-side test (conceptual):**

```java
// Java example using Pact with Protobuf plugin
@ExtendWith(PactConsumerTestExt.class)
public class UserServiceConsumerTest {
    
    @Pact(consumer = "user-ui", provider = "user-service")
    public MessagePact createUserPact(MessagePactBuilder builder) {
        return builder
            .usingPlugin("protobuf")
            .expectsToReceive("create user response")
            .withContent(new PactDslJsonBody()
                .stringValue("status", "success")
                .object("user")
                    .integerType("id", 123)
                    .stringType("name", "John Doe")
                    .stringType("email", "john@example.com")
                .closeObject())
            .toPact();
    }
}
```

## C/C++ Implementation Examples

### Example 1: Basic Contract Validation in C++

```cpp
// contract_validator.hpp
#pragma once

#include <string>
#include <vector>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

namespace contract_testing {

class ContractValidator {
public:
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    // Validate backward compatibility between two schemas
    ValidationResult ValidateBackwardCompatibility(
        const google::protobuf::FileDescriptor* old_schema,
        const google::protobuf::FileDescriptor* new_schema);

    // Check if field number was changed
    bool IsFieldNumberChanged(
        const google::protobuf::Descriptor* old_msg,
        const google::protobuf::Descriptor* new_msg);

    // Check if required field was added
    bool HasNewRequiredFields(
        const google::protobuf::Descriptor* old_msg,
        const google::protobuf::Descriptor* new_msg);
};

} // namespace contract_testing
```

```cpp
// contract_validator.cpp
#include "contract_validator.hpp"
#include <unordered_map>
#include <unordered_set>

namespace contract_testing {

ContractValidator::ValidationResult 
ContractValidator::ValidateBackwardCompatibility(
    const google::protobuf::FileDescriptor* old_schema,
    const google::protobuf::FileDescriptor* new_schema) {
    
    ValidationResult result{true, {}, {}};

    // Check each message type for compatibility
    for (int i = 0; i < new_schema->message_type_count(); i++) {
        const auto* new_msg = new_schema->message_type(i);
        const auto* old_msg = old_schema->FindMessageTypeByName(new_msg->name());

        if (old_msg == nullptr) {
            result.warnings.push_back(
                "New message type added: " + new_msg->full_name());
            continue;
        }

        // Check for field number changes
        if (IsFieldNumberChanged(old_msg, new_msg)) {
            result.is_valid = false;
            result.errors.push_back(
                "Field number changed in: " + new_msg->full_name());
        }

        // Check for new required fields
        if (HasNewRequiredFields(old_msg, new_msg)) {
            result.is_valid = false;
            result.errors.push_back(
                "New required field added in: " + new_msg->full_name());
        }
    }

    return result;
}

bool ContractValidator::IsFieldNumberChanged(
    const google::protobuf::Descriptor* old_msg,
    const google::protobuf::Descriptor* new_msg) {
    
    // Build map of field name to number for old message
    std::unordered_map<std::string, int> old_fields;
    for (int i = 0; i < old_msg->field_count(); i++) {
        const auto* field = old_msg->field(i);
        old_fields[field->name()] = field->number();
    }

    // Check if any field numbers changed in new message
    for (int i = 0; i < new_msg->field_count(); i++) {
        const auto* field = new_msg->field(i);
        auto it = old_fields.find(field->name());
        
        if (it != old_fields.end() && it->second != field->number()) {
            return true; // Field number changed
        }
    }

    return false;
}

bool ContractValidator::HasNewRequiredFields(
    const google::protobuf::Descriptor* old_msg,
    const google::protobuf::Descriptor* new_msg) {
    
    // Get set of old field numbers
    std::unordered_set<int> old_field_numbers;
    for (int i = 0; i < old_msg->field_count(); i++) {
        old_field_numbers.insert(old_msg->field(i)->number());
    }

    // Check for new required fields
    for (int i = 0; i < new_msg->field_count(); i++) {
        const auto* field = new_msg->field(i);
        
        // In proto3, all fields are optional, but check label for proto2
        if (field->is_required() && 
            old_field_numbers.find(field->number()) == old_field_numbers.end()) {
            return true;
        }
    }

    return false;
}

} // namespace contract_testing
```

### Example 2: Consumer Test in C++

```cpp
// user_service_consumer_test.cpp
#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "user.pb.h"
#include "user.grpc.pb.h"

namespace user_service_test {

class UserServiceConsumerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Mock server setup would go here
        // For contract testing, we validate message structure
    }
};

TEST_F(UserServiceConsumerTest, CreateUserRequest_HasRequiredFields) {
    user::v1::CreateUserRequest request;
    
    // Contract expectation: request must have name and email
    request.set_name("John Doe");
    request.set_email("john@example.com");
    
    // Validate the contract
    EXPECT_TRUE(request.has_name());
    EXPECT_TRUE(request.has_email());
    EXPECT_FALSE(request.name().empty());
    EXPECT_FALSE(request.email().empty());
}

TEST_F(UserServiceConsumerTest, CreateUserResponse_MeetsContract) {
    user::v1::CreateUserResponse response;
    
    // Set up expected response structure
    auto* user = response.mutable_user();
    user->set_id(123);
    user->set_name("John Doe");
    user->set_email("john@example.com");
    user->set_created_at(1234567890);
    response.set_status("success");
    
    // Contract validation
    EXPECT_TRUE(response.has_user());
    EXPECT_TRUE(response.has_status());
    EXPECT_GT(response.user().id(), 0);
    EXPECT_FALSE(response.user().name().empty());
    EXPECT_FALSE(response.user().email().empty());
}

TEST_F(UserServiceConsumerTest, ValidateMessageSerializationRoundTrip) {
    // Create original message
    user::v1::User original;
    original.set_id(123);
    original.set_name("John Doe");
    original.set_email("john@example.com");
    original.set_created_at(1234567890);
    
    // Serialize
    std::string serialized;
    ASSERT_TRUE(original.SerializeToString(&serialized));
    
    // Deserialize
    user::v1::User deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    // Contract: all fields must survive round-trip
    EXPECT_EQ(original.id(), deserialized.id());
    EXPECT_EQ(original.name(), deserialized.name());
    EXPECT_EQ(original.email(), deserialized.email());
    EXPECT_EQ(original.created_at(), deserialized.created_at());
}

} // namespace user_service_test
```

### Example 3: Provider Verification in C

```c
// provider_contract_test.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "user.pb-c.h"

// Simulated provider function
User__V1__CreateUserResponse* create_user_provider(
    const User__V1__CreateUserRequest* request) {
    
    User__V1__CreateUserResponse* response = malloc(
        sizeof(User__V1__CreateUserResponse));
    user__v1__create_user_response__init(response);
    
    // Allocate and initialize user
    response->user = malloc(sizeof(User__V1__User));
    user__v1__user__init(response->user);
    
    // Provider implementation
    response->user->id = 123;
    response->user->name = strdup(request->name);
    response->user->email = strdup(request->email);
    response->user->created_at = 1234567890;
    response->status = strdup("success");
    
    return response;
}

// Contract verification test
void test_provider_meets_create_user_contract() {
    printf("Testing provider contract for CreateUser...\n");
    
    // Create consumer's expected request
    User__V1__CreateUserRequest request = USER__V1__CREATE_USER_REQUEST__INIT;
    request.name = "John Doe";
    request.email = "john@example.com";
    
    // Call provider
    User__V1__CreateUserResponse* response = create_user_provider(&request);
    
    // Verify contract obligations
    assert(response != NULL);
    assert(response->user != NULL);
    assert(response->status != NULL);
    
    // Verify response structure
    assert(response->user->id > 0);
    assert(response->user->name != NULL);
    assert(strlen(response->user->name) > 0);
    assert(response->user->email != NULL);
    assert(strlen(response->user->email) > 0);
    assert(response->user->created_at > 0);
    assert(strcmp(response->status, "success") == 0);
    
    printf("Provider contract test passed!\n");
    
    // Cleanup
    free(response->user->name);
    free(response->user->email);
    free(response->user);
    free(response->status);
    free(response);
}

int main() {
    test_provider_meets_create_user_contract();
    return 0;
}
```

## Rust Implementation Examples

### Example 1: Contract Validator in Rust

```rust
// contract_validator.rs
use prost_types::FileDescriptorProto;
use std::collections::{HashMap, HashSet};

#[derive(Debug)]
pub struct ValidationResult {
    pub is_valid: bool,
    pub errors: Vec<String>,
    pub warnings: Vec<String>,
}

pub struct ContractValidator;

impl ContractValidator {
    pub fn new() -> Self {
        ContractValidator
    }

    /// Validates backward compatibility between two Protobuf schemas
    pub fn validate_backward_compatibility(
        &self,
        old_schema: &FileDescriptorProto,
        new_schema: &FileDescriptorProto,
    ) -> ValidationResult {
        let mut result = ValidationResult {
            is_valid: true,
            errors: Vec::new(),
            warnings: Vec::new(),
        };

        // Compare message types
        let old_messages: HashMap<_, _> = old_schema
            .message_type
            .iter()
            .map(|m| (m.name.as_ref().unwrap().as_str(), m))
            .collect();

        for new_msg in &new_schema.message_type {
            let new_msg_name = new_msg.name.as_ref().unwrap();
            
            if let Some(old_msg) = old_messages.get(new_msg_name.as_str()) {
                // Check for field number changes
                if self.has_field_number_changes(old_msg, new_msg) {
                    result.is_valid = false;
                    result.errors.push(format!(
                        "Field number changed in message: {}",
                        new_msg_name
                    ));
                }

                // Check for deleted fields
                if let Some(deleted) = self.find_deleted_fields(old_msg, new_msg) {
                    result.warnings.push(format!(
                        "Fields deleted in {}: {:?}",
                        new_msg_name, deleted
                    ));
                }
            } else {
                result.warnings.push(format!(
                    "New message type added: {}",
                    new_msg_name
                ));
            }
        }

        result
    }

    fn has_field_number_changes(
        &self,
        old_msg: &prost_types::DescriptorProto,
        new_msg: &prost_types::DescriptorProto,
    ) -> bool {
        let old_fields: HashMap<_, _> = old_msg
            .field
            .iter()
            .map(|f| (f.name.as_ref().unwrap().as_str(), f.number.unwrap()))
            .collect();

        for new_field in &new_msg.field {
            let field_name = new_field.name.as_ref().unwrap();
            if let Some(&old_number) = old_fields.get(field_name.as_str()) {
                if old_number != new_field.number.unwrap() {
                    return true; // Field number changed
                }
            }
        }

        false
    }

    fn find_deleted_fields(
        &self,
        old_msg: &prost_types::DescriptorProto,
        new_msg: &prost_types::DescriptorProto,
    ) -> Option<Vec<String>> {
        let new_field_numbers: HashSet<_> = new_msg
            .field
            .iter()
            .map(|f| f.number.unwrap())
            .collect();

        let deleted: Vec<String> = old_msg
            .field
            .iter()
            .filter(|f| !new_field_numbers.contains(&f.number.unwrap()))
            .map(|f| f.name.as_ref().unwrap().clone())
            .collect();

        if deleted.is_empty() {
            None
        } else {
            Some(deleted)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_field_number_change() {
        let validator = ContractValidator::new();
        
        // Create old schema
        let mut old_field = prost_types::FieldDescriptorProto::default();
        old_field.name = Some("user_id".to_string());
        old_field.number = Some(1);
        
        let mut old_msg = prost_types::DescriptorProto::default();
        old_msg.name = Some("User".to_string());
        old_msg.field.push(old_field);
        
        // Create new schema with changed field number
        let mut new_field = prost_types::FieldDescriptorProto::default();
        new_field.name = Some("user_id".to_string());
        new_field.number = Some(2); // Changed!
        
        let mut new_msg = prost_types::DescriptorProto::default();
        new_msg.name = Some("User".to_string());
        new_msg.field.push(new_field);
        
        assert!(validator.has_field_number_changes(&old_msg, &new_msg));
    }
}
```

### Example 2: Consumer Test in Rust

```rust
// user_service_consumer_test.rs
use prost::Message;

// Generated from .proto file
mod user {
    include!(concat!(env!("OUT_DIR"), "/user.v1.rs"));
}

#[cfg(test)]
mod consumer_tests {
    use super::*;
    use user::v1::*;

    #[test]
    fn test_create_user_request_contract() {
        // Consumer's expectation of the request structure
        let request = CreateUserRequest {
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        };

        // Contract validation
        assert!(!request.name.is_empty(), "Name must not be empty");
        assert!(!request.email.is_empty(), "Email must not be empty");
        assert!(request.email.contains('@'), "Email must be valid");
    }

    #[test]
    fn test_create_user_response_contract() {
        // Consumer's expectation of the response structure
        let response = CreateUserResponse {
            user: Some(User {
                id: 123,
                name: "John Doe".to_string(),
                email: "john@example.com".to_string(),
                created_at: 1234567890,
            }),
            status: "success".to_string(),
        };

        // Verify contract obligations
        assert!(response.user.is_some(), "User must be present");
        assert!(!response.status.is_empty(), "Status must not be empty");
        
        let user = response.user.unwrap();
        assert!(user.id > 0, "User ID must be positive");
        assert!(!user.name.is_empty(), "User name must not be empty");
        assert!(!user.email.is_empty(), "User email must not be empty");
        assert!(user.created_at > 0, "Created timestamp must be valid");
    }

    #[test]
    fn test_message_serialization_roundtrip() {
        // Original message
        let original = User {
            id: 123,
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
            created_at: 1234567890,
        };

        // Serialize to bytes
        let mut buf = Vec::new();
        original.encode(&mut buf).expect("Failed to encode");

        // Deserialize from bytes
        let deserialized = User::decode(&buf[..])
            .expect("Failed to decode");

        // Contract: all fields must survive serialization
        assert_eq!(original.id, deserialized.id);
        assert_eq!(original.name, deserialized.name);
        assert_eq!(original.email, deserialized.email);
        assert_eq!(original.created_at, deserialized.created_at);
    }

    #[test]
    fn test_forward_compatibility() {
        // Simulate old consumer receiving message from new provider
        // with additional fields
        
        // New provider sends extra field (not in old schema)
        let new_message_bytes = vec![
            0x08, 0x7b, // id = 123
            0x12, 0x08, 0x4a, 0x6f, 0x68, 0x6e, 0x20, 0x44, 0x6f, 0x65, // name
            0x1a, 0x10, 0x6a, 0x6f, 0x68, 0x6e, 0x40, 0x65, 0x78, 0x61,
            0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, // email
            0x20, 0xd2, 0x85, 0xd8, 0xcc, 0x04, // created_at
            // Additional unknown field would be here
        ];

        // Old consumer should still parse known fields
        let user = User::decode(&new_message_bytes[..])
            .expect("Should parse message with unknown fields");
        
        assert_eq!(user.id, 123);
        assert_eq!(user.name, "John Doe");
    }
}
```

### Example 3: Provider Verification in Rust

```rust
// user_service_provider_test.rs
use tonic::{Request, Response, Status};

mod user {
    include!(concat!(env!("OUT_DIR"), "/user.v1.rs"));
}

use user::v1::*;

// Mock provider implementation
pub struct UserServiceProvider;

impl UserServiceProvider {
    pub async fn create_user(
        &self,
        request: CreateUserRequest,
    ) -> Result<CreateUserResponse, Status> {
        // Validate input meets contract
        if request.name.is_empty() {
            return Err(Status::invalid_argument("Name is required"));
        }
        if request.email.is_empty() {
            return Err(Status::invalid_argument("Email is required"));
        }

        // Provider implementation
        let user = User {
            id: 123,
            name: request.name,
            email: request.email,
            created_at: 1234567890,
        };

        Ok(CreateUserResponse {
            user: Some(user),
            status: "success".to_string(),
        })
    }
}

#[cfg(test)]
mod provider_tests {
    use super::*;

    #[tokio::test]
    async fn test_provider_meets_create_user_contract() {
        let provider = UserServiceProvider;

        // Consumer's expected request
        let request = CreateUserRequest {
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        };

        // Call provider
        let response = provider
            .create_user(request)
            .await
            .expect("Provider should return valid response");

        // Verify provider meets contract obligations
        assert!(response.user.is_some(), "Response must include user");
        assert!(!response.status.is_empty(), "Response must include status");

        let user = response.user.unwrap();
        assert!(user.id > 0, "User ID must be positive");
        assert!(!user.name.is_empty(), "User name must not be empty");
        assert!(!user.email.is_empty(), "User email must not be empty");
        assert!(user.created_at > 0, "Created timestamp must be positive");
        assert_eq!(response.status, "success");
    }

    #[tokio::test]
    async fn test_provider_validates_input() {
        let provider = UserServiceProvider;

        // Test with empty name
        let bad_request = CreateUserRequest {
            name: "".to_string(),
            email: "john@example.com".to_string(),
        };

        let result = provider.create_user(bad_request).await;
        assert!(result.is_err(), "Provider should reject invalid input");
    }

    #[tokio::test]
    async fn test_provider_backward_compatibility() {
        // Ensure provider can handle old client requests
        // that might be missing newly added optional fields
        
        let provider = UserServiceProvider;
        
        let minimal_request = CreateUserRequest {
            name: "John".to_string(),
            email: "john@example.com".to_string(),
            // Optional fields omitted
        };

        let response = provider
            .create_user(minimal_request)
            .await
            .expect("Provider should handle minimal valid request");

        assert!(response.user.is_some());
    }
}
```

### Example 4: Integration Test with Mock Server (Rust)

```rust
// integration_test.rs
use tonic::transport::Server;
use std::net::SocketAddr;

mod user {
    include!(concat!(env!("OUT_DIR"), "/user.v1.rs"));
}

use user::v1::{user_service_server::*, *};

#[derive(Default)]
pub struct TestUserService;

#[tonic::async_trait]
impl UserService for TestUserService {
    async fn create_user(
        &self,
        request: tonic::Request<CreateUserRequest>,
    ) -> Result<tonic::Response<CreateUserResponse>, tonic::Status> {
        let req = request.into_inner();
        
        let user = User {
            id: 123,
            name: req.name,
            email: req.email,
            created_at: chrono::Utc::now().timestamp(),
        };

        Ok(tonic::Response::new(CreateUserResponse {
            user: Some(user),
            status: "success".to_string(),
        }))
    }

    async fn get_user(
        &self,
        _request: tonic::Request<GetUserRequest>,
    ) -> Result<tonic::Response<GetUserResponse>, tonic::Status> {
        unimplemented!()
    }
}

#[cfg(test)]
mod integration_tests {
    use super::*;
    use user::v1::user_service_client::UserServiceClient;

    async fn start_test_server() -> SocketAddr {
        let addr = "127.0.0.1:0".parse().unwrap();
        let service = TestUserService::default();
        
        let server = Server::builder()
            .add_service(UserServiceServer::new(service))
            .serve(addr);

        let addr = server.local_addr();
        tokio::spawn(server);
        addr
    }

    #[tokio::test]
    async fn test_contract_end_to_end() {
        let addr = start_test_server().await;
        let mut client = UserServiceClient::connect(format!("http://{}", addr))
            .await
            .expect("Failed to connect");

        let request = tonic::Request::new(CreateUserRequest {
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        });

        let response = client
            .create_user(request)
            .await
            .expect("Request failed");

        let response = response.into_inner();
        
        // Verify contract is fulfilled
        assert!(response.user.is_some());
        let user = response.user.unwrap();
        assert!(user.id > 0);
        assert_eq!(user.name, "John Doe");
        assert_eq!(user.email, "john@example.com");
    }
}
```

## Best Practices

### 1. Schema Evolution Rules

**Safe Changes (Non-Breaking):**
- Adding optional fields
- Adding new message types
- Adding new RPC methods
- Marking fields as deprecated
- Adding new enum values (at the end)

**Unsafe Changes (Breaking):**
- Changing field numbers
- Changing field types
- Renaming fields (affects JSON representation)
- Removing fields (better to deprecate)
- Changing message/service names
- Adding required fields (proto2)

### 2. Versioning Strategy

```protobuf
// Use versioned packages
syntax = "proto3";

package user.v1;

// When making breaking changes, create new version
package user.v2;
```

### 3. CI/CD Integration

**GitHub Actions Example:**

```yaml
name: Contract Testing

on:
  pull_request:
    branches: [ main ]

jobs:
  contract-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
        with:
          fetch-depth: 0  # Need full history for buf breaking
      
      - name: Install Buf
        uses: bufbuild/buf-setup-action@v1
      
      - name: Lint Protobuf
        run: buf lint
      
      - name: Check Breaking Changes
        run: buf breaking --against '.git#branch=main'
      
      - name: Run Contract Tests
        run: cargo test --test contract_tests
```

### 4. Documentation

Always document your contracts:

```protobuf
syntax = "proto3";

package user.v1;

// UserService provides user management operations.
// Version: 1.0
// Last Updated: 2024-01-15
service UserService {
  // CreateUser creates a new user account.
  // Returns an error if email already exists.
  rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
}

// User represents a user account.
message User {
  // Unique identifier for the user
  int32 id = 1;
  
  // Full name of the user
  string name = 2;
  
  // Email address (must be unique)
  string email = 3;
  
  // Unix timestamp of account creation
  int64 created_at = 4;
}
```

### 5. Testing Pyramid

```
              /\
             /  \    E2E Tests (Few)
            /____\
           /      \
          / Inte-  \  Integration Tests (Some)
         /  gration \
        /____________\
       /              \
      /   Contract     \  Contract Tests (More)
     /     Tests        \
    /____________________\
   /                      \
  /     Unit Tests         \  Unit Tests (Many)
 /__________________________\
```

## Common Pitfalls to Avoid

1. **Forgetting Field Numbers**: Never reuse field numbers even for deleted fields
2. **Changing Field Types**: This breaks binary compatibility
3. **Not Testing Backward Compatibility**: Always test that old clients work with new servers
4. **Skipping Contract Tests in CI**: Make contract validation part of your pipeline
5. **Overusing Required Fields**: In proto3, all fields are optional by default (which is good!)
6. **Not Versioning APIs**: Plan for evolution from the start

## Tools Ecosystem

### Schema Management
- **Buf**: Modern Protobuf build tool with linting and breaking change detection
- **Prototool**: Uber's Protobuf workflow tool (now deprecated, use Buf)
- **Proto-Break**: Standalone breaking change detector

### Contract Testing Frameworks
- **Pact**: Consumer-driven contract testing with Protobuf plugin
- **Spring Cloud Contract**: For Spring Boot applications
- **Specmatic**: Contract-driven development tool

### Validation
- **protovalidate**: Runtime validation for Protobuf messages
- **protoc-gen-validate**: Code generation for validation logic

## Summary

Contract Testing with Protobuf provides a robust methodology for ensuring service compatibility in distributed systems. Key takeaways:

- **Use schema as contract**: The `.proto` file is your source of truth
- **Automate validation**: Integrate tools like Buf into CI/CD pipelines
- **Test independently**: Consumer and provider tests run in isolation
- **Version carefully**: Plan for schema evolution from day one
- **Document thoroughly**: Your contracts are API documentation

By implementing contract testing with Protobuf, teams can:
- Deploy services independently with confidence
- Catch breaking changes before production
- Enable safe schema evolution
- Reduce the need for expensive end-to-end tests
- Maintain clear API contracts between teams

The combination of Protobuf's type safety, schema evolution capabilities, and contract testing tools creates a powerful framework for building reliable distributed systems.

---

# Contract Testing with Protobuf - Summary

## What is Contract Testing with Protobuf?

Contract Testing with Protobuf validates that services maintain compatible interfaces by testing their Protocol Buffer schema contracts independently. The `.proto` file serves as the contract between provider and consumer services.

## Core Concept

Instead of testing full service integration, contract testing verifies:
- **Consumer side**: The consumer's expectations about the provider's API
- **Provider side**: The provider meets the contracts defined by consumers

## Key Benefits

1. **Early Detection** - Catch breaking changes before production
2. **Independent Testing** - Test services in isolation without complex environments
3. **Safe Evolution** - Evolve APIs with backward/forward compatibility
4. **Living Documentation** - Proto files document the API contract
5. **Type Safety** - Protobuf catches errors at compile time

## Main Approaches

### 1. Schema Validation (Buf Tool)
- Automated breaking change detection
- Linting for consistent style
- Compares schemas against previous versions
- Integrates with CI/CD pipelines

```bash
# Detect breaking changes
buf breaking --against '.git#branch=main'
```

### 2. Consumer-Driven Testing (Pact Framework)
- Consumer defines expectations
- Provider verifies it meets them
- Generates contract artifacts
- Tests run independently

## Safe vs Breaking Changes

**Safe Changes:**
- ✅ Add optional fields
- ✅ Add new messages/services
- ✅ Deprecate fields
- ✅ Add enum values (at end)

**Breaking Changes:**
- ❌ Change field numbers
- ❌ Change field types
- ❌ Remove fields
- ❌ Rename messages/services
- ❌ Add required fields

## Code Examples Overview

### C/C++ Examples Provided
1. **Contract Validator** - Validates backward compatibility between schemas
2. **Consumer Test** - Tests consumer expectations using Google Test
3. **Provider Test** - Verifies provider meets contract obligations

### Rust Examples Provided
1. **Contract Validator** - Schema compatibility checker using prost
2. **Consumer Test** - Message structure and serialization tests
3. **Provider Test** - Service implementation verification
4. **Integration Test** - End-to-end contract validation with Tonic

## Essential Workflow

```
1. Define Contract (.proto file)
        ↓
2. Consumer writes tests based on contract
        ↓
3. Provider implements contract
        ↓
4. Provider runs verification tests
        ↓
5. CI/CD validates no breaking changes
        ↓
6. Deploy with confidence
```

## Best Practices

1. **Version your APIs** - Use `package user.v1`, `user.v2`, etc.
2. **Never reuse field numbers** - Even for deleted fields
3. **Automate validation** - Add contract tests to CI/CD
4. **Document contracts** - Add comments to all proto definitions
5. **Test backward compatibility** - Ensure old clients work with new servers
6. **Use semantic versioning** - Major version for breaking changes

## Essential Tools

- **Buf** - Modern Protobuf management (linting, breaking changes)
- **Pact** - Consumer-driven contract testing framework
- **protovalidate** - Runtime message validation
- **protoc** - Protocol Buffer compiler

## Quick CI/CD Integration

```yaml
# GitHub Actions example
- name: Check Breaking Changes
  run: buf breaking --against '.git#branch=main'
  
- name: Run Contract Tests
  run: cargo test --test contract_tests
```

## Common Pitfalls

1. ⚠️ Changing field numbers destroys binary compatibility
2. ⚠️ Renaming fields breaks JSON representation
3. ⚠️ Not testing old client/new server scenarios
4. ⚠️ Skipping contract validation in development
5. ⚠️ Forgetting to version API packages

## Why It Matters

Contract testing with Protobuf enables:
- **Faster deployments** - Services deploy independently
- **Better reliability** - Catch issues before production
- **Clear boundaries** - Well-defined service interfaces
- **Reduced costs** - Fewer expensive integration tests
- **Team autonomy** - Teams own their service contracts

## The Testing Pyramid for Contracts

```
    /\      E2E (Few)
   /  \     
  /____\    Integration (Some)
 /      \   
/Contract\  Contract (Many)
__________  
   Unit     Unit (Most)
```

Contract tests sit between unit and integration tests, providing high confidence with reasonable cost.

## Field Number Management

**Critical Rule**: Field numbers are permanent identifiers in Protobuf

```protobuf
message User {
  int32 id = 1;        // Never change this to 2
  string name = 2;     // Never reuse 1 for a different field
  // int32 age = 3;    // If deleted, reserve the number
  reserved 3;          // Prevents accidental reuse
}
```

## When to Use Contract Testing

**Perfect For:**
- Microservices architectures
- Multiple teams owning different services
- Public APIs that can't break clients
- gRPC/Protobuf-based communication
- Event-driven systems with schema registries

**Less Useful For:**
- Monolithic applications
- Single-team projects
- Rapid prototyping phases
- Internal-only temporary services

## Success Metrics

Track these to measure contract testing effectiveness:
- **Breaking changes caught pre-production**
- **Time to deploy services** (should decrease)
- **Production incidents from API incompatibility** (should decrease)
- **Developer confidence in deployments** (should increase)

## Next Steps

1. Add Buf to your protobuf project
2. Configure breaking change detection in CI
3. Write consumer contract tests
4. Set up provider verification
5. Document your contracts thoroughly
6. Establish versioning strategy

Contract testing with Protobuf creates a safety net for distributed systems, enabling teams to move fast without breaking things.