# MQTT and the Publisher-Subscriber Pattern

## Overview

The Publisher-Subscriber (Pub-Sub) pattern is a messaging paradigm where senders (publishers) don't send messages directly to specific receivers (subscribers). Instead, messages are published to topics or channels, and subscribers express interest in specific topics. This creates a loosely coupled architecture where publishers and subscribers don't need to know about each other's existence.

MQTT (Message Queuing Telemetry Transport) is a lightweight, publish-subscribe network protocol that's ideal for IoT devices, remote sensors, and applications where bandwidth is limited. It operates on top of TCP/IP and uses a broker-based architecture.

## Key Concepts

**MQTT Broker**: A central server that receives all messages from publishers and distributes them to subscribers based on topic subscriptions.

**Topics**: Hierarchical strings that categorize messages (e.g., `home/bedroom/temperature`). Topics use forward slashes as delimiters and support wildcards:
- `+` (single-level wildcard): `home/+/temperature` matches `home/bedroom/temperature` and `home/kitchen/temperature`
- `#` (multi-level wildcard): `home/#` matches all topics under `home/`

**Quality of Service (QoS)**: MQTT provides three levels:
- QoS 0: At most once delivery (fire and forget)
- QoS 1: At least once delivery (acknowledged)
- QoS 2: Exactly once delivery (assured)

**Retained Messages**: The broker stores the last message on a topic, delivering it immediately to new subscribers.

**Last Will and Testament (LWT)**: A message automatically sent by the broker if a client disconnects unexpectedly.

## Benefits of Pub-Sub Architecture

The pattern decouples components in several ways:
- **Space decoupling**: Publishers and subscribers don't need to know each other's network location
- **Time decoupling**: Components don't need to be active simultaneously
- **Synchronization decoupling**: Publishing and receiving are asynchronous operations

This makes systems more scalable, flexible, and easier to maintain.

## C/C++ Implementation

Here's an example using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "C_MQTT_Client"
#define TOPIC "sensor/temperature"
#define QOS 1
#define TIMEOUT 10000L

// Callback for received messages
int message_arrived(void *context, char *topic, int topic_len, 
                    MQTTClient_message *message) {
    printf("Message arrived on topic '%s': %.*s\n", 
           topic, message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

// Callback for connection lost
void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

// Publisher function
void publish_temperature(MQTTClient client, float temperature) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    char payload[50];
    snprintf(payload, sizeof(payload), "%.2f", temperature);
    
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    printf("Published: %s to topic %s\n", payload, TOPIC);
    
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
}

// Subscriber setup
int setup_subscriber(MQTTClient client) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);
    
    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }
    
    MQTTClient_subscribe(client, TOPIC, QOS);
    printf("Subscribed to topic: %s\n", TOPIC);
    return MQTTCLIENT_SUCCESS;
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    
    // Create client
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                     MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Example: Run as subscriber
    if (argc > 1 && strcmp(argv[1], "subscribe") == 0) {
        setup_subscriber(client);
        printf("Waiting for messages (press Ctrl+C to exit)...\n");
        
        // Keep running to receive messages
        while (1) {
            // In a real application, you'd handle this more gracefully
        }
    }
    // Example: Run as publisher
    else {
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        MQTTClient_connect(client, &conn_opts);
        
        // Publish some temperature readings
        for (int i = 0; i < 5; i++) {
            float temp = 20.0 + (float)i * 0.5;
            publish_temperature(client, temp);
            sleep(1);
        }
        
        MQTTClient_disconnect(client, TIMEOUT);
    }
    
    MQTTClient_destroy(&client);
    return 0;
}
```

### C++ Object-Oriented Approach

```cpp
#include <mqtt/async_client.h>
#include <iostream>
#include <string>
#include <memory>

class TemperatureSubscriber : public virtual mqtt::callback {
private:
    mqtt::async_client& client_;
    
public:
    TemperatureSubscriber(mqtt::async_client& client) : client_(client) {}
    
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message received on topic '" 
                  << msg->get_topic() << "': "
                  << msg->to_string() << std::endl;
    }
    
    void connection_lost(const std::string& cause) override {
        std::cout << "Connection lost: " << cause << std::endl;
    }
};

class TemperaturePublisher {
private:
    mqtt::async_client client_;
    std::string topic_;
    
public:
    TemperaturePublisher(const std::string& broker, 
                        const std::string& clientId,
                        const std::string& topic) 
        : client_(broker, clientId), topic_(topic) {}
    
    void connect() {
        mqtt::connect_options connOpts;
        connOpts.set_keep_alive_interval(20);
        connOpts.set_clean_session(true);
        
        try {
            client_.connect(connOpts)->wait();
            std::cout << "Connected to broker" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
        }
    }
    
    void publish(float temperature) {
        auto msg = mqtt::make_message(topic_, std::to_string(temperature));
        msg->set_qos(1);
        client_.publish(msg)->wait();
        std::cout << "Published: " << temperature << "°C" << std::endl;
    }
    
    void disconnect() {
        client_.disconnect()->wait();
    }
};

