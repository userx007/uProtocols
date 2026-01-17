# WebSocket Protocol: Full-Duplex Communication over TCP

## Overview

The WebSocket protocol provides full-duplex, bidirectional communication channels over a single TCP connection. Defined in RFC 6455, it enables real-time data exchange between clients and servers without the overhead of HTTP polling or long-polling techniques.

## Key Characteristics

**Full-Duplex Communication**: Both client and server can send messages independently and simultaneously, unlike traditional HTTP's request-response model.

**Persistent Connection**: After the initial handshake, the connection remains open, eliminating the overhead of establishing new connections for each message.

**Low Overhead**: After the handshake, data frames have minimal header overhead (2-14 bytes) compared to HTTP headers.

**Protocol Upgrade**: WebSocket connections begin as HTTP requests and upgrade via the HTTP/1.1 upgrade mechanism.

## WebSocket Handshake

The WebSocket connection starts with an HTTP handshake:

**Client Request:**
```
GET /chat HTTP/1.1
Host: server.example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

**Server Response:**
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

The `Sec-WebSocket-Accept` is calculated by concatenating the client's `Sec-WebSocket-Key` with a magic string and computing the SHA-1 hash.

## Frame Structure

WebSocket frames have a compact structure:

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
|                               | Masking-key, if MASK set to 1 |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data continued ...                |
+---------------------------------------------------------------+
```

**Opcodes:**
- 0x0: Continuation frame
- 0x1: Text frame
- 0x2: Binary frame
- 0x8: Connection close
- 0x9: Ping
- 0xA: Pong

## C/C++ Implementation

Here's a basic WebSocket server implementation in C++:

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

class WebSocketServer {
private:
    int server_fd;
    const int PORT = 8080;
    static constexpr const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    std::string base64_encode(const unsigned char* input, int length) {
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input, length);
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        
        std::string result(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);
        
        return result;
    }

    std::string generateAcceptKey(const std::string& clientKey) {
        std::string combined = clientKey + WS_MAGIC;
        unsigned char hash[SHA_DIGEST_LENGTH];
        
        SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), 
             combined.length(), hash);
        
        return base64_encode(hash, SHA_DIGEST_LENGTH);
    }

    bool performHandshake(int client_socket) {
        char buffer[4096] = {0};
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
        
        if (bytes_read <= 0) return false;
        
        std::string request(buffer);
        size_t key_pos = request.find("Sec-WebSocket-Key: ");
        
        if (key_pos == std::string::npos) return false;
        
        key_pos += 19; // Length of "Sec-WebSocket-Key: "
        size_t key_end = request.find("\r\n", key_pos);
        std::string client_key = request.substr(key_pos, key_end - key_pos);
        
        std::string accept_key = generateAcceptKey(client_key);
        
        std::string response = 
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
        
        send(client_socket, response.c_str(), response.length(), 0);
        return true;
    }

    struct WebSocketFrame {
        bool fin;
        uint8_t opcode;
        bool masked;
        uint64_t payload_length;
        uint8_t masking_key[4];
        std::vector<uint8_t> payload;
    };

    WebSocketFrame parseFrame(int client_socket) {
        WebSocketFrame frame;
        uint8_t header[2];
        
        recv(client_socket, header, 2, 0);
        
        frame.fin = (header[0] & 0x80) != 0;
        frame.opcode = header[0] & 0x0F;
        frame.masked = (header[1] & 0x80) != 0;
        frame.payload_length = header[1] & 0x7F;
        
        // Handle extended payload length
        if (frame.payload_length == 126) {
            uint8_t extended[2];
            recv(client_socket, extended, 2, 0);
            frame.payload_length = (extended[0] << 8) | extended[1];
        } else if (frame.payload_length == 127) {
            uint8_t extended[8];
            recv(client_socket, extended, 8, 0);
            frame.payload_length = 0;
            for (int i = 0; i < 8; i++) {
                frame.payload_length = (frame.payload_length << 8) | extended[i];
            }
        }
        
        // Read masking key if present
        if (frame.masked) {
            recv(client_socket, frame.masking_key, 4, 0);
        }
        
        // Read payload
        frame.payload.resize(frame.payload_length);
        recv(client_socket, frame.payload.data(), frame.payload_length, 0);
        
        // Unmask payload
        if (frame.masked) {
            for (uint64_t i = 0; i < frame.payload_length; i++) {
                frame.payload[i] ^= frame.masking_key[i % 4];
            }
        }
        
        return frame;
    }

    void sendFrame(int client_socket, uint8_t opcode, const std::string& data) {
        std::vector<uint8_t> frame;
        
        // FIN bit set, opcode
        frame.push_back(0x80 | opcode);
        
        // Payload length (server frames are not masked)
        if (data.length() < 126) {
            frame.push_back(static_cast<uint8_t>(data.length()));
        } else if (data.length() < 65536) {
            frame.push_back(126);
            frame.push_back((data.length() >> 8) & 0xFF);
            frame.push_back(data.length() & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((data.length() >> (i * 8)) & 0xFF);
            }
        }
        
        // Append payload
        frame.insert(frame.end(), data.begin(), data.end());
        
        send(client_socket, frame.data(), frame.size(), 0);
    }

