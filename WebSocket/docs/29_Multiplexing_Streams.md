# Multiplexing Streams over WebSocket

## Overview

**Stream multiplexing** is a technique that allows multiple independent logical communication channels (streams) to operate simultaneously over a single physical WebSocket connection. Instead of opening separate WebSocket connections for different data flows, multiplexing enables you to send and receive data from multiple streams through one connection, improving efficiency and reducing overhead.

This is particularly valuable in applications where you need to handle multiple concurrent conversations, file transfers, or data streams between client and server without the resource cost of maintaining multiple connections.

## Core Concepts

**Why Multiplexing?**
- **Resource Efficiency**: Each WebSocket connection consumes system resources (file descriptors, memory buffers, CPU cycles). Multiplexing reduces this overhead.
- **Connection Limits**: Browsers and servers often limit the number of concurrent connections. Multiplexing works within these constraints.
- **Reduced Latency**: Multiplexed streams share the same TCP connection, avoiding the handshake overhead of establishing multiple connections.
- **Simplified Management**: One connection is easier to manage, monitor, and secure than many.

**Key Components**:
1. **Stream ID**: A unique identifier for each logical stream
2. **Framing Protocol**: A method to distinguish which data belongs to which stream
3. **Flow Control**: Managing bandwidth allocation across streams
4. **Stream Lifecycle**: Creating, using, and closing individual streams

## Implementation Approach

A typical multiplexing protocol uses a frame structure like this:

```
+------------------+------------------+------------------+
| Stream ID (4B)   | Message Type (1B)| Length (4B)      |
+------------------+------------------+------------------+
| Payload Data (variable length)                         |
+--------------------------------------------------------+
```

## C Implementation Example

Here's a practical C implementation demonstrating WebSocket stream multiplexing:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

// Stream multiplexing constants
#define MAX_STREAMS 256
#define FRAME_HEADER_SIZE 9
#define MAX_PAYLOAD_SIZE 65536

// Message types
typedef enum {
    MSG_DATA = 0x01,
    MSG_STREAM_OPEN = 0x02,
    MSG_STREAM_CLOSE = 0x03,
    MSG_STREAM_ACK = 0x04
} MessageType;

// Multiplexed frame structure
typedef struct {
    uint32_t stream_id;
    uint8_t msg_type;
    uint32_t length;
    uint8_t *payload;
} MuxFrame;

// Stream state
typedef struct {
    uint32_t stream_id;
    int active;
    pthread_mutex_t lock;
    void (*on_data)(uint32_t stream_id, const uint8_t *data, size_t len);
    void *user_data;
} Stream;

// Multiplexer context
typedef struct {
    Stream streams[MAX_STREAMS];
    pthread_mutex_t mux_lock;
    int (*send_websocket)(const uint8_t *data, size_t len, void *ctx);
    void *ws_context;
} Multiplexer;

// Initialize multiplexer
Multiplexer* mux_create(int (*send_fn)(const uint8_t*, size_t, void*), void *ctx) {
    Multiplexer *mux = calloc(1, sizeof(Multiplexer));
    if (!mux) return NULL;
    
    pthread_mutex_init(&mux->mux_lock, NULL);
    mux->send_websocket = send_fn;
    mux->ws_context = ctx;
    
    for (int i = 0; i < MAX_STREAMS; i++) {
        pthread_mutex_init(&mux->streams[i].lock, NULL);
        mux->streams[i].stream_id = i;
        mux->streams[i].active = 0;
    }
    
    return mux;
}

