# Certificate Validation in TCP/IP

## Overview

Certificate validation is a critical security mechanism in TLS/SSL connections that ensures you're communicating with the legitimate server you intended to reach, not an attacker performing a man-in-the-middle attack. Proper certificate validation involves two key components:

1. **Certificate Chain Validation**: Verifying that the server's certificate is signed by a trusted Certificate Authority (CA) and that the entire chain of trust is valid
2. **Hostname Verification**: Ensuring the certificate was issued for the domain name you're connecting to

Without proper validation, encrypted connections provide no real security against active attackers.

## Certificate Chain Validation

When a server presents its certificate during the TLS handshake, the client must verify:

- The certificate is signed by a trusted CA (or a chain leading to one)
- The certificate hasn't expired
- The certificate hasn't been revoked
- Each certificate in the chain is valid and properly signed
- The signature algorithms used are secure

The validation process walks up the certificate chain from the server's certificate (leaf) through intermediate certificates to a root CA certificate that the client trusts.

## Hostname Verification

Even if a certificate is validly signed by a trusted CA, you must verify it was issued for the domain you're connecting to. The client checks that the hostname matches either:

- The Common Name (CN) in the certificate's Subject field
- One of the Subject Alternative Names (SAN) in the certificate

Modern certificates primarily use SANs, which support wildcards (e.g., `*.example.com`) and multiple domains.

## C/C++ Implementation with OpenSSL

```c
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>

// Custom hostname verification (OpenSSL < 1.0.2)
int verify_hostname(const char *hostname, X509 *cert) {
    int result = 0;
    
    // Check Subject Alternative Names (SAN) first
    GENERAL_NAMES *san_names = (GENERAL_NAMES *)X509_get_ext_d2i(
        cert, NID_subject_alt_name, NULL, NULL
    );
    
    if (san_names) {
        int san_count = sk_GENERAL_NAME_num(san_names);
        for (int i = 0; i < san_count && !result; i++) {
            const GENERAL_NAME *name = sk_GENERAL_NAME_value(san_names, i);
            
            if (name->type == GEN_DNS) {
                const char *dns_name = (const char *)ASN1_STRING_get0_data(name->d.dNSName);
                size_t dns_len = ASN1_STRING_length(name->d.dNSName);
                
                // Simple comparison (production code should handle wildcards)
                if (strlen(hostname) == dns_len && 
                    strncasecmp(hostname, dns_name, dns_len) == 0) {
                    result = 1;
                }
            }
        }
        GENERAL_NAMES_free(san_names);
    }
    
    // Fall back to Common Name if no SAN match
    if (!result) {
        X509_NAME *subject = X509_get_subject_name(cert);
        int cn_pos = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
        
        if (cn_pos >= 0) {
            X509_NAME_ENTRY *cn_entry = X509_NAME_get_entry(subject, cn_pos);
            ASN1_STRING *cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
            const char *cn = (const char *)ASN1_STRING_get0_data(cn_asn1);
            
            if (strcasecmp(hostname, cn) == 0) {
                result = 1;
            }
        }
    }
    
    return result;
}

// Verification callback for certificate chain
int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
    if (!preverify_ok) {
        int err = X509_STORE_CTX_get_error(ctx);
        int depth = X509_STORE_CTX_get_error_depth(ctx);
        
        fprintf(stderr, "Certificate verification failed at depth %d: %s\n",
                depth, X509_verify_cert_error_string(err));
    }
    return preverify_ok;
}

int secure_connect(const char *hostname, int port) {
    SSL_CTX *ctx;
    SSL *ssl;
    int sockfd;
    struct hostent *host;
    struct sockaddr_in addr;
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create SSL context
    const SSL_METHOD *method = TLS_client_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    
    // Load default trusted CA certificates
    if (!SSL_CTX_set_default_verify_paths(ctx)) {
        fprintf(stderr, "Failed to load CA certificates\n");
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Set verification mode - require valid certificate
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
    
    // Set minimum TLS version
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Resolve hostname
    host = gethostbyname(hostname);
    if (!host) {
        fprintf(stderr, "Failed to resolve hostname\n");
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Connect to server
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Create SSL connection
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    
    // Enable hostname verification (OpenSSL 1.0.2+)
    #if OPENSSL_VERSION_NUMBER >= 0x10002000L
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    X509_VERIFY_PARAM_set1_host(param, hostname, 0);
    #else
    // For older OpenSSL, we'll verify manually after handshake
    #endif
    
    // Perform TLS handshake
    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "SSL handshake failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    // Manual hostname verification for older OpenSSL
    #if OPENSSL_VERSION_NUMBER < 0x10002000L
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        fprintf(stderr, "No certificate presented by server\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    if (!verify_hostname(hostname, cert)) {
        fprintf(stderr, "Hostname verification failed\n");
        X509_free(cert);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    X509_free(cert);
    #endif
    
    // Verify the result of chain validation
    long verify_result = SSL_get_verify_result(ssl);
    if (verify_result != X509_V_OK) {
        fprintf(stderr, "Certificate verification error: %s\n",
                X509_verify_cert_error_string(verify_result));
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sockfd);
        SSL_CTX_free(ctx);
        return -1;
    }
    
    printf("Successfully connected to %s with valid certificate\n", hostname);
    printf("Cipher: %s\n", SSL_get_cipher(ssl));
    
    // Send HTTP request
    const char *request = "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n";
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), request, hostname);
    SSL_write(ssl, buffer, strlen(buffer));
    
    // Read response
    int bytes;
    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    
    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sockfd);
    SSL_CTX_free(ctx);
    
    return 0;
}

int main() {
    secure_connect("www.google.com", 443);
    return 0;
}
```

