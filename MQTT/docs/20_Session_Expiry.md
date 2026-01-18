# MQTT Session Expiry: A Comprehensive Guide

## Overview

Session Expiry is a feature introduced in MQTT v5 that allows clients and brokers to manage the lifecycle of session state more precisely. It determines how long the broker should retain session information (subscriptions, QoS 1 and QoS 2 messages) after a client disconnects. This is crucial for optimizing resource usage and ensuring reliable message delivery in IoT and messaging applications.

## Key Concepts

### Session State
When an MQTT client connects with **Clean Start = 0** (persistent session), the broker maintains:
- Client subscriptions
- QoS 1 and QoS 2 messages that haven't been acknowledged
- QoS 2 messages received but not yet completely acknowledged

### Session Expiry Interval
The **Session Expiry Interval** property specifies how many seconds the broker should retain the session state after the client disconnects. This value is set in seconds and can be:
- **0**: Session expires immediately when the client disconnects (similar to Clean Session = 1 in MQTT v3.1.1)
- **Greater than 0**: Session persists for the specified duration
- **0xFFFFFFFF (4,294,967,295)**: Session never expires (maximum value)

### How It Works
1. Client connects with a Session Expiry Interval value
2. Client disconnects (gracefully or ungracefully)
3. Broker maintains session state for the specified interval
4. If client reconnects within the interval, session state is restored
5. If interval expires, broker deletes the session state

## Code Examples

### C/C++ Implementation (Using Eclipse Paho MQTT C Library)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "SessionExpiryClient_C"
#define TOPIC       "test/session"
#define QOS         1
#define TIMEOUT     10000L

// Callback for successful connection
void onConnect(void* context, char* cause) {
    printf("Connected successfully\n");
}

// Callback for connection lost
void onConnectionLost(void* context, char* cause) {
    printf("Connection lost: %s\n", cause);
}

