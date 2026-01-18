# MQTT Session Management: Clean Session vs Persistent Session

## Overview

Session management is a critical aspect of MQTT that determines how the broker handles client state and message queuing when clients disconnect and reconnect. The choice between clean and persistent sessions significantly impacts message delivery guarantees, resource usage, and overall system behavior.

## Fundamental Concepts

### What is an MQTT Session?

An MQTT session represents the stateful connection between a client and broker, storing:
- **Subscriptions**: Active topic subscriptions the client has registered
- **QoS 1 and QoS 2 messages**: Unacknowledged messages waiting for delivery
- **Pending QoS 2 messages**: Messages in mid-flight for QoS 2 delivery
- **Queued messages**: Messages that arrived while the client was disconnected

### Clean Session (MQTT 3.1.1)

When a client connects with the Clean Session flag set to `true`:
- The broker discards any previous session state
- No messages are queued while the client is disconnected
- All subscriptions are removed when the client disconnects
- Ideal for clients that don't need to receive missed messages

### Persistent Session (MQTT 3.1.1)

When Clean Session is set to `false`:
- The broker maintains session state across disconnections
- Messages are queued during disconnection (subject to broker limits)
- Subscriptions persist across reconnections
- Critical for reliable message delivery in unstable networks

### Session Expiry (MQTT 5.0)

MQTT 5.0 introduces more granular control through the Session Expiry Interval property, which specifies how long the broker should maintain session state after disconnection (in seconds).

## Impact on Message Delivery

| Aspect | Clean Session | Persistent Session |
|--------|---------------|-------------------|
| Message queuing | No queuing when disconnected | Messages queued during disconnection |
| Subscriptions | Lost on disconnect | Maintained across reconnects |
| Resource usage | Minimal broker resources | Higher broker resource consumption |
| Use case | Real-time data, ephemeral clients | Critical notifications, guaranteed delivery |

## C/C++ Implementation Examples

### Using Eclipse Paho MQTT C Library

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://mqtt.example.com:1883"
#define CLIENTID    "ExampleClientPersistent"
#define TOPIC       "test/session"
#define QOS         1
#define TIMEOUT     10000L

// Persistent Session Example
void persistent_session_example() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);

    // Configure for persistent session
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0;  // Persistent session
    conn_opts.reliable = 1;

    // Connect
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Connected with persistent session\n");

    // Subscribe to topic - subscription persists across disconnects
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Subscribed to %s (will persist)\n", TOPIC);

    // Simulate some work
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    pubmsg.payload = "Persistent message";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, TIMEOUT);

    // Disconnect - session state preserved
    MQTTClient_disconnect(client, 10000);
    printf("Disconnected - session preserved on broker\n");

    MQTTClient_destroy(&client);
}

// Clean Session Example
void clean_session_example() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    // Configure for clean session
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;  // Clean session - no state preserved

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Connected with clean session\n");

    // Subscribe - will be lost on disconnect
    MQTTClient_subscribe(client, TOPIC, QOS);
    printf("Subscribed to %s (temporary)\n", TOPIC);

    // Publish and disconnect
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    pubmsg.payload = "Ephemeral message";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = 0;  // QoS 0 for clean sessions
    pubmsg.retained = 0;

    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);

    MQTTClient_disconnect(client, 10000);
    printf("Disconnected - all session state cleared\n");

    MQTTClient_destroy(&client);
}

// Message arrival callback for receiving queued messages
int message_arrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Reconnecting and receiving queued messages
void receive_queued_messages() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);

    // Set callback for message arrival
    MQTTClient_setCallbacks(client, NULL, NULL, message_arrived, NULL);

    // Reconnect with persistent session
    conn_opts.cleansession = 0;
    conn_opts.keepAliveInterval = 20;

    if (MQTTClient_connect(client, &conn_opts) == MQTTCLIENT_SUCCESS) {
        printf("Reconnected - receiving queued messages...\n");
        
        // Messages queued during disconnection will be delivered
        // Wait for messages
        getchar();  // Simple wait - replace with proper event loop
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
}

int main(int argc, char* argv[]) {
    printf("=== Persistent Session Demo ===\n");
    persistent_session_example();
    
    printf("\n=== Clean Session Demo ===\n");
    clean_session_example();
    
    printf("\n=== Receiving Queued Messages ===\n");
    receive_queued_messages();
    
    return 0;
}
```

### C++ with Modern Features

```cpp
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include "mqtt/async_client.h"

