# Binary vs Text Frames in WebSocket

## Overview

WebSocket protocol distinguishes between two primary data frame types: **text frames** and **binary frames**. This distinction is fundamental to the protocol's design and affects how data is transmitted, validated, and processed on both client and server sides.

## Understanding Frame Types

### Text Frames
Text frames carry UTF-8 encoded textual data. The WebSocket protocol mandates strict UTF-8 validation for text frames. If invalid UTF-8 sequences are detected, the connection must be closed with an appropriate error code (1007 - Invalid frame payload data).

**Key characteristics:**
- Must contain valid UTF-8 encoded data
- Used for JSON, XML, plain text, and other text-based protocols
- Requires UTF-8 validation on reception
- Opcode: 0x1

### Binary Frames
Binary frames carry arbitrary binary data without any encoding constraints. No validation is performed on the payload content.

**Key characteristics:**
- Can contain any byte sequence
- Used for images, audio, video, serialized binary protocols (Protocol Buffers, MessagePack)
- No content validation required
- Opcode: 0x2

## Why the Distinction Matters

1. **Data Integrity**: UTF-8 validation ensures text data integrity and prevents encoding-related bugs
2. **Performance**: Binary frames skip validation, offering better performance for non-text data
3. **Interoperability**: Clear separation allows different programming languages to handle data appropriately
4. **Security**: Proper validation prevents injection attacks through malformed text

## C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// WebSocket frame opcodes
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2

// UTF-8 validation result
typedef enum {
    UTF8_VALID,
    UTF8_INVALID,
    UTF8_INCOMPLETE
} utf8_status_t;

// Validate UTF-8 sequence
utf8_status_t validate_utf8(const uint8_t *data, size_t length) {
    size_t i = 0;
    
    while (i < length) {
        uint8_t byte = data[i];
        int continuation_bytes = 0;
        
        // Single-byte character (0xxxxxxx)
        if ((byte & 0x80) == 0) {
            i++;
            continue;
        }
        // Two-byte character (110xxxxx 10xxxxxx)
        else if ((byte & 0xE0) == 0xC0) {
            continuation_bytes = 1;
            // Check for overlong encoding
            if ((byte & 0x1E) == 0) return UTF8_INVALID;
        }
        // Three-byte character (1110xxxx 10xxxxxx 10xxxxxx)
        else if ((byte & 0xF0) == 0xE0) {
            continuation_bytes = 2;
        }
        // Four-byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        else if ((byte & 0xF8) == 0xF0) {
            continuation_bytes = 3;
            // Check for invalid code points (> U+10FFFF)
            if ((byte & 0x07) > 4) return UTF8_INVALID;
        }
        else {
            return UTF8_INVALID;
        }
        
        // Check if we have enough bytes
        if (i + continuation_bytes >= length) {
            return UTF8_INCOMPLETE;
        }
        
        // Validate continuation bytes
        for (int j = 1; j <= continuation_bytes; j++) {
            if ((data[i + j] & 0xC0) != 0x80) {
                return UTF8_INVALID;
            }
        }
        
        // Additional validation for three-byte sequences
        if (continuation_bytes == 2) {
            uint32_t codepoint = ((byte & 0x0F) << 12) |
                                ((data[i + 1] & 0x3F) << 6) |
                                (data[i + 2] & 0x3F);
            // Check for overlong encoding and surrogate pairs
            if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
                return UTF8_INVALID;
            }
        }
        
        i += continuation_bytes + 1;
    }
    
    return UTF8_VALID;
}

// WebSocket frame structure
typedef struct {
    uint8_t opcode;
    bool fin;
    uint8_t *payload;
    size_t payload_length;
} ws_frame_t;

