# CQRS Command and Query Models with Protocol Buffers

## Detailed Description

### What is CQRS?

Command Query Responsibility Segregation (CQRS) is a design pattern that segregates read and write operations for a data store into separate data models. This architectural pattern treats retrieving data (queries) and changing data (commands) differently, allowing each to be optimized independently.

#### Core Concepts

**Commands:**
- Commands should represent specific business tasks instead of low-level data updates
- Commands update data and do not return results
- Part of the write model
- Include validation and domain logic
- Examples: `CreateOrder`, `UpdateInventory`, `CancelReservation`

**Queries:**
- Queries never alter data. Instead, they return data transfer objects (DTOs) that present the required data in a convenient format, without any domain logic
- Read-only operations
- Part of the read model
- Optimized for presentation layer
- Examples: `GetOrderById`, `ListActiveOrders`, `SearchProducts`

### Why Use Protocol Buffers with CQRS?

Protocol Buffers (protobuf) are ideal for implementing CQRS patterns because they:

1. **Type Safety**: Strongly typed message definitions prevent data inconsistencies
2. **Language Neutrality**: Support multiple programming languages (C++, Rust, Java, Python, Go, etc.)
3. **Versioning**: Built-in support for backward and forward compatibility
4. **Performance**: Binary serialization is compact and fast
5. **Clear Contracts**: Explicit message schemas serve as documentation

### CQRS with Protocol Buffers Benefits

When combining CQRS with Protocol Buffers:

- **Separation of Concerns**: Command and query models can evolve independently
- **Service Communication**: Perfect for microservices using gRPC
- **Event Sourcing Integration**: Commands can generate events stored as protobuf messages
- **Scalability**: Read and write models can be scaled independently
- **Multiple Read Models**: Different protobuf query messages can represent various views of the same data

### When to Use CQRS

The CQRS pattern is useful in scenarios that require a clear separation between commands and reads. Appropriate use cases include:

- Collaborative domains with concurrent users
- Task-based UIs guiding users through workflows
- High-traffic systems requiring independent scaling of reads/writes
- Complex business logic concentrated in write operations
- Systems requiring multiple optimized read models
- Applications using event sourcing patterns

### Architecture Patterns

**Basic CQRS:**
Both read and write models share the same database but maintain distinct logic and message definitions.

**Advanced CQRS:**
Separate databases for read and write models, synchronized through events or messaging systems.

**Event Sourcing + CQRS:**
Commands generate events that update the write model, and event handlers update the query models asynchronously.

---

## Protocol Buffer Definitions

### Command Messages (.proto file)

```protobuf
// commands.proto
syntax = "proto3";

package ecommerce.commands;

import "google/protobuf/timestamp.proto";

// Command Messages - Write Operations

message CreateOrderCommand {
  string command_id = 1;  // Unique command identifier
  string customer_id = 2;
  repeated OrderItem items = 3;
  Address shipping_address = 4;
  google.protobuf.Timestamp timestamp = 5;
}

message OrderItem {
  string product_id = 1;
  int32 quantity = 2;
  double price = 3;
}

message Address {
  string street = 1;
  string city = 2;
  string state = 3;
  string postal_code = 4;
  string country = 5;
}

message UpdateOrderStatusCommand {
  string command_id = 1;
  string order_id = 2;
  OrderStatus new_status = 3;
  string updated_by = 4;
  google.protobuf.Timestamp timestamp = 5;
}

message CancelOrderCommand {
  string command_id = 1;
  string order_id = 2;
  string reason = 3;
  string cancelled_by = 4;
  google.protobuf.Timestamp timestamp = 5;
}

message AddInventoryCommand {
  string command_id = 1;
  string product_id = 2;
  int32 quantity = 3;
  string warehouse_id = 4;
  google.protobuf.Timestamp timestamp = 5;
}

enum OrderStatus {
  ORDER_STATUS_UNSPECIFIED = 0;
  ORDER_STATUS_PENDING = 1;
  ORDER_STATUS_CONFIRMED = 2;
  ORDER_STATUS_SHIPPED = 3;
  ORDER_STATUS_DELIVERED = 4;
  ORDER_STATUS_CANCELLED = 5;
}

// Command Response
message CommandResponse {
  string command_id = 1;
  bool success = 2;
  string message = 3;
  string entity_id = 4;  // ID of created/updated entity
  google.protobuf.Timestamp processed_at = 5;
}
```

### Query Messages (.proto file)

