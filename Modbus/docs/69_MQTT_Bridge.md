# MQTT Bridge for Modbus: Detailed Description

## Overview

An MQTT Bridge for Modbus is a software component that acts as a protocol translator between Modbus devices (sensors, PLCs, controllers) and MQTT brokers. This bridge enables Industrial IoT (IIoT) applications by converting Modbus data into MQTT messages, allowing legacy industrial equipment to communicate with modern cloud platforms, dashboards, and analytics systems.

## Architecture and Concepts

### Key Components

1. **Modbus Client/Master**: Polls Modbus devices (RTU/TCP) to read register values
2. **Data Mapper**: Transforms Modbus register data into meaningful MQTT payloads (JSON, binary, etc.)
3. **MQTT Publisher**: Publishes formatted data to MQTT broker topics
4. **Configuration Manager**: Defines polling intervals, register mappings, and topic structures
5. **Event Handler**: Manages bidirectional communication (MQTT subscriptions to write Modbus registers)

### Data Flow

```
Modbus Device → Modbus Poll → Data Transformation → MQTT Publish → MQTT Broker → Cloud/Apps
              ← Modbus Write ← Command Parser ← MQTT Subscribe ← MQTT Broker ← Cloud/Apps
```

### Common Use Cases

- Remote monitoring of industrial equipment
- Predictive maintenance analytics
- Energy management systems
- Building automation integration
- SCADA cloud connectivity

## C/C++ Implementation

### Example using libmodbus and Paho MQTT C

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modbus.h>
#include <MQTTClient.h>
#include <unistd.h>
#include <time.h>

#define MQTT_ADDRESS    "tcp://localhost:1883"
#define MQTT_CLIENTID   "ModbusBridge"
#define MQTT_TOPIC      "factory/plc/data"
#define MODBUS_ADDRESS  "192.168.1.100"
#define MODBUS_PORT     502
#define QOS             1
#define TIMEOUT         10000L

typedef struct {
    modbus_t *mb_ctx;
    MQTTClient mqtt_client;
    int running;
} bridge_context_t;

// Initialize Modbus connection
modbus_t* init_modbus(const char *ip, int port) {
    modbus_t *ctx = modbus_new_tcp(ip, port);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create Modbus context\n");
        return NULL;
    }
    
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Modbus connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return NULL;
    }
    
    printf("Modbus connected to %s:%d\n", ip, port);
    return ctx;
}

