# DPV2 Slave-to-Slave Communication in Profibus

## Overview

DPV2 Slave-to-Slave Communication is an advanced feature in the Profibus protocol that enables direct data exchange between slave devices without requiring the master to act as an intermediary. This implements a **publisher-subscriber model** where one slave (publisher) can broadcast data that multiple other slaves (subscribers) can receive and process directly.

## Key Concepts

### Traditional vs. DPV2 Communication

**Traditional Profibus (DPV0/DPV1):**
- All communication flows through the master
- Master reads from Slave A, then writes to Slave B
- Introduces latency and increases bus load
- Master CPU becomes a bottleneck

**DPV2 Slave-to-Slave:**
- Slaves communicate directly using MSAC2 (Master-Slave-Acyclic-Communication-2) services
- Publisher broadcasts data using DPV2 multicast
- Subscribers receive data with minimal master involvement
- Reduces cycle times and master workload

### Communication Model

The publisher-subscriber pattern works as follows:

1. **Publisher Configuration**: A slave is configured to publish specific data blocks
2. **Subscriber Registration**: Other slaves register interest in published data
3. **Data Broadcasting**: Publisher sends data using MS2 services
4. **Direct Reception**: Subscribers receive data directly from the bus
5. **Master Supervision**: Master monitors but doesn't relay data

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// DPV2 Message Types
#define DPV2_DATA_TRANSPORT     0x01
#define DPV2_MULTICAST          0x02
#define DPV2_ALARM              0x03

// DPV2 Service Access Points
#define SAP_PUBLISHER           50
#define SAP_SUBSCRIBER          51

// DPV2 Frame Structure
typedef struct {
    uint8_t frame_control;
    uint8_t destination_addr;
    uint8_t source_addr;
    uint8_t function_code;
    uint8_t data_unit_ref;
    uint16_t data_length;
    uint8_t data[246];  // Maximum DPV2 payload
    uint8_t fcs;        // Frame Check Sequence
} dpv2_frame_t;

// Publisher Data Structure
typedef struct {
    uint8_t publisher_addr;
    uint8_t group_id;       // Multicast group identifier
    uint16_t data_id;       // Published data identifier
    uint8_t* data_ptr;
    uint16_t data_size;
    bool is_active;
} dpv2_publisher_t;

// Subscriber Data Structure
typedef struct {
    uint8_t subscriber_addr;
    uint8_t group_id;       // Subscribed group
    uint16_t data_id;       // Expected data identifier
    void (*callback)(uint8_t* data, uint16_t size);
    bool is_active;
} dpv2_subscriber_t;

// Publisher Implementation
typedef struct {
    dpv2_publisher_t config;
    uint32_t publish_count;
    uint32_t error_count;
} publisher_context_t;

// Initialize Publisher
bool dpv2_publisher_init(publisher_context_t* ctx, 
                         uint8_t addr, 
                         uint8_t group_id,
                         uint16_t data_id) {
    if (!ctx) return false;
    
    memset(ctx, 0, sizeof(publisher_context_t));
    ctx->config.publisher_addr = addr;
    ctx->config.group_id = group_id;
    ctx->config.data_id = data_id;
    ctx->config.is_active = true;
    
    return true;
}

// Publish Data (Multicast)
int dpv2_publish_data(publisher_context_t* ctx, 
                      uint8_t* data, 
                      uint16_t size) {
    if (!ctx || !data || size > 246) return -1;
    if (!ctx->config.is_active) return -2;
    
    dpv2_frame_t frame;
    memset(&frame, 0, sizeof(dpv2_frame_t));
    
    // Build DPV2 multicast frame
    frame.frame_control = 0x73;  // DPV2 multicast with FC bit
    frame.destination_addr = ctx->config.group_id;  // Group address
    frame.source_addr = ctx->config.publisher_addr;
    frame.function_code = DPV2_MULTICAST;
    frame.data_unit_ref = (ctx->publish_count & 0xFF);
    frame.data_length = size + 2;  // +2 for data_id
    
    // Pack data ID and payload
    frame.data[0] = (ctx->config.data_id >> 8) & 0xFF;
    frame.data[1] = ctx->config.data_id & 0xFF;
    memcpy(&frame.data[2], data, size);
    
    // Calculate FCS (simplified - use proper CRC in production)
    frame.fcs = 0;
    for (int i = 0; i < size + 2; i++) {
        frame.fcs ^= frame.data[i];
    }
    
    // Send frame to Profibus stack (implementation-specific)
    if (profibus_send_frame(&frame)) {
        ctx->publish_count++;
        return 0;
    }
    
    ctx->error_count++;
    return -3;
}