// Serialize frame to bytes
uint8_t* mux_serialize_frame(const MuxFrame *frame, size_t *out_len) {
    *out_len = FRAME_HEADER_SIZE + frame->length;
    uint8_t *buffer = malloc(*out_len);
    if (!buffer) return NULL;
    
    // Pack header (big-endian)
    buffer[0] = (frame->stream_id >> 24) & 0xFF;
    buffer[1] = (frame->stream_id >> 16) & 0xFF;
    buffer[2] = (frame->stream_id >> 8) & 0xFF;
    buffer[3] = frame->stream_id & 0xFF;
    buffer[4] = frame->msg_type;
    buffer[5] = (frame->length >> 24) & 0xFF;
    buffer[6] = (frame->length >> 16) & 0xFF;
    buffer[7] = (frame->length >> 8) & 0xFF;
    buffer[8] = frame->length & 0xFF;
    
    // Copy payload
    if (frame->length > 0 && frame->payload) {
        memcpy(buffer + FRAME_HEADER_SIZE, frame->payload, frame->length);
    }
    
    return buffer;
}

// Deserialize bytes to frame
int mux_deserialize_frame(const uint8_t *data, size_t len, MuxFrame *frame) {
    if (len < FRAME_HEADER_SIZE) return -1;
    
    // Unpack header
    frame->stream_id = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                       ((uint32_t)data[2] << 8) | data[3];
    frame->msg_type = data[4];
    frame->length = ((uint32_t)data[5] << 24) | ((uint32_t)data[6] << 16) |
                    ((uint32_t)data[7] << 8) | data[8];
    
    if (len < FRAME_HEADER_SIZE + frame->length) return -1;
    
    // Point to payload (caller owns original buffer)
    frame->payload = (uint8_t*)(data + FRAME_HEADER_SIZE);
    
    return 0;
}

// Open a new stream
int mux_open_stream(Multiplexer *mux, uint32_t stream_id,
                    void (*callback)(uint32_t, const uint8_t*, size_t),
                    void *user_data) {
    if (stream_id >= MAX_STREAMS) return -1;
    
    pthread_mutex_lock(&mux->streams[stream_id].lock);
    
    if (mux->streams[stream_id].active) {
        pthread_mutex_unlock(&mux->streams[stream_id].lock);
        return -1; // Stream already active
    }
    
    mux->streams[stream_id].active = 1;
    mux->streams[stream_id].on_data = callback;
    mux->streams[stream_id].user_data = user_data;
    
    pthread_mutex_unlock(&mux->streams[stream_id].lock);
    
    // Send STREAM_OPEN message
    MuxFrame frame = {
        .stream_id = stream_id,
        .msg_type = MSG_STREAM_OPEN,
        .length = 0,
        .payload = NULL
    };
    
    size_t frame_len;
    uint8_t *frame_data = mux_serialize_frame(&frame, &frame_len);
    if (frame_data) {
        mux->send_websocket(frame_data, frame_len, mux->ws_context);
        free(frame_data);
    }
    
    return 0;
}

// Send data on a stream
int mux_send(Multiplexer *mux, uint32_t stream_id, 
             const uint8_t *data, size_t len) {
    if (stream_id >= MAX_STREAMS || !mux->streams[stream_id].active) {
        return -1;
    }
    
    MuxFrame frame = {
        .stream_id = stream_id,
        .msg_type = MSG_DATA,
        .length = len,
        .payload = (uint8_t*)data
    };
    
    size_t frame_len;
    uint8_t *frame_data = mux_serialize_frame(&frame, &frame_len);
    if (!frame_data) return -1;
    
    int result = mux->send_websocket(frame_data, frame_len, mux->ws_context);
    free(frame_data);
    
    return result;
}

// Handle incoming WebSocket data
void mux_receive(Multiplexer *mux, const uint8_t *data, size_t len) {
    MuxFrame frame;
    
    if (mux_deserialize_frame(data, len, &frame) != 0) {
        fprintf(stderr, "Failed to deserialize frame\n");
        return;
    }
    
    if (frame.stream_id >= MAX_STREAMS) {
        fprintf(stderr, "Invalid stream ID: %u\n", frame.stream_id);
        return;
    }
    
    Stream *stream = &mux->streams[frame.stream_id];
    
    switch (frame.msg_type) {
        case MSG_STREAM_OPEN:
            pthread_mutex_lock(&stream->lock);
            stream->active = 1;
            pthread_mutex_unlock(&stream->lock);
            printf("Stream %u opened\n", frame.stream_id);
            break;
            
        case MSG_DATA:
            pthread_mutex_lock(&stream->lock);
            if (stream->active && stream->on_data) {
                stream->on_data(frame.stream_id, frame.payload, 
                               frame.length);
            }
            pthread_mutex_unlock(&stream->lock);
            break;
            
        case MSG_STREAM_CLOSE:
            pthread_mutex_lock(&stream->lock);
            stream->active = 0;
            stream->on_data = NULL;
            pthread_mutex_unlock(&stream->lock);
            printf("Stream %u closed\n", frame.stream_id);
            break;
            
        default:
            fprintf(stderr, "Unknown message type: %u\n", frame.msg_type);
    }
}

