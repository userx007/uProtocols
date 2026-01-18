# MQTT Telemetry and Metrics

## Overview

Telemetry and metrics represent one of MQTT's most powerful use cases. The protocol excels at streaming continuous sensor data and system metrics from distributed devices to central monitoring systems. This pattern is fundamental to IoT deployments, industrial monitoring, smart buildings, and observability platforms.

MQTT's lightweight publish-subscribe model makes it ideal for telemetry because devices can continuously publish measurements without needing to know who's consuming the data. Multiple subscribers can independently monitor the same metrics, and the broker handles message routing efficiently even with thousands of concurrent data streams.

## Key Concepts

**Telemetry** refers to automated measurement and transmission of data from remote sources. In MQTT contexts, this typically means sensors publishing readings like temperature, pressure, vibration, or location data at regular intervals.

**Metrics** are quantitative measurements of system performance or state, such as CPU usage, memory consumption, network throughput, or application-specific counters. These help operators understand system health and performance trends.

**Time-series data** is the natural format for telemetry, where each measurement is timestamped. MQTT messages carrying telemetry often include timestamps to maintain temporal accuracy even if network delays occur.

**Quality of Service (QoS)** levels become important architectural decisions. Telemetry streams often use QoS 0 (at most once) since occasional lost readings are acceptable when data arrives frequently. Critical metrics might use QoS 1 (at least once) to ensure delivery.

**Topic design** for metrics typically follows hierarchical patterns like `metrics/{system}/{subsystem}/{metric_name}` or `telemetry/{device_id}/{sensor_type}`, enabling subscribers to filter exactly what they need.

## C/C++ Implementation

Here's a comprehensive example using the Eclipse Paho MQTT C library for publishing and subscribing to telemetry data:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <MQTTClient.h>

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "telemetry_publisher"
#define QOS 0
#define TIMEOUT 10000L

// Structure to represent a telemetry reading
typedef struct {
    char sensor_id[32];
    char sensor_type[16];
    double value;
    long timestamp;
    char unit[8];
} TelemetryReading;

// Convert telemetry reading to JSON payload
char* telemetry_to_json(TelemetryReading *reading) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"sensor_id\":\"%s\",\"type\":\"%s\",\"value\":%.2f,\"timestamp\":%ld,\"unit\":\"%s\"}",
             reading->sensor_id, reading->sensor_type, reading->value, 
             reading->timestamp, reading->unit);
    return buffer;
}

// Simulate sensor reading
TelemetryReading get_temperature_reading(const char* sensor_id) {
    TelemetryReading reading;
    strncpy(reading.sensor_id, sensor_id, sizeof(reading.sensor_id));
    strncpy(reading.sensor_type, "temperature", sizeof(reading.sensor_type));
    strncpy(reading.unit, "C", sizeof(reading.unit));
    
    // Simulate temperature between 20-30°C with some variation
    reading.value = 20.0 + (rand() % 100) / 10.0;
    reading.timestamp = time(NULL);
    
    return reading;
}

// Publisher: Send telemetry data
int publish_telemetry() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }

    printf("Publishing telemetry data...\n");
    
    // Publish 50 readings at 1-second intervals
    for (int i = 0; i < 50; i++) {
        TelemetryReading reading = get_temperature_reading("sensor_001");
        char* payload = telemetry_to_json(&reading);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        MQTTClient_publishMessage(client, "telemetry/building_a/floor_1/temperature", 
                                  &pubmsg, &token);
        
        printf("Published: %s\n", payload);
        
        // Wait for token to ensure delivery (even with QoS 0, this helps with pacing)
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        
        sleep(1);  // 1 second interval
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}

// Callback for incoming messages
int message_arrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Subscriber: Receive and process telemetry
int subscribe_telemetry() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, BROKER_ADDRESS, "telemetry_subscriber",
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, NULL, message_arrived, NULL);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }

    printf("Subscribing to telemetry topics...\n");
    
    // Subscribe to all temperature sensors in building_a
    MQTTClient_subscribe(client, "telemetry/building_a/+/temperature", QOS);
    
    // Keep running for 60 seconds
    sleep(60);

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}

// System metrics collector
typedef struct {
    double cpu_usage;
    double memory_usage;
    long uptime_seconds;
    long timestamp;
} SystemMetrics;