// Subscriber Implementation
typedef struct {
    dpv2_subscriber_t config;
    uint32_t receive_count;
    uint32_t missed_count;
    uint8_t last_data_ref;
} subscriber_context_t;

// Initialize Subscriber
bool dpv2_subscriber_init(subscriber_context_t* ctx,
                          uint8_t addr,
                          uint8_t group_id,
                          uint16_t data_id,
                          void (*callback)(uint8_t*, uint16_t)) {
    if (!ctx || !callback) return false;
    
    memset(ctx, 0, sizeof(subscriber_context_t));
    ctx->config.subscriber_addr = addr;
    ctx->config.group_id = group_id;
    ctx->config.data_id = data_id;
    ctx->config.callback = callback;
    ctx->config.is_active = true;
    
    // Register with group multicast address
    return profibus_join_multicast_group(group_id);
}

// Process Received Frame
void dpv2_subscriber_process(subscriber_context_t* ctx, 
                             dpv2_frame_t* frame) {
    if (!ctx || !frame || !ctx->config.is_active) return;
    
    // Check if frame is for our group
    if (frame->destination_addr != ctx->config.group_id) return;
    
    // Verify frame type
    if (frame->function_code != DPV2_MULTICAST) return;
    
    // Extract data ID
    uint16_t received_data_id = (frame->data[0] << 8) | frame->data[1];
    
    // Check if this is the data we're interested in
    if (received_data_id != ctx->config.data_id) return;
    
    // Check for missed frames
    if (ctx->receive_count > 0) {
        uint8_t expected_ref = (ctx->last_data_ref + 1) & 0xFF;
        if (frame->data_unit_ref != expected_ref) {
            ctx->missed_count++;
        }
    }
    
    ctx->last_data_ref = frame->data_unit_ref;
    ctx->receive_count++;
    
    // Invoke callback with payload data
    ctx->config.callback(&frame->data[2], frame->data_length - 2);
}

// Example: Sensor Data Publisher
typedef struct {
    float temperature;
    float pressure;
    uint16_t status;
} sensor_data_t;

void sensor_publisher_task(void) {
    publisher_context_t pub_ctx;
    sensor_data_t sensor_data;
    
    // Initialize publisher on slave address 5, group 10, data ID 100
    dpv2_publisher_init(&pub_ctx, 5, 10, 100);
    
    while (1) {
        // Read sensor values
        sensor_data.temperature = read_temperature();
        sensor_data.pressure = read_pressure();
        sensor_data.status = get_sensor_status();
        
        // Publish sensor data
        dpv2_publish_data(&pub_ctx, 
                         (uint8_t*)&sensor_data, 
                         sizeof(sensor_data_t));
        
        // Wait 100ms (typical cycle)
        delay_ms(100);
    }
}

// Example: Actuator Data Subscriber
void actuator_data_callback(uint8_t* data, uint16_t size) {
    if (size != sizeof(sensor_data_t)) return;
    
    sensor_data_t* sensor = (sensor_data_t*)data;
    
    // React to sensor data
    if (sensor->temperature > 85.0f) {
        activate_cooling();
    }
    
    if (sensor->pressure < 100.0f) {
        trigger_alarm();
    }
}

