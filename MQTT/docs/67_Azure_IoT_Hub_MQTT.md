# Azure IoT Hub MQTT: Detailed Description

Azure IoT Hub MQTT is Microsoft's cloud-based IoT platform that supports MQTT protocol for bidirectional communication between IoT devices and the cloud. It provides enterprise-grade security, device management, and telemetry capabilities while leveraging the lightweight MQTT protocol for efficient device-to-cloud and cloud-to-device messaging.

## Core Concepts

**Authentication & Security:**
- Uses SAS (Shared Access Signature) tokens or X.509 certificates
- TLS/SSL encryption required on port 8883
- Device identity management through Azure IoT Hub registry

**MQTT Topic Structure:**
- **Telemetry (D2C)**: `devices/{device_id}/messages/events/`
- **Commands (C2D)**: `devices/{device_id}/messages/devicebound/#`
- **Direct Methods**: Request/response pattern via specific topics
- **Device Twins**: `$iothub/twin/PATCH/properties/reported/?$rid={request_id}`

**Key Features:**
- Bi-directional messaging
- Device twin synchronization (desired/reported properties)
- Direct methods for immediate device control
- File upload capabilities
- Message routing and integration with Azure services

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

// Azure IoT Hub configuration
#define HUB_HOSTNAME "your-hub-name.azure-devices.net"
#define DEVICE_ID "your-device-id"
#define SAS_TOKEN "SharedAccessSignature sr=your-hub-name.azure-devices.net..."

#define CLIENT_ID DEVICE_ID
#define USERNAME HUB_HOSTNAME "/" DEVICE_ID "/?api-version=2021-04-12"
#define PASSWORD SAS_TOKEN

#define QOS 1
#define TIMEOUT 10000L

// MQTT Topics
#define TELEMETRY_TOPIC "devices/" DEVICE_ID "/messages/events/"
#define C2D_TOPIC "devices/" DEVICE_ID "/messages/devicebound/#"
#define TWIN_REPORTED_TOPIC "$iothub/twin/PATCH/properties/reported/?$rid=1"
#define TWIN_DESIRED_TOPIC "$iothub/twin/PATCH/properties/desired/#"

// Message arrived callback
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Handle cloud-to-device messages
    if (strstr(topicName, "messages/devicebound") != NULL) {
        printf("Cloud-to-Device command received\n");
        // Process command logic here
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Connection lost callback
void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause ? cause : "unknown");
}

// Send telemetry data to Azure IoT Hub
int send_telemetry(MQTTClient client, const char *json_data) {
    MQTTClient_message message = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    message.payload = (void*)json_data;
    message.payloadlen = strlen(json_data);
    message.qos = QOS;
    message.retained = 0;
    
    int rc = MQTTClient_publishMessage(client, TELEMETRY_TOPIC, &message, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to send telemetry: %d\n", rc);
        return rc;
    }
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Telemetry sent, delivery token: %d\n", token);
    return rc;
}

// Update device twin reported properties
int update_device_twin(MQTTClient client, const char *properties) {
    MQTTClient_message message = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    message.payload = (void*)properties;
    message.payloadlen = strlen(properties);
    message.qos = QOS;
    message.retained = 0;
    
    int rc = MQTTClient_publishMessage(client, TWIN_REPORTED_TOPIC, &message, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to update device twin: %d\n", rc);
        return rc;
    }
    
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Device twin updated\n");
    return rc;
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    int rc;
    
    // Create MQTT client
    char address[256];
    snprintf(address, sizeof(address), "ssl://%s:8883", HUB_HOSTNAME);
    
    MQTTClient_create(&client, address, CLIENT_ID, 
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);
    
    // Configure SSL/TLS
    ssl_opts.enableServerCertAuth = 1;
    ssl_opts.trustStore = NULL; // Use system certificates
    
    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = USERNAME;
    conn_opts.password = PASSWORD;
    conn_opts.ssl = &ssl_opts;
    conn_opts.MQTTVersion = MQTTVERSION_3_1_1;
    
    // Connect to Azure IoT Hub
    printf("Connecting to Azure IoT Hub: %s\n", HUB_HOSTNAME);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    printf("Connected successfully\n");
    
    // Subscribe to cloud-to-device messages
    MQTTClient_subscribe(client, C2D_TOPIC, QOS);
    printf("Subscribed to C2D messages\n");
    
    // Subscribe to device twin desired properties
    MQTTClient_subscribe(client, TWIN_DESIRED_TOPIC, QOS);
    printf("Subscribed to device twin updates\n");
    
    // Send telemetry data
    char telemetry[256];
    for (int i = 0; i < 5; i++) {
        snprintf(telemetry, sizeof(telemetry), 
                "{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":%d}",
                20.0 + i, 60.0 + i * 2, (int)time(NULL));
        
        send_telemetry(client, telemetry);
        sleep(5);
    }
    
    // Update device twin
    const char *twin_properties = "{\"firmwareVersion\":\"1.0.5\",\"batteryLevel\":85}";
    update_device_twin(client, twin_properties);
    
    // Keep connection alive to receive messages
    printf("Waiting for cloud-to-device messages (press Ctrl+C to exit)...\n");
    while (1) {
        sleep(1);
    }
    
    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

