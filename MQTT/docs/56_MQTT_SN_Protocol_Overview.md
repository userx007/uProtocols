# MQTT-SN Protocol Overview

## Detailed Description

**MQTT-SN (MQTT for Sensor Networks)** is a specialized variant of the MQTT protocol designed specifically for resource-constrained devices and networks, particularly wireless sensor networks (WSNs). While standard MQTT relies on TCP/IP connections, MQTT-SN is optimized for environments where TCP is impractical or unavailable, utilizing UDP, ZigBee, or other lightweight transport protocols.

### Key Characteristics

**Transport Independence**: Unlike MQTT's TCP requirement, MQTT-SN can operate over UDP, allowing it to work in networks where establishing reliable TCP connections would be too resource-intensive. This makes it ideal for battery-powered sensors and low-bandwidth networks.

**Reduced Protocol Overhead**: MQTT-SN uses shorter message headers (typically 2-4 bytes vs MQTT's variable header sizes) and employs topic ID mapping to replace verbose topic strings with 2-byte identifiers, significantly reducing bandwidth consumption.

**Gateway Architecture**: MQTT-SN networks typically use a gateway that translates between MQTT-SN (client side) and standard MQTT (broker side), allowing sensor devices to communicate with existing MQTT infrastructure without modification.

**Sleep Mode Support**: The protocol includes built-in support for sleeping clients, essential for battery-operated sensors. Devices can sleep for extended periods while the gateway buffers messages, with special mechanisms for waking and synchronizing.

**Message Types**: MQTT-SN introduces several new message types beyond standard MQTT, including SEARCHGW (gateway discovery), ADVERTISE (gateway announcement), REGISTER (topic registration), and WILLTOPICUPD (will topic updates).

**QoS Levels**: Like MQTT, MQTT-SN supports three Quality of Service levels (0, 1, 2), but with modifications to accommodate connectionless transport and sleeping clients. QoS -1 is also available for publish-only scenarios without connection establishment.

## C/C++ Code Examples

### Basic MQTT-SN Client Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// MQTT-SN Message Types
#define MQTTSN_ADVERTISE    0x00
#define MQTTSN_SEARCHGW     0x01
#define MQTTSN_GWINFO       0x02
#define MQTTSN_CONNECT      0x04
#define MQTTSN_CONNACK      0x05
#define MQTTSN_REGISTER     0x0A
#define MQTTSN_REGACK       0x0B
#define MQTTSN_PUBLISH      0x0C
#define MQTTSN_PUBACK       0x0D
#define MQTTSN_SUBSCRIBE    0x12
#define MQTTSN_SUBACK       0x13
#define MQTTSN_PINGREQ      0x16
#define MQTTSN_PINGRESP     0x17
#define MQTTSN_DISCONNECT   0x18

// MQTT-SN Return Codes
#define MQTTSN_RC_ACCEPTED  0x00
#define MQTTSN_RC_REJECTED_CONGESTION 0x01

// MQTT-SN Flags
#define MQTTSN_FLAG_DUP     0x80
#define MQTTSN_FLAG_QOS_0   0x00
#define MQTTSN_FLAG_QOS_1   0x20
#define MQTTSN_FLAG_QOS_2   0x40
#define MQTTSN_FLAG_RETAIN  0x10
#define MQTTSN_FLAG_TOPIC_TYPE_NORMAL   0x00
#define MQTTSN_FLAG_TOPIC_TYPE_PREDEFINED 0x01
#define MQTTSN_FLAG_TOPIC_TYPE_SHORT    0x02

typedef struct {
    int socket_fd;
    struct sockaddr_in gateway_addr;
    uint16_t msg_id;
    char client_id[24];
} mqttsn_client_t;

// Initialize MQTT-SN client
mqttsn_client_t* mqttsn_client_init(const char* gateway_ip, 
                                     int gateway_port, 
                                     const char* client_id) {
    mqttsn_client_t* client = malloc(sizeof(mqttsn_client_t));
    
    // Create UDP socket
    client->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client->socket_fd < 0) {
        perror("Socket creation failed");
        free(client);
        return NULL;
    }
    
    // Configure gateway address
    memset(&client->gateway_addr, 0, sizeof(client->gateway_addr));
    client->gateway_addr.sin_family = AF_INET;
    client->gateway_addr.sin_port = htons(gateway_port);
    client->gateway_addr.sin_addr.s_addr = inet_addr(gateway_ip);
    
    client->msg_id = 1;
    strncpy(client->client_id, client_id, sizeof(client->client_id) - 1);
    
    return client;
}

