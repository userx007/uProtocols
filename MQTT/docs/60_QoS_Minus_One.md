# QoS -1 (Quality Minus One): Fire-and-Forget Messaging

## Detailed Description

QoS -1, also known as "Quality Minus One" or "Fire-and-Forget," is a non-standard MQTT extension designed for extreme low-power and constrained network scenarios. Unlike the standard MQTT QoS levels (0, 1, and 2), QoS -1 eliminates virtually all protocol overhead to maximize battery life and minimize bandwidth usage.

### Key Characteristics

**Minimal Protocol Overhead**: QoS -1 strips away acknowledgments, session state, and even connection handshakes in some implementations. Messages are transmitted without expecting any response from the broker.

**No Connection State**: Devices don't maintain a persistent connection to the broker. They wake up, transmit their payload, and immediately return to sleep without waiting for any acknowledgment.

**Ultra-Low Power**: By eliminating the need to keep radios active waiting for ACKs, devices can achieve sleep currents in the microampere range, extending battery life from months to years.

**No Delivery Guarantees**: Messages may be lost due to network issues, broker unavailability, or collisions. This is acceptable for applications where occasional data loss is tolerable (e.g., frequent sensor readings where the next reading will arrive soon).

**Implementation Variants**: Since QoS -1 isn't part of the official MQTT specification, implementations vary. Some use UDP transport instead of TCP, while others use minimal TCP connections that close immediately after transmission.

## Use Cases

- Battery-powered environmental sensors (temperature, humidity) that report readings every few minutes
- Agricultural IoT sensors in remote locations with intermittent connectivity
- Low-priority telemetry data where occasional loss is acceptable
- Devices deployed in massive quantities where connection overhead becomes prohibitive

## C/C++ Implementation Example

```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>

// Minimal MQTT PUBLISH packet structure for QoS -1
typedef struct {
    uint8_t header;        // Fixed header
    uint8_t remaining_len; // Remaining length
    uint8_t topic_len_msb; // Topic length MSB
    uint8_t topic_len_lsb; // Topic length LSB
    char topic[32];        // Topic name (variable)
    char payload[64];      // Payload (variable)
} __attribute__((packed)) mqtt_qos_minus_one_t;

// Send QoS -1 message via UDP (common implementation)
int send_qos_minus_one_udp(const char* broker_ip, int broker_port,
                           const char* topic, const char* payload) {
    int sock;
    struct sockaddr_in broker_addr;
    mqtt_qos_minus_one_t packet;
    
    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Configure broker address
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(broker_port);
    inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr);
    
    // Build minimal MQTT PUBLISH packet
    // Fixed header: PUBLISH (0x30) with QoS 0, no flags
    packet.header = 0x30;
    
    // Calculate remaining length
    uint16_t topic_len = strlen(topic);
    uint16_t payload_len = strlen(payload);
    packet.remaining_len = 2 + topic_len + payload_len; // 2 bytes for topic length
    
    // Topic length (big-endian)
    packet.topic_len_msb = (topic_len >> 8) & 0xFF;
    packet.topic_len_lsb = topic_len & 0xFF;
    
    // Copy topic and payload
    memcpy(packet.topic, topic, topic_len);
    memcpy(packet.topic + topic_len, payload, payload_len);
    
    // Send packet (fire and forget - no wait for response)
    int total_len = 4 + topic_len + payload_len;
    sendto(sock, &packet, total_len, 0,
           (struct sockaddr*)&broker_addr, sizeof(broker_addr));
    
    // Immediately close socket
    close(sock);
    
    return 0;
}

// Example usage for low-power sensor
int main() {
    const char* broker_ip = "192.168.1.100";
    int broker_port = 1883;
    
    // Simulate sensor reading
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"temp\":23.5,\"battery\":3.7}");
    
    // Send message and return to sleep immediately
    send_qos_minus_one_udp(broker_ip, broker_port, 
                          "sensors/temperature/room1", payload);
    
    printf("Message sent, entering low-power mode...\n");
    
    // In real embedded system: enter deep sleep here
    // sleep_mode_enter();
    
    return 0;
}
```

