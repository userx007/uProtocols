# MQTT v5 Properties: A Comprehensive Guide

## Overview

MQTT v5.0, released in 2019, introduced a significant enhancement to the protocol through **Properties**. These are optional metadata fields that can be attached to most MQTT control packets, providing extended functionality, improved diagnostics, and better control over message behavior. This represents one of the most substantial improvements over MQTT v3.1.1.

## What Are MQTT v5 Properties?

Properties are key-value pairs that extend MQTT packets with additional information. Each property has:
- A **Property Identifier** (1-byte value)
- A **Property Value** (type varies by property)

Properties enable features like:
- **User Properties**: Custom key-value metadata
- **Reason Codes**: Detailed success/failure information
- **Content Type**: Message format specification
- **Response Topics**: Request-response patterns
- **Message Expiry**: Time-to-live for messages
- **Server Keep Alive**: Server-suggested connection intervals

## Key Property Categories

### 1. **Message Properties**
- **Payload Format Indicator**: Specifies if payload is UTF-8 text or binary
- **Content Type**: MIME type of the message content
- **Response Topic**: Topic for response messages (request/response pattern)
- **Correlation Data**: Links request and response messages

### 2. **Connection Properties**
- **Session Expiry Interval**: How long the server retains session state
- **Receive Maximum**: Maximum QoS 1 and 2 messages the client can handle
- **Maximum Packet Size**: Largest packet size supported
- **Topic Alias Maximum**: Number of topic aliases allowed

### 3. **Diagnostic Properties**
- **Reason String**: Human-readable error description
- **User Properties**: Arbitrary key-value pairs for custom metadata

### 4. **Flow Control Properties**
- **Server Keep Alive**: Server's suggested keep-alive value
- **Request Response Information**: Client requests response info from server

## Code Examples

### C/C++ Implementation (Using Eclipse Paho MQTT C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.hivemq.com:1883"
#define CLIENTID    "MQTTv5_Properties_Example"
#define TOPIC       "sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Publish with MQTT v5 Properties
int publish_with_properties(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    MQTTProperties props = MQTTProperties_initializer;
    MQTTProperty property;
    int rc;
    
    // Set message payload
    char payload[50];
    sprintf(payload, "{\"temp\": 22.5, \"unit\": \"celsius\"}");
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    // Add Payload Format Indicator (1 = UTF-8 string)
    property.identifier = MQTTPROPERTY_CODE_PAYLOAD_FORMAT_INDICATOR;
    property.value.byte = 1;
    MQTTProperties_add(&props, &property);
    
    // Add Content Type
    property.identifier = MQTTPROPERTY_CODE_CONTENT_TYPE;
    property.value.data.data = "application/json";
    property.value.data.len = strlen("application/json");
    MQTTProperties_add(&props, &property);
    
    // Add Message Expiry Interval (300 seconds)
    property.identifier = MQTTPROPERTY_CODE_MESSAGE_EXPIRY_INTERVAL;
    property.value.integer4 = 300;
    MQTTProperties_add(&props, &property);
    
    // Add User Properties (custom metadata)
    property.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
    property.value.data.data = "sensor_id";
    property.value.data.len = strlen("sensor_id");
    property.value.value.data = "temp_001";
    property.value.value.len = strlen("temp_001");
    MQTTProperties_add(&props, &property);
    
    property.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
    property.value.data.data = "location";
    property.value.data.len = strlen("location");
    property.value.value.data = "warehouse_a";
    property.value.value.len = strlen("warehouse_a");
    MQTTProperties_add(&props, &property);
    
    // Publish with properties
    MQTTResponse response = MQTTClient_publishMessage5(client, TOPIC, &pubmsg, &props, &token);
    
    if (response.reasonCode != MQTTREASONCODE_SUCCESS) {
        printf("Failed to publish, reason code: %d\n", response.reasonCode);
        if (response.properties && response.properties->count > 0) {
            // Check for Reason String property
            MQTTProperty* reasonString = MQTTProperties_getProperty(
                response.properties, MQTTPROPERTY_CODE_REASON_STRING);
            if (reasonString) {
                printf("Reason: %.*s\n", 
                    reasonString->value.data.len, 
                    reasonString->value.data.data);
            }
        }
        rc = -1;
    } else {
        printf("Message published successfully\n");
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    }
    
    MQTTProperties_free(&props);
    MQTTResponse_free(response);
    return rc;
}

