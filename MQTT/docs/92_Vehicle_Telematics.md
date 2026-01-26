# Vehicle Telematics over MQTT

## Detailed Description

Vehicle telematics refers to the technology that combines telecommunications and informatics to monitor, track, and manage vehicles remotely. In connected car ecosystems and fleet management systems, MQTT (Message Queuing Telemetry Transport) serves as an ideal protocol due to its lightweight nature, efficiency over unreliable networks, and publish-subscribe architecture.

**Core Capabilities:**
- **Real-time Location Tracking**: GPS coordinates, speed, heading, and altitude
- **Vehicle Diagnostics**: Engine metrics (RPM, temperature, fuel level), battery status, tire pressure, error codes (DTCs)
- **Driver Behavior Monitoring**: Acceleration patterns, braking events, cornering force, idle time
- **Fleet Management**: Route optimization, maintenance scheduling, fuel consumption analysis
- **Safety & Security**: Collision detection, theft alerts, geofencing violations
- **Environmental Monitoring**: Emission levels, eco-driving metrics

**MQTT Topic Structure:**
```
fleet/{fleet_id}/vehicle/{vehicle_id}/{data_type}
fleet/acme-logistics/vehicle/truck-001/gps
fleet/acme-logistics/vehicle/truck-001/diagnostics/engine
fleet/acme-logistics/vehicle/truck-001/diagnostics/fuel
fleet/acme-logistics/vehicle/truck-001/events/maintenance
```

**Key Advantages:**
- Low bandwidth consumption (critical for cellular connections)
- QoS levels ensure critical data delivery
- Last Will and Testament (LWT) for connection monitoring
- Retained messages for latest vehicle state
- Scalable for managing thousands of vehicles

---

## C/C++ Implementation

Using the Eclipse Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <MQTTClient.h>
#include <time.h>

#define BROKER_ADDRESS "tcp://mqtt.fleet.example.com:1883"
#define CLIENT_ID "vehicle_truck_001"
#define QOS 1
#define TIMEOUT 10000L

typedef struct {
    double latitude;
    double longitude;
    double speed;
    double heading;
    time_t timestamp;
} GPSData;

typedef struct {
    int rpm;
    float engine_temp;
    float fuel_level;
    int odometer;
    time_t timestamp;
} DiagnosticsData;

// Format GPS data as JSON
void format_gps_json(GPSData* gps, char* buffer, size_t size) {
    snprintf(buffer, size,
        "{\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.2f,\"heading\":%.2f,\"timestamp\":%ld}",
        gps->latitude, gps->longitude, gps->speed, gps->heading, gps->timestamp);
}

// Format diagnostics data as JSON
void format_diagnostics_json(DiagnosticsData* diag, char* buffer, size_t size) {
    snprintf(buffer, size,
        "{\"rpm\":%d,\"engine_temp\":%.1f,\"fuel_level\":%.1f,\"odometer\":%d,\"timestamp\":%ld}",
        diag->rpm, diag->engine_temp, diag->fuel_level, diag->odometer, diag->timestamp);
}

// Publish vehicle data
int publish_vehicle_data(MQTTClient client, const char* topic, const char* payload) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 1; // Retain latest state
    
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        return rc;
    }
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message published to topic %s\n", topic);
    return rc;
}

// Simulate GPS data reading
GPSData read_gps_sensor() {
    GPSData gps;
    gps.latitude = 40.7128 + ((rand() % 1000) / 100000.0);
    gps.longitude = -74.0060 + ((rand() % 1000) / 100000.0);
    gps.speed = 50.0 + (rand() % 40);
    gps.heading = rand() % 360;
    gps.timestamp = time(NULL);
    return gps;
}

// Simulate vehicle diagnostics reading
DiagnosticsData read_diagnostics() {
    DiagnosticsData diag;
    diag.rpm = 2000 + (rand() % 2000);
    diag.engine_temp = 80.0 + (rand() % 30);
    diag.fuel_level = 20.0 + (rand() % 80);
    diag.odometer = 50000 + (rand() % 10000);
    diag.timestamp = time(NULL);
    return diag;
}

int main() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;
    int rc;
    
    // Set Last Will and Testament
    will_opts.topicName = "fleet/acme-logistics/vehicle/truck-001/status";
    will_opts.message = "{\"status\":\"offline\",\"reason\":\"connection_lost\"}";
    will_opts.retained = 1;
    will_opts.qos = 1;
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.will = &will_opts;
    
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return EXIT_FAILURE;
    }
    
    printf("Connected to MQTT broker\n");
    
    // Publish online status
    publish_vehicle_data(client, "fleet/acme-logistics/vehicle/truck-001/status",
                        "{\"status\":\"online\",\"vehicle_id\":\"truck-001\"}");
    
    char payload[512];
    
    // Main telemetry loop
    for (int i = 0; i < 10; i++) {
        // Publish GPS data
        GPSData gps = read_gps_sensor();
        format_gps_json(&gps, payload, sizeof(payload));
        publish_vehicle_data(client, "fleet/acme-logistics/vehicle/truck-001/gps", payload);
        
        // Publish diagnostics every 5 seconds
        if (i % 5 == 0) {
            DiagnosticsData diag = read_diagnostics();
            format_diagnostics_json(&diag, payload, sizeof(payload));
            publish_vehicle_data(client, 
                               "fleet/acme-logistics/vehicle/truck-001/diagnostics", 
                               payload);
        }
        
        sleep(1);
    }
    
    // Clean disconnect
    publish_vehicle_data(client, "fleet/acme-logistics/vehicle/truck-001/status",
                        "{\"status\":\"offline\",\"reason\":\"shutdown\"}");
    
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return EXIT_SUCCESS;
}
```

---

## Rust Implementation

Using the `rumqttc` library:

```rust
use rumqttc::{Client, MqttOptions, QoS, LastWill};
use serde::{Serialize, Deserialize};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use std::thread;
use rand::Rng;

