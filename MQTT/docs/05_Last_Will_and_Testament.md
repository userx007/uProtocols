# Last Will and Testament (LWT) in MQTT

## Overview

Last Will and Testament (LWT) is a critical MQTT feature that enables graceful handling of unexpected client disconnections. When a client connects to an MQTT broker, it can specify a "last will" message that the broker will automatically publish on behalf of the client if the connection is lost unexpectedly (due to network failure, crash, or power loss).

## How LWT Works

When establishing a connection, a client can configure:
- **Will Topic**: The topic where the LWT message will be published
- **Will Message**: The payload to send
- **Will QoS**: Quality of Service level (0, 1, or 2)
- **Will Retain**: Whether the message should be retained by the broker

The broker monitors the client connection through keep-alive packets. If the client fails to send a keep-alive within the expected timeframe, or if the connection drops without a proper DISCONNECT packet, the broker publishes the LWT message to notify other subscribers that the client is offline.

## Use Cases

- **Device status monitoring**: Detecting when IoT devices go offline
- **Presence detection**: Tracking online/offline status of users or services
- **System health**: Alerting when critical services disconnect
- **Failover mechanisms**: Triggering backup systems when primary services fail

## C/C++ Implementation (Eclipse Paho MQTT C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "DeviceMonitor_001"
#define STATUS_TOPIC "devices/monitor_001/status"
#define QOS         1
#define TIMEOUT     10000L

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    // Configure Last Will and Testament
    will_opts.topicName = STATUS_TOPIC;
    will_opts.message = "offline";
    will_opts.retained = 1;  // Retain the offline status
    will_opts.qos = QOS;

    // Configure connection options with LWT
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.will = &will_opts;  // Attach LWT to connection

    printf("Connecting to broker: %s\n", ADDRESS);
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    printf("Connected successfully\n");

    // Publish "online" status immediately after connecting
    pubmsg.payload = "online";
    pubmsg.payloadlen = strlen("online");
    pubmsg.qos = QOS;
    pubmsg.retained = 1;
    
    MQTTClient_publishMessage(client, STATUS_TOPIC, &pubmsg, NULL);
    printf("Published online status\n");

    // Simulate device running for some time
    printf("Device running... (Press Ctrl+C to simulate crash)\n");
    
    // Keep the application running
    while(1) {
        // Simulate device work
        sleep(5);
        printf("Device still running...\n");
    }

    // This won't be reached in crash scenario
    // If gracefully shutting down, clear the will by publishing offline
    pubmsg.payload = "offline";
    pubmsg.payloadlen = strlen("offline");
    MQTTClient_publishMessage(client, STATUS_TOPIC, &pubmsg, NULL);
    
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

### C++ Example with Monitoring

