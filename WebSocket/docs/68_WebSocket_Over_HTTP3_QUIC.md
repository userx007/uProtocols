# WebSocket Over HTTP/3 (QUIC)

## Overview

WebSocket over HTTP/3 represents the evolution of real-time bidirectional communication by leveraging QUIC (Quick UDP Internet Connections) as the transport protocol instead of TCP. HTTP/3 is built on QUIC, which runs over UDP and provides significant improvements over traditional TCP-based connections including reduced latency, better handling of packet loss, and built-in encryption.

## Key Concepts

### QUIC Transport Protocol
QUIC is a multiplexed transport protocol that provides:
- **0-RTT Connection Establishment**: Faster connection setup compared to TCP+TLS
- **Stream Multiplexing**: Multiple streams without head-of-line blocking
- **Connection Migration**: Maintains connections across network changes (WiFi to cellular)
- **Built-in Encryption**: TLS 1.3 integrated at the transport layer
- **Improved Loss Recovery**: Independent stream handling prevents blocking

### WebSocket over HTTP/3
The WebSocket protocol can be bootstrapped over HTTP/3 using the extended CONNECT method, similar to HTTP/2 but with QUIC's advantages:
- Uses HTTP/3 CONNECT method with `:protocol` pseudo-header set to `websocket`
- Maintains WebSocket framing over QUIC streams
- Benefits from QUIC's multiplexing and congestion control

## C/C++ Implementation

Here's an example using a hypothetical QUIC library (based on patterns from libraries like ngtcp2 or quiche):

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <quic/quic.h>

// WebSocket over HTTP/3 client structure
typedef struct {
    quic_connection_t *conn;
    quic_stream_t *ws_stream;
    int connected;
} ws_http3_client_t;

// WebSocket frame opcodes
#define WS_OPCODE_TEXT 0x01
#define WS_OPCODE_BINARY 0x02
#define WS_OPCODE_CLOSE 0x08
#define WS_OPCODE_PING 0x09
#define WS_OPCODE_PONG 0x0A

// Initialize WebSocket over HTTP/3 connection
int ws_http3_connect(ws_http3_client_t *client, const char *host, 
                      uint16_t port, const char *path) {
    quic_config_t config = {0};
    config.idle_timeout = 30000; // 30 seconds
    config.initial_max_streams_bidi = 100;
    
    // Create QUIC connection
    client->conn = quic_connection_new(host, port, &config);
    if (!client->conn) {
        fprintf(stderr, "Failed to create QUIC connection\n");
        return -1;
    }
    
    // Perform QUIC handshake (includes TLS 1.3)
    if (quic_connection_handshake(client->conn) < 0) {
        fprintf(stderr, "QUIC handshake failed\n");
        return -1;
    }
    
    // Create HTTP/3 stream for WebSocket upgrade
    client->ws_stream = quic_stream_new(client->conn, QUIC_STREAM_BIDI);
    if (!client->ws_stream) {
        fprintf(stderr, "Failed to create stream\n");
        return -1;
    }
    
    // Send HTTP/3 CONNECT request for WebSocket
    char headers[512];
    snprintf(headers, sizeof(headers),
             ":method: CONNECT\r\n"
             ":protocol: websocket\r\n"
             ":scheme: https\r\n"
             ":path: %s\r\n"
             ":authority: %s:%d\r\n"
             "sec-websocket-version: 13\r\n"
             "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
             path, host, port);
    
    if (quic_stream_send(client->ws_stream, (uint8_t*)headers, 
                         strlen(headers), 0) < 0) {
        fprintf(stderr, "Failed to send CONNECT request\n");
        return -1;
    }
    
    client->connected = 1;
    return 0;
}

// Send WebSocket frame over HTTP/3
int ws_http3_send(ws_http3_client_t *client, const char *data, 
                  size_t len, uint8_t opcode) {
    if (!client->connected) {
        return -1;
    }
    
    // Build WebSocket frame
    uint8_t frame[len + 14]; // Max header size
    size_t frame_len = 0;
    
    // First byte: FIN bit + opcode
    frame[frame_len++] = 0x80 | (opcode & 0x0F);
    
    // Payload length and mask bit
    if (len < 126) {
        frame[frame_len++] = 0x80 | len; // Masked
    } else if (len < 65536) {
        frame[frame_len++] = 0x80 | 126;
        frame[frame_len++] = (len >> 8) & 0xFF;
        frame[frame_len++] = len & 0xFF;
    } else {
        frame[frame_len++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            frame[frame_len++] = (len >> (i * 8)) & 0xFF;
        }
    }
    
    // Masking key
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xFF;
        frame[frame_len++] = mask[i];
    }
    
    // Masked payload
    for (size_t i = 0; i < len; i++) {
        frame[frame_len++] = data[i] ^ mask[i % 4];
    }
    
    // Send over QUIC stream
    return quic_stream_send(client->ws_stream, frame, frame_len, 0);
}

