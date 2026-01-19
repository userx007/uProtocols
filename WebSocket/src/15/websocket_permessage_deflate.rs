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