void publish_system_metrics(MQTTClient client) {
    SystemMetrics metrics;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char payload[512];
    
    // Simulate system metrics collection
    metrics.cpu_usage = (rand() % 100);
    metrics.memory_usage = 30.0 + (rand() % 50);
    metrics.uptime_seconds = time(NULL);
    metrics.timestamp = time(NULL);
    
    // Publish individual metrics to separate topics
    snprintf(payload, sizeof(payload), "{\"value\":%.2f,\"timestamp\":%ld}", 
             metrics.cpu_usage, metrics.timestamp);
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = 1;  // Use QoS 1 for system metrics
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, "metrics/system/cpu_usage", &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
    
    snprintf(payload, sizeof(payload), "{\"value\":%.2f,\"timestamp\":%ld}", 
             metrics.memory_usage, metrics.timestamp);
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    
    MQTTClient_publishMessage(client, "metrics/system/memory_usage", &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    if (argc < 2) {
        printf("Usage: %s [publish|subscribe|metrics]\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "publish") == 0) {
        return publish_telemetry();
    } else if (strcmp(argv[1], "subscribe") == 0) {
        return subscribe_telemetry();
    } else if (strcmp(argv[1], "metrics") == 0) {
        MQTTClient client;
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        
        MQTTClient_create(&client, BROKER_ADDRESS, "metrics_publisher",
                          MQTTCLIENT_PERSISTENCE_NONE, NULL);
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        
        if (MQTTClient_connect(client, &conn_opts) == MQTTCLIENT_SUCCESS) {
            for (int i = 0; i < 20; i++) {
                publish_system_metrics(client);
                sleep(5);  // Publish metrics every 5 seconds
            }
            MQTTClient_disconnect(client, 10000);
        }
        MQTTClient_destroy(&client);
    }
    
    return 0;
}
```

## Rust Implementation

Here's an equivalent implementation using the `rumqttc` library, demonstrating Rust's safety and concurrency features:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use tokio::time::{sleep, Duration};
use chrono::Utc;
use rand::Rng;
use std::error::Error;

// Telemetry reading structure with serialization
#[derive(Debug, Serialize, Deserialize, Clone)]
struct TelemetryReading {
    sensor_id: String,
    sensor_type: String,
    value: f64,
    timestamp: i64,
    unit: String,
}

impl TelemetryReading {
    fn new(sensor_id: &str, sensor_type: &str, value: f64, unit: &str) -> Self {
        Self {
            sensor_id: sensor_id.to_string(),
            sensor_type: sensor_type.to_string(),
            value,
            timestamp: Utc::now().timestamp(),
            unit: unit.to_string(),
        }
    }
    
    fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(self)
    }
}

// System metrics structure
#[derive(Debug, Serialize, Deserialize)]
struct SystemMetrics {
    cpu_usage: f64,
    memory_usage: f64,
    disk_io: u64,
    network_rx: u64,
    network_tx: u64,
    timestamp: i64,
}

impl SystemMetrics {
    fn collect() -> Self {
        let mut rng = rand::thread_rng();
        Self {
            cpu_usage: rng.gen_range(0.0..100.0),
            memory_usage: rng.gen_range(30.0..80.0),
            disk_io: rng.gen_range(1000..10000),
            network_rx: rng.gen_range(5000..50000),
            network_tx: rng.gen_range(2000..20000),
            timestamp: Utc::now().timestamp(),
        }
    }
}

// Telemetry publisher
async fn publish_telemetry() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("telemetry_publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn task to handle eventloop
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("Connected to broker");
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    // Wait for connection
    sleep(Duration::from_secs(1)).await;

    println!("Publishing telemetry data...");
    
    let mut rng = rand::thread_rng();
    
    // Publish 50 temperature readings
    for i in 0..50 {
        let temperature = 20.0 + rng.gen_range(0.0..10.0);
        let reading = TelemetryReading::new(
            "sensor_001",
            "temperature",
            temperature,
            "C"
        );
        
        let payload = reading.to_json()?;
        let topic = "telemetry/building_a/floor_1/temperature";
        
        client.publish(topic, QoS::AtMostOnce, false, payload.as_bytes()).await?;
        
        println!("Published reading #{}: {}", i + 1, payload);
        
        sleep(Duration::from_secs(1)).await;
    }
    
    sleep(Duration::from_secs(2)).await;
    Ok(())
}

// Telemetry subscriber
async fn subscribe_telemetry() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("telemetry_subscriber", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to temperature sensors
    client.subscribe("telemetry/building_a/+/temperature", QoS::AtMostOnce).await?;
    
    // Also subscribe to all telemetry from building_a
    client.subscribe("telemetry/building_a/#", QoS::AtMostOnce).await?;
    
    println!("Subscribed to telemetry topics. Waiting for messages...");

    // Process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let topic = publish.topic.clone();
                let payload = String::from_utf8_lossy(&publish.payload);
                
                println!("\n--- Received Telemetry ---");
                println!("Topic: {}", topic);
                
                // Try to parse as TelemetryReading
                if let Ok(reading) = serde_json::from_str::<TelemetryReading>(&payload) {
                    println!("Sensor: {} ({})", reading.sensor_id, reading.sensor_type);
                    println!("Value: {} {}", reading.value, reading.unit);
                    println!("Timestamp: {}", reading.timestamp);
                } else {
                    println!("Raw payload: {}", payload);
                }
            }
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Connected to broker");
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Error: {:?}", e);
                sleep(Duration::from_secs(1)).await;
            }
        }
    }
}

// Metrics publisher with batching
async fn publish_system_metrics() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("metrics_publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Handle eventloop in background
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("Metrics publisher connected");
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    sleep(Duration::from_secs(1)).await;

    println!("Publishing system metrics...");
    
    // Publish metrics every 5 seconds for 2 minutes
    for iteration in 0..24 {
        let metrics = SystemMetrics::collect();
        
        // Publish individual metrics to separate topics
        let cpu_payload = serde_json::json!({
            "value": metrics.cpu_usage,
            "timestamp": metrics.timestamp
        }).to_string();
        
        let memory_payload = serde_json::json!({
            "value": metrics.memory_usage,
            "timestamp": metrics.timestamp
        }).to_string();
        
        let disk_payload = serde_json::json!({
            "value": metrics.disk_io,
            "timestamp": metrics.timestamp
        }).to_string();
        
        // Use QoS 1 for system metrics to ensure delivery
        client.publish("metrics/system/cpu_usage", QoS::AtLeastOnce, 
                      false, cpu_payload.as_bytes()).await?;
        
        client.publish("metrics/system/memory_usage", QoS::AtLeastOnce, 
                      false, memory_payload.as_bytes()).await?;
        
        client.publish("metrics/system/disk_io", QoS::AtLeastOnce, 
                      false, disk_payload.as_bytes()).await?;
        
        // Publish aggregated metrics to a single topic
        let all_metrics = serde_json::to_string(&metrics)?;
        client.publish("metrics/system/all", QoS::AtLeastOnce, 
                      false, all_metrics.as_bytes()).await?;
        
        println!("Published metrics iteration #{}: CPU={:.1}%, Memory={:.1}%", 
                iteration + 1, metrics.cpu_usage, metrics.memory_usage);
        
        sleep(Duration::from_secs(5)).await;
    }
    
    Ok(())
}

// Advanced: Multi-sensor telemetry aggregator
async fn aggregate_telemetry() -> Result<(), Box<dyn Error>> {
    use std::collections::HashMap;
    use tokio::sync::RwLock;
    use std::sync::Arc;
    
    let mut mqttoptions = MqttOptions::new("telemetry_aggregator", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Shared state to store latest readings from each sensor
    let readings: Arc<RwLock<HashMap<String, TelemetryReading>>> = 
        Arc::new(RwLock::new(HashMap::new()));
    
    let readings_clone = readings.clone();
    
    // Subscribe to all telemetry
    client.subscribe("telemetry/#", QoS::AtMostOnce).await?;
    
    println!("Aggregating telemetry from all sensors...");
    
    // Spawn task to print summary every 10 seconds
    let summary_task = tokio::spawn(async move {
        loop {
            sleep(Duration::from_secs(10)).await;
            
            let readings_map = readings_clone.read().await;
            println!("\n=== Telemetry Summary ({} sensors) ===", readings_map.len());
            
            for (sensor_id, reading) in readings_map.iter() {
                println!("{}: {:.2} {} ({})", 
                    sensor_id, reading.value, reading.unit, reading.sensor_type);
            }
            println!("=====================================\n");
        }
    });
    
    // Process incoming telemetry
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let payload = String::from_utf8_lossy(&publish.payload);
                
                if let Ok(reading) = serde_json::from_str::<TelemetryReading>(&payload) {
                    let mut readings_map = readings.write().await;
                    readings_map.insert(reading.sensor_id.clone(), reading);
                }
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Error: {:?}", e);
                sleep(Duration::from_secs(1)).await;
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        println!("Usage: {} [publish|subscribe|metrics|aggregate]", args[0]);
        return Ok(());
    }
    
    match args[1].as_str() {
        "publish" => publish_telemetry().await?,
        "subscribe" => subscribe_telemetry().await?,
        "metrics" => publish_system_metrics().await?,
        "aggregate" => aggregate_telemetry().await?,
        _ => println!("Unknown command. Use: publish, subscribe, metrics, or aggregate"),
    }
    
    Ok(())
}
```

## Summary

MQTT's publish-subscribe architecture makes it exceptionally well-suited for telemetry and metrics streaming. The protocol's lightweight overhead, flexible QoS levels, and hierarchical topic structure enable efficient collection of sensor data and system metrics from distributed devices.

Key advantages for telemetry include the ability to handle thousands of concurrent data streams, support for unreliable networks through QoS guarantees, and the decoupling of data producers from consumers. This allows telemetry systems to scale horizontally by adding subscribers without impacting publishers.

The C/C++ examples demonstrate low-level control suitable for embedded devices and resource-constrained environments, while the Rust implementation showcases memory safety, strong typing, and modern async/await patterns ideal for building robust telemetry infrastructure. Both approaches support structured data formats like JSON, enabling interoperability with time-series databases and monitoring platforms.

Effective telemetry systems typically combine appropriate topic hierarchies for filtering, suitable QoS levels balancing reliability with overhead, efficient payload formats, and proper timestamp handling to maintain data integrity across network delays. These patterns enable real-time monitoring, historical analysis, and alerting across diverse IoT and distributed system deployments.