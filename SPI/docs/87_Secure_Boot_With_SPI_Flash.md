# 87. Secure Boot with SPI Flash

**Architecture & Theory**
- Chain of Trust diagram (ROM → BL1 → BL2 → Application)
- Cryptographic foundations — signing at build time vs. verification at boot
- SPI Flash memory layout with typical offset assignments

**C/C++ Implementation (mbedTLS)**
- SPI Flash HAL abstraction layer
- Firmware image header struct (`firmware_header_t`, `repr(C, packed)`)
- Full verifier: header CRC → OTP key hash check → body hash → ECDSA P-256 verify
- Host-side OpenSSL signing tool
- A/B slot manager with fallback logic

**Rust Implementation (no_std)**
- `p256` + `sha2` crate usage with `no_std` compatibility
- Trait-based `SpiFlash` and `OtpStorage` abstractions
- Streaming body hasher, constant-time comparisons, full verifier struct
- Bare-metal Cortex-M entry point with inline assembly jump
- Rust host-side signing tool

**Security Hardening**
- OTP/eFuse provisioning flow with manufacturing steps
- Anti-rollback via NOR Flash monotonic bit-clearing counter
- Fault injection mitigations (double-check pattern)
- Timing attack prevention (constant-time comparisons)
- Notes on JTAG lockdown, key revocation, and encryption vs. authentication

