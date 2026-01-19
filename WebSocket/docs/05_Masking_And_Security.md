# WebSocket Masking and Security

## Overview

WebSocket masking is a critical security feature mandated by the WebSocket protocol (RFC 6455) that requires all frames sent from client to server to be masked with a randomly generated 32-bit masking key. This mechanism was specifically designed to prevent cache poisoning attacks and protect intermediary network infrastructure from being exploited through carefully crafted WebSocket frames.

## The Security Problem

Before understanding masking, it's essential to understand the vulnerability it addresses:

### Cache Poisoning Attack

In the early days of WebSocket development, researchers discovered that transparent proxies and caching servers could be exploited. An attacker could craft WebSocket frames that, when interpreted as HTTP traffic by naive intermediaries, would appear as valid HTTP responses. This could lead to:

- **Cache poisoning**: Malicious content being cached and served to other users
- **Request smuggling**: Bypassing security controls by confusing protocol boundaries
- **Cross-protocol attacks**: Exploiting differences in how clients and intermediaries parse data

By requiring clients to mask their frames with unpredictable keys, the protocol ensures that attackers cannot reliably craft frames that will be misinterpreted as HTTP by intermediaries.

## How Masking Works

### The Masking Algorithm

Masking is a reversible XOR operation applied to the payload data:

```
transformed-octet-i = original-octet-i XOR masking-key-octet-j
where j = i MOD 4
```

The masking key is a random 32-bit value included in the frame header. Each byte of the payload is XORed with one of the four masking key bytes in rotation.

### Frame Structure with Masking

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

## Protocol Requirements

1. **All client-to-server frames MUST be masked**
2. **Server-to-client frames MUST NOT be masked**
3. **The masking key MUST be unpredictable** (derived from a strong random source)
4. **Servers MUST close connections if they receive unmasked frames from clients**
5. **Clients MUST close connections if they receive masked frames from servers**

## Code Examples

### C/C++ Implementation

Here's a complete WebSocket masking implementation in C:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// Generate a random 32-bit masking key
uint32_t generate_masking_key() {
    // In production, use a cryptographically secure RNG
    // like /dev/urandom on Unix or CryptGenRandom on Windows
    return ((uint32_t)rand() << 16) | ((uint32_t)rand() & 0xFFFF);
}

// Apply masking to payload data
void apply_mask(uint8_t *payload, size_t length, uint32_t masking_key) {
    uint8_t mask_bytes[4];
    
    // Extract individual bytes from masking key
    mask_bytes[0] = (masking_key >> 24) & 0xFF;
    mask_bytes[1] = (masking_key >> 16) & 0xFF;
    mask_bytes[2] = (masking_key >> 8) & 0xFF;
    mask_bytes[3] = masking_key & 0xFF;
    
    // XOR each payload byte with the corresponding mask byte
    for (size_t i = 0; i < length; i++) {
        payload[i] ^= mask_bytes[i % 4];
    }
}

// Create a masked WebSocket frame (simplified, text frame only)
size_t create_masked_frame(const char *message, uint8_t *frame_buffer, size_t buffer_size) {
    size_t msg_len = strlen(message);
    size_t frame_size = 2 + 4 + msg_len; // header + mask + payload
    
    if (msg_len > 125 || frame_size > buffer_size) {
        return 0; // Simplified: only handling small payloads
    }
    
    // Byte 0: FIN=1, opcode=1 (text frame)
    frame_buffer[0] = 0x81;
    
    // Byte 1: MASK=1, payload length
    frame_buffer[1] = 0x80 | (uint8_t)msg_len;
    
    // Generate and store masking key (bytes 2-5)
    uint32_t masking_key = generate_masking_key();
    frame_buffer[2] = (masking_key >> 24) & 0xFF;
    frame_buffer[3] = (masking_key >> 16) & 0xFF;
    frame_buffer[4] = (masking_key >> 8) & 0xFF;
    frame_buffer[5] = masking_key & 0xFF;
    
    // Copy payload and apply mask (bytes 6+)
    memcpy(&frame_buffer[6], message, msg_len);
    apply_mask(&frame_buffer[6], msg_len, masking_key);
    
    return frame_size;
}

