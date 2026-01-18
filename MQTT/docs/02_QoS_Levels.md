# MQTT Quality of Service (QoS) Levels

## Overview

Quality of Service (QoS) is a fundamental concept in MQTT that defines the guarantee of message delivery between a client and broker. MQTT provides three QoS levels (0, 1, and 2) that offer different trade-offs between reliability, bandwidth usage, and latency. Understanding these levels is crucial for designing robust IoT applications.

## The Three QoS Levels

### QoS 0: At Most Once Delivery
- **Guarantee**: The message is delivered at most once, or it may not be delivered at all
- **Mechanism**: Fire-and-forget; no acknowledgment required
- **Use Cases**: Sensor data where occasional loss is acceptable (e.g., temperature readings sent frequently)
- **Overhead**: Minimal - single message transmission
- **Delivery Guarantee**: No guarantee of delivery

### QoS 1: At Least Once Delivery
- **Guarantee**: The message is delivered at least once, but duplicates may occur
- **Mechanism**: Sender stores message until PUBACK acknowledgment is received
- **Use Cases**: Important data where duplicates can be handled (e.g., notifications with idempotency)
- **Overhead**: Moderate - requires acknowledgment (PUBACK)
- **Delivery Guarantee**: Guaranteed delivery, possible duplicates

### QoS 2: Exactly Once Delivery
- **Guarantee**: The message is delivered exactly once, no duplicates
- **Mechanism**: Four-way handshake (PUBLISH → PUBREC → PUBREL → PUBCOMP)
- **Use Cases**: Critical data where duplicates would cause issues (e.g., billing transactions, commands)
- **Overhead**: Highest - requires four-message handshake
- **Delivery Guarantee**: Guaranteed exactly-once delivery

## Message Flow Diagrams

**QoS 0 Flow:**
```
Client → PUBLISH → Broker
(No acknowledgment)
```

**QoS 1 Flow:**
```
Client → PUBLISH → Broker
Client ← PUBACK ← Broker
```

**QoS 2 Flow:**
```
Client → PUBLISH → Broker
Client ← PUBREC ← Broker
Client → PUBREL → Broker
Client ← PUBCOMP ← Broker
```

## Code Examples

### C/C++ Implementation (Using Eclipse Paho MQTT C Library)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "ExampleClientPub"
#define TOPIC       "sensors/temperature"
#define PAYLOAD     "25.5"
#define TIMEOUT     10000L

