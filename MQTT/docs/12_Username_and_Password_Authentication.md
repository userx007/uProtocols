# MQTT Username and Password Authentication

## Overview

Username and password authentication is the most fundamental security mechanism in MQTT. It provides a basic layer of access control by requiring clients to present valid credentials when connecting to an MQTT broker. While not as robust as certificate-based authentication, it's widely supported, easy to implement, and suitable for many use cases, especially when combined with TLS/SSL encryption.

## How It Works

When an MQTT client initiates a connection to a broker, it can include a username and password in the CONNECT packet. The broker validates these credentials against its configured user database or authentication backend. If the credentials are valid, the connection is established; otherwise, the broker sends a CONNACK packet with a return code indicating authentication failure.

**Key Points:**
- Credentials are sent during the connection phase, not per-message
- Without TLS/SSL, credentials are transmitted in plaintext (security risk)
- The MQTT protocol supports usernames up to 65,535 bytes and passwords of similar length
- Brokers may implement additional features like role-based access control (ACL) based on username

## Security Considerations

Username/password authentication should always be used with TLS/SSL encryption to prevent credential interception. Even with encryption, consider implementing:
- Strong password policies
- Regular credential rotation
- Account lockout mechanisms after failed attempts
- Logging and monitoring of authentication events
- Integration with enterprise authentication systems (LDAP, OAuth, etc.)

## Code Examples

### C/C++ Implementation with Eclipse Paho

Here's a comprehensive example using the Paho MQTT C library:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

#define ADDRESS     "tcp://broker.example.com:1883"
#define CLIENTID    "SecureClient_001"
#define USERNAME    "mqtt_user"
#define PASSWORD    "secure_password_123"
#define TOPIC       "home/sensors/temperature"
#define QOS         1
#define TIMEOUT     10000L

// Callback for connection lost
void connlost(void *context, char *cause) {
    printf("\nConnection lost\n");
    printf("     cause: %s\n", cause);
}

