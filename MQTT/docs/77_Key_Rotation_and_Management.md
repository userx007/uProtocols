# MQTT Key Rotation and Management

## Overview

Key rotation and management is a critical security practice in production MQTT deployments that involves systematically replacing cryptographic keys (TLS certificates, pre-shared keys, authentication tokens) before they expire or become compromised. This reduces the window of vulnerability if a key is leaked and ensures compliance with security policies.

## Core Concepts

**Why Key Rotation Matters:**
- Limits the damage from compromised keys
- Meets compliance requirements (PCI-DSS, HIPAA, SOC 2)
- Reduces cryptanalysis opportunities for attackers
- Enables zero-downtime security updates

**Key Types in MQTT:**
- TLS/SSL certificates (server and client)
- Pre-shared keys (PSK)
- Authentication tokens (JWT, OAuth)
- Symmetric encryption keys for payload encryption

**Rotation Strategies:**
- Time-based (e.g., every 90 days)
- Event-based (suspected compromise)
- Dual-key overlap (gradual rotation)

## C/C++ Implementation

Here's a complete example using Eclipse Paho MQTT C library with TLS certificate rotation:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "MQTTClient.h"

#define ADDRESS     "ssl://mqtt.example.com:8883"
#define CLIENTID    "SecureClient"
#define QOS         1
#define TIMEOUT     10000L

typedef struct {
    char ca_cert[256];
    char client_cert[256];
    char client_key[256];
    time_t expiry_time;
    int version;
} TLSCredentials;

typedef struct {
    MQTTClient client;
    TLSCredentials current_creds;
    TLSCredentials next_creds;
    pthread_mutex_t cred_mutex;
    int rotation_in_progress;
} MQTTContext;

// Load credentials from secure storage
int load_credentials(TLSCredentials *creds, int version) {
    snprintf(creds->ca_cert, sizeof(creds->ca_cert), 
             "/etc/mqtt/certs/v%d/ca.crt", version);
    snprintf(creds->client_cert, sizeof(creds->client_cert), 
             "/etc/mqtt/certs/v%d/client.crt", version);
    snprintf(creds->client_key, sizeof(creds->client_key), 
             "/etc/mqtt/certs/v%d/client.key", version);
    
    // Set expiry to 90 days from now
    creds->expiry_time = time(NULL) + (90 * 24 * 60 * 60);
    creds->version = version;
    
    return 1;
}

// Check if credentials are expiring soon (within 7 days)
int credentials_need_rotation(TLSCredentials *creds) {
    time_t now = time(NULL);
    time_t seven_days = 7 * 24 * 60 * 60;
    return (creds->expiry_time - now) < seven_days;
}

// Reconnect with new credentials
int reconnect_with_new_credentials(MQTTContext *ctx, TLSCredentials *new_creds) {
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    // Disconnect gracefully
    MQTTClient_disconnect(ctx->client, 1000);
    
    // Configure new SSL options
    ssl_opts.trustStore = new_creds->ca_cert;
    ssl_opts.keyStore = new_creds->client_cert;
    ssl_opts.privateKey = new_creds->client_key;
    ssl_opts.enableServerCertAuth = 1;
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 0; // Maintain session
    conn_opts.ssl = &ssl_opts;
    
    int rc = MQTTClient_connect(ctx->client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to reconnect with new credentials: %d\n", rc);
        return 0;
    }
    
    printf("Successfully rotated to credential version %d\n", new_creds->version);
    return 1;
}

// Key rotation thread
void* key_rotation_thread(void* arg) {
    MQTTContext *ctx = (MQTTContext*)arg;
    
    while (1) {
        sleep(3600); // Check every hour
        
        pthread_mutex_lock(&ctx->cred_mutex);
        
        if (credentials_need_rotation(&ctx->current_creds) && 
            !ctx->rotation_in_progress) {
            
            printf("Credentials expiring soon, initiating rotation...\n");
            ctx->rotation_in_progress = 1;
            
            // Load next version of credentials
            int next_version = ctx->current_creds.version + 1;
            if (load_credentials(&ctx->next_creds, next_version)) {
                
                // Perform rotation
                if (reconnect_with_new_credentials(ctx, &ctx->next_creds)) {
                    // Swap credentials
                    TLSCredentials temp = ctx->current_creds;
                    ctx->current_creds = ctx->next_creds;
                    
                    // Securely wipe old credentials
                    memset(&temp, 0, sizeof(TLSCredentials));
                    
                    printf("Key rotation completed successfully\n");
                } else {
                    printf("Key rotation failed, keeping current credentials\n");
                }
            }
            
            ctx->rotation_in_progress = 0;
        }
        
        pthread_mutex_unlock(&ctx->cred_mutex);
    }
    
    return NULL;
}