---

## Rust Implementation

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet, TlsConfiguration};
use tokio::time::{sleep, Duration};
use serde_json::json;
use std::error::Error;

// Azure IoT Hub configuration
const HUB_HOSTNAME: &str = "your-hub-name.azure-devices.net";
const DEVICE_ID: &str = "your-device-id";
const SAS_TOKEN: &str = "SharedAccessSignature sr=your-hub-name.azure-devices.net...";

// MQTT Topics
const TELEMETRY_TOPIC: &str = "devices/your-device-id/messages/events/";
const C2D_TOPIC: &str = "devices/your-device-id/messages/devicebound/#";
const TWIN_REPORTED_TOPIC: &str = "$iothub/twin/PATCH/properties/reported/?$rid=";
const TWIN_DESIRED_TOPIC: &str = "$iothub/twin/PATCH/properties/desired/#";

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new(DEVICE_ID, HUB_HOSTNAME, 8883);
    
    // Set credentials
    let username = format!("{}/?api-version=2021-04-12", HUB_HOSTNAME);
    mqtt_options.set_credentials(username, SAS_TOKEN);
    
    // Configure connection parameters
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(true);
    
    // Enable TLS
    let tls_config = TlsConfiguration::default();
    mqtt_options.set_transport(rumqttc::Transport::Tls(tls_config));
    
    // Create MQTT client
    let (client, mut event_loop) = AsyncClient::new(mqtt_options, 10);
    
    println!("Connecting to Azure IoT Hub: {}", HUB_HOSTNAME);
    
    // Spawn task to handle incoming messages
    let client_clone = client.clone();
    tokio::spawn(async move {
        handle_events(&mut event_loop, &client_clone).await;
    });
    
    // Wait for connection
    sleep(Duration::from_secs(2)).await;
    
    // Subscribe to cloud-to-device messages
    client.subscribe(C2D_TOPIC, QoS::AtLeastOnce).await?;
    println!("Subscribed to C2D messages");
    
    // Subscribe to device twin desired properties
    client.subscribe(TWIN_DESIRED_TOPIC, QoS::AtLeastOnce).await?;
    println!("Subscribed to device twin updates");
    
    // Send telemetry data
    send_telemetry_loop(&client).await?;
    
    Ok(())
}

async fn handle_events(event_loop: &mut rumqttc::EventLoop, client: &AsyncClient) {
    loop {
        match event_loop.poll().await {
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Connected to Azure IoT Hub successfully");
            }
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let topic = publish.topic.as_str();
                let payload = String::from_utf8_lossy(&publish.payload);
                
                println!("Message received on topic: {}", topic);
                println!("Payload: {}", payload);
                
                // Handle cloud-to-device messages
                if topic.contains("messages/devicebound") {
                    handle_c2d_message(&payload, client).await;
                }
                
                // Handle device twin desired properties
                if topic.contains("twin/PATCH/properties/desired") {
                    handle_twin_desired(&payload, client).await;
                }
            }
            Ok(Event::Incoming(Packet::SubAck(_))) => {
                println!("Subscription acknowledged");
            }
            Ok(Event::Outgoing(_)) => {
                // Outgoing event (publish, subscribe, etc.)
            }
            Err(e) => {
                eprintln!("Connection error: {}", e);
                sleep(Duration::from_secs(5)).await;
            }
            _ => {}
        }
    }
}