class SessionManager {
private:
    std::unique_ptr<mqtt::async_client> client_;
    std::string broker_address_;
    std::string client_id_;
    bool use_persistent_session_;

public:
    SessionManager(const std::string& broker, const std::string& client_id, 
                   bool persistent)
        : broker_address_(broker), 
          client_id_(client_id),
          use_persistent_session_(persistent) {
        
        // Choose persistence based on session type
        if (persistent) {
            client_ = std::make_unique<mqtt::async_client>(
                broker_address_, client_id_, 
                mqtt::create_options(MQTTVERSION_5));
        } else {
            client_ = std::make_unique<mqtt::async_client>(
                broker_address_, client_id_);
        }
    }

    void connect() {
        auto conn_opts = mqtt::connect_options_builder()
            .clean_session(!use_persistent_session_)
            .keep_alive_interval(std::chrono::seconds(20))
            .automatic_reconnect(std::chrono::seconds(2), 
                               std::chrono::seconds(30))
            .finalize();

        try {
            auto tok = client_->connect(conn_opts);
            tok->wait();
            
            std::cout << "Connected with " 
                      << (use_persistent_session_ ? "persistent" : "clean")
                      << " session\n";
        } catch (const mqtt::exception& exc) {
            std::cerr << "Connection failed: " << exc.what() << std::endl;
        }
    }

    void subscribe(const std::string& topic, int qos) {
        try {
            auto tok = client_->subscribe(topic, qos);
            tok->wait();
            std::cout << "Subscribed to " << topic 
                      << " (QoS " << qos << ")\n";
        } catch (const mqtt::exception& exc) {
            std::cerr << "Subscription failed: " << exc.what() << std::endl;
        }
    }

    void publish(const std::string& topic, const std::string& payload, int qos) {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(qos);
        
        try {
            auto tok = client_->publish(msg);
            tok->wait();
            std::cout << "Published message\n";
        } catch (const mqtt::exception& exc) {
            std::cerr << "Publish failed: " << exc.what() << std::endl;
        }
    }

    void disconnect() {
        try {
            auto tok = client_->disconnect();
            tok->wait();
            std::cout << "Disconnected\n";
        } catch (const mqtt::exception& exc) {
            std::cerr << "Disconnect failed: " << exc.what() << std::endl;
        }
    }

    void set_message_callback(std::function<void(mqtt::const_message_ptr)> cb) {
        client_->set_message_callback([cb](mqtt::const_message_ptr msg) {
            cb(msg);
        });
    }
};

