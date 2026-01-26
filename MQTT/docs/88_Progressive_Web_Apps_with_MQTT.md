# Progressive Web Apps with MQTT: Detailed Description

## Overview

Progressive Web Apps (PWAs) with MQTT combine the benefits of modern web applications with real-time messaging capabilities. This integration enables offline-first architectures where PWAs can queue messages locally when disconnected and synchronize data automatically when connectivity is restored.

## Key Concepts

**Progressive Web Apps (PWAs)** are web applications that provide native app-like experiences, including:
- Offline functionality via Service Workers
- Installation to home screen
- Push notifications
- Background sync capabilities

**MQTT Integration** adds:
- Real-time bidirectional communication
- Lightweight protocol ideal for mobile/web
- Quality of Service (QoS) guarantees
- Last Will and Testament for connection handling

## Architecture Components

1. **Service Worker**: Manages offline caching and background sync
2. **IndexedDB/LocalStorage**: Stores queued MQTT messages offline
3. **MQTT Client**: WebSocket-based MQTT connection
4. **Sync Manager**: Coordinates message queue and transmission

---

## C/C++ Implementation

While PWAs are primarily web-based, C/C++ can power backend services or embedded devices communicating with PWAs.

```cpp
#include <mosquitto.h>
#include <iostream>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>

class MQTTBridge {
private:
    struct mosquitto *mosq;
    std::queue<std::string> messageQueue;
    std::mutex queueMutex;
    bool isConnected;
    
    static void on_connect(struct mosquitto *mosq, void *obj, int rc) {
        MQTTBridge *bridge = static_cast<MQTTBridge*>(obj);
        if(rc == 0) {
            std::cout << "Connected successfully\n";
            bridge->isConnected = true;
            bridge->processQueue();
        }
    }
    
    static void on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
        MQTTBridge *bridge = static_cast<MQTTBridge*>(obj);
        bridge->isConnected = false;
        std::cout << "Disconnected. Messages will be queued.\n";
    }
    
    static void on_message(struct mosquitto *mosq, void *obj, 
                          const struct mosquitto_message *msg) {
        std::cout << "Received: " << (char*)msg->payload 
                  << " on topic: " << msg->topic << "\n";
    }

public:
    MQTTBridge(const std::string& id) : isConnected(false) {
        mosquitto_lib_init();
        mosq = mosquitto_new(id.c_str(), true, this);
        
        mosquitto_connect_callback_set(mosq, on_connect);
        mosquitto_disconnect_callback_set(mosq, on_disconnect);
        mosquitto_message_callback_set(mosq, on_message);
    }
    
    ~MQTTBridge() {
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }
    
    bool connect(const std::string& host, int port) {
        int rc = mosquitto_connect(mosq, host.c_str(), port, 60);
        if(rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "Connection failed: " << mosquitto_strerror(rc) << "\n";
            return false;
        }
        
        mosquitto_loop_start(mosq);
        return true;
    }
    
    void publish(const std::string& topic, const std::string& message, int qos = 1) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        if(isConnected) {
            int rc = mosquitto_publish(mosq, nullptr, topic.c_str(), 
                                      message.length(), message.c_str(), qos, false);
            if(rc != MOSQ_ERR_SUCCESS) {
                std::cerr << "Publish failed, queueing message\n";
                messageQueue.push(topic + "|" + message);
            }
        } else {
            std::cout << "Offline - queueing message\n";
            messageQueue.push(topic + "|" + message);
        }
    }
    
    void processQueue() {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        while(!messageQueue.empty()) {
            std::string msg = messageQueue.front();
            size_t pos = msg.find('|');
            std::string topic = msg.substr(0, pos);
            std::string payload = msg.substr(pos + 1);
            
            int rc = mosquitto_publish(mosq, nullptr, topic.c_str(), 
                                      payload.length(), payload.c_str(), 1, false);
            if(rc == MOSQ_ERR_SUCCESS) {
                messageQueue.pop();
                std::cout << "Sent queued message\n";
            } else {
                break; // Stop if send fails
            }
        }
    }
    
    void subscribe(const std::string& topic) {
        mosquitto_subscribe(mosq, nullptr, topic.c_str(), 1);
    }
};

int main() {
    MQTTBridge bridge("pwa-backend-bridge");
    
    if(bridge.connect("localhost", 1883)) {
        bridge.subscribe("pwa/data/#");
        
        // Simulate publishing data that might be offline
        for(int i = 0; i < 5; i++) {
            bridge.publish("pwa/sensor/temp", 
                          "{\"temperature\": " + std::to_string(20 + i) + "}");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    return 0;
}
```

**Compile**: `g++ -o mqtt_pwa mqtt_pwa.cpp -lmosquitto -lpthread`

---

## Rust Implementation

Rust offers excellent WebAssembly support for PWA frontends and robust backend services.

