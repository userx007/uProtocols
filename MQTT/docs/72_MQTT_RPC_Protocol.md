# MQTT-RPC Protocol: Detailed Description

## Overview

MQTT-RPC (Remote Procedure Call over MQTT) is a design pattern that enables request-response communication on top of MQTT's publish-subscribe architecture. While MQTT is inherently asynchronous and designed for one-way messaging, MQTT-RPC adds a synchronous-like RPC layer, allowing clients to invoke remote procedures and receive responses.

## Core Concepts

### Topic Structure

MQTT-RPC typically uses a structured topic convention:

**Request Topic Pattern:**
```
{service}/{method}/request/{client_id}
```

**Response Topic Pattern:**
```
{service}/{method}/response/{client_id}
```

Or alternatively:
```
rpc/{service}/{method}/req
rpc/{service}/{method}/res/{correlation_id}
```

### Key Components

1. **Correlation ID**: Unique identifier linking requests to responses
2. **Request/Response Payloads**: Typically JSON or Protocol Buffers
3. **Timeout Handling**: Client-side timeouts for unresponsive services
4. **Error Handling**: Standardized error responses

## C/C++ Implementation

Here's a comprehensive example using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS    "tcp://localhost:1883"
#define CLIENT_ID         "rpc_client_001"
#define QOS              1
#define TIMEOUT          10000L
#define RPC_TIMEOUT      5000  // 5 seconds

typedef struct {
    char correlation_id[37];  // UUID length
    char* response_payload;
    int response_received;
    int timeout_ms;
} RPCContext;

// Generate a simple correlation ID (in production, use UUID)
void generate_correlation_id(char* buffer, size_t size) {
    snprintf(buffer, size, "%ld-%d", time(NULL), rand());
}

// Message arrival callback
int message_arrived(void* context, char* topic_name, int topic_len, 
                    MQTTClient_message* message) {
    RPCContext* rpc_ctx = (RPCContext*)context;
    
    // Extract correlation_id from topic or payload
    // For simplicity, assuming it's in the topic: rpc/service/method/res/{corr_id}
    char* corr_id_start = strrchr(topic_name, '/');
    if (corr_id_start && strcmp(corr_id_start + 1, rpc_ctx->correlation_id) == 0) {
        rpc_ctx->response_payload = malloc(message->payloadlen + 1);
        memcpy(rpc_ctx->response_payload, message->payload, message->payloadlen);
        rpc_ctx->response_payload[message->payloadlen] = '\0';
        rpc_ctx->response_received = 1;
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic_name);
    return 1;
}