// Demonstration of publishing with different QoS levels
void publish_with_qos(MQTTClient client, const char* topic, 
                      const char* payload, int qos) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = qos;  // Set QoS level (0, 1, or 2)
    pubmsg.retained = 0;

    rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        return;
    }

    printf("Message published with QoS %d\n", qos);

    // Wait for delivery confirmation (important for QoS 1 and 2)
    if (qos > 0) {
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("Message with token %d delivered\n", token);
    }
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Publish with QoS 0 - Fire and forget
    printf("\n--- Publishing with QoS 0 ---\n");
    publish_with_qos(client, TOPIC, "QoS 0 message", 0);

    // Publish with QoS 1 - At least once
    printf("\n--- Publishing with QoS 1 ---\n");
    publish_with_qos(client, TOPIC, "QoS 1 message", 1);

    // Publish with QoS 2 - Exactly once
    printf("\n--- Publishing with QoS 2 ---\n");
    publish_with_qos(client, TOPIC, "QoS 2 message", 2);

    // Disconnect
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);

    return rc;
}
```

**Subscriber Example in C:**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "ExampleClientSub"
#define TOPIC       "sensors/#"
#define QOS         1  // Subscription QoS level
#define TIMEOUT     10000L

volatile int finished = 0;

// Message arrived callback
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("\n--- Message Arrived ---\n");
    printf("Topic: %s\n", topicName);
    printf("QoS: %d\n", message->qos);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("Duplicate: %s\n", message->dup ? "Yes" : "No");
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("\nConnection lost: %s\n", cause);
    finished = 1;
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Subscribing to topic %s with QoS %d\n", TOPIC, QOS);
    MQTTClient_subscribe(client, TOPIC, QOS);

    // Wait for messages
    while (!finished) {
        #ifdef _WIN32
            Sleep(100);
        #else
            usleep(100000);
        #endif
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

### Rust Implementation (Using rumqttc Library)

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;

fn main() {
    // Publisher example demonstrating all QoS levels
    publisher_example();
    
    // Give publisher time to connect
    thread::sleep(Duration::from_secs(1));
    
    // Subscriber example
    subscriber_example();
}

fn publisher_example() {
    thread::spawn(|| {
        // Create MQTT options
        let mut mqttoptions = MqttOptions::new(
            "rust-publisher", 
            "broker.hivemq.com", 
            1883
        );
        
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        
        // Create client
        let (client, mut connection) = Client::new(mqttoptions, 10);
        
        // Spawn thread to handle connection events
        thread::spawn(move || {
            for (i, notification) in connection.iter().enumerate() {
                match notification {
                    Ok(Event::Incoming(Packet::PubAck(ack))) => {
                        println!("QoS 1 - PUBACK received for packet: {}", ack.pkid);
                    }
                    Ok(Event::Incoming(Packet::PubRec(rec))) => {
                        println!("QoS 2 - PUBREC received for packet: {}", rec.pkid);
                    }
                    Ok(Event::Incoming(Packet::PubComp(comp))) => {
                        println!("QoS 2 - PUBCOMP received for packet: {}", comp.pkid);
                    }
                    Ok(Event::Outgoing(_)) => {
                        // Outgoing packet sent
                    }
                    Err(e) => {
                        eprintln!("Connection error: {:?}", e);
                        break;
                    }
                    _ => {}
                }
            }
        });
        
        // Wait for connection
        thread::sleep(Duration::from_secs(1));
        
        // Publish with QoS 0 - At most once
        println!("\n--- Publishing with QoS 0 (At Most Once) ---");
        client.publish(
            "sensors/temperature/qos0",
            QoS::AtMostOnce,
            false,
            "Temperature: 22.5°C (QoS 0)"
        ).unwrap();
        
        thread::sleep(Duration::from_millis(500));
        
        // Publish with QoS 1 - At least once
        println!("\n--- Publishing with QoS 1 (At Least Once) ---");
        client.publish(
            "sensors/temperature/qos1",
            QoS::AtLeastOnce,
            false,
            "Temperature: 23.1°C (QoS 1)"
        ).unwrap();
        
        thread::sleep(Duration::from_millis(500));
        
        // Publish with QoS 2 - Exactly once
        println!("\n--- Publishing with QoS 2 (Exactly Once) ---");
        client.publish(
            "sensors/temperature/qos2",
            QoS::ExactlyOnce,
            false,
            "Temperature: 24.7°C (QoS 2)"
        ).unwrap();
        
        // Keep thread alive to process acknowledgments
        thread::sleep(Duration::from_secs(3));
    });
}

fn subscriber_example() {
    thread::spawn(|| {
        let mut mqttoptions = MqttOptions::new(
            "rust-subscriber",
            "broker.hivemq.com",
            1883
        );
        
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut connection) = Client::new(mqttoptions, 10);
        
        // Subscribe to topics with different QoS levels
        client.subscribe("sensors/temperature/qos0", QoS::AtMostOnce).unwrap();
        client.subscribe("sensors/temperature/qos1", QoS::AtLeastOnce).unwrap();
        client.subscribe("sensors/temperature/qos2", QoS::ExactlyOnce).unwrap();
        
        println!("\n--- Subscriber waiting for messages ---");
        
        // Process incoming messages
        for notification in connection.iter() {
            match notification {
                Ok(Event::Incoming(Packet::Publish(publish))) => {
                    let topic = publish.topic.clone();
                    let payload = String::from_utf8_lossy(&publish.payload);
                    let qos = publish.qos;
                    let dup = publish.dup;
                    
                    println!("\n📨 Message Received:");
                    println!("   Topic: {}", topic);
                    println!("   QoS: {:?}", qos);
                    println!("   Payload: {}", payload);
                    println!("   Duplicate: {}", dup);
                }
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("✓ Connected to broker");
                }
                Ok(Event::Incoming(Packet::SubAck(suback))) => {
                    println!("✓ Subscription acknowledged: {:?}", suback);
                }
                Err(e) => {
                    eprintln!("❌ Connection error: {:?}", e);
                    break;
                }
                _ => {}
            }
        }
    });
    
    // Keep main thread alive
    thread::sleep(Duration::from_secs(10));
}
```

**Advanced Rust Example with Error Handling:**

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;
use std::sync::{Arc, Mutex};

#[derive(Debug)]
struct MessageStats {
    qos0_sent: u32,
    qos1_sent: u32,
    qos2_sent: u32,
    qos1_acked: u32,
    qos2_completed: u32,
}

impl MessageStats {
    fn new() -> Self {
        MessageStats {
            qos0_sent: 0,
            qos1_sent: 0,
            qos2_sent: 0,
            qos1_acked: 0,
            qos2_completed: 0,
        }
    }
}

