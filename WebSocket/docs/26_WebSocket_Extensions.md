# WebSocket Extensions: Negotiating and Implementing Protocol Extensions

## Overview

WebSocket extensions are mechanisms that allow clients and servers to negotiate additional capabilities beyond the base WebSocket protocol (RFC 6455). Extensions can add features like compression, multiplexing, or custom framing modifications. They are negotiated during the opening handshake using the `Sec-WebSocket-Extensions` header.

## How WebSocket Extensions Work

### Extension Negotiation Process

1. **Client Request**: The client sends extension offers in the `Sec-WebSocket-Extensions` header
2. **Server Response**: The server accepts, modifies, or rejects extensions
3. **Active Extensions**: Both parties apply agreed-upon extensions to the connection

### Extension Header Format

```
Sec-WebSocket-Extensions: extension-name; param1=value1; param2=value2
```

Multiple extensions can be offered:
```
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits, extension2
```

## Common WebSocket Extensions

### 1. **permessage-deflate** (RFC 7692)
The most widely used extension, providing per-message compression using DEFLATE algorithm.

**Parameters:**
- `server_no_context_takeover`: Server resets compression context after each message
- `client_no_context_takeover`: Client resets compression context after each message
- `server_max_window_bits`: Server's LZ77 sliding window size (8-15)
- `client_max_window_bits`: Client's LZ77 sliding window size (8-15)

### 2. **permessage-bzip2**
Alternative compression using bzip2 (less common)

### 3. **multiplexing**
Allows multiple logical connections over a single WebSocket (draft, not standardized)

## C/C++ Implementation

Here's a comprehensive C++ implementation showing extension negotiation and permessage-deflate:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <zlib.h>
#include <sstream>
#include <cstring>

// Extension parameter structure
struct ExtensionParam {
    std::string name;
    std::string value;
};

// Extension structure
struct Extension {
    std::string name;
    std::vector<ExtensionParam> params;
};

// Parse extension header
std::vector<Extension> parseExtensions(const std::string& header) {
    std::vector<Extension> extensions;
    std::istringstream stream(header);
    std::string token;
    
    while (std::getline(stream, token, ',')) {
        Extension ext;
        std::istringstream extStream(token);
        std::string part;
        
        // First part is extension name
        if (std::getline(extStream, part, ';')) {
            // Trim whitespace
            size_t start = part.find_first_not_of(" \t");
            size_t end = part.find_last_not_of(" \t");
            ext.name = part.substr(start, end - start + 1);
        }
        
        // Parse parameters
        while (std::getline(extStream, part, ';')) {
            size_t eqPos = part.find('=');
            ExtensionParam param;
            
            if (eqPos != std::string::npos) {
                param.name = part.substr(0, eqPos);
                param.value = part.substr(eqPos + 1);
            } else {
                param.name = part;
                param.value = "";
            }
            
            // Trim whitespace
            size_t start = param.name.find_first_not_of(" \t");
            size_t end = param.name.find_last_not_of(" \t");
            if (start != std::string::npos) {
                param.name = param.name.substr(start, end - start + 1);
            }
            
            ext.params.push_back(param);
        }
        
        extensions.push_back(ext);
    }
    
    return extensions;
}

// WebSocket frame header with RSV bits for extensions
struct FrameHeader {
    bool fin;
    bool rsv1;  // Used by permessage-deflate
    bool rsv2;
    bool rsv3;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
};

// Compression context for permessage-deflate
class DeflateExtension {
private:
    z_stream deflate_stream;
    z_stream inflate_stream;
    bool server_no_context_takeover;
    bool client_no_context_takeover;
    int max_window_bits;
    bool initialized;
    
public:
    DeflateExtension(bool server_takeover = false, 
                     bool client_takeover = false,
                     int window_bits = 15) 
        : server_no_context_takeover(server_takeover),
          client_no_context_takeover(client_takeover),
          max_window_bits(window_bits),
          initialized(false) {
        initializeStreams();
    }
    
    ~DeflateExtension() {
        if (initialized) {
            deflateEnd(&deflate_stream);
            inflateEnd(&inflate_stream);
        }
    }
    