int main() {
    MQTTContext ctx;
    pthread_t rotation_tid;
    
    // Initialize
    memset(&ctx, 0, sizeof(MQTTContext));
    pthread_mutex_init(&ctx.cred_mutex, NULL);
    
    // Load initial credentials
    load_credentials(&ctx.current_creds, 1);
    
    // Create MQTT client
    MQTTClient_create(&ctx.client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Initial connection
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    
    ssl_opts.trustStore = ctx.current_creds.ca_cert;
    ssl_opts.keyStore = ctx.current_creds.client_cert;
    ssl_opts.privateKey = ctx.current_creds.client_key;
    ssl_opts.enableServerCertAuth = 1;
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.ssl = &ssl_opts;
    
    int rc = MQTTClient_connect(ctx.client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Connection failed: %d\n", rc);
        return EXIT_FAILURE;
    }
    
    printf("Connected with credential version %d\n", ctx.current_creds.version);
    
    // Start key rotation thread
    pthread_create(&rotation_tid, NULL, key_rotation_thread, &ctx);
    
    // Publish some messages
    for (int i = 0; i < 100; i++) {
        char payload[100];
        snprintf(payload, sizeof(payload), "Message %d", i);
        
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        MQTTClient_deliveryToken token;
        MQTTClient_publishMessage(ctx.client, "sensor/data", &pubmsg, &token);
        MQTTClient_waitForCompletion(ctx.client, token, TIMEOUT);
        
        sleep(60); // Publish every minute
    }
    
    // Cleanup
    MQTTClient_disconnect(ctx.client, 1000);
    MQTTClient_destroy(&ctx.client);
    pthread_mutex_destroy(&ctx.cred_mutex);
    
    return EXIT_SUCCESS;
}
```

## Rust Implementation

Here's a comprehensive Rust implementation using the `rumqttc` library with token-based authentication rotation:

```rust
use rumqttc::{AsyncClient, MqttOptions, QoS, Transport, TlsConfiguration};
use std::sync::Arc;
use tokio::sync::RwLock;
use tokio::time::{sleep, Duration};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
struct AuthToken {
    token: String,
    expires_at: DateTime<Utc>,
    version: u32,
}

impl AuthToken {
    fn is_expiring_soon(&self) -> bool {
        let now = Utc::now();
        let time_until_expiry = self.expires_at - now;
        time_until_expiry.num_days() < 7
    }
    
    fn is_expired(&self) -> bool {
        Utc::now() > self.expires_at
    }
}

#[derive(Clone)]
struct CredentialManager {
    current_token: Arc<RwLock<AuthToken>>,
    ca_cert_path: String,
    client_cert_path: String,
    client_key_path: String,
}

impl CredentialManager {
    fn new() -> Self {
        let initial_token = AuthToken {
            token: "initial_token_value".to_string(),
            expires_at: Utc::now() + chrono::Duration::days(90),
            version: 1,
        };
        
        Self {
            current_token: Arc::new(RwLock::new(initial_token)),
            ca_cert_path: "/etc/mqtt/certs/ca.crt".to_string(),
            client_cert_path: "/etc/mqtt/certs/client.crt".to_string(),
            client_key_path: "/etc/mqtt/certs/client.key".to_string(),
        }
    }
    
    async fn get_current_token(&self) -> String {
        let token = self.current_token.read().await;
        token.token.clone()
    }
    
    // Simulate fetching new token from auth service
    async fn fetch_new_token(&self) -> Result<AuthToken, Box<dyn std::error::Error>> {
        // In production, this would call your authentication service
        println!("Fetching new authentication token...");
        
        let current = self.current_token.read().await;
        let new_version = current.version + 1;
        
        // Simulate API call delay
        sleep(Duration::from_secs(1)).await;
        
        Ok(AuthToken {
            token: format!("token_v{}_secure_value", new_version),
            expires_at: Utc::now() + chrono::Duration::days(90),
            version: new_version,
        })
    }
    
    async fn rotate_token(&self) -> Result<(), Box<dyn std::error::Error>> {
        let new_token = self.fetch_new_token().await?;
        
        println!("Rotating token from v{} to v{}", 
                 self.current_token.read().await.version,
                 new_token.version);
        
        let mut current = self.current_token.write().await;
        *current = new_token;
        
        println!("Token rotation completed successfully");
        Ok(())
    }
}

async fn create_mqtt_client(
    cred_manager: &CredentialManager,
    client_id: &str,
) -> Result<AsyncClient, Box<dyn std::error::Error>> {
    let mut mqttoptions = MqttOptions::new(client_id, "mqtt.example.com", 8883);
    mqttoptions.set_keep_alive(Duration::from_secs(20));
    
    // Configure TLS
    let ca = std::fs::read(&cred_manager.ca_cert_path)?;
    let client_cert = std::fs::read(&cred_manager.client_cert_path)?;
    let client_key = std::fs::read(&cred_manager.client_key_path)?;
    
    let transport = Transport::Tls(TlsConfiguration::Simple {
        ca,
        alpn: None,
        client_auth: Some((client_cert, client_key)),
    });
    
    mqttoptions.set_transport(transport);
    
    // Set authentication token as username
    let token = cred_manager.get_current_token().await;
    mqttoptions.set_credentials(&token, "");
    
    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Spawn eventloop handler
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(_) => {},
                Err(e) => {
                    eprintln!("MQTT eventloop error: {:?}", e);
                    sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });
    
    Ok(client)
}

