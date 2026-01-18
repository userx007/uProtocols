# Error Handling in Rust MQTT

## Overview

Error handling in Rust MQTT applications leverages Rust's type system and Result/Option types to create robust, safe MQTT clients. Unlike C/C++ where errors are often handled through return codes or exceptions, Rust enforces explicit error handling at compile time, making it impossible to accidentally ignore errors.

## Core Concepts

### Result Type
Rust's `Result<T, E>` type represents either success (`Ok(T)`) or failure (`Err(E)`). MQTT operations return Results that must be handled explicitly.

### Error Propagation
The `?` operator provides concise error propagation, automatically converting errors and returning early from functions when errors occur.

### Custom Error Types
MQTT applications benefit from custom error types that can represent various failure modes (connection errors, protocol errors, subscription failures, etc.).

## Code Examples

### Rust Implementation

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use thiserror::Error;

// Custom error type for MQTT operations
#[derive(Error, Debug)]
pub enum MqttError {
    #[error("Connection failed: {0}")]
    ConnectionError(String),
    
    #[error("Subscription failed for topic '{topic}': {reason}")]
    SubscriptionError { topic: String, reason: String },
    
    #[error("Publish failed: {0}")]
    PublishError(String),
    
    #[error("Protocol error: {0}")]
    ProtocolError(String),
    
    #[error("Client error: {0}")]
    ClientError(#[from] rumqttc::ClientError),
    
    #[error("Connection error: {0}")]
    ConnError(#[from] rumqttc::ConnectionError),
    
    #[error("Timeout waiting for response")]
    Timeout,
}

// Result type alias for convenience
pub type MqttResult<T> = Result<T, MqttError>;

pub struct MqttClient {
    client: Client,
}

impl MqttClient {
    /// Creates a new MQTT client with error handling
    pub fn new(client_id: &str, host: &str, port: u16) -> MqttResult<Self> {
        let mut mqtt_options = MqttOptions::new(client_id, host, port);
        mqtt_options.set_keep_alive(Duration::from_secs(30));
        mqtt_options.set_clean_session(true);
        
        let (client, mut connection) = Client::new(mqtt_options, 10);
        
        // Spawn connection handler in separate thread
        std::thread::spawn(move || {
            for notification in connection.iter() {
                match notification {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("Connected successfully");
                    }
                    Ok(Event::Incoming(packet)) => {
                        println!("Received: {:?}", packet);
                    }
                    Ok(Event::Outgoing(_)) => {}
                    Err(e) => {
                        eprintln!("Connection error: {}", e);
                    }
                }
            }
        });
        
        Ok(MqttClient { client })
    }
    
    /// Publishes a message with comprehensive error handling
    pub fn publish(&self, topic: &str, payload: &[u8], qos: QoS) -> MqttResult<()> {
        self.client
            .publish(topic, qos, false, payload)
            .map_err(|e| MqttError::PublishError(format!("Failed to publish to '{}': {}", topic, e)))
    }
    
    /// Subscribes to a topic with error context
    pub fn subscribe(&self, topic: &str, qos: QoS) -> MqttResult<()> {
        self.client
            .subscribe(topic, qos)
            .map_err(|e| MqttError::SubscriptionError {
                topic: topic.to_string(),
                reason: e.to_string(),
            })
    }
    
    /// Unsubscribes with error handling
    pub fn unsubscribe(&self, topic: &str) -> MqttResult<()> {
        self.client
            .unsubscribe(topic)
            .map_err(|e| MqttError::ProtocolError(format!("Unsubscribe failed: {}", e)))
    }
}

// Example with retry logic and error recovery
pub struct RobustMqttClient {
    client: MqttClient,
    max_retries: u32,
}

impl RobustMqttClient {
    pub fn new(client_id: &str, host: &str, port: u16, max_retries: u32) -> MqttResult<Self> {
        let client = MqttClient::new(client_id, host, port)?;
        Ok(RobustMqttClient { client, max_retries })
    }
    
