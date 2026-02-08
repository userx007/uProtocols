# Certificate Pinning

## Detailed Description

Certificate pinning is a security technique that protects applications from man-in-the-middle (MITM) attacks by validating that the server's SSL/TLS certificate matches a known, trusted certificate or public key. Instead of relying solely on the standard certificate authority (CA) trust chain, certificate pinning "pins" specific certificates or public keys that the application expects to see.

### Why Certificate Pinning Matters

In a standard TLS connection, the client validates the server's certificate by checking:
1. The certificate is signed by a trusted CA
2. The certificate hasn't expired
3. The certificate's domain matches the requested domain

However, this trust model has vulnerabilities:
- **Compromised CAs**: If a CA is compromised, attackers can issue fraudulent certificates
- **Government interference**: Some governments force CAs to issue certificates for surveillance
- **Corporate proxies**: Some corporate environments use MITM proxies with custom CA certificates
- **Rogue certificates**: Attackers who compromise a CA can intercept encrypted traffic

Certificate pinning mitigates these risks by adding an additional validation layer: the application only trusts specific certificates or public keys that it has been explicitly configured to trust.

### Types of Certificate Pinning

1. **Certificate Pinning**: Pin the entire certificate (including public key and metadata)
   - More secure but requires updates when certificates are rotated
   - Recommended to pin multiple certificates (current + backup)

2. **Public Key Pinning**: Pin only the public key
   - More flexible during certificate rotation if the key remains the same
   - Allows certificate renewal without app updates

3. **Subject Public Key Info (SPKI) Pinning**: Pin the hash of the public key
   - Most common approach
   - Used by HTTP Public Key Pinning (HPKP) standard

### Implementation Strategies

- **Pin multiple keys**: Always pin backup keys to avoid service disruption if primary certificate needs emergency rotation
- **Implement pin expiration**: Include mechanisms to update pins without app updates
- **Report violations**: Log pinning failures for security monitoring
- **Graceful degradation**: Consider fallback strategies for pin validation failures

---

## C/C++ Implementation

### Using OpenSSL for Certificate Pinning

```c
#include <stdio.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

// Store the expected SHA256 hash of the public key (SPKI)
const char *EXPECTED_SPKI_HASH = 
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

/**
 * Extract and hash the public key from the certificate
 * Returns 1 on success, 0 on failure
 */
int get_spki_hash(X509 *cert, unsigned char *hash_out) {
    EVP_PKEY *pubkey = X509_get_pubkey(cert);
    if (!pubkey) {
        fprintf(stderr, "Failed to extract public key\n");
        return 0;
    }
    
    // Get the DER-encoded public key
    unsigned char *pubkey_der = NULL;
    int pubkey_len = i2d_PUBKEY(pubkey, &pubkey_der);
    
    if (pubkey_len <= 0) {
        fprintf(stderr, "Failed to encode public key\n");
        EVP_PKEY_free(pubkey);
        return 0;
    }
    
    // Calculate SHA256 hash
    SHA256(pubkey_der, pubkey_len, hash_out);
    
    // Cleanup
    OPENSSL_free(pubkey_der);
    EVP_PKEY_free(pubkey);
    
    return 1;
}

/**
 * Convert hash to hex string
 */
void hash_to_hex(const unsigned char *hash, size_t len, char *hex_out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_out + (i * 2), "%02x", hash[i]);
    }
    hex_out[len * 2] = '\0';
}

/**
 * Certificate verification callback with pinning
 */
int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
    // Get the certificate being verified
    X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
    int depth = X509_STORE_CTX_get_error_depth(ctx);
    
    // Only check the leaf certificate (depth 0)
    if (depth == 0) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        char hex_hash[SHA256_DIGEST_LENGTH * 2 + 1];
        
        if (!get_spki_hash(cert, hash)) {
            fprintf(stderr, "Failed to get certificate hash\n");
            return 0;
        }
        
        hash_to_hex(hash, SHA256_DIGEST_LENGTH, hex_hash);
        
        printf("Certificate SPKI Hash: %s\n", hex_hash);
        printf("Expected SPKI Hash:    %s\n", EXPECTED_SPKI_HASH);
        
        // Compare with expected hash
        if (strcmp(hex_hash, EXPECTED_SPKI_HASH) != 0) {
            fprintf(stderr, "Certificate pinning failed! Hash mismatch.\n");
            return 0;
        }
        
        printf("Certificate pinning successful!\n");
    }
    
    return preverify_ok;
}

/**
 * Create SSL connection with certificate pinning
 */
int connect_with_pinning(const char *hostname, int port) {
    SSL_CTX *ctx;
    SSL *ssl;
    int sock;
    struct sockaddr_in addr;
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create SSL context
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        fprintf(stderr, "Failed to create SSL context\n");
        return -1;
    }
    
    // Load default CA certificates
    SSL_CTX_set_default_verify_paths(ctx);
    
    // Set verification mode and callback
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Setup address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // Convert hostname to IP (simplified - should use getaddrinfo)
    if (inet_pton(AF_INET, hostname, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address\n");
        close(sock);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Connect
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Create SSL object
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, hostname);
    
    // Perform SSL handshake
    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "SSL handshake failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(sock);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    printf("SSL connection established with pinning verification\n");
    
    // Send HTTP request
    const char *request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    SSL_write(ssl, request, strlen(request));
    
    // Read response
    char buffer[4096];
    int bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Received: %s\n", buffer);
    }
    
    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    
    return 0;
}

int main() {
    // Example usage
    connect_with_pinning("93.184.216.34", 443); // example.com IP
    return 0;
}
```

