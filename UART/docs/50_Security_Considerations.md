# 50. UART Security Considerations

- Threat model table covering injection, replay, overflow, eavesdropping, DoS, and format-string attacks
- Input validation principles and common attack vectors
- Challenge–response authentication and replay protection design
- Secure framing protocol (SOF / LENGTH / SEQ / PAYLOAD / CRC)
- Encryption and key exchange strategies
- Debug port / bootloader lockdown guidance
- Rate limiting and DoS protection
- Logging and anomaly detection

**C/C++ examples (6):**
1. Safe bounded receive with whitelist validation
2. Framed protocol parser with CRC-16
3. Sliding-window replay protection
4. HMAC-SHA256 challenge–response auth (mbedTLS)
5. Token-bucket rate limiter
6. Safe command dispatcher (prevents format-string and injection)

**Rust examples (7):**
1. Bounded receive with `embedded-hal` and `Result`
2. Frame parser using `heapless::Vec` (no-std safe)
3. Replay window with unit tests
4. HMAC-SHA256 using `hmac`/`sha2`/`subtle` crates
5. Token bucket with `saturating_add`
6. Type-safe command dispatcher
7. Full secure receive loop tying all layers together

**Summary table** comparing C/C++ vs. Rust approaches for each security layer.

> **Protecting against injection attacks and unauthorized access**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Threat Model](#threat-model)
3. [Input Validation and Injection Attacks](#input-validation-and-injection-attacks)
4. [Authentication and Access Control](#authentication-and-access-control)
5. [Data Integrity and Framing](#data-integrity-and-framing)
6. [Encryption over UART](#encryption-over-uart)
7. [Secure Bootloader / Debug Port Lockdown](#secure-bootloader--debug-port-lockdown)
8. [Rate Limiting and DoS Protection](#rate-limiting-and-dos-protection)
9. [Logging and Anomaly Detection](#logging-and-anomaly-detection)
10. [Code Examples in C/C++](#code-examples-in-cc)
11. [Code Examples in Rust](#code-examples-in-rust)
12. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most widely used serial communication interfaces in embedded systems, IoT devices, industrial controllers, and debug consoles. Its simplicity is both a strength and a security liability.

Unlike network protocols (TCP/IP, TLS), raw UART provides **no built-in authentication, encryption, or integrity checks**. Any attacker with physical or logical access to the UART lines can:

- Eavesdrop on plaintext data
- Inject malicious commands
- Replay previously captured frames
- Impersonate a legitimate device
- Crash or reconfigure firmware via a debug console

Security must therefore be **designed into the application layer** sitting above the physical UART transport. This document covers the key attack vectors and the countermeasures you should implement in C/C++ and Rust.

---

## Threat Model

| Threat | Attack Vector | Impact |
|--------|--------------|--------|
| Command injection | Malformed or crafted input bytes | Arbitrary command execution |
| Replay attack | Re-sending a captured valid frame | Unauthorized repeated commands |
| Buffer overflow | Oversized input | Code execution / crash |
| Eavesdropping | Physical tap on TX/RX lines | Data confidentiality breach |
| Unauthorized access | No authentication on debug UART | Full device control |
| DoS / flooding | Rapid frame injection | Resource exhaustion |
| Format string attack | Passing input to `printf`-family | Memory read/write primitives |

---

## Input Validation and Injection Attacks

### Principles

1. **Never trust UART input.** Treat every byte from the UART as potentially adversarial.
2. **Whitelist, don't blacklist.** Accept only known-good characters/values.
3. **Length-check before copy.** Verify lengths against declared buffer sizes.
4. **Null-terminate defensively.** Always ensure C strings are null-terminated within bounds.
5. **Avoid passing raw input to interpreters** (shell, `printf`, `eval`).

### Common Injection Vectors

#### Buffer Overflow

Reading more bytes than the destination buffer can hold is the most classic vulnerability in UART handlers.

#### Command Injection

If received strings are passed to `system()`, `popen()`, or a command parser without sanitization, an attacker can append extra commands via delimiters (`;`, `&&`, `|`, `\n`).

#### Format String Injection

Passing UART-received strings directly to `printf(buf)` instead of `printf("%s", buf)` exposes the stack.

---

## Authentication and Access Control

### Challenge–Response Authentication

Without physical security, the UART port should require authentication before accepting any commands. A simple HMAC-SHA256 challenge–response scheme:

1. Host sends `HELLO` request.
2. Device responds with a random 16-byte nonce.
3. Host computes `HMAC-SHA256(shared_secret, nonce)` and sends it back.
4. Device verifies the MAC before granting a session.

### Sequence Numbers / Replay Protection

Authenticated sessions should include a monotonically increasing sequence number or timestamp in each frame. The receiver rejects frames with out-of-order or duplicate sequence numbers.

---

## Data Integrity and Framing

A secure UART frame should include:

```
[SOF][LENGTH][SEQ][PAYLOAD...][CRC16 or HMAC-truncated]
```

- **SOF** – Start-of-frame marker (e.g., `0xAA 0x55`)
- **LENGTH** – Length of the payload (validates against maximum)
- **SEQ** – Sequence number (replay protection)
- **PAYLOAD** – Actual data
- **CRC/MAC** – Integrity check

---

## Encryption over UART

For sensitive data, AES-128-CTR or ChaCha20 can be applied at the application layer. Key exchange can use:

- **Pre-shared keys (PSK)** – Simplest, suitable for factory-provisioned devices.
- **ECDH** – If the device has sufficient compute; generates a session key per session.

---

## Secure Bootloader / Debug Port Lockdown

Many microcontrollers (STM32, nRF52, ESP32) expose a UART-based bootloader or JTAG/SWD interface. Best practices:

- **Disable the bootloader UART** in production firmware via option bytes or e-fuse.
- **Lock the debug port** via Read Out Protection (ROP) levels.
- **Require signed firmware** for any update accepted over UART.
- **Authenticate before entering bootloader mode** even if you keep it enabled.

---

## Rate Limiting and DoS Protection

A UART command parser should limit:

- Bytes accepted per second (token-bucket or leaky-bucket)
- Number of failed authentication attempts before lockout
- Maximum frame rate (frames per second)

---

## Logging and Anomaly Detection

Even on resource-constrained systems, security events should be logged to non-volatile memory:

- Failed authentication attempts
- Malformed frame counts
- Unexpected resets triggered by UART input
- Rate-limit violations

---

## Code Examples in C/C++

### 1. Safe Receive with Length and Whitelist Validation

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define UART_BUF_MAX   128
#define CMD_MAX_LEN     64

/* Receive a null-terminated command string from UART.
 * Returns number of bytes received, or -1 on error.
 * Enforces maximum length and whitelist of printable ASCII. */
int uart_receive_command(uint8_t *buf, uint16_t buf_size)
{
    if (!buf || buf_size == 0) return -1;

    uint16_t idx = 0;
    int byte;

    while (idx < buf_size - 1) {          /* always reserve room for '\0' */
        byte = uart_getchar_timeout(100);  /* platform-specific, returns -1 on timeout */
        if (byte < 0) break;              /* timeout or error */

        /* Accept only printable ASCII (0x20–0x7E) and newline as terminator */
        if ((uint8_t)byte == '\n' || (uint8_t)byte == '\r') {
            break;                         /* end of command */
        }
        if ((uint8_t)byte < 0x20 || (uint8_t)byte > 0x7E) {
            /* Reject non-printable / non-ASCII bytes */
            return -1;
        }

        buf[idx++] = (uint8_t)byte;
    }

    buf[idx] = '\0';   /* safe: idx <= buf_size - 1 */
    return (int)idx;
}
```

---

### 2. Framed Protocol Parser with CRC and Length Check

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define FRAME_SOF_0     0xAAu
#define FRAME_SOF_1     0x55u
#define FRAME_PAYLOAD_MAX 120u

typedef struct {
    uint8_t  seq;
    uint8_t  length;           /* payload length only */
    uint8_t  payload[FRAME_PAYLOAD_MAX];
    uint16_t crc;
} UartFrame;

/* CRC-16/CCITT-FALSE */
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1;
        }
    }
    return crc;
}

/* Returns true if a valid frame was received into *frame. */
bool uart_receive_frame(UartFrame *frame)
{
    /* Wait for SOF */
    if (uart_getchar_blocking() != FRAME_SOF_0) return false;
    if (uart_getchar_blocking() != FRAME_SOF_1) return false;

    frame->seq    = (uint8_t)uart_getchar_blocking();
    frame->length = (uint8_t)uart_getchar_blocking();

    /* *** Critical: length bound check BEFORE reading payload *** */
    if (frame->length > FRAME_PAYLOAD_MAX) return false;

    for (uint8_t i = 0; i < frame->length; i++) {
        frame->payload[i] = (uint8_t)uart_getchar_blocking();
    }

    uint8_t crc_hi = (uint8_t)uart_getchar_blocking();
    uint8_t crc_lo = (uint8_t)uart_getchar_blocking();
    frame->crc = ((uint16_t)crc_hi << 8) | crc_lo;

    /* Compute CRC over seq + length + payload */
    uint8_t crc_buf[2 + FRAME_PAYLOAD_MAX];
    crc_buf[0] = frame->seq;
    crc_buf[1] = frame->length;
    memcpy(&crc_buf[2], frame->payload, frame->length);
    uint16_t expected_crc = crc16(crc_buf, 2u + frame->length);

    return (expected_crc == frame->crc);
}
```

---

### 3. Replay Protection with Sequence Numbers

```c
#include <stdint.h>
#include <stdbool.h>

#define SEQ_WINDOW 8u   /* accept up to 8 out-of-order but not replayed */

static uint8_t last_accepted_seq = 0u;
static uint8_t seq_bitmap = 0u;   /* bitmap of accepted seq in the window */

bool replay_check_and_update(uint8_t seq)
{
    int8_t diff = (int8_t)(seq - last_accepted_seq);

    if (diff > 0) {
        /* New, forward-moving sequence number */
        if (diff > (int8_t)SEQ_WINDOW) {
            /* Too far ahead – probably a skip or attack */
            return false;
        }
        /* Slide the window */
        seq_bitmap = (uint8_t)(seq_bitmap << (uint8_t)diff);
        seq_bitmap |= 1u;
        last_accepted_seq = seq;
        return true;
    } else if (diff == 0) {
        return false; /* exact duplicate */
    } else {
        /* Behind current head – check the window */
        uint8_t age = (uint8_t)(-diff);
        if (age >= SEQ_WINDOW) return false;   /* too old */
        uint8_t mask = (uint8_t)(1u << age);
        if (seq_bitmap & mask) return false;   /* already seen */
        seq_bitmap |= mask;
        return true;
    }
}
```

---

### 4. HMAC-SHA256 Challenge–Response Authentication (C++)

```cpp
#include <cstdint>
#include <cstring>
#include <array>
#include "mbedtls/md.h"   // mbedTLS, widely used on embedded targets

static const uint8_t PSK[32] = {
    /* 32-byte pre-shared key – replace with device-unique key from secure storage */
    0x6f, 0x3a, 0x21, 0xbc, 0x9e, 0x44, 0x7d, 0x18,
    0xaa, 0x55, 0xf2, 0x03, 0x11, 0xcc, 0x87, 0x6e,
    0xd4, 0x91, 0x38, 0x5c, 0xe7, 0x4b, 0x02, 0xa9,
    0x70, 0xfe, 0x83, 0x1a, 0x09, 0xde, 0x6b, 0x47
};

/* Computes HMAC-SHA256 of nonce using PSK into mac_out[32] */
bool compute_auth_mac(const uint8_t *nonce, size_t nonce_len,
                      uint8_t mac_out[32])
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0) return false;
    if (mbedtls_md_hmac_starts(&ctx, PSK, sizeof(PSK)) != 0) goto fail;
    if (mbedtls_md_hmac_update(&ctx, nonce, nonce_len) != 0) goto fail;
    if (mbedtls_md_hmac_finish(&ctx, mac_out) != 0) goto fail;

    mbedtls_md_free(&ctx);
    return true;

fail:
    mbedtls_md_free(&ctx);
    return false;
}

/* Constant-time comparison to prevent timing side-channels */
bool constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

/* Server-side: verify the MAC received from the client */
bool uart_verify_auth(const uint8_t *nonce, size_t nonce_len,
                      const uint8_t *received_mac)
{
    uint8_t expected[32];
    if (!compute_auth_mac(nonce, nonce_len, expected)) return false;
    return constant_time_compare(expected, received_mac, 32);
}
```

---

### 5. Rate Limiter (Token Bucket)

```c
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t tokens;        /* current token count (scaled by 1000) */
    uint32_t capacity;      /* maximum tokens (scaled) */
    uint32_t refill_rate;   /* tokens added per ms (scaled) */
    uint32_t last_refill_ms;
} TokenBucket;

void token_bucket_init(TokenBucket *tb, uint32_t capacity_frames,
                       uint32_t frames_per_second)
{
    tb->capacity      = capacity_frames * 1000u;
    tb->tokens        = tb->capacity;
    tb->refill_rate   = (frames_per_second * 1000u) / 1000u; /* per ms */
    tb->last_refill_ms = system_get_ms(); /* platform-specific */
}

bool token_bucket_consume(TokenBucket *tb)
{
    uint32_t now = system_get_ms();
    uint32_t elapsed = now - tb->last_refill_ms;
    tb->last_refill_ms = now;

    /* Refill */
    uint32_t new_tokens = elapsed * tb->refill_rate;
    tb->tokens += new_tokens;
    if (tb->tokens > tb->capacity) tb->tokens = tb->capacity;

    /* Consume one token (1000 scaled units) */
    if (tb->tokens < 1000u) return false;  /* rate exceeded */
    tb->tokens -= 1000u;
    return true;
}
```

---

### 6. Safe Command Dispatcher (No Format String Injection)

```c
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    void (*handler)(const char *args);
} Command;

static void cmd_status(const char *args) { /* ... */ (void)args; }
static void cmd_reset(const char *args)  { /* ... */ (void)args; }

static const Command CMD_TABLE[] = {
    { "STATUS", cmd_status },
    { "RESET",  cmd_reset  },
};
#define CMD_TABLE_SIZE (sizeof(CMD_TABLE) / sizeof(CMD_TABLE[0]))

void dispatch_command(const char *input)
{
    /* Split at first space */
    char name[32] = {0};
    const char *args = "";

    const char *sp = strchr(input, ' ');
    if (sp) {
        size_t name_len = (size_t)(sp - input);
        if (name_len >= sizeof(name)) return;   /* name too long */
        memcpy(name, input, name_len);
        args = sp + 1;
    } else {
        if (strlen(input) >= sizeof(name)) return;
        strncpy(name, input, sizeof(name) - 1);
    }

    for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(name, CMD_TABLE[i].name) == 0) {
            CMD_TABLE[i].handler(args);
            return;
        }
    }

    /* Command not found – use %s not the raw string! */
    fprintf(stderr, "Unknown command: %s\n", name);  /* safe: %s format literal */
}
```

---

## Code Examples in Rust

Rust's type system and ownership model eliminate entire classes of memory-safety bugs at compile time. The examples below show idiomatic Rust for embedded UART security.

### 1. Safe Bounded Receive and Whitelist Validation

```rust
use core::str;

const CMD_MAX: usize = 64;

#[derive(Debug, PartialEq)]
pub enum UartError {
    TooLong,
    InvalidByte(u8),
    Timeout,
    Io,
}

/// Read a printable-ASCII command line from a UART reader.
/// Accepts bytes in [0x20, 0x7E] and terminates on '\n'.
pub fn receive_command<R: embedded_hal::serial::Read<u8>>(
    uart: &mut R,
    buf: &mut [u8; CMD_MAX],
) -> Result<usize, UartError> {
    let mut idx = 0usize;

    loop {
        let byte = nb::block!(uart.read()).map_err(|_| UartError::Io)?;

        if byte == b'\n' || byte == b'\r' {
            break;
        }

        // Whitelist: printable ASCII only
        if !(0x20..=0x7E).contains(&byte) {
            return Err(UartError::InvalidByte(byte));
        }

        if idx >= CMD_MAX {
            return Err(UartError::TooLong);   // reject before overflow
        }

        buf[idx] = byte;
        idx += 1;
    }

    Ok(idx)
}
```

---

### 2. Framed Protocol with CRC-16

```rust
use core::convert::TryInto;

const SOF: [u8; 2]     = [0xAA, 0x55];
const PAYLOAD_MAX: usize = 120;

#[derive(Debug)]
pub struct Frame {
    pub seq:     u8,
    pub payload: heapless::Vec<u8, PAYLOAD_MAX>,
}

/// CRC-16/CCITT-FALSE
pub fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

pub fn parse_frame(raw: &[u8]) -> Option<Frame> {
    // Minimum frame: SOF(2) + seq(1) + len(1) + crc(2) = 6 bytes
    if raw.len() < 6 { return None; }
    if raw[0] != SOF[0] || raw[1] != SOF[1] { return None; }

    let seq    = raw[2];
    let length = raw[3] as usize;

    // *** Bounds check before indexing into raw ***
    if length > PAYLOAD_MAX { return None; }
    if raw.len() < 4 + length + 2 { return None; }

    let payload_slice = &raw[4..4 + length];
    let crc_bytes: [u8; 2] = raw[4 + length..4 + length + 2]
        .try_into()
        .ok()?;
    let received_crc = u16::from_be_bytes(crc_bytes);

    // Compute expected CRC over seq + len + payload
    let mut crc_input = heapless::Vec::<u8, { 2 + PAYLOAD_MAX }>::new();
    crc_input.push(seq).ok()?;
    crc_input.push(length as u8).ok()?;
    crc_input.extend_from_slice(payload_slice).ok()?;

    let expected_crc = crc16(&crc_input);
    if expected_crc != received_crc { return None; }

    let mut payload = heapless::Vec::new();
    payload.extend_from_slice(payload_slice).ok()?;

    Some(Frame { seq, payload })
}
```

---

### 3. Replay Protection with a Sliding Window

```rust
/// Replay-protection window for 8-bit sequence numbers.
pub struct ReplayWindow {
    last_seq: u8,
    bitmap:   u8,  // bit i set = seq (last_seq - i) was accepted
    window:   u8,  // window size (≤ 8 for u8 bitmap)
}

impl ReplayWindow {
    pub fn new(window_size: u8) -> Self {
        assert!(window_size <= 8);
        Self { last_seq: 0, bitmap: 0, window: window_size }
    }

    /// Returns true if the sequence number is fresh, and records it.
    pub fn check_and_update(&mut self, seq: u8) -> bool {
        let diff = seq.wrapping_sub(self.last_seq) as i8;

        if diff > 0 {
            // Forward: new highest
            if diff as u8 > self.window { return false; }
            self.bitmap = self.bitmap.wrapping_shl(diff as u32) | 1;
            self.last_seq = seq;
            true
        } else if diff == 0 {
            false // duplicate of last
        } else {
            let age = (-diff) as u8;
            if age >= self.window { return false; }
            let mask = 1u8 << age;
            if self.bitmap & mask != 0 { return false; }
            self.bitmap |= mask;
            true
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_in_order() {
        let mut w = ReplayWindow::new(8);
        assert!(w.check_and_update(1));
        assert!(w.check_and_update(2));
        assert!(w.check_and_update(3));
    }

    #[test]
    fn rejects_duplicate() {
        let mut w = ReplayWindow::new(8);
        assert!(w.check_and_update(5));
        assert!(!w.check_and_update(5));
    }

    #[test]
    fn rejects_old() {
        let mut w = ReplayWindow::new(8);
        assert!(w.check_and_update(10));
        assert!(!w.check_and_update(1)); // too old
    }
}
```

---

### 4. HMAC-SHA256 Authentication

```rust
use hmac::{Hmac, Mac};
use sha2::Sha256;

type HmacSha256 = Hmac<Sha256>;

const PSK: &[u8; 32] = b"REPLACE_WITH_32_BYTE_SECRET_KEY!";

/// Compute HMAC-SHA256 of the nonce using the pre-shared key.
pub fn compute_mac(nonce: &[u8]) -> [u8; 32] {
    let mut mac = HmacSha256::new_from_slice(PSK)
        .expect("HMAC can take key of any size");
    mac.update(nonce);
    mac.finalize().into_bytes().into()
}

/// Constant-time comparison to prevent timing side-channels.
pub fn verify_mac(nonce: &[u8], received_mac: &[u8; 32]) -> bool {
    let expected = compute_mac(nonce);
    // subtle::ConstantTimeEq ensures no early exit on mismatch
    use subtle::ConstantTimeEq;
    expected.ct_eq(received_mac).into()
}
```

---

### 5. Token Bucket Rate Limiter

```rust
/// Token-bucket rate limiter for UART frame acceptance.
pub struct TokenBucket {
    tokens:      u64,   // in microseconds (scaled)
    capacity:    u64,
    refill_rate: u64,   // tokens per microsecond
    last_refill: u64,   // last timestamp in microseconds
}

impl TokenBucket {
    /// capacity_frames: burst capacity; frames_per_sec: sustained rate
    pub fn new(capacity_frames: u32, frames_per_sec: u32) -> Self {
        let capacity = capacity_frames as u64 * 1_000_000;
        let refill_rate = frames_per_sec as u64; // 1 token = 1_000_000 µs units
        Self {
            tokens: capacity,
            capacity,
            refill_rate,
            last_refill: 0,
        }
    }

    /// Pass current time in microseconds; returns true if frame is allowed.
    pub fn try_consume(&mut self, now_us: u64) -> bool {
        let elapsed = now_us.saturating_sub(self.last_refill);
        self.last_refill = now_us;

        // Add tokens proportional to elapsed time
        let new_tokens = elapsed.saturating_mul(self.refill_rate);
        self.tokens = self.tokens.saturating_add(new_tokens).min(self.capacity);

        // Consume one frame's worth (1_000_000 units)
        if self.tokens >= 1_000_000 {
            self.tokens -= 1_000_000;
            true
        } else {
            false
        }
    }
}
```

---

### 6. Command Dispatcher (No String Injection)

```rust
type CmdHandler = fn(&str);

struct Command {
    name:    &'static str,
    handler: CmdHandler,
}

fn cmd_status(_args: &str) { /* send status response */ }
fn cmd_reset(_args: &str)  { /* perform reset */ }

static CMD_TABLE: &[Command] = &[
    Command { name: "STATUS", handler: cmd_status },
    Command { name: "RESET",  handler: cmd_reset  },
];

pub fn dispatch(input: &str) {
    let (name, args) = match input.find(' ') {
        Some(i) => (&input[..i], &input[i + 1..]),
        None    => (input, ""),
    };

    // Whitelist lookup – no dynamic eval, no shell, no format string
    match CMD_TABLE.iter().find(|c| c.name == name) {
        Some(cmd) => (cmd.handler)(args),
        None      => { /* log unknown command safely */ }
    }
}
```

---

### 7. Putting It All Together – Secure UART Receive Loop (Rust)

```rust
pub fn secure_uart_loop<R>(uart: &mut R, session: &mut Session)
where
    R: embedded_hal::serial::Read<u8>,
{
    let mut raw: [u8; 256] = [0u8; 256];
    let mut rate_limiter = TokenBucket::new(10, 20);  // burst 10, 20fps max
    let now_us = system_time_us();                     // platform-specific

    // 1. Rate limit
    if !rate_limiter.try_consume(now_us) {
        log_security_event("rate_limit_exceeded");
        return;
    }

    // 2. Receive raw bytes (bounded)
    let n = match uart_read_frame_bytes(uart, &mut raw) {
        Ok(n) => n,
        Err(_) => return,
    };

    // 3. Parse and CRC-check
    let frame = match parse_frame(&raw[..n]) {
        Some(f) => f,
        None => {
            log_security_event("bad_frame");
            return;
        }
    };

    // 4. Replay check
    if !session.replay_window.check_and_update(frame.seq) {
        log_security_event("replay_detected");
        return;
    }

    // 5. Authenticate (if not yet in authenticated state)
    if !session.authenticated {
        handle_auth_exchange(&frame, session);
        return;
    }

    // 6. Dispatch command safely
    if let Ok(cmd_str) = core::str::from_utf8(&frame.payload) {
        dispatch(cmd_str);
    }
}
```

---

## Summary

| Security Layer | C/C++ Approach | Rust Approach |
|---|---|---|
| **Input validation** | Explicit bounds checks before `memcpy`; whitelist bytes | Type-safe slices, `Result`; bounds checked by default |
| **Buffer overflow** | `strncpy`, size guards, never `gets`/`scanf %s` | Impossible by default; `heapless::Vec` for no-std |
| **Injection** | Whitelist command table; never `system(buf)` or `printf(buf)` | Same pattern; no `unsafe` format strings |
| **Frame integrity** | CRC-16 over header+payload | CRC-16; slice length enforced by type system |
| **Replay protection** | Sliding window bitmap on sequence number | Same; unit-tested with `#[test]` |
| **Authentication** | HMAC-SHA256 via mbedTLS; constant-time compare | `hmac` + `sha2` crates; `subtle::ConstantTimeEq` |
| **Rate limiting** | Token bucket in ISR-safe C struct | Token bucket as Rust struct with `saturating_add` |
| **Debug port** | Disable via option bytes / e-fuse at production | Same HW measures; `cfg(feature = "production")` to compile out debug handlers |
| **Format string** | Always use `printf("%s", buf)` not `printf(buf)` | Not applicable – Rust `format!` is type-checked at compile time |
| **Timing side-channels** | Constant-time compare for MAC | `subtle` crate `ConstantTimeEq` |

### Key Takeaways

1. **UART provides zero security by default** – authentication, integrity, and encryption must all be added at the application layer.
2. **Validate length before every copy** – buffer overflows remain the most exploited UART vulnerability.
3. **Use a whitelist command table** – never pass received strings to shells, `printf`, or format parsers without stripping.
4. **Add a sequence number + CRC/HMAC to every frame** – this defeats both data corruption and replay attacks.
5. **Rate-limit at the frame receiver** – prevents DoS against resource-constrained embedded targets.
6. **Disable debug UART in production builds** – a bootloader or shell left accessible in field devices is a permanent attack surface.
7. **Use constant-time comparisons for all MAC/password checks** – timing side-channels are exploitable even on slow embedded hardware.
8. **Rust's type system eliminates entire categories of bugs** – no null-pointer dereferences, no out-of-bounds indexing, no use-after-free; the remaining security work (auth, crypto, protocol design) is shared with C/C++.

---

*Document: UART Security Considerations — Part 50 of the UART Programming Reference Series*