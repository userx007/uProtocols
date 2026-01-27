# Kubernetes WebSocket Services

## Overview

Kubernetes WebSocket Services involve deploying, managing, and scaling WebSocket applications within a Kubernetes cluster. WebSocket connections are long-lived, bidirectional, and stateful, which presents unique challenges in containerized, orchestrated environments where pods can be created, destroyed, or moved dynamically.

## Key Challenges

**Connection Persistence**: WebSocket connections require persistent TCP connections that can last for extended periods. When pods restart or scale, these connections can be disrupted.

**Load Balancing**: Traditional round-robin load balancing doesn't work well with WebSockets since connections are long-lived. Session affinity (sticky sessions) is often required.

**Graceful Shutdown**: When pods are terminated, existing WebSocket connections need to be handled gracefully to avoid abrupt disconnections.

**Health Checks**: Kubernetes liveness and readiness probes need to be configured properly to avoid killing pods with active WebSocket connections.

## Architecture Considerations

### Ingress Configuration

Kubernetes Ingress controllers need special configuration for WebSocket traffic:
- Enable WebSocket protocol upgrades
- Configure connection timeouts appropriately (often much longer than HTTP)
- Implement session affinity if needed

### Service Types

- **ClusterIP**: Internal service access
- **NodePort**: Direct node access (useful for testing)
- **LoadBalancer**: External access with cloud provider integration

## C/C++ Implementation Example

Here's a WebSocket server implementation using the `websocketpp` library suitable for Kubernetes deployment:

```c++
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include <set>
#include <signal.h>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> con_list;

class WebSocketServer {
private:
    server m_server;
    con_list m_connections;
    std::mutex m_connection_lock;
    bool m_shutdown_requested;

public:
    WebSocketServer() : m_shutdown_requested(false) {
        m_server.init_asio();
        m_server.set_reuse_addr(true);
        
        // Connection handlers
        m_server.set_open_handler([this](websocketpp::connection_hdl hdl) {
            on_open(hdl);
        });
        
        m_server.set_close_handler([this](websocketpp::connection_hdl hdl) {
            on_close(hdl);
        });
        
        m_server.set_message_handler([this](websocketpp::connection_hdl hdl, server::message_ptr msg) {
            on_message(hdl, msg);
        });
    }

    void on_open(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(m_connection_lock);
        m_connections.insert(hdl);
        std::cout << "Connection opened. Total connections: " << m_connections.size() << std::endl;
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(m_connection_lock);
        m_connections.erase(hdl);
        std::cout << "Connection closed. Total connections: " << m_connections.size() << std::endl;
    }

    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
        // Echo server example
        try {
            m_server.send(hdl, msg->get_payload(), msg->get_opcode());
        } catch (const std::exception& e) {
            std::cerr << "Send failed: " << e.what() << std::endl;
        }
    }

    // Health check endpoint for Kubernetes probes
    int get_connection_count() {
        std::lock_guard<std::mutex> lock(m_connection_lock);
        return m_connections.size();
    }

    // Graceful shutdown for Kubernetes pod termination
    void graceful_shutdown() {
        std::cout << "Initiating graceful shutdown..." << std::endl;
        m_shutdown_requested = true;
        
        // Close all connections gracefully
        {
            std::lock_guard<std::mutex> lock(m_connection_lock);
            for (auto it : m_connections) {
                try {
                    m_server.close(it, websocketpp::close::status::going_away, 
                                   "Server shutting down");
                } catch (const std::exception& e) {
                    std::cerr << "Error closing connection: " << e.what() << std::endl;
                }
            }
        }
        
        // Give connections time to close
        std::this_thread::sleep_for(std::chrono::seconds(5));
        m_server.stop();
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        
        std::cout << "WebSocket server listening on port " << port << std::endl;
        m_server.run();
    }

    void stop() {
        m_server.stop_listening();
    }
};

// Global server instance for signal handling
WebSocketServer* g_server = nullptr;

void signal_handler(int signal) {
    if (g_server && signal == SIGTERM) {
        g_server->graceful_shutdown();
    }
}

int main() {
    WebSocketServer ws_server;
    g_server = &ws_server;
    
    // Handle SIGTERM from Kubernetes
    signal(SIGTERM, signal_handler);
    
    // Read port from environment variable (Kubernetes pattern)
    const char* port_env = std::getenv("WS_PORT");
    uint16_t port = port_env ? std::atoi(port_env) : 8080;
    
    try {
        ws_server.run(port);
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

**Dockerfile for C++ WebSocket Server:**

```dockerfile
FROM ubuntu:22.04 as builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libwebsocketpp-dev \
    git

