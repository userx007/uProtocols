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