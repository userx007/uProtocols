# Protocol Violation Handling in WebSocket

Protocol violation handling is a critical aspect of WebSocket implementation that ensures robust communication by detecting and responding appropriately to malformed frames, invalid data, and protocol violations. Proper handling prevents security vulnerabilities, maintains connection integrity, and ensures compliance with RFC 6455.

## Understanding Protocol Violations

Protocol violations in WebSocket can include:

- **Malformed frames**: Incorrect frame structure, invalid opcodes, or corrupted headers
- **Invalid masking**: Client frames without masking or server frames with masking
- **Fragmentation errors**: Continuation frames without an initial frame, or mixed message types
- **Control frame violations**: Fragmented control frames or control frames exceeding 125 bytes
- **Invalid UTF-8**: Text frames containing invalid UTF-8 sequences
- **Reserved bit usage**: Non-zero reserved bits without negotiated extensions
- **Close frame violations**: Invalid close codes or malformed close payloads

## Key Concepts

### Frame Validation
Every incoming frame must be validated against RFC 6455 specifications before processing. This includes checking the frame header, payload length encoding, masking requirements, and opcode validity.

### Error Response Strategy
When violations are detected, the connection should be closed with an appropriate status code (typically 1002 for protocol error). The implementation should log the violation for debugging while avoiding information leakage to potential attackers.

### Security Considerations
Strict protocol enforcement prevents exploitation attempts. Malformed frames might indicate attacks such as buffer overflow attempts, injection attacks, or denial-of-service attempts.

## C Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// WebSocket opcodes
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

// Close status codes
#define WS_CLOSE_NORMAL           1000
#define WS_CLOSE_GOING_AWAY       1001
#define WS_CLOSE_PROTOCOL_ERROR   1002
#define WS_CLOSE_INVALID_DATA     1003
#define WS_CLOSE_INVALID_PAYLOAD  1007
#define WS_CLOSE_POLICY_VIOLATION 1008
#define WS_CLOSE_MESSAGE_TOO_BIG  1009

typedef struct {
    bool fin;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t masking_key[4];
} ws_frame_header_t;

typedef struct {
    bool in_fragmented_message;
    uint8_t fragmented_opcode;
    bool is_server; // Server doesn't send masked frames
    bool extensions_negotiated;
} ws_connection_state_t;

typedef enum {
    WS_VALIDATION_OK = 0,
    WS_VALIDATION_INVALID_OPCODE,
    WS_VALIDATION_RESERVED_BITS,
    WS_VALIDATION_CONTROL_FRAGMENTED,
    WS_VALIDATION_CONTROL_TOO_LARGE,
    WS_VALIDATION_INVALID_MASKING,
    WS_VALIDATION_CONTINUATION_ERROR,
    WS_VALIDATION_INVALID_UTF8,
    WS_VALIDATION_INVALID_CLOSE_CODE
} ws_validation_error_t;

// Validate frame header
ws_validation_error_t validate_frame_header(
    const ws_frame_header_t *header,
    const ws_connection_state_t *state
) {
    // Check for valid opcode
    if (header->opcode > 0xA || 
        (header->opcode > 0x2 && header->opcode < 0x8)) {
        fprintf(stderr, "Invalid opcode: 0x%X\n", header->opcode);
        return WS_VALIDATION_INVALID_OPCODE;
    }
    
    // Check reserved bits (must be 0 unless extensions negotiated)
    if (!state->extensions_negotiated && 
        (header->rsv1 || header->rsv2 || header->rsv3)) {
        fprintf(stderr, "Reserved bits set without negotiated extension\n");
        return WS_VALIDATION_RESERVED_BITS;
    }
    
    // Control frames must not be fragmented
    if (header->opcode >= 0x8 && !header->fin) {
        fprintf(stderr, "Control frame cannot be fragmented\n");
        return WS_VALIDATION_CONTROL_FRAGMENTED;
    }
    
    // Control frames must have payload <= 125 bytes
    if (header->opcode >= 0x8 && header->payload_length > 125) {
        fprintf(stderr, "Control frame payload too large: %llu\n", 
                (unsigned long long)header->payload_length);
        return WS_VALIDATION_CONTROL_TOO_LARGE;
    }
    
    // Validate masking requirements
    if (state->is_server && !header->masked) {
        fprintf(stderr, "Client frame must be masked\n");
        return WS_VALIDATION_INVALID_MASKING;
    }
    if (!state->is_server && header->masked) {
        fprintf(stderr, "Server frame must not be masked\n");
        return WS_VALIDATION_INVALID_MASKING;
    }
    
    // Validate continuation frames
    if (header->opcode == WS_OPCODE_CONTINUATION) {
        if (!state->in_fragmented_message) {
            fprintf(stderr, "Continuation frame without initial frame\n");
            return WS_VALIDATION_CONTINUATION_ERROR;
        }
    } else if (header->opcode == WS_OPCODE_TEXT || 
               header->opcode == WS_OPCODE_BINARY) {
        if (state->in_fragmented_message && !header->fin) {
            fprintf(stderr, "New message started during fragmentation\n");
            return WS_VALIDATION_CONTINUATION_ERROR;
        }
    }
    
    return WS_VALIDATION_OK;
}