// Receive WebSocket frame over HTTP/3
int ws_http3_recv(ws_http3_client_t *client, uint8_t *buffer, 
                  size_t buffer_size, size_t *received) {
    if (!client->connected) {
        return -1;
    }
    
    // Read from QUIC stream (non-blocking)
    ssize_t n = quic_stream_recv(client->ws_stream, buffer, 
                                  buffer_size, 0);
    if (n < 0) {
        return -1;
    }
    
    *received = n;
    return 0;
}

// Close WebSocket connection
void ws_http3_close(ws_http3_client_t *client) {
    if (client->connected) {
        // Send WebSocket close frame
        uint8_t close_frame[] = {0x88, 0x00};
        quic_stream_send(client->ws_stream, close_frame, 2, 1);
        
        // Close stream and connection
        quic_stream_close(client->ws_stream);
        quic_connection_close(client->conn);
        
        client->connected = 0;
    }
}

// Example usage
int main() {
    ws_http3_client_t client = {0};
    
    if (ws_http3_connect(&client, "example.com", 443, "/ws") < 0) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }
    
    printf("WebSocket over HTTP/3 connected\n");
    
    // Send message
    const char *message = "Hello from HTTP/3!";
    ws_http3_send(&client, message, strlen(message), WS_OPCODE_TEXT);
    
    // Receive response
    uint8_t buffer[4096];
    size_t received;
    if (ws_http3_recv(&client, buffer, sizeof(buffer), &received) == 0) {
        printf("Received %zu bytes\n", received);
    }
    
    ws_http3_close(&client);
    return 0;
}
```

## Rust Implementation

Rust has excellent support for async/await and QUIC through libraries like `quinn`:

```rust
use quinn::{Endpoint, ClientConfig, Connection};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use rustls::crypto::aws_lc_rs;
use std::sync::Arc;
use std::error::Error;

// WebSocket frame opcodes
const WS_OPCODE_TEXT: u8 = 0x01;
const WS_OPCODE_BINARY: u8 = 0x02;
const WS_OPCODE_CLOSE: u8 = 0x08;
const WS_OPCODE_PING: u8 = 0x09;
const WS_OPCODE_PONG: u8 = 0x0A;

struct WebSocketHttp3Client {
    connection: Connection,
    send_stream: Option<quinn::SendStream>,
    recv_stream: Option<quinn::RecvStream>,
}

impl WebSocketHttp3Client {
    async fn connect(host: &str, port: u16, path: &str) 
        -> Result<Self, Box<dyn Error>> {
        // Configure QUIC client with TLS
        let mut roots = rustls::RootCertStore::empty();
        roots.extend(webpki_roots::TLS_SERVER_ROOTS.iter().cloned());
        
        let crypto = rustls::ClientConfig::builder()
            .with_root_certificates(roots)
            .with_no_client_auth();
        
        let client_config = ClientConfig::new(Arc::new(
            quinn::crypto::rustls::QuicClientConfig::try_from(crypto)?
        ));
        
        // Create endpoint and connect
        let mut endpoint = Endpoint::client("0.0.0.0:0".parse()?)?;
        endpoint.set_default_client_config(client_config);
        
        let connection = endpoint
            .connect(format!("{}:{}", host, port).parse()?, host)?
            .await?;
        
        println!("QUIC connection established");
        
        // Open bidirectional stream for WebSocket
        let (mut send, recv) = connection.open_bi().await?;
        
        // Send HTTP/3 CONNECT request for WebSocket upgrade
        let request = format!(
            ":method: CONNECT\r\n\
             :protocol: websocket\r\n\
             :scheme: https\r\n\
             :path: {}\r\n\
             :authority: {}:{}\r\n\
             sec-websocket-version: 13\r\n\
             sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
            path, host, port
        );
        
        send.write_all(request.as_bytes()).await?;
        
        // Wait for upgrade response (simplified)
        // In production, parse HTTP/3 headers properly
        
        Ok(Self {
            connection,
            send_stream: Some(send),
            recv_stream: Some(recv),
        })
    }
    
