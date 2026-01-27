# Connection Draining in WebSocket Programming

## Overview

**Connection Draining** is a graceful shutdown pattern that allows a WebSocket server to stop accepting new connections while allowing existing connections to complete their work before terminating. This is crucial for zero-downtime deployments, server maintenance, load balancer updates, and connection migration scenarios.

The core concept involves:
1. Stopping acceptance of new connections
2. Notifying existing clients of the impending shutdown
3. Allowing in-flight operations to complete
4. Closing connections gracefully after a timeout or completion
5. Optionally redirecting clients to alternative servers

## Key Concepts

### Shutdown Phases

1. **Pre-drain**: Server operates normally
2. **Drain initiated**: Stop accepting new connections
3. **Active drain**: Existing connections complete work
4. **Force close**: Terminate remaining connections after timeout
5. **Shutdown**: Server stops completely

### Connection States During Draining

- **Active**: Normal operation, processing messages
- **Draining**: No new messages accepted, completing current work
- **Closing**: Graceful WebSocket close handshake
- **Closed**: Connection terminated

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <libwebsockets.h>

#define MAX_CONNECTIONS 1024
#define DRAIN_TIMEOUT_SEC 30

// Connection state tracking
typedef enum {
    CONN_ACTIVE,
    CONN_DRAINING,
    CONN_CLOSING,
    CONN_CLOSED
} ConnectionState;

typedef struct {
    struct lws *wsi;
    ConnectionState state;
    time_t drain_start_time;
    int pending_operations;
} Connection;

// Server state
typedef struct {
    volatile int is_draining;
    volatile int force_shutdown;
    Connection connections[MAX_CONNECTIONS];
    int connection_count;
    pthread_mutex_t lock;
    time_t drain_start_time;
    char redirect_url[256];
} ServerState;

ServerState server_state = {0};

// Initialize server state
void init_server_state(void) {
    pthread_mutex_init(&server_state.lock, NULL);
    server_state.is_draining = 0;
    server_state.force_shutdown = 0;
    server_state.connection_count = 0;
    server_state.drain_start_time = 0;
    strcpy(server_state.redirect_url, "ws://backup-server.example.com:8080");
}

// Add connection to tracking
int add_connection(struct lws *wsi) {
    pthread_mutex_lock(&server_state.lock);
    
    if (server_state.is_draining) {
        pthread_mutex_unlock(&server_state.lock);
        return -1; // Reject new connections during drain
    }
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server_state.connections[i].wsi == NULL) {
            server_state.connections[i].wsi = wsi;
            server_state.connections[i].state = CONN_ACTIVE;
            server_state.connections[i].pending_operations = 0;
            server_state.connection_count++;
            pthread_mutex_unlock(&server_state.lock);
            return i;
        }
    }
    
    pthread_mutex_unlock(&server_state.lock);
    return -1; // No space available
}

// Remove connection from tracking
void remove_connection(struct lws *wsi) {
    pthread_mutex_lock(&server_state.lock);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server_state.connections[i].wsi == wsi) {
            server_state.connections[i].wsi = NULL;
            server_state.connections[i].state = CONN_CLOSED;
            server_state.connection_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&server_state.lock);
}

// Send drain notification to client
void send_drain_notification(struct lws *wsi, const char *redirect_url) {
    char buffer[LWS_PRE + 512];
    char *payload = &buffer[LWS_PRE];
    
    int len = snprintf(payload, 512,
        "{\"type\":\"drain_notification\","
        "\"message\":\"Server shutting down\","
        "\"redirect_url\":\"%s\","
        "\"timeout_seconds\":%d}",
        redirect_url, DRAIN_TIMEOUT_SEC);
    
    lws_write(wsi, (unsigned char *)payload, len, LWS_WRITE_TEXT);
}

// Initiate connection draining
void initiate_drain(void) {
    pthread_mutex_lock(&server_state.lock);
    
    if (server_state.is_draining) {
        pthread_mutex_unlock(&server_state.lock);
        return;
    }
    
    printf("Initiating connection drain...\n");
    server_state.is_draining = 1;
    server_state.drain_start_time = time(NULL);
    
    // Notify all active connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server_state.connections[i].wsi != NULL &&
            server_state.connections[i].state == CONN_ACTIVE) {
            
            server_state.connections[i].state = CONN_DRAINING;
            server_state.connections[i].drain_start_time = time(NULL);
            
            send_drain_notification(server_state.connections[i].wsi,
                                   server_state.redirect_url);
            
            // Request callback to close connection
            lws_callback_on_writable(server_state.connections[i].wsi);
        }
    }
    
    pthread_mutex_unlock(&server_state.lock);
}

