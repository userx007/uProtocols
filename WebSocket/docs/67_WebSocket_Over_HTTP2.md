# WebSocket Over HTTP/2: Detailed Description and Programming Guide

## Overview

WebSocket Over HTTP/2 refers to the technique of running WebSocket-style bidirectional communication over HTTP/2 connections, rather than the traditional HTTP/1.1 upgrade mechanism. This approach leverages HTTP/2's multiplexing capabilities and stream-based architecture to provide similar real-time, full-duplex communication while benefiting from HTTP/2's features.

## Key Concepts

### Traditional WebSocket (HTTP/1.1)
- Uses HTTP Upgrade mechanism to switch protocols
- Dedicated TCP connection per WebSocket
- Cannot multiplex multiple WebSockets over single connection
- Simple handshake: `Connection: Upgrade` and `Upgrade: websocket`

### WebSocket Over HTTP/2
- Uses HTTP/2 streams as transport layer
- Multiple bidirectional streams over single TCP connection
- Leverages HTTP/2 multiplexing, prioritization, and flow control
- Can use Extended CONNECT method (RFC 8441) for WebSocket bootstrapping
- Maintains compatibility with existing WebSocket APIs

## Benefits

1. **Connection Efficiency**: Multiplexes multiple WebSocket-style connections over a single TCP connection
2. **Better Resource Usage**: Reduces overhead of maintaining multiple TCP connections
3. **Improved Performance**: Benefits from HTTP/2's header compression (HPACK)
4. **Stream Prioritization**: Can prioritize different data streams
5. **Flow Control**: Built-in HTTP/2 flow control mechanisms

## C/C++ Implementation Example

Here's a practical example using libcurl for HTTP/2 WebSocket connections:

```c
#include <stdio.h>
#include <curl/curl.h>
#include <string.h>

// Callback for receiving data
size_t websocket_receive_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    printf("Received: %.*s\n", (int)realsize, (char*)contents);
    return realsize;
}

// Function to establish WebSocket over HTTP/2
int connect_websocket_http2(const char *url) {
    CURL *curl;
    CURLcode res;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        // Enable HTTP/2
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        // Configure for WebSocket-like behavior
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Connection: Upgrade");
        headers = curl_slist_append(headers, "Upgrade: websocket");
        headers = curl_slist_append(headers, "Sec-WebSocket-Version: 13");
        headers = curl_slist_append(headers, "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==");
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Set receive callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, websocket_receive_callback);
        
        // Enable verbose output for debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Perform the connection
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "Connection failed: %s\n", curl_easy_strerror(res));
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    curl_global_cleanup();
    return 0;
}

int main(void) {
    connect_websocket_http2("https://example.com/websocket");
    return 0;
}
```

### Lower-Level C++ Implementation with nghttp2

