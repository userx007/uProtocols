# Mosquitto Broker Configuration: A Comprehensive Guide

## Overview

Mosquitto is a lightweight, open-source MQTT broker that implements the MQTT protocol versions 3.1, 3.1.1, and 5.0. Proper configuration is essential for security, performance, and reliability in production environments. This guide covers advanced configuration options, listener setup, authentication, and optimization techniques.

## Core Configuration Concepts

### Configuration File Structure

The Mosquitto broker reads its configuration from `mosquitto.conf`, typically located in `/etc/mosquitto/` on Linux systems. The configuration file uses a simple key-value format with support for includes and conditional settings.

### Basic Configuration Parameters

**Persistence**: Controls whether the broker stores messages and subscriptions to disk
- `persistence true|false` - Enable/disable message persistence
- `persistence_location /var/lib/mosquitto/` - Where to store persistent data
- `autosave_interval <seconds>` - How often to save in-memory database to disk

**Logging**: Configure what and where the broker logs
- `log_dest file /var/log/mosquitto/mosquitto.log` - Log to file
- `log_dest stdout` - Log to console
- `log_type error|warning|notice|information|debug` - Log levels

**Security**: Authentication and authorization settings
- `allow_anonymous false` - Require authentication
- `password_file /etc/mosquitto/passwd` - Password file location
- `acl_file /etc/mosquitto/acl` - Access control list

## Listener Configuration

Listeners define how clients connect to the broker. You can configure multiple listeners on different ports with different security settings.

### Standard TCP Listener

```conf
# Default MQTT listener on port 1883
listener 1883
protocol mqtt

# Bind to specific interface (optional)
bind_address 0.0.0.0

# Maximum connections
max_connections 1000
```

### Secure WebSocket Listener

```conf
# WebSocket listener for browser clients
listener 9001
protocol websockets
socket_domain ipv4

# WebSocket-specific settings
http_dir /var/www/mqtt
```

### TLS/SSL Encrypted Listener

```conf
# Secure MQTT listener
listener 8883
protocol mqtt

# TLS certificate configuration
cafile /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/server.crt
keyfile /etc/mosquitto/certs/server.key

# Require client certificates
require_certificate true
use_identity_as_username true

# TLS version restrictions
tls_version tlsv1.2
```

## C/C++ Code Examples

### Basic MQTT Client with Mosquitto Library

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BROKER_ADDRESS "localhost"
#define BROKER_PORT 1883
#define CLIENT_ID "c_mqtt_client"
#define TOPIC "sensor/temperature"

// Callback when connection is established
void on_connect(struct mosquitto *mosq, void *obj, int result) {
    if(result == 0) {
        printf("Connected to broker successfully\n");
        // Subscribe to topic after connecting
        mosquitto_subscribe(mosq, NULL, TOPIC, 0);
    } else {
        fprintf(stderr, "Connection failed with code %d\n", result);
    }
}

// Callback when message arrives
void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    printf("Received message on topic '%s': %s\n", 
           msg->topic, (char *)msg->payload);
}

// Callback when subscription is confirmed
void on_subscribe(struct mosquitto *mosq, void *obj, 
                  int mid, int qos_count, const int *granted_qos) {
    printf("Subscribed to topic with QoS %d\n", granted_qos[0]);
}

