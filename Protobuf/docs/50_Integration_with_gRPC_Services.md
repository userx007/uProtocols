# Integration with gRPC Services

## Overview

gRPC (gRPC Remote Procedure Calls) is a modern, high-performance RPC framework that uses Protocol Buffers as its Interface Definition Language (IDL) and message serialization format. This integration creates a powerful combination for building distributed systems with strong typing, automatic code generation, and efficient binary serialization.

## Service Definitions in Protocol Buffers

In gRPC, you define services using the `service` keyword in `.proto` files. Each service contains one or more RPC methods that specify request and response message types.

### Basic Service Definition

```protobuf
syntax = "proto3";

package example;

// Message definitions
message UserRequest {
  string user_id = 1;
}

message UserResponse {
  string user_id = 1;
  string name = 2;
  string email = 3;
}

message CreateUserRequest {
  string name = 1;
  string email = 2;
}

// Service definition
service UserService {
  // Unary RPC: single request, single response
  rpc GetUser(UserRequest) returns (UserResponse);
  
  // Unary RPC for creating a user
  rpc CreateUser(CreateUserRequest) returns (UserResponse);
}
```

## Streaming Patterns

gRPC supports four types of RPC patterns:

1. **Unary RPC**: Client sends single request, server returns single response
2. **Server Streaming**: Client sends single request, server returns stream of responses
3. **Client Streaming**: Client sends stream of requests, server returns single response
4. **Bidirectional Streaming**: Both client and server send streams of messages

### Complete Service with All Patterns

```protobuf
syntax = "proto3";

package streaming;

message DataPoint {
  int64 timestamp = 1;
  double value = 2;
  string sensor_id = 3;
}

message DataRequest {
  string sensor_id = 1;
  int64 start_time = 2;
  int64 end_time = 3;
}

message AggregateResponse {
  double average = 1;
  double min = 2;
  double max = 3;
  int32 count = 4;
}

message StreamStatus {
  bool success = 1;
  string message = 2;
}

service DataService {
  // Unary: Get single data point
  rpc GetLatest(DataRequest) returns (DataPoint);
  
  // Server streaming: Get historical data
  rpc GetHistory(DataRequest) returns (stream DataPoint);
  
  // Client streaming: Upload multiple data points
  rpc UploadData(stream DataPoint) returns (AggregateResponse);
  
  // Bidirectional streaming: Real-time data processing
  rpc MonitorData(stream DataPoint) returns (stream StreamStatus);
}
```

## C/C++ Implementation

### Server Implementation

```cpp
#include <grpcpp/grpcpp.h>
#include "user_service.grpc.pb.h"
#include <memory>
#include <string>
#include <iostream>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;
using grpc::ServerReader;
using grpc::ServerReaderWriter;

using example::UserService;
using example::UserRequest;
using example::UserResponse;
using example::CreateUserRequest;
using streaming::DataService;
using streaming::DataPoint;
using streaming::DataRequest;
using streaming::AggregateResponse;
using streaming::StreamStatus;

// Unary RPC implementation
class UserServiceImpl final : public UserService::Service {
  Status GetUser(ServerContext* context, 
                 const UserRequest* request,
                 UserResponse* response) override {
    // Simulate database lookup
    response->set_user_id(request->user_id());
    response->set_name("John Doe");
    response->set_email("john@example.com");
    
    std::cout << "GetUser called for ID: " << request->user_id() << std::endl;
    return Status::OK;
  }
  
  Status CreateUser(ServerContext* context,
                    const CreateUserRequest* request,
                    UserResponse* response) override {
    // Simulate user creation
    response->set_user_id("generated_id_123");
    response->set_name(request->name());
    response->set_email(request->email());
    
    std::cout << "CreateUser called: " << request->name() << std::endl;
    return Status::OK;
  }
};

// Streaming RPC implementation
class DataServiceImpl final : public DataService::Service {
  // Server streaming RPC
  Status GetHistory(ServerContext* context,
                    const DataRequest* request,
                    ServerWriter<DataPoint>* writer) override {
    std::cout << "GetHistory called for sensor: " << request->sensor_id() << std::endl;
    
    // Send multiple data points
    for (int i = 0; i < 10; ++i) {
      DataPoint point;
      point.set_timestamp(request->start_time() + i * 1000);
      point.set_value(20.0 + i * 0.5);
      point.set_sensor_id(request->sensor_id());
      
      if (!writer->Write(point)) {
        // Client disconnected
        break;
      }
    }
    
    return Status::OK;
  }
  
  // Client streaming RPC
  Status UploadData(ServerContext* context,
                    ServerReader<DataPoint>* reader,
                    AggregateResponse* response) override {
    DataPoint point;
    double sum = 0.0;
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    int count = 0;
    
    // Read stream from client
    while (reader->Read(&point)) {
      sum += point.value();
      min_val = std::min(min_val, point.value());
      max_val = std::max(max_val, point.value());
      count++;
    }
    
    // Calculate and return aggregate
    response->set_average(count > 0 ? sum / count : 0.0);
    response->set_min(min_val);
    response->set_max(max_val);
    response->set_count(count);
    
    std::cout << "UploadData completed: " << count << " points" << std::endl;
    return Status::OK;
  }
  
  // Bidirectional streaming RPC
  Status MonitorData(ServerContext* context,
                     ServerReaderWriter<StreamStatus, DataPoint>* stream) override {
    DataPoint point;
    
    while (stream->Read(&point)) {
      // Process incoming data point
      StreamStatus status;
      
      // Validate data
      if (point.value() < 0 || point.value() > 100) {
        status.set_success(false);
        status.set_message("Value out of range");
      } else {
        status.set_success(true);
        status.set_message("Data accepted");
      }
      
      // Send status back to client
      if (!stream->Write(status)) {
        break;
      }
    }
    
    return Status::OK;
  }
};

// Server startup
void RunServer() {
  std::string server_address("0.0.0.0:50051");
  UserServiceImpl user_service;
  DataServiceImpl data_service;
  
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&user_service);
  builder.RegisterService(&data_service);
  
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}
```

