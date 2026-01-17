# HTTP/2 Fundamentals

## Overview

HTTP/2 is a major revision of the HTTP protocol that improves web performance through several key innovations. Released in 2015, it addresses limitations of HTTP/1.1 while maintaining backward compatibility at the application layer. The protocol introduces a binary framing layer, multiplexing, header compression, and server push capabilities.

## Core Concepts

### 1. Binary Framing Layer

Unlike HTTP/1.1's text-based protocol, HTTP/2 uses a binary framing layer that encapsulates all messages into frames. This makes parsing more efficient and less error-prone.

**Frame Structure:**
- **Length** (24 bits): Frame payload length
- **Type** (8 bits): Frame type (DATA, HEADERS, PRIORITY, etc.)
- **Flags** (8 bits): Frame-specific flags
- **Stream Identifier** (31 bits): Unique stream ID
- **Payload**: Variable-length frame data

### 2. Multiplexing

HTTP/2 allows multiple concurrent exchanges on a single TCP connection. Each request/response is assigned a unique stream ID, eliminating head-of-line blocking at the application layer. Multiple streams can be interleaved, with frames from different streams transmitted in any order.

**Benefits:**
- Reduced latency through parallel requests
- Better TCP connection utilization
- Eliminates need for domain sharding or multiple connections

### 3. Server Push

Servers can proactively send resources to clients before they're requested. When a client requests an HTML page, the server can push associated CSS, JavaScript, and images, reducing round trips.

### 4. Header Compression (HPACK)

HTTP/2 uses HPACK compression to reduce header overhead. Headers are compressed using Huffman encoding and a dynamic table that references previously sent headers, significantly reducing redundant data transmission.

## Code Examples

### C Implementation - Basic HTTP/2 Frame Parser

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

// HTTP/2 Frame Types
#define FRAME_TYPE_DATA         0x00
#define FRAME_TYPE_HEADERS      0x01
#define FRAME_TYPE_PRIORITY     0x02
#define FRAME_TYPE_RST_STREAM   0x03
#define FRAME_TYPE_SETTINGS     0x04
#define FRAME_TYPE_PUSH_PROMISE 0x05
#define FRAME_TYPE_PING         0x06
#define FRAME_TYPE_GOAWAY       0x07
#define FRAME_TYPE_WINDOW_UPDATE 0x08
#define FRAME_TYPE_CONTINUATION 0x09

// Frame Flags
#define FLAG_END_STREAM  0x01
#define FLAG_END_HEADERS 0x04
#define FLAG_PADDED      0x08
#define FLAG_PRIORITY    0x20

// HTTP/2 Frame Header Structure
typedef struct {
    uint32_t length;      // 24-bit length (stored in 32-bit for alignment)
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;   // 31-bit stream ID
} http2_frame_header_t;

// Parse HTTP/2 frame header from wire format
int parse_frame_header(const uint8_t *data, http2_frame_header_t *header) {
    if (!data || !header) return -1;
    
    // Parse 24-bit length (3 bytes)
    header->length = (data[0] << 16) | (data[1] << 8) | data[2];
    
    // Parse type and flags
    header->type = data[3];
    header->flags = data[4];
    
    // Parse 32-bit stream ID (ignore reserved bit)
    header->stream_id = ntohl(*(uint32_t*)(data + 5)) & 0x7FFFFFFF;
    
    return 0;
}

// Serialize HTTP/2 frame header to wire format
int serialize_frame_header(const http2_frame_header_t *header, uint8_t *data) {
    if (!header || !data) return -1;
    
    // Serialize 24-bit length
    data[0] = (header->length >> 16) & 0xFF;
    data[1] = (header->length >> 8) & 0xFF;
    data[2] = header->length & 0xFF;
    
    // Serialize type and flags
    data[3] = header->type;
    data[4] = header->flags;
    
    // Serialize stream ID (ensure reserved bit is 0)
    uint32_t stream_id = htonl(header->stream_id & 0x7FFFFFFF);
    memcpy(data + 5, &stream_id, 4);
    
    return 0;
}

// Create a SETTINGS frame
void create_settings_frame(uint8_t *buffer, size_t *frame_size) {
    http2_frame_header_t header = {
        .length = 12,  // 2 settings * 6 bytes each
        .type = FRAME_TYPE_SETTINGS,
        .flags = 0,
        .stream_id = 0  // Settings always on stream 0
    };
    
    serialize_frame_header(&header, buffer);
    
    // Setting: SETTINGS_MAX_CONCURRENT_STREAMS = 100
    buffer[9] = 0x00;
    buffer[10] = 0x03;  // Setting ID
    uint32_t value1 = htonl(100);
    memcpy(buffer + 11, &value1, 4);
    
    // Setting: SETTINGS_INITIAL_WINDOW_SIZE = 65535
    buffer[15] = 0x00;
    buffer[16] = 0x04;  // Setting ID
    uint32_t value2 = htonl(65535);
    memcpy(buffer + 17, &value2, 4);
    
    *frame_size = 9 + 12;  // Header + payload
}