    /// Publishes with automatic retry on failure
    pub fn publish_with_retry(&self, topic: &str, payload: &[u8], qos: QoS) -> MqttResult<()> {
        let mut attempts = 0;
        let mut last_error = None;
        
        while attempts < self.max_retries {
            match self.client.publish(topic, payload, qos) {
                Ok(_) => return Ok(()),
                Err(e) => {
                    attempts += 1;
                    last_error = Some(e);
                    
                    if attempts < self.max_retries {
                        eprintln!("Publish attempt {} failed, retrying...", attempts);
                        std::thread::sleep(Duration::from_millis(100 * attempts as u64));
                    }
                }
            }
        }
        
        Err(last_error.unwrap_or(MqttError::PublishError("Unknown error".to_string())))
    }
}

// Usage example with comprehensive error handling
fn main() -> MqttResult<()> {
    // Using ? operator for clean error propagation
    let client = MqttClient::new("rust_client", "localhost", 1883)?;
    
    // Subscribe with error handling
    if let Err(e) = client.subscribe("sensors/#", QoS::AtLeastOnce) {
        eprintln!("Subscription failed: {}", e);
        // Can continue or return error depending on requirements
    }
    
    // Publish with pattern matching
    match client.publish("sensors/temp", b"22.5", QoS::AtLeastOnce) {
        Ok(_) => println!("Message published successfully"),
        Err(MqttError::PublishError(msg)) => {
            eprintln!("Publish error: {}", msg);
            // Handle publish-specific error
        }
        Err(e) => {
            eprintln!("Unexpected error: {}", e);
            return Err(e);
        }
    }
    
    // Using robust client with retries
    let robust_client = RobustMqttClient::new("robust_client", "localhost", 1883, 3)?;
    robust_client.publish_with_retry("test/topic", b"data", QoS::AtLeastOnce)?;
    
    Ok(())
}
```

### C Implementation (For Comparison)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "C_ErrorHandling_Client"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Error codes
typedef enum {
    MQTT_SUCCESS = 0,
    MQTT_CONNECTION_ERROR = -1,
    MQTT_PUBLISH_ERROR = -2,
    MQTT_SUBSCRIBE_ERROR = -3,
    MQTT_DISCONNECT_ERROR = -4,
    MQTT_MEMORY_ERROR = -5
} mqtt_error_t;

// Error handling structure
typedef struct {
    mqtt_error_t code;
    char message[256];
    int paho_rc;  // Paho return code
} mqtt_error_info_t;

// Set error information
void set_error(mqtt_error_info_t *error, mqtt_error_t code, 
               const char *message, int paho_rc) {
    if (error) {
        error->code = code;
        error->paho_rc = paho_rc;
        snprintf(error->message, sizeof(error->message), "%s (Paho RC: %d)", 
                 message, paho_rc);
    }
}

// Print error information
void print_error(const mqtt_error_info_t *error) {
    if (error && error->code != MQTT_SUCCESS) {
        fprintf(stderr, "Error %d: %s\n", error->code, error->message);
    }
}

// Connect with error handling
mqtt_error_t mqtt_connect_safe(MQTTClient *client, mqtt_error_info_t *error) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    rc = MQTTClient_create(client, ADDRESS, CLIENTID,
                          MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        set_error(error, MQTT_CONNECTION_ERROR, 
                 "Failed to create client", rc);
        return MQTT_CONNECTION_ERROR;
    }
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    rc = MQTTClient_connect(*client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        set_error(error, MQTT_CONNECTION_ERROR, 
                 "Failed to connect", rc);
        MQTTClient_destroy(client);
        return MQTT_CONNECTION_ERROR;
    }
    
    return MQTT_SUCCESS;
}

// Publish with error handling and retry
mqtt_error_t mqtt_publish_safe(MQTTClient client, const char *topic, 
                               const char *payload, int max_retries,
                               mqtt_error_info_t *error) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    int attempts = 0;
    
    if (!topic || !payload) {
        set_error(error, MQTT_PUBLISH_ERROR, 
                 "Invalid parameters (NULL pointer)", 0);
        return MQTT_PUBLISH_ERROR;
    }
    
    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    while (attempts < max_retries) {
        rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            if (rc == MQTTCLIENT_SUCCESS) {
                return MQTT_SUCCESS;
            }
        }
        
        attempts++;
        if (attempts < max_retries) {
            fprintf(stderr, "Publish attempt %d failed (rc=%d), retrying...\n", 
                   attempts, rc);
            usleep(100000 * attempts);  // Exponential backoff
        }
    }
    
    set_error(error, MQTT_PUBLISH_ERROR, 
             "Failed to publish after retries", rc);
    return MQTT_PUBLISH_ERROR;
}

// Subscribe with error handling
mqtt_error_t mqtt_subscribe_safe(MQTTClient client, const char *topic,
                                 mqtt_error_info_t *error) {
    int rc;
    
    if (!topic) {
        set_error(error, MQTT_SUBSCRIBE_ERROR, 
                 "Invalid topic (NULL)", 0);
        return MQTT_SUBSCRIBE_ERROR;
    }
    
    rc = MQTTClient_subscribe(client, topic, QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        set_error(error, MQTT_SUBSCRIBE_ERROR, 
                 "Subscription failed", rc);
        return MQTT_SUBSCRIBE_ERROR;
    }
    
    return MQTT_SUCCESS;
}

// Main example with comprehensive error handling
int main(int argc, char* argv[]) {
    MQTTClient client;
    mqtt_error_info_t error = {0};
    mqtt_error_t result;
    
    // Connect with error handling
    result = mqtt_connect_safe(&client, &error);
    if (result != MQTT_SUCCESS) {
        print_error(&error);
        return EXIT_FAILURE;
    }
    printf("Connected successfully\n");
    
    // Subscribe with error handling
    result = mqtt_subscribe_safe(client, "sensors/#", &error);
    if (result != MQTT_SUCCESS) {
        print_error(&error);
        // Decide whether to continue or exit
    }
    
    // Publish with retry
    result = mqtt_publish_safe(client, TOPIC, "25.3", 3, &error);
    if (result != MQTT_SUCCESS) {
        print_error(&error);
        goto cleanup;
    }
    printf("Message published successfully\n");
    
cleanup:
    // Disconnect
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    
    return (result == MQTT_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### C++ Implementation

```cpp
#include <iostream>
#include <string>
#include <stdexcept>
#include <memory>
#include <chrono>
#include <thread>
#include "mqtt/client.h"

// Custom exception hierarchy
class MqttException : public std::runtime_error {
public:
    explicit MqttException(const std::string& message) 
        : std::runtime_error(message) {}
};

class MqttConnectionException : public MqttException {
public:
    explicit MqttConnectionException(const std::string& message)
        : MqttException("Connection error: " + message) {}
};

class MqttPublishException : public MqttException {
public:
    explicit MqttPublishException(const std::string& message)
        : MqttException("Publish error: " + message) {}
};

class MqttSubscriptionException : public MqttException {
public:
    MqttSubscriptionException(const std::string& topic, const std::string& reason)
        : MqttException("Subscription failed for '" + topic + "': " + reason),
          topic_(topic) {}
    
