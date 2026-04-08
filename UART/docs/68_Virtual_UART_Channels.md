# 68. Virtual UART Channels

- **Introduction & Motivation** — why physical UART scarcity drives the need for multiplexing, with a use-case table
- **Protocol Design** — frame format decisions (SOF, channel ID, length, CRC-8), byte stuffing (SLIP-style), and trade-offs
- **C Implementation** — a complete `vuart.h`/`vuart.c` with a byte-stuffing transmitter, a 5-state RX state machine, and a C++ wrapper class using `std::function` callbacks
- **Rust Implementation** — a `no_std`-compatible design using `heapless::Vec`, separated `Mux`/`Demux` types, full unit tests covering round-trips, SOF-in-payload stuffing, CRC corruption, and multi-frame feeds
- **Flow Control** — XON/XOFF and credit-based schemes with code sketches
- **Error Handling** — sequence numbers, resynchronisation timeouts, watchdog integration
- **RTOS Integration** — FreeRTOS task-per-channel pattern and DMA-driven TX
- **Testing & Debugging** — loopback tests, Wireshark dissector suggestion, statistics counters
- **Summary** — concise recap of all design decisions and real-world protocol analogues (GSM 07.10, BT HCI UART)


## Implementing Multiple Logical Channels over a Single Physical UART

---

## Table of Contents

