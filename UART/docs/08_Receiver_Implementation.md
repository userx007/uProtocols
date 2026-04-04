# 08. UART Receiver Implementation

**Theory sections** explain *why* each technique exists — oversampling gives the receiver clock resolution to find bit centers, the start-bit falling edge re-synchronizes on every frame to eliminate drift, and majority voting across ticks 7/8/9 provides single-error correction per bit.

**C/C++ implementation** provides a complete, self-contained state machine (`uart_rx_tick()`) called at 16× baud, with majority voting, parity checking, framing error detection, a ring-buffer wrapper, and a hardware STM32 register-level example.

**Rust implementation** covers the same logic with idiomatic Rust ownership/types, including both a `std` version using `VecDeque` and a `no_std` embedded version using `heapless::Queue` for bare-metal targets.

**Advanced sections** cover interrupt-driven reception with FreeRTOS semaphores and DMA circular-buffer reception for high-throughput use cases.

## Implementing UART Receive Logic with Oversampling and Bit Synchronization

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Receiver Architecture](#uart-receiver-architecture)
3. [Oversampling Theory](#oversampling-theory)
4. [Bit Synchronization](#bit-synchronization)
5. [Start Bit Detection](#start-bit-detection)
6. [Data Sampling Strategy](#data-sampling-strategy)
7. [Stop Bit Verification](#stop-bit-verification)
8. [Error Detection](#error-detection)
9. [Implementation in C/C++](#implementation-in-cc)
10. [Implementation in Rust](#implementation-in-rust)
11. [Hardware Register-Level Implementation (C)](#hardware-register-level-implementation-c)
12. [Interrupt-Driven Reception](#interrupt-driven-reception)
13. [DMA-Assisted Reception](#dma-assisted-reception)
14. [Noise Tolerance and Majority Voting](#noise-tolerance-and-majority-voting)
15. [Summary](#summary)

---

## Introduction

The UART (Universal Asynchronous Receiver/Transmitter) receiver is fundamentally more complex than the transmitter. While a transmitter simply clocks out bits at a known rate, the receiver must:

- **Detect** the asynchronous start of a frame with no shared clock
- **Synchronize** its internal sampling clock to the incoming bit stream
- **Sample** each bit at the optimal point (the center of the bit period)
- **Tolerate** clock drift between sender and receiver
- **Detect** framing errors, parity errors, and noise

The key technique that makes all of this possible is **oversampling** — running the receiver's internal clock at a multiple of the baud rate and using that higher resolution to accurately locate the center of each received bit.

---

## UART Receiver Architecture

```
                    ┌─────────────────────────────────────────────────┐
                    │              UART Receiver                      │
                    │                                                 │
  RX Pin ──────────►│  Start Bit   Oversampling    Bit          Data  │
  (Async)           │  Detector    Counter         Sampler      Buffer│
                    │     │             │              │           │  │
                    │     ▼             ▼              ▼           ▼  │
                    │  [Edge]──►[Sync Counter]──►[Mid-Bit]──►[Shift]  │
                    │                                       Register  │
                    │                                                 │
                    │  16x Clock ─────────────────────────────────►   │
                    │  (Oversampling)                                 │
                    └─────────────────────────────────────────────────┘
```

The receiver state machine typically progresses through these states:

```
IDLE ──(start bit detected)──► SYNC ──(half bit period)──► SAMPLE
  ▲                                                           │
  └────────(stop bit OK)──── STOP ◄──(n data bits)────────────┘
```

---

## Oversampling Theory

Oversampling is the practice of running the receiver clock at **N times** the baud rate (commonly N = 16, but also 8 or even 3). This provides several benefits:

### Why 16x Oversampling?

At 16x oversampling, each bit period contains 16 sample clock ticks. This allows:

- **Accurate start bit detection** — the falling edge can be detected within 1/16th of a bit period
- **Center-bit sampling** — after detecting the start edge, wait 8 clocks to reach the center of the start bit, then sample every 16 clocks
- **Majority voting** — sample bits 7, 8, 9 (three samples around center) and take the majority

```
Bit Period (16 oversampling clocks)
│◄──────────────────────────────────►│
│                                    │
│  0  1  2  3  4  5  6  7  8  9 ...  │
│                         ▲          │
│                    Center sample   │
│                    (tick 8)        │
```

### Clock Tolerance with Oversampling

The maximum allowable frequency error between transmitter and receiver clocks:

```
Max error = (0.5 / N_bits) * 100%
```

For 8-bit data + 1 start + 1 stop = 10 bits total, with 16x oversampling:
- Error per bit: 1/16 = 6.25%
- Accumulated over 9.5 bits to the last data bit center: 0.5 / 9.5 ≈ 5.26%
- **Practical tolerance: ±2–3%** (accounting for margins)

---

## Bit Synchronization

Bit synchronization is the process of aligning the receiver's sampling instants to the transmitter's bit centers. In asynchronous UART, this re-synchronization happens **on every frame** using the start bit's falling edge.

### Synchronization Process

```
Line idle (HIGH)
        │
        ▼
Falling edge detected ──► Reset oversampling counter to 0
        │
        ▼
Wait 8 oversampling clocks ──► Sample start bit center (should be LOW)
        │
        ▼
Valid start bit? ──NO──► Return to IDLE (noise rejection)
        │YES
        ▼
Sample each data bit at: 8 + (N * 16) clocks from edge
   where N = 0, 1, 2, ... (bit index)
```

---

## Start Bit Detection

The start bit is the critical anchor for all subsequent sampling. The receiver must distinguish a genuine start bit from a noise glitch.

### Glitch Filtering

A common technique is to require the line to be LOW for at least N consecutive oversampling clocks before accepting a start bit:

```
No glitch (valid start):    ─────┐
                                 └──────────── (stays LOW)
                                 ▲
                            Falling edge

Glitch (rejected):          ─────┐  ┌──────── (returns HIGH quickly)
                                 └──┘
                                 ▲
                            Ignored
```

---

## Data Sampling Strategy

Once synchronized to the start bit, data bits are sampled at **the center of each bit period**. With 16x oversampling, this means sampling at oversampling ticks: 8, 24, 40, 56, 72, 88, 104, 120 (for 8 data bits).

```
                 Start  D0    D1    D2    D3    D4    D5    D6    D7   Stop
                ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
Line:   HIGH ───┘     │  1  │  0  │  1  │  1  │  0  │  0  │  1  │  0  └─── HIGH
                      └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
                       ▲     ▲     ▲     ▲     ▲     ▲     ▲     ▲
Samples (tick 8):      S     S     S     S     S     S     S     S
```

---

## Stop Bit Verification

After receiving all data bits and optionally a parity bit, the receiver expects the line to return HIGH for the stop bit period. If the line is LOW during the stop bit sample, a **framing error** is declared.

---

## Error Detection

| Error Type      | Cause                                          | Detection Method                          |
|-----------------|------------------------------------------------|-------------------------------------------|
| Framing Error   | Stop bit is LOW                                | Sample stop bit; check for HIGH           |
| Parity Error    | Data corruption changes bit count parity       | Recalculate parity; compare to parity bit |
| Overrun Error   | New data received before old data read         | Receive buffer full flag                  |
| Noise Error     | Majority vote does not agree (16x OS only)     | 3-sample majority disagrees               |
| Break Condition | Line held LOW for entire frame + stop bit time | Special framing error detection           |

---

## Implementation in C/C++

### Core Data Structures

```c
#include <stdint.h>
#include <stdbool.h>

/* Oversampling ratio — 16x is standard */
#define UART_OVERSAMPLE     16
#define UART_HALF_BIT       (UART_OVERSAMPLE / 2)  /* 8 */
#define UART_DATA_BITS      8
#define UART_TOTAL_BITS     (1 + UART_DATA_BITS + 1)  /* start + data + stop */

typedef enum {
    UART_RX_IDLE,           /* Waiting for start bit */
    UART_RX_START,          /* Verifying start bit */
    UART_RX_DATA,           /* Receiving data bits */
    UART_RX_PARITY,         /* Receiving parity bit (optional) */
    UART_RX_STOP,           /* Verifying stop bit */
    UART_RX_ERROR           /* Error recovery */
} uart_rx_state_t;

typedef struct {
    uart_rx_state_t state;
    uint8_t         oversample_count;  /* Current oversampling tick (0..15) */
    uint8_t         bit_index;         /* Current data bit being received (0..7) */
    uint8_t         shift_reg;         /* Incoming data shift register */
    uint8_t         rx_data;           /* Completed received byte */
    bool            rx_ready;          /* New byte available */

    /* Error flags */
    bool            framing_error;
    bool            parity_error;
    bool            overrun_error;
    bool            noise_error;

    /* Majority voting samples (ticks 7, 8, 9) */
    uint8_t         sample_buf[3];
    uint8_t         sample_idx;

    /* Parity configuration */
    bool            parity_enable;
    bool            parity_even;       /* true = even, false = odd */
} uart_rx_t;
```

### Initialization

```c
void uart_rx_init(uart_rx_t *rx, bool parity_enable, bool parity_even)
{
    rx->state            = UART_RX_IDLE;
    rx->oversample_count = 0;
    rx->bit_index        = 0;
    rx->shift_reg        = 0;
    rx->rx_data          = 0;
    rx->rx_ready         = false;
    rx->framing_error    = false;
    rx->parity_error     = false;
    rx->overrun_error    = false;
    rx->noise_error      = false;
    rx->sample_idx       = 0;
    rx->parity_enable    = parity_enable;
    rx->parity_even      = parity_even;
}
```

### Majority Voting Helper

```c
/**
 * Perform 3-sample majority vote.
 * Returns the majority bit value (0 or 1).
 * Sets *noise if all three samples are not identical.
 */
static uint8_t majority_vote(uint8_t a, uint8_t b, uint8_t c, bool *noise)
{
    uint8_t sum = a + b + c;
    *noise = (a != b) || (b != c);  /* Not all equal → noise detected */
    return (sum >= 2) ? 1 : 0;      /* Majority: 2 or 3 ones → 1 */
}
```

### Parity Calculation

```c
/**
 * Calculate even parity over a byte.
 * Returns 1 if number of set bits is odd (parity bit needed to make even).
 */
static uint8_t calc_parity(uint8_t data)
{
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return data & 0x01;
}
```

### Main Receiver State Machine (Called at 16x Baud Rate)

```c
/**
 * Process one oversampling clock tick.
 * Call this function at 16x the desired baud rate.
 *
 * @param rx      Receiver context
 * @param rx_pin  Current state of the RX pin (0 or 1)
 */
void uart_rx_tick(uart_rx_t *rx, uint8_t rx_pin)
{
    switch (rx->state) {

    /* ----------------------------------------------------------------
     * IDLE: Wait for falling edge (start bit)
     * ---------------------------------------------------------------- */
    case UART_RX_IDLE:
        if (rx_pin == 0) {
            /* Falling edge detected — possible start bit */
            rx->oversample_count = 0;
            rx->state = UART_RX_START;
        }
        break;

    /* ----------------------------------------------------------------
     * START: Verify start bit at center (tick 8)
     * ---------------------------------------------------------------- */
    case UART_RX_START:
        rx->oversample_count++;

        if (rx->oversample_count == UART_HALF_BIT) {
            /* Sample the center of the start bit */
            if (rx_pin == 0) {
                /* Valid start bit confirmed */
                rx->bit_index        = 0;
                rx->shift_reg        = 0;
                rx->sample_idx       = 0;
                rx->noise_error      = false;
                rx->oversample_count = 0;
                rx->state            = UART_RX_DATA;
            } else {
                /* Line went HIGH — was a glitch, not a start bit */
                rx->state = UART_RX_IDLE;
            }
        }
        break;

    /* ----------------------------------------------------------------
     * DATA: Sample each data bit with majority voting
     * ---------------------------------------------------------------- */
    case UART_RX_DATA:
        rx->oversample_count++;

        /* Collect three samples around the bit center: ticks 7, 8, 9 */
        if (rx->oversample_count >= (UART_HALF_BIT - 1) &&
            rx->oversample_count <= (UART_HALF_BIT + 1))
        {
            rx->sample_buf[rx->sample_idx++] = rx_pin;
        }

        if (rx->oversample_count == UART_OVERSAMPLE) {
            /* Full bit period elapsed — evaluate the bit */
            bool noise;
            uint8_t bit = majority_vote(rx->sample_buf[0],
                                        rx->sample_buf[1],
                                        rx->sample_buf[2],
                                        &noise);
            if (noise) rx->noise_error = true;

            /* LSB first: shift bit into register from the top */
            rx->shift_reg >>= 1;
            if (bit) rx->shift_reg |= 0x80;

            rx->bit_index++;
            rx->oversample_count = 0;
            rx->sample_idx       = 0;

            if (rx->bit_index == UART_DATA_BITS) {
                /* All data bits received */
                rx->state = rx->parity_enable ? UART_RX_PARITY : UART_RX_STOP;
            }
        }
        break;

    /* ----------------------------------------------------------------
     * PARITY: Sample and verify parity bit
     * ---------------------------------------------------------------- */
    case UART_RX_PARITY:
        rx->oversample_count++;

        if (rx->oversample_count >= (UART_HALF_BIT - 1) &&
            rx->oversample_count <= (UART_HALF_BIT + 1))
        {
            rx->sample_buf[rx->sample_idx++] = rx_pin;
        }

        if (rx->oversample_count == UART_OVERSAMPLE) {
            bool noise;
            uint8_t parity_bit = majority_vote(rx->sample_buf[0],
                                               rx->sample_buf[1],
                                               rx->sample_buf[2],
                                               &noise);
            if (noise) rx->noise_error = true;

            uint8_t expected_parity = calc_parity(rx->shift_reg);
            if (!rx->parity_even) expected_parity ^= 1;  /* Invert for odd parity */

            if (parity_bit != expected_parity) {
                rx->parity_error = true;
            }

            rx->oversample_count = 0;
            rx->sample_idx       = 0;
            rx->state            = UART_RX_STOP;
        }
        break;

    /* ----------------------------------------------------------------
     * STOP: Verify stop bit (must be HIGH)
     * ---------------------------------------------------------------- */
    case UART_RX_STOP:
        rx->oversample_count++;

        if (rx->oversample_count == UART_HALF_BIT) {
            /* Sample center of stop bit */
            if (rx_pin == 1) {
                /* Valid stop bit */
                if (rx->rx_ready) {
                    rx->overrun_error = true;  /* Previous byte not read */
                }
                rx->rx_data  = rx->shift_reg;
                rx->rx_ready = true;
            } else {
                /* Stop bit is LOW — framing error */
                rx->framing_error = true;
            }
            rx->state = UART_RX_IDLE;
        }
        break;

    case UART_RX_ERROR:
        /* Wait for line to return HIGH before re-entering IDLE */
        if (rx_pin == 1) {
            rx->state = UART_RX_IDLE;
        }
        break;
    }
}
```

### Reading Received Data

```c
/**
 * Read a received byte from the UART receiver.
 *
 * @param rx    Receiver context
 * @param data  Output: received byte (valid only if return is true)
 * @return      true if a byte was available, false otherwise
 */
bool uart_rx_read(uart_rx_t *rx, uint8_t *data)
{
    if (!rx->rx_ready) return false;

    *data        = rx->rx_data;
    rx->rx_ready = false;
    return true;
}

/**
 * Check and clear error flags.
 * Returns combined error bitmask.
 */
uint8_t uart_rx_get_errors(uart_rx_t *rx)
{
    uint8_t errors = 0;
    if (rx->framing_error) errors |= (1 << 0);
    if (rx->parity_error)  errors |= (1 << 1);
    if (rx->overrun_error) errors |= (1 << 2);
    if (rx->noise_error)   errors |= (1 << 3);

    /* Clear all error flags */
    rx->framing_error = false;
    rx->parity_error  = false;
    rx->overrun_error = false;
    rx->noise_error   = false;

    return errors;
}
```

### Receive Buffer with Ring Buffer

```c
#define RX_BUFFER_SIZE  64  /* Must be a power of 2 */

typedef struct {
    uart_rx_t   uart;
    uint8_t     buf[RX_BUFFER_SIZE];
    uint16_t    head;
    uint16_t    tail;
} uart_rx_buffered_t;

void uart_rx_buf_init(uart_rx_buffered_t *ctx)
{
    uart_rx_init(&ctx->uart, false, false);
    ctx->head = 0;
    ctx->tail = 0;
}

/* Call from ISR or tick loop */
void uart_rx_buf_tick(uart_rx_buffered_t *ctx, uint8_t rx_pin)
{
    uart_rx_tick(&ctx->uart, rx_pin);

    uint8_t data;
    if (uart_rx_read(&ctx->uart, &data)) {
        uint16_t next = (ctx->head + 1) & (RX_BUFFER_SIZE - 1);
        if (next != ctx->tail) {
            ctx->buf[ctx->head] = data;
            ctx->head = next;
        } else {
            /* Buffer full — overrun */
        }
    }
}

/* Call from application */
bool uart_rx_buf_get(uart_rx_buffered_t *ctx, uint8_t *data)
{
    if (ctx->head == ctx->tail) return false;
    *data      = ctx->buf[ctx->tail];
    ctx->tail  = (ctx->tail + 1) & (RX_BUFFER_SIZE - 1);
    return true;
}
```

---

## Implementation in Rust

Rust's ownership model and type system make UART receiver implementations safer and more expressive.

### Core Types

```rust
/// Oversampling ratio
const OVERSAMPLE: u8 = 16;
const HALF_BIT: u8 = OVERSAMPLE / 2;
const DATA_BITS: u8 = 8;

/// Parity configuration
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Parity {
    None,
    Even,
    Odd,
}

/// Receiver state machine states
#[derive(Debug, Clone, Copy, PartialEq)]
enum RxState {
    Idle,
    Start,
    Data,
    ParityBit,
    Stop,
}

/// Accumulated error flags (bitfield-style)
#[derive(Debug, Default, Clone, Copy)]
pub struct RxErrors {
    pub framing: bool,
    pub parity:  bool,
    pub overrun: bool,
    pub noise:   bool,
}

impl RxErrors {
    pub fn any(&self) -> bool {
        self.framing || self.parity || self.overrun || self.noise
    }
}

/// Core UART receiver context
pub struct UartReceiver {
    state:            RxState,
    oversample_count: u8,
    bit_index:        u8,
    shift_reg:        u8,
    rx_data:          Option<u8>,
    errors:           RxErrors,
    sample_buf:       [u8; 3],
    sample_idx:       usize,
    parity:           Parity,
}
```

### Initialization and Helpers

```rust
impl UartReceiver {
    /// Create a new UART receiver with the specified parity setting.
    pub fn new(parity: Parity) -> Self {
        Self {
            state:            RxState::Idle,
            oversample_count: 0,
            bit_index:        0,
            shift_reg:        0,
            rx_data:          None,
            errors:           RxErrors::default(),
            sample_buf:       [0; 3],
            sample_idx:       0,
            parity,
        }
    }

    /// Perform majority vote on three samples.
    /// Returns (voted_bit, noise_detected).
    fn majority_vote(samples: &[u8; 3]) -> (u8, bool) {
        let sum: u8 = samples.iter().sum();
        let noise = samples[0] != samples[1] || samples[1] != samples[2];
        (if sum >= 2 { 1 } else { 0 }, noise)
    }

    /// Calculate even parity of a byte.
    fn even_parity(data: u8) -> u8 {
        let mut x = data;
        x ^= x >> 4;
        x ^= x >> 2;
        x ^= x >> 1;
        x & 1
    }

    /// Reset oversampling state for a new bit period.
    fn reset_bit(&mut self) {
        self.oversample_count = 0;
        self.sample_idx = 0;
    }

    /// Collect majority-vote samples at ticks 7, 8, 9.
    fn collect_sample(&mut self, pin: u8) {
        let tick = self.oversample_count;
        if tick >= HALF_BIT - 1 && tick <= HALF_BIT + 1 {
            if self.sample_idx < 3 {
                self.sample_buf[self.sample_idx] = pin;
                self.sample_idx += 1;
            }
        }
    }
```

### State Machine Tick Function

```rust
    /// Process one oversampling clock tick.
    /// Call at 16× the desired baud rate.
    ///
    /// Returns `Some(byte)` when a complete, error-free frame is received.
    pub fn tick(&mut self, rx_pin: u8) -> Option<u8> {
        match self.state {
            // ----------------------------------------------------------
            // IDLE: wait for falling edge
            // ----------------------------------------------------------
            RxState::Idle => {
                if rx_pin == 0 {
                    self.reset_bit();
                    self.state = RxState::Start;
                }
                None
            }

            // ----------------------------------------------------------
            // START: verify start bit at center (tick 8)
            // ----------------------------------------------------------
            RxState::Start => {
                self.oversample_count += 1;
                if self.oversample_count == HALF_BIT {
                    if rx_pin == 0 {
                        // Valid start bit
                        self.bit_index  = 0;
                        self.shift_reg  = 0;
                        self.errors     = RxErrors::default();
                        self.state      = RxState::Data;
                        self.reset_bit();
                    } else {
                        // Glitch — return to idle
                        self.state = RxState::Idle;
                    }
                }
                None
            }

            // ----------------------------------------------------------
            // DATA: receive bits with majority voting
            // ----------------------------------------------------------
            RxState::Data => {
                self.oversample_count += 1;
                self.collect_sample(rx_pin);

                if self.oversample_count == OVERSAMPLE {
                    let (bit, noise) = Self::majority_vote(&self.sample_buf);
                    if noise { self.errors.noise = true; }

                    // LSB first
                    self.shift_reg >>= 1;
                    if bit == 1 { self.shift_reg |= 0x80; }

                    self.bit_index += 1;
                    self.reset_bit();

                    if self.bit_index == DATA_BITS {
                        self.state = match self.parity {
                            Parity::None => RxState::Stop,
                            _            => RxState::ParityBit,
                        };
                    }
                }
                None
            }

            // ----------------------------------------------------------
            // PARITY: verify parity bit
            // ----------------------------------------------------------
            RxState::ParityBit => {
                self.oversample_count += 1;
                self.collect_sample(rx_pin);

                if self.oversample_count == OVERSAMPLE {
                    let (parity_bit, noise) = Self::majority_vote(&self.sample_buf);
                    if noise { self.errors.noise = true; }

                    let expected = match self.parity {
                        Parity::Even => Self::even_parity(self.shift_reg),
                        Parity::Odd  => Self::even_parity(self.shift_reg) ^ 1,
                        Parity::None => unreachable!(),
                    };

                    if parity_bit != expected {
                        self.errors.parity = true;
                    }

                    self.reset_bit();
                    self.state = RxState::Stop;
                }
                None
            }

            // ----------------------------------------------------------
            // STOP: verify stop bit and emit data
            // ----------------------------------------------------------
            RxState::Stop => {
                self.oversample_count += 1;

                if self.oversample_count == HALF_BIT {
                    self.state = RxState::Idle;
                    if rx_pin == 1 {
                        // Valid stop bit — return byte if no errors
                        if !self.errors.any() {
                            return Some(self.shift_reg);
                        }
                    } else {
                        self.errors.framing = true;
                    }
                }
                None
            }
        }
    }

    /// Take the current error flags, resetting them afterward.
    pub fn take_errors(&mut self) -> RxErrors {
        let e = self.errors;
        self.errors = RxErrors::default();
        e
    }
}
```

### Receive Buffer using VecDeque

```rust
use std::collections::VecDeque;

pub struct BufferedUartRx {
    receiver: UartReceiver,
    buffer:   VecDeque<u8>,
    capacity: usize,
}

impl BufferedUartRx {
    pub fn new(parity: Parity, capacity: usize) -> Self {
        Self {
            receiver: UartReceiver::new(parity),
            buffer:   VecDeque::with_capacity(capacity),
            capacity,
        }
    }

    /// Drive from ISR or timer tick (called at 16× baud rate).
    pub fn tick(&mut self, rx_pin: u8) {
        if let Some(byte) = self.receiver.tick(rx_pin) {
            if self.buffer.len() < self.capacity {
                self.buffer.push_back(byte);
            } else {
                self.receiver.errors.overrun = true;
            }
        }
    }

    /// Read next byte from the buffer (application side).
    pub fn read(&mut self) -> Option<u8> {
        self.buffer.pop_front()
    }

    /// Peek at bytes without consuming.
    pub fn peek(&self) -> Option<u8> {
        self.buffer.front().copied()
    }

    pub fn available(&self) -> usize {
        self.buffer.len()
    }

    pub fn errors(&mut self) -> RxErrors {
        self.receiver.take_errors()
    }
}
```

### no_std Embedded Rust (heapless ring buffer)

```rust
// Cargo.toml: heapless = "0.8"
#![no_std]
use heapless::spsc::Queue;

const RX_QUEUE_SIZE: usize = 64;

pub struct EmbeddedUartRx {
    receiver: UartReceiver,
    queue:    Queue<u8, RX_QUEUE_SIZE>,
}

impl EmbeddedUartRx {
    pub fn new(parity: Parity) -> Self {
        Self {
            receiver: UartReceiver::new(parity),
            queue:    Queue::new(),
        }
    }

    /// Called from timer ISR at 16× baud
    pub fn isr_tick(&mut self, rx_pin: u8) {
        if let Some(byte) = self.receiver.tick(rx_pin) {
            // Silently drop on overflow — could set overrun flag instead
            let _ = self.queue.enqueue(byte);
        }
    }

    /// Called from application task
    pub fn read(&mut self) -> Option<u8> {
        self.queue.dequeue()
    }
}
```

---

## Hardware Register-Level Implementation (C)

On a real microcontroller (STM32 example), the UART receiver is implemented in hardware with registers:

```c
#include "stm32f4xx.h"

/* USART2 receiver initialization at 115200 baud, 16× oversampling */
void usart2_rx_init(void)
{
    /* 1. Enable clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;   /* GPIOA clock */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;  /* USART2 clock */

    /* 2. Configure PA3 as USART2_RX (AF7) */
    GPIOA->MODER   &= ~(3U << (3 * 2));
    GPIOA->MODER   |=  (2U << (3 * 2));    /* Alternate function */
    GPIOA->AFR[0]  &= ~(0xFU << (3 * 4));
    GPIOA->AFR[0]  |=  (7U   << (3 * 4)); /* AF7 = USART2 */
    GPIOA->PUPDR   &= ~(3U << (3 * 2));
    GPIOA->PUPDR   |=  (1U << (3 * 2));   /* Pull-up on RX */

    /* 3. Configure USART2 */
    USART2->CR1 = 0;                        /* Reset */
    USART2->CR2 = 0;
    USART2->CR3 = 0;

    /* Baud rate: BRR = fCLK / (16 * baud) = 16MHz / 115200 ≈ 138.9
     * Integer part: 138, Fractional part: 0.9 * 16 ≈ 14 → BRR = (138 << 4) | 14 */
    USART2->BRR = (138U << 4) | 14U;

    /* 4. Enable receiver + RXNE interrupt */
    USART2->CR1 |= USART_CR1_RE;           /* Receiver enable */
    USART2->CR1 |= USART_CR1_RXNEIE;       /* RXNE interrupt enable */
    NVIC_EnableIRQ(USART2_IRQn);
    NVIC_SetPriority(USART2_IRQn, 5);

    /* 5. Enable USART */
    USART2->CR1 |= USART_CR1_UE;
}

/* Ring buffer for received data */
#define RX_BUF_SIZE 256
static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* USART2 IRQ handler */
void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;

    if (sr & USART_SR_RXNE) {
        uint8_t data = (uint8_t)(USART2->DR & 0xFF);

        if (sr & USART_SR_FE) {
            /* Framing error — reading DR clears it */
        } else if (sr & USART_SR_PE) {
            /* Parity error */
        } else if (sr & USART_SR_NE) {
            /* Noise error */
        } else if (sr & USART_SR_ORE) {
            /* Overrun — data lost */
        } else {
            /* Good data — enqueue it */
            uint16_t next = (rx_head + 1) & (RX_BUF_SIZE - 1);
            if (next != rx_tail) {
                rx_buf[rx_head] = data;
                rx_head = next;
            }
        }
    }
}

/* Application-level read */
bool usart2_rx_read(uint8_t *data)
{
    if (rx_head == rx_tail) return false;
    *data    = rx_buf[rx_tail];
    rx_tail  = (rx_tail + 1) & (RX_BUF_SIZE - 1);
    return true;
}
```

---

## Interrupt-Driven Reception

Interrupt-driven reception keeps the CPU free until data actually arrives:

```c
/* Using a semaphore or event flag in an RTOS context (FreeRTOS example) */
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t rx_semaphore;
static uint8_t           rx_ring[256];
static volatile uint16_t rx_wr = 0;
static volatile uint16_t rx_rd = 0;

void uart_rx_rtos_init(void)
{
    rx_semaphore = xSemaphoreCreateCounting(256, 0);
    usart2_rx_init();  /* See above */
}

/* Called from ISR — note use of FromISR variant */
void USART2_IRQHandler(void)
{
    if (USART2->SR & USART_SR_RXNE) {
        uint8_t byte = USART2->DR;
        uint16_t next = (rx_wr + 1) & 0xFF;
        if (next != rx_rd) {
            rx_ring[rx_wr] = byte;
            rx_wr = next;
        }
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(rx_semaphore, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

/* Task waits for data */
void uart_rx_task(void *arg)
{
    uint8_t data;
    for (;;) {
        if (xSemaphoreTake(rx_semaphore, portMAX_DELAY) == pdTRUE) {
            data = rx_ring[rx_rd];
            rx_rd = (rx_rd + 1) & 0xFF;
            /* Process data */
        }
    }
}
```

---

## DMA-Assisted Reception

For high-speed or continuous data streams, DMA eliminates the CPU overhead of handling individual bytes:

```c
/* STM32 DMA circular buffer UART reception */
#define DMA_BUF_SIZE  256

static uint8_t dma_buf[DMA_BUF_SIZE];
static uint16_t dma_last_pos = 0;

void usart2_dma_rx_init(void)
{
    /* Configure USART2 as above but enable DMA request */
    USART2->CR3 |= USART_CR3_DMAR;

    /* Configure DMA1 Stream5 (USART2 RX) */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;

    DMA1_Stream5->CR = 0;
    while (DMA1_Stream5->CR & DMA_SxCR_EN);  /* Wait for disable */

    DMA1_Stream5->PAR  = (uint32_t)&USART2->DR;
    DMA1_Stream5->M0AR = (uint32_t)dma_buf;
    DMA1_Stream5->NDTR = DMA_BUF_SIZE;

    DMA1_Stream5->CR =
        (4U << DMA_SxCR_CHSEL_Pos) |   /* Channel 4 = USART2_RX */
        DMA_SxCR_MINC                |  /* Memory increment */
        DMA_SxCR_CIRC                |  /* Circular mode */
        DMA_SxCR_TCIE;                  /* Transfer complete interrupt */

    DMA1_Stream5->CR |= DMA_SxCR_EN;   /* Enable DMA */
}

/**
 * Called periodically (or from DMA half/full interrupt) to process
 * newly received bytes from the circular buffer.
 */
void usart2_dma_process(void)
{
    /* DMA writes forward; NDTR counts down from DMA_BUF_SIZE */
    uint16_t dma_pos = DMA_BUF_SIZE - (uint16_t)DMA1_Stream5->NDTR;

    if (dma_pos != dma_last_pos) {
        if (dma_pos > dma_last_pos) {
            /* Linear region */
            process_bytes(&dma_buf[dma_last_pos], dma_pos - dma_last_pos);
        } else {
            /* Wrapped around */
            process_bytes(&dma_buf[dma_last_pos], DMA_BUF_SIZE - dma_last_pos);
            process_bytes(&dma_buf[0], dma_pos);
        }
        dma_last_pos = dma_pos;
    }
}
```

---

## Noise Tolerance and Majority Voting

At higher baud rates or in electrically noisy environments, individual sample points may be corrupted. The three-sample majority vote (at ticks N-1, N, N+1 of each bit center) dramatically improves noise tolerance.

### Majority Vote Analysis

```
Scenario           Samples     Vote Result    Noise Flag
─────────────────────────────────────────────────────────
Clean '1'          1, 1, 1     → 1            No
Clean '0'          0, 0, 0     → 0            No
'1' with 1 noise   1, 0, 1     → 1 (correct)  Yes
'0' with 1 noise   0, 1, 0     → 0 (correct)  Yes
Heavy noise (2)    1, 0, 0     → 0 (may be wrong) Yes
Heavy noise (2)    0, 1, 1     → 1 (may be wrong) Yes
```

With one noise hit per bit (SNR sufficient), the majority vote always recovers the correct bit while flagging the noise condition. This is a **Single Error Correction** mechanism within each bit period.

### BER Improvement

For a given bit error probability `p`, the probability that the majority vote produces a wrong output is:

```
P_error = 3p² - 2p³   (three independent samples)
```

For p = 0.05 (5% per sample):  P_error = 3(0.0025) - 2(0.000125) = 0.0073 → ~7× improvement.

---

## Summary

UART receiver implementation centers on four interlocking concepts:

**Oversampling** is the foundation: by running the receiver at 16× (or 8×) the baud rate, the receiver gains fine-grained resolution over each bit period. This resolution allows accurate edge detection and optimal sample-point placement without any shared clock signal.

**Bit synchronization** exploits the mandatory falling edge of the UART start bit to re-anchor the receiver's internal counter on every frame. This eliminates accumulated clock drift and means each character is independently synchronized — a key property of asynchronous communication.

**Majority voting** uses the oversampling resolution to take three measurements around the nominal bit center (ticks 7, 8, 9 of a 16-tick period) and selects the majority value. This single-error-correction mechanism gives robust reception in noisy environments without any additional hardware.

**Error detection** — framing errors (invalid stop bit), parity errors, overrun errors, and noise flags — provides the application layer with actionable feedback about transmission integrity.

In practice, most microcontroller UART peripherals implement all four of these mechanisms in dedicated silicon. Understanding the underlying algorithms is essential when:
- Implementing a software UART (bit-banging)
- Debugging reception failures at the register level
- Designing systems that operate near the noise or baud-rate tolerance limits
- Porting UART drivers between different hardware platforms

Both the C/C++ and Rust implementations shown here faithfully model the hardware behavior, making them suitable for software-only UART on GPIO pins, for simulation and testing, or as reference implementations for understanding real hardware peripherals.

---

*End of Document: 08. Receiver Implementation*