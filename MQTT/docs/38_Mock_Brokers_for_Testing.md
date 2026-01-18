# Mock Brokers for Testing in MQTT

## Introduction

Mock MQTT brokers are essential tools for testing MQTT clients and applications without requiring a full broker infrastructure. They enable developers to create controlled test environments, simulate various broker behaviors, verify message flows, and conduct integration testing in isolation. This approach improves test reliability, speed, and reproducibility while reducing dependencies on external services.

## Why Use Mock Brokers?

**Benefits of mock brokers include:**

- **Isolation**: Tests run independently without external dependencies
- **Speed**: In-memory brokers execute faster than networked alternatives
- **Control**: Simulate edge cases, failures, and specific behaviors
- **Reproducibility**: Consistent test results across environments
- **Cost**: No need for dedicated testing infrastructure
- **Offline Development**: Work without internet connectivity

## Testing Strategies

### 1. Embedded Mock Brokers
Lightweight brokers that run within your test process, ideal for unit and integration tests.

### 2. Containerized Brokers
Docker containers running real brokers like Mosquitto, providing realistic environments while maintaining isolation.

### 3. Custom Mock Implementations
Hand-crafted mocks that simulate specific broker behaviors for targeted testing.

## C/C++ Implementation Examples

### Basic Mock Broker Using libmosquitto Test Framework

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

// Mock broker context
typedef struct {
    char *last_published_topic;
    char *last_published_payload;
    int publish_count;
    int connect_count;
    bool is_connected;
} MockBrokerContext;

// Initialize mock context
MockBrokerContext* mock_broker_init() {
    MockBrokerContext *ctx = calloc(1, sizeof(MockBrokerContext));
    ctx->last_published_topic = NULL;
    ctx->last_published_payload = NULL;
    ctx->publish_count = 0;
    ctx->connect_count = 0;
    ctx->is_connected = false;
    return ctx;
}

// Cleanup mock context
void mock_broker_cleanup(MockBrokerContext *ctx) {
    if (ctx) {
        free(ctx->last_published_topic);
        free(ctx->last_published_payload);
        free(ctx);
    }
}

// Mock callbacks
void mock_on_connect(struct mosquitto *mosq, void *obj, int rc) {
    MockBrokerContext *ctx = (MockBrokerContext *)obj;
    ctx->connect_count++;
    ctx->is_connected = (rc == 0);
    printf("[MOCK] Connection event: rc=%d\n", rc);
}

void mock_on_publish(struct mosquitto *mosq, void *obj, int mid) {
    MockBrokerContext *ctx = (MockBrokerContext *)obj;
    ctx->publish_count++;
    printf("[MOCK] Publish complete: mid=%d\n", mid);
}

void mock_on_message(struct mosquitto *mosq, void *obj, 
                     const struct mosquitto_message *msg) {
    MockBrokerContext *ctx = (MockBrokerContext *)obj;
    
    // Store last message for verification
    free(ctx->last_published_topic);
    free(ctx->last_published_payload);
    
    ctx->last_published_topic = strdup(msg->topic);
    ctx->last_published_payload = malloc(msg->payloadlen + 1);
    memcpy(ctx->last_published_payload, msg->payload, msg->payloadlen);
    ctx->last_published_payload[msg->payloadlen] = '\0';
    
    printf("[MOCK] Message received: %s = %s\n", 
           msg->topic, (char *)msg->payload);
}

