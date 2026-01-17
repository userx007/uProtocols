# 27. Cryptographic Best Practices

## Overview

Cryptographic best practices in TCP/IP networking are essential for ensuring secure communication channels. This encompasses three critical areas: secure random number generation for unpredictable keys and initialization vectors, proper key derivation to transform passwords into cryptographic keys, and appropriate cipher selection to balance security and performance. Poor cryptographic practices can undermine even the most robust network protocols, making adherence to established standards crucial.

## Core Concepts

### 1. Secure Random Number Generation

Random numbers are fundamental to cryptography. They're used for:
- **Session keys** in TLS/SSL
- **Initialization vectors (IVs)** for block ciphers
- **Nonces** to prevent replay attacks
- **Salts** for password hashing

**Key Requirements:**
- **Unpredictability**: Previous outputs must not allow prediction of future outputs
- **Non-determinism**: Must use entropy sources (hardware noise, system events)
- **Sufficient entropy**: Adequate randomness for cryptographic purposes

**Common Pitfalls:**
- Using `rand()` or `random()` (pseudorandom, not cryptographically secure)
- Seeding with predictable values (like `time()`)
- Insufficient entropy collection

### 2. Key Derivation

Key Derivation Functions (KDFs) transform passwords or master keys into cryptographic keys suitable for encryption.

**Purpose:**
- Stretch weak passwords to full-length keys
- Derive multiple keys from a single master key
- Add computational cost to resist brute-force attacks

**Common KDFs:**
- **PBKDF2** (Password-Based Key Derivation Function 2)
- **bcrypt** (primarily for password hashing)
- **scrypt** (memory-hard function)
- **Argon2** (modern, recommended for new applications)
- **HKDF** (HMAC-based KDF for key expansion)

### 3. Cipher Selection

Choosing appropriate ciphers involves balancing security, performance, and compatibility.

**Recommended Modern Ciphers:**
- **AES-256-GCM** (Authenticated encryption)
- **ChaCha20-Poly1305** (Stream cipher with authentication)
- **AES-128-GCM** (Faster, still secure)

**Avoid:**
- DES, 3DES (deprecated)
- RC4 (broken)
- ECB mode (deterministic, patterns leak)
- CBC mode without authentication (vulnerable to padding oracle attacks)

## Detailed Examples

### C/C++ Implementation

Using OpenSSL for cryptographic operations:

```c
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>

// 1. Secure Random Number Generation
int generate_secure_random(unsigned char *buffer, size_t length) {
    if (RAND_bytes(buffer, length) != 1) {
        fprintf(stderr, "Error generating random bytes\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    return 0;
}

// 2. Key Derivation using PBKDF2
int derive_key_pbkdf2(const char *password, 
                      const unsigned char *salt, size_t salt_len,
                      unsigned char *key, size_t key_len) {
    const int iterations = 100000; // OWASP recommended minimum
    
    if (PKCS5_PBKDF2_HMAC(password, strlen(password),
                          salt, salt_len,
                          iterations,
                          EVP_sha256(),
                          key_len, key) != 1) {
        fprintf(stderr, "PBKDF2 derivation failed\n");
        return -1;
    }
    return 0;
}

// 3. AES-256-GCM Encryption (Authenticated Encryption)
int encrypt_aes_gcm(const unsigned char *plaintext, size_t plaintext_len,
                    const unsigned char *key, const unsigned char *iv,
                    unsigned char *ciphertext, unsigned char *tag) {
    EVP_CIPHER_CTX *ctx;
    int len, ciphertext_len;
    
    // Create and initialize context
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }
    
    // Initialize encryption with AES-256-GCM
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    // Encrypt plaintext
    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;
    
    // Get authentication tag (16 bytes for GCM)
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}

// Complete example demonstrating best practices
int main() {
    unsigned char key[32];        // 256-bit key
    unsigned char iv[12];         // 96-bit IV for GCM
    unsigned char salt[16];       // 128-bit salt
    unsigned char derived_key[32];
    unsigned char tag[16];        // Authentication tag
    
    // Generate cryptographically secure random salt
    if (generate_secure_random(salt, sizeof(salt)) != 0) {
        return 1;
    }
    
    // Generate secure IV for GCM
    if (generate_secure_random(iv, sizeof(iv)) != 0) {
        return 1;
    }
    
    // Derive key from password using PBKDF2
    const char *password = "MySecurePassword123!";
    if (derive_key_pbkdf2(password, salt, sizeof(salt), 
                          derived_key, sizeof(derived_key)) != 0) {
        return 1;
    }
    
    // Encrypt data using AES-256-GCM
    const char *plaintext = "Sensitive network data";
    unsigned char ciphertext[128];
    
    int ciphertext_len = encrypt_aes_gcm(
        (unsigned char *)plaintext, strlen(plaintext),
        derived_key, iv, ciphertext, tag
    );
    
    if (ciphertext_len < 0) {
        fprintf(stderr, "Encryption failed\n");
        return 1;
    }
    
    printf("Successfully encrypted %d bytes\n", ciphertext_len);
    printf("Authentication tag generated\n");
    
    // In practice: transmit ciphertext, tag, iv, and salt
    // Store password hash separately for authentication
    
    return 0;
}
```

