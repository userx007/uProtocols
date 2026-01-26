# MQTT v3.1.1 vs v5.0 Migration

## Overview

MQTT v5.0, released in 2019, represents a significant evolution from v3.1.1 (2014). The upgrade introduces enhanced features for modern IoT applications while maintaining backward compatibility considerations. Understanding the differences and migration strategies is crucial for developers looking to leverage v5.0's capabilities.

## Key Differences Between v3.1.1 and v5.0

### 1. **Enhanced Error Reporting**
- **v3.1.1**: Limited reason codes (0x00-0x05)
- **v5.0**: Comprehensive reason codes (140+ codes) with descriptive error messages

### 2. **User Properties**
- **v5.0**: Custom key-value pairs can be attached to any packet type
- **v3.1.1**: No support for custom metadata

### 3. **Session Expiry**
- **v5.0**: Configurable session expiry interval (separate from connection)
- **v3.1.1**: Sessions tied to `cleanSession` flag only

### 4. **Message Expiry**
- **v5.0**: Per-message expiry intervals
- **v3.1.1**: No native message expiry

### 5. **Topic Aliases**
- **v5.0**: Reduces bandwidth by using numeric aliases for topic names
- **v3.1.1**: Full topic name required in every PUBLISH

### 6. **Flow Control**
- **v5.0**: Receive Maximum property limits in-flight QoS 1 and 2 messages
- **v3.1.1**: No built-in flow control

### 7. **Request/Response Pattern**
- **v5.0**: Native support with Response Topic and Correlation Data
- **v3.1.1**: Must be implemented manually

### 8. **Shared Subscriptions**
- **v5.0**: Built-in shared subscription support (`$share/group/topic`)
- **v3.1.1**: Broker-specific extensions required

### 9. **Server Capabilities**
- **v5.0**: Servers advertise capabilities in CONNACK
- **v3.1.1**: No capability negotiation

## C/C++ Migration Examples

### Using Eclipse Paho MQTT C Library

```c
// MQTT v3.1.1 Connection
#include "MQTTClient.h"

void connect_v3(void) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    MQTTClient_create(&client, "tcp://broker.example.com:1883", 
                      "ClientID_v3", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleansession = 1;
    conn_opts.MQTTVersion = MQTTVERSION_3_1_1;
    
    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
    }
}

// MQTT v5.0 Connection with Enhanced Features
void connect_v5(void) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer5;
    MQTTProperties connect_props = MQTTProperties_initializer;
    MQTTProperty property;
    
    MQTTClient_create(&client, "tcp://broker.example.com:1883", 
                      "ClientID_v5", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleanstart = 1;
    conn_opts.MQTTVersion = MQTTVERSION_5;
    
    // Session Expiry Interval (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    property.value.integer4 = 3600; // 1 hour
    MQTTProperties_add(&connect_props, &property);
    
    // Maximum Packet Size (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_MAXIMUM_PACKET_SIZE;
    property.value.integer4 = 65536;
    MQTTProperties_add(&connect_props, &property);
    
    // User Properties (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
    property.value.data.data = "application";
    property.value.data.len = strlen("application");
    property.value.value.data = "sensor-gateway";
    property.value.value.len = strlen("sensor-gateway");
    MQTTProperties_add(&connect_props, &property);
    
    conn_opts.connectProperties = &connect_props;
    
    MQTTResponse response = MQTTClient_connect5(client, &conn_opts, 
                                                 NULL, NULL);
    if (response.reasonCode != MQTTREASONCODE_SUCCESS) {
        printf("Failed to connect, reason code %d: %s\n", 
               response.reasonCode, 
               MQTTReasonCode_toString(response.reasonCode));
    } else {
        printf("Connected successfully\n");
        // Check server capabilities from CONNACK
        MQTTProperties *props = response.properties;
        // Process server-sent properties...
    }
    
    MQTTProperties_free(&connect_props);
    MQTTResponse_free(response);
}
```

### Publishing with Message Expiry and Topic Alias

