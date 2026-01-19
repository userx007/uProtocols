# Custom Control Frames in WebSocket

## Overview

Custom control frames represent an advanced technique for designing application-specific control mechanisms within the WebSocket protocol's constraints. While WebSocket defines standard control frames (Close, Ping, Pong), applications often need specialized signaling mechanisms for coordination, health checks, or protocol extensions without disrupting the data stream.

## Understanding WebSocket Frame Structure

WebSocket frames consist of:
- **FIN bit**: Indicates final fragment
- **RSV1-3 bits**: Reserved for extensions
- **Opcode**: Defines frame type (0x0-0xF)
  - 0x0: Continuation
  - 0x1: Text
  - 0x2: Binary
  - 0x8: Close
  - 0x9: Ping
  - 0xA: Pong
  - 0x3-0x7, 0xB-0xF: Reserved for future control/data frames
- **Payload length and data**

## Design Approaches

Since opcodes 0xB-0xF are reserved, we typically implement custom control mechanisms using:

1. **Reserved opcodes** (if controlling both endpoints)
2. **Special payload formats** in data frames
3. **Custom text/binary message protocols**
4. **Extension negotiation** during handshake

## C Implementation

Here's a comprehensive example using a custom protocol over binary frames:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

// Custom control frame types (application layer)
#define CTRL_HEARTBEAT 0x01
#define CTRL_PRIORITY  0x02
#define CTRL_FLOW_CTRL 0x03
#define CTRL_METADATA  0x04

// Custom control frame structure
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t length;
    uint8_t data[];
} __attribute__((packed)) CustomControlFrame;

// WebSocket frame header (simplified)
typedef struct {
    uint8_t fin_rsv_opcode;
    uint8_t mask_length;
    uint8_t payload[];
} WSFrame;

// Create custom control frame
CustomControlFrame* create_control_frame(uint8_t type, uint8_t flags, 
                                         const void* data, uint16_t data_len) {
    size_t total_size = sizeof(CustomControlFrame) + data_len;
    CustomControlFrame* frame = malloc(total_size);
    
    if (!frame) return NULL;
    
    frame->type = type;
    frame->flags = flags;
    frame->length = htons(data_len);
    
    if (data && data_len > 0) {
        memcpy(frame->data, data, data_len);
    }
    
    return frame;
}

// Send custom control frame over WebSocket (as binary data)
int send_custom_control(int sockfd, CustomControlFrame* ctrl_frame) {
    uint16_t payload_len = ntohs(ctrl_frame->length) + sizeof(CustomControlFrame);
    
    // WebSocket binary frame header: FIN=1, opcode=0x2 (binary)
    uint8_t header[2];
    header[0] = 0x82; // FIN=1, RSV=000, opcode=0010
    header[1] = payload_len < 126 ? payload_len : 126;
    
    // Send header
    if (send(sockfd, header, 2, 0) < 0) return -1;
    
    // Send extended payload length if needed
    if (payload_len >= 126) {
        uint16_t ext_len = htons(payload_len);
        if (send(sockfd, &ext_len, 2, 0) < 0) return -1;
    }
    
    // Send control frame
    if (send(sockfd, ctrl_frame, payload_len, 0) < 0) return -1;
    
    return 0;
}

// Parse received custom control frame
int parse_control_frame(const uint8_t* data, size_t len, 
                        CustomControlFrame** out_frame) {
    if (len < sizeof(CustomControlFrame)) return -1;
    
    const CustomControlFrame* frame = (const CustomControlFrame*)data;
    uint16_t data_len = ntohs(frame->length);
    
    if (len < sizeof(CustomControlFrame) + data_len) return -1;
    
    *out_frame = malloc(sizeof(CustomControlFrame) + data_len);
    if (!*out_frame) return -1;
    
    memcpy(*out_frame, frame, sizeof(CustomControlFrame) + data_len);
    return 0;
}

// Handle different control frame types
void handle_control_frame(CustomControlFrame* frame) {
    uint16_t data_len = ntohs(frame->length);
    
    switch (frame->type) {
        case CTRL_HEARTBEAT:
            printf("Heartbeat received - sequence: %u\n", 
                   data_len >= 4 ? ntohl(*(uint32_t*)frame->data) : 0);
            break;
            
        case CTRL_PRIORITY:
            printf("Priority change: level=%u\n", 
                   data_len >= 1 ? frame->data[0] : 0);
            break;
            
        case CTRL_FLOW_CTRL:
            printf("Flow control: window_size=%u\n",
                   data_len >= 4 ? ntohl(*(uint32_t*)frame->data) : 0);
            break;
            
        case CTRL_METADATA:
            printf("Metadata: %.*s\n", data_len, frame->data);
            break;
            
        default:
            printf("Unknown control type: 0x%02x\n", frame->type);
    }
}