// Search for gateway
int mqttsn_search_gateway(mqttsn_client_t* client, uint8_t radius) {
    uint8_t buffer[3];
    buffer[0] = 3;                  // Length
    buffer[1] = MQTTSN_SEARCHGW;    // Message type
    buffer[2] = radius;              // Radius (broadcast hops)
    
    ssize_t sent = sendto(client->socket_fd, buffer, 3, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send SEARCHGW");
        return -1;
    }
    
    printf("Sent SEARCHGW message with radius %d\n", radius);
    return 0;
}

// Connect to gateway
int mqttsn_connect(mqttsn_client_t* client, uint16_t keep_alive, 
                   bool clean_session) {
    uint8_t buffer[256];
    int offset = 0;
    
    uint8_t flags = clean_session ? 0x04 : 0x00;
    int client_id_len = strlen(client->client_id);
    int length = 6 + client_id_len;
    
    buffer[offset++] = length;           // Length
    buffer[offset++] = MQTTSN_CONNECT;   // Message type
    buffer[offset++] = flags;             // Flags
    buffer[offset++] = 0x01;              // Protocol ID
    buffer[offset++] = (keep_alive >> 8) & 0xFF;  // Duration MSB
    buffer[offset++] = keep_alive & 0xFF;         // Duration LSB
    memcpy(&buffer[offset], client->client_id, client_id_len);
    offset += client_id_len;
    
    ssize_t sent = sendto(client->socket_fd, buffer, offset, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send CONNECT");
        return -1;
    }
    
    printf("Sent CONNECT message for client '%s'\n", client->client_id);
    
    // Wait for CONNACK
    uint8_t recv_buffer[256];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(client->socket_fd, recv_buffer, 
                                sizeof(recv_buffer), 0,
                                (struct sockaddr*)&from_addr, &from_len);
    
    if (received >= 3 && recv_buffer[1] == MQTTSN_CONNACK) {
        if (recv_buffer[2] == MQTTSN_RC_ACCEPTED) {
            printf("Connection accepted\n");
            return 0;
        } else {
            printf("Connection rejected with code %d\n", recv_buffer[2]);
            return -1;
        }
    }
    
    return -1;
}

// Register topic
int mqttsn_register(mqttsn_client_t* client, const char* topic_name,
                    uint16_t* topic_id) {
    uint8_t buffer[256];
    int offset = 0;
    int topic_len = strlen(topic_name);
    int length = 6 + topic_len;
    
    buffer[offset++] = length;           // Length
    buffer[offset++] = MQTTSN_REGISTER;  // Message type
    buffer[offset++] = 0x00;              // Topic ID MSB (0 for registration)
    buffer[offset++] = 0x00;              // Topic ID LSB
    buffer[offset++] = (client->msg_id >> 8) & 0xFF;  // Message ID MSB
    buffer[offset++] = client->msg_id & 0xFF;         // Message ID LSB
    memcpy(&buffer[offset], topic_name, topic_len);
    offset += topic_len;
    
    ssize_t sent = sendto(client->socket_fd, buffer, offset, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send REGISTER");
        return -1;
    }
    
    printf("Sent REGISTER message for topic '%s'\n", topic_name);
    
    // Wait for REGACK
    uint8_t recv_buffer[7];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(client->socket_fd, recv_buffer, 
                                sizeof(recv_buffer), 0,
                                (struct sockaddr*)&from_addr, &from_len);
    
    if (received >= 7 && recv_buffer[1] == MQTTSN_REGACK) {
        *topic_id = (recv_buffer[2] << 8) | recv_buffer[3];
        uint16_t recv_msg_id = (recv_buffer[4] << 8) | recv_buffer[5];
        uint8_t return_code = recv_buffer[6];
        
        if (recv_msg_id == client->msg_id && 
            return_code == MQTTSN_RC_ACCEPTED) {
            printf("Topic registered with ID: %u\n", *topic_id);
            client->msg_id++;
            return 0;
        }
    }
    
    return -1;
}

