# OAuth2 and JWT Integration with MQTT

## Overview

Modern MQTT deployments increasingly require sophisticated authentication and authorization mechanisms beyond simple username/password combinations. **OAuth2** (Open Authorization 2.0) and **JWT** (JSON Web Tokens) provide industry-standard solutions for secure, scalable authentication in distributed IoT and messaging systems.

This integration enables:
- **Delegated authorization** without sharing credentials
- **Fine-grained access control** to specific topics and operations
- **Stateless authentication** that scales horizontally
- **Token-based security** with expiration and refresh mechanisms
- **Single Sign-On (SSO)** capabilities across multiple services

## Authentication Flow

### OAuth2 with MQTT

The typical flow combines OAuth2 for obtaining tokens and MQTT for messaging:

1. **Client requests access token** from OAuth2 authorization server
2. **Authorization server validates** client credentials and issues JWT
3. **Client connects to MQTT broker** using JWT as password
4. **Broker validates JWT** (signature, expiration, claims)
5. **Broker enforces authorization** based on JWT claims (topics, QoS levels)

### JWT Structure for MQTT

A JWT token consists of three parts (Header.Payload.Signature):

**Header:**
```json
{
  "alg": "RS256",
  "typ": "JWT"
}
```

**Payload (Claims):**
```json
{
  "sub": "device_12345",
  "iss": "auth.example.com",
  "aud": "mqtt.example.com",
  "exp": 1735689600,
  "iat": 1735686000,
  "scope": "mqtt:publish mqtt:subscribe",
  "topics": ["sensors/+/temperature", "devices/12345/#"]
}
```

## C/C++ Implementation

### Using Eclipse Paho MQTT C Library with JWT

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <jwt.h>
#include <openssl/pem.h>

#define BROKER_ADDRESS "ssl://mqtt.example.com:8883"
#define CLIENT_ID "iot_device_001"
#define QOS 1
#define TIMEOUT 10000L

// Function to create JWT token
char* create_jwt_token(const char* device_id, const char* private_key_path) {
    jwt_t *jwt = NULL;
    char *token = NULL;
    FILE *fp;
    EVP_PKEY *pkey = NULL;
    
    // Read private key
    fp = fopen(private_key_path, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open private key file\n");
        return NULL;
    }
    pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    
    if (!pkey) {
        fprintf(stderr, "Failed to read private key\n");
        return NULL;
    }
    
    // Create JWT
    jwt_new(&jwt);
    
    // Set algorithm
    jwt_set_alg(jwt, JWT_ALG_RS256, (unsigned char*)pkey, 0);
    
    // Add claims
    time_t now = time(NULL);
    jwt_add_grant(jwt, "sub", device_id);
    jwt_add_grant(jwt, "iss", "iot-platform");
    jwt_add_grant(jwt, "aud", "mqtt-broker");
    jwt_add_grant_int(jwt, "iat", now);
    jwt_add_grant_int(jwt, "exp", now + 3600); // 1 hour expiration
    jwt_add_grant(jwt, "scope", "mqtt:publish mqtt:subscribe");
    
    // Encode token
    token = jwt_encode_str(jwt);
    
    // Cleanup
    jwt_free(jwt);
    EVP_PKEY_free(pkey);
    
    return token;
}

// MQTT connection with JWT authentication
int connect_mqtt_with_jwt(const char* jwt_token) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    int rc;
    
    // Create MQTT client
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Configure SSL/TLS
    ssl_opts.trustStore = "/etc/ssl/certs/ca-certificates.crt";
    ssl_opts.enableServerCertAuth = 1;
    
    // Configure connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = CLIENT_ID;
    conn_opts.password = jwt_token;  // JWT as password
    conn_opts.ssl = &ssl_opts;
    
    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Failed to connect, return code %d\n", rc);
        return rc;
    }
    
    printf("Connected to MQTT broker with JWT authentication\n");
    
    // Publish a message
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    
    const char* payload = "{\"temperature\": 22.5, \"humidity\": 60}";
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_publishMessage(client, "sensors/device001/data", &pubmsg, &token);
    printf("Waiting for publication completion\n");
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    printf("Message with delivery token %d delivered\n", token);
    
    // Cleanup
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    
    return rc;
}

int main(int argc, char* argv[]) {
    // Create JWT token
    char* jwt_token = create_jwt_token(CLIENT_ID, "/path/to/private_key.pem");
    
    if (jwt_token) {
        printf("JWT Token: %s\n", jwt_token);
        
        // Connect to MQTT broker using JWT
        connect_mqtt_with_jwt(jwt_token);
        
        free(jwt_token);
    }
    
    return 0;
}
```

### C++ Implementation with Modern Libraries

```cpp
#include <iostream>
#include <string>
#include <chrono>
#include <mqtt/async_client.h>
#include <jwt-cpp/jwt.h>

class MQTTJWTClient {
private:
    std::string broker_address;
    std::string client_id;
    std::string private_key;
    mqtt::async_client client;
    
public:
    MQTTJWTClient(const std::string& address, const std::string& id, 
                  const std::string& key)
        : broker_address(address), client_id(id), private_key(key),
          client(address, id) {}
    