// Subscribe with properties and handle incoming messages
int message_arrived(void *context, char *topicName, int topicLen, 
                   MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Access MQTT v5 properties
    MQTTProperties *props = &message->properties;
    
    // Check Content Type
    MQTTProperty* contentType = MQTTProperties_getProperty(
        props, MQTTPROPERTY_CODE_CONTENT_TYPE);
    if (contentType) {
        printf("Content-Type: %.*s\n", 
            contentType->value.data.len, 
            contentType->value.data.data);
    }
    
    // Check User Properties
    MQTTProperty* userProp = MQTTProperties_getPropertyAt(
        props, MQTTPROPERTY_CODE_USER_PROPERTY, 0);
    int index = 0;
    while (userProp) {
        printf("User Property: %.*s = %.*s\n",
            userProp->value.data.len, userProp->value.data.data,
            userProp->value.value.len, userProp->value.value.data);
        index++;
        userProp = MQTTProperties_getPropertyAt(
            props, MQTTPROPERTY_CODE_USER_PROPERTY, index);
    }
    
    // Check Response Topic (for request/response pattern)
    MQTTProperty* responseTopic = MQTTProperties_getProperty(
        props, MQTTPROPERTY_CODE_RESPONSE_TOPIC);
    if (responseTopic) {
        printf("Response requested to topic: %.*s\n",
            responseTopic->value.data.len,
            responseTopic->value.data.data);
        
        // Send response (implementation would go here)
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer5;
    MQTTProperties connect_props = MQTTProperties_initializer;
    MQTTProperty property;
    int rc;
    
    // Create client
    MQTTClient_create(&client, ADDRESS, CLIENTID, 
                     MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set connection properties for MQTT v5
    conn_opts.MQTTVersion = MQTTVERSION_5;
    conn_opts.cleanstart = 1;
    
    // Set Session Expiry Interval
    property.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    property.value.integer4 = 3600; // 1 hour
    MQTTProperties_add(&connect_props, &property);
    
    // Set Receive Maximum
    property.identifier = MQTTPROPERTY_CODE_RECEIVE_MAXIMUM;
    property.value.integer2 = 100;
    MQTTProperties_add(&connect_props, &property);
    
    conn_opts.connectProperties = &connect_props;
    
    // Set callback
    MQTTClient_setCallbacks(client, NULL, NULL, message_arrived, NULL);
    
    // Connect
    MQTTResponse response = MQTTClient_connect5(client, &conn_opts, 
                                                NULL, NULL);
    if (response.reasonCode != MQTTREASONCODE_SUCCESS) {
        printf("Failed to connect, reason code: %d\n", response.reasonCode);
        MQTTProperties_free(&connect_props);
        MQTTResponse_free(response);
        return EXIT_FAILURE;
    }
    
    printf("Connected successfully\n");
    
    // Publish message
    publish_with_properties(client);
    
    // Cleanup
    MQTTProperties_free(&connect_props);
    MQTTResponse_free(response);
    MQTTClient_disconnect5(client, 5000, MQTTREASONCODE_NORMAL_DISCONNECTION, NULL);
    MQTTClient_destroy(&client);
    
    return EXIT_SUCCESS;
}
```

### Rust Implementation (Using rumqttc)

```rust
use rumqttc::{
    Client, MqttOptions, QoS, Event, Packet, 
    v5::mqttbytes::v5::{ConnectProperties, PublishProperties, Property},
};
use std::time::Duration;
use std::thread;

fn main() {
    // Configure MQTT v5 client
    let mut mqttoptions = MqttOptions::new("mqtt_v5_properties_client", "broker.hivemq.com", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Set connection properties
    let mut connect_props = ConnectProperties::new();
    
    // Session Expiry Interval: 1 hour
    connect_props.session_expiry_interval = Some(3600);
    
    // Receive Maximum: max 100 in-flight messages
    connect_props.receive_maximum = Some(100);
    
    // Maximum Packet Size: 1MB
    connect_props.max_packet_size = Some(1024 * 1024);
    
    // User Properties for connection
    connect_props.user_properties.push((
        "client_version".to_string(),
        "1.0.0".to_string()
    ));
    
    mqttoptions.set_connect_properties(connect_props);
    
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn thread to handle incoming messages
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(Event::Incoming(Packet::Publish(publish))) => {
                    println!("Topic: {}", publish.topic);
                    println!("Payload: {:?}", String::from_utf8_lossy(&publish.payload));
                    println!("QoS: {:?}", publish.qos);
                    
                    // Access MQTT v5 properties
                    if let Some(props) = &publish.properties {
                        // Content Type
                        if let Some(content_type) = &props.content_type {
                            println!("Content-Type: {}", content_type);
                        }
                        
                        // Payload Format Indicator
                        if let Some(format) = props.payload_format_indicator {
                            println!("Payload Format: {}", 
                                if format == 1 { "UTF-8" } else { "Binary" });
                        }
                        
                        // Message Expiry Interval
                        if let Some(expiry) = props.message_expiry_interval {
                            println!("Message expires in {} seconds", expiry);
                        }
                        
                        // Response Topic (for request/response)
                        if let Some(response_topic) = &props.response_topic {
                            println!("Response topic: {}", response_topic);
                            
                            // Could send response here
                        }
                        
                        // Correlation Data
                        if let Some(correlation) = &props.correlation_data {
                            println!("Correlation Data: {:?}", correlation);
                        }
                        
                        // User Properties
                        for (key, value) in &props.user_properties {
                            println!("User Property: {} = {}", key, value);
                        }
                    }
                    println!("---");
                }
                Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                    println!("Connected! Session present: {}", connack.session_present);
                    
                    // Check server properties
                    if let Some(props) = &connack.properties {
                        if let Some(keep_alive) = props.server_keep_alive {
                            println!("Server keep-alive: {} seconds", keep_alive);
                        }
                        
                        if let Some(max_qos) = props.max_qos {
                            println!("Maximum QoS: {:?}", max_qos);
                        }
                        
                        if let Some(reason) = &props.reason_string {
                            println!("Reason: {}", reason);
                        }
                    }
                }
                Ok(Event::Incoming(Packet::SubAck(suback))) => {
                    println!("Subscription acknowledged");
                    
                    // Check reason codes
                    for reason_code in &suback.return_codes {
                        println!("Subscription reason code: {:?}", reason_code);
                    }
                    
                    if let Some(props) = &suback.properties {
                        if let Some(reason) = &props.reason_string {
                            println!("Subscription reason: {}", reason);
                        }
                    }
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Wait for connection
    thread::sleep(Duration::from_secs(2));
    
    // Subscribe to topic
    client.subscribe("sensors/temperature", QoS::AtLeastOnce).unwrap();
    
    // Publish message with properties
    publish_with_properties(&client);
    
    // Keep running
    thread::sleep(Duration::from_secs(10));
}

fn publish_with_properties(client: &Client) {
    let topic = "sensors/temperature";
    let payload = r#"{"temp": 22.5, "unit": "celsius"}"#;
    
    // Create publish properties
    let mut pub_props = PublishProperties::default();
    
    // Payload Format Indicator (1 = UTF-8 text)
    pub_props.payload_format_indicator = Some(1);
    
    // Content Type
    pub_props.content_type = Some("application/json".to_string());
    
    // Message Expiry Interval (5 minutes)
    pub_props.message_expiry_interval = Some(300);
    
    // Response Topic (for request/response pattern)
    pub_props.response_topic = Some("sensors/temperature/response".to_string());
    
    // Correlation Data (to match request and response)
    pub_props.correlation_data = Some(vec![1, 2, 3, 4]);
    
    // User Properties (custom metadata)
    pub_props.user_properties.push((
        "sensor_id".to_string(),
        "temp_001".to_string()
    ));
    
    pub_props.user_properties.push((
        "location".to_string(),
        "warehouse_a".to_string()
    ));
    
    pub_props.user_properties.push((
        "timestamp".to_string(),
        chrono::Utc::now().to_rfc3339()
    ));
    
    // Publish with properties
    match client.publish_with_properties(
        topic,
        QoS::AtLeastOnce,
        false,
        payload.as_bytes(),
        pub_props
    ) {
        Ok(_) => println!("Published message with properties"),
        Err(e) => eprintln!("Failed to publish: {:?}", e),
    }
}

// Example: Request/Response Pattern
fn request_response_pattern(client: &Client) {
    let request_topic = "service/calculate";
    let response_topic = "service/calculate/response/client123";
    
    // Subscribe to response topic first
    client.subscribe(&response_topic, QoS::AtLeastOnce).unwrap();
    
    // Create request with response topic
    let mut props = PublishProperties::default();
    props.response_topic = Some(response_topic.to_string());
    props.correlation_data = Some(b"request-001".to_vec());
    props.content_type = Some("application/json".to_string());
    
    let request_payload = r#"{"operation": "add", "a": 5, "b": 3}"#;
    
    client.publish_with_properties(
        request_topic,
        QoS::AtLeastOnce,
        false,
        request_payload.as_bytes(),
        props
    ).unwrap();
    
    println!("Request sent, waiting for response...");
}

// Example: Handling Reason Codes
fn handle_reason_codes() {
    use rumqttc::v5::mqttbytes::v5::ConnectReturnCode;
    
    // Simulate handling different reason codes
    let reason_code = ConnectReturnCode::Success;
    
    match reason_code {
        ConnectReturnCode::Success => {
            println!("Connection successful");
        }
        ConnectReturnCode::UnspecifiedError => {
            eprintln!("Unspecified error occurred");
        }
        ConnectReturnCode::MalformedPacket => {
            eprintln!("Malformed packet sent");
        }
        ConnectReturnCode::QuotaExceeded => {
            eprintln!("Quota exceeded - rate limiting in effect");
        }
        ConnectReturnCode::NotAuthorized => {
            eprintln!("Not authorized to connect");
        }
        _ => {
            eprintln!("Other error: {:?}", reason_code);
        }
    }
}
```

## Summary

**MQTT v5 Properties** revolutionized the protocol by adding extensibility and enhanced functionality without breaking backward compatibility. The key improvements include:

1. **User Properties** enable custom metadata attachment to any packet, allowing application-specific information flow without payload modification
2. **Reason Codes and Reason Strings** provide detailed diagnostic information for connection failures, subscription issues, and message delivery problems
3. **Content Type and Payload Format Indicator** clarify message formats, enabling better payload handling
4. **Request/Response Pattern** support through Response Topic and Correlation Data properties facilitates RPC-style communication
5. **Flow Control Properties** like Receive Maximum and Maximum Packet Size allow better resource management
6. **Message Expiry** prevents stale messages from being delivered to offline clients
7. **Session Management** improvements through Session Expiry Interval give fine-grained control over persistent sessions

These properties make MQTT v5 significantly more powerful for modern IoT and messaging applications, providing better observability, flexibility, and control while maintaining the protocol's lightweight nature. The examples demonstrate how to leverage these properties in both C/C++ and Rust for building robust MQTT applications with enhanced capabilities.