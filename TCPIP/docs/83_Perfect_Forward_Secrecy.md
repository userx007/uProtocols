# Perfect Forward Secrecy (PFS)

## Overview

Perfect Forward Secrecy is a cryptographic property that ensures session keys remain secure even if the server's long-term private key is compromised in the future. This is achieved by generating ephemeral (temporary) key pairs for each session that are discarded after use, ensuring that past communications cannot be decrypted even if an attacker later obtains the server's private key.

## How PFS Works

Traditional key exchange using static RSA keys works like this:
1. Client generates a random pre-master secret
2. Encrypts it with server's public RSA key
3. Server decrypts with its private RSA key
4. Both derive session keys from the pre-master secret

**The problem**: If the server's private key is compromised years later, an attacker who recorded the encrypted traffic can decrypt all past sessions.

With PFS:
1. Both parties generate temporary key pairs for each session
2. They perform a key exchange (typically using Diffie-Hellman)
3. The ephemeral private keys are immediately discarded after deriving session keys
4. Even if the server's long-term key is compromised, past session keys cannot be recovered

## Ephemeral Key Exchange Methods

**ECDHE (Elliptic Curve Diffie-Hellman Ephemeral)**: Most common modern approach, uses elliptic curve cryptography for efficient key exchange.

**DHE (Diffie-Hellman Ephemeral)**: Traditional Diffie-Hellman with ephemeral keys, less efficient than ECDHE but still secure.

## C Implementation Example

Here's a simplified example using OpenSSL for ECDHE:

```c
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/ec.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// Server-side TLS setup with PFS
SSL_CTX* create_pfs_server_context() {
    SSL_CTX *ctx;
    
    // Initialize OpenSSL
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    // Create context with TLS 1.2 or higher
    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    
    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }
    
    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }
    
    // Set cipher suites to only use PFS-enabled ciphers
    // ECDHE provides elliptic curve ephemeral key exchange
    if (!SSL_CTX_set_cipher_list(ctx, "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256")) {
        fprintf(stderr, "Failed to set cipher list\n");
        SSL_CTX_free(ctx);
        return NULL;
    }
    
    // Configure ECDH curve for ephemeral keys
    SSL_CTX_set_ecdh_auto(ctx, 1);
    
    // Or explicitly set a curve
    EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ecdh) {
        SSL_CTX_set_tmp_ecdh(ctx, ecdh);
        EC_KEY_free(ecdh);
    }
    
    return ctx;
}

// Example server accepting connections with PFS
void run_pfs_server(int port) {
    SSL_CTX *ctx = create_pfs_server_context();
    if (!ctx) return;
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        SSL_CTX_free(ctx);
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        SSL_CTX_free(ctx);
        return;
    }
    
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        SSL_CTX_free(ctx);
        return;
    }
    
    printf("Server listening on port %d with PFS enabled\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        // Create new SSL connection with ephemeral keys
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);
        
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {
            // Get cipher info
            const char *cipher = SSL_get_cipher(ssl);
            printf("Connection established with cipher: %s\n", cipher);
            
            // Each connection uses new ephemeral keys
            char buffer[1024];
            int bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                printf("Received: %s\n", buffer);
                SSL_write(ssl, "Message received securely", 25);
            }
        }
        
        // Ephemeral keys are destroyed here
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }
    
    close(server_fd);
    SSL_CTX_free(ctx);
}

int main() {
    run_pfs_server(8443);
    return 0;
}
```

## C++ Implementation Example

Here's a more modern C++ implementation:

