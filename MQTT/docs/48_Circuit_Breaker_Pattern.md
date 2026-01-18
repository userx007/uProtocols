# Circuit Breaker Pattern in MQTT

## Overview

The Circuit Breaker Pattern is a critical design pattern for building resilient MQTT applications. It prevents cascading failures when an MQTT broker becomes unavailable or unresponsive by "breaking the circuit" after a threshold of failures is reached. This pattern allows your application to fail gracefully, avoid overwhelming a struggling broker, and automatically recover when the broker becomes healthy again.

## How It Works

The Circuit Breaker operates in three states:

1. **Closed (Normal Operation)**: Requests flow through normally to the MQTT broker. Failures are counted.

2. **Open (Failure State)**: After a threshold of consecutive failures, the circuit opens. All requests fail immediately without attempting to contact the broker, preventing resource waste and allowing the broker time to recover.

3. **Half-Open (Recovery Testing)**: After a timeout period, the circuit allows a limited number of test requests through. If these succeed, the circuit closes and normal operation resumes. If they fail, the circuit returns to the open state.

This pattern is essential for:
- Preventing resource exhaustion from repeated connection attempts
- Providing fallback mechanisms during broker outages
- Graceful degradation of service
- Faster recovery times through controlled retry logic

## C/C++ Implementation

Here's a robust Circuit Breaker implementation using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "CircuitBreakerClient"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Circuit Breaker States
typedef enum {
    CB_CLOSED,      // Normal operation
    CB_OPEN,        // Circuit is open, failing fast
    CB_HALF_OPEN    // Testing if service recovered
} CircuitState;

// Circuit Breaker Configuration
typedef struct {
    CircuitState state;
    int failure_count;
    int failure_threshold;      // Number of failures before opening
    time_t last_failure_time;
    int timeout_seconds;        // Time before attempting half-open
    int success_threshold;      // Successes needed in half-open to close
    int half_open_successes;
} CircuitBreaker;

// Initialize circuit breaker
void cb_init(CircuitBreaker* cb, int failure_threshold, int timeout_seconds, int success_threshold) {
    cb->state = CB_CLOSED;
    cb->failure_count = 0;
    cb->failure_threshold = failure_threshold;
    cb->timeout_seconds = timeout_seconds;
    cb->success_threshold = success_threshold;
    cb->half_open_successes = 0;
    cb->last_failure_time = 0;
}

// Check if circuit breaker allows operation
int cb_can_attempt(CircuitBreaker* cb) {
    time_t current_time = time(NULL);
    
    switch(cb->state) {
        case CB_CLOSED:
            return 1;
            
        case CB_OPEN:
            // Check if timeout has elapsed
            if (difftime(current_time, cb->last_failure_time) >= cb->timeout_seconds) {
                printf("Circuit Breaker: Transitioning to HALF_OPEN\n");
                cb->state = CB_HALF_OPEN;
                cb->half_open_successes = 0;
                return 1;
            }
            printf("Circuit Breaker: OPEN - Failing fast\n");
            return 0;
            
        case CB_HALF_OPEN:
            return 1;
    }
    return 0;
}

// Record successful operation
void cb_record_success(CircuitBreaker* cb) {
    switch(cb->state) {
        case CB_CLOSED:
            cb->failure_count = 0;
            break;
            
        case CB_HALF_OPEN:
            cb->half_open_successes++;
            if (cb->half_open_successes >= cb->success_threshold) {
                printf("Circuit Breaker: Closing circuit - Service recovered\n");
                cb->state = CB_CLOSED;
                cb->failure_count = 0;
            }
            break;
            
        case CB_OPEN:
            // Should not happen
            break;
    }
}

// Record failed operation
void cb_record_failure(CircuitBreaker* cb) {
    cb->last_failure_time = time(NULL);
    
    switch(cb->state) {
        case CB_CLOSED:
            cb->failure_count++;
            if (cb->failure_count >= cb->failure_threshold) {
                printf("Circuit Breaker: Opening circuit - Threshold reached (%d failures)\n", 
                       cb->failure_count);
                cb->state = CB_OPEN;
            }
            break;
            
        case CB_HALF_OPEN:
            printf("Circuit Breaker: Test failed - Returning to OPEN state\n");
            cb->state = CB_OPEN;
            cb->half_open_successes = 0;
            break;
            
        case CB_OPEN:
            // Already open, just update timestamp
            break;
    }
}

