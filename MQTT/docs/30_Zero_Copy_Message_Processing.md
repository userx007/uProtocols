# Zero-Copy Message Processing in MQTT

## Overview

Zero-copy message processing is an optimization technique that eliminates unnecessary data copying during message handling. In traditional message processing, data is often copied multiple times as it moves through different layers of the system (network buffer → parser → application buffer). Zero-copy techniques allow direct access to the original data buffers, significantly reducing memory overhead and improving performance.

This is particularly valuable in MQTT implementations where:
- High message throughput is required
- Memory is constrained (IoT devices)
- Low latency is critical
- Large payloads are transmitted

## Core Concepts

**Traditional Approach (Multiple Copies):**
```
Network Buffer → Parser Buffer → Application Buffer
```

**Zero-Copy Approach:**
```
Network Buffer → Direct Reference/View → Application
```

**Key Benefits:**
- Reduced memory allocation/deallocation
- Lower CPU usage
- Improved cache locality
- Better throughput and latency

## C/C++ Implementation

In C/C++, zero-copy is achieved through careful pointer management and buffer ownership semantics.

### Basic Zero-Copy Message Handler

```c
#include <stdint.h>
#include <string.h>
#include <mosquitto.h>

// Message view structure - doesn't own the data
typedef struct {
    const char* topic;
    const uint8_t* payload;
    size_t payload_len;
    int qos;
    bool retain;
} mqtt_message_view_t;

// Zero-copy callback - operates directly on mosquitto's buffers
void on_message_zerocopy(struct mosquitto *mosq, void *userdata, 
                         const struct mosquitto_message *msg) {
    // Create a view without copying
    mqtt_message_view_t view = {
        .topic = msg->topic,           // Points to mosquitto's buffer
        .payload = msg->payload,       // Points to mosquitto's buffer
        .payload_len = msg->payloadlen,
        .qos = msg->qos,
        .retain = msg->retain
    };
    
    // Process message using the view
    process_message_view(&view);
    
    // No cleanup needed - we don't own the data
}

// Processing function that works with views
void process_message_view(const mqtt_message_view_t* view) {
    // Example: Parse JSON payload without copying
    if (view->payload_len > 0) {
        // Work directly with the payload buffer
        // Note: payload is NOT null-terminated
        
        // For string operations, use length-aware functions
        if (view->payload_len >= 4 && 
            memcmp(view->payload, "CMD:", 4) == 0) {
            // Process command directly from buffer
            handle_command(view->payload + 4, view->payload_len - 4);
        }
    }
}
```

### Advanced Zero-Copy with Buffer Pooling

```cpp
#include <memory>
#include <vector>
#include <string_view>
#include <mosquitto.h>

class ZeroCopyMQTTHandler {
private:
    // Buffer pool to avoid allocations
    struct BufferPool {
        std::vector<std::unique_ptr<uint8_t[]>> buffers;
        size_t buffer_size;
        
        BufferPool(size_t size = 4096, size_t count = 10) 
            : buffer_size(size) {
            for (size_t i = 0; i < count; ++i) {
                buffers.push_back(std::make_unique<uint8_t[]>(size));
            }
        }
    };
    
    BufferPool pool;

public:
    // Message view using C++17 string_view (zero-copy)
    struct MessageView {
        std::string_view topic;
        std::string_view payload;
        int qos;
        bool retain;
    };
    
    void onMessage(const mosquitto_message* msg) {
        // Create zero-copy views
        MessageView view{
            std::string_view(msg->topic),
            std::string_view(
                reinterpret_cast<const char*>(msg->payload), 
                msg->payloadlen
            ),
            msg->qos,
            msg->retain
        };
        
        // Route based on topic without copying
        if (view.topic.starts_with("sensor/")) {
            handleSensorData(view);
        } else if (view.topic.starts_with("command/")) {
            handleCommand(view);
        }
    }
    
private:
    void handleSensorData(const MessageView& view) {
        // Parse JSON directly from view
        // Using a zero-copy JSON parser like simdjson
        // simdjson::padded_string_view json(view.payload);
        // auto doc = parser.iterate(json);
        
        // For demonstration, simple parsing:
        if (view.payload.find("temperature") != std::string_view::npos) {
            // Extract value without copying the entire payload
            extractTemperature(view.payload);
        }
    }
    
    void extractTemperature(std::string_view json) {
        // Find and extract value directly from view
        size_t pos = json.find("\"temperature\":");
        if (pos != std::string_view::npos) {
            pos += 14; // Skip past key
            // Parse number directly from string_view
            // strtod or custom parsing
        }
    }
    
    void handleCommand(const MessageView& view) {
        // Command processing without copying
        auto cmd_view = view.payload;
        
        if (cmd_view == "RESTART") {
            // Handle restart
        } else if (cmd_view.starts_with("SET_")) {
            // Handle configuration
        }
    }
};
```

