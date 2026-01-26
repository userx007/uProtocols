# Custom MQTT Broker Development

## Overview

Custom MQTT broker development involves creating specialized, lightweight message brokers tailored to specific use cases rather than using general-purpose brokers like Mosquitto or HiveMQ. This approach is valuable when you need fine-grained control over message routing, protocol extensions, resource constraints, or integration with proprietary systems.

## Why Build a Custom Broker?

**Specialized Use Cases:**
- **IoT Edge Devices**: Ultra-lightweight brokers for resource-constrained environments
- **Protocol Bridging**: Connecting MQTT to proprietary or legacy protocols
- **Custom Authentication**: Integration with specialized security systems
- **Message Transformation**: Built-in data processing and filtering
- **Embedded Systems**: Brokers running on microcontrollers or embedded Linux
- **Testing & Simulation**: Controlled environments for testing MQTT clients

**Advantages:**
- Complete control over message routing logic
- Minimal resource footprint
- Custom protocol extensions
- Tight integration with existing systems
- Specialized quality-of-service implementations
- Proprietary security requirements

## Core Components

A basic MQTT broker consists of:

1. **Network Layer**: TCP/TLS socket handling
2. **Protocol Parser**: MQTT packet encoding/decoding
3. **Session Manager**: Client connection state tracking
4. **Subscription Manager**: Topic subscription and matching
5. **Message Router**: Publishing messages to subscribers
6. **Persistence Layer**: QoS message storage (optional)

## C/C++ Implementation

Here's a simplified custom MQTT broker implementation in C++:

```cpp
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <regex>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>

// MQTT packet types
enum MQTTPacketType {
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    SUBSCRIBE = 8,
    SUBACK = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14
};

// Client session representation
struct ClientSession {
    int socket;
    std::string clientId;
    bool connected;
    std::vector<std::string> subscriptions;
    
    ClientSession(int sock, const std::string& id) 
        : socket(sock), clientId(id), connected(true) {}
};

// Simple MQTT Broker
class SimpleMQTTBroker {
private:
    int serverSocket;
    std::map<std::string, std::shared_ptr<ClientSession>> clients;
    std::map<std::string, std::vector<std::string>> topicSubscribers;
    
    // Parse MQTT fixed header
    uint8_t parseFixedHeader(const uint8_t* buffer, size_t& remainingLength) {
        uint8_t packetType = (buffer[0] >> 4) & 0x0F;
        
        // Decode remaining length
        int multiplier = 1;
        remainingLength = 0;
        int pos = 1;
        uint8_t byte;
        
        do {
            byte = buffer[pos++];
            remainingLength += (byte & 127) * multiplier;
            multiplier *= 128;
        } while ((byte & 128) != 0);
        
        return packetType;
    }
    
    // Extract string from MQTT packet
    std::string extractString(const uint8_t* buffer, size_t& offset) {
        uint16_t length = (buffer[offset] << 8) | buffer[offset + 1];
        offset += 2;
        
        std::string result((char*)(buffer + offset), length);
        offset += length;
        
        return result;
    }
    
    // Handle CONNECT packet
    void handleConnect(int clientSocket, const uint8_t* buffer, size_t length) {
        size_t offset = 0;
        
        // Skip protocol name
        std::string protocolName = extractString(buffer, offset);
        
        // Protocol level
        uint8_t protocolLevel = buffer[offset++];
        
        // Connect flags
        uint8_t connectFlags = buffer[offset++];
        
        // Keep alive
        uint16_t keepAlive = (buffer[offset] << 8) | buffer[offset + 1];
        offset += 2;
        
        // Client ID
        std::string clientId = extractString(buffer, offset);
        
        // Create session
        auto session = std::make_shared<ClientSession>(clientSocket, clientId);
        clients[clientId] = session;
        
        std::cout << "Client connected: " << clientId << std::endl;
        
        // Send CONNACK
        uint8_t connack[] = {
            (CONNACK << 4),  // Packet type
            2,               // Remaining length
            0,               // Session present flag
            0                // Return code (0 = accepted)
        };
        
        send(clientSocket, connack, sizeof(connack), 0);
    }
    
    // Handle SUBSCRIBE packet
    void handleSubscribe(const std::string& clientId, const uint8_t* buffer, size_t length) {
        size_t offset = 0;
        
        // Packet identifier
        uint16_t packetId = (buffer[offset] << 8) | buffer[offset + 1];
        offset += 2;
        
        std::vector<uint8_t> returnCodes;
        
        // Parse subscriptions
        while (offset < length) {
            std::string topic = extractString(buffer, offset);
            uint8_t qos = buffer[offset++];
            
            // Add subscription
            if (clients.find(clientId) != clients.end()) {
                clients[clientId]->subscriptions.push_back(topic);
                topicSubscribers[topic].push_back(clientId);
                returnCodes.push_back(qos);  // Grant requested QoS
                
                std::cout << "Client " << clientId << " subscribed to: " << topic << std::endl;
            }
        }
        
        // Send SUBACK
        std::vector<uint8_t> suback;
        suback.push_back(SUBACK << 4);
        suback.push_back(2 + returnCodes.size());  // Remaining length
        suback.push_back((packetId >> 8) & 0xFF);
        suback.push_back(packetId & 0xFF);
        
        for (uint8_t code : returnCodes) {
            suback.push_back(code);
        }
        
        if (clients.find(clientId) != clients.end()) {
            send(clients[clientId]->socket, suback.data(), suback.size(), 0);
        }
    }
    
    // Topic matching with wildcards
    bool topicMatches(const std::string& filter, const std::string& topic) {
        if (filter == topic) return true;
        if (filter == "#") return true;
        
        // Simple wildcard matching
        std::regex pattern(filter);
        std::string regexFilter = filter;
        
        // Replace + with [^/]+
        size_t pos = 0;
        while ((pos = regexFilter.find("+", pos)) != std::string::npos) {
            regexFilter.replace(pos, 1, "[^/]+");
            pos += 6;
        }
        
        // Replace # with .*
        pos = 0;
        while ((pos = regexFilter.find("#", pos)) != std::string::npos) {
            regexFilter.replace(pos, 1, ".*");
            pos += 2;
        }
        
        return std::regex_match(topic, std::regex(regexFilter));
    }
    
    // Handle PUBLISH packet
    void handlePublish(const std::string& publisherId, const uint8_t* buffer, size_t length) {
        size_t offset = 0;
        
        // Extract topic
        std::string topic = extractString(buffer, offset);
        
        // Extract payload
        std::vector<uint8_t> payload(buffer + offset, buffer + length);
        
        std::cout << "Publishing to topic: " << topic << std::endl;
        
        // Route to subscribers
        for (auto& [clientId, session] : clients) {
            if (clientId == publisherId) continue;
            
            for (const auto& subscription : session->subscriptions) {
                if (topicMatches(subscription, topic)) {
                    // Build PUBLISH packet
                    std::vector<uint8_t> publishPacket;
                    publishPacket.push_back(PUBLISH << 4);
                    
                    // Calculate remaining length
                    size_t remainingLength = 2 + topic.length() + payload.size();
                    publishPacket.push_back(remainingLength);
                    
                    // Topic length and name
                    publishPacket.push_back((topic.length() >> 8) & 0xFF);
                    publishPacket.push_back(topic.length() & 0xFF);
                    publishPacket.insert(publishPacket.end(), topic.begin(), topic.end());
                    
                    // Payload
                    publishPacket.insert(publishPacket.end(), payload.begin(), payload.end());
                    
                    send(session->socket, publishPacket.data(), publishPacket.size(), 0);
                    break;
                }
            }
        }
    }
    
    // Handle client connection
    void handleClient(int clientSocket) {
        uint8_t buffer[2048];
        std::string currentClientId;
        
        while (true) {
            ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
            
            if (bytesRead <= 0) {
                // Client disconnected
                if (!currentClientId.empty()) {
                    clients.erase(currentClientId);
                    std::cout << "Client disconnected: " << currentClientId << std::endl;
                }
                close(clientSocket);
                break;
            }
            
            size_t remainingLength;
            uint8_t packetType = parseFixedHeader(buffer, remainingLength);
            
            switch (packetType) {
                case CONNECT:
                    handleConnect(clientSocket, buffer + 2, remainingLength);
                    // Store client ID for this connection
                    for (auto& [id, session] : clients) {
                        if (session->socket == clientSocket) {
                            currentClientId = id;
                            break;
                        }
                    }
                    break;
                    
                case SUBSCRIBE:
                    if (!currentClientId.empty()) {
                        handleSubscribe(currentClientId, buffer + 2, remainingLength);
                    }
                    break;
                    
                case PUBLISH:
                    if (!currentClientId.empty()) {
                        handlePublish(currentClientId, buffer + 2, remainingLength);
                    }
                    break;
                    
                case PINGREQ: {
                    uint8_t pingresp[] = {(PINGRESP << 4), 0};
                    send(clientSocket, pingresp, sizeof(pingresp), 0);
                    break;
                }
                    
                case DISCONNECT:
                    if (!currentClientId.empty()) {
                        clients.erase(currentClientId);
                    }
                    close(clientSocket);
                    return;
            }
        }
    }
    
public:
    SimpleMQTTBroker(int port = 1883) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);
        
        bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
        listen(serverSocket, 5);
        
        std::cout << "MQTT Broker listening on port " << port << std::endl;
    }
    
    void run() {
        while (true) {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            
            int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
            
            if (clientSocket >= 0) {
                std::thread(&SimpleMQTTBroker::handleClient, this, clientSocket).detach();
            }
        }
    }
    
    ~SimpleMQTTBroker() {
        close(serverSocket);
    }
};

// Main function
int main() {
    SimpleMQTTBroker broker(1883);
    broker.run();
    return 0;
}
```

