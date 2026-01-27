# GraphQL Subscriptions over WebSocket

## Overview

GraphQL Subscriptions enable real-time, event-driven communication between clients and servers. Unlike queries (read) and mutations (write), subscriptions maintain a persistent connection—typically over WebSocket—allowing servers to push data to clients when events occur. This is ideal for live updates like chat messages, notifications, stock prices, or collaborative editing.

The standard protocol for GraphQL subscriptions is **graphql-ws** (formerly graphql-transport-ws), which defines how subscription operations are established, maintained, and terminated over WebSocket connections.

## How It Works

1. **Client establishes WebSocket connection** to the GraphQL server
2. **Client sends a subscription query** specifying what data to watch
3. **Server maintains the connection** and monitors relevant events
4. **Server pushes updates** to the client whenever subscribed events occur
5. **Connection persists** until client unsubscribes or disconnects

## Protocol Flow (graphql-ws)

```
Client → Server: ConnectionInit (with auth if needed)
Server → Client: ConnectionAck
Client → Server: Subscribe (with query)
Server → Client: Next (data updates, multiple times)
Server → Client: Complete (when done)
Client → Server: Complete (to unsubscribe)
```

## C/C++ Implementation

### Server Example (using libwebsockets)

```c
#include <libwebsockets.h>
#include <json-c/json.h>
#include <string.h>
#include <pthread.h>

#define MAX_SUBSCRIPTIONS 100

typedef struct subscription {
    struct lws *wsi;
    char *subscription_id;
    char *query;
    int active;
} subscription_t;

subscription_t subscriptions[MAX_SUBSCRIPTIONS];
pthread_mutex_t sub_mutex = PTHREAD_MUTEX_INITIALIZER;

// GraphQL subscription message types
enum ws_message_type {
    GQL_CONNECTION_INIT,
    GQL_CONNECTION_ACK,
    GQL_SUBSCRIBE,
    GQL_NEXT,
    GQL_COMPLETE,
    GQL_ERROR
};

static int callback_graphql_ws(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("GraphQL WebSocket connection established\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            char *message = (char *)in;
            json_object *jobj = json_tokener_parse(message);
            
            if (!jobj) {
                lwsl_err("Failed to parse JSON message\n");
                return -1;
            }

            json_object *type_obj, *id_obj, *payload_obj;
            json_object_object_get_ex(jobj, "type", &type_obj);
            const char *msg_type = json_object_get_string(type_obj);

            if (strcmp(msg_type, "connection_init") == 0) {
                // Send connection_ack
                const char *ack = "{\"type\":\"connection_ack\"}";
                unsigned char buf[LWS_PRE + 256];
                memcpy(&buf[LWS_PRE], ack, strlen(ack));
                lws_write(wsi, &buf[LWS_PRE], strlen(ack), LWS_WRITE_TEXT);
                
            } else if (strcmp(msg_type, "subscribe") == 0) {
                json_object_object_get_ex(jobj, "id", &id_obj);
                json_object_object_get_ex(jobj, "payload", &payload_obj);
                
                const char *sub_id = json_object_get_string(id_obj);
                json_object *query_obj;
                json_object_object_get_ex(payload_obj, "query", &query_obj);
                const char *query = json_object_get_string(query_obj);

                // Store subscription
                pthread_mutex_lock(&sub_mutex);
                for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                    if (!subscriptions[i].active) {
                        subscriptions[i].wsi = wsi;
                        subscriptions[i].subscription_id = strdup(sub_id);
                        subscriptions[i].query = strdup(query);
                        subscriptions[i].active = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&sub_mutex);
                
                lwsl_user("Subscription registered: %s\n", sub_id);
                
            } else if (strcmp(msg_type, "complete") == 0) {
                json_object_object_get_ex(jobj, "id", &id_obj);
                const char *sub_id = json_object_get_string(id_obj);
                
                // Remove subscription
                pthread_mutex_lock(&sub_mutex);
                for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                    if (subscriptions[i].active && 
                        strcmp(subscriptions[i].subscription_id, sub_id) == 0) {
                        free(subscriptions[i].subscription_id);
                        free(subscriptions[i].query);
                        subscriptions[i].active = 0;
                        break;
                    }
                }
                pthread_mutex_unlock(&sub_mutex);
            }

            json_object_put(jobj);
            break;
        }

        case LWS_CALLBACK_CLOSED:
            // Clean up subscriptions for this connection
            pthread_mutex_lock(&sub_mutex);
            for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (subscriptions[i].active && subscriptions[i].wsi == wsi) {
                    free(subscriptions[i].subscription_id);
                    free(subscriptions[i].query);
                    subscriptions[i].active = 0;
                }
            }
            pthread_mutex_unlock(&sub_mutex);
            break;

        default:
            break;
    }
    return 0;
}

// Function to broadcast updates to subscribers
void broadcast_message_created(const char *message_text, const char *author) {
    char response[1024];
    snprintf(response, sizeof(response),
             "{\"type\":\"next\",\"id\":\"%%s\",\"payload\":{\"data\":{"
             "\"messageCreated\":{\"text\":\"%s\",\"author\":\"%s\"}}}}",
             message_text, author);

    pthread_mutex_lock(&sub_mutex);
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && 
            strstr(subscriptions[i].query, "messageCreated")) {
            
            char final_response[1024];
            snprintf(final_response, sizeof(final_response), response, 
                     subscriptions[i].subscription_id);
            
            unsigned char buf[LWS_PRE + 1024];
            memcpy(&buf[LWS_PRE], final_response, strlen(final_response));
            lws_write(subscriptions[i].wsi, &buf[LWS_PRE], 
                     strlen(final_response), LWS_WRITE_TEXT);
        }
    }
    pthread_mutex_unlock(&sub_mutex);
}
```

