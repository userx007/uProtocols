The user wants a markdown document (not a Word doc), so I'll create a detailed `.md` file directly.Here's your detailed markdown document on **SPI Firmware Authentication**. Here's what it covers:

**Structure (12 sections):**

1. **Introduction** — why firmware authentication matters in embedded/IoT systems
2. **Background** — how SPI NOR flash is organized, typical memory map
3. **Threat Model** — what attacks it defends (and doesn't defend) against
4. **Cryptographic Primitives** — comparison tables for hash functions (SHA-256/512/BLAKE2b) and signature algorithms (RSA, ECDSA, Ed25519)
5. **Firmware Image Layout** — annotated binary structure stored in flash
6. **Authentication Workflow** — step-by-step flowchart from boot to execution
7. **C/C++ Implementation** — complete code including: firmware header struct, SPI HAL abstraction, SHA-256 streaming hash, Ed25519 verification, anti-rollback check, main auth entry point, dual-bank failover bootloader, and a host-side signing tool
8. **Rust Implementation** — full `no_std` equivalent using `sha2` + `ed25519-dalek`, with a `SpiFlash` trait, unit tests (including tamper detection), and a signing tool
9. **Hardware-Assisted Authentication** — STM32 HASH peripheral, NXP HAB/eFuse, TrustZone NSC entry points
10. **Anti-Rollback** — OTP monotonic counter and semantic version strategies
11. **Pitfalls & Security** — timing side-channels, fault injection countermeasures, key rotation, secure erase on failure
12. **Summary** — reference table of all components and their purpose

# 84. Firmware Authentication
## Verifying Firmware Signatures Stored in SPI Flash Before Execution

---

## Table of Contents

1. [Introduction](#introduction)
2. [Background: SPI Flash and Firmware Storage](#background)
3. [Threat Model](#threat-model)
4. [Authentication Concepts and Cryptographic Primitives](#concepts)
5. [Firmware Image Layout in SPI Flash](#image-layout)
6. [Authentication Workflow](#workflow)
7. [C/C++ Implementation](#c-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Hardware-Assisted Authentication](#hardware-assisted)
10. [Anti-Rollback and Version Protection](#anti-rollback)
11. [Common Pitfalls and Security Considerations](#pitfalls)
12. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

Firmware authentication is a critical security mechanism used in embedded systems, IoT devices,
automotive ECUs, industrial controllers, and any platform that stores executable firmware in
external or internal SPI (Serial Peripheral Interface) NOR/NAND flash memory. The goal is to
cryptographically verify the integrity and authenticity of firmware images before the processor
transfers execution to them.

Without firmware authentication, an attacker who gains physical or software access to the SPI bus
or the flash memory chip can replace or tamper with the firmware, leading to persistent, hard-to-
detect compromise of the entire system. Firmware authentication forms one of the foundational
pillars of a **Secure Boot** chain of trust.

---

## 2. Background: SPI Flash and Firmware Storage <a name="background"></a>

SPI flash devices (e.g., Winbond W25Q series, Micron MT25Q, ISSI IS25LP) are commonly used as
non-volatile storage for firmware in embedded systems. They connect to a host microcontroller or
SoC via 1-, 2-, or 4-bit SPI interfaces.

### Typical SPI Flash Memory Map

```
+---------------------------+  0x000000
|   Bootloader (Stage 1)    |  (immutable, often in internal ROM)
+---------------------------+  0x010000
|   Public Key / Certificate|
+---------------------------+  0x012000
|   Firmware Header         |
|   (version, size, hash)   |
+---------------------------+  0x013000
|   Application Firmware    |
|   (signed binary)         |
+---------------------------+  0x100000
|   Firmware Signature      |
|   (ECDSA/RSA/Ed25519)     |
+---------------------------+  0x110000
|   Backup Firmware Slot    |
+---------------------------+
|   Configuration / NVS     |
+---------------------------+  End of flash
```

The bootloader reads the firmware image from SPI flash, verifies the cryptographic signature
against a trusted public key (stored in OTP, internal flash, or hardware), and only jumps to the
application if verification succeeds.

---

## 3. Threat Model <a name="threat-model"></a>

Firmware authentication is designed to defend against:

- **Physical SPI bus interception**: An attacker sniffing or injecting data on the SPI lines
  during a read operation.
- **Flash chip replacement**: Physically swapping the SPI flash chip with a malicious one.
- **In-system flash reprogramming**: An attacker using the SPI interface to overwrite firmware
  (e.g., via JTAG, debug ports, or a software vulnerability).
- **Supply chain attacks**: Compromised firmware injected before device deployment.
- **Downgrade attacks**: Rolling back to an older, vulnerable firmware version.

Firmware authentication does **not** protect against:
- Runtime attacks on already-executing firmware.
- Physical hardware implants on the PCB after the flash chip.
- Compromise of the signing key itself.

---

## 4. Authentication Concepts and Cryptographic Primitives <a name="concepts"></a>

### 4.1 Hash Functions

Before signing, the firmware binary is hashed with a cryptographic hash function:

| Algorithm | Output Size | Use Case                         |
|-----------|-------------|----------------------------------|
| SHA-256   | 32 bytes    | Most common, excellent security  |
| SHA-384   | 48 bytes    | Higher security margin           |
| SHA-512   | 64 bytes    | Maximum hash strength            |
| BLAKE2b   | Configurable| Faster than SHA on some MCUs     |

### 4.2 Signature Algorithms

| Algorithm  | Key Size      | Signature Size | Notes                               |
|------------|---------------|----------------|-------------------------------------|
| RSA-2048   | 2048-bit      | 256 bytes      | Legacy, widely supported            |
| RSA-3072   | 3072-bit      | 384 bytes      | More secure, heavier computation    |
| ECDSA P-256| 256-bit       | 64 bytes       | Compact, good performance           |
| Ed25519    | 256-bit       | 64 bytes       | Fast, deterministic, recommended    |

### 4.3 Chain of Trust

```
Silicon ROM (immutable)
    │  verifies
    ▼
Stage 1 Bootloader (internal flash / OTP)
    │  contains trusted Root Public Key
    │  verifies
    ▼
Stage 2 Bootloader or Application (SPI flash)
    │  verified against Root Public Key
    │  optionally verifies
    ▼
Application / RTOS / OS
```

The root of trust must be anchored in hardware — typically in one-time programmable (OTP) memory,
a hardware security module (HSM), or ROM that cannot be altered after manufacturing.

---

## 5. Firmware Image Layout in SPI Flash <a name="image-layout"></a>

A signed firmware image typically consists of a structured binary with three main sections:

```
+--------------------------------+
|  MAGIC NUMBER  (4 bytes)       |  e.g., 0xDEADC0DE
+--------------------------------+
|  VERSION       (4 bytes)       |  Semantic version (major.minor.patch)
+--------------------------------+
|  IMAGE SIZE    (4 bytes)       |  Size of firmware payload in bytes
+--------------------------------+
|  HASH ALGO     (2 bytes)       |  e.g., 0x0001 = SHA-256
+--------------------------------+
|  SIG ALGO      (2 bytes)       |  e.g., 0x0002 = Ed25519
+--------------------------------+
|  RESERVED      (16 bytes)      |  Future use / padding
+--------------------------------+
|  SHA-256 HASH  (32 bytes)      |  Hash of firmware payload
+--------------------------------+
|  SIGNATURE     (64 bytes)      |  Signature over hash (Ed25519)
+--------------------------------+
|  FIRMWARE PAYLOAD (variable)   |  Actual executable code + data
+--------------------------------+
```

The signature typically covers either:
- The hash of the firmware payload alone, or
- The entire header (excluding the signature field) concatenated with the payload hash.

---

## 6. Authentication Workflow <a name="workflow"></a>

```
Boot Start
    │
    ▼
Read Firmware Header from SPI Flash
    │
    ▼
Validate Magic Number & Sanity Checks
    │
    ├── FAIL ──► Halt or load backup firmware
    │
    ▼
Read Firmware Payload from SPI Flash
(stream through hash engine or buffer)
    │
    ▼
Compute SHA-256 Hash of Payload
    │
    ▼
Compare Computed Hash with Header Hash
    │
    ├── MISMATCH ──► Integrity failure → Halt
    │
    ▼
Load Trusted Public Key (from OTP / internal ROM)
    │
    ▼
Verify Signature (Ed25519 / ECDSA) over Hash
    │
    ├── INVALID ──► Authentication failure → Halt
    │
    ▼
Check Anti-Rollback Version Counter
    │
    ├── FAIL ──► Downgrade attempt → Halt
    │
    ▼
Mark Firmware as Trusted
    │
    ▼
Transfer Execution to Firmware Entry Point
```

---

## 7. C/C++ Implementation <a name="c-implementation"></a>

### 7.1 Firmware Header Structure

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define FIRMWARE_MAGIC        0xDEADC0DEU
#define FIRMWARE_HASH_SIZE    32   /* SHA-256 */
#define FIRMWARE_SIG_SIZE     64   /* Ed25519 */
#define PUBLIC_KEY_SIZE       32   /* Ed25519 public key */
#define MAX_FIRMWARE_SIZE     (512 * 1024)  /* 512 KB */

/* Firmware header stored at the beginning of the image in SPI flash */
typedef struct __attribute__((packed)) {
    uint32_t magic;                        /* FIRMWARE_MAGIC */
    uint32_t version;                      /* Firmware version */
    uint32_t image_size;                   /* Payload size in bytes */
    uint16_t hash_algo;                    /* 0x0001 = SHA-256 */
    uint16_t sig_algo;                     /* 0x0002 = Ed25519 */
    uint8_t  reserved[16];                 /* Reserved / padding */
    uint8_t  payload_hash[FIRMWARE_HASH_SIZE];  /* SHA-256 of payload */
    uint8_t  signature[FIRMWARE_SIG_SIZE]; /* Ed25519 over payload_hash */
} firmware_header_t;

/* Verification result codes */
typedef enum {
    FW_AUTH_OK              = 0,
    FW_AUTH_BAD_MAGIC       = -1,
    FW_AUTH_BAD_SIZE        = -2,
    FW_AUTH_HASH_MISMATCH   = -3,
    FW_AUTH_SIG_INVALID     = -4,
    FW_AUTH_VERSION_INVALID = -5,
    FW_AUTH_READ_ERROR      = -6,
} fw_auth_result_t;
```

### 7.2 SPI Flash Read Abstraction

```c
/* Platform abstraction layer for SPI flash access */
typedef struct {
    /* Read `len` bytes from SPI flash at `offset` into `buf`.
     * Returns 0 on success, negative on error. */
    int (*read)(uint32_t offset, uint8_t *buf, uint32_t len, void *ctx);
    void *ctx;
} spi_flash_dev_t;

/* Example HAL implementation for a generic SPI NOR flash */
int spi_nor_read(uint32_t offset, uint8_t *buf, uint32_t len, void *ctx)
{
    /* In real hardware: assert CS, send READ command (0x03),
     * send 3-byte address, clock out `len` bytes, deassert CS.
     * Here we simulate with a buffer for illustration. */
    const uint8_t *flash_base = (const uint8_t *)ctx;
    memcpy(buf, flash_base + offset, len);
    return 0;
}
```

### 7.3 SHA-256 Hash Verification

```c
#include "sha256.h"  /* e.g., from mbedTLS or a bare-metal implementation */

/**
 * Stream-hash the firmware payload from SPI flash and compare
 * against the hash stored in the firmware header.
 *
 * @param dev        SPI flash device handle
 * @param hdr        Parsed firmware header
 * @param fw_offset  Byte offset of the firmware payload in flash
 * @return           true if hash matches, false otherwise
 */
bool verify_payload_hash(const spi_flash_dev_t *dev,
                         const firmware_header_t *hdr,
                         uint32_t fw_offset)
{
    sha256_context ctx;
    uint8_t computed_hash[FIRMWARE_HASH_SIZE];
    uint8_t chunk[256];
    uint32_t remaining = hdr->image_size;
    uint32_t offset    = fw_offset;

    sha256_init(&ctx);

    /* Stream firmware through hash engine in chunks to minimise RAM usage */
    while (remaining > 0) {
        uint32_t chunk_size = (remaining < sizeof(chunk))
                              ? remaining : sizeof(chunk);

        if (dev->read(offset, chunk, chunk_size, dev->ctx) != 0) {
            return false;
        }

        sha256_update(&ctx, chunk, chunk_size);
        offset    += chunk_size;
        remaining -= chunk_size;
    }

    sha256_final(&ctx, computed_hash);

    /* Constant-time comparison to prevent timing side-channels */
    uint8_t diff = 0;
    for (int i = 0; i < FIRMWARE_HASH_SIZE; i++) {
        diff |= computed_hash[i] ^ hdr->payload_hash[i];
    }
    return (diff == 0);
}
```

### 7.4 Ed25519 Signature Verification

```c
#include "ed25519.h"  /* e.g., SUPERCOP/ref10 or mbedTLS */

/* Root public key — burned into OTP or stored in internal ROM.
 * In production this would be loaded from a hardware register or
 * secure key storage, not a compile-time constant. */
static const uint8_t ROOT_PUBLIC_KEY[PUBLIC_KEY_SIZE] = {
    0xAB, 0xCD, 0xEF, 0x01, /* ... 28 more bytes ... */
};

/**
 * Verify the Ed25519 signature in the firmware header.
 * The signature is over payload_hash (32 bytes).
 *
 * @param hdr  Firmware header containing signature and payload_hash
 * @return     true if signature is valid, false otherwise
 */
bool verify_signature(const firmware_header_t *hdr)
{
    /*
     * ed25519_verify(signature, message, message_len, public_key)
     * Returns 1 on success, 0 on failure (SUPERCOP convention).
     */
    int result = ed25519_verify(
        hdr->signature,          /* 64-byte signature */
        hdr->payload_hash,       /* message = SHA-256 of firmware */
        FIRMWARE_HASH_SIZE,      /* 32 bytes */
        ROOT_PUBLIC_KEY          /* trusted root public key */
    );
    return (result == 1);
}
```

### 7.5 Anti-Rollback Check

```c
/* Minimum acceptable firmware version — stored in OTP/eFuse.
 * Once updated, it can never be decremented. */
static uint32_t read_min_version_from_otp(void)
{
    /* Platform-specific: read from eFuse / OTP register */
    return OTP_REGISTER_MIN_VERSION;
}

bool check_anti_rollback(uint32_t firmware_version)
{
    uint32_t min_version = read_min_version_from_otp();
    return (firmware_version >= min_version);
}
```

### 7.6 Main Authentication Entry Point

```c
/**
 * Full firmware authentication procedure.
 *
 * @param dev        SPI flash device handle
 * @param hdr_offset Byte offset of the firmware header in SPI flash
 * @return           FW_AUTH_OK on success, error code otherwise
 */
fw_auth_result_t authenticate_firmware(const spi_flash_dev_t *dev,
                                       uint32_t hdr_offset)
{
    firmware_header_t hdr;
    uint32_t payload_offset;

    /* Step 1: Read and validate the header */
    if (dev->read(hdr_offset, (uint8_t *)&hdr, sizeof(hdr), dev->ctx) != 0) {
        return FW_AUTH_READ_ERROR;
    }

    if (hdr.magic != FIRMWARE_MAGIC) {
        return FW_AUTH_BAD_MAGIC;
    }

    if (hdr.image_size == 0 || hdr.image_size > MAX_FIRMWARE_SIZE) {
        return FW_AUTH_BAD_SIZE;
    }

    /* Step 2: Anti-rollback version check */
    if (!check_anti_rollback(hdr.version)) {
        return FW_AUTH_VERSION_INVALID;
    }

    /* Step 3: Verify signature first (cheaper before full hash pass) */
    if (!verify_signature(&hdr)) {
        return FW_AUTH_SIG_INVALID;
    }

    /* Step 4: Verify payload hash (stream entire firmware image) */
    payload_offset = hdr_offset + sizeof(firmware_header_t);
    if (!verify_payload_hash(dev, &hdr, payload_offset)) {
        return FW_AUTH_HASH_MISMATCH;
    }

    return FW_AUTH_OK;
}
```

### 7.7 Bootloader Main with Dual-Bank Failover

```c
#define SLOT_A_OFFSET  0x010000U
#define SLOT_B_OFFSET  0x110000U
#define ENTRY_POINT_A  0x20010000U  /* RAM load address for slot A */

typedef void (*fw_entry_t)(void);

void bootloader_main(void)
{
    spi_flash_dev_t flash = {
        .read = spi_nor_read,
        .ctx  = (void *)FLASH_BASE_ADDR
    };

    fw_auth_result_t result = authenticate_firmware(&flash, SLOT_A_OFFSET);

    if (result == FW_AUTH_OK) {
        /* Copy verified firmware to RAM and jump */
        copy_firmware_to_ram(&flash, SLOT_A_OFFSET, ENTRY_POINT_A);
        fw_entry_t entry = (fw_entry_t)ENTRY_POINT_A;
        entry();
    } else {
        /* Try backup slot */
        result = authenticate_firmware(&flash, SLOT_B_OFFSET);
        if (result == FW_AUTH_OK) {
            /* Boot from backup slot */
        } else {
            /* All slots failed — enter recovery or halt */
            system_halt();
        }
    }

    /* Should never reach here */
    __builtin_unreachable();
}
```

### 7.8 Firmware Signing Tool (Host Side, C++)

```cpp
#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <stdexcept>
#include "sha256.h"
#include "ed25519.h"

struct FirmwareHeader {
    uint32_t magic        = 0xDEADC0DE;
    uint32_t version      = 0;
    uint32_t image_size   = 0;
    uint16_t hash_algo    = 0x0001;  // SHA-256
    uint16_t sig_algo     = 0x0002;  // Ed25519
    uint8_t  reserved[16] = {};
    uint8_t  payload_hash[32] = {};
    uint8_t  signature[64]    = {};
} __attribute__((packed));

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void sign_firmware(const std::string& input_path,
                   const std::string& output_path,
                   const uint8_t private_key[64],
                   uint32_t version)
{
    auto payload = read_file(input_path);

    FirmwareHeader hdr;
    hdr.version    = version;
    hdr.image_size = static_cast<uint32_t>(payload.size());

    // Compute SHA-256 of payload
    sha256(payload.data(), payload.size(), hdr.payload_hash);

    // Sign the hash with Ed25519
    ed25519_sign(hdr.signature, hdr.payload_hash, 32, private_key);

    // Write signed image: header + payload
    std::ofstream out(output_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(payload.data()), payload.size());

    printf("Signed firmware written to %s\n", output_path.c_str());
    printf("  Version:    0x%08X\n", version);
    printf("  Size:       %u bytes\n", hdr.image_size);
}
```

---

## 8. Rust Implementation <a name="rust-implementation"></a>

Rust is increasingly used for embedded firmware and bootloaders due to its memory safety guarantees.
The following uses the `sha2` and `ed25519-dalek` crates, which are `no_std` compatible.

### 8.1 Cargo.toml Dependencies

```toml
[dependencies]
sha2           = { version = "0.10", default-features = false }
ed25519-dalek  = { version = "2.0", default-features = false, features = ["alloc"] }
digest         = { version = "0.10", default-features = false }
zerocopy       = "0.7"

[profile.release]
opt-level = "s"    # Optimize for size on embedded targets
lto        = true
panic      = "abort"
```

### 8.2 Firmware Header and Error Types

```rust
#![no_std]
#![no_main]

use core::mem::size_of;

/// Magic number identifying a valid firmware image.
const FIRMWARE_MAGIC: u32 = 0xDEAD_C0DE;
const FIRMWARE_HASH_SIZE: usize = 32;
const FIRMWARE_SIG_SIZE: usize = 64;
const PUBLIC_KEY_SIZE: usize = 32;
const MAX_FIRMWARE_SIZE: u32 = 512 * 1024; // 512 KB

/// Firmware header as stored in SPI flash.
/// Must be #[repr(C, packed)] for direct memory-mapped access.
#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct FirmwareHeader {
    pub magic:        u32,
    pub version:      u32,
    pub image_size:   u32,
    pub hash_algo:    u16,
    pub sig_algo:     u16,
    pub reserved:     [u8; 16],
    pub payload_hash: [u8; FIRMWARE_HASH_SIZE],
    pub signature:    [u8; FIRMWARE_SIG_SIZE],
}

/// Errors that can occur during firmware authentication.
#[derive(Debug, PartialEq)]
pub enum AuthError {
    ReadError,
    BadMagic,
    BadSize,
    HashMismatch,
    SignatureInvalid,
    VersionTooOld,
}
```

### 8.3 SPI Flash Trait Abstraction

```rust
/// Trait abstracting over SPI flash reads.
/// Enables testing with in-memory buffers and production with real SPI HAL.
pub trait SpiFlash {
    type Error;
    fn read(&self, offset: u32, buf: &mut [u8]) -> Result<(), Self::Error>;
}

/// In-memory mock for testing.
pub struct MemFlash<'a>(&'a [u8]);

impl<'a> SpiFlash for MemFlash<'a> {
    type Error = ();
    fn read(&self, offset: u32, buf: &mut [u8]) -> Result<(), ()> {
        let start = offset as usize;
        let end   = start + buf.len();
        if end > self.0.len() { return Err(()); }
        buf.copy_from_slice(&self.0[start..end]);
        Ok(())
    }
}
```

### 8.4 SHA-256 Hash Verification

```rust
use sha2::{Sha256, Digest};

/// Stream the firmware payload from SPI flash, compute its SHA-256 hash,
/// and compare it against the hash in the firmware header.
pub fn verify_payload_hash<F: SpiFlash>(
    flash:          &F,
    header:         &FirmwareHeader,
    payload_offset: u32,
) -> Result<(), AuthError>
where
    F::Error: core::fmt::Debug,
{
    let mut hasher    = Sha256::new();
    let mut remaining = header.image_size;
    let mut offset    = payload_offset;
    let mut chunk     = [0u8; 256];

    while remaining > 0 {
        let chunk_size = remaining.min(chunk.len() as u32) as usize;
        let buf        = &mut chunk[..chunk_size];

        flash.read(offset, buf).map_err(|_| AuthError::ReadError)?;
        hasher.update(&*buf);

        offset    += chunk_size as u32;
        remaining -= chunk_size as u32;
    }

    let computed: [u8; 32] = hasher.finalize().into();

    // Constant-time comparison
    let diff = computed.iter()
        .zip(header.payload_hash.iter())
        .fold(0u8, |acc, (a, b)| acc | (a ^ b));

    if diff != 0 {
        Err(AuthError::HashMismatch)
    } else {
        Ok(())
    }
}
```

### 8.5 Ed25519 Signature Verification

```rust
use ed25519_dalek::{Signature, VerifyingKey, Verifier};

/// Root public key — in production, loaded from OTP / hardware secure storage.
const ROOT_PUBLIC_KEY_BYTES: [u8; PUBLIC_KEY_SIZE] = [
    0xAB, 0xCD, 0xEF, 0x01, /* ... 28 more bytes ... */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
];

/// Verify the Ed25519 signature stored in the firmware header.
/// The signature authenticates `payload_hash` (32 bytes).
pub fn verify_signature(header: &FirmwareHeader) -> Result<(), AuthError> {
    let verifying_key = VerifyingKey::from_bytes(&ROOT_PUBLIC_KEY_BYTES)
        .map_err(|_| AuthError::SignatureInvalid)?;

    let signature = Signature::from_bytes(&header.signature);

    verifying_key
        .verify(&header.payload_hash, &signature)
        .map_err(|_| AuthError::SignatureInvalid)
}
```

### 8.6 Anti-Rollback Check

```rust
/// Read the minimum allowed firmware version from OTP registers.
/// This is a platform-specific operation; shown here as an extern.
extern "C" {
    fn otp_read_min_version() -> u32;
}

pub fn check_anti_rollback(firmware_version: u32) -> Result<(), AuthError> {
    let min_version = unsafe { otp_read_min_version() };
    if firmware_version >= min_version {
        Ok(())
    } else {
        Err(AuthError::VersionTooOld)
    }
}
```

### 8.7 Main Authentication Function

```rust
/// Authenticate a firmware image stored in SPI flash.
///
/// # Arguments
/// - `flash`      — SPI flash device implementing the `SpiFlash` trait
/// - `hdr_offset` — Byte offset of the `FirmwareHeader` in flash
///
/// # Returns
/// `Ok(())` if the firmware is authentic, `Err(AuthError)` otherwise.
pub fn authenticate_firmware<F>(
    flash:      &F,
    hdr_offset: u32,
) -> Result<(), AuthError>
where
    F: SpiFlash,
    F::Error: core::fmt::Debug,
{
    // Step 1: Read header
    let mut raw = [0u8; size_of::<FirmwareHeader>()];
    flash.read(hdr_offset, &mut raw).map_err(|_| AuthError::ReadError)?;

    // SAFETY: FirmwareHeader is repr(C, packed) with no padding requirements
    let header: FirmwareHeader = unsafe {
        core::ptr::read_unaligned(raw.as_ptr() as *const FirmwareHeader)
    };

    // Step 2: Magic check
    if header.magic != FIRMWARE_MAGIC {
        return Err(AuthError::BadMagic);
    }

    // Step 3: Size sanity check
    if header.image_size == 0 || header.image_size > MAX_FIRMWARE_SIZE {
        return Err(AuthError::BadSize);
    }

    // Step 4: Anti-rollback version check
    check_anti_rollback(header.version)?;

    // Step 5: Signature verification (validates hash field authenticity)
    verify_signature(&header)?;

    // Step 6: Payload hash verification (validates actual firmware bytes)
    let payload_offset = hdr_offset + size_of::<FirmwareHeader>() as u32;
    verify_payload_hash(flash, &header, payload_offset)?;

    Ok(())
}
```

### 8.8 Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use sha2::{Sha256, Digest};
    use ed25519_dalek::{SigningKey, Signer};

    fn make_signed_image(payload: &[u8], version: u32) -> Vec<u8> {
        // Generate a test signing key (in production, load from secure storage)
        let signing_key = SigningKey::from_bytes(&[0x42u8; 32]);
        let verifying_key = signing_key.verifying_key();

        let hash: [u8; 32] = Sha256::digest(payload).into();
        let sig = signing_key.sign(&hash);

        let header = FirmwareHeader {
            magic:        FIRMWARE_MAGIC,
            version,
            image_size:   payload.len() as u32,
            hash_algo:    0x0001,
            sig_algo:     0x0002,
            reserved:     [0u8; 16],
            payload_hash: hash,
            signature:    sig.to_bytes(),
        };

        let hdr_bytes = unsafe {
            core::slice::from_raw_parts(
                &header as *const _ as *const u8,
                size_of::<FirmwareHeader>(),
            )
        };

        let mut image = Vec::new();
        image.extend_from_slice(hdr_bytes);
        image.extend_from_slice(payload);
        image
    }

    #[test]
    fn test_valid_firmware_authenticates() {
        let payload = b"Hello, Secure Boot!";
        let image   = make_signed_image(payload, 1);
        let flash   = MemFlash(&image);

        // Should succeed with correct image
        assert!(authenticate_firmware(&flash, 0).is_ok());
    }

    #[test]
    fn test_tampered_payload_detected() {
        let payload  = b"Original firmware payload data";
        let mut image = make_signed_image(payload, 1);

        // Tamper with a byte in the payload section
        let hdr_size = size_of::<FirmwareHeader>();
        image[hdr_size + 5] ^= 0xFF;

        let flash = MemFlash(&image);
        assert_eq!(
            authenticate_firmware(&flash, 0),
            Err(AuthError::HashMismatch)
        );
    }

    #[test]
    fn test_bad_magic_rejected() {
        let payload  = b"Firmware data";
        let mut image = make_signed_image(payload, 1);

        // Corrupt the magic number
        image[0] = 0xDE;
        image[1] = 0xAD;
        image[2] = 0xBE;
        image[3] = 0xEF;

        let flash = MemFlash(&image);
        assert_eq!(authenticate_firmware(&flash, 0), Err(AuthError::BadMagic));
    }
}
```

### 8.9 Host-Side Firmware Signing Tool (Rust)

```rust
//! Host-side firmware signing tool.
//! Usage: sign_firmware <input.bin> <output.bin> <version>

use std::fs;
use std::env;
use sha2::{Sha256, Digest};
use ed25519_dalek::{SigningKey, Signer};

#[repr(C, packed)]
struct FirmwareHeader {
    magic:        u32,
    version:      u32,
    image_size:   u32,
    hash_algo:    u16,
    sig_algo:     u16,
    reserved:     [u8; 16],
    payload_hash: [u8; 32],
    signature:    [u8; 64],
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let (input, output, version) = (&args[1], &args[2], args[3].parse::<u32>().unwrap());

    // Load private signing key (from file / HSM in production)
    let key_bytes: [u8; 32] = load_signing_key("signing_key.bin");
    let signing_key = SigningKey::from_bytes(&key_bytes);

    let payload = fs::read(input).expect("Failed to read firmware");
    let hash: [u8; 32] = Sha256::digest(&payload).into();
    let sig  = signing_key.sign(&hash);

    let header = FirmwareHeader {
        magic:        0xDEAD_C0DE,
        version,
        image_size:   payload.len() as u32,
        hash_algo:    0x0001,
        sig_algo:     0x0002,
        reserved:     [0u8; 16],
        payload_hash: hash,
        signature:    sig.to_bytes(),
    };

    let hdr_bytes = unsafe {
        std::slice::from_raw_parts(
            &header as *const _ as *const u8,
            std::mem::size_of::<FirmwareHeader>(),
        )
    };

    let mut out = Vec::new();
    out.extend_from_slice(hdr_bytes);
    out.extend_from_slice(&payload);
    fs::write(output, &out).expect("Failed to write signed image");

    println!("Signed firmware written to {}", output);
    println!("  Version: {:#010X}", version);
    println!("  Size:    {} bytes", payload.len());
}

fn load_signing_key(path: &str) -> [u8; 32] {
    let bytes = fs::read(path).expect("Cannot read signing key");
    bytes.try_into().expect("Key must be 32 bytes")
}
```

---

## 9. Hardware-Assisted Authentication <a name="hardware-assisted"></a>

Modern SoCs provide hardware accelerators and security peripherals that make firmware
authentication both faster and more resistant to software attacks.

### 9.1 Hardware SHA/Crypto Engines

Many ARM Cortex-M and RISC-V MCUs include dedicated crypto coprocessors:

```c
/* Example: Using STM32 hardware hash (HASH peripheral) for SHA-256 */
#include "stm32h7xx_hal.h"

void hw_sha256_firmware(const uint8_t *data, uint32_t len, uint8_t *out)
{
    HASH_HandleTypeDef hhash = {0};
    hhash.Init.DataType = HASH_DATATYPE_8B;
    hhash.Init.Algorithm = HASH_ALGOSELECTION_SHA256;

    HAL_HASH_Init(&hhash);
    HAL_HASH_SHA256_Start(&hhash, (uint8_t *)data, len, out, HAL_MAX_DELAY);
}
```

### 9.2 Secure Boot with eFuse/OTP Root Key

```c
/* Example: NXP i.MX RT1060 HAB (High Assurance Boot) integration */
/* The HAB ROM validates the IVT (Image Vector Table) and firmware 
 * against the SRK (Super Root Key) fused in the device's OTP array. */

typedef struct {
    uint32_t header;        /* Tag = 0xD1, length, version */
    uint32_t entry;         /* Absolute address of first instruction */
    uint32_t reserved1;
    uint32_t dcd_ptr;       /* Pointer to Device Configuration Data */
    uint32_t boot_data_ptr; /* Pointer to boot data structure */
    uint32_t self_ptr;      /* Pointer to this IVT (for relocation check) */
    uint32_t csf_ptr;       /* Pointer to Command Sequence File (CSF) */
    uint32_t reserved2;
} image_vector_table_t;

/* The CSF contains:
 * - Install SRK Table command
 * - Install CSFK command
 * - Authenticate CSF command
 * - Install Key command (for image signing key)
 * - Authenticate Data command (covers firmware image ranges)
 */
```

### 9.3 TrustZone / Secure Enclave Integration

On ARMv8-M devices with TrustZone, the authentication itself can execute in the Secure World:

```c
/* Non-Secure Callable (NSC) function exposed to Normal World bootloader */
__attribute__((cmse_nonsecure_entry))
int32_t NSC_AuthenticateFirmware(uint32_t fw_offset, uint32_t fw_size)
{
    /* This executes in Secure World with access to:
     * - Secure ROM containing root keys
     * - Secure hardware crypto engine
     * - OTP / eFuse controller
     * Normal World only sees the return value (pass/fail).
     */
    return (int32_t)authenticate_firmware_secure(fw_offset, fw_size);
}
```

---

## 10. Anti-Rollback and Version Protection <a name="anti-rollback"></a>

Anti-rollback prevents an attacker from downgrading to an older, vulnerable firmware version.

### Strategy 1: Monotonic OTP Counter

Each OTP bit represents one firmware generation. Once a bit is blown, it cannot be restored:

```c
/* Example: 32-bit eFuse counter, allowing up to 32 increment steps */
uint32_t read_rollback_counter(void)
{
    uint32_t fuse = read_otp_word(OTP_ROLLBACK_FUSE_ADDR);
    /* Count leading zeros (unblown bits) to get the counter value */
    return 32 - __builtin_clz(fuse | 1);
}

void increment_rollback_counter(void)
{
    uint32_t counter = read_rollback_counter();
    if (counter < 32) {
        /* Blow the next fuse bit */
        uint32_t new_fuse = (1U << counter);
        write_otp_word(OTP_ROLLBACK_FUSE_ADDR, new_fuse);
    }
}
```

### Strategy 2: Semantic Version in Firmware Header (Rust)

```rust
/// Packed semantic version: major (8 bits), minor (12 bits), patch (12 bits)
pub fn version_major(v: u32) -> u32 { (v >> 24) & 0xFF }
pub fn version_minor(v: u32) -> u32 { (v >> 12) & 0xFFF }
pub fn version_patch(v: u32) -> u32 { v & 0xFFF }

/// Only major version increments are considered breaking for rollback purposes.
pub fn anti_rollback_ok(firmware_version: u32, min_version: u32) -> bool {
    version_major(firmware_version) > version_major(min_version)
    || (version_major(firmware_version) == version_major(min_version)
        && firmware_version >= min_version)
}
```

---

## 11. Common Pitfalls and Security Considerations <a name="pitfalls"></a>

### 11.1 Timing Side-Channel Attacks

Never use `memcmp()` for comparing hashes or MACs:

```c
/* WRONG — branches on first mismatch, leaks timing information */
if (memcmp(computed_hash, stored_hash, 32) != 0) { /* ... */ }

/* CORRECT — constant-time comparison */
uint8_t diff = 0;
for (int i = 0; i < 32; i++) {
    diff |= computed_hash[i] ^ stored_hash[i];
}
if (diff != 0) { /* ... */ }
```

### 11.2 Fault Injection (Glitching)

Hardware fault injection attacks can skip conditional branches. Mitigate with:

```c
/* Double-check critical branches and use redundant variables */
volatile int auth_result_1 = verify_signature(&hdr);
volatile int auth_result_2 = verify_signature(&hdr);  /* Repeat */

if ((auth_result_1 != 0) || (auth_result_2 != 0)) {
    secure_halt();  /* Fault injection may skip one check, not both */
}
```

### 11.3 Verify Signature Before Hash

Compute and verify the signature **before** streaming the entire firmware to compute the hash.
This prevents wasting CPU cycles on potentially large untrusted data:

> **Recommended order**: Header sanity → Anti-rollback → Signature verify → Payload hash

### 11.4 Key Management

- Never store private signing keys on the target device.
- Use HSMs or dedicated signing servers for CI/CD pipelines.
- Rotate keys periodically and update the stored root key via a secure update mechanism.
- Implement key revocation by maintaining a list of revoked key fingerprints in OTP.

### 11.5 Secure Erase on Authentication Failure

On persistent authentication failure, consider erasing sensitive data rather than
leaving the device in an indeterminate state:

```c
void handle_auth_failure(fw_auth_result_t err)
{
    log_security_event(err);
    
    if (failure_count > MAX_FAILURES) {
        /* Wipe sensitive keys from RAM */
        secure_memzero(session_keys, sizeof(session_keys));
        /* Enter recovery mode or brick the device */
        enter_lockdown_mode();
    }
}
```

---

## 12. Summary <a name="summary"></a>

Firmware authentication for SPI flash is a multi-layer security process that forms the
foundation of a Secure Boot chain of trust. The key components are:

| Component                  | Purpose                                              |
|----------------------------|------------------------------------------------------|
| Structured firmware header | Contains metadata, hash, and signature in flash      |
| Cryptographic hash (SHA-256)| Detects accidental or intentional corruption         |
| Digital signature (Ed25519) | Proves the firmware was produced by a trusted party  |
| Trusted root public key     | Anchored in OTP / hardware — the root of trust       |
| Anti-rollback counter       | Prevents downgrade to vulnerable older versions      |
| Hardware crypto engines     | Accelerate and harden the verification process       |
| Constant-time comparisons   | Prevent timing side-channel leakage                  |
| Fault injection countermeasures | Redundant checks to survive hardware glitching  |

**In C/C++**, firmware authentication is typically implemented directly in the bootloader with
minimal dependencies, using a trusted library such as mbedTLS, wolfCrypt, or a lightweight
bare-metal Ed25519 implementation. The focus is on minimal code size, deterministic execution
time, and resistance to hardware attacks.

**In Rust**, the type system and ownership model provide additional safety guarantees — eliminating
entire classes of memory safety bugs that are common sources of authentication bypass
vulnerabilities. The `no_std` ecosystem (sha2, ed25519-dalek) is mature enough for embedded use,
and traits like `SpiFlash` enable clean abstraction and testability.

In both languages, the critical non-negotiables are: anchoring the root of trust in hardware,
using a properly authenticated signature scheme (preferably Ed25519), applying constant-time
comparisons, and implementing anti-rollback to prevent downgrade attacks. Together, these
mechanisms ensure that only firmware explicitly signed by the manufacturer can execute on
the device.

---

*Document: 84 — Firmware Authentication | SPI Flash Security Series*