// Unmask received data (server-side)
void unmask_frame(uint8_t *payload, size_t length, uint32_t masking_key) {
    // Unmasking is the same operation as masking (XOR is reversible)
    apply_mask(payload, length, masking_key);
}

// Verify frame masking (server-side validation)
int validate_client_frame(uint8_t *frame, size_t frame_len) {
    if (frame_len < 2) {
        return 0; // Frame too short
    }
    
    uint8_t mask_bit = frame[1] & 0x80;
    
    if (!mask_bit) {
        fprintf(stderr, "ERROR: Client frame is not masked!\n");
        return 0; // Violation: client frames must be masked
    }
    
    return 1;
}

int main() {
    srand(time(NULL));
    
    const char *message = "Hello WebSocket!";
    uint8_t frame[256];
    
    printf("Original message: %s\n", message);
    
    // Create masked frame (client-side)
    size_t frame_size = create_masked_frame(message, frame, sizeof(frame));
    
    if (frame_size == 0) {
        fprintf(stderr, "Failed to create frame\n");
        return 1;
    }
    
    printf("Frame size: %zu bytes\n", frame_size);
    printf("Frame hex: ");
    for (size_t i = 0; i < frame_size; i++) {
        printf("%02X ", frame[i]);
    }
    printf("\n");
    
    // Validate frame (server-side)
    if (!validate_client_frame(frame, frame_size)) {
        return 1;
    }
    
    // Extract masking key and unmask (server-side)
    uint32_t masking_key = ((uint32_t)frame[2] << 24) |
                           ((uint32_t)frame[3] << 16) |
                           ((uint32_t)frame[4] << 8) |
                           ((uint32_t)frame[5]);
    
    unmask_frame(&frame[6], frame_size - 6, masking_key);
    
    printf("Unmasked message: %.*s\n", (int)(frame_size - 6), &frame[6]);
    
    return 0;
}
```

### C++ Implementation with Better Structure

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <cstring>
#include <stdexcept>

class WebSocketFrame {
private:
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<uint32_t> dis;
    
public:
    WebSocketFrame() : gen(rd()), dis(0, UINT32_MAX) {}
    
    // Generate cryptographically random masking key
    uint32_t generateMaskingKey() {
        return dis(gen);
    }
    
    // Apply/remove mask (XOR operation)
    void applyMask(std::vector<uint8_t>& data, uint32_t maskingKey) {
        uint8_t maskBytes[4] = {
            static_cast<uint8_t>((maskingKey >> 24) & 0xFF),
            static_cast<uint8_t>((maskingKey >> 16) & 0xFF),
            static_cast<uint8_t>((maskingKey >> 8) & 0xFF),
            static_cast<uint8_t>(maskingKey & 0xFF)
        };
        
        for (size_t i = 0; i < data.size(); i++) {
            data[i] ^= maskBytes[i % 4];
        }
    }
    
    // Create masked client frame
    std::vector<uint8_t> createMaskedFrame(const std::string& message) {
        std::vector<uint8_t> frame;
        size_t msgLen = message.length();
        
        // Byte 0: FIN=1, RSV=0, opcode=1 (text)
        frame.push_back(0x81);
        
        // Byte 1: MASK=1, payload length
        if (msgLen <= 125) {
            frame.push_back(0x80 | static_cast<uint8_t>(msgLen));
        } else if (msgLen <= 65535) {
            frame.push_back(0x80 | 126);
            frame.push_back((msgLen >> 8) & 0xFF);
            frame.push_back(msgLen & 0xFF);
        } else {
            frame.push_back(0x80 | 127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((msgLen >> (i * 8)) & 0xFF);
            }
        }
        
        // Generate and add masking key
        uint32_t maskingKey = generateMaskingKey();
        frame.push_back((maskingKey >> 24) & 0xFF);
        frame.push_back((maskingKey >> 16) & 0xFF);
        frame.push_back((maskingKey >> 8) & 0xFF);
        frame.push_back(maskingKey & 0xFF);
        
        // Add payload
        std::vector<uint8_t> payload(message.begin(), message.end());
        applyMask(payload, maskingKey);
        frame.insert(frame.end(), payload.begin(), payload.end());
        
        return frame;
    }
    
    // Server-side: validate and unmask frame
    std::string unmaskClientFrame(const std::vector<uint8_t>& frame) {
        if (frame.size() < 6) {
            throw std::runtime_error("Frame too small");
        }
        
        // Check MASK bit
        if (!(frame[1] & 0x80)) {
            throw std::runtime_error("Client frame must be masked!");
        }
        
        size_t payloadLen = frame[1] & 0x7F;
        size_t maskOffset = 2;
        
        if (payloadLen == 126) {
            payloadLen = (frame[2] << 8) | frame[3];
            maskOffset = 4;
        } else if (payloadLen == 127) {
            payloadLen = 0;
            for (int i = 0; i < 8; i++) {
                payloadLen = (payloadLen << 8) | frame[2 + i];
            }
            maskOffset = 10;
        }
        
        // Extract masking key
        uint32_t maskingKey = (frame[maskOffset] << 24) |
                              (frame[maskOffset + 1] << 16) |
                              (frame[maskOffset + 2] << 8) |
                              frame[maskOffset + 3];
        
        // Extract and unmask payload
        std::vector<uint8_t> payload(
            frame.begin() + maskOffset + 4,
            frame.begin() + maskOffset + 4 + payloadLen
        );
        
        applyMask(payload, maskingKey);
        
        return std::string(payload.begin(), payload.end());
    }
};

int main() {
    WebSocketFrame wsFrame;
    
    std::string message = "Secure WebSocket message!";
    std::cout << "Original: " << message << std::endl;
    
    // Client: Create masked frame
    auto frame = wsFrame.createMaskedFrame(message);
    
    std::cout << "Frame (" << frame.size() << " bytes): ";
    for (auto byte : frame) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;
    
    // Server: Unmask and validate
    try {
        std::string unmasked = wsFrame.unmaskClientFrame(frame);
        std::cout << "Unmasked: " << unmasked << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use rand::Rng;
use std::io::{self, Write};

#[derive(Debug)]
pub enum WebSocketError {
    InvalidFrame,
    UnmaskedClientFrame,
    FrameTooSmall,
}

pub struct WebSocketFrame {
    rng: rand::rngs::ThreadRng,
}

impl WebSocketFrame {
    pub fn new() -> Self {
        WebSocketFrame {
            rng: rand::thread_rng(),
        }
    }
    
    /// Generate a random 32-bit masking key
    fn generate_masking_key(&mut self) -> u32 {
        self.rng.gen::<u32>()
    }
    
    /// Apply or remove masking (XOR operation is reversible)
    fn apply_mask(data: &mut [u8], masking_key: u32) {
        let mask_bytes = [
            ((masking_key >> 24) & 0xFF) as u8,
            ((masking_key >> 16) & 0xFF) as u8,
            ((masking_key >> 8) & 0xFF) as u8,
            (masking_key & 0xFF) as u8,
        ];
        
        for (i, byte) in data.iter_mut().enumerate() {
            *byte ^= mask_bytes[i % 4];
        }
    }
    
    /// Create a masked WebSocket frame (client-side)
    pub fn create_masked_frame(&mut self, message: &str) -> Vec<u8> {
        let payload = message.as_bytes();
        let payload_len = payload.len();
        let mut frame = Vec::new();
        
        // Byte 0: FIN=1, RSV=0, opcode=1 (text frame)
        frame.push(0x81);
        
        // Byte 1+: MASK=1, payload length
        if payload_len <= 125 {
            frame.push(0x80 | (payload_len as u8));
        } else if payload_len <= 65535 {
            frame.push(0x80 | 126);
            frame.push((payload_len >> 8) as u8);
            frame.push((payload_len & 0xFF) as u8);
        } else {
            frame.push(0x80 | 127);
            frame.extend_from_slice(&(payload_len as u64).to_be_bytes());
        }
        
        // Generate and add masking key
        let masking_key = self.generate_masking_key();
        frame.extend_from_slice(&masking_key.to_be_bytes());
        
        // Add masked payload
        let mut masked_payload = payload.to_vec();
        Self::apply_mask(&mut masked_payload, masking_key);
        frame.extend_from_slice(&masked_payload);
        
        frame
    }
    
    /// Unmask a client frame (server-side)
    pub fn unmask_client_frame(frame: &[u8]) -> Result<String, WebSocketError> {
        if frame.len() < 6 {
            return Err(WebSocketError::FrameTooSmall);
        }
        
        // Check MASK bit (must be set for client frames)
        if frame[1] & 0x80 == 0 {
            return Err(WebSocketError::UnmaskedClientFrame);
        }
        
        // Parse payload length
        let mut payload_len = (frame[1] & 0x7F) as usize;
        let mut mask_offset = 2;
        
        if payload_len == 126 {
            if frame.len() < 8 {
                return Err(WebSocketError::FrameTooSmall);
            }
            payload_len = ((frame[2] as usize) << 8) | (frame[3] as usize);
            mask_offset = 4;
        } else if payload_len == 127 {
            if frame.len() < 14 {
                return Err(WebSocketError::FrameTooSmall);
            }
            payload_len = 0;
            for i in 0..8 {
                payload_len = (payload_len << 8) | (frame[2 + i] as usize);
            }
            mask_offset = 10;
        }
        
        // Extract masking key
        if frame.len() < mask_offset + 4 + payload_len {
            return Err(WebSocketError::FrameTooSmall);
        }
        
        let masking_key = u32::from_be_bytes([
            frame[mask_offset],
            frame[mask_offset + 1],
            frame[mask_offset + 2],
            frame[mask_offset + 3],
        ]);
        
        // Extract and unmask payload
        let payload_start = mask_offset + 4;
        let mut payload = frame[payload_start..payload_start + payload_len].to_vec();
        Self::apply_mask(&mut payload, masking_key);
        
        String::from_utf8(payload).map_err(|_| WebSocketError::InvalidFrame)
    }
    
    /// Validate that a frame from client is properly masked
    pub fn validate_client_frame(frame: &[u8]) -> Result<(), WebSocketError> {
        if frame.len() < 2 {
            return Err(WebSocketError::FrameTooSmall);
        }
        
        if frame[1] & 0x80 == 0 {
            return Err(WebSocketError::UnmaskedClientFrame);
        }
        
        Ok(())
    }
}

fn main() {
    let mut ws_frame = WebSocketFrame::new();
    
    let message = "Hello from Rust WebSocket!";
    println!("Original message: {}", message);
    
    // Client: Create masked frame
    let frame = ws_frame.create_masked_frame(message);
    
    print!("Frame ({} bytes): ", frame.len());
    for byte in &frame {
        print!("{:02X} ", byte);
    }
    println!();
    
    // Server: Validate and unmask
    match WebSocketFrame::validate_client_frame(&frame) {
        Ok(_) => println!("✓ Frame validation passed"),
        Err(e) => {
            eprintln!("✗ Frame validation failed: {:?}", e);
            return;
        }
    }
    
    match WebSocketFrame::unmask_client_frame(&frame) {
        Ok(unmasked) => println!("Unmasked message: {}", unmasked),
        Err(e) => eprintln!("Error unmasking: {:?}", e),
    }
}
```

