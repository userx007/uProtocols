# Stream Processing in WebSocket

## Overview

Stream processing in WebSocket refers to the handling of incoming data in a continuous, incremental manner rather than waiting for complete messages. Since WebSocket data arrives over a network as a stream of bytes, you often receive partial frames or fragments that must be assembled correctly while maintaining proper state transitions. This is crucial for building robust, high-performance WebSocket implementations.

## Core Concepts

### 1. **Partial Frame Handling**
WebSocket frames may arrive in chunks. You might receive:
- Only part of the frame header
- Header complete but payload incomplete
- Multiple frames in a single TCP read
- A frame split across multiple network packets

### 2. **State Machines**
A state machine tracks the current parsing state:
- **READING_HEADER**: Parsing frame header bytes
- **READING_PAYLOAD_LENGTH**: Reading extended payload length
- **READING_MASK_KEY**: Reading the 4-byte masking key (client frames)
- **READING_PAYLOAD**: Reading actual payload data
- **FRAME_COMPLETE**: Frame ready for processing

### 3. **Buffering Strategy**
Efficient buffering is essential:
- Maintain a buffer for incomplete data
- Avoid excessive memory allocations
- Handle backpressure when data arrives faster than it can be processed

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// WebSocket frame opcodes
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

// Parser states
typedef enum {
    WS_STATE_READING_HEADER,
    WS_STATE_READING_EXTENDED_LENGTH_16,
    WS_STATE_READING_EXTENDED_LENGTH_64,
    WS_STATE_READING_MASK_KEY,
    WS_STATE_READING_PAYLOAD,
    WS_STATE_FRAME_COMPLETE
} ws_parser_state_t;

// Frame structure
typedef struct {
    bool fin;
    bool rsv1, rsv2, rsv3;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t mask_key[4];
    uint8_t *payload;
    uint64_t payload_received;
} ws_frame_t;

// Parser context
typedef struct {
    ws_parser_state_t state;
    ws_frame_t current_frame;
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffer_pos;
    uint8_t header_bytes_read;
} ws_parser_t;

// Initialize parser
void ws_parser_init(ws_parser_t *parser) {
    memset(parser, 0, sizeof(ws_parser_t));
    parser->state = WS_STATE_READING_HEADER;
    parser->buffer_size = 8192;
    parser->buffer = (uint8_t *)malloc(parser->buffer_size);
}

