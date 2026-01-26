# HiveMQ Configuration: Professional MQTT Broker Setup

## Overview

HiveMQ is an enterprise-grade MQTT broker designed for mission-critical IoT applications. It provides high availability, horizontal scalability, and extensive customization through extensions. Unlike lightweight brokers like Mosquitto, HiveMQ is built for large-scale deployments requiring advanced features like clustering, security integrations, and real-time monitoring.

## Key Features

**Enterprise Capabilities:**
- Cluster support for high availability and load distribution
- Extension framework for custom business logic
- Advanced security with authentication/authorization plugins
- Data governance and compliance features
- WebSocket support for browser-based clients
- Quality of Service (QoS) 0, 1, and 2 support

**Performance & Scalability:**
- Handles millions of concurrent connections
- Horizontal scaling across multiple nodes
- Optimized message routing and persistence
- Low-latency message delivery

## Configuration Structure

HiveMQ uses XML-based configuration files located in the `conf/` directory:

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<hivemq>
    <listeners>
        <tcp-listener>
            <port>1883</port>
            <bind-address>0.0.0.0</bind-address>
        </tcp-listener>
        
        <tls-tcp-listener>
            <port>8883</port>
            <bind-address>0.0.0.0</bind-address>
            <tls>
                <keystore>
                    <path>/path/to/keystore.jks</path>
                    <password>changeit</password>
                    <private-key-password>changeit</private-key-password>
                </keystore>
                <truststore>
                    <path>/path/to/truststore.jks</path>
                    <password>changeit</password>
                </truststore>
                <client-authentication-mode>REQUIRED</client-authentication-mode>
            </tls>
        </tls-tcp-listener>
        
        <websocket-listener>
            <port>8000</port>
            <bind-address>0.0.0.0</bind-address>
            <path>/mqtt</path>
        </websocket-listener>
    </listeners>

    <mqtt>
        <max-client-id-length>65535</max-client-id-length>
        <message-expiry>
            <max-interval>4294967296</max-interval>
        </message-expiry>
        <queued-messages>
            <max-queue-size>1000</max-queue-size>
        </queued-messages>
    </mqtt>

    <restrictions>
        <max-connections>-1</max-connections>
        <max-topic-length>65535</max-topic-length>
    </restrictions>
</hivemq>
```

## C/C++ Client Implementation

Using the Eclipse Paho MQTT C library to connect to HiveMQ:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "HiveMQClient_C"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Callback for message arrival
int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Connection lost callback
void connectionLost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connectionLost, 
                           messageArrived, NULL);

    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "mqttuser";
    conn_opts.password = "mqttpass";

    // Connect to HiveMQ
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    printf("Connected to HiveMQ broker\n");

    // Subscribe to topic
    MQTTClient_subscribe(client, TOPIC, QOS);

    // Publish a message
    char payload[100];
    snprintf(payload, sizeof(payload), "{\"temperature\": 22.5, \"unit\": \"C\"}");
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    printf("Waiting for publication of %s\n", payload);
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message with delivery token %d delivered\n", token);

    // Keep running to receive messages
    printf("Waiting for messages (press Ctrl+C to exit)...\n");
    while(1) {
        // In a real application, you'd have proper exit handling
        MQTTClient_yield();
    }

    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

**C++ Implementation with Modern Features:**

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <mqtt/async_client.h>

const std::string SERVER_ADDRESS{"tcp://localhost:1883"};
const std::string CLIENT_ID{"HiveMQClient_CPP"};
const std::string TOPIC{"sensors/temperature"};
const int QOS = 1;

class Callback : public virtual mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "Connection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "Topic: " << msg->get_topic() << std::endl;
        std::cout << "Payload: " << msg->to_string() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override {
        std::cout << "Delivery complete for token: " 
                  << (tok ? tok->get_message_id() : -1) << std::endl;
    }
};

int main() {
    try {
        // Create MQTT client
        mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
        
        // Set callback
        Callback cb;
        client.set_callback(cb);

        // Connection options with TLS
        auto connOpts = mqtt::connect_options_builder()
            .keep_alive_interval(std::chrono::seconds(20))
            .clean_session(true)
            .user_name("mqttuser")
            .password("mqttpass")
            .finalize();

        // Connect to HiveMQ
        std::cout << "Connecting to HiveMQ broker..." << std::endl;
        auto tok = client.connect(connOpts);
        tok->wait();
        std::cout << "Connected!" << std::endl;

        // Subscribe to topic
        client.subscribe(TOPIC, QOS)->wait();
        std::cout << "Subscribed to: " << TOPIC << std::endl;

        // Publish message
        std::string payload = R"({"temperature": 22.5, "unit": "C"})";
        auto pubmsg = mqtt::make_message(TOPIC, payload);
        pubmsg->set_qos(QOS);
        client.publish(pubmsg)->wait();
        std::cout << "Message published" << std::endl;

        // Wait for messages
        std::cout << "Waiting for messages... (press Enter to exit)" << std::endl;
        std::cin.get();

        // Disconnect
        client.disconnect()->wait();
        std::cout << "Disconnected" << std::endl;

    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Rust Client Implementation

Using the `paho-mqtt` crate for Rust:

```rust
use paho_mqtt as mqtt;
use std::time::Duration;
use std::thread;

