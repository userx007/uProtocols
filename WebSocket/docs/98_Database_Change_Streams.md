# Database Change Streams via WebSocket

## Overview

Database Change Streams is a pattern for real-time data synchronization where database modifications (inserts, updates, deletes) are automatically pushed to connected clients through WebSocket connections. This eliminates the need for polling and provides instant notifications when data changes, making it ideal for collaborative applications, dashboards, live feeds, and reactive UIs.

## Core Concepts

**Change Data Capture (CDC)**: The mechanism by which databases track and expose data modifications as a stream of change events.

**Change Streams**: A continuous, ordered sequence of change events from a database that applications can subscribe to.

**WebSocket Bridge**: The component that receives change events from the database and broadcasts them to connected WebSocket clients.

**Filtering & Routing**: Logic to determine which clients receive which change events based on subscriptions, permissions, and data relevance.

## Architecture Components

1. **Database with CDC Support**: MongoDB (Change Streams), PostgreSQL (LISTEN/NOTIFY, logical replication), MySQL (binlog), etc.
2. **Change Stream Consumer**: Watches database changes and processes events
3. **WebSocket Server**: Manages client connections and subscriptions
4. **Message Router**: Filters and routes changes to appropriate clients
5. **Client Applications**: Subscribe to specific data changes via WebSocket

## C/C++ Implementation

### Using PostgreSQL LISTEN/NOTIFY with libpq

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <libwebsockets.h>

#define MAX_CLIENTS 100

// WebSocket client structure
struct ws_client {
    struct lws *wsi;
    char subscribed_channel[64];
};

struct ws_client clients[MAX_CLIENTS];
int client_count = 0;

// PostgreSQL connection
PGconn *pg_conn = NULL;

// Initialize PostgreSQL connection and set up LISTEN
PGconn* setup_pg_listener(const char *conninfo, const char *channel) {
    PGconn *conn = PQconnectdb(conninfo);
    
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }
    
    // Set up LISTEN command
    char listen_cmd[128];
    snprintf(listen_cmd, sizeof(listen_cmd), "LISTEN %s", channel);
    
    PGresult *res = PQexec(conn, listen_cmd);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "LISTEN failed: %s", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return NULL;
    }
    
    PQclear(res);
    printf("Listening on channel: %s\n", channel);
    return conn;
}

// Check for PostgreSQL notifications
void check_pg_notifications() {
    if (!pg_conn) return;
    
    PQconsumeInput(pg_conn);
    PGnotify *notify;
    
    while ((notify = PQnotifies(pg_conn)) != NULL) {
        printf("Notification: %s - %s\n", notify->relname, notify->extra);
        
        // Broadcast to all subscribed WebSocket clients
        for (int i = 0; i < client_count; i++) {
            if (strcmp(clients[i].subscribed_channel, notify->relname) == 0) {
                // Send message to client
                unsigned char buf[LWS_PRE + 512];
                int len = snprintf((char*)&buf[LWS_PRE], 512, 
                    "{\"event\":\"change\",\"channel\":\"%s\",\"data\":%s}",
                    notify->relname, notify->extra);
                
                lws_write(clients[i].wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            }
        }
        
        PQfreemem(notify);
    }
}

// WebSocket callback
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Client connected\n");
            if (client_count < MAX_CLIENTS) {
                clients[client_count].wsi = wsi;
                strcpy(clients[client_count].subscribed_channel, "data_changes");
                client_count++;
            }
            break;
            
        case LWS_CALLBACK_RECEIVE:
            printf("Received: %.*s\n", (int)len, (char*)in);
            // Parse subscription message and update client's channel
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Client disconnected\n");
            // Remove client from array
            for (int i = 0; i < client_count; i++) {
                if (clients[i].wsi == wsi) {
                    memmove(&clients[i], &clients[i+1], 
                           (client_count - i - 1) * sizeof(struct ws_client));
                    client_count--;
                    break;
                }
            }
            break;
            
        default:
            break;
    }
    return 0;
}

