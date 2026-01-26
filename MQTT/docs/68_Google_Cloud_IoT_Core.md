# Google Cloud IoT Core with MQTT

## Overview

Google Cloud IoT Core was a fully managed service that enabled secure device connection and management for IoT applications on Google Cloud Platform. **Important: Google Cloud IoT Core was retired on August 16, 2023.** However, understanding its MQTT bridge implementation remains valuable for:

- Migrating to alternative solutions
- Understanding cloud IoT architecture patterns
- Working with similar services from other providers

The service used an MQTT bridge that allowed devices to connect securely using the MQTT protocol and publish telemetry data or receive configuration updates from the cloud.

## Key Concepts

**Device Registry**: Logical container for devices with shared properties
**Device**: Individual IoT entity with unique credentials
**Telemetry**: Event data sent from device to cloud
**Configuration**: State data sent from cloud to device
**JWT Authentication**: JSON Web Tokens signed with device keys for authentication

## Architecture

```
Device → MQTT Bridge → Cloud IoT Core → Cloud Pub/Sub → Cloud Functions/Dataflow
```

**MQTT Topics Structure:**
- Telemetry: `/devices/{device-id}/events` or `/devices/{device-id}/events/{subfolder}`
- State: `/devices/{device-id}/state`
- Configuration: `/devices/{device-id}/config`
- Commands: `/devices/{device-id}/commands/#`

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <jwt.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <time.h>

#define MQTT_HOST "mqtt.googleapis.com"
#define MQTT_PORT 8883
#define PROJECT_ID "your-project-id"
#define REGION "us-central1"
#define REGISTRY_ID "your-registry"
#define DEVICE_ID "your-device"

// Generate JWT token for authentication
char* create_jwt(const char* project_id, const char* private_key_file) {
    jwt_t *jwt = NULL;
    char *token = NULL;
    time_t now = time(NULL);
    time_t exp = now + 3600; // Token expires in 1 hour
    
    // Create JWT
    jwt_new(&jwt);
    jwt_add_grant(jwt, "iat", (long)now);
    jwt_add_grant(jwt, "exp", (long)exp);
    jwt_add_grant(jwt, "aud", project_id);
    jwt_set_alg(jwt, JWT_ALG_ES256, NULL, 0);
    
    // Load private key
    FILE *fp = fopen(private_key_file, "r");
    EC_KEY *ec_key = PEM_read_ECPrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    
    // Sign and encode
    token = jwt_encode_str(jwt);
    
    jwt_free(jwt);
    EC_KEY_free(ec_key);
    
    return token;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    printf("Connected with result code: %d\n", rc);
    
    if (rc == 0) {
        // Subscribe to configuration topic
        char config_topic[256];
        snprintf(config_topic, sizeof(config_topic), 
                "/devices/%s/config", DEVICE_ID);
        mosquitto_subscribe(mosq, NULL, config_topic, 1);
        
        // Subscribe to commands
        char cmd_topic[256];
        snprintf(cmd_topic, sizeof(cmd_topic), 
                "/devices/%s/commands/#", DEVICE_ID);
        mosquitto_subscribe(mosq, NULL, cmd_topic, 0);
    }
}

void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    printf("Received message on %s: %s\n", 
           msg->topic, (char*)msg->payload);
}

int main() {
    struct mosquitto *mosq;
    char client_id[256];
    char username[256];
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create client ID
    snprintf(client_id, sizeof(client_id),
            "projects/%s/locations/%s/registries/%s/devices/%s",
            PROJECT_ID, REGION, REGISTRY_ID, DEVICE_ID);
    
    // Create MQTT client
    mosq = mosquitto_new(client_id, true, NULL);
    
    // Set username (unused, but required)
    snprintf(username, sizeof(username), "unused");
    
    // Generate JWT password
    char *jwt_token = create_jwt(PROJECT_ID, "private_key.pem");
    
    // Set credentials
    mosquitto_username_pw_set(mosq, username, jwt_token);
    
    // Enable TLS
    mosquitto_tls_set(mosq, "/path/to/roots.pem", NULL, NULL, NULL, NULL);
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    
    // Connect
    int rc = mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("Connection failed: %s\n", mosquitto_strerror(rc));
        return 1;
    }
    
    // Publish telemetry
    char telemetry_topic[256];
    snprintf(telemetry_topic, sizeof(telemetry_topic),
            "/devices/%s/events", DEVICE_ID);
    
    char payload[128];
    int counter = 0;
    
    // Main loop
    while (1) {
        // Publish sensor data
        snprintf(payload, sizeof(payload), 
                "{\"temperature\": 22.5, \"counter\": %d}", counter++);
        
        mosquitto_publish(mosq, NULL, telemetry_topic, 
                         strlen(payload), payload, 1, false);
        
        // Process network events
        mosquitto_loop(mosq, 1000, 1);
        
        sleep(10);
    }
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    free(jwt_token);
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o iot_device iot_device.c -lmosquitto -ljwt -lcrypto -lssl
```

## Rust Implementation

```rust
use rumqttc::{Client, MqttOptions, QoS, TlsConfiguration, Transport};
use serde_json::json;
use jsonwebtoken::{encode, Algorithm, EncodingKey, Header};
use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};
use std::fs;
use std::thread;
use std::time::Duration;