## Chain of Trust Verification Using Digitally Signed Code in SPI Memory

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Chain of Trust Architecture](#chain-of-trust-architecture)
4. [Cryptographic Foundations](#cryptographic-foundations)
5. [SPI Flash Memory Layout for Secure Boot](#spi-flash-memory-layout-for-secure-boot)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Key Provisioning and OTP/eFuse](#key-provisioning-and-otpefuse)
9. [Anti-Rollback Protection](#anti-rollback-protection)
10. [Practical Considerations and Pitfalls](#practical-considerations-and-pitfalls)
11. [Summary](#summary)

---

## Introduction

**Secure Boot** is a security mechanism that ensures a device only executes firmware or software that has been cryptographically verified and authorized. When combined with **SPI Flash memory** — the most common non-volatile storage medium in embedded systems — it forms the backbone of hardware root-of-trust for microcontrollers, SoCs, and embedded Linux systems.

Without Secure Boot, an attacker with physical or remote access to a device could replace legitimate firmware with malicious code, install backdoors, or extract sensitive data. Secure Boot prevents this by verifying a **digital signature** on every piece of code before execution, tracing back to a trusted anchor burned into the hardware itself.

Secure Boot is mandated or strongly recommended in:
- IoT device security frameworks (ETSI EN 303 645, IEC 62443)
- Industrial control systems
- Automotive ECUs (ISO/SAE 21434)
- Medical devices (FDA cybersecurity guidance)
- Consumer electronics and smart home devices

---

## Core Concepts

### What is SPI Flash?

Serial Peripheral Interface (SPI) Flash is a category of NOR Flash memory accessed via the SPI bus. It is the dominant storage medium for bootloaders and firmware in embedded systems because:

- It supports **eXecute-In-Place (XIP)** (on many variants), allowing code to run directly from flash
- It offers **byte-addressable reads** with fast random access
- It is **cheap, compact, and widely available** (W25Q series, MX25L series, etc.)
- It is typically connected via 4-wire SPI (MOSI, MISO, CLK, CS) or quad-SPI (QSPI) for higher throughput

### Digital Signatures

A **digital signature** proves:
1. **Authenticity** — the code was signed by the holder of the private key
2. **Integrity** — the code has not been tampered with since signing

Common algorithms used:
| Algorithm | Key Size | Notes |
|-----------|----------|-------|
| RSA-2048/4096 | 2048–4096 bit | Widely supported, larger signatures |
| ECDSA (P-256, P-384) | 256–384 bit | Compact keys, efficient on constrained MCUs |
| Ed25519 | 255 bit | Fast, deterministic, increasingly popular |

### Root of Trust (RoT)

The **Root of Trust** is the anchor of the entire security chain. It is an immutable, hardware-stored public key hash (typically burned into OTP/eFuse memory) that cannot be altered after manufacturing. Every verification step traces back to this anchor.

---

## Chain of Trust Architecture

The chain of trust is a sequence of verified handoffs from hardware to the final application:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         HARDWARE ROOT OF TRUST                      │
│              (Public Key Hash in OTP/eFuse — Immutable)             │
└────────────────────────────┬────────────────────────────────────────┘
                             │ verifies
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    STAGE 0: ROM Bootloader (BL0)                    │
│         (Mask ROM — Cannot be modified, executes from chip ROM)     │
│   - Reads public key from SPI Flash                                 │
│   - Verifies key hash against OTP                                   │
│   - Verifies BL1 signature with that key                            │
└────────────────────────────┬────────────────────────────────────────┘
                             │ verifies & jumps to
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   STAGE 1: First Stage Bootloader (BL1)             │
│                        (Stored in SPI Flash)                        │
│   - Reads BL2 image from SPI Flash                                  │
│   - Verifies BL2 signature                                          │
│   - Optionally decrypts BL2                                         │
└────────────────────────────┬────────────────────────────────────────┘
                             │ verifies & jumps to
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                 STAGE 2: Second Stage Bootloader (BL2)              │
│               (e.g., U-Boot, MCUboot, custom bootloader)            │
│   - Verifies application firmware or OS kernel                      │
│   - Handles A/B slot selection                                      │
│   - Implements rollback protection                                  │
└────────────────────────────┬────────────────────────────────────────┘
                             │ verifies & jumps to
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    APPLICATION / OS KERNEL                          │
│                    (Fully verified, trusted execution)              │
└─────────────────────────────────────────────────────────────────────┘
```

**Each link in the chain:**
- Verifies the cryptographic signature of the **next stage** before transferring control
- If any verification fails, the boot process **halts or enters a recovery state**
- No stage is trusted unless the previous stage has explicitly verified it

---

## Cryptographic Foundations

### Signing a Firmware Image (Build-Time)

At build time, the firmware binary is:
1. Hashed (e.g., SHA-256)
2. The hash is signed with the **developer's private key** (RSA, ECDSA, or Ed25519)
3. The signature and public key (or public key hash) are appended to the image header

### Verifying at Boot Time

At boot time, the bootloader:
1. Reads the image header to obtain the public key and signature
2. Verifies the public key against the hardware-stored key hash (OTP)
3. Computes the hash of the firmware body
4. Verifies the signature against the hash using the public key

```
Signing (offline):
  firmware.bin ──► SHA-256 ──► hash ──► ECDSA_Sign(private_key) ──► signature
                                                                          │
  firmware_header = { magic, version, size, hash, signature, pubkey }     │
                                                                          ▼
                                                                  signed_firmware.bin

Verification (on-device):
  signed_firmware.bin ──► parse header ──► extract pubkey, signature, image_body
                                 │
                                 ├──► SHA-256(image_body) ──► computed_hash
                                 │
                                 ├──► verify pubkey_hash == OTP_stored_hash
                                 │
                                 └──► ECDSA_Verify(pubkey, computed_hash, signature)
                                               │
                                       PASS ◄──┴──► FAIL (halt)
```

---

## SPI Flash Memory Layout for Secure Boot

A typical secure boot SPI flash layout:

```
SPI Flash (e.g., 8 MB)
┌──────────────────────────┐ 0x000000
│  Bootloader (BL1)        │  64 KB
│  + Signature Header      │
├──────────────────────────┤ 0x010000
│  Public Key Store        │   4 KB
│  (with OTP-verified hash)│
├──────────────────────────┤ 0x011000
│  Bootloader Config /     │   4 KB
│  Device Tree / Params    │
├──────────────────────────┤ 0x012000
│  Anti-Rollback Counter   │   4 KB
│  (Monotonic counter)     │
├──────────────────────────┤ 0x013000
│  Application Slot A      │ ~3.5 MB
│  (Signed firmware image) │
├──────────────────────────┤ 0x370000
│  Application Slot B      │ ~3.5 MB
│  (OTA update target)     │
├──────────────────────────┤ 0x6D0000
│  Factory Data / Certs    │  64 KB
├──────────────────────────┤ 0x6E0000
│  NVS / Settings          │ ~128 KB
└──────────────────────────┘ 0x7FFFFF
```

### Image Header Format

```c
/* Firmware image header for secure boot */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 0x53425446 "SBTF" */
    uint32_t header_version;  /* Header format version */
    uint32_t image_version;   /* Firmware version (anti-rollback) */
    uint32_t image_size;      /* Size of firmware body in bytes */
    uint32_t load_address;    /* Target RAM/flash execution address */
    uint32_t entry_point;     /* Entry point offset */
    uint8_t  image_hash[32];  /* SHA-256 of firmware body */
    uint8_t  public_key[64];  /* ECDSA P-256 public key (uncompressed X+Y) */
    uint8_t  signature[64];   /* ECDSA P-256 signature (r+s) */
    uint32_t flags;           /* Feature flags */
    uint32_t header_crc;      /* CRC32 of header (excl. this field) */
} firmware_header_t;
```

---

## Implementation in C/C++

The following examples use **mbedTLS** (widely used in embedded systems) for cryptographic operations and assume a generic SPI Flash HAL.

### SPI Flash HAL (Hardware Abstraction Layer)

```c
/* spi_flash_hal.h — Minimal SPI Flash HAL for secure boot */
#ifndef SPI_FLASH_HAL_H
#define SPI_FLASH_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SPI_FLASH_PAGE_SIZE     256
#define SPI_FLASH_SECTOR_SIZE   4096

typedef enum {
    SPI_FLASH_OK = 0,
    SPI_FLASH_ERR_TIMEOUT,
    SPI_FLASH_ERR_IO,
    SPI_FLASH_ERR_BOUNDS,
} spi_flash_err_t;

/* Read len bytes from SPI flash at offset into buf */
spi_flash_err_t spi_flash_read(uint32_t offset, void *buf, size_t len);

/* Write page-aligned data (caller must erase first) */
spi_flash_err_t spi_flash_write(uint32_t offset, const void *buf, size_t len);

/* Erase a 4K sector */
spi_flash_err_t spi_flash_erase_sector(uint32_t offset);

#endif /* SPI_FLASH_HAL_H */
```

### Image Header Definition

```c
/* secure_boot.h */
#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#include <stdint.h>
#include <stdbool.h>

#define FIRMWARE_MAGIC          0x53425446UL  /* "SBTF" */
#define FIRMWARE_HEADER_VER     1
#define SECURE_BOOT_PUBKEY_LEN  64    /* ECDSA P-256 uncompressed (no 0x04 prefix) */
#define SECURE_BOOT_SIG_LEN     64    /* ECDSA P-256 signature r||s */
#define SECURE_BOOT_HASH_LEN    32    /* SHA-256 */

/* Slot definitions */
#define SLOT_A_OFFSET           0x013000UL
#define SLOT_B_OFFSET           0x370000UL
#define SLOT_MAX_SIZE           (3UL * 1024 * 1024)

/* Anti-rollback counter storage offset */
#define ROLLBACK_CTR_OFFSET     0x012000UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_version;       /* Must be >= stored anti-rollback counter */
    uint32_t image_size;
    uint32_t load_address;
    uint32_t entry_point;
    uint8_t  image_hash[SECURE_BOOT_HASH_LEN];
    uint8_t  public_key[SECURE_BOOT_PUBKEY_LEN];
    uint8_t  signature[SECURE_BOOT_SIG_LEN];
    uint32_t flags;
    uint32_t header_crc;          /* CRC32 of bytes 0..(sizeof-4) */
} firmware_header_t;

typedef enum {
    SECURE_BOOT_OK = 0,
    SECURE_BOOT_ERR_MAGIC,
    SECURE_BOOT_ERR_HEADER_CRC,
    SECURE_BOOT_ERR_KEY_HASH,
    SECURE_BOOT_ERR_HASH_MISMATCH,
    SECURE_BOOT_ERR_SIG_INVALID,
    SECURE_BOOT_ERR_ROLLBACK,
    SECURE_BOOT_ERR_IO,
} secure_boot_err_t;

/**
 * Verify and boot firmware from the given SPI flash slot offset.
 * Does NOT return on success (jumps to firmware entry point).
 * Returns error code on failure.
 */
secure_boot_err_t secure_boot_verify_and_boot(uint32_t slot_offset);

#endif /* SECURE_BOOT_H */
```

### Core Verification Logic (mbedTLS)

```c
/* secure_boot.c — Chain of trust verification */

#include "secure_boot.h"
#include "spi_flash_hal.h"
#include "otp_hal.h"          /* OTP/eFuse reading abstraction */

/* mbedTLS headers */
#include "mbedtls/sha256.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/crc.h"      /* Or use your platform CRC32 */

#include <string.h>

/* -----------------------------------------------------------------
 * Internal: compute CRC32 of a buffer
 * ----------------------------------------------------------------- */
static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* -----------------------------------------------------------------
 * Internal: read and validate firmware header
 * ----------------------------------------------------------------- */
static secure_boot_err_t read_and_check_header(uint32_t offset,
                                                firmware_header_t *hdr)
{
    if (spi_flash_read(offset, hdr, sizeof(*hdr)) != SPI_FLASH_OK)
        return SECURE_BOOT_ERR_IO;

    /* 1. Check magic */
    if (hdr->magic != FIRMWARE_MAGIC)
        return SECURE_BOOT_ERR_MAGIC;

    /* 2. Verify header CRC (covers all bytes except the last 4) */
    uint32_t computed_crc = crc32((const uint8_t *)hdr,
                                   sizeof(*hdr) - sizeof(uint32_t));
    if (computed_crc != hdr->header_crc)
        return SECURE_BOOT_ERR_HEADER_CRC;

    return SECURE_BOOT_OK;
}

/* -----------------------------------------------------------------
 * Internal: verify public key against OTP-stored hash
 * ----------------------------------------------------------------- */
static secure_boot_err_t verify_public_key(const uint8_t *pubkey,
                                            size_t pubkey_len)
{
    uint8_t computed_hash[32];
    uint8_t otp_hash[32];

    /* Hash the public key presented in the image header */
    mbedtls_sha256(pubkey, pubkey_len, computed_hash, 0 /* is224=0 */);

    /* Read the expected hash from OTP/eFuse (immutable hardware storage) */
    if (otp_read_pubkey_hash(otp_hash, sizeof(otp_hash)) != OTP_OK)
        return SECURE_BOOT_ERR_IO;

    /* Constant-time comparison to prevent timing attacks */
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++)
        diff |= computed_hash[i] ^ otp_hash[i];

    return (diff == 0) ? SECURE_BOOT_OK : SECURE_BOOT_ERR_KEY_HASH;
}

/* -----------------------------------------------------------------
 * Internal: hash the firmware body from SPI flash (streaming)
 * ----------------------------------------------------------------- */
#define HASH_CHUNK_SIZE 512

static secure_boot_err_t hash_firmware_body(uint32_t offset,
                                             uint32_t size,
                                             uint8_t out_hash[32])
{
    mbedtls_sha256_context ctx;
    uint8_t chunk[HASH_CHUNK_SIZE];
    uint32_t remaining = size;
    uint32_t pos = offset;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0 /* is224 */);

    while (remaining > 0) {
        uint32_t to_read = (remaining < HASH_CHUNK_SIZE) ? remaining
                                                          : HASH_CHUNK_SIZE;
        if (spi_flash_read(pos, chunk, to_read) != SPI_FLASH_OK) {
            mbedtls_sha256_free(&ctx);
            return SECURE_BOOT_ERR_IO;
        }
        mbedtls_sha256_update(&ctx, chunk, to_read);
        pos       += to_read;
        remaining -= to_read;
    }

    mbedtls_sha256_finish(&ctx, out_hash);
    mbedtls_sha256_free(&ctx);
    return SECURE_BOOT_OK;
}

/* -----------------------------------------------------------------
 * Internal: verify ECDSA P-256 signature
 * ----------------------------------------------------------------- */
static secure_boot_err_t verify_ecdsa_signature(
        const uint8_t pubkey[64],   /* X||Y uncompressed, no 0x04 prefix */
        const uint8_t hash[32],
        const uint8_t signature[64] /* r||s */
)
{
    int ret;
    mbedtls_ecdsa_context ctx;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;

    mbedtls_ecdsa_init(&ctx);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    /* Load the P-256 group */
    ret = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) goto cleanup;

    /* Import the public key point from raw X||Y bytes */
    ret = mbedtls_mpi_read_binary(&Q.X, pubkey,      32);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_read_binary(&Q.Y, pubkey + 32, 32);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_lset(&Q.Z, 1);
    if (ret != 0) goto cleanup;

    /* Validate the point is on the curve */
    ret = mbedtls_ecp_check_pubkey(&ctx.grp, &Q);
    if (ret != 0) goto cleanup;

    /* Copy Q into context */
    ret = mbedtls_ecp_copy(&ctx.Q, &Q);
    if (ret != 0) goto cleanup;

    /* Import r and s from raw bytes */
    ret = mbedtls_mpi_read_binary(&r, signature,      32);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_read_binary(&s, signature + 32, 32);
    if (ret != 0) goto cleanup;

    /* Verify the signature */
    ret = mbedtls_ecdsa_verify(&ctx.grp, hash, 32, &ctx.Q, &r, &s);

cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecdsa_free(&ctx);

    return (ret == 0) ? SECURE_BOOT_OK : SECURE_BOOT_ERR_SIG_INVALID;
}

/* -----------------------------------------------------------------
 * Internal: check anti-rollback version
 * ----------------------------------------------------------------- */
static secure_boot_err_t check_rollback(uint32_t image_version)
{
    uint32_t min_version;

    if (spi_flash_read(ROLLBACK_CTR_OFFSET, &min_version,
                       sizeof(min_version)) != SPI_FLASH_OK)
        return SECURE_BOOT_ERR_IO;

    return (image_version >= min_version) ? SECURE_BOOT_OK
                                          : SECURE_BOOT_ERR_ROLLBACK;
}

/* -----------------------------------------------------------------
 * Public API: verify and boot
 * ----------------------------------------------------------------- */
secure_boot_err_t secure_boot_verify_and_boot(uint32_t slot_offset)
{
    secure_boot_err_t err;
    firmware_header_t hdr;
    uint8_t computed_hash[32];

    /* Step 1: Read and structurally validate the header */
    err = read_and_check_header(slot_offset, &hdr);
    if (err != SECURE_BOOT_OK) return err;

    /* Step 2: Verify the public key against the OTP-stored hash */
    err = verify_public_key(hdr.public_key, sizeof(hdr.public_key));
    if (err != SECURE_BOOT_OK) return err;

    /* Step 3: Check anti-rollback counter */
    err = check_rollback(hdr.image_version);
    if (err != SECURE_BOOT_OK) return err;

    /* Step 4: Hash the firmware body from SPI Flash */
    uint32_t body_offset = slot_offset + sizeof(firmware_header_t);
    err = hash_firmware_body(body_offset, hdr.image_size, computed_hash);
    if (err != SECURE_BOOT_OK) return err;

    /* Step 5: Verify the hash matches the header's declared hash */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++)
        diff |= computed_hash[i] ^ hdr.image_hash[i];
    if (diff != 0)
        return SECURE_BOOT_ERR_HASH_MISMATCH;

    /* Step 6: Verify the ECDSA signature over the hash */
    err = verify_ecdsa_signature(hdr.public_key, computed_hash, hdr.signature);
    if (err != SECURE_BOOT_OK) return err;

    /*
     * All checks passed — transfer control to the firmware.
     * The entry_point is an offset from the slot start.
     * This is platform-specific: on ARM Cortex-M this typically means
     * loading the vector table address and jumping to the reset handler.
     */
    uint32_t entry = hdr.load_address + hdr.entry_point;

    /* Disable interrupts before jumping */
    __disable_irq();

    /* Set the new vector table (Cortex-M VTOR) */
    SCB->VTOR = hdr.load_address;

    /* Load stack pointer and jump to reset handler */
    typedef void (*entry_fn_t)(void);
    uint32_t *vtor   = (uint32_t *)hdr.load_address;
    uint32_t  sp     = vtor[0];
    entry_fn_t reset = (entry_fn_t)(vtor[1]);

    __set_MSP(sp);
    reset(); /* Does not return */

    /* Unreachable */
    return SECURE_BOOT_OK;
}
```

### Signing Tool (Host-Side C++)

```cpp
/* sign_firmware.cpp — Host tool to sign a firmware binary */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <stdexcept>

