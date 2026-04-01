# 66. I2C Bus Security

**Structure:** 10 sections with ~1,800 lines covering every layer of I2C security from threat modeling to bare-metal Rust.

**Threat Model:** Organized into passive attacks (eavesdropping, traffic analysis), active attacks (spoofing, MitM, replay, DoS, fault injection), and physical threats (probing, interposers, decapsulation).

**C/C++ Code Examples:**
- A complete `i2c_secure.c` implementing AES-128-CTR encryption, truncated HMAC-SHA256 with MAC-then-encrypt, monotonic counter replay protection, and HMAC-based challenge-response device authentication — all using the Linux `/dev/i2c-*` interface
- A C++17 `I2cSecureClient` class adding RAII, address whitelisting, rate limiting, and transaction logging

**Rust Code Examples:**
- A `std`-based secure context using `aes-gcm` (AES-128-GCM authenticated encryption) with full replay protection, address whitelisting, rate limiting, and challenge-response authentication — with a complete test suite
- A `no_std` bare-metal implementation using `embedded-hal` traits, compatible with STM32, nRF52, RP2040 and similar targets

**Key design principles** are called out throughout: constant-time MAC comparison, key zeroization on drop, AAD-binding of counters to ciphertext, and the importance of validating data even after authentication.

## Protecting Against Eavesdropping, Tampering, and Unauthorized Device Access

---

## Table of Contents

