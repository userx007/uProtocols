# MQTT and the Mosquitto C Library for Embedded Systems

## Overview

MQTT (Message Queuing Telemetry Transport) is a lightweight, publish-subscribe network protocol designed for constrained devices and low-bandwidth, high-latency, or unreliable networks. It's particularly well-suited for IoT (Internet of Things) applications where devices need to communicate efficiently with minimal overhead.

The **Mosquitto C Library** (libmosquitto) is an open-source client library that implements MQTT protocol versions 3.1 and 3.1.1, as well as MQTT 5.0. It's developed by the Eclipse Foundation and provides a robust, efficient API for embedded systems and resource-constrained devices.

## Key Concepts

### MQTT Architecture
- **Broker**: Central server that receives all messages from publishers and routes them to subscribers
- **Publisher**: Client that sends messages to topics
- **Subscriber**: Client that receives messages from topics it's subscribed to
- **Topic**: Hierarchical string (e.g., `home/livingroom/temperature`) used for message routing
- **QoS (Quality of Service)**: Three levels (0, 1, 2) determining message delivery guarantees

### Why Mosquitto for Embedded Systems?
- Minimal memory footprint
- Low CPU overhead
- Written in C for maximum portability
- Supports async and sync operations
- Thread-safe with proper configuration
- Wide platform support (Linux, RTOS, bare-metal with adaptation)

## C/C++ Implementation with libmosquitto

### Basic Setup and Connection

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Callback when connection is established
void on_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        printf("Connected to broker successfully\n");
        // Subscribe to a topic after connecting
        mosquitto_subscribe(mosq, NULL, "sensors/temperature", 0);
    } else {
        fprintf(stderr, "Connection failed: %s\n", mosquitto_connack_string(result));
    }
}

// Callback when a message is received
void on_message(struct mosquitto *mosq, void *userdata, 
                const struct mosquitto_message *message) {
    if (message->payloadlen) {
        printf("Topic: %s\n", message->topic);
        printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    }
}

// Callback when subscription is confirmed
void on_subscribe(struct mosquitto *mosq, void *userdata, int mid, 
                  int qos_count, const int *granted_qos) {
    printf("Subscribed (mid: %d, QoS: %d)\n", mid, granted_qos[0]);
}