// Validate UTF-8 encoding (simplified check)
bool is_valid_utf8(const uint8_t *data, size_t length) {
    size_t i = 0;
    
    while (i < length) {
        uint8_t byte = data[i];
        int continuation_bytes = 0;
        
        // Single-byte character (0xxxxxxx)
        if ((byte & 0x80) == 0) {
            i++;
            continue;
        }
        
        // Multi-byte character
        if ((byte & 0xE0) == 0xC0) continuation_bytes = 1;
        else if ((byte & 0xF0) == 0xE0) continuation_bytes = 2;
        else if ((byte & 0xF8) == 0xF0) continuation_bytes = 3;
        else return false; // Invalid start byte
        
        // Check continuation bytes
        for (int j = 0; j < continuation_bytes; j++) {
            i++;
            if (i >= length || (data[i] & 0xC0) != 0x80) {
                return false;
            }
        }
        i++;
    }
    
    return true;
}

// Validate close frame payload
ws_validation_error_t validate_close_payload(
    const uint8_t *payload,
    size_t length
) {
    if (length == 0) return WS_VALIDATION_OK;
    
    if (length == 1) {
        fprintf(stderr, "Close payload must be 0 or >= 2 bytes\n");
        return WS_VALIDATION_INVALID_CLOSE_CODE;
    }
    
    // Extract close code (big-endian)
    uint16_t close_code = (payload[0] << 8) | payload[1];
    
    // Validate close code ranges
    if (close_code < 1000 || 
        (close_code >= 1004 && close_code <= 1006) ||
        (close_code >= 1012 && close_code <= 2999)) {
        fprintf(stderr, "Invalid close code: %u\n", close_code);
        return WS_VALIDATION_INVALID_CLOSE_CODE;
    }
    
    // Validate UTF-8 reason (if present)
    if (length > 2 && !is_valid_utf8(payload + 2, length - 2)) {
        fprintf(stderr, "Invalid UTF-8 in close reason\n");
        return WS_VALIDATION_INVALID_UTF8;
    }
    
    return WS_VALIDATION_OK;
}

// Handle protocol violation
void handle_protocol_violation(
    ws_validation_error_t error,
    ws_connection_state_t *state
) {
    uint16_t close_code;
    const char *reason;
    
    switch (error) {
        case WS_VALIDATION_INVALID_OPCODE:
        case WS_VALIDATION_RESERVED_BITS:
        case WS_VALIDATION_CONTROL_FRAGMENTED:
        case WS_VALIDATION_CONTROL_TOO_LARGE:
        case WS_VALIDATION_INVALID_MASKING:
        case WS_VALIDATION_CONTINUATION_ERROR:
            close_code = WS_CLOSE_PROTOCOL_ERROR;
            reason = "Protocol violation";
            break;
            
        case WS_VALIDATION_INVALID_UTF8:
        case WS_VALIDATION_INVALID_CLOSE_CODE:
            close_code = WS_CLOSE_INVALID_PAYLOAD;
            reason = "Invalid payload data";
            break;
            
        default:
            close_code = WS_CLOSE_PROTOCOL_ERROR;
            reason = "Unknown error";
    }
    
    printf("Closing connection: %s (code %u)\n", reason, close_code);
    
    // In a real implementation, send close frame and close socket
    // send_close_frame(close_code, reason);
    // close_socket();
}

