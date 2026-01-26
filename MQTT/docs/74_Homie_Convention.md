# Homie Convention: Detailed Description

The **Homie Convention** is a lightweight MQTT convention for IoT and home automation devices that defines a standardized topic structure and communication protocol. It promotes interoperability, auto-discovery, and self-description of devices, making it easier to integrate different IoT devices into home automation systems.

## Core Concepts

### Topic Structure
Homie uses a hierarchical topic structure:
```
homie/<device-id>/<node-id>/<property-id>
```

- **Device**: Represents a physical or logical device (e.g., a sensor unit)
- **Node**: A logical component of a device (e.g., temperature sensor, humidity sensor)
- **Property**: An attribute of a node (e.g., temperature value, unit)

### Device Lifecycle States
- `init`: Device is initializing
- `ready`: Device is ready to operate
- `disconnected`: Device is offline (set via MQTT Last Will)
- `sleeping`: Device is in sleep mode
- `lost`: Connection lost unexpectedly
- `alert`: Device encountered an error

### Attributes
Homie devices publish metadata describing their capabilities:
- **$homie**: Protocol version
- **$name**: Human-readable name
- **$state**: Current device state
- **$nodes**: List of nodes
- **$properties**: List of properties (per node)
- **$datatype**: Property data type (integer, float, boolean, string, enum, color)
- **$settable**: Whether a property can be set

---

## C/C++ Implementation

```cpp
#include <PubSubClient.h>
#include <WiFi.h>

class HomieDevice {
private:
    PubSubClient& mqtt;
    String deviceId;
    String baseTopic;
    
public:
    HomieDevice(PubSubClient& client, const char* devId) 
        : mqtt(client), deviceId(devId) {
        baseTopic = "homie/" + deviceId;
    }
    
    // Initialize device and publish metadata
    void begin() {
        // Homie version
        mqtt.publish((baseTopic + "/$homie").c_str(), "4.0.0", true);
        
        // Device name
        mqtt.publish((baseTopic + "/$name").c_str(), "Temperature Sensor", true);
        
        // Device state
        setState("init");
        
        // Define nodes
        mqtt.publish((baseTopic + "/$nodes").c_str(), "sensor", true);
        
        // Node name
        mqtt.publish((baseTopic + "/sensor/$name").c_str(), "DHT22 Sensor", true);
        
        // Node properties
        mqtt.publish((baseTopic + "/sensor/$properties").c_str(), 
                    "temperature,humidity", true);
        
        // Temperature property metadata
        mqtt.publish((baseTopic + "/sensor/temperature/$name").c_str(), 
                    "Temperature", true);
        mqtt.publish((baseTopic + "/sensor/temperature/$datatype").c_str(), 
                    "float", true);
        mqtt.publish((baseTopic + "/sensor/temperature/$unit").c_str(), 
                    "°C", true);
        mqtt.publish((baseTopic + "/sensor/temperature/$settable").c_str(), 
                    "false", true);
        
        // Humidity property metadata
        mqtt.publish((baseTopic + "/sensor/humidity/$name").c_str(), 
                    "Humidity", true);
        mqtt.publish((baseTopic + "/sensor/humidity/$datatype").c_str(), 
                    "float", true);
        mqtt.publish((baseTopic + "/sensor/humidity/$unit").c_str(), 
                    "%", true);
        mqtt.publish((baseTopic + "/sensor/humidity/$settable").c_str(), 
                    "false", true);
        
        // Device is ready
        setState("ready");
    }
    
    // Set device state
    void setState(const char* state) {
        mqtt.publish((baseTopic + "/$state").c_str(), state, true);
    }
    
    // Publish property value
    void publishProperty(const char* node, const char* property, float value) {
        String topic = baseTopic + "/" + node + "/" + property;
        char valueStr[16];
        dtostrf(value, 1, 2, valueStr);
        mqtt.publish(topic.c_str(), valueStr, true);
    }
    
    // Set Last Will Testament
    void setLastWill() {
        String lwt = baseTopic + "/$state";
        mqtt.setWill(lwt.c_str(), "lost", true, 1);
    }
};

// Usage example
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
HomieDevice homieDevice(mqttClient, "device-001");

void setup() {
    // Connect to WiFi and MQTT
    // ...
    
    homieDevice.setLastWill();
    homieDevice.begin();
    
    // Publish sensor readings
    homieDevice.publishProperty("sensor", "temperature", 23.5);
    homieDevice.publishProperty("sensor", "humidity", 65.2);
}
```

---

