# MQTT Sleeping Client Support

## Overview

Sleeping Client Support is a critical MQTT 5.0 feature designed for battery-powered IoT devices that need to conserve energy by entering sleep modes. These devices cannot maintain persistent connections or receive messages while sleeping, creating challenges for message delivery and session management.

## The Problem

Traditional MQTT clients maintain continuous connections to receive messages. However, battery-powered devices (sensors, wearables, remote monitors) must sleep periodically to conserve power. During sleep:
- Network connections are closed
- Messages cannot be received in real-time
- The device is unreachable
- Session state must be preserved

## MQTT 5.0 Solution

MQTT 5.0 introduces specific mechanisms to handle sleeping clients:

1. **Session Expiry Interval**: Keeps sessions alive during sleep periods
2. **Message Expiry Interval**: Prevents stale messages from being delivered
3. **Receive Maximum**: Controls message flow to prevent overwhelming clients
4. **Will Delay Interval**: Delays "last will" messages for planned disconnections

## Key Concepts

### Session Persistence
When a sleeping client disconnects, it requests the broker to maintain its session (subscriptions, QoS 1/2 messages) using a Session Expiry Interval longer than its expected sleep duration.

### Wake-Connect-Process-Sleep Cycle
1. **Wake**: Device powers up
2. **Connect**: Establishes MQTT connection with Clean Start = false
3. **Process**: Receives queued messages
4. **Disconnect**: Gracefully disconnects with Session Expiry Interval
5. **Sleep**: Enters low-power mode

---

## C/C++ Implementation (using Paho MQTT C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS    "tcp://broker.example.com:1883"
#define CLIENT_ID         "sleeping_sensor_001"
#define TOPIC            "sensors/temperature"
#define QOS              1
#define SLEEP_SECONDS    300  // 5 minutes sleep

// Session expiry: 1 hour (longer than sleep period)
#define SESSION_EXPIRY   3600

typedef struct {
    MQTTClient client;
    int messageCount;
} SleepingClientContext;

// Message arrival callback
int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    SleepingClientContext *ctx = (SleepingClientContext *)context;
    
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char *)message->payload);
    printf("QoS: %d, Retained: %d\n", message->qos, message->retained);
    
    ctx->messageCount++;
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Connection lost callback
void connectionLost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause ? cause : "unknown");
}

// Initialize sleeping client with session persistence
int initSleepingClient(SleepingClientContext *ctx) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_connectOptions5 conn_opts5 = MQTTClient_connectOptions5_initializer;
    MQTTProperties connect_properties = MQTTProperties_initializer;
    MQTTProperty property;
    int rc;
    
    // Create MQTT client
    rc = MQTTClient_create(&ctx->client, BROKER_ADDRESS, CLIENT_ID,
                           MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to create client, return code %d\n", rc);
        return rc;
    }
    
    // Set callbacks
    MQTTClient_setCallbacks(ctx->client, ctx, connectionLost, 
                           messageArrived, NULL);
    
    // Configure MQTT 5.0 connection options
    conn_opts5.MQTTVersion = MQTTVERSION_5;
    conn_opts5.cleanstart = 0;  // Resume existing session
    conn_opts5.keepAliveInterval = 60;
    
    // Set Session Expiry Interval property
    property.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    property.value.integer4 = SESSION_EXPIRY;
    MQTTProperties_add(&connect_properties, &property);
    
    conn_opts5.connectProperties = &connect_properties;
    
    // Connect to broker
    printf("Connecting to broker %s...\n", BROKER_ADDRESS);
    rc = MQTTClient_connect5(ctx->client, &conn_opts5, NULL, NULL);
    
    MQTTProperties_free(&connect_properties);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }
    
    printf("Connected successfully\n");
    return MQTTCLIENT_SUCCESS;
}

// Subscribe to topics
int subscribeTopics(SleepingClientContext *ctx) {
    MQTTClient_subscribeOptions sub_opts = MQTTClient_subscribeOptions_initializer;
    MQTTResponse response;
    
    printf("Subscribing to topic: %s\n", TOPIC);
    response = MQTTClient_subscribe5(ctx->client, TOPIC, QOS, &sub_opts, NULL);
    
    if (response.reasonCode != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, reason code %d\n", response.reasonCode);
        return response.reasonCode;
    }
    
    printf("Subscribed successfully\n");
    return MQTTCLIENT_SUCCESS;
}

// Publish sensor data
int publishData(SleepingClientContext *ctx, const char *data) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    MQTTProperties properties = MQTTProperties_initializer;
    MQTTProperty property;
    int rc;
    
    // Set message expiry to 30 minutes
    property.identifier = MQTTPROPERTY_CODE_MESSAGE_EXPIRY_INTERVAL;
    property.value.integer4 = 1800;
    MQTTProperties_add(&properties, &property);
    
    pubmsg.payload = (void *)data;
    pubmsg.payloadlen = strlen(data);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    pubmsg.properties = properties;
    
    rc = MQTTClient_publishMessage5(ctx->client, TOPIC, &pubmsg, &token);
    
    MQTTProperties_free(&properties);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        return rc;
    }
    
    printf("Waiting for publication...\n");
    rc = MQTTClient_waitForCompletion(ctx->client, token, 10000);
    printf("Message published successfully\n");
    
    return rc;
}