async fn send_telemetry_loop(client: &AsyncClient) -> Result<(), Box<dyn Error>> {
    let mut counter = 0;
    
    loop {
        // Create telemetry JSON
        let telemetry = json!({
            "temperature": 20.0 + counter as f64,
            "humidity": 60.0 + counter as f64 * 2.0,
            "timestamp": chrono::Utc::now().timestamp(),
            "deviceId": DEVICE_ID
        });
        
        let payload = telemetry.to_string();
        
        // Publish telemetry
        client
            .publish(TELEMETRY_TOPIC, QoS::AtLeastOnce, false, payload.as_bytes())
            .await?;
        
        println!("Telemetry sent: {}", payload);
        
        // Update device twin every 10 messages
        if counter % 10 == 0 {
            update_device_twin(client, counter).await?;
        }
        
        counter += 1;
        sleep(Duration::from_secs(5)).await;
    }
}

async fn update_device_twin(client: &AsyncClient, counter: u32) -> Result<(), Box<dyn Error>> {
    let twin_properties = json!({
        "firmwareVersion": "1.0.5",
        "batteryLevel": 85 - (counter % 20),
        "lastUpdate": chrono::Utc::now().to_rfc3339()
    });
    
    let topic = format!("{}1", TWIN_REPORTED_TOPIC);
    let payload = twin_properties.to_string();
    
    client
        .publish(&topic, QoS::AtLeastOnce, false, payload.as_bytes())
        .await?;
    
    println!("Device twin updated: {}", payload);
    Ok(())
}

async fn handle_c2d_message(payload: &str, _client: &AsyncClient) {
    println!("Processing cloud-to-device command: {}", payload);
    
    // Parse and execute command
    match serde_json::from_str::<serde_json::Value>(payload) {
        Ok(json) => {
            if let Some(command) = json.get("command") {
                println!("Executing command: {}", command);
                // Add your command execution logic here
            }
        }
        Err(e) => {
            eprintln!("Failed to parse C2D message: {}", e);
        }
    }
}

async fn handle_twin_desired(payload: &str, client: &AsyncClient) {
    println!("Device twin desired properties updated: {}", payload);
    
    // Parse desired properties
    match serde_json::from_str::<serde_json::Value>(payload) {
        Ok(desired) => {
            // Echo back as reported properties
            let reported = json!({
                "acknowledged": true,
                "desiredVersion": desired.get("$version").unwrap_or(&json!(0))
            });
            
            let topic = format!("{}2", TWIN_REPORTED_TOPIC);
            let _ = client
                .publish(&topic, QoS::AtLeastOnce, false, reported.to_string().as_bytes())
                .await;
        }
        Err(e) => {
            eprintln!("Failed to parse twin desired properties: {}", e);
        }
    }
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
rumqttc = "0.24"
tokio = { version = "1", features = ["full"] }
serde_json = "1.0"
chrono = "0.4"
```

---

## Summary

**Azure IoT Hub MQTT** enables secure, scalable IoT device connectivity through the MQTT protocol with enterprise-grade features:

**Key Capabilities:**
- **Secure Authentication**: SAS tokens or X.509 certificates with mandatory TLS encryption
- **Bidirectional Communication**: Device-to-cloud telemetry and cloud-to-device commands
- **Device Twins**: JSON documents for synchronizing device state and configuration
- **Direct Methods**: Synchronous request/response patterns for immediate device control
- **Message Routing**: Integration with Azure services (Storage, Functions, Stream Analytics)

**MQTT Topic Patterns:**
- Telemetry: `devices/{device_id}/messages/events/`
- Commands: `devices/{device_id}/messages/devicebound/#`
- Twin Updates: `$iothub/twin/PATCH/properties/reported/`

**Implementation Considerations:**
- Port 8883 for secure MQTT over TLS
- Username format: `{hub_hostname}/{device_id}/?api-version=2021-04-12`
- SAS token authentication or X.509 certificates
- QoS 0 or 1 supported (QoS 2 not supported)
- Maximum message size: 256 KB

**Use Cases:**
- Industrial IoT monitoring and control
- Smart home device management
- Fleet tracking and telematics
- Environmental sensor networks
- Healthcare device integration

Azure IoT Hub MQTT combines the simplicity of MQTT with Azure's enterprise features, making it ideal for production-grade IoT deployments requiring scalability, security, and cloud integration.