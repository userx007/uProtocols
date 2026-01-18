# Service Boundary Design in Protocol Buffers

## Overview

Service boundary design in Protocol Buffers focuses on creating well-defined interfaces between services that promote loose coupling, maintainability, and independent evolution. The goal is to structure messages and service definitions so that services can communicate effectively while remaining independently deployable and modifiable.

## Key Principles

### 1. **Encapsulation and Information Hiding**
Services should expose only what's necessary and hide internal implementation details. Messages crossing service boundaries should represent contracts, not internal data structures.

### 2. **Stability Over Convenience**
Service interfaces should prioritize stability and backward compatibility over developer convenience. Internal changes shouldn't ripple across service boundaries.

### 3. **Autonomy and Independence**
Each service should own its data model and business logic. Avoid sharing internal message definitions directly between services.

### 4. **Explicit Contracts**
Use dedicated request/response message types rather than reusing internal entities. This creates explicit, documented contracts.

## Common Anti-Patterns to Avoid

- **Sharing internal proto definitions** across service boundaries
- **Exposing database schemas** directly as proto messages
- **Tightly coupled message hierarchies** that force coordinated deployments
- **Generic "god messages"** that serve multiple purposes
- **Leaking implementation details** through message structure

## Design Strategies

### Strategy 1: Interface Segregation

Create specific message types for each service boundary rather than reusing internal types.

### Strategy 2: Canonical Data Models

Define canonical representations at boundaries while allowing different internal representations.

### Strategy 3: Version Tolerance

Design messages to tolerate additions and changes without breaking existing clients.

---

## Code Examples

### C++ Implementation

```cpp
// ============================================
// BAD EXAMPLE: Tight Coupling
// ============================================

// internal_user.proto (service internals)
message InternalUser {
  int64 id = 1;
  string username = 2;
  string password_hash = 3;  // Internal detail
  string email = 4;
  repeated string roles = 5;
  google.protobuf.Timestamp last_login = 6;
  int32 failed_login_attempts = 7;  // Internal detail
}

// user_service.proto (exposing internals - BAD!)
service UserService {
  rpc GetUser(GetUserRequest) returns (InternalUser);  // Leaking internals!
}

// ============================================
// GOOD EXAMPLE: Proper Boundary Design
// ============================================

// user_api.proto (public API contract)
syntax = "proto3";

package user.api.v1;

import "google/protobuf/timestamp.proto";

// Public representation - only what consumers need
message User {
  string user_id = 1;  // Opaque ID, not internal PK
  string username = 2;
  string email = 3;
  repeated string roles = 4;
  google.protobuf.Timestamp created_at = 5;
}

message GetUserRequest {
  string user_id = 1;
}

message GetUserResponse {
  User user = 1;
  ResponseMetadata metadata = 2;
}

message ResponseMetadata {
  google.protobuf.Timestamp timestamp = 1;
  string request_id = 2;
}

service UserService {
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
  rpc ListUsers(ListUsersRequest) returns (ListUsersResponse);
}

// ============================================
// C++ Service Implementation
// ============================================

#include "user_api.grpc.pb.h"
#include <grpc++/grpc++.h>
#include <memory>

// Internal domain model (not exposed)
struct InternalUser {
    int64_t id;
    std::string username;
    std::string password_hash;
    std::string email;
    std::vector<std::string> roles;
    time_t last_login;
};

class UserServiceImpl final : public user::api::v1::UserService::Service {
private:
    // Mapping function: Internal -> API
    void MapToApiUser(const InternalUser& internal, 
                      user::api::v1::User* api_user) {
        // Convert internal ID to opaque string
        api_user->set_user_id("usr_" + std::to_string(internal.id));
        api_user->set_username(internal.username);
        api_user->set_email(internal.email);
        
        for (const auto& role : internal.roles) {
            api_user->add_roles(role);
        }
        
        auto* timestamp = api_user->mutable_created_at();
        timestamp->set_seconds(internal.last_login);
    }

public:
    grpc::Status GetUser(
        grpc::ServerContext* context,
        const user::api::v1::GetUserRequest* request,
        user::api::v1::GetUserResponse* response) override {
        
        // Extract internal ID from opaque user_id
        std::string user_id = request->user_id();
        if (user_id.substr(0, 4) != "usr_") {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid user ID format");
        }
        
        int64_t internal_id = std::stoll(user_id.substr(4));
        
        // Fetch internal representation (from DB, cache, etc.)
        InternalUser internal_user = FetchUserById(internal_id);
        
        // Map to API contract
        MapToApiUser(internal_user, response->mutable_user());
        
        // Set metadata
        auto* metadata = response->mutable_metadata();
        metadata->set_request_id(GenerateRequestId());
        auto* ts = metadata->mutable_timestamp();
        ts->set_seconds(time(nullptr));
        
        return grpc::Status::OK;
    }
    
private:
    InternalUser FetchUserById(int64_t id) {
        // Database/cache lookup implementation
        InternalUser user;
        user.id = id;
        user.username = "john_doe";
        user.email = "john@example.com";
        user.roles = {"user", "admin"};
        user.last_login = time(nullptr);
        return user;
    }
    
    std::string GenerateRequestId() {
        return "req_" + std::to_string(time(nullptr));
    }
};

// ============================================
// Client Example
// ============================================

class UserClient {
private:
    std::unique_ptr<user::api::v1::UserService::Stub> stub_;

public:
    UserClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(user::api::v1::UserService::NewStub(channel)) {}
    
    std::optional<user::api::v1::User> GetUser(const std::string& user_id) {
        user::api::v1::GetUserRequest request;
        request.set_user_id(user_id);
        
        user::api::v1::GetUserResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->GetUser(&context, request, &response);
        
        if (status.ok()) {
            return response.user();
        }
        return std::nullopt;
    }
};
```

