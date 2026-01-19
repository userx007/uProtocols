# WebSocket Message Fragmentation

## Detailed Description

**Message Fragmentation** is a core WebSocket protocol feature that allows large messages to be split into multiple smaller frames for transmission. This mechanism is essential for efficient network communication, preventing buffer overflows, and enabling interleaved transmission of multiple messages.

### Why Fragmentation Matters

When dealing with large payloads (such as file transfers, streaming data, or large JSON documents), sending them as a single frame can cause several problems:

1. **Memory constraints**: Large frames require significant contiguous memory allocation
2. **Latency**: The entire message must be buffered before transmission begins
3. **Blocking**: Other messages cannot be sent until the large frame completes
4. **Network efficiency**: Large frames are more susceptible to packet loss issues

Fragmentation solves these problems by breaking messages into manageable chunks.

### How WebSocket Fragmentation Works

WebSocket frames use two control bits in the frame header:

- **FIN bit**: Indicates if this is the final fragment (1) or if more fragments follow (0)
- **Opcode**: The first fragment contains the actual opcode (text=0x1, binary=0x2), while continuation fragments use opcode 0x0

**Frame sequence for a fragmented message:**
1. First fragment: FIN=0, Opcode=0x1 or 0x2
2. Middle fragments: FIN=0, Opcode=0x0 (continuation)
3. Final fragment: FIN=1, Opcode=0x0 (continuation)

**Important rules:**
- Control frames (ping, pong, close) cannot be fragmented
- Control frames can be injected between fragmented message frames
- All fragments of a message must have the same data type (text or binary)
- Fragmentation is hop-by-hop; intermediaries may re-fragment messages

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define FRAGMENT_SIZE 1024  // Maximum fragment size
#define MAX_MESSAGE_SIZE 10240

// WebSocket opcodes
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA

// WebSocket frame header structure
typedef struct {
    uint8_t fin;
    uint8_t opcode;
    uint8_t mask;
    uint64_t payload_length;
    uint8_t masking_key[4];
} ws_frame_header_t;

// Message reassembly buffer
typedef struct {
    uint8_t *buffer;
    size_t length;
    size_t capacity;
    uint8_t opcode;  // Original opcode (text/binary)
    bool receiving;
} ws_message_buffer_t;

// Initialize message buffer
ws_message_buffer_t* ws_message_buffer_init(size_t initial_capacity) {
    ws_message_buffer_t *buf = (ws_message_buffer_t*)malloc(sizeof(ws_message_buffer_t));
    if (!buf) return NULL;
    
    buf->buffer = (uint8_t*)malloc(initial_capacity);
    if (!buf->buffer) {
        free(buf);
        return NULL;
    }
    
    buf->capacity = initial_capacity;
    buf->length = 0;
    buf->receiving = false;
    buf->opcode = 0;
    
    return buf;
}

// Free message buffer
void ws_message_buffer_free(ws_message_buffer_t *buf) {
    if (buf) {
        free(buf->buffer);
        free(buf);
    }
}

// Append fragment to buffer
bool ws_message_buffer_append(ws_message_buffer_t *buf, 
                              const uint8_t *data, 
                              size_t length) {
    // Resize if needed
    if (buf->length + length > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        while (new_capacity < buf->length + length) {
            new_capacity *= 2;
        }
        
        uint8_t *new_buffer = (uint8_t*)realloc(buf->buffer, new_capacity);
        if (!new_buffer) return false;
        
        buf->buffer = new_buffer;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->buffer + buf->length, data, length);
    buf->length += length;
    
    return true;
}

// Send a fragmented message
int ws_send_fragmented(int sockfd, 
                       const uint8_t *message, 
                       size_t message_len,
                       uint8_t opcode,
                       size_t fragment_size) {
    size_t offset = 0;
    
    while (offset < message_len) {
        size_t chunk_size = (message_len - offset > fragment_size) 
                           ? fragment_size 
                           : (message_len - offset);
        
        ws_frame_header_t header;
        
        // First fragment
        if (offset == 0) {
            header.fin = (chunk_size == message_len) ? 1 : 0;
            header.opcode = opcode;
        }
        // Continuation fragments
        else {
            header.fin = (offset + chunk_size == message_len) ? 1 : 0;
            header.opcode = WS_OPCODE_CONTINUATION;
        }
        
        header.mask = 0;  // Server doesn't mask
        header.payload_length = chunk_size;
        
        // In production, you'd encode the header and send it
        printf("Sending fragment: FIN=%d, Opcode=0x%x, Length=%zu\n",
               header.fin, header.opcode, chunk_size);
        
        // send(sockfd, &encoded_header, header_size, 0);
        // send(sockfd, message + offset, chunk_size, 0);
        
        offset += chunk_size;
    }
    
    return 0;
}