// Check if drain timeout has expired
int check_drain_timeout(void) {
    time_t now = time(NULL);
    
    pthread_mutex_lock(&server_state.lock);
    
    if (!server_state.is_draining) {
        pthread_mutex_unlock(&server_state.lock);
        return 0;
    }
    
    if (now - server_state.drain_start_time > DRAIN_TIMEOUT_SEC) {
        server_state.force_shutdown = 1;
        pthread_mutex_unlock(&server_state.lock);
        return 1;
    }
    
    pthread_mutex_unlock(&server_state.lock);
    return 0;
}

// Force close remaining connections
void force_close_connections(struct lws_context *context) {
    pthread_mutex_lock(&server_state.lock);
    
    printf("Force closing %d remaining connections...\n", 
           server_state.connection_count);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server_state.connections[i].wsi != NULL) {
            lws_close_reason(server_state.connections[i].wsi,
                           LWS_CLOSE_STATUS_GOINGAWAY,
                           (unsigned char *)"Server shutdown", 15);
            server_state.connections[i].state = CONN_CLOSING;
        }
    }
    
    pthread_mutex_unlock(&server_state.lock);
}

// WebSocket callback
static int callback_websocket(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            if (add_connection(wsi) < 0) {
                printf("Rejecting connection during drain\n");
                return -1; // Reject connection
            }
            printf("Connection established\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Check if draining
            pthread_mutex_lock(&server_state.lock);
            if (server_state.is_draining) {
                pthread_mutex_unlock(&server_state.lock);
                // Don't process new messages during drain
                return 0;
            }
            pthread_mutex_unlock(&server_state.lock);
            
            // Process message normally
            printf("Received: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Check if we should close during drain
            pthread_mutex_lock(&server_state.lock);
            int should_close = 0;
            
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                if (server_state.connections[i].wsi == wsi &&
                    server_state.connections[i].state == CONN_DRAINING &&
                    server_state.connections[i].pending_operations == 0) {
                    should_close = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&server_state.lock);
            
            if (should_close) {
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL,
                               (unsigned char *)"Drain complete", 14);
                return -1;
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            remove_connection(wsi);
            printf("Connection closed\n");
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("Received signal %d, initiating drain...\n", sig);
    initiate_drain();
}

// Main drain monitoring thread
void *drain_monitor_thread(void *arg) {
    struct lws_context *context = (struct lws_context *)arg;
    
    while (1) {
        sleep(1);
        
        if (check_drain_timeout()) {
            force_close_connections(context);
            sleep(2); // Give time for close handshakes
            lws_cancel_service(context);
            break;
        }
        
        // Check if all connections closed
        pthread_mutex_lock(&server_state.lock);
        int count = server_state.connection_count;
        int draining = server_state.is_draining;
        pthread_mutex_unlock(&server_state.lock);
        
        if (draining && count == 0) {
            printf("All connections drained gracefully\n");
            lws_cancel_service(context);
            break;
        }
    }
    
    return NULL;
}

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    static struct lws_protocols protocols[] = {
        {
            "websocket-protocol",
            callback_websocket,
            0,
            4096,
        },
        { NULL, NULL, 0, 0 }
    };
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize server state
    init_server_state();
    
    // Configure context
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    printf("WebSocket server started on port 8080\n");
    
    // Start drain monitor thread
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, drain_monitor_thread, context);
    
    // Main event loop
    while (1) {
        if (lws_service(context, 50) < 0) {
            break;
        }
    }
    
    // Cleanup
    pthread_join(monitor_thread, NULL);
    lws_context_destroy(context);
    pthread_mutex_destroy(&server_state.lock);
    
    printf("Server shutdown complete\n");
    return 0;
}
```

## Rust Implementation

```rust
use tokio::net::TcpListener;
use tokio_tungstenite::{accept_async, tungstenite::protocol::Message};
use futures_util::{SinkExt, StreamExt};
use std::sync::{Arc, atomic::{AtomicBool, AtomicUsize, Ordering}};
use std::time::{Duration, Instant};
use tokio::sync::{RwLock, Semaphore};
use tokio::time::timeout;
use serde::{Serialize, Deserialize};

