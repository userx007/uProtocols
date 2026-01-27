# Certificate Pinning for WebSocket Clients

Certificate pinning is a security technique that hardcodes or embeds specific server certificates or public keys into client applications. This prevents man-in-the-middle (MITM) attacks by ensuring the client only trusts explicitly pinned certificates, rather than relying solely on the standard certificate authority (CA) chain validation.

## Why Certificate Pinning Matters for WebSockets

WebSocket connections maintain long-lived, persistent connections that transmit sensitive data. Standard TLS/SSL validation relies on the CA system, but if an attacker compromises a CA or installs a rogue CA certificate on a device, they can intercept encrypted traffic. Certificate pinning adds an extra layer of defense by validating that the server's certificate matches what the application expects.

## Core Concepts

**Pin Types:**
- **Certificate Pinning**: Pins the entire certificate (expires when cert expires)
- **Public Key Pinning**: Pins only the public key (survives certificate rotation if key stays the same)
- **Hash Pinning**: Pins the hash of the certificate or public key (most common)

**Implementation Considerations:**
- Always include backup pins to handle certificate rotation
- Implement pin expiration and update mechanisms
- Handle pin validation failures gracefully
- Test thoroughly to avoid locking users out

## C/C++ Implementation

Using OpenSSL with WebSocket libraries:

```c
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

// Expected SHA-256 hash of the server's public key
const char* PINNED_PUBLIC_KEY_HASH = 
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// Function to extract and hash the public key
int verify_certificate_pin(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        fprintf(stderr, "No certificate presented by server\n");
        return 0;
    }
    
    // Extract public key
    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (!pkey) {
        X509_free(cert);
        return 0;
    }
    
    // Get DER-encoded public key
    unsigned char* der_key = NULL;
    int der_len = i2d_PUBKEY(pkey, &der_key);
    if (der_len < 0) {
        EVP_PKEY_free(pkey);
        X509_free(cert);
        return 0;
    }
    
    // Calculate SHA-256 hash
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, der_key, der_len);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    
    // Convert hash to hex string
    char hash_hex[65];
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(&hash_hex[i * 2], "%02x", hash[i]);
    }
    hash_hex[64] = '\0';
    
    // Compare with pinned hash
    int result = (strcmp(hash_hex, PINNED_PUBLIC_KEY_HASH) == 0);
    
    // Cleanup
    OPENSSL_free(der_key);
    EVP_PKEY_free(pkey);
    X509_free(cert);
    
    if (!result) {
        fprintf(stderr, "Certificate pin validation failed!\n");
        fprintf(stderr, "Expected: %s\n", PINNED_PUBLIC_KEY_HASH);
        fprintf(stderr, "Got: %s\n", hash_hex);
    }
    
    return result;
}

// SSL verification callback
int ssl_verify_callback(int preverify_ok, X509_STORE_CTX* ctx) {
    // Let standard validation complete first
    if (!preverify_ok) {
        return 0;
    }
    
    // Get SSL connection from context
    SSL* ssl = X509_STORE_CTX_get_ex_data(ctx, 
                    SSL_get_ex_data_X509_STORE_CTX_idx());
    
    // Perform certificate pinning check
    return verify_certificate_pin(ssl);
}

// WebSocket connection setup with pinning
SSL_CTX* create_pinned_ssl_context() {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        return NULL;
    }
    
    // Enable standard certificate verification
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, ssl_verify_callback);
    
    // Load system CA certificates
    SSL_CTX_set_default_verify_paths(ctx);
    
    return ctx;
}

// Example usage
int main() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    SSL_CTX* ctx = create_pinned_ssl_context();
    if (!ctx) {
        fprintf(stderr, "Failed to create SSL context\n");
        return 1;
    }
    
    // Create SSL connection
    SSL* ssl = SSL_new(ctx);
    
    // ... connect to WebSocket server ...
    // ... perform handshake ...
    
    // The pinning verification happens automatically during handshake
    
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    
    return 0;
}
```

## Rust Implementation

Using `tokio-tungstenite` with `rustls` and custom certificate verification:

