# MQTT Message Persistence: Ensuring Message Durability During Broker Restarts

## Overview

Message persistence is a critical feature in MQTT that ensures messages are not lost when a broker restarts or crashes. When a message is marked as persistent, the broker stores it to disk (or another durable storage medium) before forwarding it to subscribers. This guarantees that even if the broker fails unexpectedly, messages will be available for delivery once the broker restarts.

## Core Concepts

### Persistent vs Non-Persistent Messages

MQTT provides two modes for message handling:

1. **Non-Persistent Messages (QoS 0)**: Stored only in memory, lost on broker restart
2. **Persistent Messages (QoS 1 and QoS 2)**: Stored to disk, survive broker restarts

The persistence behavior is controlled by the **retain flag** and **QoS level** of messages, combined with broker configuration settings.

### When Message Persistence Matters

Message persistence is essential in scenarios such as:
- Industrial IoT systems where sensor data cannot be lost
- Financial applications requiring guaranteed message delivery
- Critical alerts and notifications
- Systems with intermittent connectivity
- Applications requiring exactly-once delivery semantics

### How It Works

When a publisher sends a message with QoS 1 or 2:
1. The broker receives the message
2. The broker writes the message to persistent storage (disk)
3. The broker acknowledges receipt to the publisher
4. The broker forwards the message to subscribers
5. Upon acknowledgment from subscribers, the broker removes the message from storage

If the broker crashes between steps 2 and 5, the message remains on disk and will be delivered after restart.

## Code Examples

### C/C++ Implementation (Eclipse Paho MQTT C Library)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "PersistentPublisher"
#define TOPIC       "sensors/temperature"
#define QOS         2  // Highest QoS for persistence
#define TIMEOUT     10000L

// Persistent storage configuration
#define PERSISTENCE_TYPE MQTTCLIENT_PERSISTENCE_DEFAULT
#define PERSISTENCE_CONTEXT NULL

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create client with persistent storage
    // The library will create a directory to store unacknowledged messages
    rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
                          PERSISTENCE_TYPE, PERSISTENCE_CONTEXT);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to create client, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Configure connection options for clean session = 0
    // This ensures the broker also persists session state
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0;  // Persistent session
    conn_opts.reliable = 1;      // Ensure reliable delivery

    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Prepare persistent message
    char payload[50];
    sprintf(payload, "Temperature: 25.5C - Critical reading");
    
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;           // QoS 2 for exactly-once delivery
    pubmsg.retained = 0;        // Not a retained message (different from persistence)

    // Publish message - will be persisted by both client and broker
    rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Waiting for message delivery...\n");
    
    // Wait for message delivery confirmation
    // The client library persists this message until confirmed
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message with delivery token %d delivered\n", token);

    // Disconnect
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

### C++ Subscriber with Persistent Session

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include "mqtt/client.h"

const std::string SERVER_ADDRESS{"tcp://localhost:1883"};
const std::string CLIENT_ID{"PersistentSubscriber"};
const std::string TOPIC{"sensors/temperature"};
const int QOS = 2;

class PersistentCallback : public virtual mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "\nConnection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "\ttopic: " << msg->get_topic() << std::endl;
        std::cout << "\tpayload: " << msg->to_string() << std::endl;
        std::cout << "\tQoS: " << msg->get_qos() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {}
};

