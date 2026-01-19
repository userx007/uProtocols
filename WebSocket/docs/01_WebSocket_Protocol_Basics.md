# WebSocket Protocol: A Comprehensive Guide

## Introduction

WebSocket is a communication protocol that provides full-duplex communication channels over a single TCP connection. Unlike the traditional HTTP request-response model, WebSocket enables persistent, bidirectional communication between client and server, making it ideal for real-time applications like chat systems, live notifications, gaming, and financial trading platforms.

## WebSocket Protocol Basics

### 1. **Core Concepts**

WebSocket operates on top of TCP and begins its life as an HTTP connection that gets "upgraded" to a WebSocket connection through a handshake process. Once established, both client and server can send data at any time without the overhead of HTTP headers for each message.

**Key Characteristics:**
- Full-duplex bidirectional communication
- Persistent connection (no need to reconnect for each message)
- Low latency and overhead compared to HTTP polling
- Operates over standard HTTP ports (80 for ws://, 443 for wss://)
- Uses a lightweight framing protocol for messages

### 2. **The WebSocket Handshake**

The handshake is an HTTP Upgrade request that transitions the connection from HTTP to WebSocket protocol.

**Client Handshake Request:**
```
GET /chat HTTP/1.1
Host: example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
Origin: http://example.com
```

**Server Handshake Response:**
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

The `Sec-WebSocket-Key` is a base64-encoded random value. The server concatenates this with a magic string `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`, hashes it with SHA-1, and base64-encodes the result to produce `Sec-WebSocket-Accept`.

### 3. **Frame Structure**

After the handshake, data is transmitted in frames. Each frame has a specific structure:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data continued ...                |
+---------------------------------------------------------------+
```

**Key Frame Components:**
- **FIN bit**: Indicates if this is the final fragment of a message
- **Opcode**: Defines the frame type (text=0x1, binary=0x2, close=0x8, ping=0x9, pong=0xA)
- **MASK bit**: Indicates if payload is masked (required for client-to-server frames)
- **Payload length**: 7 bits, 7+16 bits, or 7+64 bits depending on data size

### 4. **WebSocket vs HTTP**

| Aspect | HTTP | WebSocket |
|--------|------|-----------|
| Communication | Half-duplex (request-response) | Full-duplex (bidirectional) |
| Connection | Stateless, closes after response | Persistent, stays open |
| Overhead | High (headers with each request) | Low (small frame overhead) |
| Latency | Higher (connection setup each time) | Lower (connection always ready) |
| Use Case | Document retrieval, APIs | Real-time data, live updates |

---

## Code Examples

### C Implementation (Server)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Base64 encode function
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *buffer_ptr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    char* encoded = (char*)malloc(buffer_ptr->length + 1);
    memcpy(encoded, buffer_ptr->data, buffer_ptr->length);
    encoded[buffer_ptr->length] = '\0';
    
    BIO_free_all(bio);
    return encoded;
}

// Generate WebSocket accept key
char* generate_accept_key(const char* client_key) {
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", client_key, MAGIC_STRING);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)combined, strlen(combined), hash);
    
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

// Parse WebSocket key from handshake
char* extract_websocket_key(const char* request) {
    const char* key_header = "Sec-WebSocket-Key: ";
    char* key_start = strstr(request, key_header);
    
    if (!key_start) return NULL;
    
    key_start += strlen(key_header);
    char* key_end = strstr(key_start, "\r\n");
    
    if (!key_end) return NULL;
    
    int key_length = key_end - key_start;
    char* key = (char*)malloc(key_length + 1);
    strncpy(key, key_start, key_length);
    key[key_length] = '\0';
    
    return key;
}

// Send WebSocket handshake response
void send_handshake_response(int client_socket, const char* accept_key) {
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_key);
    
    send(client_socket, response, strlen(response), 0);
    printf("WebSocket handshake completed\n");
}

// Decode WebSocket frame
int decode_frame(unsigned char* buffer, int length, char* output) {
    if (length < 2) return -1;
    
    unsigned char opcode = buffer[0] & 0x0F;
    unsigned char masked = buffer[1] & 0x80;
    uint64_t payload_length = buffer[1] & 0x7F;
    
    int pos = 2;
    
    // Extended payload length
    if (payload_length == 126) {
        payload_length = (buffer[2] << 8) | buffer[3];
        pos = 4;
    } else if (payload_length == 127) {
        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length = (payload_length << 8) | buffer[2 + i];
        }
        pos = 10;
    }
    
    // Extract masking key
    unsigned char mask[4];
    if (masked) {
        memcpy(mask, buffer + pos, 4);
        pos += 4;
    }
    
    // Decode payload
    for (uint64_t i = 0; i < payload_length; i++) {
        output[i] = masked ? (buffer[pos + i] ^ mask[i % 4]) : buffer[pos + i];
    }
    output[payload_length] = '\0';
    
    return payload_length;
}

// Encode and send WebSocket frame
void send_frame(int client_socket, const char* message, int length) {
    unsigned char frame[10 + length];
    int frame_size = 0;
    
    // FIN bit set, opcode 0x1 (text)
    frame[0] = 0x81;
    
    if (length <= 125) {
        frame[1] = length;
        frame_size = 2;
    } else if (length <= 65535) {
        frame[1] = 126;
        frame[2] = (length >> 8) & 0xFF;
        frame[3] = length & 0xFF;
        frame_size = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (length >> (56 - 8*i)) & 0xFF;
        }
        frame_size = 10;
    }
    
    memcpy(frame + frame_size, message, length);
    send(client_socket, frame, frame_size + length, 0);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    // Listen for connections
    listen(server_socket, 5);
    printf("WebSocket server listening on port %d\n", PORT);
    
    // Accept client connection
    client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (client_socket < 0) {
        perror("Accept failed");
        return 1;
    }
    
    // Receive handshake request
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    buffer[bytes_received] = '\0';
    
    // Extract and generate keys
    char* client_key = extract_websocket_key(buffer);
    if (!client_key) {
        printf("Invalid WebSocket handshake\n");
        close(client_socket);
        return 1;
    }
    
    char* accept_key = generate_accept_key(client_key);
    send_handshake_response(client_socket, accept_key);
    
    free(client_key);
    free(accept_key);
    
    // Handle WebSocket frames
    while (1) {
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) break;
        
        char decoded_message[BUFFER_SIZE];
        int payload_length = decode_frame((unsigned char*)buffer, bytes_received, decoded_message);
        
        if (payload_length > 0) {
            printf("Received: %s\n", decoded_message);
            
            // Echo back
            char response[256];
            snprintf(response, sizeof(response), "Echo: %s", decoded_message);
            send_frame(client_socket, response, strlen(response));
        }
    }
    
    close(client_socket);
    close(server_socket);
    return 0;
}
```

### C++ Implementation (Client)

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

class WebSocketClient {
private:
    int sock;
    std::string host;
    int port;
    
    std::string base64_encode(const unsigned char* input, int length) {
        BIO *bio, *b64;
        BUF_MEM *buffer_ptr;
        
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input, length);
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &buffer_ptr);
        
        std::string result(buffer_ptr->data, buffer_ptr->length);
        BIO_free_all(bio);
        return result;
    }
    
    std::string generate_random_key() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        unsigned char random_bytes[16];
        for (int i = 0; i < 16; i++) {
            random_bytes[i] = dis(gen);
        }
        
        return base64_encode(random_bytes, 16);
    }
    