const BROKER_ADDRESS: &str = "tcp://localhost:1883";
const CLIENT_ID: &str = "HiveMQClient_Rust";
const TOPIC: &str = "sensors/temperature";
const QOS: i32 = 1;

fn main() {
    // Create MQTT client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri(BROKER_ADDRESS)
        .client_id(CLIENT_ID)
        .finalize();

    let mut client = mqtt::Client::new(create_opts)
        .expect("Failed to create client");

    // Set up callback for incoming messages
    let rx = client.start_consuming();

    // Connection options
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .user_name("mqttuser")
        .password("mqttpass")
        .finalize();

    // Connect to HiveMQ
    println!("Connecting to HiveMQ broker at {}", BROKER_ADDRESS);
    match client.connect(conn_opts) {
        Ok(_) => println!("Connected successfully!"),
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            return;
        }
    }

    // Subscribe to topic
    println!("Subscribing to topic: {}", TOPIC);
    client.subscribe(TOPIC, QOS)
        .expect("Failed to subscribe");

    // Publish a message
    let payload = r#"{"temperature": 22.5, "unit": "C"}"#;
    let msg = mqtt::MessageBuilder::new()
        .topic(TOPIC)
        .payload(payload)
        .qos(QOS)
        .finalize();

    println!("Publishing message: {}", payload);
    client.publish(msg).expect("Failed to publish");

    // Spawn thread to handle incoming messages
    let handle = thread::spawn(move || {
        println!("Waiting for messages... (Ctrl+C to exit)");
        for msg in rx.iter() {
            if let Some(msg) = msg {
                println!("Received message:");
                println!("  Topic: {}", msg.topic());
                println!("  Payload: {}", msg.payload_str());
                println!("  QoS: {}", msg.qos());
            } else {
                // Connection lost
                break;
            }
        }
    });

    // Wait for thread to complete (or Ctrl+C)
    handle.join().unwrap();

    // Disconnect
    println!("Disconnecting from broker...");
    client.disconnect(None).expect("Failed to disconnect");
    println!("Disconnected");
}
```

**Advanced Rust Implementation with Error Handling:**

```rust
use paho_mqtt as mqtt;
use std::time::Duration;
use serde::{Deserialize, Serialize};
use anyhow::{Result, Context};

#[derive(Serialize, Deserialize, Debug)]
struct TemperatureReading {
    temperature: f32,
    unit: String,
    timestamp: i64,
}

struct HiveMQClient {
    client: mqtt::Client,
}

impl HiveMQClient {
    fn new(broker: &str, client_id: &str) -> Result<Self> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();

        let client = mqtt::Client::new(create_opts)
            .context("Failed to create MQTT client")?;

