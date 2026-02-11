# gRPC: A Comprehensive Technical Guide

**Core Topics:**
- Introduction to gRPC and its advantages over REST
- Protocol Buffers syntax and usage
- All four communication patterns (unary, server/client/bidirectional streaming)

**Implementation Examples:**
- **C**: Low-level implementation with grpc_core API
- **C++**: Modern implementation with grpcpp library
- **Rust**: Async implementation using Tonic framework

**Advanced Topics:**
- Authentication (JWT, OAuth2)
- Load balancing
- Error handling patterns
- Interceptors and middleware
- Health checking
- Compression
- TLS/SSL configuration

**Best Practices:**
- Protocol Buffer design guidelines
- Performance optimization
- Security considerations
- Testing strategies
- Monitoring and observability


## Table of Contents
1. [Introduction to gRPC](#introduction-to-grpc)
2. [Core Concepts](#core-concepts)
3. [Protocol Buffers](#protocol-buffers)
4. [Communication Patterns](#communication-patterns)
5. [C Implementation](#c-implementation)
6. [C++ Implementation](#c-implementation-1)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Topics](#advanced-topics)
9. [Best Practices](#best-practices)


## Introduction to gRPC

**gRPC** (gRPC Remote Procedure Call) is a modern, high-performance, open-source RPC (Remote Procedure Call) framework developed by Google. It enables client and server applications to communicate transparently and build connected systems.

### Key Features

- **High Performance**: Uses HTTP/2 for transport, enabling multiplexing, flow control, header compression, and bidirectional streaming
- **Language Agnostic**: Supports multiple programming languages with automatic code generation
- **Protocol Buffers**: Uses Protocol Buffers (protobuf) as the Interface Definition Language (IDL)
- **Streaming Support**: Supports unary, server streaming, client streaming, and bidirectional streaming
- **Pluggable**: Supports pluggable authentication, load balancing, retries, and more
- **Deadline/Timeout**: Built-in timeout and deadline propagation

### Advantages over REST

- **Performance**: Binary serialization (protobuf) is faster and more compact than JSON
- **Strong Typing**: Contract-first API design with strict type checking
- **Streaming**: Native support for streaming requests and responses
- **Code Generation**: Automatic client and server code generation
- **HTTP/2**: Single TCP connection for multiple requests, server push, header compression

---

## Core Concepts

### Architecture

```
┌─────────┐                                ┌─────────┐
│         │    Serialized Protobuf         │         │
│ Client  │◄──────────────────────────────►│ Server  │
│  Stub   │         over HTTP/2            │ Service │
└─────────┘                                └─────────┘
     │                                          │
     │                                          │
     ▼                                          ▼
┌─────────────────┐                    ┌──────────────┐
│ Generated Code  │                    │ Service Impl │
│ from .proto     │                    │ (Your Code)  │
└─────────────────┘                    └──────────────┘
```

### Service Definition

Services are defined using Protocol Buffer `.proto` files, which describe:
- Message structures (request/response types)
- Service interfaces (RPC methods)

### Channels and Stubs

- **Channel**: Represents a connection to a gRPC server on a specific host and port
- **Stub**: Client-side object that implements the same methods as the service

### Metadata

Key-value pairs sent with RPC calls, similar to HTTP headers, used for authentication, tracing, etc.

---

## Protocol Buffers

Protocol Buffers are Google's language-neutral, platform-neutral extensible mechanism for serializing structured data.

### Basic .proto File Example

```protobuf
syntax = "proto3";

package calculator;

// The calculator service definition
service Calculator {
  // Unary RPC: Simple request-response
  rpc Add (AddRequest) returns (AddResponse) {}
  
  // Server streaming: Client sends one request, server sends stream of responses
  rpc Fibonacci (FibRequest) returns (stream FibResponse) {}
  
  // Client streaming: Client sends stream of requests, server sends one response
  rpc Average (stream Number) returns (AverageResponse) {}
  
  // Bidirectional streaming: Both sides send streams of messages
  rpc Chat (stream ChatMessage) returns (stream ChatMessage) {}
}

// Message definitions
message AddRequest {
  int32 a = 1;
  int32 b = 2;
}

message AddResponse {
  int32 result = 1;
}

message FibRequest {
  int32 n = 1;
}

message FibResponse {
  int32 value = 1;
}

message Number {
  double value = 1;
}

message AverageResponse {
  double average = 1;
}

message ChatMessage {
  string user = 1;
  string message = 2;
  int64 timestamp = 3;
}
```

### Field Rules

- **singular**: Default in proto3, field can have zero or one instance
- **repeated**: Field can be repeated any number of times (including zero)
- **map**: Key-value pair field type

### Field Numbers

Each field has a unique number used to identify fields in the binary format. Numbers 1-15 use 1 byte, 16-2047 use 2 bytes.

---

## Communication Patterns

### 1. Unary RPC
Simple request-response, like a normal function call.

```protobuf
rpc GetUser (UserRequest) returns (UserResponse) {}
```

### 2. Server Streaming RPC
Client sends a single request, server responds with a stream of messages.

```protobuf
rpc ListUsers (ListRequest) returns (stream User) {}
```

**Use Cases**: 
- Large data sets
- Real-time updates
- File downloads

### 3. Client Streaming RPC
Client sends a stream of messages, server responds with a single message.

```protobuf
rpc UploadFile (stream FileChunk) returns (UploadStatus) {}
```

**Use Cases**:
- File uploads
- Batch operations
- Collecting metrics

### 4. Bidirectional Streaming RPC
Both client and server send streams of messages independently.

```protobuf
rpc Chat (stream ChatMessage) returns (stream ChatMessage) {}
```

**Use Cases**:
- Chat applications
- Real-time collaboration
- Game networking

---

## C Implementation

gRPC C API provides the core implementation. It's the foundation for other language bindings.

### Installation

```bash
# Install gRPC C library
git clone --recurse-submodules -b v1.60.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
cd cmake/build
cmake ../..
make
sudo make install
```

### Example .proto File

```protobuf
// greeter.proto
syntax = "proto3";

package helloworld;

service Greeter {
  rpc SayHello (HelloRequest) returns (HelloReply) {}
  rpc SayHelloStream (HelloRequest) returns (stream HelloReply) {}
}

message HelloRequest {
  string name = 1;
}

message HelloReply {
  string message = 1;
}
```

### C Server Implementation

```c
// greeter_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "greeter.pb-c.h"

#define SERVER_ADDRESS "0.0.0.0:50051"

// Handler for SayHello RPC
static void handle_say_hello(grpc_call *call, 
                              const Helloworld__HelloRequest *request,
                              grpc_metadata_array *initial_metadata) {
    Helloworld__HelloReply response = HELLOWORLD__HELLO_REPLY__INIT;
    char greeting[256];
    
    // Create response message
    snprintf(greeting, sizeof(greeting), "Hello, %s!", request->name);
    response.message = greeting;
    
    // Serialize response
    size_t response_size = helloworld__hello_reply__get_packed_size(&response);
    uint8_t *response_buffer = malloc(response_size);
    helloworld__hello_reply__pack(&response, response_buffer);
    
    // Create response slice
    grpc_slice response_slice = grpc_slice_from_copied_buffer(
        (const char *)response_buffer, response_size);
    
    // Send initial metadata
    grpc_op ops[3];
    memset(ops, 0, sizeof(ops));
    
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    
    ops[1].op = GRPC_OP_SEND_MESSAGE;
    grpc_byte_buffer *response_payload = grpc_raw_byte_buffer_create(
        &response_slice, 1);
    ops[1].data.send_message.send_message = response_payload;
    
    ops[2].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    ops[2].data.send_status_from_server.status = GRPC_STATUS_OK;
    ops[2].data.send_status_from_server.trailing_metadata_count = 0;
    grpc_slice status_details = grpc_slice_from_static_string("OK");
    ops[2].data.send_status_from_server.status_details = &status_details;
    
    // Execute operations
    grpc_call_error error = grpc_call_start_batch(
        call, ops, 3, NULL, NULL);
    
    // Cleanup
    grpc_byte_buffer_destroy(response_payload);
    grpc_slice_unref(response_slice);
    free(response_buffer);
}

int main(int argc, char **argv) {
    grpc_init();
    
    // Create server
    grpc_server *server = grpc_server_create(NULL, NULL);
    
    // Add listening port
    grpc_server_credentials *creds = grpc_insecure_server_credentials_create();
    int port = grpc_server_add_insecure_http2_port(server, SERVER_ADDRESS);
    
    if (port == 0) {
        fprintf(stderr, "Failed to bind to %s\n", SERVER_ADDRESS);
        return 1;
    }
    
    printf("Server listening on %s\n", SERVER_ADDRESS);
    
    // Start server
    grpc_server_start(server);
    
    // Server loop
    grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
    grpc_server_register_completion_queue(server, cq, NULL);
    
    // Wait for events (simplified - real implementation needs proper event loop)
    gpr_timespec deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_seconds(3600, GPR_TIMESPAN));
    
    grpc_event event = grpc_completion_queue_next(cq, deadline, NULL);
    
    // Shutdown
    grpc_server_shutdown_and_notify(server, cq, NULL);
    grpc_completion_queue_shutdown(cq);
    grpc_completion_queue_destroy(cq);
    grpc_server_destroy(server);
    grpc_server_credentials_release(creds);
    
    grpc_shutdown();
    return 0;
}
```

### C Client Implementation

```c
// greeter_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "greeter.pb-c.h"

#define SERVER_ADDRESS "localhost:50051"

int main(int argc, char **argv) {
    grpc_init();
    
    // Create channel
    grpc_channel_credentials *creds = grpc_insecure_credentials_create();
    grpc_channel *channel = grpc_channel_create(SERVER_ADDRESS, creds, NULL);
    
    // Create completion queue
    grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
    
    // Prepare request
    Helloworld__HelloRequest request = HELLOWORLD__HELLO_REQUEST__INIT;
    request.name = "World";
    
    size_t request_size = helloworld__hello_request__get_packed_size(&request);
    uint8_t *request_buffer = malloc(request_size);
    helloworld__hello_request__pack(&request, request_buffer);
    
    grpc_slice request_slice = grpc_slice_from_copied_buffer(
        (const char *)request_buffer, request_size);
    grpc_byte_buffer *request_payload = grpc_raw_byte_buffer_create(
        &request_slice, 1);
    
    // Create call
    grpc_slice method = grpc_slice_from_static_string("/helloworld.Greeter/SayHello");
    grpc_call *call = grpc_channel_create_call(
        channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
        method, NULL,
        gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    
    // Prepare operations
    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    
    grpc_metadata_array initial_metadata;
    grpc_metadata_array trailing_metadata;
    grpc_metadata_array_init(&initial_metadata);
    grpc_metadata_array_init(&trailing_metadata);
    
    grpc_byte_buffer *response_payload = NULL;
    grpc_status_code status;
    grpc_slice status_details;
    
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    
    ops[1].op = GRPC_OP_SEND_MESSAGE;
    ops[1].data.send_message.send_message = request_payload;
    
    ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    
    ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
    ops[3].data.recv_initial_metadata.recv_initial_metadata = &initial_metadata;
    
    ops[4].op = GRPC_OP_RECV_MESSAGE;
    ops[4].data.recv_message.recv_message = &response_payload;
    
    ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    ops[5].data.recv_status_on_client.trailing_metadata = &trailing_metadata;
    ops[5].data.recv_status_on_client.status = &status;
    ops[5].data.recv_status_on_client.status_details = &status_details;
    
    // Start batch
    grpc_call_error error = grpc_call_start_batch(call, ops, 6, NULL, NULL);
    
    // Wait for completion
    grpc_event event = grpc_completion_queue_next(
        cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    
    if (event.type == GRPC_OP_COMPLETE && status == GRPC_STATUS_OK) {
        // Parse response
        if (response_payload) {
            grpc_byte_buffer_reader reader;
            grpc_byte_buffer_reader_init(&reader, response_payload);
            
            grpc_slice response_slice = grpc_byte_buffer_reader_readall(&reader);
            size_t response_size = GRPC_SLICE_LENGTH(response_slice);
            uint8_t *response_buffer = GRPC_SLICE_START_PTR(response_slice);
            
            Helloworld__HelloReply *response = helloworld__hello_reply__unpack(
                NULL, response_size, response_buffer);
            
            if (response) {
                printf("Response: %s\n", response->message);
                helloworld__hello_reply__free_unpacked(response, NULL);
            }
            
            grpc_slice_unref(response_slice);
            grpc_byte_buffer_reader_destroy(&reader);
        }
    } else {
        fprintf(stderr, "RPC failed with status %d\n", status);
    }
    
    // Cleanup
    if (response_payload) {
        grpc_byte_buffer_destroy(response_payload);
    }
    grpc_byte_buffer_destroy(request_payload);
    grpc_slice_unref(request_slice);
    free(request_buffer);
    grpc_metadata_array_destroy(&initial_metadata);
    grpc_metadata_array_destroy(&trailing_metadata);
    grpc_slice_unref(status_details);
    grpc_call_unref(call);
    grpc_completion_queue_shutdown(cq);
    grpc_completion_queue_destroy(cq);
    grpc_channel_destroy(channel);
    grpc_channel_credentials_release(creds);
    
    grpc_shutdown();
    return 0;
}
```

### Build Command for C

```bash
# Compile proto file
protoc --c_out=. greeter.proto

# Compile server
gcc greeter_server.c greeter.pb-c.c -o greeter_server \
    -lgrpc -lprotobuf-c -lpthread

# Compile client
gcc greeter_client.c greeter.pb-c.c -o greeter_client \
    -lgrpc -lprotobuf-c -lpthread
```

---

## C++ Implementation

C++ has excellent gRPC support with modern C++ features and a cleaner API.

### Installation

```bash
# Using CMake
cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF ..
make -j
sudo make install
```

### C++ Server Implementation

```cpp
// greeter_server.cc
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include "greeter.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloRequest;
using helloworld::HelloReply;

// Service implementation
class GreeterServiceImpl final : public Greeter::Service {
public:
    // Unary RPC implementation
    Status SayHello(ServerContext* context, 
                    const HelloRequest* request,
                    HelloReply* reply) override {
        std::string prefix("Hello, ");
        reply->set_message(prefix + request->name());
        
        // Access metadata
        const auto& metadata = context->client_metadata();
        for (const auto& pair : metadata) {
            std::cout << "Metadata: " << pair.first << " = " 
                      << pair.second << std::endl;
        }
        
        return Status::OK;
    }
    
    // Server streaming RPC implementation
    Status SayHelloStream(ServerContext* context,
                          const HelloRequest* request,
                          ServerWriter<HelloReply>* writer) override {
        std::string prefix("Hello, ");
        HelloReply reply;
        
        // Send multiple responses
        for (int i = 0; i < 5; i++) {
            reply.set_message(prefix + request->name() + " #" + std::to_string(i));
            
            // Check if client cancelled
            if (context->IsCancelled()) {
                return Status(grpc::StatusCode::CANCELLED, "Client cancelled");
            }
            
            if (!writer->Write(reply)) {
                // Broken stream
                return Status(grpc::StatusCode::UNKNOWN, "Failed to write");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        return Status::OK;
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    GreeterServiceImpl service;
    
    // Enable default health checking service
    grpc::EnableDefaultHealthCheckService(true);
    
    // Enable reflection
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    
    ServerBuilder builder;
    
    // Listen on the given address without authentication
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    
    // Register service
    builder.RegisterService(&service);
    
    // Set options
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);  // 4MB
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);     // 4MB
    
    // Add completion queue (for async operations)
    // std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    
    // Build and start server
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    
    // Wait for server to shutdown
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}
```

### C++ Client Implementation

```cpp
// greeter_client.cc
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "greeter.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloRequest;
using helloworld::HelloReply;

class GreeterClient {
public:
    GreeterClient(std::shared_ptr<Channel> channel)
        : stub_(Greeter::NewStub(channel)) {}
    
    // Unary RPC
    std::string SayHello(const std::string& user) {
        HelloRequest request;
        request.set_name(user);
        
        HelloReply reply;
        ClientContext context;
        
        // Set deadline (timeout)
        std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(10);
        context.set_deadline(deadline);
        
        // Add metadata
        context.AddMetadata("client-id", "cpp-client-001");
        context.AddMetadata("api-key", "secret-key");
        
        // Make RPC call
        Status status = stub_->SayHello(&context, request, &reply);
        
        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << "RPC failed: " << status.error_code() << ": " 
                      << status.error_message() << std::endl;
            return "RPC failed";
        }
    }
    
    // Server streaming RPC
    void SayHelloStream(const std::string& user) {
        HelloRequest request;
        request.set_name(user);
        
        ClientContext context;
        HelloReply reply;
        
        std::unique_ptr<ClientReader<HelloReply>> reader(
            stub_->SayHelloStream(&context, request));
        
        // Read all responses from server
        while (reader->Read(&reply)) {
            std::cout << "Received: " << reply.message() << std::endl;
        }
        
        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "SayHelloStream rpc failed: " 
                      << status.error_message() << std::endl;
        }
    }
    
private:
    std::unique_ptr<Greeter::Stub> stub_;
};

int main(int argc, char** argv) {
    std::string target_str("localhost:50051");
    
    // Create channel with custom options
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(4 * 1024 * 1024);  // 4MB
    args.SetMaxSendMessageSize(4 * 1024 * 1024);     // 4MB
    
    // Use insecure credentials for testing
    GreeterClient client(grpc::CreateCustomChannel(
        target_str, 
        grpc::InsecureChannelCredentials(),
        args));
    
    // Test unary RPC
    std::string user("C++ World");
    std::string reply = client.SayHello(user);
    std::cout << "Greeter received: " << reply << std::endl;
    
    // Test server streaming RPC
    std::cout << "\nStreaming responses:" << std::endl;
    client.SayHelloStream(user);
    
    return 0;
}
```

### Advanced C++ Example: Bidirectional Streaming

```cpp
// chat_client.cc
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

class ChatClient {
public:
    ChatClient(std::shared_ptr<Channel> channel)
        : stub_(Chat::NewStub(channel)) {}
    
    void RunChat(const std::string& username) {
        ClientContext context;
        std::shared_ptr<ClientReaderWriter<ChatMessage, ChatMessage>> stream(
            stub_->StreamChat(&context));
        
        // Thread to read messages from server
        std::thread reader([stream]() {
            ChatMessage server_msg;
            while (stream->Read(&server_msg)) {
                std::cout << "[" << server_msg.user() << "]: " 
                          << server_msg.message() << std::endl;
            }
        });
        
        // Main thread writes messages to server
        ChatMessage client_msg;
        client_msg.set_user(username);
        
        std::string input;
        while (std::getline(std::cin, input)) {
            if (input == "quit") {
                break;
            }
            
            client_msg.set_message(input);
            client_msg.set_timestamp(
                std::chrono::system_clock::now().time_since_epoch().count());
            
            if (!stream->Write(client_msg)) {
                break;
            }
        }
        
        stream->WritesDone();
        reader.join();
        
        Status status = stream->Finish();
        if (!status.ok()) {
            std::cout << "Chat failed: " << status.error_message() << std::endl;
        }
    }
    
private:
    std::unique_ptr<Chat::Stub> stub_;
};
```

### CMakeLists.txt for C++

```cmake
cmake_minimum_required(VERSION 3.10)
project(GreeterExample)

set(CMAKE_CXX_STANDARD 17)

# Find packages
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Generate protobuf and gRPC files
set(PROTO_FILES greeter.proto)
add_library(grpc_proto ${PROTO_FILES})

target_link_libraries(grpc_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
        gRPC::grpc++_reflection
)

target_include_directories(grpc_proto 
    PUBLIC 
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Compile proto files
protobuf_generate(
    TARGET grpc_proto
    LANGUAGE cpp
)

protobuf_generate(
    TARGET grpc_proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
)

# Server executable
add_executable(greeter_server greeter_server.cc)
target_link_libraries(greeter_server grpc_proto)

# Client executable
add_executable(greeter_client greeter_client.cc)
target_link_libraries(greeter_client grpc_proto)
```

---

## Rust Implementation

Rust has excellent gRPC support through the `tonic` crate, providing type-safe, async gRPC.

### Cargo.toml

```toml
[package]
name = "grpc-example"
version = "0.1.0"
edition = "2021"

[dependencies]
tonic = "0.11"
prost = "0.12"
tokio = { version = "1", features = ["macros", "rt-multi-thread"] }
tokio-stream = "0.1"

[build-dependencies]
tonic-build = "0.11"
```

### Build Script (build.rs)

```rust
// build.rs
fn main() -> Result<(), Box<dyn std::error::Error>> {
    tonic_build::configure()
        .build_server(true)
        .build_client(true)
        .compile(
            &["proto/greeter.proto"],
            &["proto/"],
        )?;
    Ok(())
}
```

### Rust Server Implementation

```rust
// src/server.rs
use tonic::{transport::Server, Request, Response, Status};
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;

// Import generated code
pub mod greeter {
    tonic::include_proto!("helloworld");
}

use greeter::greeter_server::{Greeter, GreeterServer};
use greeter::{HelloRequest, HelloReply};

// Service implementation
#[derive(Debug, Default)]
pub struct MyGreeter {}

#[tonic::async_trait]
impl Greeter for MyGreeter {
    // Unary RPC
    async fn say_hello(
        &self,
        request: Request<HelloRequest>,
    ) -> Result<Response<HelloReply>, Status> {
        println!("Got a request from {:?}", request.remote_addr());
        
        // Access metadata
        let metadata = request.metadata();
        for key_and_value in metadata.iter() {
            match key_and_value {
                tonic::metadata::KeyAndValueRef::Ascii(key, value) => {
                    println!("Metadata: {} = {:?}", key, value);
                }
                tonic::metadata::KeyAndValueRef::Binary(key, value) => {
                    println!("Binary Metadata: {} = {:?}", key, value);
                }
            }
        }
        
        let reply = HelloReply {
            message: format!("Hello, {}!", request.into_inner().name),
        };
        
        Ok(Response::new(reply))
    }
    
    // Server streaming RPC
    type SayHelloStreamStream = ReceiverStream<Result<HelloReply, Status>>;
    
    async fn say_hello_stream(
        &self,
        request: Request<HelloRequest>,
    ) -> Result<Response<Self::SayHelloStreamStream>, Status> {
        let name = request.into_inner().name;
        
        // Create channel for streaming responses
        let (tx, rx) = mpsc::channel(4);
        
        // Spawn task to send responses
        tokio::spawn(async move {
            for i in 0..5 {
                let reply = HelloReply {
                    message: format!("Hello, {} #{}!", name, i),
                };
                
                if tx.send(Ok(reply)).await.is_err() {
                    // Client disconnected
                    break;
                }
                
                tokio::time::sleep(tokio::time::Duration::from_millis(500)).await;
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let greeter = MyGreeter::default();
    
    println!("GreeterServer listening on {}", addr);
    
    Server::builder()
        .add_service(GreeterServer::new(greeter))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### Rust Client Implementation

```rust
// src/client.rs
use tonic::Request;
use tokio_stream::StreamExt;

pub mod greeter {
    tonic::include_proto!("helloworld");
}

use greeter::greeter_client::GreeterClient;
use greeter::HelloRequest;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = GreeterClient::connect("http://[::1]:50051").await?;
    
    // Unary RPC
    let mut request = Request::new(HelloRequest {
        name: "Rust World".into(),
    });
    
    // Add metadata
    request.metadata_mut().insert(
        "client-id",
        "rust-client-001".parse().unwrap(),
    );
    
    // Set timeout
    request.set_timeout(std::time::Duration::from_secs(10));
    
    let response = client.say_hello(request).await?;
    println!("RESPONSE={:?}", response.into_inner());
    
    // Server streaming RPC
    println!("\n--- Streaming responses ---");
    let stream_request = Request::new(HelloRequest {
        name: "Streaming Rust".into(),
    });
    
    let mut stream = client
        .say_hello_stream(stream_request)
        .await?
        .into_inner();
    
    while let Some(response) = stream.next().await {
        match response {
            Ok(reply) => println!("Received: {}", reply.message),
            Err(e) => eprintln!("Error: {:?}", e),
        }
    }
    
    Ok(())
}
```

### Advanced Rust Example: Client Streaming

```rust
use tonic::{Request, Response, Status};
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;

pub mod calculator {
    tonic::include_proto!("calculator");
}

use calculator::calculator_server::{Calculator, CalculatorServer};
use calculator::{Number, AverageResponse};

#[derive(Debug, Default)]
pub struct CalculatorService {}

#[tonic::async_trait]
impl Calculator for CalculatorService {
    // Client streaming RPC
    async fn average(
        &self,
        request: Request<tonic::Streaming<Number>>,
    ) -> Result<Response<AverageResponse>, Status> {
        let mut stream = request.into_inner();
        
        let mut sum = 0.0;
        let mut count = 0;
        
        // Receive all numbers from client
        while let Some(number) = stream.message().await? {
            sum += number.value;
            count += 1;
        }
        
        let average = if count > 0 { sum / count as f64 } else { 0.0 };
        
        let reply = AverageResponse { average };
        Ok(Response::new(reply))
    }
}

// Client code
async fn send_numbers() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = calculator::calculator_client::CalculatorClient::connect(
        "http://[::1]:50051"
    ).await?;
    
    // Create channel for outbound stream
    let (tx, rx) = mpsc::channel(10);
    
    // Spawn task to send numbers
    tokio::spawn(async move {
        let numbers = vec![10.0, 20.0, 30.0, 40.0, 50.0];
        for num in numbers {
            tx.send(Number { value: num }).await.unwrap();
        }
    });
    
    let request = Request::new(ReceiverStream::new(rx));
    let response = client.average(request).await?;
    
    println!("Average: {}", response.into_inner().average);
    Ok(())
}
```

### Advanced Rust Example: Bidirectional Streaming

```rust
use tonic::{Request, Response, Status, Streaming};
use tokio::sync::mpsc;
use tokio_stream::{wrappers::ReceiverStream, StreamExt};

pub mod chat {
    tonic::include_proto!("chat");
}

use chat::chat_server::{Chat, ChatServer};
use chat::ChatMessage;

#[derive(Debug, Default)]
pub struct ChatService {}

#[tonic::async_trait]
impl Chat for ChatService {
    type StreamChatStream = ReceiverStream<Result<ChatMessage, Status>>;
    
    async fn stream_chat(
        &self,
        request: Request<Streaming<ChatMessage>>,
    ) -> Result<Response<Self::StreamChatStream>, Status> {
        let mut in_stream = request.into_inner();
        let (tx, rx) = mpsc::channel(128);
        
        // Spawn task to handle bidirectional communication
        tokio::spawn(async move {
            while let Some(result) = in_stream.next().await {
                match result {
                    Ok(msg) => {
                        println!("Received: {} from {}", msg.message, msg.user);
                        
                        // Echo back with modification
                        let response = ChatMessage {
                            user: "Server".to_string(),
                            message: format!("Echo: {}", msg.message),
                            timestamp: chrono::Utc::now().timestamp(),
                        };
                        
                        if tx.send(Ok(response)).await.is_err() {
                            break;
                        }
                    }
                    Err(e) => {
                        eprintln!("Error: {:?}", e);
                        break;
                    }
                }
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

// Client implementation
async fn chat_client() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = chat::chat_client::ChatClient::connect(
        "http://[::1]:50051"
    ).await?;
    
    let (tx, rx) = mpsc::channel(10);
    
    // Spawn task to send messages
    tokio::spawn(async move {
        let messages = vec!["Hello", "How are you?", "Goodbye"];
        for msg in messages {
            let chat_msg = ChatMessage {
                user: "RustClient".to_string(),
                message: msg.to_string(),
                timestamp: chrono::Utc::now().timestamp(),
            };
            tx.send(chat_msg).await.unwrap();
            tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
        }
    });
    
    let request = Request::new(ReceiverStream::new(rx));
    let mut response_stream = client.stream_chat(request).await?.into_inner();
    
    // Receive responses
    while let Some(response) = response_stream.next().await {
        match response {
            Ok(msg) => println!("{}: {}", msg.user, msg.message),
            Err(e) => eprintln!("Error: {:?}", e),
        }
    }
    
    Ok(())
}
```

### Rust with TLS/SSL

```rust
use tonic::transport::{Server, ServerTlsConfig, Certificate, Identity};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Load certificates
    let cert = tokio::fs::read("server-cert.pem").await?;
    let key = tokio::fs::read("server-key.pem").await?;
    let server_identity = Identity::from_pem(cert, key);
    
    // Optional: Load CA cert for mutual TLS
    let ca_cert = tokio::fs::read("ca-cert.pem").await?;
    let ca_cert = Certificate::from_pem(ca_cert);
    
    let tls_config = ServerTlsConfig::new()
        .identity(server_identity)
        .client_ca_root(ca_cert);  // For mutual TLS
    
    let addr = "[::1]:50051".parse()?;
    let greeter = MyGreeter::default();
    
    Server::builder()
        .tls_config(tls_config)?
        .add_service(GreeterServer::new(greeter))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

---

## Advanced Topics

### 1. Authentication & Authorization

#### JWT Authentication Example (C++)

```cpp
// Custom authentication interceptor
class JwtAuthInterceptor : public grpc::experimental::Interceptor {
public:
    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
        if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
            
            auto* map = methods->GetSendInitialMetadata();
            map->insert(std::make_pair("authorization", "Bearer " + GetJwtToken()));
        }
        methods->Proceed();
    }
    
private:
    std::string GetJwtToken() {
        // Generate or retrieve JWT token
        return "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...";
    }
};

// Server-side validation
class AuthenticatedGreeterImpl final : public Greeter::Service {
    Status SayHello(ServerContext* context, 
                    const HelloRequest* request,
                    HelloReply* reply) override {
        const auto& metadata = context->client_metadata();
        auto auth_it = metadata.find("authorization");
        
        if (auth_it == metadata.end()) {
            return Status(grpc::StatusCode::UNAUTHENTICATED, "Missing token");
        }
        
        std::string token = std::string(auth_it->second.data(), 
                                       auth_it->second.length());
        
        if (!ValidateJwtToken(token)) {
            return Status(grpc::StatusCode::UNAUTHENTICATED, "Invalid token");
        }
        
        // Process request
        reply->set_message("Hello, " + request->name());
        return Status::OK;
    }
};
```

#### OAuth2 Authentication (Rust)

```rust
use tonic::{Request, Status};
use tonic::metadata::MetadataValue;

// Interceptor for adding OAuth token
fn add_oauth_token(mut req: Request<()>) -> Result<Request<()>, Status> {
    let token = get_oauth_token()?;
    let bearer = format!("Bearer {}", token);
    
    req.metadata_mut().insert(
        "authorization",
        MetadataValue::try_from(&bearer).unwrap(),
    );
    
    Ok(req)
}

// Server-side validation
async fn validate_token(
    request: Request<HelloRequest>,
) -> Result<Request<HelloRequest>, Status> {
    let token = request
        .metadata()
        .get("authorization")
        .ok_or_else(|| Status::unauthenticated("No token"))?;
    
    let token_str = token.to_str()
        .map_err(|_| Status::unauthenticated("Invalid token format"))?;
    
    if !token_str.starts_with("Bearer ") {
        return Err(Status::unauthenticated("Invalid token format"));
    }
    
    // Validate token (e.g., verify JWT)
    validate_jwt(&token_str[7..])?;
    
    Ok(request)
}
```

### 2. Load Balancing

#### Client-Side Load Balancing (C++)

```cpp
#include <grpcpp/create_channel.h>

void CreateLoadBalancedChannel() {
    grpc::ChannelArguments args;
    
    // Round-robin load balancing
    args.SetLoadBalancingPolicyName("round_robin");
    
    // Multiple server addresses
    std::string target = "dns:///myservice.example.com";
    
    auto channel = grpc::CreateCustomChannel(
        target,
        grpc::InsecureChannelCredentials(),
        args
    );
    
    // Or use service config for more control
    grpc::ChannelArguments args_with_config;
    args_with_config.SetServiceConfigJSON(R"({
        "loadBalancingPolicy": "round_robin",
        "healthCheckConfig": {
            "serviceName": "helloworld.Greeter"
        }
    })");
}
```

### 3. Error Handling Best Practices

#### Structured Error Handling (Rust)

```rust
use tonic::{Code, Status};

#[derive(Debug)]
enum AppError {
    NotFound(String),
    Unauthorized,
    InvalidInput(String),
    Internal(String),
}

impl From<AppError> for Status {
    fn from(error: AppError) -> Self {
        match error {
            AppError::NotFound(msg) => {
                Status::not_found(msg)
            }
            AppError::Unauthorized => {
                Status::unauthenticated("Authentication required")
            }
            AppError::InvalidInput(msg) => {
                Status::invalid_argument(msg)
            }
            AppError::Internal(msg) => {
                Status::internal(msg)
            }
        }
    }
}

// Usage in service
async fn get_user(
    &self,
    request: Request<UserRequest>,
) -> Result<Response<User>, Status> {
    let user_id = request.into_inner().id;
    
    let user = self.db.find_user(user_id)
        .await
        .map_err(|_| AppError::NotFound(
            format!("User {} not found", user_id)
        ))?;
    
    Ok(Response::new(user))
}
```

### 4. Interceptors and Middleware

#### Logging Interceptor (C++)

```cpp
class LoggingInterceptor : public grpc::experimental::Interceptor {
public:
    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
        if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
            std::cout << "Sending request..." << std::endl;
        }
        
        if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
            std::cout << "Received response" << std::endl;
        }
        
        methods->Proceed();
    }
};
```

### 5. Health Checking

#### Health Check Service (Rust)

```rust
use tonic_health::server::HealthReporter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let (mut health_reporter, health_service) = tonic_health::server::health_reporter();
    
    health_reporter
        .set_serving::<GreeterServer<MyGreeter>>()
        .await;
    
    let addr = "[::1]:50051".parse()?;
    
    Server::builder()
        .add_service(health_service)
        .add_service(GreeterServer::new(MyGreeter::default()))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### 6. Compression

```rust
use tonic::codec::CompressionEncoding;

// Server
let server = GreeterServer::new(greeter)
    .send_compressed(CompressionEncoding::Gzip)
    .accept_compressed(CompressionEncoding::Gzip);

// Client
let mut client = GreeterClient::connect("http://[::1]:50051")
    .await?
    .send_compressed(CompressionEncoding::Gzip)
    .accept_compressed(CompressionEncoding::Gzip);
```

---

## Best Practices

### 1. Protocol Buffer Design

- **Use semantic versioning**: Never change field numbers or types
- **Reserve deleted field numbers**: Prevent reuse
- **Use `reserved` keyword**: Document removed fields
- **Choose appropriate types**: Use `bytes` for binary data, `string` for UTF-8 text
- **Nested messages**: Group related fields
- **Enums**: Start at 0, reserve 0 for default

```protobuf
message UserProfile {
    // Reserved for removed fields
    reserved 2, 15, 9 to 11;
    reserved "old_field_name", "deprecated_field";
    
    int32 user_id = 1;
    string username = 3;
    
    enum Status {
        UNKNOWN = 0;  // Default value
        ACTIVE = 1;
        INACTIVE = 2;
    }
    Status status = 4;
    
    message Address {
        string street = 1;
        string city = 2;
        string country = 3;
    }
    Address address = 5;
}
```

### 2. Performance Optimization

- **Connection Pooling**: Reuse channels/connections
- **Batch Requests**: Combine multiple requests when possible
- **Compression**: Enable for large payloads
- **Keep-Alive**: Configure TCP keep-alive
- **Message Size Limits**: Set appropriate max message sizes
- **Streaming**: Use streaming for large data transfers

### 3. Error Handling

- **Use appropriate status codes**: 
  - `OK` (0): Success
  - `CANCELLED` (1): Operation cancelled
  - `INVALID_ARGUMENT` (3): Invalid request
  - `NOT_FOUND` (5): Resource not found
  - `UNAUTHENTICATED` (16): Missing authentication
  - `INTERNAL` (13): Server error

- **Include error details**: Use status messages
- **Implement retries**: With exponential backoff
- **Handle timeouts**: Set reasonable deadlines

### 4. Security

- **Always use TLS in production**: Never use insecure credentials
- **Implement authentication**: JWT, OAuth2, mTLS
- **Validate inputs**: Check all user inputs
- **Rate limiting**: Prevent abuse
- **Audit logging**: Log all security-relevant events

### 5. Testing

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use tonic::Request;
    
    #[tokio::test]
    async fn test_say_hello() {
        let greeter = MyGreeter::default();
        
        let request = Request::new(HelloRequest {
            name: "Test".into(),
        });
        
        let response = greeter.say_hello(request).await.unwrap();
        assert_eq!(response.into_inner().message, "Hello, Test!");
    }
}
```

### 6. Monitoring and Observability

- **Metrics**: Request count, latency, error rate
- **Tracing**: OpenTelemetry integration
- **Logging**: Structured logs with correlation IDs
- **Health checks**: Implement health check service

### 7. Graceful Shutdown

```rust
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let (mut health_reporter, health_service) = 
        tonic_health::server::health_reporter();
    
    let addr = "[::1]:50051".parse()?;
    let greeter = MyGreeter::default();
    
    health_reporter.set_serving::<GreeterServer<MyGreeter>>().await;
    
    let server = Server::builder()
        .add_service(health_service)
        .add_service(GreeterServer::new(greeter))
        .serve_with_shutdown(addr, async {
            tokio::signal::ctrl_c()
                .await
                .expect("failed to listen for ctrl-c");
        });
    
    server.await?;
    Ok(())
}
```

---

## Conclusion

gRPC provides a robust, efficient framework for building distributed systems. Key takeaways:

- **Protocol Buffers** provide efficient serialization and strong typing
- **HTTP/2** enables high-performance, multiplexed communication
- **Multiple patterns** (unary, streaming) support diverse use cases
- **Language support** across C, C++, Rust, and many others
- **Production-ready features**: Authentication, load balancing, health checks

Choose gRPC when you need:
- High performance and low latency
- Strong API contracts
- Bidirectional streaming
- Multi-language support
- Microservices architecture

The examples provided demonstrate practical implementations across C, C++, and Rust, covering basics to advanced patterns including authentication, streaming, and error handling.