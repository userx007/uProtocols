# RFC 6455 Compliance: WebSocket Protocol Specification

## Overview

RFC 6455 is the official IETF specification that defines the WebSocket protocol. Published in December 2011, it establishes the standard for full-duplex communication channels over a single TCP connection between clients and servers. Understanding and implementing RFC 6455 compliance is essential for creating interoperable WebSocket applications that work correctly across different implementations.

## Core Protocol Components

### The WebSocket Handshake

The WebSocket connection begins with an HTTP upgrade request. The client sends a specially crafted HTTP request, and if the server agrees, it responds with an upgrade confirmation, establishing the WebSocket connection.

**Client Handshake Request:**
```
GET /chat HTTP/1.1
Host: server.example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

**Server Handshake Response:**
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

The `Sec-WebSocket-Accept` value is computed by concatenating the client's `Sec-WebSocket-Key` with the magic string "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", then taking the SHA-1 hash and base64-encoding it.

### Frame Structure

WebSocket data is transmitted in frames. Each frame has a specific structure defined by RFC 6455:

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

**Key fields:**
- **FIN**: Indicates if this is the final fragment (1) or not (0)
- **Opcode**: Defines the frame type (0x0=continuation, 0x1=text, 0x2=binary, 0x8=close, 0x9=ping, 0xA=pong)
- **MASK**: Indicates if payload is masked (required for client-to-server)
- **Payload length**: Length of the payload data

## C Implementation Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Calculate the Sec-WebSocket-Accept value
char* calculate_accept_key(const char* client_key) {
    char combined[256];
    unsigned char hash[SHA_DIGEST_LENGTH];
    char* accept_key;
    int len;
    
    // Concatenate client key with GUID
    snprintf(combined, sizeof(combined), "%s%s", client_key, WS_GUID);
    
    // Calculate SHA-1 hash
    SHA1((unsigned char*)combined, strlen(combined), hash);
    
    // Base64 encode
    accept_key = malloc(128);
    len = EVP_EncodeBlock((unsigned char*)accept_key, hash, SHA_DIGEST_LENGTH);
    accept_key[len] = '\0';
    
    return accept_key;
}

// Frame opcodes
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

typedef struct {
    int fin;
    int rsv1, rsv2, rsv3;
    ws_opcode_t opcode;
    int masked;
    uint64_t payload_length;
    unsigned char masking_key[4];
    unsigned char* payload;
} ws_frame_t;

// Parse WebSocket frame header
int parse_frame_header(const unsigned char* data, size_t len, ws_frame_t* frame) {
    if (len < 2) return -1;
    
    // First byte
    frame->fin = (data[0] & 0x80) >> 7;
    frame->rsv1 = (data[0] & 0x40) >> 6;
    frame->rsv2 = (data[0] & 0x20) >> 5;
    frame->rsv3 = (data[0] & 0x10) >> 4;
    frame->opcode = data[0] & 0x0F;
    
    // Second byte
    frame->masked = (data[1] & 0x80) >> 7;
    uint8_t payload_len = data[1] & 0x7F;
    
    size_t offset = 2;
    
    // Extended payload length
    if (payload_len == 126) {
        if (len < 4) return -1;
        frame->payload_length = (data[2] << 8) | data[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return -1;
        frame->payload_length = 0;
        for (int i = 0; i < 8; i++) {
            frame->payload_length = (frame->payload_length << 8) | data[2 + i];
        }
        offset = 10;
    } else {
        frame->payload_length = payload_len;
    }
    
    // Masking key
    if (frame->masked) {
        if (len < offset + 4) return -1;
        memcpy(frame->masking_key, data + offset, 4);
        offset += 4;
    }
    
    return offset;
}

// Unmask payload data
void unmask_payload(unsigned char* payload, size_t len, const unsigned char* mask) {
    for (size_t i = 0; i < len; i++) {
        payload[i] ^= mask[i % 4];
    }
}

// Create a WebSocket frame
size_t create_frame(unsigned char* buffer, size_t buffer_size,
                   ws_opcode_t opcode, const unsigned char* payload, 
                   size_t payload_len, int fin) {
    size_t offset = 0;
    
    // First byte: FIN and opcode
    buffer[offset++] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
    
    // Second byte: MASK (0 for server) and payload length
    if (payload_len < 126) {
        buffer[offset++] = payload_len & 0x7F;
    } else if (payload_len < 65536) {
        buffer[offset++] = 126;
        buffer[offset++] = (payload_len >> 8) & 0xFF;
        buffer[offset++] = payload_len & 0xFF;
    } else {
        buffer[offset++] = 127;
        for (int i = 7; i >= 0; i--) {
            buffer[offset++] = (payload_len >> (i * 8)) & 0xFF;
        }
    }
    
    // Payload
    if (payload && payload_len > 0) {
        memcpy(buffer + offset, payload, payload_len);
        offset += payload_len;
    }
    
    return offset;
}

int main() {
    // Example: Calculate accept key
    const char* client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char* accept_key = calculate_accept_key(client_key);
    printf("Sec-WebSocket-Accept: %s\n", accept_key);
    free(accept_key);
    
    // Example: Create a text frame
    unsigned char frame_buffer[1024];
    const char* message = "Hello, WebSocket!";
    size_t frame_size = create_frame(frame_buffer, sizeof(frame_buffer),
                                     WS_OPCODE_TEXT, 
                                     (unsigned char*)message, 
                                     strlen(message), 1);
    
    printf("Created frame of %zu bytes\n", frame_size);
    
    return 0;
}
```