int main(int argc, char *argv[]) {
    struct mosquitto *mosq = NULL;
    int rc;
    
    // Initialize the Mosquitto library
    mosquitto_lib_init();
    
    // Create a new client instance
    // Parameters: id (NULL for random), clean_session, userdata
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "Error: Out of memory.\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_subscribe_callback_set(mosq, on_subscribe);
    
    // Connect to broker
    // Parameters: host, port, keepalive
    rc = mosquitto_connect(mosq, "localhost", 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        return 1;
    }
    
    // Start the network loop (blocking)
    mosquitto_loop_forever(mosq, -1, 1);
    
    // Cleanup
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### Publishing Messages

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void on_publish(struct mosquitto *mosq, void *userdata, int mid) {
    printf("Message published (mid: %d)\n", mid);
}

int main() {
    struct mosquitto *mosq;
    int rc;
    
    mosquitto_lib_init();
    mosq = mosquitto_new("publisher_client", true, NULL);
    
    if (!mosq) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    mosquitto_publish_callback_set(mosq, on_publish);
    
    rc = mosquitto_connect(mosq, "localhost", 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Connection failed\n");
        mosquitto_destroy(mosq);
        return 1;
    }
    
    // Start non-blocking loop
    mosquitto_loop_start(mosq);
    
    // Publish messages
    for (int i = 0; i < 10; i++) {
        char payload[50];
        snprintf(payload, sizeof(payload), "Temperature: %d.%d°C", 20 + i, i * 10);
        
        // Parameters: topic, payload length, payload, QoS, retain flag
        rc = mosquitto_publish(mosq, NULL, "sensors/temperature", 
                              strlen(payload), payload, 0, false);
        
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Publish failed: %s\n", mosquitto_strerror(rc));
        }
        
        sleep(1);
    }
    
    mosquitto_loop_stop(mosq, false);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### Advanced: TLS/SSL and Authentication

```c
#include <mosquitto.h>
#include <stdio.h>

int main() {
    struct mosquitto *mosq;
    int rc;
    
    mosquitto_lib_init();
    mosq = mosquitto_new("secure_client", true, NULL);
    
    // Set username and password
    mosquitto_username_pw_set(mosq, "myuser", "mypassword");
    
    // Configure TLS/SSL
    // Parameters: CA cert file, cert path, client cert, client key, pw callback
    rc = mosquitto_tls_set(mosq, 
                          "/path/to/ca.crt",    // CA certificate
                          NULL,                  // Certificate path
                          "/path/to/client.crt", // Client certificate
                          "/path/to/client.key", // Client key
                          NULL);                 // Password callback
    
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "TLS setup failed: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        return 1;
    }
    
    // Optionally set TLS options
    mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
    
    // Connect to secure broker
    rc = mosquitto_connect(mosq, "broker.example.com", 8883, 60);
    
    if (rc == MOSQ_ERR_SUCCESS) {
        printf("Connected securely\n");
        mosquitto_loop_forever(mosq, -1, 1);
    }
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

## Rust Implementation

Rust has several MQTT client libraries. The most popular is **rumqttc**, which provides both sync and async interfaces.

### Basic Publisher-Subscriber in Rust

```rust
// Cargo.toml dependencies:
// rumqttc = "0.24"
// tokio = { version = "1", features = ["full"] }

use rumqttc::{Client, MqttOptions, QoS};
use std::time::Duration;
use std::thread;

fn main() {
    // Create MQTT client options
    let mut mqttoptions = MqttOptions::new("rust_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Create client and connection
    let (client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn thread to handle publishing
    let publish_client = client.clone();
    thread::spawn(move || {
        for i in 0..10 {
            let payload = format!("Temperature: {}.{}°C", 20 + i, i * 10);
            publish_client
                .publish("sensors/temperature", QoS::AtLeastOnce, false, payload)
                .unwrap();
            println!("Published message {}", i);
            thread::sleep(Duration::from_secs(1));
        }
    });
    
    // Subscribe to topic
    client.subscribe("sensors/temperature", QoS::AtMostOnce).unwrap();
    
    // Handle incoming messages
    for (i, notification) in connection.iter().enumerate() {
        match notification {
            Ok(event) => {
                println!("Event: {:?}", event);
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
        
        if i > 15 {
            break;
        }
    }
}
```

### Async Rust Implementation

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::{task, time};
use std::time::Duration;

#[tokio::main]
async fn main() {
    let mut mqttoptions = MqttOptions::new("async_rust_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn task to handle publishing
    task::spawn(async move {
        for i in 0..10 {
            let topic = "sensors/temperature";
            let payload = format!("Temperature: {}.{}°C", 20 + i, i * 10);
            
            client
                .publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())
                .await
                .unwrap();
            
            println!("Published: {}", payload);
            time::sleep(Duration::from_secs(1)).await;
        }
    });
    
    // Subscribe and handle events
    let client_handle = eventloop.client();
    client_handle.subscribe("sensors/#", QoS::AtMostOnce).await.unwrap();
    
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(p))) => {
                let payload = String::from_utf8_lossy(&p.payload);
                println!("Received: {} on topic: {}", payload, p.topic);
            }
            Ok(Event::Incoming(i)) => {
                println!("Incoming: {:?}", i);
            }
            Ok(Event::Outgoing(o)) => {
                println!("Outgoing: {:?}", o);
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }
}
```

### Rust with TLS/SSL

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, TlsConfiguration, Transport};
use std::time::Duration;
use tokio::fs;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("secure_rust_client", "broker.example.com", 8883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Load certificates
    let ca = fs::read("ca.crt").await?;
    let client_cert = fs::read("client.crt").await?;
    let client_key = fs::read("client.key").await?;
    
    // Configure TLS
    let tls_config = TlsConfiguration::Simple {
        ca,
        alpn: None,
        client_auth: Some((client_cert, client_key)),
    };
    
    mqttoptions.set_transport(Transport::Tls(tls_config));
    
    // Set credentials
    mqttoptions.set_credentials("username", "password");
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    client.subscribe("test/topic", QoS::AtMostOnce).await?;
    
    loop {
        match eventloop.poll().await {
            Ok(event) => println!("Event: {:?}", event),
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

### Embedded Rust Example (no_std compatible approach)

```rust
// For true embedded systems, you'd use libraries like:
// mqtt-sn for sensor networks or minimal MQTT implementations

// Example using rust-mqtt for embedded (conceptual)
use embedded_hal::blocking::delay::DelayMs;

pub struct EmbeddedMqttClient<T> {
    transport: T,
    buffer: [u8; 256],
}

impl<T> EmbeddedMqttClient<T> {
    pub fn new(transport: T) -> Self {
        Self {
            transport,
            buffer: [0u8; 256],
        }
    }
    
    pub fn connect(&mut self, client_id: &str) -> Result<(), Error> {
        // Build CONNECT packet manually
        let mut packet = [0u8; 64];
        packet[0] = 0x10; // CONNECT packet type
        // ... build rest of packet
        // self.transport.write(&packet)?;
        Ok(())
    }
    
    pub fn publish(&mut self, topic: &str, payload: &[u8], qos: u8) -> Result<(), Error> {
        // Build PUBLISH packet
        let mut packet = [0u8; 256];
        packet[0] = 0x30 | (qos << 1); // PUBLISH packet
        // ... encode topic and payload
        // self.transport.write(&packet)?;
        Ok(())
    }
}

#[derive(Debug)]
pub enum Error {
    Transport,
    Protocol,
}
```

## Compilation and Linking

### C/C++ with libmosquitto

```bash
# Install libmosquitto
# On Ubuntu/Debian:
sudo apt-get install libmosquitto-dev

# Compile
gcc -o mqtt_client mqtt_client.c -lmosquitto

# For C++
g++ -o mqtt_client mqtt_client.cpp -lmosquitto

# Cross-compile for ARM embedded Linux
arm-linux-gnueabihf-gcc -o mqtt_client mqtt_client.c -lmosquitto
```

### Rust

```bash
# Add to Cargo.toml
# [dependencies]
# rumqttc = "0.24"

# Build
cargo build --release

# For embedded targets
cargo build --target thumbv7em-none-eabihf --release
```

## Summary

The Mosquitto C Library provides a robust, efficient solution for implementing MQTT communication in embedded systems. Its lightweight design, minimal dependencies, and C-based API make it ideal for resource-constrained devices. Key advantages include fine-grained control over memory usage, synchronous and asynchronous operation modes, comprehensive callback system for event handling, and built-in support for TLS/SSL and authentication.

For C/C++ developers, libmosquitto offers direct control and minimal overhead, making it perfect for real-time embedded applications. The Rust ecosystem provides higher-level abstractions through libraries like rumqttc, offering memory safety and modern async programming patterns while maintaining good performance.

Both approaches excel in IoT scenarios where devices need reliable, low-bandwidth communication with brokers, whether for sensor data collection, device control, or distributed system coordination. The choice between C and Rust often depends on project requirements, with C offering maximum portability and Rust providing enhanced safety and productivity.