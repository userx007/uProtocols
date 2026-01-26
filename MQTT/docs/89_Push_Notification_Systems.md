# Push Notification Systems with MQTT

## Overview

Push notification systems using MQTT leverage the protocol's lightweight, publish-subscribe architecture to deliver real-time notifications to clients. Unlike traditional HTTP polling, MQTT maintains persistent connections, enabling instant message delivery with minimal overhead and battery consumption—ideal for mobile and IoT applications.

## Architecture

The system typically consists of:
- **Notification Service**: Publishes notifications to MQTT topics
- **MQTT Broker**: Routes messages between publishers and subscribers
- **Client Applications**: Subscribe to relevant topics and display notifications
- **Topic Structure**: Organized hierarchically for efficient filtering

Common topic patterns:
- `notifications/user/{user_id}/alerts`
- `notifications/device/{device_id}/updates`
- `notifications/broadcast/all`
- `notifications/group/{group_id}/messages`

## C/C++ Implementation

Using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.example.com:1883"
#define CLIENTID    "NotificationClient_001"
#define QOS         1
#define TIMEOUT     10000L

// Callback for incoming messages
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("📬 Notification Received!\n");
    printf("Topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Parse JSON payload (simplified)
    char *payload = (char*)message->payload;
    
    // Display notification (platform-specific code would go here)
    // For example: show_system_notification(payload);
    
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
    
    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);
    
    // Configure connection
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "notification_user";
    conn_opts.password = "secure_password";
    
    // Connect
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to broker\n");
    
    // Subscribe to user-specific notifications
    const char *user_id = "user_12345";
    char topic[256];
    snprintf(topic, sizeof(topic), "notifications/user/%s/#", user_id);
    
    MQTTClient_subscribe(client, topic, QOS);
    printf("Subscribed to: %s\n", topic);
    
    // Also subscribe to broadcast notifications
    MQTTClient_subscribe(client, "notifications/broadcast/all", QOS);
    
    // Keep running to receive notifications
    printf("Waiting for notifications... Press Ctrl+C to exit\n");
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

### Publishing Notifications (C++)

```cpp
#include <iostream>
#include <string>
#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class NotificationPublisher {
private:
    mqtt::async_client client;
    const int QOS = 1;
    
public:
    NotificationPublisher(const std::string& broker_address, 
                         const std::string& client_id)
        : client(broker_address, client_id) {
        
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        
        try {
            client.connect(conn_opts)->wait();
            std::cout << "Publisher connected to broker" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
        }
    }
    
    void send_notification(const std::string& user_id,
                          const std::string& title,
                          const std::string& message,
                          const std::string& priority = "normal") {
        // Create JSON payload
        json notification = {
            {"title", title},
            {"message", message},
            {"timestamp", std::time(nullptr)},
            {"priority", priority},
            {"type", "alert"}
        };
        
        std::string payload = notification.dump();
        std::string topic = "notifications/user/" + user_id + "/alerts";
        
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(QOS);
        msg->set_retained(false);
        
        try {
            client.publish(msg)->wait();
            std::cout << "Notification sent to " << user_id << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Publish failed: " << exc.what() << std::endl;
        }
    }
    
    void broadcast_notification(const std::string& title,
                               const std::string& message) {
        json notification = {
            {"title", title},
            {"message", message},
            {"timestamp", std::time(nullptr)},
            {"type", "broadcast"}
        };
        
        std::string payload = notification.dump();
        auto msg = mqtt::make_message("notifications/broadcast/all", payload);
        msg->set_qos(QOS);
        
        client.publish(msg)->wait();
        std::cout << "Broadcast notification sent" << std::endl;
    }
    
    ~NotificationPublisher() {
        try {
            client.disconnect()->wait();
        } catch (...) {}
    }
};

int main() {
    NotificationPublisher publisher("tcp://broker.example.com:1883", 
                                   "NotificationService_001");
    
    // Send individual notification
    publisher.send_notification("user_12345", 
                               "New Message", 
                               "You have a new message from Alice",
                               "high");
    
    // Send broadcast
    publisher.broadcast_notification("System Maintenance",
                                    "Scheduled maintenance in 1 hour");
    
    return 0;
}
```

## Rust Implementation

Using the `rumqttc` library:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use tokio::time::Duration;

#[derive(Serialize, Deserialize, Debug)]
struct Notification {
    title: String,
    message: String,
    timestamp: i64,
    priority: String,
    #[serde(rename = "type")]
    notification_type: String,
}

