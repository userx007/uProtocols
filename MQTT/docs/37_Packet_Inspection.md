# MQTT Packet Inspection: Analyzing Traffic with Wireshark and tcpdump

## Overview

Packet inspection is a critical skill for debugging MQTT implementations, troubleshooting connectivity issues, monitoring security, and understanding protocol behavior. By capturing and analyzing MQTT traffic at the network level, developers can observe the actual bytes transmitted between clients and brokers, verify protocol compliance, identify performance bottlenecks, and detect security vulnerabilities.

## Why Packet Inspection Matters for MQTT

MQTT operates over TCP/IP, making it accessible to standard network analysis tools. Packet inspection helps you:

- **Debug connection issues**: See exactly what's happening during the CONNECT handshake
- **Verify QoS behavior**: Observe PUBACK, PUBREC, PUBREL, and PUBCOMP message flows
- **Monitor security**: Detect unencrypted credentials or insecure configurations
- **Analyze performance**: Identify network latency, packet loss, or excessive keepalive traffic
- **Validate implementations**: Ensure your MQTT client/broker follows protocol specifications
- **Troubleshoot interoperability**: Compare different client/broker behaviors

## MQTT Protocol Structure

Before analyzing packets, it's helpful to understand MQTT's wire format:

- **Fixed Header** (2-5 bytes): Message type, flags, remaining length
- **Variable Header** (optional): Packet-specific data like packet identifier, topic name
- **Payload** (optional): Actual message content

## Tools for Packet Inspection

### Wireshark
A powerful GUI-based packet analyzer with built-in MQTT protocol dissection. It can decode MQTT packets, display protocol fields, and filter traffic intelligently.

### tcpdump
A command-line packet capture tool perfect for headless systems, servers, and automated analysis. Captured files can be analyzed later with Wireshark.

## Practical Examples

### Example 1: Capturing MQTT Traffic with tcpdump

```c
// C program to publish MQTT messages for packet capture analysis
// Compile: gcc mqtt_test.c -o mqtt_test -lpaho-mqtt3c
// Run tcpdump in another terminal: sudo tcpdump -i any -w mqtt_capture.pcap port 1883

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "PacketInspectionClient"
#define TOPIC       "test/packet/inspection"
#define QOS         1
#define TIMEOUT     10000L

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "testuser";  // This will be visible in plaintext!
    conn_opts.password = "testpass";  // Security risk on unencrypted connections
    
    printf("Connecting to broker: %s\n", ADDRESS);
    printf("Start tcpdump now if not already running:\n");
    printf("  sudo tcpdump -i any -w mqtt_capture.pcap port 1883\n\n");
    sleep(2);
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected - observe CONNECT and CONNACK packets\n");
    sleep(1);
    
    // Publish multiple messages with different QoS levels
    for (int qos = 0; qos <= 2; qos++) {
        char payload[128];
        snprintf(payload, sizeof(payload), 
                 "Test message with QoS %d - observe packet flow!", qos);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = qos;
        pubmsg.retained = 0;
        
        printf("\nPublishing QoS %d message - observe PUBLISH", qos);
        if (qos == 1) printf(" and PUBACK");
        if (qos == 2) printf(", PUBREC, PUBREL, and PUBCOMP");
        printf(" packets\n");
        
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        
        if (qos > 0) {
            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            printf("Message delivery confirmed (token=%d)\n", token);
        }
        
        sleep(2);  // Allow time to observe packets
    }
    
    printf("\nDisconnecting - observe DISCONNECT packet\n");
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    printf("\nCapture complete. Analyze with:\n");
    printf("  wireshark mqtt_capture.pcap\n");
    printf("  or\n");
    printf("  tcpdump -r mqtt_capture.pcap -A\n");
    
    return rc;
}
```

### Example 2: MQTT Subscriber with Connection Diagnostics (C++)