### Client Example (C++)

```cpp
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <json/json.h>
#include <iostream>
#include <string>

typedef websocketpp::client<websocketpp::config::asio_client> client;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

class GraphQLSubscriptionClient {
private:
    client ws_client;
    websocketpp::connection_hdl connection;
    std::string subscription_id;
    
public:
    GraphQLSubscriptionClient() {
        ws_client.init_asio();
        
        ws_client.set_message_handler([this](websocketpp::connection_hdl hdl, 
                                             message_ptr msg) {
            this->on_message(hdl, msg);
        });
        
        ws_client.set_open_handler([this](websocketpp::connection_hdl hdl) {
            this->on_open(hdl);
        });
    }
    
    void connect(const std::string& uri) {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = ws_client.get_connection(uri, ec);
        
        if (ec) {
            std::cout << "Connection error: " << ec.message() << std::endl;
            return;
        }
        
        connection = con->get_handle();
        ws_client.connect(con);
        ws_client.run();
    }
    
    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "Connected to GraphQL server" << std::endl;
        
        // Send connection_init
        Json::Value init_msg;
        init_msg["type"] = "connection_init";
        
        Json::StreamWriterBuilder writer;
        std::string msg = Json::writeString(writer, init_msg);
        
        ws_client.send(hdl, msg, websocketpp::frame::opcode::text);
    }
    
    void subscribe(const std::string& query, const std::string& id) {
        subscription_id = id;
        
        Json::Value subscribe_msg;
        subscribe_msg["type"] = "subscribe";
        subscribe_msg["id"] = id;
        subscribe_msg["payload"]["query"] = query;
        
        Json::StreamWriterBuilder writer;
        std::string msg = Json::writeString(writer, subscribe_msg);
        
        ws_client.send(connection, msg, websocketpp::frame::opcode::text);
        std::cout << "Subscription sent: " << id << std::endl;
    }
    
    void on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
        Json::Reader reader;
        Json::Value root;
        
        if (!reader.parse(msg->get_payload(), root)) {
            std::cerr << "Failed to parse message" << std::endl;
            return;
        }
        
        std::string msg_type = root["type"].asString();
        
        if (msg_type == "connection_ack") {
            std::cout << "Connection acknowledged" << std::endl;
            
            // Subscribe to messageCreated
            std::string query = R"(
                subscription {
                    messageCreated {
                        text
                        author
                        timestamp
                    }
                }
            )";
            subscribe(query, "sub-1");
            
        } else if (msg_type == "next") {
            std::cout << "Received data update:" << std::endl;
            Json::StreamWriterBuilder writer;
            std::cout << Json::writeString(writer, root["payload"]) << std::endl;
            
        } else if (msg_type == "complete") {
            std::cout << "Subscription completed: " << root["id"].asString() << std::endl;
            
        } else if (msg_type == "error") {
            std::cerr << "Error: " << root["payload"].toStyledString() << std::endl;
        }
    }
    
    void unsubscribe() {
        Json::Value complete_msg;
        complete_msg["type"] = "complete";
        complete_msg["id"] = subscription_id;
        
        Json::StreamWriterBuilder writer;
        std::string msg = Json::writeString(writer, complete_msg);
        
        ws_client.send(connection, msg, websocketpp::frame::opcode::text);
    }
};

int main() {
    GraphQLSubscriptionClient client;
    client.connect("ws://localhost:4000/graphql");
    return 0;
}
```