int main(int argc, char *argv[]) {
    struct mosquitto *mosq;
    int rc;
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create mosquitto client instance
    mosq = mosquitto_new(CLIENT_ID, true, NULL);
    if(!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_subscribe_callback_set(mosq, on_subscribe);
    
    // Connect to broker
    rc = mosquitto_connect(mosq, BROKER_ADDRESS, BROKER_PORT, 60);
    if(rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        return 1;
    }
    
    // Start network loop
    mosquitto_loop_start(mosq);
    
    // Publish some messages
    for(int i = 0; i < 5; i++) {
        char payload[50];
        snprintf(payload, sizeof(payload), "Temperature: %.1f C", 20.0 + i);
        
        rc = mosquitto_publish(mosq, NULL, TOPIC, strlen(payload), 
                              payload, 1, false);
        if(rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Publish failed: %s\n", mosquitto_strerror(rc));
        }
        sleep(2);
    }
    
    // Keep running for a while to receive messages
    sleep(10);
    
    // Cleanup
    mosquitto_loop_stop(mosq, false);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### Secure MQTT Client with TLS

```cpp
#include <mosquitto.h>
#include <iostream>
#include <string>
#include <cstring>

class SecureMQTTClient {
private:
    struct mosquitto *mosq;
    std::string client_id;
    
public:
    SecureMQTTClient(const std::string &id) : client_id(id) {
        mosquitto_lib_init();
        mosq = mosquitto_new(client_id.c_str(), true, this);
        
        if(!mosq) {
            throw std::runtime_error("Failed to create mosquitto instance");
        }
        
        // Set callbacks
        mosquitto_connect_callback_set(mosq, on_connect_wrapper);
        mosquitto_message_callback_set(mosq, on_message_wrapper);
    }
    
    ~SecureMQTTClient() {
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }
    
    bool connect_secure(const std::string &host, int port,
                       const std::string &cafile,
                       const std::string &certfile,
                       const std::string &keyfile) {
        // Configure TLS
        int rc = mosquitto_tls_set(mosq, 
                                   cafile.c_str(),
                                   NULL,  // capath
                                   certfile.c_str(),
                                   keyfile.c_str(),
                                   NULL); // pw_callback
        
        if(rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "TLS setup failed: " 
                     << mosquitto_strerror(rc) << std::endl;
            return false;
        }
        
        // Set TLS options
        mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
        
        // Connect to broker
        rc = mosquitto_connect(mosq, host.c_str(), port, 60);
        if(rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "Connection failed: " 
                     << mosquitto_strerror(rc) << std::endl;
            return false;
        }
        
        return true;
    }
    
    void start_loop() {
        mosquitto_loop_start(mosq);
    }
    
    bool publish(const std::string &topic, const std::string &payload, 
                int qos = 0, bool retain = false) {
        int rc = mosquitto_publish(mosq, NULL, topic.c_str(),
                                  payload.length(), payload.c_str(),
                                  qos, retain);
        return (rc == MOSQ_ERR_SUCCESS);
    }
    
    bool subscribe(const std::string &topic, int qos = 0) {
        int rc = mosquitto_subscribe(mosq, NULL, topic.c_str(), qos);
        return (rc == MOSQ_ERR_SUCCESS);
    }
    
private:
    static void on_connect_wrapper(struct mosquitto *mosq, void *obj, int rc) {
        SecureMQTTClient *client = static_cast<SecureMQTTClient*>(obj);
        client->on_connect(rc);
    }
    
    static void on_message_wrapper(struct mosquitto *mosq, void *obj,
                                  const struct mosquitto_message *msg) {
        SecureMQTTClient *client = static_cast<SecureMQTTClient*>(obj);
        client->on_message(msg);
    }
    
    void on_connect(int result) {
        if(result == 0) {
            std::cout << "Connected securely to broker" << std::endl;
        } else {
            std::cerr << "Connection failed: " << result << std::endl;
        }
    }
    
    void on_message(const struct mosquitto_message *msg) {
        std::cout << "Topic: " << msg->topic 
                 << " | Payload: " 
                 << std::string((char*)msg->payload, msg->payloadlen)
                 << std::endl;
    }
};

int main() {
    try {
        SecureMQTTClient client("secure_cpp_client");
        
        if(client.connect_secure("mqtt.example.com", 8883,
                                 "/etc/mosquitto/certs/ca.crt",
                                 "/etc/mosquitto/certs/client.crt",
                                 "/etc/mosquitto/certs/client.key")) {
            client.start_loop();
            client.subscribe("sensors/#", 1);
            client.publish("status/client", "Connected", 1, true);
            
            // Keep running
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    } catch(const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Code Examples

### Basic MQTT Client with Rumqttc

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;

fn main() {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Create client and connection
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn a thread to handle incoming messages
    thread::spawn(move || {
        for (i, notification) in connection.iter().enumerate() {
            match notification {
                Ok(Event::Incoming(Packet::Publish(publish))) => {
                    let payload = String::from_utf8_lossy(&publish.payload);
                    println!("Received: Topic={}, Payload={}", 
                            publish.topic, payload);
                }
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("Connected to broker");
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Subscribe to topics
    client.subscribe("sensor/temperature", QoS::AtLeastOnce)
        .expect("Failed to subscribe");
    
    // Publish messages
    for i in 0..10 {
        let topic = "sensor/temperature";
        let payload = format!("{{\"temp\": {}, \"unit\": \"C\"}}", 20 + i);
        
        client.publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())
            .expect("Failed to publish");
        
        println!("Published: {}", payload);
        thread::sleep(Duration::from_secs(1));
    }
    
    // Keep running to receive messages
    thread::sleep(Duration::from_secs(5));
}
```

### Secure MQTT Client with TLS

```rust
use rumqttc::{Client, MqttOptions, QoS, TlsConfiguration, Transport};
use std::time::Duration;
use std::fs;
use rustls::{ClientConfig, RootCertStore};
use rustls_pemfile;
use std::io::BufReader;

fn load_tls_config(ca_file: &str, cert_file: &str, key_file: &str) 
    -> Result<ClientConfig, Box<dyn std::error::Error>> {
    
    // Load CA certificate
    let ca_cert = fs::read(ca_file)?;
    let mut ca_reader = BufReader::new(&ca_cert[..]);
    let ca_certs = rustls_pemfile::certs(&mut ca_reader)?;
    
    let mut root_store = RootCertStore::empty();
    for cert in ca_certs {
        root_store.add(&rustls::Certificate(cert))?;
    }
    
    // Load client certificate and key
    let cert_chain = fs::read(cert_file)?;
    let mut cert_reader = BufReader::new(&cert_chain[..]);
    let certs = rustls_pemfile::certs(&mut cert_reader)?
        .into_iter()
        .map(rustls::Certificate)
        .collect();
    
    let key_der = fs::read(key_file)?;
    let mut key_reader = BufReader::new(&key_der[..]);
    let key = rustls_pemfile::pkcs8_private_keys(&mut key_reader)?
        .into_iter()
        .next()
        .ok_or("No private key found")?;
    
    let config = ClientConfig::builder()
        .with_safe_defaults()
        .with_root_certificates(root_store)
        .with_client_auth_cert(certs, rustls::PrivateKey(key))?;
    
    Ok(config)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("secure_rust_client", 
                                          "mqtt.example.com", 8883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Load TLS configuration
    let tls_config = load_tls_config(
        "/etc/mosquitto/certs/ca.crt",
        "/etc/mosquitto/certs/client.crt",
        "/etc/mosquitto/certs/client.key"
    )?;
    
    // Set TLS transport
    mqttoptions.set_transport(Transport::Tls(TlsConfiguration::Rustls(
        std::sync::Arc::new(tls_config)
    )));
    
    // Create client
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Handle connection events
    std::thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(event) => println!("Event: {:?}", event),
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Subscribe and publish
    client.subscribe("secure/data", QoS::ExactlyOnce)?;
    
    for i in 0..5 {
        let payload = format!("Secure message {}", i);
        client.publish("secure/data", QoS::ExactlyOnce, false, 
                      payload.as_bytes())?;
        std::thread::sleep(Duration::from_secs(1));
    }
    
    std::thread::sleep(Duration::from_secs(10));
    Ok(())
}
```

### Advanced Rust Client with Connection Management

```rust
use rumqttc::{Client, Connection, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::sync::{Arc, Mutex};
use std::collections::HashMap;

struct MQTTManager {
    client: Client,
    subscriptions: Arc<Mutex<HashMap<String, QoS>>>,
}

impl MQTTManager {
    fn new(client_id: &str, broker: &str, port: u16) -> (Self, Connection) {
        let mut mqttoptions = MqttOptions::new(client_id, broker, port);
        mqttoptions.set_keep_alive(Duration::from_secs(60));
        mqttoptions.set_max_packet_size(1024 * 1024, 1024 * 1024); // 1MB
        
        let (client, connection) = Client::new(mqttoptions, 100);
        
        let manager = MQTTManager {
            client,
            subscriptions: Arc::new(Mutex::new(HashMap::new())),
        };
        
        (manager, connection)
    }
    
    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<(), rumqttc::ClientError> {
        self.client.subscribe(topic, qos)?;
        self.subscriptions.lock().unwrap().insert(topic.to_string(), qos);
        Ok(())
    }
    
    fn publish(&mut self, topic: &str, qos: QoS, retain: bool, payload: &[u8]) 
        -> Result<(), rumqttc::ClientError> {
        self.client.publish(topic, qos, retain, payload)
    }
    
    fn resubscribe_all(&mut self) -> Result<(), rumqttc::ClientError> {
        let subs = self.subscriptions.lock().unwrap();
        for (topic, qos) in subs.iter() {
            self.client.subscribe(topic, *qos)?;
        }
        Ok(())
    }
}

fn handle_events(mut connection: Connection, manager: Arc<Mutex<MQTTManager>>) {
    for notification in connection.iter() {
        match notification {
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Connected to broker");
                // Resubscribe to all topics after reconnection
                if let Ok(mut mgr) = manager.lock() {
                    if let Err(e) = mgr.resubscribe_all() {
                        eprintln!("Resubscription failed: {:?}", e);
                    }
                }
            }
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let payload = String::from_utf8_lossy(&publish.payload);
                println!("[{}] {}", publish.topic, payload);
            }
            Ok(Event::Incoming(Packet::SubAck(suback))) => {
                println!("Subscription confirmed: {:?}", suback);
            }
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                std::thread::sleep(Duration::from_secs(5));
            }
            _ => {}
        }
    }
}

fn main() {
    let (manager, connection) = MQTTManager::new(
        "advanced_rust_client", 
        "localhost", 
        1883
    );
    
    let manager = Arc::new(Mutex::new(manager));
    let manager_clone = Arc::clone(&manager);
    
    // Spawn event handler thread
    std::thread::spawn(move || {
        handle_events(connection, manager_clone);
    });
    
    // Wait for connection
    std::thread::sleep(Duration::from_secs(1));
    
    // Subscribe to topics
    {
        let mut mgr = manager.lock().unwrap();
        mgr.subscribe("sensor/+/temperature", QoS::AtLeastOnce)
            .expect("Subscribe failed");
        mgr.subscribe("sensor/+/humidity", QoS::AtLeastOnce)
            .expect("Subscribe failed");
    }
    
    // Publish sensor data
    for i in 0..20 {
        let sensor_id = i % 3;
        let temp_topic = format!("sensor/{}/temperature", sensor_id);
        let hum_topic = format!("sensor/{}/humidity", sensor_id);
        
        let temp_payload = format!("{{\"value\": {}}}", 18.0 + (i as f32) * 0.5);
        let hum_payload = format!("{{\"value\": {}}}", 45.0 + (i as f32) * 2.0);
        
        let mut mgr = manager.lock().unwrap();
        mgr.publish(&temp_topic, QoS::AtLeastOnce, false, temp_payload.as_bytes())
            .expect("Publish failed");
        mgr.publish(&hum_topic, QoS::AtLeastOnce, false, hum_payload.as_bytes())
            .expect("Publish failed");
        
        std::thread::sleep(Duration::from_millis(500));
    }
    
    std::thread::sleep(Duration::from_secs(5));
}
```

## Performance Optimization

### Memory Management

```conf
# Limit message queue size
max_queued_messages 1000

# Maximum QoS 1 and 2 messages in flight
max_inflight_messages 20

# Message size limits
message_size_limit 268435456  # 256MB

# Memory optimization
max_queued_bytes 0  # 0 = unlimited
```

### Connection Limits

```conf
# Global connection limit
max_connections -1  # -1 = unlimited

# Per-listener limits
listener 1883
max_connections 1000

# Connection timeout
persistent_client_expiration 1h
```

### Persistence Tuning

```conf
# Persistence settings
persistence true
persistence_location /var/lib/mosquitto/
autosave_interval 300  # 5 minutes
autosave_on_changes false

# Database file
persistence_file mosquitto.db
```

## Summary

**Mosquitto Broker Configuration** encompasses the setup and optimization of the Eclipse Mosquitto MQTT broker for production environments. Key aspects include configuring multiple listeners for different protocols (MQTT, WebSockets), implementing security through TLS/SSL encryption and authentication, managing persistence for message durability, and optimizing performance through connection limits and memory management. The broker supports flexible access control lists, multiple authentication backends, and can be fine-tuned for specific use cases ranging from IoT sensor networks to enterprise messaging systems. Proper configuration ensures reliable, secure, and performant message delivery across diverse MQTT client implementations in C/C++, Rust, and other languages.