```cpp
// C++ MQTT subscriber with detailed connection logging for packet analysis
// Compile: g++ -std=c++17 mqtt_diagnostic_sub.cpp -o mqtt_sub -lpaho-mqttpp3 -lpaho-mqtt3as

#include <iostream>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID("DiagnosticSubscriber");
const std::string TOPIC("test/packet/inspection");
const int QOS = 1;

class Callback : public virtual mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "\n[DISCONNECT EVENT] Connection lost";
        if (!cause.empty())
            std::cout << ": " << cause;
        std::cout << "\nCheck Wireshark for TCP FIN/RST packets\n";
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::system_clock::to_time_t(now);
        
        std::cout << "\n[MESSAGE RECEIVED] " << std::ctime(&timestamp);
        std::cout << "Topic: " << msg->get_topic() << "\n";
        std::cout << "QoS: " << msg->get_qos() << "\n";
        std::cout << "Payload (" << msg->get_payload().size() << " bytes): " 
                  << msg->get_payload_str() << "\n";
        std::cout << "Check Wireshark for PUBLISH packet details\n";
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {}
};

int main(int argc, char* argv[]) {
    std::cout << "=== MQTT Packet Inspection Subscriber ===\n";
    std::cout << "Start packet capture before running:\n";
    std::cout << "  sudo tcpdump -i lo -w mqtt_sub.pcap port 1883\n";
    std::cout << "  or use Wireshark on loopback interface\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(3));

    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    Callback cb;
    client.set_callback(cb);

    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);
    connOpts.set_user_name("subscriber");
    connOpts.set_password("subpass");
    
    std::cout << "[CONNECTING] Observe CONNECT packet with:\n";
    std::cout << "  - Client ID: " << CLIENT_ID << "\n";
    std::cout << "  - Keep Alive: 20 seconds\n";
    std::cout << "  - Clean Session: true\n";
    std::cout << "  - Username/Password (visible in plaintext!)\n\n";

    try {
        auto tok = client.connect(connOpts);
        tok->wait();
        std::cout << "[CONNECTED] Check for CONNACK with return code 0\n\n";

        std::cout << "[SUBSCRIBING] to topic: " << TOPIC << " (QoS " << QOS << ")\n";
        std::cout << "Observe SUBSCRIBE and SUBACK packets\n\n";
        
        client.subscribe(TOPIC, QOS)->wait();
        
        std::cout << "[SUBSCRIBED] Waiting for messages...\n";
        std::cout << "Publish test messages from another client\n";
        std::cout << "Press Enter to disconnect and examine capture\n\n";
        
        std::cin.get();
        
        std::cout << "\n[UNSUBSCRIBING] Observe UNSUBSCRIBE and UNSUBACK\n";
        client.unsubscribe(TOPIC)->wait();
        
        std::cout << "[DISCONNECTING] Observe DISCONNECT packet\n";
        client.disconnect()->wait();
        
        std::cout << "\nSession ended. Analyze packets with Wireshark filters:\n";
        std::cout << "  mqtt.msgtype == 1   (CONNECT)\n";
        std::cout << "  mqtt.msgtype == 2   (CONNACK)\n";
        std::cout << "  mqtt.msgtype == 3   (PUBLISH)\n";
        std::cout << "  mqtt.msgtype == 8   (SUBSCRIBE)\n";
        std::cout << "  mqtt.msgtype == 14  (DISCONNECT)\n";
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << "\n";
        return 1;
    }

    return 0;
}
```

### Example 3: MQTT Client in Rust with TLS Inspection

```rust
// Rust MQTT client demonstrating TLS encrypted traffic
// Add to Cargo.toml:
// [dependencies]
// paho-mqtt = "0.12"
// rustls = "0.21"

use paho_mqtt as mqtt;
use std::thread;
use std::time::Duration;

fn main() {
    println!("=== MQTT TLS Packet Inspection Demo ===\n");
    println!("For TLS traffic, capture shows encrypted data.");
    println!("Use SSLKEYLOGFILE to decrypt in Wireshark:\n");
    println!("  export SSLKEYLOGFILE=/tmp/sslkeys.log");
    println!("  (then run this program)\n");
    println!("In Wireshark: Edit → Preferences → Protocols → TLS");
    println!("  → (Pre)-Master-Secret log filename: /tmp/sslkeys.log\n");
    println!("Starting capture on port 8883...\n");
    
    thread::sleep(Duration::from_secs(3));

    // Create MQTT client for TLS connection
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri("ssl://broker.hivemq.com:8883")
        .client_id("RustPacketInspection")
        .finalize();

    let client = mqtt::Client::new(create_opts)
        .expect("Failed to create client");

    // Configure TLS/SSL options
    let ssl_opts = mqtt::SslOptionsBuilder::new()
        .trust_store("/etc/ssl/certs/ca-certificates.crt")
        .expect("Failed to set trust store")
        .finalize();

    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .ssl_options(ssl_opts)
        .keep_alive_interval(Duration::from_secs(30))
        .clean_session(true)
        .user_name("rustclient")
        .password("rustpass")
        .finalize();

    println!("[CONNECTING] Observe TLS handshake:");
    println!("  - Client Hello");
    println!("  - Server Hello, Certificate");
    println!("  - Key Exchange");
    println!("  - Encrypted MQTT CONNECT (if decryption configured)\n");

    match client.connect(conn_opts) {
        Ok(_) => {
            println!("[CONNECTED] TLS session established");
            println!("MQTT traffic is now encrypted\n");

            let topic = "rust/test/inspection";
            let qos = 1;
            
            println!("[PUBLISHING] Encrypted PUBLISH packets");
            
            for i in 0..3 {
                let payload = format!("Encrypted message #{} - invisible without TLS keys", i);
                let msg = mqtt::MessageBuilder::new()
                    .topic(topic)
                    .payload(payload)
                    .qos(qos)
                    .finalize();
                
                match client.publish(msg) {
                    Ok(_) => println!("  Published message {}", i),
                    Err(e) => eprintln!("  Publish error: {}", e),
                }
                
                thread::sleep(Duration::from_secs(1));
            }

            println!("\n[DISCONNECTING] Encrypted DISCONNECT packet");
            client.disconnect(None).expect("Disconnect failed");
            
            println!("\nCapture analysis tips:");
            println!("  - Without SSLKEYLOGFILE: see only encrypted TLS records");
            println!("  - With SSLKEYLOGFILE: Wireshark decrypts MQTT protocol");
            println!("  - Look for TLS Application Data packets");
            println!("  - Certificate details visible in Server Hello");
        }
        Err(e) => {
            eprintln!("Connection failed: {}", e);
        }
    }
}
```

