# JSON to Protobuf Migration

## Overview

Migrating from JSON to Protocol Buffers (Protobuf) in REST APIs is a common evolution path for systems seeking better performance, type safety, and bandwidth efficiency. This migration requires careful planning to maintain backward compatibility while introducing protobuf's benefits gradually.

## Why Migrate?

**Benefits of Protobuf over JSON:**
- **Performance**: 3-10x faster serialization/deserialization
- **Size**: 20-50% smaller payload sizes
- **Type Safety**: Strong typing prevents runtime errors
- **Schema Evolution**: Built-in versioning with backward/forward compatibility
- **Code Generation**: Automatic client/server code generation
- **Validation**: Schema-based validation at compile time

**When to Migrate:**
- High-traffic APIs where bandwidth matters
- Mobile applications with limited connectivity
- Microservices requiring strict contracts
- Systems with complex nested data structures

## Migration Strategies

### 1. Dual-Format Support (Recommended)

Support both JSON and Protobuf simultaneously based on `Content-Type` headers.

**Proto Definition:**
```protobuf
syntax = "proto3";

package api.v1;

message User {
  int64 id = 1;
  string username = 2;
  string email = 3;
  int64 created_at = 4;
  repeated string roles = 5;
  UserProfile profile = 6;
}

message UserProfile {
  string first_name = 1;
  string last_name = 2;
  string bio = 3;
  map<string, string> metadata = 4;
}

message CreateUserRequest {
  string username = 1;
  string email = 2;
  string password = 3;
}

message CreateUserResponse {
  User user = 1;
  string message = 2;
}
```

### 2. C++ Implementation

**Server-Side Handler with Dual Format:**

```cpp
#include <iostream>
#include <string>
#include <memory>
#include "user.pb.h"
#include <google/protobuf/util/json_util.h>

class UserHandler {
public:
    // Dual-format endpoint
    std::pair<std::string, std::string> CreateUser(
        const std::string& request_body,
        const std::string& content_type) {
        
        api::v1::CreateUserRequest req;
        
        // Parse based on content type
        if (content_type == "application/x-protobuf" || 
            content_type == "application/protobuf") {
            // Parse protobuf
            if (!req.ParseFromString(request_body)) {
                return {"", "application/json"};  // Error handling
            }
        } else {
            // Parse JSON (default for compatibility)
            google::protobuf::util::JsonParseOptions options;
            options.ignore_unknown_fields = true;
            
            auto status = google::protobuf::util::JsonStringToMessage(
                request_body, &req, options);
            
            if (!status.ok()) {
                return {"", "application/json"};
            }
        }
        
        // Business logic
        api::v1::User user;
        user.set_id(GenerateUserId());
        user.set_username(req.username());
        user.set_email(req.email());
        user.set_created_at(GetCurrentTimestamp());
        
        api::v1::CreateUserResponse response;
        response.mutable_user()->CopyFrom(user);
        response.set_message("User created successfully");
        
        // Serialize based on Accept header
        std::string response_body;
        std::string response_type;
        
        if (content_type == "application/x-protobuf" || 
            content_type == "application/protobuf") {
            response.SerializeToString(&response_body);
            response_type = "application/x-protobuf";
        } else {
            google::protobuf::util::JsonPrintOptions print_options;
            print_options.add_whitespace = true;
            print_options.always_print_primitive_fields = true;
            
            google::protobuf::util::MessageToJsonString(
                response, &response_body, print_options);
            response_type = "application/json";
        }
        
        return {response_body, response_type};
    }
    
private:
    int64_t GenerateUserId() { return rand() % 100000; }
    int64_t GetCurrentTimestamp() { return time(nullptr); }
};

// Migration compatibility layer
class CompatibilityLayer {
public:
    // Convert legacy JSON field names to protobuf fields
    static std::string TransformLegacyJson(const std::string& legacy_json) {
        // Example: "user_name" -> "username"
        std::string transformed = legacy_json;
        
        // Field name mappings
        ReplaceAll(transformed, "\"user_name\"", "\"username\"");
        ReplaceAll(transformed, "\"createdAt\"", "\"created_at\"");
        
        return transformed;
    }
    
    // Ensure backward compatible JSON output
    static std::string AddLegacyFields(const std::string& protobuf_json) {
        // Could add deprecated fields for old clients
        return protobuf_json;
    }
    
private:
    static void ReplaceAll(std::string& str, 
                          const std::string& from, 
                          const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
};

// Example usage
int main() {
    UserHandler handler;
    
    // JSON request (legacy client)
    std::string json_request = R"({
        "username": "john_doe",
        "email": "john@example.com",
        "password": "secure123"
    })";
    
    auto [json_response, json_type] = handler.CreateUser(
        json_request, "application/json");
    
    std::cout << "JSON Response (" << json_type << "):\n" 
              << json_response << "\n\n";
    
    // Protobuf request (new client)
    api::v1::CreateUserRequest pb_request;
    pb_request.set_username("jane_doe");
    pb_request.set_email("jane@example.com");
    pb_request.set_password("secure456");
    
    std::string pb_serialized;
    pb_request.SerializeToString(&pb_serialized);
    
    auto [pb_response, pb_type] = handler.CreateUser(
        pb_serialized, "application/x-protobuf");
    
    std::cout << "Protobuf Response (" << pb_type << "): " 
              << pb_response.size() << " bytes\n";
    
    return 0;
}
```