// Test example: Publish and verify
void test_publish_receive() {
    printf("\n=== Test: Publish and Receive ===\n");
    
    struct mosquitto *mosq_pub = NULL;
    struct mosquitto *mosq_sub = NULL;
    MockBrokerContext *ctx_pub = mock_broker_init();
    MockBrokerContext *ctx_sub = mock_broker_init();
    
    mosquitto_lib_init();
    
    // Create publisher
    mosq_pub = mosquitto_new("test_publisher", true, ctx_pub);
    mosquitto_connect_callback_set(mosq_pub, mock_on_connect);
    mosquitto_publish_callback_set(mosq_pub, mock_on_publish);
    
    // Create subscriber
    mosq_sub = mosquitto_new("test_subscriber", true, ctx_sub);
    mosquitto_connect_callback_set(mosq_sub, mock_on_connect);
    mosquitto_message_callback_set(mosq_sub, mock_on_message);
    
    // Connect to local test broker (e.g., mosquitto -p 1883)
    mosquitto_connect(mosq_pub, "localhost", 1883, 60);
    mosquitto_connect(mosq_sub, "localhost", 1883, 60);
    
    // Start network loops
    mosquitto_loop_start(mosq_pub);
    mosquitto_loop_start(mosq_sub);
    
    // Wait for connections
    sleep(1);
    
    // Subscribe to test topic
    mosquitto_subscribe(mosq_sub, NULL, "test/mock", 0);
    sleep(1);
    
    // Publish test message
    const char *payload = "Hello Mock Broker";
    mosquitto_publish(mosq_pub, NULL, "test/mock", 
                     strlen(payload), payload, 0, false);
    
    // Wait for message delivery
    sleep(2);
    
    // Verify results
    assert(ctx_pub->is_connected);
    assert(ctx_sub->is_connected);
    assert(ctx_pub->publish_count > 0);
    assert(ctx_sub->last_published_topic != NULL);
    assert(strcmp(ctx_sub->last_published_topic, "test/mock") == 0);
    assert(strcmp(ctx_sub->last_published_payload, payload) == 0);
    
    printf("[TEST] All assertions passed!\n");
    
    // Cleanup
    mosquitto_loop_stop(mosq_pub, false);
    mosquitto_loop_stop(mosq_sub, false);
    mosquitto_destroy(mosq_pub);
    mosquitto_destroy(mosq_sub);
    mock_broker_cleanup(ctx_pub);
    mock_broker_cleanup(ctx_sub);
    mosquitto_lib_cleanup();
}

int main() {
    test_publish_receive();
    return 0;
}
```

### Simple In-Memory Mock Broker (C++)

```cpp
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>

// Simple in-memory MQTT mock broker
class MockMQTTBroker {
private:
    struct Subscription {
        std::string client_id;
        std::function<void(const std::string&, const std::string&)> callback;
    };
    
    std::map<std::string, std::vector<Subscription>> subscriptions;
    std::vector<std::pair<std::string, std::string>> message_log;
    
public:
    // Simulate client subscription
    void subscribe(const std::string& client_id, 
                   const std::string& topic,
                   std::function<void(const std::string&, const std::string&)> callback) {
        subscriptions[topic].push_back({client_id, callback});
        std::cout << "[MOCK BROKER] " << client_id 
                  << " subscribed to " << topic << std::endl;
    }
    
    // Simulate message publishing
    void publish(const std::string& topic, const std::string& payload) {
        message_log.push_back({topic, payload});
        std::cout << "[MOCK BROKER] Publishing to " << topic 
                  << ": " << payload << std::endl;
        
        // Deliver to subscribers
        auto it = subscriptions.find(topic);
        if (it != subscriptions.end()) {
            for (auto& sub : it->second) {
                sub.callback(topic, payload);
            }
        }
        
        // Handle wildcard subscriptions (simplified)
        for (auto& [sub_topic, subs] : subscriptions) {
            if (matches_wildcard(sub_topic, topic)) {
                for (auto& sub : subs) {
                    sub.callback(topic, payload);
                }
            }
        }
    }
    
    // Simple wildcard matching
    bool matches_wildcard(const std::string& pattern, const std::string& topic) {
        if (pattern == "#") return true;
        if (pattern == topic) return true;
        
        // Basic + wildcard support
        size_t pos = pattern.find('+');
        if (pos != std::string::npos) {
            std::string prefix = pattern.substr(0, pos);
            if (topic.find(prefix) == 0) return true;
        }
        
        return false;
    }
    
    // Test utilities
    size_t get_message_count() const { return message_log.size(); }
    
    std::vector<std::string> get_messages_for_topic(const std::string& topic) {
        std::vector<std::string> messages;
        for (const auto& [t, payload] : message_log) {
            if (t == topic) messages.push_back(payload);
        }
        return messages;
    }
    
    void clear() {
        subscriptions.clear();
        message_log.clear();
    }
};

// Test client
class TestMQTTClient {
private:
    std::string client_id;
    MockMQTTBroker& broker;
    std::vector<std::pair<std::string, std::string>> received_messages;
    
public:
    TestMQTTClient(const std::string& id, MockMQTTBroker& b) 
        : client_id(id), broker(b) {}
    
    void subscribe(const std::string& topic) {
        broker.subscribe(client_id, topic, 
            [this](const std::string& t, const std::string& p) {
                received_messages.push_back({t, p});
                std::cout << "[CLIENT " << client_id << "] Received: " 
                          << t << " = " << p << std::endl;
            });
    }
    
    void publish(const std::string& topic, const std::string& payload) {
        broker.publish(topic, payload);
    }
    
