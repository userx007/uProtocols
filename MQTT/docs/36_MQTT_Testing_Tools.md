# MQTT Testing Tools

## Overview

MQTT testing tools are essential utilities for developers working with MQTT-based systems. They enable you to publish messages, subscribe to topics, debug message flows, and monitor broker activity without writing full applications. The most commonly used tools include **mosquitto_pub** and **mosquitto_sub** (command-line utilities from the Eclipse Mosquitto project) and **MQTT Explorer** (a comprehensive GUI tool).

## Core Testing Tools

### 1. mosquitto_pub (Command-Line Publisher)

A command-line utility for publishing MQTT messages to a broker. It's invaluable for testing subscriptions, simulating sensors, or triggering events.

**Key Features:**
- Publish to any topic with custom payloads
- Support for QoS levels (0, 1, 2)
- Retained message publishing
- Authentication (username/password, TLS)
- Single or repeated message publishing

**Common Usage Examples:**

```bash
# Basic publish
mosquitto_pub -h broker.example.com -t "sensors/temperature" -m "23.5"

# Publish with QoS 1
mosquitto_pub -h localhost -t "home/living/light" -m "ON" -q 1

# Publish retained message
mosquitto_pub -h localhost -t "config/version" -m "v1.2.3" -r

# Publish with authentication
mosquitto_pub -h broker.example.com -t "secure/data" -m "secret" -u username -P password

# Publish from file
mosquitto_pub -h localhost -t "logs/system" -f logfile.txt

# Publish repeated messages (every 5 seconds)
mosquitto_pub -h localhost -t "heartbeat" -m "alive" -r -l
```

### 2. mosquitto_sub (Command-Line Subscriber)

A command-line utility for subscribing to MQTT topics and displaying received messages.

**Key Features:**
- Subscribe to single or multiple topics
- Wildcard topic support (+ and #)
- Display message metadata (topic, QoS, retain flag)
- Save messages to files
- Support for authentication and TLS

**Common Usage Examples:**

```bash
# Basic subscription
mosquitto_sub -h broker.example.com -t "sensors/temperature"

# Subscribe to multiple topics
mosquitto_sub -h localhost -t "sensors/+" -t "actuators/#"

# Subscribe with verbose output (shows topic for each message)
mosquitto_sub -h localhost -t "#" -v

# Subscribe with authentication
mosquitto_sub -h broker.example.com -t "secure/#" -u username -P password

# Subscribe and show all metadata
mosquitto_sub -h localhost -t "#" -v -F "@Y-@m-@dT@H:@M:@S@z : %t : %p"

# Subscribe to all topics (wildcard)
mosquitto_sub -h localhost -t "#" -v
```

### 3. MQTT Explorer (GUI Tool)

MQTT Explorer is a powerful graphical tool that provides a comprehensive view of your MQTT broker and message flow.

**Key Features:**
- Visual topic hierarchy tree
- Real-time message monitoring
- Message history with timestamps
- Publish messages through GUI
- Connection management (multiple brokers)
- Search and filter capabilities
- Statistics and metrics
- Export/import functionality

**Main Use Cases:**
- Visualizing complex topic structures
- Debugging message flows in development
- Monitoring production systems
- Understanding retained messages
- Testing topic hierarchies and wildcards

## Code Examples

### C/C++ Testing Helper (Using Eclipse Paho)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "TestClient"
#define TIMEOUT     10000L

// Simple test publisher
int publish_test_message(const char* topic, const char* payload, int qos) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Connect
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }

    // Prepare message
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = qos;
    pubmsg.retained = 0;

    // Publish
    MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    printf("Publishing to topic '%s': %s\n", topic, payload);
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message with delivery token %d delivered (QoS %d)\n", token, qos);

    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}

