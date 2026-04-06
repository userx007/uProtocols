# 36. UART Framing Protocols

- **Why framing is necessary** — UART delivers raw bytes with no packet boundaries; noise, buffer overruns, and cold starts make synchronization essential
- **Four core strategies** — delimiter-based, length-prefixed, fixed-length, and hybrid frames, with trade-offs for each
- **Byte stuffing** — both SLIP (RFC 1055) and an explanation of COBS
- **Error detection** — XOR, CRC-8, CRC-16-CCITT (recommended), CRC-32
- **Receiver state machine diagram** — all 8 states from `WAIT_SOF1` through `READ_CRC_HI`

**C/C++ code:**

- `crc16_update()` / `crc16_buf()` — inline CRC-16/CCITT-FALSE
- `frame_encode()` — serialises a frame into raw bytes
- `frame_rx_byte()` — byte-by-byte state machine decoder
- C++17 `TypedFrame<T>` template with a `FrameDispatcher` callback registry
- Full SLIP encoder and stateful SLIP decoder

**Rust code:**

- `no_std`-compatible `RawFrame` with `encode()` returning a slice
- `FrameReceiver` state machine with `CrcMismatch`, `PayloadTooLarge`, and `SequenceGap` error variants
- Fluent `FrameBuilder` API with `push_u16_le()`
- SLIP encoder
- Unit tests covering round-trip, CRC corruption, and SLIP escaping