    const auto& get_received_messages() const { return received_messages; }
    
    void clear_received() { received_messages.clear(); }
};

// Test suite
void run_tests() {
    std::cout << "\n=== Running Mock Broker Tests ===\n\n";
    
    MockMQTTBroker broker;
    
    // Test 1: Basic publish/subscribe
    {
        std::cout << "Test 1: Basic Pub/Sub\n";
        TestMQTTClient client1("client1", broker);
        client1.subscribe("sensors/temperature");
        client1.publish("sensors/temperature", "23.5");
        
        assert(client1.get_received_messages().size() == 1);
        assert(client1.get_received_messages()[0].second == "23.5");
        std::cout << "✓ Test 1 passed\n\n";
        broker.clear();
    }
    
    // Test 2: Multiple subscribers
    {
        std::cout << "Test 2: Multiple Subscribers\n";
        TestMQTTClient client1("client1", broker);
        TestMQTTClient client2("client2", broker);
        
        client1.subscribe("devices/status");
        client2.subscribe("devices/status");
        
        broker.publish("devices/status", "online");
        
        assert(client1.get_received_messages().size() == 1);
        assert(client2.get_received_messages().size() == 1);
        std::cout << "✓ Test 2 passed\n\n";
        broker.clear();
    }
    
    // Test 3: Message filtering
    {
        std::cout << "Test 3: Message Filtering\n";
        TestMQTTClient client1("client1", broker);
        client1.subscribe("home/kitchen/temp");
        
        broker.publish("home/kitchen/temp", "22.0");
        broker.publish("home/bedroom/temp", "20.0");
        
        assert(client1.get_received_messages().size() == 1);
        assert(client1.get_received_messages()[0].first == "home/kitchen/temp");
        std::cout << "✓ Test 3 passed\n\n";
    }
    
    std::cout << "All tests passed!\n";
}

int main() {
    run_tests();
    return 0;
}
```

## Rust Implementation Examples

### Mock Broker Using rumqttc Test Framework

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use std::thread;

// Mock broker context for testing
#[derive(Debug, Clone)]
struct MockBrokerContext {
    published_messages: Arc<Mutex<Vec<(String, String)>>>,
    received_messages: Arc<Mutex<Vec<(String, String)>>>,
    connection_count: Arc<Mutex<usize>>,
}

impl MockBrokerContext {
    fn new() -> Self {
        Self {
            published_messages: Arc::new(Mutex::new(Vec::new())),
            received_messages: Arc::new(Mutex::new(Vec::new())),
            connection_count: Arc::new(Mutex::new(0)),
        }
    }
    
    fn record_publish(&self, topic: String, payload: String) {
        self.published_messages.lock().unwrap().push((topic, payload));
    }
    
    fn record_receive(&self, topic: String, payload: String) {
        self.received_messages.lock().unwrap().push((topic, payload));
    }
    
    fn increment_connections(&self) {
        *self.connection_count.lock().unwrap() += 1;
    }
    
    fn get_received_count(&self) -> usize {
        self.received_messages.lock().unwrap().len()
    }
    
    fn get_last_message(&self) -> Option<(String, String)> {
        self.received_messages.lock().unwrap().last().cloned()
    }
    
    fn clear(&self) {
        self.published_messages.lock().unwrap().clear();
        self.received_messages.lock().unwrap().clear();
        *self.connection_count.lock().unwrap() = 0;
    }
}

// Test client wrapper
struct TestMqttClient {
    client: Client,
    context: MockBrokerContext,
}

impl TestMqttClient {
    fn new(client_id: &str, broker_addr: &str, port: u16) -> (Self, rumqttc::Connection) {
        let mut mqttoptions = MqttOptions::new(client_id, broker_addr, port);
        mqttoptions.set_keep_alive(Duration::from_secs(5));
        
        let (client, connection) = Client::new(mqttoptions, 10);
        let context = MockBrokerContext::new();
        
        (Self { client, context }, connection)
    }
    
    fn publish(&self, topic: &str, payload: &str) -> Result<(), rumqttc::ClientError> {
        self.context.record_publish(topic.to_string(), payload.to_string());
        self.client.publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())
    }
    
    fn subscribe(&self, topic: &str) -> Result<(), rumqttc::ClientError> {
        self.client.subscribe(topic, QoS::AtLeastOnce)
    }
    
    fn get_context(&self) -> MockBrokerContext {
        self.context.clone()
    }
}

// Test examples
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_basic_publish_subscribe() {
        println!("\n=== Test: Basic Publish/Subscribe ===");
        
        // Create publisher
        let (publisher, mut pub_connection) = TestMqttClient::new(
            "test_publisher",
            "localhost",
            1883
        );
        
        // Create subscriber
        let (subscriber, mut sub_connection) = TestMqttClient::new(
            "test_subscriber",
            "localhost",
            1883
        );
        
        let sub_context = subscriber.get_context();
        
        // Handle subscriber events
        thread::spawn(move || {
            for notification in sub_connection.iter() {
                match notification {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("[SUBSCRIBER] Connected");
                        sub_context.increment_connections();
                    }
                    Ok(Event::Incoming(Packet::Publish(p))) => {
                        let topic = p.topic.clone();
                        let payload = String::from_utf8_lossy(&p.payload).to_string();
                        println!("[SUBSCRIBER] Received: {} = {}", topic, payload);
                        sub_context.record_receive(topic, payload);
                    }
                    Err(e) => {
                        eprintln!("[SUBSCRIBER] Error: {:?}", e);
                        break;
                    }
                    _ => {}
                }
            }
        });
        
        // Handle publisher events
        let pub_context = publisher.get_context();
        thread::spawn(move || {
            for notification in pub_connection.iter() {
                match notification {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("[PUBLISHER] Connected");
                        pub_context.increment_connections();
                    }
                    Err(e) => {
                        eprintln!("[PUBLISHER] Error: {:?}", e);
                        break;
                    }
                    _ => {}
                }
            }
        });
        
        thread::sleep(Duration::from_secs(1));
        
        // Subscribe to topic
        subscriber.subscribe("test/mock").unwrap();
        thread::sleep(Duration::from_millis(500));
        
        // Publish message
        publisher.publish("test/mock", "Hello from Rust!").unwrap();
        thread::sleep(Duration::from_secs(2));
        
        // Verify
        let ctx = subscriber.get_context();
        assert!(ctx.get_received_count() > 0);
        
        if let Some((topic, payload)) = ctx.get_last_message() {
            assert_eq!(topic, "test/mock");
            assert_eq!(payload, "Hello from Rust!");
            println!("[TEST] ✓ All assertions passed!");
        }
    }
}

fn main() {
    println!("Mock Broker Testing Example");
    println!("Run with: cargo test -- --nocapture");
}
```

