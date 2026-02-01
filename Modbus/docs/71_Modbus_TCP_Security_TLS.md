# Modbus TCP Security (TLS)

## Overview

Modbus TCP Security with TLS addresses the fundamental security vulnerabilities in standard Modbus TCP protocol by adding encryption, authentication, and data integrity verification. Traditional Modbus TCP transmits data in plaintext over port 502, making it vulnerable to eavesdropping, man-in-the-middle attacks, and unauthorized access. Modbus TCP Security (also known as Modbus/TLS) wraps the standard Modbus TCP protocol within a TLS tunnel, typically operating on port 802.

## Key Concepts

### Security Enhancements

**Encryption**: All Modbus communication is encrypted using TLS, protecting against packet sniffing and data interception.

**Authentication**: TLS certificates verify both server and client identities, preventing unauthorized device access.

**Data Integrity**: Cryptographic checksums detect tampering or corruption during transmission.

**Forward Secrecy**: Modern TLS configurations support perfect forward secrecy, ensuring past sessions remain secure even if keys are compromised.

### Protocol Stack

```
┌─────────────────────────┐
│   Modbus Application    │
├─────────────────────────┤
│   Modbus TCP Protocol   │
├─────────────────────────┤
│   TLS/SSL Layer         │
├─────────────────────────┤
│   TCP Transport         │
├─────────────────────────┤
│   IP Network            │
└─────────────────────────┘
```

### Certificate Management

Proper certificate handling is critical. You'll need:
- CA (Certificate Authority) certificates for trust chains
- Server certificates for Modbus devices
- Client certificates for mutual authentication (optional but recommended)
- Private keys securely stored and protected

## C/C++ Implementation

Here's a comprehensive implementation using OpenSSL:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define MODBUS_TCP_DEFAULT_PORT 502
#define MODBUS_TLS_DEFAULT_PORT 802
#define MODBUS_TCP_HEADER_SIZE 6

// Modbus TCP Security Client
typedef struct {
    SSL_CTX *ssl_ctx;
    SSL *ssl;
    int socket;
    char server_ip[16];
    int server_port;
} modbus_tls_client_t;

// Initialize OpenSSL
void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

// Cleanup OpenSSL
void cleanup_openssl() {
    EVP_cleanup();
}

// Create SSL context with security settings
SSL_CTX* create_ssl_context(int is_server) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    if (is_server) {
        method = TLS_server_method();
    } else {
        method = TLS_client_method();
    }

    ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // Set minimum TLS version to 1.2
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // Configure cipher suites (strong ciphers only)
    SSL_CTX_set_cipher_list(ctx, 
        "ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-RSA-AES128-GCM-SHA256:"
        "DHE-RSA-AES256-GCM-SHA384");

    // Enable certificate verification
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

    return ctx;
}

// Load certificates for client
int configure_client_certificates(SSL_CTX *ctx, 
                                  const char *ca_cert_file,
                                  const char *client_cert_file,
                                  const char *client_key_file) {
    // Load CA certificate for server verification
    if (SSL_CTX_load_verify_locations(ctx, ca_cert_file, NULL) != 1) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // Load client certificate for mutual authentication
    if (client_cert_file && client_key_file) {
        if (SSL_CTX_use_certificate_file(ctx, client_cert_file, SSL_FILETYPE_PEM) != 1) {
            ERR_print_errors_fp(stderr);
            return -1;
        }

        if (SSL_CTX_use_PrivateKey_file(ctx, client_key_file, SSL_FILETYPE_PEM) != 1) {
            ERR_print_errors_fp(stderr);
            return -1;
        }

        // Verify private key matches certificate
        if (SSL_CTX_check_private_key(ctx) != 1) {
            fprintf(stderr, "Private key does not match certificate\n");
            return -1;
        }
    }

    return 0;
}

// Create Modbus TLS client
modbus_tls_client_t* modbus_tls_client_create(const char *server_ip, int port) {
    modbus_tls_client_t *client = malloc(sizeof(modbus_tls_client_t));
    if (!client) return NULL;

    strncpy(client->server_ip, server_ip, 15);
    client->server_ip[15] = '\0';
    client->server_port = port;
    client->ssl_ctx = NULL;
    client->ssl = NULL;
    client->socket = -1;

    return client;
}