### Client Implementation

```cpp
#include <grpcpp/grpcpp.h>
#include "user_service.grpc.pb.h"
#include <memory>
#include <string>
#include <iostream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientReaderWriter;

class UserServiceClient {
 public:
  UserServiceClient(std::shared_ptr<Channel> channel)
      : stub_(UserService::NewStub(channel)) {}
  
  // Unary RPC call
  bool GetUser(const std::string& user_id) {
    UserRequest request;
    request.set_user_id(user_id);
    
    UserResponse response;
    ClientContext context;
    
    Status status = stub_->GetUser(&context, request, &response);
    
    if (status.ok()) {
      std::cout << "User: " << response.name() 
                << " (" << response.email() << ")" << std::endl;
      return true;
    } else {
      std::cerr << "RPC failed: " << status.error_message() << std::endl;
      return false;
    }
  }
  
 private:
  std::unique_ptr<UserService::Stub> stub_;
};

class DataServiceClient {
 public:
  DataServiceClient(std::shared_ptr<Channel> channel)
      : stub_(DataService::NewStub(channel)) {}
  
  // Server streaming RPC
  void GetHistory(const std::string& sensor_id) {
    DataRequest request;
    request.set_sensor_id(sensor_id);
    request.set_start_time(1000000);
    request.set_end_time(2000000);
    
    ClientContext context;
    DataPoint point;
    
    std::unique_ptr<ClientReader<DataPoint>> reader(
        stub_->GetHistory(&context, request));
    
    while (reader->Read(&point)) {
      std::cout << "Received: timestamp=" << point.timestamp()
                << " value=" << point.value() << std::endl;
    }
    
    Status status = reader->Finish();
    if (!status.ok()) {
      std::cerr << "GetHistory failed: " << status.error_message() << std::endl;
    }
  }
  
  // Client streaming RPC
  void UploadData() {
    ClientContext context;
    AggregateResponse response;
    
    std::unique_ptr<ClientWriter<DataPoint>> writer(
        stub_->UploadData(&context, &response));
    
    // Send multiple data points
    for (int i = 0; i < 20; ++i) {
      DataPoint point;
      point.set_timestamp(1000000 + i * 1000);
      point.set_value(15.0 + i * 0.3);
      point.set_sensor_id("sensor_001");
      
      if (!writer->Write(point)) {
        break;
      }
    }
    
    writer->WritesDone();
    Status status = writer->Finish();
    
    if (status.ok()) {
      std::cout << "Upload complete. Average: " << response.average() << std::endl;
    }
  }
  
  // Bidirectional streaming RPC
  void MonitorData() {
    ClientContext context;
    
    std::shared_ptr<ClientReaderWriter<DataPoint, StreamStatus>> stream(
        stub_->MonitorData(&context));
    
    // Thread for reading responses
    std::thread reader([stream]() {
      StreamStatus status;
      while (stream->Read(&status)) {
        std::cout << "Status: " << (status.success() ? "OK" : "ERROR")
                  << " - " << status.message() << std::endl;
      }
    });
    
    // Send data points
    for (int i = 0; i < 10; ++i) {
      DataPoint point;
      point.set_timestamp(1000000 + i * 1000);
      point.set_value(50.0 + i * 5.0);
      point.set_sensor_id("sensor_monitor");
      
      stream->Write(point);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    stream->WritesDone();
    reader.join();
    
    Status status = stream->Finish();
    if (!status.ok()) {
      std::cerr << "MonitorData failed: " << status.error_message() << std::endl;
    }
  }
  
 private:
  std::unique_ptr<DataService::Stub> stub_;
};
```

