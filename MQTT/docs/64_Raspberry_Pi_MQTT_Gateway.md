# Raspberry Pi MQTT Gateway

## Overview

A Raspberry Pi MQTT Gateway serves as a protocol bridge at the network edge, enabling communication between devices using different protocols (like Modbus, Zigbee, BLE, or serial devices) and MQTT-based cloud or local systems. The Raspberry Pi's versatility, GPIO pins, and support for various communication interfaces make it ideal for this role.

## Key Concepts

**Protocol Bridging**: Converting data from non-MQTT protocols (Modbus RTU/TCP, Zigbee, BLE, serial) into MQTT messages and vice versa.

**Edge Processing**: Performing data filtering, aggregation, or transformation locally before publishing to MQTT brokers.

**Bidirectional Communication**: Receiving MQTT commands and translating them back to the original protocol for device control.

**Multiple Interface Support**: Using USB, serial, SPI, I2C, or network interfaces to communicate with various devices.

## C/C++ Implementation

```c
// mqtt_modbus_gateway.c
// Bridges Modbus RTU devices to MQTT using libmodbus and mosquitto

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <modbus.h>
#include <mosquitto.h>
#include <signal.h>

#define MQTT_BROKER "localhost"
#define MQTT_PORT 1883
#define MODBUS_DEVICE "/dev/ttyUSB0"
#define MODBUS_BAUD 9600
#define POLL_INTERVAL 5

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    printf("Connected to MQTT broker with code %d\n", rc);
    if (rc == 0) {
        // Subscribe to command topics
        mosquitto_subscribe(mosq, NULL, "gateway/modbus/+/write/#", 0);
    }
}

void on_message(struct mosquitto *mosq, void *obj, 
                const struct mosquitto_message *msg) {
    modbus_t *ctx = (modbus_t *)obj;
    char topic[256];
    int slave_id, address, value;
    
    strcpy(topic, msg->topic);
    
    // Parse topic: gateway/modbus/{slave_id}/write/{address}
    if (sscanf(topic, "gateway/modbus/%d/write/%d", &slave_id, &address) == 2) {
        value = atoi((char *)msg->payload);
        
        modbus_set_slave(ctx, slave_id);
        if (modbus_write_register(ctx, address, value) == 1) {
            printf("Wrote %d to register %d on slave %d\n", 
                   value, address, slave_id);
        } else {
            fprintf(stderr, "Modbus write failed: %s\n", modbus_strerror(errno));
        }
    }
}

int read_modbus_registers(modbus_t *ctx, struct mosquitto *mosq, 
                         int slave_id, int start_addr, int count) {
    uint16_t registers[count];
    char topic[256];
    char payload[64];
    
    modbus_set_slave(ctx, slave_id);
    
    if (modbus_read_registers(ctx, start_addr, count, registers) == count) {
        for (int i = 0; i < count; i++) {
            snprintf(topic, sizeof(topic), 
                    "gateway/modbus/%d/register/%d", 
                    slave_id, start_addr + i);
            snprintf(payload, sizeof(payload), "%u", registers[i]);
            
            mosquitto_publish(mosq, NULL, topic, strlen(payload), 
                            payload, 0, false);
        }
        return 0;
    }
    
    fprintf(stderr, "Modbus read failed: %s\n", modbus_strerror(errno));
    return -1;
}

int main(int argc, char *argv[]) {
    struct mosquitto *mosq;
    modbus_t *ctx;
    int rc;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize Modbus RTU
    ctx = modbus_new_rtu(MODBUS_DEVICE, MODBUS_BAUD, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        return 1;
    }
    
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Modbus connection failed: %s\n", 
                modbus_strerror(errno));
        modbus_free(ctx);
        return 1;
    }
    
    // Initialize MQTT
    mosquitto_lib_init();
    mosq = mosquitto_new("raspberry_pi_gateway", true, ctx);
    
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        modbus_close(ctx);
        modbus_free(ctx);
        return 1;
    }
    
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    
    rc = mosquitto_connect(mosq, MQTT_BROKER, MQTT_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT connection failed\n");
        mosquitto_destroy(mosq);
        modbus_close(ctx);
        modbus_free(ctx);
        return 1;
    }
    
    mosquitto_loop_start(mosq);
    
    printf("Gateway running. Press Ctrl+C to exit.\n");
    
    // Main polling loop
    while (running) {
        // Poll slave ID 1, registers 0-9
        read_modbus_registers(ctx, mosq, 1, 0, 10);
        
        // Poll slave ID 2, registers 0-4
        read_modbus_registers(ctx, mosq, 2, 0, 5);
        
        sleep(POLL_INTERVAL);
    }
    
    printf("\nShutting down...\n");
    
    mosquitto_loop_stop(mosq, false);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o mqtt_modbus_gateway mqtt_modbus_gateway.c -lmosquitto -lmodbus
```

## Rust Implementation

