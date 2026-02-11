# SOME/IP: Scalable service-Oriented MiddlewarE over IP

## Overview

SOME/IP (Scalable service-Oriented MiddlewarE over IP) is an automotive middleware solution designed for control messages in automotive Ethernet networks. Developed by BMW and standardized by AUTOSAR, it enables service-oriented communication between Electronic Control Units (ECUs) in modern vehicles.

## Core Concepts

### Service-Oriented Architecture in Automotive

SOME/IP implements a client-server architecture where services are discovered dynamically and communication happens through well-defined interfaces. This differs from traditional CAN-based signal-oriented communication, allowing for more flexible and scalable automotive software architectures.

### Key Components

**Service Discovery (SOME/IP-SD)**: A protocol extension that enables dynamic discovery of services on the network. Services announce their availability, and clients can find and subscribe to them without hardcoded configurations.

**Methods**: Request-response communication pattern where a client invokes a method on a service and receives a response. Methods can be called with or without expecting a return value (fire-and-forget).

**Events**: Publish-subscribe pattern where services notify subscribed clients about state changes or cyclic data updates. Events can be sent via multicast or unicast depending on configuration.

**Fields**: Combination of events and getters/setters that represent service attributes. Fields support notification mechanisms and can be accessed through getter/setter methods.

## Protocol Structure

### Message Format

The SOME/IP message consists of a header and payload:

```
+------------------+
|   Message ID     | 4 bytes (Service ID + Method ID)
+------------------+
|   Length         | 4 bytes
+------------------+
|   Request ID     | 4 bytes (Client ID + Session ID)
+------------------+
|   Protocol Ver   | 1 byte
+------------------+
|   Interface Ver  | 1 byte
+------------------+
|   Message Type   | 1 byte
+------------------+
|   Return Code    | 1 byte
+------------------+
|   Payload        | Variable length
+------------------+
```

**Message ID**: Combines Service ID (16 bits) and Method/Event ID (16 bits) to uniquely identify the remote procedure or event.

**Length**: Indicates the total length of the message including header (minus the first 8 bytes).

**Request ID**: Combines Client ID (16 bits) and Session ID (16 bits) to match requests with responses.

**Protocol Version**: Currently 0x01 for SOME/IP.

**Interface Version**: Major version of the service interface.

**Message Type**: Indicates the type of message (REQUEST, REQUEST_NO_RETURN, NOTIFICATION, RESPONSE, ERROR, etc.).

**Return Code**: Status code indicating success or specific error conditions.

### Message Types

- **REQUEST (0x00)**: A request expecting a response
- **REQUEST_NO_RETURN (0x01)**: Fire-and-forget request
- **NOTIFICATION (0x02)**: Event notification
- **RESPONSE (0x80)**: Response to a request
- **ERROR (0x81)**: Error response

### Return Codes

- **E_OK (0x00)**: Success
- **E_NOT_OK (0x01)**: Generic error
- **E_UNKNOWN_SERVICE (0x02)**: Service not found
- **E_UNKNOWN_METHOD (0x03)**: Method not found
- **E_NOT_READY (0x04)**: Service not ready
- **E_NOT_REACHABLE (0x05)**: Network unreachable
- **E_TIMEOUT (0x06)**: Operation timeout
- **E_WRONG_PROTOCOL_VERSION (0x07)**: Protocol version mismatch
- **E_WRONG_INTERFACE_VERSION (0x08)**: Interface version incompatible
- **E_MALFORMED_MESSAGE (0x09)**: Malformed message received
- **E_WRONG_MESSAGE_TYPE (0x0A)**: Unexpected message type

## Service Discovery

SOME/IP-SD operates over UDP (typically port 30490) and uses specific message formats to announce services and find them:

### SD Message Types

**FindService**: Clients broadcast to discover available services
**OfferService**: Services announce their availability
**Subscribe**: Clients request event notifications
**SubscribeAck**: Services acknowledge subscriptions
**StopSubscribe**: Clients cancel subscriptions

### Service Entry Format

Service entries contain information about offered or requested services, including Service ID, Instance ID, Major Version, Minor Version, and endpoint options (IP address, port, protocol).

## Serialization

SOME/IP uses a custom serialization format that is both compact and efficient for automotive use cases. Data types are serialized in network byte order (big-endian).

### Basic Types Serialization

