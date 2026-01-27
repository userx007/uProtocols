# Reverse Proxy Configuration for WebSockets

## Overview

A reverse proxy sits between clients and WebSocket servers, forwarding connections while providing benefits like load balancing, SSL termination, security filtering, and centralized routing. However, WebSockets require special configuration because they use the HTTP Upgrade mechanism to switch from HTTP to the WebSocket protocol.

## Key Concepts

### The WebSocket Upgrade Process

WebSockets start as HTTP requests with special headers:
- `Connection: Upgrade`
- `Upgrade: websocket`
- `Sec-WebSocket-Key`, `Sec-WebSocket-Version`, etc.

The server responds with a 101 status code, and the connection becomes a bidirectional WebSocket. Reverse proxies must properly handle this upgrade and maintain the long-lived connection.

### Critical Proxy Requirements

1. **Preserve Upgrade Headers**: Forward `Upgrade` and `Connection` headers
2. **Disable Buffering**: Prevent response buffering for real-time communication
3. **Connection Timeouts**: Set appropriate timeouts for long-lived connections
4. **HTTP/1.1**: WebSocket upgrade requires HTTP/1.1

---

## Nginx Configuration

### Basic WebSocket Proxy

```nginx
http {
    # Define upstream WebSocket servers
    upstream websocket_backend {
        server 127.0.0.1:8080;
        server 127.0.0.1:8081;
        # Add more backend servers for load balancing
    }

    # Map to handle Connection header upgrade
    map $http_upgrade $connection_upgrade {
        default upgrade;
        '' close;
    }

    server {
        listen 80;
        server_name ws.example.com;

        location /ws {
            # Proxy to upstream
            proxy_pass http://websocket_backend;
            
            # WebSocket-specific headers
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection $connection_upgrade;
            
            # Forward client information
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
            
            # Timeout settings
            proxy_connect_timeout 7d;
            proxy_send_timeout 7d;
            proxy_read_timeout 7d;
        }
    }
}
```

### SSL/TLS Configuration (wss://)

```nginx
server {
    listen 443 ssl http2;
    server_name ws.example.com;

    ssl_certificate /etc/nginx/ssl/cert.pem;
    ssl_certificate_key /etc/nginx/ssl/key.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;

    location /ws {
        proxy_pass http://websocket_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $connection_upgrade;
        
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
        
        proxy_read_timeout 86400;
    }
}
```

---

## HAProxy Configuration

### Basic WebSocket Setup

```haproxy
global
    log /dev/log local0
    maxconn 4096
    
defaults
    log global
    mode http
    option httplog
    timeout connect 5000ms
    timeout client 50000ms
    timeout server 50000ms

frontend websocket_front
    bind *:80
    bind *:443 ssl crt /etc/haproxy/certs/ws.example.com.pem
    
    # ACL to detect WebSocket upgrade
    acl is_websocket hdr(Upgrade) -i websocket
    acl is_websocket_path path_beg /ws
    
    # Route WebSocket traffic
    use_backend websocket_back if is_websocket is_websocket_path
    default_backend web_back

backend websocket_back
    balance roundrobin
    
    # WebSocket-specific timeouts
    timeout tunnel 3600s
    timeout server 3600s
    
    # Health check
    option httpchk GET /health
    http-check expect status 200
    
    # Backend servers
    server ws1 127.0.0.1:8080 check
    server ws2 127.0.0.1:8081 check
    server ws3 127.0.0.1:8082 check backup

backend web_back
    balance roundrobin
    server web1 127.0.0.1:3000 check
```

### Advanced HAProxy with Sticky Sessions

```haproxy
backend websocket_back
    balance roundrobin
    
    # Sticky sessions based on source IP
    stick-table type ip size 100k expire 30m
    stick on src
    
    # Or use cookie-based stickiness
    cookie SERVERID insert indirect nocache
    
    timeout tunnel 3600s
    timeout server 3600s
    
    server ws1 127.0.0.1:8080 check cookie ws1
    server ws2 127.0.0.1:8081 check cookie ws2
```

---

## Traefik Configuration

### Dynamic Configuration (YAML)

```yaml
# traefik.yml (static configuration)
entryPoints:
  web:
    address: ":80"
  websecure:
    address: ":443"

providers:
  file:
    filename: "/etc/traefik/dynamic.yml"

# dynamic.yml (dynamic configuration)
http:
  routers:
    websocket-router:
      rule: "Host(`ws.example.com`) && PathPrefix(`/ws`)"
      service: websocket-service
      entryPoints:
        - websecure
      tls:
        certResolver: letsencrypt

  services:
    websocket-service:
      loadBalancer:
        servers:
          - url: "http://127.0.0.1:8080"
          - url: "http://127.0.0.1:8081"
        sticky:
          cookie:
            name: server_id
            secure: true
            httpOnly: true
```

### Docker Compose with Traefik

```yaml
version: '3.8'

services:
  traefik:
    image: traefik:v2.10
    command:
      - "--api.insecure=true"
      - "--providers.docker=true"
      - "--entrypoints.websecure.address=:443"
    ports:
      - "443:443"
      - "8080:8080"
    volumes:
      - "/var/run/docker.sock:/var/run/docker.sock:ro"

  websocket-server:
    image: my-websocket-app
    labels:
      - "traefik.enable=true"
      - "traefik.http.routers.ws.rule=Host(`ws.example.com`)"
      - "traefik.http.routers.ws.entrypoints=websecure"
      - "traefik.http.services.ws.loadbalancer.sticky.cookie=true"
    deploy:
      replicas: 3
```

