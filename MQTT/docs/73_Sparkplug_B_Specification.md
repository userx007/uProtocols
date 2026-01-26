# Sparkplug B Specification

## Overview

Sparkplug B is a standardized MQTT topic namespace and payload specification designed specifically for industrial IoT (IIoT) applications, particularly in SCADA (Supervisory Control and Data Acquisition) and ICS (Industrial Control Systems) environments. It was developed by Cirrus Link Solutions and is now maintained by the Eclipse Foundation as part of the Eclipse Tahu project.

## Core Concepts

### Purpose
Sparkplug B addresses critical challenges in industrial environments:
- **Interoperability**: Provides a common language for devices, gateways, and SCADA systems
- **Auto-discovery**: Enables automatic detection of devices and their capabilities
- **State management**: Tracks online/offline status of edge devices and gateways
- **Data efficiency**: Uses Protocol Buffers for compact binary payloads
- **Birth/Death certificates**: Maintains system integrity through lifecycle management

### Architecture Components

1. **MQTT Infrastructure**: The underlying message broker
2. **Primary Application**: Central SCADA/HMI system that consumes data
3. **Edge of Network (EoN) Nodes**: Gateways that interface with field devices
4. **Devices**: Sensors, actuators, and other industrial equipment

### Topic Namespace Structure

```
spBv1.0/{group_id}/{message_type}/{edge_node_id}/{device_id}
```

**Components:**
- `spBv1.0`: Sparkplug B version 1.0 namespace
- `{group_id}`: Logical grouping (e.g., site, facility)
- `{message_type}`: NBIRTH, NDEATH, DBIRTH, DDEATH, NDATA, DDATA, NCMD, DCMD, STATE
- `{edge_node_id}`: Unique identifier for the gateway/edge node
- `{device_id}`: Unique identifier for the device (optional for node-level messages)

### Message Types

- **NBIRTH** (Node Birth): Published when an edge node comes online, includes all metrics
- **NDEATH** (Node Death): Last Will and Testament message when edge node disconnects
- **DBIRTH** (Device Birth): Published when a device connects to an edge node
- **DDEATH** (Device Death): Published when a device disconnects
- **NDATA** (Node Data): Runtime data from the edge node
- **DDATA** (Device Data): Runtime data from devices
- **NCMD** (Node Command): Commands sent to edge nodes
- **DCMD** (Device Command): Commands sent to devices
- **STATE** (State): Primary application state message

## C/C++ Implementation