### Simple In-Memory Mock Broker (Rust)

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

type MessageCallback = Arc<dyn Fn(&str, &[u8]) + Send + Sync>;

#[derive(Clone)]
pub struct MockMqttBroker {
    subscriptions: Arc<Mutex<HashMap<String, Vec<(String, MessageCallback)>>>>,
    message_log: Arc<Mutex<Vec<(String, Vec<u8>)>>>,
}

impl MockMqttBroker {
    pub fn new() -> Self {
        Self {
            subscriptions: Arc::new(Mutex::new(HashMap::new())),
            message_log: Arc::new(Mutex::new(Vec::new())),
        }
    }
    
    pub fn subscribe<F>(&self, client_id: &str, topic: &str, callback: F)
    where
        F: Fn(&str, &[u8]) + Send + Sync + 'static,
    {
        let mut subs = self.subscriptions.lock().unwrap();
        subs.entry(topic.to_string())
            .or_insert_with(Vec::new)
            .push((client_id.to_string(), Arc::new(callback)));
        
        println!("[MOCK BROKER] {} subscribed to {}", client_id, topic);
    }
    
    pub fn publish(&self, topic: &str, payload: &[u8]) {
        let mut log = self.message_log.lock().unwrap();
        log.push((topic.to_string(), payload.to_vec()));
        
        println!("[MOCK BROKER] Publishing to {}: {:?}", 
                 topic, String::from_utf8_lossy(payload));
        
        let subs = self.subscriptions.lock().unwrap();
        
        // Exact topic match
        if let Some(subscribers) = subs.get(topic) {
            for (client_id, callback) in subscribers {
                println!("[MOCK BROKER] Delivering to {}", client_id);
                callback(topic, payload);
            }
        }
        
        // Wildcard matching
        for (pattern, subscribers) in subs.iter() {
            if Self::matches_wildcard(pattern, topic) && pattern != topic {
                for (client_id, callback) in subscribers {
                    println!("[MOCK BROKER] Delivering to {} (wildcard)", client_id);
                    callback(topic, payload);
                }
            }
        }
    }
    
