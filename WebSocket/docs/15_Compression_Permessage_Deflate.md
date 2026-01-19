# WebSocket Compression (permessage-deflate)

## Overview

The **permessage-deflate** extension is a WebSocket compression mechanism defined in RFC 7692 that reduces bandwidth usage by compressing message payloads using the DEFLATE algorithm (the same compression used in gzip). This extension is negotiated during the WebSocket handshake and, when enabled, compresses each WebSocket message independently before transmission.

## How permessage-deflate Works

### Compression Flow

1. **Handshake Negotiation**: Client and server negotiate compression parameters during the HTTP upgrade handshake
2. **Message Compression**: Before sending, messages are compressed using DEFLATE
3. **Frame Marking**: The RSV1 bit in the WebSocket frame header is set to indicate compression
4. **Decompression**: Receiving end decompresses the payload before processing
5. **Context Management**: Compression contexts can be maintained across messages or reset per message

### Key Parameters

- **server_no_context_takeover**: Server resets compression context after each message
- **client_no_context_takeover**: Client resets compression context after each message
- **server_max_window_bits**: Maximum LZ77 sliding window size for server (8-15)
- **client_max_window_bits**: Maximum LZ77 sliding window size for client (8-15)

### Benefits

- **Bandwidth Reduction**: 40-70% reduction in data transfer for text-heavy applications
- **Cost Savings**: Lower network costs for high-traffic applications
- **Performance**: Faster transmission of large messages over limited bandwidth
- **Transparent**: Application layer doesn't need to handle compression manually

## Code Examples

### C/C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <zlib.h>
#include <stdexcept>

// WebSocket frame header bits
#define WS_FIN 0x80
#define WS_RSV1 0x40  // Compression bit
#define WS_OPCODE_TEXT 0x01

class PermessageDeflate {
private:
    z_stream deflate_stream;
    z_stream inflate_stream;
    bool server_no_context_takeover;
    bool client_no_context_takeover;
    int window_bits;
    
public:
    PermessageDeflate(int window_bits = 15, 
                      bool no_context_takeover = false) 
        : window_bits(window_bits),
          server_no_context_takeover(no_context_takeover),
          client_no_context_takeover(no_context_takeover) {
        
        // Initialize deflate stream (compression)
        deflate_stream.zalloc = Z_NULL;
        deflate_stream.zfree = Z_NULL;
        deflate_stream.opaque = Z_NULL;
        
        if (deflateInit2(&deflate_stream, Z_DEFAULT_COMPRESSION,
                        Z_DEFLATED, -window_bits, 8, 
                        Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("Failed to initialize deflate");
        }
        
        // Initialize inflate stream (decompression)
        inflate_stream.zalloc = Z_NULL;
        inflate_stream.zfree = Z_NULL;
        inflate_stream.opaque = Z_NULL;
        
        if (inflateInit2(&inflate_stream, -window_bits) != Z_OK) {
            deflateEnd(&deflate_stream);
            throw std::runtime_error("Failed to initialize inflate");
        }
    }
    
    ~PermessageDeflate() {
        deflateEnd(&deflate_stream);
        inflateEnd(&inflate_stream);
    }
    
    // Compress a message payload
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        if (client_no_context_takeover) {
            deflateReset(&deflate_stream);
        }
        
        std::vector<uint8_t> compressed;
        compressed.resize(deflateBound(&deflate_stream, data.size()));
        
        deflate_stream.next_in = const_cast<uint8_t*>(data.data());
        deflate_stream.avail_in = data.size();
        deflate_stream.next_out = compressed.data();
        deflate_stream.avail_out = compressed.size();
        
        int ret = deflate(&deflate_stream, Z_SYNC_FLUSH);
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            throw std::runtime_error("Compression failed");
        }
        
        size_t compressed_size = compressed.size() - deflate_stream.avail_out;
        
        // Remove trailing 0x00 0x00 0xFF 0xFF from SYNC_FLUSH
        if (compressed_size >= 4 &&
            compressed[compressed_size - 4] == 0x00 &&
            compressed[compressed_size - 3] == 0x00 &&
            compressed[compressed_size - 2] == 0xFF &&
            compressed[compressed_size - 1] == 0xFF) {
            compressed_size -= 4;
        }
        
