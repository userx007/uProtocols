# VerneMQ Clustering: Distributed MQTT Broker with High Availability

## Overview

VerneMQ is a high-performance, distributed MQTT message broker designed for enterprise-scale deployments. When configured in a cluster, VerneMQ provides horizontal scalability, fault tolerance, and high availability for MQTT messaging systems. The clustering feature allows multiple VerneMQ nodes to work together as a single logical broker, distributing the load and providing redundancy.

## Core Concepts

**Clustering Architecture**: VerneMQ uses Erlang's distributed computing capabilities to create a cluster of broker nodes. Each node can accept client connections, and the cluster automatically synchronizes state information across all nodes, including subscriptions, retained messages, and session data.

**High Availability Features**:
- Automatic failover when a node becomes unavailable
- Load distribution across multiple nodes
- Shared subscription state across the cluster
- Persistent sessions that survive node failures
- Queue migration for disconnected clients

**Key Clustering Mechanisms**:
- Node discovery (manual or automatic)
- Subscription synchronization via CRDT (Conflict-free Replicated Data Types)
- Message routing between nodes
- Distributed session storage

## C/C++ Implementation

Here's a practical example using the Eclipse Paho MQTT C library to connect to a VerneMQ cluster:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define QOS 1
#define TIMEOUT 10000L

// VerneMQ cluster nodes
const char* CLUSTER_NODES[] = {
    "tcp://vernemq-node1.example.com:1883",
    "tcp://vernemq-node2.example.com:1883",
    "tcp://vernemq-node3.example.com:1883"
};
const int NUM_NODES = 3;

typedef struct {
    MQTTClient client;
    int current_node_index;
    volatile int connection_lost;
} ClusterClient;

void connectionLost(void *context, char *cause) {
    ClusterClient *cluster_client = (ClusterClient*)context;
    printf("Connection lost: %s\n", cause ? cause : "unknown");
    cluster_client->connection_lost = 1;
}

int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int connectToCluster(ClusterClient *cluster_client, const char *client_id) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    // Try connecting to each node in the cluster
    for (int attempt = 0; attempt < NUM_NODES * 2; attempt++) {
        int node_idx = cluster_client->current_node_index % NUM_NODES;
        const char* address = CLUSTER_NODES[node_idx];
        
        printf("Attempting to connect to node: %s\n", address);
        
        // Create client if not exists
        if (cluster_client->client == NULL) {
            rc = MQTTClient_create(&cluster_client->client, address, 
                                   client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
            if (rc != MQTTCLIENT_SUCCESS) {
                printf("Failed to create MQTT client: %d\n", rc);
                cluster_client->current_node_index++;
                continue;
            }
        }
        
        // Set callbacks
        MQTTClient_setCallbacks(cluster_client->client, cluster_client,
                                connectionLost, messageArrived, NULL);
        
        // Configure connection options
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 0;  // Persistent session
        conn_opts.reliable = 1;
        conn_opts.connectTimeout = 5;
        conn_opts.retryInterval = 1;
        
        // Attempt connection
        rc = MQTTClient_connect(cluster_client->client, &conn_opts);
        if (rc == MQTTCLIENT_SUCCESS) {
            printf("Successfully connected to cluster via node: %s\n", address);
            cluster_client->connection_lost = 0;
            return MQTTCLIENT_SUCCESS;
        }
        
        printf("Connection failed with code %d, trying next node...\n", rc);
        MQTTClient_destroy(&cluster_client->client);
        cluster_client->client = NULL;
        cluster_client->current_node_index++;
    }
    
    return MQTTCLIENT_FAILURE;
}

int publishWithRetry(ClusterClient *cluster_client, const char *topic, 
                     const char *payload) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    rc = MQTTClient_publishMessage(cluster_client->client, topic, 
                                   &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Publish failed, attempting reconnection...\n");
        if (connectToCluster(cluster_client, "resilient_client") == MQTTCLIENT_SUCCESS) {
            rc = MQTTClient_publishMessage(cluster_client->client, topic, 
                                          &pubmsg, &token);
        }
    }
    
    if (rc == MQTTCLIENT_SUCCESS) {
        rc = MQTTClient_waitForCompletion(cluster_client->client, token, TIMEOUT);
        printf("Message published successfully\n");
    }
    
    return rc;
}

