# WebSocket Frame Structure and Opcodes

## Overview

WebSocket frames are the fundamental units of data transmission in the WebSocket protocol. Unlike HTTP, which uses text-based headers and bodies, WebSocket uses a compact binary framing protocol that allows efficient, low-overhead bidirectional communication. Understanding frame structure and opcodes is essential for implementing WebSocket clients and servers, debugging network issues, and building custom WebSocket libraries.

## WebSocket Frame Structure

A WebSocket frame consists of several components packed into a binary format. The minimum frame size is 2 bytes (for small unmasked frames), but frames can grow much larger depending on payload length and masking requirements.

### Frame Header Format

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

### Field Descriptions

**FIN (1 bit)**: Indicates if this is the final fragment in a message. When set to 1, this is the last fragment. When 0, more fragments follow.

**RSV1, RSV2, RSV3 (1 bit each)**: Reserved for extensions. Must be 0 unless an extension defines their use.

**Opcode (4 bits)**: Defines the interpretation of the payload data (see opcodes section below).

**MASK (1 bit)**: Indicates whether the payload is masked. Client-to-server frames MUST be masked (set to 1). Server-to-client frames MUST NOT be masked (set to 0).

**Payload Length (7 bits, 7+16 bits, or 7+64 bits)**: 
- If 0-125, this is the payload length
- If 126, the following 2 bytes contain the length as a 16-bit unsigned integer
- If 127, the following 8 bytes contain the length as a 64-bit unsigned integer

**Masking Key (0 or 4 bytes)**: Present if MASK bit is set. Used to XOR the payload data.

**Payload Data**: The actual application data, masked if the MASK bit is set.

## Opcodes

Opcodes define the type of frame being transmitted. They're divided into data frames and control frames.

### Data Frame Opcodes

- **0x0 (Continuation)**: Continuation frame for a fragmented message
- **0x1 (Text)**: Text data frame (UTF-8 encoded)
- **0x2 (Binary)**: Binary data frame

### Control Frame Opcodes

- **0x8 (Close)**: Connection close frame
- **0x9 (Ping)**: Ping frame for connection keepalive
- **0xA (Pong)**: Pong frame in response to ping

### Reserved Opcodes

- **0x3-0x7**: Reserved for future non-control frames
- **0xB-0xF**: Reserved for future control frames

### Control Frame Rules

Control frames have special restrictions:
1. Maximum payload of 125 bytes
2. Cannot be fragmented (FIN bit must be 1)
3. Can be injected between fragmented message frames

## C/C++ Implementation

Here's a comprehensive C++ implementation for parsing and creating WebSocket frames:

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <arpa/inet.h>

// WebSocket opcodes
enum class Opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket frame structure
struct WebSocketFrame {
    bool fin;
    bool rsv1, rsv2, rsv3;
    Opcode opcode;
    bool masked;
    uint64_t payload_length;
    uint32_t masking_key;
    std::vector<uint8_t> payload;
};

class WebSocketFrameParser {
public:
    // Parse a WebSocket frame from raw bytes
    static bool parseFrame(const uint8_t* data, size_t length, WebSocketFrame& frame) {
        if (length < 2) {
            return false; // Minimum frame size
        }

        size_t offset = 0;

        // First byte: FIN, RSV, Opcode
        uint8_t byte1 = data[offset++];
        frame.fin = (byte1 & 0x80) != 0;
        frame.rsv1 = (byte1 & 0x40) != 0;
        frame.rsv2 = (byte1 & 0x20) != 0;
        frame.rsv3 = (byte1 & 0x10) != 0;
        frame.opcode = static_cast<Opcode>(byte1 & 0x0F);

        // Second byte: MASK, Payload length
        uint8_t byte2 = data[offset++];
        frame.masked = (byte2 & 0x80) != 0;
        uint8_t payload_len = byte2 & 0x7F;

        // Extended payload length
        if (payload_len == 126) {
            if (length < offset + 2) return false;
            frame.payload_length = ntohs(*reinterpret_cast<const uint16_t*>(data + offset));
            offset += 2;
        } else if (payload_len == 127) {
            if (length < offset + 8) return false;
            frame.payload_length = be64toh(*reinterpret_cast<const uint64_t*>(data + offset));
            offset += 8;
        } else {
            frame.payload_length = payload_len;
        }

        // Masking key
        if (frame.masked) {
            if (length < offset + 4) return false;
            frame.masking_key = *reinterpret_cast<const uint32_t*>(data + offset);
            offset += 4;
        }

        // Payload data
        if (length < offset + frame.payload_length) {
            return false;
        }

        frame.payload.resize(frame.payload_length);
        std::memcpy(frame.payload.data(), data + offset, frame.payload_length);

        // Unmask payload if needed
        if (frame.masked) {
            unmaskPayload(frame.payload, frame.masking_key);
        }

        return true;
    }