// Publish message
int mqttsn_publish(mqttsn_client_t* client, uint16_t topic_id,
                   const uint8_t* data, size_t data_len, 
                   uint8_t qos, bool retain) {
    uint8_t buffer[256];
    int offset = 0;
    int length = 7 + data_len;
    
    uint8_t flags = MQTTSN_FLAG_TOPIC_TYPE_NORMAL;
    if (qos == 1) flags |= MQTTSN_FLAG_QOS_1;
    else if (qos == 2) flags |= MQTTSN_FLAG_QOS_2;
    if (retain) flags |= MQTTSN_FLAG_RETAIN;
    
    buffer[offset++] = length;           // Length
    buffer[offset++] = MQTTSN_PUBLISH;   // Message type
    buffer[offset++] = flags;             // Flags
    buffer[offset++] = (topic_id >> 8) & 0xFF;  // Topic ID MSB
    buffer[offset++] = topic_id & 0xFF;         // Topic ID LSB
    buffer[offset++] = (client->msg_id >> 8) & 0xFF;  // Message ID MSB
    buffer[offset++] = client->msg_id & 0xFF;         // Message ID LSB
    memcpy(&buffer[offset], data, data_len);
    offset += data_len;
    
    ssize_t sent = sendto(client->socket_fd, buffer, offset, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send PUBLISH");
        return -1;
    }
    
    printf("Published message to topic ID %u\n", topic_id);
    
    if (qos > 0) {
        // Wait for PUBACK for QoS 1
        uint8_t recv_buffer[7];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(client->socket_fd, recv_buffer, 
                                    sizeof(recv_buffer), 0,
                                    (struct sockaddr*)&from_addr, &from_len);
        
        if (received >= 7 && recv_buffer[1] == MQTTSN_PUBACK) {
            printf("PUBACK received\n");
        }
    }
    
    client->msg_id++;
    return 0;
}

// Subscribe to topic
int mqttsn_subscribe(mqttsn_client_t* client, const char* topic_name,
                     uint8_t qos, uint16_t* topic_id) {
    uint8_t buffer[256];
    int offset = 0;
    int topic_len = strlen(topic_name);
    int length = 5 + topic_len;
    
    uint8_t flags = MQTTSN_FLAG_TOPIC_TYPE_NORMAL;
    if (qos == 1) flags |= MQTTSN_FLAG_QOS_1;
    else if (qos == 2) flags |= MQTTSN_FLAG_QOS_2;
    
    buffer[offset++] = length;            // Length
    buffer[offset++] = MQTTSN_SUBSCRIBE;  // Message type
    buffer[offset++] = flags;              // Flags
    buffer[offset++] = (client->msg_id >> 8) & 0xFF;  // Message ID MSB
    buffer[offset++] = client->msg_id & 0xFF;         // Message ID LSB
    memcpy(&buffer[offset], topic_name, topic_len);
    offset += topic_len;
    
    ssize_t sent = sendto(client->socket_fd, buffer, offset, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send SUBSCRIBE");
        return -1;
    }
    
    printf("Sent SUBSCRIBE for topic '%s'\n", topic_name);
    
    // Wait for SUBACK
    uint8_t recv_buffer[8];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(client->socket_fd, recv_buffer, 
                                sizeof(recv_buffer), 0,
                                (struct sockaddr*)&from_addr, &from_len);
    
    if (received >= 8 && recv_buffer[1] == MQTTSN_SUBACK) {
        *topic_id = (recv_buffer[3] << 8) | recv_buffer[4];
        printf("Subscribed with topic ID: %u\n", *topic_id);
        client->msg_id++;
        return 0;
    }
    
    return -1;
}