// Initialize MQTT connection
int init_mqtt(MQTTClient *client) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    MQTTClient_create(client, MQTT_ADDRESS, MQTT_CLIENTID,
                      MQTT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    if ((rc = MQTTClient_connect(*client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "MQTT connection failed, return code %d\n", rc);
        return -1;
    }
    
    printf("MQTT connected to %s\n", MQTT_ADDRESS);
    return 0;
}

// Read Modbus data and publish to MQTT
int poll_and_publish(bridge_context_t *ctx) {
    uint16_t holding_regs[10];
    uint16_t input_regs[5];
    char payload[512];
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    
    // Read holding registers (address 0, count 10)
    rc = modbus_read_registers(ctx->mb_ctx, 0, 10, holding_regs);
    if (rc == -1) {
        fprintf(stderr, "Modbus read error: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    // Read input registers (address 100, count 5)
    rc = modbus_read_input_registers(ctx->mb_ctx, 100, 5, input_regs);
    if (rc == -1) {
        fprintf(stderr, "Modbus input read error: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    // Create JSON payload
    time_t now = time(NULL);
    snprintf(payload, sizeof(payload),
             "{"
             "\"timestamp\":%ld,"
             "\"device\":\"PLC-001\","
             "\"holding_registers\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],"
             "\"input_registers\":[%u,%u,%u,%u,%u],"
             "\"temperature\":%.2f,"
             "\"pressure\":%.2f"
             "}",
             now,
             holding_regs[0], holding_regs[1], holding_regs[2], holding_regs[3],
             holding_regs[4], holding_regs[5], holding_regs[6], holding_regs[7],
             holding_regs[8], holding_regs[9],
             input_regs[0], input_regs[1], input_regs[2], input_regs[3], input_regs[4],
             (float)holding_regs[0] / 10.0,  // Temperature from register 0
             (float)holding_regs[1] / 100.0  // Pressure from register 1
    );
    
    // Publish to MQTT
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(ctx->mqtt_client, MQTT_TOPIC, &pubmsg, &token);
    rc = MQTTClient_waitForCompletion(ctx->mqtt_client, token, TIMEOUT);
    
    printf("Published: %s\n", payload);
    return rc;
}

// MQTT message callback for writing to Modbus
int mqtt_message_arrived(void *context, char *topicName, int topicLen, 
                         MQTTClient_message *message) {
    bridge_context_t *ctx = (bridge_context_t *)context;
    char *payload = (char *)message->payload;
    
    printf("Received MQTT command: %s\n", payload);
    
    // Simple command parser: "WRITE:100:1234" (address:value)
    if (strncmp(payload, "WRITE:", 6) == 0) {
        int address, value;
        if (sscanf(payload + 6, "%d:%d", &address, &value) == 2) {
            uint16_t reg_value = (uint16_t)value;
            if (modbus_write_register(ctx->mb_ctx, address, reg_value) == 1) {
                printf("Written %d to register %d\n", value, address);
            } else {
                fprintf(stderr, "Write failed: %s\n", modbus_strerror(errno));
            }
        }
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main(int argc, char *argv[]) {
    bridge_context_t bridge;
    bridge.running = 1;
    
    // Initialize connections
    bridge.mb_ctx = init_modbus(MODBUS_ADDRESS, MODBUS_PORT);
    if (!bridge.mb_ctx) {
        return 1;
    }
    
    if (init_mqtt(&bridge.mqtt_client) != 0) {
        modbus_close(bridge.mb_ctx);
        modbus_free(bridge.mb_ctx);
        return 1;
    }
    
    // Subscribe to command topic
    MQTTClient_setCallbacks(bridge.mqtt_client, &bridge, NULL, 
                           mqtt_message_arrived, NULL);
    MQTTClient_subscribe(bridge.mqtt_client, "factory/plc/commands", QOS);
    
    // Main polling loop
    printf("Starting bridge loop (Ctrl+C to exit)...\n");
    while (bridge.running) {
        poll_and_publish(&bridge);
        sleep(5);  // Poll every 5 seconds
    }
    
    // Cleanup
    MQTTClient_disconnect(bridge.mqtt_client, TIMEOUT);
    MQTTClient_destroy(&bridge.mqtt_client);
    modbus_close(bridge.mb_ctx);
    modbus_free(bridge.mb_ctx);
    
    return 0;
}
```

### Advanced C++ Implementation with Threading

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <json/json.h>
#include <modbus/modbus.h>
#include <mqtt/async_client.h>

class ModbusMQTTBridge {
private:
    modbus_t* modbus_ctx_;
    mqtt::async_client mqtt_client_;
    std::atomic<bool> running_;
    std::thread poll_thread_;
    
    const std::string mqtt_server_ = "tcp://localhost:1883";
    const std::string client_id_ = "ModbusBridge";
    const int poll_interval_ms_ = 5000;
    
public:
    ModbusMQTTBridge(const std::string& modbus_ip, int modbus_port)
        : mqtt_client_(mqtt_server_, client_id_), running_(false) {
        
        // Initialize Modbus
        modbus_ctx_ = modbus_new_tcp(modbus_ip.c_str(), modbus_port);
        if (!modbus_ctx_ || modbus_connect(modbus_ctx_) == -1) {
            throw std::runtime_error("Modbus connection failed");
        }
        
        // Initialize MQTT
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        
        try {
            mqtt_client_.connect(conn_opts)->wait();
            std::cout << "Connected to MQTT broker" << std::endl;
        } catch (const mqtt::exception& exc) {
            throw std::runtime_error("MQTT connection failed: " + std::string(exc.what()));
        }
        
        // Subscribe to command topic
        mqtt_client_.subscribe("factory/plc/commands", 1);
        mqtt_client_.start_consuming();
    }
    
    ~ModbusMQTTBridge() {
        stop();
        if (modbus_ctx_) {
            modbus_close(modbus_ctx_);
            modbus_free(modbus_ctx_);
        }
        mqtt_client_.disconnect()->wait();
    }
    
    void start() {
        running_ = true;
        poll_thread_ = std::thread(&ModbusMQTTBridge::poll_loop, this);
        
        // Command processing thread
        std::thread cmd_thread([this]() {
            while (running_) {
                auto msg = mqtt_client_.consume_message();
                if (msg) {
                    process_command(msg->to_string());
                }
            }
        });
        cmd_thread.detach();
    }
    
    void stop() {
        running_ = false;
        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
    }
    
private:
    void poll_loop() {
        while (running_) {
            try {
                uint16_t holding_regs[10];
                uint16_t input_regs[5];
                
                // Read Modbus data
                if (modbus_read_registers(modbus_ctx_, 0, 10, holding_regs) != -1 &&
                    modbus_read_input_registers(modbus_ctx_, 100, 5, input_regs) != -1) {
                    
                    // Create JSON payload
                    Json::Value root;
                    root["timestamp"] = static_cast<Json::Int64>(
                        std::chrono::system_clock::now().time_since_epoch().count());
                    root["device"] = "PLC-001";
                    
                    Json::Value holding_array(Json::arrayValue);
                    for (int i = 0; i < 10; i++) {
                        holding_array.append(holding_regs[i]);
                    }
                    root["holding_registers"] = holding_array;
                    
                    Json::Value input_array(Json::arrayValue);
                    for (int i = 0; i < 5; i++) {
                        input_array.append(input_regs[i]);
                    }
                    root["input_registers"] = input_array;
                    
                    root["temperature"] = holding_regs[0] / 10.0;
                    root["pressure"] = holding_regs[1] / 100.0;
                    
                    // Publish to MQTT
                    Json::StreamWriterBuilder writer;
                    std::string payload = Json::writeString(writer, root);
                    
                    auto pubmsg = mqtt::make_message("factory/plc/data", payload);
                    pubmsg->set_qos(1);
                    mqtt_client_.publish(pubmsg);
                    
                    std::cout << "Published: " << payload << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Poll error: " << e.what() << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
        }
    }
    
    void process_command(const std::string& cmd) {
        // Parse command: "WRITE:100:1234"
        if (cmd.substr(0, 6) == "WRITE:") {
            size_t pos1 = cmd.find(':', 6);
            if (pos1 != std::string::npos) {
                int address = std::stoi(cmd.substr(6, pos1 - 6));
                int value = std::stoi(cmd.substr(pos1 + 1));
                
                if (modbus_write_register(modbus_ctx_, address, 
                                         static_cast<uint16_t>(value)) == 1) {
                    std::cout << "Written " << value << " to register " 
                             << address << std::endl;
                }
            }
        }
    }
};

int main() {
    try {
        ModbusMQTTBridge bridge("192.168.1.100", 502);
        bridge.start();
        
        std::cout << "Bridge running. Press Enter to exit..." << std::endl;
        std::cin.get();
        
        bridge.stop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

### Using tokio-modbus and rumqttc

```rust
use tokio_modbus::prelude::*;
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde_json::json;
use std::time::Duration;
use tokio::time;
use anyhow::Result;

#[derive(Clone)]
struct BridgeConfig {
    modbus_host: String,
    modbus_port: u16,
    mqtt_broker: String,
    mqtt_port: u16,
    poll_interval: Duration,
    publish_topic: String,
    command_topic: String,
}

impl Default for BridgeConfig {
    fn default() -> Self {
        Self {
            modbus_host: "192.168.1.100".to_string(),
            modbus_port: 502,
            mqtt_broker: "localhost".to_string(),
            mqtt_port: 1883,
            poll_interval: Duration::from_secs(5),
            publish_topic: "factory/plc/data".to_string(),
            command_topic: "factory/plc/commands".to_string(),
        }
    }
}

struct ModbusMQTTBridge {
    config: BridgeConfig,
}

impl ModbusMQTTBridge {
    fn new(config: BridgeConfig) -> Self {
        Self { config }
    }
    
    async fn run(&self) -> Result<()> {
        // Create MQTT client
        let mqtt_options = MqttOptions::new(
            "ModbusBridge",
            &self.config.mqtt_broker,
            self.config.mqtt_port
        );
        let (mqtt_client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        
        // Subscribe to command topic
        mqtt_client
            .subscribe(&self.config.command_topic, QoS::AtLeastOnce)
            .await?;
        
        println!("MQTT connected to {}:{}", 
                 self.config.mqtt_broker, self.config.mqtt_port);
        
        // Create Modbus client
        let socket_addr = format!("{}:{}", 
                                 self.config.modbus_host, 
                                 self.config.modbus_port);
        
        // Spawn polling task
        let poll_config = self.config.clone();
        let poll_client = mqtt_client.clone();
        tokio::spawn(async move {
            Self::poll_task(poll_config, poll_client).await;
        });
        
        // Handle MQTT events (commands)
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    if let Ok(payload) = std::str::from_utf8(&p.payload) {
                        if let Err(e) = self.process_command(payload).await {
                            eprintln!("Command processing error: {}", e);
                        }
                    }
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("MQTT error: {}", e);
                    time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    }
    
    async fn poll_task(config: BridgeConfig, mqtt_client: AsyncClient) {
        let mut interval = time::interval(config.poll_interval);
        
        loop {
            interval.tick().await;
            
            match Self::read_and_publish(&config, &mqtt_client).await {
                Ok(_) => println!("Data published successfully"),
                Err(e) => eprintln!("Poll error: {}", e),
            }
        }
    }
    
    async fn read_and_publish(
        config: &BridgeConfig,
        mqtt_client: &AsyncClient
    ) -> Result<()> {
        let socket_addr = format!("{}:{}", config.modbus_host, config.modbus_port);
        let mut ctx = tcp::connect(socket_addr).await?;
        
        // Read holding registers
        let holding_regs = ctx.read_holding_registers(0, 10).await?;
        
        // Read input registers
        let input_regs = ctx.read_input_registers(100, 5).await?;
        
        // Create JSON payload
        let payload = json!({
            "timestamp": chrono::Utc::now().timestamp(),
            "device": "PLC-001",
            "holding_registers": holding_regs,
            "input_registers": input_regs,
            "temperature": holding_regs.get(0).unwrap_or(&0) / 10.0,
            "pressure": holding_regs.get(1).unwrap_or(&0) / 100.0,
        });
        
        let payload_str = serde_json::to_string(&payload)?;
        
        // Publish to MQTT
        mqtt_client
            .publish(
                &config.publish_topic,
                QoS::AtLeastOnce,
                false,
                payload_str.as_bytes()
            )
            .await?;
        
        println!("Published: {}", payload_str);
        Ok(())
    }
    
    async fn process_command(&self, command: &str) -> Result<()> {
        // Parse command: "WRITE:100:1234"
        if let Some(cmd) = command.strip_prefix("WRITE:") {
            let parts: Vec<&str> = cmd.split(':').collect();
            if parts.len() == 2 {
                let address: u16 = parts[0].parse()?;
                let value: u16 = parts[1].parse()?;
                
                let socket_addr = format!("{}:{}", 
                                         self.config.modbus_host, 
                                         self.config.modbus_port);
                let mut ctx = tcp::connect(socket_addr).await?;
                
                ctx.write_single_register(address, value).await?;
                println!("Written {} to register {}", value, address);
            }
        }
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let config = BridgeConfig::default();
    let bridge = ModbusMQTTBridge::new(config);
    
    println!("Starting Modbus-MQTT Bridge...");
    bridge.run().await?;
    
    Ok(())
}
```

### Advanced Rust Implementation with Configuration File

```rust
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Deserialize, Serialize)]
struct RegisterMapping {
    address: u16,
    count: u16,
    register_type: String,  // "holding", "input", "coil", "discrete"
    mqtt_topic: String,
    field_name: String,
    scale_factor: Option<f64>,
    unit: Option<String>,
}

#[derive(Debug, Deserialize)]
struct BridgeConfigFile {
    modbus: ModbusConfig,
    mqtt: MqttConfig,
    mappings: Vec<RegisterMapping>,
    poll_interval_ms: u64,
}

#[derive(Debug, Deserialize)]
struct ModbusConfig {
    host: String,
    port: u16,
    slave_id: u8,
}

#[derive(Debug, Deserialize)]
struct MqttConfig {
    broker: String,
    port: u16,
    client_id: String,
    base_topic: String,
}

impl BridgeConfigFile {
    fn from_file(path: &str) -> Result<Self> {
        let content = std::fs::read_to_string(path)?;
        let config: BridgeConfigFile = serde_json::from_str(&content)?;
        Ok(config)
    }
}

async fn process_mapping(
    ctx: &mut tcp::Context,
    mapping: &RegisterMapping,
    mqtt_client: &AsyncClient,
) -> Result<()> {
    let values = match mapping.register_type.as_str() {
        "holding" => ctx.read_holding_registers(mapping.address, mapping.count).await?,
        "input" => ctx.read_input_registers(mapping.address, mapping.count).await?,
        _ => return Err(anyhow::anyhow!("Unsupported register type")),
    };
    
    let mut processed_values: Vec<f64> = values
        .iter()
        .map(|&v| {
            let val = v as f64;
            mapping.scale_factor.map_or(val, |scale| val * scale)
        })
        .collect();
    
    let payload = json!({
        "field": mapping.field_name,
        "values": processed_values,
        "unit": mapping.unit,
        "timestamp": chrono::Utc::now().to_rfc3339(),
    });
    
    mqtt_client
        .publish(
            &mapping.mqtt_topic,
            QoS::AtLeastOnce,
            false,
            serde_json::to_string(&payload)?.as_bytes()
        )
        .await?;
    
    Ok(())
}
```

---

## Summary

**MQTT Bridge for Modbus** enables seamless integration between industrial Modbus devices and modern IoT ecosystems by:

1. **Protocol Translation**: Converting Modbus register data to MQTT messages (typically JSON format)
2. **Bidirectional Communication**: Supporting both data publishing (Modbus → MQTT) and command execution (MQTT → Modbus)
3. **Scalability**: Allowing multiple Modbus devices to feed data into cloud platforms through a single MQTT broker
4. **Flexibility**: Supporting various data formats, polling intervals, and topic structures through configuration

**Key Implementation Aspects**:
- **C/C++**: Offers high performance and low-level control using libraries like libmodbus and Paho MQTT
- **Rust**: Provides memory safety, async capabilities, and modern tooling with tokio-modbus and rumqttc
- **Common Features**: Configurable register mappings, data transformation/scaling, JSON payload formatting, error handling, and reconnection logic

**Typical Architecture**: Poll Modbus devices at regular intervals → Transform raw register values → Publish formatted data to MQTT topics → Cloud platforms subscribe and process data for analytics, visualization, and control.

This bridge pattern is foundational for Industry 4.0 initiatives, enabling legacy industrial equipment to participate in modern data-driven operations and cloud-based monitoring systems.