/* Use OpenSSL for host-side signing */
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/err.h>

/*
 * firmware_header_t must match the device-side definition exactly.
 * Include the shared header here.
 */
#include "secure_boot.h"

static uint32_t crc32_table_crc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFFUL;
}

static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::string("Cannot open: ") + path);
    size_t size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char *>(buf.data()), size);
    return buf;
}

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <firmware.bin> <private_key.pem> "
                "<image_version> <output.bin>\n", argv[0]);
        return 1;
    }

    const char *fw_path   = argv[1];
    const char *key_path  = argv[2];
    uint32_t    version   = (uint32_t)atoi(argv[3]);
    const char *out_path  = argv[4];

    /* Load firmware binary */
    auto firmware = read_file(fw_path);

    /* Load EC private key */
    FILE *keyfile = fopen(key_path, "r");
    if (!keyfile) { perror("fopen key"); return 1; }
    EVP_PKEY *pkey = PEM_read_PrivateKey(keyfile, nullptr, nullptr, nullptr);
    fclose(keyfile);
    if (!pkey) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    EC_KEY *ec_key = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec_key) { fprintf(stderr, "Not an EC key\n"); return 1; }

    /* Extract raw public key X||Y */
    const EC_POINT *pub = EC_KEY_get0_public_key(ec_key);
    const EC_GROUP *grp = EC_KEY_get0_group(ec_key);
    uint8_t pubkey_raw[64] = {};
    {
        uint8_t uncompressed[65];
        size_t len = EC_POINT_point2oct(grp, pub,
                                         POINT_CONVERSION_UNCOMPRESSED,
                                         uncompressed, sizeof(uncompressed),
                                         nullptr);
        if (len != 65) { fprintf(stderr, "Bad public key length\n"); return 1; }
        /* Skip the 0x04 prefix */
        memcpy(pubkey_raw, uncompressed + 1, 64);
    }

    /* Compute SHA-256 of firmware body */
    uint8_t hash[32];
    SHA256(firmware.data(), firmware.size(), hash);

    /* Sign the hash with ECDSA */
    ECDSA_SIG *sig = ECDSA_do_sign(hash, sizeof(hash), ec_key);
    if (!sig) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    /* Extract r and s as 32-byte big-endian integers */
    uint8_t sig_raw[64] = {};
    {
        const BIGNUM *r, *s;
        ECDSA_SIG_get0(sig, &r, &s);
        BN_bn2binpad(r, sig_raw,      32);
        BN_bn2binpad(s, sig_raw + 32, 32);
    }

    /* Build the firmware header */
    firmware_header_t hdr = {};
    hdr.magic          = FIRMWARE_MAGIC;
    hdr.header_version = FIRMWARE_HEADER_VER;
    hdr.image_version  = version;
    hdr.image_size     = (uint32_t)firmware.size();
    hdr.load_address   = 0x08010000UL; /* Example: STM32 application start */
    hdr.entry_point    = 0;
    memcpy(hdr.image_hash, hash,       32);
    memcpy(hdr.public_key, pubkey_raw, 64);
    memcpy(hdr.signature,  sig_raw,    64);
    hdr.flags          = 0;
    hdr.header_crc     = crc32_table_crc(
                             reinterpret_cast<const uint8_t *>(&hdr),
                             sizeof(hdr) - sizeof(uint32_t));

    /* Write signed image: header + firmware body */
    std::ofstream out(out_path, std::ios::binary);
    if (!out) { perror("fopen output"); return 1; }
    out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char *>(firmware.data()),
              firmware.size());

    printf("Signed firmware written to %s\n", out_path);
    printf("  Image version : %u\n", version);
    printf("  Image size    : %zu bytes\n", firmware.size());

    /* Cleanup */
    ECDSA_SIG_free(sig);
    EC_KEY_free(ec_key);
    EVP_PKEY_free(pkey);

    return 0;
}
```

### A/B Slot Selection with Fallback

```c
/* slot_manager.c — Select the best valid firmware slot */