```protobuf
// queries.proto
syntax = "proto3";

package ecommerce.queries;

import "google/protobuf/timestamp.proto";

// Query Messages - Read Operations

message GetOrderByIdQuery {
  string query_id = 1;
  string order_id = 2;
}

message ListOrdersByCustomerQuery {
  string query_id = 1;
  string customer_id = 2;
  int32 page = 3;
  int32 page_size = 4;
}

message SearchOrdersQuery {
  string query_id = 1;
  string search_term = 2;
  OrderStatus status_filter = 3;
  google.protobuf.Timestamp from_date = 4;
  google.protobuf.Timestamp to_date = 5;
  int32 page = 6;
  int32 page_size = 7;
}

message GetInventoryQuery {
  string query_id = 1;
  string product_id = 2;
  string warehouse_id = 3;
}

// Query Response Models - DTOs

message OrderDTO {
  string order_id = 1;
  string customer_id = 2;
  string customer_name = 3;
  repeated OrderItemDTO items = 4;
  AddressDTO shipping_address = 5;
  OrderStatus status = 6;
  double total_amount = 7;
  google.protobuf.Timestamp created_at = 8;
  google.protobuf.Timestamp updated_at = 9;
}

message OrderItemDTO {
  string product_id = 1;
  string product_name = 2;
  int32 quantity = 3;
  double price = 4;
  double subtotal = 5;
}

message AddressDTO {
  string street = 1;
  string city = 2;
  string state = 3;
  string postal_code = 4;
  string country = 5;
}

message OrderListDTO {
  repeated OrderDTO orders = 1;
  int32 total_count = 2;
  int32 page = 3;
  int32 page_size = 4;
}

message InventoryDTO {
  string product_id = 1;
  string product_name = 2;
  int32 available_quantity = 3;
  string warehouse_id = 4;
  google.protobuf.Timestamp last_updated = 5;
}

enum OrderStatus {
  ORDER_STATUS_UNSPECIFIED = 0;
  ORDER_STATUS_PENDING = 1;
  ORDER_STATUS_CONFIRMED = 2;
  ORDER_STATUS_SHIPPED = 3;
  ORDER_STATUS_DELIVERED = 4;
  ORDER_STATUS_CANCELLED = 5;
}

// Query Response Wrapper
message QueryResponse {
  string query_id = 1;
  bool success = 2;
  string message = 3;
  oneof result {
    OrderDTO order = 4;
    OrderListDTO order_list = 5;
    InventoryDTO inventory = 6;
  }
}
```

### gRPC Service Definitions (.proto file)

```protobuf
// services.proto
syntax = "proto3";

package ecommerce.services;

import "commands.proto";
import "queries.proto";

// Command Service - Write Operations
service CommandService {
  rpc CreateOrder(ecommerce.commands.CreateOrderCommand) 
      returns (ecommerce.commands.CommandResponse);
  
  rpc UpdateOrderStatus(ecommerce.commands.UpdateOrderStatusCommand) 
      returns (ecommerce.commands.CommandResponse);
  
  rpc CancelOrder(ecommerce.commands.CancelOrderCommand) 
      returns (ecommerce.commands.CommandResponse);
  
  rpc AddInventory(ecommerce.commands.AddInventoryCommand) 
      returns (ecommerce.commands.CommandResponse);
}

// Query Service - Read Operations
service QueryService {
  rpc GetOrderById(ecommerce.queries.GetOrderByIdQuery) 
      returns (ecommerce.queries.QueryResponse);
  
  rpc ListOrdersByCustomer(ecommerce.queries.ListOrdersByCustomerQuery) 
      returns (ecommerce.queries.QueryResponse);
  
  rpc SearchOrders(ecommerce.queries.SearchOrdersQuery) 
      returns (ecommerce.queries.QueryResponse);
  
  rpc GetInventory(ecommerce.queries.GetInventoryQuery) 
      returns (ecommerce.queries.QueryResponse);
}
```

---

## C/C++ Implementation

### Compilation

```bash
# Install protobuf compiler
# Ubuntu/Debian: sudo apt-get install protobuf-compiler libprotobuf-dev
# macOS: brew install protobuf

# Compile proto files for C++
protoc --cpp_out=. commands.proto
protoc --cpp_out=. queries.proto
protoc --cpp_out=. services.proto

# For gRPC support
protoc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` services.proto
```

### Command Handler Implementation (C++)

```cpp
// command_handler.hpp
#pragma once

#include "commands.pb.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace ecommerce {

class CommandHandler {
public:
    using CommandProcessor = std::function<commands::CommandResponse(const google::protobuf::Message&)>;
    
    CommandHandler();
    
    // Register command processors
    void RegisterHandler(const std::string& command_type, CommandProcessor processor);
    
    // Process commands
    commands::CommandResponse HandleCreateOrder(const commands::CreateOrderCommand& cmd);
    commands::CommandResponse HandleUpdateOrderStatus(const commands::UpdateOrderStatusCommand& cmd);
    commands::CommandResponse HandleCancelOrder(const commands::CancelOrderCommand& cmd);
    commands::CommandResponse HandleAddInventory(const commands::AddInventoryCommand& cmd);
    
private:
    std::unordered_map<std::string, CommandProcessor> handlers_;
    
    // Helper methods
    bool ValidateCommand(const google::protobuf::Message& cmd);
    void PublishEvent(const std::string& event_type, const google::protobuf::Message& event);
    std::string GenerateEntityId();
};

} // namespace ecommerce
```

```cpp
// command_handler.cpp
#include "command_handler.hpp"
#include <iostream>
#include <chrono>
#include <uuid/uuid.h>