// Print frame information
void print_frame_info(const http2_frame_header_t *header) {
    const char *type_str[] = {
        "DATA", "HEADERS", "PRIORITY", "RST_STREAM",
        "SETTINGS", "PUSH_PROMISE", "PING", "GOAWAY",
        "WINDOW_UPDATE", "CONTINUATION"
    };
    
    printf("Frame Type: %s\n", 
           header->type < 10 ? type_str[header->type] : "UNKNOWN");
    printf("Length: %u\n", header->length);
    printf("Flags: 0x%02X\n", header->flags);
    printf("Stream ID: %u\n", header->stream_id);
}

int main() {
    uint8_t buffer[256];
    size_t frame_size;
    
    printf("Creating HTTP/2 SETTINGS frame...\n");
    create_settings_frame(buffer, &frame_size);
    
    printf("\nParsing frame header...\n");
    http2_frame_header_t header;
    parse_frame_header(buffer, &header);
    print_frame_info(&header);
    
    return 0;
}
```

### C++ Implementation - HTTP/2 Stream Manager

```cpp
#include <iostream>
#include <map>
#include <vector>
#include <queue>
#include <memory>
#include <cstdint>

enum class FrameType : uint8_t {
    DATA = 0x00,
    HEADERS = 0x01,
    PRIORITY = 0x02,
    RST_STREAM = 0x03,
    SETTINGS = 0x04,
    PUSH_PROMISE = 0x05,
    PING = 0x06,
    GOAWAY = 0x07,
    WINDOW_UPDATE = 0x08,
    CONTINUATION = 0x09
};

enum class StreamState {
    IDLE,
    RESERVED_LOCAL,
    RESERVED_REMOTE,
    OPEN,
    HALF_CLOSED_LOCAL,
    HALF_CLOSED_REMOTE,
    CLOSED
};

struct Frame {
    uint32_t length;
    FrameType type;
    uint8_t flags;
    uint32_t stream_id;
    std::vector<uint8_t> payload;
    
    Frame(uint32_t len, FrameType t, uint8_t f, uint32_t sid)
        : length(len), type(t), flags(f), stream_id(sid) {
        payload.reserve(len);
    }
};

class HTTP2Stream {
private:
    uint32_t stream_id_;
    StreamState state_;
    int32_t send_window_;
    int32_t recv_window_;
    std::queue<std::shared_ptr<Frame>> pending_frames_;
    
public:
    HTTP2Stream(uint32_t id, int32_t initial_window = 65535)
        : stream_id_(id), 
          state_(StreamState::IDLE),
          send_window_(initial_window),
          recv_window_(initial_window) {}
    
    uint32_t id() const { return stream_id_; }
    StreamState state() const { return state_; }
    
    void transition(FrameType frame_type, bool send, bool end_stream) {
        switch (state_) {
            case StreamState::IDLE:
                if (frame_type == FrameType::HEADERS) {
                    state_ = send ? StreamState::OPEN : StreamState::OPEN;
                    if (end_stream) {
                        state_ = send ? StreamState::HALF_CLOSED_LOCAL 
                                     : StreamState::HALF_CLOSED_REMOTE;
                    }
                } else if (frame_type == FrameType::PUSH_PROMISE) {
                    state_ = send ? StreamState::RESERVED_LOCAL 
                                 : StreamState::RESERVED_REMOTE;
                }
                break;
                
            case StreamState::OPEN:
                if (end_stream) {
                    state_ = send ? StreamState::HALF_CLOSED_LOCAL 
                                 : StreamState::HALF_CLOSED_REMOTE;
                }
                break;
                
            case StreamState::HALF_CLOSED_LOCAL:
                if (!send && end_stream) {
                    state_ = StreamState::CLOSED;
                }
                break;
                
            case StreamState::HALF_CLOSED_REMOTE:
                if (send && end_stream) {
                    state_ = StreamState::CLOSED;
                }
                break;
                
            default:
                break;
        }
    }
    
    bool update_send_window(int32_t delta) {
        send_window_ += delta;
        return send_window_ >= 0;
    }
    
    bool update_recv_window(int32_t delta) {
        recv_window_ += delta;
        return recv_window_ >= 0;
    }
    
