# Smart City Infrastructure with MQTT

## Detailed Description

Smart City Infrastructure represents a comprehensive ecosystem of interconnected IoT devices and systems that manage urban environments through real-time data collection, analysis, and automated control. MQTT serves as the backbone communication protocol for these deployments due to its lightweight nature, reliability, and ability to handle thousands of concurrent connections efficiently.

### Key Components

**Traffic Management Systems** monitor vehicle flow, control adaptive traffic signals, manage parking availability, and optimize public transportation routes. Sensors deployed at intersections, along roadways, and in parking facilities publish real-time data that enables dynamic responses to changing traffic conditions.

**Utilities Management** encompasses smart grid electricity distribution, water quality and consumption monitoring, waste management optimization, and street lighting control. These systems reduce energy consumption, detect infrastructure issues early, and improve service delivery to citizens.

**Environmental Monitoring** includes air quality sensors, noise level detectors, weather stations, and flood monitoring systems that provide continuous environmental data to city operators and citizens.

**Public Safety Systems** integrate emergency response coordination, public safety alerts, surveillance systems, and incident reporting through unified MQTT communication channels.

### MQTT Topic Architecture

A well-designed topic hierarchy is crucial for scalability and maintainability:

```
city/{city_id}/{domain}/{zone}/{device_type}/{device_id}/{metric}

Examples:
city/nyc/traffic/manhattan/signal/5th_42nd/status
city/nyc/traffic/manhattan/signal/5th_42nd/timing
city/nyc/parking/downtown/sensor/lot_a_spot_12/occupied
city/nyc/utilities/grid/transformer/sub_station_7/load
city/nyc/environment/central_park/air_quality/pm25
city/nyc/lighting/broadway/streetlight/pole_453/brightness
```

---

## C/C++ Implementation

Using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://mqtt.smartcity.local:1883"
#define CLIENTID    "TrafficController_001"
#define QOS         1
#define TIMEOUT     10000L

// Traffic signal controller structure
typedef struct {
    char intersection_id[64];
    int red_duration;
    int yellow_duration;
    int green_duration;
    char current_state[16];
    int vehicle_count;
} TrafficSignal;

// Callback for incoming messages
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Parse commands for traffic signal control
    if (strstr(topicName, "/command")) {
        // Handle traffic signal timing adjustments
        printf("Processing command...\n");
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
    printf("Attempting to reconnect...\n");
}

// Publish traffic signal status
int publish_signal_status(MQTTClient client, TrafficSignal *signal) {
    char topic[256];
    char payload[512];
    
    snprintf(topic, sizeof(topic), 
             "city/nyc/traffic/manhattan/signal/%s/status", 
             signal->intersection_id);
    
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\",\"red\":%d,\"yellow\":%d,\"green\":%d,\"vehicles\":%d,\"timestamp\":%ld}",
             signal->current_state,
             signal->red_duration,
             signal->yellow_duration,
             signal->green_duration,
             signal->vehicle_count,
             time(NULL));
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        return rc;
    }
    
    MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Published to %s\n", topic);
    return rc;
}

// Smart parking sensor implementation
int publish_parking_status(MQTTClient client, const char *lot_id, 
                           const char *spot_id, int occupied) {
    char topic[256];
    char payload[256];
    
    snprintf(topic, sizeof(topic), 
             "city/nyc/parking/downtown/sensor/%s_%s/occupied", 
             lot_id, spot_id);
    
    snprintf(payload, sizeof(payload),
             "{\"occupied\":%s,\"lot\":\"%s\",\"spot\":\"%s\",\"timestamp\":%ld}",
             occupied ? "true" : "false",
             lot_id, spot_id, time(NULL));
    
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 1;  // Retain parking status
    
    MQTTClient_deliveryToken token;
    return MQTTClient_publishMessage(client, topic, &pubmsg, &token);
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                            message_arrived, NULL);
    
    // Configure connection
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "smart_city_controller";
    conn_opts.password = "secure_password";
    
    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to Smart City MQTT Broker\n");
    
    // Subscribe to command topics
    MQTTClient_subscribe(client, "city/nyc/traffic/+/signal/+/command", QOS);
    MQTTClient_subscribe(client, "city/nyc/utilities/+/control/#", QOS);
    
    // Initialize traffic signal
    TrafficSignal signal;
    strcpy(signal.intersection_id, "5th_42nd");
    signal.red_duration = 45;
    signal.yellow_duration = 3;
    signal.green_duration = 40;
    strcpy(signal.current_state, "green");
    signal.vehicle_count = 0;
    
    // Main control loop
    for (int i = 0; i < 100; i++) {
        // Simulate vehicle detection
        signal.vehicle_count = rand() % 50;
        
        // Publish status
        publish_signal_status(client, &signal);
        
        // Simulate parking updates
        publish_parking_status(client, "lot_a", "spot_12", rand() % 2);
        
        sleep(5);
    }
    
    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}