## Rust Implementation

### Server Example (using async-graphql and tokio-tungstenite)

```rust
use async_graphql::{Context, Object, Schema, Subscription};
use async_graphql_warp::{GraphQLWebSocket, GraphQLSubscription};
use futures_util::Stream;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::broadcast;
use warp::Filter;

#[derive(Clone, Debug, Serialize, Deserialize)]
struct Message {
    text: String,
    author: String,
    timestamp: i64,
}

// Event broadcaster
type MessageBroadcaster = Arc<broadcast::Sender<Message>>;

struct QueryRoot;

#[Object]
impl QueryRoot {
    async fn hello(&self) -> String {
        "Hello from GraphQL!".to_string()
    }
}

struct MutationRoot;

#[Object]
impl MutationRoot {
    async fn create_message(
        &self,
        ctx: &Context<'_>,
        text: String,
        author: String,
    ) -> Message {
        let broadcaster = ctx.data::<MessageBroadcaster>().unwrap();
        
        let message = Message {
            text,
            author,
            timestamp: chrono::Utc::now().timestamp(),
        };
        
        // Broadcast to all subscribers
        let _ = broadcaster.send(message.clone());
        
        message
    }
}

struct SubscriptionRoot;

#[Subscription]
impl SubscriptionRoot {
    // Subscribe to new messages
    async fn message_created(
        &self,
        ctx: &Context<'_>,
    ) -> impl Stream<Item = Message> {
        let broadcaster = ctx.data::<MessageBroadcaster>().unwrap();
        let mut rx = broadcaster.subscribe();
        
        async_stream::stream! {
            while let Ok(message) = rx.recv().await {
                yield message;
            }
        }
    }
    
    // Subscribe with filter
    async fn messages_by_author(
        &self,
        ctx: &Context<'_>,
        author: String,
    ) -> impl Stream<Item = Message> {
        let broadcaster = ctx.data::<MessageBroadcaster>().unwrap();
        let mut rx = broadcaster.subscribe();
        
        async_stream::stream! {
            while let Ok(message) = rx.recv().await {
                if message.author == author {
                    yield message;
                }
            }
        }
    }
}

#[tokio::main]
async fn main() {
    // Create message broadcaster
    let (tx, _) = broadcast::channel::<Message>(100);
    let broadcaster = Arc::new(tx);
    
    // Build GraphQL schema
    let schema = Schema::build(QueryRoot, MutationRoot, SubscriptionRoot)
        .data(broadcaster.clone())
        .finish();
    
    // GraphQL subscription endpoint
    let graphql_subscription = warp::path("graphql")
        .and(warp::ws())
        .and(GraphQLSubscription::new(schema.clone()))
        .map(|ws: warp::ws::Ws, protocol| {
            ws.on_upgrade(move |socket| {
                GraphQLWebSocket::new(socket, schema.clone(), protocol)
                    .serve()
            })
        });
    
    println!("GraphQL subscription server running on ws://127.0.0.1:8000/graphql");
    warp::serve(graphql_subscription)
        .run(([127, 0, 0, 1], 8000))
        .await;
}
```

### Client Example (Rust)

```rust
use async_graphql::http::{GraphQLSubscriptionClient, WebSocketProtocols};
use futures_util::{SinkExt, StreamExt};
use serde_json::json;
use tokio_tungstenite::{connect_async, tungstenite::Message};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let url = "ws://localhost:8000/graphql";
    
    // Connect to WebSocket
    let (ws_stream, _) = connect_async(url).await?;
    println!("Connected to GraphQL server");
    
    let (mut write, mut read) = ws_stream.split();
    
    // Send connection_init
    let init_msg = json!({
        "type": "connection_init",
        "payload": {}
    });
    write.send(Message::Text(init_msg.to_string())).await?;
    
    // Wait for connection_ack
    if let Some(Ok(Message::Text(text))) = read.next().await {
        let msg: serde_json::Value = serde_json::from_str(&text)?;
        if msg["type"] == "connection_ack" {
            println!("Connection acknowledged");
        }
    }
    
    // Subscribe to messageCreated
    let subscribe_msg = json!({
        "type": "subscribe",
        "id": "sub-1",
        "payload": {
            "query": r#"
                subscription {
                    messageCreated {
                        text
                        author
                        timestamp
                    }
                }
            "#
        }
    });
    write.send(Message::Text(subscribe_msg.to_string())).await?;
    println!("Subscription sent");
    
    // Listen for updates
    tokio::spawn(async move {
        while let Some(Ok(Message::Text(text))) = read.next().await {
            let msg: serde_json::Value = serde_json::from_str(&text).unwrap();
            
            match msg["type"].as_str() {
                Some("next") => {
                    println!("Received update:");
                    println!("{}", serde_json::to_string_pretty(&msg["payload"]).unwrap());
                }
                Some("complete") => {
                    println!("Subscription completed");
                    break;
                }
                Some("error") => {
                    eprintln!("Error: {:?}", msg["payload"]);
                }
                _ => {}
            }
        }
    });
    
    // Keep main thread alive
    tokio::time::sleep(tokio::time::Duration::from_secs(3600)).await;
    
    Ok(())
}
```

