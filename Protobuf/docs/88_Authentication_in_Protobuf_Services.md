# Authentication in Protobuf Services

## Key Topics Covered:

1. **Overview & Concepts** - Understanding gRPC metadata, authentication mechanisms, and interceptors

2. **Protocol Buffer Definitions** - Complete .proto files for authentication and protected services

3. **C++ Implementation**:
   - Custom JWT authentication metadata processor
   - Server-side authentication with interceptors
   - Client-side credentials plugin
   - Manual metadata addition
   - Production-ready examples with SSL/TLS

4. **Rust Implementation**:
   - JWT-based authentication with `jsonwebtoken`
   - Synchronous and asynchronous interceptors
   - Token management and refresh patterns
   - Client interceptors for automatic token injection
   - Advanced async authentication with database validation

5. **Best Practices** - TLS usage, token expiration, secure secret storage, rate limiting, logging, and error handling

6. **Common Patterns** - Per-service authentication, method-level authorization, and context propagation across services

The document includes working code examples demonstrating how authentication tokens and credentials are integrated into gRPC service calls, with complete implementations showing both server-side validation and client-side credential injection patterns.

## Overview

Authentication in Protobuf-based gRPC services is a critical security mechanism that verifies the identity of clients making RPC calls. Unlike traditional REST APIs, gRPC provides multiple built-in and extensible authentication approaches that leverage HTTP/2's metadata capabilities to transmit credentials securely alongside service requests.

## Key Concepts

### 1. gRPC Metadata
Metadata in gRPC is analogous to HTTP headers - it consists of key-value pairs sent alongside RPC calls. Metadata is implemented using HTTP/2 headers and is the primary mechanism for transmitting authentication credentials.

**Characteristics:**
- Keys are ASCII strings (case-insensitive)
- Values can be ASCII strings or binary data
- Keys cannot start with `grpc-` (reserved prefix)
- Two types: Initial metadata (headers) and Trailing metadata (trailers)
- Size limit typically 8 KiB (server-dependent)

### 2. Authentication Mechanisms

#### Built-in Options:
- **SSL/TLS**: Channel-level encryption and server authentication
- **Token-based**: OAuth2 access tokens for Google services
- **ALTS**: Application Layer Transport Security (Google Cloud)

#### Custom Options:
- **JWT (JSON Web Tokens)**: Industry-standard token authentication
- **API Keys**: Simple shared secret authentication
- **Basic Auth**: Username/password (not recommended for production)
- **Custom Credentials Plugins**: Fully extensible authentication

### 3. Interceptors

Interceptors are middleware components that intercept RPC calls before they reach the service handler. They provide a powerful mechanism for:
- Extracting and validating authentication tokens
- Adding authentication metadata to requests
- Implementing authorization logic
- Cross-cutting concerns (logging, metrics, etc.)

**Types:**
- **Unary Interceptors**: For single request-response calls
- **Stream Interceptors**: For streaming RPCs
- **Client Interceptors**: Add credentials to outgoing requests
- **Server Interceptors**: Validate credentials on incoming requests

## Authentication Workflow

```
Client                          Server
  |                               |
  |  1. Create credentials        |
  |     (JWT, API key, etc.)     |
  |                               |
  |  2. Add to metadata           |
  |     via interceptor           |
  |                               |
  |  3. Send RPC request -------> |
  |     with metadata             |
  |                               |  4. Server interceptor
  |                               |     extracts metadata
  |                               |
  |                               |  5. Validate credentials
  |                               |     (check signature, expiry)
  |                               |
  |                               |  6. Authorize request
  |                               |     (check permissions)
  |                               |
  | <------- 7. RPC response      |
  |     or Status::UNAUTHENTICATED|
  |                               |
```

## Protocol Buffer Definitions

First, let's define our authentication service in Protocol Buffers:

