# TLS/SSL with OpenSSL: Comprehensive Guide

## Overview

Transport Layer Security (TLS) and its predecessor Secure Sockets Layer (SSL) are cryptographic protocols designed to provide secure communication over computer networks. OpenSSL is a robust, open-source toolkit implementing these protocols, offering encryption, authentication, and data integrity for network applications.

## Key Concepts

**TLS/SSL Fundamentals:**
- **Encryption**: Protects data confidentiality using symmetric encryption (AES, ChaCha20)
- **Authentication**: Verifies identity using X.509 certificates and public key cryptography
- **Integrity**: Ensures data hasn't been tampered with using message authentication codes (MAC)

**TLS Handshake Process:**
1. Client sends "ClientHello" with supported cipher suites
2. Server responds with "ServerHello" and certificate
3. Key exchange occurs (RSA, DH, ECDH)
4. Both parties derive session keys
5. Encrypted communication begins

**Certificate Verification:**
- Chain of trust from root Certificate Authority (CA)
- Certificate validity period checking
- Hostname verification against certificate's Common Name (CN) or Subject Alternative Name (SAN)

## C/C++ Implementation with OpenSSL

### Basic TLS Client

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX* create_client_context() {
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

void configure_context(SSL_CTX *ctx) {
    // Set minimum TLS version
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    
    // Load trusted CA certificates
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    // Enable certificate verification
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    SSL_CTX *ctx;
    SSL *ssl;
    const char *hostname = "example.com";
    int port = 443;

    init_openssl();
    ctx = create_client_context();
    configure_context(ctx);

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "93.184.216.34", &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    // Create SSL connection
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    
    // Set hostname for SNI (Server Name Indication)
    SSL_set_tlsext_host_name(ssl, hostname);

    // Perform TLS handshake
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    printf("Connected with %s encryption\n", SSL_get_cipher(ssl));

    // Verify certificate
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (cert == NULL) {
        fprintf(stderr, "No certificate presented by server\n");
    } else {
        printf("Server certificate:\n");
        char *line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
        printf("Subject: %s\n", line);
        free(line);
        line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
        printf("Issuer: %s\n", line);
        free(line);
        X509_free(cert);
    }

    // Verify the result
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "Certificate verification failed\n");
    }

    // Send HTTP request
    const char *request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    SSL_write(ssl, request, strlen(request));

    // Receive response
    char buffer[4096];
    int bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Received:\n%s\n", buffer);
    }

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();

    return 0;
}
```

### TLS Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

SSL_CTX* create_server_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLS_server_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_server_context(SSL_CTX *ctx) {
    // Set minimum TLS version
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // Load server certificate
    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Load server private key
    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Verify private key matches certificate
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match certificate\n");
        exit(EXIT_FAILURE);
    }

    // Set cipher list (prefer forward secrecy)
    SSL_CTX_set_cipher_list(ctx, "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256");
}

void handle_client(SSL *ssl) {
    char buffer[1024];
    int bytes;

    bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Received: %s\n", buffer);

        const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, TLS!\n";
        SSL_write(ssl, response, strlen(response));
    }
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    SSL_CTX *ctx;

    init_openssl();
    ctx = create_server_context();
    configure_server_context(ctx);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(4433);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 4433\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {
            printf("Client connected with %s\n", SSL_get_cipher(ssl));
            handle_client(ssl);
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }

    close(server_fd);
    SSL_CTX_free(ctx);
    cleanup_openssl();

    return 0;
}
```

## Rust Implementation

Rust provides several TLS libraries, with `native-tls` and `rustls` being the most popular. Here's an example using both:

### Using native-tls (OpenSSL wrapper)

```rust
// Cargo.toml dependencies:
// [dependencies]
// native-tls = "0.2"

use native_tls::{TlsConnector, TlsStream};
use std::io::{Read, Write};
use std::net::TcpStream;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create TLS connector
    let connector = TlsConnector::builder()
        .min_protocol_version(Some(native_tls::Protocol::Tlsv12))
        .build()?;

    // Connect to server
    let stream = TcpStream::connect("example.com:443")?;
    
    // Establish TLS connection
    let mut tls_stream = connector.connect("example.com", stream)?;

    println!("TLS connection established");

    // Send HTTP request
    let request = b"GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    tls_stream.write_all(request)?;

    // Read response
    let mut response = Vec::new();
    tls_stream.read_to_end(&mut response)?;

    println!("Response received: {} bytes", response.len());
    println!("{}", String::from_utf8_lossy(&response[..500.min(response.len())]));

    Ok(())
}
```

### Using rustls (Pure Rust implementation)