// Close a stream
void mux_close_stream(Multiplexer *mux, uint32_t stream_id) {
    if (stream_id >= MAX_STREAMS) return;
    
    MuxFrame frame = {
        .stream_id = stream_id,
        .msg_type = MSG_STREAM_CLOSE,
        .length = 0,
        .payload = NULL
    };
    
    size_t frame_len;
    uint8_t *frame_data = mux_serialize_frame(&frame, &frame_len);
    if (frame_data) {
        mux->send_websocket(frame_data, frame_len, mux->ws_context);
        free(frame_data);
    }
    
    pthread_mutex_lock(&mux->streams[stream_id].lock);
    mux->streams[stream_id].active = 0;
    pthread_mutex_unlock(&mux->streams[stream_id].lock);
}

// Example callback for stream data
void handle_stream_data(uint32_t stream_id, const uint8_t *data, size_t len) {
    printf("Stream %u received %zu bytes: %.*s\n", 
           stream_id, len, (int)len, data);
}

// Example usage (mock WebSocket send function)
int mock_ws_send(const uint8_t *data, size_t len, void *ctx) {
    printf("Sending %zu bytes via WebSocket\n", len);
    return 0;
}

int main() {
    // Create multiplexer
    Multiplexer *mux = mux_create(mock_ws_send, NULL);
    
    // Open multiple streams
    mux_open_stream(mux, 0, handle_stream_data, NULL);
    mux_open_stream(mux, 1, handle_stream_data, NULL);
    
    // Send data on different streams
    const char *msg1 = "Hello on stream 0";
    const char *msg2 = "Hello on stream 1";
    
    mux_send(mux, 0, (uint8_t*)msg1, strlen(msg1));
    mux_send(mux, 1, (uint8_t*)msg2, strlen(msg2));
    
    // Simulate receiving multiplexed data
    MuxFrame recv_frame = {
        .stream_id = 0,
        .msg_type = MSG_DATA,
        .length = 13,
        .payload = (uint8_t*)"Reply stream 0"
    };
    
    size_t frame_len;
    uint8_t *frame_data = mux_serialize_frame(&recv_frame, &frame_len);
    mux_receive(mux, frame_data, frame_len);
    free(frame_data);
    
    // Close streams
    mux_close_stream(mux, 0);
    mux_close_stream(mux, 1);
    
    return 0;
}
```

## Rust Implementation Example

Here's a Rust implementation with async support and better type safety:

```rust
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{mpsc, Mutex};
use bytes::{Buf, BufMut, BytesMut};

// Message types for multiplexed protocol
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
enum MessageType {
    Data = 0x01,
    StreamOpen = 0x02,
    StreamClose = 0x03,
    StreamAck = 0x04,
}

impl MessageType {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x01 => Some(MessageType::Data),
            0x02 => Some(MessageType::StreamOpen),
            0x03 => Some(MessageType::StreamClose),
            0x04 => Some(MessageType::StreamAck),
            _ => None,
        }
    }
}

// Multiplexed frame structure
#[derive(Debug, Clone)]
struct MuxFrame {
    stream_id: u32,
    msg_type: MessageType,
    payload: Vec<u8>,
}

impl MuxFrame {
    const HEADER_SIZE: usize = 9;

