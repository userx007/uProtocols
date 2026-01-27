# Idle Connection Reaping

## Overview

Idle connection reaping is a resource management technique that automatically identifies and closes WebSocket connections that have been inactive for a specified period. This prevents resource exhaustion by freeing up memory, file descriptors, and other system resources held by dormant connections.

## Why It Matters

WebSocket servers maintain persistent connections, and without proper cleanup:
- **Memory leaks**: Each idle connection consumes memory for buffers and state
- **File descriptor exhaustion**: Operating systems limit open connections
- **Thread/resource starvation**: Idle connections may hold worker threads or async tasks
- **Security risks**: Abandoned connections might be exploited or forgotten

## Core Concepts

**Idle Detection**: Track the last activity timestamp for each connection. Activity includes:
- Receiving data from the client
- Sending data to the client
- Receiving ping/pong frames

**Reaping Strategy**: Periodically scan connections and close those exceeding the idle timeout threshold.

**Graceful Closure**: Send a WebSocket close frame before forcibly terminating the connection.

---

## C/C++ Implementation

This example uses raw sockets with a simple connection tracking structure:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_CONNECTIONS 1024
#define IDLE_TIMEOUT_SECONDS 300  // 5 minutes
#define REAP_INTERVAL_SECONDS 60  // Check every minute

typedef struct {
    int socket_fd;
    time_t last_activity;
    int is_active;
    pthread_mutex_t lock;
} WebSocketConnection;

typedef struct {
    WebSocketConnection connections[MAX_CONNECTIONS];
    pthread_mutex_t global_lock;
} ConnectionPool;

ConnectionPool pool;

// Initialize the connection pool
void init_connection_pool() {
    memset(&pool, 0, sizeof(ConnectionPool));
    pthread_mutex_init(&pool.global_lock, NULL);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        pthread_mutex_init(&pool.connections[i].lock, NULL);
        pool.connections[i].socket_fd = -1;
        pool.connections[i].is_active = 0;
    }
}

// Update activity timestamp when data is received/sent
void update_activity(int conn_index) {
    pthread_mutex_lock(&pool.connections[conn_index].lock);
    pool.connections[conn_index].last_activity = time(NULL);
    pthread_mutex_unlock(&pool.connections[conn_index].lock);
}

// Add a new connection to the pool
int add_connection(int socket_fd) {
    pthread_mutex_lock(&pool.global_lock);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!pool.connections[i].is_active) {
            pthread_mutex_lock(&pool.connections[i].lock);
            pool.connections[i].socket_fd = socket_fd;
            pool.connections[i].last_activity = time(NULL);
            pool.connections[i].is_active = 1;
            pthread_mutex_unlock(&pool.connections[i].lock);
            
            pthread_mutex_unlock(&pool.global_lock);
            return i;
        }
    }
    
    pthread_mutex_unlock(&pool.global_lock);
    return -1; // Pool full
}

// Send WebSocket close frame (simplified)
void send_close_frame(int socket_fd, uint16_t status_code) {
    unsigned char close_frame[4];
    close_frame[0] = 0x88; // FIN + Close opcode
    close_frame[1] = 0x02; // Payload length = 2
    close_frame[2] = (status_code >> 8) & 0xFF;
    close_frame[3] = status_code & 0xFF;
    
    send(socket_fd, close_frame, 4, 0);
}