// Database trigger function (to be executed in PostgreSQL)
/*
CREATE OR REPLACE FUNCTION notify_changes()
RETURNS trigger AS $$
BEGIN
    PERFORM pg_notify('data_changes', 
        json_build_object(
            'operation', TG_OP,
            'table', TG_TABLE_NAME,
            'data', row_to_json(NEW)
        )::text
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER users_change_trigger
AFTER INSERT OR UPDATE OR DELETE ON users
FOR EACH ROW EXECUTE FUNCTION notify_changes();
*/

int main() {
    // Initialize PostgreSQL listener
    pg_conn = setup_pg_listener(
        "host=localhost dbname=mydb user=postgres password=pass",
        "data_changes"
    );
    
    if (!pg_conn) return 1;
    
    // Set up libwebsockets
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    
    static struct lws_protocols protocols[] = {
        { "database-changes", ws_callback, 0, 1024 },
        { NULL, NULL, 0, 0 }
    };
    info.protocols = protocols;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create WebSocket context\n");
        return 1;
    }
    
    printf("WebSocket server running on port 8080\n");
    
    // Main event loop
    while (1) {
        lws_service(context, 50);  // 50ms timeout
        check_pg_notifications();
    }
    
    lws_context_destroy(context);
    PQfinish(pg_conn);
    return 0;
}
```

### C++ with MongoDB Change Streams

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pipeline.hpp>
#include <bsoncxx/json.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::connection_ptr connection_ptr;

class ChangeStreamBridge {
private:
    mongocxx::instance instance_;
    mongocxx::client client_;
    server ws_server_;
    std::vector<connection_ptr> connections_;
    std::mutex connections_mutex_;
    
public:
    ChangeStreamBridge(const std::string& mongo_uri) 
        : client_(mongocxx::uri(mongo_uri)) {
        
        // Configure WebSocket server
        ws_server_.init_asio();
        ws_server_.set_open_handler([this](auto hdl) {
            this->on_open(hdl);
        });
        ws_server_.set_close_handler([this](auto hdl) {
            this->on_close(hdl);
        });
        ws_server_.set_message_handler([this](auto hdl, auto msg) {
            this->on_message(hdl, msg);
        });
    }
    
    void on_open(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.push_back(ws_server_.get_con_from_hdl(hdl));
        std::cout << "Client connected. Total: " << connections_.size() << std::endl;
    }
    
    void on_close(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto conn = ws_server_.get_con_from_hdl(hdl);
        connections_.erase(
            std::remove(connections_.begin(), connections_.end(), conn),
            connections_.end()
        );
        std::cout << "Client disconnected. Total: " << connections_.size() << std::endl;
    }
    
    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
        std::cout << "Received: " << msg->get_payload() << std::endl;
        // Handle subscription requests
    }
    
    void broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            ws_server_.send(conn, message, websocketpp::frame::opcode::text);
        }
    }
    
    void watch_collection(const std::string& db_name, const std::string& coll_name) {
        auto db = client_[db_name];
        auto collection = db[coll_name];
        
        // Create change stream pipeline (optional filtering)
        mongocxx::pipeline pipeline;
        pipeline.match(bsoncxx::builder::stream::document{}
            << "operationType" << bsoncxx::builder::stream::open_document
                << "$in" << bsoncxx::builder::stream::open_array
                    << "insert" << "update" << "delete"
                << bsoncxx::builder::stream::close_array
            << bsoncxx::builder::stream::close_document
            << bsoncxx::builder::stream::finalize);
        
        // Start watching
        auto stream = collection.watch(pipeline);
        
        std::cout << "Watching collection: " << db_name << "." << coll_name << std::endl;
        
        // Process change events
        for (const auto& event : stream) {
            std::string json = bsoncxx::to_json(event);
            
            // Extract relevant information
            auto operation = event["operationType"].get_utf8().value.to_string();
            
            // Build notification message
            std::string notification = "{"
                "\"type\":\"change\","
                "\"collection\":\"" + coll_name + "\","
                "\"operation\":\"" + operation + "\","
                "\"data\":" + json +
            "}";
            
            std::cout << "Change detected: " << operation << std::endl;
            broadcast(notification);
        }
    }
    
    void run(uint16_t port) {
        // Start WebSocket server in separate thread
        std::thread ws_thread([this, port]() {
            ws_server_.listen(port);
            ws_server_.start_accept();
            std::cout << "WebSocket server listening on port " << port << std::endl;
            ws_server_.run();
        });
        
        // Watch MongoDB collection in main thread
        watch_collection("mydb", "users");
        
        ws_thread.join();
    }
};

int main() {
    try {
        ChangeStreamBridge bridge("mongodb://localhost:27017");
        bridge.run(8080);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## Rust Implementation

### Using PostgreSQL with tokio-postgres and tokio-tungstenite

```rust
use tokio_postgres::{Client, NoTls, AsyncMessage};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::TcpListener;
use tokio::sync::broadcast;
use futures_util::{StreamExt, SinkExt};
use serde_json::json;
use std::sync::Arc;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Channel for broadcasting database changes
    let (tx, _rx) = broadcast::channel::<String>(100);
    let tx = Arc::new(tx);
    
    // Spawn PostgreSQL listener task
    let tx_clone = tx.clone();
    tokio::spawn(async move {
        if let Err(e) = pg_listener(tx_clone).await {
            eprintln!("PostgreSQL listener error: {}", e);
        }
    });
    
    // Start WebSocket server
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, addr)) = listener.accept().await {
        let tx = tx.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream, addr, tx).await {
                eprintln!("Connection error: {}", e);
            }
        });
    }
    
    Ok(())
}

