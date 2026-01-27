# WebSocket Message Encryption: Detailed Overview

## Introduction

Message encryption in WebSocket communications refers to securing data at the application layer through end-to-end encryption (E2E). Unlike transport-layer security (TLS/WSS), which only protects data in transit between client and server, application-layer encryption ensures that messages remain encrypted throughout their entire lifecycle, including at rest on servers and during processing.

## Why Application-Layer Encryption?

**Transport vs. Application Layer Security:**
- **TLS/WSS** encrypts data between endpoints but the server can read plaintext
- **Application-layer encryption** ensures only intended recipients can decrypt messages
- Protects against compromised servers, malicious intermediaries, and data breaches
- Enables zero-knowledge architectures where service providers cannot access user data

**Use Cases:**
- Secure messaging applications (Signal, WhatsApp)
- Financial transaction systems
- Healthcare data exchange
- Confidential business communications
- Privacy-focused collaboration tools

## Encryption Approaches

### 1. Symmetric Encryption
Both parties share the same secret key. Fast and efficient but requires secure key exchange.

**Common Algorithms:**
- AES-256-GCM (Authenticated Encryption)
- ChaCha20-Poly1305

### 2. Asymmetric Encryption
Public/private key pairs where public keys encrypt and private keys decrypt.

**Common Algorithms:**
- RSA-2048/4096
- Elliptic Curve Cryptography (ECC)

### 3. Hybrid Approach
Combine asymmetric encryption for key exchange with symmetric encryption for message data (most common in practice).

---

## C/C++ Implementation

Using OpenSSL for AES-256-GCM encryption:

```c
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>
#include <stdio.h>

#define AES_KEY_SIZE 32  // 256 bits
#define AES_GCM_IV_SIZE 12
#define AES_GCM_TAG_SIZE 16

typedef struct {
    unsigned char key[AES_KEY_SIZE];
    unsigned char iv[AES_GCM_IV_SIZE];
    unsigned char tag[AES_GCM_TAG_SIZE];
} EncryptionContext;

// Initialize encryption context with random key and IV
int init_encryption_context(EncryptionContext *ctx) {
    if (RAND_bytes(ctx->key, AES_KEY_SIZE) != 1) {
        return -1;
    }
    if (RAND_bytes(ctx->iv, AES_GCM_IV_SIZE) != 1) {
        return -1;
    }
    return 0;
}

// Encrypt message using AES-256-GCM
int encrypt_message(EncryptionContext *ctx, 
                   const unsigned char *plaintext, 
                   int plaintext_len,
                   unsigned char *ciphertext,
                   int *ciphertext_len) {
    EVP_CIPHER_CTX *cipher_ctx;
    int len;
    int final_len;
    
    // Create and initialize context
    if (!(cipher_ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }
    
    // Initialize encryption operation
    if (EVP_EncryptInit_ex(cipher_ctx, EVP_aes_256_gcm(), 
                          NULL, ctx->key, ctx->iv) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Encrypt plaintext
    if (EVP_EncryptUpdate(cipher_ctx, ciphertext, &len, 
                         plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(cipher_ctx, ciphertext + len, 
                           &final_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len += final_len;
    
    // Get authentication tag
    if (EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_GCM_GET_TAG, 
                           AES_GCM_TAG_SIZE, ctx->tag) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Decrypt message using AES-256-GCM
int decrypt_message(EncryptionContext *ctx,
                   const unsigned char *ciphertext,
                   int ciphertext_len,
                   unsigned char *plaintext,
                   int *plaintext_len) {
    EVP_CIPHER_CTX *cipher_ctx;
    int len;
    int final_len;
    
    if (!(cipher_ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(cipher_ctx, EVP_aes_256_gcm(), 
                          NULL, ctx->key, ctx->iv) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Decrypt ciphertext
    if (EVP_DecryptUpdate(cipher_ctx, plaintext, &len, 
                         ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *plaintext_len = len;
    
    // Set expected tag value
    if (EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_GCM_SET_TAG, 
                           AES_GCM_TAG_SIZE, ctx->tag) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Finalize and verify authentication tag
    if (EVP_DecryptFinal_ex(cipher_ctx, plaintext + len, 
                           &final_len) != 1) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;  // Authentication failed
    }
    *plaintext_len += final_len;
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Example WebSocket integration
void send_encrypted_websocket_message(void *ws_connection, 
                                     EncryptionContext *ctx,
                                     const char *message) {
    unsigned char ciphertext[1024];
    int ciphertext_len;
    
    // Generate new IV for each message
    RAND_bytes(ctx->iv, AES_GCM_IV_SIZE);
    
    // Encrypt the message
    if (encrypt_message(ctx, (unsigned char*)message, 
                       strlen(message), ciphertext, 
                       &ciphertext_len) == 0) {
        
        // Create encrypted packet: IV + Ciphertext + Tag
        unsigned char packet[2048];
        int offset = 0;
        
        memcpy(packet + offset, ctx->iv, AES_GCM_IV_SIZE);
        offset += AES_GCM_IV_SIZE;
        
        memcpy(packet + offset, ciphertext, ciphertext_len);
        offset += ciphertext_len;
        
        memcpy(packet + offset, ctx->tag, AES_GCM_TAG_SIZE);
        offset += AES_GCM_TAG_SIZE;
        
        // Send via WebSocket (pseudo-code)
        // ws_send_binary(ws_connection, packet, offset);
        printf("Encrypted packet ready (%d bytes)\n", offset);
    }
}
```

