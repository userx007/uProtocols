# Health Checks and Readiness for WebSocket Services

## Overview

Health checks and readiness probes are critical monitoring mechanisms that determine whether a WebSocket service is functioning correctly and ready to accept connections. These endpoints allow load balancers, orchestration platforms (like Kubernetes), and monitoring systems to automatically detect issues and route traffic appropriately.

## Core Concepts

**Health Checks** verify that a service is alive and operating. They typically test:
- Process responsiveness
- Critical dependency availability (databases, message queues)
- Resource utilization (memory, CPU)

**Readiness Probes** determine if a service can handle traffic. A service might be healthy but not ready (e.g., still warming up caches or establishing upstream connections).

**Liveness Probes** detect when a service has entered an unrecoverable state and needs restarting.

## Implementation Strategy

For WebSocket services, health endpoints are typically HTTP-based (often REST) because:
1. They're easier for monitoring tools to consume
2. They don't require WebSocket handshake overhead
3. They can be checked without maintaining persistent connections

Common patterns:
- `GET /health` - Basic liveness check
- `GET /ready` - Readiness check
- `GET /health/live` and `GET /health/ready` - Kubernetes-style endpoints

## C/C++ Implementation

Using **libwebsockets** with an HTTP health endpoint:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    bool is_ready;
    time_t last_check;
    int active_connections;
    bool database_connected;
} health_state_t;

static health_state_t g_health = {
    .is_ready = false,
    .last_check = 0,
    .active_connections = 0,
    .database_connected = false
};

// HTTP callback for health endpoints
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_HTTP: {
            char *requested_uri = (char *)in;
            unsigned char buffer[LWS_PRE + 512];
            unsigned char *p = &buffer[LWS_PRE];
            unsigned char *end = &buffer[sizeof(buffer) - 1];
            
            if (strcmp(requested_uri, "/health") == 0) {
                // Liveness check - always returns 200 if process responds
                p += snprintf((char *)p, end - p,
                    "{\"status\":\"ok\",\"timestamp\":%ld}", time(NULL));
                
                if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                    "application/json", lws_ptr_diff(p, &buffer[LWS_PRE]),
                    &p, end))
                    return 1;
                    
                if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE,
                    &p, end))
                    return 1;
                    
                lws_write(wsi, &buffer[LWS_PRE], 
                         lws_ptr_diff(p, &buffer[LWS_PRE]), LWS_WRITE_HTTP);
                goto try_to_reuse;
            }
            
            if (strcmp(requested_uri, "/ready") == 0) {
                // Readiness check - checks dependencies
                int status = g_health.is_ready && 
                            g_health.database_connected ? 
                            HTTP_STATUS_OK : HTTP_STATUS_SERVICE_UNAVAILABLE;
                
                p += snprintf((char *)p, end - p,
                    "{\"ready\":%s,\"connections\":%d,\"database\":%s}",
                    g_health.is_ready ? "true" : "false",
                    g_health.active_connections,
                    g_health.database_connected ? "true" : "false");
                
                if (lws_add_http_common_headers(wsi, status,
                    "application/json", lws_ptr_diff(p, &buffer[LWS_PRE]),
                    &p, end))
                    return 1;
                    
                if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE,
                    &p, end))
                    return 1;
                    
                lws_write(wsi, &buffer[LWS_PRE], 
                         lws_ptr_diff(p, &buffer[LWS_PRE]), LWS_WRITE_HTTP);
                goto try_to_reuse;
            }
            
            return lws_http_serve_default_file(wsi, requested_uri);
            
try_to_reuse:
            if (lws_http_transaction_completed(wsi))
                return -1;
            return 0;
        }
        
        default:
            break;
    }
    return 0;
}

// WebSocket callback that updates connection count
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            g_health.active_connections++;
            g_health.is_ready = true;
            break;
            
        case LWS_CALLBACK_CLOSED:
            g_health.active_connections--;
            break;
            
        default:
            break;
    }
    return 0;
}

// Background health checker
void* health_check_thread(void *arg) {
    while (1) {
        // Simulate database connectivity check
        // In production, actually ping your database
        g_health.database_connected = check_database_connection();
        g_health.last_check = time(NULL);
        
        sleep(5); // Check every 5 seconds
    }
    return NULL;
}

int main() {
    struct lws_context_creation_info info;
    struct lws_protocols protocols[] = {
        { "http", callback_http, 0, 0 },
        { "websocket-protocol", callback_websocket, 0, 1024 },
        { NULL, NULL, 0, 0 }
    };
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    // Start health check thread
    pthread_t health_thread;
    pthread_create(&health_thread, NULL, health_check_thread, NULL);
    
    g_health.is_ready = true;
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}
```

## Rust Implementation

Using **tokio-tungstenite** and **axum** for HTTP endpoints:

```rust
use axum::{
    extract::State,
    http::StatusCode,
    response::Json,
    routing::get,
    Router,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::RwLock;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Clone)]
struct HealthState {
    is_ready: Arc<RwLock<bool>>,
    active_connections: Arc<RwLock<usize>>,
    database_connected: Arc<RwLock<bool>>,
}

impl HealthState {
    fn new() -> Self {
        Self {
            is_ready: Arc::new(RwLock::new(false)),
            active_connections: Arc::new(RwLock::new(0)),
            database_connected: Arc::new(RwLock::new(false)),
        }
    }
    
    async fn increment_connections(&self) {
        let mut conn = self.active_connections.write().await;
        *conn += 1;
    }
    
    async fn decrement_connections(&self) {
        let mut conn = self.active_connections.write().await;
        *conn = conn.saturating_sub(1);
    }
}