async fn pg_listener(tx: Arc<broadcast::Sender<String>>) -> Result<(), Box<dyn std::error::Error>> {
    let (client, mut connection) = tokio_postgres::connect(
        "host=localhost user=postgres dbname=mydb password=pass",
        NoTls
    ).await?;
    
    // Spawn connection task
    tokio::spawn(async move {
        if let Err(e) = connection.await {
            eprintln!("PostgreSQL connection error: {}", e);
        }
    });
    
    // Set up LISTEN
    client.execute("LISTEN data_changes", &[]).await?;
    println!("Listening for PostgreSQL notifications on 'data_changes'");
    
    // Process notifications
    let mut stream = futures_util::stream::poll_fn(move |cx| {
        connection.poll_message(cx)
    });
    
    while let Some(msg) = stream.next().await {
        if let Ok(AsyncMessage::Notification(notification)) = msg {
            let payload = notification.payload();
            println!("Received notification: {}", payload);
            
            let message = json!({
                "event": "change",
                "channel": notification.channel(),
                "data": payload
            }).to_string();
            
            // Broadcast to all WebSocket clients
            let _ = tx.send(message);
        }
    }
    
    Ok(())
}

async fn handle_connection(
    stream: tokio::net::TcpStream,
    addr: std::net::SocketAddr,
    tx: Arc<broadcast::Sender<String>>
) -> Result<(), Box<dyn std::error::Error>> {
    println!("New connection from: {}", addr);
    
    let ws_stream = accept_async(stream).await?;
    let (mut ws_sender, mut ws_receiver) = ws_stream.split();
    
    // Subscribe to broadcast channel
    let mut rx = tx.subscribe();
    
    // Send database changes to WebSocket client
    let send_task = tokio::spawn(async move {
        while let Ok(msg) = rx.recv().await {
            if ws_sender.send(Message::Text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // Receive messages from WebSocket client
    let receive_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_receiver.next().await {
            if let Message::Text(text) = msg {
                println!("Client message: {}", text);
                // Handle subscription requests here
            }
        }
    });
    
    // Wait for either task to complete
    tokio::select! {
        _ = send_task => {},
        _ = receive_task => {},
    }
    
    println!("Connection closed: {}", addr);
    Ok(())
}
```

### Using MongoDB Change Streams with mongodb and tokio-tungstenite

```rust
use mongodb::{Client, bson::doc, options::ClientOptions};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use tokio::net::TcpListener;
use tokio::sync::broadcast;
use futures_util::{StreamExt, SinkExt, TryStreamExt};
use serde_json::json;
use std::sync::Arc;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Broadcast channel for database changes
    let (tx, _rx) = broadcast::channel::<String>(100);
    let tx = Arc::new(tx);
    
    // Spawn MongoDB change stream watcher
    let tx_clone = tx.clone();
    tokio::spawn(async move {
        if let Err(e) = watch_mongodb(tx_clone).await {
            eprintln!("MongoDB watcher error: {}", e);
        }
    });
    
    // Start WebSocket server
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, addr)) = listener.accept().await {
        let tx = tx.clone();
        tokio::spawn(async move {
            handle_ws_connection(stream, addr, tx).await;
        });
    }
    
    Ok(())
}

