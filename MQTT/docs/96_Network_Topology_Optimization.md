# Network Topology Optimization for MQTT

## Overview

Network topology optimization in MQTT refers to the strategic design and arrangement of MQTT brokers, clients, and their interconnections to achieve optimal performance, reliability, and scalability. This involves decisions about broker placement, clustering strategies, bridging configurations, and client distribution patterns.

## Key Topology Patterns

### 1. **Centralized Topology**
A single broker serves all clients. Simple but creates a single point of failure.

### 2. **Hierarchical Topology**
Multiple brokers arranged in levels, with edge brokers forwarding selected topics to central brokers.

### 3. **Bridged Topology**
Multiple brokers connected via MQTT bridges, allowing topic sharing between broker instances.

### 4. **Clustered Topology**
Multiple broker instances working together as a single logical broker for high availability and load distribution.

## C/C++ Implementation Examples

### Example 1: Edge Broker with Selective Topic Forwarding

```c
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    struct mosquitto *edge_broker;
    struct mosquitto *central_connection;
    char *central_host;
    int central_port;
} topology_manager_t;

// Callback when message received on edge broker
void on_edge_message(struct mosquitto *mosq, void *obj, 
                      const struct mosquitto_message *msg) {
    topology_manager_t *manager = (topology_manager_t *)obj;
    
    // Forward only critical topics to central broker
    if (strncmp(msg->topic, "sensors/critical/", 17) == 0 ||
        strncmp(msg->topic, "alerts/", 7) == 0) {
        
        printf("Forwarding to central: %s\n", msg->topic);
        mosquitto_publish(manager->central_connection, NULL,
                         msg->topic, msg->payloadlen, msg->payload,
                         msg->qos, msg->retain);
    } else {
        printf("Local only: %s\n", msg->topic);
    }
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if (rc == 0) {
        printf("Connected successfully\n");
        // Subscribe to all local topics
        mosquitto_subscribe(mosq, NULL, "#", 0);
    }
}

int main(int argc, char *argv[]) {
    mosquitto_lib_init();
    
    topology_manager_t manager;
    manager.central_host = "central-broker.example.com";
    manager.central_port = 1883;
    
    // Edge broker connection (acts as client to edge broker)
    manager.edge_broker = mosquitto_new("edge_forwarder", true, &manager);
    mosquitto_connect_callback_set(manager.edge_broker, on_connect);
    mosquitto_message_callback_set(manager.edge_broker, on_edge_message);
    mosquitto_connect(manager.edge_broker, "localhost", 1883, 60);
    
    // Central broker connection
    manager.central_connection = mosquitto_new("to_central", true, NULL);
    mosquitto_connect(manager.central_connection, 
                     manager.central_host, manager.central_port, 60);
    
    // Run both connections
    mosquitto_loop_start(manager.edge_broker);
    mosquitto_loop_start(manager.central_connection);
    
    // Keep running
    while(1) {
        sleep(1);
    }
    
    mosquitto_destroy(manager.edge_broker);
    mosquitto_destroy(manager.central_connection);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

### Example 2: Load Balancing Client Connector

```cpp
#include <mosquitto.h>
#include <vector>
#include <string>
#include <random>
#include <iostream>

class LoadBalancedMQTTClient {
private:
    struct BrokerInfo {
        std::string host;
        int port;
        int priority;
        int current_load;
    };
    
    std::vector<BrokerInfo> brokers;
    struct mosquitto *mosq;
    
public:
    LoadBalancedMQTTClient(const std::string &client_id) {
        mosquitto_lib_init();
        mosq = mosquitto_new(client_id.c_str(), true, this);
    }
    
    void add_broker(const std::string &host, int port, int priority = 1) {
        brokers.push_back({host, port, priority, 0});
    }
    
    BrokerInfo select_best_broker() {
        // Weighted random selection based on priority
        int total_weight = 0;
        for (const auto &broker : brokers) {
            total_weight += broker.priority;
        }
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, total_weight - 1);
        
        int random_value = dis(gen);
        int cumulative = 0;
        
        for (const auto &broker : brokers) {
            cumulative += broker.priority;
            if (random_value < cumulative) {
                return broker;
            }
        }
        
        return brokers[0]; // Fallback
    }
    
    bool connect_with_fallback() {
        // Try connecting to brokers in order of priority
        std::vector<BrokerInfo> sorted_brokers = brokers;
        std::sort(sorted_brokers.begin(), sorted_brokers.end(),
                  [](const BrokerInfo &a, const BrokerInfo &b) {
                      return a.priority > b.priority;
                  });
        
        for (auto &broker : sorted_brokers) {
            std::cout << "Attempting connection to " << broker.host 
                      << ":" << broker.port << std::endl;
            
            int rc = mosquitto_connect(mosq, broker.host.c_str(), 
                                      broker.port, 60);
            if (rc == MOSQ_ERR_SUCCESS) {
                std::cout << "Connected to " << broker.host << std::endl;
                return true;
            }
        }
        
        std::cerr << "Failed to connect to any broker" << std::endl;
        return false;
    }
    
    void publish(const std::string &topic, const std::string &payload) {
        mosquitto_publish(mosq, NULL, topic.c_str(), 
                         payload.length(), payload.c_str(), 0, false);
    }
    
    ~LoadBalancedMQTTClient() {
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }
};

