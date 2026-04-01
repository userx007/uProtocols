# 67. Authentication over I2C


**Structure at a glance:**

- **Threat Model** — what I2C exposes (bus spoofing, replay, MitM, counterfeiting) and what assets need protecting
- **Authentication Fundamentals** — symmetric vs. asymmetric schemes, and the challenge-response pattern explained step by step
- **Common Schemes** — HMAC-SHA256, AES-CMAC, ECDSA, and AES-GCM for encrypted sessions
- **Hardware Security ICs** — reference table covering ATECC608, SE050, OPTIGA Trust M, and others

**Code examples provided:**

| Topic | C/C++ | Rust |
|---|---|---|
| I2C bus access | `i2cdev` / Linux `i2c-dev` | `i2cdev` crate |
| Nonce generation | OpenSSL `RAND_bytes` | `rand::OsRng` |
| HMAC-SHA256 | OpenSSL `HMAC()` | `hmac` + `sha2` crates |
| AES-CMAC | mbedTLS | `cmac` + `aes` crates |
| ECDSA (ATECC608) | `cryptoauthlib` HAL | — |
| Constant-time compare | `volatile` XOR loop | `subtle::ConstantTimeEq` |
| Session key (ECDH) | mbedTLS ECDH + HKDF | — |
| Key derivation | OpenSSL HKDF | — |

The summary consolidates the 5 essential security rules that apply regardless of which scheme you choose.