int main() {
    const std::string BROKER = "tcp://mqtt.example.com:1883";
    const std::string CLIENT_ID = "cpp_session_demo";
    const std::string TOPIC = "test/session";

    // Persistent session example
    {
        std::cout << "=== Persistent Session Demo ===\n";
        SessionManager persistent_client(BROKER, CLIENT_ID, true);
        
        persistent_client.set_message_callback([](mqtt::const_message_ptr msg) {
            std::cout << "Received: " << msg->get_topic() 
                      << " -> " << msg->to_string() << std::endl;
        });

        persistent_client.connect();
        persistent_client.subscribe(TOPIC, 1);
        persistent_client.publish(TOPIC, "Persistent message", 1);
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        persistent_client.disconnect();
        
        // Session state preserved on broker
        std::cout << "Session preserved - messages will be queued\n\n";
    }

    // Clean session example
    {
        std::cout << "=== Clean Session Demo ===\n";
        SessionManager clean_client(BROKER, CLIENT_ID + "_clean", false);
        
        clean_client.connect();
        clean_client.subscribe(TOPIC, 0);
        clean_client.publish(TOPIC, "Ephemeral message", 0);
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        clean_client.disconnect();
        
        // All state cleared
        std::cout << "Session cleared - no message queuing\n";
    }

    return 0;
}
```

## Rust Implementation Examples

### Using rumqttc Library

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;

// Persistent Session Example
fn persistent_session_example() {
    println!("=== Persistent Session Demo ===");
    
    let mut mqtt_options = MqttOptions::new(
        "rust_persistent_client", 
        "mqtt.example.com", 
        1883
    );
    
    // Configure persistent session
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(false);  // Persistent session
    
    let (mut client, mut connection) = Client::new(mqtt_options, 10);
    
    // Subscribe to topic - subscription persists
    client.subscribe("test/session", QoS::AtLeastOnce).unwrap();
    println!("Subscribed with persistent session");
    
    // Publish a message
    client.publish(
        "test/session", 
        QoS::AtLeastOnce, 
        false, 
        b"Persistent message"
    ).unwrap();
    
    // Process some events
    for (i, notification) in connection.iter().enumerate() {
        match notification {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                println!("Received: {:?}", 
                    String::from_utf8_lossy(&publish.payload));
            }
            Ok(Event::Incoming(Packet::SubAck(_))) => {
                println!("Subscription confirmed");
            }
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                break;
            }
            _ => {}
        }
        
        if i >= 5 {
            break;
        }
    }
    
    // Disconnect - session preserved on broker
    client.disconnect().unwrap();
    println!("Disconnected - session state preserved\n");
}

// Clean Session Example
fn clean_session_example() {
    println!("=== Clean Session Demo ===");
    
    let mut mqtt_options = MqttOptions::new(
        "rust_clean_client", 
        "mqtt.example.com", 
        1883
    );
    
    // Configure clean session
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(true);  // Clean session
    
    let (mut client, mut connection) = Client::new(mqtt_options, 10);
    
    // Subscribe - will be lost on disconnect
    client.subscribe("test/session", QoS::AtMostOnce).unwrap();
    println!("Subscribed with clean session (temporary)");
    
    // Publish
    client.publish(
        "test/session", 
        QoS::AtMostOnce, 
        false, 
        b"Ephemeral message"
    ).unwrap();
    
    // Process events briefly
    for (i, _) in connection.iter().enumerate() {
        if i >= 3 {
            break;
        }
    }
    
    // Disconnect - all state cleared
    client.disconnect().unwrap();
    println!("Disconnected - all session state cleared\n");
}

// Reconnect and receive queued messages
fn receive_queued_messages() {
    println!("=== Receiving Queued Messages ===");
    
    let mut mqtt_options = MqttOptions::new(
        "rust_persistent_client",  // Same client ID as before
        "mqtt.example.com", 
        1883
    );
    
    mqtt_options.set_clean_session(false);  // Reconnect with persistent session
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    
    let (_client, mut connection) = Client::new(mqtt_options, 10);
    
    println!("Reconnected - processing queued messages...");
    
    // Receive all queued messages
    for notification in connection.iter() {
        match notification {
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                println!("Queued message: {} -> {}", 
                    publish.topic,
                    String::from_utf8_lossy(&publish.payload));
            }
            Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                println!("Connection acknowledged, session_present: {}", 
                    connack.session_present);
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
            _ => {}
        }
        
        thread::sleep(Duration::from_millis(100));
    }
}

// Advanced: MQTT 5.0 Session Expiry
fn mqtt5_session_expiry() {
    println!("=== MQTT 5.0 Session Expiry ===");
    
    let mut mqtt_options = MqttOptions::new(
        "rust_mqtt5_client", 
        "mqtt.example.com", 
        1883
    );
    
    // MQTT 5.0 specific settings
    mqtt_options.set_clean_session(false);
    
    // Session expiry interval (in seconds)
    // 0 = session ends when connection closes
    // 0xFFFFFFFF = session never expires
    // Other values = specific expiry time
    let session_expiry_interval = 3600;  // 1 hour
    
    println!("Session will expire after {} seconds of disconnection", 
        session_expiry_interval);
    
    // Note: Session expiry is set via MQTT 5.0 properties
    // Implementation depends on broker MQTT 5.0 support
}

// Message handler with session awareness
struct SessionAwareHandler {
    client_id: String,
    persistent: bool,
}

impl SessionAwareHandler {
    fn new(client_id: String, persistent: bool) -> Self {
        Self { client_id, persistent }
    }
    
    fn handle_connection(&self, session_present: bool) {
        if self.persistent && session_present {
            println!("Reconnected to existing session for {}", self.client_id);
            println!("Previous subscriptions and queued messages will be restored");
        } else if self.persistent && !session_present {
            println!("New persistent session created for {}", self.client_id);
            println!("Need to resubscribe to topics");
        } else {
            println!("Clean session started for {}", self.client_id);
        }
    }
    
    fn should_resubscribe(&self, session_present: bool) -> bool {
        // Only resubscribe if we're using persistent session but no session exists
        self.persistent && !session_present
    }
}

fn main() {
    // Run examples
    persistent_session_example();
    thread::sleep(Duration::from_secs(1));
    
    clean_session_example();
    thread::sleep(Duration::from_secs(1));
    
    receive_queued_messages();
    thread::sleep(Duration::from_secs(1));
    
    mqtt5_session_expiry();
}
```

