# Industrial Automation (IIoT) with MQTT

## Detailed Description

Industrial IoT (IIoT) represents the application of Internet of Things technologies in manufacturing, industrial processes, and supply chain management. MQTT has become a cornerstone protocol for IIoT implementations due to its lightweight nature, reliability through QoS levels, and ability to operate in challenging industrial network environments.

In Industry 4.0 contexts, MQTT enables real-time communication between programmable logic controllers (PLCs), sensors, actuators, SCADA systems, and cloud-based analytics platforms. The protocol's publish-subscribe model decouples data producers from consumers, allowing flexible system architectures that can scale from a single production line to entire manufacturing facilities.

Key industrial applications include:

**Process Monitoring and Control**: Real-time data collection from temperature sensors, pressure gauges, flow meters, and other instrumentation. MQTT facilitates bidirectional communication for both monitoring current states and sending control commands to adjust process parameters.

**Predictive Maintenance**: Continuous streaming of vibration data, temperature profiles, and operational metrics enables machine learning models to predict equipment failures before they occur, reducing downtime and maintenance costs.

**Production Line Coordination**: Synchronization of robotic systems, conveyor belts, and quality control stations through event-driven messaging ensures smooth production flow and rapid response to bottlenecks or quality issues.

**Energy Management**: Monitoring power consumption across facilities and dynamically adjusting operations to optimize energy usage while maintaining production targets.

Industrial MQTT deployments typically incorporate edge computing for local processing and decision-making, with selective data forwarding to cloud platforms for deeper analytics and long-term storage.

## C/C++ Implementation

Here's a comprehensive example using the Eclipse Paho MQTT C library for an industrial sensor system:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <MQTTClient.h>
#include <time.h>

#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "industrial_plc_001"
#define QOS 1
#define TIMEOUT 10000L

// Industrial sensor data structure
typedef struct {
    float temperature;
    float pressure;
    float vibration;
    int machine_status;
    char timestamp[64];
} SensorData;

// Callback for incoming control messages
int message_arrived(void *context, char *topicName, int topicLen, 
                    MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);
    
    // Parse control commands
    if (strcmp(topicName, "factory/line1/control/cmd") == 0) {
        char *payload = (char*)message->payload;
        
        if (strstr(payload, "STOP")) {
            printf("Emergency stop command received!\n");
            // Implement emergency stop logic
        } else if (strstr(payload, "START")) {
            printf("Start production command received\n");
            // Implement start logic
        } else if (strstr(payload, "SET_SPEED")) {
            int speed;
            if (sscanf(payload, "SET_SPEED:%d", &speed) == 1) {
                printf("Setting production speed to %d%%\n", speed);
                // Implement speed control
            }
        }
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connection_lost(void *context, char *cause) {
    printf("Connection lost: %s\n", cause);
    printf("Attempting reconnection...\n");
}

// Simulate sensor readings
SensorData read_sensors() {
    SensorData data;
    data.temperature = 45.0 + (rand() % 100) / 10.0;
    data.pressure = 100.0 + (rand() % 50) / 10.0;
    data.vibration = 0.5 + (rand() % 30) / 100.0;
    data.machine_status = (rand() % 100) > 5 ? 1 : 0; // 95% operational
    
    time_t now = time(NULL);
    strftime(data.timestamp, sizeof(data.timestamp), 
             "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    
    return data;
}

// Publish sensor data with error handling
int publish_sensor_data(MQTTClient client, const char *topic, SensorData data) {
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"timestamp\":\"%s\","
             "\"machine_id\":\"PLC_001\","
             "\"temperature\":%.2f,"
             "\"pressure\":%.2f,"
             "\"vibration\":%.3f,"
             "\"status\":%d}",
             data.timestamp, data.temperature, data.pressure, 
             data.vibration, data.machine_status);
    
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
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    return rc;
}

int main(int argc, char *argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    // Initialize MQTT client
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, 
                           message_arrived, NULL);
    
    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "industrial_user";
    conn_opts.password = "secure_password";
    
    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to MQTT broker\n");
    
    // Subscribe to control topics
    MQTTClient_subscribe(client, "factory/line1/control/cmd", QOS);
    MQTTClient_subscribe(client, "factory/line1/control/config", QOS);
    
    // Main industrial control loop
    printf("Starting sensor monitoring...\n");
    for (int i = 0; i < 100; i++) {
        SensorData data = read_sensors();
        
        // Publish sensor data
        publish_sensor_data(client, "factory/line1/sensors/machine001", data);
        
        // Check for alarm conditions
        if (data.temperature > 80.0) {
            char alarm[256];
            snprintf(alarm, sizeof(alarm),
                    "{\"type\":\"temperature_alarm\","
                    "\"machine\":\"PLC_001\","
                    "\"value\":%.2f,"
                    "\"threshold\":80.0}", data.temperature);
            
            MQTTClient_message alarm_msg = MQTTClient_message_initializer;
            alarm_msg.payload = alarm;
            alarm_msg.payloadlen = strlen(alarm);
            alarm_msg.qos = 2; // Highest QoS for alarms
            alarm_msg.retained = 0;
            
            MQTTClient_deliveryToken token;
            MQTTClient_publishMessage(client, "factory/line1/alarms", 
                                     &alarm_msg, &token);
            
            printf("ALARM: Temperature exceeded threshold!\n");
        }
        
        sleep(2); // 2 second sampling interval
    }
    
    // Cleanup
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
    
    return 0;
}
```

## Rust Implementation

Here's a robust Rust implementation using the `rumqttc` library with async/await patterns:

```rust
use rumqttc::{AsyncClient, Event, MqttOptions, Packet, QoS};
use serde::{Deserialize, Serialize};
use tokio::time::{sleep, Duration};
use chrono::Utc;
use rand::Rng;

