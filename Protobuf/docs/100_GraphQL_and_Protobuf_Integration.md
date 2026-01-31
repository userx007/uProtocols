# GraphQL and Protobuf Integration

## Overview

GraphQL and Protocol Buffers (Protobuf) serve different but complementary purposes in modern distributed systems. GraphQL provides a flexible, client-friendly query language for APIs, while Protobuf offers efficient, strongly-typed serialization for backend services. Integrating these technologies allows you to expose a developer-friendly GraphQL interface while maintaining efficient internal communication using Protobuf.

## Core Concepts

### Why Integrate GraphQL with Protobuf?

1. **Best of Both Worlds**: GraphQL's flexible querying meets Protobuf's efficient serialization
2. **Type Safety**: Both systems are strongly typed, enabling end-to-end type safety
3. **Performance**: Use GraphQL for client-facing APIs and Protobuf for high-performance internal RPCs
4. **Schema Evolution**: Leverage both systems' versioning capabilities
5. **Microservices Architecture**: GraphQL gateway can aggregate multiple Protobuf-based services

### Key Integration Patterns

- **Schema Mapping**: Converting between GraphQL and Protobuf type systems
- **Gateway Pattern**: GraphQL server as a gateway to Protobuf services (gRPC)
- **Code Generation**: Automated conversion between schemas
- **Resolver Implementation**: Translating GraphQL queries to Protobuf RPC calls

## C/C++ Implementation

### Protobuf Schema Definition

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
  int64 id = 1;
  string name = 2;
  string email = 3;
  repeated string roles = 4;
  int64 created_at = 5;
}

message GetUserRequest {
  int64 id = 1;
}

message GetUserResponse {
  User user = 1;
  bool found = 2;
}

message ListUsersRequest {
  int32 page = 1;
  int32 page_size = 2;
  string filter = 3;
}

message ListUsersResponse {
  repeated User users = 1;
  int32 total_count = 2;
}

service UserService {
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
  rpc ListUsers(ListUsersRequest) returns (ListUsersResponse);
}
```

### C++ GraphQL to Protobuf Bridge

```cpp
#include <graphqlservice/GraphQLService.h>
#include <grpcpp/grpcpp.h>
#include "user.grpc.pb.h"
#include <memory>
#include <string>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// GraphQL to Protobuf adapter
class UserServiceAdapter {
private:
    std::unique_ptr<example::UserService::Stub> stub_;

public:
    explicit UserServiceAdapter(std::shared_ptr<Channel> channel)
        : stub_(example::UserService::NewStub(channel)) {}

    // Convert GraphQL user query to Protobuf RPC
    std::optional<example::User> getUser(int64_t id) {
        example::GetUserRequest request;
        request.set_id(id);
        
        example::GetUserResponse response;
        ClientContext context;
        
        Status status = stub_->GetUser(&context, request, &response);
        
        if (status.ok() && response.found()) {
            return response.user();
        }
        return std::nullopt;
    }

    // List users with filtering
    std::vector<example::User> listUsers(int32_t page, int32_t pageSize, 
                                         const std::string& filter) {
        example::ListUsersRequest request;
        request.set_page(page);
        request.set_page_size(pageSize);
        request.set_filter(filter);
        
        example::ListUsersResponse response;
        ClientContext context;
        
        Status status = stub_->ListUsers(&context, request, &response);
        
        std::vector<example::User> users;
        if (status.ok()) {
            for (const auto& user : response.users()) {
                users.push_back(user);
            }
        }
        return users;
    }
};

// GraphQL Schema Types (pseudo-code representation)
namespace graphql {

struct UserType {
    int64_t id;
    std::string name;
    std::string email;
    std::vector<std::string> roles;
    int64_t createdAt;
    
    // Convert from Protobuf to GraphQL
    static UserType fromProtobuf(const example::User& pb_user) {
        UserType user;
        user.id = pb_user.id();
        user.name = pb_user.name();
        user.email = pb_user.email();
        user.createdAt = pb_user.created_at();
        
        for (const auto& role : pb_user.roles()) {
            user.roles.push_back(role);
        }
        return user;
    }
};

// GraphQL Resolver
class QueryResolver {
private:
    std::shared_ptr<UserServiceAdapter> userService_;

public:
    explicit QueryResolver(std::shared_ptr<UserServiceAdapter> service)
        : userService_(service) {}

