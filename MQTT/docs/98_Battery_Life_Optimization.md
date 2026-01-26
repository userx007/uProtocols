# Battery Life Optimization for MQTT Devices

## Overview

Battery life optimization is critical for IoT devices that operate on battery power, such as sensors, wearables, and remote monitoring equipment. MQTT devices can drain batteries quickly if not properly optimized, as they maintain network connections, process messages, and perform periodic communications. This guide covers power-saving strategies and implementation patterns to maximize battery life while maintaining reliable MQTT connectivity.

## Key Power-Saving Strategies

### 1. Connection Management
- **Persistent Sessions**: Use MQTT's clean session flag (set to false) to maintain session state across disconnections, avoiding costly reconnection overhead
- **Keep-Alive Tuning**: Increase keep-alive intervals to reduce ping traffic (typical range: 60-3600 seconds for battery devices)
- **Connection Pooling**: Minimize connection/disconnection cycles by batching messages

### 2. Quality of Service (QoS) Selection
- **QoS 0**: Fire-and-forget, lowest power consumption but no delivery guarantee
- **QoS 1**: At-least-once delivery, moderate power usage with acknowledgments
- **QoS 2**: Exactly-once delivery, highest power consumption due to four-way handshake

### 3. Sleep Modes
- **Deep Sleep**: Complete system shutdown between transmissions
- **Light Sleep**: CPU sleep with RAM retention and faster wake-up
- **Modem Sleep**: Network hardware sleep while CPU remains active

### 4. Message Optimization
- **Payload Compression**: Reduce transmission time and data usage
- **Message Batching**: Accumulate data and send in bulk
- **Delta Encoding**: Transmit only changes rather than full state

## C/C++ Implementation (Using Eclipse Paho)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"
#include <unistd.h>

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "BatteryOptimizedDevice"
#define TOPIC       "sensors/temperature"
#define QOS         0  // Use QoS 0 for battery savings
#define TIMEOUT     10000L
#define KEEP_ALIVE  300  // 5 minutes keep-alive

// Battery-optimized MQTT configuration
typedef struct {
    MQTTClient client;
    int sleep_duration;  // seconds between transmissions
    int message_batch_size;
    char** message_buffer;
    int buffer_count;
} BatteryOptimizedMQTT;

// Initialize with power-saving settings
int mqtt_battery_init(BatteryOptimizedMQTT* mqtt_ctx, 
                      int sleep_duration, 
                      int batch_size) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    MQTTClient_create(&mqtt_ctx->client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Configure connection for battery optimization
    conn_opts.keepAliveInterval = KEEP_ALIVE;
    conn_opts.cleansession = 0;  // Persistent session to avoid reconnection overhead
    conn_opts.connectTimeout = 30;
    conn_opts.retryInterval = 60;
    
    mqtt_ctx->sleep_duration = sleep_duration;
    mqtt_ctx->message_batch_size = batch_size;
    mqtt_ctx->message_buffer = malloc(batch_size * sizeof(char*));
    mqtt_ctx->buffer_count = 0;
    
    int rc = MQTTClient_connect(mqtt_ctx->client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }
    
    printf("Connected with battery-optimized settings\n");
    return MQTTCLIENT_SUCCESS;
}

// Add message to batch buffer
void add_to_batch(BatteryOptimizedMQTT* mqtt_ctx, const char* payload) {
    if (mqtt_ctx->buffer_count < mqtt_ctx->message_batch_size) {
        mqtt_ctx->message_buffer[mqtt_ctx->buffer_count] = strdup(payload);
        mqtt_ctx->buffer_count++;
    }
}

