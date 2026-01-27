# WebSocket Topic: API Gateway Integration

## Detailed Description

API Gateway Integration refers to the architectural pattern of combining REST APIs and WebSocket connections within a unified gateway infrastructure. This approach creates a single entry point for all client communications, whether they're traditional request-response HTTP/REST calls or real-time bidirectional WebSocket connections.

### Key Concepts

**Unified Entry Point**: An API gateway acts as a reverse proxy that routes both HTTP and WebSocket traffic to appropriate backend services. This provides:
- Centralized authentication and authorization
- Rate limiting and throttling across all protocols
- Unified logging and monitoring
- Load balancing for both REST and WebSocket connections
- Protocol translation when needed

**Architecture Benefits**:
- **Simplified Client Logic**: Clients connect to one endpoint for all communication types
- **Security**: Single point for SSL/TLS termination and security policy enforcement
- **Scalability**: Independent scaling of gateway and backend services
- **Flexibility**: Easy to add new services or modify routing without client changes

**Common Use Cases**:
- Chat applications with REST for user management and WebSocket for messaging
- IoT platforms with REST for device registration and WebSocket for telemetry
- Trading platforms with REST for account operations and WebSocket for live price feeds
- Collaborative applications with REST for document storage and WebSocket for real-time updates

---

## C/C++ Implementation Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <libwebsockets.h>
#include <pthread.h>

#define REST_PORT 8080
#define WS_PORT 8081

// Shared data structure for both REST and WebSocket
typedef struct {
    char message[256];
    int message_count;
    pthread_mutex_t lock;
} shared_state_t;

shared_state_t *global_state;

// ============================================
// REST API Handler (using libmicrohttpd)
// ============================================

static enum MHD_Result handle_rest_request(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls)
{
    struct MHD_Response *response;
    enum MHD_Result ret;
    char response_body[512];
    
    // Handle GET /api/status
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/status") == 0) {
        pthread_mutex_lock(&global_state->lock);
        snprintf(response_body, sizeof(response_body),
                 "{\"status\":\"ok\",\"message\":\"%s\",\"count\":%d}",
                 global_state->message, global_state->message_count);
        pthread_mutex_unlock(&global_state->lock);
        
        response = MHD_create_response_from_buffer(
            strlen(response_body),
            (void *)response_body,
            MHD_RESPMEM_MUST_COPY);
        
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Handle POST /api/message
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/message") == 0) {
        if (*upload_data_size != 0) {
            pthread_mutex_lock(&global_state->lock);
            strncpy(global_state->message, upload_data, 
                    sizeof(global_state->message) - 1);
            global_state->message_count++;
            pthread_mutex_unlock(&global_state->lock);
            
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        snprintf(response_body, sizeof(response_body),
                 "{\"status\":\"created\",\"message\":\"Message stored\"}");
        
        response = MHD_create_response_from_buffer(
            strlen(response_body),
            (void *)response_body,
            MHD_RESPMEM_MUST_COPY);
        
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_CREATED, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // 404 Not Found
    const char *not_found = "{\"error\":\"Not Found\"}";
    response = MHD_create_response_from_buffer(
        strlen(not_found),
        (void *)not_found,
        MHD_RESPMEM_PERSISTENT);
    
    ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

// ============================================
// WebSocket Handler (using libwebsockets)
// ============================================

struct session_data {
    int message_number;
};

static int callback_websocket(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len)
{
    struct session_data *session = (struct session_data *)user;
    unsigned char buf[LWS_PRE + 512];
    unsigned char *p = &buf[LWS_PRE];
    int n;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket connection established\n");
            session->message_number = 0;
            break;
            
        case LWS_CALLBACK_RECEIVE:
            printf("Received WebSocket data: %.*s\n", (int)len, (char *)in);
            
            // Store message in shared state
            pthread_mutex_lock(&global_state->lock);
            strncpy(global_state->message, (char *)in, 
                    sizeof(global_state->message) - 1);
            global_state->message_count++;
            
            // Echo back with state info
            n = snprintf((char *)p, 512,
                        "{\"echo\":\"%.*s\",\"total_messages\":%d}",
                        (int)len, (char *)in, global_state->message_count);
            pthread_mutex_unlock(&global_state->lock);
            
            lws_write(wsi, p, n, LWS_WRITE_TEXT);
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Periodic broadcast of current state
            pthread_mutex_lock(&global_state->lock);
            n = snprintf((char *)p, 512,
                        "{\"type\":\"broadcast\",\"message\":\"%s\",\"count\":%d}",
                        global_state->message, global_state->message_count);
            pthread_mutex_unlock(&global_state->lock);
            
            lws_write(wsi, p, n, LWS_WRITE_TEXT);
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket connection closed\n");
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "gateway-protocol",
        callback_websocket,
        sizeof(struct session_data),
        1024,
    },
    { NULL, NULL, 0, 0 } // terminator
};

// ============================================
// Gateway Main
// ============================================

void *websocket_thread(void *arg) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = WS_PORT;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create WebSocket context\n");
        return NULL;
    }
    
    printf("WebSocket server listening on port %d\n", WS_PORT);
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return NULL;
}