### Advanced Rust Example: Complete Frame Parser

```rust
use std::io::{self, Read, Write};

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
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x0 => Some(OpCode::Continuation),
            0x1 => Some(OpCode::Text),
            0x2 => Some(OpCode::Binary),
            0x8 => Some(OpCode::Close),
            0x9 => Some(OpCode::Ping),
            0xA => Some(OpCode::Pong),
            _ => None,
        }
    }
}

pub struct Frame {
    pub fin: bool,
    pub opcode: OpCode,
    pub masked: bool,
    pub payload: Vec<u8>,
}

impl Frame {
    /// Parse a WebSocket frame with security validation
    pub fn parse(data: &[u8]) -> io::Result<Self> {
        if data.len() < 2 {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "Frame too short"));
        }
        
        let fin = (data[0] & 0x80) != 0;
        let opcode = OpCode::from_u8(data[0] & 0x0F)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "Invalid opcode"))?;
        
        let masked = (data[1] & 0x80) != 0;
        let mut payload_len = (data[1] & 0x7F) as usize;
        let mut offset = 2;
        
        // Parse extended payload length
        if payload_len == 126 {
            if data.len() < 4 {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "Incomplete length"));
            }
            payload_len = ((data[2] as usize) << 8) | (data[3] as usize);
            offset = 4;
        } else if payload_len == 127 {
            if data.len() < 10 {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "Incomplete length"));
            }
            payload_len = 0;
            for i in 0..8 {
                payload_len = (payload_len << 8) | (data[2 + i] as usize);
            }
            offset = 10;
        }
        
        let mut payload = if masked {
            if data.len() < offset + 4 {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "Missing mask"));
            }
            
            let masking_key = u32::from_be_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]);
            offset += 4;
            
            if data.len() < offset + payload_len {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "Incomplete payload"));
            }
            
            let mut payload = data[offset..offset + payload_len].to_vec();
            Self::apply_mask(&mut payload, masking_key);
            payload
        } else {
            if data.len() < offset + payload_len {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "Incomplete payload"));
            }
            data[offset..offset + payload_len].to_vec()
        };
        
        Ok(Frame {
            fin,
            opcode,
            masked,
            payload,
        })
    }
    
    fn apply_mask(data: &mut [u8], masking_key: u32) {
        let mask_bytes = masking_key.to_be_bytes();
        for (i, byte) in data.iter_mut().enumerate() {
            *byte ^= mask_bytes[i % 4];
        }
    }
}
```