// Example usage
int main() {
    // Simulated socket descriptor
    int sockfd = 0; // In real code, this would be a connected socket
    
    // Send heartbeat
    uint32_t sequence = htonl(42);
    CustomControlFrame* hb = create_control_frame(CTRL_HEARTBEAT, 0, 
                                                   &sequence, sizeof(sequence));
    // send_custom_control(sockfd, hb);
    handle_control_frame(hb);
    free(hb);
    
    // Send flow control
    uint32_t window = htonl(65536);
    CustomControlFrame* fc = create_control_frame(CTRL_FLOW_CTRL, 0x01,
                                                   &window, sizeof(window));
    handle_control_frame(fc);
    free(fc);
    
    // Send metadata
    const char* meta = "version=1.2.3";
    CustomControlFrame* md = create_control_frame(CTRL_METADATA, 0,
                                                   meta, strlen(meta));
    handle_control_frame(md);
    free(md);
    
    return 0;
}
```

## C++ Implementation

A modern C++ approach with type safety and RAII:

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <variant>
#include <optional>

enum class ControlFrameType : uint8_t {
    Heartbeat = 0x01,
    Priority = 0x02,
    FlowControl = 0x03,
    Metadata = 0x04,
    RateLimit = 0x05
};

// Control frame payloads (type-safe variants)
struct HeartbeatPayload {
    uint32_t sequence;
    uint64_t timestamp;
};

struct PriorityPayload {
    uint8_t level;
    uint32_t stream_id;
};

struct FlowControlPayload {
    uint32_t window_size;
    uint32_t increment;
};

struct MetadataPayload {
    std::string key;
    std::string value;
};

using ControlPayload = std::variant
    HeartbeatPayload,
    PriorityPayload,
    FlowControlPayload,
    MetadataPayload
>;

class CustomControlFrame {
private:
    ControlFrameType type_;
    uint8_t flags_;
    std::vector<uint8_t> raw_data_;
    
public:
    CustomControlFrame(ControlFrameType type, uint8_t flags = 0)
        : type_(type), flags_(flags) {}
    
    // Template method to set payload
    template<typename T>
    void setPayload(const T& payload) {
        if constexpr (std::is_same_v<T, HeartbeatPayload>) {
            raw_data_.resize(sizeof(HeartbeatPayload));
            std::memcpy(raw_data_.data(), &payload, sizeof(HeartbeatPayload));
        } else if constexpr (std::is_same_v<T, FlowControlPayload>) {
            raw_data_.resize(sizeof(FlowControlPayload));
            std::memcpy(raw_data_.data(), &payload, sizeof(FlowControlPayload));
        } else if constexpr (std::is_same_v<T, MetadataPayload>) {
            // Simple encoding: key_len(2) + key + value
            size_t key_len = payload.key.size();
            raw_data_.resize(2 + key_len + payload.value.size());
            raw_data_[0] = (key_len >> 8) & 0xFF;
            raw_data_[1] = key_len & 0xFF;
            std::memcpy(raw_data_.data() + 2, payload.key.data(), key_len);
            std::memcpy(raw_data_.data() + 2 + key_len, 
                       payload.value.data(), payload.value.size());
        }
    }
    
    // Serialize to wire format
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> result;
        result.reserve(4 + raw_data_.size());
        
        result.push_back(static_cast<uint8_t>(type_));
        result.push_back(flags_);
        
        uint16_t length = static_cast<uint16_t>(raw_data_.size());
        result.push_back((length >> 8) & 0xFF);
        result.push_back(length & 0xFF);
        
        result.insert(result.end(), raw_data_.begin(), raw_data_.end());
        return result;
    }
    
    // Deserialize from wire format
    static std::optional<CustomControlFrame> deserialize(
        const std::vector<uint8_t>& data) {
        
        if (data.size() < 4) return std::nullopt;
        
        ControlFrameType type = static_cast<ControlFrameType>(data[0]);
        uint8_t flags = data[1];
        uint16_t length = (static_cast<uint16_t>(data[2]) << 8) | data[3];
        
        if (data.size() < 4 + length) return std::nullopt;
        
        CustomControlFrame frame(type, flags);
        frame.raw_data_.assign(data.begin() + 4, data.begin() + 4 + length);
        
        return frame;
    }
    
    ControlFrameType getType() const { return type_; }
    uint8_t getFlags() const { return flags_; }
    const std::vector<uint8_t>& getData() const { return raw_data_; }
    
    // Parse typed payload
    template<typename T>
    std::optional<T> getPayload() const {
        if constexpr (std::is_same_v<T, HeartbeatPayload>) {
            if (raw_data_.size() < sizeof(HeartbeatPayload)) return std::nullopt;
            HeartbeatPayload payload;
            std::memcpy(&payload, raw_data_.data(), sizeof(HeartbeatPayload));
            return payload;
        } else if constexpr (std::is_same_v<T, MetadataPayload>) {
            if (raw_data_.size() < 2) return std::nullopt;
            uint16_t key_len = (static_cast<uint16_t>(raw_data_[0]) << 8) | raw_data_[1];
            if (raw_data_.size() < 2 + key_len) return std::nullopt;
            
            MetadataPayload payload;
            payload.key.assign(raw_data_.begin() + 2, raw_data_.begin() + 2 + key_len);
            payload.value.assign(raw_data_.begin() + 2 + key_len, raw_data_.end());
            return payload;
        }
        return std::nullopt;
    }
};

// WebSocket frame wrapper
class WebSocketControlSender {
public:
    static std::vector<uint8_t> wrapAsWebSocketBinary(
        const std::vector<uint8_t>& payload) {
        
        std::vector<uint8_t> frame;
        frame.push_back(0x82); // FIN=1, opcode=binary
        
        if (payload.size() < 126) {
            frame.push_back(static_cast<uint8_t>(payload.size()));
        } else if (payload.size() < 65536) {
            frame.push_back(126);
            frame.push_back((payload.size() >> 8) & 0xFF);
            frame.push_back(payload.size() & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((payload.size() >> (i * 8)) & 0xFF);
            }
        }
        
        frame.insert(frame.end(), payload.begin(), payload.end());
        return frame;
    }
};

int main() {
    // Create and send heartbeat
    CustomControlFrame heartbeat(ControlFrameType::Heartbeat);
    HeartbeatPayload hb_payload{42, 1234567890};
    heartbeat.setPayload(hb_payload);
    
    auto serialized = heartbeat.serialize();
    auto ws_frame = WebSocketControlSender::wrapAsWebSocketBinary(serialized);
    
    std::cout << "WebSocket frame size: " << ws_frame.size() << " bytes\n";
    
    // Deserialize and parse
    auto received = CustomControlFrame::deserialize(serialized);
    if (received) {
        auto parsed = received->getPayload<HeartbeatPayload>();
        if (parsed) {
            std::cout << "Heartbeat sequence: " << parsed->sequence 
                      << ", timestamp: " << parsed->timestamp << "\n";
        }
    }
    
    // Metadata example
    CustomControlFrame metadata(ControlFrameType::Metadata);
    MetadataPayload meta{"client-version", "2.1.0"};
    metadata.setPayload(meta);
    
    auto meta_serialized = metadata.serialize();
    auto meta_received = CustomControlFrame::deserialize(meta_serialized);
    if (meta_received) {
        auto parsed_meta = meta_received->getPayload<MetadataPayload>();
        if (parsed_meta) {
            std::cout << "Metadata: " << parsed_meta->key 
                      << " = " << parsed_meta->value << "\n";
        }
    }
    
    return 0;
}
```

