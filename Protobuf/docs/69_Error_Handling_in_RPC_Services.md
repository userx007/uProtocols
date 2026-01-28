# Error Handling in RPC Services with Protocol Buffers

## Overview

Error handling in RPC (Remote Procedure Call) services is critical for building robust, maintainable distributed systems. Protocol Buffers provides standardized mechanisms through `google.rpc.Status` for communicating errors between clients and servers in a structured, language-agnostic way.

## Core Concepts

### 1. **The google.rpc.Status Message**

The `google.rpc.Status` is the standard error model in gRPC and Protocol Buffers:

```protobuf
syntax = "proto3";

package google.rpc;

message Status {
  // The status code (see google.rpc.Code)
  int32 code = 1;
  
  // A developer-facing error message
  string message = 2;
  
  // Additional error details
  repeated google.protobuf.Any details = 3;
}
```

### 2. **Standard Status Codes**

The `google.rpc.Code` enum defines standard error codes:
- `OK` (0): Success
- `CANCELLED` (1): Operation cancelled
- `INVALID_ARGUMENT` (3): Invalid client input
- `NOT_FOUND` (5): Resource not found
- `PERMISSION_DENIED` (7): Permission denied
- `UNAUTHENTICATED` (16): Authentication required
- `INTERNAL` (13): Server-side error
- And many more...

## Defining Error Messages

### Example Service Definition

```protobuf
syntax = "proto3";

package example;

import "google/rpc/status.proto";
import "google/protobuf/any.proto";

// Custom error detail messages
message ValidationError {
  string field = 1;
  string description = 2;
}

message QuotaExceeded {
  string resource = 1;
  int64 limit = 2;
  int64 current_usage = 3;
}

service UserService {
  rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
}

message CreateUserRequest {
  string username = 1;
  string email = 2;
}

message CreateUserResponse {
  oneof result {
    User user = 1;
    google.rpc.Status error = 2;
  }
}

message User {
  string id = 1;
  string username = 2;
  string email = 3;
}
```

## C/C++ Implementation

### Server-Side Error Handling

```cpp
#include <grpcpp/grpcpp.h>
#include <google/rpc/status.pb.h>
#include <google/rpc/error_details.pb.h>
#include "user_service.grpc.pb.h"

class UserServiceImpl final : public example::UserService::Service {
public:
    grpc::Status CreateUser(
        grpc::ServerContext* context,
        const example::CreateUserRequest* request,
        example::CreateUserResponse* response) override {
        
        // Validate input
        if (request->username().empty()) {
            return CreateValidationError(
                "username", 
                "Username cannot be empty"
            );
        }
        
        if (!IsValidEmail(request->email())) {
            return CreateValidationError(
                "email", 
                "Invalid email format"
            );
        }
        
        // Check if user already exists
        if (UserExists(request->username())) {
            return grpc::Status(
                grpc::StatusCode::ALREADY_EXISTS,
                "User already exists with this username"
            );
        }
        
        // Create user
        auto* user = response->mutable_user();
        user->set_id(GenerateId());
        user->set_username(request->username());
        user->set_email(request->email());
        
        return grpc::Status::OK;
    }

private:
    grpc::Status CreateValidationError(
        const std::string& field, 
        const std::string& description) {
        
        google::rpc::Status status;
        status.set_code(grpc::StatusCode::INVALID_ARGUMENT);
        status.set_message("Validation failed");
        
        // Add detailed error information
        example::ValidationError validation_error;
        validation_error.set_field(field);
        validation_error.set_description(description);
        
        google::protobuf::Any* detail = status.add_details();
        detail->PackFrom(validation_error);
        
        // Convert to gRPC status
        return grpc::Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            status.message()
        );
    }
    
    bool IsValidEmail(const std::string& email) {
        return email.find('@') != std::string::npos;
    }
    
    bool UserExists(const std::string& username) {
        // Check database
        return false;
    }
    
    std::string GenerateId() {
        return "user_" + std::to_string(rand());
    }
};
```

### Client-Side Error Handling

