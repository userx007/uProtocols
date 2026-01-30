# Bazel Integration for Protobuf

## Overview

Bazel is Google's open-source build system designed for large-scale, multi-language projects. It provides excellent support for Protocol Buffers with built-in rules for code generation, dependency management, and cross-language builds. Bazel's hermetic and reproducible builds make it ideal for projects using protobufs across multiple services and languages.

## Core Concepts

**BUILD Files**: Define build targets, dependencies, and rules for each directory in your project.

**Workspaces**: The root directory containing a WORKSPACE file that declares external dependencies.

**Rules**: Bazel provides `proto_library`, `cc_proto_library`, and language-specific rules for generating and compiling protobuf code.

**Hermetic Builds**: Bazel ensures consistent builds by isolating dependencies and caching intermediate artifacts.

## Setting Up Bazel with Protobuf

### WORKSPACE Configuration

```python
# WORKSPACE file
workspace(name = "protobuf_example")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Protobuf rules
http_archive(
    name = "com_google_protobuf",
    sha256 = "bc3dbf1f09dba1b2eb3f2f70352ee97b9049066c9040ce0c9b67fb3294e91e4b",
    strip_prefix = "protobuf-3.21.12",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.21.12.tar.gz"],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()

# For Rust support
http_archive(
    name = "rules_rust",
    sha256 = "4a9cb4fda6ccd5b5ec393b2e944822a62e050c7c06f1ea41607f14c4fdec57a2",
    urls = ["https://github.com/bazelbuild/rules_rust/releases/download/0.25.1/rules_rust-v0.25.1.tar.gz"],
)

load("@rules_rust//rust:repositories.bzl", "rules_rust_dependencies", "rust_register_toolchains")
rules_rust_dependencies()
rust_register_toolchains()

load("@rules_rust//proto/prost:repositories.bzl", "rust_prost_dependencies")
rust_prost_dependencies()

load("@rules_rust//proto/prost:transitive_repositories.bzl", "rust_prost_transitive_repositories")
rust_prost_transitive_repositories()
```

## C/C++ Integration

### Proto Definition

```protobuf
// user.proto
syntax = "proto3";

package example;

message User {
  int32 id = 1;
  string name = 2;
  string email = 3;
  repeated string roles = 4;
}

message UserRequest {
  int32 user_id = 1;
}

message UserResponse {
  User user = 1;
  bool success = 2;
  string error_message = 3;
}
```

### BUILD File for C++

```python
# BUILD file
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library", "cc_binary")

# Define the proto library
proto_library(
    name = "user_proto",
    srcs = ["user.proto"],
    visibility = ["//visibility:public"],
)

# Generate C++ code from proto
cc_proto_library(
    name = "user_cc_proto",
    deps = [":user_proto"],
)

# C++ binary using the generated code
cc_binary(
    name = "user_service",
    srcs = ["user_service.cc"],
    deps = [
        ":user_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)
```

### C++ Implementation

```cpp
// user_service.cc
#include <iostream>
#include <fstream>
#include "user.pb.h"

void createUser() {
    example::User user;
    user.set_id(101);
    user.set_name("Alice Johnson");
    user.set_email("alice@example.com");
    user.add_roles("developer");
    user.add_roles("admin");

    // Serialize to file
    std::fstream output("user.bin", 
                       std::ios::out | std::ios::binary);
    if (!user.SerializeToOstream(&output)) {
        std::cerr << "Failed to write user." << std::endl;
        return;
    }
    output.close();
    
    std::cout << "User created: " << user.name() << std::endl;
}

void readUser() {
    example::User user;
    
    std::fstream input("user.bin", 
                      std::ios::in | std::ios::binary);
    if (!user.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse user." << std::endl;
        return;
    }
    
    std::cout << "User ID: " << user.id() << std::endl;
    std::cout << "Name: " << user.name() << std::endl;
    std::cout << "Email: " << user.email() << std::endl;
    std::cout << "Roles:" << std::endl;
    for (const auto& role : user.roles()) {
        std::cout << "  - " << role << std::endl;
    }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    createUser();
    readUser();
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

### Advanced C++ BUILD with Multiple Protos

```python
# BUILD file for complex project
proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
)

proto_library(
    name = "user_proto",
    srcs = ["user.proto"],
    deps = [":common_proto"],
)

cc_proto_library(
    name = "common_cc_proto",
    deps = [":common_proto"],
)

cc_proto_library(
    name = "user_cc_proto",
    deps = [":user_proto"],
)