    const std::string& getTopic() const { return topic_; }
    
private:
    std::string topic_;
};

// RAII wrapper for MQTT client
class MqttClientWrapper {
public:
    MqttClientWrapper(const std::string& serverURI, const std::string& clientId)
        : client_(serverURI, clientId) {
        
        try {
            mqtt::connect_options connOpts;
            connOpts.set_keep_alive_interval(20);
            connOpts.set_clean_session(true);
            
            auto tok = client_.connect(connOpts);
            tok->wait();
            
        } catch (const mqtt::exception& exc) {
            throw MqttConnectionException(exc.what());
        }
    }
    
    ~MqttClientWrapper() {
        try {
            if (client_.is_connected()) {
                client_.disconnect()->wait();
            }
        } catch (const mqtt::exception& exc) {
            std::cerr << "Disconnect error: " << exc.what() << std::endl;
        }
    }
    
    // Delete copy operations
    MqttClientWrapper(const MqttClientWrapper&) = delete;
    MqttClientWrapper& operator=(const MqttClientWrapper&) = delete;
    
    void publish(const std::string& topic, const std::string& payload, 
                 int qos = 1, int maxRetries = 3) {
        int attempts = 0;
        mqtt::exception lastException("Unknown error");
        
        while (attempts < maxRetries) {
            try {
                auto msg = mqtt::make_message(topic, payload);
                msg->set_qos(qos);
                
                auto tok = client_.publish(msg);
                tok->wait();
                return;  // Success
                
            } catch (const mqtt::exception& exc) {
                lastException = exc;
                attempts++;
                
                if (attempts < maxRetries) {
                    std::cerr << "Publish attempt " << attempts 
                             << " failed, retrying..." << std::endl;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(100 * attempts));
                }
            }
        }
        