## Rust Implementation with rustls

```rust
use rustls::{ClientConfig, RootCertStore, ServerName};
use rustls_native_certs;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::Arc;
use webpki_roots;

fn secure_connect(hostname: &str, port: u16) -> Result<(), Box<dyn std::error::Error>> {
    // Create root certificate store
    let mut root_store = RootCertStore::empty();
    
    // Option 1: Use webpki-roots (Mozilla's CA bundle)
    root_store.add_server_trust_anchors(webpki_roots::TLS_SERVER_ROOTS.0.iter().map(|ta| {
        rustls::OwnedTrustAnchor::from_subject_spki_name_constraints(
            ta.subject,
            ta.spki,
            ta.name_constraints,
        )
    }));
    
    // Option 2: Load system certificates (alternative to webpki-roots)
    // for cert in rustls_native_certs::load_native_certs()? {
    //     root_store.add(&rustls::Certificate(cert.0))?;
    // }
    
    // Create client configuration with certificate validation
    let config = ClientConfig::builder()
        .with_safe_defaults() // Secure defaults including strong cipher suites
        .with_root_certificates(root_store)
        .with_no_client_auth(); // No client certificate
    
    let rc_config = Arc::new(config);
    
    // Parse hostname for SNI and certificate validation
    let server_name = ServerName::try_from(hostname)
        .map_err(|_| format!("Invalid hostname: {}", hostname))?;
    
    // Create TCP connection
    let addr = format!("{}:{}", hostname, port);
    let mut tcp_stream = TcpStream::connect(&addr)?;
    println!("TCP connection established to {}", addr);
    
    // Create TLS connection
    let mut tls_conn = rustls::ClientConnection::new(rc_config, server_name)?;
    let mut tls_stream = rustls::Stream::new(&mut tls_conn, &mut tcp_stream);
    
    println!("TLS handshake successful");
    println!("Negotiated cipher suite: {:?}", tls_conn.negotiated_cipher_suite());
    println!("Protocol version: {:?}", tls_conn.protocol_version());
    
    // Certificate validation happens automatically during handshake
    // rustls validates:
    // 1. Certificate chain to trusted root
    // 2. Hostname matches certificate
    // 3. Certificate is not expired
    // 4. Signatures are valid
    
    // Send HTTP request
    let request = format!(
        "GET / HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        hostname
    );
    tls_stream.write_all(request.as_bytes())?;
    
    // Read response
    let mut response = Vec::new();
    tls_stream.read_to_end(&mut response)?;
    println!("\nResponse:\n{}", String::from_utf8_lossy(&response));
    
    Ok(())
}

// Custom certificate verification example
use rustls::client::ServerCertVerifier;
use rustls::Certificate;

struct CustomVerifier {
    inner: Arc<dyn ServerCertVerifier>,
    allowed_hosts: Vec<String>,
}

impl CustomVerifier {
    fn new(allowed_hosts: Vec<String>) -> Self {
        let root_store = RootCertStore::empty();
        let config = ClientConfig::builder()
            .with_safe_defaults()
            .with_root_certificates(root_store)
            .with_no_client_auth();
        
        Self {
            inner: config.verifier().clone(),
            allowed_hosts,
        }
    }
}

impl ServerCertVerifier for CustomVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &Certificate,
        intermediates: &[Certificate],
        server_name: &ServerName,
        scts: &mut dyn Iterator<Item = &[u8]>,
        ocsp_response: &[u8],
        now: std::time::SystemTime,
    ) -> Result<rustls::client::ServerCertVerified, rustls::Error> {
        // First, perform standard verification
        self.inner.verify_server_cert(
            end_entity,
            intermediates,
            server_name,
            scts,
            ocsp_response,
            now,
        )?;
        
        // Additional custom checks
        let hostname = match server_name {
            ServerName::DnsName(name) => name.as_ref(),
            _ => return Err(rustls::Error::General("Invalid server name".to_string())),
        };
        
        if !self.allowed_hosts.iter().any(|h| h == hostname) {
            return Err(rustls::Error::General(
                format!("Hostname {} not in allowed list", hostname)
            ));
        }
        
        Ok(rustls::client::ServerCertVerified::assertion())
    }
}

// Example with custom verifier
fn secure_connect_with_custom_verifier(
    hostname: &str,
    port: u16,
) -> Result<(), Box<dyn std::error::Error>> {
    let verifier = CustomVerifier::new(vec![
        "www.google.com".to_string(),
        "www.example.com".to_string(),
    ]);
    
    let config = ClientConfig::builder()
        .with_safe_defaults()
        .with_custom_certificate_verifier(Arc::new(verifier))
        .with_no_client_auth();
    
    let rc_config = Arc::new(config);
    let server_name = ServerName::try_from(hostname)?;
    
    let mut tcp_stream = TcpStream::connect(format!("{}:{}", hostname, port))?;
    let mut tls_conn = rustls::ClientConnection::new(rc_config, server_name)?;
    let mut tls_stream = rustls::Stream::new(&mut tls_conn, &mut tcp_stream);
    
    println!("Connected with custom certificate verifier");
    
    let request = format!(
        "GET / HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        hostname
    );
    tls_stream.write_all(request.as_bytes())?;
    
    let mut response = vec![0u8; 1024];
    let n = tls_stream.read(&mut response)?;
    println!("{}", String::from_utf8_lossy(&response[..n]));
    
    Ok(())
}

fn main() {
    if let Err(e) = secure_connect("www.google.com", 443) {
        eprintln!("Connection error: {}", e);
    }
}
```