```protobuf
// auth.proto
syntax = "proto3";

package auth;

// Authentication service
service AuthService {
    rpc Login(LoginRequest) returns (LoginResponse);
    rpc Register(RegisterRequest) returns (RegisterResponse);
    rpc RefreshToken(RefreshRequest) returns (TokenResponse);
}

// Login request
message LoginRequest {
    string username = 1;
    string password = 2;
}

// Login response with JWT token
message LoginResponse {
    string access_token = 1;
    string refresh_token = 2;
    int64 expires_in = 3;
}

// Registration request
message RegisterRequest {
    string username = 1;
    string email = 2;
    string password = 3;
}

message RegisterResponse {
    string user_id = 1;
    string access_token = 2;
}

// Token refresh
message RefreshRequest {
    string refresh_token = 1;
}

message TokenResponse {
    string access_token = 1;
    int64 expires_in = 2;
}
```

For a protected service that requires authentication:

```protobuf
// greeting.proto
syntax = "proto3";

package greeting;

service GreetingService {
    // Requires authentication
    rpc SayHello(HelloRequest) returns (HelloResponse);
    
    // Requires authentication
    rpc SayGoodbye(GoodbyeRequest) returns (GoodbyeResponse);
}

message HelloRequest {
    string name = 1;
}

message HelloResponse {
    string message = 1;
}

message GoodbyeRequest {
    string name = 1;
}

message GoodbyeResponse {
    string message = 1;
}
```

## C++ Implementation

### Server-Side Authentication with JWT

#### 1. Custom Authentication Interceptor

```cpp
// auth_interceptor.h
#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/auth_metadata_processor.h>
#include <jwt-cpp/jwt.h>
#include <string>
#include <memory>

class JWTAuthMetadataProcessor : public grpc::AuthMetadataProcessor {
public:
    explicit JWTAuthMetadataProcessor(const std::string& secret_key)
        : secret_key_(secret_key) {}

    grpc::Status Process(
        const grpc::AuthMetadataProcessor::InputMetadata& auth_metadata,
        grpc::AuthContext* context,
        grpc::AuthMetadataProcessor::OutputMetadata* consumed_auth_metadata,
        grpc::AuthMetadataProcessor::OutputMetadata* response_metadata) override {
        
        // Extract authorization header
        auto auth_header = auth_metadata.find("authorization");
        if (auth_header == auth_metadata.end()) {
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, 
                              "Missing authorization header");
        }

        std::string token = std::string(auth_header->second.data(), 
                                       auth_header->second.size());
        
        // Remove "Bearer " prefix if present
        if (token.substr(0, 7) == "Bearer ") {
            token = token.substr(7);
        }

        try {
            // Verify JWT token
            auto verifier = jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{secret_key_})
                .with_issuer("auth-service");
            
            auto decoded = jwt::decode(token);
            verifier.verify(decoded);

            // Extract user information from claims
            std::string user_id = decoded.get_payload_claim("user_id").as_string();
            std::string username = decoded.get_payload_claim("username").as_string();

            // Add user information to auth context
            context->AddProperty("user_id", user_id);
            context->AddProperty("username", username);
            
            // Mark authorization header as consumed
            consumed_auth_metadata->insert(std::make_pair(
                std::string(auth_header->first.data(), auth_header->first.size()),
                std::string(auth_header->second.data(), auth_header->second.size())
            ));

            return grpc::Status::OK;

        } catch (const std::exception& e) {
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, 
                              std::string("Invalid token: ") + e.what());
        }
    }

private:
    std::string secret_key_;
};
```

#### 2. Server Setup with Authentication