// Example usage
int main(void) {
    ws_connection_state_t state = {
        .in_fragmented_message = false,
        .fragmented_opcode = 0,
        .is_server = true,
        .extensions_negotiated = false
    };
    
    // Example 1: Valid frame
    ws_frame_header_t valid_frame = {
        .fin = true,
        .rsv1 = false, .rsv2 = false, .rsv3 = false,
        .opcode = WS_OPCODE_TEXT,
        .masked = true,
        .payload_length = 50
    };
    
    ws_validation_error_t result = validate_frame_header(&valid_frame, &state);
    printf("Valid frame check: %s\n", 
           result == WS_VALIDATION_OK ? "PASS" : "FAIL");
    
    // Example 2: Invalid - control frame fragmented
    ws_frame_header_t invalid_frame = {
        .fin = false,
        .rsv1 = false, .rsv2 = false, .rsv3 = false,
        .opcode = WS_OPCODE_PING,
        .masked = true,
        .payload_length = 10
    };
    
    result = validate_frame_header(&invalid_frame, &state);
    if (result != WS_VALIDATION_OK) {
        handle_protocol_violation(result, &state);
    }
    
    // Example 3: Validate close payload
    uint8_t close_payload[] = {0x03, 0xE8, 'B', 'y', 'e'}; // 1000 + "Bye"
    result = validate_close_payload(close_payload, sizeof(close_payload));
    printf("Close payload check: %s\n", 
           result == WS_VALIDATION_OK ? "PASS" : "FAIL");
    
    return 0;
}
```

## C++ Implementation

```cpp
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
```

## Rust Implementation

```rust
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
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

    fn is_control(&self) -> bool {
        (*self as u8) >= 0x8
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
enum CloseCode {
    Normal = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    InvalidData = 1003,
    InvalidPayload = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009,
}

#[derive(Debug)]
struct ProtocolViolation {
    code: CloseCode,
    message: String,
}

impl ProtocolViolation {
    fn new(code: CloseCode, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

impl fmt::Display for ProtocolViolation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} (code: {})", self.message, self.code as u16)
    }
}

impl std::error::Error for ProtocolViolation {}

type Result<T> = std::result::Result<T, ProtocolViolation>;

#[derive(Debug, Clone)]
struct FrameHeader {
    fin: bool,
    rsv1: bool,
    rsv2: bool,
    rsv3: bool,
    opcode: Opcode,
    masked: bool,
    payload_length: u64,
    masking_key: Option<[u8; 4]>,
}

impl FrameHeader {
    fn new(opcode: Opcode) -> Self {
        Self {
            fin: true,
            rsv1: false,
            rsv2: false,
            rsv3: false,
            opcode,
            masked: false,
            payload_length: 0,
            masking_key: None,
        }
    }
}

struct ConnectionState {
    in_fragmented_message: bool,
    fragmented_opcode: Option<Opcode>,
    is_server: bool,
    extensions_negotiated: bool,
}

impl ConnectionState {
    fn new(is_server: bool) -> Self {
        Self {
            in_fragmented_message: false,
            fragmented_opcode: None,
            is_server,
            extensions_negotiated: false,
        }
    }

    fn reset_fragmentation(&mut self) {
        self.in_fragmented_message = false;
        self.fragmented_opcode = None;
    }
}

struct WebSocketValidator {
    state: ConnectionState,
}

impl WebSocketValidator {
    fn new(is_server: bool) -> Self {
        Self {
            state: ConnectionState::new(is_server),
        }
    }