### C++ Modern Implementation

Using a more idiomatic C++ approach:

```cpp
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <array>

class CryptoHelper {
public:
    // Secure random generation
    static std::vector<unsigned char> generateRandom(size_t length) {
        std::vector<unsigned char> buffer(length);
        if (RAND_bytes(buffer.data(), length) != 1) {
            throw std::runtime_error("Failed to generate random bytes");
        }
        return buffer;
    }
    
    // PBKDF2 key derivation
    static std::vector<unsigned char> deriveKey(
        const std::string& password,
        const std::vector<unsigned char>& salt,
        size_t keyLength = 32,
        int iterations = 100000) {
        
        std::vector<unsigned char> key(keyLength);
        
        if (PKCS5_PBKDF2_HMAC(
            password.c_str(), password.length(),
            salt.data(), salt.size(),
            iterations,
            EVP_sha256(),
            keyLength, key.data()) != 1) {
            throw std::runtime_error("Key derivation failed");
        }
        
        return key;
    }
    
    // AES-256-GCM encryption with RAII
    struct EncryptionResult {
        std::vector<unsigned char> ciphertext;
        std::array<unsigned char, 16> tag;
        std::array<unsigned char, 12> iv;
    };
    
    static EncryptionResult encryptAesGcm(
        const std::vector<unsigned char>& plaintext,
        const std::vector<unsigned char>& key) {
        
        if (key.size() != 32) {
            throw std::invalid_argument("Key must be 256 bits");
        }
        
        EncryptionResult result;
        result.iv = generateRandomArray<12>();
        result.ciphertext.resize(plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
        
        // RAII wrapper for EVP_CIPHER_CTX
        auto ctx_deleter = [](EVP_CIPHER_CTX* ctx) { 
            EVP_CIPHER_CTX_free(ctx); 
        };
        std::unique_ptr<EVP_CIPHER_CTX, decltype(ctx_deleter)> ctx(
            EVP_CIPHER_CTX_new(), ctx_deleter
        );
        
        if (!ctx) {
            throw std::runtime_error("Failed to create cipher context");
        }
        
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, 
                               key.data(), result.iv.data()) != 1) {
            throw std::runtime_error("Encryption initialization failed");
        }
        
        // Encrypt data
        int len;
        if (EVP_EncryptUpdate(ctx.get(), result.ciphertext.data(), &len,
                             plaintext.data(), plaintext.size()) != 1) {
            throw std::runtime_error("Encryption update failed");
        }
        int ciphertext_len = len;
        
        // Finalize
        if (EVP_EncryptFinal_ex(ctx.get(), 
                               result.ciphertext.data() + len, &len) != 1) {
            throw std::runtime_error("Encryption finalization failed");
        }
        ciphertext_len += len;
        result.ciphertext.resize(ciphertext_len);
        
        // Get tag
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 
                               16, result.tag.data()) != 1) {
            throw std::runtime_error("Failed to get authentication tag");
        }
        
        return result;
    }
    
private:
    template<size_t N>
    static std::array<unsigned char, N> generateRandomArray() {
        std::array<unsigned char, N> arr;
        if (RAND_bytes(arr.data(), N) != 1) {
            throw std::runtime_error("Failed to generate random array");
        }
        return arr;
    }
};

// Usage example
int main() {
    try {
        // Generate salt
        auto salt = CryptoHelper::generateRandom(16);
        
        // Derive key from password
        std::string password = "MySecurePassword123!";
        auto key = CryptoHelper::deriveKey(password, salt);
        
        // Encrypt data
        std::string message = "Sensitive network data";
        std::vector<unsigned char> plaintext(message.begin(), message.end());
        
        auto result = CryptoHelper::encryptAesGcm(plaintext, key);
        
        printf("Encrypted %zu bytes\n", result.ciphertext.size());
        printf("IV and tag generated for authenticated encryption\n");
        
        // In practice: transmit result.ciphertext, result.tag, result.iv, and salt
        
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

Rust's type system and ownership model provide additional safety guarantees:

```rust
use ring::rand::{SecureRandom, SystemRandom};
use ring::pbkdf2;
use ring::aead::{Aad, Algorithm, BoundKey, Nonce, NonceSequence, OpeningKey, SealingKey, UnboundKey, AES_256_GCM};
use ring::error::Unspecified;
use std::num::NonZeroU32;

