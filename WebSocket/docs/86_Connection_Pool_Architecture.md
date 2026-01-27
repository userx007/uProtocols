# Connection Pool Architecture for WebSocket Integration

## Overview

Connection Pool Architecture is a design pattern for managing multiple WebSocket connections efficiently in backend systems. Instead of creating and destroying connections on demand, a pool maintains a set of reusable connections, reducing overhead and improving performance for high-throughput applications.

## Core Concepts

### Why Connection Pooling?

**Traditional Approach Problems:**
- Creating new WebSocket connections is expensive (TCP handshake, TLS negotiation, WebSocket upgrade)
- Frequent connection/disconnection causes resource churn
- Unpredictable latency during connection establishment
- Resource exhaustion under high load

**Connection Pool Benefits:**
- Amortizes connection setup costs
- Predictable performance characteristics
- Better resource utilization
- Graceful degradation under load
- Connection reuse across multiple operations

### Key Components

1. **Pool Manager** - Controls pool lifecycle, sizing, and health monitoring
2. **Connection Objects** - Individual WebSocket connections with state tracking
3. **Acquisition/Release Logic** - Thread-safe borrowing and returning of connections
4. **Health Checks** - Validates connection viability before use
5. **Reconnection Strategy** - Handles connection failures and recovery

## C/C++ Implementation