- **uint8, int8**: 1 byte
- **uint16, int16**: 2 bytes (big-endian)
- **uint32, int32**: 4 bytes (big-endian)
- **uint64, int64**: 8 bytes (big-endian)
- **float32**: 4 bytes (IEEE 754)
- **float64**: 8 bytes (IEEE 754)
- **boolean**: 1 byte (0x00 or 0x01)

### Complex Types

**Strings**: Length-prefixed with uint32 followed by UTF-8 bytes (including null terminator)
**Arrays**: Length-prefixed with uint32 followed by serialized elements
**Structs**: Sequential serialization of all members with optional alignment padding

## Implementation Examples

### C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// SOME/IP Header Structure
typedef struct {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    uint8_t message_type;
    uint8_t return_code;
} __attribute__((packed)) someip_header_t;

// Message Types
#define SOMEIP_MSG_REQUEST          0x00
#define SOMEIP_MSG_REQUEST_NO_RETURN 0x01
#define SOMEIP_MSG_NOTIFICATION     0x02
#define SOMEIP_MSG_RESPONSE         0x80
#define SOMEIP_MSG_ERROR            0x81

// Return Codes
#define SOMEIP_E_OK                 0x00
#define SOMEIP_E_NOT_OK             0x01
#define SOMEIP_E_UNKNOWN_SERVICE    0x02
#define SOMEIP_E_TIMEOUT            0x06

// Service Configuration
#define SERVICE_ID                  0x1234
#define METHOD_GET_SPEED           0x0001
#define METHOD_SET_SPEED           0x0002
#define EVENT_SPEED_CHANGED        0x8001

// Client configuration
#define CLIENT_ID                   0x5678
#define SERVER_PORT                 30509
#define SERVER_IP                   "127.0.0.1"

// Serialize SOME/IP header
void serialize_someip_header(uint8_t* buffer, someip_header_t* header) {
    uint16_t* ptr16 = (uint16_t*)buffer;
    uint32_t* ptr32;
    
    ptr16[0] = htons(header->service_id);
    ptr16[1] = htons(header->method_id);
    
    ptr32 = (uint32_t*)(buffer + 4);
    *ptr32 = htonl(header->length);
    
    ptr16 = (uint16_t*)(buffer + 8);
    ptr16[0] = htons(header->client_id);
    ptr16[1] = htons(header->session_id);
    
    buffer[12] = header->protocol_version;
    buffer[13] = header->interface_version;
    buffer[14] = header->message_type;
    buffer[15] = header->return_code;
}

// Deserialize SOME/IP header
void deserialize_someip_header(uint8_t* buffer, someip_header_t* header) {
    uint16_t* ptr16 = (uint16_t*)buffer;
    uint32_t* ptr32;
    
    header->service_id = ntohs(ptr16[0]);
    header->method_id = ntohs(ptr16[1]);
    
    ptr32 = (uint32_t*)(buffer + 4);
    header->length = ntohl(*ptr32);
    
    ptr16 = (uint16_t*)(buffer + 8);
    header->client_id = ntohs(ptr16[0]);
    header->session_id = ntohs(ptr16[1]);
    
    header->protocol_version = buffer[12];
    header->interface_version = buffer[13];
    header->message_type = buffer[14];
    header->return_code = buffer[15];
}

// Serialize uint32 payload
void serialize_uint32(uint8_t* buffer, uint32_t value) {
    uint32_t* ptr = (uint32_t*)buffer;
    *ptr = htonl(value);
}

// Deserialize uint32 payload
uint32_t deserialize_uint32(uint8_t* buffer) {
    uint32_t* ptr = (uint32_t*)buffer;
    return ntohl(*ptr);
}

// Client: Send request to get speed
int send_get_speed_request(int sockfd, struct sockaddr_in* server_addr, 
                          uint16_t session_id) {
    uint8_t buffer[16]; // Header only, no payload
    someip_header_t header;
    
    // Prepare header
    header.service_id = SERVICE_ID;
    header.method_id = METHOD_GET_SPEED;
    header.length = 8; // Length of header after length field
    header.client_id = CLIENT_ID;
    header.session_id = session_id;
    header.protocol_version = 0x01;
    header.interface_version = 0x01;
    header.message_type = SOMEIP_MSG_REQUEST;
    header.return_code = SOMEIP_E_OK;
    
    serialize_someip_header(buffer, &header);
    
    int sent = sendto(sockfd, buffer, 16, 0, 
                     (struct sockaddr*)server_addr, sizeof(*server_addr));
    
    if (sent < 0) {
        perror("sendto failed");
        return -1;
    }
    
    printf("Sent GetSpeed request (Session ID: %u)\n", session_id);
    return 0;
}