public:
    WebSocketClient(const std::string& host, int port) 
        : host(host), port(port), sock(-1) {}
    
    ~WebSocketClient() {
        if (sock >= 0) {
            close(sock);
        }
    }
    
    bool connect() {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address" << std::endl;
            return false;
        }
        
        if (::connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }
        
        return performHandshake();
    }
    
    bool performHandshake() {
        std::string key = generate_random_key();
        
        std::ostringstream request;
        request << "GET / HTTP/1.1\r\n"
                << "Host: " << host << ":" << port << "\r\n"
                << "Upgrade: websocket\r\n"
                << "Connection: Upgrade\r\n"
                << "Sec-WebSocket-Key: " << key << "\r\n"
                << "Sec-WebSocket-Version: 13\r\n\r\n";
        
        std::string req_str = request.str();
        send(sock, req_str.c_str(), req_str.length(), 0);
        
        char buffer[4096];
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        buffer[bytes_received] = '\0';
        
        if (strstr(buffer, "101 Switching Protocols")) {
            std::cout << "WebSocket connection established" << std::endl;
            return true;
        }
        
        std::cerr << "Handshake failed" << std::endl;
        return false;
    }
    
    void sendMessage(const std::string& message) {
        size_t message_length = message.length();
        size_t frame_size = 6 + message_length; // Header + mask + payload
        
        unsigned char* frame = new unsigned char[frame_size];
        
        // FIN bit set, opcode 0x1 (text)
        frame[0] = 0x81;
        
        // Payload length with MASK bit set
        if (message_length <= 125) {
            frame[1] = 0x80 | message_length;
            frame_size = 6 + message_length;
        }
        
        // Generate masking key
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        unsigned char mask[4];
        for (int i = 0; i < 4; i++) {
            mask[i] = dis(gen);
            frame[2 + i] = mask[i];
        }
        
        // Mask and copy payload
        for (size_t i = 0; i < message_length; i++) {
            frame[6 + i] = message[i] ^ mask[i % 4];
        }
        
        send(sock, frame, frame_size, 0);
        delete[] frame;
    }
    
    std::string receiveMessage() {
        unsigned char buffer[4096];
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            return "";
        }
        
        uint64_t payload_length = buffer[1] & 0x7F;
        int pos = 2;
        
        if (payload_length == 126) {
            payload_length = (buffer[2] << 8) | buffer[3];
            pos = 4;
        } else if (payload_length == 127) {
            payload_length = 0;
            for (int i = 0; i < 8; i++) {
                payload_length = (payload_length << 8) | buffer[2 + i];
            }
            pos = 10;
        }
        
        std::string result(reinterpret_cast<char*>(buffer + pos), payload_length);
        return result;
    }
};