    // Serialize frame to bytes
    fn encode(&self) -> Vec<u8> {
        let mut buf = BytesMut::with_capacity(Self::HEADER_SIZE + self.payload.len());
        
        buf.put_u32(self.stream_id);
        buf.put_u8(self.msg_type as u8);
        buf.put_u32(self.payload.len() as u32);
        buf.put_slice(&self.payload);
        
        buf.to_vec()
    }

    // Deserialize bytes to frame
    fn decode(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < Self::HEADER_SIZE {
            return Err("Insufficient data for header");
        }

        let mut buf = &data[..];
        let stream_id = buf.get_u32();
        let msg_type = MessageType::from_u8(buf.get_u8())
            .ok_or("Invalid message type")?;
        let length = buf.get_u32() as usize;

        if buf.len() < length {
            return Err("Insufficient data for payload");
        }

        let payload = buf[..length].to_vec();

        Ok(MuxFrame {
            stream_id,
            msg_type,
            payload,
        })
    }
}

// Stream handle for sending data
#[derive(Clone)]
pub struct StreamHandle {
    stream_id: u32,
    tx: mpsc::UnboundedSender<MuxFrame>,
}

impl StreamHandle {
    pub async fn send(&self, data: Vec<u8>) -> Result<(), &'static str> {
        let frame = MuxFrame {
            stream_id: self.stream_id,
            msg_type: MessageType::Data,
            payload: data,
        };
        
        self.tx.send(frame).map_err(|_| "Failed to send data")?;
        Ok(())
    }

    pub async fn close(self) -> Result<(), &'static str> {
        let frame = MuxFrame {
            stream_id: self.stream_id,
            msg_type: MessageType::StreamClose,
            payload: Vec::new(),
        };
        
        self.tx.send(frame).map_err(|_| "Failed to close stream")?;
        Ok(())
    }
}

// Individual stream state
struct Stream {
    stream_id: u32,
    data_tx: mpsc::UnboundedSender<Vec<u8>>,
}

// WebSocket multiplexer
pub struct Multiplexer {
    streams: Arc<Mutex<HashMap<u32, Stream>>>,
    frame_tx: mpsc::UnboundedSender<MuxFrame>,
    next_stream_id: Arc<Mutex<u32>>,
}

impl Multiplexer {
    pub fn new<F>(ws_send: F) -> Self 
    where
        F: Fn(Vec<u8>) + Send + 'static,
    {
        let (frame_tx, mut frame_rx) = mpsc::unbounded_channel::<MuxFrame>();
        
        // Spawn task to serialize and send frames
        tokio::spawn(async move {
            while let Some(frame) = frame_rx.recv().await {
                let encoded = frame.encode();
                ws_send(encoded);
            }
        });

        Multiplexer {
            streams: Arc::new(Mutex::new(HashMap::new())),
            frame_tx,
            next_stream_id: Arc::new(Mutex::new(0)),
        }
    }

    // Open a new stream
    pub async fn open_stream(&self) -> (u32, StreamHandle, mpsc::UnboundedReceiver<Vec<u8>>) {
        let mut next_id = self.next_stream_id.lock().await;
        let stream_id = *next_id;
        *next_id += 1;
        drop(next_id);

        let (data_tx, data_rx) = mpsc::unbounded_channel();
        
        let stream = Stream {
            stream_id,
            data_tx,
        };

        self.streams.lock().await.insert(stream_id, stream);

        // Send STREAM_OPEN frame
        let open_frame = MuxFrame {
            stream_id,
            msg_type: MessageType::StreamOpen,
            payload: Vec::new(),
        };
        let _ = self.frame_tx.send(open_frame);

        let handle = StreamHandle {
            stream_id,
            tx: self.frame_tx.clone(),
        };

        (stream_id, handle, data_rx)
    }

    // Handle incoming WebSocket data
    pub async fn handle_incoming(&self, data: Vec<u8>) -> Result<(), &'static str> {
        let frame = MuxFrame::decode(&data)?;