```cpp
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

class SSLContextDeleter {
public:
    void operator()(SSL_CTX* ctx) const {
        if (ctx) SSL_CTX_free(ctx);
    }
};

class SSLDeleter {
public:
    void operator()(SSL* ssl) const {
        if (ssl) SSL_free(ssl);
    }
};

using SSLContextPtr = std::unique_ptr<SSL_CTX, SSLContextDeleter>;
using SSLPtr = std::unique_ptr<SSL, SSLDeleter>;

class PFSServer {
private:
    SSLContextPtr ctx_;
    int server_fd_;
    int port_;
    
public:
    PFSServer(int port, const std::string& cert_file, const std::string& key_file)
        : server_fd_(-1), port_(port) {
        
        // Initialize OpenSSL
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        
        // Create TLS context
        ctx_ = SSLContextPtr(SSL_CTX_new(TLS_server_method()));
        if (!ctx_) {
            throw std::runtime_error("Failed to create SSL context");
        }
        
        // Load certificates
        if (SSL_CTX_use_certificate_file(ctx_.get(), cert_file.c_str(), 
                                         SSL_FILETYPE_PEM) <= 0) {
            throw std::runtime_error("Failed to load certificate");
        }
        
        if (SSL_CTX_use_PrivateKey_file(ctx_.get(), key_file.c_str(), 
                                        SSL_FILETYPE_PEM) <= 0) {
            throw std::runtime_error("Failed to load private key");
        }
        
        // Configure PFS-only cipher suites
        configurePFS();
    }
    
    void configurePFS() {
        // Use only ephemeral key exchange ciphers
        const char* cipher_list = 
            "ECDHE-ECDSA-AES256-GCM-SHA384:"
            "ECDHE-RSA-AES256-GCM-SHA384:"
            "ECDHE-ECDSA-AES128-GCM-SHA256:"
            "ECDHE-RSA-AES128-GCM-SHA256:"
            "DHE-RSA-AES256-GCM-SHA384:"
            "DHE-RSA-AES128-GCM-SHA256";
        
        if (!SSL_CTX_set_cipher_list(ctx_.get(), cipher_list)) {
            throw std::runtime_error("Failed to set cipher list");
        }
        
        // Enable automatic ECDH curve selection
        SSL_CTX_set_ecdh_auto(ctx_.get(), 1);
        
        // Set minimum TLS version to 1.2 (PFS support)
        SSL_CTX_set_min_proto_version(ctx_.get(), TLS1_2_VERSION);
        
        // Prefer server cipher order
        SSL_CTX_set_options(ctx_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    
    void start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("Failed to bind");
        }
        
        if (listen(server_fd_, 10) < 0) {
            throw std::runtime_error("Failed to listen");
        }
        
        std::cout << "PFS-enabled server listening on port " << port_ << std::endl;
        
        acceptConnections();
    }
    
    void acceptConnections() {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd_, 
                                  reinterpret_cast<sockaddr*>(&client_addr), 
                                  &client_len);
            
            if (client_fd < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }
            
            handleClient(client_fd);
        }
    }
    
    void handleClient(int client_fd) {
        // Create SSL object with ephemeral keys for this session
        SSLPtr ssl(SSL_new(ctx_.get()));
        if (!ssl) {
            close(client_fd);
            return;
        }
        
        SSL_set_fd(ssl.get(), client_fd);
        
        if (SSL_accept(ssl.get()) <= 0) {
            ERR_print_errors_fp(stderr);
            close(client_fd);
            return;
        }
        
        // Display session information
        displaySessionInfo(ssl.get());
        
        // Handle communication
        std::vector<char> buffer(4096);
        int bytes = SSL_read(ssl.get(), buffer.data(), buffer.size());
        
        if (bytes > 0) {
            std::string message(buffer.data(), bytes);
            std::cout << "Received: " << message << std::endl;
            
            std::string response = "Secure response with PFS";
            SSL_write(ssl.get(), response.c_str(), response.size());
        }
        
        // Ephemeral keys are automatically destroyed when SSL object is freed
        SSL_shutdown(ssl.get());
        close(client_fd);
    }
    
    void displaySessionInfo(SSL* ssl) {
        const char* cipher = SSL_get_cipher(ssl);
        const char* version = SSL_get_version(ssl);
        
        std::cout << "TLS Version: " << version << std::endl;
        std::cout << "Cipher Suite: " << cipher << std::endl;
        
        // Check if PFS is actually being used
        if (std::string(cipher).find("ECDHE") != std::string::npos ||
            std::string(cipher).find("DHE") != std::string::npos) {
            std::cout << "✓ Perfect Forward Secrecy is ACTIVE" << std::endl;
        } else {
            std::cout << "✗ Warning: PFS not active!" << std::endl;
        }
    }
    
    ~PFSServer() {
        if (server_fd_ >= 0) {
            close(server_fd_);
        }
    }
};

int main() {
    try {
        PFSServer server(8443, "server.crt", "server.key");
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## Rust Implementation Example

Rust implementation using the `rustls` library, which has PFS built-in by default:

```rust
use rustls::{ServerConfig, ServerConnection, StreamOwned};
use rustls_pemfile::{certs, rsa_private_keys};
use std::fs::File;
use std::io::{BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::error::Error;

// rustls provides PFS by default with modern cipher suites
fn create_pfs_server_config(
    cert_path: &str,
    key_path: &str,
) -> Result<Arc<ServerConfig>, Box<dyn Error>> {
    
    // Load certificates
    let cert_file = File::open(cert_path)?;
    let mut cert_reader = BufReader::new(cert_file);
    let certs = certs(&mut cert_reader)?
        .into_iter()
        .map(|cert| cert.to_vec())
        .collect::<Vec<_>>();
    
    // Load private key
    let key_file = File::open(key_path)?;
    let mut key_reader = BufReader::new(key_file);
    let mut keys = rsa_private_keys(&mut key_reader)?;
    
    if keys.is_empty() {
        return Err("No private keys found".into());
    }
    
    let key = rustls::PrivateKey(keys.remove(0).secret_der().to_vec());
    
    // Create server config with PFS-enabled cipher suites
    let config = ServerConfig::builder()
        .with_safe_default_cipher_suites()  // Includes only PFS ciphers
        .with_safe_default_kx_groups()      // Ephemeral key exchange groups
        .with_safe_default_protocol_versions()?
        .with_no_client_auth()
        .with_single_cert(certs, key)?;
    
    Ok(Arc::new(config))
}

fn handle_client(
    stream: TcpStream,
    config: Arc<ServerConfig>,
) -> Result<(), Box<dyn Error>> {
    
    // Create server connection with ephemeral keys
    let mut conn = ServerConnection::new(config)?;
    let mut tls_stream = StreamOwned::new(conn, stream);
    
    // Get negotiated cipher suite info
    if let Some(suite) = tls_stream.conn.negotiated_cipher_suite() {
        println!("Negotiated cipher suite: {:?}", suite.suite());
        
        // rustls only supports PFS cipher suites
        println!("✓ Perfect Forward Secrecy is ACTIVE");
    }
    
    // Read client data
    let mut buffer = [0u8; 4096];
    match tls_stream.read(&mut buffer) {
        Ok(n) if n > 0 => {
            let message = String::from_utf8_lossy(&buffer[..n]);
            println!("Received: {}", message);
            
            // Send response
            tls_stream.write_all(b"Secure response with PFS")?;
        }
        Ok(_) => println!("Connection closed by client"),
        Err(e) => eprintln!("Read error: {}", e),
    }
    
    // Ephemeral keys are automatically destroyed when connection drops
    Ok(())
}

fn run_pfs_server(port: u16, cert_path: &str, key_path: &str) -> Result<(), Box<dyn Error>> {
    let config = create_pfs_server_config(cert_path, key_path)?;
    
    let listener = TcpListener::bind(format!("0.0.0.0:{}", port))?;
    println!("PFS-enabled server listening on port {}", port);
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                let config = Arc::clone(&config);
                
                // Spawn thread for each connection
                std::thread::spawn(move || {
                    if let Err(e) = handle_client(stream, config) {
                        eprintln!("Client error: {}", e);
                    }
                });
            }
            Err(e) => eprintln!("Connection failed: {}", e),
        }
    }
    
    Ok(())
}