// Callback for message arrival
int onMessageArrived(void* context, char* topicName, int topicLen, 
                     MQTTClient_message* message) {
    printf("Message arrived on topic %s: %.*s\n", 
           topicName, message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_createOptions create_opts = MQTTClient_createOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create MQTT client with MQTT v5
    create_opts.MQTTVersion = MQTTVERSION_5;
    MQTTClient_createWithOptions(&client, ADDRESS, CLIENTID, 
                                 MQTTCLIENT_PERSISTENCE_NONE, NULL, &create_opts);

    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, onConnectionLost, 
                           onMessageArrived, NULL);

    // Configure connection options for MQTT v5
    conn_opts.MQTTVersion = MQTTVERSION_5;
    conn_opts.cleanstart = 0;  // Persistent session
    conn_opts.keepAliveInterval = 60;
    
    // Set session expiry interval to 300 seconds (5 minutes)
    MQTTProperties connect_props = MQTTProperties_initializer;
    MQTTProperty session_expiry;
    session_expiry.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    session_expiry.value.integer4 = 300;  // 5 minutes
    MQTTProperties_add(&connect_props, &session_expiry);
    
    conn_opts.connectProperties = &connect_props;

    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    printf("Connected with Session Expiry Interval: 300 seconds\n");

    // Subscribe to topic
    MQTTClient_subscribe(client, TOPIC, QOS);
    printf("Subscribed to topic: %s\n", TOPIC);

    // Publish a message
    pubmsg.payload = "Hello from Session Expiry Example";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    printf("Waiting for message delivery...\n");
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message delivered\n");

    // Simulate work
    printf("Client working... (disconnect to test session expiry)\n");
    MQTTClient_yield();  // Process callbacks
    
    // Clean disconnect with updated session expiry (optional)
    MQTTProperties disconnect_props = MQTTProperties_initializer;
    MQTTProperty disconnect_expiry;
    disconnect_expiry.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    disconnect_expiry.value.integer4 = 600;  // Extend to 10 minutes on disconnect
    MQTTProperties_add(&disconnect_props, &disconnect_expiry);
    
    MQTTClient_disconnect5(client, 0, MQTTREASONCODE_SUCCESS, &disconnect_props);
    printf("Disconnected with updated Session Expiry: 600 seconds\n");
    
    MQTTProperties_free(&connect_props);
    MQTTProperties_free(&disconnect_props);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

### C++ Implementation (Modern C++ with Paho MQTT C++)

```cpp
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS("tcp://broker.hivemq.com:1883");
const std::string CLIENT_ID("SessionExpiryClient_CPP");
const std::string TOPIC("test/session/cpp");
const int QOS = 1;

// Callback class for connection events
class Callback : public virtual mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "Connection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
        std::cout << "  Topic: " << msg->get_topic() << std::endl;
        std::cout << "  Payload: " << msg->to_string() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {
        std::cout << "Delivery complete for token: " 
                  << (token ? token->get_message_id() : -1) << std::endl;
    }
};

int main(int argc, char* argv[]) {
    try {
        // Create MQTT client
        mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
        
        Callback cb;
        client.set_callback(cb);

        // Configure connection options with session expiry
        mqtt::connect_options conn_opts;
        conn_opts.set_mqtt_version(MQTTVERSION_5);
        conn_opts.set_clean_start(false);  // Persistent session
        conn_opts.set_keep_alive_interval(std::chrono::seconds(60));
        
        // Set session expiry interval using properties
        mqtt::properties connect_props;
        connect_props.add(mqtt::property(
            mqtt::property::SESSION_EXPIRY_INTERVAL, 
            300  // 5 minutes in seconds
        ));
        conn_opts.set_properties(connect_props);

        // Connect to broker
        std::cout << "Connecting to broker..." << std::endl;
        mqtt::token_ptr conn_token = client.connect(conn_opts);
        conn_token->wait();
        std::cout << "Connected with Session Expiry Interval: 300 seconds" << std::endl;

        // Subscribe to topic
        client.subscribe(TOPIC, QOS)->wait();
        std::cout << "Subscribed to topic: " << TOPIC << std::endl;

        // Publish a message
        mqtt::message_ptr pub_msg = mqtt::make_message(
            TOPIC, 
            "Hello from C++ Session Expiry Example"
        );
        pub_msg->set_qos(QOS);
        client.publish(pub_msg)->wait();
        std::cout << "Message published" << std::endl;

        // Simulate work
        std::cout << "Client working for 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Disconnect with updated session expiry interval
        mqtt::properties disconnect_props;
        disconnect_props.add(mqtt::property(
            mqtt::property::SESSION_EXPIRY_INTERVAL, 
            600  // Extend to 10 minutes on disconnect
        ));
        
        mqtt::disconnect_options disc_opts;
        disc_opts.set_properties(disconnect_props);
        
        std::cout << "Disconnecting with updated Session Expiry: 600 seconds" << std::endl;
        client.disconnect(disc_opts)->wait();
        std::cout << "Disconnected successfully" << std::endl;

    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
```

### Rust Implementation (Using rumqttc Library)

```rust
use rumqttc::{
    AsyncClient, Event, EventLoop, MqttOptions, Packet, Property, 
    QoS, ConnectProperties, DisconnectProperties
};
use std::time::Duration;
use tokio::time::sleep;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqtt_opts = MqttOptions::new(
        "SessionExpiryClient_Rust",
        "broker.hivemq.com",
        1883
    );
    mqtt_opts.set_keep_alive(Duration::from_secs(60));
    mqtt_opts.set_clean_start(false); // Persistent session

    // Create connect properties with session expiry interval
    let mut connect_properties = ConnectProperties::new();
    connect_properties.session_expiry_interval = Some(300); // 5 minutes

    // Create client and event loop
    let (client, mut eventloop) = AsyncClient::new(mqtt_opts, 10);
    
    // Spawn task to handle incoming events
    tokio::spawn(async move {
        handle_events(&mut eventloop).await;
    });

    println!("Connecting to broker with Session Expiry Interval: 300 seconds");
    
    // Connect with properties
    client.connect_with_props(connect_properties).await?;
    
    // Wait for connection to be established
    sleep(Duration::from_secs(2)).await;
    println!("Connected successfully");

    // Subscribe to topic
    let topic = "test/session/rust";
    client.subscribe(topic, QoS::AtLeastOnce).await?;
    println!("Subscribed to topic: {}", topic);

    // Publish a message
    let payload = "Hello from Rust Session Expiry Example";
    client.publish(
        topic,
        QoS::AtLeastOnce,
        false,
        payload.as_bytes()
    ).await?;
    println!("Message published");

    // Simulate work
    println!("Client working for 5 seconds...");
    sleep(Duration::from_secs(5)).await;

    // Disconnect with updated session expiry interval
    let mut disconnect_properties = DisconnectProperties::new();
    disconnect_properties.session_expiry_interval = Some(600); // Extend to 10 minutes
    
    println!("Disconnecting with updated Session Expiry: 600 seconds");
    client.disconnect_with_props(disconnect_properties).await?;
    println!("Disconnected successfully");

    Ok(())
}

async fn handle_events(eventloop: &mut EventLoop) {
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                println!("Connection acknowledged");
                if let Some(props) = connack.properties {
                    if let Some(expiry) = props.session_expiry_interval {
                        println!("  Server-assigned Session Expiry: {} seconds", expiry);
                    }
                }
            }
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                println!("Message received on topic: {}", publish.topic);
                let payload = String::from_utf8_lossy(&publish.payload);
                println!("  Payload: {}", payload);
            }
            Ok(Event::Incoming(Packet::SubAck(_))) => {
                println!("Subscription acknowledged");
            }
            Ok(Event::Incoming(Packet::Disconnect(disconnect))) => {
                println!("Disconnect packet received");
                if let Some(props) = disconnect.properties {
                    if let Some(expiry) = props.session_expiry_interval {
                        println!("  Session Expiry: {} seconds", expiry);
                    }
                }
                break;
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Error in event loop: {:?}", e);
                break;
            }
        }
    }
}
```

### Advanced Example: Dynamic Session Management (Rust)

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, ConnectProperties};
use std::time::Duration;