## C++ Implementation Example

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <stdexcept>

class WebSocketFrame {
public:
    enum Opcode {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
    
private:
    bool fin_;
    bool rsv1_, rsv2_, rsv3_;
    Opcode opcode_;
    bool masked_;
    uint64_t payload_length_;
    uint8_t masking_key_[4];
    std::vector<uint8_t> payload_;
    
public:
    WebSocketFrame() : fin_(true), rsv1_(false), rsv2_(false), rsv3_(false),
                       opcode_(TEXT), masked_(false), payload_length_(0) {
        std::memset(masking_key_, 0, 4);
    }
    
    // Parse frame from raw data
    static WebSocketFrame parse(const std::vector<uint8_t>& data) {
        WebSocketFrame frame;
        
        if (data.size() < 2) {
            throw std::runtime_error("Invalid frame: too short");
        }
        
        // Parse first byte
        frame.fin_ = (data[0] & 0x80) != 0;
        frame.rsv1_ = (data[0] & 0x40) != 0;
        frame.rsv2_ = (data[0] & 0x20) != 0;
        frame.rsv3_ = (data[0] & 0x10) != 0;
        frame.opcode_ = static_cast<Opcode>(data[0] & 0x0F);
        
        // Parse second byte
        frame.masked_ = (data[1] & 0x80) != 0;
        uint8_t payload_len = data[1] & 0x7F;
        
        size_t offset = 2;
        
        // Extended payload length
        if (payload_len == 126) {
            if (data.size() < 4) throw std::runtime_error("Invalid frame length");
            frame.payload_length_ = (data[2] << 8) | data[3];
            offset = 4;
        } else if (payload_len == 127) {
            if (data.size() < 10) throw std::runtime_error("Invalid frame length");
            frame.payload_length_ = 0;
            for (int i = 0; i < 8; i++) {
                frame.payload_length_ = (frame.payload_length_ << 8) | data[2 + i];
            }
            offset = 10;
        } else {
            frame.payload_length_ = payload_len;
        }
        
        // Masking key
        if (frame.masked_) {
            if (data.size() < offset + 4) throw std::runtime_error("Invalid frame");
            std::memcpy(frame.masking_key_, &data[offset], 4);
            offset += 4;
        }
        
        // Payload
        if (data.size() < offset + frame.payload_length_) {
            throw std::runtime_error("Incomplete payload");
        }
        
        frame.payload_.assign(data.begin() + offset, 
                             data.begin() + offset + frame.payload_length_);
        
        // Unmask if necessary
        if (frame.masked_) {
            for (size_t i = 0; i < frame.payload_.size(); i++) {
                frame.payload_[i] ^= frame.masking_key_[i % 4];
            }
        }
        
        return frame;
    }
    