// Connect to Modbus TLS server
int modbus_tls_connect(modbus_tls_client_t *client,
                       const char *ca_cert,
                       const char *client_cert,
                       const char *client_key) {
    struct sockaddr_in server_addr;

    // Create SSL context
    client->ssl_ctx = create_ssl_context(0);
    if (!client->ssl_ctx) return -1;

    // Configure certificates
    if (configure_client_certificates(client->ssl_ctx, ca_cert, 
                                      client_cert, client_key) != 0) {
        return -1;
    }

    // Create TCP socket
    client->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->server_port);
    inet_pton(AF_INET, client->server_ip, &server_addr.sin_addr);

    // Connect to server
    if (connect(client->socket, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client->socket);
        return -1;
    }

    // Create SSL structure
    client->ssl = SSL_new(client->ssl_ctx);
    SSL_set_fd(client->ssl, client->socket);

    // Perform TLS handshake
    if (SSL_connect(client->ssl) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(client->ssl);
        close(client->socket);
        return -1;
    }

    printf("TLS connection established\n");
    printf("Cipher: %s\n", SSL_get_cipher(client->ssl));

    // Verify server certificate
    X509 *cert = SSL_get_peer_certificate(client->ssl);
    if (cert) {
        long verify_result = SSL_get_verify_result(client->ssl);
        if (verify_result == X509_V_OK) {
            printf("Server certificate verified successfully\n");
        } else {
            printf("Certificate verification failed: %ld\n", verify_result);
            X509_free(cert);
            return -1;
        }
        X509_free(cert);
    } else {
        printf("No server certificate received\n");
        return -1;
    }

    return 0;
}

// Build Modbus TCP request
int build_modbus_request(uint8_t *buffer, uint16_t transaction_id,
                        uint8_t unit_id, uint8_t function_code,
                        uint16_t start_address, uint16_t quantity) {
    int idx = 0;

    // Transaction ID
    buffer[idx++] = (transaction_id >> 8) & 0xFF;
    buffer[idx++] = transaction_id & 0xFF;

    // Protocol ID (always 0 for Modbus)
    buffer[idx++] = 0x00;
    buffer[idx++] = 0x00;

    // Length (6 bytes following)
    buffer[idx++] = 0x00;
    buffer[idx++] = 0x06;

    // Unit ID
    buffer[idx++] = unit_id;

    // Function code
    buffer[idx++] = function_code;

    // Start address
    buffer[idx++] = (start_address >> 8) & 0xFF;
    buffer[idx++] = start_address & 0xFF;

    // Quantity
    buffer[idx++] = (quantity >> 8) & 0xFF;
    buffer[idx++] = quantity & 0xFF;

    return idx;
}

// Read holding registers over TLS
int modbus_tls_read_holding_registers(modbus_tls_client_t *client,
                                      uint8_t unit_id,
                                      uint16_t start_addr,
                                      uint16_t num_registers,
                                      uint16_t *output) {
    uint8_t request[12];
    uint8_t response[256];
    static uint16_t transaction_id = 0;
    int bytes_sent, bytes_received;

    // Build request
    int req_len = build_modbus_request(request, ++transaction_id, unit_id,
                                       0x03, start_addr, num_registers);

    // Send encrypted request
    bytes_sent = SSL_write(client->ssl, request, req_len);
    if (bytes_sent != req_len) {
        fprintf(stderr, "Failed to send request\n");
        return -1;
    }

    // Receive encrypted response
    bytes_received = SSL_read(client->ssl, response, sizeof(response));
    if (bytes_received < 9) {
        fprintf(stderr, "Invalid response size\n");
        return -1;
    }

    // Parse response
    uint8_t byte_count = response[8];
    if (byte_count != num_registers * 2) {
        fprintf(stderr, "Unexpected byte count\n");
        return -1;
    }

    // Extract register values
    for (int i = 0; i < num_registers; i++) {
        output[i] = (response[9 + i*2] << 8) | response[10 + i*2];
    }

    return num_registers;
}

// Cleanup
void modbus_tls_disconnect(modbus_tls_client_t *client) {
    if (client->ssl) {
        SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
    }
    if (client->socket >= 0) {
        close(client->socket);
    }
    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
    }
    free(client);
}