**Client Migration Helper:**

```cpp
#include "user.pb.h"
#include <curl/curl.h>
#include <google/protobuf/util/json_util.h>

class ApiClient {
public:
    enum Format { JSON, PROTOBUF };
    
    ApiClient(const std::string& base_url, Format format = JSON) 
        : base_url_(base_url), format_(format) {}
    
    // Gradually migrate clients by changing format
    api::v1::CreateUserResponse CreateUser(
        const api::v1::CreateUserRequest& request) {
        
        CURL* curl = curl_easy_init();
        std::string response_data;
        
        if (curl) {
            std::string url = base_url_ + "/users";
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            
            std::string request_body;
            struct curl_slist* headers = nullptr;
            
            if (format_ == PROTOBUF) {
                request.SerializeToString(&request_body);
                headers = curl_slist_append(headers, 
                    "Content-Type: application/x-protobuf");
                headers = curl_slist_append(headers, 
                    "Accept: application/x-protobuf");
            } else {
                google::protobuf::util::MessageToJsonString(
                    request, &request_body);
                headers = curl_slist_append(headers, 
                    "Content-Type: application/json");
                headers = curl_slist_append(headers, 
                    "Accept: application/json");
            }
            
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, 
                           request_body.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
            
            curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
        api::v1::CreateUserResponse response;
        
        if (format_ == PROTOBUF) {
            response.ParseFromString(response_data);
        } else {
            google::protobuf::util::JsonStringToMessage(
                response_data, &response);
        }
        
        return response;
    }
    
private:
    std::string base_url_;
    Format format_;
    
    static size_t WriteCallback(void* contents, size_t size, 
                               size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};
```

### 3. Rust Implementation

**Server-Side with Actix-Web:**