// Process incoming stream data
int ws_parser_process(ws_parser_t *parser, const uint8_t *data, size_t len) {
    size_t offset = 0;
    
    while (offset < len) {
        switch (parser->state) {
            case WS_STATE_READING_HEADER: {
                // Need at least 2 bytes for basic header
                if (parser->header_bytes_read < 2) {
                    parser->buffer[parser->header_bytes_read++] = data[offset++];
                    
                    if (parser->header_bytes_read == 2) {
                        // Parse first byte
                        parser->current_frame.fin = (parser->buffer[0] & 0x80) != 0;
                        parser->current_frame.rsv1 = (parser->buffer[0] & 0x40) != 0;
                        parser->current_frame.rsv2 = (parser->buffer[0] & 0x20) != 0;
                        parser->current_frame.rsv3 = (parser->buffer[0] & 0x10) != 0;
                        parser->current_frame.opcode = parser->buffer[0] & 0x0F;
                        
                        // Parse second byte
                        parser->current_frame.masked = (parser->buffer[1] & 0x80) != 0;
                        uint8_t payload_len = parser->buffer[1] & 0x7F;
                        
                        if (payload_len < 126) {
                            parser->current_frame.payload_length = payload_len;
                            parser->state = parser->current_frame.masked ? 
                                          WS_STATE_READING_MASK_KEY : 
                                          WS_STATE_READING_PAYLOAD;
                        } else if (payload_len == 126) {
                            parser->state = WS_STATE_READING_EXTENDED_LENGTH_16;
                        } else {
                            parser->state = WS_STATE_READING_EXTENDED_LENGTH_64;
                        }
                        parser->header_bytes_read = 0;
                    }
                }
                break;
            }
            
            case WS_STATE_READING_EXTENDED_LENGTH_16: {
                parser->buffer[parser->header_bytes_read++] = data[offset++];
                if (parser->header_bytes_read == 2) {
                    parser->current_frame.payload_length = 
                        ((uint16_t)parser->buffer[0] << 8) | parser->buffer[1];
                    parser->state = parser->current_frame.masked ? 
                                  WS_STATE_READING_MASK_KEY : 
                                  WS_STATE_READING_PAYLOAD;
                    parser->header_bytes_read = 0;
                }
                break;
            }
            
            case WS_STATE_READING_EXTENDED_LENGTH_64: {
                parser->buffer[parser->header_bytes_read++] = data[offset++];
                if (parser->header_bytes_read == 8) {
                    parser->current_frame.payload_length = 0;
                    for (int i = 0; i < 8; i++) {
                        parser->current_frame.payload_length = 
                            (parser->current_frame.payload_length << 8) | parser->buffer[i];
                    }
                    parser->state = parser->current_frame.masked ? 
                                  WS_STATE_READING_MASK_KEY : 
                                  WS_STATE_READING_PAYLOAD;
                    parser->header_bytes_read = 0;
                }
                break;
            }
            
            case WS_STATE_READING_MASK_KEY: {
                parser->current_frame.mask_key[parser->header_bytes_read++] = data[offset++];
                if (parser->header_bytes_read == 4) {
                    parser->state = WS_STATE_READING_PAYLOAD;
                    parser->header_bytes_read = 0;
                    
                    // Allocate payload buffer
                    if (parser->current_frame.payload_length > 0) {
                        parser->current_frame.payload = 
                            (uint8_t *)malloc(parser->current_frame.payload_length);
                        parser->current_frame.payload_received = 0;
                    }
                }
                break;
            }
            
            case WS_STATE_READING_PAYLOAD: {
                if (parser->current_frame.payload_length == 0) {
                    parser->state = WS_STATE_FRAME_COMPLETE;
                    break;
                }
                
                // Calculate how much we can read
                uint64_t remaining = parser->current_frame.payload_length - 
                                    parser->current_frame.payload_received;
                size_t available = len - offset;
                size_t to_read = (remaining < available) ? remaining : available;
                
                // Copy data
                memcpy(parser->current_frame.payload + parser->current_frame.payload_received,
                       data + offset, to_read);
                
                // Unmask if needed
                if (parser->current_frame.masked) {
                    for (size_t i = 0; i < to_read; i++) {
                        uint64_t idx = parser->current_frame.payload_received + i;
                        parser->current_frame.payload[idx] ^= 
                            parser->current_frame.mask_key[idx % 4];
                    }
                }
                
                parser->current_frame.payload_received += to_read;
                offset += to_read;
                
                if (parser->current_frame.payload_received == 
                    parser->current_frame.payload_length) {
                    parser->state = WS_STATE_FRAME_COMPLETE;
                }
                break;
            }
            
            case WS_STATE_FRAME_COMPLETE: {
                // Frame is ready for processing
                printf("Frame complete: opcode=%d, length=%llu, fin=%d\n",
                       parser->current_frame.opcode,
                       parser->current_frame.payload_length,
                       parser->current_frame.fin);
                
                // Reset for next frame
                if (parser->current_frame.payload) {
                    free(parser->current_frame.payload);
                }
                memset(&parser->current_frame, 0, sizeof(ws_frame_t));
                parser->state = WS_STATE_READING_HEADER;
                break;
            }
        }
    }
    
    return 0;
}

// Cleanup
void ws_parser_destroy(ws_parser_t *parser) {
    if (parser->buffer) {
        free(parser->buffer);
    }
    if (parser->current_frame.payload) {
        free(parser->current_frame.payload);
    }
}
```

## Rust Implementation

```rust
use std::io::{self, ErrorKind};

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
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