```c
#include <libwebsockets.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POOL_MIN_SIZE 5
#define POOL_MAX_SIZE 20
#define HEALTH_CHECK_INTERVAL 30

typedef enum {
    CONN_AVAILABLE,
    CONN_IN_USE,
    CONN_UNHEALTHY,
    CONN_CONNECTING
} connection_state_t;

typedef struct ws_connection {
    struct lws *wsi;
    connection_state_t state;
    time_t last_used;
    time_t last_health_check;
    int retry_count;
    struct ws_connection *next;
} ws_connection_t;

typedef struct {
    ws_connection_t *head;
    pthread_mutex_t lock;
    pthread_cond_t available;
    size_t total_count;
    size_t available_count;
    size_t max_size;
    size_t min_size;
    struct lws_context *context;
    char *server_address;
    int port;
    char *path;
} ws_connection_pool_t;

// Initialize connection pool
ws_connection_pool_t* ws_pool_create(const char *address, int port, 
                                      const char *path, size_t min_size, 
                                      size_t max_size) {
    ws_connection_pool_t *pool = calloc(1, sizeof(ws_connection_pool_t));
    if (!pool) return NULL;
    
    pool->server_address = strdup(address);
    pool->port = port;
    pool->path = strdup(path);
    pool->min_size = min_size;
    pool->max_size = max_size;
    
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->available, NULL);
    
    // Create libwebsockets context
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = /* protocol definitions */;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    pool->context = lws_create_context(&info);
    
    // Pre-populate pool with minimum connections
    for (size_t i = 0; i < min_size; i++) {
        ws_connection_t *conn = create_connection(pool);
        if (conn) {
            conn->next = pool->head;
            pool->head = conn;
            pool->total_count++;
            pool->available_count++;
        }
    }
    
    return pool;
}

// Create a new connection
ws_connection_t* create_connection(ws_connection_pool_t *pool) {
    ws_connection_t *conn = calloc(1, sizeof(ws_connection_t));
    if (!conn) return NULL;
    
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = pool->context;
    ccinfo.address = pool->server_address;
    ccinfo.port = pool->port;
    ccinfo.path = pool->path;
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = "ws-pool-protocol";
    ccinfo.ssl_connection = LXWS_SSL_CONNECTION;
    
    conn->wsi = lws_client_connect_via_info(&ccinfo);
    conn->state = CONN_CONNECTING;
    conn->last_used = time(NULL);
    conn->retry_count = 0;
    
    return conn;
}

// Acquire connection from pool
ws_connection_t* ws_pool_acquire(ws_connection_pool_t *pool, int timeout_ms) {
    pthread_mutex_lock(&pool->lock);
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    
    while (pool->available_count == 0) {
        // Try to create new connection if under max limit
        if (pool->total_count < pool->max_size) {
            ws_connection_t *conn = create_connection(pool);
            if (conn) {
                conn->next = pool->head;
                pool->head = conn;
                pool->total_count++;
                break;
            }
        }
        
        // Wait for available connection
        int ret = pthread_cond_timedwait(&pool->available, &pool->lock, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
    }
    
    // Find healthy available connection
    ws_connection_t *conn = pool->head;
    ws_connection_t *prev = NULL;
    
    while (conn) {
        if (conn->state == CONN_AVAILABLE) {
            // Perform health check if needed
            time_t now = time(NULL);
            if (now - conn->last_health_check > HEALTH_CHECK_INTERVAL) {
                if (!perform_health_check(conn)) {
                    conn->state = CONN_UNHEALTHY;
                    conn = conn->next;
                    continue;
                }
                conn->last_health_check = now;
            }
            
            conn->state = CONN_IN_USE;
            conn->last_used = now;
            pool->available_count--;
            pthread_mutex_unlock(&pool->lock);
            return conn;
        }
        prev = conn;
        conn = conn->next;
    }
    
    pthread_mutex_unlock(&pool->lock);
    return NULL;
}

// Release connection back to pool
void ws_pool_release(ws_connection_pool_t *pool, ws_connection_t *conn) {
    pthread_mutex_lock(&pool->lock);
    
    if (conn->state == CONN_UNHEALTHY) {
        // Remove and recreate if below minimum
        remove_connection(pool, conn);
        if (pool->total_count < pool->min_size) {
            ws_connection_t *new_conn = create_connection(pool);
            if (new_conn) {
                new_conn->next = pool->head;
                pool->head = new_conn;
                pool->total_count++;
                pool->available_count++;
            }
        }
    } else {
        conn->state = CONN_AVAILABLE;
        pool->available_count++;
        pthread_cond_signal(&pool->available);
    }
    
    pthread_mutex_unlock(&pool->lock);
}

// Health check implementation
bool perform_health_check(ws_connection_t *conn) {
    if (!conn->wsi) return false;
    
    // Send ping frame
    unsigned char buf[LWS_PRE + 125];
    int n = lws_write(conn->wsi, &buf[LWS_PRE], 0, LWS_WRITE_PING);
    
    return n >= 0;
}

// Cleanup pool
void ws_pool_destroy(ws_connection_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    
    ws_connection_t *conn = pool->head;
    while (conn) {
        ws_connection_t *next = conn->next;
        if (conn->wsi) {
            lws_close_reason(conn->wsi, LWS_CLOSE_STATUS_GOINGAWAY, 
                           (unsigned char *)"pool shutdown", 14);
        }
        free(conn);
        conn = next;
    }
    
    lws_context_destroy(pool->context);
    free(pool->server_address);
    free(pool->path);
    
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->available);
    free(pool);
}
```

## Rust Implementation

