# Versioning Strategies for APIs in Protocol Buffers

## Overview

API versioning in Protocol Buffers is crucial for maintaining backward and forward compatibility as your services evolve. Unlike REST APIs where versioning often appears in URLs (v1, v2), Protobuf versioning leverages the protocol's built-in compatibility features alongside organizational strategies.

## Core Versioning Approaches

### 1. **Package Versioning**

Package versioning involves including version identifiers in your `.proto` file package declarations. This creates completely separate namespaces for different API versions.

```protobuf
// v1/user_service.proto
syntax = "proto3";
package myapp.user.v1;

message User {
  int32 id = 1;
  string name = 2;
  string email = 3;
}

service UserService {
  rpc GetUser(GetUserRequest) returns (User);
}
```

```protobuf
// v2/user_service.proto
syntax = "proto3";
package myapp.user.v2;

message User {
  int32 id = 1;
  string name = 2;
  string email = 3;
  string phone = 4;  // New field
  UserRole role = 5; // New field
}

enum UserRole {
  USER_ROLE_UNSPECIFIED = 0;
  USER_ROLE_ADMIN = 1;
  USER_ROLE_MEMBER = 2;
}

service UserService {
  rpc GetUser(GetUserRequest) returns (User);
  rpc ListUsers(ListUsersRequest) returns (ListUsersResponse); // New RPC
}
```

### 2. **Major Version Bumps**

Major version bumps occur when you introduce breaking changes that cannot be handled through backward-compatible field additions:

- Removing fields
- Changing field types incompatibly
- Renaming services or RPCs
- Changing RPC signatures fundamentally

## Code Examples

### C/C++ Implementation

```cpp
// server_v1.cpp - Version 1 Service Implementation
#include "v1/user_service.pb.h"
#include "v1/user_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class UserServiceV1Impl final : public myapp::user::v1::UserService::Service {
  Status GetUser(ServerContext* context,
                 const myapp::user::v1::GetUserRequest* request,
                 myapp::user::v1::User* response) override {
    // V1 implementation - returns basic user info
    response->set_id(request->user_id());
    response->set_name("John Doe");
    response->set_email("john@example.com");
    return Status::OK;
  }
};

void RunV1Server() {
  std::string server_address("0.0.0.0:50051");
  UserServiceV1Impl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "V1 Server listening on " << server_address << std::endl;
  server->Wait();
}
```

```cpp
// server_v2.cpp - Version 2 Service Implementation
#include "v2/user_service.pb.h"
#include "v2/user_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>

class UserServiceV2Impl final : public myapp::user::v2::UserService::Service {
  Status GetUser(ServerContext* context,
                 const myapp::user::v2::GetUserRequest* request,
                 myapp::user::v2::User* response) override {
    // V2 implementation - returns enhanced user info
    response->set_id(request->user_id());
    response->set_name("John Doe");
    response->set_email("john@example.com");
    response->set_phone("+1234567890");  // New in V2
    response->set_role(myapp::user::v2::USER_ROLE_ADMIN);  // New in V2
    return Status::OK;
  }

  Status ListUsers(ServerContext* context,
                   const myapp::user::v2::ListUsersRequest* request,
                   myapp::user::v2::ListUsersResponse* response) override {
    // New RPC in V2
    for (int i = 0; i < request->page_size(); ++i) {
      auto* user = response->add_users();
      user->set_id(i);
      user->set_name("User " + std::to_string(i));
    }
    return Status::OK;
  }
};

void RunV2Server() {
  std::string server_address("0.0.0.0:50052");
  UserServiceV2Impl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "V2 Server listening on " << server_address << std::endl;
  server->Wait();
}
```

