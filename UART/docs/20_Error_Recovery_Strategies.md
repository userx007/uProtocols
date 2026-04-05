# 20. UART Error Recovery Strategies

**Structure (9 major sections):**

1. **Introduction** — Why UART needs a software recovery layer (hardware only provides raw flags)
2. **Error Types** — Hardware flags (framing, parity, overrun, break, noise) vs. protocol-level errors (CRC, timeout, sequence gaps) — with a transient/systematic/bursty classification
3. **Detection Mechanisms** — CRC-16/CCITT selection rationale, sequence numbers, framing delimiters, timeout watchdogs
4. **Recovery Architectures** — Receiver FSM diagram, ARQ variants comparison table (Stop-and-Wait, Go-Back-N, Selective Repeat), exponential back-off formula
5. **Retransmission Protocol** — Minimal frame structure with byte-stuffing, ACK/NAK frame layout, retry logic parameters
6. **C/C++ Implementation** — Full working code: CRC-16, error flags, receiver FSM, byte-stuffed encoder, Stop-and-Wait ARQ, periodic tick handler, ISR-level hardware error handler
7. **Rust Implementation** — Full `no_std` implementation: typed error enums, `FrameDecoder` state machine, `Transmitter` with exponential back-off, `Receiver` with event-driven API, integration example
8. **Advanced Strategies** — Sliding window Go-Back-N, auto-baudrate detection, link quality / BER monitoring, RS-485 half-duplex collision avoidance, watchdog integration
9. **Summary table** — Quick-reference of all key design decisions