```cpp
// server.cpp
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include "greeting.grpc.pb.h"
#include "auth_interceptor.h"

class GreetingServiceImpl final : public greeting::GreetingService::Service {
public:
    grpc::Status SayHello(
        grpc::ServerContext* context,
        const greeting::HelloRequest* request,
        greeting::HelloResponse* response) override {
        
        // Access authenticated user information
        auto auth_context = context->auth_context();
        std::string user_id;
        
        for (const auto& property : auth_context->GetPeerIdentity()) {
            if (property.first == "user_id") {
                user_id = std::string(property.second.data(), property.second.size());
                break;
            }
        }

        // Alternative: access from properties
        auto user_id_properties = auth_context->FindPropertyValues("user_id");
        if (!user_id_properties.empty()) {
            user_id = std::string(user_id_properties[0].data(), 
                                 user_id_properties[0].size());
        }

        std::string message = "Hello " + request->name() + 
                            "! (User ID: " + user_id + ")";
        response->set_message(message);
        
        return grpc::Status::OK;
    }

    grpc::Status SayGoodbye(
        grpc::ServerContext* context,
        const greeting::GoodbyeRequest* request,
        greeting::GoodbyeResponse* response) override {
        
        response->set_message("Goodbye " + request->name() + "!");
        return grpc::Status::OK;
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    GreetingServiceImpl service;

    grpc::ServerBuilder builder;
    
    // Create credentials with authentication processor
    auto creds = grpc::InsecureServerCredentials();
    
    // For production, use SSL/TLS:
    // grpc::SslServerCredentialsOptions ssl_opts;
    // ssl_opts.pem_root_certs = "";
    // ssl_opts.pem_key_cert_pairs.push_back({server_key, server_cert});
    // auto creds = grpc::SslServerCredentials(ssl_opts);

    // Set up authentication processor
    auto auth_processor = std::make_shared<JWTAuthMetadataProcessor>("your-secret-key");
    creds->SetAuthMetadataProcessor(auth_processor);
    
    builder.AddListeningPort(server_address, creds);
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}
```

### Client-Side Authentication

#### 1. Custom Credentials Plugin

```cpp
// auth_credentials.h
#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>

class JWTCredentialsPlugin : public grpc::MetadataCredentialsPlugin {
public:
    explicit JWTCredentialsPlugin(const std::string& token) 
        : token_(token) {}

    grpc::Status GetMetadata(
        grpc::string_ref service_url,
        grpc::string_ref method_name,
        const grpc::AuthContext& channel_auth_context,
        std::multimap<grpc::string, grpc::string>* metadata) override {
        
        // Add authorization header with Bearer token
        metadata->insert(std::make_pair("authorization", "Bearer " + token_));
        
        return grpc::Status::OK;
    }

    void UpdateToken(const std::string& new_token) {
        token_ = new_token;
    }

private:
    std::string token_;
};
```

#### 2. Client Implementation

```cpp
// client.cpp
#include <grpcpp/grpcpp.h>
#include "greeting.grpc.pb.h"
#include "auth_credentials.h"
#include <memory>
#include <string>

class GreetingClient {
public:
    GreetingClient(const std::string& server_address, const std::string& token) {
        // Create channel credentials
        auto channel_creds = grpc::InsecureChannelCredentials();
        
        // For production with SSL/TLS:
        // grpc::SslCredentialsOptions ssl_opts;
        // ssl_opts.pem_root_certs = root_cert;
        // auto channel_creds = grpc::SslCredentials(ssl_opts);

        // Create call credentials with JWT token
        auto call_creds = grpc::MetadataCredentialsFromPlugin(
            std::make_unique<JWTCredentialsPlugin>(token)
        );

        // Compose credentials
        auto creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);

        // Create channel
        channel_ = grpc::CreateChannel(server_address, creds);
        stub_ = greeting::GreetingService::NewStub(channel_);
    }

    std::string SayHello(const std::string& name) {
        greeting::HelloRequest request;
        request.set_name(name);

        greeting::HelloResponse response;
        grpc::ClientContext context;
        
        // Optional: Set deadline
        auto deadline = std::chrono::system_clock::now() + 
                       std::chrono::seconds(10);
        context.set_deadline(deadline);

        grpc::Status status = stub_->SayHello(&context, request, &response);

        if (status.ok()) {
            return response.message();
        } else {
            std::cout << "RPC failed: " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

    // Alternative: Manual metadata addition (without credentials plugin)
    std::string SayHelloManual(const std::string& name, const std::string& token) {
        greeting::HelloRequest request;
        request.set_name(name);

        greeting::HelloResponse response;
        grpc::ClientContext context;
        
        // Add metadata manually
        context.AddMetadata("authorization", "Bearer " + token);

        grpc::Status status = stub_->SayHello(&context, request, &response);

        if (status.ok()) {
            return response.message();
        } else {
            return "RPC failed: " + status.error_message();
        }
    }

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<greeting::GreetingService::Stub> stub_;
};

int main(int argc, char** argv) {
    // Obtain token from authentication service (simplified)
    std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...";
    
    GreetingClient client("localhost:50051", token);
    
    std::string reply = client.SayHello("World");
    std::cout << "Response: " << reply << std::endl;

    return 0;
}
```

