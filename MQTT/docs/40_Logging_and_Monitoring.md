# MQTT Logging and Monitoring: A Comprehensive Guide

## Overview

Logging and monitoring are critical components of production MQTT systems. They provide visibility into system behavior, help diagnose issues, track performance metrics, and ensure reliability. Proper logging captures message flows, connection events, and errors, while monitoring tracks system health through metrics like message rates, latency, and resource utilization.

## Key Concepts

### 1. **Logging Levels and Categories**
- **Connection Events**: Client connect/disconnect, authentication
- **Message Flow**: Publish/subscribe operations, QoS handling
- **Errors and Warnings**: Failed operations, timeout events
- **Performance Metrics**: Message throughput, latency measurements
- **Security Audits**: Authentication attempts, authorization failures

### 2. **Monitoring Metrics**
- **Throughput**: Messages per second, bytes transferred
- **Latency**: End-to-end message delivery time
- **Connection Health**: Active connections, reconnection rates
- **Queue Depths**: Pending messages, backlog indicators
- **Resource Usage**: CPU, memory, network bandwidth

### 3. **Best Practices**
- Use structured logging (JSON) for easier parsing
- Include correlation IDs for tracing message flows
- Implement log rotation and retention policies
- Set up alerting for critical conditions
- Balance verbosity with performance impact

## Code Examples

### C/C++ Implementation with Paho MQTT

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MQTTClient.h>

// Logging levels
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

// Metrics structure
typedef struct {
    unsigned long messages_sent;
    unsigned long messages_received;
    unsigned long connection_failures;
    unsigned long reconnection_count;
    time_t last_message_time;
    double avg_latency_ms;
} MQTTMetrics;

// Global metrics
static MQTTMetrics g_metrics = {0};

// Logging function with timestamp
void mqtt_log(LogLevel level, const char* component, const char* format, ...) {
    time_t now;
    char timestamp[64];
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] [%s] [%s] ", timestamp, level_str[level], component);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}

// Message arrived callback with logging
int message_arrived(void* context, char* topicName, int topicLen, 
                    MQTTClient_message* message) {
    struct timespec arrival_time;
    clock_gettime(CLOCK_MONOTONIC, &arrival_time);
    
    g_metrics.messages_received++;
    g_metrics.last_message_time = time(NULL);
    
    mqtt_log(LOG_INFO, "MQTT_CLIENT", 
             "Message received on topic '%s' (QoS=%d, retained=%d, size=%d bytes)",
             topicName, message->qos, message->retained, message->payloadlen);
    
    mqtt_log(LOG_DEBUG, "MQTT_CLIENT", "Payload: %.*s", 
             message->payloadlen, (char*)message->payload);
    
    // Calculate latency if timestamp is embedded in message
    // (simplified example)
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Connection lost callback with logging
void connection_lost(void* context, char* cause) {
    g_metrics.connection_failures++;
    
    mqtt_log(LOG_ERROR, "MQTT_CLIENT", 
             "Connection lost: %s (total failures: %lu)",
             cause ? cause : "unknown reason", 
             g_metrics.connection_failures);
}

// Delivery complete callback
void delivery_complete(void* context, MQTTClient_deliveryToken token) {
    mqtt_log(LOG_DEBUG, "MQTT_CLIENT", 
             "Message delivery confirmed (token: %d)", token);
}

// Publish with metrics tracking
int mqtt_publish_with_logging(MQTTClient client, const char* topic, 
                               const char* payload, int qos) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = qos;
    pubmsg.retained = 0;
    
    mqtt_log(LOG_DEBUG, "MQTT_CLIENT", 
             "Publishing to topic '%s' (QoS=%d, size=%d bytes)",
             topic, qos, pubmsg.payloadlen);
    
    rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        mqtt_log(LOG_ERROR, "MQTT_CLIENT", 
                 "Failed to publish message: error code %d", rc);
        return rc;
    }
    
    g_metrics.messages_sent++;
    
    mqtt_log(LOG_INFO, "MQTT_CLIENT", 
             "Message published successfully (token: %d, total sent: %lu)",
             token, g_metrics.messages_sent);
    
    // Wait for completion (for QoS 1 and 2)
    if (qos > 0) {
        rc = MQTTClient_waitForCompletion(client, token, 5000);
        if (rc != MQTTCLIENT_SUCCESS) {
            mqtt_log(LOG_WARNING, "MQTT_CLIENT", 
                     "Message delivery not confirmed within timeout");
        }
    }
    
    return rc;
}

