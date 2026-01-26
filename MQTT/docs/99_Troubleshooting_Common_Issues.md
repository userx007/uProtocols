# MQTT Troubleshooting: Common Issues and Solutions

## Overview

MQTT troubleshooting involves diagnosing and resolving issues related to connection stability, message delivery, and system performance. Common problems include broker connectivity failures, message loss, quality of service (QoS) violations, and performance bottlenecks. Effective troubleshooting requires understanding MQTT protocol mechanics, proper logging, and systematic debugging approaches.

## Common MQTT Issues

### 1. Connection Failures
Connection issues often stem from network problems, authentication errors, incorrect broker addresses, firewall restrictions, or SSL/TLS certificate problems.

### 2. Message Loss
Messages may be lost due to improper QoS settings, client disconnections without clean sessions, buffer overflows, or network instability.

### 3. Performance Problems
Performance degradation can result from high message volumes, inefficient topic structures, inadequate broker resources, or poor network conditions.

## C/C++ Implementation

Here's a comprehensive troubleshooting implementation using the Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <time.h>

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "TroubleshootClient"
#define TOPIC       "test/diagnostics"
#define QOS         1
#define TIMEOUT     10000L

// Troubleshooting statistics
typedef struct {
    int connection_attempts;
    int successful_connections;
    int failed_connections;
    int messages_sent;
    int messages_received;
    int messages_lost;
    time_t last_connection_time;
    time_t last_message_time;
} DiagnosticStats;

DiagnosticStats stats = {0};

// Connection lost callback with diagnostics
void connection_lost(void *context, char *cause) {
    printf("\n[ERROR] Connection lost\n");
    printf("  Cause: %s\n", cause ? cause : "Unknown");
    printf("  Last successful message: %ld seconds ago\n", 
           time(NULL) - stats.last_message_time);
    
    stats.failed_connections++;
    
    // Log connection statistics
    printf("\n[DIAGNOSTICS] Connection Statistics:\n");
    printf("  Total attempts: %d\n", stats.connection_attempts);
    printf("  Successful: %d\n", stats.successful_connections);
    printf("  Failed: %d\n", stats.failed_connections);
}

// Message delivery callback with tracking
void message_delivered(void *context, MQTTClient_deliveryToken token) {
    printf("[SUCCESS] Message with token %d delivered\n", token);
    stats.messages_sent++;
    stats.last_message_time = time(NULL);
}