// MQTT Connection with Circuit Breaker
int mqtt_connect_with_cb(MQTTClient* client, CircuitBreaker* cb) {
    if (!cb_can_attempt(cb)) {
        return MQTTCLIENT_FAILURE;
    }
    
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.connectTimeout = 5;
    
    int rc = MQTTClient_connect(*client, &conn_opts);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Connection failed with code %d\n", rc);
        cb_record_failure(cb);
        return rc;
    }
    
    printf("Connected successfully\n");
    cb_record_success(cb);
    return rc;
}

// Publish with Circuit Breaker protection
int mqtt_publish_with_cb(MQTTClient client, CircuitBreaker* cb, 
                         const char* topic, const char* payload) {
    if (!cb_can_attempt(cb)) {
        // Fallback: log to file, queue for later, etc.
        printf("Circuit OPEN - Storing message to fallback queue\n");
        return MQTTCLIENT_FAILURE;
    }
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Publish failed with code %d\n", rc);
        cb_record_failure(cb);
        return rc;
    }
    
    // Wait for delivery
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        cb_record_success(cb);
    } else {
        cb_record_failure(cb);
    }
    
    return rc;
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    CircuitBreaker cb;
    int rc;
    
    // Initialize circuit breaker: 3 failures, 10 second timeout, 2 successes to close
    cb_init(&cb, 3, 10, 2);
    
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Simulate publishing loop with potential failures
    for (int i = 0; i < 20; i++) {
        printf("\n--- Attempt %d ---\n", i + 1);
        
        // Try to connect if not connected
        if (!MQTTClient_isConnected(client)) {
            rc = mqtt_connect_with_cb(&client, &cb);
            if (rc != MQTTCLIENT_SUCCESS) {
                sleep(2);
                continue;
            }
        }
        
        // Publish message
        char payload[100];
        snprintf(payload, sizeof(payload), "{\"temperature\": %.1f, \"timestamp\": %ld}", 
                 20.0 + (rand() % 100) / 10.0, time(NULL));
        
        rc = mqtt_publish_with_cb(client, &cb, TOPIC, payload);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            printf("Message published successfully\n");
        }
        
        sleep(2);
    }
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

## Rust Implementation

Here's a comprehensive Circuit Breaker implementation using the `rumqttc` library:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration, Instant};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Debug, Clone, PartialEq)]
enum CircuitState {
    Closed,
    Open,
    HalfOpen,
}

#[derive(Debug, Clone)]
struct CircuitBreakerConfig {
    failure_threshold: u32,
    timeout_duration: Duration,
    success_threshold: u32,
}

struct CircuitBreaker {
    state: CircuitState,
    failure_count: u32,
    success_count: u32,
    last_failure_time: Option<Instant>,
    config: CircuitBreakerConfig,
}

impl CircuitBreaker {
    fn new(config: CircuitBreakerConfig) -> Self {
        CircuitBreaker {
            state: CircuitState::Closed,
            failure_count: 0,
            success_count: 0,
            last_failure_time: None,
            config,
        }
    }

    fn can_attempt(&mut self) -> bool {
        match self.state {
            CircuitState::Closed => true,
            CircuitState::Open => {
                if let Some(last_failure) = self.last_failure_time {
                    if last_failure.elapsed() >= self.config.timeout_duration {
                        println!("Circuit Breaker: Transitioning to HALF_OPEN");
                        self.state = CircuitState::HalfOpen;
                        self.success_count = 0;
                        return true;
                    }
                }
                println!("Circuit Breaker: OPEN - Failing fast");
                false
            }
            CircuitState::HalfOpen => true,
        }
    }

    fn record_success(&mut self) {
        match self.state {
            CircuitState::Closed => {
                self.failure_count = 0;
            }
            CircuitState::HalfOpen => {
                self.success_count += 1;
                if self.success_count >= self.config.success_threshold {
                    println!("Circuit Breaker: Closing circuit - Service recovered");
                    self.state = CircuitState::Closed;
                    self.failure_count = 0;
                    self.success_count = 0;
                }
            }
            CircuitState::Open => {}
        }
    }

    fn record_failure(&mut self) {
        self.last_failure_time = Some(Instant::now());

        match self.state {
            CircuitState::Closed => {
                self.failure_count += 1;
                if self.failure_count >= self.config.failure_threshold {
                    println!(
                        "Circuit Breaker: Opening circuit - Threshold reached ({} failures)",
                        self.failure_count
                    );
                    self.state = CircuitState::Open;
                }
            }
            CircuitState::HalfOpen => {
                println!("Circuit Breaker: Test failed - Returning to OPEN state");
                self.state = CircuitState::Open;
                self.success_count = 0;
            }
            CircuitState::Open => {}
        }
    }
}