#include "secure_boot.h"
#include "spi_flash_hal.h"
#include <string.h>

#define SLOT_FLAG_VALID     (1u << 0)
#define SLOT_FLAG_CONFIRMED (1u << 1)

typedef struct {
    uint32_t offset;
    uint32_t version;
    bool     valid;
    bool     confirmed;
} slot_info_t;

static bool probe_slot(uint32_t offset, slot_info_t *info)
{
    firmware_header_t hdr;

    if (spi_flash_read(offset, &hdr, sizeof(hdr)) != SPI_FLASH_OK)
        return false;
    if (hdr.magic != FIRMWARE_MAGIC)
        return false;

    /* Quick CRC check only at this stage — full crypto check done later */
    uint32_t crc = crc32((const uint8_t *)&hdr, sizeof(hdr) - sizeof(uint32_t));
    if (crc != hdr.header_crc)
        return false;

    info->offset    = offset;
    info->version   = hdr.image_version;
    info->valid     = (hdr.flags & SLOT_FLAG_VALID) != 0;
    info->confirmed = (hdr.flags & SLOT_FLAG_CONFIRMED) != 0;
    return true;
}

uint32_t select_boot_slot(void)
{
    slot_info_t slot_a = {0}, slot_b = {0};
    bool have_a = probe_slot(SLOT_A_OFFSET, &slot_a);
    bool have_b = probe_slot(SLOT_B_OFFSET, &slot_b);

    /* Prefer newer confirmed slot; fall back to older or unconfirmed */
    if (have_a && have_b) {
        if (slot_a.confirmed && !slot_b.confirmed)
            return SLOT_A_OFFSET;
        if (slot_b.confirmed && !slot_a.confirmed)
            return SLOT_B_OFFSET;
        /* Both confirmed or neither: pick newer version */
        return (slot_a.version >= slot_b.version) ? SLOT_A_OFFSET
                                                   : SLOT_B_OFFSET;
    }
    if (have_a) return SLOT_A_OFFSET;
    if (have_b) return SLOT_B_OFFSET;

    /* No valid slot found — enter recovery */
    enter_recovery_mode(); /* Platform-specific */
    while (1) {}
}
```

---

## Implementation in Rust

Rust is increasingly used for bootloader development due to its memory safety guarantees without a garbage collector. The following uses the `p256` crate for ECDSA and `sha2` for hashing — both `no_std` compatible.

### Cargo.toml Dependencies

```toml
[package]
name    = "secure-boot-verifier"
version = "0.1.0"
edition = "2021"

