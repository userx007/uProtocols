# 90. Side-Channel Attack Prevention in SPI Communication

**Theory & Threat Model** — Defines the adversary (physical access, passive measurement, chosen plaintext) and maps attack types to target assets: keys, tokens, control flow.

**Attack Types** — Explains timing attacks (early-exit comparison), SPA/DPA (Hamming weight leakage, power trace analysis), EM analysis, and cache-timing attacks on table-lookup AES.

**C/C++ Examples (7 implementations):**
- `ct_memcmp` / `ct_select_u8` — branchless constant-time primitives
- `secure_memzero` — volatile-write zeroization with compiler fence
- Constant-time MAC verification for SPI packets
- Masked AES S-box lookup (first-order Boolean masking)
- TRNG-based timing jitter injection for SPI transfers
- Constant-time key slot selection using bitmask accumulation
- AES-GCM authenticated frame with monotonic nonce counter

**Rust Examples (6 implementations):**
- `subtle::ConstantTimeEq` for MAC verification
- `ConditionallySelectable` key store lookup (no branch on slot index)
- `MaskedByte` struct — masked-domain XOR with `ZeroizeOnDrop`
- `JitteredSpiBus` trait wrapper with `black_box` dummy ops
- `SpiSession` using `aes_gcm` with replay detection
- `no_std` constant-time utilities for bare-metal embedded

**Testing & Validation** — Unit tests for all CT primitives, plus a tooling table covering `dudect`, `ctgrind`, `ChipWhisperer`, and Rust lints.

