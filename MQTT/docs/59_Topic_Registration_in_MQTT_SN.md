# Topic Registration in MQTT-SN

## Overview

MQTT-SN (MQTT for Sensor Networks) is a variant of MQTT designed for wireless sensor networks and other constrained environments where bandwidth and device resources are limited. Topic registration is a key optimization that allows devices to use short, numeric topic IDs (2 bytes) instead of full topic name strings, significantly reducing message overhead.

## Key Concepts

### Why Topic Registration?

In standard MQTT, topic names are strings that can be quite long (e.g., "home/sensors/temperature/livingroom"). In constrained networks with limited bandwidth or devices with minimal memory, transmitting these strings repeatedly is inefficient. MQTT-SN solves this by:

1. **Mapping topic names to short IDs**: A 2-byte integer (0-65535) replaces the full string
2. **One-time registration**: The mapping happens once, then only the ID is transmitted
3. **Bandwidth savings**: Dramatically reduces packet size for publish/subscribe operations

### Topic ID Types

MQTT-SN supports three types of topic IDs:

- **Normal Topic ID** (2 bytes): Registered topic name, most common use case
- **Pre-defined Topic ID** (2 bytes): Statically configured IDs known to both client and gateway
- **Short Topic Name** (2 bytes): The topic name itself is exactly 2 characters

## Registration Procedures

### Registration Flow

1. **Client-initiated**: Client sends REGISTER message with topic name
2. **Gateway assigns ID**: Gateway responds with REGACK containing the assigned topic ID
3. **Subsequent use**: Client uses the short topic ID for PUBLISH/SUBSCRIBE

Alternatively, the gateway can initiate registration when it receives a PUBLISH for an unregistered topic.

## C/C++ Code Examples

### Basic Registration (Client)

```c
#include <stdint.h>
#include <string.h>

// MQTT-SN message types
#define MQTTSN_REGISTER     0x0A
#define MQTTSN_REGACK       0x0B
#define MQTTSN_PUBLISH      0x0C

// Structure for REGISTER message
typedef struct {
    uint8_t length;
    uint8_t msg_type;
    uint16_t topic_id;
    uint16_t msg_id;
    char topic_name[256];
} mqttsn_register_msg_t;

// Structure for REGACK message
typedef struct {
    uint8_t length;
    uint8_t msg_type;
    uint16_t topic_id;
    uint16_t msg_id;
    uint8_t return_code;
} mqttsn_regack_msg_t;

// Function to create a REGISTER message
int create_register_message(uint8_t *buffer, const char *topic_name, uint16_t msg_id) {
    int topic_len = strlen(topic_name);
    int msg_len = 6 + topic_len; // 1(len) + 1(type) + 2(topic_id) + 2(msg_id) + topic_name
    
    buffer[0] = msg_len;
    buffer[1] = MQTTSN_REGISTER;
    buffer[2] = 0x00; // Topic ID = 0x0000 for registration
    buffer[3] = 0x00;
    buffer[4] = (msg_id >> 8) & 0xFF;
    buffer[5] = msg_id & 0xFF;
    memcpy(&buffer[6], topic_name, topic_len);
    
    return msg_len;
}

// Function to parse REGACK response
int parse_regack(const uint8_t *buffer, uint16_t *assigned_topic_id, uint8_t *return_code) {
    if (buffer[1] != MQTTSN_REGACK) {
        return -1; // Not a REGACK message
    }
    
    *assigned_topic_id = (buffer[2] << 8) | buffer[3];
    *return_code = buffer[6];
    
    return (*return_code == 0) ? 0 : -1; // 0 = success
}

// Example usage
void register_topic_example() {
    uint8_t send_buffer[256];
    uint8_t recv_buffer[256];
    uint16_t topic_id;
    uint8_t return_code;
    
    // Create and send REGISTER message
    const char *topic = "sensors/temperature";
    int len = create_register_message(send_buffer, topic, 1234);
    // send_udp(send_buffer, len); // Send via UDP
    
    // Receive and parse REGACK
    // recv_udp(recv_buffer, sizeof(recv_buffer));
    if (parse_regack(recv_buffer, &topic_id, &return_code) == 0) {
        // Registration successful, use topic_id for future PUBLISH
        printf("Topic registered with ID: %u\n", topic_id);
    }
}
```

### Publishing with Registered Topic ID