// Example demonstrating ephemeral key exchange details
mod key_exchange_demo {
    use rustls::crypto::ring as provider;
    use rustls::SupportedKxGroup;
    
    pub fn display_supported_groups() {
        println!("\nSupported ephemeral key exchange groups:");
        
        // rustls supports these ephemeral groups by default
        let groups = provider::kx_group::ALL_KX_GROUPS;
        
        for group in groups {
            println!("  - {}: ephemeral keys generated per-session", 
                     group.name());
        }
        
        println!("\nKey exchange properties:");
        println!("  ✓ New ephemeral key pair generated for each session");
        println!("  ✓ Private keys never written to disk");
        println!("  ✓ Keys destroyed immediately after session");
        println!("  ✓ Past sessions cannot be decrypted if long-term key compromised");
    }
}

// Client example with PFS
fn create_pfs_client() -> Result<Arc<rustls::ClientConfig>, Box<dyn Error>> {
    let mut root_store = rustls::RootCertStore::empty();
    
    // Add system certificates
    for cert in rustls_native_certs::load_native_certs()? {
        root_store.add(&rustls::Certificate(cert.0))?;
    }
    
    let config = rustls::ClientConfig::builder()
        .with_safe_default_cipher_suites()  // PFS-only ciphers
        .with_safe_default_kx_groups()      // Ephemeral key exchange
        .with_safe_default_protocol_versions()?
        .with_root_certificates(root_store)
        .with_no_client_auth();
    
    Ok(Arc::new(config))
}