#[derive(Debug)]
pub struct Frame {
    pub fin: bool,
    pub rsv1: bool,
    pub rsv2: bool,
    pub rsv3: bool,
    pub opcode: OpCode,
    pub masked: bool,
    pub payload: Vec<u8>,
}

#[derive(Debug, PartialEq)]
enum ParserState {
    ReadingHeader,
    ReadingExtendedLength16,
    ReadingExtendedLength64,
    ReadingMaskKey,
    ReadingPayload,
}

pub struct StreamParser {
    state: ParserState,
    buffer: Vec<u8>,
    
    // Current frame being parsed
    fin: bool,
    rsv1: bool,
    rsv2: bool,
    rsv3: bool,
    opcode: Option<OpCode>,
    masked: bool,
    payload_length: u64,
    mask_key: [u8; 4],
    payload: Vec<u8>,
    
    // Tracking
    bytes_needed: usize,
    bytes_read: usize,
}

impl StreamParser {
    pub fn new() -> Self {
        Self {
            state: ParserState::ReadingHeader,
            buffer: Vec::with_capacity(8192),
            fin: false,
            rsv1: false,
            rsv2: false,
            rsv3: false,
            opcode: None,
            masked: false,
            payload_length: 0,
            mask_key: [0; 4],
            payload: Vec::new(),
            bytes_needed: 2,
            bytes_read: 0,
        }
    }
    
    /// Process incoming bytes and return completed frames
    pub fn process(&mut self, data: &[u8]) -> io::Result<Vec<Frame>> {
        self.buffer.extend_from_slice(data);
        let mut frames = Vec::new();
        
        loop {
            match self.state {
                ParserState::ReadingHeader => {
                    if self.buffer.len() < 2 {
                        break;
                    }
                    
                    let byte1 = self.buffer[0];
                    let byte2 = self.buffer[1];
                    
                    self.fin = (byte1 & 0x80) != 0;
                    self.rsv1 = (byte1 & 0x40) != 0;
                    self.rsv2 = (byte1 & 0x20) != 0;
                    self.rsv3 = (byte1 & 0x10) != 0;
                    
                    let opcode_value = byte1 & 0x0F;
                    self.opcode = OpCode::from_u8(opcode_value);
                    
                    if self.opcode.is_none() {
                        return Err(io::Error::new(
                            ErrorKind::InvalidData,
                            "Invalid opcode",
                        ));
                    }
                    
                    self.masked = (byte2 & 0x80) != 0;
                    let payload_len = (byte2 & 0x7F) as u64;
                    
                    self.buffer.drain(..2);
                    
                    if payload_len < 126 {
                        self.payload_length = payload_len;
                        self.transition_to_mask_or_payload();
                    } else if payload_len == 126 {
                        self.state = ParserState::ReadingExtendedLength16;
                        self.bytes_needed = 2;
                    } else {
                        self.state = ParserState::ReadingExtendedLength64;
                        self.bytes_needed = 8;
                    }
                }
                
                ParserState::ReadingExtendedLength16 => {
                    if self.buffer.len() < 2 {
                        break;
                    }
                    
                    self.payload_length = u16::from_be_bytes([
                        self.buffer[0],
                        self.buffer[1],
                    ]) as u64;
                    
                    self.buffer.drain(..2);
                    self.transition_to_mask_or_payload();
                }
                
                ParserState::ReadingExtendedLength64 => {
                    if self.buffer.len() < 8 {
                        break;
                    }
                    
                    let mut bytes = [0u8; 8];
                    bytes.copy_from_slice(&self.buffer[..8]);
                    self.payload_length = u64::from_be_bytes(bytes);
                    
                    self.buffer.drain(..8);
                    self.transition_to_mask_or_payload();
                }
                
                ParserState::ReadingMaskKey => {
                    if self.buffer.len() < 4 {
                        break;
                    }
                    
                    self.mask_key.copy_from_slice(&self.buffer[..4]);
                    self.buffer.drain(..4);
                    
                    self.payload.reserve(self.payload_length as usize);
                    self.state = ParserState::ReadingPayload;
                    self.bytes_needed = self.payload_length as usize;
                }
                
                ParserState::ReadingPayload => {
                    let remaining = self.payload_length as usize - self.payload.len();
                    
                    if remaining == 0 {
                        // Frame complete
                        frames.push(self.extract_frame());
                        self.reset_for_next_frame();
                        continue;
                    }
                    
                    let available = self.buffer.len().min(remaining);
                    
                    if available == 0 {
                        break;
                    }
                    
                    let chunk: Vec<u8> = self.buffer.drain(..available).collect();
                    
                    // Unmask if needed
                    if self.masked {
                        let start_idx = self.payload.len();
                        for (i, byte) in chunk.iter().enumerate() {
                            let unmasked = byte ^ self.mask_key[(start_idx + i) % 4];
                            self.payload.push(unmasked);
                        }
                    } else {
                        self.payload.extend_from_slice(&chunk);
                    }
                }
            }
        }
        
        Ok(frames)
    }
    