const DRAIN_TIMEOUT: Duration = Duration::from_secs(30);
const MAX_CONNECTIONS: usize = 1024;

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Active,
    Draining,
    Closing,
    Closed,
}

#[derive(Serialize, Deserialize)]
struct DrainNotification {
    r#type: String,
    message: String,
    redirect_url: String,
    timeout_seconds: u64,
}

struct ConnectionInfo {
    state: ConnectionState,
    drain_start: Option<Instant>,
    pending_operations: AtomicUsize,
}

struct ServerState {
    is_draining: AtomicBool,
    drain_start: RwLock<Option<Instant>>,
    active_connections: AtomicUsize,
    redirect_url: String,
    connection_semaphore: Arc<Semaphore>,
}

impl ServerState {
    fn new(redirect_url: String) -> Self {
        Self {
            is_draining: AtomicBool::new(false),
            drain_start: RwLock::new(None),
            active_connections: AtomicUsize::new(0),
            redirect_url,
            connection_semaphore: Arc::new(Semaphore::new(MAX_CONNECTIONS)),
        }
    }

    fn is_draining(&self) -> bool {
        self.is_draining.load(Ordering::SeqCst)
    }

    async fn initiate_drain(&self) {
        if self.is_draining.swap(true, Ordering::SeqCst) {
            return; // Already draining
        }

        println!("Initiating connection drain...");
        *self.drain_start.write().await = Some(Instant::now());
    }

    fn increment_connections(&self) -> bool {
        if self.is_draining() {
            return false; // Reject new connections
        }
        self.active_connections.fetch_add(1, Ordering::SeqCst);
        true
    }

    fn decrement_connections(&self) {
        self.active_connections.fetch_sub(1, Ordering::SeqCst);
    }

    fn connection_count(&self) -> usize {
        self.active_connections.load(Ordering::SeqCst)
    }

    async fn drain_timeout_expired(&self) -> bool {
        if let Some(start) = *self.drain_start.read().await {
            start.elapsed() > DRAIN_TIMEOUT
        } else {
            false
        }
    }
}

async fn send_drain_notification(
    sink: &mut futures_util::stream::SplitSink
        tokio_tungstenite::WebSocketStream<tokio::net::TcpStream>,
        Message
    >,
    redirect_url: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let notification = DrainNotification {
        r#type: "drain_notification".to_string(),
        message: "Server shutting down".to_string(),
        redirect_url: redirect_url.to_string(),
        timeout_seconds: DRAIN_TIMEOUT.as_secs(),
    };

    let json = serde_json::to_string(&notification)?;
    sink.send(Message::Text(json)).await?;
    Ok(())
}