// Disconnect
int mqttsn_disconnect(mqttsn_client_t* client, uint16_t duration) {
    uint8_t buffer[4];
    int offset = 0;
    
    if (duration > 0) {
        buffer[offset++] = 4;  // Length
        buffer[offset++] = MQTTSN_DISCONNECT;
        buffer[offset++] = (duration >> 8) & 0xFF;
        buffer[offset++] = duration & 0xFF;
    } else {
        buffer[offset++] = 2;  // Length
        buffer[offset++] = MQTTSN_DISCONNECT;
    }
    
    ssize_t sent = sendto(client->socket_fd, buffer, offset, 0,
                          (struct sockaddr*)&client->gateway_addr,
                          sizeof(client->gateway_addr));
    
    if (sent < 0) {
        perror("Failed to send DISCONNECT");
        return -1;
    }
    
    printf("Sent DISCONNECT message\n");
    return 0;
}

// Cleanup
void mqttsn_client_destroy(mqttsn_client_t* client) {
    if (client) {
        close(client->socket_fd);
        free(client);
    }
}

// Example usage
int main() {
    mqttsn_client_t* client = mqttsn_client_init("192.168.1.100", 1884, 
                                                  "sensor_node_01");
    if (!client) {
        return 1;
    }
    
    // Connect to gateway
    if (mqttsn_connect(client, 60, true) == 0) {
        // Register and publish to a topic
        uint16_t topic_id;
        if (mqttsn_register(client, "sensors/temperature", &topic_id) == 0) {
            const char* data = "22.5";
            mqttsn_publish(client, topic_id, (uint8_t*)data, 
                          strlen(data), 1, false);
        }
        
        // Subscribe to a topic
        uint16_t sub_topic_id;
        mqttsn_subscribe(client, "commands/control", 1, &sub_topic_id);
        
        // Disconnect
        mqttsn_disconnect(client, 0);
    }
    
    mqttsn_client_destroy(client);
    return 0;
}
```

## Rust Code Examples

### MQTT-SN Client in Rust

```rust
use std::net::{UdpSocket, SocketAddr};
use std::time::Duration;
use std::io::{self, Error, ErrorKind};

// MQTT-SN Message Types
const MQTTSN_ADVERTISE: u8 = 0x00;
const MQTTSN_SEARCHGW: u8 = 0x01;
const MQTTSN_GWINFO: u8 = 0x02;
const MQTTSN_CONNECT: u8 = 0x04;
const MQTTSN_CONNACK: u8 = 0x05;
const MQTTSN_REGISTER: u8 = 0x0A;
const MQTTSN_REGACK: u8 = 0x0B;
const MQTTSN_PUBLISH: u8 = 0x0C;
const MQTTSN_PUBACK: u8 = 0x0D;
const MQTTSN_SUBSCRIBE: u8 = 0x12;
const MQTTSN_SUBACK: u8 = 0x13;
const MQTTSN_PINGREQ: u8 = 0x16;
const MQTTSN_PINGRESP: u8 = 0x17;
const MQTTSN_DISCONNECT: u8 = 0x18;

// Return Codes
const MQTTSN_RC_ACCEPTED: u8 = 0x00;

// Flags
const MQTTSN_FLAG_QOS_0: u8 = 0x00;
const MQTTSN_FLAG_QOS_1: u8 = 0x20;
const MQTTSN_FLAG_QOS_2: u8 = 0x40;
const MQTTSN_FLAG_RETAIN: u8 = 0x10;
const MQTTSN_FLAG_CLEAN_SESSION: u8 = 0x04;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum QoS {
    AtMostOnce = 0,
    AtLeastOnce = 1,
    ExactlyOnce = 2,
}

impl QoS {
    fn to_flag(&self) -> u8 {
        match self {
            QoS::AtMostOnce => MQTTSN_FLAG_QOS_0,
            QoS::AtLeastOnce => MQTTSN_FLAG_QOS_1,
            QoS::ExactlyOnce => MQTTSN_FLAG_QOS_2,
        }
    }
}

pub struct MqttSnClient {
    socket: UdpSocket,
    gateway_addr: SocketAddr,
    msg_id: u16,
    client_id: String,
}

impl MqttSnClient {
    /// Create a new MQTT-SN client
    pub fn new(gateway_addr: &str, client_id: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        socket.set_read_timeout(Some(Duration::from_secs(5)))?;
        
        let gateway_addr = gateway_addr.parse::<SocketAddr>()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;
        
        Ok(MqttSnClient {
            socket,
            gateway_addr,
            msg_id: 1,
            client_id: client_id.to_string(),
        })
    }
    
