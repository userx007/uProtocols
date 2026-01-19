# WebSocket Connection Handshake Implementation

## Overview

The WebSocket connection handshake is the crucial initial phase that upgrades an HTTP connection to a WebSocket connection. This process involves a carefully orchestrated exchange between client and server, where specific headers are validated and cryptographic keys are exchanged to establish a persistent, bidirectional communication channel.

## The Handshake Process

### Client Request

The client initiates the handshake by sending an HTTP request with specific upgrade headers:

```
GET /chat HTTP/1.1
Host: example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

The `Sec-WebSocket-Key` is a randomly generated 16-byte value, base64-encoded. This key serves as a nonce to prevent caching proxies from interfering with the connection.

### Server Response

The server validates the request and responds with:

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

The `Sec-WebSocket-Accept` value is calculated by concatenating the client's `Sec-WebSocket-Key` with a specific GUID, then computing the SHA-1 hash and base64-encoding the result.

## Key Generation and Accept Header Calculation

The magic string (GUID) defined in RFC 6455 is:
```
258EAFA5-E914-47DA-95CA-C5AB0DC85B11
```

**Calculation steps:**
1. Concatenate `Sec-WebSocket-Key` + GUID
2. Compute SHA-1 hash of the concatenated string
3. Base64-encode the hash result

## C Implementation

Here's a complete C implementation of the WebSocket handshake:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <time.h>
#include <unistd.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_KEY_LENGTH 16

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
    
    char* result = (char*)malloc(buffer_ptr->length + 1);
    memcpy(result, buffer_ptr->data, buffer_ptr->length);
    result[buffer_ptr->length] = '\0';
    
    BIO_free_all(bio);
    return result;
}

// Base64 decode function
int base64_decode(const char* input, unsigned char** output) {
    BIO *bio, *b64;
    int decode_len = strlen(input);
    *output = (unsigned char*)malloc(decode_len);
    
    bio = BIO_new_mem_buf(input, -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int length = BIO_read(bio, *output, decode_len);
    
    BIO_free_all(bio);
    return length;
}

// Generate random WebSocket key
char* generate_websocket_key() {
    unsigned char random_bytes[WS_KEY_LENGTH];
    
    // Generate random bytes
    FILE* urandom = fopen("/dev/urandom", "r");
    if (urandom) {
        fread(random_bytes, 1, WS_KEY_LENGTH, urandom);
        fclose(urandom);
    } else {
        // Fallback to rand() if /dev/urandom unavailable
        srand(time(NULL));
        for (int i = 0; i < WS_KEY_LENGTH; i++) {
            random_bytes[i] = rand() % 256;
        }
    }
    
    return base64_encode(random_bytes, WS_KEY_LENGTH);
}

// Calculate Sec-WebSocket-Accept value
char* calculate_accept_key(const char* client_key) {
    char concatenated[256];
    unsigned char hash[SHA_DIGEST_LENGTH];
    
    // Concatenate client key with GUID
    snprintf(concatenated, sizeof(concatenated), "%s%s", client_key, WS_GUID);
    
    // Calculate SHA-1 hash
    SHA1((unsigned char*)concatenated, strlen(concatenated), hash);
    
    // Base64 encode the hash
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

// Validate WebSocket handshake request
int validate_handshake_request(const char* request, char** ws_key) {
    // Check for required headers
    if (!strstr(request, "Upgrade: websocket")) return 0;
    if (!strstr(request, "Connection: Upgrade")) return 0;
    
    // Extract Sec-WebSocket-Key
    const char* key_header = strstr(request, "Sec-WebSocket-Key: ");
    if (!key_header) return 0;
    
    key_header += strlen("Sec-WebSocket-Key: ");
    const char* key_end = strstr(key_header, "\r\n");
    if (!key_end) return 0;
    
    int key_length = key_end - key_header;
    *ws_key = (char*)malloc(key_length + 1);
    strncpy(*ws_key, key_header, key_length);
    (*ws_key)[key_length] = '\0';
    
    return 1;
}

// Generate server handshake response
char* generate_handshake_response(const char* client_key) {
    char* accept_key = calculate_accept_key(client_key);
    char* response = (char*)malloc(512);
    
    snprintf(response, 512,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);
    
    free(accept_key);
    return response;
}

// Client-side: Generate handshake request
char* generate_client_handshake(const char* host, const char* path, char** out_key) {
    *out_key = generate_websocket_key();
    char* request = (char*)malloc(512);
    
    snprintf(request, 512,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, *out_key);
    
    return request;
}

// Client-side: Validate server response
int validate_server_response(const char* response, const char* client_key) {
    // Check status code
    if (!strstr(response, "101 Switching Protocols")) return 0;
    
    // Calculate expected accept key
    char* expected_accept = calculate_accept_key(client_key);
    
    // Extract actual accept key from response
    const char* accept_header = strstr(response, "Sec-WebSocket-Accept: ");
    if (!accept_header) {
        free(expected_accept);
        return 0;
    }
    
    accept_header += strlen("Sec-WebSocket-Accept: ");
    const char* accept_end = strstr(accept_header, "\r\n");
    
    int match = (accept_end && 
                 strncmp(accept_header, expected_accept, strlen(expected_accept)) == 0);
    
    free(expected_accept);
    return match;
}

int main() {
    printf("=== WebSocket Handshake Implementation (C) ===\n\n");
    
    // Client-side example
    printf("CLIENT SIDE:\n");
    char* client_key;
    char* client_request = generate_client_handshake("example.com", "/chat", &client_key);
    printf("Generated Key: %s\n\n", client_key);
    printf("Client Request:\n%s\n", client_request);
    
    // Server-side example
    printf("SERVER SIDE:\n");
    char* extracted_key;
    if (validate_handshake_request(client_request, &extracted_key)) {
        printf("Handshake request valid!\n");
        printf("Extracted Key: %s\n\n", extracted_key);
        
        char* server_response = generate_handshake_response(extracted_key);
        printf("Server Response:\n%s\n", server_response);
        
        // Client validates server response
        printf("CLIENT VALIDATION:\n");
        if (validate_server_response(server_response, client_key)) {
            printf("✓ Server response validated successfully!\n");
            printf("✓ WebSocket connection established\n");
        } else {
            printf("✗ Server response validation failed!\n");
        }
        
        free(server_response);
        free(extracted_key);
    }
    
    free(client_request);
    free(client_key);
    
    return 0;
}
```

