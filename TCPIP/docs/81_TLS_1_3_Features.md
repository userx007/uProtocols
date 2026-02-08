# TLS 1.3 Features: 0-RTT, Improved Handshake, and Modern Cipher Suites

## Overview

TLS 1.3 (Transport Layer Security version 1.3), defined in RFC 8446, represents a major overhaul of the TLS protocol. It removes insecure cryptographic algorithms, simplifies the handshake process, and introduces performance improvements like 0-RTT (Zero Round Trip Time) resumption. The protocol reduces latency, enhances security, and streamlines cipher suite negotiation.

## Key Features

### 1. **Simplified Handshake (1-RTT)**
TLS 1.3 reduces the full handshake from 2 round trips (in TLS 1.2) to just 1 round trip. The client sends key share material in the initial ClientHello, allowing the server to respond with encrypted application data immediately.

**Handshake Flow:**
```
Client                                           Server

ClientHello
+ key_share
+ signature_algorithms       -------->
                                                ServerHello
                                                + key_share
                                          {EncryptedExtensions}
                                          {CertificateRequest*}
                                                 {Certificate*}
                                           {CertificateVerify*}
                                                    {Finished}
                             <--------     [Application Data*]
{Certificate*}
{CertificateVerify*}
{Finished}                   -------->
[Application Data]           <------->     [Application Data]
```

### 2. **0-RTT Resumption**
For resumed sessions, TLS 1.3 allows clients to send encrypted application data in the first flight, eliminating round-trip latency entirely. This uses Pre-Shared Keys (PSK) from previous sessions.

**Security Note:** 0-RTT data lacks forward secrecy and is vulnerable to replay attacks, so it should only be used for idempotent operations.

### 3. **Modern Cipher Suites**
TLS 1.3 removes all legacy cipher suites and supports only AEAD (Authenticated Encryption with Associated Data) algorithms:

- **TLS_AES_128_GCM_SHA256**
- **TLS_AES_256_GCM_SHA384**
- **TLS_CHACHA20_POLY1305_SHA256**
- **TLS_AES_128_CCM_SHA256**
- **TLS_AES_128_CCM_8_SHA256**

Removed: RSA key exchange, static DH, CBC mode ciphers, RC4, MD5, SHA-1 for signatures, export ciphers, and arbitrary DH groups.

### 4. **Perfect Forward Secrecy (PFS)**
All TLS 1.3 handshakes use ephemeral Diffie-Hellman (DHE) or Elliptic Curve Diffie-Hellman (ECDHE), ensuring that compromising long-term keys doesn't decrypt past sessions.

### 5. **Encrypted Handshake Messages**
After the ServerHello, all handshake messages are encrypted, protecting server certificates and other sensitive data from passive eavesdropping.

## Programming Examples

### C/C++ with OpenSSL

OpenSSL 1.1.1+ supports TLS 1.3.

#### Server Example

```c
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#define PORT 4433
#define CERT_FILE "server.crt"
#define KEY_FILE "server.key"

void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX* create_tls13_context() {
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    // Set minimum protocol version to TLS 1.3
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    
    // Configure cipher suites (TLS 1.3)
    if (SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256") <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    if (SSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    return ctx;
}

int main() {
    init_openssl();
    SSL_CTX *ctx = create_tls13_context();
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(sock, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }
    
    printf("TLS 1.3 Server listening on port %d\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client = accept(sock, (struct sockaddr*)&client_addr, &len);
        
        if (client < 0) {
            perror("Unable to accept");
            continue;
        }
        
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);
        
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {
            printf("TLS 1.3 connection established\n");
            printf("Cipher: %s\n", SSL_get_cipher(ssl));
            
            const char reply[] = "Hello from TLS 1.3 server!\n";
            SSL_write(ssl, reply, strlen(reply));
            
            char buf[1024] = {0};
            int bytes = SSL_read(ssl, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                buf[bytes] = '\0';
                printf("Received: %s\n", buf);
            }
        }
        
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
    }
    
    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
    return 0;
}
```

#### Client Example with 0-RTT