public:
    WebSocketServer() : server_fd(-1) {}

    void start() {
        struct sockaddr_in address;
        int opt = 1;
        
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);
        
        bind(server_fd, (struct sockaddr*)&address, sizeof(address));
        listen(server_fd, 3);
        
        std::cout << "WebSocket server listening on port " << PORT << std::endl;
        
        while (true) {
            int client_socket = accept(server_fd, nullptr, nullptr);
            
            if (performHandshake(client_socket)) {
                std::cout << "WebSocket connection established" << std::endl;
                handleClient(client_socket);
            }
            
            close(client_socket);
        }
    }

    void handleClient(int client_socket) {
        while (true) {
            WebSocketFrame frame = parseFrame(client_socket);
            
            if (frame.opcode == 0x8) { // Close frame
                std::cout << "Client requested close" << std::endl;
                break;
            } else if (frame.opcode == 0x9) { // Ping frame
                sendFrame(client_socket, 0xA, ""); // Send pong
            } else if (frame.opcode == 0x1) { // Text frame
                std::string message(frame.payload.begin(), frame.payload.end());
                std::cout << "Received: " << message << std::endl;
                
                // Echo the message back
                sendFrame(client_socket, 0x1, "Echo: " + message);
            }
        }
    }

    ~WebSocketServer() {
        if (server_fd >= 0) {
            close(server_fd);
        }
    }
};

int main() {
    WebSocketServer server;
    server.start();
    return 0;
}
```

## Rust Implementation

Here's a WebSocket implementation using Rust with the `tokio` and `tokio-tungstenite` libraries:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let addr = "127.0.0.1:8080";
    let listener = TcpListener::bind(addr).await?;
    println!("WebSocket server listening on: {}", addr);

    while let Ok((stream, peer_addr)) = listener.accept().await {
        println!("New connection from: {}", peer_addr);
        tokio::spawn(handle_connection(stream));
    }

    Ok(())
}

async fn handle_connection(stream: TcpStream) -> Result<(), Box<dyn Error>> {
    let ws_stream = accept_async(stream).await?;
    println!("WebSocket connection established");

    let (mut write, mut read) = ws_stream.split();

    while let Some(msg) = read.next().await {
        match msg {
            Ok(Message::Text(text)) => {
                println!("Received text: {}", text);
                let response = format!("Echo: {}", text);
                write.send(Message::Text(response)).await?;
            }
            Ok(Message::Binary(data)) => {
                println!("Received binary data: {} bytes", data.len());
                write.send(Message::Binary(data)).await?;
            }
            Ok(Message::Ping(data)) => {
                println!("Received ping");
                write.send(Message::Pong(data)).await?;
            }
            Ok(Message::Pong(_)) => {
                println!("Received pong");
            }
            Ok(Message::Close(frame)) => {
                println!("Received close frame: {:?}", frame);
                break;
            }
            Err(e) => {
                eprintln!("Error receiving message: {}", e);
                break;
            }
            _ => {}
        }
    }

    println!("Connection closed");
    Ok(())
}
```

Here's a more detailed Rust implementation showing frame parsing:

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use sha1::{Sha1, Digest};
use base64::{Engine as _, engine::general_purpose};
use std::error::Error;

const WS_MAGIC: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

#[derive(Debug)]
struct WebSocketFrame {
    fin: bool,
    opcode: u8,
    masked: bool,
    payload_length: u64,
    masking_key: Option<[u8; 4]>,
    payload: Vec<u8>,
}

async fn perform_handshake(stream: &mut TcpStream) -> Result<(), Box<dyn Error>> {
    let mut buffer = [0u8; 4096];
    let n = stream.read(&mut buffer).await?;
    let request = String::from_utf8_lossy(&buffer[..n]);

    // Extract Sec-WebSocket-Key
    let key = request
        .lines()
        .find(|line| line.starts_with("Sec-WebSocket-Key:"))
        .and_then(|line| line.split(": ").nth(1))
        .ok_or("Missing Sec-WebSocket-Key")?
        .trim();

    // Generate accept key
    let accept_key = generate_accept_key(key);

    let response = format!(
        "HTTP/1.1 101 Switching Protocols\r\n\
         Upgrade: websocket\r\n\
         Connection: Upgrade\r\n\
         Sec-WebSocket-Accept: {}\r\n\r\n",
        accept_key
    );

    stream.write_all(response.as_bytes()).await?;
    Ok(())
}