## C++ Implementation

Here's a modern C++ implementation using standard library features:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

class WebSocketHandshake {
private:
    static constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    static constexpr size_t WS_KEY_LENGTH = 16;

    // Base64 encoding
    static std::string base64_encode(const std::vector<unsigned char>& input) {
        BIO *bio, *b64;
        BUF_MEM *buffer_ptr;
        
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input.data(), input.size());
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &buffer_ptr);
        
        std::string result(buffer_ptr->data, buffer_ptr->length);
        BIO_free_all(bio);
        
        return result;
    }

    // Base64 decoding
    static std::vector<unsigned char> base64_decode(const std::string& input) {
        BIO *bio, *b64;
        std::vector<unsigned char> output(input.length());
        
        bio = BIO_new_mem_buf(input.c_str(), -1);
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        int length = BIO_read(bio, output.data(), input.length());
        
        BIO_free_all(bio);
        output.resize(length);
        
        return output;
    }

public:
    // Generate random WebSocket key
    static std::string generateKey() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::vector<unsigned char> random_bytes(WS_KEY_LENGTH);
        for (size_t i = 0; i < WS_KEY_LENGTH; ++i) {
            random_bytes[i] = static_cast<unsigned char>(dis(gen));
        }
        
        return base64_encode(random_bytes);
    }

    // Calculate Sec-WebSocket-Accept value
    static std::string calculateAcceptKey(const std::string& client_key) {
        std::string concatenated = client_key + WS_GUID;
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(concatenated.c_str()),
             concatenated.length(), hash);
        
        std::vector<unsigned char> hash_vec(hash, hash + SHA_DIGEST_LENGTH);
        return base64_encode(hash_vec);
    }

    // Extract header value from HTTP request/response
    static std::string extractHeader(const std::string& message, 
                                     const std::string& header_name) {
        std::string search = header_name + ": ";
        size_t pos = message.find(search);
        if (pos == std::string::npos) return "";
        
        pos += search.length();
        size_t end = message.find("\r\n", pos);
        if (end == std::string::npos) return "";
        
        return message.substr(pos, end - pos);
    }

    // Generate client handshake request
    static std::string generateClientRequest(const std::string& host,
                                             const std::string& path,
                                             std::string& out_key) {
        out_key = generateKey();
        
        std::ostringstream oss;
        oss << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << out_key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n"
            << "\r\n";
        
        return oss.str();
    }

    // Validate client handshake request
    static bool validateClientRequest(const std::string& request,
                                      std::string& out_key) {
        if (request.find("Upgrade: websocket") == std::string::npos) return false;
        if (request.find("Connection: Upgrade") == std::string::npos) return false;
        
        out_key = extractHeader(request, "Sec-WebSocket-Key");
        return !out_key.empty();
    }

    // Generate server handshake response
    static std::string generateServerResponse(const std::string& client_key) {
        std::string accept_key = calculateAcceptKey(client_key);
        
        std::ostringstream oss;
        oss << "HTTP/1.1 101 Switching Protocols\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
            << "\r\n";
        
        return oss.str();
    }

    // Validate server handshake response
    static bool validateServerResponse(const std::string& response,
                                       const std::string& client_key) {
        if (response.find("101 Switching Protocols") == std::string::npos) {
            return false;
        }
        
        std::string expected_accept = calculateAcceptKey(client_key);
        std::string actual_accept = extractHeader(response, "Sec-WebSocket-Accept");
        
        return expected_accept == actual_accept;
    }
};