    fn transition_to_mask_or_payload(&mut self) {
        if self.masked {
            self.state = ParserState::ReadingMaskKey;
        } else {
            self.payload.reserve(self.payload_length as usize);
            self.state = ParserState::ReadingPayload;
        }
    }
    
    fn extract_frame(&mut self) -> Frame {
        Frame {
            fin: self.fin,
            rsv1: self.rsv1,
            rsv2: self.rsv2,
            rsv3: self.rsv3,
            opcode: self.opcode.unwrap(),
            masked: self.masked,
            payload: std::mem::take(&mut self.payload),
        }
    }
    
    fn reset_for_next_frame(&mut self) {
        self.state = ParserState::ReadingHeader;
        self.fin = false;
        self.rsv1 = false;
        self.rsv2 = false;
        self.rsv3 = false;
        self.opcode = None;
        self.masked = false;
        self.payload_length = 0;
        self.mask_key = [0; 4];
        self.payload.clear();
        self.bytes_needed = 2;
        self.bytes_read = 0;
    }
}

// Example usage
fn main() -> io::Result<()> {
    let mut parser = StreamParser::new();
    
    // Simulate receiving data in chunks
    let chunk1 = vec![0x81, 0x85]; // Text frame, masked, 5 bytes payload
    let chunk2 = vec![0x37, 0xfa, 0x21, 0x3d]; // Mask key
    let chunk3 = vec![0x7f, 0x9f, 0x4d, 0x51, 0x58]; // Masked payload
    
    for chunk in [chunk1, chunk2, chunk3] {
        let frames = parser.process(&chunk)?;
        for frame in frames {
            println!("Received frame: opcode={:?}, length={}", 
                     frame.opcode, frame.payload.len());
            if let Ok(text) = String::from_utf8(frame.payload.clone()) {
                println!("Text content: {}", text);
            }
        }
    }
    
    Ok(())
}
```

## Summary

**Stream processing** in WebSocket is essential for handling real-world network conditions where data arrives incrementally. The key components are:

1. **State Machine Design**: Tracks parser progression through header reading, length decoding, mask key extraction, and payload assembly
2. **Partial Frame Handling**: Buffers incomplete data across multiple read operations until complete frames are assembled
3. **Efficient Buffering**: Minimizes allocations and memory copies while handling variable-length payloads
4. **Incremental Processing**: Processes available bytes immediately without blocking on complete frames

Both C/C++ and Rust implementations demonstrate how to build robust parsers that handle fragmented network data, maintain state across multiple calls, and correctly unmask client-sent frames. The Rust version leverages type safety and memory safety guarantees, while the C version offers fine-grained control over memory management. Proper stream processing ensures WebSocket implementations are reliable, performant, and capable of handling high-throughput scenarios with varying network conditions.