int main(int argc, char* argv[]) {
    ClusterClient cluster_client = {0};
    int rc;
    
    // Connect to cluster
    rc = connectToCluster(&cluster_client, "ha_client_001");
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect to any cluster node\n");
        return EXIT_FAILURE;
    }
    
    // Subscribe to topic
    rc = MQTTClient_subscribe(cluster_client.client, "sensors/+/temperature", QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe: %d\n", rc);
    }
    
    // Publish messages with automatic failover
    for (int i = 0; i < 10; i++) {
        char payload[100];
        snprintf(payload, sizeof(payload), "Temperature reading #%d: %.2f°C", 
                 i, 20.0 + (rand() % 100) / 10.0);
        
        publishWithRetry(&cluster_client, "sensors/room1/temperature", payload);
        
        #ifdef _WIN32
            Sleep(2000);
        #else
            sleep(2);
        #endif
    }
    
    // Cleanup
    MQTTClient_disconnect(cluster_client.client, 10000);
    MQTTClient_destroy(&cluster_client.client);
    
    return EXIT_SUCCESS;
}
```

## Rust Implementation

Here's a Rust implementation using the `rumqttc` library with cluster support:

```rust
use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, Packet, QoS};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;
use tokio::time::sleep;

#[derive(Clone)]
struct VerneMQCluster {
    nodes: Vec<String>,
    current_node: Arc<RwLock<usize>>,
}

impl VerneMQCluster {
    fn new(nodes: Vec<String>) -> Self {
        Self {
            nodes,
            current_node: Arc::new(RwLock::new(0)),
        }
    }

    async fn get_next_node(&self) -> String {
        let mut current = self.current_node.write().await;
        let node = self.nodes[*current % self.nodes.len()].clone();
        *current += 1;
        node
    }

    async fn create_client(&self, client_id: &str) -> Result<(AsyncClient, rumqttc::EventLoop), Box<dyn std::error::Error>> {
        let max_attempts = self.nodes.len() * 2;
        
        for attempt in 0..max_attempts {
            let node = self.get_next_node().await;
            let parts: Vec<&str> = node.split(':').collect();
            let host = parts[0];
            let port: u16 = parts.get(1).unwrap_or(&"1883").parse().unwrap_or(1883);
            
            println!("Attempt {} - Connecting to node: {}:{}", attempt + 1, host, port);
            
            let mut mqttoptions = MqttOptions::new(client_id, host, port);
            mqttoptions.set_keep_alive(Duration::from_secs(20));
            mqttoptions.set_clean_session(false); // Persistent session
            mqttoptions.set_connection_timeout(5);
            
            let (client, eventloop) = AsyncClient::new(mqttoptions, 10);
            
            // Test connection by trying to subscribe
            match tokio::time::timeout(
                Duration::from_secs(5),
                Self::test_connection(&client)
            ).await {
                Ok(Ok(_)) => {
                    println!("Successfully connected to cluster via node: {}:{}", host, port);
                    return Ok((client, eventloop));
                }
                Ok(Err(e)) => {
                    println!("Connection test failed: {:?}", e);
                }
                Err(_) => {
                    println!("Connection timeout");
                }
            }
            
            sleep(Duration::from_millis(500)).await;
        }
        
        Err("Failed to connect to any cluster node".into())
    }

    async fn test_connection(client: &AsyncClient) -> Result<(), Box<dyn std::error::Error>> {
        client.subscribe("$SYS/broker/version", QoS::AtMostOnce).await?;
        Ok(())
    }
}

struct ResilientMqttClient {
    cluster: VerneMQCluster,
    client: Arc<RwLock<Option<AsyncClient>>>,
    client_id: String,
}