## Common Validation Errors and Attacks

**Certificate Expired**: The certificate's validity period has passed. This is often a configuration issue but could indicate compromise.

**Self-Signed Certificate**: The certificate is signed by itself rather than a trusted CA. Common in development but dangerous in production.

**Hostname Mismatch**: The certificate was issued for a different domain. This could indicate a man-in-the-middle attack or misconfiguration.

**Untrusted Root**: The certificate chain doesn't lead to a CA in the client's trust store. Could be a legitimate new CA or an attack.

**Revoked Certificate**: The certificate has been revoked by the CA, often due to compromise. Checking requires CRL or OCSP.

**Chain Validation Failure**: An intermediate certificate is missing or invalid, breaking the chain of trust.

## Best Practices

Always enable certificate validation in production environments. Never disable validation or accept all certificates, even temporarily, as this completely defeats TLS security. Keep your trusted CA certificate store updated with the latest root certificates. Implement proper error handling for validation failures and log them for security monitoring. Use modern TLS versions (TLS 1.2 or higher) and disable older insecure protocols. Consider implementing certificate pinning for high-security applications where you know the expected certificate or CA. Always verify hostnames in addition to chain validation, as valid certificates can be obtained for wrong domains. Monitor for certificate expiration and renewal to avoid service disruptions. Use OCSP stapling when possible to check certificate revocation status efficiently.

## Summary

Certificate validation is the cornerstone of TLS security, ensuring both the authenticity and integrity of secure communications. Proper implementation requires validating the entire certificate chain back to a trusted root CA and verifying that the certificate matches the intended hostname. Modern libraries like Rust's rustls provide these validations automatically with secure defaults, while C/OpenSSL requires more careful configuration. Failing to properly validate certificates creates critical security vulnerabilities that enable man-in-the-middle attacks, potentially exposing sensitive data. The examples demonstrate both automatic validation using library defaults and custom verification for specialized security requirements. Always prioritize security over convenience by never disabling certificate validation in production systems.