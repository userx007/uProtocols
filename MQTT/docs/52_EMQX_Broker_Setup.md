# EMQX Broker Setup: Comprehensive Guide

## Overview

EMQX is a highly scalable, enterprise-grade MQTT broker designed for IoT and real-time messaging applications. It supports millions of concurrent connections, distributed clustering, and provides advanced features like rule engines, data persistence, and multiple protocol support beyond MQTT (including MQTT-SN, CoAP, LwM2M, and WebSocket).

## Core Concepts

**EMQX Architecture:**
- **Distributed clustering** for horizontal scalability
- **High availability** through automatic failover
- **Rule engine** for real-time data processing
- **Authentication & authorization** with multiple backends
- **Message persistence** and bridging capabilities
- **Hot configuration** updates without restart

## Installation and Basic Setup

### Docker Deployment (Simplest)

```bash
# Pull EMQX image
docker pull emqx/emqx:latest

# Run single node
docker run -d --name emqx \
  -p 1883:1883 \
  -p 8083:8083 \
  -p 8084:8084 \
  -p 8883:8883 \
  -p 18083:18083 \
  emqx/emqx:latest

# Access dashboard at http://localhost:18083
# Default credentials: admin / public
```

### Configuration File (emqx.conf)

```conf
# Node configuration
node.name = emqx@127.0.0.1
node.cookie = emqxsecretcookie

# Listeners
listeners.tcp.default {
  bind = "0.0.0.0:1883"
  max_connections = 1024000
}

listeners.ssl.default {
  bind = "0.0.0.0:8883"
  max_connections = 512000
  ssl_options {
    cacertfile = "/path/to/cacert.pem"
    certfile = "/path/to/cert.pem"
    keyfile = "/path/to/key.pem"
  }
}

# WebSocket
listeners.ws.default {
  bind = "0.0.0.0:8083"
  max_connections = 102400
}

# Zones for QoS and rate limiting
zone.external {
  max_qos_allowed = 2
  max_packet_size = 1MB
  max_clientid_len = 65535
}
```

## C/C++ Client Implementation with EMQX