        compressed.resize(compressed_size);
        return compressed;
    }
    
    // Decompress a message payload
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
        if (server_no_context_takeover) {
            inflateReset(&inflate_stream);
        }
        
        // Add back the trailing bytes for decompression
        std::vector<uint8_t> data_with_tail = data;
        data_with_tail.push_back(0x00);
        data_with_tail.push_back(0x00);
        data_with_tail.push_back(0xFF);
        data_with_tail.push_back(0xFF);
        
        std::vector<uint8_t> decompressed;
        decompressed.resize(data.size() * 4); // Initial estimate
        
        inflate_stream.next_in = data_with_tail.data();
        inflate_stream.avail_in = data_with_tail.size();
        inflate_stream.next_out = decompressed.data();
        inflate_stream.avail_out = decompressed.size();
        
        int ret = Z_OK;
        while (ret == Z_OK) {
            ret = inflate(&inflate_stream, Z_SYNC_FLUSH);
            
            if (ret == Z_BUF_ERROR || 
                (ret == Z_OK && inflate_stream.avail_out == 0)) {
                size_t old_size = decompressed.size();
                decompressed.resize(old_size * 2);
                inflate_stream.next_out = decompressed.data() + old_size;
                inflate_stream.avail_out = old_size;
                ret = Z_OK;
            }
        }
        
        if (ret != Z_OK && ret != Z_STREAM_END) {
            throw std::runtime_error("Decompression failed");
        }
        
        decompressed.resize(decompressed.size() - inflate_stream.avail_out);
        return decompressed;
    }
    
    // Create WebSocket frame with compression
    std::vector<uint8_t> createCompressedFrame(const std::string& message) {
        std::vector<uint8_t> payload(message.begin(), message.end());
        std::vector<uint8_t> compressed = compress(payload);
        
        std::vector<uint8_t> frame;
        
        // First byte: FIN, RSV1 (compression), opcode
        frame.push_back(WS_FIN | WS_RSV1 | WS_OPCODE_TEXT);
        
        // Payload length
        size_t len = compressed.size();
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len <= 0xFFFF) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        // Payload data
        frame.insert(frame.end(), compressed.begin(), compressed.end());
        
        return frame;
    }
    
    // Parse handshake extension header
    static std::string buildHandshakeOffer() {
        return "permessage-deflate; client_max_window_bits";
    }
};