// RPC Client function
int mqtt_rpc_call(MQTTClient client, const char* service, const char* method,
                  const char* request_payload, char** response_payload) {
    RPCContext rpc_ctx = {0};
    rpc_ctx.timeout_ms = RPC_TIMEOUT;
    
    // Generate correlation ID
    generate_correlation_id(rpc_ctx.correlation_id, sizeof(rpc_ctx.correlation_id));
    
    // Subscribe to response topic
    char response_topic[256];
    snprintf(response_topic, sizeof(response_topic), 
             "rpc/%s/%s/res/%s", service, method, rpc_ctx.correlation_id);
    
    MQTTClient_subscribe(client, response_topic, QOS);
    
    // Set callback context
    MQTTClient_setCallbacks(client, &rpc_ctx, NULL, message_arrived, NULL);
    
    // Publish request
    char request_topic[256];
    snprintf(request_topic, sizeof(request_topic), 
             "rpc/%s/%s/req", service, method);
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void*)request_payload;
    pubmsg.payloadlen = strlen(request_payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    MQTTClient_publishMessage(client, request_topic, &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
    
    // Wait for response with timeout
    int elapsed = 0;
    while (!rpc_ctx.response_received && elapsed < rpc_ctx.timeout_ms) {
        usleep(100000);  // 100ms
        elapsed += 100;
    }
    
    // Unsubscribe
    MQTTClient_unsubscribe(client, response_topic);
    
    if (rpc_ctx.response_received) {
        *response_payload = rpc_ctx.response_payload;
        return 0;  // Success
    }
    
    return -1;  // Timeout
}

// RPC Server handler
void mqtt_rpc_server(MQTTClient client, const char* service) {
    char request_topic[256];
    snprintf(request_topic, sizeof(request_topic), "rpc/%s/+/req", service);
    
    MQTTClient_subscribe(client, request_topic, QOS);
    
    // In a real implementation, you'd have a message handler that:
    // 1. Parses the method from the topic
    // 2. Executes the corresponding function
    // 3. Publishes the response to the response topic
}

int main() {
    MQTTClient client;
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect\n");
        return -1;
    }
    
    // Example RPC call
    char* response = NULL;
    if (mqtt_rpc_call(client, "calculator", "add", 
                      "{\"a\":5,\"b\":3}", &response) == 0) {
        printf("RPC Response: %s\n", response);
        free(response);
    } else {
        printf("RPC call timeout\n");
    }
    
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    return 0;
}
```

## Rust Implementation

Using the `rumqttc` library for MQTT and `tokio` for async runtime:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use tokio::sync::oneshot;
use tokio::time::{timeout, Duration};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;
use uuid::Uuid;

#[derive(Serialize, Deserialize, Debug)]
struct RpcRequest {
    correlation_id: String,
    method: String,
    params: serde_json::Value,
}

#[derive(Serialize, Deserialize, Debug)]
struct RpcResponse {
    correlation_id: String,
    result: Option<serde_json::Value>,
    error: Option<String>,
}

type PendingCalls = Arc<Mutex<HashMap<String, oneshot::Sender<RpcResponse>>>>;

struct MqttRpcClient {
    client: AsyncClient,
    pending_calls: PendingCalls,
}

impl MqttRpcClient {
    async fn new(broker: &str, client_id: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, broker, 1883);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        let pending_calls: PendingCalls = Arc::new(Mutex::new(HashMap::new()));
        let pending_calls_clone = pending_calls.clone();
        
        // Spawn task to handle incoming messages
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(publish))) => {
                        let topic = publish.topic.clone();
                        
                        // Extract correlation_id from topic: rpc/service/method/res/{corr_id}
                        if let Some(corr_id) = topic.split('/').last() {
                            if let Ok(response) = serde_json::from_slice::<RpcResponse>(&publish.payload) {
                                let mut pending = pending_calls_clone.lock().await;
                                if let Some(sender) = pending.remove(corr_id) {
                                    let _ = sender.send(response);
                                }
                            }
                        }
                    }
                    Ok(_) => {}
                    Err(e) => {
                        eprintln!("MQTT Error: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(1)).await;
                    }
                }
            }
        });
        
        Ok(MqttRpcClient {
            client,
            pending_calls,
        })
    }
    
    async fn call(
        &self,
        service: &str,
        method: &str,
        params: serde_json::Value,
        timeout_ms: u64,
    ) -> Result<RpcResponse, Box<dyn std::error::Error>> {
        let correlation_id = Uuid::new_v4().to_string();
        
        // Subscribe to response topic
        let response_topic = format!("rpc/{}/{}/res/{}", service, method, correlation_id);
        self.client.subscribe(&response_topic, QoS::AtLeastOnce).await?;
        
        // Create oneshot channel for response
        let (tx, rx) = oneshot::channel();
        self.pending_calls.lock().await.insert(correlation_id.clone(), tx);
        
        // Build and publish request
        let request = RpcRequest {
            correlation_id: correlation_id.clone(),
            method: method.to_string(),
            params,
        };
        
        let request_topic = format!("rpc/{}/{}/req", service, method);
        let payload = serde_json::to_vec(&request)?;
        
        self.client.publish(
            request_topic,
            QoS::AtLeastOnce,
            false,
            payload,
        ).await?;
        
        // Wait for response with timeout
        let result = timeout(Duration::from_millis(timeout_ms), rx).await;
        
        // Cleanup
        self.client.unsubscribe(&response_topic).await?;
        
        match result {
            Ok(Ok(response)) => Ok(response),
            Ok(Err(_)) => Err("Channel closed".into()),
            Err(_) => {
                self.pending_calls.lock().await.remove(&correlation_id);
                Err("RPC timeout".into())
            }
        }
    }
}

// RPC Server
struct MqttRpcServer {
    client: AsyncClient,
    service_name: String,
}

impl MqttRpcServer {
    async fn new(
        broker: &str,
        client_id: &str,
        service_name: &str,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, broker, 1883);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        
        // Subscribe to all methods for this service
        let request_topic = format!("rpc/{}/+/req", service_name);
        client.subscribe(&request_topic, QoS::AtLeastOnce).await?;
        
        let service_name_clone = service_name.to_string();
        let client_clone = client.clone();
        
        // Spawn handler task
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(publish))) => {
                        if let Ok(request) = serde_json::from_slice::<RpcRequest>(&publish.payload) {
                            let response_topic = format!(
                                "rpc/{}/{}/res/{}",
                                service_name_clone,
                                request.method,
                                request.correlation_id
                            );
                            
                            // Execute method and get result (simplified)
                            let result = Self::execute_method(&request.method, &request.params).await;
                            
                            let response = RpcResponse {
                                correlation_id: request.correlation_id,
                                result: Some(result),
                                error: None,
                            };
                            
                            if let Ok(payload) = serde_json::to_vec(&response) {
                                let _ = client_clone.publish(
                                    response_topic,
                                    QoS::AtLeastOnce,
                                    false,
                                    payload,
                                ).await;
                            }
                        }
                    }
                    Ok(_) => {}
                    Err(e) => {
                        eprintln!("Server Error: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(1)).await;
                    }
                }
            }
        });
        
        Ok(MqttRpcServer {
            client,
            service_name: service_name.to_string(),
        })
    }
    
    async fn execute_method(method: &str, params: &serde_json::Value) -> serde_json::Value {
        // Method dispatch logic here
        match method {
            "add" => {
                let a = params["a"].as_i64().unwrap_or(0);
                let b = params["b"].as_i64().unwrap_or(0);
                serde_json::json!({"sum": a + b})
            }
            _ => serde_json::json!({"error": "Unknown method"})
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Start server
    let _server = MqttRpcServer::new(
        "localhost",
        "rpc_server",
        "calculator",
    ).await?;
    
    tokio::time::sleep(Duration::from_secs(1)).await;
    
    // Create client
    let client = MqttRpcClient::new("localhost", "rpc_client").await?;
    
    // Make RPC call
    let params = serde_json::json!({"a": 5, "b": 3});
    match client.call("calculator", "add", params, 5000).await {
        Ok(response) => println!("Result: {:?}", response.result),
        Err(e) => eprintln!("RPC Error: {}", e),
    }
    
    Ok(())
}
```

## Summary

**MQTT-RPC Protocol** bridges MQTT's asynchronous pub/sub model with synchronous request-response semantics needed for RPC operations. Key characteristics:

- **Topic Convention**: Structured topics separate requests and responses using correlation IDs
- **Correlation Mechanism**: Unique identifiers link requests to their responses
- **Timeout Management**: Client-side timeouts prevent indefinite blocking
- **Payload Format**: Typically JSON or Protocol Buffers for serialization
- **Use Cases**: Microservices communication, IoT device control, distributed system coordination

**Advantages**: Leverages existing MQTT infrastructure, supports asynchronous operations, and works well in unreliable network conditions.

**Limitations**: Higher latency than traditional RPC, requires careful correlation ID management, and adds complexity to MQTT's simple pub/sub model.