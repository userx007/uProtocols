# End-to-End Encryption in MQTT

## Overview

End-to-end encryption (E2E) in MQTT refers to encrypting message payloads at the application layer before they're published, ensuring that only the intended recipient can decrypt them. This is independent of transport-layer security (TLS/SSL) and protects data even if the broker is compromised or untrusted.

## Why End-to-End Encryption?

**Transport security (TLS) protects data in transit** between client and broker, but:
- The broker can read plaintext messages
- Messages stored on the broker are readable
- Compromised broker exposes all data
- Regulatory compliance may require payload encryption

**E2E encryption ensures:**
- Only sender and receiver can read payloads
- Zero-trust architecture (broker sees only ciphertext)
- Protection against insider threats
- Data privacy even with cloud brokers

## Key Concepts

1. **Symmetric Encryption**: Same key for encryption/decryption (AES-256-GCM)
2. **Asymmetric Encryption**: Public/private key pairs (RSA, ECC)
3. **Hybrid Approach**: Asymmetric for key exchange, symmetric for data
4. **Key Management**: Secure storage and distribution of encryption keys

## C/C++ Implementation

Using OpenSSL for AES-256-GCM encryption:

```c
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <mosquitto.h>

#define AES_KEY_SIZE 32
#define GCM_IV_SIZE 12
#define GCM_TAG_SIZE 16

typedef struct {
    unsigned char key[AES_KEY_SIZE];
} encryption_context_t;

// Encrypt payload using AES-256-GCM
int encrypt_payload(encryption_context_t *ctx, 
                   const unsigned char *plaintext, int plaintext_len,
                   unsigned char *ciphertext, int *ciphertext_len,
                   unsigned char *iv, unsigned char *tag) {
    EVP_CIPHER_CTX *cipher_ctx;
    int len;
    
    // Generate random IV
    if (!RAND_bytes(iv, GCM_IV_SIZE)) {
        fprintf(stderr, "IV generation failed\n");
        return -1;
    }
    
    cipher_ctx = EVP_CIPHER_CTX_new();
    if (!cipher_ctx) return -1;
    
    // Initialize encryption
    if (EVP_EncryptInit_ex(cipher_ctx, EVP_aes_256_gcm(), NULL, 
                          ctx->key, iv) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Encrypt data
    if (EVP_EncryptUpdate(cipher_ctx, ciphertext, &len, 
                         plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(cipher_ctx, ciphertext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len += len;
    
    // Get authentication tag
    if (EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_GCM_GET_TAG, 
                           GCM_TAG_SIZE, tag) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Decrypt payload using AES-256-GCM
int decrypt_payload(encryption_context_t *ctx,
                   const unsigned char *ciphertext, int ciphertext_len,
                   const unsigned char *iv, const unsigned char *tag,
                   unsigned char *plaintext, int *plaintext_len) {
    EVP_CIPHER_CTX *cipher_ctx;
    int len;
    
    cipher_ctx = EVP_CIPHER_CTX_new();
    if (!cipher_ctx) return -1;
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(cipher_ctx, EVP_aes_256_gcm(), NULL,
                          ctx->key, iv) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Decrypt data
    if (EVP_DecryptUpdate(cipher_ctx, plaintext, &len,
                         ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *plaintext_len = len;
    
    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_GCM_SET_TAG,
                           GCM_TAG_SIZE, (void*)tag) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Finalize and verify tag
    if (EVP_DecryptFinal_ex(cipher_ctx, plaintext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1; // Authentication failed
    }
    *plaintext_len += len;
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Publish encrypted message
void publish_encrypted(struct mosquitto *mosq, encryption_context_t *ctx,
                      const char *topic, const char *message) {
    unsigned char iv[GCM_IV_SIZE];
    unsigned char tag[GCM_TAG_SIZE];
    unsigned char ciphertext[1024];
    unsigned char payload[1024 + GCM_IV_SIZE + GCM_TAG_SIZE];
    int ciphertext_len;
    
    // Encrypt the message
    if (encrypt_payload(ctx, (unsigned char*)message, strlen(message),
                       ciphertext, &ciphertext_len, iv, tag) == 0) {
        // Format: IV || TAG || CIPHERTEXT
        memcpy(payload, iv, GCM_IV_SIZE);
        memcpy(payload + GCM_IV_SIZE, tag, GCM_TAG_SIZE);
        memcpy(payload + GCM_IV_SIZE + GCM_TAG_SIZE, ciphertext, ciphertext_len);
        
        int total_len = GCM_IV_SIZE + GCM_TAG_SIZE + ciphertext_len;
        mosquitto_publish(mosq, NULL, topic, total_len, payload, 1, false);
        
        printf("Published encrypted message (%d bytes)\n", total_len);
    }
}

// Message callback - decrypt received messages
void on_message(struct mosquitto *mosq, void *obj, 
               const struct mosquitto_message *msg) {
    encryption_context_t *ctx = (encryption_context_t*)obj;
    unsigned char plaintext[1024];
    int plaintext_len;
    
    if (msg->payloadlen < GCM_IV_SIZE + GCM_TAG_SIZE) {
        fprintf(stderr, "Invalid encrypted message\n");
        return;
    }
    
    unsigned char *payload = (unsigned char*)msg->payload;
    unsigned char *iv = payload;
    unsigned char *tag = payload + GCM_IV_SIZE;
    unsigned char *ciphertext = payload + GCM_IV_SIZE + GCM_TAG_SIZE;
    int ciphertext_len = msg->payloadlen - GCM_IV_SIZE - GCM_TAG_SIZE;
    
    if (decrypt_payload(ctx, ciphertext, ciphertext_len, iv, tag,
                       plaintext, &plaintext_len) == 0) {
        plaintext[plaintext_len] = '\0';
        printf("Decrypted: %s\n", plaintext);
    } else {
        fprintf(stderr, "Decryption failed - authentication error\n");
    }
}

int main() {
    struct mosquitto *mosq;
    encryption_context_t ctx;
    
    // Generate or load encryption key (256-bit)
    if (!RAND_bytes(ctx.key, AES_KEY_SIZE)) {
        fprintf(stderr, "Key generation failed\n");
        return 1;
    }
    
    mosquitto_lib_init();
    mosq = mosquitto_new("encrypted_client", true, &ctx);
    
    mosquitto_message_callback_set(mosq, on_message);
    
    mosquitto_connect(mosq, "localhost", 1883, 60);
    mosquitto_subscribe(mosq, NULL, "sensors/+/data", 0);
    
    // Publish encrypted message
    publish_encrypted(mosq, &ctx, "sensors/temp/data", 
                     "{\"temperature\":22.5}");
    
    mosquitto_loop_forever(mosq, -1, 1);
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
```