    // Create frame bytes
    std::vector<uint8_t> toBytes(bool mask = false) const {
        std::vector<uint8_t> result;
        
        // First byte
        uint8_t byte1 = (fin_ ? 0x80 : 0x00) | 
                       (rsv1_ ? 0x40 : 0x00) |
                       (rsv2_ ? 0x20 : 0x00) |
                       (rsv3_ ? 0x10 : 0x00) |
                       (static_cast<uint8_t>(opcode_) & 0x0F);
        result.push_back(byte1);
        
        // Second byte and length
        uint64_t len = payload_.size();
        if (len < 126) {
            result.push_back((mask ? 0x80 : 0x00) | static_cast<uint8_t>(len));
        } else if (len < 65536) {
            result.push_back((mask ? 0x80 : 0x00) | 126);
            result.push_back((len >> 8) & 0xFF);
            result.push_back(len & 0xFF);
        } else {
            result.push_back((mask ? 0x80 : 0x00) | 127);
            for (int i = 7; i >= 0; i--) {
                result.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        // Masking key (if needed)
        if (mask) {
            for (int i = 0; i < 4; i++) {
                result.push_back(masking_key_[i]);
            }
        }
        
        // Payload
        result.insert(result.end(), payload_.begin(), payload_.end());
        
        return result;
    }
    
    void setPayload(const std::string& data) {
        payload_.assign(data.begin(), data.end());
        payload_length_ = payload_.size();
    }
    
    std::string getPayloadAsString() const {
        return std::string(payload_.begin(), payload_.end());
    }
    
    void setOpcode(Opcode op) { opcode_ = op; }
    Opcode getOpcode() const { return opcode_; }
    bool isFin() const { return fin_; }
};

// WebSocket handshake utilities
class WebSocketHandshake {
public:
    static std::string calculateAcceptKey(const std::string& client_key) {
        const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string combined = client_key + magic;
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), 
             combined.length(), hash);
        
        // Base64 encode
        char encoded[128];
        int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded), 
                                  hash, SHA_DIGEST_LENGTH);
        return std::string(encoded, len);
    }
};

int main() {
    // Example: Calculate accept key
    std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept_key = WebSocketHandshake::calculateAcceptKey(client_key);
    std::cout << "Sec-WebSocket-Accept: " << accept_key << std::endl;
    
    // Example: Create and serialize a frame
    WebSocketFrame frame;
    frame.setOpcode(WebSocketFrame::TEXT);
    frame.setPayload("Hello, WebSocket!");
    
    std::vector<uint8_t> frame_bytes = frame.toBytes();
    std::cout << "Frame size: " << frame_bytes.size() << " bytes" << std::endl;
    
    return 0;
}
```

## Rust Implementation Example

```rust
use sha1::{Sha1, Digest};
use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};

