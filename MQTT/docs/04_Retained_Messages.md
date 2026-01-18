# MQTT Retained Messages: A Comprehensive Guide

## Overview

Retained messages are a powerful MQTT feature that allows the broker to store the last message published on a topic and automatically deliver it to new subscribers. This mechanism is essential for maintaining state information and ensuring that clients can immediately access the current status of a system without waiting for the next update.

## How Retained Messages Work

When a publisher sends a message with the "retain" flag set to true, the MQTT broker stores that message along with its topic. Whenever a new client subscribes to that topic (or a matching wildcard pattern), the broker immediately sends the retained message to that subscriber, even if the original publisher is no longer connected.

### Key Characteristics

**Single Message Per Topic**: Only one retained message is stored per topic. Publishing a new retained message overwrites the previous one.

**Persistence**: Retained messages typically persist across broker restarts, though this depends on broker configuration.

**Deletion**: Publishing an empty payload with the retain flag set removes the retained message from that topic.

**QoS Preservation**: The retained message maintains its original Quality of Service level when delivered to new subscribers.

## Use Cases for Retained Messages

Retained messages excel in scenarios requiring state persistence:

**Device Status**: Broadcasting whether a device is online/offline, so new monitoring applications immediately know the current state.

**Configuration Data**: Publishing system configuration that new clients need immediately upon connection.

**Last Known Values**: Sensor readings where new subscribers should receive the most recent measurement without waiting.

**Presence Information**: User availability status in chat applications or IoT device presence.

## C/C++ Implementation

Using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "RetainedMessagePublisher"
#define TOPIC       "home/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Publishing a retained message
int publish_retained_message(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    
    char payload[50];
    snprintf(payload, sizeof(payload), "22.5");
    
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 1;  // Set retained flag
    
    rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        return rc;
    }
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Retained message published successfully\n");
    return rc;
}

// Deleting a retained message
int delete_retained_message(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    pubmsg.payload = "";  // Empty payload
    pubmsg.payloadlen = 0;
    pubmsg.qos = QOS;
    pubmsg.retained = 1;  // Retain flag must be set
    
    int rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Retained message deleted\n");
    return rc;
}