```cpp
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS{"tcp://localhost:1883"};
const std::string CLIENT_ID{"DeviceMonitor_CPP"};
const std::string STATUS_TOPIC{"devices/monitor_cpp/status"};
const int QOS = 1;

class DeviceMonitor : public virtual mqtt::callback {
private:
    mqtt::async_client client_;
    
public:
    DeviceMonitor(const std::string& server_uri, const std::string& client_id)
        : client_(server_uri, client_id) {
        client_.set_callback(*this);
    }

    void connection_lost(const std::string& cause) override {
        std::cout << "\nConnection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message on topic '" << msg->get_topic() 
                  << "': " << msg->to_string() << std::endl;
    }

    void connect() {
        // Configure Last Will and Testament
        mqtt::will_options lwt{
            STATUS_TOPIC,              // topic
            "offline",                 // payload
            QOS,                       // QoS
            true                       // retained
        };

        // Connection options with LWT
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        conn_opts.set_will(lwt);  // Set LWT
        conn_opts.set_automatic_reconnect(true);

        try {
            std::cout << "Connecting to broker..." << std::endl;
            auto tok = client_.connect(conn_opts);
            tok->wait();
            std::cout << "Connected!" << std::endl;

            // Publish online status
            auto msg = mqtt::make_message(STATUS_TOPIC, "online");
            msg->set_qos(QOS);
            msg->set_retained(true);
            client_.publish(msg)->wait();
            std::cout << "Published online status" << std::endl;

        } catch (const mqtt::exception& exc) {
            std::cerr << "Connection error: " << exc.what() << std::endl;
            throw;
        }
    }

    void run() {
        std::cout << "Device running... (Press Ctrl+C to simulate crash)" 
                  << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::cout << "Device heartbeat..." << std::endl;
        }
    }

    ~DeviceMonitor() {
        try {
            // Graceful shutdown - clear the will
            auto msg = mqtt::make_message(STATUS_TOPIC, "offline");
            msg->set_retained(true);
            client_.publish(msg)->wait();
            client_.disconnect()->wait();
        } catch (...) {}
    }
};

int main() {
    try {
        DeviceMonitor monitor(SERVER_ADDRESS, CLIENT_ID);
        monitor.connect();
        monitor.run();
    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## Rust Implementation (using rumqttc)

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, LastWill};
use tokio::time::{sleep, Duration};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new(
        "DeviceMonitor_Rust",
        "localhost",
        1883
    );
    
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    
    // Configure Last Will and Testament
    let lwt = LastWill::new(
        "devices/monitor_rust/status",  // topic
        "offline",                       // payload
        QoS::AtLeastOnce,               // QoS
        true                             // retain
    );
    
    mqtt_options.set_last_will(lwt);
    
    // Create async client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Spawn task to handle incoming messages
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(notification) => {
                    println!("Notification: {:?}", notification);
                }
                Err(e) => {
                    eprintln!("Event loop error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Wait for connection to establish
    sleep(Duration::from_secs(1)).await;
    
    // Publish "online" status after connecting
    client.publish(
        "devices/monitor_rust/status",
        QoS::AtLeastOnce,
        true,  // retain
        "online"
    ).await?;
    
    println!("Published online status");
    println!("Device running... (Press Ctrl+C to simulate crash)");
    
    // Simulate device operation
    loop {
        sleep(Duration::from_secs(5)).await;
        println!("Device heartbeat...");
        
        // You could publish telemetry or other data here
        client.publish(
            "devices/monitor_rust/telemetry",
            QoS::AtMostOnce,
            false,
            format!("{{\"timestamp\": {}}}", 
                    std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)?
                        .as_secs())
        ).await?;
    }
    
    // This won't be reached in crash scenario
    // In graceful shutdown:
    // client.publish("devices/monitor_rust/status", QoS::AtLeastOnce, 
    //                true, "offline").await?;
    // client.disconnect().await?;
}
```

### Rust Example: Status Monitor (Subscriber)

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use tokio::time::{sleep, Duration};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let mut mqtt_options = MqttOptions::new(
        "StatusMonitor",
        "localhost",
        1883
    );
    
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Subscribe to all device status topics
    client.subscribe("devices/+/status", QoS::AtLeastOnce).await?;
    println!("Monitoring device statuses...");
    
    // Process incoming messages
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::Publish(p))) => {
                let topic = p.topic.clone();
                let payload = String::from_utf8_lossy(&p.payload);
                
                println!("\n[{}] Device Status Update:", 
                         chrono::Local::now().format("%H:%M:%S"));
                println!("  Topic: {}", topic);
                println!("  Status: {}", payload);
                println!("  Retained: {}", p.retain);
                
                // Extract device ID from topic
                if let Some(device_id) = topic.split('/').nth(1) {
                    match payload.as_ref() {
                        "online" => println!("  ✓ Device '{}' is ONLINE", device_id),
                        "offline" => println!("  ✗ Device '{}' went OFFLINE", device_id),
                        _ => println!("  ? Unknown status for '{}'", device_id),
                    }
                }
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Connection error: {:?}", e);
                sleep(Duration::from_secs(1)).await;
            }
        }
    }
}
```

## Summary

**Last Will and Testament (LWT)** is an essential MQTT feature for building robust IoT and distributed systems. It enables automatic notification when clients disconnect unexpectedly, making it invaluable for:

- **Reliability**: Detect and respond to device failures automatically
- **System monitoring**: Track the health and availability of connected devices
- **User experience**: Update presence indicators in real-time applications
- **Automation**: Trigger failover or recovery procedures when services go offline

Key implementation points across all languages:
1. Configure LWT during connection setup (before connecting)
2. Set appropriate topic, message, QoS, and retain flag
3. Publish "online" status immediately after successful connection
4. The broker automatically publishes the LWT message on unexpected disconnection
5. For graceful shutdowns, manually publish offline status before disconnecting

LWT works seamlessly with MQTT's Quality of Service levels and retained messages to ensure status updates are reliably delivered and persist for late-joining subscribers, making it a cornerstone feature for production MQTT deployments.