### Memory-Mapped File Integration

```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    void* mapped_region;
    size_t size;
    int fd;
} mmap_buffer_t;

// Zero-copy persistent storage
int store_message_zerocopy(const mqtt_message_view_t* msg, 
                          mmap_buffer_t* storage) {
    // Write directly to memory-mapped region (zero-copy to disk)
    size_t offset = 0;
    
    // Write topic length and topic
    uint16_t topic_len = strlen(msg->topic);
    memcpy((uint8_t*)storage->mapped_region + offset, 
           &topic_len, sizeof(topic_len));
    offset += sizeof(topic_len);
    
    memcpy((uint8_t*)storage->mapped_region + offset, 
           msg->topic, topic_len);
    offset += topic_len;
    
    // Write payload
    memcpy((uint8_t*)storage->mapped_region + offset, 
           &msg->payload_len, sizeof(msg->payload_len));
    offset += sizeof(msg->payload_len);
    
    memcpy((uint8_t*)storage->mapped_region + offset, 
           msg->payload, msg->payload_len);
    
    // Sync to disk (still zero-copy)
    msync(storage->mapped_region, offset + msg->payload_len, MS_ASYNC);
    
    return 0;
}
```

## Rust Implementation

Rust's ownership system makes zero-copy processing safer and more explicit. The type system prevents many common errors like use-after-free and data races.

### Basic Zero-Copy with Borrowed Slices

```rust
use std::str;

// Message view using borrowed data (zero-copy)
#[derive(Debug)]
pub struct MqttMessageView<'a> {
    pub topic: &'a str,
    pub payload: &'a [u8],
    pub qos: u8,
    pub retain: bool,
}

impl<'a> MqttMessageView<'a> {
    // Create a view from raw parts
    pub fn new(topic: &'a str, payload: &'a [u8], qos: u8, retain: bool) -> Self {
        Self {
            topic,
            payload,
            qos,
            retain,
        }
    }
    
    // Get payload as string without copying
    pub fn payload_str(&self) -> Result<&'a str, str::Utf8Error> {
        str::from_utf8(self.payload)
    }
    
    // Check topic prefix without allocating
    pub fn topic_starts_with(&self, prefix: &str) -> bool {
        self.topic.starts_with(prefix)
    }
}

// Zero-copy message handler
pub fn handle_message(view: MqttMessageView) {
    // Pattern match on topic without copying
    match view.topic {
        t if t.starts_with("sensor/") => handle_sensor(view),
        t if t.starts_with("command/") => handle_command(view),
        _ => handle_default(view),
    }
}

fn handle_sensor(view: MqttMessageView) {
    // Parse directly from slice
    if let Ok(payload_str) = view.payload_str() {
        // Zero-copy JSON parsing with serde_json's from_str
        // or even better, use simd-json for true zero-copy
        println!("Sensor data: {}", payload_str);
    }
}

fn handle_command(view: MqttMessageView) {
    // Work with bytes directly
    if view.payload.starts_with(b"SET_") {
        // Extract value without copying
        let value_slice = &view.payload[4..];
        process_setting(value_slice);
    }
}

fn process_setting(value: &[u8]) {
    // Process without allocating
    println!("Setting value: {} bytes", value.len());
}

fn handle_default(view: MqttMessageView) {
    println!("Unknown topic: {}", view.topic);
}
```

### Advanced Zero-Copy with Rumqttc

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use bytes::Bytes;

pub struct ZeroCopyHandler {
    // Configuration
    max_payload_size: usize,
}

impl ZeroCopyHandler {
    pub fn new(max_payload_size: usize) -> Self {
        Self { max_payload_size }
    }
    
    // Process messages with zero-copy views
    pub fn process_event(&self, event: Event) {
        if let Event::Incoming(Packet::Publish(publish)) = event {
            // rumqttc uses Bytes which supports zero-copy slicing
            let view = MqttMessageView {
                topic: &publish.topic,
                payload: &publish.payload,
                qos: publish.qos as u8,
                retain: publish.retain,
            };
            
            self.handle_message_view(view);
        }
    }
    
    fn handle_message_view(&self, view: MqttMessageView) {
        // Route without copying
        if view.topic.starts_with("telemetry/") {
            self.process_telemetry(view);
        } else if view.topic.starts_with("events/") {
            self.process_event_msg(view);
        }
    }
    
    fn process_telemetry(&self, view: MqttMessageView) {
        // Parse binary protocol without copying
        if view.payload.len() >= 8 {
            // Read values directly from slice
            let timestamp = u64::from_le_bytes(
                view.payload[0..8].try_into().unwrap()
            );
            
            // Process remaining data as a view
            let data_slice = &view.payload[8..];
            self.process_sensor_data(timestamp, data_slice);
        }
    }
    
