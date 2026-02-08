# gRPC over HTTP/2

## Overview

gRPC (gRPC Remote Procedure Call) is a modern, high-performance RPC framework developed by Google that runs on top of HTTP/2. It uses Protocol Buffers (protobuf) as its interface definition language and serialization format, enabling efficient communication between services in distributed systems.

## Key Characteristics

**Protocol Foundation:**
- Built on HTTP/2, leveraging its multiplexing, flow control, and header compression
- Uses HTTP/2's bidirectional streaming capabilities
- Supports multiple concurrent RPCs over a single TCP connection

**Serialization:**
- Protocol Buffers provide compact binary serialization
- Strongly typed contracts defined in `.proto` files
- Automatic code generation for multiple languages

**Communication Patterns:**
- Unary RPCs (single request, single response)
- Server streaming (single request, stream of responses)
- Client streaming (stream of requests, single response)
- Bidirectional streaming (both sides stream simultaneously)

## C++ Implementation

### Protocol Buffer Definition (hello.proto)
```proto
syntax = "proto3";

package hello;

service Greeter {
  rpc SayHello (HelloRequest) returns (HelloReply) {}
  rpc StreamHellos (HelloRequest) returns (stream HelloReply) {}
}

message HelloRequest {
  string name = 1;
}

message HelloReply {
  string message = 1;
}
```

### C++ Server
```cpp
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "hello.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using hello::Greeter;
using hello::HelloRequest;
using hello::HelloReply;

class GreeterServiceImpl final : public Greeter::Service {
    // Unary RPC
    Status SayHello(ServerContext* context, 
                    const HelloRequest* request,
                    HelloReply* reply) override {
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        std::cout << "Received request from: " << request->name() << std::endl;
        return Status::OK;
    }
    
    // Server streaming RPC
    Status StreamHellos(ServerContext* context,
                       const HelloRequest* request,
                       ServerWriter<HelloReply>* writer) override {
        std::cout << "Streaming to: " << request->name() << std::endl;
        
        for (int i = 0; i < 5; i++) {
            HelloReply reply;
            reply.set_message("Hello " + request->name() + 
                            " #" + std::to_string(i));
            
            if (!writer->Write(reply)) {
                break; // Client disconnected
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        return Status::OK;
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    GreeterServiceImpl service;

    ServerBuilder builder;
    
    // Listen without authentication
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    
    // Register service
    builder.RegisterService(&service);
    
    // Build and start server
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    
    server->Wait();
}

int main() {
    RunServer();
    return 0;
}
```

### C++ Client
```cpp
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "hello.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;
using hello::Greeter;
using hello::HelloRequest;
using hello::HelloReply;

class GreeterClient {
public:
    GreeterClient(std::shared_ptr<Channel> channel)
        : stub_(Greeter::NewStub(channel)) {}

    // Unary RPC call
    std::string SayHello(const std::string& user) {
        HelloRequest request;
        request.set_name(user);
        
        HelloReply reply;
        ClientContext context;
        
        // Set timeout
        std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(5);
        context.set_deadline(deadline);
        
        Status status = stub_->SayHello(&context, request, &reply);
        
        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << "RPC failed: " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }
    
    // Server streaming RPC call
    void StreamHellos(const std::string& user) {
        HelloRequest request;
        request.set_name(user);
        
        ClientContext context;
        HelloReply reply;
        
        std::unique_ptr<ClientReader<HelloReply>> reader(
            stub_->StreamHellos(&context, request));
        
        while (reader->Read(&reply)) {
            std::cout << "Received: " << reply.message() << std::endl;
        }
        
        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "StreamHellos RPC failed: " 
                     << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<Greeter::Stub> stub_;
};

int main() {
    // Create channel
    auto channel = grpc::CreateChannel(
        "localhost:50051", 
        grpc::InsecureChannelCredentials());
    
    GreeterClient client(channel);
    
    // Unary call
    std::string reply = client.SayHello("World");
    std::cout << "Client received: " << reply << std::endl;
    
    // Streaming call
    std::cout << "\nStarting stream..." << std::endl;
    client.StreamHellos("Streamer");
    
    return 0;
}
```

## Rust Implementation

### Cargo.toml Dependencies
```toml
[dependencies]
tonic = "0.10"
prost = "0.12"
tokio = { version = "1.0", features = ["macros", "rt-multi-thread"] }

[build-dependencies]
tonic-build = "0.10"
```

### Build Script (build.rs)
```rust
fn main() {
    tonic_build::compile_protos("proto/hello.proto")
        .unwrap_or_else(|e| panic!("Failed to compile protos {:?}", e));
}
```