    void initializeStreams() {
        memset(&deflate_stream, 0, sizeof(deflate_stream));
        memset(&inflate_stream, 0, sizeof(inflate_stream));
        
        // Initialize deflate
        deflate_stream.zalloc = Z_NULL;
        deflate_stream.zfree = Z_NULL;
        deflate_stream.opaque = Z_NULL;
        
        // Negative value for raw deflate (no zlib header)
        if (deflateInit2(&deflate_stream, Z_DEFAULT_COMPRESSION, 
                        Z_DEFLATED, -max_window_bits, 8, 
                        Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("Failed to initialize deflate");
        }
        
        // Initialize inflate
        inflate_stream.zalloc = Z_NULL;
        inflate_stream.zfree = Z_NULL;
        inflate_stream.opaque = Z_NULL;
        inflate_stream.avail_in = 0;
        inflate_stream.next_in = Z_NULL;
        
        if (inflateInit2(&inflate_stream, -max_window_bits) != Z_OK) {
            throw std::runtime_error("Failed to initialize inflate");
        }
        
        initialized = true;
    }
    
    // Compress payload
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> compressed;
        compressed.reserve(data.size());
        
        deflate_stream.avail_in = data.size();
        deflate_stream.next_in = const_cast<uint8_t*>(data.data());
        
        uint8_t buffer[4096];
        int ret;
        
        do {
            deflate_stream.avail_out = sizeof(buffer);
            deflate_stream.next_out = buffer;
            
            ret = deflate(&deflate_stream, Z_SYNC_FLUSH);
            
            if (ret == Z_STREAM_ERROR) {
                throw std::runtime_error("Deflate error");
            }
            
            size_t have = sizeof(buffer) - deflate_stream.avail_out;
            compressed.insert(compressed.end(), buffer, buffer + have);
            
        } while (deflate_stream.avail_out == 0);
        
        // Remove trailing 0x00 0x00 0xff 0xff added by Z_SYNC_FLUSH
        if (compressed.size() >= 4) {
            compressed.resize(compressed.size() - 4);
        }
        
        if (server_no_context_takeover) {
            deflateReset(&deflate_stream);
        }
        
        return compressed;
    }
    
    // Decompress payload
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> decompressed;
        
        // Add back the 0x00 0x00 0xff 0xff trailer
        std::vector<uint8_t> input = data;
        input.push_back(0x00);
        input.push_back(0x00);
        input.push_back(0xff);
        input.push_back(0xff);
        
        inflate_stream.avail_in = input.size();
        inflate_stream.next_in = input.data();
        
        uint8_t buffer[4096];
        int ret;
        
        do {
            inflate_stream.avail_out = sizeof(buffer);
            inflate_stream.next_out = buffer;
            
            ret = inflate(&inflate_stream, Z_SYNC_FLUSH);
            
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) {
                throw std::runtime_error("Inflate error");
            }
            
            size_t have = sizeof(buffer) - inflate_stream.avail_out;
            decompressed.insert(decompressed.end(), buffer, buffer + have);
            
        } while (inflate_stream.avail_out == 0);
        
        if (client_no_context_takeover) {
            inflateReset(&inflate_stream);
        }
        
        return decompressed;
    }
};

// WebSocket server extension negotiation
class WebSocketServer {
private:
    std::vector<std::string> supported_extensions;
    DeflateExtension* deflate_ext;
    
public:
    WebSocketServer() : deflate_ext(nullptr) {
        supported_extensions.push_back("permessage-deflate");
    }
    
    ~WebSocketServer() {
        delete deflate_ext;
    }
    
    // Negotiate extensions from client request
    std::string negotiateExtensions(const std::string& client_extensions) {
        auto extensions = parseExtensions(client_extensions);
        std::vector<std::string> accepted;
        
        for (const auto& ext : extensions) {
            if (ext.name == "permessage-deflate") {
                // Parse parameters
                bool server_takeover = false;
                bool client_takeover = false;
                int window_bits = 15;
                
                for (const auto& param : ext.params) {
                    if (param.name == "server_no_context_takeover") {
                        server_takeover = true;
                    } else if (param.name == "client_no_context_takeover") {
                        client_takeover = true;
                    } else if (param.name == "server_max_window_bits") {
                        window_bits = std::stoi(param.value);
                    }
                }
                
                // Initialize compression
                deflate_ext = new DeflateExtension(server_takeover, 
                                                  client_takeover, 
                                                  window_bits);
                
                // Build response
                std::string response = "permessage-deflate";
                if (server_takeover) {
                    response += "; server_no_context_takeover";
                }
                if (client_takeover) {
                    response += "; client_no_context_takeover";
                }
                
                accepted.push_back(response);
            }
        }
        
        // Join accepted extensions
        std::string result;
        for (size_t i = 0; i < accepted.size(); ++i) {
            if (i > 0) result += ", ";
            result += accepted[i];
        }
        
        return result;
    }
    