```rust
// Cargo.toml dependencies:
// [dependencies]
// rustls = "0.21"
// rustls-native-certs = "0.6"
// webpki-roots = "0.25"

use rustls::{ClientConfig, ClientConnection, RootCertStore, StreamOwned};
use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::Arc;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create root certificate store
    let mut root_store = RootCertStore::empty();
    
    // Add system certificates
    for cert in rustls_native_certs::load_native_certs()? {
        root_store.add(&rustls::Certificate(cert.0))?;
    }

    // Configure TLS client
    let config = ClientConfig::builder()
        .with_safe_defaults()
        .with_root_certificates(root_store)
        .with_no_client_auth();

    let rc_config = Arc::new(config);
    let server_name = "example.com".try_into()?;
    
    // Create TLS connection
    let mut conn = ClientConnection::new(rc_config, server_name)?;
    let mut stream = TcpStream::connect("example.com:443")?;
    let mut tls_stream = StreamOwned::new(conn, stream);

    // Send request
    let request = b"GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    tls_stream.write_all(request)?;

    // Read response
    let mut response = Vec::new();
    tls_stream.read_to_end(&mut response)?;

    println!("Received {} bytes", response.len());
    println!("{}", String::from_utf8_lossy(&response[..500.min(response.len())]));

    Ok(())
}
```

### Rustls Server Example

```rust
use rustls::{ServerConfig, ServerConnection, StreamOwned};
use rustls_pemfile::{certs, pkcs8_private_keys};
use std::fs::File;
use std::io::{BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;

fn load_certs(path: &str) -> Result<Vec<rustls::Certificate>, Box<dyn std::error::Error>> {
    let file = File::open(path)?;
    let mut reader = BufReader::new(file);
    let certs = certs(&mut reader)?
        .into_iter()
        .map(rustls::Certificate)
        .collect();
    Ok(certs)
}

fn load_private_key(path: &str) -> Result<rustls::PrivateKey, Box<dyn std::error::Error>> {
    let file = File::open(path)?;
    let mut reader = BufReader::new(file);
    let mut keys = pkcs8_private_keys(&mut reader)?;
    
    if keys.is_empty() {
        return Err("No private key found".into());
    }
    
    Ok(rustls::PrivateKey(keys.remove(0)))
}

fn handle_client(mut tls_stream: StreamOwned<ServerConnection, TcpStream>) 
    -> Result<(), Box<dyn std::error::Error>> {
    
    let mut buffer = [0u8; 1024];
    let bytes_read = tls_stream.read(&mut buffer)?;
    
    println!("Received: {}", String::from_utf8_lossy(&buffer[..bytes_read]));

    let response = b"HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, TLS!\n";
    tls_stream.write_all(response)?;
    
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Load certificates and private key
    let certs = load_certs("server.crt")?;
    let private_key = load_private_key("server.key")?;

    // Configure server
    let config = ServerConfig::builder()
        .with_safe_defaults()
        .with_no_client_auth()
        .with_single_cert(certs, private_key)?;

    let rc_config = Arc::new(config);

    // Create listener
    let listener = TcpListener::bind("0.0.0.0:4433")?;
    println!("Server listening on port 4433");

    for stream in listener.incoming() {
        match stream {
            Ok(tcp_stream) => {
                let config = Arc::clone(&rc_config);
                
                std::thread::spawn(move || {
                    let conn = ServerConnection::new(config).unwrap();
                    let mut tls_stream = StreamOwned::new(conn, tcp_stream);
                    
                    if let Err(e) = handle_client(tls_stream) {
                        eprintln!("Error handling client: {}", e);
                    }
                });
            }
            Err(e) => eprintln!("Connection failed: {}", e),
        }
    }

    Ok(())
}
```

### Advanced: Certificate Pinning in Rust

```rust
use rustls::{ClientConfig, RootCertStore, Certificate};
use std::sync::Arc;

fn create_pinned_client() -> Result<Arc<ClientConfig>, Box<dyn std::error::Error>> {
    let mut root_store = RootCertStore::empty();
    
    // Load specific trusted certificate
    let cert_file = std::fs::read("trusted_server.crt")?;
    let cert = rustls_pemfile::certs(&mut &cert_file[..])?
        .into_iter()
        .map(Certificate)
        .next()
        .ok_or("No certificate found")?;
    
    root_store.add(&cert)?;

    let config = ClientConfig::builder()
        .with_safe_defaults()
        .with_root_certificates(root_store)
        .with_no_client_auth();

    Ok(Arc::new(config))
}
```

## Compilation and Dependencies

**C/C++ Compilation:**
```bash
# Install OpenSSL development libraries
# Ubuntu/Debian: sudo apt-get install libssl-dev
# CentOS/RHEL: sudo yum install openssl-devel

# Compile client
gcc -o tls_client tls_client.c -lssl -lcrypto

# Compile server
gcc -o tls_server tls_server.c -lssl -lcrypto
```

**Rust Compilation:**
```bash
# Add to Cargo.toml, then:
cargo build --release
```

## Summary

TLS/SSL with OpenSSL provides critical security infrastructure for network applications through encryption, authentication, and integrity verification. The C/C++ implementation offers fine-grained control and wide platform support but requires careful memory management and error handling. Rust provides safer alternatives with `native-tls` wrapping OpenSSL for compatibility and `rustls` offering a pure Rust implementation with memory safety guarantees.

Key implementation considerations include proper certificate validation, hostname verification, cipher suite selection favoring forward secrecy, enforcing minimum TLS versions (TLS 1.2 or higher), and secure private key storage. Both languages support client and server implementations, certificate pinning for enhanced security, and integration with modern applications requiring secure communication channels.