int main() {
    WebSocketClient client("127.0.0.1", 8080);
    
    if (client.connect()) {
        client.sendMessage("Hello, WebSocket!");
        std::cout << "Sent: Hello, WebSocket!" << std::endl;
        
        std::string response = client.receiveMessage();
        std::cout << "Received: " << response << std::endl;
    }
    
    return 0;
}
```

### Rust Implementation (using `tokio-tungstenite` library)

```rust
// Cargo.toml dependencies:
// [dependencies]
// tokio = { version = "1", features = ["full"] }
// tokio-tungstenite = "0.21"
// futures-util = "0.3"

use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::error::Error;

// WebSocket Server
async fn handle_client(stream: TcpStream) -> Result<(), Box<dyn Error>> {
    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");
    
    let (mut write, mut read) = ws_stream.split();
    
    while let Some(message) = read.next().await {
        match message? {
            Message::Text(text) => {
                println!("Received: {}", text);
                
                // Echo back with prefix
                let response = format!("Echo: {}", text);
                write.send(Message::Text(response)).await?;
            }
            Message::Binary(data) => {
                println!("Received binary data: {} bytes", data.len());
                write.send(Message::Binary(data)).await?;
            }
            Message::Ping(data) => {
                write.send(Message::Pong(data)).await?;
            }
            Message::Close(_) => {
                println!("Client disconnected");
                break;
            }
            _ => {}
        }
    }
    
    Ok(())
}

#[tokio::main]
async fn run_server() -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on ws://127.0.0.1:8080");
    
    while let Ok((stream, addr)) = listener.accept().await {
        println!("New connection from: {}", addr);
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream).await {
                eprintln!("Error handling client: {}", e);
            }
        });
    }
    
    Ok(())
}

// WebSocket Client
#[tokio::main]
async fn run_client() -> Result<(), Box<dyn Error>> {
    let url = "ws://127.0.0.1:8080";
    let (ws_stream, _) = connect_async(url).await?;
    println!("Connected to WebSocket server");
    
    let (mut write, mut read) = ws_stream.split();
    
    // Send a message
    write.send(Message::Text("Hello from Rust!".to_string())).await?;
    println!("Sent: Hello from Rust!");
    
    // Receive response
    if let Some(message) = read.next().await {
        match message? {
            Message::Text(text) => {
                println!("Received: {}", text);
            }
            _ => {}
        }
    }
    
    // Close connection
    write.send(Message::Close(None)).await?;
    
    Ok(())
}

