# AWS IoT Core MQTT - Detailed Description

AWS IoT Core is Amazon's managed cloud service that enables IoT devices to securely connect and interact with cloud applications and other devices using MQTT (among other protocols). It provides enterprise-grade infrastructure for billions of devices and trillions of messages.

## Key Concepts

### Device Shadows (Digital Twins)
Device shadows are JSON documents that store and retrieve current state information for a device. They enable applications to interact with devices even when they're offline, providing a persistent virtual representation of each device.

### Rules Engine
The AWS IoT Rules Engine evaluates incoming messages, transforms them, and routes them to other AWS services (Lambda, DynamoDB, S3, etc.) based on SQL-like rules.

### Topics Structure
AWS IoT uses specific topic patterns:
- `$aws/things/{thingName}/shadow/update` - Update device shadow
- `$aws/things/{thingName}/shadow/get` - Retrieve shadow
- Custom topics for application-specific messaging

---

## C/C++ Implementation

AWS provides the AWS IoT Device SDK for C, which includes MQTT support with TLS/mutual authentication.

```c
#include "aws_iot_mqtt_client.h"
#include "aws_iot_config.h"
#include "aws_iot_shadow_interface.h"
#include <stdio.h>
#include <string.h>

// Shadow callback when update is received
void shadow_update_callback(const char *pThingName, ShadowActions_t action,
                            Shadow_Ack_Status_t status,
                            const char *pReceivedJsonDocument,
                            void *pContextData) {
    if (status == SHADOW_ACK_TIMEOUT) {
        printf("Shadow update timeout\n");
    } else if (status == SHADOW_ACK_REJECTED) {
        printf("Shadow update rejected\n");
    } else if (status == SHADOW_ACK_ACCEPTED) {
        printf("Shadow update accepted: %s\n", pReceivedJsonDocument);
    }
}

// Delta callback - triggered when desired and reported states differ
void shadow_delta_callback(const char *pThingName, const char *pDeltaJson,
                           uint32_t deltaJsonLength, void *pContextData) {
    printf("Received delta: %s\n", pDeltaJson);
    // Parse delta and apply changes to device
}

int main() {
    AWS_IoT_Client mqttClient;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    // Configure connection parameters
    mqttInitParams.enableAutoReconnect = true;
    mqttInitParams.pHostURL = "your-endpoint.iot.us-east-1.amazonaws.com";
    mqttInitParams.port = 8883;
    mqttInitParams.pRootCALocation = "root-CA.crt";
    mqttInitParams.pDeviceCertLocation = "device.crt";
    mqttInitParams.pDevicePrivateKeyLocation = "device.key";

    connectParams.keepAliveIntervalInSec = 60;
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;
    connectParams.pClientID = "my-iot-device";
    connectParams.clientIDLen = strlen("my-iot-device");

    // Initialize and connect
    IoT_Error_t rc = aws_iot_mqtt_init(&mqttClient, &mqttInitParams);
    if (rc != SUCCESS) {
        printf("MQTT init failed: %d\n", rc);
        return -1;
    }

    rc = aws_iot_mqtt_connect(&mqttClient, &connectParams);
    if (rc != SUCCESS) {
        printf("MQTT connect failed: %d\n", rc);
        return -1;
    }

    // Initialize shadow
    ShadowInitParameters_t shadowInitParams;
    shadowInitParams.pHost = mqttInitParams.pHostURL;
    shadowInitParams.port = mqttInitParams.port;
    shadowInitParams.pClientCRT = mqttInitParams.pDeviceCertLocation;
    shadowInitParams.pClientKey = mqttInitParams.pDevicePrivateKeyLocation;
    shadowInitParams.pRootCA = mqttInitParams.pRootCALocation;
    shadowInitParams.enableAutoReconnect = true;

    rc = aws_iot_shadow_init(&mqttClient, &shadowInitParams);
    if (rc != SUCCESS) {
        printf("Shadow init failed\n");
        return -1;
    }

    rc = aws_iot_shadow_connect(&mqttClient, &connectParams);
    if (rc != SUCCESS) {
        printf("Shadow connect failed\n");
        return -1;
    }

    // Register delta callback
    rc = aws_iot_shadow_register_delta(&mqttClient, shadow_delta_callback, NULL);

    // Update shadow with reported state
    char jsonDoc[512];
    snprintf(jsonDoc, sizeof(jsonDoc),
             "{\"state\":{\"reported\":{\"temperature\":22.5,\"humidity\":45}}}");

    rc = aws_iot_shadow_update(&mqttClient, "MyIoTDevice", jsonDoc,
                               shadow_update_callback, NULL, 5, true);

    // Main loop - yield to process incoming messages
    while (1) {
        rc = aws_iot_shadow_yield(&mqttClient, 1000);
        if (rc == NETWORK_ATTEMPTING_RECONNECT) {
            continue;
        }
        
        // Simulate sensor readings and update shadow
        sleep(10);
    }

    aws_iot_shadow_disconnect(&mqttClient);
    return 0;
}
```

