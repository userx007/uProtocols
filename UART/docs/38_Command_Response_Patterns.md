# 38. Command-Response Patterns — Building Request-Reply Protocols over UART

**Protocol Design** — A 6-field binary frame format (start byte, sequence number, command code, length, payload, CRC-16) with a clear rationale for each field. The response flag (`0x80`) in the command byte allows the initiator to distinguish responses from new commands without extra state.

**CRC-16 (XMODEM)** — Full implementations in both languages using the standard `0x1021` polynomial with `0xFFFF` init. The Rust version includes a test against the known "123456789" → `0x29B1` vector.

**C/C++ Implementation** — Four files: the header with all types and the driver callback struct, a CRC module, a frame encoder/decoder with a byte-level state machine, and a high-level `uart_transaction()` with exponential back-off retry. The platform is abstracted via function pointers (`send`, `recv_byte`, `delay_ms`), making the core code portable across bare-metal MCUs, RTOS tasks, and POSIX.

**Rust Implementation** — Uses a `UartTransport` trait for the hardware backend, a typed `enum` for all error variants (with `Display`), and a clean `UartMaster<T>` generic struct. The same state-machine decoder is expressed with a proper `enum ParseState`.

**Responder Side** — Both languages include a command dispatcher (MCU firmware side) with an optional idempotency cache to handle retried commands without double-execution.

**Advanced Patterns** — Unsolicited events, sliding window pipelining, AT-style text/binary bootstrap mode, and RS-485 multi-drop addressing are all described with frame layout diagrams.

