# CMake Protobuf Configuration

## Overview

Protocol Buffers (protobuf) is Google's language-neutral, platform-neutral extensible mechanism for serializing structured data. Integrating protobuf into CMake-based projects requires proper configuration to automatically generate C++ code from `.proto` files during the build process.

## Core Concepts

### What CMake Does for Protobuf

1. **Finds the protobuf compiler** (`protoc`)
2. **Locates protobuf libraries** (runtime libraries)
3. **Generates C++ source files** from `.proto` definitions
4. **Links generated code** with your executable/library
5. **Manages dependencies** to trigger regeneration when `.proto` files change

## CMake Configuration

### Basic Setup

```cmake
cmake_minimum_required(VERSION 3.15)
project(ProtobufExample)

# Find Protobuf package
find_package(Protobuf REQUIRED)

# Include protobuf headers
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Define proto files
set(PROTO_FILES
    messages.proto
    service.proto
)

# Generate C++ code from proto files
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Create executable with generated sources
add_executable(my_app
    main.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Link protobuf library
target_link_libraries(my_app ${Protobuf_LIBRARIES})
```

### Modern CMake (3.20+) Approach

```cmake
cmake_minimum_required(VERSION 3.20)
project(ModernProtobuf)

find_package(Protobuf CONFIG REQUIRED)

# Add proto files as a library
add_library(proto_messages)
target_sources(proto_messages PRIVATE
    messages.proto
    service.proto
)

# Use protobuf_generate for modern approach
protobuf_generate(
    TARGET proto_messages
    LANGUAGE cpp
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(proto_messages PUBLIC protobuf::libprotobuf)

# Your application
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE proto_messages)
target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
```

## C++ Code Examples

### Example Proto File (messages.proto)

```protobuf
syntax = "proto3";

package example;

message Person {
    string name = 1;
    int32 id = 2;
    string email = 3;
    
    enum PhoneType {
        MOBILE = 0;
        HOME = 1;
        WORK = 2;
    }
    
    message PhoneNumber {
        string number = 1;
        PhoneType type = 2;
    }
    
    repeated PhoneNumber phones = 4;
}

message AddressBook {
    repeated Person people = 1;
}
```

### C++ Usage

```cpp
#include <iostream>
#include <fstream>
#include <string>
#include "messages.pb.h"

void write_address_book(const std::string& filename) {
    example::AddressBook address_book;
    
    // Add a person
    example::Person* person = address_book.add_people();
    person->set_name("John Doe");
    person->set_id(1234);
    person->set_email("john@example.com");
    
    // Add phone numbers
    example::Person::PhoneNumber* phone = person->add_phones();
    phone->set_number("555-1234");
    phone->set_type(example::Person::MOBILE);
    
    // Write to file
    std::fstream output(filename, std::ios::out | std::ios::binary);
    if (!address_book.SerializeToOstream(&output)) {
        std::cerr << "Failed to write address book." << std::endl;
    }
}

void read_address_book(const std::string& filename) {
    example::AddressBook address_book;
    
    // Read from file
    std::fstream input(filename, std::ios::in | std::ios::binary);
    if (!address_book.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse address book." << std::endl;
        return;
    }
    
    // Print data
    for (int i = 0; i < address_book.people_size(); i++) {
        const example::Person& person = address_book.people(i);
        std::cout << "Person ID: " << person.id() << std::endl;
        std::cout << "  Name: " << person.name() << std::endl;
        std::cout << "  Email: " << person.email() << std::endl;
        
        for (int j = 0; j < person.phones_size(); j++) {
            const example::Person::PhoneNumber& phone = person.phones(j);
            std::cout << "  Phone: " << phone.number() 
                      << " (" << phone.type() << ")" << std::endl;
        }
    }
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    write_address_book("addressbook.pb");
    read_address_book("addressbook.pb");
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

### Advanced C++ - Custom Options

```cpp
#include "messages.pb.h"
#include <google/protobuf/text_format.h>

// Convert to human-readable text format
std::string to_text(const example::Person& person) {
    std::string output;
    google::protobuf::TextFormat::PrintToString(person, &output);
    return output;
}

// Parse from text format
bool from_text(const std::string& text, example::Person& person) {
    return google::protobuf::TextFormat::ParseFromString(text, &person);
}

// JSON serialization (requires protobuf-util)
#include <google/protobuf/util/json_util.h>

std::string to_json(const example::Person& person) {
    std::string json_string;
    google::protobuf::util::MessageToJsonString(person, &json_string);
    return json_string;
}
```

## Rust Integration

### Cargo.toml Setup

```toml
[package]
name = "protobuf_example"
version = "0.1.0"
edition = "2021"