## Rust Implementation

Using the `aes-gcm` and `paho-mqtt` crates:

```rust
use aes_gcm::{
    aead::{Aead, KeyInit, OsRng},
    Aes256Gcm, Nonce, Key
};
use paho_mqtt as mqtt;
use std::time::Duration;

const NONCE_SIZE: usize = 12;
const TAG_SIZE: usize = 16;

pub struct EncryptionContext {
    cipher: Aes256Gcm,
}

impl EncryptionContext {
    pub fn new(key: &[u8; 32]) -> Self {
        let key = Key::<Aes256Gcm>::from_slice(key);
        let cipher = Aes256Gcm::new(key);
        Self { cipher }
    }
    
    pub fn generate_key() -> [u8; 32] {
        let key = Aes256Gcm::generate_key(&mut OsRng);
        key.into()
    }
    
    // Encrypt payload and return: nonce || ciphertext
    pub fn encrypt(&self, plaintext: &[u8]) -> Result<Vec<u8>, String> {
        let nonce = Aes256Gcm::generate_nonce(&mut OsRng);
        
        let ciphertext = self.cipher
            .encrypt(&nonce, plaintext)
            .map_err(|e| format!("Encryption failed: {}", e))?;
        
        // Format: nonce || ciphertext (tag is included in ciphertext)
        let mut result = Vec::with_capacity(NONCE_SIZE + ciphertext.len());
        result.extend_from_slice(&nonce);
        result.extend_from_slice(&ciphertext);
        
        Ok(result)
    }
    
    // Decrypt payload from: nonce || ciphertext format
    pub fn decrypt(&self, encrypted: &[u8]) -> Result<Vec<u8>, String> {
        if encrypted.len() < NONCE_SIZE {
            return Err("Invalid encrypted data".to_string());
        }
        
        let (nonce_bytes, ciphertext) = encrypted.split_at(NONCE_SIZE);
        let nonce = Nonce::from_slice(nonce_bytes);
        
        self.cipher
            .decrypt(nonce, ciphertext)
            .map_err(|e| format!("Decryption failed: {}", e))
    }
}

fn publish_encrypted(
    client: &mqtt::Client,
    ctx: &EncryptionContext,
    topic: &str,
    message: &str,
) -> Result<(), String> {
    let encrypted = ctx.encrypt(message.as_bytes())?;
    
    let msg = mqtt::MessageBuilder::new()
        .topic(topic)
        .payload(encrypted)
        .qos(1)
        .finalize();
    
    client.publish(msg)
        .map_err(|e| format!("Publish failed: {}", e))?;
    
    println!("Published encrypted message to {}", topic);
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Generate or load encryption key
    let key = EncryptionContext::generate_key();
    let ctx = EncryptionContext::new(&key);
    
    // Create MQTT client
    let create_opts = mqtt::CreateOptionsBuilder::new()
        .server_uri("tcp://localhost:1883")
        .client_id("rust_encrypted_client")
        .finalize();
    
    let client = mqtt::Client::new(create_opts)?;
    
    // Set up message callback
    let ctx_clone = EncryptionContext::new(&key);
    let rx = client.start_consuming();
    
    // Connect to broker
    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .keep_alive_interval(Duration::from_secs(20))
        .clean_session(true)
        .finalize();
    
    client.connect(conn_opts)?;
    println!("Connected to MQTT broker");
    
    // Subscribe to encrypted topics
    client.subscribe("sensors/+/data", 1)?;
    
    // Spawn thread to handle incoming messages
    std::thread::spawn(move || {
        for msg_opt in rx.iter() {
            if let Some(msg) = msg_opt {
                match ctx_clone.decrypt(msg.payload()) {
                    Ok(plaintext) => {
                        let text = String::from_utf8_lossy(&plaintext);
                        println!("Received on {}: {}", msg.topic(), text);
                    }
                    Err(e) => eprintln!("Decryption error: {}", e),
                }
            }
        }
    });
    
    // Publish encrypted messages
    publish_encrypted(
        &client,
        &ctx,
        "sensors/temp/data",
        r#"{"temperature":22.5,"humidity":45}"#,
    )?;
    
    publish_encrypted(
        &client,
        &ctx,
        "sensors/pressure/data",
        r#"{"pressure":1013.25}"#,
    )?;
    
    // Keep running
    std::thread::sleep(Duration::from_secs(30));
    
    client.disconnect(None)?;
    Ok(())
}
```