        match frame.msg_type {
            MessageType::StreamOpen => {
                println!("Remote opened stream {}", frame.stream_id);
                
                // Auto-accept incoming streams
                let (data_tx, _data_rx) = mpsc::unbounded_channel();
                let stream = Stream {
                    stream_id: frame.stream_id,
                    data_tx,
                };
                self.streams.lock().await.insert(frame.stream_id, stream);
            }
            
            MessageType::Data => {
                let streams = self.streams.lock().await;
                if let Some(stream) = streams.get(&frame.stream_id) {
                    let _ = stream.data_tx.send(frame.payload);
                }
            }
            
            MessageType::StreamClose => {
                self.streams.lock().await.remove(&frame.stream_id);
                println!("Stream {} closed", frame.stream_id);
            }
            
            MessageType::StreamAck => {
                // Handle acknowledgment if needed
            }
        }

        Ok(())
    }

    pub async fn close_stream(&self, stream_id: u32) {
        self.streams.lock().await.remove(&stream_id);
        
        let close_frame = MuxFrame {
            stream_id,
            msg_type: MessageType::StreamClose,
            payload: Vec::new(),
        };
        let _ = self.frame_tx.send(close_frame);
    }
}

// Example usage
#[tokio::main]
async fn main() {
    // Create multiplexer with mock WebSocket sender
    let mux = Multiplexer::new(|data| {
        println!("Sending {} bytes via WebSocket", data.len());
    });

    // Open first stream
    let (stream_id1, handle1, mut rx1) = mux.open_stream().await;
    println!("Opened stream {}", stream_id1);

    // Open second stream
    let (stream_id2, handle2, mut rx2) = mux.open_stream().await;
    println!("Opened stream {}", stream_id2);

    // Spawn task to handle stream 1 data
    tokio::spawn(async move {
        while let Some(data) = rx1.recv().await {
            println!("Stream {} received: {:?}", stream_id1, 
                     String::from_utf8_lossy(&data));
        }
    });

    // Spawn task to handle stream 2 data
    tokio::spawn(async move {
        while let Some(data) = rx2.recv().await {
            println!("Stream {} received: {:?}", stream_id2, 
                     String::from_utf8_lossy(&data));
        }
    });

    // Send data on both streams
    handle1.send(b"Hello from stream 1".to_vec()).await.unwrap();
    handle2.send(b"Hello from stream 2".to_vec()).await.unwrap();

    // Simulate receiving data
    let incoming_frame = MuxFrame {
        stream_id: stream_id1,
        msg_type: MessageType::Data,
        payload: b"Reply to stream 1".to_vec(),
    };
    
    mux.handle_incoming(incoming_frame.encode()).await.unwrap();

    // Close streams
    handle1.close().await.unwrap();
    handle2.close().await.unwrap();

    tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
}
```

## Advanced Considerations

**Flow Control**: Implement per-stream flow control to prevent one busy stream from starving others. This can be done using window-based flow control similar to HTTP/2.

**Priority Queuing**: Assign priorities to streams and use a priority queue to ensure important streams get bandwidth when needed.

**Backpressure**: Monitor buffer sizes and apply backpressure when a stream's receive buffer is full, preventing memory exhaustion.

**Error Handling**: Design the protocol to handle stream-level errors without affecting other streams or the underlying connection.

**Security**: Validate stream IDs, enforce quotas, and implement access control to prevent malicious clients from opening excessive streams.

## Summary

Multiplexing streams over WebSocket provides an elegant solution for managing multiple concurrent data flows efficiently. The key benefits include reduced connection overhead, better resource utilization, and simplified connection management. The implementation requires careful design of a framing protocol, stream lifecycle management, and proper synchronization when handling concurrent operations. Both C and Rust implementations demonstrate the core concepts: serialization/deserialization of multiplexed frames, stream state management, and routing of data to appropriate handlers. This technique is particularly valuable for real-time applications like collaborative tools, multiplayer games, and complex dashboard applications where multiple independent data channels are needed simultaneously.