namespace ecommerce {

CommandHandler::CommandHandler() {
    // Initialize handlers
}

commands::CommandResponse CommandHandler::HandleCreateOrder(
    const commands::CreateOrderCommand& cmd) {
    
    commands::CommandResponse response;
    response.set_command_id(cmd.command_id());
    
    try {
        // 1. Validate command
        if (cmd.customer_id().empty()) {
            response.set_success(false);
            response.set_message("Customer ID is required");
            return response;
        }
        
        if (cmd.items_size() == 0) {
            response.set_success(false);
            response.set_message("Order must have at least one item");
            return response;
        }
        
        // 2. Business logic validation
        // Check inventory, customer credit, etc.
        
        // 3. Create order entity
        std::string order_id = GenerateEntityId();
        
        // 4. Persist to write database
        // db_->SaveOrder(order_id, cmd);
        
        // 5. Publish OrderCreated event for read model update
        // PublishEvent("OrderCreated", order_event);
        
        // 6. Return success response
        response.set_success(true);
        response.set_message("Order created successfully");
        response.set_entity_id(order_id);
        
        auto now = std::chrono::system_clock::now();
        auto timestamp = response.mutable_processed_at();
        timestamp->set_seconds(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
        
        std::cout << "Order created: " << order_id << std::endl;
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

commands::CommandResponse CommandHandler::HandleUpdateOrderStatus(
    const commands::UpdateOrderStatusCommand& cmd) {
    
    commands::CommandResponse response;
    response.set_command_id(cmd.command_id());
    
    try {
        // Validate order exists
        if (cmd.order_id().empty()) {
            response.set_success(false);
            response.set_message("Order ID is required");
            return response;
        }
        
        // Load order from write database
        // Order order = db_->LoadOrder(cmd.order_id());
        
        // Apply business rules (state transitions)
        // if (!order.CanTransitionTo(cmd.new_status())) {
        //     response.set_success(false);
        //     response.set_message("Invalid status transition");
        //     return response;
        // }
        
        // Update order
        // order.SetStatus(cmd.new_status());
        // db_->SaveOrder(order);
        
        // Publish OrderStatusUpdated event
        // PublishEvent("OrderStatusUpdated", status_event);
        
        response.set_success(true);
        response.set_message("Order status updated");
        response.set_entity_id(cmd.order_id());
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

commands::CommandResponse CommandHandler::HandleCancelOrder(
    const commands::CancelOrderCommand& cmd) {
    
    commands::CommandResponse response;
    response.set_command_id(cmd.command_id());
    
    try {
        // Validate and cancel order
        // Business logic for cancellation
        // Restore inventory if needed
        // Publish OrderCancelled event
        
        response.set_success(true);
        response.set_message("Order cancelled");
        response.set_entity_id(cmd.order_id());
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

commands::CommandResponse CommandHandler::HandleAddInventory(
    const commands::AddInventoryCommand& cmd) {
    
    commands::CommandResponse response;
    response.set_command_id(cmd.command_id());
    
    try {
        // Validate product exists
        // Update inventory in write database
        // Publish InventoryAdded event
        
        response.set_success(true);
        response.set_message("Inventory added");
        response.set_entity_id(cmd.product_id());
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

std::string CommandHandler::GenerateEntityId() {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
}

void CommandHandler::PublishEvent(
    const std::string& event_type, 
    const google::protobuf::Message& event) {
    // Publish to message queue (Kafka, RabbitMQ, etc.)
    std::cout << "Publishing event: " << event_type << std::endl;
}

} // namespace ecommerce
```

### Query Handler Implementation (C++)

```cpp
// query_handler.hpp
#pragma once

#include "queries.pb.h"
#include <memory>
#include <string>
#include <vector>

namespace ecommerce {

class QueryHandler {
public:
    QueryHandler();
    
    // Query methods - read from optimized read database
    queries::QueryResponse HandleGetOrderById(const queries::GetOrderByIdQuery& query);
    queries::QueryResponse HandleListOrdersByCustomer(const queries::ListOrdersByCustomerQuery& query);
    queries::QueryResponse HandleSearchOrders(const queries::SearchOrdersQuery& query);
    queries::QueryResponse HandleGetInventory(const queries::GetInventoryQuery& query);
    
private:
    // Helper methods
    queries::OrderDTO BuildOrderDTO(const std::string& order_id);
    queries::OrderListDTO BuildOrderListDTO(const std::vector<std::string>& order_ids, 
                                           int total_count, int page, int page_size);
};

} // namespace ecommerce
```

```cpp
// query_handler.cpp
#include "query_handler.hpp"
#include <iostream>

namespace ecommerce {

QueryHandler::QueryHandler() {
    // Initialize read database connection
}

queries::QueryResponse QueryHandler::HandleGetOrderById(
    const queries::GetOrderByIdQuery& query) {
    
    queries::QueryResponse response;
    response.set_query_id(query.query_id());
    
    try {
        if (query.order_id().empty()) {
            response.set_success(false);
            response.set_message("Order ID is required");
            return response;
        }
        
        // Query optimized read database (could be different from write DB)
        // This might be a denormalized view, cache, or read replica
        // Order order = read_db_->GetOrder(query.order_id());
        
        // Build DTO
        queries::OrderDTO order_dto = BuildOrderDTO(query.order_id());
        
        // Set response
        response.set_success(true);
        response.set_message("Order found");
        *response.mutable_order() = order_dto;
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

queries::QueryResponse QueryHandler::HandleListOrdersByCustomer(
    const queries::ListOrdersByCustomerQuery& query) {
    
    queries::QueryResponse response;
    response.set_query_id(query.query_id());
    
    try {
        // Query from read model optimized for this specific query
        // std::vector<std::string> order_ids = read_db_->GetOrdersByCustomer(
        //     query.customer_id(), query.page(), query.page_size());
        // int total = read_db_->CountOrdersByCustomer(query.customer_id());
        
        std::vector<std::string> order_ids = {"order-1", "order-2"};
        int total = 2;
        
        queries::OrderListDTO list_dto = BuildOrderListDTO(
            order_ids, total, query.page(), query.page_size());
        
        response.set_success(true);
        response.set_message("Orders retrieved");
        *response.mutable_order_list() = list_dto;
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

queries::QueryResponse QueryHandler::HandleSearchOrders(
    const queries::SearchOrdersQuery& query) {
    
    queries::QueryResponse response;
    response.set_query_id(query.query_id());
    
    try {
        // Complex search query on read model
        // Could use Elasticsearch or similar for full-text search
        
        std::vector<std::string> order_ids;
        // order_ids = search_engine_->SearchOrders(
        //     query.search_term(), 
        //     query.status_filter(),
        //     query.from_date(),
        //     query.to_date());
        
        queries::OrderListDTO list_dto = BuildOrderListDTO(
            order_ids, order_ids.size(), query.page(), query.page_size());
        
        response.set_success(true);
        response.set_message("Search completed");
        *response.mutable_order_list() = list_dto;
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

queries::QueryResponse QueryHandler::HandleGetInventory(
    const queries::GetInventoryQuery& query) {
    
    queries::QueryResponse response;
    response.set_query_id(query.query_id());
    
    try {
        // Query from inventory read model
        queries::InventoryDTO* inventory = response.mutable_inventory();
        inventory->set_product_id(query.product_id());
        inventory->set_product_name("Sample Product");
        inventory->set_available_quantity(100);
        inventory->set_warehouse_id(query.warehouse_id());
        
        response.set_success(true);
        response.set_message("Inventory retrieved");
        
    } catch (const std::exception& e) {
        response.set_success(false);
        response.set_message(std::string("Error: ") + e.what());
    }
    
    return response;
}

queries::OrderDTO QueryHandler::BuildOrderDTO(const std::string& order_id) {
    queries::OrderDTO dto;
    
    // Fetch from read database and populate DTO
    dto.set_order_id(order_id);
    dto.set_customer_id("customer-123");
    dto.set_customer_name("John Doe");
    dto.set_status(queries::ORDER_STATUS_CONFIRMED);
    dto.set_total_amount(299.99);
    
    // Add items
    queries::OrderItemDTO* item = dto.add_items();
    item->set_product_id("product-1");
    item->set_product_name("Product Name");
    item->set_quantity(2);
    item->set_price(149.99);
    item->set_subtotal(299.98);
    
    // Add address
    queries::AddressDTO* address = dto.mutable_shipping_address();
    address->set_street("123 Main St");
    address->set_city("San Francisco");
    address->set_state("CA");
    address->set_postal_code("94102");
    address->set_country("USA");
    
    return dto;
}

queries::OrderListDTO QueryHandler::BuildOrderListDTO(
    const std::vector<std::string>& order_ids,
    int total_count,
    int page,
    int page_size) {
    
    queries::OrderListDTO list;
    
    for (const auto& order_id : order_ids) {
        queries::OrderDTO* order = list.add_orders();
        *order = BuildOrderDTO(order_id);
    }
    
    list.set_total_count(total_count);
    list.set_page(page);
    list.set_page_size(page_size);
    
    return list;
}

} // namespace ecommerce
```

### Client Usage Example (C++)

```cpp
// client_example.cpp
#include "commands.pb.h"
#include "queries.pb.h"
#include "command_handler.hpp"
#include "query_handler.hpp"
#include <iostream>
#include <memory>

int main() {
    // Initialize handlers
    ecommerce::CommandHandler command_handler;
    ecommerce::QueryHandler query_handler;
    
    // === COMMAND SIDE: Create an order ===
    std::cout << "=== Creating Order ===" << std::endl;
    
    ecommerce::commands::CreateOrderCommand create_cmd;
    create_cmd.set_command_id("cmd-001");
    create_cmd.set_customer_id("customer-123");
    
    // Add order items
    auto* item = create_cmd.add_items();
    item->set_product_id("product-1");
    item->set_quantity(2);
    item->set_price(149.99);
    
    // Add shipping address
    auto* address = create_cmd.mutable_shipping_address();
    address->set_street("123 Main St");
    address->set_city("San Francisco");
    address->set_state("CA");
    address->set_postal_code("94102");
    address->set_country("USA");
    
    // Execute command
    ecommerce::commands::CommandResponse cmd_response = 
        command_handler.HandleCreateOrder(create_cmd);
    
    if (cmd_response.success()) {
        std::cout << "Order created: " << cmd_response.entity_id() << std::endl;
        std::cout << "Message: " << cmd_response.message() << std::endl;
    } else {
        std::cerr << "Failed: " << cmd_response.message() << std::endl;
        return 1;
    }
    
    std::string order_id = cmd_response.entity_id();
    
    // === QUERY SIDE: Retrieve the order ===
    std::cout << "\n=== Querying Order ===" << std::endl;
    
    ecommerce::queries::GetOrderByIdQuery get_query;
    get_query.set_query_id("query-001");
    get_query.set_order_id(order_id);
    
    // Execute query
    ecommerce::queries::QueryResponse query_response = 
        query_handler.HandleGetOrderById(get_query);
    
    if (query_response.success()) {
        const auto& order = query_response.order();
        std::cout << "Order ID: " << order.order_id() << std::endl;
        std::cout << "Customer: " << order.customer_name() << std::endl;
        std::cout << "Status: " << order.status() << std::endl;
        std::cout << "Total: $" << order.total_amount() << std::endl;
        std::cout << "Items: " << order.items_size() << std::endl;
        
        for (const auto& item : order.items()) {
            std::cout << "  - " << item.product_name() 
                     << " x " << item.quantity() 
                     << " @ $" << item.price() << std::endl;
        }
    } else {
        std::cerr << "Query failed: " << query_response.message() << std::endl;
    }
    
    // === COMMAND: Update order status ===
    std::cout << "\n=== Updating Order Status ===" << std::endl;
    
    ecommerce::commands::UpdateOrderStatusCommand update_cmd;
    update_cmd.set_command_id("cmd-002");
    update_cmd.set_order_id(order_id);
    update_cmd.set_new_status(ecommerce::commands::ORDER_STATUS_SHIPPED);
    update_cmd.set_updated_by("admin-user");
    
    cmd_response = command_handler.HandleUpdateOrderStatus(update_cmd);
    
    if (cmd_response.success()) {
        std::cout << "Status updated: " << cmd_response.message() << std::endl;
    }
    
    // === QUERY: List customer orders ===
    std::cout << "\n=== Listing Customer Orders ===" << std::endl;
    
    ecommerce::queries::ListOrdersByCustomerQuery list_query;
    list_query.set_query_id("query-002");
    list_query.set_customer_id("customer-123");
    list_query.set_page(1);
    list_query.set_page_size(10);
    
    query_response = query_handler.HandleListOrdersByCustomer(list_query);
    
    if (query_response.success()) {
        const auto& order_list = query_response.order_list();
        std::cout << "Found " << order_list.total_count() << " orders" << std::endl;
        
        for (const auto& order : order_list.orders()) {
            std::cout << "  Order: " << order.order_id() 
                     << " - $" << order.total_amount() << std::endl;
        }
    }
    
    return 0;
}
```

---

## Rust Implementation

### Compilation

```bash
# Add to Cargo.toml
# [dependencies]
# prost = "0.12"
# prost-types = "0.12"
# tokio = { version = "1", features = ["full"] }
# tonic = "0.10"
# uuid = { version = "1.0", features = ["v4"] }
#
# [build-dependencies]
# tonic-build = "0.10"

# build.rs file to compile proto files
```

### Build Script (build.rs)

```rust
// build.rs
fn main() -> Result<(), Box<dyn std::error::Error>> {
    tonic_build::configure()
        .build_server(true)
        .build_client(true)
        .compile(
            &["proto/commands.proto", "proto/queries.proto", "proto/services.proto"],
            &["proto"],
        )?;
    Ok(())
}
```

### Command Handler Implementation (Rust)

```rust
// command_handler.rs
use tonic::{Request, Response, Status};
use uuid::Uuid;
use std::time::SystemTime;

// Generated protobuf code
pub mod commands {
    tonic::include_proto!("ecommerce.commands");
}

use commands::*;

pub struct CommandHandler {
    // Database connections, event publisher, etc.
}

impl CommandHandler {
    pub fn new() -> Self {
        CommandHandler {}
    }
    
    pub async fn handle_create_order(
        &self,
        cmd: CreateOrderCommand,
    ) -> Result<CommandResponse, String> {
        // Validate command
        if cmd.customer_id.is_empty() {
            return Ok(CommandResponse {
                command_id: cmd.command_id.clone(),
                success: false,
                message: "Customer ID is required".to_string(),
                entity_id: String::new(),
                processed_at: None,
            });
        }
        
        if cmd.items.is_empty() {
            return Ok(CommandResponse {
                command_id: cmd.command_id.clone(),
                success: false,
                message: "Order must have at least one item".to_string(),
                entity_id: String::new(),
                processed_at: None,
            });
        }
        
        // Business logic validation
        // Check inventory, customer credit, etc.
        
        // Generate order ID
        let order_id = Uuid::new_v4().to_string();
        
        // Persist to write database
        // self.db.save_order(&order_id, &cmd).await?;
        
        // Publish OrderCreated event for read model updates
        // self.event_publisher.publish("OrderCreated", order_event).await?;
        
        println!("Order created: {}", order_id);
        
        Ok(CommandResponse {
            command_id: cmd.command_id,
            success: true,
            message: "Order created successfully".to_string(),
            entity_id: order_id,
            processed_at: Some(prost_types::Timestamp {
                seconds: SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                nanos: 0,
            }),
        })
    }
    
    pub async fn handle_update_order_status(
        &self,
        cmd: UpdateOrderStatusCommand,
    ) -> Result<CommandResponse, String> {
        // Validate order exists
        if cmd.order_id.is_empty() {
            return Ok(CommandResponse {
                command_id: cmd.command_id.clone(),
                success: false,
                message: "Order ID is required".to_string(),
                entity_id: String::new(),
                processed_at: None,
            });
        }
        
        // Load order from write database
        // let order = self.db.load_order(&cmd.order_id).await?;
        
        // Validate state transition
        // if !order.can_transition_to(cmd.new_status) {
        //     return Ok(CommandResponse {
        //         success: false,
        //         message: "Invalid status transition".to_string(),
        //         ...
        //     });
        // }
        
        // Update order
        // order.set_status(cmd.new_status);
        // self.db.save_order(&order).await?;
        
        // Publish OrderStatusUpdated event
        // self.event_publisher.publish("OrderStatusUpdated", event).await?;
        
        Ok(CommandResponse {
            command_id: cmd.command_id,
            success: true,
            message: "Order status updated".to_string(),
            entity_id: cmd.order_id,
            processed_at: Some(prost_types::Timestamp {
                seconds: SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                nanos: 0,
            }),
        })
    }
    
    pub async fn handle_cancel_order(
        &self,
        cmd: CancelOrderCommand,
    ) -> Result<CommandResponse, String> {
        // Validate and cancel order
        // Business logic for cancellation
        // Restore inventory if needed
        // Publish OrderCancelled event
        
        Ok(CommandResponse {
            command_id: cmd.command_id,
            success: true,
            message: "Order cancelled".to_string(),
            entity_id: cmd.order_id,
            processed_at: Some(prost_types::Timestamp {
                seconds: SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                nanos: 0,
            }),
        })
    }
    
    pub async fn handle_add_inventory(
        &self,
        cmd: AddInventoryCommand,
    ) -> Result<CommandResponse, String> {
        // Validate product exists
        // Update inventory in write database
        // Publish InventoryAdded event
        
        Ok(CommandResponse {
            command_id: cmd.command_id,
            success: true,
            message: "Inventory added".to_string(),
            entity_id: cmd.product_id,
            processed_at: Some(prost_types::Timestamp {
                seconds: SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                nanos: 0,
            }),
        })
    }
}
```

### Query Handler Implementation (Rust)

```rust
// query_handler.rs
use tonic::{Request, Response, Status};
use std::time::SystemTime;

// Generated protobuf code
pub mod queries {
    tonic::include_proto!("ecommerce.queries");
}

use queries::*;

pub struct QueryHandler {
    // Read database connections
}

impl QueryHandler {
    pub fn new() -> Self {
        QueryHandler {}
    }
    
    pub async fn handle_get_order_by_id(
        &self,
        query: GetOrderByIdQuery,
    ) -> Result<QueryResponse, String> {
        if query.order_id.is_empty() {
            return Ok(QueryResponse {
                query_id: query.query_id.clone(),
                success: false,
                message: "Order ID is required".to_string(),
                result: None,
            });
        }
        
        // Query optimized read database
        // This might be a denormalized view, cache, or read replica
        // let order = self.read_db.get_order(&query.order_id).await?;
        
        // Build DTO
        let order_dto = self.build_order_dto(&query.order_id).await;
        
        Ok(QueryResponse {
            query_id: query.query_id,
            success: true,
            message: "Order found".to_string(),
            result: Some(query_response::Result::Order(order_dto)),
        })
    }
    
    pub async fn handle_list_orders_by_customer(
        &self,
        query: ListOrdersByCustomerQuery,
    ) -> Result<QueryResponse, String> {
        // Query from read model optimized for this specific query
        // let order_ids = self.read_db.get_orders_by_customer(
        //     &query.customer_id,
        //     query.page,
        //     query.page_size
        // ).await?;
        // let total = self.read_db.count_orders_by_customer(&query.customer_id).await?;
        
        let order_ids = vec!["order-1".to_string(), "order-2".to_string()];
        let total = 2;
        
        let list_dto = self.build_order_list_dto(
            order_ids,
            total,
            query.page,
            query.page_size,
        ).await;
        
        Ok(QueryResponse {
            query_id: query.query_id,
            success: true,
            message: "Orders retrieved".to_string(),
            result: Some(query_response::Result::OrderList(list_dto)),
        })
    }
    
    pub async fn handle_search_orders(
        &self,
        query: SearchOrdersQuery,
    ) -> Result<QueryResponse, String> {
        // Complex search query on read model
        // Could use Elasticsearch or similar for full-text search
        
        // let order_ids = self.search_engine.search_orders(
        //     &query.search_term,
        //     query.status_filter,
        //     query.from_date,
        //     query.to_date,
        // ).await?;
        
        let order_ids: Vec<String> = vec![];
        let list_dto = self.build_order_list_dto(
            order_ids.clone(),
            order_ids.len() as i32,
            query.page,
            query.page_size,
        ).await;
        
        Ok(QueryResponse {
            query_id: query.query_id,
            success: true,
            message: "Search completed".to_string(),
            result: Some(query_response::Result::OrderList(list_dto)),
        })
    }
    
    pub async fn handle_get_inventory(
        &self,
        query: GetInventoryQuery,
    ) -> Result<QueryResponse, String> {
        // Query from inventory read model
        let inventory = InventoryDto {
            product_id: query.product_id.clone(),
            product_name: "Sample Product".to_string(),
            available_quantity: 100,
            warehouse_id: query.warehouse_id.clone(),
            last_updated: Some(prost_types::Timestamp {
                seconds: SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64,
                nanos: 0,
            }),
        };
        
        Ok(QueryResponse {
            query_id: query.query_id,
            success: true,
            message: "Inventory retrieved".to_string(),
            result: Some(query_response::Result::Inventory(inventory)),
        })
    }
    
    async fn build_order_dto(&self, order_id: &str) -> OrderDto {
        // Fetch from read database and populate DTO
        OrderDto {
            order_id: order_id.to_string(),
            customer_id: "customer-123".to_string(),
            customer_name: "John Doe".to_string(),
            items: vec![
                OrderItemDto {
                    product_id: "product-1".to_string(),
                    product_name: "Product Name".to_string(),
                    quantity: 2,
                    price: 149.99,
                    subtotal: 299.98,
                }
            ],
            shipping_address: Some(AddressDto {
                street: "123 Main St".to_string(),
                city: "San Francisco".to_string(),
                state: "CA".to_string(),
                postal_code: "94102".to_string(),
                country: "USA".to_string(),
            }),
            status: OrderStatus::OrderStatusConfirmed as i32,
            total_amount: 299.99,
            created_at: Some(prost_types::Timestamp::default()),
            updated_at: Some(prost_types::Timestamp::default()),
        }
    }
    
    async fn build_order_list_dto(
        &self,
        order_ids: Vec<String>,
        total_count: i32,
        page: i32,
        page_size: i32,
    ) -> OrderListDto {
        let mut orders = Vec::new();
        
        for order_id in order_ids {
            orders.push(self.build_order_dto(&order_id).await);
        }
        
        OrderListDto {
            orders,
            total_count,
            page,
            page_size,
        }
    }
}
```

### gRPC Service Implementation (Rust)

```rust
// grpc_service.rs
use tonic::{Request, Response, Status};

pub mod services {
    tonic::include_proto!("ecommerce.services");
}

use services::command_service_server::CommandService;
use services::query_service_server::QueryService;
use crate::command_handler::CommandHandler;
use crate::query_handler::QueryHandler;
use crate::commands::*;
use crate::queries::*;

pub struct CommandServiceImpl {
    handler: CommandHandler,
}

impl CommandServiceImpl {
    pub fn new() -> Self {
        CommandServiceImpl {
            handler: CommandHandler::new(),
        }
    }
}

#[tonic::async_trait]
impl CommandService for CommandServiceImpl {
    async fn create_order(
        &self,
        request: Request<CreateOrderCommand>,
    ) -> Result<Response<CommandResponse>, Status> {
        let cmd = request.into_inner();
        
        match self.handler.handle_create_order(cmd).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn update_order_status(
        &self,
        request: Request<UpdateOrderStatusCommand>,
    ) -> Result<Response<CommandResponse>, Status> {
        let cmd = request.into_inner();
        
        match self.handler.handle_update_order_status(cmd).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn cancel_order(
        &self,
        request: Request<CancelOrderCommand>,
    ) -> Result<Response<CommandResponse>, Status> {
        let cmd = request.into_inner();
        
        match self.handler.handle_cancel_order(cmd).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn add_inventory(
        &self,
        request: Request<AddInventoryCommand>,
    ) -> Result<Response<CommandResponse>, Status> {
        let cmd = request.into_inner();
        
        match self.handler.handle_add_inventory(cmd).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
}

pub struct QueryServiceImpl {
    handler: QueryHandler,
}

impl QueryServiceImpl {
    pub fn new() -> Self {
        QueryServiceImpl {
            handler: QueryHandler::new(),
        }
    }
}

#[tonic::async_trait]
impl QueryService for QueryServiceImpl {
    async fn get_order_by_id(
        &self,
        request: Request<GetOrderByIdQuery>,
    ) -> Result<Response<QueryResponse>, Status> {
        let query = request.into_inner();
        
        match self.handler.handle_get_order_by_id(query).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn list_orders_by_customer(
        &self,
        request: Request<ListOrdersByCustomerQuery>,
    ) -> Result<Response<QueryResponse>, Status> {
        let query = request.into_inner();
        
        match self.handler.handle_list_orders_by_customer(query).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn search_orders(
        &self,
        request: Request<SearchOrdersQuery>,
    ) -> Result<Response<QueryResponse>, Status> {
        let query = request.into_inner();
        
        match self.handler.handle_search_orders(query).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
    
    async fn get_inventory(
        &self,
        request: Request<GetInventoryQuery>,
    ) -> Result<Response<QueryResponse>, Status> {
        let query = request.into_inner();
        
        match self.handler.handle_get_inventory(query).await {
            Ok(response) => Ok(Response::new(response)),
            Err(e) => Err(Status::internal(e)),
        }
    }
}
```

### Server Main (Rust)

```rust
// main.rs
use tonic::transport::Server;

mod command_handler;
mod query_handler;
mod grpc_service;

use grpc_service::services::command_service_server::CommandServiceServer;
use grpc_service::services::query_service_server::QueryServiceServer;
use grpc_service::{CommandServiceImpl, QueryServiceImpl};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "[::1]:50051".parse()?;
    
    let command_service = CommandServiceImpl::new();
    let query_service = QueryServiceImpl::new();
    
    println!("gRPC server listening on {}", addr);
    
    Server::builder()
        .add_service(CommandServiceServer::new(command_service))
        .add_service(QueryServiceServer::new(query_service))
        .serve(addr)
        .await?;
    
    Ok(())
}
```

### Client Usage Example (Rust)

```rust
// client.rs
use tonic::Request;

mod services {
    tonic::include_proto!("ecommerce.services");
}

use services::command_service_client::CommandServiceClient;
use services::query_service_client::QueryServiceClient;

mod commands {
    tonic::include_proto!("ecommerce.commands");
}

mod queries {
    tonic::include_proto!("ecommerce.queries");
}

use commands::*;
use queries::*;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to services
    let mut command_client = CommandServiceClient::connect("http://[::1]:50051").await?;
    let mut query_client = QueryServiceClient::connect("http://[::1]:50051").await?;
    
    // === COMMAND: Create an order ===
    println!("=== Creating Order ===");
    
    let create_cmd = CreateOrderCommand {
        command_id: "cmd-001".to_string(),
        customer_id: "customer-123".to_string(),
        items: vec![
            OrderItem {
                product_id: "product-1".to_string(),
                quantity: 2,
                price: 149.99,
            }
        ],
        shipping_address: Some(Address {
            street: "123 Main St".to_string(),
            city: "San Francisco".to_string(),
            state: "CA".to_string(),
            postal_code: "94102".to_string(),
            country: "USA".to_string(),
        }),
        timestamp: None,
    };
    
    let response = command_client.create_order(Request::new(create_cmd)).await?;
    let cmd_response = response.into_inner();
    
    if cmd_response.success {
        println!("Order created: {}", cmd_response.entity_id);
        println!("Message: {}", cmd_response.message);
    } else {
        eprintln!("Failed: {}", cmd_response.message);
        return Ok(());
    }
    
    let order_id = cmd_response.entity_id.clone();
    
    // === QUERY: Retrieve the order ===
    println!("\n=== Querying Order ===");
    
    let get_query = GetOrderByIdQuery {
        query_id: "query-001".to_string(),
        order_id: order_id.clone(),
    };
    
    let response = query_client.get_order_by_id(Request::new(get_query)).await?;
    let query_response = response.into_inner();
    
    if query_response.success {
        if let Some(query_response::Result::Order(order)) = query_response.result {
            println!("Order ID: {}", order.order_id);
            println!("Customer: {}", order.customer_name);
            println!("Status: {}", order.status);
            println!("Total: ${}", order.total_amount);
            println!("Items: {}", order.items.len());
            
            for item in &order.items {
                println!(
                    "  - {} x {} @ ${}",
                    item.product_name, item.quantity, item.price
                );
            }
        }
    } else {
        eprintln!("Query failed: {}", query_response.message);
    }
    
    // === COMMAND: Update order status ===
    println!("\n=== Updating Order Status ===");
    
    let update_cmd = UpdateOrderStatusCommand {
        command_id: "cmd-002".to_string(),
        order_id: order_id.clone(),
        new_status: OrderStatus::OrderStatusShipped as i32,
        updated_by: "admin-user".to_string(),
        timestamp: None,
    };
    
    let response = command_client.update_order_status(Request::new(update_cmd)).await?;
    let cmd_response = response.into_inner();
    
    if cmd_response.success {
        println!("Status updated: {}", cmd_response.message);
    }
    
    // === QUERY: List customer orders ===
    println!("\n=== Listing Customer Orders ===");
    
    let list_query = ListOrdersByCustomerQuery {
        query_id: "query-002".to_string(),
        customer_id: "customer-123".to_string(),
        page: 1,
        page_size: 10,
    };
    
    let response = query_client.list_orders_by_customer(Request::new(list_query)).await?;
    let query_response = response.into_inner();
    
    if query_response.success {
        if let Some(query_response::Result::OrderList(order_list)) = query_response.result {
            println!("Found {} orders", order_list.total_count);
            
            for order in &order_list.orders {
                println!("  Order: {} - ${}", order.order_id, order.total_amount);
            }
        }
    }
    
    Ok(())
}
```

---

## Summary

### Key Takeaways

1. **Pattern Separation**: CQRS explicitly divides command (write) and query (read) responsibilities into separate models, allowing each to be optimized independently.

2. **Protocol Buffers Benefits**:
   - **Type Safety**: Strongly typed schemas prevent mismatches
   - **Versioning**: Built-in backward/forward compatibility
   - **Performance**: Efficient binary serialization
   - **Cross-Language**: Works seamlessly with C++, Rust, and many other languages
   - **Documentation**: Proto files serve as clear API contracts

3. **Implementation Architecture**:
   - **Command Side**: Handles business logic, validation, and state changes
   - **Query Side**: Provides optimized read models and DTOs
   - **Event Integration**: Commands generate events to update read models
   - **Database Flexibility**: Can use different databases for reads and writes

4. **C/C++ Implementation**:
   - Uses protobuf compiler (`protoc`) to generate classes
   - Handlers implement business logic for commands and queries
   - STL containers for managing collections
   - Integration with existing C++ codebases

5. **Rust Implementation**:
   - Uses `prost` and `tonic` for protobuf and gRPC
   - Type-safe, zero-cost abstractions
   - Async/await for concurrent operations
   - Strong ownership model prevents common errors

6. **Best Practices**:
   - Keep command and query models in separate proto files
   - Use unique identifiers for commands and queries
   - Include timestamps for auditing
   - Design DTOs specifically for read operations
   - Consider eventual consistency between models
   - Use events to synchronize read and write models

7. **Scalability**:
   - Command and query services can scale independently
   - Read models can be replicated for high-availability
   - Different storage engines for different access patterns
   - Caching strategies for frequently accessed data

8. **Trade-offs**:
   - Increased complexity compared to simple CRUD
   - Eventual consistency challenges
   - Requires more infrastructure (event bus, multiple databases)
   - Best suited for complex domains with different read/write patterns

### When to Use This Pattern

- Applications with significantly different read and write loads
- Complex business logic concentrated in write operations
- Need for multiple read models of the same data
- Microservices architectures with separate command/query services
- Systems requiring detailed audit trails
- High-performance requirements for both reads and writes

### When Not to Use

- Simple CRUD applications
- Small projects without scalability requirements
- Teams unfamiliar with distributed systems
- When strong consistency is absolutely required
- Limited resources for maintaining separate models