### C++ Modern Approach with RAII

```cpp
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/sha.h>

class CertificatePin {
private:
    std::vector<std::string> pinnedHashes;
    
public:
    CertificatePin(std::vector<std::string> hashes) 
        : pinnedHashes(std::move(hashes)) {}
    
    bool verify(X509* cert) const {
        auto hash = getPublicKeyHash(cert);
        if (hash.empty()) {
            return false;
        }
        
        // Check if hash matches any pinned hash
        for (const auto& pinnedHash : pinnedHashes) {
            if (hash == pinnedHash) {
                std::cout << "Certificate pin matched: " << hash << std::endl;
                return true;
            }
        }
        
        std::cerr << "Certificate pin mismatch! Got: " << hash << std::endl;
        return false;
    }
    
private:
    std::string getPublicKeyHash(X509* cert) const {
        EVP_PKEY* pubkey = X509_get_pubkey(cert);
        if (!pubkey) {
            return "";
        }
        
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> 
            pubkey_guard(pubkey, EVP_PKEY_free);
        
        unsigned char* der = nullptr;
        int der_len = i2d_PUBKEY(pubkey, &der);
        
        if (der_len <= 0) {
            return "";
        }
        
        std::unique_ptr<unsigned char, decltype(&OPENSSL_free)> 
            der_guard(der, OPENSSL_free);
        
        std::array<unsigned char, SHA256_DIGEST_LENGTH> hash;
        SHA256(der, der_len, hash.data());
        
        return hashToHex(hash.data(), hash.size());
    }
    
    std::string hashToHex(const unsigned char* data, size_t len) const {
        std::string result;
        result.reserve(len * 2);
        
        const char* hex = "0123456789abcdef";
        for (size_t i = 0; i < len; ++i) {
            result.push_back(hex[data[i] >> 4]);
            result.push_back(hex[data[i] & 0x0f]);
        }
        
        return result;
    }
};

class SSLConnection {
private:
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl;
    CertificatePin pinValidator;
    int sock;
    
public:
    SSLConnection(std::vector<std::string> pins)
        : ctx(nullptr, SSL_CTX_free)
        , ssl(nullptr, SSL_free)
        , pinValidator(std::move(pins))
        , sock(-1) {
        
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }
    
    ~SSLConnection() {
        if (sock >= 0) {
            close(sock);
        }
    }
    
    bool connect(const std::string& host, int port) {
        // Create SSL context
        ctx.reset(SSL_CTX_new(TLS_client_method()));
        if (!ctx) {
            std::cerr << "Failed to create SSL context" << std::endl;
            return false;
        }
        
        SSL_CTX_set_default_verify_paths(ctx.get());
        
        // Set custom verification callback
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, 
            [](int preverify_ok, X509_STORE_CTX* store_ctx) -> int {
                // Get the SSLConnection instance (in real code, use SSL_CTX_set_cert_verify_callback)
                X509* cert = X509_STORE_CTX_get_current_cert(store_ctx);
                int depth = X509_STORE_CTX_get_error_depth(store_ctx);
                
                // Note: In production, you'd pass the CertificatePin instance through SSL_CTX
                return preverify_ok;
            });
        
        // Create and connect socket (simplified)
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // ... socket connection code ...
        
        // Create SSL connection
        ssl.reset(SSL_new(ctx.get()));
        SSL_set_fd(ssl.get(), sock);
        SSL_set_tlsext_host_name(ssl.get(), host.c_str());
        
        if (SSL_connect(ssl.get()) != 1) {
            std::cerr << "SSL handshake failed" << std::endl;
            ERR_print_errors_fp(stderr);
            return false;
        }
        
        // Verify certificate pinning
        X509* cert = SSL_get_peer_certificate(ssl.get());
        if (!cert) {
            std::cerr << "No peer certificate" << std::endl;
            return false;
        }
        
        std::unique_ptr<X509, decltype(&X509_free)> cert_guard(cert, X509_free);
        
        if (!pinValidator.verify(cert)) {
            std::cerr << "Certificate pinning validation failed" << std::endl;
            return false;
        }
        
        std::cout << "Secure connection established with certificate pinning" << std::endl;
        return true;
    }
    
    void sendRequest(const std::string& request) {
        if (ssl) {
            SSL_write(ssl.get(), request.c_str(), request.length());
        }
    }
    
    std::string readResponse() {
        if (!ssl) return "";
        
        char buffer[4096];
        int bytes = SSL_read(ssl.get(), buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            return std::string(buffer);
        }
        return "";
    }
};

int main() {
    // Pin multiple certificates for backup
    std::vector<std::string> pins = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "backup_hash_here_for_redundancy"
    };
    
    SSLConnection conn(pins);
    
    if (conn.connect("example.com", 443)) {
        conn.sendRequest("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
        std::cout << conn.readResponse() << std::endl;
    }
    
    return 0;
}
```