    // Create a WebSocket frame
    static std::vector<uint8_t> createFrame(Opcode opcode, const uint8_t* payload, 
                                           size_t payload_len, bool fin = true, 
                                           bool mask = false) {
        std::vector<uint8_t> frame;

        // First byte: FIN, RSV, Opcode
        uint8_t byte1 = (fin ? 0x80 : 0x00) | static_cast<uint8_t>(opcode);
        frame.push_back(byte1);

        // Second byte: MASK, Payload length
        uint8_t byte2 = (mask ? 0x80 : 0x00);
        
        if (payload_len <= 125) {
            byte2 |= payload_len;
            frame.push_back(byte2);
        } else if (payload_len <= 65535) {
            byte2 |= 126;
            frame.push_back(byte2);
            uint16_t len = htons(payload_len);
            frame.insert(frame.end(), 
                        reinterpret_cast<uint8_t*>(&len),
                        reinterpret_cast<uint8_t*>(&len) + 2);
        } else {
            byte2 |= 127;
            frame.push_back(byte2);
            uint64_t len = htobe64(payload_len);
            frame.insert(frame.end(),
                        reinterpret_cast<uint8_t*>(&len),
                        reinterpret_cast<uint8_t*>(&len) + 8);
        }

        // Masking key (if needed)
        uint32_t masking_key = 0;
        if (mask) {
            masking_key = generateMaskingKey();
            frame.insert(frame.end(),
                        reinterpret_cast<uint8_t*>(&masking_key),
                        reinterpret_cast<uint8_t*>(&masking_key) + 4);
        }

        // Payload
        std::vector<uint8_t> masked_payload(payload, payload + payload_len);
        if (mask) {
            unmaskPayload(masked_payload, masking_key);
        }
        frame.insert(frame.end(), masked_payload.begin(), masked_payload.end());

        return frame;
    }

    // Create a text frame
    static std::vector<uint8_t> createTextFrame(const std::string& text, bool mask = false) {
        return createFrame(Opcode::TEXT, 
                          reinterpret_cast<const uint8_t*>(text.c_str()), 
                          text.length(), true, mask);
    }

    // Create a close frame
    static std::vector<uint8_t> createCloseFrame(uint16_t code, const std::string& reason = "") {
        std::vector<uint8_t> payload;
        uint16_t network_code = htons(code);
        payload.insert(payload.end(),
                      reinterpret_cast<uint8_t*>(&network_code),
                      reinterpret_cast<uint8_t*>(&network_code) + 2);
        payload.insert(payload.end(), reason.begin(), reason.end());
        
        return createFrame(Opcode::CLOSE, payload.data(), payload.size(), true, false);
    }

    // Create a ping frame
    static std::vector<uint8_t> createPingFrame(const uint8_t* data = nullptr, size_t len = 0) {
        return createFrame(Opcode::PING, data, len, true, false);
    }

    // Create a pong frame
    static std::vector<uint8_t> createPongFrame(const uint8_t* data = nullptr, size_t len = 0) {
        return createFrame(Opcode::PONG, data, len, true, false);
    }

private:
    // Unmask payload data
    static void unmaskPayload(std::vector<uint8_t>& payload, uint32_t masking_key) {
        uint8_t* key_bytes = reinterpret_cast<uint8_t*>(&masking_key);
        for (size_t i = 0; i < payload.size(); i++) {
            payload[i] ^= key_bytes[i % 4];
        }
    }

    // Generate a random masking key
    static uint32_t generateMaskingKey() {
        return static_cast<uint32_t>(rand());
    }
};

// Example usage
int main() {
    // Create a text frame
    std::string message = "Hello, WebSocket!";
    auto frame = WebSocketFrameParser::createTextFrame(message, true);

    std::cout << "Created frame with " << frame.size() << " bytes" << std::endl;

    // Parse the frame back
    WebSocketFrame parsed;
    if (WebSocketFrameParser::parseFrame(frame.data(), frame.size(), parsed)) {
        std::cout << "Parsed successfully!" << std::endl;
        std::cout << "FIN: " << parsed.fin << std::endl;
        std::cout << "Opcode: " << static_cast<int>(parsed.opcode) << std::endl;
        std::cout << "Masked: " << parsed.masked << std::endl;
        std::cout << "Payload: " << std::string(parsed.payload.begin(), 
                                                 parsed.payload.end()) << std::endl;
    }

    // Create a ping frame
    auto ping = WebSocketFrameParser::createPingFrame();
    std::cout << "Ping frame size: " << ping.size() << " bytes" << std::endl;

    // Create a close frame with reason
    auto close = WebSocketFrameParser::createCloseFrame(1000, "Normal closure");
    std::cout << "Close frame size: " << close.size() << " bytes" << std::endl;

    return 0;
}
```

## Rust Implementation

Here's a robust Rust implementation using idiomatic patterns:

```rust
use std::io::{self, Read, Write};
use rand::Rng;

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