async fn create_client_with_session_strategy(
    strategy: SessionStrategy
) -> Result<AsyncClient, Box<dyn std::error::Error>> {
    let mut mqtt_opts = MqttOptions::new(
        "DynamicSessionClient",
        "broker.hivemq.com",
        1883
    );
    mqtt_opts.set_keep_alive(Duration::from_secs(30));
    
    let mut connect_props = ConnectProperties::new();
    
    match strategy {
        SessionStrategy::NoSession => {
            mqtt_opts.set_clean_start(true);
            connect_props.session_expiry_interval = Some(0); // Immediate expiry
        }
        SessionStrategy::ShortLived => {
            mqtt_opts.set_clean_start(false);
            connect_props.session_expiry_interval = Some(60); // 1 minute
        }
        SessionStrategy::MediumLived => {
            mqtt_opts.set_clean_start(false);
            connect_props.session_expiry_interval = Some(3600); // 1 hour
        }
        SessionStrategy::LongLived => {
            mqtt_opts.set_clean_start(false);
            connect_props.session_expiry_interval = Some(86400); // 24 hours
        }
        SessionStrategy::Persistent => {
            mqtt_opts.set_clean_start(false);
            connect_props.session_expiry_interval = Some(0xFFFFFFFF); // Never expires
        }
    }
    
    let (client, mut eventloop) = AsyncClient::new(mqtt_opts, 10);
    
    tokio::spawn(async move {
        loop {
            if let Err(e) = eventloop.poll().await {
                eprintln!("Event loop error: {:?}", e);
                break;
            }
        }
    });
    
    client.connect_with_props(connect_props).await?;
    println!("Connected with strategy: {:?}", strategy);
    
    Ok(client)
}

#[derive(Debug)]
enum SessionStrategy {
    NoSession,      // Clean session, no persistence
    ShortLived,     // 1 minute - for temporary connections
    MediumLived,    // 1 hour - for typical IoT devices
    LongLived,      // 24 hours - for important devices
    Persistent,     // Never expires - for critical systems
}
```

## Best Practices

### 1. **Choose Appropriate Expiry Times**
- **Short-lived devices** (mobile apps): 60-300 seconds
- **IoT sensors** (regular reporting): 1-24 hours
- **Critical systems** (must not lose messages): 24+ hours or persistent
- **Temporary connections**: 0 seconds (no session)

### 2. **Update Session Expiry on Disconnect**
Clients can update the session expiry interval when disconnecting, allowing dynamic adjustment based on expected downtime:

```cpp
// Extend session if expecting longer downtime
disconnect_props.add(mqtt::property(
    mqtt::property::SESSION_EXPIRY_INTERVAL, 
    7200  // 2 hours
));
```

### 3. **Handle Server-Assigned Values**
Brokers may override client-requested session expiry intervals. Always check the CONNACK properties:

```rust
if let Some(expiry) = connack.properties.session_expiry_interval {
    println!("Server assigned expiry: {}", expiry);
}
```

### 4. **Monitor Resource Usage**
Long session expiry intervals consume broker resources. Balance reliability needs with resource constraints.

### 5. **Implement Reconnection Logic**
Always implement automatic reconnection with exponential backoff to reconnect within the session expiry interval.

## Summary

**Session Expiry** in MQTT v5 provides fine-grained control over session persistence, offering significant advantages over MQTT v3.1.1's binary Clean Session flag. Key takeaways:

- **Flexibility**: Configure exact session lifetime in seconds (0 to 4,294,967,295)
- **Resource Optimization**: Brokers can reclaim resources from expired sessions automatically
- **Dynamic Control**: Update session expiry on disconnect for adaptive behavior
- **Backward Compatibility**: Works alongside Clean Start flag for smooth migration
- **Use Cases**: Essential for IoT devices with intermittent connectivity, mobile applications, and mission-critical systems

The Session Expiry feature enables developers to build more resilient and resource-efficient MQTT applications by precisely matching session persistence to application requirements. Combined with proper reconnection strategies and QoS levels, it ensures reliable message delivery while optimizing broker and client resources.