    int32_t send_window() const { return send_window_; }
    int32_t recv_window() const { return recv_window_; }
    
    void enqueue_frame(std::shared_ptr<Frame> frame) {
        pending_frames_.push(frame);
    }
    
    std::shared_ptr<Frame> dequeue_frame() {
        if (pending_frames_.empty()) return nullptr;
        auto frame = pending_frames_.front();
        pending_frames_.pop();
        return frame;
    }
};

class HTTP2Connection {
private:
    std::map<uint32_t, std::unique_ptr<HTTP2Stream>> streams_;
    uint32_t next_stream_id_;
    int32_t connection_send_window_;
    int32_t connection_recv_window_;
    uint32_t max_concurrent_streams_;
    
public:
    HTTP2Connection(bool is_client, uint32_t max_streams = 100)
        : next_stream_id_(is_client ? 1 : 2),
          connection_send_window_(65535),
          connection_recv_window_(65535),
          max_concurrent_streams_(max_streams) {}
    
    HTTP2Stream* create_stream() {
        if (streams_.size() >= max_concurrent_streams_) {
            return nullptr;
        }
        
        uint32_t stream_id = next_stream_id_;
        next_stream_id_ += 2;  // Client uses odd, server uses even
        
        auto stream = std::make_unique<HTTP2Stream>(stream_id);
        auto* stream_ptr = stream.get();
        streams_[stream_id] = std::move(stream);
        
        return stream_ptr;
    }
    
    HTTP2Stream* get_stream(uint32_t stream_id) {
        auto it = streams_.find(stream_id);
        return (it != streams_.end()) ? it->second.get() : nullptr;
    }
    
    void close_stream(uint32_t stream_id) {
        streams_.erase(stream_id);
    }
    
    bool update_connection_window(int32_t delta, bool send) {
        if (send) {
            connection_send_window_ += delta;
            return connection_send_window_ >= 0;
        } else {
            connection_recv_window_ += delta;
            return connection_recv_window_ >= 0;
        }
    }
    
    void print_stats() const {
        std::cout << "Active streams: " << streams_.size() << std::endl;
        std::cout << "Connection send window: " << connection_send_window_ << std::endl;
        std::cout << "Connection recv window: " << connection_recv_window_ << std::endl;
        
        for (const auto& [id, stream] : streams_) {
            std::cout << "  Stream " << id << ": ";
            switch (stream->state()) {
                case StreamState::IDLE: std::cout << "IDLE"; break;
                case StreamState::OPEN: std::cout << "OPEN"; break;
                case StreamState::HALF_CLOSED_LOCAL: std::cout << "HALF_CLOSED_LOCAL"; break;
                case StreamState::HALF_CLOSED_REMOTE: std::cout << "HALF_CLOSED_REMOTE"; break;
                case StreamState::CLOSED: std::cout << "CLOSED"; break;
                default: std::cout << "UNKNOWN"; break;
            }
            std::cout << " (send: " << stream->send_window() 
                     << ", recv: " << stream->recv_window() << ")" << std::endl;
        }
    }
};

int main() {
    std::cout << "HTTP/2 Stream Manager Demo\n" << std::endl;
    
    // Create client connection
    HTTP2Connection conn(true, 10);
    
    // Create multiple streams
    auto* stream1 = conn.create_stream();
    auto* stream2 = conn.create_stream();
    
    std::cout << "Created streams " << stream1->id() 
              << " and " << stream2->id() << std::endl;
    
    // Simulate sending HEADERS frame
    stream1->transition(FrameType::HEADERS, true, false);
    std::cout << "Stream 1 transitioned to OPEN" << std::endl;
    
    // Simulate data transfer (reduce window)
    stream1->update_send_window(-1024);
    std::cout << "Stream 1 sent 1024 bytes" << std::endl;
    
    // Update window with WINDOW_UPDATE
    stream1->update_send_window(2048);
    std::cout << "Stream 1 received WINDOW_UPDATE (+2048)" << std::endl;
    
    conn.print_stats();
    
    return 0;
}
```

### Rust Implementation - HTTP/2 Frame Handler

```rust
use std::collections::HashMap;
use std::io::{self, Read, Write};

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum FrameType {
    Data = 0x00,
    Headers = 0x01,
    Priority = 0x02,
    RstStream = 0x03,
    Settings = 0x04,
    PushPromise = 0x05,
    Ping = 0x06,
    GoAway = 0x07,
    WindowUpdate = 0x08,
    Continuation = 0x09,
}

