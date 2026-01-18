# MQTT Payload Compression

## Overview

Payload compression in MQTT is a technique used to reduce the size of message payloads before transmission, thereby decreasing bandwidth consumption, reducing transmission costs, and improving performance in constrained network environments. This is particularly valuable in IoT scenarios where devices operate on limited bandwidth, cellular networks with data caps, or satellite communications.

## Why Compress MQTT Payloads?

1. **Bandwidth Reduction**: Compressed payloads can be 50-90% smaller depending on data type
2. **Cost Savings**: Lower data usage on metered connections (cellular, satellite)
3. **Faster Transmission**: Smaller payloads transmit quicker, reducing latency
4. **Battery Life**: Less radio time means lower power consumption on battery-powered devices
5. **Network Efficiency**: More messages can traverse the same network infrastructure

## Common Compression Algorithms

- **GZIP**: Good compression ratio, widely supported
- **ZLIB**: Similar to GZIP, more flexible
- **LZ4**: Fast compression/decompression, moderate ratio
- **Zstandard (Zstd)**: Excellent balance of speed and compression ratio
- **Snappy**: Very fast, lower compression ratio

## Implementation Considerations

### When to Compress
- Payloads larger than ~100-200 bytes
- Text/JSON data (highly compressible)
- Repetitive sensor data
- Log messages

### When NOT to Compress
- Already compressed data (images, video)
- Very small payloads (<50 bytes) - overhead exceeds benefit
- Random/encrypted data (low compressibility)
- CPU-constrained devices where compression time is critical

---

## C/C++ Implementation

### Using ZLIB for Compression

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "MQTTClient.h"

#define MQTT_ADDRESS    "tcp://localhost:1883"
#define CLIENTID        "CompressedPublisher"
#define TOPIC           "sensor/data/compressed"
#define QOS             1

// Compress data using ZLIB
int compress_payload(const char* source, size_t source_len, 
                     char** dest, size_t* dest_len) {
    uLongf compressed_len = compressBound(source_len);
    *dest = (char*)malloc(compressed_len);
    
    if (*dest == NULL) {
        return -1;
    }
    
    int result = compress2((Bytef*)*dest, &compressed_len,
                          (const Bytef*)source, source_len,
                          Z_BEST_COMPRESSION);
    
    if (result != Z_OK) {
        free(*dest);
        *dest = NULL;
        return -1;
    }
    
    *dest_len = compressed_len;
    return 0;
}

// Decompress data using ZLIB
int decompress_payload(const char* source, size_t source_len,
                       char* dest, size_t* dest_len) {
    uLongf uncompressed_len = *dest_len;
    
    int result = uncompress((Bytef*)dest, &uncompressed_len,
                           (const Bytef*)source, source_len);
    
    if (result != Z_OK) {
        return -1;
    }
    
    *dest_len = uncompressed_len;
    return 0;
}