fn generate_accept_key(client_key: &str) -> String {
    let combined = format!("{}{}", client_key, WS_MAGIC);
    let mut hasher = Sha1::new();
    hasher.update(combined.as_bytes());
    let result = hasher.finalize();
    general_purpose::STANDARD.encode(&result)
}

async fn read_frame(stream: &mut TcpStream) -> Result<WebSocketFrame, Box<dyn Error>> {
    let mut header = [0u8; 2];
    stream.read_exact(&mut header).await?;

    let fin = (header[0] & 0x80) != 0;
    let opcode = header[0] & 0x0F;
    let masked = (header[1] & 0x80) != 0;
    let mut payload_length = (header[1] & 0x7F) as u64;

    // Handle extended payload length
    if payload_length == 126 {
        let mut extended = [0u8; 2];
        stream.read_exact(&mut extended).await?;
        payload_length = u16::from_be_bytes(extended) as u64;
    } else if payload_length == 127 {
        let mut extended = [0u8; 8];
        stream.read_exact(&mut extended).await?;
        payload_length = u64::from_be_bytes(extended);
    }

    // Read masking key
    let masking_key = if masked {
        let mut key = [0u8; 4];
        stream.read_exact(&mut key).await?;
        Some(key)
    } else {
        None
    };

    // Read payload
    let mut payload = vec![0u8; payload_length as usize];
    stream.read_exact(&mut payload).await?;

    // Unmask if necessary
    if let Some(key) = masking_key {
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= key[i % 4];
        }
    }

    Ok(WebSocketFrame {
        fin,
        opcode,
        masked,
        payload_length,
        masking_key,
        payload,
    })
}

async fn send_frame(
    stream: &mut TcpStream,
    opcode: u8,
    payload: &[u8],
) -> Result<(), Box<dyn Error>> {
    let mut frame = vec![];

    // First byte: FIN bit and opcode
    frame.push(0x80 | opcode);

    // Second byte and payload length
    let len = payload.len();
    if len < 126 {
        frame.push(len as u8);
    } else if len < 65536 {
        frame.push(126);
        frame.extend_from_slice(&(len as u16).to_be_bytes());
    } else {
        frame.push(127);
        frame.extend_from_slice(&(len as u64).to_be_bytes());
    }

    // Payload (server frames are not masked)
    frame.extend_from_slice(payload);

    stream.write_all(&frame).await?;
    Ok(())
}

async fn handle_client(mut stream: TcpStream) -> Result<(), Box<dyn Error>> {
    perform_handshake(&mut stream).await?;
    println!("WebSocket handshake complete");

    loop {
        let frame = read_frame(&mut stream).await?;

        match frame.opcode {
            0x1 => {
                // Text frame
                let text = String::from_utf8_lossy(&frame.payload);
                println!("Received: {}", text);
                let response = format!("Echo: {}", text);
                send_frame(&mut stream, 0x1, response.as_bytes()).await?;
            }
            0x8 => {
                // Close frame
                println!("Client closing connection");
                send_frame(&mut stream, 0x8, &[]).await?;
                break;
            }
            0x9 => {
                // Ping frame
                send_frame(&mut stream, 0xA, &frame.payload).await?;
            }
            _ => {}
        }
    }

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("WebSocket server listening on 127.0.0.1:8080");

    loop {
        let (stream, addr) = listener.accept().await?;
        println!("New connection from: {}", addr);

        tokio::spawn(async move {
            if let Err(e) = handle_client(stream).await {
                eprintln!("Error handling client: {}", e);
            }
        });
    }
}
```

## Summary

The WebSocket protocol revolutionizes real-time web communication by providing persistent, full-duplex connections over TCP. Unlike traditional HTTP, which requires a new connection for each request, WebSocket maintains a single open connection that allows both client and server to send messages independently.

**Key advantages include:**
- **Reduced latency**: No connection setup overhead for each message
- **Lower bandwidth**: Minimal frame headers (2-14 bytes) versus HTTP headers
- **Bidirectional**: Both parties can initiate communication
- **Real-time**: Ideal for chat applications, gaming, live updates, and collaborative tools

**The protocol consists of:**
1. An HTTP upgrade handshake using standard HTTP/1.1 mechanisms
2. A lightweight binary framing protocol with opcodes for text, binary, ping/pong, and connection control
3. Client-to-server masking requirement for security

Both C++ and Rust implementations demonstrate the core concepts: handshake negotiation, frame parsing with proper handling of variable-length payloads, masking/unmasking operations, and message routing. Modern applications typically use well-tested libraries like `tokio-tungstenite` in Rust or `libwebsockets` in C/C++ rather than implementing the protocol from scratch, but understanding the underlying mechanics is valuable for debugging and optimization.