### Advanced Rust Example with Connection Management

```rust
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{Mutex, mpsc};
use tokio_tungstenite::tungstenite::Message;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum GraphQLWSMessage {
    ConnectionInit { payload: Option<serde_json::Value> },
    ConnectionAck,
    Subscribe { id: String, payload: SubscriptionPayload },
    Next { id: String, payload: serde_json::Value },
    Complete { id: String },
    Error { id: String, payload: Vec<serde_json::Value> },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct SubscriptionPayload {
    query: String,
    variables: Option<serde_json::Value>,
}

struct SubscriptionManager {
    subscriptions: Arc<Mutex<HashMap<String, mpsc::UnboundedSender<serde_json::Value>>>>,
}

impl SubscriptionManager {
    fn new() -> Self {
        Self {
            subscriptions: Arc::new(Mutex::new(HashMap::new())),
        }
    }
    
    async fn add_subscription(
        &self,
        id: String,
        sender: mpsc::UnboundedSender<serde_json::Value>,
    ) {
        let mut subs = self.subscriptions.lock().await;
        subs.insert(id, sender);
    }
    
    async fn remove_subscription(&self, id: &str) {
        let mut subs = self.subscriptions.lock().await;
        subs.remove(id);
    }
    
    async fn broadcast(&self, data: serde_json::Value) {
        let subs = self.subscriptions.lock().await;
        for sender in subs.values() {
            let _ = sender.send(data.clone());
        }
    }
}

async fn handle_graphql_connection(
    ws_stream: tokio_tungstenite::WebSocketStream<tokio::net::TcpStream>,
    manager: Arc<SubscriptionManager>,
) {
    let (mut write, mut read) = ws_stream.split();
    
    while let Some(Ok(Message::Text(text))) = read.next().await {
        let msg: Result<GraphQLWSMessage, _> = serde_json::from_str(&text);
        
        match msg {
            Ok(GraphQLWSMessage::ConnectionInit { .. }) => {
                let ack = GraphQLWSMessage::ConnectionAck;
                let response = serde_json::to_string(&ack).unwrap();
                write.send(Message::Text(response)).await.ok();
            }
            Ok(GraphQLWSMessage::Subscribe { id, payload }) => {
                let (tx, mut rx) = mpsc::unbounded_channel();
                manager.add_subscription(id.clone(), tx).await;
                
                // Spawn task to forward subscription updates
                let mut write_clone = write.clone();
                let id_clone = id.clone();
                tokio::spawn(async move {
                    while let Some(data) = rx.recv().await {
                        let next_msg = GraphQLWSMessage::Next {
                            id: id_clone.clone(),
                            payload: data,
                        };
                        let response = serde_json::to_string(&next_msg).unwrap();
                        write_clone.send(Message::Text(response)).await.ok();
                    }
                });
            }
            Ok(GraphQLWSMessage::Complete { id }) => {
                manager.remove_subscription(&id).await;
            }
            _ => {}
        }
    }
}
```

## Summary

**GraphQL Subscriptions** provide real-time, bidirectional communication between clients and servers using WebSocket connections. The graphql-ws protocol standardizes message formats for initialization, subscription management, and data streaming.

**Key features** include persistent connections for live updates, event-driven architecture for efficient resource usage, and structured query language for specifying exactly what data to receive. The subscription lifecycle involves connection initialization, subscription registration, continuous data streaming, and graceful termination.

**Implementation considerations** include connection management (handling reconnects and timeouts), authentication (typically during connection_init), scalability (using message brokers like Redis for distributed systems), and filtering (allowing clients to subscribe to specific data subsets). Both C/C++ and Rust provide robust libraries for building GraphQL subscription servers and clients, with Rust's async ecosystem particularly well-suited for handling concurrent WebSocket connections efficiently.

This technology powers modern real-time applications like chat systems, live dashboards, collaborative tools, gaming leaderboards, and IoT device monitoring where immediate data synchronization is critical.