# VPN Protocols: OpenVPN, WireGuard, and IPsec

## Overview

VPN (Virtual Private Network) protocols create secure, encrypted tunnels over untrusted networks, allowing private data to traverse public infrastructure safely. The three major implementations—OpenVPN, WireGuard, and IPsec—each offer different trade-offs between security, performance, and complexity.

**OpenVPN** is a mature, SSL/TLS-based solution with extensive configurability. **WireGuard** is a modern, minimalist protocol emphasizing simplicity and speed. **IPsec** is a suite of protocols operating at the network layer, widely used in enterprise environments.

## Core Concepts

### Tunneling
VPNs encapsulate original IP packets within new packets, creating a "tunnel" through which traffic flows securely between endpoints.

### Cryptographic Primitives
- **Symmetric encryption**: AES for bulk data encryption
- **Asymmetric encryption**: RSA/ECC for key exchange
- **HMAC**: Message authentication
- **Key derivation**: Generating session keys from master secrets

### Authentication Methods
- Pre-shared keys (PSK)
- X.509 certificates
- Username/password (with TLS)

---

## OpenVPN

OpenVPN uses SSL/TLS for key exchange and creates tunnels over UDP or TCP. It's highly portable and firewall-friendly.

### C/C++ Example: Basic OpenVPN Client Simulation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define SERVER_IP "10.8.0.1"
#define SERVER_PORT 1194
#define BUFFER_SIZE 2048

// Initialize OpenSSL
void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

// Cleanup OpenSSL
void cleanup_openssl() {
    EVP_cleanup();
}

// Create SSL context
SSL_CTX* create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLS_client_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

// Configure SSL context with certificates
void configure_context(SSL_CTX *ctx) {
    // Load CA certificate
    if (SSL_CTX_load_verify_locations(ctx, "/etc/openvpn/ca.crt", NULL) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Load client certificate
    if (SSL_CTX_use_certificate_file(ctx, "/etc/openvpn/client.crt", 
                                     SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Load client private key
    if (SSL_CTX_use_PrivateKey_file(ctx, "/etc/openvpn/client.key", 
                                    SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Verify private key
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match certificate\n");
        exit(EXIT_FAILURE);
    }
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    SSL_CTX *ctx;
    SSL *ssl;
    char buffer[BUFFER_SIZE];

    init_openssl();
    ctx = create_context();
    configure_context(ctx);

    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Create SSL structure
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);

    // Perform SSL/TLS handshake
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        printf("Connected with %s encryption\n", SSL_get_cipher(ssl));
        
        // Send data through VPN tunnel
        const char *msg = "Hello through VPN tunnel";
        SSL_write(ssl, msg, strlen(msg));
        
        // Receive response
        int bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("Received: %s\n", buffer);
        }
    }

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sockfd);
    SSL_CTX_free(ctx);
    cleanup_openssl();

    return 0;
}
```

### Rust Example: OpenVPN-style TLS Tunnel

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_rustls::{TlsConnector, rustls};
use std::sync::Arc;
use std::error::Error;

async fn create_vpn_connection() -> Result<(), Box<dyn Error>> {
    // Load root certificates
    let mut root_store = rustls::RootCertStore::empty();
    let ca_file = std::fs::File::open("ca.crt")?;
    let mut ca_reader = std::io::BufReader::new(ca_file);
    
    for cert in rustls_pemfile::certs(&mut ca_reader) {
        root_store.add(cert?)?;
    }

    // Configure TLS client
    let config = rustls::ClientConfig::builder()
        .with_root_certificates(root_store)
        .with_no_client_auth();

    let connector = TlsConnector::from(Arc::new(config));
    let server_name = "vpn.example.com".try_into()?;

    // Connect to VPN server
    let stream = TcpStream::connect("10.8.0.1:1194").await?;
    let mut tls_stream = connector.connect(server_name, stream).await?;

    println!("TLS handshake completed");

    // Send data through tunnel
    let message = b"Hello through VPN tunnel";
    tls_stream.write_all(message).await?;

    // Read response
    let mut buffer = vec![0u8; 1024];
    let n = tls_stream.read(&mut buffer).await?;
    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));

    Ok(())
}

#[tokio::main]
async fn main() {
    if let Err(e) = create_vpn_connection().await {
        eprintln!("Error: {}", e);
    }
}
```