```c
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 4433
#define SESSION_FILE "session.txt"

SSL_CTX* create_tls13_client_context() {
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    // Enable TLS 1.3 only
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    
    // Enable session caching for 0-RTT
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    
    return ctx;
}

void save_session(SSL *ssl) {
    SSL_SESSION *session = SSL_get1_session(ssl);
    if (session) {
        FILE *f = fopen(SESSION_FILE, "wb");
        if (f) {
            PEM_write_SSL_SESSION(f, session);
            fclose(f);
            printf("Session saved for 0-RTT\n");
        }
        SSL_SESSION_free(session);
    }
}

SSL_SESSION* load_session() {
    FILE *f = fopen(SESSION_FILE, "rb");
    if (!f) return NULL;
    
    SSL_SESSION *session = PEM_read_SSL_SESSION(f, NULL, NULL, NULL);
    fclose(f);
    return session;
}

int main() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    
    SSL_CTX *ctx = create_tls13_client_context();
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    
    // Try to load previous session for 0-RTT
    SSL_SESSION *session = load_session();
    if (session) {
        SSL_set_session(ssl, session);
        
        // Attempt 0-RTT write
        const char early_data[] = "Early data (0-RTT)";
        size_t written;
        if (SSL_write_early_data(ssl, early_data, strlen(early_data), &written) == 1) {
            printf("0-RTT data sent: %zu bytes\n", written);
        }
        
        SSL_SESSION_free(session);
    }
    
    // Complete handshake
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        printf("Connected with %s\n", SSL_get_cipher(ssl));
        
        // Check if 0-RTT was accepted
        if (SSL_get_early_data_status(ssl) == SSL_EARLY_DATA_ACCEPTED) {
            printf("0-RTT data was accepted!\n");
        }
        
        char buf[1024] = {0};
        int bytes = SSL_read(ssl, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            printf("Received: %s\n", buf);
        }
        
        save_session(ssl);
    }
    
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    
    return 0;
}
```

### Rust with Rustls

Rustls is a modern TLS library written in Rust that supports TLS 1.3.

#### Server Example

```rust
use std::sync::Arc;
use std::net::TcpListener;
use std::io::{Read, Write};
use rustls::{ServerConfig, ServerConnection, StreamOwned};
use rustls_pemfile::{certs, pkcs8_private_keys};
use std::fs::File;
use std::io::BufReader;

fn load_certs(filename: &str) -> Vec<rustls::pki_types::CertificateDer<'static>> {
    let certfile = File::open(filename).expect("Cannot open certificate file");
    let mut reader = BufReader::new(certfile);
    certs(&mut reader)
        .map(|result| result.unwrap())
        .collect()
}

fn load_private_key(filename: &str) -> rustls::pki_types::PrivateKeyDer<'static> {
    let keyfile = File::open(filename).expect("Cannot open private key file");
    let mut reader = BufReader::new(keyfile);
    
    pkcs8_private_keys(&mut reader)
        .next()
        .unwrap()
        .expect("No private key found")
        .into()
}

fn main() {
    // Load certificate and private key
    let certs = load_certs("server.crt");
    let private_key = load_private_key("server.key");
    
    // Create TLS 1.3 server config
    let config = ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certs, private_key)
        .expect("Bad certificate/key");
    
    let config = Arc::new(config);
    
    // Bind to port
    let listener = TcpListener::bind("127.0.0.1:4433").unwrap();
    println!("TLS 1.3 server listening on port 4433");
    
    for stream in listener.incoming() {
        let mut stream = stream.unwrap();
        let mut conn = ServerConnection::new(config.clone()).unwrap();
        let mut tls_stream = StreamOwned::new(conn, stream);
        
        println!("New TLS 1.3 connection");
        
        // Read client data
        let mut buf = vec![0u8; 1024];
        match tls_stream.read(&mut buf) {
            Ok(n) if n > 0 => {
                println!("Received: {}", String::from_utf8_lossy(&buf[..n]));
                
                // Send response
                let response = b"Hello from TLS 1.3 server!\n";
                tls_stream.write_all(response).unwrap();
            }
            _ => {}
        }
    }
}
```

#### Client Example with Session Resumption