### Rust Server
```rust
use tonic::{transport::Server, Request, Response, Status};
use hello::greeter_server::{Greeter, GreeterServer};
use hello::{HelloRequest, HelloReply};
use tokio_stream::wrappers::ReceiverStream;
use std::time::Duration;

pub mod hello {
    tonic::include_proto!("hello");
}

#[derive(Debug, Default)]
pub struct MyGreeter {}

#[tonic::async_trait]
impl Greeter for MyGreeter {
    // Unary RPC
    async fn say_hello(
        &self,
        request: Request<HelloRequest>,
    ) -> Result<Response<HelloReply>, Status> {
        println!("Got request: {:?}", request);
        
        let reply = HelloReply {
            message: format!("Hello {}!", request.into_inner().name),
        };
        
        Ok(Response::new(reply))
    }
    
    // Server streaming RPC
    type StreamHellosStream = ReceiverStream<Result<HelloReply, Status>>;
    
    async fn stream_hellos(
        &self,
        request: Request<HelloRequest>,
    ) -> Result<Response<Self::StreamHellosStream>, Status> {
        let name = request.into_inner().name;
        println!("Streaming to: {}", name);
        
        let (tx, rx) = tokio::sync::mpsc::channel(4);
        
        tokio::spawn(async move {
            for i in 0..5 {
                let reply = HelloReply {
                    message: format!("Hello {} #{}", name, i),
                };
                
                if tx.send(Ok(reply)).await.is_err() {
                    break; // Client disconnected
                }
                
                tokio::time::sleep(Duration::from_secs(1)).await;
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let greeter = MyGreeter::default();

    println!("Server listening on {}", addr);

    Server::builder()
        .add_service(GreeterServer::new(greeter))
        .serve(addr)
        .await?;

    Ok(())
}
```

### Rust Client
```rust
use hello::greeter_client::GreeterClient;
use hello::HelloRequest;

pub mod hello {
    tonic::include_proto!("hello");
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = GreeterClient::connect("http://[::1]:50051").await?;

    // Unary call
    let request = tonic::Request::new(HelloRequest {
        name: "World".into(),
    });

    let response = client.say_hello(request).await?;
    println!("RESPONSE: {:?}", response.into_inner());

    // Streaming call
    let request = tonic::Request::new(HelloRequest {
        name: "Streamer".into(),
    });

    let mut stream = client.stream_hellos(request).await?.into_inner();

    println!("\nStreaming responses:");
    while let Some(response) = stream.message().await? {
        println!("Received: {}", response.message);
    }

    Ok(())
}
```

## Advanced Features

### Interceptors and Middleware (Rust)
```rust
use tonic::service::Interceptor;
use tonic::{Request, Status};

#[derive(Clone)]
struct AuthInterceptor {
    token: String,
}

impl Interceptor for AuthInterceptor {
    fn call(&mut self, mut request: Request<()>) -> Result<Request<()>, Status> {
        request.metadata_mut().insert(
            "authorization",
            format!("Bearer {}", self.token).parse().unwrap(),
        );
        Ok(request)
    }
}

// Usage
let interceptor = AuthInterceptor {
    token: "secret-token".to_string(),
};

let client = GreeterClient::with_interceptor(channel, interceptor);
```

### Deadline and Timeouts (C++)
```cpp
ClientContext context;
std::chrono::system_clock::time_point deadline =
    std::chrono::system_clock::now() + std::chrono::seconds(10);
context.set_deadline(deadline);

Status status = stub_->SayHello(&context, request, &reply);
```

### Error Handling with Status Codes
```rust
// Server side
return Err(Status::invalid_argument("Name cannot be empty"));
return Err(Status::not_found("User not found"));
return Err(Status::unauthenticated("Invalid credentials"));

// Client side
match client.say_hello(request).await {
    Ok(response) => println!("Success: {:?}", response),
    Err(e) => {
        println!("Error code: {:?}", e.code());
        println!("Error message: {}", e.message());
    }
}
```

## Summary

gRPC over HTTP/2 represents a significant advancement in RPC technology, combining HTTP/2's efficient transport layer with Protocol Buffers' compact serialization. It excels in microservices architectures where low latency, high throughput, and strong typing are critical. The framework's support for multiple streaming patterns makes it versatile for various use cases, from simple request-response to complex bidirectional communication. Both C++ and Rust provide robust implementations with excellent performance characteristics, though Rust's async/await model and type safety offer additional advantages for modern distributed systems. The automatic code generation from `.proto` files ensures consistency across different language implementations and simplifies API evolution.