[dependencies]
protobuf = "3.3"

[build-dependencies]
protobuf-codegen = "3.3"
```

### build.rs (Build Script)

```rust
// build.rs
fn main() {
    protobuf_codegen::Codegen::new()
        .pure()
        .includes(&["protos"])
        .input("protos/messages.proto")
        .cargo_out_dir("protos")
        .run_from_script();
}
```

### Alternative with protobuf-build

```toml
[build-dependencies]
protobuf-build = "0.2"
```

```rust
// build.rs
fn main() {
    protobuf_build::Builder::new()
        .search_dir_for_protos(&["protos"])
        .generate()
}
```

### Rust Usage Example

```rust
// src/main.rs
mod messages;

use messages::example::{Person, AddressBook, person::PhoneNumber, person::PhoneType};
use protobuf::Message;
use std::fs::File;
use std::io::{Read, Write};

fn write_address_book(filename: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut address_book = AddressBook::new();
    
    // Create a person
    let mut person = Person::new();
    person.name = "John Doe".to_string();
    person.id = 1234;
    person.email = "john@example.com".to_string();
    
    // Add phone number
    let mut phone = PhoneNumber::new();
    phone.number = "555-1234".to_string();
    phone.type_ = PhoneType::MOBILE.into();
    person.phones.push(phone);
    
    address_book.people.push(person);
    
    // Serialize to file
    let mut file = File::create(filename)?;
    file.write_all(&address_book.write_to_bytes()?)?;
    
    Ok(())
}

fn read_address_book(filename: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open(filename)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;
    
    let address_book = AddressBook::parse_from_bytes(&buffer)?;
    
    for person in &address_book.people {
        println!("Person ID: {}", person.id);
        println!("  Name: {}", person.name);
        println!("  Email: {}", person.email);
        
        for phone in &person.phones {
            println!("  Phone: {} ({:?})", phone.number, phone.type_);
        }
    }
    
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    write_address_book("addressbook.pb")?;
    read_address_book("addressbook.pb")?;
    Ok(())
}
```

### Rust with prost (Alternative Library)

```toml
[dependencies]
prost = "0.12"
bytes = "1.0"

[build-dependencies]
prost-build = "0.12"
```

```rust
// build.rs
fn main() {
    prost_build::compile_protos(&["protos/messages.proto"], &["protos/"])
        .unwrap();
}
```

```rust
// src/main.rs
use prost::Message;

#[derive(Clone, PartialEq, Message)]
pub struct Person {
    #[prost(string, tag = "1")]
    pub name: String,
    #[prost(int32, tag = "2")]
    pub id: i32,
    #[prost(string, tag = "3")]
    pub email: String,
}

fn serialize_example() -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let person = Person {
        name: "Alice".to_string(),
        id: 42,
        email: "alice@example.com".to_string(),
    };
    
    let mut buf = Vec::new();
    person.encode(&mut buf)?;
    Ok(buf)
}

fn deserialize_example(data: &[u8]) -> Result<Person, Box<dyn std::error::Error>> {
    let person = Person::decode(data)?;
    Ok(person)
}
```

## Advanced CMake Patterns

### Multi-Directory Proto Files

```cmake
# Collect all proto files from multiple directories
file(GLOB_RECURSE PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/protos/*.proto"
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Create a library for proto messages
add_library(proto_lib STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(proto_lib PUBLIC protobuf::libprotobuf)
target_include_directories(proto_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
```

### gRPC Integration

```cmake
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTO_FILES service.proto)

add_library(grpc_proto ${PROTO_FILES})

protobuf_generate(TARGET grpc_proto LANGUAGE cpp)
protobuf_generate(
    TARGET grpc_proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
)

target_link_libraries(grpc_proto
    PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
)
```

## Summary

**CMake Protobuf integration** automates the generation of serialization code from `.proto` definitions. Key points:

- **CMake handles code generation** automatically during build, using `find_package(Protobuf)` and `protobuf_generate_cpp()` or the modern `protobuf_generate()` function
- **C++ provides rich APIs** for serialization (binary, text, JSON), deserialization, and reflection with memory-efficient message handling
- **Rust offers two main ecosystems**: `protobuf` (pure Rust) and `prost` (lighter, more idiomatic), both using build scripts for code generation
- **Both languages** support the same wire format, enabling seamless cross-language communication
- **Modern CMake** (3.20+) offers cleaner, target-based approaches with better dependency management

The build-time code generation ensures type safety, reduces boilerplate, and maintains consistency between your data definitions and runtime code across different programming languages.