fn main() {
    // Run server or client based on argument
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() > 1 && args[1] == "server" {
        if let Err(e) = run_server() {
            eprintln!("Server error: {}", e);
        }
    } else {
        if let Err(e) = run_client() {
            eprintln!("Client error: {}", e);
        }
    }
}
```

### Rust Low-Level Implementation (Manual Frame Handling)

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use sha1::{Sha1, Digest};
use base64::{Engine as _, engine::general_purpose};

const MAGIC_STRING: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Generate WebSocket accept key
fn generate_accept_key(client_key: &str) -> String {
    let mut hasher = Sha1::new();
    hasher.update(format!("{}{}", client_key, MAGIC_STRING).as_bytes());
    let hash = hasher.finalize();
    general_purpose::STANDARD.encode(hash)
}

// Parse WebSocket key from HTTP request
fn extract_websocket_key(request: &str) -> Option<String> {
    for line in request.lines() {
        if line.starts_with("Sec-WebSocket-Key:") {
            return Some(line.split(':').nth(1)?.trim().to_string());
        }
    }
    None
}

// Decode WebSocket frame
fn decode_frame(buffer: &[u8]) -> Option<(u8, Vec<u8>)> {
    if buffer.len() < 2 {
        return None;
    }
    
    let opcode = buffer[0] & 0x0F;
    let masked = (buffer[1] & 0x80) != 0;
    let mut payload_length = (buffer[1] & 0x7F) as usize;
    let mut pos = 2;
    
    // Extended payload length
    if payload_length == 126 {
        payload_length = ((buffer[2] as usize) << 8) | (buffer[3] as usize);
        pos = 4;
    } else if payload_length == 127 {
        payload_length = 0;
        for i in 0..8 {
            payload_length = (payload_length << 8) | (buffer[2 + i] as usize);
        }
        pos = 10;
    }
    
    // Extract mask
    let mask = if masked {
        let m = [buffer[pos], buffer[pos + 1], buffer[pos + 2], buffer[pos + 3]];
        pos += 4;
        Some(m)
    } else {
        None
    };
    
    // Decode payload
    let mut payload = vec![0u8; payload_length];
    for i in 0..payload_length {
        payload[i] = if let Some(ref m) = mask {
            buffer[pos + i] ^ m[i % 4]
        } else {
            buffer[pos + i]
        };
    }
    
    Some((opcode, payload))
}

// Encode WebSocket frame
fn encode_frame(opcode: u8, payload: &[u8]) -> Vec<u8> {
    let mut frame = Vec::new();
    
    // FIN bit set + opcode
    frame.push(0x80 | opcode);
    
    // Payload length
    let len = payload.len();
    if len <= 125 {
        frame.push(len as u8);
    } else if len <= 65535 {
        frame.push(126);
        frame.push((len >> 8) as u8);
        frame.push((len & 0xFF) as u8);
    } else {
        frame.push(127);
        for i in (0..8).rev() {
            frame.push(((len >> (i * 8)) & 0xFF) as u8);
        }
    }
    
    // Payload
    frame.extend_from_slice(payload);
    frame
}

// Handle WebSocket connection
fn handle_connection(mut stream: TcpStream) -> std::io::Result<()> {
    let mut buffer = [0u8; 4096];
    
    // Read HTTP handshake
    let n = stream.read(&mut buffer)?;
    let request = String::from_utf8_lossy(&buffer[..n]);
    
    // Extract and validate key
    if let Some(client_key) = extract_websocket_key(&request) {
        let accept_key = generate_accept_key(&client_key);
        
        // Send handshake response
        let response = format!(
            "HTTP/1.1 101 Switching Protocols\r\n\
             Upgrade: websocket\r\n\
             Connection: Upgrade\r\n\
             Sec-WebSocket-Accept: {}\r\n\r\n",
            accept_key
        );
        stream.write_all(response.as_bytes())?;
        println!("WebSocket handshake completed");
        
        // Handle frames
        loop {
            let n = stream.read(&mut buffer)?;
            if n == 0 {
                break;
            }
            
            if let Some((opcode, payload)) = decode_frame(&buffer[..n]) {
                match opcode {
                    0x1 => { // Text frame
                        let message = String::from_utf8_lossy(&payload);
                        println!("Received: {}", message);
                        
                        // Echo back
                        let response = format!("Echo: {}", message);
                        let frame = encode_frame(0x1, response.as_bytes());
                        stream.write_all(&frame)?;
                    }
                    0x8 => { // Close frame
                        println!("Client closed connection");
                        break;
                    }
                    0x9 => { // Ping frame
                        let pong = encode_frame(0xA, &payload);
                        stream.write_all(&pong)?;
                    }
                    _ => {}
                }
            }
        }
    }
    
    Ok(())
}

fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("WebSocket server listening on port 8080");
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                std::thread::spawn(move || {
                    if let Err(e) = handle_connection(stream) {
                        eprintln!("Connection error: {}", e);
                    }
                });
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    Ok(())
}
```

---

## Summary

**WebSocket** is a powerful protocol for real-time, bidirectional communication between clients and servers. It starts with an HTTP upgrade handshake that transitions the connection to the WebSocket protocol, after which data flows through lightweight frames with minimal overhead.

**Key Takeaways:**

1. **Handshake Process**: WebSocket begins as an HTTP connection and upgrades using the `Sec-WebSocket-Key` and `Sec-WebSocket-Accept` mechanism for security validation.

2. **Frame Structure**: Data is transmitted in frames with opcodes indicating the frame type (text, binary, close, ping, pong), masking for client-to-server messages, and efficient payload length encoding.

3. **Advantages over HTTP**: WebSocket provides persistent connections, full-duplex communication, lower latency, and reduced overhead compared to HTTP polling or long-polling techniques.

4. **Implementation Approaches**: 
   - **C/C++**: Requires manual handling of sockets, frame parsing, masking, and cryptographic operations for the handshake
   - **Rust**: Can use high-level libraries like `tokio-tungstenite` for simplified implementation, or implement low-level frame handling for full control

5. **Use Cases**: Real-time chat applications, live notifications, multiplayer games, financial trading platforms, collaborative editing tools, and IoT device communication.

WebSocket has become an essential technology for modern web applications requiring real-time features, providing a standardized way to maintain persistent connections with efficient bidirectional data flow.