```c
// v3.1.1 - Basic Publish
void publish_v3(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    pubmsg.payload = "Temperature: 25.5C";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, "sensors/temperature", 
                               &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, 10000);
}

// v5.0 - Enhanced Publish with Expiry and Properties
void publish_v5(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTProperties props = MQTTProperties_initializer;
    MQTTProperty property;
    MQTTClient_deliveryToken token;
    
    pubmsg.payload = "Temperature: 25.5C";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    
    // Message Expiry Interval (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_MESSAGE_EXPIRY_INTERVAL;
    property.value.integer4 = 300; // Expires in 5 minutes
    MQTTProperties_add(&props, &property);
    
    // Topic Alias (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_TOPIC_ALIAS;
    property.value.integer2 = 1; // Use alias 1
    MQTTProperties_add(&props, &property);
    
    // Content Type (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_CONTENT_TYPE;
    property.value.data.data = "text/plain";
    property.value.data.len = strlen("text/plain");
    MQTTProperties_add(&props, &property);
    
    // User Property for custom metadata
    property.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
    property.value.data.data = "sensor-id";
    property.value.data.len = strlen("sensor-id");
    property.value.value.data = "TEMP001";
    property.value.value.len = strlen("TEMP001");
    MQTTProperties_add(&props, &property);
    
    pubmsg.properties = props;
    
    MQTTClient_publishMessage5(client, "sensors/temperature", 
                                &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, 10000);
    
    MQTTProperties_free(&props);
}
```

### Request/Response Pattern

```c
// v5.0 - Native Request/Response Support
void request_response_v5(MQTTClient client) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTProperties props = MQTTProperties_initializer;
    MQTTProperty property;
    MQTTClient_deliveryToken token;
    
    // Subscribe to response topic first
    MQTTClient_subscribe(client, "responses/client123", 1);
    
    // Prepare request message
    pubmsg.payload = "GET_CONFIG";
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = 1;
    
    // Response Topic (new in v5.0)
    property.identifier = MQTTPROPERTY_CODE_RESPONSE_TOPIC;
    property.value.data.data = "responses/client123";
    property.value.data.len = strlen("responses/client123");
    MQTTProperties_add(&props, &property);
    
    // Correlation Data (new in v5.0)
    const char* correlation = "REQ-12345";
    property.identifier = MQTTPROPERTY_CODE_CORRELATION_DATA;
    property.value.data.data = (void*)correlation;
    property.value.data.len = strlen(correlation);
    MQTTProperties_add(&props, &property);
    
    pubmsg.properties = props;
    
    MQTTClient_publishMessage5(client, "requests/config", &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, 10000);
    
    MQTTProperties_free(&props);
}
```

## Rust Migration Examples

### Using rumqttc Library

```rust
// MQTT v3.1.1 Connection
use rumqttc::{MqttOptions, Client, QoS};
use std::time::Duration;

fn connect_v3() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new("client_v3", "broker.example.com", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    mqttoptions.set_clean_session(true);
    
    let (client, mut connection) = Client::new(mqttoptions, 10);
    
    // Handle connection in separate thread
    std::thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(event) => println!("Event: {:?}", event),
                Err(e) => eprintln!("Error: {:?}", e),
            }
        }
    });
    
    // Publish message
    client.publish("sensors/temperature", QoS::AtLeastOnce, false, 
                   "Temperature: 25.5C")?;
    
    Ok(())
}

// MQTT v5.0 Connection with Enhanced Features
use rumqttc::v5::{
    MqttOptions as MqttOptions5, 
    AsyncClient, 
    EventLoop,
};
use rumqttc::v5::mqttbytes::v5::{
    ConnectProperties, 
    PublishProperties,
    Property,
};
use std::collections::HashMap;

async fn connect_v5() -> Result<(), Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions5::new("client_v5", "broker.example.com", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    mqttoptions.set_clean_start(true);
    
    // Configure v5.0 specific properties
    let mut connect_props = ConnectProperties::new();
    
    // Session Expiry Interval (new in v5.0)
    connect_props.session_expiry_interval = Some(3600); // 1 hour
    
    // Maximum Packet Size (new in v5.0)
    connect_props.max_packet_size = Some(65536);
    
    // Receive Maximum for flow control (new in v5.0)
    connect_props.receive_max = Some(100);
    
    // User Properties (new in v5.0)
    let mut user_properties = HashMap::new();
    user_properties.insert("application".to_string(), "sensor-gateway".to_string());
    user_properties.insert("version".to_string(), "1.0.0".to_string());
    connect_props.user_properties = user_properties;
    
    mqttoptions.set_connect_properties(connect_props);
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Handle connection events
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(event) => {
                    println!("Event: {:?}", event);
                    // Check CONNACK properties for server capabilities
                }
                Err(e) => {
                    eprintln!("Error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    Ok(())
}
```