```rust
use tokio_tungstenite::{connect_async, WebSocketStream, MaybeTlsStream};
use tokio::net::TcpStream;
use tokio::sync::{Mutex, Semaphore};
use tokio::time::{Duration, Instant};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use url::Url;
use anyhow::{Result, anyhow};

#[derive(Debug, Clone, Copy, PartialEq)]
enum ConnectionState {
    Available,
    InUse,
    Unhealthy,
    Connecting,
}

struct PooledConnection {
    ws: WebSocketStream<MaybeTlsStream<TcpStream>>,
    state: ConnectionState,
    last_used: Instant,
    last_health_check: Instant,
    retry_count: u32,
}

impl PooledConnection {
    async fn health_check(&mut self) -> Result<()> {
        use tokio_tungstenite::tungstenite::protocol::Message;
        
        // Send ping
        self.ws.send(Message::Ping(vec![1, 2, 3, 4])).await?;
        
        // Wait for pong with timeout
        let timeout = Duration::from_secs(5);
        match tokio::time::timeout(timeout, self.ws.next()).await {
            Ok(Some(Ok(Message::Pong(_)))) => {
                self.last_health_check = Instant::now();
                Ok(())
            }
            _ => Err(anyhow!("Health check failed")),
        }
    }
}

pub struct WebSocketConnectionPool {
    connections: Arc<Mutex<Vec<PooledConnection>>>,
    semaphore: Arc<Semaphore>,
    url: Url,
    min_size: usize,
    max_size: usize,
    health_check_interval: Duration,
}

impl WebSocketConnectionPool {
    pub async fn new(
        url: &str,
        min_size: usize,
        max_size: usize,
    ) -> Result<Self> {
        let url = Url::parse(url)?;
        let connections = Arc::new(Mutex::new(Vec::new()));
        let semaphore = Arc::new(Semaphore::new(max_size));
        
        let pool = Self {
            connections,
            semaphore,
            url,
            min_size,
            max_size,
            health_check_interval: Duration::from_secs(30),
        };
        
        // Pre-populate pool
        pool.populate_pool(min_size).await?;
        
        // Start background health checker
        pool.start_health_checker();
        
        Ok(pool)
    }
    
    async fn populate_pool(&self, count: usize) -> Result<()> {
        let mut connections = self.connections.lock().await;
        
        for _ in 0..count {
            match self.create_connection().await {
                Ok(conn) => connections.push(conn),
                Err(e) => eprintln!("Failed to create connection: {}", e),
            }
        }
        
        Ok(())
    }
    
    async fn create_connection(&self) -> Result<PooledConnection> {
        let (ws, _) = connect_async(self.url.as_str()).await?;
        
        Ok(PooledConnection {
            ws,
            state: ConnectionState::Available,
            last_used: Instant::now(),
            last_health_check: Instant::now(),
            retry_count: 0,
        })
    }
    
    pub async fn acquire(&self) -> Result<PooledConnectionGuard> {
        // Acquire semaphore permit
        let permit = self.semaphore.clone().acquire_owned().await?;
        
        let mut connections = self.connections.lock().await;
        
        // Try to find available healthy connection
        for conn in connections.iter_mut() {
            if conn.state == ConnectionState::Available {
                // Health check if needed
                let elapsed = conn.last_health_check.elapsed();
                if elapsed > self.health_check_interval {
                    if conn.health_check().await.is_err() {
                        conn.state = ConnectionState::Unhealthy;
                        continue;
                    }
                }
                
                conn.state = ConnectionState::InUse;
                conn.last_used = Instant::now();
                
                let index = connections.iter().position(|c| 
                    std::ptr::eq(c, conn as *const _)
                ).unwrap();
                
                return Ok(PooledConnectionGuard {
                    pool: self.connections.clone(),
                    index,
                    _permit: permit,
                });
            }
        }
        
        // No available connection, create new one if under max
        if connections.len() < self.max_size {
            let mut new_conn = self.create_connection().await?;
            new_conn.state = ConnectionState::InUse;
            connections.push(new_conn);
            let index = connections.len() - 1;
            
            return Ok(PooledConnectionGuard {
                pool: self.connections.clone(),
                index,
                _permit: permit,
            });
        }
        
        Err(anyhow!("No connections available"))
    }
    
    fn start_health_checker(&self) {
        let connections = self.connections.clone();
        let interval = self.health_check_interval;
        let url = self.url.clone();
        let min_size = self.min_size;
        
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(interval);
            
            loop {
                ticker.tick().await;
                
                let mut conns = connections.lock().await;
                let mut unhealthy_count = 0;
                
                for conn in conns.iter_mut() {
                    if conn.state == ConnectionState::Available {
                        if conn.health_check().await.is_err() {
                            conn.state = ConnectionState::Unhealthy;
                            unhealthy_count += 1;
                        }
                    }
                }
                
                // Remove unhealthy connections
                conns.retain(|c| c.state != ConnectionState::Unhealthy);
                
                // Replenish to minimum size
                while conns.len() < min_size {
                    // Connection creation logic here
                    break;
                }
            }
        });
    }
    
    pub async fn stats(&self) -> PoolStats {
        let connections = self.connections.lock().await;
        
        let total = connections.len();
        let available = connections.iter()
            .filter(|c| c.state == ConnectionState::Available)
            .count();
        let in_use = connections.iter()
            .filter(|c| c.state == ConnectionState::InUse)
            .count();
        
        PoolStats {
            total,
            available,
            in_use,
            max_size: self.max_size,
        }
    }
}

pub struct PooledConnectionGuard {
    pool: Arc<Mutex<Vec<PooledConnection>>>,
    index: usize,
    _permit: tokio::sync::OwnedSemaphorePermit,
}

impl PooledConnectionGuard {
    pub async fn send(&mut self, msg: tokio_tungstenite::tungstenite::Message) -> Result<()> {
        let mut pool = self.pool.lock().await;
        pool[self.index].ws.send(msg).await?;
        Ok(())
    }
    
    pub async fn recv(&mut self) -> Result<Option<tokio_tungstenite::tungstenite::Message>> {
        let mut pool = self.pool.lock().await;
        Ok(pool[self.index].ws.next().await.transpose()?)
    }
}

impl Drop for PooledConnectionGuard {
    fn drop(&mut self) {
        let pool = self.pool.clone();
        let index = self.index;
        
        tokio::spawn(async move {
            let mut connections = pool.lock().await;
            if index < connections.len() {
                connections[index].state = ConnectionState::Available;
            }
        });
    }
}

#[derive(Debug)]
pub struct PoolStats {
    pub total: usize,
    pub available: usize,
    pub in_use: usize,
    pub max_size: usize,
}

// Example usage
#[tokio::main]
async fn main() -> Result<()> {
    let pool = WebSocketConnectionPool::new(
        "wss://echo.websocket.org",
        5,  // min connections
        20, // max connections
    ).await?;
    
    // Spawn multiple tasks using the pool
    let mut handles = vec![];
    
    for i in 0..10 {
        let pool = pool.clone();
        let handle = tokio::spawn(async move {
            let mut conn = pool.acquire().await.unwrap();
            
            conn.send(tokio_tungstenite::tungstenite::Message::Text(
                format!("Hello from task {}", i)
            )).await.unwrap();
            
            if let Some(msg) = conn.recv().await.unwrap() {
                println!("Task {} received: {:?}", i, msg);
            }
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.await?;
    }
    
    let stats = pool.stats().await;
    println!("Pool stats: {:?}", stats);
    
    Ok(())
}
```

## Summary

Connection Pool Architecture for WebSockets provides critical infrastructure for scalable backend systems. Key takeaways:

**Architecture Benefits:**
- Reduces connection overhead by 80-95% in high-throughput scenarios
- Provides predictable latency characteristics
- Enables graceful degradation under load
- Simplifies connection lifecycle management

**Implementation Essentials:**
- Thread-safe acquire/release mechanisms with timeout support
- Automated health checking and connection recycling
- Dynamic pool sizing between min/max bounds
- Reconnection strategies for failed connections

**Best Practices:**
- Size pools based on expected concurrency (typical: 5-20 connections per backend)
- Implement exponential backoff for reconnection attempts
- Monitor pool statistics (utilization, wait times, health check failures)
- Use connection affinity when state must be maintained
- Configure appropriate timeouts for acquire operations

This pattern is essential for microservices architectures, real-time data aggregation services, and any system requiring persistent WebSocket connections to multiple backends.