impl Opcode {
    fn from_u8(value: u8) -> Result<Self, String> {
        match value {
            0x0 => Ok(Opcode::Continuation),
            0x1 => Ok(Opcode::Text),
            0x2 => Ok(Opcode::Binary),
            0x8 => Ok(Opcode::Close),
            0x9 => Ok(Opcode::Ping),
            0xA => Ok(Opcode::Pong),
            _ => Err(format!("Invalid opcode: {:#x}", value)),
        }
    }

    pub fn is_control(&self) -> bool {
        matches!(self, Opcode::Close | Opcode::Ping | Opcode::Pong)
    }
}

#[derive(Debug)]
pub struct WebSocketFrame {
    pub fin: bool,
    pub rsv1: bool,
    pub rsv2: bool,
    pub rsv3: bool,
    pub opcode: Opcode,
    pub masked: bool,
    pub payload: Vec<u8>,
}

impl WebSocketFrame {
    /// Parse a WebSocket frame from bytes
    pub fn parse(data: &[u8]) -> Result<Self, String> {
        if data.len() < 2 {
            return Err("Frame too short".to_string());
        }

        let mut offset = 0;

        // First byte: FIN, RSV, Opcode
        let byte1 = data[offset];
        offset += 1;

        let fin = (byte1 & 0x80) != 0;
        let rsv1 = (byte1 & 0x40) != 0;
        let rsv2 = (byte1 & 0x20) != 0;
        let rsv3 = (byte1 & 0x10) != 0;
        let opcode = Opcode::from_u8(byte1 & 0x0F)?;

        // Second byte: MASK, Payload length
        let byte2 = data[offset];
        offset += 1;

        let masked = (byte2 & 0x80) != 0;
        let mut payload_len = (byte2 & 0x7F) as u64;

        // Extended payload length
        if payload_len == 126 {
            if data.len() < offset + 2 {
                return Err("Incomplete extended payload length (16-bit)".to_string());
            }
            payload_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as u64;
            offset += 2;
        } else if payload_len == 127 {
            if data.len() < offset + 8 {
                return Err("Incomplete extended payload length (64-bit)".to_string());
            }
            let mut bytes = [0u8; 8];
            bytes.copy_from_slice(&data[offset..offset + 8]);
            payload_len = u64::from_be_bytes(bytes);
            offset += 8;
        }

        // Masking key
        let masking_key = if masked {
            if data.len() < offset + 4 {
                return Err("Incomplete masking key".to_string());
            }
            let key = [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]];
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

        // Unmask if necessary
        if let Some(key) = masking_key {
            Self::unmask_payload(&mut payload, &key);
        }

        Ok(WebSocketFrame {
            fin,
            rsv1,
            rsv2,
            rsv3,
            opcode,
            masked,
            payload,
        })
    }

    /// Create a new WebSocket frame
    pub fn new(opcode: Opcode, payload: Vec<u8>, fin: bool, mask: bool) -> Self {
        WebSocketFrame {
            fin,
            rsv1: false,
            rsv2: false,
            rsv3: false,
            opcode,
            masked: mask,
            payload,
        }
    }

    /// Serialize the frame to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut frame = Vec::new();

        // First byte: FIN, RSV, Opcode
        let mut byte1 = self.opcode as u8;
        if self.fin {
            byte1 |= 0x80;
        }
        if self.rsv1 {
            byte1 |= 0x40;
        }
        if self.rsv2 {
            byte1 |= 0x20;
        }
        if self.rsv3 {
            byte1 |= 0x10;
        }
        frame.push(byte1);

        // Second byte: MASK, Payload length
        let payload_len = self.payload.len();
        let mut byte2 = if self.masked { 0x80 } else { 0x00 };

        if payload_len <= 125 {
            byte2 |= payload_len as u8;
            frame.push(byte2);
        } else if payload_len <= 65535 {
            byte2 |= 126;
            frame.push(byte2);
            frame.extend_from_slice(&(payload_len as u16).to_be_bytes());
        } else {
            byte2 |= 127;
            frame.push(byte2);
            frame.extend_from_slice(&(payload_len as u64).to_be_bytes());
        }

