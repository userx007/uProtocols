# HTTP Upgrade Mechanism in WebSocket

The HTTP Upgrade mechanism is the foundation of establishing WebSocket connections. It allows an existing HTTP connection to be transformed into a WebSocket connection, enabling full-duplex, bidirectional communication over a single TCP connection.

## How It Works

The WebSocket handshake begins as a standard HTTP request but includes special headers that signal the desire to upgrade the protocol. This clever design allows WebSockets to work through existing HTTP infrastructure like proxies and firewalls.

### The Handshake Process

**Client Request:**
The client initiates a WebSocket connection by sending an HTTP GET request with upgrade headers:

```
GET /chat HTTP/1.1
Host: example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

**Server Response:**
If the server supports WebSocket and accepts the upgrade, it responds with:

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

The status code `101 Switching Protocols` indicates successful protocol upgrade. After this exchange, the connection transitions from HTTP to WebSocket protocol, and both parties can send WebSocket frames bidirectionally.

### Security Mechanism

The `Sec-WebSocket-Key` and `Sec-WebSocket-Accept` headers provide a security mechanism to ensure the server understands the WebSocket protocol. The server takes the client's key, appends a specific GUID (`258EAFA5-E914-47DA-95CA-C5AB0DC85B11`), computes the SHA-1 hash, and returns it base64-encoded.

## C Implementation Example

Here's a C implementation demonstrating both client and server sides of the HTTP upgrade:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define BUFFER_SIZE 4096
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

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
    
    char *result = malloc(buffer_ptr->length + 1);
    memcpy(result, buffer_ptr->data, buffer_ptr->length);
    result[buffer_ptr->length] = '\0';
    
    BIO_free_all(bio);
    return result;
}

// Generate Sec-WebSocket-Accept value
char* generate_accept_key(const char* client_key) {
    char concatenated[256];
    snprintf(concatenated, sizeof(concatenated), "%s%s", client_key, WEBSOCKET_GUID);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)concatenated, strlen(concatenated), hash);
    
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

// Server: Handle WebSocket upgrade request
int handle_upgrade_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read <= 0) {
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parse Sec-WebSocket-Key from request
    char *key_header = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_header) {
        fprintf(stderr, "Invalid WebSocket request\n");
        return -1;
    }
    
    key_header += strlen("Sec-WebSocket-Key: ");
    char client_key[256];
    sscanf(key_header, "%[^\r\n]", client_key);
    
    // Generate accept key
    char *accept_key = generate_accept_key(client_key);
    
    // Send upgrade response
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_key);
    
    send(client_socket, response, strlen(response), 0);
    free(accept_key);
    
    printf("WebSocket connection established\n");
    return 0;
}

// Client: Send WebSocket upgrade request
int send_upgrade_request(int server_socket, const char* host, const char* path) {
    // Generate random Sec-WebSocket-Key (simplified for example)
    const char *websocket_key = "dGhlIHNhbXBsZSBub25jZQ==";
    
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", path, host, websocket_key);
    
    send(server_socket, request, strlen(request), 0);
    
    // Receive and parse response
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(server_socket, buffer, BUFFER_SIZE - 1, 0);
    buffer[bytes_read] = '\0';
    
    if (strstr(buffer, "101 Switching Protocols")) {
        printf("WebSocket upgrade successful\n");
        return 0;
    }
    
    fprintf(stderr, "WebSocket upgrade failed\n");
    return -1;
}

int main() {
    printf("WebSocket HTTP Upgrade Example\n");
    printf("Compile with: gcc -o websocket websocket.c -lssl -lcrypto\n");
    return 0;
}
```

## C++ Implementation Example

A more modern C++ implementation using classes:

```cpp
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

class WebSocketHandshake {
private:
    static const std::string WEBSOCKET_GUID;
    
    static std::string base64_encode(const unsigned char* input, size_t length) {
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
    
    static std::string generate_accept_key(const std::string& client_key) {
        std::string concatenated = client_key + WEBSOCKET_GUID;
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(concatenated.c_str()),
             concatenated.length(), hash);
        
        return base64_encode(hash, SHA_DIGEST_LENGTH);
    }
    
    static std::string extract_header_value(const std::string& request, 
                                           const std::string& header_name) {
        size_t pos = request.find(header_name + ": ");
        if (pos == std::string::npos) return "";
        
        pos += header_name.length() + 2;
        size_t end = request.find("\r\n", pos);
        return request.substr(pos, end - pos);
    }

public:
    // Server-side upgrade handler
    static bool handle_upgrade(int client_socket) {
        char buffer[4096];
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) return false;
        
        buffer[bytes_read] = '\0';
        std::string request(buffer);
        
        // Validate upgrade request
        if (request.find("GET ") != 0 ||
            request.find("Upgrade: websocket") == std::string::npos ||
            request.find("Connection: Upgrade") == std::string::npos) {
            return false;
        }
        
        std::string client_key = extract_header_value(request, "Sec-WebSocket-Key");
        if (client_key.empty()) return false;
        
        std::string accept_key = generate_accept_key(client_key);
        
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
                 << "\r\n";
        
        std::string response_str = response.str();
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        
        std::cout << "WebSocket connection established" << std::endl;
        return true;
    }
    
    // Client-side upgrade initiator
    static bool initiate_upgrade(int server_socket, 
                                const std::string& host,
                                const std::string& path,
                                const std::string& websocket_key) {
        std::ostringstream request;
        request << "GET " << path << " HTTP/1.1\r\n"
                << "Host: " << host << "\r\n"
                << "Upgrade: websocket\r\n"
                << "Connection: Upgrade\r\n"
                << "Sec-WebSocket-Key: " << websocket_key << "\r\n"
                << "Sec-WebSocket-Version: 13\r\n"
                << "\r\n";
        
        std::string request_str = request.str();
        send(server_socket, request_str.c_str(), request_str.length(), 0);
        
        char buffer[4096];
        ssize_t bytes_read = recv(server_socket, buffer, sizeof(buffer) - 1, 0);
        buffer[bytes_read] = '\0';
        
        std::string response(buffer);
        return response.find("101 Switching Protocols") != std::string::npos;
    }
};

const std::string WebSocketHandshake::WEBSOCKET_GUID = 
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int main() {
    std::cout << "WebSocket HTTP Upgrade Example (C++)" << std::endl;
    std::cout << "Compile with: g++ -o websocket websocket.cpp -lssl -lcrypto" 
              << std::endl;
    return 0;
}
```

## Rust Implementation Example

Here's a Rust implementation leveraging its safety features and modern async capabilities:

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use sha1::{Sha1, Digest};
use base64::{Engine as _, engine::general_purpose};

const WEBSOCKET_GUID: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// Generate the Sec-WebSocket-Accept value from client key
fn generate_accept_key(client_key: &str) -> String {
    let mut hasher = Sha1::new();
    hasher.update(client_key.as_bytes());
    hasher.update(WEBSOCKET_GUID.as_bytes());
    let hash = hasher.finalize();
    general_purpose::STANDARD.encode(&hash)
}

/// Extract header value from HTTP request
fn extract_header<'a>(request: &'a str, header_name: &str) -> Option<&'a str> {
    let header_prefix = format!("{}: ", header_name);
    request.lines()
        .find(|line| line.starts_with(&header_prefix))
        .map(|line| line[header_prefix.len()..].trim())
}

