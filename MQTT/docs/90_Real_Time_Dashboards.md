# Real-Time Dashboards with MQTT

## Overview

Real-time dashboards display live data from MQTT topics, providing instant visibility into system metrics, sensor readings, or application states. They consume MQTT messages and update visualizations dynamically, enabling monitoring, alerting, and decision-making based on current conditions.

## Core Concepts

**MQTT as a Data Feed**: Devices and services publish telemetry to topics like `sensor/temperature`, `system/cpu`, or `alerts/critical`. Dashboard clients subscribe to these topics and update displays as messages arrive.

**Topic Organization**: Dashboards often subscribe to multiple topics using wildcards:
- `sensor/#` - all sensor data
- `building/+/temperature` - temperature from all rooms
- `system/+/metrics` - metrics from all servers

**Message Formats**: JSON is common for structured data:
```json
{
  "timestamp": 1706198400,
  "value": 23.5,
  "unit": "celsius",
  "location": "warehouse_a"
}
```

## C/C++ Implementation

Using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define BROKER "tcp://localhost:1883"
#define CLIENTID "dashboard_client"
#define QOS 1

// Callback for incoming messages
int message_arrived(void *context, char *topic, int topic_len, 
                    MQTTClient_message *message) {
    printf("[%s] %.*s\n", topic, message->payloadlen, 
           (char*)message->payload);
    
    // Parse JSON and update dashboard state here
    // In production, use a JSON library like cJSON
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, BROKER, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Subscribe to multiple topics for dashboard
    MQTTClient_subscribe(client, "sensor/#", QOS);
    MQTTClient_subscribe(client, "system/+/cpu", QOS);
    MQTTClient_subscribe(client, "alerts/#", QOS);

    printf("Dashboard listening...\n");
    
    // Keep running to receive messages
    while(1) {
        // In a real dashboard, this would update UI components
        // instead of blocking indefinitely
        MQTTClient_yield();
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}
```

**C++ with Modern Patterns**:

```cpp
#include <mqtt/async_client.h>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DashboardCallback : public virtual mqtt::callback {
private:
    std::map<std::string, json> data_cache;
    std::mutex cache_mutex;

public:
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::lock_guard<std::mutex> lock(cache_mutex);
        
        try {
            json payload = json::parse(msg->to_string());
            data_cache[msg->get_topic()] = payload;
            
            std::cout << "Updated: " << msg->get_topic() 
                      << " -> " << payload.dump(2) << std::endl;
        } catch (json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        }
    }

    json get_snapshot() {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return json(data_cache);
    }
};

int main() {
    mqtt::async_client client("tcp://localhost:1883", "dashboard_cpp");
    DashboardCallback cb;
    
    client.set_callback(cb);
    
    mqtt::connect_options connOpts;
    connOpts.set_clean_session(true);
    
    client.connect(connOpts)->wait();
    
    client.subscribe("sensor/#", 1);
    client.subscribe("system/+/metrics", 1);
    
    std::cout << "Dashboard active. Press Enter to get snapshot...\n";
    std::cin.get();
    
    std::cout << cb.get_snapshot().dump(2) << std::endl;
    
    client.disconnect()->wait();
    return 0;
}
```

## Rust Implementation

Using the `rumqttc` library:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use tokio;

#[derive(Debug, Deserialize, Serialize, Clone)]
struct SensorData {
    timestamp: u64,
    value: f64,
    unit: String,
    location: Option<String>,
}

#[derive(Clone)]
struct DashboardState {
    data: Arc<Mutex<HashMap<String, SensorData>>>,
}

impl DashboardState {
    fn new() -> Self {
        Self {
            data: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    fn update(&self, topic: String, payload: &[u8]) {
        if let Ok(sensor_data) = serde_json::from_slice::<SensorData>(payload) {
            let mut data = self.data.lock().unwrap();
            data.insert(topic.clone(), sensor_data.clone());
            println!("[{}] Updated: {:?}", topic, sensor_data);
        } else {
            eprintln!("Failed to parse payload for topic: {}", topic);
        }
    }

    fn get_snapshot(&self) -> HashMap<String, SensorData> {
        self.data.lock().unwrap().clone()
    }
}

#[tokio::main]
async fn main() {
    let mut mqttoptions = MqttOptions::new("dashboard_rust", "localhost", 1883);
    mqttoptions.set_keep_alive(std::time::Duration::from_secs(20));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    let state = DashboardState::new();

    // Subscribe to dashboard topics
    client.subscribe("sensor/#", QoS::AtLeastOnce).await.unwrap();
    client.subscribe("system/+/cpu", QoS::AtLeastOnce).await.unwrap();
    client.subscribe("alerts/#", QoS::AtLeastOnce).await.unwrap();

    println!("Dashboard started, listening for messages...");

    // Spawn a task to periodically print dashboard snapshot
    let state_clone = state.clone();
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(
            std::time::Duration::from_secs(10)
        );
        loop {
            interval.tick().await;
            let snapshot = state_clone.get_snapshot();
            println!("\n=== Dashboard Snapshot ===");
            for (topic, data) in snapshot {
                println!("{}: {:.2} {}", topic, data.value, data.unit);
            }
            println!("========================\n");
        }
    });

    // Event loop to process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(p))) => {
                state.update(p.topic.to_string(), &p.payload);
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Error: {:?}", e);
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            }
        }
    }
}
```

**Advanced Rust with Aggregation**:

```rust
use std::time::{Duration, SystemTime};

#[derive(Debug, Clone)]
struct MetricStats {
    count: u64,
    sum: f64,
    min: f64,
    max: f64,
    last_updated: SystemTime,
}

impl MetricStats {
    fn new(value: f64) -> Self {
        Self {
            count: 1,
            sum: value,
            min: value,
            max: value,
            last_updated: SystemTime::now(),
        }
    }

    fn update(&mut self, value: f64) {
        self.count += 1;
        self.sum += value;
        self.min = self.min.min(value);
        self.max = self.max.max(value);
        self.last_updated = SystemTime::now();
    }

    fn average(&self) -> f64 {
        self.sum / self.count as f64
    }
}

struct AggregatingDashboard {
    metrics: Arc<Mutex<HashMap<String, MetricStats>>>,
}

impl AggregatingDashboard {
    fn update_metric(&self, topic: String, value: f64) {
        let mut metrics = self.metrics.lock().unwrap();
        metrics.entry(topic)
            .and_modify(|stats| stats.update(value))
            .or_insert_with(|| MetricStats::new(value));
    }
}
```

## Summary

Real-time dashboards with MQTT enable live monitoring by subscribing to data topics and updating visualizations as messages arrive. **C/C++** implementations use callbacks with libraries like Paho MQTT, suitable for embedded or performance-critical applications. **Rust** provides memory-safe concurrency with `Arc<Mutex<>>` for shared state and async/await for efficient event handling. Key patterns include wildcard subscriptions for topic groups, JSON parsing for structured data, and state management for caching latest values or computing aggregations. Dashboards can range from simple console displays to full web interfaces, with MQTT serving as the real-time data backbone that decouples data producers from visualization consumers.