# MQTT Message Transformation

## Detailed Description

**Message Transformation** in MQTT refers to the process of converting, modifying, or enriching messages as they flow through the MQTT broker or client applications. This capability is essential in heterogeneous IoT ecosystems where devices and applications may use different data formats, protocols, or require additional context to be added to messages.

### Key Aspects of Message Transformation

**1. Format Conversion**
- Converting between data formats (JSON ↔ XML ↔ Binary ↔ Protocol Buffers)
- Encoding/decoding transformations (Base64, hex encoding)
- Character set conversions (UTF-8, ASCII, etc.)

**2. Protocol Translation**
- Bridging between MQTT and other protocols (HTTP, CoAP, AMQP, WebSockets)
- Converting proprietary device protocols to standard formats
- Adapting between different MQTT versions (3.1.1 ↔ 5.0)

**3. Data Enrichment**
- Adding metadata (timestamps, device IDs, location data)
- Injecting contextual information from external sources
- Calculating derived values or aggregations
- Adding quality-of-service markers or priority levels

**4. Common Use Cases**
- Legacy device integration with modern systems
- Multi-tenant architectures requiring data isolation
- Analytics pipelines needing standardized formats
- Security implementations requiring encryption/decryption
- Data normalization across heterogeneous device fleets

## C/C++ Implementation