// Receive and reassemble fragmented message
int ws_receive_fragmented(ws_message_buffer_t *msg_buf,
                          const ws_frame_header_t *header,
                          const uint8_t *payload) {
    // Handle first fragment
    if (header->opcode != WS_OPCODE_CONTINUATION) {
        if (msg_buf->receiving) {
            fprintf(stderr, "Error: Received new message while reassembling\n");
            return -1;
        }
        
        msg_buf->receiving = true;
        msg_buf->opcode = header->opcode;
        msg_buf->length = 0;
    }
    // Handle continuation fragment
    else if (!msg_buf->receiving) {
        fprintf(stderr, "Error: Received continuation without initial fragment\n");
        return -1;
    }
    
    // Append payload to buffer
    if (!ws_message_buffer_append(msg_buf, payload, header->payload_length)) {
        fprintf(stderr, "Error: Failed to append fragment\n");
        return -1;
    }
    
    // Check if this is the final fragment
    if (header->fin) {
        printf("Message complete: Type=0x%x, Length=%zu\n", 
               msg_buf->opcode, msg_buf->length);
        
        // Process complete message here
        if (msg_buf->opcode == WS_OPCODE_TEXT) {
            printf("Text message: %.*s\n", (int)msg_buf->length, msg_buf->buffer);
        }
        
        // Reset for next message
        msg_buf->receiving = false;
        msg_buf->length = 0;
    }
    
    return 0;
}

// Example usage
int main() {
    // Initialize message buffer
    ws_message_buffer_t *msg_buf = ws_message_buffer_init(MAX_MESSAGE_SIZE);
    if (!msg_buf) {
        fprintf(stderr, "Failed to initialize message buffer\n");
        return 1;
    }
    
    // Example: Send fragmented message
    const char *large_message = "This is a very large message that will be "
                                "split into multiple fragments for transmission "
                                "over the WebSocket connection.";
    
    printf("=== Sending Fragmented Message ===\n");
    ws_send_fragmented(0, (uint8_t*)large_message, strlen(large_message),
                       WS_OPCODE_TEXT, 20);
    
    // Example: Receive fragmented message
    printf("\n=== Receiving Fragmented Message ===\n");
    
    // Simulate receiving fragments
    const char *frag1 = "First fragment ";
    const char *frag2 = "second fragment ";
    const char *frag3 = "final fragment";
    
    ws_frame_header_t header1 = {0, WS_OPCODE_TEXT, 0, strlen(frag1), {0}};
    ws_frame_header_t header2 = {0, WS_OPCODE_CONTINUATION, 0, strlen(frag2), {0}};
    ws_frame_header_t header3 = {1, WS_OPCODE_CONTINUATION, 0, strlen(frag3), {0}};
    
    ws_receive_fragmented(msg_buf, &header1, (uint8_t*)frag1);
    ws_receive_fragmented(msg_buf, &header2, (uint8_t*)frag2);
    ws_receive_fragmented(msg_buf, &header3, (uint8_t*)frag3);
    
    // Cleanup
    ws_message_buffer_free(msg_buf);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::VecDeque;

// WebSocket opcodes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

impl Opcode {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x0 => Some(Opcode::Continuation),
            0x1 => Some(Opcode::Text),
            0x2 => Some(Opcode::Binary),
            0x8 => Some(Opcode::Close),
            0x9 => Some(Opcode::Ping),
            0xA => Some(Opcode::Pong),
            _ => None,
        }
    }
}

// WebSocket frame header
#[derive(Debug, Clone)]
struct FrameHeader {
    fin: bool,
    opcode: Opcode,
    masked: bool,
    payload_length: u64,
    masking_key: Option<[u8; 4]>,
}

// WebSocket frame
#[derive(Debug, Clone)]
struct Frame {
    header: FrameHeader,
    payload: Vec<u8>,
}

impl Frame {
    fn new(fin: bool, opcode: Opcode, payload: Vec<u8>) -> Self {
        Frame {
            header: FrameHeader {
                fin,
                opcode,
                masked: false,
                payload_length: payload.len() as u64,
                masking_key: None,
            },
            payload,
        }
    }
}

// Message reassembly buffer
struct MessageBuffer {
    buffer: Vec<u8>,
    opcode: Option<Opcode>,
    receiving: bool,
}

impl MessageBuffer {
    fn new() -> Self {
        MessageBuffer {
            buffer: Vec::new(),
            opcode: None,
            receiving: false,
        }
    }

    fn append_fragment(&mut self, frame: &Frame) -> Result<Option<Message>, String> {
        // Handle first fragment
        if frame.header.opcode != Opcode::Continuation {
            if self.receiving {
                return Err("Received new message while reassembling".to_string());
            }
            self.receiving = true;
            self.opcode = Some(frame.header.opcode);
            self.buffer.clear();
        } else if !self.receiving {
            return Err("Received continuation without initial fragment".to_string());
        }

        // Append payload
        self.buffer.extend_from_slice(&frame.payload);

        // Check if final fragment
        if frame.header.fin {
            let message = Message {
                opcode: self.opcode.unwrap(),
                data: self.buffer.clone(),
            };
            
            self.receiving = false;
            self.buffer.clear();
            self.opcode = None;
            
            Ok(Some(message))
        } else {
            Ok(None)
        }
    }

    fn is_receiving(&self) -> bool {
        self.receiving
    }
}

// Complete message
#[derive(Debug, Clone)]
struct Message {
    opcode: Opcode,
    data: Vec<u8>,
}