// Callback for message arrived
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: %.*s\n", message->payloadlen, (char*)message->payload);
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Callback for successful delivery
void delivered(void *context, MQTTClient_deliveryToken dt) {
    printf("Message delivery confirmed\n");
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create MQTT client instance
    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
                                 MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to create client, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Set callbacks
    if ((rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to set callbacks, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Configure connection options with authentication
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = USERNAME;
    conn_opts.password = PASSWORD;
    
    // Optional: Add TLS/SSL options for secure transmission
    // MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    // ssl_opts.enableServerCertAuth = 1;
    // conn_opts.ssl = &ssl_opts;

    printf("Connecting to broker: %s\n", ADDRESS);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        
        // Handle specific authentication errors
        if (rc == 4) {
            printf("Connection refused: Bad username or password\n");
        } else if (rc == 5) {
            printf("Connection refused: Not authorized\n");
        }
        
        exit(EXIT_FAILURE);
    }
    
    printf("Successfully connected with username: %s\n", USERNAME);

    // Subscribe to a topic
    printf("Subscribing to topic: %s\n", TOPIC);
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Publish a message
    char payload[] = "22.5";
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    if ((rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    
    printf("Waiting for publication of %s on topic %s\n", payload, TOPIC);
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message published successfully\n");

    // Keep connection alive for receiving messages
    printf("Waiting for messages (press Ctrl+C to exit)...\n");
    
    // In a real application, you'd have a proper event loop here
    // For demonstration, we'll just wait for a bit
    #ifdef _WIN32
        Sleep(30000);  // 30 seconds
    #else
        sleep(30);
    #endif

    // Disconnect and cleanup
    if ((rc = MQTTClient_disconnect(client, 10000)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to disconnect, return code %d\n", rc);
    }
    
    MQTTClient_destroy(&client);
    printf("Disconnected successfully\n");
    
    return rc;
}
```

### Advanced C++ Example with Credential Management

```cpp
#include <iostream>
#include <string>
#include <memory>
#include <fstream>
#include <mqtt/client.h>

class MQTTCredentials {
private:
    std::string username;
    std::string password;
    
public:
    MQTTCredentials(const std::string& user, const std::string& pass)
        : username(user), password(pass) {}
    
    // Load credentials from file (basic implementation)
    static MQTTCredentials fromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        std::string user, pass;
        
        if (file.is_open()) {
            std::getline(file, user);
            std::getline(file, pass);
            file.close();
        } else {
            throw std::runtime_error("Unable to open credentials file");
        }
        
        return MQTTCredentials(user, pass);
    }
    
    const std::string& getUsername() const { return username; }
    const std::string& getPassword() const { return password; }
};

class SecureMQTTClient {
private:
    std::unique_ptr<mqtt::client> client;
    MQTTCredentials credentials;
    
public:
    SecureMQTTClient(const std::string& serverAddress,
                     const std::string& clientId,
                     const MQTTCredentials& creds)
        : credentials(creds) {
        
        client = std::make_unique<mqtt::client>(serverAddress, clientId);
    }
    
    bool connect() {
        try {
            mqtt::connect_options connOpts;
            connOpts.set_keep_alive_interval(20);
            connOpts.set_clean_session(true);
            connOpts.set_user_name(credentials.getUsername());
            connOpts.set_password(credentials.getPassword());
            
            // Optional: Add SSL/TLS
            // mqtt::ssl_options sslOpts;
            // sslOpts.set_trust_store("ca.crt");
            // connOpts.set_ssl(sslOpts);
            
            std::cout << "Connecting to broker..." << std::endl;
            mqtt::token_ptr conntok = client->connect(connOpts);
            conntok->wait();
            
            std::cout << "Successfully authenticated as: " 
                      << credentials.getUsername() << std::endl;
            return true;
            
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
            return false;
        }
    }
    
    void publish(const std::string& topic, const std::string& payload, int qos = 1) {
        try {
            mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
            pubmsg->set_qos(qos);
            client->publish(pubmsg)->wait();
            std::cout << "Message published to " << topic << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Publish error: " << exc.what() << std::endl;
        }
    }
    
    void subscribe(const std::string& topic, int qos = 1) {
        try {
            client->subscribe(topic, qos)->wait();
            std::cout << "Subscribed to " << topic << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Subscribe error: " << exc.what() << std::endl;
        }
    }
    
    void disconnect() {
        try {
            client->disconnect()->wait();
            std::cout << "Disconnected" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Disconnect error: " << exc.what() << std::endl;
        }
    }
};

int main() {
    const std::string SERVER_ADDRESS("tcp://broker.example.com:1883");
    const std::string CLIENT_ID("CppSecureClient");
    
    try {
        // Option 1: Hardcoded credentials (not recommended for production)
        MQTTCredentials creds("mqtt_user", "secure_password_123");
        
        // Option 2: Load from file
        // MQTTCredentials creds = MQTTCredentials::fromFile("credentials.txt");
        
        SecureMQTTClient mqttClient(SERVER_ADDRESS, CLIENT_ID, creds);
        
        if (mqttClient.connect()) {
            mqttClient.subscribe("home/sensors/#");
            mqttClient.publish("home/sensors/temperature", "23.5");
            
            // Keep alive for message reception
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            mqttClient.disconnect();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation with rumqttc

Here's a comprehensive Rust example using the `rumqttc` crate:

```rust
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use std::time::Duration;
use std::error::Error;

// Struct to manage MQTT credentials
#[derive(Debug, Clone)]
struct MqttCredentials {
    username: String,
    password: String,
}

impl MqttCredentials {
    fn new(username: String, password: String) -> Self {
        Self { username, password }
    }
    
    // Load credentials from environment variables
    fn from_env() -> Result<Self, Box<dyn Error>> {
        let username = std::env::var("MQTT_USERNAME")
            .map_err(|_| "MQTT_USERNAME not set")?;
        let password = std::env::var("MQTT_PASSWORD")
            .map_err(|_| "MQTT_PASSWORD not set")?;
        
        Ok(Self::new(username, password))
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    // Configuration
    let broker_address = "broker.example.com";
    let broker_port = 1883;
    let client_id = "rust_secure_client";
    
    // Create credentials
    let credentials = MqttCredentials::new(
        "mqtt_user".to_string(),
        "secure_password_123".to_string(),
    );
    
    // Alternative: Load from environment
    // let credentials = MqttCredentials::from_env()?;
    
    // Configure MQTT options
    let mut mqtt_options = MqttOptions::new(client_id, broker_address, broker_port);
    mqtt_options.set_keep_alive(Duration::from_secs(20));
    mqtt_options.set_clean_session(true);
    
    // Set authentication credentials
    mqtt_options.set_credentials(
        credentials.username.clone(),
        credentials.password.clone(),
    );
    
    // Optional: Configure TLS for secure transmission
    // use rumqttc::TlsConfiguration;
    // use rumqttc::Transport;
    // let ca = include_bytes!("ca.crt");
    // let tls_config = TlsConfiguration::Simple {
    //     ca: ca.to_vec(),
    //     alpn: None,
    //     client_auth: None,
    // };
    // mqtt_options.set_transport(Transport::Tls(tls_config));
    
    println!("Connecting to broker at {}:{}", broker_address, broker_port);
    println!("Username: {}", credentials.username);
    
    // Create client and connection
    let (mut client, mut connection) = Client::new(mqtt_options, 10);
    
    // Spawn a thread to handle the connection event loop
    std::thread::spawn(move || {
        for (i, notification) in connection.iter().enumerate() {
            match notification {
                Ok(Event::Incoming(Packet::ConnAck(connack))) => {
                    if connack.code == rumqttc::ConnectReturnCode::Success {
                        println!("✓ Successfully authenticated and connected!");
                    } else {
                        eprintln!("✗ Connection failed: {:?}", connack.code);
                    }
                }
                Ok(Event::Incoming(Packet::Publish(publish))) => {
                    println!("Received message:");
                    println!("  Topic: {}", publish.topic);
                    println!("  Payload: {:?}", 
                             String::from_utf8_lossy(&publish.payload));
                    println!("  QoS: {:?}", publish.qos);
                }
                Ok(Event::Incoming(packet)) => {
                    println!("Incoming packet: {:?}", packet);
                }
                Ok(Event::Outgoing(packet)) => {
                    println!("Outgoing packet: {:?}", packet);
                }
                Err(e) => {
                    eprintln!("Connection error: {:?}", e);
                    break;
                }
            }
            
            // For demonstration, stop after some events
            if i > 50 {
                break;
            }
        }
    });
    
    // Give connection time to establish
    std::thread::sleep(Duration::from_secs(2));
    
    // Subscribe to topics
    let topics = vec![
        ("home/sensors/temperature", QoS::AtLeastOnce),
        ("home/sensors/humidity", QoS::AtLeastOnce),
    ];
    
    for (topic, qos) in topics {
        client.subscribe(topic, qos)?;
        println!("Subscribed to: {}", topic);
    }
    
    // Publish messages
    let messages = vec![
        ("home/sensors/temperature", "22.5"),
        ("home/sensors/humidity", "65.0"),
    ];
    
    for (topic, payload) in messages {
        client.publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())?;
        println!("Published to {}: {}", topic, payload);
        std::thread::sleep(Duration::from_millis(500));
    }
    
    // Keep the application running to receive messages
    println!("\nListening for messages (will stop after a moment)...");
    std::thread::sleep(Duration::from_secs(10));
    
    // Disconnect gracefully
    client.disconnect()?;
    println!("Disconnected successfully");
    
    Ok(())
}
```

### Advanced Rust Example with Error Handling and Reconnection

```rust
use rumqttc::{Client, Connection, MqttOptions, QoS, Event, Packet, ConnectReturnCode};
use std::time::Duration;
use std::error::Error;
use std::sync::{Arc, Mutex};
use std::thread;

struct SecureMqttClient {
    client: Client,
    connection: Arc<Mutex<Connection>>,
    credentials: MqttCredentials,
}

impl SecureMqttClient {
    fn new(
        broker: &str,
        port: u16,
        client_id: &str,
        credentials: MqttCredentials,
    ) -> Result<Self, Box<dyn Error>> {
        let mut mqtt_options = MqttOptions::new(client_id, broker, port);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        mqtt_options.set_credentials(
            credentials.username.clone(),
            credentials.password.clone(),
        );
        
        let (client, connection) = Client::new(mqtt_options, 10);
        
        Ok(Self {
            client,
            connection: Arc::new(Mutex::new(connection)),
            credentials,
        })
    }
    
    fn start_event_loop(&self) -> thread::JoinHandle<()> {
        let connection = Arc::clone(&self.connection);
        
        thread::spawn(move || {
            loop {
                let mut conn = connection.lock().unwrap();
                
                match conn.iter().next() {
                    Some(Ok(Event::Incoming(Packet::ConnAck(connack)))) => {
                        match connack.code {
                            ConnectReturnCode::Success => {
                                println!("✓ Authentication successful!");
                            }
                            ConnectReturnCode::BadUserNamePassword => {
                                eprintln!("✗ Authentication failed: Invalid username or password");
                                break;
                            }
                            ConnectReturnCode::NotAuthorized => {
                                eprintln!("✗ Authentication failed: Not authorized");
                                break;
                            }
                            code => {
                                eprintln!("✗ Connection failed: {:?}", code);
                                break;
                            }
                        }
                    }
                    Some(Ok(Event::Incoming(Packet::Publish(msg)))) => {
                        println!("📨 Message received on '{}'", msg.topic);
                        if let Ok(payload) = String::from_utf8(msg.payload.to_vec()) {
                            println!("   Payload: {}", payload);
                        }
                    }
                    Some(Err(e)) => {
                        eprintln!("Connection error: {:?}", e);
                        thread::sleep(Duration::from_secs(5));
                    }
                    _ => {}
                }
            }
        })
    }
    
    fn publish(&mut self, topic: &str, payload: &str, qos: QoS) -> Result<(), Box<dyn Error>> {
        self.client.publish(topic, qos, false, payload.as_bytes())?;
        Ok(())
    }
    
    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<(), Box<dyn Error>> {
        self.client.subscribe(topic, qos)?;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let credentials = MqttCredentials::new(
        "mqtt_user".to_string(),
        "secure_password_123".to_string(),
    );
    
    let mut client = SecureMqttClient::new(
        "broker.example.com",
        1883,
        "rust_advanced_client",
        credentials,
    )?;
    
    // Start the event loop in background
    let handle = client.start_event_loop();
    
    // Wait for connection
    thread::sleep(Duration::from_secs(2));
    
    // Subscribe and publish
    client.subscribe("test/topic", QoS::AtLeastOnce)?;
    client.publish("test/topic", "Hello from Rust!", QoS::AtLeastOnce)?;
    
    // Keep running
    thread::sleep(Duration::from_secs(30));
    
    handle.join().unwrap();
    
    Ok(())
}
```

## Summary

Username and password authentication in MQTT provides a fundamental access control mechanism that's simple to implement yet effective when properly configured. The key takeaways are:

**Strengths:** Easy to implement across all MQTT clients and brokers, minimal computational overhead, widely supported standard, suitable for many IoT and messaging applications, and can be integrated with existing authentication systems.

**Limitations:** Credentials sent in plaintext without TLS/SSL encryption, vulnerable to brute-force attacks without additional protections, less secure than certificate-based authentication, requires secure credential storage and management.

**Best Practices:** Always use TLS/SSL encryption to protect credentials in transit, implement strong password policies and regular rotation, combine with additional security layers like ACLs and IP whitelisting, monitor failed authentication attempts for security threats, use environment variables or secure vaults for credential storage rather than hardcoding, and consider certificate-based authentication for high-security environments.

The code examples demonstrate practical implementations in C/C++ and Rust, showing how to properly configure authentication, handle connection errors, and manage credentials securely. For production systems, username/password authentication should be viewed as one component of a comprehensive security strategy that includes encryption, authorization controls, and monitoring.