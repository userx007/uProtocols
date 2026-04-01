# 68. Encrypted Communication over I2C

**Conceptual coverage** — Why I2C needs encryption (physical probing, MitM, counterfeit devices), the threat model, and a comparison of encryption approaches (AES-CBC, AES-GCM, ChaCha20-Poly1305, ASCON).

**C/C++ code examples:**
- AES-128-CBC encrypt/decrypt with PKCS#7 padding using `tiny-AES-c`
- ChaCha20 stream cipher (RFC 7539 compliant, from scratch, no padding)
- HMAC-SHA256 with constant-time MAC comparison
- A complete **Encrypt-then-MAC secure I2C frame** builder and parser with anti-replay counter logic

**Rust code examples:**
- Full `no_std` module using RustCrypto crates (`aes`, `cbc`, `hmac`, `sha2`, `heapless`)
- `SecureI2cContext` struct with `build_frame()` and `parse_frame()` methods
- `embedded-hal` I2C integration example for master read/write

**Operational guidance** — Key exchange strategies (OTP, Secure Element, ECDH), nonce/counter persistence across power cycles, performance benchmarks per cipher, and a detailed list of common security pitfalls to avoid.

### Adding Encryption Layers to I2C Data Transfers for Sensitive Applications

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Encrypt I2C?](#why-encrypt-i2c)
3. [I2C Protocol Recap](#i2c-protocol-recap)
4. [Threat Model](#threat-model)
5. [Encryption Approaches](#encryption-approaches)
6. [AES-128 CBC Implementation in C/C++](#aes-128-cbc-implementation-in-cc)
7. [ChaCha20 Stream Cipher in C](#chacha20-stream-cipher-in-c)
8. [HMAC Message Authentication in C](#hmac-message-authentication-in-c)
9. [Full Secure I2C Frame: C/C++ Example](#full-secure-i2c-frame-cc-example)
10. [Encrypted I2C in Rust](#encrypted-i2c-in-rust)
11. [Key Exchange Considerations](#key-exchange-considerations)
12. [Replay Attack Prevention](#replay-attack-prevention)
13. [Performance Considerations](#performance-considerations)
14. [Security Pitfalls](#security-pitfalls)
15. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) is a synchronous, half-duplex serial communication protocol widely used to connect microcontrollers with sensors, EEPROMs, displays, and other peripherals. By design, I2C offers **no built-in security**—no authentication, no confidentiality, and no integrity protection. In many hobby or low-risk embedded applications this is acceptable, but in sensitive domains such as medical devices, industrial control systems, smart locks, payment terminals, and IoT security modules, transmitting plaintext over I2C is a significant vulnerability.

This document describes how to design and implement an **application-layer encryption layer** on top of I2C, covering symmetric encryption (AES, ChaCha20), message authentication codes (HMAC), nonce/counter management, and secure framing—with full code examples in **C/C++** and **Rust**.

---

## Why Encrypt I2C?

Although I2C is a short-range bus typically confined to a PCB, real-world attack vectors include:

- **Physical probing**: An attacker with board access and a logic analyzer (e.g., Saleae) can capture all I2C traffic in seconds.
- **Man-in-the-Middle (MitM)**: On a multi-master bus, a malicious device can inject or modify frames.
- **Counterfeit peripheral detection bypass**: Without authentication, a fake sensor or EEPROM can impersonate a trusted device.
- **Side-channel leakage**: Plaintext sensor readings (e.g., blood glucose, GPS, biometric) on an unencrypted bus expose sensitive user data.
- **Firmware update integrity**: Unprotected I2C firmware update paths allow malicious code injection.

The goal is to achieve **confidentiality**, **integrity**, and **authenticity** at the application layer without modifying the I2C hardware or driver stack.

---

## I2C Protocol Recap

I2C uses two lines:
- **SCL** – Serial Clock (master-driven)
- **SDA** – Serial Data (bidirectional, open-drain)

A typical transaction:
```
START | 7-bit Address | R/W | ACK | Data Byte(s) | STOP
```

Key constraints for our encryption design:
- **Packet size**: I2C has no inherent MTU, but many devices have small register maps (1–32 bytes per access). Large encrypted payloads must be chunked.
- **Latency sensitivity**: Adding crypto processing must not violate device timing requirements (clock stretching has limits).
- **No session concept**: Each I2C transaction is stateless unless the application layer provides context.
- **Address space**: Only 128 (7-bit) or 1024 (10-bit) addresses—no built-in device identity beyond the address.

---

## Threat Model

| Threat | Mitigated By |
|--------|-------------|
| Eavesdropping on bus | AES/ChaCha20 encryption |
| Data tampering | HMAC / AEAD (AES-GCM) authentication tag |
| Replay attacks | Nonce / counter incremented per message |
| Device spoofing | Pre-shared key (PSK) or certificate exchange |
| Brute-force key guessing | 128-bit or 256-bit key space |
| Weak IV/Nonce reuse | Counter mode with persistent storage (EEPROM/flash) |

**Out of scope**: Physical side-channel attacks (power analysis, EM), DPA on the crypto engine, and key provisioning security (hardware secure element recommended).

---

## Encryption Approaches

### 1. AES-128-CBC with HMAC-SHA256 (Encrypt-then-MAC)

The classical approach for embedded systems. AES-CBC provides confidentiality; HMAC-SHA256 provides integrity and authenticity. The MAC is computed over the ciphertext (Encrypt-then-MAC is the secure ordering).

**Frame structure:**
```
[ IV (16 bytes) | Ciphertext (N bytes, padded to AES block) | HMAC (32 bytes) ]
```

### 2. AES-128-GCM (AEAD – Authenticated Encryption with Associated Data)

GCM combines encryption and authentication in a single pass. It is preferred when a hardware AES accelerator is available (common on Cortex-M33, ESP32, STM32H7).

**Frame structure:**
```
[ Nonce/IV (12 bytes) | Ciphertext (N bytes) | Auth Tag (16 bytes) ]
```

### 3. ChaCha20-Poly1305

An excellent software-friendly alternative to AES-GCM. ChaCha20 is a stream cipher with no padding requirement; Poly1305 provides the authentication tag. Preferred on MCUs **without** hardware AES (e.g., AVR, older ARM Cortex-M0).

**Frame structure:**
```
[ Nonce (12 bytes) | Ciphertext (N bytes) | Poly1305 Tag (16 bytes) ]
```

### 4. Lightweight Alternatives

For severely constrained devices (< 4 KB RAM): **ASCON** (NIST Lightweight Cryptography winner, 2023), **GIFT-COFB**, or **TinyJAMBU** offer AEAD with smaller footprints.

---

## AES-128-CBC Implementation in C/C++

This example uses a minimal AES implementation suitable for embedded targets (no dynamic allocation, no OS dependency). In production, prefer **mbedTLS**, **wolfSSL**, or a hardware accelerator abstraction layer.

```c
// i2c_aes_cbc.c
// Demonstrates AES-128-CBC encryption/decryption for I2C payloads.
// Requires: tiny-AES-c (https://github.com/kokke/tiny-AES-c)
// Include: aes.h, aes.c

#include <stdint.h>
#include <string.h>
#include "aes.h"   // tiny-AES-c: struct AES_ctx, AES_init_ctx_iv(), AES_CBC_encrypt_buffer()

#define AES_KEY_SIZE     16   // AES-128
#define AES_BLOCK_SIZE   16
#define IV_SIZE          16

// Pre-shared 128-bit key (provisioned at manufacture time, stored in secure flash)
static const uint8_t PSK[AES_KEY_SIZE] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// PKCS#7 padding: pads buf to a multiple of AES_BLOCK_SIZE.
// Returns padded length, or -1 if buf is too small.
int pkcs7_pad(uint8_t *buf, size_t data_len, size_t buf_size) {
    size_t pad_len = AES_BLOCK_SIZE - (data_len % AES_BLOCK_SIZE);
    if (data_len + pad_len > buf_size) return -1;
    memset(buf + data_len, (uint8_t)pad_len, pad_len);
    return (int)(data_len + pad_len);
}

// Remove PKCS#7 padding. Returns unpadded length or -1 on error.
int pkcs7_unpad(uint8_t *buf, size_t data_len) {
    if (data_len == 0 || data_len % AES_BLOCK_SIZE != 0) return -1;
    uint8_t pad_len = buf[data_len - 1];
    if (pad_len == 0 || pad_len > AES_BLOCK_SIZE) return -1;
    for (size_t i = data_len - pad_len; i < data_len; i++) {
        if (buf[i] != pad_len) return -1; // padding oracle prevention
    }
    return (int)(data_len - pad_len);
}

/**
 * Encrypt plaintext for I2C transmission using AES-128-CBC.
 *
 * @param iv          Random or counter-based IV (16 bytes, sent in plaintext)
 * @param plaintext   Input data
 * @param pt_len      Length of plaintext
 * @param out_buf     Output buffer (must hold IV + padded ciphertext)
 * @param out_buf_sz  Size of out_buf
 * @return            Total output length (IV + ciphertext), or -1 on error
 */
int i2c_aes_encrypt(const uint8_t *iv, const uint8_t *plaintext, size_t pt_len,
                    uint8_t *out_buf, size_t out_buf_sz) {
    // Workspace: copy plaintext and apply PKCS#7 padding
    uint8_t workspace[256];  // max 240 bytes plaintext + 16 bytes padding
    size_t max_pt = sizeof(workspace) - AES_BLOCK_SIZE;

    if (pt_len > max_pt || out_buf_sz < IV_SIZE + pt_len + AES_BLOCK_SIZE) {
        return -1;
    }

    memcpy(workspace, plaintext, pt_len);
    int padded_len = pkcs7_pad(workspace, pt_len, sizeof(workspace));
    if (padded_len < 0) return -1;

    // Prepend IV to output
    memcpy(out_buf, iv, IV_SIZE);

    // Copy padded plaintext to ciphertext region of output
    memcpy(out_buf + IV_SIZE, workspace, padded_len);

    // Encrypt in-place using tiny-AES-c
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, PSK, iv);
    AES_CBC_encrypt_buffer(&ctx, out_buf + IV_SIZE, padded_len);

    return IV_SIZE + padded_len;
}

/**
 * Decrypt I2C received buffer using AES-128-CBC.
 *
 * @param in_buf      Buffer containing [IV | Ciphertext]
 * @param in_len      Total received length
 * @param plaintext   Output plaintext buffer
 * @param pt_buf_sz   Size of plaintext buffer
 * @return            Plaintext length, or -1 on error
 */
int i2c_aes_decrypt(const uint8_t *in_buf, size_t in_len,
                    uint8_t *plaintext, size_t pt_buf_sz) {
    if (in_len <= IV_SIZE || (in_len - IV_SIZE) % AES_BLOCK_SIZE != 0) {
        return -1;
    }

    const uint8_t *iv         = in_buf;
    const uint8_t *ciphertext = in_buf + IV_SIZE;
    size_t ct_len             = in_len - IV_SIZE;

    if (pt_buf_sz < ct_len) return -1;

    memcpy(plaintext, ciphertext, ct_len);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, PSK, iv);
    AES_CBC_decrypt_buffer(&ctx, plaintext, ct_len);

    return pkcs7_unpad(plaintext, ct_len);
}

// -----------------------------------------------------------------------
// Example: I2C master sends encrypted sensor command
// -----------------------------------------------------------------------
#include <stdio.h>

// Simulated HAL functions (replace with your platform's I2C driver)
extern void hal_i2c_write(uint8_t dev_addr, const uint8_t *data, size_t len);
extern int  hal_i2c_read (uint8_t dev_addr, uint8_t *data, size_t len);

// A monotonic counter stored in RTC backup register or EEPROM
static uint32_t tx_counter = 0;

void send_encrypted_command(uint8_t i2c_addr, const uint8_t *cmd, size_t cmd_len) {
    uint8_t iv[IV_SIZE] = {0};
    uint8_t out_buf[IV_SIZE + 256];

    // Build IV from counter (prevents IV reuse without true random source)
    tx_counter++;
    memcpy(iv, &tx_counter, sizeof(tx_counter));
    // Remaining 12 bytes of IV can be device serial number / session ID

    int enc_len = i2c_aes_encrypt(iv, cmd, cmd_len, out_buf, sizeof(out_buf));
    if (enc_len < 0) {
        fprintf(stderr, "Encryption failed\n");
        return;
    }

    hal_i2c_write(i2c_addr, out_buf, (size_t)enc_len);
}
```

---

## ChaCha20 Stream Cipher in C

ChaCha20 requires no padding (it is a stream cipher) and has no timing vulnerabilities due to table lookups—making it ideal for software-only implementations on MCUs without hardware AES.

```c
// chacha20_i2c.c
// Minimal ChaCha20 implementation for I2C encryption (RFC 7539 compliant)

#include <stdint.h>
#include <string.h>

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void chacha20_quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = ROTL32(*d, 16);
    *c += *d; *b ^= *c; *b = ROTL32(*b, 12);
    *a += *b; *d ^= *a; *d = ROTL32(*d,  8);
    *c += *d; *b ^= *c; *b = ROTL32(*b,  7);
}

// Generate a 64-byte keystream block (RFC 7539 ChaCha20 block function)
static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, in, 64);

    for (int i = 0; i < 10; i++) {  // 20 rounds = 10 double-rounds
        // Column rounds
        chacha20_quarter_round(&x[0], &x[4], &x[ 8], &x[12]);
        chacha20_quarter_round(&x[1], &x[5], &x[ 9], &x[13]);
        chacha20_quarter_round(&x[2], &x[6], &x[10], &x[14]);
        chacha20_quarter_round(&x[3], &x[7], &x[11], &x[15]);
        // Diagonal rounds
        chacha20_quarter_round(&x[0], &x[5], &x[10], &x[15]);
        chacha20_quarter_round(&x[1], &x[6], &x[11], &x[12]);
        chacha20_quarter_round(&x[2], &x[7], &x[ 8], &x[13]);
        chacha20_quarter_round(&x[3], &x[4], &x[ 9], &x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t val = x[i] + in[i];
        out[i * 4 + 0] = (uint8_t)(val >>  0);
        out[i * 4 + 1] = (uint8_t)(val >>  8);
        out[i * 4 + 2] = (uint8_t)(val >> 16);
        out[i * 4 + 3] = (uint8_t)(val >> 24);
    }
}

static inline uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * ChaCha20 encrypt/decrypt (same operation due to XOR symmetry).
 *
 * @param key        32-byte key (256-bit)
 * @param nonce      12-byte nonce (must be unique per key)
 * @param counter    Initial block counter (usually 1 per RFC 7539)
 * @param in         Input data
 * @param out        Output data (may alias in)
 * @param length     Data length in bytes
 */
void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                  uint32_t counter, const uint8_t *in, uint8_t *out, size_t length) {
    // Constants: "expand 32-byte k"
    uint32_t state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        load_le32(key +  0), load_le32(key +  4),
        load_le32(key +  8), load_le32(key + 12),
        load_le32(key + 16), load_le32(key + 20),
        load_le32(key + 24), load_le32(key + 28),
        counter,
        load_le32(nonce + 0), load_le32(nonce + 4), load_le32(nonce + 8)
    };

    uint8_t keystream[64];
    size_t offset = 0;

    while (length > 0) {
        chacha20_block(state, keystream);
        state[12]++;  // increment block counter

        size_t chunk = (length < 64) ? length : 64;
        for (size_t i = 0; i < chunk; i++) {
            out[offset + i] = in[offset + i] ^ keystream[i];
        }

        offset += chunk;
        length -= chunk;
    }
}

// Example usage: encrypt a 10-byte I2C payload
void chacha20_i2c_example(void) {
    const uint8_t key[32]   = { /* 32-byte pre-shared key */ 0x42 /* ... */ };
    uint8_t nonce[12]       = {0};
    static uint32_t pkt_ctr = 0;

    // Use packet counter as nonce (never repeat for same key!)
    pkt_ctr++;
    memcpy(nonce, &pkt_ctr, 4);

    uint8_t plaintext[10]  = {0x01, 0x02, 0x03, 0x04, 0x05,
                               0x06, 0x07, 0x08, 0x09, 0x0A};
    uint8_t ciphertext[10];

    chacha20_xor(key, nonce, 1, plaintext, ciphertext, sizeof(plaintext));

    // Transmit: [nonce (12 bytes)] + [ciphertext (10 bytes)]
    // hal_i2c_write(SLAVE_ADDR, nonce, 12);
    // hal_i2c_write(SLAVE_ADDR, ciphertext, 10);
}
```

---

## HMAC Message Authentication in C

Encryption alone does not prevent tampering. HMAC-SHA256 is appended to every frame so the receiver can verify integrity and authenticity before decrypting.

```c
// hmac_sha256.c
// Simple HMAC-SHA256 for I2C frame authentication.
// Uses a minimal SHA-256 implementation (replace with mbedTLS sha256 in production).

#include <stdint.h>
#include <string.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_HASH_SIZE   32

// ---- Minimal SHA-256 (omitted for brevity; use mbedtls_sha256 in production) ----
extern void sha256(const uint8_t *data, size_t len, uint8_t hash[SHA256_HASH_SIZE]);

/**
 * Compute HMAC-SHA256.
 *
 * @param key      HMAC key (auth_key, separate from encryption key!)
 * @param key_len  Key length in bytes
 * @param msg      Message data
 * @param msg_len  Message length
 * @param out      Output HMAC (32 bytes)
 */
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out[SHA256_HASH_SIZE]) {
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t temp[SHA256_HASH_SIZE];

    // Derive padded key
    memset(k_pad, 0, SHA256_BLOCK_SIZE);
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, k_pad);  // Hash long keys
    } else {
        memcpy(k_pad, key, key_len);
    }

    // Inner hash: H((K XOR ipad) || message)
    uint8_t inner_data[SHA256_BLOCK_SIZE + msg_len]; // VLA or dynamic alloc
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        inner_data[i] = k_pad[i] ^ 0x36;
    }
    memcpy(inner_data + SHA256_BLOCK_SIZE, msg, msg_len);
    sha256(inner_data, SHA256_BLOCK_SIZE + msg_len, temp);

    // Outer hash: H((K XOR opad) || inner_hash)
    uint8_t outer_data[SHA256_BLOCK_SIZE + SHA256_HASH_SIZE];
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        outer_data[i] = k_pad[i] ^ 0x5c;
    }
    memcpy(outer_data + SHA256_BLOCK_SIZE, temp, SHA256_HASH_SIZE);
    sha256(outer_data, SHA256_BLOCK_SIZE + SHA256_HASH_SIZE, out);
}

/**
 * Constant-time comparison to prevent timing attacks on MAC verification.
 * Returns 0 if equal, non-zero otherwise.
 */
int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (int)diff;
}
```

> **Important**: Use two separate keys—one for encryption (enc_key) and one for authentication (auth_key). Deriving both from a single master key using HKDF is recommended.

---

## Full Secure I2C Frame: C/C++ Example

This integrates AES-CBC + HMAC into a complete master-slave I2C session.

```c
// secure_i2c_frame.c
// Complete Encrypt-then-MAC frame for I2C secure communication.

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PAYLOAD     64
#define IV_LEN          16
#define MAC_LEN         32
#define COUNTER_LEN      4
#define HDR_LEN         (COUNTER_LEN + IV_LEN)  // 20 bytes header

// Frame layout (transmitted over I2C):
// [ Counter (4B) | IV (16B) | Ciphertext (N bytes, AES block aligned) | HMAC (32B) ]

typedef struct {
    uint8_t counter[COUNTER_LEN];         // Big-endian packet counter (anti-replay)
    uint8_t iv[IV_LEN];                    // AES-CBC IV
    uint8_t ciphertext[MAX_PAYLOAD + 16];  // Encrypted payload (with PKCS#7 padding)
    uint16_t ct_len;                       // Ciphertext length
    uint8_t mac[MAC_LEN];                  // HMAC-SHA256 over [counter|iv|ciphertext]
} SecureI2CFrame;

extern int  i2c_aes_encrypt(const uint8_t *iv, const uint8_t *pt, size_t pt_len,
                              uint8_t *out, size_t out_sz);
extern int  i2c_aes_decrypt(const uint8_t *in, size_t in_len,
                              uint8_t *pt, size_t pt_sz);
extern void hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[32]);
extern int  ct_memcmp(const uint8_t *a, const uint8_t *b, size_t len);

static const uint8_t ENC_KEY[16] = { 0xAB, /* ... 16 bytes */ };
static const uint8_t AUTH_KEY[32] = { 0xCD, /* ... 32 bytes */ };

static uint32_t g_tx_counter = 0;
static uint32_t g_rx_counter = 0;  // Last accepted counter from peer

// Build and serialize a secure I2C frame into out_bytes.
// Returns total byte count to transmit, or -1 on error.
int secure_frame_build(const uint8_t *payload, size_t payload_len,
                       uint8_t *out_bytes, size_t out_sz) {
    if (payload_len > MAX_PAYLOAD) return -1;

    g_tx_counter++;
    uint8_t counter_be[4] = {
        (uint8_t)(g_tx_counter >> 24), (uint8_t)(g_tx_counter >> 16),
        (uint8_t)(g_tx_counter >>  8), (uint8_t)(g_tx_counter)
    };

    // Build IV from counter + device ID (avoids full TRNG dependency)
    uint8_t iv[IV_LEN] = {0};
    memcpy(iv, counter_be, 4);
    // Bytes [4..15] can encode device serial or session token

    // Encrypt
    uint8_t enc_buf[IV_LEN + MAX_PAYLOAD + 16];
    int enc_total = i2c_aes_encrypt(iv, payload, payload_len, enc_buf, sizeof(enc_buf));
    if (enc_total < 0) return -1;

    // enc_buf = [iv (16B) | ciphertext (enc_total - 16 B)]
    size_t ct_len = (size_t)enc_total - IV_LEN;

    // Compute HMAC over [counter | ciphertext] (Encrypt-then-MAC)
    uint8_t mac_input[COUNTER_LEN + ct_len];
    memcpy(mac_input, counter_be, COUNTER_LEN);
    memcpy(mac_input + COUNTER_LEN, enc_buf + IV_LEN, ct_len);

    uint8_t mac[MAC_LEN];
    hmac_sha256(AUTH_KEY, sizeof(AUTH_KEY), mac_input, sizeof(mac_input), mac);

    // Serialize: [counter | iv | ciphertext | mac]
    size_t total = COUNTER_LEN + IV_LEN + ct_len + MAC_LEN;
    if (out_sz < total) return -1;

    size_t pos = 0;
    memcpy(out_bytes + pos, counter_be, COUNTER_LEN); pos += COUNTER_LEN;
    memcpy(out_bytes + pos, iv, IV_LEN);              pos += IV_LEN;
    memcpy(out_bytes + pos, enc_buf + IV_LEN, ct_len); pos += ct_len;
    memcpy(out_bytes + pos, mac, MAC_LEN);

    return (int)total;
}

// Receive and verify a secure I2C frame.
// Returns decrypted payload length, or -1 on error/authentication failure.
int secure_frame_parse(const uint8_t *in_bytes, size_t in_len,
                       uint8_t *payload, size_t payload_sz) {
    if (in_len < COUNTER_LEN + IV_LEN + MAC_LEN + AES_BLOCK_SIZE) return -1;

    const uint8_t *counter_be = in_bytes;
    const uint8_t *iv         = in_bytes + COUNTER_LEN;
    size_t ct_len             = in_len - COUNTER_LEN - IV_LEN - MAC_LEN;
    const uint8_t *ciphertext = in_bytes + COUNTER_LEN + IV_LEN;
    const uint8_t *recv_mac   = in_bytes + COUNTER_LEN + IV_LEN + ct_len;

    // 1. Verify HMAC first (fail fast on tampered frames)
    uint8_t mac_input[COUNTER_LEN + ct_len];
    memcpy(mac_input, counter_be, COUNTER_LEN);
    memcpy(mac_input + COUNTER_LEN, ciphertext, ct_len);

    uint8_t expected_mac[MAC_LEN];
    hmac_sha256(AUTH_KEY, sizeof(AUTH_KEY), mac_input, sizeof(mac_input), expected_mac);

    if (ct_memcmp(expected_mac, recv_mac, MAC_LEN) != 0) {
        return -1;  // Authentication FAILED — discard frame
    }

    // 2. Anti-replay: verify counter is strictly greater than last accepted
    uint32_t recv_ctr = ((uint32_t)counter_be[0] << 24) | ((uint32_t)counter_be[1] << 16) |
                        ((uint32_t)counter_be[2] <<  8) |  (uint32_t)counter_be[3];
    if (recv_ctr <= g_rx_counter) {
        return -1;  // Replay attack detected
    }
    g_rx_counter = recv_ctr;

    // 3. Decrypt
    uint8_t full_ct[IV_LEN + ct_len];
    memcpy(full_ct, iv, IV_LEN);
    memcpy(full_ct + IV_LEN, ciphertext, ct_len);

    return i2c_aes_decrypt(full_ct, IV_LEN + ct_len, payload, payload_sz);
}
```

---

## Encrypted I2C in Rust

Rust's type system and memory safety guarantees make it an excellent choice for embedded crypto. The `embedded-hal` crate provides the I2C abstraction; `aes`, `cbc`, `hmac`, and `sha2` crates from the [RustCrypto](https://github.com/RustCrypto) project provide audited, no-std compatible cryptography.

### Cargo.toml Dependencies

```toml
[dependencies]
embedded-hal = "1.0"
aes          = { version = "0.8", default-features = false }
cbc          = { version = "0.1", default-features = false }
hmac         = { version = "0.12", default-features = false }
sha2         = { version = "0.10", default-features = false }
cipher       = "0.4"
generic-array = "0.14"
heapless     = "0.8"   # fixed-size stack-allocated collections (no_std)
```

### Encryption Module (`src/secure_i2c.rs`)

```rust
//! secure_i2c.rs — Encrypted I2C frame builder/parser for embedded Rust.
//! Targets no_std environments (Cortex-M, RISC-V, etc.)

#![no_std]

use aes::Aes128;
use cbc::{Encryptor, Decryptor};
use cipher::{
    block_padding::Pkcs7,
    BlockEncryptMut, BlockDecryptMut, KeyIvInit,
};
use hmac::{Hmac, Mac};
use sha2::Sha256;
use heapless::Vec;

type HmacSha256 = Hmac<Sha256>;
type Aes128CbcEnc = Encryptor<Aes128>;
type Aes128CbcDec = Decryptor<Aes128>;

pub const KEY_LEN:       usize = 16;   // AES-128
pub const AUTH_KEY_LEN:  usize = 32;   // HMAC-SHA256
pub const IV_LEN:        usize = 16;
pub const MAC_LEN:       usize = 32;
pub const COUNTER_LEN:   usize = 4;
pub const MAX_PAYLOAD:   usize = 64;
pub const MAX_FRAME:     usize = COUNTER_LEN + IV_LEN + MAX_PAYLOAD + 16 + MAC_LEN;

#[derive(Debug, PartialEq)]
pub enum SecureI2cError {
    AuthenticationFailed,
    ReplayDetected,
    EncryptionFailed,
    DecryptionFailed,
    BufferTooSmall,
    InvalidFrame,
}

pub struct SecureI2cContext {
    enc_key:    [u8; KEY_LEN],
    auth_key:   [u8; AUTH_KEY_LEN],
    tx_counter: u32,
    rx_counter: u32,
}

impl SecureI2cContext {
    pub fn new(enc_key: [u8; KEY_LEN], auth_key: [u8; AUTH_KEY_LEN]) -> Self {
        SecureI2cContext {
            enc_key,
            auth_key,
            tx_counter: 0,
            rx_counter: 0,
        }
    }

    /// Build an encrypted, authenticated frame ready for I2C transmission.
    ///
    /// Frame layout: [counter(4) | IV(16) | ciphertext(N) | HMAC(32)]
    pub fn build_frame(
        &mut self,
        payload: &[u8],
    ) -> Result<Vec<u8, MAX_FRAME>, SecureI2cError> {
        if payload.len() > MAX_PAYLOAD {
            return Err(SecureI2cError::BufferTooSmall);
        }

        self.tx_counter += 1;
        let counter_be = self.tx_counter.to_be_bytes();

        // Construct IV from counter (bytes 0..4) + zeros (bytes 4..16)
        let mut iv = [0u8; IV_LEN];
        iv[..4].copy_from_slice(&counter_be);

        // Encrypt with AES-128-CBC + PKCS#7
        let mut ct_buf = [0u8; MAX_PAYLOAD + 16];
        ct_buf[..payload.len()].copy_from_slice(payload);

        let pt_len = payload.len();
        let encryptor = Aes128CbcEnc::new(
            self.enc_key.as_slice().into(),
            iv.as_slice().into(),
        );

        // encrypt_padded_mut returns ciphertext slice within ct_buf
        let ct_slice = encryptor
            .encrypt_padded_mut::<Pkcs7>(&mut ct_buf, pt_len)
            .map_err(|_| SecureI2cError::EncryptionFailed)?;
        let ct_len = ct_slice.len();

        // Compute HMAC over [counter | ciphertext]
        let mut mac = HmacSha256::new_from_slice(&self.auth_key)
            .map_err(|_| SecureI2cError::EncryptionFailed)?;
        mac.update(&counter_be);
        mac.update(&ct_buf[..ct_len]);
        let mac_bytes = mac.finalize().into_bytes();

        // Assemble frame
        let mut frame: Vec<u8, MAX_FRAME> = Vec::new();
        frame.extend_from_slice(&counter_be).map_err(|_| SecureI2cError::BufferTooSmall)?;
        frame.extend_from_slice(&iv).map_err(|_| SecureI2cError::BufferTooSmall)?;
        frame.extend_from_slice(&ct_buf[..ct_len]).map_err(|_| SecureI2cError::BufferTooSmall)?;
        frame.extend_from_slice(&mac_bytes).map_err(|_| SecureI2cError::BufferTooSmall)?;

        Ok(frame)
    }

    /// Parse, verify, and decrypt a received I2C frame.
    ///
    /// Returns decrypted payload on success, or an error.
    pub fn parse_frame(
        &mut self,
        frame: &[u8],
    ) -> Result<Vec<u8, MAX_PAYLOAD>, SecureI2cError> {
        let min_len = COUNTER_LEN + IV_LEN + 16 + MAC_LEN; // 16 = min 1 AES block
        if frame.len() < min_len {
            return Err(SecureI2cError::InvalidFrame);
        }

        let counter_be = &frame[..COUNTER_LEN];
        let iv         = &frame[COUNTER_LEN..COUNTER_LEN + IV_LEN];
        let ct_end     = frame.len() - MAC_LEN;
        let ciphertext = &frame[COUNTER_LEN + IV_LEN..ct_end];
        let recv_mac   = &frame[ct_end..];

        // 1. Verify HMAC (constant-time)
        let mut mac = HmacSha256::new_from_slice(&self.auth_key)
            .map_err(|_| SecureI2cError::AuthenticationFailed)?;
        mac.update(counter_be);
        mac.update(ciphertext);
        mac.verify_slice(recv_mac)
            .map_err(|_| SecureI2cError::AuthenticationFailed)?;

        // 2. Anti-replay counter check
        let recv_ctr = u32::from_be_bytes(
            counter_be.try_into().map_err(|_| SecureI2cError::InvalidFrame)?
        );
        if recv_ctr <= self.rx_counter {
            return Err(SecureI2cError::ReplayDetected);
        }
        self.rx_counter = recv_ctr;

        // 3. Decrypt
        let mut ct_buf = [0u8; MAX_PAYLOAD + 16];
        let ct_len = ciphertext.len();
        ct_buf[..ct_len].copy_from_slice(ciphertext);

        let decryptor = Aes128CbcDec::new(
            self.enc_key.as_slice().into(),
            iv.into(),
        );
        let plaintext = decryptor
            .decrypt_padded_mut::<Pkcs7>(&mut ct_buf[..ct_len])
            .map_err(|_| SecureI2cError::DecryptionFailed)?;

        let mut out: Vec<u8, MAX_PAYLOAD> = Vec::new();
        out.extend_from_slice(plaintext)
            .map_err(|_| SecureI2cError::BufferTooSmall)?;
        Ok(out)
    }
}
```

### I2C Integration with `embedded-hal` (`src/main.rs`)

```rust
//! main.rs — Using SecureI2cContext with embedded-hal I2C peripheral.

#![no_std]
#![no_main]

use embedded_hal::i2c::I2c;
use secure_i2c::{SecureI2cContext, KEY_LEN, AUTH_KEY_LEN};

const SLAVE_ADDR: u8 = 0x48;

// Provisioned at manufacture time, stored in secure flash / OTP
const ENC_KEY:  [u8; KEY_LEN]      = [0x2b, 0x7e, 0x15, 0x16, /* ... */];
const AUTH_KEY: [u8; AUTH_KEY_LEN] = [0xcd, 0xef, 0x01, 0x23, /* ... */];

fn run<I: I2c>(i2c: &mut I) {
    let mut ctx = SecureI2cContext::new(ENC_KEY, AUTH_KEY);

    // --- Master: send encrypted command ---
    let command = b"\x01\x02\x03\x04"; // e.g., sensor read request
    match ctx.build_frame(command) {
        Ok(frame) => {
            i2c.write(SLAVE_ADDR, &frame).expect("I2C write failed");
        }
        Err(e) => {
            // Handle encryption error
            let _ = e;
        }
    }

    // --- Master: receive and decrypt response ---
    let mut recv_buf = [0u8; 128];
    i2c.read(SLAVE_ADDR, &mut recv_buf).expect("I2C read failed");

    match ctx.parse_frame(&recv_buf) {
        Ok(plaintext) => {
            // Process decrypted sensor data in plaintext
            let _ = plaintext;
        }
        Err(secure_i2c::SecureI2cError::AuthenticationFailed) => {
            // Log security event, alert, increment fault counter
        }
        Err(secure_i2c::SecureI2cError::ReplayDetected) => {
            // Potential replay attack — log and ignore frame
        }
        Err(_) => {}
    }
}
```

---

## Key Exchange Considerations

Since I2C has no built-in key negotiation, pre-shared keys (PSK) are the practical choice for most embedded deployments. Key provisioning options:

| Method | Security | Complexity | Use Case |
|--------|----------|------------|----------|
| **Factory OTP/eFuse** | High | Medium | Mass-produced devices |
| **Secure Element (ATECC608, SE050)** | Very High | Medium | High-security IoT |
| **ECDH over I2C at startup** | High | High | Multi-device networks |
| **Hard-coded constants** | Low | None | Prototype / dev only |
| **UART provisioning at first boot** | Medium | Low | Field-deployable devices |

For systems that require key rotation, implement a **Key Derivation Function (HKDF-SHA256)** to derive session keys from a master key and a session nonce exchanged at device initialization.

```c
// HKDF-Extract + HKDF-Expand (RFC 5869) sketch in C:
// prk = HMAC-SHA256(salt, ikm)       // Extract
// okm = HMAC-SHA256(prk, info || 0x01) // Expand (for 1 block)
```

---

## Replay Attack Prevention

A counter (sequence number) must be **persisted** across power cycles. Options:

```c
// Option A: RTC backup registers (STM32, NXP)
HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, tx_counter);

// Option B: EEPROM (with wear-leveling for frequent writes)
eeprom_write_word(COUNTER_ADDR, tx_counter);

// Option C: Flash with page rotation (high-endurance emulation)
flash_counter_increment();

// Option D: Secure Element monotonic counter
atecc608_increment_counter(0, &new_value);
```

> The receiver maintains a **receive window** (e.g., accept counters N+1 to N+32) to tolerate lost packets while still rejecting replays.

---

## Performance Considerations

| Cipher | Flash (code) | RAM | Cycles/byte (Cortex-M4) | Padding |
|--------|-------------|-----|--------------------------|---------|
| AES-128-CBC (SW) | ~4 KB | ~0.5 KB | ~280 | PKCS#7 required |
| AES-128-CBC (HW) | ~1 KB | ~0.3 KB | ~5–10 | PKCS#7 required |
| AES-128-GCM (HW) | ~2 KB | ~0.5 KB | ~15 | None |
| ChaCha20 (SW) | ~2 KB | ~0.3 KB | ~60 | None |
| ASCON-128 (SW) | ~1.5 KB | ~0.2 KB | ~45 | None |

**I2C throughput impact example** (400 kHz Fast Mode, 32-byte payload):
- Raw I2C transfer: ~0.9 ms
- AES-128-CBC SW encrypt (Cortex-M4 @ 168 MHz): ~0.05 ms overhead
- HMAC-SHA256 SW: ~0.3 ms overhead
- Total overhead: ~40% at 400 kHz (negligible at lower data rates)

For latency-sensitive applications, use hardware AES acceleration and reduce HMAC to GHASH (i.e., use AES-GCM instead of CBC+HMAC).

---

## Security Pitfalls

1. **IV/Nonce Reuse**: Reusing an IV with the same key under CBC completely breaks confidentiality. Always use a strictly incrementing counter or a TRNG-seeded nonce.

2. **MAC-then-Encrypt (wrong ordering)**: Always use **Encrypt-then-MAC**. MAC-then-Encrypt is vulnerable to padding oracle attacks.

3. **Using `memcmp` for MAC verification**: Use constant-time comparison (`ct_memcmp`) to prevent timing attacks that reveal MAC bytes.

4. **Shared encryption and authentication keys**: Derive separate keys for encryption and authentication using HKDF.

5. **No key rotation**: Implement a maximum message count per key (e.g., 2^32 messages) and force re-provisioning.

6. **Ignoring authentication failures**: Every MAC failure should increment a lockout counter. After N failures, disable the I2C interface and alert the system.

7. **Logging plaintext on failure paths**: Ensure debug output never leaks decrypted data or key material.

8. **Software AES on side-channel-sensitive platforms**: Software AES table lookups are vulnerable to cache-timing attacks. Use hardware AES or a constant-time software implementation.

---

## Summary

I2C's simplicity is also its security liability—the protocol provides **no confidentiality, integrity, or authentication** by design. For sensitive embedded applications, a well-designed **application-layer encryption scheme** is the pragmatic solution.

**The recommended architecture** is:

- **AES-128-GCM** (or ChaCha20-Poly1305 on MCUs without hardware AES) for combined encryption and authentication (AEAD).
- **Encrypt-then-MAC** with separate keys when using CBC mode.
- **Monotonic counter-based nonces** persisted to non-volatile storage to prevent IV reuse and replay attacks.
- **Constant-time MAC verification** to resist timing side-channels.
- **Pre-shared keys** provisioned via factory OTP or a secure element; rotate using HKDF-derived session keys.

In **C/C++**, libraries such as **mbedTLS**, **wolfSSL**, and **tiny-AES-c** provide embedded-friendly implementations. In **Rust**, the **RustCrypto** ecosystem (`aes`, `cbc`, `chacha20`, `hmac`, `sha2`) offers audited, `no_std`-compatible crates that integrate cleanly with `embedded-hal`.

By layering encryption above the I2C hardware and driver stack, designers can protect sensitive sensor data, prevent counterfeit peripheral attacks, and secure firmware update channels—without any changes to the underlying bus hardware or silicon.

---

*Document: 68_Encrypted_Communication.md | I2C Security Series*