async fn watch_mongodb(tx: Arc<broadcast::Sender<String>>) -> Result<(), Box<dyn std::error::Error>> {
    // Connect to MongoDB
    let client_options = ClientOptions::parse("mongodb://localhost:27017").await?;
    let client = Client::with_options(client_options)?;
    
    let db = client.database("mydb");
    let collection = db.collection::<mongodb::bson::Document>("users");
    
    // Create change stream pipeline with filters
    let pipeline = vec![
        doc! {
            "$match": {
                "operationType": { "$in": ["insert", "update", "delete"] }
            }
        }
    ];
    
    // Watch for changes
    let mut change_stream = collection.watch(pipeline, None).await?;
    println!("Watching MongoDB collection: mydb.users");
    
    while let Some(change) = change_stream.try_next().await? {
        let operation = change.operation_type.to_string();
        
        // Serialize change event
        let change_json = mongodb::bson::to_json(&change)?;
        
        let notification = json!({
            "type": "change",
            "collection": "users",
            "operation": operation,
            "data": change_json
        }).to_string();
        
        println!("Change detected: {}", operation);
        
        // Broadcast to all connected clients
        let _ = tx.send(notification);
    }
    
    Ok(())
}

async fn handle_ws_connection(
    stream: tokio::net::TcpStream,
    addr: std::net::SocketAddr,
    tx: Arc<broadcast::Sender<String>>
) {
    println!("WebSocket connection from: {}", addr);
    
    match accept_async(stream).await {
        Ok(ws_stream) => {
            let (mut ws_sender, mut ws_receiver) = ws_stream.split();
            let mut rx = tx.subscribe();
            
            // Forward database changes to WebSocket
            let send_handle = tokio::spawn(async move {
                while let Ok(change) = rx.recv().await {
                    if ws_sender.send(Message::Text(change)).await.is_err() {
                        break;
                    }
                }
            });
            
            // Handle incoming WebSocket messages
            let recv_handle = tokio::spawn(async move {
                while let Some(Ok(msg)) = ws_receiver.next().await {
                    match msg {
                        Message::Text(text) => {
                            println!("Client sent: {}", text);
                            // Handle subscription filters
                        }
                        Message::Close(_) => break,
                        _ => {}
                    }
                }
            });
            
            tokio::select! {
                _ = send_handle => {},
                _ = recv_handle => {},
            }
        }
        Err(e) => eprintln!("WebSocket handshake error: {}", e),
    }
    
    println!("Connection closed: {}", addr);
}
```

## Summary

**Database Change Streams via WebSocket** provides a powerful pattern for building real-time, reactive applications. By combining database change data capture mechanisms (PostgreSQL LISTEN/NOTIFY, MongoDB Change Streams, etc.) with WebSocket connections, developers can push data updates instantly to clients without polling overhead.

**Key Benefits:**
- **Real-time synchronization**: Clients receive updates immediately when database changes occur
- **Reduced server load**: Eliminates constant polling and database queries
- **Scalability**: Efficient broadcasting to multiple clients
- **Selective subscriptions**: Clients can filter which changes they receive

**Common Use Cases:**
- Live dashboards and analytics
- Collaborative editing tools
- Chat applications and notifications
- Stock tickers and financial data feeds
- Gaming leaderboards and multiplayer state sync

**Implementation Considerations:**
- **Security**: Authenticate clients and filter changes based on permissions
- **Performance**: Use connection pooling and optimize change stream queries
- **Reliability**: Handle reconnections and implement message queuing for offline clients
- **Scalability**: Consider message brokers (Redis, Kafka) for distributed deployments

The code examples demonstrate production-ready patterns in C/C++ and Rust, showing how to bridge database change events to WebSocket clients efficiently with proper error handling and concurrency management.