// Example usage
int main() {
    try {
        PermessageDeflate compressor(15, false);
        
        // Original message
        std::string message = "Hello, WebSocket! This is a test message that "
                            "will be compressed using permessage-deflate. "
                            "Repeated text: test test test test test";
        
        std::cout << "Original message size: " << message.size() 
                  << " bytes\n";
        std::cout << "Original: " << message << "\n\n";
        
        // Compress
        std::vector<uint8_t> payload(message.begin(), message.end());
        std::vector<uint8_t> compressed = compressor.compress(payload);
        
        std::cout << "Compressed size: " << compressed.size() << " bytes\n";
        std::cout << "Compression ratio: " 
                  << (100.0 * compressed.size() / message.size()) 
                  << "%\n\n";
        
        // Decompress
        std::vector<uint8_t> decompressed = compressor.decompress(compressed);
        std::string result(decompressed.begin(), decompressed.end());
        
        std::cout << "Decompressed: " << result << "\n";
        std::cout << "Match: " << (message == result ? "YES" : "NO") << "\n\n";
        
        // Create full WebSocket frame
        std::vector<uint8_t> frame = compressor.createCompressedFrame(message);
        std::cout << "Full WebSocket frame size: " << frame.size() 
                  << " bytes\n";
        std::cout << "Frame header (first byte): 0x" << std::hex 
                  << static_cast<int>(frame[0]) << std::dec << "\n";
        std::cout << "RSV1 bit set: " 
                  << ((frame[0] & WS_RSV1) ? "YES" : "NO") << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```
### Rust Implementation

```rust
use flate2::Compression;
use flate2::write::{DeflateEncoder, DeflateDecoder};
use std::io::Write;

// WebSocket frame constants
const FIN: u8 = 0x80;
const RSV1: u8 = 0x40; // Compression bit
const OPCODE_TEXT: u8 = 0x01;

/// Represents the permessage-deflate extension configuration
#[derive(Debug, Clone)]
pub struct DeflateConfig {
    pub server_no_context_takeover: bool,
    pub client_no_context_takeover: bool,
    pub server_max_window_bits: u8,
    pub client_max_window_bits: u8,
}

impl Default for DeflateConfig {
    fn default() -> Self {
        Self {
            server_no_context_takeover: false,
            client_no_context_takeover: false,
            server_max_window_bits: 15,
            client_max_window_bits: 15,
        }
    }
}

/// WebSocket compression handler
pub struct PermessageDeflate {
    config: DeflateConfig,
}

impl PermessageDeflate {
    pub fn new(config: DeflateConfig) -> Self {
        Self { config }
    }

    /// Compress a message payload
    pub fn compress(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        let mut encoder = DeflateEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(data)?;
        
        let mut compressed = encoder.finish()?;
        
        // Remove trailing 0x00 0x00 0xFF 0xFF from DEFLATE stream
        // This is required by the permessage-deflate specification
        if compressed.len() >= 4 {
            let tail = &compressed[compressed.len() - 4..];
            if tail == [0x00, 0x00, 0xFF, 0xFF] {
                compressed.truncate(compressed.len() - 4);
            }
        }
        
        Ok(compressed)
    }

    /// Decompress a message payload
    pub fn decompress(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        // Add back the trailing bytes for decompression
        let mut data_with_tail = data.to_vec();
        data_with_tail.extend_from_slice(&[0x00, 0x00, 0xFF, 0xFF]);
        
        let mut decoder = DeflateDecoder::new(Vec::new());
        decoder.write_all(&data_with_tail)?;
        decoder.finish()
    }

    /// Create a WebSocket frame with compressed payload
    pub fn create_compressed_frame(&self, message: &str) -> Result<Vec<u8>, std::io::Error> {
        let payload = message.as_bytes();
        let compressed = self.compress(payload)?;
        
        let mut frame = Vec::new();
        
        // First byte: FIN | RSV1 | OPCODE
        frame.push(FIN | RSV1 | OPCODE_TEXT);
        
        // Payload length encoding
        let len = compressed.len();
        if len < 126 {
            frame.push(len as u8);
        } else if len <= 0xFFFF {
            frame.push(126);
            frame.push((len >> 8) as u8);
            frame.push((len & 0xFF) as u8);
        } else {
            frame.push(127);
            for i in (0..8).rev() {
                frame.push((len >> (i * 8)) as u8);
            }
        }
        
        // Compressed payload
        frame.extend_from_slice(&compressed);
        
        Ok(frame)
    }

    /// Parse a compressed WebSocket frame
    pub fn parse_compressed_frame(&self, frame: &[u8]) -> Result<String, Box<dyn std::error::Error>> {
        if frame.is_empty() {
            return Err("Empty frame".into());
        }
        
        let first_byte = frame[0];
        let is_compressed = (first_byte & RSV1) != 0;
        
        if !is_compressed {
            return Err("Frame is not compressed".into());
        }
        
        // Extract payload length
        let (payload_start, _payload_len) = if frame.len() < 2 {
            return Err("Invalid frame".into());
        } else {
            let len_byte = frame[1] & 0x7F;
            if len_byte < 126 {
                (2, len_byte as usize)
            } else if len_byte == 126 && frame.len() >= 4 {
                let len = ((frame[2] as usize) << 8) | (frame[3] as usize);
                (4, len)
            } else if len_byte == 127 && frame.len() >= 10 {
                let mut len = 0usize;
                for i in 0..8 {
                    len = (len << 8) | (frame[2 + i] as usize);
                }
                (10, len)
            } else {
                return Err("Invalid length encoding".into());
            }
        };
        
        // Extract and decompress payload
        let compressed_payload = &frame[payload_start..];
        let decompressed = self.decompress(compressed_payload)?;
        
        Ok(String::from_utf8(decompressed)?)
    }

    /// Generate handshake offer header value
    pub fn build_handshake_offer(&self) -> String {
        let mut params = vec!["permessage-deflate".to_string()];
        
        if self.config.server_no_context_takeover {
            params.push("server_no_context_takeover".to_string());
        }
        
        if self.config.client_no_context_takeover {
            params.push("client_no_context_takeover".to_string());
        }
        
        if self.config.client_max_window_bits != 15 {
            params.push(format!("client_max_window_bits={}", self.config.client_max_window_bits));
        }
        
        params.join("; ")
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = DeflateConfig::default();
    let compressor = PermessageDeflate::new(config);
    
    // Test message
    let message = "Hello, WebSocket! This is a test message that will be \
                   compressed using permessage-deflate. Repeated text: \
                   test test test test test test test test test";
    
    println!("Original message size: {} bytes", message.len());
    println!("Original: {}\n", message);
    
    // Compress
    let compressed = compressor.compress(message.as_bytes())?;
    println!("Compressed size: {} bytes", compressed.len());
    println!("Compression ratio: {:.2}%", 
             (compressed.len() as f64 / message.len() as f64) * 100.0);
    println!("Savings: {:.2}%\n", 
             (1.0 - compressed.len() as f64 / message.len() as f64) * 100.0);
    
    // Decompress
    let decompressed = compressor.decompress(&compressed)?;
    let result = String::from_utf8(decompressed)?;
    
    println!("Decompressed: {}", result);
    println!("Match: {}\n", if message == result { "YES" } else { "NO" });
    
    // Create full WebSocket frame
    let frame = compressor.create_compressed_frame(message)?;
    println!("Full WebSocket frame size: {} bytes", frame.len());
    println!("Frame header (first byte): 0x{:02X}", frame[0]);
    println!("RSV1 bit set: {}", if (frame[0] & RSV1) != 0 { "YES" } else { "NO" });
    println!("FIN bit set: {}", if (frame[0] & FIN) != 0 { "YES" } else { "NO" });
    
    // Test frame parsing
    let parsed_message = compressor.parse_compressed_frame(&frame)?;
    println!("\nParsed message from frame: {}", parsed_message);
    println!("Parse match: {}", if message == parsed_message { "YES" } else { "NO" });
    
    // Display handshake offer
    println!("\nHandshake offer:");
    println!("Sec-WebSocket-Extensions: {}", compressor.build_handshake_offer());
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compression_decompression() {
        let config = DeflateConfig::default();
        let compressor = PermessageDeflate::new(config);
        
        let original = "Test message with some repeated content: test test test";
        let compressed = compressor.compress(original.as_bytes()).unwrap();
        let decompressed = compressor.decompress(&compressed).unwrap();
        
        assert_eq!(original.as_bytes(), decompressed.as_slice());
        assert!(compressed.len() < original.len());
    }

    #[test]
    fn test_frame_creation_parsing() {
        let config = DeflateConfig::default();
        let compressor = PermessageDeflate::new(config);
        
        let message = "Hello, WebSocket compression!";
        let frame = compressor.create_compressed_frame(message).unwrap();
        let parsed = compressor.parse_compressed_frame(&frame).unwrap();
        
        assert_eq!(message, parsed);
        assert!(frame[0] & RSV1 != 0); // Check RSV1 bit is set
    }
}
```
## Handshake Negotiation Example

During the WebSocket handshake, the extension is negotiated:

**Client Request:**
```
GET /chat HTTP/1.1
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits
```

**Server Response:**
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits=15
```

## Performance Considerations

### When to Use Compression

**Best for:**
- JSON data with repeated field names
- Text-based messages with repetitive content
- Large messages (> 1KB)
- Applications where bandwidth is more constrained than CPU

**Avoid for:**
- Binary data that's already compressed (images, videos)
- Very small messages (< 100 bytes) - overhead may exceed benefits
- CPU-constrained environments
- Real-time applications where latency is critical

### Context Takeover Trade-offs

- **With context takeover**: Better compression ratios but higher memory usage
- **Without context takeover**: Slightly worse compression but predictable memory usage and easier error recovery

## Summary

The **permessage-deflate** WebSocket extension provides transparent, standards-based compression that can significantly reduce bandwidth usage for text-heavy WebSocket applications. By negotiating compression parameters during the handshake and using the DEFLATE algorithm to compress each message, it achieves typical compression ratios of 40-70% for JSON and text data.

Key implementation points include properly handling the DEFLATE stream tail bytes (0x00 0x00 0xFF 0xFF), setting the RSV1 bit in compressed frames, and managing compression contexts based on negotiated parameters. Both C/C++ (using zlib) and Rust (using flate2) provide robust libraries for implementing this extension.

The extension is particularly valuable for applications transmitting structured data like JSON, where field names and common patterns repeat frequently. However, it adds CPU overhead and should be carefully evaluated against alternatives for small messages or already-compressed binary data.