int main() {
    struct MHD_Daemon *rest_daemon;
    pthread_t ws_thread;
    
    // Initialize shared state
    global_state = malloc(sizeof(shared_state_t));
    strcpy(global_state->message, "No messages yet");
    global_state->message_count = 0;
    pthread_mutex_init(&global_state->lock, NULL);
    
    // Start REST API server
    rest_daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        REST_PORT,
        NULL, NULL,
        &handle_rest_request, NULL,
        MHD_OPTION_END);
    
    if (!rest_daemon) {
        fprintf(stderr, "Failed to start REST server\n");
        return 1;
    }
    
    printf("REST API server listening on port %d\n", REST_PORT);
    
    // Start WebSocket server in separate thread
    pthread_create(&ws_thread, NULL, websocket_thread, NULL);
    
    printf("API Gateway running. Press Enter to stop...\n");
    getchar();
    
    // Cleanup
    MHD_stop_daemon(rest_daemon);
    pthread_cancel(ws_thread);
    pthread_join(ws_thread, NULL);
    pthread_mutex_destroy(&global_state->lock);
    free(global_state);
    
    return 0;
}
```

**Compilation**:
```bash
gcc -o api_gateway api_gateway.c -lmicrohttpd -lwebsockets -lpthread
```

---

## Rust Implementation Example

```rust
use std::sync::Arc;
use tokio::sync::RwLock;
use warp::{Filter, ws::{WebSocket, Message}};
use serde::{Deserialize, Serialize};
use futures_util::{StreamExt, SinkExt};
use std::collections::HashMap;

// ============================================
// Shared State
// ============================================

#[derive(Clone, Debug, Serialize, Deserialize)]
struct AppMessage {
    message: String,
    count: u32,
}

#[derive(Clone)]
struct AppState {
    current_message: Arc<RwLock<AppMessage>>,
    connections: Arc<RwLock<HashMap<usize, tokio::sync::mpsc::UnboundedSender<String>>>>,
    next_id: Arc<RwLock<usize>>,
}

impl AppState {
    fn new() -> Self {
        AppState {
            current_message: Arc::new(RwLock::new(AppMessage {
                message: "No messages yet".to_string(),
                count: 0,
            })),
            connections: Arc::new(RwLock::new(HashMap::new())),
            next_id: Arc::new(RwLock::new(0)),
        }
    }
    
    async fn update_message(&self, new_message: String) {
        let mut msg = self.current_message.write().await;
        msg.message = new_message.clone();
        msg.count += 1;
        
        // Broadcast to all WebSocket connections
        let broadcast = serde_json::json!({
            "type": "broadcast",
            "message": new_message,
            "count": msg.count
        }).to_string();
        
        let connections = self.connections.read().await;
        for (_, tx) in connections.iter() {
            let _ = tx.send(broadcast.clone());
        }
    }
    
    async fn get_status(&self) -> AppMessage {
        self.current_message.read().await.clone()
    }
}

// ============================================
// REST API Handlers
// ============================================

#[derive(Deserialize)]
struct PostMessage {
    message: String,
}

#[derive(Serialize)]
struct ApiResponse {
    status: String,
    message: Option<String>,
    count: Option<u32>,
}

async fn handle_get_status(state: AppState) -> Result<impl warp::Reply, warp::Rejection> {
    let status = state.get_status().await;
    Ok(warp::reply::json(&ApiResponse {
        status: "ok".to_string(),
        message: Some(status.message),
        count: Some(status.count),
    }))
}

async fn handle_post_message(
    body: PostMessage,
    state: AppState,
) -> Result<impl warp::Reply, warp::Rejection> {
    state.update_message(body.message).await;
    
    Ok(warp::reply::with_status(
        warp::reply::json(&ApiResponse {
            status: "created".to_string(),
            message: Some("Message stored and broadcast".to_string()),
            count: None,
        }),
        warp::http::StatusCode::CREATED,
    ))
}

// ============================================
// WebSocket Handler
// ============================================

