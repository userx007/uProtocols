#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <sstream>

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

enum class CloseCode : uint16_t {
    Normal = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    InvalidData = 1003,
    InvalidPayload = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009
};

class ProtocolViolationException : public std::runtime_error {
private:
    CloseCode code_;
    
public:
    ProtocolViolationException(CloseCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    
    CloseCode getCloseCode() const { return code_; }
};

struct FrameHeader {
    bool fin;
    bool rsv1, rsv2, rsv3;
    Opcode opcode;
    bool masked;
    uint64_t payloadLength;
    std::vector<uint8_t> maskingKey;
    
    FrameHeader() : fin(false), rsv1(false), rsv2(false), rsv3(false),
                    opcode(Opcode::Continuation), masked(false), 
                    payloadLength(0), maskingKey(4, 0) {}
};

class WebSocketValidator {
private:
    bool inFragmentedMessage_;
    Opcode fragmentedOpcode_;
    bool isServer_;
    bool extensionsNegotiated_;
    
    bool isControlFrame(Opcode opcode) const {
        return static_cast<uint8_t>(opcode) >= 0x8;
    }
    
    bool isValidOpcode(uint8_t opcode) const {
        return opcode <= 0xA && !(opcode > 0x2 && opcode < 0x8);
    }
    
    bool isValidUtf8(const std::vector<uint8_t>& data) const {
        size_t i = 0;
        
        while (i < data.size()) {
            uint8_t byte = data[i];
            int continuationBytes = 0;
            
            // Single-byte character
            if ((byte & 0x80) == 0) {
                i++;
                continue;
            }
            
            // Multi-byte character
            if ((byte & 0xE0) == 0xC0) {
                continuationBytes = 1;
            } else if ((byte & 0xF0) == 0xE0) {
                continuationBytes = 2;
            } else if ((byte & 0xF8) == 0xF0) {
                continuationBytes = 3;
            } else {
                return false; // Invalid start byte
            }
            
            // Verify continuation bytes
            for (int j = 0; j < continuationBytes; j++) {
                i++;
                if (i >= data.size() || (data[i] & 0xC0) != 0x80) {
                    return false;
                }
            }
            i++;
        }
        
        return true;
    }
    
public:
    WebSocketValidator(bool isServer = true, bool extensionsNegotiated = false)
        : inFragmentedMessage_(false),
          fragmentedOpcode_(Opcode::Continuation),
          isServer_(isServer),
          extensionsNegotiated_(extensionsNegotiated) {}
    
    void validateFrameHeader(const FrameHeader& header) {
        // Validate opcode
        if (!isValidOpcode(static_cast<uint8_t>(header.opcode))) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Invalid opcode: 0x" + toHex(static_cast<uint8_t>(header.opcode))
            );
        }
        
        // Validate reserved bits
        if (!extensionsNegotiated_ && (header.rsv1 || header.rsv2 || header.rsv3)) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Reserved bits set without negotiated extension"
            );
        }
        
        // Control frames must not be fragmented
        if (isControlFrame(header.opcode) && !header.fin) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Control frames cannot be fragmented"
            );
        }
        
        // Control frames payload size limit
        if (isControlFrame(header.opcode) && header.payloadLength > 125) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Control frame payload exceeds 125 bytes: " + 
                std::to_string(header.payloadLength)
            );
        }
        
        // Validate masking
        if (isServer_ && !header.masked) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Client frames must be masked"
            );
        }
        if (!isServer_ && header.masked) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Server frames must not be masked"
            );
        }
        
        // Validate message fragmentation
        validateFragmentation(header);
    }
    
    void validateFragmentation(const FrameHeader& header) {
        if (header.opcode == Opcode::Continuation) {
            if (!inFragmentedMessage_) {
                throw ProtocolViolationException(
                    CloseCode::ProtocolError,
                    "Continuation frame without initial frame"
                );
            }
            
            if (header.fin) {
                inFragmentedMessage_ = false;
                fragmentedOpcode_ = Opcode::Continuation;
            }
        } else if (header.opcode == Opcode::Text || header.opcode == Opcode::Binary) {
            if (inFragmentedMessage_) {
                throw ProtocolViolationException(
                    CloseCode::ProtocolError,
                    "New message started during active fragmentation"
                );
            }
            
            if (!header.fin) {
                inFragmentedMessage_ = true;
                fragmentedOpcode_ = header.opcode;
            }
        }
    }
    
    void validateTextPayload(const std::vector<uint8_t>& payload) {
        if (!isValidUtf8(payload)) {
            throw ProtocolViolationException(
                CloseCode::InvalidPayload,
                "Text frame contains invalid UTF-8"
            );
        }
    }
    
    void validateClosePayload(const std::vector<uint8_t>& payload) {
        if (payload.empty()) return;
        
        if (payload.size() == 1) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Close payload must be 0 or at least 2 bytes"
            );
        }
        
        // Extract close code
        uint16_t closeCode = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
        
        // Validate close code
        if (closeCode < 1000 ||
            (closeCode >= 1004 && closeCode <= 1006) ||
            (closeCode >= 1012 && closeCode <= 2999)) {
            throw ProtocolViolationException(
                CloseCode::ProtocolError,
                "Invalid close code: " + std::to_string(closeCode)
            );
        }
        
        // Validate UTF-8 reason
        if (payload.size() > 2) {
            std::vector<uint8_t> reason(payload.begin() + 2, payload.end());
            if (!isValidUtf8(reason)) {
                throw ProtocolViolationException(
                    CloseCode::InvalidPayload,
                    "Close reason contains invalid UTF-8"
                );
            }
        }
    }
    
    void reset() {
        inFragmentedMessage_ = false;
        fragmentedOpcode_ = Opcode::Continuation;
    }
    