// Graceful disconnect with session preservation
int disconnectSleepingClient(SleepingClientContext *ctx) {
    MQTTClient_disconnectOptions disc_opts = MQTTClient_disconnectOptions_initializer;
    MQTTProperties disconnect_properties = MQTTProperties_initializer;
    MQTTProperty property;
    int rc;
    
    // Maintain session expiry on disconnect
    property.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    property.value.integer4 = SESSION_EXPIRY;
    MQTTProperties_add(&disconnect_properties, &property);
    
    disc_opts.properties = disconnect_properties;
    disc_opts.timeout = 10000;
    
    printf("Disconnecting...\n");
    rc = MQTTClient_disconnect5(ctx->client, &disc_opts);
    
    MQTTProperties_free(&disconnect_properties);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to disconnect, return code %d\n", rc);
        return rc;
    }
    
    printf("Disconnected successfully\n");
    return MQTTCLIENT_SUCCESS;
}

// Simulate sleep period
void simulateSleep(int seconds) {
    printf("Entering sleep mode for %d seconds...\n", seconds);
    sleep(seconds);
    printf("Waking up from sleep...\n");
}

int main(int argc, char *argv[]) {
    SleepingClientContext ctx = {0};
    int cycle = 0;
    
    while (1) {
        cycle++;
        printf("\n=== Sleep Cycle %d ===\n", cycle);
        
        // Wake and connect
        if (initSleepingClient(&ctx) != MQTTCLIENT_SUCCESS) {
            break;
        }
        
        // Subscribe (only needed on first connection)
        if (cycle == 1) {
            if (subscribeTopics(&ctx) != MQTTCLIENT_SUCCESS) {
                break;
            }
        }
        
        // Process any queued messages
        printf("Processing messages for 10 seconds...\n");
        ctx.messageCount = 0;
        sleep(10);  // Allow time for message processing
        printf("Processed %d messages\n", ctx.messageCount);
        
        // Publish sensor reading
        char data[64];
        snprintf(data, sizeof(data), "{\"temperature\":%.1f,\"cycle\":%d}", 
                 20.0 + (rand() % 100) / 10.0, cycle);
        publishData(&ctx, data);
        
        // Disconnect and sleep
        disconnectSleepingClient(&ctx);
        MQTTClient_destroy(&ctx.client);
        
        simulateSleep(SLEEP_SECONDS);
    }
    
    return 0;
}
```

---

## Rust Implementation (using rumqttc)

```rust
use rumqttc::{
    AsyncClient, Event, EventLoop, Incoming, MqttOptions, Packet, QoS,
    ConnectReturnCode, ConnectionError
};
use tokio::time::{sleep, Duration};
use serde::{Deserialize, Serialize};
use std::error::Error;

const BROKER_HOST: &str = "broker.example.com";
const BROKER_PORT: u16 = 1883;
const CLIENT_ID: &str = "sleeping_sensor_rust_001";
const TOPIC: &str = "sensors/temperature";
const SLEEP_DURATION_SECS: u64 = 300; // 5 minutes
const SESSION_EXPIRY_SECS: u32 = 3600; // 1 hour

#[derive(Serialize, Deserialize, Debug)]
struct SensorData {
    temperature: f32,
    humidity: f32,
    cycle: u32,
    timestamp: u64,
}

struct SleepingClient {
    client: AsyncClient,
    eventloop: EventLoop,
    message_count: u32,
}

impl SleepingClient {
    /// Create new sleeping client with session persistence
    fn new() -> Result<Self, Box<dyn Error>> {
        let mut mqttoptions = MqttOptions::new(CLIENT_ID, BROKER_HOST, BROKER_PORT);
        
        // Configure for sleeping client
        mqttoptions.set_keep_alive(Duration::from_secs(60));
        mqttoptions.set_clean_session(false); // Persist session
        mqttoptions.set_session_expiry_interval(Some(SESSION_EXPIRY_SECS));
        
        // Optional: Set receive maximum to control message flow
        mqttoptions.set_receive_maximum(Some(10));
        
        // Create client and event loop
        let (client, eventloop) = AsyncClient::new(mqttoptions, 10);
        
        Ok(SleepingClient {
            client,
            eventloop,
            message_count: 0,
        })
    }
    
    /// Subscribe to topics
    async fn subscribe(&self) -> Result<(), Box<dyn Error>> {
        println!("Subscribing to topic: {}", TOPIC);
        self.client.subscribe(TOPIC, QoS::AtLeastOnce).await?;
        println!("Subscribed successfully");
        Ok(())
    }
    