async fn handle_websocket(ws: WebSocket, state: AppState) {
    let (mut ws_tx, mut ws_rx) = ws.split();
    
    // Register this connection
    let id = {
        let mut next_id = state.next_id.write().await;
        let id = *next_id;
        *next_id += 1;
        id
    };
    
    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel();
    state.connections.write().await.insert(id, tx);
    
    println!("WebSocket connection {} established", id);
    
    // Send current state on connect
    let current = state.get_status().await;
    let welcome = serde_json::json!({
        "type": "welcome",
        "message": current.message,
        "count": current.count
    }).to_string();
    
    let _ = ws_tx.send(Message::text(welcome)).await;
    
    // Spawn task to handle outgoing messages
    let mut send_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_tx.send(Message::text(msg)).await.is_err() {
                break;
            }
        }
    });
    
    // Handle incoming messages
    let state_clone = state.clone();
    let mut recv_task = tokio::spawn(async move {
        while let Some(result) = ws_rx.next().await {
            match result {
                Ok(msg) => {
                    if let Ok(text) = msg.to_str() {
                        println!("Received from {}: {}", id, text);
                        
                        // Parse and handle message
                        if let Ok(parsed) = serde_json::from_str::<serde_json::Value>(text) {
                            if let Some(message) = parsed.get("message").and_then(|m| m.as_str()) {
                                state_clone.update_message(message.to_string()).await;
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("WebSocket error for connection {}: {}", id, e);
                    break;
                }
            }
        }
    });
    
    // Wait for either task to finish
    tokio::select! {
        _ = &mut send_task => recv_task.abort(),
        _ = &mut recv_task => send_task.abort(),
    }
    
    // Cleanup
    state.connections.write().await.remove(&id);
    println!("WebSocket connection {} closed", id);
}

// ============================================
// API Gateway Main
// ============================================

#[tokio::main]
async fn main() {
    let state = AppState::new();
    
    // REST API routes
    let state_filter = warp::any().map(move || state.clone());
    
    let get_status = warp::path!("api" / "status")
        .and(warp::get())
        .and(state_filter.clone())
        .and_then(handle_get_status);
    
    let post_message = warp::path!("api" / "message")
        .and(warp::post())
        .and(warp::body::json())
        .and(state_filter.clone())
        .and_then(handle_post_message);
    
    // WebSocket route
    let websocket = warp::path("ws")
        .and(warp::ws())
        .and(state_filter.clone())
        .map(|ws: warp::ws::Ws, state| {
            ws.on_upgrade(move |socket| handle_websocket(socket, state))
        });
    
    // CORS configuration
    let cors = warp::cors()
        .allow_any_origin()
        .allow_methods(vec!["GET", "POST", "OPTIONS"])
        .allow_headers(vec!["Content-Type"]);
    
    // Combine routes
    let routes = get_status
        .or(post_message)
        .or(websocket)
        .with(cors)
        .with(warp::log("api_gateway"));
    
    println!("API Gateway listening on http://localhost:8080");
    println!("REST API: http://localhost:8080/api/*");
    println!("WebSocket: ws://localhost:8080/ws");
    
    warp::serve(routes).run(([0, 0, 0, 0], 8080)).await;
}
```

**Cargo.toml**:
```toml
[package]
name = "api-gateway"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1.35", features = ["full"] }
warp = "0.3"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
futures-util = "0.3"
```

**Running**:
```bash
cargo run
```

---

## Client Examples

### JavaScript Client

```javascript
// REST API calls
async function getStatus() {
    const response = await fetch('http://localhost:8080/api/status');
    const data = await response.json();
    console.log('Status:', data);
}

async function sendMessage(message) {
    const response = await fetch('http://localhost:8080/api/message', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ message })
    });
    const data = await response.json();
    console.log('Response:', data);
}

// WebSocket connection
const ws = new WebSocket('ws://localhost:8080/ws');

ws.onopen = () => {
    console.log('WebSocket connected');
    ws.send(JSON.stringify({ message: 'Hello from WebSocket!' }));
};

ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    console.log('WebSocket message:', data);
};

// Use both protocols
getStatus();
sendMessage('Hello from REST!');
```

---

## Summary

**API Gateway Integration** unifies REST and WebSocket protocols behind a single entry point, providing:

**Key Benefits**:
- **Single endpoint** for all client communications
- **Centralized** authentication, rate limiting, and logging
- **Protocol flexibility** - clients can use REST, WebSocket, or both
- **Shared state** between REST and WebSocket connections
- **Simplified deployment** and load balancing

**Implementation Approach**:
1. **Shared State Management**: Use thread-safe structures (mutexes in C, Arc/RwLock in Rust) to share data between REST and WebSocket handlers
2. **Concurrent Handling**: Run REST and WebSocket servers concurrently (threads in C, async tasks in Rust)
3. **Broadcasting**: WebSocket connections can broadcast state changes triggered by REST API calls
4. **Unified Authentication**: Apply same auth logic to both protocols at gateway level

**Common Patterns**:
- REST for CRUD operations, WebSocket for real-time notifications
- REST for initialization/setup, WebSocket for ongoing data streams
- Bidirectional sync where REST changes trigger WebSocket broadcasts

This architecture is ideal for modern applications requiring both traditional API access and real-time bidirectional communication, providing clients flexibility while maintaining server-side consistency and control.