const WS_GUID: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Calculate WebSocket accept key
pub fn calculate_accept_key(client_key: &str) -> String {
    let mut hasher = Sha1::new();
    hasher.update(client_key.as_bytes());
    hasher.update(WS_GUID.as_bytes());
    let hash = hasher.finalize();
    BASE64.encode(&hash)
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OpCode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

impl OpCode {
    fn from_u8(byte: u8) -> Result<Self, String> {
        match byte {
            0x0 => Ok(OpCode::Continuation),
            0x1 => Ok(OpCode::Text),
            0x2 => Ok(OpCode::Binary),
            0x8 => Ok(OpCode::Close),
            0x9 => Ok(OpCode::Ping),
            0xA => Ok(OpCode::Pong),
            _ => Err(format!("Invalid opcode: {}", byte)),
        }
    }
}

#[derive(Debug)]
pub struct WebSocketFrame {
    fin: bool,
    rsv1: bool,
    rsv2: bool,
    rsv3: bool,
    opcode: OpCode,
    masked: bool,
    payload_length: u64,
    masking_key: Option<[u8; 4]>,
    payload: Vec<u8>,
}

impl WebSocketFrame {
    pub fn new(opcode: OpCode, payload: Vec<u8>) -> Self {
        WebSocketFrame {
            fin: true,
            rsv1: false,
            rsv2: false,
            rsv3: false,
            opcode,
            masked: false,
            payload_length: payload.len() as u64,
            masking_key: None,
            payload,
        }
    }
    
    // Parse frame from bytes
    pub fn parse(data: &[u8]) -> Result<Self, String> {
        if data.len() < 2 {
            return Err("Frame too short".to_string());
        }
        
        // First byte
        let fin = (data[0] & 0x80) != 0;
        let rsv1 = (data[0] & 0x40) != 0;
        let rsv2 = (data[0] & 0x20) != 0;
        let rsv3 = (data[0] & 0x10) != 0;
        let opcode = OpCode::from_u8(data[0] & 0x0F)?;
        
        // Second byte
        let masked = (data[1] & 0x80) != 0;
        let mut payload_len = (data[1] & 0x7F) as u64;
        
        let mut offset = 2;
        
        // Extended payload length
        if payload_len == 126 {
            if data.len() < 4 {
                return Err("Invalid frame length".to_string());
            }
            payload_len = u16::from_be_bytes([data[2], data[3]]) as u64;
            offset = 4;
        } else if payload_len == 127 {
            if data.len() < 10 {
                return Err("Invalid frame length".to_string());
            }
            payload_len = u64::from_be_bytes([
                data[2], data[3], data[4], data[5],
                data[6], data[7], data[8], data[9],
            ]);
            offset = 10;
        }
        
        // Masking key
        let masking_key = if masked {
            if data.len() < offset + 4 {
                return Err("Invalid frame".to_string());
            }
            let key = [data[offset], data[offset + 1], 
                      data[offset + 2], data[offset + 3]];
            offset += 4;
            Some(key)
        } else {
            None
        };
        
        // Payload
        if data.len() < offset + payload_len as usize {
            return Err("Incomplete payload".to_string());
        }
        
        let mut payload = data[offset..offset + payload_len as usize].to_vec();
        
        // Unmask payload
        if let Some(key) = masking_key {
            for (i, byte) in payload.iter_mut().enumerate() {
                *byte ^= key[i % 4];
            }
        }
        
        Ok(WebSocketFrame {
            fin,
            rsv1,
            rsv2,
            rsv3,
            opcode,
            masked,
            payload_length: payload_len,
            masking_key,
            payload,
        })
    }
    
    // Serialize frame to bytes
    pub fn to_bytes(&self, mask: bool) -> Vec<u8> {
        let mut result = Vec::new();
        
        // First byte
        let byte1 = (if self.fin { 0x80 } else { 0x00 })
            | (if self.rsv1 { 0x40 } else { 0x00 })
            | (if self.rsv2 { 0x20 } else { 0x00 })
            | (if self.rsv3 { 0x10 } else { 0x00 })
            | (self.opcode as u8 & 0x0F);
        result.push(byte1);
        
        // Second byte and length
        let len = self.payload.len() as u64;
        if len < 126 {
            result.push((if mask { 0x80 } else { 0x00 }) | len as u8);
        } else if len < 65536 {
            result.push((if mask { 0x80 } else { 0x00 }) | 126);
            result.extend_from_slice(&(len as u16).to_be_bytes());
        } else {
            result.push((if mask { 0x80 } else { 0x00 }) | 127);
            result.extend_from_slice(&len.to_be_bytes());
        }
        
        // Masking key (if needed)
        if mask {
            if let Some(key) = self.masking_key {
                result.extend_from_slice(&key);
            }
        }
        
        // Payload
        result.extend_from_slice(&self.payload);
        
        result
    }
    
    pub fn get_payload_as_string(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.payload.clone())
    }
    
    pub fn opcode(&self) -> OpCode {
        self.opcode
    }
    
    pub fn is_fin(&self) -> bool {
        self.fin
    }
}

// Example usage
fn main() {
    // Calculate accept key
    let client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    let accept_key = calculate_accept_key(client_key);
    println!("Sec-WebSocket-Accept: {}", accept_key);
    
    // Create a text frame
    let message = "Hello, WebSocket!".as_bytes().to_vec();
    let frame = WebSocketFrame::new(OpCode::Text, message);
    
    let frame_bytes = frame.to_bytes(false);
    println!("Frame size: {} bytes", frame_bytes.len());
    
    // Parse the frame back
    match WebSocketFrame::parse(&frame_bytes) {
        Ok(parsed) => {
            println!("Parsed frame opcode: {:?}", parsed.opcode());
            if let Ok(text) = parsed.get_payload_as_string() {
                println!("Payload: {}", text);
            }
        }
        Err(e) => println!("Parse error: {}", e),
    }
}
```

## Summary

**RFC 6455 compliance** is fundamental to building reliable WebSocket implementations. The specification defines every aspect of the protocol, from the HTTP upgrade handshake to the binary frame format used for data transmission. Key compliance requirements include:

- **Proper handshake**: Correctly computing the `Sec-WebSocket-Accept` value using SHA-1 and base64 encoding
- **Frame structure**: Accurately parsing and constructing frames with correct FIN, opcode, masking, and length fields
- **Masking requirements**: Client-to-server frames must be masked; server-to-client frames must not be
- **Opcodes**: Supporting continuation (0x0), text (0x1), binary (0x2), close (0x8), ping (0x9), and pong (0xA) frames
- **Fragmentation**: Handling messages split across multiple frames using continuation frames
- **Control frames**: Properly handling ping/pong for keepalive and close frames for graceful shutdown

The examples provided demonstrate core RFC 6455 operations in C, C++, and Rust, including handshake key calculation, frame parsing/creation, and payload masking/unmasking. These foundational operations enable building full-featured WebSocket servers and clients that interoperate correctly with any compliant implementation.