// Message arrival callback with validation
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("[RECEIVED] Message on topic: %s\n", topicName);
    printf("  Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("  QoS: %d\n", message->qos);
    printf("  Retained: %d\n", message->retained);
    
    stats.messages_received++;
    stats.last_message_time = time(NULL);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Enhanced connection with detailed error checking
int connect_with_diagnostics(MQTTClient* client) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    stats.connection_attempts++;
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.reliable = 1;
    conn_opts.connectTimeout = 30;
    
    printf("\n[CONNECTING] Attempt #%d\n", stats.connection_attempts);
    printf("  Broker: %s\n", ADDRESS);
    printf("  Client ID: %s\n", CLIENTID);
    printf("  Keep Alive: %d seconds\n", conn_opts.keepAliveInterval);
    
    if ((rc = MQTTClient_connect(*client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[ERROR] Connection failed with code: %d\n", rc);
        
        // Detailed error diagnosis
        switch(rc) {
            case 1:
                printf("  Reason: Unacceptable protocol version\n");
                break;
            case 2:
                printf("  Reason: Identifier rejected\n");
                break;
            case 3:
                printf("  Reason: Server unavailable\n");
                printf("  Action: Check if broker is running\n");
                break;
            case 4:
                printf("  Reason: Bad username or password\n");
                break;
            case 5:
                printf("  Reason: Not authorized\n");
                break;
            default:
                printf("  Reason: Network or internal error\n");
                printf("  Action: Check network connectivity and firewall\n");
        }
        
        stats.failed_connections++;
        return rc;
    }
    
    stats.successful_connections++;
    stats.last_connection_time = time(NULL);
    printf("[SUCCESS] Connected successfully\n");
    
    return MQTTCLIENT_SUCCESS;
}

// Publish with error handling and retry logic
int publish_with_diagnostics(MQTTClient client, const char* topic, 
                             const char* payload, int qos) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    int retry_count = 0;
    const int MAX_RETRIES = 3;
    
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = qos;
    pubmsg.retained = 0;
    
    while (retry_count < MAX_RETRIES) {
        rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
        
        if (rc != MQTTCLIENT_SUCCESS) {
            printf("[ERROR] Publish failed (attempt %d/%d), code: %d\n", 
                   retry_count + 1, MAX_RETRIES, rc);
            retry_count++;
            
            if (retry_count < MAX_RETRIES) {
                printf("  Retrying in 2 seconds...\n");
                MQTTClient_yield();
                // Sleep for 2 seconds
            }
        } else {
            printf("[PUBLISHED] Waiting for delivery (token: %d)\n", token);
            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            
            if (rc != MQTTCLIENT_SUCCESS) {
                printf("[WARNING] Message delivery uncertain\n");
                stats.messages_lost++;
            }
            return rc;
        }
    }
    
    stats.messages_lost++;
    return rc;
}

// Network diagnostics
void check_network_health(MQTTClient client) {
    printf("\n[DIAGNOSTICS] Network Health Check\n");
    
    if (MQTTClient_isConnected(client)) {
        printf("  Status: Connected\n");
        printf("  Uptime: %ld seconds\n", 
               time(NULL) - stats.last_connection_time);
    } else {
        printf("  Status: Disconnected\n");
        printf("  Action: Attempting reconnection...\n");
    }
    
    printf("\n[STATISTICS] Message Statistics:\n");
    printf("  Sent: %d\n", stats.messages_sent);
    printf("  Received: %d\n", stats.messages_received);
    printf("  Lost: %d\n", stats.messages_lost);
    
    if (stats.messages_sent > 0) {
        float success_rate = ((float)(stats.messages_sent - stats.messages_lost) / 
                             stats.messages_sent) * 100;
        printf("  Success rate: %.2f%%\n", success_rate);
    }
}

int main() {
    MQTTClient client;
    int rc;
    
    printf("=== MQTT Troubleshooting Tool ===\n");
    
    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                     MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, message_delivered);
    
    // Attempt connection
    rc = connect_with_diagnostics(&client);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("\n[FATAL] Unable to establish connection. Exiting.\n");
        return EXIT_FAILURE;
    }
    
    // Subscribe to topic
    printf("\n[SUBSCRIBING] Topic: %s (QoS %d)\n", TOPIC, QOS);
    rc = MQTTClient_subscribe(client, TOPIC, QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("[ERROR] Subscription failed with code: %d\n", rc);
    }
    
    // Test message publishing
    printf("\n[TESTING] Publishing test messages...\n");
    for (int i = 0; i < 5; i++) {
        char payload[100];
        snprintf(payload, sizeof(payload), "Test message #%d", i + 1);
        
        rc = publish_with_diagnostics(client, TOPIC, payload, QOS);
        if (rc != MQTTCLIENT_SUCCESS) {
            printf("[WARNING] Message #%d failed\n", i + 1);
        }
        
        MQTTClient_yield();
    }
    
    // Wait for messages
    printf("\n[LISTENING] Waiting for messages (10 seconds)...\n");
    for (int i = 0; i < 10; i++) {
        MQTTClient_yield();
        // Sleep 1 second between checks
    }
    
    // Final diagnostics
    check_network_health(client);
    
    // Cleanup
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    
    printf("\n=== Session Complete ===\n");
    return EXIT_SUCCESS;
}
```

## Rust Implementation

Here's a Rust implementation using the `paho-mqtt` crate with comprehensive error handling:

```rust
use paho_mqtt as mqtt;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};
use std::thread;

// Diagnostic statistics structure
#[derive(Debug, Clone)]
struct DiagnosticStats {
    connection_attempts: u32,
    successful_connections: u32,
    failed_connections: u32,
    messages_sent: u32,
    messages_received: u32,
    messages_lost: u32,
    last_connection_time: Option<Instant>,
    last_message_time: Option<Instant>,
}

impl DiagnosticStats {
    fn new() -> Self {
        DiagnosticStats {
            connection_attempts: 0,
            successful_connections: 0,
            failed_connections: 0,
            messages_sent: 0,
            messages_received: 0,
            messages_lost: 0,
            last_connection_time: None,
            last_message_time: None,
        }
    }