// Subscriber receiving retained messages
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("Message arrived\n");
    printf("Topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("Retained: %s\n", message->retained ? "Yes" : "No");
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    MQTTClient_create(&client, ADDRESS, CLIENTID, 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    MQTTClient_setCallbacks(client, NULL, NULL, message_arrived, NULL);
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    // Publish retained message
    publish_retained_message(client);
    
    // Subscribe to see retained message delivery
    MQTTClient_subscribe(client, TOPIC, QOS);
    
    // Wait to receive messages
    getchar();
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

## Rust Implementation

Using the `paho-mqtt` crate:

```rust
use paho_mqtt as mqtt;
use std::time::Duration;
use std::thread;

const BROKER_ADDRESS: &str = "tcp://localhost:1883";
const CLIENT_ID: &str = "rust_retained_client";
const TOPIC: &str = "home/temperature";
const QOS: i32 = 1;

// Publisher with retained messages
fn publish_retained_example() -> mqtt::Result<()> {
    // Create client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri(BROKER_ADDRESS)
        .client_id(CLIENT_ID)
        .finalize();
    
    let client = mqtt::Client::new(create_opts)?;
    
    // Connect options
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .finalize();
    
    println!("Connecting to MQTT broker...");
    client.connect(conn_opts)?;
    println!("Connected!");
    
    // Create a retained message
    let msg = mqtt::MessageBuilder::new()
        .topic(TOPIC)
        .payload("23.7")
        .qos(QOS)
        .retained(true)  // Set retained flag
        .finalize();
    
    println!("Publishing retained message...");
    client.publish(msg)?;
    println!("Retained message published");
    
    // Delete retained message by publishing empty payload
    let delete_msg = mqtt::MessageBuilder::new()
        .topic(TOPIC)
        .payload("")
        .qos(QOS)
        .retained(true)
        .finalize();
    
    thread::sleep(Duration::from_secs(2));
    println!("Deleting retained message...");
    client.publish(delete_msg)?;
    println!("Retained message deleted");
    
    client.disconnect(None)?;
    Ok(())
}

// Subscriber that receives retained messages
fn subscribe_retained_example() -> mqtt::Result<()> {
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri(BROKER_ADDRESS)
        .client_id("rust_subscriber")
        .finalize();
    
    let client = mqtt::Client::new(create_opts)?;
    
    // Set up message callback
    let rx = client.start_consuming();
    
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .finalize();
    
    client.connect(conn_opts)?;
    println!("Subscriber connected");
    
    // Subscribe to topic
    client.subscribe(TOPIC, QOS)?;
    println!("Subscribed to {}", TOPIC);
    println!("Waiting for messages (retained messages arrive immediately)...");
    
    // Receive messages
    for msg in rx.iter() {
        if let Some(msg) = msg {
            println!("\n=== Message Received ===");
            println!("Topic: {}", msg.topic());
            println!("Payload: {}", msg.payload_str());
            println!("QoS: {}", msg.qos());
            println!("Retained: {}", msg.retained());
            println!("=====================\n");
        } else {
            // Disconnection notification
            if !client.is_connected() {
                println!("Lost connection to broker");
                break;
            }
        }
    }
    
    client.disconnect(None)?;
    Ok(())
}

// Complete example with state management
struct TemperatureSensor {
    client: mqtt::Client,
    topic: String,
}

impl TemperatureSensor {
    fn new(broker: &str, client_id: &str, topic: &str) -> mqtt::Result<Self> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(false)
            .finalize();
        
        client.connect(conn_opts)?;
        
        Ok(TemperatureSensor {
            client,
            topic: topic.to_string(),
        })
    }
    
    // Publish current temperature as retained message
    fn publish_temperature(&self, temperature: f32) -> mqtt::Result<()> {
        let payload = format!("{:.1}", temperature);
        let msg = mqtt::MessageBuilder::new()
            .topic(&self.topic)
            .payload(payload)
            .qos(1)
            .retained(true)  // Always retained for state persistence
            .finalize();
        
        self.client.publish(msg)?;
        println!("Published temperature: {:.1}°C (retained)", temperature);
        Ok(())
    }
    
    // Publish device offline status
    fn publish_offline(&self) -> mqtt::Result<()> {
        let status_topic = format!("{}/status", self.topic);
        let msg = mqtt::MessageBuilder::new()
            .topic(status_topic)
            .payload("offline")
            .qos(1)
            .retained(true)
            .finalize();
        
        self.client.publish(msg)?;
        Ok(())
    }
}

fn main() -> mqtt::Result<()> {
    println!("=== MQTT Retained Messages Example ===\n");
    
    // Example 1: Basic publish/subscribe
    println!("1. Publishing retained message...");
    publish_retained_example()?;
    
    thread::sleep(Duration::from_secs(2));
    
    // Example 2: Subscriber receives retained message immediately
    println!("\n2. Starting subscriber (will receive retained message)...");
    // Run subscriber in separate thread or process
    
    // Example 3: Temperature sensor with state persistence
    println!("\n3. Temperature sensor example...");
    let sensor = TemperatureSensor::new(
        BROKER_ADDRESS,
        "temp_sensor_01",
        "sensors/temperature/room1"
    )?;
    
    sensor.publish_temperature(22.5)?;
    thread::sleep(Duration::from_secs(1));
    sensor.publish_temperature(23.1)?;
    
    Ok(())
}
```

## Best Practices

**Use Sparingly**: Retained messages consume broker memory. Don't retain every message, only state information that new subscribers genuinely need immediately.

**Clear Old Messages**: When a device goes offline permanently or a topic is no longer needed, delete the retained message by publishing an empty payload with the retain flag.

**Consider Message Age**: Retained messages can be old. Include timestamps in payloads so subscribers can determine data freshness.

**QoS Selection**: Choose appropriate QoS levels. QoS 1 is often sufficient for retained messages as they're re-delivered on subscription anyway.

**Wildcard Subscriptions**: Be aware that subscribing to wildcards (like `sensors/#`) will deliver all retained messages matching that pattern, which could be many messages.

## Summary

Retained messages are a crucial MQTT feature for maintaining system state and ensuring new subscribers receive current information immediately upon connection. By setting the retain flag when publishing, the broker stores the message and automatically delivers it to future subscribers. This is particularly valuable for status information, configuration data, and last-known values in IoT systems. The feature works across both C/C++ and Rust implementations using their respective MQTT libraries, with simple flag settings controlling retention behavior. Proper use requires understanding that only one message per topic is retained, messages can be deleted with empty payloads, and retention should be used judiciously to avoid unnecessary broker memory consumption. This mechanism bridges the gap between publish/subscribe's ephemeral nature and the need for persistent state in distributed systems.