// Example usage
int main() {
    init_openssl();

    modbus_tls_client_t *client = modbus_tls_client_create("192.168.1.100", 802);
    
    if (modbus_tls_connect(client, 
                          "ca-cert.pem",
                          "client-cert.pem", 
                          "client-key.pem") == 0) {
        
        uint16_t registers[10];
        int count = modbus_tls_read_holding_registers(client, 1, 0, 10, registers);
        
        if (count > 0) {
            printf("Read %d registers:\n", count);
            for (int i = 0; i < count; i++) {
                printf("Register %d: %u\n", i, registers[i]);
            }
        }
        
        modbus_tls_disconnect(client);
    }

    cleanup_openssl();
    return 0;
}
```

## Rust Implementation

Here's a modern Rust implementation using `tokio-modbus` and `tokio-rustls`:

```rust
use tokio::net::TcpStream;
use tokio_rustls::{TlsConnector, rustls, webpki};
use tokio_modbus::prelude::*;
use std::sync::Arc;
use std::fs::File;
use std::io::BufReader;
use rustls_pemfile::{certs, rsa_private_keys};

/// Modbus TLS Client
pub struct ModbusTlsClient {
    context: Option<client::Context>,
}

impl ModbusTlsClient {
    /// Create new Modbus TLS client
    pub fn new() -> Self {
        ModbusTlsClient { context: None }
    }

    /// Load TLS configuration
    fn load_tls_config(
        ca_cert_path: &str,
        client_cert_path: &str,
        client_key_path: &str,
    ) -> Result<rustls::ClientConfig, Box<dyn std::error::Error>> {
        // Load CA certificate
        let ca_cert_file = File::open(ca_cert_path)?;
        let mut ca_cert_reader = BufReader::new(ca_cert_file);
        let ca_certs = certs(&mut ca_cert_reader)?;

        // Create root certificate store
        let mut root_store = rustls::RootCertStore::empty();
        for cert in ca_certs {
            root_store.add(&rustls::Certificate(cert))?;
        }

        // Load client certificate
        let client_cert_file = File::open(client_cert_path)?;
        let mut client_cert_reader = BufReader::new(client_cert_file);
        let client_certs: Vec<rustls::Certificate> = certs(&mut client_cert_reader)?
            .into_iter()
            .map(rustls::Certificate)
            .collect();

        // Load client private key
        let client_key_file = File::open(client_key_path)?;
        let mut client_key_reader = BufReader::new(client_key_file);
        let mut keys = rsa_private_keys(&mut client_key_reader)?;
        
        if keys.is_empty() {
            return Err("No private keys found".into());
        }
        let client_key = rustls::PrivateKey(keys.remove(0));

        // Build TLS configuration
        let config = rustls::ClientConfig::builder()
            .with_safe_default_cipher_suites()
            .with_safe_default_kx_groups()
            .with_protocol_versions(&[&rustls::version::TLS12, &rustls::version::TLS13])?
            .with_root_certificates(root_store)
            .with_client_auth_cert(client_certs, client_key)?;

        Ok(config)
    }

    /// Connect to Modbus TLS server
    pub async fn connect(
        &mut self,
        server_addr: &str,
        server_name: &str,
        ca_cert: &str,
        client_cert: &str,
        client_key: &str,
    ) -> Result<(), Box<dyn std::error::Error>> {
        // Load TLS configuration
        let tls_config = Self::load_tls_config(ca_cert, client_cert, client_key)?;
        let tls_connector = TlsConnector::from(Arc::new(tls_config));

        // Create TCP connection
        let tcp_stream = TcpStream::connect(server_addr).await?;
        println!("TCP connection established to {}", server_addr);

        // Perform TLS handshake
        let domain = rustls::ServerName::try_from(server_name)?;
        let tls_stream = tls_connector.connect(domain, tcp_stream).await?;
        println!("TLS handshake completed");

        // Get negotiated cipher suite
        let (_, connection) = tls_stream.get_ref();
        if let Some(cipher_suite) = connection.negotiated_cipher_suite() {
            println!("Cipher suite: {:?}", cipher_suite.suite());
        }

        // Create Modbus client context
        self.context = Some(client::Context::new(tls_stream));
        println!("Modbus TLS client ready");

        Ok(())
    }

    /// Read holding registers
    pub async fn read_holding_registers(
        &mut self,
        address: u16,
        count: u16,
    ) -> Result<Vec<u16>, Box<dyn std::error::Error>> {
        if let Some(ctx) = &mut self.context {
            let result = ctx.read_holding_registers(address, count).await?;
            Ok(result)
        } else {
            Err("Not connected".into())
        }
    }

