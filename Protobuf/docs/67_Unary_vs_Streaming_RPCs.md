# Unary vs Streaming RPCs in gRPC

## Overview

gRPC supports four fundamental RPC patterns that define how data flows between client and server. These patterns determine whether requests and responses are sent as single messages or as streams of messages over time.

## The Four RPC Patterns

### 1. **Unary RPC** (Request-Response)
The simplest pattern where the client sends a single request and receives a single response.

**Use cases:**
- Simple CRUD operations
- Authentication requests
- Configuration queries
- Any operation where you have all data upfront

### 2. **Server Streaming RPC**
Client sends a single request, server responds with a stream of messages.

**Use cases:**
- Downloading large datasets in chunks
- Real-time notifications/updates
- Log streaming
- File downloads

### 3. **Client Streaming RPC**
Client sends a stream of messages, server responds with a single message after processing all client data.

**Use cases:**
- File uploads
- Batch data ingestion
- Aggregating metrics from sensors
- IoT device data collection

### 4. **Bidirectional Streaming RPC**
Both client and server send streams of messages independently. The streams operate independently, so clients and servers can read and write in any order.

**Use cases:**
- Chat applications
- Real-time gaming
- Collaborative editing
- Live video/audio streaming

---

## Protocol Buffer Definitions

```protobuf
syntax = "proto3";

package streaming;

service DataService {
  // Unary RPC
  rpc GetUser(UserRequest) returns (UserResponse);
  
  // Server streaming
  rpc ListUsers(ListRequest) returns (stream UserResponse);
  
  // Client streaming
  rpc UploadData(stream DataChunk) returns (UploadResponse);
  
  // Bidirectional streaming
  rpc Chat(stream ChatMessage) returns (stream ChatMessage);
}

message UserRequest {
  int32 user_id = 1;
}

message UserResponse {
  int32 user_id = 1;
  string name = 2;
  string email = 3;
}

message ListRequest {
  int32 page_size = 1;
}

message DataChunk {
  bytes data = 1;
  int32 chunk_id = 2;
}

message UploadResponse {
  bool success = 1;
  int64 total_bytes = 2;
}

message ChatMessage {
  string user = 1;
  string message = 2;
  int64 timestamp = 3;
}
```

---

## C++ Implementation

### Server Implementation

```cpp
#include <grpcpp/grpcpp.h>
#include "streaming.grpc.pb.h"
#include <thread>
#include <chrono>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerReaderWriter;
using grpc::Status;

class DataServiceImpl final : public streaming::DataService::Service {
public:
  // 1. Unary RPC
  Status GetUser(ServerContext* context, 
                 const streaming::UserRequest* request,
                 streaming::UserResponse* response) override {
    response->set_user_id(request->user_id());
    response->set_name("John Doe");
    response->set_email("john@example.com");
    
    std::cout << "Unary RPC: Returned user " << request->user_id() << std::endl;
    return Status::OK;
  }
  
  // 2. Server Streaming RPC
  Status ListUsers(ServerContext* context,
                   const streaming::ListRequest* request,
                   ServerWriter<streaming::UserResponse>* writer) override {
    int page_size = request->page_size();
    
    for (int i = 0; i < page_size; ++i) {
      streaming::UserResponse user;
      user.set_user_id(i);
      user.set_name("User " + std::to_string(i));
      user.set_email("user" + std::to_string(i) + "@example.com");
      
      if (!writer->Write(user)) {
        // Client disconnected
        break;
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::cout << "Server streaming: Sent user " << i << std::endl;
    }
    
    return Status::OK;
  }
  
  // 3. Client Streaming RPC
  Status UploadData(ServerContext* context,
                    ServerReader<streaming::DataChunk>* reader,
                    streaming::UploadResponse* response) override {
    streaming::DataChunk chunk;
    int64_t total_bytes = 0;
    int chunk_count = 0;
    
    while (reader->Read(&chunk)) {
      total_bytes += chunk.data().size();
      chunk_count++;
      std::cout << "Client streaming: Received chunk " << chunk.chunk_id() 
                << " (" << chunk.data().size() << " bytes)" << std::endl;
    }
    
    response->set_success(true);
    response->set_total_bytes(total_bytes);
    std::cout << "Client streaming: Received " << chunk_count 
              << " chunks, total " << total_bytes << " bytes" << std::endl;
    
    return Status::OK;
  }
  
  // 4. Bidirectional Streaming RPC
  Status Chat(ServerContext* context,
              ServerReaderWriter<streaming::ChatMessage, 
                                streaming::ChatMessage>* stream) override {
    streaming::ChatMessage msg;
    
    while (stream->Read(&msg)) {
      std::cout << "Bidirectional: Received from " << msg.user() 
                << ": " << msg.message() << std::endl;
      
      // Echo back with modification
      streaming::ChatMessage response;
      response.set_user("Server");
      response.set_message("Echo: " + msg.message());
      response.set_timestamp(
        std::chrono::system_clock::now().time_since_epoch().count());
      
      if (!stream->Write(response)) {
        break;
      }
    }
    
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  DataServiceImpl service;
  
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}
```