    // Generate JWT token
    std::string generate_jwt() {
        auto now = std::chrono::system_clock::now();
        auto exp = now + std::chrono::hours(1);
        
        auto token = jwt::create()
            .set_issuer("iot-platform")
            .set_subject(client_id)
            .set_audience("mqtt-broker")
            .set_issued_at(now)
            .set_expires_at(exp)
            .set_payload_claim("scope", jwt::claim(std::string("mqtt:publish mqtt:subscribe")))
            .set_payload_claim("topics", jwt::claim(picojson::array({
                picojson::value("sensors/+/temperature"),
                picojson::value("devices/" + client_id + "/#")
            })))
            .sign(jwt::algorithm::rs256("", private_key, "", ""));
        
        return token;
    }
    
    // Connect to MQTT broker with JWT
    void connect_with_jwt() {
        try {
            std::string jwt_token = generate_jwt();
            std::cout << "Generated JWT token" << std::endl;
            
            // Configure connection options
            auto conn_opts = mqtt::connect_options_builder()
                .user_name(client_id)
                .password(jwt_token)
                .keep_alive_interval(std::chrono::seconds(20))
                .clean_session(true)
                .automatic_reconnect(std::chrono::seconds(2), std::chrono::seconds(30))
                .finalize();
            
            // Configure SSL/TLS
            auto ssl_opts = mqtt::ssl_options_builder()
                .trust_store("/etc/ssl/certs/ca-certificates.crt")
                .enable_server_cert_auth(true)
                .finalize();
            
            conn_opts.set_ssl(ssl_opts);
            
            // Connect
            std::cout << "Connecting to broker..." << std::endl;
            auto tok = client.connect(conn_opts);
            tok->wait();
            std::cout << "Connected successfully!" << std::endl;
            
            // Subscribe to topics
            client.subscribe("sensors/+/temperature", 1)->wait();
            std::cout << "Subscribed to sensors/+/temperature" << std::endl;
            
            // Publish message
            auto msg = mqtt::make_message("sensors/device001/temperature", 
                                         "{\"value\": 23.5, \"unit\": \"celsius\"}");
            msg->set_qos(1);
            client.publish(msg)->wait();
            std::cout << "Message published" << std::endl;
            
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
        }
    }
    
    void disconnect() {
        try {
            client.disconnect()->wait();
            std::cout << "Disconnected" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "Error: " << exc.what() << std::endl;
        }
    }
};

int main() {
    const std::string BROKER = "ssl://mqtt.example.com:8883";
    const std::string CLIENT_ID = "cpp_client_001";
    const std::string PRIVATE_KEY = R"(-----BEGIN RSA PRIVATE KEY-----
... your private key ...
-----END RSA PRIVATE KEY-----)";
    
    MQTTJWTClient client(BROKER, CLIENT_ID, PRIVATE_KEY);
    client.connect_with_jwt();
    
    // Keep running for a while
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    client.disconnect();
    
    return 0;
}
```

## Rust Implementation

### Using rumqttc and jsonwebtoken Crates

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Transport};
use jsonwebtoken::{encode, EncodingKey, Header, Algorithm};
use serde::{Serialize, Deserialize};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use tokio::time;

#[derive(Debug, Serialize, Deserialize)]
struct Claims {
    sub: String,          // Subject (device ID)
    iss: String,          // Issuer
    aud: String,          // Audience
    exp: u64,             // Expiration time
    iat: u64,             // Issued at
    scope: String,        // Scopes/permissions
    topics: Vec<String>,  // Allowed topics
}

impl Claims {
    fn new(device_id: &str, duration_secs: u64) -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        Claims {
            sub: device_id.to_string(),
            iss: "iot-platform".to_string(),
            aud: "mqtt-broker".to_string(),
            iat: now,
            exp: now + duration_secs,
            scope: "mqtt:publish mqtt:subscribe".to_string(),
            topics: vec![
                "sensors/+/temperature".to_string(),
                format!("devices/{}/#", device_id),
            ],
        }
    }
}

// Generate JWT token
fn generate_jwt_token(device_id: &str, private_key_pem: &[u8]) -> Result<String, jsonwebtoken::errors::Error> {
    let claims = Claims::new(device_id, 3600); // 1 hour expiration
    
    let header = Header::new(Algorithm::RS256);
    let encoding_key = EncodingKey::from_rsa_pem(private_key_pem)?;
    
    encode(&header, &claims, &encoding_key)
}

// MQTT Client with JWT authentication
struct MqttJwtClient {
    client: AsyncClient,
    device_id: String,
}

impl MqttJwtClient {
    async fn new(broker: &str, port: u16, device_id: &str, jwt_token: &str) 
        -> Result<Self, Box<dyn std::error::Error>> {
        
        let mut mqtt_options = MqttOptions::new(device_id, broker, port);
        mqtt_options.set_keep_alive(Duration::from_secs(20));
        mqtt_options.set_credentials(device_id, jwt_token);
        
        // Configure TLS
        let transport = Transport::tls_with_default_config();
        mqtt_options.set_transport(transport);
        
        let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
        
        // Spawn eventloop handler
        tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(notification) => {
                        println!("Received: {:?}", notification);
                    }
                    Err(e) => {
                        eprintln!("Error: {:?}", e);
                        time::sleep(Duration::from_secs(1)).await;
                    }
                }
            }
        });
        
        Ok(MqttJwtClient {
            client,
            device_id: device_id.to_string(),
        })
    }
    
    async fn subscribe(&self, topic: &str) -> Result<(), rumqttc::ClientError> {
        self.client.subscribe(topic, QoS::AtLeastOnce).await
    }
    
    async fn publish(&self, topic: &str, payload: &str) -> Result<(), rumqttc::ClientError> {
        self.client
            .publish(topic, QoS::AtLeastOnce, false, payload.as_bytes())
            .await
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    const BROKER: &str = "mqtt.example.com";
    const PORT: u16 = 8883;
    const DEVICE_ID: &str = "rust_device_001";
    
    // Load private key from file
    let private_key_pem = std::fs::read("private_key.pem")?;
    
    // Generate JWT token
    let jwt_token = generate_jwt_token(DEVICE_ID, &private_key_pem)?;
    println!("Generated JWT token: {}", jwt_token);
    
    // Create MQTT client with JWT authentication
    let mqtt_client = MqttJwtClient::new(BROKER, PORT, DEVICE_ID, &jwt_token).await?;
    
    // Subscribe to topics
    mqtt_client.subscribe("sensors/+/temperature").await?;
    println!("Subscribed to sensors/+/temperature");
    
    // Publish messages
    for i in 0..5 {
        let payload = format!(r#"{{"temperature": {}, "timestamp": {}}}"#, 
                             20.0 + i as f32 * 0.5, 
                             SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs());
        
        mqtt_client.publish(
            &format!("sensors/{}/temperature", DEVICE_ID),
            &payload
        ).await?;
        
        println!("Published: {}", payload);
        time::sleep(Duration::from_secs(2)).await;
    }
    
    // Keep running to receive messages
    time::sleep(Duration::from_secs(10)).await;
    
    Ok(())
}
```

