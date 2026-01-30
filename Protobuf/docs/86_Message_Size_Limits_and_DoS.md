# Protocol Buffers: Message Size Limits and DoS Protection

The document covers:

**Core Concepts:**
- Why size limits exist (memory exhaustion, resource starvation)
- Standard limits (64 MB default, 2 GB hard maximum, 1 MB recommended)
- Security implications of parsing untrusted messages

**C++ Implementation:**
- Basic parsing with `CodedInputStream::SetTotalBytesLimit()`
- Streaming multiple messages with length-delimited encoding
- Production-ready parser with comprehensive validation
- Legacy API notes for older Protobuf versions

**Rust Implementation:**
- Examples using both `prost` and `rust-protobuf` libraries
- Size validation before parsing
- Streaming message handling
- Production-ready parser with timing metrics

**Best Practices:**
- Setting appropriate limits
- Per-message limits for streams
- Monitoring and logging violations
- Recursion depth controls
- Timeout mechanisms
- Rate limiting strategies

**Common Pitfalls:**
- Cumulative limits in streams
- Validating after vs. before parsing
- Integer overflow concerns

The document includes a security checklist and complete working examples that you can adapt to your specific needs.

## Overview

Protocol Buffers (Protobuf) implements various message size limits to protect applications from denial-of-service (DoS) attacks and excessive memory allocation. When an attacker or malformed data sends extremely large messages, servers can run out of memory trying to parse them. Understanding and properly configuring these limits is critical for building secure, production-ready systems.

## Key Concepts

### Why Size Limits Matter

1. **Memory Exhaustion**: Parsing large messages requires allocating substantial RAM. A malicious actor could send messages claiming to be gigabytes in size, causing the server to crash.

2. **Resource Starvation**: Even if a single message doesn't crash the server, many concurrent large messages can exhaust system resources and make the service unavailable.

3. **Algorithmic Complexity**: Very deep nesting or complex message structures can lead to exponential parsing time.

### Standard Size Limits

Protocol Buffers implementations have several built-in limits:

| Limit Type | Default Value | Maximum Value | Rationale |
|------------|---------------|---------------|-----------|
| **Default Parse Limit** | 64 MB | - | Security default to prevent memory exhaustion |
| **Hard Maximum** | 2 GB | 2 GB | Limited by 32-bit signed integer (INT_MAX) used internally |
| **Recommended Maximum** | 1 MB | - | Google's official recommendation for usability |

**Important Notes:**
- The 2GB limit exists because all implementations use 32-bit signed arithmetic
- The default 64MB limit exists for security reasons
- Large messages require allocating substantial memory for both the serialized buffer and the parsed object

## C++ Implementation

### Understanding CodedInputStream

In C++, the `CodedInputStream` class provides control over message size limits through the `SetTotalBytesLimit()` method.

#### Basic Usage Example

```cpp
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <fstream>
#include "my_message.pb.h"

bool ParseMessageWithLimit(const std::string& filename, 
                          MyMessage& message,
                          int max_bytes = 64 * 1024 * 1024) {
    // Open file for reading
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        std::cerr << "Failed to open file" << std::endl;
        return false;
    }
    
    // Create input stream
    google::protobuf::io::IstreamInputStream raw_input(&input);
    google::protobuf::io::CodedInputStream coded_input(&raw_input);
    
    // Set the size limit (default is INT_MAX ~2GB)
    coded_input.SetTotalBytesLimit(max_bytes);
    
    // Parse the message
    if (!message.ParseFromCodedStream(&coded_input)) {
        std::cerr << "Failed to parse message" << std::endl;
        return false;
    }
    
    return true;
}
```

### Advanced: Streaming Multiple Messages

When reading multiple messages from a stream, the limit applies to the cumulative total bytes read. You need to create a new `CodedInputStream` for each message or adjust the limit accordingly.