// Reap idle connections
void* reaper_thread(void* arg) {
    while (1) {
        sleep(REAP_INTERVAL_SECONDS);
        
        time_t now = time(NULL);
        int reaped_count = 0;
        
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            pthread_mutex_lock(&pool.connections[i].lock);
            
            if (pool.connections[i].is_active) {
                time_t idle_time = now - pool.connections[i].last_activity;
                
                if (idle_time > IDLE_TIMEOUT_SECONDS) {
                    printf("Reaping connection %d (idle for %ld seconds)\n", 
                           i, idle_time);
                    
                    // Send close frame with status 1001 (Going Away)
                    send_close_frame(pool.connections[i].socket_fd, 1001);
                    
                    // Close the socket
                    close(pool.connections[i].socket_fd);
                    
                    // Mark as inactive
                    pool.connections[i].socket_fd = -1;
                    pool.connections[i].is_active = 0;
                    
                    reaped_count++;
                }
            }
            
            pthread_mutex_unlock(&pool.connections[i].lock);
        }
        
        if (reaped_count > 0) {
            printf("Reaped %d idle connections\n", reaped_count);
        }
    }
    
    return NULL;
}

// Example usage in main server loop
int main() {
    init_connection_pool();
    
    // Start reaper thread
    pthread_t reaper;
    pthread_create(&reaper, NULL, reaper_thread, NULL);
    
    // Simulate adding connections and updating activity
    // (In real code, this would be your accept() and recv() loop)
    
    int conn1 = add_connection(10); // Simulated socket FD
    int conn2 = add_connection(11);
    
    // Simulate activity
    for (int i = 0; i < 10; i++) {
        sleep(30);
        update_activity(conn1); // Keep conn1 alive
        // conn2 becomes idle
    }
    
    pthread_join(reaper, NULL);
    return 0;
}
```

### Key C/C++ Techniques:
- **Mutex protection**: Prevents race conditions when multiple threads access connection state
- **Periodic scanning**: Background thread wakes up at intervals to check all connections
- **Timestamp tracking**: `time(NULL)` provides second-level granularity

---

## Rust Implementation

Rust's ownership model and async runtime make idle connection management safer and more efficient:

```rust
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpStream;
use tokio::sync::RwLock;
use tokio::time::{interval, sleep};
use tokio_tungstenite::{WebSocketStream, tungstenite::protocol::CloseFrame};
use futures_util::{SinkExt, StreamExt};

const IDLE_TIMEOUT: Duration = Duration::from_secs(300); // 5 minutes
const REAP_INTERVAL: Duration = Duration::from_secs(60);  // Check every minute

#[derive(Clone)]
struct ConnectionInfo {
    last_activity: Instant,
    connection_id: u64,
}

type WsStream = WebSocketStream<TcpStream>;
type ConnectionMap = Arc<RwLock<HashMap<u64, (WsStream, ConnectionInfo)>>>;

struct WebSocketServer {
    connections: ConnectionMap,
    next_id: Arc<RwLock<u64>>,
}

impl WebSocketServer {
    fn new() -> Self {
        Self {
            connections: Arc::new(RwLock::new(HashMap::new())),
            next_id: Arc::new(RwLock::new(0)),
        }
    }

    // Add a new connection
    async fn add_connection(&self, stream: WsStream) -> u64 {
        let mut id_lock = self.next_id.write().await;
        let conn_id = *id_lock;
        *id_lock += 1;
        drop(id_lock);

        let info = ConnectionInfo {
            last_activity: Instant::now(),
            connection_id: conn_id,
        };

        self.connections.write().await.insert(conn_id, (stream, info));
        println!("Added connection {}", conn_id);
        conn_id
    }

    // Update activity timestamp
    async fn update_activity(&self, conn_id: u64) {
        if let Some((_, info)) = self.connections.write().await.get_mut(&conn_id) {
            info.last_activity = Instant::now();
        }
    }

