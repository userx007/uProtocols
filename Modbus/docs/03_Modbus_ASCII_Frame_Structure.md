# Modbus ASCII Frame Structure

## Overview

Modbus ASCII is one of the two serial transmission modes for Modbus (the other being Modbus RTU). Unlike RTU which uses binary encoding, ASCII mode transmits all data as hexadecimal ASCII characters, making frames human-readable and easier to debug but less efficient in terms of bandwidth and transmission speed.

## Frame Structure Components

### 1. **Start Delimiter (1 byte)**
- **Character**: Colon `:` (0x3A)
- Marks the beginning of every ASCII frame
- Receivers wait for this character to begin processing a message

### 2. **Address Field (2 ASCII characters)**
- Represents the slave device address (1-247)
- Encoded as two hexadecimal ASCII characters
- Example: Device address 17 (0x11) → transmitted as "11" (0x31 0x31)

### 3. **Function Code (2 ASCII characters)**
- Indicates the action to be performed
- Encoded as two hexadecimal characters
- Example: Function 03 (Read Holding Registers) → "03" (0x30 0x33)

### 4. **Data Field (variable length, even number of ASCII characters)**
- Contains request parameters or response data
- Length varies by function code
- All bytes encoded as pairs of hexadecimal ASCII characters

### 5. **LRC Error Check (2 ASCII characters)**
- **LRC** = Longitudinal Redundancy Check
- Calculated from address through data fields
- Two's complement of the sum of all bytes, keeping only the least significant byte

### 6. **End Delimiter (2 bytes)**
- **Characters**: Carriage Return + Line Feed (CR+LF) or `\r\n` (0x0D 0x0A)
- Marks the end of the frame

## LRC Calculation

The LRC is computed as follows:
1. Sum all bytes from address field through data field
2. Take the two's complement: `LRC = -SUM` or `LRC = ((SUM ^ 0xFF) + 1) & 0xFF`
3. Keep only the least significant byte

## Complete Frame Example

**Reading 2 holding registers starting at address 1000 from device 17:**

```
:1103E8000275\r\n
```

Breaking it down:
- `:` - Start delimiter
- `11` - Device address (17 decimal)
- `03` - Function code (Read Holding Registers)
- `03E8` - Starting address (1000 decimal)
- `0002` - Number of registers (2)
- `75` - LRC checksum
- `\r\n` - End delimiter

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Calculate LRC for Modbus ASCII
uint8_t calculate_lrc(const uint8_t *data, size_t length) {
    uint8_t lrc = 0;
    for (size_t i = 0; i < length; i++) {
        lrc += data[i];
    }
    return (uint8_t)((-lrc) & 0xFF); // Two's complement
}

// Convert byte to ASCII hex (2 characters)
void byte_to_ascii_hex(uint8_t byte, char *output) {
    const char hex[] = "0123456789ABCDEF";
    output[0] = hex[(byte >> 4) & 0x0F];
    output[1] = hex[byte & 0x0F];
}

// Convert ASCII hex character to value
uint8_t ascii_hex_to_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Build Modbus ASCII request frame
size_t build_ascii_frame(uint8_t slave_addr, uint8_t function_code,
                         const uint8_t *data, size_t data_len,
                         char *frame_buffer) {
    // Prepare raw data
    uint8_t raw[256];
    raw[0] = slave_addr;
    raw[1] = function_code;
    memcpy(&raw[2], data, data_len);
    
    // Calculate LRC
    uint8_t lrc = calculate_lrc(raw, 2 + data_len);
    
    // Build ASCII frame
    size_t pos = 0;
    frame_buffer[pos++] = ':'; // Start delimiter
    
    // Convert each byte to ASCII hex
    for (size_t i = 0; i < 2 + data_len; i++) {
        byte_to_ascii_hex(raw[i], &frame_buffer[pos]);
        pos += 2;
    }
    
    // Add LRC
    byte_to_ascii_hex(lrc, &frame_buffer[pos]);
    pos += 2;
    
    // Add end delimiter
    frame_buffer[pos++] = '\r';
    frame_buffer[pos++] = '\n';
    frame_buffer[pos] = '\0';
    
    return pos;
}