cc_library(
    name = "user_service_lib",
    srcs = ["user_service.cc"],
    hdrs = ["user_service.h"],
    deps = [
        ":user_cc_proto",
        ":common_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "user_service_test",
    srcs = ["user_service_test.cc"],
    deps = [
        ":user_service_lib",
        "@com_google_googletest//:gtest_main",
    ],
)
```

## Rust Integration

### BUILD File for Rust

```python
# BUILD file
load("@rules_rust//rust:defs.bzl", "rust_binary", "rust_library")
load("@rules_rust//proto/prost:defs.bzl", "rust_prost_library")

proto_library(
    name = "user_proto",
    srcs = ["user.proto"],
)

# Generate Rust code using prost
rust_prost_library(
    name = "user_rust_proto",
    proto = ":user_proto",
)

rust_binary(
    name = "user_service_rust",
    srcs = ["user_service.rs"],
    deps = [
        ":user_rust_proto",
        "@crates//:prost",
        "@crates//:bytes",
    ],
)
```

### Cargo.toml for Dependencies

```toml
# .bazelrc should include: build --@rules_rust//rust/toolchain/channel=stable

# Cargo.toml (for external crates)
[package]
name = "user_service"
version = "0.1.0"
edition = "2021"

[dependencies]
prost = "0.12"
bytes = "1.4"
```

### Rust Implementation

```rust
// user_service.rs
use std::fs::File;
use std::io::{Read, Write};
use prost::Message;

// Import generated protobuf code
mod proto {
    include!(concat!(env!("OUT_DIR"), "/example.rs"));
}

use proto::{User, UserRequest, UserResponse};

fn create_user() -> Result<(), Box<dyn std::error::Error>> {
    let user = User {
        id: 101,
        name: "Alice Johnson".to_string(),
        email: "alice@example.com".to_string(),
        roles: vec!["developer".to_string(), "admin".to_string()],
    };

    // Serialize to file
    let mut file = File::create("user_rust.bin")?;
    let mut buf = Vec::new();
    user.encode(&mut buf)?;
    file.write_all(&buf)?;
    
    println!("User created: {}", user.name);
    Ok(())
}

fn read_user() -> Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open("user_rust.bin")?;
    let mut buf = Vec::new();
    file.read_to_end(&mut buf)?;
    
    let user = User::decode(&buf[..])?;
    
    println!("User ID: {}", user.id);
    println!("Name: {}", user.name);
    println!("Email: {}", user.email);
    println!("Roles:");
    for role in &user.roles {
        println!("  - {}", role);
    }
    
    Ok(())
}

fn create_response() -> UserResponse {
    let user = User {
        id: 202,
        name: "Bob Smith".to_string(),
        email: "bob@example.com".to_string(),
        roles: vec!["viewer".to_string()],
    };
    
    UserResponse {
        user: Some(user),
        success: true,
        error_message: String::new(),
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    create_user()?;
    read_user()?;
    
    let response = create_response();
    if let Some(user) = response.user {
        println!("\nResponse user: {}", user.name);
    }
    
    Ok(())
}
```

### Advanced Rust BUILD with Custom Configuration

```python
# BUILD file with multiple targets
load("@rules_rust//proto/prost:defs.bzl", "rust_prost_library")
load("@rules_rust//rust:defs.bzl", "rust_library", "rust_test")

proto_library(
    name = "models_proto",
    srcs = glob(["*.proto"]),
    deps = ["//common:timestamp_proto"],
)

rust_prost_library(
    name = "models_rust_proto",
    proto = ":models_proto",
    deps = ["//common:timestamp_rust_proto"],
)

rust_library(
    name = "user_lib",
    srcs = ["lib.rs"],
    deps = [
        ":models_rust_proto",
        "@crates//:prost",
        "@crates//:serde",
        "@crates//:serde_json",
    ],
    visibility = ["//visibility:public"],
)

rust_test(
    name = "user_test",
    crate = ":user_lib",
)
```

## Multi-Language Project Structure

```
project_root/
├── WORKSPACE
├── .bazelrc
├── proto/
│   ├── BUILD
│   ├── user.proto
│   └── common.proto
├── cpp/
│   ├── BUILD
│   ├── user_service.cc
│   └── user_service.h
├── rust/
│   ├── BUILD
│   ├── Cargo.toml
│   └── user_service.rs
└── go/
    ├── BUILD
    └── user_service.go
```

### Root .bazelrc

```bash
# .bazelrc
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17

# Rust configuration
build --@rules_rust//rust/toolchain/channel=stable

# Performance optimizations
build --jobs=auto
build --local_cpu_resources=HOST_CPUS*.75

# Output settings
build --show_timestamps
build --color=yes
```

## Building and Running

```bash
# Build all targets
bazel build //...

# Build specific C++ target
bazel build //cpp:user_service

# Run C++ binary
bazel run //cpp:user_service

# Build Rust target
bazel build //rust:user_service_rust

# Run Rust binary
bazel run //rust:user_service_rust

# Run tests
bazel test //...

# Clean build artifacts
bazel clean

# Query dependencies
bazel query 'deps(//cpp:user_service)'

# Build with specific configuration
bazel build --config=release //cpp:user_service
```

## Summary

Bazel provides robust integration for Protocol Buffers across multiple programming languages with several key advantages: hermetic and reproducible builds ensure consistency across different environments, efficient caching and incremental builds speed up development cycles, and native support for multi-language projects simplifies polyglot architectures. The declarative BUILD files make dependencies explicit and manageable, while Bazel's scalability handles large codebases with thousands of proto files efficiently.

For C++ projects, Bazel offers seamless integration with the protobuf compiler through `cc_proto_library`, automatic dependency management, and strong type safety. Rust integration via `rust_prost_library` provides modern, idiomatic Rust code generation with excellent performance characteristics. Both languages benefit from Bazel's parallel build execution and cross-platform support, making it an excellent choice for teams building distributed systems with Protocol Buffers at scale.