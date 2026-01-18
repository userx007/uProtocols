# MQTT Broker Clustering: High Availability and Horizontal Scaling

## Overview

**Broker clustering** is an advanced MQTT deployment strategy that connects multiple MQTT brokers together to form a unified, distributed system. This approach addresses critical production requirements: high availability (ensuring the system remains operational even when individual brokers fail) and horizontal scaling (distributing client connections and message load across multiple servers to handle growing demand).

## Key Concepts

### High Availability (HA)
- **Redundancy**: Multiple broker instances ensure no single point of failure
- **Failover**: Automatic client reconnection to healthy brokers when one fails
- **Data Replication**: Retained messages and persistent sessions synchronized across cluster nodes
- **Health Monitoring**: Continuous checks to detect and isolate failed nodes

### Horizontal Scaling
- **Load Distribution**: Spreading millions of concurrent connections across multiple brokers
- **Message Routing**: Efficiently forwarding messages between brokers to reach subscribers on different nodes
- **Shared State**: Coordinating subscriptions, sessions, and retained messages across the cluster
- **Geographic Distribution**: Placing brokers closer to clients for reduced latency

### Clustering Approaches

1. **Bridge-based Clustering**: Brokers connected via MQTT bridges, forwarding specific topics
2. **Shared Backend Clustering**: Brokers share a common database/cache (Redis, PostgreSQL)
3. **Native Clustering**: Built-in clustering protocols (Mosquitto clustering plugins, VerneMQ, EMQX)
4. **Load Balancer + Shared State**: Layer 4/7 load balancer distributing connections to stateless brokers

## Architecture Patterns

```
┌─────────────┐
│   Clients   │
└──────┬──────┘
       │
   ┌───▼────┐
   │  LB    │ (Load Balancer)
   └───┬────┘
       │
   ┌───┴────────────────┐
   │                    │
┌──▼──────┐      ┌─────▼─────┐
│ Broker1 │◄────►│  Broker2  │ (Cluster Communication)
└──┬──────┘      └─────┬─────┘
   │                   │
   └────────┬──────────┘
            │
      ┌─────▼──────┐
      │ Shared DB  │ (Redis/PostgreSQL)
      └────────────┘
```

---

## C/C++ Code Example: Client with Automatic Failover

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>

#define QOS 1
#define TIMEOUT 10000L

// Multiple broker addresses for failover
const char* BROKER_URLS[] = {
    "tcp://broker1.example.com:1883",
    "tcp://broker2.example.com:1883",
    "tcp://broker3.example.com:1883"
};
const int NUM_BROKERS = 3;

typedef struct {
    MQTTClient client;
    int current_broker_index;
} ClusterClient;

// Connection lost callback - triggers failover
void connection_lost(void *context, char *cause) {
    ClusterClient *cc = (ClusterClient*)context;
    printf("Connection lost: %s\n", cause ? cause : "unknown");
    printf("Attempting failover to next broker...\n");
    
    // Try next broker in round-robin fashion
    for (int attempts = 0; attempts < NUM_BROKERS; attempts++) {
        cc->current_broker_index = (cc->current_broker_index + 1) % NUM_BROKERS;
        const char* next_broker = BROKER_URLS[cc->current_broker_index];
        
        printf("Trying broker %d: %s\n", cc->current_broker_index, next_broker);
        
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 0; // Persistent session for HA
        conn_opts.reliable = 1;
        
        if (MQTTClient_connect(cc->client, &conn_opts) == MQTTCLIENT_SUCCESS) {
            printf("Successfully connected to %s\n", next_broker);
            return;
        }
    }
    
    printf("ERROR: All brokers unreachable!\n");
}

