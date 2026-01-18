# Paho MQTT C Client: Comprehensive Guide

## Overview

The Eclipse Paho MQTT C Client is a fully-featured MQTT client library written in ANSI C. It provides both synchronous and asynchronous APIs for implementing MQTT communication in embedded systems, IoT devices, and applications requiring lightweight messaging. The library supports MQTT versions 3.1, 3.1.1, and 5.0, making it one of the most versatile and widely-used MQTT client implementations.

## Key Concepts

### MQTT Protocol Basics
MQTT (Message Queuing Telemetry Transport) is a lightweight publish-subscribe messaging protocol designed for constrained devices and low-bandwidth networks. Key concepts include:

- **Broker**: Central server that routes messages between clients
- **Topics**: Hierarchical message routing paths (e.g., `home/sensors/temperature`)
- **QoS Levels**: Quality of Service guarantees (0, 1, or 2)
- **Retained Messages**: Last message on a topic stored by broker
- **Last Will and Testament (LWT)**: Message sent when client disconnects unexpectedly

### Paho C Client Architecture

The library provides two main APIs:

1. **Synchronous API** (`MQTTClient.h`): Blocking calls, simpler to use
2. **Asynchronous API** (`MQTTAsync.h`): Non-blocking, callback-based, better for complex applications

## C/C++ Implementation

### Installation

```bash
# Ubuntu/Debian
sudo apt-get install libpaho-mqtt-dev

# From source
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
cmake -Bbuild -H. -DPAHO_WITH_SSL=ON
cmake --build build
sudo cmake --build build --target install
```

### Synchronous Publisher Example (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "PahoSyncPublisher"
#define TOPIC       "sensor/temperature"
#define QOS         1
#define TIMEOUT     10000L

int main(int argc, char* argv[])
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create client
    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to create client, return code %d\n", rc);
        return EXIT_FAILURE;
    }

    // Set connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        MQTTClient_destroy(&client);
        return EXIT_FAILURE;
    }

    // Publish message
    char payload[50];
    snprintf(payload, sizeof(payload), "Temperature: %.2f°C", 23.5);
    
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    if ((rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token)) 
        != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to publish message, return code %d\n", rc);
    }
    else
    {
        printf("Waiting for publication of %s\n", payload);
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("Message delivered, token: %d\n", token);
    }

    // Disconnect and cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

### Asynchronous Subscriber Example (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "PahoAsyncSubscriber"
#define TOPIC       "sensor/#"  // Wildcard subscription
#define QOS         1

volatile int finished = 0;

// Message arrival callback
int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message)
{
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("QoS: %d\n", message->qos);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    
    return 1;
}

// Connection lost callback
void connectionLost(void *context, char *cause)
{
    printf("\nConnection lost: %s\n", cause);
    finished = 1;
}