    /// Publish sensor data with message expiry
    async fn publish_data(&self, data: &SensorData) -> Result<(), Box<dyn Error>> {
        let payload = serde_json::to_string(data)?;
        
        println!("Publishing sensor data: {}", payload);
        
        // Note: rumqttc doesn't directly support message expiry interval
        // You would need to use a lower-level client or add custom properties
        self.client
            .publish(TOPIC, QoS::AtLeastOnce, false, payload.as_bytes())
            .await?;
        
        println!("Data published successfully");
        Ok(())
    }
    
    /// Process messages for a specified duration
    async fn process_messages(&mut self, duration_secs: u64) -> Result<(), Box<dyn Error>> {
        println!("Processing messages for {} seconds...", duration_secs);
        
        let start = std::time::Instant::now();
        self.message_count = 0;
        
        loop {
            // Check if time limit reached
            if start.elapsed().as_secs() >= duration_secs {
                break;
            }
            
            // Poll for events with timeout
            match tokio::time::timeout(
                Duration::from_secs(1),
                self.eventloop.poll()
            ).await {
                Ok(Ok(event)) => {
                    self.handle_event(event);
                }
                Ok(Err(e)) => {
                    eprintln!("Event loop error: {:?}", e);
                    if matches!(e, ConnectionError::Io(_)) {
                        break;
                    }
                }
                Err(_) => {
                    // Timeout, continue
                }
            }
        }
        
        println!("Processed {} messages", self.message_count);
        Ok(())
    }
    
    /// Handle incoming MQTT events
    fn handle_event(&mut self, event: Event) {
        match event {
            Event::Incoming(Incoming::Publish(p)) => {
                self.message_count += 1;
                println!(
                    "Message #{} received on topic: {}",
                    self.message_count, p.topic
                );
                
                if let Ok(payload) = std::str::from_utf8(&p.payload) {
                    println!("Payload: {}", payload);
                }
                println!("QoS: {:?}, Retain: {}", p.qos, p.retain);
            }
            Event::Incoming(Incoming::ConnAck(ack)) => {
                println!("Connected! Session present: {}", ack.session_present);
            }
            Event::Incoming(Incoming::SubAck(_)) => {
                println!("Subscription acknowledged");
            }
            Event::Incoming(Incoming::PubAck(_)) => {
                println!("Publish acknowledged");
            }
            Event::Outgoing(_) => {
                // Ignore outgoing events for brevity
            }
            _ => {
                // Handle other events as needed
            }
        }
    }
    
    /// Gracefully disconnect
    async fn disconnect(&self) -> Result<(), Box<dyn Error>> {
        println!("Disconnecting...");
        self.client.disconnect().await?;
        println!("Disconnected successfully");
        Ok(())
    }
}

/// Simulate sleep period
async fn simulate_sleep(seconds: u64) {
    println!("Entering sleep mode for {} seconds...", seconds);
    sleep(Duration::from_secs(seconds)).await;
    println!("Waking up from sleep...");
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let mut cycle = 0u32;
    
    loop {
        cycle += 1;
        println!("\n=== Sleep Cycle {} ===", cycle);
        
        // Create sleeping client
        let mut client = SleepingClient::new()?;
        
        // Subscribe on first connection
        if cycle == 1 {
            client.subscribe().await?;
        }
        
        // Process queued messages
        client.process_messages(10).await?;
        
        // Publish sensor reading
        let sensor_data = SensorData {
            temperature: 20.0 + (rand::random::<f32>() * 10.0),
            humidity: 40.0 + (rand::random::<f32>() * 20.0),
            cycle,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
        };
        
        client.publish_data(&sensor_data).await?;
        
        // Disconnect
        client.disconnect().await?;
        
        // Sleep period
        simulate_sleep(SLEEP_DURATION_SECS).await;
    }
}
```

---

## Summary

**Sleeping Client Support** enables battery-powered IoT devices to use MQTT efficiently by:

- **Session Persistence**: Maintaining subscriptions and QoS state during sleep using Session Expiry Intervals (typically hours)
- **Message Queuing**: Brokers queue QoS 1/2 messages for delivery when clients reconnect
- **Message Expiry**: Preventing stale messages using Message Expiry Intervals (typically minutes)
- **Graceful Sleep Cycles**: Wake → Connect (Clean Start=false) → Process → Publish → Disconnect → Sleep pattern

**Key Benefits:**
- Dramatic power savings (months/years on battery vs. days)
- Reliable message delivery despite intermittent connectivity
- Scalable solution for massive IoT deployments
- No message loss for important QoS 1/2 messages

**Implementation Considerations:**
- Session Expiry must exceed maximum sleep duration
- Clean Start = false on reconnection to resume sessions
- Use appropriate QoS levels (QoS 1/2 for critical messages)
- Configure receive maximum to prevent overwhelming clients on wake
- Handle potential message backlogs gracefully

This feature is essential for battery-powered sensors, wearables, remote monitors, and any IoT device requiring energy efficiency while maintaining reliable MQTT communication.