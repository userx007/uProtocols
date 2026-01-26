# LoRaWAN to MQTT Bridge

## Overview

A LoRaWAN to MQTT bridge serves as a critical gateway component that connects LoRaWAN (Long Range Wide Area Network) devices to MQTT-based systems. This bridge receives data from LoRaWAN sensors through a LoRaWAN network server and publishes it to MQTT topics, enabling integration with IoT platforms, dashboards, and other MQTT-compatible applications.

## Architecture

The typical architecture involves:

1. **LoRaWAN End Devices** - Sensors transmitting data via LoRa radio
2. **LoRaWAN Gateway** - Receives LoRa packets and forwards to network server
3. **LoRaWAN Network Server** - Manages device authentication, deduplication, and routing
4. **Bridge Application** - Subscribes to network server events and publishes to MQTT
5. **MQTT Broker** - Distributes sensor data to applications

## Topic Structure

Common MQTT topic patterns for LoRaWAN bridges:

```
lorawan/{application_id}/{device_id}/up        # Uplink data from device
lorawan/{application_id}/{device_id}/down      # Downlink commands to device
lorawan/{application_id}/{device_id}/status    # Device status/metadata
lorawan/{application_id}/{device_id}/join      # Join notifications
```

## C/C++ Implementation

Here's a bridge implementation using the Eclipse Paho MQTT library and ChirpStack API:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <json-c/json.h>

#define MQTT_BROKER "tcp://localhost:1883"
#define CLIENT_ID "lorawan_bridge"
#define QOS 1
#define TIMEOUT 10000L

typedef struct {
    char device_id[64];
    char application_id[64];
    uint8_t* payload;
    size_t payload_len;
    int rssi;
    float snr;
} LoRaWANMessage;

MQTTClient mqtt_client;

// Parse LoRaWAN uplink message (ChirpStack format)
int parse_lorawan_uplink(const char* json_str, LoRaWANMessage* msg) {
    struct json_object *parsed_json, *dev_eui, *app_id, *data, *rssi, *snr;
    
    parsed_json = json_tokener_parse(json_str);
    
    if (!json_object_object_get_ex(parsed_json, "devEUI", &dev_eui) ||
        !json_object_object_get_ex(parsed_json, "applicationID", &app_id) ||
        !json_object_object_get_ex(parsed_json, "data", &data)) {
        json_object_put(parsed_json);
        return -1;
    }
    
    strncpy(msg->device_id, json_object_get_string(dev_eui), 63);
    strncpy(msg->application_id, json_object_get_string(app_id), 63);
    
    // Decode base64 payload
    const char* b64_data = json_object_get_string(data);
    // Base64 decode implementation needed here
    
    if (json_object_object_get_ex(parsed_json, "rxInfo", &rssi)) {
        struct json_object *rx_array = json_object_array_get_idx(rssi, 0);
        struct json_object *rssi_val;
        if (json_object_object_get_ex(rx_array, "rssi", &rssi_val)) {
            msg->rssi = json_object_get_int(rssi_val);
        }
    }
    
    json_object_put(parsed_json);
    return 0;
}

// Publish to MQTT
int publish_to_mqtt(const LoRaWANMessage* msg) {
    char topic[256];
    char payload[1024];
    
    snprintf(topic, sizeof(topic), 
             "lorawan/%s/%s/up", 
             msg->application_id, msg->device_id);
    
    // Create JSON payload
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"rssi\":%d,\"snr\":%.2f,\"data\":\"%s\"}",
             msg->device_id, msg->rssi, msg->snr, "base64_encoded_data");
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(mqtt_client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Failed to publish message, return code %d\n", rc);
        return -1;
    }
    
    MQTTClient_waitForCompletion(mqtt_client, token, TIMEOUT);
    printf("Message published to %s\n", topic);
    return 0;
}