impl TryFrom<u8> for FrameType {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x00 => Ok(FrameType::Data),
            0x01 => Ok(FrameType::Headers),
            0x02 => Ok(FrameType::Priority),
            0x03 => Ok(FrameType::RstStream),
            0x04 => Ok(FrameType::Settings),
            0x05 => Ok(FrameType::PushPromise),
            0x06 => Ok(FrameType::Ping),
            0x07 => Ok(FrameType::GoAway),
            0x08 => Ok(FrameType::WindowUpdate),
            0x09 => Ok(FrameType::Continuation),
            _ => Err("Unknown frame type"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum StreamState {
    Idle,
    ReservedLocal,
    ReservedRemote,
    Open,
    HalfClosedLocal,
    HalfClosedRemote,
    Closed,
}

pub struct FrameHeader {
    pub length: u32,      // 24-bit length
    pub frame_type: FrameType,
    pub flags: u8,
    pub stream_id: u32,   // 31-bit stream ID
}

impl FrameHeader {
    const FRAME_HEADER_SIZE: usize = 9;
    
    pub fn parse(data: &[u8]) -> io::Result<Self> {
        if data.len() < Self::FRAME_HEADER_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "Insufficient data for frame header",
            ));
        }
        
        // Parse 24-bit length
        let length = ((data[0] as u32) << 16) 
                   | ((data[1] as u32) << 8) 
                   | (data[2] as u32);
        
        // Parse frame type
        let frame_type = FrameType::try_from(data[3])
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        
        let flags = data[4];
        
        // Parse 31-bit stream ID (ignore reserved bit)
        let stream_id = u32::from_be_bytes([data[5], data[6], data[7], data[8]]) 
                       & 0x7FFF_FFFF;
        
        Ok(FrameHeader {
            length,
            frame_type,
            flags,
            stream_id,
        })
    }
    
    pub fn serialize(&self) -> [u8; Self::FRAME_HEADER_SIZE] {
        let mut buffer = [0u8; Self::FRAME_HEADER_SIZE];
        
        // Serialize 24-bit length
        buffer[0] = ((self.length >> 16) & 0xFF) as u8;
        buffer[1] = ((self.length >> 8) & 0xFF) as u8;
        buffer[2] = (self.length & 0xFF) as u8;
        
        buffer[3] = self.frame_type as u8;
        buffer[4] = self.flags;
        
        // Serialize stream ID (ensure reserved bit is 0)
        let stream_id = (self.stream_id & 0x7FFF_FFFF).to_be_bytes();
        buffer[5..9].copy_from_slice(&stream_id);
        
        buffer
    }
}

pub struct Http2Stream {
    stream_id: u32,
    state: StreamState,
    send_window: i32,
    recv_window: i32,
}

impl Http2Stream {
    pub fn new(stream_id: u32, initial_window: i32) -> Self {
        Http2Stream {
            stream_id,
            state: StreamState::Idle,
            send_window: initial_window,
            recv_window: initial_window,
        }
    }
    
    pub fn transition(&mut self, frame_type: FrameType, send: bool, end_stream: bool) {
        self.state = match (self.state, frame_type, send, end_stream) {
            (StreamState::Idle, FrameType::Headers, _, false) => StreamState::Open,
            (StreamState::Idle, FrameType::Headers, true, true) => StreamState::HalfClosedLocal,
            (StreamState::Idle, FrameType::Headers, false, true) => StreamState::HalfClosedRemote,
            (StreamState::Idle, FrameType::PushPromise, true, _) => StreamState::ReservedLocal,
            (StreamState::Idle, FrameType::PushPromise, false, _) => StreamState::ReservedRemote,
            
            (StreamState::Open, _, true, true) => StreamState::HalfClosedLocal,
            (StreamState::Open, _, false, true) => StreamState::HalfClosedRemote,
            
            (StreamState::HalfClosedLocal, _, false, true) => StreamState::Closed,
            (StreamState::HalfClosedRemote, _, true, true) => StreamState::Closed,
            
            _ => self.state, // No state change
        };
    }
    
    pub fn update_window(&mut self, delta: i32, send: bool) -> Result<(), &'static str> {
        if send {
            self.send_window = self.send_window.checked_add(delta)
                .ok_or("Window overflow")?;
        } else {
            self.recv_window = self.recv_window.checked_add(delta)
                .ok_or("Window overflow")?;
        }
        
        if (send && self.send_window < 0) || (!send && self.recv_window < 0) {
            return Err("Window underflow");
        }
        