```cpp
#include <mosquitto.h>
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Structure to hold transformation context
typedef struct {
    struct mosquitto *mosq;
    char output_topic[256];
} transform_context_t;

// Transform JSON to enriched JSON with metadata
char* transform_json_message(const char* input_json, const char* device_id) {
    // Parse input JSON
    struct json_object *input_obj = json_tokener_parse(input_json);
    if (!input_obj) {
        fprintf(stderr, "Failed to parse JSON\n");
        return NULL;
    }
    
    // Create enriched output object
    struct json_object *output_obj = json_object_new_object();
    
    // Add timestamp
    time_t now = time(NULL);
    json_object_object_add(output_obj, "timestamp", 
                          json_object_new_int64(now));
    
    // Add device ID
    json_object_object_add(output_obj, "device_id", 
                          json_object_new_string(device_id));
    
    // Copy original data under "payload" key
    json_object_object_add(output_obj, "payload", input_obj);
    
    // Convert to string
    const char* output_str = json_object_to_json_string(output_obj);
    char* result = strdup(output_str);
    
    json_object_put(output_obj);
    return result;
}

// Convert binary sensor data to JSON
char* transform_binary_to_json(const uint8_t* data, int len) {
    struct json_object *obj = json_object_new_object();
    
    // Example: First 2 bytes = temperature (in 0.1°C units)
    if (len >= 2) {
        int16_t temp_raw = (data[0] << 8) | data[1];
        double temp = temp_raw / 10.0;
        json_object_object_add(obj, "temperature", 
                              json_object_new_double(temp));
    }
    
    // Next 2 bytes = humidity (in 0.1% units)
    if (len >= 4) {
        int16_t hum_raw = (data[2] << 8) | data[3];
        double humidity = hum_raw / 10.0;
        json_object_object_add(obj, "humidity", 
                              json_object_new_double(humidity));
    }
    
    const char* json_str = json_object_to_json_string(obj);
    char* result = strdup(json_str);
    
    json_object_put(obj);
    return result;
}

// Callback for incoming messages
void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    transform_context_t *ctx = (transform_context_t*)obj;
    
    printf("Received message on topic: %s\n", msg->topic);
    
    char* transformed_payload = NULL;
    
    // Check topic to determine transformation type
    if (strstr(msg->topic, "/raw/binary") != NULL) {
        // Binary to JSON transformation
        transformed_payload = transform_binary_to_json(
            (uint8_t*)msg->payload, msg->payloadlen);
        
    } else if (strstr(msg->topic, "/raw/json") != NULL) {
        // JSON enrichment
        char device_id[64];
        sscanf(msg->topic, "devices/%[^/]", device_id);
        transformed_payload = transform_json_message(
            (char*)msg->payload, device_id);
    }
    
    // Publish transformed message
    if (transformed_payload) {
        mosquitto_publish(mosq, NULL, ctx->output_topic, 
                         strlen(transformed_payload), 
                         transformed_payload, 0, false);
        printf("Published transformed message to: %s\n", 
               ctx->output_topic);
        free(transformed_payload);
    }
}

int main() {
    mosquitto_lib_init();
    
    transform_context_t ctx;
    strcpy(ctx.output_topic, "devices/transformed");
    
    ctx.mosq = mosquitto_new("transformer", true, &ctx);
    
    mosquitto_message_callback_set(ctx.mosq, on_message);
    
    mosquitto_connect(ctx.mosq, "localhost", 1883, 60);
    
    // Subscribe to topics requiring transformation
    mosquitto_subscribe(ctx.mosq, NULL, "devices/+/raw/#", 0);
    
    printf("Message transformer started...\n");
    mosquitto_loop_forever(ctx.mosq, -1, 1);
    
    mosquitto_destroy(ctx.mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

## Rust Implementation

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use chrono::Utc;
use std::time::Duration;
use std::thread;

// Input data structure (binary sensor format)
#[derive(Debug)]
struct BinarySensorData {
    temperature: f32,
    humidity: f32,
}

// Enriched output structure
#[derive(Debug, Serialize, Deserialize)]
struct EnrichedMessage {
    timestamp: i64,
    device_id: String,
    payload: Value,
    metadata: MessageMetadata,
}

#[derive(Debug, Serialize, Deserialize)]
struct MessageMetadata {
    source: String,
    transformation_version: String,
    qos: u8,
}

// Message transformer trait
trait MessageTransformer {
    fn transform(&self, input: &[u8], topic: &str) -> Result<String, String>;
}

// Binary to JSON transformer
struct BinaryToJsonTransformer;

impl MessageTransformer for BinaryToJsonTransformer {
    fn transform(&self, input: &[u8], _topic: &str) -> Result<String, String> {
        if input.len() < 4 {
            return Err("Insufficient data".to_string());
        }
        
        // Parse binary data (big-endian)
        let temp_raw = i16::from_be_bytes([input[0], input[1]]);
        let hum_raw = i16::from_be_bytes([input[2], input[3]]);
        
        let sensor_data = json!({
            "temperature": temp_raw as f32 / 10.0,
            "humidity": hum_raw as f32 / 10.0,
            "unit_temp": "celsius",
            "unit_humidity": "percent"
        });
        
        Ok(sensor_data.to_string())
    }
}

// JSON enrichment transformer
struct JsonEnrichmentTransformer;

impl JsonEnrichmentTransformer {
    fn extract_device_id(topic: &str) -> String {
        topic.split('/')
            .nth(1)
            .unwrap_or("unknown")
            .to_string()
    }
}

impl MessageTransformer for JsonEnrichmentTransformer {
    fn transform(&self, input: &[u8], topic: &str) -> Result<String, String> {
        // Parse input JSON
        let input_str = std::str::from_utf8(input)
            .map_err(|e| format!("UTF-8 error: {}", e))?;
        
        let payload: Value = serde_json::from_str(input_str)
            .map_err(|e| format!("JSON parse error: {}", e))?;
        
        // Create enriched message
        let enriched = EnrichedMessage {
            timestamp: Utc::now().timestamp(),
            device_id: Self::extract_device_id(topic),
            payload,
            metadata: MessageMetadata {
                source: "mqtt_transformer".to_string(),
                transformation_version: "1.0.0".to_string(),
                qos: 0,
            },
        };
        
        serde_json::to_string(&enriched)
            .map_err(|e| format!("Serialization error: {}", e))
    }
}

// Protocol converter: MQTT to HTTP (conceptual)
struct MqttToHttpBridge {
    http_endpoint: String,
}

impl MqttToHttpBridge {
    fn new(endpoint: String) -> Self {
        Self { http_endpoint: endpoint }
    }
    
    async fn forward_to_http(&self, payload: &str) -> Result<(), String> {
        // In a real implementation, use reqwest or similar
        println!("Would POST to {}: {}", self.http_endpoint, payload);
        Ok(())
    }
}

// Main transformation service
struct TransformationService {
    binary_transformer: BinaryToJsonTransformer,
    json_transformer: JsonEnrichmentTransformer,
}

impl TransformationService {
    fn new() -> Self {
        Self {
            binary_transformer: BinaryToJsonTransformer,
            json_transformer: JsonEnrichmentTransformer,
        }
    }
    
    fn process_message(&self, payload: &[u8], topic: &str) 
        -> Option<(String, String)> {
        
        let (transformer, output_topic): (&dyn MessageTransformer, String) = 
            if topic.contains("/raw/binary") {
                (&self.binary_transformer, 
                 topic.replace("/raw/binary", "/processed/json"))
            } else if topic.contains("/raw/json") {
                (&self.json_transformer, 
                 topic.replace("/raw/json", "/enriched/json"))
            } else {
                return None;
            };
        
        match transformer.transform(payload, topic) {
            Ok(transformed) => Some((output_topic, transformed)),
            Err(e) => {
                eprintln!("Transformation error: {}", e);
                None
            }
        }
    }
}

fn main() {
    let mut mqttoptions = MqttOptions::new(
        "rust_transformer", 
        "localhost", 
        1883
    );
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    let service = TransformationService::new();
    
    // Subscribe to topics requiring transformation
    client.subscribe("devices/+/raw/binary", QoS::AtLeastOnce).unwrap();
    client.subscribe("devices/+/raw/json", QoS::AtLeastOnce).unwrap();
    
    println!("Message transformer started...");
    
    // Clone client for publishing
    let publish_client = client.clone();
    
    thread::spawn(move || {
        for notification in connection.iter() {
            if let Ok(Event::Incoming(Packet::Publish(publish))) = notification {
                let topic = publish.topic.clone();
                let payload = publish.payload.to_vec();
                
                println!("Received on {}: {} bytes", topic, payload.len());
                
                if let Some((output_topic, transformed_payload)) = 
                    service.process_message(&payload, &topic) {
                    
                    publish_client
                        .publish(
                            output_topic.clone(),
                            QoS::AtLeastOnce,
                            false,
                            transformed_payload.as_bytes()
                        )
                        .unwrap();
                    
                    println!("Published transformed message to: {}", output_topic);
                }
            }
        }
    });
    
    // Keep main thread alive
    loop {
        thread::sleep(Duration::from_secs(1));
    }
}
```

## Summary

**Message Transformation** is a critical middleware capability in MQTT architectures that enables:

- **Interoperability**: Converting between different data formats and protocols to bridge incompatible systems
- **Data Enrichment**: Adding contextual metadata, timestamps, and derived values to enhance message utility
- **Standardization**: Normalizing heterogeneous device outputs into consistent formats for downstream processing
- **Protocol Translation**: Bridging MQTT with other protocols (HTTP, CoAP, AMQP) in hybrid architectures

Both implementations demonstrate key transformation patterns: binary-to-JSON conversion for legacy devices, JSON enrichment with metadata for analytics pipelines, and topic-based routing for selective transformation. The C/C++ version uses the Mosquitto library with json-c for lightweight embedded scenarios, while the Rust implementation leverages serde for type-safe serialization and rumqttc for robust async MQTT handling.

Transformation can occur at multiple points: in edge gateways, broker plugins, dedicated middleware services, or client applications. The choice depends on processing requirements, latency constraints, and architectural preferences. Proper error handling, logging, and monitoring are essential for production deployments to track transformation failures and data quality issues.