## Rust Implementation

### Server-Side Authentication with Tonic

#### 1. Dependencies (Cargo.toml)

```toml
[dependencies]
tonic = "0.11"
prost = "0.12"
tokio = { version = "1.0", features = ["macros", "rt-multi-thread"] }
jsonwebtoken = "9.2"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
chrono = "0.4"

[build-dependencies]
tonic-build = "0.11"
```

#### 2. Authentication Interceptor

```rust
// auth.rs
use jsonwebtoken::{decode, encode, DecodingKey, EncodingKey, Header, Validation};
use serde::{Deserialize, Serialize};
use tonic::{Request, Status};
use chrono::{Duration, Utc};

#[derive(Debug, Serialize, Deserialize)]
pub struct Claims {
    pub sub: String,      // Subject (user ID)
    pub username: String,
    pub exp: usize,       // Expiry time
    pub iat: usize,       // Issued at
}

impl Claims {
    pub fn new(user_id: String, username: String) -> Self {
        let iat = Utc::now();
        let exp = iat + Duration::hours(24);
        
        Claims {
            sub: user_id,
            username,
            iat: iat.timestamp() as usize,
            exp: exp.timestamp() as usize,
        }
    }
}

pub struct AuthService {
    secret: String,
}

impl AuthService {
    pub fn new(secret: String) -> Self {
        AuthService { secret }
    }

    pub fn generate_token(&self, user_id: String, username: String) 
        -> Result<String, Status> {
        let claims = Claims::new(user_id, username);
        
        encode(
            &Header::default(),
            &claims,
            &EncodingKey::from_secret(self.secret.as_bytes()),
        )
        .map_err(|e| Status::internal(format!("Token generation failed: {}", e)))
    }

    pub fn verify_token(&self, token: &str) -> Result<Claims, Status> {
        decode::<Claims>(
            token,
            &DecodingKey::from_secret(self.secret.as_bytes()),
            &Validation::default(),
        )
        .map(|data| data.claims)
        .map_err(|e| Status::unauthenticated(format!("Invalid token: {}", e)))
    }
}

// Interceptor function
pub fn check_auth(mut req: Request<()>) -> Result<Request<()>, Status> {
    let token = req
        .metadata()
        .get("authorization")
        .ok_or_else(|| Status::unauthenticated("Missing authorization header"))?
        .to_str()
        .map_err(|_| Status::unauthenticated("Invalid authorization header"))?;

    // Remove "Bearer " prefix
    let token = token.strip_prefix("Bearer ")
        .ok_or_else(|| Status::unauthenticated("Invalid token format"))?;

    // Verify token (simplified - in production, inject AuthService)
    let auth_service = AuthService::new("your-secret-key".to_string());
    let claims = auth_service.verify_token(token)?;

    // Add claims to request extensions
    req.extensions_mut().insert(claims);

    Ok(req)
}
```

#### 3. Server Implementation

```rust
// server.rs
use tonic::{transport::Server, Request, Response, Status};
use greeting::greeting_service_server::{GreetingService, GreetingServiceServer};
use greeting::{HelloRequest, HelloResponse, GoodbyeRequest, GoodbyeResponse};

mod auth;
use auth::{check_auth, Claims};

pub mod greeting {
    tonic::include_proto!("greeting");
}

#[derive(Debug, Default)]
pub struct MyGreetingService;

#[tonic::async_trait]
impl GreetingService for MyGreetingService {
    async fn say_hello(
        &self,
        request: Request<HelloRequest>,
    ) -> Result<Response<HelloResponse>, Status> {
        // Extract claims from request extensions
        let claims = request
            .extensions()
            .get::<Claims>()
            .ok_or_else(|| Status::unauthenticated("No authentication data"))?;

        let message = format!(
            "Hello {}! (User: {}, ID: {})",
            request.get_ref().name,
            claims.username,
            claims.sub
        );

        Ok(Response::new(HelloResponse { message }))
    }

    async fn say_goodbye(
        &self,
        request: Request<GoodbyeRequest>,
    ) -> Result<Response<GoodbyeResponse>, Status> {
        let claims = request.extensions().get::<Claims>().unwrap();
        
        let message = format!(
            "Goodbye {}! See you later, {}",
            request.get_ref().name,
            claims.username
        );

        Ok(Response::new(GoodbyeResponse { message }))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let greeting_service = MyGreetingService::default();

    println!("GreetingServer listening on {}", addr);

    Server::builder()
        .add_service(
            GreetingServiceServer::with_interceptor(greeting_service, check_auth)
        )
        .serve(addr)
        .await?;

    Ok(())
}
```