    /// Write single register
    pub async fn write_single_register(
        &mut self,
        address: u16,
        value: u16,
    ) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(ctx) = &mut self.context {
            ctx.write_single_register(address, value).await?;
            Ok(())
        } else {
            Err("Not connected".into())
        }
    }

    /// Write multiple registers
    pub async fn write_multiple_registers(
        &mut self,
        address: u16,
        values: &[u16],
    ) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(ctx) = &mut self.context {
            ctx.write_multiple_registers(address, values).await?;
            Ok(())
        } else {
            Err("Not connected".into())
        }
    }

    /// Read input registers
    pub async fn read_input_registers(
        &mut self,
        address: u16,
        count: u16,
    ) -> Result<Vec<u16>, Box<dyn std::error::Error>> {
        if let Some(ctx) = &mut self.context {
            let result = ctx.read_input_registers(address, count).await?;
            Ok(result)
        } else {
            Err("Not connected".into())
        }
    }

    /// Disconnect from server
    pub async fn disconnect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(_ctx) = self.context.take() {
            println!("Disconnected from Modbus TLS server");
        }
        Ok(())
    }
}

/// Example: Modbus TLS Server
pub struct ModbusTlsServer {
    // Server implementation would go here
}

impl ModbusTlsServer {
    /// Load server TLS configuration
    fn load_server_tls_config(
        cert_path: &str,
        key_path: &str,
        ca_cert_path: &str,
    ) -> Result<rustls::ServerConfig, Box<dyn std::error::Error>> {
        // Load server certificate
        let cert_file = File::open(cert_path)?;
        let mut cert_reader = BufReader::new(cert_file);
        let certs: Vec<rustls::Certificate> = certs(&mut cert_reader)?
            .into_iter()
            .map(rustls::Certificate)
            .collect();

        // Load private key
        let key_file = File::open(key_path)?;
        let mut key_reader = BufReader::new(key_file);
        let mut keys = rsa_private_keys(&mut key_reader)?;
        
        if keys.is_empty() {
            return Err("No private keys found".into());
        }
        let key = rustls::PrivateKey(keys.remove(0));

        // Load CA certificate for client verification
        let ca_file = File::open(ca_cert_path)?;
        let mut ca_reader = BufReader::new(ca_file);
        let ca_certs = certs(&mut ca_reader)?;

        let mut client_cert_verifier = rustls::RootCertStore::empty();
        for cert in ca_certs {
            client_cert_verifier.add(&rustls::Certificate(cert))?;
        }

        // Build server config with client authentication
        let client_verifier = rustls::server::AllowAnyAuthenticatedClient::new(
            client_cert_verifier
        );

        let config = rustls::ServerConfig::builder()
            .with_safe_default_cipher_suites()
            .with_safe_default_kx_groups()
            .with_protocol_versions(&[&rustls::version::TLS12, &rustls::version::TLS13])?
            .with_client_cert_verifier(Arc::new(client_verifier))
            .with_single_cert(certs, key)?;

        Ok(config)
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = ModbusTlsClient::new();

    // Connect to Modbus TLS server
    client.connect(
        "192.168.1.100:802",
        "modbus-server.local",
        "ca-cert.pem",
        "client-cert.pem",
        "client-key.pem",
    ).await?;

    // Read holding registers
    match client.read_holding_registers(0, 10).await {
        Ok(registers) => {
            println!("Read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("Register {}: {}", i, value);
            }
        }
        Err(e) => eprintln!("Error reading registers: {}", e),
    }

    // Write single register
    client.write_single_register(100, 42).await?;
    println!("Wrote value 42 to register 100");

    // Write multiple registers
    let values = vec![100, 200, 300, 400];
    client.write_multiple_registers(200, &values).await?;
    println!("Wrote {} values starting at register 200", values.len());

    // Disconnect
    client.disconnect().await?;

    Ok(())
}
```

## Summary

Modbus TCP Security with TLS provides essential protection for industrial control systems by encrypting all Modbus communications, authenticating devices through certificates, and ensuring data integrity. The implementation wraps standard Modbus TCP frames within a TLS tunnel, typically on port 802, while maintaining complete compatibility with the Modbus application layer.

Key implementation aspects include proper certificate management with CA-signed certificates for production environments, mutual TLS authentication for both client and server verification, and configuration to use only strong cipher suites with minimum TLS 1.2 or preferably TLS 1.3. Both the C/C++ implementation using OpenSSL and the Rust implementation using tokio-rustls demonstrate production-ready patterns including error handling, certificate validation, and secure connection establishment.

This security enhancement is critical for SCADA systems, industrial IoT deployments, and any Modbus TCP installation exposed to untrusted networks, addressing the protocol's original lack of built-in security while preserving its simplicity and widespread compatibility.