### C++ Modern Approach with libsodium

```cpp
#include <sodium.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <memory>

class WebSocketEncryption {
private:
    std::vector<unsigned char> shared_key;
    
public:
    WebSocketEncryption() : shared_key(crypto_secretbox_KEYBYTES) {
        if (sodium_init() < 0) {
            throw std::runtime_error("Failed to initialize libsodium");
        }
        // Generate random key
        crypto_secretbox_keygen(shared_key.data());
    }
    
    // Encrypt message with authenticated encryption
    std::vector<unsigned char> encrypt(const std::string& plaintext) {
        // Generate random nonce
        std::vector<unsigned char> nonce(crypto_secretbox_NONCEBYTES);
        randombytes_buf(nonce.data(), nonce.size());
        
        // Allocate space for ciphertext
        std::vector<unsigned char> ciphertext(
            crypto_secretbox_MACBYTES + plaintext.size()
        );
        
        // Encrypt
        if (crypto_secretbox_easy(
                ciphertext.data(),
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                plaintext.size(),
                nonce.data(),
                shared_key.data()
            ) != 0) {
            throw std::runtime_error("Encryption failed");
        }
        
        // Prepend nonce to ciphertext
        std::vector<unsigned char> result;
        result.reserve(nonce.size() + ciphertext.size());
        result.insert(result.end(), nonce.begin(), nonce.end());
        result.insert(result.end(), ciphertext.begin(), ciphertext.end());
        
        return result;
    }
    
    // Decrypt message
    std::string decrypt(const std::vector<unsigned char>& encrypted_data) {
        if (encrypted_data.size() < crypto_secretbox_NONCEBYTES + 
            crypto_secretbox_MACBYTES) {
            throw std::runtime_error("Invalid encrypted data");
        }
        
        // Extract nonce
        std::vector<unsigned char> nonce(
            encrypted_data.begin(),
            encrypted_data.begin() + crypto_secretbox_NONCEBYTES
        );
        
        // Extract ciphertext
        std::vector<unsigned char> ciphertext(
            encrypted_data.begin() + crypto_secretbox_NONCEBYTES,
            encrypted_data.end()
        );
        
        // Allocate space for plaintext
        std::vector<unsigned char> plaintext(
            ciphertext.size() - crypto_secretbox_MACBYTES
        );
        
        // Decrypt
        if (crypto_secretbox_open_easy(
                plaintext.data(),
                ciphertext.data(),
                ciphertext.size(),
                nonce.data(),
                shared_key.data()
            ) != 0) {
            throw std::runtime_error("Decryption failed or authentication error");
        }
        
        return std::string(plaintext.begin(), plaintext.end());
    }
    
    // Get key for sharing (in real app, use secure key exchange)
    std::vector<unsigned char> get_key() const {
        return shared_key;
    }
    
    void set_key(const std::vector<unsigned char>& key) {
        if (key.size() != crypto_secretbox_KEYBYTES) {
            throw std::runtime_error("Invalid key size");
        }
        shared_key = key;
    }
};

// Usage example
void websocket_example() {
    WebSocketEncryption crypto;
    
    std::string message = "Sensitive data for WebSocket transmission";
    
    // Encrypt
    auto encrypted = crypto.encrypt(message);
    
    // In practice: send encrypted data via WebSocket
    // ws_send_binary(connection, encrypted.data(), encrypted.size());
    
    // Decrypt
    std::string decrypted = crypto.decrypt(encrypted);
    
    // Verify
    if (message == decrypted) {
        printf("Encryption/Decryption successful!\n");
    }
}
```

---

## Rust Implementation

Using the `ring` and `chacha20poly1305` crates:

```rust
use chacha20poly1305::{
    aead::{Aead, KeyInit, OsRng},
    ChaCha20Poly1305, Nonce
};
use rand::RngCore;
use std::error::Error;

pub struct WebSocketCrypto {
    cipher: ChaCha20Poly1305,
}

impl WebSocketCrypto {
    /// Create new encryption context with random key
    pub fn new() -> Self {
        let key = ChaCha20Poly1305::generate_key(&mut OsRng);
        let cipher = ChaCha20Poly1305::new(&key);
        
        Self { cipher }
    }
    
    /// Create from existing key
    pub fn from_key(key: &[u8; 32]) -> Self {
        let cipher = ChaCha20Poly1305::new(key.into());
        Self { cipher }
    }
    
    /// Encrypt a message
    pub fn encrypt(&self, plaintext: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
        // Generate random nonce
        let mut nonce_bytes = [0u8; 12];
        OsRng.fill_bytes(&mut nonce_bytes);
        let nonce = Nonce::from_slice(&nonce_bytes);
        
        // Encrypt the data
        let ciphertext = self.cipher.encrypt(nonce, plaintext)
            .map_err(|e| format!("Encryption error: {}", e))?;
        
        // Prepend nonce to ciphertext for transmission
        let mut result = Vec::with_capacity(nonce_bytes.len() + ciphertext.len());
        result.extend_from_slice(&nonce_bytes);
        result.extend_from_slice(&ciphertext);
        
        Ok(result)
    }
    
    /// Decrypt a message
    pub fn decrypt(&self, encrypted_data: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
        if encrypted_data.len() < 12 {
            return Err("Invalid encrypted data: too short".into());
        }
        
        // Extract nonce
        let (nonce_bytes, ciphertext) = encrypted_data.split_at(12);
        let nonce = Nonce::from_slice(nonce_bytes);
        
        // Decrypt
        let plaintext = self.cipher.decrypt(nonce, ciphertext)
            .map_err(|e| format!("Decryption error: {}", e))?;
        
        Ok(plaintext)
    }
}

// WebSocket integration example using tokio-tungstenite
use tokio_tungstenite::{connect_async, tungstenite::Message};
use futures_util::{StreamExt, SinkExt};

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let crypto = WebSocketCrypto::new();
    
    // Connect to WebSocket server
    let (ws_stream, _) = connect_async("ws://localhost:8080").await?;
    let (mut write, mut read) = ws_stream.split();
    
    // Send encrypted message
    let plaintext = b"Secret message over WebSocket";
    let encrypted = crypto.encrypt(plaintext)?;
    write.send(Message::Binary(encrypted)).await?;
    
    // Receive and decrypt message
    if let Some(Ok(msg)) = read.next().await {
        if let Message::Binary(data) = msg {
            let decrypted = crypto.decrypt(&data)?;
            let text = String::from_utf8(decrypted)?;
            println!("Received: {}", text);
        }
    }
    
    Ok(())
}
```

### Advanced Rust Example with Hybrid Encryption