int main(int argc, char* argv[])
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to create client, return code %d\n", rc);
        return EXIT_FAILURE;
    }

    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connectionLost, messageArrived, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        MQTTClient_destroy(&client);
        return EXIT_FAILURE;
    }

    printf("Subscribing to topic %s with QoS %d\n", TOPIC, QOS);
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to subscribe, return code %d\n", rc);
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
        return EXIT_FAILURE;
    }

    printf("Waiting for messages (Ctrl+C to exit)...\n");
    while (!finished)
    {
        #if defined(_WIN32)
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

### C++ Wrapper Example

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include "MQTTClient.h"

class MQTTClientWrapper {
private:
    MQTTClient client;
    std::string address;
    std::string clientId;

    static int messageArrivedStatic(void *context, char *topicName, 
                                   int topicLen, MQTTClient_message *message)
    {
        auto* wrapper = static_cast<MQTTClientWrapper*>(context);
        return wrapper->messageArrived(topicName, message);
    }

    int messageArrived(char *topicName, MQTTClient_message *message)
    {
        std::string topic(topicName);
        std::string payload((char*)message->payload, message->payloadlen);
        
        std::cout << "Topic: " << topic << " | Payload: " << payload << std::endl;
        
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }

public:
    MQTTClientWrapper(const std::string& addr, const std::string& id)
        : address(addr), clientId(id)
    {
        int rc = MQTTClient_create(&client, address.c_str(), clientId.c_str(),
                                   MQTTCLIENT_PERSISTENCE_NONE, nullptr);
        if (rc != MQTTCLIENT_SUCCESS) {
            throw std::runtime_error("Failed to create MQTT client");
        }
    }

    ~MQTTClientWrapper()
    {
        disconnect();
        MQTTClient_destroy(&client);
    }

    bool connect()
    {
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;

        MQTTClient_setCallbacks(client, this, nullptr, messageArrivedStatic, nullptr);

        int rc = MQTTClient_connect(client, &conn_opts);
        return rc == MQTTCLIENT_SUCCESS;
    }

    bool publish(const std::string& topic, const std::string& payload, int qos = 1)
    {
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;

        pubmsg.payload = (void*)payload.c_str();
        pubmsg.payloadlen = payload.length();
        pubmsg.qos = qos;
        pubmsg.retained = 0;

        int rc = MQTTClient_publishMessage(client, topic.c_str(), &pubmsg, &token);
        if (rc == MQTTCLIENT_SUCCESS) {
            MQTTClient_waitForCompletion(client, token, 10000L);
            return true;
        }
        return false;
    }

    bool subscribe(const std::string& topic, int qos = 1)
    {
        int rc = MQTTClient_subscribe(client, topic.c_str(), qos);
        return rc == MQTTCLIENT_SUCCESS;
    }

    void disconnect()
    {
        MQTTClient_disconnect(client, 10000);
    }
};

int main()
{
    try {
        MQTTClientWrapper mqtt("tcp://broker.hivemq.com:1883", "CppWrapper");
        
        if (mqtt.connect()) {
            std::cout << "Connected successfully!" << std::endl;
            
            mqtt.subscribe("test/cpp/#");
            mqtt.publish("test/cpp/hello", "Hello from C++!");
            
            // Keep alive for some time
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation

While Paho has an official Rust wrapper (`paho-mqtt`), here's how to use it:

### Add Dependency

```toml
[dependencies]
paho-mqtt = "0.12"
```

### Synchronous Publisher (Rust)

```rust
use paho_mqtt as mqtt;
use std::time::Duration;

fn main() {
    // Create client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri("tcp://broker.hivemq.com:1883")
        .client_id("RustSyncPublisher")
        .finalize();

    let client = mqtt::Client::new(create_opts)
        .expect("Failed to create client");

    // Connection options
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .finalize();

    // Connect to broker
    match client.connect(conn_opts) {
        Ok(_) => println!("Connected to broker"),
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            return;
        }
    }

    // Create and publish message
    let msg = mqtt::MessageBuilder::new()
        .topic("sensor/temperature")
        .payload("Temperature: 23.5°C")
        .qos(1)
        .finalize();

    match client.publish(msg) {
        Ok(_) => println!("Message published successfully"),
        Err(e) => eprintln!("Publish failed: {}", e),
    }

    // Disconnect
    client.disconnect(None).expect("Disconnect failed");
}
```

### Asynchronous Subscriber (Rust)

```rust
use paho_mqtt as mqtt;
use std::time::Duration;
use std::process;

fn main() {
    let host = "tcp://broker.hivemq.com:1883";
    let client_id = "RustAsyncSubscriber";

    // Create async client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri(host)
        .client_id(client_id)
        .finalize();

    let mut client = mqtt::AsyncClient::new(create_opts)
        .expect("Failed to create client");

    // Set up callbacks
    client.set_message_callback(|_client, msg| {
        if let Some(msg) = msg {
            println!("Topic: {}", msg.topic());
            println!("Payload: {}", msg.payload_str());
            println!("QoS: {:?}", msg.qos());
        }
    });

    client.set_connection_lost_callback(|_client| {
        println!("Connection lost!");
    });

    // Connection options
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .finalize();

    // Connect
    match client.connect(conn_opts).wait() {
        Ok(_) => println!("Connected to broker"),
        Err(e) => {
            eprintln!("Connection failed: {}", e);
            process::exit(1);
        }
    }

    // Subscribe to topics
    let subscriptions = &["sensor/#"];
    let qos = &[1];

    match client.subscribe_many(subscriptions, qos).wait() {
        Ok(_) => println!("Subscribed to topics"),
        Err(e) => {
            eprintln!("Subscribe failed: {}", e);
            process::exit(1);
        }
    }

    println!("Waiting for messages (Ctrl+C to exit)...");

    // Keep the program running
    loop {
        std::thread::sleep(Duration::from_secs(1));
    }
}
```

### Advanced Rust Example with Error Handling

```rust
use paho_mqtt as mqtt;
use std::time::Duration;

struct MqttHandler {
    client: mqtt::AsyncClient,
}

impl MqttHandler {
    fn new(broker: &str, client_id: &str) -> Result<Self, mqtt::Error> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker)
            .client_id(client_id)
            .finalize();

        let mut client = mqtt::AsyncClient::new(create_opts)?;

        // Setup callbacks
        client.set_connection_lost_callback(|cli| {
            println!("Connection lost. Attempting reconnect...");
        });

        client.set_connected_callback(|_cli| {
            println!("Successfully connected!");
        });

        Ok(MqttHandler { client })
    }

    async fn connect(&self) -> Result<(), mqtt::Error> {
        let lwt = mqtt::MessageBuilder::new()
            .topic("status/disconnect")
            .payload("Client disconnected unexpectedly")
            .qos(1)
            .retained(true)
            .finalize();

        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(true)
            .will_message(lwt)
            .automatic_reconnect(Duration::from_secs(1), Duration::from_secs(30))
            .finalize();

        self.client.connect(conn_opts).await?;
        Ok(())
    }

    async fn publish(&self, topic: &str, payload: &str, qos: i32) 
        -> Result<(), mqtt::Error> 
    {
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(qos)
            .finalize();

        self.client.publish(msg).await?;
        Ok(())
    }

    async fn subscribe(&self, topics: &[&str], qos: &[i32]) 
        -> Result<(), mqtt::Error> 
    {
        self.client.subscribe_many(topics, qos).await?;
        Ok(())
    }

    fn start_consuming(&self) -> mqtt::Receiver<Option<mqtt::Message>> {
        self.client.start_consuming()
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let handler = MqttHandler::new(
        "tcp://broker.hivemq.com:1883",
        "RustAdvanced"
    )?;

    handler.connect().await?;
    handler.subscribe(&["sensor/#", "control/#"], &[1, 1]).await?;

    let rx = handler.start_consuming();

    // Message loop
    for msg in rx.iter() {
        if let Some(msg) = msg {
            println!("Received: {} - {}", msg.topic(), msg.payload_str());
            
            // Respond to control messages
            if msg.topic().starts_with("control/") {
                handler.publish("status/ack", "Command received", 1).await?;
            }
        }
    }

    Ok(())
}
```

## Summary

The Eclipse Paho MQTT C Client library provides robust, production-ready MQTT communication capabilities for C/C++ applications and embedded systems. It offers both synchronous and asynchronous APIs, supporting all MQTT protocol versions with features like automatic reconnection, SSL/TLS encryption, and flexible QoS levels.

**Key advantages:**
- Lightweight footprint suitable for embedded devices
- Battle-tested in production IoT deployments
- Active community and commercial support
- Cross-platform compatibility (Linux, Windows, macOS, RTOS)
- Comprehensive documentation and examples

**Common use cases:**
- IoT sensor networks and device communication
- Industrial automation and SCADA systems
- Real-time monitoring and telemetry
- Home automation systems
- Mobile and edge computing applications

The Rust wrapper maintains the reliability of the C library while providing memory safety and modern async capabilities, making it ideal for safe, concurrent IoT applications. Whether using C/C++ for embedded constraints or Rust for system-level safety, Paho MQTT provides a solid foundation for publish-subscribe messaging architectures.