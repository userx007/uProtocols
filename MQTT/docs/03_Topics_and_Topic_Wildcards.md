# MQTT Topics and Topic Wildcards

## Overview

MQTT topics are the routing mechanism that enables publish-subscribe messaging. They function as UTF-8 encoded strings that form a hierarchical namespace, allowing publishers and subscribers to communicate without direct connections. Topics use forward slashes (/) as level separators, creating a tree-like structure that organizes messages logically.

## Topic Naming Conventions

### Basic Rules

Topics in MQTT follow specific conventions:

- **Case-sensitive**: `sensor/temperature` and `Sensor/Temperature` are different topics
- **UTF-8 encoded**: Support for international characters
- **Forward slash separator**: `/` creates hierarchy levels
- **No leading slash recommended**: `home/kitchen/temp` preferred over `/home/kitchen/temp`
- **Maximum length**: Typically 65,535 bytes, though brokers may impose lower limits

### Best Practices

1. **Use meaningful hierarchies**: Organize from general to specific (e.g., `building/floor/room/device/metric`)
2. **Avoid spaces**: Use underscores or hyphens instead
3. **Keep it readable**: Balance between descriptive and concise
4. **Consistent naming**: Establish conventions across your system
5. **Avoid special characters**: Stick to alphanumerics, hyphens, and underscores where possible

### Example Hierarchy

```
home/
├── living_room/
│   ├── temperature
│   ├── humidity
│   └── light/status
├── kitchen/
│   ├── temperature
│   └── refrigerator/door
└── bedroom/
    ├── temperature
    └── alarm/status
```

## Topic Wildcards

MQTT provides two wildcard characters for flexible subscriptions:

### Single-Level Wildcard (+)

The `+` wildcard matches exactly one topic level. It can appear anywhere in the subscription pattern but must occupy an entire level.

**Valid examples:**
- `home/+/temperature` matches `home/kitchen/temperature` and `home/bedroom/temperature`
- `+/kitchen/temperature` matches `home/kitchen/temperature` and `office/kitchen/temperature`
- `home/kitchen/+` matches `home/kitchen/temperature` and `home/kitchen/humidity`

**Invalid examples:**
- `home/kit+en/temperature` (must occupy entire level)

### Multi-Level Wildcard (#)

The `#` wildcard matches zero or more topic levels and must be the last character in the subscription.

**Valid examples:**
- `home/#` matches `home/kitchen/temperature`, `home/bedroom/light/status`, and even `home`
- `home/kitchen/#` matches everything under `home/kitchen/`

**Invalid examples:**
- `home/#/temperature` (# must be last)
- `home#` (must occupy entire level)

## Code Examples

### C/C++ Implementation

Here's a practical example using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "ExampleClient"
#define QOS         1
#define TIMEOUT     10000L

// Callback for incoming messages
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Parse topic hierarchy
    char *token;
    char topic_copy[256];
    strncpy(topic_copy, topicName, sizeof(topic_copy) - 1);
    
    printf("Topic hierarchy: ");
    token = strtok(topic_copy, "/");
    while (token != NULL) {
        printf("[%s] ", token);
        token = strtok(NULL, "/");
    }
    printf("\n\n");
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID, 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);

    // Connect to broker
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Connected to MQTT broker\n");

    // Subscribe to various topics with wildcards
    printf("Subscribing to topics...\n");
    
    // Subscribe to all temperatures in home
    MQTTClient_subscribe(client, "home/+/temperature", QOS);
    printf("Subscribed to: home/+/temperature\n");
    
    // Subscribe to everything in kitchen
    MQTTClient_subscribe(client, "home/kitchen/#", QOS);
    printf("Subscribed to: home/kitchen/#\n");
    
    // Subscribe to all light statuses
    MQTTClient_subscribe(client, "+/+/light/status", QOS);
    printf("Subscribed to: +/+/light/status\n");

    // Publish some test messages
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    // Publish temperature readings
    const char *temp_topics[] = {
        "home/living_room/temperature",
        "home/kitchen/temperature",
        "home/bedroom/temperature"
    };
    
    for (int i = 0; i < 3; i++) {
        char payload[32];
        snprintf(payload, sizeof(payload), "%.1f", 20.0 + i * 2.5);
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        
        MQTTClient_publishMessage(client, temp_topics[i], &pubmsg, &token);
        printf("Published: %s -> %s\n", temp_topics[i], payload);
        MQTTClient_waitForCompletion(client, token, TIMEOUT);
    }
    
    // Keep running to receive messages
    printf("\nWaiting for messages... (Press Ctrl+C to exit)\n");
    while (1) {
        #ifdef _WIN32
            Sleep(1000);
        #else
            sleep(1);
        #endif
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

### C++ Object-Oriented Approach

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <mqtt/async_client.h>

class TopicManager {
private:
    std::string base_topic;
    
public:
    TopicManager(const std::string& base) : base_topic(base) {}
    
    // Build hierarchical topic
    std::string build_topic(const std::vector<std::string>& levels) {
        std::string topic = base_topic;
        for (const auto& level : levels) {
            topic += "/" + level;
        }
        return topic;
    }
    
    // Generate wildcard pattern
    std::string single_level_wildcard(const std::vector<std::string>& levels, 
                                      int wildcard_position) {
        std::string topic = base_topic;
        for (size_t i = 0; i < levels.size(); i++) {
            topic += "/";
            topic += (i == wildcard_position) ? "+" : levels[i];
        }
        return topic;
    }
    
    std::string multi_level_wildcard(const std::vector<std::string>& levels) {
        std::string topic = base_topic;
        for (const auto& level : levels) {
            topic += "/" + level;
        }
        topic += "/#";
        return topic;
    }
};

class MQTTCallback : public virtual mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "Connection lost: " << cause << std::endl;
    }
    
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "  Topic: " << msg->get_topic() << std::endl;
        std::cout << "  Payload: " << msg->to_string() << std::endl;
        
        // Parse topic levels
        std::string topic = msg->get_topic();
        std::cout << "  Levels: ";
        size_t pos = 0;
        while ((pos = topic.find('/')) != std::string::npos) {
            std::cout << "[" << topic.substr(0, pos) << "] ";
            topic.erase(0, pos + 1);
        }
        std::cout << "[" << topic << "]" << std::endl;
    }
};