```cpp
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <vector>

// Read length-delimited messages
bool ReadDelimitedMessages(std::istream& input, 
                          std::vector<MyMessage>& messages,
                          int per_message_limit = 10 * 1024 * 1024) {
    google::protobuf::io::IstreamInputStream raw_input(&input);
    
    while (true) {
        // Create new CodedInputStream for each message
        google::protobuf::io::CodedInputStream coded_input(&raw_input);
        coded_input.SetTotalBytesLimit(per_message_limit);
        
        // Read the message size (varint encoded)
        uint32_t size;
        if (!coded_input.ReadVarint32(&size)) {
            // End of stream
            break;
        }
        
        // Enforce additional size check
        if (size > per_message_limit) {
            std::cerr << "Message size " << size 
                     << " exceeds limit " << per_message_limit << std::endl;
            return false;
        }
        
        // Set a limit for just this message
        auto limit = coded_input.PushLimit(size);
        
        // Parse the message
        MyMessage message;
        if (!message.ParseFromCodedStream(&coded_input)) {
            std::cerr << "Failed to parse message" << std::endl;
            return false;
        }
        
        coded_input.PopLimit(limit);
        messages.push_back(std::move(message));
    }
    
    return true;
}

// Write length-delimited messages
bool WriteDelimitedMessage(std::ostream& output, const MyMessage& message) {
    google::protobuf::io::OstreamOutputStream raw_output(&output);
    google::protobuf::io::CodedOutputStream coded_output(&raw_output);
    
    // Write the size as a varint
    coded_output.WriteVarint32(message.ByteSizeLong());
    
    // Write the message
    if (!message.SerializeToCodedStream(&coded_output)) {
        return false;
    }
    
    return true;
}
```

### Production-Ready Parser with Validation

```cpp
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <memory>
#include <chrono>

class SecureMessageParser {
private:
    static constexpr int DEFAULT_MAX_SIZE = 10 * 1024 * 1024;  // 10 MB
    static constexpr int MAX_RECURSION_DEPTH = 100;
    
public:
    struct ParseResult {
        bool success;
        std::string error_message;
        size_t bytes_consumed;
        std::chrono::milliseconds parse_time;
    };
    
    template<typename MessageType>
    static ParseResult ParseFromArray(const void* data, 
                                     size_t size,
                                     MessageType& message,
                                     int max_size = DEFAULT_MAX_SIZE) {
        auto start_time = std::chrono::steady_clock::now();
        ParseResult result;
        result.bytes_consumed = size;
        
        // Pre-validation: check size
        if (size > max_size) {
            result.success = false;
            result.error_message = "Message size " + std::to_string(size) + 
                                  " exceeds maximum allowed " + std::to_string(max_size);
            return result;
        }
        
        // Create input stream
        google::protobuf::io::ArrayInputStream array_input(data, size);
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        
        // Configure security limits
        coded_input.SetTotalBytesLimit(max_size);
        coded_input.SetRecursionLimit(MAX_RECURSION_DEPTH);
        
        // Parse
        result.success = message.ParseFromCodedStream(&coded_input);
        
        if (!result.success) {
            result.error_message = "Failed to parse protobuf message";
        }
        
        // Verify all bytes were consumed (detects truncation)
        if (result.success && !coded_input.ConsumedEntireMessage()) {
            result.success = false;
            result.error_message = "Message was not fully consumed";
        }
        
        auto end_time = std::chrono::steady_clock::now();
        result.parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        return result;
    }
};

// Usage example
int main() {
    MyMessage message;
    std::vector<uint8_t> data = /* ... received data ... */;
    
    auto result = SecureMessageParser::ParseFromArray(
        data.data(), 
        data.size(), 
        message,
        5 * 1024 * 1024  // 5 MB limit
    );
    
    if (result.success) {
        std::cout << "Successfully parsed message in " 
                 << result.parse_time.count() << "ms" << std::endl;
        // Process message...
    } else {
        std::cerr << "Parse error: " << result.error_message << std::endl;
        // Log security event, reject message, etc.
    }
    
    return 0;
}
```

### Legacy API Note (Protobuf < 3.11)