int main() {
    LoadBalancedMQTTClient client("load_balanced_client");
    
    // Add multiple brokers with priorities
    client.add_broker("broker1.example.com", 1883, 3);  // Higher priority
    client.add_broker("broker2.example.com", 1883, 2);
    client.add_broker("broker3.example.com", 1883, 1);  // Backup
    
    if (client.connect_with_fallback()) {
        client.publish("test/topic", "Hello from optimized topology");
    }
    
    return 0;
}
```

## Rust Implementation Examples

### Example 1: Hierarchical Topic Router

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::task;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Clone)]
struct TopologyRouter {
    edge_client: AsyncClient,
    central_client: AsyncClient,
    routing_rules: Arc<Mutex<HashMap<String, bool>>>, // topic prefix -> forward to central
}

impl TopologyRouter {
    async fn new(
        edge_broker: &str,
        central_broker: &str,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        // Edge broker connection
        let mut edge_options = MqttOptions::new("edge_router", edge_broker, 1883);
        edge_options.set_keep_alive(std::time::Duration::from_secs(30));
        let (edge_client, mut edge_eventloop) = AsyncClient::new(edge_options, 10);
        
        // Central broker connection
        let mut central_options = MqttOptions::new("central_uplink", central_broker, 1883);
        central_options.set_keep_alive(std::time::Duration::from_secs(30));
        let (central_client, mut central_eventloop) = AsyncClient::new(central_options, 10);
        
        let routing_rules = Arc::new(Mutex::new(HashMap::new()));
        
        // Set up routing rules
        {
            let mut rules = routing_rules.lock().await;
            rules.insert("sensors/critical/".to_string(), true);
            rules.insert("alerts/".to_string(), true);
            rules.insert("commands/".to_string(), true);
            rules.insert("sensors/local/".to_string(), false); // Stay local
        }
        
        let router = TopologyRouter {
            edge_client: edge_client.clone(),
            central_client: central_client.clone(),
            routing_rules: routing_rules.clone(),
        };
        
        // Subscribe to all topics on edge
        edge_client.subscribe("#", QoS::AtLeastOnce).await?;
        
        // Spawn edge event handler
        let router_clone = router.clone();
        task::spawn(async move {
            loop {
                match edge_eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(publish))) => {
                        router_clone.handle_message(&publish.topic, &publish.payload).await;
                    }
                    Ok(_) => {}
                    Err(e) => {
                        eprintln!("Edge connection error: {:?}", e);
                        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
                    }
                }
            }
        });
        
        // Spawn central event handler
        task::spawn(async move {
            loop {
                match central_eventloop.poll().await {
                    Ok(_) => {}
                    Err(e) => {
                        eprintln!("Central connection error: {:?}", e);
                        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
                    }
                }
            }
        });
        
        Ok(router)
    }
    
    async fn handle_message(&self, topic: &str, payload: &[u8]) {
        println!("Received on edge: {}", topic);
        
        let rules = self.routing_rules.lock().await;
        let should_forward = rules.iter().any(|(prefix, forward)| {
            topic.starts_with(prefix) && *forward
        });
        
        if should_forward {
            println!("Forwarding to central: {}", topic);
            if let Err(e) = self.central_client.publish(
                topic,
                QoS::AtLeastOnce,
                false,
                payload,
            ).await {
                eprintln!("Failed to forward message: {:?}", e);
            }
        } else {
            println!("Keeping local: {}", topic);
        }
    }
    
    async fn add_routing_rule(&self, prefix: String, forward: bool) {
        let mut rules = self.routing_rules.lock().await;
        rules.insert(prefix, forward);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let router = TopologyRouter::new(
        "localhost",
        "central.example.com",
    ).await?;
    
    println!("Topology router running...");
    
    // Keep running
    tokio::signal::ctrl_c().await?;
    println!("Shutting down...");
    
    Ok(())
}
```

### Example 2: Multi-Broker Client with Automatic Failover

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet, ConnectReturnCode};
use tokio::time::{sleep, Duration};
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Clone, Debug)]
struct BrokerConfig {
    host: String,
    port: u16,
    priority: u8,
}

struct ResilientMQTTClient {
    brokers: Vec<BrokerConfig>,
    current_broker: Arc<RwLock<Option<usize>>>,
    client: Arc<RwLock<Option<AsyncClient>>>,
}