---

## WireGuard

WireGuard is built on modern cryptographic primitives (Curve25519, ChaCha20, Poly1305) and uses UDP for transport. It's significantly simpler than OpenVPN or IPsec.

### C Example: WireGuard Key Generation and Configuration

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

#define KEY_SIZE 32
#define BASE64_KEY_SIZE 45

// Generate WireGuard keypair
void generate_keypair(unsigned char *private_key, unsigned char *public_key) {
    // Generate random private key
    randombytes_buf(private_key, KEY_SIZE);
    
    // Clamp private key (WireGuard specification)
    private_key[0] &= 248;
    private_key[31] &= 127;
    private_key[31] |= 64;
    
    // Derive public key using Curve25519
    crypto_scalarmult_base(public_key, private_key);
}

// Convert key to base64
void key_to_base64(const unsigned char *key, char *base64_out) {
    sodium_bin2base64(base64_out, BASE64_KEY_SIZE, key, KEY_SIZE,
                      sodium_base64_VARIANT_ORIGINAL);
}

// Create WireGuard configuration
void create_wg_config(const char *private_key_b64, const char *peer_public_key_b64) {
    printf("[Interface]\n");
    printf("PrivateKey = %s\n", private_key_b64);
    printf("Address = 10.0.0.2/24\n");
    printf("DNS = 1.1.1.1\n\n");
    
    printf("[Peer]\n");
    printf("PublicKey = %s\n", peer_public_key_b64);
    printf("AllowedIPs = 0.0.0.0/0\n");
    printf("Endpoint = vpn.example.com:51820\n");
    printf("PersistentKeepalive = 25\n");
}

int main() {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    unsigned char private_key[KEY_SIZE];
    unsigned char public_key[KEY_SIZE];
    char private_key_b64[BASE64_KEY_SIZE];
    char public_key_b64[BASE64_KEY_SIZE];

    // Generate keypair
    generate_keypair(private_key, public_key);
    
    // Convert to base64
    key_to_base64(private_key, private_key_b64);
    key_to_base64(public_key, public_key_b64);

    printf("Generated WireGuard Keys:\n");
    printf("Private Key: %s\n", private_key_b64);
    printf("Public Key: %s\n\n", public_key_b64);

    // Example peer public key (would be from another machine)
    const char *peer_key = "gN65BkIKy1eCE9pP1wdc8ROUtkHLF2PfAqYdyYBz6EA=";
    
    printf("WireGuard Configuration:\n");
    printf("========================\n");
    create_wg_config(private_key_b64, peer_key);

    return 0;
}
```

### Rust Example: WireGuard Packet Encryption

```rust
use chacha20poly1305::{
    aead::{Aead, KeyInit, OsRng},
    ChaCha20Poly1305, Nonce
};
use x25519_dalek::{EphemeralSecret, PublicKey};
use blake2::{Blake2s256, Digest};

struct WireGuardPeer {
    private_key: [u8; 32],
    public_key: [u8; 32],
    peer_public_key: [u8; 32],
}

impl WireGuardPeer {
    fn new() -> Self {
        let private = EphemeralSecret::random_from_rng(OsRng);
        let public = PublicKey::from(&private);
        
        // For demo, using same key as peer (in reality, would be different)
        let peer_private = EphemeralSecret::random_from_rng(OsRng);
        let peer_public = PublicKey::from(&peer_private);

        WireGuardPeer {
            private_key: private.to_bytes(),
            public_key: public.to_bytes(),
            peer_public_key: peer_public.to_bytes(),
        }
    }

    // Derive shared secret using ECDH
    fn derive_shared_secret(&self) -> [u8; 32] {
        let private = EphemeralSecret::from(self.private_key);
        let peer_public = PublicKey::from(self.peer_public_key);
        let shared = private.diffie_hellman(&peer_public);
        
        // Use BLAKE2s to derive encryption key
        let mut hasher = Blake2s256::new();
        hasher.update(shared.as_bytes());
        let result = hasher.finalize();
        
        result.into()
    }