### Async Rust with Tokio

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};

#[tokio::main]
async fn main() {
    persistent_session_async().await;
    clean_session_async().await;
}

async fn persistent_session_async() {
    println!("=== Async Persistent Session ===");
    
    let mut mqtt_options = MqttOptions::new(
        "async_persistent", 
        "mqtt.example.com", 
        1883
    );
    mqtt_options.set_clean_session(false);
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Spawn task to handle events
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    println!("Async received: {}", 
                        String::from_utf8_lossy(&p.payload));
                }
                Ok(Event::Incoming(Packet::ConnAck(ack))) => {
                    println!("Session present: {}", ack.session_present);
                }
                Err(e) => {
                    eprintln!("Event loop error: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
                _ => {}
            }
        }
    });
    
    // Subscribe and publish
    client.subscribe("test/async", QoS::AtLeastOnce).await.unwrap();
    
    for i in 0..5 {
        let payload = format!("Async message {}", i);
        client.publish(
            "test/async", 
            QoS::AtLeastOnce, 
            false, 
            payload.as_bytes()
        ).await.unwrap();
        
        sleep(Duration::from_millis(500)).await;
    }
    
    sleep(Duration::from_secs(2)).await;
    println!("Persistent session maintained\n");
}

async fn clean_session_async() {
    println!("=== Async Clean Session ===");
    
    let mut mqtt_options = MqttOptions::new(
        "async_clean", 
        "mqtt.example.com", 
        1883
    );
    mqtt_options.set_clean_session(true);
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    tokio::spawn(async move {
        while let Ok(event) = eventloop.poll().await {
            if let Event::Incoming(Packet::Publish(p)) = event {
                println!("Clean session received: {}", 
                    String::from_utf8_lossy(&p.payload));
            }
        }
    });
    
    client.subscribe("test/async/clean", QoS::AtMostOnce).await.unwrap();
    client.publish(
        "test/async/clean", 
        QoS::AtMostOnce, 
        false, 
        b"Clean message"
    ).await.unwrap();
    
    sleep(Duration::from_secs(1)).await;
    println!("Clean session - no state preserved\n");
}
```

## Best Practices and Recommendations

### When to Use Clean Sessions
- Real-time sensor data where only current values matter
- Status updates that become stale quickly
- High-frequency telemetry data
- Clients that connect frequently and don't need historical data
- Mobile apps showing current state only

### When to Use Persistent Sessions
- Critical notifications that must not be missed
- Command and control systems
- Financial transactions
- Device firmware updates
- Applications requiring guaranteed message delivery
- Clients with unstable network connections

### Resource Considerations
- Persistent sessions consume broker memory for queued messages
- Set appropriate message queue limits on the broker
- Use unique client IDs to avoid session conflicts
- Monitor broker resource usage with many persistent clients
- Consider session expiry intervals in MQTT 5.0 to clean up abandoned sessions

### Security Considerations
- Use authentication even with clean sessions
- Be aware that persistent sessions store data on the broker
- Implement client takeover protection (same client ID from different sources)
- Consider encrypting sensitive data in queued messages

## Summary

Session management in MQTT provides flexible control over message delivery reliability and client state persistence. **Clean sessions** offer lightweight, ephemeral connections ideal for real-time data streams where missed messages are acceptable, while **persistent sessions** ensure reliable message queuing and subscription persistence across network interruptions, making them essential for critical applications requiring guaranteed delivery.

The key tradeoffs involve balancing reliability against resource consumption: persistent sessions provide stronger delivery guarantees but require broker resources to maintain state and queue messages. MQTT 5.0's session expiry intervals add nuance by allowing time-limited persistence, helping manage broker resources while still supporting temporarily disconnected clients.

When implementing MQTT applications, carefully evaluate your message delivery requirements, network reliability, and resource constraints to choose the appropriate session strategy. Modern libraries in C/C++ and Rust provide straightforward APIs for configuring session behavior, allowing developers to implement robust messaging patterns that match their specific use cases.