// Print metrics report
void print_metrics_report() {
    mqtt_log(LOG_INFO, "METRICS", "=== MQTT Metrics Report ===");
    mqtt_log(LOG_INFO, "METRICS", "Messages Sent: %lu", g_metrics.messages_sent);
    mqtt_log(LOG_INFO, "METRICS", "Messages Received: %lu", g_metrics.messages_received);
    mqtt_log(LOG_INFO, "METRICS", "Connection Failures: %lu", g_metrics.connection_failures);
    mqtt_log(LOG_INFO, "METRICS", "Reconnections: %lu", g_metrics.reconnection_count);
    
    if (g_metrics.last_message_time > 0) {
        time_t now = time(NULL);
        mqtt_log(LOG_INFO, "METRICS", "Time Since Last Message: %ld seconds", 
                 now - g_metrics.last_message_time);
    }
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    mqtt_log(LOG_INFO, "MAIN", "Starting MQTT client with logging");
    
    // Create client
    rc = MQTTClient_create(&client, "tcp://localhost:1883", "LoggingClient",
                           MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        mqtt_log(LOG_ERROR, "MAIN", "Failed to create client: error %d", rc);
        return EXIT_FAILURE;
    }
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, delivery_complete);
    
    // Connect
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    mqtt_log(LOG_INFO, "MAIN", "Connecting to broker...");
    rc = MQTTClient_connect(client, &conn_opts);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        mqtt_log(LOG_ERROR, "MAIN", "Connection failed: error %d", rc);
        return EXIT_FAILURE;
    }
    
    mqtt_log(LOG_INFO, "MAIN", "Successfully connected to broker");
    
    // Subscribe
    rc = MQTTClient_subscribe(client, "test/logging", 1);
    if (rc != MQTTCLIENT_SUCCESS) {
        mqtt_log(LOG_ERROR, "MAIN", "Subscription failed: error %d", rc);
    } else {
        mqtt_log(LOG_INFO, "MAIN", "Subscribed to topic 'test/logging'");
    }
    
    // Publish some test messages
    for (int i = 0; i < 5; i++) {
        char payload[64];
        snprintf(payload, sizeof(payload), "Test message %d", i);
        mqtt_publish_with_logging(client, "test/logging", payload, 1);
        sleep(1);
    }
    
    // Print metrics
    print_metrics_report();
    
    // Cleanup
    mqtt_log(LOG_INFO, "MAIN", "Disconnecting and cleaning up");
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return EXIT_SUCCESS;
}
```

### Rust Implementation with Paho MQTT

```rust
use paho_mqtt as mqtt;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};
use chrono::{DateTime, Local};
use serde::{Deserialize, Serialize};

// Logging levels
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum LogLevel {
    Debug,
    Info,
    Warning,
    Error,
}

// Metrics structure
#[derive(Debug, Clone, Serialize, Deserialize)]
struct MqttMetrics {
    messages_sent: u64,
    messages_received: u64,
    connection_failures: u64,
    reconnection_count: u64,
    total_bytes_sent: u64,
    total_bytes_received: u64,
    last_message_time: Option<SystemTime>,
    latency_samples: Vec<f64>,
}

impl MqttMetrics {
    fn new() -> Self {
        Self {
            messages_sent: 0,
            messages_received: 0,
            connection_failures: 0,
            reconnection_count: 0,
            total_bytes_sent: 0,
            total_bytes_received: 0,
            last_message_time: None,
            latency_samples: Vec::new(),
        }
    }
    
    fn avg_latency(&self) -> Option<f64> {
        if self.latency_samples.is_empty() {
            None
        } else {
            let sum: f64 = self.latency_samples.iter().sum();
            Some(sum / self.latency_samples.len() as f64)
        }
    }
    
    fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_default()
    }
}

// Logger struct
struct MqttLogger {
    log_file: Option<std::fs::File>,
    min_level: LogLevel,
}

impl MqttLogger {
    fn new(min_level: LogLevel, log_file_path: Option<&str>) -> Self {
        let log_file = log_file_path.and_then(|path| {
            std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(path)
                .ok()
        });
        
        Self { log_file, min_level }
    }
    