int main(int argc, char* argv[]) {
    // Create client with persistent storage
    // The "./persist" directory will store undelivered messages
    mqtt::client client(SERVER_ADDRESS, CLIENT_ID, "./persist");
    
    PersistentCallback cb;
    client.set_callback(cb);

    // Connection options with persistent session
    mqtt::connect_options connOpts;
    connOpts.set_clean_session(false);  // Maintain session across restarts
    connOpts.set_automatic_reconnect(true);
    connOpts.set_keep_alive_interval(20);

    try {
        std::cout << "Connecting to broker..." << std::endl;
        client.connect(connOpts);
        std::cout << "Connected successfully" << std::endl;

        // Subscribe with QoS 2 for persistent delivery
        std::cout << "Subscribing to topic: " << TOPIC << std::endl;
        client.subscribe(TOPIC, QOS);
        std::cout << "Subscribed successfully" << std::endl;

        // Keep running to receive messages
        std::cout << "Waiting for messages... (Press Ctrl+C to exit)" << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        client.unsubscribe(TOPIC);
        client.disconnect();
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
```

### Rust Implementation (rumqttc Library)

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;

fn main() {
    // Publisher with persistent storage
    publish_persistent_message();
    
    // Subscriber with persistent session
    subscribe_persistent();
}

fn publish_persistent_message() {
    // Configure MQTT options with persistent session
    let mut mqttoptions = MqttOptions::new(
        "rust_persistent_publisher",
        "localhost",
        1883
    );
    
    // Clean session = false ensures broker persists session state
    mqttoptions.set_clean_session(false);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    // Create client with persistent storage
    // The client will persist messages to disk in case of disconnection
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn thread to handle connection events
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(Event::Incoming(Packet::PubAck(_))) => {
                    println!("Message acknowledged by broker (QoS 1)");
                }
                Ok(Event::Incoming(Packet::PubComp(_))) => {
                    println!("Message delivery complete (QoS 2)");
                }
                Ok(Event::Outgoing(_)) => {
                    // Message sent to broker
                }
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
                _ => {}
            }
        }
    });

    // Wait for connection to establish
    thread::sleep(Duration::from_secs(1));

    // Publish persistent message with QoS 2
    let topic = "sensors/temperature";
    let payload = b"Temperature: 25.5C - Critical reading";
    
    match client.publish(topic, QoS::ExactlyOnce, false, payload) {
        Ok(_) => println!("Published persistent message to {}", topic),
        Err(e) => eprintln!("Failed to publish: {:?}", e),
    }

    // Give time for acknowledgment
    thread::sleep(Duration::from_secs(2));
}

fn subscribe_persistent() {
    // Configure subscriber with persistent session
    let mut mqttoptions = MqttOptions::new(
        "rust_persistent_subscriber",
        "localhost",
        1883
    );
    
    // Persistent session - broker will queue messages during offline periods
    mqttoptions.set_clean_session(false);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Subscribe to topic with QoS 2
    client.subscribe("sensors/temperature", QoS::ExactlyOnce)
        .expect("Failed to subscribe");
    
    println!("Subscribed to sensors/temperature with QoS 2");
    println!("Waiting for messages...");

    // Process incoming messages
    for notification in connection.iter() {
        match notification {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let payload = String::from_utf8_lossy(&publish.payload);
                println!("\n=== Message Received ===");
                println!("Topic: {}", publish.topic);
                println!("Payload: {}", payload);
                println!("QoS: {:?}", publish.qos);
                println!("========================\n");
            }
            Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                // session_present = true means broker has persistent session
                if connack.session_present {
                    println!("Resumed persistent session - queued messages will be delivered");
                } else {
                    println!("New session started");
                }
            }
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                thread::sleep(Duration::from_secs(5));
            }
            _ => {}
        }
    }
}
```

### Advanced Rust: Custom Persistence Store

```rust
use rumqttc::{Client, MqttOptions, QoS, StateStore};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct PersistedMessage {
    topic: String,
    payload: Vec<u8>,
    qos: u8,
    timestamp: u64,
}

// Custom persistence implementation
struct FilePersistence {
    storage: Arc<Mutex<HashMap<String, Vec<u8>>>>,
    file_path: String,
}

impl FilePersistence {
    fn new(path: &str) -> Self {
        // Load existing persisted data from disk
        let storage = Arc::new(Mutex::new(HashMap::new()));
        
        FilePersistence {
            storage,
            file_path: path.to_string(),
        }
    }
    
    fn persist_message(&self, msg: &PersistedMessage) -> Result<(), Box<dyn std::error::Error>> {
        let serialized = serde_json::to_vec(msg)?;
        let mut store = self.storage.lock().unwrap();
        let key = format!("{}:{}", msg.topic, msg.timestamp);
        store.insert(key, serialized);
        
        // Write to disk (simplified)
        std::fs::write(&self.file_path, serde_json::to_string(&*store)?)?;
        Ok(())
    }
    
    fn remove_message(&self, key: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut store = self.storage.lock().unwrap();
        store.remove(key);
        std::fs::write(&self.file_path, serde_json::to_string(&*store)?)?;
        Ok(())
    }
}

fn main() {
    let persistence = FilePersistence::new("./mqtt_messages.json");
    
    // Your MQTT client code using custom persistence
    println!("Custom persistence store initialized");
}
```

## Summary

**Message persistence** in MQTT is a fundamental mechanism that ensures critical messages survive broker restarts and network failures. By leveraging QoS levels 1 and 2 combined with persistent sessions (clean session = false), both the broker and client can maintain message state across disconnections.

**Key takeaways:**

- **QoS 1 and 2** enable message persistence at the broker level
- **Clean session = false** maintains session state and subscriptions across restarts
- **Client-side persistence** stores outgoing messages until acknowledged
- **Broker-side persistence** queues messages for offline subscribers with persistent sessions
- Proper error handling and acknowledgment tracking are essential for reliable systems

Message persistence adds overhead in terms of disk I/O and storage requirements but provides critical guarantees for enterprise and industrial applications where data loss is unacceptable. The examples provided demonstrate how to implement persistent messaging patterns in C/C++ using Eclipse Paho and in Rust using rumqttc, giving you the foundation to build robust, fault-tolerant MQTT applications.