// Notification subscriber
async fn notification_subscriber() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new(
        "notification_client_rust_001",
        "broker.example.com",
        1883
    );
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_credentials("notification_user", "secure_password");
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to notifications
    let user_id = "user_12345";
    let topic = format!("notifications/user/{}/#", user_id);
    client.subscribe(&topic, QoS::AtLeastOnce).await?;
    client.subscribe("notifications/broadcast/all", QoS::AtLeastOnce).await?;
    
    println!("Subscribed to notifications for user: {}", user_id);
    println!("Waiting for notifications...\n");
    
    // Process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(p))) => {
                let payload = String::from_utf8_lossy(&p.payload);
                
                println!("📬 Notification Received!");
                println!("Topic: {}", p.topic);
                
                // Parse JSON notification
                match serde_json::from_str::<Notification>(&payload) {
                    Ok(notification) => {
                        println!("Title: {}", notification.title);
                        println!("Message: {}", notification.message);
                        println!("Priority: {}", notification.priority);
                        println!("Type: {}", notification.notification_type);
                        
                        // Handle based on priority
                        match notification.priority.as_str() {
                            "high" | "urgent" => {
                                // Show system notification immediately
                                show_urgent_notification(&notification);
                            },
                            "normal" => {
                                // Queue for display
                                queue_notification(&notification);
                            },
                            _ => {}
                        }
                    },
                    Err(e) => {
                        println!("Failed to parse notification: {}", e);
                        println!("Raw payload: {}", payload);
                    }
                }
                println!("---\n");
            },
            Ok(_) => {},
            Err(e) => {
                eprintln!("Error: {}", e);
                tokio::time::sleep(Duration::from_secs(1)).await;
            }
        }
    }
}

fn show_urgent_notification(notification: &Notification) {
    // Platform-specific notification code
    println!("🔔 URGENT: {} - {}", notification.title, notification.message);
}

fn queue_notification(notification: &Notification) {
    // Add to notification queue
    println!("📝 Queued: {}", notification.title);
}

// Notification publisher
async fn notification_publisher() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new(
        "notification_service_rust_001",
        "broker.example.com",
        1883
    );
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn event loop
    tokio::spawn(async move {
        loop {
            let _ = eventloop.poll().await;
        }
    });
    
    // Wait for connection
    tokio::time::sleep(Duration::from_secs(1)).await;
    
    // Send individual notification
    let notification = Notification {
        title: "New Message".to_string(),
        message: "You have a new message from Bob".to_string(),
        timestamp: chrono::Utc::now().timestamp(),
        priority: "high".to_string(),
        notification_type: "alert".to_string(),
    };
    
    let payload = serde_json::to_string(&notification)?;
    let topic = "notifications/user/user_12345/alerts";
    
    client.publish(topic, QoS::AtLeastOnce, false, payload).await?;
    println!("Sent notification to user_12345");
    
    // Send broadcast notification
    let broadcast = Notification {
        title: "System Update".to_string(),
        message: "New features available!".to_string(),
        timestamp: chrono::Utc::now().timestamp(),
        priority: "normal".to_string(),
        notification_type: "broadcast".to_string(),
    };
    
    let broadcast_payload = serde_json::to_string(&broadcast)?;
    client.publish(
        "notifications/broadcast/all",
        QoS::AtLeastOnce,
        false,
        broadcast_payload
    ).await?;
    println!("Sent broadcast notification");
    
    tokio::time::sleep(Duration::from_secs(2)).await;
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Run publisher or subscriber based on argument
    let args: Vec<String> = std::env::args().collect();
    
    match args.get(1).map(String::as_str) {
        Some("publish") => notification_publisher().await,
        _ => notification_subscriber().await,
    }
}
```

## Summary

Push notification systems built on MQTT provide efficient, real-time message delivery with several key advantages:

**Benefits:**
- **Low latency**: Instant delivery through persistent connections
- **Efficient**: Minimal bandwidth and battery usage compared to polling
- **Scalable**: Broker handles routing for millions of clients
- **Flexible**: Topic-based filtering allows granular subscription control
- **Reliable**: QoS levels ensure delivery guarantees

**Key Features:**
- Hierarchical topic structure for user/device/group targeting
- JSON payloads for rich notification content
- Priority-based handling (urgent vs. normal)
- Support for broadcast and targeted notifications
- Connection state management and automatic reconnection

**Use Cases:**
- Mobile app notifications (chat, alerts, updates)
- IoT device alerts and warnings
- Real-time collaboration tools
- System monitoring and alerts
- Multi-user application updates

MQTT's publish-subscribe model makes it ideal for push notifications, offering better performance and user experience than traditional HTTP-based approaches while maintaining simplicity in implementation.