### Using Paho MQTT C Library

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "emqx_c_client"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Message arrived callback
int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived\n");
    printf("Topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Connection lost callback
void connectionLost(void *context, char *cause) {
    printf("\nConnection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connectionLost, 
                           messageArrived, NULL);

    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "emqx_user";
    conn_opts.password = "emqx_password";
    
    // Connect to EMQX broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to EMQX broker\n");

    // Subscribe to topic
    printf("Subscribing to topic %s for QoS %d\n", TOPIC, QOS);
    MQTTClient_subscribe(client, TOPIC, QOS);

    // Publish message
    char payload[256];
    snprintf(payload, sizeof(payload), 
             "{\"device\":\"sensor_01\",\"temperature\":25.5,\"timestamp\":%ld}", 
             time(NULL));
    
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    printf("Waiting for publication\n");
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message with delivery token %d delivered\n", token);

    // Keep running to receive messages
    printf("Press Q<Enter> to quit\n");
    int ch;
    do {
        ch = getchar();
    } while (ch != 'Q' && ch != 'q');

    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

### C++ Client with Paho MQTT C++

```cpp
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID("emqx_cpp_client");
const std::string TOPIC("sensors/temperature");
const int QOS = 1;

class Callback : public virtual mqtt::callback {
private:
    mqtt::async_client& client_;
    
public:
    Callback(mqtt::async_client& client) : client_(client) {}
    
    void connection_lost(const std::string& cause) override {
        std::cout << "\nConnection lost: " << cause << std::endl;
    }
    
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "Topic: " << msg->get_topic() << std::endl;
        std::cout << "Payload: " << msg->to_string() << std::endl;
    }
    
    void delivery_complete(mqtt::delivery_token_ptr token) override {
        std::cout << "Delivery complete for token: " 
                  << (token ? token->get_message_id() : -1) << std::endl;
    }
};

class EMQXClient {
private:
    mqtt::async_client client_;
    Callback callback_;
    mqtt::connect_options connOpts_;
    
public:
    EMQXClient(const std::string& serverURI, const std::string& clientId)
        : client_(serverURI, clientId), callback_(client_) {
        
        client_.set_callback(callback_);
        
        // Configure connection options
        connOpts_.set_keep_alive_interval(20);
        connOpts_.set_clean_session(true);
        connOpts_.set_user_name("emqx_user");
        connOpts_.set_password("emqx_password");
        
        // Configure automatic reconnection
        connOpts_.set_automatic_reconnect(true);
    }
    
    void connect() {
        try {
            std::cout << "Connecting to EMQX broker..." << std::endl;
            mqtt::token_ptr conntok = client_.connect(connOpts_);
            conntok->wait();
            std::cout << "Connected successfully" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
            throw;
        }
    }
    
    void subscribe(const std::string& topic, int qos) {
        try {
            client_.subscribe(topic, qos)->wait();
            std::cout << "Subscribed to: " << topic << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error subscribing: " << exc.what() << std::endl;
        }
    }
    
    void publish(const std::string& topic, const std::string& payload, int qos) {
        try {
            auto msg = mqtt::make_message(topic, payload);
            msg->set_qos(qos);
            client_.publish(msg)->wait();
            std::cout << "Message published" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error publishing: " << exc.what() << std::endl;
        }
    }
    
    void disconnect() {
        try {
            client_.disconnect()->wait();
            std::cout << "Disconnected" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error disconnecting: " << exc.what() << std::endl;
        }
    }
};

int main() {
    EMQXClient client(SERVER_ADDRESS, CLIENT_ID);
    
    try {
        client.connect();
        client.subscribe(TOPIC, QOS);
        
        // Publish temperature readings
        for (int i = 0; i < 10; ++i) {
            std::string payload = "{\"device\":\"sensor_01\",\"temperature\":" 
                                + std::to_string(20.0 + i * 0.5) + "}";
            client.publish(TOPIC, payload, QOS);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
        client.disconnect();
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Client Implementation

### Using rumqttc Library

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use tokio::time;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
struct SensorData {
    device: String,
    temperature: f32,
    humidity: f32,
    timestamp: i64,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options for EMQX
    let mut mqttoptions = MqttOptions::new("emqx_rust_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_credentials("emqx_user", "emqx_password");
    mqttoptions.set_clean_session(true);
    
    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn task to handle incoming messages
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    println!("Received message on topic: {}", p.topic);
                    let payload = String::from_utf8_lossy(&p.payload);
                    println!("Payload: {}", payload);
                    
                    // Parse JSON payload
                    if let Ok(data) = serde_json::from_str::<SensorData>(&payload) {
                        println!("Parsed data: {:?}", data);
                    }
                }
                Ok(Event::Incoming(i)) => {
                    println!("Incoming event: {:?}", i);
                }
                Ok(Event::Outgoing(o)) => {
                    println!("Outgoing event: {:?}", o);
                }
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });
    
    // Subscribe to topic
    client.subscribe("sensors/+/temperature", QoS::AtLeastOnce).await?;
    println!("Subscribed to sensors/+/temperature");
    
    // Publish messages periodically
    let mut interval = time::interval(Duration::from_secs(5));
    for i in 0..20 {
        interval.tick().await;
        
        let sensor_data = SensorData {
            device: format!("sensor_{:02}", i % 3),
            temperature: 20.0 + (i as f32 * 0.5),
            humidity: 45.0 + (i as f32 * 0.3),
            timestamp: chrono::Utc::now().timestamp(),
        };
        
        let payload = serde_json::to_string(&sensor_data)?;
        let topic = format!("sensors/{}/temperature", sensor_data.device);
        
        client.publish(
            topic,
            QoS::AtLeastOnce,
            false,
            payload.as_bytes()
        ).await?;
        
        println!("Published: {}", payload);
    }
    
    // Keep running to receive messages
    time::sleep(Duration::from_secs(10)).await;
    
    Ok(())
}
```

### Advanced Rust Client with TLS and Reconnection

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, TlsConfiguration, Transport};
use std::time::Duration;
use tokio::time;

struct EMQXClient {
    client: AsyncClient,
}

impl EMQXClient {
    fn new(
        client_id: &str,
        host: &str,
        port: u16,
        username: &str,
        password: &str,
        use_tls: bool,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqttoptions = MqttOptions::new(client_id, host, port);
        
        // Connection settings
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        mqttoptions.set_credentials(username, password);
        mqttoptions.set_clean_session(true);
        mqttoptions.set_max_packet_size(1024 * 1024, 1024 * 1024); // 1MB
        
        // Configure TLS if needed
        if use_tls {
            let ca = std::fs::read("certs/ca.pem")?;
            let client_cert = std::fs::read("certs/client-cert.pem")?;
            let client_key = std::fs::read("certs/client-key.pem")?;
            
            let transport = Transport::Tls(TlsConfiguration::Simple {
                ca,
                alpn: None,
                client_auth: Some((client_cert, client_key)),
            });
            
            mqttoptions.set_transport(transport);
        }
        
        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 100);
        
        // Handle events in background
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(notification) => {
                        println!("Event: {:?}", notification);
                    }
                    Err(e) => {
                        eprintln!("Error polling: {:?}", e);
                        time::sleep(Duration::from_secs(1)).await;
                    }
                }
            }
        });
        
        Ok(Self { client })
    }
    
    async fn publish_with_retry(
        &self,
        topic: &str,
        payload: &[u8],
        qos: QoS,
        retain: bool,
        max_retries: u32,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut retries = 0;
        
        loop {
            match self.client.publish(topic, qos, retain, payload).await {
                Ok(_) => {
                    println!("Published successfully to {}", topic);
                    return Ok(());
                }
                Err(e) => {
                    retries += 1;
                    if retries >= max_retries {
                        return Err(format!("Failed after {} retries: {}", max_retries, e).into());
                    }
                    eprintln!("Publish failed, retry {}/{}: {}", retries, max_retries, e);
                    time::sleep(Duration::from_millis(500 * retries as u64)).await;
                }
            }
        }
    }
    
    async fn subscribe_multiple(&self, topics: Vec<(&str, QoS)>) -> Result<(), Box<dyn std::error::Error>> {
        for (topic, qos) in topics {
            self.client.subscribe(topic, qos).await?;
            println!("Subscribed to {} with QoS {:?}", topic, qos);
        }
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = EMQXClient::new(
        "rust_emqx_advanced",
        "localhost",
        1883,
        "emqx_user",
        "emqx_password",
        false, // TLS disabled for this example
    )?;
    
    // Subscribe to multiple topics
    client.subscribe_multiple(vec![
        ("sensors/+/temperature", QoS::AtLeastOnce),
        ("sensors/+/humidity", QoS::ExactlyOnce),
        ("alerts/#", QoS::AtMostOnce),
    ]).await?;
    
    // Publish with retry mechanism
    let payload = b"{\"device\":\"sensor_01\",\"value\":25.5}";
    client.publish_with_retry(
        "sensors/sensor_01/temperature",
        payload,
        QoS::AtLeastOnce,
        false,
        3,
    ).await?;
    
    // Keep running
    time::sleep(Duration::from_secs(60)).await;
    
    Ok(())
}
```

## EMQX Clustering Setup

### Docker Compose Cluster Configuration

```yaml
version: '3.8'