void actuator_subscriber_task(void) {
    subscriber_context_t sub_ctx;
    
    // Initialize subscriber on slave address 8, group 10, data ID 100
    dpv2_subscriber_init(&sub_ctx, 8, 10, 100, actuator_data_callback);
    
    while (1) {
        dpv2_frame_t frame;
        
        // Wait for incoming frames
        if (profibus_receive_frame(&frame, 1000)) {
            dpv2_subscriber_process(&sub_ctx, &frame);
        }
    }
}

// External function declarations (implementation-specific)
extern bool profibus_send_frame(dpv2_frame_t* frame);
extern bool profibus_receive_frame(dpv2_frame_t* frame, uint32_t timeout_ms);
extern bool profibus_join_multicast_group(uint8_t group_id);
extern float read_temperature(void);
extern float read_pressure(void);
extern uint16_t get_sensor_status(void);
extern void activate_cooling(void);
extern void trigger_alarm(void);
extern void delay_ms(uint32_t ms);
```

## Rust Implementation

```rust
use std::sync::{Arc, Mutex};
use std::collections::HashMap;

// DPV2 Constants
const DPV2_DATA_TRANSPORT: u8 = 0x01;
const DPV2_MULTICAST: u8 = 0x02;
const DPV2_ALARM: u8 = 0x03;
const MAX_DPV2_PAYLOAD: usize = 246;

// DPV2 Frame Structure
#[derive(Debug, Clone)]
pub struct Dpv2Frame {
    pub frame_control: u8,
    pub destination_addr: u8,
    pub source_addr: u8,
    pub function_code: u8,
    pub data_unit_ref: u8,
    pub data_length: u16,
    pub data: Vec<u8>,
    pub fcs: u8,
}

impl Dpv2Frame {
    pub fn new_multicast(
        source: u8,
        group: u8,
        data_ref: u8,
        payload: &[u8],
    ) -> Result<Self, &'static str> {
        if payload.len() > MAX_DPV2_PAYLOAD {
            return Err("Payload exceeds maximum size");
        }

        let mut frame = Dpv2Frame {
            frame_control: 0x73, // DPV2 multicast
            destination_addr: group,
            source_addr: source,
            function_code: DPV2_MULTICAST,
            data_unit_ref: data_ref,
            data_length: payload.len() as u16,
            data: payload.to_vec(),
            fcs: 0,
        };

        // Calculate frame check sequence
        frame.fcs = frame.data.iter().fold(0u8, |acc, &byte| acc ^ byte);

        Ok(frame)
    }
}

// Publisher Configuration
#[derive(Debug, Clone)]
pub struct PublisherConfig {
    pub address: u8,
    pub group_id: u8,
    pub data_id: u16,
}

// Publisher Statistics
#[derive(Debug, Default)]
pub struct PublisherStats {
    pub publish_count: u64,
    pub error_count: u64,
}

// DPV2 Publisher
pub struct Dpv2Publisher {
    config: PublisherConfig,
    stats: Arc<Mutex<PublisherStats>>,
    data_ref: u8,
    active: bool,
}

impl Dpv2Publisher {
    pub fn new(address: u8, group_id: u8, data_id: u16) -> Self {
        Dpv2Publisher {
            config: PublisherConfig {
                address,
                group_id,
                data_id,
            },
            stats: Arc::new(Mutex::new(PublisherStats::default())),
            data_ref: 0,
            active: true,
        }
    }

    pub fn publish(&mut self, data: &[u8]) -> Result<(), String> {
        if !self.active {
            return Err("Publisher not active".to_string());
        }

        if data.len() > MAX_DPV2_PAYLOAD - 2 {
            return Err("Data too large".to_string());
        }

        // Prepare payload with data ID
        let mut payload = Vec::with_capacity(data.len() + 2);
        payload.push((self.config.data_id >> 8) as u8);
        payload.push(self.config.data_id as u8);
        payload.extend_from_slice(data);

        // Create multicast frame
        let frame = Dpv2Frame::new_multicast(
            self.config.address,
            self.config.group_id,
            self.data_ref,
            &payload,
        )?;

        // Send frame (implementation would interface with actual Profibus stack)
        match self.send_frame(&frame) {
            Ok(_) => {
                self.data_ref = self.data_ref.wrapping_add(1);
                let mut stats = self.stats.lock().unwrap();
                stats.publish_count += 1;
                Ok(())
            }
            Err(e) => {
                let mut stats = self.stats.lock().unwrap();
                stats.error_count += 1;
                Err(e)
            }
        }
    }