#### 4. Advanced: Async Interceptor with Database Validation

```rust
// advanced_auth.rs
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use tonic::{Request, Status};
use tonic_async_interceptor::AsyncInterceptor;

// Mock user database
pub struct UserDatabase {
    // In production, this would be a real database connection
}

impl UserDatabase {
    pub async fn verify_token(&self, token: &str) -> Result<String, String> {
        // Simulate async database lookup
        tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;
        
        // Mock validation
        if token.starts_with("valid_") {
            Ok("user_123".to_string())
        } else {
            Err("Invalid token".to_string())
        }
    }
}

#[derive(Debug, Clone)]
pub struct AsyncAuthInterceptor {
    pub database: Arc<UserDatabase>,
}

impl AsyncAuthInterceptor {
    pub fn new(database: Arc<UserDatabase>) -> Self {
        AsyncAuthInterceptor { database }
    }
    
    async fn authenticate(
        &self,
        mut request: Request<()>,
    ) -> Result<Request<()>, Status> {
        // Extract authorization header
        let auth_header = request
            .metadata()
            .get("authorization")
            .ok_or_else(|| Status::unauthenticated("Missing authorization header"))?
            .to_str()
            .map_err(|_| Status::unauthenticated("Invalid authorization header"))?;

        // Remove "Bearer " prefix
        let token = auth_header
            .strip_prefix("Bearer ")
            .ok_or_else(|| Status::unauthenticated("Invalid token format"))?;

        // Async database validation
        let user_id = self
            .database
            .verify_token(token)
            .await
            .map_err(|e| Status::unauthenticated(e))?;

        // Add user_id to request extensions
        request.extensions_mut().insert(user_id);

        Ok(request)
    }
}

impl AsyncInterceptor for AsyncAuthInterceptor {
    type Future = Pin<Box<dyn Future<Output = Result<Request<()>, Status>> + Send + 'static>>;

    fn call(&mut self, request: Request<()>) -> Self::Future {
        let fut = self.authenticate(request);
        Box::pin(fut)
    }
}

// Server setup with async interceptor
use tonic_async_interceptor::async_interceptor;

pub async fn run_server_with_async_auth() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    
    let database = Arc::new(UserDatabase {});
    let auth_interceptor = AsyncAuthInterceptor::new(database);
    
    let greeting_service = MyGreetingService::default();

    Server::builder()
        .layer(async_interceptor(auth_interceptor))
        .add_service(GreetingServiceServer::new(greeting_service))
        .serve(addr)
        .await?;

    Ok(())
}
```

### Client-Side Authentication

#### 1. Simple Client with Token

```rust
// client.rs
use tonic::transport::Channel;
use tonic::Request;
use greeting::greeting_service_client::GreetingServiceClient;
use greeting::HelloRequest;

pub mod greeting {
    tonic::include_proto!("greeting");
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let channel = Channel::from_static("http://[::1]:50051")
        .connect()
        .await?;

    let token = "your-jwt-token-here";
    let mut client = GreetingServiceClient::new(channel);

    // Create request
    let mut request = Request::new(HelloRequest {
        name: "Tonic User".to_string(),
    });

    // Add authorization metadata
    request.metadata_mut().insert(
        "authorization",
        format!("Bearer {}", token).parse().unwrap(),
    );

    let response = client.say_hello(request).await?;
    println!("RESPONSE={:?}", response.into_inner());

    Ok(())
}
```

#### 2. Client with Interceptor