struct MqttCircuitBreaker {
    client: AsyncClient,
    circuit_breaker: Arc<Mutex<CircuitBreaker>>,
}

impl MqttCircuitBreaker {
    async fn new(broker: &str, port: u16, client_id: &str, config: CircuitBreakerConfig) -> Self {
        let mut mqttoptions = MqttOptions::new(client_id, broker, port);
        mqttoptions.set_keep_alive(Duration::from_secs(20));
        mqttoptions.set_connection_timeout(5);

        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
        let circuit_breaker = Arc::new(Mutex::new(CircuitBreaker::new(config)));

        // Spawn event loop to handle connection state
        let cb_clone = Arc::clone(&circuit_breaker);
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Packet::ConnAck(_))) => {
                        println!("Connected to MQTT broker");
                        cb_clone.lock().await.record_success();
                    }
                    Err(e) => {
                        eprintln!("MQTT Error: {:?}", e);
                        cb_clone.lock().await.record_failure();
                        sleep(Duration::from_secs(2)).await;
                    }
                    _ => {}
                }
            }
        });

        MqttCircuitBreaker {
            client,
            circuit_breaker,
        }
    }

    async fn publish(&self, topic: &str, payload: &str, qos: QoS) -> Result<(), String> {
        let mut cb = self.circuit_breaker.lock().await;

        if !cb.can_attempt() {
            // Implement fallback: store to queue, log to file, etc.
            println!("Circuit OPEN - Message stored to fallback queue");
            return Err("Circuit breaker is open".to_string());
        }

        drop(cb); // Release lock before async operation

        match self.client.publish(topic, qos, false, payload).await {
            Ok(_) => {
                println!("Message published successfully");
                self.circuit_breaker.lock().await.record_success();
                Ok(())
            }
            Err(e) => {
                eprintln!("Publish failed: {:?}", e);
                self.circuit_breaker.lock().await.record_failure();
                Err(format!("Publish error: {:?}", e))
            }
        }
    }

    async fn get_state(&self) -> CircuitState {
        self.circuit_breaker.lock().await.state.clone()
    }
}

#[tokio::main]
async fn main() {
    let config = CircuitBreakerConfig {
        failure_threshold: 3,
        timeout_duration: Duration::from_secs(10),
        success_threshold: 2,
    };

    let mqtt_cb = MqttCircuitBreaker::new(
        "localhost",
        1883,
        "circuit_breaker_client",
        config,
    ).await;

    // Give initial connection time to establish
    sleep(Duration::from_secs(2)).await;

    // Simulate publishing loop
    for i in 0..20 {
        println!("\n--- Attempt {} ---", i + 1);

        let payload = format!(
            r#"{{"temperature": {:.1}, "timestamp": {}}}"#,
            20.0 + (i as f64 * 0.5),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs()
        );

        match mqtt_cb.publish("sensors/temperature", &payload, QoS::AtLeastOnce).await {
            Ok(_) => println!("Publish succeeded"),
            Err(e) => println!("Publish failed: {}", e),
        }

        println!("Circuit Breaker State: {:?}", mqtt_cb.get_state().await);
        sleep(Duration::from_secs(2)).await;
    }

    sleep(Duration::from_secs(5)).await;
}
```

## Summary

The Circuit Breaker Pattern is an essential fault tolerance mechanism for MQTT applications that prevents cascading failures and enables graceful degradation when the broker becomes unavailable. By monitoring failure rates and automatically transitioning between Closed, Open, and Half-Open states, the pattern protects both the client application and the MQTT broker from being overwhelmed during outages.

**Key benefits include:**
- **Fast failure detection** - Stops attempting doomed operations immediately
- **Resource conservation** - Prevents wasted CPU, network, and connection resources
- **Automatic recovery** - Tests broker health and resumes normal operation when possible
- **Fallback support** - Enables alternative behaviors like local queuing or logging during outages
- **System stability** - Prevents retry storms that can delay broker recovery

Both implementations demonstrate core circuit breaker functionality with configurable thresholds, timeout periods, and state management. The C implementation uses manual state tracking and callbacks, while the Rust implementation leverages async/await patterns and Arc/Mutex for thread-safe state sharing. In production systems, you would extend these examples with persistent queuing, metrics collection, and integration with monitoring systems to create fully resilient MQTT architectures.