[dependencies]
# Elliptic curve cryptography — no_std + no alloc compatible
p256    = { version = "0.13", default-features = false, features = ["ecdsa"] }
sha2    = { version = "0.10", default-features = false }
ecdsa   = { version = "0.16", default-features = false }

# For embedded targets:
[profile.release]
opt-level  = "z"   # Optimize for size
lto        = true
panic      = "abort"
```

### Image Header and Error Types

```rust
// secure_boot/mod.rs

use core::fmt;

pub const FIRMWARE_MAGIC: u32 = 0x5342_5446; // "SBTF"
pub const PUBKEY_LEN: usize = 64;
pub const SIG_LEN: usize = 64;
pub const HASH_LEN: usize = 32;

pub const SLOT_A_OFFSET: u32 = 0x0001_3000;
pub const SLOT_B_OFFSET: u32 = 0x0037_0000;
pub const ROLLBACK_CTR_OFFSET: u32 = 0x0001_2000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SecureBootError {
    IoError,
    BadMagic,
    BadHeaderCrc,
    KeyHashMismatch,
    HashMismatch,
    InvalidSignature,
    RollbackViolation,
    InvalidKeyPoint,
}

impl fmt::Display for SecureBootError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IoError            => write!(f, "SPI flash I/O error"),
            Self::BadMagic           => write!(f, "Invalid firmware magic"),
            Self::BadHeaderCrc       => write!(f, "Header CRC mismatch"),
            Self::KeyHashMismatch    => write!(f, "Public key hash does not match OTP"),
            Self::HashMismatch       => write!(f, "Firmware body hash mismatch"),
            Self::InvalidSignature   => write!(f, "ECDSA signature verification failed"),
            Self::RollbackViolation  => write!(f, "Anti-rollback: image version too old"),
            Self::InvalidKeyPoint    => write!(f, "Public key is not a valid curve point"),
        }
    }
}

/// Firmware image header — must be repr(C, packed) to match the on-flash layout
#[repr(C, packed)]
#[derive(Clone)]
pub struct FirmwareHeader {
    pub magic:          u32,
    pub header_version: u32,
    pub image_version:  u32,
    pub image_size:     u32,
    pub load_address:   u32,
    pub entry_point:    u32,
    pub image_hash:     [u8; HASH_LEN],
    pub public_key:     [u8; PUBKEY_LEN],
    pub signature:      [u8; SIG_LEN],
    pub flags:          u32,
    pub header_crc:     u32,
}

impl FirmwareHeader {
    pub const SIZE: usize = core::mem::size_of::<Self>();
}
```

### SPI Flash Trait

```rust
// spi_flash.rs

use crate::secure_boot::SecureBootError;

pub trait SpiFlash {
    /// Read `len` bytes from flash at `offset` into `buf`.
    fn read(&self, offset: u32, buf: &mut [u8]) -> Result<(), SecureBootError>;
}
```

### OTP Trait

```rust
// otp.rs

use crate::secure_boot::SecureBootError;

pub trait OtpStorage {
    /// Read the 32-byte SHA-256 hash of the trusted public key.
    fn read_pubkey_hash(&self) -> Result<[u8; 32], SecureBootError>;

    /// Read the minimum acceptable firmware version (anti-rollback counter).
    fn read_min_version(&self) -> Result<u32, SecureBootError>;
}
```

### Verifier Implementation

```rust
// verifier.rs

use crate::secure_boot::{
    FirmwareHeader, SecureBootError,
    FIRMWARE_MAGIC, HASH_LEN, PUBKEY_LEN, SIG_LEN,
};
use crate::spi_flash::SpiFlash;
use crate::otp::OtpStorage;

use sha2::{Sha256, Digest};
use p256::ecdsa::{VerifyingKey, Signature, signature::Verifier};
use p256::EncodedPoint;