**Summary Table** — Maps each reliability concern to its concrete solution in the protocol.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Protocol Frame Design](#protocol-frame-design)
4. [Timeout and Retry Strategies](#timeout-and-retry-strategies)
5. [Implementation in C/C++](#implementation-in-cc)
6. [Implementation in Rust](#implementation-in-rust)
7. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
8. [Advanced Patterns](#advanced-patterns)
9. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is a point-to-point serial communication protocol
that transmits data byte-by-byte with no built-in framing, addressing, or acknowledgement mechanism.
When building a reliable system on top of raw UART, a **Command-Response Pattern** provides structure:
one side (the *master* or *initiator*) sends a command frame; the other side (the *slave* or *responder*)
processes it and sends back a response frame within a defined time window.

This pattern is the foundation of many embedded protocols — AT-command modems, MODBUS RTU, custom sensor
networks, bootloader interfaces, and host-to-MCU bridges all use variants of it.

### When to Use This Pattern

- A host PC or MCU needs to control a peripheral (motor driver, sensor module, display).
- A bidirectional query-response flow is required (read a register, write a value, execute a command).
- Reliable delivery must be confirmed at the application layer (UART itself provides no ACK).
- Multiple logical operations must be serialized over a single physical wire pair.

---

## Core Concepts

### The Transaction Model

Every interaction is a **transaction** consisting of exactly two frames:

```
Initiator                         Responder
    |                                 |
    |──── Command Frame ─────────────>|
    |                                 |  (processes command)
    |<─── Response Frame ─────────────|
    |                                 |
```

Key invariants:
- Only one transaction is in-flight at any time (half-duplex logical model, even over full-duplex UART).
- The initiator starts a timer when the command is sent; if no response arrives before the deadline, the
  transaction is retried or declared failed.
- The responder never sends unsolicited responses (in the basic pattern — see Advanced Patterns for
  event-driven extensions).

### Sequencing and Correlation

To correlate a response with the command that triggered it, frames typically carry:

- A **Sequence Number** (e.g., 0–255, wrapping) echoed back by the responder.
- A **Command/Response Code** pair so the initiator can verify the response type.

---

## Protocol Frame Design

A well-designed frame has five regions:

```
┌─────────┬──────┬────────┬──────────────────┬─────────┐
│  Start  │  Seq │  Cmd   │    Payload       │   CRC   │
│  Byte   │  Nr  │  Code  │  (0..N bytes)    │ 2 bytes │
│  0xAA   │ 1 B  │  1 B   │  Len + Data      │ CRC-16  │
└─────────┴──────┴────────┴──────────────────┴─────────┘
```

| Field       | Size     | Purpose                                                   |
|-------------|----------|-----------------------------------------------------------|
| Start Byte  | 1 byte   | Framing sync marker (`0xAA` or any non-ambiguous value)   |
| Seq Nr      | 1 byte   | Transaction sequence number (0–255, wrapping)             |
| Cmd Code    | 1 byte   | Identifies the operation; high bit set = response         |
| Length      | 1 byte   | Number of payload data bytes that follow                  |
| Payload     | 0–N bytes| Command arguments or response data                        |
| CRC-16      | 2 bytes  | CRC over Seq+Cmd+Len+Payload (little-endian)              |

**Response frames** use `Cmd Code | 0x80` so the initiator can distinguish a response from a new command
in the byte stream.

**Status byte** (first byte of the response payload by convention): `0x00` = OK, non-zero = error code.

### CRC-16 (CRC-CCITT / XMODEM variant)

```
Polynomial : 0x1021
Initial    : 0xFFFF
Input/Output reflection: No
Final XOR  : 0x0000
```

---

## Timeout and Retry Strategies

```
send_command()
    │
    ├─► start timer T1
    │
    │   ┌─────────────────────────────────┐
    │   │ wait for response byte-by-byte  │
    │   │ parse frame as bytes arrive     │
    │   └─────────────────────────────────┘
    │           │               │
    │       frame OK        T1 expires
    │           │               │
    │       return OK       retry_count -= 1
    │                           │
    │                   ┌───────┴───────┐
    │                retry > 0       retry == 0
    │                   │               │
    │               re-send          return TIMEOUT
    │               command          error
```

Typical timeout values:

| Baud Rate  | Per-byte time | Suggested T1 (small frame) |
|------------|---------------|-----------------------------|
| 9600       | ~1.04 ms      | 150 ms                      |
| 115200     | ~87 µs        | 20 ms                       |
| 921600     | ~11 µs        | 5 ms                        |

Use **exponential back-off** between retries to avoid flooding a struggling responder.

---

## Implementation in C/C++

### Header — Protocol Definitions (`uart_cmd.h`)

```c
#ifndef UART_CMD_H
#define UART_CMD_H

#include <stdint.h>
#include <stdbool.h>

/* ── Frame constants ─────────────────────────────────────────────────────── */
#define FRAME_START         0xAA
#define FRAME_RESP_FLAG     0x80   /* OR'd onto cmd_code in responses        */
#define FRAME_MAX_PAYLOAD   64
#define FRAME_OVERHEAD      6      /* start + seq + cmd + len + crc(2)       */
#define FRAME_MAX_TOTAL     (FRAME_OVERHEAD + FRAME_MAX_PAYLOAD)

/* ── Command codes ───────────────────────────────────────────────────────── */
typedef enum {
    CMD_PING        = 0x01,
    CMD_READ_REG    = 0x02,
    CMD_WRITE_REG   = 0x03,
    CMD_RESET       = 0x04,
    CMD_GET_VERSION = 0x05,
} uart_cmd_code_t;

/* ── Status codes (first byte of response payload) ───────────────────────── */
typedef enum {
    STATUS_OK           = 0x00,
    STATUS_ERR_UNKNOWN  = 0x01,
    STATUS_ERR_PARAM    = 0x02,
    STATUS_ERR_BUSY     = 0x03,
    STATUS_ERR_CRC      = 0x04,
} uart_status_t;

/* ── Frame structure ─────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  seq;
    uint8_t  cmd_code;
    uint8_t  payload_len;
    uint8_t  payload[FRAME_MAX_PAYLOAD];
    uint16_t crc;
} uart_frame_t;

/* ── Transaction result ──────────────────────────────────────────────────── */
typedef enum {
    TRANS_OK            =  0,
    TRANS_ERR_TIMEOUT   = -1,
    TRANS_ERR_CRC       = -2,
    TRANS_ERR_SEQ       = -3,
    TRANS_ERR_STATUS    = -4,
    TRANS_ERR_OVERFLOW  = -5,
} trans_result_t;

/* ── Driver context (platform-specific callbacks) ───────────────────────── */
typedef struct {
    /* Send raw bytes; returns bytes sent or <0 on error */
    int  (*send)(const uint8_t *buf, uint16_t len, void *ctx);
    /* Receive one byte; timeout_ms == 0 → non-blocking. Returns 1=ok, 0=timeout, <0=err */
    int  (*recv_byte)(uint8_t *byte, uint32_t timeout_ms, void *ctx);
    /* Millisecond tick (for back-off sleeps) */
    void (*delay_ms)(uint32_t ms, void *ctx);
    void *ctx;                       /* opaque platform data                 */
    uint32_t default_timeout_ms;     /* per-transaction timeout              */
    uint8_t  max_retries;            /* retry count before giving up         */
} uart_driver_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
trans_result_t uart_transaction(uart_driver_t   *drv,
                                uart_cmd_code_t  cmd,
                                const uint8_t   *tx_payload,
                                uint8_t          tx_len,
                                uart_frame_t    *response_out);

#endif /* UART_CMD_H */
```

### CRC-16 Implementation (`crc16.c`)

```c
#include <stdint.h>

/* CRC-CCITT (XMODEM) — polynomial 0x1021, init 0xFFFF */
uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

uint16_t crc16_buf(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--)
        crc = crc16_update(crc, *buf++);
    return crc;
}
```

### Frame Encoder / Decoder (`uart_cmd.c`)

```c
#include "uart_cmd.h"
#include <string.h>

extern uint16_t crc16_buf(const uint8_t *buf, uint16_t len);

/* ── Encode a frame into a flat byte buffer ─────────────────────────────── */
static uint16_t frame_encode(const uart_frame_t *f, uint8_t *buf, uint16_t buf_size)
{
    if ((uint16_t)(FRAME_OVERHEAD + f->payload_len) > buf_size)
        return 0;

    uint16_t i = 0;
    buf[i++] = FRAME_START;
    buf[i++] = f->seq;
    buf[i++] = f->cmd_code;
    buf[i++] = f->payload_len;
    memcpy(&buf[i], f->payload, f->payload_len);
    i += f->payload_len;

    /* CRC covers seq, cmd, len, payload */
    uint16_t crc = crc16_buf(&buf[1], 3 + f->payload_len);
    buf[i++] = (uint8_t)(crc & 0xFF);        /* little-endian              */
    buf[i++] = (uint8_t)((crc >> 8) & 0xFF);
    return i;
}

/* ── Receive and decode one frame from the wire ─────────────────────────── */
typedef enum {
    PARSE_START, PARSE_SEQ, PARSE_CMD, PARSE_LEN,
    PARSE_PAYLOAD, PARSE_CRC_LO, PARSE_CRC_HI
} parse_state_t;

static trans_result_t recv_frame(uart_driver_t *drv,
                                 uart_frame_t  *out,
                                 uint32_t       timeout_ms)
{
    parse_state_t state = PARSE_START;
    uint8_t       raw[FRAME_MAX_TOTAL];
    uint8_t       raw_len = 0;
    uint8_t       payload_idx = 0;
    uint8_t       byte;

    while (1) {
        int rc = drv->recv_byte(&byte, timeout_ms, drv->ctx);
        if (rc == 0)  return TRANS_ERR_TIMEOUT;
        if (rc < 0)   return TRANS_ERR_TIMEOUT;

        switch (state) {
        case PARSE_START:
            if (byte == FRAME_START) {
                raw[raw_len++] = byte;
                state = PARSE_SEQ;
            }
            break;
        case PARSE_SEQ:
            out->seq = byte;
            raw[raw_len++] = byte;
            state = PARSE_CMD;
            break;
        case PARSE_CMD:
            out->cmd_code = byte;
            raw[raw_len++] = byte;
            state = PARSE_LEN;
            break;
        case PARSE_LEN:
            if (byte > FRAME_MAX_PAYLOAD) {
                raw_len = 0; state = PARSE_START; /* oversize — resync */
                break;
            }
            out->payload_len = byte;
            raw[raw_len++] = byte;
            payload_idx = 0;
            state = (byte > 0) ? PARSE_PAYLOAD : PARSE_CRC_LO;
            break;
        case PARSE_PAYLOAD:
            out->payload[payload_idx++] = byte;
            raw[raw_len++] = byte;
            if (payload_idx >= out->payload_len)
                state = PARSE_CRC_LO;
            break;
        case PARSE_CRC_LO:
            out->crc = byte;               /* low byte */
            state = PARSE_CRC_HI;
            break;
        case PARSE_CRC_HI:
            out->crc |= (uint16_t)byte << 8;  /* high byte */
            /* Verify CRC over raw[1..1+3+payload_len] */
            uint16_t calc = crc16_buf(&raw[1], 3 + out->payload_len);
            if (calc != out->crc)
                return TRANS_ERR_CRC;
            return TRANS_OK;
        }
    }
}

/* ── High-level transaction with retry ──────────────────────────────────── */
static uint8_t s_seq = 0;   /* global sequence counter */

trans_result_t uart_transaction(uart_driver_t   *drv,
                                uart_cmd_code_t  cmd,
                                const uint8_t   *tx_payload,
                                uint8_t          tx_len,
                                uart_frame_t    *response_out)
{
    uint8_t tx_buf[FRAME_MAX_TOTAL];
    uart_frame_t tx_frame = {
        .seq         = s_seq,
        .cmd_code    = (uint8_t)cmd,
        .payload_len = tx_len,
    };
    if (tx_len > 0)
        memcpy(tx_frame.payload, tx_payload, tx_len);

    uint16_t tx_len_bytes = frame_encode(&tx_frame, tx_buf, sizeof(tx_buf));
    if (tx_len_bytes == 0)
        return TRANS_ERR_OVERFLOW;

    uint32_t backoff_ms = 5;

    for (uint8_t attempt = 0; attempt <= drv->max_retries; attempt++) {
        drv->send(tx_buf, tx_len_bytes, drv->ctx);

        trans_result_t rc = recv_frame(drv, response_out, drv->default_timeout_ms);

        if (rc == TRANS_OK) {
            /* Validate sequence number echo and response flag */
            if (response_out->seq != tx_frame.seq)
                return TRANS_ERR_SEQ;
            if ((response_out->cmd_code & ~FRAME_RESP_FLAG) != (uint8_t)cmd)
                return TRANS_ERR_SEQ;

            s_seq++;   /* advance only on confirmed success */

            if (response_out->payload_len > 0 &&
                response_out->payload[0] != STATUS_OK)
                return TRANS_ERR_STATUS;

            return TRANS_OK;
        }

        if (attempt < drv->max_retries && drv->delay_ms)
            drv->delay_ms(backoff_ms, drv->ctx);
        backoff_ms *= 2;   /* exponential back-off */
    }

    return TRANS_ERR_TIMEOUT;
}
```

### Responder Side (MCU firmware, `responder.c`)

```c
#include "uart_cmd.h"
#include <string.h>

extern uint16_t crc16_buf(const uint8_t *, uint16_t);

/* Build and send a response frame */
static void send_response(uart_driver_t   *drv,
                          uint8_t          seq,
                          uart_cmd_code_t  cmd,
                          uart_status_t    status,
                          const uint8_t   *data,
                          uint8_t          data_len)
{
    uart_frame_t resp = {
        .seq         = seq,
        .cmd_code    = (uint8_t)cmd | FRAME_RESP_FLAG,
        .payload_len = 1 + data_len,
        .payload[0]  = (uint8_t)status,
    };
    if (data_len > 0)
        memcpy(&resp.payload[1], data, data_len);

    uint8_t buf[FRAME_MAX_TOTAL];
    uint16_t len = frame_encode(&resp, buf, sizeof(buf));
    drv->send(buf, len, drv->ctx);
}

/* Command dispatch — called from the MCU's main loop or UART ISR context */
void responder_process_frame(uart_driver_t *drv, const uart_frame_t *cmd_frame)
{
    switch ((uart_cmd_code_t)cmd_frame->cmd_code) {

    case CMD_PING: {
        send_response(drv, cmd_frame->seq, CMD_PING, STATUS_OK, NULL, 0);
        break;
    }

    case CMD_READ_REG: {
        if (cmd_frame->payload_len < 1) {
            send_response(drv, cmd_frame->seq, CMD_READ_REG, STATUS_ERR_PARAM, NULL, 0);
            break;
        }
        uint8_t reg_addr = cmd_frame->payload[0];
        uint8_t reg_val  = read_hw_register(reg_addr);   /* platform call */
        send_response(drv, cmd_frame->seq, CMD_READ_REG, STATUS_OK, &reg_val, 1);
        break;
    }

    case CMD_WRITE_REG: {
        if (cmd_frame->payload_len < 2) {
            send_response(drv, cmd_frame->seq, CMD_WRITE_REG, STATUS_ERR_PARAM, NULL, 0);
            break;
        }
        uint8_t reg_addr = cmd_frame->payload[0];
        uint8_t reg_val  = cmd_frame->payload[1];
        write_hw_register(reg_addr, reg_val);             /* platform call */
        send_response(drv, cmd_frame->seq, CMD_WRITE_REG, STATUS_OK, NULL, 0);
        break;
    }

    default:
        send_response(drv, cmd_frame->seq, cmd_frame->cmd_code,
                      STATUS_ERR_UNKNOWN, NULL, 0);
        break;
    }
}
```

### Usage Example (Host application, `main.c`)

```c
#include "uart_cmd.h"
#include <stdio.h>

/* ── Minimal POSIX platform glue (Linux / macOS) ─────────────────────────── */
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

static int   g_fd = -1;

static int platform_send(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    return (int)write(g_fd, buf, len);
}

static int platform_recv_byte(uint8_t *byte, uint32_t timeout_ms, void *ctx)
{
    (void)ctx;
    struct pollfd pfd = { .fd = g_fd, .events = POLLIN };
    int rc = poll(&pfd, 1, (int)timeout_ms);
    if (rc <= 0) return 0;   /* timeout or error */
    return (int)read(g_fd, byte, 1);
}

static void platform_delay(uint32_t ms, void *ctx)
{
    (void)ctx;
    usleep(ms * 1000);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: %s <device>\n", argv[0]); return 1; }

    g_fd = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC);
    /* … configure termios for 115200 8N1 … */

    uart_driver_t drv = {
        .send             = platform_send,
        .recv_byte        = platform_recv_byte,
        .delay_ms         = platform_delay,
        .ctx              = NULL,
        .default_timeout_ms = 100,
        .max_retries      = 3,
    };

    /* ── PING ──────────────────────────────────────────────────────────── */
    uart_frame_t resp;
    trans_result_t rc = uart_transaction(&drv, CMD_PING, NULL, 0, &resp);
    printf("PING: %s\n", rc == TRANS_OK ? "OK" : "FAIL");

    /* ── READ register 0x10 ────────────────────────────────────────────── */
    uint8_t reg_addr = 0x10;
    rc = uart_transaction(&drv, CMD_READ_REG, &reg_addr, 1, &resp);
    if (rc == TRANS_OK && resp.payload_len >= 2)
        printf("REG[0x10] = 0x%02X\n", resp.payload[1]);

    /* ── WRITE register 0x10 = 0x55 ───────────────────────────────────── */
    uint8_t wr_args[2] = { 0x10, 0x55 };
    rc = uart_transaction(&drv, CMD_WRITE_REG, wr_args, 2, &resp);
    printf("WRITE: %s\n", rc == TRANS_OK ? "OK" : "FAIL");

    close(g_fd);
    return 0;
}
```

---

## Implementation in Rust

The Rust implementation uses traits for the UART backend, an `enum` for errors, and synchronous
blocking semantics that map naturally onto embedded `no_std` environments via `embedded-hal`.

### `Cargo.toml`

```toml
[package]
name    = "uart-cmd"
version = "0.1.0"
edition = "2021"

[dependencies]
# For embedded targets, replace std types with heapless equivalents:
# heapless = "0.8"
```

### Protocol Types (`src/protocol.rs`)

```rust
/// Maximum payload bytes in a single frame.
pub const MAX_PAYLOAD: usize = 64;

/// Wire-level start byte.
pub const FRAME_START: u8 = 0xAA;

/// Bit set in cmd_code of response frames.
pub const RESP_FLAG: u8 = 0x80;

// ── Command codes ──────────────────────────────────────────────────────────
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CmdCode {
    Ping       = 0x01,
    ReadReg    = 0x02,
    WriteReg   = 0x03,
    Reset      = 0x04,
    GetVersion = 0x05,
}

// ── Status codes (first byte of response payload) ─────────────────────────
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StatusCode {
    Ok         = 0x00,
    ErrUnknown = 0x01,
    ErrParam   = 0x02,
    ErrBusy    = 0x03,
    ErrCrc     = 0x04,
}

impl StatusCode {
    pub fn from_u8(v: u8) -> Self {
        match v {
            0x00 => Self::Ok,
            0x01 => Self::ErrUnknown,
            0x02 => Self::ErrParam,
            0x03 => Self::ErrBusy,
            0x04 => Self::ErrCrc,
            _    => Self::ErrUnknown,
        }
    }
}

// ── Decoded frame ──────────────────────────────────────────────────────────
#[derive(Debug, Clone)]
pub struct Frame {
    pub seq:         u8,
    pub cmd_code:    u8,
    pub payload:     [u8; MAX_PAYLOAD],
    pub payload_len: usize,
}

impl Frame {
    pub fn status(&self) -> StatusCode {
        if self.payload_len == 0 {
            StatusCode::Ok
        } else {
            StatusCode::from_u8(self.payload[0])
        }
    }
}

// ── Transaction errors ────────────────────────────────────────────────────
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransError {
    Timeout,
    CrcMismatch,
    SeqMismatch,
    BadStatus(u8),
    Overflow,
    Io,
}

impl core::fmt::Display for TransError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::Timeout       => write!(f, "transaction timed out"),
            Self::CrcMismatch   => write!(f, "CRC mismatch"),
            Self::SeqMismatch   => write!(f, "sequence number mismatch"),
            Self::BadStatus(s)  => write!(f, "remote status error: 0x{s:02X}"),
            Self::Overflow      => write!(f, "frame payload overflow"),
            Self::Io            => write!(f, "I/O error"),
        }
    }
}
```

### CRC-16 (`src/crc.rs`)

```rust
/// CRC-CCITT (XMODEM): polynomial 0x1021, initial value 0xFFFF.
pub fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            if crc & 0x8000 != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn known_vector() {
        // "123456789" → 0x29B1  (standard XMODEM test vector)
        assert_eq!(crc16(b"123456789"), 0x29B1);
    }
}
```

### UART Trait and Transaction Engine (`src/transport.rs`)

```rust
use crate::protocol::*;
use crate::crc::crc16;
use core::time::Duration;

/// Platform-specific UART backend.
pub trait UartTransport {
    type Error: core::fmt::Debug;

    /// Send a complete byte slice; blocks until all bytes are transmitted.
    fn send(&mut self, data: &[u8]) -> Result<(), Self::Error>;

    /// Receive one byte, waiting up to `timeout`. Returns `None` on timeout.
    fn recv_byte(&mut self, timeout: Duration) -> Result<Option<u8>, Self::Error>;

    /// Sleep for `duration` (used for back-off between retries).
    fn sleep(&mut self, duration: Duration);
}

// ── Frame encoder ──────────────────────────────────────────────────────────
fn encode_frame(frame: &Frame, buf: &mut [u8]) -> Result<usize, TransError> {
    let total = 6 + frame.payload_len;
    if buf.len() < total {
        return Err(TransError::Overflow);
    }
    buf[0] = FRAME_START;
    buf[1] = frame.seq;
    buf[2] = frame.cmd_code;
    buf[3] = frame.payload_len as u8;
    buf[4..4 + frame.payload_len].copy_from_slice(&frame.payload[..frame.payload_len]);

    let crc_slice = &buf[1..4 + frame.payload_len];
    let crc = crc16(crc_slice);
    buf[4 + frame.payload_len]     = (crc & 0xFF) as u8;
    buf[4 + frame.payload_len + 1] = (crc >> 8) as u8;

    Ok(total)
}

// ── Frame decoder (state-machine) ─────────────────────────────────────────
#[derive(Debug)]
enum ParseState {
    Start,
    Seq,
    Cmd,
    Len,
    Payload { remaining: usize },
    CrcLo,
    CrcHi { lo: u8 },
}

fn recv_frame<T: UartTransport>(
    transport: &mut T,
    timeout: Duration,
) -> Result<Frame, TransError> {
    let mut state = ParseState::Start;
    let mut raw: [u8; 6 + MAX_PAYLOAD] = [0u8; 6 + MAX_PAYLOAD];
    let mut raw_len: usize = 0;
    let mut frame = Frame {
        seq: 0, cmd_code: 0, payload: [0u8; MAX_PAYLOAD], payload_len: 0,
    };

    loop {
        let byte = transport
            .recv_byte(timeout)
            .map_err(|_| TransError::Io)?
            .ok_or(TransError::Timeout)?;

        state = match state {
            ParseState::Start => {
                if byte == FRAME_START {
                    raw[raw_len] = byte; raw_len += 1;
                    ParseState::Seq
                } else {
                    ParseState::Start   // resync
                }
            }
            ParseState::Seq => {
                frame.seq = byte;
                raw[raw_len] = byte; raw_len += 1;
                ParseState::Cmd
            }
            ParseState::Cmd => {
                frame.cmd_code = byte;
                raw[raw_len] = byte; raw_len += 1;
                ParseState::Len
            }
            ParseState::Len => {
                if byte as usize > MAX_PAYLOAD {
                    raw_len = 0;    // oversize frame — resync
                    ParseState::Start
                } else {
                    frame.payload_len = byte as usize;
                    raw[raw_len] = byte; raw_len += 1;
                    if frame.payload_len > 0 {
                        ParseState::Payload { remaining: frame.payload_len }
                    } else {
                        ParseState::CrcLo
                    }
                }
            }
            ParseState::Payload { remaining } => {
                let idx = frame.payload_len - remaining;
                frame.payload[idx] = byte;
                raw[raw_len] = byte; raw_len += 1;
                if remaining == 1 {
                    ParseState::CrcLo
                } else {
                    ParseState::Payload { remaining: remaining - 1 }
                }
            }
            ParseState::CrcLo => {
                ParseState::CrcHi { lo: byte }
            }
            ParseState::CrcHi { lo } => {
                let received_crc = lo as u16 | ((byte as u16) << 8);
                // CRC covers raw[1..1+3+payload_len]
                let computed_crc = crc16(&raw[1..4 + frame.payload_len]);
                if computed_crc != received_crc {
                    return Err(TransError::CrcMismatch);
                }
                return Ok(frame);
            }
        };
    }
}

// ── High-level transaction with retry and back-off ────────────────────────
pub struct UartMaster<T: UartTransport> {
    transport:   T,
    seq:         u8,
    timeout:     Duration,
    max_retries: u8,
}

impl<T: UartTransport> UartMaster<T> {
    pub fn new(transport: T, timeout: Duration, max_retries: u8) -> Self {
        Self { transport, seq: 0, timeout, max_retries }
    }

    pub fn transaction(
        &mut self,
        cmd: CmdCode,
        payload: &[u8],
    ) -> Result<Frame, TransError> {
        if payload.len() > MAX_PAYLOAD {
            return Err(TransError::Overflow);
        }

        // Build TX frame
        let mut tx_payload = [0u8; MAX_PAYLOAD];
        tx_payload[..payload.len()].copy_from_slice(payload);
        let tx_frame = Frame {
            seq: self.seq, cmd_code: cmd as u8,
            payload: tx_payload, payload_len: payload.len(),
        };

        let mut tx_buf = [0u8; 6 + MAX_PAYLOAD];
        let tx_len = encode_frame(&tx_frame, &mut tx_buf)?;

        let mut backoff = Duration::from_millis(5);

        for attempt in 0..=self.max_retries {
            self.transport.send(&tx_buf[..tx_len]).map_err(|_| TransError::Io)?;

            match recv_frame(&mut self.transport, self.timeout) {
                Ok(resp) => {
                    if resp.seq != self.seq {
                        return Err(TransError::SeqMismatch);
                    }
                    if (resp.cmd_code & !RESP_FLAG) != cmd as u8 {
                        return Err(TransError::SeqMismatch);
                    }
                    self.seq = self.seq.wrapping_add(1);

                    if resp.status() != StatusCode::Ok {
                        return Err(TransError::BadStatus(resp.payload[0]));
                    }
                    return Ok(resp);
                }
                Err(TransError::Timeout | TransError::CrcMismatch) if attempt < self.max_retries => {
                    self.transport.sleep(backoff);
                    backoff *= 2;   // exponential back-off
                }
                Err(e) => return Err(e),
            }
        }
        Err(TransError::Timeout)
    }
}
```

### Responder (MCU side, `src/responder.rs`)

```rust
use crate::protocol::*;
use crate::transport::UartTransport;
use crate::crc::crc16;

pub struct UartResponder<T: UartTransport> {
    transport: T,
}

impl<T: UartTransport> UartResponder<T> {
    pub fn new(transport: T) -> Self {
        Self { transport }
    }

    fn send_response(
        &mut self,
        seq:     u8,
        cmd:     CmdCode,
        status:  StatusCode,
        data:    &[u8],
    ) {
        let payload_len = 1 + data.len();
        let mut frame = Frame {
            seq,
            cmd_code:    cmd as u8 | RESP_FLAG,
            payload:     [0u8; MAX_PAYLOAD],
            payload_len,
        };
        frame.payload[0] = status as u8;
        frame.payload[1..1 + data.len()].copy_from_slice(data);

        let mut buf = [0u8; 6 + MAX_PAYLOAD];
        if let Ok(len) = encode_frame_pub(&frame, &mut buf) {
            let _ = self.transport.send(&buf[..len]);
        }
    }

    /// Call from your receive interrupt / main loop with a validated command frame.
    pub fn dispatch(&mut self, frame: &Frame) {
        let cmd = frame.cmd_code;
        match cmd {
            c if c == CmdCode::Ping as u8 => {
                self.send_response(frame.seq, CmdCode::Ping, StatusCode::Ok, &[]);
            }
            c if c == CmdCode::ReadReg as u8 => {
                if frame.payload_len < 1 {
                    self.send_response(frame.seq, CmdCode::ReadReg, StatusCode::ErrParam, &[]);
                    return;
                }
                let reg_addr = frame.payload[0];
                let value = hw_read_register(reg_addr);   // platform function
                self.send_response(frame.seq, CmdCode::ReadReg, StatusCode::Ok, &[value]);
            }
            c if c == CmdCode::WriteReg as u8 => {
                if frame.payload_len < 2 {
                    self.send_response(frame.seq, CmdCode::WriteReg, StatusCode::ErrParam, &[]);
                    return;
                }
                hw_write_register(frame.payload[0], frame.payload[1]); // platform function
                self.send_response(frame.seq, CmdCode::WriteReg, StatusCode::Ok, &[]);
            }
            _ => {
                self.send_response(frame.seq, CmdCode::Ping, StatusCode::ErrUnknown, &[]);
            }
        }
    }
}

// Re-export for responder use (normally module-internal)
pub fn encode_frame_pub(frame: &Frame, buf: &mut [u8]) -> Result<usize, TransError> {
    crate::transport::encode_frame(frame, buf)
}
```

### Usage Example (`src/main.rs`)

```rust
mod crc;
mod protocol;
mod transport;

use protocol::{CmdCode, MAX_PAYLOAD};
use transport::UartMaster;
use std::time::Duration;

// ── Minimal std UART transport using serialport crate ─────────────────────
use serialport::SerialPort;

struct StdUart { port: Box<dyn SerialPort> }

impl transport::UartTransport for StdUart {
    type Error = serialport::Error;

    fn send(&mut self, data: &[u8]) -> Result<(), Self::Error> {
        self.port.write_all(data).map_err(|e| serialport::Error::from(e))
    }

    fn recv_byte(&mut self, timeout: Duration) -> Result<Option<u8>, Self::Error> {
        self.port.set_timeout(timeout).ok();
        let mut buf = [0u8; 1];
        match self.port.read(&mut buf) {
            Ok(1) => Ok(Some(buf[0])),
            Ok(_) => Ok(None),
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => Ok(None),
            Err(e) => Err(serialport::Error::from(e)),
        }
    }

    fn sleep(&mut self, dur: Duration) { std::thread::sleep(dur); }
}

fn main() {
    let port = serialport::new("/dev/ttyUSB0", 115_200)
        .timeout(Duration::from_millis(100))
        .open()
        .expect("Failed to open port");

    let mut master = UartMaster::new(
        StdUart { port },
        Duration::from_millis(100),
        3,   // max retries
    );

    // PING
    match master.transaction(CmdCode::Ping, &[]) {
        Ok(_)  => println!("PING OK"),
        Err(e) => eprintln!("PING error: {e}"),
    }

    // READ register 0x10
    match master.transaction(CmdCode::ReadReg, &[0x10]) {
        Ok(resp) if resp.payload_len >= 2 => {
            println!("REG[0x10] = 0x{:02X}", resp.payload[1]);
        }
        Ok(_) => eprintln!("Short response"),
        Err(e) => eprintln!("READ error: {e}"),
    }

    // WRITE register 0x10 = 0x55
    match master.transaction(CmdCode::WriteReg, &[0x10, 0x55]) {
        Ok(_)  => println!("WRITE OK"),
        Err(e) => eprintln!("WRITE error: {e}"),
    }
}
```

---

## Error Handling and Edge Cases

### Garbage Byte Recovery

The byte-at-a-time state machine automatically resyncs: any byte that is not `0xAA` in the
`Start` state is silently discarded. Combined with CRC validation, this makes the protocol robust
to line noise, partial frames from a previous session, or a reset during a transaction.

### Sequence Number Wrap-Around

Both sides treat sequence numbers as `uint8_t`/`u8`, so they wrap from 255 → 0. The responder
must echo back whatever sequence number the master sent — it never tracks state, making it
inherently stateless and safe to restart.

### Half-Open Transactions

If the responder processes a command but the response is lost (line glitch), the master times out
and retries with the **same** sequence number. The responder must:

1. **Be idempotent** for read operations (safe to repeat).
2. For write/execute operations: optionally cache the last `(seq, response)` pair and re-send it
   on a duplicate sequence number, avoiding double-execution.

```c
/* Idempotency cache in responder (optional) */
typedef struct {
    uint8_t      last_seq;
    uart_frame_t cached_resp;
    bool         valid;
} idem_cache_t;

static idem_cache_t g_cache = { .valid = false };

void responder_process_frame_cached(uart_driver_t *drv,
                                    const uart_frame_t *cmd)
{
    if (g_cache.valid && g_cache.last_seq == cmd->seq) {
        /* Retransmit cached response without re-executing */
        uint8_t buf[FRAME_MAX_TOTAL];
        uint16_t len = frame_encode(&g_cache.cached_resp, buf, sizeof(buf));
        drv->send(buf, len, drv->ctx);
        return;
    }
    /* ... normal dispatch ... */
    /* After building response, cache it */
    g_cache.last_seq   = cmd->seq;
    g_cache.cached_resp = response;
    g_cache.valid      = true;
}
```

### Large Payload Chunking

For payloads larger than `MAX_PAYLOAD`, split into multiple transactions using an offset parameter:

```
CMD_READ_BLOCK  [offset_hi, offset_lo, count]
→ RESP with up to 64 bytes of data per call
```

The caller loops until all data is collected, incrementing the offset each time.

---

## Advanced Patterns

### 1. Unsolicited Events (Interrupt-Driven Extension)

For the responder to push events without a command (e.g., a sensor threshold breach), reserve a
special command code range (e.g., `0x40–0x5F`) for *events*. The master runs a separate receive
thread/task that listens for event frames and handles them asynchronously, while the transaction
engine only processes frames with the expected sequence number.

```
Responder                     Master
    |                            |
    |──── EVENT frame (0x41) ──> |  ← unsolicited, no seq correlation
    |                            |  master dispatches to event handler
    |                            |
    |                            |──── CMD_READ_REG ──>|  ← normal transaction
```

### 2. Pipeline / Sliding Window

For high-throughput scenarios (e.g., firmware update, bulk data streaming), allow N outstanding
commands before requiring responses, using a window of sequence numbers. Complexity rises
significantly; prefer chunked single-transaction patterns unless throughput is the bottleneck.

### 3. Binary + Text Dual Mode (AT-style Bootstrap)

Some systems start in text mode for human debugging and switch to binary framing on demand:

```
→  "BINARY\r\n"
←  "OK\r\n"
→  <binary frame: CMD_PING>
←  <binary frame: RESP_PING STATUS_OK>
```

The responder listens for the literal string `"BINARY\r\n"` before enabling the frame parser.

### 4. Multi-Responder with Address Byte

Extend the frame format by adding one address byte after the start byte, enabling a star topology
(RS-485 multi-drop) over the same framing layer:

```
┌───────┬─────────┬─────────┬──────┬────────┬──────────┬─────────┐
│ 0xAA  │  Addr   │   Seq   │ Cmd  │  Len   │ Payload  │  CRC16  │
└───────┴─────────┴─────────┴──────┴────────┴──────────┴─────────┘
```

Only the responder whose address matches processes the frame; all others stay silent.
`Addr = 0xFF` is the broadcast address (no response expected).

---

## Summary

| Concern             | Approach                                                               |
|---------------------|------------------------------------------------------------------------|
| **Framing**         | Start byte + length field; state-machine parser for robust resync      |
| **Integrity**       | CRC-16 (XMODEM) over header + payload                                  |
| **Correlation**     | Per-transaction sequence number echoed in response                     |
| **Reliability**     | Application-level timeout + retry with exponential back-off            |
| **Idempotency**     | Optional last-(seq, response) cache on responder for write commands    |
| **Error reporting** | Status byte as first payload byte; distinct error codes                |
| **Extensibility**   | Cmd code namespace, address byte for multi-drop, event frames          |
| **Portability**     | Platform callbacks (C) / trait objects (Rust) isolate hardware         |

The command-response pattern transforms raw, unreliable UART bytes into a structured, verifiable,
and retryable RPC layer. It is the right architectural choice whenever two systems need to exchange
structured commands and data over a serial link, providing a solid foundation on which to build
sensors, actuators, bootloaders, or any custom embedded protocol.