fn main() -> Result<(), Box<dyn Error>> {
    // Display key exchange information
    key_exchange_demo::display_supported_groups();
    
    // Run server
    run_pfs_server(8443, "server.crt", "server.key")?;
    
    Ok(())
}
```

## Advanced Rust Example: Session Key Inspection

```rust
use rustls::{ServerConnection, StreamOwned};
use std::sync::Arc;

// Demonstrate session key management
fn inspect_session_keys(conn: &ServerConnection) {
    // Note: rustls doesn't expose session keys directly (security by design)
    // but we can verify PFS properties
    
    if let Some(suite) = conn.negotiated_cipher_suite() {
        println!("\nSession Security Properties:");
        println!("  Algorithm: {:?}", suite.suite());
        
        // Check if using ephemeral key exchange
        let suite_name = format!("{:?}", suite.suite());
        let has_pfs = suite_name.contains("ECDHE") || suite_name.contains("DHE");
        
        if has_pfs {
            println!("  ✓ Ephemeral key exchange: YES");
            println!("  ✓ Forward secrecy: GUARANTEED");
            println!("  ✓ Session keys: Unique per connection");
            println!("  ✓ Key lifetime: Single session only");
        } else {
            println!("  ✗ Warning: Static key exchange detected!");
        }
    }
    
    if let Some(protocol) = conn.protocol_version() {
        println!("  Protocol: {:?}", protocol);
    }
}
```

## Summary

Perfect Forward Secrecy is a critical security property that protects past communications from future key compromise. By using ephemeral key pairs that are generated per-session and immediately destroyed, PFS ensures that:

- Each session has unique encryption keys
- Compromising the server's long-term private key doesn't expose past sessions
- Attackers cannot retroactively decrypt recorded traffic
- Sessions are cryptographically isolated from each other

Modern implementations use ECDHE (Elliptic Curve Diffie-Hellman Ephemeral) or DHE (Diffie-Hellman Ephemeral) for efficient ephemeral key exchange. TLS 1.2 and higher strongly encourage PFS, while TLS 1.3 mandates it by removing non-PFS cipher suites entirely.

In practice, enabling PFS in C/C++ requires configuring OpenSSL to use ECDHE or DHE cipher suites and properly managing the ephemeral keys. Rust's `rustls` library provides PFS by default with no additional configuration needed, making it an excellent choice for security-conscious applications. The key principle remains the same across all implementations: generate temporary keys, use them once, and destroy them immediately to prevent future compromise.