```rust
// mqtt_serial_gateway.rs
// Bridges serial devices to MQTT with JSON data transformation

use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use serialport::{SerialPort, SerialPortBuilder};
use serde::{Deserialize, Serialize};
use std::time::Duration;
use std::thread;
use std::sync::{Arc, Mutex};
use std::io::{Read, Write};

#[derive(Serialize, Deserialize, Debug)]
struct SensorData {
    device_id: String,
    temperature: f32,
    humidity: f32,
    timestamp: u64,
}

#[derive(Deserialize, Debug)]
struct CommandMessage {
    device_id: String,
    command: String,
    value: Option<String>,
}

struct SerialGateway {
    mqtt_client: Client,
    serial_port: Box<dyn SerialPort>,
}

impl SerialGateway {
    fn new(broker: &str, port: u16, serial_device: &str) -> Result<Self, Box<dyn std::error::Error>> {
        // Configure MQTT
        let mut mqtt_options = MqttOptions::new("rpi_serial_gateway", broker, port);
        mqtt_options.set_keep_alive(Duration::from_secs(30));
        
        let (client, mut connection) = Client::new(mqtt_options, 10);
        
        // Subscribe to command topics
        client.subscribe("gateway/serial/+/command", QoS::AtLeastOnce)?;
        
        // Spawn MQTT event loop thread
        thread::spawn(move || {
            for notification in connection.iter() {
                if let Ok(Event::Incoming(Packet::Publish(p))) = notification {
                    println!("Received: {:?}", p);
                }
            }
        });
        
        // Configure serial port
        let port = serialport::new(serial_device, 9600)
            .timeout(Duration::from_millis(1000))
            .open()?;
        
        Ok(SerialGateway {
            mqtt_client: client,
            serial_port: port,
        })
    }
    
    fn read_serial_data(&mut self) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
        let mut buffer = vec![0u8; 1024];
        let bytes_read = self.serial_port.read(&mut buffer)?;
        buffer.truncate(bytes_read);
        Ok(buffer)
    }
    
    fn parse_and_publish(&mut self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        // Parse serial data (assuming newline-delimited JSON)
        let data_str = String::from_utf8_lossy(data);
        
        for line in data_str.lines() {
            if let Ok(sensor_data) = serde_json::from_str::<SensorData>(line) {
                let topic = format!("gateway/serial/{}/data", sensor_data.device_id);
                let payload = serde_json::to_string(&sensor_data)?;
                
                self.mqtt_client.publish(
                    topic,
                    QoS::AtLeastOnce,
                    false,
                    payload.as_bytes(),
                )?;
                
                println!("Published: {} -> {}", sensor_data.device_id, payload);
            }
        }
        
        Ok(())
    }
    
    fn handle_mqtt_command(&mut self, topic: &str, payload: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        if let Ok(cmd) = serde_json::from_slice::<CommandMessage>(payload) {
            // Format command for serial device
            let serial_cmd = format!("CMD:{}:{}:{}\n", 
                                    cmd.device_id, 
                                    cmd.command,
                                    cmd.value.unwrap_or_default());
            
            self.serial_port.write_all(serial_cmd.as_bytes())?;
            println!("Sent to serial: {}", serial_cmd.trim());
        }
        
        Ok(())
    }
    
    fn run(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("Gateway started. Reading from serial port...");
        
        loop {
            match self.read_serial_data() {
                Ok(data) if !data.is_empty() => {
                    if let Err(e) = self.parse_and_publish(&data) {
                        eprintln!("Parse/publish error: {}", e);
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    // Timeout is normal, continue
                    continue;
                }
                Err(e) => {
                    eprintln!("Serial read error: {}", e);
                    thread::sleep(Duration::from_secs(1));
                }
                _ => {}
            }
            
            thread::sleep(Duration::from_millis(100));
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut gateway = SerialGateway::new(
        "localhost",
        1883,
        "/dev/ttyACM0"
    )?;
    
    gateway.run()
}
```

**Cargo.toml:**
```toml
[dependencies]
rumqttc = "0.24"
serialport = "4.2"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
```

## Architecture Patterns

### Multi-Protocol Gateway
```
┌─────────────────────────────────────┐
│      Raspberry Pi Gateway           │
│  ┌───────────────────────────────┐  │
│  │   Protocol Handlers           │  │
│  │  ┌─────┐ ┌─────┐ ┌─────┐      │  │
│  │  │Modbr│ │ BLE │ │Zigbe│      │  │
│  │  └──┬──┘ └──┬──┘ └──┬──┘      │  │
│  └─────┼───────┼───────┼─────────┘  │
│        └───────┴───────┘            │
│              │                      │
│        ┌─────▼─────┐                │
│        │   MQTT    │                │
│        │  Client   │                │
│        └─────┬─────┘                │
└──────────────┼──────────────────────┘
               │
          ┌────▼────┐
          │  MQTT   │
          │ Broker  │
          └─────────┘
```

## Summary

Raspberry Pi MQTT Gateways provide powerful edge computing capabilities for IoT deployments by bridging legacy and proprietary protocols to MQTT. The C/C++ implementation demonstrates Modbus RTU bridging with bidirectional communication, while the Rust example shows serial device integration with JSON data transformation. These gateways enable local data processing, protocol translation, and reduced cloud dependency, making them essential for industrial IoT, building automation, and distributed sensor networks. The Raspberry Pi's GPIO, multiple communication interfaces, and processing power make it an ideal platform for creating flexible, cost-effective protocol bridges at the network edge.