## Rust Implementation

Here's a custom MQTT broker implementation in Rust using async/await:

```rust
use std::collections::HashMap;
use std::sync::Arc;
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::RwLock;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

// MQTT packet types
#[derive(Debug, Clone, Copy, PartialEq)]
enum PacketType {
    Connect = 1,
    Connack = 2,
    Publish = 3,
    Puback = 4,
    Subscribe = 8,
    Suback = 9,
    Pingreq = 12,
    Pingresp = 13,
    Disconnect = 14,
}

impl PacketType {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            1 => Some(PacketType::Connect),
            2 => Some(PacketType::Connack),
            3 => Some(PacketType::Publish),
            4 => Some(PacketType::Puback),
            8 => Some(PacketType::Subscribe),
            9 => Some(PacketType::Suback),
            12 => Some(PacketType::Pingreq),
            13 => Some(PacketType::Pingresp),
            14 => Some(PacketType::Disconnect),
            _ => None,
        }
    }
}

// Client session
#[derive(Clone)]
struct ClientSession {
    client_id: String,
    subscriptions: Vec<String>,
}

// Shared broker state
struct BrokerState {
    clients: HashMap<String, ClientSession>,
    connections: HashMap<String, Arc<tokio::sync::Mutex<TcpStream>>>,
}

type SharedState = Arc<RwLock<BrokerState>>;

// Simple MQTT Broker
struct MqttBroker {
    state: SharedState,
}

impl MqttBroker {
    fn new() -> Self {
        let state = Arc::new(RwLock::new(BrokerState {
            clients: HashMap::new(),
            connections: HashMap::new(),
        }));
        
        MqttBroker { state }
    }
    
    // Parse MQTT remaining length
    fn decode_remaining_length(buffer: &[u8], offset: &mut usize) -> usize {
        let mut multiplier = 1;
        let mut value = 0;
        
        loop {
            let byte = buffer[*offset];
            *offset += 1;
            
            value += ((byte & 127) as usize) * multiplier;
            multiplier *= 128;
            
            if (byte & 128) == 0 {
                break;
            }
        }
        
        value
    }
    
    // Extract UTF-8 string from MQTT packet
    fn extract_string(buffer: &[u8], offset: &mut usize) -> String {
        let length = ((buffer[*offset] as usize) << 8) | (buffer[*offset + 1] as usize);
        *offset += 2;
        
        let string = String::from_utf8_lossy(&buffer[*offset..*offset + length]).to_string();
        *offset += length;
        
        string
    }
    
    // Handle CONNECT packet
    async fn handle_connect(
        &self,
        stream: &mut TcpStream,
        buffer: &[u8],
    ) -> Result<String, Box<dyn std::error::Error>> {
        let mut offset = 0;
        
        // Protocol name
        let _protocol_name = Self::extract_string(buffer, &mut offset);
        
        // Protocol level
        let _protocol_level = buffer[offset];
        offset += 1;
        
        // Connect flags
        let _connect_flags = buffer[offset];
        offset += 1;
        
        // Keep alive
        let _keep_alive = ((buffer[offset] as u16) << 8) | (buffer[offset + 1] as u16);
        offset += 2;
        
        // Client ID
        let client_id = Self::extract_string(buffer, &mut offset);
        
        println!("Client connected: {}", client_id);
        
        // Send CONNACK
        let connack = vec![
            (PacketType::Connack as u8) << 4,
            2,  // Remaining length
            0,  // Session present
            0,  // Return code (accepted)
        ];
        
        stream.write_all(&connack).await?;
        
        Ok(client_id)
    }
    
    // Handle SUBSCRIBE packet
    async fn handle_subscribe(
        &self,
        client_id: &str,
        stream: &mut TcpStream,
        buffer: &[u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut offset = 0;
        
        // Packet identifier
        let packet_id = ((buffer[offset] as u16) << 8) | (buffer[offset + 1] as u16);
        offset += 2;
        
        let mut return_codes = Vec::new();
        let mut subscriptions = Vec::new();
        
        // Parse all subscriptions in packet
        while offset < buffer.len() {
            let topic = Self::extract_string(buffer, &mut offset);
            let qos = buffer[offset];
            offset += 1;
            
            subscriptions.push(topic.clone());
            return_codes.push(qos);
            
            println!("Client {} subscribed to: {}", client_id, topic);
        }
        
        // Update state
        let mut state = self.state.write().await;
        if let Some(session) = state.clients.get_mut(client_id) {
            session.subscriptions.extend(subscriptions);
        }
        
        // Send SUBACK
        let mut suback = vec![
            (PacketType::Suback as u8) << 4,
            (2 + return_codes.len()) as u8,
            (packet_id >> 8) as u8,
            packet_id as u8,
        ];
        suback.extend(return_codes);
        
        stream.write_all(&suback).await?;
        
        Ok(())
    }
    
    // Topic matching with wildcards
    fn topic_matches(filter: &str, topic: &str) -> bool {
        if filter == topic || filter == "#" {
            return true;
        }
        
        let filter_parts: Vec<&str> = filter.split('/').collect();
        let topic_parts: Vec<&str> = topic.split('/').collect();
        
        let mut i = 0;
        while i < filter_parts.len() && i < topic_parts.len() {
            if filter_parts[i] == "#" {
                return true;
            }
            if filter_parts[i] != "+" && filter_parts[i] != topic_parts[i] {
                return false;
            }
            i += 1;
        }
        
        i == filter_parts.len() && i == topic_parts.len()
    }
    
    // Handle PUBLISH packet
    async fn handle_publish(
        &self,
        publisher_id: &str,
        buffer: &[u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut offset = 0;
        
        // Extract topic
        let topic = Self::extract_string(buffer, &mut offset);
        
        // Extract payload
        let payload = &buffer[offset..];
        
        println!("Publishing to topic: {} ({} bytes)", topic, payload.len());
        
        // Find matching subscribers
        let state = self.state.read().await;
        let mut targets = Vec::new();
        
        for (client_id, session) in &state.clients {
            if client_id == publisher_id {
                continue;
            }
            
            for subscription in &session.subscriptions {
                if Self::topic_matches(subscription, &topic) {
                    if let Some(stream) = state.connections.get(client_id) {
                        targets.push((client_id.clone(), stream.clone()));
                    }
                    break;
                }
            }
        }
        
        drop(state);
        
        // Build PUBLISH packet
        let mut publish_packet = vec![(PacketType::Publish as u8) << 4];
        let remaining_length = 2 + topic.len() + payload.len();
        publish_packet.push(remaining_length as u8);
        publish_packet.push((topic.len() >> 8) as u8);
        publish_packet.push(topic.len() as u8);
        publish_packet.extend(topic.as_bytes());
        publish_packet.extend(payload);
        
        // Send to all matching subscribers
        for (_client_id, stream) in targets {
            let mut locked_stream = stream.lock().await;
            let _ = locked_stream.write_all(&publish_packet).await;
        }
        
        Ok(())
    }
    
    // Handle client connection
    async fn handle_client(
        self: Arc<Self>,
        mut stream: TcpStream,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut buffer = vec![0u8; 2048];
        let mut client_id = String::new();
        
        loop {
            let n = stream.read(&mut buffer).await?;
            
            if n == 0 {
                // Connection closed
                break;
            }
            
            let packet_type_byte = (buffer[0] >> 4) & 0x0F;
            let packet_type = PacketType::from_u8(packet_type_byte);
            
            let mut offset = 1;
            let remaining_length = Self::decode_remaining_length(&buffer, &mut offset);
            
            match packet_type {
                Some(PacketType::Connect) => {
                    client_id = self.handle_connect(&mut stream, &buffer[offset..offset + remaining_length]).await?;
                    
                    // Register client
                    let mut state = self.state.write().await;
                    state.clients.insert(
                        client_id.clone(),
                        ClientSession {
                            client_id: client_id.clone(),
                            subscriptions: Vec::new(),
                        },
                    );
                    state.connections.insert(
                        client_id.clone(),
                        Arc::new(tokio::sync::Mutex::new(stream.try_clone().await?)),
                    );
                }
                
                Some(PacketType::Subscribe) => {
                    self.handle_subscribe(&client_id, &mut stream, &buffer[offset..offset + remaining_length]).await?;
                }
                
                Some(PacketType::Publish) => {
                    self.handle_publish(&client_id, &buffer[offset..offset + remaining_length]).await?;
                }
                
                Some(PacketType::Pingreq) => {
                    let pingresp = vec![(PacketType::Pingresp as u8) << 4, 0];
                    stream.write_all(&pingresp).await?;
                }
                
                Some(PacketType::Disconnect) => {
                    break;
                }
                
                _ => {}
            }
        }
        
        // Cleanup
        if !client_id.is_empty() {
            let mut state = self.state.write().await;
            state.clients.remove(&client_id);
            state.connections.remove(&client_id);
            println!("Client disconnected: {}", client_id);
        }
        
        Ok(())
    }
    
    // Run the broker
    async fn run(&self, addr: &str) -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind(addr).await?;
        println!("MQTT Broker listening on {}", addr);
        
        loop {
            let (stream, _) = listener.accept().await?;
            let broker = Arc::new(self.clone());
            
            tokio::spawn(async move {
                if let Err(e) = broker.handle_client(stream).await {
                    eprintln!("Error handling client: {}", e);
                }
            });
        }
    }
}

impl Clone for MqttBroker {
    fn clone(&self) -> Self {
        MqttBroker {
            state: self.state.clone(),
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let broker = MqttBroker::new();
    broker.run("127.0.0.1:1883").await?;
    Ok(())
}
```

## Summary

**Custom MQTT broker development** enables creating specialized message brokers tailored to specific requirements. While full-featured brokers like Mosquitto and HiveMQ excel at general-purpose messaging, custom brokers shine in scenarios requiring minimal resource usage, specialized routing logic, proprietary integrations, or embedded deployments.

**Key considerations when building custom brokers:**
- **Protocol Compliance**: Implement MQTT specification correctly for interoperability
- **Performance**: Optimize for your specific use case (throughput vs. latency vs. memory)
- **QoS Levels**: Decide which Quality of Service levels to support
- **Persistence**: Determine if message storage is needed
- **Security**: Implement appropriate authentication and encryption
- **Scalability**: Plan for horizontal or vertical scaling if needed

Both the C++ and Rust examples demonstrate core broker functionality including connection handling, subscription management, and message routing with wildcard support. Production brokers would add persistence, advanced QoS handling, TLS support, authentication mechanisms, clustering capabilities, and comprehensive error handling. Custom brokers are particularly valuable for IoT edge computing, protocol gateways, testing frameworks, and embedded systems where standard brokers may be too heavyweight or inflexible.