```rust
use std::sync::Arc;
use std::net::TcpStream;
use std::io::{Read, Write};
use rustls::{ClientConfig, ClientConnection, StreamOwned, pki_types::ServerName};
use rustls::client::Resumption;

fn main() {
    // Create TLS 1.3 client config with session resumption
    let mut config = ClientConfig::builder()
        .with_root_certificates(rustls::RootCertStore::empty())
        .with_no_client_auth();
    
    // Enable session resumption (supports 0-RTT)
    config.resumption = Resumption::in_memory_sessions(256);
    
    // Dangerous: accept invalid certificates (for testing only!)
    config.dangerous()
        .set_certificate_verifier(Arc::new(NoVerifier));
    
    let config = Arc::new(config);
    
    // First connection
    println!("=== First Connection ===");
    {
        let stream = TcpStream::connect("127.0.0.1:4433").unwrap();
        let server_name = ServerName::try_from("localhost").unwrap();
        let mut conn = ClientConnection::new(config.clone(), server_name.clone()).unwrap();
        let mut tls_stream = StreamOwned::new(conn, stream);
        
        // Send data
        tls_stream.write_all(b"Hello from client (first connection)").unwrap();
        
        // Read response
        let mut buf = vec![0u8; 1024];
        let n = tls_stream.read(&mut buf).unwrap();
        println!("Received: {}", String::from_utf8_lossy(&buf[..n]));
        
        println!("Session established, ticket cached");
    }
    
    // Second connection - should use resumption (potentially 0-RTT)
    println!("\n=== Second Connection (Resumption) ===");
    {
        let stream = TcpStream::connect("127.0.0.1:4433").unwrap();
        let server_name = ServerName::try_from("localhost").unwrap();
        let mut conn = ClientConnection::new(config.clone(), server_name).unwrap();
        
        // Check if resuming
        if conn.is_handshaking() {
            println!("Attempting session resumption...");
        }
        
        let mut tls_stream = StreamOwned::new(conn, stream);
        
        // Send early data (0-RTT) if possible
        tls_stream.write_all(b"Hello with resumption!").unwrap();
        
        let mut buf = vec![0u8; 1024];
        let n = tls_stream.read(&mut buf).unwrap();
        println!("Received: {}", String::from_utf8_lossy(&buf[..n]));
    }
}

// Dangerous: skip certificate verification (testing only!)
struct NoVerifier;

impl rustls::client::danger::ServerCertVerifier for NoVerifier {
    fn verify_server_cert(
        &self,
        _end_entity: &rustls::pki_types::CertificateDer<'_>,
        _intermediates: &[rustls::pki_types::CertificateDer<'_>],
        _server_name: &rustls::pki_types::ServerName<'_>,
        _ocsp_response: &[u8],
        _now: rustls::pki_types::UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }
    
    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &rustls::pki_types::CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }
    
    fn verify_tls13_signature(
        &self,
        _message: &[u8],
        _cert: &rustls::pki_types::CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }
    
    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        vec![
            rustls::SignatureScheme::RSA_PKCS1_SHA256,
            rustls::SignatureScheme::ECDSA_NISTP256_SHA256,
            rustls::SignatureScheme::ED25519,
        ]
    }
}
```

## Summary

**TLS 1.3** significantly improves upon previous versions by:

1. **Reducing Latency**: 1-RTT handshake (down from 2-RTT) and optional 0-RTT resumption cut connection establishment time dramatically
2. **Enhancing Security**: Removing weak cryptography, enforcing AEAD ciphers, requiring forward secrecy, and encrypting more of the handshake
3. **Simplifying Protocol**: Streamlined cipher suite negotiation, removed legacy features, and clearer state machine
4. **Modern Cryptography**: Support for ChaCha20-Poly1305, AES-GCM only, and strong key agreement algorithms

**Key Considerations**:
- 0-RTT trades some security (replay vulnerability) for performance; use only for idempotent requests
- All major libraries (OpenSSL 1.1.1+, BoringSSL, Rustls, etc.) support TLS 1.3
- TLS 1.3 is incompatible with middleboxes expecting TLS 1.2 behavior, though compatibility fallback mechanisms exist
- Perfect forward secrecy is mandatory, protecting past communications even if keys are compromised later

TLS 1.3 represents the current state-of-the-art in transport security, balancing performance with robust cryptographic protection.