    fn validate_frame_header(&mut self, header: &FrameHeader) -> Result<()> {
        // Validate reserved bits
        if !self.state.extensions_negotiated
            && (header.rsv1 || header.rsv2 || header.rsv3)
        {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Reserved bits set without negotiated extension",
            ));
        }

        // Control frames must not be fragmented
        if header.opcode.is_control() && !header.fin {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Control frames cannot be fragmented",
            ));
        }

        // Control frames payload size limit
        if header.opcode.is_control() && header.payload_length > 125 {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                format!(
                    "Control frame payload exceeds 125 bytes: {}",
                    header.payload_length
                ),
            ));
        }

        // Validate masking
        self.validate_masking(header)?;

        // Validate fragmentation
        self.validate_fragmentation(header)?;

        Ok(())
    }

    fn validate_masking(&self, header: &FrameHeader) -> Result<()> {
        if self.state.is_server && !header.masked {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Client frames must be masked",
            ));
        }

        if !self.state.is_server && header.masked {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Server frames must not be masked",
            ));
        }

        Ok(())
    }

    fn validate_fragmentation(&mut self, header: &FrameHeader) -> Result<()> {
        match header.opcode {
            Opcode::Continuation => {
                if !self.state.in_fragmented_message {
                    return Err(ProtocolViolation::new(
                        CloseCode::ProtocolError,
                        "Continuation frame without initial frame",
                    ));
                }

                if header.fin {
                    self.state.reset_fragmentation();
                }
            }
            Opcode::Text | Opcode::Binary => {
                if self.state.in_fragmented_message {
                    return Err(ProtocolViolation::new(
                        CloseCode::ProtocolError,
                        "New message started during active fragmentation",
                    ));
                }

                if !header.fin {
                    self.state.in_fragmented_message = true;
                    self.state.fragmented_opcode = Some(header.opcode);
                }
            }
            _ => {} // Control frames
        }

        Ok(())
    }

    fn validate_text_payload(&self, payload: &[u8]) -> Result<()> {
        if !is_valid_utf8(payload) {
            return Err(ProtocolViolation::new(
                CloseCode::InvalidPayload,
                "Text frame contains invalid UTF-8",
            ));
        }
        Ok(())
    }

    fn validate_close_payload(&self, payload: &[u8]) -> Result<()> {
        if payload.is_empty() {
            return Ok(());
        }

        if payload.len() == 1 {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Close payload must be 0 or at least 2 bytes",
            ));
        }

        // Extract close code (big-endian)
        let close_code = u16::from_be_bytes([payload[0], payload[1]]);

        // Validate close code ranges
        if close_code < 1000
            || (close_code >= 1004 && close_code <= 1006)
            || (close_code >= 1012 && close_code <= 2999)
        {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                format!("Invalid close code: {}", close_code),
            ));
        }

        // Validate UTF-8 reason
        if payload.len() > 2 && !is_valid_utf8(&payload[2..]) {
            return Err(ProtocolViolation::new(
                CloseCode::InvalidPayload,
                "Close reason contains invalid UTF-8",
            ));
        }

        Ok(())
    }
}

fn is_valid_utf8(data: &[u8]) -> bool {
    std::str::from_utf8(data).is_ok()
}

struct WebSocketConnection {
    validator: WebSocketValidator,
}

impl WebSocketConnection {
    fn new(is_server: bool) -> Self {
        Self {
            validator: WebSocketValidator::new(is_server),
        }
    }

    fn process_frame(&mut self, header: &FrameHeader, payload: &[u8]) {
        match self.validate_and_process(header, payload) {
            Ok(_) => println!("✓ Frame processed successfully"),
            Err(violation) => self.handle_protocol_violation(violation),
        }
    }

    fn validate_and_process(
        &mut self,
        header: &FrameHeader,
        payload: &[u8],
    ) -> Result<()> {
        // Validate frame header
        self.validator.validate_frame_header(header)?;

        // Additional payload validation
        match header.opcode {
            Opcode::Text => self.validator.validate_text_payload(payload)?,
            Opcode::Close => self.validator.validate_close_payload(payload)?,
            _ => {}
        }

        Ok(())
    }