    pub fn get_stats(&self) -> PublisherStats {
        self.stats.lock().unwrap().clone()
    }

    pub fn activate(&mut self) {
        self.active = true;
    }

    pub fn deactivate(&mut self) {
        self.active = false;
    }

    // Placeholder for actual Profibus stack interface
    fn send_frame(&self, frame: &Dpv2Frame) -> Result<(), String> {
        // In real implementation, this would interface with Profibus hardware
        println!(
            "Publishing to group {}: {} bytes",
            self.config.group_id,
            frame.data.len()
        );
        Ok(())
    }
}

// Subscriber Callback Type
pub type SubscriberCallback = Arc<dyn Fn(&[u8]) + Send + Sync>;

// Subscriber Configuration
#[derive(Clone)]
pub struct SubscriberConfig {
    pub address: u8,
    pub group_id: u8,
    pub data_id: u16,
    pub callback: SubscriberCallback,
}

// Subscriber Statistics
#[derive(Debug, Default, Clone)]
pub struct SubscriberStats {
    pub receive_count: u64,
    pub missed_count: u64,
    pub error_count: u64,
}

// DPV2 Subscriber
pub struct Dpv2Subscriber {
    config: SubscriberConfig,
    stats: Arc<Mutex<SubscriberStats>>,
    last_data_ref: Option<u8>,
    active: bool,
}

impl Dpv2Subscriber {
    pub fn new(
        address: u8,
        group_id: u8,
        data_id: u16,
        callback: SubscriberCallback,
    ) -> Result<Self, String> {
        // Join multicast group
        Self::join_multicast_group(group_id)?;

        Ok(Dpv2Subscriber {
            config: SubscriberConfig {
                address,
                group_id,
                data_id,
                callback,
            },
            stats: Arc::new(Mutex::new(SubscriberStats::default())),
            last_data_ref: None,
            active: true,
        })
    }

    pub fn process_frame(&mut self, frame: &Dpv2Frame) {
        if !self.active {
            return;
        }

        // Check if frame is for our group
        if frame.destination_addr != self.config.group_id {
            return;
        }

        // Verify frame type
        if frame.function_code != DPV2_MULTICAST {
            return;
        }

        // Extract and verify data ID
        if frame.data.len() < 2 {
            return;
        }

        let received_data_id = ((frame.data[0] as u16) << 8) | (frame.data[1] as u16);
        if received_data_id != self.config.data_id {
            return;
        }

        // Check for missed frames
        if let Some(last_ref) = self.last_data_ref {
            let expected_ref = last_ref.wrapping_add(1);
            if frame.data_unit_ref != expected_ref {
                let mut stats = self.stats.lock().unwrap();
                stats.missed_count += 1;
            }
        }

        self.last_data_ref = Some(frame.data_unit_ref);

        // Invoke callback with payload (skip data ID bytes)
        let payload = &frame.data[2..];
        (self.config.callback)(payload);

        let mut stats = self.stats.lock().unwrap();
        stats.receive_count += 1;
    }

    pub fn get_stats(&self) -> SubscriberStats {
        self.stats.lock().unwrap().clone()
    }

    pub fn activate(&mut self) {
        self.active = true;
    }

    pub fn deactivate(&mut self) {
        self.active = false;
    }

    // Placeholder for joining multicast group
    fn join_multicast_group(group_id: u8) -> Result<(), String> {
        println!("Joining multicast group {}", group_id);
        Ok(())
    }
}

// Example: Sensor Data Structure
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct SensorData {
    pub temperature: f32,
    pub pressure: f32,
    pub status: u16,
}