async fn handle_connection(
    stream: tokio::net::TcpStream,
    state: Arc<ServerState>,
) -> Result<(), Box<dyn std::error::Error>> {
    // Try to acquire connection slot
    let _permit = match state.connection_semaphore.clone().try_acquire_owned() {
        Ok(permit) => permit,
        Err(_) => {
            println!("Connection limit reached, rejecting connection");
            return Ok(());
        }
    };

    if !state.increment_connections() {
        println!("Rejecting connection during drain");
        return Ok(());
    }

    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");

    let (mut write, mut read) = ws_stream.split();
    let conn_info = Arc::new(RwLock::new(ConnectionInfo {
        state: ConnectionState::Active,
        drain_start: None,
        pending_operations: AtomicUsize::new(0),
    }));

    let conn_info_clone = conn_info.clone();
    let state_clone = state.clone();

    // Spawn task to monitor drain state
    let drain_monitor = tokio::spawn(async move {
        loop {
            tokio::time::sleep(Duration::from_millis(500)).await;

            if state_clone.is_draining() {
                let mut info = conn_info_clone.write().await;
                if info.state == ConnectionState::Active {
                    info.state = ConnectionState::Draining;
                    info.drain_start = Some(Instant::now());
                    drop(info);

                    // Send drain notification
                    if send_drain_notification(&mut write, &state_clone.redirect_url)
                        .await
                        .is_err()
                    {
                        break;
                    }
                }

                // Check if we should close
                let info = conn_info_clone.read().await;
                if info.state == ConnectionState::Draining
                    && info.pending_operations.load(Ordering::SeqCst) == 0
                {
                    drop(info);
                    let _ = write
                        .send(Message::Close(Some(
                            tokio_tungstenite::tungstenite::protocol::CloseFrame {
                                code: tokio_tungstenite::tungstenite::protocol::frame::coding::CloseCode::Normal,
                                reason: "Drain complete".into(),
                            },
                        )))
                        .await;
                    break;
                }
            }
        }
    });

    // Message processing loop
    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                // Check if draining
                if state.is_draining() {
                    println!("Ignoring message during drain: {}", text);
                    continue;
                }

                // Simulate pending operation
                conn_info.read().await.pending_operations.fetch_add(1, Ordering::SeqCst);
                
                println!("Received: {}", text);
                
                // Process message (simulate work)
                tokio::time::sleep(Duration::from_millis(100)).await;
                
                // Complete operation
                conn_info.read().await.pending_operations.fetch_sub(1, Ordering::SeqCst);
            }
            Ok(Message::Close(_)) => {
                println!("Client initiated close");
                break;
            }
            Ok(Message::Ping(data)) => {
                let _ = write.send(Message::Pong(data)).await;
            }
            Err(e) => {
                eprintln!("WebSocket error: {}", e);
                break;
            }
            _ => {}
        }
    }

    drain_monitor.abort();
    state.decrement_connections();
    println!("Connection closed");

    Ok(())
}

async fn drain_monitor_task(state: Arc<ServerState>) {
    loop {
        tokio::time::sleep(Duration::from_secs(1)).await;

        if !state.is_draining() {
            continue;
        }

        let count = state.connection_count();
        println!("Draining... {} connections remaining", count);

        if count == 0 {
            println!("All connections drained gracefully");
            break;
        }

        if state.drain_timeout_expired().await {
            println!("Drain timeout expired, forcing shutdown");
            break;
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on {}", addr);

    let state = Arc::new(ServerState::new(
        "ws://backup-server.example.com:8080".to_string(),
    ));

    let state_clone = state.clone();
    
    // Setup signal handler for graceful shutdown
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.expect("Failed to listen for ctrl-c");
        println!("Received shutdown signal");
        state_clone.initiate_drain().await;
    });

    let state_clone = state.clone();
    let drain_task = tokio::spawn(async move {
        drain_monitor_task(state_clone).await;
    });

    // Accept connections with timeout during drain
    loop {
        let accept_result = if state.is_draining() {
            timeout(Duration::from_secs(1), listener.accept()).await
        } else {
            Ok(listener.accept().await)
        };

        match accept_result {
            Ok(Ok((stream, addr))) => {
                if state.is_draining() {
                    println!("Rejecting new connection from {} during drain", addr);
                    continue;
                }

                println!("New connection from {}", addr);
                let state_clone = state.clone();
                
                tokio::spawn(async move {
                    if let Err(e) = handle_connection(stream, state_clone).await {
                        eprintln!("Error handling connection: {}", e);
                    }
                });
            }
            Ok(Err(e)) => {
                eprintln!("Accept error: {}", e);
            }
            Err(_) => {
                // Timeout during drain - check if we should exit
                if state.is_draining() && state.connection_count() == 0 {
                    break;
                }
            }
        }
    }

    drain_task.await?;
    println!("Server shutdown complete");

    Ok(())
}
```

## Summary

**Connection Draining** is an essential pattern for production WebSocket servers that enables:

- **Zero-downtime deployments**: Gracefully migrate connections to new server versions
- **Planned maintenance**: Safely take servers offline without disrupting clients
- **Load balancing**: Remove servers from rotation while preserving active sessions
- **Connection migration**: Redirect clients to alternative endpoints

The pattern involves stopping new connection acceptance, notifying existing clients with optional redirect information, allowing in-flight operations to complete within a timeout window, and forcefully closing any remaining connections after the timeout expires. Both implementations demonstrate tracking connection states, implementing timeout mechanisms, providing client notifications with redirect URLs, and handling graceful vs. forced shutdown scenarios. This approach ensures reliable service continuity while maintaining operational flexibility for server infrastructure changes.