// Send text frame
int ws_send_text(int sockfd, const char *text) {
    size_t length = strlen(text);
    
    // Validate UTF-8
    if (validate_utf8((const uint8_t *)text, length) != UTF8_VALID) {
        fprintf(stderr, "Invalid UTF-8 in text frame\n");
        return -1;
    }
    
    // Build frame header
    uint8_t header[10];
    size_t header_len = 0;
    
    // FIN bit + opcode
    header[header_len++] = 0x80 | WS_OPCODE_TEXT;
    
    // Payload length
    if (length < 126) {
        header[header_len++] = (uint8_t)length;
    } else if (length < 65536) {
        header[header_len++] = 126;
        header[header_len++] = (length >> 8) & 0xFF;
        header[header_len++] = length & 0xFF;
    } else {
        header[header_len++] = 127;
        for (int i = 7; i >= 0; i--) {
            header[header_len++] = (length >> (i * 8)) & 0xFF;
        }
    }
    
    // Send header and payload (simplified, actual implementation needs error handling)
    // send(sockfd, header, header_len, 0);
    // send(sockfd, text, length, 0);
    
    printf("Sent text frame: %zu bytes\n", length);
    return 0;
}

// Send binary frame
int ws_send_binary(int sockfd, const uint8_t *data, size_t length) {
    uint8_t header[10];
    size_t header_len = 0;
    
    // FIN bit + opcode
    header[header_len++] = 0x80 | WS_OPCODE_BINARY;
    
    // Payload length
    if (length < 126) {
        header[header_len++] = (uint8_t)length;
    } else if (length < 65536) {
        header[header_len++] = 126;
        header[header_len++] = (length >> 8) & 0xFF;
        header[header_len++] = length & 0xFF;
    } else {
        header[header_len++] = 127;
        for (int i = 7; i >= 0; i--) {
            header[header_len++] = (length >> (i * 8)) & 0xFF;
        }
    }
    
    // Send header and payload (simplified)
    // send(sockfd, header, header_len, 0);
    // send(sockfd, data, length, 0);
    
    printf("Sent binary frame: %zu bytes\n", length);
    return 0;
}

// Process received frame
int ws_process_frame(const ws_frame_t *frame) {
    if (frame->opcode == WS_OPCODE_TEXT) {
        // Validate UTF-8 for text frames
        utf8_status_t status = validate_utf8(frame->payload, frame->payload_length);
        if (status != UTF8_VALID) {
            fprintf(stderr, "Invalid UTF-8 in received text frame\n");
            return -1; // Should close connection with code 1007
        }
        printf("Received valid text frame: %.*s\n", 
               (int)frame->payload_length, frame->payload);
    } 
    else if (frame->opcode == WS_OPCODE_BINARY) {
        // No validation needed for binary frames
        printf("Received binary frame: %zu bytes\n", frame->payload_length);
        // Process binary data as needed
    }
    
    return 0;
}