```cpp
// OLD API (deprecated in Protobuf 3.6+, removed in 3.11+)
// coded_input.SetTotalBytesLimit(max_bytes, warning_threshold);

// NEW API (Protobuf 3.6+)
coded_input.SetTotalBytesLimit(max_bytes);
```

## Rust Implementation

Rust has two main Protobuf libraries: `prost` and `rust-protobuf`. Both handle size limits differently than C++.

### Using prost

The `prost` library uses a simpler approach focused on buffer management rather than explicit size limits.

#### Basic Message Parsing with Size Validation

```rust
use prost::Message;
use bytes::Buf;
use std::io::Cursor;

// Define your protobuf message (generated by prost-build)
#[derive(Clone, PartialEq, Message)]
pub struct MyMessage {
    #[prost(string, tag = "1")]
    pub name: String,
    
    #[prost(int32, tag = "2")]
    pub id: i32,
    
    #[prost(bytes, tag = "3")]
    pub data: Vec<u8>,
}

// Parse with size limit enforcement
pub fn parse_with_limit(data: &[u8], max_size: usize) -> Result<MyMessage, String> {
    // Validate size before parsing
    if data.len() > max_size {
        return Err(format!(
            "Message size {} exceeds maximum allowed {}",
            data.len(),
            max_size
        ));
    }
    
    // Parse the message
    MyMessage::decode(data)
        .map_err(|e| format!("Failed to decode message: {}", e))
}

// Parse length-delimited message
pub fn parse_length_delimited_with_limit(
    data: &[u8],
    max_size: usize
) -> Result<MyMessage, String> {
    if data.len() > max_size {
        return Err(format!(
            "Message size {} exceeds maximum allowed {}",
            data.len(),
            max_size
        ));
    }
    
    MyMessage::decode_length_delimited(data)
        .map_err(|e| format!("Failed to decode length-delimited message: {}", e))
}
```

#### Streaming Multiple Messages

```rust
use prost::Message;
use bytes::{Buf, BytesMut};
use std::io::{self, Read};

pub struct MessageStream<R: Read> {
    reader: R,
    max_message_size: usize,
}

impl<R: Read> MessageStream<R> {
    pub fn new(reader: R, max_message_size: usize) -> Self {
        Self {
            reader,
            max_message_size,
        }
    }
    
    pub fn read_next(&mut self) -> io::Result<Option<MyMessage>> {
        // Read the length prefix (varint encoded)
        let length = match self.read_varint()? {
            Some(len) => len,
            None => return Ok(None), // End of stream
        };
        
        // Validate size
        if length > self.max_message_size {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Message size {} exceeds limit {}", length, self.max_message_size)
            ));
        }
        
        // Read the message data
        let mut buffer = vec![0u8; length];
        self.reader.read_exact(&mut buffer)?;
        
        // Decode the message
        MyMessage::decode(&buffer[..])
            .map(Some)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
    }
    
    fn read_varint(&mut self) -> io::Result<Option<usize>> {
        let mut result: usize = 0;
        let mut shift = 0;
        
        loop {
            let mut byte = [0u8; 1];
            match self.reader.read_exact(&mut byte) {
                Ok(_) => {},
                Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => return Ok(None),
                Err(e) => return Err(e),
            }
            
            let b = byte[0];
            result |= ((b & 0x7F) as usize) << shift;
            
            if b & 0x80 == 0 {
                return Ok(Some(result));
            }
            
            shift += 7;
            if shift >= 64 {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "Varint too long"
                ));
            }
        }
    }
}

// Write length-delimited message
pub fn write_length_delimited<W: io::Write>(
    writer: &mut W,
    message: &MyMessage
) -> io::Result<()> {
    let encoded = message.encode_length_delimited_to_vec();
    writer.write_all(&encoded)
}
```

#### Production-Ready Parser with Validation