// Secure random number generation
fn generate_random_bytes(length: usize) -> Result<Vec<u8>, Unspecified> {
    let rng = SystemRandom::new();
    let mut bytes = vec![0u8; length];
    rng.fill(&mut bytes)?;
    Ok(bytes)
}

// PBKDF2 key derivation
fn derive_key(
    password: &str,
    salt: &[u8],
    iterations: u32,
) -> Result<[u8; 32], Unspecified> {
    let mut key = [0u8; 32];
    let iterations = NonZeroU32::new(iterations).unwrap();
    
    pbkdf2::derive(
        pbkdf2::PBKDF2_HMAC_SHA256,
        iterations,
        salt,
        password.as_bytes(),
        &mut key,
    );
    
    Ok(key)
}

// Verify a password against a derived key
fn verify_password(
    password: &str,
    salt: &[u8],
    iterations: u32,
    expected_key: &[u8],
) -> Result<(), Unspecified> {
    let iterations = NonZeroU32::new(iterations).unwrap();
    
    pbkdf2::verify(
        pbkdf2::PBKDF2_HMAC_SHA256,
        iterations,
        salt,
        password.as_bytes(),
        expected_key,
    )
}

// Custom nonce sequence for deterministic IV generation
struct CounterNonceSequence {
    counter: u128,
}

impl CounterNonceSequence {
    fn new() -> Self {
        CounterNonceSequence { counter: 0 }
    }
}

impl NonceSequence for CounterNonceSequence {
    fn advance(&mut self) -> Result<Nonce, Unspecified> {
        let mut nonce_bytes = [0u8; 12];
        let counter_bytes = self.counter.to_be_bytes();
        nonce_bytes[..12].copy_from_slice(&counter_bytes[4..16]);
        self.counter = self.counter.wrapping_add(1);
        Nonce::try_assume_unique_for_key(&nonce_bytes)
    }
}

// AES-256-GCM encryption result
#[derive(Debug)]
struct EncryptionResult {
    ciphertext: Vec<u8>,
    nonce: [u8; 12],
}

// AES-256-GCM encryption
fn encrypt_aes_gcm(
    plaintext: &[u8],
    key: &[u8; 32],
) -> Result<EncryptionResult, Unspecified> {
    // Generate random nonce
    let rng = SystemRandom::new();
    let mut nonce_bytes = [0u8; 12];
    rng.fill(&mut nonce_bytes)?;
    
    // Create unbound key
    let unbound_key = UnboundKey::new(&AES_256_GCM, key)?;
    
    // Create sealing key with single-use nonce
    let nonce = Nonce::try_assume_unique_for_key(&nonce_bytes)?;
    let mut sealing_key = SealingKey::new(unbound_key, OneNonceSequence::new(nonce));
    
    // Prepare data (in-place encryption requires extra space for tag)
    let mut in_out = plaintext.to_vec();
    
    // Encrypt (appends authentication tag)
    sealing_key.seal_in_place_append_tag(Aad::empty(), &mut in_out)?;
    
    Ok(EncryptionResult {
        ciphertext: in_out,
        nonce: nonce_bytes,
    })
}

// Single-use nonce sequence
struct OneNonceSequence {
    nonce: Option<Nonce>,
}

impl OneNonceSequence {
    fn new(nonce: Nonce) -> Self {
        OneNonceSequence { nonce: Some(nonce) }
    }
}

impl NonceSequence for OneNonceSequence {
    fn advance(&mut self) -> Result<Nonce, Unspecified> {
        self.nonce.take().ok_or(Unspecified)
    }
}

// AES-256-GCM decryption
fn decrypt_aes_gcm(
    ciphertext: &[u8],
    nonce_bytes: &[u8; 12],
    key: &[u8; 32],
) -> Result<Vec<u8>, Unspecified> {
    // Create unbound key
    let unbound_key = UnboundKey::new(&AES_256_GCM, key)?;
    
    // Create opening key
    let nonce = Nonce::try_assume_unique_for_key(nonce_bytes)?;
    let mut opening_key = OpeningKey::new(unbound_key, OneNonceSequence::new(nonce));
    
    // Decrypt (modifies in-place, removes tag)
    let mut in_out = ciphertext.to_vec();
    let plaintext = opening_key.open_in_place(Aad::empty(), &mut in_out)?;
    
    Ok(plaintext.to_vec())
}