## Rust Implementation Example

```rust
use std::net::UdpSocket;
use std::io::Result;

/// Represents a minimal MQTT QoS -1 publisher
pub struct QoSMinusOnePublisher {
    broker_addr: String,
}

impl QoSMinusOnePublisher {
    /// Create a new QoS -1 publisher
    pub fn new(broker_ip: &str, broker_port: u16) -> Self {
        QoSMinusOnePublisher {
            broker_addr: format!("{}:{}", broker_ip, broker_port),
        }
    }
    
    /// Build minimal MQTT PUBLISH packet
    fn build_publish_packet(topic: &str, payload: &[u8]) -> Vec<u8> {
        let mut packet = Vec::new();
        
        // Fixed header: PUBLISH (0x30) with QoS 0
        packet.push(0x30);
        
        // Calculate remaining length
        let topic_len = topic.len();
        let payload_len = payload.len();
        let remaining_len = 2 + topic_len + payload_len;
        
        // Remaining length (simplified - assumes < 128 bytes)
        packet.push(remaining_len as u8);
        
        // Topic length (big-endian)
        packet.push((topic_len >> 8) as u8);
        packet.push((topic_len & 0xFF) as u8);
        
        // Topic
        packet.extend_from_slice(topic.as_bytes());
        
        // Payload
        packet.extend_from_slice(payload);
        
        packet
    }
    
    /// Publish a message with QoS -1 (fire-and-forget via UDP)
    pub fn publish(&self, topic: &str, payload: &[u8]) -> Result<()> {
        // Create UDP socket
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        
        // Set timeout to prevent blocking (though we don't wait for response)
        socket.set_write_timeout(Some(std::time::Duration::from_millis(100)))?;
        
        // Build and send packet
        let packet = Self::build_publish_packet(topic, payload);
        socket.send_to(&packet, &self.broker_addr)?;
        
        // Socket automatically closed when it goes out of scope
        // No waiting for acknowledgment - true fire-and-forget
        
        Ok(())
    }
}

// Example usage for embedded sensor application
fn main() -> Result<()> {
    let publisher = QoSMinusOnePublisher::new("192.168.1.100", 1883);
    
    // Simulate periodic sensor readings
    loop {
        // Read sensor (simulated)
        let temperature = 23.5;
        let battery_voltage = 3.7;
        
        // Format payload
        let payload = format!(
            r#"{{"temp":{:.1},"battery":{:.2}}}"#,
            temperature, battery_voltage
        );
        
        // Publish and immediately continue (no blocking)
        publisher.publish("sensors/temperature/room1", payload.as_bytes())?;
        
        println!("Published: {}", payload);
        
        // In real embedded system: enter deep sleep here
        // For demo: sleep for 60 seconds
        std::thread::sleep(std::time::Duration::from_secs(60));
    }
}

// Advanced: Custom error handling for production
#[derive(Debug)]
pub enum QoSMinusOneError {
    NetworkError(std::io::Error),
    PacketTooLarge,
}

impl From<std::io::Error> for QoSMinusOneError {
    fn from(err: std::io::Error) -> Self {
        QoSMinusOneError::NetworkError(err)
    }
}
```

## Summary

**QoS -1** is a specialized, non-standard MQTT extension that sacrifices all delivery guarantees for minimal power consumption and network overhead. By eliminating acknowledgments, connection state, and handshakes, it enables ultra-low-power IoT devices to transmit telemetry data while spending minimal time with radios active. 

The trade-off is simple: messages may be lost, but for applications like environmental sensors that transmit frequently, losing an occasional reading is acceptable. Typical implementations use UDP transport to avoid TCP connection overhead, and devices can wake from deep sleep, transmit in milliseconds, and return to sleep drawing only microamperes.

This approach is ideal for battery-powered sensors that must operate for years on a single battery, massive IoT deployments where connection overhead becomes prohibitive, or scenarios where data is inherently ephemeral and the next reading will arrive soon regardless. However, it should never be used for critical control messages, commands, or any data where loss is unacceptable.