### Rust Implementation

```rust
// ============================================
// Protocol Buffer Definitions
// ============================================

// user_api.proto
/*
syntax = "proto3";
package user.api.v1;

message User {
  string user_id = 1;
  string username = 2;
  string email = 3;
  repeated string roles = 4;
  int64 created_at = 5;
}

message GetUserRequest {
  string user_id = 1;
}

message GetUserResponse {
  User user = 1;
  ResponseMetadata metadata = 2;
}

message ResponseMetadata {
  int64 timestamp = 1;
  string request_id = 2;
}

service UserService {
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
}
*/

// ============================================
// Rust Implementation
// ============================================

use tonic::{Request, Response, Status};
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::Uuid;

// Generated from proto
pub mod user_api {
    tonic::include_proto!("user.api.v1");
}

// Internal domain model (not exposed across boundaries)
#[derive(Debug, Clone)]
struct InternalUser {
    id: i64,
    username: String,
    password_hash: String,  // Never exposed
    email: String,
    roles: Vec<String>,
    last_login: i64,
    failed_attempts: i32,   // Never exposed
}

// Service implementation
pub struct UserServiceImpl {
    // Database connection, cache, etc.
}

impl UserServiceImpl {
    pub fn new() -> Self {
        Self {}
    }
    
    // Mapping: Internal -> API (boundary translation)
    fn map_to_api_user(&self, internal: &InternalUser) -> user_api::User {
        user_api::User {
            user_id: format!("usr_{}", internal.id),  // Opaque ID
            username: internal.username.clone(),
            email: internal.email.clone(),
            roles: internal.roles.clone(),
            created_at: internal.last_login,
        }
    }
    
    // Internal data access
    fn fetch_user_by_id(&self, id: i64) -> Result<InternalUser, Status> {
        // Simulated database lookup
        Ok(InternalUser {
            id,
            username: "john_doe".to_string(),
            password_hash: "hashed_secret".to_string(),
            email: "john@example.com".to_string(),
            roles: vec!["user".to_string(), "admin".to_string()],
            last_login: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs() as i64,
            failed_attempts: 0,
        })
    }
    
    fn parse_user_id(&self, user_id: &str) -> Result<i64, Status> {
        if !user_id.starts_with("usr_") {
            return Err(Status::invalid_argument("Invalid user ID format"));
        }
        
        user_id[4..]
            .parse::<i64>()
            .map_err(|_| Status::invalid_argument("Invalid user ID"))
    }
}

#[tonic::async_trait]
impl user_api::user_service_server::UserService for UserServiceImpl {
    async fn get_user(
        &self,
        request: Request<user_api::GetUserRequest>,
    ) -> Result<Response<user_api::GetUserResponse>, Status> {
        let req = request.into_inner();
        
        // Parse opaque ID to internal ID
        let internal_id = self.parse_user_id(&req.user_id)?;
        
        // Fetch internal representation
        let internal_user = self.fetch_user_by_id(internal_id)?;
        
        // Map to API contract (boundary translation)
        let api_user = self.map_to_api_user(&internal_user);
        
        // Build response with metadata
        let response = user_api::GetUserResponse {
            user: Some(api_user),
            metadata: Some(user_api::ResponseMetadata {
                timestamp: SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                request_id: Uuid::new_v4().to_string(),
            }),
        };
        
        Ok(Response::new(response))
    }
}

// ============================================
// Client Example
// ============================================

pub struct UserClient {
    client: user_api::user_service_client::UserServiceClient<tonic::transport::Channel>,
}

impl UserClient {
    pub async fn new(addr: String) -> Result<Self, Box<dyn std::error::Error>> {
        let client = user_api::user_service_client::UserServiceClient::connect(addr).await?;
        Ok(Self { client })
    }
    
    pub async fn get_user(
        &mut self,
        user_id: String,
    ) -> Result<user_api::User, Status> {
        let request = tonic::Request::new(user_api::GetUserRequest { user_id });
        
        let response = self.client.get_user(request).await?;
        let get_user_response = response.into_inner();
        
        get_user_response
            .user
            .ok_or_else(|| Status::not_found("User not found"))
    }
}

// ============================================
// Advanced Pattern: Anti-Corruption Layer
// ============================================

// When integrating with external services that have
// poor boundary design, use an anti-corruption layer

pub struct ExternalUserAdapter {
    external_client: ExternalUserServiceClient,
}

impl ExternalUserAdapter {
    // Translate external poorly-designed API to our clean boundary
    pub async fn get_user(&self, user_id: &str) -> Result<user_api::User, Status> {
        // Fetch from poorly-designed external service
        let external_user = self.external_client
            .fetch_user_with_all_details(user_id)
            .await?;
        
        // Transform to our clean API contract
        Ok(user_api::User {
            user_id: format!("ext_{}", external_user.internal_pk),
            username: external_user.login_name,
            email: external_user.contact_email,
            roles: self.map_external_permissions(&external_user.permissions),
            created_at: external_user.registration_timestamp,
        })
    }
    
    fn map_external_permissions(&self, perms: &[String]) -> Vec<String> {
        // Translate external permission model to our role model
        perms.iter()
            .filter_map(|p| match p.as_str() {
                "ADMIN_FULL" => Some("admin".to_string()),
                "USER_BASIC" => Some("user".to_string()),
                _ => None,
            })
            .collect()
    }
}

// Stub for external service
struct ExternalUserServiceClient;
struct ExternalUser {
    internal_pk: i64,
    login_name: String,
    contact_email: String,
    permissions: Vec<String>,
    registration_timestamp: i64,
}

impl ExternalUserServiceClient {
    async fn fetch_user_with_all_details(
        &self,
        _user_id: &str,
    ) -> Result<ExternalUser, Status> {
        Ok(ExternalUser {
            internal_pk: 123,
            login_name: "external_user".to_string(),
            contact_email: "ext@example.com".to_string(),
            permissions: vec!["ADMIN_FULL".to_string()],
            registration_timestamp: 1234567890,
        })
    }
}
```

## Summary

**Service boundary design in Protocol Buffers** is about creating stable, loosely-coupled interfaces between services. Key takeaways:

1. **Separate API contracts from internal models** - Never expose internal message types directly across service boundaries
2. **Use opaque identifiers** - Hide internal implementation details like database primary keys
3. **Create explicit request/response pairs** - Each operation gets dedicated message types with metadata
4. **Implement translation layers** - Map between internal domain models and external API contracts
5. **Design for evolution** - Use field numbers wisely, avoid breaking changes, embrace optional fields
6. **Encapsulate business logic** - Services should own their data and logic without tight coupling

Well-designed service boundaries enable independent deployment, testing, and evolution while maintaining clear contracts. This approach trades some development convenience for long-term maintainability and system resilience.