```rust
use tokio_tungstenite::{connect_async, tungstenite::Error};
use tokio_tungstenite::tungstenite::client::IntoClientRequest;
use rustls::{ClientConfig, RootCertStore};
use rustls::client::{ServerCertVerifier, ServerCertVerified};
use rustls::Certificate;
use std::sync::Arc;
use std::time::SystemTime;
use sha2::{Sha256, Digest};
use webpki::DnsNameRef;

// Custom certificate verifier with pinning
struct PinnedCertVerifier {
    pinned_hashes: Vec<String>,
    root_store: RootCertStore,
}

impl PinnedCertVerifier {
    fn new(pinned_hashes: Vec<String>) -> Self {
        let mut root_store = RootCertStore::empty();
        // Add system root certificates
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
            pinned_hashes,
            root_store,
        }
    }
    
    fn verify_pin(&self, cert: &Certificate) -> bool {
        // Extract public key and compute hash
        let hash = Sha256::digest(&cert.0);
        let hash_hex = hex::encode(hash);
        
        self.pinned_hashes.contains(&hash_hex)
    }
}

impl ServerCertVerifier for PinnedCertVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &Certificate,
        intermediates: &[Certificate],
        server_name: &rustls::ServerName,
        scts: &mut dyn Iterator<Item = &[u8]>,
        ocsp_response: &[u8],
        now: SystemTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        // First, perform standard certificate validation
        let verifier = rustls::client::WebPkiVerifier::new(
            self.root_store.clone(),
            None
        );
        
        verifier.verify_server_cert(
            end_entity,
            intermediates,
            server_name,
            scts,
            ocsp_response,
            now,
        )?;
        
        // Then verify certificate pinning
        if !self.verify_pin(end_entity) {
            eprintln!("Certificate pin validation failed!");
            return Err(rustls::Error::InvalidCertificateData(
                "Certificate does not match pinned hash".to_string()
            ));
        }
        
        Ok(ServerCertVerified::assertion())
    }
}

// Create WebSocket connection with certificate pinning
async fn connect_with_pinning(
    url: &str,
    pinned_hashes: Vec<String>
) -> Result<(), Box<dyn std::error::Error>> {
    // Create custom TLS config with pinning
    let verifier = Arc::new(PinnedCertVerifier::new(pinned_hashes));
    
    let mut tls_config = ClientConfig::builder()
        .with_safe_defaults()
        .with_custom_certificate_verifier(verifier)
        .with_no_client_auth();
    
    // Create connector with custom TLS config
    let connector = tokio_tungstenite::Connector::Rustls(Arc::new(tls_config));
    
    // Prepare request
    let request = url.into_client_request()?;
    
    // Connect with pinning verification
    let (ws_stream, response) = connect_async_with_config(
        request,
        None,
        false,
        Some(connector)
    ).await?;
    
    println!("Connected successfully with pinned certificate");
    println!("Response status: {}", response.status());
    
    // Use WebSocket connection...
    
    Ok(())
}

// Helper function with custom connector
async fn connect_async_with_config(
    request: tokio_tungstenite::tungstenite::handshake::client::Request,
    config: Option<tokio_tungstenite::tungstenite::protocol::WebSocketConfig>,
    disable_nagle: bool,
    connector: Option<tokio_tungstenite::Connector>,
) -> Result
    (
        tokio_tungstenite::WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
        tokio_tungstenite::tungstenite::handshake::client::Response
    ),
    Error
> {
    use tokio_tungstenite::connect_async_tls_with_config;
    connect_async_tls_with_config(request, config, disable_nagle, connector).await
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Example: Pin multiple certificates (primary + backup)
    let pinned_hashes = vec![
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".to_string(),
        // Backup pin for certificate rotation
        "a1b2c3d4e5f6789012345678901234567890123456789012345678901234567890".to_string(),
    ];
    
    let url = "wss://secure.example.com/ws";
    
    match connect_with_pinning(url, pinned_hashes).await {
        Ok(_) => println!("Connection successful"),
        Err(e) => eprintln!("Connection failed: {}", e),
    }
    
    Ok(())
}
```

## Advanced Rust Example with Multiple Pin Support

```rust
use std::collections::HashSet;

struct CertificatePinConfig {
    // Primary pins (current certificates)
    primary_pins: HashSet<String>,
    // Backup pins (for rotation)
    backup_pins: HashSet<String>,
    // Pin expiration timestamp
    expires_at: Option<SystemTime>,
}

impl CertificatePinConfig {
    fn new() -> Self {
        Self {
            primary_pins: HashSet::new(),
            backup_pins: HashSet::new(),
            expires_at: None,
        }
    }
    
    fn add_primary_pin(&mut self, hash: String) {
        self.primary_pins.insert(hash);
    }
    
    fn add_backup_pin(&mut self, hash: String) {
        self.backup_pins.insert(hash);
    }
    
    fn is_expired(&self) -> bool {
        if let Some(expires) = self.expires_at {
            SystemTime::now() > expires
        } else {
            false
        }
    }
    
    fn verify_hash(&self, hash: &str) -> bool {
        if self.is_expired() {
            eprintln!("Warning: Certificate pins have expired");
            return false;
        }
        
        self.primary_pins.contains(hash) || self.backup_pins.contains(hash)
    }
}

// Usage example
fn create_pin_config() -> CertificatePinConfig {
    let mut config = CertificatePinConfig::new();
    
    // Add primary certificate pins
    config.add_primary_pin(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".to_string()
    );
    
    // Add backup pins for certificate rotation
    config.add_backup_pin(
        "a1b2c3d4e5f6789012345678901234567890123456789012345678901234567890".to_string()
    );
    
    // Set expiration (e.g., 90 days from now)
    config.expires_at = Some(
        SystemTime::now() + std::time::Duration::from_secs(90 * 24 * 60 * 60)
    );
    
    config
}
```

## Summary

Certificate pinning enhances WebSocket security by validating that the server presents an expected certificate or public key. In C/C++, this involves custom OpenSSL verification callbacks that check certificate hashes during the TLS handshake. In Rust, you implement custom `ServerCertVerifier` traits with `rustls` to perform pin validation alongside standard certificate checks.

**Key implementation points:**
- Always pin public keys rather than full certificates when possible (survives rotation)
- Include multiple backup pins to handle certificate updates
- Perform standard CA validation first, then add pin verification
- Implement pin expiration and update mechanisms
- Use SHA-256 hashing for pin comparison
- Handle validation failures gracefully to avoid locking out users

Certificate pinning is particularly critical for mobile applications where WebSocket connections transmit sensitive data and the risk of compromised CAs or device-level attacks is higher.