```rust
use rumqttc::{MqttOptions, AsyncClient, QoS, Event, Packet};
use tokio::sync::Mutex;
use std::sync::Arc;
use std::collections::VecDeque;
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct QueuedMessage {
    topic: String,
    payload: String,
    qos: QoS,
    timestamp: i64,
}

struct PWAMqttClient {
    client: AsyncClient,
    message_queue: Arc<Mutex<VecDeque<QueuedMessage>>>,
    is_connected: Arc<Mutex<bool>>,
}

impl PWAMqttClient {
    fn new(client_id: &str, host: &str, port: u16) -> Self {
        let mut mqttoptions = MqttOptions::new(client_id, host, port);
        mqttoptions.set_keep_alive(std::time::Duration::from_secs(30));
        mqttoptions.set_clean_session(false); // Persist session
        
        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
        let message_queue = Arc::new(Mutex::new(VecDeque::new()));
        let is_connected = Arc::new(Mutex::new(false));
        
        let queue_clone = message_queue.clone();
        let connected_clone = is_connected.clone();
        let client_clone = client.clone();
        
        // Event loop handler
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("✓ Connected to MQTT broker");
                        *connected_clone.lock().await = true;
                        
                        // Process queued messages
                        Self::process_queue_static(
                            queue_clone.clone(), 
                            client_clone.clone()
                        ).await;
                    }
                    Ok(Event::Incoming(Packet::Disconnect)) => {
                        println!("✗ Disconnected from broker");
                        *connected_clone.lock().await = false;
                    }
                    Ok(Event::Incoming(Packet::Publish(p))) => {
                        let payload = String::from_utf8_lossy(&p.payload);
                        println!("📨 Received: {} on {}", payload, p.topic);
                    }
                    Err(e) => {
                        eprintln!("Connection error: {:?}", e);
                        *connected_clone.lock().await = false;
                        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
                    }
                    _ => {}
                }
            }
        });
        
        PWAMqttClient {
            client,
            message_queue,
            is_connected,
        }
    }
    
    async fn publish(&self, topic: &str, payload: &str, qos: QoS) {
        let connected = *self.is_connected.lock().await;
        
        if connected {
            match self.client.publish(topic, qos, false, payload).await {
                Ok(_) => println!("✓ Published to {}", topic),
                Err(e) => {
                    eprintln!("Publish failed: {:?}, queueing message", e);
                    self.queue_message(topic, payload, qos).await;
                }
            }
        } else {
            println!("📥 Offline - queueing message");
            self.queue_message(topic, payload, qos).await;
        }
    }
    
    async fn queue_message(&self, topic: &str, payload: &str, qos: QoS) {
        let msg = QueuedMessage {
            topic: topic.to_string(),
            payload: payload.to_string(),
            qos,
            timestamp: chrono::Utc::now().timestamp(),
        };
        
        self.message_queue.lock().await.push_back(msg);
    }
    
    async fn process_queue_static(
        queue: Arc<Mutex<VecDeque<QueuedMessage>>>,
        client: AsyncClient
    ) {
        let mut queue_lock = queue.lock().await;
        let mut failed = VecDeque::new();
        
        while let Some(msg) = queue_lock.pop_front() {
            match client.publish(&msg.topic, msg.qos, false, &msg.payload).await {
                Ok(_) => println!("✓ Sent queued message to {}", msg.topic),
                Err(_) => failed.push_back(msg),
            }
        }
        
        // Re-queue failed messages
        queue_lock.extend(failed);
    }
    
    async fn subscribe(&self, topic: &str, qos: QoS) {
        self.client.subscribe(topic, qos).await.unwrap();
        println!("📬 Subscribed to {}", topic);
    }
}

#[tokio::main]
async fn main() {
    let client = PWAMqttClient::new("pwa-rust-client", "localhost", 1883);
    
    // Wait for connection
    tokio::time::sleep(tokio::time::Duration::from_secs(2)).await;
    
    client.subscribe("pwa/notifications/#", QoS::AtLeastOnce).await;
    
    // Simulate PWA sending data (some while offline)
    for i in 0..5 {
        let data = format!(r#"{{"event": "user_action", "id": {}}}"#, i);
        client.publish("pwa/events", &data, QoS::AtLeastOnce).await;
        tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    }
    
    // Keep alive
    tokio::time::sleep(tokio::time::Duration::from_secs(30)).await;
}
```

**Dependencies** (`Cargo.toml`):
```toml
[dependencies]
rumqttc = "0.24"
tokio = { version = "1", features = ["full"] }
serde = { version = "1", features = ["derive"] }
serde_json = "1"
chrono = "0.4"
```

**Run**: `cargo run`

---

## Summary

**Progressive Web Apps with MQTT** enable robust offline-first applications by:

✅ **Offline Resilience**: Messages are queued locally when connectivity is lost and automatically synchronized when restored

✅ **Real-time Sync**: MQTT provides instant bidirectional communication for live updates across devices

✅ **Quality of Service**: QoS levels ensure reliable message delivery even with unreliable networks

✅ **Cross-Platform**: Web-based PWAs work across desktop, mobile, and tablets while maintaining MQTT connectivity

✅ **Backend Integration**: C/C++ and Rust implementations demonstrate server-side bridges that handle PWA message queuing and processing

**Use Cases**: Chat applications, collaborative tools, IoT dashboards, real-time monitoring systems, field service apps, and any application requiring offline capability with synchronized state.

The combination of Service Workers for offline capability and MQTT for efficient messaging creates powerful applications that work seamlessly regardless of network conditions.