### Client Implementation

```cpp
#include <grpcpp/grpcpp.h>
#include "streaming.grpc.pb.h"
#include <iostream>
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientWriter;
using grpc::ClientReaderWriter;
using grpc::Status;

class DataServiceClient {
public:
  DataServiceClient(std::shared_ptr<Channel> channel)
    : stub_(streaming::DataService::NewStub(channel)) {}
  
  // 1. Unary RPC
  void GetUser(int user_id) {
    streaming::UserRequest request;
    request.set_user_id(user_id);
    
    streaming::UserResponse response;
    ClientContext context;
    
    Status status = stub_->GetUser(&context, request, &response);
    
    if (status.ok()) {
      std::cout << "Unary: User " << response.user_id() 
                << " - " << response.name() << std::endl;
    } else {
      std::cout << "Unary RPC failed" << std::endl;
    }
  }
  
  // 2. Server Streaming RPC
  void ListUsers(int page_size) {
    streaming::ListRequest request;
    request.set_page_size(page_size);
    
    ClientContext context;
    streaming::UserResponse user;
    
    std::unique_ptr<ClientReader<streaming::UserResponse>> reader(
      stub_->ListUsers(&context, request));
    
    while (reader->Read(&user)) {
      std::cout << "Server streaming: User " << user.user_id() 
                << " - " << user.name() << std::endl;
    }
    
    Status status = reader->Finish();
    if (!status.ok()) {
      std::cout << "Server streaming RPC failed" << std::endl;
    }
  }
  
  // 3. Client Streaming RPC
  void UploadData(int num_chunks) {
    ClientContext context;
    streaming::UploadResponse response;
    
    std::unique_ptr<ClientWriter<streaming::DataChunk>> writer(
      stub_->UploadData(&context, &response));
    
    for (int i = 0; i < num_chunks; ++i) {
      streaming::DataChunk chunk;
      chunk.set_chunk_id(i);
      chunk.set_data("Data chunk " + std::to_string(i));
      
      if (!writer->Write(chunk)) {
        break;
      }
      
      std::cout << "Client streaming: Sent chunk " << i << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    writer->WritesDone();
    Status status = writer->Finish();
    
    if (status.ok()) {
      std::cout << "Client streaming: Upload complete, total bytes: " 
                << response.total_bytes() << std::endl;
    }
  }
  
  // 4. Bidirectional Streaming RPC
  void Chat() {
    ClientContext context;
    
    std::shared_ptr<ClientReaderWriter<streaming::ChatMessage, 
                                       streaming::ChatMessage>> stream(
      stub_->Chat(&context));
    
    // Writer thread
    std::thread writer([stream]() {
      std::vector<std::string> messages = {
        "Hello", "How are you?", "Goodbye"
      };
      
      for (const auto& msg : messages) {
        streaming::ChatMessage chat_msg;
        chat_msg.set_user("Client");
        chat_msg.set_message(msg);
        chat_msg.set_timestamp(
          std::chrono::system_clock::now().time_since_epoch().count());
        
        stream->Write(chat_msg);
        std::cout << "Bidirectional: Sent: " << msg << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      
      stream->WritesDone();
    });
    
    // Reader thread
    streaming::ChatMessage response;
    while (stream->Read(&response)) {
      std::cout << "Bidirectional: Received from " << response.user() 
                << ": " << response.message() << std::endl;
    }
    
    writer.join();
    Status status = stream->Finish();
    if (!status.ok()) {
      std::cout << "Bidirectional RPC failed" << std::endl;
    }
  }

private:
  std::unique_ptr<streaming::DataService::Stub> stub_;
};

int main() {
  DataServiceClient client(
    grpc::CreateChannel("localhost:50051", 
                       grpc::InsecureChannelCredentials()));
  
  client.GetUser(1);
  client.ListUsers(5);
  client.UploadData(10);
  client.Chat();
  
  return 0;
}
```

