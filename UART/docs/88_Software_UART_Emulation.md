# 88. Software UART Emulation

- **Protocol fundamentals** — frame structure, baud rate math, bit timing
- **Why software UART** — use cases: no hardware UART, all UARTs occupied, flexible pin routing
- **Core concepts** — TX path (trivial) vs RX path (edge detection + center-sampling)
- **C/C++ code** — bare bit-bang transmitter, interrupt-based receiver with ring buffer, and a full OOP `SoftwareUART` class using `std::function` callbacks
- **Rust code** — `embedded-hal`-based TX and RX structs, plus a full `no_std` combined implementation with atomic ring buffer and `core::fmt::Write` support for `write!/writeln!`
- **Timing** — sources of error, cycle-counter approach, DWT on Cortex-M
- **Error handling** — framing, overflow, and parity errors
- **Advanced topics** — half-duplex, multi-channel timer-driven state machines, parity support
- **Hardware vs Software comparison table**
- **Summary** — concise takeaways and practical baud rate guidance

## Implementing UART in Software When Hardware is Unavailable

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Protocol Fundamentals](#uart-protocol-fundamentals)
3. [Why Software UART?](#why-software-uart)
4. [Core Implementation Concepts](#core-implementation-concepts)
5. [C/C++ Implementation](#cc-implementation)
   - [Bit-Bang Transmitter](#bit-bang-transmitter-cc)
   - [Bit-Bang Receiver (Interrupt-Based)](#bit-bang-receiver-interrupt-based-cc)
   - [Full Software UART Class (C++)](#full-software-uart-class-c)
6. [Rust Implementation](#rust-implementation)
   - [Bit-Bang Transmitter (Rust)](#bit-bang-transmitter-rust)
   - [Bit-Bang Receiver (Rust)](#bit-bang-receiver-rust)
   - [Full Software UART (Rust, no_std)](#full-software-uart-rust-nostd)
7. [Timing Considerations](#timing-considerations)
8. [Error Handling](#error-handling)
9. [Advanced Topics](#advanced-topics)
   - [Half-Duplex Operation](#half-duplex-operation)
   - [Multiple Software UARTs](#multiple-software-uarts)
   - [Parity Support](#parity-support)
10. [Comparison: Hardware vs Software UART](#comparison-hardware-vs-software-uart)
11. [Summary](#summary)

---

## Introduction

A **Universal Asynchronous Receiver-Transmitter (UART)** is one of the oldest and most widely used serial communication protocols in embedded systems. Most microcontrollers include at least one hardware UART peripheral. However, there are situations where the hardware UART is unavailable, already in use, or the target device has none at all. In such cases, **Software UART Emulation** — also called **bit-banging** — allows developers to implement the UART protocol entirely in software using general-purpose I/O (GPIO) pins.

This document covers the theory, design patterns, and practical implementations of Software UART in both **C/C++** and **Rust**, targeting bare-metal embedded environments.

---

## UART Protocol Fundamentals

Before implementing a software UART, it is essential to understand the physical layer of the protocol.

### Frame Structure

A standard UART frame consists of:

```
Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  Parity  Stop
  1     0    ?   ?   ?   ?   ?   ?   ?   ?    opt.     1
```

- **Idle state**: The line is held HIGH when no data is being transmitted.
- **Start bit**: Always LOW (logic 0), signals the beginning of a frame.
- **Data bits**: Typically 8 bits, LSB transmitted first.
- **Parity bit** (optional): Even, odd, or none.
- **Stop bit(s)**: Always HIGH (logic 1); one or two stop bits are common.

### Baud Rate

The **baud rate** defines the number of symbols (bits) transmitted per second. Common values: 9600, 19200, 38400, 57600, 115200 bps. Each bit has a duration of:

```
bit_duration = 1 / baud_rate

Example for 9600 baud:
  bit_duration = 1 / 9600 ≈ 104.17 µs
```

Accurate timing is the single most critical requirement for software UART correctness.

---

## Why Software UART?

There are several compelling reasons to implement UART in software:

- **No hardware UART available**: Some low-cost microcontrollers (e.g., ATtiny, small PICs, some ARM Cortex-M0 variants) have limited or no UART hardware.
- **All hardware UARTs are occupied**: Complex systems may already use all available UARTs for other peripherals (GPS, GSM module, debug console).
- **Flexible pin assignment**: Software UART can run on any GPIO, freeing hardware UARTs for time-critical channels.
- **Prototyping and debugging**: Useful for quick prototyping without hardware constraints.
- **Multiple simultaneous channels**: A single microcontroller can emulate several UARTs simultaneously.

---

## Core Implementation Concepts

### Transmit Path

Transmitting is straightforward:

1. Pull TX pin LOW (start bit) for one bit period.
2. Output each data bit (LSB first), holding for one bit period each.
3. Output optional parity bit.
4. Pull TX pin HIGH (stop bit) for one or more bit periods.

### Receive Path

Receiving is more challenging and typically involves:

1. **Detecting the start bit**: Either by polling or by attaching an external interrupt to the falling edge.
2. **Sampling each bit**: After detecting the start bit, delay 1.5 bit periods to sample the center of the first data bit, then sample at 1-bit intervals thereafter.
3. **Assembling the byte**: Shift sampled bits into a buffer.
4. **Validating the stop bit**: Confirm the line is HIGH after all data bits.

> **Key insight**: Sampling at the *center* of each bit, rather than the edge, maximizes noise immunity.

---

## C/C++ Implementation

### Bit-Bang Transmitter (C/C++)

This example targets a generic embedded system. The `delay_us()` function must be calibrated for the target CPU frequency.

```c
#include <stdint.h>
#include <stdbool.h>

/* ---- Platform-specific stubs (replace with your HAL) ---- */
static inline void tx_pin_high(void) { /* Set GPIO HIGH */ }
static inline void tx_pin_low(void)  { /* Set GPIO LOW  */ }
static inline void delay_us(uint32_t us) { /* Busy-wait N microseconds */ }

/* ---- Configuration ---- */
#define BAUD_RATE    9600UL
#define BIT_DELAY_US (1000000UL / BAUD_RATE)   /* ~104 µs at 9600 baud */

/* ---- Software UART Transmit ---- */
void suart_transmit_byte(uint8_t byte)
{
    /* Start bit: TX LOW for one bit period */
    tx_pin_low();
    delay_us(BIT_DELAY_US);

    /* 8 data bits, LSB first */
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) {
            tx_pin_high();
        } else {
            tx_pin_low();
        }
        delay_us(BIT_DELAY_US);
    }

    /* Stop bit: TX HIGH for one bit period */
    tx_pin_high();
    delay_us(BIT_DELAY_US);
}

/* ---- Transmit a null-terminated string ---- */
void suart_print(const char *str)
{
    while (*str) {
        suart_transmit_byte((uint8_t)*str++);
    }
}
```

### Bit-Bang Receiver (Interrupt-Based, C/C++)

The interrupt-based receiver is triggered on the falling edge of the start bit. An internal timer or cycle counter is used to sample subsequent bits.

```c
#include <stdint.h>
#include <stdbool.h>

/* ---- Platform-specific stubs ---- */
static inline bool rx_pin_read(void)     { return false; /* Read GPIO */ }
static inline uint32_t get_cycle_count(void) { return 0; /* Read CPU cycle counter */ }

/* ---- Configuration ---- */
#define CPU_FREQ_HZ   16000000UL
#define BAUD_RATE     9600UL
#define CYCLES_PER_BIT   (CPU_FREQ_HZ / BAUD_RATE)
#define CYCLES_1_5_BITS  (CYCLES_PER_BIT + CYCLES_PER_BIT / 2)

/* ---- Receive buffer ---- */
#define RX_BUFFER_SIZE 64
static volatile uint8_t  rx_buffer[RX_BUFFER_SIZE];
static volatile uint8_t  rx_head = 0;
static volatile uint8_t  rx_tail = 0;

/* ---- Called from GPIO falling-edge ISR ---- */
void suart_rx_isr(void)
{
    uint32_t start = get_cycle_count();
    uint8_t  received = 0;

    /* Wait 1.5 bit periods to land in the center of bit 0 */
    while ((get_cycle_count() - start) < CYCLES_1_5_BITS);

    /* Sample 8 data bits */
    for (int i = 0; i < 8; i++) {
        if (rx_pin_read()) {
            received |= (1 << i);   /* LSB first */
        }
        /* Wait for the next bit center */
        start += CYCLES_PER_BIT;
        while ((get_cycle_count() - start) < CYCLES_PER_BIT);
    }

    /* Optional: verify stop bit is HIGH */
    bool stop_ok = rx_pin_read();

    if (stop_ok) {
        uint8_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next_head != rx_tail) {         /* Not full */
            rx_buffer[rx_head] = received;
            rx_head = next_head;
        }
    }
    /* Re-enable the falling-edge interrupt here */
}

/* ---- Read one byte from the receive buffer ---- */
bool suart_read_byte(uint8_t *out)
{
    if (rx_head == rx_tail) return false;   /* Buffer empty */
    *out   = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
    return true;
}
```

### Full Software UART Class (C++)

A more reusable, object-oriented approach for C++:

```cpp
#pragma once
#include <cstdint>
#include <cstdbool>
#include <functional>

class SoftwareUART {
public:
    using PinWriter  = std::function<void(bool)>;
    using PinReader  = std::function<bool()>;
    using DelayUsec  = std::function<void(uint32_t)>;

    SoftwareUART(uint32_t baud,
                 PinWriter tx_write,
                 PinReader rx_read,
                 DelayUsec delay)
        : bit_delay_us_(1'000'000UL / baud),
          tx_write_(tx_write),
          rx_read_(rx_read),
          delay_(delay)
    {
        tx_write_(true);    /* Idle HIGH */
    }

    /* --- Transmit --- */
    void write(uint8_t byte) {
        // Start bit
        tx_write_(false);
        delay_(bit_delay_us_);

        // Data bits (8N1)
        for (int i = 0; i < 8; i++) {
            tx_write_((byte >> i) & 1);
            delay_(bit_delay_us_);
        }

        // Stop bit
        tx_write_(true);
        delay_(bit_delay_us_);
    }

    void write(const char *str) {
        while (*str) write(static_cast<uint8_t>(*str++));
    }

    /* --- Receive (blocking, with timeout) --- */
    bool read(uint8_t &out, uint32_t timeout_us = 100'000UL) {
        // Wait for start bit (falling edge)
        uint32_t waited = 0;
        while (rx_read_()) {
            delay_(1);
            if (++waited > timeout_us) return false;
        }

        // Skip to center of first data bit (1.5 bit periods from start edge)
        delay_(bit_delay_us_ + bit_delay_us_ / 2);

        uint8_t received = 0;
        for (int i = 0; i < 8; i++) {
            if (rx_read_()) received |= (1 << i);
            delay_(bit_delay_us_);
        }

        // Validate stop bit
        bool valid = rx_read_();
        if (valid) {
            out = received;
            return true;
        }
        return false;
    }

private:
    uint32_t  bit_delay_us_;
    PinWriter tx_write_;
    PinReader rx_read_;
    DelayUsec delay_;
};

/* ---- Usage Example ---- */
/*
SoftwareUART uart(
    9600,
    [](bool v){ gpio_write(PIN_TX, v); },
    []() -> bool { return gpio_read(PIN_RX); },
    [](uint32_t us){ busy_wait_us(us); }
);

uart.write("Hello, World!\r\n");

uint8_t c;
if (uart.read(c)) {
    // process c
}
*/
```

---

## Rust Implementation

Rust's ownership model and zero-cost abstractions make it well-suited for safe, `no_std` embedded development. The following examples use `embedded-hal` traits for portability.

### Bit-Bang Transmitter (Rust)

```rust
#![no_std]

use embedded_hal::digital::v2::OutputPin;

pub struct SoftUartTx<TX> {
    tx: TX,
    bit_delay_cycles: u32,
}

impl<TX: OutputPin> SoftUartTx<TX> {
    /// Create a new software UART transmitter.
    /// `bit_delay_cycles` = CPU_FREQ_HZ / baud_rate
    pub fn new(mut tx: TX, bit_delay_cycles: u32) -> Self {
        let _ = tx.set_high();   // Idle HIGH
        Self { tx, bit_delay_cycles }
    }

    /// Transmit a single byte (8N1).
    pub fn write_byte(&mut self, byte: u8) {
        // Start bit
        let _ = self.tx.set_low();
        Self::delay(self.bit_delay_cycles);

        // 8 data bits, LSB first
        for i in 0..8u8 {
            if (byte >> i) & 1 == 1 {
                let _ = self.tx.set_high();
            } else {
                let _ = self.tx.set_low();
            }
            Self::delay(self.bit_delay_cycles);
        }

        // Stop bit
        let _ = self.tx.set_high();
        Self::delay(self.bit_delay_cycles);
    }

    /// Transmit a byte slice.
    pub fn write_bytes(&mut self, data: &[u8]) {
        for &b in data {
            self.write_byte(b);
        }
    }

    /// Transmit a string slice.
    pub fn write_str(&mut self, s: &str) {
        self.write_bytes(s.as_bytes());
    }

    /// Busy-wait delay (platform-specific).
    /// Replace with your HAL's cycle-accurate delay.
    #[inline(always)]
    fn delay(cycles: u32) {
        // Example: ARM Cortex-M NOP loop (approximate)
        for _ in 0..cycles {
            cortex_m::asm::nop();
        }
    }
}
```

### Bit-Bang Receiver (Rust)

```rust
#![no_std]

use embedded_hal::digital::v2::InputPin;

pub struct SoftUartRx<RX> {
    rx: RX,
    bit_delay_cycles: u32,
    half_bit_delay_cycles: u32,
}

#[derive(Debug)]
pub enum UartError {
    Timeout,
    FramingError,   // Stop bit was not HIGH
}

impl<RX: InputPin> SoftUartRx<RX> {
    pub fn new(rx: RX, bit_delay_cycles: u32) -> Self {
        Self {
            rx,
            bit_delay_cycles,
            half_bit_delay_cycles: bit_delay_cycles / 2,
        }
    }

    /// Blocking receive with timeout (in units of ~1 cycle check).
    pub fn read_byte(&mut self, timeout_cycles: u32) -> Result<u8, UartError> {
        // Wait for falling edge (start bit)
        let mut wait = 0u32;
        loop {
            if self.rx.is_low().unwrap_or(false) {
                break;
            }
            wait = wait.wrapping_add(1);
            if wait >= timeout_cycles {
                return Err(UartError::Timeout);
            }
        }

        // Delay 1.5 bit periods to hit center of bit 0
        Self::delay(self.bit_delay_cycles + self.half_bit_delay_cycles);

        // Sample 8 data bits
        let mut byte = 0u8;
        for i in 0..8u8 {
            if self.rx.is_high().unwrap_or(false) {
                byte |= 1 << i;
            }
            Self::delay(self.bit_delay_cycles);
        }

        // Verify stop bit
        if self.rx.is_low().unwrap_or(true) {
            return Err(UartError::FramingError);
        }

        Ok(byte)
    }

    #[inline(always)]
    fn delay(cycles: u32) {
        for _ in 0..cycles {
            cortex_m::asm::nop();
        }
    }
}
```

### Full Software UART (Rust, `no_std`)

A combined, ring-buffered, interrupt-capable design using `bare-metal` primitives:

```rust
#![no_std]

use core::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use embedded_hal::digital::v2::{InputPin, OutputPin};

/// Ring buffer size — must be a power of 2.
const BUFFER_SIZE: usize = 64;
const BUFFER_MASK: usize = BUFFER_SIZE - 1;

/// Shared receive buffer (filled from ISR context).
static RX_BUF:  [AtomicU8; BUFFER_SIZE] = {
    const ZERO: AtomicU8 = AtomicU8::new(0);
    [ZERO; BUFFER_SIZE]
};
static RX_HEAD: AtomicU8 = AtomicU8::new(0);
static RX_TAIL: AtomicU8 = AtomicU8::new(0);

pub struct SoftwareUart<TX, RX> {
    tx: TX,
    rx: RX,
    bit_delay: u32,
}

impl<TX: OutputPin, RX: InputPin> SoftwareUart<TX, RX> {
    pub fn new(mut tx: TX, rx: RX, bit_delay: u32) -> Self {
        let _ = tx.set_high();
        Self { tx, rx, bit_delay }
    }

    // ------------------------------------------------------------------ TX --

    pub fn send(&mut self, byte: u8) {
        let _ = self.tx.set_low();                   // Start bit
        Self::delay(self.bit_delay);

        for i in 0..8 {
            if (byte >> i) & 1 == 1 {
                let _ = self.tx.set_high();
            } else {
                let _ = self.tx.set_low();
            }
            Self::delay(self.bit_delay);
        }

        let _ = self.tx.set_high();                  // Stop bit
        Self::delay(self.bit_delay);
    }

    pub fn send_str(&mut self, s: &str) {
        for b in s.bytes() { self.send(b); }
    }

    // ------------------------------------------------------------------ RX --

    /// Call from GPIO falling-edge interrupt service routine.
    /// Returns the received byte if the frame was valid.
    pub fn isr_receive(&mut self) -> Option<u8> {
        // Skip to bit-center of first data bit (1.5 periods)
        Self::delay(self.bit_delay + self.bit_delay / 2);

        let mut byte = 0u8;
        for i in 0..8 {
            if self.rx.is_high().unwrap_or(false) {
                byte |= 1 << i;
            }
            Self::delay(self.bit_delay);
        }

        // Validate stop bit
        if self.rx.is_low().unwrap_or(true) {
            return None;   // Framing error
        }

        // Push into ring buffer
        let head = RX_HEAD.load(Ordering::Relaxed) as usize;
        let next = (head + 1) & BUFFER_MASK;
        let tail = RX_TAIL.load(Ordering::Relaxed) as usize;
        if next != tail {
            RX_BUF[head].store(byte, Ordering::Relaxed);
            RX_HEAD.store(next as u8, Ordering::Release);
            Some(byte)
        } else {
            None   // Buffer overflow — byte dropped
        }
    }

    /// Poll for a received byte (from main context).
    pub fn poll_rx(&self) -> Option<u8> {
        let tail = RX_TAIL.load(Ordering::Acquire) as usize;
        let head = RX_HEAD.load(Ordering::Acquire) as usize;
        if head == tail {
            return None;
        }
        let byte = RX_BUF[tail].load(Ordering::Relaxed);
        RX_TAIL.store(((tail + 1) & BUFFER_MASK) as u8, Ordering::Release);
        Some(byte)
    }

    // ---------------------------------------------------------- Utilities --

    #[inline(always)]
    fn delay(cycles: u32) {
        for _ in 0..cycles {
            core::hint::black_box(());   // Prevent optimizer from removing loop
        }
    }
}

// Implement core::fmt::Write for convenience with write!/writeln! macros
use core::fmt;

impl<TX: OutputPin, RX: InputPin> fmt::Write for SoftwareUart<TX, RX> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.send_str(s);
        Ok(())
    }
}

/* ---- Usage Example ----
use core::fmt::Write;

let mut uart = SoftwareUart::new(tx_pin, rx_pin, CPU_HZ / BAUD);

// Formatted output via the Write trait
writeln!(uart, "Boot count: {}", count).ok();

// In GPIO ISR:
// uart.isr_receive();

// In main loop:
if let Some(byte) = uart.poll_rx() {
    // handle byte
}
*/
```

---

## Timing Considerations

Accurate timing is the most critical aspect of software UART. Even small timing errors accumulate across bits and can cause framing errors.

### Sources of Timing Error

| Source | Impact | Mitigation |
|--------|--------|------------|
| Interrupt latency | Shifts start-bit detection | Use cycle counters, not timers |
| Compiler optimization | Removes delay loops | Use `volatile`, `black_box`, or inline ASM |
| CPU frequency variation | Scales all timings | Calibrate delay at startup |
| Interrupts during bit period | Extends bit duration | Disable global interrupts during TX/RX |
| Branch misprediction | Small random jitter | Prefer loop unrolling |

### Timing Budget

At 9600 baud, each bit is ~104 µs. A typical 8N1 frame = 10 bits = **1.04 ms**. Sampling error must remain below ±50% of a bit period. At 115200 baud, each bit is only ~8.68 µs, leaving very little margin — software UART at this speed requires a fast MCU and careful timing.

### Recommended Approach: Cycle Counter

Rather than delay functions, use a free-running CPU cycle counter (DWT_CYCCNT on ARM Cortex-M, or `RDTSC` on x86 for testing):

```c
/* ARM Cortex-M cycle-accurate delay example */
static inline void delay_cycles(uint32_t n) {
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < n);
}

/* Enable DWT cycle counter at startup */
void dwt_enable(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}
```

---

## Error Handling

Software UART should handle the following error conditions:

### Framing Error
Occurs when the stop bit is detected as LOW, indicating a bit-timing mismatch or line noise.

```c
typedef enum {
    SUART_OK           = 0,
    SUART_FRAMING_ERR  = 1,
    SUART_OVERFLOW_ERR = 2,
    SUART_TIMEOUT_ERR  = 3,
} suart_status_t;

suart_status_t suart_receive(uint8_t *out) {
    /* ... receive bits ... */
    if (!rx_pin_read()) {           /* Stop bit should be HIGH */
        return SUART_FRAMING_ERR;
    }
    *out = received;
    return SUART_OK;
}
```

### Buffer Overflow
When incoming bytes arrive faster than the application processes them:

```c
static volatile bool overflow_flag = false;

void push_to_buffer(uint8_t byte) {
    uint8_t next = (rx_head + 1) % RX_BUFFER_SIZE;
    if (next == rx_tail) {
        overflow_flag = true;   /* Drop byte, set flag */
        return;
    }
    rx_buffer[rx_head] = byte;
    rx_head = next;
}
```

### Parity Error
When optional parity is enabled, validate after the data bits:

```c
bool check_parity(uint8_t byte, bool received_parity, bool even_parity) {
    uint8_t count = __builtin_popcount(byte);   /* Count set bits */
    bool expected = even_parity ? (count % 2 == 0) : (count % 2 != 0);
    return (received_parity == expected);
}
```

---

## Advanced Topics

### Half-Duplex Operation

In half-duplex mode, TX and RX share a single wire. The MCU must switch between transmit and receive modes:

```c
typedef enum { MODE_TX, MODE_RX } uart_mode_t;
static uart_mode_t current_mode = MODE_RX;

void set_mode(uart_mode_t mode) {
    if (mode == MODE_TX) {
        /* Configure shared pin as output, pull HIGH */
        gpio_set_output(SHARED_PIN);
        gpio_write(SHARED_PIN, true);
        current_mode = MODE_TX;
    } else {
        /* Configure shared pin as input with pull-up */
        gpio_set_input_pullup(SHARED_PIN);
        /* Re-arm falling-edge interrupt */
        gpio_enable_falling_interrupt(SHARED_PIN);
        current_mode = MODE_RX;
    }
}

void half_duplex_transmit(uint8_t byte) {
    set_mode(MODE_TX);
    suart_transmit_byte(byte);
    /* Wait for line to settle, then switch back */
    delay_us(BIT_DELAY_US * 2);
    set_mode(MODE_RX);
}
```

### Multiple Software UARTs

Multiple software UARTs can be run on a single MCU by using a hardware timer to drive bit sampling for all channels concurrently. Each channel maintains its own state machine:

```c
typedef enum {
    SUART_STATE_IDLE,
    SUART_STATE_START,
    SUART_STATE_DATA,
    SUART_STATE_STOP
} suart_state_t;

typedef struct {
    uint8_t        pin;
    suart_state_t  state;
    uint8_t        bit_index;
    uint8_t        data;
    uint8_t        rx_buf[64];
    uint8_t        rx_head, rx_tail;
} suart_channel_t;

#define NUM_CHANNELS 4
static suart_channel_t channels[NUM_CHANNELS];

/* Called from a periodic timer ISR at (baud_rate) Hz */
void timer_isr(void) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        suart_channel_t *ch = &channels[i];
        switch (ch->state) {
            case SUART_STATE_IDLE:
                if (!gpio_read(ch->pin)) {      /* Start bit detected */
                    ch->state     = SUART_STATE_DATA;
                    ch->bit_index = 0;
                    ch->data      = 0;
                }
                break;
            case SUART_STATE_DATA:
                if (gpio_read(ch->pin)) ch->data |= (1 << ch->bit_index);
                if (++ch->bit_index >= 8) ch->state = SUART_STATE_STOP;
                break;
            case SUART_STATE_STOP:
                if (gpio_read(ch->pin)) {       /* Valid stop bit */
                    uint8_t next = (ch->rx_head + 1) % 64;
                    if (next != ch->rx_tail) {
                        ch->rx_buf[ch->rx_head] = ch->data;
                        ch->rx_head = next;
                    }
                }
                ch->state = SUART_STATE_IDLE;
                break;
            default: break;
        }
    }
}
```

### Parity Support

Adding optional parity to the C transmitter:

```c
typedef enum { PARITY_NONE, PARITY_EVEN, PARITY_ODD } parity_t;

void suart_transmit_parity(uint8_t byte, parity_t parity) {
    tx_pin_low();                       /* Start bit */
    delay_us(BIT_DELAY_US);

    uint8_t ones = 0;
    for (int i = 0; i < 8; i++) {
        bool bit = (byte >> i) & 1;
        if (bit) ones++;
        bit ? tx_pin_high() : tx_pin_low();
        delay_us(BIT_DELAY_US);
    }

    if (parity != PARITY_NONE) {
        bool parity_bit = (parity == PARITY_EVEN) ? (ones % 2 != 0)
                                                   : (ones % 2 == 0);
        parity_bit ? tx_pin_high() : tx_pin_low();
        delay_us(BIT_DELAY_US);
    }

    tx_pin_high();                      /* Stop bit */
    delay_us(BIT_DELAY_US);
}
```

---

## Comparison: Hardware vs Software UART

| Feature | Hardware UART | Software UART |
|---|---|---|
| **Timing accuracy** | Excellent (dedicated hardware) | Good (depends on CPU load) |
| **CPU overhead** | Minimal (DMA/interrupt) | High (busy-wait or ISR) |
| **Maximum baud rate** | Up to several Mbps | Typically ≤ 115200 bps |
| **Pin flexibility** | Fixed pins | Any GPIO |
| **Simultaneous channels** | Limited by hardware | Theoretically unlimited |
| **Interrupt sensitivity** | Not affected | Susceptible to jitter |
| **Implementation complexity** | Low (peripheral registers) | Moderate to high |
| **Power consumption** | Low | Higher (CPU active) |
| **Availability** | Hardware dependent | Any MCU with GPIO |

---

## Summary

**Software UART Emulation** (bit-banging) is a powerful technique that allows any GPIO-capable microcontroller to communicate over UART without dedicated hardware. The core principle is simple: drive a GPIO pin to mimic the UART waveform by precisely timing start bits, data bits, and stop bits according to the desired baud rate.

Key takeaways:

- **Transmit** is simple: toggle a GPIO pin with calibrated delays.
- **Receive** is harder: detect the start bit edge, then sample each bit at its center (offset 1.5 bit periods from the start bit).
- **Timing accuracy is paramount**: use cycle counters rather than software delays where possible, and disable interrupts during critical bit periods.
- **Interrupt-based RX** is preferred over polling for real applications; it decouples reception from the main loop and reduces missed bytes.
- **C/C++** implementations rely on low-level GPIO macros and delay functions, often leveraging hardware cycle counters for accuracy.
- **Rust** implementations leverage the `embedded-hal` trait abstractions for portability, and `no_std` atomics for safe ISR-to-main-loop communication.
- **Multiple software UARTs** can coexist on the same MCU using a timer-driven state machine, though the CPU overhead scales with the number of channels.
- Software UART is most reliable at **9600 to 57600 baud**; at 115200 baud and above, tight timing constraints require fast MCUs and careful optimization.

Software UART remains an indispensable tool in embedded development, enabling serial communication in resource-constrained environments where hardware peripherals are scarce or fully allocated.

---

*Document: 88 — Software UART Emulation | UART Programming Series*