// Parse received ASCII frame
int parse_ascii_frame(const char *frame, uint8_t *slave_addr,
                      uint8_t *function_code, uint8_t *data,
                      size_t *data_len) {
    // Check start delimiter
    if (frame[0] != ':') return -1;
    
    // Find frame length (until CR or LF)
    size_t frame_len = 0;
    while (frame[frame_len] != '\r' && frame[frame_len] != '\n' && 
           frame[frame_len] != '\0') {
        frame_len++;
    }
    
    // Minimum frame: :AAFFLL\r\n (11 chars)
    if (frame_len < 9) return -2;
    
    // Convert ASCII hex to bytes
    uint8_t raw[256];
    size_t raw_len = (frame_len - 1) / 2; // Exclude ':' and divide by 2
    
    for (size_t i = 0; i < raw_len; i++) {
        uint8_t high = ascii_hex_to_value(frame[1 + i * 2]);
        uint8_t low = ascii_hex_to_value(frame[2 + i * 2]);
        raw[i] = (high << 4) | low;
    }
    
    // Verify LRC
    uint8_t received_lrc = raw[raw_len - 1];
    uint8_t calculated_lrc = calculate_lrc(raw, raw_len - 1);
    
    if (received_lrc != calculated_lrc) return -3;
    
    // Extract fields
    *slave_addr = raw[0];
    *function_code = raw[1];
    *data_len = raw_len - 3; // Exclude addr, func, lrc
    memcpy(data, &raw[2], *data_len);
    
    return 0;
}

// Example usage
int main() {
    char frame[256];
    uint8_t request_data[] = {0x03, 0xE8, 0x00, 0x02}; // Start=1000, Count=2
    
    // Build request
    size_t len = build_ascii_frame(0x11, 0x03, request_data, 4, frame);
    printf("Request frame: %s", frame);
    printf("Frame length: %zu bytes\n\n", len);
    
    // Parse the same frame
    uint8_t addr, func, data[256];
    size_t data_len;
    
    if (parse_ascii_frame(frame, &addr, &func, data, &data_len) == 0) {
        printf("Parsed successfully:\n");
        printf("  Address: 0x%02X\n", addr);
        printf("  Function: 0x%02X\n", func);
        printf("  Data bytes: %zu\n", data_len);
    }
    
    return 0;
}
```

## Rust Implementation

```rust
// Calculate LRC checksum
fn calculate_lrc(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
    ((!sum).wrapping_add(1)) & 0xFF // Two's complement
}

// Convert byte to ASCII hex characters
fn byte_to_ascii_hex(byte: u8) -> [char; 2] {
    const HEX: &[u8] = b"0123456789ABCDEF";
    [
        HEX[(byte >> 4) as usize] as char,
        HEX[(byte & 0x0F) as usize] as char,
    ]
}

// Convert ASCII hex character to value
fn ascii_hex_to_value(c: char) -> Option<u8> {
    match c {
        '0'..='9' => Some(c as u8 - b'0'),
        'A'..='F' => Some(c as u8 - b'A' + 10),
        'a'..='f' => Some(c as u8 - b'a' + 10),
        _ => None,
    }
}

// Build Modbus ASCII frame
fn build_ascii_frame(slave_addr: u8, function_code: u8, data: &[u8]) -> String {
    // Prepare raw bytes
    let mut raw = vec![slave_addr, function_code];
    raw.extend_from_slice(data);
    
    // Calculate LRC
    let lrc = calculate_lrc(&raw);
    
    // Build ASCII frame
    let mut frame = String::from(":");
    
    // Convert each byte to ASCII hex
    for &byte in &raw {
        let hex = byte_to_ascii_hex(byte);
        frame.push(hex[0]);
        frame.push(hex[1]);
    }
    
    // Add LRC
    let lrc_hex = byte_to_ascii_hex(lrc);
    frame.push(lrc_hex[0]);
    frame.push(lrc_hex[1]);
    
    // Add end delimiter
    frame.push_str("\r\n");
    
    frame
}