async fn reconnect_with_new_credentials(
    cred_manager: &CredentialManager,
    client_id: &str,
) -> Result<AsyncClient, Box<dyn std::error::Error>> {
    println!("Reconnecting with new credentials...");
    create_mqtt_client(cred_manager, client_id).await
}

async fn credential_rotation_task(cred_manager: CredentialManager, client_id: String) {
    let mut current_client = create_mqtt_client(&cred_manager, &client_id)
        .await
        .expect("Failed to create initial client");
    
    loop {
        sleep(Duration::from_secs(3600)).await; // Check every hour
        
        let token = cred_manager.current_token.read().await;
        
        if token.is_expiring_soon() {
            drop(token); // Release lock
            
            println!("Token expiring soon, initiating rotation...");
            
            // Rotate the token
            if let Err(e) = cred_manager.rotate_token().await {
                eprintln!("Token rotation failed: {:?}", e);
                continue;
            }
            
            // Reconnect with new credentials
            match reconnect_with_new_credentials(&cred_manager, &client_id).await {
                Ok(new_client) => {
                    // Gracefully shutdown old client
                    let _ = current_client.disconnect().await;
                    current_client = new_client;
                    println!("Successfully reconnected with new credentials");
                }
                Err(e) => {
                    eprintln!("Failed to reconnect: {:?}", e);
                }
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cred_manager = CredentialManager::new();
    let client_id = "rust_secure_client";
    
    // Create initial client
    let client = create_mqtt_client(&cred_manager, client_id).await?;
    
    // Start credential rotation task
    let rotation_cred_manager = cred_manager.clone();
    tokio::spawn(credential_rotation_task(
        rotation_cred_manager,
        client_id.to_string(),
    ));
    
    // Publish messages
    for i in 0..100 {
        let payload = format!("Message {}", i);
        
        client
            .publish("sensor/data", QoS::AtLeastOnce, false, payload.as_bytes())
            .await?;
        
        println!("Published: {}", payload);
        sleep(Duration::from_secs(60)).await;
    }
    
    client.disconnect().await?;
    Ok(())
}

// Additional helper: Key rotation with HSM integration
#[cfg(feature = "hsm")]
mod hsm_rotation {
    use super::*;
    
    pub struct HSMKeyManager {
        hsm_client: HSMClient, // Your HSM client
        key_id: String,
        rotation_policy: RotationPolicy,
    }
    
    pub struct RotationPolicy {
        pub max_age_days: u32,
        pub max_operations: u64,
    }
    
    impl HSMKeyManager {
        pub async fn rotate_key_if_needed(&mut self) -> Result<(), Box<dyn std::error::Error>> {
            let key_metadata = self.hsm_client.get_key_metadata(&self.key_id).await?;
            
            let needs_rotation = key_metadata.age_days > self.rotation_policy.max_age_days
                || key_metadata.operation_count > self.rotation_policy.max_operations;
            
            if needs_rotation {
                println!("HSM key rotation required");
                let new_key_id = self.hsm_client.create_key().await?;
                
                // Gradual migration: keep both keys active temporarily
                self.hsm_client.enable_dual_key_mode(&self.key_id, &new_key_id).await?;
                
                tokio::time::sleep(Duration::from_secs(300)).await; // 5 min overlap
                
                // Deactivate old key
                self.hsm_client.deactivate_key(&self.key_id).await?;
                self.key_id = new_key_id;
                
                println!("HSM key rotation completed");
            }
            
            Ok(())
        }
    }
}
```

## Summary

**Key Rotation and Management** in MQTT deployments is essential for maintaining long-term security. The practice involves:

- **Automated rotation** of TLS certificates, authentication tokens, and encryption keys before expiry
- **Graceful transitions** using dual-key overlap periods to avoid downtime
- **Secure storage** of credentials using HSMs, key vaults, or encrypted file systems
- **Monitoring and alerting** for upcoming expirations and rotation failures

The C/C++ example demonstrates certificate-based rotation with thread-safe credential swapping, while the Rust implementation shows token-based authentication rotation with async operations. Both approaches maintain active MQTT connections during rotation by reconnecting with new credentials while preserving session state.

Production deployments should integrate with enterprise key management systems (AWS KMS, Azure Key Vault, HashiCorp Vault), implement audit logging, and use automated certificate provisioning services like Let's Encrypt with ACME protocol for seamless renewals.