    fn print_summary(&self) {
        println!("\n[DIAGNOSTICS] Connection Statistics:");
        println!("  Total attempts: {}", self.connection_attempts);
        println!("  Successful: {}", self.successful_connections);
        println!("  Failed: {}", self.failed_connections);
        
        println!("\n[STATISTICS] Message Statistics:");
        println!("  Sent: {}", self.messages_sent);
        println!("  Received: {}", self.messages_received);
        println!("  Lost: {}", self.messages_lost);
        
        if self.messages_sent > 0 {
            let success_rate = ((self.messages_sent - self.messages_lost) as f64 
                              / self.messages_sent as f64) * 100.0;
            println!("  Success rate: {:.2}%", success_rate);
        }
        
        if let Some(last_msg) = self.last_message_time {
            println!("  Time since last message: {:?}", last_msg.elapsed());
        }
    }
}

// Enhanced error handling
fn diagnose_connection_error(err: &mqtt::Error) {
    eprintln!("[ERROR] Connection failed: {}", err);
    
    match err {
        mqtt::Error::PahoDescr(code, desc) => {
            eprintln!("  Error code: {}", code);
            eprintln!("  Description: {}", desc);
            
            match *code {
                -1 => eprintln!("  Action: Check network connectivity"),
                1 => eprintln!("  Action: Check MQTT protocol version"),
                2 => eprintln!("  Action: Verify client ID is valid"),
                3 => eprintln!("  Action: Ensure broker is running and accessible"),
                4 => eprintln!("  Action: Check username and password"),
                5 => eprintln!("  Action: Verify authentication credentials"),
                _ => eprintln!("  Action: Review broker logs for details"),
            }
        }
        mqtt::Error::Timeout => {
            eprintln!("  Reason: Connection timeout");
            eprintln!("  Action: Check network latency and broker responsiveness");
        }
        _ => {
            eprintln!("  Action: Check broker configuration and network");
        }
    }
}

// Publish with retry logic
fn publish_with_retry(
    client: &mqtt::Client,
    topic: &str,
    payload: &str,
    qos: i32,
    stats: Arc<Mutex<DiagnosticStats>>,
) -> Result<(), mqtt::Error> {
    const MAX_RETRIES: u32 = 3;
    let mut retry_count = 0;
    
    let msg = mqtt::MessageBuilder::new()
        .topic(topic)
        .payload(payload)
        .qos(qos)
        .finalize();
    
    loop {
        match client.publish(msg.clone()) {
            Ok(token) => {
                println!("[PUBLISHED] Message sent, waiting for confirmation...");
                
                match token.wait_for(Duration::from_secs(10)) {
                    Ok(_) => {
                        println!("[SUCCESS] Message delivered");
                        let mut stats = stats.lock().unwrap();
                        stats.messages_sent += 1;
                        stats.last_message_time = Some(Instant::now());
                        return Ok(());
                    }
                    Err(e) => {
                        eprintln!("[WARNING] Delivery confirmation failed: {}", e);
                        let mut stats = stats.lock().unwrap();
                        stats.messages_lost += 1;
                        return Err(e);
                    }
                }
            }
            Err(e) => {
                retry_count += 1;
                eprintln!("[ERROR] Publish failed (attempt {}/{}): {}", 
                         retry_count, MAX_RETRIES, e);
                
                if retry_count >= MAX_RETRIES {
                    let mut stats = stats.lock().unwrap();
                    stats.messages_lost += 1;
                    return Err(e);
                }
                
                println!("  Retrying in 2 seconds...");
                thread::sleep(Duration::from_secs(2));
            }
        }
    }
}

// Connection with diagnostics
fn connect_with_diagnostics(
    client: &mqtt::Client,
    stats: Arc<Mutex<DiagnosticStats>>,
) -> Result<(), mqtt::Error> {
    {
        let mut stats = stats.lock().unwrap();
        stats.connection_attempts += 1;
    }
    
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .connect_timeout(Duration::from_secs(30))
        .automatic_reconnect(Duration::from_secs(1), Duration::from_secs(60))
        .finalize();
    
    println!("\n[CONNECTING] Attempting connection...");
    println!("  Keep alive: 20 seconds");
    println!("  Clean session: true");
    
    match client.connect(conn_opts) {
        Ok(_) => {
            println!("[SUCCESS] Connected successfully");
            let mut stats = stats.lock().unwrap();
            stats.successful_connections += 1;
            stats.last_connection_time = Some(Instant::now());
            Ok(())
        }
        Err(e) => {
            diagnose_connection_error(&e);
            let mut stats = stats.lock().unwrap();
            stats.failed_connections += 1;
            Err(e)
        }
    }
}

