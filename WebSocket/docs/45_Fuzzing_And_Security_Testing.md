# Fuzzing and Security Testing for WebSocket Implementations

## Overview

Fuzzing and security testing are critical practices for discovering vulnerabilities in WebSocket implementations. These techniques involve automated testing with malformed, unexpected, or random data to find bugs, crashes, memory leaks, and security flaws that traditional testing might miss.

## Why Fuzzing Matters for WebSockets

WebSocket implementations are particularly vulnerable because they:
- Handle untrusted data from network connections
- Parse complex protocols (HTTP upgrade, framing, masking)
- Maintain stateful connections with potential race conditions
- Process binary and text data with different encodings
- Deal with compression (permessage-deflate extension)

## Key Vulnerability Categories

### 1. **Protocol Parsing Vulnerabilities**
- Malformed frame headers
- Invalid UTF-8 in text frames
- Incorrect masking key handling
- Frame fragmentation edge cases

### 2. **Memory Safety Issues**
- Buffer overflows/underflows
- Use-after-free
- Memory leaks
- Integer overflows in length calculations

### 3. **Logic Vulnerabilities**
- State machine confusion
- Race conditions in connection handling
- Improper error handling
- Resource exhaustion (DoS)

---

## C/C++ Implementation with AFL (American Fuzzy Lop)

### Basic WebSocket Frame Parser (Fuzzing Target)

```c
// websocket_parser.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PAYLOAD_SIZE (1024 * 1024) // 1MB limit

typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

typedef struct {
    bool fin;
    bool rsv1, rsv2, rsv3;
    ws_opcode_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t masking_key[4];
    uint8_t *payload;
} ws_frame_t;

// Vulnerable function for fuzzing
int parse_websocket_frame(const uint8_t *data, size_t len, ws_frame_t *frame) {
    if (len < 2) return -1;
    
    size_t offset = 0;
    
    // Byte 0: FIN, RSV, Opcode
    frame->fin = (data[offset] & 0x80) != 0;
    frame->rsv1 = (data[offset] & 0x40) != 0;
    frame->rsv2 = (data[offset] & 0x20) != 0;
    frame->rsv3 = (data[offset] & 0x10) != 0;
    frame->opcode = (ws_opcode_t)(data[offset] & 0x0F);
    offset++;
    
    // Byte 1: MASK, Payload length
    frame->masked = (data[offset] & 0x80) != 0;
    uint8_t payload_len = data[offset] & 0x7F;
    offset++;
    
    // Extended payload length
    if (payload_len == 126) {
        if (len < offset + 2) return -1;
        frame->payload_length = (data[offset] << 8) | data[offset + 1];
        offset += 2;
    } else if (payload_len == 127) {
        if (len < offset + 8) return -1;
        frame->payload_length = 0;
        for (int i = 0; i < 8; i++) {
            frame->payload_length = (frame->payload_length << 8) | data[offset + i];
        }
        offset += 8;
    } else {
        frame->payload_length = payload_len;
    }
    
    // Security check: prevent excessive allocations
    if (frame->payload_length > MAX_PAYLOAD_SIZE) {
        return -1;
    }
    
    // Masking key
    if (frame->masked) {
        if (len < offset + 4) return -1;
        memcpy(frame->masking_key, data + offset, 4);
        offset += 4;
    }
    
    // Payload
    if (len < offset + frame->payload_length) return -1;
    
    frame->payload = malloc(frame->payload_length);
    if (!frame->payload) return -1;
    
    memcpy(frame->payload, data + offset, frame->payload_length);
    
    // Unmask if needed
    if (frame->masked) {
        for (uint64_t i = 0; i < frame->payload_length; i++) {
            frame->payload[i] ^= frame->masking_key[i % 4];
        }
    }
    
    // UTF-8 validation for text frames
    if (frame->opcode == WS_OPCODE_TEXT) {
        if (!validate_utf8(frame->payload, frame->payload_length)) {
            free(frame->payload);
            return -1;
        }
    }
    
    return 0;
}

bool validate_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        if ((data[i] & 0x80) == 0) {
            i++; // ASCII
        } else if ((data[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((data[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len) return false;
            if ((data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((data[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len) return false;
            if ((data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || 
                (data[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

// AFL fuzzing harness
int main(int argc, char **argv) {
    uint8_t buffer[65536];
    size_t len;
    
    // Read from stdin for AFL
    len = fread(buffer, 1, sizeof(buffer), stdin);
    
    ws_frame_t frame = {0};
    parse_websocket_frame(buffer, len, &frame);
    
    if (frame.payload) {
        free(frame.payload);
    }
    
    return 0;
}
```

### Compiling and Running with AFL

```bash
# Install AFL++
sudo apt-get install afl++

# Compile with AFL instrumentation
afl-gcc -o websocket_fuzz websocket_parser.c -fsanitize=address

# Create test case directory
mkdir testcases
echo -ne '\x81\x05Hello' > testcases/basic_text.bin

# Run fuzzer
afl-fuzz -i testcases -o findings ./websocket_fuzz
```