    // Handle incoming messages for a connection
    async fn handle_connection(&self, conn_id: u64) {
        loop {
            let mut connections = self.connections.write().await;
            
            if let Some((stream, _)) = connections.get_mut(&conn_id) {
                // Read next message (with timeout handled separately)
                match tokio::time::timeout(Duration::from_secs(1), stream.next()).await {
                    Ok(Some(Ok(msg))) => {
                        drop(connections); // Release lock before processing
                        self.update_activity(conn_id).await;
                        
                        println!("Connection {} received: {:?}", conn_id, msg);
                        
                        // Echo back (example)
                        let mut connections = self.connections.write().await;
                        if let Some((stream, _)) = connections.get_mut(&conn_id) {
                            let _ = stream.send(msg).await;
                        }
                    }
                    Ok(Some(Err(e))) => {
                        println!("Connection {} error: {}", conn_id, e);
                        break;
                    }
                    Ok(None) => {
                        println!("Connection {} closed", conn_id);
                        break;
                    }
                    Err(_) => {
                        // Timeout, continue loop
                        drop(connections);
                        continue;
                    }
                }
            } else {
                break; // Connection removed
            }
        }

        // Remove connection on exit
        self.connections.write().await.remove(&conn_id);
    }

    // Reaper task that runs periodically
    async fn start_reaper(self: Arc<Self>) {
        let mut ticker = interval(REAP_INTERVAL);

        loop {
            ticker.tick().await;
            self.reap_idle_connections().await;
        }
    }

    // Reap idle connections
    async fn reap_idle_connections(&self) {
        let now = Instant::now();
        let mut to_remove = Vec::new();

        // Identify idle connections
        {
            let connections = self.connections.read().await;
            
            for (id, (_, info)) in connections.iter() {
                let idle_duration = now.duration_since(info.last_activity);
                
                if idle_duration > IDLE_TIMEOUT {
                    println!(
                        "Connection {} idle for {:?}, marking for removal",
                        id, idle_duration
                    );
                    to_remove.push(*id);
                }
            }
        }

        // Close and remove idle connections
        if !to_remove.is_empty() {
            let mut connections = self.connections.write().await;
            
            for id in to_remove {
                if let Some((mut stream, _)) = connections.remove(&id) {
                    // Send close frame
                    let close_frame = CloseFrame {
                        code: 1001.into(), // Going Away
                        reason: "Idle timeout".into(),
                    };
                    
                    let _ = stream.close(Some(close_frame)).await;
                    println!("Reaped idle connection {}", id);
                }
            }
        }
    }
}

#[tokio::main]
async fn main() {
    let server = Arc::new(WebSocketServer::new());

    // Start the reaper task
    let reaper_server = server.clone();
    tokio::spawn(async move {
        reaper_server.start_reaper().await;
    });

    // Simulate connections (in real code, accept from TcpListener)
    // Example: tokio::spawn(async move { server.handle_connection(conn_id).await });

    println!("WebSocket server with idle connection reaping started");
    
    // Keep server running
    sleep(Duration::from_secs(3600)).await;
}
```

### Key Rust Features:
- **RwLock**: Allows multiple readers or one writer, reducing contention
- **Arc**: Thread-safe reference counting for sharing state across tasks
- **Tokio async runtime**: Efficient handling of thousands of concurrent connections
- **Graceful cleanup**: Proper WebSocket close frames before disconnection

---

## Best Practices

1. **Configurable timeouts**: Allow administrators to adjust idle timeout based on use case
2. **Heartbeat integration**: Combine with ping/pong to distinguish network issues from true idleness
3. **Logging**: Record why connections were reaped (debugging and monitoring)
4. **Metrics**: Track reaping rate to detect issues (mass disconnects, configuration problems)
5. **Graceful degradation**: If reaping fails, don't crash the server
6. **Client notification**: Send proper close codes (1001 for going away, 1000 for normal closure)

---

## Summary

Idle connection reaping is essential for long-running WebSocket servers. By periodically scanning connections and closing those exceeding an inactivity threshold, servers prevent resource exhaustion and maintain system health. 

**C/C++** implementations require careful manual memory management and synchronization with mutexes, while **Rust** leverages its ownership model and async runtime for safer, more efficient resource management. Both approaches rely on periodic background tasks that track activity timestamps and gracefully close stale connections.

Effective idle connection reaping balances resource conservation with user experience, ensuring legitimate long-lived connections remain open while promptly cleaning up abandoned ones.