```cpp
#include <grpcpp/grpcpp.h>
#include <google/rpc/status.pb.h>
#include "user_service.grpc.pb.h"
#include <iostream>

class UserClient {
public:
    UserClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(example::UserService::NewStub(channel)) {}
    
    void CreateUser(const std::string& username, const std::string& email) {
        example::CreateUserRequest request;
        request.set_username(username);
        request.set_email(email);
        
        example::CreateUserResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->CreateUser(&context, request, &response);
        
        if (status.ok()) {
            if (response.has_user()) {
                std::cout << "User created successfully: " 
                          << response.user().id() << std::endl;
            } else if (response.has_error()) {
                HandleDetailedError(response.error());
            }
        } else {
            HandleGrpcError(status);
        }
    }

private:
    void HandleGrpcError(const grpc::Status& status) {
        std::cerr << "RPC failed: " << status.error_code() 
                  << " - " << status.error_message() << std::endl;
        
        switch (status.error_code()) {
            case grpc::StatusCode::INVALID_ARGUMENT:
                std::cerr << "Invalid input provided" << std::endl;
                break;
            case grpc::StatusCode::ALREADY_EXISTS:
                std::cerr << "Resource already exists" << std::endl;
                break;
            case grpc::StatusCode::NOT_FOUND:
                std::cerr << "Resource not found" << std::endl;
                break;
            case grpc::StatusCode::PERMISSION_DENIED:
                std::cerr << "Permission denied" << std::endl;
                break;
            default:
                std::cerr << "Unknown error occurred" << std::endl;
        }
    }
    
    void HandleDetailedError(const google::rpc::Status& error_status) {
        std::cerr << "Detailed error: " << error_status.message() << std::endl;
        
        for (const auto& detail : error_status.details()) {
            if (detail.Is<example::ValidationError>()) {
                example::ValidationError validation_error;
                detail.UnpackTo(&validation_error);
                std::cerr << "Validation error on field '" 
                          << validation_error.field() << "': "
                          << validation_error.description() << std::endl;
            }
        }
    }
    
    std::unique_ptr<example::UserService::Stub> stub_;
};
```

## Rust Implementation

### Dependencies (Cargo.toml)

```toml
[dependencies]
tonic = "0.11"
prost = "0.12"
tokio = { version = "1.0", features = ["macros", "rt-multi-thread"] }

[build-dependencies]
tonic-build = "0.11"
```

### Proto Definition

```protobuf
syntax = "proto3";

package example;

service UserService {
  rpc CreateUser(CreateUserRequest) returns (CreateUserResponse);
  rpc GetUser(GetUserRequest) returns (GetUserResponse);
}

message CreateUserRequest {
  string username = 1;
  string email = 2;
}

message CreateUserResponse {
  string id = 1;
  string username = 2;
  string email = 3;
}

message GetUserRequest {
  string id = 1;
}

message GetUserResponse {
  string id = 1;
  string username = 2;
  string email = 3;
}
```

### Server Implementation

```rust
use tonic::{transport::Server, Request, Response, Status, Code};

pub mod example {
    tonic::include_proto!("example");
}

use example::user_service_server::{UserService, UserServiceServer};
use example::{CreateUserRequest, CreateUserResponse, GetUserRequest, GetUserResponse};

#[derive(Debug, Default)]
pub struct UserServiceImpl {}

impl UserServiceImpl {
    fn validate_email(email: &str) -> Result<(), Status> {
        if !email.contains('@') {
            return Err(Status::new(
                Code::InvalidArgument,
                format!("Invalid email format: {}", email),
            ));
        }
        Ok(())
    }
    
    fn validate_username(username: &str) -> Result<(), Status> {
        if username.is_empty() {
            return Err(Status::new(
                Code::InvalidArgument,
                "Username cannot be empty",
            ));
        }
        
        if username.len() < 3 {
            return Err(Status::new(
                Code::InvalidArgument,
                "Username must be at least 3 characters",
            ));
        }
        
        Ok(())
    }
}

#[tonic::async_trait]
impl UserService for UserServiceImpl {
    async fn create_user(
        &self,
        request: Request<CreateUserRequest>,
    ) -> Result<Response<CreateUserResponse>, Status> {
        let req = request.into_inner();
        
        // Validate username
        Self::validate_username(&req.username)?;
        
        // Validate email
        Self::validate_email(&req.email)?;
        
        // Simulate checking if user exists
        if req.username == "admin" {
            return Err(Status::new(
                Code::AlreadyExists,
                "User with this username already exists",
            ));
        }
        
        // Create user
        let response = CreateUserResponse {
            id: format!("user_{}", uuid::Uuid::new_v4()),
            username: req.username,
            email: req.email,
        };
        
        Ok(Response::new(response))
    }
    
    async fn get_user(
        &self,
        request: Request<GetUserRequest>,
    ) -> Result<Response<GetUserResponse>, Status> {
        let req = request.into_inner();
        
        if req.id.is_empty() {
            return Err(Status::new(
                Code::InvalidArgument,
                "User ID cannot be empty",
            ));
        }
        
        // Simulate user not found
        if !req.id.starts_with("user_") {
            return Err(Status::new(
                Code::NotFound,
                format!("User not found: {}", req.id),
            ));
        }
        
        // Return mock user
        let response = GetUserResponse {
            id: req.id,
            username: "john_doe".to_string(),
            email: "john@example.com".to_string(),
        };
        
        Ok(Response::new(response))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:50051".parse()?;
    let user_service = UserServiceImpl::default();
    
    println!("UserService listening on {}", addr);
    
    Server::builder()
        .add_service(UserServiceServer::new(user_service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### Client Implementation

```rust
use tonic::Request;