```c
// Structure for PUBLISH message with normal topic ID
typedef struct {
    uint8_t length;
    uint8_t msg_type;
    uint8_t flags;
    uint16_t topic_id;
    uint16_t msg_id;
    uint8_t data[256];
} mqttsn_publish_msg_t;

#define MQTTSN_FLAG_TOPIC_TYPE_NORMAL  0x00
#define MQTTSN_FLAG_QOS_0              0x00
#define MQTTSN_FLAG_QOS_1              0x20

// Publish using registered topic ID
int publish_with_topic_id(uint8_t *buffer, uint16_t topic_id, 
                          const uint8_t *data, int data_len, uint16_t msg_id) {
    int msg_len = 7 + data_len;
    
    buffer[0] = msg_len;
    buffer[1] = MQTTSN_PUBLISH;
    buffer[2] = MQTTSN_FLAG_TOPIC_TYPE_NORMAL | MQTTSN_FLAG_QOS_0;
    buffer[3] = (topic_id >> 8) & 0xFF;
    buffer[4] = topic_id & 0xFF;
    buffer[5] = (msg_id >> 8) & 0xFF;
    buffer[6] = msg_id & 0xFF;
    memcpy(&buffer[7], data, data_len);
    
    return msg_len;
}
```

## Rust Code Examples

### Registration with Type-Safe Structures

```rust
use std::io::{self, Write, Read};
use std::net::UdpSocket;

// MQTT-SN message types
const MQTTSN_REGISTER: u8 = 0x0A;
const MQTTSN_REGACK: u8 = 0x0B;
const MQTTSN_PUBLISH: u8 = 0x0C;

// Return codes
const ACCEPTED: u8 = 0x00;
const REJECTED_CONGESTION: u8 = 0x01;
const REJECTED_INVALID_TOPIC_ID: u8 = 0x02;

#[derive(Debug, Clone)]
pub struct RegisterMessage {
    pub topic_id: u16,
    pub msg_id: u16,
    pub topic_name: String,
}

impl RegisterMessage {
    pub fn new(topic_name: String, msg_id: u16) -> Self {
        RegisterMessage {
            topic_id: 0, // 0x0000 for new registration
            msg_id,
            topic_name,
        }
    }
    
    pub fn encode(&self) -> Vec<u8> {
        let topic_bytes = self.topic_name.as_bytes();
        let length = 6 + topic_bytes.len();
        
        let mut buffer = Vec::with_capacity(length);
        buffer.push(length as u8);
        buffer.push(MQTTSN_REGISTER);
        buffer.extend_from_slice(&self.topic_id.to_be_bytes());
        buffer.extend_from_slice(&self.msg_id.to_be_bytes());
        buffer.extend_from_slice(topic_bytes);
        
        buffer
    }
}

#[derive(Debug, Clone)]
pub struct RegAckMessage {
    pub topic_id: u16,
    pub msg_id: u16,
    pub return_code: u8,
}

impl RegAckMessage {
    pub fn decode(buffer: &[u8]) -> Result<Self, &'static str> {
        if buffer.len() < 7 {
            return Err("Buffer too short");
        }
        if buffer[1] != MQTTSN_REGACK {
            return Err("Not a REGACK message");
        }
        
        Ok(RegAckMessage {
            topic_id: u16::from_be_bytes([buffer[2], buffer[3]]),
            msg_id: u16::from_be_bytes([buffer[4], buffer[5]]),
            return_code: buffer[6],
        })
    }
    
    pub fn is_accepted(&self) -> bool {
        self.return_code == ACCEPTED
    }
}

#[derive(Debug, Clone)]
pub struct PublishMessage {
    pub flags: u8,
    pub topic_id: u16,
    pub msg_id: u16,
    pub data: Vec<u8>,
}

impl PublishMessage {
    pub fn new(topic_id: u16, data: Vec<u8>, msg_id: u16, qos: u8) -> Self {
        let flags = (qos << 5) & 0x60; // QoS in bits 5-6
        
        PublishMessage {
            flags,
            topic_id,
            msg_id,
            data,
        }
    }
    
    pub fn encode(&self) -> Vec<u8> {
        let length = 7 + self.data.len();
        
        let mut buffer = Vec::with_capacity(length);
        buffer.push(length as u8);
        buffer.push(MQTTSN_PUBLISH);
        buffer.push(self.flags);
        buffer.extend_from_slice(&self.topic_id.to_be_bytes());
        buffer.extend_from_slice(&self.msg_id.to_be_bytes());
        buffer.extend_from_slice(&self.data);
        
        buffer
    }
}

// Client implementation
pub struct MqttSnClient {
    socket: UdpSocket,
    msg_id_counter: u16,
}

impl MqttSnClient {
    pub fn new(local_addr: &str, gateway_addr: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind(local_addr)?;
        socket.connect(gateway_addr)?;
        
        Ok(MqttSnClient {
            socket,
            msg_id_counter: 1,
        })
    }
    
    pub fn register_topic(&mut self, topic_name: &str) -> io::Result<u16> {
        let msg_id = self.next_msg_id();
        let register_msg = RegisterMessage::new(topic_name.to_string(), msg_id);
        
        // Send REGISTER
        self.socket.send(&register_msg.encode())?;
        
        // Wait for REGACK
        let mut buffer = [0u8; 256];
        let len = self.socket.recv(&mut buffer)?;
        
        let regack = RegAckMessage::decode(&buffer[..len])
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        
        if !regack.is_accepted() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Registration rejected with code: {}", regack.return_code)
            ));
        }
        
        Ok(regack.topic_id)
    }
    
    pub fn publish(&mut self, topic_id: u16, data: &[u8], qos: u8) -> io::Result<()> {
        let msg_id = self.next_msg_id();
        let publish_msg = PublishMessage::new(topic_id, data.to_vec(), msg_id, qos);
        
        self.socket.send(&publish_msg.encode())?;
        Ok(())
    }
    
    fn next_msg_id(&mut self) -> u16 {
        let id = self.msg_id_counter;
        self.msg_id_counter = self.msg_id_counter.wrapping_add(1);
        if self.msg_id_counter == 0 {
            self.msg_id_counter = 1;
        }
        id
    }
}

// Example usage
fn example_usage() -> io::Result<()> {
    let mut client = MqttSnClient::new("0.0.0.0:0", "192.168.1.100:1883")?;
    
    // Register topic and get topic ID
    let topic_id = client.register_topic("home/sensors/temperature")?;
    println!("Topic registered with ID: {}", topic_id);
    
    // Publish using the topic ID
    let temperature_data = b"23.5";
    client.publish(topic_id, temperature_data, 0)?;
    
    Ok(())
}
```