#[derive(Debug, Serialize, Deserialize)]
struct SensorData {
    timestamp: String,
    machine_id: String,
    temperature: f32,
    pressure: f32,
    vibration: f32,
    status: u8,
}

#[derive(Debug, Deserialize)]
struct ControlCommand {
    command: String,
    #[serde(default)]
    value: Option<i32>,
}

#[derive(Debug, Serialize)]
struct AlarmMessage {
    alarm_type: String,
    machine_id: String,
    value: f32,
    threshold: f32,
    timestamp: String,
}

struct IndustrialController {
    client: AsyncClient,
    machine_id: String,
    running: bool,
    speed: i32,
}

impl IndustrialController {
    fn new(client: AsyncClient, machine_id: String) -> Self {
        Self {
            client,
            machine_id,
            running: false,
            speed: 100,
        }
    }

    async fn read_sensors(&self) -> SensorData {
        let mut rng = rand::thread_rng();
        
        SensorData {
            timestamp: Utc::now().to_rfc3339(),
            machine_id: self.machine_id.clone(),
            temperature: 45.0 + rng.gen_range(0.0..10.0),
            pressure: 100.0 + rng.gen_range(0.0..5.0),
            vibration: 0.5 + rng.gen_range(0.0..0.3),
            status: if rng.gen_range(0..100) > 5 { 1 } else { 0 },
        }
    }

    async fn publish_sensor_data(&self, data: &SensorData) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("factory/line1/sensors/{}", self.machine_id);
        let payload = serde_json::to_string(data)?;
        
        self.client
            .publish(&topic, QoS::AtLeastOnce, false, payload)
            .await?;
        