    fn process_sensor_data(&self, timestamp: u64, data: &[u8]) {
        println!("Sensor data at {}: {} bytes", timestamp, data.len());
        // Further processing without copying
    }
    
    fn process_event_msg(&self, view: MqttMessageView) {
        // JSON parsing with zero-copy using simd-json
        // or direct deserialization
        println!("Event on {}: {} bytes", view.topic, view.payload.len());
    }
}

// Example usage
pub fn run_zerocopy_client() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("zerocopy_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(5));
    
    let (client, mut connection) = Client::new(mqttoptions, 10);
    let handler = ZeroCopyHandler::new(1024 * 1024); // 1MB max
    
    // Subscribe
    client.subscribe("telemetry/#", QoS::AtLeastOnce)?;
    client.subscribe("events/#", QoS::AtLeastOnce)?;
    
    // Event loop with zero-copy processing
    for notification in connection.iter() {
        if let Ok(event) = notification {
            handler.process_event(event);
        }
    }
    
    Ok(())
}
```

### Zero-Copy with Bytes and Custom Protocols

```rust
use bytes::{Bytes, BytesMut, Buf, BufMut};

pub struct BinaryMessage {
    // Bytes supports cheap cloning and slicing (reference counting)
    data: Bytes,
}

impl BinaryMessage {
    pub fn new(data: Bytes) -> Self {
        Self { data }
    }
    
    // Get a zero-copy slice
    pub fn get_header(&self) -> Bytes {
        self.data.slice(0..16) // Creates a view, no copy
    }
    
    // Get payload without copying
    pub fn get_payload(&self) -> Bytes {
        self.data.slice(16..) // View into the rest
    }
    
    // Parse header fields directly
    pub fn message_type(&self) -> u8 {
        self.data[0]
    }
    
    pub fn message_id(&self) -> u32 {
        let mut slice = &self.data[1..5];
        slice.get_u32()
    }
}

// Zero-copy protocol parser
pub struct ProtocolParser;

impl ProtocolParser {
    pub fn parse(payload: &[u8]) -> Result<ParsedMessage, ParseError> {
        // Parse without allocating
        if payload.len() < 4 {
            return Err(ParseError::TooShort);
        }
        
        let msg_type = payload[0];
        let length = u16::from_be_bytes([payload[1], payload[2]]) as usize;
        
        if payload.len() < 3 + length {
            return Err(ParseError::Incomplete);
        }
        
        // Create view into the data section
        let data = &payload[3..3 + length];
        
        Ok(ParsedMessage {
            msg_type,
            data, // Just a reference, no copy
        })
    }
}

#[derive(Debug)]
pub struct ParsedMessage<'a> {
    pub msg_type: u8,
    pub data: &'a [u8],
}

#[derive(Debug)]
pub enum ParseError {
    TooShort,
    Incomplete,
}

// Usage example
pub fn process_mqtt_payload(payload: &[u8]) {
    match ProtocolParser::parse(payload) {
        Ok(msg) => {
            // Process without copying
            println!("Type: {}, Data len: {}", msg.msg_type, msg.data.len());
            
            // Can pass the view to other functions
            handle_parsed_data(msg.data);
        }
        Err(e) => eprintln!("Parse error: {:?}", e),
    }
}

fn handle_parsed_data(data: &[u8]) {
    // Work directly with the data
    println!("Processing {} bytes", data.len());
}
```

## Summary

**Zero-Copy Message Processing** is a performance optimization technique that eliminates unnecessary data copying by working directly with original buffer references instead of creating duplicates.

**Key Takeaways:**

1. **C/C++ Approach**: Requires careful manual pointer management and buffer ownership tracking. Uses raw pointers, `string_view`, and buffer pooling to avoid copies.

2. **Rust Approach**: Leverages the ownership system and lifetimes to make zero-copy safe by design. Uses borrowed slices (`&[u8]`, `&str`) and smart reference-counted types like `Bytes`.

3. **Benefits**: Reduced memory allocation, lower CPU usage, improved throughput, and better cache performance—critical for high-throughput MQTT applications and resource-constrained IoT devices.

4. **Trade-offs**: Increased complexity in buffer lifetime management, potential constraints on data processing patterns, and the need for careful API design to maintain zero-copy semantics.

5. **Best Practices**: Use buffer pooling to reduce allocations, leverage memory-mapped I/O for persistence, employ length-aware string operations, and choose parsers that support zero-copy (like `simd-json` or `simdjson`).

Zero-copy techniques are essential for building high-performance MQTT applications, particularly in scenarios with large message volumes, constrained resources, or strict latency requirements.