int message_arrived(void *context, char *topic, int topic_len, 
                    MQTTClient_message *message) {
    printf("Message arrived [%s]: %.*s\n", topic, 
           message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

int main() {
    ClusterClient cc;
    cc.current_broker_index = 0;
    
    // Create client with first broker
    MQTTClient_create(&cc.client, BROKER_URLS[0], "ha_client_001",
                      MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(cc.client, &cc, connection_lost, 
                           message_arrived, NULL);
    
    // Initial connection
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0;
    
    if (MQTTClient_connect(cc.client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect to initial broker\n");
        return EXIT_FAILURE;
    }
    
    printf("Connected to cluster\n");
    
    // Subscribe to topic
    MQTTClient_subscribe(cc.client, "sensor/#", QOS);
    
    // Publish with QoS 1 for guaranteed delivery
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = "Temperature: 22.5C";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(cc.client, "sensor/temp", &pubmsg, NULL);
    
    // Keep running
    printf("Waiting for messages (Ctrl+C to exit)...\n");
    while(1) {
        #ifdef WIN32
            Sleep(1000);
        #else
            sleep(1);
        #endif
    }
    
    MQTTClient_disconnect(cc.client, 10000);
    MQTTClient_destroy(&cc.client);
    return EXIT_SUCCESS;
}
```

### Key Features in C Example:
- **Multiple broker URLs** configured for automatic failover
- **Persistent sessions** (`cleansession = 0`) to maintain subscriptions across reconnections
- **Connection lost callback** that automatically tries alternative brokers
- **Round-robin failover** strategy cycling through available brokers
- **QoS 1 messaging** for guaranteed delivery in HA scenarios

---

## Rust Code Example: Clustered Publisher with Health Checks

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::thread;
use std::sync::{Arc, Mutex};

#[derive(Clone)]
struct BrokerCluster {
    brokers: Vec<String>,
    current_index: Arc<Mutex<usize>>,
}

impl BrokerCluster {
    fn new(brokers: Vec<String>) -> Self {
        BrokerCluster {
            brokers,
            current_index: Arc::new(Mutex::new(0)),
        }
    }
    
    fn get_current_broker(&self) -> (String, u16) {
        let index = *self.current_index.lock().unwrap();
        let broker = &self.brokers[index];
        
        // Parse "host:port" format
        let parts: Vec<&str> = broker.split(':').collect();
        let host = parts[0].to_string();
        let port = parts.get(1).unwrap_or(&"1883").parse().unwrap_or(1883);
        
        (host, port)
    }
    
    fn failover(&self) -> Option<(String, u16)> {
        let mut index = self.current_index.lock().unwrap();
        *index = (*index + 1) % self.brokers.len();
        
        if *index == 0 {
            println!("⚠️  All brokers tried, cycling back to first");
            return None; // Tried all brokers
        }
        
        drop(index); // Release lock before calling get_current_broker
        Some(self.get_current_broker())
    }
}

fn create_mqtt_client(cluster: &BrokerCluster, client_id: &str) -> Client {
    let (host, port) = cluster.get_current_broker();
    println!("🔌 Connecting to broker: {}:{}", host, port);
    
    let mut mqttoptions = MqttOptions::new(client_id, host, port);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_clean_session(false); // Persistent session for HA
    mqttoptions.set_connection_timeout(5);
    
    let (client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn connection event loop
    let cluster_clone = cluster.clone();
    thread::spawn(move || {
        for (i, notification) in connection.iter().enumerate() {
            match notification {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    println!("✅ Connected to cluster node");
                }
                Ok(Event::Incoming(Packet::Disconnect)) => {
                    println!("❌ Disconnected from broker");
                    if let Some((new_host, new_port)) = cluster_clone.failover() {
                        println!("🔄 Attempting failover to {}:{}", new_host, new_port);
                    }
                }
                Err(e) => {
                    println!("❌ Connection error: {:?}", e);
                    thread::sleep(Duration::from_secs(2));
                }
                _ => {}
            }
        }
    });
    
    client
}

fn main() {
    // Define cluster nodes
    let cluster = BrokerCluster::new(vec![
        "broker1.example.com:1883".to_string(),
        "broker2.example.com:1883".to_string(),
        "broker3.example.com:1883".to_string(),
    ]);
    
    let client = create_mqtt_client(&cluster, "rust_cluster_client_001");
    
    // Subscribe to topics
    client.subscribe("cluster/health/#", QoS::AtLeastOnce).unwrap();
    println!("📬 Subscribed to cluster/health/#");
    
    // Publish messages with retry logic
    for i in 0..100 {
        let topic = "cluster/metrics/temperature";
        let payload = format!(r#"{{"sensor_id": "temp_01", "value": {}, "timestamp": {}}}"#, 
                            20.0 + (i as f32 * 0.1), i);
        
        match client.publish(topic, QoS::AtLeastOnce, false, payload.as_bytes()) {
            Ok(_) => println!("📤 Published message {}: {}", i, payload),
            Err(e) => {
                println!("⚠️  Publish failed: {:?}. Retrying...", e);
                thread::sleep(Duration::from_secs(1));
                // In production, implement exponential backoff
            }
        }
        
        thread::sleep(Duration::from_secs(2));
    }
    
    println!("✅ Publishing complete. Press Ctrl+C to exit");
    loop {
        thread::sleep(Duration::from_secs(1));
    }
}
```

### Key Features in Rust Example:
- **Custom `BrokerCluster` struct** managing multiple broker addresses
- **Automatic failover logic** triggered on disconnect events
- **Thread-safe broker index** using `Arc<Mutex<T>>`
- **Persistent sessions** with `clean_session(false)`
- **Connection event monitoring** for health checks
- **Retry logic** for failed publish operations

---

## Production Clustering Solutions

### 1. **EMQX Cluster** (Most Popular)
- Auto-discovery via Kubernetes, etcd, or static nodes
- Shared subscription load balancing
- Built-in metrics and monitoring

```bash
# Docker Compose EMQX Cluster
version: '3'
services:
  emqx1:
    image: emqx/emqx:latest
    environment:
      - EMQX_NAME=emqx
      - EMQX_CLUSTER__DISCOVERY=static
      - EMQX_CLUSTER__STATIC__SEEDS=emqx@emqx1,emqx@emqx2
    ports:
      - "1883:1883"
      - "18083:18083"
  
  emqx2:
    image: emqx/emqx:latest
    environment:
      - EMQX_NAME=emqx
      - EMQX_CLUSTER__DISCOVERY=static
      - EMQX_CLUSTER__STATIC__SEEDS=emqx@emqx1,emqx@emqx2
```

### 2. **VerneMQ Cluster**
- Masterless architecture (no single point of failure)
- Eventual consistency model
- Supports shared subscriptions

### 3. **HiveMQ Cluster**
- Enterprise-grade with clustering extension
- Horizontal scalability to millions of connections
- Advanced persistence layer

---

## Summary

**MQTT Broker Clustering** is essential for production IoT deployments requiring:

✅ **High Availability**: Automatic failover ensures continuous operation despite broker failures  
✅ **Horizontal Scaling**: Distributes load across multiple servers to handle massive client counts  
✅ **Geographic Distribution**: Reduced latency by placing brokers near clients  
✅ **Transparent Failover**: Clients automatically reconnect using persistent sessions and multiple broker URLs  

**Implementation Strategies**:
- Client-side: Configure multiple broker addresses with automatic failover logic
- Server-side: Use native clustering (EMQX, VerneMQ) or shared backend (Redis/PostgreSQL)
- Network-side: Deploy load balancers (HAProxy, Nginx) with health checks

**Critical Design Considerations**:
- Use **persistent sessions** (`clean_session=false`) to maintain state during failover
- Implement **QoS 1/2** for guaranteed message delivery in HA scenarios
- Monitor **cluster health** and node synchronization status
- Plan for **split-brain scenarios** with proper quorum/consensus mechanisms
- Test **failover performance** under realistic load conditions

Clustering transforms MQTT from a single-broker system into a resilient, scalable platform capable of supporting mission-critical IoT applications with millions of devices.