// Network health check
fn check_network_health(client: &mqtt::Client, stats: Arc<Mutex<DiagnosticStats>>) {
    println!("\n[DIAGNOSTICS] Network Health Check");
    
    if client.is_connected() {
        println!("  Status: Connected");
        let stats = stats.lock().unwrap();
        if let Some(conn_time) = stats.last_connection_time {
            println!("  Uptime: {:?}", conn_time.elapsed());
        }
    } else {
        println!("  Status: Disconnected");
        println!("  Action: Connection lost, attempting automatic reconnection");
    }
    
    let stats = stats.lock().unwrap();
    stats.print_summary();
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== MQTT Troubleshooting Tool (Rust) ===\n");
    
    let stats = Arc::new(Mutex::new(DiagnosticStats::new()));
    let stats_clone = Arc::clone(&stats);
    
    // Create client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri("tcp://localhost:1883")
        .client_id("RustTroubleshootClient")
        .finalize();
    
    let client = mqtt::Client::new(create_opts)?;
    
    // Set up callbacks
    let rx = client.start_consuming();
    
    let stats_clone2 = Arc::clone(&stats);
    client.set_connection_lost_callback(move |cli: &mqtt::Client| {
        eprintln!("\n[ERROR] Connection lost!");
        let mut stats = stats_clone2.lock().unwrap();
        stats.failed_connections += 1;
    });
    
    // Connect with diagnostics
    connect_with_diagnostics(&client, Arc::clone(&stats))?;
    
    // Subscribe
    println!("\n[SUBSCRIBING] Topic: test/diagnostics (QoS 1)");
    client.subscribe("test/diagnostics", 1)?;
    
    // Publish test messages
    println!("\n[TESTING] Publishing test messages...");
    for i in 1..=5 {
        let payload = format!("Test message #{}", i);
        
        if let Err(e) = publish_with_retry(
            &client, 
            "test/diagnostics", 
            &payload, 
            1,
            Arc::clone(&stats)
        ) {
            eprintln!("[WARNING] Message #{} failed: {}", i, e);
        }
        
        thread::sleep(Duration::from_millis(500));
    }
    
    // Listen for messages
    println!("\n[LISTENING] Waiting for messages (10 seconds)...");
    let timeout = Duration::from_secs(10);
    let start = Instant::now();
    
    while start.elapsed() < timeout {
        if let Ok(Some(msg)) = rx.recv_timeout(Duration::from_secs(1)) {
            println!("[RECEIVED] Message on topic: {}", msg.topic());
            println!("  Payload: {}", msg.payload_str());
            println!("  QoS: {}", msg.qos());
            
            let mut stats = stats.lock().unwrap();
            stats.messages_received += 1;
            stats.last_message_time = Some(Instant::now());
        }
    }
    
    // Final diagnostics
    check_network_health(&client, Arc::clone(&stats));
    
    // Cleanup
    client.disconnect(None)?;
    println!("\n=== Session Complete ===");
    
    Ok(())
}
```

## Summary

MQTT troubleshooting requires systematic approaches to identify and resolve connection, messaging, and performance issues. Key troubleshooting strategies include:

**Connection Issues**: Implement detailed logging of connection attempts, error codes, and network states. Use callbacks to detect connection loss and implement automatic reconnection with exponential backoff.

**Message Loss**: Track message delivery tokens, implement QoS appropriately (QoS 1 for at-least-once, QoS 2 for exactly-once), use persistent sessions when needed, and monitor delivery confirmations.

**Performance Problems**: Monitor message throughput, connection latency, and broker resource usage. Implement appropriate keep-alive intervals, optimize topic structures, and use batching for high-volume scenarios.

**Best Practices**: Always implement comprehensive error handling, maintain diagnostic statistics, use timeouts for all operations, implement retry logic with backoff, log all significant events, and test under various network conditions including disconnections and high latency.

Both C/C++ and Rust implementations demonstrate robust error handling, detailed diagnostics logging, automatic retry mechanisms, and comprehensive statistics tracking to effectively troubleshoot MQTT applications in production environments.