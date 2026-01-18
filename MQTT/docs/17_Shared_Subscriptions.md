# MQTT Shared Subscriptions: Load Balancing and Message Distribution

## Overview

**Shared Subscriptions** is an MQTT feature that enables load balancing of messages across multiple subscribers within a subscription group. Instead of every subscriber receiving a copy of each message (standard MQTT behavior), shared subscriptions distribute messages among group members, with each message delivered to only one subscriber in the group.

This feature is particularly valuable in scenarios where you have multiple consumers processing messages from high-throughput topics, need horizontal scaling of message processing, or want to implement competing consumer patterns for workload distribution.

## How Shared Subscriptions Work

### Standard vs. Shared Subscriptions

**Standard Subscription:**
- Topic: `sensors/temperature`
- Behavior: Every subscriber receives every message
- Use case: Broadcasting information to all interested parties

**Shared Subscription:**
- Topic: `$share/group1/sensors/temperature`
- Behavior: Messages are distributed among subscribers in `group1`
- Use case: Load balancing work across multiple processors

### Syntax

The shared subscription topic format is:
```
$share/{ShareName}/{TopicFilter}
```

- `$share`: Prefix indicating a shared subscription
- `{ShareName}`: The name of the subscriber group
- `{TopicFilter}`: The actual MQTT topic filter (can include wildcards)

### Distribution Strategy

MQTT brokers typically use round-robin or random distribution to balance messages across group members. The specific algorithm depends on the broker implementation (Mosquitto, HiveMQ, EMQX, etc.).

## C/C++ Implementation

Here's a comprehensive example using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID_PREFIX "SharedSubscriber"
#define TOPIC       "sensors/temperature"
#define SHARE_GROUP "processing_group"
#define QOS         1
#define TIMEOUT     10000L