/// Server: Handle WebSocket upgrade request
fn handle_upgrade_request(mut stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
    let mut buffer = [0u8; 4096];
    let bytes_read = stream.read(&mut buffer)?;
    let request = String::from_utf8_lossy(&buffer[..bytes_read]);
    
    // Validate WebSocket upgrade request
    if !request.starts_with("GET ") ||
       !request.contains("Upgrade: websocket") ||
       !request.contains("Connection: Upgrade") {
        return Err("Invalid WebSocket upgrade request".into());
    }
    
    // Extract and validate Sec-WebSocket-Key
    let client_key = extract_header(&request, "Sec-WebSocket-Key")
        .ok_or("Missing Sec-WebSocket-Key header")?;
    
    // Validate WebSocket version
    let version = extract_header(&request, "Sec-WebSocket-Version")
        .ok_or("Missing Sec-WebSocket-Version header")?;
    
    if version != "13" {
        return Err("Unsupported WebSocket version".into());
    }
    
    // Generate accept key
    let accept_key = generate_accept_key(client_key);
    
    // Send upgrade response
    let response = format!(
        "HTTP/1.1 101 Switching Protocols\r\n\
         Upgrade: websocket\r\n\
         Connection: Upgrade\r\n\
         Sec-WebSocket-Accept: {}\r\n\
         \r\n",
        accept_key
    );
    
    stream.write_all(response.as_bytes())?;
    stream.flush()?;
    
    println!("WebSocket connection established");
    Ok(())
}

/// Client: Send WebSocket upgrade request
fn send_upgrade_request(
    mut stream: TcpStream,
    host: &str,
    path: &str,
    websocket_key: &str
) -> Result<(), Box<dyn std::error::Error>> {
    let request = format!(
        "GET {} HTTP/1.1\r\n\
         Host: {}\r\n\
         Upgrade: websocket\r\n\
         Connection: Upgrade\r\n\
         Sec-WebSocket-Key: {}\r\n\
         Sec-WebSocket-Version: 13\r\n\
         \r\n",
        path, host, websocket_key
    );
    
    stream.write_all(request.as_bytes())?;
    stream.flush()?;
    
    // Read response
    let mut buffer = [0u8; 4096];
    let bytes_read = stream.read(&mut buffer)?;
    let response = String::from_utf8_lossy(&buffer[..bytes_read]);
    
    if response.contains("101 Switching Protocols") {
        println!("WebSocket upgrade successful");
        Ok(())
    } else {
        Err("WebSocket upgrade failed".into())
    }
}

/// Simple WebSocket server example
fn run_server(addr: &str) -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind(addr)?;
    println!("WebSocket server listening on {}", addr);
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                println!("New connection from: {:?}", stream.peer_addr());
                if let Err(e) = handle_upgrade_request(stream) {
                    eprintln!("Error handling upgrade: {}", e);
                }
            }
            Err(e) => eprintln!("Connection error: {}", e),
        }
    }
    
    Ok(())
}

fn main() {
    println!("WebSocket HTTP Upgrade Example (Rust)");
    println!("Add to Cargo.toml:");
    println!("  [dependencies]");
    println!("  sha1 = \"0.10\"");
    println!("  base64 = \"0.21\"");
}
```

## Summary

The HTTP Upgrade mechanism is an elegant solution that allows WebSocket connections to be established over existing HTTP infrastructure. The handshake process involves:

1. **Client initiates** an HTTP GET request with special upgrade headers including `Upgrade: websocket`, `Connection: Upgrade`, and a security key
2. **Server validates** the request and generates a cryptographic response by hashing the client's key with a predefined GUID
3. **Server responds** with HTTP 101 status code indicating successful protocol switch
4. **Connection transforms** from HTTP to WebSocket, enabling bidirectional frame-based communication

The security mechanism using `Sec-WebSocket-Key` and `Sec-WebSocket-Accept` ensures both parties understand the WebSocket protocol and prevents cross-protocol attacks. This design allows WebSockets to traverse HTTP-aware infrastructure while providing the performance benefits of persistent, full-duplex connections.

The code examples demonstrate how to implement both client and server sides of the upgrade handshake in C, C++, and Rust, showing the practical aspects of parsing HTTP headers, generating cryptographic keys, and managing the protocol transition.