// Callback for incoming LoRaWAN messages
int lorawan_message_callback(void *context, char *topicName, int topicLen, 
                             MQTTClient_message *message) {
    LoRaWANMessage lora_msg = {0};
    
    if (parse_lorawan_uplink((char*)message->payload, &lora_msg) == 0) {
        publish_to_mqtt(&lora_msg);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main(int argc, char* argv[]) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    // Initialize MQTT client
    MQTTClient_create(&mqtt_client, MQTT_BROKER, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    MQTTClient_setCallbacks(mqtt_client, NULL, NULL, 
                           lorawan_message_callback, NULL);
    
    if ((rc = MQTTClient_connect(mqtt_client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    // Subscribe to LoRaWAN network server topics
    MQTTClient_subscribe(mqtt_client, "application/+/device/+/event/up", QOS);
    
    printf("LoRaWAN to MQTT Bridge running...\n");
    
    // Keep running
    while (1) {
        sleep(1);
    }
    
    MQTTClient_disconnect(mqtt_client, 10000);
    MQTTClient_destroy(&mqtt_client);
    return 0;
}
```

## Rust Implementation

Here's a Rust implementation using `rumqttc` and `tokio`:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Deserialize, Serialize};
use base64::{Engine as _, engine::general_purpose};
use tokio;
use anyhow::Result;

#[derive(Debug, Deserialize)]
struct LoRaWANUplink {
    #[serde(rename = "devEUI")]
    dev_eui: String,
    #[serde(rename = "applicationID")]
    application_id: String,
    data: String,  // Base64 encoded
    #[serde(rename = "rxInfo")]
    rx_info: Option<Vec<RxInfo>>,
    #[serde(rename = "fPort")]
    f_port: u8,
}

#[derive(Debug, Deserialize)]
struct RxInfo {
    rssi: i32,
    #[serde(rename = "loRaSNR")]
    lora_snr: f32,
}

#[derive(Debug, Serialize)]
struct MqttPayload {
    device_id: String,
    application_id: String,
    port: u8,
    rssi: Option<i32>,
    snr: Option<f32>,
    data: Vec<u8>,
    timestamp: u64,
}

struct LoRaWANBridge {
    mqtt_client: AsyncClient,
}

impl LoRaWANBridge {
    fn new(broker_url: &str, client_id: &str) -> Result<Self> {
        let mut mqttoptions = MqttOptions::new(client_id, broker_url, 1883);
        mqttoptions.set_keep_alive(std::time::Duration::from_secs(30));
        
        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
        
        // Spawn eventloop in background
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(notification) => {
                        if let Event::Incoming(Packet::Publish(p)) = notification {
                            // Handle incoming messages
                        }
                    }
                    Err(e) => {
                        eprintln!("MQTT Error: {:?}", e);
                        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                    }
                }
            }
        });
        
        Ok(Self { mqtt_client: client })
    }
    
    async fn process_lorawan_uplink(&self, uplink: LoRaWANUplink) -> Result<()> {
        // Decode base64 payload
        let payload_bytes = general_purpose::STANDARD
            .decode(&uplink.data)
            .unwrap_or_default();
        
        // Extract RSSI and SNR from first gateway
        let (rssi, snr) = uplink.rx_info
            .as_ref()
            .and_then(|info| info.first())
            .map(|info| (Some(info.rssi), Some(info.lora_snr)))
            .unwrap_or((None, None));
        
        let mqtt_payload = MqttPayload {
            device_id: uplink.dev_eui.clone(),
            application_id: uplink.application_id.clone(),
            port: uplink.f_port,
            rssi,
            snr,
            data: payload_bytes,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
        };
        
        let topic = format!(
            "lorawan/{}/{}/up",
            uplink.application_id, uplink.dev_eui
        );
        
        let payload_json = serde_json::to_string(&mqtt_payload)?;
        
        self.mqtt_client
            .publish(topic, QoS::AtLeastOnce, false, payload_json.as_bytes())
            .await?;
        
        println!("Published data from device {} to MQTT", uplink.dev_eui);
        Ok(())
    }
    
    async fn handle_downlink(&self, device_id: &str, app_id: &str, 
                            payload: Vec<u8>, port: u8) -> Result<()> {
        // Encode payload as base64 for LoRaWAN network server
        let b64_payload = general_purpose::STANDARD.encode(&payload);
        
        let downlink_msg = serde_json::json!({
            "devEUI": device_id,
            "fPort": port,
            "data": b64_payload,
            "confirmed": false
        });
        
        let topic = format!("application/{}/device/{}/command/down", 
                          app_id, device_id);
        
        self.mqtt_client
            .publish(topic, QoS::AtLeastOnce, false, 
                    downlink_msg.to_string().as_bytes())
            .await?;
        
        println!("Sent downlink to device {}", device_id);
        Ok(())
    }
    
    async fn subscribe_to_network_server(&self) -> Result<()> {
        // Subscribe to ChirpStack uplink events
        self.mqtt_client
            .subscribe("application/+/device/+/event/up", QoS::AtLeastOnce)
            .await?;
        
        // Subscribe to join events
        self.mqtt_client
            .subscribe("application/+/device/+/event/join", QoS::AtLeastOnce)
            .await?;
        
        println!("Subscribed to LoRaWAN network server topics");
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let bridge = LoRaWANBridge::new("localhost", "lorawan_bridge_rust")?;
    
    bridge.subscribe_to_network_server().await?;
    
    println!("LoRaWAN to MQTT Bridge started");
    
    // Keep the application running
    loop {
        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
    }
}
```

## Key Features to Implement

1. **Payload Decoding**: Transform LoRaWAN payloads into human-readable formats
2. **Device Management**: Track device status, battery levels, and connectivity
3. **Downlink Queueing**: Buffer commands when devices are sleeping
4. **Error Handling**: Manage network failures and reconnection logic
5. **Security**: Implement TLS for MQTT connections and validate device credentials
6. **Scalability**: Handle multiple applications and thousands of devices
7. **Monitoring**: Log metrics like message rates, latency, and packet loss

## Summary

A LoRaWAN to MQTT bridge is essential infrastructure for integrating low-power, long-range sensors into modern IoT ecosystems. It handles the complexity of LoRaWAN protocol interactions while exposing simple MQTT topics for application consumption. The bridge typically subscribes to a LoRaWAN network server (like ChirpStack or The Things Network) and republishes device data to MQTT with appropriate transformations and metadata enrichment.

Key considerations include handling bidirectional communication (uplink and downlink), managing base64-encoded payloads, preserving signal quality metrics (RSSI/SNR), and implementing robust error handling for both wireless and network connectivity issues. The bridge serves as a critical translation layer enabling LoRaWAN's long-range, low-power capabilities to integrate seamlessly with MQTT-based IoT platforms.