int main() {
    const std::string BROKER = "tcp://localhost:1883";
    const std::string TOPIC = "sensor/temperature";
    
    // Publisher example
    TemperaturePublisher publisher(BROKER, "cpp_publisher", TOPIC);
    publisher.connect();
    
    for (int i = 0; i < 5; i++) {
        float temp = 22.0f + i * 0.3f;
        publisher.publish(temp);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    publisher.disconnect();
    
    return 0;
}
```

## Rust Implementation

Here's an example using the `rumqttc` crate (add `rumqttc = "0.24"` and `tokio` to Cargo.toml):

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::error::Error;

/// Publisher that sends temperature readings
async fn temperature_publisher() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("rust_publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn task to handle eventloop
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("Publisher connected to broker");
                }
                Ok(_) => {},
                Err(e) => {
                    eprintln!("Eventloop error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Give connection time to establish
    sleep(Duration::from_millis(500)).await;
    
    // Publish temperature readings
    for i in 0..5 {
        let temperature = 20.0 + (i as f32) * 0.5;
        let payload = format!("{:.2}", temperature);
        
        client
            .publish("sensor/temperature", QoS::AtLeastOnce, false, payload.as_bytes())
            .await?;
        
        println!("Published: {}°C", temperature);
        sleep(Duration::from_secs(1)).await;
    }
    
    Ok(())
}

/// Subscriber that receives temperature readings
async fn temperature_subscriber() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("rust_subscriber", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to topic
    client.subscribe("sensor/temperature", QoS::AtLeastOnce).await?;
    client.subscribe("sensor/+/humidity", QoS::AtLeastOnce).await?; // Wildcard example
    
    println!("Subscriber waiting for messages...");
    
    // Process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Subscriber connected to broker");
            }
            Ok(Event::Incoming(Packet::Publish(p))) => {
                let payload = String::from_utf8_lossy(&p.payload);
                println!("Received on '{}': {}", p.topic, payload);
            }
            Ok(_) => {},
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                sleep(Duration::from_secs(5)).await;
            }
        }
    }
}

/// Multi-topic publisher with structured data
struct SensorData {
    sensor_id: String,
    value: f32,
    unit: String,
}

impl SensorData {
    fn to_json(&self) -> String {
        format!(
            r#"{{"sensor_id":"{}","value":{},"unit":"{}"}}"#,
            self.sensor_id, self.value, self.unit
        )
    }
}

async fn multi_sensor_publisher() -> Result<(), Box<dyn Error>> {
    let mut mqttoptions = MqttOptions::new("multi_publisher", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    tokio::spawn(async move {
        loop {
            if let Err(e) = eventloop.poll().await {
                eprintln!("Eventloop error: {:?}", e);
                break;
            }
        }
    });
    
    sleep(Duration::from_millis(500)).await;
    
    // Publish to multiple topics
    let sensors = vec![
        ("sensor/living_room/temperature", SensorData {
            sensor_id: "LR-TEMP-01".to_string(),
            value: 22.5,
            unit: "°C".to_string(),
        }),
        ("sensor/living_room/humidity", SensorData {
            sensor_id: "LR-HUM-01".to_string(),
            value: 45.0,
            unit: "%".to_string(),
        }),
        ("sensor/bedroom/temperature", SensorData {
            sensor_id: "BR-TEMP-01".to_string(),
            value: 20.0,
            unit: "°C".to_string(),
        }),
    ];
    
    for (topic, data) in sensors {
        let payload = data.to_json();
        client
            .publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())
            .await?;
        println!("Published to {}: {}", topic, payload);
        sleep(Duration::from_millis(500)).await;
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Run publisher or subscriber based on command line argument
    let args: Vec<String> = std::env::args().collect();
    
    match args.get(1).map(|s| s.as_str()) {
        Some("subscribe") => temperature_subscriber().await?,
        Some("multi") => multi_sensor_publisher().await?,
        _ => temperature_publisher().await?,
    }
    
    Ok(())
}
```

### Advanced Rust Example with Last Will Testament

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, LastWill};
use tokio::time::{sleep, Duration};

async fn client_with_lwt() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("rust_lwt_client", "localhost", 1883);
    
    // Configure Last Will and Testament
    let lwt = LastWill::new(
        "clients/rust_lwt_client/status",
        "offline",
        QoS::AtLeastOnce,
        true, // retained
    );
    mqttoptions.set_last_will(lwt);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    tokio::spawn(async move {
        loop {
            if let Err(e) = eventloop.poll().await {
                eprintln!("Connection error: {:?}", e);
                break;
            }
        }
    });
    
    sleep(Duration::from_millis(500)).await;
    
    // Publish online status
    client
        .publish("clients/rust_lwt_client/status", QoS::AtLeastOnce, true, "online")
        .await?;
    
    println!("Client online. Will send offline message if disconnected unexpectedly.");
    
    // Simulate some work
    sleep(Duration::from_secs(10)).await;
    
    Ok(())
}
```

## Summary

The Publisher-Subscriber pattern, as implemented in MQTT, provides a powerful architecture for decoupling components in distributed systems. Publishers send messages to topics without knowing who will receive them, while subscribers express interest in topics without knowing who publishes to them. This separation creates highly scalable and maintainable systems.

MQTT's lightweight protocol makes it ideal for IoT and resource-constrained environments, supporting quality of service levels, retained messages, and last will testament features. The hierarchical topic structure with wildcard support enables flexible message routing.

In C/C++, the Eclipse Paho library provides both synchronous and asynchronous APIs with callback mechanisms for message handling. The C++ version offers object-oriented wrappers that integrate well with modern C++ practices.

Rust's `rumqttc` crate leverages async/await for non-blocking operations and provides a type-safe interface that prevents common errors at compile time. The tokio runtime enables efficient concurrent message handling.

All three implementations demonstrate the core pub-sub principles: loose coupling, asynchronous communication, and scalability. The choice of language depends on your requirements—C/C++ for embedded systems or legacy integration, and Rust for memory safety and modern concurrent programming with excellent performance.