    // Process frame with extension
    std::vector<uint8_t> processIncomingFrame(const FrameHeader& header, 
                                               const std::vector<uint8_t>& payload) {
        // If RSV1 is set and deflate extension is active, decompress
        if (header.rsv1 && deflate_ext != nullptr) {
            return deflate_ext->decompress(payload);
        }
        return payload;
    }
    
    // Create frame with extension
    void prepareOutgoingFrame(FrameHeader& header, 
                             std::vector<uint8_t>& payload) {
        // Compress if deflate extension is active
        if (deflate_ext != nullptr) {
            payload = deflate_ext->compress(payload);
            header.rsv1 = true;  // Set RSV1 bit
        }
    }
};

// Example usage
int main() {
    std::cout << "WebSocket Extension Example\n\n";
    
    // Simulate client extension request
    std::string client_request = 
        "permessage-deflate; client_max_window_bits";
    
    std::cout << "Client Request: " << client_request << "\n";
    
    // Server negotiates extensions
    WebSocketServer server;
    std::string server_response = server.negotiateExtensions(client_request);
    
    std::cout << "Server Response: " << server_response << "\n\n";
    
    // Simulate sending compressed message
    std::string message = "Hello, WebSocket! This is a test message that "
                         "will be compressed using the permessage-deflate "
                         "extension. Compression works best with repetitive "
                         "data. Repetitive data. Repetitive data.";
    
    std::vector<uint8_t> payload(message.begin(), message.end());
    FrameHeader header = {true, false, false, false, 1, false, payload.size()};
    
    std::cout << "Original size: " << payload.size() << " bytes\n";
    
    server.prepareOutgoingFrame(header, payload);
    
    std::cout << "Compressed size: " << payload.size() << " bytes\n";
    std::cout << "Compression ratio: " 
              << (1.0 - (double)payload.size() / message.size()) * 100 
              << "%\n";
    std::cout << "RSV1 bit set: " << (header.rsv1 ? "Yes" : "No") << "\n";
    
    return 0;
}
```

## Rust Implementation

Here's a Rust implementation with proper error handling and type safety:

```rust
use flate2::write::{DeflateEncoder, DeflateDecoder};
use flate2::Compression;
use std::io::Write;
use std::collections::HashMap;

#[derive(Debug, Clone)]
struct ExtensionParam {
    name: String,
    value: Option<String>,
}

#[derive(Debug, Clone)]
struct Extension {
    name: String,
    params: Vec<ExtensionParam>,
}

// Parse extension header
fn parse_extensions(header: &str) -> Vec<Extension> {
    header.split(',')
        .filter_map(|ext_str| {
            let parts: Vec<&str> = ext_str.split(';').collect();
            if parts.is_empty() {
                return None;
            }
            
            let name = parts[0].trim().to_string();
            let params = parts[1..]
                .iter()
                .filter_map(|param_str| {
                    let param = param_str.trim();
                    if let Some(eq_pos) = param.find('=') {
                        Some(ExtensionParam {
                            name: param[..eq_pos].trim().to_string(),
                            value: Some(param[eq_pos + 1..].trim().to_string()),
                        })
                    } else if !param.is_empty() {
                        Some(ExtensionParam {
                            name: param.to_string(),
                            value: None,
                        })
                    } else {
                        None
                    }
                })
                .collect();
            
            Some(Extension { name, params })
        })
        .collect()
}

// WebSocket frame header
#[derive(Debug, Clone)]
struct FrameHeader {
    fin: bool,
    rsv1: bool,
    rsv2: bool,
    rsv3: bool,
    opcode: u8,
    masked: bool,
    payload_length: u64,
}

// Deflate extension implementation
struct DeflateExtension {
    server_no_context_takeover: bool,
    client_no_context_takeover: bool,
    max_window_bits: u8,
}