> **Implementing robust error recovery and retransmission protocols**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Types of UART Errors](#types-of-uart-errors)
3. [Error Detection Mechanisms](#error-detection-mechanisms)
4. [Error Recovery Architectures](#error-recovery-architectures)
5. [Retransmission Protocols](#retransmission-protocols)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Strategies](#advanced-strategies)
9. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) communication is inherently susceptible to
transmission errors caused by electrical noise, timing mismatches, baudrate drift, buffer
overruns, and physical signal degradation. Unlike higher-level protocols such as USB or Ethernet
that include built-in error correction at the hardware level, UART provides only rudimentary
hardware error flags: **framing errors**, **parity errors**, **overrun errors**, and
**break conditions**.

Robust embedded systems therefore require a **software-level error recovery layer** that:

- Detects errors reliably (beyond raw UART hardware flags)
- Classifies errors by severity and type
- Implements appropriate recovery actions (retry, resync, reset)
- Provides a retransmission protocol with sequence numbering and acknowledgements
- Limits retry attempts to avoid livelock
- Exposes error statistics for diagnostics

This chapter describes the theory and practical implementation of such a recovery layer in
both **C/C++** (for bare-metal / RTOS environments) and **Rust** (using `embedded-hal` traits).

---

## Types of UART Errors

### 2.1 Hardware-Level Errors

| Error Type        | Cause                                                           | Hardware Flag      |
|-------------------|-----------------------------------------------------------------|--------------------|
| **Framing Error** | Stop bit not detected; baudrate mismatch or line noise         | `FE` / `FERR`      |
| **Parity Error**  | Parity bit mismatch; data corruption during transmission       | `PE` / `PERR`      |
| **Overrun Error** | Receiver FIFO full; software too slow to consume bytes         | `OE` / `OERR`      |
| **Break Detect**  | Line held low for longer than a full frame                     | `BD` / `BREAK`     |
| **Noise Error**   | Signal-level noise detected on line (some MCUs only)           | `NF` / `NOISE`     |

### 2.2 Protocol-Level Errors

These are not reported by hardware and must be detected in software:

- **Timeout** – Expected byte or frame not received within a deadline
- **Checksum / CRC mismatch** – Frame received intact but data content corrupted
- **Sequence number gap** – Packet received out of order or duplicated
- **Buffer overflow** – Application-level ring buffer full
- **Incomplete frame** – Partial packet received before timeout

### 2.3 Systematic vs. Transient Errors

| Category       | Description                                         | Recovery Strategy          |
|----------------|-----------------------------------------------------|----------------------------|
| **Transient**  | Single-bit flip, brief noise spike                  | Retry immediately (1–3×)   |
| **Systematic** | Persistent baudrate mismatch, hardware fault        | Reset link, alert operator |
| **Bursty**     | Electrical interference, adjacent switching noise   | Back-off and retry         |

---

## Error Detection Mechanisms

### 3.1 CRC-16 / CRC-32 Frame Checksums

The most reliable method for detecting data corruption. Appended to each transmitted frame
and verified by the receiver before processing payload bytes.

Common polynomials:
- **CRC-8 (0x07)** – Lightweight, suitable for short frames (<16 bytes)
- **CRC-16/CCITT (0x1021)** – Excellent for UART; detects all 1/2-bit errors in frames up to 32 Kbits
- **CRC-32 (0x04C11DB7)** – Maximum reliability for large frames

### 3.2 Sequence Numbers

Each transmitted frame carries a monotonically incrementing sequence number (8-bit wrapping
is typical for UART). The receiver:

- Detects **gaps** (missed packets)
- Detects **duplicates** (retransmitted packets accepted twice)
- Enables **selective retransmission** of specific lost frames

### 3.3 Framing Delimiters

Using special start/end bytes (e.g., HDLC-style `0x7E`) with byte-stuffing allows the
receiver to re-synchronize after a corrupted frame without discarding all subsequent data.

### 3.4 Timeout Watchdogs

Every inter-byte gap and inter-frame gap should be monitored with a software timer. If a
frame is not completed within the expected window, the partial frame is discarded and the
receiver resets to the idle state.

---

## Error Recovery Architectures

### 4.1 Receiver State Machine

A clean error-recovery design models the receiver as a finite-state machine (FSM):

```
         IDLE ──────────────────────────────────────────────────┐
           │ SOF received                                        │
           ▼                                                     │ Timeout / Framing error
        RECEIVING ──── Checksum ok ──► PROCESSING               │
           │                              │                      │
           │ Checksum fail                │ Dispatch payload     │
           ▼                              ▼                      │
        ERROR_HANDLE ◄──────────────── SEND_ACK/NAK ────────────┘
```

States:
- **IDLE** – Waiting for start-of-frame marker
- **RECEIVING** – Accumulating frame bytes, running CRC
- **PROCESSING** – CRC passed, decode and dispatch
- **SEND_ACK/NAK** – Send acknowledgement or negative-acknowledgement
- **ERROR_HANDLE** – Log error, increment counter, decide retry/reset

### 4.2 ARQ (Automatic Repeat reQuest) Variants

| Variant            | Description                                                   | Suitable For           |
|--------------------|---------------------------------------------------------------|------------------------|
| **Stop-and-Wait**  | Send one frame, wait for ACK before next                      | Simple, low-throughput |
| **Go-Back-N**      | Send window of N frames; on NAK retransmit from error frame   | Moderate throughput    |
| **Selective Repeat**| Retransmit only the NAK'd frame                              | High efficiency        |

For most embedded UART applications, **Stop-and-Wait ARQ** is sufficient and simplest to implement.

### 4.3 Exponential Back-off

When retries fail repeatedly (e.g., systematic noise), an exponential back-off prevents the
system from hammering a broken link:

```
delay_ms = base_delay × 2^(attempt_number)
         = 10ms, 20ms, 40ms, 80ms, 160ms …  (capped at max_delay)
```

---

## Retransmission Protocols

### 5.1 Frame Structure

A minimal reliable UART frame suitable for embedded use:

```
 ┌────────┬──────┬────────┬─────────────────────┬────────────┐
 │ SOF    │ SEQ  │ LEN    │ PAYLOAD (0–255 bytes)│ CRC-16     │
 │ 0x7E   │ 8-bit│ 8-bit  │                     │ 2 bytes LE │
 └────────┴──────┴────────┴─────────────────────┴────────────┘
```

- **SOF** (`0x7E`) – Start-of-frame; any `0x7E` inside payload is escaped as `0x7D 0x5E`
- **SEQ** – Sequence number, wraps at 255→0
- **LEN** – Payload length in bytes
- **PAYLOAD** – Application data (byte-stuffed)
- **CRC-16** – CRC over SEQ + LEN + PAYLOAD

### 5.2 ACK / NAK Frame

```
 ┌────────┬───────────┬──────────┬────────────┐
 │ SOF    │ TYPE      │ SEQ      │ CRC-16     │
 │ 0x7E   │ ACK=0x06  │ echo seq │ 2 bytes LE │
 │        │ NAK=0x15  │          │            │
 └────────┴───────────┴──────────┴────────────┘
```

### 5.3 Retry Logic

```
Max retries    = 3
Retry interval = 100 ms (or timeout from last TX)
On max retries exceeded → notify application, reset link state
```

---

## C/C++ Implementation

### 6.1 CRC-16/CCITT

```c
#include <stdint.h>
#include <stddef.h>

/**
 * Compute CRC-16/CCITT (polynomial 0x1021, initial value 0xFFFF).
 * Suitable for UART frame integrity checks.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}
```

---

### 6.2 Error Flags and Status Types

```c
#include <stdint.h>
#include <stdbool.h>

/* ── Hardware error flags (mirror MCU UART status register bits) ── */
#define UART_ERR_NONE       0x00U
#define UART_ERR_FRAMING    0x01U  /* Stop bit not detected         */
#define UART_ERR_PARITY     0x02U  /* Parity mismatch               */
#define UART_ERR_OVERRUN    0x04U  /* RX FIFO / buffer overflow      */
#define UART_ERR_BREAK      0x08U  /* Break condition on line        */
#define UART_ERR_NOISE      0x10U  /* Noise flag (NXP/STM specific)  */

/* ── Protocol-level error codes ── */
typedef enum {
    PROTO_OK            = 0,
    PROTO_ERR_CRC,          /* CRC mismatch on received frame        */
    PROTO_ERR_TIMEOUT,      /* Frame not completed within deadline   */
    PROTO_ERR_SEQ,          /* Unexpected sequence number            */
    PROTO_ERR_OVERRUN,      /* Application ring-buffer full          */
    PROTO_ERR_MAX_RETRY,    /* Retransmission limit reached          */
    PROTO_ERR_LINK_DOWN,    /* Persistent failure; link considered down */
} proto_error_t;

/* ── Error statistics counters ── */
typedef struct {
    uint32_t hw_framing;
    uint32_t hw_parity;
    uint32_t hw_overrun;
    uint32_t hw_noise;
    uint32_t crc_failures;
    uint32_t timeouts;
    uint32_t seq_errors;
    uint32_t retransmissions;
    uint32_t link_resets;
} uart_error_stats_t;
```

---

### 6.3 Frame Definition and Receiver State Machine

```c
#include <string.h>

#define UART_SOF            0x7EU
#define UART_ESC            0x7DU
#define UART_ESC_XOR        0x20U
#define UART_MAX_PAYLOAD    255U
#define UART_FRAME_OVERHEAD 5U    /* SOF + SEQ + LEN + CRC(2) */

#define UART_ACK_TYPE       0x06U
#define UART_NAK_TYPE       0x15U

#define UART_MAX_RETRIES    3
#define UART_RETRY_MS       100
#define UART_RX_TIMEOUT_MS  500

/* ── Transmit frame buffer ── */
typedef struct {
    uint8_t  seq;
    uint8_t  len;
    uint8_t  payload[UART_MAX_PAYLOAD];
    uint16_t crc;
} uart_frame_t;

/* ── Receiver FSM states ── */
typedef enum {
    RX_IDLE,
    RX_SEQ,
    RX_LEN,
    RX_PAYLOAD,
    RX_CRC_LO,
    RX_CRC_HI,
} rx_state_t;

/* ── Receiver context ── */
typedef struct {
    rx_state_t       state;
    uint8_t          seq_expected;
    uint8_t          rx_seq;
    uint8_t          rx_len;
    uint8_t          rx_buf[UART_MAX_PAYLOAD];
    uint16_t         rx_crc_recv;
    size_t           rx_pos;
    bool             escaping;
    uint32_t         last_byte_ms;   /* Timestamp of last received byte  */
    uart_error_stats_t stats;
} uart_rx_ctx_t;

/* ── Transmitter context ── */
typedef struct {
    uint8_t          tx_seq;
    uint8_t          retry_count;
    uint8_t          pending_buf[UART_MAX_PAYLOAD + UART_FRAME_OVERHEAD + 64];
    size_t           pending_len;
    bool             awaiting_ack;
    uint32_t         tx_timestamp_ms;
} uart_tx_ctx_t;

/* ── Platform abstraction (implement per MCU / OS) ── */
extern void     uart_hw_send_bytes(const uint8_t *data, size_t len);
extern uint32_t uart_get_ms(void);           /* Millisecond tick counter */
extern void     app_frame_received(const uint8_t *payload, uint8_t len);
extern void     app_error_notify(proto_error_t err);
```

---

### 6.4 Frame Encoding (Transmitter with Byte-Stuffing)

```c
/**
 * Encode and transmit a frame over UART with byte-stuffing.
 * Stores a copy in ctx->pending_buf for potential retransmission.
 *
 * @param tx     Transmitter context
 * @param payload  Application data to send
 * @param len      Payload length (0–255)
 */
void uart_send_frame(uart_tx_ctx_t *tx, const uint8_t *payload, uint8_t len)
{
    uint8_t   raw[3 + UART_MAX_PAYLOAD]; /* SEQ + LEN + PAYLOAD (pre-CRC) */
    uint8_t   out[2 * (UART_MAX_PAYLOAD + UART_FRAME_OVERHEAD) + 2];
    size_t    out_idx = 0;

    /* Build raw buffer for CRC calculation */
    raw[0] = tx->tx_seq;
    raw[1] = len;
    memcpy(&raw[2], payload, len);
    uint16_t crc = crc16_ccitt(raw, 2U + len);

    /* Helper: emit a byte with byte-stuffing */
    #define STUFF_BYTE(b)                          \
        do {                                       \
            uint8_t _b = (b);                      \
            if (_b == UART_SOF || _b == UART_ESC) {\
                out[out_idx++] = UART_ESC;         \
                out[out_idx++] = _b ^ UART_ESC_XOR;\
            } else {                               \
                out[out_idx++] = _b;               \
            }                                      \
        } while (0)

    out[out_idx++] = UART_SOF;       /* Start-of-frame (never stuffed) */
    STUFF_BYTE(raw[0]);              /* SEQ  */
    STUFF_BYTE(raw[1]);              /* LEN  */
    for (uint8_t i = 0; i < len; i++)
        STUFF_BYTE(payload[i]);      /* PAYLOAD */
    STUFF_BYTE((uint8_t)(crc & 0xFF));        /* CRC lo */
    STUFF_BYTE((uint8_t)((crc >> 8) & 0xFF)); /* CRC hi */

    #undef STUFF_BYTE

    /* Cache frame for retransmission */
    memcpy(tx->pending_buf, out, out_idx);
    tx->pending_len      = out_idx;
    tx->awaiting_ack     = true;
    tx->tx_timestamp_ms  = uart_get_ms();
    tx->retry_count      = 0;

    uart_hw_send_bytes(out, out_idx);
}

/**
 * Retransmit the last cached frame (called on NAK or timeout).
 * Returns false when retry limit exceeded.
 */
bool uart_retransmit(uart_tx_ctx_t *tx)
{
    if (tx->retry_count >= UART_MAX_RETRIES) {
        tx->awaiting_ack = false;
        app_error_notify(PROTO_ERR_MAX_RETRY);
        return false;
    }

    /* Exponential back-off: wait 2^retry × base_ms */
    uint32_t back_off_ms = UART_RETRY_MS * (1U << tx->retry_count);
    uint32_t now = uart_get_ms();
    if ((now - tx->tx_timestamp_ms) < back_off_ms)
        return true;   /* Not yet time to retry */

    tx->retry_count++;
    tx->tx_timestamp_ms = now;
    uart_hw_send_bytes(tx->pending_buf, tx->pending_len);
    return true;
}
```

---

### 6.5 Frame Decoding (Receiver FSM)

```c
/**
 * Feed one received byte into the receiver FSM.
 * Call from UART RX ISR or polling loop.
 */
void uart_rx_byte(uart_rx_ctx_t *rx, uart_tx_ctx_t *tx, uint8_t byte)
{
    rx->last_byte_ms = uart_get_ms();

    /* ── Handle escape sequences ── */
    if (rx->escaping) {
        byte ^= UART_ESC_XOR;
        rx->escaping = false;
    } else if (byte == UART_ESC) {
        rx->escaping = true;
        return;
    } else if (byte == UART_SOF) {
        /* SOF resets the FSM (re-sync after error) */
        rx->state   = RX_SEQ;
        rx->rx_pos  = 0;
        rx->escaping = false;
        return;
    }

    switch (rx->state) {
    /* ─────────────────────────────── */
    case RX_IDLE:
        break;   /* Waiting for SOF (handled above) */

    /* ─────────────────────────────── */
    case RX_SEQ:
        rx->rx_seq = byte;
        rx->state  = RX_LEN;
        break;

    /* ─────────────────────────────── */
    case RX_LEN:
        rx->rx_len = byte;
        rx->rx_pos = 0;
        rx->state  = (byte > 0) ? RX_PAYLOAD : RX_CRC_LO;
        break;

    /* ─────────────────────────────── */
    case RX_PAYLOAD:
        rx->rx_buf[rx->rx_pos++] = byte;
        if (rx->rx_pos >= rx->rx_len)
            rx->state = RX_CRC_LO;
        break;

    /* ─────────────────────────────── */
    case RX_CRC_LO:
        rx->rx_crc_recv = (uint16_t)byte;
        rx->state       = RX_CRC_HI;
        break;

    /* ─────────────────────────────── */
    case RX_CRC_HI: {
        rx->rx_crc_recv |= (uint16_t)byte << 8;
        rx->state        = RX_IDLE;

        /* ── Verify CRC ── */
        uint8_t crc_buf[2 + UART_MAX_PAYLOAD];
        crc_buf[0] = rx->rx_seq;
        crc_buf[1] = rx->rx_len;
        memcpy(&crc_buf[2], rx->rx_buf, rx->rx_len);
        uint16_t crc_calc = crc16_ccitt(crc_buf, 2U + rx->rx_len);

        if (crc_calc != rx->rx_crc_recv) {
            rx->stats.crc_failures++;
            uart_send_nak(tx, rx->rx_seq);   /* Request retransmission */
            break;
        }

        /* ── Check for ACK/NAK control frames ── */
        if (rx->rx_len == 1) {
            if (rx->rx_buf[0] == UART_ACK_TYPE) {
                /* ACK: confirm TX and advance sequence */
                if (tx->awaiting_ack && tx->tx_seq == rx->rx_seq) {
                    tx->awaiting_ack = false;
                    tx->tx_seq++;            /* Advance TX sequence */
                }
                break;
            } else if (rx->rx_buf[0] == UART_NAK_TYPE) {
                /* NAK: retransmit */
                rx->stats.retransmissions++;
                uart_retransmit(tx);
                break;
            }
        }

        /* ── Sequence number check ── */
        if (rx->rx_seq != rx->rx_seq_expected) {
            rx->stats.seq_errors++;
            /* Duplicate: re-ACK silently; gap: send NAK for missing */
            uart_send_ack(tx, rx->rx_seq);
            break;
        }

        /* ── Valid data frame ── */
        rx->seq_expected++;
        uart_send_ack(tx, rx->rx_seq);
        app_frame_received(rx->rx_buf, rx->rx_len);
        break;
    }

    default:
        rx->state = RX_IDLE;
        break;
    }
}

/* ── Send ACK ── */
void uart_send_ack(uart_tx_ctx_t *tx, uint8_t seq)
{
    uint8_t payload = UART_ACK_TYPE;
    uint8_t save_seq = tx->tx_seq;
    tx->tx_seq = seq;
    uart_send_frame(tx, &payload, 1);
    tx->tx_seq       = save_seq;
    tx->awaiting_ack = false;   /* ACK frames don't themselves need ACKing */
}

/* ── Send NAK ── */
void uart_send_nak(uart_tx_ctx_t *tx, uint8_t seq)
{
    uint8_t payload = UART_NAK_TYPE;
    uint8_t save_seq = tx->tx_seq;
    tx->tx_seq = seq;
    uart_send_frame(tx, &payload, 1);
    tx->tx_seq       = save_seq;
    tx->awaiting_ack = false;
}
```

---

### 6.6 Timeout and Periodic Tick Handler

```c
/**
 * Call this from a 10 ms periodic timer tick or main loop.
 * Handles RX inter-byte timeout and TX retry timeout.
 */
void uart_tick(uart_rx_ctx_t *rx, uart_tx_ctx_t *tx)
{
    uint32_t now = uart_get_ms();

    /* ── RX timeout: partial frame stuck in FSM ── */
    if (rx->state != RX_IDLE &&
        (now - rx->last_byte_ms) > UART_RX_TIMEOUT_MS)
    {
        rx->stats.timeouts++;
        rx->state    = RX_IDLE;
        rx->escaping = false;
        app_error_notify(PROTO_ERR_TIMEOUT);
    }

    /* ── TX retry: ACK not received within window ── */
    if (tx->awaiting_ack &&
        (now - tx->tx_timestamp_ms) > UART_RETRY_MS)
    {
        if (!uart_retransmit(tx)) {
            /* Max retries exceeded — link considered down */
            rx->stats.link_resets++;
            app_error_notify(PROTO_ERR_LINK_DOWN);
            uart_link_reset(rx, tx);
        }
    }
}

/**
 * Full link reset: clear all state, re-synchronize sequence numbers.
 */
void uart_link_reset(uart_rx_ctx_t *rx, uart_tx_ctx_t *tx)
{
    memset(rx, 0, sizeof(*rx));
    memset(tx, 0, sizeof(*tx));
    /* rx->stats intentionally NOT cleared — preserve diagnostics */
}
```

---

### 6.7 Hardware Error Flag Handler (ISR-Level)

```c
/**
 * Called from UART ISR when hardware error flags are set.
 * Clear hardware flags after reading to re-enable reception.
 *
 * @param hw_flags  Bitmask of UART_ERR_* flags from status register
 */
void uart_hw_error_handler(uart_rx_ctx_t *rx, uart_tx_ctx_t *tx,
                            uint8_t hw_flags)
{
    if (hw_flags & UART_ERR_FRAMING) {
        rx->stats.hw_framing++;
        rx->state = RX_IDLE;   /* Re-sync on framing error */
    }
    if (hw_flags & UART_ERR_PARITY)
        rx->stats.hw_parity++;
    if (hw_flags & UART_ERR_OVERRUN) {
        rx->stats.hw_overrun++;
        rx->state = RX_IDLE;
    }
    if (hw_flags & UART_ERR_NOISE)
        rx->stats.hw_noise++;

    /* Discard the corrupted byte — do not feed it to the FSM */
}
```

---

## Rust Implementation

### 7.1 Project Setup (`Cargo.toml`)

```toml
[package]
name    = "uart-recovery"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"
heapless     = "0.8"   # Fixed-capacity collections for no_std

[profile.release]
opt-level = "s"         # Size-optimised for embedded targets
```

---

### 7.2 Error Types

```rust
#![no_std]

/// Hardware-level UART error flags (mirrors MCU status register)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwErrorFlags(pub u8);

impl HwErrorFlags {
    pub const NONE:     Self = Self(0x00);
    pub const FRAMING:  Self = Self(0x01);
    pub const PARITY:   Self = Self(0x02);
    pub const OVERRUN:  Self = Self(0x04);
    pub const BREAK:    Self = Self(0x08);
    pub const NOISE:    Self = Self(0x10);

    pub fn contains(self, flag: Self) -> bool { self.0 & flag.0 != 0 }
    pub fn any(self) -> bool { self.0 != 0 }
}

/// Protocol-level error codes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProtoError {
    CrcMismatch,
    Timeout,
    SequenceError { expected: u8, got: u8 },
    BufferOverflow,
    MaxRetriesExceeded,
    LinkDown,
}

/// Combined UART error
#[derive(Debug, Clone, Copy)]
pub enum UartError {
    Hardware(HwErrorFlags),
    Protocol(ProtoError),
}

/// Error statistics counters
#[derive(Debug, Default)]
pub struct ErrorStats {
    pub hw_framing:      u32,
    pub hw_parity:       u32,
    pub hw_overrun:      u32,
    pub hw_noise:        u32,
    pub crc_failures:    u32,
    pub timeouts:        u32,
    pub seq_errors:      u32,
    pub retransmissions: u32,
    pub link_resets:     u32,
}
```

---

### 7.3 CRC-16/CCITT

```rust
/// Compute CRC-16/CCITT (poly 0x1021, init 0xFFFF).
pub fn crc16_ccitt(data: &[u8]) -> u16 {
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

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn crc_known_vector() {
        // "123456789" → 0x29B1 for CRC-16/CCITT
        let result = crc16_ccitt(b"123456789");
        assert_eq!(result, 0x29B1);
    }
}
```

---

### 7.4 Frame Codec (Encoder + Decoder)

```rust
use heapless::Vec;

pub const SOF: u8          = 0x7E;
pub const ESC: u8          = 0x7D;
pub const ESC_XOR: u8      = 0x20;
pub const ACK_TYPE: u8     = 0x06;
pub const NAK_TYPE: u8     = 0x15;
pub const MAX_PAYLOAD: usize = 255;
pub const ENCODED_CAPACITY: usize = 2 * (MAX_PAYLOAD + 5) + 2;

pub type EncodedFrame = Vec<u8, ENCODED_CAPACITY>;
pub type PayloadBuf   = Vec<u8, MAX_PAYLOAD>;

/// Encode a frame with byte-stuffing.
///
/// Frame layout (pre-stuffing): SOF | SEQ | LEN | PAYLOAD… | CRC_LO | CRC_HI
pub fn encode_frame(seq: u8, payload: &[u8]) -> Result<EncodedFrame, ()> {
    if payload.len() > MAX_PAYLOAD { return Err(()); }

    // Build CRC input
    let mut crc_input: Vec<u8, { MAX_PAYLOAD + 2 }> = Vec::new();
    crc_input.push(seq).map_err(|_| ())?;
    crc_input.push(payload.len() as u8).map_err(|_| ())?;
    crc_input.extend_from_slice(payload).map_err(|_| ())?;
    let crc = crc16_ccitt(&crc_input);

    let mut out = EncodedFrame::new();
    out.push(SOF).map_err(|_| ())?;

    let stuff = |out: &mut EncodedFrame, byte: u8| -> Result<(), ()> {
        if byte == SOF || byte == ESC {
            out.push(ESC).map_err(|_| ())?;
            out.push(byte ^ ESC_XOR).map_err(|_| ())?;
        } else {
            out.push(byte).map_err(|_| ())?;
        }
        Ok(())
    };

    stuff(&mut out, seq)?;
    stuff(&mut out, payload.len() as u8)?;
    for &b in payload { stuff(&mut out, b)?; }
    stuff(&mut out, (crc & 0xFF) as u8)?;
    stuff(&mut out, (crc >> 8) as u8)?;

    Ok(out)
}

// ── Decoder state machine ───────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq)]
enum RxState {
    Idle,
    Seq,
    Len,
    Payload,
    CrcLo,
    CrcHi,
}

pub struct FrameDecoder {
    state:      RxState,
    seq:        u8,
    len:        u8,
    buf:        PayloadBuf,
    crc_recv:   u16,
    escaping:   bool,
}

pub enum DecodeResult {
    /// Frame fully received and CRC verified
    Complete { seq: u8, payload: PayloadBuf },
    /// Frame received but CRC mismatch
    CrcError { seq: u8 },
    /// Not yet complete — keep feeding bytes
    Incomplete,
    /// SOF received mid-frame — decoder resynced
    Resynced,
}

impl FrameDecoder {
    pub const fn new() -> Self {
        Self {
            state:    RxState::Idle,
            seq:      0,
            len:      0,
            buf:      PayloadBuf::new(),
            crc_recv: 0,
            escaping: false,
        }
    }

    /// Feed one received byte; returns the decode outcome.
    pub fn feed(&mut self, mut byte: u8) -> DecodeResult {
        // ── Escape handling ──
        if self.escaping {
            byte ^= ESC_XOR;
            self.escaping = false;
        } else if byte == ESC {
            self.escaping = true;
            return DecodeResult::Incomplete;
        } else if byte == SOF {
            self.reset_partial();
            self.state = RxState::Seq;
            return DecodeResult::Resynced;
        }

        match self.state {
            RxState::Idle => DecodeResult::Incomplete,

            RxState::Seq => {
                self.seq   = byte;
                self.state = RxState::Len;
                DecodeResult::Incomplete
            }

            RxState::Len => {
                self.len   = byte;
                self.buf.clear();
                self.state = if byte == 0 { RxState::CrcLo } else { RxState::Payload };
                DecodeResult::Incomplete
            }

            RxState::Payload => {
                let _ = self.buf.push(byte);
                if self.buf.len() >= self.len as usize {
                    self.state = RxState::CrcLo;
                }
                DecodeResult::Incomplete
            }

            RxState::CrcLo => {
                self.crc_recv = byte as u16;
                self.state    = RxState::CrcHi;
                DecodeResult::Incomplete
            }

            RxState::CrcHi => {
                self.crc_recv |= (byte as u16) << 8;
                self.state = RxState::Idle;
                self.verify_and_emit()
            }
        }
    }

    fn verify_and_emit(&mut self) -> DecodeResult {
        let mut crc_input: Vec<u8, { MAX_PAYLOAD + 2 }> = Vec::new();
        let _ = crc_input.push(self.seq);
        let _ = crc_input.push(self.len);
        let _ = crc_input.extend_from_slice(&self.buf);

        let crc_calc = crc16_ccitt(&crc_input);
        let seq = self.seq;

        if crc_calc == self.crc_recv {
            let payload = self.buf.clone();
            self.buf.clear();
            DecodeResult::Complete { seq, payload }
        } else {
            DecodeResult::CrcError { seq }
        }
    }

    fn reset_partial(&mut self) {
        self.state    = RxState::Idle;
        self.escaping = false;
        self.buf.clear();
    }
}
```

---

### 7.5 Stop-and-Wait ARQ Transmitter

```rust
use core::time::Duration;

pub const MAX_RETRIES: u8       = 3;
pub const BASE_RETRY_MS: u64    = 100;
pub const ACK_TIMEOUT_MS: u64   = 500;

pub struct Transmitter {
    seq:             u8,
    retry_count:     u8,
    pending:         Option<(u8, EncodedFrame)>, // (seq, encoded_bytes)
    tx_timestamp_ms: u64,
    pub stats:       ErrorStats,
}

impl Transmitter {
    pub const fn new() -> Self {
        Self {
            seq:             0,
            retry_count:     0,
            pending:         None,
            tx_timestamp_ms: 0,
            stats:           ErrorStats {
                hw_framing:      0,
                hw_parity:       0,
                hw_overrun:      0,
                hw_noise:        0,
                crc_failures:    0,
                timeouts:        0,
                seq_errors:      0,
                retransmissions: 0,
                link_resets:     0,
            },
        }
    }

    /// Begin a new transmission. Returns the encoded frame bytes to send,
    /// or `Err(())` if a previous frame is still pending.
    pub fn send(
        &mut self,
        payload: &[u8],
        now_ms: u64,
    ) -> Result<EncodedFrame, ()> {
        if self.pending.is_some() { return Err(()); } // Busy

        let frame = encode_frame(self.seq, payload)?;
        self.pending         = Some((self.seq, frame.clone()));
        self.tx_timestamp_ms = now_ms;
        self.retry_count     = 0;
        Ok(frame)
    }

    /// Process a received ACK. Returns true if it matches the pending frame.
    pub fn on_ack(&mut self, ack_seq: u8) -> bool {
        if let Some((seq, _)) = &self.pending {
            if *seq == ack_seq {
                self.pending = None;
                self.seq     = self.seq.wrapping_add(1);
                return true;
            }
        }
        false
    }

    /// Called on NAK. Returns the frame to retransmit, or an error if
    /// max retries have been exceeded.
    pub fn on_nak(&mut self, now_ms: u64) -> Result<Option<EncodedFrame>, ProtoError> {
        self.stats.retransmissions += 1;
        self.retry_inner(now_ms)
    }

    /// Periodic tick — checks for ACK timeout with exponential back-off.
    /// Returns `Ok(Some(frame))` when it's time to retry, `Ok(None)` when
    /// waiting, or `Err` when max retries are exceeded.
    pub fn tick(&mut self, now_ms: u64) -> Result<Option<EncodedFrame>, ProtoError> {
        if self.pending.is_none() { return Ok(None); }

        let back_off = BASE_RETRY_MS * (1u64 << self.retry_count);
        if now_ms.saturating_sub(self.tx_timestamp_ms) >= back_off {
            self.retry_inner(now_ms)
        } else {
            Ok(None)
        }
    }

    fn retry_inner(&mut self, now_ms: u64) -> Result<Option<EncodedFrame>, ProtoError> {
        if self.retry_count >= MAX_RETRIES {
            self.pending     = None;
            return Err(ProtoError::MaxRetriesExceeded);
        }
        self.retry_count    += 1;
        self.tx_timestamp_ms = now_ms;
        Ok(self.pending.as_ref().map(|(_, f)| f.clone()))
    }

    pub fn is_idle(&self) -> bool { self.pending.is_none() }
}
```

---

### 7.6 Full Receiver with Error Recovery

```rust
pub struct Receiver {
    decoder:       FrameDecoder,
    seq_expected:  u8,
    last_byte_ms:  u64,
    pub stats:     ErrorStats,
}

pub enum RxEvent<'a> {
    /// Data frame ready for application
    Data { seq: u8, payload: &'a [u8] },
    /// Send this ACK frame
    SendAck(EncodedFrame),
    /// Send this NAK frame
    SendNak(EncodedFrame),
    /// Nothing actionable yet
    Pending,
    /// Error occurred (stats already updated)
    Error(UartError),
}

impl Receiver {
    pub const fn new() -> Self {
        Self {
            decoder:      FrameDecoder::new(),
            seq_expected: 0,
            last_byte_ms: 0,
            stats:        ErrorStats { /* all zeroes */ .. unsafe { core::mem::zeroed() } },
        }
    }

    /// Feed one byte (from ISR or poll loop).
    pub fn feed_byte(
        &mut self,
        byte: u8,
        now_ms: u64,
        tx: &mut Transmitter,
    ) -> RxEvent {
        self.last_byte_ms = now_ms;

        match self.decoder.feed(byte) {
            DecodeResult::Incomplete | DecodeResult::Resynced => RxEvent::Pending,

            DecodeResult::CrcError { seq } => {
                self.stats.crc_failures += 1;
                // Request retransmission via NAK
                if let Ok(nak) = encode_frame(seq, &[NAK_TYPE]) {
                    RxEvent::SendNak(nak)
                } else {
                    RxEvent::Error(UartError::Protocol(ProtoError::CrcMismatch))
                }
            }

            DecodeResult::Complete { seq, payload } => {
                // Check if this is an ACK/NAK control frame
                if payload.len() == 1 {
                    match payload[0] {
                        ACK_TYPE => {
                            tx.on_ack(seq);
                            return RxEvent::Pending;
                        }
                        NAK_TYPE => {
                            match tx.on_nak(now_ms) {
                                Ok(Some(frame)) => return RxEvent::SendNak(frame),
                                Err(e) => return RxEvent::Error(UartError::Protocol(e)),
                                _ => return RxEvent::Pending,
                            }
                        }
                        _ => {}
                    }
                }

                // Sequence number validation
                if seq != self.seq_expected {
                    self.stats.seq_errors += 1;
                    // Re-ACK duplicates; for gaps we'd need a more complex protocol
                    let ack = encode_frame(seq, &[ACK_TYPE]).unwrap_or_default();
                    return RxEvent::SendAck(ack);
                }

                self.seq_expected = self.seq_expected.wrapping_add(1);
                let ack = encode_frame(seq, &[ACK_TYPE]).unwrap_or_default();

                // NOTE: In a real system, deliver `payload` to app here.
                // Returning SendAck; application checks for data via separate queue.
                RxEvent::SendAck(ack)
            }
        }
    }

    /// Periodic tick — detect inter-frame timeout.
    pub fn tick(&mut self, now_ms: u64, timeout_ms: u64) -> Option<UartError> {
        if self.decoder.state != RxState::Idle
            && now_ms.saturating_sub(self.last_byte_ms) > timeout_ms
        {
            self.stats.timeouts += 1;
            self.decoder.reset_partial();
            Some(UartError::Protocol(ProtoError::Timeout))
        } else {
            None
        }
    }

    /// Handle hardware error flags from ISR.
    pub fn on_hw_error(&mut self, flags: HwErrorFlags) {
        if flags.contains(HwErrorFlags::FRAMING) {
            self.stats.hw_framing += 1;
            self.decoder.reset_partial();
        }
        if flags.contains(HwErrorFlags::PARITY)  { self.stats.hw_parity  += 1; }
        if flags.contains(HwErrorFlags::OVERRUN) {
            self.stats.hw_overrun += 1;
            self.decoder.reset_partial();
        }
        if flags.contains(HwErrorFlags::NOISE)   { self.stats.hw_noise   += 1; }
    }
}
```

---

### 7.7 Integration Example (RTOS / bare-metal)

```rust
// Pseudo-code showing integration in a main loop / task

fn uart_task(uart: &mut impl embedded_hal::serial::Write<u8>) {
    let mut rx = Receiver::new();
    let mut tx = Transmitter::new();
    let mut now_ms: u64 = 0; // Provided by SysTick / RTOS

    // ── Send a frame ──────────────────────────────────────────
    let payload = b"Hello, UART!";
    if let Ok(frame) = tx.send(payload, now_ms) {
        for byte in &frame {
            let _ = uart.write(*byte); // Non-blocking; buffer in DMA/FIFO
        }
    }

    // ── Main loop ────────────────────────────────────────────
    loop {
        now_ms += 1; // Simulated tick

        // Process incoming bytes
        while let Ok(byte) = uart.read() {
            match rx.feed_byte(byte, now_ms, &mut tx) {
                RxEvent::SendAck(ack) | RxEvent::SendNak(ack) => {
                    for b in &ack { let _ = uart.write(*b); }
                }
                RxEvent::Error(e) => {
                    // Log or handle error
                    let _ = e; // suppress warning
                }
                _ => {}
            }
        }

        // Handle TX retransmission / timeout
        match tx.tick(now_ms) {
            Ok(Some(frame)) => {
                for b in &frame { let _ = uart.write(*b); }
            }
            Err(ProtoError::MaxRetriesExceeded) => {
                // Notify application of link failure
            }
            _ => {}
        }

        // Check RX timeout
        if let Some(_err) = rx.tick(now_ms, 500) {
            // Frame timeout — RX state reset automatically
        }
    }
}
```

---

## Advanced Strategies

### 8.1 Sliding Window Protocol (Go-Back-N)

For higher-throughput UART channels (e.g., 921600 baud), Stop-and-Wait wastes bandwidth
waiting for each ACK. A **window size W** allows W frames in-flight simultaneously:

```
TX: [SEQ=0] [SEQ=1] [SEQ=2]  →→→
                    ← ACK(0)
              [SEQ=3] →→→
                    ← NAK(1)  ← retransmit from 1
              [SEQ=1][SEQ=2][SEQ=3] →→→
```

Implementation requires:
- A circular send buffer of W frames (typically W = 4 or 8 for embedded UART)
- Per-frame timeout timers
- Receiver-side reordering buffer (for Selective Repeat)

### 8.2 Automatic Baudrate Detection

When the remote baudrate is unknown, transmit a known sync sequence (`0x55 0x55 0x55`)
and measure pulse widths to calculate the baudrate:

```c
/* Measure time between transitions on RX line to auto-detect baud */
uint32_t pulse_us = capture_first_transition_us();
uint32_t baud     = 1000000UL / pulse_us;  /* Nearest standard rate */
```

### 8.3 Link Quality Monitoring

Track rolling error rates to adapt behavior dynamically:

```c
typedef struct {
    uint32_t frames_ok;
    uint32_t frames_err;
} link_quality_t;

float uart_ber(const link_quality_t *q) {
    uint32_t total = q->frames_ok + q->frames_err;
    return (total > 0) ? (float)q->frames_err / (float)total : 0.0f;
}
/* BER > 0.05 → reduce baudrate or alert operator */
```

### 8.4 Half-Duplex Collision Avoidance

On RS-485 half-duplex buses, a simple CSMA strategy prevents collisions:

```c
/* Before transmitting: wait for RX line to be idle for ≥ 2 frame times */
while (!rx_line_idle_for(2 * frame_duration_ms()))
    yield();

/* Enable TX driver, send frame, disable driver */
rs485_de_assert(true);
uart_hw_send_bytes(frame, len);
uart_wait_tx_complete();
rs485_de_assert(false);
```

### 8.5 Watchdog Integration

For safety-critical embedded systems, integrate a hardware watchdog that is only petted
when a valid frame is received within the expected interval:

```c
void app_frame_received(const uint8_t *payload, uint8_t len) {
    watchdog_kick();   /* Reset HW watchdog — comms link is healthy */
    process_command(payload, len);
}
```

---

## Summary

| Aspect                    | Key Points                                                                      |
|---------------------------|---------------------------------------------------------------------------------|
| **Error Detection**       | Use CRC-16/CCITT per frame; never rely on parity alone                          |
| **Frame Synchronization** | SOF byte + byte-stuffing enables reliable resync after any corruption           |
| **Sequence Numbers**      | 8-bit wrapping counter detects gaps and duplicates                              |
| **ACK/NAK**               | Explicit positive/negative acknowledgements drive retransmission logic          |
| **Retry Strategy**        | Stop-and-Wait ARQ with exponential back-off; max 3 retries before link reset   |
| **Timeouts**              | Guard inter-byte and inter-frame gaps; reset FSM on expiry                     |
| **Hardware Errors**       | Clear framing/overrun flags in ISR; reset RX FSM; track in statistics          |
| **Diagnostics**           | Maintain error counters; expose via debug interface or telemetry                |
| **Rust vs C**             | Rust's type system enforces error handling at compile time; C is simpler for ISR-heavy ports |
| **Scalability**           | Upgrade to sliding window ARQ when throughput requirements exceed Stop-and-Wait |

A well-implemented UART error recovery layer transforms an unreliable byte stream into a
dependable messaging channel suitable for safety-relevant embedded applications — from
industrial sensor networks to automotive ECU communications.

---

*End of Chapter 20 — UART Error Recovery Strategies*