---

## Rust Implementation

### Server Implementation

```rust
use tonic::{transport::Server, Request, Response, Status, Streaming};
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tokio_stream::StreamExt;

pub mod streaming {
    tonic::include_proto!("streaming");
}

use streaming::data_service_server::{DataService, DataServiceServer};
use streaming::*;

#[derive(Debug, Default)]
pub struct DataServiceImpl {}

#[tonic::async_trait]
impl DataService for DataServiceImpl {
    // 1. Unary RPC
    async fn get_user(
        &self,
        request: Request<UserRequest>,
    ) -> Result<Response<UserResponse>, Status> {
        let req = request.into_inner();
        
        let response = UserResponse {
            user_id: req.user_id,
            name: "John Doe".to_string(),
            email: "john@example.com".to_string(),
        };
        
        println!("Unary RPC: Returned user {}", req.user_id);
        Ok(Response::new(response))
    }
    
    // 2. Server Streaming RPC
    type ListUsersStream = ReceiverStream<Result<UserResponse, Status>>;
    
    async fn list_users(
        &self,
        request: Request<ListRequest>,
    ) -> Result<Response<Self::ListUsersStream>, Status> {
        let page_size = request.into_inner().page_size;
        let (tx, rx) = mpsc::channel(4);
        
        tokio::spawn(async move {
            for i in 0..page_size {
                let user = UserResponse {
                    user_id: i,
                    name: format!("User {}", i),
                    email: format!("user{}@example.com", i),
                };
                
                println!("Server streaming: Sent user {}", i);
                
                if tx.send(Ok(user)).await.is_err() {
                    break;
                }
                
                tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
    
    // 3. Client Streaming RPC
    async fn upload_data(
        &self,
        request: Request<Streaming<DataChunk>>,
    ) -> Result<Response<UploadResponse>, Status> {
        let mut stream = request.into_inner();
        let mut total_bytes: i64 = 0;
        let mut chunk_count = 0;
        
        while let Some(chunk) = stream.next().await {
            match chunk {
                Ok(chunk) => {
                    total_bytes += chunk.data.len() as i64;
                    chunk_count += 1;
                    println!(
                        "Client streaming: Received chunk {} ({} bytes)",
                        chunk.chunk_id,
                        chunk.data.len()
                    );
                }
                Err(e) => {
                    return Err(e);
                }
            }
        }
        
        println!(
            "Client streaming: Received {} chunks, total {} bytes",
            chunk_count, total_bytes
        );
        
        let response = UploadResponse {
            success: true,
            total_bytes,
        };
        
        Ok(Response::new(response))
    }
    
    // 4. Bidirectional Streaming RPC
    type ChatStream = ReceiverStream<Result<ChatMessage, Status>>;
    
    async fn chat(
        &self,
        request: Request<Streaming<ChatMessage>>,
    ) -> Result<Response<Self::ChatStream>, Status> {
        let mut in_stream = request.into_inner();
        let (tx, rx) = mpsc::channel(4);
        
        tokio::spawn(async move {
            while let Some(result) = in_stream.next().await {
                match result {
                    Ok(msg) => {
                        println!(
                            "Bidirectional: Received from {}: {}",
                            msg.user, msg.message
                        );
                        
                        let response = ChatMessage {
                            user: "Server".to_string(),
                            message: format!("Echo: {}", msg.message),
                            timestamp: std::time::SystemTime::now()
                                .duration_since(std::time::UNIX_EPOCH)
                                .unwrap()
                                .as_secs() as i64,
                        };
                        
                        if tx.send(Ok(response)).await.is_err() {
                            break;
                        }
                    }
                    Err(e) => {
                        eprintln!("Error receiving message: {:?}", e);
                        break;
                    }
                }
            }
        });
        
        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    let service = DataServiceImpl::default();
    
    println!("Server listening on {}", addr);
    
    Server::builder()
        .add_service(DataServiceServer::new(service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### Client Implementation

```rust
use streaming::data_service_client::DataServiceClient;
use streaming::*;
use tokio_stream::StreamExt;
use tonic::Request;