### Advanced Rust Implementation with Token Refresh

```rust
use std::sync::Arc;
use tokio::sync::RwLock;
use std::time::{SystemTime, UNIX_EPOCH, Duration};

struct TokenManager {
    device_id: String,
    private_key: Vec<u8>,
    current_token: Arc<RwLock<Option<String>>>,
    token_expiry: Arc<RwLock<u64>>,
}

impl TokenManager {
    fn new(device_id: String, private_key: Vec<u8>) -> Self {
        TokenManager {
            device_id,
            private_key,
            current_token: Arc::new(RwLock::new(None)),
            token_expiry: Arc::new(RwLock::new(0)),
        }
    }
    
    async fn get_valid_token(&self) -> Result<String, Box<dyn std::error::Error>> {
        let now = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
        let expiry = *self.token_expiry.read().await;
        
        // Check if token needs refresh (5 minutes before expiry)
        if expiry < now + 300 {
            let new_token = generate_jwt_token(&self.device_id, &self.private_key)?;
            let new_expiry = now + 3600;
            
            *self.current_token.write().await = Some(new_token.clone());
            *self.token_expiry.write().await = new_expiry;
            
            println!("Token refreshed, expires at: {}", new_expiry);
            Ok(new_token)
        } else {
            Ok(self.current_token.read().await.as_ref().unwrap().clone())
        }
    }
    
    // Background task to refresh token automatically
    async fn start_auto_refresh(self: Arc<Self>) {
        tokio::spawn(async move {
            loop {
                time::sleep(Duration::from_secs(300)).await; // Check every 5 minutes
                
                if let Err(e) = self.get_valid_token().await {
                    eprintln!("Failed to refresh token: {:?}", e);
                }
            }
        });
    }
}
```

## Summary

**OAuth2 and JWT integration with MQTT** provides enterprise-grade authentication and authorization for IoT and messaging applications:

### Key Benefits:
- **Enhanced Security**: Token-based authentication eliminates credential sharing
- **Scalability**: Stateless validation enables horizontal scaling of brokers
- **Fine-Grained Control**: JWT claims define precise topic and operation permissions
- **Time-Limited Access**: Automatic token expiration reduces security risks
- **Interoperability**: Standard protocols work across platforms and languages

### Implementation Considerations:
- **Token Management**: Implement refresh logic before expiration
- **Secure Key Storage**: Protect private keys using HSMs or secure enclaves
- **TLS/SSL**: Always use encrypted transport alongside JWT
- **Claim Validation**: Brokers must verify issuer, audience, and expiration
- **Performance**: Cache public keys for signature verification

### Common Use Cases:
- Multi-tenant IoT platforms with device isolation
- Microservices architectures with service-to-service authentication
- Mobile applications requiring temporary MQTT access
- Edge computing scenarios with centralized identity management
- Compliance-driven environments requiring audit trails

The examples demonstrate practical implementations in C/C++ and Rust, showing token generation, MQTT connection establishment, and message publishing/subscribing with JWT-based authentication.