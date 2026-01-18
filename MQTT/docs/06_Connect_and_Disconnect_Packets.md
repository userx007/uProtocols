# MQTT Connect and Disconnect Packets

## Overview

Connect and Disconnect packets are fundamental to MQTT session management. The **CONNECT** packet is the first packet a client sends to establish a session with the broker, while the **DISCONNECT** packet allows for graceful session termination. These packets handle authentication, session configuration, and cleanup.

## CONNECT Packet Structure

The CONNECT packet contains several critical components:

### Fixed Header
- **Packet Type**: 1 (CONNECT)
- **Flags**: Reserved (must be 0)
- **Remaining Length**: Variable

### Variable Header
- **Protocol Name**: "MQTT" (for MQTT 3.1.1+)
- **Protocol Level**: 4 for MQTT 3.1.1, 5 for MQTT 5.0
- **Connect Flags**: 8-bit field containing:
  - Username flag
  - Password flag
  - Will Retain flag
  - Will QoS (2 bits)
  - Will flag
  - Clean Session/Clean Start flag
  - Reserved bit (must be 0)
- **Keep Alive**: Time interval in seconds

### Payload
- **Client ID**: Unique identifier for the client
- **Will Topic**: (if Will flag is set)
- **Will Message**: (if Will flag is set)
- **Username**: (if Username flag is set)
- **Password**: (if Password flag is set)

## DISCONNECT Packet Structure

The DISCONNECT packet is simpler:

### MQTT 3.1.1
- **Fixed Header**: Packet type 14, no payload

### MQTT 5.0
- **Fixed Header**: Packet type 14
- **Variable Header**: Reason code and properties
- **Payload**: None

## C/C++ Implementation Examples

### Basic CONNECT Packet Construction (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t remaining_length;
} mqtt_fixed_header_t;

typedef struct {
    char protocol_name[5];
    uint8_t protocol_level;
    uint8_t connect_flags;
    uint16_t keep_alive;
} mqtt_connect_var_header_t;

// Encode remaining length (MQTT variable byte integer)
int encode_remaining_length(uint8_t *buf, uint32_t length) {
    int bytes = 0;
    do {
        uint8_t encoded_byte = length % 128;
        length /= 128;
        if (length > 0) {
            encoded_byte |= 128;
        }
        buf[bytes++] = encoded_byte;
    } while (length > 0);
    return bytes;
}

// Build CONNECT packet
int build_connect_packet(uint8_t *buffer, size_t buffer_size,
                        const char *client_id,
                        const char *username,
                        const char *password,
                        uint16_t keep_alive) {
    int offset = 0;
    
    // Calculate payload length
    uint16_t client_id_len = strlen(client_id);
    uint16_t username_len = username ? strlen(username) : 0;
    uint16_t password_len = password ? strlen(password) : 0;
    
    // Variable header: 10 bytes (protocol name + level + flags + keep alive)
    uint32_t remaining_length = 10;
    remaining_length += 2 + client_id_len; // Client ID with length prefix
    if (username) remaining_length += 2 + username_len;
    if (password) remaining_length += 2 + password_len;
    
    // Fixed header
    buffer[offset++] = 0x10; // CONNECT packet type
    offset += encode_remaining_length(&buffer[offset], remaining_length);
    
    // Variable header - Protocol name
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x04;
    memcpy(&buffer[offset], "MQTT", 4);
    offset += 4;
    
    // Protocol level (4 = MQTT 3.1.1)
    buffer[offset++] = 0x04;
    
    // Connect flags
    uint8_t flags = 0x02; // Clean session
    if (username) flags |= 0x80;
    if (password) flags |= 0x40;
    buffer[offset++] = flags;
    
    // Keep alive
    buffer[offset++] = (keep_alive >> 8) & 0xFF;
    buffer[offset++] = keep_alive & 0xFF;
    
    // Payload - Client ID
    buffer[offset++] = (client_id_len >> 8) & 0xFF;
    buffer[offset++] = client_id_len & 0xFF;
    memcpy(&buffer[offset], client_id, client_id_len);
    offset += client_id_len;
    
    // Username
    if (username) {
        buffer[offset++] = (username_len >> 8) & 0xFF;
        buffer[offset++] = username_len & 0xFF;
        memcpy(&buffer[offset], username, username_len);
        offset += username_len;
    }
    
    // Password
    if (password) {
        buffer[offset++] = (password_len >> 8) & 0xFF;
        buffer[offset++] = password_len & 0xFF;
        memcpy(&buffer[offset], password, password_len);
        offset += password_len;
    }
    
    return offset;
}