// ----------------------------------------------------------------
// CRC32 (no_std compatible)
// ----------------------------------------------------------------
fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xEDB8_8320 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    crc ^ 0xFFFF_FFFF
}

// ----------------------------------------------------------------
// Constant-time byte comparison (prevent timing attacks)
// ----------------------------------------------------------------
fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff: u8 = 0;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

// ----------------------------------------------------------------
// Header parsing
// ----------------------------------------------------------------
fn parse_header(bytes: &[u8; FirmwareHeader::SIZE]) -> FirmwareHeader {
    // SAFETY: FirmwareHeader is repr(C, packed) and we've verified the
    // slice is exactly the right size. All bit patterns are valid for
    // the integer/array fields.
    unsafe { core::ptr::read_unaligned(bytes.as_ptr() as *const FirmwareHeader) }
}

// ----------------------------------------------------------------
// Main verifier
// ----------------------------------------------------------------
pub struct SecureBootVerifier<F: SpiFlash, O: OtpStorage> {
    flash: F,
    otp:   O,
}

impl<F: SpiFlash, O: OtpStorage> SecureBootVerifier<F, O> {
    pub fn new(flash: F, otp: O) -> Self {
        Self { flash, otp }
    }

    /// Verify firmware at `slot_offset`. Returns entry point address on success.
    pub fn verify(&self, slot_offset: u32) -> Result<u32, SecureBootError> {
        // ── Step 1: Read header ─────────────────────────────────
        let mut hdr_bytes = [0u8; FirmwareHeader::SIZE];
        self.flash.read(slot_offset, &mut hdr_bytes)?;
        let hdr = parse_header(&hdr_bytes);

        // ── Step 2: Check magic ─────────────────────────────────
        if hdr.magic != FIRMWARE_MAGIC {
            return Err(SecureBootError::BadMagic);
        }

        // ── Step 3: Validate header CRC ────────────────────────
        let crc_cover = &hdr_bytes[..FirmwareHeader::SIZE - 4];
        let computed_crc = crc32(crc_cover);
        if computed_crc != hdr.header_crc {
            return Err(SecureBootError::BadHeaderCrc);
        }

        // ── Step 4: Verify public key against OTP hash ─────────
        {
            let otp_hash = self.otp.read_pubkey_hash()?;
            let key_hash: [u8; 32] = Sha256::digest(&hdr.public_key).into();
            if !ct_eq(&key_hash, &otp_hash) {
                return Err(SecureBootError::KeyHashMismatch);
            }
        }

        // ── Step 5: Anti-rollback check ────────────────────────
        {
            let min_version = self.otp.read_min_version()?;
            if hdr.image_version < min_version {
                return Err(SecureBootError::RollbackViolation);
            }
        }

        // ── Step 6: Hash firmware body (streaming) ──────────────
        let body_offset = slot_offset + FirmwareHeader::SIZE as u32;
        let computed_hash = self.hash_body(body_offset, hdr.image_size)?;

        // ── Step 7: Compare hash to header's declared hash ──────
        if !ct_eq(&computed_hash, &hdr.image_hash) {
            return Err(SecureBootError::HashMismatch);
        }

        // ── Step 8: Verify ECDSA signature ──────────────────────
        self.verify_signature(&hdr.public_key, &computed_hash, &hdr.signature)?;

        // All checks passed — return entry point
        Ok(hdr.load_address + hdr.entry_point)
    }

    fn hash_body(&self, offset: u32, size: u32)
        -> Result<[u8; HASH_LEN], SecureBootError>
    {
        const CHUNK: usize = 512;
        let mut hasher = Sha256::new();
        let mut buf = [0u8; CHUNK];
        let mut remaining = size as usize;
        let mut pos = offset;

        while remaining > 0 {
            let to_read = remaining.min(CHUNK);
            self.flash.read(pos, &mut buf[..to_read])?;
            hasher.update(&buf[..to_read]);
            pos       += to_read as u32;
            remaining -= to_read;
        }

        Ok(hasher.finalize().into())
    }

    fn verify_signature(
        &self,
        pubkey_raw: &[u8; PUBKEY_LEN],
        hash:       &[u8; HASH_LEN],
        sig_raw:    &[u8; SIG_LEN],
    ) -> Result<(), SecureBootError> {
        // Build uncompressed point: 0x04 || X || Y
        let mut uncompressed = [0u8; 65];
        uncompressed[0] = 0x04;
        uncompressed[1..65].copy_from_slice(pubkey_raw);

        let encoded_point = EncodedPoint::from_bytes(&uncompressed)
            .map_err(|_| SecureBootError::InvalidKeyPoint)?;

        let verifying_key = VerifyingKey::from_encoded_point(&encoded_point)
            .map_err(|_| SecureBootError::InvalidKeyPoint)?;

        // Parse DER-less r||s signature
        let signature = Signature::from_slice(sig_raw)
            .map_err(|_| SecureBootError::InvalidSignature)?;

        // Verify — the p256 crate verifies against the raw digest bytes
        // using ecdsa::signature::hazmat::PrehashVerifier
        use p256::ecdsa::signature::hazmat::PrehashVerifier;
        verifying_key
            .verify_prehash(hash, &signature)
            .map_err(|_| SecureBootError::InvalidSignature)
    }
}
```

### Bootloader Entry Point (no_std)

```rust
// main.rs — Bare-metal bootloader entry point (Cortex-M example)

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod secure_boot;
mod verifier;
mod spi_flash;
mod otp;

use verifier::SecureBootVerifier;
use spi_flash::HalSpiFlash;   // Your platform HAL implementation
use otp::HalOtp;              // Your OTP HAL implementation
use secure_boot::{SLOT_A_OFFSET, SLOT_B_OFFSET};

#[cortex_m_rt::entry]
fn main() -> ! {
    // Initialize minimal hardware (clocks, SPI peripheral)
    let flash = HalSpiFlash::init();
    let otp   = HalOtp::init();

    let verifier = SecureBootVerifier::new(flash, otp);

    // Try Slot A first, then Slot B
    let entry_point = verifier.verify(SLOT_A_OFFSET)
        .or_else(|_| verifier.verify(SLOT_B_OFFSET))
        .unwrap_or_else(|err| {
            // Log error if UART is available, then halt
            // defmt::error!("Secure boot failed: {}", err);
            loop { cortex_m::asm::nop(); }
        });

    // Jump to verified firmware
    // SAFETY: entry_point has been cryptographically verified above
    unsafe { boot_to(entry_point) }
}

