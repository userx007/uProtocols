# MQTT Request-Response Pattern: RPC-Style Communication

## Overview

The **Request-Response Pattern** in MQTT implements Remote Procedure Call (RPC-style) communication over the publish-subscribe protocol. While MQTT is inherently designed for asynchronous, decoupled messaging, many applications require synchronous request-response interactions similar to traditional RPC or HTTP APIs. This pattern bridges that gap by establishing conventions for clients to send requests and receive correlated responses.

## Key Concepts

### Core Components

1. **Request Topic Structure**: Typically follows a pattern like `request/{service}/{method}` or `rpc/{client-id}/request`
2. **Response Topic Structure**: Often includes the requester's identifier: `response/{client-id}` or `rpc/{client-id}/response`
3. **Correlation ID**: A unique identifier embedded in the message payload that links requests to their corresponding responses
4. **Timeout Handling**: Mechanisms to handle scenarios where responses don't arrive within expected timeframes

### Pattern Variations

- **Dedicated Response Topics**: Each client subscribes to its own response topic
- **Shared Response Topics**: Multiple clients share a topic and filter by correlation ID
- **Temporary Topics**: Response topics created dynamically per request

## Implementation Details

The pattern typically involves:

1. **Requester**:
   - Generates a unique correlation ID
   - Subscribes to a response topic (if not already subscribed)
   - Publishes request with correlation ID to the request topic
   - Waits for response matching the correlation ID
   - Implements timeout logic

2. **Responder**:
   - Subscribes to the request topic
   - Processes incoming requests
   - Extracts correlation ID and response topic from request
   - Publishes response with same correlation ID to the response topic

## C/C++ Implementation

Here's a complete example using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <time.h>
#include <pthread.h>

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID_PREFIX "rpc_client_"
#define QOS 1
#define TIMEOUT 10000L
#define REQUEST_TIMEOUT_SEC 5

// Request-Response structure
typedef struct {
    char correlation_id[37]; // UUID string
    char response_topic[128];
    char *response_payload;
    int response_received;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} RPCContext;

// Generate simple correlation ID
void generate_correlation_id(char *buffer) {
    sprintf(buffer, "%ld-%d", time(NULL), rand() % 10000);
}

