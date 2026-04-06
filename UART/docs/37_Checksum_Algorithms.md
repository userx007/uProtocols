# 37. Checksum Algorithms

**Algorithms covered (weakest → strongest):**
- **Sum-8 / XOR-8 / LRC** — trivial to implement, detect simple errors, still widely used in legacy protocols (NMEA, Modbus ASCII)
- **Fletcher-16 / Fletcher-32** — position-sensitive double accumulator, catches transpositions that sum-based checks miss
- **Adler-32** — Fletcher variant with a prime modulus, used in zlib/PNG
- **CRC-8 / CRC-16 / CRC-32** — polynomial division giving burst-error detection guarantees, the industry standard for UART

**Code provided for each in both C/C++ and Rust:**
- Bit-by-bit (hardware-friendly, minimal RAM)
- Lookup-table variants (fast, one XOR + one table access per byte)
- A **complete UART packet framing system** — build/parse with SOF, length, sequence number, payload, and CRC-16

**Quick-reference section at the end** gives canonical output values for `"123456789"` across all algorithms, so you can validate any implementation instantly.

## CRC, Fletcher, and Other Checksums for UART Data Integrity

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Checksums Matter in UART Communication](#why-checksums-matter)
3. [Simple Sum Checksum](#simple-sum-checksum)
4. [XOR Checksum](#xor-checksum)
5. [Longitudinal Redundancy Check (LRC)](#longitudinal-redundancy-check)
6. [Fletcher Checksum](#fletcher-checksum)
7. [Adler-32](#adler-32)
8. [CRC — Cyclic Redundancy Check](#crc--cyclic-redundancy-check)
   - [CRC-8](#crc-8)
   - [CRC-16](#crc-16)
   - [CRC-32](#crc-32)
9. [Lookup-Table Optimisation](#lookup-table-optimisation)
10. [Algorithm Comparison](#algorithm-comparison)
11. [Framing a UART Packet with a Checksum](#framing-a-uart-packet-with-a-checksum)
12. [Summary](#summary)

---

## Introduction

In any serial communication system — UART included — data bits can be corrupted by noise,
voltage spikes, timing jitter, or line reflections. A **checksum** is a small, fixed-size
value computed from a block of data and appended to (or embedded in) a transmission so that
the receiver can detect — and sometimes correct — errors without requesting a retransmission.

The term "checksum" is used loosely to cover several families of algorithm:

| Family | Detection Strength | Cost |
|---|---|---|
| Sum / XOR | Weak | Trivial |
| Fletcher / Adler | Moderate | Low |
| CRC | Strong | Low–Medium |
| MD5 / SHA (crypto) | Very strong | High |

For UART on embedded systems the sweet spot is almost always **CRC**, with Fletcher as a
lightweight alternative when ROM and CPU cycles are scarce.

---

## Why Checksums Matter in UART Communication

UART is an *asynchronous* protocol — sender and receiver derive timing independently. This
makes it robust but not immune to errors:

- **Framing errors** – start/stop bits misread.
- **Noise** – a logic level flips a bit.
- **Overrun / buffer overflow** – bytes are dropped.
- **Baud-rate mismatch** – accumulated drift garbles later bytes.

A checksum lets the receiver answer: *"Is this packet intact?"*

```
Sender:  [SOF][LEN][PAYLOAD ... ][CHECKSUM][EOF]
                         ↑
              Checksum covers these bytes
```

If `computed_checksum ≠ received_checksum`, discard and (optionally) request a retry.

---

## Simple Sum Checksum

The simplest possible integrity check: add every byte modulo 256.

### C/C++

```c
#include <stdint.h>
#include <stddef.h>

/**
 * Compute an 8-bit additive checksum.
 * The checksum byte is chosen so that the sum of all bytes
 * (including the checksum itself) equals 0 mod 256.
 */
uint8_t checksum_sum8(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(0x00u - sum);   /* two's complement negation */
}

/** Verify: returns non-zero if the packet (including checksum byte) is valid. */
int checksum_sum8_verify(const uint8_t *packet, size_t total_len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < total_len; i++) {
        sum += packet[i];
    }
    return (sum == 0);
}

/* --- Usage example --- */
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    size_t  len       = sizeof(payload);

    /* Build transmit packet: payload + checksum */
    uint8_t packet[sizeof(payload) + 1];
    memcpy(packet, payload, len);
    packet[len] = checksum_sum8(payload, len);

    printf("Checksum: 0x%02X\n", packet[len]);          /* → 0xF6 */
    printf("Valid: %s\n", checksum_sum8_verify(packet, len + 1) ? "YES" : "NO");

    /* Simulate corruption */
    packet[1] ^= 0xFF;
    printf("After corruption valid: %s\n",
           checksum_sum8_verify(packet, len + 1) ? "YES" : "NO");

    return 0;
}
```

### Rust

```rust
/// Compute an 8-bit additive checksum (two's-complement negation of byte sum).
pub fn checksum_sum8(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
    0u8.wrapping_sub(sum)
}

/// Verify a packet whose last byte is the checksum.
/// Returns true if the packet (payload + checksum) is intact.
pub fn checksum_sum8_verify(packet: &[u8]) -> bool {
    packet.iter().fold(0u8, |acc, &b| acc.wrapping_add(b)) == 0
}

fn main() {
    let payload = [0x01u8, 0x02, 0x03, 0x04];
    let cs = checksum_sum8(&payload);

    let mut packet = Vec::from(payload.as_ref());
    packet.push(cs);

    println!("Checksum : 0x{:02X}", cs);                // → 0xF6
    println!("Valid    : {}", checksum_sum8_verify(&packet));

    // Simulate corruption
    packet[1] ^= 0xFF;
    println!("Corrupted: {}", checksum_sum8_verify(&packet));
}
```

**Weakness:** A sum checksum cannot detect swapped bytes (e.g., `[0x01, 0x02]` and
`[0x02, 0x01]` produce the same sum), nor can it detect an even number of equal and
opposite bit-flip errors.

---

## XOR Checksum

XOR every byte together. Even simpler, and appears frequently in legacy NMEA/GPS sentences
and Modbus-like protocols.

### C/C++

```c
#include <stdint.h>
#include <stddef.h>

uint8_t checksum_xor(const uint8_t *data, size_t len)
{
    uint8_t xor_val = 0;
    for (size_t i = 0; i < len; i++) {
        xor_val ^= data[i];
    }
    return xor_val;
}

int checksum_xor_verify(const uint8_t *payload, size_t len, uint8_t received_cs)
{
    return checksum_xor(payload, len) == received_cs;
}
```

### Rust

```rust
pub fn checksum_xor(data: &[u8]) -> u8 {
    data.iter().fold(0u8, |acc, &b| acc ^ b)
}

pub fn checksum_xor_verify(payload: &[u8], received_cs: u8) -> bool {
    checksum_xor(payload) == received_cs
}
```

**Use case:** NMEA 0183 sentences: `$GPGLL,...*hh<CR><LF>` — the `hh` is a two-digit hex
XOR checksum of the characters between `$` and `*`.

---

## Longitudinal Redundancy Check (LRC)

Used by ISO 1155 and some Modbus variants. The LRC is the two's-complement of the sum of
all bytes mod 256 — effectively identical to `checksum_sum8` above, just named differently.

### C/C++

```c
/* LRC as defined in Modbus ASCII mode */
uint8_t lrc_compute(const uint8_t *data, size_t len)
{
    uint8_t lrc = 0;
    for (size_t i = 0; i < len; i++) {
        lrc = (uint8_t)(lrc + data[i]);
    }
    lrc = (uint8_t)(~lrc + 1u);   /* two's complement */
    return lrc;
}
```

### Rust

```rust
pub fn lrc_compute(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
    (!sum).wrapping_add(1)   // two's complement
}
```

---

## Fletcher Checksum

Invented by John G. Fletcher (1982). Fletcher adds position sensitivity to a plain sum by
maintaining *two* running accumulators, making it much harder to miss transpositions.

```
C0 = Σ  data[i]          (mod M)
C1 = Σ  C0_after_step_i  (mod M)
```

The most common variants are **Fletcher-16** (M = 255, byte-wise) and **Fletcher-32**
(M = 65535, 16-bit-wise).

### Fletcher-16 — C/C++

```c
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t c0;
    uint8_t c1;
} Fletcher16_t;

Fletcher16_t fletcher16(const uint8_t *data, size_t len)
{
    uint16_t c0 = 0, c1 = 0;

    for (size_t i = 0; i < len; i++) {
        c0 = (c0 + data[i]) % 255;
        c1 = (c1 + c0)      % 255;
    }

    return (Fletcher16_t){ .c0 = (uint8_t)c0, .c1 = (uint8_t)c1 };
}

/**
 * Compute the two check bytes to append so that the full block
 * (data + check bytes) yields a Fletcher-16 checksum of {0, 0}.
 */
void fletcher16_check_bytes(const uint8_t *data, size_t len,
                             uint8_t *cb0, uint8_t *cb1)
{
    Fletcher16_t f = fletcher16(data, len);
    *cb0 = (uint8_t)(255u - ((f.c0 + f.c1) % 255));
    *cb1 = (uint8_t)(255u - ((f.c0 + *cb0) % 255));
}

int fletcher16_verify(const uint8_t *packet, size_t total_len)
{
    Fletcher16_t f = fletcher16(packet, total_len);
    return (f.c0 == 0 && f.c1 == 0);
}

/* --- Demo --- */
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* "abcde" → Fletcher-16 checksum should be {0xC8, 0xFB} */
    const uint8_t msg[] = "abcde";
    size_t        len   = 5;

    Fletcher16_t f = fletcher16(msg, len);
    printf("Fletcher-16 C0=0x%02X C1=0x%02X\n", f.c0, f.c1);

    uint8_t packet[len + 2];
    memcpy(packet, msg, len);
    fletcher16_check_bytes(msg, len, &packet[len], &packet[len + 1]);

    printf("Check bytes: 0x%02X 0x%02X\n", packet[len], packet[len + 1]);
    printf("Verify: %s\n", fletcher16_verify(packet, len + 2) ? "OK" : "FAIL");

    return 0;
}
```

### Fletcher-16 — Rust

```rust
#[derive(Debug)]
pub struct Fletcher16 {
    pub c0: u8,
    pub c1: u8,
}

pub fn fletcher16(data: &[u8]) -> Fletcher16 {
    let mut c0: u16 = 0;
    let mut c1: u16 = 0;
    for &byte in data {
        c0 = (c0 + byte as u16) % 255;
        c1 = (c1 + c0)          % 255;
    }
    Fletcher16 { c0: c0 as u8, c1: c1 as u8 }
}

pub fn fletcher16_check_bytes(data: &[u8]) -> (u8, u8) {
    let f   = fletcher16(data);
    let cb0 = 255 - ((f.c0 as u16 + f.c1 as u16) % 255) as u8;
    let cb1 = 255 - ((f.c0 as u16 + cb0 as u16)  % 255) as u8;
    (cb0, cb1)
}

pub fn fletcher16_verify(packet: &[u8]) -> bool {
    let f = fletcher16(packet);
    f.c0 == 0 && f.c1 == 0
}

fn main() {
    let msg = b"abcde";
    let f   = fletcher16(msg);
    println!("Fletcher-16 C0=0x{:02X} C1=0x{:02X}", f.c0, f.c1);

    let (cb0, cb1) = fletcher16_check_bytes(msg);
    let mut packet = msg.to_vec();
    packet.push(cb0);
    packet.push(cb1);

    println!("Check bytes: 0x{:02X} 0x{:02X}", cb0, cb1);
    println!("Verify: {}", fletcher16_verify(&packet));
}
```

### Fletcher-32 — C/C++

```c
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t c0;
    uint16_t c1;
} Fletcher32_t;

Fletcher32_t fletcher32(const uint16_t *data, size_t word_count)
{
    uint32_t c0 = 0, c1 = 0;

    for (size_t i = 0; i < word_count; i++) {
        c0 = (c0 + data[i]) % 65535;
        c1 = (c1 + c0)      % 65535;
    }

    return (Fletcher32_t){ .c0 = (uint16_t)c0, .c1 = (uint16_t)c1 };
}
```

### Fletcher-32 — Rust

```rust
pub fn fletcher32(data: &[u16]) -> (u16, u16) {
    let mut c0: u32 = 0;
    let mut c1: u32 = 0;
    for &word in data {
        c0 = (c0 + word as u32) % 65535;
        c1 = (c1 + c0)          % 65535;
    }
    (c0 as u16, c1 as u16)
}
```

**Advantage over plain sum:** Detects all single-bit errors, all single-byte errors, all
transposition errors of adjacent bytes, and most burst errors.

---

## Adler-32

Used in zlib/PNG. Conceptually identical to Fletcher-32 but uses a base of 65521 (the
largest prime ≤ 65535), which improves the distribution of the check values.

### C/C++

```c
#include <stdint.h>
#include <stddef.h>

#define ADLER_MOD 65521u

uint32_t adler32(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;

    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % ADLER_MOD;
        b = (b + a)       % ADLER_MOD;
    }

    return (b << 16) | a;
}

/* Optimised: defer modulo to every 5552 bytes (max before 32-bit overflow) */
uint32_t adler32_fast(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;
    const size_t NMAX = 5552;

    while (len > 0) {
        size_t n = (len > NMAX) ? NMAX : len;
        len -= n;
        while (n--) {
            a += *data++;
            b += a;
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }

    return (b << 16) | a;
}
```

### Rust

```rust
const ADLER_MOD: u32 = 65521;

pub fn adler32(data: &[u8]) -> u32 {
    let (mut a, mut b) = (1u32, 0u32);
    for &byte in data {
        a = (a + byte as u32) % ADLER_MOD;
        b = (b + a)           % ADLER_MOD;
    }
    (b << 16) | a
}

/// Optimised variant: defers modulo reduction.
pub fn adler32_fast(data: &[u8]) -> u32 {
    const NMAX: usize = 5552;
    let (mut a, mut b) = (1u32, 0u32);

    for chunk in data.chunks(NMAX) {
        for &byte in chunk {
            a = a.wrapping_add(byte as u32);
            b = b.wrapping_add(a);
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }
    (b << 16) | a
}

fn main() {
    let data = b"Wikipedia";
    println!("Adler-32: 0x{:08X}", adler32(data));   // → 0x11E60398
}
```

---

## CRC — Cyclic Redundancy Check

CRC treats the data as a binary polynomial and divides it by a fixed **generator
polynomial**, keeping the remainder as the checksum. It is the gold standard for embedded
error detection:

- Detects all **single-bit** errors.
- Detects all **double-bit** errors (for polynomials of suitable degree).
- Detects all **burst errors** shorter than the CRC degree.
- Detects all **odd numbers** of bit errors (when the polynomial includes `x+1` as a factor).

### Key Parameters

| Parameter | Meaning |
|---|---|
| `poly` | Generator polynomial (without the leading 1) |
| `init` | Initial value of the CRC register |
| `refin` | Reflect input bytes? |
| `refout` | Reflect final CRC? |
| `xorout` | XOR applied to final value |

---

### CRC-8

CRC-8/MAXIM (Dallas 1-Wire), polynomial `0x31`.

#### C/C++

```c
#include <stdint.h>
#include <stddef.h>

#define CRC8_POLY 0x31u    /* x^8 + x^5 + x^4 + 1  (Dallas/Maxim) */
#define CRC8_INIT 0x00u

uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = CRC8_INIT;

    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80u) {
                crc = (uint8_t)((crc << 1) ^ CRC8_POLY);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Reflected variant (LSB-first, as used by Dallas 1-Wire) */
uint8_t crc8_reflected(const uint8_t *data, size_t len)
{
    uint8_t crc = CRC8_INIT;
    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x01u) ? (uint8_t)((crc >> 1) ^ 0x8Cu) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}
```

#### Rust

```rust
const CRC8_POLY: u8 = 0x31;
const CRC8_INIT: u8 = 0x00;

pub fn crc8(data: &[u8]) -> u8 {
    let mut crc = CRC8_INIT;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ CRC8_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

pub fn crc8_reflected(data: &[u8]) -> u8 {
    let mut crc = CRC8_INIT;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x01 != 0 {
                (crc >> 1) ^ 0x8C
            } else {
                crc >> 1
            };
        }
    }
    crc
}
```

---

### CRC-16

Two common variants:

| Variant | Poly | Init | RefIn | RefOut | XorOut | Usage |
|---|---|---|---|---|---|---|
| CRC-16/IBM (ARC) | 0x8005 | 0x0000 | Yes | Yes | 0x0000 | USB, Modbus |
| CRC-16/CCITT-FALSE | 0x1021 | 0xFFFF | No | No | 0x0000 | X/YModem |

#### C/C++ — CRC-16/CCITT-FALSE

```c
#include <stdint.h>
#include <stddef.h>

#define CRC16_POLY 0x1021u
#define CRC16_INIT 0xFFFFu

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;

    while (len--) {
        crc ^= (uint16_t)(*data++ << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u)
                ? (uint16_t)((crc << 1) ^ CRC16_POLY)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* CRC-16/IBM  (reflected, used in Modbus RTU) */
uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x0001u)
                ? (uint16_t)((crc >> 1) ^ 0xA001u)   /* reflected 0x8005 */
                : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
```

#### Rust — CRC-16/CCITT and Modbus

```rust
const CRC16_CCITT_POLY: u16 = 0x1021;
const CRC16_CCITT_INIT: u16 = 0xFFFF;

pub fn crc16_ccitt(data: &[u8]) -> u16 {
    let mut crc = CRC16_CCITT_INIT;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ CRC16_CCITT_POLY
            } else {
                crc << 1
            };
        }
    }
    crc
}

pub fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            crc = if crc & 0x0001 != 0 {
                (crc >> 1) ^ 0xA001   // reflected poly
            } else {
                crc >> 1
            };
        }
    }
    crc
}

fn main() {
    let data = b"123456789";
    println!("CRC-16/CCITT-FALSE : 0x{:04X}", crc16_ccitt(data));  // → 0x29B1
    println!("CRC-16/Modbus      : 0x{:04X}", crc16_modbus(data)); // → 0x4B37
}
```

---

### CRC-32

The most widely deployed checksum in the world: Ethernet, ZIP, PNG, gzip all use
CRC-32 (polynomial `0x04C11DB7`, reflected `0xEDB88320`).

#### C/C++

```c
#include <stdint.h>
#include <stddef.h>

#define CRC32_POLY_REF 0xEDB88320uL   /* reflected 0x04C11DB7 */

uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFuL;

    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ CRC32_POLY_REF) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFuL;
}

/* Verification: crc32(data + crc_bytes) == 0x2144DF1C (the "residue") */
int crc32_verify(const uint8_t *packet, size_t total_len)
{
    return crc32(packet, total_len) == 0x2144DF1CuL;
}

/* --- Demo --- */
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t msg[] = "123456789";
    uint32_t      crc   = crc32(msg, 9);

    printf("CRC-32: 0x%08lX\n", (unsigned long)crc);  /* → 0xCBF43926 */

    /* Append CRC LSB-first to form a self-verifying packet */
    uint8_t packet[9 + 4];
    memcpy(packet, msg, 9);
    packet[9]  = (uint8_t)(crc);
    packet[10] = (uint8_t)(crc >> 8);
    packet[11] = (uint8_t)(crc >> 16);
    packet[12] = (uint8_t)(crc >> 24);

    printf("Self-verifying: %s\n", crc32_verify(packet, 13) ? "OK" : "FAIL");
    return 0;
}
```

#### Rust

```rust
const CRC32_POLY_REF: u32 = 0xEDB8_8320;

pub fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ CRC32_POLY_REF
            } else {
                crc >> 1
            };
        }
    }
    crc ^ 0xFFFF_FFFF
}

pub fn crc32_verify(packet: &[u8]) -> bool {
    crc32(packet) == 0x2144_DF1C
}

fn main() {
    let msg = b"123456789";
    let crc = crc32(msg);
    println!("CRC-32: 0x{:08X}", crc);   // → 0xCBF43926

    // Build self-verifying packet
    let mut packet = msg.to_vec();
    packet.extend_from_slice(&crc.to_le_bytes());

    println!("Verify: {}", crc32_verify(&packet));
}
```

---

## Lookup-Table Optimisation

The bit-by-bit loops above are fine for low data rates. For high-throughput UART streams,
a **256-entry lookup table** eliminates the inner loop, reducing CRC computation to one
XOR and one table lookup per byte.

### CRC-32 Table Generation — C/C++

```c
#include <stdint.h>
#include <stddef.h>

static uint32_t crc32_table[256];
static int      table_ready = 0;

void crc32_table_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320uL) : (crc >> 1);
        }
        crc32_table[i] = crc;
    }
    table_ready = 1;
}

uint32_t crc32_table_calc(const uint8_t *data, size_t len)
{
    if (!table_ready) crc32_table_init();
    uint32_t crc = 0xFFFFFFFFuL;
    while (len--) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ *data++) & 0xFFu];
    }
    return crc ^ 0xFFFFFFFFuL;
}
```

### CRC-32 Table — Rust (compile-time generation)

```rust
/// Generate the CRC-32 lookup table at compile time.
const fn make_crc32_table() -> [u32; 256] {
    let mut table = [0u32; 256];
    let mut i = 0usize;
    while i < 256 {
        let mut crc = i as u32;
        let mut bit = 0;
        while bit < 8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
            bit += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
}

static CRC32_TABLE: [u32; 256] = make_crc32_table();

pub fn crc32_fast(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        let idx = ((crc ^ byte as u32) & 0xFF) as usize;
        crc = (crc >> 8) ^ CRC32_TABLE[idx];
    }
    crc ^ 0xFFFF_FFFF
}

fn main() {
    let data = b"123456789";
    println!("CRC-32 (fast): 0x{:08X}", crc32_fast(data));  // → 0xCBF43926
}
```

### CRC-16 Table — C/C++

```c
#include <stdint.h>
#include <stddef.h>

static uint16_t crc16_table[256];

void crc16_table_init(void)
{
    for (uint16_t i = 0; i < 256; i++) {
        uint16_t crc = 0;
        uint16_t c   = i << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = ((crc ^ c) & 0x8000u)
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
            c <<= 1;
        }
        crc16_table[i] = crc;
    }
}

uint16_t crc16_ccitt_fast(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    while (len--) {
        uint8_t idx = (uint8_t)(crc >> 8) ^ *data++;
        crc = (uint16_t)((crc << 8) ^ crc16_table[idx]);
    }
    return crc;
}
```

---

## Algorithm Comparison

| Algorithm | Output | Error Detection | Code Size | Speed | Best Use |
|---|---|---|---|---|---|
| Sum-8 | 1 byte | Weak | Tiny | Fastest | Legacy / toy |
| XOR-8 | 1 byte | Weak | Tiny | Fastest | NMEA, simple frames |
| LRC | 1 byte | Weak | Tiny | Fastest | Modbus ASCII |
| Fletcher-16 | 2 bytes | Good | Small | Fast | Embedded, IETF RFC 3309 |
| Fletcher-32 | 4 bytes | Good | Small | Fast | Memory integrity |
| Adler-32 | 4 bytes | Good | Small | Fast | zlib / PNG |
| CRC-8 | 1 byte | Good | Small | Fast | 1-Wire sensors |
| CRC-16 | 2 bytes | Very good | Small | Fast | USB, Modbus RTU, XMODEM |
| CRC-32 | 4 bytes | Excellent | Medium | Fast (with table) | Ethernet, ZIP, UART |
| MD5 | 16 bytes | Cryptographic | Large | Slow | Not for UART |

**Rule of thumb for UART:**

- 8-bit payload, tight RAM → **CRC-8**
- General purpose embedded → **CRC-16/CCITT** or **CRC-16/Modbus**
- High-reliability / firmware update → **CRC-32**

---

## Framing a UART Packet with a Checksum

A complete production UART packet scheme combining framing bytes, length, sequence number,
payload, and CRC:

```
┌────────┬────────┬─────────┬──────────────────┬───────────────┐
│  SOF   │  LEN   │  SEQ    │   PAYLOAD[0..N]  │  CRC-16 (LE)  │
│  0xAA  │ 1 byte │ 1 byte  │   N bytes        │   2 bytes     │
└────────┴────────┴─────────┴──────────────────┴───────────────┘
  CRC covers: LEN + SEQ + PAYLOAD (not SOF)
```

### C/C++ — Full Packet Builder & Parser

```c
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define UART_SOF      0xAAu
#define UART_MAX_PAYLOAD 128u

typedef struct {
    uint8_t sof;
    uint8_t len;
    uint8_t seq;
    uint8_t payload[UART_MAX_PAYLOAD];
    uint16_t crc;
} UartPacket_t;

/* Re-use the CRC-16/CCITT function shown earlier */
extern uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/**
 * Build a UART frame into `buf`. Returns total frame length.
 * buf must be at least (4 + payload_len) bytes.
 */
size_t uart_build_frame(uint8_t       *buf,
                         uint8_t        seq,
                         const uint8_t *payload,
                         uint8_t        payload_len)
{
    buf[0] = UART_SOF;
    buf[1] = payload_len;
    buf[2] = seq;
    memcpy(&buf[3], payload, payload_len);

    /* CRC covers len + seq + payload */
    uint16_t crc = crc16_ccitt(&buf[1], 2u + payload_len);
    buf[3 + payload_len]     = (uint8_t)(crc & 0xFFu);
    buf[3 + payload_len + 1] = (uint8_t)(crc >> 8);

    return 3u + payload_len + 2u;
}

typedef enum {
    PARSE_OK = 0,
    PARSE_ERR_SOF,
    PARSE_ERR_LEN,
    PARSE_ERR_CRC,
} ParseResult_t;

/**
 * Parse and validate a raw UART frame.
 * `frame_len` must equal 3 + packet->len + 2.
 */
ParseResult_t uart_parse_frame(const uint8_t *buf,
                                size_t         frame_len,
                                UartPacket_t  *out)
{
    if (buf[0] != UART_SOF)                        return PARSE_ERR_SOF;
    uint8_t plen = buf[1];
    if (frame_len != (size_t)(3u + plen + 2u))    return PARSE_ERR_LEN;
    if (plen > UART_MAX_PAYLOAD)                   return PARSE_ERR_LEN;

    uint16_t recv_crc = (uint16_t)(buf[3 + plen]) |
                        ((uint16_t)(buf[3 + plen + 1]) << 8);
    uint16_t calc_crc = crc16_ccitt(&buf[1], 2u + plen);
    if (recv_crc != calc_crc)                      return PARSE_ERR_CRC;

    out->sof = buf[0];
    out->len = plen;
    out->seq = buf[2];
    memcpy(out->payload, &buf[3], plen);
    out->crc = recv_crc;
    return PARSE_OK;
}

/* --- Demo --- */
#include <stdio.h>

int main(void)
{
    uint8_t payload[] = { 0x10, 0x20, 0x30 };
    uint8_t frame[sizeof(payload) + 8];

    size_t flen = uart_build_frame(frame, 1, payload, sizeof(payload));
    printf("Frame (%zu bytes):", flen);
    for (size_t i = 0; i < flen; i++) printf(" %02X", frame[i]);
    puts("");

    UartPacket_t pkt;
    ParseResult_t res = uart_parse_frame(frame, flen, &pkt);
    printf("Parse result: %s\n", res == PARSE_OK ? "OK" : "ERROR");

    /* Corrupt one byte */
    frame[3] ^= 0xFF;
    res = uart_parse_frame(frame, flen, &pkt);
    printf("After corruption: %s\n", res == PARSE_OK ? "OK" : "CRC ERROR");

    return 0;
}
```

### Rust — Full Packet Builder & Parser

```rust
const UART_SOF: u8 = 0xAA;

pub fn uart_build_frame(seq: u8, payload: &[u8]) -> Vec<u8> {
    let mut frame = Vec::with_capacity(5 + payload.len());
    frame.push(UART_SOF);
    frame.push(payload.len() as u8);
    frame.push(seq);
    frame.extend_from_slice(payload);

    // CRC covers len + seq + payload (indices 1..)
    let crc = crc16_ccitt(&frame[1..]);
    frame.push(crc as u8);
    frame.push((crc >> 8) as u8);
    frame
}

#[derive(Debug, PartialEq)]
pub enum ParseError {
    BadSof,
    BadLength,
    BadCrc,
}

pub struct UartPacket {
    pub seq:     u8,
    pub payload: Vec<u8>,
}

pub fn uart_parse_frame(buf: &[u8]) -> Result<UartPacket, ParseError> {
    if buf.is_empty() || buf[0] != UART_SOF {
        return Err(ParseError::BadSof);
    }
    let plen = buf[1] as usize;
    if buf.len() != 3 + plen + 2 {
        return Err(ParseError::BadLength);
    }

    let recv_crc = buf[3 + plen] as u16 | ((buf[4 + plen] as u16) << 8);
    let calc_crc = crc16_ccitt(&buf[1..3 + plen]);
    if recv_crc != calc_crc {
        return Err(ParseError::BadCrc);
    }

    Ok(UartPacket {
        seq:     buf[2],
        payload: buf[3..3 + plen].to_vec(),
    })
}

fn main() {
    let payload = [0x10u8, 0x20, 0x30];
    let frame = uart_build_frame(1, &payload);

    print!("Frame ({} bytes):", frame.len());
    for b in &frame { print!(" {:02X}", b); }
    println!();

    match uart_parse_frame(&frame) {
        Ok(pkt) => println!("Parsed OK, seq={}", pkt.seq),
        Err(e)  => println!("Error: {:?}", e),
    }

    // Corrupt and re-parse
    let mut bad_frame = frame.clone();
    bad_frame[3] ^= 0xFF;
    match uart_parse_frame(&bad_frame) {
        Ok(_)  => println!("OK (unexpected)"),
        Err(e) => println!("Corruption detected: {:?}", e),
    }
}
```

---

## Summary

Checksum algorithms are an essential layer of defence in UART communication. Here is a
concise guide to choosing the right one:

| Scenario | Recommended Algorithm | Rationale |
|---|---|---|
| Minimal overhead, low risk | XOR-8 or Sum-8 | Simple, almost zero CPU |
| Modbus RTU | CRC-16/IBM | Protocol mandated |
| XMODEM / YMODEM | CRC-16/CCITT | Protocol mandated |
| General embedded UART | CRC-16/CCITT-FALSE | Balance of strength and size |
| Firmware OTA update | CRC-32 | Maximum error detection |
| RAM/Flash self-test | Fletcher-32 or Adler-32 | Good on word-aligned data |
| Tight ROM, 8-bit MCU | CRC-8/Maxim | Single-byte output, small tables |

### Key Design Rules

1. **CRC is almost always preferable** to simple sum or XOR for anything safety-relevant.
2. **Use a lookup table** when data throughput is a concern; the table costs 256 bytes for
   CRC-8, 512 bytes for CRC-16, and 1 KB for CRC-32.
3. **Always include the checksum in a clearly defined packet frame** — agree on exactly
   which bytes the checksum covers (usually everything after the SOF byte).
4. **Transmit CRC bytes in a defined byte order** (little-endian is conventional on most
   embedded systems); document it.
5. **Verify on the receiver side before acting on data** — never trust unchecked bytes,
   especially for commands that actuate hardware.
6. **CRC is not encryption** — it detects accidental corruption only. Malicious
   modification requires cryptographic authentication (HMAC).
7. **Use well-known polynomial/init/xorout combinations** so that on-line CRC calculators
   can cross-check your implementation during development.

### Algorithm Quick Reference

```
Data: "123456789" (ASCII bytes 0x31…0x39)

Sum-8          : 0xDD
XOR-8          : 0x31
Fletcher-16    : {0xDE, 0xB1}
Adler-32       : 0x091E01DE
CRC-8/Maxim    : 0xA1
CRC-16/CCITT   : 0x29B1
CRC-16/Modbus  : 0x4B37
CRC-32         : 0xCBF43926
```

These reference values allow you to validate any implementation instantly.