// Flush batch to broker
int flush_batch(BatteryOptimizedMQTT* mqtt_ctx) {
    if (mqtt_ctx->buffer_count == 0) {
        return MQTTCLIENT_SUCCESS;
    }
    
    // Combine messages into single payload (example: JSON array)
    char combined_payload[4096] = "[";
    
    for (int i = 0; i < mqtt_ctx->buffer_count; i++) {
        strcat(combined_payload, mqtt_ctx->message_buffer[i]);
        if (i < mqtt_ctx->buffer_count - 1) {
            strcat(combined_payload, ",");
        }
        free(mqtt_ctx->message_buffer[i]);
    }
    strcat(combined_payload, "]");
    
    // Publish with QoS 0 for lowest power
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = combined_payload;
    pubmsg.payloadlen = strlen(combined_payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(mqtt_ctx->client, TOPIC, &pubmsg, &token);
    
    mqtt_ctx->buffer_count = 0;
    
    return rc;
}

// Simulate deep sleep
void deep_sleep(int seconds) {
    printf("Entering deep sleep for %d seconds...\n", seconds);
    sleep(seconds);
    printf("Waking up from deep sleep\n");
}

// Main battery-optimized loop
void battery_optimized_loop(BatteryOptimizedMQTT* mqtt_ctx) {
    for (int cycle = 0; cycle < 10; cycle++) {
        // Collect sensor data (simulated)
        char payload[128];
        snprintf(payload, sizeof(payload), 
                "{\"temp\":%.1f,\"cycle\":%d}", 
                20.0 + (cycle * 0.5), cycle);
        
        printf("Collected: %s\n", payload);
        add_to_batch(mqtt_ctx, payload);
        
        // Flush when batch is full or last cycle
        if (mqtt_ctx->buffer_count >= mqtt_ctx->message_batch_size || 
            cycle == 9) {
            flush_batch(mqtt_ctx);
        }
        
        // Enter deep sleep to save power
        if (cycle < 9) {
            deep_sleep(mqtt_ctx->sleep_duration);
        }
    }
}

// Cleanup
void mqtt_battery_cleanup(BatteryOptimizedMQTT* mqtt_ctx) {
    flush_batch(mqtt_ctx);  // Send any remaining messages
    MQTTClient_disconnect(mqtt_ctx->client, TIMEOUT);
    MQTTClient_destroy(&mqtt_ctx->client);
    free(mqtt_ctx->message_buffer);
}

int main() {
    BatteryOptimizedMQTT mqtt_ctx;
    
    // Initialize with 60s sleep and batch size of 3
    if (mqtt_battery_init(&mqtt_ctx, 60, 3) == MQTTCLIENT_SUCCESS) {
        battery_optimized_loop(&mqtt_ctx);
        mqtt_battery_cleanup(&mqtt_ctx);
    }
    
    return 0;
}
```

## Rust Implementation (Using rumqttc)

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;
use serde_json::json;

struct BatteryOptimizedMqtt {
    client: Client,
    sleep_duration: Duration,
    message_batch: Vec<String>,
    batch_size: usize,
}

impl BatteryOptimizedMqtt {
    fn new(broker: &str, port: u16, client_id: &str, 
           sleep_duration: Duration, batch_size: usize) -> Self {
        let mut mqtt_options = MqttOptions::new(client_id, broker, port);
        
        // Battery optimization settings
        mqtt_options.set_keep_alive(Duration::from_secs(300)); // 5 minutes
        mqtt_options.set_clean_session(false); // Persistent session
        mqtt_options.set_connection_timeout(Duration::from_secs(30));
        
        // Create client with optimized options
        let (client, mut connection) = Client::new(mqtt_options, 10);
        
        // Spawn connection handler in background
        thread::spawn(move || {
            for notification in connection.iter() {
                match notification {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("Connected with battery-optimized settings");
                    }
                    Ok(Event::Incoming(Packet::PubAck(_))) => {
                        // Acknowledgment received (if using QoS 1)
                    }
                    Err(e) => {
                        eprintln!("Connection error: {:?}", e);
                        thread::sleep(Duration::from_secs(5));
                    }
                    _ => {}
                }
            }
        });
        
        // Give connection time to establish
        thread::sleep(Duration::from_millis(500));
        
        BatteryOptimizedMqtt {
            client,
            sleep_duration,
            message_batch: Vec::new(),
            batch_size,
        }
    }
    
    // Add message to batch
    fn add_to_batch(&mut self, payload: String) {
        self.message_batch.push(payload);
    }
    
    // Flush batch to broker
    fn flush_batch(&mut self, topic: &str) -> Result<(), rumqttc::ClientError> {
        if self.message_batch.is_empty() {
            return Ok(());
        }
        
        // Create combined payload (JSON array)
        let combined = format!("[{}]", self.message_batch.join(","));
        
        println!("Publishing batch of {} messages", self.message_batch.len());
        
        // Publish with QoS 0 for lowest power consumption
        self.client.publish(topic, QoS::AtMostOnce, false, combined.as_bytes())?;
        
        self.message_batch.clear();
        Ok(())
    }
    
    // Simulate deep sleep
    fn deep_sleep(&self) {
        println!("Entering deep sleep for {:?}...", self.sleep_duration);
        thread::sleep(self.sleep_duration);
        println!("Waking up from deep sleep");
    }
    
    // Battery-optimized data collection loop
    fn run_optimized_loop(&mut self, topic: &str, cycles: usize) 
        -> Result<(), rumqttc::ClientError> {
        for cycle in 0..cycles {
            // Simulate sensor reading
            let temp = 20.0 + (cycle as f32 * 0.5);
            let payload = json!({
                "temp": temp,
                "cycle": cycle,
                "battery": self.estimate_battery_level(cycle)
            }).to_string();
            
            println!("Collected: {}", payload);
            self.add_to_batch(payload);
            
            // Flush when batch is full or on last cycle
            if self.message_batch.len() >= self.batch_size || cycle == cycles - 1 {
                self.flush_batch(topic)?;
            }
            
            // Enter deep sleep to conserve power
            if cycle < cycles - 1 {
                self.deep_sleep();
            }
        }
        
        Ok(())
    }
    
    // Simulate battery level estimation
    fn estimate_battery_level(&self, cycle: usize) -> f32 {
        100.0 - (cycle as f32 * 2.0)
    }
}

// Advanced: Delta encoding for efficient data transmission
struct DeltaEncoder {
    last_value: Option<f32>,
}

impl DeltaEncoder {
    fn new() -> Self {
        DeltaEncoder { last_value: None }
    }
    
    fn encode(&mut self, current: f32) -> String {
        match self.last_value {
            None => {
                self.last_value = Some(current);
                json!({"value": current, "type": "full"}).to_string()
            }
            Some(last) => {
                let delta = current - last;
                self.last_value = Some(current);
                
                // Only send if change is significant (threshold-based transmission)
                if delta.abs() > 0.1 {
                    json!({"delta": delta, "type": "delta"}).to_string()
                } else {
                    String::new() // Skip transmission if change is insignificant
                }
            }
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create battery-optimized MQTT client
    let mut mqtt = BatteryOptimizedMqtt::new(
        "broker.hivemq.com",
        1883,
        "BatteryOptimizedRustDevice",
        Duration::from_secs(60),  // 60s sleep between cycles
        3  // Batch 3 messages together
    );
    
    // Run optimized loop
    mqtt.run_optimized_loop("sensors/temperature", 10)?;
    
    println!("Battery-optimized transmission complete");
    
    // Demonstrate delta encoding
    println!("\n--- Delta Encoding Example ---");
    let mut encoder = DeltaEncoder::new();
    let readings = vec![20.0, 20.1, 20.05, 20.5, 20.52];
    
    for reading in readings {
        let encoded = encoder.encode(reading);
        if !encoded.is_empty() {
            println!("Transmitted: {}", encoded);
        } else {
            println!("Skipped transmission (insignificant change)");
        }
    }
    
    Ok(())
}
```

## Summary

Battery life optimization for MQTT devices involves multiple strategies working together:

**Connection Strategies**: Use persistent sessions (clean session = false) to avoid reconnection overhead, tune keep-alive intervals to 5+ minutes, and minimize connection cycles.

**QoS Selection**: Choose QoS 0 for non-critical data to eliminate acknowledgment overhead, reserving higher QoS levels only for critical messages.

**Sleep Modes**: Implement deep sleep between transmissions where the device powers down completely, waking only to collect and transmit data in scheduled intervals.

**Message Batching**: Accumulate multiple readings and transmit them together in a single payload, reducing the number of costly network operations.

**Data Optimization**: Use delta encoding to transmit only changes, implement threshold-based transmission to skip insignificant updates, and compress payloads when beneficial.

**Implementation Considerations**: Both C/C++ and Rust examples demonstrate practical battery optimization patterns including message buffering, batch transmission, and simulated sleep cycles. Real implementations would integrate with hardware-specific sleep APIs and power management features.

By combining these techniques, battery-operated MQTT devices can extend their operational lifetime from days to months or even years, depending on transmission frequency and hardware capabilities.