```rust
use rsa::{RsaPrivateKey, RsaPublicKey, Pkcs1v15Encrypt};
use rsa::pkcs8::{EncodePublicKey, DecodePublicKey};
use rand::rngs::OsRng;
use chacha20poly1305::{ChaCha20Poly1305, KeyInit};

pub struct HybridWebSocketCrypto {
    private_key: RsaPrivateKey,
    public_key: RsaPublicKey,
    peer_public_key: Option<RsaPublicKey>,
}

impl HybridWebSocketCrypto {
    /// Generate new RSA key pair
    pub fn new(bits: usize) -> Result<Self, Box<dyn Error>> {
        let mut rng = OsRng;
        let private_key = RsaPrivateKey::new(&mut rng, bits)?;
        let public_key = RsaPublicKey::from(&private_key);
        
        Ok(Self {
            private_key,
            public_key,
            peer_public_key: None,
        })
    }
    
    /// Set peer's public key for encryption
    pub fn set_peer_public_key(&mut self, pem: &str) -> Result<(), Box<dyn Error>> {
        let public_key = RsaPublicKey::from_public_key_pem(pem)?;
        self.peer_public_key = Some(public_key);
        Ok(())
    }
    
    /// Get own public key as PEM
    pub fn get_public_key_pem(&self) -> Result<String, Box<dyn Error>> {
        Ok(self.public_key.to_public_key_pem(Default::default())?)
    }
    
    /// Encrypt message using hybrid approach
    pub fn encrypt_message(&self, plaintext: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
        let peer_key = self.peer_public_key.as_ref()
            .ok_or("Peer public key not set")?;
        
        // Generate random symmetric key
        let symmetric_key = ChaCha20Poly1305::generate_key(&mut OsRng);
        
        // Encrypt the symmetric key with peer's public key
        let mut rng = OsRng;
        let encrypted_key = peer_key.encrypt(&mut rng, Pkcs1v15Encrypt, &symmetric_key)?;
        
        // Encrypt the message with symmetric key
        let cipher = ChaCha20Poly1305::new(&symmetric_key);
        let mut nonce_bytes = [0u8; 12];
        OsRng.fill_bytes(&mut nonce_bytes);
        
        let ciphertext = cipher.encrypt((&nonce_bytes).into(), plaintext)
            .map_err(|e| format!("Symmetric encryption failed: {}", e))?;
        
        // Package: [encrypted_key_len(2)] [encrypted_key] [nonce(12)] [ciphertext]
        let mut result = Vec::new();
        result.extend_from_slice(&(encrypted_key.len() as u16).to_be_bytes());
        result.extend_from_slice(&encrypted_key);
        result.extend_from_slice(&nonce_bytes);
        result.extend_from_slice(&ciphertext);
        
        Ok(result)
    }
    
    /// Decrypt message using hybrid approach
    pub fn decrypt_message(&self, encrypted_data: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
        if encrypted_data.len() < 2 {
            return Err("Invalid encrypted data".into());
        }
        
        // Extract encrypted symmetric key length
        let key_len = u16::from_be_bytes([encrypted_data[0], encrypted_data[1]]) as usize;
        
        if encrypted_data.len() < 2 + key_len + 12 {
            return Err("Invalid encrypted data format".into());
        }
        
        // Extract encrypted symmetric key
        let encrypted_key = &encrypted_data[2..2 + key_len];
        
        // Decrypt symmetric key with our private key
        let symmetric_key_bytes = self.private_key.decrypt(Pkcs1v15Encrypt, encrypted_key)?;
        
        if symmetric_key_bytes.len() != 32 {
            return Err("Invalid symmetric key size".into());
        }
        
        let mut key_array = [0u8; 32];
        key_array.copy_from_slice(&symmetric_key_bytes);
        
        // Extract nonce and ciphertext
        let nonce_start = 2 + key_len;
        let nonce = &encrypted_data[nonce_start..nonce_start + 12];
        let ciphertext = &encrypted_data[nonce_start + 12..];
        
        // Decrypt message
        let cipher = ChaCha20Poly1305::new(&key_array.into());
        let plaintext = cipher.decrypt(nonce.into(), ciphertext)
            .map_err(|e| format!("Symmetric decryption failed: {}", e))?;
        
        Ok(plaintext)
    }
}

// Example usage
fn hybrid_encryption_example() -> Result<(), Box<dyn Error>> {
    // Alice creates her crypto context
    let mut alice = HybridWebSocketCrypto::new(2048)?;
    
    // Bob creates his crypto context
    let mut bob = HybridWebSocketCrypto::new(2048)?;
    
    // Exchange public keys (in practice, this happens over WebSocket)
    let alice_pub = alice.get_public_key_pem()?;
    let bob_pub = bob.get_public_key_pem()?;
    
    alice.set_peer_public_key(&bob_pub)?;
    bob.set_peer_public_key(&alice_pub)?;
    
    // Alice sends encrypted message to Bob
    let message = b"Secret WebSocket message from Alice to Bob";
    let encrypted = alice.encrypt_message(message)?;
    
    // Bob decrypts the message
    let decrypted = bob.decrypt_message(&encrypted)?;
    
    assert_eq!(message, decrypted.as_slice());
    println!("Hybrid encryption successful!");
    
    Ok(())
}
```

---

## Summary

**WebSocket message encryption** provides end-to-end security at the application layer, ensuring that only intended recipients can read message content. Key takeaways:

**Core Concepts:**
- Application-layer encryption complements transport-layer security (TLS/WSS)
- Enables zero-knowledge architectures where servers cannot access plaintext
- Essential for privacy-critical applications like secure messaging and financial systems

**Implementation Approaches:**
- **Symmetric encryption** (AES-GCM, ChaCha20-Poly1305): Fast, efficient, requires secure key exchange
- **Asymmetric encryption** (RSA, ECC): Secure key exchange but slower for large messages
- **Hybrid encryption**: Best of both worlds—asymmetric for key exchange, symmetric for message data

**Best Practices:**
- Always use authenticated encryption (AEAD) to prevent tampering
- Generate new nonces/IVs for each message
- Implement proper key rotation and management
- Use established cryptographic libraries (OpenSSL, libsodium, ring)
- Combine with TLS/WSS for defense-in-depth
- Consider forward secrecy for long-term security

**Language-Specific Strengths:**
- **C/C++**: Direct control, OpenSSL integration, performance-critical applications
- **Rust**: Memory safety, modern cryptographic libraries, zero-cost abstractions

Application-layer encryption in WebSocket applications transforms them into secure communication channels suitable for transmitting highly sensitive information while maintaining the real-time, bidirectional nature of WebSocket protocols.