impl ResilientMQTTClient {
    fn new(brokers: Vec<BrokerConfig>) -> Self {
        ResilientMQTTClient {
            brokers,
            current_broker: Arc::new(RwLock::new(None)),
            client: Arc::new(RwLock::new(None)),
        }
    }
    
    async fn connect(&self) -> Result<(), Box<dyn std::error::Error>> {
        // Sort brokers by priority (highest first)
        let mut sorted_brokers = self.brokers.clone();
        sorted_brokers.sort_by(|a, b| b.priority.cmp(&a.priority));
        
        for (idx, broker) in sorted_brokers.iter().enumerate() {
            println!("Attempting connection to {}:{}", broker.host, broker.port);
            
            let mut mqttoptions = MqttOptions::new(
                "resilient_client",
                &broker.host,
                broker.port,
            );
            mqttoptions.set_keep_alive(Duration::from_secs(30));
            
            let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
            
            // Try to connect
            match tokio::time::timeout(Duration::from_secs(5), eventloop.poll()).await {
                Ok(Ok(Event::Incoming(Packet::ConnAck(ack)))) => {
                    if ack.code == ConnectReturnCode::Success {
                        println!("Connected to {}", broker.host);
                        *self.current_broker.write().await = Some(idx);
                        *self.client.write().await = Some(client.clone());
                        
                        // Spawn event loop
                        self.spawn_event_loop(eventloop).await;
                        return Ok(());
                    }
                }
                _ => {
                    println!("Failed to connect to {}", broker.host);
                    continue;
                }
            }
        }
        
        Err("Failed to connect to any broker".into())
    }
    
    async fn spawn_event_loop(&self, mut eventloop: rumqttc::EventLoop) {
        let client_arc = self.client.clone();
        let current_broker_arc = self.current_broker.clone();
        
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(event) => {
                        if let Event::Incoming(Packet::Publish(p)) = event {
                            println!("Received: {} - {:?}", p.topic, p.payload);
                        }
                    }
                    Err(e) => {
                        eprintln!("Connection lost: {:?}", e);
                        *client_arc.write().await = None;
                        *current_broker_arc.write().await = None;
                        break;
                    }
                }
            }
        });
    }
    
    async fn publish(
        &self,
        topic: &str,
        payload: Vec<u8>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let client_guard = self.client.read().await;
        if let Some(client) = client_guard.as_ref() {
            client.publish(topic, QoS::AtLeastOnce, false, payload).await?;
            Ok(())
        } else {
            Err("Not connected to any broker".into())
        }
    }
    
    async fn reconnect_if_needed(&self) -> Result<(), Box<dyn std::error::Error>> {
        let is_connected = self.client.read().await.is_some();
        if !is_connected {
            println!("Reconnecting...");
            self.connect().await?;
        }
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let brokers = vec![
        BrokerConfig {
            host: "primary.example.com".to_string(),
            port: 1883,
            priority: 10,
        },
        BrokerConfig {
            host: "secondary.example.com".to_string(),
            port: 1883,
            priority: 5,
        },
        BrokerConfig {
            host: "backup.example.com".to_string(),
            port: 1883,
            priority: 1,
        },
    ];
    
    let client = ResilientMQTTClient::new(brokers);
    client.connect().await?;
    
    // Monitoring and auto-reconnect loop
    tokio::spawn(async move {
        loop {
            sleep(Duration::from_secs(10)).await;
            if let Err(e) = client.reconnect_if_needed().await {
                eprintln!("Reconnection failed: {:?}", e);
            }
        }
    });
    
    tokio::signal::ctrl_c().await?;
    Ok(())
}
```

## Summary

Network topology optimization for MQTT involves strategic architectural decisions that balance performance, reliability, scalability, and cost. Key considerations include:

**Design Patterns:**
- **Centralized**: Simple but limited scalability
- **Hierarchical**: Efficient for edge computing scenarios with local processing
- **Bridged**: Enables geographic distribution and isolation
- **Clustered**: Provides high availability and horizontal scaling

**Implementation Strategies:**
- Selective topic forwarding to reduce bandwidth and central broker load
- Load balancing across multiple brokers for better resource utilization
- Automatic failover mechanisms for resilience
- Geographic distribution for latency optimization

**Best Practices:**
- Keep local data local when possible to reduce network traffic
- Forward only critical or aggregated data to central systems
- Implement connection pooling and retry logic
- Use QoS levels appropriately for topology requirements
- Monitor broker health and implement automatic failover
- Consider network latency and bandwidth constraints in topology design

The code examples demonstrate practical implementations of hierarchical routing, load balancing, and resilient connection management in both C/C++ and Rust, providing patterns that can be adapted to various MQTT deployment scenarios.