```rust
use prost::Message;
use std::time::Instant;

pub struct SecureMessageParser {
    max_size: usize,
    max_depth: usize,
}

#[derive(Debug)]
pub struct ParseResult {
    pub success: bool,
    pub error_message: Option<String>,
    pub bytes_consumed: usize,
    pub parse_duration_ms: u128,
}

impl SecureMessageParser {
    const DEFAULT_MAX_SIZE: usize = 10 * 1024 * 1024; // 10 MB
    const DEFAULT_MAX_DEPTH: usize = 100;
    
    pub fn new(max_size: usize) -> Self {
        Self {
            max_size,
            max_depth: Self::DEFAULT_MAX_DEPTH,
        }
    }
    
    pub fn default() -> Self {
        Self::new(Self::DEFAULT_MAX_SIZE)
    }
    
    pub fn parse<M: Message + Default>(&self, data: &[u8]) -> ParseResult {
        let start = Instant::now();
        let bytes_consumed = data.len();
        
        // Pre-validation: check size
        if data.len() > self.max_size {
            return ParseResult {
                success: false,
                error_message: Some(format!(
                    "Message size {} exceeds maximum allowed {}",
                    data.len(),
                    self.max_size
                )),
                bytes_consumed,
                parse_duration_ms: start.elapsed().as_millis(),
            };
        }
        
        // Attempt to parse
        let result = match M::decode(data) {
            Ok(_message) => ParseResult {
                success: true,
                error_message: None,
                bytes_consumed,
                parse_duration_ms: start.elapsed().as_millis(),
            },
            Err(e) => ParseResult {
                success: false,
                error_message: Some(format!("Parse error: {}", e)),
                bytes_consumed,
                parse_duration_ms: start.elapsed().as_millis(),
            },
        };
        
        result
    }
}

// Usage example
fn main() {
    let parser = SecureMessageParser::new(5 * 1024 * 1024); // 5 MB limit
    let data: Vec<u8> = vec![/* ... received data ... */];
    
    let result = parser.parse::<MyMessage>(&data);
    
    if result.success {
        println!("Successfully parsed message in {}ms", result.parse_duration_ms);
        // Process message...
    } else {
        eprintln!("Parse error: {:?}", result.error_message);
        // Log security event, reject message, etc.
    }
}
```

### Using rust-protobuf

The `rust-protobuf` library (alternative to prost) provides more direct control similar to the C++ API.

```rust
use protobuf::Message;
use protobuf::CodedInputStream;
use std::io::Read;

pub fn parse_with_limit<M: Message>(
    reader: &mut dyn Read,
    max_size: u64
) -> protobuf::Result<M> {
    let mut coded_input = CodedInputStream::new(reader);
    
    // Note: rust-protobuf doesn't have a direct SetTotalBytesLimit equivalent
    // Size validation must be done at the buffer level
    
    // Parse the message
    M::parse_from(&mut coded_input)
}

pub fn parse_from_bytes_with_limit<M: Message>(
    bytes: &[u8],
    max_size: usize
) -> Result<M, String> {
    if bytes.len() > max_size {
        return Err(format!(
            "Message size {} exceeds limit {}",
            bytes.len(),
            max_size
        ));
    }
    
    M::parse_from_bytes(bytes)
        .map_err(|e| format!("Parse error: {}", e))
}
```

## Best Practices for DoS Prevention

### 1. Always Set Appropriate Limits

```cpp
// C++: Don't rely on defaults
coded_input.SetTotalBytesLimit(10 * 1024 * 1024);  // 10 MB

// Rust: Validate before parsing
if data.len() > MAX_MESSAGE_SIZE {
    return Err("Message too large");
}
```

### 2. Use Per-Message Limits for Streams

When reading multiple messages, reset limits for each message to prevent cumulative limit issues.

### 3. Monitor and Log Limit Violations

```cpp
if (!message.ParseFromCodedStream(&coded_input)) {
    // Log this as a potential security event
    LOG(WARNING) << "Rejected oversized message from " << client_ip 
                 << " size: " << message_size;
    // Potentially rate-limit or block the client
}
```

```rust
if data.len() > max_size {
    log::warn!(
        "Rejected oversized message from {}: {} bytes (limit: {})",
        client_addr, data.len(), max_size
    );
    // Take defensive action
}
```