const PROJECT_ID: &str = "your-project-id";
const REGION: &str = "us-central1";
const REGISTRY_ID: &str = "your-registry";
const DEVICE_ID: &str = "your-device";

#[derive(Debug, Serialize, Deserialize)]
struct Claims {
    iat: u64,
    exp: u64,
    aud: String,
}

// Generate JWT token
fn create_jwt(project_id: &str, private_key_path: &str) -> Result<String, Box<dyn std::error::Error>> {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)?
        .as_secs();
    
    let claims = Claims {
        iat: now,
        exp: now + 3600, // 1 hour expiration
        aud: project_id.to_string(),
    };
    
    // Read private key
    let private_key = fs::read(private_key_path)?;
    let encoding_key = EncodingKey::from_ec_pem(&private_key)?;
    
    // Create JWT with ES256 algorithm
    let header = Header::new(Algorithm::ES256);
    let token = encode(&header, &claims, &encoding_key)?;
    
    Ok(token)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create client ID
    let client_id = format!(
        "projects/{}/locations/{}/registries/{}/devices/{}",
        PROJECT_ID, REGION, REGISTRY_ID, DEVICE_ID
    );
    
    // Generate JWT
    let jwt_token = create_jwt(PROJECT_ID, "private_key.pem")?;
    
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new(
        client_id,
        "mqtt.googleapis.com",
        8883,
    );
    
    mqtt_options.set_credentials("unused", &jwt_token);
    mqtt_options.set_keep_alive(Duration::from_secs(60));
    
    // Configure TLS
    let ca = fs::read("roots.pem")?;
    let tls_config = TlsConfiguration::Simple {
        ca,
        alpn: None,
        client_auth: None,
    };
    mqtt_options.set_transport(Transport::Tls(tls_config));
    
    // Create client
    let (mut client, mut connection) = Client::new(mqtt_options, 10);
    
    // Subscribe to configuration topic
    let config_topic = format!("/devices/{}/config", DEVICE_ID);
    client.subscribe(&config_topic, QoS::AtLeastOnce)?;
    
    // Subscribe to commands
    let cmd_topic = format!("/devices/{}/commands/#", DEVICE_ID);
    client.subscribe(&cmd_topic, QoS::AtMostOnce)?;
    
    // Spawn thread to handle incoming messages
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(event) => {
                    println!("Event: {:?}", event);
                }
                Err(e) => {
                    eprintln!("Error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Publish telemetry
    let telemetry_topic = format!("/devices/{}/events", DEVICE_ID);
    let mut counter = 0;
    
    loop {
        let payload = json!({
            "temperature": 22.5,
            "humidity": 65.3,
            "counter": counter,
        });
        
        client.publish(
            &telemetry_topic,
            QoS::AtLeastOnce,
            false,
            payload.to_string().as_bytes(),
        )?;
        
        println!("Published telemetry: {}", counter);
        counter += 1;
        
        thread::sleep(Duration::from_secs(10));
    }
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
rumqttc = "0.23"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
jsonwebtoken = "9.2"
```

## Key Features Demonstrated

### Authentication
Both implementations show JWT-based authentication using ES256 algorithm with elliptic curve keys.

### Topic Structure
- **Events/Telemetry**: Publishing sensor data to `/devices/{device-id}/events`
- **Configuration**: Subscribing to `/devices/{device-id}/config` for cloud-to-device updates
- **Commands**: Subscribing to `/devices/{device-id}/commands/#` for real-time commands

### QoS Levels
- QoS 1 for telemetry (at least once delivery)
- QoS 0 for commands (at most once, lower latency)

### TLS Security
Both examples use TLS 1.2+ with Google's root CA certificates for encrypted communication.

## Summary

Google Cloud IoT Core provided a managed MQTT bridge for connecting IoT devices to GCP. While the service has been retired, its patterns remain relevant:

- **Secure authentication** via JWT tokens with short expiration times
- **Structured topics** separating telemetry, state, configuration, and commands
- **TLS encryption** for all communications
- **Integration with Cloud Pub/Sub** for scalable message processing

**Migration alternatives** include:
- Google Cloud Pub/Sub with custom MQTT broker
- AWS IoT Core
- Azure IoT Hub
- Third-party platforms (HiveMQ, Eclipse IoT)

The code examples demonstrate production-ready patterns for device authentication, bidirectional communication, and proper error handling that apply to most cloud IoT platforms.