    async fn send_message(&mut self, data: &[u8], opcode: u8) 
        -> Result<(), Box<dyn Error>> {
        let send_stream = self.send_stream.as_mut()
            .ok_or("Send stream not available")?;
        
        // Build WebSocket frame
        let mut frame = Vec::new();
        
        // First byte: FIN (1) + RSV (000) + opcode
        frame.push(0x80 | (opcode & 0x0F));
        
        // Second byte: MASK (1) + payload length
        let len = data.len();
        if len < 126 {
            frame.push(0x80 | len as u8);
        } else if len < 65536 {
            frame.push(0x80 | 126);
            frame.extend_from_slice(&(len as u16).to_be_bytes());
        } else {
            frame.push(0x80 | 127);
            frame.extend_from_slice(&(len as u64).to_be_bytes());
        }
        
        // Masking key (random 4 bytes)
        let mask: [u8; 4] = rand::random();
        frame.extend_from_slice(&mask);
        
        // Masked payload
        for (i, byte) in data.iter().enumerate() {
            frame.push(byte ^ mask[i % 4]);
        }
        
        // Send over QUIC stream
        send_stream.write_all(&frame).await?;
        
        Ok(())
    }
    
    async fn receive_message(&mut self) -> Result<Vec<u8>, Box<dyn Error>> {
        let recv_stream = self.recv_stream.as_mut()
            .ok_or("Receive stream not available")?;
        
        // Read WebSocket frame header
        let mut header = [0u8; 2];
        recv_stream.read_exact(&mut header).await?;
        
        let fin = (header[0] & 0x80) != 0;
        let opcode = header[0] & 0x0F;
        let masked = (header[1] & 0x80) != 0;
        let mut payload_len = (header[1] & 0x7F) as usize;
        
        // Extended payload length
        if payload_len == 126 {
            let mut len_bytes = [0u8; 2];
            recv_stream.read_exact(&mut len_bytes).await?;
            payload_len = u16::from_be_bytes(len_bytes) as usize;
        } else if payload_len == 127 {
            let mut len_bytes = [0u8; 8];
            recv_stream.read_exact(&mut len_bytes).await?;
            payload_len = u64::from_be_bytes(len_bytes) as usize;
        }
        
        // Read masking key if present
        let mask = if masked {
            let mut mask_bytes = [0u8; 4];
            recv_stream.read_exact(&mut mask_bytes).await?;
            Some(mask_bytes)
        } else {
            None
        };
        
        // Read payload
        let mut payload = vec![0u8; payload_len];
        recv_stream.read_exact(&mut payload).await?;
        
        // Unmask if necessary
        if let Some(mask_key) = mask {
            for (i, byte) in payload.iter_mut().enumerate() {
                *byte ^= mask_key[i % 4];
            }
        }
        
        Ok(payload)
    }
    
    async fn close(mut self) -> Result<(), Box<dyn Error>> {
        // Send WebSocket close frame
        let close_frame = vec![0x88, 0x00];
        if let Some(mut send) = self.send_stream {
            send.write_all(&close_frame).await?;
            send.finish()?;
        }
        
        // Close QUIC connection gracefully
        self.connection.close(0u32.into(), b"WebSocket closed");
        
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Connect to WebSocket over HTTP/3
    let mut client = WebSocketHttp3Client::connect(
        "example.com", 
        443, 
        "/ws"
    ).await?;
    
    println!("WebSocket over HTTP/3 connected!");
    
    // Send text message
    client.send_message(
        b"Hello from Rust over HTTP/3!", 
        WS_OPCODE_TEXT
    ).await?;
    
    // Receive response
    let response = client.receive_message().await?;
    println!("Received: {}", String::from_utf8_lossy(&response));
    
    // Send ping
    client.send_message(b"", WS_OPCODE_PING).await?;
    
    // Receive pong
    let pong = client.receive_message().await?;
    println!("Received pong");
    
    // Close connection
    client.close().await?;
    
    Ok(())
}
```

## Summary

**WebSocket over HTTP/3 (QUIC)** represents the next evolution of real-time web communication, combining WebSocket's bidirectional messaging with QUIC's modern transport advantages:

**Key Benefits:**
- **Faster connection establishment** with 0-RTT and 1-RTT handshakes
- **Elimination of head-of-line blocking** through independent stream handling
- **Better mobile experience** with connection migration across networks
- **Improved security** with mandatory TLS 1.3 encryption
- **Superior loss recovery** without blocking unaffected streams

**Implementation Considerations:**
- Uses HTTP/3 extended CONNECT method with `:protocol: websocket` header
- Maintains standard WebSocket framing over QUIC streams
- Requires QUIC-capable libraries (quinn for Rust, quiche/ngtcp2 for C/C++)
- Browser and server support still evolving (experimental in most platforms)

**Use Cases:**
- Real-time gaming with low latency requirements
- Mobile applications requiring resilient connections
- Multiplexed communication channels without blocking
- Applications needing fast reconnection after network changes

WebSocket over HTTP/3 is particularly valuable for applications where connection reliability, latency, and mobile performance are critical. As HTTP/3 adoption grows, it will likely become the standard transport for real-time web applications.