    /// Search for available gateways
    pub fn search_gateway(&self, radius: u8) -> io::Result<()> {
        let mut buffer = vec![3, MQTTSN_SEARCHGW, radius];
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Sent SEARCHGW message with radius {}", radius);
        
        Ok(())
    }
    
    /// Connect to the gateway
    pub fn connect(&self, keep_alive: u16, clean_session: bool) -> io::Result<()> {
        let mut buffer = Vec::new();
        
        let flags = if clean_session { MQTTSN_FLAG_CLEAN_SESSION } else { 0 };
        let client_id_bytes = self.client_id.as_bytes();
        let length = (6 + client_id_bytes.len()) as u8;
        
        buffer.push(length);
        buffer.push(MQTTSN_CONNECT);
        buffer.push(flags);
        buffer.push(0x01); // Protocol ID
        buffer.extend_from_slice(&keep_alive.to_be_bytes());
        buffer.extend_from_slice(client_id_bytes);
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Sent CONNECT message for client '{}'", self.client_id);
        
        // Wait for CONNACK
        let mut recv_buffer = [0u8; 256];
        let (amt, _src) = self.socket.recv_from(&mut recv_buffer)?;
        
        if amt >= 3 && recv_buffer[1] == MQTTSN_CONNACK {
            if recv_buffer[2] == MQTTSN_RC_ACCEPTED {
                println!("Connection accepted");
                return Ok(());
            } else {
                return Err(Error::new(
                    ErrorKind::ConnectionRefused,
                    format!("Connection rejected with code {}", recv_buffer[2])
                ));
            }
        }
        
        Err(Error::new(ErrorKind::InvalidData, "Invalid CONNACK response"))
    }
    
    /// Register a topic and get its topic ID
    pub fn register(&mut self, topic_name: &str) -> io::Result<u16> {
        let mut buffer = Vec::new();
        let topic_bytes = topic_name.as_bytes();
        let length = (6 + topic_bytes.len()) as u8;
        
        buffer.push(length);
        buffer.push(MQTTSN_REGISTER);
        buffer.extend_from_slice(&[0, 0]); // Topic ID (0 for registration)
        buffer.extend_from_slice(&self.msg_id.to_be_bytes());
        buffer.extend_from_slice(topic_bytes);
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Sent REGISTER message for topic '{}'", topic_name);
        
        // Wait for REGACK
        let mut recv_buffer = [0u8; 7];
        let (amt, _src) = self.socket.recv_from(&mut recv_buffer)?;
        
        if amt >= 7 && recv_buffer[1] == MQTTSN_REGACK {
            let topic_id = u16::from_be_bytes([recv_buffer[2], recv_buffer[3]]);
            let recv_msg_id = u16::from_be_bytes([recv_buffer[4], recv_buffer[5]]);
            let return_code = recv_buffer[6];
            
            if recv_msg_id == self.msg_id && return_code == MQTTSN_RC_ACCEPTED {
                println!("Topic registered with ID: {}", topic_id);
                self.msg_id += 1;
                return Ok(topic_id);
            }
        }
        
        Err(Error::new(ErrorKind::InvalidData, "Failed to register topic"))
    }
    
    /// Publish a message to a topic
    pub fn publish(
        &mut self,
        topic_id: u16,
        data: &[u8],
        qos: QoS,
        retain: bool,
    ) -> io::Result<()> {
        let mut buffer = Vec::new();
        let length = (7 + data.len()) as u8;
        
        let mut flags = qos.to_flag();
        if retain {
            flags |= MQTTSN_FLAG_RETAIN;
        }
        
        buffer.push(length);
        buffer.push(MQTTSN_PUBLISH);
        buffer.push(flags);
        buffer.extend_from_slice(&topic_id.to_be_bytes());
        buffer.extend_from_slice(&self.msg_id.to_be_bytes());
        buffer.extend_from_slice(data);
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Published message to topic ID {}", topic_id);
        
        // Wait for PUBACK if QoS > 0
        if qos != QoS::AtMostOnce {
            let mut recv_buffer = [0u8; 7];
            match self.socket.recv_from(&mut recv_buffer) {
                Ok((amt, _src)) if amt >= 7 && recv_buffer[1] == MQTTSN_PUBACK => {
                    println!("PUBACK received");
                }
                _ => {}
            }
        }
        
        self.msg_id += 1;
        Ok(())
    }
    