## Rust Implementation

```rust
use rumqttc::{Client, MqttOptions, QoS, LastWill};
use std::time::Duration;

pub struct HomieDevice {
    client: Client,
    device_id: String,
    base_topic: String,
}

impl HomieDevice {
    pub fn new(client: Client, device_id: &str) -> Self {
        let base_topic = format!("homie/{}", device_id);
        HomieDevice {
            client,
            device_id: device_id.to_string(),
            base_topic,
        }
    }
    
    // Initialize device with metadata
    pub fn begin(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        // Homie version
        self.publish("$homie", "4.0.0", true)?;
        
        // Device name
        self.publish("$name", "Temperature Sensor", true)?;
        
        // Initial state
        self.set_state("init")?;
        
        // Define nodes
        self.publish("$nodes", "sensor", true)?;
        
        // Node metadata
        self.publish("sensor/$name", "DHT22 Sensor", true)?;
        self.publish("sensor/$properties", "temperature,humidity", true)?;
        
        // Temperature property
        self.publish("sensor/temperature/$name", "Temperature", true)?;
        self.publish("sensor/temperature/$datatype", "float", true)?;
        self.publish("sensor/temperature/$unit", "°C", true)?;
        self.publish("sensor/temperature/$settable", "false", true)?;
        
        // Humidity property
        self.publish("sensor/humidity/$name", "Humidity", true)?;
        self.publish("sensor/humidity/$datatype", "float", true)?;
        self.publish("sensor/humidity/$unit", "%", true)?;
        self.publish("sensor/humidity/$settable", "false", true)?;
        
        // Device ready
        self.set_state("ready")?;
        
        Ok(())
    }
    
    // Publish helper
    fn publish(&mut self, suffix: &str, payload: &str, retain: bool) 
        -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("{}/{}", self.base_topic, suffix);
        let qos = if retain { QoS::AtLeastOnce } else { QoS::AtMostOnce };
        self.client.publish(topic, qos, retain, payload)?;
        Ok(())
    }
    
    // Set device state
    pub fn set_state(&mut self, state: &str) -> Result<(), Box<dyn std::error::Error>> {
        self.publish("$state", state, true)
    }
    
    // Publish property value
    pub fn publish_property(&mut self, node: &str, property: &str, value: f32) 
        -> Result<(), Box<dyn std::error::Error>> {
        let suffix = format!("{}/{}", node, property);
        let payload = format!("{:.2}", value);
        self.publish(&suffix, &payload, true)
    }
}

// Usage example
fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Setup MQTT with Last Will
    let mut mqttoptions = MqttOptions::new("homie-device", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    let lwt = LastWill::new(
        "homie/device-001/$state",
        "lost",
        QoS::AtLeastOnce,
        true
    );
    mqttoptions.set_last_will(lwt);
    
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Create Homie device
    let mut device = HomieDevice::new(client, "device-001");
    device.begin()?;
    
    // Publish sensor data
    device.publish_property("sensor", "temperature", 23.5)?;
    device.publish_property("sensor", "humidity", 65.2)?;
    
    // Event loop would go here
    // for notification in connection.iter() { ... }
    
    Ok(())
}
```

---

## Key Features

### Auto-Discovery
Controllers can automatically discover devices by subscribing to `homie/+/$homie` and parsing the device tree.

### Settable Properties
Properties marked as `$settable=true` can be controlled:
```
homie/device-001/light/power/set → "true"
homie/device-001/light/power → "true" (device confirms)
```

### Data Types
Supported datatypes: `integer`, `float`, `boolean`, `string`, `enum`, `color`

### Retained Messages
All metadata and state messages use MQTT's retained flag for persistence.

---

## Summary

The **Homie Convention** provides a standardized, self-documenting MQTT structure for IoT devices that enables:

- **Interoperability**: Devices from different manufacturers work together seamlessly
- **Auto-discovery**: Controllers automatically detect and understand device capabilities
- **Clear semantics**: Hierarchical structure (device → node → property) organizes functionality logically
- **Rich metadata**: Properties include datatype, units, settability, and human-readable names
- **State management**: Devices communicate their lifecycle state (init, ready, sleeping, lost)
- **Retained messages**: Metadata persists across connections for reliable discovery

Both C/C++ (ideal for ESP32/Arduino) and Rust (for performance-critical or systems-level applications) implementations demonstrate publishing device metadata, property values, and managing device states. The convention reduces development complexity by providing clear guidelines for topic structure, making IoT ecosystems more maintainable and extensible.