int main() {
    std::cout << "=== WebSocket Handshake Implementation (C++) ===\n\n";
    
    // Client-side example
    std::cout << "CLIENT SIDE:\n";
    std::string client_key;
    std::string client_request = 
        WebSocketHandshake::generateClientRequest("example.com", "/chat", client_key);
    
    std::cout << "Generated Key: " << client_key << "\n\n";
    std::cout << "Client Request:\n" << client_request << "\n";
    
    // Server-side example
    std::cout << "SERVER SIDE:\n";
    std::string extracted_key;
    if (WebSocketHandshake::validateClientRequest(client_request, extracted_key)) {
        std::cout << "Handshake request valid!\n";
        std::cout << "Extracted Key: " << extracted_key << "\n\n";
        
        std::string server_response = 
            WebSocketHandshake::generateServerResponse(extracted_key);
        std::cout << "Server Response:\n" << server_response << "\n";
        
        // Client validates server response
        std::cout << "CLIENT VALIDATION:\n";
        if (WebSocketHandshake::validateServerResponse(server_response, client_key)) {
            std::cout << "✓ Server response validated successfully!\n";
            std::cout << "✓ WebSocket connection established\n";
        } else {
            std::cout << "✗ Server response validation failed!\n";
        }
    }
    
    return 0;
}
```

## Rust Implementation

Here's a Rust implementation leveraging its safety features and standard library:

```rust
use rand::Rng;
use sha1::{Sha1, Digest};
use base64::{Engine as _, engine::general_purpose};
use std::fmt;

const WS_GUID: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const WS_KEY_LENGTH: usize = 16;

#[derive(Debug)]
pub enum HandshakeError {
    InvalidRequest,
    MissingHeader(String),
    InvalidKey,
    ValidationFailed,
}

impl fmt::Display for HandshakeError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            HandshakeError::InvalidRequest => write!(f, "Invalid handshake request"),
            HandshakeError::MissingHeader(h) => write!(f, "Missing header: {}", h),
            HandshakeError::InvalidKey => write!(f, "Invalid WebSocket key"),
            HandshakeError::ValidationFailed => write!(f, "Validation failed"),
        }
    }
}

impl std::error::Error for HandshakeError {}

pub struct WebSocketHandshake;

impl WebSocketHandshake {
    /// Generate a random WebSocket key
    pub fn generate_key() -> String {
        let mut rng = rand::thread_rng();
        let random_bytes: Vec<u8> = (0..WS_KEY_LENGTH)
            .map(|_| rng.gen())
            .collect();
        
        general_purpose::STANDARD.encode(&random_bytes)
    }

    /// Calculate Sec-WebSocket-Accept value from client key
    pub fn calculate_accept_key(client_key: &str) -> String {
        let concatenated = format!("{}{}", client_key, WS_GUID);
        
        let mut hasher = Sha1::new();
        hasher.update(concatenated.as_bytes());
        let hash = hasher.finalize();
        
        general_purpose::STANDARD.encode(&hash)
    }

    /// Extract a header value from an HTTP message
    fn extract_header(message: &str, header_name: &str) -> Option<String> {
        let search = format!("{}: ", header_name);
        let start = message.find(&search)? + search.len();
        let end = message[start..].find("\r\n")? + start;
        
        Some(message[start..end].to_string())
    }

    /// Generate client handshake request
    pub fn generate_client_request(host: &str, path: &str) -> (String, String) {
        let key = Self::generate_key();
        
        let request = format!(
            "GET {} HTTP/1.1\r\n\
             Host: {}\r\n\
             Upgrade: websocket\r\n\
             Connection: Upgrade\r\n\
             Sec-WebSocket-Key: {}\r\n\
             Sec-WebSocket-Version: 13\r\n\
             \r\n",
            path, host, key
        );
        
        (request, key)
    }

    /// Validate client handshake request and extract key
    pub fn validate_client_request(request: &str) -> Result<String, HandshakeError> {
        if !request.contains("Upgrade: websocket") {
            return Err(HandshakeError::MissingHeader("Upgrade".to_string()));
        }
        
        if !request.contains("Connection: Upgrade") {
            return Err(HandshakeError::MissingHeader("Connection".to_string()));
        }
        
        Self::extract_header(request, "Sec-WebSocket-Key")
            .ok_or(HandshakeError::MissingHeader("Sec-WebSocket-Key".to_string()))
    }