/// Transfer execution to the verified firmware image.
/// Sets up MSP and jumps to reset handler from the new vector table.
unsafe fn boot_to(load_address: u32) -> ! {
    let vtor = load_address as *const u32;
    let sp   = *vtor;
    let pc   = *vtor.add(1);

    core::arch::asm!(
        "msr msp, {sp}",
        "bx  {pc}",
        sp = in(reg) sp,
        pc = in(reg) pc,
        options(noreturn)
    );
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop { cortex_m::asm::nop(); }
}
```

### Signing Tool in Rust (Host-Side)

```rust
// tools/sign_firmware/src/main.rs — Host-side firmware signing tool

use std::{fs, path::Path};
use p256::{
    ecdsa::{SigningKey, Signature, signature::Signer},
    pkcs8::DecodePrivateKey,
};
use sha2::{Sha256, Digest};

const FIRMWARE_MAGIC: u32 = 0x5342_5446;
const FIRMWARE_HEADER_VER: u32 = 1;

fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &b in data {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xEDB8_8320 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    crc ^ 0xFFFF_FFFF
}

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 5 {
        eprintln!("Usage: {} <firmware.bin> <private_key.pem> \
                   <image_version> <output.bin>", args[0]);
        std::process::exit(1);
    }

    let firmware_bytes = fs::read(&args[1])?;
    let key_pem        = fs::read_to_string(&args[2])?;
    let image_version: u32 = args[3].parse()?;
    let out_path       = &args[4];

    // Load ECDSA P-256 private key from PKCS#8 PEM
    let signing_key = SigningKey::from_pkcs8_pem(&key_pem)?;
    let verifying_key = signing_key.verifying_key();

    // Extract raw public key X||Y (64 bytes, no 0x04 prefix)
    let encoded = verifying_key.to_encoded_point(false);
    let pubkey_raw: [u8; 64] = {
        let bytes = encoded.as_bytes(); // 65 bytes: 0x04 | X | Y
        bytes[1..65].try_into()?
    };

    // Hash the firmware body
    let image_hash: [u8; 32] = Sha256::digest(&firmware_bytes).into();

    // Sign the hash (ECDSA over prehash)
    use p256::ecdsa::signature::hazmat::PrehashSigner;
    let signature: Signature = signing_key.sign_prehash(&image_hash)?;
    let sig_bytes = signature.to_bytes();
    let sig_raw: [u8; 64] = sig_bytes.as_slice().try_into()?;

    // Build header (little-endian, matching repr(C, packed))
    let load_address: u32 = 0x0801_0000;
    let entry_point:  u32 = 0;
    let flags:        u32 = 0b11; // VALID | CONFIRMED

    let mut header = vec![0u8; 128]; // sizeof(FirmwareHeader)
    let mut w = header.as_mut_slice();

    // Write fields in order matching the C struct
    let fields: &[u32] = &[
        FIRMWARE_MAGIC,
        FIRMWARE_HEADER_VER,
        image_version,
        firmware_bytes.len() as u32,
        load_address,
        entry_point,
    ];
    let mut offset = 0;
    for &f in fields {
        header[offset..offset+4].copy_from_slice(&f.to_le_bytes());
        offset += 4;
    }
    header[offset..offset+32].copy_from_slice(&image_hash); offset += 32;
    header[offset..offset+64].copy_from_slice(&pubkey_raw); offset += 64;
    header[offset..offset+64].copy_from_slice(&sig_raw);    offset += 64;
    header[offset..offset+4].copy_from_slice(&flags.to_le_bytes()); offset += 4;

    // CRC covers everything except the last 4 bytes
    let crc = crc32(&header[..offset]);
    header[offset..offset+4].copy_from_slice(&crc.to_le_bytes());

    // Write signed image
    let mut output = header;
    output.extend_from_slice(&firmware_bytes);
    fs::write(out_path, &output)?;

    println!("✓ Signed firmware written to {out_path}");
    println!("  Version : {image_version}");
    println!("  Size    : {} bytes", firmware_bytes.len());

    Ok(())
}
```

---

## Key Provisioning and OTP/eFuse

The root of trust depends on securely programming the public key hash into **One-Time Programmable (OTP)** or **eFuse** memory. This is a one-way operation — once programmed, it cannot be changed.

### OTP HAL Example (C)

```c
/* otp_hal.h — Platform-specific OTP abstraction */

#ifndef OTP_HAL_H
#define OTP_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    OTP_OK = 0,
    OTP_ERR_LOCKED,   /* Field already programmed */
    OTP_ERR_IO,
    OTP_ERR_VERIFY,   /* Written value does not read back correctly */
} otp_err_t;

/**
 * Read the 32-byte trusted public key hash from OTP memory.
 * Returns OTP_ERR_IO if the field has not been programmed.
 */
otp_err_t otp_read_pubkey_hash(uint8_t *hash_out, size_t len);

/**
 * Program the public key hash into OTP (factory use only).
 * Will fail with OTP_ERR_LOCKED if already programmed.
 */
otp_err_t otp_program_pubkey_hash(const uint8_t *hash, size_t len);

/**
 * Lock the OTP region permanently (no further writes possible).
 * Call after otp_program_pubkey_hash() during manufacturing.
 */
otp_err_t otp_lock_pubkey_region(void);

#endif
```

### Manufacturing Flow

```
Factory Provisioning Flow:
─────────────────────────────────────────────────────────────
1. Generate ECDSA P-256 key pair offline on HSM:
   $ openssl ecparam -name prime256v1 -genkey -noout -out private.pem
   $ openssl ec -in private.pem -pubout -out public.pem

2. Compute SHA-256 hash of the raw public key:
   $ openssl ec -in public.pem -pubin -outform DER | \
     dd bs=1 skip=27 | sha256sum

3. Program the hash into device OTP (via JTAG / production fixture):
   otp_program_pubkey_hash(hash, 32);
   otp_lock_pubkey_region();