## Rust Implementation

Leveraging Rust's type system and zero-cost abstractions:

```rust
use std::io::{self, Write, Read};
use std::convert::TryFrom;

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum ControlFrameType {
    Heartbeat = 0x01,
    Priority = 0x02,
    FlowControl = 0x03,
    Metadata = 0x04,
    RateLimit = 0x05,
}

impl TryFrom<u8> for ControlFrameType {
    type Error = ();
    
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x01 => Ok(ControlFrameType::Heartbeat),
            0x02 => Ok(ControlFrameType::Priority),
            0x03 => Ok(ControlFrameType::FlowControl),
            0x04 => Ok(ControlFrameType::Metadata),
            0x05 => Ok(ControlFrameType::RateLimit),
            _ => Err(()),
        }
    }
}

#[derive(Debug, Clone)]
pub enum ControlPayload {
    Heartbeat { sequence: u32, timestamp: u64 },
    Priority { level: u8, stream_id: u32 },
    FlowControl { window_size: u32, increment: u32 },
    Metadata { key: String, value: String },
    RateLimit { max_rate: u32, period_ms: u32 },
}

#[derive(Debug)]
pub struct CustomControlFrame {
    frame_type: ControlFrameType,
    flags: u8,
    payload: ControlPayload,
}

impl CustomControlFrame {
    pub fn new(payload: ControlPayload, flags: u8) -> Self {
        let frame_type = match &payload {
            ControlPayload::Heartbeat { .. } => ControlFrameType::Heartbeat,
            ControlPayload::Priority { .. } => ControlFrameType::Priority,
            ControlPayload::FlowControl { .. } => ControlFrameType::FlowControl,
            ControlPayload::Metadata { .. } => ControlFrameType::Metadata,
            ControlPayload::RateLimit { .. } => ControlFrameType::RateLimit,
        };
        
        Self { frame_type, flags, payload }
    }
    
    // Serialize to bytes
    pub fn serialize(&self) -> Vec<u8> {
        let mut buffer = Vec::new();
        buffer.push(self.frame_type as u8);
        buffer.push(self.flags);
        
        let payload_bytes = self.serialize_payload();
        let length = payload_bytes.len() as u16;
        
        buffer.extend_from_slice(&length.to_be_bytes());
        buffer.extend_from_slice(&payload_bytes);
        
        buffer
    }
    
    fn serialize_payload(&self) -> Vec<u8> {
        match &self.payload {
            ControlPayload::Heartbeat { sequence, timestamp } => {
                let mut buf = Vec::with_capacity(12);
                buf.extend_from_slice(&sequence.to_be_bytes());
                buf.extend_from_slice(&timestamp.to_be_bytes());
                buf
            },
            ControlPayload::Priority { level, stream_id } => {
                let mut buf = Vec::with_capacity(5);
                buf.push(*level);
                buf.extend_from_slice(&stream_id.to_be_bytes());
                buf
            },
            ControlPayload::FlowControl { window_size, increment } => {
                let mut buf = Vec::with_capacity(8);
                buf.extend_from_slice(&window_size.to_be_bytes());
                buf.extend_from_slice(&increment.to_be_bytes());
                buf
            },
            ControlPayload::Metadata { key, value } => {
                let mut buf = Vec::new();
                let key_len = key.len() as u16;
                buf.extend_from_slice(&key_len.to_be_bytes());
                buf.extend_from_slice(key.as_bytes());
                buf.extend_from_slice(value.as_bytes());
                buf
            },
            ControlPayload::RateLimit { max_rate, period_ms } => {
                let mut buf = Vec::with_capacity(8);
                buf.extend_from_slice(&max_rate.to_be_bytes());
                buf.extend_from_slice(&period_ms.to_be_bytes());
                buf
            },
        }
    }
    
    // Deserialize from bytes
    pub fn deserialize(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < 4 {
            return Err("Insufficient data for frame header");
        }
        
        let frame_type = ControlFrameType::try_from(data[0])
            .map_err(|_| "Invalid frame type")?;
        let flags = data[1];
        let length = u16::from_be_bytes([data[2], data[3]]) as usize;
        
        if data.len() < 4 + length {
            return Err("Insufficient data for payload");
        }
        
        let payload_data = &data[4..4 + length];
        let payload = Self::deserialize_payload(frame_type, payload_data)?;
        
        Ok(Self { frame_type, flags, payload })
    }
    
    fn deserialize_payload(
        frame_type: ControlFrameType,
        data: &[u8]
    ) -> Result<ControlPayload, &'static str> {
        match frame_type {
            ControlFrameType::Heartbeat => {
                if data.len() < 12 {
                    return Err("Invalid heartbeat payload");
                }
                let sequence = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
                let timestamp = u64::from_be_bytes([
                    data[4], data[5], data[6], data[7],
                    data[8], data[9], data[10], data[11]
                ]);
                Ok(ControlPayload::Heartbeat { sequence, timestamp })
            },
            ControlFrameType::Priority => {
                if data.len() < 5 {
                    return Err("Invalid priority payload");
                }
                let level = data[0];
                let stream_id = u32::from_be_bytes([data[1], data[2], data[3], data[4]]);
                Ok(ControlPayload::Priority { level, stream_id })
            },
            ControlFrameType::FlowControl => {
                if data.len() < 8 {
                    return Err("Invalid flow control payload");
                }
                let window_size = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
                let increment = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
                Ok(ControlPayload::FlowControl { window_size, increment })
            },
            ControlFrameType::Metadata => {
                if data.len() < 2 {
                    return Err("Invalid metadata payload");
                }
                let key_len = u16::from_be_bytes([data[0], data[1]]) as usize;
                if data.len() < 2 + key_len {
                    return Err("Invalid metadata key length");
                }
                
                let key = String::from_utf8(data[2..2 + key_len].to_vec())
                    .map_err(|_| "Invalid UTF-8 in key")?;
                let value = String::from_utf8(data[2 + key_len..].to_vec())
                    .map_err(|_| "Invalid UTF-8 in value")?;
                
                Ok(ControlPayload::Metadata { key, value })
            },
            ControlFrameType::RateLimit => {
                if data.len() < 8 {
                    return Err("Invalid rate limit payload");
                }
                let max_rate = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
                let period_ms = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
                Ok(ControlPayload::RateLimit { max_rate, period_ms })
            },
        }
    }
    
    pub fn frame_type(&self) -> ControlFrameType {
        self.frame_type
    }
    
    pub fn payload(&self) -> &ControlPayload {
        &self.payload
    }
}

// WebSocket frame wrapper
pub struct WebSocketFrame;

impl WebSocketFrame {
    pub fn wrap_binary(payload: &[u8]) -> Vec<u8> {
        let mut frame = Vec::new();
        frame.push(0x82); // FIN=1, opcode=binary
        
        match payload.len() {
            len if len < 126 => {
                frame.push(len as u8);
            },
            len if len < 65536 => {
                frame.push(126);
                frame.extend_from_slice(&(len as u16).to_be_bytes());
            },
            len => {
                frame.push(127);
                frame.extend_from_slice(&(len as u64).to_be_bytes());
            },
        }
        
        frame.extend_from_slice(payload);
        frame
    }
}

fn main() {
    // Create heartbeat frame
    let heartbeat = CustomControlFrame::new(
        ControlPayload::Heartbeat {
            sequence: 42,
            timestamp: 1234567890,
        },
        0,
    );
    
    let serialized = heartbeat.serialize();
    println!("Serialized heartbeat: {} bytes", serialized.len());
    
    // Wrap in WebSocket binary frame
    let ws_frame = WebSocketFrame::wrap_binary(&serialized);
    println!("WebSocket frame: {} bytes", ws_frame.len());
    
    // Deserialize
    match CustomControlFrame::deserialize(&serialized) {
        Ok(frame) => {
            if let ControlPayload::Heartbeat { sequence, timestamp } = frame.payload() {
                println!("Received heartbeat - seq: {}, ts: {}", sequence, timestamp);
            }
        },
        Err(e) => eprintln!("Error: {}", e),
    }
    
    // Metadata example
    let metadata = CustomControlFrame::new(
        ControlPayload::Metadata {
            key: "client-version".to_string(),
            value: "3.0.1".to_string(),
        },
        0,
    );
    
    let meta_serialized = metadata.serialize();
    if let Ok(frame) = CustomControlFrame::deserialize(&meta_serialized) {
        if let ControlPayload::Metadata { key, value } = frame.payload() {
            println!("Metadata: {} = {}", key, value);
        }
    }
    
    // Flow control example
    let flow_control = CustomControlFrame::new(
        ControlPayload::FlowControl {
            window_size: 65536,
            increment: 32768,
        },
        0x01, // ACK flag
    );
    
    println!("Flow control frame type: {:?}", flow_control.frame_type());
}
```

## Summary

**Custom control frames** enable sophisticated application-layer protocols within WebSocket by creating structured, typed control messages that operate alongside regular data transmission. The key approaches include:

**Design strategies**: Using reserved opcodes (when controlling both endpoints), embedding special payload formats in binary frames, implementing custom text/binary message protocols, or negotiating extensions during the handshake phase.

**Implementation patterns**: All three language examples demonstrate similar architectural approaches—defining type-safe control frame enumerations, creating serialization/deserialization logic with proper byte ordering, wrapping custom frames in standard WebSocket binary frames, and implementing handlers for different control types.

**Common use cases**: Heartbeat mechanisms with sequence tracking, priority signaling for stream multiplexing, flow control with window size management, metadata exchange for protocol versioning, and rate limiting coordination between endpoints.

**Best practices**: Maintain backward compatibility by versioning control frames, use compact binary encodings to minimize overhead, implement proper error handling for malformed frames, document the custom protocol thoroughly, and consider using extension negotiation during handshake to ensure both endpoints support the custom control mechanisms.

The implementations showcase progressively more sophisticated type systems—from C's manual memory management and struct packing, to C++'s templates and variants, to Rust's enums with associated data and comprehensive error handling—while all maintaining wire-format compatibility for interoperability.