## Rust Implementation

### Server Implementation (using Tonic)

```rust
// Cargo.toml dependencies:
// tonic = "0.11"
// prost = "0.12"
// tokio = { version = "1.0", features = ["macros", "rt-multi-thread"] }
// tokio-stream = "0.1"

use tonic::{transport::Server, Request, Response, Status};
use tokio_stream::wrappers::ReceiverStream;
use tokio::sync::mpsc;

// Generated code from .proto file
pub mod example {
    tonic::include_proto!("example");
}

pub mod streaming {
    tonic::include_proto!("streaming");
}

use example::{
    user_service_server::{UserService, UserServiceServer},
    UserRequest, UserResponse, CreateUserRequest,
};

use streaming::{
    data_service_server::{DataService, DataServiceServer},
    DataPoint, DataRequest, AggregateResponse, StreamStatus,
};

#[derive(Debug, Default)]
pub struct MyUserService {}

#[tonic::async_trait]
impl UserService for MyUserService {
    // Unary RPC
    async fn get_user(
        &self,
        request: Request<UserRequest>,
    ) -> Result<Response<UserResponse>, Status> {
        println!("GetUser called for ID: {}", request.get_ref().user_id);
        
        let response = UserResponse {
            user_id: request.into_inner().user_id,
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        };
        
        Ok(Response::new(response))
    }
    
    async fn create_user(
        &self,
        request: Request<CreateUserRequest>,
    ) -> Result<Response<UserResponse>, Status> {
        let req = request.into_inner();
        println!("CreateUser called: {}", req.name);
        
        let response = UserResponse {
            user_id: "generated_id_123".to_string(),
            name: req.name,
            email: req.email,
        };
        
        Ok(Response::new(response))
    }
}

#[derive(Debug, Default)]
pub struct MyDataService {}

#[tonic::async_trait]
impl DataService for MyDataService {
    type GetHistoryStream = ReceiverStream<Result<DataPoint, Status>>;
    
    // Server streaming RPC
    async fn get_history(
        &self,
        request: Request<DataRequest>,
    ) -> Result<Response<Self::GetHistoryStream>, Status> {
        let req = request.into_inner();
        println!("GetHistory called for sensor: {}", req.sensor_id);
        
        let (tx, rx) = mpsc::channel(128);
        
        tokio::spawn(async move {
            for i in 0..10 {
                let point = DataPoint {
                    timestamp: req.start_time + i * 1000,
                    value: 20.0 + i as f64 * 0.5,
                    sensor_id: req.sensor_id.clone(),
                };
                
                if tx.send(Ok(point)).await.is_err() {
                    break; // Client disconnected
                }
                
                tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
    
    // Client streaming RPC
    async fn upload_data(
        &self,
        request: Request<tonic::Streaming<DataPoint>>,
    ) -> Result<Response<AggregateResponse>, Status> {
        let mut stream = request.into_inner();
        
        let mut sum = 0.0;
        let mut min_val = f64::MAX;
        let mut max_val = f64::MIN;
        let mut count = 0;
        
        while let Some(point) = stream.message().await? {
            sum += point.value;
            min_val = min_val.min(point.value);
            max_val = max_val.max(point.value);
            count += 1;
        }
        
        println!("UploadData completed: {} points", count);
        
        let response = AggregateResponse {
            average: if count > 0 { sum / count as f64 } else { 0.0 },
            min: min_val,
            max: max_val,
            count,
        };
        
        Ok(Response::new(response))
    }
    
    type MonitorDataStream = ReceiverStream<Result<StreamStatus, Status>>;
    
    // Bidirectional streaming RPC
    async fn monitor_data(
        &self,
        request: Request<tonic::Streaming<DataPoint>>,
    ) -> Result<Response<Self::MonitorDataStream>, Status> {
        let mut stream = request.into_inner();
        let (tx, rx) = mpsc::channel(128);
        
        tokio::spawn(async move {
            while let Ok(Some(point)) = stream.message().await {
                let status = if point.value < 0.0 || point.value > 100.0 {
                    StreamStatus {
                        success: false,
                        message: "Value out of range".to_string(),
                    }
                } else {
                    StreamStatus {
                        success: true,
                        message: "Data accepted".to_string(),
                    }
                };
                
                if tx.send(Ok(status)).await.is_err() {
                    break;
                }
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let user_service = MyUserService::default();
    let data_service = MyDataService::default();
    
    println!("Server listening on {}", addr);
    
    Server::builder()
        .add_service(UserServiceServer::new(user_service))
        .add_service(DataServiceServer::new(data_service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### Client Implementation (Rust)

```rust
use example::user_service_client::UserServiceClient;
use example::{UserRequest, CreateUserRequest};
use streaming::data_service_client::DataServiceClient;
use streaming::{DataPoint, DataRequest};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to server
    let mut user_client = UserServiceClient::connect("http://[::1]:50051").await?;
    let mut data_client = DataServiceClient::connect("http://[::1]:50051").await?;
    
    // Unary RPC call
    let request = tonic::Request::new(UserRequest {
        user_id: "user_123".to_string(),
    });
    
    let response = user_client.get_user(request).await?;
    println!("User: {} ({})", response.get_ref().name, response.get_ref().email);
    
    // Server streaming RPC
    let request = tonic::Request::new(DataRequest {
        sensor_id: "sensor_001".to_string(),
        start_time: 1000000,
        end_time: 2000000,
    });
    
    let mut stream = data_client.get_history(request).await?.into_inner();
    
    while let Some(point) = stream.message().await? {
        println!("Received: timestamp={} value={}", point.timestamp, point.value);
    }
    
    // Client streaming RPC
    let (tx, rx) = mpsc::channel(128);
    
    tokio::spawn(async move {
        for i in 0..20 {
            let point = DataPoint {
                timestamp: 1000000 + i * 1000,
                value: 15.0 + i as f64 * 0.3,
                sensor_id: "sensor_001".to_string(),
            };
            tx.send(point).await.unwrap();
        }
    });
    
    let request = tonic::Request::new(ReceiverStream::new(rx));
    let response = data_client.upload_data(request).await?;
    println!("Upload complete. Average: {}", response.get_ref().average);
    
    // Bidirectional streaming
    let (tx, rx) = mpsc::channel(128);
    
    let request = tonic::Request::new(ReceiverStream::new(rx));
    let mut response_stream = data_client.monitor_data(request).await?.into_inner();
    
    // Spawn task to send data
    tokio::spawn(async move {
        for i in 0..10 {
            let point = DataPoint {
                timestamp: 1000000 + i * 1000,
                value: 50.0 + i as f64 * 5.0,
                sensor_id: "sensor_monitor".to_string(),
            };
            tx.send(point).await.unwrap();
            tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
        }
    });
    
    // Read responses
    while let Some(status) = response_stream.message().await? {
        println!("Status: {} - {}", 
                 if status.success { "OK" } else { "ERROR" },
                 status.message);
    }
    
    Ok(())
}
```

## Summary

**gRPC with Protocol Buffers** provides a robust foundation for building distributed services with:

- **Strong typing** through protobuf message definitions and service contracts
- **Four streaming patterns**: unary, server streaming, client streaming, and bidirectional streaming
- **Automatic code generation** for multiple languages (C++, Rust, Go, Python, Java, etc.)
- **Efficient binary serialization** using protobuf's compact wire format
- **Built-in features**: authentication, load balancing, deadlines, cancellation
- **HTTP/2 based** for multiplexing, flow control, and header compression

The integration allows developers to define APIs once in `.proto` files and generate both client and server code automatically, ensuring consistency across services and enabling polyglot microservices architectures. The streaming capabilities make gRPC particularly well-suited for real-time applications, IoT data processing, and high-throughput systems.