1. [Introduction](#introduction)
2. [Motivation and Use Cases](#motivation-and-use-cases)
3. [Core Concepts](#core-concepts)
4. [Protocol Design](#protocol-design)
5. [Framing and Packet Structure](#framing-and-packet-structure)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Flow Control and Back-pressure](#flow-control-and-back-pressure)
9. [Error Handling and Recovery](#error-handling-and-recovery)
10. [RTOS Integration Patterns](#rtos-integration-patterns)
11. [Testing and Debugging](#testing-and-debugging)
12. [Summary](#summary)

---

## Introduction

A **Virtual UART Channel (VUC)** is a software abstraction that multiplexes multiple independent logical communication streams over a single physical UART hardware interface. Rather than requiring one UART peripheral per communication partner or protocol, a virtual channel layer allows a single wire pair (TX/RX) to carry structured, tagged data belonging to several concurrent logical connections.

This technique is widely used in embedded systems where hardware UART peripherals are scarce, and where it is necessary to simultaneously handle concerns such as debug logging, firmware update, sensor data streaming, command/response protocols, or inter-processor communication — all on the same physical pins.

---

## Motivation and Use Cases

### Why Multiplex?

Microcontrollers often expose only one or two UART peripherals, yet modern embedded designs require communication with:

- A host PC for debug logging
- A Bluetooth or Wi-Fi module for wireless connectivity
- An RTOS shell or command interface
- A secondary microcontroller or co-processor
- A firmware update (DFU) channel

Without virtual channels, each of these would demand a dedicated UART, quickly exhausting hardware resources. Virtual channels solve this by time-sharing the physical link.

### Common Application Scenarios

| Scenario | Channels Needed |
|---|---|
| Embedded Linux + bare-metal MCU bridge | Debug, data, control |
| GPS/GSM modem with AT command layer | AT commands, raw NMEA, SMS data |
| BLE module with extended HCI | HCI commands, HCI events, ACL data |
| Multi-sensor IoT node | Sensor A, Sensor B, OTA update, logging |
| Automotive ECU diagnostics | UDS, logging, calibration, flashing |

---

## Core Concepts

### The Multiplexer/Demultiplexer Model

The virtual channel system consists of two cooperating components:

```
Transmit side                         Receive side
┌────────┐   ┌─────────┐             ┌──────────┐   ┌────────┐
│ Chan 0 ├──►│         │             │          ├──►│ Chan 0 │
│ Chan 1 ├──►│  MUX    ├──► UART ───►│  DEMUX   ├──►│ Chan 1 │
│ Chan 2 ├──►│         │             │          ├──►│ Chan 2 │
└────────┘   └─────────┘             └──────────┘   └────────┘
```

Each logical channel is identified by a **channel ID** embedded in a frame header. The multiplexer wraps outgoing bytes with this header, and the demultiplexer at the far end reads the header to route received bytes to the correct channel buffer.

### Synchronization and State Machine

The receiver operates as a state machine, scanning the byte stream for:

1. **Start-of-frame (SOF)** marker
2. **Channel ID** field
3. **Payload length** field
4. **Payload data** bytes
5. **End-of-frame (EOF)** marker or checksum

---

## Protocol Design

### Design Decisions

Before implementing, decide:

- **Fixed vs. variable frame size:** Fixed frames simplify state machines but waste bandwidth. Variable frames are more efficient but require length fields.
- **In-band vs. out-of-band framing:** HDLC-style byte stuffing or SLIP encoding allows arbitrary payload content without confusion with framing bytes.
- **Error detection:** CRC-8, CRC-16, or a simple checksum provides corruption detection.
- **Flow control:** Per-channel credits or XON/XOFF can prevent fast producers from overwhelming slow consumers.

### Reference Frame Format

```
┌────────┬────────────┬──────────┬───────────────────┬─────────┐
│  SOF   │  Chan ID   │  Length  │      Payload      │   CRC   │
│ 1 byte │   1 byte   │  1 byte  │   0–255 bytes     │  1 byte │
└────────┴────────────┴──────────┴───────────────────┴─────────┘
  0xAA      0x00–0x0F    0x01–0xFF    application data   CRC-8
```

- **SOF** = `0xAA` (not a valid byte-stuffed escape, chosen for high transition density)
- **Chan ID** = upper nibble reserved, lower nibble = channel (0–15)
- **Length** = payload byte count (NOT including header or CRC)
- **CRC** = CRC-8 over Chan ID + Length + Payload

---

## Framing and Packet Structure

### Byte Stuffing (SLIP-style Encoding)

To allow `0xAA` to appear in user payloads, apply byte stuffing:

- Replace `0xAA` in the payload with `0xBB 0x01`
- Replace `0xBB` in the payload with `0xBB 0x02`
- The receiver reverses this mapping after CRC verification

This ensures the SOF byte is unique in the raw byte stream.

### CRC-8 Algorithm

A Dallas/Maxim CRC-8 (polynomial `0x31`) is sufficient for frames up to 256 bytes:

```c
static const uint8_t crc8_table[256] = { /* precomputed */ };

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) crc = crc8_table[crc ^ *data++];
    return crc;
}
```

---

## Implementation in C/C++

### Header: `vuart.h`

```c
#ifndef VUART_H
#define VUART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define VUART_SOF           0xAAu
#define VUART_ESC           0xBBu
#define VUART_ESC_SOF       0x01u
#define VUART_ESC_ESC       0x02u
#define VUART_MAX_CHANNELS  8
#define VUART_MAX_PAYLOAD   64
#define VUART_FRAME_OVERHEAD 4   /* SOF + ChanID + Len + CRC */

/* Opaque buffer type: adapt to your platform ring buffer */
typedef struct ring_buf ring_buf_t;

typedef void (*vuart_rx_cb_t)(uint8_t channel, const uint8_t *data, uint8_t len, void *user);

typedef struct {
    ring_buf_t      *rx_buf[VUART_MAX_CHANNELS];
    vuart_rx_cb_t    rx_cb[VUART_MAX_CHANNELS];
    void            *rx_cb_user[VUART_MAX_CHANNELS];

    /* Physical UART write function pointer */
    int (*uart_write)(const uint8_t *buf, size_t len);

    /* Internal RX state machine */
    enum {
        RX_WAIT_SOF,
        RX_WAIT_CHANID,
        RX_WAIT_LEN,
        RX_READ_PAYLOAD,
        RX_WAIT_CRC
    } rx_state;

    uint8_t  rx_chan;
    uint8_t  rx_expected_len;
    uint8_t  rx_pos;
    uint8_t  rx_payload[VUART_MAX_PAYLOAD];
    bool     rx_escaped;
} vuart_ctx_t;

/* Initialise context; uart_write must be provided */
int  vuart_init(vuart_ctx_t *ctx, int (*uart_write)(const uint8_t *, size_t));

/* Register a receive callback for a channel */
int  vuart_register_channel(vuart_ctx_t *ctx, uint8_t channel,
                             vuart_rx_cb_t cb, void *user);

/* Send data on a channel; may be called from any task/ISR */
int  vuart_send(vuart_ctx_t *ctx, uint8_t channel,
                const uint8_t *data, uint8_t len);

/* Feed received raw bytes into the demultiplexer (call from UART RX ISR or DMA callback) */
void vuart_feed(vuart_ctx_t *ctx, const uint8_t *bytes, size_t count);

#endif /* VUART_H */
```

### Implementation: `vuart.c`

```c
#include "vuart.h"
#include <string.h>
#include <assert.h>

/* ---- CRC-8 (Dallas/Maxim, poly=0x31) ------------------------------------ */

static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        if ((crc ^ byte) & 0x80) {
            crc = (uint8_t)((crc << 1) ^ 0x31u);
        } else {
            crc <<= 1;
        }
        byte <<= 1;
    }
    return crc;
}

static uint8_t crc8_buf(const uint8_t *buf, size_t len)
{
    uint8_t crc = 0x00;
    while (len--) crc = crc8_update(crc, *buf++);
    return crc;
}

/* ---- Initialisation ------------------------------------------------------ */

int vuart_init(vuart_ctx_t *ctx, int (*uart_write)(const uint8_t *, size_t))
{
    if (!ctx || !uart_write) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->uart_write = uart_write;
    ctx->rx_state   = RX_WAIT_SOF;
    return 0;
}

int vuart_register_channel(vuart_ctx_t *ctx, uint8_t channel,
                            vuart_rx_cb_t cb, void *user)
{
    if (!ctx || channel >= VUART_MAX_CHANNELS) return -1;
    ctx->rx_cb[channel]      = cb;
    ctx->rx_cb_user[channel] = user;
    return 0;
}

/* ---- Transmit ------------------------------------------------------------ */

/*
 * Build a frame:
 *   [SOF][ChanID][Len][...stuffed payload...][CRC]
 *
 * Byte stuffing applied ONLY to the payload; CRC covers raw payload content.
 */
int vuart_send(vuart_ctx_t *ctx, uint8_t channel,
               const uint8_t *data, uint8_t len)
{
    if (!ctx || channel >= VUART_MAX_CHANNELS || !data || len == 0) return -1;
    if (len > VUART_MAX_PAYLOAD) return -1;

    /* Worst case: every byte in payload requires stuffing → 2× size */
    uint8_t frame[VUART_FRAME_OVERHEAD + VUART_MAX_PAYLOAD * 2];
    size_t  pos = 0;

    uint8_t crc = crc8_buf(data, len);  /* CRC over raw payload */

    frame[pos++] = VUART_SOF;
    frame[pos++] = channel & 0x0Fu;
    frame[pos++] = len;

    /* Stuff payload bytes */
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] == VUART_SOF) {
            frame[pos++] = VUART_ESC;
            frame[pos++] = VUART_ESC_SOF;
        } else if (data[i] == VUART_ESC) {
            frame[pos++] = VUART_ESC;
            frame[pos++] = VUART_ESC_ESC;
        } else {
            frame[pos++] = data[i];
        }
    }

    frame[pos++] = crc;

    return ctx->uart_write(frame, pos);
}

/* ---- Receive State Machine ----------------------------------------------- */

void vuart_feed(vuart_ctx_t *ctx, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t b = bytes[i];

        /* SOF byte always resets regardless of current state */
        if (b == VUART_SOF && ctx->rx_state != RX_READ_PAYLOAD) {
            ctx->rx_state   = RX_WAIT_CHANID;
            ctx->rx_escaped = false;
            continue;
        }

        switch (ctx->rx_state) {

        case RX_WAIT_SOF:
            /* Waiting; ignore all non-SOF bytes */
            break;

        case RX_WAIT_CHANID:
            if (b < VUART_MAX_CHANNELS) {
                ctx->rx_chan  = b;
                ctx->rx_state = RX_WAIT_LEN;
            } else {
                ctx->rx_state = RX_WAIT_SOF; /* invalid channel */
            }
            break;

        case RX_WAIT_LEN:
            if (b == 0 || b > VUART_MAX_PAYLOAD) {
                ctx->rx_state = RX_WAIT_SOF;
            } else {
                ctx->rx_expected_len = b;
                ctx->rx_pos          = 0;
                ctx->rx_escaped      = false;
                ctx->rx_state        = RX_READ_PAYLOAD;
            }
            break;

        case RX_READ_PAYLOAD:
            /* Handle byte unstuffing */
            if (b == VUART_SOF) {
                /* SOF in payload → frame error, restart */
                ctx->rx_state = RX_WAIT_CHANID;
                break;
            }
            if (b == VUART_ESC) {
                ctx->rx_escaped = true;
                break;
            }
            if (ctx->rx_escaped) {
                ctx->rx_escaped = false;
                if (b == VUART_ESC_SOF)      b = VUART_SOF;
                else if (b == VUART_ESC_ESC) b = VUART_ESC;
                else { ctx->rx_state = RX_WAIT_SOF; break; } /* unknown escape */
            }

            ctx->rx_payload[ctx->rx_pos++] = b;
            if (ctx->rx_pos == ctx->rx_expected_len) {
                ctx->rx_state = RX_WAIT_CRC;
            }
            break;

        case RX_WAIT_CRC: {
            uint8_t expected = crc8_buf(ctx->rx_payload, ctx->rx_expected_len);
            if (b == expected) {
                uint8_t ch = ctx->rx_chan;
                if (ctx->rx_cb[ch]) {
                    ctx->rx_cb[ch](ch, ctx->rx_payload,
                                   ctx->rx_expected_len,
                                   ctx->rx_cb_user[ch]);
                }
            }
            /* Whether CRC passes or fails, go back to looking for next frame */
            ctx->rx_state = RX_WAIT_SOF;
            break;
        }

        default:
            ctx->rx_state = RX_WAIT_SOF;
            break;
        }
    }
}
```

### Usage Example (C)

```c
#include "vuart.h"
#include <stdio.h>
#include <string.h>

/* Simulated UART write — replace with HAL_UART_Transmit() or similar */
static int platform_uart_write(const uint8_t *buf, size_t len)
{
    /* e.g. HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY); */
    return (int)len;
}

/* Callback invoked when data arrives on channel 0 (logging) */
static void on_log_channel(uint8_t ch, const uint8_t *data, uint8_t len, void *user)
{
    (void)ch; (void)user;
    printf("[LOG] %.*s\n", (int)len, data);
}

/* Callback for channel 1 (sensor data) */
static void on_sensor_channel(uint8_t ch, const uint8_t *data, uint8_t len, void *user)
{
    (void)ch; (void)user;
    if (len >= 4) {
        int32_t temperature;
        memcpy(&temperature, data, 4);
        printf("[SENSOR] temp = %d mdeg\n", (int)temperature);
    }
}

int main(void)
{
    vuart_ctx_t ctx;
    vuart_init(&ctx, platform_uart_write);

    vuart_register_channel(&ctx, 0, on_log_channel,    NULL);
    vuart_register_channel(&ctx, 1, on_sensor_channel, NULL);

    /* Send "Hello" on channel 0 */
    const char *msg = "Hello from channel 0";
    vuart_send(&ctx, 0, (const uint8_t *)msg, (uint8_t)strlen(msg));

    /* Send a 32-bit temperature value on channel 1 */
    int32_t temp_mdeg = 23500; /* 23.500 °C */
    vuart_send(&ctx, 1, (const uint8_t *)&temp_mdeg, sizeof(temp_mdeg));

    /* Simulate receiving a frame on channel 0 (loopback test) */
    uint8_t rx_frame[] = { 0xAA, 0x00, 0x05, 'H','e','l','l','o',
                           /* CRC calculated over payload: */ 0x00 /* placeholder */ };
    uint8_t crc = 0;
    /* In real code, pre-compute crc8("Hello", 5) and fill in rx_frame[8] */
    (void)crc;
    vuart_feed(&ctx, rx_frame, sizeof(rx_frame));

    return 0;
}
```

### C++ Wrapper Class

```cpp
#pragma once
#include "vuart.h"
#include <functional>
#include <cstring>

class VirtualUART {
public:
    using RxCallback = std::function<void(uint8_t, const uint8_t*, uint8_t)>;

    explicit VirtualUART(int (*uart_write)(const uint8_t*, size_t))
    {
        vuart_init(&m_ctx, uart_write);
    }

    bool registerChannel(uint8_t ch, RxCallback cb)
    {
        if (ch >= VUART_MAX_CHANNELS) return false;
        m_callbacks[ch] = std::move(cb);
        vuart_register_channel(&m_ctx, ch, &VirtualUART::staticRxCb, this);
        return true;
    }

    int send(uint8_t ch, const uint8_t *data, uint8_t len)
    {
        return vuart_send(&m_ctx, ch, data, len);
    }

    int send(uint8_t ch, const char *str)
    {
        return send(ch, reinterpret_cast<const uint8_t*>(str),
                    static_cast<uint8_t>(std::strlen(str)));
    }

    void feed(const uint8_t *bytes, size_t count)
    {
        vuart_feed(&m_ctx, bytes, count);
    }

private:
    static void staticRxCb(uint8_t ch, const uint8_t *data,
                           uint8_t len, void *user)
    {
        auto *self = static_cast<VirtualUART*>(user);
        if (self->m_callbacks[ch]) {
            self->m_callbacks[ch](ch, data, len);
        }
    }

    vuart_ctx_t m_ctx{};
    RxCallback  m_callbacks[VUART_MAX_CHANNELS]{};
};

/* --- Usage ---------------------------------------------------------------- */
/*
    VirtualUART vu(platform_uart_write);

    vu.registerChannel(0, [](uint8_t, const uint8_t *d, uint8_t l) {
        std::string s(reinterpret_cast<const char*>(d), l);
        std::cout << "[CH0] " << s << "\n";
    });

    vu.send(0, "Hello, channel 0!");
*/
```

---

## Implementation in Rust

### Cargo.toml Dependencies

```toml
[dependencies]
heapless = "0.8"      # no_std fixed-size collections
crc     = "3"         # CRC computation
```

### `vuart.rs` — Core Implementation

```rust
//! Virtual UART channel multiplexer / demultiplexer.
//!
//! Supports up to `MAX_CHANNELS` logical channels over a single byte stream.
//! Frame format: [SOF=0xAA][ChanID:u8][Len:u8][...stuffed payload...][CRC8]

use heapless::Vec;

pub const SOF: u8 = 0xAA;
pub const ESC: u8 = 0xBB;
pub const ESC_SOF: u8 = 0x01;
pub const ESC_ESC: u8 = 0x02;
pub const MAX_CHANNELS: usize = 8;
pub const MAX_PAYLOAD: usize = 64;

/// Errors returned by the virtual UART layer.
#[derive(Debug, PartialEq, Eq)]
pub enum VuartError {
    InvalidChannel,
    PayloadTooLarge,
    WriteError,
    FramingError,
}

/// CRC-8 (Dallas/Maxim, poly=0x31).
fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0x00;
    for &byte in data {
        let mut b = byte;
        for _ in 0..8 {
            if (crc ^ b) & 0x80 != 0 {
                crc = crc.wrapping_shl(1) ^ 0x31;
            } else {
                crc = crc.wrapping_shl(1);
            }
            b = b.wrapping_shl(1);
        }
    }
    crc
}

/// Receiver state machine states.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum RxState {
    WaitSof,
    WaitChanId,
    WaitLen,
    ReadPayload,
    WaitCrc,
}

/// A complete, validated incoming frame.
#[derive(Debug)]
pub struct Frame {
    pub channel: u8,
    pub payload: Vec<u8, MAX_PAYLOAD>,
}

/// Demultiplexer: consumes raw bytes and produces `Frame`s.
pub struct Demux {
    state:        RxState,
    rx_chan:      u8,
    rx_expected:  u8,
    rx_payload:   Vec<u8, MAX_PAYLOAD>,
    rx_escaped:   bool,
}

impl Default for Demux {
    fn default() -> Self {
        Self {
            state:       RxState::WaitSof,
            rx_chan:     0,
            rx_expected: 0,
            rx_payload:  Vec::new(),
            rx_escaped:  false,
        }
    }
}

impl Demux {
    pub fn new() -> Self { Self::default() }

    /// Feed raw received bytes. Returns any complete valid frames.
    pub fn feed<'a>(&mut self, bytes: &'a [u8]) -> Vec<Frame, 16> {
        let mut frames: Vec<Frame, 16> = Vec::new();

        for &b in bytes {
            // SOF always resets (except mid-payload to catch re-sync)
            if b == SOF && self.state != RxState::ReadPayload {
                self.state      = RxState::WaitChanId;
                self.rx_escaped = false;
                continue;
            }

            match self.state {
                RxState::WaitSof => { /* ignore */ }

                RxState::WaitChanId => {
                    if (b as usize) < MAX_CHANNELS {
                        self.rx_chan = b;
                        self.state  = RxState::WaitLen;
                    } else {
                        self.state = RxState::WaitSof;
                    }
                }

                RxState::WaitLen => {
                    if b == 0 || b as usize > MAX_PAYLOAD {
                        self.state = RxState::WaitSof;
                    } else {
                        self.rx_expected = b;
                        self.rx_payload.clear();
                        self.rx_escaped = false;
                        self.state = RxState::ReadPayload;
                    }
                }

                RxState::ReadPayload => {
                    // Byte unstuffing
                    let decoded = if self.rx_escaped {
                        self.rx_escaped = false;
                        match b {
                            ESC_SOF => Some(SOF),
                            ESC_ESC => Some(ESC),
                            _ => { self.state = RxState::WaitSof; None }
                        }
                    } else if b == ESC {
                        self.rx_escaped = true;
                        None
                    } else if b == SOF {
                        // Bare SOF in payload → framing error, re-sync
                        self.state = RxState::WaitChanId;
                        None
                    } else {
                        Some(b)
                    };

                    if let Some(raw) = decoded {
                        let _ = self.rx_payload.push(raw);
                        if self.rx_payload.len() == self.rx_expected as usize {
                            self.state = RxState::WaitCrc;
                        }
                    }
                }

                RxState::WaitCrc => {
                    let expected = crc8(&self.rx_payload);
                    if b == expected {
                        let frame = Frame {
                            channel: self.rx_chan,
                            payload: self.rx_payload.clone(),
                        };
                        let _ = frames.push(frame);
                    }
                    // Both CRC-pass and CRC-fail reset the state machine
                    self.state = RxState::WaitSof;
                }
            }
        }

        frames
    }
}

/// Multiplexer: builds framed, stuffed byte sequences ready for UART TX.
pub struct Mux;

impl Mux {
    /// Encode `channel` + `payload` into a frame.
    /// Returns a `Vec` of raw bytes to write to the physical UART.
    pub fn encode(
        channel: u8,
        payload: &[u8],
    ) -> Result<Vec<u8, { MAX_PAYLOAD * 2 + 4 }>, VuartError> {
        if channel as usize >= MAX_CHANNELS { return Err(VuartError::InvalidChannel); }
        if payload.len() > MAX_PAYLOAD      { return Err(VuartError::PayloadTooLarge); }

        let crc = crc8(payload);
        let mut buf: Vec<u8, { MAX_PAYLOAD * 2 + 4 }> = Vec::new();

        let _ = buf.push(SOF);
        let _ = buf.push(channel);
        let _ = buf.push(payload.len() as u8);

        // Byte stuffing
        for &b in payload {
            if b == SOF {
                let _ = buf.push(ESC);
                let _ = buf.push(ESC_SOF);
            } else if b == ESC {
                let _ = buf.push(ESC);
                let _ = buf.push(ESC_ESC);
            } else {
                let _ = buf.push(b);
            }
        }

        let _ = buf.push(crc);
        Ok(buf)
    }
}

/// High-level context combining Mux + Demux with a write callback.
pub struct VuartContext<W>
where
    W: FnMut(&[u8]) -> Result<(), VuartError>,
{
    demux:     Demux,
    uart_write: W,
}

impl<W> VuartContext<W>
where
    W: FnMut(&[u8]) -> Result<(), VuartError>,
{
    pub fn new(uart_write: W) -> Self {
        Self { demux: Demux::new(), uart_write }
    }

    pub fn send(&mut self, channel: u8, payload: &[u8]) -> Result<(), VuartError> {
        let frame = Mux::encode(channel, payload)?;
        (self.uart_write)(frame.as_slice())
    }

    /// Feed received bytes; returns validated frames.
    pub fn feed(&mut self, bytes: &[u8]) -> Vec<Frame, 16> {
        self.demux.feed(bytes)
    }
}
```

### Rust Usage Example

```rust
mod vuart;
use vuart::{VuartContext, VuartError};

fn main() {
    // Simulated UART write: collect bytes into a Vec for inspection
    let mut sent_bytes: Vec<u8> = Vec::new();

    let write_fn = |bytes: &[u8]| -> Result<(), VuartError> {
        sent_bytes.extend_from_slice(bytes);
        Ok(())
    };

    let mut ctx = VuartContext::new(write_fn);

    // Send a text message on channel 0
    ctx.send(0, b"Hello from Rust channel 0").unwrap();

    // Send binary sensor data on channel 1
    let temp_mdeg: i32 = 23_500;
    ctx.send(1, &temp_mdeg.to_le_bytes()).unwrap();

    println!("Sent {} bytes over virtual UART", sent_bytes.len());

    // Simulate receive path: encode a frame and feed it back
    let frame_bytes = vuart::Mux::encode(0, b"World").unwrap();
    let mut rx_ctx = VuartContext::new(|_| Ok(()));
    let frames = rx_ctx.feed(frame_bytes.as_slice());

    for frame in &frames {
        println!(
            "Received on channel {}: {:?}",
            frame.channel,
            core::str::from_utf8(&frame.payload).unwrap_or("<binary>")
        );
    }
}
```

### Rust Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::vuart::{Demux, Mux, SOF, ESC, ESC_SOF};

    #[test]
    fn roundtrip_simple_payload() {
        let payload = b"Hello";
        let frame   = Mux::encode(2, payload).unwrap();
        let mut demux = Demux::new();
        let frames = demux.feed(&frame);
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].channel, 2);
        assert_eq!(frames[0].payload.as_slice(), payload);
    }

    #[test]
    fn roundtrip_payload_containing_sof() {
        // Payload includes 0xAA (SOF) and 0xBB (ESC)
        let payload: &[u8] = &[0x01, SOF, 0x02, ESC, 0x03];
        let frame   = Mux::encode(3, payload).unwrap();
        let mut demux = Demux::new();
        let frames = demux.feed(&frame);
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].payload.as_slice(), payload);
    }

    #[test]
    fn crc_error_drops_frame() {
        let mut frame = Mux::encode(0, b"Test").unwrap();
        // Corrupt the CRC byte (last byte)
        let last = frame.len() - 1;
        frame[last] ^= 0xFF;
        let mut demux = Demux::new();
        let frames = demux.feed(&frame);
        assert_eq!(frames.len(), 0);
    }

    #[test]
    fn multiple_frames_in_one_feed() {
        let f1 = Mux::encode(0, b"Ping").unwrap();
        let f2 = Mux::encode(1, b"Pong").unwrap();
        let mut combined = f1.to_vec();
        combined.extend_from_slice(&f2);
        let mut demux = Demux::new();
        let frames = demux.feed(&combined);
        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].channel, 0);
        assert_eq!(frames[1].channel, 1);
    }
}
```

---

## Flow Control and Back-pressure

Without flow control, a fast sender can overwhelm a slow receiver's channel buffers. Two strategies apply:

### XON/XOFF Per-Channel (Software)

Reserve channel 15 as a control channel. Receivers send a 1-byte control frame:

```
[0xAA][0x0F][0x02][CHAN_ID][STATE][CRC]
   SOF  ctrl  len                  crc
   
STATE: 0x00 = XOFF (pause), 0x01 = XON (resume)
```

The transmitter maintains a `paused[MAX_CHANNELS]` flag and holds outgoing frames when set.

### Credit-Based Flow Control

A more robust scheme issues credits:

```c
typedef struct {
    uint8_t tx_credits[VUART_MAX_CHANNELS];  /* how many frames we may send */
    uint8_t rx_credits[VUART_MAX_CHANNELS];  /* credits we have issued to peer */
} flow_ctx_t;

/* Sender: decrement before sending, block (or queue) if zero */
/* Receiver: send CREDIT frames after consuming from rx buffer */
```

Credit-based flow control is used by GSM 07.10 multiplexer (on which many Bluetooth HCI UART transports are based) and is the most reliable approach for embedded systems.

---

## Error Handling and Recovery

### Frame Loss Detection

Add a per-channel 8-bit sequence number in the channel ID byte (upper nibble):

```
Byte 1: [seq:4][chan_id:4]
```

The receiver detects gaps in sequence numbers and can request retransmission or simply log the loss.

### Resynchronisation

When a framing error is detected, the receiver must discard bytes until the next valid SOF. A timeout mechanism ensures that a half-received frame does not block the channel indefinitely:

```c
/* In ISR or polling loop: */
if (time_since_last_byte_ms > FRAME_TIMEOUT_MS) {
    ctx->rx_state = RX_WAIT_SOF;  /* force resync */
}
```

### Watchdog Integration

For safety-critical channels, a watchdog counter per channel can trigger an alert if no frame is received within an expected period.

---

## RTOS Integration Patterns

### FreeRTOS Task per Channel

```c
void channel0_task(void *arg)
{
    vuart_ctx_t *ctx = (vuart_ctx_t *)arg;
    uint8_t     buf[VUART_MAX_PAYLOAD];

    for (;;) {
        /* Block on channel-specific queue populated by vuart_feed() callback */
        if (xQueueReceive(ch0_rx_queue, buf, portMAX_DELAY) == pdTRUE) {
            /* process buf */
        }
    }
}
```

The `vuart_rx_cb_t` for each channel pushes to its respective FreeRTOS queue. This decouples the ISR-driven demultiplexer from application-layer processing.

### DMA-Driven TX

On STM32 or similar, DMA greatly reduces CPU overhead for transmission:

```c
int platform_uart_write_dma(const uint8_t *buf, size_t len)
{
    /* Wait for previous DMA transfer to complete */
    while (HAL_DMA_GetState(&hdma_usart1_tx) == HAL_DMA_STATE_BUSY);
    memcpy(dma_tx_buf, buf, len);
    HAL_UART_Transmit_DMA(&huart1, dma_tx_buf, (uint16_t)len);
    return (int)len;
}
```

---

## Testing and Debugging

### Loopback Test

Connect TX to RX on the same device and verify every frame sent is received intact on the expected channel:

```c
/* After vuart_send(), call vuart_feed() with the same bytes.
   All callbacks should fire with the original payloads. */
```

### Wireshark Dissector

A Python-based Wireshark LUA dissector can decode the multiplexed stream from a USB-UART capture, enabling protocol-level debugging without modifying the firmware.

### Statistics Counters

Maintain per-channel counters for:

- `tx_frames`, `tx_bytes`
- `rx_frames`, `rx_bytes`
- `rx_crc_errors`
- `rx_framing_errors`
- `rx_dropped_frames` (buffer overflow)

Expose via the logging channel or a dedicated diagnostics channel for runtime inspection.

---

## Summary

Virtual UART Channels solve the common embedded problem of having too many communication needs for the available UART hardware. The key design elements are:

**Frame structure:** A lightweight header (SOF + channel ID + length) wraps each payload, and a CRC trailer detects corruption. Byte stuffing ensures the SOF marker is unique in the raw byte stream.

**State machine demultiplexer:** A compact, interrupt-safe state machine processes each incoming byte, reconstructing and validating frames before dispatching them to the correct application-layer callback.

**C/C++ implementation** centres on a `vuart_ctx_t` structure with function-pointer injection for the physical UART write operation, making it portable across HAL layers. A C++ class wraps the C core with `std::function` callbacks.

**Rust implementation** leverages `heapless` collections for `no_std` compatibility, expresses the state machine with exhaustive `match` arms, and separates `Mux` (encoding) and `Demux` (decoding) into distinct types for clear ownership semantics.

**Flow control** (XON/XOFF or credit-based) prevents buffer overflow when channel consumers are slower than producers.

**RTOS integration** typically assigns one task per channel, using the demultiplexer callback to post messages to a channel-specific queue, decoupling ISR context from application logic.

Virtual UART channels are the foundation of well-known protocols including GSM 07.10 (used by all AT-command modems), Bluetooth HCI UART transport, and custom inter-processor communication layers in automotive and industrial embedded systems.

---

*Document version 1.0 — Virtual UART Channels (Topic 68)*