```c
#include <stdio.h>
#include <string.h>
#include <mosquitto.h>
#include "tahu.pb-c.h" // Protocol Buffers generated header

#define MQTT_BROKER "localhost"
#define MQTT_PORT 1883
#define GROUP_ID "MyGroup"
#define EDGE_NODE_ID "Gateway01"
#define DEVICE_ID "Sensor01"

// Sparkplug B metric structure
typedef struct {
    char *name;
    uint64_t timestamp;
    uint32_t datatype;
    union {
        int64_t long_value;
        double double_value;
        bool bool_value;
        char *string_value;
    } value;
} SparkplugMetric;

// Build a Sparkplug B NBIRTH payload
void build_nbirth_payload(uint8_t **payload, size_t *payload_len) {
    // Create Sparkplug B payload using Protocol Buffers
    OrgEclipseTahuProtobufPayload msg = ORG_ECLIPSE_TAHU_PROTOBUF_PAYLOAD__INIT;
    
    // Set timestamp
    msg.timestamp = (uint64_t)time(NULL) * 1000; // milliseconds
    msg.has_timestamp = 1;
    
    // Set sequence number
    msg.seq = 0;
    msg.has_seq = 1;
    
    // Add metrics
    msg.n_metrics = 2;
    msg.metrics = malloc(sizeof(OrgEclipseTahuProtobufPayload__Metric*) * 2);
    
    // Metric 1: Temperature
    msg.metrics[0] = malloc(sizeof(OrgEclipseTahuProtobufPayload__Metric));
    org_eclipse_tahu_protobuf_payload__metric__init(msg.metrics[0]);
    msg.metrics[0]->name = "Temperature";
    msg.metrics[0]->timestamp = msg.timestamp;
    msg.metrics[0]->has_timestamp = 1;
    msg.metrics[0]->datatype = 9; // Double
    msg.metrics[0]->has_datatype = 1;
    msg.metrics[0]->has_double_value = 1;
    msg.metrics[0]->double_value = 25.5;
    
    // Metric 2: Node Control/Rebirth
    msg.metrics[1] = malloc(sizeof(OrgEclipseTahuProtobufPayload__Metric));
    org_eclipse_tahu_protobuf_payload__metric__init(msg.metrics[1]);
    msg.metrics[1]->name = "Node Control/Rebirth";
    msg.metrics[1]->timestamp = msg.timestamp;
    msg.metrics[1]->has_timestamp = 1;
    msg.metrics[1]->datatype = 11; // Boolean
    msg.metrics[1]->has_datatype = 1;
    msg.metrics[1]->has_boolean_value = 1;
    msg.metrics[1]->boolean_value = false;
    
    // Pack the message
    *payload_len = org_eclipse_tahu_protobuf_payload__get_packed_size(&msg);
    *payload = malloc(*payload_len);
    org_eclipse_tahu_protobuf_payload__pack(&msg, *payload);
    
    // Cleanup
    free(msg.metrics[0]);
    free(msg.metrics[1]);
    free(msg.metrics);
}

// Publish NBIRTH message
void publish_nbirth(struct mosquitto *mosq) {
    char topic[256];
    uint8_t *payload;
    size_t payload_len;
    
    // Build topic
    snprintf(topic, sizeof(topic), "spBv1.0/%s/NBIRTH/%s", GROUP_ID, EDGE_NODE_ID);
    
    // Build payload
    build_nbirth_payload(&payload, &payload_len);
    
    // Publish
    mosquitto_publish(mosq, NULL, topic, payload_len, payload, 0, false);
    
    printf("Published NBIRTH to %s\n", topic);
    
    free(payload);
}

// Publish NDATA message
void publish_ndata(struct mosquitto *mosq, const char *metric_name, double value) {
    char topic[256];
    uint8_t *payload;
    size_t payload_len;
    
    snprintf(topic, sizeof(topic), "spBv1.0/%s/NDATA/%s", GROUP_ID, EDGE_NODE_ID);
    
    // Create payload
    OrgEclipseTahuProtobufPayload msg = ORG_ECLIPSE_TAHU_PROTOBUF_PAYLOAD__INIT;
    msg.timestamp = (uint64_t)time(NULL) * 1000;
    msg.has_timestamp = 1;
    
    msg.n_metrics = 1;
    msg.metrics = malloc(sizeof(OrgEclipseTahuProtobufPayload__Metric*));
    msg.metrics[0] = malloc(sizeof(OrgEclipseTahuProtobufPayload__Metric));
    org_eclipse_tahu_protobuf_payload__metric__init(msg.metrics[0]);
    
    msg.metrics[0]->name = (char*)metric_name;
    msg.metrics[0]->has_double_value = 1;
    msg.metrics[0]->double_value = value;
    msg.metrics[0]->datatype = 9; // Double
    msg.metrics[0]->has_datatype = 1;
    
    payload_len = org_eclipse_tahu_protobuf_payload__get_packed_size(&msg);
    payload = malloc(payload_len);
    org_eclipse_tahu_protobuf_payload__pack(&msg, payload);
    
    mosquitto_publish(mosq, NULL, topic, payload_len, payload, 0, false);
    
    free(msg.metrics[0]);
    free(msg.metrics);
    free(payload);
}

int main() {
    struct mosquitto *mosq;
    char lwt_topic[256];
    
    mosquitto_lib_init();
    mosq = mosquitto_new(EDGE_NODE_ID, true, NULL);
    
    // Set Last Will and Testament (NDEATH)
    snprintf(lwt_topic, sizeof(lwt_topic), "spBv1.0/%s/NDEATH/%s", GROUP_ID, EDGE_NODE_ID);
    
    // Create minimal NDEATH payload
    OrgEclipseTahuProtobufPayload lwt_msg = ORG_ECLIPSE_TAHU_PROTOBUF_PAYLOAD__INIT;
    lwt_msg.timestamp = (uint64_t)time(NULL) * 1000;
    lwt_msg.has_timestamp = 1;
    
    size_t lwt_len = org_eclipse_tahu_protobuf_payload__get_packed_size(&lwt_msg);
    uint8_t *lwt_payload = malloc(lwt_len);
    org_eclipse_tahu_protobuf_payload__pack(&lwt_msg, lwt_payload);
    
    mosquitto_will_set(mosq, lwt_topic, lwt_len, lwt_payload, 0, false);
    free(lwt_payload);
    
    // Connect
    if (mosquitto_connect(mosq, MQTT_BROKER, MQTT_PORT, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect\n");
        return 1;
    }
    
    // Publish NBIRTH
    publish_nbirth(mosq);
    
    // Publish some data
    for (int i = 0; i < 5; i++) {
        sleep(5);
        publish_ndata(mosq, "Temperature", 20.0 + i);
    }
    
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

## Rust Implementation

```rust
use prost::Message;
use rumqttc::{Client, MqttOptions, QoS, LastWill};
use std::time::{SystemTime, UNIX_EPOCH};
use std::thread;
use std::time::Duration;