// MQTT message callback for receiving compressed data
int message_arrived(void* context, char* topic, int topic_len,
                   MQTTClient_message* message) {
    printf("Received compressed message (%d bytes)\n", message->payloadlen);
    
    // Allocate buffer for decompressed data
    size_t decompressed_len = 8192; // Max expected size
    char* decompressed = (char*)malloc(decompressed_len);
    
    if (decompress_payload((char*)message->payload, message->payloadlen,
                          decompressed, &decompressed_len) == 0) {
        decompressed[decompressed_len] = '\0';
        printf("Decompressed (%zu bytes): %s\n", decompressed_len, decompressed);
    } else {
        printf("Decompression failed!\n");
    }
    
    free(decompressed);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    MQTTClient_create(&client, MQTT_ADDRESS, CLIENTID,
                     MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    MQTTClient_setCallbacks(client, NULL, NULL, message_arrived, NULL);
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect\n");
        return -1;
    }
    
    // Example: Compress and publish sensor data
    const char* sensor_data = 
        "{\"temperature\":22.5,\"humidity\":65.3,\"pressure\":1013.25,"
        "\"timestamp\":\"2026-01-18T10:30:00Z\",\"device_id\":\"sensor001\","
        "\"location\":\"Building A, Floor 2, Room 205\"}";
    
    size_t original_len = strlen(sensor_data);
    char* compressed = NULL;
    size_t compressed_len = 0;
    
    if (compress_payload(sensor_data, original_len, 
                        &compressed, &compressed_len) == 0) {
        
        printf("Original size: %zu bytes\n", original_len);
        printf("Compressed size: %zu bytes\n", compressed_len);
        printf("Compression ratio: %.2f%%\n", 
               (1.0 - (double)compressed_len / original_len) * 100);
        
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = compressed;
        pubmsg.payloadlen = compressed_len;
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        MQTTClient_deliveryToken token;
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        MQTTClient_waitForCompletion(client, token, 1000);
        
        printf("Message published\n");
        free(compressed);
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return 0;
}
```

### C++ with LZ4 (High-Speed Compression)

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <lz4.h>
#include <mqtt/client.h>

class CompressedMQTTClient {
private:
    mqtt::client client_;
    
    // Compress using LZ4
    std::vector<char> compress_lz4(const std::string& data) {
        int max_compressed_size = LZ4_compressBound(data.size());
        std::vector<char> compressed(max_compressed_size);
        
        int compressed_size = LZ4_compress_default(
            data.c_str(),
            compressed.data(),
            data.size(),
            max_compressed_size
        );
        
        if (compressed_size <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }
        
        compressed.resize(compressed_size);
        return compressed;
    }
    
    // Decompress using LZ4
    std::string decompress_lz4(const std::vector<char>& compressed, 
                               size_t original_size) {
        std::string decompressed(original_size, '\0');
        
        int result = LZ4_decompress_safe(
            compressed.data(),
            &decompressed[0],
            compressed.size(),
            original_size
        );
        
        if (result < 0) {
            throw std::runtime_error("LZ4 decompression failed");
        }
        
        return decompressed;
    }
    
public:
    CompressedMQTTClient(const std::string& address, 
                        const std::string& client_id)
        : client_(address, client_id) {}
    
    void connect() {
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        client_.connect(conn_opts);
    }
    
    void publish_compressed(const std::string& topic, 
                          const std::string& payload) {
        try {
            auto compressed = compress_lz4(payload);
            
            std::cout << "Original: " << payload.size() << " bytes\n";
            std::cout << "Compressed: " << compressed.size() << " bytes\n";
            std::cout << "Ratio: " << std::fixed << std::setprecision(2)
                     << (1.0 - (double)compressed.size() / payload.size()) * 100 
                     << "%\n";
            
            // Store original size in first 4 bytes for decompression
            std::vector<char> message(4 + compressed.size());
            uint32_t orig_size = payload.size();
            memcpy(message.data(), &orig_size, 4);
            memcpy(message.data() + 4, compressed.data(), compressed.size());
            
            mqtt::message_ptr pubmsg = mqtt::make_message(topic, message);
            pubmsg->set_qos(1);
            client_.publish(pubmsg);
            
        } catch (const std::exception& e) {
            std::cerr << "Publish error: " << e.what() << "\n";
        }
    }
    
    void disconnect() {
        client_.disconnect();
    }
};

int main() {
    CompressedMQTTClient client("tcp://localhost:1883", "CPPCompressor");
    
    try {
        client.connect();
        std::cout << "Connected to MQTT broker\n";
        
        std::string sensor_data = R"({
            "sensors": [
                {"id": "temp01", "value": 22.5, "unit": "celsius"},
                {"id": "hum01", "value": 65.3, "unit": "percent"},
                {"id": "press01", "value": 1013.25, "unit": "hPa"}
            ],
            "timestamp": "2026-01-18T10:30:00Z",
            "location": "Building A"
        })";
        
        client.publish_compressed("sensor/compressed", sensor_data);
        
        client.disconnect();
        
    } catch (const mqtt::exception& e) {
        std::cerr << "MQTT Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

---

## Rust Implementation

### Using Flate2 (GZIP/ZLIB)

```rust
use flate2::Compression;
use flate2::write::{GzEncoder, GzDecoder};
use std::io::Write;
use paho_mqtt as mqtt;
use std::time::Duration;

struct CompressedMqttClient {
    client: mqtt::Client,
}

impl CompressedMqttClient {
    fn new(broker: &str, client_id: &str) -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        Ok(Self { client })
    }
    
    fn compress_gzip(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        let mut encoder = GzEncoder::new(Vec::new(), Compression::best());
        encoder.write_all(data)?;
        encoder.finish()
    }
    
    fn decompress_gzip(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        let mut decoder = GzDecoder::new(Vec::new());
        decoder.write_all(data)?;
        decoder.finish()
    }
    
    fn connect(&self) -> Result<(), mqtt::Error> {
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(true)
            .finalize();
        
        self.client.connect(conn_opts)?;
        Ok(())
    }
    
    fn publish_compressed(&self, topic: &str, payload: &str) -> Result<(), Box<dyn std::error::Error>> {
        let original_size = payload.len();
        let compressed = self.compress_gzip(payload.as_bytes())?;
        let compressed_size = compressed.len();
        
        let ratio = (1.0 - (compressed_size as f64 / original_size as f64)) * 100.0;
        
        println!("Original size: {} bytes", original_size);
        println!("Compressed size: {} bytes", compressed_size);
        println!("Compression ratio: {:.2}%", ratio);
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(compressed)
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        Ok(())
    }
    
    fn subscribe_and_decompress(&self, topic: &str) -> Result<(), Box<dyn std::error::Error>> {
        self.client.subscribe(topic, 1)?;
        
        let rx = self.client.start_consuming();
        
        println!("Waiting for compressed messages on '{}'...", topic);
        
        for msg in rx.iter() {
            if let Some(msg) = msg {
                println!("Received compressed message: {} bytes", msg.payload().len());
                
                match self.decompress_gzip(msg.payload()) {
                    Ok(decompressed) => {
                        let text = String::from_utf8_lossy(&decompressed);
                        println!("Decompressed ({} bytes): {}", decompressed.len(), text);
                    }
                    Err(e) => println!("Decompression error: {}", e),
                }
            }
        }
        
        Ok(())
    }
    
    fn disconnect(&self) -> Result<(), mqtt::Error> {
        self.client.disconnect(None)?;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = CompressedMqttClient::new(
        "tcp://localhost:1883",
        "RustCompressor"
    )?;
    
    client.connect()?;
    println!("Connected to MQTT broker");
    
    let sensor_data = r#"{
        "device_id": "sensor_rust_001",
        "measurements": {
            "temperature": 23.7,
            "humidity": 58.2,
            "pressure": 1012.8,
            "light": 450
        },
        "metadata": {
            "timestamp": "2026-01-18T10:30:00Z",
            "location": "Lab Room 3",
            "firmware": "v2.1.5"
        }
    }"#;
    
    client.publish_compressed("sensor/data/compressed", sensor_data)?;
    
    client.disconnect()?;
    println!("Disconnected");
    
    Ok(())
}
```

### Using Zstd (Modern, High-Performance)

```rust
use zstd;
use paho_mqtt as mqtt;
use std::time::Duration;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug)]
struct SensorReading {
    device_id: String,
    temperature: f64,
    humidity: f64,
    pressure: f64,
    timestamp: String,
}