```cpp
#include <nghttp2/nghttp2.h>
#include <iostream>
#include <memory>
#include <string>

class HTTP2WebSocket {
private:
    nghttp2_session *session;
    int32_t stream_id;
    
    // Callback when data is received
    static ssize_t data_source_read_callback(
        nghttp2_session *session, int32_t stream_id,
        uint8_t *buf, size_t length, uint32_t *data_flags,
        nghttp2_data_source *source, void *user_data) {
        
        // Read data from application buffer
        auto *ws = static_cast<HTTP2WebSocket*>(user_data);
        // Implementation would read from internal buffer
        return 0; // Return bytes read
    }
    
    static int on_frame_recv_callback(
        nghttp2_session *session, const nghttp2_frame *frame,
        void *user_data) {
        
        auto *ws = static_cast<HTTP2WebSocket*>(user_data);
        
        if (frame->hd.type == NGHTTP2_DATA) {
            std::cout << "Received DATA frame on stream " 
                      << frame->hd.stream_id << std::endl;
        }
        return 0;
    }
    
public:
    HTTP2WebSocket() : session(nullptr), stream_id(-1) {}
    
    bool initialize() {
        nghttp2_session_callbacks *callbacks;
        nghttp2_session_callbacks_new(&callbacks);
        
        nghttp2_session_callbacks_set_on_frame_recv_callback(
            callbacks, on_frame_recv_callback);
        
        // Create client session
        int rv = nghttp2_session_client_new(&session, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);
        
        return rv == 0;
    }
    
    int32_t connect(const std::string& path) {
        // Prepare CONNECT method headers for WebSocket
        nghttp2_nv hdrs[] = {
            {(uint8_t*)":method", (uint8_t*)"CONNECT", 7, 7, 
             NGHTTP2_NV_FLAG_NONE},
            {(uint8_t*)":protocol", (uint8_t*)"websocket", 9, 9, 
             NGHTTP2_NV_FLAG_NONE},
            {(uint8_t*)":scheme", (uint8_t*)"https", 7, 5, 
             NGHTTP2_NV_FLAG_NONE},
            {(uint8_t*)":path", (uint8_t*)path.c_str(), 5, path.length(), 
             NGHTTP2_NV_FLAG_NONE},
            {(uint8_t*)"sec-websocket-version", (uint8_t*)"13", 21, 2, 
             NGHTTP2_NV_FLAG_NONE}
        };
        
        // Submit CONNECT request
        stream_id = nghttp2_submit_request(
            session, nullptr, hdrs, sizeof(hdrs)/sizeof(hdrs[0]),
            nullptr, this);
        
        return stream_id;
    }
    
    void send_message(const std::string& message) {
        nghttp2_data_provider data_prd;
        data_prd.source.ptr = (void*)message.c_str();
        data_prd.read_callback = data_source_read_callback;
        
        nghttp2_submit_data(session, NGHTTP2_FLAG_END_STREAM, 
                           stream_id, &data_prd);
    }
    
    void run() {
        // Send pending frames
        nghttp2_session_send(session);
    }
    
    ~HTTP2WebSocket() {
        if (session) {
            nghttp2_session_del(session);
        }
    }
};

int main() {
    HTTP2WebSocket ws;
    
    if (ws.initialize()) {
        ws.connect("/chat");
        ws.send_message("Hello via HTTP/2 WebSocket!");
        ws.run();
    }
    
    return 0;
}
```

## Rust Implementation Example

### Using `hyper` and `tokio` for HTTP/2 WebSocket

```rust
use hyper::{Client, Body, Request, Method};
use hyper::client::HttpConnector;
use hyper_tls::HttpsConnector;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Create HTTPS connector
    let https = HttpsConnector::new();
    let client = Client::builder()
        .http2_only(true)
        .build::<_, Body>(https);
    
    // Create WebSocket upgrade request using Extended CONNECT
    let req = Request::builder()
        .method(Method::CONNECT)
        .uri("https://example.com/websocket")
        .header("Connection", "Upgrade")
        .header("Upgrade", "websocket")
        .header("Sec-WebSocket-Version", "13")
        .header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==")
        .header(":protocol", "websocket")
        .body(Body::empty())?;
    
    let resp = client.request(req).await?;
    
    println!("Response status: {}", resp.status());
    
    // Upgrade to WebSocket
    if resp.status() == 200 {
        let upgraded = hyper::upgrade::on(resp).await?;
        println!("WebSocket connection established over HTTP/2");
        
        // Use the upgraded connection
        handle_websocket(upgraded).await?;
    }
    
    Ok(())
}

async fn handle_websocket(mut stream: hyper::upgrade::Upgraded) -> Result<(), Box<dyn Error>> {
    // Send a message
    let message = b"Hello from HTTP/2 WebSocket!";
    stream.write_all(message).await?;
    
    // Read response
    let mut buffer = vec![0u8; 1024];
    let n = stream.read(&mut buffer).await?;
    
    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
    
    Ok(())
}
```