    /// Subscribe to a topic
    pub fn subscribe(&mut self, topic_name: &str, qos: QoS) -> io::Result<u16> {
        let mut buffer = Vec::new();
        let topic_bytes = topic_name.as_bytes();
        let length = (5 + topic_bytes.len()) as u8;
        
        let flags = qos.to_flag();
        
        buffer.push(length);
        buffer.push(MQTTSN_SUBSCRIBE);
        buffer.push(flags);
        buffer.extend_from_slice(&self.msg_id.to_be_bytes());
        buffer.extend_from_slice(topic_bytes);
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Sent SUBSCRIBE for topic '{}'", topic_name);
        
        // Wait for SUBACK
        let mut recv_buffer = [0u8; 8];
        let (amt, _src) = self.socket.recv_from(&mut recv_buffer)?;
        
        if amt >= 8 && recv_buffer[1] == MQTTSN_SUBACK {
            let topic_id = u16::from_be_bytes([recv_buffer[3], recv_buffer[4]]);
            println!("Subscribed with topic ID: {}", topic_id);
            self.msg_id += 1;
            return Ok(topic_id);
        }
        
        Err(Error::new(ErrorKind::InvalidData, "Failed to subscribe"))
    }
    
    /// Receive messages
    pub fn receive(&self) -> io::Result<(u16, Vec<u8>)> {
        let mut buffer = [0u8; 256];
        let (amt, _src) = self.socket.recv_from(&mut buffer)?;
        
        if amt >= 7 && buffer[1] == MQTTSN_PUBLISH {
            let topic_id = u16::from_be_bytes([buffer[3], buffer[4]]);
            let data = buffer[7..amt].to_vec();
            
            println!("Received message on topic ID {}", topic_id);
            return Ok((topic_id, data));
        }
        
        Err(Error::new(ErrorKind::InvalidData, "Not a PUBLISH message"))
    }
    
    /// Send keep-alive ping
    pub fn ping(&self) -> io::Result<()> {
        let buffer = vec![2, MQTTSN_PINGREQ];
        self.socket.send_to(&buffer, self.gateway_addr)?;
        
        // Wait for PINGRESP
        let mut recv_buffer = [0u8; 2];
        let (amt, _src) = self.socket.recv_from(&mut recv_buffer)?;
        
        if amt >= 2 && recv_buffer[1] == MQTTSN_PINGRESP {
            println!("PINGRESP received");
            return Ok(());
        }
        
        Err(Error::new(ErrorKind::TimedOut, "No PINGRESP received"))
    }
    
    /// Disconnect from the gateway
    pub fn disconnect(&self, duration: Option<u16>) -> io::Result<()> {
        let buffer = if let Some(dur) = duration {
            vec![4, MQTTSN_DISCONNECT, (dur >> 8) as u8, dur as u8]
        } else {
            vec![2, MQTTSN_DISCONNECT]
        };
        
        self.socket.send_to(&buffer, self.gateway_addr)?;
        println!("Sent DISCONNECT message");
        
        Ok(())
    }
}

// Example usage
fn main() -> io::Result<()> {
    let mut client = MqttSnClient::new("192.168.1.100:1884", "rust_sensor_01")?;
    
    // Connect to gateway
    client.connect(60, true)?;
    
    // Register and publish to a topic
    let topic_id = client.register("sensors/temperature")?;
    client.publish(topic_id, b"23.5", QoS::AtLeastOnce, false)?;
    
    // Subscribe to a topic
    let sub_topic_id = client.subscribe("commands/control", QoS::AtLeastOnce)?;
    
    // Listen for messages (with timeout)
    match client.receive() {
        Ok((tid, data)) => {
            println!("Received on topic {}: {:?}", tid, 
                     String::from_utf8_lossy(&data));
        }
        Err(e) => println!("No message received: {}", e),
    }
    
    // Send ping
    client.ping()?;
    
    // Disconnect
    client.disconnect(None)?;
    
    Ok(())