impl DeflateExtension {
    fn new(
        server_no_context_takeover: bool,
        client_no_context_takeover: bool,
        max_window_bits: u8,
    ) -> Self {
        Self {
            server_no_context_takeover,
            client_no_context_takeover,
            max_window_bits: max_window_bits.clamp(8, 15),
        }
    }
    
    // Compress data using DEFLATE
    fn compress(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        let mut encoder = DeflateEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(data)?;
        let mut compressed = encoder.finish()?;
        
        // Remove trailing 0x00 0x00 0xff 0xff added by flush
        if compressed.len() >= 4 
            && &compressed[compressed.len() - 4..] == &[0x00, 0x00, 0xff, 0xff] {
            compressed.truncate(compressed.len() - 4);
        }
        
        Ok(compressed)
    }
    
    // Decompress data using INFLATE
    fn decompress(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        // Add back the trailing bytes
        let mut input = data.to_vec();
        input.extend_from_slice(&[0x00, 0x00, 0xff, 0xff]);
        
        let mut decoder = DeflateDecoder::new(Vec::new());
        decoder.write_all(&input)?;
        decoder.finish()
    }
}

// WebSocket server with extension support
struct WebSocketServer {
    supported_extensions: Vec<String>,
    deflate_extension: Option<DeflateExtension>,
}

impl WebSocketServer {
    fn new() -> Self {
        Self {
            supported_extensions: vec!["permessage-deflate".to_string()],
            deflate_extension: None,
        }
    }
    
    // Negotiate extensions based on client request
    fn negotiate_extensions(&mut self, client_header: &str) -> String {
        let extensions = parse_extensions(client_header);
        let mut accepted = Vec::new();
        
        for ext in extensions {
            if ext.name == "permessage-deflate" {
                let mut server_takeover = false;
                let mut client_takeover = false;
                let mut max_window_bits = 15;
                
                for param in &ext.params {
                    match param.name.as_str() {
                        "server_no_context_takeover" => server_takeover = true,
                        "client_no_context_takeover" => client_takeover = true,
                        "server_max_window_bits" => {
                            if let Some(ref value) = param.value {
                                max_window_bits = value.parse().unwrap_or(15);
                            }
                        }
                        "client_max_window_bits" => {
                            // Client is requesting to limit its window size
                        }
                        _ => {}
                    }
                }
                
                // Create deflate extension
                self.deflate_extension = Some(DeflateExtension::new(
                    server_takeover,
                    client_takeover,
                    max_window_bits,
                ));
                
                // Build response
                let mut response = "permessage-deflate".to_string();
                if server_takeover {
                    response.push_str("; server_no_context_takeover");
                }
                if client_takeover {
                    response.push_str("; client_no_context_takeover");
                }
                
                accepted.push(response);
            }
        }
        
        accepted.join(", ")
    }
    
    // Process incoming frame (decompress if needed)
    fn process_incoming_frame(
        &self,
        header: &FrameHeader,
        payload: &[u8],
    ) -> Result<Vec<u8>, std::io::Error> {
        if header.rsv1 {
            if let Some(ref deflate) = self.deflate_extension {
                return deflate.decompress(payload);
            }
        }
        Ok(payload.to_vec())
    }
    
    // Prepare outgoing frame (compress if extension active)
    fn prepare_outgoing_frame(
        &self,
        header: &mut FrameHeader,
        payload: &[u8],
    ) -> Result<Vec<u8>, std::io::Error> {
        if let Some(ref deflate) = self.deflate_extension {
            header.rsv1 = true;
            deflate.compress(payload)
        } else {
            Ok(payload.to_vec())
        }
    }
}

// Custom extension example
trait WebSocketExtension {
    fn name(&self) -> &str;
    fn negotiate(&mut self, params: &[ExtensionParam]) -> Option<Vec<ExtensionParam>>;
    fn process_outgoing(&self, data: &[u8]) -> Vec<u8>;
    fn process_incoming(&self, data: &[u8]) -> Vec<u8>;
}

struct CustomExtension {
    name: String,
    enabled: bool,
}

impl WebSocketExtension for CustomExtension {
    fn name(&self) -> &str {
        &self.name
    }
    
    fn negotiate(&mut self, params: &[ExtensionParam]) -> Option<Vec<ExtensionParam>> {
        // Custom negotiation logic
        self.enabled = true;
        Some(vec![])
    }
    