### Publishing Custom MQTT Messages (C++)

```cpp
#include "aws_iot_mqtt_client_interface.h"
#include <string>
#include <json/json.h>

class AWSIoTDevice {
private:
    AWS_IoT_Client client;
    std::string thingName;

public:
    void messageCallback(AWS_IoT_Client *pClient, char *topicName,
                        uint16_t topicNameLen,
                        IoT_Publish_Message_Params *params, void *pData) {
        printf("Message on %s: %.*s\n", topicName,
               (int)params->payloadLen, (char*)params->payload);
    }

    bool publishTelemetry(double temperature, double humidity) {
        Json::Value root;
        root["deviceId"] = thingName;
        root["temperature"] = temperature;
        root["humidity"] = humidity;
        root["timestamp"] = time(nullptr);

        Json::FastWriter writer;
        std::string payload = writer.write(root);

        IoT_Publish_Message_Params params;
        params.qos = QOS1;
        params.payload = (void*)payload.c_str();
        params.payloadLen = payload.length();
        params.isRetained = 0;

        std::string topic = "devices/" + thingName + "/telemetry";
        IoT_Error_t rc = aws_iot_mqtt_publish(&client, topic.c_str(),
                                              topic.length(), &params);
        return (rc == SUCCESS);
    }

    bool subscribe(const std::string& topic) {
        IoT_Error_t rc = aws_iot_mqtt_subscribe(&client, topic.c_str(),
                                                topic.length(), QOS1,
                                                messageCallback, nullptr);
        return (rc == SUCCESS);
    }
};
```

---

## Rust Implementation

Using the `rumqttc` library with AWS IoT Core requires custom TLS configuration for mutual authentication.