4. Store private key in HSM — never export it.

5. Sign firmware images with HSM before each release:
   sign_firmware firmware.bin private.pem <version> signed.bin
─────────────────────────────────────────────────────────────
```

---

## Anti-Rollback Protection

Anti-rollback prevents attackers from downgrading firmware to a known-vulnerable version.

### Monotonic Counter in SPI Flash

```c
/* rollback.c — Monotonic counter using NOR Flash bit-clearing */

#include "spi_flash_hal.h"
#include "secure_boot.h"

/*
 * NOR flash bits can only be cleared (1→0), never set (0→1) without erase.
 * We exploit this property: each version increment clears one more bit.
 * Version N = number of cleared bits in the counter word.
 * Max version = 32 per 4-byte word; use an array for more range.
 */

#define ROLLBACK_SLOTS 8   /* Supports versions 0..255 */

typedef struct {
    uint32_t counters[ROLLBACK_SLOTS];
    uint32_t crc;
} rollback_record_t;

uint32_t rollback_read_version(void)
{
    rollback_record_t rec;
    spi_flash_read(ROLLBACK_CTR_OFFSET, &rec, sizeof(rec));

    uint32_t version = 0;
    for (int i = 0; i < ROLLBACK_SLOTS; i++) {
        /* Count number of cleared bits (0s) = cleared = version increments */
        uint32_t val = rec.counters[i];
        while (~val & 0x80000000u) { version++; val <<= 1; }
        if (val == 0xFFFFFFFFu) break; /* This slot not yet used */
    }
    return version;
}

int rollback_update_version(uint32_t new_version)
{
    uint32_t current = rollback_read_version();
    if (new_version <= current) return 0; /* Nothing to do */
    if (new_version > ROLLBACK_SLOTS * 32) return -1; /* Out of range */

    rollback_record_t rec;
    spi_flash_read(ROLLBACK_CTR_OFFSET, &rec, sizeof(rec));

    /* Clear bits up to new_version */
    for (uint32_t v = current; v < new_version; v++) {
        uint32_t slot = v / 32;
        uint32_t bit  = 31 - (v % 32);
        rec.counters[slot] &= ~(1u << bit); /* Clear the bit */
    }

    /* Write back — no erase needed (only clearing bits) */
    return spi_flash_write(ROLLBACK_CTR_OFFSET, &rec,
                           sizeof(rec)) == SPI_FLASH_OK ? 0 : -1;
}
```

---

## Practical Considerations and Pitfalls

### 1. Timing Attacks on Comparisons
Always use constant-time comparison functions when comparing hashes, MACs, or signatures. Naive `memcmp` may short-circuit on the first mismatch, leaking timing information.

```c
/* ✗ WRONG — timing-variable */
if (memcmp(computed_hash, expected_hash, 32) != 0) ...

/* ✓ CORRECT — constant time */
uint8_t diff = 0;
for (int i = 0; i < 32; i++) diff |= computed_hash[i] ^ expected_hash[i];
if (diff != 0) ...
```

### 2. Fault Injection / Glitching
Attackers may use voltage glitching or EM fault injection to skip the verification check. Mitigations:
- Double-check critical results: verify the signature, then check again before jumping
- Use hardware security monitors (brown-out detectors, glitch detectors)
- Add redundant execution with XOR comparison of results

```c
/* Double-check pattern against fault injection */
secure_boot_err_t r1 = verify_ecdsa_signature(key, hash, sig);
secure_boot_err_t r2 = verify_ecdsa_signature(key, hash, sig);
if ((r1 ^ r2) != 0 || r1 != SECURE_BOOT_OK) {
    /* Possible fault injection — halt unconditionally */
    while (1) {}
}
```

### 3. Key Revocation
Since OTP cannot be changed, plan for key rotation by including a **key index** in the header. Store multiple key hashes in OTP (or derive from a hierarchy), and allow revocation of specific key indices.

### 4. Debug Interface Lockdown
Lock JTAG/SWD after production to prevent an attacker from reading out flash or bypassing the bootloader via the debug interface.

### 5. Secure Erase of Failed Slots
If signature verification fails repeatedly, consider triggering a secure erase of the slot and entering a known-good recovery mode rather than retrying indefinitely.

### 6. Encryption vs. Authentication
Note that signature verification provides **authentication and integrity**, but **not confidentiality**. If the firmware binary itself contains trade secrets, add AES-256-GCM encryption of the firmware body, with the decryption key derived from a device-unique key stored in the hardware key store (e.g., ARM TrustZone, STSAFE, ATECC608).

---

## Summary

**Secure Boot with SPI Flash** establishes an unbroken chain of trust from immutable hardware all the way to the application. The key concepts are:

**Chain of Trust** — Each boot stage cryptographically verifies the next before handing over control. The chain anchors to a public key hash burned permanently into OTP/eFuse memory, which cannot be modified after manufacturing.

**Digital Signatures** — ECDSA P-256 (or Ed25519) over SHA-256 hashes provides compact, efficient signing and verification well-suited to constrained embedded hardware. The private key never leaves an HSM; the public key is distributed with each firmware image, and its hash is the root of trust.

**Image Header** — Every firmware image carries a header containing the magic number, version, size, hash, public key, and signature. A header CRC provides quick structural validation before the more expensive cryptographic checks.

**A/B Slots** — Dual firmware slots in SPI Flash allow safe OTA updates. The new image is written to the inactive slot and only booted after verification; if boot confirmation is not received (watchdog timeout), the bootloader reverts to the previously confirmed slot.

**Anti-Rollback** — A monotonic counter (exploiting NOR Flash's one-way bit-clearing property) prevents attackers from downgrading to a known-vulnerable firmware version, even if they possess a valid old signature.

**Security Hardening** — Production devices must lock JTAG interfaces, use constant-time comparisons, add fault injection countermeasures, and consider firmware encryption for confidentiality.

Rust offers significant advantages for bootloader implementation — memory safety by construction, strong type guarantees, and `no_std` compatibility — while C/C++ with mbedTLS remains the most widely deployed approach across established embedded ecosystems. Both approaches, when implemented correctly, yield a robust and verifiable chain of trust from silicon to software.

---

*Document: 87 — Secure Boot with SPI Flash | SPI Programming Reference Series*