// Parse Modbus ASCII frame
fn parse_ascii_frame(frame: &str) -> Result<(u8, u8, Vec<u8>), &'static str> {
    // Check start delimiter
    if !frame.starts_with(':') {
        return Err("Missing start delimiter");
    }
    
    // Remove start delimiter and trim whitespace
    let frame = frame[1..].trim_end();
    
    // Must have even number of hex characters + at least addr(2) + func(2) + lrc(2)
    if frame.len() < 6 || frame.len() % 2 != 0 {
        return Err("Invalid frame length");
    }
    
    // Convert ASCII hex pairs to bytes
    let mut raw = Vec::new();
    let hex_chars: Vec<char> = frame.chars().collect();
    
    for chunk in hex_chars.chunks(2) {
        if chunk.len() != 2 {
            return Err("Incomplete hex pair");
        }
        
        let high = ascii_hex_to_value(chunk[0]).ok_or("Invalid hex character")?;
        let low = ascii_hex_to_value(chunk[1]).ok_or("Invalid hex character")?;
        raw.push((high << 4) | low);
    }
    
    // Verify minimum length
    if raw.len() < 3 {
        return Err("Frame too short");
    }
    
    // Verify LRC
    let received_lrc = raw.pop().unwrap();
    let calculated_lrc = calculate_lrc(&raw);
    
    if received_lrc != calculated_lrc {
        return Err("LRC check failed");
    }
    
    // Extract fields
    let slave_addr = raw[0];
    let function_code = raw[1];
    let data = raw[2..].to_vec();
    
    Ok((slave_addr, function_code, data))
}

fn main() {
    // Build a request frame: Read 2 holding registers from address 1000, device 17
    let request_data = vec![0x03, 0xE8, 0x00, 0x02];
    let frame = build_ascii_frame(0x11, 0x03, &request_data);
    
    println!("Request frame: {}", frame);
    println!("Frame length: {} bytes\n", frame.len());
    
    // Parse the frame
    match parse_ascii_frame(&frame) {
        Ok((addr, func, data)) => {
            println!("Parsed successfully:");
            println!("  Address: 0x{:02X}", addr);
            println!("  Function: 0x{:02X}", func);
            println!("  Data: {:02X?}", data);
        }
        Err(e) => println!("Parse error: {}", e),
    }
}
```

## Key Differences from RTU Mode

| Aspect | ASCII Mode | RTU Mode |
|--------|-----------|----------|
| **Encoding** | Hexadecimal ASCII characters | Raw binary |
| **Efficiency** | ~50% (each byte becomes 2 chars) | 100% |
| **Error Check** | LRC (1 byte) | CRC-16 (2 bytes) |
| **Delimiters** | `:` start, `\r\n` end | Silent intervals (3.5 char times) |
| **Readability** | Human-readable | Binary, not readable |
| **Speed** | Slower (more bytes transmitted) | Faster |
| **Debugging** | Easier (can see in terminal) | Requires hex viewer |

## Advantages and Disadvantages

### Advantages
- **Human-readable**: Easy to debug with simple serial terminal
- **Simple timing**: No critical timing requirements (unlike RTU's 3.5 character gaps)
- **Error detection**: Clear frame boundaries with delimiters

### Disadvantages
- **Inefficient**: Requires twice the bandwidth of RTU
- **Slower**: Takes longer to transmit same data
- **Less robust**: LRC is weaker than CRC-16 for error detection

## Summary

Modbus ASCII frames use colon-delimited, hexadecimal ASCII encoding with LRC error checking and CR+LF terminators. While less efficient than RTU mode, ASCII mode excels in debugging scenarios and applications where human readability matters more than bandwidth efficiency. The LRC checksum provides basic error detection through two's complement calculation of all data bytes. Both C/C++ and Rust implementations demonstrate the straightforward nature of ASCII frame construction and parsing, making it accessible for educational purposes and diagnostic tools despite its performance trade-offs in production environments.