    // Resolve GraphQL query: user(id: ID!)
    std::optional<UserType> resolveUser(int64_t id) {
        auto pb_user = userService_->getUser(id);
        if (pb_user) {
            return UserType::fromProtobuf(*pb_user);
        }
        return std::nullopt;
    }

    // Resolve GraphQL query: users(page: Int, pageSize: Int, filter: String)
    std::vector<UserType> resolveUsers(int32_t page, int32_t pageSize,
                                       const std::string& filter) {
        auto pb_users = userService_->listUsers(page, pageSize, filter);
        std::vector<UserType> users;
        
        for (const auto& pb_user : pb_users) {
            users.push_back(UserType::fromProtobuf(pb_user));
        }
        return users;
    }
};

} // namespace graphql

// Main integration setup
int main() {
    // Create gRPC channel to Protobuf service
    auto channel = grpc::CreateChannel(
        "localhost:50051",
        grpc::InsecureChannelCredentials()
    );
    
    // Create adapter and resolver
    auto adapter = std::make_shared<UserServiceAdapter>(channel);
    auto resolver = std::make_shared<graphql::QueryResolver>(adapter);
    
    // Set up GraphQL server (implementation specific)
    // This would integrate with a GraphQL C++ library like cppgraphqlgen
    
    std::cout << "GraphQL-Protobuf gateway running..." << std::endl;
    
    return 0;
}
```

### Type Mapping Helper (C++)

```cpp
#include <google/protobuf/timestamp.pb.h>
#include <chrono>
#include <string>

class TypeMapper {
public:
    // Convert GraphQL DateTime to Protobuf Timestamp
    static google::protobuf::Timestamp toProtobufTimestamp(int64_t unix_seconds) {
        google::protobuf::Timestamp timestamp;
        timestamp.set_seconds(unix_seconds);
        timestamp.set_nanos(0);
        return timestamp;
    }
    
    // Convert Protobuf Timestamp to GraphQL DateTime (Unix timestamp)
    static int64_t fromProtobufTimestamp(const google::protobuf::Timestamp& ts) {
        return ts.seconds();
    }
    
    // Handle optional fields
    template<typename T>
    static std::optional<T> handleOptional(const T& value, bool has_value) {
        return has_value ? std::optional<T>(value) : std::nullopt;
    }
    
    // Convert repeated fields to vectors
    template<typename PbType, typename GqlType>
    static std::vector<GqlType> convertRepeated(
        const google::protobuf::RepeatedPtrField<PbType>& pb_list,
        std::function<GqlType(const PbType&)> converter
    ) {
        std::vector<GqlType> result;
        for (const auto& item : pb_list) {
            result.push_back(converter(item));
        }
        return result;
    }
};
```

## Rust Implementation

### Rust Project Setup

```toml
# Cargo.toml
[package]
name = "graphql-protobuf-bridge"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1.35", features = ["full"] }
async-graphql = "7.0"
async-graphql-axum = "7.0"
tonic = "0.11"
prost = "0.12"
axum = "0.7"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"

[build-dependencies]
tonic-build = "0.11"
```

### Build Script

```rust
// build.rs
fn main() -> Result<(), Box<dyn std::error::Error>> {
    tonic_build::configure()
        .build_server(false)
        .build_client(true)
        .compile(&["proto/user.proto"], &["proto/"])?;
    Ok(())
}
```

### Protobuf Schema (Same as C++ version)

```protobuf
// proto/user.proto
// (Same schema as shown in C++ section)
```

### Rust GraphQL Schema with Protobuf Integration

```rust
use async_graphql::{Context, Object, Result, SimpleObject, ID};
use tonic::transport::Channel;

// Include generated protobuf code
pub mod proto {
    tonic::include_proto!("example");
}

use proto::user_service_client::UserServiceClient;
use proto::{GetUserRequest, ListUsersRequest};

// GraphQL Types
#[derive(SimpleObject, Clone)]
pub struct User {
    pub id: ID,
    pub name: String,
    pub email: String,
    pub roles: Vec<String>,
    pub created_at: i64,
}