#[derive(Serialize)]
struct LivenessResponse {
    status: String,
    timestamp: u64,
}

#[derive(Serialize)]
struct ReadinessResponse {
    ready: bool,
    connections: usize,
    database: bool,
}

// Liveness endpoint - always returns 200 if process responds
async fn health_handler() -> Json<LivenessResponse> {
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    
    Json(LivenessResponse {
        status: "ok".to_string(),
        timestamp,
    })
}

// Readiness endpoint - checks dependencies
async fn ready_handler(
    State(health): State<HealthState>,
) -> Result<Json<ReadinessResponse>, StatusCode> {
    let is_ready = *health.is_ready.read().await;
    let connections = *health.active_connections.read().await;
    let database = *health.database_connected.read().await;
    
    let response = ReadinessResponse {
        ready: is_ready && database,
        connections,
        database,
    };
    
    if response.ready {
        Ok(Json(response))
    } else {
        Err(StatusCode::SERVICE_UNAVAILABLE)
    }
}

// Startup probe - checks if initialization is complete
async fn startup_handler(
    State(health): State<HealthState>,
) -> Result<Json<LivenessResponse>, StatusCode> {
    let is_ready = *health.is_ready.read().await;
    
    if is_ready {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        Ok(Json(LivenessResponse {
            status: "started".to_string(),
            timestamp,
        }))
    } else {
        Err(StatusCode::SERVICE_UNAVAILABLE)
    }
}

// Background task to check dependencies
async fn health_check_task(health: HealthState) {
    let mut interval = tokio::time::interval(tokio::time::Duration::from_secs(5));
    
    loop {
        interval.tick().await;
        
        // Simulate database connectivity check
        let db_ok = check_database_connection().await;
        *health.database_connected.write().await = db_ok;
        
        // Could add more checks here: cache availability, message queue, etc.
    }
}

async fn check_database_connection() -> bool {
    // In production, actually ping your database
    // For example with sqlx:
    // sqlx::query("SELECT 1").fetch_one(&pool).await.is_ok()
    true
}

use tokio_tungstenite::accept_async;
use tokio::net::TcpListener;
use futures_util::{StreamExt, SinkExt};

async fn handle_websocket_connection(
    stream: tokio::net::TcpStream,
    health: HealthState,
) {
    health.increment_connections().await;
    
    let ws_stream = match accept_async(stream).await {
        Ok(ws) => ws,
        Err(e) => {
            eprintln!("WebSocket handshake failed: {}", e);
            health.decrement_connections().await;
            return;
        }
    };
    
    let (mut write, mut read) = ws_stream.split();
    
    while let Some(msg) = read.next().await {
        match msg {
            Ok(msg) => {
                if msg.is_close() {
                    break;
                }
                // Handle message
                if write.send(msg).await.is_err() {
                    break;
                }
            }
            Err(_) => break,
        }
    }
    
    health.decrement_connections().await;
}

#[tokio::main]
async fn main() {
    let health = HealthState::new();
    
    // Mark as ready after initialization
    *health.is_ready.write().await = true;
    *health.database_connected.write().await = true;
    
    // Start health check background task
    let health_clone = health.clone();
    tokio::spawn(async move {
        health_check_task(health_clone).await;
    });
    
    // Build HTTP health check server
    let app = Router::new()
        .route("/health", get(health_handler))
        .route("/ready", get(ready_handler))
        .route("/startup", get(startup_handler))
        .with_state(health.clone());
    
    // Start HTTP server for health checks
    let health_clone = health.clone();
    tokio::spawn(async move {
        let listener = tokio::net::TcpListener::bind("0.0.0.0:8081")
            .await
            .unwrap();
        axum::serve(listener, app).await.unwrap();
    });
    
    // Start WebSocket server
    let ws_listener = TcpListener::bind("0.0.0.0:8080").await.unwrap();
    println!("WebSocket server listening on 0.0.0.0:8080");
    println!("Health endpoints on 0.0.0.0:8081/health and /ready");
    
    while let Ok((stream, _)) = ws_listener.accept().await {
        let health_clone = health.clone();
        tokio::spawn(async move {
            handle_websocket_connection(stream, health_clone).await;
        });
    }
}
```

## Kubernetes Configuration Example

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: websocket-service
spec:
  replicas: 3
  template:
    spec:
      containers:
      - name: websocket
        image: my-websocket-service:latest
        ports:
        - containerPort: 8080
          name: websocket
        - containerPort: 8081
          name: health
        livenessProbe:
          httpGet:
            path: /health
            port: 8081
          initialDelaySeconds: 10
          periodSeconds: 10
          timeoutSeconds: 3
          failureThreshold: 3
        readinessProbe:
          httpGet:
            path: /ready
            port: 8081
          initialDelaySeconds: 5
          periodSeconds: 5
          timeoutSeconds: 2
          failureThreshold: 2
        startupProbe:
          httpGet:
            path: /startup
            port: 8081
          initialDelaySeconds: 0
          periodSeconds: 2
          timeoutSeconds: 2
          failureThreshold: 30
```

## Summary

Health checks and readiness probes are essential for production WebSocket services. They enable automated monitoring, load balancing, and orchestration by providing HTTP endpoints that report service status. The key distinctions are: liveness checks verify basic responsiveness, readiness checks confirm the service can handle traffic (checking dependencies like databases), and startup probes allow slow-starting services time to initialize. Implementing these endpoints separately from WebSocket connections simplifies monitoring infrastructure and improves reliability in distributed systems.