    fn process_outgoing(&self, data: &[u8]) -> Vec<u8> {
        if self.enabled {
            // Custom processing
            data.to_vec()
        } else {
            data.to_vec()
        }
    }
    
    fn process_incoming(&self, data: &[u8]) -> Vec<u8> {
        if self.enabled {
            // Custom processing
            data.to_vec()
        } else {
            data.to_vec()
        }
    }
}

fn main() {
    println!("WebSocket Extension Example\n");
    
    // Simulate client request
    let client_request = "permessage-deflate; client_max_window_bits";
    println!("Client Request: {}", client_request);
    
    // Server negotiates extensions
    let mut server = WebSocketServer::new();
    let server_response = server.negotiate_extensions(client_request);
    println!("Server Response: {}\n", server_response);
    
    // Test compression
    let message = "Hello, WebSocket! This is a test message that will be \
                   compressed using the permessage-deflate extension. \
                   Compression works best with repetitive data. \
                   Repetitive data. Repetitive data.";
    
    let payload = message.as_bytes();
    let mut header = FrameHeader {
        fin: true,
        rsv1: false,
        rsv2: false,
        rsv3: false,
        opcode: 1, // Text frame
        masked: false,
        payload_length: payload.len() as u64,
    };
    
    println!("Original size: {} bytes", payload.len());
    
    match server.prepare_outgoing_frame(&mut header, payload) {
        Ok(compressed) => {
            println!("Compressed size: {} bytes", compressed.len());
            println!(
                "Compression ratio: {:.2}%",
                (1.0 - compressed.len() as f64 / payload.len() as f64) * 100.0
            );
            println!("RSV1 bit set: {}", header.rsv1);
            
            // Test decompression
            match server.process_incoming_frame(&header, &compressed) {
                Ok(decompressed) => {
                    let recovered = String::from_utf8_lossy(&decompressed);
                    println!("\nDecompression successful!");
                    println!("Matches original: {}", recovered == message);
                }
                Err(e) => eprintln!("Decompression error: {}", e),
            }
        }
        Err(e) => eprintln!("Compression error: {}", e),
    }
}
```

## Key Concepts and Best Practices

### 1. **Extension Ordering**
Extensions are applied in the order they appear in the negotiation. The first extension processes data first on sending, and last on receiving.

### 2. **RSV Bits**
- RSV1, RSV2, RSV3 are reserved bits in the WebSocket frame header
- Extensions must define which RSV bits they use
- permessage-deflate uses RSV1
- Bits must be 0 unless an extension is negotiated that defines their use

### 3. **Context Takeover**
- **With context takeover**: Compression dictionary persists across messages (better compression)
- **Without context takeover**: Dictionary resets after each message (lower memory, thread-safe)

### 4. **Window Bits**
- Controls LZ77 sliding window size: 2^bits bytes
- Larger windows = better compression, more memory
- Range: 8-15 (256 bytes to 32 KB)

### 5. **Error Handling**
- If extension negotiation fails, connection proceeds without extensions
- Invalid compressed data should close the connection with status 1002 (protocol error)
- Unknown extensions should be ignored

### 6. **Security Considerations**
- **Compression oracles**: Attackers can infer encrypted content through compression ratios (CRIME/BREACH attacks)
- Mitigation: Disable compression for sensitive data or use `no_context_takeover`
- Validate extension parameters to prevent resource exhaustion

## Summary

WebSocket extensions provide a standardized way to add capabilities to the WebSocket protocol while maintaining backward compatibility. The negotiation happens during the opening handshake through the `Sec-WebSocket-Extensions` header, where clients propose extensions and servers accept or reject them with optional parameter modifications.

The **permessage-deflate** extension is the most widely implemented, offering significant bandwidth savings through DEFLATE compression. Implementation requires careful handling of RSV bits in frame headers, proper compression context management, and robust error handling for malformed compressed data.

Both C/C++ and Rust implementations demonstrate the core patterns: parsing extension headers, negotiating parameters, maintaining compression state, and applying transformations to frame payloads. The key challenges include managing zlib streams, handling the required trailing bytes in compressed messages, and deciding between context takeover modes based on memory constraints and thread safety requirements.

Extensions enable powerful features while keeping the base protocol simple, making WebSocket adaptable to diverse use cases from real-time gaming to IoT communications where bandwidth efficiency is critical.