WORKDIR /app
COPY . .
RUN g++ -std=c++14 -o ws_server main.cpp -lboost_system -lpthread

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libboost-system1.74.0
COPY --from=builder /app/ws_server /usr/local/bin/
EXPOSE 8080
CMD ["ws_server"]
```

## Rust Implementation Example

Using the `tokio-tungstenite` library for async WebSocket handling:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use tokio::sync::{RwLock, Mutex};
use std::collections::HashMap;
use tokio::signal;

type ConnectionId = usize;
type Connections = Arc<RwLock<HashMap<ConnectionId, Arc<Mutex<tokio_tungstenite::WebSocketStream<TcpStream>>>>>>;

struct WebSocketServer {
    connections: Connections,
    next_id: Arc<Mutex<ConnectionId>>,
}

impl WebSocketServer {
    fn new() -> Self {
        Self {
            connections: Arc::new(RwLock::new(HashMap::new())),
            next_id: Arc::new(Mutex::new(0)),
        }
    }

    async fn handle_connection(
        &self,
        stream: TcpStream,
        addr: std::net::SocketAddr,
    ) -> Result<(), Box<dyn std::error::Error>> {
        println!("New connection from: {}", addr);
        
        let ws_stream = accept_async(stream).await?;
        let ws_stream = Arc::new(Mutex::new(ws_stream));
        
        // Register connection
        let conn_id = {
            let mut id = self.next_id.lock().await;
            let current_id = *id;
            *id += 1;
            current_id
        };
        
        {
            let mut connections = self.connections.write().await;
            connections.insert(conn_id, ws_stream.clone());
        }
        
        println!("Connection {} established. Total: {}", 
                 conn_id, 
                 self.connections.read().await.len());

        // Handle messages
        let connections_clone = self.connections.clone();
        let result = Self::message_loop(ws_stream, conn_id, connections_clone).await;
        
        // Cleanup on disconnect
        {
            let mut connections = self.connections.write().await;
            connections.remove(&conn_id);
        }
        
        println!("Connection {} closed. Total: {}", 
                 conn_id,
                 self.connections.read().await.len());
        
        result
    }

    async fn message_loop(
        ws_stream: Arc<Mutex<tokio_tungstenite::WebSocketStream<TcpStream>>>,
        conn_id: ConnectionId,
        connections: Connections,
    ) -> Result<(), Box<dyn std::error::Error>> {
        loop {
            let msg = {
                let mut stream = ws_stream.lock().await;
                match stream.next().await {
                    Some(Ok(msg)) => msg,
                    Some(Err(e)) => {
                        eprintln!("Error receiving message: {}", e);
                        break;
                    }
                    None => break,
                }
            };

            match msg {
                Message::Text(text) => {
                    println!("Received from {}: {}", conn_id, text);
                    
                    // Echo back
                    let mut stream = ws_stream.lock().await;
                    stream.send(Message::Text(text)).await?;
                }
                Message::Binary(data) => {
                    let mut stream = ws_stream.lock().await;
                    stream.send(Message::Binary(data)).await?;
                }
                Message::Ping(data) => {
                    let mut stream = ws_stream.lock().await;
                    stream.send(Message::Pong(data)).await?;
                }
                Message::Close(_) => {
                    println!("Client {} initiated close", conn_id);
                    break;
                }
                _ => {}
            }
        }
        
        Ok(())
    }

    async fn graceful_shutdown(&self) {
        println!("Initiating graceful shutdown...");
        
        let connections = self.connections.read().await;
        for (id, ws_stream) in connections.iter() {
            println!("Closing connection {}", id);
            let mut stream = ws_stream.lock().await;
            let _ = stream.send(Message::Close(None)).await;
        }
        
        // Wait for connections to close
        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
        println!("Shutdown complete");
    }

    // Health check for Kubernetes probes
    async fn get_connection_count(&self) -> usize {
        self.connections.read().await.len()
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port = std::env::var("WS_PORT").unwrap_or_else(|_| "8080".to_string());
    let addr = format!("0.0.0.0:{}", port);
    
    let listener = TcpListener::bind(&addr).await?;
    println!("WebSocket server listening on: {}", addr);
    
    let server = Arc::new(WebSocketServer::new());
    let server_clone = server.clone();
    
    // Spawn graceful shutdown handler
    tokio::spawn(async move {
        signal::ctrl_c().await.expect("Failed to listen for SIGTERM");
        println!("Received shutdown signal");
        server_clone.graceful_shutdown().await;
        std::process::exit(0);
    });
    
    // Accept connections
    loop {
        let (stream, addr) = listener.accept().await?;
        let server_clone = server.clone();
        
        tokio::spawn(async move {
            if let Err(e) = server_clone.handle_connection(stream, addr).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }
}
```