### Publishing with v5.0 Features

```rust
use rumqttc::v5::mqttbytes::v5::{PublishProperties, Property};
use rumqttc::v5::AsyncClient;
use rumqttc::QoS;

async fn publish_with_v5_features(client: &AsyncClient) 
    -> Result<(), Box<dyn std::error::Error>> {
    
    let topic = "sensors/temperature";
    let payload = "Temperature: 25.5C";
    
    // Create v5.0 publish properties
    let mut props = PublishProperties::new();
    
    // Message Expiry Interval (new in v5.0)
    props.message_expiry_interval = Some(300); // 5 minutes
    
    // Content Type (new in v5.0)
    props.content_type = Some("text/plain".to_string());
    
    // Response Topic for request/response (new in v5.0)
    props.response_topic = Some("responses/sensor001".to_string());
    
    // Correlation Data (new in v5.0)
    props.correlation_data = Some(b"REQ-12345".to_vec());
    
    // Topic Alias (new in v5.0)
    props.topic_alias = Some(1);
    
    // User Properties (new in v5.0)
    let mut user_props = HashMap::new();
    user_props.insert("sensor-id".to_string(), "TEMP001".to_string());
    user_props.insert("location".to_string(), "warehouse-a".to_string());
    props.user_properties = user_props;
    
    // Payload Format Indicator (new in v5.0)
    props.payload_format_indicator = Some(1); // UTF-8 string
    
    client.publish_with_properties(
        topic,
        QoS::AtLeastOnce,
        false,
        payload,
        props
    ).await?;
    
    Ok(())
}
```

### Shared Subscriptions (v5.0)

```rust
use rumqttc::v5::AsyncClient;
use rumqttc::QoS;

async fn shared_subscription_v5(client: &AsyncClient) 
    -> Result<(), Box<dyn std::error::Error>> {
    
    // Shared subscription - multiple clients in same group
    // share load for this topic
    // Format: $share/{group}/{topic}
    let shared_topic = "$share/worker-group/jobs/process";
    
    client.subscribe(shared_topic, QoS::AtLeastOnce).await?;
    
    println!("Subscribed to shared topic: {}", shared_topic);
    println!("Messages will be load-balanced across group members");
    
    Ok(())
}
```

### Handling Enhanced Error Codes

```rust
use rumqttc::v5::mqttbytes::v5::ConnectReturnCode;

fn handle_v5_errors(return_code: ConnectReturnCode) {
    match return_code {
        ConnectReturnCode::Success => {
            println!("Connected successfully");
        }
        ConnectReturnCode::UnspecifiedError => {
            eprintln!("Unspecified error occurred");
        }
        ConnectReturnCode::MalformedPacket => {
            eprintln!("Malformed packet sent");
        }
        ConnectReturnCode::ProtocolError => {
            eprintln!("Protocol error");
        }
        ConnectReturnCode::ImplementationSpecificError => {
            eprintln!("Implementation-specific error");
        }
        ConnectReturnCode::UnsupportedProtocolVersion => {
            eprintln!("MQTT v5.0 not supported by broker");
        }
        ConnectReturnCode::ClientIdentifierNotValid => {
            eprintln!("Client ID rejected");
        }
        ConnectReturnCode::BadUserNamePassword => {
            eprintln!("Authentication failed");
        }
        ConnectReturnCode::NotAuthorized => {
            eprintln!("Not authorized to connect");
        }
        ConnectReturnCode::ServerUnavailable => {
            eprintln!("Server unavailable");
        }
        ConnectReturnCode::ServerBusy => {
            eprintln!("Server busy, retry later");
        }
        ConnectReturnCode::Banned => {
            eprintln!("Client banned from server");
        }
        ConnectReturnCode::BadAuthenticationMethod => {
            eprintln!("Authentication method not supported");
        }
        ConnectReturnCode::TopicNameInvalid => {
            eprintln!("Topic name invalid");
        }
        ConnectReturnCode::PacketTooLarge => {
            eprintln!("Packet exceeds maximum size");
        }
        ConnectReturnCode::QuotaExceeded => {
            eprintln!("Quota exceeded");
        }
        ConnectReturnCode::PayloadFormatInvalid => {
            eprintln!("Payload format invalid");
        }
        ConnectReturnCode::RetainNotSupported => {
            eprintln!("Retain not supported");
        }
        ConnectReturnCode::QoSNotSupported => {
            eprintln!("QoS level not supported");
        }
        ConnectReturnCode::UseAnotherServer => {
            eprintln!("Client should use another server");
        }
        ConnectReturnCode::ServerMoved => {
            eprintln!("Server has moved");
        }
        ConnectReturnCode::ConnectionRateExceeded => {
            eprintln!("Connection rate limit exceeded");
        }
    }
}
```