pub mod example {
    tonic::include_proto!("example");
}

use example::user_service_client::UserServiceClient;
use example::{CreateUserRequest, GetUserRequest};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = UserServiceClient::connect("http://127.0.0.1:50051").await?;
    
    // Successful user creation
    match create_user(&mut client, "alice", "alice@example.com").await {
        Ok(response) => {
            println!("User created: {} - {}", response.id, response.username);
        }
        Err(e) => {
            handle_error(e);
        }
    }
    
    // Try to create duplicate user (will fail)
    match create_user(&mut client, "admin", "admin@example.com").await {
        Ok(_) => println!("User created"),
        Err(e) => {
            handle_error(e);
        }
    }
    
    // Try with invalid email
    match create_user(&mut client, "bob", "invalid-email").await {
        Ok(_) => println!("User created"),
        Err(e) => {
            handle_error(e);
        }
    }
    
    // Get user
    match get_user(&mut client, "user_123").await {
        Ok(response) => {
            println!("User found: {} - {}", response.username, response.email);
        }
        Err(e) => {
            handle_error(e);
        }
    }
    
    // Try to get non-existent user
    match get_user(&mut client, "invalid_id").await {
        Ok(_) => println!("User found"),
        Err(e) => {
            handle_error(e);
        }
    }
    
    Ok(())
}

async fn create_user(
    client: &mut UserServiceClient<tonic::transport::Channel>,
    username: &str,
    email: &str,
) -> Result<example::CreateUserResponse, tonic::Status> {
    let request = Request::new(CreateUserRequest {
        username: username.to_string(),
        email: email.to_string(),
    });
    
    let response = client.create_user(request).await?;
    Ok(response.into_inner())
}

async fn get_user(
    client: &mut UserServiceClient<tonic::transport::Channel>,
    id: &str,
) -> Result<example::GetUserResponse, tonic::Status> {
    let request = Request::new(GetUserRequest {
        id: id.to_string(),
    });
    
    let response = client.get_user(request).await?;
    Ok(response.into_inner())
}

fn handle_error(status: tonic::Status) {
    eprintln!("RPC Error: {} - {}", status.code(), status.message());
    
    match status.code() {
        tonic::Code::InvalidArgument => {
            eprintln!("Invalid input: {}", status.message());
        }
        tonic::Code::AlreadyExists => {
            eprintln!("Resource already exists: {}", status.message());
        }
        tonic::Code::NotFound => {
            eprintln!("Resource not found: {}", status.message());
        }
        tonic::Code::PermissionDenied => {
            eprintln!("Permission denied: {}", status.message());
        }
        tonic::Code::Unauthenticated => {
            eprintln!("Authentication required: {}", status.message());
        }
        _ => {
            eprintln!("Unknown error: {}", status.message());
        }
    }
}
```

## Best Practices

1. **Use Standard Status Codes**: Stick to `google.rpc.Code` values for consistency
2. **Provide Context**: Include meaningful error messages for developers
3. **Add Details When Helpful**: Use the `details` field for structured error information
4. **Don't Expose Internals**: Avoid leaking sensitive server details in error messages
5. **Log Server-Side**: Always log detailed errors server-side for debugging
6. **Handle Retryable Errors**: Distinguish between transient and permanent failures
7. **Use Idempotency Tokens**: For operations that might be retried
8. **Document Error Cases**: Clearly document which errors each RPC can return

## Summary

Error handling in RPC services using Protocol Buffers centers around the `google.rpc.Status` message, which provides a standardized way to communicate errors across language boundaries. The system uses numeric status codes from `google.rpc.Code` for machine-readable error types, human-readable messages for developers, and optional structured details via the `Any` type for additional context.

Both C++ and Rust implementations leverage their respective gRPC frameworks (grpcpp and Tonic) to propagate these errors naturally. In C++, errors are returned as `grpc::Status` objects, while Rust uses `Result<T, tonic::Status>` types that integrate with the language's error handling idioms. Custom error details can be packed into the status using protocol buffer serialization, allowing clients to extract rich, structured information about failures. This approach enables building resilient distributed systems with clear error semantics, proper observability, and good user experience.