---

## Rust Implementation

Rust provides excellent TLS libraries with built-in support for certificate pinning through the `rustls` and `native-tls` crates.

### Using `rustls` with Custom Certificate Verification

```rust
use rustls::{ClientConfig, RootCertStore, ServerName};
use rustls::client::{ServerCertVerifier, ServerCertVerified};
use std::sync::Arc;
use std::io::{Read, Write};
use std::net::TcpStream;
use rustls::StreamOwned;
use sha2::{Sha256, Digest};
use x509_parser::prelude::*;

/// Custom certificate verifier that implements pinning
struct PinningVerifier {
    pinned_spki_hashes: Vec<String>,
    root_store: RootCertStore,
}

impl PinningVerifier {
    fn new(pinned_hashes: Vec<String>) -> Self {
        let mut root_store = RootCertStore::empty();
        
        // Load native root certificates
        root_store.add_server_trust_anchors(
            webpki_roots::TLS_SERVER_ROOTS.0.iter().map(|ta| {
                rustls::OwnedTrustAnchor::from_subject_spki_name_constraints(
                    ta.subject,
                    ta.spki,
                    ta.name_constraints,
                )
            })
        );
        
        Self {
            pinned_spki_hashes: pinned_hashes,
            root_store,
        }
    }
    
    fn extract_spki_hash(&self, cert_der: &[u8]) -> Result<String, Box<dyn std::error::Error>> {
        let (_, cert) = X509Certificate::from_der(cert_der)?;
        let spki = cert.public_key();
        
        let mut hasher = Sha256::new();
        hasher.update(spki.raw);
        let hash = hasher.finalize();
        
        Ok(hex::encode(hash))
    }
}

impl ServerCertVerifier for PinningVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &rustls::Certificate,
        intermediates: &[rustls::Certificate],
        server_name: &ServerName,
        scts: &mut dyn Iterator<Item = &[u8]>,
        ocsp_response: &[u8],
        now: std::time::SystemTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        // First, perform standard certificate verification
        let webpki_verifier = rustls::client::WebPkiVerifier::new(
            self.root_store.clone(),
            None,
        );
        
        webpki_verifier.verify_server_cert(
            end_entity,
            intermediates,
            server_name,
            scts,
            ocsp_response,
            now,
        )?;
        
        // Now perform pin verification
        let cert_hash = self.extract_spki_hash(&end_entity.0)
            .map_err(|e| rustls::Error::General(format!("Failed to extract hash: {}", e)))?;
        
        println!("Certificate SPKI Hash: {}", cert_hash);
        
        // Check if the hash matches any pinned hash
        if self.pinned_spki_hashes.iter().any(|pin| pin == &cert_hash) {
            println!("Certificate pin matched!");
            Ok(ServerCertVerified::assertion())
        } else {
            eprintln!("Certificate pinning failed! Hash not in pinned set.");
            Err(rustls::Error::General("Certificate pin mismatch".to_string()))
        }
    }
}

/// Connect to a server with certificate pinning
fn connect_with_pinning(
    host: &str,
    port: u16,
    pinned_hashes: Vec<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    // Create custom verifier
    let verifier = Arc::new(PinningVerifier::new(pinned_hashes));
    
    // Build TLS configuration
    let config = ClientConfig::builder()
        .with_safe_defaults()
        .with_custom_certificate_verifier(verifier)
        .with_no_client_auth();
    
    let connector = Arc::new(config);
    
    // Connect to server
    let addr = format!("{}:{}", host, port);
    let tcp_stream = TcpStream::connect(&addr)?;
    
    let server_name = ServerName::try_from(host)
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid hostname"))?;
    
    let mut tls_stream = StreamOwned::new(
        rustls::ClientConnection::new(connector, server_name)?,
        tcp_stream,
    );
    
    println!("TLS connection established with certificate pinning");
    
    // Send HTTP request
    let request = format!(
        "GET / HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        host
    );
    tls_stream.write_all(request.as_bytes())?;
    
    // Read response
    let mut response = String::new();
    tls_stream.read_to_string(&mut response)?;
    
    println!("Response received:\n{}", response);
    
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Pin multiple certificates for redundancy
    let pinned_hashes = vec![
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".to_string(),
        "backup_hash_for_redundancy".to_string(),
    ];
    
    connect_with_pinning("example.com", 443, pinned_hashes)?;
    
    Ok(())
}
```