```rust
// client_with_interceptor.rs
use tonic::transport::Channel;
use tonic::{Request, Status};
use greeting::greeting_service_client::GreetingServiceClient;
use greeting::HelloRequest;

pub mod greeting {
    tonic::include_proto!("greeting");
}

// Client interceptor to add token to every request
fn add_token(mut req: Request<()>) -> Result<Request<()>, Status> {
    let token = "your-jwt-token-here";
    req.metadata_mut().insert(
        "authorization",
        format!("Bearer {}", token).parse().unwrap(),
    );
    Ok(req)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let channel = Channel::from_static("http://[::1]:50051")
        .connect()
        .await?;

    let mut client = GreetingServiceClient::with_interceptor(channel, add_token);

    // Token is automatically added by interceptor
    let request = Request::new(HelloRequest {
        name: "Tonic User".to_string(),
    });

    let response = client.say_hello(request).await?;
    println!("RESPONSE={:?}", response.into_inner());

    Ok(())
}
```

#### 3. Advanced: Token Refresh Client

```rust
// token_refresh_client.rs
use std::sync::{Arc, RwLock};
use tonic::transport::Channel;
use tonic::{Request, Status};
use chrono::{DateTime, Utc};

pub struct TokenManager {
    access_token: RwLock<String>,
    refresh_token: RwLock<String>,
    expires_at: RwLock<DateTime<Utc>>,
}

impl TokenManager {
    pub fn new(access_token: String, refresh_token: String, expires_in: i64) -> Self {
        let expires_at = Utc::now() + chrono::Duration::seconds(expires_in);
        
        TokenManager {
            access_token: RwLock::new(access_token),
            refresh_token: RwLock::new(refresh_token),
            expires_at: RwLock::new(expires_at),
        }
    }

    pub fn get_token(&self) -> String {
        self.access_token.read().unwrap().clone()
    }

    pub fn is_expired(&self) -> bool {
        Utc::now() >= *self.expires_at.read().unwrap()
    }

    pub async fn refresh_if_needed(&self) -> Result<(), Box<dyn std::error::Error>> {
        if self.is_expired() {
            // Call auth service to refresh token
            let new_token = self.refresh_token_from_service().await?;
            *self.access_token.write().unwrap() = new_token.access_token;
            *self.expires_at.write().unwrap() = 
                Utc::now() + chrono::Duration::seconds(new_token.expires_in);
        }
        Ok(())
    }

    async fn refresh_token_from_service(&self) 
        -> Result<TokenResponse, Box<dyn std::error::Error>> {
        // Implementation to call auth service
        // This is simplified
        Ok(TokenResponse {
            access_token: "new_token".to_string(),
            expires_in: 3600,
        })
    }
}

pub struct TokenResponse {
    pub access_token: String,
    pub expires_in: i64,
}

// Interceptor using TokenManager
pub struct TokenInterceptor {
    token_manager: Arc<TokenManager>,
}

impl TokenInterceptor {
    pub fn new(token_manager: Arc<TokenManager>) -> Self {
        TokenInterceptor { token_manager }
    }

    pub async fn intercept(&self, mut req: Request<()>) -> Result<Request<()>, Status> {
        // Refresh token if needed
        self.token_manager
            .refresh_if_needed()
            .await
            .map_err(|e| Status::internal(format!("Token refresh failed: {}", e)))?;

        let token = self.token_manager.get_token();
        req.metadata_mut().insert(
            "authorization",
            format!("Bearer {}", token).parse().unwrap(),
        );

        Ok(req)
    }
}
```

## Best Practices

### 1. **Use HTTPS/TLS in Production**
Always use SSL/TLS encryption in production to protect credentials in transit.

```cpp
// C++ - SSL credentials
grpc::SslServerCredentialsOptions ssl_opts;
ssl_opts.pem_root_certs = root_cert;
ssl_opts.pem_key_cert_pairs.push_back({server_key, server_cert});
auto creds = grpc::SslServerCredentials(ssl_opts);
```

```rust
// Rust - TLS configuration
use tonic::transport::{Server, ServerTlsConfig, Identity};

let cert = tokio::fs::read("server.crt").await?;
let key = tokio::fs::read("server.key").await?;
let identity = Identity::from_pem(cert, key);

let tls_config = ServerTlsConfig::new().identity(identity);

Server::builder()
    .tls_config(tls_config)?
    .add_service(service)
    .serve(addr)
    .await?;
```