**Cargo.toml:**

```toml
[package]
name = "k8s-websocket-server"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1.35", features = ["full"] }
tokio-tungstenite = "0.21"
futures-util = "0.3"
```

**Dockerfile for Rust WebSocket Server:**

```dockerfile
FROM rust:1.75 as builder
WORKDIR /app
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo build --release

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/target/release/k8s-websocket-server /usr/local/bin/
EXPOSE 8080
CMD ["k8s-websocket-server"]
```

## Kubernetes Manifests

**Deployment with PreStop Hook:**

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: websocket-server
spec:
  replicas: 3
  selector:
    matchLabels:
      app: websocket-server
  template:
    metadata:
      labels:
        app: websocket-server
    spec:
      containers:
      - name: websocket-server
        image: websocket-server:latest
        ports:
        - containerPort: 8080
          name: websocket
        env:
        - name: WS_PORT
          value: "8080"
        lifecycle:
          preStop:
            exec:
              command: ["/bin/sh", "-c", "sleep 15"]
        readinessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 5
          periodSeconds: 10
        livenessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 15
          periodSeconds: 20
        resources:
          requests:
            memory: "128Mi"
            cpu: "100m"
          limits:
            memory: "512Mi"
            cpu: "500m"
      terminationGracePeriodSeconds: 30
```

**Service with Session Affinity:**

```yaml
apiVersion: v1
kind: Service
metadata:
  name: websocket-service
spec:
  type: LoadBalancer
  sessionAffinity: ClientIP
  sessionAffinityConfig:
    clientIP:
      timeoutSeconds: 3600
  selector:
    app: websocket-server
  ports:
  - protocol: TCP
    port: 80
    targetPort: 8080
```

**Ingress for WebSocket (NGINX):**

```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: websocket-ingress
  annotations:
    nginx.ingress.kubernetes.io/proxy-read-timeout: "3600"
    nginx.ingress.kubernetes.io/proxy-send-timeout: "3600"
    nginx.ingress.kubernetes.io/websocket-services: "websocket-service"
    nginx.ingress.kubernetes.io/affinity: "cookie"
spec:
  ingressClassName: nginx
  rules:
  - host: ws.example.com
    http:
      paths:
      - path: /
        pathType: Prefix
        backend:
          service:
            name: websocket-service
            port:
              number: 80
```

## Summary

Deploying WebSocket services in Kubernetes requires careful consideration of connection lifecycle management, load balancing strategies, and graceful shutdown procedures. Key requirements include:

- **Graceful shutdown handling** with proper preStop hooks and termination grace periods
- **Session affinity** for load balancing to maintain connection persistence
- **Extended timeouts** in ingress controllers to accommodate long-lived connections
- **Proper health checks** that don't disrupt active connections
- **Signal handling** (SIGTERM) for coordinated pod termination

Both C/C++ and Rust implementations demonstrate production-ready patterns including connection tracking, graceful shutdown, and environment-based configuration suitable for containerized deployment. The Rust implementation particularly benefits from Tokio's async runtime for efficient handling of thousands of concurrent connections with minimal resource overhead.