pub mod streaming {
    tonic::include_proto!("streaming");
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = DataServiceClient::connect("http://[::1]:50051").await?;
    
    // 1. Unary RPC
    let request = Request::new(UserRequest { user_id: 1 });
    let response = client.get_user(request).await?;
    let user = response.into_inner();
    println!("Unary: User {} - {}", user.user_id, user.name);
    
    // 2. Server Streaming RPC
    let request = Request::new(ListRequest { page_size: 5 });
    let mut stream = client.list_users(request).await?.into_inner();
    
    while let Some(user) = stream.next().await {
        match user {
            Ok(user) => {
                println!("Server streaming: User {} - {}", user.user_id, user.name);
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }
    
    // 3. Client Streaming RPC
    let chunks = tokio_stream::iter(0..10).map(|i| DataChunk {
        chunk_id: i,
        data: format!("Data chunk {}", i).into_bytes(),
    });
    
    let request = Request::new(chunks);
    let response = client.upload_data(request).await?;
    let result = response.into_inner();
    println!(
        "Client streaming: Upload complete, total bytes: {}",
        result.total_bytes
    );
    
    // 4. Bidirectional Streaming RPC
    let messages = vec!["Hello", "How are you?", "Goodbye"];
    
    let outbound = async_stream::stream! {
        for msg in messages {
            yield ChatMessage {
                user: "Client".to_string(),
                message: msg.to_string(),
                timestamp: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
            };
            
            println!("Bidirectional: Sent: {}", msg);
            tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
        }
    };
    
    let request = Request::new(outbound);
    let mut inbound = client.chat(request).await?.into_inner();
    
    while let Some(msg) = inbound.next().await {
        match msg {
            Ok(msg) => {
                println!("Bidirectional: Received from {}: {}", msg.user, msg.message);
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

---

## Summary

**Key Differences:**

| Pattern | Client Sends | Server Sends | Best For |
|---------|-------------|--------------|----------|
| **Unary** | Single message | Single message | Simple request-response, CRUD operations |
| **Server Streaming** | Single message | Stream of messages | Data downloads, notifications, real-time updates |
| **Client Streaming** | Stream of messages | Single message | File uploads, batch data ingestion, aggregations |
| **Bidirectional** | Stream of messages | Stream of messages | Chat, gaming, collaborative tools, real-time communication |

**Implementation Considerations:**

- **Unary RPCs** are simplest and most similar to traditional REST APIs
- **Streaming RPCs** enable efficient handling of large datasets and real-time communication
- **Backpressure** is automatically handled by gRPC's flow control mechanisms
- **Error handling** differs: streaming RPCs can have errors during streaming or at completion
- **Connection management**: Streaming keeps connections open longer, requiring proper resource management

**Performance Benefits:**
- Streaming reduces overhead by reusing HTTP/2 connections
- Client streaming is efficient for uploading large files in chunks
- Server streaming enables progressive data delivery
- Bidirectional streaming minimizes latency for real-time applications