// Include the generated protobuf code
// You would generate this from sparkplug_b.proto using prost-build
pub mod sparkplug {
    include!(concat!(env!("OUT_DIR"), "/org.eclipse.tahu.protobuf.rs"));
}

use sparkplug::Payload;
use sparkplug::payload::Metric;

const MQTT_BROKER: &str = "localhost";
const MQTT_PORT: u16 = 1883;
const GROUP_ID: &str = "MyGroup";
const EDGE_NODE_ID: &str = "Gateway01";
const DEVICE_ID: &str = "Sensor01";

struct SparkplugNode {
    client: Client,
    sequence: u64,
}

impl SparkplugNode {
    fn new() -> Self {
        let mut mqttoptions = MqttOptions::new(EDGE_NODE_ID, MQTT_BROKER, MQTT_PORT);
        mqttoptions.set_keep_alive(Duration::from_secs(60));
        
        // Create NDEATH Last Will
        let ndeath_topic = format!("spBv1.0/{}/NDEATH/{}", GROUP_ID, EDGE_NODE_ID);
        let ndeath_payload = Self::create_death_payload();
        
        let lwt = LastWill::new(
            &ndeath_topic,
            &ndeath_payload,
            QoS::AtLeastOnce,
            false,
        );
        mqttoptions.set_last_will(lwt);
        
        let (client, mut connection) = Client::new(mqttoptions, 10);
        
        // Spawn connection handler
        thread::spawn(move || {
            for notification in connection.iter() {
                if let Err(e) = notification {
                    eprintln!("MQTT Error: {:?}", e);
                    break;
                }
            }
        });
        
        SparkplugNode {
            client,
            sequence: 0,
        }
    }
    