services:
  emqx1:
    image: emqx/emqx:latest
    container_name: emqx1
    environment:
      - "EMQX_NAME=emqx"
      - "EMQX_HOST=node1.emqx.io"
      - "EMQX_CLUSTER__DISCOVERY_STRATEGY=static"
      - "EMQX_CLUSTER__STATIC__SEEDS=[emqx@node1.emqx.io,emqx@node2.emqx.io]"
    ports:
      - "1883:1883"
      - "18083:18083"
    networks:
      emqx_net:
        aliases:
          - node1.emqx.io

  emqx2:
    image: emqx/emqx:latest
    container_name: emqx2
    environment:
      - "EMQX_NAME=emqx"
      - "EMQX_HOST=node2.emqx.io"
      - "EMQX_CLUSTER__DISCOVERY_STRATEGY=static"
      - "EMQX_CLUSTER__STATIC__SEEDS=[emqx@node1.emqx.io,emqx@node2.emqx.io]"
    networks:
      emqx_net:
        aliases:
          - node2.emqx.io

networks:
  emqx_net:
    driver: bridge
```

## Summary

**EMQX** is an enterprise-grade MQTT broker that excels in IoT deployments requiring high scalability, reliability, and advanced features. It supports millions of concurrent connections through distributed clustering and provides comprehensive authentication, authorization, and data processing capabilities through its built-in rule engine.

**Key advantages** include horizontal scalability through clustering, hot configuration updates, multiple protocol support, extensive monitoring and management dashboards, and enterprise features like data persistence and bridging to other systems.

**Implementation across languages** is straightforward: C/C++ developers can use the Paho MQTT libraries for efficient, low-level control; Rust developers benefit from modern async/await patterns with rumqttc for safe, concurrent programming; all languages support standard MQTT features including QoS levels, retained messages, last will and testament, and TLS encryption.

**Production deployments** typically involve clustered configurations with load balancers, authentication backends like PostgreSQL or Redis, TLS encryption for secure communications, and integration with monitoring systems for operational visibility.