impl Message {
    fn as_text(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.data.clone())
    }
}

// Message fragmenter
struct MessageFragmenter {
    fragment_size: usize,
}

impl MessageFragmenter {
    fn new(fragment_size: usize) -> Self {
        MessageFragmenter { fragment_size }
    }

    fn fragment_message(&self, data: &[u8], opcode: Opcode) -> Vec<Frame> {
        let mut frames = Vec::new();
        let mut offset = 0;

        while offset < data.len() {
            let chunk_size = std::cmp::min(self.fragment_size, data.len() - offset);
            let chunk = data[offset..offset + chunk_size].to_vec();
            
            let frame_opcode = if offset == 0 {
                opcode
            } else {
                Opcode::Continuation
            };
            
            let is_final = offset + chunk_size == data.len();
            
            frames.push(Frame::new(is_final, frame_opcode, chunk));
            offset += chunk_size;
        }

        frames
    }
}

// WebSocket message handler
struct WebSocketHandler {
    message_buffer: MessageBuffer,
    fragmenter: MessageFragmenter,
}

impl WebSocketHandler {
    fn new(fragment_size: usize) -> Self {
        WebSocketHandler {
            message_buffer: MessageBuffer::new(),
            fragmenter: MessageFragmenter::new(fragment_size),
        }
    }

    fn send_message(&self, data: &[u8], opcode: Opcode) -> Vec<Frame> {
        self.fragmenter.fragment_message(data, opcode)
    }

    fn receive_frame(&mut self, frame: Frame) -> Result<Option<Message>, String> {
        // Control frames cannot be fragmented
        if matches!(frame.header.opcode, Opcode::Close | Opcode::Ping | Opcode::Pong) {
            if !frame.header.fin {
                return Err("Control frames cannot be fragmented".to_string());
            }
            // Process control frame immediately
            return Ok(Some(Message {
                opcode: frame.header.opcode,
                data: frame.payload.clone(),
            }));
        }

        // Handle data frames
        self.message_buffer.append_fragment(&frame)
    }
}

// Example usage
fn main() {
    let mut handler = WebSocketHandler::new(20);

    println!("=== Sending Fragmented Message ===");
    let large_message = b"This is a very large message that will be split into multiple fragments for transmission over the WebSocket connection.";
    
    let fragments = handler.send_message(large_message, Opcode::Text);
    
    for (i, frame) in fragments.iter().enumerate() {
        println!(
            "Fragment {}: FIN={}, Opcode={:?}, Length={}",
            i + 1,
            frame.header.fin,
            frame.header.opcode,
            frame.payload.len()
        );
    }

    println!("\n=== Receiving Fragmented Message ===");
    
    // Simulate receiving fragments
    let frag1 = Frame::new(false, Opcode::Text, b"First fragment ".to_vec());
    let frag2 = Frame::new(false, Opcode::Continuation, b"second fragment ".to_vec());
    let frag3 = Frame::new(true, Opcode::Continuation, b"final fragment".to_vec());

    match handler.receive_frame(frag1) {
        Ok(None) => println!("Fragment 1 received, waiting for more..."),
        Ok(Some(_)) => println!("Unexpected complete message"),
        Err(e) => println!("Error: {}", e),
    }

    match handler.receive_frame(frag2) {
        Ok(None) => println!("Fragment 2 received, waiting for more..."),
        Ok(Some(_)) => println!("Unexpected complete message"),
        Err(e) => println!("Error: {}", e),
    }

    match handler.receive_frame(frag3) {
        Ok(None) => println!("Unexpected incomplete message"),
        Ok(Some(msg)) => {
            println!("Message complete!");
            println!("Type: {:?}", msg.opcode);
            println!("Length: {}", msg.data.len());
            if let Ok(text) = msg.as_text() {
                println!("Content: {}", text);
            }
        }
        Err(e) => println!("Error: {}", e),
    }

    // Example: Sending text message with automatic fragmentation
    println!("\n=== Auto-fragmentation Example ===");
    let text_message = b"Hello WebSocket World!";
    let frames = handler.send_message(text_message, Opcode::Text);
    println!("Message split into {} frame(s)", frames.len());
}
```

---

## Summary

**WebSocket Message Fragmentation** enables efficient transmission of large messages by splitting them into smaller frames. The protocol uses the FIN bit and opcode field to distinguish between initial fragments (with original opcode), continuation fragments (opcode 0x0), and final fragments (FIN=1). This mechanism provides several benefits: reduced memory overhead by avoiding large contiguous allocations, lower latency through streaming transmission, non-blocking communication by allowing control frames to be interleaved, and improved network reliability with smaller packet sizes.

The implementations demonstrate both sending (fragmenting outbound messages) and receiving (reassembling inbound fragments) workflows. The C/C++ version shows low-level buffer management with dynamic reallocation, while the Rust version leverages type safety and Result types for robust error handling. Both implementations handle the critical aspects: tracking fragmentation state, validating fragment sequences, dynamically growing reassembly buffers, and distinguishing between data and control frames. Proper fragmentation handling is essential for building production-ready WebSocket applications that can handle arbitrary message sizes efficiently.