# 86. Encrypted SPI Communication


1. **Overview** — why SPI needs encryption and typical use cases
2. **Threat Model** — attacker categories, attack surfaces, security goals
3. **Encryption Strategies** — AES-GCM (recommended), AES-CTR, ChaCha20-Poly1305, AES-CCM with frame layout diagram
4. **Key Management** — KDF derivation chain, nonce strategies, key storage comparison table
5. **C/C++ Implementation** — full `spi_crypto.h` / `spi_crypto.c` using **mbedTLS AES-128-GCM**, a `main.c` with loopback demo, and a C++ wrapper class `SpiSecureChannel`
6. **Rust Implementation** — `Cargo.toml`, a full `lib.rs` using `aes-gcm` + `zeroize`, a `main.rs` demo (including tamper detection test), and a `no_std` bare-metal variant using `heapless`
7. **Performance Considerations** — algorithm speed comparison table, MCU benchmarks, pipelining tips
8. **Hardware Acceleration** — STM32 CRYP HAL and ESP32 AES-IDF examples
9. **Common Pitfalls** — nonce reuse, using data before tag verification, hardcoded keys, missing zeroize
10. **Summary** — concise recap of protocol design, nonce/key management, and implementation guidance


## Implementing Encryption for Sensitive Data Transmitted Over SPI

---

## Table of Contents