```cpp
// client_multi_version.cpp - Client supporting both versions
#include "v1/user_service.grpc.pb.h"
#include "v2/user_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>

class MultiVersionClient {
 private:
  std::unique_ptr<myapp::user::v1::UserService::Stub> stub_v1_;
  std::unique_ptr<myapp::user::v2::UserService::Stub> stub_v2_;

 public:
  MultiVersionClient(std::shared_ptr<grpc::Channel> channel_v1,
                     std::shared_ptr<grpc::Channel> channel_v2)
      : stub_v1_(myapp::user::v1::UserService::NewStub(channel_v1)),
        stub_v2_(myapp::user::v2::UserService::NewStub(channel_v2)) {}

  void GetUserV1(int32_t user_id) {
    myapp::user::v1::GetUserRequest request;
    request.set_user_id(user_id);
    myapp::user::v1::User response;
    grpc::ClientContext context;

    grpc::Status status = stub_v1_->GetUser(&context, request, &response);
    if (status.ok()) {
      std::cout << "V1 User: " << response.name() 
                << " (" << response.email() << ")" << std::endl;
    }
  }

  void GetUserV2(int32_t user_id) {
    myapp::user::v2::GetUserRequest request;
    request.set_user_id(user_id);
    myapp::user::v2::User response;
    grpc::ClientContext context;

    grpc::Status status = stub_v2_->GetUser(&context, request, &response);
    if (status.ok()) {
      std::cout << "V2 User: " << response.name() 
                << " (" << response.email() << ")"
                << " Phone: " << response.phone()
                << " Role: " << response.role() << std::endl;
    }
  }
};

int main() {
  auto channel_v1 = grpc::CreateChannel("localhost:50051", 
                                        grpc::InsecureChannelCredentials());
  auto channel_v2 = grpc::CreateChannel("localhost:50052", 
                                        grpc::InsecureChannelCredentials());
  
  MultiVersionClient client(channel_v1, channel_v2);
  
  client.GetUserV1(123);
  client.GetUserV2(123);
  
  return 0;
}
```

### Rust Implementation

```rust
// v1/user_service.proto compiled with tonic
// src/server_v1.rs
use tonic::{transport::Server, Request, Response, Status};

pub mod user_v1 {
    tonic::include_proto!("myapp.user.v1");
}

use user_v1::user_service_server::{UserService, UserServiceServer};
use user_v1::{GetUserRequest, User};

#[derive(Default)]
pub struct UserServiceV1;

#[tonic::async_trait]
impl UserService for UserServiceV1 {
    async fn get_user(
        &self,
        request: Request<GetUserRequest>,
    ) -> Result<Response<User>, Status> {
        let req = request.into_inner();
        
        let user = User {
            id: req.user_id,
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        };
        
        Ok(Response::new(user))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let service = UserServiceV1::default();
    
    println!("V1 Server listening on {}", addr);
    
    Server::builder()
        .add_service(UserServiceServer::new(service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

```rust
// src/server_v2.rs
use tonic::{transport::Server, Request, Response, Status};

pub mod user_v2 {
    tonic::include_proto!("myapp.user.v2");
}

use user_v2::user_service_server::{UserService, UserServiceServer};
use user_v2::{GetUserRequest, ListUsersRequest, ListUsersResponse, User, UserRole};

#[derive(Default)]
pub struct UserServiceV2;

#[tonic::async_trait]
impl UserService for UserServiceV2 {
    async fn get_user(
        &self,
        request: Request<GetUserRequest>,
    ) -> Result<Response<User>, Status> {
        let req = request.into_inner();
        
        let user = User {
            id: req.user_id,
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
            phone: "+1234567890".to_string(),  // New in V2
            role: UserRole::Admin as i32,       // New in V2
        };
        
        Ok(Response::new(user))
    }
    
