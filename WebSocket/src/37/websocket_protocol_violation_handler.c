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