impl ResilientMqttClient {
    async fn new(cluster: VerneMQCluster, client_id: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let (client, mut eventloop) = cluster.create_client(client_id).await?;
        
        let resilient_client = Self {
            cluster,
            client: Arc::new(RwLock::new(Some(client))),
            client_id: client_id.to_string(),
        };
        
        // Spawn task to handle events and reconnections
        let client_clone = resilient_client.client.clone();
        let cluster_clone = resilient_client.cluster.clone();
        let client_id_clone = resilient_client.client_id.clone();
        
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(event) => {
                        if let Event::Incoming(Packet::Publish(publish)) = event {
                            println!(
                                "Received: Topic={}, Payload={:?}",
                                publish.topic,
                                String::from_utf8_lossy(&publish.payload)
                            );
                        }
                    }
                    Err(e) => {
                        println!("EventLoop error: {:?}", e);
                        println!("Attempting to reconnect...");
                        
                        // Attempt reconnection
                        match cluster_clone.create_client(&client_id_clone).await {
                            Ok((new_client, new_eventloop)) => {
                                let mut client_lock = client_clone.write().await;
                                *client_lock = Some(new_client);
                                eventloop = new_eventloop;
                                println!("Reconnected successfully!");
                            }
                            Err(e) => {
                                println!("Reconnection failed: {:?}", e);
                                sleep(Duration::from_secs(5)).await;
                            }
                        }
                    }
                }
            }
        });
        
        Ok(resilient_client)
    }

    async fn publish(&self, topic: &str, payload: &str, qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        let client_lock = self.client.read().await;
        
        if let Some(client) = client_lock.as_ref() {
            client.publish(topic, qos, false, payload.as_bytes()).await?;
            Ok(())
        } else {
            Err("Client not connected".into())
        }
    }

    async fn subscribe(&self, topic: &str, qos: QoS) -> Result<(), Box<dyn std::error::Error>> {
        let client_lock = self.client.read().await;
        
        if let Some(client) = client_lock.as_ref() {
            client.subscribe(topic, qos).await?;
            Ok(())
        } else {
            Err("Client not connected".into())
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Define VerneMQ cluster nodes
    let cluster = VerneMQCluster::new(vec![
        "vernemq-node1.example.com:1883".to_string(),
        "vernemq-node2.example.com:1883".to_string(),
        "vernemq-node3.example.com:1883".to_string(),
    ]);

    // Create resilient client
    let client = ResilientMqttClient::new(cluster, "rust_ha_client_001").await?;

    // Subscribe to topics
    client.subscribe("sensors/+/temperature", QoS::AtLeastOnce).await?;
    client.subscribe("alerts/#", QoS::ExactlyOnce).await?;

    println!("Subscribed to topics successfully");

    // Publish messages periodically
    for i in 0..20 {
        let payload = format!(
            "{{\"reading\": {}, \"temperature\": {:.2}, \"timestamp\": {}}}",
            i,
            20.0 + (i as f64 * 0.5),
            chrono::Utc::now().timestamp()
        );

        match client.publish("sensors/room1/temperature", &payload, QoS::AtLeastOnce).await {
            Ok(_) => println!("Published message #{}", i),
            Err(e) => println!("Failed to publish message #{}: {:?}", i, e),
        }

        sleep(Duration::from_secs(2)).await;
    }

    // Keep running to receive messages
    sleep(Duration::from_secs(60)).await;

    Ok(())
}
```

## VerneMQ Configuration Example

Here's a basic VerneMQ cluster configuration:

```erlang
// vernemq.conf for node1
nodename = VerneMQ@192.168.1.10
listener.tcp.default = 0.0.0.0:1883
listener.ws.default = 0.0.0.0:8080

// Clustering configuration
distributed_cookie = vernemq_cluster_secret
cluster.node = VerneMQ@192.168.1.11
cluster.node = VerneMQ@192.168.1.12

// High availability settings
max_offline_messages = 1000
max_online_messages = 1000
persistent_client_expiration = 1w

// Load balancing
shared_subscription_policy = random
allow_anonymous = false
```

## Summary

VerneMQ Clustering provides a robust, distributed MQTT broker solution that excels in enterprise environments requiring high availability and horizontal scalability. By leveraging Erlang's distributed computing model and CRDT-based state synchronization, VerneMQ ensures that client subscriptions, sessions, and messages remain consistent across the cluster even during node failures.

Key advantages include automatic failover, load distribution, persistent session management across nodes, and the ability to scale by simply adding more nodes to the cluster. The C/C++ and Rust examples demonstrate client-side strategies for connecting to clustered brokers with automatic node failover, ensuring applications remain connected even when individual broker nodes become unavailable. This architecture is ideal for IoT deployments, real-time messaging systems, and any application requiring reliable, scalable MQTT messaging with minimal downtime.