        Ok(HiveMQClient { client })
    }

    fn connect(&mut self, username: &str, password: &str) -> Result<()> {
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(true)
            .user_name(username)
            .password(password)
            .automatic_reconnect(Duration::from_secs(1), Duration::from_secs(30))
            .finalize();

        self.client.connect(conn_opts)
            .context("Failed to connect to broker")?;
        
        println!("Connected to HiveMQ broker");
        Ok(())
    }

    fn subscribe(&self, topic: &str, qos: i32) -> Result<()> {
        self.client.subscribe(topic, qos)
            .context(format!("Failed to subscribe to {}", topic))?;
        println!("Subscribed to: {}", topic);
        Ok(())
    }

    fn publish_json<T: Serialize>(&self, topic: &str, data: &T, qos: i32) -> Result<()> {
        let payload = serde_json::to_string(data)
            .context("Failed to serialize data")?;

        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(qos)
            .finalize();

        self.client.publish(msg)
            .context("Failed to publish message")?;
        
        Ok(())
    }

    fn start_listening(&self) -> mqtt::Receiver<Option<mqtt::Message>> {
        self.client.start_consuming()
    }

    fn disconnect(&self) -> Result<()> {
        self.client.disconnect(None)
            .context("Failed to disconnect")?;
        Ok(())
    }
}

fn main() -> Result<()> {
    let mut client = HiveMQClient::new("tcp://localhost:1883", "RustHiveMQClient")?;
    
    client.connect("mqttuser", "mqttpass")?;
    client.subscribe("sensors/#", 1)?;

    // Publish temperature reading
    let reading = TemperatureReading {
        temperature: 22.5,
        unit: "C".to_string(),
        timestamp: chrono::Utc::now().timestamp(),
    };
    
    client.publish_json("sensors/temperature", &reading, 1)?;

    // Listen for messages
    let rx = client.start_listening();
    for msg in rx.iter() {
        if let Some(msg) = msg {
            println!("Received: {} on {}", msg.payload_str(), msg.topic());
            
            // Try to parse as TemperatureReading
            if let Ok(reading) = serde_json::from_str::<TemperatureReading>(msg.payload_str()) {
                println!("Parsed reading: {:?}", reading);
            }
        }
    }

    client.disconnect()?;
    Ok(())
}
```

## HiveMQ Extensions

HiveMQ's extension framework allows custom business logic. Example extension structure:

```java
// Extension main class (Java)
public class CustomAuthExtension implements ExtensionMain {
    
    @Override
    public void extensionStart(@NotNull ExtensionStartInput input, 
                               @NotNull ExtensionStartOutput output) {
        
        final Services services = input.getServerInformation();
        
        // Register custom authentication
        services.securityRegistry().setAuthenticatorProvider(
            new CustomAuthenticatorProvider()
        );
    }

    @Override
    public void extensionStop(@NotNull ExtensionStopInput input, 
                              @NotNull ExtensionStopOutput output) {
        // Cleanup
    }
}
```

## Summary

HiveMQ Configuration represents enterprise-grade MQTT broker deployment suitable for mission-critical IoT applications. Key characteristics include XML-based configuration for listeners (TCP, TLS, WebSocket), comprehensive security options, horizontal scalability through clustering, and an extensible architecture for custom business logic. The broker supports millions of concurrent connections with optimized message routing and persistence.

Client implementations in C/C++ and Rust demonstrate connecting to HiveMQ with authentication, subscribing to topics with wildcards, publishing JSON payloads, and handling callbacks for message arrival and connection events. Both languages support QoS levels, automatic reconnection, and TLS encryption for secure communications.

HiveMQ excels in production environments requiring high availability, compliance features, real-time monitoring, and integration with enterprise systems. Its extension framework enables custom authentication, authorization, data transformation, and integration with external services without modifying the broker core. This makes HiveMQ the preferred choice for large-scale IoT deployments in industries like automotive, manufacturing, energy, and telecommunications.