// Simple test subscriber with timeout
void subscribe_test(const char* topic, int timeout_ms) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    char* topicName = NULL;
    int topicLen;
    MQTTClient_message* message = NULL;

    MQTTClient_create(&client, ADDRESS, "TestSubscriber",
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return;
    }

    printf("Subscribing to topic '%s' for %d ms\n", topic, timeout_ms);
    MQTTClient_subscribe(client, topic, 0);

    // Receive messages for specified timeout
    long start_time = MQTTClient_getVersionInfo()->timestamp;
    while ((MQTTClient_getVersionInfo()->timestamp - start_time) < timeout_ms) {
        rc = MQTTClient_receive(client, &topicName, &topicLen, &message, 1000);
        
        if (message != NULL) {
            printf("Received on '%s': %.*s (QoS %d)\n",
                   topicName, message->payloadlen, (char*)message->payload,
                   message->qos);
            
            MQTTClient_freeMessage(&message);
            MQTTClient_free(topicName);
        }
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
}

// Test utility: publish multiple messages
void stress_test_publish(const char* topic_prefix, int count) {
    char topic[256];
    char payload[256];
    
    for (int i = 0; i < count; i++) {
        snprintf(topic, sizeof(topic), "%s/msg%d", topic_prefix, i);
        snprintf(payload, sizeof(payload), "Test message %d - timestamp: %ld", 
                 i, time(NULL));
        
        publish_test_message(topic, payload, 0);
        usleep(100000); // 100ms delay
    }
}

int main(int argc, char* argv[]) {
    // Example: Run stress test
    printf("=== MQTT Testing Tool - C Example ===\n\n");
    
    // Test 1: Simple publish
    publish_test_message("test/simple", "Hello MQTT", 0);
    
    // Test 2: QoS levels
    publish_test_message("test/qos0", "QoS 0 message", 0);
    publish_test_message("test/qos1", "QoS 1 message", 1);
    publish_test_message("test/qos2", "QoS 2 message", 2);
    
    // Test 3: Stress test
    stress_test_publish("stress/test", 10);
    
    return 0;
}
```

### Rust Testing Helper (Using rumqttc)

```rust
use rumqttc::{Client, MqttOptions, QoS};
use std::time::{Duration, SystemTime};
use std::thread;

/// Simple MQTT test publisher
pub struct MqttTestPublisher {
    client: Client,
}

impl MqttTestPublisher {
    pub fn new(broker: &str, port: u16, client_id: &str) -> Self {
        let mut mqttoptions = MqttOptions::new(client_id, broker, port);
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut connection) = Client::new(mqttoptions, 10);
        
        // Spawn connection handler
        thread::spawn(move || {
            for notification in connection.iter() {
                match notification {
                    Ok(event) => {
                        // Handle events silently for test tool
                    }
                    Err(e) => {
                        eprintln!("Connection error: {:?}", e);
                        break;
                    }
                }
            }
        });
        
        thread::sleep(Duration::from_millis(500)); // Wait for connection
        
        Self { client }
    }
    
    /// Publish a test message
    pub fn publish(&self, topic: &str, payload: &str, qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        println!("Publishing to '{}': {}", topic, payload);
        self.client.publish(topic, qos, false, payload.as_bytes())?;
        Ok(())
    }
    
    /// Publish a retained message
    pub fn publish_retained(&self, topic: &str, payload: &str, qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        println!("Publishing retained to '{}': {}", topic, payload);
        self.client.publish(topic, qos, true, payload.as_bytes())?;
        Ok(())
    }
    
    /// Stress test: publish multiple messages
    pub fn stress_test(&self, topic_prefix: &str, count: usize, delay_ms: u64) -> Result<(), Box<dyn std::error::Error>> {
        for i in 0..count {
            let topic = format!("{}/msg{}", topic_prefix, i);
            let timestamp = SystemTime::now()
                .duration_since(SystemTime::UNIX_EPOCH)?
                .as_secs();
            let payload = format!("Test message {} - timestamp: {}", i, timestamp);
            
            self.publish(&topic, &payload, QoS::AtMostOnce)?;
            thread::sleep(Duration::from_millis(delay_ms));
        }
        Ok(())
    }
    
    /// Test QoS levels
    pub fn test_qos_levels(&self, topic_prefix: &str) -> Result<(), Box<dyn std::error::Error>> {
        self.publish(&format!("{}/qos0", topic_prefix), "QoS 0 message", QoS::AtMostOnce)?;
        self.publish(&format!("{}/qos1", topic_prefix), "QoS 1 message", QoS::AtLeastOnce)?;
        self.publish(&format!("{}/qos2", topic_prefix), "QoS 2 message", QoS::ExactlyOnce)?;
        Ok(())
    }
}