```rust
use rumqttc::{MqttOptions, Client, QoS, Event, Packet};
use rustls::{ClientConfig, Certificate, PrivateKey};
use std::fs::File;
use std::io::BufReader;
use std::sync::Arc;
use std::time::Duration;
use serde::{Deserialize, Serialize};
use serde_json;

#[derive(Serialize, Deserialize)]
struct DeviceShadow {
    state: ShadowState,
}

#[derive(Serialize, Deserialize)]
struct ShadowState {
    reported: Option<DeviceState>,
    desired: Option<DeviceState>,
}

#[derive(Serialize, Deserialize, Clone)]
struct DeviceState {
    temperature: f32,
    humidity: f32,
    power: bool,
}

#[derive(Serialize)]
struct TelemetryMessage {
    device_id: String,
    temperature: f32,
    humidity: f32,
    timestamp: i64,
}

fn load_certificates(cert_path: &str, key_path: &str, ca_path: &str) 
    -> Result<ClientConfig, Box<dyn std::error::Error>> {
    
    let mut config = ClientConfig::new();
    
    // Load CA certificate
    let ca_file = File::open(ca_path)?;
    let mut ca_reader = BufReader::new(ca_file);
    let ca_certs = rustls_pemfile::certs(&mut ca_reader)?;
    
    for cert in ca_certs {
        config.root_store.add(&Certificate(cert))?;
    }
    
    // Load client certificate
    let cert_file = File::open(cert_path)?;
    let mut cert_reader = BufReader::new(cert_file);
    let cert_chain = rustls_pemfile::certs(&mut cert_reader)?
        .into_iter()
        .map(Certificate)
        .collect();
    
    // Load private key
    let key_file = File::open(key_path)?;
    let mut key_reader = BufReader::new(key_file);
    let keys = rustls_pemfile::pkcs8_private_keys(&mut key_reader)?;
    let private_key = PrivateKey(keys[0].clone());
    
    config.set_single_client_cert(cert_chain, private_key)?;
    
    Ok(config)
}

struct AWSIoTClient {
    client: Client,
    thing_name: String,
}

impl AWSIoTClient {
    fn new(endpoint: &str, thing_name: &str, cert_path: &str, 
           key_path: &str, ca_path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        
        let mut mqtt_options = MqttOptions::new(thing_name, endpoint, 8883);
        mqtt_options.set_keep_alive(Duration::from_secs(60));
        
        // Load TLS configuration
        let tls_config = load_certificates(cert_path, key_path, ca_path)?;
        mqtt_options.set_transport(rumqttc::Transport::Tls(Arc::new(tls_config)));
        
        let (client, _connection) = Client::new(mqtt_options, 10);
        
        Ok(AWSIoTClient {
            client,
            thing_name: thing_name.to_string(),
        })
    }
    
    fn update_shadow(&self, state: &DeviceState) -> Result<(), Box<dyn std::error::Error>> {
        let shadow = DeviceShadow {
            state: ShadowState {
                reported: Some(state.clone()),
                desired: None,
            },
        };
        
        let payload = serde_json::to_string(&shadow)?;
        let topic = format!("$aws/things/{}/shadow/update", self.thing_name);
        
        self.client.publish(topic, QoS::AtLeastOnce, false, payload)?;
        Ok(())
    }
    
    fn get_shadow(&self) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("$aws/things/{}/shadow/get", self.thing_name);
        self.client.publish(topic, QoS::AtLeastOnce, false, "")?;
        Ok(())
    }
    
    fn subscribe_to_shadow_delta(&self) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("$aws/things/{}/shadow/update/delta", self.thing_name);
        self.client.subscribe(topic, QoS::AtLeastOnce)?;
        Ok(())
    }
    
    fn subscribe_to_shadow_accepted(&self) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("$aws/things/{}/shadow/get/accepted", self.thing_name);
        self.client.subscribe(topic, QoS::AtLeastOnce)?;
        Ok(())
    }
    
    fn publish_telemetry(&self, temperature: f32, humidity: f32) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        let message = TelemetryMessage {
            device_id: self.thing_name.clone(),
            temperature,
            humidity,
            timestamp: chrono::Utc::now().timestamp(),
        };
        
        let payload = serde_json::to_string(&message)?;
        let topic = format!("devices/{}/telemetry", self.thing_name);
        
        self.client.publish(topic, QoS::AtLeastOnce, false, payload)?;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let endpoint = "your-endpoint.iot.us-east-1.amazonaws.com";
    let thing_name = "MyIoTDevice";
    
    let iot_client = AWSIoTClient::new(
        endpoint,
        thing_name,
        "certs/device.crt",
        "certs/device.key",
        "certs/root-CA.crt"
    )?;
    
    // Subscribe to shadow updates
    iot_client.subscribe_to_shadow_delta()?;
    iot_client.subscribe_to_shadow_accepted()?;
    
    // Create event loop in separate thread
    let (client, mut connection) = {
        let mut opts = MqttOptions::new(thing_name, endpoint, 8883);
        opts.set_keep_alive(Duration::from_secs(60));
        Client::new(opts, 10)
    };
    
    std::thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    let topic = std::str::from_utf8(&p.topic).unwrap();
                    let payload = std::str::from_utf8(&p.payload).unwrap();
                    
                    println!("Received on {}: {}", topic, payload);
                    
                    if topic.contains("/shadow/update/delta") {
                        // Handle desired state changes
                        if let Ok(shadow) = serde_json::from_str::<DeviceShadow>(payload) {
                            if let Some(desired) = shadow.state.desired {
                                println!("Applying desired state: {:?}", desired);
                                // Apply changes to device
                            }
                        }
                    }
                }
                Ok(_) => {}
                Err(e) => eprintln!("Connection error: {}", e),
            }
        }
    });
    
    // Main loop - publish telemetry
    loop {
        let state = DeviceState {
            temperature: 22.5,
            humidity: 45.0,
            power: true,
        };
        
        iot_client.update_shadow(&state)?;
        iot_client.publish_telemetry(state.temperature, state.humidity)?;
        
        std::thread::sleep(Duration::from_secs(10));
    }
}
```

---

## Summary

**AWS IoT Core MQTT** provides enterprise-grade IoT infrastructure with:

- **Security**: Mutual TLS authentication with X.509 certificates, fine-grained policies
- **Device Shadows**: Persistent device state enabling offline/online synchronization
- **Rules Engine**: Message routing and transformation to 50+ AWS services
- **Scalability**: Supports billions of devices and trillions of messages
- **MQTT 3.1.1/5.0 Support**: Standard protocol with AWS-specific enhancements

**Implementation requires**:
- Device certificates provisioned through AWS IoT
- Proper IAM policies and thing types
- TLS 1.2+ with mutual authentication
- Topic-based access control

**Common use cases**: Industrial IoT, smart home devices, fleet management, predictive maintenance, real-time monitoring, and building device-to-cloud data pipelines with integrated AWS service workflows.