        Ok(())
    }
    
    pub fn id(&self) -> u32 { self.stream_id }
    pub fn state(&self) -> StreamState { self.state }
    pub fn send_window(&self) -> i32 { self.send_window }
    pub fn recv_window(&self) -> i32 { self.recv_window }
}

pub struct Http2Connection {
    streams: HashMap<u32, Http2Stream>,
    next_stream_id: u32,
    connection_send_window: i32,
    connection_recv_window: i32,
    max_concurrent_streams: u32,
}

impl Http2Connection {
    pub fn new(is_client: bool, max_streams: u32) -> Self {
        Http2Connection {
            streams: HashMap::new(),
            next_stream_id: if is_client { 1 } else { 2 },
            connection_send_window: 65535,
            connection_recv_window: 65535,
            max_concurrent_streams: max_streams,
        }
    }
    
    pub fn create_stream(&mut self) -> Option<u32> {
        if self.streams.len() >= self.max_concurrent_streams as usize {
            return None;
        }
        
        let stream_id = self.next_stream_id;
        self.next_stream_id += 2; // Client uses odd, server uses even
        
        self.streams.insert(stream_id, Http2Stream::new(stream_id, 65535));
        Some(stream_id)
    }
    
    pub fn get_stream(&self, stream_id: u32) -> Option<&Http2Stream> {
        self.streams.get(&stream_id)
    }
    
    pub fn get_stream_mut(&mut self, stream_id: u32) -> Option<&mut Http2Stream> {
        self.streams.get_mut(&stream_id)
    }
    
    pub fn update_connection_window(&mut self, delta: i32, send: bool) -> Result<(), &'static str> {
        if send {
            self.connection_send_window = self.connection_send_window
                .checked_add(delta)
                .ok_or("Connection window overflow")?;
        } else {
            self.connection_recv_window = self.connection_recv_window
                .checked_add(delta)
                .ok_or("Connection window overflow")?;
        }
        Ok(())
    }
    
    pub fn print_stats(&self) {
        println!("Active streams: {}", self.streams.len());
        println!("Connection send window: {}", self.connection_send_window);
        println!("Connection recv window: {}", self.connection_recv_window);
        
        for (id, stream) in &self.streams {
            println!("  Stream {}: {:?} (send: {}, recv: {})",
                     id, stream.state(), stream.send_window(), stream.recv_window());
        }
    }
}

fn main() {
    println!("HTTP/2 Frame Handler Demo\n");
    
    // Create and parse a frame header
    let header = FrameHeader {
        length: 100,
        frame_type: FrameType::Headers,
        flags: 0x04, // END_HEADERS
        stream_id: 1,
    };
    
    let serialized = header.serialize();
    println!("Serialized frame header: {:?}", serialized);
    
    let parsed = FrameHeader::parse(&serialized).unwrap();
    println!("Parsed: length={}, type={:?}, flags={:#x}, stream_id={}",
             parsed.length, parsed.frame_type, parsed.flags, parsed.stream_id);
    
    // Create connection and streams
    let mut conn = Http2Connection::new(true, 10);
    
    let stream1 = conn.create_stream().unwrap();
    let stream2 = conn.create_stream().unwrap();
    
    println!("\nCreated streams {} and {}", stream1, stream2);
    
    // Simulate stream transitions
    if let Some(s) = conn.get_stream_mut(stream1) {
        s.transition(FrameType::Headers, true, false);
        println!("Stream {} transitioned to {:?}", stream1, s.state());
        
        s.update_window(-1024, true).unwrap();
        println!("Stream {} sent 1024 bytes, window: {}", stream1, s.send_window());
    }
    
    println!();
    conn.print_stats();
}
```

## Summary

**HTTP/2 Fundamentals** revolutionizes web performance through four primary mechanisms:

1. **Binary Framing Layer**: Replaces text-based HTTP/1.1 with an efficient binary protocol that encapsulates messages into typed frames with specific structures, enabling better parsing and error handling.

2. **Multiplexing**: Allows multiple concurrent request/response streams over a single TCP connection, identified by unique stream IDs. This eliminates head-of-line blocking at the application layer and removes the need for multiple connections or domain sharding.

3. **Server Push**: Enables servers to proactively send resources to clients before they're explicitly requested, reducing round-trip latency for dependent resources like CSS, JavaScript, and images.

4. **Header Compression (HPACK)**: Compresses HTTP headers using Huffman encoding and dynamic indexing tables, dramatically reducing overhead from redundant header transmission.

The protocol maintains semantic compatibility with HTTP/1.1 while introducing stream states, flow control through window management, and prioritization mechanisms. These innovations result in faster page loads, better resource utilization, and improved user experience, making HTTP/2 the foundation for modern web communication.