impl SensorData {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(10);
        bytes.extend_from_slice(&self.temperature.to_le_bytes());
        bytes.extend_from_slice(&self.pressure.to_le_bytes());
        bytes.extend_from_slice(&self.status.to_le_bytes());
        bytes
    }

    pub fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 10 {
            return None;
        }

        Some(SensorData {
            temperature: f32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
            pressure: f32::from_le_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]),
            status: u16::from_le_bytes([bytes[8], bytes[9]]),
        })
    }
}

// Example: Complete Publisher-Subscriber System
pub struct SlaveToSlaveSystem {
    publishers: HashMap<u16, Dpv2Publisher>,
    subscribers: HashMap<u16, Dpv2Subscriber>,
}

impl SlaveToSlaveSystem {
    pub fn new() -> Self {
        SlaveToSlaveSystem {
            publishers: HashMap::new(),
            subscribers: HashMap::new(),
        }
    }

    pub fn add_publisher(
        &mut self,
        data_id: u16,
        address: u8,
        group_id: u8,
    ) -> Result<(), String> {
        let publisher = Dpv2Publisher::new(address, group_id, data_id);
        self.publishers.insert(data_id, publisher);
        Ok(())
    }

    pub fn add_subscriber(
        &mut self,
        data_id: u16,
        address: u8,
        group_id: u8,
        callback: SubscriberCallback,
    ) -> Result<(), String> {
        let subscriber = Dpv2Subscriber::new(address, group_id, data_id, callback)?;
        self.subscribers.insert(data_id, subscriber);
        Ok(())
    }

    pub fn publish(&mut self, data_id: u16, data: &[u8]) -> Result<(), String> {
        match self.publishers.get_mut(&data_id) {
            Some(publisher) => publisher.publish(data),
            None => Err(format!("Publisher {} not found", data_id)),
        }
    }

    pub fn process_incoming_frame(&mut self, frame: &Dpv2Frame) {
        for subscriber in self.subscribers.values_mut() {
            subscriber.process_frame(frame);
        }
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_publisher_subscriber() {
        let mut system = SlaveToSlaveSystem::new();

        // Create callback for subscriber
        let callback = Arc::new(|data: &[u8]| {
            if let Some(sensor_data) = SensorData::from_bytes(data) {
                println!("Received: temp={}, pressure={}, status={}",
                    sensor_data.temperature,
                    sensor_data.pressure,
                    sensor_data.status
                );
            }
        });

        // Add publisher (slave 5, group 10, data ID 100)
        system.add_publisher(100, 5, 10).unwrap();

        // Add subscriber (slave 8, group 10, data ID 100)
        system.add_subscriber(100, 8, 10, callback).unwrap();

        // Publish sensor data
        let sensor_data = SensorData {
            temperature: 72.5,
            pressure: 101.3,
            status: 0x01,
        };

        system.publish(100, &sensor_data.to_bytes()).unwrap();
    }
}
```

## Summary

**DPV2 Slave-to-Slave Communication** revolutionizes Profibus architectures by enabling direct peer-to-peer data exchange between slaves using a publisher-subscriber model. This advanced feature significantly reduces communication latency, decreases master CPU load, and enables more responsive distributed control systems.

**Key Benefits:**
- **Reduced Latency**: Direct communication eliminates master relay delays
- **Lower Bus Load**: Master doesn't need to read and re-transmit data
- **Scalability**: Multiple subscribers can receive the same data efficiently
- **Flexibility**: Decouples data producers from consumers

**Implementation Considerations:**
- Requires DPV2-capable hardware on all participating slaves
- Multicast addressing must be properly configured
- Frame sequencing helps detect missed transmissions
- Error handling and timeout mechanisms are critical

**Typical Applications:**
- High-speed sensor data distribution to multiple actuators
- Coordinated multi-axis motion control systems
- Distributed safety systems requiring rapid peer communication
- Real-time process synchronization across multiple control nodes

The code examples demonstrate complete publisher and subscriber implementations with frame handling, statistics tracking, and practical sensor/actuator scenarios that showcase the power of direct slave-to-slave communication in industrial automation systems.