1. [Introduction](#introduction)
2. [I2C Security Threat Model](#i2c-security-threat-model)
3. [Eavesdropping on the I2C Bus](#eavesdropping-on-the-i2c-bus)
4. [Tampering and Man-in-the-Middle Attacks](#tampering-and-man-in-the-middle-attacks)
5. [Unauthorized Device Access](#unauthorized-device-access)
6. [Software-Level Mitigations](#software-level-mitigations)
7. [Application-Layer Encryption over I2C](#application-layer-encryption-over-i2c)
8. [Secure Authentication of I2C Devices](#secure-authentication-of-i2c-devices)
9. [Physical and Hardware Countermeasures](#physical-and-hardware-countermeasures)
10. [Implementation in C/C++](#implementation-in-cc)
11. [Implementation in Rust](#implementation-in-rust)
12. [Summary](#summary)

---

## Introduction

The I2C (Inter-Integrated Circuit) bus was designed in the early 1980s by Philips Semiconductor for low-speed, short-distance communication between ICs on a single PCB. Its design goals centered on simplicity, low pin count, and ease of use — **security was never part of the original specification**.

As embedded systems are increasingly deployed in connected, adversarial environments — industrial control systems, medical devices, automotive electronics, IoT nodes, and smart infrastructure — the inherent openness of the I2C bus presents significant security challenges:

- Any device connected to the bus can observe all traffic (no inherent confidentiality)
- The bus uses no built-in authentication (any master can address any slave)
- Physical access to the PCB exposes all communication in plaintext
- Address collisions and masquerading attacks are trivially easy to perform

This document provides a comprehensive treatment of I2C security threats and practical mitigation strategies, with full code examples in **C/C++** and **Rust**.

---

## I2C Security Threat Model

Before selecting countermeasures, it is essential to define the threat model. I2C-based systems face the following categories of attack:

### Passive Attacks
| Threat | Description | Example |
|--------|-------------|---------|
| **Eavesdropping** | Attacker probes SDA/SCL lines to capture bus traffic | Logic analyzer captures sensor readings or configuration data |
| **Traffic analysis** | Even without decryption, timing and frequency of transactions leaks information | Inferring device activity from I2C transaction patterns |

### Active Attacks
| Threat | Description | Example |
|--------|-------------|---------|
| **Device spoofing** | Malicious device responds to a legitimate device's address | Fake sensor sends crafted data to the master |
| **Master spoofing** | Rogue master sends unauthorized commands to slaves | Unauthorized reconfiguration of an EEPROM or ADC |
| **Replay attacks** | Captured commands are retransmitted later | Replaying an "unlock" or "enable" command out of context |
| **Man-in-the-Middle (MitM)** | Attacker intercepts and modifies transactions between master and slave | Altering sensor readings or command parameters in flight |
| **Denial of Service (DoS)** | Bus is held low (clock stretching abuse, SDA/SCL stuck) | Jamming the bus to disrupt system operation |
| **Glitching / Fault Injection** | Electrical disturbances force devices into unexpected states | Bypassing access control in a secure element |

### Physical Threats
| Threat | Description |
|--------|-------------|
| **Test point probing** | Exposed vias or test points allow non-invasive access |
| **PCB interposer** | A thin PCB inserted between IC and board intercepts all traffic |
| **Decapsulation** | For high-value targets, the IC package is removed and bonding wires probed |

---

## Eavesdropping on the I2C Bus

### Why I2C Is Inherently Observable

I2C is an open-drain, shared-bus protocol. Every transaction is visible to every device connected to the bus:

```
Master ──┬─── SDA ───┬─── SDA ───┬─── SDA
         │           │           │
      Slave A     Slave B     Attacker Probe
         │           │           │
Master ──┴─── SCL ───┴─── SCL ───┴─── SCL
```

A logic analyzer, oscilloscope, or even a Raspberry Pi with a GPIO-based I2C sniffer can decode all traffic with zero interaction — the bus requires no active participation to be monitored.

### What an Attacker Can Extract

- Device addresses (7-bit or 10-bit) — device topology
- Register addresses being read/written
- Raw sensor data (temperature, pressure, accelerometer readings)
- Configuration values (gain, resolution, thresholds)
- Authentication tokens or challenge/response sequences if implemented naively

### Eavesdropping Countermeasures

Since the physical layer of I2C provides no confidentiality, all confidentiality measures must be implemented at the **application layer**:

1. **Encrypt payloads** before writing to I2C; decrypt after reading
2. **Use message authentication codes (MAC)** to detect tampering
3. **Minimize sensitive data exposure** — avoid transmitting raw secrets over I2C
4. **Physical PCB hardening** — eliminate test points, use conformal coating, employ ground-plane shielding around bus traces

---

## Tampering and Man-in-the-Middle Attacks

### The MitM Scenario

In a hardware MitM attack, an adversary physically intercepts the I2C bus:

```
Master ── SDA/SCL ──[Interposer]── SDA/SCL ── Slave
                         │
                    Attacker MCU
```

The interposer acts as a slave to the master and a master to the slave, relaying modified transactions. This is feasible with a small microcontroller or FPGA.

### Consequences of Tampering
- Sensor readings altered to cause incorrect system behavior
- Configuration writes intercepted and changed
- Authentication tokens captured and replayed

### Software Mitigation: Message Authentication Codes

A MAC (e.g., HMAC-SHA256, AES-CMAC) ensures that any modification to a message is detectable. Both parties share a secret key; the MAC is computed over the payload and verified by the receiver.

```
Transmit: [Payload] + [MAC(key, Payload)]
Receive:  Verify MAC(key, Payload) == received MAC
```

If the MitM modifies Payload, the MAC verification fails.

---

## Unauthorized Device Access

### The Authentication Gap

The I2C protocol identifies devices purely by their 7-bit (or 10-bit) addresses. There is no built-in mechanism for a slave to verify that the master is authorized, nor for a master to verify that a slave is genuine.

This creates two failure modes:

1. **Rogue master** — any master connected to the bus can issue commands to any slave device (e.g., an attacker with USB-to-I2C hardware like an FT232H).
2. **Rogue slave** — a spoofed slave responds to a legitimate device's address, feeding false data to the master.

### Challenge-Response Authentication

The most practical software-level solution is a challenge-response protocol:

```
Master → Slave:  CHALLENGE (nonce, random 16 bytes)
Slave  → Master: RESPONSE  (HMAC(shared_key, nonce))
Master:          Verify RESPONSE
                 Proceed if valid, abort if not
```

This ensures that only a slave possessing the shared secret can provide a valid response. Replay attacks are mitigated by the freshness of the nonce.

---

## Software-Level Mitigations

### 1. Input Validation and Bounds Checking

Even without encryption, validate all data received over I2C before acting on it:

```c
/* Always validate ranges before using I2C-received data */
int16_t raw_temp = i2c_read_word(TEMP_SENSOR_ADDR, REG_TEMPERATURE);
if (raw_temp < TEMP_MIN_RAW || raw_temp > TEMP_MAX_RAW) {
    log_security_event(SEC_EVENT_OUT_OF_RANGE);
    return ERROR_INVALID_DATA;
}
```

### 2. Transaction Sequencing and Replay Detection

Maintain a monotonic transaction counter. Each message must carry a counter value greater than the last accepted one:

```
Message: [Counter | Payload | MAC(key, Counter || Payload)]
```

A replayed message will have an old counter value and be rejected.

### 3. Rate Limiting and Anomaly Detection

Detect unusual patterns:
- Unexpected addresses appearing on the bus
- Abnormal transaction rates (DoS or bus-scan attempts)
- Sequential address scanning (attacker enumerating devices)

---

## Application-Layer Encryption over I2C

Since I2C itself carries no encryption, the application must encrypt data before it is placed on the bus. The most practical symmetric cipher for embedded systems is **AES-128 in CTR mode** (no padding required, works well with fixed-size I2C payloads) or **AES-128-CCM** (provides both confidentiality and authentication in one pass).

### Encryption Architecture

```
┌──────────────────────────────────────────────────┐
│  Application Layer (plaintext data)              │
├──────────────────────────────────────────────────┤
│  Security Layer (AES-CCM encrypt/decrypt + MAC)  │
├──────────────────────────────────────────────────┤
│  I2C Transport Layer (encrypted ciphertext)      │
└──────────────────────────────────────────────────┘
```

Key management considerations:
- Keys must be provisioned securely (not hardcoded in firmware)
- Use a hardware security module (HSM) or secure element (e.g., ATECC608) for key storage where available
- Derive session keys from a master key using a KDF (HKDF, SP800-108)

---

## Secure Authentication of I2C Devices

### Using Dedicated Secure Elements

Several ICs implement cryptographic authentication over I2C:

| Device | Manufacturer | Algorithm | Notes |
|--------|-------------|-----------|-------|
| ATECC608B | Microchip | ECC P-256, SHA-256 HMAC | Full PKI support |
| DS28E38 | Maxim/Analog | ECC P-256 | ChipDNA PUF key storage |
| SE050 | NXP | ECC, RSA, AES | Common Criteria EAL 6+ |
| STSAFE-A | ST Micro | ECC P-256/384 | Automotive grade available |

These devices implement the cryptographic heavy lifting in tamper-resistant silicon, avoiding key exposure in software.

### Lightweight HMAC-Based Authentication (Software)

For resource-constrained systems without dedicated secure elements:

```
Session establishment:
  1. Master generates 16-byte nonce N
  2. Master sends N to Slave over I2C
  3. Slave computes R = HMAC-SHA256(K, "AUTH" || N)
  4. Slave sends R[0..15] (first 16 bytes) to Master
  5. Master independently computes expected R and compares
  6. If match: bus communication proceeds; else: halt
```

---

## Implementation in C/C++

### Overview of the C/C++ Examples

The following examples demonstrate:
1. AES-128-CTR encryption/decryption of I2C payloads (using a minimal software AES)
2. HMAC-SHA256-based device authentication
3. Replay attack prevention using a monotonic counter
4. Secure I2C transaction wrapper

These examples assume a Linux-based I2C userspace interface (`/dev/i2c-*`) but the cryptographic logic applies equally to bare-metal embedded systems.

---

### C Example 1: I2C Secure Transaction Layer (Linux userspace)

```c
/**
 * i2c_secure.h / i2c_secure.c
 *
 * Secure I2C transaction layer providing:
 *   - AES-128-CTR payload encryption
 *   - HMAC-SHA256 message authentication
 *   - Replay protection via monotonic counter
 *
 * Dependencies: OpenSSL (for AES + HMAC in this example)
 * On embedded targets, replace with mbedTLS or a hardware crypto driver.
 *
 * Compile: gcc -o i2c_secure i2c_secure.c -lssl -lcrypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

/* ─────────────────────────────────────────────────────────────
   Security Parameters
   ───────────────────────────────────────────────────────────── */

#define I2C_KEY_LEN         16    /* AES-128 key length in bytes      */
#define I2C_MAC_LEN         16    /* Truncated HMAC-SHA256 (128 bits) */
#define I2C_NONCE_LEN       12    /* AES-CTR nonce length             */
#define I2C_COUNTER_LEN     4     /* Monotonic counter (32-bit)       */
#define I2C_MAX_PAYLOAD     32    /* Maximum plaintext payload bytes  */

/* Secure packet format (transmitted over I2C):
 *  [4 bytes counter] [12 bytes nonce] [N bytes ciphertext] [16 bytes MAC]
 *  MAC covers: counter || nonce || ciphertext
 */
#define I2C_OVERHEAD        (I2C_COUNTER_LEN + I2C_NONCE_LEN + I2C_MAC_LEN)
#define I2C_MAX_PACKET      (I2C_MAX_PAYLOAD + I2C_OVERHEAD)

/* ─────────────────────────────────────────────────────────────
   Secure Context
   ───────────────────────────────────────────────────────────── */

typedef struct {
    int      fd;                        /* I2C file descriptor          */
    uint8_t  addr;                      /* 7-bit slave address          */
    uint8_t  enc_key[I2C_KEY_LEN];     /* AES-128 encryption key       */
    uint8_t  mac_key[I2C_KEY_LEN];     /* HMAC-SHA256 MAC key          */
    uint32_t tx_counter;               /* Transmit monotonic counter   */
    uint32_t rx_counter;               /* Last accepted receive counter */
    bool     authenticated;            /* True after device auth passed */
} i2c_secure_ctx_t;

/* ─────────────────────────────────────────────────────────────
   AES-128-CTR Encryption / Decryption
   OpenSSL EVP interface; replace with mbedtls_aes_crypt_ctr on
   bare-metal targets.
   ───────────────────────────────────────────────────────────── */

static int aes_ctr_crypt(const uint8_t *key,
                          const uint8_t *nonce,   /* 12 bytes */
                          uint32_t       counter,  /* initial counter value */
                          const uint8_t *in,
                          uint8_t       *out,
                          size_t         len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    /* Build full 128-bit IV: [nonce 12B][counter 4B] */
    uint8_t iv[16];
    memcpy(iv, nonce, 12);
    iv[12] = (counter >> 24) & 0xFF;
    iv[13] = (counter >> 16) & 0xFF;
    iv[14] = (counter >>  8) & 0xFF;
    iv[15] = (counter      ) & 0xFF;

    int ok = 1;
    int out_len = 0;
    ok &= EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv);
    ok &= EVP_EncryptUpdate(ctx, out, &out_len, in, (int)len);

    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────
   HMAC-SHA256 (truncated to I2C_MAC_LEN bytes)
   ───────────────────────────────────────────────────────────── */

static int compute_mac(const uint8_t  *key,
                        const uint8_t  *data,
                        size_t          data_len,
                        uint8_t        *mac_out)   /* I2C_MAC_LEN bytes */
{
    uint8_t  full_mac[32];
    unsigned full_mac_len = 32;

    if (!HMAC(EVP_sha256(), key, I2C_KEY_LEN,
              data, data_len, full_mac, &full_mac_len)) {
        return -1;
    }
    memcpy(mac_out, full_mac, I2C_MAC_LEN);
    return 0;
}

static bool verify_mac(const uint8_t *key,
                        const uint8_t *data,
                        size_t         data_len,
                        const uint8_t *mac_received)
{
    uint8_t mac_expected[I2C_MAC_LEN];
    if (compute_mac(key, data, data_len, mac_expected) != 0) return false;

    /* Constant-time comparison to prevent timing oracle attacks */
    volatile uint8_t diff = 0;
    for (int i = 0; i < I2C_MAC_LEN; i++) {
        diff |= mac_expected[i] ^ mac_received[i];
    }
    return diff == 0;
}

/* ─────────────────────────────────────────────────────────────
   Context Initialization
   ───────────────────────────────────────────────────────────── */

int i2c_secure_init(i2c_secure_ctx_t *ctx,
                    const char        *dev,       /* e.g. "/dev/i2c-1" */
                    uint8_t            addr,
                    const uint8_t     *enc_key,   /* 16 bytes */
                    const uint8_t     *mac_key)   /* 16 bytes */
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->fd = open(dev, O_RDWR);
    if (ctx->fd < 0) {
        perror("i2c_secure_init: open");
        return -1;
    }
    if (ioctl(ctx->fd, I2C_SLAVE, addr) < 0) {
        perror("i2c_secure_init: ioctl I2C_SLAVE");
        close(ctx->fd);
        return -1;
    }

    ctx->addr       = addr;
    ctx->tx_counter = 1;          /* Start counters at 1; 0 is reserved */
    ctx->rx_counter = 0;
    ctx->authenticated = false;

    memcpy(ctx->enc_key, enc_key, I2C_KEY_LEN);
    memcpy(ctx->mac_key, mac_key, I2C_KEY_LEN);

    return 0;
}

void i2c_secure_close(i2c_secure_ctx_t *ctx)
{
    if (ctx->fd >= 0) close(ctx->fd);
    /* Zero keys before freeing context (zeroization) */
    memset(ctx->enc_key, 0, I2C_KEY_LEN);
    memset(ctx->mac_key, 0, I2C_KEY_LEN);
    memset(ctx, 0, sizeof(*ctx));
}

/* ─────────────────────────────────────────────────────────────
   Secure Write: Encrypt + MAC + Counter
   Packet layout:
     [0..3]   : tx_counter (big-endian)
     [4..15]  : random nonce (12 bytes)
     [16..15+len] : AES-CTR ciphertext
     [end-16..end]: HMAC-SHA256 MAC (16 bytes)
   ───────────────────────────────────────────────────────────── */

int i2c_secure_write(i2c_secure_ctx_t *ctx,
                     uint8_t           reg,
                     const uint8_t    *plaintext,
                     size_t            len)
{
    if (len == 0 || len > I2C_MAX_PAYLOAD) return -EINVAL;
    if (!ctx->authenticated) return -EACCES;

    uint8_t packet[1 + I2C_MAX_PACKET]; /* +1 for register byte */
    size_t  pkt_offset = 0;

    /* Register byte (first byte in I2C write) */
    packet[0] = reg;
    pkt_offset = 1;

    /* Counter (big-endian, 4 bytes) */
    uint32_t ctr = ctx->tx_counter++;
    packet[pkt_offset++] = (ctr >> 24) & 0xFF;
    packet[pkt_offset++] = (ctr >> 16) & 0xFF;
    packet[pkt_offset++] = (ctr >>  8) & 0xFF;
    packet[pkt_offset++] = (ctr      ) & 0xFF;

    /* Random nonce (12 bytes) */
    if (RAND_bytes(&packet[pkt_offset], I2C_NONCE_LEN) != 1) return -EIO;
    uint8_t *nonce = &packet[pkt_offset];
    pkt_offset += I2C_NONCE_LEN;

    /* AES-CTR encrypt plaintext → ciphertext in packet */
    if (aes_ctr_crypt(ctx->enc_key, nonce, ctr,
                      plaintext, &packet[pkt_offset], len) != 0) {
        return -EIO;
    }
    pkt_offset += len;

    /* MAC over: [counter(4)] [nonce(12)] [ciphertext(len)] */
    /* MAC input starts right after the register byte */
    if (compute_mac(ctx->mac_key, &packet[1], pkt_offset - 1,
                    &packet[pkt_offset]) != 0) {
        return -EIO;
    }
    pkt_offset += I2C_MAC_LEN;

    /* Write to I2C bus */
    ssize_t written = write(ctx->fd, packet, pkt_offset);
    if (written != (ssize_t)pkt_offset) {
        perror("i2c_secure_write: write");
        return -EIO;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────
   Secure Read: Read ciphertext, verify MAC, decrypt, check counter
   ───────────────────────────────────────────────────────────── */

int i2c_secure_read(i2c_secure_ctx_t *ctx,
                    uint8_t           reg,
                    uint8_t          *plaintext_out,
                    size_t            expected_len)
{
    if (expected_len == 0 || expected_len > I2C_MAX_PAYLOAD) return -EINVAL;
    if (!ctx->authenticated) return -EACCES;

    /* Send register address first */
    if (write(ctx->fd, &reg, 1) != 1) {
        perror("i2c_secure_read: register write");
        return -EIO;
    }

    /* Read the secure packet */
    size_t  pkt_len = I2C_OVERHEAD + expected_len;
    uint8_t packet[I2C_MAX_PACKET];

    ssize_t rd = read(ctx->fd, packet, pkt_len);
    if (rd != (ssize_t)pkt_len) {
        perror("i2c_secure_read: read");
        return -EIO;
    }

    /* Verify MAC first (before any decryption) */
    size_t mac_cover_len = I2C_COUNTER_LEN + I2C_NONCE_LEN + expected_len;
    if (!verify_mac(ctx->mac_key, packet, mac_cover_len,
                    &packet[mac_cover_len])) {
        fprintf(stderr, "i2c_secure_read: MAC verification FAILED (tampering?)\n");
        return -EBADMSG;
    }

    /* Check counter (replay protection) */
    uint32_t rx_ctr = ((uint32_t)packet[0] << 24) |
                      ((uint32_t)packet[1] << 16) |
                      ((uint32_t)packet[2] <<  8) |
                      ((uint32_t)packet[3]      );

    if (rx_ctr <= ctx->rx_counter) {
        fprintf(stderr, "i2c_secure_read: counter replay detected "
                "(got %u, expected > %u)\n", rx_ctr, ctx->rx_counter);
        return -EBADMSG;
    }
    ctx->rx_counter = rx_ctr;

    /* Decrypt ciphertext → plaintext */
    const uint8_t *nonce      = &packet[I2C_COUNTER_LEN];
    const uint8_t *ciphertext = &packet[I2C_COUNTER_LEN + I2C_NONCE_LEN];

    if (aes_ctr_crypt(ctx->enc_key, nonce, rx_ctr,
                      ciphertext, plaintext_out, expected_len) != 0) {
        return -EIO;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────
   Challenge-Response Device Authentication
   Master sends 16-byte nonce; slave must respond with
   HMAC-SHA256(mac_key, "I2C-AUTH" || nonce)[0..15]
   ───────────────────────────────────────────────────────────── */

#define AUTH_REG_CHALLENGE  0xA0
#define AUTH_REG_RESPONSE   0xA1
#define AUTH_PREFIX         "I2C-AUTH"
#define AUTH_PREFIX_LEN     8

int i2c_secure_authenticate(i2c_secure_ctx_t *ctx)
{
    /* Generate fresh 16-byte nonce */
    uint8_t nonce[16];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) return -EIO;

    /* Send nonce as challenge (plaintext — no secret data yet) */
    uint8_t challenge_pkt[17] = { AUTH_REG_CHALLENGE };
    memcpy(&challenge_pkt[1], nonce, 16);
    if (write(ctx->fd, challenge_pkt, 17) != 17) {
        perror("i2c_secure_authenticate: challenge write");
        return -EIO;
    }

    /* Small delay for slave to compute HMAC */
    usleep(5000);  /* 5ms — tune per slave capability */

    /* Read 16-byte response */
    uint8_t reg = AUTH_REG_RESPONSE;
    if (write(ctx->fd, &reg, 1) != 1) return -EIO;

    uint8_t response[16];
    if (read(ctx->fd, response, 16) != 16) {
        perror("i2c_secure_authenticate: response read");
        return -EIO;
    }

    /* Compute expected response locally */
    uint8_t auth_data[AUTH_PREFIX_LEN + 16];
    memcpy(auth_data, AUTH_PREFIX, AUTH_PREFIX_LEN);
    memcpy(auth_data + AUTH_PREFIX_LEN, nonce, 16);

    uint8_t expected_response[I2C_MAC_LEN];
    if (compute_mac(ctx->mac_key, auth_data, sizeof(auth_data),
                    expected_response) != 0) {
        return -EIO;
    }

    /* Constant-time comparison */
    volatile uint8_t diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= expected_response[i] ^ response[i];
    }

    if (diff != 0) {
        fprintf(stderr, "i2c_secure_authenticate: FAILED — device not authenticated\n");
        return -EACCES;
    }

    ctx->authenticated = true;
    printf("i2c_secure_authenticate: device at 0x%02X authenticated OK\n", ctx->addr);
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   Example Usage
   ───────────────────────────────────────────────────────────── */

int main(void)
{
    /* In production: load keys from secure storage, not hardcoded */
    const uint8_t enc_key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    const uint8_t mac_key[16] = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
        0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81
    };

    i2c_secure_ctx_t ctx;

    if (i2c_secure_init(&ctx, "/dev/i2c-1", 0x48, enc_key, mac_key) != 0) {
        fprintf(stderr, "Failed to initialize secure I2C context\n");
        return 1;
    }

    /* Authenticate the device before any data exchange */
    if (i2c_secure_authenticate(&ctx) != 0) {
        fprintf(stderr, "Device authentication failed — aborting\n");
        i2c_secure_close(&ctx);
        return 1;
    }

    /* Write encrypted configuration */
    uint8_t config[] = { 0x01, 0x23, 0x45, 0x67 };
    if (i2c_secure_write(&ctx, 0x10, config, sizeof(config)) != 0) {
        fprintf(stderr, "Secure write failed\n");
    }

    /* Read encrypted sensor data */
    uint8_t sensor_data[4];
    if (i2c_secure_read(&ctx, 0x20, sensor_data, sizeof(sensor_data)) == 0) {
        printf("Sensor data: %02X %02X %02X %02X\n",
               sensor_data[0], sensor_data[1],
               sensor_data[2], sensor_data[3]);
    }

    i2c_secure_close(&ctx);
    return 0;
}
```

---

### C++ Example 2: RAII Secure I2C Client with Address Whitelist

```cpp
/**
 * I2cSecureClient.hpp
 *
 * C++17 RAII wrapper for secure I2C communication.
 * Adds:
 *  - Address whitelist enforcement (reject unlisted addresses)
 *  - Transaction logging for anomaly detection
 *  - RAII resource management
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <unordered_set>
#include <stdexcept>
#include <chrono>
#include <functional>

class I2cSecureClient {
public:
    static constexpr size_t KEY_LEN     = 16;
    static constexpr size_t MAC_LEN     = 16;
    static constexpr size_t MAX_PAYLOAD = 32;

    using Key        = std::array<uint8_t, KEY_LEN>;
    using MacResult  = std::array<uint8_t, MAC_LEN>;
    using LogCallback = std::function<void(uint8_t addr, uint8_t reg, bool success)>;

    struct SecurityConfig {
        Key         enc_key;
        Key         mac_key;
        std::unordered_set<uint8_t> allowed_addresses;  /* address whitelist */
        uint32_t    max_transactions_per_second = 100;  /* rate limit        */
        bool        require_authentication      = true;
    };

    explicit I2cSecureClient(const SecurityConfig& config)
        : config_(config)
        , tx_counter_(1)
        , rx_counter_(0)
        , authenticated_(false)
        , transaction_count_(0)
    {
        last_rate_reset_ = std::chrono::steady_clock::now();
    }

    /* Non-copyable, non-movable (holds crypto state) */
    I2cSecureClient(const I2cSecureClient&)            = delete;
    I2cSecureClient& operator=(const I2cSecureClient&) = delete;

    ~I2cSecureClient() {
        /* Zeroize keys on destruction */
        volatile uint8_t *p = config_.enc_key.data();
        for (size_t i = 0; i < KEY_LEN; i++) p[i] = 0;
        p = config_.mac_key.data();
        for (size_t i = 0; i < KEY_LEN; i++) p[i] = 0;
    }

    void set_log_callback(LogCallback cb) { log_callback_ = cb; }

    /**
     * Validate address against whitelist before performing any I2C operation.
     * Throws std::runtime_error if address is not in the whitelist.
     */
    void validate_address(uint8_t addr) const {
        if (!config_.allowed_addresses.empty() &&
            config_.allowed_addresses.find(addr) == config_.allowed_addresses.end()) {
            throw std::runtime_error(
                "I2cSecureClient: address 0x" +
                to_hex(addr) +
                " not in allowed address whitelist — possible bus scan or spoofing");
        }
    }

    /**
     * Rate limiting check — prevents DoS and bus flooding.
     */
    bool check_rate_limit() {
        using namespace std::chrono;
        auto now     = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - last_rate_reset_).count();

        if (elapsed >= 1000) {
            transaction_count_ = 0;
            last_rate_reset_   = now;
        }

        if (++transaction_count_ > config_.max_transactions_per_second) {
            return false;  /* Rate limit exceeded */
        }
        return true;
    }

    /**
     * Build authenticated plaintext frame (before encryption):
     *   [4-byte counter BE] [4-byte payload length BE] [payload bytes]
     * Returns frame as vector.
     */
    std::vector<uint8_t> build_authenticated_frame(
            const uint8_t *payload, size_t payload_len)
    {
        if (payload_len > MAX_PAYLOAD)
            throw std::length_error("Payload exceeds MAX_PAYLOAD");

        std::vector<uint8_t> frame;
        frame.reserve(8 + payload_len + MAC_LEN);

        uint32_t ctr = tx_counter_++;

        /* Counter (big-endian) */
        frame.push_back((ctr >> 24) & 0xFF);
        frame.push_back((ctr >> 16) & 0xFF);
        frame.push_back((ctr >>  8) & 0xFF);
        frame.push_back((ctr      ) & 0xFF);

        /* Payload length (big-endian) */
        frame.push_back((payload_len >> 24) & 0xFF);
        frame.push_back((payload_len >> 16) & 0xFF);
        frame.push_back((payload_len >>  8) & 0xFF);
        frame.push_back((payload_len      ) & 0xFF);

        /* Payload */
        frame.insert(frame.end(), payload, payload + payload_len);

        /* Compute and append MAC */
        MacResult mac = compute_mac(frame.data(), frame.size());
        frame.insert(frame.end(), mac.begin(), mac.end());

        return frame;
    }

    /**
     * Verify and parse a received authenticated frame.
     * Returns true if valid; populates payload_out and advances rx_counter.
     */
    bool verify_frame(const std::vector<uint8_t>& frame,
                      std::vector<uint8_t>&        payload_out)
    {
        if (frame.size() < 8 + MAC_LEN) return false;

        size_t data_len = frame.size() - MAC_LEN;

        /* Verify MAC first */
        MacResult rx_mac;
        std::copy(frame.begin() + data_len, frame.end(), rx_mac.begin());

        MacResult expected_mac = compute_mac(frame.data(), data_len);
        if (!constant_time_equal(rx_mac, expected_mac)) {
            if (log_callback_) log_callback_(0xFF, 0xFF, false);
            return false;
        }

        /* Check counter */
        uint32_t rx_ctr = ((uint32_t)frame[0] << 24) |
                          ((uint32_t)frame[1] << 16) |
                          ((uint32_t)frame[2] <<  8) |
                          ((uint32_t)frame[3]      );

        if (rx_ctr <= rx_counter_) return false;  /* Replay detected */
        rx_counter_ = rx_ctr;

        /* Extract payload length and payload */
        uint32_t payload_len = ((uint32_t)frame[4] << 24) |
                               ((uint32_t)frame[5] << 16) |
                               ((uint32_t)frame[6] <<  8) |
                               ((uint32_t)frame[7]      );

        if (payload_len > MAX_PAYLOAD || 8 + payload_len + MAC_LEN != frame.size())
            return false;

        payload_out.assign(frame.begin() + 8, frame.begin() + 8 + payload_len);
        return true;
    }

private:
    SecurityConfig   config_;
    uint32_t         tx_counter_;
    uint32_t         rx_counter_;
    bool             authenticated_;
    uint32_t         transaction_count_;
    std::chrono::steady_clock::time_point last_rate_reset_;
    LogCallback      log_callback_;

    MacResult compute_mac(const uint8_t *data, size_t len) const {
        /* Placeholder: replace with real HMAC-SHA256 using OpenSSL/mbedTLS */
        MacResult result{};
        /* Real implementation: HMAC(config_.mac_key, data, len) */
        (void)data; (void)len;  /* suppress unused-parameter warning */
        return result;
    }

    template<size_t N>
    static bool constant_time_equal(const std::array<uint8_t, N>& a,
                                    const std::array<uint8_t, N>& b) {
        volatile uint8_t diff = 0;
        for (size_t i = 0; i < N; i++) diff |= a[i] ^ b[i];
        return diff == 0;
    }

    static std::string to_hex(uint8_t v) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", v);
        return buf;
    }
};

/* ─────────────────────────────────────────────────────────────
   Example usage of I2cSecureClient
   ───────────────────────────────────────────────────────────── */
/*
int main() {
    I2cSecureClient::SecurityConfig config{};

    // Set keys (in production: load from secure storage)
    config.enc_key = {0x2b, 0x7e, 0x15, 0x16, ...};
    config.mac_key = {0x60, 0x3d, 0xeb, 0x10, ...};

    // Only allow specific device addresses (defense against rogue devices)
    config.allowed_addresses = {0x48, 0x49, 0x76};
    config.max_transactions_per_second = 50;

    I2cSecureClient client(config);

    // Log all transaction outcomes
    client.set_log_callback([](uint8_t addr, uint8_t reg, bool ok) {
        if (!ok) {
            printf("SECURITY EVENT: Failed transaction addr=0x%02X reg=0x%02X\n",
                   addr, reg);
        }
    });

    try {
        client.validate_address(0x48);  // OK: in whitelist

        if (!client.check_rate_limit()) {
            fprintf(stderr, "Rate limit exceeded!\n");
            return 1;
        }

        uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
        auto frame = client.build_authenticated_frame(payload, sizeof(payload));

        // Transmit frame over I2C...
        // ...

    } catch (const std::runtime_error& e) {
        fprintf(stderr, "Security violation: %s\n", e.what());
        return 1;
    }

    return 0;
}
*/
```

---

## Implementation in Rust

### Rust Example 1: Secure I2C Abstraction with AES-GCM

```rust
//! i2c_secure.rs
//!
//! Secure I2C communication layer in Rust.
//! Uses AES-128-GCM (provides both confidentiality and authentication
//! in a single pass — ideal for constrained I2C payloads).
//!
//! Dependencies (Cargo.toml):
//!   [dependencies]
//!   aes-gcm = "0.10"
//!   rand    = "0.8"
//!   sha2    = "0.10"
//!   hmac    = "0.12"
//!   linux-embedded-hal = "0.4"  # for I2C on Linux
//!   embedded-hal       = "1.0"
//!
//! The cryptographic traits are hardware-agnostic; swap the I2C backend
//! for any embedded-hal-compatible implementation (e.g., stm32f4xx-hal).

use std::time::{Duration, Instant};
use std::collections::HashSet;

use aes_gcm::{
    aead::{Aead, AeadCore, KeyInit, OsRng},
    Aes128Gcm, Key, Nonce,
};
use hmac::{Hmac, Mac};
use sha2::Sha256;
use rand::RngCore;

// ─────────────────────────────────────────────────────────────
// Type aliases
// ─────────────────────────────────────────────────────────────

type HmacSha256 = Hmac<Sha256>;

pub const KEY_LEN: usize = 16;    // AES-128
pub const MAC_LEN: usize = 16;    // GCM tag length
pub const NONCE_LEN: usize = 12;  // AES-GCM nonce
pub const MAX_PAYLOAD: usize = 32;

// ─────────────────────────────────────────────────────────────
// Errors
// ─────────────────────────────────────────────────────────────

#[derive(Debug, thiserror::Error)]
pub enum I2cSecureError {
    #[error("authentication failed")]
    AuthenticationFailed,

    #[error("MAC verification failed — possible tampering")]
    MacVerificationFailed,

    #[error("replay attack detected (counter {received} <= last accepted {last})")]
    ReplayDetected { received: u32, last: u32 },

    #[error("address 0x{addr:02X} not in whitelist")]
    AddressNotAllowed { addr: u8 },

    #[error("payload too large: {size} > {max}")]
    PayloadTooLarge { size: usize, max: usize },

    #[error("rate limit exceeded")]
    RateLimitExceeded,

    #[error("I2C I/O error: {0}")]
    IoError(String),

    #[error("encryption error: {0}")]
    EncryptionError(String),
}

pub type Result<T> = std::result::Result<T, I2cSecureError>;

// ─────────────────────────────────────────────────────────────
// Security Configuration
// ─────────────────────────────────────────────────────────────

pub struct SecurityConfig {
    /// AES-128-GCM key (16 bytes)
    pub aes_key: [u8; KEY_LEN],
    /// HMAC-SHA256 key for challenge-response auth (16 bytes)
    pub hmac_key: [u8; KEY_LEN],
    /// Allowed I2C device addresses (empty = allow all)
    pub allowed_addresses: HashSet<u8>,
    /// Maximum transactions per second (rate limiting)
    pub max_tps: u32,
    /// Require device authentication before data exchange
    pub require_auth: bool,
}

impl Drop for SecurityConfig {
    fn drop(&mut self) {
        // Zeroize sensitive key material on drop
        self.aes_key.iter_mut().for_each(|b| *b = 0);
        self.hmac_key.iter_mut().for_each(|b| *b = 0);
    }
}

// ─────────────────────────────────────────────────────────────
// Secure Packet Structure
// ─────────────────────────────────────────────────────────────
//
// Layout (transmitted over I2C):
//   [4 bytes] monotonic counter (big-endian)
//   [12 bytes] AES-GCM nonce
//   [N bytes] ciphertext  (AES-128-GCM encrypted payload)
//   [16 bytes] GCM authentication tag
//
// The GCM tag covers both the nonce and ciphertext.
// The counter is included as Additional Authenticated Data (AAD)
// so it is authenticated but not encrypted.

#[derive(Debug)]
pub struct SecurePacket {
    pub counter: u32,
    pub nonce: [u8; NONCE_LEN],
    pub ciphertext: Vec<u8>,  // includes GCM tag appended by aes-gcm crate
}

impl SecurePacket {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(4 + NONCE_LEN + self.ciphertext.len());
        buf.extend_from_slice(&self.counter.to_be_bytes());
        buf.extend_from_slice(&self.nonce);
        buf.extend_from_slice(&self.ciphertext);
        buf
    }

    pub fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 4 + NONCE_LEN + MAC_LEN { return None; }
        let counter = u32::from_be_bytes(bytes[0..4].try_into().ok()?);
        let mut nonce = [0u8; NONCE_LEN];
        nonce.copy_from_slice(&bytes[4..4 + NONCE_LEN]);
        let ciphertext = bytes[4 + NONCE_LEN..].to_vec();
        Some(Self { counter, nonce, ciphertext })
    }
}

// ─────────────────────────────────────────────────────────────
// Secure I2C Context
// ─────────────────────────────────────────────────────────────

pub struct I2cSecureCtx {
    config: SecurityConfig,
    tx_counter: u32,
    rx_counter: u32,
    authenticated: bool,
    rate_window_start: Instant,
    rate_count: u32,
}

impl I2cSecureCtx {
    pub fn new(config: SecurityConfig) -> Self {
        Self {
            config,
            tx_counter: 1,
            rx_counter: 0,
            authenticated: false,
            rate_window_start: Instant::now(),
            rate_count: 0,
        }
    }

    // ── Address Whitelist Check ────────────────────────────────

    pub fn validate_address(&self, addr: u8) -> Result<()> {
        if !self.config.allowed_addresses.is_empty()
            && !self.config.allowed_addresses.contains(&addr)
        {
            return Err(I2cSecureError::AddressNotAllowed { addr });
        }
        Ok(())
    }

    // ── Rate Limiting ──────────────────────────────────────────

    pub fn check_rate_limit(&mut self) -> Result<()> {
        let now = Instant::now();
        if now.duration_since(self.rate_window_start) >= Duration::from_secs(1) {
            self.rate_window_start = now;
            self.rate_count = 0;
        }
        self.rate_count += 1;
        if self.rate_count > self.config.max_tps {
            Err(I2cSecureError::RateLimitExceeded)
        } else {
            Ok(())
        }
    }

    // ── AES-128-GCM Encryption ─────────────────────────────────

    pub fn encrypt(&mut self, plaintext: &[u8]) -> Result<SecurePacket> {
        if plaintext.len() > MAX_PAYLOAD {
            return Err(I2cSecureError::PayloadTooLarge {
                size: plaintext.len(),
                max: MAX_PAYLOAD,
            });
        }

        let key   = Key::<Aes128Gcm>::from_slice(&self.config.aes_key);
        let cipher = Aes128Gcm::new(key);

        // Generate fresh nonce for every packet
        let nonce_generic = Aes128Gcm::generate_nonce(&mut OsRng);
        let mut nonce = [0u8; NONCE_LEN];
        nonce.copy_from_slice(nonce_generic.as_slice());

        let counter = self.tx_counter;
        self.tx_counter = self.tx_counter
            .checked_add(1)
            .ok_or_else(|| I2cSecureError::EncryptionError(
                "counter overflow".into()
            ))?;

        // Use counter as Additional Authenticated Data (AAD)
        // This binds the counter to the ciphertext without encrypting it
        let aad = counter.to_be_bytes();
        let payload = aes_gcm::aead::Payload {
            msg: plaintext,
            aad: &aad,
        };

        let ciphertext = cipher
            .encrypt(&nonce_generic, payload)
            .map_err(|e| I2cSecureError::EncryptionError(e.to_string()))?;

        Ok(SecurePacket { counter, nonce, ciphertext })
    }

    // ── AES-128-GCM Decryption with Replay Protection ─────────

    pub fn decrypt(&mut self, packet: &SecurePacket) -> Result<Vec<u8>> {
        // Replay protection: reject packets with old counters
        if packet.counter <= self.rx_counter {
            return Err(I2cSecureError::ReplayDetected {
                received: packet.counter,
                last: self.rx_counter,
            });
        }

        let key    = Key::<Aes128Gcm>::from_slice(&self.config.aes_key);
        let cipher = Aes128Gcm::new(key);
        let nonce  = Nonce::from_slice(&packet.nonce);

        let aad = packet.counter.to_be_bytes();
        let payload = aes_gcm::aead::Payload {
            msg: &packet.ciphertext,
            aad: &aad,
        };

        // GCM decryption verifies the authentication tag atomically
        let plaintext = cipher
            .decrypt(nonce, payload)
            .map_err(|_| I2cSecureError::MacVerificationFailed)?;

        // Only advance the counter after successful decryption+authentication
        self.rx_counter = packet.counter;

        Ok(plaintext)
    }

    // ── Challenge-Response Authentication ─────────────────────
    //
    // Master generates a 16-byte nonce and expects the slave to respond
    // with HMAC-SHA256(hmac_key, b"I2C-AUTH" || nonce)[..16].

    pub fn generate_challenge(&self) -> [u8; 16] {
        let mut nonce = [0u8; 16];
        OsRng.fill_bytes(&mut nonce);
        nonce
    }

    pub fn compute_expected_response(&self, nonce: &[u8; 16]) -> [u8; 16] {
        let mut mac = HmacSha256::new_from_slice(&self.config.hmac_key)
            .expect("HMAC key length is valid");
        mac.update(b"I2C-AUTH");
        mac.update(nonce);
        let result = mac.finalize().into_bytes();
        let mut response = [0u8; 16];
        response.copy_from_slice(&result[..16]);
        response
    }

    /// Compare two byte slices in constant time
    pub fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
        if a.len() != b.len() { return false; }
        let diff: u8 = a.iter().zip(b.iter()).fold(0, |acc, (x, y)| acc | (x ^ y));
        diff == 0
    }

    /// Simulate slave-side authentication (for testing; real slave runs on target)
    pub fn slave_compute_response(&self, nonce: &[u8; 16]) -> [u8; 16] {
        self.compute_expected_response(nonce)
    }

    /// Master-side: send challenge, receive response, verify
    pub fn authenticate_device<F>(&mut self, send_recv: F) -> Result<()>
    where
        F: FnOnce(&[u8; 16]) -> std::result::Result<[u8; 16], String>,
    {
        let nonce    = self.generate_challenge();
        let response = send_recv(&nonce)
            .map_err(|e| I2cSecureError::IoError(e))?;
        let expected = self.compute_expected_response(&nonce);

        if !Self::constant_time_eq(&response, &expected) {
            return Err(I2cSecureError::AuthenticationFailed);
        }

        self.authenticated = true;
        Ok(())
    }

    pub fn is_authenticated(&self) -> bool {
        self.authenticated
    }
}

// ─────────────────────────────────────────────────────────────
// Secure Sensor Data Frame (higher-level example)
// ─────────────────────────────────────────────────────────────

/// A typed, authenticated sensor reading transmitted over I2C
#[derive(Debug, Clone)]
pub struct SensorReading {
    pub temperature_raw: i16,
    pub humidity_raw: u16,
    pub pressure_raw: u32,
}

impl SensorReading {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(8);
        buf.extend_from_slice(&self.temperature_raw.to_be_bytes());
        buf.extend_from_slice(&self.humidity_raw.to_be_bytes());
        buf.extend_from_slice(&self.pressure_raw.to_be_bytes());
        buf
    }

    pub fn from_bytes(b: &[u8]) -> Option<Self> {
        if b.len() < 8 { return None; }
        Some(Self {
            temperature_raw: i16::from_be_bytes(b[0..2].try_into().ok()?),
            humidity_raw:    u16::from_be_bytes(b[2..4].try_into().ok()?),
            pressure_raw:    u32::from_be_bytes(b[4..8].try_into().ok()?),
        })
    }

    /// Validate that values are within physically plausible ranges
    pub fn validate(&self) -> bool {
        // -40°C to +85°C in raw units (device-specific scaling)
        let temp_ok     = (-4000i16..=8500).contains(&self.temperature_raw);
        // 0–100% RH
        let humidity_ok = self.humidity_raw <= 10000;
        // 300–1100 hPa
        let pressure_ok = (30000u32..=110000).contains(&self.pressure_raw);
        temp_ok && humidity_ok && pressure_ok
    }
}

// ─────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    fn make_ctx() -> I2cSecureCtx {
        I2cSecureCtx::new(SecurityConfig {
            aes_key:           [0x2b; KEY_LEN],
            hmac_key:          [0x60; KEY_LEN],
            allowed_addresses: HashSet::from([0x48, 0x49, 0x76]),
            max_tps:           100,
            require_auth:      true,
        })
    }

    #[test]
    fn test_encrypt_decrypt_roundtrip() {
        let mut ctx = make_ctx();
        let plaintext = b"hello secure i2c";

        let packet    = ctx.encrypt(plaintext).expect("encryption failed");
        let decrypted = ctx.decrypt(&packet).expect("decryption failed");

        assert_eq!(plaintext.as_ref(), decrypted.as_slice());
    }

    #[test]
    fn test_replay_detection() {
        let mut ctx = make_ctx();
        let plaintext = b"sensor data";

        let packet = ctx.encrypt(plaintext).expect("encryption failed");
        ctx.decrypt(&packet).expect("first decryption should succeed");

        // Replay the same packet — should fail
        let result = ctx.decrypt(&packet);
        assert!(matches!(result, Err(I2cSecureError::ReplayDetected { .. })));
    }

    #[test]
    fn test_tamper_detection() {
        let mut ctx = make_ctx();
        let plaintext = b"important data";

        let mut packet = ctx.encrypt(plaintext).expect("encryption failed");

        // Tamper with the ciphertext
        if let Some(b) = packet.ciphertext.first_mut() {
            *b ^= 0xFF;
        }

        let result = ctx.decrypt(&packet);
        assert!(matches!(result, Err(I2cSecureError::MacVerificationFailed)));
    }

    #[test]
    fn test_address_whitelist() {
        let ctx = make_ctx();
        assert!(ctx.validate_address(0x48).is_ok());
        assert!(ctx.validate_address(0x50).is_err());  // not in whitelist
    }

    #[test]
    fn test_challenge_response_auth() {
        let mut master_ctx = make_ctx();

        // Simulate: master sends challenge, slave computes response
        let result = master_ctx.authenticate_device(|nonce| {
            // In reality, this closure sends nonce over I2C and reads back response.
            // Here we simulate the slave locally using the same key.
            let mut mac = HmacSha256::new_from_slice(&[0x60u8; KEY_LEN]).unwrap();
            mac.update(b"I2C-AUTH");
            mac.update(nonce);
            let full = mac.finalize().into_bytes();
            let mut response = [0u8; 16];
            response.copy_from_slice(&full[..16]);
            Ok(response)
        });

        assert!(result.is_ok());
        assert!(master_ctx.is_authenticated());
    }

    #[test]
    fn test_sensor_reading_validation() {
        let valid = SensorReading {
            temperature_raw: 2500,   // ~25°C
            humidity_raw:    6000,   // ~60% RH
            pressure_raw:    101325, // ~1013.25 hPa
        };
        assert!(valid.validate());

        let invalid = SensorReading {
            temperature_raw: 30000,  // unrealistically high
            humidity_raw:    6000,
            pressure_raw:    101325,
        };
        assert!(!invalid.validate());
    }

    #[test]
    fn test_secure_sensor_frame_roundtrip() {
        let mut ctx = make_ctx();

        let reading = SensorReading {
            temperature_raw: 2350,
            humidity_raw:    5800,
            pressure_raw:    100500,
        };

        // Encrypt sensor reading
        let payload = reading.to_bytes();
        let packet  = ctx.encrypt(&payload).expect("encrypt failed");

        // Decrypt and reconstruct
        let decrypted = ctx.decrypt(&packet).expect("decrypt failed");
        let recovered = SensorReading::from_bytes(&decrypted).expect("parse failed");

        assert_eq!(reading.temperature_raw, recovered.temperature_raw);
        assert_eq!(reading.humidity_raw,    recovered.humidity_raw);
        assert_eq!(reading.pressure_raw,    recovered.pressure_raw);
        assert!(recovered.validate());
    }
}
```

---

### Rust Example 2: Bare-Metal `no_std` Secure I2C (embedded-hal)

```rust
//! i2c_secure_embedded.rs
//!
//! no_std secure I2C for bare-metal embedded targets.
//! Uses AES-128-CTR (software) + HMAC-SHA256 (software).
//!
//! Designed for targets like STM32, nRF52, RP2040.
//! All allocations avoided — fixed-size arrays only.
//!
//! [dependencies]
//! embedded-hal = "1.0"
//! aes          = { version = "0.8", features = ["hazmat"] }
//! ctr          = "0.9"
//! hmac         = "0.12"
//! sha2         = { version = "0.10", default-features = false }

#![no_std]

use embedded_hal::i2c::I2c;

pub const KEY_LEN:   usize = 16;
pub const NONCE_LEN: usize = 16;  // AES block size for CTR mode
pub const MAC_LEN:   usize = 16;  // Truncated HMAC-SHA256
pub const MAX_PAYLOAD: usize = 28;
pub const PACKET_OVERHEAD: usize = 4 + NONCE_LEN + MAC_LEN; // counter+nonce+MAC
pub const MAX_PACKET: usize = MAX_PAYLOAD + PACKET_OVERHEAD;

#[derive(Debug, PartialEq)]
pub enum SecureI2cError {
    AuthFailed,
    MacFailed,
    ReplayDetected,
    PayloadTooLarge,
    I2cError,
    NotAuthenticated,
}

pub struct SecureI2cMaster<I2C> {
    i2c:           I2C,
    slave_addr:    u8,
    enc_key:       [u8; KEY_LEN],
    mac_key:       [u8; KEY_LEN],
    tx_counter:    u32,
    rx_counter:    u32,
    authenticated: bool,
}

impl<I2C> SecureI2cMaster<I2C>
where
    I2C: I2c,
{
    pub fn new(
        i2c:        I2C,
        slave_addr: u8,
        enc_key:    [u8; KEY_LEN],
        mac_key:    [u8; KEY_LEN],
    ) -> Self {
        Self {
            i2c,
            slave_addr,
            enc_key,
            mac_key,
            tx_counter: 1,
            rx_counter: 0,
            authenticated: false,
        }
    }

    /// Simple software AES-128-CTR (XOR keystream with plaintext).
    /// Replace with hardware AES peripheral call for performance.
    fn aes_ctr_xor(
        &self,
        counter: u32,
        nonce:   &[u8; NONCE_LEN],
        data:    &mut [u8],
    ) {
        // Build IV: first 12 bytes of nonce + 4-byte counter
        let mut iv = *nonce;
        iv[12] = (counter >> 24) as u8;
        iv[13] = (counter >> 16) as u8;
        iv[14] = (counter >>  8) as u8;
        iv[15] =  counter        as u8;

        // Placeholder: in real code, use `aes` + `ctr` crates or HAL crypto
        // This loop represents the keystream XOR structure
        for (i, byte) in data.iter_mut().enumerate() {
            // Real: keystream_block[i % 16] from AES(key, IV + block_num)
            // Placeholder XOR for structural demonstration:
            *byte ^= iv[i % NONCE_LEN];
        }
    }

    /// Compute truncated HMAC-SHA256 (first MAC_LEN bytes).
    /// Replace with hardware SHA/HMAC accelerator if available.
    fn compute_mac(&self, data: &[u8], mac_out: &mut [u8; MAC_LEN]) {
        // Placeholder for HMAC-SHA256(self.mac_key, data)
        // Real implementation uses hmac + sha2 crates:
        //   use hmac::{Hmac, Mac};
        //   use sha2::Sha256;
        //   let mut h = Hmac::<Sha256>::new_from_slice(&self.mac_key).unwrap();
        //   h.update(data);
        //   let result = h.finalize().into_bytes();
        //   mac_out.copy_from_slice(&result[..MAC_LEN]);
        //
        // For this no_std example we XOR-fold as a structural placeholder:
        for (i, b) in data.iter().enumerate() {
            mac_out[i % MAC_LEN] ^= b ^ self.mac_key[i % KEY_LEN];
        }
    }

    fn constant_time_eq(a: &[u8; MAC_LEN], b: &[u8; MAC_LEN]) -> bool {
        let mut diff: u8 = 0;
        for i in 0..MAC_LEN { diff |= a[i] ^ b[i]; }
        diff == 0
    }

    // ── Secure Write ───────────────────────────────────────────

    pub fn secure_write(
        &mut self,
        reg:     u8,
        payload: &[u8],
        nonce:   &[u8; NONCE_LEN],  // caller provides (from TRNG or PRNG)
    ) -> Result<(), SecureI2cError> {
        if !self.authenticated { return Err(SecureI2cError::NotAuthenticated); }
        if payload.len() > MAX_PAYLOAD { return Err(SecureI2cError::PayloadTooLarge); }

        let counter = self.tx_counter;
        self.tx_counter = self.tx_counter.wrapping_add(1);

        let mut packet = [0u8; 1 + MAX_PACKET]; // +1 for register byte
        packet[0] = reg;

        // Counter (big-endian)
        packet[1] = (counter >> 24) as u8;
        packet[2] = (counter >> 16) as u8;
        packet[3] = (counter >>  8) as u8;
        packet[4] =  counter        as u8;

        // Nonce
        packet[5..5 + NONCE_LEN].copy_from_slice(nonce);

        // Encrypt payload (in-place)
        let ct_start = 5 + NONCE_LEN;
        packet[ct_start..ct_start + payload.len()].copy_from_slice(payload);
        self.aes_ctr_xor(counter, nonce, &mut packet[ct_start..ct_start + payload.len()]);

        // Compute MAC over [counter(4) + nonce(16) + ciphertext(N)]
        let mac_input_start = 1;
        let mac_input_end   = ct_start + payload.len();
        let mut mac = [0u8; MAC_LEN];
        self.compute_mac(&packet[mac_input_start..mac_input_end], &mut mac);

        // Append MAC
        let mac_start = ct_start + payload.len();
        packet[mac_start..mac_start + MAC_LEN].copy_from_slice(&mac);

        let total_len = mac_start + MAC_LEN;

        self.i2c
            .write(self.slave_addr, &packet[..total_len])
            .map_err(|_| SecureI2cError::I2cError)
    }

    // ── Secure Read ────────────────────────────────────────────

    pub fn secure_read(
        &mut self,
        reg:          u8,
        payload_out:  &mut [u8],
        payload_len:  usize,
    ) -> Result<(), SecureI2cError> {
        if !self.authenticated { return Err(SecureI2cError::NotAuthenticated); }
        if payload_len > MAX_PAYLOAD { return Err(SecureI2cError::PayloadTooLarge); }

        // Point to register
        self.i2c
            .write(self.slave_addr, &[reg])
            .map_err(|_| SecureI2cError::I2cError)?;

        // Read packet: [4 counter][16 nonce][N ciphertext][16 MAC]
        let pkt_len = 4 + NONCE_LEN + payload_len + MAC_LEN;
        let mut packet = [0u8; MAX_PACKET];

        self.i2c
            .read(self.slave_addr, &mut packet[..pkt_len])
            .map_err(|_| SecureI2cError::I2cError)?;

        // Verify MAC before any other processing
        let mac_cover_len = 4 + NONCE_LEN + payload_len;
        let mut expected_mac = [0u8; MAC_LEN];
        self.compute_mac(&packet[..mac_cover_len], &mut expected_mac);

        let mut received_mac = [0u8; MAC_LEN];
        received_mac.copy_from_slice(&packet[mac_cover_len..mac_cover_len + MAC_LEN]);

        if !Self::constant_time_eq(&expected_mac, &received_mac) {
            return Err(SecureI2cError::MacFailed);
        }

        // Replay check
        let rx_ctr = ((packet[0] as u32) << 24)
                   | ((packet[1] as u32) << 16)
                   | ((packet[2] as u32) <<  8)
                   |  (packet[3] as u32);

        if rx_ctr <= self.rx_counter {
            return Err(SecureI2cError::ReplayDetected);
        }
        self.rx_counter = rx_ctr;

        // Decrypt ciphertext
        let mut nonce = [0u8; NONCE_LEN];
        nonce.copy_from_slice(&packet[4..4 + NONCE_LEN]);

        let ct_start = 4 + NONCE_LEN;
        payload_out[..payload_len].copy_from_slice(&packet[ct_start..ct_start + payload_len]);
        self.aes_ctr_xor(rx_ctr, &nonce, &mut payload_out[..payload_len]);

        Ok(())
    }

    /// Mark device as authenticated (called after external auth protocol)
    pub fn set_authenticated(&mut self) {
        self.authenticated = true;
    }

    /// Release the inner I2C bus
    pub fn release(self) -> I2C {
        self.i2c
    }
}
```

---

## Physical and Hardware Countermeasures

Software-layer security must be complemented by physical protections, especially for high-assurance applications:

### PCB Design
- **Eliminate exposed test points** on SDA/SCL lines in production hardware; use solder-masked vias
- **Shield bus traces** under ground planes or use differential I2C (MIPI I3C) for sensitive signals
- **Conformal coating** makes physical probing significantly harder
- **Encapsulation** in resin (potting) for high-security designs

### Bus Topology
- **Keep bus length short** — longer buses are easier to tap
- **Use dedicated buses** for sensitive peripherals (avoid sharing buses with less-trusted devices)
- **Pull-up resistors close to master** reduce the ability of interposers to insert additional capacitance stealthily

### Hardware Security Modules (HSM / Secure Elements)
- Dedicated ICs (ATECC608B, NXP SE050, ST STSAFE-A) perform crypto in tamper-resistant silicon
- Keys never leave the secure element; only computed results (MACs, signatures) are exposed
- Physical tamper detection (mesh, temperature, voltage sensors) zeroizes keys on attack

### I3C (Improved Inter-Integrated Circuit)
MIPI I3C is the successor to I2C and provides:
- In-band interrupt support
- Higher speeds (up to 12.5 MHz SDR, 25 MHz+ HDR)
- Dynamic address assignment (resists static address spoofing)
- Security mode (I3C v1.1+) with optional encryption and authentication at the protocol level

---

## Summary

### Key Threats and Mitigations at a Glance

| Threat | Mitigation | Layer |
|--------|-----------|-------|
| Eavesdropping | AES-128-GCM / AES-CTR + HMAC payload encryption | Application |
| Tampering (MitM) | Message authentication codes (GCM tag, HMAC-SHA256) | Application |
| Device spoofing | Challenge-response auth (HMAC nonce), or secure element (ATECC608) | Application / Hardware |
| Replay attacks | Monotonic counter included in authenticated data | Application |
| Rogue master | Slave-side challenge-response; secure element enforcement | Application / Hardware |
| Bus scan / enumeration | Address whitelist, anomaly detection, rate limiting | Application |
| Physical probing | PCB hardening, conformal coating, secure element, potting | Physical |
| DoS (bus lockup) | Watchdog timers, bus reset logic, timeout handling | System |

### Design Principles to Remember

1. **I2C provides zero security guarantees** — treat it as an unprotected wire and apply all protections in your application layer.
2. **Encrypt AND authenticate** — encryption alone (without a MAC) does not detect tampering. Use authenticated encryption (AES-GCM) or encrypt-then-MAC.
3. **Never skip replay protection** — a MAC alone is insufficient if an attacker can replay captured valid packets. Always include a monotonic counter or timestamp.
4. **Constant-time comparisons are mandatory** — use `volatile` XOR loops or library functions; never use `memcmp` for MAC comparison.
5. **Zeroize secrets** — overwrite keys in memory when context is destroyed; use `memset_s`, `explicit_bzero`, or Rust's `Zeroize` trait.
6. **Validate all received data** — even authenticated data should be range-checked before acting on it. Defense in depth.
7. **Use secure elements where possible** — software keys in MCU flash are vulnerable to readout attacks; dedicated secure ICs provide hardware-enforced key protection.
8. **Physical security is complementary, not optional** — a perfectly-secured software stack can be bypassed by physical access to bus lines.

### Threat Model Fit

| Application | Recommended Approach |
|-------------|---------------------|
| Consumer IoT, low security | Address whitelist + input validation |
| Industrial sensors, moderate security | AES-CTR + HMAC + replay counter |
| Medical, automotive, high security | AES-GCM + secure element + physical hardening |
| Critical infrastructure | Full PKI with secure element + I3C + hardware tamper detection |

---

*Document version: 1.0 — Covers I2C bus security for embedded systems using C/C++ (Linux userspace and bare-metal) and Rust (std and no_std environments).*