```rust
use actix_web::{web, App, HttpRequest, HttpResponse, HttpServer};
use prost::Message;
use serde::{Deserialize, Serialize};
use serde_json;

// Generated from .proto file using prost
pub mod api {
    pub mod v1 {
        include!(concat!(env!("OUT_DIR"), "/api.v1.rs"));
    }
}

use api::v1::{User, UserProfile, CreateUserRequest, CreateUserResponse};

// JSON compatibility structs (mirrors protobuf)
#[derive(Serialize, Deserialize)]
struct UserJson {
    id: i64,
    username: String,
    email: String,
    created_at: i64,
    roles: Vec<String>,
    profile: Option<UserProfileJson>,
}

#[derive(Serialize, Deserialize)]
struct UserProfileJson {
    first_name: String,
    last_name: String,
    bio: String,
    metadata: std::collections::HashMap<String, String>,
}

#[derive(Serialize, Deserialize)]
struct CreateUserRequestJson {
    username: String,
    email: String,
    password: String,
}

#[derive(Serialize, Deserialize)]
struct CreateUserResponseJson {
    user: UserJson,
    message: String,
}

// Dual-format handler
async fn create_user(
    req: HttpRequest,
    body: web::Bytes,
) -> actix_web::Result<HttpResponse> {
    let content_type = req
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("application/json");
    
    let accept = req
        .headers()
        .get("accept")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("application/json");
    
    // Parse request
    let request: CreateUserRequest = if content_type.contains("protobuf") {
        CreateUserRequest::decode(&body[..])
            .map_err(|e| actix_web::error::ErrorBadRequest(e))?
    } else {
        // Parse JSON and convert to protobuf
        let json_req: CreateUserRequestJson = serde_json::from_slice(&body)
            .map_err(|e| actix_web::error::ErrorBadRequest(e))?;
        
        CreateUserRequest {
            username: json_req.username,
            email: json_req.email,
            password: json_req.password,
        }
    };
    
    // Business logic
    let user = User {
        id: generate_user_id(),
        username: request.username.clone(),
        email: request.email.clone(),
        created_at: chrono::Utc::now().timestamp(),
        roles: vec!["user".to_string()],
        profile: None,
    };
    
    let response = CreateUserResponse {
        user: Some(user),
        message: "User created successfully".to_string(),
    };
    
    // Serialize response
    if accept.contains("protobuf") {
        let mut buf = Vec::new();
        response.encode(&mut buf)
            .map_err(|e| actix_web::error::ErrorInternalServerError(e))?;
        
        Ok(HttpResponse::Ok()
            .content_type("application/x-protobuf")
            .body(buf))
    } else {
        // Convert to JSON
        let user = response.user.unwrap();
        let json_response = CreateUserResponseJson {
            user: UserJson {
                id: user.id,
                username: user.username,
                email: user.email,
                created_at: user.created_at,
                roles: user.roles,
                profile: user.profile.map(|p| UserProfileJson {
                    first_name: p.first_name,
                    last_name: p.last_name,
                    bio: p.bio,
                    metadata: p.metadata,
                }),
            },
            message: response.message,
        };
        
        Ok(HttpResponse::Ok()
            .content_type("application/json")
            .json(json_response))
    }
}

fn generate_user_id() -> i64 {
    use rand::Rng;
    rand::thread_rng().gen_range(1..100000)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    HttpServer::new(|| {
        App::new()
            .route("/users", web::post().to(create_user))
    })
    .bind("127.0.0.1:8080")?
    .run()
    .await
}
```

**Client with Automatic Format Detection:**

```rust
use prost::Message;
use reqwest;

pub struct ApiClient {
    base_url: String,
    client: reqwest::Client,
    use_protobuf: bool,
}

impl ApiClient {
    pub fn new(base_url: String, use_protobuf: bool) -> Self {
        Self {
            base_url,
            client: reqwest::Client::new(),
            use_protobuf,
        }
    }
    
    pub async fn create_user(
        &self,
        request: CreateUserRequest,
    ) -> Result<CreateUserResponse, Box<dyn std::error::Error>> {
        let url = format!("{}/users", self.base_url);
        
        let response = if self.use_protobuf {
            // Send as protobuf
            let mut buf = Vec::new();
            request.encode(&mut buf)?;
            
            self.client
                .post(&url)
                .header("Content-Type", "application/x-protobuf")
                .header("Accept", "application/x-protobuf")
                .body(buf)
                .send()
                .await?
        } else {
            // Send as JSON (for backward compatibility)
            self.client
                .post(&url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .json(&request)
                .send()
                .await?
        };
        
        let body = response.bytes().await?;
        
        let result = if self.use_protobuf {
            CreateUserResponse::decode(&body[..])?
        } else {
            // Parse JSON response
            serde_json::from_slice(&body)?
        };
        
        Ok(result)
    }
}

// Migration strategy: Feature flag based rollout
pub struct MigrationClient {
    json_client: ApiClient,
    protobuf_client: ApiClient,
    rollout_percentage: u8, // 0-100
}

impl MigrationClient {
    pub fn new(base_url: String, rollout_percentage: u8) -> Self {
        Self {
            json_client: ApiClient::new(base_url.clone(), false),
            protobuf_client: ApiClient::new(base_url, true),
            rollout_percentage: rollout_percentage.min(100),
        }
    }
    
    pub async fn create_user(
        &self,
        request: CreateUserRequest,
    ) -> Result<CreateUserResponse, Box<dyn std::error::Error>> {
        // Randomly decide which format to use based on rollout percentage
        let use_protobuf = rand::random::<u8>() % 100 < self.rollout_percentage;
        
        if use_protobuf {
            println!("Using protobuf format");
            self.protobuf_client.create_user(request).await
        } else {
            println!("Using JSON format");
            self.json_client.create_user(request).await
        }
    }
}
```