    async fn list_users(
        &self,
        request: Request<ListUsersRequest>,
    ) -> Result<Response<ListUsersResponse>, Status> {
        let req = request.into_inner();
        
        let users: Vec<User> = (0..req.page_size)
            .map(|i| User {
                id: i,
                name: format!("User {}", i),
                email: format!("user{}@example.com", i),
                phone: String::new(),
                role: UserRole::Member as i32,
            })
            .collect();
        
        Ok(Response::new(ListUsersResponse { users }))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50052".parse()?;
    let service = UserServiceV2::default();
    
    println!("V2 Server listening on {}", addr);
    
    Server::builder()
        .add_service(UserServiceServer::new(service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

```rust
// src/client_multi_version.rs
use tonic::Request;

pub mod user_v1 {
    tonic::include_proto!("myapp.user.v1");
}

pub mod user_v2 {
    tonic::include_proto!("myapp.user.v2");
}

use user_v1::user_service_client::UserServiceClient as UserServiceClientV1;
use user_v2::user_service_client::UserServiceClient as UserServiceClientV2;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to V1 service
    let mut client_v1 = UserServiceClientV1::connect("http://[::1]:50051").await?;
    
    let request_v1 = Request::new(user_v1::GetUserRequest {
        user_id: 123,
    });
    
    let response_v1 = client_v1.get_user(request_v1).await?;
    let user_v1 = response_v1.into_inner();
    
    println!("V1 User: {} ({})", user_v1.name, user_v1.email);
    
    // Connect to V2 service
    let mut client_v2 = UserServiceClientV2::connect("http://[::1]:50052").await?;
    
    let request_v2 = Request::new(user_v2::GetUserRequest {
        user_id: 123,
    });
    
    let response_v2 = client_v2.get_user(request_v2).await?;
    let user_v2 = response_v2.into_inner();
    
    println!(
        "V2 User: {} ({}) Phone: {} Role: {}",
        user_v2.name, user_v2.email, user_v2.phone, user_v2.role
    );
    
    // Use new V2 RPC
    let list_request = Request::new(user_v2::ListUsersRequest {
        page_size: 5,
        page_token: String::new(),
    });
    
    let list_response = client_v2.list_users(list_request).await?;
    println!("V2 Listed {} users", list_response.into_inner().users.len());
    
    Ok(())
}
```

## Maintaining Multiple API Versions

### Strategy 1: Parallel Services

Run V1 and V2 services simultaneously on different ports or endpoints:

```cpp
// Combined server running both versions
void RunMultiVersionServer() {
  UserServiceV1Impl service_v1;
  UserServiceV2Impl service_v2;
  
  ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
  builder.AddListeningPort("0.0.0.0:50052", grpc::InsecureServerCredentials());
  builder.RegisterService(&service_v1);
  builder.RegisterService(&service_v2);
  
  std::unique_ptr<Server> server(builder.BuildAndStart());
  server->Wait();
}
```

### Strategy 2: Adapter Pattern

Create adapters to translate between versions:

```rust
// Adapter converting V1 requests to V2 internally
pub struct V1ToV2Adapter {
    v2_service: Arc<UserServiceV2>,
}

impl V1ToV2Adapter {
    async fn get_user_v1(&self, req: user_v1::GetUserRequest) 
        -> Result<user_v1::User, Status> {
        // Call V2 service
        let v2_req = user_v2::GetUserRequest {
            user_id: req.user_id,
        };
        
        let v2_user = self.v2_service.get_user(Request::new(v2_req)).await?;
        let v2_user = v2_user.into_inner();
        
        // Convert V2 response to V1 (dropping new fields)
        Ok(user_v1::User {
            id: v2_user.id,
            name: v2_user.name,
            email: v2_user.email,
        })
    }
}
```

## Best Practices

1. **Use semantic versioning** - v1, v2, v3 for major breaking changes
2. **Deprecate gracefully** - Give clients time to migrate before removing old versions
3. **Document breaking changes** - Clearly communicate what changes between versions
4. **Minimize major bumps** - Use backward-compatible field additions when possible
5. **Version at the package level** - Keep entire API surfaces versioned together
6. **Monitor version usage** - Track which clients use which versions to plan deprecation

## Summary

Protocol Buffer API versioning strategies balance compatibility with evolution. Package versioning creates clear separation between major API versions, allowing services to maintain multiple versions simultaneously while clients migrate. Major version bumps should be reserved for truly breaking changes, while minor enhancements use Protobuf's backward compatibility through optional fields and reserved numbers. The combination of C/C++ and Rust examples demonstrates that these versioning patterns work across language ecosystems, with the protocol buffer schema providing a language-neutral contract. Successful API evolution requires planning deprecation cycles, monitoring version adoption, and providing clear migration paths for clients.