fn advanced_publisher_with_stats() {
    let stats = Arc::new(Mutex::new(MessageStats::new()));
    let stats_clone = Arc::clone(&stats);
    
    let mut mqttoptions = MqttOptions::new(
        "rust-advanced-pub",
        "broker.hivemq.com",
        1883
    );
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut connection) = Client::new(mqttoptions, 10);
    
    // Event processing thread
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(Event::Incoming(Packet::PubAck(_))) => {
                    let mut stats = stats_clone.lock().unwrap();
                    stats.qos1_acked += 1;
                    println!("✓ QoS 1 acknowledged (Total: {})", stats.qos1_acked);
                }
                Ok(Event::Incoming(Packet::PubComp(_))) => {
                    let mut stats = stats_clone.lock().unwrap();
                    stats.qos2_completed += 1;
                    println!("✓ QoS 2 completed (Total: {})", stats.qos2_completed);
                }
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("✓ Connected to broker");
                }
                Err(e) => {
                    eprintln!("❌ Error: {:?}", e);
                    break;
                }
                _ => {}
            }
        }
    });
    
    thread::sleep(Duration::from_secs(1));
    
    // Send messages with different QoS levels
    for i in 0..5 {
        // QoS 0
        if let Err(e) = client.publish(
            "test/qos0",
            QoS::AtMostOnce,
            false,
            format!("QoS 0 message {}", i)
        ) {
            eprintln!("Failed to publish QoS 0: {:?}", e);
        } else {
            stats.lock().unwrap().qos0_sent += 1;
        }
        
        // QoS 1
        if let Err(e) = client.publish(
            "test/qos1",
            QoS::AtLeastOnce,
            false,
            format!("QoS 1 message {}", i)
        ) {
            eprintln!("Failed to publish QoS 1: {:?}", e);
        } else {
            stats.lock().unwrap().qos1_sent += 1;
        }
        
        // QoS 2
        if let Err(e) = client.publish(
            "test/qos2",
            QoS::ExactlyOnce,
            false,
            format!("QoS 2 message {}", i)
        ) {
            eprintln!("Failed to publish QoS 2: {:?}", e);
        } else {
            stats.lock().unwrap().qos2_sent += 1;
        }
        
        thread::sleep(Duration::from_millis(200));
    }
    
    thread::sleep(Duration::from_secs(2));
    
    // Print statistics
    let final_stats = stats.lock().unwrap();
    println!("\n📊 Final Statistics:");
    println!("   QoS 0 sent: {}", final_stats.qos0_sent);
    println!("   QoS 1 sent: {} (Acked: {})", 
             final_stats.qos1_sent, final_stats.qos1_acked);
    println!("   QoS 2 sent: {} (Completed: {})", 
             final_stats.qos2_sent, final_stats.qos2_completed);
}
```

## QoS Negotiation

An important aspect of MQTT QoS is that the **effective QoS is the minimum** of:
1. The QoS specified by the publisher
2. The QoS requested by the subscriber

**Example:**
- Publisher sends with QoS 2
- Subscriber subscribes with QoS 1
- **Result**: Messages delivered with QoS 1

This allows subscribers to reduce bandwidth/overhead if they don't need the highest delivery guarantee.

## Performance Considerations

| Aspect | QoS 0 | QoS 1 | QoS 2 |
|--------|-------|-------|-------|
| Network overhead | Lowest | Medium | Highest |
| Latency | Lowest | Medium | Highest |
| Bandwidth | Minimal | Moderate | Maximum |
| Battery impact (IoT) | Minimal | Moderate | Significant |
| Storage requirements | None | Minimal | Moderate |

## Best Practices

1. **Choose the right QoS for your use case**: Don't default to QoS 2 for everything
2. **Consider network reliability**: On stable networks, QoS 0 may be sufficient
3. **Handle duplicates at application level**: Even with QoS 1, design for idempotency
4. **Monitor delivery**: Track acknowledgments for critical messages
5. **Balance trade-offs**: Higher QoS means higher latency and resource usage
6. **Use retained messages wisely**: Combine with appropriate QoS for last-known-good values
7. **Test failure scenarios**: Verify behavior during disconnections and reconnections

## Summary

MQTT's Quality of Service levels provide flexible delivery guarantees to match application requirements. **QoS 0** offers fire-and-forget messaging suitable for high-frequency, loss-tolerant data like sensor readings. **QoS 1** guarantees at-least-once delivery with a single acknowledgment, ideal for important messages where occasional duplicates can be handled. **QoS 2** ensures exactly-once delivery through a four-way handshake, necessary for critical operations like financial transactions or device commands where duplicates would cause problems.

The choice of QoS level involves trade-offs between reliability, network overhead, latency, and resource consumption. Understanding these trade-offs and the message flow for each level enables developers to build efficient, reliable MQTT applications. The QoS level is negotiated between publisher and subscriber, with the effective QoS being the minimum of the two, allowing flexibility in system design. Proper implementation of QoS handling, as demonstrated in the C/C++ and Rust examples above, is essential for robust IoT and messaging applications.