private:
    std::string toHex(uint8_t value) const {
        std::stringstream ss;
        ss << std::hex << static_cast<int>(value);
        return ss.str();
    }
};

// WebSocket connection handler
class WebSocketConnection {
private:
    WebSocketValidator validator_;
    
public:
    WebSocketConnection(bool isServer = true) : validator_(isServer) {}
    
    void processFrame(const FrameHeader& header, const std::vector<uint8_t>& payload) {
        try {
            // Validate frame header
            validator_.validateFrameHeader(header);
            
            // Additional payload validation based on opcode
            if (header.opcode == Opcode::Text) {
                validator_.validateTextPayload(payload);
            } else if (header.opcode == Opcode::Close) {
                validator_.validateClosePayload(payload);
            }
            
            std::cout << "Frame processed successfully\n";
            
        } catch (const ProtocolViolationException& e) {
            handleProtocolViolation(e);
        }
    }
    
    void handleProtocolViolation(const ProtocolViolationException& e) {
        std::cerr << "Protocol violation detected: " << e.what() << "\n";
        std::cerr << "Closing with code: " 
                  << static_cast<uint16_t>(e.getCloseCode()) << "\n";
        
        // Send close frame with appropriate code
        sendCloseFrame(e.getCloseCode(), e.what());
        
        // Close the connection
        closeConnection();
    }
    
    void sendCloseFrame(CloseCode code, const std::string& reason) {
        std::cout << "Sending close frame: " << reason << "\n";
        // Implementation would send actual close frame
    }
    
    void closeConnection() {
        std::cout << "Connection closed\n";
        validator_.reset();
        // Implementation would close socket
    }
};

// Example usage
int main() {
    WebSocketConnection connection(true); // Server-side
    
    std::cout << "=== Test 1: Valid text frame ===\n";
    FrameHeader validFrame;
    validFrame.fin = true;
    validFrame.opcode = Opcode::Text;
    validFrame.masked = true;
    validFrame.payloadLength = 11;
    std::vector<uint8_t> payload = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
    connection.processFrame(validFrame, payload);
    
    std::cout << "\n=== Test 2: Control frame fragmented (invalid) ===\n";
    FrameHeader invalidFrame;
    invalidFrame.fin = false;
    invalidFrame.opcode = Opcode::Ping;
    invalidFrame.masked = true;
    invalidFrame.payloadLength = 10;
    connection.processFrame(invalidFrame, {});
    
    std::cout << "\n=== Test 3: Invalid UTF-8 in text frame ===\n";
    FrameHeader textFrame;
    textFrame.fin = true;
    textFrame.opcode = Opcode::Text;
    textFrame.masked = true;
    textFrame.payloadLength = 2;
    std::vector<uint8_t> invalidUtf8 = {0xFF, 0xFE}; // Invalid UTF-8
    connection.processFrame(textFrame, invalidUtf8);
    
    return 0;
}