int main() {
    // Example: Sending text
    const char *json_msg = "{\"type\":\"message\",\"data\":\"Hello 世界\"}";
    ws_send_text(0, json_msg);
    
    // Example: Sending binary (image data, protocol buffer, etc.)
    uint8_t binary_data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    ws_send_binary(0, binary_data, sizeof(binary_data));
    
    // Example: Processing received frame
    ws_frame_t text_frame = {
        .opcode = WS_OPCODE_TEXT,
        .fin = true,
        .payload = (uint8_t *)"Test message",
        .payload_length = 12
    };
    ws_process_frame(&text_frame);
    
    return 0;
}
```

## C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <memory>

enum class FrameType {
    Text = 0x1,
    Binary = 0x2
};

enum class Utf8Status {
    Valid,
    Invalid,
    Incomplete
};

class Utf8Validator {
public:
    static Utf8Status validate(const std::vector<uint8_t>& data) {
        return validate(data.data(), data.size());
    }
    
    static Utf8Status validate(const uint8_t* data, size_t length) {
        size_t i = 0;
        
        while (i < length) {
            uint8_t byte = data[i];
            int continuation_bytes = 0;
            uint32_t codepoint = 0;
            
            if ((byte & 0x80) == 0) {
                // Single-byte character
                i++;
                continue;
            } else if ((byte & 0xE0) == 0xC0) {
                continuation_bytes = 1;
                codepoint = byte & 0x1F;
                if ((byte & 0x1E) == 0) return Utf8Status::Invalid;
            } else if ((byte & 0xF0) == 0xE0) {
                continuation_bytes = 2;
                codepoint = byte & 0x0F;
            } else if ((byte & 0xF8) == 0xF0) {
                continuation_bytes = 3;
                codepoint = byte & 0x07;
                if ((byte & 0x07) > 4) return Utf8Status::Invalid;
            } else {
                return Utf8Status::Invalid;
            }
            
            if (i + continuation_bytes >= length) {
                return Utf8Status::Incomplete;
            }
            
            for (int j = 1; j <= continuation_bytes; j++) {
                if ((data[i + j] & 0xC0) != 0x80) {
                    return Utf8Status::Invalid;
                }
                codepoint = (codepoint << 6) | (data[i + j] & 0x3F);
            }
            
            // Check for overlong encodings
            if ((continuation_bytes == 1 && codepoint < 0x80) ||
                (continuation_bytes == 2 && codepoint < 0x800) ||
                (continuation_bytes == 3 && codepoint < 0x10000)) {
                return Utf8Status::Invalid;
            }
            
            // Check for surrogate pairs and invalid code points
            if ((codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
                return Utf8Status::Invalid;
            }
            
            i += continuation_bytes + 1;
        }
        
        return Utf8Status::Valid;
    }
};

class WebSocketFrame {
private:
    FrameType type_;
    std::vector<uint8_t> payload_;
    bool fin_;

public:
    WebSocketFrame(FrameType type, std::vector<uint8_t> payload, bool fin = true)
        : type_(type), payload_(std::move(payload)), fin_(fin) {
        
        if (type_ == FrameType::Text) {
            if (Utf8Validator::validate(payload_) != Utf8Status::Valid) {
                throw std::invalid_argument("Invalid UTF-8 in text frame");
            }
        }
    }
    
    static WebSocketFrame createText(const std::string& text) {
        std::vector<uint8_t> payload(text.begin(), text.end());
        return WebSocketFrame(FrameType::Text, std::move(payload));
    }
    
    static WebSocketFrame createBinary(const std::vector<uint8_t>& data) {
        return WebSocketFrame(FrameType::Binary, data);
    }
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> frame;
        
        // FIN bit + opcode
        frame.push_back(0x80 | static_cast<uint8_t>(type_));
        
        // Payload length
        size_t length = payload_.size();
        if (length < 126) {
            frame.push_back(static_cast<uint8_t>(length));
        } else if (length < 65536) {
            frame.push_back(126);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((length >> (i * 8)) & 0xFF);
            }
        }
        
        // Payload
        frame.insert(frame.end(), payload_.begin(), payload_.end());
        
        return frame;
    }
    
    FrameType getType() const { return type_; }
    const std::vector<uint8_t>& getPayload() const { return payload_; }
    
    std::string getTextPayload() const {
        if (type_ != FrameType::Text) {
            throw std::runtime_error("Not a text frame");
        }
        return std::string(payload_.begin(), payload_.end());
    }
};

class WebSocketConnection {
public:
    void sendText(const std::string& message) {
        try {
            auto frame = WebSocketFrame::createText(message);
            auto serialized = frame.serialize();
            // send(socket_, serialized.data(), serialized.size(), 0);
            std::cout << "Sent text frame: " << message << " (" 
                      << serialized.size() << " bytes)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to send text: " << e.what() << std::endl;
        }
    }
    
    void sendBinary(const std::vector<uint8_t>& data) {
        auto frame = WebSocketFrame::createBinary(data);
        auto serialized = frame.serialize();
        // send(socket_, serialized.data(), serialized.size(), 0);
        std::cout << "Sent binary frame: " << data.size() 
                  << " bytes payload" << std::endl;
    }
    
    void processFrame(const WebSocketFrame& frame) {
        if (frame.getType() == FrameType::Text) {
            std::string text = frame.getTextPayload();
            std::cout << "Received text: " << text << std::endl;
            // Process text data
        } else {
            const auto& data = frame.getPayload();
            std::cout << "Received binary: " << data.size() << " bytes" << std::endl;
            // Process binary data
        }
    }
};

int main() {
    WebSocketConnection conn;
    
    // Sending text messages
    conn.sendText("Hello, WebSocket!");
    conn.sendText("{\"event\":\"update\",\"data\":\"用户消息\"}");
    
    // Sending binary data
    std::vector<uint8_t> imageData = {0xFF, 0xD8, 0xFF, 0xE0}; // JPEG header
    conn.sendBinary(imageData);
    
    // Processing frames
    auto textFrame = WebSocketFrame::createText("Test message");
    conn.processFrame(textFrame);
    
    auto binaryFrame = WebSocketFrame::createBinary({0x01, 0x02, 0x03, 0x04});
    conn.processFrame(binaryFrame);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq)]
enum FrameType {
    Text = 0x1,
    Binary = 0x2,
}

#[derive(Debug)]
enum Utf8Error {
    Invalid,
    Incomplete,
}

impl fmt::Display for Utf8Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Utf8Error::Invalid => write!(f, "Invalid UTF-8 sequence"),
            Utf8Error::Incomplete => write!(f, "Incomplete UTF-8 sequence"),
        }
    }
}

impl Error for Utf8Error {}

struct Utf8Validator;

impl Utf8Validator {
    fn validate(data: &[u8]) -> Result<(), Utf8Error> {
        let mut i = 0;
        
        while i < data.len() {
            let byte = data[i];
            let continuation_bytes: usize;
            let mut codepoint: u32;
            
            if (byte & 0x80) == 0 {
                // Single-byte character
                i += 1;
                continue;
            } else if (byte & 0xE0) == 0xC0 {
                continuation_bytes = 1;
                codepoint = (byte & 0x1F) as u32;
                if (byte & 0x1E) == 0 {
                    return Err(Utf8Error::Invalid);
                }
            } else if (byte & 0xF0) == 0xE0 {
                continuation_bytes = 2;
                codepoint = (byte & 0x0F) as u32;
            } else if (byte & 0xF8) == 0xF0 {
                continuation_bytes = 3;
                codepoint = (byte & 0x07) as u32;
                if (byte & 0x07) > 4 {
                    return Err(Utf8Error::Invalid);
                }
            } else {
                return Err(Utf8Error::Invalid);
            }
            
            if i + continuation_bytes >= data.len() {
                return Err(Utf8Error::Incomplete);
            }
            
            for j in 1..=continuation_bytes {
                let cont_byte = data[i + j];
                if (cont_byte & 0xC0) != 0x80 {
                    return Err(Utf8Error::Invalid);
                }
                codepoint = (codepoint << 6) | ((cont_byte & 0x3F) as u32);
            }
            
            // Check for overlong encodings
            let is_overlong = match continuation_bytes {
                1 => codepoint < 0x80,
                2 => codepoint < 0x800,
                3 => codepoint < 0x10000,
                _ => false,
            };
            
            if is_overlong {
                return Err(Utf8Error::Invalid);
            }
            
            // Check for surrogate pairs and invalid code points
            if (0xD800..=0xDFFF).contains(&codepoint) || codepoint > 0x10FFFF {
                return Err(Utf8Error::Invalid);
            }
            
            i += continuation_bytes + 1;
        }
        
        Ok(())
    }
}

struct WebSocketFrame {
    frame_type: FrameType,
    payload: Vec<u8>,
    fin: bool,
}

impl WebSocketFrame {
    fn new_text(text: String) -> Result<Self, Utf8Error> {
        let payload = text.into_bytes();
        Utf8Validator::validate(&payload)?;
        
        Ok(WebSocketFrame {
            frame_type: FrameType::Text,
            payload,
            fin: true,
        })
    }
    
    fn new_binary(data: Vec<u8>) -> Self {
        WebSocketFrame {
            frame_type: FrameType::Binary,
            payload: data,
            fin: true,
        }
    }
    
    fn serialize(&self) -> Vec<u8> {
        let mut frame = Vec::new();
        
        // FIN bit + opcode
        frame.push(0x80 | self.frame_type as u8);
        
        // Payload length
        let length = self.payload.len();
        if length < 126 {
            frame.push(length as u8);
        } else if length < 65536 {
            frame.push(126);
            frame.push((length >> 8) as u8);
            frame.push(length as u8);
        } else {
            frame.push(127);
            for i in (0..8).rev() {
                frame.push((length >> (i * 8)) as u8);
            }
        }
        
        // Payload
        frame.extend_from_slice(&self.payload);
        
        frame
    }
    
    fn get_text_payload(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.payload.clone())
    }
}

struct WebSocketConnection;

impl WebSocketConnection {
    fn send_text(&self, message: &str) -> Result<(), Box<dyn Error>> {
        let frame = WebSocketFrame::new_text(message.to_string())?;
        let serialized = frame.serialize();
        
        // In real implementation: socket.write_all(&serialized)?;
        println!("Sent text frame: {} ({} bytes)", message, serialized.len());
        Ok(())
    }
    
    fn send_binary(&self, data: Vec<u8>) -> Result<(), Box<dyn Error>> {
        let frame = WebSocketFrame::new_binary(data.clone());
        let serialized = frame.serialize();
        
        // In real implementation: socket.write_all(&serialized)?;
        println!("Sent binary frame: {} bytes payload", data.len());
        Ok(())
    }
    
    fn process_frame(&self, frame: &WebSocketFrame) -> Result<(), Box<dyn Error>> {
        match frame.frame_type {
            FrameType::Text => {
                let text = frame.get_text_payload()?;
                println!("Received text: {}", text);
                // Process text data
            }
            FrameType::Binary => {
                println!("Received binary: {} bytes", frame.payload.len());
                // Process binary data
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let conn = WebSocketConnection;
    
    // Sending text messages
    conn.send_text("Hello, WebSocket!")?;
    conn.send_text(r#"{"event":"update","data":"用户消息"}"#)?;
    
    // Sending binary data
    let image_data = vec![0xFF, 0xD8, 0xFF, 0xE0]; // JPEG header
    conn.send_binary(image_data)?;
    
    // Processing frames
    let text_frame = WebSocketFrame::new_text("Test message".to_string())?;
    conn.process_frame(&text_frame)?;
    
    let binary_frame = WebSocketFrame::new_binary(vec![0x01, 0x02, 0x03, 0x04]);
    conn.process_frame(&binary_frame)?;
    
    // Demonstrating UTF-8 validation
    let invalid_utf8 = vec![0xFF, 0xFE, 0xFD];
    match WebSocketFrame::new_text(String::from_utf8_lossy(&invalid_utf8).to_string()) {
        Ok(_) => println!("Frame created successfully"),
        Err(e) => println!("Failed to create text frame: {}", e),
    }
    
    Ok(())
}
```

## Summary

**Binary vs Text Frames** is a core WebSocket concept that enables efficient and type-safe data transmission:

**Text Frames** carry UTF-8 encoded strings and require strict validation to ensure data integrity. They're ideal for JSON, XML, and human-readable protocols. The WebSocket specification mandates that implementations validate UTF-8 encoding and close connections with error code 1007 if invalid sequences are detected.

**Binary Frames** carry raw byte data without encoding constraints, making them perfect for images, audio, video, and binary protocols like Protocol Buffers or MessagePack. No validation overhead means better performance for large binary payloads.

The code examples demonstrate UTF-8 validation algorithms that check for overlong encodings, invalid continuation bytes, surrogate pairs, and code points beyond U+10FFFF. All three implementations show how to create type-safe abstractions that enforce UTF-8 validation for text frames while allowing unrestricted binary data transmission.

Proper frame type handling is essential for building robust WebSocket applications that handle diverse data types efficiently while maintaining protocol compliance and security.