    fn log(&mut self, level: LogLevel, component: &str, message: &str) {
        if level as u8 >= self.min_level as u8 {
            let now: DateTime<Local> = Local::now();
            let level_str = match level {
                LogLevel::Debug => "DEBUG",
                LogLevel::Info => "INFO",
                LogLevel::Warning => "WARN",
                LogLevel::Error => "ERROR",
            };
            
            let log_entry = format!(
                "[{}] [{}] [{}] {}",
                now.format("%Y-%m-%d %H:%M:%S%.3f"),
                level_str,
                component,
                message
            );
            
            println!("{}", log_entry);
            
            // Write to file if configured
            if let Some(ref mut file) = self.log_file {
                use std::io::Write;
                let _ = writeln!(file, "{}", log_entry);
            }
        }
    }
}

// Main MQTT client with logging
struct LoggedMqttClient {
    client: mqtt::Client,
    logger: Arc<Mutex<MqttLogger>>,
    metrics: Arc<Mutex<MqttMetrics>>,
}

impl LoggedMqttClient {
    fn new(broker_url: &str, client_id: &str, log_level: LogLevel) -> mqtt::Result<Self> {
        let mut logger = MqttLogger::new(log_level, Some("mqtt_client.log"));
        logger.log(LogLevel::Info, "CLIENT", &format!("Creating MQTT client '{}'", client_id));
        
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker_url)
            .client_id(client_id)
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        
        let logger = Arc::new(Mutex::new(logger));
        let metrics = Arc::new(Mutex::new(MqttMetrics::new()));
        
        Ok(Self {
            client,
            logger,
            metrics,
        })
    }
    
    fn connect(&self, clean_session: bool) -> mqtt::Result<()> {
        self.logger.lock().unwrap().log(
            LogLevel::Info,
            "CLIENT",
            &format!("Connecting to broker (clean_session={})", clean_session),
        );
        
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(clean_session)
            .finalize();
        
        match self.client.connect(conn_opts) {
            Ok(_) => {
                self.logger.lock().unwrap().log(
                    LogLevel::Info,
                    "CLIENT",
                    "Successfully connected to broker",
                );
                Ok(())
            }
            Err(e) => {
                self.metrics.lock().unwrap().connection_failures += 1;
                self.logger.lock().unwrap().log(
                    LogLevel::Error,
                    "CLIENT",
                    &format!("Connection failed: {:?}", e),
                );
                Err(e)
            }
        }
    }
    
    fn subscribe(&self, topic: &str, qos: i32) -> mqtt::Result<()> {
        self.logger.lock().unwrap().log(
            LogLevel::Info,
            "CLIENT",
            &format!("Subscribing to topic '{}' with QoS {}", topic, qos),
        );
        
        match self.client.subscribe(topic, qos) {
            Ok(_) => {
                self.logger.lock().unwrap().log(
                    LogLevel::Info,
                    "CLIENT",
                    &format!("Successfully subscribed to '{}'", topic),
                );
                Ok(())
            }
            Err(e) => {
                self.logger.lock().unwrap().log(
                    LogLevel::Error,
                    "CLIENT",
                    &format!("Subscription failed: {:?}", e),
                );
                Err(e)
            }
        }
    }
    
    fn publish(&self, topic: &str, payload: &[u8], qos: i32, retained: bool) -> mqtt::Result<()> {
        let start_time = Instant::now();
        
        self.logger.lock().unwrap().log(
            LogLevel::Debug,
            "CLIENT",
            &format!(
                "Publishing to '{}' (QoS={}, retained={}, {} bytes)",
                topic, qos, retained, payload.len()
            ),
        );
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(qos)
            .retained(retained)
            .finalize();
        
        match self.client.publish(msg) {
            Ok(_) => {
                let elapsed = start_time.elapsed();
                let mut metrics = self.metrics.lock().unwrap();
                metrics.messages_sent += 1;
                metrics.total_bytes_sent += payload.len() as u64;
                metrics.last_message_time = Some(SystemTime::now());
                metrics.latency_samples.push(elapsed.as_secs_f64() * 1000.0);
                
                self.logger.lock().unwrap().log(
                    LogLevel::Info,
                    "CLIENT",
                    &format!(
                        "Published successfully (took {:.2}ms, total sent: {})",
                        elapsed.as_secs_f64() * 1000.0,
                        metrics.messages_sent
                    ),
                );
                Ok(())
            }
            Err(e) => {
                self.logger.lock().unwrap().log(
                    LogLevel::Error,
                    "CLIENT",
                    &format!("Publish failed: {:?}", e),
                );
                Err(e)
            }
        }
    }
    
    fn start_consuming(&self) {
        let logger = Arc::clone(&self.logger);
        let metrics = Arc::clone(&self.metrics);
        let rx = self.client.start_consuming();
        
        std::thread::spawn(move || {
            for msg_opt in rx.iter() {
                if let Some(msg) = msg_opt {
                    let mut metrics = metrics.lock().unwrap();
                    metrics.messages_received += 1;
                    metrics.total_bytes_received += msg.payload().len() as u64;
                    metrics.last_message_time = Some(SystemTime::now());
                    
                    logger.lock().unwrap().log(
                        LogLevel::Info,
                        "CONSUMER",
                        &format!(
                            "Received message on '{}' (QoS={}, {} bytes)",
                            msg.topic(),
                            msg.qos(),
                            msg.payload().len()
                        ),
                    );
                    
                    logger.lock().unwrap().log(
                        LogLevel::Debug,
                        "CONSUMER",
                        &format!("Payload: {:?}", String::from_utf8_lossy(msg.payload())),
                    );
                } else {
                    logger.lock().unwrap().log(
                        LogLevel::Warning,
                        "CONSUMER",
                        "Connection lost, waiting for reconnection...",
                    );
                    
                    metrics.lock().unwrap().connection_failures += 1;
                }
            }
        });
    }
    
    fn print_metrics_report(&self) {
        let metrics = self.metrics.lock().unwrap();
        let mut logger = self.logger.lock().unwrap();
        
        logger.log(LogLevel::Info, "METRICS", "=== MQTT Metrics Report ===");
        logger.log(LogLevel::Info, "METRICS", &format!("Messages Sent: {}", metrics.messages_sent));
        logger.log(LogLevel::Info, "METRICS", &format!("Messages Received: {}", metrics.messages_received));
        logger.log(LogLevel::Info, "METRICS", &format!("Total Bytes Sent: {}", metrics.total_bytes_sent));
        logger.log(LogLevel::Info, "METRICS", &format!("Total Bytes Received: {}", metrics.total_bytes_received));
        logger.log(LogLevel::Info, "METRICS", &format!("Connection Failures: {}", metrics.connection_failures));
        
        if let Some(avg) = metrics.avg_latency() {
            logger.log(LogLevel::Info, "METRICS", &format!("Average Latency: {:.2}ms", avg));
        }
        
        if let Some(last_time) = metrics.last_message_time {
            if let Ok(duration) = SystemTime::now().duration_since(last_time) {
                logger.log(
                    LogLevel::Info,
                    "METRICS",
                    &format!("Time Since Last Message: {}s", duration.as_secs()),
                );
            }
        }
        
        // Export metrics as JSON
        logger.log(LogLevel::Debug, "METRICS", &format!("JSON Export:\n{}", metrics.to_json()));
    }
    
    fn disconnect(&self) -> mqtt::Result<()> {
        self.logger.lock().unwrap().log(LogLevel::Info, "CLIENT", "Disconnecting from broker");
        self.client.disconnect(None)
    }
}