---

## C++ Implementation with libFuzzer

```cpp
// websocket_fuzzer.cpp
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

extern "C" {

class WebSocketParser {
public:
    enum class Opcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };
    
    struct Frame {
        bool fin;
        Opcode opcode;
        bool masked;
        std::vector<uint8_t> payload;
    };
    
    bool parse(const uint8_t* data, size_t size) {
        if (size < 2) return false;
        
        size_t offset = 0;
        Frame frame;
        
        // Parse header
        frame.fin = (data[offset] & 0x80) != 0;
        frame.opcode = static_cast<Opcode>(data[offset] & 0x0F);
        offset++;
        
        frame.masked = (data[offset] & 0x80) != 0;
        uint64_t payload_length = data[offset] & 0x7F;
        offset++;
        
        // Extended length
        if (payload_length == 126) {
            if (size < offset + 2) return false;
            payload_length = (static_cast<uint64_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;
        } else if (payload_length == 127) {
            if (size < offset + 8) return false;
            payload_length = 0;
            for (int i = 0; i < 8; i++) {
                payload_length = (payload_length << 8) | data[offset + i];
            }
            offset += 8;
        }
        
        // Prevent DoS
        if (payload_length > 10 * 1024 * 1024) return false;
        
        // Masking key
        uint8_t masking_key[4] = {0};
        if (frame.masked) {
            if (size < offset + 4) return false;
            std::memcpy(masking_key, data + offset, 4);
            offset += 4;
        }
        
        // Payload
        if (size < offset + payload_length) return false;
        
        frame.payload.resize(payload_length);
        std::memcpy(frame.payload.data(), data + offset, payload_length);
        
        // Unmask
        if (frame.masked) {
            for (size_t i = 0; i < payload_length; i++) {
                frame.payload[i] ^= masking_key[i % 4];
            }
        }
        
        return true;
    }
};

// libFuzzer entry point
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    WebSocketParser parser;
    parser.parse(data, size);
    return 0;
}

} // extern "C"
```

### Compiling and Running with libFuzzer

```bash
# Compile with libFuzzer and sanitizers
clang++ -g -O1 -fsanitize=fuzzer,address,undefined \
    websocket_fuzzer.cpp -o websocket_fuzzer

# Run fuzzer
./websocket_fuzzer -max_len=65536 -runs=1000000
```

---

## Rust Implementation with cargo-fuzz

```rust
// src/lib.rs
use std::io::Cursor;

#[derive(Debug, Clone, Copy)]
pub enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

#[derive(Debug)]
pub struct Frame {
    pub fin: bool,
    pub opcode: Opcode,
    pub masked: bool,
    pub payload: Vec<u8>,
}

pub fn parse_frame(data: &[u8]) -> Result<Frame, &'static str> {
    if data.len() < 2 {
        return Err("Insufficient data");
    }
    
    let mut cursor = Cursor::new(data);
    let mut offset = 0;
    
    // Byte 0: FIN, RSV, Opcode
    let byte0 = data[offset];
    let fin = (byte0 & 0x80) != 0;
    let opcode = match byte0 & 0x0F {
        0x0 => Opcode::Continuation,
        0x1 => Opcode::Text,
        0x2 => Opcode::Binary,
        0x8 => Opcode::Close,
        0x9 => Opcode::Ping,
        0xA => Opcode::Pong,
        _ => return Err("Invalid opcode"),
    };
    offset += 1;
    
    // Byte 1: MASK, Payload length
    let byte1 = data[offset];
    let masked = (byte1 & 0x80) != 0;
    let mut payload_length = (byte1 & 0x7F) as u64;
    offset += 1;
    
    // Extended payload length
    if payload_length == 126 {
        if data.len() < offset + 2 {
            return Err("Insufficient data for extended length");
        }
        payload_length = u16::from_be_bytes([data[offset], data[offset + 1]]) as u64;
        offset += 2;
    } else if payload_length == 127 {
        if data.len() < offset + 8 {
            return Err("Insufficient data for extended length");
        }
        let mut bytes = [0u8; 8];
        bytes.copy_from_slice(&data[offset..offset + 8]);
        payload_length = u64::from_be_bytes(bytes);
        offset += 8;
    }
    
    // Prevent DoS
    if payload_length > 10 * 1024 * 1024 {
        return Err("Payload too large");
    }
    
    // Masking key
    let masking_key = if masked {
        if data.len() < offset + 4 {
            return Err("Insufficient data for masking key");
        }
        let key = [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]];
        offset += 4;
        Some(key)
    } else {
        None
    };
    
    // Payload
    let payload_length = payload_length as usize;
    if data.len() < offset + payload_length {
        return Err("Insufficient data for payload");
    }
    
    let mut payload = data[offset..offset + payload_length].to_vec();
    
    // Unmask
    if let Some(key) = masking_key {
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= key[i % 4];
        }
    }
    
    // UTF-8 validation for text frames
    if matches!(opcode, Opcode::Text) {
        std::str::from_utf8(&payload).map_err(|_| "Invalid UTF-8")?;
    }
    
    Ok(Frame {
        fin,
        opcode,
        masked,
        payload,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_basic_text_frame() {
        let data = vec![0x81, 0x05, b'H', b'e', b'l', b'l', b'o'];
        let frame = parse_frame(&data).unwrap();
        assert!(frame.fin);
        assert_eq!(frame.payload, b"Hello");
    }
}
```