### Example 4: tcpdump Command Reference for MQTT Analysis

```bash
#!/bin/bash
# Shell script with tcpdump examples for MQTT packet inspection

echo "=== MQTT Packet Capture Examples with tcpdump ==="
echo ""

echo "1. Basic capture on MQTT port (requires root/sudo):"
echo "   sudo tcpdump -i any -w mqtt.pcap port 1883"
echo ""

echo "2. Capture with ASCII output (see plaintext content):"
echo "   sudo tcpdump -i any -A port 1883"
echo ""

echo "3. Capture with hex and ASCII output:"
echo "   sudo tcpdump -i any -X port 1883"
echo ""

echo "4. Capture only specific host:"
echo "   sudo tcpdump -i any -w mqtt.pcap 'port 1883 and host broker.hivemq.com'"
echo ""

echo "5. Capture with packet details and no name resolution:"
echo "   sudo tcpdump -i any -nn -v port 1883"
echo ""

echo "6. Read and analyze existing capture:"
echo "   tcpdump -r mqtt.pcap -A"
echo ""

echo "7. Filter MQTT CONNECT packets (starts with 0x10):"
echo "   sudo tcpdump -i any -X 'port 1883 and tcp[20:1] = 0x10'"
echo ""

echo "8. Capture both MQTT and MQTTS:"
echo "   sudo tcpdump -i any -w mqtt_all.pcap 'port 1883 or port 8883'"
echo ""

echo "9. Limit capture size (100MB):"
echo "   sudo tcpdump -i any -w mqtt.pcap -C 100 port 1883"
echo ""

echo "10. Capture with rotating files (5 files max):"
echo "    sudo tcpdump -i any -w mqtt.pcap -C 50 -W 5 port 1883"
```

## Wireshark Filter Reference for MQTT

- `mqtt` - Display all MQTT traffic
- `mqtt.msgtype == 1` - CONNECT packets
- `mqtt.msgtype == 2` - CONNACK packets
- `mqtt.msgtype == 3` - PUBLISH packets
- `mqtt.msgtype == 4` - PUBACK packets
- `mqtt.msgtype == 8` - SUBSCRIBE packets
- `mqtt.msgtype == 9` - SUBACK packets
- `mqtt.msgtype == 12` - PINGREQ packets
- `mqtt.msgtype == 14` - DISCONNECT packets
- `mqtt.topic contains "sensor"` - Filter by topic substring
- `mqtt.qos == 2` - Only QoS 2 messages
- `mqtt.username` - Packets containing username

## Security Considerations

**Critical**: MQTT packets transmitted over unencrypted connections (port 1883) expose sensitive information:
- Usernames and passwords in plaintext
- Message payloads fully visible
- Topic names revealed
- Client identifiers exposed

**Always use TLS/SSL (port 8883)** in production environments. Even with TLS, packet metadata (packet sizes, timing) can leak information.

## Summary

Packet inspection is an essential technique for MQTT development and troubleshooting. Wireshark provides user-friendly protocol dissection with powerful filtering capabilities, while tcpdump excels at automated capture on headless systems. Together, they enable deep visibility into MQTT communication patterns, help diagnose connection issues, verify QoS flows, and identify security vulnerabilities. Whether debugging a failing CONNECT handshake, analyzing keepalive behavior, or ensuring proper TLS encryption, packet-level analysis provides ground truth about what's actually happening on the wire. For production systems, always use encrypted connections and be mindful that packet inspection on unencrypted MQTT exposes credentials and data.