```

### C++ Smart Streetlight Controller

```cpp
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class SmartStreetlight {
private:
    mqtt::async_client client;
    std::string light_id;
    int brightness;
    bool motion_detected;
    
    const std::string BASE_TOPIC = "city/nyc/lighting/broadway/streetlight/";
    
public:
    SmartStreetlight(const std::string& broker_address, 
                     const std::string& id)
        : client(broker_address, "streetlight_" + id),
          light_id(id),
          brightness(0),
          motion_detected(false) {
        
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        conn_opts.set_automatic_reconnect(true);
        
        try {
            client.connect(conn_opts)->wait();
            std::cout << "Streetlight " << light_id << " connected\n";
            
            // Subscribe to control commands
            client.subscribe(BASE_TOPIC + light_id + "/control", 1)->wait();
            
            // Set message callback
            client.set_message_callback([this](mqtt::const_message_ptr msg) {
                handle_message(msg);
            });
            
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
        }
    }
    
    void handle_message(mqtt::const_message_ptr msg) {
        std::string topic = msg->get_topic();
        std::string payload = msg->to_string();
        
        try {
            auto j = json::parse(payload);
            
            if (j.contains("brightness")) {
                set_brightness(j["brightness"].get<int>());
            }
            
            if (j.contains("mode")) {
                std::string mode = j["mode"];
                std::cout << "Switching to mode: " << mode << std::endl;
            }
            
        } catch (json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        }
    }
    
    void set_brightness(int level) {
        brightness = std::max(0, std::min(100, level));
        std::cout << "Brightness set to " << brightness << "%\n";
        publish_status();
    }
    
    void detect_motion(bool detected) {
        motion_detected = detected;
        
        // Auto-adjust brightness based on motion
        if (detected) {
            set_brightness(100);
        } else {
            set_brightness(30);  // Dim when no motion
        }
    }
    
    void publish_status() {
        json status;
        status["light_id"] = light_id;
        status["brightness"] = brightness;
        status["motion_detected"] = motion_detected;
        status["power_consumption"] = brightness * 0.5;  // Simulated watts
        status["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        
        std::string topic = BASE_TOPIC + light_id + "/status";
        std::string payload = status.dump();
        
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(1);
        
        client.publish(msg)->wait();
    }
    
    void publish_energy_usage() {
        json energy;
        energy["light_id"] = light_id;
        energy["daily_kwh"] = (brightness * 0.5 * 12) / 1000.0;  // 12 hour estimate
        energy["cost_usd"] = energy["daily_kwh"].get<double>() * 0.12;
        
        std::string topic = "city/nyc/utilities/energy/streetlight/" + light_id;
        
        auto msg = mqtt::make_message(topic, energy.dump());
        msg->set_qos(1);
        
        client.publish(msg);
    }
    
    ~SmartStreetlight() {
        try {
            client.disconnect()->wait();
        } catch (...) {}
    }
};

int main() {
    SmartStreetlight light("tcp://mqtt.smartcity.local:1883", "pole_453");
    
    // Simulate operation
    for (int i = 0; i < 50; i++) {
        // Simulate motion detection
        bool motion = (rand() % 10) < 3;  // 30% chance
        light.detect_motion(motion);
        
        light.publish_status();
        
        if (i % 10 == 0) {
            light.publish_energy_usage();
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    return 0;
}
```

---

## Rust Implementation

Using the `paho-mqtt` and `serde_json` crates:

```rust
use paho_mqtt as mqtt;
use serde::{Deserialize, Serialize};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use std::thread;

// Air quality monitoring structure
#[derive(Serialize, Deserialize, Debug)]
struct AirQualityData {
    sensor_id: String,
    location: String,
    pm25: f32,
    pm10: f32,
    co2: f32,
    temperature: f32,
    humidity: f32,
    timestamp: u64,
}

impl AirQualityData {
    fn new(sensor_id: &str, location: &str) -> Self {
        AirQualityData {
            sensor_id: sensor_id.to_string(),
            location: location.to_string(),
            pm25: 0.0,
            pm10: 0.0,
            co2: 0.0,
            temperature: 0.0,
            humidity: 0.0,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
        }
    }
    
    fn read_sensors(&mut self) {
        // Simulate sensor readings
        self.pm25 = 15.0 + (rand::random::<f32>() * 20.0);
        self.pm10 = 25.0 + (rand::random::<f32>() * 30.0);
        self.co2 = 400.0 + (rand::random::<f32>() * 100.0);
        self.temperature = 20.0 + (rand::random::<f32>() * 15.0);
        self.humidity = 40.0 + (rand::random::<f32>() * 30.0);
        self.timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
    }
    
    fn check_air_quality_alert(&self) -> Option<String> {
        if self.pm25 > 35.0 {
            Some(format!("High PM2.5 levels detected: {:.2} µg/m³", self.pm25))
        } else if self.pm10 > 50.0 {
            Some(format!("High PM10 levels detected: {:.2} µg/m³", self.pm10))
        } else {
            None
        }
    }
}

// Waste management bin sensor
#[derive(Serialize, Deserialize, Debug)]
struct WasteBinSensor {
    bin_id: String,
    location: String,
    fill_level: u8,  // 0-100%
    last_collection: u64,
    requires_collection: bool,
    timestamp: u64,
}

impl WasteBinSensor {
    fn new(bin_id: &str, location: &str) -> Self {
        WasteBinSensor {
            bin_id: bin_id.to_string(),
            location: location.to_string(),
            fill_level: 0,
            last_collection: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
            requires_collection: false,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
        }
    }
    
    fn update_fill_level(&mut self, level: u8) {
        self.fill_level = level.min(100);
        self.requires_collection = self.fill_level >= 80;
        self.timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
    }
}

// Smart City MQTT Client
struct SmartCityClient {
    client: mqtt::Client,
    city_id: String,
}

impl SmartCityClient {
    fn new(broker_url: &str, client_id: &str, city_id: &str) -> mqtt::Result<Self> {
        let create_opts = mqtt::CreateOptionsBuilder::new()
            .server_uri(broker_url)
            .client_id(client_id)
            .finalize();
        
        let client = mqtt::Client::new(create_opts)?;
        
        Ok(SmartCityClient {
            client,
            city_id: city_id.to_string(),
        })
    }
    
    fn connect(&self) -> mqtt::Result<()> {
        let conn_opts = mqtt::ConnectOptionsBuilder::new()
            .keep_alive_interval(Duration::from_secs(20))
            .clean_session(true)
            .user_name("smart_city_monitor")
            .password("secure_pass")
            .automatic_reconnect(Duration::from_secs(1), Duration::from_secs(30))
            .finalize();
        
        self.client.connect(conn_opts)?;
        println!("Connected to Smart City MQTT Broker");
        Ok(())
    }
    
    fn publish_air_quality(&self, data: &AirQualityData) -> mqtt::Result<()> {
        let topic = format!(
            "city/{}/environment/{}/air_quality/data",
            self.city_id, data.location
        );
        
        let payload = serde_json::to_string(data)
            .expect("Failed to serialize air quality data");
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        
        // Publish alert if necessary
        if let Some(alert_msg) = data.check_air_quality_alert() {
            self.publish_alert("air_quality", &alert_msg)?;
        }
        
        Ok(())
    }
    
    fn publish_waste_bin_status(&self, bin: &WasteBinSensor) -> mqtt::Result<()> {
        let topic = format!(
            "city/{}/waste/bin/{}/status",
            self.city_id, bin.bin_id
        );
        
        let payload = serde_json::to_string(bin)
            .expect("Failed to serialize waste bin data");
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(payload)
            .qos(1)
            .retained(true)  // Retain bin status
            .finalize();
        
        self.client.publish(msg)?;
        
        // Publish collection request if needed
        if bin.requires_collection {
            self.publish_collection_request(bin)?;
        }
        
        Ok(())
    }
    
    fn publish_collection_request(&self, bin: &WasteBinSensor) -> mqtt::Result<()> {
        let topic = format!(
            "city/{}/waste/collection/requests",
            self.city_id
        );
        
        let request = serde_json::json!({
            "bin_id": bin.bin_id,
            "location": bin.location,
            "fill_level": bin.fill_level,
            "priority": if bin.fill_level >= 95 { "high" } else { "medium" },
            "timestamp": bin.timestamp
        });
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(request.to_string())
            .qos(2)  // Exactly once delivery for collection requests
            .finalize();
        
        self.client.publish(msg)?;
        println!("Collection request sent for bin {}", bin.bin_id);
        Ok(())
    }
    
    fn publish_alert(&self, alert_type: &str, message: &str) -> mqtt::Result<()> {
        let topic = format!(
            "city/{}/alerts/{}",
            self.city_id, alert_type
        );
        
        let alert = serde_json::json!({
            "type": alert_type,
            "message": message,
            "severity": "warning",
            "timestamp": SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs()
        });
        
        let msg = mqtt::MessageBuilder::new()
            .topic(topic)
            .payload(alert.to_string())
            .qos(1)
            .finalize();
        
        self.client.publish(msg)?;
        println!("Alert published: {}", message);
        Ok(())
    }
    
    fn subscribe_to_commands(&self) -> mqtt::Result<mqtt::Receiver<Option<mqtt::Message>>> {
        let topics = vec![
            format!("city/{}/environment/+/control", self.city_id),
            format!("city/{}/waste/+/command", self.city_id),
            format!("city/{}/system/broadcast", self.city_id),
        ];
        
        let qos = vec![1, 1, 1];
        
        self.client.subscribe_many(&topics, &qos)?;
        println!("Subscribed to command topics");
        
        Ok(self.client.start_consuming())
    }
}

fn main() -> mqtt::Result<()> {
    let client = SmartCityClient::new(
        "tcp://mqtt.smartcity.local:1883",
        "env_monitor_001",
        "nyc"
    )?;
    
    client.connect()?;
    
    // Subscribe to commands
    let rx = client.subscribe_to_commands()?;
    
    // Spawn thread to handle incoming messages
    thread::spawn(move || {
        for msg_opt in rx.iter() {
            if let Some(msg) = msg_opt {
                println!("Received command on {}: {}", 
                         msg.topic(), msg.payload_str());
                
                // Handle commands here
            }
        }
    });
    
    // Initialize sensors
    let mut air_sensor = AirQualityData::new("aq_central_001", "central_park");
    let mut waste_bin = WasteBinSensor::new("bin_downtown_042", "5th_ave_42nd");
    
    // Main monitoring loop
    for cycle in 0..100 {
        // Read and publish air quality data
        air_sensor.read_sensors();
        client.publish_air_quality(&air_sensor)?;
        
        // Update and publish waste bin status
        let fill_increment = (cycle % 10) * 8;  // Simulate gradual filling
        waste_bin.update_fill_level(fill_increment as u8);
        client.publish_waste_bin_status(&waste_bin)?;
        
        println!("Monitoring cycle {} completed", cycle + 1);
        thread::sleep(Duration::from_secs(3));
    }
    
    Ok(())
}
```

---

## Summary

**Smart City Infrastructure** leverages MQTT to create interconnected urban systems that improve efficiency, sustainability, and quality of life. The protocol's publish-subscribe model enables scalable communication between thousands of sensors, controllers, and management systems across traffic, utilities, environment, and public services.

**Key advantages** include real-time responsiveness to changing conditions, reduced operational costs through automation, improved resource allocation, enhanced public safety, and data-driven urban planning. The hierarchical topic structure provides logical organization while supporting flexible subscription patterns for different city departments and applications.

**Implementation considerations** involve ensuring network reliability with automatic reconnection, using appropriate QoS levels (QoS 0 for frequent sensor readings, QoS 1 for status updates, QoS 2 for critical commands), implementing retained messages for device states, securing communications with TLS and authentication, and designing for horizontal scalability as cities expand their IoT deployments.

The code examples demonstrate practical implementations from traffic signal controllers and parking sensors in C/C++ to environmental monitoring and waste management systems in Rust, showcasing how MQTT enables cohesive smart city ecosystems.