        Ok(())
    }

    async fn publish_alarm(&self, alarm: AlarmMessage) -> Result<(), Box<dyn std::error::Error>> {
        let payload = serde_json::to_string(&alarm)?;
        
        self.client
            .publish("factory/line1/alarms", QoS::ExactlyOnce, false, payload)
            .await?;
        
        println!("⚠️  ALARM: {} - Value: {:.2}", alarm.alarm_type, alarm.value);
        Ok(())
    }

    async fn handle_control_command(&mut self, command: ControlCommand) {
        match command.command.as_str() {
            "STOP" => {
                println!("🛑 Emergency stop command received!");
                self.running = false;
            }
            "START" => {
                println!("▶️  Start production command received");
                self.running = true;
            }
            "SET_SPEED" => {
                if let Some(speed) = command.value {
                    println!("⚙️  Setting production speed to {}%", speed);
                    self.speed = speed;
                }
            }
            _ => {
                println!("Unknown command: {}", command.command);
            }
        }
    }

    async fn check_alarm_conditions(&self, data: &SensorData) -> Option<AlarmMessage> {
        const TEMP_THRESHOLD: f32 = 80.0;
        const PRESSURE_THRESHOLD: f32 = 110.0;
        const VIBRATION_THRESHOLD: f32 = 1.0;

        if data.temperature > TEMP_THRESHOLD {
            return Some(AlarmMessage {
                alarm_type: "temperature_alarm".to_string(),
                machine_id: self.machine_id.clone(),
                value: data.temperature,
                threshold: TEMP_THRESHOLD,
                timestamp: Utc::now().to_rfc3339(),
            });
        }

        if data.pressure > PRESSURE_THRESHOLD {
            return Some(AlarmMessage {
                alarm_type: "pressure_alarm".to_string(),
                machine_id: self.machine_id.clone(),
                value: data.pressure,
                threshold: PRESSURE_THRESHOLD,
                timestamp: Utc::now().to_rfc3339(),
            });
        }

        if data.vibration > VIBRATION_THRESHOLD {
            return Some(AlarmMessage {
                alarm_type: "vibration_alarm".to_string(),
                machine_id: self.machine_id.clone(),
                value: data.vibration,
                threshold: VIBRATION_THRESHOLD,
                timestamp: Utc::now().to_rfc3339(),
            });
        }

        None
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new("industrial_plc_001", "localhost", 1883);
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_credentials("industrial_user", "secure_password");
    
    // Create async client
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    
    // Initialize controller
    let controller = IndustrialController::new(
        client.clone(),
        "machine_001".to_string()
    );
    let controller = std::sync::Arc::new(tokio::sync::Mutex::new(controller));

    // Subscribe to control topics
    client
        .subscribe("factory/line1/control/cmd", QoS::AtLeastOnce)
        .await?;
    client
        .subscribe("factory/line1/control/config", QoS::AtLeastOnce)
        .await?;

    println!("✅ Connected to MQTT broker");
    println!("📡 Subscribed to control topics");

    // Spawn task for handling incoming messages
    let controller_clone = controller.clone();
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    if let Ok(payload) = std::str::from_utf8(&p.payload) {
                        if p.topic.contains("control/cmd") {
                            if let Ok(cmd) = serde_json::from_str::<ControlCommand>(payload) {
                                let mut ctrl = controller_clone.lock().await;
                                ctrl.handle_control_command(cmd).await;
                            }
                        }
                    }
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("Error in event loop: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    // Main sensor monitoring loop
    println!("🔄 Starting sensor monitoring...");
    loop {
        let mut ctrl = controller.lock().await;
        
        // Read sensor data
        let sensor_data = ctrl.read_sensors().await;
        
        // Publish sensor data
        if let Err(e) = ctrl.publish_sensor_data(&sensor_data).await {
            eprintln!("Failed to publish sensor data: {}", e);
        } else {
            println!("📊 Published: Temp={:.1}°C, Pressure={:.1}bar, Vibration={:.2}mm/s",
                     sensor_data.temperature, sensor_data.pressure, sensor_data.vibration);
        }

        // Check and publish alarms
        if let Some(alarm) = ctrl.check_alarm_conditions(&sensor_data).await {
            if let Err(e) = ctrl.publish_alarm(alarm).await {
                eprintln!("Failed to publish alarm: {}", e);
            }
        }

        drop(ctrl); // Release lock
        sleep(Duration::from_secs(2)).await;
    }
}
```

## Summary

MQTT has become the de facto standard for Industrial IoT applications, providing a robust, scalable communication backbone for Industry 4.0 implementations. Its lightweight protocol overhead, QoS guarantees, and publish-subscribe architecture make it ideal for connecting diverse industrial equipment ranging from edge sensors to enterprise systems.

The C/C++ implementation demonstrates low-level control suitable for embedded systems and PLCs, with direct hardware integration and minimal resource overhead. This approach is common in real-time industrial controllers where deterministic behavior and tight timing constraints are critical.

The Rust implementation showcases modern async/await patterns with strong type safety and memory guarantees, making it excellent for industrial gateway applications and edge computing platforms where reliability and concurrent processing are essential. Rust's ownership model prevents common bugs that could lead to industrial system failures.

Both implementations illustrate key industrial patterns including alarm management with elevated QoS levels, bidirectional control messaging, structured data serialization with JSON, and error handling for network resilience. Industrial MQTT deployments typically incorporate additional features like TLS encryption, authentication, message persistence, and integration with time-series databases for analytics and compliance reporting.