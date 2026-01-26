# MQTT Integration with React/Angular - Detailed Description

## Overview

React/Angular MQTT Integration involves connecting modern web applications to MQTT brokers using WebSockets, enabling real-time bidirectional communication between web clients and IoT devices or backend services. This integration allows web applications to subscribe to MQTT topics, publish messages, and receive live updates without polling, making it ideal for dashboards, monitoring systems, and interactive IoT applications.

## Key Concepts

### MQTT over WebSockets
- **Protocol**: MQTT can run over WebSocket (ws://) or secure WebSocket (wss://) connections
- **Port**: Typically port 80 (ws) or 443 (wss), avoiding firewall issues common with standard MQTT port 1883
- **Browser Compatibility**: WebSockets are natively supported in all modern browsers
- **Path**: Usually accessed via `/mqtt` or `/ws` endpoint on the broker

### Integration Architecture
1. **MQTT Broker** with WebSocket support (Mosquitto, HiveMQ, EMQX)
2. **Web Application** (React/Angular) running in browser
3. **MQTT Client Library** (Paho JavaScript, MQTT.js)
4. **Real-time Data Flow** between devices and web interface

---

## C/C++ Backend Example (MQTT Broker Interaction)

While web apps use JavaScript, backend services in C/C++ can publish data that web apps consume:

```c
#include <stdio.h>
#include <mosquitto.h>
#include <string.h>
#include <unistd.h>

// Callback for successful connection
void on_connect(struct mosquitto *mosq, void *obj, int result) {
    if(result == 0) {
        printf("Connected to broker successfully\n");
    } else {
        fprintf(stderr, "Connection failed: %d\n", result);
    }
}

// Callback for published messages
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
    printf("Message published (mid: %d)\n", mid);
}

int main() {
    struct mosquitto *mosq;
    int rc;
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create mosquitto instance
    mosq = mosquitto_new("web_backend_publisher", true, NULL);
    if(!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_publish_callback_set(mosq, on_publish);
    
    // Connect to broker
    rc = mosquitto_connect(mosq, "localhost", 1883, 60);
    if(rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Unable to connect: %s\n", mosquitto_strerror(rc));
        return 1;
    }
    
    // Start network loop in background
    mosquitto_loop_start(mosq);
    
    // Publish sensor data every 5 seconds
    for(int i = 0; i < 10; i++) {
        char payload[100];
        snprintf(payload, sizeof(payload), 
                "{\"temperature\": %.2f, \"humidity\": %.2f}", 
                20.0 + i * 0.5, 45.0 + i * 2.0);
        
        mosquitto_publish(mosq, NULL, "sensors/temperature", 
                         strlen(payload), payload, 1, false);
        
        sleep(5);
    }
    
    // Cleanup
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o mqtt_publisher mqtt_publisher.c -lmosquitto
```

---

## Rust Backend Example

```rust
use rumqttc::{MqttOptions, Client, QoS};
use serde_json::json;
use std::time::Duration;
use std::thread;

fn main() {
    // Configure MQTT options
    let mut mqttoptions = MqttOptions::new("rust_web_backend", "localhost", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(60));
    
    // Create client and connection
    let (mut client, mut connection) = Client::new(mqttoptions, 10);
    
    // Spawn connection handler thread
    thread::spawn(move || {
        for notification in connection.iter() {
            match notification {
                Ok(event) => println!("Event: {:?}", event),
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
            }
        }
    });
    
    // Publish telemetry data for web dashboard
    for i in 0..10 {
        let payload = json!({
            "device_id": "sensor_01",
            "temperature": 22.5 + (i as f64) * 0.3,
            "humidity": 50.0 + (i as f64) * 1.5,
            "timestamp": chrono::Utc::now().to_rfc3339()
        });
        
        let payload_str = payload.to_string();
        
        client
            .publish(
                "dashboard/telemetry",
                QoS::AtLeastOnce,
                false,
                payload_str.as_bytes()
            )
            .expect("Failed to publish");
        
        println!("Published: {}", payload_str);
        thread::sleep(Duration::from_secs(3));
    }
    
    println!("Publishing complete");
    thread::sleep(Duration::from_secs(2));
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
rumqttc = "0.24"
serde_json = "1.0"
chrono = "0.4"
```

---

## React Integration Example (JavaScript/TypeScript)

```javascript
import React, { useState, useEffect } from 'react';
import mqtt from 'mqtt';

const MQTTDashboard = () => {
    const [client, setClient] = useState(null);
    const [messages, setMessages] = useState([]);
    const [temperature, setTemperature] = useState(null);
    const [connected, setConnected] = useState(false);

    useEffect(() => {
        // Connect to MQTT broker via WebSockets
        const mqttClient = mqtt.connect('ws://localhost:8083/mqtt', {
            clientId: 'react_web_client_' + Math.random().toString(16).substr(2, 8),
            clean: true,
            reconnectPeriod: 1000,
        });

        mqttClient.on('connect', () => {
            console.log('Connected to MQTT broker');
            setConnected(true);
            
            // Subscribe to topics
            mqttClient.subscribe('sensors/temperature', { qos: 1 });
            mqttClient.subscribe('dashboard/telemetry', { qos: 1 });
        });

        mqttClient.on('message', (topic, message) => {
            const payload = message.toString();
            console.log(`Received on ${topic}: ${payload}`);
            
            try {
                const data = JSON.parse(payload);
                
                if (topic === 'sensors/temperature') {
                    setTemperature(data.temperature);
                }
                
                setMessages(prev => [...prev, { topic, data, time: new Date() }].slice(-10));
            } catch (e) {
                console.error('Failed to parse message:', e);
            }
        });

        mqttClient.on('error', (err) => {
            console.error('MQTT Error:', err);
            setConnected(false);
        });

        setClient(mqttClient);

        return () => {
            if (mqttClient) {
                mqttClient.end();
            }
        };
    }, []);

    const publishMessage = () => {
        if (client && connected) {
            const payload = JSON.stringify({
                command: 'toggle_light',
                timestamp: new Date().toISOString()
            });
            
            client.publish('devices/control', payload, { qos: 1 }, (err) => {
                if (err) {
                    console.error('Publish error:', err);
                }
            });
        }
    };

    return (
        <div className="dashboard">
            <h1>IoT Dashboard</h1>
            <div className="status">
                Status: {connected ? '✅ Connected' : '❌ Disconnected'}
            </div>
            
            <div className="metrics">
                <h2>Current Temperature</h2>
                <div className="temperature-display">
                    {temperature ? `${temperature}°C` : 'Waiting...'}
                </div>
            </div>

            <button onClick={publishMessage} disabled={!connected}>
                Toggle Device
            </button>

            <div className="messages">
                <h3>Recent Messages</h3>
                {messages.map((msg, idx) => (
                    <div key={idx} className="message">
                        <strong>{msg.topic}</strong>: {JSON.stringify(msg.data)}
                    </div>
                ))}
            </div>
        </div>
    );
};

export default MQTTDashboard;
```

**Package installation:**
```bash
npm install mqtt
```

---

## Angular Integration Example (TypeScript)

```typescript
// mqtt.service.ts
import { Injectable } from '@angular/core';
import { connect, MqttClient } from 'mqtt';
import { BehaviorSubject, Observable } from 'rxjs';

export interface MqttMessage {
    topic: string;
    payload: any;
    timestamp: Date;
}

@Injectable({
    providedIn: 'root'
})
export class MqttService {
    private client: MqttClient | null = null;
    private messagesSubject = new BehaviorSubject<MqttMessage[]>([]);
    private connectedSubject = new BehaviorSubject<boolean>(false);

    public messages$: Observable<MqttMessage[]> = this.messagesSubject.asObservable();
    public connected$: Observable<boolean> = this.connectedSubject.asObservable();

    constructor() {
        this.connect();
    }

    private connect(): void {
        this.client = connect('ws://localhost:8083/mqtt', {
            clientId: 'angular_client_' + Math.random().toString(16).substr(2, 8),
            clean: true,
            reconnectPeriod: 1000,
        });

        this.client.on('connect', () => {
            console.log('Angular MQTT client connected');
            this.connectedSubject.next(true);
        });

        this.client.on('message', (topic: string, message: Buffer) => {
            const payload = message.toString();
            
            try {
                const data = JSON.parse(payload);
                const mqttMessage: MqttMessage = {
                    topic,
                    payload: data,
                    timestamp: new Date()
                };
                
                const current = this.messagesSubject.value;
                this.messagesSubject.next([...current, mqttMessage].slice(-20));
            } catch (e) {
                console.error('Failed to parse MQTT message:', e);
            }
        });

        this.client.on('error', (error) => {
            console.error('MQTT Error:', error);
            this.connectedSubject.next(false);
        });
    }

    public subscribe(topic: string, qos: 0 | 1 | 2 = 1): void {
        if (this.client) {
            this.client.subscribe(topic, { qos }, (err) => {
                if (err) {
                    console.error(`Subscribe error for ${topic}:`, err);
                }
            });
        }
    }

    public publish(topic: string, message: any, qos: 0 | 1 | 2 = 1): void {
        if (this.client) {
            const payload = JSON.stringify(message);
            this.client.publish(topic, payload, { qos }, (err) => {
                if (err) {
                    console.error(`Publish error for ${topic}:`, err);
                }
            });
        }
    }

    public disconnect(): void {
        if (this.client) {
            this.client.end();
            this.connectedSubject.next(false);
        }
    }
}
```

---

## Summary

**React/Angular MQTT Integration** enables modern web applications to communicate with IoT devices and backend services in real-time through MQTT over WebSockets. This architecture allows:

- **Real-time Dashboards**: Live telemetry data visualization without polling
- **Bidirectional Control**: Web interfaces can both monitor and control devices
- **Scalable Architecture**: MQTT's pub/sub model efficiently handles multiple subscribers
- **Cross-Platform**: C/C++ and Rust backends can publish data consumed by JavaScript frontends

**Key Implementation Points:**
- Web apps connect via WebSocket URLs (ws:// or wss://)
- JavaScript libraries (MQTT.js, Paho) handle browser-side MQTT communication
- Backend services in C/C++ or Rust publish device data to topics
- QoS levels ensure message delivery reliability
- Reconnection logic maintains persistent connections

**Common Use Cases:**
- IoT monitoring dashboards
- Industrial automation HMIs
- Smart home control panels
- Real-time analytics displays
- Collaborative applications with live updates

This integration pattern bridges the gap between traditional embedded systems and modern web technologies, enabling sophisticated real-time applications.