### Using `h2` crate for lower-level control

```rust
use h2::client;
use tokio::net::TcpStream;
use tokio_rustls::{TlsConnector, rustls};
use http::{Request, Method};
use bytes::Bytes;
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Establish TLS connection
    let addr = "example.com:443";
    let tcp = TcpStream::connect(addr).await?;
    
    let mut config = rustls::ClientConfig::new();
    config.alpn_protocols = vec![b"h2".to_vec()];
    
    let connector = TlsConnector::from(std::sync::Arc::new(config));
    let domain = rustls::ServerName::try_from("example.com")?;
    let tls_stream = connector.connect(domain, tcp).await?;
    
    // Create HTTP/2 connection
    let (mut client, h2_conn) = client::handshake(tls_stream).await?;
    
    // Spawn connection handler
    tokio::spawn(async move {
        if let Err(e) = h2_conn.await {
            eprintln!("Connection error: {}", e);
        }
    });
    
    // Wait for the client to be ready
    client.ready().await?;
    
    // Create CONNECT request for WebSocket
    let request = Request::builder()
        .method(Method::CONNECT)
        .uri("/websocket")
        .header(":protocol", "websocket")
        .header("sec-websocket-version", "13")
        .header("sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ==")
        .body(())?;
    
    // Send request and get response
    let (response, mut send_stream) = client.send_request(request, false)?;
    
    // Handle response
    tokio::spawn(async move {
        let response = response.await.unwrap();
        println!("Response status: {}", response.status());
        
        let mut body = response.into_body();
        while let Some(chunk) = body.data().await {
            let chunk = chunk.unwrap();
            println!("Received: {:?}", chunk);
            
            // Release flow control
            let _ = body.flow_control().release_capacity(chunk.len());
        }
    });
    
    // Send WebSocket message
    send_stream.send_data(Bytes::from("Hello HTTP/2 WebSocket!"), false)?;
    
    // Keep connection alive
    tokio::time::sleep(tokio::time::Duration::from_secs(10)).await;
    
    Ok(())
}
```

### High-level Rust implementation using `tokio-tungstenite` with HTTP/2

```rust
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use url::Url;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let url = Url::parse("wss://example.com/websocket")?;
    
    // Connect with HTTP/2 enabled (requires server support)
    let (ws_stream, response) = connect_async(url).await?;
    
    println!("Connected! Response: {:?}", response);
    
    let (mut write, mut read) = ws_stream.split();
    
    // Send message
    write.send(Message::Text("Hello via WebSocket over HTTP/2!".into())).await?;
    
    // Receive messages
    while let Some(message) = read.next().await {
        match message? {
            Message::Text(text) => println!("Received text: {}", text),
            Message::Binary(bin) => println!("Received binary: {} bytes", bin.len()),
            Message::Close(_) => {
                println!("Connection closed");
                break;
            }
            _ => {}
        }
    }
    
    Ok(())
}
```

## Summary

**WebSocket Over HTTP/2** modernizes real-time bidirectional communication by leveraging HTTP/2's multiplexing and stream-based architecture instead of HTTP/1.1's upgrade mechanism. Key advantages include efficient connection pooling (multiple WebSocket streams over one TCP connection), improved resource utilization, and access to HTTP/2 features like stream prioritization and flow control.

The Extended CONNECT method (RFC 8441) enables WebSocket bootstrapping over HTTP/2 by using the `:protocol` pseudo-header with value "websocket". Implementation approaches range from high-level libraries (curl, tokio-tungstenite) to low-level HTTP/2 libraries (nghttp2, h2 crate) that provide fine-grained control over streams and frames.

While offering significant benefits for applications with many concurrent WebSocket connections, adoption requires both client and server support for HTTP/2 Extended CONNECT. The technology is particularly valuable in microservices architectures, IoT platforms, and real-time collaborative applications where connection efficiency is critical.