#[derive(Serialize, Deserialize, Debug)]
struct GpsData {
    lat: f64,
    lon: f64,
    speed: f64,
    heading: f64,
    timestamp: u64,
}

#[derive(Serialize, Deserialize, Debug)]
struct DiagnosticsData {
    rpm: u32,
    engine_temp: f32,
    fuel_level: f32,
    odometer: u32,
    timestamp: u64,
}

#[derive(Serialize, Deserialize, Debug)]
struct VehicleStatus {
    status: String,
    vehicle_id: String,
    reason: Option<String>,
}

// Simulate GPS sensor reading
fn read_gps_sensor() -> GpsData {
    let mut rng = rand::thread_rng();
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    
    GpsData {
        lat: 40.7128 + (rng.gen::<f64>() * 0.01),
        lon: -74.0060 + (rng.gen::<f64>() * 0.01),
        speed: 50.0 + (rng.gen::<f64>() * 40.0),
        heading: rng.gen::<f64>() * 360.0,
        timestamp,
    }
}

// Simulate vehicle diagnostics reading
fn read_diagnostics() -> DiagnosticsData {
    let mut rng = rand::thread_rng();
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    
    DiagnosticsData {
        rpm: 2000 + rng.gen_range(0..2000),
        engine_temp: 80.0 + (rng.gen::<f32>() * 30.0),
        fuel_level: 20.0 + (rng.gen::<f32>() * 80.0),
        odometer: 50000 + rng.gen_range(0..10000),
        timestamp,
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new(
        "vehicle_truck_001",
        "mqtt.fleet.example.com",
        1883
    );
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    // Set Last Will and Testament
    let lwt_payload = serde_json::to_string(&VehicleStatus {
        status: "offline".to_string(),
        vehicle_id: "truck-001".to_string(),
        reason: Some("connection_lost".to_string()),
    })?;
    
    let last_will = LastWill::new(
        "fleet/acme-logistics/vehicle/truck-001/status",
        lwt_payload,
        QoS::AtLeastOnce,
        true, // retained
    );
    mqttoptions.set_last_will(last_will);
    
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn connection handler
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(event) => println!("Event: {:?}", event),
                Err(e) => eprintln!("Connection error: {:?}", e),
            }
        }
    });
    
    // Wait for connection
    thread::sleep(Duration::from_secs(1));
    
    // Publish online status
    let online_status = serde_json::to_string(&VehicleStatus {
        status: "online".to_string(),
        vehicle_id: "truck-001".to_string(),
        reason: None,
    })?;
    
    client.publish(
        "fleet/acme-logistics/vehicle/truck-001/status",
        QoS::AtLeastOnce,
        true, // retained
        online_status,
    )?;
    
    println!("Vehicle telematics system started");
    
    // Main telemetry loop
    for i in 0..10 {
        // Publish GPS data
        let gps_data = read_gps_sensor();
        let gps_json = serde_json::to_string(&gps_data)?;
        
        client.publish(
            "fleet/acme-logistics/vehicle/truck-001/gps",
            QoS::AtLeastOnce,
            true, // retained
            gps_json,
        )?;
        
        println!("Published GPS: lat={:.6}, lon={:.6}, speed={:.2} km/h",
                 gps_data.lat, gps_data.lon, gps_data.speed);
        
        // Publish diagnostics every 5 seconds
        if i % 5 == 0 {
            let diag_data = read_diagnostics();
            let diag_json = serde_json::to_string(&diag_data)?;
            
            client.publish(
                "fleet/acme-logistics/vehicle/truck-001/diagnostics",
                QoS::AtLeastOnce,
                true, // retained
                diag_json,
            )?;
            
            println!("Published Diagnostics: RPM={}, Temp={:.1}°C, Fuel={:.1}%",
                     diag_data.rpm, diag_data.engine_temp, diag_data.fuel_level);
        }
        
        thread::sleep(Duration::from_secs(1));
    }
    
    // Clean shutdown
    let offline_status = serde_json::to_string(&VehicleStatus {
        status: "offline".to_string(),
        vehicle_id: "truck-001".to_string(),
        reason: Some("shutdown".to_string()),
    })?;
    
    client.publish(
        "fleet/acme-logistics/vehicle/truck-001/status",
        QoS::AtLeastOnce,
        true,
        offline_status,
    )?;
    
    thread::sleep(Duration::from_millis(500)); // Allow final publish
    
    println!("Vehicle telematics system stopped");
    
    Ok(())
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
rumqttc = "0.24"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
rand = "0.8"
```

---

## Summary

Vehicle telematics over MQTT enables efficient, real-time monitoring of connected vehicles and fleet management. The protocol's lightweight nature makes it ideal for cellular and satellite connections common in automotive applications. Key features include:

- **Efficient Data Transmission**: Minimal bandwidth usage for GPS, diagnostics, and event data
- **Reliable Delivery**: QoS levels ensure critical alerts (accidents, theft) reach fleet managers
- **Connection Monitoring**: Last Will and Testament automatically reports vehicle disconnections
- **State Retention**: Retained messages provide instant access to last known vehicle position and status
- **Scalability**: Supports thousands of vehicles with hierarchical topic structures
- **Security**: TLS/SSL encryption and authentication protect sensitive vehicle and location data

Both C/C++ and Rust implementations demonstrate real-world patterns: structured topics for data organization, JSON payloads for interoperability, retained messages for state tracking, and proper connection lifecycle management. These patterns form the foundation for production telematics systems managing logistics fleets, ride-sharing vehicles, and connected car ecosystems.