    /// Generate server handshake response
    pub fn generate_server_response(client_key: &str) -> String {
        let accept_key = Self::calculate_accept_key(client_key);
        
        format!(
            "HTTP/1.1 101 Switching Protocols\r\n\
             Upgrade: websocket\r\n\
             Connection: Upgrade\r\n\
             Sec-WebSocket-Accept: {}\r\n\
             \r\n",
            accept_key
        )
    }

    /// Validate server handshake response
    pub fn validate_server_response(response: &str, client_key: &str) 
        -> Result<(), HandshakeError> {
        if !response.contains("101 Switching Protocols") {
            return Err(HandshakeError::InvalidRequest);
        }
        
        let expected_accept = Self::calculate_accept_key(client_key);
        let actual_accept = Self::extract_header(response, "Sec-WebSocket-Accept")
            .ok_or(HandshakeError::MissingHeader("Sec-WebSocket-Accept".to_string()))?;
        
        if expected_accept == actual_accept {
            Ok(())
        } else {
            Err(HandshakeError::ValidationFailed)
        }
    }
}

// Example usage with error handling
pub struct WebSocketClient {
    key: Option<String>,
}

impl WebSocketClient {
    pub fn new() -> Self {
        Self { key: None }
    }

    pub fn create_handshake(&mut self, host: &str, path: &str) -> String {
        let (request, key) = WebSocketHandshake::generate_client_request(host, path);
        self.key = Some(key);
        request
    }

    pub fn validate_response(&self, response: &str) -> Result<(), HandshakeError> {
        let key = self.key.as_ref().ok_or(HandshakeError::InvalidKey)?;
        WebSocketHandshake::validate_server_response(response, key)
    }
}

pub struct WebSocketServer;

impl WebSocketServer {
    pub fn handle_handshake(request: &str) -> Result<String, HandshakeError> {
        let client_key = WebSocketHandshake::validate_client_request(request)?;
        Ok(WebSocketHandshake::generate_server_response(&client_key))
    }
}

fn main() {
    println!("=== WebSocket Handshake Implementation (Rust) ===\n");
    
    // Client-side example
    println!("CLIENT SIDE:");
    let mut client = WebSocketClient::new();
    let client_request = client.create_handshake("example.com", "/chat");
    
    println!("Generated Key: {}\n", client.key.as_ref().unwrap());
    println!("Client Request:\n{}", client_request);
    
    // Server-side example
    println!("SERVER SIDE:");
    match WebSocketServer::handle_handshake(&client_request) {
        Ok(server_response) => {
            println!("Handshake request valid!");
            println!("\nServer Response:\n{}", server_response);
            
            // Client validates server response
            println!("CLIENT VALIDATION:");
            match client.validate_response(&server_response) {
                Ok(_) => {
                    println!("✓ Server response validated successfully!");
                    println!("✓ WebSocket connection established");
                }
                Err(e) => println!("✗ Validation failed: {}", e),
            }
        }
        Err(e) => println!("✗ Handshake failed: {}", e),
    }
    
    // Demonstrate key calculation
    println!("\n=== Key Calculation Example ===");
    let test_key = "dGhlIHNhbXBsZSBub25jZQ==";
    let accept_key = WebSocketHandshake::calculate_accept_key(test_key);
    println!("Client Key:  {}", test_key);
    println!("Accept Key:  {}", accept_key);
    println!("Expected:    s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    println!("Match: {}", accept_key == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}
```

For the Rust implementation, add these dependencies to your `Cargo.toml`:

```toml
[dependencies]
rand = "0.8"
sha1 = "0.10"
base64 = "0.21"
```

## Summary

The WebSocket connection handshake is a critical security mechanism that establishes a bidirectional communication channel. The key components include:

**Client Responsibilities:**
- Generate a random 16-byte nonce, base64-encoded as `Sec-WebSocket-Key`
- Send an HTTP upgrade request with proper headers
- Validate the server's `Sec-WebSocket-Accept` response

**Server Responsibilities:**
- Validate the upgrade request headers
- Extract the client's `Sec-WebSocket-Key`
- Calculate `Sec-WebSocket-Accept` by concatenating the key with the GUID, hashing with SHA-1, and base64-encoding
- Send a 101 Switching Protocols response

**Security Features:**
- The handshake prevents accidental caching proxy interference
- The cryptographic accept key validation ensures both parties understand the WebSocket protocol
- The random nonce prevents replay attacks

The implementations demonstrate how to handle the handshake in C (low-level with manual memory management), C++ (object-oriented with RAII), and Rust (safe, idiomatic with strong error handling). Each implementation shows both client and server perspectives, key generation, validation, and the complete handshake flow. The Rust version particularly showcases modern error handling patterns and type safety, while the C/C++ versions demonstrate traditional approaches with OpenSSL integration.