## Designing Packet Structures with Delimiters and Length Fields

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Framing is Necessary](#why-framing-is-necessary)
3. [Core Framing Strategies](#core-framing-strategies)
   - [Delimiter-Based Framing](#delimiter-based-framing)
   - [Length-Prefixed Framing](#length-prefixed-framing)
   - [Fixed-Length Framing](#fixed-length-framing)
   - [Hybrid Framing (Header + Length + Payload + CRC)](#hybrid-framing)
4. [Packet Structure Design](#packet-structure-design)
5. [Byte Stuffing / Escaping](#byte-stuffing--escaping)
6. [Error Detection in Frames](#error-detection-in-frames)
7. [C/C++ Implementation](#cc-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Comparison Table](#comparison-table)
10. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is a low-level serial protocol that transmits raw bytes, one at a time, with no built-in notion of where a "message" begins or ends. The hardware only guarantees that individual bytes are framed correctly at the bit level (start bit, data bits, optional parity, stop bit). What it does **not** provide is any application-level structure — no packet boundaries, no message length, no error detection across multiple bytes.

**Framing protocols** solve this problem by imposing structure on the raw byte stream. They answer the fundamental questions:

- Where does a packet start?
- Where does it end?
- How many bytes of payload are there?
- Is the received data valid?

Without a framing protocol, the receiver has no reliable way to distinguish a burst of sensor readings from a command packet, or to recover gracefully when bytes are lost or corrupted.

---

## Why Framing is Necessary

Consider a system where a microcontroller sends temperature and pressure readings over UART:

```
Sender:   [0x23][0x01][0xF4][0x00][0x7D][0x23][0x02][0xAB]...
Receiver: ???
```

The receiver sees a raw stream of bytes. Without framing it cannot know:

- Whether `0x23` is the start of a packet or mid-payload data
- How many bytes belong to one reading
- Where one reading ends and the next begins
- Whether any bytes were dropped by the UART FIFO

Real-world UART links suffer from:

- **Buffer overruns** — bytes dropped when the MCU is busy
- **Glitches and noise** — spurious bytes injected into the stream
- **Cold starts** — receiver joins mid-stream and must synchronize
- **Baud-rate mismatches** — causing framing errors at the bit level that produce garbage bytes

A well-designed framing protocol must handle all these cases robustly.

---

## Core Framing Strategies

### Delimiter-Based Framing

The simplest approach: a special byte (or byte sequence) marks the **start** and/or **end** of a packet.

#### End-Only Delimiter (e.g., `\n` in ASCII protocols)

```
[PAYLOAD BYTES...][0x0A]
```

Used by: AT commands, NMEA GPS sentences, Modbus ASCII. Simple but fragile — the delimiter byte cannot appear in the payload without escaping.

#### Start + End Delimiters (e.g., HDLC-style)

```
[0x7E][PAYLOAD...][0x7E]
```

Where `0x7E` is the **flag byte**. The receiver synchronizes by scanning for the flag. Any `0x7E` inside the payload must be escaped (see [Byte Stuffing](#byte-stuffing--escaping)).

#### Start + End + Different Values

```
[SOF=0xAA][PAYLOAD...][EOF=0x55]
```

Using distinct start-of-frame (SOF) and end-of-frame (EOF) values reduces false synchronization but still requires escaping if payload can contain these values.

**Pros:** Simple to implement; receiver easily re-synchronizes after errors.  
**Cons:** Delimiter bytes require escaping in payload; variable-length overhead.

---

### Length-Prefixed Framing

Instead of delimiters, the packet begins with a **length field** that tells the receiver exactly how many bytes follow.

```
[LENGTH (1-4 bytes)][PAYLOAD (LENGTH bytes)]
```

Example (1-byte length):
```
[0x05][0x01][0x02][0x03][0x04][0x05]
  ^     ^--- 5 bytes of payload ---^
  |
  Length = 5
```

The receiver reads the length field first, then reads exactly that many bytes. No scanning for delimiters, no escaping needed.

**Pros:** No delimiter conflicts; payload can contain any byte value; efficient.  
**Cons:** If length field is corrupted, the receiver desyncs and may read garbage as payload for a long time; requires a separate synchronization mechanism.

---

### Fixed-Length Framing

Every packet is exactly N bytes. The simplest possible protocol:

```
[BYTE_0][BYTE_1]...[BYTE_N-1]
```

Used by: some IMUs, motor controllers, and sensor modules with fixed data formats.

**Pros:** Zero overhead; trivially simple to parse; no synchronization needed if the stream starts cleanly.  
**Cons:** Inflexible; a single lost byte causes permanent desynchronization unless a higher-level sync mechanism exists.

---

### Hybrid Framing

Production-quality protocols combine multiple techniques into a **structured frame**:

```
┌──────┬──────┬─────────┬─────────────────────────┬─────────┐
│ SOF  │ CMD  │ LENGTH  │       PAYLOAD           │  CRC    │
│ 1 B  │ 1 B  │ 2 B LE  │      0–255 B            │  2 B    │
└──────┴──────┴─────────┴─────────────────────────┴─────────┘
```

This is the approach taken by protocols like COBS, SLIP, STM32 bootloader protocol, and many proprietary embedded protocols. It offers:

- Start-of-frame for synchronization
- Command/type field for dispatch
- Length field for exact payload extraction
- CRC for integrity verification

---

## Packet Structure Design

### Choosing Field Widths

| Field | Typical Size | Notes |
|---|---|---|
| SOF / Magic | 1–2 bytes | Fixed value(s), enables sync |
| Packet Type / CMD | 1 byte | Up to 256 command types |
| Sequence Number | 1 byte | Detects dropped packets; wraps at 256 |
| Length | 1–2 bytes | 1 byte → max 255 B payload; 2 bytes → 65535 B |
| Payload | 0–N bytes | Application data |
| CRC / Checksum | 1–4 bytes | CRC-8, CRC-16, CRC-32 |
| EOF (optional) | 1 byte | Extra synchronization anchor |

### Example: A Practical Hybrid Frame

```
Byte offset:  0      1      2      3      4      5..N+4   N+5  N+6
              ┌──────┬──────┬──────┬──────┬──────┬────────┬────┬────┐
              │ 0xAA │ 0x55 │ CMD  │ SEQ  │ LEN  │PAYLOAD │CRC_L│CRC_H│
              └──────┴──────┴──────┴──────┴──────┴────────┴────┴────┘
              │←── Magic ──→│                    │←CRC-16 of bytes 2..N+4→│
```

- **Magic (2 bytes):** `0xAA 0x55` — easy to spot on a logic analyzer; unlikely in payload.
- **CMD (1 byte):** Command or packet type identifier.
- **SEQ (1 byte):** Sequence number, incremented per packet.
- **LEN (1 byte):** Payload length in bytes (0–255).
- **PAYLOAD (LEN bytes):** Application data.
- **CRC-16 (2 bytes, little-endian):** Over CMD + SEQ + LEN + PAYLOAD.

### State Machine for the Receiver

```
         ┌─────────────────────────────────────────────────────┐
         │                   WAIT_SOF_1                        │
         │   Scan for 0xAA; ignore all other bytes             │
         └─────────────────────┬───────────────────────────────┘
                               │ got 0xAA
                               ▼
         ┌─────────────────────────────────────────────────────┐
         │                   WAIT_SOF_2                        │
         │   Expect 0x55; if not, go back to WAIT_SOF_1        │
         └─────────────────────┬───────────────────────────────┘
                               │ got 0x55
                               ▼
         ┌─────────────────────────────────────────────────────┐
         │                   READ_HEADER                       │
         │   Read CMD, SEQ, LEN (3 bytes)                      │
         └─────────────────────┬───────────────────────────────┘
                               │ 3 bytes received
                               ▼
         ┌─────────────────────────────────────────────────────┐
         │                   READ_PAYLOAD                      │
         │   Read LEN bytes into buffer                        │
         └─────────────────────┬───────────────────────────────┘
                               │ LEN bytes received
                               ▼
         ┌─────────────────────────────────────────────────────┐
         │                   READ_CRC                          │
         │   Read 2 CRC bytes; verify; dispatch or discard     │
         └─────────────────────┬───────────────────────────────┘
                               │ CRC OK
                               ▼
                         DISPATCH / HANDLE
                               │
                               └──► back to WAIT_SOF_1
```

---

## Byte Stuffing / Escaping

When delimiter-based framing is used, the delimiter byte value must not appear in the payload. Two common solutions:

### SLIP-Style Escaping (RFC 1055)

- `END` byte (`0xC0`) signals end of packet
- If payload contains `0xC0`, replace with `[ESC=0xDB][ESC_END=0xDC]`
- If payload contains `0xDB`, replace with `[ESC=0xDB][ESC_ESC=0xDD]`

### COBS (Consistent Overhead Byte Stuffing)

COBS is a superior algorithm that guarantees **zero occurrence of `0x00`** in the encoded output, using `0x00` as a reliable frame delimiter. It replaces all zero bytes with offset pointers, adding at most 1 byte of overhead per 254 bytes of payload. Widely used in CAN, USB, and serial protocols.

Encoding idea: replace every `0x00` with the distance (in bytes) to the next `0x00` or end-of-frame.

---

## Error Detection in Frames

### Simple XOR Checksum (weak)

```c
uint8_t checksum = 0;
for (size_t i = 0; i < len; i++) checksum ^= data[i];
```

Fast but cannot detect burst errors or transposed bytes.

### CRC-8 (moderate)

Good for short packets (< 256 bytes). Polynomial 0x07 (CCITT).

### CRC-16-CCITT (recommended for UART)

- Polynomial: `0x1021`, Initial value: `0xFFFF`
- Detects all single and double bit errors in packets up to 32767 bits
- Standard in XMODEM, CCITT, X.25, and most embedded serial protocols

### CRC-32 (robust, higher overhead)

For larger payloads or safety-critical applications.

---

## C/C++ Implementation

### Frame Definition and CRC-16

```c
/* framing.h */
#ifndef FRAMING_H
#define FRAMING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ──────────────────────────────────────────────
   Frame Format:
     [0xAA][0x55][CMD][SEQ][LEN][PAYLOAD...][CRC_L][CRC_H]
   CRC-16 covers: CMD + SEQ + LEN + PAYLOAD
   ────────────────────────────────────────────── */

#define SOF_BYTE_1    0xAAu
#define SOF_BYTE_2    0x55u
#define MAX_PAYLOAD   255u
#define HEADER_SIZE   5u     /* SOF1 + SOF2 + CMD + SEQ + LEN */
#define TRAILER_SIZE  2u     /* CRC16 low + high */
#define MAX_FRAME_LEN (HEADER_SIZE + MAX_PAYLOAD + TRAILER_SIZE)

typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[MAX_PAYLOAD];
} Frame;

/* CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF */
static inline uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (crc << 1) ^ 0x1021u : (crc << 1);
    }
    return crc;
}

static inline uint16_t crc16_buf(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) crc = crc16_update(crc, buf[i]);
    return crc;
}

#endif /* FRAMING_H */
```

### Frame Encoder

```c
/* frame_encode: serialise a Frame into raw bytes ready for UART TX.
   Returns number of bytes written into 'out', or 0 on error.
   'out' must be at least MAX_FRAME_LEN bytes. */
size_t frame_encode(const Frame *f, uint8_t *out) {
    if (!f || !out || f->len > MAX_PAYLOAD) return 0;

    size_t idx = 0;

    /* Start-of-frame magic */
    out[idx++] = SOF_BYTE_1;
    out[idx++] = SOF_BYTE_2;

    /* Header fields — also the CRC input region */
    const size_t crc_start = idx;
    out[idx++] = f->cmd;
    out[idx++] = f->seq;
    out[idx++] = f->len;

    /* Payload */
    memcpy(&out[idx], f->payload, f->len);
    idx += f->len;

    /* CRC over [CMD..PAYLOAD] */
    uint16_t crc = crc16_buf(&out[crc_start], idx - crc_start);
    out[idx++] = (uint8_t)(crc & 0xFFu);        /* CRC low byte  */
    out[idx++] = (uint8_t)((crc >> 8) & 0xFFu); /* CRC high byte */

    return idx;
}
```

### Frame Decoder — State Machine

```c
/* Receiver state machine — feed one byte at a time via frame_rx_byte().
   Call frame_rx_init() once before use. */

typedef enum {
    RX_WAIT_SOF1,
    RX_WAIT_SOF2,
    RX_READ_CMD,
    RX_READ_SEQ,
    RX_READ_LEN,
    RX_READ_PAYLOAD,
    RX_READ_CRC_LO,
    RX_READ_CRC_HI,
} RxState;

typedef struct {
    RxState  state;
    Frame    frame;
    uint16_t rx_crc;        /* CRC accumulated during reception  */
    uint16_t calc_crc;      /* CRC we compute over received data */
    size_t   payload_idx;
} RxContext;

void frame_rx_init(RxContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = RX_WAIT_SOF1;
}

/* Returns true when a complete, valid frame has been received.
   The decoded frame is available in ctx->frame. */
bool frame_rx_byte(RxContext *ctx, uint8_t byte) {
    switch (ctx->state) {

    case RX_WAIT_SOF1:
        if (byte == SOF_BYTE_1) ctx->state = RX_WAIT_SOF2;
        break;

    case RX_WAIT_SOF2:
        ctx->state = (byte == SOF_BYTE_2) ? RX_READ_CMD : RX_WAIT_SOF1;
        /* If we got another 0xAA, stay watching for SOF2 */
        if (byte == SOF_BYTE_1) ctx->state = RX_WAIT_SOF2;
        break;

    case RX_READ_CMD:
        ctx->frame.cmd  = byte;
        ctx->calc_crc   = crc16_update(0xFFFFu, byte); /* start CRC */
        ctx->state      = RX_READ_SEQ;
        break;

    case RX_READ_SEQ:
        ctx->frame.seq = byte;
        ctx->calc_crc  = crc16_update(ctx->calc_crc, byte);
        ctx->state     = RX_READ_LEN;
        break;

    case RX_READ_LEN:
        if (byte > MAX_PAYLOAD) {
            /* Implausible length — re-sync */
            ctx->state = RX_WAIT_SOF1;
            break;
        }
        ctx->frame.len  = byte;
        ctx->calc_crc   = crc16_update(ctx->calc_crc, byte);
        ctx->payload_idx = 0;
        ctx->state = (byte == 0) ? RX_READ_CRC_LO : RX_READ_PAYLOAD;
        break;

    case RX_READ_PAYLOAD:
        ctx->frame.payload[ctx->payload_idx++] = byte;
        ctx->calc_crc = crc16_update(ctx->calc_crc, byte);
        if (ctx->payload_idx == ctx->frame.len) ctx->state = RX_READ_CRC_LO;
        break;

    case RX_READ_CRC_LO:
        ctx->rx_crc = byte;          /* low byte */
        ctx->state  = RX_READ_CRC_HI;
        break;

    case RX_READ_CRC_HI:
        ctx->rx_crc |= (uint16_t)byte << 8; /* high byte */
        ctx->state = RX_WAIT_SOF1;
        if (ctx->rx_crc == ctx->calc_crc) return true;  /* ✓ valid frame */
        /* CRC mismatch — silently discard */
        break;
    }
    return false;
}
```

### C++ Wrapper with Templates and RAII

```cpp
/* framing.hpp — C++17 typed framing layer */
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <functional>
#include "framing.h"   /* reuse C CRC helpers */

/* Strongly-typed command enum */
enum class Cmd : uint8_t {
    Ping        = 0x01,
    Pong        = 0x02,
    SetLed      = 0x10,
    GetSensor   = 0x20,
    SensorData  = 0x21,
    Ack         = 0xAC,
    Nack        = 0xAD,
};

/* Typed frame wrapper */
template<typename PayloadT>
struct TypedFrame {
    Cmd      cmd;
    uint8_t  seq;
    PayloadT payload;

    /* Encode to raw bytes; returns byte count */
    size_t encode(uint8_t (&out)[MAX_FRAME_LEN]) const {
        Frame f{};
        f.cmd = static_cast<uint8_t>(cmd);
        f.seq = seq;
        f.len = static_cast<uint8_t>(sizeof(PayloadT));
        static_assert(sizeof(PayloadT) <= MAX_PAYLOAD, "Payload too large");
        std::memcpy(f.payload, &payload, sizeof(PayloadT));
        return frame_encode(&f, out);
    }
};

/* Example payload structs — use __attribute__((packed)) on GCC/Clang */
struct __attribute__((packed)) SensorPayload {
    int16_t  temperature_cdeg;  /* 0.01 °C per LSB */
    uint16_t pressure_pa;       /* Pascals           */
    uint8_t  humidity_pct;      /* 0–100             */
};

struct __attribute__((packed)) SetLedPayload {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

/* FrameDispatcher: register per-command callbacks */
class FrameDispatcher {
public:
    using Handler = std::function<void(const Frame &)>;

    void on(Cmd cmd, Handler h) {
        handlers_[static_cast<uint8_t>(cmd)] = std::move(h);
    }

    /* Feed one byte; calls registered handler if frame is complete & valid */
    void feed(uint8_t byte) {
        if (frame_rx_byte(&ctx_, byte)) {
            auto &h = handlers_[ctx_.frame.cmd];
            if (h) h(ctx_.frame);
        }
    }

    void reset() { frame_rx_init(&ctx_); }

private:
    RxContext ctx_{};
    std::array<Handler, 256> handlers_{};
};

/* ──── Usage example ────────────────────────────────────────────────────────

    FrameDispatcher dispatcher;
    dispatcher.reset();

    dispatcher.on(Cmd::SensorData, [](const Frame &f) {
        SensorPayload sp;
        std::memcpy(&sp, f.payload, sizeof(sp));
        printf("Temp: %.2f°C  Pressure: %u Pa\n",
               sp.temperature_cdeg / 100.0, sp.pressure_pa);
    });

    // In UART RX ISR or polling loop:
    // dispatcher.feed(uart_read_byte());

    // Sending a typed frame:
    TypedFrame<SetLedPayload> led_frame{
        .cmd     = Cmd::SetLed,
        .seq     = seq_counter++,
        .payload = {.red = 255, .green = 0, .blue = 128},
    };
    uint8_t buf[MAX_FRAME_LEN];
    size_t n = led_frame.encode(buf);
    uart_write(buf, n);

   ─────────────────────────────────────────────────────────────────────── */
```

### Byte Stuffing — SLIP Implementation

```c
/* SLIP encoder/decoder (RFC 1055) */
#define SLIP_END      0xC0u
#define SLIP_ESC      0xDBu
#define SLIP_ESC_END  0xDCu
#define SLIP_ESC_ESC  0xDDu

/* Encode src[0..src_len) into dst using SLIP.
   dst must hold at worst 2*src_len + 2 bytes.
   Returns encoded length. */
size_t slip_encode(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_cap)
{
    size_t out = 0;
    dst[out++] = SLIP_END;  /* optional leading END for re-sync */
    for (size_t i = 0; i < src_len; i++) {
        if (out + 2 > dst_cap) return 0;  /* overflow guard */
        if (src[i] == SLIP_END) {
            dst[out++] = SLIP_ESC;
            dst[out++] = SLIP_ESC_END;
        } else if (src[i] == SLIP_ESC) {
            dst[out++] = SLIP_ESC;
            dst[out++] = SLIP_ESC_ESC;
        } else {
            dst[out++] = src[i];
        }
    }
    if (out < dst_cap) dst[out++] = SLIP_END;
    return out;
}

typedef struct {
    uint8_t  buf[512];
    size_t   len;
    bool     escape_next;
    bool     in_packet;
} SlipRxCtx;

/* Returns pointer to completed packet (in ctx->buf) and sets *pkt_len,
   or NULL if packet not yet complete. */
const uint8_t *slip_rx_byte(SlipRxCtx *ctx, uint8_t byte, size_t *pkt_len) {
    if (byte == SLIP_END) {
        if (ctx->in_packet && ctx->len > 0) {
            *pkt_len = ctx->len;
            ctx->len = 0;
            ctx->in_packet = false;
            ctx->escape_next = false;
            return ctx->buf;  /* caller must process before next call */
        }
        ctx->in_packet = true;
        ctx->len = 0;
        return NULL;
    }

    if (!ctx->in_packet) return NULL;

    if (byte == SLIP_ESC) { ctx->escape_next = true; return NULL; }

    if (ctx->escape_next) {
        ctx->escape_next = false;
        byte = (byte == SLIP_ESC_END) ? SLIP_END :
               (byte == SLIP_ESC_ESC) ? SLIP_ESC : byte; /* protocol error */
    }

    if (ctx->len < sizeof(ctx->buf))
        ctx->buf[ctx->len++] = byte;

    return NULL;
}
```

---

## Rust Implementation

### Frame Types and CRC

```rust
// framing.rs — UART framing protocol in Rust
// no_std compatible (no heap allocation required)
#![allow(dead_code)]

use core::mem;

pub const SOF_BYTE_1: u8 = 0xAA;
pub const SOF_BYTE_2: u8 = 0x55;
pub const MAX_PAYLOAD: usize = 255;

// ── CRC-16/CCITT-FALSE ──────────────────────────────────────────────────────

pub fn crc16_update(mut crc: u16, byte: u8) -> u16 {
    crc ^= (byte as u16) << 8;
    for _ in 0..8 {
        crc = if crc & 0x8000 != 0 {
            (crc << 1) ^ 0x1021
        } else {
            crc << 1
        };
    }
    crc
}

pub fn crc16(data: &[u8]) -> u16 {
    data.iter().fold(0xFFFF_u16, |crc, &b| crc16_update(crc, b))
}

// ── Command enum ─────────────────────────────────────────────────────────────

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cmd {
    Ping      = 0x01,
    Pong      = 0x02,
    SetLed    = 0x10,
    GetSensor = 0x20,
    SensorData = 0x21,
    Ack       = 0xAC,
    Nack      = 0xAD,
    Unknown   = 0xFF,
}

impl From<u8> for Cmd {
    fn from(v: u8) -> Self {
        match v {
            0x01 => Cmd::Ping,
            0x02 => Cmd::Pong,
            0x10 => Cmd::SetLed,
            0x20 => Cmd::GetSensor,
            0x21 => Cmd::SensorData,
            0xAC => Cmd::Ack,
            0xAD => Cmd::Nack,
            _    => Cmd::Unknown,
        }
    }
}

// ── Frame ────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct Frame {
    pub cmd:     Cmd,
    pub seq:     u8,
    pub payload: heapless::Vec<u8, MAX_PAYLOAD>,  // or [u8; MAX_PAYLOAD] + len
}

// For no_std without heapless, use a fixed array:
#[derive(Debug, Clone)]
pub struct RawFrame {
    pub cmd:     u8,
    pub seq:     u8,
    pub len:     u8,
    pub payload: [u8; MAX_PAYLOAD],
}

impl RawFrame {
    pub fn new(cmd: u8, seq: u8, payload: &[u8]) -> Option<Self> {
        if payload.len() > MAX_PAYLOAD { return None; }
        let mut f = RawFrame { cmd, seq, len: payload.len() as u8,
                               payload: [0u8; MAX_PAYLOAD] };
        f.payload[..payload.len()].copy_from_slice(payload);
        Some(f)
    }

    /// Encode into a fixed-size output buffer.
    /// Returns slice of the buffer that was filled, or None on error.
    pub fn encode<'a>(&self, out: &'a mut [u8]) -> Option<&'a [u8]> {
        let frame_len = 5 + self.len as usize + 2; // SOF+SOF+CMD+SEQ+LEN + payload + CRC
        if out.len() < frame_len { return None; }

        let mut idx = 0usize;
        out[idx] = SOF_BYTE_1; idx += 1;
        out[idx] = SOF_BYTE_2; idx += 1;

        let crc_start = idx;
        out[idx] = self.cmd;   idx += 1;
        out[idx] = self.seq;   idx += 1;
        out[idx] = self.len;   idx += 1;

        let plen = self.len as usize;
        out[idx..idx + plen].copy_from_slice(&self.payload[..plen]);
        idx += plen;

        let crc = crc16(&out[crc_start..idx]);
        out[idx]     = (crc & 0xFF) as u8;
        out[idx + 1] = (crc >> 8)   as u8;
        idx += 2;

        Some(&out[..idx])
    }
}
```

### Receiver State Machine

```rust
// ── Receiver state machine ───────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum RxState {
    WaitSof1,
    WaitSof2,
    ReadCmd,
    ReadSeq,
    ReadLen,
    ReadPayload,
    ReadCrcLo,
    ReadCrcHi,
}

#[derive(Debug)]
pub struct FrameReceiver {
    state:       RxState,
    frame:       RawFrame,
    payload_idx: usize,
    calc_crc:    u16,
    rx_crc:      u16,

    // Statistics
    pub frames_ok:    u32,
    pub frames_bad:   u32,
    pub frames_lost:  u32,
    last_seq:         u8,
    seq_initialized:  bool,
}

#[derive(Debug, PartialEq)]
pub enum RxError {
    CrcMismatch { expected: u16, got: u16 },
    PayloadTooLarge(u8),
    SequenceGap { expected: u8, got: u8 },
}

impl FrameReceiver {
    pub const fn new() -> Self {
        FrameReceiver {
            state:          RxState::WaitSof1,
            frame:          RawFrame { cmd: 0, seq: 0, len: 0, payload: [0; MAX_PAYLOAD] },
            payload_idx:    0,
            calc_crc:       0xFFFF,
            rx_crc:         0,
            frames_ok:      0,
            frames_bad:     0,
            frames_lost:    0,
            last_seq:       0,
            seq_initialized: false,
        }
    }

    pub fn reset(&mut self) {
        self.state = RxState::WaitSof1;
        self.payload_idx = 0;
        self.calc_crc = 0xFFFF;
    }

    /// Feed one byte into the state machine.
    /// Returns Some(Ok(frame)) on success, Some(Err(...)) on framing error,
    /// None if more bytes are needed.
    pub fn feed(&mut self, byte: u8) -> Option<Result<RawFrame, RxError>> {
        match self.state {
            RxState::WaitSof1 => {
                if byte == SOF_BYTE_1 { self.state = RxState::WaitSof2; }
                None
            }

            RxState::WaitSof2 => {
                if byte == SOF_BYTE_2 {
                    self.state = RxState::ReadCmd;
                } else if byte == SOF_BYTE_1 {
                    // Stay in WaitSof2 — might be 0xAA 0xAA 0x55
                } else {
                    self.state = RxState::WaitSof1;
                }
                None
            }

            RxState::ReadCmd => {
                self.frame.cmd  = byte;
                self.calc_crc   = crc16_update(0xFFFF, byte);
                self.state      = RxState::ReadSeq;
                None
            }

            RxState::ReadSeq => {
                self.frame.seq  = byte;
                self.calc_crc   = crc16_update(self.calc_crc, byte);
                self.state      = RxState::ReadLen;
                None
            }

            RxState::ReadLen => {
                if byte as usize > MAX_PAYLOAD {
                    self.state = RxState::WaitSof1;
                    self.frames_bad += 1;
                    return Some(Err(RxError::PayloadTooLarge(byte)));
                }
                self.frame.len  = byte;
                self.calc_crc   = crc16_update(self.calc_crc, byte);
                self.payload_idx = 0;
                self.state = if byte == 0 { RxState::ReadCrcLo }
                             else { RxState::ReadPayload };
                None
            }

            RxState::ReadPayload => {
                self.frame.payload[self.payload_idx] = byte;
                self.payload_idx += 1;
                self.calc_crc = crc16_update(self.calc_crc, byte);
                if self.payload_idx == self.frame.len as usize {
                    self.state = RxState::ReadCrcLo;
                }
                None
            }

            RxState::ReadCrcLo => {
                self.rx_crc = byte as u16;
                self.state  = RxState::ReadCrcHi;
                None
            }

            RxState::ReadCrcHi => {
                self.rx_crc |= (byte as u16) << 8;
                self.state = RxState::WaitSof1;

                if self.rx_crc != self.calc_crc {
                    self.frames_bad += 1;
                    return Some(Err(RxError::CrcMismatch {
                        expected: self.calc_crc,
                        got:      self.rx_crc,
                    }));
                }

                // Sequence gap detection
                let seq_err = if self.seq_initialized {
                    let expected = self.last_seq.wrapping_add(1);
                    if self.frame.seq != expected {
                        self.frames_lost += (self.frame.seq.wrapping_sub(expected)) as u32;
                        Some(RxError::SequenceGap { expected, got: self.frame.seq })
                    } else { None }
                } else { None };

                self.last_seq       = self.frame.seq;
                self.seq_initialized = true;
                self.frames_ok      += 1;

                // Return the valid frame (sequence errors are advisory only)
                let _ = seq_err; // could be surfaced separately
                Some(Ok(self.frame.clone()))
            }
        }
    }
}
```

### Frame Builder and SLIP Encoder

```rust
// ── Frame builder (fluent API) ────────────────────────────────────────────────

pub struct FrameBuilder {
    cmd: u8,
    seq: u8,
    payload: [u8; MAX_PAYLOAD],
    payload_len: usize,
}

impl FrameBuilder {
    pub fn new(cmd: Cmd, seq: u8) -> Self {
        FrameBuilder { cmd: cmd as u8, seq, payload: [0; MAX_PAYLOAD], payload_len: 0 }
    }

    pub fn payload(mut self, data: &[u8]) -> Self {
        let n = data.len().min(MAX_PAYLOAD);
        self.payload[..n].copy_from_slice(&data[..n]);
        self.payload_len = n;
        self
    }

    /// Append a little-endian u16 to payload
    pub fn push_u16_le(mut self, v: u16) -> Self {
        if self.payload_len + 2 <= MAX_PAYLOAD {
            self.payload[self.payload_len]     = (v & 0xFF) as u8;
            self.payload[self.payload_len + 1] = (v >> 8)   as u8;
            self.payload_len += 2;
        }
        self
    }

    pub fn build(self) -> Option<RawFrame> {
        RawFrame::new(self.cmd, self.seq, &self.payload[..self.payload_len])
    }
}

// ── SLIP encoder (no_std, no allocation) ────────────────────────────────────

pub const SLIP_END:     u8 = 0xC0;
pub const SLIP_ESC:     u8 = 0xDB;
pub const SLIP_ESC_END: u8 = 0xDC;
pub const SLIP_ESC_ESC: u8 = 0xDD;

/// Encode `src` using SLIP into `dst`.
/// Returns Ok(encoded_len) or Err(()) if dst is too small.
pub fn slip_encode(src: &[u8], dst: &mut [u8]) -> Result<usize, ()> {
    let mut out = 0usize;
    let mut push = |dst: &mut [u8], b: u8| -> bool {
        if out < dst.len() { dst[out] = b; out += 1; true } else { false }
    };

    if !push(dst, SLIP_END) { return Err(()); }
    for &b in src {
        match b {
            SLIP_END => { push(dst, SLIP_ESC); push(dst, SLIP_ESC_END); }
            SLIP_ESC => { push(dst, SLIP_ESC); push(dst, SLIP_ESC_ESC); }
            b        => { push(dst, b); }
        }
    }
    push(dst, SLIP_END);
    Ok(out)
}

// ── Integration example ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_decode_roundtrip() {
        let payload = [0x01u8, 0x02, 0x03, 0xAA, 0x55]; // contains SOF bytes!
        let frame   = RawFrame::new(0x21, 42, &payload).unwrap();

        let mut buf = [0u8; 512];
        let encoded = frame.encode(&mut buf).unwrap();

        let mut rx = FrameReceiver::new();
        let mut received: Option<RawFrame> = None;

        for &byte in encoded {
            if let Some(result) = rx.feed(byte) {
                received = Some(result.expect("CRC should be valid"));
                break;
            }
        }

        let r = received.unwrap();
        assert_eq!(r.cmd, 0x21);
        assert_eq!(r.seq, 42);
        assert_eq!(r.len, 5);
        assert_eq!(&r.payload[..5], &payload);
    }

    #[test]
    fn crc_mismatch_detected() {
        let frame = RawFrame::new(0x01, 1, &[0xDE, 0xAD]).unwrap();
        let mut buf = [0u8; 512];
        let encoded = frame.encode(&mut buf).unwrap();

        // Corrupt the CRC bytes
        let len = encoded.len();
        buf[len - 1] ^= 0xFF;

        let mut rx = FrameReceiver::new();
        let mut encoded_copy = buf[..len].to_vec();
        for &byte in &encoded_copy {
            if let Some(result) = rx.feed(byte) {
                assert!(result.is_err());
                return;
            }
        }
        panic!("Should have gotten a CRC error");
    }

    #[test]
    fn slip_roundtrip() {
        let data = [0xC0u8, 0xDB, 0x01, 0x02]; // contains SLIP_END and SLIP_ESC
        let mut encoded = [0u8; 20];
        let n = slip_encode(&data, &mut encoded).unwrap();
        // Verify SLIP_END and SLIP_ESC don't appear raw in encoded[1..n-1]
        for &b in &encoded[1..n-1] {
            assert!(b != SLIP_END, "SLIP_END appeared raw in encoded payload");
        }
    }

    #[test]
    fn builder_api() {
        let frame = FrameBuilder::new(Cmd::SensorData, 7)
            .push_u16_le(2345)  // temperature in centidegrees
            .push_u16_le(10132) // pressure in Pa
            .payload(&[55])     // humidity %  (illustrative: builder resets payload)
            .build()
            .unwrap();
        assert_eq!(frame.cmd, Cmd::SensorData as u8);
        assert_eq!(frame.seq, 7);
    }
}
```

---

## Comparison Table

| Strategy | Overhead | Sync Recovery | Payload Transparency | Complexity | Best For |
|---|---|---|---|---|---|
| End delimiter only | 1 byte | Fast | Requires escaping | Low | ASCII text protocols, NMEA |
| Start + End delimiter | 2 bytes | Very fast | Requires escaping | Low–Medium | HDLC, PPP, SLIP |
| Length prefix only | 1–2 bytes | Slow (re-sync hard) | Full | Low | Trusted links, USB CDC |
| Fixed length | 0 bytes | Never (once lost) | Full | Very Low | Fixed sensor readouts |
| Hybrid (SOF+LEN+CRC) | 5–7 bytes | Fast (scan SOF) | Full | Medium | **Most embedded UART** |
| COBS | ≤1 byte/254 | Very fast (`0x00`) | Full (no `0x00`) | Medium | CAN, USB serial, IoT |

---

## Summary

UART framing protocols bridge the gap between the raw byte stream delivered by UART hardware and the structured messages needed by application code. The choice of framing strategy involves trade-offs between overhead, synchronization robustness, payload transparency, and implementation complexity.

**Key design principles:**

**Synchronization anchor:** Always include a start-of-frame marker (magic bytes) that is detectable by scanning. This allows the receiver to re-synchronize after noise, power glitches, or cold starts without requiring a reset of the link.

**Length before payload:** A length field eliminates the need to scan for an end delimiter within the payload, removing the risk of false matches and the cost of byte escaping. It also allows the receiver to allocate or prepare a buffer of the exact right size.

**Integrity verification:** A CRC (preferably CRC-16-CCITT) over the header and payload detects the vast majority of transmission errors. A plain checksum or XOR is inadequate for production use on noisy links.

**State machine receiver:** Parse incoming bytes with an explicit finite state machine, not ad-hoc logic. This handles partial reception, timeouts, and error recovery cleanly. Each state transition should be deterministic and the machine should always converge back to a known state after any error.

**Byte stuffing when needed:** If delimiter-based framing is chosen, use a well-tested escaping scheme (SLIP or COBS) rather than inventing one. COBS is strongly preferred for new designs due to its bounded overhead and guaranteed delimiter-free output.

**Sequence numbers:** Add a 1-byte rolling sequence number to detect dropped frames. This costs one byte but provides invaluable diagnostic information and enables higher-level retransmission schemes.

**In C/C++**, implement the state machine as an explicit enum with a per-byte dispatch function, keeping all mutable state in a context struct. This is ISR-friendly, re-entrant if contexts are per-channel, and straightforward to unit test.

**In Rust**, the borrow checker enforces correct ownership of the receive buffer and frame data, the type system enforces exhaustive match on all states, and `#![no_std]` compatibility is achievable with fixed-size arrays — making Rust an excellent choice for safety-critical embedded framing implementations.

---

*Document: 36 — UART Framing Protocols | Series: Embedded UART Programming*