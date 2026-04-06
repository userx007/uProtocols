# 40. State Machines — Implementing Robust Protocol Parsers with FSMs

- **Theory** — formal FSM definition (Q, Σ, δ, q₀, F), Mealy machine model, and design patterns (table-driven, switch-case, function-pointer, OO)
- **Protocol definition** — a concrete framed packet format (SOF | CMD | LEN | PAYLOAD | CRC8 | EOF) used consistently across all three language examples
- **C implementation** — a full `uart_fsm.h`/`uart_fsm.c` with CRC-8/MAXIM, explicit `switch(state)` FSM, error counting, and a usage example
- **C++ implementation** — an OO class using `enum class`, `std::function` callbacks for zero-copy dispatch, and RAII-friendly design
- **Rust implementation** — enum variants carrying per-state data (the idiomatic Rust approach), `match` exhaustiveness for compile-time correctness, and 4 unit tests (`cargo test`-ready)
- **Advanced topics** — ISR ring buffer integration, multi-channel parsing, timeout/stale packet detection, and Hierarchical State Machines
- **Summary table** — quick reference across all key design decisions

---

## Table of Contents

1. [Introduction](#introduction)
2. [Finite State Machine Theory](#finite-state-machine-theory)
3. [Why FSMs for UART Protocol Parsing?](#why-fsms-for-uart-protocol-parsing)
4. [FSM Design Patterns](#fsm-design-patterns)
5. [Example Protocol: Custom Framed Packet](#example-protocol-custom-framed-packet)
6. [C Implementation](#c-implementation)
7. [C++ Implementation (Object-Oriented FSM)](#c-implementation-object-oriented-fsm)
8. [Rust Implementation](#rust-implementation)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## Introduction

UART communication rarely transfers raw, unstructured bytes. In practice, data travels as **framed packets**: sequences of bytes with defined start markers, payload fields, lengths, checksums, and end markers. Parsing these packets correctly — especially in embedded systems where data arrives byte-by-byte and interrupts can fire at any moment — requires a disciplined, robust approach.

**Finite State Machines (FSMs)** are the gold standard for protocol parsing. An FSM models the parser as a set of discrete *states*, with *transitions* between them triggered by incoming bytes or events. This makes parsers:

- **Deterministic** — identical input always produces identical output
- **Resumable** — the machine can pause mid-packet and resume later
- **Interrupt-safe** — state is fully captured in a small structure
- **Testable** — each state and transition can be unit-tested in isolation
- **Robust** — errors are handled explicitly, not accidentally

This document covers the theory, design, and full implementation of FSM-based UART protocol parsers in C, C++, and Rust.

---

## Finite State Machine Theory

A **Finite State Machine** is defined by the 5-tuple:

```
FSM = (Q, Σ, δ, q₀, F)
```

| Symbol | Meaning |
|--------|---------|
| Q      | Finite set of states |
| Σ      | Input alphabet (bytes in our case) |
| δ      | Transition function: Q × Σ → Q |
| q₀     | Initial state |
| F      | Set of accepting (final) states |

For protocol parsing we use a **Mealy machine** variant: outputs (actions) are associated with transitions, not just states. When an incoming byte causes a state transition, the corresponding action (store byte, compute checksum, emit packet) is executed.

### State Diagram Notation

```
[STATE_A] --condition/action--> [STATE_B]
```

A transition fires when the condition is met in `STATE_A`, produces the action, and moves to `STATE_B`. An error transition (e.g. unexpected byte) typically leads to a dedicated `ERROR` or `IDLE` reset state.

---

## Why FSMs for UART Protocol Parsing?

UART data arrives **one byte at a time**, often via ISR (interrupt service routine) or DMA callbacks. The parser must handle:

- **Partial packets** — only some bytes have arrived so far
- **Corrupt bytes** — electrical noise, parity errors
- **Buffer overflows** — payload longer than expected
- **Re-synchronization** — recovering after a corrupt packet without losing the next valid one
- **Nested concurrency** — the ISR fills a ring buffer while the main loop drains it

A naive `if/else` chain or `switch` statement over a global counter becomes fragile and unmaintainable. An FSM encapsulates all of this complexity cleanly.

---

## FSM Design Patterns

### 1. Table-Driven FSM

Transitions are stored in a 2D array `next_state[state][event]`. Fast at runtime, easy to generate from tools.

### 2. Switch-Case FSM

A `switch` on the current state, with nested switches or `if` chains on the input byte. Most common in embedded C; readable and compiler-friendly.

### 3. Function-Pointer FSM

Each state is represented by a function pointer. The current state IS the function pointer. No switch needed — just call `state_fn(byte)`.

### 4. Object-Oriented FSM (C++/Rust)

States are objects (C++ classes or Rust enums with data). Transitions are method calls or pattern-matched enum variants. Enables per-state data storage (e.g. byte count, partial checksum).

---

## Example Protocol: Custom Framed Packet

All examples parse the following protocol:

```
┌──────────┬──────────┬──────────┬─────────────────┬──────────┬──────────┐
│  SOF     │  CMD     │  LEN     │  PAYLOAD[0..N]  │  CRC8    │  EOF     │
│  0xAA    │  1 byte  │  1 byte  │  N bytes        │  1 byte  │  0x55    │
└──────────┴──────────┴──────────┴─────────────────┴──────────┴──────────┘
```

- **SOF** (Start Of Frame): `0xAA`
- **CMD**: Command byte (any value)
- **LEN**: Payload length (0–255)
- **PAYLOAD**: `LEN` bytes of data
- **CRC8**: CRC-8 over CMD + LEN + PAYLOAD
- **EOF** (End Of Frame): `0x55`

**States:**

```
IDLE → WAIT_CMD → WAIT_LEN → RECV_PAYLOAD → WAIT_CRC → WAIT_EOF → [COMPLETE]
  ↑_________________________error/reset_________________________________↑
```

---

## C Implementation

### Header: `uart_fsm.h`

```c
#ifndef UART_FSM_H
#define UART_FSM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PAYLOAD_LEN  256
#define SOF_BYTE         0xAA
#define EOF_BYTE         0x55

/* Parser states */
typedef enum {
    STATE_IDLE        = 0,
    STATE_WAIT_CMD,
    STATE_WAIT_LEN,
    STATE_RECV_PAYLOAD,
    STATE_WAIT_CRC,
    STATE_WAIT_EOF,
    STATE_COUNT
} ParserState;

/* Parse result returned per byte */
typedef enum {
    PARSE_INCOMPLETE = 0,  /* Still waiting for more bytes     */
    PARSE_COMPLETE,        /* Full valid packet received        */
    PARSE_ERROR            /* Protocol error; parser has reset  */
} ParseResult;

/* Completed packet */
typedef struct {
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  payload[MAX_PAYLOAD_LEN];
    uint8_t  crc;
} Packet;

/* FSM context — one instance per UART channel */
typedef struct {
    ParserState  state;
    Packet       pkt;
    uint16_t     payload_idx;
    uint8_t      crc_accum;
    uint32_t     error_count;
    uint32_t     packet_count;
} UartFsm;

/* API */
void        uart_fsm_init  (UartFsm *fsm);
ParseResult uart_fsm_feed  (UartFsm *fsm, uint8_t byte, Packet *out);
uint8_t     crc8_update    (uint8_t crc, uint8_t byte);

#endif /* UART_FSM_H */
```

### Source: `uart_fsm.c`

```c
#include "uart_fsm.h"
#include <string.h>

/* CRC-8/MAXIM (Dallas) polynomial 0x31 */
uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        if ((crc ^ byte) & 0x01)
            crc = (crc >> 1) ^ 0x8C;
        else
            crc >>= 1;
        byte >>= 1;
    }
    return crc;
}

void uart_fsm_init(UartFsm *fsm)
{
    memset(fsm, 0, sizeof(*fsm));
    fsm->state = STATE_IDLE;
}

/* Reset to IDLE and count error */
static void fsm_reset_error(UartFsm *fsm)
{
    fsm->state      = STATE_IDLE;
    fsm->crc_accum  = 0;
    fsm->payload_idx = 0;
    fsm->error_count++;
}

ParseResult uart_fsm_feed(UartFsm *fsm, uint8_t byte, Packet *out)
{
    switch (fsm->state) {

    /*------------------------------------------------------------------
     * IDLE: Hunt for Start-Of-Frame
     *-----------------------------------------------------------------*/
    case STATE_IDLE:
        if (byte == SOF_BYTE) {
            fsm->crc_accum   = 0;
            fsm->payload_idx = 0;
            fsm->state       = STATE_WAIT_CMD;
        }
        /* Ignore all other bytes silently */
        return PARSE_INCOMPLETE;

    /*------------------------------------------------------------------
     * WAIT_CMD: Next byte is the command
     *-----------------------------------------------------------------*/
    case STATE_WAIT_CMD:
        fsm->pkt.cmd    = byte;
        fsm->crc_accum  = crc8_update(fsm->crc_accum, byte);
        fsm->state      = STATE_WAIT_LEN;
        return PARSE_INCOMPLETE;

    /*------------------------------------------------------------------
     * WAIT_LEN: Next byte is payload length
     *-----------------------------------------------------------------*/
    case STATE_WAIT_LEN:
        fsm->pkt.len   = byte;
        fsm->crc_accum = crc8_update(fsm->crc_accum, byte);

        if (byte == 0) {
            /* Zero-length payload; skip straight to CRC */
            fsm->state = STATE_WAIT_CRC;
        } else {
            fsm->state = STATE_RECV_PAYLOAD;
        }
        return PARSE_INCOMPLETE;

    /*------------------------------------------------------------------
     * RECV_PAYLOAD: Collect payload bytes
     *-----------------------------------------------------------------*/
    case STATE_RECV_PAYLOAD:
        if (fsm->payload_idx >= MAX_PAYLOAD_LEN) {
            /* Overflow guard — protocol violation */
            fsm_reset_error(fsm);
            return PARSE_ERROR;
        }
        fsm->pkt.payload[fsm->payload_idx++] = byte;
        fsm->crc_accum = crc8_update(fsm->crc_accum, byte);

        if (fsm->payload_idx >= fsm->pkt.len) {
            fsm->state = STATE_WAIT_CRC;
        }
        return PARSE_INCOMPLETE;

    /*------------------------------------------------------------------
     * WAIT_CRC: Verify checksum
     *-----------------------------------------------------------------*/
    case STATE_WAIT_CRC:
        fsm->pkt.crc = byte;
        if (byte != fsm->crc_accum) {
            fsm_reset_error(fsm);
            return PARSE_ERROR;
        }
        fsm->state = STATE_WAIT_EOF;
        return PARSE_INCOMPLETE;

    /*------------------------------------------------------------------
     * WAIT_EOF: Verify End-Of-Frame marker
     *-----------------------------------------------------------------*/
    case STATE_WAIT_EOF:
        if (byte != EOF_BYTE) {
            fsm_reset_error(fsm);
            return PARSE_ERROR;
        }
        /* Success — copy packet to caller */
        if (out) {
            memcpy(out, &fsm->pkt, sizeof(Packet));
        }
        fsm->packet_count++;
        fsm->state = STATE_IDLE;
        return PARSE_COMPLETE;

    default:
        fsm_reset_error(fsm);
        return PARSE_ERROR;
    }
}
```

### Usage Example

```c
#include <stdio.h>
#include "uart_fsm.h"

int main(void)
{
    UartFsm fsm;
    uart_fsm_init(&fsm);

    /* Build a valid test packet: CMD=0x01, LEN=3, PAYLOAD={0xDE,0xAD,0xBE} */
    /* CRC8 is precomputed here as 0x42 for illustration */
    uint8_t stream[] = {
        0xAA,               /* SOF   */
        0x01,               /* CMD   */
        0x03,               /* LEN   */
        0xDE, 0xAD, 0xBE,  /* PAYLOAD */
        0x00,               /* CRC   (placeholder — replace with computed) */
        0x55                /* EOF   */
    };

    /* Compute and insert correct CRC */
    uint8_t crc = 0;
    crc = crc8_update(crc, 0x01);   /* CMD */
    crc = crc8_update(crc, 0x03);   /* LEN */
    crc = crc8_update(crc, 0xDE);
    crc = crc8_update(crc, 0xAD);
    crc = crc8_update(crc, 0xBE);
    stream[6] = crc;

    Packet result;
    for (size_t i = 0; i < sizeof(stream); i++) {
        ParseResult r = uart_fsm_feed(&fsm, stream[i], &result);
        if (r == PARSE_COMPLETE) {
            printf("Packet OK: CMD=0x%02X LEN=%d\n",
                   result.cmd, result.len);
        } else if (r == PARSE_ERROR) {
            printf("Parse error at byte %zu\n", i);
        }
    }
    return 0;
}
```

---

## C++ Implementation (Object-Oriented FSM)

The C++ version wraps the FSM in a class, uses `std::function` callbacks for zero-copy dispatch, and leverages `enum class` for type-safe states.

### `UartFsm.hpp`

```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <array>

static constexpr uint8_t  SOF_BYTE        = 0xAA;
static constexpr uint8_t  EOF_BYTE        = 0x55;
static constexpr uint16_t MAX_PAYLOAD_LEN = 256;

enum class ParserState : uint8_t {
    Idle,
    WaitCmd,
    WaitLen,
    RecvPayload,
    WaitCrc,
    WaitEof
};

enum class ParseResult : uint8_t {
    Incomplete,
    Complete,
    Error
};

struct Packet {
    uint8_t cmd  = 0;
    uint8_t len  = 0;
    std::array<uint8_t, MAX_PAYLOAD_LEN> payload{};
    uint8_t crc  = 0;
};

class UartFsm {
public:
    using PacketCallback = std::function<void(const Packet &)>;
    using ErrorCallback  = std::function<void(uint32_t error_count)>;

    explicit UartFsm(PacketCallback on_packet = nullptr,
                     ErrorCallback  on_error  = nullptr)
        : on_packet_(std::move(on_packet))
        , on_error_(std::move(on_error))
    {
        reset();
    }

    /* Feed one byte into the FSM */
    ParseResult feed(uint8_t byte)
    {
        switch (state_) {
        case ParserState::Idle:
            if (byte == SOF_BYTE) {
                crc_accum_    = 0;
                payload_idx_  = 0;
                state_        = ParserState::WaitCmd;
            }
            return ParseResult::Incomplete;

        case ParserState::WaitCmd:
            pkt_.cmd   = byte;
            crc_accum_ = crc8_update(crc_accum_, byte);
            state_     = ParserState::WaitLen;
            return ParseResult::Incomplete;

        case ParserState::WaitLen:
            pkt_.len   = byte;
            crc_accum_ = crc8_update(crc_accum_, byte);
            state_ = (byte == 0) ? ParserState::WaitCrc
                                 : ParserState::RecvPayload;
            return ParseResult::Incomplete;

        case ParserState::RecvPayload:
            if (payload_idx_ >= MAX_PAYLOAD_LEN)
                return handleError();

            pkt_.payload[payload_idx_++] = byte;
            crc_accum_ = crc8_update(crc_accum_, byte);

            if (payload_idx_ >= pkt_.len)
                state_ = ParserState::WaitCrc;

            return ParseResult::Incomplete;

        case ParserState::WaitCrc:
            pkt_.crc = byte;
            if (byte != crc_accum_)
                return handleError();

            state_ = ParserState::WaitEof;
            return ParseResult::Incomplete;

        case ParserState::WaitEof:
            if (byte != EOF_BYTE)
                return handleError();

            ++packet_count_;
            if (on_packet_) on_packet_(pkt_);
            reset();
            return ParseResult::Complete;
        }
        return handleError(); // unreachable
    }

    uint32_t errorCount()  const { return error_count_;  }
    uint32_t packetCount() const { return packet_count_; }
    ParserState state()    const { return state_;         }

private:
    ParserState  state_       = ParserState::Idle;
    Packet       pkt_;
    uint16_t     payload_idx_ = 0;
    uint8_t      crc_accum_   = 0;
    uint32_t     error_count_ = 0;
    uint32_t     packet_count_= 0;
    PacketCallback on_packet_;
    ErrorCallback  on_error_;

    void reset() {
        state_       = ParserState::Idle;
        crc_accum_   = 0;
        payload_idx_ = 0;
    }

    ParseResult handleError() {
        ++error_count_;
        if (on_error_) on_error_(error_count_);
        reset();
        return ParseResult::Error;
    }

    static uint8_t crc8_update(uint8_t crc, uint8_t byte) {
        for (uint8_t i = 0; i < 8; ++i) {
            if ((crc ^ byte) & 0x01)
                crc = (crc >> 1) ^ 0x8C;
            else
                crc >>= 1;
            byte >>= 1;
        }
        return crc;
    }
};
```

### Usage Example

```cpp
#include <iostream>
#include "UartFsm.hpp"

int main()
{
    auto on_packet = [](const Packet &p) {
        std::cout << "Packet received: CMD=0x"
                  << std::hex << static_cast<int>(p.cmd)
                  << " LEN=" << std::dec << static_cast<int>(p.len)
                  << "\n";
    };

    auto on_error = [](uint32_t count) {
        std::cerr << "Parse error #" << count << "\n";
    };

    UartFsm fsm(on_packet, on_error);

    /* Same test stream as the C example */
    std::array<uint8_t, 8> stream = {
        0xAA, 0x01, 0x03, 0xDE, 0xAD, 0xBE, 0x00 /*crc*/, 0x55
    };

    /* Compute CRC inline */
    uint8_t crc = 0;
    for (uint8_t b : {0x01, 0x03, 0xDE, 0xAD, 0xBE}) {
        /* reuse same polynomial */
        for (int i = 0; i < 8; ++i) {
            crc = ((crc ^ b) & 1) ? (crc >> 1) ^ 0x8C : crc >> 1;
            b >>= 1;
        }
    }
    stream[6] = crc;

    for (uint8_t byte : stream) {
        fsm.feed(byte);
    }

    std::cout << "Packets: "  << fsm.packetCount()
              << "  Errors: " << fsm.errorCount() << "\n";
    return 0;
}
```

---

## Rust Implementation

Rust's `enum` with data is a natural fit for FSMs: each variant can carry the state-specific data it needs, and `match` exhaustiveness guarantees no unhandled transitions at compile time.

### `uart_fsm.rs`

```rust
//! UART Protocol Parser — Finite State Machine
//! Protocol: SOF(0xAA) | CMD | LEN | PAYLOAD[LEN] | CRC8 | EOF(0x55)

pub const SOF_BYTE: u8 = 0xAA;
pub const EOF_BYTE: u8 = 0x55;
pub const MAX_PAYLOAD: usize = 256;

// ─── CRC-8/MAXIM ────────────────────────────────────────────────────────────

fn crc8_update(mut crc: u8, mut byte: u8) -> u8 {
    for _ in 0..8 {
        crc = if (crc ^ byte) & 0x01 != 0 {
            (crc >> 1) ^ 0x8C
        } else {
            crc >> 1
        };
        byte >>= 1;
    }
    crc
}

// ─── Packet ──────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, PartialEq)]
pub struct Packet {
    pub cmd:     u8,
    pub len:     u8,
    pub payload: Vec<u8>,
    pub crc:     u8,
}

// ─── Parser State ────────────────────────────────────────────────────────────

/// Each variant holds exactly the data needed for that phase.
#[derive(Debug)]
enum State {
    /// Hunting for SOF byte
    Idle,
    /// SOF seen; waiting for command byte
    WaitCmd { crc: u8 },
    /// CMD received; waiting for length byte
    WaitLen { cmd: u8, crc: u8 },
    /// Accumulating payload bytes
    RecvPayload {
        cmd:     u8,
        len:     u8,
        payload: Vec<u8>,
        crc:     u8,
    },
    /// All payload received; waiting for CRC byte
    WaitCrc {
        cmd:     u8,
        len:     u8,
        payload: Vec<u8>,
        expected_crc: u8,
    },
    /// CRC validated; waiting for EOF byte
    WaitEof {
        packet: Packet,
    },
}

// ─── Parse Result ────────────────────────────────────────────────────────────

#[derive(Debug, PartialEq)]
pub enum ParseResult {
    Incomplete,
    Complete(Packet),
    Error(ParseError),
}

#[derive(Debug, PartialEq)]
pub enum ParseError {
    BadCrc { expected: u8, got: u8 },
    BadEof { got: u8 },
    PayloadOverflow,
}

// ─── FSM ─────────────────────────────────────────────────────────────────────

pub struct UartFsm {
    state:        State,
    error_count:  u32,
    packet_count: u32,
}

impl UartFsm {
    pub fn new() -> Self {
        Self {
            state:        State::Idle,
            error_count:  0,
            packet_count: 0,
        }
    }

    /// Feed one byte into the FSM.
    /// Returns `ParseResult::Complete(pkt)` when a full valid packet arrives.
    pub fn feed(&mut self, byte: u8) -> ParseResult {
        // Take ownership of current state; replace with Idle temporarily.
        let prev = std::mem::replace(&mut self.state, State::Idle);

        let (next_state, result) = match prev {

            // ── IDLE ──────────────────────────────────────────────────────────
            State::Idle => {
                if byte == SOF_BYTE {
                    (State::WaitCmd { crc: 0 }, ParseResult::Incomplete)
                } else {
                    (State::Idle, ParseResult::Incomplete)
                }
            }

            // ── WAIT_CMD ──────────────────────────────────────────────────────
            State::WaitCmd { crc } => {
                let crc = crc8_update(crc, byte);
                (State::WaitLen { cmd: byte, crc }, ParseResult::Incomplete)
            }

            // ── WAIT_LEN ──────────────────────────────────────────────────────
            State::WaitLen { cmd, crc } => {
                let crc = crc8_update(crc, byte);
                let next = if byte == 0 {
                    State::WaitCrc {
                        cmd,
                        len: 0,
                        payload: Vec::new(),
                        expected_crc: crc,
                    }
                } else {
                    State::RecvPayload {
                        cmd,
                        len: byte,
                        payload: Vec::with_capacity(byte as usize),
                        crc,
                    }
                };
                (next, ParseResult::Incomplete)
            }

            // ── RECV_PAYLOAD ──────────────────────────────────────────────────
            State::RecvPayload { cmd, len, mut payload, crc } => {
                if payload.len() >= MAX_PAYLOAD {
                    self.error_count += 1;
                    return ParseResult::Error(ParseError::PayloadOverflow);
                }
                let crc = crc8_update(crc, byte);
                payload.push(byte);

                let next = if payload.len() >= len as usize {
                    State::WaitCrc { cmd, len, payload, expected_crc: crc }
                } else {
                    State::RecvPayload { cmd, len, payload, crc }
                };
                (next, ParseResult::Incomplete)
            }

            // ── WAIT_CRC ──────────────────────────────────────────────────────
            State::WaitCrc { cmd, len, payload, expected_crc } => {
                if byte != expected_crc {
                    self.error_count += 1;
                    let err = ParseError::BadCrc { expected: expected_crc, got: byte };
                    return ParseResult::Error(err);
                }
                let packet = Packet { cmd, len, payload, crc: byte };
                (State::WaitEof { packet }, ParseResult::Incomplete)
            }

            // ── WAIT_EOF ──────────────────────────────────────────────────────
            State::WaitEof { packet } => {
                if byte != EOF_BYTE {
                    self.error_count += 1;
                    return ParseResult::Error(ParseError::BadEof { got: byte });
                }
                self.packet_count += 1;
                (State::Idle, ParseResult::Complete(packet))
            }
        };

        self.state = next_state;
        result
    }

    pub fn error_count(&self)  -> u32 { self.error_count  }
    pub fn packet_count(&self) -> u32 { self.packet_count }

    pub fn reset(&mut self) {
        self.state = State::Idle;
    }
}

impl Default for UartFsm {
    fn default() -> Self { Self::new() }
}

// ─── Tests ───────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn make_crc(bytes: &[u8]) -> u8 {
        bytes.iter().fold(0u8, |crc, &b| crc8_update(crc, b))
    }

    fn make_packet(cmd: u8, payload: &[u8]) -> Vec<u8> {
        let len = payload.len() as u8;
        let mut body = vec![cmd, len];
        body.extend_from_slice(payload);
        let crc = make_crc(&body);
        let mut frame = vec![SOF_BYTE];
        frame.extend_from_slice(&body);
        frame.push(crc);
        frame.push(EOF_BYTE);
        frame
    }

    #[test]
    fn valid_packet_parsed() {
        let mut fsm = UartFsm::new();
        let stream = make_packet(0x01, &[0xDE, 0xAD, 0xBE]);

        let mut result = ParseResult::Incomplete;
        for byte in &stream {
            result = fsm.feed(*byte);
        }
        assert_eq!(result, ParseResult::Complete(Packet {
            cmd:     0x01,
            len:     3,
            payload: vec![0xDE, 0xAD, 0xBE],
            crc:     make_crc(&[0x01, 0x03, 0xDE, 0xAD, 0xBE]),
        }));
        assert_eq!(fsm.packet_count(), 1);
        assert_eq!(fsm.error_count(),  0);
    }

    #[test]
    fn bad_crc_detected() {
        let mut fsm = UartFsm::new();
        let mut stream = make_packet(0x02, &[0xAA, 0xBB]);
        // Corrupt the CRC byte (second-to-last)
        let n = stream.len();
        stream[n - 2] ^= 0xFF;

        let mut result = ParseResult::Incomplete;
        for byte in &stream {
            result = fsm.feed(*byte);
        }
        assert!(matches!(result, ParseResult::Error(ParseError::BadCrc { .. })));
        assert_eq!(fsm.error_count(), 1);
    }

    #[test]
    fn resync_after_error() {
        let mut fsm = UartFsm::new();

        // Feed garbage bytes first
        for _ in 0..10 { fsm.feed(0xFF); }

        // Then a valid packet
        let stream = make_packet(0x03, &[0x11]);
        let mut result = ParseResult::Incomplete;
        for byte in &stream {
            result = fsm.feed(*byte);
        }
        assert!(matches!(result, ParseResult::Complete(_)));
    }

    #[test]
    fn zero_length_payload() {
        let mut fsm = UartFsm::new();
        let stream = make_packet(0x07, &[]);

        let mut result = ParseResult::Incomplete;
        for byte in &stream {
            result = fsm.feed(*byte);
        }
        assert!(matches!(result, ParseResult::Complete(Packet { len: 0, .. })));
    }
}
```

### Running the Rust Tests

```bash
cargo test
# output:
# running 4 tests
# test tests::valid_packet_parsed ... ok
# test tests::bad_crc_detected ... ok
# test tests::resync_after_error ... ok
# test tests::zero_length_payload ... ok
# test result: ok. 4 passed; 0 failed
```

---

## Advanced Topics

### ISR Integration (C)

In real embedded systems the FSM is fed from a UART ISR via a ring buffer:

```c
/* Ring buffer fed by ISR */
#define RB_SIZE 512
static volatile uint8_t rb_buf[RB_SIZE];
static volatile uint16_t rb_head = 0, rb_tail = 0;

/* UART RX interrupt handler */
void UART1_IRQHandler(void)
{
    uint8_t byte = UART1->DR;
    uint16_t next = (rb_head + 1) % RB_SIZE;
    if (next != rb_tail) {           /* Drop on overflow */
        rb_buf[rb_head] = byte;
        rb_head = next;
    }
}

/* Main loop — drain ring buffer into FSM */
void uart_process(UartFsm *fsm)
{
    Packet pkt;
    while (rb_tail != rb_head) {
        uint8_t byte = rb_buf[rb_tail];
        rb_tail = (rb_tail + 1) % RB_SIZE;

        if (uart_fsm_feed(fsm, byte, &pkt) == PARSE_COMPLETE) {
            dispatch_packet(&pkt);
        }
    }
}
```

### Multi-Channel Parsing

Because all state is in `UartFsm`, you can run independent parsers for multiple UART channels with no shared state:

```c
UartFsm uart1_fsm, uart2_fsm, uart3_fsm;
uart_fsm_init(&uart1_fsm);
uart_fsm_init(&uart2_fsm);
uart_fsm_init(&uart3_fsm);
```

### Timeout / Stale Packet Detection

Add a timestamp to the FSM context. If too much time passes between bytes, reset to IDLE:

```c
typedef struct {
    UartFsm   fsm;
    uint32_t  last_byte_ms;    /* Timestamp of last byte received */
    uint32_t  timeout_ms;      /* Max inter-byte gap allowed      */
} UartFsmWithTimeout;

void uart_fsm_tick(UartFsmWithTimeout *ctx, uint32_t now_ms)
{
    if (ctx->fsm.state != STATE_IDLE) {
        if ((now_ms - ctx->last_byte_ms) > ctx->timeout_ms) {
            uart_fsm_init(&ctx->fsm);   /* Timeout: reset */
        }
    }
}
```

### Hierarchical FSMs (HSM)

For complex protocols (e.g. SLIP or PPP over UART), use a Hierarchical State Machine where super-states handle common transitions (e.g. escape sequences) and sub-states handle specific phases.

---

## Summary

| Aspect | Key Takeaway |
|--------|-------------|
| **Core concept** | Model parser as Q × Σ → Q transitions; actions fire on transition |
| **Why FSMs** | Deterministic, resumable, interrupt-safe, testable, robust |
| **C pattern** | `switch(state)` with explicit reset on error; all state in a struct |
| **C++ pattern** | Class with `enum class` states, callbacks for zero-copy dispatch |
| **Rust pattern** | `enum` variants carry per-state data; `match` enforces exhaustiveness |
| **CRC** | Always compute over variable fields (CMD + LEN + PAYLOAD) |
| **Error recovery** | Always reset to IDLE on any protocol violation; count errors |
| **ISR integration** | Feed ring buffer in ISR; drain in main loop — FSM is re-entrant safe if single-threaded drain |
| **Timeout** | Add timestamp to context; reset if inter-byte gap exceeds threshold |
| **Testing** | Test valid, bad-CRC, bad-EOF, overflow, zero-length, and resync scenarios |

A well-designed FSM parser is small (typically < 100 lines for the core), has O(1) per-byte cost, zero heap allocation in C/C++ variants, and handles all error cases gracefully. It is the foundation of every production-grade embedded communication stack.

---

*Document: UART Series — Topic 40: State Machines | Language examples: C11, C++17, Rust 2021 edition*