    fn handle_protocol_violation(&mut self, violation: ProtocolViolation) {
        eprintln!("✗ Protocol violation: {}", violation);
        self.send_close_frame(violation.code, &violation.message);
        self.close_connection();
    }

    fn send_close_frame(&self, code: CloseCode, reason: &str) {
        println!("  → Sending close frame: {} ({})", code as u16, reason);
        // Implementation would construct and send actual close frame
    }

    fn close_connection(&mut self) {
        println!("  → Connection closed");
        self.validator.state.reset_fragmentation();
        // Implementation would close socket
    }
}

fn main() {
    let mut connection = WebSocketConnection::new(true); // Server-side

    println!("=== Test 1: Valid text frame ===");
    let mut valid_frame = FrameHeader::new(Opcode::Text);
    valid_frame.masked = true;
    valid_frame.payload_length = 11;
    let payload = b"Hello World";
    connection.process_frame(&valid_frame, payload);

    println!("\n=== Test 2: Control frame fragmented (invalid) ===");
    let mut invalid_frame = FrameHeader::new(Opcode::Ping);
    invalid_frame.fin = false;
    invalid_frame.masked = true;
    connection.process_frame(&invalid_frame, &[]);

    println!("\n=== Test 3: Unmasked client frame (invalid) ===");
    let mut unmasked_frame = FrameHeader::new(Opcode::Text);
    unmasked_frame.masked = false;
    connection.process_frame(&unmasked_frame, b"test");

    println!("\n=== Test 4: Invalid UTF-8 in text frame ===");
    let mut text_frame = FrameHeader::new(Opcode::Text);
    text_frame.masked = true;
    let invalid_utf8 = &[0xFF, 0xFE];
    connection.process_frame(&text_frame, invalid_utf8);

    println!("\n=== Test 5: Valid close frame ===");
    let mut close_frame = FrameHeader::new(Opcode::Close);
    close_frame.masked = true;
    close_frame.payload_length = 5;
    let close_payload = &[0x03, 0xE8, b'B', b'y', b'e']; // 1000 + "Bye"
    connection.process_frame(&close_frame, close_payload);

    println!("\n=== Test 6: Fragmentation sequence ===");
    let mut start_frame = FrameHeader::new(Opcode::Text);
    start_frame.fin = false;
    start_frame.masked = true;
    connection.process_frame(&start_frame, b"Hello ");

    let mut cont_frame = FrameHeader::new(Opcode::Continuation);
    cont_frame.masked = true;
    connection.process_frame(&cont_frame, b"World");
}
```

## Summary

**Protocol Violation Handling** in WebSocket is essential for maintaining secure, robust connections. Key takeaways:

### Critical Validation Points

1. **Frame Structure**: Validate opcodes, reserved bits, FIN flag, and masking requirements
2. **Control Frames**: Must not be fragmented and cannot exceed 125 bytes payload
3. **Masking Rules**: Client-to-server frames must be masked; server-to-client frames must not be
4. **UTF-8 Validation**: Text frames and close reasons must contain valid UTF-8
5. **Fragmentation**: Continuation frames must follow initial frames without interleaving

### Implementation Strategies

**C Implementation**: Demonstrates low-level validation with explicit error codes and procedural error handling. Suitable for embedded systems or performance-critical applications.

**C++ Implementation**: Uses exceptions for violation handling with RAII principles. Provides type-safe opcodes and clean separation of concerns through the validator class.

**Rust Implementation**: Leverages Rust's `Result` type for error propagation and pattern matching for opcode handling. Provides compile-time guarantees about error handling with zero-cost abstractions.

### Best Practices

- **Fail Fast**: Detect violations early and close connections immediately
- **Appropriate Close Codes**: Use RFC-defined codes (1002 for protocol errors, 1007 for invalid payload)
- **Security First**: Never trust client input; validate everything
- **Clear Logging**: Log violations for debugging without exposing internals to clients
- **State Management**: Track fragmentation state to detect sequencing errors
- **No Assumptions**: Don't assume well-behaved clients; enforce all protocol rules

Proper protocol violation handling prevents security exploits, ensures interoperability, and maintains connection integrity throughout the WebSocket lifecycle.