    fn matches_wildcard(pattern: &str, topic: &str) -> bool {
        if pattern == "#" {
            return true;
        }
        
        let pattern_parts: Vec<&str> = pattern.split('/').collect();
        let topic_parts: Vec<&str> = topic.split('/').collect();
        
        if pattern_parts.len() != topic_parts.len() && !pattern.contains('#') {
            return false;
        }
        
        for (i, pattern_part) in pattern_parts.iter().enumerate() {
            if *pattern_part == "#" {
                return true;
            }
            if *pattern_part == "+" {
                continue;
            }
            if i >= topic_parts.len() || *pattern_part != topic_parts[i] {
                return false;
            }
        }
        
        true
    }
    
    pub fn get_message_count(&self) -> usize {
        self.message_log.lock().unwrap().len()
    }
    
    pub fn get_messages_for_topic(&self, topic: &str) -> Vec<Vec<u8>> {
        self.message_log
            .lock()
            .unwrap()
            .iter()
            .filter(|(t, _)| t == topic)
            .map(|(_, p)| p.clone())
            .collect()
    }
    
    pub fn clear(&self) {
        self.subscriptions.lock().unwrap().clear();
        self.message_log.lock().unwrap().clear();
    }
}

// Test client
pub struct TestMqttClient {
    client_id: String,
    broker: MockMqttBroker,
    received_messages: Arc<Mutex<Vec<(String, Vec<u8>)>>>,
}

impl TestMqttClient {
    pub fn new(client_id: &str, broker: MockMqttBroker) -> Self {
        Self {
            client_id: client_id.to_string(),
            broker,
            received_messages: Arc::new(Mutex::new(Vec::new())),
        }
    }
    
    pub fn subscribe(&self, topic: &str) {
        let client_id = self.client_id.clone();
        let messages = Arc::clone(&self.received_messages);
        
        self.broker.subscribe(&self.client_id, topic, move |t, p| {
            messages.lock().unwrap().push((t.to_string(), p.to_vec()));
            println!("[CLIENT {}] Received: {} = {:?}", 
                     client_id, t, String::from_utf8_lossy(p));
        });
    }
    
    pub fn publish(&self, topic: &str, payload: &[u8]) {
        self.broker.publish(topic, payload);
    }
    
    pub fn get_received_count(&self) -> usize {
        self.received_messages.lock().unwrap().len()
    }
    
    pub fn get_last_message(&self) -> Option<(String, Vec<u8>)> {
        self.received_messages.lock().unwrap().last().cloned()
    }
}

// Tests
#[cfg(test)]
mod mock_tests {
    use super::*;
    
    #[test]
    fn test_basic_pubsub() {
        println!("\n=== Test: Basic Pub/Sub ===");
        let broker = MockMqttBroker::new();
        let client = TestMqttClient::new("client1", broker.clone());
        
        client.subscribe("sensors/temp");
        client.publish("sensors/temp", b"23.5");
        
        assert_eq!(client.get_received_count(), 1);
        let (topic, payload) = client.get_last_message().unwrap();
        assert_eq!(topic, "sensors/temp");
        assert_eq!(payload, b"23.5");
        println!("✓ Test passed");
    }
    
    #[test]
    fn test_wildcard_subscription() {
        println!("\n=== Test: Wildcard Subscription ===");
        let broker = MockMqttBroker::new();
        let client = TestMqttClient::new("client1", broker.clone());
        
        client.subscribe("sensors/+/temp");
        client.publish("sensors/room1/temp", b"22.0");
        client.publish("sensors/room2/temp", b"23.0");
        client.publish("sensors/room1/humidity", b"45");
        
        assert_eq!(client.get_received_count(), 2);
        println!("✓ Test passed");
    }
}

fn main() {
    println!("Run tests with: cargo test -- --nocapture");
}
```

## Summary

Mock MQTT brokers are indispensable for developing reliable MQTT applications. They provide isolated, controllable test environments that enable rapid iteration and comprehensive testing without external dependencies. The examples demonstrate three key approaches: using test frameworks with real brokers for integration testing, creating simple in-memory mocks for unit tests, and building custom mocks for specific behaviors.

**Key takeaways:**
- Mock brokers improve test speed, reliability, and reproducibility
- In-memory implementations work well for unit tests and quick validation
- Real brokers in containers provide realistic integration testing
- Custom mocks enable testing of edge cases and failure scenarios
- Both C/C++ and Rust ecosystems support various mocking strategies
- Proper test isolation ensures consistent results across environments

By incorporating mock brokers into your testing strategy, you can catch issues early, verify complex message flows, and build more robust MQTT-based systems.