## Advanced: Hybrid Encryption with Key Exchange

For scenarios where devices don't share pre-shared keys:

```rust
use rsa::{RsaPrivateKey, RsaPublicKey, Pkcs1v15Encrypt};
use rand::rngs::OsRng;

pub struct HybridEncryption {
    private_key: RsaPrivateKey,
    public_key: RsaPublicKey,
}

impl HybridEncryption {
    pub fn new() -> Self {
        let mut rng = OsRng;
        let bits = 2048;
        let private_key = RsaPrivateKey::new(&mut rng, bits)
            .expect("Failed to generate key");
        let public_key = RsaPublicKey::from(&private_key);
        
        Self { private_key, public_key }
    }
    
    // Encrypt: Generate ephemeral AES key, encrypt data with AES,
    // encrypt AES key with RSA public key
    pub fn encrypt_for_recipient(
        &self,
        recipient_public_key: &RsaPublicKey,
        plaintext: &[u8],
    ) -> Result<Vec<u8>, String> {
        let mut rng = OsRng;
        
        // Generate ephemeral AES key
        let aes_key = EncryptionContext::generate_key();
        let ctx = EncryptionContext::new(&aes_key);
        
        // Encrypt data with AES
        let encrypted_data = ctx.encrypt(plaintext)?;
        
        // Encrypt AES key with RSA
        let encrypted_key = recipient_public_key
            .encrypt(&mut rng, Pkcs1v15Encrypt, &aes_key)
            .map_err(|e| format!("RSA encryption failed: {}", e))?;
        
        // Format: encrypted_key_length (2 bytes) || encrypted_key || encrypted_data
        let mut result = Vec::new();
        result.extend_from_slice(&(encrypted_key.len() as u16).to_be_bytes());
        result.extend_from_slice(&encrypted_key);
        result.extend_from_slice(&encrypted_data);
        
        Ok(result)
    }
    
    // Decrypt: Extract RSA-encrypted AES key, decrypt it, use it to decrypt data
    pub fn decrypt_from_sender(&self, encrypted: &[u8]) -> Result<Vec<u8>, String> {
        if encrypted.len() < 2 {
            return Err("Invalid encrypted data".to_string());
        }
        
        let key_len = u16::from_be_bytes([encrypted[0], encrypted[1]]) as usize;
        
        if encrypted.len() < 2 + key_len {
            return Err("Corrupted encrypted data".to_string());
        }
        
        let encrypted_key = &encrypted[2..2 + key_len];
        let encrypted_data = &encrypted[2 + key_len..];
        
        // Decrypt AES key with RSA
        let aes_key = self.private_key
            .decrypt(Pkcs1v15Encrypt, encrypted_key)
            .map_err(|e| format!("RSA decryption failed: {}", e))?;
        
        // Decrypt data with AES
        let mut key_array = [0u8; 32];
        key_array.copy_from_slice(&aes_key[..32]);
        let ctx = EncryptionContext::new(&key_array);
        
        ctx.decrypt(encrypted_data)
    }
}
```

## Summary

**End-to-end encryption in MQTT** adds a critical security layer by encrypting payloads before transmission, ensuring confidentiality even with untrusted brokers. Key implementation aspects include:

- **AES-256-GCM** provides authenticated encryption with strong security guarantees
- **Payload format** typically combines IV/nonce, authentication tag, and ciphertext
- **Key management** is critical—use secure generation, storage (HSM, key vaults), and rotation
- **Hybrid schemes** (RSA + AES) enable encryption without pre-shared keys
- **Performance trade-off**: encryption adds computational overhead but protects sensitive data
- **Complementary to TLS**: Use both transport security (TLS) and payload encryption for defense-in-depth

This approach is essential for IoT deployments handling sensitive data (healthcare, financial, industrial control) or using cloud brokers where zero-trust architecture is required.