> **Protecting against timing and power analysis attacks on SPI communication**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Threat Model](#threat-model)
3. [Types of Side-Channel Attacks](#types-of-side-channel-attacks)
   - [Timing Attacks](#timing-attacks)
   - [Power Analysis Attacks (SPA / DPA)](#power-analysis-attacks)
   - [Electromagnetic (EM) Analysis](#electromagnetic-analysis)
   - [Cache-Timing Attacks](#cache-timing-attacks)
4. [Countermeasures Overview](#countermeasures-overview)
5. [Constant-Time Programming](#constant-time-programming)
6. [Masking and Blinding](#masking-and-blinding)
7. [Noise Injection](#noise-injection)
8. [Hardware-Level Mitigations](#hardware-level-mitigations)
9. [Secure SPI Key Management](#secure-spi-key-management)
10. [Code Examples in C/C++](#code-examples-in-cc)
11. [Code Examples in Rust](#code-examples-in-rust)
12. [Testing and Validation](#testing-and-validation)
13. [Summary](#summary)

---

## Introduction

SPI (Serial Peripheral Interface) is a synchronous full-duplex serial communication protocol widely used to connect microcontrollers to peripherals such as flash memory, sensors, displays, and cryptographic co-processors. While SPI itself is a physical/transport-layer protocol and does not carry inherent cryptographic guarantees, the *data* flowing over SPI—encryption keys, authentication tokens, plaintext/ciphertext blocks—can leak sensitive information through observable physical phenomena.

**Side-channel attacks** exploit indirect information leaked by the *implementation* of a system rather than mathematical weaknesses in algorithms. Even a perfectly secure AES-256 implementation can be broken if its execution time or power consumption varies depending on secret key bits.

The attack surface on SPI includes:

- The **host controller** (e.g., a microcontroller) performing cryptographic operations before transmission.
- The **SPI peripheral** (e.g., a secure element or crypto accelerator) during command processing.
- The **bus itself**, whose voltage transitions encode processed data.
- **Clock and chip-select timing**, which may reveal operation boundaries.

This document covers the theoretical foundations, practical attack vectors, and robust countermeasures applicable to embedded and systems-level SPI implementations.

---

## Threat Model

Before applying mitigations, it is important to define the adversary model:

| Attacker Capability | Example |
|---|---|
| **Physical access** to the device | Probing SPI lines with an oscilloscope |
| **Passive measurement** of power consumption | Measuring voltage drop across a shunt resistor |
| **Electromagnetic probing** | Near-field EM probe above the MCU package |
| **Timing measurements** via the SPI bus or a side channel | Remote timing via network-connected device |
| **Replay and fault injection** | Clock glitching, voltage glitching |
| **Multiple queries with known/chosen plaintexts** | Differential Power Analysis (DPA) |

The attacker goal is typically to recover:
- **Secret keys** used for encryption/authentication.
- **PINs, passwords, or tokens** exchanged over SPI.
- **Algorithm control flow** revealing which branch was taken.

---

## Types of Side-Channel Attacks

### Timing Attacks

Timing attacks exploit variation in the *execution time* of cryptographic operations. If a function processing secret data takes different amounts of time depending on secret values (due to conditional branches, early exits, or data-dependent memory access patterns), an attacker can statistically infer those values.

**Example**: A naive byte comparison:
```c
// INSECURE: Returns early on first mismatch → timing varies with key similarity
int compare(const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}
```
An attacker who can measure the duration of SPI-triggered authentication can determine how many bytes of a submitted password match the stored secret by observing when the comparison returns early.

### Power Analysis Attacks

Power analysis measures the instantaneous current drawn by a device during computation. Two primary variants:

- **Simple Power Analysis (SPA)**: A single trace reveals key-dependent operations. For example, RSA using the square-and-multiply algorithm performs *different operations* for key bits 0 and 1, visible directly in the power trace.

- **Differential Power Analysis (DPA)**: Statistical analysis of many power traces, each with a different known input. DPA can recover secret keys even with significant noise, requiring only a few hundred to a few thousand traces for unprotected implementations.

**Hamming weight leakage**: Most CMOS circuits consume power proportional to the number of bit transitions (Hamming distance) or the Hamming weight of processed data. This means that operating on `0xFF` consumes measurably more power than operating on `0x00`.

### Electromagnetic Analysis

Similar to power analysis but uses a near-field EM probe placed above the chip. EM analysis can be more localized, targeting specific modules (e.g., the AES S-box computation area), and can sometimes defeat power-line filtering countermeasures.

### Cache-Timing Attacks

On processors with caches (application processors running Linux, etc.), table-lookup-based cryptographic implementations (like AES using 256-byte S-boxes) exhibit cache miss/hit patterns that depend on secret key material. If an SPI transaction triggers such computations, and an attacker can measure cache access times (via a co-resident process), key material can be extracted.

---

## Countermeasures Overview

| Countermeasure | Addresses | Complexity |
|---|---|---|
| Constant-time code | Timing attacks | Medium |
| Masking / Secret sharing | DPA, EM analysis | High |
| Blinding | Timing, SPA | Medium |
| Random dummy operations | DPA, SPA | Low |
| Noise injection (hardware) | DPA, EM | Low |
| Power supply decoupling | Power analysis | Low (hardware) |
| EM shielding | EM analysis | Low (hardware) |
| Key refreshing | DPA (reduces traces) | Medium |
| Authenticated encryption | Protocol level | Low |
| Hardware security modules | All physical | High (cost) |

---

## Constant-Time Programming

The foundation of software side-channel resistance. The execution time of a function must be **independent of secret values**.

### Rules for Constant-Time Code

1. **No secret-dependent branches**: Replace `if (secret)` with arithmetic masks.
2. **No secret-dependent memory accesses**: Avoid `table[secret_index]`.
3. **No secret-dependent loop counts**: Iterate a fixed number of times.
4. **Beware of compiler optimizations**: Compilers may reintroduce branches. Use compiler barriers and `volatile`.
5. **Beware of CPU microarchitecture**: Branch predictors, out-of-order execution can reintroduce timing variation. Critical paths may require assembly.

### Constant-Time Primitives

The following primitives are the building blocks of constant-time code:

```
CT_SELECT(cond, a, b):  returns a if cond != 0, else b   (no branch)
CT_COMPARE(a, b, len):  returns 0 if equal, non-zero otherwise (no early exit)
CT_COPY(dst, src, len, cond): conditionally copies (no branch on cond)
```

---

## Masking and Blinding

**Masking** splits secret values into multiple shares such that each individual share is statistically independent of the secret. A first-order mask uses two shares: `x = x_masked XOR mask`, where `mask` is a fresh random value. All operations must be performed on the masked representation.

**Blinding** applies a random bijection to inputs before processing and inverts it afterward. Used in RSA: instead of computing `m^d mod n` directly, compute `(m * r^e)^d mod n * r^{-1} mod n = m^d mod n`, where `r` is a random blinding factor.

Both techniques ensure that the power consumption/timing of a single trace is decorrelated from the secret, requiring many more traces (or rendering DPA infeasible) against a first-order masking scheme.

---

## Noise Injection

**Hardware noise** can be injected by:
- Running a parallel "dummy" process that performs random memory operations.
- Using a hardware random number generator to toggle GPIO lines.
- Adding carefully designed decoupling capacitors and ferrite beads to the power supply.

**Software-level jitter**: Inserting random delays (using a TRNG) before and during SPI operations smears power traces in time, making trace alignment (a prerequisite for DPA) significantly harder.

---

## Hardware-Level Mitigations

- **Power supply filtering**: Large decoupling capacitors (100 µF+) on VCC lines smooth power signatures.
- **On-chip voltage regulators**: Isolate the SPI peripheral's supply from observable external rails.
- **EM shielding**: Metal enclosures and shielded cables for SPI buses.
- **Secure Elements (SE) / Trusted Platform Modules (TPM)**: Dedicated chips with built-in countermeasures, certified to resist physical attacks (Common Criteria EAL4+, FIPS 140-3).
- **Randomized SPI clocks**: Some hardware supports spread-spectrum clocking, which smears EM emissions.

---

## Secure SPI Key Management

Keys used in SPI-protected communication should:
- Never be stored in plaintext flash that is directly readable over SPI.
- Be loaded into hardware key registers (if available) rather than software buffers.
- Be ephemeral where possible (session keys derived via ECDH, for example).
- Be erased from RAM immediately after use using a secure memory wipe (not `memset`, which compilers may optimize away).

---

## Code Examples in C/C++

### 1. Constant-Time Byte Comparison (MAC Verification)

```c
#include <stdint.h>
#include <stddef.h>

/**
 * Constant-time comparison of two byte buffers.
 * The execution time does NOT depend on the content of 'a' or 'b'.
 *
 * Returns 0 if buffers are equal, non-zero otherwise.
 *
 * IMPORTANT: The *length* must not be secret (it's typically known).
 */
int ct_memcmp(const volatile uint8_t *a, const volatile uint8_t *b, size_t len) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];   /* accumulate all differences; no early exit */
    }
    /* diff == 0 iff all bytes matched */
    return (int)diff;
}

/**
 * Constant-time select: returns 'a' if mask != 0, 'b' if mask == 0.
 * mask must be 0x00 or 0xFF (a full-byte mask).
 */
static inline uint8_t ct_select_u8(uint8_t mask, uint8_t a, uint8_t b) {
    return (mask & a) | (~mask & b);
}

/**
 * Produce a full-byte mask (0xFF) if val != 0, (0x00) if val == 0.
 * No branch on val.
 */
static inline uint8_t ct_nonzero_mask(uint8_t val) {
    /* Propagate any set bit to the MSB, then arithmetic-shift right */
    uint16_t tmp = (uint16_t)val;
    tmp = (tmp | (0u - tmp)) >> 8;
    return (uint8_t)(0u - tmp); /* 0xFF if val!=0, 0x00 if val==0 */
}
```

### 2. Secure Memory Erase (Prevents Compiler Optimization)

```c
#include <string.h>

/**
 * Guaranteed-to-execute memory zeroization.
 * Standard memset() may be elided by the compiler if the buffer
 * is not subsequently read. This version uses a memory fence to prevent that.
 */
void secure_memzero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
    /* Full compiler memory barrier: prevents reordering past this point */
    __asm__ __volatile__("" ::: "memory");
}

/* C11 alternative using memset_s (if available) */
#if defined(__STDC_LIB_EXT1__)
#include <string.h>
void secure_memzero_c11(void *ptr, size_t len) {
    memset_s(ptr, len, 0, len);
}
#endif
```

### 3. Constant-Time SPI MAC Verification

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAC_LEN   16   /* 128-bit HMAC-SHA256 truncated, or CMAC */
#define DATA_MAX  256

typedef struct {
    uint8_t data[DATA_MAX];
    uint8_t mac[MAC_LEN];
    uint16_t data_len;
} spi_packet_t;

/* External: compute MAC over data using a stored key */
extern void compute_mac(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t *out_mac);
extern const uint8_t g_spi_session_key[32];

/**
 * Verify the MAC in a received SPI packet.
 * SECURE: The accept/reject decision does NOT reveal which bytes mismatched.
 *
 * Returns true if MAC is valid, false otherwise.
 */
bool spi_verify_mac(const spi_packet_t *pkt) {
    uint8_t expected_mac[MAC_LEN];

    /* Always compute the expected MAC (never skip) */
    compute_mac(g_spi_session_key, sizeof(g_spi_session_key),
                pkt->data, pkt->data_len, expected_mac);

    /* Constant-time comparison: no timing information about match position */
    int result = ct_memcmp(
        (const volatile uint8_t *)expected_mac,
        (const volatile uint8_t *)pkt->mac,
        MAC_LEN
    );

    /* Zeroize intermediate MAC immediately */
    secure_memzero(expected_mac, sizeof(expected_mac));

    return (result == 0);
}
```

### 4. Masked AES S-Box Lookup (First-Order Boolean Masking)

```c
#include <stdint.h>

/*
 * Standard AES S-box (256 bytes).
 * Normally indexed by a byte that depends on the secret key.
 * A direct table lookup leaks key bits through cache timing.
 */
extern const uint8_t aes_sbox[256];

/**
 * Masked S-box lookup using first-order Boolean masking.
 *
 * Instead of sbox[x], we compute sbox[x XOR mask] XOR sbox_masked[mask],
 * where sbox_masked is a precomputed table that encodes the mask effect.
 *
 * For demonstration: a simpler (but still protective) approach is
 * to evaluate the S-box in a data-independent way using bitslicing.
 *
 * Here we show the random-mask approach for clarity.
 *
 * @param x_masked  The masked input byte: x XOR input_mask
 * @param input_mask The random mask applied to x
 * @param output_mask The desired output mask
 * @return  sbox[x] XOR output_mask  (masked output)
 */
uint8_t masked_sbox(uint8_t x_masked, uint8_t input_mask, uint8_t output_mask,
                    const uint8_t precomp_sbox_mask[256]) {
    /*
     * Correctness: sbox[x] = sbox[x_masked XOR input_mask]
     * We evaluate sbox at (x_masked XOR input_mask) -- this access pattern
     * still depends on x. For a truly masked lookup you need a full
     * table re-randomization or a bitsliced S-box.
     *
     * The precomputed sbox_mask[input_mask] allows correction:
     *   result = sbox[x_masked] XOR precomp_sbox_mask[input_mask] XOR output_mask
     *
     * NOTE: This is a simplified illustration. Production implementations
     * should use verified bitsliced or tower-field S-box constructions.
     */
    uint8_t s1 = aes_sbox[x_masked];
    uint8_t s2 = precomp_sbox_mask[input_mask]; /* = sbox[input_mask] */
    return s1 ^ s2 ^ output_mask;
}
```

### 5. Adding Timing Noise to SPI Transactions

```c
#include <stdint.h>

/* Platform-specific: read a 32-bit hardware random number */
extern uint32_t hw_trng_read(void);

/* Platform-specific: busy-wait for 'cycles' CPU cycles */
extern void busy_wait_cycles(uint32_t cycles);

#define JITTER_MAX_CYCLES  200   /* Maximum random delay in CPU cycles */

/**
 * Perform an SPI byte transfer with random pre-transfer jitter.
 * This smears the power/EM trace in time, complicating DPA alignment.
 *
 * IMPORTANT: Jitter alone is NOT sufficient against DPA; it must be
 * combined with algorithmic countermeasures. It increases the number
 * of traces needed by approximately JITTER_MAX_CYCLES / clock_period.
 */
uint8_t spi_transfer_jittered(uint8_t tx_byte) {
    /* Random jitter: [0, JITTER_MAX_CYCLES) cycles */
    uint32_t jitter = hw_trng_read() % JITTER_MAX_CYCLES;
    busy_wait_cycles(jitter);

    /* Perform the actual SPI transfer (platform-specific HAL call) */
    extern uint8_t hal_spi_transfer_byte(uint8_t);
    uint8_t rx_byte = hal_spi_transfer_byte(tx_byte);

    return rx_byte;
}

/**
 * Transmit a fixed-length buffer over SPI with per-byte jitter.
 * The total transfer time is randomized, making trace alignment harder.
 */
void spi_transmit_secure(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        spi_transfer_jittered(buf[i]);
    }
    /* Post-transfer dummy operations to obscure end-of-operation boundary */
    uint32_t dummy_count = (hw_trng_read() % 5) + 1;
    for (uint32_t d = 0; d < dummy_count; d++) {
        (void)hw_trng_read();  /* Dummy TRNG read: causes power activity */
    }
}
```

### 6. Constant-Time Key Loading from SPI Flash

```c
#include <stdint.h>
#include <stdbool.h>

#define KEY_SLOT_COUNT   4
#define KEY_SIZE         32   /* 256-bit key */

typedef struct {
    uint8_t  key[KEY_SIZE];
    uint32_t key_id;
    bool     valid;
} key_slot_t;

/* Key store in secure RAM (never DMA'd to SPI bus directly) */
static volatile key_slot_t key_store[KEY_SLOT_COUNT];

/**
 * Constant-time key selection from the key store.
 *
 * Returns the key for 'target_id' into 'out_key'.
 * The access pattern does NOT branch on which slot matches,
 * preventing an attacker from inferring the key index via timing.
 *
 * Returns true if a matching key was found.
 */
bool ct_get_key(uint32_t target_id, uint8_t out_key[KEY_SIZE]) {
    volatile uint8_t found_mask = 0x00; /* 0xFF when found */
    uint8_t result[KEY_SIZE] = {0};

    for (int slot = 0; slot < KEY_SLOT_COUNT; slot++) {
        /* Constant-time ID comparison */
        uint32_t id_xor = key_store[slot].key_id ^ target_id;

        /* Fold all bits of id_xor into a single byte */
        uint8_t id_diff = (uint8_t)(id_xor | (id_xor >> 8) |
                                     (id_xor >> 16) | (id_xor >> 24));

        /* valid_and_match: 0xFF if this slot is valid AND id matches */
        uint8_t valid = (uint8_t)(key_store[slot].valid ? 0xFF : 0x00);
        uint8_t match = (uint8_t)(ct_nonzero_mask(id_diff) ^ 0xFF); /* 0xFF if id_diff==0 */
        uint8_t slot_mask = valid & match & ~found_mask; /* only first match counts */

        /* Conditionally copy this slot's key into result (constant-time) */
        for (int b = 0; b < KEY_SIZE; b++) {
            result[b] = ct_select_u8(slot_mask, (uint8_t)key_store[slot].key[b], result[b]);
        }

        /* Mark as found (prevents subsequent slots from overwriting) */
        found_mask |= slot_mask;
    }

    /* Copy result to output */
    memcpy(out_key, result, KEY_SIZE);
    secure_memzero(result, KEY_SIZE);

    return (found_mask != 0x00);
}
```

### 7. SPI Transaction Wrapper with Replay Protection and Timing Hardening

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define NONCE_SIZE   12  /* 96-bit nonce for AES-GCM */
#define TAG_SIZE     16  /* 128-bit authentication tag */

typedef struct {
    uint8_t  nonce[NONCE_SIZE];
    uint16_t payload_len;
    uint8_t  payload[DATA_MAX];
    uint8_t  tag[TAG_SIZE];
} secure_spi_frame_t;

static uint64_t g_tx_counter = 0; /* Monotonic transmit counter */
static uint64_t g_rx_counter = 0; /* Expected receive counter  */

/**
 * Prepare and transmit a hardened SPI frame:
 *  - Authenticated encryption (AES-GCM) prevents data tampering.
 *  - Monotonic counter in nonce prevents replay attacks.
 *  - Fixed frame structure prevents length-based side channels.
 */
bool spi_send_secure_frame(const uint8_t *plaintext, uint16_t pt_len,
                           const uint8_t key[32]) {
    secure_spi_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Build nonce: 4-byte random || 8-byte counter */
    uint32_t rnd = hw_trng_read();
    memcpy(frame.nonce, &rnd, 4);
    memcpy(frame.nonce + 4, &g_tx_counter, 8);
    g_tx_counter++;

    frame.payload_len = pt_len;

    /* AES-GCM encrypt (platform/library provided) */
    extern bool aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                                 const uint8_t *nonce, size_t nonce_len,
                                 const uint8_t *pt, size_t pt_len,
                                 uint8_t *ct, uint8_t *tag);

    bool ok = aes_gcm_encrypt(key, 32,
                               frame.nonce, NONCE_SIZE,
                               plaintext, pt_len,
                               frame.payload, frame.tag);
    if (!ok) {
        secure_memzero(&frame, sizeof(frame));
        return false;
    }

    /* Transmit the fixed-size frame (always send sizeof(frame) bytes) */
    spi_transmit_secure((const uint8_t *)&frame, sizeof(frame));
    secure_memzero(&frame, sizeof(frame));
    return true;
}
```

---

## Code Examples in Rust

Rust's type system and borrow checker eliminate entire classes of memory safety bugs, but side-channel safety still requires explicit attention. The `subtle` crate provides constant-time primitives audited for Rust.

### 1. Constant-Time Comparison Using the `subtle` Crate

```rust
// Cargo.toml dependencies:
// subtle = "2.5"
// zeroize = "1.7"

use subtle::ConstantTimeEq;
use zeroize::Zeroize;

const MAC_LEN: usize = 16;

/// Verify a MAC received over SPI in constant time.
/// Returns `true` only if `received_mac` exactly matches `expected_mac`.
///
/// Using `subtle::ConstantTimeEq` ensures the comparison time is
/// independent of where (or if) the two arrays differ.
pub fn verify_spi_mac(expected_mac: &[u8; MAC_LEN], received_mac: &[u8; MAC_LEN]) -> bool {
    // ConstantTimeEq::ct_eq() returns a subtle::Choice (not a bool)
    // .into() converts the Choice to a bool only at the final decision point
    expected_mac.ct_eq(received_mac).into()
}

/// Example: SPI packet authentication
pub struct SpiPacket {
    pub data: Vec<u8>,
    pub mac:  [u8; MAC_LEN],
}

pub fn authenticate_packet(packet: &SpiPacket, session_key: &[u8; 32]) -> bool {
    let mut expected = [0u8; MAC_LEN];

    // Compute expected MAC (using a real HMAC/CMAC library in production)
    compute_hmac_sha256(session_key, &packet.data, &mut expected);

    let result = verify_spi_mac(&expected, &packet.mac);

    // Zeroize intermediate MAC value immediately after use
    expected.zeroize();

    result
}

fn compute_hmac_sha256(_key: &[u8; 32], _data: &[u8], _out: &mut [u8; MAC_LEN]) {
    // In production: use hmac + sha2 crates from RustCrypto
    todo!("use hmac::Hmac<sha2::Sha256> from RustCrypto")
}
```

### 2. Secure Key Store with Constant-Time Lookup

```rust
use subtle::{Choice, ConditionallySelectable, ConstantTimeEq};
use zeroize::{Zeroize, ZeroizeOnDrop};

const KEY_SIZE: usize = 32;
const KEY_SLOTS: usize = 4;

/// A key slot that zeroizes its key material when dropped.
#[derive(Clone, ZeroizeOnDrop)]
struct KeySlot {
    key_id: u32,
    key:    [u8; KEY_SIZE],
    valid:  bool,
}

impl Default for KeySlot {
    fn default() -> Self {
        KeySlot { key_id: 0, key: [0u8; KEY_SIZE], valid: false }
    }
}

/// A key store that exposes keys via constant-time lookup.
pub struct SecureKeyStore {
    slots: [KeySlot; KEY_SLOTS],
}

impl SecureKeyStore {
    pub fn new() -> Self {
        SecureKeyStore {
            slots: Default::default(),
        }
    }

    /// Store a key in the first available slot.
    pub fn store_key(&mut self, key_id: u32, key: &[u8; KEY_SIZE]) -> bool {
        for slot in &mut self.slots {
            if !slot.valid {
                slot.key_id = key_id;
                slot.key.copy_from_slice(key);
                slot.valid = true;
                return true;
            }
        }
        false // No free slots
    }

    /// Retrieve a key by ID using constant-time selection.
    ///
    /// The access pattern does NOT branch on which slot matches,
    /// preventing timing-based inference of the key index.
    pub fn get_key(&self, target_id: u32) -> Option<[u8; KEY_SIZE]> {
        let mut result    = [0u8; KEY_SIZE];
        // subtle::Choice: 0u8 = false, 1u8 = true
        let mut found     = Choice::from(0u8);

        for slot in &self.slots {
            let id_matches = slot.key_id.ct_eq(&target_id);
            let is_valid   = Choice::from(slot.valid as u8);
            // This slot contributes only if it is valid AND the ID matches
            // AND we haven't already found a match (first match wins)
            let select = is_valid & id_matches & !found;

            // Conditionally copy key bytes without branching
            for (r, &k) in result.iter_mut().zip(slot.key.iter()) {
                *r = u8::conditional_select(r, &k, select);
            }
            found |= select;
        }

        if bool::from(found) {
            Some(result)
        } else {
            None
        }
    }
}

impl Drop for SecureKeyStore {
    fn drop(&mut self) {
        // ZeroizeOnDrop on KeySlot handles key zeroization automatically
    }
}
```

### 3. Masked SPI Byte Processing

```rust
use rand::RngCore;
use zeroize::Zeroize;

/// First-order Boolean masking wrapper for sensitive byte values.
///
/// Invariant: `masked ^ mask == plaintext` at all times.
/// Neither `masked` nor `mask` alone reveals `plaintext`.
#[derive(Clone)]
pub struct MaskedByte {
    masked: u8,
    mask:   u8,
}

impl MaskedByte {
    /// Create a masked representation of `value` using a fresh random mask.
    pub fn new<R: RngCore>(value: u8, rng: &mut R) -> Self {
        let mask   = (rng.next_u32() & 0xFF) as u8;
        let masked = value ^ mask;
        MaskedByte { masked, mask }
    }

    /// XOR two masked bytes: (a ^ mask_a) ^ (b ^ mask_b) == (a^b) ^ (mask_a^mask_b)
    /// The result is a masked representation of (a XOR b).
    pub fn xor(&self, other: &MaskedByte) -> MaskedByte {
        MaskedByte {
            masked: self.masked ^ other.masked,
            mask:   self.mask   ^ other.mask,
        }
    }

    /// Unmask the value. Only call this when the result must leave the
    /// masked domain (e.g., to write to SPI TX register).
    ///
    /// Minimize the lifetime of the unmasked value.
    pub fn unmask(&self) -> u8 {
        self.masked ^ self.mask
    }
}

impl Zeroize for MaskedByte {
    fn zeroize(&mut self) {
        self.masked.zeroize();
        self.mask.zeroize();
    }
}

impl Drop for MaskedByte {
    fn drop(&mut self) {
        self.zeroize();
    }
}

/// Process a sensitive SPI payload byte-by-byte in the masked domain,
/// applying a key byte via XOR (stream cipher mode illustration).
///
/// Both `payload` and `key` are masked; the XOR is performed without
/// ever exposing either unmasked value in a register that persists.
pub fn masked_xor_encrypt(
    payload: &[u8],
    key:     &[u8],
    rng:     &mut impl RngCore,
) -> Vec<u8> {
    assert_eq!(payload.len(), key.len());
    let mut output = Vec::with_capacity(payload.len());

    for (&p_byte, &k_byte) in payload.iter().zip(key.iter()) {
        let mut mp = MaskedByte::new(p_byte, rng);
        let mut mk = MaskedByte::new(k_byte, rng);

        // XOR in masked domain: no unmasked intermediate
        let mut mc = mp.xor(&mk);

        // Unmask only at the last moment, immediately written to output buffer
        output.push(mc.unmask());

        // Zeroize all masked intermediates
        mc.zeroize();
        mp.zeroize();
        mk.zeroize();
    }

    output
}
```

### 4. SPI Transfer with Timing Jitter

```rust
use rand::RngCore;
use std::hint::black_box;

const JITTER_MAX_US: u64 = 200; // Maximum random delay in microseconds

/// Trait abstracting an SPI bus for testability.
pub trait SpiBus {
    fn transfer_byte(&mut self, tx: u8) -> u8;
}

/// Wraps a real SPI bus and adds random timing jitter.
pub struct JitteredSpiBus<B: SpiBus, R: RngCore> {
    inner: B,
    rng:   R,
}

impl<B: SpiBus, R: RngCore> JitteredSpiBus<B, R> {
    pub fn new(inner: B, rng: R) -> Self {
        JitteredSpiBus { inner, rng }
    }

    /// Transfer a single byte with a random pre-transfer delay.
    pub fn transfer_byte_jittered(&mut self, tx: u8) -> u8 {
        let jitter_us = self.rng.next_u64() % JITTER_MAX_US;
        // Platform-specific sleep (no_std: use a busy loop instead)
        std::thread::sleep(std::time::Duration::from_micros(jitter_us));
        self.inner.transfer_byte(tx)
    }

    /// Transfer a buffer with per-byte jitter and post-transfer dummy ops.
    pub fn transfer_buffer_secure(&mut self, tx: &[u8], rx: &mut [u8]) {
        assert_eq!(tx.len(), rx.len());
        for (t, r) in tx.iter().zip(rx.iter_mut()) {
            *r = self.transfer_byte_jittered(*t);
        }

        // Dummy post-transfer operations to obscure the end-of-operation boundary
        let dummy_count = (self.rng.next_u32() % 5) + 1;
        for _ in 0..dummy_count {
            // black_box prevents the compiler from eliding the dummy work
            let _ = black_box(self.rng.next_u32());
        }
    }
}
```

### 5. Authenticated Encrypted SPI Frame (AES-GCM)

```rust
use aes_gcm::{
    aead::{Aead, KeyInit, OsRng},
    Aes256Gcm, Nonce, Key,
};
use zeroize::{Zeroize, ZeroizeOnDrop};

const NONCE_SIZE: usize = 12;
const TAG_SIZE:   usize = 16;

/// A transmitted SPI frame: nonce + ciphertext + GCM tag
pub struct SecureSpiFrame {
    pub nonce:      [u8; NONCE_SIZE],
    pub ciphertext: Vec<u8>,  // includes appended tag (aes_gcm crate convention)
}

/// Session state tracking the monotonic transmit counter.
#[derive(ZeroizeOnDrop)]
pub struct SpiSession {
    cipher:  Aes256Gcm,
    tx_seq:  u64,
    rx_seq:  u64,
}

impl SpiSession {
    pub fn new(key: &[u8; 32]) -> Self {
        let key = Key::<Aes256Gcm>::from_slice(key);
        SpiSession {
            cipher: Aes256Gcm::new(key),
            tx_seq: 0,
            rx_seq: 0,
        }
    }

    /// Encrypt and authenticate a plaintext payload for transmission.
    ///
    /// The nonce embeds a monotonically increasing counter to prevent replay.
    pub fn seal(&mut self, plaintext: &[u8]) -> Result<SecureSpiFrame, &'static str> {
        let mut nonce_bytes = [0u8; NONCE_SIZE];
        // First 4 bytes: random, last 8 bytes: sequence counter
        let rand_part: [u8; 4] = OsRng.next_u32().to_le_bytes();
        nonce_bytes[..4].copy_from_slice(&rand_part);
        nonce_bytes[4..].copy_from_slice(&self.tx_seq.to_le_bytes());
        self.tx_seq += 1;

        let nonce = Nonce::from_slice(&nonce_bytes);
        let ciphertext = self.cipher
            .encrypt(nonce, plaintext)
            .map_err(|_| "Encryption failed")?;

        Ok(SecureSpiFrame { nonce: nonce_bytes, ciphertext })
    }

    /// Decrypt and verify a received SPI frame.
    ///
    /// Enforces monotonically increasing sequence numbers to prevent replay.
    pub fn open(&mut self, frame: &SecureSpiFrame) -> Result<Vec<u8>, &'static str> {
        // Extract and verify the sequence counter from the nonce
        let mut rx_counter = [0u8; 8];
        rx_counter.copy_from_slice(&frame.nonce[4..]);
        let counter = u64::from_le_bytes(rx_counter);

        // Replay check: counter must be strictly greater than last accepted
        if counter <= self.rx_seq && self.rx_seq != 0 {
            return Err("Replay detected");
        }

        let nonce = Nonce::from_slice(&frame.nonce);
        let plaintext = self.cipher
            .decrypt(nonce, frame.ciphertext.as_slice())
            .map_err(|_| "Decryption/authentication failed")?;

        self.rx_seq = counter;
        Ok(plaintext)
    }
}
```

### 6. No-STD Constant-Time Utilities for Embedded Rust

```rust
#![no_std]

/// Constant-time byte equality: returns 0xFF if a == b, 0x00 otherwise.
/// No branches, no data-dependent operations.
#[inline(always)]
pub fn ct_eq_u8(a: u8, b: u8) -> u8 {
    let mut x = (a ^ b) as u16;
    // If x != 0, set high bit; then arithmetic shift right 8 gives 0xFF or 0x00
    x = x.wrapping_neg().wrapping_shr(8); // 0x01XX -> 0xFF, 0x0000 -> 0x00
    !(x as u8) // invert: 0x00 -> 0xFF (equal), 0xFF -> 0x00 (not equal)
}

/// Constant-time select: returns `a` if `mask` is 0xFF, `b` if `mask` is 0x00.
#[inline(always)]
pub fn ct_select(mask: u8, a: u8, b: u8) -> u8 {
    (mask & a) | (!mask & b)
}

/// Constant-time comparison of two fixed-length arrays.
/// Returns 0xFF if equal, 0x00 if not equal.
pub fn ct_array_eq<const N: usize>(a: &[u8; N], b: &[u8; N]) -> u8 {
    let mut diff: u8 = 0;
    for i in 0..N {
        diff |= a[i] ^ b[i];
    }
    // diff == 0 iff all bytes matched; produce 0xFF for equal, 0x00 for not equal
    let mut result = diff as u16;
    result = result.wrapping_neg().wrapping_shr(8);
    !(result as u8)
}

/// Securely zeroize a mutable byte slice.
/// Uses a volatile write loop to prevent compiler elision.
pub fn secure_zeroize(buf: &mut [u8]) {
    for byte in buf.iter_mut() {
        // Volatile write: compiler cannot prove it has no observable effect,
        // so it will not optimize it away.
        unsafe {
            core::ptr::write_volatile(byte as *mut u8, 0u8);
        }
    }
    // Compiler fence: prevents reordering of memory operations
    core::sync::atomic::compiler_fence(core::sync::atomic::Ordering::SeqCst);
}

/// SPI receive buffer handler: verify an expected pattern in constant time.
/// Used to detect frame synchronization bytes without timing leakage.
pub fn verify_frame_header(received: &[u8; 4], expected: &[u8; 4]) -> bool {
    ct_array_eq(received, expected) == 0xFF
}
```

---

## Testing and Validation

### Unit Testing Constant-Time Properties

While it is difficult to *prove* constant-time execution in pure software testing (this requires microarchitectural analysis), you can validate logical correctness and check for obvious timing issues:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ct_eq_u8_correctness() {
        assert_eq!(ct_eq_u8(0xAB, 0xAB), 0xFF);
        assert_eq!(ct_eq_u8(0xAB, 0xAC), 0x00);
        assert_eq!(ct_eq_u8(0x00, 0x00), 0xFF);
        assert_eq!(ct_eq_u8(0xFF, 0x00), 0x00);
    }

    #[test]
    fn ct_array_eq_correctness() {
        let a = [0u8, 1, 2, 3, 255, 128, 64, 32];
        let b = a;
        let mut c = a;
        c[7] ^= 1;
        assert_eq!(ct_array_eq(&a, &b), 0xFF);
        assert_eq!(ct_array_eq(&a, &c), 0x00);
    }

    #[test]
    fn ct_select_correctness() {
        assert_eq!(ct_select(0xFF, 0xAA, 0xBB), 0xAA);
        assert_eq!(ct_select(0x00, 0xAA, 0xBB), 0xBB);
    }

    #[test]
    fn mac_verify_rejects_modified_mac() {
        let expected = [0x42u8; 16];
        let mut tampered = expected;
        tampered[8] ^= 0x01;
        // Must reject; timing must not differ from a pass
        assert!(!verify_spi_mac(&expected, &tampered));
    }

    #[test]
    fn key_store_constant_time_retrieval() {
        let mut store = SecureKeyStore::new();
        let key0 = [0xAAu8; KEY_SIZE];
        let key1 = [0xBBu8; KEY_SIZE];
        store.store_key(100, &key0);
        store.store_key(200, &key1);

        assert_eq!(store.get_key(100).unwrap(), key0);
        assert_eq!(store.get_key(200).unwrap(), key1);
        assert!(store.get_key(999).is_none());
    }
}
```

### Tooling for Side-Channel Analysis

| Tool | Purpose |
|---|---|
| **dudect** (C library) | Statistical constant-time testing using Welch's t-test |
| **ctgrind** (Valgrind plugin) | Tracks secret-tainted memory; flags secret-dependent branches |
| **TIMECOP** | Marks secret memory regions; fails on non-CT operations |
| **ChipWhisperer** | Open-source hardware platform for power/EM capture and analysis |
| **Inspector SCA** (Riscure) | Commercial DPA/CPA analysis tool |
| **scared** (Python) | Open-source side-channel analysis framework |

### Static Analysis

```bash
# Use clang's memory sanitizer to detect uninitialized reads
clang -fsanitize=memory -fno-omit-frame-pointer -O1 spi_secure.c -o test

# Check for constant-time violations with ctgrind
valgrind --tool=memcheck --track-origins=yes ./test

# Rust: deny unsafe and enable strict lints
RUSTFLAGS="-D warnings -D unsafe-code" cargo build
```

---

## Summary

Side-channel attacks on SPI communication represent a powerful class of practical attacks that exploit *physical information leakage* rather than mathematical weaknesses. The key takeaways are:

**Threat Landscape**

Attackers with physical access—or sometimes even remote timing access—can recover secret keys and authentication tokens by measuring execution time, power consumption, or electromagnetic emissions during SPI-triggered cryptographic operations. Even a single SPA trace can reveal an RSA private key operation; a few hundred DPA traces can break unprotected AES.

**Software Countermeasures**

- **Constant-time code** is the primary defense against timing attacks. Replace all secret-dependent branches, early exits, and data-dependent memory accesses with branchless arithmetic using masking and bit manipulation.
- **Secure memory zeroization** using `volatile` writes or library primitives (`zeroize` crate, `memset_s`) ensures key material is not left in memory after use.
- **First-order Boolean masking** splits secret values into random shares, decorrelating single power/EM traces from key-dependent computation.
- **Random timing jitter** inserted before and during SPI operations makes trace alignment harder, increasing the number of traces needed for DPA.
- **Authenticated encryption** (AES-GCM) with monotonic nonces provides integrity, confidentiality, and replay prevention at the protocol level.

**Rust-Specific Advantages**

The `subtle` crate provides peer-reviewed constant-time comparison primitives. The `zeroize` crate ensures automatic, compiler-resistant key zeroization. Rust's `ZeroizeOnDrop` derive macro eliminates entire classes of key-material leakage by construction.

**Hardware Mitigations**

Software measures alone are insufficient against a determined adversary with specialized equipment. Hardware countermeasures—power supply decoupling, EM shielding, spread-spectrum clocking, and certified secure elements—are essential for high-assurance deployments.

**Validation**

Use tools like `dudect`, `ctgrind`, and ChipWhisperer to empirically verify constant-time properties. No software review alone can guarantee resistance to all microarchitectural side channels (speculative execution, cache timing); combine software, hardware, and formal verification where possible.

| Countermeasure | Threat Mitigated | Effort |
|---|---|---|
| Constant-time compare | Timing attacks | Low |
| Secure zeroize | Key-material leakage | Low |
| Boolean masking | DPA / EM analysis | High |
| Timing jitter | Trace alignment | Low |
| AES-GCM + counter nonce | Replay, integrity | Medium |
| Hardware decoupling | Power analysis | Low (hardware) |
| Secure Element | All physical | High (cost) |

Implementing even the low-effort software countermeasures—constant-time comparisons, secure zeroization, and authenticated encryption—significantly raises the bar for practical attacks on SPI-connected cryptographic systems.

---

*Document: 90 — Side-Channel Attack Prevention in SPI Communication*
*Languages: C/C++, Rust | Target: Embedded & Systems Programming*