        throw MqttPublishException(lastException.what());
    }
    
    void subscribe(const std::string& topic, int qos = 1) {
        try {
            auto tok = client_.subscribe(topic, qos);
            tok->wait();
            
        } catch (const mqtt::exception& exc) {
            throw MqttSubscriptionException(topic, exc.what());
        }
    }
    
    void unsubscribe(const std::string& topic) {
        try {
            auto tok = client_.unsubscribe(topic);
            tok->wait();
            
        } catch (const mqtt::exception& exc) {
            throw MqttException("Unsubscribe failed: " + std::string(exc.what()));
        }
    }
    
    bool isConnected() const {
        return client_.is_connected();
    }
    
private:
    mqtt::client client_;
};

// Usage example
int main() {
    try {
        // RAII ensures cleanup even if exceptions occur
        MqttClientWrapper client("tcp://localhost:1883", "cpp_error_client");
        
        std::cout << "Connected successfully" << std::endl;
        
        // Subscribe with exception handling
        try {
            client.subscribe("sensors/#");
            std::cout << "Subscribed successfully" << std::endl;
        } catch (const MqttSubscriptionException& exc) {
            std::cerr << exc.what() << std::endl;
            std::cerr << "Failed topic: " << exc.getTopic() << std::endl;
            // Decide whether to continue
        }
        
        // Publish with retry logic
        try {
            client.publish("sensors/temperature", "23.7", 1, 3);
            std::cout << "Published successfully" << std::endl;
        } catch (const MqttPublishException& exc) {
            std::cerr << exc.what() << std::endl;
            throw;  // Re-throw if critical
        }
        
        // Client automatically disconnects when leaving scope
        
    } catch (const MqttConnectionException& exc) {
        std::cerr << "Fatal: " << exc.what() << std::endl;
        return EXIT_FAILURE;
        
    } catch (const MqttException& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return EXIT_FAILURE;
        
    } catch (const std::exception& exc) {
        std::cerr << "Unexpected error: " << exc.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
```

## Summary

**Rust Advantages:**
- **Compile-time error checking**: Impossible to ignore errors; all Results must be handled
- **Type-safe error propagation**: The `?` operator provides clean, automatic error conversion
- **No runtime overhead**: Zero-cost abstractions for error handling
- **Custom error types**: Easy to create domain-specific errors with `thiserror` or `anyhow`
- **Pattern matching**: Exhaustive error handling with match expressions

**C Approach:**
- Manual error checking required for every operation
- Return codes can be accidentally ignored
- Error context must be manually maintained
- Requires discipline to implement comprehensive error handling
- Memory management adds complexity (potential for leaks on error paths)

**C++ Approach:**
- Exception-based error handling with RAII for resource cleanup
- Can skip intermediate error handling (exceptions bubble up)
- Runtime overhead from exception handling
- Custom exception hierarchies provide type-safe error categorization
- RAII ensures cleanup even when exceptions occur

**Key Takeaway:** Rust's error handling model makes robust MQTT applications easier to build by enforcing error handling at compile time, while C requires manual discipline and C++ relies on runtime exception mechanisms. Rust's approach prevents entire categories of bugs before code ever runs.