// Convert from Protobuf to GraphQL
impl From<proto::User> for User {
    fn from(pb_user: proto::User) -> Self {
        User {
            id: ID(pb_user.id.to_string()),
            name: pb_user.name,
            email: pb_user.email,
            roles: pb_user.roles,
            created_at: pb_user.created_at,
        }
    }
}

// GraphQL Query Root
pub struct QueryRoot;

#[Object]
impl QueryRoot {
    /// Get a single user by ID
    async fn user(&self, ctx: &Context<'_>, id: ID) -> Result<Option<User>> {
        let mut client = ctx.data::<UserServiceClient<Channel>>()?.clone();
        
        let user_id: i64 = id.parse()
            .map_err(|_| "Invalid user ID")?;
        
        let request = tonic::Request::new(GetUserRequest {
            id: user_id,
        });
        
        let response = client.get_user(request).await?;
        let inner = response.into_inner();
        
        if inner.found {
            Ok(inner.user.map(User::from))
        } else {
            Ok(None)
        }
    }
    
    /// List users with pagination and filtering
    async fn users(
        &self,
        ctx: &Context<'_>,
        #[graphql(default = 1)] page: i32,
        #[graphql(default = 10)] page_size: i32,
        #[graphql(default)] filter: Option<String>,
    ) -> Result<UsersConnection> {
        let mut client = ctx.data::<UserServiceClient<Channel>>()?.clone();
        
        let request = tonic::Request::new(ListUsersRequest {
            page,
            page_size,
            filter: filter.unwrap_or_default(),
        });
        
        let response = client.list_users(request).await?;
        let inner = response.into_inner();
        
        Ok(UsersConnection {
            users: inner.users.into_iter().map(User::from).collect(),
            total_count: inner.total_count,
            page,
            page_size,
        })
    }
}

// Connection type for pagination
#[derive(SimpleObject)]
pub struct UsersConnection {
    pub users: Vec<User>,
    pub total_count: i32,
    pub page: i32,
    pub page_size: i32,
}

// GraphQL Mutation Root
pub struct MutationRoot;

#[Object]
impl MutationRoot {
    async fn placeholder(&self) -> bool {
        true
    }
}
```

### Complete Server Implementation

```rust
use async_graphql::{EmptySubscription, Schema};
use async_graphql_axum::{GraphQLRequest, GraphQLResponse};
use axum::{
    extract::State,
    response::IntoResponse,
    routing::post,
    Router,
};
use tonic::transport::Channel;
use std::net::SocketAddr;

mod graphql_schema;
use graphql_schema::{QueryRoot, MutationRoot};
use graphql_schema::proto::user_service_client::UserServiceClient;

type ServiceSchema = Schema<QueryRoot, MutationRoot, EmptySubscription>;

async fn graphql_handler(
    State(schema): State<ServiceSchema>,
    req: GraphQLRequest,
) -> GraphQLResponse {
    schema.execute(req.into_inner()).await.into()
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to gRPC backend service
    let grpc_channel = Channel::from_static("http://localhost:50051")
        .connect()
        .await?;
    
    let user_client = UserServiceClient::new(grpc_channel);
    
    // Build GraphQL schema
    let schema = Schema::build(QueryRoot, MutationRoot, EmptySubscription)
        .data(user_client)
        .finish();
    
    // Build Axum router
    let app = Router::new()
        .route("/graphql", post(graphql_handler))
        .with_state(schema);
    
    // Run server
    let addr = SocketAddr::from(([127, 0, 0, 1], 8000));
    println!("GraphQL server running on http://{}/graphql", addr);
    
    axum::Server::bind(&addr)
        .serve(app.into_make_service())
        .await?;
    
    Ok(())
}
```

### Advanced Type Mapping (Rust)

```rust
use async_graphql::{InputObject, Enum};
use prost_types::Timestamp;
use chrono::{DateTime, Utc};

// Complex type mapping example
#[derive(Enum, Copy, Clone, Eq, PartialEq)]
pub enum UserRole {
    Admin,
    User,
    Guest,
}