struct ZstdMqttClient {
    client: mqtt::Client,
    compression_level: i32,
}

impl ZstdMqttClient {
    fn new(broker: &str, client_id: &str, compression_level: i32) 
        -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        Ok(Self { client, compression_level })
    }
    
    fn compress_zstd(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        zstd::encode_all(data, self.compression_level)
    }
    
    fn decompress_zstd(&self, data: &[u8]) -> Result<Vec<u8>, std::io::Error> {
        zstd::decode_all(data)
    }
    
    fn connect(&self) -> Result<(), mqtt::Error> {
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(true)
            .finalize();
        
        self.client.connect(conn_opts)?;
        Ok(())
    }
    
    fn publish_json_compressed<T: Serialize>(
        &self, 
        topic: &str, 
        data: &T
    ) -> Result<(), Box<dyn std::error::Error>> {
        // Serialize to JSON
        let json = serde_json::to_string(data)?;
        let original_size = json.len();
        
        // Compress
        let compressed = self.compress_zstd(json.as_bytes())?;
        let compressed_size = compressed.len();
        
        let ratio = (1.0 - (compressed_size as f64 / original_size as f64)) * 100.0;
        
        println!("JSON size: {} bytes", original_size);
        println!("Compressed: {} bytes", compressed_size);
        println!("Saved: {:.2}%", ratio);
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(compressed)
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = ZstdMqttClient::new(
        "tcp://localhost:1883",
        "RustZstdClient",
        3  // Compression level (1-22, higher = better compression)
    )?;
    
    client.connect()?;
    
    let reading = SensorReading {
        device_id: "temp_sensor_042".to_string(),
        temperature: 24.3,
        humidity: 62.1,
        pressure: 1015.2,
        timestamp: "2026-01-18T10:30:00Z".to_string(),
    };
    
    client.publish_json_compressed("sensor/readings/compressed", &reading)?;
    
    Ok(())
}
```

---

## Summary

**MQTT Payload Compression** is a powerful optimization technique that reduces message sizes by 50-90%, leading to:

- **Bandwidth savings** on constrained or metered networks
- **Reduced costs** for cellular/satellite IoT deployments  
- **Improved performance** through faster transmission times
- **Extended battery life** for low-power devices

**Key Implementation Points:**

1. **Choose the right algorithm**: GZIP/ZLIB for general use, LZ4 for speed, Zstd for best balance
2. **Compress selectively**: Only compress payloads >100-200 bytes with compressible data (JSON, text, logs)
3. **Include metadata**: Store original size or use headers to facilitate decompression
4. **Handle errors gracefully**: Compression can fail; always validate before transmission
5. **Consider both sides**: Both publisher and subscriber must support compression/decompression
6. **Test compression ratios**: Measure actual savings for your specific data patterns

**Best Practices:**
- Use compression levels that balance CPU usage with size reduction
- Cache compressors/decompressors to avoid repeated initialization overhead
- Consider pre-shared compression dictionaries for highly repetitive data
- Monitor compression ratios to detect anomalies or inefficient configurations
- Document compression usage in topic naming or message properties for clarity

Payload compression is especially valuable in IoT, telemetry, and monitoring scenarios where message volumes are high and network resources are limited.