### Simpler Approach Using `reqwest` with Custom Certificate Validation

```rust
use reqwest::blocking::Client;
use reqwest::Certificate;
use std::fs;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Load the pinned certificate
    let cert_pem = fs::read("pinned_cert.pem")?;
    let cert = Certificate::from_pem(&cert_pem)?;
    
    // Build client with pinned certificate
    let client = Client::builder()
        .add_root_certificate(cert)
        .build()?;
    
    // Make request
    let response = client
        .get("https://example.com")
        .send()?;
    
    println!("Status: {}", response.status());
    println!("Body: {}", response.text()?);
    
    Ok(())
}
```

### Production-Ready Rust Implementation with Multiple Pins

```rust
use rustls::{ClientConfig, RootCertStore};
use std::sync::Arc;
use sha2::{Sha256, Digest};
use hex;

#[derive(Clone)]
pub struct CertificatePinner {
    primary_pins: Vec<String>,
    backup_pins: Vec<String>,
}

impl CertificatePinner {
    pub fn new() -> Self {
        Self {
            primary_pins: Vec::new(),
            backup_pins: Vec::new(),
        }
    }
    
    pub fn add_primary_pin(mut self, hash: String) -> Self {
        self.primary_pins.push(hash);
        self
    }
    
    pub fn add_backup_pin(mut self, hash: String) -> Self {
        self.backup_pins.push(hash);
        self
    }
    
    pub fn verify(&self, cert_der: &[u8]) -> bool {
        match self.compute_spki_hash(cert_der) {
            Ok(hash) => {
                self.primary_pins.contains(&hash) || self.backup_pins.contains(&hash)
            }
            Err(e) => {
                eprintln!("Failed to compute certificate hash: {}", e);
                false
            }
        }
    }
    
    fn compute_spki_hash(&self, cert_der: &[u8]) -> Result<String, Box<dyn std::error::Error>> {
        use x509_parser::prelude::*;
        
        let (_, cert) = X509Certificate::from_der(cert_der)?;
        let spki = cert.public_key();
        
        let mut hasher = Sha256::new();
        hasher.update(spki.raw);
        let result = hasher.finalize();
        
        Ok(hex::encode(result))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_certificate_pinning() {
        let pinner = CertificatePinner::new()
            .add_primary_pin("abc123".to_string())
            .add_backup_pin("def456".to_string());
        
        assert_eq!(pinner.primary_pins.len(), 1);
        assert_eq!(pinner.backup_pins.len(), 1);
    }
}
```

---

## Summary

**Certificate Pinning** is a critical security mechanism that protects against man-in-the-middle attacks by validating that server certificates match expected, pre-configured values. Rather than trusting any certificate signed by a recognized Certificate Authority, pinning ensures only specific certificates or public keys are accepted.

### Key Takeaways:

1. **Protection Level**: Defends against compromised CAs, rogue certificates, and MITM attacks that bypass standard TLS validation

2. **Implementation Types**:
   - Full certificate pinning (most restrictive)
   - Public key pinning (more flexible)
   - SPKI hash pinning (most common, used in production)

3. **Best Practices**:
   - Always pin multiple certificates (primary + backup) to prevent service disruption during certificate rotation
   - Use public key pinning over full certificate pinning for easier rotation
   - Implement pin expiration and update mechanisms
   - Log pinning failures for security monitoring
   - Test thoroughly before production deployment

4. **Implementation Considerations**:
   - **C/C++**: Requires manual implementation using OpenSSL's verification callbacks
   - **Rust**: Better supported through `rustls` with custom verifiers and strong type safety
   - Both languages benefit from hashing the Subject Public Key Info (SPKI) rather than entire certificates

5. **Trade-offs**:
   - **Security**: Significantly improved protection against sophisticated attacks
   - **Maintenance**: Requires careful certificate rotation planning and app updates
   - **Risk**: Improper implementation can cause service outages if pins expire or certificates rotate unexpectedly

Certificate pinning is essential for high-security applications (banking, healthcare, government) where trust in the public CA infrastructure is insufficient, but requires careful operational planning to avoid service disruptions.