// Complete example with best practices
fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 1. Generate cryptographically secure salt
    let salt = generate_random_bytes(16)?;
    println!("Generated {} byte salt", salt.len());
    
    // 2. Derive key from password using PBKDF2
    let password = "MySecurePassword123!";
    let iterations = 100_000; // OWASP recommended minimum
    let key = derive_key(password, &salt, iterations)?;
    println!("Derived 256-bit key using PBKDF2 with {} iterations", iterations);
    
    // 3. Encrypt data using AES-256-GCM
    let plaintext = b"Sensitive network data";
    let result = encrypt_aes_gcm(plaintext, &key)?;
    println!("Encrypted {} bytes -> {} bytes (includes 16-byte auth tag)", 
             plaintext.len(), result.ciphertext.len());
    
    // 4. Decrypt to verify
    let decrypted = decrypt_aes_gcm(&result.ciphertext, &result.nonce, &key)?;
    println!("Decrypted successfully: {}", String::from_utf8_lossy(&decrypted));
    
    // 5. Verify password (constant-time comparison)
    match verify_password(password, &salt, iterations, &key) {
        Ok(()) => println!("Password verification successful"),
        Err(_) => println!("Password verification failed"),
    }
    
    // In practice: transmit ciphertext, nonce, and salt
    // Store derived key hash for authentication
    
    Ok(())
}

// Additional example: ChaCha20-Poly1305 (alternative to AES-GCM)
use ring::aead::CHACHA20_POLY1305;

fn encrypt_chacha20_poly1305(
    plaintext: &[u8],
    key: &[u8; 32],
) -> Result<EncryptionResult, Unspecified> {
    let rng = SystemRandom::new();
    let mut nonce_bytes = [0u8; 12];
    rng.fill(&mut nonce_bytes)?;
    
    let unbound_key = UnboundKey::new(&CHACHA20_POLY1305, key)?;
    let nonce = Nonce::try_assume_unique_for_key(&nonce_bytes)?;
    let mut sealing_key = SealingKey::new(unbound_key, OneNonceSequence::new(nonce));
    
    let mut in_out = plaintext.to_vec();
    sealing_key.seal_in_place_append_tag(Aad::empty(), &mut in_out)?;
    
    Ok(EncryptionResult {
        ciphertext: in_out,
        nonce: nonce_bytes,
    })
}
```

## Best Practices Summary

### Random Number Generation
1. **Always use cryptographically secure PRNGs**: `/dev/urandom` on Unix, `CryptGenRandom` on Windows, or library functions like `RAND_bytes()` (OpenSSL) or `SystemRandom` (ring)
2. **Never use standard `rand()`**: It's predictable and designed for simulations, not security
3. **Don't seed with predictable values**: Time-based seeds are particularly dangerous

### Key Derivation
1. **Use modern KDFs**: PBKDF2 (minimum), scrypt, or Argon2 (preferred for new projects)
2. **Use sufficient iterations**: At least 100,000 for PBKDF2 (OWASP recommendation), adjust based on threat model
3. **Always use unique salts**: Generate random salts for each password/key derivation
4. **Use appropriate hash functions**: SHA-256 or better for PBKDF2
5. **Store salts securely**: They can be public but must be available for verification

### Cipher Selection
1. **Use authenticated encryption**: AES-GCM or ChaCha20-Poly1305 prevent tampering
2. **Avoid deprecated algorithms**: No DES, 3DES, RC4, or MD5
3. **Use appropriate key sizes**: 256-bit for AES, 128-bit minimum
4. **Never reuse IVs/nonces with the same key**: Catastrophic security failure for GCM
5. **Prefer AEAD modes**: They combine encryption and authentication (GCM, CCM, Poly1305)
6. **Never use ECB mode**: It leaks patterns in plaintext

### General Security
1. **Zero sensitive memory**: Clear keys and passwords after use
2. **Use constant-time operations**: Prevent timing attacks in password verification
3. **Implement proper error handling**: Don't leak information through error messages
4. **Keep libraries updated**: Security vulnerabilities are regularly discovered and patched
5. **Follow principle of least privilege**: Only grant necessary cryptographic capabilities
6. **Use TLS 1.3**: For network communications, leverage modern protocol security

## Summary

Cryptographic best practices form the foundation of secure TCP/IP communication. Secure random number generation ensures unpredictability in cryptographic primitives, proper key derivation transforms human-memorable passwords into strong cryptographic keys while resisting brute-force attacks, and careful cipher selection balances security with performance requirements. Modern applications should use AES-256-GCM or ChaCha20-Poly1305 for authenticated encryption, derive keys with PBKDF2 (100,000+ iterations) or Argon2, and generate all random values using cryptographically secure sources. Avoiding deprecated algorithms like DES and RC4, never reusing nonces with the same key, and implementing constant-time comparisons for authentication are critical safeguards. These practices, combined with regular security audits and keeping cryptographic libraries updated, provide robust protection against both passive eavesdropping and active attacks on network communications.