    fn get_timestamp() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_millis() as u64
    }
    
    fn create_death_payload() -> Vec<u8> {
        let payload = Payload {
            timestamp: Some(Self::get_timestamp()),
            metrics: vec![],
            seq: Some(0),
            uuid: None,
            body: None,
        };
        
        let mut buf = Vec::new();
        payload.encode(&mut buf).unwrap();
        buf
    }
    
    fn create_metric(name: &str, value: MetricValue) -> Metric {
        let timestamp = Self::get_timestamp();
        
        let (datatype, metric_value) = match value {
            MetricValue::Double(v) => (9, Some(sparkplug::payload::metric::Value::DoubleValue(v))),
            MetricValue::Int64(v) => (8, Some(sparkplug::payload::metric::Value::LongValue(v))),
            MetricValue::Boolean(v) => (11, Some(sparkplug::payload::metric::Value::BooleanValue(v))),
            MetricValue::String(v) => (12, Some(sparkplug::payload::metric::Value::StringValue(v))),
        };
        
        Metric {
            name: Some(name.to_string()),
            alias: None,
            timestamp: Some(timestamp),
            datatype: Some(datatype),
            is_historical: None,
            is_transient: None,
            is_null: None,
            metadata: None,
            properties: None,
            value: metric_value,
        }
    }
    
    pub fn publish_nbirth(&mut self) {
        let topic = format!("spBv1.0/{}/NBIRTH/{}", GROUP_ID, EDGE_NODE_ID);
        
        let metrics = vec![
            Self::create_metric("Temperature", MetricValue::Double(25.5)),
            Self::create_metric("Pressure", MetricValue::Double(101.3)),
            Self::create_metric("Node Control/Rebirth", MetricValue::Boolean(false)),
            Self::create_metric("bdSeq", MetricValue::Int64(0)),
        ];
        
        let payload = Payload {
            timestamp: Some(Self::get_timestamp()),
            metrics,
            seq: Some(self.sequence),
            uuid: None,
            body: None,
        };
        
        self.sequence += 1;
        
        let mut buf = Vec::new();
        payload.encode(&mut buf).unwrap();
        
        self.client.publish(&topic, QoS::AtLeastOnce, false, buf).unwrap();
        println!("Published NBIRTH to {}", topic);
    }
    
    pub fn publish_dbirth(&mut self, device_id: &str) {
        let topic = format!("spBv1.0/{}/DBIRTH/{}/{}", GROUP_ID, EDGE_NODE_ID, device_id);
        
        let metrics = vec![
            Self::create_metric("Value", MetricValue::Double(0.0)),
            Self::create_metric("Status", MetricValue::String("Online".to_string())),
        ];
        
        let payload = Payload {
            timestamp: Some(Self::get_timestamp()),
            metrics,
            seq: Some(self.sequence),
            uuid: None,
            body: None,
        };
        
        self.sequence += 1;
        
        let mut buf = Vec::new();
        payload.encode(&mut buf).unwrap();
        
        self.client.publish(&topic, QoS::AtLeastOnce, false, buf).unwrap();
        println!("Published DBIRTH for device {}", device_id);
    }
    
    pub fn publish_ndata(&mut self, metric_name: &str, value: MetricValue) {
        let topic = format!("spBv1.0/{}/NDATA/{}", GROUP_ID, EDGE_NODE_ID);
        
        let metrics = vec![Self::create_metric(metric_name, value)];
        
        let payload = Payload {
            timestamp: Some(Self::get_timestamp()),
            metrics,
            seq: Some(self.sequence),
            uuid: None,
            body: None,
        };
        
        self.sequence += 1;
        
        let mut buf = Vec::new();
        payload.encode(&mut buf).unwrap();
        
        self.client.publish(&topic, QoS::AtLeastOnce, false, buf).unwrap();
        println!("Published NDATA: {} = {:?}", metric_name, value);
    }
    
    pub fn publish_ddata(&mut self, device_id: &str, metric_name: &str, value: MetricValue) {
        let topic = format!("spBv1.0/{}/DDATA/{}/{}", GROUP_ID, EDGE_NODE_ID, device_id);
        
        let metrics = vec![Self::create_metric(metric_name, value)];
        
        let payload = Payload {
            timestamp: Some(Self::get_timestamp()),
            metrics,
            seq: Some(self.sequence),
            uuid: None,
            body: None,
        };
        
        self.sequence += 1;
        
        let mut buf = Vec::new();
        payload.encode(&mut buf).unwrap();
        
        self.client.publish(&topic, QoS::AtLeastOnce, false, buf).unwrap();
        println!("Published DDATA for {}: {} = {:?}", device_id, metric_name, value);
    }
}

#[derive(Debug)]
enum MetricValue {
    Double(f64),
    Int64(i64),
    Boolean(bool),
    String(String),
}

fn main() {
    let mut node = SparkplugNode::new();
    
    // Wait for connection
    thread::sleep(Duration::from_secs(1));
    
    // Publish NBIRTH
    node.publish_nbirth();
    
    // Publish DBIRTH for a device
    node.publish_dbirth(DEVICE_ID);
    
    // Simulate data collection
    for i in 0..5 {
        thread::sleep(Duration::from_secs(5));
        
        // Publish node data
        node.publish_ndata("Temperature", MetricValue::Double(20.0 + i as f64));
        
        // Publish device data
        node.publish_ddata(
            DEVICE_ID,
            "Value",
            MetricValue::Double(100.0 + (i as f64 * 10.0)),
        );
    }
    
    println!("Example complete");
    thread::sleep(Duration::from_secs(2));
}
```

## Summary

**Sparkplug B** is a comprehensive specification that standardizes MQTT communication in industrial environments. It provides:

1. **Standardized Topic Namespace**: Hierarchical structure for organizing industrial data
2. **Binary Payloads**: Efficient Protocol Buffers encoding for bandwidth-constrained networks
3. **State Management**: Birth/Death certificates ensure system integrity and auto-discovery
4. **Metric Organization**: Structured data model with timestamps, datatypes, and metadata
5. **Command/Control**: Bidirectional communication between SCADA systems and field devices

**Key Benefits:**
- Reduced development time through standardization
- Improved interoperability between vendors
- Efficient bandwidth usage in industrial networks
- Built-in state awareness and fault detection
- Scalable architecture from single devices to enterprise-wide deployments

The specification is particularly valuable in manufacturing, oil & gas, utilities, and other industrial sectors where reliable, standardized communication between disparate systems is critical. Both C/C++ (via Eclipse Tahu) and Rust implementations leverage Protocol Buffers for payload serialization, ensuring compatibility across implementations.