### 2. **Implement Token Expiration**
JWT tokens should have short expiration times with refresh token mechanism:

```rust
// Set appropriate expiration
let exp = Utc::now() + Duration::hours(1);  // 1 hour expiry
```

### 3. **Validate All Required Claims**
Always validate issuer, audience, and custom claims:

```rust
let mut validation = Validation::default();
validation.set_issuer(&["your-auth-service"]);
validation.set_audience(&["your-api"]);
validation.validate_exp = true;
```

### 4. **Use Secure Secret Storage**
Never hardcode secrets. Use environment variables or secret management systems:

```rust
use std::env;

let secret = env::var("JWT_SECRET")
    .expect("JWT_SECRET must be set");
```

### 5. **Implement Rate Limiting**
Protect against brute force attacks on authentication endpoints.

### 6. **Log Authentication Events**
Log successful and failed authentication attempts for security monitoring:

```rust
if let Err(e) = verify_result {
    eprintln!("Authentication failed: {} from {:?}", e, remote_addr);
}
```

### 7. **Handle Multiple Authentication Methods**
Support different authentication schemes for different use cases:

```rust
fn authenticate(req: &Request<()>) -> Result<Claims, Status> {
    // Try JWT first
    if let Some(jwt_claims) = try_jwt_auth(req) {
        return Ok(jwt_claims);
    }
    
    // Fall back to API key
    if let Some(api_key_claims) = try_api_key_auth(req) {
        return Ok(api_key_claims);
    }
    
    Err(Status::unauthenticated("No valid authentication found"))
}
```

### 8. **Implement Proper Error Handling**
Don't leak sensitive information in error messages:

```rust
// Bad - leaks information
Err(Status::unauthenticated("User 'admin' not found"))

// Good - generic message
Err(Status::unauthenticated("Invalid credentials"))
```

## Common Patterns

### Pattern 1: Per-Service Authentication
Apply different authentication to different services:

```rust
Server::builder()
    .add_service(
        PublicServiceServer::new(public_service)
    )
    .add_service(
        ProtectedServiceServer::with_interceptor(
            protected_service,
            check_auth
        )
    )
    .serve(addr)
    .await?;
```

### Pattern 2: Method-Level Authorization
Check permissions for specific methods:

```rust
fn check_permission(req: &Request<()>, method: &str) -> Result<(), Status> {
    let claims = req.extensions().get::<Claims>().unwrap();
    
    match method {
        "/greeting.GreetingService/SayHello" => {
            if claims.role == "admin" || claims.role == "user" {
                Ok(())
            } else {
                Err(Status::permission_denied("Insufficient permissions"))
            }
        }
        _ => Ok(())
    }
}
```

### Pattern 3: Context Propagation
Propagate authentication across service boundaries:

```rust
async fn call_downstream_service(
    auth_token: &str,
) -> Result<Response<DownstreamResponse>, Status> {
    let channel = Channel::from_static("http://downstream:50052")
        .connect()
        .await?;
    
    let mut client = DownstreamServiceClient::new(channel);
    let mut request = Request::new(DownstreamRequest {});
    
    // Propagate auth token
    request.metadata_mut().insert(
        "authorization",
        auth_token.parse().unwrap(),
    );
    
    client.call_method(request).await
}
```

## Summary

Authentication in Protobuf/gRPC services provides robust security through:

1. **Metadata-based Credential Transmission**: Using HTTP/2 headers to pass authentication tokens securely alongside requests

2. **Interceptor Architecture**: Middleware components that intercept calls to validate credentials before reaching service handlers

3. **Multiple Authentication Schemes**: Support for JWT, OAuth2, API keys, mutual TLS, and custom authentication mechanisms

4. **Language-Agnostic Implementation**: Consistent authentication patterns across C++, Rust, and other gRPC-supported languages

5. **Production-Ready Security**: Built-in support for TLS encryption, token validation, claim verification, and secure credential management

Both C++ and Rust implementations follow similar patterns:
- Extract credentials from metadata
- Validate tokens/credentials
- Add user context to request
- Enforce authorization rules
- Handle errors gracefully

The combination of Protocol Buffers' efficient serialization with gRPC's authentication mechanisms creates a powerful foundation for building secure, high-performance microservices.