// Client: Send request to set speed
int send_set_speed_request(int sockfd, struct sockaddr_in* server_addr, 
                          uint16_t session_id, uint32_t speed) {
    uint8_t buffer[20]; // Header + 4 bytes payload
    someip_header_t header;
    
    // Prepare header
    header.service_id = SERVICE_ID;
    header.method_id = METHOD_SET_SPEED;
    header.length = 12; // Header (8) + payload (4)
    header.client_id = CLIENT_ID;
    header.session_id = session_id;
    header.protocol_version = 0x01;
    header.interface_version = 0x01;
    header.message_type = SOMEIP_MSG_REQUEST;
    header.return_code = SOMEIP_E_OK;
    
    serialize_someip_header(buffer, &header);
    serialize_uint32(buffer + 16, speed);
    
    int sent = sendto(sockfd, buffer, 20, 0, 
                     (struct sockaddr*)server_addr, sizeof(*server_addr));
    
    if (sent < 0) {
        perror("sendto failed");
        return -1;
    }
    
    printf("Sent SetSpeed request: %u km/h (Session ID: %u)\n", 
           speed, session_id);
    return 0;
}

// Client: Receive and process response
int receive_response(int sockfd) {
    uint8_t buffer[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    int received = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                           (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 16) {
        fprintf(stderr, "Received malformed message\n");
        return -1;
    }
    
    someip_header_t header;
    deserialize_someip_header(buffer, &header);
    
    printf("Received response:\n");
    printf("  Service ID: 0x%04X\n", header.service_id);
    printf("  Method ID: 0x%04X\n", header.method_id);
    printf("  Session ID: %u\n", header.session_id);
    printf("  Message Type: 0x%02X\n", header.message_type);
    printf("  Return Code: 0x%02X\n", header.return_code);
    
    if (header.message_type == SOMEIP_MSG_RESPONSE && 
        header.return_code == SOMEIP_E_OK) {
        if (header.method_id == METHOD_GET_SPEED && received >= 20) {
            uint32_t speed = deserialize_uint32(buffer + 16);
            printf("  Current Speed: %u km/h\n", speed);
        } else if (header.method_id == METHOD_SET_SPEED) {
            printf("  Speed set successfully\n");
        }
    }
    
    return 0;
}

// Simple client example
int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    uint16_t session_id = 1;
    
    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Send GetSpeed request
    send_get_speed_request(sockfd, &server_addr, session_id++);
    receive_response(sockfd);
    
    sleep(1);
    
    // Send SetSpeed request
    send_set_speed_request(sockfd, &server_addr, session_id++, 120);
    receive_response(sockfd);
    
    sleep(1);
    
    // Send GetSpeed request again
    send_get_speed_request(sockfd, &server_addr, session_id++);
    receive_response(sockfd);
    
    close(sockfd);
    return 0;
}
```

### C++ Implementation

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <memory>
#include <chrono>
#include <thread>
#include <functional>
#include <map>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace someip {

// Message Types
enum class MessageType : uint8_t {
    REQUEST = 0x00,
    REQUEST_NO_RETURN = 0x01,
    NOTIFICATION = 0x02,
    RESPONSE = 0x80,
    ERROR = 0x81
};

// Return Codes
enum class ReturnCode : uint8_t {
    E_OK = 0x00,
    E_NOT_OK = 0x01,
    E_UNKNOWN_SERVICE = 0x02,
    E_UNKNOWN_METHOD = 0x03,
    E_NOT_READY = 0x04,
    E_TIMEOUT = 0x06,
    E_WRONG_PROTOCOL_VERSION = 0x07,
    E_MALFORMED_MESSAGE = 0x09
};

// SOME/IP Header
struct Header {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    MessageType message_type;
    ReturnCode return_code;
    
    static constexpr size_t HEADER_SIZE = 16;
    
    void serialize(std::vector<uint8_t>& buffer) const {
        buffer.resize(HEADER_SIZE);
        
        *reinterpret_cast<uint16_t*>(&buffer[0]) = htons(service_id);
        *reinterpret_cast<uint16_t*>(&buffer[2]) = htons(method_id);
        *reinterpret_cast<uint32_t*>(&buffer[4]) = htonl(length);
        *reinterpret_cast<uint16_t*>(&buffer[8]) = htons(client_id);
        *reinterpret_cast<uint16_t*>(&buffer[10]) = htons(session_id);
        buffer[12] = protocol_version;
        buffer[13] = interface_version;
        buffer[14] = static_cast<uint8_t>(message_type);
        buffer[15] = static_cast<uint8_t>(return_code);
    }
    
    static Header deserialize(const std::vector<uint8_t>& buffer) {
        if (buffer.size() < HEADER_SIZE) {
            throw std::runtime_error("Buffer too small for SOME/IP header");
        }
        
        Header header;
        header.service_id = ntohs(*reinterpret_cast<const uint16_t*>(&buffer[0]));
        header.method_id = ntohs(*reinterpret_cast<const uint16_t*>(&buffer[2]));
        header.length = ntohl(*reinterpret_cast<const uint32_t*>(&buffer[4]));
        header.client_id = ntohs(*reinterpret_cast<const uint16_t*>(&buffer[8]));
        header.session_id = ntohs(*reinterpret_cast<const uint16_t*>(&buffer[10]));
        header.protocol_version = buffer[12];
        header.interface_version = buffer[13];
        header.message_type = static_cast<MessageType>(buffer[14]);
        header.return_code = static_cast<ReturnCode>(buffer[15]);
        
        return header;
    }
};

// Serializer for basic types
class Serializer {
public:
    void write_uint8(uint8_t value) {
        buffer_.push_back(value);
    }
    
    void write_uint16(uint16_t value) {
        uint16_t net_value = htons(value);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&net_value);
        buffer_.insert(buffer_.end(), bytes, bytes + 2);
    }
    
    void write_uint32(uint32_t value) {
        uint32_t net_value = htonl(value);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&net_value);
        buffer_.insert(buffer_.end(), bytes, bytes + 4);
    }
    
    void write_string(const std::string& str) {
        write_uint32(str.length() + 1); // Include null terminator
        buffer_.insert(buffer_.end(), str.begin(), str.end());
        buffer_.push_back(0); // Null terminator
    }
    
    void write_bytes(const std::vector<uint8_t>& data) {
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }
    
    const std::vector<uint8_t>& get_buffer() const { return buffer_; }
    std::vector<uint8_t>& get_buffer() { return buffer_; }
    
private:
    std::vector<uint8_t> buffer_;
};

// Deserializer for basic types
class Deserializer {
public:
    Deserializer(const std::vector<uint8_t>& buffer) 
        : buffer_(buffer), position_(0) {}
    
    uint8_t read_uint8() {
        if (position_ + 1 > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
        return buffer_[position_++];
    }
    
    uint16_t read_uint16() {
        if (position_ + 2 > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
        uint16_t value = ntohs(*reinterpret_cast<const uint16_t*>(&buffer_[position_]));
        position_ += 2;
        return value;
    }
    
    uint32_t read_uint32() {
        if (position_ + 4 > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
        uint32_t value = ntohl(*reinterpret_cast<const uint32_t*>(&buffer_[position_]));
        position_ += 4;
        return value;
    }
    
    std::string read_string() {
        uint32_t length = read_uint32();
        if (position_ + length > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
        std::string str(reinterpret_cast<const char*>(&buffer_[position_]), length - 1);
        position_ += length;
        return str;
    }
    
    std::vector<uint8_t> read_bytes(size_t count) {
        if (position_ + count > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
        std::vector<uint8_t> data(buffer_.begin() + position_, 
                                 buffer_.begin() + position_ + count);
        position_ += count;
        return data;
    }
    
private:
    const std::vector<uint8_t>& buffer_;
    size_t position_;
};

// Message class combining header and payload
class Message {
public:
    Message(const Header& header, const std::vector<uint8_t>& payload = {})
        : header_(header), payload_(payload) {}
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        
        // Update length in header
        Header header = header_;
        header.length = Header::HEADER_SIZE - 8 + payload_.size();
        
        header.serialize(buffer);
        buffer.insert(buffer.end(), payload_.begin(), payload_.end());
        
        return buffer;
    }
    
    static Message deserialize(const std::vector<uint8_t>& buffer) {
        Header header = Header::deserialize(buffer);
        
        std::vector<uint8_t> payload;
        if (buffer.size() > Header::HEADER_SIZE) {
            payload.assign(buffer.begin() + Header::HEADER_SIZE, buffer.end());
        }
        
        return Message(header, payload);
    }
    
    const Header& get_header() const { return header_; }
    const std::vector<uint8_t>& get_payload() const { return payload_; }
    
private:
    Header header_;
    std::vector<uint8_t> payload_;
};

// Client implementation
class Client {
public:
    Client(uint16_t client_id) 
        : client_id_(client_id), session_id_(1), socket_fd_(-1) {}
    
    ~Client() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
        }
    }
    
    bool connect(const std::string& server_ip, uint16_t server_port) {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket\n";
            return false;
        }
        
        memset(&server_addr_, 0, sizeof(server_addr_));
        server_addr_.sin_family = AF_INET;
        server_addr_.sin_port = htons(server_port);
        server_addr_.sin_addr.s_addr = inet_addr(server_ip.c_str());
        
        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        return true;
    }
    
    bool send_request(uint16_t service_id, uint16_t method_id, 
                     const std::vector<uint8_t>& payload = {}) {
        Header header;
        header.service_id = service_id;
        header.method_id = method_id;
        header.client_id = client_id_;
        header.session_id = session_id_++;
        header.protocol_version = 0x01;
        header.interface_version = 0x01;
        header.message_type = MessageType::REQUEST;
        header.return_code = ReturnCode::E_OK;
        
        Message message(header, payload);
        auto buffer = message.serialize();
        
        int sent = sendto(socket_fd_, buffer.data(), buffer.size(), 0,
                         (struct sockaddr*)&server_addr_, sizeof(server_addr_));
        
        if (sent < 0) {
            std::cerr << "Failed to send message\n";
            return false;
        }
        
        std::cout << "Sent request: Service=0x" << std::hex << service_id 
                  << ", Method=0x" << method_id << std::dec << "\n";
        
        return true;
    }
    
    std::unique_ptr<Message> receive_response() {
        std::vector<uint8_t> buffer(1024);
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        int received = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                               (struct sockaddr*)&from_addr, &from_len);
        
        if (received < 0) {
            std::cerr << "Failed to receive response\n";
            return nullptr;
        }
        
        buffer.resize(received);
        
        try {
            auto message = std::make_unique<Message>(Message::deserialize(buffer));
            
            std::cout << "Received response: Session=" << message->get_header().session_id
                      << ", ReturnCode=" << static_cast<int>(message->get_header().return_code)
                      << "\n";
            
            return message;
        } catch (const std::exception& e) {
            std::cerr << "Failed to deserialize message: " << e.what() << "\n";
            return nullptr;
        }
    }
    
private:
    uint16_t client_id_;
    uint16_t session_id_;
    int socket_fd_;
    struct sockaddr_in server_addr_;
};

} // namespace someip

// Example usage: Vehicle Speed Service
constexpr uint16_t SERVICE_ID = 0x1234;
constexpr uint16_t METHOD_GET_SPEED = 0x0001;
constexpr uint16_t METHOD_SET_SPEED = 0x0002;
constexpr uint16_t CLIENT_ID = 0x5678;

int main() {
    using namespace someip;
    
    Client client(CLIENT_ID);
    
    if (!client.connect("127.0.0.1", 30509)) {
        std::cerr << "Failed to connect to server\n";
        return 1;
    }
    
    // Get current speed
    std::cout << "\n=== Getting current speed ===\n";
    client.send_request(SERVICE_ID, METHOD_GET_SPEED);
    
    if (auto response = client.receive_response()) {
        if (!response->get_payload().empty()) {
            Deserializer deserializer(response->get_payload());
            uint32_t speed = deserializer.read_uint32();
            std::cout << "Current speed: " << speed << " km/h\n";
        }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Set new speed
    std::cout << "\n=== Setting speed to 120 km/h ===\n";
    Serializer serializer;
    serializer.write_uint32(120);
    client.send_request(SERVICE_ID, METHOD_SET_SPEED, serializer.get_buffer());
    
    if (auto response = client.receive_response()) {
        std::cout << "Speed updated successfully\n";
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Get speed again
    std::cout << "\n=== Getting updated speed ===\n";
    client.send_request(SERVICE_ID, METHOD_GET_SPEED);
    
    if (auto response = client.receive_response()) {
        if (!response->get_payload().empty()) {
            Deserializer deserializer(response->get_payload());
            uint32_t speed = deserializer.read_uint32();
            std::cout << "Current speed: " << speed << " km/h\n";
        }
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::net::{UdpSocket, SocketAddr};
use std::io::{self, Error, ErrorKind};
use std::time::Duration;
use std::thread;

// Message Types
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
enum MessageType {
    Request = 0x00,
    RequestNoReturn = 0x01,
    Notification = 0x02,
    Response = 0x80,
    ErrorMsg = 0x81,
}

impl MessageType {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x00 => Some(MessageType::Request),
            0x01 => Some(MessageType::RequestNoReturn),
            0x02 => Some(MessageType::Notification),
            0x80 => Some(MessageType::Response),
            0x81 => Some(MessageType::ErrorMsg),
            _ => None,
        }
    }
}

// Return Codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
enum ReturnCode {
    Ok = 0x00,
    NotOk = 0x01,
    UnknownService = 0x02,
    UnknownMethod = 0x03,
    NotReady = 0x04,
    Timeout = 0x06,
    WrongProtocolVersion = 0x07,
    MalformedMessage = 0x09,
}

impl ReturnCode {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x00 => Some(ReturnCode::Ok),
            0x01 => Some(ReturnCode::NotOk),
            0x02 => Some(ReturnCode::UnknownService),
            0x03 => Some(ReturnCode::UnknownMethod),
            0x04 => Some(ReturnCode::NotReady),
            0x06 => Some(ReturnCode::Timeout),
            0x07 => Some(ReturnCode::WrongProtocolVersion),
            0x09 => Some(ReturnCode::MalformedMessage),
            _ => None,
        }
    }
}

// SOME/IP Header
#[derive(Debug, Clone)]
struct SomeIpHeader {
    service_id: u16,
    method_id: u16,
    length: u32,
    client_id: u16,
    session_id: u16,
    protocol_version: u8,
    interface_version: u8,
    message_type: MessageType,
    return_code: ReturnCode,
}

impl SomeIpHeader {
    const HEADER_SIZE: usize = 16;
    
    fn new(service_id: u16, method_id: u16, client_id: u16, session_id: u16) -> Self {
        SomeIpHeader {
            service_id,
            method_id,
            length: 8, // Will be updated based on payload
            client_id,
            session_id,
            protocol_version: 0x01,
            interface_version: 0x01,
            message_type: MessageType::Request,
            return_code: ReturnCode::Ok,
        }
    }
    
    fn serialize(&self) -> Vec<u8> {
        let mut buffer = Vec::with_capacity(Self::HEADER_SIZE);
        
        buffer.extend_from_slice(&self.service_id.to_be_bytes());
        buffer.extend_from_slice(&self.method_id.to_be_bytes());
        buffer.extend_from_slice(&self.length.to_be_bytes());
        buffer.extend_from_slice(&self.client_id.to_be_bytes());
        buffer.extend_from_slice(&self.session_id.to_be_bytes());
        buffer.push(self.protocol_version);
        buffer.push(self.interface_version);
        buffer.push(self.message_type as u8);
        buffer.push(self.return_code as u8);
        
        buffer
    }
    
    fn deserialize(buffer: &[u8]) -> io::Result<Self> {
        if buffer.len() < Self::HEADER_SIZE {
            return Err(Error::new(ErrorKind::InvalidData, "Buffer too small"));
        }
        
        let service_id = u16::from_be_bytes([buffer[0], buffer[1]]);
        let method_id = u16::from_be_bytes([buffer[2], buffer[3]]);
        let length = u32::from_be_bytes([buffer[4], buffer[5], buffer[6], buffer[7]]);
        let client_id = u16::from_be_bytes([buffer[8], buffer[9]]);
        let session_id = u16::from_be_bytes([buffer[10], buffer[11]]);
        let protocol_version = buffer[12];
        let interface_version = buffer[13];
        
        let message_type = MessageType::from_u8(buffer[14])
            .ok_or_else(|| Error::new(ErrorKind::InvalidData, "Invalid message type"))?;
        let return_code = ReturnCode::from_u8(buffer[15])
            .ok_or_else(|| Error::new(ErrorKind::InvalidData, "Invalid return code"))?;
        
        Ok(SomeIpHeader {
            service_id,
            method_id,
            length,
            client_id,
            session_id,
            protocol_version,
            interface_version,
            message_type,
            return_code,
        })
    }
}

// Message combining header and payload
struct SomeIpMessage {
    header: SomeIpHeader,
    payload: Vec<u8>,
}

impl SomeIpMessage {
    fn new(header: SomeIpHeader, payload: Vec<u8>) -> Self {
        let mut msg = SomeIpMessage { header, payload };
        msg.header.length = (SomeIpHeader::HEADER_SIZE - 8 + msg.payload.len()) as u32;
        msg
    }
    
    fn serialize(&self) -> Vec<u8> {
        let mut buffer = self.header.serialize();
        buffer.extend_from_slice(&self.payload);
        buffer
    }
    
    fn deserialize(buffer: &[u8]) -> io::Result<Self> {
        let header = SomeIpHeader::deserialize(buffer)?;
        
        let payload = if buffer.len() > SomeIpHeader::HEADER_SIZE {
            buffer[SomeIpHeader::HEADER_SIZE..].to_vec()
        } else {
            Vec::new()
        };
        
        Ok(SomeIpMessage { header, payload })
    }
}

// Serializer for payloads
struct Serializer {
    buffer: Vec<u8>,
}

impl Serializer {
    fn new() -> Self {
        Serializer { buffer: Vec::new() }
    }
    
    fn write_u8(&mut self, value: u8) {
        self.buffer.push(value);
    }
    
    fn write_u16(&mut self, value: u16) {
        self.buffer.extend_from_slice(&value.to_be_bytes());
    }
    
    fn write_u32(&mut self, value: u32) {
        self.buffer.extend_from_slice(&value.to_be_bytes());
    }
    
    fn write_string(&mut self, value: &str) {
        let bytes = value.as_bytes();
        self.write_u32((bytes.len() + 1) as u32); // +1 for null terminator
        self.buffer.extend_from_slice(bytes);
        self.buffer.push(0); // Null terminator
    }
    
    fn into_vec(self) -> Vec<u8> {
        self.buffer
    }
}

// Deserializer for payloads
struct Deserializer<'a> {
    buffer: &'a [u8],
    position: usize,
}

impl<'a> Deserializer<'a> {
    fn new(buffer: &'a [u8]) -> Self {
        Deserializer { buffer, position: 0 }
    }
    
    fn read_u8(&mut self) -> io::Result<u8> {
        if self.position + 1 > self.buffer.len() {
            return Err(Error::new(ErrorKind::UnexpectedEof, "Buffer underflow"));
        }
        let value = self.buffer[self.position];
        self.position += 1;
        Ok(value)
    }
    
    fn read_u16(&mut self) -> io::Result<u16> {
        if self.position + 2 > self.buffer.len() {
            return Err(Error::new(ErrorKind::UnexpectedEof, "Buffer underflow"));
        }
        let value = u16::from_be_bytes([
            self.buffer[self.position],
            self.buffer[self.position + 1],
        ]);
        self.position += 2;
        Ok(value)
    }
    
    fn read_u32(&mut self) -> io::Result<u32> {
        if self.position + 4 > self.buffer.len() {
            return Err(Error::new(ErrorKind::UnexpectedEof, "Buffer underflow"));
        }
        let value = u32::from_be_bytes([
            self.buffer[self.position],
            self.buffer[self.position + 1],
            self.buffer[self.position + 2],
            self.buffer[self.position + 3],
        ]);
        self.position += 4;
        Ok(value)
    }
    
    fn read_string(&mut self) -> io::Result<String> {
        let length = self.read_u32()? as usize;
        if self.position + length > self.buffer.len() {
            return Err(Error::new(ErrorKind::UnexpectedEof, "Buffer underflow"));
        }
        
        let string_bytes = &self.buffer[self.position..self.position + length - 1];
        self.position += length;
        
        String::from_utf8(string_bytes.to_vec())
            .map_err(|e| Error::new(ErrorKind::InvalidData, e))
    }
}

// Client implementation
struct SomeIpClient {
    client_id: u16,
    session_id: u16,
    socket: UdpSocket,
    server_addr: SocketAddr,
}

impl SomeIpClient {
    fn new(client_id: u16, server_addr: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        socket.set_read_timeout(Some(Duration::from_secs(2)))?;
        
        let server_addr: SocketAddr = server_addr.parse()
            .map_err(|e| Error::new(ErrorKind::InvalidInput, e))?;
        
        Ok(SomeIpClient {
            client_id,
            session_id: 1,
            socket,
            server_addr,
        })
    }
    
    fn send_request(&mut self, service_id: u16, method_id: u16, payload: Vec<u8>) 
        -> io::Result<()> {
        let header = SomeIpHeader::new(
            service_id,
            method_id,
            self.client_id,
            self.session_id,
        );
        
        self.session_id += 1;
        
        let message = SomeIpMessage::new(header, payload);
        let buffer = message.serialize();
        
        self.socket.send_to(&buffer, self.server_addr)?;
        
        println!("Sent request: Service=0x{:04X}, Method=0x{:04X}", 
                 service_id, method_id);
        
        Ok(())
    }
    
    fn receive_response(&self) -> io::Result<SomeIpMessage> {
        let mut buffer = vec![0u8; 1024];
        
        let (size, _) = self.socket.recv_from(&mut buffer)?;
        buffer.truncate(size);
        
        let message = SomeIpMessage::deserialize(&buffer)?;
        
        println!("Received response: Session={}, ReturnCode={:?}", 
                 message.header.session_id, message.header.return_code);
        
        Ok(message)
    }
}

// Example: Vehicle Speed Service
const SERVICE_ID: u16 = 0x1234;
const METHOD_GET_SPEED: u16 = 0x0001;
const METHOD_SET_SPEED: u16 = 0x0002;
const CLIENT_ID: u16 = 0x5678;

fn main() -> io::Result<()> {
    let mut client = SomeIpClient::new(CLIENT_ID, "127.0.0.1:30509")?;
    
    // Get current speed
    println!("\n=== Getting current speed ===");
    client.send_request(SERVICE_ID, METHOD_GET_SPEED, Vec::new())?;
    
    if let Ok(response) = client.receive_response() {
        if !response.payload.is_empty() {
            let mut deserializer = Deserializer::new(&response.payload);
            if let Ok(speed) = deserializer.read_u32() {
                println!("Current speed: {} km/h", speed);
            }
        }
    }
    
    thread::sleep(Duration::from_secs(1));
    
    // Set new speed
    println!("\n=== Setting speed to 120 km/h ===");
    let mut serializer = Serializer::new();
    serializer.write_u32(120);
    client.send_request(SERVICE_ID, METHOD_SET_SPEED, serializer.into_vec())?;
    
    if let Ok(_response) = client.receive_response() {
        println!("Speed updated successfully");
    }
    
    thread::sleep(Duration::from_secs(1));
    
    // Get speed again
    println!("\n=== Getting updated speed ===");
    client.send_request(SERVICE_ID, METHOD_GET_SPEED, Vec::new())?;
    
    if let Ok(response) = client.receive_response() {
        if !response.payload.is_empty() {
            let mut deserializer = Deserializer::new(&response.payload);
            if let Ok(speed) = deserializer.read_u32() {
                println!("Current speed: {} km/h", speed);
            }
        }
    }
    
    Ok(())
}
```

## Advanced Topics

### Event Groups and Multicast

SOME/IP supports grouping multiple events into event groups for efficient subscription management. Clients can subscribe to entire event groups rather than individual events. Events can be transmitted via multicast to reduce network load when multiple subscribers exist.

### Transformation Properties

SOME/IP supports different serialization formats through transformation properties in the service interface definition. This includes standard SOME/IP serialization, as well as more compact or optimized formats for specific use cases.

### Security

SOME/IP-SEC extends the protocol with authentication, encryption, and integrity protection. It uses TLS for secure communication channels and supports certificate-based authentication between ECUs.

### Quality of Service

The protocol supports various QoS parameters including message priorities, time-to-live values, and reliability levels. These allow fine-grained control over message delivery guarantees and network resource usage.

## Use Cases in Automotive

SOME/IP is widely deployed in modern vehicles for diverse applications including infotainment systems communicating with head units, ADAS systems sharing sensor data and control commands, over-the-air update services, vehicle diagnostics and remote maintenance, and inter-domain communication in zonal E/E architectures. The protocol's flexibility and standardization make it a cornerstone of modern automotive software architectures.