### 4. Set Recursion Limits

Deep nesting can cause stack overflow or exponential parsing time.

```cpp
coded_input.SetRecursionLimit(100);  // Default is 100
```

### 5. Timeout Long-Running Parses

```rust
use std::time::{Duration, Instant};

pub fn parse_with_timeout<M: Message + Default>(
    data: &[u8],
    max_size: usize,
    timeout: Duration
) -> Result<M, String> {
    if data.len() > max_size {
        return Err("Message too large".to_string());
    }
    
    let start = Instant::now();
    let result = M::decode(data)
        .map_err(|e| format!("Parse error: {}", e))?;
    
    if start.elapsed() > timeout {
        return Err("Parse timeout exceeded".to_string());
    }
    
    Ok(result)
}
```

### 6. Rate Limit Large Messages

Implement application-level rate limiting for messages approaching size limits.

### 7. Break Up Large Data Sets

Instead of one massive message, use repeated smaller messages:

```protobuf
// Bad: One huge message
message DataDump {
    repeated Record records = 1;  // Could be millions
}

// Good: Stream individual messages
message Record {
    // Individual record data
}
// Send many Record messages sequentially
```

## Common Pitfalls

### 1. Cumulative Limit in Streams

❌ **Wrong**: Reusing `CodedInputStream` for multiple messages hits cumulative limit.

```cpp
// This will fail after 64 MB total across all messages
google::protobuf::io::CodedInputStream coded_input(&raw_input);
while (read_next_message(coded_input, message)) {
    process(message);
}
```

✅ **Correct**: Create new `CodedInputStream` per message or use `PushLimit/PopLimit`.

### 2. Forgetting to Validate Before Parsing

❌ **Wrong**: Parse first, validate later.

```rust
let message = MyMessage::decode(untrusted_data)?;  // Could exhaust memory
if message.encoded_len() > MAX_SIZE {
    // Too late!
}
```

✅ **Correct**: Validate size before parsing.

```rust
if untrusted_data.len() > MAX_SIZE {
    return Err("Too large");
}
let message = MyMessage::decode(untrusted_data)?;
```

### 3. Integer Overflow

Be aware of the 2 GB hard limit due to signed 32-bit integers:

```cpp
// This can overflow on 32-bit systems or with messages near 2GB
int size = message.ByteSize();  // Returns int32

// Better: Use ByteSizeLong() for large messages
size_t size = message.ByteSizeLong();  // Returns size_t
```

## Summary

### Key Takeaways

1. **Default Limits**: Protobuf has a 64 MB default parse limit and a 2 GB hard maximum due to 32-bit signed integers.

2. **Recommendations**: Google recommends keeping messages under 1 MB for optimal performance and usability.

3. **DoS Prevention**: Always validate message sizes before parsing to prevent memory exhaustion attacks.

4. **Streaming**: When reading multiple messages, manage limits per-message to avoid cumulative issues.

5. **Layered Defense**: Combine size limits, recursion limits, timeouts, and rate limiting for comprehensive protection.

6. **Language Differences**:
   - **C++**: Use `CodedInputStream::SetTotalBytesLimit()`
   - **Rust (prost)**: Validate buffer size before calling `Message::decode()`
   - **Rust (rust-protobuf)**: Similar validation required at buffer level

7. **Production Considerations**: Monitor limit violations, log security events, and adjust limits based on legitimate usage patterns.

### Security Checklist

- ✅ Set explicit size limits (don't rely on defaults)
- ✅ Validate message size before parsing
- ✅ Set recursion depth limits
- ✅ Implement parsing timeouts
- ✅ Log and monitor limit violations
- ✅ Rate-limit clients sending large messages
- ✅ Use length-delimited encoding for streams
- ✅ Design schemas to avoid huge single messages
- ✅ Test with maliciously crafted large messages
- ✅ Keep Protobuf libraries updated for security patches

By implementing these protections, you can safely use Protocol Buffers in production environments while mitigating denial-of-service risks from malicious or malformed messages.