// Build DISCONNECT packet
int build_disconnect_packet(uint8_t *buffer) {
    buffer[0] = 0xE0; // DISCONNECT packet type
    buffer[1] = 0x00; // Remaining length = 0
    return 2;
}

int main() {
    uint8_t packet[256];
    int len;
    
    // Create CONNECT packet
    len = build_connect_packet(packet, sizeof(packet),
                               "client123",
                               "user1",
                               "pass123",
                               60);
    
    printf("CONNECT packet (%d bytes):\n", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", packet[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n\n");
    
    // Create DISCONNECT packet
    len = build_disconnect_packet(packet);
    printf("DISCONNECT packet (%d bytes): ", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
    
    return 0;
}
```

### C++ with Last Will and Testament

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

class MQTTConnectPacket {
private:
    std::string client_id;
    std::string username;
    std::string password;
    std::string will_topic;
    std::string will_message;
    uint16_t keep_alive;
    bool clean_session;
    uint8_t will_qos;
    bool will_retain;

    void encodeString(std::vector<uint8_t>& buffer, const std::string& str) {
        uint16_t len = str.length();
        buffer.push_back((len >> 8) & 0xFF);
        buffer.push_back(len & 0xFF);
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    void encodeRemainingLength(std::vector<uint8_t>& buffer, uint32_t length) {
        do {
            uint8_t encoded_byte = length % 128;
            length /= 128;
            if (length > 0) {
                encoded_byte |= 128;
            }
            buffer.push_back(encoded_byte);
        } while (length > 0);
    }

public:
    MQTTConnectPacket(const std::string& client_id, uint16_t keep_alive = 60)
        : client_id(client_id), keep_alive(keep_alive), 
          clean_session(true), will_qos(0), will_retain(false) {}

    void setCredentials(const std::string& user, const std::string& pass) {
        username = user;
        password = pass;
    }

    void setWill(const std::string& topic, const std::string& message, 
                 uint8_t qos = 0, bool retain = false) {
        will_topic = topic;
        will_message = message;
        will_qos = qos;
        will_retain = retain;
    }

    std::vector<uint8_t> encode() {
        std::vector<uint8_t> packet;
        std::vector<uint8_t> variable_header;
        std::vector<uint8_t> payload;

        // Variable header - Protocol name
        variable_header.push_back(0x00);
        variable_header.push_back(0x04);
        variable_header.insert(variable_header.end(), {'M', 'Q', 'T', 'T'});

        // Protocol level
        variable_header.push_back(0x04);

        // Connect flags
        uint8_t flags = 0;
        if (clean_session) flags |= 0x02;
        if (!will_topic.empty()) {
            flags |= 0x04;
            flags |= (will_qos & 0x03) << 3;
            if (will_retain) flags |= 0x20;
        }
        if (!username.empty()) flags |= 0x80;
        if (!password.empty()) flags |= 0x40;
        variable_header.push_back(flags);

        // Keep alive
        variable_header.push_back((keep_alive >> 8) & 0xFF);
        variable_header.push_back(keep_alive & 0xFF);

        // Payload - Client ID
        encodeString(payload, client_id);

        // Will topic and message
        if (!will_topic.empty()) {
            encodeString(payload, will_topic);
            encodeString(payload, will_message);
        }

        // Username and password
        if (!username.empty()) encodeString(payload, username);
        if (!password.empty()) encodeString(payload, password);

        // Fixed header
        packet.push_back(0x10); // CONNECT packet type
        uint32_t remaining_length = variable_header.size() + payload.size();
        encodeRemainingLength(packet, remaining_length);

        // Combine all parts
        packet.insert(packet.end(), variable_header.begin(), variable_header.end());
        packet.insert(packet.end(), payload.begin(), payload.end());

        return packet;
    }
};

int main() {
    // Create CONNECT packet with Last Will
    MQTTConnectPacket connect("sensor_node_42", 120);
    connect.setCredentials("mqtt_user", "secure_password");
    connect.setWill("devices/sensor_node_42/status", "offline", 1, true);

    auto packet = connect.encode();

    std::cout << "CONNECT packet with LWT (" << packet.size() << " bytes):\n";
    for (size_t i = 0; i < packet.size(); i++) {
        printf("%02X ", packet[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << "\n";

    return 0;
}
```

## Rust Implementation Examples

### Basic CONNECT and DISCONNECT

```rust
use std::io::{self, Write};

#[derive(Debug)]
struct MqttConnectPacket {
    client_id: String,
    username: Option<String>,
    password: Option<String>,
    keep_alive: u16,
    clean_session: bool,
}

impl MqttConnectPacket {
    fn new(client_id: String, keep_alive: u16) -> Self {
        Self {
            client_id,
            username: None,
            password: None,
            keep_alive,
            clean_session: true,
        }
    }

    fn with_credentials(mut self, username: String, password: String) -> Self {
        self.username = Some(username);
        self.password = Some(password);
        self
    }

    fn encode_string(buffer: &mut Vec<u8>, s: &str) {
        let len = s.len() as u16;
        buffer.push((len >> 8) as u8);
        buffer.push((len & 0xFF) as u8);
        buffer.extend_from_slice(s.as_bytes());
    }

    fn encode_remaining_length(buffer: &mut Vec<u8>, mut length: u32) {
        loop {
            let mut encoded_byte = (length % 128) as u8;
            length /= 128;
            if length > 0 {
                encoded_byte |= 128;
            }
            buffer.push(encoded_byte);
            if length == 0 {
                break;
            }
        }
    }

    fn encode(&self) -> Vec<u8> {
        let mut packet = Vec::new();
        let mut variable_header = Vec::new();
        let mut payload = Vec::new();

        // Variable header - Protocol name
        variable_header.push(0x00);
        variable_header.push(0x04);
        variable_header.extend_from_slice(b"MQTT");

        // Protocol level (4 = MQTT 3.1.1)
        variable_header.push(0x04);

        // Connect flags
        let mut flags = 0u8;
        if self.clean_session {
            flags |= 0x02;
        }
        if self.username.is_some() {
            flags |= 0x80;
        }
        if self.password.is_some() {
            flags |= 0x40;
        }
        variable_header.push(flags);

        // Keep alive
        variable_header.push((self.keep_alive >> 8) as u8);
        variable_header.push((self.keep_alive & 0xFF) as u8);

        // Payload - Client ID
        Self::encode_string(&mut payload, &self.client_id);

        // Username
        if let Some(ref username) = self.username {
            Self::encode_string(&mut payload, username);
        }

        // Password
        if let Some(ref password) = self.password {
            Self::encode_string(&mut payload, password);
        }

        // Fixed header
        packet.push(0x10); // CONNECT packet type
        let remaining_length = (variable_header.len() + payload.len()) as u32;
        Self::encode_remaining_length(&mut packet, remaining_length);

        // Combine all parts
        packet.extend_from_slice(&variable_header);
        packet.extend_from_slice(&payload);

        packet
    }
}

struct MqttDisconnectPacket;

impl MqttDisconnectPacket {
    fn encode() -> Vec<u8> {
        vec![0xE0, 0x00] // DISCONNECT packet type with 0 remaining length
    }
}

fn main() {
    // Create and encode CONNECT packet
    let connect = MqttConnectPacket::new("rust_client_001".to_string(), 60)
        .with_credentials("rust_user".to_string(), "rust_pass".to_string());

    let connect_packet = connect.encode();
    println!("CONNECT packet ({} bytes):", connect_packet.len());
    for (i, byte) in connect_packet.iter().enumerate() {
        print!("{:02X} ", byte);
        if (i + 1) % 16 == 0 {
            println!();
        }
    }
    println!("\n");

    // Create and encode DISCONNECT packet
    let disconnect_packet = MqttDisconnectPacket::encode();
    println!("DISCONNECT packet ({} bytes):", disconnect_packet.len());
    for byte in disconnect_packet.iter() {
        print!("{:02X} ", byte);
    }
    println!("\n");
}
```

### Advanced Rust with Last Will Testament

```rust
#[derive(Debug, Clone)]
struct WillMessage {
    topic: String,
    message: String,
    qos: u8,
    retain: bool,
}

#[derive(Debug)]
struct MqttConnectBuilder {
    client_id: String,
    username: Option<String>,
    password: Option<String>,
    will: Option<WillMessage>,
    keep_alive: u16,
    clean_session: bool,
}

impl MqttConnectBuilder {
    fn new(client_id: impl Into<String>) -> Self {
        Self {
            client_id: client_id.into(),
            username: None,
            password: None,
            will: None,
            keep_alive: 60,
            clean_session: true,
        }
    }

    fn credentials(mut self, username: impl Into<String>, password: impl Into<String>) -> Self {
        self.username = Some(username.into());
        self.password = Some(password.into());
        self
    }

    fn will_message(mut self, topic: impl Into<String>, message: impl Into<String>, 
                    qos: u8, retain: bool) -> Self {
        self.will = Some(WillMessage {
            topic: topic.into(),
            message: message.into(),
            qos,
            retain,
        });
        self
    }

    fn keep_alive(mut self, seconds: u16) -> Self {
        self.keep_alive = seconds;
        self
    }

    fn clean_session(mut self, clean: bool) -> Self {
        self.clean_session = clean;
        self
    }

    fn encode_string(buffer: &mut Vec<u8>, s: &str) {
        let len = s.len() as u16;
        buffer.extend_from_slice(&len.to_be_bytes());
        buffer.extend_from_slice(s.as_bytes());
    }

    fn encode_remaining_length(length: u32) -> Vec<u8> {
        let mut bytes = Vec::new();
        let mut len = length;
        loop {
            let mut byte = (len % 128) as u8;
            len /= 128;
            if len > 0 {
                byte |= 0x80;
            }
            bytes.push(byte);
            if len == 0 {
                break;
            }
        }
        bytes
    }

    fn build(self) -> Vec<u8> {
        let mut variable_header = Vec::new();
        let mut payload = Vec::new();

        // Protocol name and level
        variable_header.extend_from_slice(&[0x00, 0x04]);
        variable_header.extend_from_slice(b"MQTT");
        variable_header.push(0x04); // MQTT 3.1.1

        // Connect flags
        let mut flags = 0u8;
        if self.clean_session {
            flags |= 0x02;
        }
        if let Some(ref will) = self.will {
            flags |= 0x04; // Will flag
            flags |= (will.qos & 0x03) << 3;
            if will.retain {
                flags |= 0x20;
            }
        }
        if self.username.is_some() {
            flags |= 0x80;
        }
        if self.password.is_some() {
            flags |= 0x40;
        }
        variable_header.push(flags);

        // Keep alive
        variable_header.extend_from_slice(&self.keep_alive.to_be_bytes());

        // Payload - Client ID
        Self::encode_string(&mut payload, &self.client_id);

        // Will topic and message
        if let Some(will) = self.will {
            Self::encode_string(&mut payload, &will.topic);
            Self::encode_string(&mut payload, &will.message);
        }

        // Credentials
        if let Some(ref username) = self.username {
            Self::encode_string(&mut payload, username);
        }
        if let Some(ref password) = self.password {
            Self::encode_string(&mut payload, password);
        }

        // Assemble packet
        let mut packet = vec![0x10]; // CONNECT packet type
        let remaining_length = (variable_header.len() + payload.len()) as u32;
        packet.extend_from_slice(&Self::encode_remaining_length(remaining_length));
        packet.extend_from_slice(&variable_header);
        packet.extend_from_slice(&payload);

        packet
    }
}

fn main() {
    // Build comprehensive CONNECT packet
    let packet = MqttConnectBuilder::new("iot_device_rust_42")
        .credentials("device_user", "secure_token_xyz")
        .will_message("devices/iot_device_rust_42/status", "disconnected", 1, true)
        .keep_alive(120)
        .clean_session(true)
        .build();

    println!("Complete CONNECT packet ({} bytes):", packet.len());
    for (i, byte) in packet.iter().enumerate() {
        print!("{:02X} ", byte);
        if (i + 1) % 16 == 0 {
            println!();
        }
    }
    println!();
}
```

## Summary

**MQTT Connect and Disconnect packets** are essential for session lifecycle management in MQTT communications:

**Key Features of CONNECT Packet:**
- Establishes client identity through a unique Client ID
- Supports authentication via username/password
- Configures session behavior with Clean Session flag
- Implements Last Will and Testament (LWT) for ungraceful disconnections
- Sets Keep Alive interval for connection monitoring
- Contains protocol version negotiation

**Key Features of DISCONNECT Packet:**
- Provides graceful session termination
- Prevents Last Will message from being sent
- Simple structure (just 2 bytes in MQTT 3.1.1)
- Extended in MQTT 5.0 with reason codes and properties

**Implementation Considerations:**
- Variable byte integer encoding for remaining length
- String encoding with 2-byte length prefix
- Bit flag manipulation for connect flags
- Proper handling of optional fields (username, password, will)
- Session persistence based on Clean Session flag

Both C/C++ and Rust implementations demonstrate building these packets from scratch, showing the binary protocol structure and enabling developers to understand MQTT at the wire level. This knowledge is crucial for debugging, implementing custom MQTT clients, or working with constrained embedded systems where libraries may not be available.