# Smart Home Automation with MQTT

## Overview

Smart home automation using MQTT creates a robust, scalable architecture for managing IoT devices. MQTT serves as the messaging backbone, enabling lightweight, real-time communication between sensors, actuators, and home automation platforms like Home Assistant and OpenHAB.

## Architecture Patterns

**Hub-and-Spoke Model**: A central broker (like Mosquitto) manages all device communications. Devices publish sensor data and subscribe to command topics, while automation platforms coordinate logic and user interfaces.

**Topic Hierarchy**: Organized namespaces enable efficient routing and filtering:
- `home/livingroom/temperature` - sensor readings
- `home/kitchen/light/set` - actuator commands
- `home/bedroom/motion/status` - binary sensor states

**Quality of Service (QoS)**: 
- QoS 0 for frequent sensor updates (temperature)
- QoS 1 for commands requiring confirmation (light switches)
- QoS 2 for critical operations (security system states)

**Retained Messages**: Last known states persist on the broker, allowing new subscribers to immediately retrieve device status without waiting for updates.

## C/C++ Implementation

Using the Eclipse Paho MQTT C library for embedded devices:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define BROKER_ADDRESS    "tcp://localhost:1883"
#define CLIENT_ID         "SmartThermostat"
#define TEMP_TOPIC        "home/livingroom/temperature"
#define SETPOINT_TOPIC    "home/livingroom/thermostat/setpoint"
#define QOS               1
#define TIMEOUT           10000L

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt) {
    deliveredtoken = dt;
}

int messageArrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    
    if (strcmp(topicName, SETPOINT_TOPIC) == 0) {
        float setpoint = atof((char*)message->payload);
        printf("New thermostat setpoint: %.1f°C\n", setpoint);
        // Apply setpoint logic here
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connectionLost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, connectionLost, 
                           messageArrived, delivered);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    MQTTClient_subscribe(client, SETPOINT_TOPIC, QOS);

    // Simulate temperature sensor publishing
    char payload[50];
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    for (int i = 0; i < 10; i++) {
        float temp = 20.0 + (rand() % 50) / 10.0;
        snprintf(payload, sizeof(payload), "%.1f", temp);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 1;  // Retain last temperature reading
        
        MQTTClient_publishMessage(client, TEMP_TOPIC, &pubmsg, &token);
        printf("Published temperature: %s°C\n", payload);
        
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        sleep(2);
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}
```

## Rust Implementation

Using the `rumqttc` crate for async MQTT communication:

```rust
use rumqttc::{MqttOptions, AsyncClient, QoS, Event, Packet};
use tokio::{time, task};
use std::time::Duration;
use serde::{Serialize, Deserialize};
use serde_json;

#[derive(Serialize, Deserialize, Debug)]
struct SensorData {
    device_id: String,
    temperature: f32,
    humidity: f32,
    timestamp: u64,
}

#[derive(Serialize, Deserialize, Debug)]
struct LightCommand {
    state: String,  // "ON" or "OFF"
    brightness: Option<u8>,
    color: Option<String>,
}

async fn publish_sensor_data(client: AsyncClient) {
    let mut interval = time::interval(Duration::from_secs(5));
    
    loop {
        interval.tick().await;
        
        let sensor_data = SensorData {
            device_id: "sensor_bedroom_01".to_string(),
            temperature: 21.5 + (rand::random::<f32>() * 3.0),
            humidity: 45.0 + (rand::random::<f32>() * 10.0),
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs(),
        };
        
        let payload = serde_json::to_string(&sensor_data).unwrap();
        
        client.publish(
            "home/bedroom/sensor/data",
            QoS::AtLeastOnce,
            true,  // retain
            payload.as_bytes()
        ).await.unwrap();
        
        println!("Published: {:?}", sensor_data);
    }
}

async fn handle_messages(mut eventloop: rumqttc::EventLoop) {
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(p))) => {
                let topic = p.topic.as_str();
                let payload = String::from_utf8_lossy(&p.payload);
                
                if topic.contains("/light/set") {
                    if let Ok(cmd) = serde_json::from_str::<LightCommand>(&payload) {
                        println!("Light command received: {:?}", cmd);
                        // Execute light control logic
                        handle_light_command(cmd).await;
                    }
                }
            }
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                println!("Connected to MQTT broker");
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                time::sleep(Duration::from_secs(1)).await;
            }
            _ => {}
        }
    }
}

async fn handle_light_command(cmd: LightCommand) {
    println!("Setting light to: {}", cmd.state);
    if let Some(brightness) = cmd.brightness {
        println!("Brightness: {}%", brightness);
    }
    // GPIO or smart bulb API calls here
}

#[tokio::main]
async fn main() {
    let mut mqttoptions = MqttOptions::new("smart_home_device", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    mqttoptions.set_clean_session(true);
    
    let (client, eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to command topics
    client.subscribe("home/+/light/set", QoS::AtLeastOnce).await.unwrap();
    client.subscribe("home/+/thermostat/setpoint", QoS::AtLeastOnce).await.unwrap();
    
    // Spawn tasks
    let publish_handle = task::spawn(publish_sensor_data(client.clone()));
    let message_handle = task::spawn(handle_messages(eventloop));
    
    // Wait for both tasks
    tokio::try_join!(publish_handle, message_handle).unwrap();
}
```

## Integration with Home Assistant

Home Assistant auto-discovers MQTT devices using standardized discovery topics:

```yaml
# configuration.yaml
mqtt:
  broker: localhost
  port: 1883
  discovery: true
  discovery_prefix: homeassistant

sensor:
  - platform: mqtt
    name: "Living Room Temperature"
    state_topic: "home/livingroom/temperature"
    unit_of_measurement: "°C"
    
light:
  - platform: mqtt
    name: "Kitchen Light"
    command_topic: "home/kitchen/light/set"
    state_topic: "home/kitchen/light/status"
    payload_on: "ON"
    payload_off: "OFF"
```

## Summary

MQTT-based smart home automation provides a flexible, efficient architecture for IoT device management. The C/C++ example demonstrates embedded device programming with retained messages and QoS levels, suitable for resource-constrained hardware like ESP32 or Raspberry Pi. The Rust implementation showcases async patterns with structured JSON payloads and multi-topic subscriptions, ideal for more powerful edge devices or gateways. Both integrate seamlessly with platforms like Home Assistant and OpenHAB through standardized topic hierarchies and discovery protocols. Key benefits include low bandwidth usage, reliable message delivery, and easy scalability from single-room to whole-home automation systems.