// Message callback for responses
int message_arrived(void *context, char *topic, int topic_len, MQTTClient_message *message) {
    RPCContext *rpc_ctx = (RPCContext *)context;
    
    // Parse the response to check correlation ID
    // Simple format: "correlation_id:response_data"
    char *payload = (char *)message->payload;
    char recv_corr_id[37];
    
    if (sscanf(payload, "%36[^:]:", recv_corr_id) == 1) {
        pthread_mutex_lock(&rpc_ctx->mutex);
        
        if (strcmp(recv_corr_id, rpc_ctx->correlation_id) == 0) {
            // Extract response data after the colon
            char *response_start = strchr(payload, ':') + 1;
            rpc_ctx->response_payload = strdup(response_start);
            rpc_ctx->response_received = 1;
            pthread_cond_signal(&rpc_ctx->cond);
        }
        
        pthread_mutex_unlock(&rpc_ctx->mutex);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

// RPC Client
int rpc_call(MQTTClient client, const char *request_topic, 
             const char *response_topic, const char *request_data,
             char **response, int timeout_sec) {
    
    RPCContext rpc_ctx;
    generate_correlation_id(rpc_ctx.correlation_id);
    strcpy(rpc_ctx.response_topic, response_topic);
    rpc_ctx.response_payload = NULL;
    rpc_ctx.response_received = 0;
    pthread_mutex_init(&rpc_ctx.mutex, NULL);
    pthread_cond_init(&rpc_ctx.cond, NULL);
    
    // Set callback with context
    MQTTClient_setCallbacks(client, &rpc_ctx, NULL, message_arrived, NULL);
    
    // Subscribe to response topic
    MQTTClient_subscribe(client, response_topic, QOS);
    
    // Build request payload: "correlation_id:request_data"
    char payload[512];
    snprintf(payload, sizeof(payload), "%s:%s", rpc_ctx.correlation_id, request_data);
    
    // Publish request
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, request_topic, &pubmsg, NULL);
    
    // Wait for response with timeout
    pthread_mutex_lock(&rpc_ctx.mutex);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    
    int result = 0;
    while (!rpc_ctx.response_received) {
        if (pthread_cond_timedwait(&rpc_ctx.cond, &rpc_ctx.mutex, &ts) != 0) {
            // Timeout
            result = -1;
            break;
        }
    }
    
    if (result == 0 && rpc_ctx.response_payload) {
        *response = rpc_ctx.response_payload;
    }
    
    pthread_mutex_unlock(&rpc_ctx.mutex);
    pthread_mutex_destroy(&rpc_ctx.mutex);
    pthread_cond_destroy(&rpc_ctx.cond);
    
    return result;
}

// RPC Server - processes requests
void handle_request(char *payload, char *response_buffer, size_t buffer_size) {
    char correlation_id[37];
    char request_data[256];
    
    if (sscanf(payload, "%36[^:]:%255s", correlation_id, request_data) == 2) {
        // Process the request (simple echo with modification)
        snprintf(response_buffer, buffer_size, "%s:Processed: %s", 
                 correlation_id, request_data);
    }
}

int server_message_callback(void *context, char *topic, int topic_len, 
                            MQTTClient_message *message) {
    MQTTClient client = (MQTTClient)context;
    char response[512];
    
    handle_request((char *)message->payload, response, sizeof(response));
    
    // Publish response
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = response;
    pubmsg.payloadlen = strlen(response);
    pubmsg.qos = QOS;
    
    MQTTClient_publishMessage(client, "rpc/response", &pubmsg, NULL);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

int main() {
    MQTTClient client;
    MQTTClient_create(&client, BROKER_ADDRESS, "rpc_example_client", 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect\n");
        return -1;
    }
    
    // Make RPC call
    char *response = NULL;
    if (rpc_call(client, "rpc/request", "rpc/response", 
                 "Hello RPC", &response, REQUEST_TIMEOUT_SEC) == 0) {
        printf("RPC Response: %s\n", response);
        free(response);
    } else {
        printf("RPC call timed out\n");
    }
    
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    return 0;
}
```

## Rust Implementation

Here's an implementation using the `rumqttc` library:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::sync::oneshot;
use tokio::time::{timeout, Duration};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;
use uuid::Uuid;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
struct RPCRequest {
    correlation_id: String,
    response_topic: String,
    method: String,
    params: serde_json::Value,
}

#[derive(Debug, Serialize, Deserialize)]
struct RPCResponse {
    correlation_id: String,
    result: Option<serde_json::Value>,
    error: Option<String>,
}

type PendingRequests = Arc<Mutex<HashMap<String, oneshot::Sender<RPCResponse>>>>;

pub struct RPCClient {
    client: AsyncClient,
    response_topic: String,
    pending: PendingRequests,
}

impl RPCClient {
    pub async fn new(broker: &str, client_id: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, broker, 1883);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        let response_topic = format!("rpc/response/{}", client_id);
        let pending = Arc::new(Mutex::new(HashMap::new()));
        
        // Subscribe to response topic
        client.subscribe(&response_topic, QoS::AtLeastOnce).await?;
        
        // Spawn task to handle incoming responses
        let pending_clone = pending.clone();
        let response_topic_clone = response_topic.clone();
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(p))) => {
                        if p.topic == response_topic_clone {
                            if let Ok(response) = serde_json::from_slice::<RPCResponse>(&p.payload) {
                                let mut pending = pending_clone.lock().await;
                                if let Some(sender) = pending.remove(&response.correlation_id) {
                                    let _ = sender.send(response);
                                }
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("MQTT Error: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(1)).await;
                    }
                    _ => {}
                }
            }
        });
        
        Ok(RPCClient {
            client,
            response_topic,
            pending,
        })
    }
    
    pub async fn call(
        &self,
        request_topic: &str,
        method: &str,
        params: serde_json::Value,
        timeout_duration: Duration,
    ) -> Result<RPCResponse, Box<dyn std::error::Error>> {
        let correlation_id = Uuid::new_v4().to_string();
        let (tx, rx) = oneshot::channel();
        
        // Register pending request
        {
            let mut pending = self.pending.lock().await;
            pending.insert(correlation_id.clone(), tx);
        }
        
        // Build and send request
        let request = RPCRequest {
            correlation_id: correlation_id.clone(),
            response_topic: self.response_topic.clone(),
            method: method.to_string(),
            params,
        };
        
        let payload = serde_json::to_vec(&request)?;
        self.client
            .publish(request_topic, QoS::AtLeastOnce, false, payload)
            .await?;
        
        // Wait for response with timeout
        match timeout(timeout_duration, rx).await {
            Ok(Ok(response)) => Ok(response),
            Ok(Err(_)) => Err("Response channel closed".into()),
            Err(_) => {
                // Clean up pending request on timeout
                let mut pending = self.pending.lock().await;
                pending.remove(&correlation_id);
                Err("Request timeout".into())
            }
        }
    }
}

pub struct RPCServer {
    client: AsyncClient,
    request_topic: String,
}

impl RPCServer {
    pub async fn new(
        broker: &str,
        client_id: &str,
        request_topic: &str,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, broker, 1883);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        
        // Subscribe to request topic
        client.subscribe(request_topic, QoS::AtLeastOnce).await?;
        
        let request_topic_owned = request_topic.to_string();
        let client_clone = client.clone();
        
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::Publish(p))) => {
                        if p.topic == request_topic_owned {
                            if let Ok(request) = serde_json::from_slice::<RPCRequest>(&p.payload) {
                                Self::handle_request(client_clone.clone(), request).await;
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("MQTT Error: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(1)).await;
                    }
                    _ => {}
                }
            }
        });
        
        Ok(RPCServer {
            client,
            request_topic: request_topic.to_string(),
        })
    }
    
    async fn handle_request(client: AsyncClient, request: RPCRequest) {
        // Process the request (example: echo service)
        let result = match request.method.as_str() {
            "echo" => Some(request.params),
            "add" => {
                if let Some(nums) = request.params.as_array() {
                    let sum: f64 = nums.iter()
                        .filter_map(|v| v.as_f64())
                        .sum();
                    Some(serde_json::json!(sum))
                } else {
                    None
                }
            }
            _ => None,
        };
        
        let response = if let Some(res) = result {
            RPCResponse {
                correlation_id: request.correlation_id,
                result: Some(res),
                error: None,
            }
        } else {
            RPCResponse {
                correlation_id: request.correlation_id,
                result: None,
                error: Some("Unknown method or invalid params".to_string()),
            }
        };
        
        if let Ok(payload) = serde_json::to_vec(&response) {
            let _ = client
                .publish(&request.response_topic, QoS::AtLeastOnce, false, payload)
                .await;
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Start server
    let _server = RPCServer::new("localhost", "rpc_server", "rpc/request").await?;
    
    // Give server time to subscribe
    tokio::time::sleep(Duration::from_secs(1)).await;
    
    // Create client and make RPC call
    let client = RPCClient::new("localhost", "rpc_client_1").await?;
    
    // Example 1: Echo request
    let response = client.call(
        "rpc/request",
        "echo",
        serde_json::json!({"message": "Hello RPC"}),
        Duration::from_secs(5),
    ).await?;
    
    println!("Echo response: {:?}", response);
    
    // Example 2: Add request
    let response = client.call(
        "rpc/request",
        "add",
        serde_json::json!([10, 20, 30]),
        Duration::from_secs(5),
    ).await?;
    
    println!("Add response: {:?}", response);
    
    // Keep running
    tokio::time::sleep(Duration::from_secs(2)).await;
    
    Ok(())
}
```

## Summary

The **Request-Response Pattern** enables synchronous, RPC-style communication over MQTT's asynchronous publish-subscribe architecture. Key implementation aspects include:

**Essential Elements:**
- **Correlation IDs** uniquely link requests to responses
- **Dedicated response topics** per client ensure proper message routing
- **Timeout mechanisms** handle unresponsive services gracefully
- **JSON/structured payloads** carry method names, parameters, and results

**Benefits:**
- Familiar RPC semantics over MQTT infrastructure
- Leverages existing MQTT broker capabilities
- No need for direct client-to-client connections
- Works across firewalls and NAT boundaries

**Trade-offs:**
- Adds complexity to the simple pub-sub model
- Requires careful correlation ID management
- Response topics increase subscription overhead
- Not as efficient as purpose-built RPC protocols for high-throughput scenarios

**Best Use Cases:**
- IoT device control and configuration
- Microservice communication in MQTT-centric architectures
- Remote management systems requiring command acknowledgments
- Scenarios where MQTT's QoS and will message features add value over HTTP

This pattern demonstrates MQTT's flexibility while maintaining the protocol's core strengths of reliability, lightweight operation, and network resilience.