/// Simple MQTT test subscriber
pub struct MqttTestSubscriber {
    client: Client,
}

impl MqttTestSubscriber {
    pub fn new(broker: &str, port: u16, client_id: &str) -> Self {
        let mut mqttoptions = MqttOptions::new(client_id, broker, port);
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        
        let (client, _connection) = Client::new(mqttoptions, 10);
        
        Self { client }
    }
    
    /// Subscribe to a topic
    pub fn subscribe(&self, topic: &str, qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        println!("Subscribing to '{}'", topic);
        self.client.subscribe(topic, qos)?;
        Ok(())
    }
    
    /// Subscribe to multiple topics
    pub fn subscribe_multiple(&self, topics: &[&str], qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        for topic in topics {
            self.subscribe(topic, qos)?;
        }
        Ok(())
    }
}

// Example usage and tests
fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== MQTT Testing Tool - Rust Example ===\n");
    
    // Create test publisher
    let publisher = MqttTestPublisher::new("localhost", 1883, "RustTestPub");
    
    // Test 1: Simple publish
    publisher.publish("test/rust/simple", "Hello from Rust", QoS::AtMostOnce)?;
    
    // Test 2: Retained message
    publisher.publish_retained("test/rust/config", "v1.0.0", QoS::AtLeastOnce)?;
    
    // Test 3: QoS levels
    publisher.test_qos_levels("test/rust")?;
    
    // Test 4: Stress test (10 messages, 100ms apart)
    publisher.stress_test("stress/rust", 10, 100)?;
    
    // Wait for messages to be sent
    thread::sleep(Duration::from_secs(2));
    
    println!("\nAll tests completed!");
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_publisher_creation() {
        let publisher = MqttTestPublisher::new("localhost", 1883, "TestClient");
        // If we get here without panic, connection was successful
    }
    
    #[test]
    fn test_publish_message() {
        let publisher = MqttTestPublisher::new("localhost", 1883, "TestClient");
        let result = publisher.publish("test/unit", "test payload", QoS::AtMostOnce);
        assert!(result.is_ok());
    }
}
```

## Practical Testing Workflows

### Workflow 1: Debug a Subscription Issue
```bash
# Terminal 1: Subscribe and monitor
mosquitto_sub -h localhost -t "sensors/#" -v

# Terminal 2: Publish test messages
mosquitto_pub -h localhost -t "sensors/temp" -m "22.5"
mosquitto_pub -h localhost -t "sensors/humidity" -m "65"
```

### Workflow 2: Test Retained Messages
```bash
# Publish a retained message
mosquitto_pub -h localhost -t "config/version" -m "v2.0.0" -r

# New subscriber receives retained message immediately
mosquitto_sub -h localhost -t "config/version" -v
```

### Workflow 3: Wildcard Testing
```bash
# Subscribe to single-level wildcard
mosquitto_sub -h localhost -t "home/+/temperature" -v

# Subscribe to multi-level wildcard
mosquitto_sub -h localhost -t "home/#" -v
```

### Workflow 4: Performance Testing
```bash
# Publisher script for load testing
for i in {1..1000}; do
  mosquitto_pub -h localhost -t "load/test" -m "Message $i"
done
```

## Summary

**MQTT testing tools** are indispensable for developing and debugging MQTT-based systems:

- **mosquitto_pub**: Command-line publisher for sending test messages, supporting various QoS levels, retained messages, and authentication
- **mosquitto_sub**: Command-line subscriber for monitoring topics with wildcard support and verbose output options
- **MQTT Explorer**: GUI tool providing visual topic hierarchies, real-time monitoring, and comprehensive broker insights

These tools enable developers to:
- Quickly test publish/subscribe functionality without writing code
- Debug message flows and topic structures
- Simulate sensors and devices
- Monitor production brokers
- Validate QoS behavior and retained messages
- Perform stress testing and performance validation

The code examples in C/C++ and Rust demonstrate how to build custom testing utilities for automated testing, integration into CI/CD pipelines, and specialized testing scenarios. Whether using command-line tools for quick debugging or building automated test suites, these testing approaches are essential for robust MQTT application development.