## Migration Strategy

### 1. **Assessment Phase**
```rust
// Check broker v5.0 support
async fn check_broker_compatibility(broker: &str, port: u16) 
    -> Result<bool, Box<dyn std::error::Error>> {
    
    // Try v5.0 connection
    let mut mqttoptions = MqttOptions5::new("test_client", broker, port);
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    match eventloop.poll().await {
        Ok(_) => {
            println!("Broker supports MQTT v5.0");
            Ok(true)
        }
        Err(e) => {
            println!("Broker may not support v5.0: {:?}", e);
            Ok(false)
        }
    }
}
```

### 2. **Dual-Version Support**
```c
// Wrapper function supporting both versions
typedef enum {
    MQTT_VERSION_3_1_1,
    MQTT_VERSION_5_0
} MQTTVersionType;

int connect_mqtt(MQTTClient client, MQTTVersionType version) {
    if (version == MQTT_VERSION_5_0) {
        // Use v5.0 connection logic
        MQTTClient_connectOptions conn_opts = 
            MQTTClient_connectOptions_initializer5;
        conn_opts.MQTTVersion = MQTTVERSION_5;
        // ... v5.0 specific setup
        return MQTTClient_connect5(client, &conn_opts, NULL, NULL).reasonCode;
    } else {
        // Use v3.1.1 connection logic
        MQTTClient_connectOptions conn_opts = 
            MQTTClient_connectOptions_initializer;
        conn_opts.MQTTVersion = MQTTVERSION_3_1_1;
        return MQTTClient_connect(client, &conn_opts);
    }
}
```

### 3. **Gradual Feature Adoption**
```rust
// Start with basic v5.0, gradually add features
struct MqttConfig {
    use_message_expiry: bool,
    use_topic_aliases: bool,
    use_user_properties: bool,
    use_request_response: bool,
}

impl MqttConfig {
    fn basic_v5() -> Self {
        Self {
            use_message_expiry: false,
            use_topic_aliases: false,
            use_user_properties: false,
            use_request_response: false,
        }
    }
    
    fn full_v5() -> Self {
        Self {
            use_message_expiry: true,
            use_topic_aliases: true,
            use_user_properties: true,
            use_request_response: true,
        }
    }
}
```

## Compatibility Considerations

### Protocol Negotiation
- v5.0 clients CAN connect to v3.1.1 brokers by negotiating down
- v3.1.1 clients CANNOT connect to v5.0-only brokers
- Most brokers support both versions simultaneously

### Breaking Changes
1. **Clean Session vs Clean Start**: Semantics changed slightly
2. **Return Codes**: Completely different numbering and meanings
3. **UNSUBACK**: Now includes reason codes (v3.1.1 had none)

### Non-Breaking Enhancements
- All v5.0 properties are optional
- v3.1.1 behavior is subset of v5.0
- Can start with v5.0 connection without using new features

## Summary

**MQTT v5.0 Migration** involves upgrading from the widely-deployed v3.1.1 protocol to the feature-rich v5.0 specification. Key improvements include enhanced error reporting with 140+ reason codes, user properties for custom metadata, configurable session and message expiry, topic aliases for bandwidth optimization, built-in flow control, native request/response patterns, and standardized shared subscriptions.

Migration strategies typically involve assessing broker compatibility, implementing dual-version support during transition periods, and gradually adopting new features. The protocol maintains reasonable backward compatibility—v5.0 clients can negotiate down to v3.1.1, though the reverse isn't true. Both C/C++ (via Paho) and Rust (via rumqttc) ecosystems provide robust libraries supporting both versions, allowing developers to leverage v5.0's advanced capabilities like message expiry, correlation data, and enhanced diagnostics while maintaining compatibility with existing infrastructure. The migration path is straightforward for most applications, with the primary considerations being broker support verification and understanding the semantic differences in session management between the two versions.