// Callback for received messages
int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    int subscriber_id = *(int*)context;
    
    printf("[Subscriber %d] Received message:\n", subscriber_id);
    printf("  Topic: %s\n", topicName);
    printf("  Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("  QoS: %d\n", message->qos);
    
    // Simulate message processing
    sleep(1);
    printf("[Subscriber %d] Processing complete\n\n", subscriber_id);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connectionLost(void *context, char *cause) {
    int subscriber_id = *(int*)context;
    printf("[Subscriber %d] Connection lost: %s\n", subscriber_id, cause);
}

int setup_shared_subscriber(int subscriber_id) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    char client_id[50];
    char shared_topic[100];
    int rc;
    
    // Create unique client ID
    snprintf(client_id, sizeof(client_id), "%s_%d", CLIENTID_PREFIX, subscriber_id);
    
    // Create shared subscription topic
    snprintf(shared_topic, sizeof(shared_topic), "$share/%s/%s", SHARE_GROUP, TOPIC);
    
    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, client_id, 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    int *id_ptr = malloc(sizeof(int));
    *id_ptr = subscriber_id;
    MQTTClient_setCallbacks(client, id_ptr, connectionLost, messageArrived, NULL);
    
    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[Subscriber %d] Failed to connect, return code %d\n", subscriber_id, rc);
        return -1;
    }
    
    printf("[Subscriber %d] Connected to broker\n", subscriber_id);
    
    // Subscribe to shared topic
    if ((rc = MQTTClient_subscribe(client, shared_topic, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("[Subscriber %d] Failed to subscribe, return code %d\n", subscriber_id, rc);
        return -1;
    }
    
    printf("[Subscriber %d] Subscribed to shared topic: %s\n", subscriber_id, shared_topic);
    
    return 0;
}

// Publisher function to demonstrate message distribution
void publish_messages(int count) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char payload[100];
    
    MQTTClient_create(&client, ADDRESS, "Publisher", 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Publisher failed to connect\n");
        return;
    }
    
    printf("Publisher connected, sending %d messages...\n\n", count);
    
    for (int i = 0; i < count; i++) {
        snprintf(payload, sizeof(payload), "Temperature reading #%d: %.2f°C", 
                 i + 1, 20.0 + (rand() % 100) / 10.0);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        MQTTClient_waitForCompletion(client, token, TIMEOUT);
        
        printf("Published message #%d\n", i + 1);
        usleep(500000); // 500ms delay between messages
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
}

int main(int argc, char* argv[]) {
    // Create 3 shared subscribers
    for (int i = 1; i <= 3; i++) {
        if (fork() == 0) {
            setup_shared_subscriber(i);
            // Keep subscriber running
            while (1) {
                sleep(1);
            }
            exit(0);
        }
    }
    
    // Parent process acts as publisher
    sleep(2); // Wait for subscribers to connect
    publish_messages(9); // Publish 9 messages to be distributed
    
    // Keep program running
    printf("\nPress Ctrl+C to exit\n");
    while (1) {
        sleep(1);
    }
    
    return 0;
}
```

## Rust Implementation

Here's an implementation using the `rumqttc` library in Rust:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::sync::Arc;

#[tokio::main]
async fn main() {
    // Spawn multiple shared subscribers
    let mut handles = vec![];
    
    for subscriber_id in 1..=3 {
        let handle = tokio::spawn(async move {
            shared_subscriber(subscriber_id).await;
        });
        handles.push(handle);
    }
    
    // Wait for subscribers to initialize
    sleep(Duration::from_secs(2)).await;
    
    // Start publisher
    let publisher_handle = tokio::spawn(async {
        publish_messages(9).await;
    });
    
    // Wait for publisher to finish
    publisher_handle.await.unwrap();
    
    // Keep subscribers running
    for handle in handles {
        handle.await.unwrap();
    }
}

async fn shared_subscriber(subscriber_id: u32) {
    let client_id = format!("shared_subscriber_{}", subscriber_id);
    let share_group = "processing_group";
    let topic = "sensors/temperature";
    let shared_topic = format!("$share/{}/{}", share_group, topic);
    
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new(client_id.clone(), "localhost", 1883);
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(true);
    
    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Subscribe to shared topic
    client
        .subscribe(&shared_topic, QoS::AtLeastOnce)
        .await
        .unwrap();
    
    println!("[Subscriber {}] Connected and subscribed to: {}", 
             subscriber_id, shared_topic);
    
    // Process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let payload = String::from_utf8_lossy(&publish.payload);
                
                println!("[Subscriber {}] Received message:", subscriber_id);
                println!("  Topic: {}", publish.topic);
                println!("  Payload: {}", payload);
                println!("  QoS: {:?}", publish.qos);
                
                // Simulate message processing
                sleep(Duration::from_secs(1)).await;
                println!("[Subscriber {}] Processing complete\n", subscriber_id);
            }
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("[Subscriber {}] Connection acknowledged", subscriber_id);
            }
            Ok(Event::Incoming(Packet::SubAck(_))) => {
                println!("[Subscriber {}] Subscription acknowledged", subscriber_id);
            }
            Err(e) => {
                eprintln!("[Subscriber {}] Error: {:?}", subscriber_id, e);
                sleep(Duration::from_secs(1)).await;
            }
            _ => {}
        }
    }
}

async fn publish_messages(count: u32) {
    let mut mqtt_options = MqttOptions::new("publisher", "localhost", 1883);
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(true);
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Spawn event loop handler
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Publisher event loop error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Wait for connection
    sleep(Duration::from_secs(1)).await;
    
    println!("Publisher connected, sending {} messages...\n", count);
    
    for i in 1..=count {
        let temperature = 20.0 + (i as f32 * 0.5);
        let payload = format!("Temperature reading #{}: {:.2}°C", i, temperature);
        
        client
            .publish(
                "sensors/temperature",
                QoS::AtLeastOnce,
                false,
                payload.as_bytes(),
            )
            .await
            .unwrap();
        
        println!("Published message #{}", i);
        sleep(Duration::from_millis(500)).await;
    }
    
    println!("\nAll messages published");
    sleep(Duration::from_secs(5)).await; // Allow time for processing
}
```

### Advanced Rust Example with Error Handling

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::sync::Arc;
use tokio::sync::Mutex;
use anyhow::Result;

struct SharedSubscriberConfig {
    subscriber_id: u32,
    broker_address: String,
    broker_port: u16,
    share_group: String,
    topic: String,
    qos: QoS,
}

struct MessageProcessor {
    subscriber_id: u32,
    processed_count: Arc<Mutex<u32>>,
}

impl MessageProcessor {
    fn new(subscriber_id: u32) -> Self {
        Self {
            subscriber_id,
            processed_count: Arc::new(Mutex::new(0)),
        }
    }
    
    async fn process_message(&self, topic: &str, payload: &[u8]) -> Result<()> {
        let message = String::from_utf8_lossy(payload);
        
        println!("[Subscriber {}] Processing message from topic: {}", 
                 self.subscriber_id, topic);
        println!("  Payload: {}", message);
        
        // Simulate processing work
        sleep(Duration::from_millis(800)).await;
        
        let mut count = self.processed_count.lock().await;
        *count += 1;
        
        println!("[Subscriber {}] Total processed: {}\n", 
                 self.subscriber_id, *count);
        
        Ok(())
    }
    
    async fn get_processed_count(&self) -> u32 {
        *self.processed_count.lock().await
    }
}

async fn run_shared_subscriber(config: SharedSubscriberConfig) -> Result<()> {
    let client_id = format!("shared_sub_{}_{}", config.share_group, config.subscriber_id);
    let shared_topic = format!("$share/{}/{}", config.share_group, config.topic);
    
    let mut mqtt_options = MqttOptions::new(
        client_id.clone(),
        &config.broker_address,
        config.broker_port,
    );
    mqtt_options.set_keep_alive(Duration::from_secs(30));
    mqtt_options.set_clean_session(true);
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 20);
    let processor = MessageProcessor::new(config.subscriber_id);
    
    // Subscribe to shared topic
    client.subscribe(&shared_topic, config.qos).await?;
    
    println!("[Subscriber {}] Subscribed to: {}", 
             config.subscriber_id, shared_topic);
    
    // Event loop
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                if let Err(e) = processor.process_message(
                    &publish.topic,
                    &publish.payload
                ).await {
                    eprintln!("[Subscriber {}] Processing error: {}", 
                              config.subscriber_id, e);
                }
            }
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("[Subscriber {}] Connected to broker", config.subscriber_id);
            }
            Err(e) => {
                eprintln!("[Subscriber {}] Connection error: {:?}", 
                          config.subscriber_id, e);
                sleep(Duration::from_secs(2)).await;
            }
            _ => {}
        }
    }
}
```

## Key Benefits and Use Cases

### Benefits
1. **Load Balancing**: Distribute message processing workload across multiple consumers
2. **Horizontal Scaling**: Add more subscribers to handle increased message volume
3. **Fault Tolerance**: If one subscriber fails, others continue processing messages
4. **Resource Efficiency**: Prevents duplicate processing of the same message
5. **Flexible Architecture**: Easily scale consumer count up or down based on demand

### Common Use Cases
- **IoT Data Processing**: Multiple workers processing sensor data streams
- **Order Processing Systems**: Distributing customer orders across multiple handlers
- **Log Aggregation**: Balancing log message processing across multiple analyzers
- **Task Queues**: Implementing job distribution systems
- **Microservices Architecture**: Load balancing work between service instances

## Summary

MQTT Shared Subscriptions provide an elegant solution for load balancing and horizontal scaling in message-driven architectures. By using the `$share/{group}/{topic}` syntax, multiple subscribers can form a consumer group where each message is delivered to only one member, enabling efficient distribution of processing workload.

The feature is particularly powerful in cloud-native and microservices environments where elastic scaling is essential. Whether implementing in C/C++ with Paho MQTT or Rust with rumqttc, the pattern remains consistent: create multiple clients subscribing to the same shared topic, and the broker handles intelligent message distribution.

Key considerations include choosing appropriate QoS levels (typically QoS 1 for reliable delivery), implementing proper error handling and reconnection logic, and monitoring individual subscriber performance to ensure balanced load distribution. When combined with MQTT's reliability features and flexible topic structure, shared subscriptions enable building robust, scalable distributed systems capable of handling high-throughput message processing scenarios.