    // Encrypt packet
    fn encrypt_packet(&self, plaintext: &[u8]) -> Result<Vec<u8>, String> {
        let key_bytes = self.derive_shared_secret();
        let cipher = ChaCha20Poly1305::new(&key_bytes.into());
        
        // In real WireGuard, nonce is derived from counter
        let nonce = Nonce::from_slice(b"unique nonce");
        
        cipher.encrypt(nonce, plaintext)
            .map_err(|e| format!("Encryption failed: {:?}", e))
    }

    // Decrypt packet
    fn decrypt_packet(&self, ciphertext: &[u8]) -> Result<Vec<u8>, String> {
        let key_bytes = self.derive_shared_secret();
        let cipher = ChaCha20Poly1305::new(&key_bytes.into());
        
        let nonce = Nonce::from_slice(b"unique nonce");
        
        cipher.decrypt(nonce, ciphertext)
            .map_err(|e| format!("Decryption failed: {:?}", e))
    }
}

fn main() {
    let peer = WireGuardPeer::new();
    
    println!("Public Key: {:?}", hex::encode(peer.public_key));
    
    // Encrypt a packet
    let message = b"Hello through WireGuard tunnel";
    match peer.encrypt_packet(message) {
        Ok(encrypted) => {
            println!("Encrypted packet ({} bytes)", encrypted.len());
            
            // Decrypt the packet
            match peer.decrypt_packet(&encrypted) {
                Ok(decrypted) => {
                    println!("Decrypted: {}", String::from_utf8_lossy(&decrypted));
                }
                Err(e) => eprintln!("Decryption error: {}", e),
            }
        }
        Err(e) => eprintln!("Encryption error: {}", e),
    }
}
```

---

## IPsec

IPsec operates at the network layer and consists of two main protocols: **AH (Authentication Header)** for integrity and **ESP (Encapsulating Security Payload)** for confidentiality. It uses **IKE (Internet Key Exchange)** for key management.

### C++ Example: IPsec ESP Packet Structure

```cpp
#include <iostream>
#include <cstring>
#include <vector>
#include <iomanip>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <arpa/inet.h>

// ESP header structure
struct ESPHeader {
    uint32_t spi;        // Security Parameters Index
    uint32_t seq_num;    // Sequence Number
    uint8_t iv[16];      // Initialization Vector (AES-128)
};

// ESP trailer
struct ESPTrailer {
    uint8_t pad_length;
    uint8_t next_header;
};

class IPsecESP {
private:
    uint32_t spi_;
    uint32_t seq_num_;
    std::vector<uint8_t> encryption_key_;
    
public:
    IPsecESP(uint32_t spi, const std::vector<uint8_t>& key) 
        : spi_(spi), seq_num_(0), encryption_key_(key) {}
    
    // Encrypt payload using AES-CBC
    std::vector<uint8_t> encrypt_packet(const std::vector<uint8_t>& payload, 
                                        uint8_t next_header) {
        std::vector<uint8_t> packet;
        ESPHeader header;
        
        // Fill ESP header
        header.spi = htonl(spi_);
        header.seq_num = htonl(++seq_num_);
        
        // Generate random IV
        RAND_bytes(header.iv, 16);
        
        // Add header to packet
        packet.insert(packet.end(), 
                     reinterpret_cast<uint8_t*>(&header),
                     reinterpret_cast<uint8_t*>(&header) + sizeof(ESPHeader));
        
        // Calculate padding
        size_t block_size = 16; // AES block size
        size_t pad_length = (block_size - 
                            ((payload.size() + sizeof(ESPTrailer)) % block_size)) 
                            % block_size;
        
        // Prepare data to encrypt: payload + padding + trailer
        std::vector<uint8_t> plaintext = payload;
        for (size_t i = 0; i < pad_length; ++i) {
            plaintext.push_back(static_cast<uint8_t>(i + 1));
        }
        
        ESPTrailer trailer;
        trailer.pad_length = static_cast<uint8_t>(pad_length);
        trailer.next_header = next_header;
        plaintext.insert(plaintext.end(),
                        reinterpret_cast<uint8_t*>(&trailer),
                        reinterpret_cast<uint8_t*>(&trailer) + sizeof(ESPTrailer));
        
        // Encrypt using AES-CBC
        std::vector<uint8_t> ciphertext(plaintext.size());
        AES_KEY aes_key;
        AES_set_encrypt_key(encryption_key_.data(), 128, &aes_key);
        AES_cbc_encrypt(plaintext.data(), ciphertext.data(), plaintext.size(),
                       &aes_key, header.iv, AES_ENCRYPT);
        
        // Append encrypted data
        packet.insert(packet.end(), ciphertext.begin(), ciphertext.end());
        
        return packet;
    }
    