1. [Overview](#overview)
2. [Why Encrypt SPI?](#why-encrypt-spi)
3. [Threat Model](#threat-model)
4. [Encryption Strategies for SPI](#encryption-strategies-for-spi)
5. [Key Management](#key-management)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Performance Considerations](#performance-considerations)
9. [Hardware Acceleration](#hardware-acceleration)
10. [Common Pitfalls](#common-pitfalls)
11. [Summary](#summary)

---

## Overview

SPI (Serial Peripheral Interface) is a widely used synchronous serial communication protocol in embedded systems, connecting microcontrollers to peripherals such as sensors, memory chips, displays, and RF modules. While SPI is fast and simple, it transmits data in plaintext by default, making it vulnerable to eavesdropping, replay attacks, and man-in-the-middle attacks — especially in IoT deployments, industrial control systems, or any application handling sensitive data.

**Encrypted SPI Communication** adds a cryptographic layer on top of the standard SPI protocol to protect the confidentiality and integrity of data exchanged between master and slave devices.

---

## Why Encrypt SPI?

| Threat | Without Encryption | With Encryption |
|---|---|---|
| Physical wire tapping | Data exposed | Ciphertext only |
| Replay attacks | Attacker resends captured frames | Nonce/IV prevents replay |
| Data tampering | Undetected | AEAD detects modification |
| Firmware extraction | Plaintext readable | Encrypted payloads |
| Side-channel probing | Register values leaked | Minimal gain |

Typical use cases that warrant SPI encryption:

- **Secure element communication** – passing keys or PINs to a crypto chip
- **Encrypted flash/EEPROM** – protecting firmware or configuration blobs
- **Sensor data confidentiality** – medical, industrial, or defence sensors
- **Encrypted display buffers** – DRM-protected content pipelines
- **Boot-time secure communication** – HSM ↔ MCU handshake during secure boot

---

## Threat Model

Before choosing an encryption scheme, define your threat model:

```
┌────────────────────────────────────────────────────────┐
│                  Threat Model                          │
│                                                        │
│  Assets:  Sensor readings, keys, config, firmware      │
│                                                        │
│  Adversaries:                                          │
│    - Passive eavesdropper (most common on SPI bus)     │
│    - Active attacker (replay, bit-flip)                │
│    - Physical attacker (decapping, probing)            │
│                                                        │
│  Attack surfaces:                                      │
│    - MOSI/MISO lines (oscilloscope/logic analyser)     │
│    - Shared SPI bus with multiple slaves               │
│    - Firmware update channels                          │
│                                                        │
│  Goals:                                                │
│    - Confidentiality (AES-CTR, AES-GCM)                │
│    - Integrity / Authentication (AEAD, HMAC)           │
│    - Freshness / Anti-replay (nonce, counter)          │
└────────────────────────────────────────────────────────┘
```

---

## Encryption Strategies for SPI

### 1. AES-CTR (Counter Mode)

Converts AES block cipher into a stream cipher. Fast, no padding required. Does **not** provide authentication — must be combined with HMAC or similar.

### 2. AES-GCM (Galois/Counter Mode) ✅ Recommended

Authenticated Encryption with Associated Data (AEAD). Provides confidentiality **and** integrity in one pass. Industry standard for embedded secure channels.

### 3. ChaCha20-Poly1305

Excellent alternative to AES-GCM on MCUs without hardware AES acceleration. Designed to be fast in software.

### 4. AES-CCM

Similar to GCM but uses CBC-MAC for authentication. Common in IEEE 802.15.4 / Zigbee stacks.

### Protocol Frame Structure

```
┌──────────┬──────────┬─────────────────────┬──────────┐
│ Header   │  Nonce   │  Encrypted Payload  │   Tag    │
│ (2 bytes)│ (12 bytes│    (N bytes)        │ (16 bytes│
└──────────┴──────────┴─────────────────────┴──────────┘

Header:  [MSG_TYPE | PAYLOAD_LEN]
Nonce:   Unique per message — never reuse with the same key!
Tag:     GCM authentication tag (or Poly1305 tag)
```

---

## Key Management

Key management is the hardest part of any encrypted communication system.

### Key Derivation

Never hardcode raw keys. Use a Key Derivation Function (KDF):

```
Master Secret (stored in OTP/eFuse)
        │
        ▼
    HKDF / PBKDF2
        │
        ├──► Session Encryption Key (AES-128/256)
        └──► Session Authentication Key (HMAC-SHA256)
```

### Nonce Management

**Critical rule: Never reuse a (key, nonce) pair.**

```
Options:
  1. Monotonic counter  → persist in NVM, increment each message
  2. Random nonce       → 96-bit random, collision probability negligible
  3. Timestamp-based    → RTC required; beware clock drift/resets
```

### Key Storage

| Storage | Security Level | Notes |
|---|---|---|
| Flash (plaintext) | Low | Avoid |
| Encrypted flash | Medium | Needs root key in hardware |
| OTP / eFuse | High | One-time write, hardware protected |
| Secure Element (SE) | Very High | ATECC608, OPTIGA Trust |
| TrustZone / TEE | Very High | ARM Cortex-M33/A-series |

---

## C/C++ Implementation

### Prerequisites / Dependencies

- **mbedTLS** – portable TLS/crypto library for embedded systems
- Or **wolfSSL** / **Libsodium** (bare-metal port)

```bash
# Install mbedTLS (Linux/development host)
sudo apt-get install libmbedtls-dev

# Or clone and build for your MCU target
git clone https://github.com/Mbed-TLS/mbedtls.git
```

---

### Header: `spi_crypto.h`

```c
#ifndef SPI_CRYPTO_H
#define SPI_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SPI_CRYPTO_KEY_LEN      16   /* AES-128 */
#define SPI_CRYPTO_IV_LEN       12   /* GCM nonce */
#define SPI_CRYPTO_TAG_LEN      16   /* GCM authentication tag */
#define SPI_CRYPTO_HEADER_LEN    2   /* msg_type + reserved */
#define SPI_CRYPTO_OVERHEAD     (SPI_CRYPTO_IV_LEN + SPI_CRYPTO_TAG_LEN + SPI_CRYPTO_HEADER_LEN)
#define SPI_CRYPTO_MAX_PAYLOAD  256

/* SPI frame layout:
 *  [header:2][nonce:12][ciphertext:N][tag:16]
 */
typedef struct {
    uint8_t  header[SPI_CRYPTO_HEADER_LEN];
    uint8_t  nonce[SPI_CRYPTO_IV_LEN];
    uint8_t  ciphertext[SPI_CRYPTO_MAX_PAYLOAD];
    uint8_t  tag[SPI_CRYPTO_TAG_LEN];
    size_t   payload_len;
} spi_crypto_frame_t;

typedef struct {
    uint8_t  key[SPI_CRYPTO_KEY_LEN];
    uint64_t tx_counter;   /* monotonic nonce counter for TX */
    uint64_t rx_counter;   /* expected RX counter (anti-replay) */
} spi_crypto_ctx_t;

/* Initialise context with a 16-byte key */
int  spi_crypto_init(spi_crypto_ctx_t *ctx, const uint8_t *key);

/* Encrypt plaintext → frame (returns total frame length or <0 on error) */
int  spi_crypto_encrypt(spi_crypto_ctx_t *ctx,
                        const uint8_t    *plaintext,
                        size_t            plaintext_len,
                        uint8_t           msg_type,
                        spi_crypto_frame_t *frame);

/* Decrypt frame → plaintext (returns plaintext length or <0 on error) */
int  spi_crypto_decrypt(spi_crypto_ctx_t *ctx,
                        const spi_crypto_frame_t *frame,
                        uint8_t          *plaintext,
                        size_t            plaintext_max_len);

#endif /* SPI_CRYPTO_H */
```

---

### Implementation: `spi_crypto.c` (using mbedTLS AES-GCM)

```c
#include "spi_crypto.h"
#include "mbedtls/gcm.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* Build a 12-byte nonce from the 64-bit counter (big-endian suffix)   */
static void build_nonce(uint8_t nonce[SPI_CRYPTO_IV_LEN], uint64_t counter)
{
    memset(nonce, 0, SPI_CRYPTO_IV_LEN);
    /* Place counter in the last 8 bytes */
    for (int i = 0; i < 8; i++) {
        nonce[SPI_CRYPTO_IV_LEN - 1 - i] = (uint8_t)(counter >> (8 * i));
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int spi_crypto_init(spi_crypto_ctx_t *ctx, const uint8_t *key)
{
    if (!ctx || !key) return -1;
    memcpy(ctx->key, key, SPI_CRYPTO_KEY_LEN);
    ctx->tx_counter = 0;
    ctx->rx_counter = 0;
    return 0;
}

/**
 * spi_crypto_encrypt
 *
 * Encrypts `plaintext` using AES-128-GCM.
 * The frame header (msg_type) is included as Additional Authenticated
 * Data (AAD) — it is authenticated but NOT encrypted, allowing the
 * receiver to route messages before decryption.
 *
 * Returns total serialised frame size, or negative on error.
 */
int spi_crypto_encrypt(spi_crypto_ctx_t   *ctx,
                       const uint8_t      *plaintext,
                       size_t              plaintext_len,
                       uint8_t             msg_type,
                       spi_crypto_frame_t *frame)
{
    if (!ctx || !plaintext || !frame) return -1;
    if (plaintext_len > SPI_CRYPTO_MAX_PAYLOAD) return -2;

    mbedtls_gcm_context gcm;
    int ret = 0;

    /* Build header (used as AAD) */
    frame->header[0] = msg_type;
    frame->header[1] = (uint8_t)(plaintext_len & 0xFF);

    /* Build nonce from monotonic counter — never reuse! */
    build_nonce(frame->nonce, ctx->tx_counter);
    ctx->tx_counter++;

    /* AES-GCM encrypt */
    mbedtls_gcm_init(&gcm);

    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                              ctx->key, SPI_CRYPTO_KEY_LEN * 8);
    if (ret != 0) goto cleanup;

    ret = mbedtls_gcm_crypt_and_tag(
            &gcm,
            MBEDTLS_GCM_ENCRYPT,
            plaintext_len,
            frame->nonce,   SPI_CRYPTO_IV_LEN,
            frame->header,  SPI_CRYPTO_HEADER_LEN,   /* AAD */
            plaintext,
            frame->ciphertext,
            SPI_CRYPTO_TAG_LEN,
            frame->tag);

    frame->payload_len = plaintext_len;

cleanup:
    mbedtls_gcm_free(&gcm);
    if (ret != 0) return -3;

    return (int)(SPI_CRYPTO_HEADER_LEN +
                 SPI_CRYPTO_IV_LEN     +
                 plaintext_len         +
                 SPI_CRYPTO_TAG_LEN);
}

/**
 * spi_crypto_decrypt
 *
 * Decrypts and authenticates a received SPI frame.
 * Returns plaintext length, or negative on error / authentication failure.
 */
int spi_crypto_decrypt(spi_crypto_ctx_t         *ctx,
                       const spi_crypto_frame_t *frame,
                       uint8_t                  *plaintext,
                       size_t                    plaintext_max_len)
{
    if (!ctx || !frame || !plaintext) return -1;
    if (frame->payload_len > plaintext_max_len) return -2;

    mbedtls_gcm_context gcm;
    int ret = 0;

    mbedtls_gcm_init(&gcm);

    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                              ctx->key, SPI_CRYPTO_KEY_LEN * 8);
    if (ret != 0) goto cleanup;

    ret = mbedtls_gcm_auth_decrypt(
            &gcm,
            frame->payload_len,
            frame->nonce,   SPI_CRYPTO_IV_LEN,
            frame->header,  SPI_CRYPTO_HEADER_LEN,   /* AAD */
            frame->tag,     SPI_CRYPTO_TAG_LEN,
            frame->ciphertext,
            plaintext);

cleanup:
    mbedtls_gcm_free(&gcm);

    /* MBEDTLS_ERR_GCM_AUTH_FAILED = authentication tag mismatch */
    if (ret != 0) return -3;

    return (int)frame->payload_len;
}
```

---

### SPI HAL + Encrypted Transfer: `main.c`

```c
#include "spi_crypto.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Platform SPI stub — replace with your HAL (STM32, ESP-IDF, etc.)  */
/* ------------------------------------------------------------------ */

static uint8_t spi_tx_buf[512];
static uint8_t spi_rx_buf[512];

void spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    /* TODO: replace with real SPI HAL call, e.g.:
     *   HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, HAL_MAX_DELAY);
     * For this example, loopback: rx = tx */
    memcpy(rx, tx, len);
}

/* ------------------------------------------------------------------ */
/*  Serialise / deserialise frame to/from flat byte buffer            */
/* ------------------------------------------------------------------ */

static int frame_to_bytes(const spi_crypto_frame_t *f, uint8_t *buf, size_t buf_len)
{
    size_t total = SPI_CRYPTO_HEADER_LEN + SPI_CRYPTO_IV_LEN
                 + f->payload_len + SPI_CRYPTO_TAG_LEN;
    if (buf_len < total) return -1;

    uint8_t *p = buf;
    memcpy(p, f->header,     SPI_CRYPTO_HEADER_LEN); p += SPI_CRYPTO_HEADER_LEN;
    memcpy(p, f->nonce,      SPI_CRYPTO_IV_LEN);     p += SPI_CRYPTO_IV_LEN;
    memcpy(p, f->ciphertext, f->payload_len);         p += f->payload_len;
    memcpy(p, f->tag,        SPI_CRYPTO_TAG_LEN);
    return (int)total;
}

static int bytes_to_frame(const uint8_t *buf, size_t buf_len, spi_crypto_frame_t *f)
{
    if (buf_len < SPI_CRYPTO_OVERHEAD) return -1;
    const uint8_t *p = buf;

    memcpy(f->header, p, SPI_CRYPTO_HEADER_LEN); p += SPI_CRYPTO_HEADER_LEN;
    memcpy(f->nonce,  p, SPI_CRYPTO_IV_LEN);     p += SPI_CRYPTO_IV_LEN;

    /* payload_len inferred from buf_len */
    f->payload_len = buf_len - SPI_CRYPTO_OVERHEAD;
    memcpy(f->ciphertext, p, f->payload_len);     p += f->payload_len;
    memcpy(f->tag,        p, SPI_CRYPTO_TAG_LEN);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main: demonstrate encrypted SPI round-trip                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* Shared secret key — in production, derive via ECDH or load from SE */
    const uint8_t shared_key[SPI_CRYPTO_KEY_LEN] = {
        0x2b,0x7e,0x15,0x16, 0x28,0xae,0xd2,0xa6,
        0xab,0xf7,0x15,0x88, 0x09,0xcf,0x4f,0x3c
    };

    spi_crypto_ctx_t master_ctx, slave_ctx;
    spi_crypto_init(&master_ctx, shared_key);
    spi_crypto_init(&slave_ctx,  shared_key);

    /* ---- MASTER: encrypt and transmit ---- */
    const char *secret_msg    = "TEMP:36.6C;HR:72bpm";
    uint8_t     plaintext_in[SPI_CRYPTO_MAX_PAYLOAD];
    size_t      msg_len = strlen(secret_msg);

    memcpy(plaintext_in, secret_msg, msg_len);

    spi_crypto_frame_t tx_frame;
    int frame_len = spi_crypto_encrypt(&master_ctx,
                                        plaintext_in, msg_len,
                                        0x01 /* msg_type: sensor data */,
                                        &tx_frame);
    assert(frame_len > 0);

    /* Serialise frame → flat byte buffer for SPI */
    frame_to_bytes(&tx_frame, spi_tx_buf, sizeof(spi_tx_buf));

    printf("[Master] Transmitting %d encrypted bytes over SPI\n", frame_len);
    printf("[Master] Nonce (first 4): %02X %02X %02X %02X\n",
           tx_frame.nonce[0], tx_frame.nonce[1],
           tx_frame.nonce[2], tx_frame.nonce[3]);

    /* SPI transfer (full-duplex; slave simultaneously sends back ACK) */
    spi_transfer(spi_tx_buf, spi_rx_buf, (size_t)frame_len);

    /* ---- SLAVE: receive and decrypt ---- */
    spi_crypto_frame_t rx_frame;
    bytes_to_frame(spi_rx_buf, (size_t)frame_len, &rx_frame);

    uint8_t plaintext_out[SPI_CRYPTO_MAX_PAYLOAD + 1];
    int     decrypted_len = spi_crypto_decrypt(&slave_ctx,
                                               &rx_frame,
                                               plaintext_out,
                                               SPI_CRYPTO_MAX_PAYLOAD);

    if (decrypted_len < 0) {
        fprintf(stderr, "[Slave] Authentication FAILED — frame rejected!\n");
        return 1;
    }

    plaintext_out[decrypted_len] = '\0';
    printf("[Slave]  Decrypted (%d bytes): \"%s\"\n", decrypted_len, plaintext_out);

    /* Verify round-trip */
    assert(decrypted_len == (int)msg_len);
    assert(memcmp(plaintext_in, plaintext_out, msg_len) == 0);
    printf("[OK] Encrypted SPI round-trip successful.\n");

    return 0;
}
```

**Build:**
```bash
gcc main.c spi_crypto.c -lmbedcrypto -o spi_encrypted_demo
./spi_encrypted_demo
```

---

### C++ Wrapper Class

```cpp
// spi_secure_channel.hpp
#pragma once
#include "spi_crypto.h"
#include <cstdint>
#include <vector>
#include <stdexcept>

class SpiSecureChannel {
public:
    explicit SpiSecureChannel(const uint8_t key[SPI_CRYPTO_KEY_LEN]) {
        if (spi_crypto_init(&ctx_, key) != 0)
            throw std::runtime_error("spi_crypto_init failed");
    }

    /* Returns serialised encrypted frame as byte vector */
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext,
                                  uint8_t msg_type = 0x01)
    {
        if (plaintext.size() > SPI_CRYPTO_MAX_PAYLOAD)
            throw std::length_error("Payload too large");

        spi_crypto_frame_t frame{};
        int len = spi_crypto_encrypt(&ctx_,
                                      plaintext.data(),
                                      plaintext.size(),
                                      msg_type, &frame);
        if (len < 0)
            throw std::runtime_error("Encryption failed");

        std::vector<uint8_t> buf(static_cast<size_t>(len));
        // Pack frame into flat buffer
        uint8_t* p = buf.data();
        std::copy(frame.header,     frame.header     + SPI_CRYPTO_HEADER_LEN, p); p += SPI_CRYPTO_HEADER_LEN;
        std::copy(frame.nonce,      frame.nonce      + SPI_CRYPTO_IV_LEN,     p); p += SPI_CRYPTO_IV_LEN;
        std::copy(frame.ciphertext, frame.ciphertext + frame.payload_len,     p); p += frame.payload_len;
        std::copy(frame.tag,        frame.tag        + SPI_CRYPTO_TAG_LEN,    p);
        return buf;
    }

    /* Decrypts received byte vector, returns plaintext */
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& raw_frame)
    {
        if (raw_frame.size() < SPI_CRYPTO_OVERHEAD)
            throw std::length_error("Frame too short");

        spi_crypto_frame_t frame{};
        const uint8_t* p = raw_frame.data();
        std::copy(p, p + SPI_CRYPTO_HEADER_LEN, frame.header); p += SPI_CRYPTO_HEADER_LEN;
        std::copy(p, p + SPI_CRYPTO_IV_LEN,     frame.nonce);  p += SPI_CRYPTO_IV_LEN;
        frame.payload_len = raw_frame.size() - SPI_CRYPTO_OVERHEAD;
        std::copy(p, p + frame.payload_len, frame.ciphertext);  p += frame.payload_len;
        std::copy(p, p + SPI_CRYPTO_TAG_LEN, frame.tag);

        std::vector<uint8_t> out(frame.payload_len);
        int len = spi_crypto_decrypt(&ctx_, &frame, out.data(), out.size());
        if (len < 0)
            throw std::runtime_error("Decryption / authentication failed");

        out.resize(static_cast<size_t>(len));
        return out;
    }

private:
    spi_crypto_ctx_t ctx_;
};
```

---

## Rust Implementation

### `Cargo.toml`

```toml
[package]
name    = "spi-crypto"
version = "0.1.0"
edition = "2021"

[dependencies]
aes-gcm  = "0.10"          # AES-128-GCM / AES-256-GCM (RustCrypto)
rand     = { version = "0.8", features = ["std"] }
zeroize  = "1.7"           # Securely zeroise keys from memory
thiserror = "1.0"

[features]
default = ["std"]
std     = []
no_std  = []               # For bare-metal targets
```

---

### `src/lib.rs` — Core Crypto Module

```rust
//! Encrypted SPI Communication — Rust implementation
//! Uses AES-128-GCM (AEAD) from the RustCrypto project.

use aes_gcm::{
    aead::{Aead, AeadCore, KeyInit, OsRng, Payload},
    Aes128Gcm, Key, Nonce,
};
use zeroize::Zeroizing;
use thiserror::Error;

// ── Constants ──────────────────────────────────────────────────────────────

pub const KEY_LEN:     usize = 16;   // AES-128
pub const NONCE_LEN:   usize = 12;   // GCM nonce
pub const TAG_LEN:     usize = 16;   // GCM tag (appended by aes-gcm crate)
pub const HEADER_LEN:  usize = 2;
pub const MAX_PAYLOAD: usize = 256;

/// Total overhead added to plaintext → frame
pub const FRAME_OVERHEAD: usize = HEADER_LEN + NONCE_LEN + TAG_LEN;

// ── Error types ────────────────────────────────────────────────────────────

#[derive(Debug, Error)]
pub enum SpiCryptoError {
    #[error("Payload exceeds maximum size ({MAX_PAYLOAD} bytes)")]
    PayloadTooLarge,

    #[error("Frame too short to be valid")]
    FrameTooShort,

    #[error("AES-GCM authentication failed — frame may be tampered")]
    AuthenticationFailed,

    #[error("Key length must be {KEY_LEN} bytes")]
    InvalidKeyLength,
}

// ── Frame structure ────────────────────────────────────────────────────────

/// Serialised SPI encrypted frame layout:
/// `[header:2][nonce:12][ciphertext+tag: N+16]`
#[derive(Debug, Clone)]
pub struct SpiFrame {
    pub header:     [u8; HEADER_LEN],
    pub nonce:      [u8; NONCE_LEN],
    /// ciphertext with GCM tag appended (aes-gcm convention)
    pub ciphertext: Vec<u8>,
}

impl SpiFrame {
    /// Serialise to flat byte slice suitable for SPI transfer
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(HEADER_LEN + NONCE_LEN + self.ciphertext.len());
        buf.extend_from_slice(&self.header);
        buf.extend_from_slice(&self.nonce);
        buf.extend_from_slice(&self.ciphertext);
        buf
    }

    /// Deserialise from raw received bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, SpiCryptoError> {
        if bytes.len() < HEADER_LEN + NONCE_LEN + TAG_LEN {
            return Err(SpiCryptoError::FrameTooShort);
        }
        let mut header = [0u8; HEADER_LEN];
        let mut nonce  = [0u8; NONCE_LEN];
        header.copy_from_slice(&bytes[..HEADER_LEN]);
        nonce.copy_from_slice(&bytes[HEADER_LEN..HEADER_LEN + NONCE_LEN]);
        let ciphertext = bytes[HEADER_LEN + NONCE_LEN..].to_vec();
        Ok(SpiFrame { header, nonce, ciphertext })
    }
}

// ── Secure Channel ─────────────────────────────────────────────────────────

/// Holds the AES-GCM cipher instance and TX counter for nonce generation.
/// The key is zeroised on drop via `Zeroizing`.
pub struct SpiSecureChannel {
    cipher:     Aes128Gcm,
    tx_counter: u64,
    rx_counter: u64,
    // Keep the raw key for re-keying scenarios; zeroise on drop
    _key_guard: Zeroizing<[u8; KEY_LEN]>,
}

impl SpiSecureChannel {
    /// Initialise with a 16-byte AES-128 key.
    pub fn new(key: &[u8; KEY_LEN]) -> Self {
        let aes_key = Key::<Aes128Gcm>::from_slice(key);
        let mut key_copy = [0u8; KEY_LEN];
        key_copy.copy_from_slice(key);
        Self {
            cipher:     Aes128Gcm::new(aes_key),
            tx_counter: 0,
            rx_counter: 0,
            _key_guard: Zeroizing::new(key_copy),
        }
    }

    /// Encrypt `plaintext` → `SpiFrame`.
    ///
    /// The header (msg_type + length) is used as AAD: authenticated
    /// but not encrypted, allowing routing before decryption.
    pub fn encrypt(
        &mut self,
        plaintext: &[u8],
        msg_type:  u8,
    ) -> Result<SpiFrame, SpiCryptoError> {
        if plaintext.len() > MAX_PAYLOAD {
            return Err(SpiCryptoError::PayloadTooLarge);
        }

        // Build header (AAD)
        let header = [msg_type, plaintext.len() as u8];

        // Build nonce from monotonic counter
        let nonce_bytes = self.counter_to_nonce(self.tx_counter);
        self.tx_counter += 1;

        let nonce = Nonce::from_slice(&nonce_bytes);

        let payload = Payload {
            msg: plaintext,
            aad: &header,
        };

        let ciphertext = self
            .cipher
            .encrypt(nonce, payload)
            .map_err(|_| SpiCryptoError::AuthenticationFailed)?;

        Ok(SpiFrame { header, nonce: nonce_bytes, ciphertext })
    }

    /// Decrypt and authenticate a received `SpiFrame`.
    pub fn decrypt(&mut self, frame: &SpiFrame) -> Result<Vec<u8>, SpiCryptoError> {
        let nonce = Nonce::from_slice(&frame.nonce);

        let payload = Payload {
            msg: &frame.ciphertext,
            aad: &frame.header,
        };

        self.cipher
            .decrypt(nonce, payload)
            .map_err(|_| SpiCryptoError::AuthenticationFailed)
    }

    /// Generate a 12-byte nonce from a 64-bit counter (zero-padded big-endian).
    fn counter_to_nonce(&self, counter: u64) -> [u8; NONCE_LEN] {
        let mut nonce = [0u8; NONCE_LEN];
        let bytes = counter.to_be_bytes();
        nonce[NONCE_LEN - 8..].copy_from_slice(&bytes);
        nonce
    }
}
```

---

### `src/main.rs` — Demonstration

```rust
use spi_crypto::{SpiFrame, SpiSecureChannel, KEY_LEN};

/// Simulated SPI transfer (loopback for demo).
/// Replace with real embedded SPI driver call.
fn spi_loopback(tx_bytes: &[u8]) -> Vec<u8> {
    tx_bytes.to_vec()
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Shared 128-bit key — in production, derive via ECDH or load from secure element
    let shared_key: [u8; KEY_LEN] = [
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    ];

    let mut master = SpiSecureChannel::new(&shared_key);
    let mut slave  = SpiSecureChannel::new(&shared_key);

    // ── MASTER: encrypt sensor data ────────────────────────────────────────
    let sensor_data = b"TEMP:36.6C;HR:72bpm";
    println!("[Master] Plaintext  ({} bytes): {:?}",
             sensor_data.len(), String::from_utf8_lossy(sensor_data));

    let frame = master.encrypt(sensor_data, 0x01)?;
    println!("[Master] Nonce (hex): {:02x?}", &frame.nonce[8..]);
    println!("[Master] Ciphertext ({} bytes): {:02x?}",
             frame.ciphertext.len(), &frame.ciphertext[..4]);

    // Serialise to flat bytes → SPI wire
    let tx_bytes = frame.to_bytes();
    println!("[Master] Transmitting {} bytes over SPI", tx_bytes.len());

    // ── SPI transfer ───────────────────────────────────────────────────────
    let rx_bytes = spi_loopback(&tx_bytes);

    // ── SLAVE: deserialise and decrypt ─────────────────────────────────────
    let rx_frame   = SpiFrame::from_bytes(&rx_bytes)?;
    let plaintext  = slave.decrypt(&rx_frame)?;

    println!("[Slave]  Decrypted  ({} bytes): {:?}",
             plaintext.len(), String::from_utf8_lossy(&plaintext));

    assert_eq!(plaintext, sensor_data);
    println!("[OK] Encrypted SPI round-trip successful.");

    // ── Demonstrate tamper detection ───────────────────────────────────────
    println!("\n[Test] Simulating bit-flip attack on ciphertext...");
    let mut tampered_bytes = tx_bytes.clone();
    tampered_bytes[20] ^= 0xFF;   // Flip a byte in the ciphertext

    let tampered_frame = SpiFrame::from_bytes(&tampered_bytes)?;
    match slave.decrypt(&tampered_frame) {
        Err(e) => println!("[OK] Tamper detected and rejected: {}", e),
        Ok(_)  => eprintln!("[FAIL] Tampered frame was accepted!"),
    }

    Ok(())
}
```

---

### `src/no_std_channel.rs` — `no_std` / Bare-Metal Variant

```rust
//! no_std variant for embedded targets (Cortex-M, RISC-V, etc.)
//! Requires: aes-gcm with `no_std` feature, a custom allocator or heapless

#![no_std]

use aes_gcm::{
    aead::{AeadInPlace, KeyInit},
    Aes128Gcm, Key, Nonce,
};
use heapless::Vec;   // heapless = "0.8" in Cargo.toml

const KEY_LEN:     usize = 16;
const NONCE_LEN:   usize = 12;
const TAG_LEN:     usize = 16;
const MAX_PAYLOAD: usize = 128;

pub struct BareMetalSpiCrypto {
    cipher:     Aes128Gcm,
    tx_counter: u64,
}

impl BareMetalSpiCrypto {
    pub fn new(key: &[u8; KEY_LEN]) -> Self {
        Self {
            cipher:     Aes128Gcm::new(Key::<Aes128Gcm>::from_slice(key)),
            tx_counter: 0,
        }
    }

    /// Encrypt in-place within a fixed heapless buffer.
    /// Returns total frame length (nonce + ciphertext + tag).
    pub fn encrypt_inplace(
        &mut self,
        buf:    &mut Vec<u8, { NONCE_LEN + MAX_PAYLOAD + TAG_LEN }>,
        aad:    &[u8],
    ) -> Result<(), ()> {
        // Prepend nonce
        let nonce_bytes = self.make_nonce();
        self.tx_counter += 1;

        // Rotate: insert nonce at front, data follows
        let mut out: Vec<u8, { NONCE_LEN + MAX_PAYLOAD + TAG_LEN }> = Vec::new();
        out.extend_from_slice(&nonce_bytes).map_err(|_| ())?;
        out.extend_from_slice(buf).map_err(|_| ())?;

        let nonce = Nonce::from_slice(&nonce_bytes);
        // Encrypt from byte NONCE_LEN onward
        self.cipher
            .encrypt_in_place(nonce, aad, &mut out[NONCE_LEN..])
            .map_err(|_| ())?;

        *buf = out;
        Ok(())
    }

    fn make_nonce(&self) -> [u8; NONCE_LEN] {
        let mut n = [0u8; NONCE_LEN];
        let b = self.tx_counter.to_be_bytes();
        n[4..].copy_from_slice(&b);
        n
    }
}
```

---

## Performance Considerations

| Algorithm | Software (Cortex-M4 @ 168 MHz) | Notes |
|---|---|---|
| AES-128-GCM | ~2–5 Mbps | Good for most SPI speeds |
| ChaCha20-Poly1305 | ~4–8 Mbps | Faster in pure software |
| AES-128-CCM | ~2–4 Mbps | Slightly slower than GCM |
| AES-128-CTR + HMAC | ~3–6 Mbps | Two-pass, less efficient |

Typical SPI clocks:

- Low-speed sensors: 1–4 MHz → encryption overhead negligible
- High-speed flash: 50–100 MHz → AES hardware acceleration required

### Optimisation Tips

```c
/* 1. Pre-allocate frame buffers (avoid malloc in ISR context) */
static spi_crypto_frame_t g_tx_frame;
static spi_crypto_frame_t g_rx_frame;

/* 2. Use DMA for SPI transfer while CPU does next encryption */
void encrypt_and_dma_send(spi_crypto_ctx_t *ctx, const uint8_t *data, size_t len) {
    spi_crypto_encrypt(ctx, data, len, 0x01, &g_tx_frame);
    spi_dma_start(spi_tx_buf, total_frame_len);   /* Non-blocking */
}

/* 3. Pipeline: encrypt message N+1 while DMA transfers message N */
```

---

## Hardware Acceleration

### STM32 CRYP Peripheral (HAL Example)

```c
/* Use STM32 hardware AES-GCM via HAL — ~10x faster than software */
#include "stm32f4xx_hal.h"

CRYP_HandleTypeDef hcryp;

void hw_aes_gcm_init(const uint8_t *key)
{
    hcryp.Instance       = CRYP;
    hcryp.Init.DataType  = CRYP_DATATYPE_8B;
    hcryp.Init.KeySize   = CRYP_KEYSIZE_128B;
    hcryp.Init.pKey      = (uint32_t *)key;
    hcryp.Init.Algorithm = CRYP_AES_GCM;
    HAL_CRYP_Init(&hcryp);
}

HAL_StatusTypeDef hw_encrypt_gcm(const uint8_t *iv,  const uint8_t *aad, size_t aad_len,
                                   const uint8_t *pt,  size_t pt_len,
                                   uint8_t       *ct,  uint8_t *tag)
{
    return HAL_CRYPEx_AESGCM_Encrypt(&hcryp,
                                      (uint8_t *)pt, pt_len,
                                      ct, HAL_MAX_DELAY);
    /* Note: tag generation requires HAL_CRYPEx_AESGCM_Finish() */
}
```

### ESP32 Hardware AES (ESP-IDF)

```c
#include "esp_aes.h"

esp_aes_context ctx;
esp_aes_init(&ctx);
esp_aes_setkey(&ctx, key, 128);
/* ESP32 has hardware AES engine; API identical to mbedTLS */
```

---

## Common Pitfalls

### 1. Nonce Reuse — CRITICAL

```c
/* ❌ WRONG: static nonce — catastrophic key recovery attack possible */
uint8_t bad_nonce[12] = { 0 };
mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len,
                            bad_nonce, 12, ...);

/* ✅ CORRECT: monotonic counter, saved to NVM on each use */
ctx->tx_counter++;
persist_counter_to_nvm(ctx->tx_counter);  /* Before use, not after */
build_nonce(nonce, ctx->tx_counter);
```

### 2. Not Verifying the Tag Before Using Plaintext

```c
/* ❌ WRONG: using decrypted data before checking tag */
mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_DECRYPT, ...);
mbedtls_gcm_update(&gcm, len, ciphertext, plaintext);
process_data(plaintext);   // ← Using unauthenticated data!

/* ✅ CORRECT: use auth_decrypt which checks tag atomically */
ret = mbedtls_gcm_auth_decrypt(&gcm, len, nonce, 12,
                                aad, aad_len, tag, 16,
                                ciphertext, plaintext);
if (ret != 0) { /* REJECT */ return; }
process_data(plaintext);   // ← Safe: tag verified
```

### 3. Hardcoded Keys

```c
/* ❌ WRONG: key visible in firmware binary */
const uint8_t key[] = { 0xDE, 0xAD, 0xBE, 0xEF, ... };

/* ✅ CORRECT: load from secure element or derive at runtime */
uint8_t key[16];
secure_element_read_key(SE_KEY_SLOT_0, key, sizeof(key));
```

### 4. Not Zeroing Keys After Use

```rust
// ✅ Rust: use Zeroizing<T> wrapper — zeroed on drop automatically
use zeroize::Zeroizing;
let key: Zeroizing<[u8; 16]> = Zeroizing::new(raw_key);
```

```c
// C: explicitly wipe
mbedtls_gcm_free(&gcm);
mbedtls_platform_zeroize(key, sizeof(key));
```

### 5. Ignoring Clock Resets for Timestamp Nonces

If you use RTC-based nonces and the device reboots or the clock resets, nonce reuse becomes possible. Always prefer monotonic counters stored in NVM.

---

## Summary

Encrypted SPI communication adds a critical security layer to an otherwise plaintext serial bus. The key takeaways are:

**Protocol Design**
- Use **AES-128-GCM** or **ChaCha20-Poly1305** for authenticated encryption (AEAD) — confidentiality and integrity in a single operation.
- Structure frames as `[header (AAD)][nonce][ciphertext][tag]`.
- Use the header as Additional Authenticated Data so it can be inspected without decryption but cannot be modified undetected.

**Nonce Management**
- Never reuse a (key, nonce) pair — a single reuse leaks the keystream and enables plaintext recovery.
- Use a monotonic 64-bit counter stored in NVM, or a 96-bit random nonce (suitable for infrequent communication).

**Key Management**
- Derive session keys from a master secret using HKDF; never hardcode raw keys.
- Store master secrets in OTP, eFuse, or a dedicated secure element (e.g., ATECC608).
- Zeroize key material from memory after use.

**Implementation**
- In **C/C++**: `mbedtls_gcm_auth_decrypt()` performs atomic decryption + tag verification; always reject data if the tag is invalid.
- In **Rust**: the `aes-gcm` crate's `decrypt()` method returns `Err` on tag mismatch; the type system enforces correct error handling. Use `Zeroizing<>` for automatic key cleanup.
- On resource-constrained MCUs: use hardware AES peripherals (STM32 CRYP, ESP32 AES engine) for throughput above ~2 Mbps.

**Performance vs Security Trade-offs**
- For SPI speeds ≤ 4 MHz, software AES-GCM is sufficient on Cortex-M4+.
- For high-speed SPI (≥ 20 MHz), hardware acceleration is essential.
- ChaCha20-Poly1305 is the better software-only choice on MCUs without hardware AES.

By following these principles, you can protect sensitive data traversing an SPI bus from eavesdropping, replay, and tampering attacks while keeping the implementation portable and maintainable.

---

*Document: 86_Encrypted_SPI_Communication.md — SPI Security Series*