### Implementing Cryptographic Authentication for Secure I2C Devices

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Authentication on I2C?](#why-authentication-on-i2c)
3. [Threat Model](#threat-model)
4. [Authentication Fundamentals](#authentication-fundamentals)
5. [Common Authentication Schemes](#common-authentication-schemes)
6. [Hardware Security ICs for I2C](#hardware-security-ics-for-i2c)
7. [Implementation in C/C++](#implementation-in-cc)
8. [Implementation in Rust](#implementation-in-rust)
9. [Secure Session Establishment](#secure-session-establishment)
10. [Anti-Replay and Freshness Mechanisms](#anti-replay-and-freshness-mechanisms)
11. [Key Management](#key-management)
12. [Best Practices and Pitfalls](#best-practices-and-pitfalls)
13. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) is a widely used two-wire serial protocol designed for communication between microcontrollers and peripheral devices — sensors, EEPROMs, displays, ADCs, and more. Originally conceived in the early 1980s, the protocol was designed for simplicity and cost-efficiency, not security.

As embedded systems are deployed in safety-critical, tamper-sensitive, or internet-connected environments, the lack of any native authentication in I2C has become a serious concern. Any device on the bus can listen, impersonate a peripheral, or inject malicious data.

**Authentication over I2C** refers to the practice of layering cryptographic protocols on top of standard I2C communication so that a host can verify that a device is genuine, and optionally vice versa. This is critical in:

- **Consumable authentication** — e.g., ink cartridges, medical test strips, battery packs
- **Accessory verification** — ensuring only certified peripherals connect
- **Tamper detection** — detecting replacement of secure hardware
- **Secure boot and provisioning** — verifying firmware-loading peripherals

---

## Why Authentication on I2C?

I2C is fundamentally **unauthenticated** and **unencrypted** by design. Problems this creates:

| Threat | Description |
|--------|-------------|
| **Counterfeit devices** | A cloned sensor can respond to I2C commands as if it were genuine |
| **Man-in-the-Middle (MitM)** | An attacker intercepts and modifies I2C traffic |
| **Replay attacks** | Recorded valid transactions are re-sent to spoof authentication |
| **Bus spoofing** | A rogue device answers to the address of a legitimate device |
| **Physical probing** | I2C is easily sniffable with a logic analyzer on a PCB |

Standard I2C provides **none** of the following: confidentiality, integrity, authentication, or freshness. All of these must be added in software or via dedicated hardware security ICs.

---

## Threat Model

Before implementing any authentication scheme, you must define your threat model:

```
Host MCU  ──── SDA/SCL ────  Peripheral Device
              (attacker can probe, inject, replay)
```

**Assets to protect:**
- Device identity (is this a genuine part?)
- Data integrity (has the sensor reading been tampered with?)
- Secret keys stored in the device

**Attacker capabilities to consider:**
- **Physical access** to PCB traces
- **Software-level** access to the host MCU (firmware compromise)
- **Supply chain** attacks (swapping a genuine part for a counterfeit)
- **Replay attacks** (recording and re-sending authentication sequences)

---

## Authentication Fundamentals

### Symmetric vs. Asymmetric Authentication

| Property | Symmetric (HMAC/AES) | Asymmetric (ECDSA/RSA) |
|----------|---------------------|----------------------|
| Key type | Shared secret | Public/private key pair |
| Speed | Very fast | Slower |
| Key storage risk | Both sides hold secret | Only device holds private key |
| Suitable for MCU | Yes | ECDSA (P-256) feasible |
| Use case | Closed ecosystems | Open/multi-vendor ecosystems |

### Challenge-Response Authentication

The most common pattern for device authentication over I2C:

```
Host                              Device
 │                                   │
 │── (1) Send random NONCE ─────────▶│
 │                                   │ (2) Compute MAC = HMAC(Key, Nonce)
 │◀── (3) Return MAC ────────────────│
 │                                   │
 │ (4) Verify MAC with shared Key    │
 │                                   │
 ├── PASS: Device is authentic ──────┤
 └── FAIL: Reject device ───────────┘
```

**Properties of a good challenge-response:**
- The nonce must be **cryptographically random** (not a counter alone)
- The MAC function must be **cryptographically secure** (HMAC-SHA256, AES-CMAC)
- The device must **never reveal the key**, only the MAC output
- The host must **verify** the response, not just receive it

---

## Common Authentication Schemes

### 1. HMAC-SHA256 Challenge-Response

The most widely deployed scheme for symmetric authentication on I2C:

- Host generates a 32-byte random nonce
- Device computes `MAC = HMAC-SHA256(SharedKey, Nonce || DeviceID || OtherData)`
- Host independently computes the expected MAC and compares

### 2. AES-CMAC (Cipher-based MAC)

An alternative to HMAC using AES in CMAC mode. Preferred when the device already has an AES hardware accelerator:

- `MAC = AES-CMAC(Key, Message)`
- CMAC is standardized in NIST SP 800-38B

### 3. ECDSA Signature Verification

For asymmetric setups — the device holds a private key and signs a challenge; the host verifies with the public key:

- Device: `Signature = ECDSA_Sign(PrivateKey, Hash(Nonce))`
- Host: `Valid = ECDSA_Verify(PublicKey, Signature, Hash(Nonce))`

### 4. Encrypted and Authenticated Data Transfer

For ongoing secure communication after initial authentication, AES-GCM or ChaCha20-Poly1305 provides both **encryption** and **integrity**:

- Encrypt sensor data so it cannot be read or tampered with in transit
- Authenticate every packet with a MAC tag

---

## Hardware Security ICs for I2C

Dedicated security co-processors expose authentication capabilities over I2C. Examples:

| Device | Vendor | Interface | Algorithms |
|--------|--------|-----------|------------|
| ATECC608A/B | Microchip | I2C | ECDH, ECDSA, SHA-256, AES |
| DS28E38 | Maxim/Analog | 1-Wire/I2C | ECDSA P-256 |
| SE050 | NXP | I2C | RSA, ECC, AES, 3DES |
| OPTIGA Trust M | Infineon | I2C | ECC, RSA, AES, SHA |
| A71CH | NXP | I2C | ECC P-256, AES |

These ICs handle all cryptographic operations internally. The host MCU only sends commands and receives results over I2C — the private keys never leave the device.

---

## Implementation in C/C++

### Basic I2C Transaction Layer (Linux i2c-dev)

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#define I2C_BUS          "/dev/i2c-1"
#define DEVICE_ADDR      0x60        // Example: ATECC608 default address
#define NONCE_SIZE       32
#define HMAC_SIZE        32
#define SHARED_KEY_SIZE  32

// --- I2C helpers ---

int i2c_open(const char *bus, uint8_t addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) { perror("open i2c"); return -1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("ioctl I2C_SLAVE"); close(fd); return -1; }
    return fd;
}

int i2c_write(int fd, const uint8_t *data, size_t len) {
    return (write(fd, data, len) == (ssize_t)len) ? 0 : -1;
}

int i2c_read(int fd, uint8_t *buf, size_t len) {
    return (read(fd, buf, len) == (ssize_t)len) ? 0 : -1;
}
```

### Generating a Cryptographic Nonce

```c
/**
 * Generate a cryptographically secure random nonce.
 * NEVER use rand() or a simple counter — it must be unpredictable.
 */
int generate_nonce(uint8_t *nonce, size_t len) {
    if (RAND_bytes(nonce, (int)len) != 1) {
        fprintf(stderr, "Failed to generate random nonce\n");
        return -1;
    }
    return 0;
}
```

### HMAC-SHA256 Computation

```c
/**
 * Compute HMAC-SHA256(key, message).
 * Returns 0 on success, -1 on failure.
 */
int compute_hmac_sha256(const uint8_t *key,    size_t key_len,
                         const uint8_t *msg,    size_t msg_len,
                         uint8_t       *out_mac) {
    unsigned int mac_len = HMAC_SIZE;
    unsigned char *result = HMAC(EVP_sha256(), key, (int)key_len,
                                  msg, msg_len, out_mac, &mac_len);
    return (result != NULL) ? 0 : -1;
}
```

### Challenge-Response Authentication (Full Flow)

```c
/**
 * Authenticate a device over I2C using HMAC-SHA256 challenge-response.
 *
 * Protocol:
 *   1. Host sends CMD_GET_CHALLENGE (0x01) to device
 *   2. Device returns 32-byte nonce (or host generates and sends it)
 *   3. Host sends CMD_AUTHENTICATE (0x02) + 32-byte nonce
 *   4. Device returns 32-byte HMAC-SHA256(key, nonce)
 *   5. Host verifies the MAC
 */

#define CMD_SEND_NONCE    0x01
#define CMD_GET_MAC       0x02
#define CMD_RESPONSE_OK   0xAA
#define CMD_RESPONSE_FAIL 0xFF

typedef struct {
    const uint8_t *shared_key;
    size_t         key_len;
} AuthContext;

/**
 * Constant-time memory comparison to prevent timing attacks.
 */
int secure_memcmp(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (diff == 0) ? 0 : -1;
}

int authenticate_device(int fd, const AuthContext *ctx) {
    uint8_t nonce[NONCE_SIZE];
    uint8_t device_mac[HMAC_SIZE];
    uint8_t expected_mac[HMAC_SIZE];
    uint8_t cmd_buf[1 + NONCE_SIZE];
    int ret;

    // Step 1: Generate random nonce
    if (generate_nonce(nonce, NONCE_SIZE) != 0) {
        return -1;
    }

    // Step 2: Send nonce to device
    cmd_buf[0] = CMD_SEND_NONCE;
    memcpy(cmd_buf + 1, nonce, NONCE_SIZE);
    if (i2c_write(fd, cmd_buf, sizeof(cmd_buf)) != 0) {
        fprintf(stderr, "Failed to send nonce\n");
        return -1;
    }

    // Step 3: Small delay for device computation (device-specific)
    usleep(5000); // 5ms

    // Step 4: Request MAC from device
    cmd_buf[0] = CMD_GET_MAC;
    if (i2c_write(fd, cmd_buf, 1) != 0) {
        fprintf(stderr, "Failed to request MAC\n");
        return -1;
    }

    // Step 5: Read device's HMAC response
    if (i2c_read(fd, device_mac, HMAC_SIZE) != 0) {
        fprintf(stderr, "Failed to read MAC response\n");
        return -1;
    }

    // Step 6: Compute expected MAC locally
    ret = compute_hmac_sha256(ctx->shared_key, ctx->key_len,
                               nonce, NONCE_SIZE,
                               expected_mac);
    if (ret != 0) {
        fprintf(stderr, "Failed to compute expected MAC\n");
        return -1;
    }

    // Step 7: Constant-time comparison (CRITICAL — prevents timing attacks)
    if (secure_memcmp(device_mac, expected_mac, HMAC_SIZE) != 0) {
        fprintf(stderr, "Authentication FAILED — device MAC mismatch!\n");
        return -1;
    }

    printf("Authentication PASSED — device is genuine.\n");
    return 0;
}
```

### Main Execution

```c
int main(void) {
    // In production, load this from secure storage (not hardcoded!)
    const uint8_t shared_key[SHARED_KEY_SIZE] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
    };

    AuthContext ctx = {
        .shared_key = shared_key,
        .key_len    = sizeof(shared_key)
    };

    int fd = i2c_open(I2C_BUS, DEVICE_ADDR);
    if (fd < 0) return 1;

    int result = authenticate_device(fd, &ctx);
    close(fd);

    return (result == 0) ? 0 : 1;
}
```

### AES-CMAC Authentication (C/C++ with mbedTLS)

```c
#include "mbedtls/cmac.h"
#include "mbedtls/aes.h"

/**
 * Compute AES-CMAC tag over a message.
 * Commonly used when a device has AES hardware acceleration.
 */
int compute_aes_cmac(const uint8_t *key,     // 16, 24, or 32 bytes
                      size_t         key_bits, // 128, 192, or 256
                      const uint8_t *msg,
                      size_t         msg_len,
                      uint8_t       *tag_out) { // 16 bytes output

    mbedtls_cipher_context_t ctx;
    const mbedtls_cipher_info_t *info;
    int ret;

    mbedtls_cipher_init(&ctx);

    info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (!info) return -1;

    ret = mbedtls_cipher_setup(&ctx, info);
    if (ret) goto cleanup;

    ret = mbedtls_cipher_cmac_starts(&ctx, key, key_bits);
    if (ret) goto cleanup;

    ret = mbedtls_cipher_cmac_update(&ctx, msg, msg_len);
    if (ret) goto cleanup;

    ret = mbedtls_cipher_cmac_finish(&ctx, tag_out);

cleanup:
    mbedtls_cipher_free(&ctx);
    return ret;
}
```

### ECDSA-Based Authentication with ATECC608 (C, HAL abstraction)

```c
/**
 * ATECC608A/B based ECDSA authentication.
 * The device internally holds a P-256 private key that never leaves the chip.
 *
 * Requires: Microchip's cryptoauthlib
 */
#include "cryptoauthlib.h"

#define AUTH_PRIV_KEY_SLOT  0    // Slot holding device private key
#define NONCE_SIZE          32

int ecdsa_authenticate_atecc608(void) {
    ATCA_STATUS status;
    uint8_t nonce[NONCE_SIZE];
    uint8_t signature[64];    // P-256 signature = 64 bytes (r || s)
    uint8_t public_key[64];   // P-256 public key = 64 bytes (x || y)

    // Step 1: Initialize the I2C interface to ATECC608
    ATCAIfaceCfg cfg = cfg_ateccx08a_i2c_default;
    cfg.atcai2c.address = 0xC0; // Default address (shifted)

    status = atcab_init(&cfg);
    if (status != ATCA_SUCCESS) {
        printf("ATECC608 init failed: %d\n", status);
        return -1;
    }

    // Step 2: Generate a random nonce on the host
    if (generate_nonce(nonce, NONCE_SIZE) != 0) {
        atcab_release();
        return -1;
    }

    // Step 3: Command device to sign the nonce with its private key
    // The private key NEVER leaves the device
    status = atcab_sign(AUTH_PRIV_KEY_SLOT, nonce, signature);
    if (status != ATCA_SUCCESS) {
        printf("ATECC608 sign failed: %d\n", status);
        atcab_release();
        return -1;
    }

    // Step 4: Read device public key (or use pre-stored trusted copy)
    status = atcab_get_pubkey(AUTH_PRIV_KEY_SLOT, public_key);
    if (status != ATCA_SUCCESS) {
        atcab_release();
        return -1;
    }

    // Step 5: Verify signature using the public key
    bool verified = false;
    status = atcab_verify_extern(nonce, signature, public_key, &verified);
    if (status != ATCA_SUCCESS || !verified) {
        printf("ECDSA verification FAILED!\n");
        atcab_release();
        return -1;
    }

    printf("ECDSA authentication PASSED.\n");
    atcab_release();
    return 0;
}
```

---

## Implementation in Rust

### Dependencies (`Cargo.toml`)

```toml
[package]
name = "i2c-auth"
version = "0.1.0"
edition = "2021"

[dependencies]
linux-embedded-hal = "0.4"
embedded-hal = "1.0"
i2cdev = "0.6"
hmac = "0.12"
sha2 = "0.10"
rand = { version = "0.8", features = ["getrandom"] }
hex = "0.4"
aes = "0.8"
cmac = "0.7"
subtle = "2.5"   # Constant-time comparisons
```

### I2C Device Abstraction

```rust
use i2cdev::core::I2CDevice;
use i2cdev::linux::LinuxI2CDevice;
use std::time::Duration;
use std::thread;

const I2C_BUS: &str = "/dev/i2c-1";
const DEVICE_ADDR: u16 = 0x60;
const CMD_SEND_NONCE: u8 = 0x01;
const CMD_GET_MAC: u8 = 0x02;
const NONCE_SIZE: usize = 32;
const HMAC_SIZE: usize = 32;

/// Wraps the Linux I2C device with auth-specific helpers
struct SecureI2CDevice {
    dev: LinuxI2CDevice,
}

impl SecureI2CDevice {
    fn new(bus: &str, addr: u16) -> Result<Self, Box<dyn std::error::Error>> {
        let dev = LinuxI2CDevice::new(bus, addr)?;
        Ok(Self { dev })
    }

    fn write_bytes(&mut self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        self.dev.write(data)?;
        Ok(())
    }

    fn read_bytes(&mut self, buf: &mut [u8]) -> Result<(), Box<dyn std::error::Error>> {
        self.dev.read(buf)?;
        Ok(())
    }
}
```

### Nonce Generation

```rust
use rand::RngCore;

/// Generate a cryptographically secure random nonce.
/// Uses the OS CSPRNG via the `rand` crate with `getrandom` backend.
fn generate_nonce() -> [u8; NONCE_SIZE] {
    let mut nonce = [0u8; NONCE_SIZE];
    rand::rngs::OsRng.fill_bytes(&mut nonce);
    nonce
}
```

### HMAC-SHA256 Computation

```rust
use hmac::{Hmac, Mac};
use sha2::Sha256;

type HmacSha256 = Hmac<Sha256>;

/// Compute HMAC-SHA256(key, message).
fn compute_hmac(key: &[u8], message: &[u8]) -> Result<[u8; HMAC_SIZE], String> {
    let mut mac = HmacSha256::new_from_slice(key)
        .map_err(|e| format!("Invalid HMAC key: {}", e))?;

    mac.update(message);

    let result = mac.finalize().into_bytes();
    let mut out = [0u8; HMAC_SIZE];
    out.copy_from_slice(&result);
    Ok(out)
}
```

### Constant-Time Comparison

```rust
use subtle::ConstantTimeEq;

/// Constant-time MAC comparison to prevent timing side-channel attacks.
/// NEVER use == or memcmp for MAC comparison.
fn macs_equal(a: &[u8; HMAC_SIZE], b: &[u8; HMAC_SIZE]) -> bool {
    a.ct_eq(b).into()
}
```

### Full HMAC Challenge-Response Authentication

```rust
/// Authenticate a device over I2C using HMAC-SHA256 challenge-response.
///
/// Returns Ok(()) on successful authentication, Err on failure.
fn authenticate_device(
    device: &mut SecureI2CDevice,
    shared_key: &[u8],
) -> Result<(), Box<dyn std::error::Error>> {

    // Step 1: Generate random nonce (host-side)
    let nonce = generate_nonce();
    println!("Nonce: {}", hex::encode(nonce));

    // Step 2: Send CMD + nonce to device
    let mut cmd_buf = Vec::with_capacity(1 + NONCE_SIZE);
    cmd_buf.push(CMD_SEND_NONCE);
    cmd_buf.extend_from_slice(&nonce);
    device.write_bytes(&cmd_buf)?;

    // Step 3: Allow device time to compute MAC
    thread::sleep(Duration::from_millis(5));

    // Step 4: Request MAC from device
    device.write_bytes(&[CMD_GET_MAC])?;

    // Step 5: Read HMAC response (32 bytes)
    let mut device_mac = [0u8; HMAC_SIZE];
    device.read_bytes(&mut device_mac)?;
    println!("Device MAC: {}", hex::encode(device_mac));

    // Step 6: Compute expected MAC locally
    let expected_mac = compute_hmac(shared_key, &nonce)?;
    println!("Expected MAC: {}", hex::encode(expected_mac));

    // Step 7: Constant-time comparison — critical for security!
    if macs_equal(&device_mac, &expected_mac) {
        println!("✓ Authentication PASSED — device is genuine.");
        Ok(())
    } else {
        Err("Authentication FAILED — MAC mismatch. Device may be counterfeit!".into())
    }
}
```

### AES-CMAC in Rust

```rust
use aes::Aes128;
use cmac::{Cmac, Mac as CmacMac};

/// Compute AES-128-CMAC(key, message).
/// Produces a 16-byte authentication tag.
fn compute_aes_cmac(key: &[u8; 16], message: &[u8]) -> Result<[u8; 16], String> {
    let mut mac = Cmac::<Aes128>::new_from_slice(key)
        .map_err(|e| format!("CMAC init failed: {}", e))?;

    mac.update(message);

    let tag = mac.finalize().into_bytes();
    let mut out = [0u8; 16];
    out.copy_from_slice(&tag);
    Ok(out)
}

/// Example: Authenticate using AES-CMAC instead of HMAC-SHA256
fn authenticate_device_cmac(
    device: &mut SecureI2CDevice,
    aes_key: &[u8; 16],
) -> Result<(), Box<dyn std::error::Error>> {
    let nonce = generate_nonce();

    // Send nonce
    let mut buf = vec![CMD_SEND_NONCE];
    buf.extend_from_slice(&nonce);
    device.write_bytes(&buf)?;

    thread::sleep(Duration::from_millis(5));
    device.write_bytes(&[CMD_GET_MAC])?;

    let mut device_tag = [0u8; 16];
    device.read_bytes(&mut device_tag)?;

    let expected_tag = compute_aes_cmac(aes_key, &nonce)?;

    // Constant-time comparison
    if device_tag.ct_eq(&expected_tag).into() {
        println!("✓ AES-CMAC Authentication PASSED.");
        Ok(())
    } else {
        Err("AES-CMAC Authentication FAILED.".into())
    }
}
```

### Main Entry Point (Rust)

```rust
fn main() {
    // In production, derive or load this key from secure storage
    let shared_key: [u8; 32] = [
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    ];

    let mut device = SecureI2CDevice::new(I2C_BUS, DEVICE_ADDR)
        .expect("Failed to open I2C device");

    match authenticate_device(&mut device, &shared_key) {
        Ok(()) => {
            println!("Device authenticated — proceeding with normal operation.");
            std::process::exit(0);
        }
        Err(e) => {
            eprintln!("SECURITY ERROR: {}", e);
            std::process::exit(1);
        }
    }
}
```

---

## Secure Session Establishment

After authentication, you may want to establish an **encrypted session** for ongoing communication. A simple approach using ECDH key agreement:

```c
/*
 * Simplified ECDH session key establishment over I2C (C, using mbedTLS)
 *
 * 1. Both sides generate ephemeral ECDH keypairs
 * 2. Public keys are exchanged over I2C
 * 3. Both sides compute the shared secret
 * 4. Session key = KDF(shared_secret, nonce_host || nonce_device)
 * 5. All subsequent data is encrypted with AES-GCM using the session key
 */
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/hkdf.h"

typedef struct {
    uint8_t session_key[32];
    uint8_t tx_nonce[12];   // For AES-GCM
    uint32_t tx_counter;    // Prevents nonce reuse
} SecureSession;

int establish_session(int i2c_fd, SecureSession *session,
                      const uint8_t *device_pubkey) {
    mbedtls_ecdh_context   ecdh;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    uint8_t host_pubkey[65]; // Uncompressed P-256 point
    uint8_t shared_secret[32];
    size_t  olen;
    int ret;

    mbedtls_ecdh_init(&ecdh);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Generate ephemeral keypair
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_ecdh_gen_public(&ecdh.ctx.mbed_ecdh.grp,
                             &ecdh.ctx.mbed_ecdh.d,
                             &ecdh.ctx.mbed_ecdh.Q,
                             mbedtls_ctr_drbg_random, &ctr_drbg);

    // Export host public key and send to device over I2C
    mbedtls_ecp_point_write_binary(&ecdh.ctx.mbed_ecdh.grp,
                                    &ecdh.ctx.mbed_ecdh.Q,
                                    MBEDTLS_ECP_PF_UNCOMPRESSED,
                                    &olen, host_pubkey, sizeof(host_pubkey));
    // ... i2c_write(i2c_fd, host_pubkey, olen) ...

    // Compute shared secret using device's public key
    // ... load device_pubkey into ecdh.ctx.mbed_ecdh.Qp ...

    mbedtls_ecdh_calc_secret(&ecdh, &olen, shared_secret, sizeof(shared_secret),
                              mbedtls_ctr_drbg_random, &ctr_drbg);

    // Derive session key using HKDF-SHA256
    uint8_t salt[64]; // nonce_host || nonce_device
    RAND_bytes(salt, 32); // host portion
    // ... receive device nonce and append ...

    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  salt, sizeof(salt),
                  shared_secret, sizeof(shared_secret),
                  (const uint8_t *)"I2C-Session-v1", 14,
                  session->session_key, 32);

    session->tx_counter = 0;
    RAND_bytes(session->tx_nonce, 12);

    // Cleanup
    mbedtls_ecdh_free(&ecdh);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return 0;
}
```

---

## Anti-Replay and Freshness Mechanisms

A valid MAC is useless if the attacker can record and replay it. Countermeasures:

### 1. Monotonic Counter (in non-volatile memory)

```c
/**
 * Include a monotonic counter in the MAC input.
 * Counter must be stored in non-volatile memory and never reset.
 *
 * MAC = HMAC-SHA256(Key, Nonce || Counter || Message)
 */
typedef struct {
    uint8_t  nonce[NONCE_SIZE];
    uint32_t counter;          // Big-endian, stored in NVM
    uint8_t  message[64];
    size_t   message_len;
} AuthMessage;

int compute_auth_mac_with_counter(const AuthMessage *msg,
                                   const uint8_t *key,
                                   uint8_t *mac_out) {
    uint8_t buf[NONCE_SIZE + 4 + 64];
    uint32_t be_counter = __builtin_bswap32(msg->counter);

    memcpy(buf, msg->nonce, NONCE_SIZE);
    memcpy(buf + NONCE_SIZE, &be_counter, 4);
    memcpy(buf + NONCE_SIZE + 4, msg->message, msg->message_len);

    return compute_hmac_sha256(key, SHARED_KEY_SIZE,
                                buf, NONCE_SIZE + 4 + msg->message_len,
                                mac_out);
}
```

### 2. Timestamp Validation

```rust
use std::time::{SystemTime, UNIX_EPOCH};

const MAX_TIMESTAMP_DRIFT_SECS: u64 = 30;

/// Validate that a device's timestamp is within acceptable bounds.
/// Prevents replay of old valid messages.
fn validate_timestamp(device_timestamp_secs: u64) -> bool {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time error")
        .as_secs();

    let drift = now.abs_diff(device_timestamp_secs);
    drift <= MAX_TIMESTAMP_DRIFT_SECS
}
```

---

## Key Management

Keys are only as secure as how they are stored. Recommendations:

| Storage Method | Security Level | Notes |
|---------------|----------------|-------|
| Hardcoded in firmware | ❌ Very low | Extractable via debug port or disassembly |
| External EEPROM, unencrypted | ❌ Low | Trivially readable on bus |
| Flash + read-protection bits | ⚠️ Medium | MCU-specific, can sometimes be bypassed |
| Dedicated SE (e.g. ATECC608) | ✅ High | Key never leaves the chip |
| TrustZone / Secure Enclave | ✅ High | Hardware-isolated secure world |

### Key Derivation (C)

```c
#include <openssl/evp.h>
#include <openssl/kdf.h>

/**
 * Derive a device-specific key using HKDF-SHA256.
 * device_id should be the serial number or UID of the target device.
 *
 * Allows a single master key to generate unique per-device keys,
 * so a compromised device does not compromise all devices.
 */
int derive_device_key(const uint8_t *master_key, size_t mk_len,
                       const uint8_t *device_id,  size_t id_len,
                       uint8_t *derived_key,       size_t dk_len) {
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    int ret = -1;

    if (!pctx) return -1;
    if (EVP_PKEY_derive_init(pctx) <= 0) goto cleanup;
    if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) goto cleanup;
    if (EVP_PKEY_CTX_set1_hkdf_key(pctx, master_key, mk_len) <= 0) goto cleanup;
    if (EVP_PKEY_CTX_add1_hkdf_info(pctx, device_id, id_len) <= 0) goto cleanup;
    if (EVP_PKEY_derive(pctx, derived_key, &dk_len) <= 0) goto cleanup;
    ret = 0;

cleanup:
    EVP_PKEY_CTX_free(pctx);
    return ret;
}
```

---

## Best Practices and Pitfalls

### ✅ Do

- **Use cryptographically random nonces** — `RAND_bytes()` in C, `OsRng` in Rust. Never use `rand()`, counters alone, or timestamps alone as nonces.
- **Use constant-time comparison** for all MAC/tag verification — prevents timing side-channel attacks.
- **Include a monotonic counter or timestamp** in the authenticated data to prevent replay attacks.
- **Derive per-device keys** from a master key using HKDF — limits blast radius of a single compromised device.
- **Use dedicated security ICs** (ATECC608, SE050) when possible — they protect keys in hardware.
- **Verify before trusting any data** from an I2C peripheral — don't process data before authentication passes.
- **Authenticate both directions** (mutual authentication) in high-security applications.

### ❌ Don't

- **Don't use ECB mode** for any authentication or encryption — ECB is deterministic and reveals patterns.
- **Don't reuse nonces** — nonce reuse in GCM completely breaks security.
- **Don't use short MACs** — minimum 128 bits (16 bytes); 256 bits (32 bytes) preferred.
- **Don't ignore timing** — if your auth check takes different time for valid vs. invalid responses, you have a timing oracle.
- **Don't store keys in plain firmware** — use secure elements or TrustZone.
- **Don't skip authentication on "internal" buses** — PCBs are accessible to attackers with physical access.
- **Don't rely on I2C address as identity** — addresses can be spoofed by any device on the bus.

---

## Summary

Authentication over I2C is a critical layer of security for embedded systems that rely on peripheral devices. Because the I2C protocol provides no native authentication, integrity, or confidentiality, all of these must be implemented at the application layer.

**The core pattern** is the **challenge-response protocol**: the host generates a random nonce, sends it to the device, and the device returns a cryptographic MAC (HMAC-SHA256 or AES-CMAC). The host verifies the response using the shared key. Only a device possessing the correct secret key can produce a valid MAC. For asymmetric setups — especially with dedicated security ICs — ECDSA-based signing provides even stronger guarantees, as the private key never leaves the hardware.

**In C/C++**, libraries such as OpenSSL and mbedTLS provide the cryptographic primitives, and the Linux `i2c-dev` interface or vendor HALs (e.g., `cryptoauthlib` for ATECC608) handle bus communication. In **Rust**, the `hmac`, `sha2`, `cmac`, and `subtle` crates compose cleanly to provide safe, constant-time authenticated communication with strong type guarantees.

**Key security rules to remember:**
1. Always use a cryptographically secure random nonce (never a simple counter alone).
2. Always compare MACs using constant-time functions (`subtle::ConstantTimeEq` in Rust, custom `volatile` loop in C).
3. Include freshness data (counter + nonce) to defeat replay attacks.
4. Derive per-device keys from a master key to limit the scope of a key compromise.
5. Prefer hardware security elements for key storage whenever possible.

Authentication over I2C is not a silver bullet — a fully secure system also requires secure boot, physical tamper resistance, and secure provisioning. But cryptographic authentication is the essential first step in ensuring that your I2C peripherals are exactly what they claim to be.

---

*Document: 67 — Authentication over I2C | I2C Security Series*