    // Decrypt ESP packet
    std::vector<uint8_t> decrypt_packet(const std::vector<uint8_t>& packet) {
        if (packet.size() < sizeof(ESPHeader)) {
            throw std::runtime_error("Packet too small");
        }
        
        // Extract header
        const ESPHeader* header = reinterpret_cast<const ESPHeader*>(packet.data());
        
        // Extract encrypted payload
        size_t encrypted_size = packet.size() - sizeof(ESPHeader);
        std::vector<uint8_t> ciphertext(packet.begin() + sizeof(ESPHeader), 
                                       packet.end());
        
        // Decrypt using AES-CBC
        std::vector<uint8_t> plaintext(encrypted_size);
        AES_KEY aes_key;
        uint8_t iv[16];
        memcpy(iv, header->iv, 16);
        
        AES_set_decrypt_key(encryption_key_.data(), 128, &aes_key);
        AES_cbc_encrypt(ciphertext.data(), plaintext.data(), encrypted_size,
                       &aes_key, iv, AES_DECRYPT);
        
        // Extract trailer
        const ESPTrailer* trailer = reinterpret_cast<const ESPTrailer*>(
            plaintext.data() + plaintext.size() - sizeof(ESPTrailer));
        
        // Remove padding and trailer
        size_t payload_size = plaintext.size() - trailer->pad_length - 
                             sizeof(ESPTrailer);
        plaintext.resize(payload_size);
        
        return plaintext;
    }
    
    void print_packet(const std::vector<uint8_t>& packet) {
        std::cout << "ESP Packet (" << packet.size() << " bytes):" << std::endl;
        for (size_t i = 0; i < packet.size(); ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                     << static_cast<int>(packet[i]) << " ";
            if ((i + 1) % 16 == 0) std::cout << std::endl;
        }
        std::cout << std::dec << std::endl;
    }
};

int main() {
    // Initialize encryption key (would normally be derived from IKE)
    std::vector<uint8_t> key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    
    IPsecESP ipsec(0x12345678, key);
    
    // Original payload
    std::string message = "Secure IPsec communication";
    std::vector<uint8_t> payload(message.begin(), message.end());
    
    std::cout << "Original payload: " << message << std::endl;
    
    // Encrypt
    auto encrypted = ipsec.encrypt_packet(payload, 4); // Next header = IP
    ipsec.print_packet(encrypted);
    
    // Decrypt
    auto decrypted = ipsec.decrypt_packet(encrypted);
    std::string recovered(decrypted.begin(), decrypted.end());
    std::cout << "Decrypted payload: " << recovered << std::endl;
    
    return 0;
}
```

### Rust Example: IKE (Internet Key Exchange) Simulation

```rust
use rand::Rng;
use sha2::{Sha256, Digest};
use aes::Aes128;
use aes::cipher::{BlockEncrypt, KeyInit};
use aes::cipher::generic_array::GenericArray;

#[derive(Debug)]
struct IKEv2Session {
    initiator_spi: u64,
    responder_spi: u64,
    shared_secret: Option<[u8; 32]>,
}

impl IKEv2Session {
    fn new() -> Self {
        let mut rng = rand::thread_rng();
        IKEv2Session {
            initiator_spi: rng.gen(),
            responder_spi: 0,
            shared_secret: None,
        }
    }