impl From<i32> for UserRole {
    fn from(value: i32) -> Self {
        match value {
            0 => UserRole::Admin,
            1 => UserRole::User,
            _ => UserRole::Guest,
        }
    }
}

impl From<UserRole> for i32 {
    fn from(role: UserRole) -> Self {
        match role {
            UserRole::Admin => 0,
            UserRole::User => 1,
            UserRole::Guest => 2,
        }
    }
}

// Timestamp conversion utilities
pub struct TimestampConverter;

impl TimestampConverter {
    pub fn from_protobuf(ts: &Timestamp) -> DateTime<Utc> {
        DateTime::from_timestamp(ts.seconds, ts.nanos as u32)
            .unwrap_or_default()
    }
    
    pub fn to_protobuf(dt: DateTime<Utc>) -> Timestamp {
        Timestamp {
            seconds: dt.timestamp(),
            nanos: dt.timestamp_subsec_nanos() as i32,
        }
    }
}

// Input type for mutations
#[derive(InputObject)]
pub struct CreateUserInput {
    pub name: String,
    pub email: String,
    pub roles: Vec<UserRole>,
}

impl From<CreateUserInput> for proto::CreateUserRequest {
    fn from(input: CreateUserInput) -> Self {
        proto::CreateUserRequest {
            name: input.name,
            email: input.email,
            roles: input.roles.into_iter().map(i32::from).collect(),
        }
    }
}
```

### Error Handling and Middleware

```rust
use async_graphql::{Error, ErrorExtensions};
use tonic::Status;

// Convert gRPC errors to GraphQL errors
pub fn grpc_to_graphql_error(status: Status) -> Error {
    let message = status.message().to_string();
    let code = match status.code() {
        tonic::Code::NotFound => "NOT_FOUND",
        tonic::Code::InvalidArgument => "BAD_REQUEST",
        tonic::Code::PermissionDenied => "FORBIDDEN",
        tonic::Code::Unauthenticated => "UNAUTHORIZED",
        _ => "INTERNAL_SERVER_ERROR",
    };
    
    Error::new(message).extend_with(|_, e| {
        e.set("code", code);
    })
}

// Result type alias
pub type GqlResult<T> = Result<T, Error>;

// Example usage in resolver
async fn protected_user(
    &self,
    ctx: &Context<'_>,
    id: ID,
) -> GqlResult<Option<User>> {
    let mut client = ctx.data::<UserServiceClient<Channel>>()?.clone();
    
    let request = tonic::Request::new(GetUserRequest {
        id: id.parse().map_err(|_| Error::new("Invalid ID"))?,
    });
    
    match client.get_user(request).await {
        Ok(response) => {
            let inner = response.into_inner();
            Ok(inner.user.map(User::from))
        }
        Err(status) => Err(grpc_to_graphql_error(status)),
    }
}
```

## Summary

**GraphQL and Protobuf Integration** creates a powerful architecture that combines GraphQL's client-friendly query interface with Protobuf's efficient serialization and RPC capabilities.

### Key Takeaways:

1. **Architecture Pattern**: GraphQL acts as an API gateway that translates client queries into Protobuf/gRPC calls to backend services

2. **Type Safety**: Both C++ and Rust implementations maintain strong type safety through schema-to-code generation for both GraphQL and Protobuf

3. **Performance Benefits**: Clients get GraphQL's flexibility while backend services communicate efficiently using binary Protobuf serialization

4. **Implementation Approaches**:
   - **C++**: Uses libraries like cppgraphqlgen with gRPC, requiring manual adapter layers
   - **Rust**: Leverages async-graphql with Tonic for more ergonomic, async-native integration

5. **Common Patterns**:
   - Adapter/bridge classes to convert between GraphQL and Protobuf types
   - Type mappers for handling different data representations (timestamps, enums, optionals)
   - Error translation from gRPC Status codes to GraphQL errors
   - Connection patterns for pagination

6. **Best Practices**:
   - Keep type conversion logic centralized and testable
   - Use code generation where possible to maintain schema consistency
   - Implement proper error handling and translation
   - Design GraphQL schemas that align with Protobuf service boundaries

This integration pattern is ideal for microservices architectures where you want to expose a unified GraphQL API while maintaining efficient, typed internal communication via Protobuf/gRPC.