fn main() -> mqtt::Result<()> {
    let client = LoggedMqttClient::new(
        "tcp://localhost:1883",
        "RustLoggingClient",
        LogLevel::Debug,
    )?;
    
    client.connect(true)?;
    client.subscribe("test/logging", 1)?;
    
    // Start consuming messages in background
    client.start_consuming();
    
    // Publish test messages
    for i in 0..5 {
        let payload = format!("Test message {}", i);
        client.publish("test/logging", payload.as_bytes(), 1, false)?;
        std::thread::sleep(Duration::from_secs(1));
    }
    
    // Wait for messages to arrive
    std::thread::sleep(Duration::from_secs(2));
    
    // Print metrics report
    client.print_metrics_report();
    
    client.disconnect()?;
    
    Ok(())
}
```

## Summary

**Logging and monitoring are essential for production MQTT systems**, providing visibility into operations, performance, and reliability. Effective logging captures connection events, message flows, and errors with appropriate detail levels, while monitoring tracks key metrics like throughput, latency, and resource utilization.

**Key implementation points:**
- **Structured logging** with timestamps, levels, and components enables easier analysis
- **Metrics collection** tracks operational health through counters and measurements
- **Performance impact** must be balanced—verbose logging can affect throughput
- **Correlation IDs** help trace message flows through distributed systems
- **Alerting mechanisms** notify operators of critical conditions

Both C/C++ and Rust implementations demonstrate practical approaches to instrumentation, with Rust offering stronger type safety and thread-safe shared state management through `Arc<Mutex<>>`, while C provides lower-level control with careful manual synchronization. Production systems should integrate with centralized logging platforms (ELK stack, Grafana) and implement log rotation, retention policies, and automated alerting for comprehensive observability.