### Advanced: Topic ID Cache

```rust
use std::collections::HashMap;

pub struct TopicRegistry {
    name_to_id: HashMap<String, u16>,
    id_to_name: HashMap<u16, String>,
}

impl TopicRegistry {
    pub fn new() -> Self {
        TopicRegistry {
            name_to_id: HashMap::new(),
            id_to_name: HashMap::new(),
        }
    }
    
    pub fn register(&mut self, topic_name: String, topic_id: u16) {
        self.name_to_id.insert(topic_name.clone(), topic_id);
        self.id_to_name.insert(topic_id, topic_name);
    }
    
    pub fn get_id(&self, topic_name: &str) -> Option<u16> {
        self.name_to_id.get(topic_name).copied()
    }
    
    pub fn get_name(&self, topic_id: u16) -> Option<&str> {
        self.id_to_name.get(&topic_id).map(|s| s.as_str())
    }
    
    pub fn is_registered(&self, topic_name: &str) -> bool {
        self.name_to_id.contains_key(topic_name)
    }
}

// Enhanced client with topic caching
pub struct MqttSnClientWithCache {
    client: MqttSnClient,
    registry: TopicRegistry,
}

impl MqttSnClientWithCache {
    pub fn new(local_addr: &str, gateway_addr: &str) -> io::Result<Self> {
        Ok(MqttSnClientWithCache {
            client: MqttSnClient::new(local_addr, gateway_addr)?,
            registry: TopicRegistry::new(),
        })
    }
    
    pub fn publish_by_name(&mut self, topic_name: &str, data: &[u8], qos: u8) -> io::Result<()> {
        // Check if topic is already registered
        let topic_id = if let Some(id) = self.registry.get_id(topic_name) {
            id
        } else {
            // Register topic if not in cache
            let id = self.client.register_topic(topic_name)?;
            self.registry.register(topic_name.to_string(), id);
            id
        };
        
        self.client.publish(topic_id, data, qos)
    }
}
```

## Summary

Topic registration in MQTT-SN is a bandwidth optimization technique that maps long topic name strings to 2-byte numeric IDs. This is essential for constrained devices and networks where every byte counts.

**Key benefits:**
- **Reduced overhead**: 2 bytes instead of potentially dozens for topic names
- **Memory efficiency**: Constrained devices store small integers instead of strings
- **One-time cost**: Registration happens once, then all messages use the short ID

**Registration process:**
1. Client sends REGISTER with topic name and message ID
2. Gateway responds with REGACK containing assigned topic ID and return code
3. Client caches the mapping for future use
4. All subsequent PUBLISH/SUBSCRIBE operations use the 2-byte topic ID

**Best practices:**
- Cache topic ID mappings to avoid re-registration
- Handle registration failures gracefully with retry logic
- Use pre-defined topic IDs when both client and gateway can be configured statically
- Consider short topic names (2 characters) for ultra-constrained scenarios

The code examples demonstrate complete registration flows in both C/C++ and Rust, including message encoding/decoding, client implementations, and topic ID caching strategies for production use.