int main() {
    const std::string SERVER_ADDRESS("tcp://localhost:1883");
    const std::string CLIENT_ID("CPPClient");
    
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    MQTTCallback cb;
    client.set_callback(cb);
    
    TopicManager tm("home");
    
    try {
        std::cout << "Connecting to broker..." << std::endl;
        client.connect()->wait();
        std::cout << "Connected!" << std::endl;
        
        // Subscribe with wildcards
        std::vector<std::string> subscriptions = {
            tm.single_level_wildcard({"living_room"}, 0) + "/temperature",
            tm.multi_level_wildcard({"kitchen"}),
            "sensors/+/+/data"
        };
        
        for (const auto& sub : subscriptions) {
            client.subscribe(sub, 1)->wait();
            std::cout << "Subscribed to: " << sub << std::endl;
        }
        
        // Publish test messages
        std::vector<std::string> locations = {"living_room", "kitchen", "bedroom"};
        for (const auto& loc : locations) {
            std::string topic = tm.build_topic({loc, "temperature"});
            client.publish(topic, "22.5", 1, false)->wait();
            std::cout << "Published to: " << topic << std::endl;
        }
        
        // Keep running
        std::cout << "Waiting for messages..." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::error::Error;

// Topic builder for hierarchical topics
struct TopicBuilder {
    base: String,
}

impl TopicBuilder {
    fn new(base: &str) -> Self {
        TopicBuilder {
            base: base.to_string(),
        }
    }
    
    fn build(&self, levels: &[&str]) -> String {
        let mut topic = self.base.clone();
        for level in levels {
            topic.push('/');
            topic.push_str(level);
        }
        topic
    }
    
    fn single_wildcard(&self, levels: &[&str], wildcard_pos: usize) -> String {
        let mut topic = self.base.clone();
        for (i, level) in levels.iter().enumerate() {
            topic.push('/');
            if i == wildcard_pos {
                topic.push('+');
            } else {
                topic.push_str(level);
            }
        }
        topic
    }
    
    fn multi_wildcard(&self, levels: &[&str]) -> String {
        format!("{}/{}/#", self.base, levels.join("/"))
    }
}

// Parse topic into levels
fn parse_topic(topic: &str) -> Vec<&str> {
    topic.split('/').collect()
}

// Check if topic matches pattern with wildcards
fn topic_matches(topic: &str, pattern: &str) -> bool {
    let topic_parts: Vec<&str> = topic.split('/').collect();
    let pattern_parts: Vec<&str> = pattern.split('/').collect();
    
    if pattern_parts.last() == Some(&"#") {
        // Multi-level wildcard
        if topic_parts.len() < pattern_parts.len() - 1 {
            return false;
        }
        for i in 0..pattern_parts.len() - 1 {
            if pattern_parts[i] != "+" && pattern_parts[i] != topic_parts[i] {
                return false;
            }
        }
        true
    } else {
        // No multi-level wildcard or single-level only
        if topic_parts.len() != pattern_parts.len() {
            return false;
        }
        for i in 0..pattern_parts.len() {
            if pattern_parts[i] != "+" && pattern_parts[i] != topic_parts[i] {
                return false;
            }
        }
        true
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_client", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Clone client for publishing task
    let pub_client = client.clone();
    
    // Spawn task to handle incoming messages
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    let topic = String::from_utf8_lossy(&p.topic);
                    let payload = String::from_utf8_lossy(&p.payload);
                    
                    println!("\n📨 Message received:");
                    println!("  Topic: {}", topic);
                    println!("  Payload: {}", payload);
                    
                    // Parse and display topic hierarchy
                    let levels = parse_topic(&topic);
                    println!("  Hierarchy: {:?}", levels);
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Error: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });
    
    // Wait for connection
    sleep(Duration::from_secs(1)).await;
    
    let builder = TopicBuilder::new("home");
    
    // Subscribe to various topics with wildcards
    println!("📡 Subscribing to topics...\n");
    
    // All temperatures in home
    let sub1 = builder.single_wildcard(&["living_room"], 0) + "/temperature";
    client.subscribe(&sub1, QoS::AtLeastOnce).await?;
    println!("✓ Subscribed: {}", sub1);
    
    // Everything in kitchen
    let sub2 = builder.multi_wildcard(&["kitchen"]);
    client.subscribe(&sub2, QoS::AtLeastOnce).await?;
    println!("✓ Subscribed: {}", sub2);
    
    // All sensor data with two-level hierarchy
    client.subscribe("sensors/+/+/data", QoS::AtLeastOnce).await?;
    println!("✓ Subscribed: sensors/+/+/data");
    
    // All device status updates
    client.subscribe("#", QoS::AtLeastOnce).await?;
    println!("✓ Subscribed: # (all topics)\n");
    
    sleep(Duration::from_secs(1)).await;
    
    // Publish test messages
    println!("📤 Publishing messages...\n");
    
    let locations = vec!["living_room", "kitchen", "bedroom"];
    for location in &locations {
        let topic = builder.build(&[location, "temperature"]);
        let payload = format!("{{\"value\": 22.5, \"unit\": \"C\"}}");
        
        pub_client.publish(&topic, QoS::AtLeastOnce, false, payload.as_bytes()).await?;
        println!("✓ Published to: {}", topic);
        sleep(Duration::from_millis(100)).await;
    }
    
    // Publish to kitchen subtopics
    let kitchen_topics = vec![
        ("home/kitchen/humidity", "65"),
        ("home/kitchen/refrigerator/door", "closed"),
        ("home/kitchen/oven/temperature", "180"),
    ];
    
    for (topic, value) in kitchen_topics {
        pub_client.publish(topic, QoS::AtLeastOnce, false, value.as_bytes()).await?;
        println!("✓ Published to: {}", topic);
        sleep(Duration::from_millis(100)).await;
    }
    
    // Demonstrate pattern matching
    println!("\n🔍 Testing pattern matching:");
    let test_cases = vec![
        ("home/living_room/temperature", "home/+/temperature", true),
        ("home/bedroom/humidity", "home/+/temperature", false),
        ("home/kitchen/oven/status", "home/kitchen/#", true),
        ("sensors/temp/room1/data", "sensors/+/+/data", true),
    ];
    
    for (topic, pattern, expected) in test_cases {
        let matches = topic_matches(topic, pattern);
        println!("  {} {} {} → {}",
                 topic,
                 if matches { "✓" } else { "✗" },
                 pattern,
                 if matches == expected { "PASS" } else { "FAIL" });
    }
    
    // Keep running
    println!("\n⏳ Waiting for messages (press Ctrl+C to exit)...");
    sleep(Duration::from_secs(3600)).await;
    
    Ok(())
}
```

## Summary

MQTT topics provide a flexible, hierarchical routing mechanism for publish-subscribe messaging. The forward-slash separator creates intuitive organizational structures, while wildcard subscriptions (`+` for single-level and `#` for multi-level) enable powerful pattern-based message consumption. 

Key takeaways:
- **Topics are UTF-8 strings** organized hierarchically with `/` separators
- **Single-level wildcard `+`** matches exactly one level in the hierarchy
- **Multi-level wildcard `#`** matches zero or more levels and must be the final character
- **Best practices** include meaningful hierarchies, consistent naming, and avoiding special characters
- **Implementation** is straightforward across languages, with libraries providing subscribe/publish APIs that handle topic matching automatically

Understanding topic design and wildcard usage is fundamental to building scalable, maintainable MQTT-based systems. Proper topic hierarchies enable efficient message routing, simplified subscriptions, and logical organization of IoT data streams.