        // Masking key and payload
        let mut payload = self.payload.clone();
        if self.masked {
            let masking_key = Self::generate_masking_key();
            frame.extend_from_slice(&masking_key);
            Self::unmask_payload(&mut payload, &masking_key);
        }

        frame.extend_from_slice(&payload);
        frame
    }

    /// Create a text frame
    pub fn text(text: &str, mask: bool) -> Self {
        Self::new(Opcode::Text, text.as_bytes().to_vec(), true, mask)
    }

    /// Create a binary frame
    pub fn binary(data: Vec<u8>, mask: bool) -> Self {
        Self::new(Opcode::Binary, data, true, mask)
    }

    /// Create a close frame
    pub fn close(code: u16, reason: &str) -> Self {
        let mut payload = Vec::new();
        payload.extend_from_slice(&code.to_be_bytes());
        payload.extend_from_slice(reason.as_bytes());
        Self::new(Opcode::Close, payload, true, false)
    }

    /// Create a ping frame
    pub fn ping(data: Option<&[u8]>) -> Self {
        let payload = data.map(|d| d.to_vec()).unwrap_or_default();
        Self::new(Opcode::Ping, payload, true, false)
    }

    /// Create a pong frame
    pub fn pong(data: Option<&[u8]>) -> Self {
        let payload = data.map(|d| d.to_vec()).unwrap_or_default();
        Self::new(Opcode::Pong, payload, true, false)
    }

    /// Unmask payload using XOR with masking key
    fn unmask_payload(payload: &mut [u8], masking_key: &[u8; 4]) {
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= masking_key[i % 4];
        }
    }

    /// Generate a random masking key
    fn generate_masking_key() -> [u8; 4] {
        let mut rng = rand::thread_rng();
        [rng.gen(), rng.gen(), rng.gen(), rng.gen()]
    }

    /// Get payload as UTF-8 string (for text frames)
    pub fn payload_as_string(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.payload.clone())
    }
}

// Example usage
fn main() {
    // Create a text frame
    let text_frame = WebSocketFrame::text("Hello, WebSocket!", true);
    let bytes = text_frame.to_bytes();
    println!("Text frame: {} bytes", bytes.len());

    // Parse the frame
    match WebSocketFrame::parse(&bytes) {
        Ok(parsed) => {
            println!("Parsed frame:");
            println!("  FIN: {}", parsed.fin);
            println!("  Opcode: {:?}", parsed.opcode);
            println!("  Masked: {}", parsed.masked);
            println!("  Payload: {}", parsed.payload_as_string().unwrap());
        }
        Err(e) => println!("Parse error: {}", e),
    }

    // Create a ping frame
    let ping_frame = WebSocketFrame::ping(Some(b"ping-data"));
    println!("Ping frame: {} bytes", ping_frame.to_bytes().len());

    // Create a close frame
    let close_frame = WebSocketFrame::close(1000, "Normal closure");
    println!("Close frame: {} bytes", close_frame.to_bytes().len());

    // Create a binary frame
    let binary_data = vec![0x01, 0x02, 0x03, 0x04, 0x05];
    let binary_frame = WebSocketFrame::binary(binary_data, false);
    println!("Binary frame: {} bytes", binary_frame.to_bytes().len());
}
```

## Summary

WebSocket frame structure and opcodes form the foundation of the WebSocket protocol's efficient binary communication system. The frame format uses a compact header design that minimizes overhead while supporting features like message fragmentation, masking, and control frames.

**Key takeaways:**

- **Frame anatomy**: Frames consist of a header (2-14 bytes) containing FIN bit, reserved bits, opcode, mask bit, payload length, optional masking key, and the actual payload data.

- **Opcodes**: Define message types—data frames (continuation, text, binary) for application data and control frames (close, ping, pong) for connection management.

- **Masking requirement**: Client-to-server frames must be masked using a random 32-bit key and XOR operation to prevent cache poisoning attacks. Server-to-client frames must not be masked.

- **Fragmentation support**: Large messages can be split across multiple frames using the continuation opcode, with the FIN bit indicating the final fragment.

- **Control frame constraints**: Limited to 125 bytes, cannot be fragmented, and can be interjected between data frame fragments for responsive connection management.

Understanding these concepts is crucial for implementing WebSocket libraries, debugging network issues, building protocol extensions, and optimizing real-time communication applications. The provided C++ and Rust implementations demonstrate practical approaches to parsing and constructing WebSocket frames with proper error handling and byte-level manipulation.