    // Simulated Diffie-Hellman key exchange
    fn perform_key_exchange(&mut self, peer_public: u64) -> u64 {
        let mut rng = rand::thread_rng();
        let private_key: u64 = rng.gen();
        
        // Simplified DH (in reality, uses groups like modp2048)
        let public_key = private_key.wrapping_mul(2);
        
        // Compute shared secret
        let raw_shared = private_key.wrapping_mul(peer_public);
        
        // Derive key material using PRF (HMAC-SHA256)
        let mut hasher = Sha256::new();
        hasher.update(&raw_shared.to_be_bytes());
        let result = hasher.finalize();
        
        let mut secret = [0u8; 32];
        secret.copy_from_slice(&result[..32]);
        self.shared_secret = Some(secret);
        
        public_key
    }

    // Generate CHILD_SA keying material
    fn derive_child_keys(&self) -> ([u8; 16], [u8; 16]) {
        let shared = self.shared_secret.expect("No shared secret");
        
        // Derive encryption key
        let mut hasher = Sha256::new();
        hasher.update(&shared);
        hasher.update(b"CHILD_SA_ENCR");
        let encr_result = hasher.finalize();
        
        // Derive integrity key
        let mut hasher = Sha256::new();
        hasher.update(&shared);
        hasher.update(b"CHILD_SA_INTEG");
        let integ_result = hasher.finalize();
        
        let mut encr_key = [0u8; 16];
        let mut integ_key = [0u8; 16];
        encr_key.copy_from_slice(&encr_result[..16]);
        integ_key.copy_from_slice(&integ_result[..16]);
        
        (encr_key, integ_key)
    }

    fn print_session_info(&self) {
        println!("IKEv2 Session:");
        println!("  Initiator SPI: 0x{:016x}", self.initiator_spi);
        println!("  Responder SPI: 0x{:016x}", self.responder_spi);
        if let Some(secret) = self.shared_secret {
            println!("  Shared Secret: {}", hex::encode(&secret[..16]));
        }
    }
}

fn main() {
    println!("=== IKEv2 Key Exchange Simulation ===\n");
    
    // Initiator
    let mut initiator = IKEv2Session::new();
    println!("Initiator created");
    
    // Responder
    let mut responder = IKEv2Session::new();
    responder.responder_spi = rand::thread_rng().gen();
    println!("Responder created\n");
    
    // Exchange public keys
    let initiator_public = initiator.perform_key_exchange(12345);
    let responder_public = responder.perform_key_exchange(initiator_public);
    
    // Both sides now have shared secret
    initiator.responder_spi = responder.responder_spi;
    
    println!("Key exchange completed\n");
    initiator.print_session_info();
    
    // Derive child SA keys
    let (encr_key, integ_key) = initiator.derive_child_keys();
    println!("\nChild SA Keys:");
    println!("  Encryption Key: {}", hex::encode(encr_key));
    println!("  Integrity Key: {}", hex::encode(integ_key));
}
```

---

## Summary

**VPN protocols** secure communications across untrusted networks through encryption and authentication:

- **OpenVPN**: SSL/TLS-based, highly configurable, works over TCP/UDP, uses certificate-based authentication, excellent firewall traversal, but more complex configuration

- **WireGuard**: Modern cryptographic primitives (Curve25519, ChaCha20Poly1305), minimal codebase (~4,000 lines), superior performance, UDP-only, simpler configuration with public key infrastructure, ideal for performance-critical applications

- **IPsec**: Network-layer protocol suite, consists of ESP (encryption) and AH (authentication), uses IKE for key management, enterprise-grade security, native OS support, more complex but feature-rich

**Key Programming Considerations**:
- Certificate/key management is critical for security
- Proper random number generation for nonces and keys
- Constant-time operations to prevent timing attacks
- Packet fragmentation and MTU handling
- State management for long-running connections
- Replay protection through sequence numbers

Each protocol excels in different scenarios: OpenVPN for compatibility, WireGuard for performance and simplicity, IPsec for enterprise integration.