### Fuzz Target

```rust
// fuzz/fuzz_targets/parse_frame.rs
#![no_main]

use libfuzzer_sys::fuzz_target;
use websocket_parser::parse_frame;

fuzz_target!(|data: &[u8]| {
    // Attempt to parse the frame
    let _ = parse_frame(data);
});
```

### Cargo.toml Configuration

```toml
[package]
name = "websocket-parser"
version = "0.1.0"
edition = "2021"

[dependencies]

[dev-dependencies]

[profile.release]
opt-level = 3
lto = true

[[bin]]
name = "websocket-parser"
path = "src/main.rs"
```

### Running cargo-fuzz

```bash
# Install cargo-fuzz
cargo install cargo-fuzz

# Initialize fuzzing
cargo fuzz init

# Create fuzz target (already shown above)

# Run fuzzer
cargo fuzz run parse_frame

# Run with sanitizers
cargo fuzz run parse_frame -- -runs=1000000

# Minimize crash cases
cargo fuzz cmin parse_frame

# Test a specific crash
cargo fuzz run parse_frame fuzz/artifacts/parse_frame/crash-xyz
```

---

## Advanced Fuzzing Techniques

### 1. **Structure-Aware Fuzzing**

```rust
// Custom fuzzer with protocol structure awareness
use arbitrary::Arbitrary;

#[derive(Arbitrary, Debug)]
struct FuzzFrame {
    fin: bool,
    opcode: u8,
    masked: bool,
    payload_length: u64,
    payload: Vec<u8>,
}

impl FuzzFrame {
    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        // Construct valid frame structure
        let byte0 = (self.fin as u8) << 7 | (self.opcode & 0x0F);
        bytes.push(byte0);
        
        let len = self.payload.len().min(self.payload_length as usize);
        
        if len < 126 {
            bytes.push((self.masked as u8) << 7 | len as u8);
        } else if len < 65536 {
            bytes.push((self.masked as u8) << 7 | 126);
            bytes.extend_from_slice(&(len as u16).to_be_bytes());
        } else {
            bytes.push((self.masked as u8) << 7 | 127);
            bytes.extend_from_slice(&(len as u64).to_be_bytes());
        }
        
        if self.masked {
            bytes.extend_from_slice(&[0x12, 0x34, 0x56, 0x78]); // Mock key
        }
        
        bytes.extend_from_slice(&self.payload[..len]);
        bytes
    }
}

fuzz_target!(|frame: FuzzFrame| {
    let bytes = frame.to_bytes();
    let _ = parse_frame(&bytes);
});
```

### 2. **Differential Fuzzing**

Compare two implementations:

```cpp
// Compare your implementation against a reference
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    auto result1 = your_websocket_parse(data, size);
    auto result2 = reference_websocket_parse(data, size);
    
    // They should behave identically
    assert(result1.success == result2.success);
    if (result1.success) {
        assert(result1.payload == result2.payload);
    }
    
    return 0;
}
```

---

## Security Testing Checklist

### Protocol Compliance
- [ ] Handle all frame types correctly
- [ ] Validate reserved bits
- [ ] Enforce masking requirements
- [ ] Check frame fragmentation rules
- [ ] Validate close frame status codes

### Memory Safety
- [ ] No buffer overflows
- [ ] No use-after-free
- [ ] No memory leaks
- [ ] Integer overflow protection
- [ ] Stack overflow protection

### Input Validation
- [ ] UTF-8 validation for text frames
- [ ] Maximum frame size limits
- [ ] Maximum message size limits
- [ ] Compression bomb protection
- [ ] Malformed frame rejection

### DoS Protection
- [ ] Connection rate limiting
- [ ] Message rate limiting
- [ ] Memory usage limits
- [ ] CPU usage limits
- [ ] Proper timeout handling

---

## Summary

**Fuzzing and security testing** are essential for building robust WebSocket implementations. Key takeaways:

1. **Multiple Fuzzing Tools**: AFL for C, libFuzzer for C++, and cargo-fuzz for Rust each offer different strengths
2. **Memory Safety**: Use sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer) to catch memory errors
3. **Protocol Awareness**: Structure-aware fuzzing finds deeper bugs than purely random fuzzing
4. **Continuous Testing**: Integrate fuzzing into CI/CD pipelines
5. **Defense in Depth**: Combine fuzzing with static analysis, code review, and penetration testing
6. **Rust Advantages**: Rust's memory safety prevents entire classes of vulnerabilities found in C/C++

Fuzzing typically finds: parsing errors (40%), memory corruption (30%), logic bugs (20%), and DoS vulnerabilities (10%). Running fuzzers for 24+ hours often uncovers critical issues missed by traditional testing.