**Backward Compatibility Layer:**

```rust
use std::collections::HashMap;

// Handle field name transformations for legacy clients
pub struct CompatibilityTransform;

impl CompatibilityTransform {
    // Transform legacy JSON field names
    pub fn transform_legacy_json(json: &str) -> String {
        let mut result = json.to_string();
        
        // Field mappings (camelCase -> snake_case)
        let mappings = HashMap::from([
            ("userName", "username"),
            ("firstName", "first_name"),
            ("lastName", "last_name"),
            ("createdAt", "created_at"),
        ]);
        
        for (old, new) in mappings {
            result = result.replace(
                &format!("\"{}\"", old),
                &format!("\"{}\"", new),
            );
        }
        
        result
    }
    
    // Add deprecated fields to response for old clients
    pub fn add_legacy_fields(user: &mut User) {
        // Could add computed fields or aliases here
        // e.g., user.full_name = format!("{} {}", first, last);
    }
}

// Versioned API support
pub mod versioning {
    use super::*;
    
    pub enum ApiVersion {
        V1,
        V2,
    }
    
    pub fn handle_versioned_request(
        version: ApiVersion,
        request: CreateUserRequest,
    ) -> CreateUserResponse {
        match version {
            ApiVersion::V1 => {
                // V1: Always use JSON for compatibility
                handle_v1_request(request)
            }
            ApiVersion::V2 => {
                // V2: Prefer protobuf, support both
                handle_v2_request(request)
            }
        }
    }
    
    fn handle_v1_request(request: CreateUserRequest) -> CreateUserResponse {
        // Legacy behavior
        CreateUserResponse {
            user: Some(User {
                id: 1,
                username: request.username,
                email: request.email,
                created_at: 0,
                roles: vec![],
                profile: None,
            }),
            message: "User created (v1)".to_string(),
        }
    }
    
    fn handle_v2_request(request: CreateUserRequest) -> CreateUserResponse {
        // Modern behavior with enhanced features
        CreateUserResponse {
            user: Some(User {
                id: 1,
                username: request.username,
                email: request.email,
                created_at: chrono::Utc::now().timestamp(),
                roles: vec!["user".to_string()],
                profile: Some(UserProfile::default()),
            }),
            message: "User created successfully (v2)".to_string(),
        }
    }
}
```

## Migration Checklist

**Phase 1: Preparation**
- ✓ Define .proto schemas matching existing JSON structure
- ✓ Set up protobuf compilation in build system
- ✓ Create conversion utilities between JSON and protobuf
- ✓ Implement dual-format support in servers

**Phase 2: Gradual Rollout**
- ✓ Deploy servers with dual-format support
- ✓ Monitor JSON and protobuf traffic separately
- ✓ Start with internal/test clients using protobuf
- ✓ Gradually increase protobuf rollout percentage

**Phase 3: Full Migration**
- ✓ Update all clients to use protobuf
- ✓ Maintain JSON support for external/legacy clients
- ✓ Document migration for API consumers
- ✓ Eventually deprecate JSON (with long notice period)

## Summary

JSON to Protobuf migration is a strategic evolution requiring a phased approach. The key is **dual-format support** allowing both formats to coexist during transition. C++ provides robust protobuf support through the official library with built-in JSON conversion, while Rust leverages prost for code generation with seamless integration into web frameworks like Actix.

Critical success factors include maintaining strict backward compatibility, implementing gradual rollout mechanisms (feature flags, percentage-based deployment), comprehensive monitoring of both formats, and clear documentation for API consumers. The migration delivers significant performance gains—smaller payloads, faster processing, and stronger type safety—making it worthwhile for high-traffic production systems.