---

## C/C++ Client Example (with Proxy)

Using libwebsockets with proxy support:

```c
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>

static int callback_websocket(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connected to WebSocket server via proxy\n");
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf("Received: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            unsigned char buf[LWS_PRE + 256];
            unsigned char *p = &buf[LWS_PRE];
            size_t n = sprintf((char *)p, "Hello from client!");
            lws_write(wsi, p, n, LWS_WRITE_TEXT);
            break;
        }
            
        case LWS_CALLBACK_CLOSED:
            printf("Connection closed\n");
            break;
            
        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "ws-protocol", callback_websocket, 0, 1024 },
    { NULL, NULL, 0, 0 }
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    // Connect through reverse proxy
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "ws.example.com";  // Proxy address
    ccinfo.port = 443;
    ccinfo.path = "/ws";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL;
    
    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        fprintf(stderr, "Failed to connect\n");
        lws_context_destroy(context);
        return 1;
    }
    
    // Event loop
    while (lws_service(context, 50) >= 0) {
        // Service the connection
    }
    
    lws_context_destroy(context);
    return 0;
}
```

---

## Rust Client Example (with Proxy)

Using `tokio-tungstenite` with `hyper-proxy`:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use url::Url;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to WebSocket through reverse proxy
    let url = Url::parse("wss://ws.example.com/ws")?;
    
    println!("Connecting to {}", url);
    
    let (ws_stream, response) = connect_async(url).await?;
    println!("Connected! Response: {:?}", response);
    
    let (mut write, mut read) = ws_stream.split();
    
    // Spawn a task to send messages
    let write_task = tokio::spawn(async move {
        for i in 0..5 {
            let msg = format!("Message #{}", i);
            println!("Sending: {}", msg);
            
            if let Err(e) = write.send(Message::Text(msg)).await {
                eprintln!("Error sending message: {}", e);
                break;
            }
            
            tokio::time::sleep(tokio::time::Duration::from_secs(2)).await;
        }
        
        // Close connection gracefully
        let _ = write.send(Message::Close(None)).await;
    });
    
    // Receive messages
    let read_task = tokio::spawn(async move {
        while let Some(message) = read.next().await {
            match message {
                Ok(Message::Text(text)) => {
                    println!("Received: {}", text);
                }
                Ok(Message::Binary(data)) => {
                    println!("Received binary: {} bytes", data.len());
                }
                Ok(Message::Close(_)) => {
                    println!("Server closed connection");
                    break;
                }
                Ok(Message::Ping(_)) => {
                    println!("Received ping");
                }
                Ok(Message::Pong(_)) => {
                    println!("Received pong");
                }
                Err(e) => {
                    eprintln!("Error reading message: {}", e);
                    break;
                }
                _ => {}
            }
        }
    });
    
    // Wait for both tasks
    let _ = tokio::join!(write_task, read_task);
    
    Ok(())
}
```

### Rust Server Behind Proxy

```rust
use tokio::net::TcpListener;
use tokio_tungstenite::accept_async;
use futures_util::{StreamExt, SinkExt};
use tokio_tungstenite::tungstenite::Message;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on 127.0.0.1:8080");
    println!("Expects to be behind a reverse proxy");
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        
        tokio::spawn(async move {
            // Check for proxy headers (X-Forwarded-For, X-Real-IP)
            let ws_stream = match accept_async(stream).await {
                Ok(ws) => ws,
                Err(e) => {
                    eprintln!("WebSocket handshake error: {}", e);
                    return;
                }
            };
            
            println!("WebSocket connection established with {}", addr);
            
            let (mut write, mut read) = ws_stream.split();
            
            // Echo server
            while let Some(message) = read.next().await {
                match message {
                    Ok(Message::Text(text)) => {
                        println!("Received from {}: {}", addr, text);
                        if let Err(e) = write.send(Message::Text(text)).await {
                            eprintln!("Error sending message: {}", e);
                            break;
                        }
                    }
                    Ok(Message::Binary(data)) => {
                        if let Err(e) = write.send(Message::Binary(data)).await {
                            eprintln!("Error sending binary: {}", e);
                            break;
                        }
                    }
                    Ok(Message::Close(_)) => {
                        println!("Client {} closed connection", addr);
                        break;
                    }
                    Err(e) => {
                        eprintln!("WebSocket error with {}: {}", addr, e);
                        break;
                    }
                    _ => {}
                }
            }
        });
    }
    
    Ok(())
}
```

---

## Summary

**Reverse proxies are essential for production WebSocket deployments**, providing:

- **Load Balancing**: Distribute connections across multiple backend servers
- **SSL/TLS Termination**: Handle encryption at the proxy layer
- **Security**: Centralized access control, rate limiting, and DDoS protection
- **Routing**: Path-based routing to different WebSocket services
- **Monitoring**: Centralized logging and metrics collection

**Key configuration points:**
- Always set `proxy_http_version 1.1` (Nginx) or ensure HTTP/1.1 is used
- Forward `Upgrade` and `Connection` headers properly
- Configure long timeouts (hours or days) for persistent connections
- Implement health checks for backend servers
- Consider sticky sessions for stateful applications
- Use SSL/TLS (wss://) in production for security

**Popular choices:**
- **Nginx**: Most widely used, excellent performance, rich ecosystem
- **HAProxy**: Superior load balancing features, TCP-level capabilities
- **Traefik**: Best for containerized environments, automatic service discovery

The code examples show how clients connect through proxies transparently—the proxy handles the upgrade mechanism, and clients connect to the proxy's address as if it were the actual WebSocket server.