## Security Best Practices

### 1. **Always Use Cryptographically Secure Random Number Generators**

```c
// Bad - Predictable
uint32_t weak_key = rand();

// Good - Use system entropy
// On Unix/Linux:
int fd = open("/dev/urandom", O_RDONLY);
read(fd, &masking_key, sizeof(masking_key));
close(fd);

// On Windows:
// Use CryptGenRandom or BCryptGenRandom
```

### 2. **Validate All Incoming Frames**

```rust
fn handle_client_frame(frame_data: &[u8]) -> Result<(), WebSocketError> {
    // MUST check mask bit
    if frame_data[1] & 0x80 == 0 {
        return Err(WebSocketError::UnmaskedClientFrame);
    }
    
    // Process frame...
    Ok(())
}
```

### 3. **Never Reuse Masking Keys**

Each frame must have a unique, unpredictable masking key.

### 4. **Close Connections on Protocol Violations**

```rust
if let Err(e) = validate_frame(&frame) {
    send_close_frame(1002, "Protocol error");
    close_connection();
}
```

## Summary

WebSocket masking is a mandatory security feature that protects against cache poisoning and protocol-level attacks by requiring clients to XOR their frame payloads with unpredictable 32-bit keys. Key points include:

- **Client frames must always be masked** with a random key included in the frame header
- **Server frames must never be masked** to maintain asymmetry in the protocol
- **Masking uses XOR operation**, making it reversible and computationally inexpensive
- **Security depends on unpredictability** of masking keys, requiring cryptographically secure random generation
- **Protocol violations must result in connection closure** to maintain security guarantees
- **The primary threat model** addresses cache poisoning through transparent proxies and intermediaries

Implementations must strictly enforce these rules on both client and server sides. The masking mechanism adds minimal overhead while providing critical protection against cross-protocol attacks that could compromise network infrastructure. Proper validation of the MASK bit and rejection of non-compliant frames are essential for maintaining the security properties of the WebSocket protocol.